#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>


#define DEFAULT_USER_AGENT      "graceful-downloader/" VERSION " (" ARCH ")"

#define min(a, b)               \
({                              \
    __typeof__(a) __a = (a);	\
    __typeof__(b) __b = (b);	\
    __a < __b ? __a : __b;	\
})

#define max(a, b)               \
({                              \
    __typeof__(a) __a = (a);	\
    __typeof__(b) __b = (b);	\
    __a > __b ? __a : __b;	\
})



#endif // GLOBAL_H
