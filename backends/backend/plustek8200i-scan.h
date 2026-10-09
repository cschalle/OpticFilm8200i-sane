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
 * Scan operations for Plustek OpticFilm 8200i
 *
 * Decoded from USB captures of the vendor driver.
 *
 * This module implements a minimal viable scan operation:
 * - Register initialization from USB captures
 * - Basic scan control (start/stop)
 * - Data reading via DMA
 * - Preview resolution (150 DPI)
 *
 * USB Captures: a macOS USB capture
 */

/*
 * Register addresses are defined in plustek8200i.h.
 * Key scan registers:
 *   REG_MTRPWR (0x02): Motor power/control
 *   REG_DPISET_H/L (0x2C/0x2D): DPI setting [11:0]
 *   REG_STRPIXEL_H/L (0x30/0x31): Start pixel [12:0]
 *   REG_ENDPIXEL_H/L (0x32/0x33): End pixel [12:0]
 *   REG_LPERIOD_H/L (0x38/0x39): Line period [15:0]
 *   REG_LINCNT_H/M/L (0x25/0x26/0x27): Line count [19:0]
 *   REG_FEEDL_H/M/L (0x3D/0x3E/0x3F): Feed length [19:0]
 *   CISSET_SCAN (0x01): Scan enable (reg 0x01 bit 0)
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
