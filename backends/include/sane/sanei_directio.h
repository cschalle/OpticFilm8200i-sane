#ifndef __SANEI_DIRECTIO_H__
#define __SANEI_DIRECTIO_H__

#ifdef HAVE_SYS_IO_H
#include <sys/io.h>
#endif

#ifdef ENABLE_PARPORT_DIRECTIO

#if (! defined SANE_INB ) && ( defined HAVE_SYS_HW_H )	/* OS/2 EMX case */
#define SANE_INB 1
#endif /* OS/2 EMX case */



#if (! defined SANE_INB ) && ( defined HAVE_MACHINE_CPUFUNC_H )	/* FreeBSD case */
#define SANE_INB 2
#endif /* FreeBSD case */


/* linux GCC on i386 */
#if ( ! defined SANE_INB ) && ( defined HAVE_SYS_IO_H ) && ( defined __GNUC__ ) && ( defined __i386__ )
#define SANE_INB 3
#endif /* linux GCC on i386 */


/* linux GCC non i386 */
#if ( ! defined SANE_INB ) && ( defined HAVE_SYS_IO_H ) && ( defined __GNUC__ ) && ( ! defined __i386__ )
#define SANE_INB 4
#endif /* linux GCC non i386 */


/* ICC on i386 */
#if ( ! defined SANE_INB ) && ( defined __INTEL_COMPILER ) && ( defined __i386__ )
#define SANE_INB 5
#endif /* ICC on i386 */

/* direct io requested, but no valid inb/oub */
#if ( ! defined SANE_INB) && ( defined ENABLE_PARPORT_DIRECTIO )
#warning "ENABLE_PARPORT_DIRECTIO cannot be used du to lack of inb/out definition"
#undef ENABLE_PARPORT_DIRECTIO
#endif

#endif /* ENABLE_PARPORT_DIRECTIO */

/*
 * no inb/outb without --enable-parport-directio *
 */
#ifndef ENABLE_PARPORT_DIRECTIO
#define SANE_INB 0
#endif /* ENABLE_PARPORT_DIRECTIO is not defined */

/* we need either direct io or ppdev */
#if ! defined ENABLE_PARPORT_DIRECTIO && ! defined HAVE_LINUX_PPDEV_H && ! defined HAVE_DEV_PPBUS_PPI_H
#define IO_SUPPORT_MISSING
#endif


extern int sanei_ioperm (int start, int length, int enable);
extern unsigned char sanei_inb (unsigned int port);
extern void sanei_outb (unsigned int port, unsigned char value);
extern void sanei_insb (unsigned int port, unsigned char *addr,
		 unsigned long count);
extern void sanei_insl (unsigned int port, unsigned char *addr,
		 unsigned long count);
extern void sanei_outsb (unsigned int port, const unsigned char *addr,
	          unsigned long count);
extern void sanei_outsl (unsigned int port, const unsigned char *addr,
	          unsigned long count);

#endif // __SANEI_DIRECTIO_H__
