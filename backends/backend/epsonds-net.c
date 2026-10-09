/*
 * epsonds-net.c - SANE library for Epson scanners.
 *
 * Copyright (C) 2006-2016 Tower Technologies
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

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include "sane/sane.h"
#include "sane/saneopts.h"
#include "sane/sanei_tcp.h"
#include "sane/sanei_config.h"
#include "sane/sanei_backend.h"

#include "epsonds.h"
#include "epsonds-net.h"
#include "epsonds-tcp.h"

#include "byteorder.h"

#include "sane/sanei_debug.h"


static ssize_t
epsonds_net_read_raw(epsonds_scanner *s, unsigned char *buf, size_t wanted,
		       SANE_Status *status)
{
	DBG(15, "%s: wanted: %zu\n", __func__, wanted);

	if (wanted == 0)
	{
	    *status = SANE_STATUS_GOOD;
		return 0;
	}

	ssize_t read = -1;
	read = epsonds_tcp_read(s, buf, wanted);

	if ((read < 0) || ((size_t)read < wanted)) {
		*status = SANE_STATUS_IO_ERROR;
	}

	return read;
}

static ssize_t
epsonds_net_read_buf(epsonds_scanner *s, unsigned char *buf, size_t wanted,
		       SANE_Status * status)
{
	ssize_t read = 0;

	DBG(23, "%s: reading up to %lu from buffer at %p, %lu available\n",
		__func__, (u_long) wanted, (void *) s->netptr, (u_long) s->netlen);

	if ((size_t) wanted > s->netlen) {
		*status = SANE_STATUS_IO_ERROR;
		wanted = s->netlen;
	}

	memcpy(buf, s->netptr, wanted);
	read = wanted;

	s->netptr += read;
	s->netlen -= read;

	if (s->netlen == 0) {
		DBG(23, "%s: freeing %p\n", __func__, (void *) s->netbuf);
		free(s->netbuf);
		s->netbuf = s->netptr = NULL;
		s->netlen = 0;
	}

	return read;
}

ssize_t
epsonds_net_read(epsonds_scanner *s, unsigned char *buf, size_t wanted,
		       SANE_Status * status)
{
	size_t size;
	ssize_t read = 0;
	unsigned char header[12];

	/* read from remainder of buffer */
	if (s->netptr) {
		return epsonds_net_read_buf(s, buf, wanted, status);
	}

	/* receive net header */
	read = epsonds_net_read_raw(s, header, 12, status);
	if (read != 12) {
		return 0;
	}

	/* validate header */
	if (header[0] != 'I' || header[1] != 'S') {
		DBG(1, "header mismatch: %02X %02x\n", header[0], header[1]);
		*status = SANE_STATUS_IO_ERROR;
		return 0;
	}

	/* parse payload size */
	size = be32atoh(&header[6]);

	*status = SANE_STATUS_GOOD;

	if (!s->netbuf) {
		DBG(15, "%s: direct read\n", __func__);
		DBG(23, "%s: wanted = %lu, available = %lu\n", __func__,
			(u_long) wanted, (u_long) size);

		if ((size_t) wanted > size) {
			wanted = size;
		}

		read = epsonds_net_read_raw(s, buf, wanted, status);
	} else {
		DBG(15, "%s: buffered read\n", __func__);
		DBG(23, "%s: bufferable = %lu, available = %lu\n", __func__,
			(u_long) s->netlen, (u_long) size);

		if (s->netlen > size) {
			s->netlen = size;
		}

		/* fill buffer */
		read = epsonds_net_read_raw(s, s->netbuf, s->netlen, status);
		s->netptr = s->netbuf;
		s->netlen = (read > 0 ? read : 0);

		/* copy wanted part */
		read = epsonds_net_read_buf(s, buf, wanted, status);
	}

	return read;
}

SANE_Status
epsonds_net_request_read(epsonds_scanner *s, size_t len)
{
	SANE_Status status;
	epsonds_net_write(s, 0x2000, NULL, 0, len, &status);
	return status;
}

size_t
epsonds_net_write(epsonds_scanner *s, unsigned int cmd, const unsigned char *buf,
			size_t buf_size, size_t reply_len, SANE_Status *status)
{
	unsigned char *h1, *h2;
	unsigned char *packet = malloc(12 + 8);

	if (!packet) {
		*status = SANE_STATUS_NO_MEM;
		return 0;
	}

	h1 = packet;		// packet header
	h2 = packet + 12;	// data header

	if (reply_len) {
		if (s->netbuf) {
			DBG(23, "%s, freeing %p, %ld bytes unprocessed\n",
				__func__, (void *) s->netbuf, (u_long) s->netlen);
			free(s->netbuf);
			s->netbuf = s->netptr = NULL;
			s->netlen = 0;
		}
		s->netbuf = malloc(reply_len);
		if (!s->netbuf) {
			free(packet);
			*status = SANE_STATUS_NO_MEM;
			return 0;
		}
		s->netlen = reply_len;
		DBG(24, "%s: allocated %lu bytes at %p\n", __func__,
			(u_long) s->netlen, (void *) s->netbuf);
	}

	DBG(24, "%s: cmd = %04x, buf = %p, buf_size = %lu, reply_len = %lu\n",
		__func__, cmd, (void *) buf, (u_long) buf_size, (u_long) reply_len);

	memset(h1, 0x00, 12);
	memset(h2, 0x00, 8);

	h1[0] = 'I';
	h1[1] = 'S';

	h1[2] = cmd >> 8;	// packet type
	h1[3] = cmd;		// data type

	h1[4] = 0x00;
	h1[5] = 0x0C; // data offset

	DBG(24, "H1[0]: %02x %02x %02x %02x\n", h1[0], h1[1], h1[2], h1[3]);

	// 0x20 passthru
	// 0x21 job control

	if (buf_size) {
		htobe32a(&h1[6], buf_size);
	}

	if((cmd >> 8) == 0x20) {

		htobe32a(&h1[6], buf_size + 8);	// data size (data header + payload)

		htobe32a(&h2[0], buf_size);	// payload size
		htobe32a(&h2[4], reply_len);	// expected answer size

		DBG(24, "H1[6]: %02x %02x %02x %02x (%lu)\n", h1[6], h1[7], h1[8], h1[9], (u_long) (buf_size + 8));
		DBG(24, "H2[0]: %02x %02x %02x %02x (%lu)\n", h2[0], h2[1], h2[2], h2[3], (u_long) buf_size);
		DBG(24, "H2[4]: %02x %02x %02x %02x (%lu)\n", h2[4], h2[5], h2[6], h2[7], (u_long) reply_len);
	}

	if ((cmd >> 8) == 0x20 && (buf_size || reply_len)) {

		// send header + data header
		epsonds_tcp_write(s, packet, 12 + 8);

	} else {
		epsonds_tcp_write(s, packet, 12);
	}

	// send payload
	if (buf_size)
		epsonds_tcp_write(s, buf, buf_size);

	free(packet);

	*status = SANE_STATUS_GOOD;
	return buf_size;
}

SANE_Status
epsonds_net_lock(struct epsonds_scanner *s)
{
	SANE_Status status;
	unsigned char buf[7] = "\x01\xa0\x04\x00\x00\x01\x2c";

	DBG(1, "%s\n", __func__);

	epsonds_net_write(s, 0x2100, buf, 7, 0, &status);
	epsonds_net_read(s, buf, 1, &status);

	// buf[0] should be ACK, 0x06

	return status;
}

SANE_Status
epsonds_net_unlock(struct epsonds_scanner *s)
{
	SANE_Status status;

	DBG(1, "%s\n", __func__);

	epsonds_net_write(s, 0x2101, NULL, 0, 0, &status);
/*	epsonds_net_read(s, buf, 1, &status); */
	return status;
}
#if WITH_AVAHI

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <sys/time.h>
#include <errno.h>

static AvahiSimplePoll *simple_poll = NULL;

static struct timeval borowseEndTime;

static int resolvedCount = 0;
static int browsedCount = 0;
static int waitResolver = 0;

typedef struct {
    AvahiClient* client;
    Device_Found_CallBack callBack;
}EDSAvahiUserData;

static int my_avahi_simple_poll_loop(AvahiSimplePoll *s) {
    struct timeval currentTime;

    for (;;)
    {
         int r = avahi_simple_poll_iterate(s, 1);
		if (r != 0)
		{
			if (r >= 0 || errno != EINTR)
			{
					DBG(10, "my_avahi_simple_poll_loop end\n");
					return r;
			}
		}

		if (waitResolver) {
			gettimeofday(&currentTime, NULL);

			if ((currentTime.tv_sec - borowseEndTime.tv_sec) >= 3)
			{
				avahi_simple_poll_quit(simple_poll);
				DBG(10, "resolve timeout\n");
				return 0;
			}
		}
    }
}

static void
epsonds_resolve_callback(AvahiServiceResolver *r, AVAHI_GCC_UNUSED AvahiIfIndex interface,
                            AVAHI_GCC_UNUSED AvahiProtocol protocol,
                            AvahiResolverEvent event, const char *name,
                            const char  *type,
                            const char  *domain,
                            const char  *host_name,
                            const AvahiAddress *address, uint16_t port, AvahiStringList *txt,
                            AvahiLookupResultFlags  flags,
                            void  *userdata)
{
	// unused parameter
	(void)r;
	(void)type;
	(void)domain;
	(void)host_name;
	(void)port;
	(void)flags;
    EDSAvahiUserData* data = userdata;
    char ipAddr[AVAHI_ADDRESS_STR_MAX];

	DBG(10, "epsonds_searchDevices resolve_callback\n");


    resolvedCount++;

    switch (event) {
    case AVAHI_RESOLVER_FAILURE:
        break;
    case AVAHI_RESOLVER_FOUND:
        avahi_address_snprint(ipAddr, sizeof(ipAddr), address);
	   DBG(10, "epsonds_searchDevices name = %s \n", name);
        if (strlen(name) > 7)
        {
            if (strncmp(name, "EPSON", 5) == 0)
            {
				while(txt != NULL)
				{
					char* text = (char*)avahi_string_list_get_text(txt);
					DBG(10, "avahi string = %s\n", text);

					if (strlen(text) > 4 && strncmp(text, "mdl=", 4) == 0)
					{
						if (data->callBack)
                		{
							data->callBack(&text[4], ipAddr);
							break;
                		}
					}
					txt = avahi_string_list_get_next(txt);
				}

            }
        }
		break;
    }
}

static void
browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface,
                            AvahiProtocol protocol, AvahiBrowserEvent event,
                            const char *name, const char *type,
                            const char *domain,
                            AvahiLookupResultFlags flags,
                            void* userdata)
{
    DBG(10, "browse_callback event = %d\n", event);

	//unused parameter
	(void)b;
	(void)flags;

    EDSAvahiUserData *data = userdata;
    switch (event) {
    case AVAHI_BROWSER_FAILURE:
        avahi_simple_poll_quit(simple_poll);
        return;
    case AVAHI_BROWSER_NEW:
	    DBG(10, "browse_callback name = %s\n", name);
		browsedCount++;
        if (!(avahi_service_resolver_new(data->client, interface, protocol, name,
                                                               type, domain,
                                                               AVAHI_PROTO_UNSPEC, 0,
                                                               epsonds_resolve_callback, data)))
		{
			DBG(10, "avahi_service_resolver_new fails\n");
		    break;
		}
    case AVAHI_BROWSER_REMOVE:
        break;
    case AVAHI_BROWSER_ALL_FOR_NOW:
		DBG(10, "AVAHI_BROWSER_ALL_FOR_NOW\n");
        gettimeofday(&borowseEndTime, NULL);

        if (browsedCount > resolvedCount)
        {
			  DBG(10, "WAIT RESOLVER\n");
               waitResolver = 1;
         }else{
			 DBG(10, "QUIT POLL\n");
             avahi_simple_poll_quit(simple_poll);
         }
		break;
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
		 DBG(10, "AVAHI_BROWSER_CACHE_EXHAUSTED\n");
        break;
    }
}

static void
client_callback(AvahiClient *c, AvahiClientState state,
                         AVAHI_GCC_UNUSED void *userdata)
{
    assert(c);
    if (state == AVAHI_CLIENT_FAILURE)
        avahi_simple_poll_quit(simple_poll);
}

SANE_Status epsonds_searchDevices(Device_Found_CallBack deviceFoundCallBack)
{
	int result = SANE_STATUS_GOOD;

    AvahiClient *client = NULL;
    AvahiServiceBrowser *sb = NULL;

    EDSAvahiUserData data;

    resolvedCount = 0;
	browsedCount = 0;
	waitResolver = 0;


	int error = 0;
    DBG(10, "epsonds_searchDevices\n");

    if (!(simple_poll = avahi_simple_poll_new())) {
        DBG(10, "avahi_simple_poll_new failed\n");
		result = SANE_STATUS_INVAL;
        goto fail;
    }
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0,
                                               client_callback, NULL, &error);
    if (!client) {
        DBG(10, "avahi_client_new failed %s\n", avahi_strerror(error));
		result = SANE_STATUS_INVAL;
        goto fail;
    }
    data.client = client;
    data.callBack = deviceFoundCallBack;

    if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC,
                                                                   AVAHI_PROTO_UNSPEC, "_scanner._tcp",
                                                                   NULL, 0, browse_callback, &data))) {
        DBG(10, "avahi_service_browser_new failed: %s\n",
                              avahi_strerror(avahi_client_errno(client)));
		result = SANE_STATUS_INVAL;
        goto fail;
    }
    my_avahi_simple_poll_loop(simple_poll);
fail:
    if (sb)
        avahi_service_browser_free(sb);
    if (client)
        avahi_client_free(client);
    if (simple_poll)
        avahi_simple_poll_free(simple_poll);

    DBG(10, "epsonds_searchDevices fin\n");

    return result;
}
#endif
