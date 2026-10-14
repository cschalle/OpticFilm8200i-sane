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
 * DMA functions for Plustek OpticFilm 8200i, from USB captures of the
 * vendor driver:
 * - Register 0x82: DMA command (8 bytes: address + length)
 * - Register 0x18 (via 0x8E): DMA status (bits 2&3 = busy flags)
 * - Bulk transfers are padded to 512-byte blocks
 * - Sequence: wait_ready, setup, bulk transfer, wait_ready
 */

/*
 * Scanner SDRAM is mapped at 0x10000000-0x17FFFFFF (see the note at the
 * register definitions in plustek8200i.h).  The five motor tables occupy
 * 0x10000000-0x10013FFF, 0x4000 each.  With DATAENB (reg 0x40 bit 0) set,
 * reads at 0x10000000 return linearized image data from the scan FIFO
 * instead of SDRAM.
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

/* Scan data read: a DMA header before each chunk is required, otherwise
 * the GL128 FIFO does not advance and returns stale data. */
SANE_Status plustek8200i_read_scan_data(int fd, uint8_t *buffer, size_t length);
SANE_Status plustek8200i_read_scan_chunk(int fd, uint8_t *buffer, size_t remaining,
                                        size_t *got);
SANE_Status plustek8200i_read_scan_data_cancellable(int fd, uint8_t *buffer, size_t length,
                                                    volatile int *cancel);

#endif /* PLUSTEK8200I_DMA_H */
