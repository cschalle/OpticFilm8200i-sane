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
 * replay - replay register and AHB writes from a text file, in order.
 *
 *   W rr vv              register write (hex)
 *   D aaaaaaaa hh hh ..  AHB write (hex address, bytes; padded to 512)
 *
 * Writes go out exactly as the driver sends them (control OUT 0xA0,
 * wValue 0x83 / 0x82). Meant for bisecting captured sequences; it sends
 * whatever the file says, so only feed it sequences without motion.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

int main(int argc, char **argv)
{
    libusb_context *ctx;
    libusb_device_handle *h;
    char line[8192];
    FILE *f;
    int n = 0;

    if (argc != 2 || !(f = fopen(argv[1], "r")))
        return 1;
    if (libusb_init(&ctx))
        return 1;
    h = libusb_open_device_with_vid_pid(ctx, 0x07b3, 0x1825);
    if (!h || libusb_claim_interface(h, 0)) {
        fprintf(stderr, "replay: scanner not available\n");
        return 1;
    }
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'W') {
            unsigned r, v;
            unsigned char b[2];
            if (sscanf(line + 1, "%x %x", &r, &v) != 2)
                continue;
            b[0] = r; b[1] = v;
            if (libusb_control_transfer(h, 0x40, 0xA0, 0x83, 0, b, 2, 1000) != 2) {
                fprintf(stderr, "replay: write %02x failed\n", r);
                return 1;
            }
            n++;
        } else if (line[0] == 'D') {
            unsigned char hdr[8], data[65536];
            unsigned long addr;
            int len = 0, done, i;
            char *p = line + 1, *e;
            addr = strtoul(p, &e, 16);
            p = e;
            while (len < (int)sizeof(data)) {
                unsigned long v = strtoul(p, &e, 16);
                if (e == p) break;
                data[len++] = (unsigned char)v;
                p = e;
            }
            for (i = 0; i < 4; i++) {
                hdr[i] = (addr >> (8 * i)) & 0xFF;
                hdr[4 + i] = ((unsigned)len >> (8 * i)) & 0xFF;
            }
            i = (len + 511) & ~511;
            memset(data + len, 0, i - len);
            if (libusb_control_transfer(h, 0x40, 0xA0, 0x82, 1, hdr, 8, 1000) != 8
                || libusb_bulk_transfer(h, 0x02, data, i, &done, 2000) || done != i) {
                fprintf(stderr, "replay: AHB write %08lx failed\n", addr);
                return 1;
            }
            n++;
        }
    }
    fprintf(stderr, "replay: %d operations\n", n);
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
