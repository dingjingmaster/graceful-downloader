#include <stdio.h>

#include "http.h"

extern void http_debug (const Http* http);

int main (int argc, char* argv[])
{
    GUri* uri = g_uri_build (G_URI_FLAGS_NONE, "http", NULL, "www.baidu.com", 80, "/index.html", NULL, NULL);

    Http* http = http_new (uri);

    http_debug (http);

    if (!http_request (http, "index.html")) {
        printf ("http_request failed!\n");
        goto error;
    }

    http_debug (http);

error:
    if (http)       http_destroy (http);
    if (uri)        g_uri_unref (uri);

    return 0;
}
