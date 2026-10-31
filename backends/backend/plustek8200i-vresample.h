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

#ifndef PLUSTEK8200I_VRESAMPLE_H
#define PLUSTEK8200I_VRESAMPLE_H

#include <math.h>
#include <stdint.h>

/*
 * Vertical resample kernel.
 *
 * The sensor samples ~2.6x finer along the scan direction than across it,
 * so every resolution below the hardware line rate arrives with surplus
 * lines that have to be squashed away to get square pixels (squash 4.97 at
 * 300 dpi, 2.48 at 600, 1.24 at 1200, none above).  Both the streaming and
 * the full-buffer path resample, and the streaming path's ring buffer has
 * to be sized from the same support, so the weights live here -- one
 * definition, and testable without a scanner.
 *
 * A plain box area-average was shipped first.  It is a poor low-pass: it
 * attenuates detail well below the new Nyquist and still passes enough
 * above it to alias.  Scored against an ideal brick-wall band-limited
 * reference on an unresampled 3600 dpi scan of real film, RMS error and
 * mid-band detail (1.000 = ideal):
 *
 *     filter        RMS    detail      at 4.97 (300 dpi)   at 2.48 (600 dpi)
 *     box          120.6    0.937      115.5 / 0.935       100.3 / 0.932
 *     triangle     107.0    0.825
 *     Catmull-Rom   84.5    0.950
 *     Lanczos-2     83.7    0.951
 *     Lanczos-3     69.9    1.017       68.1 / 1.009        64.8 / 1.033
 *
 * So Lanczos-3, except at small squash factors: at 1.24 the band-limiting
 * needed is slight, the box is already close (RMS 48.6, detail 0.969) and a
 * wide windowed sinc mostly adds ringing (58.5, 1.125 -- overshooting the
 * ideal).  Hence the gate below, which keeps 1200 dpi on the box.
 */

/* Squash at or below which no resample happens: the motor was slowed and
 * the lines genuinely exist. */
#define PLUSTEK8200I_VNONE 1.01
/* Squash at or above which Lanczos-3 beats the box average. */
#define PLUSTEK8200I_VLANCZOS_MIN 1.3
/* Taps one output line can read: 6 * squash + 2, so ~31 at 300 dpi, the
 * coarsest resolution.  The unit test pins the real counts under this. */
#define PLUSTEK8200I_VTAPS_MAX 64

#define PLUSTEK8200I_VPI 3.14159265358979323846

typedef struct {
    int    first;                        /* source line of tap 0, may be < 0 */
    int    n;                            /* taps, <= PLUSTEK8200I_VTAPS_MAX */
    double w[PLUSTEK8200I_VTAPS_MAX];    /* normalised: sums to 1 */
} plustek8200i_vweights_t;

static inline double
plustek8200i_vsinc(double x)
{
    if (x > -1e-9 && x < 1e-9)
        return 1.0;
    x *= PLUSTEK8200I_VPI;
    return sin(x) / x;
}

/* Lanczos-3 on a normalised offset: t is in output pitches, so the kernel
 * is stretched to band-limit to the output's Nyquist, not the input's. */
static inline double
plustek8200i_vlanczos3(double t)
{
    if (t <= -3.0 || t >= 3.0)
        return 0.0;
    return plustek8200i_vsinc(t) * plustek8200i_vsinc(t / 3.0);
}

/* Source lines one output line reads each side of its centre. */
static inline double
plustek8200i_vresample_support(double vsquash)
{
    if (vsquash <= PLUSTEK8200I_VNONE)
        return 0.0;
    return (vsquash >= PLUSTEK8200I_VLANCZOS_MIN ? 3.0 : 0.5) * vsquash;
}

/*
 * Source lines output line k reads, as [*first, *last] inclusive and
 * before clamping to the frame.  The streaming path's "is that line here
 * yet" test and its ring size both come from this, so they cannot drift
 * from the weights.
 */
static inline void
plustek8200i_vresample_span(double vsquash, int k, int *first, int *last)
{
    double s = plustek8200i_vresample_support(vsquash);
    double c;

    if (s <= 0.0) {
        *first = *last = k;
        return;
    }
    c = (k + 0.5) * vsquash;
    *first = (int)floor(c - s);
    *last = (int)floor(c + s - 1e-9);
}

/*
 * Weights for output line k.  Returns 0, or -1 if the span will not fit
 * PLUSTEK8200I_VTAPS_MAX or the kernel summed to nothing -- neither
 * happens at the supported resolutions, but a caller must not emit a
 * black line if it ever does.
 *
 * Normalising over the unclamped kernel is deliberate: edge taps are
 * redirected to the first or last line (see _src below), which replicates
 * the edge without changing the weight sum.
 */
static inline int
plustek8200i_vresample_weights(double vsquash, int k,
                               plustek8200i_vweights_t *out)
{
    int first, last, i;
    double sum = 0.0;

    plustek8200i_vresample_span(vsquash, k, &first, &last);
    out->first = first;
    out->n = last - first + 1;
    if (out->n < 1 || out->n > PLUSTEK8200I_VTAPS_MAX)
        return -1;
    if (vsquash <= PLUSTEK8200I_VNONE) {
        out->w[0] = 1.0;
        return 0;
    }
    if (vsquash >= PLUSTEK8200I_VLANCZOS_MIN) {
        double c = (k + 0.5) * vsquash;

        for (i = 0; i < out->n; i++) {
            out->w[i] = plustek8200i_vlanczos3(((first + i) + 0.5 - c) / vsquash);
            sum += out->w[i];
        }
    } else {
        double y0 = k * vsquash, y1 = (k + 1) * vsquash;

        for (i = 0; i < out->n; i++) {
            double lo = first + i, hi = lo + 1.0;

            if (lo < y0) lo = y0;
            if (hi > y1) hi = y1;
            out->w[i] = hi > lo ? hi - lo : 0.0;
            sum += out->w[i];
        }
    }
    if (sum <= 1e-12)
        return -1;
    for (i = 0; i < out->n; i++)
        out->w[i] /= sum;
    return 0;
}

/* Source line tap `tap` reads, with the edges replicated. */
static inline int
plustek8200i_vresample_src(const plustek8200i_vweights_t *w, int tap,
                           int src_lines)
{
    int i = w->first + tap;

    if (i < 0)
        i = 0;
    if (i >= src_lines)
        i = src_lines - 1;
    return i;
}

/* One accumulated sample to 16 bit.  Lanczos has negative lobes, so this
 * clamps below as well as above: ringing at a hard edge -- a film rebate,
 * a dust speck -- undershoots past zero. */
static inline uint16_t
plustek8200i_vresample_clamp(double v)
{
    if (v <= 0.0)
        return 0;
    if (v >= 65535.0)
        return 65535;
    return (uint16_t)(v + 0.5);
}

#endif
