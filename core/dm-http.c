#include "dm-http.h"

bool dm_http_init(DownloadData *d)
{
    g_return_val_if_fail (d && d->uri, false);

    Http* http = http_new (d->uri);
    g_return_val_if_fail (http, false);

    d->data = http;

    return true;
}

bool dm_http_download(DownloadData *d)
{
    g_return_val_if_fail (d && d->data, false);

    return http_request ((Http*)d->data, d->outputName);
}

void dm_http_free(DownloadData *d)
{
    g_return_if_fail (d);

    if (d->data)        http_destroy (d->data);
    if (d->outputName)  g_free (d->outputName);
    if (d->uri)         g_uri_unref (d->uri);
}
