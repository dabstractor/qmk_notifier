# PRP — P1.M4 (Milestone): Host-Side Test Suite (9 programs + test runner)

> **Milestone scope.** This PRP covers the **four** P1.M4 sub-tasks that produce
> the host-side acceptance harness: **T1** core pattern tests
> (`test_pattern_match.c` 376 cases + `test_char_classification.c` 179 +
> `test_metachar_verification.c` smoke), **T2** word-boundary & integration
> (`test_word_boundary_basic.c` 74 + `test_word_boundary_integration.c` 189 +
> `test_comprehensive_integration.c` 10 categories), **T3** robustness & errors
> (`test_error_handling.c` + `test_memory_stress.c` + `test_invalid_patterns.c`
> 1008 cases), and **T4** the runner (`run_all_tests.sh`). All nine `.c` files
> link **only** `pattern_match.c` (never `notifier.c`) and exercise the matcher
> solely through the public `pattern_match()`. **The deliverable MUST report 0
> failures across all suites** (PRD §11.2A), which requires fixing the one
> currently-failing **G3** case in `test_memory_stress.c`.

## Goal

**Feature Goal**: Land the 9 gcc host-side test programs + `run_all_tests.sh` that
constitute the PRD §11 acceptance gate. Every suite compiles with the exact flags
in PRD §11.1, links only `pattern_match.c`, uses the `test_case_t`/`run_test` (or
`test_pattern`, or boolean-smoke) framework from PRD §11.4, prints the exact
summary lines `run_all_tests.sh` greps, and reports **0 `FAIL:` lines** (the §11.2A
gate). The pathological-NFA stress (§11.2B) and the realistic-pattern checks
(§11.2C) are emitted/run by the runner.

**Deliverable**: 10 new files at repo root (no other file touched):
1. `run_all_tests.sh` (~181 lines) — compiles all 9, runs them, aggregates counts
   via grep, runs a perf micro-benchmark, exits non-zero on any failure.
2. `test_pattern_match.c` — 376-case main suite (Style A: `test_case_t` + `run_test`).
3. `test_char_classification.c` — 179 cases (classes via metachars + boundary positions).
4. `test_word_boundary_basic.c` — 74 cases (Style A, summary `"Tests run:"`).
5. `test_word_boundary_integration.c` — 189 cases (Style A, summary `"Tests run:"`).
6. `test_metachar_verification.c` — smoke test (Style C, no counts, `return 0`).
7. `test_comprehensive_integration.c` — 10 timed categories (`run_test_with_perf`,
   `-DNOTIFIER_STUB`, summary `"Total test categories run:"`).
8. `test_error_handling.c` — NULL/garbage/malformed-escape survival (~93 cases).
9. `test_memory_stress.c` — memory-stress + error recovery (**G3 fixed**; ~100 cases).
10. `test_invalid_patterns.c` — 1008 cases (46-pathological-pattern × input table + explicit).

**Success Definition**:
- All 10 files exist; the 9 `.c` compile cleanly with the §11.1 flags (plain gcc,
  **zero** warnings with `-Wall`).
- `./run_all_tests.sh` reports **0 failures across every suite** (the §11.2A loop
  prints `fails=0` for all 9 lines) and exits 0.
- The runner emits the §11.2B pathological stress (`a+a+a+a+a+a+a+a+a+a+b` vs
  199×`a`) printing `result=0` in **< 50 ms**, and the §11.2C realistic-pattern
  checks.
- Aggregate count ≈ 2019 (5 "Total tests run:" suites + 2 "Tests run:" suites);
  comprehensive + metachar are excluded from the sum by design (checked via exit code).
- The G3 `"Anchored huge pattern exact match"` case in `test_memory_stress.c`
  **PASSES** (pattern kept within `NFA_MAX_PATTERN=2048`).
- ASan/UBSan clean on the error + memory-stress suites (no crash on any input).
- No modification to `pattern_match.{c,h}`, `notifier.{c,h}`, `rules.mk`,
  `PRD.md`, `tasks.json`, `prd_snapshot.md`, `.gitignore`, or the `qmk_stubs/`
  notifier-test artifacts (those are P2-era, out of scope).

## User Persona (if applicable)

**Target User**: Two consumers. (1) The **developer/maintainer** running
`./run_all_tests.sh` as the PRD §11 acceptance gate before a release. (2)
**`pattern_match.c` itself** — the suites are its behavioral specification; PRD
§17: "Where this spec and the code disagree, the code + the passing tests win;
report the drift." The suites call **only** `pattern_match()` (public API).

**Use Case**: `run_all_tests.sh` → gcc each `.c` with `pattern_match.c` → run
each binary → grep summary lines for counts + `$?` for pass/fail → aggregate →
emit + run the perf bench → exit. CI or a human reads the final verdict line.

**User Journey**:
```
./run_all_tests.sh
 ├─ gcc -o test_X test_X.c pattern_match.c [flags]   (×9, exact §11.1 flags)
 ├─ for each suite: ./test_X
 │     ├─ runs category functions, each calling pattern_match(pattern,input,cs)
 │     ├─ prints "PASS: <desc>" / "FAIL: <desc>" per case + diagnostic on FAIL
 │     └─ prints summary: "Total tests run: N" | "Tests run: N" | (smoke: none)
 ├─ run_test() aggregates counts via the grep contract; ✓/✗ per suite via $?
 ├─ Overall: "Total tests run across all suites: ~2019 / passed / failed"
 ├─ embeds perf_test.c heredoc (100000×7 calls, asserts < 1.0s)
 └─ exit (total_failed==0 ? 0 : 1)
```

**Pain Points Addressed**: Without the suite there is no acceptance gate; a
matcher rebuild cannot be verified. The suite encodes the exact intended
semantics (PRD §13 #12: "If a test flips red, fix the matcher, not the test")
AND the performance guarantee (§11.2B linear-time NFA, no catastrophic
backtracking). The current live repo fails the gate by exactly one case (G3);
this milestone closes that gap so the gate reads green.

## Why

- **P1.M4 is the acceptance gate.** PRD §11 is titled "Build & Test (the
  acceptance gate)" and §17 names the test corpus the "living source of truth."
  P1.M1/M2/M3 (matcher) and P2 (firmware) are complete or finishing in parallel;
  P3 then runs this exact gate. Without P1.M4, "Definition of Done" cannot be
  checked.
- **The suites ARE the spec.** PRD §13 #12 and §17 make the tests authoritative:
  ~1826 numbered data-table assertions (376+179+74+189+1008) plus survival suites
  encode every construct in §15 (Appendix A). A matcher regression flips a test
  red; the fix is always in the matcher, never the test — so the tests must
  exist and must be exact.
- **Closes the G3 defect (the single current failure).** P1.M3's PRP documents
  that `run_all_tests.sh` is 2018/2019 because of one `test_memory_stress` case
  (`"Anchored huge pattern exact match"`) that builds a 40000-char pattern
  overflowing `NFA_MAX_PATTERN=2048`. PRD §11.2A requires `fails=0` for every
  line, so delivering green **requires** fixing that case in `test_memory_stress.c`
  (a test-file fix, not a matcher change). The fix is verified (see Context).
- **Cohesion across the plan.** P1.M3 (parallel) finishes the matcher; P3 runs
  this gate + syncs docs (README counts, §11.3 figures). P1.M4 must produce a
  self-contained, reproducible harness whose per-suite `fails=0` is the contract
  P3 validates. The harness links only `pattern_match.c` and depends on no QMK
  symbols, so it builds on any host with gcc.

## What

Create 10 files at repo root. **The exact summary-line each counting suite prints
is a hard constraint** (run_all_tests.sh greps it — see Context §2). **The exact
gcc flags are fixed by PRD §11.1** (see Context §3). **All suites link only
`pattern_match.c`.**

### T4 — `run_all_tests.sh` (the contract; implement first or in lockstep)
Bash script (~181 lines): banner; the 9 `gcc -o …` lines from §11.1 (verbatim
flags); a `run_test(name, exe)` function that runs `./$exe`, echoes output, greps
`"Total tests run:"` then `"Tests run:"` to parse `N/P/F`, accumulates totals,
prints ✓ (exit 0) / ✗ (non-zero); calls `run_test` for all 9 in PRD §3 order;
overall summary (`Total tests run across all suites:`, `Total tests passed:`,
`Total tests failed:`, success rate via `bc -l`); a `perf_test.c` heredoc
(100000 iterations × 7 patterns, asserts < 1.0s); final exit `(total_failed==0)`.

### T1 — core pattern tests
- `test_pattern_match.c` (376 cases, Style A): categories for start-anchor `^`,
  end-anchor `$`, full-anchor `^…$`, anchors+wildcards, escape sequences
  (`\^ \$ \* \\`), case sensitivity, wildcard-only, parsing/edge cases,
  metachars-with-anchors, word-boundary escape processing. Summary
  `"Total tests run: %d"`. Exit `tests_failed>0?1:0`.
- `test_char_classification.c` (179 cases, Style A*): digit/word/whitespace
  classes exercised **indirectly via `\d \D \w \W \s \S` metachars** through the
  public `pattern_match()` (the classifiers are `static` — unreachable directly);
  plus `is_word_boundary` position cases (NULL, edges, interior) surfaced via
  `\b`/`\B`. Summary `"Total tests run: %d"`.
- `test_metachar_verification.c` (Style C smoke): ~25 inline
  `printf("\\d matches '5': %s\n", pattern_match(...) ? "PASS" : "FAIL")` lines
  covering `\d \D \w \W \s \S` + combos. **No counters; `return 0;` always.**

### T2 — word-boundary & integration
- `test_word_boundary_basic.c` (74 cases, Style A): `\b`/`\B` basic semantics.
  **Summary `"Tests run: %d"`** (no "Total"). Exit `return tests_failed>0?1:0;`.
- `test_word_boundary_integration.c` (189 cases, Style A): `\b`/`\B` integrated
  with anchors, wildcards, metachar classes, edge cases, case sensitivity.
  Summary `"Tests run: %d"`. Exit `return tests_failed>0?1:0;`.
- `test_comprehensive_integration.c` (10 timed categories): `#include <time.h>`
  + `<assert.h>`; `run_test_with_perf(name, bool (*fn)(void))` timing each
  category with `clock()`; counts `tests_run/passed/failed` as **categories**;
  tracks `memory_operations` (count of `pattern_match` calls) and `total_time`.
  Summary `"Total test categories run: %d"` (NOT aggregated by the runner —
  intentional; caught via exit code). Built with `-std=c99 -DNOTIFIER_STUB`.

### T3 — robustness & errors
- `test_error_handling.c` (Style A + inline NULL tests, ~93 cases): explicit
  `pattern_match(NULL,"test",true)`, `pattern_match("test",NULL,true)`,
  `pattern_match(NULL,NULL,true)` each asserting `false` (PRD §6 `@note`); plus
  a `test_case_t` table of malformed/unknown escapes kept literal (`\x \z \1 \9
  \@ \# \% \&`), trailing/odd backslashes, embedded NUL handling. Summary
  `"Total tests run: %d"`. Exit `tests_failed>0?1:0`.
- `test_memory_stress.c` (Style B, ~100 cases): `test_pattern(p,in,cs,exp,desc)`
  inline helper; categories = memory stress (long escaped/metachar patterns),
  memory edge cases, pathological patterns (wildcards, `X+`), error recovery,
  **max-length strings**. **G3 FIX: in `test_maximum_length_strings()`, cap the
  `huge_pattern` build loop so the pattern stays within `NFA_MAX_PATTERN=2048`
  (e.g. `i < 500` → ~2000 chars) so the `"Anchored huge pattern exact match"`
  case passes; keep the 50KB `malloc`s and the huge-STRING tests.** Summary
  `"Total tests run: %d"`. Exit `tests_failed>0?1:0`.
- `test_invalid_patterns.c` (Style B, 1008 cases): a `problematic_patterns[]`
  table of **46** pathological patterns (unmatched `[`/`(`/quantifiers/braces,
  invalid escapes, `^^`/`$$`/`**`, `|`/`a|b`, escaped `\[\]\(\)\|`) × a
  `test_inputs[]` table → cross product (~920 combo cases) + ~88 explicit
  invalid-construct cases (all treated as literals). Summary
  `"Total tests run: %d"`. Exit `tests_failed>0?1:0`.

### Success Criteria

- [ ] All 10 files present at repo root; the 9 `.c` compile with the exact §11.1
      flags; `gcc -Wall` on each → **zero** warnings.
- [ ] `./run_all_tests.sh` → every suite prints `fails=0` in the §11.2A loop;
      overall `Total tests failed: 0`; script exits 0.
- [ ] `test_memory_stress` reports **0 failures** (G3 case passes).
- [ ] The runner emits §11.2B (pathological NFA, `result=0`, < 50 ms) and the
      §11.2C realistic-pattern checks.
- [ ] Summary-line contract honored: 5 suites print `"Total tests run:"`, 2 print
      `"Tests run:"`, comprehensive prints `"Total test categories run:"`,
      metachar prints none.
- [ ] ASan/UBSan clean on `test_error_handling` + `test_memory_stress`.
- [ ] No file other than the 10 deliverables is modified.

## All Needed Context

### Context Completeness Check

**Pass.** The live repo **is** the PRD §17 source of truth: all 9 `test_*.c` +
`run_all_tests.sh` exist and (except for the one G3 case) pass. Every structural
fact below — the 3 framework styles, the exact summary-line strings, the gcc
flags, the per-suite counts (376/179/74/189/1008/…), the runner's grep contract,
the G3 root cause and its verified fix — was **read from the live code and
verified empirically** during research (see `research/findings.md`). An
implementer needs no access to P1.M1/M2/M3 beyond `pattern_match.h` (reproduced
inline below) and the knowledge that the matcher is complete and 2018/2019-green
at P1.M4 start.

### Documentation & References

```yaml
# MUST READ — authoritative spec for the test suite
- file: PRD.md
  section: "## 11. Build & Test (the acceptance gate)"
  why: "The exact gcc flags (11.1), the 3-part acceptance gate (11.2A/B/C), the
        test inventory with per-suite counts (11.3), and the test framework
        description (11.4). This is the contract the 10 files must satisfy."
  critical: "11.2A is the gate: 'fails=0 for every line' via grep '^FAIL:'. The
        G3 case currently makes test_memory_stress fail=1 -> the rebuild MUST
        fix it (Context §1). 11.2B needs the pathological case < 50 ms."

- file: PRD.md
  section: "### 11.3 Test inventory (what each suite covers)" + "### 11.4 The test framework"
  why: "The per-suite counts (376/179/74/189/smoke/10/~93/~100/1008) and the
        test_case_t/run_test pattern. 'run_all_tests.sh greps these summary lines
        to aggregate. The exit code is non-zero iff any suite failed.'"
  critical: "Counts are approximate and drift; the §11.2A per-suite fails=0 is
        authoritative, not a fixed integer. The framework's summary line is the
        grep target the runner depends on."

- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "The verified truth table (e.g. '*' vs 'a\\nb' -> true; 'a.b' vs 'a\\nb' ->
        false; '\\bword\\b' vs 'a word here' cs=0 -> true; '^hello$' vs 'hello
        world' -> false). Use these as the seed assertions for the data tables."

- file: PRD.md
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"  +  "Definition of Done"
  why: "Lists the target line counts (run_all_tests.sh ~181; test_pattern_match.c
        ~843; etc.) and states 'the code + the passing tests win.' The live
        test_*.c ARE the source of truth to reproduce (with the G3 fix applied)."

- file: pattern_match.h
  why: "The ONLY symbol the suites link against. Public API:
        bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
        @note: 'Returns false if either argument is NULL.' Reproduce the doc
        comment's EXAMPLES as test cases."
  critical: "NULL pattern OR NULL str -> false. This is the contract test_error_
        handling verifies directly (pattern_match(NULL,...) etc.)."

- file: plan/001_e329fbe4ae4d/P1M3/PRP.md
  section: "## Goal / Success Definition" + "### Known Gotchas"
  why: "The PARALLEL predecessor. P1.M3 finishes the matcher. Its Success
        Definition: 'run_all_tests.sh -> >= 2018/2019; the only failure is the
        G3 memory-stress case (P1.M4/P3 scope).' Defines the exact state P1.M4
        starts from (matcher complete, 2018/2019 green, one G3 failure to fix)."
  critical: "Treat P1.M3 as a CONTRACT: assume pattern_match() is fully correct
        when P1.M4 starts. Do NOT modify pattern_match.c. The G3 failure is a
        test-file defect, fixed in test_memory_stress.c only."

- file: plan/001_e329fbe4ae4d/P1M2/PRP.md
  section: "### Known Gotchas -> G2" + Sizing
  why: "Explains NFA_MAX_PATTERN sizing: host/test default 2048, #ifndef-guarded
        so QMK overrides. Confirms WHY a >2048-byte processed pattern overflows
        the pool and degrades (bounded clamp, no crash) — the G3 mechanism."
  critical: "NFA_MAX_PATTERN=2048 is the host ceiling. test_memory_stress patterns
        MUST stay under it or they hit graceful (result-corrupting) degradation."

- file: plan/001_e329fbe4ae4d/architecture/findings_and_risks.md
  section: "RISK-1" + "RISK-2" + "Current Test Results"
  why: "RISK-1: notifier.c has ZERO host coverage (out of scope for P1.M4 — the
        qmk_stubs/ + run_notifier_stub_tests.sh + test_notifier_dispatch.c are
        P2 artifacts, NOT part of run_all_tests.sh). RISK-2: -DNOTIFIER_STUB is
        vestigial (pass harmlessly). Confirms the 1826/2019 count semantics."
  critical: "Do NOT add notifier tests to run_all_tests.sh. Do NOT #ifdef on
        NOTIFIER_STUB. The 9 suites link pattern_match.c ONLY."

- file: plan/001_e329fbe4ae4d/P1M4/research/findings.md
  why: "THIS milestone's research: the G3 root cause + verified fix, the
        summary-line contract table, the compile flags, the 3 framework styles,
        the per-suite content map, and the runner structure. The PRP's factual
        basis."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1 (COMPLETE) — public API. Reproduce its doc-comment
                       #   EXAMPLES as test assertions. DO NOT TOUCH.
pattern_match.c        # P1.M1+P1.M2+P1.M3 (COMPLETE or finishing in parallel).
                       #   NFA_MAX_PATTERN=2048 host default. DO NOT TOUCH.
notifier.h notifier.c  # P2 (COMPLETE). Links QMK symbols -> NOT host-compilable.
                       #   OUT OF P1.M4 SCOPE. DO NOT TOUCH.
rules.mk               # P2 — DO NOT TOUCH.
test_*.c               # THE 9 DELIVERABLES (live source of truth; rebuild from
                       #   this PRP, applying the G3 fix to test_memory_stress.c).
run_all_tests.sh       # THE RUNNER DELIVERABLE (rebuild from this PRP).
qmk_stubs/  run_notifier_stub_tests.sh  test_notifier_dispatch.c
                       # P2-era notifier-stub tests (RISK-1). NOT part of
                       #   run_all_tests.sh. DO NOT TOUCH / DO NOT WIRE IN.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own.
.gitignore             # DO NOT TOUCH (never add plan/, PRD.md, task files).
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
run_all_tests.sh                    # T4 — builds 9, runs 9, aggregates via grep,
                                    #   perf bench, exits non-zero on any fail.
test_pattern_match.c                # T1 — 376-case main suite (Style A).
test_char_classification.c          # T1 — 179 cases (classes via metachars).
test_metachar_verification.c        # T1 — smoke (Style C, no counts, return 0).
test_word_boundary_basic.c          # T2 — 74 cases (Style A, "Tests run:").
test_word_boundary_integration.c    # T2 — 189 cases (Style A, "Tests run:").
test_comprehensive_integration.c    # T2 — 10 timed categories (-DNOTIFIER_STUB).
test_error_handling.c               # T3 — NULL + malformed-escape survival.
test_memory_stress.c                # T3 — memory stress (G3 FIXED) + recovery.
test_invalid_patterns.c             # T3 — 1008 cases (46 patterns × inputs + explicit).
# (All 9 .c link ONLY pattern_match.c; never notifier.c. All #include "pattern_match.h".)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — the G3 fix. test_memory_stress.c::test_maximum_length_strings()
// currently builds a 40000-char pattern (10000 * "test") -> overflows
// NFA_MAX_PATTERN=2048 -> nfa_compile clamps -> the "^...$" exact-match NFA is
// corrupted -> returns false (expected true) -> FAIL. VERIFIED FIX: cap the
// pattern build loop so the pattern stays <= ~2000 chars (e.g. i < 500), OR use
// a short pattern for the anchored case against the huge STRING. Keep the 50KB
// mallocs + huge-string tests (the memory-stress intent). Do NOT raise
// NFA_MAX_PATTERN (that is a matcher change, P1.M2 scope, and would overflow MCU
// RAM in the QMK build). Do NOT delete the case (it tests exact-match on large
// data) — fix its pattern sizing.

// CRITICAL — summary-line strings are load-bearing. run_all_tests.sh greps:
//   1st: "Total tests run:"  (pattern_match, char_classification, error_handling,
//                              memory_stress, invalid_patterns)
//   2nd: "Tests run:"         (word_boundary_basic, word_boundary_integration)
// A suite printing "Total test cases run:" (comprehensive) is NOT summed (its
// pass/fail is still caught via exit code). metachar prints no summary. Match
// these strings EXACTLY or the runner's aggregation breaks. The exact printf is:
//   printf("Total tests run: %d\n", tests_run);
//   printf("Tests passed: %d\n", tests_passed);
//   printf("Tests failed: %d\n", tests_failed);
//   printf("Success rate: %.1f%%\n", tests_run>0 ? (100.0*tests_passed/tests_run):0.0);

// CRITICAL — every counting suite MUST exit non-zero on failure (the runner's
// ✓/✗ per-suite mark uses $?):
//   if (tests_failed == 0) { printf(...); return 0; }
//   else                   { printf(...); return 1; }
//   // or the terse form: return tests_failed > 0 ? 1 : 0;
// test_metachar_verification is the SOLE exception: it returns 0 always (visual
// smoke, no counts). test_comprehensive_integration returns tests_failed>0?1:0.

// GOTCHA — the classifiers (is_digit_char/is_word_char/is_whitespace_char/
// is_word_boundary) are `static` in pattern_match.c -> UNREACHABLE from a host
// test that links pattern_match.c as a separate TU. test_char_classification
// must exercise them INDIRECTLY via the \d \D \w \W \s \S and \b \B metachars
// through the public pattern_match(). Never declare them extern; never #include
// the .c.

// GOTCHA — link pattern_match.c ONLY. notifier.c #include "QMK_KEYBOARD_H" +
// "raw_hid.h" -> cannot compile on a host. The 9 test binaries are gcc host
// programs; they #include "pattern_match.h" and link pattern_match.c. PRD §3
// "Two compilation contexts."

// GOTCHA — -DNOTIFIER_STUB on test_comprehensive_integration is VESTIGIAL
// (referenced nowhere; PRD §3 / RISK-2). Pass it from run_all_tests.sh exactly
// as PRD §11.1 shows; do NOT #ifdef on it inside the .c.

// GOTCHA — the runner uses `bc -l` for the success-rate percentage; `bc` must
// be present (it is on any dev host). Guard: if total_failed==0 print "100.0"
// directly; else compute via bc. Reproduce the live runner's logic verbatim.

// GOTCHA — `bc` may emit a warning to stderr if locale uses ',' decimal; the
// live runner tolerates this. Do not over-engineer.

// GOTCHA — counts drift. PRD §11.3 "1826 assertions" = the 5 data-table suites
// (376+179+74+189+1008). The runner's aggregate ~2019 adds error_handling +
// memory_stress. Target 0 FAILURES (the §11.2A gate), NOT a fixed integer.

// GOTCHA — the perf_test.c heredoc inside run_all_tests.sh uses patterns with
// no regex classes ("test", "^test", "test*", ...) on input "test" — these are
// the backward-compat micro-bench (PRD §12 "~0.1 us/call"). Assert < 1.0s for
// 100000*7 calls. Reproduce verbatim.

// GOTCHA — embedded NUL bytes in a C string literal ("\x00...") truncate at the
// NUL; you cannot feed a real embedded NUL via a string literal. test_error_
// handling's "null character in pattern" cases therefore test the LITERAL
// backslash-zero source (matching "\0" text), not an actual NUL byte. Keep that
// semantic (it matches the live suite) — do not try to construct a real NUL.
```

## Implementation Blueprint

### Data models and structure

No persistent data models. Each suite has file-local counters
`static int tests_run, tests_passed, tests_failed;` and one of three frameworks:

```c
/* Style A — data-table driven (test_pattern_match, word_boundary_*, error_handling) */
typedef struct {
    const char *pattern;
    const char *input;
    bool case_sensitive;
    bool expected_result;
    const char *description;
} test_case_t;
static void run_test(test_case_t t){           /* increments counters, PASS:/FAIL: */
    tests_run++; bool r = pattern_match(t.pattern, t.input, t.case_sensitive);
    if (r == t.expected_result){ tests_passed++; printf("PASS: %s\n", t.description); }
    else { tests_failed++; printf("FAIL: %s\n", t.description);
           printf("      Pattern: '%s', Input: '%s', Case sensitive: %s\n",
                  t.pattern, t.input, t.case_sensitive ? "true" : "false");
           printf("      Expected: %s, Got: %s\n",
                  t.expected_result ? "true" : "false", r ? "true" : "false"); }
}
/* usage: test_case_t arr[] = { {...}, {...} };
   for (int i=0;i<sizeof(arr)/sizeof(arr[0]);i++) run_test(arr[i]); */

/* Style B — inline helper (memory_stress, invalid_patterns) — same body, no struct */
static void test_pattern(const char *pattern, const char *input,
        bool case_sensitive, bool expected, const char *description){ ...same body... }

/* Style C — boolean smoke (metachar_verification) — no counters */
printf("\\d matches '5': %s\n", pattern_match("\\d","5",true) ? "PASS" : "FAIL");
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: CREATE run_all_tests.sh  (T4 — the contract; implement first)
  - PLACE: repo root. Executable (chmod +x; shebang #!/bin/bash).
  - IMPLEMENT:
      1. Banner ("Pattern Matching Library - Full Test Suite").
      2. "Compiling all test files..." then the 9 gcc lines EXACTLY as PRD §11.1
         (verbatim, incl. -std=c99 -DNOTIFIER_STUB on comprehensive and -I. on
         error_handling/memory_stress/invalid_patterns).
      3. run_test(name, exe): run ./$exe, echo output, if grep "Total tests run:"
         parse N/P/F; elif grep "Tests run:" parse N/P/F; accumulate totals;
         print ✓ ($?==0) / ✗ (non-zero).
      4. run_test for all 9 in PRD §3 order.
      5. Overall summary (Total tests run/passed/failed across all suites;
         success_rate via bc -l; 100.0 if total_failed==0; ✓/✗ verdict).
      6. perf_test.c heredoc (100000 iterations × 7 patterns on "test"; assert
         < 1.0s; print us/match); gcc + run + rm.
      7. exit (total_failed==0 ? 0 : 1).
  - DEPENDENCIES: the 9 .c files (Tasks 2-7). The grep contract (Context §2).
  - DO NOT: add notifier/qmk_stubs tests; use make; change compile flags.

Task 2: CREATE test_pattern_match.c  (T1.S1 — 376 cases, Style A)
  - PLACE: repo root. #include <stdio.h> <stdbool.h> <string.h> "pattern_match.h".
  - IMPLEMENT: the test_case_t struct + run_test (Style A). Category functions
    each holding a test_case_t[]: start_anchor, end_anchor, full_anchor,
    anchors_with_wildcards, escape_sequences (\^ \$ \* \\), case_sensitivity,
    wildcard_only, parsing/edge_cases, metachars_with_anchors, word_boundary_
    escape_processing. ~376 cases total spanning PRD §15 Appendix A.
  - SUMMARY: printf("Total tests run: %d\n", tests_run); Tests passed/failed;
    Success rate %.1f%%.
  - EXIT: if (tests_failed==0){...; return 0;} else {...; return 1;}
  - NAMING: test_case_t; run_test; category funcs test_<topic>().
  - DO NOT: test static functions (impossible across TU); use <ctype.h>.

Task 3: CREATE test_char_classification.c (T1.S2 — 179 cases) +
        test_metachar_verification.c (T1.S2 — smoke)
  - test_char_classification: #include <stdio.h> <stdbool.h> <string.h> <stddef.h>
    "pattern_match.h". 179 cases exercising \d \D \w \W \s \S (incl. \w matches
    '_', \s matches ' '\t'\n'\r'\f'\v') and is_word_boundary positions (NULL,
    pos==0, pos==len, interior XOR) surfaced via \b/\B. Custom inline runner
    printing "PASS: <func> - <desc>". Summary "Total tests run: %d". Exit on fail.
  - test_metachar_verification: #include <stdio.h> <stdbool.h> "pattern_match.h".
    ~25 Style-C lines: \d\D\w\W\s\S smoke + combos + case sensitivity, each
    printing "...: PASS"/"...: FAIL". NO counters; return 0; (always).
  - DO NOT: declare the static classifiers extern; #include the .c.

Task 4: CREATE test_word_boundary_basic.c (T2.S1 — 74) +
        test_word_boundary_integration.c (T2.S1 — 189)  (both Style A)
  - Both: #include <stdio.h> <stdbool.h> <string.h> "pattern_match.h".
    test_case_t + run_test (Style A).
  - basic: \b/\B semantics (word/non-word edges, start/end of string, adjacent
    word chars, adjacent non-word). 74 cases.
  - integration: \b/\B + anchors (^/$), wildcards (*), metachar classes
    (\d\w\s), edge cases, case sensitivity. 189 cases.
  - SUMMARY (BOTH): printf("Tests run: %d\n", tests_run); Tests passed/failed;
    Success rate %.1f%%.  <-- "Tests run:" NOT "Total tests run:" (grep elif).
  - EXIT (BOTH): return tests_failed > 0 ? 1 : 0;
  - DO NOT: print "Total tests run:" (would be caught by the 1st grep branch and
    double-shape the aggregation — match the live format exactly).

Task 5: CREATE test_comprehensive_integration.c (T2.S2 — 10 timed categories)
  - #include <stdio.h> <stdbool.h> <string.h> <assert.h> <time.h> <stdlib.h>
    "pattern_match.h".
  - IMPLEMENT: run_test_with_perf(name, bool (*fn)(void)) timing each category
    with clock(); tracks tests_run/passed/failed (CATEGORIES), memory_operations
    (count of pattern_match calls), total_time. 10 categories combining multiple
    features (anchors+wildcards+classes, escapes+anchors, \b+classes, performance
    category asserting pathological-ish patterns are fast, memory-management
    category running many ops).
  - SUMMARY: printf("Total test categories run: %d\n", tests_run); Test categories
    passed/failed; "Total pattern_match operations: %d"; avg/total time;
    "Success rate: %.1f%%".  <-- "Total test categories run:" (NOT aggregated).
  - EXIT: return tests_failed > 0 ? 1 : 0;  (int main(void))
  - COMPILE: -std=c99 -DNOTIFIER_STUB (vestigial macro; do not #ifdef on it).
  - DO NOT: print "Total tests run:" or "Tests run:".

Task 6: CREATE test_error_handling.c (T3.S1) + test_memory_stress.c (T3.S1, G3 FIX)
  - test_error_handling: #include <stdio.h> <stdbool.h> <string.h> <stdlib.h>
    <limits.h> "pattern_match.h". Style A (test_case_t + run_test) for the
    malformed-escape table PLUS inline NULL tests:
        pattern_match(NULL, "test", true)  -> assert false
        pattern_match("test", NULL, true)  -> assert false
        pattern_match(NULL, NULL,   true)  -> assert false
    (each: tests_run++; if false {passed; "PASS: NULL ... returns false"} else
    {failed; "FAIL: ..."}). Covers unknown escapes kept literal (\x \z \1 \9 \@
    \# \% \&), trailing/odd backslashes, escaped literals. Summary
    "Total tests run: %d". Exit on fail.
  - test_memory_stress: #include <stdio.h> <stdbool.h> <string.h> <stdlib.h>
    <limits.h> "pattern_match.h". Style B (test_pattern inline helper). Categories:
    memory_stress (long escaped/metachar patterns via malloc'd buffers),
    memory_edge_cases, pathological_patterns (wildcards, X+, .* combos),
    error_recovery, maximum_length_strings. *** G3 FIX (Context §1) ***:
    in test_maximum_length_strings() cap the huge_pattern build loop so the
    processed pattern is <= ~2000 chars (within NFA_MAX_PATTERN=2048), e.g.
    `for (int i = 0; i < 500; i++) strcat(huge_pattern, "test");` (2000 chars);
    KEEP max_test_size=50000 mallocs and the huge-STRING tests. The
    "Anchored huge pattern exact match" case then passes (expected=true). Summary
    "Total tests run: %d". Exit on fail.
  - DO NOT: raise NFA_MAX_PATTERN (matcher change); delete the G3 case; leak the
    malloc'd buffers (free them — the suites are ASan-cleaned at Level 3).

Task 7: CREATE test_invalid_patterns.c (T3.S2 — 1008 cases)
  - #include <stdio.h> <stdbool.h> <string.h> <stdlib.h> "pattern_match.h".
    Style B (test_pattern inline helper).
  - IMPLEMENT: a const char* problematic_patterns[] table of 46 pathological
    patterns (unmatched `[`/`]`/`(`/`)`, `?` `{n}` `{n,m}` quantifiers, invalid
    escapes, `^^` `$$` `**` `***`, `|` `a|b`, `\[...\]` `\(...\)`) and a
    const char* test_inputs[] table; the cross product
    (pattern_count × input_count ≈ 920 cases) + ~88 explicit invalid-construct
    cases (all invalid constructs treated as LITERALS — expected=true for
    literal-vs-literal). Total ~1008.
  - SUMMARY: printf("Total tests run: %d\n", tests_run); Tests passed/failed;
    Success rate %.1f%%.
  - EXIT: if (tests_failed==0){...; return 0;} else {...; return 1;}
  - DO NOT: expect brackets/parens/alternation to be implemented (they are
    literals — PRD §15 construct list has no `[` `(` `|`); assert accordingly.
```

### Implementation Patterns & Key Details

```c
// PATTERN: every counting suite ends with the same 4-line summary + exit-on-fail.
//   printf("Total tests run: %d\n", tests_run);   // or "Tests run:" / "Total test categories run:"
//   printf("Tests passed: %d\n", tests_passed);
//   printf("Tests failed: %d\n", tests_failed);
//   printf("Success rate: %.1f%%\n", tests_run>0 ? 100.0*tests_passed/tests_run : 0.0);
//   return tests_failed > 0 ? 1 : 0;

// PATTERN: data-table suites iterate with sizeof. Prefer literal compound init:
//   test_case_t tests[] = { {"^a","ab",true,true,"..."}, {...} };
//   for (int i=0; i < (int)(sizeof(tests)/sizeof(tests[0])); i++) run_test(tests[i]);

// PATTERN: seed assertions from PRD §15 Appendix A (verified truth table) and
//   pattern_match.h doc-comment EXAMPLES. e.g. ^hello$ vs "hello world" -> false;
//   * vs "a\nb" -> true (glob matches newline); a.b vs "a\nb" -> false (dot excludes).

// PATTERN: test the NULL contract directly (PRD §6 @note). Do not assume; assert:
//   assert(pattern_match(NULL,"x",true)==false);  // or the tests_run++ inline form

// PATTERN: realistic stress = short pattern + huge STRING (NFA is O(states*strlen)).
//   Keep patterns <= NFA_MAX_PATTERN (2048). The huge STRING is the stress.

// ANTI-PATTERN: do NOT reproduce the G3 bug. The live test_memory_stress.c uses a
//   40000-char pattern that fails. Cap it (~2000 chars) in the rebuild.

// ANTI-PATTERN: do NOT print a different summary string. "Tests run:" (basic/
//   integration) vs "Total tests run:" (5 others) vs "Total test categories run:"
//   (comprehensive) vs none (metachar) — each is load-bearing for the runner's
//   grep/elif and the aggregate-vs-exclude behavior.

// ANTI-PATTERN: do NOT link notifier.c or add qmk_stubs tests. notifier.c pulls
//   QMK_KEYBOARD_H; the 9 suites are host gcc programs linking pattern_match.c only.

// ANTI-PATTERN: do NOT try to unit-test the static classifiers directly. They are
//   file-local; reach them INDIRECTLY via \d \w \s \b through pattern_match().

// ANTI-PATTERN: do NOT change the §11.1 gcc flags (e.g. dropping -I. or
//   -DNOTIFIER_STUB "because they're unnecessary"). Reproduce them verbatim; the
//   gate compares against the canonical command set.
```

### Integration Points

```yaml
BUILD:
  - 9 host gcc compilations (PRD §11.1 exact flags) + run_all_tests.sh runner.
  - No make, no CMake, no QMK. Plain gcc on any host.
  - All 9 .c #include "pattern_match.h" and link pattern_match.c only.

CONSUMERS (upstream, NOT this milestone):
  - The 9 suites call ONLY: bool pattern_match(const char*, const char*, bool)
    (P1.M1 public API). Nothing else. The static helpers (classifiers, NFA) are
    reached transitively through pattern_match().
  - run_all_tests.sh consumes the suites' stdout (grep) + exit codes.

CROSS-MILESTONE CONTRACTS (fixed upstream; do not change):
  - pattern_match signature: (const char *pattern, const char *str, bool case_sensitive) -> bool
  - pattern_match(NULL, _, _) -> false;  pattern_match(_, NULL, _) -> false.  (PRD §6 @note)
  - NFA_MAX_PATTERN = 2048 host default (P1.M2). Patterns must stay under it.

CONFIG / DATABASE / ROUTES:
  - N/A (host test harness; no runtime firmware effect).
  - Do NOT modify .gitignore (never add plan/, PRD.md, or task files).

OUT OF SCOPE (do NOT touch):
  - pattern_match.{c,h}, notifier.{c,h}, rules.mk (matcher/firmware — other milestones).
  - qmk_stubs/, run_notifier_stub_tests.sh, test_notifier_dispatch.c (P2 notifier
    coverage; RISK-1; NOT part of run_all_tests.sh).
  - PRD.md, tasks.json, prd_snapshot.md, .gitignore.
```

## Validation Loop

> C host-test harness — use gcc + the runner. All commands verified against the
> live repo during research. The §11.2A gate (per-suite `fails=0`) is authoritative.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Each test file compiles cleanly with -Wall (zero warnings).
for f in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  flags=""
  [ "$f" = "test_comprehensive_integration" ] && flags="-std=c99 -DNOTIFIER_STUB"
  case "$f" in test_error_handling|test_memory_stress|test_invalid_patterns) flags="-I.";; esac
  gcc -Wall $flags -c $f.c -o /tmp/$f.o && echo "ok  $f" || echo "FAIL $f"
done
# Expected: "ok  <suite>" for all 9; ZERO warnings on stderr.

# 1b. The §11.1 link lines succeed (each produces an executable).
gcc -o test_pattern_match             test_pattern_match.c             pattern_match.c
gcc -o test_char_classification       test_char_classification.c       pattern_match.c
gcc -o test_word_boundary_basic       test_word_boundary_basic.c       pattern_match.c
gcc -o test_word_boundary_integration test_word_boundary_integration.c pattern_match.c
gcc -o test_metachar_verification     test_metachar_verification.c     pattern_match.c
gcc -o test_comprehensive_integration test_comprehensive_integration.c pattern_match.c -std=c99 -DNOTIFIER_STUB
gcc -o test_error_handling            test_error_handling.c            pattern_match.c -I.
gcc -o test_memory_stress             test_memory_stress.c             pattern_match.c -I.
gcc -o test_invalid_patterns          test_invalid_patterns.c          pattern_match.c -I.
# Expected: 9 executables; exit 0 each.

# 1c. run_all_tests.sh is executable and has the shebang + 9 gcc lines.
head -1 run_all_tests.sh                       # -> #!/bin/bash
grep -c '^gcc -o test_' run_all_tests.sh       # -> 9

# 1d. Summary-line contract honored (grep targets exist).
grep -l 'printf("Total tests run:' test_pattern_match.c test_char_classification.c test_error_handling.c test_memory_stress.c test_invalid_patterns.c | wc -l   # -> 5
grep -l 'printf("Tests run:' test_word_boundary_basic.c test_word_boundary_integration.c | wc -l   # -> 2
grep -q 'printf("Total test categories run:' test_comprehensive_integration.c && echo "comprehensive ok"
# test_metachar_verification.c: NO summary line (smoke).

rm -f /tmp/*.o
```

### Level 2: Component Tests (per suite, as created)

```bash
cd /home/dustin/projects/qmk-notifier

# Run each suite; each must print its summary with Tests failed: 0 and exit 0.
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_comprehensive_integration \
         test_error_handling test_memory_stress test_invalid_patterns; do
  out=$(./$t 2>&1); rc=$?
  fails=$(printf '%s' "$out" | grep -c '^FAIL:')
  summ=$(printf '%s' "$out" | grep -E 'Tests failed:' | tail -1)
  printf '%-34s rc=%s fails=%s  %s\n' "$t" "$rc" "$fails" "$summ"
done
./test_metachar_verification 2>&1 | grep -c 'FAIL'   # expect 0 (smoke; lines say PASS/FAIL)
# Expected: rc=0 and fails=0 for EVERY suite; metachar shows 0 FAIL lines.

# G3 specifically: test_memory_stress must report 0 failures (the case passes).
./test_memory_stress 2>&1 | grep -E 'Anchored huge pattern exact match' | grep -q PASS \
  && echo "G3 fixed (ok)" || echo "FAIL: G3 still failing"
```

### Level 3: Integration & Acceptance (the full gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 3A. *** THE ACCEPTANCE GATE — PRD §11.2A: fails=0 for every suite. ***
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  printf '%-36s fails=%s\n' "$t" "$(./$t 2>&1 | grep -c '^FAIL:')"
done
# Expected: fails=0 on EVERY line (all 9). Any non-zero is a regression — fix it
# (in the test, if the expected value is wrong; in the matcher otherwise — but the
# matcher is P1.M1-M3 and presumed correct, so first suspect the test's expected).

# 3B. The full runner: rebuilds all 9, runs them, aggregates, perf bench.
./run_all_tests.sh 2>&1 | tail -25
# Expected: "Total tests failed: 0" + "ALL TESTS PASSED" + exit 0.
./run_all_tests.sh >/dev/null 2>&1; echo "runner exit=$?"
# Expected: runner exit=0.

# 3C. *** Acceptance gate §11.2B — pathological NFA must finish < 50 ms. ***
# (The runner may emit this; also verify standalone.)
cat > /tmp/nfa_stress.c <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){
  char s[200]; for(int i=0;i<199;i++) s[i]='a'; s[199]='\0';
  const char* p="a+a+a+a+a+a+a+a+a+a+b";
  clock_t t=clock(); int r=pattern_match(p,s,1);
  printf("result=%d  %.1f us\n", r, 1e6*(double)(clock()-t)/CLOCKS_PER_SEC);
  return 0;
}
EOF
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress
timeout 5 /tmp/nfa_stress   # MUST print result=0 in < 50 ms (live: ~1.8 ms)
rm -f /tmp/nfa_stress.c /tmp/nfa_stress

# 3D. §11.2C realistic patterns (note: the ^\w+@\w+$ vs user_host case is a PRD
# doc error — correctly 0, not 1; the runner/per PRD says "six 1s" but #3 is a
# known doc drift per findings_and_risks.md DRIFT-2. Verify the matcher, do NOT
# force a wrong expectation into a test.)
cat > /tmp/nfa_real.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));                          /* 1 */
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1));/* 1 */
  printf("%d\n", pattern_match("v\\.code","v.code",1));                     /* 1 */
  printf("%d\n", pattern_match("a+b","aaab",1));                            /* 1 */
  printf("%d\n", pattern_match("*slack*","Slack - general",0));             /* 1 */
  return 0;
}
EOF
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real
# Expected: five 1s (the @\w+$ vs user_host case is a doc error — correctly 0).
rm -f /tmp/nfa_real.c /tmp/nfa_real

# 3E. AddressSanitizer — no crash / no leak on the robustness + memory suites.
gcc -O1 -g -fsanitize=address,undefined -w pattern_match.c test_error_handling.c -I. -o /tmp/eh_asan
/tmp/eh_asan >/dev/null && echo "error_handling ASan: clean"
gcc -O1 -g -fsanitize=address,undefined -w pattern_match.c test_memory_stress.c -I. -o /tmp/ms_asan
/tmp/ms_asan >/dev/null && echo "memory_stress ASan: clean (incl. G3-fixed case)"
rm -f /tmp/eh_asan /tmp/ms_asan
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Backward-compat micro-benchmark (PRD §12 "~0.1 us/call"). The runner already
# embeds perf_test.c; here an independent check.
gcc -O2 -w pattern_match.c <(cat <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){ const char*p="*chrome*"; clock_t t=clock();
  for(int i=0;i<100000;i++) pattern_match(p,"Google Chrome",0);
  printf("%.3f us/call\n",1e6*(double)(clock()-t)/CLOCKS_PER_SEC/100000); return 0; }
EOF
) -I. -o /tmp/pm_bench && /tmp/pm_bench   # expect sub-microsecond (live: ~0.1)
rm -f /tmp/pm_bench

# PRD §10.2 reference-keymap parity smoke (matcher behavior the suites encode).
cat > /tmp/keymap_smoke.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("neovide","neovide",0));                 /* 1 */
  printf("%d\n", pattern_match("*chrome*","Google Chrome",0));          /* 1 */
  printf("%d\n", pattern_match("cs2","CS2",0));                         /* 1 ci */
  printf("%d\n", pattern_match("Counter-Strike 2","counter-strike 2",0));/* 1 ci */
  return 0;
}
EOF
gcc -w /tmp/keymap_smoke.c pattern_match.c -I. -o /tmp/keymap_smoke && /tmp/keymap_smoke
# Expected: four 1s.
rm -f /tmp/keymap_smoke.c /tmp/keymap_smoke

# Doc-contract: each suite carries a one-line header comment naming its PRD §11.3
# role; run_all_tests.sh references "Task 18: Final backward compatibility verification".
grep -q 'backward compatibility\|acceptance' run_all_tests.sh && echo "runner banner (ok)"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: all 9 `.c` compile with `gcc -Wall` → zero warnings; the 9 §11.1
      link lines produce executables; `run_all_tests.sh` is executable with 9 gcc lines.
- [ ] Level 1: summary-line contract honored (5×`"Total tests run:"`,
      2×`"Tests run:"`, 1×`"Total test categories run:"`, metachar none).
- [ ] Level 2: every suite exits 0 and prints `Tests failed: 0`; metachar shows
      0 `FAIL` lines.
- [ ] Level 2: **G3 fixed** — `test_memory_stress` "Anchored huge pattern exact
      match" prints PASS (pattern within `NFA_MAX_PATTERN=2048`).
- [ ] Level 3A: §11.2A gate → `fails=0` for **all 9** suites.
- [ ] Level 3B: `./run_all_tests.sh` → `Total tests failed: 0`, `ALL TESTS PASSED`,
      exit 0.
- [ ] Level 3C: pathological NFA prints `result=0` in < 50 ms.
- [ ] Level 3D: §11.2C prints five `1`s (the `@\w+$` case is a PRD doc error).
- [ ] Level 3E: ASan/UBSan clean on error_handling + memory_stress (no crash/leak).

### Feature Validation

- [ ] `test_pattern_match` has the ~376 anchor/escape/wildcard/case/parsing cases.
- [ ] `test_char_classification` exercises classes INDIRECTLY via `\d\w\s` + `\b`
      (no direct static-classifier access).
- [ ] `test_word_boundary_basic` (74) + `_integration` (189) cover `\b`/`\B`.
- [ ] `test_metachar_verification` is a Style-C smoke (no counts, return 0).
- [ ] `test_comprehensive_integration` has 10 timed categories + memory-op count.
- [ ] `test_error_handling` directly asserts the NULL contract (`false`) + escapes.
- [ ] `test_memory_stress` stresses memory with NO crash and **G3 passing**.
- [ ] `test_invalid_patterns` has the 46-pattern × input cross product (~1008).
- [ ] `run_all_tests.sh` aggregates via grep, runs the perf bench, exits on failure.

### Code Quality Validation

- [ ] Matches the live source-of-truth framework styles branch-for-branch (PRD §17).
- [ ] All 9 `.c` link ONLY `pattern_match.c`; all `#include "pattern_match.h"`.
- [ ] No file other than the 10 deliverables is modified (esp. not
      `pattern_match.{c,h}`, `notifier.{c,h}`, `rules.mk`, `PRD.md`,
      `tasks.json`, `prd_snapshot.md`, `.gitignore`, `qmk_stubs/`).
- [ ] Counts target 0 FAILURES (the §11.2A gate), not a fixed integer (counts drift).
- [ ] malloc'd buffers in memory_stress/error_handling are freed (ASan-clean).

### Documentation & Deployment

- [ ] Each suite has a one-line header comment naming its PRD §11.3 role.
- [ ] `run_all_tests.sh` banner references the acceptance/backward-compat gate.
- [ ] No new env vars / config / build-system files (plain gcc + bash + bc).

---

## Anti-Patterns to Avoid

- ❌ Don't reproduce the G3 bug. The live `test_memory_stress.c` uses a 40000-char
  pattern (10000×"test") that overflows `NFA_MAX_PATTERN=2048` and fails. Cap the
  pattern loop (~500×"test" = 2000 chars) in the rebuild; keep the huge STRINGs.
- ❌ Don't print a summary string other than the one the runner greps. 5 suites
  print `"Total tests run:"`, 2 print `"Tests run:"`, comprehensive prints
  `"Total test categories run:"`, metachar prints none — each is load-bearing.
- ❌ Don't return 0 on failure from a counting suite. The runner marks ✓/✗ per
  suite via `$?`; every counting suite must exit non-zero if `tests_failed>0`.
  (metachar is the sole exception — always 0.)
- ❌ Don't link `notifier.c` or add the `qmk_stubs/` notifier tests to
  `run_all_tests.sh`. `notifier.c` pulls `QMK_KEYBOARD_H` (not host-compilable);
  the 9 suites are the `pattern_match.c`-only harness per PRD §11 (RISK-1).
- ❌ Don't unit-test the `static` classifiers/NFA directly. They are file-local;
  reach them INDIRECTLY via `\d \w \s \b` through the public `pattern_match()`.
- ❌ Don't change the §11.1 gcc flags (dropping `-I.` or `-DNOTIFIER_STUB`
  "because they're unnecessary"). Reproduce them verbatim — the gate compares
  against the canonical command set, and `-DNOTIFIER_STUB` is vestigial-but-required.
- ❌ Don't `#ifdef` on `NOTIFIER_STUB` inside `test_comprehensive_integration.c`.
  It is passed from the runner, referenced nowhere (RISK-2), and harmless.
- ❌ Don't expect regex features the matcher lacks (`[...]`, `(...)`, `|`, `{n}`).
  They are treated as literals (PRD §15) — assert literal-vs-literal (`expected=true`).
- ❌ Don't try to feed a real embedded NUL via a C string literal (`"\x00..."`
  truncates). The error-handling "null character" cases test the literal source
  text `"\0"`, matching the live suite — keep that semantic.
- ❌ Don't modify `pattern_match.c` to "fix" a failing test. PRD §13 #12: if a
  test flips red, the matcher is wrong (fix the matcher, in its own milestone) —
  BUT for P1.M4 the matcher is presumed complete (P1.M1-M3); the only legitimate
  P1.M4 edit to make a test pass is the G3 pattern-sizing fix in the test itself.

---

## Confidence Score

**9/10** — One-pass success is highly likely. The live repo is the PRD §17 source
of truth: all 9 `test_*.c` and `run_all_tests.sh` exist and (bar the single G3
case) pass; every structural fact — the 3 framework styles, the exact summary-line
strings, the §11.1 gcc flags, the per-suite counts, the runner's grep/elif
contract — was read from the live code and verified empirically during research.
The G3 root cause (40000-char pattern overflows `NFA_MAX_PATTERN=2048`) and its
verified fix (cap the pattern at ~2000 chars, keep the huge strings) are encoded
as a hard requirement gated by Level 2/3 checks. The parallel-execution contract
(P1.M3 delivers a complete, 2018/2019-green matcher) is explicitly assumed. The
only residual risk is an implementer mis-copying a summary-line string or
reproducing the G3 oversized-pattern bug — both are encoded as hard requirements
with Level 1 grep gates.
