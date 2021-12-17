#include "utils.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <gio/gio.h>
#include <sys/time.h>

double gf_gettime (void)
{
    struct timeval time[1];

    gettimeofday(time, NULL);
    return (double)time->tv_sec + (double)time->tv_usec / 1000000;
}

int gf_sleep (struct timespec delay)
{
    int res;
    while ((res = nanosleep(&delay, &delay)) && errno == EINTR) ;
    return res;
}

size_t gf_strlcpy(char* dst, const char* src, size_t dsize)
{
    const char *osrc = src;
    size_t nleft = dsize;

    /* Copy as many bytes as will fit. */
    if (nleft != 0) {
        while ((--nleft != 0) && ((*dst++ = *src++) != '\0'));
    }

    /* Not enough room in dst, add NUL and traverse rest of src. */
    if (nleft == 0) {
        if (dsize != 0) {
            *dst = '\0';
        }

        while (*src++);
    }

    return (src - osrc - 1);
}


size_t gf_strlcat(char *dst, const char *src, size_t dsize)
{
    const char *odst = dst;
    const char *osrc = src;
    size_t n = dsize;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end. */
    while (n-- != 0 && *dst != '\0') {
        dst++;
    }

    dlen = dst - odst;
    n = dsize - dlen;

    if (n-- == 0) {
        return(dlen + strlen (src));
    }

    while (*src != '\0') {
        if (n != 0) {
            *dst++ = *src;
            n--;
        }
        src++;
    }
    *dst = '\0';

    return (dlen + (src - osrc)); /* count does not include NUL */
}

int gf_get_process_num_by_name (const char *progressName)
{
    int num = 0;
    char str[10] = {0};

    g_return_val_if_fail (progressName, -1);

    g_autofree char* buf = NULL;
    g_autofree char* cmd = g_strdup_printf ("pidof %s", progressName);

    FILE* fp = popen (cmd, "r");
    if (NULL != fp) {
        for (int ret = 1; ret > 0;) {
            memset (str, 0, sizeof (str));
            ret = fread (str, sizeof (str), sizeof (char), fp);
            if (!buf) {
                buf = g_strdup (str);
            } else {
                g_autofree char* tmp = buf;
                buf = g_strdup_printf ("%s%s", tmp, str);
            }
        }
        pclose (fp);

        g_return_val_if_fail (buf, -1);

        char** arr = g_strsplit (buf, " ", -1);

        num = g_strv_length (arr);

        if (arr) g_strfreev (arr);
    }

    return num;
}
