/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "common/cpu/recompiler/llvm/ir.h"
#include "common/cpu/recompiler/llvm/link.h"
#include "common/utilities/common.h"
#include "nx/loader/loader.h"
#include "nx/loader/module.h"
#include "nx/loader/module_abi.h"
#include "nx/loader/module_llvm.h"
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
    assert(!snx_ir_init(error,sizeof error));
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
    /* Exercise the actual game-module builder with our own tiny ARM64 program.
       No original game assets and no iOS code is executed on the host. */
    unsigned char memory[0x2000] = {0};
    const unsigned words[] = {0x91000400, 0xd65f03c0}; /* add x0,x0,#1; ret */
    memcpy(memory+0x1000, words, sizeof words);
    SnxModuleImage image = {0};
    image.name = "fixture"; image.base = SNX_CODE_BASE;
    image.size = image.image_size = sizeof memory; image.image = memory;
    image.text_off = 0x1000; image.text_size = 0x1000; image.is_nro = 1;
    SnxProgram program = {0};
    program.modules = &image; program.module_count = 1;
    program.npdm.is64 = 1; program.npdm.stack = 0x100000;
    strcpy(program.npdm.name,"fixture");
    SnxCodegenOptions options = {0};
    options.abi = SNX_ABI; options.title = "fixture"; options.quiet = 1;
    char* folder = snx_temp_dir(NULL);
    assert(folder);
    char* path = snx_path_join(folder,"fixture.dylib");
    SnxLlvmBuild build = {0};
    build.output = path; build.triple = "arm64-apple-ios16.0";
    build.native_library = 1; build.opt = '2'; build.jobs = 1; build.quiet = 1;
    assert(!snx_build_module_llvm(&program,&options,&build));
    unsigned char* library = (unsigned char*)snx_read_file(path,&size);
    assert(library && size > 32 && rd32(library) == 0xfeedfacf);
    free(library);
    assert(!snx_remove_tree(folder));
    free(path); free(folder);
    puts("PASS offline iOS helpers and game-module generation/linking (not executed)");
}
