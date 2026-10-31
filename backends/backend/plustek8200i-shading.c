/*
   Plustek OpticFilm 8200i (GL128) - hardware shading correction

   Copyright (C) 2026 Christian Schaller

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

   The ASIC's shading unit (DVDSET, register 0x01 bit 5) computes
   (raw - dark) * gain / 8192 for every sample, from a table at AHB
   0x10014000. Table format and the dark-strip procedure were decoded
   from usbmon captures of the vendor's SilverFast software:

     512-byte blocks of 42 pixels x (R, G, B) x (dark u16 LE, gain u16 LE,
     8192 = 1.0) = 504 bytes, then 8 bytes of padding; the last block is
     short. Samples are in the order the sensor sends them.

   Per scan, with every image register in place and the carriage at
   home, all stationary:

     1. AFE gains to code 0 (x1.0). White strip (lamp on) and dark strip
        (lamp off): the open light per sample, i.e. the illumination
        profile, and per channel how bright the open light is.
     2. Gain codes per channel so the open light lands at AUTO_TARGET of
        full scale -- SilverFast's reference too: fixed, independent of
        the picture, the same for every frame of a roll. What the 6-bit
        code cannot hit exactly is trimmed in the shading gains. The AFE is an
        AD9826: gain(code) = 6 / (1 + 5 (63 - code) / 63) (measured:
        code 3 vs 0 = x1.042, formula x1.041).
     3. Dark strip again at the final gains; the shading table is built
        from it and the white profile.

   The white strip only works at low gain: at the old frame-calibrated
   gains (1E/32/3B) the open window clips the ADC. At code 0 its profile
   matches a moving pass over the open window to 0.003.
*/

#define DEBUG_DECLARE_ONLY
#include "../include/sane/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "plustek8200i.h"
#include "plustek8200i-dma.h"
#include "plustek8200i-scan.h"
#include "plustek8200i-shading.h"
#include "plustek8200i-irflat.h"

#define STRIP_LINES      128
#define TABLE_ADDR       0x10014000
#define GAIN_UNITY       8192
#define PX_PER_BLOCK     42
#define AUTO_TARGET      0.90      /* open light, fraction of full scale;
                                      SilverFast uses ~1.01 (66183) */

/* Bytes of shading table for a line of `pixels` pixels. */
static size_t
table_bytes(int pixels)
{
    return (size_t)(pixels / PX_PER_BLOCK) * 512
         + (size_t)(pixels % PX_PER_BLOCK) * 12;
}

static void
build_table(uint8_t *t, int pixels, const double *dark, const double *gain)
{
    int x, c;
    size_t o = 0;

    memset(t, 0, table_bytes(pixels));
    for (x = 0; x < pixels; x++) {
        for (c = 0; c < 3; c++) {
            double d = dark[x * 3 + c];
            double g = gain ? gain[x * 3 + c] : GAIN_UNITY;
            unsigned dv = d < 0 ? 0 : d > 65535 ? 65535 : (unsigned)(d + 0.5);
            unsigned gv = g < 0 ? 0 : g > 65535 ? 65535 : (unsigned)(g + 0.5);
            t[o++] = dv & 0xFF;
            t[o++] = dv >> 8;
            t[o++] = gv & 0xFF;
            t[o++] = gv >> 8;
        }
        if ((x + 1) % PX_PER_BLOCK == 0)
            o += 8;                          /* block padding, gain 0 */
    }
}

static void
write24(int fd, uint8_t hi, uint32_t v)
{
    plustek8200i_write_reg(fd, hi, (v >> 16) & 0xFF);
    plustek8200i_write_reg(fd, hi + 1, (v >> 8) & 0xFF);
    plustek8200i_write_reg(fd, hi + 2, v & 0xFF);
}

/* 99th percentile of every `step`-th value starting at `start`. */
static double
robust_max(const double *v, size_t n, int start, int step)
{
    size_t m = 0, i, k;
    double *t = malloc(sizeof(double) * (n / step + 1));
    double r;

    if (!t)
        return 0;
    for (i = start; i < n; i += step)
        t[m++] = v[i];
    if (!m) {
        free(t);
        return 0;
    }
    k = (size_t)(m * 0.99);
    if (k >= m)
        k = m - 1;
    for (i = 0; i <= k; i++) {        /* partial selection sort */
        size_t j, lo = i;
        for (j = i + 1; j < m; j++)
            if (t[j] < t[lo])
                lo = j;
        r = t[i]; t[i] = t[lo]; t[lo] = r;
    }
    r = t[k];
    free(t);
    return r;
}

/* Stationary dark strip: per-sample means over STRIP_LINES lines. */
static SANE_Status
read_dark_strip(int fd, int pixels, double *mean)
{
    size_t line = (size_t)pixels * 6, len = line * STRIP_LINES;
    uint8_t *buf = malloc(len);
    SANE_Status st;
    size_t i;
    int y;

    if (!buf)
        return SANE_STATUS_NO_MEM;

    plustek8200i_write_reg(fd, 0x0D, 0x05);
    plustek8200i_write_reg(fd, 0x0D, 0x07);
    plustek8200i_write_reg(fd, 0x01, 0x03);     /* SHDAREA | SCAN */
    plustek8200i_write_reg(fd, 0x0F, 0x01);     /* START */
    st = plustek8200i_read_scan_data(fd, buf, len);
    plustek8200i_write_reg(fd, 0x01, 0x02);     /* clear SCAN */
    if (st == SANE_STATUS_GOOD) {
        memset(mean, 0, sizeof(double) * pixels * 3);
        for (y = 0; y < STRIP_LINES; y++) {
            const uint8_t *p = buf + (size_t)y * line;
            for (i = 0; i < (size_t)pixels * 3; i++)
                mean[i] += p[i * 2] | (p[i * 2 + 1] << 8);
        }
        for (i = 0; i < (size_t)pixels * 3; i++)
            mean[i] /= STRIP_LINES;
    }
    free(buf);
    return st;
}

/* AD9826 PGA gain for a 6-bit code. */
static double
afe_gain(int code)
{
    return 6.0 / (1.0 + 5.0 * (63 - code) / 63.0);
}

/* Smallest code whose gain reaches g (clamped to 0..63). */
static int
afe_code_for(double g)
{
    int code;
    for (code = 0; code < 63; code++)
        if (afe_gain(code) >= g)
            return code;
    return 63;
}

static SANE_Status
write_afe_gains(int fd, const int code[3])
{
    int c;
    for (c = 0; c < 3; c++) {
        uint8_t block[6] = { 0x51, (uint8_t)(0x02 + c), 0x5D, 0x00,
                             0x5E, (uint8_t)code[c] };
        SANE_Status st = plustek8200i_write_regs(fd, block, sizeof(block));
        if (st != SANE_STATUS_GOOD)
            return st;
    }
    return SANE_STATUS_GOOD;
}

/* Stationary strip with the lamp on (lamp_on) or off. */
static SANE_Status
strip(int fd, int pixels, int lamp_on, double *mean)
{
    plustek8200i_write_reg(fd, 0x03, lamp_on ? 0x30 : 0x20);
    usleep(300000);
    return read_dark_strip(fd, pixels, mean);
}

SANE_Status
plustek8200i_shading_calibrate(int fd, int pixels, int img_lincnt,
                               int img_feedl, int img_table_period,
                               int img_exposure, int auto_gain,
                               plustek8200i_shading_stats_t *stats)
{
    size_t n = (size_t)pixels * 3, i;
    double *dark = calloc(n, sizeof(double));
    double *white = calloc(n, sizeof(double));
    double *gain = calloc(n, sizeof(double));
    uint8_t *table = calloc(1, table_bytes(pixels) + 512);
    SANE_Status st = SANE_STATUS_NO_MEM;
    int c, code[3] = { 0, 0, 0 };
    double trim[3] = { 1.0, 1.0, 1.0 };

    if (!dark || !white || !gain || !table)
        goto out;

    plustek8200i_regtrace_mark("SHADING: strips");
    write24(fd, 0x25, STRIP_LINES);             /* LINCNT */
    write24(fd, 0x3D, 1);                       /* FEEDL: no feed */
    write24(fd, 0x7D, PLUSTEK8200I_BASE_EXPOSURE); /* gains are for x1.0 */
    plustek8200i_write_reg(fd, 0x02, 0x00);     /* motor off */
    plustek8200i_write_reg(fd, 0x01, 0x02);     /* SHDAREA, no DVDSET */

    if (auto_gain && (st = write_afe_gains(fd, code)) != SANE_STATUS_GOOD)
        goto out;
    /*
     * The lamp has to be steady before its brightness is measured: after
     * the --ir-clean infrared pass (white lamp off for ~25 s) it was still
     * brightening, the gains came out 2% high for the image that followed.
     * Repeat the white strip until two in a row agree to 0.2%.
     */
    {
        double prev[3] = { 0, 0, 0 };
        int tries;
        for (tries = 1; tries <= 8; tries++) {
            double cur[3] = { 0, 0, 0 }, worst = 0;
            if ((st = strip(fd, pixels, 1, white)) != SANE_STATUS_GOOD)
                goto out;
            for (i = 0; i < n; i++)
                cur[i % 3] += white[i];
            for (c = 0; c < 3; c++) {
                double d = prev[c] > 0 ? cur[c] / prev[c] - 1.0 : 1.0;
                if (d < 0) d = -d;
                if (d > worst) worst = d;
                prev[c] = cur[c];
            }
            if (worst < 0.002) {
                DBG(DBG_info, "SHADING: lamp steady after %d white strips\n", tries);
                break;
            }
            if (tries == 8)
                DBG(DBG_warning, "SHADING: lamp still drifting (%.2f%% between strips)\n",
                    100.0 * worst);
        }
    }
    if ((st = strip(fd, pixels, 0, dark)) != SANE_STATUS_GOOD)
        goto out;
    for (i = 0; i < n; i++)
        white[i] -= dark[i];                    /* open light, per sample */

    for (c = 0; c < 3; c++) {
        double top = robust_max(white, n, c, 3), raw_top = 0;
        int x;
        for (x = 0; x < pixels; x++)
            if (white[x * 3 + c] + dark[x * 3 + c] > raw_top)
                raw_top = white[x * 3 + c] + dark[x * 3 + c];
        stats->white[c] = top;
        if (raw_top > 65000 || top < 1000) {
            DBG(DBG_warning, "SHADING: open light on channel %d unusable "
                "(top %.0f, raw peak %.0f) -- shading NOT applied\n",
                c, top, raw_top);
            st = SANE_STATUS_INVAL;
            goto out;
        }
        if (auto_gain)
            code[c] = afe_code_for(AUTO_TARGET * 65535.0 / top * afe_gain(0));
        stats->code[c] = code[c];
    }

    /*
     * AFE codes are ~1.5% steps, and a code flipping between two scans
     * would shift a frame's level by that much -- bad for a roll that is
     * meant to come out consistent. The remainder is therefore taken up
     * digitally in the shading gains, so the open light lands on the
     * target exactly; this also brings a channel down that is too bright
     * even at the lowest code (green: 84% of full scale at x1.0).
     */
    for (c = 0; c < 3; c++)
        trim[c] = auto_gain ? AUTO_TARGET * 65535.0
                              / (stats->white[c] * afe_gain(code[c]) / afe_gain(0))
                            : 1.0;

    if (auto_gain) {
        if ((st = write_afe_gains(fd, code)) != SANE_STATUS_GOOD
            || (st = strip(fd, pixels, 0, dark)) != SANE_STATUS_GOOD)
            goto out;
    }

    /*
     * Gains, relative: each sample is scaled to the brightest samples of
     * its own class. Samples are R,G,B per pixel and even and odd pixels
     * come from different CCD taps, so i % 6 is one colour on one tap.
     * The brightest part of the line keeps its level, the falloff towards
     * the ends is lifted and tap mismatch evened out. Samples with little
     * light (dummy pixels outside the window) keep unity gain.
     */
    {
        int phase;
        for (phase = 0; phase < 6; phase++) {
            double ref = robust_max(white, n, phase, 6);
            for (i = phase; i < n; i += 6) {
                if (ref < 1000.0 || white[i] < 0.5 * ref)
                    gain[i] = GAIN_UNITY;
                else
                    gain[i] = ref * GAIN_UNITY / white[i] * trim[phase % 3];
            }
        }
    }
    for (c = 0; c < 3; c++) {
        double ref = robust_max(white, n, c, 3), lo = 1e9, d = 0, dmin = 1e9;
        int x, lit = 0;
        for (x = 0; x < pixels; x++) {
            double w = white[x * 3 + c];
            if (w < 0.5 * ref)
                continue;
            if (w < lo) lo = w;
            if (dark[x * 3 + c] < dmin) dmin = dark[x * 3 + c];
            d += dark[x * 3 + c];
            lit++;
        }
        stats->dark[c] = lit ? d / lit : 0;
        stats->gain_max[c] = (lit && lo > 0) ? ref / lo : 1.0;
        DBG(DBG_info, "SHADING: ch%d open light %.0f at code 0 -> AFE code %d "
            "(x%.3f) and trim x%.4f -> %.0f; dark %.0f (min %.0f); falloff gain up to %.3f\n",
            c, stats->white[c], code[c], afe_gain(code[c]), trim[c],
            stats->white[c] * afe_gain(code[c]) / afe_gain(0) * trim[c],
            stats->dark[c], dmin, stats->gain_max[c]);
        if (dmin < 30)
            DBG(DBG_warning, "SHADING: WARNING ch%d dark level %.0f is close to 0 -- "
                "raise the AFE offset or dark detail clips\n", c, dmin);
    }

    build_table(table, pixels, dark, gain);
    st = plustek8200i_dma_write(fd, TABLE_ADDR, table, table_bytes(pixels));

out:
    /* Back to the image pass's values. */
    plustek8200i_write_reg(fd, 0x03, 0x30);
    usleep(100000);
    write24(fd, 0x25, (uint32_t)img_lincnt);
    write24(fd, 0x3D, (uint32_t)img_feedl);
    write24(fd, 0x7D, (uint32_t)img_exposure);
    plustek8200i_load_scan_motor_table(fd, img_table_period);
    plustek8200i_write_reg(fd, 0x02, 0x30);
    plustek8200i_write_reg(fd, 0x01, 0x02);
    free(dark);
    free(white);
    free(gain);
    free(table);
    return st;
}

/* IR LED (0x37 bit 2) on or off, keeping the register's other bits. */
static void
ir_led(int fd, int on)
{
    uint8_t cur = 0;
    plustek8200i_read_reg(fd, 0x37, &cur);
    plustek8200i_write_reg(fd, 0x37, on ? (cur | 0x04) : (cur & ~0x04));
}

/*
 * Infrared pass: measure the open IR light with a stationary strip (IR LED
 * on) minus a dark strip (IR LED off), at the image's own exposure and
 * AFE gains, and return the factor that brings it to IR_REF_LEVEL. The
 * caller scales the IR image by it.
 *
 * Why: the IR level depended on what used the scanner last -- after a
 * SilverFast session it read 37.5k instead of 48-51k until the next power
 * cycle. The state SilverFast leaves is not in the register file, the boot
 * blobs, its lamp sequences, its AFE values, its table writes or its
 * vendor reads (each replayed on a freshly power-cycled scanner without
 * effect), so it is measured and compensated instead.
 *
 * Digital, not analog: in the clean state the open IR light is already
 * 58-65k at the lowest AFE gain, so gains cannot bring the two states to
 * one level. The stationary strip does not respond monotonically to
 * exposure either (3500 -> 49.5k, 7000 -> 43.7k), so it is taken at the
 * image exposure, and green -- the dimmest channel, unclipped in both
 * states -- is the reference.
 */
#define IR_REF_LEVEL 58000.0

SANE_Status
plustek8200i_ir_reference(int fd, int pixels, int img_lincnt, int img_feedl,
                          int img_table_period, int img_exposure,
                          double *scale, double *gain)
{
    size_t n = (size_t)pixels * 3, i;
    double *ir = calloc(n, sizeof(double));
    double *dark = calloc(n, sizeof(double));
    SANE_Status st = SANE_STATUS_NO_MEM;
    double top = 0, raw = 0;
    int gain_ok = 0;

    *scale = 1.0;
    if (gain)
        gain[0] = 0.0;          /* "unusable" until the strips prove out */
    if (!ir || !dark)
        goto out;

    plustek8200i_regtrace_mark("IR REFERENCE: strips");
    write24(fd, 0x25, STRIP_LINES);
    write24(fd, 0x3D, 1);
    plustek8200i_write_reg(fd, 0x02, 0x00);
    plustek8200i_write_reg(fd, 0x01, 0x02);

    ir_led(fd, 1);
    usleep(300000);
    if ((st = read_dark_strip(fd, pixels, ir)) != SANE_STATUS_GOOD)
        goto out;
    ir_led(fd, 0);
    usleep(300000);
    st = read_dark_strip(fd, pixels, dark);
    ir_led(fd, 1);
    if (st != SANE_STATUS_GOOD)
        goto out;
    /*
     * The same two strips give a per-column gain, which is what the
     * infrared pass has always been missing: it cannot be shading
     * corrected in hardware, so without this every sensor column keeps its
     * own offset and the pattern lands in the noise floor a frontend
     * measures.  This costs no extra scanning -- the strips are read
     * above either way.
     *
     * Built here, before the loop below subtracts the dark strip from the
     * green channel of `ir` in place: plustek8200i_irflat_open does its
     * own subtraction over all three channels, so afterwards green would
     * be subtracted twice.
     */
    if (gain)
        gain_ok = plustek8200i_irflat_build(ir, dark, pixels, gain) == 0;

    for (i = 1; i < n; i += 3) {            /* green */
        if (ir[i] > raw) raw = ir[i];
        ir[i] -= dark[i];
    }
    top = robust_max(ir, n, 1, 3);
    if (raw > 65000 || top < 1000) {
        DBG(DBG_warning, "IR REFERENCE: unusable (green %.0f, raw peak %.0f) -- "
            "IR not normalised\n", top, raw);
        st = SANE_STATUS_INVAL;
        goto out;
    }
    *scale = IR_REF_LEVEL / top;
    DBG(DBG_info, "IR REFERENCE: open IR light %.0f (green) -> IR scaled x%.3f\n",
        top, *scale);

    if (gain && gain_ok) {
        double glo = gain[0], ghi = gain[0];
        int x;

        for (x = 1; x < pixels; x++) {
            if (gain[x] < glo) glo = gain[x];
            if (gain[x] > ghi) ghi = gain[x];
        }
        DBG(DBG_info, "IR REFERENCE: per-column flat field over %d px, "
            "gain %.3f-%.3f (the scalar it replaces was %.3f)\n",
            pixels, glo, ghi, *scale);
    } else if (gain) {
        DBG(DBG_warning, "IR REFERENCE: per-column flat field rejected -- "
            "falling back to the scalar\n");
        gain[0] = 0.0;
    }

out:
    ir_led(fd, 1);
    write24(fd, 0x25, (uint32_t)img_lincnt);
    write24(fd, 0x3D, (uint32_t)img_feedl);
    write24(fd, 0x7D, (uint32_t)img_exposure);
    plustek8200i_load_scan_motor_table(fd, img_table_period);
    plustek8200i_write_reg(fd, 0x02, 0x30);
    plustek8200i_write_reg(fd, 0x01, 0x02);
    free(ir);
    free(dark);
    return st;
}
