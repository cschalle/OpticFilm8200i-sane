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
 * regwrite - write GL128 registers of the OpticFilm 8200i (07b3:1825)
 * directly, for replaying short register sequences seen in captures.
 *
 *   regwrite 03=20 03=10 03=00 ...     (hex reg=value, written in order)
 *
 * Same write as the driver: control OUT, vendor, bRequest 0xA0, wValue
 * 0x0083, wIndex 0, data [reg, value].
 */
#include <stdio.h>
#include <libusb-1.0/libusb.h>

int main(int argc, char **argv)
{
    libusb_context *ctx;
    libusb_device_handle *h;
    int i;

    if (libusb_init(&ctx))
        return 1;
    h = libusb_open_device_with_vid_pid(ctx, 0x07b3, 0x1825);
    if (!h || libusb_claim_interface(h, 0)) {
        fprintf(stderr, "regwrite: scanner not available\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        unsigned reg, val;
        unsigned char b[2];
        if (sscanf(argv[i], "%x=%x", &reg, &val) != 2 || reg > 0xFF || val > 0xFF) {
            fprintf(stderr, "regwrite: bad argument %s\n", argv[i]);
            return 1;
        }
        b[0] = reg; b[1] = val;
        if (libusb_control_transfer(h, 0x40, 0xA0, 0x0083, 0, b, 2, 1000) != 2) {
            fprintf(stderr, "regwrite: write %s failed\n", argv[i]);
            return 1;
        }
    }
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
