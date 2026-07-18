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

/* ===== P1.M1.T2.S2: parse_pattern, free_parsed_pattern, get_escaped_char,
 *                     pattern_match (public), match_with_anchors STUB ===== */

/* Holds the result of parsing a user pattern: anchor flags + the
 * process_escapes()-processed core the NFA consumes. */
typedef struct {
    const char *core_pattern;    /* points into processed_pattern, or the raw pattern on malloc failure */
    bool        start_anchored;  /* true if the original pattern began with '^' */
    bool        end_anchored;    /* true if the original pattern ended with an unescaped '$' */
    char       *processed_pattern; /* malloc'd by process_escapes(); freed by free_parsed_pattern() */
} parsed_pattern_t;

/* match_with_anchors is fully implemented in P1.M3.T2.S2. Until then a STUB
 * returning false is provided (see below) so pattern_match() links and the
 * public API is exercised; real matching (and the passing suites) arrive with
 * P1.M3.T2.S2. */
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive);

/* Reverse-map a process_escapes() placeholder byte back to the human-readable
 * character. Used by pattern_char_matches() (P1.M3.T2.S1) for the escaped-literal
 * branch (0x01-0x04); the class/assertion/dot entries (0x05-0x0D) are kept for
 * debug/diagnostic readability. (Item-spec §3c, debug-only.) */
static char get_escaped_char(char placeholder) {
    switch (placeholder) {
        case '\x01': return '^';   /* \^ */
        case '\x02': return '$';   /* \$ */
        case '\x03': return '*';   /* \* */
        case '\x04': return '\\';  /* \\ */
        /* The following metacharacters have no single literal equivalent; they
         * represent classes/assertions handled directly in pattern_char_matches.
         * Returned here only for debug/error messages. */
        case '\x05': return 'd';   /* \d */
        case '\x06': return 'D';   /* \D */
        case '\x07': return 'w';   /* \w */
        case '\x08': return 'W';   /* \W */
        case '\x09': return 's';   /* \s */
        case '\x0A': return 'S';   /* \S */
        case '\x0B': return 'b';   /* \b */
        case '\x0C': return 'B';   /* \B */
        case '\x0D': return '.';   /* .  (dot metacharacter) */
        default:     return placeholder;  /* ordinary literal byte */
    }
}

/* Release the malloc'd processed-pattern buffer (if any) and NULL both pointers.
 * Safe on a zero-initialized struct or when parsed == NULL. On the malloc-failure
 * fallback path processed_pattern is NULL and core_pattern points at the CALLER's
 * pattern, so we must NOT free core_pattern — the `processed_pattern` guard
 * ensures we only ever free what we allocated. */
static void free_parsed_pattern(parsed_pattern_t *parsed) {
    if (parsed && parsed->processed_pattern) {
        free(parsed->processed_pattern);
        parsed->processed_pattern = NULL;
        parsed->core_pattern = NULL;  /* it aliased processed_pattern (or the caller's pattern) */
    }
}

/* Detect a leading '^' (start anchor) and a trailing UNESCAPED '$' (end anchor),
 * carve out the core substring between them, and process its escapes.
 *
 * EVEN-BACKSLASH-COUNT RULE for the end anchor (PRD §7.3, item-spec §6 Mode A):
 * A trailing '$' is a real end anchor ONLY when an EVEN number of backslashes
 * (including zero) immediately precede it. An ODD count means the '$' is escaped
 * and is part of the core (process_escapes turns '\$' into the 0x02 literal).
 *   "abc$"     : 0 backslashes => end anchor,  core = "abc"
 *   "abc\\$"    : 1 backslash   => escaped '$',  core = "abc" + 0x02
 *   "abc\\\\$"   : 2 backslashes => end anchor,   core = "abc" + 0x04 (the '\\' -> 0x04)
 *   "abc\\\\\\$"  : 3 backslashes => escaped '$',  core = "abc" + 0x04 + 0x02
 * This is the standard "is the final metacharacter quoted?" test: walk left from
 * the '$' counting consecutive '\\'; even => unquoted.
 *
 * The `end > start` guard rejects degenerate inputs: a lone "^" (start anchor
 * only) leaves start==end after skipping '^', so no end check runs; "^$" still
 * detects both anchors with an empty core (PRD §15: '^$' matches the empty string). */
static parsed_pattern_t parse_pattern(const char *pattern) {
    parsed_pattern_t parsed = {0};   /* all flags false, all pointers NULL */

    if (!pattern) {
        return parsed;
    }

    const char *start = pattern;
    const char *end   = pattern + strlen(pattern);

    /* Start anchor: a leading '^' is always a start anchor (it cannot be an
     * escape target as the first char; '\^' would be processed to 0x01 later). */
    if (*start == '^') {
        parsed.start_anchored = true;
        start++;                     /* skip the '^' */
    }

    /* End anchor: trailing '$' that is NOT escaped (even backslash count). */
    if (end > start && *(end - 1) == '$') {
        int backslash_count = 0;
        const char *check = end - 2;
        while (check >= start && *check == '\\') {
            backslash_count++;
            check--;
        }
        if (backslash_count % 2 == 0) {   /* even (0,2,4,...) => unescaped '$' */
            parsed.end_anchored = true;
            end--;                          /* drop the '$' */
        }
    }

    /* Carve the core (between anchors) and process its escapes. */
    size_t core_len = (size_t)(end - start);
    char *core_pattern = malloc(core_len + 1);
    if (!core_pattern) {
        /* malloc failure: fall back to the raw pattern, no escape processing.
         * processed_pattern stays NULL => free_parsed_pattern() is a no-op. */
        parsed.core_pattern      = pattern;
        parsed.processed_pattern = NULL;
        return parsed;
    }
    strncpy(core_pattern, start, core_len);
    core_pattern[core_len] = '\0';

    parsed.processed_pattern = process_escapes(core_pattern);
    free(core_pattern);              /* temp copy no longer needed */

    if (parsed.processed_pattern) {
        parsed.core_pattern = parsed.processed_pattern;
    } else {
        /* process_escapes failed (its own malloc): fall back to the raw pattern. */
        parsed.core_pattern = pattern;
    }

    return parsed;
}

/* STUB — replaced by P1.M3.T2.S2. Returns false so pattern_match() is safe to
 * link and call today; the real anchor-aware NFA matching (and the passing test
 * suites) arrive with P1.M3.T2.S2. Item-spec §5 explicitly permits this stub. */
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive) {
    (void)parsed;
    (void)str;
    (void)case_sensitive;
    return false;
}

/* ===== PUBLIC API (declared in pattern_match.h) =====
 * NULL-guard -> parse -> match -> free. Caller frees nothing (PRD §6). */
bool pattern_match(const char *pattern, const char *str, bool case_sensitive) {
    if (!pattern || !str) {
        return false;
    }

    parsed_pattern_t parsed = parse_pattern(pattern);

    bool result = match_with_anchors(&parsed, str, case_sensitive);

    free_parsed_pattern(&parsed);

    return result;
}
