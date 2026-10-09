/*
 * epsonds.c - Epson ESC/I-2 driver, JPEG support.
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
void eds_decode_jpeg(epsonds_scanner*s, SANE_Byte *data, SANE_Int size, ring_buffer* ringBuffer, SANE_Int isBackSide, SANE_Int needToConvertBW);
