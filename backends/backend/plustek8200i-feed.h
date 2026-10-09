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

/* Longest feed SilverFast was seen to make. */
#define PLUSTEK8200I_FEED_MAX 28292
/* An image FEEDL of F reaches the same place as a feed of F - this. */
#define PLUSTEK8200I_FEED_LEAD 520

typedef struct {
    int     n;
    uint8_t val[64];
} plustek8200i_feed_saved_t;

SANE_Status plustek8200i_feed_save(int fd, plustek8200i_feed_saved_t *saved);
SANE_Status plustek8200i_feed_restore(int fd, const plustek8200i_feed_saved_t *saved);
/* One positioning feed of `steps` (1..PLUSTEK8200I_FEED_MAX), with
 * SilverFast's slow (first feed) or fast (second feed) ramp. Leaves
 * 0x02 = 0x08 and FEEDL = 1, as SilverFast does; the motor tables at
 * 0x1000C000/0x10010000 hold the feed ramp afterwards. */
SANE_Status plustek8200i_feed(int fd, int steps, int slow_ramp);

#endif
