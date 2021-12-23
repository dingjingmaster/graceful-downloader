#ifndef HTTPDM_H
#define HTTPDM_H

#include "protocol-interface.h"

bool http_init          (DownloadData* d);
bool http_download      (DownloadData* d);
void http_free          (DownloadData* d);

#endif // HTTPDM_H
