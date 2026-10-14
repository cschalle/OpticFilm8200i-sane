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

/* USB register I/O for Plustek OpticFilm 8200i */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"

/* USB constants, from USB captures */
#define USB_REQ_READ_REG    0xC0
#define USB_REQ_WRITE_REG   0x40
#define USB_WVAL_STATUS_POLL 0x008E
#define USB_WVAL_REG_WRITE   0x0083

/*
 * Read a register.  The reply is 2 bytes: [value, 'U'].
 *
 * wIndex = (reg << 8) | 0x22; wValue = 0x8E with bit 8 taken from
 * reg_addr.  Bit 8 selects the source: reg 0x01 reads the shadow (last
 * written) value, 0x101 the actual hardware state, which is what status
 * polling after MOVE needs.
 */
SANE_Status
plustek8200i_read_reg(int usb_fd, uint16_t reg_addr, uint8_t *value)
{
    uint8_t response[2];
    uint8_t reg = reg_addr & 0xFF;
    uint16_t wValue = (reg_addr & 0x100) | 0x008E;
    uint16_t wIndex = (reg << 8) | 0x22;
    SANE_Status status;

    DBG(DBG_debug, "read_reg: reg=0x%02x wValue=0x%04x wIndex=0x%04x\n",
        reg, wValue, wIndex);

    status = sanei_usb_control_msg(usb_fd,
                                   0xC0,    /* bmRequestType: device-to-host, vendor */
                                   0xA0,    /* bRequest */
                                   wValue,
                                   wIndex,
                                   2,       /* wLength */
                                   response);

    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "read_reg: control read failed for reg 0x%02x: %s\n",
            reg, sane_strstatus(status));
        return status;
    }

    if (response[1] != 'U') {
        DBG(DBG_warning, "read_reg: expected 'U' confirmation, got 0x%02x\n", response[1]);
    }

    *value = response[0];
    DBG(DBG_data, "read_reg: reg 0x%02x = 0x%02x\n", reg, *value);

    return SANE_STATUS_GOOD;
}

/*
 * Register-write tracing goes through the debug levels: set
 * SANE_DEBUG_PLUSTEK8200I to DBG_data or above to reconstruct the register
 * state at any point of a scan from the log.
 */
void
plustek8200i_regtrace_pairs(const uint8_t *pairs, size_t count)
{
    size_t i;

    for (i = 0; i + 1 < count; i += 2)
        DBG(DBG_data, "reg write: 0x%02x = 0x%02x\n", pairs[i], pairs[i + 1]);
}

void
plustek8200i_regtrace_mark(const char *text)
{
    DBG(DBG_info, "trace: %s\n", text);
}

/*
 * Write a register as a [reg, val] payload to wValue 0x0083, the same
 * format as the bulk register write.  Encoding the register in wIndex
 * instead was silently ignored by the scanner.
 */
SANE_Status
plustek8200i_write_reg(int usb_fd, uint8_t reg, uint8_t val)
{
    uint8_t reg_val_pair[2] = { reg, val };
    SANE_Status status;

    DBG(DBG_debug, "write_reg: reg=0x%02x value=0x%02x\n", reg, val);

    status = sanei_usb_control_msg(usb_fd,
                                   0x40,    /* bmRequestType */
                                   0xA0,    /* bRequest */
                                   0x0083,  /* wValue */
                                   0x0000,  /* wIndex */
                                   2,       /* wLength */
                                   reg_val_pair);

    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "write_reg: control write failed for reg 0x%02x: %s\n",
            reg, sane_strstatus(status));
        return status;
    }

    DBG(DBG_data, "write_reg: reg 0x%02x = 0x%02x OK\n", reg, val);

    return SANE_STATUS_GOOD;
}
