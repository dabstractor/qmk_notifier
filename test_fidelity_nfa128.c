/* test_fidelity_nfa128.c — NFA budget fidelity gate (Issue 3, PRD §7.9/§11.3).
 *
 * WHY THIS SUITE EXISTS: the 9 standard pattern suites in run_all_tests.sh
 * compile pattern_match.c DIRECTLY, so the #ifndef guard at pattern_match.c:286
 * defaults NFA_MAX_PATTERN to 2048. The firmware, however, runs at
 * NFA_MAX_PATTERN=128 (notifier.c:14 #defines it before #include
 * "pattern_match.c"). A user pattern between ~129 and ~256 processed bytes
 * would pass every standard suite yet be SILENTLY CLAMPED on hardware — NEW()
 * (pattern_match.c:364) reuses the last pool slot once the state count reaches
 * NFA_MAX_STATES, degrading the NFA graph and potentially matching incorrectly.
 *
 * This suite closes that fidelity gap: run_all_tests.sh compiles it WITH
 * -DNFA_MAX_PATTERN=128 (the firmware budget), and the #error guard below makes
 * it FAIL TO COMPILE at any other budget. So the acceptance gate now exercises
 * the matcher at the production budget, and cannot silently drift back to 2048. */

#include <stdio.h>
#include <string.h>
#include "pattern_match.h"

/* Fidelity guard: this suite is meaningless unless compiled at the firmware
 * budget. The -DNFA_MAX_PATTERN=128 flag (run_all_tests.sh compile line) defines
 * the macro for BOTH this TU and pattern_match.c. Without it the compile aborts. */
#if !defined(NFA_MAX_PATTERN) || NFA_MAX_PATTERN != 128
#error "test_fidelity_nfa128 MUST be compiled with -DNFA_MAX_PATTERN=128 (the firmware budget). See run_all_tests.sh."
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define CK(cond, name) do { \
    tests_run++; \
    if (cond) { tests_passed++; printf("PASS: %s\n", name); } \
    else      { tests_failed++; printf("FAIL: %s\n", name); } \
} while (0)

int main(void) {
    /* (guard) Confirms the suite is actually running at the firmware budget. */
    CK(NFA_MAX_PATTERN == 128, "guard: NFA_MAX_PATTERN == 128 (firmware budget in effect)");

    /* (smoke) A short pattern still matches correctly at the smaller 128 budget
     * (no regression from the smaller state pool). */
    CK(pattern_match("hello", "hello", 1) == true,  "smoke: short pattern matches at 128 budget");
    CK(pattern_match("hello", "world", 1) == false, "smoke: short pattern rejects at 128 budget");

    /* (i) A pattern using a full 128 processed bytes (a 128-char literal -> 129
     * states, well under NFA_MAX_STATES=258) compiles fully and matches its
     * exact string, rejecting a too-short one. This is the firmware's whole
     * pattern budget being exercised. */
    {
        char pat[129], str_match[129], str_short[128];
        memset(pat, 'a', 128);       pat[128] = '\0';
        memset(str_match, 'a', 128); str_match[128] = '\0';
        memset(str_short, 'a', 127); str_short[127] = '\0';
        CK(pattern_match(pat, str_match, 1) == true,  "(i) 128-byte pattern matches its exact 128-char string");
        CK(pattern_match(pat, str_short, 1) == false, "(i) 128-byte pattern rejects a 127-char (too-short) string");
    }

    /* (ii) A pattern EXCEEDING the state pool is safely clamped, not crashed. A
     * 260-char literal -> 261 states -> NEW() reuses the last slot
     * (NFA_MAX_STATES=258), silently degrading the graph. The call must not
     * crash and must still reject a clearly non-matching string (safe no-match
     * per Issue 3). This is the exact hardware-only regression the 2048-budget
     * suites cannot detect. */
    {
        char pat[261];
        memset(pat, 'a', 260); pat[260] = '\0';
        CK(pattern_match(pat, "no-a-here-only-bbb", 1) == false, "(ii) over-budget pattern does not crash; rejects a non-match (safe clamp)");
    }

    printf("\nTotal tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    return tests_failed ? 1 : 0;
}