#ifndef COMMON_H
#define COMMON_H

#include "protocol-interface.h"

static void save_state              (DownloadData* d);
static void download_divide         (DownloadData* d);
static void reactivate_connection   (DownloadData* d, int thread);


#endif // COMMON_H
