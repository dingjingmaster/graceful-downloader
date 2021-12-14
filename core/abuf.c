#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "abuf.h"

extern size_t strlcat (char *dst, const char *src, size_t dsize);

/**
 * Abstract buffer allocation/free.
 * @returns 0 if OK, a negative value on error.
 */
int abuf_setup (Abuf *abuf, size_t len)
{
    char *p = realloc(abuf->p, len);
    if (!p && len) {
        return -ENOMEM;
    }

    abuf->p = p;
    abuf->len = len;

    return 0;
}

int abuf_printf(Abuf *abuf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (;;) {
        size_t len = vsnprintf(abuf->p, abuf->len, fmt, ap);
        if (len < abuf->len) {
            break;
        }

        int r = abuf_setup(abuf, len + 1);
        if (r < 0) {
            return r;
        }
    }

    va_end(ap);

    return 0;
}

/**
 * String concatenation.  The buffer must contain a valid C string.
 * @returns 0 if OK, or negative value on error.
 */
int abuf_strcat(Abuf* abuf, const char *src)
{
    size_t nread = strlcat(abuf->p, src, abuf->len);
    if (nread > abuf->len) {
        size_t done = abuf->len - 1;
        int ret = abuf_setup(abuf, nread);
        if (ret < 0) {
            return ret;
        }

        memcpy(abuf->p + done, src + done, nread - done);
    }

    return 0;
}
