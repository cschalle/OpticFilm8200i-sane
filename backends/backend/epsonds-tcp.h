
#ifndef _EPSONDS_TCP_H_
#define _EPSONDS_TCP_H_

#include <sys/types.h>
#include "../include/sane/sane.h"

extern void epsonds_tcp_close(struct epsonds_scanner* s);
extern SANE_Status epsonds_tcp_open(struct epsonds_scanner* s, const char *host, int port);
extern ssize_t epsonds_tcp_read(struct epsonds_scanner* s, unsigned char *buf, size_t wanted);
extern  ssize_t epsonds_tcp_write(struct epsonds_scanner* s, const unsigned char *buf, size_t count);

#endif
