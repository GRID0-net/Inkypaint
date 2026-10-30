#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the inkypaint contributors
"""Run one owned build session and forward cancellation to its process group."""
import os
import signal
import subprocess
import sys

child = None
cancelled = False

def cancel(signum, frame):
    global cancelled
    cancelled = True
    if child is not None and child.poll() is None:
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("No build command provided")
    signal.signal(signal.SIGTERM, cancel)
    signal.signal(signal.SIGINT, cancel)
    child = subprocess.Popen(sys.argv[1:], start_new_session=True)
    if cancelled:
        cancel(signal.SIGTERM, None)
    status = child.wait()
    sys.exit(130 if cancelled else status if status >= 0 else 128-status)
