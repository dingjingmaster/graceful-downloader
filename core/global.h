#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>


#define MAX_REDIRECT            20
#define MAX_ADD_HEADERS         10
#define DEFAULT_IO_TIMEOUT      120

#define DN_MATCH_MALFORMED      -1

#define MAX_STRING              ((size_t) 1024)
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


typedef struct _Message         Message;
typedef Message                 Url;
typedef Message                 If;
typedef struct _App             App;
typedef struct _Conf            Conf;
typedef struct _Conn            Conn;
typedef struct _Ftp             Ftp;

struct _Message
{
    void *next;
    char text[MAX_STRING];
};

struct _App
{
    Conn                *conn;
    Conf                *conf;
    char                filename[MAX_STRING];
    double              start_time;
    int                 next_state, finish_time;
    off_t               bytes_done, start_byte, size;
    long long int       bytes_per_second;
    struct timespec     delay_time;

    int                 outfd;
    int                 ready;

    Message             *message, *last_message;
    Url                 *url;
};



#endif // GLOBAL_H
