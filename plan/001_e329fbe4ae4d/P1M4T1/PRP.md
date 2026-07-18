# PRP — P1.M4.T1: Core Pattern Tests

> **Scope.** This PRP covers the **two steps** of work item P1.M4.T1:
> **S1** `test_pattern_match.c` (the 376-case main suite) and **S2**
> `test_char_classification.c` (179 cases) + `test_metachar_verification.c`
> (smoke test). It produces **three host-side test programs** compiled with plain
> gcc (NOT the QMK build) that validate the public `pattern_match()` API declared
> in `pattern_match.h` (PRD §6). It does **not** implement the matcher
> (`pattern_match.c` — P1.M1/P1.M2/P1.M3, the engine), nor the word-boundary /
> comprehensive / robustness suites (`test_word_boundary_*.c`,
> `test_comprehensive_integration.c`, `test_error_handling.c`,
> `test_memory_stress.c`, `test_invalid_patterns.c` — siblings P1.M4.T2/T3, treated
> as independent), nor `run_all_tests.sh` (P1.M4.T4, COMPLETE), nor any firmware
> file (`notifier.*`, `rules.mk`). Do only what this document specifies.
>
> **Live source of truth (PRD §17).** All three files **already exist and pass** at
> HEAD (verified: 376/376, 179/179, 24-PASS/0-FAIL). Like the P1.M3.T2 PRP, this
> document treats the committed files as the implementation contract — reproduce
> them convention-for-convention and category-for-category.

## Goal

**Feature Goal**: Author the three core host-side test programs that exercise the
public `pattern_match(pattern, input, case_sensitive)` API across anchors,
escapes, wildcards, character classes (`\d \D \w \W \s \S`), word-boundary
assertions (`\b \B`), case sensitivity, parsing edge cases, and the dot/`+`
metacharacters — encoding the exact intended semantics of PRD §7/§15 as 376 + 179
data-driven assertions plus a ~24-assertion smoke test. These suites are the
**authoritative behavioral contract** for the matcher: per PRD §13 #12, "if a test
flips red, fix the matcher, not the test."

**Deliverable**: **Three new files at the repo root** (no other file touched):

1. **`test_pattern_match.c`** (~843 lines, **376 cases**) — the main suite. Uses
   the `test_case_t { pattern, input, case_sensitive, expected_result,
   description }` table + `run_test()` helper + module counters + a
   `run_pattern_match_tests()` driver over **17 assertion categories** (plus one
   info-only placeholder category). Prints the load-bearing summary
   `Total tests run: %d` grepped by `run_all_tests.sh`.
2. **`test_char_classification.c`** (~245 lines, **179 cases**) — exhaustive
   classification of `\d \D \w \W \s \S` (loop-enumerated ASCII ranges) + `\b \B`
   boundary assertions + anchored single-char classes. Uses a 6-arg `test_class`
   helper (because the classifiers are `static` in `pattern_match.c` and must be
   reached indirectly via the public API). Prints the load-bearing summary.
3. **`test_metachar_verification.c`** (~51 lines, ~24 assertions) — a plain
   boolean `PASS`/`FAIL` smoke test of `\d \D \w \W \s \S` in isolation + with
   operators. **No counters, no summary line** (not aggregated by the runner).

**Success Definition**:
- All three files compile with the **exact** `run_all_tests.sh` commands (Level 1)
  — bare `gcc`, zero warnings (that is how the runner builds them). Under stricter
  `-Wall -Wextra -std=c99`, `test_char_classification.c` and `test_metachar_verification.c`
  are **clean (zero warnings)**; `test_pattern_match.c` carries **16 pre-existing
  `-Wsign-compare` warnings** from its `for (int i = 0; i < sizeof(arr)/sizeof(arr[0]);
  i++)` idiom (int vs size_t). These are benign and tolerated (PRD DoD: "no new
  compiler warnings beyond pre-existing ones"); reproduce the idiom as-is to match the
  committed source of truth (see Gotcha).
- `test_pattern_match` reports `Total tests run: 376 / Tests failed: 0`.
- `test_char_classification` reports `Total tests run: 179 / Tests failed: 0`.
- `test_metachar_verification` prints **0 `FAIL:` lines** (24 `PASS:` lines).
- Each suite's exit code is non-zero iff it had failures (except
  `test_metachar_verification`, which always returns 0 — it is a smoke test).
- `run_all_tests.sh` aggregates these three's counts (376 + 179) into the overall
  total without distortion (test_metachar_verification contributes nothing — by
  design, it has no summary line).
- No edit to `pattern_match.{c,h}`, `notifier.*`, `rules.mk`, any sibling
  `test_*.c`, `run_all_tests.sh`, `PRD.md`, `tasks.json`, or `prd_snapshot.md`.

## User Persona (if applicable)

**Target User**: The developer agent (and CI) rebuilding or regressing the matcher.
These are host-side regression suites run with plain gcc on a dev machine — never
on the MCU, never inside the QMK build.

**Use Case**: After any change to `pattern_match.c` (the P1.M1 pipeline, the
P1.M2 NFA, the P1.M3 classifiers/predicate), the developer runs
`./run_all_tests.sh` (which builds+runs all 9 suites) to prove no observable
matching behavior changed. The three suites this PRP authors cover the bulk of
the behavioral surface (anchors/escapes/wildcards/classes/boundaries/case).

**Pain Points Addressed**: Without these suites, the matcher has no executable
specification — a refactor could silently flip `\w` to exclude `_`, or make `.`
match `\n`, or break `^...$` exactness, and nothing would catch it. The data
tables encode PRD §15 Appendix A as runnable truth.

## Why

- **The tests ARE the spec (PRD §17, §13 #12).** This module's "Definition of Done"
  is `./run_all_tests.sh` reporting 0 failures across ~2 019 assertions. The 376 +
  179 cases in these two files are the largest single contribution to that total
  and encode the anchors/escapes/wildcards/classes semantics most likely to
  regress during the NFA rewrite. A faithful, exhaustive table here is the
  difference between "the matcher matches what we intend" and "the matcher matches
  whatever the current code happens to do."
- **Classification must be tested INDIRECTLY (PRD §11.4).**
  `is_digit_char`/`is_word_char`/`is_whitespace_char`/`is_word_boundary` are
  `static` inside `pattern_match.c`. A host test that links `pattern_match.c` as a
  separate translation unit **cannot see them**. The only correct way to validate
  classification across that boundary is to drive it transitively through the
  `\d \D \w \W \s \S \b \B` metacharacters via the public API — which is exactly
  what `test_char_classification.c` does. (The tempting wrong approach —
  `#include "pattern_match.c"` or `extern`-declaring the classifiers — would
  couple the test to internals and is explicitly rejected by the header comment.)
- **Exhaustive enumeration catches off-by-one class boundaries.** `test_char_classification.c`
  loops `for (char c='0'; c<='9'; c++)` etc. so every digit, every letter, and all
  six whitespace chars (` \t\n\r\f\v`) are asserted, plus curated non-matching sets
  (punctuation, control chars). This is what pins "underscore IS a word char" and
  "exactly these 6 whitespace chars" — the two facts most easily broken.
- **`test_metachar_verification.c` is the fast smoke lever.** A 24-line boolean
  printout that a human can eyeball in one screenful — the first thing to run when
  "did I break the basic classes?" is the question. It complements (does not
  replace) the data-driven suites.
- **Cohesion across the plan.** P1.M1/P1.M2/P1.M3 build the engine; P1.M4 builds
  the suites that gate it; P2 builds the firmware that consumes it. These three
  files depend ONLY on the public `pattern_match()` signature (P1.M1.T1.S1,
  frozen), so they can be authored in parallel with the engine and will simply
  fail until P1.M3.T1+T2 land — which is the correct signal, not a bug. The
  sibling suites (P1.M4.T2/T3) and the runner (P1.M4.T4) are independent; this PRP
  touches none of them.

## What

Three self-contained C programs. Each links **only** `pattern_match.c` and
includes **only** `pattern_match.h` (+ standard headers). The exact content is the
live source of truth (PRD §17); reproduce it convention-for-convention.

### S1 — `test_pattern_match.c` (376 cases, 18 categories)

**Framework skeleton (top of file):**
```c
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "pattern_match.h"

typedef struct {
    const char *pattern;
    const char *input;
    bool case_sensitive;
    bool expected_result;
    const char *description;
} test_case_t;

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void run_test(test_case_t test) {
    tests_run++;
    bool result = pattern_match(test.pattern, test.input, test.case_sensitive);
    if (result == test.expected_result) {
        tests_passed++;
        printf("PASS: %s\n", test.description);
    } else {
        tests_failed++;
        printf("FAIL: %s\n", test.description);
        printf("      Pattern: '%s', Input: '%s', Case sensitive: %s\n",
               test.pattern, test.input, test.case_sensitive ? "true" : "false");
        printf("      Expected: %s, Got: %s\n",
               test.expected_result ? "true" : "false", result ? "true" : "false");
    }
}
```

**Category shape** (repeat per category):
```c
static void test_start_anchor() {
    printf("\n=== Testing Start Anchor (^) Functionality ===\n");
    test_case_t start_anchor_tests[] = {
        {"^searchterm", "searchterm", true, true,  "Start anchor: exact match from beginning"},
        {"^searchterm", "presearchterm", true, false, "Start anchor: should not match when not at beginning"},
        /* ... */
    };
    for (int i = 0; i < sizeof(start_anchor_tests) / sizeof(start_anchor_tests[0]); i++)
        run_test(start_anchor_tests[i]);
}
```

**The 18 categories** (exact names; reproduce all — the driver calls them in a
specific order and the total is exactly 376 assertion rows):

| # | Category function | Covers | Asserts? |
|---|---|---|---|
| 1 | `test_start_anchor` | `^` prefix matching | yes |
| 2 | `test_end_anchor` | `$` suffix matching | yes |
| 3 | `test_full_anchor` | `^...$` exact matching | yes |
| 4 | `test_anchors_with_wildcards` | `^sear*term$`, `^*test`, `test*$`, `^a*b*c$` | yes |
| 5 | `test_character_classification` | (placeholder — prints classifier descriptions) | **NO — info-only** (see Gotchas) |
| 6 | `test_basic_metacharacter_escapes` | proves `\d \D \w \W \s \S` are processed (not literal) | yes |
| 7 | `test_basic_metacharacter_matching` | isolated `\d \D \w \W \s \S` single + multi-char | yes |
| 8 | `test_word_boundary_escape_processing` | proves `\b \B` processed (not literal); invalid `\x`/`\z` literal | yes |
| 9 | `test_escape_sequences` | `\^ \$ \* \\` literal matching | yes |
| 10 | `test_backward_compatibility` | unanchored substring, glob `*` | yes |
| 11 | `test_case_sensitivity` | cs=true vs cs=false on letters | yes |
| 12 | `test_pattern_parsing` | anchor detection, mixed anchors | yes |
| 13 | `test_edge_cases` | empty patterns, NULL, empty strings, `^$` | yes |
| 14 | `test_metacharacters_with_anchors` | `\d`/`\w`/`\s` combined with `^`/`$` | yes |
| 15 | `test_metacharacters_with_wildcards` | `\d*`, `*\w`, `\s*\d` combos | yes |
| 16 | `test_metacharacter_case_sensitivity` | cs behavior of the classes | yes |
| 17 | `test_metacharacter_backward_compatibility` | classes + glob preserve old behavior | yes |

(driver `run_pattern_match_tests()` calls categories 1–13, then 14–17 as the
"task 4 integration" group, then the summary block.)

**Driver + summary + main (bottom of file):**
```c
void run_pattern_match_tests(void) {
    printf("Starting Pattern Match Test Suite\n=================================\n");
    tests_run = 0; tests_passed = 0; tests_failed = 0;
    test_start_anchor(); test_end_anchor(); test_full_anchor();
    test_anchors_with_wildcards(); test_character_classification();
    test_basic_metacharacter_escapes(); test_basic_metacharacter_matching();
    test_word_boundary_escape_processing(); test_escape_sequences();
    test_backward_compatibility(); test_case_sensitivity();
    test_pattern_parsing(); test_edge_cases();
    test_metacharacters_with_anchors(); test_metacharacters_with_wildcards();
    test_metacharacter_case_sensitivity(); test_metacharacter_backward_compatibility();
    printf("\n=== Test Summary ===\n");
    printf("Total tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    if (tests_failed == 0) printf("All tests PASSED! ✓\n");
    else                   printf("Some tests FAILED! ✗\n");
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
}
int main(void) {
    run_pattern_match_tests();
    return tests_failed > 0 ? 1 : 0;
}
```

### S2a — `test_char_classification.c` (179 cases, indirect classification)

**Header comment (reproduce verbatim — it documents the indirect-testing rationale):**
explains that the classifiers are `static` and therefore unreachable across a TU
boundary, so classification is driven transitively through `\d \D \w \W \s \S \b \B`
via the public API (PRD §11.4/§15). States the suite links ONLY `pattern_match.c`,
never re-declares the classifiers `extern`, never copies them, never `#include`s
the `.c`.

**Framework (6-arg helper + 2 small utilities):**
```c
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include "pattern_match.h"

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void test_class(const char *pattern, const char *input,
                       bool case_sensitive, bool expected,
                       const char *metachar, const char *description) {
    tests_run++;
    bool result = pattern_match(pattern, input, case_sensitive);
    if (result == expected) { tests_passed++; printf("PASS: %s - %s\n", metachar, description); }
    else { tests_failed++; printf("FAIL: %s - %s\n", metachar, description);
           printf("      Pattern: '%s', Input: '%s', ... Expected: ... Got: ...\n", ...); }
}
static char *single_char(char *buf, char c) { buf[0]=c; buf[1]='\0'; return buf; }
static const char *char_name(char c, char *out, size_t n);  /* '\t'->"'\\t'" etc. */
```

**Sections in `main()`** (loop-enumerated, exhaustive on the matching ranges):
- `\d` — `for (char c='0'; c<='9'; c++)` all match; curated `non_digits[]` (`a z A Z ' ' ! _ \t \n \r \f \v / :`) none match.
- `\D` — representative samples (inverse of `\d`).
- `\w` — `for` loops over `a-z`, `A-Z`, `0-9` all match + `_` matches; curated `non_word[]` (33 punctuation/control chars) none match.
- `\W` — representative samples (inverse of `\w`; `_` is NOT `\W`).
- `\s` — all six `' \t \n \r \f \v'` match; curated `non_ws[]` none match.
- `\S` — representative samples (inverse of `\s`).
- `\b`/`\B` — NULL input, empty string, standalone word, word-in-phrase, word-glued-to-word-chars (no boundary), leading/trailing boundary, `\Bword` interior, `a\Bb`.
- **anchored single-char classes** — `^\d$`, `^\w$`, `^\s$`, `^\S$` deterministic.

**Summary block (load-bearing — grepped by run_all_tests.sh):**
```c
printf("\n=== Test Results Summary ===\n");
printf("Total tests run: %d\n", tests_run);
printf("Tests passed: %d\n", tests_passed);
printf("Tests failed: %d\n", tests_failed);
printf("Success rate: %.1f%%\n", tests_run > 0 ? 100.0*tests_passed/tests_run : 0.0);
if (tests_failed == 0) { printf("\nAll character classification tests PASSED! ✓\n"); return 0; }
else { printf("\nSome character classification tests FAILED! ✗\n"); return 1; }
```

### S2b — `test_metachar_verification.c` (~24-assertion smoke test)

**Style B — inline boolean printout, NO counters/summary:**
```c
#include <stdio.h>
#include <stdbool.h>
#include "pattern_match.h"
int main() {
    printf("Testing Basic Metacharacter Implementation\n==========================================\n\n");
    printf("Testing \\d (digit) and \\D (non-digit):\n");
    printf("\\d matches '5': %s\n", pattern_match("\\d", "5", true) ? "PASS" : "FAIL");
    printf("\\d matches 'a': %s\n", pattern_match("\\d", "a", true) ? "FAIL" : "PASS");
    /* ... \D, \d\d, ^\d$, \d*, then \w/\W, \s/\S, case sensitivity,
       and combos (^\\d\\w$, \\s*, \\w*\\d) ... */
    return 0;   /* ALWAYS returns 0 — smoke test, no failure aggregation */
}
```
Covers `\d \D` (incl. `\d\d`, `^\d$`, `\d*`), `\w \W` (incl. `_` matches `\w`,
space matches `\W`), `\s \S` (incl. `\t`), case sensitivity, and combos
(`^\d\w$`, `\s*`, `\w*\d`). ~24 assertions; each prints `PASS` or `FAIL`.

### Success Criteria

- [ ] `test_pattern_match.c` exists, uses `test_case_t`+`run_test`, has all 18
      categories (incl. the info-only `test_character_classification`), a
      `run_pattern_match_tests()` driver, and `main()` returning non-zero on
      failure. Prints `Total tests run: 376` with 0 failures.
- [ ] `test_char_classification.c` exists, uses the 6-arg `test_class` helper +
      `single_char`/`char_name`, loop-enumerates the ASCII ranges, has the
      indirect-testing header comment, and prints `Total tests run: 179` with 0
      failures.
- [ ] `test_metachar_verification.c` exists, uses inline `? "PASS" : "FAIL"`
      printout, has NO counters/summary, always returns 0, and prints 0 `FAIL:`.
- [ ] All three compile with the exact `run_all_tests.sh` commands (Level 1a): exit 0,
      zero warnings under bare `gcc` (how the runner builds them).
- [ ] Under `-Wall -Wextra -std=c99` (Level 1b): test_char_classification.c +
      test_metachar_verification.c are clean; test_pattern_match.c carries only the
      16 pre-existing `-Wsign-compare` warnings from its for-loop idiom (no NEW warnings).
- [ ] No edit to any file other than these three. No new `#include` beyond
      `<stdio.h> <stdbool.h> <string.h> <stddef.h>` + `pattern_match.h`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact files to write are the live source of truth (all three exist
and pass at HEAD: 376/376, 179/179, 24-PASS/0-FAIL — verified by running each
binary). The full framework conventions (the `test_case_t` table+`run_test` style
vs the 6-arg `test_class` loop style vs the inline boolean smoke style), all 18
category names and their ordering in the driver, the info-only
`test_character_classification` placeholder quirk, the exhaustive ASCII-range
enumeration with curated non-matching sets, the load-bearing summary string
format, the exact build commands (no `-I.`/`-std=c99` for these three), the
exit-code conventions, the C-string escape-doubling rule, and the cross-item
dependency (validation "0 failures" requires the P1.M3.T1+T2 engine to be live)
were **all verified against the live code and passing tests during research**. The
single upstream contract — the public `pattern_match(const char*, const char*,
bool)` signature and its NULL/anchor/escape/class semantics (PRD §6, §15) — is
stated verbatim below so an implementer needs no access to the matcher internals.

### Documentation & References

```yaml
# MUST READ — authoritative behavioral spec the tables encode
- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "The verified truth table for every construct: '*' matches '\\n' (true),
        'a.b' vs 'a\\nb' (false, dot excludes newline), 'a+b' vs 'aaab' (true),
        '\\bword\\b' vs 'a word here' (true), '^hello$' exact, 'world$' suffix,
        'sear*term' substring, 'abc' vs 'ABC' cs=0/1. These expected_result values
        are what the test tables encode."
  critical: "The tables are AUTHORITATIVE (PRD §13 #12). If a row's expected
        result disagrees with the matcher's actual output, the MATCHER is wrong,
        not the test. Do not 'fix' a test to match buggy matcher behavior."

- file: PRD.md
  section: "### 11.3 Test inventory (what each suite covers)" + "### 11.4 The test framework"
  why: "§11.3 pins test_pattern_match=376 cases, test_char_classification=179 cases,
        test_metachar_verification=boolean smoke test. §11.4 documents the shared
        test_case_t{pattern,input,case_sensitive,expected,description} + run_test +
        summary('Total tests run: N / passed / failed / success rate') framework
        that run_all_tests.sh greps."
  critical: "The summary string 'Total tests run: %d' is LOAD-BEARING — run_all_tests.sh
        greps it to aggregate. test_metachar_verification deliberately has NONE (it is
        a smoke test, not aggregated). Do not add one to it or the runner's totals distort."

- file: PRD.md
  section: "## 6. File Specification: pattern_match.h"
  why: "The public API contract: bool pattern_match(const char *pattern, const char
        *str, bool case_sensitive). Returns false if either arg is NULL. The ONLY
        symbol these three test files call. Every test_case/test_class invocation
        exercises this function."
  critical: "The tests #include 'pattern_match.h' (NOT pattern_match.c) and link
        pattern_match.c. They never name a static internal. If you find yourself
        wanting to test is_digit_char directly, you are doing it wrong — see
        test_char_classification.c's header comment for the indirect approach."

- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve" (#8 dot vs glob; #12 tests authoritative)
  why: "#8: glob '*' matches '\\n'/'\\r'; dot '.' does NOT — the test tables assert
        both directions. #12: 'No observable result may change. The test suites
        encode the exact intended semantics. If a test flips red, fix the matcher,
        not the test.'"
  critical: "#12 is the governing principle for THESE files: they are the spec, not
        a consumer of it."

# The live source of truth (PRD §17) — reproduce convention-for-convention
- file: test_pattern_match.c
  section: "all 18 category functions + run_pattern_match_tests() driver + main()"
  why: "The implemented, passing 376-case suite. The canonical example of the
        test_case_t + run_test framework. Reproduce its category list, table
        contents, driver ordering, and summary format."
  critical: "Category #5 (test_character_classification) is INFO-ONLY — it prints
        classifier descriptions and makes ZERO run_test() calls, and contains a
        STALE '146 tests pass' figure (live count is 179, in test_char_classification.c).
        Reproduce it as-is: converting it to real assertions would change the 376
        count, and 'fixing' the 146 is out of scope."

- file: test_char_classification.c
  section: "header comment (indirect-testing rationale) + test_class helper + main() loops"
  why: "The implemented, passing 179-case classification suite. The canonical
        example of the 6-arg test_class + single_char + char_name framework and the
        for-loop ASCII-range enumeration with curated non-matching sets."
  critical: "Underscore MUST be asserted as a word char (\\w matches '_'); the
        whitespace class is EXACTLY ' \\t \\n \\r \\f \\v' (6 chars) — both are
        exhaustively enumerated here and are the facts most easily broken."

- file: test_metachar_verification.c
  section: "the whole 51-line file"
  why: "The implemented, passing smoke test. The canonical example of Style B
        (inline boolean printf, no counters)."
  critical: "It always returns 0 and has NO 'Total tests run:' line. run_all_tests.sh
        does NOT aggregate it. Adding a summary would distort the runner totals."

# How the runner consumes these files
- file: run_all_tests.sh
  section: "the 9 gcc compile lines + run_test() aggregation (lines ~17-26, ~48-110)"
  why: "The EXACT build commands: 'gcc -o test_pattern_match test_pattern_match.c
        pattern_match.c' (and identical for the other two — NO -I., NO -std=c99 for
        these three). The runner greps 'Total tests run:' to sum counts."
  critical: "These three files MUST compile with exactly those bare gcc commands.
        If a file needs -I. or -std=c99 to compile, it diverges from run_all_tests.sh
        and the runner's 'Compiling all test files...' step will fail."

# Cross-item contract — the public API these files consume
- file: pattern_match.h
  section: "the bool pattern_match(...) declaration + doc comment"
  why: "The single function signature every test calls. Frozen by P1.M1.T1.S1.
        Semantics (NULL->false, ^/$ anchors, \\*\\\\ escapes, substring default)
        are documented in the doc comment and encoded as expected_result values."
  critical: "The tests assume this signature is stable. They do NOT depend on any
        static internal of pattern_match.c."

# Parallel-item contract — what makes the assertions PASS
- file: plan/001_e329fbe4ae4d/P1M3T2/PRP.md
  section: "## What" (pattern_char_matches, match_with_anchors, wrappers)
  why: "P1.M3.T2 wires the public pattern_match() to the NFA. WITHOUT it (and
        P1.M3.T1's classifiers), pattern_match returns false for everything and
        EVERY assertion in these test files fails. The test files themselves are
        independent artifacts — they can be authored now — but the '0 failures'
        validation gate REQUIRES P1.M3.T1+T2 to be live."
  critical: "If validation shows mass failures, FIRST confirm P1.M3.T1+T2 have
        landed (grep for the real bodies, not stubs). Do NOT weaken expected_result
        values to make a half-built matcher pass — that corrupts the spec."

# External reference (reading only — rationale, not test-authoring material)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast'. Background
        on the Thompson NFA the matcher uses. Not needed to author tests; useful to
        understand WHY the pathological 'a+a+...+b' case must finish fast (it is
        covered by the sibling test_comprehensive_integration / nfa_stress, not here)."
  critical: "No dependency to add. The tests call only the public C API."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public API. The ONE function the tests call. DO NOT TOUCH.
pattern_match.c        # P1.M1+P1.M2 COMPLETE; P1.M3.T1+T2 (parallel) wire the public API live.
                        #   The tests LINK this file. They do not #include it.
notifier.h notifier.c  # P2 (COMPLETE). Do not touch; not referenced by these tests.
rules.mk               # P2 — do not touch.
test_pattern_match.c   # <-- THIS task S1 (exists, passes 376/376). Reproduce.
test_char_classification.c   # <-- THIS task S2a (exists, passes 179/179). Reproduce.
test_metachar_verification.c # <-- THIS task S2b (exists, 24-PASS smoke). Reproduce.
test_word_boundary_basic.c        # P1.M4.T2 (sibling) — do not touch.
test_word_boundary_integration.c  # P1.M4.T2 (sibling) — do not touch.
test_comprehensive_integration.c  # P1.M4.T2 (sibling) — do not touch.
test_error_handling.c             # P1.M4.T3 (sibling) — do not touch.
test_memory_stress.c              # P1.M4.T3 (sibling) — do not touch.
test_invalid_patterns.c           # P1.M4.T3 (sibling) — do not touch.
run_all_tests.sh        # P1.M4.T4 (COMPLETE) — gcc runner + aggregator. THE primary gate.
PRD.md                  # READ-ONLY.
plan/                   # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
test_pattern_match.c            # S1 — 376-case main suite (test_case_t + run_test).
                                #   18 categories: anchors, wildcards, escapes, classes,
                                #   boundaries, case sensitivity, parsing, edge cases,
                                #   + metachar integration groups. Prints summary.
test_char_classification.c      # S2a — 179-case classification suite (6-arg test_class).
                                #   Exhaustive ASCII-range enumeration for \d\D\w\W\s\S
                                #   + \b\B boundaries + anchored single-char. Prints summary.
test_metachar_verification.c    # S2b — ~24-assertion smoke test (inline PASS/FAIL).
                                #   No counters, no summary, always returns 0.
# Nothing else changes. No new files. No edits to the matcher, runner, or siblings.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — C string-literal escape doubling. Every backslash metacharacter in a
//   test pattern is written with a DOUBLED backslash in the source: the pattern
//   \d is the string literal "\\d"; \^ is "\\^"; \$ is "\\$"; \* is "\\*"; \\ is
//   "\\\\". A single backslash in source (\d, \^) is an undefined/other escape
//   and would NOT produce the intended pattern byte. The tables are FULL of "\\" —
//   reproduce exactly. (e.g. {"\\d", "5", true, true, "\\d: matches digit 5"}.)

// CRITICAL — the summary string is LOAD-BEARING. run_all_tests.sh greps the literal
//   "Total tests run: %d" (and "Tests passed:" / "Tests failed:") to aggregate the
//   overall total. test_pattern_match.c and test_char_classification.c MUST emit
//   exactly these three lines. test_metachar_verification.c MUST NOT (it is a smoke
//   test, not aggregated — adding a summary line would double-count or distort).

// CRITICAL — test_character_classification() (category #5 in test_pattern_match.c)
//   is INFO-ONLY. It printf's a description of the static classifiers and makes
//   ZERO run_test() calls. It also contains a STALE "146 tests pass" figure (the
//   live count is 179, living in test_char_classification.c). Reproduce it as-is:
//   do NOT convert it to real assertions (would change the 376 count) and do NOT
//   "fix" the 146 (out of scope; the live count is authoritative elsewhere).

// CRITICAL — classification is tested INDIRECTLY. is_digit_char/is_word_char/
//   is_whitespace_char/is_word_boundary are `static` in pattern_match.c, so a host
//   test linking pattern_match.c as a separate TU CANNOT see them. Drive them
//   transitively through \d \D \w \W \s \S \b \B via the public pattern_match()
//   (PRD §11.4). NEVER #include "pattern_match.c", NEVER extern-declare the
//   classifiers, NEVER copy them. test_char_classification.c's header comment
//   documents this contract — reproduce it.

// CRITICAL — build commands have NO -I. and NO -std=c99 for these three files.
//   run_all_tests.sh compiles them as:
//     gcc -o test_pattern_match        test_pattern_match.c        pattern_match.c
//     gcc -o test_char_classification  test_char_classification.c  pattern_match.c
//     gcc -o test_metachar_verification test_metachar_verification.c pattern_match.c
//   They compile cleanly because they only #include "pattern_match.h" (found via
//   the default include path for a quoted include in the same dir). Do NOT add a
//   dependency on -I. (the sibling error/memory/invalid suites use it; these don't).

// GOTCHA — exit codes. test_pattern_match.c and test_char_classification.c return
//   non-zero (1) on failure (CI gate: run_all_tests.sh checks exit_code).
//   test_metachar_verification.c ALWAYS returns 0 — it is a smoke test with no
//   failure aggregation; the runner only checks it built and ran.

// GOTCHA — the tests are pure CONSUMERS of the public API. They depend ONLY on
//   pattern_match()'s signature (P1.M1.T1.S1, frozen). They can be authored and
//   compiled even if the engine (P1.M3.T1+T2) is not yet live — but they will
//   FAIL every assertion until it is. That is the correct signal: mass failure
//   means "wire the engine," not "the tests are wrong." Do not weaken
//   expected_result values to match a half-built matcher (PRD §13 #12).

// GOTCHA — underscore is a WORD char; whitespace is EXACTLY ' \t \n \r \f \v'.
//   test_char_classification.c asserts both exhaustively (the for-loops over a-z/
//   A-Z/0-9 + the explicit '_' row; the 6-element ws[] array). These two facts are
//   the most easily broken by a matcher refactor and are the reason the
//   classification suite exists. Reproduce the exhaustive enumeration faithfully.

// GOTCHA — case_sensitive is passed EXPLICITLY per row (true in most rows). Do not
//   rely on a struct-level default; pin the expected behavior in each row.

// GOTCHA — the for-loop array-iteration idiom DIFFERS between the two counting
//   suites, and this is load-bearing for the warning profile:
//     test_pattern_match.c uses:        for (int i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
//     test_char_classification.c uses:  for (i = 0; i < (int)sizeof(arr); i++)  (casts!)
//   The UNCAST form in test_pattern_match.c produces 16 -Wsign-compare warnings under
//   -Wall -Wextra (comparing signed `int i` with unsigned size_t). These are PRE-EXISTING
//   and benign (array sizes are small); run_all_tests.sh compiles with bare gcc (no
//   -Wall/-Wextra) so they are invisible and tolerated (PRD DoD). Reproduce EACH file's
//   idiom verbatim — do NOT "fix" test_pattern_match.c's loops to cast (that diverges
//   from the committed source of truth, PRD §17) and do NOT drop the cast from
//   test_char_classification.c (it would add NEW warnings).
```

## Implementation Blueprint

### Data models and structure

No new data models beyond the test framework structs:

```c
// test_pattern_match.c
typedef struct {
    const char *pattern;          // pattern string (backslashes DOUBLED in the literal)
    const char *input;            // input string (may be "" for empty; NULL only in edge cases)
    bool case_sensitive;          // passed as 3rd arg to pattern_match()
    bool expected_result;         // the authoritative expected return (PRD §15)
    const char *description;      // printed on PASS:/FAIL:
} test_case_t;

// test_char_classification.c — no struct; uses positional args to test_class():
//   (pattern, input, case_sensitive, expected, metachar_label, description)
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1 (S2b): CREATE test_metachar_verification.c — the smoke test (smallest, sets the idiom)
  - CREATE: a ~51-line program: #include <stdio.h>, <stdbool.h>, "pattern_match.h"; int main().
  - IMPLEMENT: ~24 inline printf("%s\n", pattern_match(P,I,cs) ? "PASS" : "FAIL") assertions
    covering \d \D (incl. \d\d, ^\d$, \d*), \w \W (incl. _ matches \w, space matches \W),
    \s \S (incl. \t), case sensitivity, and combos (^\\d\\w$, \\s*, \\w*\\d).
  - FOLLOW pattern: the live test_metachar_verification.c (Style B: inline boolean, no counters).
  - NAMING: file test_metachar_verification.c; no helper functions (all inline in main).
  - PLACEMENT: repo root (same dir as pattern_match.c, so the quoted #include resolves).
  - DO NOT:
      * add counters or a "Total tests run:" summary line (it is a smoke test, not aggregated);
      * return non-zero on failure (always return 0);
      * add -I. or -std=c99 requirements (compiles with bare gcc).

Task 2 (S1): CREATE test_pattern_match.c — the 376-case main suite
  - CREATE: an ~843-line program with the test_case_t struct + run_test helper + module
    counters (tests_run/passed/failed) + 18 category functions + run_pattern_match_tests()
    driver + main().
  - IMPLEMENT (top): the typedef, the 3 static counters, run_test() (calls pattern_match,
    compares to expected_result, prints PASS:/FAIL: with diagnostics, bumps counters).
  - IMPLEMENT (categories): all 18 functions in the table above, each a printf banner +
    a test_case_t[] literal initializer + a for-loop calling run_test. Categories 1-4 =
    anchors; 5 = info-only placeholder (printf only, NO run_test); 6-8 = escape/class/boundary
    processing; 9-13 = escapes/backcompat/case/parsing/edge; 14-17 = metachar integration.
    Total assertion rows across categories 1-4,6-17 = exactly 376.
  - IMPLEMENT (driver): run_pattern_match_tests() — reset counters, call categories in the
    documented order (1-13, then 14-17), print the "=== Test Summary ===" block with the
    load-bearing "Total tests run: %d" line.
  - IMPLEMENT (entry): main() { run_pattern_match_tests(); return tests_failed > 0 ? 1 : 0; }.
  - FOLLOW pattern: the live test_pattern_match.c (reproduce category names, table contents,
    driver ordering, summary format verbatim — they are the spec).
  - NAMING: file test_pattern_match.c; helpers run_test, run_pattern_match_tests; categories
    test_<name>.
  - PLACEMENT: repo root.
  - DEPENDENCIES: pattern_match() [P1.M1.T1.S1 signature, frozen]; the FULL engine
    (P1.M1+P1.M2+P1.M3.T1+T2) for the assertions to PASS.
  - DO NOT:
      * convert test_character_classification (category #5) to real assertions (info-only);
      * "fix" its stale "146 tests" figure (out of scope);
      * change the 376 total (reproduce the exact row set);
      * add a summary line that differs from "Total tests run: %d";
      * weaken expected_result values to match a half-built matcher.

Task 3 (S2a): CREATE test_char_classification.c — the 179-case classification suite
  - CREATE: a ~245-line program with a header comment (indirect-testing rationale), the
    test_class 6-arg helper, single_char + char_name utilities, module counters, and main().
  - IMPLEMENT (helpers): test_class(pattern, input, cs, expected, metachar, desc) — calls
    pattern_match, compares, prints PASS:/FAIL: with metachar label + diagnostics, bumps
    counters. single_char(buf, c) writes a 2-char NUL-terminated string. char_name renders
    control chars readably.
  - IMPLEMENT (main): printf banner; then sections — \d (for c='0'..'9' loop + curated
    non_digits[]), \D (samples), \w (for a-z/A-Z/0-9 loops + '_' + curated non_word[]),
    \W (samples, incl. '_' is NOT \W), \s (the 6 ws[] chars + curated non_ws[]), \S (samples),
    \b/\B (NULL, empty, standalone, in-phrase, glued, leading/trailing, interior \B),
    anchored single-char classes (^\d$, ^\w$, ^\s$, ^\S$). Total test_class calls = 179.
  - IMPLEMENT (summary): the "=== Test Results Summary ===" block with the load-bearing
    "Total tests run: %d" line; return 0 on success, 1 on failure.
  - FOLLOW pattern: the live test_char_classification.c (reproduce the for-loop enumeration,
    the curated non-matching arrays, the \b/\B cases, the header comment verbatim).
  - NAMING: file test_char_classification.c; helpers test_class, single_char, char_name.
  - PLACEMENT: repo root.
  - DEPENDENCIES: pattern_match() [frozen]; the classifiers (P1.M3.T1.S1, reached indirectly)
    + pattern_char_matches (P1.M3.T2.S1) for the assertions to PASS.
  - DO NOT:
      * #include "pattern_match.c" or extern-declare the static classifiers (indirect only);
      * omit the header comment documenting the indirect-testing rationale;
      * skip the exhaustive ASCII-range loops (they are the point — catch off-by-one boundaries);
      * forget the '_' word-char row and the exactly-6 ws[] array.

Task 4: VERIFY — build all three with the exact run_all_tests.sh commands, run them,
  confirm 376/179/0-FAIL, confirm zero warnings under -Wall -Wextra -std=c99, confirm
  run_all_tests.sh aggregates the counts without distortion (Validation Loop).
```

### Implementation Patterns & Key Details

```c
// PATTERN (Style A — test_pattern_match.c): a fixed test_case_t struct + a single
//   run_test helper + module counters. Each category is a local array of test_case_t
//   initialized inline, iterated with sizeof(arr)/sizeof(arr[0]). This is the
//   framework PRD §11.4 specifies and run_all_tests.sh's grep expects.

// PATTERN (Style A* — test_char_classification.c): when the thing under test is
//   `static` (the classifiers), do NOT reach for #include "pattern_match.c". Instead
//   drive it transitively through the public API via the metachars that exercise it
//   (\d \D \w \W \s \S \b \B), and use a positional-arg helper (test_class) rather
//   than the struct, because the "metachar under test" is a useful diagnostic label.

// PATTERN (Style B — test_metachar_verification.c): for a fast human-eyeball smoke
//   test, inline printf("%s\n", pattern_match(...) ? "PASS" : "FAIL") with no
//   counters and no summary. It is the complement to, not a replacement for, the
//   data-driven suites.

// PATTERN: exhaustive enumeration over ASCII ranges (for c='0'..'9') is how you
//   catch off-by-one class boundaries and pin "underscore IS a word char" / "exactly
//   these 6 whitespace chars" — the two facts most easily broken. Pair every matching
//   range with a curated non-matching set (punctuation, control chars).

// PATTERN: the summary string "Total tests run: %d" is a CONTRACT with run_all_tests.sh.
//   Emit it (and "Tests passed:" / "Tests failed:") verbatim in the counting suites;
//   omit it entirely in the smoke test.

// ANTI-PATTERN: do NOT couple tests to matcher internals (#include the .c, extern the
//   statics, copy the classifiers). That makes the test fragile to refactors and
//   violates the TU-boundary discipline. Indirect via the public API only.

// ANTI-PATTERN: do NOT weaken expected_result to make a half-built matcher pass. The
//   tables encode PRD §15 (the intended semantics). If mass failures appear, confirm
//   P1.M3.T1+T2 landed first (PRD §13 #12).

// ANTI-PATTERN: do NOT add a "Total tests run:" line to test_metachar_verification.c.
//   It is a smoke test; run_all_tests.sh does not aggregate it. Adding one distorts totals.

// ANTI-PATTERN: do NOT "fix" the stale "146 tests" in test_character_classification()
//   or convert it to real assertions. It is an info-only placeholder; reproducing it
//   as-is preserves the 376 count and the historical record.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - THREE new files at the repo root (alongside pattern_match.c so the quoted
    #include "pattern_match.h" resolves with no -I. flag):
      (1) test_pattern_match.c         (S1, 376 cases)
      (2) test_char_classification.c    (S2a, 179 cases)
      (3) test_metachar_verification.c  (S2b, smoke)
    No other file changes. No edit to the matcher, runner, siblings, or docs.

BUILD (consumed by run_all_tests.sh, P1.M4.T4 — COMPLETE):
  - run_all_tests.sh lines ~17-19 compile these three with bare gcc:
      gcc -o test_pattern_match        test_pattern_match.c        pattern_match.c
      gcc -o test_char_classification  test_char_classification.c  pattern_match.c
      gcc -o test_metachar_verification test_metachar_verification.c pattern_match.c
    These MUST succeed with zero warnings under -Wall -Wextra -std=c99 (Level 1).

AGGREGATION (consumed by run_all_tests.sh):
  - The runner greps "Total tests run:" / "Tests passed:" / "Tests failed:" from each
    suite's stdout and sums them. test_pattern_match (376) + test_char_classification
    (179) contribute to the overall total (~2019 across all 9 suites).
    test_metachar_verification contributes NOTHING (no summary line — by design).

CONSUMERS (upstream callers, NOT this task):
  - pattern_match() [P1.M1.T1.S1, frozen signature] — the ONLY function these tests call.
  - The full engine (P1.M1 pipeline + P1.M2 NFA + P1.M3.T1 classifiers + P1.M3.T2
    predicate/strategy) — REQUIRED for the assertions to PASS (not to author/compile).

CROSS-MILESTONE CONTRACTS:
  - bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
    [pattern_match.h, P1.M1.T1.S1] — returns false on NULL; ^/$ anchors; \*\\ escapes;
    \d\D\w\W\s\S classes; \b\B boundaries; . dot; X+ quantifier; substring default.
  - PRD §15 Appendix A — the expected_result values are drawn from this truth table.

CONFIG / DATABASE / ROUTES:
  - N/A (host-side C test programs; pure consumers of a C function. No MCU, no QMK,
    no HID, no config. The runtime effect of these files is regression coverage of
    the matcher's public behavior.)
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. These three files are pure test
> programs; validation = they compile with the exact `run_all_tests.sh` commands
> (zero warnings) and report 0 failures. **Caveat:** the "0 failures" gate
> REQUIRES the P1.M3.T1+T2 engine to be live (parallel). If it is not, these
> suites will mass-fail — that signals "wire the engine," not "the tests are wrong."

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile each file with the EXACT run_all_tests.sh commands (no -I., no -std=c99).
gcc -o test_pattern_match test_pattern_match.c pattern_match.c; echo "tpm exit=$?"
gcc -o test_char_classification test_char_classification.c pattern_match.c; echo "tcc exit=$?"
gcc -o test_metachar_verification test_metachar_verification.c pattern_match.c; echo "tmv exit=$?"
# Expected: exit 0 for all three. FAIL if any is non-zero (compile error).

# 1b. Warning profile under strict flags (PRD DoD: "no NEW warnings beyond pre-existing").
#     test_char_classification.c and test_metachar_verification.c are CLEAN; test_pattern_match.c
#     carries 16 PRE-EXISTING -Wsign-compare warnings from its `int i < sizeof()` loop idiom.
gcc -Wall -Wextra -std=c99 -c test_pattern_match.c -o /tmp/tpm.o 2>/tmp/tpm_w.txt; echo "tpm exit=$?"
gcc -Wall -Wextra -std=c99 -c test_char_classification.c -o /tmp/tcc.o 2>/tmp/tcc_w.txt; echo "tcc exit=$?"
gcc -Wall -Wextra -std=c99 -c test_metachar_verification.c -o /tmp/tmv.o 2>/tmp/tmv_w.txt; echo "tmv exit=$?"
echo "tpm warnings=$(wc -l </tmp/tpm_w.txt)  tcc warnings=$(wc -l </tmp/tcc_w.txt)  tmv warnings=$(wc -l </tmp/tmv_w.txt)"
echo "tpm -Wsign-compare count=$(grep -c -- '-Wsign-compare' /tmp/tpm_w.txt)"
# Expected: tcc warnings=0, tmv warnings=0, tpm exit=0. tpm warnings should be 16 (or the
#   header lines gcc interleaves — assert the WARNING count via the grep). The ONLY warning
#   category in tpm_w.txt must be -Wsign-compare (no unused-function, no missing-prototype,
#   no other). FAIL if tpm has any OTHER warning category, or if tcc/tmv warn at all.
grep -o -- '-W[a-z-]*' /tmp/tpm_w.txt | sort -u   # Expected: a single line "-Wsign-compare"
rm -f /tmp/tpm.o /tmp/tcc.o /tmp/tmv.o /tmp/tpm_w.txt /tmp/tcc_w.txt /tmp/tmv_w.txt

# 1c. The framework contracts are present (grep the load-bearing tokens).
grep -q 'typedef struct {' test_pattern_match.c && grep -q 'test_case_t' test_pattern_match.c && echo "tpm framework (ok)"
grep -q 'static void run_test(test_case_t test)' test_pattern_match.c && echo "tpm run_test (ok)"
grep -q 'Total tests run: %d' test_pattern_match.c && echo "tpm summary line (ok)"
grep -q 'return tests_failed > 0 ? 1 : 0;' test_pattern_match.c && echo "tpm exit code (ok)"
grep -q 'static void test_class(const char \*pattern, const char \*input,' test_char_classification.c && echo "tcc test_class (ok)"
grep -q 'Total tests run: %d' test_char_classification.c && echo "tcc summary line (ok)"
grep -q 'pattern_match(' test_metachar_verification.c && echo "tmv calls pattern_match (ok)"
# CRITICAL: test_metachar_verification must NOT have a summary line.
test "$(grep -c 'Total tests run' test_metachar_verification.c)" -eq 0 && echo "tmv no-summary (ok)" \
  || echo "FAIL: test_metachar_verification has a summary line (would distort runner totals)"

# 1d. All 18 categories present in test_pattern_match.c (names are the contract).
for fn in test_start_anchor test_end_anchor test_full_anchor test_anchors_with_wildcards \
          test_character_classification test_basic_metacharacter_escapes \
          test_basic_metacharacter_matching test_word_boundary_escape_processing \
          test_escape_sequences test_backward_compatibility test_case_sensitivity \
          test_pattern_parsing test_edge_cases test_metacharacters_with_anchors \
          test_metacharacters_with_wildcards test_metacharacter_case_sensitivity \
          test_metacharacter_backward_compatibility; do
  grep -q "static void ${fn}(" test_pattern_match.c && echo "${fn} (ok)" || echo "FAIL: missing ${fn}"
done

# 1e. No forbidden coupling to matcher internals.
grep -q '#include "pattern_match.c"' test_char_classification.c \
  && echo "FAIL: tcc includes the .c (must be indirect)" || echo "tcc no-.c-include (ok)"
grep -qE 'extern.*(is_digit_char|is_word_char|is_whitespace_char|is_word_boundary)' test_char_classification.c \
  && echo "FAIL: tcc extern-declares a static classifier" || echo "tcc no-extern-classifiers (ok)"
```

### Level 2: Unit Run — the three suites in isolation

```bash
cd /home/dustin/projects/qmk-notifier

# 2A. test_pattern_match — 376 cases, 0 failures, non-zero exit on failure.
./test_pattern_match >/tmp/tpm.out 2>&1; echo "exit=$?"
grep -E 'Total tests run|Tests passed|Tests failed|Success rate' /tmp/tpm.out
# Expected: "Total tests run: 376 / Tests passed: 376 / Tests failed: 0 / Success rate: 100.0%"
#           AND exit=0. FAIL if Tests failed != 0 or exit != 0.
#           (If mass failures: P1.M3.T1+T2 engine not yet live — coordinate, do NOT weaken tests.)
grep -c '^FAIL:' /tmp/tpm.out   # Expected: 0

# 2B. test_char_classification — 179 cases, 0 failures.
./test_char_classification >/tmp/tcc.out 2>&1; echo "exit=$?"
grep -E 'Total tests run|Tests passed|Tests failed|Success rate' /tmp/tcc.out
# Expected: "Total tests run: 179 / Tests passed: 179 / Tests failed: 0 / 100.0%" AND exit=0.
grep -c '^FAIL:' /tmp/tcc.out   # Expected: 0

# 2C. test_metachar_verification — smoke test, 0 FAIL lines, always exit 0.
./test_metachar_verification >/tmp/tmv.out 2>&1; echo "exit=$?"
echo "PASS=$(grep -c 'PASS' /tmp/tmv.out)  FAIL=$(grep -c 'FAIL' /tmp/tmv.out)"
# Expected: FAIL=0 (PASS ~24) AND exit=0. (No "Total tests run:" line in tmv.out.)
rm -f /tmp/tpm.out /tmp/tcc.out /tmp/tmv.out
```

### Level 3: Integration — the canonical runner aggregates correctly

```bash
cd /home/dustin/projects/qmk-notifier

# 3A. The full runner builds+runs all 9 suites and aggregates. These three must
#     contribute 376 + 179 = 555 to the total with 0 failures each.
./run_all_tests.sh 2>&1 | grep -E 'Total tests run across all suites|Total tests passed|Total tests failed|✓.*PATTERN|✗.*PATTERN|Character Classification|Metacharacter Verification'
# Expected: "Total tests run across all suites: <N>" with the matched suites
#           reporting ALL TESTS PASSED, and overall "ALL TESTS PASSED".
# FAIL if any of the three reports failures or the overall result is FAILED.
# NOTE: test_metachar_verification prints PASS/FAIL but is NOT in the aggregated
#       total (no summary line) — verify it appears in the run but adds 0 to the count.

# 3B. Belt-and-suspenders: per-suite fail counts via the PRD §11.2A idiom.
for t in test_pattern_match test_char_classification test_metachar_verification; do
  gcc -o "$t" "$t.c" pattern_match.c 2>/dev/null \
    && printf "%-32s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')" \
    || echo "FAIL: $t did not build"
done
# Expected: fails=0 for every line. (test_metachar_verification has no '^FAIL:' prefix
#           on its lines — it prints "PASS"/"FAIL" inline — so grep '^FAIL:' yields 0
#           either way; rely on Level 2C's "FAIL=" count for it.)
```

### Level 4: Cross-Check Against the Behavioral Spec (PRD §15)

```bash
cd /home/dustin/projects/qmk-notifier

# 4A. A handful of PRD §15 Appendix A cases must hold (proves the tables encode the
#     spec, not the current code's accidents). Build a tiny harness in the suite's idiom.
cat > /tmp/spec_probe.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  int f=0;
  #define CK(p,i,cs,want) do{ if(pattern_match(p,i,cs)!=(want)){ printf("FAIL: %s,%s,%d want %d\n",p,i,cs,want); f++; } }while(0)
  CK("*","a\nb",1,1);          /* glob matches newline            (PRD §15) */
  CK("a.b","a\nb",1,0);        /* dot excludes newline            (PRD §13 #8) */
  CK("a.b","axb",1,1);         /* dot matches other chars         */
  CK("a+b","aaab",1,1);        /* + = one-or-more                 */
  CK("a+b","b",0,0);           /* + needs >=1                     */
  CK("\\^","^",1,1);           /* escaped caret literal           */
  CK("a\\+b","a+b",1,1);       /* escaped plus literal            */
  CK("v\\.code","v.code",1,1); /* escaped dot literal             */
  CK("\\bword\\b","a word here",0,1); /* word-boundary anchored   */
  CK("^hello$","hello",1,1);   /* full anchor exact               */
  CK("^hello$","hello world",1,0); /* full anchor rejects non-exact */
  CK("world$","hello world",0,1); /* end anchor suffix            */
  CK("abc","ABC",0,1);         /* case-insensitive substring      */
  CK("abc","ABC",1,0);         /* case-sensitive no-match         */
  printf("%d failures\n", f);
  return f ? 1 : 0;
}
EOF
gcc -w /tmp/spec_probe.c pattern_match.c -I. -o /tmp/spec_probe && /tmp/spec_probe
# Expected: "0 failures", exit 0. These are the PRD §15 truth-table rows; the committed
# test_pattern_match.c / test_char_classification.c tables encode these same expectations.
rm -f /tmp/spec_probe.c /tmp/spec_probe
```

## Final Validation Checklist

### Technical Validation

- [ ] All 4 validation levels completed successfully.
- [ ] All three files compile with the exact `run_all_tests.sh` commands (Level 1a): exit 0.
- [ ] Warning profile correct (Level 1b): tcc + tmv clean; tpm has ONLY the 16 pre-existing
      `-Wsign-compare` warnings (no other category).
- [ ] `test_pattern_match` reports `Total tests run: 376 / Tests failed: 0`, exit 0.
- [ ] `test_char_classification` reports `Total tests run: 179 / Tests failed: 0`, exit 0.
- [ ] `test_metachar_verification` prints 0 `FAIL` lines, exit 0 (no summary line).
- [ ] `./run_all_tests.sh` reports 0 failures overall and aggregates 376 + 179 without distortion.

### Feature Validation

- [ ] All success criteria from "What" section met (3 files, exact frameworks, exact counts).
- [ ] The 18 categories of `test_pattern_match.c` are all present with the documented names.
- [ ] `test_character_classification` category (#5) is info-only (no `run_test` calls) — reproduced as-is.
- [ ] `test_char_classification.c` exhaustively enumerates `\d \w \s` ASCII ranges + curated non-matching sets.
- [ ] Underscore asserted as a word char; whitespace exactly `' \t \n \r \f \v'` (6 chars).
- [ ] `test_metachar_verification.c` has NO counters/summary and always returns 0.
- [ ] PRD §15 spec probe (Level 4A) prints "0 failures".

### Code Quality Validation

- [ ] Follows the established test framework conventions (`test_case_t`+`run_test`; 6-arg `test_class`; inline smoke).
- [ ] File placement matches the desired tree (repo root, alongside `pattern_match.c`).
- [ ] Anti-patterns avoided (no `.c` include, no `extern` statics, no weakened `expected_result`, no summary line on the smoke test).
- [ ] No new `#include` beyond `<stdio.h> <stdbool.h> <string.h> <stddef.h>` + `pattern_match.h`.
- [ ] C string-literal escape doubling correct throughout (`"\\d"` not `"\d"`, etc.).

### Documentation & Deployment

- [ ] `test_char_classification.c` header comment documents the indirect-testing rationale (reproduced verbatim).
- [ ] Load-bearing summary strings match `run_all_tests.sh`'s grep expectations exactly.
- [ ] No environment variables or config introduced (host-side C tests).

---

## Anti-Patterns to Avoid

- ❌ Don't couple tests to matcher internals (`#include "pattern_match.c"`, `extern` the static
  classifiers, copy the classifiers). Drive classification indirectly via the public API.
- ❌ Don't weaken `expected_result` values to make a half-built matcher pass — the tables are the
  spec (PRD §13 #12). Mass failure means "wire P1.M3.T1+T2," not "the tests are wrong."
- ❌ Don't add a `Total tests run:` summary line to `test_metachar_verification.c` — it is a smoke
  test, not aggregated by the runner; adding one distorts the totals.
- ❌ Don't convert `test_character_classification()` (category #5) to real assertions, or "fix" its
  stale "146 tests" figure — it is an info-only placeholder; reproducing it preserves the 376 count.
- ❌ Don't use single backslashes in C string literals for metachar patterns — `\d` is `"\d"` in a
  pattern but must be written `"\\d"` in source (escape doubling).
- ❌ Don't add `-I.` or `-std=c99` requirements to these three files — `run_all_tests.sh` compiles
  them with bare `gcc -o X X.c pattern_match.c`; diverging breaks the runner's compile step.
- ❌ Don't change the exit-code conventions — counting suites return non-zero on failure (CI gate);
  the smoke test always returns 0.
- ❌ Don't edit the matcher, the runner, sibling suites, `PRD.md`, or `tasks.json`.