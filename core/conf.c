#include "conf.h"

#include <error.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <gio/gio.h>

#include "log.h"
#include "config.h"
#include "utils.h"

#define MATCH if (0);
#define KEY(name) \
else if (!strcmp(key, #name)) \
    dst = &conf->name;

int parse_interfaces(Conf *conf, char *s);

extern int      get_if_ip   (char *dst, size_t len, const char *iface);

static int gf_fscanf(FILE *fp, const char *format, ...)
{
    va_list params;
    int ret;

    va_start(params, format);
    ret = vfscanf(fp, format, params);
    va_end(params);

    ret = !(ret == EOF && ferror(fp));
    if (!ret) {
        loge ("I/O error while reading config file: %s", strerror(errno));
    }

    return ret;
}

static int parse_protocol(Conf *conf, const char *value)
{
    if (strcasecmp(value, "ipv4") == 0) {
        conf->ai_family = AF_INET;
    } else if (strcasecmp(value, "ipv6") == 0) {
        conf->ai_family = AF_INET6;
    } else {
        loge("Unknown protocol %s", value);
        return 0;
    }

    return 1;
}


int conf_init(Conf *conf)
{
    g_return_val_if_fail (conf, -1);

    /* Set defaults */
    memset(conf, 0, sizeof(Conf));

    gf_strlcpy(conf->default_filename, "default", sizeof(conf->default_filename));
    *conf->http_proxy = 0;
    *conf->no_proxy = 0;
    conf->strip_cgi_parameters = 1;
    conf->save_state_interval = 10;
    conf->connection_timeout = 45;
    conf->reconnect_delay = 20;
    conf->num_connections = 4;
    conf->max_redirect = MAX_REDIRECT;
    conf->io_timeout = DEFAULT_IO_TIMEOUT;
    conf->buffer_size = 5120;
    conf->max_speed = 0;
    conf->verbose = 1;
    conf->insecure = 0;
    conf->no_clobber = 0;

    conf->search_timeout = 10;
    conf->search_threads = 3;
    conf->search_amount = 15;
    conf->search_top = 3;

    conf->ai_family = AF_UNSPEC;

    conf_hdr_make(conf->add_header[HDR_USER_AGENT], "User-Agent", DEFAULT_USER_AGENT);
    conf->add_header_count = HDR_count_init;

    conf->interfaces = calloc(1, sizeof(If));
    if (!conf->interfaces) {
        return 0;
    }

    conf->interfaces->next = conf->interfaces;

    /* Detect if stdout is a tty, set the default indicator to alternate.
       Otherwise, keep it to original.*/
    conf->alternate_output = isatty(STDOUT_FILENO);

    char *s2 = NULL;
    if ((s2 = getenv("http_proxy")) || (s2 = getenv("HTTP_PROXY"))) {
        gf_strlcpy(conf->http_proxy, s2, sizeof(conf->http_proxy));
    }

    /* Convert no_proxy to a 0-separated-and-00-terminated list.. */
    int i = 0;
    for (; conf->no_proxy[i] && i < sizeof (conf->no_proxy); ++i) {
        if (conf->no_proxy[i] == ',') {
            conf->no_proxy[i] = 0;
        }
    }
    conf->no_proxy[i + 1] = 0;

    return 1;
}

/* release resources allocated by conf_init() */
void conf_free(Conf *conf)
{
    free(conf->interfaces);
}

int parse_interfaces(Conf *conf, char *s)
{
    char *s2;
    If *iface;

    iface = conf->interfaces->next;
    while (iface != conf->interfaces) {
        If *i;

        i = iface->next;
        free(iface);
        iface = i;
    }
    free(conf->interfaces);

    if (!*s) {
        conf->interfaces = calloc(1, sizeof(If));
        if (!conf->interfaces)
            return 0;

        conf->interfaces->next = conf->interfaces;
        return 1;
    }

    s[strlen(s) + 1] = 0;
    conf->interfaces = iface = malloc(sizeof(If));
    if (!conf->interfaces) {
        return 0;
    }

    while (1) {
        while ((*s == ' ' || *s == ' ') && *s)
            s++;
        for (s2 = s; *s2 != ' ' && *s2 != ' ' && *s2; s2++) ;
        *s2 = 0;
        if (*s < '0' || *s > '9') {
            get_if_ip (iface->text, sizeof(iface->text), s);
        } else {
            gf_strlcpy (iface->text, s, sizeof(iface->text));
        }
        s = s2 + 1;
        if (*s) {
            iface->next = malloc(sizeof(If));
            if (!iface->next) {
                return 0;
            }

            iface = iface->next;
        } else {
            iface->next = conf->interfaces;
            break;
        }
    }

    return 1;
}
