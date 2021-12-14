#ifndef _HTTP_H
#define _HTTP_H

#include "tcp.h"
#include "abuf.h"
#include "global.h"

typedef struct _Http    Http;

struct _Http
{
    char host[MAX_STRING];
    char auth[MAX_STRING];
    Abuf request[1], headers[1];
    int port;
    int proto;		/* FTP through HTTP proxies */
    int proxy;
    char proxy_auth[MAX_STRING];
    off_t firstbyte;
    off_t lastbyte;
    int status;
    Tcp tcp;
    char *local_if;
};

int http_connect (Http* conn, int proto, char *proxy, char *host, int port, char *user, char *pass, unsigned io_timeout);
void http_disconnect (Http* conn);
void http_get (Http* conn, char *lurl);

#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif /* __GNUC__ */

void http_addheader (Http* conn, const char *format, ...);
int http_exec(Http* conn);
const char* http_header (const Http* conn, const char *header);
void http_filename(const Http* conn, char *filename);
off_t http_size(Http* conn);
off_t http_size_from_range(Http* conn);
void http_encode(char *s, size_t len);
void http_decode(char *s);

#endif
