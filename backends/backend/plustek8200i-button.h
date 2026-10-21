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

/* Front-panel buttons; see plustek8200i-button.c for how they report. */

#ifndef PLUSTEK8200I_BUTTON_H
#define PLUSTEK8200I_BUTTON_H

#include "plustek8200i.h"

/* Read everything queued on the interrupt endpoint into dev->btn_latch. */
void plustek8200i_button_poll(plustek8200i_device_t *dev);

/* Discard presses the device is holding from before this session. */
void plustek8200i_button_discard(plustek8200i_device_t *dev);

/* The latch itself is folded and read with the inline helpers in
 * plustek8200i.h, so the unit tests can exercise them without USB. */

#endif /* PLUSTEK8200I_BUTTON_H */
