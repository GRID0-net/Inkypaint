/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "common/cpu/recompiler/llvm/code_loader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char error[512] = {0};
    assert(!snx_code_loader_new(0,error,sizeof error));
    assert(strstr(error,"disabled in the signed iOS AOT build"));
    puts("PASS signed AOT mode refuses runtime executable-memory loading");
}
