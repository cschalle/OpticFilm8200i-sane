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
 * SANE backend for the Plustek OpticFilm 8200i film scanner (GL128, 07b3:1825).
 *
 * The protocol was worked out from USB captures of the vendor software and
 * measurements on the hardware; no vendor driver code is used.
 */

#include <math.h>
#include "plustek8200i.h"
#include "plustek8200i-motor.h"
#include "plustek8200i-motor-tables.h"
#include "plustek8200i-dma.h"
#include "plustek8200i-scan.h"
#include "plustek8200i-shading.h"
#include "plustek8200i-feed.h"
#include "plustek8200i-button.h"
#include "plustek8200i-vresample.h"
#include "../include/sane/sanei_config.h"
#include <sys/time.h>
#include <limits.h>

#ifndef PLUSTEK8200I_CONFIG_FILE
#define PLUSTEK8200I_CONFIG_FILE "plustek8200i.conf"
#endif

/* Global variables */
static int num_devices = 0;
static plustek8200i_device_t *first_device = NULL;
static const SANE_Device **device_list = NULL;

/* String lists for SANE options */
/*
 * "Infrared" is the dust/scratch channel (the "i" in 8200i): IR LED on,
 * white lamp off, AFE at minimum gain.  Film dye is transparent to IR, so
 * only physical defects remain.  Output is single-channel.
 */
static SANE_String_Const mode_list[] = {
    "Color", "Gray", "Infrared", "Color+Infrared", NULL
};
/*
 * "Color+Infrared" delivers two frames: the colour image (SANE_FRAME_RGB,
 * last_frame FALSE), then on the next sane_start its pixel-aligned
 * infrared plane (SANE_FRAME_GRAY, last_frame TRUE), taken from an
 * infrared pass made first (the one --ir-clean uses).  SANE has no
 * standard 4-channel format, so this is the conforming way; scanimage
 * cannot save it, a libsane client can.  Internally the colour pass runs
 * as MODE_COLOR with dev->opt_rgbi set.
 */
#define MODE_STR_RGBI  3

#define MODE_COLOR     0
#define MODE_GRAY      1
#define MODE_INFRARED  2

/*
 * Supported resolutions.  Measured and exactly linear: horizontal sampling
 * is DPISET * 6 dpi, the line DPISET * 8.64 px (DPISET 100 -> 864 px ->
 * 600 dpi; 400 -> 3456 px -> 2400 dpi).
 *
 * 7200 dpi (DPISET 1200) gives 10368x7755 px in ~118 s.  An earlier failure
 * there was a 30 s read timeout expiring during the 57.5 s lead-in feed,
 * not a line-width limit.
 *
 * Above PLUSTEK8200I_HW_LINE_DPI (1490) the vertical sampling is set by
 * line rate against motor speed, not DPISET, so the higher entries are
 * vertically upsampled (see plustek8200i_target_vdpi()).
 *
 * 150 dpi is omitted: DPISET 25 gives 216 px, below the floor the scoring
 * tools assume, and the frontend can derive it from 300.
 */
static const SANE_Word resolution_list[] = {
    6,     /* number of entries */
    300, 600, 1200, 2400, 3600, 7200
};

static const SANE_Range exposure_range = {
    SANE_FIX(1.0), SANE_FIX(PLUSTEK8200I_EXPOSURE_MAX), SANE_FIX(0.05)
};

static const SANE_Word bit_depth_list[] = {
    2,     /* number of entries */
    8, 16
};

/* Scan area range in mm (SANE_FIX format)
 * Film frame: ~36mm x 24mm (35mm film)
 * Vendor PAGESIZE 10368x7200 at UNITDPI 7200 = 36.58 x 25.4 mm */
/*
 * Geometry ranges carry no quantisation step: SANE_FIX(0.1) is 0.09999, so
 * a 0.1 mm step can never land on the window maximum, and a frontend that
 * writes br-x back (scanimage does) lost the last pixel of every line.
 */
static const SANE_Range x_range = {
    SANE_FIX(0.0),
    SANE_FIX(PLUSTEK8200I_WINDOW_W_MM),
    0
};

static const SANE_Range y_range = {
    SANE_FIX(0.0),
    SANE_FIX(PLUSTEK8200I_WINDOW_H_MM),
    0
};

static int update_option_deps(plustek8200i_device_t *dev);
static void release_scan_resources(plustek8200i_device_t *dev);

/*
 * Initialize SANE option descriptors for a device
 */
static void
init_options(plustek8200i_device_t *dev)
{
    SANE_Option_Descriptor *od;

    /* Option 0: Number of options */
    od = &dev->opt[OPT_NUM_OPTS];
    od->name = SANE_NAME_NUM_OPTIONS;
    od->title = SANE_TITLE_NUM_OPTIONS;
    od->desc = SANE_DESC_NUM_OPTIONS;
    od->type = SANE_TYPE_INT;
    od->unit = SANE_UNIT_NONE;
    od->size = sizeof(SANE_Int);
    od->cap = SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    /* Group: Mode */
    od = &dev->opt[OPT_MODE_GROUP];
    od->name = "";
    od->title = "Scan Mode";
    od->desc = "";
    od->type = SANE_TYPE_GROUP;
    od->unit = SANE_UNIT_NONE;
    od->size = 0;
    od->cap = 0;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    /* Option: Color Mode */
    od = &dev->opt[OPT_MODE];
    od->name = SANE_NAME_SCAN_MODE;
    od->title = SANE_TITLE_SCAN_MODE;
    od->desc = "Selects the scan mode. Color+Infrared returns two frames, "
               "the colour image and then its infrared plane (gray); "
               "scanimage cannot save it (it aborts on the second frame).";
    od->type = SANE_TYPE_STRING;
    od->unit = SANE_UNIT_NONE;
    od->size = 32;
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_STRING_LIST;
    od->constraint.string_list = mode_list;

    /* Option: Bit Depth */
    od = &dev->opt[OPT_BIT_DEPTH];
    od->name = SANE_NAME_BIT_DEPTH;
    od->title = SANE_TITLE_BIT_DEPTH;
    od->desc = SANE_DESC_BIT_DEPTH;
    od->type = SANE_TYPE_INT;
    od->unit = SANE_UNIT_NONE;
    od->size = sizeof(SANE_Int);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_WORD_LIST;
    od->constraint.word_list = bit_depth_list;

    /* Option: Resolution */
    od = &dev->opt[OPT_RESOLUTION];
    od->name = SANE_NAME_SCAN_RESOLUTION;
    od->title = SANE_TITLE_SCAN_RESOLUTION;
    od->desc = SANE_DESC_SCAN_RESOLUTION;
    od->type = SANE_TYPE_INT;
    od->unit = SANE_UNIT_DPI;
    od->size = sizeof(SANE_Int);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_WORD_LIST;
    od->constraint.word_list = resolution_list;

    /* Group: Geometry */
    od = &dev->opt[OPT_IR_CLEAN];
    od->name = "ir-clean";
    od->title = SANE_I18N("Infrared dust removal");
    od->desc = SANE_I18N("Make a second pass with infrared illumination "
                         "and repair dust and scratches in the colour "
                         "image. Doubles the scan time.");
    od->type = SANE_TYPE_BOOL;
    od->unit = SANE_UNIT_NONE;
    od->size = sizeof(SANE_Bool);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    od = &dev->opt[OPT_AUTO_WB];
    od->name = "auto-wb";
    od->title = SANE_I18N("Automatic white balance");
    od->desc = SANE_I18N("Scale the colour channels so their means match. "
                         "Off delivers the sensor's calibrated response "
                         "unchanged, which negative conversion and colour "
                         "profiling need: the balance otherwise varies with "
                         "picture content and clips highlights.");
    od->type = SANE_TYPE_BOOL;
    od->unit = SANE_UNIT_NONE;
    od->size = sizeof(SANE_Bool);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT | SANE_CAP_ADVANCED;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    od = &dev->opt[OPT_EXPOSURE];
    od->name = "exposure";
    od->title = SANE_I18N("Exposure");
    od->desc = SANE_I18N("Light integration time as a multiple of the "
                         "default. Raises every channel by the same factor; "
                         "the carriage slows down to keep the geometry, so "
                         "the scan takes proportionally longer. For dense "
                         "negatives and dark slides.");
    od->type = SANE_TYPE_FIXED;
    od->unit = SANE_UNIT_NONE;
    od->size = sizeof(SANE_Fixed);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_RANGE;
    od->constraint.range = &exposure_range;

    od = &dev->opt[OPT_GEOMETRY_GROUP];
    od->name = "";
    od->title = "Geometry";
    od->desc = "";
    od->type = SANE_TYPE_GROUP;
    od->unit = SANE_UNIT_NONE;
    od->size = 0;
    od->cap = 0;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    /* Option: Top-left X */
    od = &dev->opt[OPT_TL_X];
    od->name = SANE_NAME_SCAN_TL_X;
    od->title = SANE_TITLE_SCAN_TL_X;
    od->desc = SANE_DESC_SCAN_TL_X;
    od->type = SANE_TYPE_FIXED;
    od->unit = SANE_UNIT_MM;
    od->size = sizeof(SANE_Fixed);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_RANGE;
    od->constraint.range = &x_range;

    /* Option: Top-left Y */
    od = &dev->opt[OPT_TL_Y];
    od->name = SANE_NAME_SCAN_TL_Y;
    od->title = SANE_TITLE_SCAN_TL_Y;
    od->desc = SANE_DESC_SCAN_TL_Y;
    od->type = SANE_TYPE_FIXED;
    od->unit = SANE_UNIT_MM;
    od->size = sizeof(SANE_Fixed);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_RANGE;
    od->constraint.range = &y_range;

    /* Option: Bottom-right X */
    od = &dev->opt[OPT_BR_X];
    od->name = SANE_NAME_SCAN_BR_X;
    od->title = SANE_TITLE_SCAN_BR_X;
    od->desc = SANE_DESC_SCAN_BR_X;
    od->type = SANE_TYPE_FIXED;
    od->unit = SANE_UNIT_MM;
    od->size = sizeof(SANE_Fixed);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_RANGE;
    od->constraint.range = &x_range;

    /* Option: Bottom-right Y */
    od = &dev->opt[OPT_BR_Y];
    od->name = SANE_NAME_SCAN_BR_Y;
    od->title = SANE_TITLE_SCAN_BR_Y;
    od->desc = SANE_DESC_SCAN_BR_Y;
    od->type = SANE_TYPE_FIXED;
    od->unit = SANE_UNIT_MM;
    od->size = sizeof(SANE_Fixed);
    od->cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    od->constraint_type = SANE_CONSTRAINT_RANGE;
    od->constraint.range = &y_range;

    /* Group: Sensors */
    od = &dev->opt[OPT_SENSOR_GROUP];
    od->name = SANE_NAME_SENSORS;
    od->title = SANE_TITLE_SENSORS;
    od->desc = SANE_DESC_SENSORS;
    od->type = SANE_TYPE_GROUP;
    od->unit = SANE_UNIT_NONE;
    od->size = 0;
    od->cap = SANE_CAP_ADVANCED;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    /*
     * The two front-panel buttons.  SANE_CAP_HARD_SELECT says the value is
     * chosen at the hardware, SANE_CAP_SOFT_DETECT that it can be read --
     * the combination every button-watching frontend (scanbd and friends)
     * looks for.  Reading one reports a press once and consumes it; see
     * plustek8200i-button.c.
     *
     * QuickScan takes the standard "scan" name because it is this panel's
     * scan button: that is what a generic button daemon will look for.
     * IntelliScan has no standard name, and inventing a meaning for one of
     * the reserved names ("email", "copy") would be worse than its own.
     */
    od = &dev->opt[OPT_SCAN_SW];
    od->name = SANE_NAME_SCAN;
    od->title = SANE_I18N("QuickScan button");
    od->desc = SANE_I18N("The QuickScan button on the front of the scanner "
                         "was pressed. Reading this reports a press once.");
    od->type = SANE_TYPE_BOOL;
    od->unit = SANE_UNIT_NONE;
    od->size = sizeof(SANE_Bool);
    od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    od = &dev->opt[OPT_INTELLISCAN_SW];
    od->name = "intelliscan";
    od->title = SANE_I18N("IntelliScan button");
    od->desc = SANE_I18N("The IntelliScan button on the front of the scanner "
                         "was pressed. Reading this reports a press once.");
    od->type = SANE_TYPE_BOOL;
    od->unit = SANE_UNIT_NONE;
    od->size = sizeof(SANE_Bool);
    od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_HARD_SELECT | SANE_CAP_ADVANCED;
    od->constraint_type = SANE_CONSTRAINT_NONE;

    /* Set defaults */
    dev->opt_resolution = 3600;
    dev->opt_bit_depth = 16;
    dev->opt_mode = 0;       /* Color */
    dev->opt_ir_clean = SANE_FALSE;
    dev->opt_auto_wb = SANE_FALSE;
    dev->opt_exposure = SANE_FIX(1.0);
    dev->in_ir_pass = 0;
    dev->rgbi_ir_next = 0;
    dev->s_eof = 0;
    dev->ir_map = NULL;
    dev->ir_map_size = 0;
    dev->btn_latch = 0;
    dev->btn_last_poll = 0.0;
    dev->opt_tl_x = SANE_FIX(0.0);
    dev->opt_tl_y = SANE_FIX(0.0);
    dev->opt_br_x = SANE_FIX(PLUSTEK8200I_WINDOW_W_MM);
    dev->opt_br_y = SANE_FIX(PLUSTEK8200I_WINDOW_H_MM);

    update_option_deps(dev);
}

/*
 * Mark --ir-clean and --auto-wb inactive in "Infrared" mode (no dye image,
 * one channel) so a GUI greys them out.  Returns non-zero if any
 * descriptor changed, so the caller can raise SANE_INFO_RELOAD_OPTIONS.
 */
static int
update_option_deps(plustek8200i_device_t *dev)
{
    SANE_Word before_ir = dev->opt[OPT_IR_CLEAN].cap;
    SANE_Word before_wb = dev->opt[OPT_AUTO_WB].cap;

    if (dev->opt_mode == MODE_INFRARED) {
        dev->opt[OPT_IR_CLEAN].cap |= SANE_CAP_INACTIVE;
        dev->opt[OPT_AUTO_WB].cap  |= SANE_CAP_INACTIVE;
    } else {
        dev->opt[OPT_IR_CLEAN].cap &= ~SANE_CAP_INACTIVE;
        dev->opt[OPT_AUTO_WB].cap  &= ~SANE_CAP_INACTIVE;
    }

    return dev->opt[OPT_IR_CLEAN].cap != before_ir
        || dev->opt[OPT_AUTO_WB].cap  != before_wb;
}

/* USB device matching callback */
static SANE_Status
usb_attach_callback(SANE_String_Const devname)
{
    plustek8200i_device_t *dev;

    DBG(DBG_info, "usb_attach_callback: found device %s\n", devname);

    /* Check if already in list */
    for (dev = first_device; dev; dev = dev->next) {
        if (strcmp(dev->sane.name, devname) == 0)
            return SANE_STATUS_GOOD;  /* Already added */
    }

    /* Add new device */
    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return SANE_STATUS_NO_MEM;

    dev->sane.name = strdup(devname);
    dev->sane.vendor = "Plustek";
    dev->sane.model = "OpticFilm 8200i";
    dev->sane.type = "film scanner";
    dev->usb_fd = -1;

    dev->next = first_device;
    first_device = dev;
    num_devices++;

    return SANE_STATUS_GOOD;
}

/*
 * SANE API: sane_init
 */
SANE_Status
sane_init(SANE_Int *version_code, SANE_Auth_Callback authorize)
{
    FILE *fp;

    (void)authorize;  /* Unused */

    DBG_INIT();
    DBG(DBG_info, "sane_init: Plustek 8200i backend\n");

    if (version_code)
        *version_code = SANE_VERSION_CODE(SANE_CURRENT_MAJOR, V_MINOR, 0);

    /* Initialize USB subsystem */
    sanei_usb_init();

    /*
     * Read plustek8200i.conf so that a `usb VID PID' line (for a relabelled
     * unit or new product ID) takes effect; sanei_usb_attach_matching_devices
     * understands both names and `usb VID PID'.
     */
    fp = sanei_config_open(PLUSTEK8200I_CONFIG_FILE);
    if (fp) {
        SANE_Char line[PATH_MAX];

        while (sanei_config_read(line, sizeof(line), fp)) {
            char *p = line;

            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '#' || *p == '\0')
                continue;
            if (strncmp(p, "option", 6) == 0) {
                DBG(DBG_info, "sane_init: %s: this backend implements no "
                    "options, ignoring \"%s\"\n",
                    PLUSTEK8200I_CONFIG_FILE, p);
                continue;
            }
            DBG(DBG_info, "sane_init: config: \"%s\"\n", p);
            sanei_usb_attach_matching_devices(p, usb_attach_callback);
        }
        fclose(fp);
    } else {
        DBG(DBG_info, "sane_init: no %s found, probing for the built-in "
            "device ID only\n", PLUSTEK8200I_CONFIG_FILE);
    }

    /*
     * Probe for the ID we actually support regardless, so the scanner is
     * found whether or not a config file is installed.
     * usb_attach_callback de-duplicates by device name.
     */
    sanei_usb_find_devices(0x07b3, 0x1825, usb_attach_callback);

    return SANE_STATUS_GOOD;
}

/*
 * SANE API: sane_exit
 */
void
sane_exit(void)
{
    plustek8200i_device_t *dev, *next;

    DBG(DBG_info, "sane_exit\n");

    for (dev = first_device; dev; dev = next) {
        next = dev->next;
        /* A frontend is permitted to call sane_exit without closing each
         * handle first. Freeing the device while its fd and scan buffers
         * are still live would leak both for the life of the process --
         * which for saned is the life of the daemon. */
        if (dev->usb_fd >= 0)
            sane_close(dev);
        free((void *)dev->sane.name);
        free(dev);
    }

    if (device_list)
        free(device_list);

    first_device = NULL;
    device_list = NULL;
    num_devices = 0;
}

/*
 * SANE API: sane_get_devices
 */
SANE_Status
sane_get_devices(const SANE_Device ***device_list_ptr, SANE_Bool local_only)
{
    plustek8200i_device_t *dev;
    int i;

    (void)local_only;  /* Unused */

    DBG(DBG_info, "sane_get_devices: %d devices\n", num_devices);

    /* Build device list array */
    if (device_list) {
        free(device_list);
        device_list = NULL;
    }

    device_list = malloc((num_devices + 1) * sizeof(device_list[0]));
    if (!device_list)
        return SANE_STATUS_NO_MEM;

    i = 0;
    for (dev = first_device; dev; dev = dev->next) {
        DBG(DBG_info, "sane_get_devices: adding device '%s'\n", dev->sane.name);
        device_list[i++] = &dev->sane;
    }
    device_list[i] = NULL;

    *device_list_ptr = device_list;

    return SANE_STATUS_GOOD;
}

/*
 * SANE API: sane_open
 */
SANE_Status
sane_open(SANE_String_Const devicename, SANE_Handle *handle)
{
    plustek8200i_device_t *dev;
    SANE_Status status;

    DBG(DBG_info, "sane_open: %s\n", devicename ? devicename : "(null)");

    /*
     * An empty or NULL name, or this backend's own name (what a directly
     * linked frontend tends to pass), means the default device.  Any other
     * name must match exactly: falling back to the first device would hand a
     * frontend a different scanner than it asked for.
     */
    if (!devicename || devicename[0] == '\0'
        || strcmp(devicename, STRINGIFY(BACKEND_NAME)) == 0) {
        dev = first_device;
    } else {
        for (dev = first_device; dev; dev = dev->next)
            if (strcmp(dev->sane.name, devicename) == 0)
                break;
    }

    if (!dev) {
        DBG(DBG_error, "sane_open: no such device '%s'\n",
            devicename ? devicename : "(null)");
        return SANE_STATUS_INVAL;
    }

    /*
     * The handle is the device struct, so a second open would alias the first
     * (shared usb_fd, options and buffers) and leak its fd.  Refuse instead.
     */
    if (dev->usb_fd >= 0) {
        DBG(DBG_info, "sane_open: device '%s' is already open\n",
            dev->sane.name);
        return SANE_STATUS_DEVICE_BUSY;
    }

    DBG(DBG_info, "sane_open: opening device '%s'\n", dev->sane.name);

    /* Open USB device using the device name from enumeration */
    status = sanei_usb_open(dev->sane.name, &dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "sane_open: USB open failed: %s\n", sane_strstatus(status));
        return status;
    }

    DBG(DBG_info, "sane_open: USB device opened, fd=%d\n", dev->usb_fd);

    /* Endpoints: 0x81 bulk IN (scan data), 0x02 bulk OUT (tables), 0x83 interrupt IN */
    sanei_usb_set_endpoint(dev->usb_fd,
                           USB_DIR_IN | USB_ENDPOINT_TYPE_BULK, 0x81);
    sanei_usb_set_endpoint(dev->usb_fd,
                           USB_DIR_OUT | USB_ENDPOINT_TYPE_BULK, 0x02);
    sanei_usb_set_endpoint(dev->usb_fd,
                           USB_DIR_IN | USB_ENDPOINT_TYPE_INTERRUPT, 0x83);
    DBG(DBG_info, "sane_open: endpoints configured "
                  "(bulk IN=0x81, bulk OUT=0x02, interrupt IN=0x83)\n");

    /* Initialize SANE options with defaults */
    init_options(dev);

    /*
     * The scanner holds an unread button press indefinitely, so whatever is
     * queued now dates from before this session and means nothing to the
     * frontend that just opened us.  Drop it.
     */
    plustek8200i_button_discard(dev);

    *handle = dev;
    return SANE_STATUS_GOOD;
}

/*
 * SANE API: sane_close
 */
void
sane_close(SANE_Handle handle)
{
    plustek8200i_device_t *dev = handle;

    DBG(DBG_info, "sane_close\n");

    if (!dev)
        return;

    /*
     * A frontend may close mid-scan.  Stop via sane_cancel, which knows not to
     * cut motor power while the carriage is homing, then free every buffer
     * the pass owns (hundreds of MB at 7200 dpi, accumulating in saned).
     */
    if (dev->scanning)
        sane_cancel(dev);

    release_scan_resources(dev);

    if (dev->usb_fd >= 0) {
        sanei_usb_close(dev->usb_fd);
        dev->usb_fd = -1;
    }
}

/*
 * SANE API: sane_get_option_descriptor
 */
const SANE_Option_Descriptor *
sane_get_option_descriptor(SANE_Handle handle, SANE_Int option)
{
    plustek8200i_device_t *dev = handle;

    if (option < 0 || option >= NUM_OPTIONS)
        return NULL;

    return &dev->opt[option];
}

/*
 * SANE API: sane_control_option
 */
SANE_Status
sane_control_option(SANE_Handle handle, SANE_Int option,
                   SANE_Action action, void *value, SANE_Int *info)
{
    plustek8200i_device_t *dev = handle;

    if (info)
        *info = 0;

    if (option < 0 || option >= NUM_OPTIONS)
        return SANE_STATUS_INVAL;

    /* Groups have no value */
    if (dev->opt[option].type == SANE_TYPE_GROUP)
        return SANE_STATUS_INVAL;

    if (action == SANE_ACTION_GET_VALUE) {
        switch (option) {
        case OPT_NUM_OPTS:
            *(SANE_Int *)value = NUM_OPTIONS;
            return SANE_STATUS_GOOD;
        case OPT_MODE:
            strcpy(value, dev->opt_rgbi ? mode_list[MODE_STR_RGBI]
                                        : mode_list[dev->opt_mode]);
            return SANE_STATUS_GOOD;
        case OPT_IR_CLEAN:
            *(SANE_Bool *)value = dev->opt_ir_clean;
            return SANE_STATUS_GOOD;
        case OPT_AUTO_WB:
            *(SANE_Bool *)value = dev->opt_auto_wb;
            return SANE_STATUS_GOOD;
        case OPT_EXPOSURE:
            *(SANE_Fixed *)value = dev->opt_exposure;
            return SANE_STATUS_GOOD;

        case OPT_BIT_DEPTH:
            *(SANE_Int *)value = dev->opt_bit_depth;
            return SANE_STATUS_GOOD;
        case OPT_RESOLUTION:
            *(SANE_Int *)value = dev->opt_resolution;
            return SANE_STATUS_GOOD;
        case OPT_TL_X:
            *(SANE_Fixed *)value = dev->opt_tl_x;
            return SANE_STATUS_GOOD;
        case OPT_TL_Y:
            *(SANE_Fixed *)value = dev->opt_tl_y;
            return SANE_STATUS_GOOD;
        case OPT_BR_X:
            *(SANE_Fixed *)value = dev->opt_br_x;
            return SANE_STATUS_GOOD;
        case OPT_BR_Y:
            *(SANE_Fixed *)value = dev->opt_br_y;
            return SANE_STATUS_GOOD;

        /*
         * Buttons.  One poll serves whichever button is read first and
         * fills the latch for both, so a frontend reading the pair in a row
         * pays for a single endpoint read.
         */
        case OPT_SCAN_SW:
        case OPT_INTELLISCAN_SW:
            plustek8200i_button_poll(dev);
            *(SANE_Bool *)value =
                plustek8200i_button_take(&dev->btn_latch,
                                         option == OPT_SCAN_SW
                                           ? PLUSTEK8200I_BTN_QUICKSCAN
                                           : PLUSTEK8200I_BTN_INTELLISCAN)
                ? SANE_TRUE : SANE_FALSE;
            return SANE_STATUS_GOOD;
        default:
            return SANE_STATUS_INVAL;
        }
    }

    if (action == SANE_ACTION_SET_VALUE) {
        SANE_Status cst;

        if (dev->scanning)
            return SANE_STATUS_DEVICE_BUSY;
        if (!value)
            return SANE_STATUS_INVAL;
        if (!SANE_OPTION_IS_SETTABLE(dev->opt[option].cap))
            return SANE_STATUS_INVAL;

        /*
         * Enforce the advertised constraints: clamp to the range, snap to the
         * quantisation or word list, validate against the string list, and raise
         * SANE_INFO_INEXACT when the value moved.  Otherwise e.g. --depth 12 or
         * --resolution 5000 reach the geometry maths and give an unparsable frame.
         */
        cst = sanei_constrain_value(&dev->opt[option], value, info);
        if (cst != SANE_STATUS_GOOD)
            return cst;

        switch (option) {
        case OPT_MODE:
            dev->opt_rgbi = 0;
            if (strcmp(value, "Color") == 0)
                dev->opt_mode = MODE_COLOR;
            else if (strcmp(value, mode_list[MODE_STR_RGBI]) == 0) {
                dev->opt_mode = MODE_COLOR;
                dev->opt_rgbi = 1;
            }
            else if (strcmp(value, "Gray") == 0)
                dev->opt_mode = MODE_GRAY;
            else if (strcmp(value, "Infrared") == 0)
                dev->opt_mode = MODE_INFRARED;
            else
                return SANE_STATUS_INVAL;
            if (info) {
                *info |= SANE_INFO_RELOAD_PARAMS;
                if (update_option_deps(dev))
                    *info |= SANE_INFO_RELOAD_OPTIONS;
            } else {
                update_option_deps(dev);
            }
            return SANE_STATUS_GOOD;


        case OPT_BIT_DEPTH:
            dev->opt_bit_depth = *(SANE_Int *)value;
            if (info)
                *info |= SANE_INFO_RELOAD_PARAMS;
            return SANE_STATUS_GOOD;

        case OPT_IR_CLEAN:
            dev->opt_ir_clean = *(SANE_Bool *)value;
            DBG(DBG_info, "ir-clean set to %d\n", dev->opt_ir_clean);
            return SANE_STATUS_GOOD;

        case OPT_AUTO_WB:
            dev->opt_auto_wb = *(SANE_Bool *)value;
            return SANE_STATUS_GOOD;

        case OPT_EXPOSURE: {
            SANE_Fixed v = *(SANE_Fixed *)value;
            if (v < exposure_range.min || v > exposure_range.max)
                return SANE_STATUS_INVAL;
            dev->opt_exposure = v;
            return SANE_STATUS_GOOD;
        }

        case OPT_RESOLUTION:
            dev->opt_resolution = *(SANE_Int *)value;
            if (info)
                *info |= SANE_INFO_RELOAD_PARAMS;
            return SANE_STATUS_GOOD;

        case OPT_TL_X:
            dev->opt_tl_x = *(SANE_Fixed *)value;
            if (info)
                *info |= SANE_INFO_RELOAD_PARAMS;
            return SANE_STATUS_GOOD;
        case OPT_TL_Y:
            dev->opt_tl_y = *(SANE_Fixed *)value;
            if (info)
                *info |= SANE_INFO_RELOAD_PARAMS;
            return SANE_STATUS_GOOD;
        case OPT_BR_X:
            dev->opt_br_x = *(SANE_Fixed *)value;
            if (info)
                *info |= SANE_INFO_RELOAD_PARAMS;
            return SANE_STATUS_GOOD;
        case OPT_BR_Y:
            dev->opt_br_y = *(SANE_Fixed *)value;
            if (info)
                *info |= SANE_INFO_RELOAD_PARAMS;
            return SANE_STATUS_GOOD;

        default:
            return SANE_STATUS_INVAL;
        }
    }

    return SANE_STATUS_UNSUPPORTED;
}

/*
 * SANE API: sane_get_parameters
 *
 * Computes image dimensions from the current scan options:
 *   pixels = (scan_width_mm / 25.4) * resolution
 *   lines  = (scan_height_mm / 25.4) * resolution
 */
/*
 * Requested scan area in output pixels of a full window fw x fh.
 * Always at least one pixel, always inside the window.
 */
static void
plustek8200i_crop_rect(plustek8200i_device_t *dev, int fw, int fh,
                       int *x0, int *y0, int *x1, int *y1)
{
    double k = dev->opt_resolution / 25.4;
    int a = (int)(SANE_UNFIX(dev->opt_tl_x) * k);
    int b = (int)(SANE_UNFIX(dev->opt_tl_y) * k);
    int e = (int)(SANE_UNFIX(dev->opt_br_x) * k + 0.5);
    int f = (int)(SANE_UNFIX(dev->opt_br_y) * k + 0.5);

    /* br at the range maximum means "to the edge", whatever the exact
     * window size at this resolution. */
    if (dev->opt_br_x >= SANE_FIX(PLUSTEK8200I_WINDOW_W_MM)) e = fw;
    if (dev->opt_br_y >= SANE_FIX(PLUSTEK8200I_WINDOW_H_MM)) f = fh;
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (e > fw) e = fw;
    if (f > fh) f = fh;
    if (a > fw - 1) a = fw - 1;
    if (b > fh - 1) b = fh - 1;
    if (e <= a) e = a + 1;
    if (f <= b) f = b + 1;
    *x0 = a; *y0 = b; *x1 = e; *y1 = f;
}

/*
 * Color+Infrared delivers the image frame first and the GRAY infrared frame
 * second, each from its own sane_start and each streamed through sane_read,
 * so a frontend gets data and progress for both: the colour pass runs first
 * and the infrared pass follows.  The older order (an infrared pre-pass
 * inside the first sane_start, which reports nothing until it is done, then
 * the colour pass, then the stashed plane) stays for --ir-clean, whose repair
 * needs the infrared plane before the colour data, and for
 * PLUSTEK8200I_IR_FIRST in the environment.
 */
static int
rgbi_image_first(const plustek8200i_device_t *dev)
{
    return dev->opt_rgbi && !dev->opt_ir_clean && !getenv("PLUSTEK8200I_IR_FIRST");
}

/* Give up on the infrared frame of a Color+Infrared image: the next sane_start
 * begins a new image. */
static void
rgbi_abandon(plustek8200i_device_t *dev)
{
    if (dev->opt_rgbi && dev->rgbi_ir_next)
        dev->opt_mode = MODE_COLOR;
    dev->rgbi_ir_next = 0;
}

SANE_Status
sane_get_parameters(SANE_Handle handle, SANE_Parameters *params)
{
    plustek8200i_device_t *dev = handle;
    int channels, bytes_per_sample;

    DBG(DBG_info, "sane_get_parameters\n");

    if (!params)
        return SANE_STATUS_INVAL;

    /* If scanning is active, return params set by sane_start (which uses
     * the actual hardware DPISET, not opt_resolution). Recomputing here
     * would overwrite the correct values with wrong dimensions. */
    if (dev->scanning) {
        *params = dev->params;
        DBG(DBG_info, "Image (active scan): %d x %d pixels, %d bytes/line, depth=%d\n",
            dev->params.pixels_per_line, dev->params.lines,
            dev->params.bytes_per_line, dev->params.depth);
        return SANE_STATUS_GOOD;
    }

    /* Estimate from the measured full window, then the crop. Exact
     * values are set by sane_start once the scan has been made. */
    {
        int x0, y0, x1, y1;
        int fw = plustek8200i_full_frame_pixels(dev->opt_resolution);
        int fh = plustek8200i_full_frame_lines(dev->opt_resolution);
        plustek8200i_crop_rect(dev, fw, fh, &x0, &y0, &x1, &y1);
        dev->params.pixels_per_line = x1 - x0;
        dev->params.lines = y1 - y0;
    }

    /* Color mode determines the delivered format. The hardware always
     * reads three channels; Gray and Infrared are averaged afterwards. */
    {
        int ir_frame = rgbi_image_first(dev) && dev->rgbi_ir_next;

        if (dev->opt_mode == MODE_COLOR && !ir_frame) {
            dev->params.format = SANE_FRAME_RGB;
            channels = 3;
        } else {
            dev->params.format = SANE_FRAME_GRAY;
            channels = 1;
        }
        dev->params.last_frame = (dev->opt_rgbi && !ir_frame) ? SANE_FALSE : SANE_TRUE;
    }
    dev->params.depth = dev->opt_bit_depth;

    bytes_per_sample = (dev->opt_bit_depth == 16) ? 2 : 1;
    dev->params.bytes_per_line = dev->params.pixels_per_line * channels * bytes_per_sample;

    *params = dev->params;

    DBG(DBG_info, "Image: %d x %d pixels, %d bytes/line, depth=%d, %s\n",
        dev->params.pixels_per_line, dev->params.lines,
        dev->params.bytes_per_line, dev->params.depth,
        dev->opt_rgbi ? mode_list[MODE_STR_RGBI] : mode_list[dev->opt_mode]);

    return SANE_STATUS_GOOD;
}

/*
 * SANE API: sane_start
 *
 * sane_cancel may be called from another thread or a signal handler;
 * cancel_requested is checked between bulk reads, never with a transfer
 * outstanding (that wedges the USB link on this ASIC family).
 */
/*
 * Infrared defect detection on dev->ir_map (`lines` rows of scan_pixels
 * 16-bit samples): flat-fields it, then overwrites it with a mark map
 * (0 = defect, 65535 = clean) and sets the interior bounds
 * dev->ir_r0..ir_r1 / ir_c0..ir_c1.  Returns 0 if repair can run.  Split
 * from the repair so a streamed scan can prepare up front and repair line
 * by line.
 */
static int
plustek8200i_ir_prepare(plustek8200i_device_t *dev, int lines)
{
    int px = dev->scan_pixels;
    int y, x;

    dev->ir_ready = 0;
    dev->ir_marked = dev->ir_repaired = 0;
    /*
     * Detect defects on the frame interior only: the dark border (~13% of the
     * image, near zero) defeats any global threshold, which marked 13-14%
     * whatever the multiplier, against 0.7% marked and 0.09% repaired (k=5)
     * on the interior, matching the independent count of ~0.1%.  Median and
     * MAD rather than mean and sd, so a few opaque specks do not move the
     * baseline.
     */
    uint16_t *ir = (uint16_t *)dev->ir_map;
    static uint32_t hist[65536];
    int r0 = 0, r1 = lines, c0 = 0, c1 = px;
    size_t acc, i, n;
    double median, mad, thr;
    uint32_t gmed;

    /* Rough global median, to locate the lit area. */
    memset(hist, 0, sizeof(hist));
    for (i = 0; i < (size_t)px * lines; i++) hist[ir[i]]++;
    acc = 0;
    for (i = 0; i < 65536; i++) {
        acc += hist[i];
        if (acc >= (size_t)px * lines / 2) break;
    }
    gmed = (uint32_t)i;

    /* Frame interior: rows and columns mostly above half that. */
    {
        uint32_t lim = gmed / 2;
        int yy, xx, cnt;
        for (yy = 0; yy < lines; yy++) {
            cnt = 0;
            for (xx = 0; xx < px; xx++)
                if (ir[(size_t)yy * px + xx] > lim) cnt++;
            if (cnt * 2 > px) { r0 = yy; break; }
        }
        for (yy = lines - 1; yy > r0; yy--) {
            cnt = 0;
            for (xx = 0; xx < px; xx++)
                if (ir[(size_t)yy * px + xx] > lim) cnt++;
            if (cnt * 2 > px) { r1 = yy + 1; break; }
        }
        for (xx = 0; xx < px; xx++) {
            cnt = 0;
            for (yy = r0; yy < r1; yy++)
                if (ir[(size_t)yy * px + xx] > lim) cnt++;
            if (cnt * 2 > (r1 - r0)) { c0 = xx; break; }
        }
        for (xx = px - 1; xx > c0; xx--) {
            cnt = 0;
            for (yy = r0; yy < r1; yy++)
                if (ir[(size_t)yy * px + xx] > lim) cnt++;
            if (cnt * 2 > (r1 - r0)) { c1 = xx + 1; break; }
        }
    }
    n = (size_t)(r1 - r0) * (c1 - c0);
    if (n < 1024) {
        DBG(DBG_info, "IR clean: no usable frame interior; "
            "skipping repair\n");
        return -1;
    }

    /* Interior median, then MAD about it. */
    memset(hist, 0, sizeof(hist));
    for (y = r0; y < r1; y++)
        for (x = c0; x < c1; x++) hist[ir[(size_t)y * px + x]]++;
    acc = 0;
    for (i = 0; i < 65536; i++) {
        acc += hist[i];
        if (acc >= n / 2) break;
    }
    median = (double)i;
    {
        static uint32_t dh[65536];
        size_t j;
        memset(dh, 0, sizeof(dh));
        for (j = 0; j < 65536; j++) {
            long d;
            if (!hist[j]) continue;
            d = (long)j - (long)median;
            if (d < 0) d = -d;
            if (d > 65535) d = 65535;
            dh[d] += hist[j];
        }
        acc = 0;
        for (j = 0; j < 65536; j++) {
            acc += dh[j];
            if (acc >= n / 2) break;
        }
        mad = (double)j * 1.4826;
    }
    if (mad < 1.0) mad = 1.0;
    thr = median - 5.0 * mad;
    if (thr < 0.0) thr = 0.0;

    /*
     * Flat-field before thresholding.  The IR illumination falls off ~29%
     * towards the left edge (min/max 0.71 at 600 dpi), so one global threshold
     * marked the dim edge columns (80% of 4.3% marked, in the leftmost 5%).
     * Dividing by the frame's own column and row profiles, box-smoothed over
     * 1/16 of the frame so a hair cannot pull its own column down, removes the
     * falloff.  The interior is trimmed by 2 px: the outermost pixels are
     * half-lit and read as a ring of false marks.
     */
    if (c1 - c0 > 64 && r1 - r0 > 64) {
        c0 += 2; c1 -= 2; r0 += 2; r1 -= 2;
    }
    {
        int nc = c1 - c0, nr = r1 - r0, j, wc, wr;
        double *colp = calloc(nc, sizeof(double));
        double *cols = calloc(nc, sizeof(double));
        double *rowp = calloc(nr, sizeof(double));
        double *rows = calloc(nr, sizeof(double));
        double nmed, nmad;
        size_t nn = 0;

        if (!colp || !cols || !rowp || !rows) {
            free(colp); free(cols); free(rowp); free(rows);
            DBG(DBG_info, "IR clean: out of memory\n");
            return -1;
        }
        for (x = 0; x < nc; x++) {
            double s = 0;
            for (y = r0; y < r1; y++) s += ir[(size_t)y * px + c0 + x];
            colp[x] = s / nr;
        }
        wc = (nc / 16 > 31 ? nc / 16 : 31) | 1;
        for (x = 0; x < nc; x++) {
            double s = 0;
            for (j = -wc / 2; j <= wc / 2; j++) {
                int k = x + j;
                if (k < 0) k = 0;
                if (k >= nc) k = nc - 1;
                s += colp[k];
            }
            cols[x] = s / wc;
            if (cols[x] < 1.0) cols[x] = 1.0;
        }
        for (y = 0; y < nr; y++) {
            double s = 0;
            for (x = 0; x < nc; x++)
                s += ir[(size_t)(r0 + y) * px + c0 + x] / cols[x];
            rowp[y] = s / nc;
        }
        wr = (nr / 16 > 31 ? nr / 16 : 31) | 1;
        for (y = 0; y < nr; y++) {
            double s = 0;
            for (j = -wr / 2; j <= wr / 2; j++) {
                int k = y + j;
                if (k < 0) k = 0;
                if (k >= nr) k = nr - 1;
                s += rowp[k];
            }
            rows[y] = s / wr;
            if (rows[y] < 1e-6) rows[y] = 1e-6;
        }

        /* Median and MAD of the normalised frame, 1.0 = 32768. */
        memset(hist, 0, sizeof(hist));
        for (y = 0; y < nr; y++)
            for (x = 0; x < nc; x++) {
                double v = ir[(size_t)(r0 + y) * px + c0 + x]
                           / (cols[x] * rows[y]) * 32768.0;
                if (v < 0) v = 0;
                if (v > 65535) v = 65535;
                hist[(int)v]++;
                nn++;
            }
        acc = 0;
        for (i = 0; i < 65536; i++) {
            acc += hist[i];
            if (acc >= nn / 2) break;
        }
        nmed = (double)i;
        {
            static uint32_t dh2[65536];
            size_t q;
            memset(dh2, 0, sizeof(dh2));
            for (q = 0; q < 65536; q++) {
                long d;
                if (!hist[q]) continue;
                d = (long)q - (long)nmed;
                if (d < 0) d = -d;
                if (d > 65535) d = 65535;
                dh2[d] += hist[q];
            }
            acc = 0;
            for (q = 0; q < 65536; q++) {
                acc += dh2[q];
                if (acc >= nn / 2) break;
            }
            nmad = (double)q;
        }
        if (nmad < 1.0) nmad = 1.0;

        /* k = 10: with the falloff gone the MAD is ~0.7% (was ~3.8%), so k = 5
         * would mark grain; at 10 the marks are specks and hairs. */
        {
            double tf = (nmed - 10.0 * nmad) / 32768.0;
            uint16_t *lvl = malloc((size_t)nc * nr * sizeof(uint16_t));
            if (!lvl) {
                free(colp); free(cols); free(rowp); free(rows);
                return -1;
            }
            /* Per-pixel threshold, stored in place of the IR so
             * the repair loop below compares against it. */
            for (y = 0; y < nr; y++)
                for (x = 0; x < nc; x++) {
                    double t = cols[x] * rows[y] * tf;
                    size_t p = (size_t)(r0 + y) * px + c0 + x;
                    /* 1 = defective, 0 = clean */
                    lvl[(size_t)y * nc + x] = (ir[p] < t) ? 1 : 0;
                }
            for (y = 0; y < nr; y++)
                for (x = 0; x < nc; x++)
                    ir[(size_t)(r0 + y) * px + c0 + x] =
                        lvl[(size_t)y * nc + x] ? 0 : 65535;
            free(lvl);
            DBG(DBG_info, "IR clean: flat-fielded median %.4f "
                "mad %.4f, defect depth %.1f%% below local level\n",
                nmed / 32768.0, nmad / 32768.0,
                100.0 * (1.0 - tf));
        }
        {
            double lo = 1e30, hi = 0;
            for (x = 0; x < nc; x++) {
                if (cols[x] < lo) lo = cols[x];
                if (cols[x] > hi) hi = cols[x];
            }
            DBG(DBG_info, "IR clean: column profile min/max %.3f\n",
                hi > 0 ? lo / hi : 0.0);
        }
        free(colp); free(cols); free(rowp); free(rows);
        thr = 32767.0;   /* ir now holds 0 = defect, 65535 = clean */
    }

    dev->ir_r0 = r0; dev->ir_r1 = r1; dev->ir_c0 = c0; dev->ir_c1 = c1;
    dev->ir_n = n;
    dev->ir_median = median; dev->ir_mad = mad;
    dev->ir_ready = 1;
    (void)thr;
    return 0;
}

/* Repair one output row of the colour image from the mark map. */
static void
plustek8200i_ir_repair_row(plustek8200i_device_t *dev, int y, uint16_t *row)
{
    int px = dev->scan_pixels, r0 = dev->ir_r0, r1 = dev->ir_r1;
    int c0 = dev->ir_c0, c1 = dev->ir_c1, x;
    const uint16_t *irow;

    if (!dev->ir_ready || y < r0 || y >= r1)
        return;
    irow = (const uint16_t *)dev->ir_map + (size_t)y * px;
    x = c0;
    while (x < c1) {
        int x0, x1, c;
        if (irow[x] >= 32767) { x++; continue; }
        x0 = x;
        while (x < c1 && irow[x] < 32767) x++;
        x1 = x;                     /* [x0, x1) is defective */
        dev->ir_marked += x1 - x0;
        /*
         * Wide runs are damage or shading, not a speck. Interpolating
         * across them smears real detail -- at a looser setting this
         * produced visible coloured streaks across the frame. Leave them.
         */
        if (x1 - x0 > (c1 - c0) / 32) continue;
        for (c = 0; c < 3; c++) {
            int lo = (x0 > c0) ? row[(x0 - 1) * 3 + c] : -1;
            int hi = (x1 < c1) ? row[x1 * 3 + c] : -1;
            int i2;
            if (lo < 0 && hi < 0) continue;
            if (lo < 0) lo = hi;
            if (hi < 0) hi = lo;
            for (i2 = x0; i2 < x1; i2++)
                row[i2 * 3 + c] = (uint16_t)
                    (lo + (hi - lo) * (i2 - x0 + 1) / (x1 - x0 + 1));
        }
        dev->ir_repaired += x1 - x0;
    }
}

static void
plustek8200i_ir_report(plustek8200i_device_t *dev)
{
    DBG(DBG_info, "IR clean: interior %dx%d, median %.0f mad %.0f; "
        "%d px marked, %d repaired (%.3f%%)\n",
        dev->ir_c1 - dev->ir_c0, dev->ir_r1 - dev->ir_r0,
        dev->ir_median, dev->ir_mad, dev->ir_marked, dev->ir_repaired,
        dev->ir_n ? 100.0 * dev->ir_repaired / (double)dev->ir_n : 0.0);
}

/*
 * Line-by-line image pipeline for streaming a scan from sane_read.  The
 * stages match the full-buffer post-processing at the end of start_scan,
 * with the same rounding: vertical resample (rounded to 16 bit per row),
 * channel registration (red from row y - s, blue from y + s,
 * clamped at the edges), horizontal mirror, Gray/IR averaging, IR repair.
 * Auto white balance needs the whole image and is not part of it.
 */
/* Raw line i: the flat buffer, or slot i mod p_ring_lines of the ring
 * the streaming path reads into. */
static inline const uint8_t *
pipe_raw_line(plustek8200i_device_t *dev, int i)
{
    size_t slot = dev->p_ring_lines ? (size_t)(i % dev->p_ring_lines) : (size_t)i;
    return dev->p_raw + slot * dev->p_raw_line_bytes;
}

static void
pipe_resample_row(plustek8200i_device_t *dev, int k, uint16_t *out)
{
    int ncomp = dev->p_raw_line_bytes / 2, x, i;
    double v = dev->p_vsquash;
    double *acc = dev->p_acc;
    plustek8200i_vweights_t wt;

    if (v <= PLUSTEK8200I_VNONE) {
        memcpy(out, pipe_raw_line(dev, k), dev->p_raw_line_bytes);
        return;
    }
    if (plustek8200i_vresample_weights(v, k, &wt) != 0) {
        /* Cannot happen at the supported resolutions -- the unit test pins
         * the tap counts under the limit -- but deliver the nearest line
         * rather than a black one if it ever does. */
        int c = (int)((k + 0.5) * v);
        if (c < 0) c = 0;
        if (c >= dev->p_raw_lines) c = dev->p_raw_lines - 1;
        DBG(DBG_warning, "Vertical resample: no weights for line %d "
            "(squash %.3f); using raw line %d\n", k, v, c);
        memcpy(out, pipe_raw_line(dev, c), dev->p_raw_line_bytes);
        return;
    }
    memset(acc, 0, ncomp * sizeof(double));
    for (i = 0; i < wt.n; i++) {
        double w = wt.w[i];
        const uint16_t *src;
        if (w == 0.0) continue;
        src = (const uint16_t *)pipe_raw_line(dev,
              plustek8200i_vresample_src(&wt, i, dev->p_raw_lines));
        for (x = 0; x < ncomp; x++) acc[x] += w * src[x];
    }
    for (x = 0; x < ncomp; x++)
        out[x] = plustek8200i_vresample_clamp(acc[x]);
}

/* Oldest raw line output line y still reads (for the ring). */
static int
pipe_raw_oldest(plustek8200i_device_t *dev, int y)
{
    int k = y - dev->p_shift, first, last;
    if (k < 0) k = 0;
    plustek8200i_vresample_span(dev->p_vsquash, k, &first, &last);
    return first < 0 ? 0 : first;
}

/* Raw lines that must be in p_raw before output line y can be built. */
static int
pipe_raw_needed(plustek8200i_device_t *dev, int y)
{
    int k = y + dev->p_shift, first, last, n;
    if (k > dev->p_res_lines - 1) k = dev->p_res_lines - 1;
    plustek8200i_vresample_span(dev->p_vsquash, k, &first, &last);
    n = last + 1;
    if (n < 1) n = 1;
    return n > dev->p_raw_lines ? dev->p_raw_lines : n;
}

/* Build output line y (full width, before cropping) into out:
 * px*3 samples for Color, px for Gray/Infrared. */
static void
pipe_line(plustek8200i_device_t *dev, int y, uint16_t *out)
{
    int px = dev->scan_pixels, s = dev->p_shift, x, c;
    int L = dev->p_res_lines;
    uint16_t *row = dev->p_row, *tmp = dev->p_tmp;

    pipe_resample_row(dev, y, row);
    if (s > 0) {
        int yr = y >= s ? y - s : 0, yb = y + s < L ? y + s : L - 1;
        int px3 = (dev->p_raw_line_bytes / 2) - (dev->p_raw_line_bytes / 2) % 3;
        pipe_resample_row(dev, yr, tmp);
        for (x = 0; x < px3; x += 3) row[x] = tmp[x];
        pipe_resample_row(dev, yb, tmp);
        for (x = 2; x < px3; x += 3) row[x] = tmp[x];
    }
    for (x = 0; x < px / 2; x++) {
        int o = px - 1 - x;
        for (c = 0; c < 3; c++) {
            uint16_t t = row[x * 3 + c];
            row[x * 3 + c] = row[o * 3 + c];
            row[o * 3 + c] = t;
        }
    }
    if (dev->opt_mode == MODE_COLOR) {
        if (dev->ir_ready)
            plustek8200i_ir_repair_row(dev, y, row);
        memcpy(out, row, (size_t)px * 3 * 2);
    } else if (dev->opt_mode == MODE_INFRARED) {
        for (x = 0; x < px; x++) {
            double v = (row[x * 3] + row[x * 3 + 1] + row[x * 3 + 2])
                       / 3.0 * dev->ir_scale;
            out[x] = v > 65535.0 ? 65535 : (uint16_t)(v + 0.5);
        }
    } else {
        for (x = 0; x < px; x++)
            out[x] = (uint16_t)((row[x * 3] * 299u + row[x * 3 + 1] * 587u +
                                 row[x * 3 + 2] * 114u) / 1000u);
    }
}

/* Set up the pipeline over a raw buffer of raw_lines lines. */
static int
pipe_setup(plustek8200i_device_t *dev, uint8_t *raw, int raw_lines, int raw_line_bytes)
{
    int rdpi = dev->opt_resolution;
    int ncomp = raw_line_bytes / 2;

    if (rdpi != 300 && rdpi != 600 && rdpi != 1200 &&
        rdpi != 2400 && rdpi != 3600 && rdpi != 7200) rdpi = 600;
    dev->p_raw = raw;
    dev->p_ring_lines = 0;
    dev->p_raw_lines = raw_lines;
    dev->p_raw_line_bytes = raw_line_bytes;
    dev->p_vsquash = (double)plustek8200i_target_vdpi(rdpi) / (double)rdpi;
    dev->p_res_lines = dev->p_vsquash <= PLUSTEK8200I_VNONE ? raw_lines
                       : (int)(raw_lines / dev->p_vsquash);
    if (dev->p_res_lines < 1) dev->p_res_lines = 1;
    dev->p_shift = dev->opt_resolution / 300;
    if (!(dev->p_shift > 0 && dev->p_res_lines > 2 * dev->p_shift && ncomp >= 3))
        dev->p_shift = 0;
    free(dev->p_acc); free(dev->p_row); free(dev->p_tmp);
    dev->p_acc = calloc(ncomp, sizeof(double));
    dev->p_row = calloc(ncomp, sizeof(uint16_t));
    dev->p_tmp = calloc(ncomp, sizeof(uint16_t));
    return (dev->p_acc && dev->p_row && dev->p_tmp) ? 0 : -1;
}

static void
pipe_free(plustek8200i_device_t *dev)
{
    free(dev->p_acc); free(dev->p_row); free(dev->p_tmp);
    dev->p_acc = NULL; dev->p_row = NULL; dev->p_tmp = NULL;
    /* p_raw is always an alias (image_buffer, or a caller's ring buffer)
     * and is never owned here -- but it must still be dropped, or it is
     * left dangling once that owner frees it. */
    dev->p_raw = NULL;
    dev->p_raw_lines = dev->p_ring_lines = 0;
}

/*
 * Streaming.  start_scan programs everything, issues the image START and
 * returns; sane_read pulls raw chunks only as far as the next output line
 * needs, builds it with pipe_line, crops and hands it out, so a frontend
 * gets data and progress while the carriage moves.  Used unless the full
 * image is needed first (--auto-wb, the --ir-clean infrared pre-pass, the
 * raw dump, the equivalence check) or PLUSTEK8200I_STREAM=0.
 */
static double
now_s(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void
stream_cleanup(plustek8200i_device_t *dev)
{
    pipe_free(dev);
    free(dev->s_full);
    free(dev->s_out);
    dev->s_full = NULL;
    dev->s_out = NULL;
    free(dev->s_ir);
    dev->s_ir = NULL;
    free(dev->s_stage);
    dev->s_stage = NULL;
    free(dev->image_buffer);
    dev->image_buffer = NULL;
    dev->buffer_size = 0;
    free(dev->ir_map);
    dev->ir_map = NULL;
    dev->ir_map_size = 0;
    dev->ir_ready = 0;
    dev->streaming = 0;
}

/*
 * Release everything a scan pass allocated, whatever path it died on.
 * stream_cleanup() covers the pipeline, the staging and ring buffers, the
 * IR plane and the stashed IR map. Safe to call repeatedly and on a device that never scanned.
 */
static void
release_scan_resources(plustek8200i_device_t *dev)
{
    stream_cleanup(dev);
    dev->scanning = SANE_FALSE;
    dev->bytes_read = dev->bytes_total = 0;
}

static SANE_Status
stream_begin(plustek8200i_device_t *dev)
{
    int px = dev->scan_pixels, x0, y0, x1, y1;
    int comps = dev->opt_mode == MODE_COLOR ? 3 : 1;
    /* The infrared pre-pass is kept at 16 bit whatever the depth. */
    int bps = (dev->opt_bit_depth == 8 && !dev->in_ir_pass) ? 1 : 2;
    int lb = dev->scan_line_bytes, ring;

    if (pipe_setup(dev, NULL, dev->raw_scan_lines, lb) != 0)
        return SANE_STATUS_NO_MEM;
    /*
     * Raw lines are kept in a ring, not a whole-frame buffer (482 MB at
     * 7200 dpi).  Output line y needs raw lines pipe_raw_oldest(y) up to
     * pipe_raw_needed(y): the channel registration spreads the rows it
     * reads over 2 * shift output pitches, and each of those rows reaches
     * the resampler's support either side of its own centre.  Taking the
     * support from the kernel rather than assuming an area-average keeps
     * this in step with it -- Lanczos-3 reads 3 output pitches each way,
     * six times as far as the box it replaced.  Reading runs at most one
     * USB chunk ahead.
     */
    {
        double v = dev->p_vsquash <= PLUSTEK8200I_VNONE ? 1.0 : dev->p_vsquash;
        double sup = plustek8200i_vresample_support(dev->p_vsquash);
        ring = (int)(2 * dev->p_shift * v + 2 * sup + 0.999) + 3
               + PLUSTEK8200I_STREAM_CHUNK / lb + 2 + 16;
        if (ring > dev->raw_scan_lines)
            ring = dev->raw_scan_lines;
    }
    free(dev->image_buffer);
    dev->image_buffer = malloc((size_t)ring * lb);
    dev->s_stage = malloc(PLUSTEK8200I_STREAM_CHUNK);
    if (!dev->image_buffer || !dev->s_stage) {
        stream_cleanup(dev);
        return SANE_STATUS_NO_MEM;
    }
    dev->buffer_size = (size_t)ring * lb;
    dev->p_raw = dev->image_buffer;
    dev->p_ring_lines = ring;
    dev->s_bps = bps;
    if (dev->opt_rgbi && !dev->in_ir_pass && !rgbi_image_first(dev)) {
        /* The IR plane is kept as scanned; --ir-clean gets its own copy
         * because detection overwrites the map with marks. */
        size_t need = (size_t)px * 2 * dev->p_res_lines;
        if (!dev->ir_map || dev->ir_map_size < need) {
            DBG(DBG_info, "Color+Infrared: IR pass has %zu bytes, colour needs %zu\n",
                dev->ir_map ? dev->ir_map_size : 0, need);
            stream_cleanup(dev);
            return SANE_STATUS_IO_ERROR;
        }
        dev->s_ir = (uint16_t *)dev->ir_map;
        dev->ir_map = NULL;
        dev->ir_map_size = 0;
        if (dev->opt_ir_clean) {
            dev->ir_map = malloc(need);
            if (dev->ir_map) {
                memcpy(dev->ir_map, dev->s_ir, need);
                dev->ir_map_size = need;
            }
        }
    }
    if (dev->ir_map && dev->opt_mode == MODE_COLOR && dev->opt_ir_clean) {
        if (dev->ir_map_size >= (size_t)px * 2 * dev->p_res_lines)
            plustek8200i_ir_prepare(dev, dev->p_res_lines);
        else
            DBG(DBG_warning, "IR clean: map does not match the colour pass; skipped\n");
    }
    if (dev->in_ir_pass) {
        x0 = 0; y0 = 0; x1 = px; y1 = dev->p_res_lines;  /* full plane */
    } else {
        plustek8200i_crop_rect(dev, px, dev->p_res_lines, &x0, &y0, &x1, &y1);
    }
    dev->s_x0 = x0; dev->s_y0 = y0; dev->s_x1 = x1; dev->s_y1 = y1;
    dev->s_comps = comps;
    dev->s_full = malloc((size_t)px * 3 * sizeof(uint16_t));
    dev->s_out = malloc((size_t)(x1 - x0) * comps * 2);
    if (!dev->s_full || !dev->s_out) {
        stream_cleanup(dev);
        return SANE_STATUS_NO_MEM;
    }
    dev->params.format = comps == 3 ? SANE_FRAME_RGB : SANE_FRAME_GRAY;
    /* In the image-first order the infrared frame is the last one. */
    dev->params.last_frame = (dev->opt_rgbi && !(rgbi_image_first(dev) && dev->opt_mode == MODE_INFRARED)) ? SANE_FALSE : SANE_TRUE;
    dev->s_frame = 0;
    dev->s_eof = 0;
    dev->params.depth = dev->opt_bit_depth;
    dev->params.pixels_per_line = x1 - x0;
    dev->params.lines = y1 - y0;
    dev->params.bytes_per_line = (x1 - x0) * comps * bps;
    dev->s_raw_read = 0;
    dev->s_raw_total = (size_t)dev->raw_scan_lines * dev->scan_line_bytes;
    dev->s_out_y = y0;
    dev->s_line_len = dev->s_line_pos = 0;
    dev->s_hw_done = 0;
    dev->s_first = 1;
    dev->s_ir_reported = 0;
    dev->s_t0 = now_s();
    dev->streaming = 1;
    dev->scanning = SANE_TRUE;
    dev->bytes_read = 0;
    sanei_usb_set_timeout(180000);   /* first chunk waits out the lead-in */
    DBG(DBG_info, "Streaming: %dx%d %s from %d raw lines (squash %.3f, shift %d), "
        "ring %d lines = %.1f MB\n",
        x1 - x0, y1 - y0, dev->in_ir_pass ? "IR plane" : comps == 3
        ? (dev->opt_rgbi ? "RGB (+IR frame)" : "RGB") : "gray", dev->raw_scan_lines,
        dev->p_vsquash, dev->p_shift, ring, dev->buffer_size / 1e6);
    return SANE_STATUS_GOOD;
}

/* Stop the scan engine once all raw data is in; the carriage returns
 * home by itself (AGOHOME). */
static void
stream_hw_done(plustek8200i_device_t *dev)
{
    if (!dev->s_hw_done) {
        plustek8200i_scan_stop(dev->usb_fd);
        dev->s_hw_done = 1;
        DBG(DBG_info, "Streaming: all %zu raw bytes read after %.1f s; carriage returns home\n",
            dev->s_raw_total, now_s() - dev->s_t0);
    }
}

static SANE_Status
stream_read(plustek8200i_device_t *dev, SANE_Byte *data, SANE_Int max_length,
            SANE_Int *length)
{
    SANE_Status st = SANE_STATUS_GOOD;
    int bps = dev->s_bps;

    *length = 0;
    dev->in_read = 1;
    while (*length < max_length) {
        if (dev->s_line_pos < dev->s_line_len) {
            size_t n = dev->s_line_len - dev->s_line_pos;
            if (n > (size_t)(max_length - *length))
                n = max_length - *length;
            memcpy(data + *length, dev->s_out + dev->s_line_pos, n);
            dev->s_line_pos += n;
            *length += (SANE_Int)n;
            continue;
        }
        if (dev->s_out_y >= dev->s_y1) {
            stream_hw_done(dev);
            if (rgbi_image_first(dev) && !dev->s_eof) {
                /* The image frame is done: the infrared frame is next.  After
                 * the infrared frame, the next image starts with colour. */
                dev->s_eof = 1;
                if (dev->opt_mode == MODE_COLOR) {
                    dev->rgbi_ir_next = 1;
                } else if (dev->opt_mode == MODE_INFRARED) {
                    dev->opt_mode = MODE_COLOR;
                    dev->rgbi_ir_next = 0;
                }
            }
                if (dev->ir_ready && !dev->s_ir_reported) {
                plustek8200i_ir_report(dev);
                dev->s_ir_reported = 1;
            }
        if (*length == 0)
                st = SANE_STATUS_EOF;
            break;
        }
        if (dev->s_frame == 1) {
            /* Infrared frame of Color+Infrared: from memory. */
            int x, w = dev->s_x1 - dev->s_x0;
            const uint16_t *ir = dev->s_ir + (size_t)dev->s_out_y * dev->scan_pixels
                                 + dev->s_x0;
            if (bps == 2)
                memcpy(dev->s_out, ir, (size_t)w * 2);
            else
                for (x = 0; x < w; x++)
                    dev->s_out[x] = (uint8_t)(ir[x] >> 8);
            dev->s_line_len = (size_t)w * bps;
            dev->s_line_pos = 0;
            dev->s_out_y++;
            continue;
        }
        {
            int need = pipe_raw_needed(dev, dev->s_out_y);
            while ((int)(dev->s_raw_read / dev->scan_line_bytes) < need
                   && dev->s_raw_read < dev->s_raw_total) {
                size_t got = 0;
                if (dev->cancel_requested) {
                    st = SANE_STATUS_CANCELLED;
                    goto out;
                }
                st = plustek8200i_read_scan_chunk(dev->usb_fd, dev->s_stage,
                         dev->s_raw_total - dev->s_raw_read, &got);
                if (st != SANE_STATUS_GOOD) {
                    DBG(DBG_error, "Streaming: read failed at %zu of %zu bytes\n",
                        dev->s_raw_read, dev->s_raw_total);
                    goto out;
                }
                {
                    /* Into the ring. Never overwrite a line this output
                     * line still reads (cannot happen with the ring
                     * sized in stream_begin; checked anyway). */
                    size_t lb = dev->scan_line_bytes, rb = dev->buffer_size;
                    size_t end_line = (dev->s_raw_read + got + lb - 1) / lb;
                    size_t pos = dev->s_raw_read % rb, n1 = rb - pos;
                    int oldest = pipe_raw_oldest(dev, dev->s_out_y);
                    if (end_line > (size_t)oldest + dev->p_ring_lines) {
                        DBG(DBG_warning, "Streaming: ring of %d lines too small "
                            "(line %zu, oldest needed %d)\n",
                            dev->p_ring_lines, end_line, oldest);
                        st = SANE_STATUS_IO_ERROR;
                        goto out;
                    }
                    if (n1 > got)
                        n1 = got;
                    memcpy(dev->image_buffer + pos, dev->s_stage, n1);
                    memcpy(dev->image_buffer, dev->s_stage + n1, got - n1);
                }
                if (dev->s_first) {
                    dev->s_first = 0;
                    sanei_usb_set_timeout(30000);
                    DBG(DBG_info, "Streaming: first data after %.2f s\n",
                        now_s() - dev->s_t0);
                }
                dev->s_raw_read += got;
            }
            if (dev->s_raw_read >= dev->s_raw_total)
                stream_hw_done(dev);
        }
        pipe_line(dev, dev->s_out_y, dev->s_full);
        {
            int comps = dev->s_comps, x, n = (dev->s_x1 - dev->s_x0) * comps;
            const uint16_t *src = dev->s_full + (size_t)dev->s_x0 * comps;
            if (bps == 2) {
                /* SANE transmits 16-bit samples in the HOST's native byte
                 * order -- NOT big-endian. net.c swaps when the saned
                 * server and the client disagree, so a backend must never
                 * byte-swap itself. Copying the native uint16_t words out
                 * is therefore both correct and the whole job. */
                memcpy(dev->s_out, src, (size_t)n * 2);
            } else {
                for (x = 0; x < n; x++)
                    dev->s_out[x] = (uint8_t)(src[x] >> 8);
            }
            dev->s_line_len = (size_t)n * bps;
            dev->s_line_pos = 0;
        }
        dev->s_out_y++;
        {
            int done = dev->s_out_y - dev->s_y0, all = dev->s_y1 - dev->s_y0;
            if (all >= 10 && done % (all / 10) == 0)
                DBG(DBG_info, "Streaming: line %d/%d at %.1f s (raw %zu%%)\n",
                    done, all, now_s() - dev->s_t0,
                    100 * dev->s_raw_read / dev->s_raw_total);
        }
    }
out:
    dev->bytes_read += *length;
    dev->in_read = 0;
    if (st == SANE_STATUS_GOOD && *length == 0 && dev->s_out_y >= dev->s_y1)
        st = SANE_STATUS_EOF;
    if (st == SANE_STATUS_CANCELLED || (st != SANE_STATUS_GOOD && st != SANE_STATUS_EOF)) {
        rgbi_abandon(dev);
        if (!dev->s_hw_done) {
            plustek8200i_scan_stop(dev->usb_fd);
            plustek8200i_motor_stop(dev->usb_fd);
            dev->s_hw_done = 1;
        }
        stream_cleanup(dev);
        dev->scanning = SANE_FALSE;
        *length = 0;
    }
    return st;
}

/* Whether this pass streams (ring buffer) or uses the full-buffer path. */
static int
stream_wanted(plustek8200i_device_t *dev)
{
    if (dev->in_ir_pass)
        return 1;
    if (dev->opt_rgbi)
        return 1;
    return !dev->opt_auto_wb;
}

/*
 * The streamed infrared pre-pass of --ir-clean / Color+Infrared: builds
 * the full, uncropped 16-bit IR plane line by line, keeping only the
 * plane, and leaves it in image_buffer for sane_start to take as ir_map.
 */
static SANE_Status
ir_pass_stream(plustek8200i_device_t *dev)
{
    SANE_Status st = stream_begin(dev);
    size_t size, off = 0;
    uint8_t *plane;

    if (st != SANE_STATUS_GOOD)
        return st;
    size = (size_t)dev->params.bytes_per_line * dev->params.lines;
    plane = malloc(size);
    if (!plane) {
        stream_cleanup(dev);
        return SANE_STATUS_NO_MEM;
    }
    for (;;) {
        SANE_Int len = 0, want = size - off > (1 << 24) ? (1 << 24) : (SANE_Int)(size - off);
        st = stream_read(dev, plane + off, want, &len);
        off += len;
        if (st != SANE_STATUS_GOOD || off >= size)
            break;
    }
    if (st == SANE_STATUS_GOOD || st == SANE_STATUS_EOF) {
        stream_hw_done(dev);   /* no-op if already done */
        stream_cleanup(dev);
        if (off != size) {
            free(plane);
            return SANE_STATUS_IO_ERROR;
        }
        dev->image_buffer = plane;
        dev->buffer_size = size;
        return SANE_STATUS_GOOD;
    }
    free(plane);               /* stream_read has stopped and cleaned up */
    return st;
}

/*
 * Full init: three register/AFE passes, GPIO, lamp source.
 */
static SANE_Status
scan_init_sequence(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /*
     * Init sequence, as observed in a USB capture of the vendor driver
     * (register names: see plustek8200i.h):
     *  1. Write reg 0x0E = 0x00 (clear status)
     *  2. Bulk register write (init-phase values)
     *  3. Post-register CCD timing, 100 ms delay, reg 0x0A = 0x40
     *  4. AFE init (8 x 6-byte blocks)
     *  5. Read interrupt register 0x8A (clear pending interrupts)
     *  6. Repeat 2-4
     *  7. Reg 0x03 = 0x30 (lamp source)
     *  8. Regs 0x02=0x18, 0xAE=0x00, 0xAF=0xFF, 0x3D=0x00, 0x3E=0x11, 0x3F=0x62
     *  9. Motor table to 0x1000C000 and 0x10010000, with a reg 0x18 DMA
     *     status read after each
     * 10. Reg 0x0F = 0x01 (MOVE, last), then poll reg 0x01 for completion
     * (No CMD write 0x0D = 0x45 is made.)
     */
    /*
     * Let the previous scan's return home finish.  After the image pass
     * (run with AGOHOME) the ASIC drives the carriage home by itself; starting
     * sooner made MOVE #1 take 7.8 s instead of 0.4 s.  Wait for
     * home-and-idle (0x101 bit 3 set, bit 0 clear); no motion is commanded.
     * Carry on after 10 s (an aborted scan can leave the carriage anywhere);
     * MOVE #1/#2 rehome it.
     */
    {
        uint8_t st = 0;
        int waited = 0;

        while (waited < 10000) {
            if (plustek8200i_read_reg(dev->usb_fd, 0x101, &st) != SANE_STATUS_GOOD)
                break;
            /* At home (bit 3) AND motor no longer enabled (bit 0):
             * stopping on the sensor happens a moment after it trips,
             * and starting MOVE #1 in that moment (status 0xFD instead
             * of 0xFC) put the frame 11 rows off. */
            if ((st & 0x09) == 0x08)
                break;
            usleep(20000);
            waited += 20;
        }
        DBG(DBG_info, "Waited %d ms for the carriage to rest at home (status 0x%02x%s)\n",
            waited, st, ((st & 0x09) == 0x08) ? "" : ", NOT at rest at home -- continuing");
    }

    /*
     * Lamp drive level.  SilverFast opens every session with two writes on a
     * separate vendor path (control OUT, bRequest 0x0C, wValue 0x8C, address
     * in wIndex): 0x10 = 0x0C, then 0x13 = 0x0C (usbmon capture
     * 20261006-120458-sf-prescan-pos, t = 2.23 s).  Together, and only
     * together, they fix the white lamp and IR LED level until power-off
     * (open light at AFE code 0 reads R/G 40.6k/54.9k instead of
     * 54.3k/>65.5k after a power cycle), found by bisecting a replay.  Without
     * them the level depended on whether SilverFast had run since power-on,
     * the white strip saturated and the per-scan calibration had to refuse.
     */
    {
        static const uint16_t addr[2] = { 0x10, 0x13 };
        int k;
        for (k = 0; k < 2; k++) {
            uint8_t v = 0x0C;
            SANE_Status ls = sanei_usb_control_msg(dev->usb_fd, 0x40, 0x0C,
                                                   0x8C, addr[k], 1, &v);
            if (ls != SANE_STATUS_GOOD)
                DBG(DBG_error, "Lamp level write 0x%02x failed: %s\n",
                    addr[k], sane_strstatus(ls));
        }
        DBG(DBG_info, "Lamp level set (0x8C path: 0x10 = 0x13 = 0x0C)\n");
    }

    DBG(DBG_info, "=== Init sequence ===\n");
    plustek8200i_regtrace_mark("Init sequence");

    /* Step 1: Clear status register */
    DBG(DBG_info, "Step 1: Clear status register 0x0E\n");
    status = plustek8200i_write_reg(dev->usb_fd, 0x0E, 0x00);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Failed to clear status register: %s\n", sane_strstatus(status));
        return status;
    }

    /* Step 2: WriteAllReg (first pass) */
    DBG(DBG_info, "Step 2: WriteAllReg (first pass)\n");
    status = plustek8200i_init_registers(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "WriteAllReg (first pass) failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* Step 3-5: Post-WriteAllReg CCD timing adjustments */
    DBG(DBG_info, "Step 3-5: Post-WriteAllReg adjustments\n");
    status = plustek8200i_post_writeallreg(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Post-WriteAllReg failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* GPIO init, in the order of sane-genesys' GPO table for a related ASIC
     * (0x6e, 0x6f first).  Register 0x6d bit 7 (GPIO8) probably powers the
     * transparency lamp. */
    DBG(DBG_info, "GPIO init: writing GPO registers for transparency lamp\n");
    plustek8200i_write_reg(dev->usb_fd, 0x6e, 0x4c);  /* Extended GPIO direction */
    plustek8200i_write_reg(dev->usb_fd, 0x6f, 0x80);  /* Extended GPIO data out (bit 7 HIGH) */
    plustek8200i_write_reg(dev->usb_fd, 0x6b, 0x30);  /* GPIO direction */
    plustek8200i_write_reg(dev->usb_fd, 0x6c, 0x4c);  /* GPIO output enable */
    plustek8200i_write_reg(dev->usb_fd, 0x6d, 0x80);  /* GPIO data output (GPIO8 HIGH — lamp enable) */
    plustek8200i_write_reg(dev->usb_fd, 0xa6, 0x00);  /* GPIO control */
    plustek8200i_write_reg(dev->usb_fd, 0xa7, 0x07);  /* GPIO control */
    plustek8200i_write_reg(dev->usb_fd, 0xa8, 0x20);  /* GPIO control (bit 5) */
    plustek8200i_write_reg(dev->usb_fd, 0xa9, 0x01);  /* GPIO control */

    /* Step 6: WriteAllAfe (first pass) */
    DBG(DBG_info, "Step 6: WriteAllAfe (first pass)\n");
    status = plustek8200i_write_afe_init(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "WriteAllAfe (first pass) failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* Step 7: Read interrupt register 0x8A to clear pending interrupts */
    DBG(DBG_info, "Step 7: Clear pending interrupts (read reg 0x8A)\n");
    {
        uint8_t irq_status;
        status = plustek8200i_read_reg(dev->usb_fd, 0x8A, &irq_status);
        if (status == SANE_STATUS_GOOD) {
            DBG(DBG_info, "Interrupt register 0x8A = 0x%02x (cleared)\n", irq_status);
        }
    }

    /* Step 8: Repeat WriteAllReg + post-adjustments + WriteAllAfe (second pass) */
    DBG(DBG_info, "Step 8: WriteAllReg (second pass)\n");
    status = plustek8200i_init_registers(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "WriteAllReg (second pass) failed: %s\n", sane_strstatus(status));
        return status;
    }

    status = plustek8200i_post_writeallreg(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Post-WriteAllReg (second pass) failed: %s\n", sane_strstatus(status));
        return status;
    }

    status = plustek8200i_write_afe_init(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "WriteAllAfe (second pass) failed: %s\n", sane_strstatus(status));
        return status;
    }

    /*
     * Third init pass (before MOVE #1); the capture shows three register
     * passes, not two.  Between the second AFE and the third register
     * write it reads two special registers: the interrupt register
     * (wValue 0x008A, wIndex 0x26FE) and a status poll (wValue 0x008E,
     * wIndex 0).  After the third pass only a partial post-write follows:
     * 0x10=0x0E, 0x13=0x0E, 0x0B=0x6C (no 0x0B=0x84/0x8C, which is for
     * passes 1 and 2).  Without it reg 0x0B stays 0x8C instead of 0x6C.
     */
    DBG(DBG_info, "=== Third init pass ===\n");
    plustek8200i_regtrace_mark("Third init pass");

    /* Interrupt read (wValue 0x008A, wIndex 0x26FE): not the read_reg format */
    {
        uint8_t irq_val;
        SANE_Status irq_status;
        irq_status = sanei_usb_control_msg(dev->usb_fd,
                                            0xC0,    /* bmRequestType: vendor IN */
                                            0xA0,    /* bRequest */
                                            0x008A,  /* wValue: interrupt read */
                                            0x26FE,  /* wIndex: special format */
                                            1,       /* wLength */
                                            &irq_val);
        if (irq_status == SANE_STATUS_GOOD) {
            DBG(DBG_info, "Interrupt read (0x008A): 0x%02x\n", irq_val);
        } else {
            DBG(DBG_warning, "Interrupt read failed: %s (continuing)\n",
                sane_strstatus(irq_status));
        }
    }

    /* Status poll: wValue 0x008E, wIndex 0x0000 */
    {
        uint8_t poll_val;
        SANE_Status poll_status;
        poll_status = sanei_usb_control_msg(dev->usb_fd,
                                             0xC0,    /* bmRequestType: vendor IN */
                                             0xA0,    /* bRequest */
                                             0x008E,  /* wValue: status poll */
                                             0x0000,  /* wIndex: 0x0000 */
                                             1,       /* wLength */
                                             &poll_val);
        if (poll_status == SANE_STATUS_GOOD) {
            DBG(DBG_info, "Status poll (0x008E/0x0000): 0x%02x\n", poll_val);
        } else {
            DBG(DBG_warning, "Status poll failed: %s (continuing)\n",
                sane_strstatus(poll_status));
        }
    }

    /* Third WriteAllReg — same 116-pair init data as passes 1 and 2 */
    DBG(DBG_info, "Third init WriteAllReg (116 pairs)\n");
    status = plustek8200i_init_registers(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "WriteAllReg (third pass) failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* Partial post-WriteAllReg for third pass:
     * Only 0x10=0x0E and 0x13=0x0E (no 0x0B=0x84/0x8C cycle) */
    plustek8200i_write_reg(dev->usb_fd, 0x10, 0x0E);
    plustek8200i_write_reg(dev->usb_fd, 0x13, 0x0E);

    /* 0x0B=0x6C individually, as in the capture */
    plustek8200i_write_reg(dev->usb_fd, 0x0B, 0x6C);

    /* Third AFE */
    DBG(DBG_info, "Third init WriteAllAfe\n");
    status = plustek8200i_write_afe_init(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "WriteAllAfe (third pass) failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* The capture shows a ~450 ms gap after the third AFE pass, during which
     * reg 0x01 goes from 0x22 to 0xD8+ (ready). */
    DBG(DBG_info, "Waiting 500ms for hardware init to settle...\n");
    usleep(500000);

    /* Diagnostic: read multiple registers to check hardware state */
    {
        uint8_t val;
        plustek8200i_read_reg(dev->usb_fd, 0x101, &val);
        DBG(DBG_info, "DIAG: reg 0x01 (hw) = 0x%02x (expect ~0xD8)\n", val);
        plustek8200i_read_reg(dev->usb_fd, 0x0B, &val);
        DBG(DBG_info, "DIAG: reg 0x0B = 0x%02x (expect 0x6C after 3rd pass)\n", val);
        plustek8200i_read_reg(dev->usb_fd, 0x02, &val);
        DBG(DBG_info, "DIAG: reg 0x02 = 0x%02x (expect 0x78)\n", val);
    }

    /* Step 9: Set lamp source control */
    DBG(DBG_info, "Step 9: Lamp source control (reg 0x03 = 0x30)\n");
    status = plustek8200i_write_reg(dev->usb_fd, 0x03, 0x30);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Lamp source control failed: %s\n", sane_strstatus(status));
        return status;
    }

    return SANE_STATUS_GOOD;
}

/*
 * MOVE #1 -- drive the carriage to the home position and wait for it.
 */
static SANE_Status
scan_move_home(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /* MOVE #1: homing, with the init motor table loaded in the register phase */
    DBG(DBG_info, "=== MOVE #1: Homing ===\n");
    plustek8200i_regtrace_mark("MOVE #1: Homing");

    /* Pre-MOVE #1: 12-byte WriteRegs */
    DBG(DBG_info, "MOVE #1: Pre-MOVE register setup (12-byte WriteRegs)\n");
    {
        static const uint8_t move1_regs[12] = {
            0x02, 0x18, 0xAE, 0x00, 0xAF, 0xFF,
            0x3D, 0x00, 0x3E, 0x11, 0x3F, 0x62
        };
        status = plustek8200i_write_regs(dev->usb_fd, move1_regs, sizeof(move1_regs));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "MOVE #1 pre-MOVE WriteRegs failed: %s\n", sane_strstatus(status));
            return status;
        }
    }

    /* Load init motor tables */
    DBG(DBG_info, "MOVE #1: Loading init motor tables via DMA\n");
    status = plustek8200i_load_motor_tables(dev->usb_fd, 0x0076);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Init motor table loading failed: %s\n", sane_strstatus(status));
        DBG(DBG_warning, "Continuing anyway\n");
    }

    /* MOVE #1 command */
    DBG(DBG_info, "MOVE #1: Sending MOVE command\n");
    status = plustek8200i_write_reg(dev->usb_fd, 0x0F, 0x01);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "MOVE #1 failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* Poll for MOVE #1 completion every 2 ms (the capture polls at ~1.7 ms and
     * MOVE #1 takes ~315 ms), accepting when bit 0 clears.  Status goes
     * DD -> D5 -> F5 -> F4; bit 3 clears early. */
    DBG(DBG_info, "MOVE #1: Polling for completion (2ms intervals)...\n");
    {
        uint8_t reg01;
        int poll_elapsed = 0;
        /*
         * MOVE #1 must finish: after its feed the carriage creeps until the
         * motor is released (bit 0 clears), 0.5 s from home but 8-10 s after
         * a previous scan.  A 10 s limit made the frame position vary by 11
         * rows at 600 dpi; waiting for the release makes it repeatable.
         */
        int poll_timeout = 20000;

        while (poll_elapsed < poll_timeout) {
            status = plustek8200i_read_reg(dev->usb_fd, 0x101, &reg01);
            if (status == SANE_STATUS_GOOD) {
                /* Log every 50ms to track status progression */
                if (poll_elapsed % 50 == 0) {
                    DBG(DBG_info, "MOVE #1: r01=0x%02x (%dms) bit3=%d\n",
                        reg01, poll_elapsed, (reg01 >> 3) & 1);
                }
                /* Accept when bit 0 clears (no minimum wait) */
                if ((reg01 & 0x01) == 0) {
                    DBG(DBG_info, "MOVE #1 complete (reg 0x01 = 0x%02x, %dms)\n",
                        reg01, poll_elapsed);
                    break;
                }
            }
            usleep(2000);  /* 2 ms between polls */
            poll_elapsed += 2;
        }

        if (poll_elapsed >= poll_timeout) {
            DBG(DBG_warning, "MOVE #1 timeout (reg 0x01 = 0x%02x)\n", reg01);
            DBG(DBG_warning, "Continuing to scan phase anyway\n");
        }
    }

    return SANE_STATUS_GOOD;
}

/*
 * MOVE #2: the intermediate positioning step.
 */
static SANE_Status
scan_move_intermediate(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /*
     * MOVE #2: intermediate positioning between homing and scan
     * positioning, seen in the capture.  Reg 0x02 = 0x1C (lamp + motor
     * power), feed length 0x42D902, 0x7E/0x7F = init line period (0x2AF8),
     * init motor table.  Without it the hardware does not reach the right
     * state for scanning (reg 0x01 bits differ).
     */
    DBG(DBG_info, "=== MOVE #2: Intermediate positioning ===\n");
    plustek8200i_regtrace_mark("MOVE #2: Intermediate positioning");

    /* 38-byte WriteRegs block */
    {
        static const uint8_t move2_regs[38] = {
            0x02, 0x1C, 0x3D, 0x02, 0x3E, 0xD9, 0x3F, 0x42,
            0x7D, 0x00, 0x7E, 0x2A, 0x7F, 0xF8, 0x1C, 0x00,
            0x8A, 0x00, 0x8B, 0x00, 0x8C, 0x00, 0x8D, 0x00,
            0x8E, 0x00, 0x8F, 0x00, 0x90, 0x00, 0x91, 0x00,
            0x92, 0x00, 0xAE, 0x00, 0xAF, 0xFF
        };
        status = plustek8200i_write_regs(dev->usb_fd, move2_regs, sizeof(move2_regs));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "MOVE #2 WriteRegs failed: %s\n", sane_strstatus(status));
            return status;
        }
    }

    /* Load init motor tables (same table as MOVE #1) */
    DBG(DBG_info, "MOVE #2: Loading init motor tables\n");
    status = plustek8200i_load_motor_tables(dev->usb_fd, 0x0076);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "MOVE #2 motor table load failed: %s\n", sane_strstatus(status));
        DBG(DBG_warning, "Continuing anyway\n");
    }

    /* MOVE #2 command */
    DBG(DBG_info, "MOVE #2: Sending MOVE command\n");
    status = plustek8200i_write_reg(dev->usb_fd, 0x0F, 0x01);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "MOVE #2 failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* Poll for MOVE #2 completion (~334 ms in the capture).  Status goes
     * D5 -> DD -> DC; bit 3 must be set afterwards for MOVE #3 to work, and
     * AGOHOME (bit 2 of reg 0x02 = 0x1C) sets it. */
    DBG(DBG_info, "MOVE #2: Polling for completion (2ms intervals)...\n");
    {
        uint8_t reg01;
        int poll_elapsed = 0;
        int poll_timeout = 15000;  /* 15 seconds — large feed distance */

        while (poll_elapsed < poll_timeout) {
            status = plustek8200i_read_reg(dev->usb_fd, 0x101, &reg01);
            if (status == SANE_STATUS_GOOD) {
                /* Log every 50ms to track status progression */
                if (poll_elapsed % 50 == 0) {
                    DBG(DBG_info, "MOVE #2: r01=0x%02x (%dms) bit3=%d\n",
                        reg01, poll_elapsed, (reg01 >> 3) & 1);
                }
                /* Accept when bit 0 clears (no minimum wait) */
                if ((reg01 & 0x01) == 0) {
                    DBG(DBG_info, "MOVE #2 complete (reg 0x01 = 0x%02x, %dms, bit3=%d)\n",
                        reg01, poll_elapsed, (reg01 >> 3) & 1);
                    break;
                }
            }
            usleep(2000);  /* 2 ms between polls */
            poll_elapsed += 2;
        }

        if (poll_elapsed >= poll_timeout) {
            DBG(DBG_warning, "MOVE #2 timeout (reg 0x01 = 0x%02x)\n", reg01);
            DBG(DBG_warning, "Continuing anyway\n");
        }
    }

    return SANE_STATUS_GOOD;
}

/*
 * Re-run the register/AFE init and cycle the lamp after MOVE #2.
 */
static SANE_Status
scan_reinit_after_move2(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /*
     * Post-MOVE #2 re-initialization: CCD timing 0x10=0x0C, 0x13=0x0C, a
     * full register write with ~20 values changed from init, the AFE init
     * again, then lamp cycling 0x03 = 0x00 -> 0x20 -> 0x30.
     */
    DBG(DBG_info, "=== Post-MOVE #2: Re-initialization ===\n");
    plustek8200i_regtrace_mark("Post-MOVE #2: Re-initialization");

    /* CCD timing writes */
    plustek8200i_write_reg(dev->usb_fd, 0x10, 0x0C);
    plustek8200i_write_reg(dev->usb_fd, 0x13, 0x0C);

    /* Third register pass: a bulk write of 125 pairs, ~20 differing from init */
    DBG(DBG_info, "Third WriteAllReg pass (proper bulk write)\n");
    status = plustek8200i_init_pass3_registers(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Third WriteAllReg failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* WriteAllAfe (same as init) */
    DBG(DBG_info, "Third pass WriteAllAfe\n");
    status = plustek8200i_write_afe_init(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Third pass WriteAllAfe failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* Lamp cycling: read reg 0x01 (hw), write 0x03=0x00, read again, then
     * 0x03=0x20, 0x03=0x30.  A ~12 ms delay follows before MOVE #3, during
     * which reg 0x01 (hw) goes D8 -> DC (bits 2-3 set). */
    DBG(DBG_info, "Lamp cycling: 0x03 = 0x00 → 0x20 → 0x30\n");
    {
        uint8_t val;
        plustek8200i_read_reg(dev->usb_fd, 0x101, &val);
        DBG(DBG_info, "Pre-lamp: reg 0x01 (hw) = 0x%02x (expect 0xD8)\n", val);
        plustek8200i_write_reg(dev->usb_fd, 0x03, 0x00);
        plustek8200i_read_reg(dev->usb_fd, 0x101, &val);
        DBG(DBG_info, "After 0x03=0x00: reg 0x01 (hw) = 0x%02x (expect 0xD8)\n", val);
        plustek8200i_write_reg(dev->usb_fd, 0x03, 0x20);
        plustek8200i_write_reg(dev->usb_fd, 0x03, 0x30);

        /* Settle ~12 ms after lamp activation; reg 0x01 should read 0xDC */
        usleep(15000);  /* 15ms */
        plustek8200i_read_reg(dev->usb_fd, 0x101, &val);
        DBG(DBG_info, "Post-lamp (15ms): reg 0x01 (hw) = 0x%02x (expect 0xDC)\n", val);
        plustek8200i_read_reg(dev->usb_fd, 0x101, &val);
        DBG(DBG_info, "Post-lamp (2nd read): reg 0x01 (hw) = 0x%02x\n", val);
    }

    return SANE_STATUS_GOOD;
}

/*
 * MOVE #3 -- position on the frame and wait for the motor to reach speed.
 */
static SANE_Status
scan_move_to_frame(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /*
     * MOVE #3: positioning to the scan start area, with the scan motor
     * table and AF=0x46 (MOVE #1 uses 0xFF).  Order: registers, DMA motor
     * table, MOVE.
     *
     * The motor must keep running through the scan-phase configuration
     * into MOVE #4.  The capture shows DD -> FD -> FC over ~27 ms with
     * bit 3 set (running); waiting for it to stop (F4) prevents the scan.
     * So poll every 2 ms and proceed when bit 0 clears, whatever bit 3.
     */
    DBG(DBG_info, "=== MOVE #3: Positioning ===\n");
    plustek8200i_regtrace_mark("MOVE #3: Positioning");

    /* Pre-MOVE #3: 12-byte WriteRegs first */
    DBG(DBG_info, "MOVE #3: Pre-MOVE register setup\n");
    {
        static const uint8_t move3_regs[12] = {
            0x02, 0x18, 0xAE, 0x00, 0xAF, 0x46,
            0x3D, 0x00, 0x3E, 0x00, 0x3F, 0x01
        };
        status = plustek8200i_write_regs(dev->usb_fd, move3_regs, sizeof(move3_regs));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "MOVE #3 pre-MOVE WriteRegs failed: %s\n", sane_strstatus(status));
            return status;
        }
    }

    /* Load the scan-phase motor table after the registers, with the raw DMA
     * sequence (setup, bulk write, status read).  The capture does no
     * pre-wait before the setup, which dma_write() would add. */
    DBG(DBG_info, "MOVE #3: Loading scan motor table (raw DMA, no pre-wait)\n");
    {
        extern const uint8_t scan_motor_table[512];
        /* Table 4: 0x1000C000 */
        status = plustek8200i_dma_setup(dev->usb_fd, 0x1000C000, 512, true);
        if (status == SANE_STATUS_GOOD) {
            status = plustek8200i_bulk_write(dev->usb_fd, scan_motor_table, 512);
        }
        if (status == SANE_STATUS_GOOD) {
            status = plustek8200i_dma_wait_ready(dev->usb_fd);
        }
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Scan motor table 0x1000C000 failed: %s\n", sane_strstatus(status));
        }

        /* Table 5: 0x10010000 */
        status = plustek8200i_dma_setup(dev->usb_fd, 0x10010000, 512, true);
        if (status == SANE_STATUS_GOOD) {
            status = plustek8200i_bulk_write(dev->usb_fd, scan_motor_table, 512);
        }
        if (status == SANE_STATUS_GOOD) {
            status = plustek8200i_dma_wait_ready(dev->usb_fd);
        }
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Scan motor table 0x10010000 failed: %s\n", sane_strstatus(status));
            DBG(DBG_warning, "Continuing anyway\n");
        }
    }

    /* MOVE #3 command */
    DBG(DBG_info, "MOVE #3: Sending MOVE command\n");
    status = plustek8200i_write_reg(dev->usb_fd, 0x0F, 0x01);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "MOVE #3 failed: %s\n", sane_strstatus(status));
        return status;
    }

    /* Poll for MOVE #3 at speed (not completion): DD -> FD -> FC over ~27 ms
     * with bit 3 staying set.  Proceed while the motor runs, once bit 0
     * clears.  Polls every 2 ms. */
    DBG(DBG_info, "MOVE #3: Polling for motor at speed (2ms intervals)...\n");
    {
        uint8_t reg01;
        int poll_elapsed = 0;
        int poll_timeout = 5000;  /* 5 second safety timeout */

        while (poll_elapsed < poll_timeout) {
            status = plustek8200i_read_reg(dev->usb_fd, 0x101, &reg01);
            if (status == SANE_STATUS_GOOD) {
                /* Log every 4ms to track status progression without flooding */
                if (poll_elapsed % 4 == 0) {
                    DBG(DBG_info, "MOVE #3: r01=0x%02x (%dms) bit3=%d\n",
                        reg01, poll_elapsed, (reg01 >> 3) & 1);
                }
                /* Bit 0 clear means motor at speed (FC-like state) */
                if ((reg01 & 0x01) == 0) {
                    DBG(DBG_info, "MOVE #3 motor at speed (reg 0x01 = 0x%02x, %dms, bit3=%d)\n",
                        reg01, poll_elapsed, (reg01 >> 3) & 1);
                    break;
                }
            }
            usleep(2000);  /* 2 ms between polls */
            poll_elapsed += 2;
        }

        if (poll_elapsed >= poll_timeout) {
            DBG(DBG_warning, "MOVE #3 timeout (reg 0x01 = 0x%02x)\n", reg01);
            DBG(DBG_warning, "Continuing to scan phase anyway\n");
        }
    }

    return SANE_STATUS_GOOD;
}

/*
 * Switch the ASIC to its scan-phase register configuration.
 */
static SANE_Status
scan_apply_scan_registers(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /*
     * MOVE #4: the actual scan, the first MOVE that produces data.
     * Needs the full scan-phase register reconfiguration, CMD clears
     * (CLRMCNT, CLRLNCNT), SCAN set (reg 0x01 bit 0) and then MOVE.  With
     * SCAN=1 the GL128 captures CCD data as the motor moves: FEEDL is the
     * pre-scan feed, LINCNT the line count, and the speed comes from the
     * line period (0x38-0x39) and motor tables 1-3.  DATAENB (reg 0x40)
     * becomes 1 in scan mode.
     */
    DBG(DBG_info, "=== MOVE #4: Scan (with data capture) ===\n");
    plustek8200i_regtrace_mark("MOVE #4: Scan (with data capture)");

    /* Apply scan-phase register configuration (base values from USB capture) */
    status = plustek8200i_configure_scan_phase(dev->usb_fd);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Scan-phase configuration failed: %s\n", sane_strstatus(status));
        return status;
    }

    /*
     * Calibration scan, between the scan-phase config and the final scan:
     * a short scan (LINCNT=1 from scan_block2) capturing CCD data for
     * shading/dark calibration.
     *   1. Clear counters: 0x0D = 0x05, 0x0D = 0x07
     *   2. SCAN without DVDSET: 0x01 = 0x03
     *   3. MOVE: 0x0F = 0x01, poll reg 0x01 for completion
     *   4. DMA read of 3072 bytes from 0x10000000
     *
     * SCAN is 0x03 (SHDAREA | SCAN), not 0x23: DVDSET (bit 5) enables
     * shading correction, which needs calibration data in SDRAM, and
     * without it DATAENB stays 0.
     */
    DBG(DBG_info, "=== CALIBRATION SCAN ===\n");

    return SANE_STATUS_GOOD;
}

/*
 * Short calibration scan, read back over DMA.
 */
static SANE_Status
scan_calibration_pass(plustek8200i_device_t *dev)
{
    SANE_Status status;

    plustek8200i_regtrace_mark("CALIBRATION SCAN");

    /* Step 1: Clear motor and line counters */
    plustek8200i_write_reg(dev->usb_fd, 0x0D, 0x05);  /* CLRMCNT | CLRLNCNT */
    plustek8200i_write_reg(dev->usb_fd, 0x0D, 0x07);  /* CLRMCNT | CLRDOCJM | CLRLNCNT */

    /* Step 2: SCAN without DVDSET (0x03 = SHDAREA | SCAN, not 0x23) */
    plustek8200i_write_reg(dev->usb_fd, 0x01, 0x03);

    /* Step 3: MOVE command for calibration scan */
    DBG(DBG_info, "CALIBRATION: Sending MOVE (SCAN=1, no DVDSET)\n");
    plustek8200i_write_reg(dev->usb_fd, 0x0F, 0x01);

    /* Step 4: poll for completion (~30 ms, CD -> ED -> AD -> 00) */
    {
        uint8_t cal_r01;
        int cal_polls = 0;
        int cal_timeout = 5000;  /* 5 second safety timeout */

        for (cal_polls = 0; cal_polls < cal_timeout; cal_polls += 2) {
            plustek8200i_read_reg(dev->usb_fd, 0x101, &cal_r01);
            if (cal_polls % 10 == 0) {
                DBG(DBG_info, "CAL poll: r01=0x%02x (%dms) [MOTORENB=%d HOMESNR=%d FEEDFSH=%d BUFEMPTY=%d]\n",
                    cal_r01, cal_polls,
                    cal_r01 & 1, (cal_r01 >> 3) & 1,
                    (cal_r01 >> 5) & 1, (cal_r01 >> 6) & 1);
            }
            /* Check DATAENB during calibration scan */
            if (cal_polls == 4) {
                uint8_t r40;
                plustek8200i_read_reg(dev->usb_fd, 0x140, &r40);
                DBG(DBG_info, "CAL DATAENB check: reg 0x40 hw=0x%02x [DATAENB=%d MOTMFLG=%d]\n",
                    r40, r40 & 1, (r40 >> 1) & 1);
            }
            /* Accept when MOTORENB clears (bit 0 = 0) */
            if ((cal_r01 & 0x01) == 0) {
                DBG(DBG_info, "CALIBRATION complete (r01=0x%02x, %dms)\n",
                    cal_r01, cal_polls);
                break;
            }
            usleep(2000);
        }
        if (cal_polls >= cal_timeout) {
            DBG(DBG_warning, "CALIBRATION timeout (r01=0x%02x)\n", cal_r01);
        }
    }

    /* Step 5-6: DMA read of the 3072 bytes (0x0C00) of CCD calibration data;
     * read and discarded for now (TODO: use it for shading correction). */
    {
        uint8_t cal_data[3072];
        size_t cal_len = sizeof(cal_data);

        DBG(DBG_info, "CALIBRATION: Reading %zu bytes from 0x10000000\n", cal_len);
        status = plustek8200i_dma_wait_ready(dev->usb_fd);
        if (status == SANE_STATUS_GOOD) {
            status = plustek8200i_dma_setup(dev->usb_fd, 0x10000000, cal_len, false);
        }
        if (status == SANE_STATUS_GOOD) {
            status = plustek8200i_bulk_read(dev->usb_fd, cal_data, cal_len);
        }
        if (status == SANE_STATUS_GOOD) {
            /* Check if calibration data is non-zero (confirms DATAENB worked) */
            int nonzero = 0;
            size_t i;
            for (i = 0; i < cal_len && nonzero < 20; i += 2) {
                uint16_t pixel = cal_data[i] | (cal_data[i+1] << 8);
                if (pixel != 0) nonzero++;
            }
            DBG(DBG_info, "CALIBRATION: Read %zu bytes, %d non-zero pixels "
                "(first 4: %02x%02x %02x%02x %02x%02x %02x%02x)\n",
                cal_len, nonzero,
                cal_data[0], cal_data[1], cal_data[2], cal_data[3],
                cal_data[4], cal_data[5], cal_data[6], cal_data[7]);
        } else {
            DBG(DBG_warning, "CALIBRATION: DMA read failed: %s (continuing)\n",
                sane_strstatus(status));
        }
    }

    return SANE_STATUS_GOOD;
}

/*
 * Load the constant-speed scan motor tables and set lamp power.
 */
static SANE_Status
scan_load_scan_motor_tables(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /* Load scan motor tables 1-3 (constant speed).  The GL128 uses them when
     * FASTFED=0; tables 4-5 were loaded at init.  Without them MOVE #4 runs
     * on empty tables and yields 0 valid words.  The capture writes them
     * between the scan-phase configuration and the scan MOVE. */
    status = plustek8200i_load_scan_motor_table(dev->usb_fd,
                 plustek8200i_vdpi_period(
                     plustek8200i_target_vdpi(dev->opt_resolution)));
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Scan motor table loading failed: %s\n", sane_strstatus(status));
        return status;
    }

    /*
     * MOVE #4: scan with LPERIOD matched to the motor speed.  Keeping the
     * calibration registers left LPERIOD (0x4400 from init) out of step
     * with DPISET, so the motor crossed the film faster than the CCD
     * captured lines (~400 of 1134 lines valid).
     *   effective_vertical_dpi = motor_speed * MOTORDPI / LPERIOD
     * so LPERIOD = motor_speed * MOTORDPI / DPISET.  Motor control
     * registers (STEPNO, FWDSTEP, BWDSTEP) and MAXWD are set to match.
     */
    DBG(DBG_info, "MOVE #4: Scan setup with motor/CCD sync\n");

    /* Lamp power control: 0x03=0x30 before the scan */
    plustek8200i_write_reg(dev->usb_fd, 0x03, 0x30);

    /*
     * Reset GL128 scan state machine to accept new scan.
     * After calibration scan, transition from post-scan to ready state.
     */
    plustek8200i_write_reg(dev->usb_fd, 0x01, 0x02);  /* Clear SCAN, keep SHDAREA */

    /* Clear interrupt registers */
    {
        static const uint8_t clear_interrupts[] = {
            0x8A, 0x00, 0x8B, 0x00, 0x8C, 0x00,
            0x8D, 0x00, 0x8E, 0x00, 0x8F, 0x00,
            0x90, 0x00, 0x91, 0x00, 0x92, 0x00,
        };
        status = plustek8200i_write_regs(dev->usb_fd, clear_interrupts,
                                          sizeof(clear_interrupts));
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "MOVE #4: clear interrupts failed\n");
            return status;
        }
    }

    return SANE_STATUS_GOOD;
}

/*
 * Compute the scan geometry for the requested mode and program every
 * image-pass register from it.
 */
static SANE_Status
scan_program_image_pass(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /*
     * Write LINCNT, FEEDL, MAXWD and the image buffers for the scan,
     * keeping DPISET and LPERIOD at their init values.
     *
     * MAXWD must not be the calibration value (0x2F04C0 for a different
     * scan mode) unless it is the flow-control threshold described below;
     * the image buffers (0xE0-0xF7) must hold the scan data (calibration
     * buffers are ~4 MB, ~406 lines; 1134 lines at 10206 bytes need ~11.6 MB).
     */
    {
        /* The hardware always reads the full window; the scan-area
         * options are applied by cropping at the end of sane_start.
         * 36 x 24 mm is the nominal frame the geometry was tuned for --
         * the margins added below make the delivered window larger. */
        double height_mm = PLUSTEK8200I_FRAME_H_MM;
        /* Tell the register layer whether this is an infrared pass. */
        plustek8200i_set_infrared(dev->opt_mode == MODE_INFRARED);

        /*
         * At DPISET=100 the sensor emits 864 px per line, measured on the
         * wire in the Windows capture and ours (2592 samples = 864 px x 3
         * channels), spanning the 1.44" frame, i.e. 600 dpi.  DPISET and
         * this width are a matched pair; re-measure before changing either.
         */
        /* DPISET is dpi/6 and the line dpi*1.44 px (1.44 inches of frame); both
         * measured, see resolution_list above. */
        int req_dpi = dev->opt_resolution;
        if (req_dpi != 300 && req_dpi != 600 && req_dpi != 1200 &&
            req_dpi != 2400 && req_dpi != 3600 && req_dpi != 7200)
            req_dpi = 600;
        /* Above the default vertical sampling the motor is slowed so the extra
         * lines exist; below it the motor keeps the known-good speed and the
         * surplus is downsampled. */
        int pixel_dpi = req_dpi;
        int line_dpi = 1200;    /* Geometry DPI for height */
        int hw_dpiset = req_dpi / 6;  /* DPISET */
        int lincnt, feedl;
        int output_lines, raw_lines;
        int scan_pixels, scan_line_bytes;
        /* The sensor has no single-channel readout: it always clocks out three
         * interleaved channels, and Gray/Infrared are averaged afterwards. */
        int channels = 3;
        int bytes_per_sample = 2;  /* CCD always outputs 16-bit */
        uint32_t maxwd;
        int color_shift_g, color_shift_b;

        /* The sensor always emits dpi*1.44 px per line (the full 1.44 inch frame,
         * a few pixels more than the nominal 36.0 mm), so use that measured
         * width to make the line arithmetic and SANE parameters match. */
        scan_pixels = (req_dpi * 36) / 25;      /* dpi * 1.44 */
        scan_line_bytes = scan_pixels * bytes_per_sample * channels;

        /* Rounding scan_pixels up to even (as the vendor driver does for a
         * sensor width table value, a different quantity from mm x dpi)
         * returned 11,580,408 bytes of zeroes.  Do not. */

        /* Output lines at 1200 DPI to preserve motor timing */
        output_lines = (int)(height_mm * line_dpi / 25.4 + 0.5);
        if (output_lines < 1) output_lines = 1;

        /* No line-level colour shift: the device returns contiguous RGB
         * triples.  Only genesys-style CCD registers (0x70=0x0A) would need
         * ld_shift_g=12, ld_shift_b=24. */
        color_shift_g = 0;
        color_shift_b = 0;

        /*
         * Lines to read must equal the lines the device emits (LINCNT), not
         * the nominal output_lines; a 2x error here once cancelled against
         * a wrong line_bytes (10206 vs the true 5184).  The vertical
         * sampling is ~2.2x the horizontal (~1300 lines/inch against 600
         * px/inch): hardware behaviour, the same 1303 rows x 864 px as
         * Windows, resampled in sane_read.
         */
        /* Lines to capture: the frame height at the vertical sampling rate plus
         * a margin at each end.  (Scaling output_lines * 2 asked for 2268
         * lines to hold a 601-row frame.) */
        /* Shared with sane_get_parameters() so the pre-scan line count
         * cannot drift from the number of rows actually delivered. */
        raw_lines = plustek8200i_raw_frame_lines(req_dpi);

        DBG(DBG_info, "MOVE #4: pixel_dpi=%d line_dpi=%d (DPISET=%d), color_shift G=%d B=%d, "
            "raw_lines=%d\n", pixel_dpi, line_dpi, hw_dpiset, color_shift_g, color_shift_b, raw_lines);

        /* Store in device struct for sane_read */
        dev->color_shift_g = color_shift_g;
        dev->color_shift_b = color_shift_b;
        dev->raw_scan_lines = raw_lines;
        dev->scan_pixels = scan_pixels;
        dev->scan_line_bytes = scan_line_bytes;

        /* LINCNT is exactly the lines read; the CCD and USB rates match (~51
         * lines/s), so there is no FIFO backpressure to allow for. */
        lincnt = raw_lines;      /* read exactly what the device emits */
        dev->img_lincnt = lincnt;
        if (lincnt > 0xFFFFF) lincnt = 0xFFFFF;

        /*
         * FEEDL: motor feed to the scan start, in quarter-steps at MOTORDPI
         * (3600 * 4 = 14400 per inch, i.e. 24 steps per output row at
         * 600 dpi).  The film frame is not at the top of the holder: the
         * vendor configuration puts it at y = 6750 of UNITDPI 7200, i.e.
         * 3375 MOTORDPI steps; the motor is ~325 steps from home after
         * MOVEs 1-3.  Without this offset the CCD scans the opaque holder.
         */
        {
            /* Measured: FEEDL 10000 left 270 lead-in rows and 13000 left 145 at
             * 600 dpi, so 24 steps per row.  10000 wasted ~30% of every scan
             * on dark lead-in; 15880 puts the frame start ~25 rows in, a
             * margin so the top is never clipped. */
            int base_feedl = 15380;  /* re-centred after the MOVE #1 fix */
            if (base_feedl < 1) base_feedl = 1;
            feedl = base_feedl;
            dev->img_feedl = feedl;
        }
        if (feedl < 1) feedl = 1;
        if (feedl > 0xFFFFF) feedl = 0xFFFFF;

        DBG(DBG_info, "MOVE #4: pixel_dpi=%d (DPISET=%d), output_lines=%d, hw_LINCNT=%d (0x%05X), FEEDL=%d\n",
            pixel_dpi, hw_dpiset, output_lines, lincnt, lincnt, feedl);
        DBG(DBG_info, "MOVE #4: scan_pixels=%d, line_bytes=%d, channels=%d\n",
            scan_pixels, scan_line_bytes, channels);

        /* MAXWD (regs 0x35-0x37) is a buffer flow-control threshold, not a line
         * stride: "buffer full" when free space < 2*MAXWD (4-word units).
         * Too small and the CCD never captures (all zeros); a line-sized
         * value (sane-genesys: (line_bytes * channels) >> 2) does not work
         * here.  The init value 0x2F04C0 works for all configurations. */
        maxwd = 0x2F04C0;

        DBG(DBG_info, "MOVE #4: MAXWD=0x%06X (init default, line_bytes=%d)\n",
            maxwd, scan_line_bytes);

        {
            uint8_t scan_regs[48];
            int idx = 0;
            unsigned exposure = (unsigned)(PLUSTEK8200I_BASE_EXPOSURE
                                * SANE_UNFIX(dev->opt_exposure) + 0.5);

            /* Exposure / line period: 0x7D-0x7F (24-bit). Scaled with the
             * scan table so the geometry stays put; see the header. */
            scan_regs[idx++] = 0x7D;
            scan_regs[idx++] = (exposure >> 16) & 0xFF;
            scan_regs[idx++] = 0x7E;
            scan_regs[idx++] = (exposure >> 8) & 0xFF;
            scan_regs[idx++] = 0x7F;
            scan_regs[idx++] = exposure & 0xFF;
            DBG(DBG_info, "MOVE #4: exposure %u (x%.2f)\n",
                exposure, SANE_UNFIX(dev->opt_exposure));

            /* LINCNT: 0x25-0x27 (20-bit) */
            scan_regs[idx++] = REG_LINCNT_H;
            scan_regs[idx++] = (lincnt >> 16) & 0x0F;
            scan_regs[idx++] = REG_LINCNT_M;
            scan_regs[idx++] = (lincnt >> 8) & 0xFF;
            scan_regs[idx++] = REG_LINCNT_L;
            scan_regs[idx++] = lincnt & 0xFF;

            /* DPISET: 0x2C-0x2D (12-bit)
             * From sane-genesys sensor table: 3600 DPI → DPISET=600 */
            scan_regs[idx++] = REG_DPISET_H;
            scan_regs[idx++] = (hw_dpiset >> 8) & 0x0F;
            scan_regs[idx++] = REG_DPISET_L;
            scan_regs[idx++] = hw_dpiset & 0xFF;

            /* MAXWD: 0x35-0x37 (24-bit) — SDRAM line stride in bytes.
             * MUST match actual CCD line data size. If too large, each
             * SDRAM line has huge padding, causing diagonal stripe artifacts
             * when reading continuous data from the FIFO. */
            scan_regs[idx++] = 0x35;
            scan_regs[idx++] = (maxwd >> 16) & 0xFF;
            scan_regs[idx++] = 0x36;
            scan_regs[idx++] = (maxwd >> 8) & 0xFF;
            scan_regs[idx++] = 0x37;
            scan_regs[idx++] = maxwd & 0xFF;

            /* FEEDL: 0x3D-0x3F (20-bit) */
            scan_regs[idx++] = REG_FEEDL_H;
            scan_regs[idx++] = (feedl >> 16) & 0x0F;
            scan_regs[idx++] = REG_FEEDL_M;
            scan_regs[idx++] = (feedl >> 8) & 0xFF;
            scan_regs[idx++] = REG_FEEDL_L;
            scan_regs[idx++] = feedl & 0xFF;

            status = plustek8200i_write_regs(dev->usb_fd, scan_regs, idx);
            if (status != SANE_STATUS_GOOD) {
                DBG(DBG_error, "MOVE #4: LINCNT/DPISET/MAXWD/FEEDL write failed\n");
                return status;
            }
        }

        /*
         * Verified CCD timing/clock/segment registers. Written AFTER the
         * block above so they win over anything set during init, and so
         * the final state matches the Windows capture exactly.
         */
        status = plustek8200i_apply_verified_ccd_regs(dev->usb_fd);
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "MOVE #4: verified CCD register write failed\n");
            return status;
        }

        /*
         * 7200 dpi: the 10368-pixel line needs SilverFast's line timing
         * (from its resolution ladder as tabulated by the pyopticfilm
         * project): LPERIOD 0x28-0x2A = 15963 (11067 otherwise), dummy
         * clock 0x2B = 0x17, pixel clocks 0xA5/0xAB = 0x01.  Written after
         * the CCD profile, which also sets 0x2A/0x2B.  Without them the
         * first bulk read failed and the scanner dropped off the bus.
         */
        if (dev->opt_resolution == 7200) {
            static const uint8_t t7200[] = {
                0x28, (15963 >> 16) & 0xFF, 0x29, (15963 >> 8) & 0xFF,
                0x2A, 15963 & 0xFF, 0x2B, 0x17, 0xA5, 0x01, 0xAB, 0x01,
            };
            plustek8200i_write_regs(dev->usb_fd, t7200, sizeof(t7200));
        }

        DBG(DBG_info, "MOVE #4: CCD profile: verified\n");

        /*
         * Image buffers (0xE0-0xF7) keep the calibration/init values.  The
         * SDRAM is 4 MB and they already span 0x00A8-0x3FFB; larger
         * addresses wrap and corrupt.  The 4 MB FIFO is circular with GL128
         * backpressure (the CCD stalls until USB drains), limiting unique
         * data to ~400 lines at the init line rate, hence the slower
         * LPERIOD above.
         */
        DBG(DBG_info, "MOVE #4: Image buffers: keeping init values (4 MB SDRAM)\n");

        /*
         * Motor control for scan movement.  Five slope tables in SRAM:
         * 0 SCAN, 1 BACKTRACK, 2 STOP, 3 FAST (positioning), 4 HOME.  Init
         * loads only 3-4; for scanning, 0-2 are loaded with a constant
         * period (7000, 0x1B58 in the captured scan, far slower than the
         * fast tables ending at ~294) and the motor registers set to match.
         * (A "~24x too fast, 50% coverage" note in history was really the
         * raw_lines/LINCNT mismatch, not the motor.)
         */
        dev->img_table_period = (int)(plustek8200i_vdpi_period(
                     plustek8200i_target_vdpi(dev->opt_resolution))
                       * SANE_UNFIX(dev->opt_exposure) + 0.5);
        status = plustek8200i_load_scan_motor_table(dev->usb_fd,
                                                    dev->img_table_period);
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "MOVE #4: Scan motor table loading failed\n");
            return status;
        }

        /* Motor power and mode */
        plustek8200i_write_reg(dev->usb_fd, REG_MTRPWR, 0x30);

        /*
         * Motor step registers, following sane-genesys' setup for related ASICs:
         *   STEPNO  (0x21)  table_size / step_multiplier = 256 / 16 = 16, with
         *                   multiplier 1 << ((reg 0x9D & 0x0F) >> 1) = 16 for 0x08
         *   FWDSTEP (0x22)  table_size / 2 - 1 = 127: acceleration steps replayed
         *                   when restarting after a FIFO backpressure stall
         *   BWDSTEP (0x23)  as FWDSTEP
         *   FASTNO  (0x24), FSHDEC (0x69)  as STEPNO
         *   FMOVNO  (0x6A), FMOVDEC (0x5F) from init (homing table)
         */
        {
            int table_entries = 256;  /* motor table entries */
            int step_multiplier = 16; /* from reg 0x9D=0x08: 1<<((8&0xF)>>1) */
            int stepno = table_entries / step_multiplier;  /* 256/16 = 16 */
            int restep = table_entries / 2 - 1; /* 127 */

            plustek8200i_write_reg(dev->usb_fd, 0x21, stepno);   /* STEPNO */
            plustek8200i_write_reg(dev->usb_fd, 0x22, restep);   /* FWDSTEP */
            plustek8200i_write_reg(dev->usb_fd, 0x23, restep);   /* BWDSTEP */
            plustek8200i_write_reg(dev->usb_fd, 0x24, stepno);   /* FASTNO */
            plustek8200i_write_reg(dev->usb_fd, 0x69, stepno);   /* FSHDEC */

            DBG(DBG_info, "MOVE #4: Motor steps: STEPNO=0x%02X FWDSTEP=0x%02X "
                "BWDSTEP=0x%02X FASTNO=0x%02X FSHDEC=0x%02X\n",
                stepno, restep, restep, stepno, stepno);
        }

        /*
         * Step type: REG 0x60 bits[7:5] STEPSEL (scan) and REG 0x63 bits[7:5]
         * FSTPSEL (fast) use 0x00 full, 0x20 half, 0x40 quarter, 0x60 eighth.
         * Quarter step, as the sane-genesys profile: 1/14400 inch per
         * micro-step; period 1450 -> 17408 / 1450 = 12 micro-steps per line
         * -> 14400 / 12 = 1200 dpi.  Z1MOD/Z2MOD (bits[4:0]) phase sync
         * values stay 0.
         */
        /*
         * STEPSEL = quarter step; do not change it alone.  Eighth stepping
         * (0x60) made the motor grind until a power cycle: the slope tables
         * from plustek8200i_load_scan_motor_table() are matched to quarter
         * stepping, and halving the step asks for a rate the motor cannot
         * follow.  Change STEPSEL, the slope tables and LINCNT together.
         */
        plustek8200i_write_reg(dev->usb_fd, 0x60, 0x40);  /* STEPSEL=quarter, Z1MOD=0 */
        plustek8200i_write_reg(dev->usb_fd, 0x61, 0x00);  /* Z1MOD mid */
        plustek8200i_write_reg(dev->usb_fd, 0x62, 0x00);  /* Z1MOD low */
        plustek8200i_write_reg(dev->usb_fd, 0x63, 0x40);  /* FSTPSEL=quarter, Z2MOD=0 */
        plustek8200i_write_reg(dev->usb_fd, 0x64, 0x00);  /* Z2MOD mid */
        plustek8200i_write_reg(dev->usb_fd, 0x65, 0x00);  /* Z2MOD low */

        /* Motor voltage reference (REG 0x80), sane-genesys motor_vref = 3.
         * Bits [7:6] TABLE5, [5:4] TABLE4, [3:2] TABLE2, [1:0] TABLE1; all
         * at 3 gives 0xFF. */
        plustek8200i_write_reg(dev->usb_fd, 0x80, 0xFF);

        /* Motor current control */
        plustek8200i_write_reg(dev->usb_fd, 0x67, 0x7F);
        plustek8200i_write_reg(dev->usb_fd, 0x68, 0x7F);

        DBG(DBG_info, "MOVE #4: Motor configured: STEPSEL=quarter(0x40), VREF=0xFF, "
            "scan tables 0-2 loaded, LINCNT=%d (×40)\n", lincnt);

        /* Update SANE parameters for sane_read.
         * Use output_lines (real lines to capture), not lincnt (inflated HW value). */
        dev->params.pixels_per_line = scan_pixels;
        dev->params.lines = output_lines;
        dev->params.depth = (dev->opt_bit_depth == 8) ? 8 : 16;
        dev->params.format = (dev->opt_mode == MODE_COLOR)
                           ? SANE_FRAME_RGB : SANE_FRAME_GRAY;
        dev->params.last_frame = SANE_TRUE;

        if (dev->opt_bit_depth == 8) {
            dev->params.bytes_per_line = scan_pixels * 1 * channels;
        } else {
            dev->params.bytes_per_line = scan_line_bytes;
        }

        /* Initial bytes_total for raw scan. Desegmentation (post-read) will
         * update this, params.pixels_per_line, and params.bytes_per_line. */
        dev->bytes_total = (size_t)scan_line_bytes * output_lines;
        {
            size_t raw_bytes_total = (size_t)scan_line_bytes * raw_lines;

            /* Allocate image buffer for full raw scan. A streamed pass
             * gets a ring of raw lines in stream_begin instead. */
            free(dev->image_buffer);
            dev->image_buffer = NULL;
            dev->buffer_size = 0;
            if (!stream_wanted(dev)) {
                dev->image_buffer = calloc(1, raw_bytes_total);
                if (!dev->image_buffer) {
                    DBG(DBG_error, "MOVE #4: failed to allocate %zu bytes for image buffer\n",
                        raw_bytes_total);
                    return SANE_STATUS_NO_MEM;
                }
                dev->buffer_size = raw_bytes_total;
                DBG(DBG_info, "MOVE #4: allocated %zu bytes for image buffer "
                    "(%d raw lines x %d bytes/line)\n",
                    raw_bytes_total, raw_lines, scan_line_bytes);
            }
        }

        DBG(DBG_info, "MOVE #4: params: %dx%d, %d bytes/line, depth=%d, total=%zu\n",
            dev->params.pixels_per_line, dev->params.lines,
            dev->params.bytes_per_line, dev->params.depth, dev->bytes_total);
        DBG(DBG_info, "MOVE #4: hw_lincnt=%d (2×%d), raw_lines=%d, bytes_total=%zu\n",
            lincnt, raw_lines, raw_lines, dev->bytes_total);
    }

    return SANE_STATUS_GOOD;
}

/*
 * Shading calibration, the infrared reference, and the DRYRUN stop.
 */
static SANE_Status
scan_shading_and_reference(plustek8200i_device_t *dev)
{
    SANE_Status status;

    /* Log key register values for diagnostics */
    {
        uint8_t r35, r36, r37, r38, r39, r2c, r2d, r21, r22, r23, r02;
        uint8_t r30, r31, r32, r33, r34;
        uint8_t r52;
        uint8_t r70, r71, r72, r73;
        plustek8200i_read_reg(dev->usb_fd, 0x35, &r35);
        plustek8200i_read_reg(dev->usb_fd, 0x36, &r36);
        plustek8200i_read_reg(dev->usb_fd, 0x37, &r37);
        plustek8200i_read_reg(dev->usb_fd, 0x38, &r38);
        plustek8200i_read_reg(dev->usb_fd, 0x39, &r39);
        plustek8200i_read_reg(dev->usb_fd, 0x2C, &r2c);
        plustek8200i_read_reg(dev->usb_fd, 0x2D, &r2d);
        plustek8200i_read_reg(dev->usb_fd, 0x21, &r21);
        plustek8200i_read_reg(dev->usb_fd, 0x22, &r22);
        plustek8200i_read_reg(dev->usb_fd, 0x23, &r23);
        plustek8200i_read_reg(dev->usb_fd, 0x02, &r02);
        plustek8200i_read_reg(dev->usb_fd, 0x30, &r30);
        plustek8200i_read_reg(dev->usb_fd, 0x31, &r31);
        plustek8200i_read_reg(dev->usb_fd, 0x32, &r32);
        plustek8200i_read_reg(dev->usb_fd, 0x33, &r33);
        plustek8200i_read_reg(dev->usb_fd, 0x34, &r34);
        plustek8200i_read_reg(dev->usb_fd, 0x52, &r52);
        plustek8200i_read_reg(dev->usb_fd, 0x70, &r70);
        plustek8200i_read_reg(dev->usb_fd, 0x71, &r71);
        plustek8200i_read_reg(dev->usb_fd, 0x72, &r72);
        plustek8200i_read_reg(dev->usb_fd, 0x73, &r73);
        DBG(DBG_info, "MOVE #4 verify: MAXWD=0x%02X%02X%02X LPERIOD=0x%02X%02X DPISET=0x%02X%02X\n",
            r35, r36, r37, r38, r39, r2c, r2d);
        DBG(DBG_info, "MOVE #4 verify: STEPNO=0x%02X FWDSTEP=0x%02X BWDSTEP=0x%02X REG02=0x%02X\n",
            r21, r22, r23, r02);
        DBG(DBG_info, "MOVE #4 verify: STRPIXEL=0x%02X%02X ENDPIXEL=0x%02X%02X DUMMY=0x%02X\n",
            r30, r31, r32, r33, r34);
        {   /* read back the whole CCD/AFE timing block we just wrote */
            int rb;
            char line[256]; int pos = 0;
            for (rb = 0x51; rb <= 0x5F; rb++) {
                uint8_t v = 0;
                plustek8200i_read_reg(dev->usb_fd, rb, &v);
                pos += snprintf(line + pos, sizeof(line) - pos,
                                "%02X:%02X ", rb, v);
            }
            DBG(DBG_info, "MOVE #4 readback 0x51-0x5F: %s\n", line);
        }
        DBG(DBG_info, "MOVE #4 verify: CCD 0x52=0x%02X 0x70=0x%02X 0x71=0x%02X 0x72=0x%02X 0x73=0x%02X\n",
            r52, r70, r71, r72, r73);
    }

    /*
     * CCD registers: the init values are kept.  Plain sane-genesys CCD
     * registers are incompatible with this init sequence (R/G/B cycling
     * garbage); the verified set from plustek8200i_apply_verified_ccd_regs()
     * gives contiguous RGB.
     */

    /*
     * Hardware shading: dark and white strips with the carriage at home,
     * taken after every image register is set so they match the image's
     * scan mode, pixel window and DPISET.  Restores LINCNT, FEEDL, motor
     * mode, lamp and the scan table afterwards.  Not for the infrared
     * pass: DVDSET clips IR.
     */
    dev->shading_ok = 0;
    dev->ir_scale = 1.0;
    if (dev->opt_mode == MODE_INFRARED || plustek8200i_use_infrared()) {
        plustek8200i_ir_reference(dev->usb_fd, dev->scan_pixels,
            dev->img_lincnt, dev->img_feedl, dev->img_table_period,
            (int)(PLUSTEK8200I_BASE_EXPOSURE * SANE_UNFIX(dev->opt_exposure) + 0.5),
            &dev->ir_scale);
    }
    dev->shading_ok = 0;
    if (dev->opt_mode != MODE_INFRARED && !plustek8200i_use_infrared()) {
        {
            plustek8200i_shading_stats_t sst;
            SANE_Status shst = plustek8200i_shading_calibrate(dev->usb_fd,
                                   dev->scan_pixels, dev->img_lincnt,
                                   dev->img_feedl, dev->img_table_period,
                                   (int)(PLUSTEK8200I_BASE_EXPOSURE
                                         * SANE_UNFIX(dev->opt_exposure) + 0.5),
                                   1, &sst);
            dev->shading_ok = (shst == SANE_STATUS_GOOD);
            DBG(DBG_info, "SHADING: %s\n", dev->shading_ok
                ? "table loaded, image pass runs with DVDSET" : "not applied");
        }
    }

    /*
     * Positioning feed.  Instead of reaching the frame with the image
     * FEEDL at scan speed (9 s at 600 dpi, 57 s at 7200), move there with
     * a fast feed as SilverFast does and start the image with FEEDL = 1.
     * An image FEEDL of F lands where a feed of F - 520 does (a 14860-step
     * feed + FEEDL 1 matches FEEDL 15380 to 0.1 row at 300-7200 dpi).
     * Uses SilverFast's fast ramp (its slow one took 7.7 s here).
     * Registers and our motor table are restored afterwards.
     */
    {
        int steps = dev->img_feedl - PLUSTEK8200I_FEED_LEAD;
        if (steps >= 1000 && steps <= PLUSTEK8200I_FEED_MAX) {
            extern const uint8_t scan_motor_table[512];
            static const uint8_t feedl1[] = { 0x3D, 0x00, 0x3E, 0x00, 0x3F, 0x01 };
            plustek8200i_feed_saved_t sv;
            status = plustek8200i_feed_save(dev->usb_fd, &sv);
            if (status == SANE_STATUS_GOOD)
                status = plustek8200i_feed(dev->usb_fd, steps, 0);
            plustek8200i_feed_restore(dev->usb_fd, &sv);
            plustek8200i_dma_write(dev->usb_fd, 0x1000C000, scan_motor_table, 512);
            plustek8200i_dma_write(dev->usb_fd, 0x10010000, scan_motor_table, 512);
            if (status != SANE_STATUS_GOOD) {
                DBG(DBG_error, "FEED: positioning feed failed: %s\n", sane_strstatus(status));
                return status;
            }
            plustek8200i_write_regs(dev->usb_fd, feedl1, sizeof(feedl1));
        }
    }

    /* Counter clear (same pattern as working calibration scan) */
    DBG(DBG_info, "MOVE #4: Clearing counters\n");
    plustek8200i_write_reg(dev->usb_fd, 0x0D, 0x05);  /* CLRMCNT | CLRLNCNT */
    plustek8200i_write_reg(dev->usb_fd, 0x0D, 0x07);  /* CLRMCNT | CLRDOCJM | CLRLNCNT */

    /* Enable SCAN: 0x03 = SHDAREA | SCAN, plus DVDSET (0x20) when the
     * shading table was loaded. */
    DBG(DBG_info, "MOVE #4: Enable SCAN (0x%02x)\n", dev->shading_ok ? 0x23 : 0x03);
    plustek8200i_write_reg(dev->usb_fd, 0x01, dev->shading_ok ? 0x23 : 0x03);

    return SANE_STATUS_GOOD;
}

/*
 * Start the image pass and deliver it: streamed, or read whole and
 * post-processed.
 */
/*
 * Average the three sensor channels down to one, for Gray and Infrared.
 */
static SANE_Status
image_reduce_to_one_channel(plustek8200i_device_t *dev)
{
    /* Single-channel output for Gray and Infrared: the sensor has no
     * one-channel readout, so average the three channels (noise reduction
     * for IR, a luminance mix for Gray). */
    if (dev->opt_mode != MODE_COLOR) {
        int px = dev->scan_pixels;
        int lines = dev->raw_scan_lines;
        int out_line = px * 2;
        size_t total = (size_t)out_line * lines;
        uint8_t *gbuf = malloc(total);
        int y, x;

        if (!gbuf) {
            free(dev->image_buffer);
            dev->image_buffer = NULL;
            return SANE_STATUS_NO_MEM;
        }
        for (y = 0; y < lines; y++) {
            uint16_t *src = (uint16_t *)(dev->image_buffer +
                            (size_t)y * dev->scan_line_bytes);
            uint16_t *dst = (uint16_t *)(gbuf + (size_t)y * out_line);
            if (dev->opt_mode == MODE_INFRARED) {
                /* mean of the three channels, normalised to the open IR
                 * light (dev->ir_scale, see plustek8200i_ir_reference) */
                for (x = 0; x < px; x++) {
                    double v = (src[x * 3] + src[x * 3 + 1] + src[x * 3 + 2])
                               / 3.0 * dev->ir_scale;
                    dst[x] = v > 65535.0 ? 65535 : (uint16_t)(v + 0.5);
                }
            } else {
                for (x = 0; x < px; x++)
                    dst[x] = (uint16_t)((src[x * 3] * 299u +
                                         src[x * 3 + 1] * 587u +
                                         src[x * 3 + 2] * 114u) / 1000u);
            }
        }
        free(dev->image_buffer);
        dev->image_buffer = gbuf;
        dev->buffer_size = total;
        dev->scan_line_bytes = out_line;
        dev->params.bytes_per_line = out_line;
        dev->params.format = SANE_FRAME_GRAY;
        dev->bytes_total = total;
        DBG(DBG_info, "%s: averaged to 1 channel, %d bytes/line\n",
            dev->opt_mode == MODE_INFRARED ? "Infrared" : "Gray",
            out_line);
    }

    return SANE_STATUS_GOOD;
}

/*
 * Repair the colour image from the infrared defect map.
 */
static void
image_apply_ir_repair(plustek8200i_device_t *dev)
{
    /* Apply the infrared defect map.  Dye is transparent to IR, so dust, hair
     * and scratches read dark; pixels well below the local IR level are
     * replaced by horizontal interpolation, which suits specks and thin
     * scratches.  Wide damage is left alone rather than smeared. */
    if (dev->ir_map && dev->opt_mode == MODE_COLOR && dev->image_buffer) {
        int px = dev->scan_pixels;
        int lines = dev->raw_scan_lines;
        size_t need = (size_t)px * 2 * lines;
        int y;

        if (dev->ir_map_size < need) {
            DBG(DBG_info, "IR clean: map is %zu bytes, need %zu -- "
                "geometry differs, skipping repair\n",
                dev->ir_map_size, need);
        } else {
            if (plustek8200i_ir_prepare(dev, lines) == 0) {
                for (y = dev->ir_r0; y < dev->ir_r1; y++)
                    plustek8200i_ir_repair_row(dev, y, (uint16_t *)(dev->image_buffer +
                                               (size_t)y * dev->scan_line_bytes));
                plustek8200i_ir_report(dev);
            }
        }
        free(dev->ir_map);
        dev->ir_map = NULL;
        dev->ir_map_size = 0;
    }
}

/*
 * Cut the requested rectangle out of the delivered full window.
 */
static void
image_crop_to_scan_area(plustek8200i_device_t *dev)
{
    /*
     * Scan area: cut the requested rectangle out of the full window.
     * Skipped on the infrared pre-pass of --ir-clean, whose defect map
     * must cover the whole window the colour pass delivers.
     */
    if (!dev->in_ir_pass && dev->image_buffer) {
        int fw = dev->params.pixels_per_line, fh = dev->params.lines;
        int x0, y0, x1, y1;

        plustek8200i_crop_rect(dev, fw, fh, &x0, &y0, &x1, &y1);
        if (x0 != 0 || y0 != 0 || x1 != fw || y1 != fh) {
            /* The buffer is always 16-bit; with --depth 8 sane_read
             * halves it on delivery. So crop by the buffer's own line
             * length, and give the frontend the delivered one. */
            size_t buf_line = (size_t)dev->scan_line_bytes;
            size_t bpp = buf_line / fw;
            size_t out_line = bpp * (x1 - x0);
            int y;

            for (y = y0; y < y1; y++)
                memmove(dev->image_buffer + (size_t)(y - y0) * out_line,
                        dev->image_buffer + (size_t)y * buf_line + bpp * x0,
                        out_line);
            dev->params.pixels_per_line = x1 - x0;
            dev->params.lines = y1 - y0;
            dev->params.bytes_per_line = (SANE_Int)(dev->opt_bit_depth == 8
                                                    ? out_line / 2 : out_line);
            dev->scan_line_bytes = (int)out_line;
            dev->bytes_total = out_line * (size_t)(y1 - y0);
            DBG(DBG_info, "Scan area: %dx%d window cropped to x %d..%d, y %d..%d\n",
                fw, fh, x0, x1, y0, y1);
        }
    }
}

static SANE_Status
scan_run_image_pass(plustek8200i_device_t *dev)
{
    SANE_Status status;

    plustek8200i_regtrace_mark("IMAGE PASS START");
    DBG(DBG_info, "MOVE #4: Sending scan MOVE command — LISTEN FOR MOTOR!\n");
    status = plustek8200i_write_reg(dev->usb_fd, 0x0F, 0x01);
    if (status != SANE_STATUS_GOOD) {
        DBG(DBG_error, "Scan MOVE failed: %s\n", sane_strstatus(status));
        return status;
    }

    /*
     * Streaming read.  The GL128 has a ~4 MB circular SDRAM buffer, so
     * data must be read during the scan or the engine stalls (MOTORENB
     * stays 1).  As in sane-genesys, start the scan and read at once; the
     * bulk read blocks until data arrives.
     */
    {
        size_t raw_bytes = dev->buffer_size;

        if (stream_wanted(dev)) {
            if (dev->in_ir_pass)
                return ir_pass_stream(dev);
            if (dev->opt_rgbi && dev->opt_auto_wb)
                DBG(DBG_info, "Color+Infrared: --auto-wb is not applied\n");
            return stream_begin(dev);
        }

        DBG(DBG_info, "Starting streaming scan read (%zu bytes, %d lines)...\n",
            raw_bytes, dev->raw_scan_lines);

        status = plustek8200i_read_scan_data_cancellable(dev->usb_fd,
                                             dev->image_buffer, raw_bytes,
                                             &dev->cancel_requested);
        if (status == SANE_STATUS_CANCELLED) {
            DBG(DBG_warning, "Scan cancelled during the image pass\n");
            plustek8200i_scan_stop(dev->usb_fd);
            plustek8200i_motor_stop(dev->usb_fd);
            free(dev->image_buffer);
            dev->image_buffer = NULL;
            dev->buffer_size = 0;
            return SANE_STATUS_CANCELLED;
        }
        if (status != SANE_STATUS_GOOD) {
            DBG(DBG_error, "Scan data read failed: %s\n", sane_strstatus(status));
            /*
             * Continue with partial data instead of aborting.
             * This lets us examine whatever scan data was received.
             * The remaining buffer is zero-filled (calloc).
             */
            DBG(DBG_info, "Continuing with partial scan data for analysis\n");
        }

        DBG(DBG_info, "Scan data read complete. Post-processing...\n");
        /* Read motor/scan counters for analysis */
        {
            uint8_t fedcnt_h, fedcnt_m, fedcnt_l;
            uint8_t scancnt_h, scancnt_m, scancnt_l;
            uint8_t status41;
            plustek8200i_read_reg(dev->usb_fd, 0x48, &fedcnt_h);
            plustek8200i_read_reg(dev->usb_fd, 0x49, &fedcnt_m);
            plustek8200i_read_reg(dev->usb_fd, 0x4A, &fedcnt_l);
            plustek8200i_read_reg(dev->usb_fd, 0x4B, &scancnt_h);
            plustek8200i_read_reg(dev->usb_fd, 0x4C, &scancnt_m);
            plustek8200i_read_reg(dev->usb_fd, 0x4D, &scancnt_l);
            plustek8200i_read_reg(dev->usb_fd, 0x41, &status41);
            unsigned fedcnt = ((fedcnt_h & 0x0F) << 16) | (fedcnt_m << 8) | fedcnt_l;
            unsigned scancnt = ((scancnt_h & 0x0F) << 16) | (scancnt_m << 8) | scancnt_l;
            DBG(DBG_info, "Post-scan counters: FEDCNT=%u (0x%05X) SCANCNT=%u (0x%05X) STATUS41=0x%02X\n",
                fedcnt, fedcnt, scancnt, scancnt, status41);
            /* Estimate physical distance */
            double distance_mm = (double)fedcnt / 3600.0 * 25.4;
            DBG(DBG_info, "Post-scan: estimated motor distance = %.1fmm (FEDCNT/3600*25.4)\n",
                distance_mm);
        }

        /*
         * End of the image pass: clear SCAN only, as SilverFast does
         * (0x01 = 0x22, then nothing).  The pass started with AGOHOME
         * (0x02 = 0x30), so the ASIC drives the carriage home itself;
         * cutting motor power as well stranded the carriage and cost the
         * next MOVE #1 ~10 s.
         */
        plustek8200i_scan_stop(dev->usb_fd);
        DBG(DBG_info, "Scan stopped after data read; carriage returns home (AGOHOME)\n");

        /*
         * Vertical resample to square pixels.  The sensor samples ~2.6x
         * finer along the scan direction (PLUSTEK8200I_HW_LINE_DPI against
         * pixel_dpi); Windows gives the same shape (1303 rows x 864 px) and
         * SilverFast resamples.  Measured two ways, agreeing to 0.2%: film
         * rebate edges 2.605, cross-correlation with the Windows scan
         * 2.611.  Re-measure if the motor setup or LPERIOD changes.  A real
         * downsample, through the kernel in plustek8200i-vresample.h.
         */
        {
            /* Fixed by line rate against motor speed at ~PLUSTEK8200I_HW_LINE_DPI,
             * independent of DPISET: squash 300 dpi -> 4.97, 600 -> 2.48,
             * 1200 -> 1.24. */
            int rdpi = dev->opt_resolution;
            double vsquash;
            if (rdpi != 300 && rdpi != 600 && rdpi != 1200 &&
                rdpi != 2400 && rdpi != 3600 && rdpi != 7200) rdpi = 600;
            vsquash = (double)plustek8200i_target_vdpi(rdpi) / (double)rdpi;
            int src_lines = dev->raw_scan_lines;
            int dst_lines = (int)(src_lines / vsquash);
            /* Components per line come from the line width, not the scan mode: the
             * buffer is always three interleaved channels here.  A mode-derived
             * count processed a third of each line in Infrared mode. */
            int ncomp = dev->scan_line_bytes / 2;
            int sline = dev->scan_line_bytes;
            size_t dst_total;
            uint8_t *dst_buf;
            double *acc;
            int y, x, i;

            if (dst_lines < 1) dst_lines = 1;
            if (vsquash <= PLUSTEK8200I_VNONE) {
                /* No downsample needed (the motor was slowed so the lines exist).  The
                 * captured count is raw_lines, not the nominal output_lines. */
                dev->params.lines = dev->raw_scan_lines;
                dev->bytes_total = (size_t)sline * dev->raw_scan_lines;
                DBG(DBG_info, "No vertical resample (squash %.3f); "
                    "delivering %d lines\n", vsquash, dev->raw_scan_lines);
                goto no_resample;
            }
            dst_total = (size_t)sline * dst_lines;
            dst_buf = calloc(1, dst_total);
            acc = calloc(ncomp, sizeof(double));
            if (!dst_buf || !acc) {
                free(dst_buf); free(acc);
                free(dev->image_buffer); dev->image_buffer = NULL;
                return SANE_STATUS_NO_MEM;
            }

            for (y = 0; y < dst_lines; y++) {
                uint16_t *dst = (uint16_t *)(dst_buf +
                                (size_t)y * sline);
                plustek8200i_vweights_t wt;

                if (plustek8200i_vresample_weights(vsquash, y, &wt) != 0) {
                    int c = (int)((y + 0.5) * vsquash);
                    if (c < 0) c = 0;
                    if (c >= src_lines) c = src_lines - 1;
                    DBG(DBG_warning, "Vertical resample: no weights for line %d "
                        "(squash %.3f); using raw line %d\n", y, vsquash, c);
                    memcpy(dst, dev->image_buffer + (size_t)c * sline, sline);
                    continue;
                }
                memset(acc, 0, ncomp * sizeof(double));
                for (i = 0; i < wt.n; i++) {
                    double w = wt.w[i];
                    uint16_t *src;
                    if (w == 0.0) continue;
                    src = (uint16_t *)(dev->image_buffer + (size_t)sline *
                          plustek8200i_vresample_src(&wt, i, src_lines));
                    for (x = 0; x < ncomp; x++) acc[x] += w * src[x];
                }
                for (x = 0; x < ncomp; x++)
                    dst[x] = plustek8200i_vresample_clamp(acc[x]);
            }
            free(acc);
            free(dev->image_buffer);
            dev->image_buffer = dst_buf;
            dev->buffer_size = dst_total;
            dev->raw_scan_lines = dst_lines;
            dev->params.lines = dst_lines;
            dev->bytes_total = dst_total;
            DBG(DBG_info, "Vertical resample: %d -> %d lines "
                "(squash %.3f) -> square pixels\n",
                src_lines, dst_lines, vsquash);
        no_resample: ;
        }

        /*
         * Channel registration.  The CCD's R, G, B lines sit 1/300 inch
         * apart along the scan direction (red leads green and blue trails
         * by dpi/300 rows: 1, 2, 4, 8, 12 at 300 ... 3600 dpi), which showed
         * as colour fringes and, in the IR mean, as triple-smeared specks.
         * Shift red down and blue up by dpi/300 rows, repeating the nearest
         * row at the edges.  The buffer is always three channels here.
         */
        {
            int s = dev->opt_resolution / 300;
            int comps = dev->scan_line_bytes / 2;
            int lines = dev->raw_scan_lines;
            int px3 = comps - comps % 3;

            if (s > 0 && lines > 2 * s && comps >= 3) {
                int y, x;
                for (y = lines - 1; y >= 0; y--) {          /* red: down */
                    uint16_t *dst = (uint16_t *)(dev->image_buffer +
                                    (size_t)y * dev->scan_line_bytes);
                    uint16_t *src = (uint16_t *)(dev->image_buffer +
                                    (size_t)(y >= s ? y - s : 0) * dev->scan_line_bytes);
                    for (x = 0; x < px3; x += 3) dst[x] = src[x];
                }
                for (y = 0; y < lines; y++) {               /* blue: up */
                    uint16_t *dst = (uint16_t *)(dev->image_buffer +
                                    (size_t)y * dev->scan_line_bytes);
                    uint16_t *src = (uint16_t *)(dev->image_buffer +
                                    (size_t)(y + s < lines ? y + s : lines - 1) * dev->scan_line_bytes);
                    for (x = 2; x < px3; x += 3) dst[x] = src[x];
                }
                DBG(DBG_info, "Channel registration: R +%d rows, B -%d rows\n", s, s);
            }
        }

        /* Horizontal mirror: the sensor clocks pixels out right to left.
         * Against the SilverFast reference, mirrored scores ncc +0.7696 /
         * gradcorr +0.6454, unmirrored +0.5797 / +0.6039. */
        {
            /* Always three channels in the buffer here; see above. */
            int nchan = 3;
            int px = dev->scan_pixels;
            int sline = dev->scan_line_bytes;
            int y, x, c;

            for (y = 0; y < dev->raw_scan_lines; y++) {
                uint16_t *row = (uint16_t *)(dev->image_buffer +
                                (size_t)y * sline);
                for (x = 0; x < px / 2; x++) {
                    int o = px - 1 - x;
                    for (c = 0; c < nchan; c++) {
                        uint16_t t = row[x * nchan + c];
                        row[x * nchan + c] = row[o * nchan + c];
                        row[o * nchan + c] = t;
                    }
                }
            }
            DBG(DBG_info, "Horizontal mirror applied (%d px)\n", px);
        }

        /*
         * The device returns contiguous RGB-interleaved lines.  The old
         * desegmentation + demosaic undid a modulation of the previous CCD
         * configuration (suppressed line carrier, +-2-sample sidebands),
         * which was fixed at the source (REG06/SCANMOD, see
         * plustek8200i_apply_verified_ccd_regs); running it now would
         * corrupt good data.
         */
        {
            int total_lines = dev->raw_scan_lines;
            int line_bytes = dev->scan_line_bytes;
            int pixels = dev->scan_pixels;

            DBG(DBG_info, "Readout: %dx%d, %d bytes/line, no desegmentation\n",
                pixels, total_lines, line_bytes);

            /* Colour shift correction: R is the reference; for output line Y, G
             * comes from raw line Y + color_shift_g and B from Y + color_shift_b. */
            if (dev->color_shift_b > 0) {
                int out_lines = total_lines - dev->color_shift_b;
                size_t out_total;
                uint8_t *out_buf;
                int y;

                if (out_lines < 1) out_lines = 1;
                out_total = (size_t)line_bytes * out_lines;

                DBG(DBG_info, "Color shift: G=%d B=%d lines, %d → %d output lines\n",
                    dev->color_shift_g, dev->color_shift_b, total_lines, out_lines);

                out_buf = malloc(out_total);
                if (!out_buf) {
                    free(dev->image_buffer);
                    dev->image_buffer = NULL;
                    return SANE_STATUS_NO_MEM;
                }

                for (y = 0; y < out_lines; y++) {
                    uint16_t *dst = (uint16_t *)(out_buf + (size_t)y * line_bytes);
                    uint16_t *src_r = (uint16_t *)(dev->image_buffer + (size_t)y * line_bytes);
                    uint16_t *src_g = (uint16_t *)(dev->image_buffer + (size_t)(y + dev->color_shift_g) * line_bytes);
                    uint16_t *src_b = (uint16_t *)(dev->image_buffer + (size_t)(y + dev->color_shift_b) * line_bytes);
                    int x;
                    for (x = 0; x < pixels; x++) {
                        dst[x * 3 + 0] = src_r[x * 3 + 0];  /* R from line Y */
                        dst[x * 3 + 1] = src_g[x * 3 + 1];  /* G from line Y+shift_g */
                        dst[x * 3 + 2] = src_b[x * 3 + 2];  /* B from line Y+shift_b */
                    }
                }

                free(dev->image_buffer);
                dev->image_buffer = out_buf;
                dev->buffer_size = out_total;
                dev->raw_scan_lines = out_lines;
                total_lines = out_lines;

                /* Update SANE parameters */
                dev->params.lines = out_lines;
                dev->bytes_total = out_total;

                DBG(DBG_info, "Color shift complete: %dx%d\n", pixels, out_lines);
            }

            /* White balance, only on request.  Grey-world: each channel is scaled
             * up to the brightest channel's mean, so gains follow picture
             * content, vary frame to frame and clip.  Negative conversion needs
             * a fixed response to find the film base, hence the default off. */
            if (dev->opt_auto_wb) {
                double r_sum = 0, g_sum = 0, b_sum = 0;
                int total_px = pixels * total_lines;
                double r_mean, g_mean, b_mean, target;
                double r_mul, g_mul, b_mul;
                int i;

                for (i = 0; i < total_px; i++) {
                    uint16_t *px = (uint16_t *)(dev->image_buffer + i * 6);
                    r_sum += px[0];
                    g_sum += px[1];
                    b_sum += px[2];
                }
                r_mean = r_sum / total_px;
                g_mean = g_sum / total_px;
                b_mean = b_sum / total_px;

                DBG(DBG_info, "White balance: R_mean=%.0f G_mean=%.0f B_mean=%.0f\n",
                    r_mean, g_mean, b_mean);

                target = r_mean;
                if (g_mean > target) target = g_mean;
                if (b_mean > target) target = b_mean;

                r_mul = target / r_mean;
                g_mul = target / g_mean;
                b_mul = target / b_mean;

                DBG(DBG_info, "White balance: target=%.0f, mul R=%.4f G=%.4f B=%.4f\n",
                    target, r_mul, g_mul, b_mul);

                for (i = 0; i < total_px; i++) {
                    uint16_t *px = (uint16_t *)(dev->image_buffer + i * 6);
                    uint32_t r = (uint32_t)(px[0] * r_mul + 0.5);
                    uint32_t g = (uint32_t)(px[1] * g_mul + 0.5);
                    uint32_t b = (uint32_t)(px[2] * b_mul + 0.5);
                    px[0] = (r > 65535) ? 65535 : (uint16_t)r;
                    px[1] = (g > 65535) ? 65535 : (uint16_t)g;
                    px[2] = (b > 65535) ? 65535 : (uint16_t)b;
                }

                DBG(DBG_info, "White balance applied to %d pixels\n", total_px);
            }
        }
    }

    status = image_reduce_to_one_channel(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    image_apply_ir_repair(dev);

    image_crop_to_scan_area(dev);

    /* Mark scan as active — sane_read delivers from image_buffer */
    dev->scanning = SANE_TRUE;
    dev->bytes_read = 0;

    return SANE_STATUS_GOOD;
}

static SANE_Status
start_scan(SANE_Handle handle)
{
    /*
     * Infrared dust removal: run the IR pass first, stash the defect map,
     * then fall through to the colour scan and repair it at the end.
     * Re-enters sane_start once (guarded by in_ir_pass), as restructuring
     * the 1800-line scan body would be a far larger change.  The passes
     * share FEEDL, LINCNT and DPISET, so they are pixel-aligned (measured
     * dy 0, dx 0..1) without registration.
     */
    {
        plustek8200i_device_t *d = handle;
        if ((d->opt_ir_clean || d->opt_rgbi) && d->opt_mode == MODE_COLOR
            && !d->in_ir_pass && !rgbi_image_first(d)) {
            SANE_Status irst;
            int saved_mode = d->opt_mode;

            free(d->ir_map);
            d->ir_map = NULL;
            d->ir_map_size = 0;

            d->in_ir_pass = 1;
            d->opt_mode = MODE_INFRARED;
            DBG(DBG_info, "IR clean: infrared pass 1 of 2\n");
            irst = start_scan(handle);
            d->opt_mode = saved_mode;
            d->in_ir_pass = 0;

            if (irst == SANE_STATUS_GOOD && d->image_buffer) {
                d->ir_map = d->image_buffer;
                d->ir_map_size = d->buffer_size;
                d->image_buffer = NULL;
                d->buffer_size = 0;
                d->scanning = SANE_FALSE;
                DBG(DBG_info, "IR clean: defect map captured, %zu bytes\n",
                    d->ir_map_size);
            } else if (irst == SANE_STATUS_CANCELLED) {
                return SANE_STATUS_CANCELLED;
            } else if (d->opt_rgbi) {
                DBG(DBG_error, "Color+Infrared: infrared pass failed (%s)\n",
                    sane_strstatus(irst));
                return irst == SANE_STATUS_GOOD ? SANE_STATUS_IO_ERROR : irst;
            } else {
                DBG(DBG_error, "IR clean: infrared pass failed (%s); "
                    "continuing without repair\n", sane_strstatus(irst));
            }
        }
    }

    plustek8200i_device_t *dev = handle;
    SANE_Status status;

    DBG(DBG_info, "sane_start: Starting scan\n");

    /* Make sure parameters are set */
    if (dev->params.bytes_per_line == 0) {
        SANE_Parameters params;
        status = sane_get_parameters(handle, &params);
        if (status != SANE_STATUS_GOOD)
            return status;
    }

    /* Calculate total image size */
    dev->bytes_total = dev->params.bytes_per_line * dev->params.lines;
    dev->bytes_read = 0;

    DBG(DBG_info, "Image size: %d lines × %d bytes/line = %zu bytes total\n",
        dev->params.lines, dev->params.bytes_per_line, dev->bytes_total);


    status = scan_init_sequence(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_move_home(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_move_intermediate(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_reinit_after_move2(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_move_to_frame(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_apply_scan_registers(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_calibration_pass(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_load_scan_motor_tables(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_program_image_pass(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    status = scan_shading_and_reference(dev);
    if (status != SANE_STATUS_GOOD)
        return status;

    return scan_run_image_pass(dev);
}

/*
 * SANE API: sane_start
 *
 * A frontend may call sane_cancel from another thread (or a signal
 * handler) while this runs; cancel_requested is checked between bulk
 * reads, after each read has completed -- aborting with a transfer
 * outstanding is what wedges the USB link on this ASIC family.
 */
SANE_Status
sane_start(SANE_Handle handle)
{
    plustek8200i_device_t *dev = handle;
    SANE_Status status;

    if (dev->streaming && dev->opt_rgbi && dev->s_frame == 0 && dev->s_ir
        && dev->s_out_y >= dev->s_y1) {
        /* Color+Infrared: the colour frame is done; hand out the IR
         * plane as the second, last frame. */
        int bps = dev->opt_bit_depth == 8 ? 1 : 2;
        dev->s_frame = 1;
        dev->s_comps = 1;
        dev->s_out_y = dev->s_y0;
        dev->s_line_len = dev->s_line_pos = 0;
        dev->params.format = SANE_FRAME_GRAY;
        dev->params.last_frame = SANE_TRUE;
        dev->params.bytes_per_line = (dev->s_x1 - dev->s_x0) * bps;
        dev->bytes_read = 0;
        dev->scanning = SANE_TRUE;
        DBG(DBG_info, "Color+Infrared: infrared frame %dx%d\n",
            dev->s_x1 - dev->s_x0, dev->s_y1 - dev->s_y0);
        return SANE_STATUS_GOOD;
    }
    if (dev->streaming)
        stream_cleanup(dev);
    dev->cancel_requested = 0;
    /* Image-first Color+Infrared: after the colour frame comes the infrared pass. */
    if (rgbi_image_first(dev) && dev->rgbi_ir_next && dev->opt_mode == MODE_COLOR)
        dev->opt_mode = MODE_INFRARED;
    dev->in_start = 1;
    status = start_scan(handle);
    dev->in_start = 0;
    if (status == SANE_STATUS_GOOD && dev->cancel_requested) {
        /* Cancelled during post-processing, or while a streamed image
         * pass was being started: drop the result. */
        if (dev->streaming) {
            plustek8200i_scan_stop(dev->usb_fd);
            plustek8200i_motor_stop(dev->usb_fd);
            stream_cleanup(dev);
        } else {
            free(dev->image_buffer);
            dev->image_buffer = NULL;
            dev->buffer_size = 0;
        }
        dev->scanning = SANE_FALSE;
        status = SANE_STATUS_CANCELLED;
    }
    if (status != SANE_STATUS_GOOD)
        rgbi_abandon(dev);
    if (status == SANE_STATUS_CANCELLED)
        DBG(DBG_warning, "sane_start: cancelled\n");
    return status;
}

/* SANE API: sane_read: delivers the scan data processed in sane_start */
SANE_Status
sane_read(SANE_Handle handle, SANE_Byte *data, SANE_Int max_length, SANE_Int *length)
{
    plustek8200i_device_t *dev = handle;
    size_t bytes_remaining;
    size_t chunk_size;

    /* The standard requires *length to be set on every return path,
     * including the error ones -- net.c/saned read it unconditionally. */
    if (!data || !length)
        return SANE_STATUS_INVAL;
    *length = 0;

    if (!dev->scanning) {
        DBG(DBG_info, "sane_read: not scanning\n");
        return SANE_STATUS_CANCELLED;
    }

    if (dev->streaming)
        return stream_read(dev, data, max_length, length);

    /* Check if we've delivered all the data */
    if (dev->bytes_read >= dev->bytes_total) {
        DBG(DBG_info, "sane_read: EOF (delivered %zu of %zu bytes)\n",
            dev->bytes_read, dev->bytes_total);
        *length = 0;
        return SANE_STATUS_EOF;
    }

    /* Calculate how much to deliver from image_buffer.
     * For 8-bit output, we still track bytes_read in 16-bit units
     * and convert when delivering. */
    bytes_remaining = dev->bytes_total - dev->bytes_read;
    chunk_size = ((size_t)max_length < bytes_remaining) ? (size_t)max_length : bytes_remaining;

    /* For 8-bit mode, chunk_size is in 16-bit buffer units.
     * We read 2 bytes from buffer for each 1 byte delivered.
     * Limit chunk_size so the converted output fits in max_length. */
    if (dev->opt_bit_depth == 8) {
        size_t max_16bit = (size_t)max_length * 2;
        if (chunk_size > max_16bit)
            chunk_size = max_16bit;
        if (chunk_size & 1)
            chunk_size &= ~(size_t)1;  /* Ensure even for 16→8 conversion */
    }

    /* Copy from pre-read, color-corrected image_buffer */
    memcpy(data, dev->image_buffer + dev->bytes_read, chunk_size);
    dev->bytes_read += chunk_size;
    *length = chunk_size;

    /*
     * 16-bit output needs no work: the buffer already holds native-order
     * uint16_t samples, which is exactly what SANE transmits. Do NOT
     * byte-swap to big-endian here -- see the note in stream_read().
     */
    if (dev->opt_bit_depth == 8) {
        /*
         * 8-bit output: keep the high byte of each 16-bit sample. Read it
         * through a uint16_t so this is correct on either endianness (and
         * memcpy because `data` carries no alignment guarantee).
         */
        size_t i;
        size_t samples = chunk_size / 2;
        for (i = 0; i < samples; i++) {
            uint16_t v;
            memcpy(&v, data + i * 2, sizeof v);
            data[i] = (SANE_Byte)(v >> 8);
        }
        *length = samples;
    }

    DBG(DBG_data, "sane_read: delivered %d bytes, buffer %zu/%zu\n",
        *length, dev->bytes_read, dev->bytes_total);

    return SANE_STATUS_GOOD;
}

/*
 * SANE API: sane_cancel
 */
void
sane_cancel(SANE_Handle handle)
{
    plustek8200i_device_t *dev = handle;

    DBG(DBG_warning, "sane_cancel\n");

    if (dev->in_start || dev->in_read) {
        /* sane_start or sane_read is running in another thread (or we
         * are in scanimage's SIGINT handler): ask it to stop, and let it
         * do the USB work itself once the current transfer completes. */
        dev->cancel_requested = 1;
        DBG(DBG_warning, "sane_cancel: cancel requested during %s\n",
            dev->in_start ? "sane_start" : "sane_read");
        return;
    }

    /* A frontend that stops after the image frame, or has read both: the
     * next sane_start begins a new image. */
    rgbi_abandon(dev);

    if (!dev->scanning) {
        DBG(DBG_info, "sane_cancel: not scanning, nothing to cancel\n");
        return;
    }

    if (dev->streaming) {
        /* Cancelled between reads (no transfer outstanding), or the
         * frontend's normal cancel after EOF. Only stop the hardware if
         * the image pass had not finished. */
        if (!dev->s_hw_done) {
            DBG(DBG_warning, "sane_cancel: stopping the scan mid-pass\n");
            plustek8200i_scan_stop(dev->usb_fd);
            plustek8200i_motor_stop(dev->usb_fd);
        }
        stream_cleanup(dev);
        dev->scanning = SANE_FALSE;
        dev->bytes_read = 0;
        dev->bytes_total = 0;
        return;
    }

    /*
     * The image pass finished inside sane_start (all data is in RAM), and
     * the carriage is on its way home under AGOHOME. Leave the motor
     * alone: cutting its power here strands the carriage mid-travel.
     */

    /* Free image buffer */
    if (dev->image_buffer) {
        free(dev->image_buffer);
        dev->image_buffer = NULL;
        dev->buffer_size = 0;
    }

    /* Mark scan as inactive */
    dev->scanning = SANE_FALSE;
    dev->bytes_read = 0;
    dev->bytes_total = 0;

    DBG(DBG_info, "Scan cancelled\n");
}

/*
 * SANE API: sane_set_io_mode
 */
SANE_Status
sane_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking)
{
    plustek8200i_device_t *dev = handle;

    /* Only non-blocking I/O may be refused. Blocking mode is the default
     * every backend must support, so answering UNSUPPORTED to it makes
     * conforming frontends give up on an otherwise fine device. */
    if (!dev->scanning)
        return SANE_STATUS_INVAL;

    return non_blocking ? SANE_STATUS_UNSUPPORTED : SANE_STATUS_GOOD;
}

/*
 * SANE API: sane_get_select_fd
 */
SANE_Status
sane_get_select_fd(SANE_Handle handle, SANE_Int *fd)
{
    (void)handle;
    (void)fd;

    return SANE_STATUS_UNSUPPORTED;
}
