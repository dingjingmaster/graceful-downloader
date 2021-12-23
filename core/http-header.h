#ifndef HTTPHEADER_H
#define HTTPHEADER_H

#define HTTP_HEADER_MAX     256

typedef struct _HttpHeaderList          HttpHeaderList;

struct _HttpHeaderList
{
    char        *header[HTTP_HEADER_MAX];
    char        *value[HTTP_HEADER_MAX];
};


HttpHeaderList* http_header_list_new ();

void http_header_list_destroy (HttpHeaderList* ls);

bool http_header_list_set_value (HttpHeaderList* ls, const char* key, const char* value);
char* http_header_list_get_value (HttpHeaderList* ls, const char* key);

#endif // HTTPHEADER_H
