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

/*
 * Front-panel buttons (IntelliScan and QuickScan).
 *
 * The scanner reports a press on its interrupt endpoint 0x83 (1 byte,
 * bInterval 8), not through a register.  Measured on the hardware:
 *
 *   - one packet per press: 0x08 for IntelliScan, 0x40 for QuickScan;
 *   - nothing at all while the panel is untouched, so the endpoint is
 *     silent except for events;
 *   - no packet on release, and holding a button down does not repeat;
 *   - no initialisation needed.  A cold scanner that has never been sent
 *     a single register write still reports presses;
 *   - the device BUFFERS a press until somebody reads the endpoint.  A
 *     press made with nothing listening arrived 14 ms into the next read,
 *     a minute later.
 *
 * That last property is what shapes this code.  Polling a few times a
 * second cannot miss a press, so no thread has to sit on the endpoint --
 * which is just as well, since a SANE backend has no good place to put
 * one.  It also means a press is still waiting from whenever the scanner
 * was last touched, possibly hours ago and with no relation to what the
 * frontend is doing now, so sane_open() throws away what it finds
 * (plustek8200i_button_discard) rather than reporting a phantom press.
 *
 * The SANE side of this is a read-and-clear: a press stays latched until a
 * frontend reads that button's option, and reading it reports SANE_TRUE
 * once.  Upstream's genesys backend behaves the same way, and it is what a
 * polling frontend needs -- a press must not be missed because the poll
 * landed after the button came back up.
 */

#define DEBUG_DECLARE_ONLY
#include "../include/sane/config.h"
#include <sys/time.h>

#include "../include/sane/sanei_usb.h"

#include "plustek8200i.h"
#include "plustek8200i-button.h"

/* Enough to catch a packet at bInterval 8 (16 ms at high speed) without
 * stalling a frontend's poll noticeably when there is nothing to report. */
#define BUTTON_READ_TIMEOUT_MS   50

/* The timeout the rest of the backend expects to find in place. */
#define BUTTON_RESTORE_TIMEOUT_MS 30000

/* A frontend reads one option per call, so a GUI polling both buttons
 * makes two calls in a row.  Within this window the second call is served
 * from the latch instead of paying for its own endpoint read. */
#define BUTTON_POLL_COALESCE_S   0.050

/* A press is one packet, so a handful of reads drains anything queued.
 * Bounded so a scanner that somehow streams on this endpoint cannot wedge
 * a frontend's option read. */
#define BUTTON_MAX_DRAIN         8

static double
button_now_s(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/*
 * Read everything the endpoint has queued into the latch.
 *
 * A timeout means "no button pressed", which is the normal case and not
 * worth a word in the log.  sanei_usb_read_int cannot distinguish it from
 * a real I/O error -- both come back as SANE_STATUS_IO_ERROR -- so neither
 * can we; a genuinely broken endpoint shows up as buttons that never
 * report, with the detail at DBG_debug.
 */
static void
button_drain(plustek8200i_device_t *dev, unsigned *into)
{
    int i;

    sanei_usb_set_timeout(BUTTON_READ_TIMEOUT_MS);

    for (i = 0; i < BUTTON_MAX_DRAIN; i++) {
        SANE_Byte packet = 0;
        size_t len = 1;
        SANE_Status status = sanei_usb_read_int(dev->usb_fd, &packet, &len);

        if (status != SANE_STATUS_GOOD || len != 1) {
            DBG(DBG_debug, "button_drain: endpoint quiet after %d read%s (%s)\n",
                i, i == 1 ? "" : "s", sane_strstatus(status));
            break;
        }

        DBG(DBG_info, "button_drain: interrupt packet 0x%02x\n", packet);
        if (into)
            *into = plustek8200i_button_fold(*into, packet);
    }

    sanei_usb_set_timeout(BUTTON_RESTORE_TIMEOUT_MS);
}

/*
 * Bring the latch up to date before a frontend reads a button option.
 *
 * Not during a scan: the streaming path runs the bulk endpoint on its own
 * timeouts (up to 180 s for the lead-in chunk), and this function moves
 * the global sanei_usb timeout, so polling mid-scan would cut a legitimate
 * wait short.  The latch is served as it stands instead, and the next poll
 * after the scan picks up anything pressed meanwhile -- the device keeps
 * it queued.
 */
void
plustek8200i_button_poll(plustek8200i_device_t *dev)
{
    double now;

    if (!dev || dev->usb_fd < 0)
        return;

    if (dev->scanning) {
        DBG(DBG_debug, "button_poll: scanning, serving the latch unchanged\n");
        return;
    }

    now = button_now_s();
    if (now - dev->btn_last_poll < BUTTON_POLL_COALESCE_S)
        return;
    dev->btn_last_poll = now;

    button_drain(dev, &dev->btn_latch);
}

/*
 * Throw away presses the device has been holding, and start the latch
 * empty.  Called from sane_open: see the note at the top of this file on
 * why an unread press from an hour ago must not become this session's
 * first event.
 */
void
plustek8200i_button_discard(plustek8200i_device_t *dev)
{
    if (!dev || dev->usb_fd < 0)
        return;

    button_drain(dev, NULL);
    dev->btn_latch = 0;
    dev->btn_last_poll = button_now_s();
}
