#include "http.h"

Http *http_new(GUri* uri)
{
    g_return_val_if_fail (uri, NULL);

    Http* http = g_malloc0 (sizeof (Http));
    if (!http) {
        goto error;
    }

    // FIXME://
    http->port = 80;

    if (!(http->tcp = tcp_new ()))              goto error;
    if (!(http->resp = http_respose_new ()))    goto error;


    return http;

error:
    if (http && http->tcp)          tcp_destroy (&http->tcp);
    if (http && http->resp)         http_respose_destroy (http->resp);
    if (http && http->request)      http_request_destroy (http->request);
    if (http)                       g_free (http);

    return NULL;
}

void http_destroy(Http *http)
{
    g_return_if_fail (http);
}
