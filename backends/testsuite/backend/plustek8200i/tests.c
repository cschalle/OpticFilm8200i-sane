/* sane - Scanner Access Now Easy.

   Copyright (C) 2026 Christian Schaller

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
 * Unit tests for the parts of the plustek8200i backend that do not need
 * the scanner: the scan geometry, the button latch, the feed register
 * save/restore, and the vertical resample kernel.
 *
 * sane_get_parameters() has to predict the frame before sane_start() has
 * computed anything, so it shares these helpers with the scan path. The
 * expected numbers below were measured on the hardware -- each one is the
 * frame an actual scan delivered -- so a change that silently alters the
 * geometry fails here rather than in somebody's image.
 */

#include "../../../backend/plustek8200i.h"
#include "../../../backend/plustek8200i-feed.h"
#include "../../../backend/plustek8200i-vresample.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

static void
check_int(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL %-44s got %d, want %d\n", what, got, want);
        failures++;
    } else {
        printf("ok   %-44s %d\n", what, got);
    }
}

static void
check_near(const char *what, double got, double want, double tol)
{
    if (!(got - want < tol && want - got < tol)) {
        printf("FAIL %-44s got %.4f, want %.4f +-%.4f\n", what, got, want, tol);
        failures++;
    } else {
        printf("ok   %-44s %.4f\n", what, got);
    }
}

/* dpi -> pixels per line, lines: measured on the hardware. */
static const struct { int dpi, pixels, lines; } frames[] = {
    {  300,   432,  322 },
    {  600,   864,  645 },
    { 1200,  1728, 1291 },
    { 2400,  3456, 2585 },
    { 3600,  5184, 3877 },
    { 7200, 10368, 7755 },
};

static void
test_frame_geometry(void)
{
    size_t i;
    char name[96];

    for (i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        sprintf(name, "%d dpi: pixels per line", frames[i].dpi);
        check_int(name, plustek8200i_full_frame_pixels(frames[i].dpi),
                  frames[i].pixels);
        sprintf(name, "%d dpi: lines", frames[i].dpi);
        check_int(name, plustek8200i_full_frame_lines(frames[i].dpi),
                  frames[i].lines);
    }
}

static void
test_vertical_sampling(void)
{
    /* Below the hardware line rate the motor keeps its known-good speed
     * and the surplus lines are resampled away; above it the motor slows
     * so the lines genuinely exist. */
    check_int("target vdpi at 300 dpi clamps to the floor",
              plustek8200i_target_vdpi(300), PLUSTEK8200I_HW_LINE_DPI);
    check_int("target vdpi at 1200 dpi clamps to the floor",
              plustek8200i_target_vdpi(1200), PLUSTEK8200I_HW_LINE_DPI);
    check_int("target vdpi at 7200 dpi follows the request",
              plustek8200i_target_vdpi(7200), 7200);

    /* The same raw lines serve every resolution under the floor. */
    check_int("raw lines at 300 dpi", plustek8200i_raw_frame_lines(300), 1604);
    check_int("raw lines at 1200 dpi", plustek8200i_raw_frame_lines(1200), 1604);
    check_int("raw lines at 7200 dpi", plustek8200i_raw_frame_lines(7200), 7755);

    /* The period that produces the default vertical sampling. */
    check_int("motor period at the default sampling",
              plustek8200i_vdpi_period(PLUSTEK8200I_HW_LINE_DPI),
              PLUSTEK8200I_BASE_PERIOD);
}

static void
test_monotonic(void)
{
    size_t i;

    for (i = 1; i < sizeof(frames) / sizeof(frames[0]); i++) {
        int lo = plustek8200i_full_frame_pixels(frames[i - 1].dpi);
        int hi = plustek8200i_full_frame_pixels(frames[i].dpi);
        char name[96];

        sprintf(name, "pixels increase from %d to %d dpi",
                frames[i - 1].dpi, frames[i].dpi);
        check_int(name, hi > lo, 1);
    }
}

/*
 * The button latch, which a frontend reads by polling: a press must be
 * reported exactly once, and must survive until someone asks for it.
 * These are the pure halves of plustek8200i-button.c, so no scanner is
 * needed; the codes themselves were measured on the hardware.
 */
static void
test_button_latch(void)
{
    unsigned latch = 0;

    check_int("empty latch reports no QuickScan",
              plustek8200i_button_take(&latch, PLUSTEK8200I_BTN_QUICKSCAN), 0);

    /* A press is latched, reported once, and then gone. */
    latch = plustek8200i_button_fold(0, PLUSTEK8200I_BTN_INTELLISCAN);
    check_int("IntelliScan press is latched", latch != 0, 1);
    check_int("IntelliScan reads as pressed",
              plustek8200i_button_take(&latch, PLUSTEK8200I_BTN_INTELLISCAN), 1);
    check_int("IntelliScan does not read twice",
              plustek8200i_button_take(&latch, PLUSTEK8200I_BTN_INTELLISCAN), 0);

    /* Reading one button must not consume the other. */
    latch = plustek8200i_button_fold(0, PLUSTEK8200I_BTN_INTELLISCAN);
    latch = plustek8200i_button_fold(latch, PLUSTEK8200I_BTN_QUICKSCAN);
    check_int("QuickScan read leaves IntelliScan alone",
              plustek8200i_button_take(&latch, PLUSTEK8200I_BTN_QUICKSCAN), 1);
    check_int("IntelliScan still latched afterwards",
              plustek8200i_button_take(&latch, PLUSTEK8200I_BTN_INTELLISCAN), 1);

    /* Two presses between polls collapse into one report: the hardware
     * sends one packet per press, and a frontend acts once either way. */
    latch = plustek8200i_button_fold(0, PLUSTEK8200I_BTN_QUICKSCAN);
    latch = plustek8200i_button_fold(latch, PLUSTEK8200I_BTN_QUICKSCAN);
    check_int("repeated presses collapse to one report",
              plustek8200i_button_take(&latch, PLUSTEK8200I_BTN_QUICKSCAN), 1);
    check_int("and nothing is left over",
              plustek8200i_button_take(&latch, PLUSTEK8200I_BTN_QUICKSCAN), 0);

    /* A byte this panel cannot produce is not a button. */
    check_int("unknown packet bits are ignored",
              (int)plustek8200i_button_fold(0, 0x21), 0);
    check_int("known bits survive an unknown neighbour",
              (int)plustek8200i_button_fold(0, PLUSTEK8200I_BTN_QUICKSCAN | 0x01),
              PLUSTEK8200I_BTN_QUICKSCAN);
}

/*
 * The pure half of the feed register save/restore.  A positioning feed saves
 * the registers it is about to change and puts them back afterwards, and the
 * restore runs whether or not the save succeeded.  A save that stopped early
 * -- a control read of 0x05 failing between the colour and infrared frames at
 * 1200 dpi did it -- used to leave the count uninitialised, and the restore
 * then wrote two bytes per phantom entry past the end of an 84-byte stack
 * buffer.  These check the count is trusted only as far as it can be.
 */
static void
test_feed_pairs(void)
{
    uint8_t pairs[2 * PLUSTEK8200I_FEED_REGS];
    const uint8_t *touched = plustek8200i_feed_touched();
    plustek8200i_feed_saved_t sv;
    size_t i;

    for (i = 0; i < sizeof(sv.val); i++)
        sv.val[i] = (uint8_t)(0x10 + i);

    sv.n = PLUSTEK8200I_FEED_REGS;
    check_int("a full save writes every register",
              plustek8200i_feed_pairs(&sv, pairs), 2 * PLUSTEK8200I_FEED_REGS);
    check_int("and pairs each register with its value",
              pairs[0] == touched[0] && pairs[1] == sv.val[0]
              && pairs[2 * (PLUSTEK8200I_FEED_REGS - 1)]
                 == touched[PLUSTEK8200I_FEED_REGS - 1],
              1);

    /* A save that stopped part way restores exactly what it read. */
    sv.n = 3;
    check_int("a partial save writes only what it saved",
              plustek8200i_feed_pairs(&sv, pairs), 6);
    sv.n = 0;
    check_int("a save that read nothing writes nothing",
              plustek8200i_feed_pairs(&sv, pairs), 0);

    /* The counts that used to walk off the end of the buffer. */
    sv.n = 1000000;
    check_int("a wild count is clamped to the register list",
              plustek8200i_feed_pairs(&sv, pairs), 2 * PLUSTEK8200I_FEED_REGS);
    sv.n = -1;
    check_int("a negative count writes nothing",
              plustek8200i_feed_pairs(&sv, pairs), 0);
    sv.n = PLUSTEK8200I_FEED_REGS + 1;
    check_int("one past the end is clamped",
              plustek8200i_feed_pairs(&sv, pairs), 2 * PLUSTEK8200I_FEED_REGS);

    /* The restore buffer is sized from the register list, so the saved
     * values must be able to cover it. */
    check_int("the register list fits the saved values",
              PLUSTEK8200I_FEED_REGS <= (int)sizeof(sv.val), 1);
}

/*
 * The vertical resample kernel.  Every resolution below the hardware line
 * rate arrives with surplus lines to squash away, and the kernel that does
 * it decides how much real detail survives and how much of what is past the
 * new Nyquist folds back as aliasing.  A box area-average was shipped
 * first; Lanczos-3 replaced it above a squash of
 * PLUSTEK8200I_VLANCZOS_MIN.  No scanner is needed for any of this.
 */

/* Amplitude a vertical sinusoid of `period` source lines comes out with. */
static double
resample_gain(double vsquash, int k, double period)
{
    plustek8200i_vweights_t w;
    double re = 0.0, im = 0.0;
    int i;

    if (plustek8200i_vresample_weights(vsquash, k, &w) != 0)
        return -1.0;
    for (i = 0; i < w.n; i++) {
        double ph = 2.0 * PLUSTEK8200I_VPI * (w.first + i) / period;
        re += w.w[i] * cos(ph);
        im += w.w[i] * sin(ph);
    }
    return sqrt(re * re + im * im);
}

/*
 * The shipped box average at an arbitrary squash, which the kernel will no
 * longer build above PLUSTEK8200I_VLANCZOS_MIN.  Here so the two can be
 * compared at the same squash factor -- that contrast is the whole reason
 * for the change.
 */
static double
box_gain(double vsquash, int k, double period)
{
    double y0 = k * vsquash, y1 = (k + 1) * vsquash;
    int i0 = (int)y0, i1 = (int)(y1 - 1e-9), i;
    double wsum = 0.0, re = 0.0, im = 0.0;

    for (i = i0; i <= i1; i++) {
        double lo = (i > y0) ? i : y0;
        double hi = ((i + 1) < y1) ? (i + 1) : y1;
        double ph = 2.0 * PLUSTEK8200I_VPI * i / period;

        if (hi <= lo) continue;
        wsum += hi - lo;
        re += (hi - lo) * cos(ph);
        im += (hi - lo) * sin(ph);
    }
    if (wsum <= 0.0) return -1.0;
    return sqrt(re * re + im * im) / wsum;
}

static double
squash_at(int dpi)
{
    return (double)plustek8200i_target_vdpi(dpi) / (double)dpi;
}

/*
 * Which kernel each resolution gets, and how far it reaches.  The tap
 * count is the number the fallback in both resample paths is there to
 * guard: it must stay under the limit, or a line would be built from the
 * nearest source row instead.
 */
static void
test_vresample_gate(void)
{
    size_t i;
    char name[96];
    /* dpi, squash, taps -- 300 and 600 resample through Lanczos-3, 1200
     * stays on the box, and above the line rate nothing is resampled. */
    static const struct { int dpi, taps; double squash, support; } g[] = {
        {  300, 31, 4.9667, 14.900 },
        {  600, 16, 2.4833,  7.450 },
        { 1200,  2, 1.2417,  0.621 },
        { 2400,  1, 1.0000,  0.000 },
        { 3600,  1, 1.0000,  0.000 },
        { 7200,  1, 1.0000,  0.000 },
    };

    for (i = 0; i < sizeof(g) / sizeof(g[0]); i++) {
        double v = squash_at(g[i].dpi);
        plustek8200i_vweights_t w;

        sprintf(name, "%d dpi: squash", g[i].dpi);
        check_near(name, v, g[i].squash, 0.0005);
        sprintf(name, "%d dpi: kernel reach in source lines", g[i].dpi);
        check_near(name, plustek8200i_vresample_support(v), g[i].support, 0.001);
        sprintf(name, "%d dpi: taps", g[i].dpi);
        check_int(name, plustek8200i_vresample_weights(v, 100, &w) == 0 ? w.n : -1,
                  g[i].taps);
        sprintf(name, "%d dpi: taps fit the weight array", g[i].dpi);
        check_int(name, w.n <= PLUSTEK8200I_VTAPS_MAX, 1);
    }
}

/*
 * A flat field has to come out the same flat field: the weights are
 * normalised, so they sum to one at every output line, including the ones
 * whose kernel hangs off the end of the frame.
 */
static void
test_vresample_dc(void)
{
    static const int dpis[] = { 300, 600, 1200, 2400, 7200 };
    size_t d;
    char name[96];

    for (d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        double v = squash_at(dpis[d]);
        double worst = 0.0;
        int k, bad = 0;

        for (k = 0; k < 400; k++) {
            plustek8200i_vweights_t w;
            double sum = 0.0;
            int i;

            if (plustek8200i_vresample_weights(v, k, &w) != 0) { bad++; continue; }
            for (i = 0; i < w.n; i++) sum += w.w[i];
            if (fabs(sum - 1.0) > worst) worst = fabs(sum - 1.0);
        }
        sprintf(name, "%d dpi: every line has weights", dpis[d]);
        check_int(name, bad, 0);
        sprintf(name, "%d dpi: weights sum to one", dpis[d]);
        check_near(name, 1.0 + worst, 1.0, 1e-9);
    }
}

/*
 * 1200 dpi keeps the shipped area-average, so its output must stay
 * bit-identical: at a squash of 1.24 the band-limiting needed is slight,
 * the box is already close to ideal, and a wide windowed sinc mostly adds
 * ringing.  These are the weights the old inline loop computed.
 */
static void
test_vresample_box_unchanged(void)
{
    double v = squash_at(1200);
    double worst = 0.0;
    int k, shape = 1;

    for (k = 0; k < 400; k++) {
        plustek8200i_vweights_t w;
        double y0 = k * v, y1 = (k + 1) * v, wsum = 0.0;
        int i0 = (int)y0, i1 = (int)(y1 - 1e-9), i;

        if (plustek8200i_vresample_weights(v, k, &w) != 0) { shape = 0; break; }
        if (w.first != i0 || w.n != i1 - i0 + 1) { shape = 0; break; }
        for (i = i0; i <= i1; i++) {
            double lo = (i > y0) ? i : y0;
            double hi = ((i + 1) < y1) ? (i + 1) : y1;
            wsum += hi - lo;
        }
        for (i = i0; i <= i1; i++) {
            double lo = (i > y0) ? i : y0;
            double hi = ((i + 1) < y1) ? (i + 1) : y1;
            double want = (hi - lo) / wsum;
            if (fabs(w.w[i - i0] - want) > worst) worst = fabs(w.w[i - i0] - want);
        }
    }
    check_int("1200 dpi: box covers the same source lines", shape, 1);
    check_near("1200 dpi: box weights are unchanged", 1.0 + worst, 1.0, 1e-12);
}

/*
 * What the change bought, as frequency response at 600 dpi.  Periods are
 * in output pixels: 4 is half the output Nyquist and has to survive, 2 is
 * the Nyquist itself, and anything shorter can only come back as aliasing
 * and has to be thrown away.  The box passes a third of the energy at 1.5
 * output pixels straight into the image; Lanczos-3 passes a twentieth, and
 * still holds more of the passband.
 */
static void
test_vresample_response(void)
{
    double v = squash_at(600);
    double box = squash_at(1200);

    check_near("600 dpi: gain at 4 output px (keep)",
               resample_gain(v, 100, 4.0 * v), 1.0116, 0.002);
    check_near("600 dpi: gain at the Nyquist",
               resample_gain(v, 100, 2.0 * v), 0.5018, 0.002);
    check_near("600 dpi: gain at 1.5 output px (alias)",
               resample_gain(v, 100, 1.5 * v), 0.0485, 0.002);
    check_near("600 dpi: gain at 1.2 output px (alias)",
               resample_gain(v, 100, 1.2 * v), 0.0046, 0.002);

    /*
     * The box at the same squash, which is what 600 dpi used to get: it
     * loses an eighth of the passband Lanczos-3 keeps, and passes eight
     * times as much of what can only alias.
     */
    check_near("box at 600 dpi squash: gain at 4 output px",
               box_gain(v, 100, 4.0 * v), 0.8852, 0.002);
    check_near("box at 600 dpi squash: gain at 1.5 output px",
               box_gain(v, 100, 1.5 * v), 0.3389, 0.002);
    check_int("Lanczos-3 keeps more of the passband",
              resample_gain(v, 100, 4.0 * v) > box_gain(v, 100, 4.0 * v), 1);
    check_int("and passes far less of the alias band",
              resample_gain(v, 100, 1.5 * v) < box_gain(v, 100, 1.5 * v) / 5.0, 1);

    /*
     * 1200 dpi keeps the box, so its response is shipped behaviour: a
     * third of the energy at 1.5 output pixels still folds back.  That is
     * the known cost of the gate, and at a squash of 1.24 it is cheaper
     * than the ringing the alternative brings.
     */
    check_near("1200 dpi box: gain at 4 output px",
               resample_gain(box, 100, 4.0 * box), 0.8316, 0.002);
    check_near("1200 dpi box: gain at 1.5 output px",
               resample_gain(box, 100, 1.5 * box), 0.3591, 0.002);

    /* It is a windowed sinc, not a blur: the negative lobes are what sharpen
     * the passband, and they are why both paths clamp below zero as well as
     * above. */
    {
        plustek8200i_vweights_t w;
        double lo = 0.0;
        int i;

        check_int("600 dpi: weights built", plustek8200i_vresample_weights(v, 100, &w), 0);
        for (i = 0; i < w.n; i++) if (w.w[i] < lo) lo = w.w[i];
        check_int("600 dpi: Lanczos has negative lobes", lo < -0.01, 1);
        check_int("a negative accumulation clamps to black",
                  (int)plustek8200i_vresample_clamp(-1234.5), 0);
        check_int("an overshoot clamps to white",
                  (int)plustek8200i_vresample_clamp(70000.0), 65535);
        check_int("and the range between is rounded",
                  (int)plustek8200i_vresample_clamp(1234.6), 1235);
    }
}

/*
 * The frame edges, and the window the streaming ring has to hold.  A
 * kernel that hangs off the top or bottom of the frame reads the first or
 * last line again rather than dropping taps, which would leave a signed
 * kernel normalised against a partial sum.
 */
static void
test_vresample_edges(void)
{
    double v = squash_at(600);
    plustek8200i_vweights_t w;
    int src_lines = 1604, i, off = 0;

    check_int("first line: weights built", plustek8200i_vresample_weights(v, 0, &w), 0);
    check_int("first line: kernel reaches above the frame", w.first < 0, 1);
    for (i = 0; i < w.n; i++) {
        int si = plustek8200i_vresample_src(&w, i, src_lines);
        if (si < 0 || si >= src_lines) off++;
    }
    check_int("first line: every tap lands inside the frame", off, 0);
    check_int("first line: taps above the top read line 0",
              plustek8200i_vresample_src(&w, 0, src_lines), 0);

    off = 0;
    check_int("last line: weights built",
              plustek8200i_vresample_weights(v, (int)(src_lines / v) - 1, &w), 0);
    for (i = 0; i < w.n; i++) {
        int si = plustek8200i_vresample_src(&w, i, src_lines);
        if (si < 0 || si >= src_lines) off++;
    }
    check_int("last line: every tap lands inside the frame", off, 0);
    check_int("last line: taps past the end read the last line",
              plustek8200i_vresample_src(&w, w.n - 1, src_lines), src_lines - 1);
}

/*
 * The streaming ring is sized from the kernel's support rather than from
 * an assumed area-average, because channel registration makes one output
 * line read 2 * shift output pitches of source as well.  If the sizing
 * ever came up short the reader would overwrite a line the resampler had
 * not used yet, so check the geometry covers the real window.
 */
static void
test_vresample_ring(void)
{
    static const int dpis[] = { 300, 600, 1200, 2400, 7200 };
    size_t d;
    char name[96];

    for (d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        int dpi = dpis[d], shift = dpi / 300, y, short_by = 0;
        double v = squash_at(dpi);
        double sup = plustek8200i_vresample_support(v);
        double vr = v <= PLUSTEK8200I_VNONE ? 1.0 : v;
        /* The geometric half of the ring in plustek8200i.c; the rest is
         * slack for the USB reader. */
        int ring = (int)(2 * shift * vr + 2 * sup + 0.999) + 3;

        for (y = shift; y < 400; y++) {
            int lo, hi, unused;

            plustek8200i_vresample_span(v, y - shift, &lo, &unused);
            plustek8200i_vresample_span(v, y + shift, &unused, &hi);
            if (hi - lo + 1 > ring) short_by++;
        }
        sprintf(name, "%d dpi: ring covers the resample window", dpi);
        check_int(name, short_by, 0);
    }
}

int
main(void)
{
    test_frame_geometry();
    test_vertical_sampling();
    test_monotonic();
    test_button_latch();
    test_feed_pairs();
    test_vresample_gate();
    test_vresample_dc();
    test_vresample_box_unchanged();
    test_vresample_response();
    test_vresample_edges();
    test_vresample_ring();

    printf("\n%s: %d failure%s\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
