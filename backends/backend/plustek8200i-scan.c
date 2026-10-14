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
 * Scan operations for Plustek OpticFilm 8200i: the bulk register-write
 * helper and the scan stop, both decoded from USB captures of the vendor
 * driver.
 */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"
#include "plustek8200i-scan.h"
#include "plustek8200i-dma.h"
#include <unistd.h>

/*
 * Write [reg, val, ...] pairs as vendor control writes (bRequest 0xA0,
 * wValue 0x0083, as for single register writes), split into 64-byte
 * chunks as seen in USB captures.
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

    size_t offset = 0;
    while (offset < count) {
        size_t chunk = count - offset;
        if (chunk > 64)
            chunk = 64;

        DBG(DBG_debug, "write_regs: chunk %zu bytes at offset %zu\n", chunk, offset);
        plustek8200i_regtrace_pairs(reg_val_pairs + offset, chunk);

        status = sanei_usb_control_msg(fd,
                                        0x40,    /* bmRequestType */
                                        0xA0,    /* bRequest */
                                        0x0083,  /* wValue */
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

/* Clear the SCAN bit, stopping the scan and motor */
SANE_Status
plustek8200i_scan_stop(int fd)
{
    SANE_Status status;
    uint8_t cisset;

    DBG(DBG_info, "plustek8200i_scan_stop: stopping scan\n");

    status = plustek8200i_read_reg(fd, REG_CISSET, &cisset);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "scan_stop: failed to read CISSET\n");
        return status;
    }

    DBG(DBG_debug, "CISSET before: 0x%02x\n", cisset);

    cisset &= ~CISSET_SCAN;

    status = plustek8200i_write_reg(fd, REG_CISSET, cisset);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "scan_stop: failed to clear SCAN bit\n");
        return status;
    }

    DBG(DBG_info, "SCAN bit cleared, scanner stopped\n");

    return SANE_STATUS_GOOD;
}
