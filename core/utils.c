#include "utils.h"

#include <errno.h>
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
