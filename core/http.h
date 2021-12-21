#ifndef _HTTP_H
#define _HTTP_H

#include "tcp.h"
#include "abuf.h"
#include "global.h"
#include "protocol-interface.h"

typedef struct _Http    Http;


/* private */
struct _Http
{
    char            host[MAX_STRING];
    char            auth[MAX_STRING];
    Abuf            request[1], headers[1];
    int             port;
    int             proto;
    int             proxy;
    char            proxy_auth[MAX_STRING];

    off_t           firstbyte;
    off_t           lastbyte;
    int             status;

    Tcp             tcp;
    char            *localIf;
};



bool http_init          (DownloadData* d);
bool http_download      (DownloadData* d);
void http_free          (DownloadData* d);


// some function private
int  http_exec          (Http* http);
void http_get           (Http* http, const char *lurl);





int http_connect (Http* conn, int proto, const char *proxy, const char *host, int port, const char *user, const char *pass, unsigned ioTimeout);
void http_disconnect (Http* conn);

#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif /* __GNUC__ */

void http_addheader (Http* conn, const char *format, ...);
const char* http_header (const Http* conn, const char *header);
void http_filename(const Http* conn, char *filename);
off_t http_size(Http* conn);
off_t http_size_from_range(Http* conn);
void http_encode(char *s, size_t len);
void http_decode(char *s);

#endif
