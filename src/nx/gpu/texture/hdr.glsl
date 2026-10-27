/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 citron Emulator Project
   Copyright 2026 the inkypaint contributors */
/* Adapted from Citrosis's citrosis_diag.h (GPL-2.0-or-later). Integer exponent tests preserve every finite bit,
   including signed zero and subnormals, and clear only non-finite components. Packed channels are independent. */
uint hdr_word(uint v, uint mode) {
    if (mode == 1u) {
        uint lo = (v & 0x7c00u) == 0x7c00u ? 0xffffu : 0u;
        uint hi = (v & 0x7c000000u) == 0x7c000000u ? 0xffff0000u : 0u;
        return v & ~(lo | hi);
    }
    if (mode == 2u) return (v & 0x7f800000u) == 0x7f800000u ? 0u : v;
    if (mode == 3u) {
        uint r = (v & 0x000007c0u) == 0x000007c0u ? 0x000007ffu : 0u;
        uint g = (v & 0x003e0000u) == 0x003e0000u ? 0x003ff800u : 0u;
        uint b = (v & 0xf8000000u) == 0xf8000000u ? 0xffc00000u : 0u;
        return v & ~(r | g | b);
    }
    return v;
}
