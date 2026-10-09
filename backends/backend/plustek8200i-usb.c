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
 * USB I/O for Plustek OpticFilm 8200i
 *
 * TODO: Port from macOS driver the vendor driver's ReadRegs/WriteRegs
 * For now, using proven implementation from old code for hardware testing
 */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"

/* USB constants from reverse engineering */
#define USB_REQ_READ_REG    0xC0
#define USB_REQ_WRITE_REG   0x40
#define USB_WVAL_STATUS_POLL 0x008E
#define USB_WVAL_REG_WRITE   0x0083

/*
 * Read single register
 *
 * Based on the vendor driver's ReadRegs() and ControlRead():
 *   wIndex = (reg << 8) | 0x22
 *   wValue = (reg_addr & 0x100) | 0x8E
 *   Returns 2 bytes: [value, 'U']
 *
 * The register address can include bit 8 as a flag:
 *   reg_addr 0x01  → wValue=0x008E (reads shadow/written value)
 *   reg_addr 0x101 → wValue=0x018E (reads actual hardware state)
 *
 * On the wire: wValue carries 0x8E with bit 8 taken from the register
 * address, and wIndex is the address shifted up a byte with 0x22 in the
 * low byte. The vendor driver uses 0x018E (bit 8 set) when polling
 * hardware status registers such as reg 0x01 after MOVE commands.
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
                                   0xA0,    /* bRequest: vendor-specific */
                                   wValue,  /* wValue: 0x008E or 0x018E */
                                   wIndex,  /* wIndex: (reg<<8)|0x22 */
                                   2,       /* wLength */
                                   response);  /* data */

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
 * Write single register
 *
 * Uses the same [reg, val] data-payload format as write_regs (WriteAllReg),
 * which is confirmed working. The previous implementation encoded the register
 * in wIndex as (reg << 8) | 0x83 with just the value as data, but read-after-
 * write verification showed the scanner silently ignored those writes.
 *
 * Now delegates to the bulk write path: bmRequestType=0x40, bRequest=0xA0,
 * wValue=0x0083, wIndex=0x0000, data=[reg, val].
 */
/*
 * PLUSTEK8200I_REGTRACE=<path>: append every register write as
 * "W reg val" lines, plus "M <text>" markers from plustek8200i_regtrace_mark,
 * so the register state at any point of a scan can be reconstructed
 * without usbmon (which needs root).
 */
static FILE *regtrace_fp;
static int regtrace_init;

static FILE *
regtrace_file(void)
{
    if (!regtrace_init) {
        const char *p = getenv("PLUSTEK8200I_REGTRACE");
        regtrace_init = 1;
        if (p && *p)
            regtrace_fp = fopen(p, "a");
    }
    return regtrace_fp;
}

void
plustek8200i_regtrace_pairs(const uint8_t *pairs, size_t count)
{
    FILE *f = regtrace_file();
    size_t i;

    if (!f)
        return;
    for (i = 0; i + 1 < count; i += 2)
        fprintf(f, "W %02x %02x\n", pairs[i], pairs[i + 1]);
    fflush(f);
}

void
plustek8200i_regtrace_mark(const char *text)
{
    FILE *f = regtrace_file();

    if (!f)
        return;
    fprintf(f, "M %s\n", text);
    fflush(f);
}

SANE_Status
plustek8200i_write_reg(int usb_fd, uint8_t reg, uint8_t val)
{
    uint8_t reg_val_pair[2] = { reg, val };
    SANE_Status status;

    DBG(DBG_debug, "write_reg: reg=0x%02x value=0x%02x\n", reg, val);
    plustek8200i_regtrace_pairs(reg_val_pair, 2);

    status = sanei_usb_control_msg(usb_fd,
                                   0x40,    /* bmRequestType */
                                   0xA0,    /* bRequest */
                                   0x0083,  /* wValue: register write */
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
