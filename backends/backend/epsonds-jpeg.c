/*
 * epsonds-jpeg.c - Epson ESC/I-2 driver, JPEG support.
 *
 * Copyright (C) 2015 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This file is part of the SANE package.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#define DEBUG_DECLARE_ONLY

#include "sane/config.h"

#include <math.h>

#include "epsonds.h"
#include "epsonds-jpeg.h"
#include "epsonds-ops.h"
#include <setjmp.h>

struct my_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr * my_error_ptr;


METHODDEF(void) my_error_exit (j_common_ptr cinfo)
{

	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message) (cinfo, buffer);

	DBG(10,"Jpeg decode error [%s]", buffer);
}

LOCAL(struct jpeg_error_mgr *) jpeg_custom_error (struct my_error_mgr * err)
{

	struct jpeg_error_mgr* pRet  = jpeg_std_error(&(err->pub));
	err->pub.error_exit = my_error_exit;

	return pRet;
}

typedef struct
{
	struct jpeg_source_mgr pub;
	JOCTET *buffer;
	int length;
}
epsonds_src_mgr;

METHODDEF(void)
jpeg_init_source(j_decompress_ptr __sane_unused__ cinfo)
{
}

METHODDEF(void)
jpeg_term_source(j_decompress_ptr __sane_unused__ cinfo)
{
}

METHODDEF(boolean)
jpeg_fill_input_buffer(j_decompress_ptr cinfo)
{
	epsonds_src_mgr *src = (epsonds_src_mgr *)cinfo->src;
	/* read from scanner if no data? */

	src->pub.next_input_byte = src->buffer;
	src->pub.bytes_in_buffer = src->length;
	DBG(18, "reading from ring buffer, %d left\n",  src->length);

	return TRUE;
}

METHODDEF (void)
jpeg_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	epsonds_src_mgr *src = (epsonds_src_mgr *)cinfo->src;

	if (num_bytes > 0) {

		while (num_bytes > (long)src->pub.bytes_in_buffer) {
			num_bytes -= (long)src->pub.bytes_in_buffer;
			jpeg_fill_input_buffer(cinfo);
		}

		src->pub.next_input_byte += (size_t) num_bytes;
		src->pub.bytes_in_buffer -= (size_t) num_bytes;
	}
}


void eds_decode_jpeg(epsonds_scanner*s, SANE_Byte *data, SANE_Int size, ring_buffer* ringBuffer, SANE_Int isBackSide, SANE_Int needToConvertBW)
{
    struct jpeg_decompress_struct jpeg_cinfo;
   	struct my_error_mgr jpeg_err;

    {
        epsonds_src_mgr *src;

        jpeg_cinfo.err = jpeg_custom_error(&jpeg_err);

        jpeg_create_decompress(&jpeg_cinfo);

        jpeg_cinfo.src = (struct jpeg_source_mgr *)(*jpeg_cinfo.mem->alloc_small)((j_common_ptr)&jpeg_cinfo,
                            JPOOL_PERMANENT, sizeof(epsonds_src_mgr));

        memset(jpeg_cinfo.src, 0x00, sizeof(epsonds_src_mgr));
;
    	src = (epsonds_src_mgr *)jpeg_cinfo.src;
        src->pub.init_source = jpeg_init_source;
        src->pub.fill_input_buffer = jpeg_fill_input_buffer;
        src->pub.skip_input_data = jpeg_skip_input_data;
        src->pub.resync_to_restart = jpeg_resync_to_restart;
        src->pub.term_source = jpeg_term_source;
        src->pub.bytes_in_buffer = 0;
        src->pub.next_input_byte = NULL;
		src->buffer = (JOCTET*)data;
		src->length = size;
    }
    {
	    if (jpeg_read_header(&jpeg_cinfo, TRUE)) {

		if (jpeg_start_decompress(&jpeg_cinfo)) {

			DBG(10,"%s: w: %d, h: %d, components: %d\n",
				__func__,
				jpeg_cinfo.output_width, jpeg_cinfo.output_height,
				jpeg_cinfo.output_components);
		}
        }
    }
    {
		int sum = 0;
        int bufSize = jpeg_cinfo.output_width * jpeg_cinfo.output_components;

		int monoBufSize = (jpeg_cinfo.output_width + 7)/8;

        JSAMPARRAY scanlines = (jpeg_cinfo.mem->alloc_sarray)((j_common_ptr)&jpeg_cinfo, JPOOL_IMAGE, bufSize, 1);
        while (jpeg_cinfo.output_scanline < jpeg_cinfo.output_height) {
            int l = jpeg_read_scanlines(&jpeg_cinfo, scanlines, 1);
            if (l == 0) {
                break;
            }
			sum += l;

			if (needToConvertBW)
			{
				SANE_Byte* bytes = scanlines[0];

				SANE_Int imgPos = 0;

				for (int i = 0; i < monoBufSize; i++)
				{
					SANE_Byte outByte = 0;

                    for(SANE_Int bitIndex = 0; bitIndex < 8 && imgPos < bufSize; bitIndex++) {
						//DBG(10,"bytes[imgPos] = %d\n", bytes[imgPos]);

                         if(bytes[imgPos] >= 110) {
                               SANE_Byte bit = 7 - (bitIndex % 8);
                               outByte     |= (1<< bit);
                         }
						 imgPos += 1;
                  	 }
						//DBG(10,"outByte = %d\n", outByte);
					eds_ring_write(ringBuffer, &outByte, 1);
				}
			}
			else
			{
				eds_ring_write(ringBuffer, scanlines[0], bufSize);
			}

			// decode until valida data
			if (isBackSide)
			{
				if (sum >= s->height_back)
				{
					break;
				}
			}else
			{
				if (sum >= s->height_front)
				{
					break;
				}
			}
        }
		DBG(10,"decodded lines = %d\n", sum);

		// abandon unncessary data
		if ((JDIMENSION)sum < jpeg_cinfo.output_height)
		{
			// unncessary data
			while(1)
			{
				int l = jpeg_read_scanlines(&jpeg_cinfo, scanlines, 1);
				if (l == 0)
				{
					break;
				}
			}
		}

		// if not auto crop mode padding to lines
		if (s->val[OPT_ADF_CRP].w == 0)
		{
			unsigned char* padding = malloc(s->params.bytes_per_line);
			memset(padding, 255, s->params.bytes_per_line);
			DBG(10,"padding data lines = %d to %d pa \n", sum,  s->params.lines);

			while(sum < s->params.lines)
			{
				eds_ring_write(ringBuffer, padding, bufSize);
				sum++;
			}

			free(padding);
			padding = NULL;
		}
    }
    {
        jpeg_finish_decompress(&jpeg_cinfo);
        jpeg_destroy_decompress(&jpeg_cinfo);
    }
    return;
}
