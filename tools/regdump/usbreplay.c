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
 * usbreplay - replay a vendor USB stream extracted from a usbmon capture
 * (tools: mkfull.py), with the original timing, against the OpticFilm
 * 8200i (07b3:1825).
 *
 *   T <seconds since start>
 *   CO <bRequest> <wValue> <wIndex> <hex data>     control OUT (vendor)
 *   CI <bRequest> <wValue> <wIndex> <len>          control IN (result ignored)
 *   BO <hex data>                                  bulk OUT, ep 0x02
 *   BI <len>                                       bulk IN, ep 0x81, drained
 *
 * This repeats exactly what the captured program sent -- including motor
 * commands -- and drains bulk data as it did, so the scan engine is not
 * left stalled. Use only with captures of the same scanner.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libusb-1.0/libusb.h>

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int hex2bin(const char *s, unsigned char *out, int max)
{
    int n = 0;
    while (s[0] && s[1] && n < max) {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = v;
        s += 2;
    }
    return n;
}

int main(int argc, char **argv)
{
    libusb_context *ctx;
    libusb_device_handle *h;
    char *line = NULL;
    size_t cap = 0;
    static unsigned char buf[1 << 20];
    double t0 = now(), due = 0;
    long nops = 0, bytes_in = 0;
    FILE *f;

    if (argc != 2 || !(f = fopen(argv[1], "r")))
        return 1;
    if (libusb_init(&ctx))
        return 1;
    h = libusb_open_device_with_vid_pid(ctx, 0x07b3, 0x1825);
    if (!h || libusb_claim_interface(h, 0)) {
        fprintf(stderr, "usbreplay: scanner not available\n");
        return 1;
    }
    while (getline(&line, &cap, f) > 0) {
        char op[4];
        int r = 0, br, wv, wi, len, done;
        if (sscanf(line, "%3s", op) != 1)
            continue;
        if (!strcmp(op, "T")) {
            due = atof(line + 2);
            while (now() - t0 < due)
                ;                                   /* keep the capture's pace */
            continue;
        }
        nops++;
        if (!strcmp(op, "CO")) {
            char hex[8192] = "";
            sscanf(line + 3, "%d %d %d %8191s", &br, &wv, &wi, hex);
            len = hex2bin(hex, buf, sizeof(buf));
            r = libusb_control_transfer(h, 0x40, br, wv, wi, buf, len, 5000) == len ? 0 : -1;
        } else if (!strcmp(op, "CI")) {
            sscanf(line + 3, "%d %d %d %d", &br, &wv, &wi, &len);
            r = libusb_control_transfer(h, 0xC0, br, wv, wi, buf, len, 5000) >= 0 ? 0 : -1;
        } else if (!strcmp(op, "BO")) {
            len = hex2bin(line + 3, buf, sizeof(buf));
            r = libusb_bulk_transfer(h, 0x02, buf, len, &done, 5000);
        } else if (!strcmp(op, "BI")) {
            len = atoi(line + 3);
            if (len > (int)sizeof(buf)) len = sizeof(buf);
            r = libusb_bulk_transfer(h, 0x81, buf, len, &done, 60000);
            bytes_in += done;
        }
        if (r) {
            fprintf(stderr, "usbreplay: %s failed at t=%.3f (op %ld): %s\n",
                    op, due, nops, libusb_error_name(r));
            return 2;
        }
    }
    fprintf(stderr, "usbreplay: %ld ops, %ld bytes drained, %.1f s\n", nops, bytes_in, now() - t0);
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
