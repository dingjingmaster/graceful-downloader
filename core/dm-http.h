#ifndef HTTPDM_H
#define HTTPDM_H

#include "protocol-interface.h"

#include "http.h"

bool dm_http_init       (DownloadData* d);
bool dm_http_download   (DownloadData* d);
void dm_http_free       (DownloadData* d);

#endif // HTTPDM_H
