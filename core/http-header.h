#ifndef HTTPHEADER_H
#define HTTPHEADER_H
#include <stdlib.h>
#include <stdbool.h>


#define HTTP_HEADER_MAX     256


extern const char gHttpHeaderAllow[];
extern const char gHttpHeaderContentEncoding[];
extern const char gHttpHeaderContentLanguage[];
extern const char gHttpHeaderContentLength[];
extern const char gHttpHeaderContentLocation[];
extern const char gHttpHeaderContentMD5[];
extern const char gHttpHeaderContentRange[];
extern const char gHttpHeaderContentType[];
extern const char gHttpHeaderExpires[];
extern const char gHttpHeaderLastModified[];

/* general headers */
extern const char gHttpHeaderCacheControl[];
extern const char gHttpHeaderConnection[];
extern const char gHttpHeaderDate[];
extern const char gHttpHeaderPragma[];
extern const char gHttpHeaderTransferEncoding[];
extern const char gHttpHeaderUpdate[];
extern const char gHttpHeaderTrailer[];
extern const char gHttpHeaderVia[];

/* request headers */
extern const char gHttpHeaderAccept[];
extern const char gHttpHeaderAcceptCharset[];
extern const char gHttpHeaderAcceptEncoding[];
extern const char gHttpHeaderAcceptLanguage[];
extern const char gHttpHeaderAuthorization[];
extern const char gHttpHeaderExpect[];
extern const char gHttpHeaderFrom[];
extern const char gHttpHeaderHost[];
extern const char gHttpHeaderIfModifiedSince[];
extern const char gHttpHeaderIfMatch[];
extern const char gHttpHeaderIfNoneMatch[];
extern const char gHttpHeaderIfRange[];
extern const char gHttpHeaderIfUnmodifiedSince[];
extern const char gHttpHeaderMaxForwards[];
extern const char gHttpHeaderProxyAuthorization[];
extern const char gHttpHeaderRange[];
extern const char gHttpHeaderReferrer[];
extern const char gHttpHeaderTE[];
extern const char gHttpHeaderUserAgent[];
/* response headers */
extern const char gHttpHeaderAcceptRanges[];
extern const char gHttpHeaderAge[];
extern const char gHttpHeaderETag[];
extern const char gHttpHeaderLocation[];
extern const char gHttpHeaderRetryAfter[];
extern const char gHttpHeaderServer[];
extern const char gHttpHeaderVary[];
extern const char gHttpHeaderWarning[];
extern const char gHttpHeaderWWWAuthenticate[];

/* Other headers */
extern const char gHttpHeaderSetCookie[];

/* WebDAV headers */
extern const char gHttpHeaderDAV[];
extern const char gHttpHeaderDepth[];
extern const char gHttpHeaderDestination[];
extern const char gHttpHeaderIf[];
extern const char gHttpHeaderLockToken[];
extern const char gHttpHeaderOverwrite[];
extern const char gHttpHeaderStatusURI[];
extern const char gHttpHeaderTimeout[];


typedef struct _HttpHeaderList          HttpHeaderList;

struct _HttpHeaderList
{
    char        *header[HTTP_HEADER_MAX];
    char        *value[HTTP_HEADER_MAX];
};


HttpHeaderList* http_header_list_new ();
void  http_header_list_destroy (HttpHeaderList* ls);

const char* http_header_is_known (const char* header);

bool  http_header_list_set_value (HttpHeaderList* ls, const char* key, const char* value);
char* http_header_list_get_value (HttpHeaderList* ls, const char* key);

bool  http_header_list_get_headers (HttpHeaderList* ls, char*** names, int* numNames);
bool  http_header_clear_value    (HttpHeaderList* ls, const char* name);

#endif // HTTPHEADER_H
