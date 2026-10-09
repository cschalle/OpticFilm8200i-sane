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
 * vread - issue vendor control IN requests, to replay reads seen in
 * captures (some controllers treat "reads" as commands).
 *
 *   vread <bRequest> <wValue> <wIndex> <len> [count]     (hex)
 */
#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>

int main(int argc, char **argv)
{
    libusb_context *ctx;
    libusb_device_handle *h;
    unsigned char b[64];
    int i, n, r, count;

    if (argc < 5)
        return 1;
    n = (int)strtol(argv[4], NULL, 16);
    count = argc > 5 ? atoi(argv[5]) : 1;
    if (n > 64 || libusb_init(&ctx))
        return 1;
    h = libusb_open_device_with_vid_pid(ctx, 0x07b3, 0x1825);
    if (!h || libusb_claim_interface(h, 0)) {
        fprintf(stderr, "vread: scanner not available\n");
        return 1;
    }
    for (i = 0; i < count; i++) {
        r = libusb_control_transfer(h, 0xC0, (int)strtol(argv[1], NULL, 16),
                                    (int)strtol(argv[2], NULL, 16),
                                    (int)strtol(argv[3], NULL, 16), b, n, 1000);
        if (i == 0 || i == count - 1) {
            int k;
            printf("got %d:", r);
            for (k = 0; k < r; k++)
                printf(" %02x", b[k]);
            printf("\n");
        }
    }
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
