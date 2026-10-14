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

/*
 * DMA operations for Plustek OpticFilm 8200i
 *
 * DMA command setup and bulk transfers for the GL128 chip, whose internal
 * buffers are reached through a DMA-like protocol:
 *
 * 1. Send an 8-byte DMA command to register 0x82 (address + length)
 * 2. Perform the USB bulk read/write
 * 3. Poll register 0x18 for DMA completion
 *
 * The sequence matches what USB captures of the vendor driver show.
 */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"
#include <sys/time.h>
#include "plustek8200i-dma.h"
#include <unistd.h>

/*
 * Bulk transfer sizes.  Writes are aligned to the 512-byte high-speed
 * max packet size.  Reads use 64 KB (a multiple of 512): 512-byte reads
 * give ~0.5 MB/s, large ones 10-40 MB/s.
 */
#define BULK_WRITE_SIZE 512
#define BULK_READ_SIZE  65536

/* Maximum chunk size per DMA scan data read (taken from sane-genesys) */
#define SCAN_READ_MAX_CHUNK  0xEFF0

/* A 9216-byte chunk (the vendor driver's apparent DMA unit) was tried and
 * left the image unchanged, so the chunk size is not what matters. */

/* Valid words polling timeout (70 seconds, matching sane-genesys) */
#define VALID_WORDS_TIMEOUT_MS  70000
#define VALID_WORDS_POLL_MS     100

/* DMA status polling parameters */
#define DMA_POLL_TIMEOUT_MS 833  /* ~50 ticks at 60 Hz (from USB captures) */
#define DMA_POLL_INTERVAL_MS 20  /* 20ms between polls */

/* Register 0x18 (CNFG) bits 3:2 are DMA busy flags; ready when both clear */
#define DMA_STATUS_BUSY_MASK 0x0C  /* Bits 2 and 3 */

/*
 * Send the 8-byte DMA command to register 0x82: little-endian internal
 * memory address (bytes 0-3) and transfer length (bytes 4-7).
 */
SANE_Status
plustek8200i_dma_setup(int fd, uint32_t address, uint32_t length, bool is_write)
{
    uint8_t cmd[8];
    SANE_Status status;
    char mark[64];

    snprintf(mark, sizeof(mark), "DMA %s 0x%08x len %u",
             is_write ? "W" : "R", address, length);
    plustek8200i_regtrace_mark(mark);

    DBG(DBG_info, "plustek8200i_dma_setup: addr=0x%08x len=%u %s\n",
        address, length, is_write ? "WRITE" : "READ");

    cmd[0] = address & 0xFF;
    cmd[1] = (address >> 8) & 0xFF;
    cmd[2] = (address >> 16) & 0xFF;
    cmd[3] = (address >> 24) & 0xFF;
    cmd[4] = length & 0xFF;
    cmd[5] = (length >> 8) & 0xFF;
    cmd[6] = (length >> 16) & 0xFF;
    cmd[7] = (length >> 24) & 0xFF;

    /*
     * Vendor control write (bRequest 0xA0, as for register I/O).
     * wIndex is the is_write flag, not the data length; getting that
     * wrong made consecutive DMA writes hang.
     */
    status = sanei_usb_control_msg(fd,
                                    0x40,          /* bmRequestType */
                                    0xA0,          /* bRequest */
                                    0x82,          /* wValue: register 0x82 */
                                    is_write ? 1 : 0,  /* wIndex: write flag */
                                    8,             /* wLength */
                                    cmd);          /* data */

    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "DMA setup failed: %s\n", sane_strstatus(status));
        return status;
    }

    DBG(DBG_debug, "DMA command sent: %02x %02x %02x %02x %02x %02x %02x %02x\n",
        cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7]);

    return SANE_STATUS_GOOD;
}

/*
 * Poll register 0x18 every 20 ms until bits 2 and 3 are both clear,
 * or time out.
 */
SANE_Status
plustek8200i_dma_wait_ready(int fd)
{
    uint8_t status;
    int elapsed_ms = 0;
    SANE_Status ret;

    DBG(DBG_debug, "plustek8200i_dma_wait_ready: polling register 0x18...\n");

    while (elapsed_ms < DMA_POLL_TIMEOUT_MS) {
        /* 0x118 is the hardware-read form of register 0x18 (live
         * status, not the shadow copy) */
        ret = plustek8200i_read_reg(fd, 0x118, &status);

        if (ret != SANE_STATUS_GOOD) {
            DBG(DBG_error, "DMA status read failed: %s\n", sane_strstatus(ret));
            return ret;
        }

        if ((status & DMA_STATUS_BUSY_MASK) == 0) {
            DBG(DBG_debug, "DMA ready (status=0x%02x)\n", status);
            return SANE_STATUS_GOOD;
        }

        DBG(DBG_data, "DMA busy (status=0x%02x), waiting...\n", status);

        usleep(DMA_POLL_INTERVAL_MS * 1000);
        elapsed_ms += DMA_POLL_INTERVAL_MS;
    }

    DBG(DBG_error, "DMA timeout after %dms (status=0x%02x)\n",
        elapsed_ms, status);
    return SANE_STATUS_IO_ERROR;
}

/* Bulk write in whole 512-byte blocks; a short tail is zero-padded to a block */
SANE_Status
plustek8200i_bulk_write(int fd, const uint8_t *data, size_t length)
{
    size_t aligned_len = length & ~(BULK_WRITE_SIZE - 1);  /* Round down to 512 */
    size_t remainder = length - aligned_len;
    SANE_Status status;
    size_t written;

    DBG(DBG_info, "plustek8200i_bulk_write: %zu bytes (%zu aligned + %zu remainder)\n",
        length, aligned_len, remainder);

    if (aligned_len > 0) {
        written = aligned_len;
        status = sanei_usb_write_bulk(fd, data, &written);
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Bulk write failed (aligned): %s\n",
                sane_strstatus(status));
            return status;
        }
        if (written != aligned_len) {
            DBG(DBG_warning, "Bulk write incomplete: wrote %zu, expected %zu\n",
                written, aligned_len);
            return SANE_STATUS_IO_ERROR;
        }
    }

    if (remainder > 0) {
        uint8_t padded_block[BULK_WRITE_SIZE];
        memset(padded_block, 0, BULK_WRITE_SIZE);
        memcpy(padded_block, data + aligned_len, remainder);

        written = BULK_WRITE_SIZE;
        status = sanei_usb_write_bulk(fd, padded_block, &written);
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Bulk write failed (padded): %s\n",
                sane_strstatus(status));
            return status;
        }
        if (written != BULK_WRITE_SIZE) {
            DBG(DBG_warning, "Bulk write incomplete: wrote %zu, expected %d\n",
                written, BULK_WRITE_SIZE);
            return SANE_STATUS_IO_ERROR;
        }

        DBG(DBG_debug, "Wrote %zu bytes + %zu padding\n",
            remainder, BULK_WRITE_SIZE - remainder);
    }

    return SANE_STATUS_GOOD;
}

/* Bulk read of `length` bytes */
SANE_Status
plustek8200i_bulk_read(int fd, uint8_t *buffer, size_t length)
{
    size_t total_read = 0;
    SANE_Status status;

    DBG(DBG_info, "plustek8200i_bulk_read: %zu bytes\n", length);

    /* The scanner may need time to fill its buffer as the motor moves */
    sanei_usb_set_timeout(30000);

    while (total_read < length) {
        size_t remaining = length - total_read;
        size_t actual;
        size_t chunk = (remaining > BULK_READ_SIZE) ? BULK_READ_SIZE : remaining;

        size_t aligned_chunk = (chunk + 511) & ~(size_t)511;

        if (aligned_chunk == chunk) {
            actual = aligned_chunk;
            status = sanei_usb_read_bulk(fd, buffer + total_read, &actual);
        } else {
            /* Last partial block: read the aligned amount, keep what is needed */
            actual = aligned_chunk;
            status = sanei_usb_read_bulk(fd, buffer + total_read, &actual);
            if (status == SANE_STATUS_GOOD && actual > chunk) {
                actual = chunk;
            }
        }

        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Bulk read failed at offset %zu: %s\n",
                total_read, sane_strstatus(status));
            return status;
        }

        if (actual == 0) {
            DBG(DBG_warning, "Bulk read: no data available at offset %zu\n",
                total_read);
            break;
        }

        total_read += actual;

        if (total_read <= BULK_READ_SIZE * 4) {
            DBG(DBG_info, "Bulk read: got %zu bytes (total %zu/%zu) "
                "first bytes: %02x %02x %02x %02x\n",
                actual, total_read, length,
                buffer[total_read - actual],
                actual > 1 ? buffer[total_read - actual + 1] : 0,
                actual > 2 ? buffer[total_read - actual + 2] : 0,
                actual > 3 ? buffer[total_read - actual + 3] : 0);
        } else {
            DBG(DBG_data, "Bulk read: %zu bytes (total %zu/%zu)\n",
                actual, total_read, length);
        }
    }

    if (total_read != length) {
        DBG(DBG_warning, "Bulk read incomplete: got %zu, expected %zu\n",
            total_read, length);
        return SANE_STATUS_IO_ERROR;
    }

    return SANE_STATUS_GOOD;
}

/* DMA write: wait ready, set up the DMA command, write the data, wait for finish */
SANE_Status
plustek8200i_dma_write(int fd, uint32_t address, const uint8_t *data, uint32_t length)
{
    SANE_Status status;

    DBG(DBG_info, "plustek8200i_dma_write: addr=0x%08x len=%u\n", address, length);

    status = plustek8200i_dma_wait_ready(fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_warning, "DMA not ready before write\n");
        return status;
    }

    status = plustek8200i_dma_setup(fd, address, length, true);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "DMA setup failed\n");
        return status;
    }

    status = plustek8200i_bulk_write(fd, data, length);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Bulk write failed\n");
        return status;
    }

    status = plustek8200i_dma_wait_ready(fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_warning, "DMA not ready after write\n");
        return status;
    }

    DBG(DBG_info, "DMA write complete\n");
    return SANE_STATUS_GOOD;
}

/*
 * One chunk of image data.  As in sane-genesys, the GL128 needs a DMA
 * header (control write, bRequest 0x04, wValue 0x82) before each bulk read
 * to advance the FIFO read pointer; the register 0x18 poll is not needed
 * between chunks.  Reads up to SCAN_READ_MAX_CHUNK bytes (512-aligned) of
 * `remaining`; *got is what arrived.
 */
SANE_Status
plustek8200i_read_scan_chunk(int fd, uint8_t *buffer, size_t remaining, size_t *got)
{
    SANE_Status status;
    size_t chunk;
    uint8_t dma_header[8];

    chunk = remaining;
    if (chunk > SCAN_READ_MAX_CHUNK)
        chunk = SCAN_READ_MAX_CHUNK;
    if (chunk >= 512)
        chunk &= ~(size_t)511;          /* 512-byte USB alignment */

    /* [address 0x10000000 (scan data)][length], both little-endian */
    dma_header[0] = 0x00;
    dma_header[1] = 0x00;
    dma_header[2] = 0x00;
    dma_header[3] = 0x10;
    dma_header[4] = chunk & 0xFF;
    dma_header[5] = (chunk >> 8) & 0xFF;
    dma_header[6] = (chunk >> 16) & 0xFF;
    dma_header[7] = (chunk >> 24) & 0xFF;

    /* bRequest 0x04 (REQUEST_BUFFER), wValue 0x82 (VALUE_BUFFER) */
    status = sanei_usb_control_msg(fd, 0x40, 0x04, 0x82, 0x00, 8, dma_header);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "DMA header failed: %s\n", sane_strstatus(status));
        return status;
    }

    {
        size_t actual = chunk;
        status = sanei_usb_read_bulk(fd, buffer, &actual);
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Bulk read failed: %s\n", sane_strstatus(status));
            return status;
        }
        if (actual == 0) {
            DBG(DBG_warning, "Bulk read: no data\n");
            return SANE_STATUS_IO_ERROR;
        }
        if (actual != chunk)
            DBG(DBG_warning, "Bulk read: got %zu, expected %zu\n", actual, chunk);
        *got = actual;
    }
    return SANE_STATUS_GOOD;
}

SANE_Status
plustek8200i_read_scan_data(int fd, uint8_t *buffer, size_t length)
{
    return plustek8200i_read_scan_data_cancellable(fd, buffer, length, NULL);
}

/*
 * As plustek8200i_read_scan_data, but returns SANE_STATUS_CANCELLED once
 * *cancel is set.  Checked only between chunks, so no transfer is left
 * outstanding.
 */
SANE_Status
plustek8200i_read_scan_data_cancellable(int fd, uint8_t *buffer, size_t length,
                                        volatile int *cancel)
{
    size_t total_read = 0;
    SANE_Status status;
    struct timeval t_start;

    gettimeofday(&t_start, NULL);

    DBG(DBG_info, "plustek8200i_read_scan_data: reading %zu bytes "
        "(per-chunk DMA, genesys-style)\n", length);

    /*
     * The first chunk arrives only after the lead-in feed at scan speed
     * (28.8 s at 3600 dpi, about twice that at 7200).  A timeout there
     * wedges the ASIC until a power cycle, so allow 180 s for the first
     * chunk and 30 s after.
     */
    sanei_usb_set_timeout(180000);

    while (total_read < length) {
        size_t chunk;

        status = plustek8200i_read_scan_chunk(fd, buffer + total_read,
                                              length - total_read, &chunk);
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Scan read failed at offset %zu of %zu\n",
                total_read, length);
            return status;
        }

        if (total_read == 0) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            DBG(DBG_info, "Scan read: first %zu bytes after %.2f s\n", chunk,
                (tv.tv_sec - t_start.tv_sec) + (tv.tv_usec - t_start.tv_usec) / 1e6);
            sanei_usb_set_timeout(30000);
        }
        total_read += chunk;

        if (cancel && *cancel) {
            DBG(DBG_warning, "Scan read: cancelled at %zu/%zu bytes\n",
                total_read, length);
            return SANE_STATUS_CANCELLED;
        }

        if ((total_read % (512 * 1024)) < chunk) {
            DBG(DBG_info, "Scan read: %zu/%zu bytes (%.1f%%)\n",
                total_read, length,
                100.0 * total_read / length);
        }
    }

    DBG(DBG_info, "plustek8200i_read_scan_data: complete (%zu bytes)\n",
        length);
    return SANE_STATUS_GOOD;
}
