# P1.M4.T1 Research Notes — Core Pattern Tests

> Live verification of the three deliverables this PRP specifies. All three
> files **already exist and pass** at HEAD (PRD §17: the code + passing tests win).
> This mirrors the P1.M3.T2 PRP's "live source of truth" approach. Research goal:
> capture the exact framework conventions, category inventory, build commands,
> and cross-item contracts so an implementer can recreate these files faithfully.

## Deliverables (3 files, all host-side gcc, link ONLY pattern_match.c)

| File | Lines | Cases | Build cmd (from run_all_tests.sh) |
|---|---|---|---|
| `test_pattern_match.c` | 843 | **376** | `gcc -o test_pattern_match test_pattern_match.c pattern_match.c` |
| `test_char_classification.c` | 245 | **179** | `gcc -o test_char_classification test_char_classification.c pattern_match.c` |
| `test_metachar_verification.c` | 51 | ~24 (smoke) | `gcc -o test_metachar_verification test_metachar_verification.c pattern_match.c` |

**Confirmed counts** (ran each binary):
- `test_pattern_match`: `Total tests run: 376 / passed: 376 / failed: 0` ✓
- `test_char_classification`: `Total tests run: 179 / passed: 179 / failed: 0` ✓
- `test_metachar_verification`: 24 PASS lines, 0 FAIL lines (no summary line — NOT aggregated by run_all_tests.sh) ✓

## Build flags (VERIFIED — none of these three need -I. or -std=c99)
- All three compile with bare `gcc -o X X.c pattern_match.c`. They `#include "pattern_match.h"` only.
- (Contrast: test_error_handling / test_memory_stress / test_invalid_patterns use `-I.`; test_comprehensive_integration uses `-std=c99 -DNOTIFIER_STUB`. Those are OTHER items — not this PRP.)

## Framework conventions

### test_pattern_match.c — Style A: `test_case_t` table + `run_test` helper
```c
typedef struct {
    const char *pattern; const char *input; bool case_sensitive;
    bool expected_result; const char *description;
} test_case_t;
static int tests_run, tests_passed, tests_failed;          // module-level counters
static void run_test(test_case_t test) { ... PASS:/FAIL: ... }   // compares to pattern_match()
```
Each category = `static void test_X() { printf banner; test_case_t arr[] = {...}; for(...) run_test(arr[i]); }`.
Driver `run_pattern_match_tests()` resets counters, calls all 17 assertion categories IN ORDER, prints
`=== Test Summary ===` block (`Total tests run: %d` / `Tests passed` / `Tests failed` / `Success rate: %.1f%%`).
`main()` calls it; `return tests_failed > 0 ? 1 : 0;`.

**18 categories** (grep-verified names):
1. test_start_anchor (^)
2. test_end_anchor ($)
3. test_full_anchor (^...$)
4. test_anchors_with_wildcards
5. test_character_classification — **INFO-ONLY** (printf description, NO run_test calls; placeholder category — see Gotcha)
6. test_basic_metacharacter_escapes (proves \d \D \w \W \s \S are processed, not literal)
7. test_basic_metacharacter_matching (isolated \d \D \w \W \s \S single/multi-char)
8. test_word_boundary_escape_processing (proves \b \B processed, not literal)
9. test_escape_sequences (\^ \$ \* \\ literals)
10. test_backward_compatibility
11. test_case_sensitivity
12. test_pattern_parsing
13. test_edge_cases
14. test_metacharacters_with_anchors
15. test_metacharacters_with_wildcards
16. test_metacharacter_case_sensitivity
17. test_metacharacter_backward_compatibility

(driver order groups 1-13 then 14-17 as "task 4 integration"; function def order differs slightly.)

### test_char_classification.c — Style A*: `test_class` helper (6-arg), loop-enumerated
```c
static int tests_run, tests_passed, tests_failed;
static void test_class(const char *pattern, const char *input, bool case_sensitive,
                       bool expected, const char *metachar, const char *description);
static char *single_char(char *buf, char c);          // 2-char buffer helper
static const char *char_name(char c, char *out, size_t n);  // '\t'/'\n'/... readable desc
```
`main()` uses `for (char c='0'; c<='9'; c++)` loops to EXHAUSTIVELY enumerate digit/word/whitespace
ranges + curated non-matching sets (e.g. `non_digits[]`, `non_word[]`, `non_ws[]`). Header comment
documents WHY it's indirect: classifiers are `static` in pattern_match.c, unreachable across a TU
boundary, so classification is reached transitively via \d \D \w \W \s \S \b \B metachars (PRD §11.4).
Prints load-bearing summary `Total tests run: %d` (grepped by run_all_tests.sh).

Sections: \d / \D / \w / \W / \s / \S / \b&\B boundaries / anchored single-char classes.

### test_metachar_verification.c — Style B: inline boolean printf smoke test
NO counters, NO summary line, NO aggregation by run_all_tests.sh. Just `printf("%s\n",
pattern_match(...) ? "PASS" : "FAIL")` for ~24 hand-picked \d \D \w \W \s \S combos + ops.
`return 0;` always. Header banner "Testing Basic Metacharacter Implementation".

## Cross-item contracts (what the tests assume is LIVE)

These three files are pure CONSUMERS of the public API `pattern_match(pattern, input, case_sensitive)`
declared in `pattern_match.h` (PRD §6). They link `pattern_match.c` and nothing else. For the
assertions to PASS, the full engine must be live:
- **P1.M1.T2** pipeline (`process_escapes`/`parse_pattern`/`pattern_match`/`get_escaped_char`) — COMPLETE.
- **P1.M2** NFA engine (`nfa_compile`/`nfa_addstate`/`nfa_match`) — COMPLETE.
- **P1.M3.T1** classifiers (`is_digit_char`/`is_word_char`/`is_whitespace_char`/`is_word_boundary`) — parallel, assumed present.
- **P1.M3.T2** (`pattern_char_matches`/`match_with_anchors`/wrappers) — **PARALLEL, being implemented now.** This is what wires the public API to the NFA. WITHOUT it, `pattern_match` returns false for everything and EVERY assertion in these test files fails.

**Implication for this PRP:** the test files themselves are independent artifacts — they can be
written regardless of whether P1.M3.T2 has landed. But the Validation Loop's "0 failures" gate
REQUIRES P1.M3.T2 + P1.M3.T1 to be live. The expected_result values encoded in the tables are the
behavioral contract (PRD §15 Appendix A); they are authoritative — if red, fix the matcher (PRD §13 #12).

## Key gotchas captured

1. **test_character_classification() is INFO-ONLY** (lines 131-152 of test_pattern_match.c): prints
   a description of the static classifiers, makes ZERO run_test() calls, and contains a STALE
   "146 tests pass" figure (live count is 179 in test_char_classification.c). This is a documented
   placeholder, not a defect. Reproduce it as-is; do NOT convert it to real assertions (that would
   change the 376 count) and do NOT "fix" the stale 146 (out of scope; the live count lives in
   test_char_classification.c).
2. **run_all_tests.sh aggregation depends on the EXACT summary string** `Total tests run: %d`.
   test_pattern_match + test_char_classification emit it (aggregated); test_metachar_verification
   does NOT (not aggregated — it's a smoke test with PASS/FAIL printout only). Do not add a summary
   line to test_metachar_verification or it would distort the runner's totals.
3. **No -I. / -std=c99** for these three. Adding them is harmless but diverges from run_all_tests.sh.
4. **Exit codes**: test_pattern_match + test_char_classification return non-zero on failure (CI gate);
   test_metachar_verification always returns 0 (smoke test, no failure aggregation).
5. **Escape doubling in C string literals**: every `\d` in a test pattern is written `"\\d"` in the
   source; `^`/`$`/`*` literals are `"\\^"`/`"\\$"`/`"\\*"`. A single backslash in source would be an
   undefined/other escape. The tables are full of `\\` — reproduce exactly.
6. **case_sensitive defaults to true** in most rows (the reference keymap relies on false-default at
   the STRUCT level via zero-fill, but the test tables pass true explicitly to pin behavior).

## Warning profile under strict flags (VERIFIED — corrects an initial PRP draft)

`run_all_tests.sh` compiles with bare `gcc` (no `-Wall`/`-Wextra`), so the committed files are
zero-warning in the actual build. But under `-Wall -Wextra -std=c99 -c` the profiles differ:

| File | exit | warnings (gcc line-count / actual warning count) | category |
|---|---|---|---|
| `test_pattern_match.c` | 0 | 64 lines / **16 warnings** | `-Wsign-compare` ONLY |
| `test_char_classification.c` | 0 | **0 / 0** | — |
| `test_metachar_verification.c` | 0 | **0 / 0** | — |

**Root cause of the 16 warnings:** the for-loop array-iteration idiom differs between the two
counting suites:
- `test_pattern_match.c` (16 sites): `for (int i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)`
  — compares signed `int i` with unsigned `size_t` → `-Wsign-compare`.
- `test_char_classification.c` (4 sites): `for (i = 0; i < (int)sizeof(arr); i++)`
  — casts to `int`, no warning.

These are PRE-EXISTING and benign (array sizes are small, no overflow possible). PRD DoD says
"no new compiler warnings beyond pre-existing ones," so reproducing `test_pattern_match.c`'s
uncast idiom verbatim is correct (PRD §17: the code is the source of truth). The PRP's Level 1b
gate asserts: tcc+tmv clean; tpm has ONLY `-Wsign-compare` (single category, ~16 warnings), no
unused-function / missing-prototype / other categories. `grep -o -- '-W[a-z-]*' | sort -u` is
the authoritative check (yields a single `-Wsign-compare` line).

## External references
- PRD §11.3 (test inventory), §11.4 (test framework), §15 (Appendix A semantics truth table),
  §17 (live source of truth), Definition of Done ("no new compiler warnings beyond pre-existing").
  Russ Cox NFA (https://swtch.com/~rsc/regexp/regexp1.html) is engine rationale, not test-authoring
  material.