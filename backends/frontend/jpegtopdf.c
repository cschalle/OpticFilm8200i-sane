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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "jpegtopdf.h"

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#define	SANE_NO_ERR			(0)
#define	SANE_NO_ERR_CANCLED	(1)

#define	SANE_ERR			(-128)
#define	SANE_FILE_ERR		(-1)


/* Creater/Producer */
#define SANE_PDF_CREATER "sane"
#define SANE_PDF_PRODUCER "sane"

/* PDF File Header */
#define SANE_PDF_HEADER "%%PDF-1.3\n"

/* trailer format */
#define SANE_PDF_TRAILER_OBJ "trailer\n<<\n/Size %d\n/Root 1 0 R\n/Info 3 0 R\n>>\nstartxref\n%lld\n%%%%EOF\n"

/* xref format */
#define SANE_PDF_XREF_OBJ1 "xref\n0 %d\n0000000000 65535 f \n"
#define SANE_PDF_XREF_OBJ2 "%010lld 00000 n \n"

/* Catalog format */
#define SANE_PDF_CATALOG_OBJ "1 0 obj\n<<\n/Type /Catalog\n/Pages 2 0 R\n>>\nendobj\n"

/* Pages format */
#define SANE_PDF_PAGES_OBJ1 "2 0 obj\n<<\n/Type /Pages\n/Kids [ "
#define SANE_PDF_PAGES_OBJ2 "%d 0 R "
#define SANE_PDF_PAGES_OBJ3 "]\n/Count %d\n>>\nendobj\n"

/* Info format */
#define SANE_PDF_INFO_OBJ "3 0 obj\n<<\n/Creator (" SANE_PDF_CREATER ")\n/Producer (" SANE_PDF_PRODUCER ")\n/CreationDate %s\n>>\nendobj\n"
#define SANE_PDF_INFO_DATES "(D:%4d%02d%02d%02d%02d%02d%c%02d'%02d')"

/* Page format */
#define SANE_PDF_PAGE_OBJ1 "%d 0 obj\n<<\n/Type /Page\n/Parent 2 0 R\n"
#define SANE_PDF_PAGE_OBJ2 "/Resources\n<<\n/XObject << /Im%d %d 0 R >>\n/ProcSet [ /PDF /%s ]\n>>\n"
#define SANE_PDF_PAGE_OBJ3 "/MediaBox [ 0 0 %d %d ]\n/Contents %d 0 R\n>>\nendobj\n"
#define SANE_PDF_PAGE_OBJ3_180 "/Rotate 180\n/MediaBox [ 0 0 %d %d ]\n/Contents %d 0 R\n>>\nendobj\n"
#define SANE_PDF_PAGE_OBJ		SANE_PDF_PAGE_OBJ1 SANE_PDF_PAGE_OBJ2 SANE_PDF_PAGE_OBJ3
#define SANE_PDF_PAGE_OBJ_180	SANE_PDF_PAGE_OBJ1 SANE_PDF_PAGE_OBJ2 SANE_PDF_PAGE_OBJ3_180

/* Contents format */
#define SANE_PDF_CONTENTS_OBJ1 "%d 0 obj\n<< /Length %d 0 R >>\nstream\n"
#define SANE_PDF_CONTENTS_OBJ2 "q\n%d 0 0 %d 0 0 cm\n/Im%d Do\nQ\n"

/* XObject(Image) format */
#define SANE_PDF_IMAGE_OBJ1 "%d 0 obj\n<<\n/Length %d 0 R\n/Type /XObject\n/Subtype /Image\n"
#define SANE_PDF_IMAGE_OBJ2 "/Width %d /Height %d\n/ColorSpace /%s\n/BitsPerComponent %d\n"
#define SANE_PDF_IMAGE_OBJ3 "/Filter /DCTDecode\n>>\nstream\n"
#define SANE_PDF_IMAGE_OBJ	SANE_PDF_IMAGE_OBJ1 SANE_PDF_IMAGE_OBJ2 SANE_PDF_IMAGE_OBJ3

/* Length format */
#define SANE_PDF_LENGTH_OBJ "%d 0 obj\n%d\nendobj\n"

/* end of stream/object */
#define SANE_PDF_END_ST_OBJ "\nendstream\nendobj\n"


/* object id of first page */
#define SANE_PDF_FIRST_PAGE_ID (4)

/* xref max value */
#define SANE_PDF_XREF_MAX (9999999999LL)

/* pdfwork->offset_table */
enum {
	SANE_PDF_ENDDOC_XREF = 0,
	SANE_PDF_ENDDOC_CATALOG,
	SANE_PDF_ENDDOC_PAGES,
	SANE_PDF_ENDDOC_INFO,
	SANE_PDF_ENDDOC_NUM,
};

/* pdfpage->offset_table */
enum {
	SANE_PDF_PAGE_OBJ_PAGE = 0,
	SANE_PDF_PAGE_OBJ_IMAGE,
	SANE_PDF_PAGE_OBJ_IMAGE_LEN,
	SANE_PDF_PAGE_OBJ_CONTENTS,
	SANE_PDF_PAGE_OBJ_CONTENTS_LEN,
	SANE_PDF_PAGE_OBJ_NUM,
};

/* Page object info */
typedef struct sane_pdf_page {
	SANE_Int		page;			/* page No. */
	SANE_Int		obj_id;			/* Page object id */
	SANE_Int		image_type;		/* ColorSpace, BitsPerComponent */
	SANE_Int		res;			/* image resolution */
	SANE_Int		w;				/* width (image res) */
	SANE_Int		h;				/* height (image res) */
	SANE_Int		w_72;			/* width (72dpi) */
	SANE_Int		h_72;			/* height (72dpi) */
	SANE_Int64		offset_table[SANE_PDF_PAGE_OBJ_NUM];	/* xref table */
	SANE_Int		stream_len;		/* stream object length */
	SANE_Int		status;			/* page object status */
	struct sane_pdf_page	*prev;	/* previous page data */
	struct sane_pdf_page	*next;	/* next page data */
} SANE_pdf_page;


/* PDF Work */
typedef struct {
	SANE_Int		obj_num;		/* xref - num, trailer - Size */
	SANE_Int		page_num;		/* Pages - Count */
	SANE_Int64		offset_table[SANE_PDF_ENDDOC_NUM];	/* xref table */
	SANE_pdf_page		*first;			/* first page data */
	SANE_pdf_page		*last;			/* last page data */
	FILE*			fd;				/* destination file */
} SANE_pdf_work;

static SANE_Int re_write_if_fail(
                FILE *               fd,
                void *               lpSrc,
                SANE_Int             writeSize )
{
        SANE_Int       ret = SANE_ERR, ldata_1st, ldata_2nd;

        if( ( fd == NULL ) || ( lpSrc == NULL ) || ( writeSize <= 0 ) ) {
                fprintf ( stderr, "[re_write_if_fail]Parameter is error.\n" );
                goto    EXIT;
        }
		else if( ( ldata_1st = fwrite( (SANE_Byte *)lpSrc, 1, writeSize, fd ) ) != writeSize ){
                fprintf ( stderr, "[re_write_if_fail]Can't write file(1st request:%d -> write:%d).\n", writeSize, ldata_1st );
                if( ( ldata_2nd = fwrite( (SANE_Byte*)lpSrc+ldata_1st, 1, writeSize-ldata_1st, fd) ) != writeSize-ldata_1st ){ /* For detect write() error */
                        fprintf ( stderr, "[re_write_if_fail]Can't write file(2nd request:%d -> write:%d).\n", writeSize-ldata_1st, ldata_2nd );
                        goto    EXIT;
                }
        }
        ret = SANE_NO_ERR;
EXIT:
        return  ret;
}

static SANE_Int64 _get_current_offset( FILE *fd )
{
	SANE_Int64	offset64 = (SANE_Int64)fseek( fd, 0, SEEK_CUR );

	if ( offset64 > SANE_PDF_XREF_MAX ) offset64 = -1;

	return offset64;
}

static SANE_Int _get_current_time( struct tm *pt, SANE_Byte *sign_c, int *ptz_h, int *ptz_m )
{
	SANE_Int		ret = SANE_ERR;
	time_t			t;
	long			tz;

	if ( pt == NULL || sign_c == NULL || ptz_h == NULL || ptz_m == NULL ) {
		goto EXIT;
	}

	memset ((void *)pt, 0, sizeof(struct tm) );
	/* get time */
	if( ( t = time( NULL ) ) < 0 ) {
		fprintf ( stderr, " Can't get time.\n" );
		goto EXIT;
	}
	/* get localtime */
	if ( localtime_r( &t, pt ) == NULL ) {
		fprintf ( stderr, " Can't get localtime.\n" );
		goto EXIT;
	}
	/* get time difference ( OHH'mm' ) */
#ifdef __FreeBSD__
       tz = -pt->tm_gmtoff;
#else
	tz = timezone;
#endif
	if ( tz > 0 ) {
		*sign_c = '-';
	}
	else {
		tz = -tz;
		*sign_c = '+';
	}
	*ptz_h = tz / 60 / 60;
	*ptz_m = ( tz / 60 ) % 60;

	ret = SANE_NO_ERR;
EXIT:
	return ret;
}

SANE_Int sane_pdf_open( void **ppw, FILE *fd )
{
	SANE_Int		ret = SANE_ERR;
	SANE_pdf_work		*p = NULL;

	if ( fd == NULL ) {
		fprintf ( stderr, " Initialize parameter is error!\n" );
		goto	EXIT;
	}
	else if ( ( p = (SANE_pdf_work *)calloc(1, sizeof(SANE_pdf_page) ) ) == NULL ) {
		fprintf ( stderr, " Can't get work memory!\n" );
		goto	EXIT;
	}

	p->fd = fd;
	p->obj_num = SANE_PDF_FIRST_PAGE_ID - 1;	/* Catalog, Pages, Info */
	p->page_num = 0;
	p->first = NULL;
	p->last = NULL;

	*ppw = (void *)p;

	ret = SANE_NO_ERR;
EXIT:
	return ret;
}

void sane_pdf_close( void *pw )
{
	SANE_pdf_page		*cur, *next;
	SANE_pdf_work		*pwork = (SANE_pdf_work *)pw;

	if ( pwork == NULL ) {
		fprintf ( stderr, " Initialize parameter is error!\n");
		goto	EXIT;
	}

	cur = pwork->first;
	while ( cur != NULL ) {
		next = cur->next;
		free( (void *)cur );
		cur = next;
	}

	free ( (void *)pwork );

EXIT:
	return ;
}

SANE_Int sane_pdf_start_doc( void *pw )
{
	SANE_Int		ret = SANE_ERR, ldata;
	SANE_Byte		str[32];
	SANE_Int			len;
	SANE_pdf_work		*pwork = (SANE_pdf_work *)pw;

	if ( pwork == NULL ) {
		fprintf ( stderr, " Initialize parameter is error!\n");
		goto	EXIT;
	}

	len = snprintf( (char*)str, sizeof(str), SANE_PDF_HEADER );
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	ret = SANE_NO_ERR;
EXIT:
	return ret;
}

SANE_Int sane_pdf_end_doc( void *pw )
{
	SANE_Int		ret = SANE_ERR, ldata, i, size, w_count;
	SANE_pdf_page		*p = NULL;
	SANE_Byte		str[1024], str_t[64];
	SANE_Int			len;
	SANE_pdf_work		*pwork = (SANE_pdf_work *)pw;

	struct tm		tm;
	SANE_Byte		sign_c;
	int				tz_h = 0, tz_m = 0;

	if ( pwork == NULL ) {
		fprintf ( stderr, " Initialize parameter is error!\n");
		goto	EXIT;
	}

	size = pwork->obj_num + 1;
	w_count = 1;

	/* <1> Pages */
	if ( ( pwork->offset_table[ SANE_PDF_ENDDOC_PAGES ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write Pages(1) */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_PAGES_OBJ1 );
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* write Pages(2) ... Kids array */
	p = pwork->first;
	i = 0;
	while ( p != NULL ) {
		i++;
		if ( p->status != SANE_NO_ERR ) {
			fprintf ( stderr, " page(%d) is NG!\n", i );
			goto EXIT;
		}

		len = snprintf( (char*)str, sizeof(str), SANE_PDF_PAGES_OBJ2, (int)p->obj_id );	/* Page object id */
		if ( (size_t)len >= sizeof(str) || len < 0 ) {
			fprintf ( stderr, " string is too long!\n" );
			goto EXIT;
		}
		if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
			fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
			goto EXIT;
		}

		p = p->next;
	}

	/* write Pages(3) */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_PAGES_OBJ3, (int)pwork->page_num );	/* Count */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* <2> Catalog */
	if ( ( pwork->offset_table[ SANE_PDF_ENDDOC_CATALOG ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write Catalog */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_CATALOG_OBJ );
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* <3> Info */
	if ( ( pwork->offset_table[ SANE_PDF_ENDDOC_INFO ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	if ( _get_current_time( &tm, &sign_c, &tz_h, &tz_m ) == SANE_ERR ) {
		fprintf ( stderr, " Error is occured in _get_current_time.\n" );
		goto EXIT;
	}
	/* Dates format */
	len = snprintf((char*)str_t, sizeof(str_t), SANE_PDF_INFO_DATES,
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, sign_c, tz_h, tz_m );
	if ( (size_t)len >= sizeof(str_t) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	/* write Info */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_INFO_OBJ, str_t );			/* CreationDate */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* <4> xref */
	if ( ( pwork->offset_table[ SANE_PDF_ENDDOC_XREF ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write xref(1) */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_XREF_OBJ1, (int)size );	/* object num */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* write xref(2) */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_XREF_OBJ2 SANE_PDF_XREF_OBJ2 SANE_PDF_XREF_OBJ2,
			pwork->offset_table[ SANE_PDF_ENDDOC_CATALOG ],			/* object id = 1 : Catalog */
			pwork->offset_table[ SANE_PDF_ENDDOC_PAGES ],			/* object id = 2 : Pages */
			pwork->offset_table[ SANE_PDF_ENDDOC_INFO ] );			/* object id = 3 : Info */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}
	w_count += SANE_PDF_FIRST_PAGE_ID - 1;

	/* write xref(3) */
	p = pwork->first;
	while ( p != NULL ) {
		/* write offset : SANE_PDF_PAGE_OBJ_PAGE -> SANE_PDF_PAGE_OBJ_CONTENTS_LEN */
		for ( i = 0; i < SANE_PDF_PAGE_OBJ_NUM; i++ ) {
			len = snprintf( (char*)str, sizeof(str), SANE_PDF_XREF_OBJ2, p->offset_table[ i ] );	/* object id = 3 ~ */
			if ( (size_t)len >= sizeof(str) || len < 0 ) {
				fprintf ( stderr, " string is too long!\n" );
				goto EXIT;
			}
			if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
				fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
				goto EXIT;
			}
			w_count ++;
		}
		p = p->next;
	}
	/* check object number */
	if ( w_count != size ) {
		fprintf ( stderr, " object number is wrong.\n" );
		goto EXIT;
	}

	/* <4> trailer */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_TRAILER_OBJ,
			(int)size,											/* object num */
			pwork->offset_table[ SANE_PDF_ENDDOC_XREF ] );		/* xref offset */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}


	ret = SANE_NO_ERR;
EXIT:
	return ret;
}

SANE_Int sane_pdf_start_page(
	void		*pw,
	SANE_Int		w,
	SANE_Int		h,
	SANE_Int		res,
	SANE_Int		type,
	SANE_Int		rotate )
{
	SANE_Int		ret = SANE_ERR, ldata;
	SANE_pdf_page		*p = NULL;
	SANE_Byte		str[1024];
	SANE_Int			len, len_c;
	SANE_Byte		*ProcSetImage[SANE_PDF_IMAGE_NUM]		= { (SANE_Byte *)"ImageC", (SANE_Byte *)"ImageG", (SANE_Byte *)"ImageG" };
	SANE_Byte		*ColorSpace[SANE_PDF_IMAGE_NUM]			= { (SANE_Byte *)"DeviceRGB", (SANE_Byte *)"DeviceGray", (SANE_Byte *)"DeviceGray" };
	SANE_Int		BitsPerComponent[SANE_PDF_IMAGE_NUM]	= { 8, 8, 1 };
	SANE_pdf_work		*pwork = (SANE_pdf_work *)pw;

	if ( pwork == NULL || w <= 0 || h <= 0 || res <= 0 ||
			!( type == SANE_PDF_IMAGE_COLOR || type == SANE_PDF_IMAGE_GRAY || type == SANE_PDF_IMAGE_MONO ) ||
			!( rotate == SANE_PDF_ROTATE_OFF || rotate == SANE_PDF_ROTATE_ON ) ) {
		fprintf ( stderr, " Initialize parameter is error!\n");
		goto	EXIT;
	}
	else if ( ( p = (SANE_pdf_page *)calloc( 1, sizeof(SANE_pdf_page) ) ) == NULL ) {
		fprintf ( stderr, " Can't get work memory!\n" );
		goto	EXIT;
	}

	pwork->obj_num += SANE_PDF_PAGE_OBJ_NUM;
	pwork->page_num ++;

	p->prev = p->next = NULL;
	if ( pwork->first == NULL ) {
		/* append first page */
		pwork->first = p;
	}
	if ( pwork->last == NULL ) {
		/* append first page */
		pwork->last = p;
	}
	else {
		/* append page */
		pwork->last->next = p;
		p->prev = pwork->last;
		pwork->last = p;
	}

	p->page = pwork->page_num;
	/* page obj id : page1=4, page2=4+5=9, page3=4+5*2=14, ... */
	p->obj_id = SANE_PDF_FIRST_PAGE_ID + ( p->page - 1 ) * SANE_PDF_PAGE_OBJ_NUM;
	p->image_type = type;
	p->res = res;
	p->w = w; p->h = h;
	p->w_72 = w * 72 / res; p->h_72 = h * 72 / res;
	p->stream_len = 0;
	p->status = SANE_ERR;

	/* <1> Page */
	if ( ( p->offset_table[ SANE_PDF_PAGE_OBJ_PAGE ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write Page */
	if ( rotate == SANE_PDF_ROTATE_OFF ) {
		len = snprintf( (char*)str, sizeof(str), SANE_PDF_PAGE_OBJ,
				(int)(p->obj_id + SANE_PDF_PAGE_OBJ_PAGE),			/* object id ( Page ) */
				(int)p->page,										/* ImX (X = page number) ... XObject/Image Name */
				(int)(p->obj_id + SANE_PDF_PAGE_OBJ_IMAGE),			/* object id ( XObject/Image ) */
				ProcSetImage[ type ],								/* ProcSet */
				(int)p->w_72, (int)p->h_72,							/* MediaBox */
				(int)(p->obj_id + SANE_PDF_PAGE_OBJ_CONTENTS) );	/* object id ( Contents ) */
	}
	else {
		len = snprintf( (char*)str, sizeof(str), SANE_PDF_PAGE_OBJ_180,
				(int)(p->obj_id + SANE_PDF_PAGE_OBJ_PAGE),			/* object id ( Page ) */
				(int)p->page,										/* ImX (X = page number) ... XObject/Image Name */
				(int)(p->obj_id + SANE_PDF_PAGE_OBJ_IMAGE),			/* object id ( XObject/Image ) */
				ProcSetImage[ type ],								/* ProcSet */
				(int)p->w_72, (int)p->h_72,							/* MediaBox */
				(int)(p->obj_id + SANE_PDF_PAGE_OBJ_CONTENTS) );	/* object id ( Contents ) */
	}
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* <2> Contents */
	if ( ( p->offset_table[ SANE_PDF_PAGE_OBJ_CONTENTS ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write Contents(1) */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_CONTENTS_OBJ1,
			(int)(p->obj_id + SANE_PDF_PAGE_OBJ_CONTENTS),			/* object id ( Contents ) */
			(int)(p->obj_id + SANE_PDF_PAGE_OBJ_CONTENTS_LEN) );	/* object id ( Length of Contents ) */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}
	/* write Contents(2) */
	len_c = len = snprintf( (char*)str, sizeof(str), SANE_PDF_CONTENTS_OBJ2,
			(int)p->w_72, (int)p->h_72,							/* CTM ( scaling ) */
			(int)p->page );										/* ImX (X = page number) ... XObject/Image Name */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* write Contents(3) */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_END_ST_OBJ );
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* <3> Length of Contents - stream */
	if ( ( p->offset_table[ SANE_PDF_PAGE_OBJ_CONTENTS_LEN ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write Length */
	len = snprintf( (char *)str, sizeof(str), SANE_PDF_LENGTH_OBJ,
			(int)(p->obj_id + SANE_PDF_PAGE_OBJ_CONTENTS_LEN),		/* object id ( Length of Contents ) */
			len_c );												/* length value */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* <4> XObject(Image) */
	if ( ( p->offset_table[ SANE_PDF_PAGE_OBJ_IMAGE ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write XObject */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_IMAGE_OBJ,
			(int)(p->obj_id + SANE_PDF_PAGE_OBJ_IMAGE),		/* object id ( XObject(Image) ) */
			(int)(p->obj_id + SANE_PDF_PAGE_OBJ_IMAGE_LEN),	/* object id ( Length of XObject ) */
			(int)p->w, (int)p->h,							/* Width/Height */
			ColorSpace[ type ],								/* ColorSpace */
			(int)BitsPerComponent[ type ] );				/* BitsPerComponent */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	ret = SANE_NO_ERR;
EXIT:
	return ret;

}

SANE_Int sane_pdf_end_page( void *pw )
{
	SANE_Int		ret = SANE_ERR, ldata;
	SANE_pdf_page		*p = NULL;
	SANE_Byte		str[1024];
	SANE_Int			len;
	SANE_pdf_work		*pwork = (SANE_pdf_work *)pw;

	if ( pwork == NULL ) {
		fprintf ( stderr, " Initialize parameter is error!\n" );
		goto	EXIT;
	}

	p = pwork->last;

	/* <1> endstream, endobj (XObject) */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_END_ST_OBJ );
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	/* <2> Length of XObject - stream */
	if ( ( p->offset_table[ SANE_PDF_PAGE_OBJ_IMAGE_LEN ] = _get_current_offset( pwork->fd ) ) < 0 ) {
		fprintf ( stderr, " offset > %lld\n", SANE_PDF_XREF_MAX );
		goto EXIT;
	}
	/* write Length */
	len = snprintf( (char*)str, sizeof(str), SANE_PDF_LENGTH_OBJ,
			(int)(p->obj_id + SANE_PDF_PAGE_OBJ_IMAGE_LEN),		/* object id ( Length of XObject stream ) */
			(int)p->stream_len );								/* length value */
	if ( (size_t)len >= sizeof(str) || len < 0 ) {
		fprintf ( stderr, " string is too long!\n" );
		goto EXIT;
	}
	if ( ( ldata = re_write_if_fail( pwork->fd, str, len ) ) < 0 ) {
		fprintf ( stderr, " Error is occured in re_write_if_fail.\n" );
		goto EXIT;
	}

	ret = SANE_NO_ERR;
	p->status = SANE_NO_ERR;
EXIT:
	return ret;
}
