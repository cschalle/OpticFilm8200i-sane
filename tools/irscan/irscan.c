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
 * irscan -- save a "Color+Infrared" scan from the plustek8200i backend.
 *
 * The mode delivers two SANE frames: RGB (last_frame FALSE), then the
 * infrared plane as GRAY (last_frame TRUE). scanimage cannot save that,
 * so this writes <prefix>.pnm (RGB) and <prefix>-ir.pgm.
 *
 *   irscan [-D device] [-r dpi] [-d 8|16] [-c] [-g l,t,r,b] [-m mode] [-x] <prefix>
 *     -D  SANE device name (default plustek8200i)
 *     -c  also --ir-clean the colour frame
 *     -g  scan area in mm (top-left and bottom-right corners)
 *     -x  cancel after the first frame (test)
 *
 * Build: see Makefile. Licensed under LGPL-2.1-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sane/sane.h>

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static int set_opt(SANE_Handle h, const char *name, const void *val)
{
    const SANE_Option_Descriptor *d;
    SANE_Int i, n = 0;
    sane_control_option(h, 0, SANE_ACTION_GET_VALUE, &n, NULL);
    for (i = 1; i < n; i++) {
        d = sane_get_option_descriptor(h, i);
        if (d && d->name && strcmp(d->name, name) == 0) {
            SANE_Status st = sane_control_option(h, i, SANE_ACTION_SET_VALUE,
                                                 (void *)val, NULL);
            if (st != SANE_STATUS_GOOD)
                fprintf(stderr, "irscan: %s: %s\n", name, sane_strstatus(st));
            return st == SANE_STATUS_GOOD ? 0 : -1;
        }
    }
    fprintf(stderr, "irscan: no option %s\n", name);
    return -1;
}

int main(int argc, char **argv)
{
    SANE_Handle h;
    SANE_Status st;
    SANE_Int ver, dpi = 600, depth = 16;
    SANE_Bool clean = SANE_FALSE;
    const char *mode = "Color+Infrared";
    const char *devname = "plustek8200i";
    int opt, frame = 0, cancel_after_first = 0;
    double g[4];
    int have_g = 0;
    double t0;

    while ((opt = getopt(argc, argv, "D:r:d:cm:g:x")) != -1) {
        if (opt == 'g') {
            have_g = sscanf(optarg, "%lf,%lf,%lf,%lf", &g[0], &g[1], &g[2], &g[3]) == 4;
            continue;
        }
        if (opt == 'x') { cancel_after_first = 1; continue; }
        if (opt == 'D') { devname = optarg; continue; }
        if (opt == 'r') dpi = atoi(optarg);
        else if (opt == 'd') depth = atoi(optarg);
        else if (opt == 'c') clean = SANE_TRUE;
        else if (opt == 'm') mode = optarg;
        else return 2;
    }
    if (optind >= argc) {
        fprintf(stderr, "usage: irscan [-D device] [-r dpi] [-d 8|16] [-c] [-g l,t,r,b] [-m mode] [-x] prefix\n");
        return 2;
    }
    sane_init(&ver, NULL);
    st = sane_open(devname, &h);
    if (st != SANE_STATUS_GOOD) {
        fprintf(stderr, "irscan: open: %s\n", sane_strstatus(st));
        return 1;
    }
    if (set_opt(h, "mode", mode) || set_opt(h, "resolution", &dpi)
        || set_opt(h, "depth", &depth) || (clean && set_opt(h, "ir-clean", &clean)))
        return 1;
    if (have_g) {
        static const char *names[4] = { "tl-x", "tl-y", "br-x", "br-y" };
        int i;
        for (i = 0; i < 4; i++) {
            SANE_Fixed v = SANE_FIX(g[i]);
            if (set_opt(h, names[i], &v))
                return 1;
        }
    }

    t0 = now();
    for (;;) {
        SANE_Parameters p;
        char path[4096];
        FILE *f;
        SANE_Byte buf[65536];
        SANE_Int len;
        size_t total = 0, want;

        st = sane_start(h);
        if (st != SANE_STATUS_GOOD) {
            fprintf(stderr, "irscan: start (frame %d): %s\n", frame, sane_strstatus(st));
            return 1;
        }
        sane_get_parameters(h, &p);
        want = (size_t)p.bytes_per_line * p.lines;
        snprintf(path, sizeof(path), "%s%s", argv[optind],
                 p.format == SANE_FRAME_RGB ? ".pnm"
                 : frame == 0 ? ".pgm" : "-ir.pgm");
        f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        fprintf(f, "P%c\n%d %d\n%d\n", p.format == SANE_FRAME_RGB ? '6' : '5',
                p.pixels_per_line, p.lines, p.depth == 16 ? 65535 : 255);
        while ((st = sane_read(h, buf, sizeof(buf), &len)) == SANE_STATUS_GOOD) {
            if (p.depth == 16) {            /* PNM is big-endian */
                SANE_Int i;
                for (i = 0; i + 1 < len; i += 2) {
                    SANE_Byte t = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = t;
                }
            }
            fwrite(buf, 1, len, f);
            total += len;
        }
        fclose(f);
        if (st != SANE_STATUS_EOF) {
            fprintf(stderr, "irscan: read (frame %d): %s\n", frame, sane_strstatus(st));
            return 1;
        }
        fprintf(stderr, "irscan: frame %d %s %dx%d depth %d: %zu of %zu bytes, %s, at %.1f s\n",
                frame, p.format == SANE_FRAME_RGB ? "RGB" : "GRAY",
                p.pixels_per_line, p.lines, p.depth, total, want,
                total == want ? "OK" : "SHORT", now() - t0);
        frame++;
        if (p.last_frame)
            break;
        if (cancel_after_first) {
            fprintf(stderr, "irscan: cancelling after frame 0\n");
            break;
        }
    }
    sane_cancel(h);
    sane_close(h);
    sane_exit();
    return 0;
}
