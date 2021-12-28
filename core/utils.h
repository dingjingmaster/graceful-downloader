#ifndef _UTILS_H
#define _UTILS_H

#include <time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <gio/gio.h>


void    gf_error (GError** error, const char* fmt, ...);

double  gf_gettime  (void);
int     gf_sleep    (struct timespec delay);
size_t  gf_strlcpy  (char *dst, const char *src, size_t dsize);
size_t  gf_strlcat  (char *dst, const char *src, size_t dsize);

int     gf_get_process_num_by_name (const char* progressName);


int     stfile_unlink   (const char *bname);
char*   stfile_makename (const char *bname);
int     stfile_access   (const char *bname, int mode);
int     stfile_open     (const char *bname, int flags, mode_t mode);

#endif // UTILS_H
