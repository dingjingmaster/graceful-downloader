#include "http-respose.h"


HttpResponse *http_respose_new()
{
    HttpResponse* resp = g_malloc0 (sizeof (HttpResponse));
    if (!resp) {
        return NULL;
    }

    return resp;
}

void http_respose_destroy(HttpResponse *resp)
{
    g_return_if_fail (resp);

    if (resp->reason)       g_free (resp->reason);
    if (resp->headers)      http_header_list_destroy (resp->headers);
    if (resp->body)         g_free (resp->body);

    g_free (resp);
}
