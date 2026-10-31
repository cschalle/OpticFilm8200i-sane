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

#ifndef PLUSTEK8200I_IRFLAT_H
#define PLUSTEK8200I_IRFLAT_H

/*
 * Per-column flat field for the infrared plane.
 *
 * The colour pass is shading-corrected in hardware, but the infrared pass
 * cannot be: DVDSET clips IR, so the table is not loaded and the whole
 * calibration is skipped whenever infrared is in use.  In its place
 * plustek8200i_ir_reference() measured the open IR light and returned one
 * scalar, so every sensor column was divided by the same number and each
 * column kept its own offset.
 *
 * That offset is not noise and does not average away.  Measured against
 * SilverFast's 64-bit HDRi export of the same frame, after fitting out the
 * lamp surface and the picture's ghost in the red channel:
 *
 *     spread of the IR residual   ours      SilverFast
 *     pixel to pixel along a row  0.00814   0.00820
 *     line to line down a column  0.00806   0.00873
 *     column means                0.01999   0.00211     <-- 9.5x
 *
 * The random noise is the same; the column pattern is ours alone.  It
 * raised the residual MAD a frontend measures from 0.0081 to 0.0229, and
 * since dust detection thresholds on a multiple of that MAD, a 2-4 px
 * speck sat at 6.5 MAD instead of 18.9 and fell under the bar: 28% of them
 * were found instead of 98%.
 *
 * The cure needs no extra scanning.  ir_reference already reads a full
 * per-pixel strip with the IR LED on and another with it off, and then
 * throws all but one number away.  Keeping them gives a per-column gain,
 * which is ordinary flat fielding: a clear column maps to the reference
 * level whatever its own sensitivity.
 *
 * Removing a per-column median from the residual in post, as a check on
 * the same scans, took the MAD from 0.02289 to 0.00806 -- below
 * SilverFast's 0.01040 -- and the speck depth from 6.49 to 18.90 MAD.
 */

/* Level a clear column is normalised to.  Below 65535 so that noise above
 * the open-light reference has somewhere to go. */
#define PLUSTEK8200I_IRFLAT_REF 58000.0
/* Open IR light a usable strip must reach, in sensor counts. */
#define PLUSTEK8200I_IRFLAT_MIN_REF 1000.0
/* How far from the median a column may be and still be believed. */
#define PLUSTEK8200I_IRFLAT_SPAN 4.0
/* Columns that may need clamping before the whole table is distrusted. */
#define PLUSTEK8200I_IRFLAT_MAX_CLAMPED_PCT 2
/* Narrower than this is not a sensor line. */
#define PLUSTEK8200I_IRFLAT_MIN_PIXELS 16
/* Histogram for the median: 256 bins over the 16-bit range is far finer
 * than a factor-of-four sanity band needs, and costs no allocation. */
#define PLUSTEK8200I_IRFLAT_BINS 256

/*
 * Open IR light at column `x`: the lit strip less the dark strip, averaged
 * over the three sensor channels.
 *
 * The three are averaged because that is what the image path forms -- both
 * the streaming and the full-buffer path emit (r + g + b) / 3 for an
 * infrared line -- so the gain divides out exactly the quantity it
 * corrects.  The old scalar was measured on green alone, which is why it
 * could not have been applied per column without this mismatch showing up.
 */
static inline double
plustek8200i_irflat_open(const double *lit, const double *dark, int x)
{
    double v = ((lit[x * 3] - dark[x * 3])
                + (lit[x * 3 + 1] - dark[x * 3 + 1])
                + (lit[x * 3 + 2] - dark[x * 3 + 2])) / 3.0;

    return v > 0.0 ? v : 0.0;
}

/*
 * Build the per-column gain from the two strips, each `pixels * 3` samples
 * of interleaved sensor channels.
 *
 * Returns 0 with `gain` filled, or -1 if the strips cannot be trusted --
 * the open light never gets going, or too many columns are implausible
 * against the rest.  On failure `gain` holds nothing meaningful and the
 * caller must keep using the scalar, which is exactly the old behaviour.
 *
 * Note that a clear column maps to PLUSTEK8200I_IRFLAT_REF whatever its
 * own sensitivity, so unlike the scalar this cannot push a bright column
 * past full scale: with the scalar, the columns above the 99th percentile
 * it was anchored to already ran over.
 */
static inline int
plustek8200i_irflat_build(const double *lit, const double *dark, int pixels,
                          double *gain)
{
    int hist[PLUSTEK8200I_IRFLAT_BINS];
    int x, b, clamped = 0;
    long acc = 0;
    double med, lo, hi;

    if (!lit || !dark || !gain || pixels < PLUSTEK8200I_IRFLAT_MIN_PIXELS)
        return -1;

    for (b = 0; b < PLUSTEK8200I_IRFLAT_BINS; b++)
        hist[b] = 0;

    for (x = 0; x < pixels; x++) {
        double v = plustek8200i_irflat_open(lit, dark, x);

        gain[x] = v;
        b = (int)(v * PLUSTEK8200I_IRFLAT_BINS / 65536.0);
        if (b < 0)
            b = 0;
        if (b >= PLUSTEK8200I_IRFLAT_BINS)
            b = PLUSTEK8200I_IRFLAT_BINS - 1;
        hist[b]++;
    }

    for (b = 0; b < PLUSTEK8200I_IRFLAT_BINS; b++) {
        acc += hist[b];
        if (acc * 2 >= pixels)
            break;
    }
    if (b >= PLUSTEK8200I_IRFLAT_BINS)
        b = PLUSTEK8200I_IRFLAT_BINS - 1;
    med = (b + 0.5) * (65536.0 / PLUSTEK8200I_IRFLAT_BINS);
    if (med < PLUSTEK8200I_IRFLAT_MIN_REF)
        return -1;

    lo = med / PLUSTEK8200I_IRFLAT_SPAN;
    hi = med * PLUSTEK8200I_IRFLAT_SPAN;
    for (x = 0; x < pixels; x++) {
        double v = gain[x];

        if (v < lo) {
            v = lo;
            clamped++;
        } else if (v > hi) {
            v = hi;
            clamped++;
        }
        gain[x] = PLUSTEK8200I_IRFLAT_REF / v;
    }
    if (clamped * 100 > pixels * PLUSTEK8200I_IRFLAT_MAX_CLAMPED_PCT)
        return -1;
    return 0;
}

/*
 * Gain for output column `x` of a row that has already been mirrored.
 *
 * Both image paths flip the row before the infrared is formed -- the
 * sensor clocks pixels out right to left -- while the calibration strips
 * are read straight off the sensor.  So output column x is sensor column
 * pixels - 1 - x, and getting this backwards would double the very pattern
 * the table removes rather than cancel it.
 */
static inline double
plustek8200i_irflat_gain(const double *gain, int pixels, int x, double fallback)
{
    if (!gain || x < 0 || x >= pixels)
        return fallback;
    return gain[pixels - 1 - x];
}

#endif
