#define DEBUG_DECLARE_ONLY

#include "epsonds.h"
#include "epsonds-net.h"
#include "epsonds-tcp.h"
#include "epsonds-io.h"
#include "sane/sanei_tcp.h"
#include "sane/config.h"
#include <ctype.h>
#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif
#include <limits.h>

#include "sane/sanei_debug.h"



#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

typedef struct epsonds_scanner epsonds_scanner;

SANE_Status epsonds_tcp_open(epsonds_scanner* s, const char *host, int port) {

    SANE_Status status = SANE_STATUS_GOOD;

    if(s == NULL || host == NULL) {
        return SANE_STATUS_INVAL;
    }

    status = sanei_tcp_open(&s->hw->name[4], port, &s->fd);
    if (status == SANE_STATUS_GOOD) {

        ssize_t read;
        struct timeval tv;
        unsigned char buf[5];

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof(tv));

        s->netlen = 0;
		DBG(5, "host = %s\n", &s->hw->name[4]);

        DBG(32, "awaiting welcome message\n");
        /* the scanner sends a kind of welcome msg */
        // XXX check command type, answer to connect is 0x80
        read = eds_recv(s, buf, 5, &status);
        if (read != 5) {
            sanei_tcp_close(s->fd);
            s->fd = -1;
            status = SANE_STATUS_INVAL;
        }
        else {
            return SANE_STATUS_GOOD;
        }
#ifdef HAVE_OPENSSL
        SSL_CTX *ssl = NULL;
        BIO *bio = NULL;
        crypt_context *crypt_ctx = NULL;

        crypt_ctx = (crypt_context*)malloc(sizeof(crypt_context));
        if(crypt_ctx  == NULL) {
            return SANE_STATUS_INVAL;
        }

        ssl = SSL_CTX_new( TLS_client_method());
        if (ssl == NULL) {
            free(crypt_ctx);
            return SANE_STATUS_INVAL;
        }


        bio = BIO_new_ssl_connect(ssl);
        if (bio == NULL) {
            SSL_CTX_free(ssl);
            free(crypt_ctx);
            return SANE_STATUS_INVAL;
        }

        char port_s[5];
        snprintf(port_s, sizeof(port_s), "%d", port);

        DBG(1, "port_s=%s", port_s);
        BIO_set_conn_hostname(bio, host);
        BIO_set_conn_port(bio, port_s);

        BIO_set_ssl_renegotiate_timeout(bio, 2);

        if (BIO_do_connect(bio) <= 0) {
            BIO_free_all(bio);
            SSL_CTX_free(ssl);
            free(crypt_ctx);
            return SANE_STATUS_INVAL;
        }

        crypt_ctx->bio = bio;
        crypt_ctx->ssl = ssl;

        s -> cryptContext = crypt_ctx;

        read = eds_recv(s, buf, 5, &status);
        if (read != 5) {
            epsonds_tcp_close(s);
            s->fd = -1;
            return SANE_STATUS_INVAL;
        }
#endif
    }
    return status;
}

void epsonds_tcp_close(epsonds_scanner* s) {
#ifdef HAVE_OPENSSL
    if(s->cryptContext == NULL) {
        sanei_tcp_close(s->fd);
    }
    else {
        BIO_free_all(s->cryptContext->bio);
        s->cryptContext->bio = NULL;
        SSL_CTX_free(s->cryptContext->ssl);
        s->cryptContext->ssl= NULL;
        free(s->cryptContext);
        s->cryptContext = NULL;
    }
#else
    sanei_tcp_close(s->fd);
#endif
    s->fd = -1;
}

ssize_t epsonds_tcp_read(epsonds_scanner* s, unsigned char *buf, size_t wanted) {
    ssize_t read = -1;

#ifdef HAVE_OPENSSL
    if(s->cryptContext == NULL) {
        int ready;
        fd_set readable;
        struct timeval tv;

        tv.tv_sec = 10;
        tv.tv_usec = 0;

        FD_ZERO(&readable);
        FD_SET(s->fd, &readable);

        ready = select(s->fd + 1, &readable, NULL, NULL, &tv);
        if (ready > 0) {
            read = sanei_tcp_read(s->fd, buf, wanted);
        } else {
            DBG(15, "%s: select failed: %d\n", __func__, ready);
        }
    }
    else {
        size_t bytes_recv = 0;
        int rc = 1;

        while (bytes_recv < wanted && rc > 0)
        {
            rc = BIO_read(s->cryptContext->bio, buf+bytes_recv, wanted-bytes_recv);
            if (rc > 0)
                bytes_recv += rc;
            DBG(1, "wanted=%zu, bytes_recv:%zu, rc=%d\n", wanted, bytes_recv, rc);
        }

        read = bytes_recv;
    }
#else
        int ready;
        fd_set readable;
        struct timeval tv;

        tv.tv_sec = 10;
        tv.tv_usec = 0;

        FD_ZERO(&readable);
        FD_SET(s->fd, &readable);

        ready = select(s->fd + 1, &readable, NULL, NULL, &tv);
        if (ready > 0) {
            read = sanei_tcp_read(s->fd, buf, wanted);
        } else {
            DBG(15, "%s: select failed: %d\n", __func__, ready);
        }
#endif

    return read;
}

ssize_t epsonds_tcp_write(epsonds_scanner* s, const unsigned char *buf, size_t count) {
#ifdef HAVE_OPENSSL
    if(s->cryptContext == NULL) {
        return sanei_tcp_write(s->fd, buf, count);
    }
    else {
        if(INT_MAX < count) {
            return -1;
        }
        return BIO_write(s->cryptContext->bio, buf, count);
    }
#else
    return sanei_tcp_write(s->fd, buf, count);
#endif
}
