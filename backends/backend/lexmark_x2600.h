/* lexmark_x2600.c: SANE backend for Lexmark x2600 scanners.

   (C) 2023 "Benoit Juin" <benoit.juin@gmail.com>

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

   **************************************************************************/
#ifndef LEXMARK_X2600_H
#define LEXMARK_X2600_H
#define BACKEND_NAME lexmark_x2600
#include "../include/sane/config.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/time.h>

#include "../include/_stdint.h"
#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/sanei_backend.h"


typedef struct Read_Buffer
{
  SANE_Int gray_offset;
  SANE_Int max_gray_offset;
  SANE_Int region;
  SANE_Int red_offset;
  SANE_Int green_offset;
  SANE_Int blue_offset;
  SANE_Int max_red_offset;
  SANE_Int max_green_offset;
  SANE_Int max_blue_offset;
  SANE_Byte *data;
  SANE_Byte *readptr;
  SANE_Byte *writeptr;
  SANE_Byte *max_writeptr;
  size_t size;
  size_t linesize;
  size_t last_line_bytes_read;
  SANE_Bool empty;
  SANE_Int image_line_no;
  SANE_Int write_byte_counter;
  SANE_Int read_byte_counter;
}
Read_Buffer;


typedef enum
{
  OPT_NUM_OPTS = 0,
  OPT_MODE,
  OPT_RESOLUTION,
  OPT_PREVIEW,

  OPT_GEOMETRY_GROUP,
  OPT_TL_X,			/* top-left x */
  OPT_TL_Y,			/* top-left y */
  OPT_BR_X,			/* bottom-right x */
  OPT_BR_Y,			/* bottom-right y */

  /* must come last: */
  NUM_OPTIONS
}
Lexmark_Options;

typedef enum
{
  READ = 0,
  WRITE = 1,
}
Debug_Packet;


typedef struct Lexmark_Device
{
  struct Lexmark_Device *next;
  SANE_Bool missing;	/**< devices has been unplugged or swtiched off */

  SANE_Device sane;
  SANE_Option_Descriptor opt[NUM_OPTIONS];
  Option_Value val[NUM_OPTIONS];
  SANE_Parameters params;
  SANE_Int devnum;
  long data_size;
  SANE_Bool initialized;
  SANE_Bool eof;
  SANE_Int x_dpi;
  SANE_Int y_dpi;
  long data_ctr;
  SANE_Bool device_cancelled;
  SANE_Int cancel_ctr;
  SANE_Byte *transfer_buffer;
  size_t bytes_read;
  size_t bytes_remaining;
  size_t bytes_in_buffer;
  SANE_Byte *read_pointer;
  Read_Buffer *read_buffer;
}
Lexmark_Device;


void debug_packet(const SANE_Byte * source, SANE_Int source_size, Debug_Packet dp);

#endif /* LEXMARK_X2600_H */
