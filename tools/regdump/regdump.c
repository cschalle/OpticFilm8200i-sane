/* Copyright (C) 2026 Christian Schaller

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this program; if not, see
   <https://www.gnu.org/licenses/>.

   SPDX-License-Identifier: LGPL-2.1-or-later
*/

/*
 * regdump - read every GL128 register of the OpticFilm 8200i (07b3:1825)
 * without initialising anything, so the state another program (or the
 * last scan) left behind can be inspected.
 *
 *   regdump > state.txt      lines: "reg written hardware"
 *
 * Same read as the driver's plustek8200i_read_reg: control IN, vendor,
 * bRequest 0xA0, wValue 0x008E (register file) or 0x018E (hardware
 * state), wIndex (reg << 8) | 0x22, 2 bytes [value, 'U'].
 */
#include <stdio.h>
#include <libusb-1.0/libusb.h>

static int rd(libusb_device_handle *h, int reg, int hw)
{
    unsigned char b[2];
    int r = libusb_control_transfer(h, 0xC0, 0xA0, (hw ? 0x100 : 0) | 0x8E,
                                    (reg << 8) | 0x22, b, 2, 1000);
    if (r != 2 || b[1] != 'U')
        return -1;
    return b[0];
}

int main(void)
{
    libusb_context *ctx;
    libusb_device_handle *h;
    int reg;

    if (libusb_init(&ctx))
        return 1;
    h = libusb_open_device_with_vid_pid(ctx, 0x07b3, 0x1825);
    if (!h) {
        fprintf(stderr, "regdump: scanner 07b3:1825 not found or not accessible\n");
        return 1;
    }
    if (libusb_claim_interface(h, 0)) {
        fprintf(stderr, "regdump: cannot claim interface 0 (in use?)\n");
        return 1;
    }
    for (reg = 0; reg < 256; reg++)
        printf("%02x %3d %3d\n", reg, rd(h, reg, 0), rd(h, reg, 1));
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
