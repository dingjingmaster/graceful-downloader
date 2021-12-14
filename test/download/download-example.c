#include <stdio.h>

#include "core/ssl.h"
#include "core/app.h"
#include "core/conf.h"

extern size_t strlcpy(char *dst, const char *src, size_t dsize);

int main (int argc, char* argv[])
{
    Conf        conf;
    Search      search;

    if (!conf_init (&conf)) {
        printf ("conf init error!\n");
        return 1;
    }

    ssl_init (&conf);

    strlcpy(search.url, "www.baidu.com", sizeof(search.url));

    App* app = app_new (&conf, 1, &search);

    strlcpy(app->filename, "baidu.html", sizeof(app->filename));

    if (!app_open (app)) {
        print_messages (app);
        return -1;
    }

    print_messages (app);
    app_start (app);
    print_messages (app);

    while (!app->ready) {
        app_do (app);
    }

    app_close (app);
    conf_free (&conf);

    return 0;
}
