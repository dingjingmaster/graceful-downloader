#ifndef CONF_H
#define CONF_H

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "global.h"

struct _Conf
{
    char default_filename[MAX_STRING];
    char http_proxy[MAX_STRING];
    char no_proxy[MAX_STRING];
    uint16_t num_connections;
    int strip_cgi_parameters;
    int save_state_interval;
    int connection_timeout;
    int reconnect_delay;
    int max_redirect;
    int buffer_size;
    unsigned long long max_speed;
    int verbose;
    int alternate_output;
    int insecure;
    int no_clobber;
    int percentage;

    If* interfaces;

    sa_family_t ai_family;

    int search_timeout;
    int search_threads;
    int search_amount;
    int search_top;

    unsigned io_timeout;

    int add_header_count;
    char add_header[MAX_ADD_HEADERS] [MAX_STRING];
};

int     conf_init       (Conf* conf);
void    conf_free       (Conf* conf);

enum {
    HDR_USER_AGENT,
    HDR_count_init,
};

inline static void conf_hdr_make(char* dst, const char* k, const char* v)
{
    snprintf(dst, sizeof(((Conf *)0)->add_header[0]), "%s: %s", k, v);
}

#endif
