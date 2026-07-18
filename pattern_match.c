#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "notifier.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// Structure to hold parsed pattern information
typedef struct {
    const char *core_pattern;  // Points to processed pattern
    bool start_anchored;       // true if original pattern started with ^
    bool end_anchored;         // true if original pattern ended with $
    char *processed_pattern;   // Dynamically allocated, needs freeing
} parsed_pattern_t;

// Forward declarations for helper functions
static bool match_string_with_start(const char *pattern, const char *str, const char *string_start, bool case_sensitive);
static bool match_reaches_end_with_start(const char *pattern, const char *str, const char *string_start, bool case_sensitive);
static parsed_pattern_t parse_pattern(const char *pattern);
static void free_parsed_pattern(parsed_pattern_t *parsed);
static char *process_escapes(const char *pattern);
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive);
static char get_escaped_char(char placeholder);

// Character classification helper functions
static bool is_digit_char(char c);
static bool is_word_char(char c);
static bool is_whitespace_char(char c);
static bool is_word_boundary(const char *str, size_t pos);


// Returns true if a single consuming pattern element matches one string character.
// Handles ordinary literals, escaped-literal placeholders (\x01-\x04), character-class
// placeholders (\x05-\x0A) and the dot placeholder (\x0D). Does NOT handle '*', '\b'
// or '\B' - those are variable-/zero-width and are handled by the match loop.
static bool pattern_char_matches(char pc, char sc, bool case_sensitive) {
    if (pc >= '\x01' && pc <= '\x04') {
        char literal = get_escaped_char(pc);
        return case_sensitive ? (literal == sc)
                              : (tolower((unsigned char)literal) == tolower((unsigned char)sc));
    }
    switch (pc) {
        case '\x05': return is_digit_char(sc);           // \d
        case '\x06': return !is_digit_char(sc);          // \D
        case '\x07': return is_word_char(sc);            // \w
        case '\x08': return !is_word_char(sc);           // \W
        case '\x09': return is_whitespace_char(sc);      // \s
        case '\x0A': return !is_whitespace_char(sc);     // \S
        case '\x0D': return (sc != '\n' && sc != '\r');  // .
        default:  // ordinary literal byte
            return case_sensitive ? (pc == sc)
                                  : (tolower((unsigned char)pc) == tolower((unsigned char)sc));
    }
}

/* ====================== Thompson NFA matcher ====================== */
/* Replaces the former backtracking match_core. Compiles the processed pattern
 * into a small NFA (Thompson construction) and simulates it in
 * O(states * strlen). No backtracking => no catastrophic blow-up on patterns
 * like "a+a+a+...". Reference: Russ Cox, "Regular Expression Matching Can Be
 * Simple And Fast" (https://swtch.com/~rsc/regexp/regexp1.html). */

#define NFA_MAX_PATTERN 128                 /* max processed-pattern length      */
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)

enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };

typedef struct State State;
struct State {
    int    op;
    char   arg;       /* OP_CHAR: pattern byte; OP_ASSERT: 0x0B or 0x0C */
    State *out;
    State *out1;
    int    lastlist;  /* generation tag, set during simulation           */
};

/* ---- compile: processed pattern -> State pool, returns start state ---- */
static State *nfa_compile(const char *pat, State *pool, int *nstates_out) {
    int n = 0;
    State *start = NULL;
    State **tail = &start;            /* slot to write the next unit's start into */

    #define NEW() (&pool[n < NFA_MAX_STATES ? n++ : n])   /* allocate one state */

    for (const char *p = pat; *p; p++) {
        unsigned char b = (unsigned char)*p;
        if (b == 0x2A) {                         /* glob '*'  ==  regex .* */
            State *any = NEW(); any->op = OP_ANY;
            State *sp  = NEW(); sp->op  = OP_SPLIT; sp->out = any; sp->out1 = NULL;
            any->out = sp;                        /* loop back */
            *tail = sp; tail = &sp->out1;
        } else if (b == 0x0B || b == 0x0C) {      /* \b / \B : zero-width assert */
            State *a = NEW(); a->op = OP_ASSERT; a->arg = (char)b; a->out = NULL;
            *tail = a; tail = &a->out;
        } else if (b == 0x0E) {
            /* standalone quantifier marker should not occur (process_escapes
             * only emits it right after a consuming element). Skip defensively. */
            continue;
        } else {                                  /* consuming element (literal/class/dot) */
            State *c = NEW(); c->op = OP_CHAR; c->arg = (char)b; c->out = NULL;
            if ((unsigned char)p[1] == 0x0E) {    /* X+  ->  c then SPLIT(loop/exit) */
                State *sp = NEW(); sp->op = OP_SPLIT; sp->out = c; sp->out1 = NULL;
                c->out = sp;                      /* after one X, reach the split */
                *tail = c; tail = &sp->out1;
                p++;                              /* consume the 0x0E marker */
            } else {
                *tail = c; tail = &c->out;
            }
        }
    }

    State *m = NEW(); m->op = OP_MATCH;           /* accepting state */
    *tail = m;
    /* zero lastlist on every allocated state (fresh pool each call) */
    for (int i = 0; i < n; i++) pool[i].lastlist = 0;
    *nstates_out = n;

    #undef NEW
    return start;
}

/* ---- epsilon-closure add (follow SPLIT/ASSERT, collect CHAR/ANY/MATCH) ---- */
static int nfa_gen = 0;                           /* monotonic generation tag */

static void nfa_addstate(State **list, int *n, State *s,
                         const char *string_start, size_t abspos) {
    if (!s || s->lastlist == nfa_gen) return;     /* already in this closure */
    s->lastlist = nfa_gen;
    if (s->op == OP_MATCH) { list[(*n)++] = s; return; }
    if (s->op == OP_SPLIT) {
        nfa_addstate(list, n, s->out,  string_start, abspos);
        nfa_addstate(list, n, s->out1, string_start, abspos);
        return;
    }
    if (s->op == OP_ASSERT) {                     /* \b / \B : zero-width, guarded */
        int want_boundary = (s->arg == 0x0B);
        /* Empty original string: there is neither a boundary nor a non-boundary
         * (matches the legacy semantics the test suite encodes). */
        if (*string_start != '\0' &&
            is_word_boundary(string_start, abspos) == want_boundary)
            nfa_addstate(list, n, s->out, string_start, abspos);
        return;
    }
    list[(*n)++] = s;                             /* OP_CHAR / OP_ANY : wait for a char */
}

static int nfa_has_match(State **list, int n) {
    for (int i = 0; i < n; i++) if (list[i]->op == OP_MATCH) return 1;
    return 0;
}

/* ---- simulate. Drop-in for the old match_core (same semantics/signature) ----
 * full_match == false: success as soon as MATCH is reachable (prefix/substring).
 * full_match == true:  success only if MATCH is reachable after the whole string. */
static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive,
                      bool full_match) {
    State pool[NFA_MAX_STATES];
    int nstates;
    State *start = nfa_compile(pattern, pool, &nstates);
    if (!start) return full_match ? (*str == '\0') : true;  /* empty pattern */
    (void)nstates;

    State *clist_buf[NFA_MAX_STATES];
    State *nlist_buf[NFA_MAX_STATES];
    State **clist = clist_buf, **nlist = nlist_buf;
    int cn = 0, nn;
    size_t abspos = (size_t)(str - string_start);  /* absolute position in original string */

    nfa_gen++;
    nfa_addstate(clist, &cn, start, string_start, abspos);
    if (!full_match && nfa_has_match(clist, cn)) return true;   /* matches empty prefix */

    size_t pos = abspos;
    for (const char *p = str; *p; p++, pos++) {
        char c = *p;
        nfa_gen++; nn = 0;
        for (int i = 0; i < cn; i++) {
            State *s = clist[i];
            if (s->op == OP_ANY) {
                nfa_addstate(nlist, &nn, s->out, string_start, pos + 1);
            } else if (s->op == OP_CHAR && pattern_char_matches(s->arg, c, case_sensitive)) {
                nfa_addstate(nlist, &nn, s->out, string_start, pos + 1);
            }
        }
        /* swap clist/nlist */
        State **tmp = clist; clist = nlist; nlist = tmp; cn = nn;
        if (cn == 0) break;                                      /* dead - no states */
        if (!full_match && nfa_has_match(clist, cn)) return true;/* prefix matched */
    }
    return nfa_has_match(clist, cn) ? true : false;              /* full: match only at end */
}



bool pattern_match(const char *pattern, const char *str, bool case_sensitive) {
    if (!pattern || !str) {
        return false;
    }

    // Parse the pattern to identify anchors and process escapes
    parsed_pattern_t parsed = parse_pattern(pattern);

    // Use the appropriate matching strategy based on anchors
    bool result = match_with_anchors(&parsed, str, case_sensitive);

    // Clean up allocated memory
    free_parsed_pattern(&parsed);

    return result;
}


// Helper function to check if a match reaches the end of the string
static bool match_reaches_end_with_start(const char *pattern, const char *str, const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, true);
}

// Helper function to match with anchor support
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive) {
    if (!parsed || !str) {
        return false;
    }

    const char *core_pattern = parsed->core_pattern;

    if (parsed->start_anchored && parsed->end_anchored) {
        // Fully anchored: exact match required
        return match_reaches_end_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->start_anchored) {
        // Start anchored: match from beginning
        return match_string_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->end_anchored) {
        // End anchored: find a match that reaches the end
        size_t str_len = strlen(str);

        for (size_t i = 0; i <= str_len; i++) {
            if (match_reaches_end_with_start(core_pattern, str + i, str, case_sensitive)) {
                return true;
            }
        }
        return false;
    } else {
        // No anchors: substring matching (backward compatibility)

        // Special case: an unanchored empty pattern matches only the empty string.
        if (strlen(core_pattern) == 0) {
            return strlen(str) == 0;
        }

        // Try matching at different positions in the string
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++) {
            if (match_string_with_start(core_pattern, str + i, str, case_sensitive)) {
                return true;
            }
        }
        return false;
    }
}

// Helper function to convert escaped placeholder back to original character
static char get_escaped_char(char placeholder) {
    switch (placeholder) {
        case '\x01': return '^';
        case '\x02': return '$';
        case '\x03': return '*';
        case '\x04': return '\\';
        // Note: The following metacharacters don't have literal equivalents
        // They represent character classes and should be handled separately
        case '\x05': return 'd';  // \d placeholder (for debugging/error cases)
        case '\x06': return 'D';  // \D placeholder (for debugging/error cases)
        case '\x07': return 'w';  // \w placeholder (for debugging/error cases)
        case '\x08': return 'W';  // \W placeholder (for debugging/error cases)
        case '\x09': return 's';  // \s placeholder (for debugging/error cases)
        case '\x0A': return 'S';  // \S placeholder (for debugging/error cases)
        case '\x0B': return 'b';  // \b placeholder (for debugging/error cases)
        case '\x0C': return 'B';  // \B placeholder (for debugging/error cases)
        case '\x0D': return '.';  // . placeholder (for debugging/error cases)
        default: return placeholder;
    }
}

// Helper function to check if string starts with pattern
static bool match_string_with_start(const char *pattern, const char *str, const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, false);
}

// Process escape sequences and dot metacharacter in a pattern
// Uses special placeholder characters to mark escaped characters and metacharacters
// Process escape sequences, the dot metacharacter, and the '+' quantifier in a
// pattern. Uses control-byte placeholders for elements the matcher must treat
// specially:
//   \x01-\x04 : escaped literals ^ $ * \         \x05-\x0A : classes \d \D \w \W \s \S
//   \x0B \x0C : zero-width \b \B                 \x0D      : dot metacharacter
//   \x0E      : '+' quantifier (follows the element it quantifies)
// A literal '.' (from \.) and literal '+' (from \+ or a bare '+' not following a
// consumable element) are emitted as their ordinary ASCII bytes.
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
                case '^':  *dst++ = '\x01'; src++; last_consumable = true;  break;
                case '$':  *dst++ = '\x02'; src++; last_consumable = true;  break;
                case '*':  *dst++ = '\x03'; src++; last_consumable = true;  break;
                case '\\': *dst++ = '\x04'; src++; last_consumable = true;  break;
                case '.':  *dst++ = '.';    src++; last_consumable = true;  break;  /* \.  -> literal dot */
                case '+':  *dst++ = '+';    src++; last_consumable = true;  break;  /* \+  -> literal plus */
                case 'd':  *dst++ = '\x05'; src++; last_consumable = true;  break;
                case 'D':  *dst++ = '\x06'; src++; last_consumable = true;  break;
                case 'w':  *dst++ = '\x07'; src++; last_consumable = true;  break;
                case 'W':  *dst++ = '\x08'; src++; last_consumable = true;  break;
                case 's':  *dst++ = '\x09'; src++; last_consumable = true;  break;
                case 'S':  *dst++ = '\x0A'; src++; last_consumable = true;  break;
                case 'b':  *dst++ = '\x0B'; src++; last_consumable = false; break;  /* zero-width */
                case 'B':  *dst++ = '\x0C'; src++; last_consumable = false; break;  /* zero-width */
                default:   /* unrecognized escape: keep backslash + char literally */
                    *dst++ = '\\'; *dst++ = *src++; last_consumable = true; break;
            }
        } else if (*src == '\\' && *(src + 1) == '\0') {
            *dst++ = *src++;        /* trailing lone backslash */
            last_consumable = true;
        } else if (*src == '*') {
            *dst++ = '*'; src++;    /* glob wildcard (handled by matcher) */
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
            *dst++ = '\x0D'; src++; /* dot metacharacter */
            last_consumable = true;
        } else {
            *dst++ = *src++;        /* ordinary literal */
            last_consumable = true;
        }
    }

    *dst = '\0';
    return processed;
}




// Parse a pattern to identify anchors and process escape sequences
static parsed_pattern_t parse_pattern(const char *pattern) {
    parsed_pattern_t parsed = {0};

    if (!pattern) {
        return parsed;
    }

    const char *start = pattern;
    const char *end = pattern + strlen(pattern);

    // Check for start anchor (^) - only if not escaped
    if (*start == '^') {
        parsed.start_anchored = true;
        start++;  // Skip the ^ character
    }

    // Check for end anchor ($) - only if not escaped
    // We need to check if the $ is escaped by looking for an odd number of backslashes before it
    if (end > start && *(end - 1) == '$') {
        // Count backslashes before the $
        int backslash_count = 0;
        const char *check = end - 2;
        while (check >= start && *check == '\\') {
            backslash_count++;
            check--;
        }

        // If even number of backslashes (including 0), the $ is not escaped
        if (backslash_count % 2 == 0) {
            parsed.end_anchored = true;
            end--;  // Don't include the $ character
        }
    }

    // Create a temporary string for the core pattern (without anchors)
    size_t core_len = end - start;
    char *core_pattern = malloc(core_len + 1);
    if (!core_pattern) {
        // Memory allocation failed, fall back to original pattern
        parsed.core_pattern = pattern;
        parsed.processed_pattern = NULL;
        return parsed;
    }

    strncpy(core_pattern, start, core_len);
    core_pattern[core_len] = '\0';

    // Process escape sequences in the core pattern
    parsed.processed_pattern = process_escapes(core_pattern);
    free(core_pattern);  // Free the temporary core pattern

    if (parsed.processed_pattern) {
        parsed.core_pattern = parsed.processed_pattern;
    } else {
        // If escape processing failed, use the original pattern
        parsed.core_pattern = pattern;
    }

    return parsed;
}// 
// Free memory allocated for a parsed pattern
static void free_parsed_pattern(parsed_pattern_t *parsed) {
    if (parsed && parsed->processed_pattern) {
        free(parsed->processed_pattern);
        parsed->processed_pattern = NULL;
        parsed->core_pattern = NULL;
    }
}

// Character classification helper functions

/**
 * Check if a character is a digit (0-9)
 * @param c The character to check
 * @return true if the character is a digit, false otherwise
 */
static bool is_digit_char(char c) {
    return c >= '0' && c <= '9';
}

/**
 * Check if a character is a word character (alphanumeric + underscore)
 * Word characters are: a-z, A-Z, 0-9, and underscore (_)
 * @param c The character to check
 * @return true if the character is a word character, false otherwise
 */
static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || 
           (c >= 'A' && c <= 'Z') || 
           (c >= '0' && c <= '9') || 
           (c == '_');
}

/**
 * Check if a character is a whitespace character
 * Whitespace characters are: space, tab, newline, carriage return, form feed, vertical tab
 * @param c The character to check
 * @return true if the character is a whitespace character, false otherwise
 */
static bool is_whitespace_char(char c) {
    return c == ' ' ||   // space
           c == '\t' ||  // tab
           c == '\n' ||  // newline
           c == '\r' ||  // carriage return
           c == '\f' ||  // form feed
           c == '\v';    // vertical tab
}

/**
 * Check if a position in a string is a word boundary
 * A word boundary occurs at positions where:
 * - A word character is followed by a non-word character
 * - A non-word character is followed by a word character
 * - At string start if the first character is a word character
 * - At string end if the last character is a word character
 * @param str The string to check
 * @param pos The position to check (0-based index)
 * @return true if the position is a word boundary, false otherwise
 */
static bool is_word_boundary(const char *str, size_t pos) {
    if (!str) {
        return false;
    }
    
    size_t str_len = strlen(str);
    
    // Handle position at string start (pos == 0)
    if (pos == 0) {
        // Word boundary at start if first character is a word character
        return (str_len > 0 && is_word_char(str[0]));
    }
    
    // Handle position at string end (pos == str_len)
    if (pos == str_len) {
        // Word boundary at end if last character is a word character
        return (str_len > 0 && is_word_char(str[str_len - 1]));
    }
    
    // Handle position beyond string end
    if (pos > str_len) {
        return false;
    }
    
    // Handle position within string (between characters)
    // Word boundary exists if character types differ on either side
    bool prev_is_word = is_word_char(str[pos - 1]);
    bool curr_is_word = is_word_char(str[pos]);
    
    return prev_is_word != curr_is_word;
}
