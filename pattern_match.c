/* pattern_match.c — Thompson-NFA pattern matcher (rebuilt incrementally; see PRD §7). */

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/*
 * Process escape sequences, the dot metacharacter, and the '+' quantifier in a
 * pattern. Uses control-byte placeholders for elements the matcher must treat
 * specially (see PRD §7.1 "processed-pattern byte contract"):
 *   \x01-\x04 : escaped literals ^ $ * \         \x05-\x0A : classes \d \D \w \W \s \S
 *   \x0B \x0C : zero-width \b \B                 \x0D      : dot metacharacter
 *   \x0E      : '+' quantifier (follows the element it quantifies)
 * A literal '.' (from \.) and literal '+' (from \+ or a bare '+' not following a
 * consumable element) are emitted as their ordinary ASCII bytes (0x2E / 0x2B);
 * a bare '*' is emitted as its ordinary byte 0x2A (the glob wildcard).
 *
 * This is the single malloc per pattern_match() call; the result is freed by
 * free_parsed_pattern(). Output length is always <= input length, so
 * malloc(strlen(pattern)+1) is sufficient.
 */
static char *process_escapes(const char *pattern) {
    if (!pattern) return NULL;

    size_t len = strlen(pattern);
    char *processed = malloc(len + 1);
    if (!processed) return NULL;

    const char *src = pattern;
    char *dst = processed;
    bool last_consumable = false;   /* did the previous emitted element consume a char? */

    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++;  /* skip the backslash */
            switch (*src) {
                case '^':  *dst++ = '\x01'; src++; last_consumable = true;  break;  /* \^ */
                case '$':  *dst++ = '\x02'; src++; last_consumable = true;  break;  /* \$ */
                case '*':  *dst++ = '\x03'; src++; last_consumable = true;  break;  /* \* */
                case '\\': *dst++ = '\x04'; src++; last_consumable = true;  break;  /* \\ */
                case '.':  *dst++ = '.';    src++; last_consumable = true;  break;  /* \.  -> literal dot  (0x2E) */
                case '+':  *dst++ = '+';    src++; last_consumable = true;  break;  /* \+  -> literal plus (0x2B) */
                case 'd':  *dst++ = '\x05'; src++; last_consumable = true;  break;  /* \d */
                case 'D':  *dst++ = '\x06'; src++; last_consumable = true;  break;  /* \D */
                case 'w':  *dst++ = '\x07'; src++; last_consumable = true;  break;  /* \w */
                case 'W':  *dst++ = '\x08'; src++; last_consumable = true;  break;  /* \W */
                case 's':  *dst++ = '\x09'; src++; last_consumable = true;  break;  /* \s */
                case 'S':  *dst++ = '\x0A'; src++; last_consumable = true;  break;  /* \S */
                case 'b':  *dst++ = '\x0B'; src++; last_consumable = false; break;  /* \b zero-width */
                case 'B':  *dst++ = '\x0C'; src++; last_consumable = false; break;  /* \B zero-width */
                default:   /* unrecognized escape: keep backslash + char literally (2 bytes) */
                    *dst++ = '\\'; *dst++ = *src++; last_consumable = true; break;
            }
        } else if (*src == '\\' && *(src + 1) == '\0') {
            *dst++ = *src++;        /* trailing lone backslash -> literal '\' */
            last_consumable = true;
        } else if (*src == '*') {
            *dst++ = '*'; src++;    /* bare '*' -> 0x2A glob wildcard (handled by the matcher) */
            last_consumable = false;
        } else if (*src == '+') {
            if (last_consumable) {
                *dst++ = '\x0E';    /* quantifier: one-or-more of the previous element */
                last_consumable = false;
            } else {
                *dst++ = '+';       /* literal '+' (not after a consumable element) */
                last_consumable = true;
            }
            src++;
        } else if (*src == '.') {
            *dst++ = '\x0D'; src++; /* bare '.' dot metacharacter */
            last_consumable = true;
        } else {
            *dst++ = *src++;        /* ordinary literal */
            last_consumable = true;
        }
    }

    *dst = '\0';
    return processed;
}
