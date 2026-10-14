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
 * Register initialization for the Plustek OpticFilm 8200i.
 *
 * The values are the init-phase register writes observed in a USB capture
 * of the vendor driver (not the scan-time values).  Do not change them
 * without comparing against a capture.
 */

#define DEBUG_DECLARE_ONLY
#include "plustek8200i.h"
#include "plustek8200i-scan.h"  /* For plustek8200i_write_regs() */
#include "plustek8200i-dma.h"   /* For plustek8200i_dma_write() */
#include <unistd.h>

/*
 * Bulk register initialization: ~116 register pairs.  Registers the
 * vendor driver writes separately or not at all are left out: 0x00, 0x0E
 * (written before this), 0x0F (MOVE, written last), 0x51, 0x5D, 0x6C,
 * 0xAE, 0xAF (written before MOVE), 0xD8 and 0xDC.
 */
SANE_Status plustek8200i_init_registers(int fd)
{
    SANE_Status status;
    uint8_t reg_pairs[232];
    int idx = 0;

    DBG(DBG_info, "WriteAllReg: preparing init-phase register pairs...\n");

    reg_pairs[idx++] = 0x01; reg_pairs[idx++] = 0x22;
    reg_pairs[idx++] = 0x02; reg_pairs[idx++] = 0x78;
    reg_pairs[idx++] = 0x03; reg_pairs[idx++] = 0x20;
    reg_pairs[idx++] = 0x04; reg_pairs[idx++] = 0x02;
    reg_pairs[idx++] = 0x05; reg_pairs[idx++] = 0x48;
    reg_pairs[idx++] = 0x06; reg_pairs[idx++] = 0x18;
    reg_pairs[idx++] = 0x07; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x08; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x09; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x0A; reg_pairs[idx++] = 0x40;
    reg_pairs[idx++] = 0x0B; reg_pairs[idx++] = 0x6C;
    reg_pairs[idx++] = 0x0C; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x0D; reg_pairs[idx++] = 0x00;
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
    reg_pairs[idx++] = 0x3E; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x3F; reg_pairs[idx++] = 0x01;
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
    reg_pairs[idx++] = 0x5E; reg_pairs[idx++] = 0x1F;
    reg_pairs[idx++] = 0x5F; reg_pairs[idx++] = 0x05;
    reg_pairs[idx++] = 0x60; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x61; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0x63; reg_pairs[idx++] = 0x20;
    reg_pairs[idx++] = 0x67; reg_pairs[idx++] = 0x7F;
    reg_pairs[idx++] = 0x68; reg_pairs[idx++] = 0x7F;
    reg_pairs[idx++] = 0x69; reg_pairs[idx++] = 0x01;
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
    reg_pairs[idx++] = 0xB8; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xB9; reg_pairs[idx++] = 0x38;
    reg_pairs[idx++] = 0xBA; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xBD; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xBE; reg_pairs[idx++] = 0x00;
    reg_pairs[idx++] = 0xBF; reg_pairs[idx++] = 0x00;

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

/*
 * CCD timing adjustments written individually after each bulk register
 * write: 0x10=0x0E, 0x13=0x0E, 0x0B=0x84, 0x13=0x0F, 0x0B=0x8C, ~100 ms
 * delay, 0x0A=0x40.
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

    usleep(100000);

    status = plustek8200i_write_reg(fd, 0x0A, 0x40);
    if (status != SANE_STATUS_GOOD) return status;

    DBG(DBG_info, "Post-WriteAllReg complete\n");
    return SANE_STATUS_GOOD;
}

/*
 * AFE (analog front end) initialization: 8 blocks of 6 bytes, each a
 * separate write, after the post-register adjustments.  The AFE is
 * reached indirectly: 0x51 = register index, 0x5D/0x5E = data high/low.
 * Block n sets index n; only block 0 (0x00F8) and block 1 (0x0080) are
 * nonzero.
 */
SANE_Status plustek8200i_write_afe_init(int fd)
{
    SANE_Status status;
    int i;

    /* [index, data_high, data_low] */
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
        uint8_t block[6];
        block[0] = 0x51; block[1] = afe_data[i][0];
        block[2] = 0x5D; block[3] = afe_data[i][1];
        block[4] = 0x5E; block[5] = afe_data[i][2];

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

/*
 * Third register pass, sent after MOVE #2 as a proper bulk write (125
 * register pairs), not as the init pass plus overrides.  Main differences
 * from the init pass: 0x06 STEPNO, 0x0B CCD control, 0x1C-0x1E and
 * 0x5A-0x5B timing, 0x3B backlash, 0x52-0x57 CCD clock offsets, 0x70-0x73
 * CCD multiplexing and 0x7E-0x7F line period (0x36B0 vs 0x2AF8).
 */
SANE_Status plustek8200i_init_pass3_registers(int fd)
{
    SANE_Status status;

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

    /* One 64-byte chunk per block, as in the capture */
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

/*
 * Scan-phase configuration.  The scan is a multi-MOVE sequence: MOVE #1
 * homes (init motor table), #2-3 calibrate and position (scan motor
 * table), #4 scans with the SCAN bit set.  MOVE #4 needs its own CCD
 * timing, buffer layout and AFE settings, taken from a USB capture.
 */

/*
 * Scan-phase motor table from the MOVE #3 bulk OUT of a USB capture.
 * Unlike init_motor_table (plustek8200i-motor-tables.c) it starts at 0x1585
 * rather than 0x2048.  Written to 0x1000C000 and 0x10010000.
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

/*
 * Load scan-phase motor tables 1-3 (0x10000000 scan, 0x10004000 backtrack,
 * 0x10008000 deceleration; see the register note in plustek8200i.h).
 * With FASTFED (reg 0x02 bit 3) clear the motor uses these; set, tables
 * 4-5.  All three are filled with one constant period, i.e. constant
 * velocity; acceleration is done by tables 4/5 in the positioning MOVEs.
 * Call after the scan-phase register configuration, before the scan MOVE.
 */
SANE_Status plustek8200i_load_scan_motor_table(int fd, int period)
{
    SANE_Status status;
    uint8_t scan_table[512];
    int i;

    /*
     * sane-genesys formula: table_value = (LPERIOD * ydpi / base_ydpi) >> step_shift
     * with LPERIOD = 17408 (reg 0x38-0x39), ydpi = 1200, base_ydpi = 3600
     * and quarter steps (shift 2): 17408 * 1200 / 3600 >> 2 = 1450.
     */
    uint16_t lperiod = 17408;   /* 0x4400 */
    int ydpi = 1200;
    int base_ydpi = 3600;
    int step_shift = 2;         /* quarter step (shift = 2) */

    unsigned target_speed_w = (lperiod * ydpi) / base_ydpi;
    uint16_t scan_speed = (uint16_t)(target_speed_w >> step_shift);

    /*
     * Table entries are periods: a larger value is a slower carriage and
     * more lines per inch (init_motor_table ramps 8264 -> 294).  Raising
     * it only slows the motor, so it cannot ask for an unachievable step
     * rate.  The formula predicts 1200 dpi at 1450 but the measured
     * vertical sampling is ~1490 dpi (PLUSTEK8200I_HW_LINE_DPI), so
     * scale from the measurement.
     */
    if (period >= 500 && period <= 20000)
        scan_speed = (uint16_t)period;

    DBG(DBG_info, "Scan motor period=%u (0x%04X) -> vertical ~%u dpi\n",
        scan_speed, scan_speed,
        (unsigned)((unsigned long)scan_speed * PLUSTEK8200I_HW_LINE_DPI / 1450));

    for (i = 0; i < 512; i += 2) {
        scan_table[i]     = scan_speed & 0xFF;
        scan_table[i + 1] = (scan_speed >> 8) & 0xFF;
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

/*
 * Configure the scan-phase registers before MOVE #4, the first MOVE that
 * produces scan data: shading addresses and image buffers, two register
 * blocks, and the scan-phase AFE.  Taken from a USB capture; without
 * them the motor runs but no valid data arrives.
 */
SANE_Status plustek8200i_configure_scan_phase(int fd)
{
    SANE_Status status;
    int i;

    DBG(DBG_info, "=== Configuring scan-phase registers ===\n");

    /*
     * Shading addresses and image buffer configuration (register names:
     * see the note in plustek8200i.h):
     *   0xD0-0xD9: SH0DWN-SH9DWN, shading data start addresses in SDRAM
     *   0xE0-0xF7: circular image buffer start/end per color channel,
     *              in 1k-word units
     *   0xF8:      MAXSEL/MINSEL, bank selection for buffer full/empty
     */
    DBG(DBG_info, "Scan-phase: shading addrs (D0-D2) + image buffers (E0-F8)\n");

    status = plustek8200i_write_reg(fd, 0xD0, 0x0A);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xD1, 0x0A);
    if (status != SANE_STATUS_GOOD) return status;
    status = plustek8200i_write_reg(fd, 0xD2, 0x0A);
    if (status != SANE_STATUS_GOOD) return status;

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
     * Block 1 (32 pairs): 0x80 motor voltages (VRHOME/VRMOVE/VRBACK/
     * VRSCAN), 0x81-0x86 RGB offsets, 0x87 AFE config, 0x2C-0x2D
     * ENDPIXEL, 0x01 scan control, 0x04 scan mode.
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

    /* Block 2 (17 pairs): 0x25-0x27 line count, 0x28-0x2B scan area, 0x0D CMD (CLRMCNT) */
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

    /* Scan-phase AFE: indices 2-7 only, with blocks 5-7 at 0x0080 */
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
 * CCD timing / clock / segment configuration taken from a USB capture of
 * the Windows SilverFast software on this model.  It decodes to a clean
 * image with no processing; the earlier values produced the dark/dummy
 * pixel bands and R/GB interleaving that the desegmentation code in
 * plustek8200i.c worked around.
 *
 * DPISET and the CCD clocking must change together: genesys-style CCD
 * registers (0x70=0x0A) with DPISET=1200 give constant green-only data.
 * Windows pairs that clocking with DPISET=100.
 */
SANE_Status plustek8200i_apply_verified_ccd_regs(int fd)
{
    SANE_Status status;
    int i;

    /*
     * (register, value) pairs as of scan start in the Windows capture,
     * not its end: 14 registers are rewritten during post-scan teardown,
     * and copying those gave constant data.  Only registers that differ
     * at scan time are written.
     */
    static const uint8_t verified[] = {
        /* DPISET is deliberately not written: the scan register block sets it
         * from dev->opt_resolution before this runs. */

        /* CCD transfer-gate timing */
        0x52, 0x0B, 0x53, 0x0D, 0x54, 0x0F,
        0x56, 0x05, 0x57, 0x07,

        /*
         * 0x55 is the green transfer-gate sample point, and the one place
         * Windows' value (0x01) does not work: green is quantised to 444
         * distinct values against red's 29337.  0x02 restores 15649 with
         * red and blue unchanged (isolated by reverting each register in
         * turn).  Measured, not derived.
         */
        0x55, 0x02,
        0x5A, 0x31, 0x5B, 0x79,

        /* CCD clock offsets / multiplexing */
        0x70, 0x0A, 0x71, 0x0B, 0x72, 0x0C, 0x73, 0x0D,

        /* Segment control. 0xA5/0xA7/0xA9/0xAB are NOT written: they
         * already match Windows at scan time. */
        0xA3, 0x01,

        /*
         * AFE.  Changing the CCD clock phases moves when each colour is
         * valid, so the AFE sample point must move with them; applying the
         * CCD block alone gives constant green-only data.
         */
        0x80, 0x00, 0x84, 0xF2, 0x86, 0x29, 0x87, 0x72,

        /* REG06 = SCANMOD[7:5] | PWRBIT[4] | GAIN4[3] | OPTEST[2:0].
         * Windows runs SCANMOD=7, we ran 0; PWRBIT is set in both. */
        0x06, 0xF0,

        /* Remaining non-motor differences at scan time. */
        0x2A, 0x3B, 0x2B, 0x02,
    };

    /*
     * AFE gain and offset per channel, written via the side channel
     * (index to 0x51, value to 0x5D high / 0x5E low).  Measured:
     *   index 0x02-0x04 = R/G/B gain, a 6-bit multiplicative field
     *     (0x40 behaves as 0x00); monotonic but super-linear near the top,
     *     so calibration must iterate.
     *   index 0x05-0x07 = R/G/B offset, purely additive.
     *
     * These are Windows' calibrated results for one unit, used as fixed
     * defaults; per-device calibration overrides them.  A flat 0x80
     * quantised green to 474 distinct values against red's 23329.  Our
     * own AFE calibration would be better.
     */
    static const uint8_t afe[][3] = {
        { 0x02, 0x00, 0x10 },   /* gain   R */
        { 0x03, 0x00, 0x02 },   /* gain   G */
        { 0x04, 0x00, 0x0D },   /* gain   B */
        { 0x05, 0x00, 0x20 },   /* offset R */
        { 0x06, 0x00, 0x2B },   /* offset G */
        { 0x07, 0x00, 0x1B },   /* offset B */
    };
    DBG(DBG_info, "Applying VERIFIED CCD/clock/segment registers "
        "(Windows capture 20261005-092303); DPISET=100\n");

    status = plustek8200i_write_regs(fd, verified, sizeof(verified));
    if (status != SANE_STATUS_GOOD)
        return status;

    for (i = 0; i < (int)(sizeof(afe) / sizeof(afe[0])); i++) {
        uint8_t block[6] = { 0x51, afe[i][0],
                             0x5D, afe[i][1],
                             0x5E, afe[i][2] };
        status = plustek8200i_write_regs(fd, block, sizeof(block));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "AFE write %d failed\n", i);
            return status;
        }
    }
    DBG(DBG_info, "AFE: gains R/G/B = %02X/%02X/%02X, "
        "offsets R/G/B = %02X/%02X/%02X\n",
        afe[0][2], afe[1][2], afe[2][2],
        afe[3][2], afe[4][2], afe[5][2]);

    /*
     * Infrared channel.  The device configuration's IRMASK = 0x37,0x04,0x04
     * is (register, mask, value), applied read-modify-write:
     *   IR on :  reg = (cur & ~mask) | value
     *   IR off:  reg = (cur & ~mask) | (mask ^ value)
     * The IR pass is a separate scan with its own calibration, not a
     * fourth RGB channel.  This only flips an illumination bit; no motor
     * or geometry.
     */
    if (plustek8200i_use_infrared()) {
        uint8_t cur = 0, v;
        const uint8_t ir_gain[3] = { 0x01, 0x01, 0x01 };
        int k;


        /* 1. IR illumination on: reg 0x37, mask 0x04, value 0x04. */
        plustek8200i_read_reg(fd, 0x37, &cur);
        v = (uint8_t)((cur & (uint8_t)~0x04) | 0x04);
        plustek8200i_write_reg(fd, 0x37, v);
        DBG(DBG_info, "IR: reg 0x37 0x%02X -> 0x%02X\n", cur, v);

        /*
         * 2. White lamp off, XPASEL on (REG03 bit 4 lamp, bit 5 XPASEL).
         * With the lamp left on the IR LED only adds light: the scan
         * saturates and still correlates +0.86 with the visible image;
         * off, it drops to +0.007.
         */
        plustek8200i_read_reg(fd, 0x03, &cur);
        v = (uint8_t)((cur & (uint8_t)~0x10) | 0x20);
        plustek8200i_write_reg(fd, 0x03, v);
        DBG(DBG_info, "IR: reg 0x03 0x%02X -> 0x%02X (lamp off, XPASEL)\n",
            cur, v);

        /*
         * 3. AFE gain for the IR pass.  Film base is nearly transparent to
         * IR, so the visible-pass gains clip the whole frame; use gain 1.
         * The configuration's NegativeIR / PositiveIR gain profiles drive
         * ASIC shading tables (DVDSET) that this driver does not
         * implement, so they cannot be applied.
         */
        for (k = 0; k < 3; k++) {
            uint8_t g = ir_gain[k];
            uint8_t blk[6] = { 0x51, (uint8_t)(0x02 + k),
                               0x5D, 0x00, 0x5E, g };
            plustek8200i_write_regs(fd, blk, sizeof(blk));
        }
        DBG(DBG_info, "IR: AFE gains %02X/%02X/%02X\n",
            ir_gain[0], ir_gain[1], ir_gain[2]);
    }

    return SANE_STATUS_GOOD;
}
