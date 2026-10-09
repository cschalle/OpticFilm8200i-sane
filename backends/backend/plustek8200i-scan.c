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
 * Scan operations for Plustek OpticFilm 8200i
 *
 * What is left here after the unreachable scan-init path was removed:
 * the register-write helper every other unit uses, and the scan stop.
 * Both were decoded from USB captures of the vendor driver.
 */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"
#include "plustek8200i-scan.h"
#include "plustek8200i-dma.h"
#include <unistd.h>

/*
 * Register initialization blocks from USB capture
 *
 * These are the FINAL register values after all dynamic updates. The
 * vendor driver computes register values dynamically from the scan
 * parameters (resolution, mode, area), then updates them after the
 * initial register setup; these values include those updates. They were
 * read from a USB capture of the vendor driver.
 *
 * Key changes from previous static values:
 *   - 0x0B (TG0CNT): 0x6C → 0x8C (CCD timing)
 *   - 0x13 (TRMODE): 0x08 → 0x0F (transfer mode)
 *   - 0x5E (VSMP): 0x1F → 0x80 (vertical sample)
 *   - 0x51, 0x5D: Now included (previously missing)
 *   - Many other registers updated with final values
 */

/*
 * Write multiple register/value pairs (WriteAllReg)
 *
 * Sends bulk register writes using control transfer format:
 *   bmRequestType: 0x40 (Host→Device, Vendor)
 *   bRequest: 0xA0 (same as single register writes)
 *   wValue: 0x0083 (Register write command)
 *   wIndex: 0x0000
 *   Data: [reg, val, reg, val, ...] pairs
 *
 * CRITICAL FIX: bRequest was 0x04, but macOS USB capture shows 0xA0.
 * Single write_reg already used 0xA0 correctly. Data is split into
 * 64-byte chunks matching the macOS WriteAllReg behavior.
 *
 * From macOS USB capture validation
 */
SANE_Status
plustek8200i_write_regs(int fd, const uint8_t *reg_val_pairs, size_t count)
{
    SANE_Status status;

    if (count == 0 || count % 2 != 0) {
        DBG(DBG_error, "write_regs: invalid count %zu (must be even)\n", count);
        return SANE_STATUS_INVAL;
    }

    DBG(DBG_info, "plustek8200i_write_regs: %zu bytes (%zu register pairs)\n",
        count, count / 2);

    /*
     * Split into 64-byte chunks, matching macOS WriteAllReg behavior.
     * macOS sends 64+64+64+40 for 232 bytes (116 register pairs).
     * USB control transfers have practical size limits.
     */
    size_t offset = 0;
    while (offset < count) {
        size_t chunk = count - offset;
        if (chunk > 64)
            chunk = 64;

        DBG(DBG_debug, "write_regs: chunk %zu bytes at offset %zu\n", chunk, offset);
        plustek8200i_regtrace_pairs(reg_val_pairs + offset, chunk);

        status = sanei_usb_control_msg(fd,
                                        0x40,    /* bmRequestType */
                                        0xA0,    /* bRequest (FIXED: was 0x04) */
                                        0x0083,  /* wValue: register write */
                                        0x0000,  /* wIndex */
                                        chunk,   /* wLength */
                                        (unsigned char *)(reg_val_pairs + offset));

        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "write_regs: control transfer failed at offset %zu: %s\n",
                offset, sane_strstatus(status));
            return status;
        }

        offset += chunk;
    }

    DBG(DBG_debug, "Wrote %zu register pairs successfully\n", count / 2);
    return SANE_STATUS_GOOD;
}

/*
 * Stop scanning
 *
 * Clears the SCAN bit to stop the scan operation and motor.
 */
SANE_Status
plustek8200i_scan_stop(int fd)
{
    SANE_Status status;
    uint8_t cisset;

    DBG(DBG_info, "plustek8200i_scan_stop: stopping scan\n");

    /* Read current CISSET register value */
    status = plustek8200i_read_reg(fd, REG_CISSET, &cisset);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "scan_stop: failed to read CISSET\n");
        return status;
    }

    DBG(DBG_debug, "CISSET before: 0x%02x\n", cisset);

    /* Clear SCAN bit (bit 7) */
    cisset &= ~CISSET_SCAN;

    /* Write back to register */
    status = plustek8200i_write_reg(fd, REG_CISSET, cisset);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "scan_stop: failed to clear SCAN bit\n");
        return status;
    }

    DBG(DBG_info, "SCAN bit cleared, scanner stopped\n");

    return SANE_STATUS_GOOD;
}
