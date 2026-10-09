/* scanimage -- command line scanning utility
 * Uses the SANE library.
 *
 * Copyright (C) 2021 Thierry HUCHARD <thierry@ordissimo.com>
 *
 * For questions and comments contact the sane-devel mailinglist (see
 * http://www.sane-project.org/mailing-lists.html).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef __JPEG_TO_PDF_H__
#define __JPEG_TO_PDF_H__

#include "../include/_stdint.h"

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"



#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

typedef long long SANE_Int64;

/* sane_pdf_StartPage - type */
enum {
	SANE_PDF_IMAGE_COLOR = 0,	/* RGB24bit */
	SANE_PDF_IMAGE_GRAY,		/* Gray8bit */
	SANE_PDF_IMAGE_MONO,		/* Gray1bit */
	SANE_PDF_IMAGE_NUM,
};

/* sane_pdf_StartPage - rotate */
enum {
	SANE_PDF_ROTATE_OFF = 0,	/* rotate off */
	SANE_PDF_ROTATE_ON,			/* rotate 180 degrees */
};


typedef struct mynode
{
	SANE_Int		page;
	SANE_Int		show_page;
	SANE_Int		rotate;
	struct mynode	*prev;
	struct mynode	*next;
	FILE*			fd;
	SANE_Int		file_size;
	SANE_Byte		file_path[ PATH_MAX ];
} SANE_PDF_NODE, *LPSANE_PDF_NODE;


SANE_Int sane_pdf_open( void **ppw, FILE* fd );
void sane_pdf_close( void *pw );

SANE_Int sane_pdf_start_doc( void *pw );
SANE_Int sane_pdf_end_doc( void *pw );

SANE_Int sane_pdf_start_page( void *pw, SANE_Int w, SANE_Int h, SANE_Int res, SANE_Int type, SANE_Int rotate );
SANE_Int sane_pdf_end_page( void *pw );

#endif /* __JPEG_TO_PDF_H__ */
