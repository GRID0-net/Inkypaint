/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_CALLTRACE_H
#define INKYPAINT_CALLTRACE_H
/* INKYPAINT_CALL_TRACE=<seconds>: from that many seconds into the run, two seconds of the game's indirect calls
   (virtual calls, function pointers) counted by caller, target and the vtable of their first argument, then the
   busiest written to the log, all of them to calltrace.txt. INKYPAINT_CALL_TRACE_DIRECT=lo-hi[,lo-hi...] (hex) adds
   the direct calls to the functions starting in those ranges, which the runtime then runs as JIT code (keep them
   clear of the frame-rate hooks' functions, which would lose their hooks). A map of the
   game's own call graph for the frame-rate work. Off without it */
void inkypaint_calltrace_init(void);
#endif
