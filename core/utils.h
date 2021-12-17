#ifndef _UTILS_H
#define _UTILS_H

#include <time.h>


double  gf_gettime  (void);
int     gf_sleep    (struct timespec delay);
size_t  gf_strlcpy  (char *dst, const char *src, size_t dsize);
size_t  gf_strlcat  (char *dst, const char *src, size_t dsize);

int     gf_get_process_num_by_name (const char* progressName);

#endif // UTILS_H
