/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "common/cpu/recompiler/llvm/ir.h"
#include "common/cpu/recompiler/llvm/link.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned rd32(const unsigned char* p) { return p[0] | p[1]<<8 | p[2]<<16 | (unsigned)p[3]<<24; }
int main(void) {
    char error[1024];
    unsigned char* object = NULL;
    size_t size = 0;
    SnxLinkOutput out = {0};
    assert(!llvm_init(error,sizeof error));
    assert(!snx_ir_rtlib_object_target('2',"arm64-apple-ios16.0",&object,&size,error,sizeof error));
    assert(object && size > 32 && rd32(object) == 0xfeedfacf);
    SnxLinkInput input = {object,size,"iOS runtime helpers"};
    if (snx_link(&input,1,NULL,0,"InkypaintGame",&out,error,sizeof error)) {
        fprintf(stderr,"%s\n",error); abort();
    }
    assert(out.size > 32 && rd32(out.data) == 0xfeedfacf);
    size_t off = 32;
    int ios = 0;
    for (unsigned i = 0; i < rd32(out.data+16); i++) {
        unsigned cmd = rd32(out.data+off), bytes = rd32(out.data+off+4);
        assert(bytes >= 8 && bytes <= out.size-off);
        if (cmd == 0x32) { assert(rd32(out.data+off+8) == 2); ios = 1; }
        off += bytes;
    }
    assert(ios);
    free(object); free(out.data);
    puts("PASS offline iOS Mach-O generation and linking (not executed)");
}
