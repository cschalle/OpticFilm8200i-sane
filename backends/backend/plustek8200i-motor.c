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
 * Register assignments, established from USB captures of the vendor
 * driver and confirmed on the hardware:
 *
 * - motor power is register 0x02 bit 4
 * - the SCAN bit is register 0x01 bit 7
 * - MOVE is register 0x0F, written whole rather than as a bit
 * - feed length is registers 0xAE/0xAF, 16-bit
 * - the sequence is SCAN, wait 50 ms, then MOVE
 */

/* Mark as auxiliary source file for SANE backend */
#define DEBUG_DECLARE_ONLY

#include "plustek8200i.h"
#include "plustek8200i-motor.h"
#include <unistd.h>

/* Approximate motor power GPIO registers (from macOS init baseline) */
#define REG_GPIO_LATCH     0xA5  /* Seen as 0x20 in macOS register dump */
#define REG_GPIO_MOTORDRV  0xAB  /* Seen as 0x30 in macOS register dump */
#define REG_GPO_BUS        0x6C  /* GPIO high byte, baseline 0x55 in macOS */
#define GPIO_LATCH_MOTOR   0x20  /* Bit mask to keep motor rail latched */
#define GPIO_MOTORDRV_MASK 0x30  /* Bits used for motor driver enable */

/*
 * Helper: Read-modify-write a single bit in a register
 *
 * This preserves other bits in the register while changing only the specified bit.
 */
static SANE_Status
write_reg_bit(int device_handle, uint8_t reg, uint8_t bit_mask, bool set)
{
    uint8_t val;
    SANE_Status status;

    /* Read current register value */
    status = plustek8200i_read_reg(device_handle, reg, &val);
    if (status != SANE_STATUS_GOOD)
        return status;

    /* Modify the bit */
    if (set) {
        val |= bit_mask;  /* Set bit */
    } else {
        val &= ~bit_mask; /* Clear bit */
    }

    /* Write back */
    return plustek8200i_write_reg(device_handle, reg, val);
}

/*
 * Enable or disable motor power
 *
 * Observed in the vendor driver
 *
 * macOS code (simplified):
 *   register[0x02] = (register[0x02] & 0xFFEF) | ((enable & 1) << 4);
 *
 * Translation:
 *   - Register 0x02, bit 4 (0x10)
 *   - Read-modify-write to preserve other bits
 *   - macOS uses immediate write (not batched)
 */
SANE_Status
plustek8200i_motor_power(int device_handle, bool enable)
{
    DBG(DBG_info, "plustek8200i_motor_power: %s\n", enable ? "enable" : "disable");

    /* Register 0x02, bit 4 (0x10) */
    return write_reg_bit(device_handle, REG_CONTROL, CONTROL_MTRPWR_BIT, enable);
}

/*
 * Enable or disable SCAN bit
 *
 * Observed in the vendor driver
 *
 * Register 0x01, bit 0: SCAN — start scanning process.
 *
 * NOTE: the vendor driver appears to use bit 7. That has not been
 * reconciled with bit 0, which the main sane_start() code uses and which
 * works (it writes 0x01 = 0x03 = SHDAREA|SCAN).
 *
 * Translation:
 *   - Register 0x01, bit 0 (0x01)
 *   - Read-modify-write to preserve other bits
 */
SANE_Status
plustek8200i_scan_enable(int device_handle, bool enable)
{
    DBG(DBG_info, "plustek8200i_scan_enable: %s\n", enable ? "enable" : "disable");

    /* Register 0x01, bit 0 (SCAN) */
    return write_reg_bit(device_handle, REG_CISSET_SCAN, REG_CISSET_SCAN_BIT, enable);
}

/*
 * Enable or disable motor movement (MOVE bit)
 *
 * Observed in the vendor driver
 *
 * macOS code (simplified):
 *   register[0x0F] = enable;  // Note: writes ENTIRE register, not a bit!
 *
 * Translation:
 *   - Register 0x0F
 *   - Unlike other functions, this writes the ENTIRE register value
 *   - 0x00 = MOVE disabled
 *   - 0x01 = MOVE enabled
 *
 * IMPORTANT: This is different from MTRPWR and CISSET!
 * MOVE does NOT use bit manipulation - it writes the whole register.
 */
SANE_Status
plustek8200i_move_enable(int device_handle, bool enable)
{
    DBG(DBG_info, "plustek8200i_move_enable: %s\n", enable ? "enable" : "disable");

    /*
     * MOVE is special: the whole register is written, not a single
     * bit. Observed as a direct 16-bit store in the vendor driver.
     */
    return plustek8200i_write_reg(device_handle, REG_CISSET_MOVE, enable ? 0x01 : 0x00);
}

/*
 * Stop motor movement
 *
 * Reverse of motor start: disable MOVE, SCAN, and power
 */
SANE_Status
plustek8200i_motor_stop(int device_handle)
{
    SANE_Status status;

    DBG(DBG_info, "plustek8200i_motor_stop\n");

    /* 1. Disable MOVE bit */
    status = plustek8200i_move_enable(device_handle, false);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Motor stop: Failed to disable MOVE\n");
        return status;
    }

    /* 2. Disable SCAN bit */
    status = plustek8200i_scan_enable(device_handle, false);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Motor stop: Failed to disable SCAN\n");
        return status;
    }

    /* 3. Disable motor power */
    status = plustek8200i_motor_power(device_handle, false);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Motor stop: Failed to disable motor power\n");
        return status;
    }

    /* 4. Drop motor power rails and bus gating */
    (void)plustek8200i_motor_bus_power_sel(device_handle, false);
    (void)plustek8200i_motor_power_sel(device_handle, false);

    DBG(DBG_info, "Motor stop complete\n");
    return SANE_STATUS_GOOD;
}

/*
 * Motor power rail selection (approximation of macOS MotorPowerSel)
 *
 * macOS uses registry-provided GPIO addresses/masks. For the 8200i we observed
 * the macOS init snapshot writing:
 *   - REG_GPIO_LATCH (0xA5) = 0x20
 *   - REG_GPIO_MOTORDRV (0xAB) = 0x30
 *   - REG_GPO_BUS (0x6C) = 0x55
 *
 * This helper mirrors that behavior: enabling sets those bits, disabling clears.
 */
SANE_Status
plustek8200i_motor_power_sel(int device_handle, bool enable)
{
    SANE_Status status;
    uint8_t val;

    DBG(DBG_info, "plustek8200i_motor_power_sel: %s\n", enable ? "enable" : "disable");

    /* Latch rail */
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

    /* Motor driver enable mask */
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

/*
 * Motor bus power gating (approximation of macOS MotorBusPowerSel)
 *
 * macOS toggles a MotorBusPowerReg based on system power polarity.
 * We mirror the observed baseline: set bits in REG_GPO_BUS when enabling,
 * clear them when disabling.
 */
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
        /* Use macOS baseline 0x55 as enable pattern */
        val |= 0x55;
    } else {
        val &= (uint8_t)~0x55;
    }

    return plustek8200i_write_reg(device_handle, REG_GPO_BUS, val);
}
