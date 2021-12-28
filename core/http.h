#ifndef HTTP_H
#define HTTP_H

#include "tcp.h"
#include "http-request.h"
#include "http-respose.h"

#define MAX_HTTP_BUF_SIZE       (4<<20)

typedef struct _Http            Http;

struct _Http
{
    char                   *schema;
    char                   *host;
    int                     port;
    char                   *resource;

    Tcp                    *tcp;
    HttpRequest            *request;
    HttpResponse           *resp;

    int                     headerBufLen;
    int                     headerBufCurLen;
    char                   *headerBuf;

    GError                 *error;
};

Http*   http_new        (GUri* uri);
void    http_destroy    (Http* http);
bool    http_request    (Http* http, const char* fileName);


#endif // HTTP_H
