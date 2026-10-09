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

#ifndef PLUSTEK8200I_DMA_H
#define PLUSTEK8200I_DMA_H

#include "plustek8200i.h"

/*
 * DMA functions for Plustek OpticFilm 8200i
 *
 * The DMA protocol below was established from USB captures of the
 * vendor driver:
 * - Register 0x82: DMA command register (8 bytes: address + length)
 * - Register 0x18 (via 0x8E): DMA status (bits 2&3 = busy flags)
 * - Bulk transfers must be padded to 512-byte blocks
 * - DMA sequence: wait_ready → setup → bulk_transfer → wait_ready
 *
 */

/* DMA memory addresses in scanner SDRAM
 *
 * Register names (see the note at the register definitions in plustek8200i.h):
 *   AHB address allocation
 *     SDRAM mapped at 0x1000_0000 – 0x17FF_FFFF (128MB, 16-bit)
 *   Motor table mapping in SDRAM
 *     Table 1: 0x1000_0000 – 0x1000_3FFF (8K words)
 *     Table 2: 0x1000_4000 – 0x1000_7FFF
 *     Table 3: 0x1000_8000 – 0x1000_BFFF
 *     Table 4: 0x1000_C000 – 0x1000_FFFF
 *     Table 5: 0x1001_0000 – 0x1001_3FFF
 *   Reg 0x40 bit 0 DATAENB:
 *     When DATAENB=1 (scanning mode), DMA reads at 0x10000000
 *     return linearized image data from the scan buffer FIFO,
 *     not raw SDRAM content.
 *
 * From macOS driver the vendor driver's MotorAddr() and USB capture validation:
 * - Motor tables:  0x10000000-0x10013FFF (5 tables × 0x4000 each)
 * - Scan data:     0x10000000 (when DATAENB=1, reads image FIFO)
 */
#define PLUSTEK8200I_DMA_ADDR_SCAN_BUFFER   0x10000000

/* DMA setup and control */
SANE_Status plustek8200i_dma_setup(int fd, uint32_t address, uint32_t length, bool is_write);
SANE_Status plustek8200i_dma_wait_ready(int fd);

/* High-level DMA operations */
SANE_Status plustek8200i_dma_write(int fd, uint32_t address, const uint8_t *data, uint32_t length);


/* Low-level bulk transfers */
SANE_Status plustek8200i_bulk_write(int fd, const uint8_t *data, size_t length);
SANE_Status plustek8200i_bulk_read(int fd, uint8_t *buffer, size_t length);

/* Scan data read with per-chunk DMA protocol (sane-genesys approach)
 *
 * Unlike plustek8200i_bulk_read which does raw bulk reads, this function
 * sends a REQUEST_BUFFER DMA header before each chunk and polls valid words.
 * Required for reading scan image data — without per-chunk headers the
 * GL128 FIFO doesn't advance and returns stale data.
 */
SANE_Status plustek8200i_read_scan_data(int fd, uint8_t *buffer, size_t length);
SANE_Status plustek8200i_read_scan_chunk(int fd, uint8_t *buffer, size_t remaining,
                                        size_t *got);
SANE_Status plustek8200i_read_scan_data_cancellable(int fd, uint8_t *buffer, size_t length,
                                                    volatile int *cancel);

#endif /* PLUSTEK8200I_DMA_H */
