#include "download-manager.h"

#include <stdlib.h>

#include "log.h"


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

GUri* url_Analysis (const char* url)
{
    g_return_val_if_fail (url, NULL);
    logd ("get uri: %s", url);

    g_return_val_if_fail (gIpAndPortHash, NULL);

    char** arr = g_strsplit (url, "://", -1);
    int len = g_strv_length (arr);
    g_autoptr (GUri) uri = NULL;
    g_autoptr (GError) error = NULL;
    switch (len) {
    case 1: {
        g_autofree char* turi = g_strdup_printf ("http://%s", url);
        uri = g_uri_parse (turi, G_URI_FLAGS_NONE, &error);
        if (error) {
            logi ("input url '%s' => '%s' parse error: %s", url, turi, error->message);
            goto out;
        }

        logd ("input url '%s' use default http protocol!", url);
    }
    break;
    case 2: {
        char* schema = arr[0];
        // check schema is supported
        if (g_hash_table_contains (gIpAndPortHash, schema)) {
            logd ("'%s' find schema '%s'", url, schema);
            uri = g_uri_parse (url, G_URI_FLAGS_NONE, &error);
            if (error) {
                logi ("input url '%s' parse error: %s", url, error->message);
                goto out;
            }
            logd ("input url '%s' use protocol '%s'!", url, schema);
        }
    }
    break;
    default: {
        logd ("Not found schema for uri '%s'", url);
    }
    }

out:
    if (arr) g_strfreev (arr);

    return uri ? g_uri_ref (uri) : NULL;
}

void protocol_unregister ()
{
    if (gIpAndPortHash)     g_hash_table_unref (gIpAndPortHash);
    if (gHostAndUserInfo)   g_hash_table_unref (gHostAndUserInfo);
}

void *download(DownloadData *data)
{
    return NULL;
}
