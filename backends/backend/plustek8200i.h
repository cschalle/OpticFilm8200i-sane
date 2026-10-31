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
 * Provenance: this driver contains no code from any vendor driver.  Its
 * behaviour was worked out by observing USB captures of the vendor
 * software (macOS and Windows) and by measurements on the hardware.
 * Comments that mention a "capture" refer to those observations.
 */

#ifndef PLUSTEK8200I_H
#define PLUSTEK8200I_H

/* Define backend name before including sanei_backend.h */
#define BACKEND_NAME plustek8200i
#define DEBUG_NOT_STATIC

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

/* Debug levels - using standard SANE integer levels */
#define DBG_error   0
#define DBG_warning 1
#define DBG_info    2
#define DBG_debug   3
#define DBG_data    4

/* ==========================================================================
 * Scanner Parameters
 * ========================================================================== */

/* Supported resolutions (dpi), from the vendor's device configuration */
#define PLUSTEK8200I_MAX_DPI          3600
#define PLUSTEK8200I_OPTICAL_DPI      3600
#define PLUSTEK8200I_MIN_DPI          72

/* Film dimensions in mm (35mm film frame) */
#define PLUSTEK8200I_FILM_WIDTH_MM    36.0
#define PLUSTEK8200I_FILM_HEIGHT_MM   24.0

/* Maximum image dimensions at max resolution */
#define PLUSTEK8200I_MAX_PIXELS_X     5100  /* 36mm × 3600/25.4 ≈ 5102 */
#define PLUSTEK8200I_MAX_PIXELS_Y     3400  /* 24mm × 3600/25.4 ≈ 3402 */

/* SANE option numbers */
enum plustek8200i_option {
    OPT_NUM_OPTS = 0,
    OPT_MODE_GROUP,
    OPT_MODE,            /* Color / Gray */
    OPT_BIT_DEPTH,       /* 8 or 16 */
    OPT_RESOLUTION,      /* DPI */
    OPT_IR_CLEAN,        /* infrared dust/scratch removal */
    OPT_AUTO_WB,         /* grey-world channel balance (off = raw) */
    OPT_EXPOSURE,        /* integration time, multiple of the default */
    OPT_GEOMETRY_GROUP,
    OPT_TL_X,            /* Top-left X (mm) */
    OPT_TL_Y,            /* Top-left Y (mm) */
    OPT_BR_X,            /* Bottom-right X (mm) */
    OPT_BR_Y,            /* Bottom-right Y (mm) */
    OPT_SENSOR_GROUP,
    OPT_SCAN_SW,         /* QuickScan button, read-and-clear */
    OPT_INTELLISCAN_SW,  /* IntelliScan button, read-and-clear */
    NUM_OPTIONS
};

/*
 * Button codes as they arrive on interrupt endpoint 0x83, one byte per
 * press.  Measured on the hardware; see plustek8200i-button.c.
 */
#define PLUSTEK8200I_BTN_INTELLISCAN  0x08
#define PLUSTEK8200I_BTN_QUICKSCAN    0x40

/*
 * Fold one interrupt packet into the latch.
 *
 * Unknown bits are kept out: this panel has two buttons and both codes are
 * known, so anything else is not a press we can name and must not surface
 * as one.
 */
static inline unsigned plustek8200i_button_fold(unsigned latch, uint8_t packet)
{
    return latch | (packet & (PLUSTEK8200I_BTN_INTELLISCAN
                              | PLUSTEK8200I_BTN_QUICKSCAN));
}

/*
 * Take one button out of the latch: non-zero if it was pressed since the
 * last call, and the press is consumed.
 */
static inline int plustek8200i_button_take(unsigned *latch, unsigned mask)
{
    int pressed = (*latch & mask) != 0;

    *latch &= ~mask;
    return pressed;
}

/* Device structure */
typedef struct plustek8200i_device {
    struct plustek8200i_device *next;
    SANE_Device sane;
    int usb_fd;

    /* SANE options */
    SANE_Option_Descriptor opt[NUM_OPTIONS];
    SANE_Word opt_resolution;
    SANE_Word opt_bit_depth;
    SANE_Bool opt_ir_clean;      /* apply the IR defect map to colour */
    SANE_Bool opt_auto_wb;       /* scale channels to equal means */
    int       opt_rgbi;          /* mode "Color+Infrared" (RGB + IR frame) */
    SANE_Fixed opt_exposure;     /* exposure factor, 1.0 = default */
    int       in_ir_pass;        /* guard: inside the recursive IR scan */
    int       rgbi_ir_next;      /* Color+Infrared, image first: the IR frame is the next sane_start */
    int       s_eof;             /* the streamed frame has delivered its last line */
    volatile int cancel_requested; /* set by sane_cancel during sane_start */
    int       shading_ok;        /* shading table loaded for this pass */
    int       img_lincnt, img_feedl, img_table_period; /* image pass values */
    double    ir_scale;          /* IR normalisation for this pass */
    double   *ir_gain;           /* per-column IR flat field, sensor order,
                                    NULL when the strips were not usable */
    int       ir_gain_px;        /* entries in ir_gain */
    int       ir_ready, ir_r0, ir_r1, ir_c0, ir_c1; /* IR repair state */
    int       ir_marked, ir_repaired;
    size_t    ir_n;
    double    ir_median, ir_mad;
    /* line-by-line pipeline (see pipe_line) */
    uint8_t  *p_raw;
    int       p_raw_lines, p_raw_line_bytes, p_res_lines, p_shift;
    int       p_ring_lines;      /* 0: p_raw is the whole frame */
    double    p_vsquash;
    double   *p_acc;
    uint16_t *p_row, *p_tmp;
    /* streaming (sane_read pulls raw data and builds lines on demand) */
    int       streaming, s_hw_done, s_first, s_ir_reported, s_frame, s_out_y, s_x0, s_y0, s_x1, s_y1, s_comps;
    size_t    s_raw_read, s_raw_total, s_line_len, s_line_pos;
    uint16_t *s_full;
    uint8_t  *s_stage;            /* one USB chunk, copied into the ring */
    int       s_bps;
    uint16_t *s_ir;               /* Color+Infrared: IR plane, px x lines */
    uint8_t  *s_out;
    double    s_t0;
    volatile int in_read;         /* sane_read is running */
    volatile int in_start;        /* sane_start is running */
    uint8_t  *ir_map;            /* stashed IR frame, 16-bit gray */
    size_t    ir_map_size;
    /* front-panel buttons (see plustek8200i-button.c) */
    unsigned  btn_latch;         /* presses read off 0x83, not yet reported */
    double    btn_last_poll;     /* seconds; coalesces back-to-back polls */
    int opt_mode;          /* 0=Color, 1=Gray */
    SANE_Fixed opt_tl_x;   /* Top-left X in mm (SANE_FIX) */
    SANE_Fixed opt_tl_y;   /* Top-left Y in mm */
    SANE_Fixed opt_br_x;   /* Bottom-right X in mm */
    SANE_Fixed opt_br_y;   /* Bottom-right Y in mm */

    /* Scan state */
    SANE_Bool scanning;
    SANE_Parameters params;

    /* Image buffer */
    SANE_Byte *image_buffer;
    size_t buffer_size;
    size_t bytes_read;
    size_t bytes_total;

    /* CCD color line shift (R/G/B lines physically separated on CCD) */
    int color_shift_g;       /* green channel shift in lines */
    int color_shift_b;       /* blue channel shift in lines */
    int raw_scan_lines;      /* output_lines + max_color_shift */
    int scan_pixels;         /* pixels per line */
    int scan_line_bytes;     /* bytes per raw scan line */
} plustek8200i_device_t;

/* ==========================================================================
 * Register Definitions
 *
 * WARNING: this scanner is a Genesys GL128 (USB 0x07b3/0x1825). The
 * register names below follow the conventions of other Genesys ASICs,
 * and several assignments are known not to carry over. Treat a name as
 * a hint about what a register probably does, never as authority:
 *
 *   - 0x7D-0x7F are the line integration time (the exposure) on this
 *     ASIC, not a gamma address plus an init line period. Measured; see
 *     PLUSTEK8200I_BASE_EXPOSURE below.
 *   - the per-channel exposure registers (0x10-0x15) of other Genesys
 *     ASICs do nothing here.
 *   - per-channel exposure lives in AHB tables, not in registers.
 *
 * Anything marked "measured" was established on the hardware or decoded
 * from a USB capture of the vendor driver, and outranks the names.
 * The older OpticFilm 8200i (0x07b3/0x130d) really is a GL845 and is
 * handled by the genesys backend instead.
 *
 * Naming convention:
 *   REG_xxxx   = register address
 *   xxxx_yyyy  = bit field within register (value is the bit mask)
 *
 * Register addresses for hardware status reads use bit 8 set (0x1xx).
 * When reading via USB with bit 8 set in the address, the GL128 returns
 * the hardware status register instead of the shadow/config register.
 * E.g., reading 0x101 returns status register 0x41 content.
 * ========================================================================== */

/* --- 0x00: GPIOOUT — GPIO Output (active low) --- */
#define REG_GPIOOUT          0x00

/* --- 0x01: CISSET — CIS Setting / Scan Control --- */
#define REG_CISSET           0x01
#define CISSET_SCAN          0x01   /* Bit 0: Start scanning process */
#define CISSET_SHDAREA       0x02   /* Bit 1: Enable shading in scan area */
#define CISSET_NOWAIT        0x04   /* Bit 2: No wait after line scan */
#define CISSET_CCDLMT        0x10   /* Bit 4: CCD line mode timing */
#define CISSET_DVDSET        0x20   /* Bit 5: Enable dark/white shading correction */
#define CISSET_STAGGER       0x40   /* Bit 6: Stagger mode (for CCD odd/even) */
#define CISSET_COMPENB       0x80   /* Bit 7: Compression enable */

/* --- 0x02: MTRPWR — Motor Power and Control --- */
#define REG_MTRPWR           0x02
#define REG_CONTROL          0x02   /* Alias for backward compat */
#define MTRPWR_MTRPWR        0x01   /* Bit 0: Motor driver power enable */
#define MTRPWR_CLAMP         0x02   /* Bit 1: Clamp */
#define MTRPWR_APTS          0x04   /* Bit 2: ADF paper type select */
#define MTRPWR_FASTFED       0x08   /* Bit 3: Fast feed enable (use tables 4-5) */
#define MTRPWR_MTRPWR4       0x10   /* Bit 4: Motor power (secondary) */
#define MTRPWR_AGOHOME       0x20   /* Bit 5: Auto go home after scan */
#define MTRPWR_ACDCDIS       0x40   /* Bit 6: Accel/decel disable (constant speed) */
#define MTRPWR_MTRREV        0x80   /* Bit 7: Motor reverse direction */
/* Legacy aliases */
#define CONTROL_MTRPWR_BIT   0x10
#define CONTROL_FASTFED_BIT  0x08
#define CONTROL_AGOHOME_BIT  0x20

/* --- 0x03: LAMPPWR — Lamp Power Control --- */
#define REG_LAMPPWR          0x03
#define LAMPPWR_LAMPPWR      0x01   /* Bit 0: Lamp power enable */
#define LAMPPWR_LAMPDOG      0x02   /* Bit 1: Lamp watchdog enable */
#define LAMPPWR_LAMPSEL      0x04   /* Bit 2: Lamp select (0=reflective,1=XPA) */
#define LAMPPWR_XPASEL       0x08   /* Bit 3: XPA lamp select */
#define LAMPPWR_XPAVOLT      0x10   /* Bit 4: XPA lamp voltage */
#define LAMPPWR_EXTLAMP      0x20   /* Bit 5: External lamp */

/* --- 0x04: SCANMOD — Scan Mode Control --- */
#define REG_SCANMOD          0x04
#define SCANMOD_SCANMOD_MASK 0x07   /* Bits 2:0: Scan mode select */
#define SCANMOD_FILTER_MASK  0x18   /* Bits 4:3: Filter select */
#define SCANMOD_BITSET       0x20   /* Bit 5: 16-bit output mode */
#define SCANMOD_LINEART      0x40   /* Bit 6: Line-art mode */

/* --- 0x05: DPIHW — DPI and Stop Time --- */
#define REG_DPIHW            0x05
#define DPIHW_STOPTIM_MASK   0x07   /* Bits 2:0: Motor stop time */
#define DPIHW_DPIHW_MASK     0xC0   /* Bits 7:6: Hardware DPI select
                                     * Same field layout as the related ASICs in sane-genesys */
#define DPIHW_DPIHW_600      0x00   /*   00: 600 dpi */
#define DPIHW_DPIHW_1200     0x40   /*   01: 1200 dpi */
#define DPIHW_DPIHW_2400     0x80   /*   10: 2400 dpi */
#define DPIHW_DPIHW_4800     0xC0   /*   11: 4800 dpi */

/* --- 0x06: STEPCTRL — Motor Step Control --- */
#define REG_STEPCTRL         0x06
#define STEPCTRL_HALFSTEP    0x20   /* Bit 5: Half-step mode */
#define STEPCTRL_MTRPLS      0xC0   /* Bits 7:6: Motor pulse type */

/* --- 0x07: TGCNT — CCD Timing Gate Count --- */
#define REG_TGCNT            0x07

/* --- 0x08: STLINE — Scan Start Line --- */
#define REG_STLINE           0x08

/* --- 0x09: COMPCNT — Compression Count --- */
#define REG_COMPCNT          0x09

/* --- 0x0A: DMASEL — DMA Select --- */
#define REG_DMASEL           0x0A

/* --- 0x0B: TG0CNT / ENBDRAM — CCD TG0 Count / SDRAM Control --- */
#define REG_TG0CNT           0x0B
#define REG_SDRAM_CTRL       0x0B   /* Alias */
#define TG0CNT_ENBDRAM       0x08   /* Bit 3: SDRAM power-on (rising edge!) */
#define SDRAM_ENBDRAM        0x08   /* Alias */

/* --- 0x0C: Reserved --- */

/* --- 0x0D: CNTCTRL — Counter Control --- */
#define REG_CNTCTRL          0x0D
#define CNTCTRL_CLRLNCNT     0x01   /* Bit 0: Clear line counter */
#define CNTCTRL_CLRDOCJM     0x02   /* Bit 1: Clear document jam counter */
#define CNTCTRL_CLRMCNT      0x04   /* Bit 2: Clear motor counter */

/* --- 0x0E: SCANRESET — Scanner Software Reset --- */
#define REG_SCANRESET        0x0E

/* --- 0x0F: MOVE — Motor Move Command --- */
#define REG_MOVE             0x0F
#define REG_CISSET_MOVE      0x0F   /* Alias for backward compat */
#define MOVE_START            0x01   /* Write 0x01 to start motor movement */

/* --- 0x10: EXPR[15:8] — Exposure Time Red (high byte) --- */
#define REG_EXPR_H           0x10

/* --- 0x11: EXPR[7:0] — Exposure Time Red (low byte) --- */
#define REG_EXPR_L           0x11

/* --- 0x12: EXPG[15:8] — Exposure Time Green (high byte) --- */
#define REG_EXPG_H           0x12

/* --- 0x13: EXPG[7:0] — Exposure Time Green (low byte) --- */
#define REG_EXPG_L           0x13

/* --- 0x14: EXPB[15:8] — Exposure Time Blue (high byte) --- */
#define REG_EXPB_H           0x14

/* --- 0x15: EXPB[7:0] — Exposure Time Blue (low byte) --- */
#define REG_EXPB_L           0x15

/* --- 0x16: DPISW[11:8] — DPI Software (high nibble) --- */
#define REG_DPISW_H          0x16

/* --- 0x17: DPISW[7:0] — DPI Software (low byte) --- */
#define REG_DPISW_L          0x17

/* --- 0x18: DMASTA — DMA Status --- */
#define REG_DMASTA           0x18

/* --- 0x19: EXPDMY — Exposure Dummy --- */
#define REG_EXPDMY           0x19

/* --- 0x1A: TGTIME — CCD Timing Gate Time --- */
#define REG_TGTIME           0x1A

/* --- 0x1B: CK1MAP/CK3MAP/CK4MAP Control --- */
#define REG_CK1MAP_L         0x1B

/* --- 0x1C: TGTIME2 — Additional TG timing --- */
#define REG_TGTIME2          0x1C

/* --- 0x1D: CTRLHI — Control High --- */
#define REG_CTRLHI           0x1D

/* --- 0x1E-0x1F: EXPR — Exposure/Timing (CCD) --- */
#define REG_1E               0x1E
#define REG_1F               0x1F

/* --- 0x20: STEPTIM — Step Timing (motor step divider) --- */
#define REG_STEPTIM          0x20

/* --- 0x21: STEPNO — Motor Acceleration Step Count ---
 * Number of acceleration steps from motor table during scan.
 * Must be >= 1 for the motor to start in scan mode. */
#define REG_STEPNO           0x21

/* --- 0x22: FWDSTEP — Minimum Forward Step Count ---
 * Minimum number of forward motor steps during scan.
 * Limits how far the motor can travel. */
#define REG_FWDSTEP          0x22

/* --- 0x23: BWDSTEP — Minimum Backward Step Count --- */
#define REG_BWDSTEP          0x23

/* --- 0x24: FASTNO — Fast Table Acceleration Step Count ---
 * Number of acceleration steps for fast feed tables 4-5. */
#define REG_FASTNO           0x24

/* --- 0x25-0x27: LINCNT — Line Count [19:0] --- */
#define REG_LINCNT_H         0x25   /* Bits 19:16 (in bits 3:0) */
#define REG_LINCNT_M         0x26   /* Bits 15:8 */
#define REG_LINCNT_L         0x27   /* Bits 7:0 */
/* Legacy aliases with misleading names: _LOW is LINCNT[19:16] and _HIGH
 * is LINCNT[7:0].  Prefer REG_LINCNT_H/_M/_L above. */
#define REG_LINCNT_LOW       0x25
#define REG_LINCNT_MID       0x26
#define REG_LINCNT_HIGH      0x27

/* --- 0x28-0x2B: Buffer/pixel control ---
 * NOTE: 0x28-0x29 are NOT MAXWD. MAXWD is at 0x35-0x37.
 * Confirmed by USB captures (register 0x35 is written) and by
 * sane-genesys, where REG_MAXWD = 0x35 for the related ASICs. */
#define REG_28               0x28
#define REG_29               0x29
#define REG_2A               0x2A
#define REG_2B               0x2B

/* --- 0x2C-0x2D: DPISET — DPI Setting [11:0] ---
 * Software DPI for the scan; seen written in captures. */
#define REG_DPISET_H         0x2C   /* Bits 11:8 */
#define REG_DPISET_L         0x2D   /* Bits 7:0 */

/* --- 0x2E-0x2F: Reserved --- */
#define REG_2E               0x2E
#define REG_2F               0x2F

/* --- 0x30-0x31: STRPIXEL — Start Pixel [12:0] ---
 * First pixel of the scan area (CCD pixel units); seen written in captures. */
#define REG_STRPIXEL_H       0x30   /* Bits 12:8 */
#define REG_STRPIXEL_L       0x31   /* Bits 7:0 */

/* --- 0x32-0x33: ENDPIXEL — End Pixel [12:0] ---
 * Last pixel of the scan area (CCD pixel units); seen written in captures. */
#define REG_ENDPIXEL_H       0x32   /* Bits 12:8 */
#define REG_ENDPIXEL_L       0x33   /* Bits 7:0 */

/* --- 0x34: CCD Timing --- */
#define REG_34               0x34

/* --- 0x35-0x37: MAXWD — Maximum Word Data [23:0] ---
 * Seen written in captures; same address as REG_MAXWD in sane-genesys.
 * In 4-word units: sane-genesys uses (output_line_bytes_raw * channels) >> 2
 * (the channels multiply is a known genesys quirk, but it works). */
#define REG_MAXWD_H          0x35   /* Bits 23:16 */
#define REG_MAXWD_M          0x36   /* Bits 15:8 */
#define REG_MAXWD_L          0x37   /* Bits 7:0 */

/* --- 0x38-0x39: LPERIOD — Line Period [15:0] ---
 * Time between scan lines in system clock cycles; seen read in captures. */
#define REG_LPERIOD_H        0x38   /* High byte */
#define REG_LPERIOD_L        0x39   /* Low byte */

/* --- 0x3A-0x3C: BKTRST — Back Tracking Parameters --- */
#define REG_3A               0x3A
#define REG_3B               0x3B
#define REG_3C               0x3C

/* --- 0x3D-0x3F: FEEDL — Feed Length [19:0] --- */
#define REG_FEEDL_H          0x3D   /* Bits 19:16 (in bits 3:0) */
#define REG_FEEDL_M          0x3E   /* Bits 15:8 */
#define REG_FEEDL_L          0x3F   /* Bits 7:0 */

/* --- 0x40: STATUS40 — Status Register 1 (read-only) --- */
#define REG_STATUS40         0x40
#define STATUS40_DATAENB     0x01   /* Bit 0: 1=scan mode, 0=command mode */
#define STATUS40_MOTMFLG     0x02   /* Bit 1: Motor movement flag */
#define STATUS40_HISPDFLG    0x04   /* Bit 2: High speed flag */
#define STATUS40_DOCJAM      0x08   /* Bit 3: Document jam detected */
#define STATUS40_CHKVER      0x10   /* Bit 4: Check version */
#define STATUS40_COVERSNR    0x20   /* Bit 5: Cover sensor */
#define STATUS40_ADFSNR      0x40   /* Bit 6: ADF sensor */
#define STATUS40_DOCSNR      0x80   /* Bit 7: Document sensor */

/* --- 0x41: STATUS41 — Status Register 2 (read-only) ---
 * NOTE: Hardware read of reg 0x01 (wValue=0x018E) returns this register.
 * The GL128 aliases hardware status reads through the USB interface. */
#define REG_STATUS41         0x41
#define STATUS41_MOTORENB    0x01   /* Bit 0: Motor operation in progress */
#define STATUS41_FEBUSY      0x02   /* Bit 1: Front-end busy */
#define STATUS41_LAMPSTS     0x04   /* Bit 2: Lamp status */
#define STATUS41_HOMESNR     0x08   /* Bit 3: Home sensor */
#define STATUS41_SCANFSH     0x10   /* Bit 4: Scan finished */
#define STATUS41_FEEDFSH     0x20   /* Bit 5: Feed finished */
#define STATUS41_BUFEMPTY    0x40   /* Bit 6: Buffer empty */
#define STATUS41_PWRBIT      0x80   /* Bit 7: Power bit */

/* --- 0x42-0x45: VALIDWORD — Valid Word Count [25:0] (read-only) --- */
#define REG_VALIDWORD_B3     0x42   /* Bits 25:24 (in bits 1:0) */
#define REG_VALIDWORD_B2     0x43   /* Bits 23:16 */
#define REG_VALIDWORD_B1     0x44   /* Bits 15:8 */
#define REG_VALIDWORD_B0     0x45   /* Bits 7:0 */
#define REG_VALIDWORD        0x42   /* Alias for first byte */

/* --- 0x48-0x4A: FEDCNT — Feed Counter [19:0] (read-only) --- */
#define REG_FEDCNT_H         0x48   /* Bits 19:16 (in bits 3:0) */
#define REG_FEDCNT_M         0x49   /* Bits 15:8 */
#define REG_FEDCNT_L         0x4A   /* Bits 7:0 */

/* --- 0x4B-0x4D: SCANCNT — Scan Counter [19:0] (read-only) --- */
#define REG_SCANCNT_H        0x4B   /* Bits 19:16 (in bits 3:0) */
#define REG_SCANCNT_M        0x4C   /* Bits 15:8 */
#define REG_SCANCNT_L        0x4D   /* Bits 7:0 */

/* --- 0x4F: DOGSTS — Status (DOGON, ROMBSY, etc.) --- */
#define REG_DOGSTS           0x4F
#define DOGSTS_DOGON         0x01   /* Bit 0: Watchdog active */
#define DOGSTS_ROMBSY        0x40   /* Bit 6: ROM busy */

/* --- 0x50-0x51: AFEADDR — AFE Address Register --- */
#define REG_AFEADDR          0x50
#define REG_AFEVAL           0x51

/* --- 0x52-0x57: AFE Latch Points --- */
#define REG_RHI              0x52   /* Red latch high */
#define REG_RLOW             0x53   /* Red latch low */
#define REG_GHI              0x54   /* Green latch high */
#define REG_GLOW             0x55   /* Green latch low */
#define REG_BHI              0x56   /* Blue latch high */
#define REG_BLOW             0x57   /* Blue latch low */

/* --- 0x58-0x59: VSMP/BSMP — Vertical/Bottom Sample --- */
#define REG_VSMP             0x58
#define REG_BSMP             0x59

/* --- 0x5A: RLCSEL — RLC/CDS Mode Select --- */
#define REG_RLCSEL           0x5A

/* --- 0x5B: Reserved --- */

/* --- 0x5C: HISPD — High Speed Control (motor speed during moving) --- */
#define REG_HISPD            0x5C

/* --- 0x5D: HISPD2 — Additional high speed control --- */
#define REG_HISPD2           0x5D

/* --- 0x5E: DECSEL/STOPTIM — Deceleration Select and Motor Stop Time --- */
#define REG_DECSEL           0x5E

/* --- 0x5F: FMOVDEC — Fast Move Deceleration Steps --- */
#define REG_FMOVDEC          0x5F

/* --- 0x60: STEPSEL — Step Selection --- */
#define REG_STEPSEL          0x60
#define STEPSEL_MASK         0x07   /* Bits 2:0: Step table select */

/* --- 0x61-0x62: Z1MOD — Z1 Mode [15:0] --- */
#define REG_Z1MOD_H          0x61
#define REG_Z1MOD_L          0x62

/* --- 0x63: FSTPSEL — Fast Step Selection --- */
#define REG_FSTPSEL          0x63
#define FSTPSEL_MASK         0x07   /* Bits 2:0: Fast step table select */

/* --- 0x64-0x65: Z2MOD — Z2 Mode [15:0] --- */
#define REG_Z2MOD_H          0x64
#define REG_Z2MOD_L          0x65

/* --- 0x66: PHFREQ — Phase Frequency (motor phase) --- */
#define REG_PHFREQ           0x66

/* --- 0x67: MTRPWM — Motor PWM Control --- */
#define REG_MTRPWM           0x67

/* --- 0x68: FASTPWM — Fast PWM --- */
#define REG_FASTPWM          0x68

/* --- 0x69: FSHDEC — Fast Shading Deceleration --- */
#define REG_FSHDEC           0x69

/* --- 0x6A: FMOVNO — Fast Move Number --- */
#define REG_FMOVNO           0x6A

/* --- 0x6B: MULTFILM/GPIO Mode --- */
#define REG_MULTFILM         0x6B

/* --- 0x6C: GPIO_0 — GPIO Data 0 --- */
#define REG_GPIO_0           0x6C

/* --- 0x6D: GPIO_1 — GPIO Data 1 --- */
#define REG_GPIO_1           0x6D

/* --- 0x6E: GPIO_EN0 — GPIO Enable 0 --- */
#define REG_GPIO_EN0         0x6E

/* --- 0x6F: GPIO_EN1 — GPIO Enable 1 --- */
#define REG_GPIO_EN1         0x6F

/* --- 0x70-0x71: RSH/RSL — CCD Reset High/Low --- */
#define REG_RSH              0x70
#define REG_RSL              0x71

/* --- 0x72-0x73: CPH/CPL — CCD Clock Phase High/Low --- */
#define REG_CPH              0x72
#define REG_CPL              0x73

/* --- 0x74-0x75: CK1MAP[15:0] — CCD Clock 1 Map --- */
#define REG_CK1MAP_H         0x74
#define REG_CK1MAP_LL        0x75

/* --- 0x76-0x77: CK3MAP[15:0] — CCD Clock 3 Map --- */
#define REG_CK3MAP_H         0x76
#define REG_CK3MAP_L         0x77

/* --- 0x78-0x79: CK4MAP[15:0] — CCD Clock 4 Map --- */
#define REG_CK4MAP_H         0x78
#define REG_CK4MAP_L         0x79

/* --- 0x7A-0x7C: CCD Clock Polarity and Delay --- */
#define REG_7A               0x7A
#define REG_7B               0x7B
#define REG_7C               0x7C

/* --- 0x7D-0x7F: EXPOSURE — line integration time (measured) ---
 * Other Genesys ASICs use 0x7D as a gamma/SDRAM address (GMMADDR) and
 * 0x7E-0x7F as an init line period; on the GL128 these three together are
 * the integration time, which is also the line period. Changing them
 * scales brightness and lines-per-inch by the same factor -- see
 * PLUSTEK8200I_BASE_EXPOSURE. The old names are kept as aliases because
 * the init tables still refer to them. */
#define REG_EXPOSURE_H       0x7D
#define REG_EXPOSURE_M       0x7E
#define REG_EXPOSURE_L       0x7F
#define REG_GMMADDR          REG_EXPOSURE_H
#define REG_LINEPERIOD_H     REG_EXPOSURE_M
#define REG_LINEPERIOD_L     REG_EXPOSURE_L

/* --- 0x80: VRHOME/VRMOVE/VRBACK/VRSCAN — Motor Voltages --- */
#define REG_VRMOT            0x80

/* --- 0x81-0x82: ROFFSET — Red AFE Offset [8:0] --- */
#define REG_ROFFSET_H        0x81
#define REG_ROFFSET_L        0x82

/* --- 0x83-0x84: GOFFSET — Green AFE Offset [8:0] --- */
#define REG_GOFFSET_H        0x83
#define REG_GOFFSET_L        0x84

/* --- 0x85-0x86: BOFFSET — Blue AFE Offset [8:0] --- */
#define REG_BOFFSET_H        0x85
#define REG_BOFFSET_L        0x86

/* --- 0x87: ACYCNRLC / ENOFFSET / LEDADD / CK4ADC / AUTOCONF --- */
#define REG_ACYCNRLC         0x87
#define ACYCNRLC_ENOFFSET    0x01   /* Bit 0: Enable offset */
#define ACYCNRLC_AUTOCONF    0x04   /* Bit 2: Auto configuration */
#define ACYCNRLC_LEDADD      0x10   /* Bit 4: LED additive mode */
#define ACYCNRLC_CK4ADC      0x20   /* Bit 5: CK4 ADC */

/* --- 0x8A: IRQ Status / RS232 --- */
#define REG_IRQSTS           0x8A

/* --- 0x8B-0x8F: ROM/EEPROM Access --- */
#define REG_ROMCNT           0x8B
#define REG_ROMDAT           0x8C

/* --- 0x93: ADF Timing --- */
#define REG_93               0x93

/* --- 0x94-0x96: Lamp PWM --- */
#define REG_LAMPPWM_H        0x94
#define REG_LAMPPWM_L        0x95
#define REG_96               0x96

/* --- 0x9D: SDRAM Timing --- */
#define REG_SDRAMTIM         0x9D

/* --- 0xA0: SRAMSEL — SRAM Select --- */
#define REG_SRAMSEL          0xA0

/* --- 0xA4-0xA5: Auto-Conf Line Offset --- */
#define REG_LNOFSET_H        0xA4
#define REG_LNOFSET_L        0xA5

/* --- 0xA6-0xA7: Z1MOD (alternate / extended) --- */
#define REG_Z1MOD2_H         0xA6
#define REG_Z1MOD2_L         0xA7

/* --- 0xA8-0xA9: Z2MOD (alternate / extended) --- */
#define REG_Z2MOD2_H         0xA8
#define REG_Z2MOD2_L         0xA9

/* --- 0xAA: Reserved --- */
#define REG_AA               0xAA

/* --- 0xAB: TB3TB1 — Motor Table 3 = Table 1 --- */
#define REG_TB3TB1           0xAB

/* --- 0xAC-0xAD: Motor Table Pointers --- */
#define REG_AC               0xAC
#define REG_AD               0xAD

/* --- 0xAE: FMOVNO — Fast Move Number (positioning) --- */
#define REG_AE               0xAE

/* --- 0xAF: FMOVDEC — Fast Move Deceleration --- */
#define REG_AF               0xAF

/* --- 0xB4-0xBF: CCD Timing Extended, Backtrack --- */
#define REG_B4               0xB4
#define REG_B8               0xB8
#define REG_B9               0xB9
#define REG_BA               0xBA
#define REG_BD               0xBD
#define REG_BE               0xBE
#define REG_BF               0xBF

/* --- 0xC0-0xC3: SPI Motor / Gamma Tables --- */
#define REG_C0               0xC0
#define REG_C1               0xC1
#define REG_C3               0xC3

/* --- 0xD0-0xD9: Shading Bank Addresses (SH0DWN–SH9DWN) --- */
#define REG_SHDWN_BASE       0xD0
#define REG_SH0DWN           0xD0
#define REG_SH1DWN           0xD1
#define REG_SH2DWN           0xD2
#define REG_SH3DWN           0xD3
#define REG_SH4DWN           0xD4
#define REG_SH5DWN           0xD5
#define REG_SH6DWN           0xD6
#define REG_SH7DWN           0xD7
#define REG_SH8DWN           0xD8
#define REG_SH9DWN           0xD9

/* --- 0xE0-0xF7: Image Buffer Addresses (per-channel odd/even) ---
 * Each channel has a start (DWN) and end (UP) address.
 * Addresses are 20-bit SDRAM word addresses.
 * Format: DWN = E0/E1 (high/low), UP = E2/E3 (high/low), etc.
 *
 * Image buffer address registers. */

/* R-Channel ODD buffer */
#define REG_R1DWN_H          0xE0   /* R odd start address [19:16] */
#define REG_R1DWN_L          0xE1   /* R odd start address [15:8] */
#define REG_R1UP_H           0xE2   /* R odd end address [19:16] */
#define REG_R1UP_L           0xE3   /* R odd end address [15:8] */

/* R-Channel EVEN buffer */
#define REG_R2DWN_H          0xE4
#define REG_R2DWN_L          0xE5
#define REG_R2UP_H           0xE6
#define REG_R2UP_L           0xE7

/* G-Channel ODD buffer */
#define REG_G1DWN_H          0xE8
#define REG_G1DWN_L          0xE9
#define REG_G1UP_H           0xEA
#define REG_G1UP_L           0xEB

/* G-Channel EVEN buffer */
#define REG_G2DWN_H          0xEC
#define REG_G2DWN_L          0xED
#define REG_G2UP_H           0xEE
#define REG_G2UP_L           0xEF

/* B-Channel ODD buffer */
#define REG_B1DWN_H          0xF0
#define REG_B1DWN_L          0xF1
#define REG_B1UP_H           0xF2
#define REG_B1UP_L           0xF3

/* B-Channel EVEN buffer */
#define REG_B2DWN_H          0xF4
#define REG_B2DWN_L          0xF5
#define REG_B2UP_H           0xF6
#define REG_B2UP_L           0xF7

/* --- 0xF8: MAXSEL/MINSEL — Buffer Bank Selection --- */
#define REG_BANKSEL          0xF8

/* --- 0xFF: Debug / Auto-pattern --- */
#define REG_FF               0xFF

/* ==========================================================================
 * Buffer status — backward compatibility alias
 * BUFEMPTY is at status reg 0x41 bit 6; captures show it read via USB
 * register 0x82 (1 byte).
 * ========================================================================== */
#define REG_BUFEMPTY         0x82
#define BUFEMPTY_BIT         0x40

/* ==========================================================================
 * Motor Table SDRAM Addresses
 *
 * Each table is up to 8K words (16KB). The AHB base for SDRAM is
 * 0x10000000.
 *
 * Tables 1-3: Used during scanning (FASTFED=0)
 * Tables 4-5: Used during fast feed/positioning (FASTFED=1)
 * ========================================================================== */
#define MOTOR_TABLE1_ADDR    0x10000000   /* Table 1: 0x10000000-0x10003FFF */
#define MOTOR_TABLE2_ADDR    0x10004000   /* Table 2: 0x10004000-0x10007FFF */
#define MOTOR_TABLE3_ADDR    0x10008000   /* Table 3: 0x10008000-0x1000BFFF */
#define MOTOR_TABLE4_ADDR    0x1000C000   /* Table 4: 0x1000C000-0x1000FFFF */
#define MOTOR_TABLE5_ADDR    0x10010000   /* Table 5: 0x10010000-0x10013FFF */
#define MOTOR_TABLE_SIZE     0x4000       /* 16KB per table */

/* ==========================================================================
 * AHB Address Allocation
 * ========================================================================== */
#define AHB_APB_BRIDGE       0x00000000   /* On-chip registers */
#define AHB_SDRAM_BASE       0x10000000   /* SDRAM (128MB, 16-bit) */
#define AHB_SDRAM_END        0x17FFFFFF
#define AHB_TEST_MODULE      0x20000000   /* Test module */
#define AHB_TWO_CHIP         0x30000000   /* Two-chip extension */

/* ==========================================================================
 * Scanner Hardware Constants
 *
 * From the vendor's device configuration for 07b3:1825
 * ========================================================================== */
#define PLUSTEK8200I_HWDPI           7200   /* Hardware DPI (CCD native) */
#define PLUSTEK8200I_MOTORDPI        3600   /* Motor DPI (steps/inch) */
#define PLUSTEK8200I_UNITDPI         7200   /* Unit DPI of the vendor's configuration coordinates */
#define PLUSTEK8200I_LINESPACE       24     /* CCD line spacing (RGB offset) */
#define PLUSTEK8200I_SYSTEM_CLOCK    40     /* System clock in MHz (speed1-48) */

/* USB device IDs */
#define PLUSTEK8200I_VENDOR_ID       0x07B3
#define PLUSTEK8200I_PRODUCT_ID      0x1825

/* ==========================================================================
 * Function Declarations
 * ========================================================================== */

/* Register I/O functions */
SANE_Status plustek8200i_read_reg(int fd, uint16_t reg_addr, uint8_t *value);
SANE_Status plustek8200i_write_reg(int fd, uint8_t reg, uint8_t value);
void plustek8200i_regtrace_pairs(const uint8_t *pairs, size_t count);
void plustek8200i_regtrace_mark(const char *text);

/* Register initialization, from a USB capture */
SANE_Status plustek8200i_init_registers(int fd);

/* Post-WriteAllReg CCD timing adjustments */
SANE_Status plustek8200i_post_writeallreg(int fd);

/* AFE (Analog Front End) initialization - 8 hardcoded register blocks from a capture */
SANE_Status plustek8200i_write_afe_init(int fd);

/* Third-pass WriteAllReg (post-MOVE#2 re-initialization) */
SANE_Status plustek8200i_init_pass3_registers(int fd);

/*
 * Apply the CCD timing / clock / segment registers captured from the
 * Windows software, which are known to produce a clean bulk stream.
 * See the definition in plustek8200i-init.c.
 */
SANE_Status plustek8200i_apply_verified_ccd_regs(int fd);

/*
 * Pixels per line the sensor actually emits at DPISET=100. Measured on
 * the wire (line period 2592 samples = 864 px x 3 ch) in both the
 * Windows capture and ours. Matched to DPISET -- re-measure with
 * a USB stream decoder if either changes.
 */
#define PLUSTEK8200I_HW_LINE_PIXELS 864

/*
 * Vertical sampling rate, in dpi, at the current motor configuration.
 * The sensor samples ~2.5x more finely along the scan than across the
 * line, so the raw lines must be area-averaged down to square pixels.
 *
 * Fitted against the SilverFast reference of the same frame with the
 * width held fixed, i.e. a ONE-parameter search: best correlation
 * +0.7696 at a 2.484 ratio, so 600 * 2.484 = 1490.
 *
 * Two looser estimates said 2.605 (film rebate edges) and 2.611
 * (cross-correlating against the Windows scan). Both were 4-parameter
 * or threshold-based and both landed ~5% high, which showed up as a
 * frame aspect of 1.580 where the 1.44x1.00 inch frame wants 1.44.
 * Prefer the constrained fit; re-measure the same way if the motor
 * configuration or LPERIOD changes.
 */
#define PLUSTEK8200I_HW_LINE_DPI 1490

/* Motor table period that yields PLUSTEK8200I_HW_LINE_DPI. */
#define PLUSTEK8200I_BASE_PERIOD 1450

/*
 * Exposure. On this ASIC (GL128) registers 0x7D-0x7F hold the line
 * integration time, which is also the line period: measured at 600 dpi,
 * 14000 -> 17400 brightened every channel by x1.24-1.25 and cut the
 * lines per inch by exactly the same ratio. The scan table at AHB
 * 0x10000000 sets the carriage step period (x1.2 there stretched the
 * frame x1.20 with no change in brightness). Scaling BOTH by k changes
 * exposure by k and leaves the geometry alone; k > 1 only ever slows
 * the carriage. The per-channel exposure registers (0x10-0x15) do nothing.
 */
/* Largest USB read of image data (SCAN_READ_MAX_CHUNK in -dma.c). */
#define PLUSTEK8200I_STREAM_CHUNK  0xEFF0
#define PLUSTEK8200I_BASE_EXPOSURE 14000

/*
 * Delivered window, measured: dpi * 1.44 px wide (36.58 mm) and
 * 322/645/1291/2585/3877 rows at 300..3600 dpi, i.e. 27.3 mm tall --
 * the 24 mm frame plus the positioning margins. The scan-area options
 * address this window; the ranges stop slightly inside it.
 */
#define PLUSTEK8200I_WINDOW_W_MM 36.5
#define PLUSTEK8200I_WINDOW_H_MM 27.2
#define PLUSTEK8200I_EXPOSURE_MAX  4.0

/* Spare travel kept at each end of the frame, as a fraction of its
 * height, so neither edge is clipped by small positioning variation. */
#define PLUSTEK8200I_EDGE_MARGIN 0.07

/*
 * Vertical sampling actually used for a requested resolution. Never
 * below the default, so low resolutions keep the known-good motor
 * speed and are reached by downsampling; above it the motor is slowed
 * so the lines genuinely exist.
 */
static inline int plustek8200i_target_vdpi(int req_dpi)
{
    return (req_dpi > PLUSTEK8200I_HW_LINE_DPI)
         ? req_dpi : PLUSTEK8200I_HW_LINE_DPI;
}

/* Motor period for a target vertical sampling. Larger = slower. */
static inline int plustek8200i_vdpi_period(int target_vdpi)
{
    return (int)((long)PLUSTEK8200I_BASE_PERIOD * target_vdpi
                 / PLUSTEK8200I_HW_LINE_DPI);
}

/*
 * Nominal film frame the geometry is tuned for. The delivered window is
 * larger: the edge margins above are added at each end.
 */
#define PLUSTEK8200I_FRAME_W_MM 36.0
#define PLUSTEK8200I_FRAME_H_MM 24.0

/*
 * Full delivered frame for a requested resolution.
 *
 * sane_get_parameters() must answer before sane_start() has computed
 * anything, so it needs the SAME arithmetic the scan path uses, not an
 * approximation of it. Estimating the height from the advertised window
 * (27.2 mm) instead left the pre-scan line count 0.6% short -- one row
 * at 300 dpi but 45 rows at 7200 dpi, enough to truncate a frontend that
 * sizes its buffer from the pre-scan parameters.
 */
static inline int plustek8200i_raw_frame_lines(int req_dpi)
{
    int n = (int)(plustek8200i_target_vdpi(req_dpi)
                  * (PLUSTEK8200I_FRAME_H_MM / 25.4)
                  * (1.0 + 2.0 * PLUSTEK8200I_EDGE_MARGIN));
    return n < 1 ? 1 : n;
}

/* Those raw lines after the vertical squash: the rows actually handed out. */
static inline int plustek8200i_full_frame_lines(int req_dpi)
{
    int raw = plustek8200i_raw_frame_lines(req_dpi);
    double vsquash = (double)plustek8200i_target_vdpi(req_dpi) / (double)req_dpi;
    int n = (vsquash <= 1.01) ? raw : (int)(raw / vsquash);
    return n < 1 ? 1 : n;
}

/* Pixels per line of the full frame: measured at dpi * 1.44. */
static inline int plustek8200i_full_frame_pixels(int req_dpi)
{
    return (req_dpi * 36) / 25;
}

/* True when the infrared pass should be configured. Set from the scan
 * mode; PLUSTEK8200I_IR=1 forces it on for testing. */
int plustek8200i_use_infrared(void);
void plustek8200i_set_infrared(int on);

/* Scan-phase motor table loading (different from init table) */
SANE_Status plustek8200i_load_scan_motor_table(int fd, int period);

/* Scan-phase register configuration (CCD timing, buffer layout, AFE) */
SANE_Status plustek8200i_configure_scan_phase(int fd);

/* Set the geometry registers (DPISET, STRPIXEL, ENDPIXEL, MAXWD, LPERIOD, LINCNT, FEEDL) from the scan options */
SANE_Status plustek8200i_set_scan_parameters(int fd,
                                              int resolution,
                                              double tl_x_mm,
                                              double tl_y_mm,
                                              double br_x_mm,
                                              double br_y_mm,
                                              int color_mode,
                                              int bit_depth);

#endif /* PLUSTEK8200I_H */
