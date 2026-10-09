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

/* sane - Scanner Access Now Easy.
 *
 * macOS Driver Register Initialization
 *
 * This file contains the EXACT register sequence extracted from
 * the macOS USB capture INIT phase (timestamp 0:38.486), NOT the
 * scan-time values (0:39.123) that were used previously.
 *
 * Changes from previous version:
 * - Fixed 7 register values to match init-time (not scan-time)
 * - Removed registers that macOS writes separately (0x00, 0x0E, 0x0F,
 *   0x51, 0x5D, 0x6C, 0xD8, 0xDC, 0xAE, 0xAF)
 * - Added post-WriteAllReg individual register writes
 * - Added WriteAllAfe (AFE initialization)
 *
 * DO NOT MODIFY these values without comparing against macOS USB capture.
 */

#define DEBUG_DECLARE_ONLY
#include "plustek8200i.h"
#include "plustek8200i-scan.h"  /* For plustek8200i_write_regs() */
#include "plustek8200i-dma.h"   /* For plustek8200i_dma_write() */
#include <unistd.h>

/**
 * WriteAllReg - bulk register initialization
 *
 * Writes ~116 register pairs matching the macOS init-phase WriteAllReg.
 *
 * CORRECTED VALUES (init phase, timestamp 0:38.486):
 *   0x01: 0x22 (was 0x10 — scan control mode bits)
 *   0x02: 0x78 (was 0x18 — power, macOS uses 0x78 during init)
 *   0x03: 0x20 (was 0x30 — lamp source, init value)
 *   0x08: 0x00 (was 0x55 — should be 0x00 during init)
 *   0x3E: 0x00 (was 0x11 — feed length mid, set later)
 *   0x3F: 0x01 (was 0x62 — feed length high, set later)
 *   0x5E: 0x1F (was 0x00 — init value is 0x1F)
 *
 * REMOVED from bulk write (macOS writes separately or not at all):
 *   0x00 (not in macOS WriteAllReg)
 *   0x0E (macOS writes separately before WriteAllReg)
 *   0x0F (MOVE command — must be written LAST, not in bulk)
 *   0x51, 0x5D, 0x6C, 0xD8, 0xDC (not in macOS WriteAllReg)
 *   0xAE, 0xAF (macOS writes these separately before MOVE)
 */
SANE_Status plustek8200i_init_macos_registers(int fd)
{
    SANE_Status status;
    uint8_t reg_pairs[232];  /* 116 register pairs × 2 bytes = 232 bytes */
    int idx = 0;

    DBG(DBG_info, "WriteAllReg: preparing init-phase register pairs...\n");

    /* Build register/value pairs - CORRECTED init-phase values */
    reg_pairs[idx++] = 0x01; reg_pairs[idx++] = 0x22;  /* FIXED: was 0x10 */
    reg_pairs[idx++] = 0x02; reg_pairs[idx++] = 0x78;  /* FIXED: was 0x18 */
    reg_pairs[idx++] = 0x03; reg_pairs[idx++] = 0x20;  /* FIXED: was 0x30 */
    reg_pairs[idx++] = 0x04; reg_pairs[idx++] = 0x02;
    reg_pairs[idx++] = 0x05; reg_pairs[idx++] = 0x48;
    reg_pairs[idx++] = 0x06; reg_pairs[idx++] = 0x18;
    reg_pairs[idx++] = 0x07; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x08; reg_pairs[idx++] = 0x00;  /* FIXED: was 0x55 */
    reg_pairs[idx++] = 0x09; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x0A; reg_pairs[idx++] = 0x40;
    reg_pairs[idx++] = 0x0B; reg_pairs[idx++] = 0x6C;
    reg_pairs[idx++] = 0x0C; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x0D; reg_pairs[idx++] = 0x00;
    /* 0x0E REMOVED — macOS writes separately before WriteAllReg */
    /* 0x0F REMOVED — MOVE command, written last */
    reg_pairs[idx++] = 0x11; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x12; reg_pairs[idx++] = 0x04;
    reg_pairs[idx++] = 0x13; reg_pairs[idx++] = 0x08;
    reg_pairs[idx++] = 0x14; reg_pairs[idx++] = 0x01;
    reg_pairs[idx++] = 0x15; reg_pairs[idx++] = 0x80;
    reg_pairs[idx++] = 0x16; reg_pairs[idx++] = 0x27;
    reg_pairs[idx++] = 0x17; reg_pairs[idx++] = 0x0C;
    reg_pairs[idx++] = 0x18; reg_pairs[idx++] = 0x10;
    reg_pairs[idx++] = 0x19; reg_pairs[idx++] = 0x02;
    reg_pairs[idx++] = 0x1A; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x1B; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x1C; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x1D; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x1E; reg_pairs[idx++] = 0x10;
    reg_pairs[idx++] = 0x1F; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x20; reg_pairs[idx++] = 0x0C;
    reg_pairs[idx++] = 0x21; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x22; reg_pairs[idx++] = 0x1A;
    reg_pairs[idx++] = 0x23; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x24; reg_pairs[idx++] = 0x1A;
    reg_pairs[idx++] = 0x25; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x26; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x27; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x2B; reg_pairs[idx++] = 0x20;
    reg_pairs[idx++] = 0x2C; reg_pairs[idx++] = 0x12;
    reg_pairs[idx++] = 0x2D; reg_pairs[idx++] = 0xC0;
    reg_pairs[idx++] = 0x30; reg_pairs[idx++] = 0x6F;
    reg_pairs[idx++] = 0x31; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x32; reg_pairs[idx++] = 0x22;
    reg_pairs[idx++] = 0x33; reg_pairs[idx++] = 0x04;
    reg_pairs[idx++] = 0x34; reg_pairs[idx++] = 0x80;
    reg_pairs[idx++] = 0x35; reg_pairs[idx++] = 0x2F;
    reg_pairs[idx++] = 0x36; reg_pairs[idx++] = 0x04;
    reg_pairs[idx++] = 0x37; reg_pairs[idx++] = 0xC0;
    reg_pairs[idx++] = 0x38; reg_pairs[idx++] = 0x44;
    reg_pairs[idx++] = 0x39; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x3A; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x3B; reg_pairs[idx++] = 0xFF;
    reg_pairs[idx++] = 0x3C; reg_pairs[idx++] = 0xFF;
    reg_pairs[idx++] = 0x3D; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x3E; reg_pairs[idx++] = 0x00;  /* FIXED: was 0x11 */
    reg_pairs[idx++] = 0x3F; reg_pairs[idx++] = 0x01;  /* FIXED: was 0x62 */
    reg_pairs[idx++] = 0x4F; reg_pairs[idx++] = 0x03;
    reg_pairs[idx++] = 0x52; reg_pairs[idx++] = 0x07;
    reg_pairs[idx++] = 0x53; reg_pairs[idx++] = 0x09;
    reg_pairs[idx++] = 0x54; reg_pairs[idx++] = 0x0B;
    reg_pairs[idx++] = 0x55; reg_pairs[idx++] = 0x01;
    reg_pairs[idx++] = 0x56; reg_pairs[idx++] = 0x03;
    reg_pairs[idx++] = 0x57; reg_pairs[idx++] = 0x05;
    reg_pairs[idx++] = 0x5A; reg_pairs[idx++] = 0x12;
    reg_pairs[idx++] = 0x5B; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x5C; reg_pairs[idx++] = 0x40;
    /* 0x51, 0x5D REMOVED — not in macOS WriteAllReg */
    reg_pairs[idx++] = 0x5E; reg_pairs[idx++] = 0x1F;  /* FIXED: was 0x00 */
    reg_pairs[idx++] = 0x5F; reg_pairs[idx++] = 0x05;
    reg_pairs[idx++] = 0x60; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x61; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x63; reg_pairs[idx++] = 0x20;
    reg_pairs[idx++] = 0x67; reg_pairs[idx++] = 0x7F;
    reg_pairs[idx++] = 0x68; reg_pairs[idx++] = 0x7F;
    reg_pairs[idx++] = 0x69; reg_pairs[idx++] = 0x01;
    /* 0x6C REMOVED — not in macOS WriteAllReg */
    reg_pairs[idx++] = 0x70; reg_pairs[idx++] = 0x01;
    reg_pairs[idx++] = 0x71; reg_pairs[idx++] = 0x02;
    reg_pairs[idx++] = 0x72; reg_pairs[idx++] = 0x03;
    reg_pairs[idx++] = 0x73; reg_pairs[idx++] = 0x04;
    reg_pairs[idx++] = 0x74; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x75; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x76; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x77; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x78; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x79; reg_pairs[idx++] = 0x0F;
    reg_pairs[idx++] = 0x7A; reg_pairs[idx++] = 0xFF;
    reg_pairs[idx++] = 0x7B; reg_pairs[idx++] = 0xFF;
    reg_pairs[idx++] = 0x7C; reg_pairs[idx++] = 0xFF;
    reg_pairs[idx++] = 0x7D; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x7E; reg_pairs[idx++] = 0x2A;
    reg_pairs[idx++] = 0x7F; reg_pairs[idx++] = 0xF8;
    reg_pairs[idx++] = 0x80; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x81; reg_pairs[idx++] = 0x22;
    reg_pairs[idx++] = 0x82; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x83; reg_pairs[idx++] = 0x01;
    reg_pairs[idx++] = 0x84; reg_pairs[idx++] = 0x18;
    reg_pairs[idx++] = 0x85; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x86; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x87; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x93; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x94; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x95; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x9D; reg_pairs[idx++] = 0x08;
    reg_pairs[idx++] = 0xA0; reg_pairs[idx++] = 0x12;
    reg_pairs[idx++] = 0xA4; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xA5; reg_pairs[idx++] = 0x20;
    reg_pairs[idx++] = 0xA6; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xA7; reg_pairs[idx++] = 0x07;  /* GPIO control — sane-genesys GPO value */
    reg_pairs[idx++] = 0xA8; reg_pairs[idx++] = 0x20;  /* GPIO control — sane-genesys GPO value (bit 5) */
    reg_pairs[idx++] = 0xA9; reg_pairs[idx++] = 0x01;  /* GPIO control — sane-genesys GPO value */
    reg_pairs[idx++] = 0xAA; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xAB; reg_pairs[idx++] = 0x30;
    /* 0xAE, 0xAF REMOVED — macOS writes these separately before MOVE */
    reg_pairs[idx++] = 0xB8; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xB9; reg_pairs[idx++] = 0x38;
    reg_pairs[idx++] = 0xBA; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xBD; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xBE; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xBF; reg_pairs[idx++] = 0x00;
    /* 0xD8, 0xDC REMOVED — not in macOS WriteAllReg */
    /* 0x0F REMOVED — MOVE command written as final step */

    DBG(DBG_info, "WriteAllReg: writing %d bytes (%d register pairs)...\n",
        idx, idx / 2);

    status = plustek8200i_write_regs(fd, reg_pairs, idx);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "WriteAllReg FAILED!\n");
        return status;
    }

    DBG(DBG_info, "WriteAllReg complete (%d register pairs)\n", idx / 2);
    return SANE_STATUS_GOOD;
}

/**
 * Post-WriteAllReg individual register writes
 *
 * After each WriteAllReg, macOS writes these registers individually.
 * These appear to be CCD timing adjustments that must be done after
 * the bulk register write.
 *
 * From macOS USB capture (init phase):
 *   1. Reg 0x10 = 0x0E
 *   2. Reg 0x13 = 0x0E
 *   3. Reg 0x0B = 0x84
 *   4. Reg 0x13 = 0x0F
 *   5. Reg 0x0B = 0x8C
 *   6. ~100ms delay
 *   7. Reg 0x0A = 0x40
 */
SANE_Status plustek8200i_post_writeallreg(int fd)
{
    SANE_Status status;

    DBG(DBG_info, "Post-WriteAllReg: writing CCD timing adjustments...\n");

    status = plustek8200i_write_reg(fd, 0x10, 0x0E);
    if (status != SANE_STATUS_GOOD) return status;

    status = plustek8200i_write_reg(fd, 0x13, 0x0E);
    if (status != SANE_STATUS_GOOD) return status;

    status = plustek8200i_write_reg(fd, 0x0B, 0x84);
    if (status != SANE_STATUS_GOOD) return status;

    status = plustek8200i_write_reg(fd, 0x13, 0x0F);
    if (status != SANE_STATUS_GOOD) return status;

    status = plustek8200i_write_reg(fd, 0x0B, 0x8C);
    if (status != SANE_STATUS_GOOD) return status;

    /* ~100ms delay */
    usleep(100000);

    status = plustek8200i_write_reg(fd, 0x0A, 0x40);
    if (status != SANE_STATUS_GOOD) return status;

    DBG(DBG_info, "Post-WriteAllReg complete\n");
    return SANE_STATUS_GOOD;
}

/**
 * WriteAllAfe - AFE (Analog Front End) initialization
 *
 * macOS writes 8 AFE register blocks of 6 bytes each after the
 * post-WriteAllReg adjustments. Each block is sent as a separate
 * 6-byte WriteRegs (wValue=0x0083).
 *
 * AFE registers are accessed via an indirect mechanism:
 *   0x51 = AFE register index
 *   0x5D = AFE data high byte
 *   0x5E = AFE data low byte
 *
 * From macOS USB capture:
 *   Block 0: idx=0x00, data=0x00F8 (offset/gain?)
 *   Block 1: idx=0x01, data=0x0080
 *   Block 2: idx=0x02, data=0x0000
 *   Block 3: idx=0x03, data=0x0000
 *   Block 4: idx=0x04, data=0x0000
 *   Block 5: idx=0x05, data=0x0000
 *   Block 6: idx=0x06, data=0x0000
 *   Block 7: idx=0x07, data=0x0000
 */
SANE_Status plustek8200i_write_afe_init(int fd)
{
    SANE_Status status;
    int i;

    /* AFE data: 8 blocks, each [index, data_high, data_low] */
    static const uint8_t afe_data[8][3] = {
        { 0x00, 0x00, 0xF8 },
        { 0x01, 0x00, 0x80 },
        { 0x02, 0x00, 0x00 },
        { 0x03, 0x00, 0x00 },
        { 0x04, 0x00, 0x00 },
        { 0x05, 0x00, 0x00 },
        { 0x06, 0x00, 0x00 },
        { 0x07, 0x00, 0x00 },
    };

    DBG(DBG_info, "WriteAllAfe: writing 8 AFE register blocks...\n");

    for (i = 0; i < 8; i++) {
        /* Each block: 6 bytes = 3 register pairs (0x51, 0x5D, 0x5E) */
        uint8_t block[6];
        block[0] = 0x51; block[1] = afe_data[i][0];  /* AFE index */
        block[2] = 0x5D; block[3] = afe_data[i][1];  /* Data high */
        block[4] = 0x5E; block[5] = afe_data[i][2];  /* Data low */

        DBG(DBG_debug, "AFE block %d: idx=0x%02x data=0x%02x%02x\n",
            i, afe_data[i][0], afe_data[i][1], afe_data[i][2]);

        status = plustek8200i_write_regs(fd, block, 6);
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "WriteAllAfe: block %d failed\n", i);
            return status;
        }
    }

    DBG(DBG_info, "WriteAllAfe complete (8 blocks)\n");
    return SANE_STATUS_GOOD;
}

/**
 * Third-pass WriteAllReg — post-MOVE#2 re-initialization
 *
 * After MOVE #2 completes, macOS sends a THIRD WriteAllReg with different
 * values from the init-phase passes. This must be sent as a proper bulk
 * write (not init + individual overwrites) because the GL128 may process
 * WriteAllReg differently from individual register writes.
 *
 * 125 register pairs extracted from macOS USB capture lines 371853-373004
 * (timestamp 0:39.870-0:39.876).
 *
 * Key differences from init-phase WriteAllReg (20 registers):
 *   0x06: 0x10 (init=0x18) — STEPNO
 *   0x0B: 0x4C (init=0x6C) — CCD control
 *   0x1C: 0x20 (init=0x00) — timing
 *   0x1D: 0x80 (init=0x00) — timing
 *   0x1E: 0x20 (init=0x10) — timing
 *   0x3B: 0x01 (init=0xFF) — backlash
 *   0x52-0x57: CCD clock offsets (all differ)
 *   0x5A: 0x22 (init=0x12) — CCD timing
 *   0x5B: 0x4A (init=0x00) — CCD timing
 *   0x70-0x73: CCD multiplexing (all differ)
 *   0x7E-0x7F: Line period (0x36B0 vs init 0x2AF8)
 */
SANE_Status plustek8200i_init_pass3_registers(int fd)
{
    SANE_Status status;

    /* Block 1: 64 bytes (32 register pairs) */
    static const uint8_t pass3_block1[64] = {
        0x02, 0x78, 0x3D, 0x00, 0x3E, 0x00, 0x3F, 0x01,
        0x7D, 0x00, 0x7E, 0x36, 0x7F, 0xB0, 0x1C, 0x20,
        0x8A, 0x00, 0x8B, 0x00, 0x8C, 0x00, 0x8D, 0x00,
        0x8E, 0x00, 0x8F, 0x00, 0x90, 0x00, 0x91, 0x00,
        0x92, 0x00, 0x01, 0x22, 0x03, 0x20, 0x04, 0x02,
        0x05, 0x48, 0x06, 0x10, 0x07, 0x00, 0x08, 0x00,
        0x09, 0x00, 0x0A, 0x40, 0x0B, 0x4C, 0x0C, 0x00,
        0x0D, 0x00, 0x11, 0x00, 0x12, 0x04, 0x13, 0x08
    };

    /* Block 2: 64 bytes (32 register pairs) */
    static const uint8_t pass3_block2[64] = {
        0x14, 0x01, 0x15, 0x80, 0x16, 0x27, 0x17, 0x0C,
        0x18, 0x10, 0x19, 0x02, 0x1A, 0x00, 0x1B, 0x00,
        0x1D, 0x80, 0x1E, 0x20, 0x1F, 0x00, 0x20, 0x0C,
        0x21, 0x00, 0x22, 0x1A, 0x23, 0x00, 0x24, 0x1A,
        0x25, 0x00, 0x26, 0x00, 0x27, 0x00, 0x2B, 0x20,
        0x2C, 0x12, 0x2D, 0xC0, 0x30, 0x6F, 0x31, 0x00,
        0x32, 0x22, 0x33, 0x04, 0x34, 0x80, 0x35, 0x2F,
        0x36, 0x04, 0x37, 0xC0, 0x38, 0x44, 0x39, 0x00
    };

    /* Block 3: 64 bytes (32 register pairs) */
    static const uint8_t pass3_block3[64] = {
        0x3A, 0x00, 0x3B, 0x01, 0x3C, 0xFF, 0x4F, 0x03,
        0x52, 0x08, 0x53, 0x0A, 0x54, 0x00, 0x55, 0x02,
        0x56, 0x04, 0x57, 0x06, 0x5A, 0x22, 0x5B, 0x4A,
        0x5C, 0x40, 0x5E, 0x1F, 0x5F, 0x05, 0x60, 0x00,
        0x61, 0x00, 0x63, 0x20, 0x67, 0x7F, 0x68, 0x7F,
        0x69, 0x01, 0x70, 0x06, 0x71, 0x07, 0x72, 0x07,
        0x73, 0x08, 0x74, 0x00, 0x75, 0x00, 0x76, 0x00,
        0x77, 0x00, 0x78, 0x00, 0x79, 0x0F, 0x7A, 0xFF
    };

    /* Block 4: 58 bytes (29 register pairs) */
    static const uint8_t pass3_block4[58] = {
        0x7B, 0xFF, 0x7C, 0xFF, 0x80, 0x00, 0x81, 0x22,
        0x82, 0x00, 0x83, 0x01, 0x84, 0x18, 0x85, 0x00,
        0x86, 0x00, 0x87, 0x00, 0x93, 0x00, 0x94, 0x00,
        0x95, 0x00, 0x9D, 0x08, 0xA0, 0x12, 0xA4, 0x00,
        0xA5, 0x20, 0xA6, 0x00, 0xA7, 0x07, 0xA8, 0x20,
        0xA9, 0x01, 0xAA, 0x00, 0xAB, 0x30, 0xB8, 0x00,
        0xB9, 0x38, 0xBA, 0x00, 0xBD, 0x00, 0xBE, 0x00,
        0xBF, 0x00
    };

    DBG(DBG_info, "WriteAllReg pass 3: sending 125 register pairs (250 bytes)...\n");

    /* Send each block as a separate 64-byte chunk (matching macOS behavior) */
    status = plustek8200i_write_regs(fd, pass3_block1, sizeof(pass3_block1));
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Pass 3 block 1 failed\n");
        return status;
    }

    status = plustek8200i_write_regs(fd, pass3_block2, sizeof(pass3_block2));
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Pass 3 block 2 failed\n");
        return status;
    }

    status = plustek8200i_write_regs(fd, pass3_block3, sizeof(pass3_block3));
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Pass 3 block 3 failed\n");
        return status;
    }

    status = plustek8200i_write_regs(fd, pass3_block4, sizeof(pass3_block4));
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Pass 3 block 4 failed\n");
        return status;
    }

    DBG(DBG_info, "WriteAllReg pass 3 complete (125 register pairs)\n");
    return SANE_STATUS_GOOD;
}

/*============================================================================
 * Scan-Phase Configuration
 *
 * These functions configure the scanner for the actual scanning MOVE.
 * The macOS driver uses a multi-MOVE sequence:
 *   MOVE #1: Homing (init motor table)
 *   MOVE #2-3: Calibration/positioning (scan motor table)
 *   MOVE #4: Actual scan with CCD data capture (SCAN bit enabled)
 *
 * MOVE #4 requires different CCD timing, buffer layout, and AFE settings
 * compared to the init phase. These values are extracted from the macOS
 * USB capture at timestamps 0:39.939-0:39.971.
 *===========================================================================*/

/**
 * Scan-phase motor table from macOS USB capture
 *
 * Extracted from MOVE #3 bulk OUT (timestamp ~0:39.1).
 * This is a DIFFERENT table from the init motor table (macos_motor_table
 * in plustek8200i-motor-tables.c). It has a steeper acceleration curve
 * starting at 0x1585 (5509) vs the init table's 0x2048 (8264).
 *
 * Written to 0x1000C000 (table 4) and 0x10010000 (table 5).
 */
const uint8_t scan_motor_table[512] = {
    0x85, 0x15, 0x74, 0x06, 0xF1, 0x04, 0x3A, 0x04,
    0xC9, 0x03, 0x79, 0x03, 0x3D, 0x03, 0x0D, 0x03,
    0xE6, 0x02, 0xC5, 0x02, 0xA9, 0x02, 0x90, 0x02,
    0x7B, 0x02, 0x68, 0x02, 0x56, 0x02, 0x47, 0x02,
    0x38, 0x02, 0x2B, 0x02, 0x1F, 0x02, 0x14, 0x02,
    0x09, 0x02, 0x00, 0x02, 0xF7, 0x01, 0xEE, 0x01,
    0xE6, 0x01, 0xDF, 0x01, 0xD7, 0x01, 0xD1, 0x01,
    0xCA, 0x01, 0xC4, 0x01, 0xBE, 0x01, 0xB9, 0x01,
    0xB3, 0x01, 0xAE, 0x01, 0xA9, 0x01, 0xA4, 0x01,
    0xA0, 0x01, 0x9B, 0x01, 0x97, 0x01, 0x93, 0x01,
    0x8F, 0x01, 0x8C, 0x01, 0x88, 0x01, 0x84, 0x01,
    0x81, 0x01, 0x7E, 0x01, 0x7A, 0x01, 0x77, 0x01,
    0x74, 0x01, 0x71, 0x01, 0x6E, 0x01, 0x6C, 0x01,
    0x69, 0x01, 0x66, 0x01, 0x64, 0x01, 0x61, 0x01,
    0x5F, 0x01, 0x5C, 0x01, 0x5A, 0x01, 0x58, 0x01,
    0x56, 0x01, 0x53, 0x01, 0x51, 0x01, 0x4F, 0x01,
    0x4D, 0x01, 0x4B, 0x01, 0x49, 0x01, 0x47, 0x01,
    0x45, 0x01, 0x44, 0x01, 0x42, 0x01, 0x40, 0x01,
    0x3E, 0x01, 0x3D, 0x01, 0x3B, 0x01, 0x39, 0x01,
    0x38, 0x01, 0x36, 0x01, 0x35, 0x01, 0x33, 0x01,
    0x32, 0x01, 0x30, 0x01, 0x2F, 0x01, 0x2D, 0x01,
    0x2C, 0x01, 0x2B, 0x01, 0x29, 0x01, 0x28, 0x01,
    0x27, 0x01, 0x25, 0x01, 0x24, 0x01, 0x23, 0x01,
    0x22, 0x01, 0x21, 0x01, 0x1F, 0x01, 0x1E, 0x01,
    0x1D, 0x01, 0x1C, 0x01, 0x1B, 0x01, 0x1A, 0x01,
    0x19, 0x01, 0x17, 0x01, 0x16, 0x01, 0x15, 0x01,
    0x14, 0x01, 0x13, 0x01, 0x12, 0x01, 0x11, 0x01,
    0x10, 0x01, 0x0F, 0x01, 0x0E, 0x01, 0x0E, 0x01,
    0x0D, 0x01, 0x0C, 0x01, 0x0B, 0x01, 0x0A, 0x01,
    0x09, 0x01, 0x08, 0x01, 0x07, 0x01, 0x06, 0x01,
    0x06, 0x01, 0x05, 0x01, 0x04, 0x01, 0x03, 0x01,
    0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01,
    0xFF, 0x00, 0xFE, 0x00, 0xFE, 0x00, 0xFD, 0x00,
    0xFC, 0x00, 0xFB, 0x00, 0xFB, 0x00, 0xFA, 0x00,
    0xF9, 0x00, 0xF9, 0x00, 0xF8, 0x00, 0xF7, 0x00,
    0xF6, 0x00, 0xF6, 0x00, 0xF5, 0x00, 0xF4, 0x00,
    0xF4, 0x00, 0xF3, 0x00, 0xF2, 0x00, 0xF2, 0x00,
    0xF1, 0x00, 0xF1, 0x00, 0xF0, 0x00, 0xEF, 0x00,
    0xEF, 0x00, 0xEE, 0x00, 0xEE, 0x00, 0xED, 0x00,
    0xEC, 0x00, 0xEC, 0x00, 0xEB, 0x00, 0xEB, 0x00,
    0xEA, 0x00, 0xEA, 0x00, 0xE9, 0x00, 0xE8, 0x00,
    0xE8, 0x00, 0xE7, 0x00, 0xE7, 0x00, 0xE6, 0x00,
    0xE6, 0x00, 0xE5, 0x00, 0xE5, 0x00, 0xE4, 0x00,
    0xE4, 0x00, 0xE3, 0x00, 0xE3, 0x00, 0xE2, 0x00,
    0xE2, 0x00, 0xE1, 0x00, 0xE1, 0x00, 0xE0, 0x00,
    0xE0, 0x00, 0xDF, 0x00, 0xDF, 0x00, 0xDE, 0x00,
    0xDE, 0x00, 0xDD, 0x00, 0xDD, 0x00, 0xDC, 0x00,
    0xDC, 0x00, 0xDC, 0x00, 0xDB, 0x00, 0xDB, 0x00,
    0xDA, 0x00, 0xDA, 0x00, 0xD9, 0x00, 0xD9, 0x00,
    0xD8, 0x00, 0xD8, 0x00, 0xD8, 0x00, 0xD7, 0x00,
    0xD7, 0x00, 0xD6, 0x00, 0xD6, 0x00, 0xD6, 0x00,
    0xD5, 0x00, 0xD5, 0x00, 0xD4, 0x00, 0xD4, 0x00,
    0xD4, 0x00, 0xD3, 0x00, 0xD3, 0x00, 0xD2, 0x00,
    0xD2, 0x00, 0xD2, 0x00, 0xD1, 0x00, 0xD1, 0x00,
    0xD1, 0x00, 0xD0, 0x00, 0xD0, 0x00, 0xCF, 0x00,
    0xCF, 0x00, 0xCF, 0x00, 0xCE, 0x00, 0xCE, 0x00,
    0xCE, 0x00, 0xCD, 0x00, 0xCD, 0x00, 0xCD, 0x00,
    0xCC, 0x00, 0xCC, 0x00, 0xCC, 0x00, 0xCB, 0x00,
    0xCB, 0x00, 0xCB, 0x00, 0xCA, 0x00, 0xCA, 0x00,
    0xCA, 0x00, 0xC9, 0x00, 0xC9, 0x00, 0xC9, 0x00,
    0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC7, 0x00,
    0xC7, 0x00, 0xC7, 0x00, 0xC6, 0x00, 0xC6, 0x00,
    0xC6, 0x00, 0xC5, 0x00, 0xC5, 0x00, 0xC5, 0x00,
    0xC4, 0x00, 0xC4, 0x00, 0xC4, 0x00, 0xC4, 0x00
};

/**
 * Load scan-phase motor tables (tables 1, 2, 3)
 *
 * Register names (see the note at the register definitions in plustek8200i.h):
 *   Motor table SDRAM mapping
 *     Table 1: AHB 0x1000_0000 (scan acceleration/cruise)
 *     Table 2: AHB 0x1000_4000 (backtrack)
 *     Table 3: AHB 0x1000_8000 (scan deceleration)
 *     Each table: 8K words (16K bytes max, we use 512 bytes)
 *   Reg 0x02 bit 3 FASTFED:
 *     When FASTFED=0, motor uses tables 1-3 (scan mode)
 *     When FASTFED=1, motor uses tables 4-5 (positioning mode)
 *
 * All three tables are filled with the constant value 0x1B58 (7000),
 * meaning constant motor velocity during scanning. The acceleration
 * to scan speed is handled by tables 4/5 during the positioning MOVEs.
 *
 * Extracted from macOS USB capture timestamps 0:41.889-0:41.892.
 * Must be called after scan-phase register configuration and before
 * the scan MOVE command.
 */
SANE_Status plustek8200i_load_scan_motor_table(int fd, int period)
{
    SANE_Status status;
    uint8_t scan_table[512];
    int i;

    /*
     * Compute scan motor speed using sane-genesys formula:
     *
     *   target_speed_w = (LPERIOD × ydpi) / base_ydpi
     *   table_value = target_speed_w >> step_shift
     *
     * OpticFilm 8200i parameters:
     *   LPERIOD = 17408 (0x4400, from macOS init reg 0x38-0x39)
     *   ydpi = 1200 (current scan resolution)
     *   base_ydpi = 3600 (motor native DPI, from sane-genesys motor table)
     *   step_type = QUARTER (shift = 2, from sane-genesys motor profile)
     *
     *   target_speed_w = (17408 × 1200) / 3600 = 5803
     *   table_value = 5803 >> 2 = 1450
     *
     * At this speed, motor advances 12 micro-steps per CCD line:
     *   LPERIOD / table_value = 17408 / 1450 = 12
     *   micro-steps/inch = 3600 × 4 = 14400 (quarter step)
     *   effective_ydpi = 14400 / 12 = 1200 ✓
     */
    uint16_t lperiod = 17408;   /* 0x4400 */
    int ydpi = 1200;
    int base_ydpi = 3600;
    int step_shift = 2;         /* quarter step (shift = 2) */

    unsigned target_speed_w = (lperiod * ydpi) / base_ydpi;
    uint16_t scan_speed = (uint16_t)(target_speed_w >> step_shift);

    /*
     * The table entries are PERIODS, not speeds: larger means a longer
     * interval between motor steps, so a slower carriage and MORE lines
     * per inch. (Confirmed against macos_motor_table, which ramps
     * 8264 -> 294, i.e. slow start accelerating to cruise.)
     *
     * So vertical resolution scales linearly with this value, and
     * raising it SLOWS the motor. That matters for safety: the failure
     * that ground the motor came from asking for a step rate it could
     * not achieve, and this direction cannot do that.
     *
     * The formula above predicts 1200 dpi at scan_speed 1450, but the
     * delivered vertical sampling measures ~1490 dpi
     * (PLUSTEK8200I_HW_LINE_DPI). Anchor on the measurement rather than
     * the prediction, since 1450 is the value actually in use.
     *
     */
    if (period >= 500 && period <= 20000)
        scan_speed = (uint16_t)period;

    DBG(DBG_info, "Scan motor period=%u (0x%04X) -> vertical ~%u dpi\n",
        scan_speed, scan_speed,
        (unsigned)((unsigned long)scan_speed * PLUSTEK8200I_HW_LINE_DPI / 1450));

    /* Fill table with constant scan speed (little-endian) */
    for (i = 0; i < 512; i += 2) {
        scan_table[i]     = scan_speed & 0xFF;         /* low byte */
        scan_table[i + 1] = (scan_speed >> 8) & 0xFF;  /* high byte */
    }

    DBG(DBG_info, "Loading scan motor tables 0-2 (constant speed 0x%04X = %u)\n",
        scan_speed, scan_speed);

    /* Table 0: 0x10000000 (scan accel/cruise) */
    status = plustek8200i_dma_write(fd, 0x10000000, scan_table, 512);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Scan motor table 0 write (0x10000000) failed: %s\n",
            sane_strstatus(status));
        return status;
    }

    /* Table 1: 0x10004000 (backtrack) */
    status = plustek8200i_dma_write(fd, 0x10004000, scan_table, 512);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Scan motor table 1 write (0x10004000) failed: %s\n",
            sane_strstatus(status));
        return status;
    }

    /* Table 2: 0x10008000 (scan deceleration) */
    status = plustek8200i_dma_write(fd, 0x10008000, scan_table, 512);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Scan motor table 2 write (0x10008000) failed: %s\n",
            sane_strstatus(status));
        return status;
    }

    DBG(DBG_info, "Scan motor tables 0-2 loaded successfully (speed=%u)\n", scan_speed);
    return SANE_STATUS_GOOD;
}

/**
 * Configure scan-phase registers
 *
 * Writes the complete scan-phase register configuration extracted from
 * the macOS USB capture before MOVE #4 (the first MOVE that produces
 * scan data). This includes:
 *   1. CCD timing registers (0xD0-0xF8)
 *   2. Scan buffer/area configuration (two WriteRegs blocks)
 *   3. Scan-phase AFE calibration (6 blocks)
 *
 * Extracted from macOS USB capture timestamps 0:39.939-0:39.971.
 *
 * IMPORTANT: These values are different from the init-phase values.
 * Without this reconfiguration, the scanner motor runs but produces
 * no valid scan data (valid words = 0).
 */
SANE_Status plustek8200i_configure_scan_phase(int fd)
{
    SANE_Status status;
    int i;

    DBG(DBG_info, "=== Configuring scan-phase registers ===\n");

    /*
     * Step 1: Shading addresses and image buffer configuration
     *
     * Register names (see the note at the register definitions in plustek8200i.h):
     *   0xD0-0xD9: SH0DWN-SH9DWN — Shading data start addresses
     *              in SDRAM for each calibration channel segment.
     *   0xE0-0xF7: Image buffer boundaries — Circular buffer
     *              start/end addresses in SDRAM for each color channel.
     *              Units: 1k words (2048 bytes per unit for 16-bit data).
     *   0xF8: MAXSEL/MINSEL — Bank selection for buffer-full/empty.
     *
     * NOTE: Previous comments incorrectly labeled these as "CCD timing"
     * and "CCD integration window pairs". They are in fact
     * shading and image buffer address registers.
     */
    DBG(DBG_info, "Scan-phase: shading addrs (D0-D2) + image buffers (E0-F8)\n");

    /* Shading data addresses (SH0DWN, SH1DWN, SH2DWN) */
    status = plustek8200i_write_reg(fd, 0xD0, 0x0A);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xD1, 0x0A);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xD2, 0x0A);
    if (status != SANE_STATUS_GOOD) return status;

    /* Image buffer addresses (R1DWN/R1UP through B2DWN/B2UP) */
    status = plustek8200i_write_reg(fd, 0xE0, 0x00);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE1, 0xA8);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE2, 0x0B);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE3, 0x35);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE4, 0x0B);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE5, 0x36);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE6, 0x15);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE7, 0xC3);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE8, 0x15);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xE9, 0xC4);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xEA, 0x20);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xEB, 0x51);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xEC, 0x20);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xED, 0x52);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xEE, 0x2A);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xEF, 0xDF);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF0, 0x2A);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF1, 0xE0);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF2, 0x35);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF3, 0x6D);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF4, 0x35);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF5, 0x6E);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF6, 0x3F);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF7, 0xFB);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xF8, 0x05);
    if (status != SANE_STATUS_GOOD) return status;

    /*
     * Step 2: Scan-phase WriteRegs block 1 (32 register pairs = 64 bytes)
     *
     * Register names (see the note at the register definitions in plustek8200i.h):
     *   0x80: VRHOME/VRMOVE/VRBACK/VRSCAN — motor voltage regs
     *   0x81-0x86: ROFFSET/GOFFSET/BOFFSET — RGB offset calibration
     *   0x87: ACYCNRLC, ENOFFSET, LEDADD, CK4ADC, AUTOCONF
     *   0x2C-0x2D: ENDPIXEL — end pixel position
     *   0x01: SCAN(0), SHDAREA(1), DVDSET(5) — scan control
     *   0x04: LINEART, BITSET, SCANMOD — scan mode config
     *
     * NOTE: 0x80-0x87 are motor voltage and RGB offset registers,
     * NOT buffer addresses as previously documented.
     */
    DBG(DBG_info, "Scan-phase: WriteRegs block 1 (32 pairs)\n");
    {
        static const uint8_t scan_block1[64] = {
            0x02, 0x00,  /* MTRPWR=0 (motor power off for now) */
            0x3D, 0x00,  /* FEEDL[19:16]=0 (feed length bits 19:16) */
            0x3E, 0x00,  /* FEEDL[15:8]=0 */
            0x3F, 0x01,  /* FEEDL[7:0]=1 → FEEDL=1 motor step */
            0x7D, 0x00,  /* MDMSK */
            0x7E, 0x36,  /* LPERIOD[15:8] — line period high */
            0x7F, 0xB0,  /* LPERIOD[7:0] — line period low */
            0x1C, 0x20,  /* TGTIME */
            0x8A, 0x00,  /* Interrupt register */
            0x8B, 0x00,
            0x8C, 0x00,
            0x8D, 0x00,
            0x8E, 0x00,
            0x8F, 0x00,
            0x90, 0x00,  /* PREFED */
            0x91, 0x00,
            0x92, 0x00,  /* PSTFED */
            0x2C, 0x04,  /* ENDPIXEL[15:8] */
            0x2D, 0xB0,  /* ENDPIXEL[7:0] */
            0x80, 0x00,  /* VRHOME/VRMOVE/VRBACK/VRSCAN */
            0x81, 0x40,  /* ROFFSET[8] */
            0x82, 0x00,  /* ROFFSET[7:0] */
            0x83, 0x00,  /* GOFFSET[8] */
            0x84, 0x40,  /* GOFFSET[7:0] */
            0x85, 0x00,  /* BOFFSET[8] */
            0x86, 0x02,  /* BOFFSET[7:0] */
            0x87, 0x40,  /* ACYCNRLC/ENOFFSET/LEDADD/CK4ADC */
            0x04, 0x42,  /* SCANMOD=2, BITSET=1 */
            0x01, 0x02,  /* SHDAREA=1, SCAN=0 (not yet) */
            0x05, 0x40,  /* DPIHW/STOPTIM */
            0xA2, 0x00,
            0xA3, 0x00
        };
        status = plustek8200i_write_regs(fd, scan_block1, sizeof(scan_block1));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Scan-phase WriteRegs block 1 failed\n");
            return status;
        }
    }

    /*
     * Step 3: Scan-phase WriteRegs block 2 (17 register pairs = 34 bytes)
     *
     * Configures line count, scan area, and motor parameters.
     * Key registers:
     *   0x25-0x27: Line count (how many lines to scan)
     *   0x28-0x2B: Scan area parameters
     *   0x0D: CMD register (CLRMCNT)
     */
    DBG(DBG_info, "Scan-phase: WriteRegs block 2 (17 pairs)\n");
    {
        static const uint8_t scan_block2[34] = {
            0xA6, 0x00,
            0xA7, 0x01,
            0xA8, 0x00,
            0xA9, 0x01,
            0x1D, 0x80,
            0xA4, 0x00,
            0xA5, 0x01,
            0xAC, 0x00,
            0xAD, 0x01,
            0x0D, 0x04,  /* CMD: CLRMCNT */
            0x25, 0x00,  /* Line count low */
            0x26, 0x00,  /* Line count mid */
            0x27, 0x01,  /* Line count high */
            0x28, 0x00,
            0x29, 0x2B,
            0x2A, 0x37,
            0x2B, 0x01
        };
        status = plustek8200i_write_regs(fd, scan_block2, sizeof(scan_block2));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Scan-phase WriteRegs block 2 failed\n");
            return status;
        }
    }

    /*
     * Step 4: Scan-phase AFE (6 blocks)
     *
     * Different from init AFE: only 6 blocks (indices 2-7), not 8.
     * Blocks 5-7 have data=0x0080 instead of 0x0000.
     * This configures analog gain/offset for the scan CCD channels.
     */
    DBG(DBG_info, "Scan-phase: AFE blocks (6 blocks, indices 2-7)\n");
    {
        static const uint8_t scan_afe_data[6][3] = {
            { 0x02, 0x00, 0x00 },  /* AFE idx 2: data=0x0000 */
            { 0x03, 0x00, 0x00 },  /* AFE idx 3: data=0x0000 */
            { 0x04, 0x00, 0x00 },  /* AFE idx 4: data=0x0000 */
            { 0x05, 0x00, 0x80 },  /* AFE idx 5: data=0x0080 */
            { 0x06, 0x00, 0x80 },  /* AFE idx 6: data=0x0080 */
            { 0x07, 0x00, 0x80 },  /* AFE idx 7: data=0x0080 */
        };

        for (i = 0; i < 6; i++) {
            uint8_t block[6];
            block[0] = 0x51; block[1] = scan_afe_data[i][0];
            block[2] = 0x5D; block[3] = scan_afe_data[i][1];
            block[4] = 0x5E; block[5] = scan_afe_data[i][2];

            DBG(DBG_debug, "Scan AFE block %d: idx=0x%02x data=0x%02x%02x\n",
                i, scan_afe_data[i][0], scan_afe_data[i][1], scan_afe_data[i][2]);

            status = plustek8200i_write_regs(fd, block, 6);
            if (status != SANE_STATUS_GOOD) {
                DBG(DBG_error, "Scan AFE block %d failed\n", i);
                return status;
            }
        }
    }

    DBG(DBG_info, "=== Scan-phase configuration complete ===\n");
    return SANE_STATUS_GOOD;
}

/*
 * Verified-good CCD timing / clock / segment configuration.
 *
 * Source: the Windows SilverFast USB capture
 *   a SilverFast USB capture
 * Full register table: the recorded Windows register set
 *
 * These are not derived by analogy and not guessed. They are the exact
 * values observed driving this physical scanner minutes before our own
 * capture of the same film frame, and that stream decodes to a clean
 * image with no processing at all:
 *
 *     slice at 2592 samples -> 864 px, RGB interleaved -> luminance
 *
 * Our configuration instead produces a stream whose line-period carrier
 * is SUPPRESSED (5.4%) and replaced by +-2-sample sidebands at 26%; the
 * Windows stream has a healthy carrier (15.7%, 17x median) and no
 * sidebands. Check with:
 *
 *     python3 a USB stream decoder <capture.pcapng>
 *
 * The "dark/dummy pixel bands every 512 pixels" and "R/GB interleaving"
 * described in plustek8200i.c, and the whole desegmentation pipeline
 * built to undo them, are artifacts of the CCD values we were using.
 *
 * IMPORTANT: DPISET and the CCD clocking MUST change together. An
 * earlier session tried genesys-style CCD registers (0x70=0x0A, which is
 * what Windows uses) while holding DPISET=1200 and got "green-only
 * constant data", concluded "root cause unknown", and reverted. Windows
 * pairs that clocking with DPISET=100.
 */
SANE_Status plustek8200i_apply_verified_ccd_regs(int fd)
{
    SANE_Status status;
    int i;

    /*
     * (register, value) pairs, Windows state AS OF SCAN START.
     *
     * Sampled at the frame where the image bulk stream begins, NOT at the
     * end of the capture. 14 registers are rewritten during post-scan
     * teardown, and an earlier attempt here copied those teardown values
     * by mistake -- including 0x51, 0xA5, 0xA7 and 0xA9, where our legacy
     * values were already correct. That broke the segment configuration
     * and the scan returned constant data (one channel pinned ~38404,
     * the others near zero), which is exactly the "green-only constant
     * data" an earlier session hit and could not explain.
     *
     * Only registers that genuinely differ at scan time are written here.
     */
    static const uint8_t verified[] = {
        /*
         * DPISET is NOT written here. It selects the resolution and is
         * set from dev->opt_resolution in the scan register block, which
         * runs BEFORE this function -- hardcoding it here silently
         * overrode the caller and made a whole DPISET sweep return the
         * same 864 px line three times.
         */

        /* CCD transfer-gate timing */
        0x52, 0x0B, 0x53, 0x0D, 0x54, 0x0F,
        0x56, 0x05, 0x57, 0x07,

        /*
         * 0x55 is the GREEN channel's transfer-gate sample point, and it
         * is the one place where Windows' value does NOT work for us.
         *
         * Windows runs 0x55=0x01. With that, green comes back quantised
         * to a median code step of 8 -- 444 distinct values across the
         * frame against red's 29337 -- while red and blue are clean. Our
         * legacy 0x02 restores it to 15649 distinct values with the low
         * byte fully populated, and leaves red and blue untouched.
         *
         * Isolated by reverting each register of this block in turn
         * against a distinct-value count; 0x55 alone accounts for all of
         * it (0x54 alone changes nothing). Presumably some other clock
         * phase differs between our configuration and Windows' and
         * shifts the required green sample point by one, but 0x02 is
         * what this configuration needs -- measured, not derived.
         */
        0x55, 0x02,
        0x5A, 0x31, 0x5B, 0x79,

        /* CCD clock offsets / multiplexing */
        0x70, 0x0A, 0x71, 0x0B, 0x72, 0x0C, 0x73, 0x0D,

        /* Segment control. 0xA5/0xA7/0xA9/0xAB are NOT written: they
         * already match Windows at scan time. */
        0xA3, 0x01,

        /*
         * AFE. Changing the CCD clock phases above moves the moment at
         * which each colour is valid on the output, so the AFE's sample
         * point has to move with them -- these are not independent. With
         * the CCD block applied but the AFE left at our values, the scan
         * returns G pinned at ~38404 and R/B near zero, which is the
         * "green-only constant data" symptom.
         */
        0x80, 0x00, 0x84, 0xF2, 0x86, 0x29, 0x87, 0x72,

        /*
         * REG06 = SCANMOD[7:5] | PWRBIT[4] | GAIN4[3] | OPTEST[2:0].
         * Windows runs SCANMOD=7, we were running SCANMOD=0. SCANMOD
         * selects the sensor readout mode, so getting it wrong is a
         * plausible cause of a channel reading constant. PWRBIT is set
         * in both, so this does not touch power.
         */
        0x06, 0xF0,

        /* Remaining non-motor differences at scan time. */
        0x2A, 0x3B, 0x2B, 0x02,
    };

    /*
     * AFE gain and offset, per channel.
     *
     * The AFE is programmed over a side channel: write the register
     * index to 0x51, then the value as 0x5D (high) / 0x5E (low).
     *
     * MEASURED semantics (an earlier comment here had these backwards):
     *   index 0x02/0x03/0x04 = R/G/B GAIN, a 6-bit field. Multiplicative:
     *     sweeping it scales dark and signal by the same ratio, and 0x40
     *     behaves identically to 0x00 because bit 6 is not part of the
     *     field. Response is monotonic but super-linear near the top, so
     *     calibration has to iterate rather than fit two points.
     *   index 0x05/0x06/0x07 = R/G/B OFFSET. Purely additive: raising it
     *     shifted dark by +1202 and the signal by +1205.
     *
     * The compiled values below come from the Windows capture of one
     * specific unit and leave green and blue badly under-exposed (p99.5
     * at 22% and 18% of full scale against red's 45%). Per-device
     * calibration overrides them; see the config file below.
     *
     * Our driver left all three gains at a flat 0x80 with zero offsets.
     * Windows ends its calibration on this scanner at the values below
     * -- roughly a third of our gain, and unequal per channel. Running
     * flat 0x80 left the channels at very different levels and
     * quantised green hard: only 474 distinct values across the frame
     * against red's 23329, i.e. about 9 bits of the 16.
     *
     * These are Windows' calibrated results for this unit, used here as
     * fixed defaults. Doing our own AFE calibration pass would be
     * better.
     */
    static const uint8_t afe[][3] = {
        { 0x02, 0x00, 0x10 },   /* gain   R */
        { 0x03, 0x00, 0x02 },   /* gain   G */
        { 0x04, 0x00, 0x0D },   /* gain   B */
        { 0x05, 0x00, 0x20 },   /* offset R */
        { 0x06, 0x00, 0x2B },   /* offset G */
        { 0x07, 0x00, 0x1B },   /* offset B */
    };
    uint8_t afe_use[sizeof(afe) / sizeof(afe[0])][3];

    memcpy(afe_use, afe, sizeof(afe_use));

    DBG(DBG_info, "Applying VERIFIED CCD/clock/segment registers "
        "(Windows capture 20261005-092303); DPISET=100\n");

    status = plustek8200i_write_regs(fd, verified, sizeof(verified));
    if (status != SANE_STATUS_GOOD)
        return status;

    /*
     * Indices 0x02-0x04 are the R/G/B offsets, 0x05-0x07 the gains.
     * Used to characterise the AFE response for per-device calibration.
     */
    /*
     * Per-device calibration, if present. One "idx=value" pair per line,
     * hex, '#' comments allowed:
     *     ~/.sane/plustek8200i-afe.conf
     * Produced by tools/calibrate_afe.py, which converges the gains on
     * the actual scanner rather than trusting values captured from a
     * different unit..
     */
    {
        const char *home = getenv("HOME");
        char path[512];
        FILE *f;
        if (home) {
            snprintf(path, sizeof(path),
                     "%s/.sane/plustek8200i-afe.conf", home);
            f = fopen(path, "r");
            if (f) {
                char line[128];
                int n = 0;
                while (fgets(line, sizeof(line), f)) {
                    unsigned int idx, val;
                    char *h = strchr(line, '#');
                    if (h) *h = '\0';
                    if (sscanf(line, "%x=%x", &idx, &val) == 2) {
                        for (i = 0; i < (int)(sizeof(afe)/sizeof(afe[0])); i++)
                            if (afe_use[i][0] == idx) {
                                afe_use[i][1] = (uint8_t)(val >> 8);
                                afe_use[i][2] = (uint8_t)(val & 0xFF);
                                n++;
                            }
                    }
                }
                fclose(f);
                DBG(DBG_info, "AFE: %d values from %s\n", n, path);
            }
        }
    }


    for (i = 0; i < (int)(sizeof(afe) / sizeof(afe[0])); i++) {
        uint8_t block[6] = { 0x51, afe_use[i][0],
                             0x5D, afe_use[i][1],
                             0x5E, afe_use[i][2] };
        status = plustek8200i_write_regs(fd, block, sizeof(block));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "AFE write %d failed\n", i);
            return status;
        }
    }
    DBG(DBG_info, "AFE: gains R/G/B = %02X/%02X/%02X, "
        "offsets R/G/B = %02X/%02X/%02X\n",
        afe_use[0][2], afe_use[1][2], afe_use[2][2],
        afe_use[3][2], afe_use[4][2], afe_use[5][2]);

    /*
     * Infrared channel.
     *
     * From the device plist: IRMASK = 0x37,0x04,0x04, read as
     * (register, mask, value). the vendor driver does a
     * read-modify-write with it:
     *
     *   IR on :  reg = (cur & ~mask) | value          -> set bit 2
     *   IR off:  reg = (cur & ~mask) | (mask ^ value) -> clear bit 2
     *
     * The plist also carries full NegativeIR / PositiveIR profiles with
     * their own LINEGAIN / PIXELGAIN / LINEDARK, so the IR pass is a
     * SEPARATE scan with its own calibration, not a fourth channel in
     * the RGB readout.
     *
     * This only flips an illumination bit -- no motor, no geometry.
     */
    if (plustek8200i_use_infrared()) {
        uint8_t cur = 0, v;
        uint8_t ir_gain[3] = { 0x01, 0x01, 0x01 };
        int ir_calibrated = 0;
        int k;


        /* IR gains from the per-device calibration file, if present. */
        if (!ir_calibrated) {
            const char *home = getenv("HOME");
            char path[512];
            FILE *f;
            if (home) {
                snprintf(path, sizeof(path),
                         "%s/.sane/plustek8200i-afe.conf", home);
                f = fopen(path, "r");
                if (f) {
                    char line[128];
                    while (fgets(line, sizeof(line), f)) {
                        unsigned int idx, val;
                        char *h = strchr(line, '#');
                        if (h) *h = '\0';
                        if (sscanf(line, "ir%x=%x", &idx, &val) == 2 &&
                            idx >= 0x02 && idx <= 0x04) {
                            ir_gain[idx - 0x02] = (uint8_t)val;
                            ir_calibrated = 1;
                        }
                    }
                    fclose(f);
                }
            }
        }

        /* 1. IR illumination on: reg 0x37, mask 0x04, value 0x04. */
        plustek8200i_read_reg(fd, 0x37, &cur);
        v = (uint8_t)((cur & (uint8_t)~0x04) | 0x04);
        plustek8200i_write_reg(fd, 0x37, v);
        DBG(DBG_info, "IR: reg 0x37 0x%02X -> 0x%02X\n", cur, v);

        /*
         * 2. White lamp OFF, XPASEL on. REG03 bit 4 is lamp power and
         * bit 5 is XPASEL. With the white lamp left on, the IR LED
         * merely adds light: the scan saturates and still correlates
         * +0.86 with the visible image. With it off, the dye image
         * disappears -- interior correlation drops to +0.007.
         */
        plustek8200i_read_reg(fd, 0x03, &cur);
        v = (uint8_t)((cur & (uint8_t)~0x10) | 0x20);
        plustek8200i_write_reg(fd, 0x03, v);
        DBG(DBG_info, "IR: reg 0x03 0x%02X -> 0x%02X (lamp off, XPASEL)\n",
            cur, v);

        /*
         * 3. AFE gain for the IR pass.
         *
         * Film base is nearly transparent to IR, so the gains
         * calibrated for the visible pass clip the whole frame at
         * 65535 and only total blockers register. The IR pass needs
         * its own, far lower, gains.
         *
         * The plist carries NegativeIR / PositiveIR profiles, but their
         * LINEGAIN / PIXELGAIN / LINEDARK values drive the ASIC's
         * shading-correction tables (DVDSET), which this driver does
         * not implement -- so they cannot be applied as-is. Instead the
         * IR gains are calibrated per device like the visible ones, by
         * tools/calibrate_afe.py --infrared, and cached as ir02/ir03/
         * ir04 in ~/.sane/plustek8200i-afe.conf. Gain 1 is the fallback
         * when no calibration exists: safe, because it cannot clip.
         */
        for (k = 0; k < 3; k++) {
            uint8_t g = ir_gain[k];
            uint8_t blk[6] = { 0x51, (uint8_t)(0x02 + k),
                               0x5D, 0x00, 0x5E, g };
            plustek8200i_write_regs(fd, blk, sizeof(blk));
        }
        DBG(DBG_info, "IR: AFE gains %02X/%02X/%02X%s\n",
            ir_gain[0], ir_gain[1], ir_gain[2],
            ir_calibrated ? " (calibrated)" : " (default, uncalibrated)");
    }

    /*
     * Debug hook for bisecting against a reference capture:
     * Written last, so these win over everything above. Pairs are
     * hex "reg=value", comma separated.
     */
    return SANE_STATUS_GOOD;
}
