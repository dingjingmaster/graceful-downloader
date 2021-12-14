#ifndef SEARCH_H
#define SEARCH_H

#include "conf.h"

typedef struct _Search      Search;

struct _Search
{
    char            url[MAX_STRING];
    Conf            *conf;
    off_t           speed, size;
    double          speed_start_time;
    pthread_t       speed_thread[1];
};

int  search_makelist  (Search *results, char *url);
int  search_getspeeds (Search *results, int count);
void search_sortlist  (Search *results, int count);

#endif
