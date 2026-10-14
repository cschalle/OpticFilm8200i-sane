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
 * Register assignments come from USB captures of the vendor driver and
 * were confirmed on the hardware:
 *
 * - motor power is register 0x02 bit 4
 * - MOVE is register 0x0F, written whole rather than as a bit
 * - feed length is registers 0xAE/0xAF, 16-bit
 * - the sequence is SCAN, wait 50 ms, then MOVE
 */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"
#include "plustek8200i-motor.h"
#include <unistd.h>

/* Motor power GPIO registers, with the values seen in USB captures */
#define REG_GPIO_LATCH     0xA5  /* Seen as 0x20 */
#define REG_GPIO_MOTORDRV  0xAB  /* Seen as 0x30 */
#define REG_GPO_BUS        0x6C  /* GPIO high byte, seen as 0x55 */
#define GPIO_LATCH_MOTOR   0x20  /* Bit mask to keep motor rail latched */
#define GPIO_MOTORDRV_MASK 0x30  /* Bits used for motor driver enable */

/* Read-modify-write the bits in bit_mask, preserving the rest */
static SANE_Status
write_reg_bit(int device_handle, uint8_t reg, uint8_t bit_mask, bool set)
{
    uint8_t val;
    SANE_Status status;

    status = plustek8200i_read_reg(device_handle, reg, &val);
    if (status != SANE_STATUS_GOOD)
        return status;

    if (set)
        val |= bit_mask;
    else
        val &= ~bit_mask;

    return plustek8200i_write_reg(device_handle, reg, val);
}

/* Motor power: register 0x02 bit 4, read-modify-write */
SANE_Status
plustek8200i_motor_power(int device_handle, bool enable)
{
    DBG(DBG_info, "plustek8200i_motor_power: %s\n", enable ? "enable" : "disable");

    return write_reg_bit(device_handle, REG_CONTROL, CONTROL_MTRPWR_BIT, enable);
}

/*
 * SCAN bit: register 0x01 bit 0, read-modify-write.  sane_start() uses
 * the same bit (0x01 = 0x03 = SHDAREA|SCAN).
 */
SANE_Status
plustek8200i_scan_enable(int device_handle, bool enable)
{
    DBG(DBG_info, "plustek8200i_scan_enable: %s\n", enable ? "enable" : "disable");

    return write_reg_bit(device_handle, REG_CISSET_SCAN, REG_CISSET_SCAN_BIT, enable);
}

/* MOVE: register 0x0F, written whole (0x00 off, 0x01 on), not as a bit */
SANE_Status
plustek8200i_move_enable(int device_handle, bool enable)
{
    DBG(DBG_info, "plustek8200i_move_enable: %s\n", enable ? "enable" : "disable");

    return plustek8200i_write_reg(device_handle, REG_CISSET_MOVE, enable ? 0x01 : 0x00);
}

/* Reverse of motor start: MOVE off, SCAN off, then power */
SANE_Status
plustek8200i_motor_stop(int device_handle)
{
    SANE_Status status;

    DBG(DBG_info, "plustek8200i_motor_stop\n");

    status = plustek8200i_move_enable(device_handle, false);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Motor stop: Failed to disable MOVE\n");
        return status;
    }

    status = plustek8200i_scan_enable(device_handle, false);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Motor stop: Failed to disable SCAN\n");
        return status;
    }

    status = plustek8200i_motor_power(device_handle, false);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Motor stop: Failed to disable motor power\n");
        return status;
    }

    /* Drop the power rails and bus gating */
    (void)plustek8200i_motor_bus_power_sel(device_handle, false);
    (void)plustek8200i_motor_power_sel(device_handle, false);

    DBG(DBG_info, "Motor stop complete\n");
    return SANE_STATUS_GOOD;
}

/* Motor power rails: set (or clear) the latch and driver-enable bits */
SANE_Status
plustek8200i_motor_power_sel(int device_handle, bool enable)
{
    SANE_Status status;
    uint8_t val;

    DBG(DBG_info, "plustek8200i_motor_power_sel: %s\n", enable ? "enable" : "disable");

    status = plustek8200i_read_reg(device_handle, REG_GPIO_LATCH, &val);
    if (status != SANE_STATUS_GOOD)
        return status;
    if (enable)
        val |= GPIO_LATCH_MOTOR;
    else
        val &= (uint8_t)~GPIO_LATCH_MOTOR;
    status = plustek8200i_write_reg(device_handle, REG_GPIO_LATCH, val);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = plustek8200i_read_reg(device_handle, REG_GPIO_MOTORDRV, &val);
    if (status != SANE_STATUS_GOOD)
        return status;
    if (enable)
        val |= GPIO_MOTORDRV_MASK;
    else
        val &= (uint8_t)~GPIO_MOTORDRV_MASK;
    status = plustek8200i_write_reg(device_handle, REG_GPIO_MOTORDRV, val);
    if (status != SANE_STATUS_GOOD)
        return status;

    return SANE_STATUS_GOOD;
}

/* Motor bus gating: set (or clear) the 0x55 pattern in REG_GPO_BUS */
SANE_Status
plustek8200i_motor_bus_power_sel(int device_handle, bool enable)
{
    SANE_Status status;
    uint8_t val;

    DBG(DBG_info, "plustek8200i_motor_bus_power_sel: %s\n", enable ? "enable" : "disable");

    status = plustek8200i_read_reg(device_handle, REG_GPO_BUS, &val);
    if (status != SANE_STATUS_GOOD)
        return status;

    if (enable) {
        val |= 0x55;
    } else {
        val &= (uint8_t)~0x55;
    }

    return plustek8200i_write_reg(device_handle, REG_GPO_BUS, val);
}
