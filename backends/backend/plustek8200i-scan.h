/* sane - Scanner Access Now Easy.

   Copyright (C) 2025 Christian Schaller

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

#ifndef PLUSTEK8200I_SCAN_H
#define PLUSTEK8200I_SCAN_H

#include "plustek8200i.h"

/*
 * Scan operations for Plustek OpticFilm 8200i, decoded from USB captures
 * of the vendor driver.  Register addresses are in plustek8200i.h.
 */

/* Scan parameters for preview mode */
#define SCAN_PREVIEW_DPI       150
#define SCAN_PREVIEW_WIDTH     1200   /* pixels */
#define SCAN_PREVIEW_HEIGHT    100    /* lines for test */
#define SCAN_BYTES_PER_PIXEL   6      /* 16-bit RGB = 2 bytes × 3 channels */
#define SCAN_LINE_BYTES        (SCAN_PREVIEW_WIDTH * SCAN_BYTES_PER_PIXEL)

/* Register block write */
SANE_Status plustek8200i_write_regs(int fd, const uint8_t *reg_val_pairs, size_t count);

SANE_Status plustek8200i_scan_stop(int fd);


#endif /* PLUSTEK8200I_SCAN_H */
