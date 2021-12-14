#include "config.h"
#include <string.h>

#include "global.h"

#define DN_NEQ 1

/**
 * Hostname matching according to RFC-6125 section 6.4.3.
 *
 * For simplicity, at most one wildcard is supported, on the leftmost label
 * only.
 *
 * Hostname must be normalized and ASCII-only.
 *
 * @returns Negative on malformed input, Zero if matched, non-zero otherwise.
 */
int dn_match(const char *hostname, const char *pat, size_t pat_len)
{
    /* The pattern is partitioned at the first wildcard or dot */
    const size_t left = strcspn(pat, ".*");

    /* We can't match an IDN against a wildcard */
    const char ace_prefix[4] = "xn--";
    if (pat[left] == '*' && !strncasecmp(hostname, ace_prefix, 4))
        return DN_NEQ;

    /* Compare left-side partition */
    if (strncasecmp(hostname, pat, left))
        return DN_NEQ;

    hostname += left;
    pat += left;
    pat_len -= left;

    /* Wildcard? */
    size_t right = 0;
    if (*pat == '*') {
        --pat_len;
        right = strcspn(++pat, ".");
        const size_t rem = strcspn(hostname, ".");
        /* Shorter label in hostname? */
        if (right > rem)
            return DN_NEQ;
        /* Skip the longest match and adjust pat_len */
        hostname += rem - right;
    }

    /* Check premature end of pattern (malformed certificate) */
    if (pat_len == right - strlen(pat + right))
        return DN_MATCH_MALFORMED;

    /* Compare right-side partition */
    return !! strcasecmp(hostname, pat);
}
