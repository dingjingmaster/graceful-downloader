#ifndef _APP_H
#define _APP_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "tcp.h"
#include "ftp.h"
#include "ssl.h"
#include "http.h"
#include "conn.h"
#include "abuf.h"
#include "conf.h"
#include "search.h"


App*   app_new          (Conf* conf, int count, const Search* urls);
int    app_open         (App* app);
void   app_start        (App* app);
void   app_do           (App* app);
void   app_close        (App* app);

double app_gettime      (void);
void   print_messages   (App* app);
char*  app_size_human   (char *dst, size_t len, size_t value);


#define DN_MATCH_MALFORMED -1
int   dn_match (const char* hostname, const char* pat, size_t patLen);


#ifdef __cplusplus
}
#endif
#endif // APP_H
