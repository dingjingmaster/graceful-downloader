#ifndef ABUF_H
#define ABUF_H

#include <stdarg.h>
#include <stdlib.h>

typedef struct _Abuf    Abuf ;

struct _Abuf
{
    char*   p;
    size_t  len;
};

int abuf_setup  (Abuf *abuf, size_t len);
int abuf_strcat (Abuf *abuf, const char *src);
int abuf_printf (Abuf *abuf, const char *fmt, ...);

#define ABUF_FREE 0

#endif
