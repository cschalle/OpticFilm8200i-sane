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
 * ahbwrite - write bytes to the GL128's AHB memory, as the driver's
 * plustek8200i_dma_write does: control OUT 0xA0 / wValue 0x82 / wIndex 1
 * with [addr u32 LE, len u32 LE], then the data on bulk OUT 0x02 padded
 * to 512 bytes.
 *
 *   ahbwrite <hex addr> <hex bytes...>     e.g. ahbwrite 10000000 b0 36 b0 36
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

int main(int argc, char **argv)
{
    libusb_context *ctx;
    libusb_device_handle *h;
    unsigned char hdr[8], data[512];
    unsigned long addr;
    int i, n = argc - 2, done;

    if (argc < 3 || n > 512)
        return 1;
    addr = strtoul(argv[1], NULL, 16);
    memset(data, 0, sizeof(data));
    for (i = 0; i < n; i++)
        data[i] = (unsigned char)strtoul(argv[2 + i], NULL, 16);
    for (i = 0; i < 4; i++) {
        hdr[i] = (addr >> (8 * i)) & 0xFF;
        hdr[4 + i] = ((unsigned)n >> (8 * i)) & 0xFF;
    }
    if (libusb_init(&ctx))
        return 1;
    h = libusb_open_device_with_vid_pid(ctx, 0x07b3, 0x1825);
    if (!h || libusb_claim_interface(h, 0)) {
        fprintf(stderr, "ahbwrite: scanner not available\n");
        return 1;
    }
    if (libusb_control_transfer(h, 0x40, 0xA0, 0x82, 1, hdr, 8, 1000) != 8
        || libusb_bulk_transfer(h, 0x02, data, 512, &done, 2000) || done != 512) {
        fprintf(stderr, "ahbwrite: transfer failed\n");
        return 1;
    }
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
