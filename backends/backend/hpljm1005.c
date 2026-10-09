/* sane - Scanner Access Now Easy.

   Copyright (C) 2007-2008 Philippe Rétornaz

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.

   This backend is for HP LaserJet M1005 MFP

   Highly inspired from the epson backend
*/

#define BUILD 1

#include  "../include/sane/config.h"
#include  <stdio.h>
#include  <string.h>
#include  <stdlib.h>
#include  <fcntl.h>
#include  <unistd.h>
#include  <stdint.h>
#include <netinet/in.h>
#define  BACKEND_NAME hpljm1005
#include  "../include/sane/sanei_backend.h"
#include  "../include/sane/sanei_usb.h"
#include  "../include/sane/saneopts.h"

#define MAGIC_NUMBER 0x41535001
#define PKT_READ_STATUS 0x0
#define PKT_UNKNOW_1 0x1
#define PKT_START_SCAN 0x2
#define PKT_GO_IDLE 0x3
#define PKT_DATA 0x5
#define PKT_READCONF 0x6
#define PKT_SETCONF 0x7
#define PKT_END_DATA 0xe
#define PKT_RESET 0x15

#define RED_LAYER 0x3
#define GREEN_LAYER 0x4
#define BLUE_LAYER 0x5
#define GRAY_LAYER 0x6

#define MIN_SCAN_ZONE 101

struct usbdev_s
{
  SANE_Int vendor_id;
  SANE_Int product_id;
  SANE_String_Const vendor_s;
  SANE_String_Const model_s;
  SANE_String_Const type_s;
};

/* Zero-terminated USB VID/PID array */
static struct usbdev_s usbid[] = {
  {0x03f0, 0x3b17, "Hewlett-Packard", "LaserJet M1005",
   "multi-function peripheral"},
  {0x03f0, 0x5617, "Hewlett-Packard", "LaserJet M1120",
   "multi-function peripheral"},
  {0x03f0, 0x5717, "Hewlett-Packard", "LaserJet M1120n",
   "multi-function peripheral"},
  {0, 0, NULL, NULL, NULL},
  {0, 0, NULL, NULL, NULL}
};

static int cur_idx;

#define BR_CONT_MIN 0x1
#define BR_CONT_MAX 0xb

#define RGB 1
#define GRAY 0

#define MAX_X_H 0x351
#define MAX_Y_H 0x490
#define MAX_X_S 216
#define MAX_Y_S 297

#define OPTION_MAX 9

static SANE_Word resolution_list[] = {
  7, 75, 100, 150, 200, 300, 600, 1200
};
static SANE_Range range_x = { 0, MAX_X_S, 0 };
static SANE_Range range_y = { 0, MAX_Y_S, 0 };

static SANE_Range range_br_cont = { BR_CONT_MIN, BR_CONT_MAX, 0 };

static const SANE_String_Const mode_list[] = {
  SANE_VALUE_SCAN_MODE_GRAY,
  SANE_VALUE_SCAN_MODE_COLOR,
  NULL
};

#define X1_OFFSET 2
#define X2_OFFSET 4
#define Y1_OFFSET 3
#define Y2_OFFSET 5
#define RES_OFFSET 1
#define COLOR_OFFSET 8
#define BRIGH_OFFSET 6
#define CONTR_OFFSET 7

#define STATUS_IDLE 0
#define STATUS_SCANNING 1
#define STATUS_CANCELING 2

struct buffer_s {
  char *buffer;
  size_t w_offset;
  size_t size;
};

struct device_s
{
  struct device_s *next;
  SANE_String_Const devname;
  int idx;			/* Index in the usbid array */
  int dn;			/* Usb "Handle" */
  SANE_Option_Descriptor optiond[OPTION_MAX];
  struct buffer_s buf_r; /* also for gray mode */
  struct buffer_s buf_g;
  struct buffer_s buf_b;
  int read_offset;
  int status;
  int width;
  int height;
  int height_h;
  int data_width; /* width + some padding 0xFF which should be ignored */
  int scanned_pixels;
  SANE_Word optionw[OPTION_MAX];
  uint32_t conf_data[512];
  uint32_t packet_data[512];
};


static void
do_cancel(struct device_s *dev);


static struct device_s *devlist_head;
static int devlist_count;	/* Number of element in the list */

/*
 * List of pointers to devices - will be dynamically allocated depending
 * on the number of devices found.
 */
static SANE_Device **devlist = NULL;

/* round() is c99, so we provide our own, though this version won't return -0 */
static double
round2(double x)
{
    return (double)(x >= 0.0) ? (int)(x+0.5) : (int)(x-0.5);
}

/* This function is copy/pasted from the Epson backend */
static size_t
max_string_size (const SANE_String_Const strings[])
{
  size_t size, max_size = 0;
  int i;

  for (i = 0; strings[i]; i++)
    {
      size = strlen (strings[i]) + 1;
      if (size > max_size)
	max_size = size;
    }
  return max_size;
}


static SANE_Status
attach (SANE_String_Const devname)
{
  struct device_s *dev;

  dev = malloc (sizeof (struct device_s));
  if (!dev)
    return SANE_STATUS_NO_MEM;
  memset (dev, 0, sizeof (struct device_s));

  dev->devname = devname;
  DBG(1,"New device found: %s\n",dev->devname);

/* Init the whole structure with default values */
  /* Number of options */
  dev->optiond[0].name = "";
  dev->optiond[0].title = NULL;
  dev->optiond[0].desc = NULL;
  dev->optiond[0].type = SANE_TYPE_INT;
  dev->optiond[0].unit = SANE_UNIT_NONE;
  dev->optiond[0].size = sizeof (SANE_Word);
  dev->optionw[0] = OPTION_MAX;

  /* resolution */
  dev->optiond[RES_OFFSET].name = "resolution";
  dev->optiond[RES_OFFSET].title = "resolution";
  dev->optiond[RES_OFFSET].desc = "resolution";
  dev->optiond[RES_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[RES_OFFSET].unit = SANE_UNIT_DPI;
  dev->optiond[RES_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[RES_OFFSET].size = sizeof (SANE_Word);
  dev->optiond[RES_OFFSET].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[RES_OFFSET].constraint_type = SANE_CONSTRAINT_WORD_LIST;
  dev->optiond[RES_OFFSET].constraint.word_list = resolution_list;
  dev->optionw[RES_OFFSET] = 75;

  /* scan area */
  dev->optiond[X1_OFFSET].name = "tl-x";
  dev->optiond[X1_OFFSET].title = "tl-x";
  dev->optiond[X1_OFFSET].desc = "tl-x";
  dev->optiond[X1_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[X1_OFFSET].unit = SANE_UNIT_MM;
  dev->optiond[X1_OFFSET].size = sizeof (SANE_Word);
  dev->optiond[X1_OFFSET].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[X1_OFFSET].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->optiond[X1_OFFSET].constraint.range = &range_x;
  dev->optionw[X1_OFFSET] = 0;

  dev->optiond[Y1_OFFSET].name = "tl-y";
  dev->optiond[Y1_OFFSET].title = "tl-y";
  dev->optiond[Y1_OFFSET].desc = "tl-y";
  dev->optiond[Y1_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[Y1_OFFSET].unit = SANE_UNIT_MM;
  dev->optiond[Y1_OFFSET].size = sizeof (SANE_Word);
  dev->optiond[Y1_OFFSET].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[Y1_OFFSET].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->optiond[Y1_OFFSET].constraint.range = &range_y;
  dev->optionw[Y1_OFFSET] = 0;

  dev->optiond[X2_OFFSET].name = "br-x";
  dev->optiond[X2_OFFSET].title = "br-x";
  dev->optiond[X2_OFFSET].desc = "br-x";
  dev->optiond[X2_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[X2_OFFSET].unit = SANE_UNIT_MM;
  dev->optiond[X2_OFFSET].size = sizeof (SANE_Word);
  dev->optiond[X2_OFFSET].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[X2_OFFSET].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->optiond[X2_OFFSET].constraint.range = &range_x;
  dev->optionw[X2_OFFSET] = MAX_X_S;

  dev->optiond[Y2_OFFSET].name = "br-y";
  dev->optiond[Y2_OFFSET].title = "br-y";
  dev->optiond[Y2_OFFSET].desc = "br-y";
  dev->optiond[Y2_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[Y2_OFFSET].unit = SANE_UNIT_MM;
  dev->optiond[Y2_OFFSET].size = sizeof (SANE_Word);
  dev->optiond[Y2_OFFSET].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[Y2_OFFSET].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->optiond[Y2_OFFSET].constraint.range = &range_y;
  dev->optionw[Y2_OFFSET] = MAX_Y_S;

  /* brightness */
  dev->optiond[BRIGH_OFFSET].name = "brightness";
  dev->optiond[BRIGH_OFFSET].title = "Brightness";
  dev->optiond[BRIGH_OFFSET].desc = "Set the brightness";
  dev->optiond[BRIGH_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[BRIGH_OFFSET].unit = SANE_UNIT_NONE;
  dev->optiond[BRIGH_OFFSET].size = sizeof (SANE_Word);
  dev->optiond[BRIGH_OFFSET].cap =
    SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[BRIGH_OFFSET].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->optiond[BRIGH_OFFSET].constraint.range = &range_br_cont;
  dev->optionw[BRIGH_OFFSET] = 0x6;

  /* contrast */
  dev->optiond[CONTR_OFFSET].name = "contrast";
  dev->optiond[CONTR_OFFSET].title = "Contrast";
  dev->optiond[CONTR_OFFSET].desc = "Set the contrast";
  dev->optiond[CONTR_OFFSET].type = SANE_TYPE_INT;
  dev->optiond[CONTR_OFFSET].unit = SANE_UNIT_NONE;
  dev->optiond[CONTR_OFFSET].size = sizeof (SANE_Word);
  dev->optiond[CONTR_OFFSET].cap =
    SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[CONTR_OFFSET].constraint_type = SANE_CONSTRAINT_RANGE;
  dev->optiond[CONTR_OFFSET].constraint.range = &range_br_cont;
  dev->optionw[CONTR_OFFSET] = 0x6;

  /* Color */
  dev->optiond[COLOR_OFFSET].name = SANE_NAME_SCAN_MODE;
  dev->optiond[COLOR_OFFSET].title = SANE_TITLE_SCAN_MODE;
  dev->optiond[COLOR_OFFSET].desc = SANE_DESC_SCAN_MODE;
  dev->optiond[COLOR_OFFSET].type = SANE_TYPE_STRING;
  dev->optiond[COLOR_OFFSET].size = max_string_size (mode_list);
  dev->optiond[COLOR_OFFSET].cap =
    SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
  dev->optiond[COLOR_OFFSET].constraint_type = SANE_CONSTRAINT_STRING_LIST;
  dev->optiond[COLOR_OFFSET].constraint.string_list = mode_list;
  dev->optionw[COLOR_OFFSET] = RGB;
  dev->dn = 0;
  dev->idx = cur_idx;
  dev->status = STATUS_IDLE;

  dev->next = devlist_head;
  devlist_head = dev;
  devlist_count++;



  return SANE_STATUS_GOOD;
}

SANE_Status
sane_init (SANE_Int * version_code,
	   SANE_Auth_Callback __sane_unused__ authorize)
{

  if (version_code != NULL)
    *version_code = SANE_VERSION_CODE (SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD);

  DBG_INIT();

  sanei_usb_init ();

  return SANE_STATUS_GOOD;
}

void
sane_exit (void)
{
  /* free everything */
  struct device_s *iter;

  if (devlist)
    {
      int i;
      for (i = 0; devlist[i]; i++)
	free (devlist[i]);
      free (devlist);
      devlist = NULL;
    }
  if (devlist_head)
    {
      iter = devlist_head->next;
      free (devlist_head);
      devlist_head = NULL;
      while (iter)
	{
	  struct device_s *tmp = iter;
	  iter = iter->next;
	  free (tmp);
	}
    }
  devlist_count = 0;
}

SANE_Status
sane_get_devices (const SANE_Device * **device_list,
		  SANE_Bool __sane_unused__ local_only)
{
  struct device_s *iter;
  int i;

  devlist_count = 0;

  if (devlist_head)
    {
      iter = devlist_head->next;
      free (devlist_head);
      devlist_head = NULL;
      while (iter)
	{
	  struct device_s *tmp = iter;
	  iter = iter->next;
	  free (tmp);
	}
    }

  /* Rebuild our internal scanner list */
  for (cur_idx = 0; usbid[cur_idx].vendor_id; cur_idx++)
    sanei_usb_find_devices (usbid[cur_idx].vendor_id,
			    usbid[cur_idx].product_id, attach);

  if (devlist)
    {
      for (i = 0; devlist[i]; i++)
	free (devlist[i]);
      free (devlist);
    }

  /* rebuild the sane-API scanner list array */
  devlist = malloc (sizeof (devlist[0]) * (devlist_count + 1));
  if (!devlist)
    return SANE_STATUS_NO_MEM;

  memset (devlist, 0, sizeof (devlist[0]) * (devlist_count + 1));

  for (i = 0, iter = devlist_head; i < devlist_count; i++, iter = iter->next)
    {
      devlist[i] = malloc (sizeof (SANE_Device));
      if (!devlist[i])
	{
	  int j;
	  for (j = 0; j < i; j++)
	    free (devlist[j]);
	  free (devlist);
	  devlist = NULL;
	  return SANE_STATUS_NO_MEM;
	}
      devlist[i]->name = iter->devname;
      devlist[i]->vendor = usbid[iter->idx].vendor_s;
      devlist[i]->model = usbid[iter->idx].model_s;
      devlist[i]->type = usbid[iter->idx].type_s;
    }
  if (device_list)
    *device_list = (const SANE_Device **) devlist;
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open (SANE_String_Const name, SANE_Handle * h)
{
  struct device_s *dev;
  int ret;

  if(!devlist_head)
    sane_get_devices(NULL,(SANE_Bool)0);

  dev = devlist_head;

  if (strlen (name))
    for (; dev; dev = dev->next)
      if (!strcmp (name, dev->devname))
	break;

  if (!dev) {
    DBG(1,"Unable to find device %s\n",name);
    return SANE_STATUS_INVAL;
  }

  DBG(1,"Found device %s\n",name);

  /* Now open the usb device */
  ret = sanei_usb_open (name, &(dev->dn));
  if (ret != SANE_STATUS_GOOD) {
    DBG(1,"Unable to open device %s\n",name);
    return ret;
  }

  /* Claim the first interface */
  ret = sanei_usb_claim_interface (dev->dn, 0);
  if (ret != SANE_STATUS_GOOD)
    {
      sanei_usb_close (dev->dn);
      /* if we cannot claim the interface, this is because
         someone else is using it */
      DBG(1,"Unable to claim scanner interface on device %s\n",name);
      return SANE_STATUS_DEVICE_BUSY;
    }
#ifdef HAVE_SANEI_USB_SET_TIMEOUT
  sanei_usb_set_timeout (30000);	/* 30s timeout */
#endif

  *h = dev;

  return SANE_STATUS_GOOD;
}

void
sane_close (SANE_Handle h)
{
  struct device_s *dev = (struct device_s *) h;

  /* Just in case if sane_cancel() is called
   * after starting a scan but not while a sane_read
   */
  if (dev->status == STATUS_CANCELING)
    {
       do_cancel(dev);
    }

  sanei_usb_release_interface (dev->dn, 0);
  sanei_usb_close (dev->dn);

}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle h, SANE_Int option)
{
  struct device_s *dev = (struct device_s *) h;

  if (option >= OPTION_MAX || option < 0)
    return NULL;
  return &(dev->optiond[option]);
}

static SANE_Status
getvalue (SANE_Handle h, SANE_Int option, void *v)
{
  struct device_s *dev = (struct device_s *) h;

  if (option != COLOR_OFFSET)
    *((SANE_Word *) v) = dev->optionw[option];
  else
    {
      strcpy ((char *) v,
	      dev->optiond[option].constraint.string_list[dev->
							  optionw[option]]);
    }
  return SANE_STATUS_GOOD;
}

static SANE_Status
setvalue (SANE_Handle h, SANE_Int option, void *value, SANE_Int * info)
{
  struct device_s *dev = (struct device_s *) h;
  SANE_Status status = SANE_STATUS_GOOD;
  int s_unit;
  int s_unit_2;

  if (option == 0)
    return SANE_STATUS_UNSUPPORTED;


  status = sanei_constrain_value (&(dev->optiond[option]), value, info);

  if (status != SANE_STATUS_GOOD)
    return status;



  if (info)
    *info |= SANE_INFO_RELOAD_PARAMS;
  switch (option)
    {
    case X1_OFFSET:
      dev->optionw[option] = *((SANE_Word *) value);
      s_unit = (int) round2 ((dev->optionw[option] / ((double) MAX_X_S))
			    * MAX_X_H);
      s_unit_2 = (int) round2 ((dev->optionw[X2_OFFSET] / ((double) MAX_X_S))
			      * MAX_X_H);
      if (abs (s_unit_2 - s_unit) < MIN_SCAN_ZONE)
	s_unit = s_unit_2 - MIN_SCAN_ZONE;
      dev->optionw[option] = round2 ((s_unit / ((double) MAX_X_H)) * MAX_X_S);
      if (info)
	*info |= SANE_INFO_INEXACT;
      break;

    case X2_OFFSET:
      /* X units */
      /* convert into "scanner" unit, then back into mm */
      dev->optionw[option] = *((SANE_Word *) value);

      s_unit = (int) round2 ((dev->optionw[option] / ((double) MAX_X_S))
			    * MAX_X_H);
      s_unit_2 = (int) round2 ((dev->optionw[X1_OFFSET] / ((double) MAX_X_S))
			      * MAX_X_H);
      if (abs (s_unit_2 - s_unit) < MIN_SCAN_ZONE)
	s_unit = s_unit_2 + MIN_SCAN_ZONE;
      dev->optionw[option] = round2 ((s_unit / ((double) MAX_X_H)) * MAX_X_S);
      if (info)
	*info |= SANE_INFO_INEXACT;
      break;
    case Y1_OFFSET:
      /* Y units */
      dev->optionw[option] = *((SANE_Word *) value);

      s_unit = (int) round2 ((dev->optionw[option] / ((double) MAX_Y_S))
			    * MAX_Y_H);

      s_unit_2 = (int) round2 ((dev->optionw[Y2_OFFSET] / ((double) MAX_Y_S))
			      * MAX_Y_H);
      if (abs (s_unit_2 - s_unit) < MIN_SCAN_ZONE)
	s_unit = s_unit_2 - MIN_SCAN_ZONE;

      dev->optionw[option] = round2 ((s_unit / ((double) MAX_Y_H)) * MAX_Y_S);
      if (info)
	*info |= SANE_INFO_INEXACT;
      break;
    case Y2_OFFSET:
      /* Y units */
      dev->optionw[option] = *((SANE_Word *) value);

      s_unit = (int) round2 ((dev->optionw[option] / ((double) MAX_Y_S))
			    * MAX_Y_H);

      s_unit_2 = (int) round2 ((dev->optionw[Y1_OFFSET] / ((double) MAX_Y_S))
			      * MAX_Y_H);
      if (abs (s_unit_2 - s_unit) < MIN_SCAN_ZONE)
	s_unit = s_unit_2 + MIN_SCAN_ZONE;

      dev->optionw[option] = round2 ((s_unit / ((double) MAX_Y_H)) * MAX_Y_S);
      if (info)
	*info |= SANE_INFO_INEXACT;
      break;
    case COLOR_OFFSET:
      if (!strcmp ((char *) value, mode_list[0]))
	dev->optionw[option] = GRAY;	/* Gray */
      else if (!strcmp ((char *) value, mode_list[1]))
	dev->optionw[option] = RGB;	/* RGB */
      else
	return SANE_STATUS_INVAL;
      break;
    default:
      dev->optionw[option] = *((SANE_Word *) value);
    }
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_control_option (SANE_Handle h, SANE_Int option,
		     SANE_Action a, void *v, SANE_Int * i)
{

  if (option < 0 || option >= OPTION_MAX)
    return SANE_STATUS_INVAL;

  if (i)
    *i = 0;


  switch (a)
    {
    case SANE_ACTION_GET_VALUE:
      return getvalue (h, option, v);

    case SANE_ACTION_SET_VALUE:
      return setvalue (h, option, v, i);

    default:
      return SANE_STATUS_INVAL;
    }
}

SANE_Status
sane_get_parameters (SANE_Handle h, SANE_Parameters * p)
{
  struct device_s *dev = (struct device_s *) h;

  if (!p)
    return SANE_STATUS_INVAL;

  p->format =
    dev->optionw[COLOR_OFFSET] == RGB ? SANE_FRAME_RGB : SANE_FRAME_GRAY;
  p->last_frame = SANE_TRUE;
  p->depth = 8;

  p->pixels_per_line = dev->width;
  p->lines = dev->height;
  p->bytes_per_line = p->pixels_per_line;
  if (p->format == SANE_FRAME_RGB)
    p->bytes_per_line *= 3;

  return SANE_STATUS_GOOD;
}

static void
send_pkt (int command, int data_size, struct device_s *dev)
{
  size_t size = 32;

  DBG(100,"Sending packet %d, next data size %d, device %s\n", command, data_size, dev->devname);

  memset (dev->packet_data, 0, size);
  dev->packet_data[0] = htonl (MAGIC_NUMBER);
  dev->packet_data[1] = htonl (command);
  dev->packet_data[5] = htonl (data_size);
  sanei_usb_write_bulk (dev->dn, (unsigned char *) dev->packet_data, &size);
}


/* s: printer status */
/* Return the next packet size */
static int
wait_ack (struct device_s *dev, int *s)
{
  SANE_Status ret;
  size_t size;
  DBG(100, "Waiting scanner answer on device %s\n",dev->devname);
  do
    {
      size = 32;
      ret =
	sanei_usb_read_bulk (dev->dn, (unsigned char *) dev->packet_data,
			     &size);
    }
  while (SANE_STATUS_EOF == ret || size == 0);
  if (s)
    *s = ntohl (dev->packet_data[4]);
  return ntohl (dev->packet_data[5]);
}

static void
send_conf (struct device_s *dev)
{
  int y1, y2, x1, x2;
  size_t size = 100;
  DBG(100,"Sending configuration packet on device %s\n",dev->devname);
  y1 = (int) round2 ((dev->optionw[Y1_OFFSET] / ((double) MAX_Y_S)) * MAX_Y_H);
  y2 = (int) round2 ((dev->optionw[Y2_OFFSET] / ((double) MAX_Y_S)) * MAX_Y_H);
  x1 = (int) round2 ((dev->optionw[X1_OFFSET] / ((double) MAX_X_S)) * MAX_X_H);
  x2 = (int) round2 ((dev->optionw[X2_OFFSET] / ((double) MAX_X_S)) * MAX_X_H);

  DBG(100,"\t x1: %d, x2: %d, y1: %d, y2: %d\n",x1, x2, y1, y2);
  DBG(100,"\t brightness: %d, contrast: %d\n", dev->optionw[BRIGH_OFFSET], dev->optionw[CONTR_OFFSET]);
  DBG(100,"\t resolution: %d\n",dev->optionw[RES_OFFSET]);

  dev->conf_data[0] = htonl (0x15);
  dev->conf_data[1] = htonl (dev->optionw[BRIGH_OFFSET]);
  dev->conf_data[2] = htonl (dev->optionw[CONTR_OFFSET]);
  dev->conf_data[3] = htonl (dev->optionw[RES_OFFSET]);
  dev->conf_data[4] = htonl (0x1);
  dev->conf_data[5] = htonl (0x1);
  dev->conf_data[6] = htonl (0x1);
  dev->conf_data[7] = htonl (0x1);
  dev->conf_data[8] = 0;
  dev->conf_data[9] = 0;
  dev->conf_data[10] = htonl (0x8);
  dev->conf_data[11] = 0;
  dev->conf_data[12] = 0;
  dev->conf_data[13] = 0;
  dev->conf_data[14] = 0;
  dev->conf_data[16] = htonl (y1);
  dev->conf_data[17] = htonl (x1);
  dev->conf_data[18] = htonl (y2);
  dev->conf_data[19] = htonl (x2);
  dev->conf_data[20] = 0;
  dev->conf_data[21] = 0;
  dev->conf_data[22] = htonl (0x491);
  dev->conf_data[23] = htonl (0x352);
  dev->height_h = y2 - y1;
  if (dev->optionw[COLOR_OFFSET] == RGB)
    {
      dev->conf_data[15] = htonl (0x2);
      dev->conf_data[24] = htonl (0x1);
      DBG(100,"\t Scanning in RGB format\n");
    }
  else
    {
      dev->conf_data[15] = htonl (0x6);
      dev->conf_data[24] = htonl (0x0);
      DBG(100,"\t Scanning in Grayscale format\n");
    }
  sanei_usb_write_bulk (dev->dn, (unsigned char *) dev->conf_data, &size);
}

static SANE_Status create_buffer(struct buffer_s *buf, int buffer_size) {
  if (buf->buffer)
  {
    free(buf->buffer);
  }

  buf->buffer = malloc(buffer_size);
  if (!buf->buffer)
    return SANE_STATUS_NO_MEM;
  buf->size = buffer_size;
  buf->w_offset = 0;
  return SANE_STATUS_GOOD;
}

static SANE_Status create_buffers(struct device_s *dev, int buf_size) {
  if (create_buffer(&dev->buf_r, buf_size) != SANE_STATUS_GOOD)
    return SANE_STATUS_NO_MEM;
  if (dev->optionw[COLOR_OFFSET] == RGB)
  {
    if (create_buffer(&dev->buf_g, buf_size) != SANE_STATUS_GOOD)
      return SANE_STATUS_NO_MEM;
    if (create_buffer(&dev->buf_b, buf_size) != SANE_STATUS_GOOD)
      return SANE_STATUS_NO_MEM;
  }
  return SANE_STATUS_GOOD;
}

static SANE_Status remove_buffers(struct device_s *dev) {
  if (dev->buf_r.buffer)
    free(dev->buf_r.buffer);
  if (dev->buf_g.buffer)
    free(dev->buf_g.buffer);
  if (dev->buf_b.buffer)
    free(dev->buf_b.buffer);
  dev->buf_r.w_offset = dev->buf_g.w_offset = dev->buf_b.w_offset = 0;
  dev->buf_r.size = dev->buf_g.size = dev->buf_b.size = 0;
  dev->buf_r.buffer = dev->buf_g.buffer = dev->buf_b.buffer = NULL;
  dev->read_offset = 0;
  return SANE_STATUS_GOOD;
}

static SANE_Status get_data (struct device_s *dev)
{
  int color;
  size_t size;
  int packet_size;
  unsigned char *buffer = (unsigned char *) dev->packet_data;
  if (dev->status == STATUS_IDLE)
  {
    DBG(101, "STATUS == IDLE\n");
    return SANE_STATUS_IO_ERROR;
  }
  /* first wait a standard data pkt */
  do
  {
    size = 32;
    sanei_usb_read_bulk (dev->dn, buffer, &size);
    if (size)
    {
      if (ntohl (dev->packet_data[0]) == MAGIC_NUMBER)
      {
        if (ntohl (dev->packet_data[1]) == PKT_DATA)
          break;
        if (ntohl (dev->packet_data[1]) == PKT_END_DATA)
        {
          dev->status = STATUS_IDLE;
          DBG(100,"End of scan encountered on device %s\n",dev->devname);
          send_pkt (PKT_GO_IDLE, 0, dev);
          wait_ack (dev, NULL);
          wait_ack (dev, NULL);
          send_pkt (PKT_UNKNOW_1, 0, dev);
          wait_ack (dev, NULL);
          send_pkt (PKT_RESET, 0, dev);
          sleep (2);    /* Time for the scanning head to go back home */
          return SANE_STATUS_EOF;
        }
      }
    }
  } while (1);
  packet_size = ntohl (dev->packet_data[5]);
  if (! dev->buf_r.buffer)
  {
    /*
      For some reason scanner sends packets in order:
        <start> R G B ... R G B R G B RRR GGG BBB <end>
      To hanle the last triple portion create a triple size buffer
    */
    int buf_size = (packet_size - 24) * 3; /* 24 - size of header */ ;
    if (create_buffers(dev, buf_size) != SANE_STATUS_GOOD)
      return SANE_STATUS_NO_MEM;
  }
  /* Get the "data header" */
  do
  {
    size = 24;
    sanei_usb_read_bulk (dev->dn, buffer, &size);
  } while (!size);
  color = ntohl (dev->packet_data[0]);
  packet_size -= size;
  dev->width = ntohl (dev->packet_data[4]);
  dev->height = dev->height_h * dev->optionw[RES_OFFSET] / 100;
  dev->data_width = ntohl (dev->packet_data[5]);
  DBG(100,"Got data size %d on device %s. Scan width: %d, data width: %d\n",packet_size, dev->devname, dev->width, dev->data_width);
  /* Now, read the data */
  do
  {
    int ret;
    do
    {
      size = packet_size > 512 ? 512 : packet_size;
      ret = sanei_usb_read_bulk (dev->dn, buffer, &size);
    } while (!size || ret != SANE_STATUS_GOOD);
    packet_size -= size;
    struct buffer_s * current_buf;
    char color_char;
    switch (color)
    {
      case GRAY_LAYER:
        color_char = 'g';
        current_buf = &dev->buf_r;
        break;
      case RED_LAYER:
        color_char = 'R';
        current_buf = &dev->buf_r;
        break;
      case GREEN_LAYER:
        color_char = 'G';
        current_buf = &dev->buf_g;
        break;
      case BLUE_LAYER:
        color_char = 'B';
        current_buf = &dev->buf_b;
        break;
      default:
        DBG(101, "Unknown color code: %d \n", color);
        return SANE_STATUS_IO_ERROR;
    }
    DBG(101,"Got %c layer data on device %s\n", color_char, dev->devname);
    if (current_buf->w_offset + size > current_buf->size) {
      DBG(100, "buffer overflow\n");
      return SANE_STATUS_IO_ERROR;
    }
    memcpy(current_buf->buffer + current_buf->w_offset, buffer, size);
    current_buf->w_offset += size;
  }
  while (packet_size > 0);
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_start (SANE_Handle h)
{
  struct device_s *dev = (struct device_s *) h;
  int status;
  size_t size;

  dev->read_offset = 0;
  dev->scanned_pixels = 0;
  remove_buffers(dev);

  send_pkt (PKT_RESET, 0, dev);
  send_pkt (PKT_READ_STATUS, 0, dev);
  wait_ack (dev, &status);
  if (status)
    return SANE_STATUS_IO_ERROR;

  send_pkt (PKT_READCONF, 0, dev);

  if ((size = wait_ack (dev, NULL)))
    {
      sanei_usb_read_bulk (dev->dn, (unsigned char *) dev->conf_data, &size);
    }
  send_pkt (PKT_SETCONF, 100, dev);
  send_conf (dev);
  wait_ack (dev, NULL);

  send_pkt (PKT_START_SCAN, 0, dev);
  wait_ack (dev, NULL);
  if ((size = wait_ack (dev, NULL)))
    {
      sanei_usb_read_bulk (dev->dn, (unsigned char *) dev->conf_data, &size);
    }
  if ((size = wait_ack (dev, NULL)))
    {
      sanei_usb_read_bulk (dev->dn, (unsigned char *) dev->conf_data, &size);
    }
  if ((size = wait_ack (dev, NULL)))
    {
      sanei_usb_read_bulk (dev->dn, (unsigned char *) dev->conf_data, &size);
    }

  dev->status = STATUS_SCANNING;
  /* Get the first data */
  return get_data (dev);
}


static void
do_cancel(struct device_s *dev)
{
  while (get_data (dev) == SANE_STATUS_GOOD);
  remove_buffers(dev);
}

static int
min3 (int r, int g, int b)
{
  if (r < g && r < b)
    return r;
  if (b < r && b < g)
    return b;
  return g;
}

static int
min_buf_w_offset (struct device_s * dev)
{
  if (dev->optionw[COLOR_OFFSET] == RGB)
    return min3 (dev->buf_r.w_offset, dev->buf_g.w_offset, dev->buf_b.w_offset);
  return dev->buf_r.w_offset;
}

static int is_buf_synchronized(struct device_s * dev) {
  if (dev->optionw[COLOR_OFFSET] == RGB)
    return dev->buf_r.w_offset == dev->buf_g.w_offset
      && dev->buf_r.w_offset == dev->buf_b.w_offset;
  return 1;
}

SANE_Status
sane_read (SANE_Handle h, SANE_Byte * buf, SANE_Int maxlen, SANE_Int * len)
{
  struct device_s *dev = (struct device_s *) h;
  int available;
  int ret;
  if (dev->optionw[COLOR_OFFSET] == RGB) {
    maxlen /= 3;
  }
  *len = 0;
  if (dev->status == STATUS_IDLE)
  {
    DBG(101, "STATUS == IDLE\n");
    return SANE_STATUS_IO_ERROR;
  }
  while (min_buf_w_offset(dev) <= dev->read_offset)
  {
    ret = get_data (dev);
    if (ret != SANE_STATUS_GOOD)
    {
      if (min_buf_w_offset(dev) <= dev->read_offset) {
        return ret;
      }
    }
  }
  available = min_buf_w_offset(dev);
  int pixel_len = available - dev->read_offset;
  if (pixel_len > maxlen)
    pixel_len = maxlen;
  int img_size = dev->width * dev->height;
  for(int i=0; i<pixel_len; ++i)
  {
    int pos = dev->read_offset+i;
    if (pos % dev->data_width >= dev->width)
      continue;
    if (dev->scanned_pixels >= img_size)
    {
      DBG(101, "Extra pixels received.\n");
      break;
    }
    dev->scanned_pixels++;
    buf[(*len)++] = dev->buf_r.buffer[pos];
    if (dev->optionw[COLOR_OFFSET] == RGB)
    {
      buf[(*len)++] = dev->buf_g.buffer[pos];
      buf[(*len)++] = dev->buf_b.buffer[pos];
    }
  }
  DBG(101, "Moved %d pixels to buffer. Total pixel scanned: %d \n", *len, dev->scanned_pixels);
  if (dev->scanned_pixels == img_size)
    DBG(100, "Full image received\n");
  dev->read_offset += pixel_len;

  /*
    If w_offset is the same in all buffers and has already been completely transferred
    to the common buffer - flush buffer. It will be recreated in get_data with a reserve
    for the last triple portions
  */
  if (is_buf_synchronized(dev) && available == dev->read_offset)
  {
    remove_buffers(dev);
  }

  /* Special case where sane_cancel is called while scanning */
  if (dev->status == STATUS_CANCELING)
    {
       do_cancel(dev);
       return SANE_STATUS_CANCELLED;
    }
  return SANE_STATUS_GOOD;
}

void
sane_cancel (SANE_Handle h)
{
  struct device_s *dev = (struct device_s *) h;


  if (dev->status == STATUS_SCANNING)
    {
      dev->status = STATUS_CANCELING;
      return;
    }

  remove_buffers(dev);
}

SANE_Status
sane_set_io_mode (SANE_Handle __sane_unused__ handle,
		  SANE_Bool __sane_unused__ non_blocking)
{
  return SANE_STATUS_UNSUPPORTED;
}

SANE_Status
sane_get_select_fd (SANE_Handle __sane_unused__ handle,
		    SANE_Int __sane_unused__ * fd)
{
  return SANE_STATUS_UNSUPPORTED;
}
