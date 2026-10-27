/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_MAINTENANCE_H
#define SNX_MAINTENANCE_H

/* Coalesced cache persistence, outside the thread that pumps SDL and publishes HID. */
void maintenance_request(void);
/* Drain the worker before the final synchronous save. No request may follow this call. */
void maintenance_stop(void);

#endif
