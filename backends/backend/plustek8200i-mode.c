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


/*
 * plustek8200i-mode.c -- run-time mode flags shared across the backend.
 *
 * These were the only live functions in the former plustek8200i-config.c,
 * which otherwise held a plist parser that nothing called.
 */

#define DEBUG_DECLARE_ONLY
#include "plustek8200i.h"
#include <stdlib.h>
#include <string.h>

/* Infrared pass: set from the SANE scan mode. */
static int g_infrared;

void
plustek8200i_set_infrared(int on)
{
    g_infrared = on ? 1 : 0;
}

int
plustek8200i_use_infrared(void)
{
    return g_infrared;
}
