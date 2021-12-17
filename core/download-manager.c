#include <stdlib.h>
#include <stdbool.h>
#include <gio/gio.h>

#include "log.h"


bool protocol_register ();
void protocol_unregister ();
GUri* url_Analysis (char* url);


static GHashTable* gIpAndPortHash = NULL;
static GHashTable* gHostAndUserInfo = NULL;


bool protocol_register ()
{
    if (!gIpAndPortHash) {
        gIpAndPortHash = g_hash_table_new (g_str_hash, g_str_equal);
    }

    g_return_val_if_fail (gIpAndPortHash, false);

    if (!gHostAndUserInfo) {
        gHostAndUserInfo = g_hash_table_new (g_str_hash, g_str_equal);
    }

    g_hash_table_insert (gIpAndPortHash, "http", "80");
    g_hash_table_insert (gIpAndPortHash, "https", "443");
    g_hash_table_insert (gIpAndPortHash, "ftp", "21");


    return true;
}

GUri* url_Analysis (char* url)
{
    g_return_val_if_fail (url, NULL);
    logd ("get uri: %s", url);

    g_return_val_if_fail (gIpAndPortHash, NULL);

    char** arr = g_strsplit (url, "://", -1);
    int len = g_strv_length (arr);
    switch (len) {
    case 1: {
        g_autoptr (GError) error = NULL;
        g_autofree char* turi = g_strdup_printf ("http://%s", url);
        g_autoptr (GUri) uri = g_uri_parse (turi, G_URI_FLAGS_NONE, &error);
        if (error) {
            logi ("input url '%s' => '%s' parse error: %s", url, turi, error->message);
            return NULL;
        }

        return g_object_ref (uri);
    }
    break;
    case 2: {
        g_autoptr (GUri) uri = NULL;
        g_autoptr (GError) error = NULL;
        char* schema = arr[0];
        // check schema is supported
        if (g_hash_table_contains (gIpAndPortHash, schema)) {
            logd ("'%s' find schema '%s'", url, schema);
            uri = g_uri_parse (url, G_URI_FLAGS_NONE, &error);
            if (error) {
                logi ("input url '%s' parse error: %s", url, error->message);
            }
        }

        g_return_val_if_fail (uri, NULL);

        return uri;
    }
    break;
    default: {
        logd ("Not found schema for uri '%s'", url);
        g_strfreev (arr);
        return NULL;
    }
    }

    if (arr) g_strfreev (arr);

    return NULL;
}

void protocol_unregister ()
{
    if (gIpAndPortHash)     g_hash_table_unref (gIpAndPortHash);
    if (gHostAndUserInfo)   g_hash_table_unref (gHostAndUserInfo);
}
