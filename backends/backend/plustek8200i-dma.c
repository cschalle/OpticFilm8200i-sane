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
 * The transfer sequence below -- bulk length setup, the write and read
 * paths, and the DMA-finished poll -- follows what the vendor driver is
 * observed to do on the wire.
 *
 * This module handles DMA command setup and bulk USB transfers for the
 * GL128 scanner chip. The scanner has internal memory buffers that must
 * be accessed via a DMA-like protocol:
 *
 * 1. Send 8-byte DMA command to register 0x82 (address + length)
 * 2. Perform USB bulk read/write
 * 3. Poll register 0x18 for DMA completion
 *
 */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"
#include <sys/time.h>
#include "plustek8200i-dma.h"
#include <unistd.h>

/* USB bulk transfer block size (USB 2.0 high-speed) */
/*
 * Bulk transfer block sizes.
 *
 * BULK_WRITE_SIZE: 512 bytes — USB bulk writes must be aligned to the endpoint's
 *   max packet size (512 for USB 2.0 High Speed).
 *
 * BULK_READ_SIZE: 65536 bytes — Large reads are essential for scan data throughput.
 *   Reading 512 bytes at a time requires ~2000 USB transactions per MB, adding
 *   massive per-transaction latency overhead (~0.5 MB/s actual throughput).
 *   Reading 64KB at a time lets the USB stack batch multiple packets per URB,
 *   achieving 10-40 MB/s throughput on USB 2.0.
 *   MUST be a multiple of 512 (USB HS max packet size).
 */
#define BULK_WRITE_SIZE 512
#define BULK_READ_SIZE  65536

/* Maximum chunk size per DMA scan data read (taken from sane-genesys) */
#define SCAN_READ_MAX_CHUNK  0xEFF0

/* TRIED AND REJECTED 2026-10-05: reducing this to 9216, on the grounds that
 * Windows reads scan data in units summing to exactly 9216 (512+8704) and the
 * device's large bulk OUT transfers are also 9216 - suggesting 9216 is the
 * ASIC's DMA buffer size, while 0xEFF0 aligns down to 119*512=60928 which is
 * not a multiple of it. Result: transfer count rose 2456 -> 15820 frames as
 * expected, but the output image was UNCHANGED (same diagonal shear). So the
 * shear is in how data is interpreted, not how it is transferred.
 */

/* Valid words polling timeout (70 seconds, matching sane-genesys) */
#define VALID_WORDS_TIMEOUT_MS  70000
#define VALID_WORDS_POLL_MS     100

/* DMA status polling parameters */
#define DMA_POLL_TIMEOUT_MS 833  /* ~50 ticks at 60 Hz (from macOS driver) */
#define DMA_POLL_INTERVAL_MS 20  /* 20ms between polls */

/* DMA status register bits (register 0x18 via 0x8E)
 * Register 0x18 (CNFG).
 *   Bits 3:2 are DMA busy flags. DMA is ready when both are 0. */
#define DMA_STATUS_BUSY_MASK 0x0C  /* Bits 2 and 3 */

/*
 * Setup DMA command (register 0x82)
 *
 * Sends 8-byte DMA command to scanner:
 *   Bytes 0-3: Scanner internal memory address (little-endian)
 *   Bytes 4-7: Transfer length in bytes (little-endian)
 *
 * Sent as an 8-byte control write to register 0x82.
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

    /* Build 8-byte DMA command: [address][length] (little-endian) */
    cmd[0] = address & 0xFF;
    cmd[1] = (address >> 8) & 0xFF;
    cmd[2] = (address >> 16) & 0xFF;
    cmd[3] = (address >> 24) & 0xFF;
    cmd[4] = length & 0xFF;
    cmd[5] = (length >> 8) & 0xFF;
    cmd[6] = (length >> 16) & 0xFF;
    cmd[7] = (length >> 24) & 0xFF;

    /*
     * Send DMA command to register 0x82 via control transfer
     *
     * From macOS driver ControlWrite() and SetupBulkLength():
     *   bmRequestType: 0x40 (Host→Device, Vendor, Device)
     *   bRequest:      0xA0 (vendor-specific, same as register I/O)
     *   wValue:        0x82 (Register address)
     *   wIndex:        is_write flag (1 for write, 0 for read)
     *   wLength:       8    (Total bytes: 4 address + 4 length)
     *
     * CRITICAL: wIndex is the is_write flag, NOT the data length!
     * This was the bug causing DMA to hang on consecutive writes.
     */
    status = sanei_usb_control_msg(fd,
                                    0x40,          /* bmRequestType */
                                    0xA0,          /* bRequest (FIXED: was 0x04) */
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
 * Wait for DMA to be ready
 *
 * Polls register 0x18 (via status command 0x8E) until DMA ready or timeout.
 * DMA is ready when bits 2 and 3 are both clear (mask 0x0C == 0).
 *
 * The vendor driver polls register 0x8E every 20 ms and treats the
 * transfer as finished once bits 2 and 3 are both clear, giving up
 * after a timeout.
 */
SANE_Status
plustek8200i_dma_wait_ready(int fd)
{
    uint8_t status;
    int elapsed_ms = 0;
    SANE_Status ret;

    DBG(DBG_debug, "plustek8200i_dma_wait_ready: polling register 0x18...\n");

    while (elapsed_ms < DMA_POLL_TIMEOUT_MS) {
        /*
         * Read DMA status from register 0x18 via hardware status read.
         *
         * Uses the standard read_reg() function with hardware read prefix
         * (0x118) to read the live DMA status, not a stale shadow copy.
         *
         * Reg 0x18 (CNFG/DMASTA):
         *   Bits 3:2 = DMA busy flags. DMA is ready when both are 0.
         *
         * read_reg encodes this as:
         *   wValue = 0x018E (0x01 = hardware flag, 0x8E = read command)
         *   wIndex = 0x1822 (reg 0x18 << 8 | 0x22)
         */
        ret = plustek8200i_read_reg(fd, 0x118, &status);

        if (ret != SANE_STATUS_GOOD) {
            DBG(DBG_error, "DMA status read failed: %s\n", sane_strstatus(ret));
            return ret;
        }

        /* Check if DMA ready (bits 2 and 3 both clear) */
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

/*
 * Write data via USB bulk endpoint
 *
 * Writes data in 512-byte blocks with padding as needed.
 *
 * The vendor driver writes whole USB blocks -- 512 bytes at high speed,
 * 64 otherwise -- truncating the transfer to a block multiple and
 * padding whatever is left over up to one more block.
 */
SANE_Status
plustek8200i_bulk_write(int fd, const uint8_t *data, size_t length)
{
    size_t aligned_len = length & ~(BULK_WRITE_SIZE - 1);  /* Round down to 512 */
    size_t remainder = length - aligned_len;
    SANE_Status status;
    size_t written;

    DBG(DBG_info, "plustek8200i_bulk_write: %zu bytes (%zu aligned + %zu remainder)\n",
        length, aligned_len, remainder);

    /* Write aligned portion */
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

    /* Write remainder with zero padding */
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

/*
 * Read data via USB bulk endpoint
 *
 * Reads requested number of bytes from scanner.
 */
SANE_Status
plustek8200i_bulk_read(int fd, uint8_t *buffer, size_t length)
{
    size_t total_read = 0;
    SANE_Status status;

    DBG(DBG_info, "plustek8200i_bulk_read: %zu bytes\n", length);

    /* Set a generous timeout for bulk reads — the scanner may need time
     * to fill its buffer as the motor moves */
    sanei_usb_set_timeout(30000);  /* 30 seconds */

    while (total_read < length) {
        /* Read in large blocks for throughput.
         * USB bulk transfers can handle large sizes — the USB stack splits
         * them into 512-byte packets internally. Using 64KB blocks instead
         * of 512 bytes gives ~100× throughput improvement. */
        size_t remaining = length - total_read;
        size_t actual;
        size_t chunk = (remaining > BULK_READ_SIZE) ? BULK_READ_SIZE : remaining;

        /* Round up to 512 for USB alignment */
        size_t aligned_chunk = (chunk + 511) & ~(size_t)511;

        if (aligned_chunk == chunk) {
            /* Already aligned — read directly into buffer */
            actual = aligned_chunk;
            status = sanei_usb_read_bulk(fd, buffer + total_read, &actual);
        } else {
            /* Last partial block: read aligned amount, copy what we need */
            actual = aligned_chunk;
            status = sanei_usb_read_bulk(fd, buffer + total_read, &actual);
            if (status == SANE_STATUS_GOOD && actual > chunk) {
                actual = chunk;  /* Only count what caller needs */
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

        /* Log first few reads at higher level for diagnostics */
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

/*
 * Complete DMA write sequence
 *
 * 1. Wait for DMA ready
 * 2. Setup DMA command
 * 3. Write bulk data
 * 4. Wait for DMA finish
 *
 * That is: set the bulk length up first, and only then write the data.
 */
SANE_Status
plustek8200i_dma_write(int fd, uint32_t address, const uint8_t *data, uint32_t length)
{
    SANE_Status status;

    DBG(DBG_info, "plustek8200i_dma_write: addr=0x%08x len=%u\n", address, length);

    /* 1. Wait for DMA to be ready */
    status = plustek8200i_dma_wait_ready(fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_warning, "DMA not ready before write\n");
        return status;
    }

    /* 2. Setup DMA command */
    status = plustek8200i_dma_setup(fd, address, length, true);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "DMA setup failed\n");
        return status;
    }

    /* 3. Write bulk data */
    status = plustek8200i_bulk_write(fd, data, length);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Bulk write failed\n");
        return status;
    }

    /* 4. Wait for DMA to finish */
    status = plustek8200i_dma_wait_ready(fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_warning, "DMA not ready after write\n");
        return status;
    }

    DBG(DBG_info, "DMA write complete\n");
    return SANE_STATUS_GOOD;
}

/*
 * Read scan data with per-chunk DMA protocol (sane-genesys style)
 *
 * The GL128 requires a DMA header (register 0x82 via control transfer)
 * before each bulk read chunk to advance the FIFO read pointer.
 *
 * Protocol from sane-genesys scanner_interface_usb.cpp:
 *   1. Send DMA header (8 bytes via control transfer)
 *   2. Bulk read the chunk
 *   3. Repeat until all bytes are read
 *
 * Key difference from our previous approach: NO DMA wait between chunks.
 * sane-genesys does NOT poll register 0x18 between chunks. The DMA header
 * + bulk read pair is enough. The USB bulk read blocks until data is
 * available.
 *
 * DMA header format (from sane-genesys bulk_read_data_send_header):
 *   bmRequestType: 0x40 (Vendor, Host→Device)
 *   bRequest:      0x04 (REQUEST_BUFFER)
 *   wValue:        0x82 (VALUE_BUFFER)
 *   wIndex:        0x00
 *   data[0-3]:     Address (0x10000000 for scan data)
 *   data[4-7]:     Length in bytes
 */
/*
 * One chunk of image data: DMA header for up to SCAN_READ_MAX_CHUNK bytes
 * (512-aligned) of `remaining`, then the bulk read. *got is what arrived.
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
 * As plustek8200i_read_scan_data, but returns SANE_STATUS_CANCELLED as
 * soon as *cancel is set. It is only checked between chunks, after the
 * bulk read for the current chunk has completed, so no transfer is ever
 * left outstanding.
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
     * The first chunk of an image pass only arrives after the lead-in
     * feed to the frame, which runs at scan speed: 28.8 s at 3600 dpi
     * (measured), about twice that at 7200. With the old fixed 30 s
     * timeout 3600 dpi had 1.2 s to spare and 7200 dpi always timed out
     * on its first read -- which also left the ASIC wedged until a power
     * cycle. Allow 180 s for the first chunk, 30 s between later ones.
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

        /* Log progress periodically (every ~512KB) */
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
