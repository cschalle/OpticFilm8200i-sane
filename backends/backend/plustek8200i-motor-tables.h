/* sane - Scanner Access Now Easy.

   Copyright (C) 2025 Christian Schaller

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

#ifndef PLUSTEK8200I_MOTOR_TABLES_H
#define PLUSTEK8200I_MOTOR_TABLES_H

#include "plustek8200i.h"

/*
 * Motor table generation for Plustek OpticFilm 8200i
 *
 *
 * This module dynamically generates motor acceleration/deceleration curves
 * using the same gamma function algorithm as the macOS driver.
 *
 * Key concepts:
 * - Motor tables define speed profiles for smooth acceleration/deceleration
 * - Tables are generated using gamma curves (exponential ramp)
 * - Tables are written to scanner memory via DMA at 0x10000000-0x1000C000
 * - The scan uses 3 main tables: acceleration, constant, deceleration
 *
 * Reference: port/extracted/motor_table_generation.c
 */

/**
 * Load motor tables for scan operation
 *
 * Generates and writes acceleration/deceleration curves to scanner memory.
 * Uses gamma function to create smooth motor speed profiles.
 *
 * @param fd: USB device handle
 * @param steps: Number of motor steps for this scan (from FEEDL register)
 * @return SANE_STATUS_GOOD on success
 */
SANE_Status plustek8200i_load_motor_tables(int fd, int steps);

/**
 * Load scan motor tables (tables 0-2: SCAN, BACKTRACK, STOP)
 *
 * Writes the motor acceleration profile to tables 0-2 which are used
 * during actual scanning movement. Without these, the motor stalls
 * because it has no acceleration curve for the scan step type.
 *
 * @param fd: USB device handle
 * @return SANE_STATUS_GOOD on success
 */

#endif /* PLUSTEK8200I_MOTOR_TABLES_H */
