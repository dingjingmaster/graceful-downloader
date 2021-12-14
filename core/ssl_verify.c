
#include <stdbool.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "config.h"

inline static int match(const char *hostname, ASN1_STRING *certname)
{
    return dn_match (hostname, (const char *)ASN1_STRING_get0_data(certname), ASN1_STRING_length(certname));
}

/**
 * Match hostname against Certificate's CN field.
 *
 * @returns Negative on malformed input, Zero if matched, non-zero otherwise.
 */
static int matches_cn(const char *hostname, const X509 *cert)
{
    /* Find CN field in the Subject field */
    int loc = X509_NAME_get_index_by_NID(X509_get_subject_name(cert), NID_commonName, -1);
    if (loc < 0)
        return 1;

    X509_NAME_ENTRY *entry;
    entry = X509_NAME_get_entry(X509_get_subject_name(cert), loc);
    if (!entry)
        return 1;

    ASN1_STRING *cn = X509_NAME_ENTRY_get_data(entry);
    if (!cn)
        return 1;

    return match(hostname, cn);
}

/**
 * Match hostname against Certificate's SAN fields if available,
 * or CN field otherwise.
 *
 * @returns Negative on malformed input, Zero if matched, non-zero otherwise.
 */
static int matches_cert(const char *hostname, const X509 *cert)
{
    /* Try to extract the names within the SAN extension */
    STACK_OF(GENERAL_NAME) *names;
    names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

    /* Iff extension was not found try the Common Name */
    if (!names)
        return matches_cn(hostname, cert);

    /* Check each name until a match or error */
    const int names_len = sk_GENERAL_NAME_num(names);
    int result = 1;
    for (int i = 0; i < names_len && result > 0; i++) {
        const GENERAL_NAME *cur = sk_GENERAL_NAME_value(names, i);
        /* Check DNS names only */
        if (cur->type == GEN_DNS)
            result = match(hostname, cur->d.dNSName);
    }
    sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
    return result;
}

bool ssl_validate_hostname(const char *hostname, const X509 *cert)
{
    return hostname && cert && matches_cert(hostname, cert) == 0;
}
