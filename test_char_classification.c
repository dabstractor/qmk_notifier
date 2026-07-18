/*
 * test_char_classification.c — PRD §11.3 "Character classification" suite (~179 cases).
 *
 * The character classifiers (is_digit_char / is_word_char / is_whitespace_char /
 * is_word_boundary) are `static` in pattern_match.c, so they are unreachable
 * from a host test that links pattern_match.c as a separate translation unit.
 * This suite therefore exercises them INDIRECTLY through the public
 * pattern_match() API via the \d \D \w \W \s \S and \b \B metacharacters — the
 * only correct way to validate classification behavior across a translation-unit
 * boundary (PRD §11.4 / §15). It links ONLY pattern_match.c and includes
 * pattern_match.h; it never re-declares the classifiers extern, never copies
 * them, and never #includes the .c.
 *
 * Summary line: "Total tests run: %d"  (grepped by run_all_tests.sh).
 * Exit code: non-zero iff any case failed (tests_failed > 0).
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include "pattern_match.h"

/* ---- Test framework (Style A*: data-driven, indirect via pattern_match) ---- */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/*
 * Assert that pattern_match(pattern, input, case_sensitive) == expected.
 * Classification is reached transitively through whatever \d/\D/\w/\W/\s/\S/\b/\B
 * metacharacters the pattern embeds. The matcher is the source of truth
 * (PRD §17); a flip here means the matcher drifted.
 */
static void test_class(const char *pattern, const char *input,
                       bool case_sensitive, bool expected,
                       const char *metachar, const char *description) {
    tests_run++;
    bool result = pattern_match(pattern, input, case_sensitive);

    if (result == expected) {
        tests_passed++;
        printf("PASS: %s - %s\n", metachar, description);
    } else {
        tests_failed++;
        printf("FAIL: %s - %s\n", metachar, description);
        printf("      Pattern: '%s', Input: '%s', Case sensitive: %s\n",
               pattern, input ? input : "(null)",
               case_sensitive ? "true" : "false");
        printf("      Expected: %s, Got: %s\n",
               expected ? "true" : "false", result ? "true" : "false");
    }
}

/* Build a one-character input string for `c` into the caller's buffer. */
static char *single_char(char *buf, char c) {
    buf[0] = c;
    buf[1] = '\0';
    return buf;
}

/* Description helper for control-char inputs. */
static const char *char_name(char c, char *out, size_t n) {
    switch (c) {
        case '\t': snprintf(out, n, "'\\t'"); return out;
        case '\n': snprintf(out, n, "'\\n'"); return out;
        case '\r': snprintf(out, n, "'\\r'"); return out;
        case '\f': snprintf(out, n, "'\\f'"); return out;
        case '\v': snprintf(out, n, "'\\v'"); return out;
        case ' ':  snprintf(out, n, "' '");  return out;
        default:   snprintf(out, n, "'%c'", c); return out;
    }
}

int main(void) {
    char in[2];     /* single-char input buffer */
    char desc[40];  /* description buffer */
    int i;

    printf("Testing Character Classification (indirectly via \\d \\D \\w \\W \\s \\S \\b \\B)\n");
    printf("==================================================================================\n");

    /* ===== \d — digit class (digits 0-9 match, non-digits don't) ===== */
    printf("\n=== Testing \\d (digit class) ===\n");
    for (char c = '0'; c <= '9'; c++) {
        snprintf(desc, sizeof(desc), "digit '%c'", c);
        single_char(in, c);
        test_class("\\d", in, true, true, "\\d", desc);
    }
    /* non-digit characters must NOT match \d */
    {
        char non_digits[] = {'a', 'z', 'A', 'Z', ' ', '!', '_', '\t', '\n',
                             '\r', '\f', '\v', '/', ':'};
        for (i = 0; i < (int)sizeof(non_digits); i++) {
            single_char(in, non_digits[i]);
            test_class("\\d", in, true, false, "\\d",
                       char_name(non_digits[i], desc, sizeof(desc)));
        }
    }

    /* ===== \D — non-digit class (inverse of \d); representative samples ===== */
    printf("\n=== Testing \\D (non-digit class) ===\n");
    test_class("\\D", "5", true, false, "\\D", "digit '5' is not \\D");
    test_class("\\D", "a", true, true, "\\D", "letter 'a' is \\D");
    test_class("\\D", "_", true, true, "\\D", "underscore '_' is \\D");
    test_class("\\D", " ", true, true, "\\D", "space is \\D");

    /* ===== \w — word class (alnum + underscore match, others don't) ===== */
    printf("\n=== Testing \\w (word class) ===\n");
    for (char c = 'a'; c <= 'z'; c++) {
        snprintf(desc, sizeof(desc), "lowercase '%c'", c);
        single_char(in, c);
        test_class("\\w", in, true, true, "\\w", desc);
    }
    for (char c = 'A'; c <= 'Z'; c++) {
        snprintf(desc, sizeof(desc), "uppercase '%c'", c);
        single_char(in, c);
        test_class("\\w", in, true, true, "\\w", desc);
    }
    for (char c = '0'; c <= '9'; c++) {
        snprintf(desc, sizeof(desc), "digit '%c'", c);
        single_char(in, c);
        test_class("\\w", in, true, true, "\\w", desc);
    }
    /* underscore is a word character */
    test_class("\\w", "_", true, true, "\\w", "underscore '_'");
    /* non-word characters must NOT match \w */
    {
        char non_word[] = {' ', '!', '@', '#', '$', '%', '^', '&', '*', '(',
                           ')', '-', '+', '=', '\t', '\n', '\r', '\f', '\v',
                           '/', ':', ';', '<', '>', '?', '[', ']', '{', '}',
                           '|', '\\', '`', '~'};
        for (i = 0; i < (int)sizeof(non_word); i++) {
            single_char(in, non_word[i]);
            test_class("\\w", in, true, false, "\\w",
                       char_name(non_word[i], desc, sizeof(desc)));
        }
    }

    /* ===== \W — non-word class (inverse of \w; underscore is NOT \W);
     * representative samples. ===== */
    printf("\n=== Testing \\W (non-word class) ===\n");
    test_class("\\W", "a", true, false, "\\W", "letter is not \\W");
    test_class("\\W", "_", true, false, "\\W", "underscore '_' is not \\W");
    test_class("\\W", "7", true, false, "\\W", "digit is not \\W");
    test_class("\\W", " ", true, true, "\\W", "space is \\W");
    test_class("\\W", "!", true, true, "\\W", "'!' is \\W");

    /* ===== \s — whitespace class (space, \t, \n, \r, \f, \v) ===== */
    printf("\n=== Testing \\s (whitespace class) ===\n");
    {
        char ws[] = {' ', '\t', '\n', '\r', '\f', '\v'};
        const char *names[] = {"space", "tab", "newline", "carriage return",
                               "form feed", "vertical tab"};
        for (i = 0; i < (int)sizeof(ws); i++) {
            snprintf(desc, sizeof(desc), "whitespace %s", names[i]);
            single_char(in, ws[i]);
            test_class("\\s", in, true, true, "\\s", desc);
        }
    }
    /* non-whitespace characters must NOT match \s */
    {
        char non_ws[] = {'a', 'z', 'A', 'Z', '0', '9', '_', '!', '@', '#',
                         '$', '%', '^', '&', '*', '(', ')', '-', '+', '='};
        for (i = 0; i < (int)sizeof(non_ws); i++) {
            single_char(in, non_ws[i]);
            test_class("\\s", in, true, false, "\\s",
                       char_name(non_ws[i], desc, sizeof(desc)));
        }
    }

    /* ===== \S — non-whitespace class (inverse of \s); representative samples ===== */
    printf("\n=== Testing \\S (non-whitespace class) ===\n");
    test_class("\\S", " ", true, false, "\\S", "space is not \\S");
    test_class("\\S", "a", true, true, "\\S", "letter is \\S");
    test_class("\\S", "_", true, true, "\\S", "underscore is \\S");
    test_class("\\S", "!", true, true, "\\S", "'!' is \\S");

    /* ===== \b / \B — word-boundary assertions (NULL, edges, interior) =====
     * The boundary classifier is exercised indirectly through \b (boundary)
     * and \B (non-boundary). A NULL input is handled by pattern_match's NULL
     * contract (returns false). Position semantics (pos 0 / len / interior
     * word<->non-word transition) are surfaced by choosing inputs whose
     * boundary structure differs. */
    printf("\n=== Testing \\b and \\B (word boundaries) ===\n");
    /* NULL input -> pattern_match returns false (PRD §6 @note) */
    test_class("\\bword\\b", NULL, true, false, "\\b", "NULL input");

    /* empty string has no word boundary for 'word' */
    test_class("\\bword\\b", "", true, false, "\\b", "empty string");

    /* a single standalone word has boundaries on both sides */
    test_class("\\bword\\b", "word", true, true, "\\b", "standalone word");
    /* 'word' embedded in a larger phrase is still bounded by spaces */
    test_class("\\bword\\b", "a word here", false, true, "\\b",
               "word bounded by spaces");
    /* 'word' glued to a word char with no boundary (aword) must NOT match \bword\b */
    test_class("\\bword\\b", "awordhere", true, false, "\\b",
               "word glued to word chars - no boundary");
    /* leading boundary: \bword matches when preceded by non-word / start */
    test_class("\\bword", "word here", true, true, "\\b", "leading boundary");
    /* trailing boundary: word\b matches when followed by non-word / end */
    test_class("word\\b", "a word here", false, true, "\\b", "trailing boundary");
    /* word at start of string -> boundary at position 0 */
    test_class("\\bhello", "hello world", true, true, "\\b", "boundary at start");
    /* word at end of string -> boundary at end */
    test_class("world\\b", "hello world", true, true, "\\b", "boundary at end");

    /* \B asserts NO boundary: 'word' inside 'aword' is a non-boundary region */
    test_class("\\Bword", "aword", true, true, "\\B",
               "non-boundary before interior word");
    /* \B at a real boundary must NOT match */
    test_class("\\Bword", "word", true, false, "\\B",
               "non-boundary fails at start boundary");
    test_class("word\\B", "wordhere", true, true, "\\B",
               "non-boundary after interior word");
    /* adjacent word chars -> no boundary between them (interior XOR) */
    test_class("a\\Bb", "ab", true, true, "\\B", "interior word<->word no boundary");
    /* adjacent non-word chars -> no boundary between them */
    test_class("!\\B-", "!-", true, true, "\\B", "interior non-word<->non-word no boundary");

    /* ===== Anchored single-char classification (deterministic) ===== */
    printf("\n=== Testing anchored single-char classes ===\n");
    test_class("^\\d$", "5", true, true, "\\d", "anchored digit");
    test_class("^\\d$", "a", true, false, "\\d", "anchored non-digit");
    test_class("^\\w$", "_", true, true, "\\w", "anchored word (underscore)");
    test_class("^\\w$", " ", true, false, "\\w", "anchored non-word (space)");
    test_class("^\\s$", " ", true, true, "\\s", "anchored whitespace (space)");
    test_class("^\\S$", "a", true, true, "\\S", "anchored non-whitespace");

    /* ===== Summary (load-bearing summary line; grepped by run_all_tests.sh) ===== */
    printf("\n=== Test Results Summary ===\n");
    printf("Total tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n",
           tests_run > 0 ? 100.0 * tests_passed / tests_run : 0.0);

    if (tests_failed == 0) {
        printf("\nAll character classification tests PASSED! ✓\n");
        return 0;
    } else {
        printf("\nSome character classification tests FAILED! ✗\n");
        return 1;
    }
}
