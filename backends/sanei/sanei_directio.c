/*************************************************/
/* here we define sanei_inb/sanei_outb based on  */
/* OS dependent inb/outb definitions             */
/* SANE_INB is defined whenever a valid inb/outb */
/* definition has been found                     */
/*************************************************/

#include "../include/sane/config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/sanei_directio.h"

#ifdef ENABLE_PARPORT_DIRECTIO

#define TEST_SANE_INB(val) ( SANE_INB == val )

#if ( TEST_SANE_INB(1) )	/* OS/2 EMX case */
int
sanei_ioperm (int start, int length, int enable)
{
  if (enable)
    return _portaccess (port, port + length - 1);
  return 0;
}

unsigned char
sanei_inb (unsigned int port)
{
  return _inp8 (port) & 0xFF;
}

void
sanei_outb (unsigned int port, unsigned char value)
{
  _outp8 (port, value);
}

void
sanei_insb (unsigned int port, unsigned char *addr, unsigned long count)
{
  _inps8 (port, (unsigned char *) addr, count);
}

void
sanei_insl (unsigned int port, unsigned char *addr, unsigned long count)
{
  _inps32 (port, (unsigned long *) addr, count);
}

void
sanei_outsb (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  _outps8 (port, (unsigned char *) addr, count);
}

void
sanei_outsl (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  _outps32 (port, (unsigned long *) addr, count);
}
#endif /* OS/2 EMX case */



#if ( TEST_SANE_INB(2) )	/* FreeBSD case */
int
sanei_ioperm (int start, int length, int enable)
{
#ifdef HAVE_I386_SET_IOPERM
  return i386_set_ioperm (start, length, enable);
#else
  int fd = 0;

  /* makes compilers happy */
  start = length + enable;
  fd = open ("/dev/io", O_RDONLY);
  if (fd > 0)
    return 0;
  return -1;
#endif
}

unsigned char
sanei_inb (unsigned int port)
{
  return inb (port);
}

void
sanei_outb (unsigned int port, unsigned char value)
{
  outb (port, value);
}

void
sanei_insb (unsigned int port, unsigned char *addr, unsigned long count)
{
  insb (port, addr, count);
}

void
sanei_insl (unsigned int port, unsigned char *addr, unsigned long count)
{
  insl (port, addr, count);
}

void
sanei_outsb (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  outsb (port, addr, count);
}

void
sanei_outsl (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  outsl (port, addr, count);
}
#endif /* FreeBSD case */


/* linux GCC on i386 */
#if ( TEST_SANE_INB(3) )	/* FreeBSD case */

int
sanei_ioperm (int start, int length, int enable)
{
#ifdef HAVE_IOPERM
  return ioperm (start, length, enable);
#else
  /* linux without ioperm ? hum ... */
  /* makes compilers happy */
  start = length + enable;
  return 0;
#endif
}

unsigned char
sanei_inb (unsigned int port)
{
  return inb (port);
}

void
sanei_outb (unsigned int port, unsigned char value)
{
  outb (value, port);
}

void
sanei_insb (unsigned int port, unsigned char *addr, unsigned long count)
{
  insb (port, addr, count);
}

void
sanei_insl (unsigned int port, unsigned char *addr, unsigned long count)
{
  insl (port, addr, count);
}

void
sanei_outsb (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  outsb (port, addr, count);
}

void
sanei_outsl (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  /* oddly, 32 bit I/O are done with outsw instead of the expected outsl */
  outsw (port, addr, count);
}
#endif /* linux GCC on i386 */


/* linux GCC non i386 */
#if ( TEST_SANE_INB(4) )
int
sanei_ioperm (int start, int length, int enable)
{
#ifdef HAVE_IOPERM
  return ioperm (start, length, enable);
#else
  /* linux without ioperm ? hum ... */
  /* makes compilers happy */
  start = length + enable;
  return 0;
#endif
}

unsigned char
sanei_inb (unsigned int port)
{
  return inb (port);
}

void
sanei_outb (unsigned int port, unsigned char value)
{
  outb (value, port);
}

void
sanei_insb (unsigned int port, unsigned char *addr, unsigned long count)
{
  unsigned int i;

  for (i = 0; i < count; i++)
    addr[i] = sanei_inb (port);
}

void
sanei_insl (unsigned int port, unsigned char *addr, unsigned long count)
{
  unsigned int i;

  for (i = 0; i < count * 4; i++)
    addr[i] = sanei_inb (port);
}

void
sanei_outsb (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  unsigned int i;

  for (i = 0; i < count; i++)
    sanei_outb (port, addr[i]);
}

void
sanei_outsl (unsigned int port, const unsigned char *addr,
	     unsigned long count)
{
  unsigned int i;

  for (i = 0; i < count * 4; i++)
    sanei_outb (port, addr[i]);
}
#endif /* linux GCC non i386 */


/* ICC on i386 */
#if ( TEST_SANE_INB(5) )
int
sanei_ioperm (int start, int length, int enable)
{
#ifdef HAVE_IOPERM
  return ioperm (start, length, enable);
#else
  /* ICC without ioperm() ... */
  /* makes compilers happy */
  start = length + enable;
  return 0;
#endif
}
unsigned char
sanei_inb (unsigned int port)
{
  unsigned char ret;

  __asm__ __volatile__ ("inb %%dx,%%al":"=a" (ret):"d" ((u_int) port));
  return ret;
}

void
sanei_outb (unsigned int port, unsigned char value)
{
  __asm__ __volatile__ ("outb %%al,%%dx"::"a" (value), "d" ((u_int) port));
}

void
sanei_insb (unsigned int port, void *addr, unsigned long count)
{
  __asm__ __volatile__ ("rep ; insb":"=D" (addr), "=c" (count):"d" (port),
			"0" (addr), "1" (count));
}

void
sanei_insl (unsigned int port, void *addr, unsigned long count)
{
  __asm__ __volatile__ ("rep ; insl":"=D" (addr), "=c" (count):"d" (port),
			"0" (addr), "1" (count));
}

void
sanei_outsb (unsigned int port, const void *addr, unsigned long count)
{
  __asm__ __volatile__ ("rep ; outsb":"=S" (addr), "=c" (count):"d" (port),
			"0" (addr), "1" (count));
}

void
sanei_outsl (unsigned int port, const void *addr, unsigned long count)
{
  __asm__ __volatile__ ("rep ; outsl":"=S" (addr), "=c" (count):"d" (port),
			"0" (addr), "1" (count));
}

#endif /* ICC on i386 */

#endif /* ENABLE_PARPORT_DIRECTIO */
/*
 * no inb/outb without --enable-parport-directio *
 */
#ifndef ENABLE_PARPORT_DIRECTIO
int
sanei_ioperm (__sane_unused__ int start, __sane_unused__ int length,
              __sane_unused__ int enable)
{
  /* returns failure */
  return -1;
}

unsigned char
sanei_inb (__sane_unused__ unsigned int port)
{
  return 255;
}

void
sanei_outb (__sane_unused__ unsigned int port,
            __sane_unused__ unsigned char value)
{
}

void
sanei_insb (__sane_unused__ unsigned int port,
            __sane_unused__ unsigned char *addr,
            __sane_unused__ unsigned long count)
{
}

void
sanei_insl (__sane_unused__ unsigned int port,
            __sane_unused__ unsigned char *addr,
            __sane_unused__ unsigned long count)
{
}

void
sanei_outsb (__sane_unused__ unsigned int port,
             __sane_unused__ const unsigned char *addr,
	     __sane_unused__ unsigned long count)
{
}

void
sanei_outsl (__sane_unused__ unsigned int port,
             __sane_unused__ const unsigned char *addr,
	     __sane_unused__ unsigned long count)
{
}
#endif /* ENABLE_PARPORT_DIRECTIO is not defined */
