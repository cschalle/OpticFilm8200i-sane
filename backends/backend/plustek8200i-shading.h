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

/* Plustek OpticFilm 8200i (GL128) - hardware shading correction */

#ifndef PLUSTEK8200I_SHADING_H
#define PLUSTEK8200I_SHADING_H

#include <stdint.h>
#include <sane/sane.h>

typedef struct {
    double white[3];         /* open light (white - dark) at AFE code 0 */
    int    code[3];          /* AFE gain codes chosen (0 = not auto) */
    double dark[3];          /* mean dark level per channel, final gains */
    double gain_max[3];      /* largest shading gain, i.e. the falloff */
} plustek8200i_shading_stats_t;

/*
 * Per-scan calibration, all stationary at home: white and dark strips,
 * the AFE gains (if auto_gain) set so the open light lands at 90% of full
 * scale, a dark strip at the final gains, and the shading table. On
 * SANE_STATUS_GOOD the image pass may run with DVDSET (0x01 = 0x23).
 * Call with every image register already set; LINCNT, FEEDL, exposure,
 * motor mode (0x30), lamp and the scan table are put back to the image
 * values before returning, and 0x01 is left at 0x02.
 */
SANE_Status plustek8200i_shading_calibrate(int fd, int pixels, int img_lincnt,
                                           int img_feedl, int img_table_period,
                                           int img_exposure, int auto_gain,
                                           plustek8200i_shading_stats_t *stats);

/*
 * Infrared pass: measure the open IR light (stationary strips, IR LED on
 * and off) and return in *scale the factor that normalises the IR image
 * to a fixed level. Restores the image registers like
 * plustek8200i_shading_calibrate. *scale is 1.0 if the measurement fails.
 */
SANE_Status plustek8200i_ir_reference(int fd, int pixels, int img_lincnt,
                                      int img_feedl, int img_table_period,
                                      int img_exposure, double *scale);

#endif
