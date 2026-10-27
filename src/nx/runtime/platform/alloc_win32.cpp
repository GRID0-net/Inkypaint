// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// Windows: C++'s new and delete through mimalloc (MIT, Microsoft), which LLVM's allocations go through: the
// recompile's compiles, one a thread, and the run-time JIT. The Windows heap makes a process's threads take turns on
// it, so compiles on more threads gained less and less; LLVM's own build has LLVM_INTEGRATED_CRT_ALLOC for this, with
// MSVC only. C's malloc stays the C runtime's: memory it hands out may be freed by code that isn't ours
#include <mimalloc-new-delete.h>
