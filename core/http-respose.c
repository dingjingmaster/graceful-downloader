#include "http-respose.h"


HttpResopnse *http_respose_new()
{
    HttpResopnse* resp = g_malloc0 (sizeof (HttpResopnse));
    if (!resp) {
        return NULL;
    }

    return resp;
}

void http_respose_destroy(HttpResopnse *resp)
{
    g_return_if_fail (resp);

    if (resp->reason)       g_free (resp->reason);
    if (resp->headers)      http_header_list_destroy (resp->headers);
    if (resp->body)         g_free (resp->body);

    g_free (resp);
}
