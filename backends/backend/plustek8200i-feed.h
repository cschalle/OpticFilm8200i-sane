/* sane - Scanner Access Now Easy.

   Copyright (C) 2026 Christian Schaller

   This file is part of the SANE package.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see
   <https://www.gnu.org/licenses/>.

   SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef PLUSTEK8200I_FEED_H
#define PLUSTEK8200I_FEED_H

#include <stdint.h>
#include "../include/sane/sane.h"

/* Longest feed seen in the capture */
#define PLUSTEK8200I_FEED_MAX 28292
/* An image FEEDL of F lands where a feed of F - this does */
#define PLUSTEK8200I_FEED_LEAD 520

/* How many registers a feed changes, and so the most pairs a restore can
 * write.  A restore buffer needs room for 2 * this many bytes. */
#define PLUSTEK8200I_FEED_REGS 42

typedef struct {
    int     n;
    uint8_t val[64];
} plustek8200i_feed_saved_t;

SANE_Status plustek8200i_feed_save(int fd, plustek8200i_feed_saved_t *saved);
SANE_Status plustek8200i_feed_restore(int fd, const plustek8200i_feed_saved_t *saved);

/* The registers a feed changes, saved and restored around it. */
static inline const uint8_t *
plustek8200i_feed_touched(void)
{
    static const uint8_t touched[PLUSTEK8200I_FEED_REGS] = {
        0x01, 0x02, 0x04, 0x05, 0x1C, 0x1D, 0x2C, 0x2D, 0x3B,
        0x3D, 0x3E, 0x3F, 0x7D, 0x7E, 0x7F, 0x80, 0x81,
        0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92,
        0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAE, 0xAF,
    };
    return touched;
}

/*
 * The pure half of feed_restore: fill `pairs` (2 * PLUSTEK8200I_FEED_REGS
 * bytes) from a saved set and return how many bytes to write.
 *
 * How much was saved depends on how the device answered, so the count is
 * clamped rather than trusted.  A save that failed part way through used to
 * leave it uninitialised, and this loop then wrote two bytes per phantom
 * entry past the end of the caller's stack buffer.  Kept here, beside the
 * button helpers, so the bound can be tested without a scanner.
 */
static inline int
plustek8200i_feed_pairs(const plustek8200i_feed_saved_t *saved, uint8_t *pairs)
{
    const uint8_t *touched = plustek8200i_feed_touched();
    int i, n = saved->n;

    if (n < 0)
        n = 0;
    else if (n > PLUSTEK8200I_FEED_REGS)
        n = PLUSTEK8200I_FEED_REGS;

    for (i = 0; i < n; i++) {
        pairs[2 * i] = touched[i];
        pairs[2 * i + 1] = saved->val[i];
    }
    return 2 * n;
}
/* One positioning feed of `steps` (1..PLUSTEK8200I_FEED_MAX) with the
 * captured slow (first feed) or fast (second feed) ramp.  Leaves
 * 0x02 = 0x08 and FEEDL = 1; the feed ramp stays in the motor tables at
 * 0x1000C000/0x10010000. */
SANE_Status plustek8200i_feed(int fd, int steps, int slow_ramp);

#endif
