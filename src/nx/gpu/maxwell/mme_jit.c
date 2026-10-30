/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The macros mme_hle.c has no implementation for, compiled to host code: mme.c chooses one by the hash of its code
   (mme_choose), and the first call of a hash emits the macro's instructions as a function, compiles it with the LLVM
   backend (common/cpu/recompiler/llvm) and loads it into this process with the runtime JIT's loader. The function
   calls mme_out and mme_reg, as the interpreter does, so the methods it sends and the registers it reads are the ones
   the stream and the engine see from the interpreter, and mme_verify can compare the two.
   The control flow is the interpreter's, not a dispatch loop: the instruction words are constants, so a branch is a
   direct jump, the registers are values, r0 is zero and a fetch past the last parameter is zero. An exit's delay slot
   ends the call, a taken branch's delay slot runs before the branch's target, and a branch in a delay slot fetches
   the target of the branch before it before its own target, as step() does. A method that makes the stream wait
   (ch->park) stops the macro after the step that sent it: the state goes back to ch->mme and mme_resume continues in
   the interpreter from there, so a compiled macro needs no resume machinery of its own.
   A macro whose code the compiler can't follow (an unknown operation or ALU, a branch outside the upload batch, a
   different upload in the middle of it) has no compiled form and the interpreter runs it, as it did every macro
   before. mme_jit=0 turns the compiled forms off. */
#include "nx/gpu/maxwell/engines_int.h"
#include "common/cpu/recompiler/llvm/code_loader.h"
#include "common/cpu/recompiler/llvm/ir.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/tracy.h"

/* the functions' -O level: they are small, and the first call of a macro waits for its compilation */
#define MME_JIT_OPT '1'

typedef int (*MmeJitFn)(GpuChannel* ch, MmeRec* rec, u32 np, u32 start);

/* A point in the macro's control flow: the word the next step fetches (off, relative to the macro's start), the flags
   it starts with, and for a branch's delay slot the word the branch goes to after it. N is an ordinary instruction, S a
   taken branch's delay slot, X an exit's delay slot (the call ends after it), XS both, and END is the state a call
   returns in (running 0) */
enum { MME_JIT_N, MME_JIT_S, MME_JIT_X, MME_JIT_XS, MME_JIT_END };
typedef struct {
    u32 off, target;
    u8 mode;
} JitNode;

typedef struct {
    SnxIr* ir;
    LLVMValueRef ch, rec, np, start; /* the function's parameters */
    LLVMValueRef regs[8], carry, method, incr, next, steps; /* the macro's state, as values */
    LLVMValueRef out, reg, cutoff;                          /* mme_out, mme_reg, mme_jit_cutoff */
    LLVMTypeRef out_type, reg_type, cutoff_type;            /* their function types */
    const u32* code;                                        /* the channel's macro memory */
    u32 start_word, n;                                      /* the macro's first word and length in it */
    JitNode* nodes;
    LLVMBasicBlockRef* blocks;
    u32 nnodes, cap;
    LLVMBasicBlockRef bad;
    int failed;
} Jit;

/* A function of the runtime the macro calls. PowerPC64: loaded code keeps the arena's TOC in r2 and calls nothing
   outside the arena by name (code_loader.h), so it calls the runtime's functions through their addresses, which LLVM
   does with r2 saved around the call and the callee's own TOC set up. Elsewhere a call by name the loader resolves */
static LLVMValueRef jit_callee(SnxIr* ir, const char* name, SnxU64 address, LLVMTypeRef* type, LLVMTypeRef ret,
                               LLVMTypeRef* params, unsigned count) {
    *type = LLVMFunctionType(ret, params, count, 0);
#if defined(__powerpc64__)
    (void)name;
    return LLVMConstIntToPtr(ir_i64(ir, address), ir->t_ptr);
#else
    (void)address;
    return ir_extern(ir, name, ret, params, count);
#endif
}
static LLVMValueRef jit_call(Jit* j, LLVMValueRef callee, LLVMTypeRef type, LLVMValueRef* args, unsigned count) {
    ir_state_invalidate(j->ir);
    return LLVMBuildCall2(j->ir->b, type, callee, args, count, "");
}

#define MME_FIELD(field) (offsetof(GpuChannel, mme) + offsetof(GpuMme, field))

static LLVMValueRef jit_get(Jit* j, LLVMValueRef slot) { return ir_load(j->ir, j->ir->t_i32, slot); }
static void jit_set(Jit* j, LLVMValueRef slot, LLVMValueRef value) { ir_store(j->ir, value, slot); }

/* the channel's field at an offset */
static LLVMValueRef jit_ptr(Jit* j, size_t offset) {
    LLVMValueRef index = ir_i64(j->ir, (SnxU64)offset);
    return LLVMBuildGEP2(j->ir->b, j->ir->t_i8, j->ch, &index, 1, "");
}
static LLVMValueRef jit_load(Jit* j, size_t offset) {
    LLVMValueRef pointer = LLVMBuildBitCast(j->ir->b, jit_ptr(j, offset), LLVMPointerType(j->ir->t_i32, 0), "");
    return ir_load(j->ir, j->ir->t_i32, pointer);
}
static void jit_store(Jit* j, size_t offset, LLVMValueRef value) {
    LLVMValueRef pointer = LLVMBuildBitCast(j->ir->b, jit_ptr(j, offset), LLVMPointerType(j->ir->t_i32, 0), "");
    ir_store(j->ir, value, pointer);
}

/* fetch_param(): the next call parameter, or 0 past the last one, and the next one moves on only when it was there */
static LLVMValueRef jit_fetch(Jit* j) {
    LLVMBuilderRef b = j->ir->b;
    LLVMValueRef next = jit_get(j, j->next);
    LLVMValueRef inside = LLVMBuildICmp(b, LLVMIntULT, next, j->np, "");
    LLVMValueRef index = LLVMBuildSelect(b, inside, next, ir_i32(j->ir, 0), "");
    LLVMValueRef base = jit_ptr(j, offsetof(GpuChannel, mme_params));
    LLVMValueRef offset = LLVMBuildZExt(b, index, j->ir->t_i64, "");
    LLVMValueRef pointer = LLVMBuildGEP2(b, j->ir->t_i32, base, &offset, 1, "");
    LLVMValueRef value = ir_load(j->ir, j->ir->t_i32, pointer);
    LLVMValueRef bumped = LLVMBuildAdd(b, next, ir_i32(j->ir, 1), "");
    jit_set(j, j->next, LLVMBuildSelect(b, inside, bumped, next, ""));
    return LLVMBuildSelect(b, inside, value, ir_i32(j->ir, 0), "");
}

/* set_method(): the method in bits 0-11, the increment after each send in bits 12-17 */
static void jit_method(Jit* j, LLVMValueRef value) {
    LLVMBuilderRef b = j->ir->b;
    jit_set(j, j->method, LLVMBuildAnd(b, value, ir_i32(j->ir, 0xFFF), ""));
    jit_set(j, j->incr, LLVMBuildAnd(b, LLVMBuildLShr(b, value, ir_i32(j->ir, 12), ""), ir_i32(j->ir, 0x3F), ""));
}

/* send(): the method address moves on by the increment, then the method goes out */
static void jit_send(Jit* j, LLVMValueRef value) {
    LLVMBuilderRef b = j->ir->b;
    LLVMValueRef at = jit_get(j, j->method);
    LLVMValueRef next = LLVMBuildAdd(b, at, jit_get(j, j->incr), "");
    LLVMValueRef args[4];
    jit_set(j, j->method, LLVMBuildAnd(b, next, ir_i32(j->ir, 0xFFF), ""));
    args[0] = j->ch;
    args[1] = j->rec;
    args[2] = at;
    args[3] = value;
    jit_call(j, j->out, j->out_type, args, 4);
}

static LLVMValueRef jit_reg(Jit* j, u32 r) { return r ? jit_get(j, j->regs[r]) : ir_i32(j->ir, 0); }
static void jit_set_reg(Jit* j, u32 r, LLVMValueRef value) {
    if (r) jit_set(j, j->regs[r], value);
}

/* alu(): the result, and the carry of an add or subtract, as mme.c's alu() */
static LLVMValueRef jit_alu(Jit* j, u32 op, LLVMValueRef a, LLVMValueRef b) {
    LLVMBuilderRef bld = j->ir->b;
    LLVMValueRef x, y, r, carry;
    switch (op) {
        case 0:
        case 1:
        case 2:
        case 3:
            x = LLVMBuildZExt(bld, a, j->ir->t_i64, "");
            y = LLVMBuildZExt(bld, b, j->ir->t_i64, "");
            if (op == 0)
                r = LLVMBuildAdd(bld, x, y, "");
            else if (op == 1)
                r = LLVMBuildAdd(bld, LLVMBuildAdd(bld, x, y, ""),
                                 LLVMBuildZExt(bld, jit_get(j, j->carry), j->ir->t_i64, ""), "");
            else if (op == 2)
                r = LLVMBuildSub(bld, x, y, "");
            else {
                LLVMValueRef borrow = LLVMBuildZExt(
                    bld, LLVMBuildICmp(bld, LLVMIntEQ, jit_get(j, j->carry), ir_i32(j->ir, 0), ""), j->ir->t_i64,
                    "");
                r = LLVMBuildSub(bld, LLVMBuildSub(bld, x, y, ""), borrow, "");
            }
            if (op == 2 || op == 3)
                carry = LLVMBuildZExt(
                    bld, LLVMBuildICmp(bld, LLVMIntULT, r, ir_i64(j->ir, 0x100000000ull), ""), j->ir->t_i32, "");
            else
                carry = LLVMBuildZExt(
                    bld, LLVMBuildICmp(bld, LLVMIntNE, LLVMBuildLShr(bld, r, ir_i64(j->ir, 32), ""),
                                       ir_i64(j->ir, 0), ""),
                    j->ir->t_i32, "");
            jit_set(j, j->carry, carry);
            return LLVMBuildTrunc(bld, r, j->ir->t_i32, "");
        case 8: return LLVMBuildXor(bld, a, b, "");
        case 9: return LLVMBuildOr(bld, a, b, "");
        case 10: return LLVMBuildAnd(bld, a, b, "");
        case 11: return LLVMBuildAnd(bld, a, LLVMBuildNot(bld, b, ""), "");
        case 12: return LLVMBuildNot(bld, LLVMBuildAnd(bld, a, b, ""), "");
        default:
            j->failed = 1;
            return ir_i32(j->ir, 0);
    }
}

/* result(): what the step does with its value, as mme.c's result() */
static void jit_result(Jit* j, u32 rop, u32 dst, LLVMValueRef value) {
    switch (rop) {
        case 0: /* the value is unused, the fetched parameter is the result */
            jit_set_reg(j, dst, jit_fetch(j));
            break;
        case 1: jit_set_reg(j, dst, value); break;
        case 2:
            jit_set_reg(j, dst, value);
            jit_method(j, value);
            break;
        case 3:
            jit_set_reg(j, dst, jit_fetch(j));
            jit_send(j, value);
            break;
        case 4:
            jit_set_reg(j, dst, value);
            jit_send(j, value);
            break;
        case 5:
            jit_set_reg(j, dst, jit_fetch(j));
            jit_method(j, value);
            break;
        case 6:
            jit_set_reg(j, dst, value);
            jit_method(j, value);
            jit_send(j, jit_fetch(j));
            break;
        default:
            jit_set_reg(j, dst, value);
            jit_method(j, value);
            jit_send(j, LLVMBuildAnd(j->ir->b, LLVMBuildLShr(j->ir->b, value, ir_i32(j->ir, 12), ""),
                                     ir_i32(j->ir, 0x3F), ""));
            break;
    }
}

/* the state a park or the end returns in: the next instruction and the flags it starts with */
static void jit_state_out(Jit* j, u32 pc, u32 target, u32 mode, int running) {
    LLVMBuilderRef b = j->ir->b;
    u32 branch = mode == MME_JIT_S || mode == MME_JIT_XS;
    u32 slot = mode == MME_JIT_S || mode == MME_JIT_X || mode == MME_JIT_XS;
    u32 exiting = mode == MME_JIT_X || mode == MME_JIT_XS;
    LLVMValueRef address = LLVMBuildAnd(b, LLVMBuildAdd(b, j->start, ir_i32(j->ir, pc), ""), ir_i32(j->ir, 0x1FFF), "");
    LLVMValueRef goal = branch ? LLVMBuildAnd(
                                     b, LLVMBuildAdd(b, j->start, ir_i32(j->ir, target), ""), ir_i32(j->ir, 0x1FFF), "")
                               : ir_i32(j->ir, 0);
    u32 r;
    jit_store(j, MME_FIELD(pc), address);
    jit_store(j, MME_FIELD(target), goal);
    jit_store(j, MME_FIELD(np), j->np);
    jit_store(j, MME_FIELD(next), jit_get(j, j->next));
    jit_store(j, MME_FIELD(steps), jit_get(j, j->steps));
    jit_store(j, MME_FIELD(carry), jit_get(j, j->carry));
    jit_store(j, MME_FIELD(method), jit_get(j, j->method));
    jit_store(j, MME_FIELD(incr), jit_get(j, j->incr));
    jit_store(j, MME_FIELD(running), ir_i32(j->ir, (u32)running));
    jit_store(j, MME_FIELD(branch), ir_i32(j->ir, branch));
    jit_store(j, MME_FIELD(slot), ir_i32(j->ir, slot));
    jit_store(j, MME_FIELD(exiting), ir_i32(j->ir, exiting));
    for (r = 0; r < 8; r++) jit_store(j, MME_FIELD(regs) + (size_t)r * sizeof(u32), r ? jit_reg(j, r) : ir_i32(j->ir, 0));
}

/* the block where a compile that can't follow the code gives up: the caller discards the object */
static LLVMBasicBlockRef jit_bad(Jit* j) {
    if (!j->bad) {
        LLVMBasicBlockRef here = LLVMGetInsertBlock(j->ir->b);
        j->bad = ir_block(j->ir, "bad");
        LLVMPositionBuilderAtEnd(j->ir->b, j->bad);
        LLVMBuildRet(j->ir->b, ir_i32(j->ir, 0));
        LLVMPositionBuilderAtEnd(j->ir->b, here);
    }
    return j->bad;
}

/* the block a state's steps start in, made on first use */
static LLVMBasicBlockRef jit_block(Jit* j, JitNode node) {
    u32 i;
    if (j->failed) return jit_bad(j);
    for (i = 0; i < j->nnodes; i++)
        if (j->nodes[i].off == node.off && j->nodes[i].mode == node.mode &&
            ((node.mode != MME_JIT_S && node.mode != MME_JIT_XS) || j->nodes[i].target == node.target))
            return j->blocks[i];
    if (j->nnodes == j->cap || (node.mode != MME_JIT_END && node.off >= j->n)) {
        j->failed = 1;
        return jit_bad(j);
    }
    j->nodes[j->nnodes] = node;
    j->blocks[j->nnodes] = ir_block(j->ir, "step");
    return j->blocks[j->nnodes++];
}

/* where a step that is not a branch goes, as step() leaves the flags */
static JitNode jit_next(JitNode node, u32 insn) {
    JitNode out;
    u32 exit = (insn >> 7) & 1;
    out.target = 0;
    switch (node.mode) {
        case MME_JIT_N:
            out.mode = exit ? MME_JIT_X : MME_JIT_N;
            out.off = node.off + 1;
            break;
        case MME_JIT_S:
            out.mode = MME_JIT_N;
            out.off = node.target;
            break;
        case MME_JIT_X:
            out.mode = MME_JIT_END;
            out.off = node.off + 1;
            break;
        default: /* MME_JIT_XS */
            out.mode = MME_JIT_END;
            out.off = node.target;
            break;
    }
    return out;
}

/* where a branch goes, taken or not, as step() leaves the flags. A taken branch's delay slot is the next word; a
   branch in a delay slot leaves the fetch PC at the delay slot's branch target, so its own target comes after that
   word, which is why S and XS carry the pending target */
static JitNode jit_branch(Jit* j, JitNode node, u32 insn, int taken) {
    JitNode out;
    u32 annul = (insn >> 5) & 1;
    u32 exit = (insn >> 7) & 1;
    i32 imm = (i32)((insn >> 14) ^ 0x20000u) - 0x20000;
    out.target = 0;
    if (!taken) {
        switch (node.mode) {
            case MME_JIT_N:
                out.mode = exit ? MME_JIT_X : MME_JIT_N;
                out.off = node.off + 1;
                break;
            case MME_JIT_S:
                out.mode = MME_JIT_N;
                out.off = node.target;
                break;
            case MME_JIT_X:
                out.mode = MME_JIT_END;
                out.off = node.off + 1;
                break;
            default: /* MME_JIT_XS */
                out.mode = MME_JIT_END;
                out.off = node.target;
                break;
        }
        return out;
    }
    out.off = (node.off + (u32)imm) & 0x1FFF;
    if (out.off >= j->n) { /* the branch leaves the upload batch: the interpreter runs the macro */
        j->failed = 1;
        out.mode = MME_JIT_N;
        out.off = 0;
        return out;
    }
    switch (node.mode) {
        case MME_JIT_N:
            if (annul) {
                out.mode = MME_JIT_N;
            } else {
                out.mode = MME_JIT_S;
                out.target = out.off;
                out.off = node.off + 1;
            }
            break;
        case MME_JIT_S:
            if (annul) {
                out.mode = MME_JIT_N;
            } else {
                out.mode = MME_JIT_S;
                out.target = out.off;
                out.off = node.target;
            }
            break;
        case MME_JIT_X:
            if (annul) {
                out.mode = MME_JIT_END;
            } else {
                out.mode = MME_JIT_XS;
                out.target = out.off;
                out.off = node.off + 1;
            }
            break;
        default: /* MME_JIT_XS */
            if (annul) {
                out.mode = MME_JIT_END;
            } else {
                out.mode = MME_JIT_XS;
                out.target = out.off;
                out.off = node.target;
            }
            break;
    }
    return out;
}

static void jit_node(Jit* j, u32 index) {
    JitNode node = j->nodes[index];
    SnxIr* ir = j->ir;
    LLVMBuilderRef b = ir->b;
    u32 insn, op, rop, dst, mask, src_bit, dst_bit;
    i32 imm;
    LLVMValueRef a, value, cond;
    JitNode next;
    LLVMPositionBuilderAtEnd(b, j->blocks[index]);
    if (node.mode == MME_JIT_END) {
        jit_state_out(j, node.off, 0, MME_JIT_N, 0);
        LLVMBuildRet(b, ir_i32(ir, 1));
        return;
    }
    /* a wait the step asked for: the interpreter's loop sees it after the step, so the state is the next one's and
       mme_resume goes on from it */
    {
        LLVMBasicBlockRef parked = ir_block(ir, "parked"), run = ir_block(ir, "run");
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, jit_load(j, offsetof(GpuChannel, park)), ir_i32(ir, 0), ""),
                        parked, run);
        LLVMPositionBuilderAtEnd(b, parked);
        jit_state_out(j, node.off, node.target, node.mode, 1);
        LLVMBuildRet(b, ir_i32(ir, 1));
        LLVMPositionBuilderAtEnd(b, run);
    }
    /* the interpreter's cutoff: the step is counted before its instruction runs */
    {
        LLVMValueRef steps = LLVMBuildAdd(b, jit_get(j, j->steps), ir_i32(ir, 1), "");
        LLVMBasicBlockRef cut = ir_block(ir, "cut"), body = ir_block(ir, "body");
        LLVMValueRef args[1];
        jit_set(j, j->steps, steps);
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntUGT, steps, ir_i32(ir, MME_STEPS), ""), cut, body);
        LLVMPositionBuilderAtEnd(b, cut);
        args[0] = j->ch;
        jit_call(j, j->cutoff, j->cutoff_type, args, 1);
        jit_state_out(j, node.off, node.target, node.mode, 0);
        LLVMBuildRet(b, ir_i32(ir, 1));
        LLVMPositionBuilderAtEnd(b, body);
    }
    insn = j->code[(j->start_word + node.off) & 0x1FFF];
    op = insn & 7;
    rop = (insn >> 4) & 7;
    dst = (insn >> 8) & 7;
    mask = (1u << ((insn >> 22) & 31)) - 1;
    src_bit = (insn >> 17) & 31;
    dst_bit = (insn >> 27) & 31;
    imm = (i32)((insn >> 14) ^ 0x20000u) - 0x20000;
    if (op == 7) {
        a = jit_reg(j, (insn >> 11) & 7);
        cond = LLVMBuildICmp(b, ((insn >> 4) & 1) ? LLVMIntNE : LLVMIntEQ, a, ir_i32(ir, 0), "");
        LLVMBuildCondBr(b, cond, jit_block(j, jit_branch(j, node, insn, 1)),
                        jit_block(j, jit_branch(j, node, insn, 0)));
        return;
    }
    a = jit_reg(j, (insn >> 11) & 7);
    switch (op) {
        case 0: value = jit_alu(j, (insn >> 17) & 31, a, jit_reg(j, (insn >> 14) & 7)); break;
        case 1: value = LLVMBuildAdd(b, a, ir_i32(ir, (u32)imm), ""); break;
        case 2: /* bits of b at src_bit into a at dst_bit */
            value = LLVMBuildOr(
                b, LLVMBuildAnd(b, a, LLVMBuildNot(b, LLVMBuildShl(b, ir_i32(ir, mask), ir_i32(ir, dst_bit), ""), ""), ""),
                LLVMBuildShl(
                    b,
                    LLVMBuildAnd(b, LLVMBuildLShr(b, jit_reg(j, (insn >> 14) & 7), ir_i32(ir, src_bit), ""),
                                 ir_i32(ir, mask), ""),
                    ir_i32(ir, dst_bit), ""),
                "");
            break;
        case 3: /* extract at a register's bit, shift left by an immediate */
            value = LLVMBuildShl(
                b,
                LLVMBuildAnd(b, LLVMBuildLShr(b, jit_reg(j, (insn >> 14) & 7),
                                              LLVMBuildAnd(b, a, ir_i32(ir, 31), ""), ""),
                             ir_i32(ir, mask), ""),
                ir_i32(ir, dst_bit), "");
            break;
        case 4: /* extract at an immediate's bit, shift left by a register */
            value = LLVMBuildShl(
                b,
                LLVMBuildAnd(b, LLVMBuildLShr(b, jit_reg(j, (insn >> 14) & 7), ir_i32(ir, src_bit), ""),
                             ir_i32(ir, mask), ""),
                LLVMBuildAnd(b, a, ir_i32(ir, 31), ""), "");
            break;
        case 5: /* read a register */
            {
                LLVMValueRef args[3];
                args[0] = j->ch;
                args[1] = j->rec;
                args[2] = LLVMBuildAdd(b, a, ir_i32(ir, (u32)imm), "");
                jit_set_reg(j, dst, jit_call(j, j->reg, j->reg_type, args, 3));
            }
            LLVMBuildBr(b, jit_block(j, jit_next(node, insn)));
            return;
        default: /* operation 6 and up: the interpreter logs it and does nothing, which is no compiled form */
            j->failed = 1;
            LLVMBuildBr(b, jit_bad(j));
            return;
    }
    jit_result(j, rop, dst, value);
    next = jit_next(node, insn);
    LLVMBuildBr(b, jit_block(j, next));
}

/* builds the module's function: 0, or -1 (j->failed) when the code is not one the compiler can follow */
static int jit_emit(Jit* j, u32 start_word, const u32* code, u32 n, LLVMValueRef fn) {
    SnxIr* ir = j->ir;
    LLVMBuilderRef b = ir->b;
    LLVMTypeRef out_params[4], reg_params[3], cutoff_params[1];
    LLVMBasicBlockRef entry;
    u32 r, i;
    j->ch = LLVMGetParam(fn, 0);
    j->rec = LLVMGetParam(fn, 1);
    j->np = LLVMGetParam(fn, 2);
    j->start = LLVMGetParam(fn, 3);
    j->code = code;
    j->start_word = start_word;
    j->n = n;
    j->cap = n * 16 + 64;
    j->nodes = calloc(j->cap, sizeof *j->nodes);
    j->blocks = calloc(j->cap, sizeof *j->blocks);
    if (!j->nodes || !j->blocks) return -1;
    out_params[0] = ir->t_ptr;
    out_params[1] = ir->t_ptr;
    out_params[2] = ir->t_i32;
    out_params[3] = ir->t_i32;
    reg_params[0] = ir->t_ptr;
    reg_params[1] = ir->t_ptr;
    reg_params[2] = ir->t_i32;
    cutoff_params[0] = ir->t_ptr;
    j->out = jit_callee(ir, "mme_out", (SnxU64)(uintptr_t)mme_out, &j->out_type, ir->t_void, out_params, 4);
    j->reg = jit_callee(ir, "mme_reg", (SnxU64)(uintptr_t)mme_reg, &j->reg_type, ir->t_i32, reg_params, 3);
    j->cutoff = jit_callee(ir, "mme_jit_cutoff", (SnxU64)(uintptr_t)mme_jit_cutoff, &j->cutoff_type, ir->t_void,
                           cutoff_params, 1);
    entry = ir_block(ir, "entry");
    LLVMPositionBuilderAtEnd(b, entry);
    for (r = 1; r < 8; r++) {
        j->regs[r] = ir_local(ir, ir->t_i32);
        jit_set(j, j->regs[r], ir_i32(ir, 0));
    }
    j->carry = ir_local(ir, ir->t_i32);
    j->method = ir_local(ir, ir->t_i32);
    j->incr = ir_local(ir, ir->t_i32);
    j->next = ir_local(ir, ir->t_i32);
    j->steps = ir_local(ir, ir->t_i32);
    jit_set(j, j->carry, ir_i32(ir, 0));
    jit_set(j, j->method, ir_i32(ir, 0));
    jit_set(j, j->incr, ir_i32(ir, 0));
    jit_set(j, j->next, ir_i32(ir, 1)); /* the first parameter is in r1 */
    jit_set(j, j->steps, ir_i32(ir, 0));
    {
        LLVMValueRef base = jit_ptr(j, offsetof(GpuChannel, mme_params));
        LLVMValueRef zero = ir_i32(ir, 0);
        LLVMValueRef first = ir_load(ir, ir->t_i32, base);
        LLVMValueRef have = LLVMBuildICmp(b, LLVMIntNE, j->np, zero, "");
        jit_set(j, j->regs[1], LLVMBuildSelect(b, have, first, zero, ""));
    }
    /* a wait already asked for before the call: the interpreter runs no step */
    {
        LLVMBasicBlockRef parked = ir_block(ir, "parked0"), go = ir_block(ir, "go");
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, jit_load(j, offsetof(GpuChannel, park)), ir_i32(ir, 0), ""),
                        parked, go);
        LLVMPositionBuilderAtEnd(b, parked);
        jit_state_out(j, 0, 0, MME_JIT_N, 1);
        LLVMBuildRet(b, ir_i32(ir, 1));
        LLVMPositionBuilderAtEnd(b, go);
    }
    {
        JitNode first;
        first.off = 0;
        first.target = 0;
        first.mode = MME_JIT_N;
        LLVMBuildBr(b, jit_block(j, first));
    }
    for (i = 0; i < j->nnodes; i++) jit_node(j, i);
    return j->failed ? -1 : 0;
}

/* one module per macro: the SnxIr keeps the context and target machine, the module the function */
static int jit_compile(SnxIr* ir, SnxCodeLoader* loader, u64 hash, u32 start_word, const u32* code, u32 n, MmeJitFn* out,
                       char* error, size_t error_size) {
    char name[32];
    LLVMModuleRef module, saved_module;
    LLVMValueRef fn, saved_fn;
    LLVMTypeRef params[4];
    unsigned char* object = NULL;
    size_t object_size = 0;
    SnxLinkInput input;
    const char* names[1];
    SnxU64 address = 0;
    Jit j;
    int rc;
    if (error_size) error[0] = 0;
    snprintf(name, sizeof name, "mme_%016llx", (unsigned long long)hash);
    module = LLVMModuleCreateWithNameInContext(name, ir->ctx);
    LLVMSetTarget(module, LLVMGetTarget(ir->mod));
    LLVMSetDataLayout(module, LLVMGetDataLayoutStr(ir->mod));
    params[0] = ir->t_ptr;
    params[1] = ir->t_ptr;
    params[2] = ir->t_i32;
    params[3] = ir->t_i32;
    fn = LLVMAddFunction(module, name, LLVMFunctionType(ir->t_i32, params, 4, 0));
    LLVMSetVisibility(fn, LLVMHiddenVisibility);
    memset(&j, 0, sizeof j);
    j.ir = ir;
    saved_module = ir->mod;
    saved_fn = ir->fn;
    ir->mod = module;
    ir->fn = fn;
    rc = jit_emit(&j, start_word, code, n, fn);
    if (!rc) rc = snx_ir_module_object(ir, module, MME_JIT_OPT, &object, &object_size, error, error_size);
    ir->mod = saved_module;
    ir->fn = saved_fn;
    free(j.nodes);
    free(j.blocks);
    LLVMDisposeModule(module);
    if (rc) return -1;
    input.data = object;
    input.size = object_size;
    input.name = name;
    names[0] = name;
    rc = snx_code_load(loader, &input, 1, names, 1, &address, error, error_size);
    free(object);
    if (rc || !address) return -1;
    *out = (MmeJitFn)(uintptr_t)address;
    return 0;
}

/* the compiled functions, by the hash of the macro's code: an entry with no function is a hash whose compilation
   failed, so the interpreter runs it without the attempt every call */
typedef struct {
    u64 hash;
    MmeJitFn fn;
} MmeJitEntry;
static struct {
    SnxIr* ir;
    SnxCodeLoader* loader;
    MmeJitEntry* entries;
    size_t count, capacity;
} mme_jit;
static int mme_jit_state = -1; /* -1 not tried, 0 off (no LLVM, no loader, mme_jit=0), 1 on */

static MmeJitEntry* mme_jit_entry(u64 hash) {
    size_t i;
    if (mme_jit.count * 2 >= mme_jit.capacity) {
        size_t capacity = mme_jit.capacity ? mme_jit.capacity * 2 : 256, n;
        MmeJitEntry* entries = calloc(capacity, sizeof *entries);
        if (!entries) return NULL;
        for (n = 0; n < mme_jit.capacity; n++) {
            if (!mme_jit.entries[n].hash) continue;
            i = (size_t)(mme_jit.entries[n].hash * 0x9E3779B97F4A7C15ull >> 32) & (capacity - 1);
            while (entries[i].hash) i = (i + 1) & (capacity - 1);
            entries[i] = mme_jit.entries[n];
        }
        free(mme_jit.entries);
        mme_jit.entries = entries;
        mme_jit.capacity = capacity;
    }
    i = (size_t)(hash * 0x9E3779B97F4A7C15ull >> 32) & (mme_jit.capacity - 1);
    while (mme_jit.entries[i].hash && mme_jit.entries[i].hash != hash) i = (i + 1) & (mme_jit.capacity - 1);
    return &mme_jit.entries[i];
}

static void mme_jit_shutdown(void) {
    if (mme_jit.loader) snx_code_loader_free(mme_jit.loader);
    if (mme_jit.ir) snx_ir_free(mme_jit.ir);
    free(mme_jit.entries);
    memset(&mme_jit, 0, sizeof mme_jit);
    mme_jit_state = 0;
}

static void mme_jit_start(void) {
    char error[512];
    mme_jit_state = 0;
    if (!g_settings.compiled_macros) return;
    if (snx_ir_init(error, sizeof error)) {
        LOG_LIMIT(1, "mme: macros are interpreted: no LLVM: %s\n", error);
        return;
    }
    mme_jit.ir = snx_ir_new_jit("mme", MME_JIT_OPT, error, sizeof error);
    if (!mme_jit.ir) {
        LOG_LIMIT(1, "mme: macros are interpreted: %s\n", error);
        return;
    }
    mme_jit.loader = snx_code_loader_new(0, error, sizeof error);
    if (!mme_jit.loader) {
        LOG_LIMIT(1, "mme: macros are interpreted: %s\n", error);
        mme_jit_shutdown();
        return;
    }
    if (snx_code_define(mme_jit.loader, "mme_out", (SnxU64)(uintptr_t)mme_out) ||
        snx_code_define(mme_jit.loader, "mme_reg", (SnxU64)(uintptr_t)mme_reg) ||
        snx_code_define(mme_jit.loader, "mme_jit_cutoff", (SnxU64)(uintptr_t)mme_jit_cutoff)) {
        LOG_LIMIT(1, "mme: macros are interpreted: the loader cannot name their functions\n");
        mme_jit_shutdown();
        return;
    }
    mme_jit_state = 1;
}

void mme_jit_free(void) { mme_jit_shutdown(); }

void mme_jit_cutoff(GpuChannel* ch) {
    WARN_LIMIT(4, "gpu: macro on channel %u ran %u instructions and is cut off\n", ch->id, MME_STEPS);
}

/* mme.c's seam: the compiled form of slot idx's code, compiled on its first call */
int mme_jit_run(GpuChannel* ch, MmeRec* rec, u32 idx, u32 np) {
#ifdef SNX_IOS_AOT_ONLY
    /* Known macros keep their native HLE paths; unknown macros use the existing
       command interpreter. Shader compilation through Metal is unaffected. */
    (void)ch; (void)rec; (void)idx; (void)np;
    return 0;
#endif
    u32 s, batch, n = 0, i;
    u64 hash;
    MmeJitEntry* entry;
    char error[512];
    if (mme_jit_state < 0) mme_jit_start();
    if (mme_jit_state == 0) return 0;
    idx &= 0x7F;
    hash = ch->mme_hash[idx];
    if (!hash) return 0;
    entry = mme_jit_entry(hash);
    if (!entry) return 0;
    if (entry->hash) {
        if (!entry->fn) return 0;
        entry->fn(ch, rec, np, ch->mme_start[idx] & 0x1FFF);
        return 1;
    }
    s = ch->mme_start[idx] & 0x1FFF;
    batch = ch->mme_code_batch[s];
    if (!batch) return 0;
    for (i = 0; i < 0x2000; i++) {
        if (ch->mme_code_batch[(s + i) & 0x1FFF] != batch) break;
        n++;
    }
    if (!n) return 0;
    SNX_ZONE_TV(z_jit, "mme jit", n, "hash %016llx", (unsigned long long)hash);
    if (jit_compile(mme_jit.ir, mme_jit.loader, hash, s, ch->mme_code, n, &entry->fn, error, sizeof error)) {
        LOG_LIMIT(16, "mme: macro %u (hash %016llx) is interpreted: %s\n", idx, (unsigned long long)hash,
                  error[0] ? error : "its code is not one the compiler can follow");
        entry->hash = hash; /* the failure stays: the interpreter runs it from now on */
        mme_jit.count++;
        SNX_ZONE_END(z_jit);
        return 0;
    }
    entry->hash = hash;
    mme_jit.count++;
    SNX_ZONE_END(z_jit);
    entry->fn(ch, rec, np, s);
    return 1;
}
