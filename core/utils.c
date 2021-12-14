#include "utils.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
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
