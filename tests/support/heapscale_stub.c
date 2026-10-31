/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* The filesystem service tests build fs.c without the runtime, which grafts a larger render scale's heap budgets */
#include "nx/runtime/inkypaint_heapscale.h"

void inkypaint_heapscale_graft(void) {}
