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
 * the scanner: the scan geometry.
 *
 * sane_get_parameters() has to predict the frame before sane_start() has
 * computed anything, so it shares these helpers with the scan path. The
 * expected numbers below were measured on the hardware -- each one is the
 * frame an actual scan delivered -- so a change that silently alters the
 * geometry fails here rather than in somebody's image.
 */

#include "../../../backend/plustek8200i.h"

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

int
main(void)
{
    test_frame_geometry();
    test_vertical_sampling();
    test_monotonic();
    test_button_latch();

    printf("\n%s: %d failure%s\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
