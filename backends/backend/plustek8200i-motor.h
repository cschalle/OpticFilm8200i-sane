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

/*
 * Motor control for Plustek OpticFilm 8200i
 *
 * Based on the vendor driver's motor control sequence.
 */

#ifndef PLUSTEK8200I_MOTOR_H
#define PLUSTEK8200I_MOTOR_H

#include <sane/sane.h>
#include <stdint.h>
#include <stdbool.h>

/* Motor control registers (from analysis of the vendor driver) */
#define REG_CONTROL        0x02  /* Power control (PWRBIT) */
#define REG_CISSET_MOVE    0x0F  /* MOVE enable (entire register) */
#define REG_CISSET_SCAN    0x01  /* SCAN bit register address */
#define REG_CISSET_SCAN_BIT 0x01 /* SCAN is bit 0 (reg 0x01) */
#define REG_FEEDL_H        0x3D  /* Feed length bits [19:16] in bits [3:0] */
#define REG_FEEDL_M        0x3E  /* Feed length bits [15:8] */
#define REG_FEEDL_L        0x3F  /* Feed length bits [7:0] */
#define CONTROL_LAMPPWR_BIT 0x10  /* Bit 4: Lamp power */
#define CONTROL_MTRPWR_BIT 0x10  /* Bit 4: Motor power (matches macOS MTRPWR) */

/* Motor control functions */

/**
 * Enable or disable motor power
 */
SANE_Status plustek8200i_motor_power(int device_handle, bool enable);

/**
 * Enable or disable SCAN bit
 */
SANE_Status plustek8200i_scan_enable(int device_handle, bool enable);

/**
 * Enable or disable MOVE bit
 */
SANE_Status plustek8200i_move_enable(int device_handle, bool enable);



/**
 * Stop motor
 */
SANE_Status plustek8200i_motor_stop(int device_handle);

/**
 * Enable motor power rails (GPIO / bus gating)
 * Approximated from macOS MotorPowerSel/MotorBusPowerSel behavior for 8200i.
 */
SANE_Status plustek8200i_motor_power_sel(int device_handle, bool enable);

/**
 * Align motor bus power with system power (approximated from macOS)
 */
SANE_Status plustek8200i_motor_bus_power_sel(int device_handle, bool enable);


#endif /* PLUSTEK8200I_MOTOR_H */
