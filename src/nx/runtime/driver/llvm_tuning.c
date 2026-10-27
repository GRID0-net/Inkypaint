/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* g_settings' LLVM Internals for the recompile (module_llvm.h's snx_llvm_tuning): opt's pipeline for the level with
   the passes turned off cut out of it, and the options set on the pass builder and the target machine */
#include "nx/loader/module_llvm.h"
#include "common/cpu/recompiler/llvm/ir.h"
#include "nx/runtime/settings_llvm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a pipeline's text, cut */
typedef struct {
    const char* p; /* what is left to read */
    char* out;
    size_t at;
} Cut;

static void put(Cut* c, const char* s, size_t n) {
    memcpy(c->out + c->at, s, n);
    c->at += n;
}
/* the elements up to the end of their list (a ')' or the end), each a name, its <parameters> and its own pipeline in
   parentheses. A pass turned off is left out, and so is a pipeline left with none; require<> and invalidate<> stay.
   How many were kept */
static int cut_list(Cut* c) {
    int kept = 0;
    while (*c->p && *c->p != ')') {
        const char *start = c->p, *name_end;
        size_t mark = c->at;
        int depth = 0, keep = 1;
        if (*c->p == ',') {
            c->p++;
            continue;
        }
        while (*c->p && !strchr(",()<", *c->p)) c->p++;
        name_end = c->p;
        if (*c->p == '<') /* its parameters, which may nest */
            for (; *c->p; c->p++) {
                if (*c->p == '<') depth++;
                if (*c->p == '>' && !--depth) {
                    c->p++;
                    break;
                }
            }
        if (kept) put(c, ",", 1);
        put(c, start, (size_t)(c->p - start));
        if (*c->p == '(') { /* a pipeline of its own */
            c->p++;
            put(c, "(", 1);
            keep = cut_list(c) > 0;
            if (*c->p == ')') c->p++;
            put(c, ")", 1);
        } else
            keep = settings_llvm_pass(&g_settings, start, (size_t)(name_end - start)) != 0;
        if (keep)
            kept++;
        else
            c->at = mark;
    }
    return kept;
}

/* what snx_llvm_tuning gives, made again at each call */
static struct {
    SnxIrTuning tuning;
    char pipeline[1 << 16];
    char text[1 << 17]; /* tuning.text: the pipeline that runs, then each LLVM Internals key and its value */
} g_llvm_tuning;
static void builder(LLVMPassBuilderOptionsRef options) { settings_llvm_builder(&g_settings, options); }
static void machine(LLVMTargetMachineRef target) { settings_llvm_machine(&g_settings, target); }

/* tuning.text, a line each: "pipeline=" the one that runs (default: LLVM's own for the level), whether the SLP
   vectorizer runs (snx_ir_slp), then every key of the llvm table with its value. 0, or -1 with why */
static int describe(char* error, size_t error_size) {
    char* text = g_llvm_tuning.text;
    size_t size = sizeof g_llvm_tuning.text, at;
    const IniOpt* o;
    int n = snprintf(text, size, "pipeline=%s\nslp_vectorizer=%d\n",
                     g_llvm_tuning.tuning.pipeline ? g_llvm_tuning.tuning.pipeline : "default", snx_ir_slp());
    for (at = (size_t)n, o = g_llvm_ini; n >= 0 && at < size && o->key; o++, at += (size_t)n) {
        char value[64];
        ini_format(o, value, sizeof value);
        n = snprintf(text + at, size - at, "%s=%s\n", o->key, value);
    }
    if (n < 0 || at >= size) {
        snprintf(error, error_size, "LLVM Internals' values take more than %zu bytes as text", size - 1);
        return -1;
    }
    g_llvm_tuning.tuning.text = text;
    return 0;
}

/* The recompile's own pipeline for -O2 (the setting fast_pipeline): the inliner, then a pass over each function that
   makes the guest's registers SSA values (sroa), merges what each block computes twice (early-cse), combines and
   simplifies, and drops stores a later one replaces. default<O2>'s value numbering, loop passes and vectorizers took
   twice the compile time, half of it in value numbering and the SLP vectorizer, and the code they made ran no faster:
   7% slower over five kernels compiled for the console's CPU and recompiled (sorting, a physics step, a bytecode
   interpreter, a hash table, small calls in a loop), as the generated code keeps guest state in memory those passes
   can't see through */
static const char fast_pipeline[] =
    "cgscc(inline),function(sroa,early-cse<memssa>,instcombine<no-verify-fixpoint>,simplifycfg,dse)";

const struct SnxIrTuning* snx_llvm_tuning(char opt, char* error, size_t error_size) {
    SnxIrTuning* tuning = &g_llvm_tuning.tuning;
    char* pipeline = g_llvm_tuning.pipeline;
    int fast = opt == '2' && g_settings.fast_pipeline;
    const char* text = fast ? fast_pipeline : settings_llvm_pipeline(opt);
    tuning->pipeline = NULL;
    tuning->opt = opt;
    tuning->builder = builder;
    tuning->machine = machine;
    tuning->text = NULL;
    if (!text) { /* no pipeline to cut a pass from: a pass turned off would go on running under default<O?> */
        const IniOpt* o;
        for (o = g_llvm_ini; o->key; o++)
            if (!strncmp(o->key, "llvm_pass_", 10) && !*(const int*)o->var) {
                snprintf(error, error_size,
                         "LLVM Internals turn off %s, and this build has no -O%c pipeline to remove it from", o->key,
                         opt);
                return NULL;
            }
    }
    if (text) {
        Cut c;
        size_t n = strlen(text);
        if (n >= sizeof g_llvm_tuning.pipeline) {
            snprintf(error, error_size, "LLVM's pipeline for -O%c is longer than %u bytes", opt,
                     (unsigned)sizeof g_llvm_tuning.pipeline - 1);
            return NULL;
        }
        c.p = text;
        c.out = pipeline;
        c.at = 0;
        if (!cut_list(&c)) {
            snprintf(error, error_size, "LLVM Internals turn off every pass of -O%c's pipeline", opt);
            return NULL;
        }
        pipeline[c.at] = 0;
        /* opt's own text when nothing was cut: default<O?> runs the same, and doesn't depend on the text */
        if (fast || strcmp(pipeline, text)) tuning->pipeline = pipeline;
    }
    return describe(error, error_size) ? NULL : tuning;
}
