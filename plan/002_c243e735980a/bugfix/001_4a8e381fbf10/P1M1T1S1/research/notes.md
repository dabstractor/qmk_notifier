# Research Notes — P1.M1.T1.S1 (@-literal regression test cases)

## Task
Add 4 regression test cases to `test_pattern_match.c` that lock in the CORRECT
semantics of the `@`-literal in `^\w+@\w+$` patterns, guarding against a future
"rebuild to spec" regressing the matcher toward the (wrong) PRD §11.2C
expectation. Test-only change. DO NOT modify pattern_match.c.

## Root-cause context (from bugfix architecture)
- Bugfix plan `001_4a8e381fbf10`, Issue 1 (Major). The PRD §11.2C acceptance
  snippet's third line used `"user_host"` but the pattern `^\w+@\w+$` requires a
  literal `@`. `@` is an ordinary literal byte (PRD §7.1/§7.7); `\w`=
  `[A-Za-z0-9_]` (PRD §15) does NOT include `@`. So the matcher correctly
  returns 0 for `user_host`. The PRD string was a typo for `user@host`.
- The PRD §11.2C doc was ALREADY corrected (PRD.md:1261 now uses "user@host").
- The matcher is correct; this task adds the regression test (the actionable
  code-pipeline fix) so the correct behavior is locked in.

## Current test file state (test_pattern_match.c, 843 lines)
- Data-driven design: `test_case_t` struct (lines 8-14):
  `{ const char *pattern; const char *input; bool case_sensitive;
     bool expected_result; const char *description; }`
- `run_test(test_case_t)` helper (lines 25-36): increments `tests_run`, calls
  `pattern_match(...)`, compares to `expected_result`, bumps passed/failed.
- Section functions each declare a `test_case_t <name>_tests[]` array + a
  `for` loop calling `run_test()`. 17 section functions total.
- `run_pattern_match_tests()` (the dispatcher, ~line 786) resets counters and
  calls every section function. `main()` calls it, returns non-zero on failure.
- **Current total: 376 tests, all passing** (verified by running the binary).

## Best insertion point (CONFIRMED)
`test_metacharacters_with_anchors()` (lines 491-567). It already contains a
`\w with anchors` subsection and a trailing "Mixed literal and metacharacter
with anchors" block. The array `metachar_anchor_tests[]` closes at line ~566:
```c
        {"^x\\sy$", "x y", true, true, "Mixed literal + metachar + literal with full anchor"},
    };
```
→ Insert the 4 new cases (with a `// @-literal regression ...` comment block)
immediately BEFORE that closing `};`. This keeps anchored-`\w`+literal cases
together and requires NO dispatcher change — `test_metacharacters_with_anchors()`
is already called from `run_pattern_match_tests()` (line ~801), and the existing
`for` loop + `run_test()` counter machinery picks up the new rows automatically.

## The 4 cases (exact C source — verified expected_result values)
EMPIRICALLY VERIFIED against pattern_match.c (built + ran a probe):
1. {"^\\w+@\\w+$", "user@host", true, TRUE,  "...matches user@host..."}   → matcher=1 ✓
2. {"^\\w+@\\w+$", "user_host", true, FALSE, "...NOT match user_host..."} → matcher=0 ✓  ← CRITICAL guard
3. {"^\\w+_\\w+$", "user_host", true, TRUE,  "...matches user_host..."}   → matcher=1 ✓
4. {"\\w+@\\w+",   "user@host", true, TRUE,  "...substring..."}           → matcher=1 ✓

C-string escaping notes:
- `"^\\w+@\\w+$"` → actual pattern string `^\w+@\w+$` (`\\w`→`\w`; `+` is the
  one-or-more quantifier, NOT escaped; `@` is an ordinary literal).
- Inputs `"user@host"` / `"user_host"` need no escaping.
- Matches existing file style (e.g. `{"^\\w$", "_", ...}`, `{"^x\\sy$", ...}`).

## Toolchain / validation
- gcc, no make/cmake. The project's ACTUAL build command (used by run_all_tests.sh)
  is PLAIN gcc with NO -Wall/-Wextra:
  `gcc -o test_pattern_match test_pattern_match.c pattern_match.c && ./test_pattern_match`
- GOTCHA (verified): compiling the EXISTING file with -Wextra floods ~16
  pre-existing `-Wsign-compare` warnings from the file's idiom
  `for (int i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)`. These are NOT caused by
  this task and are out of scope. Judge success by exit code 0 + the Level 2
  totals, NOT a warning-free -Wextra build. Do not "fix" the for-loops.
- Expect after change: "Total tests run: 380", "Tests failed: 0", exit 0.
- Aggregate: `run_all_tests.sh` recompiles test_pattern_match.c automatically
  (no script change); expect aggregate 0 failures. Other suites' totals unaffected.
- Item says "380 tests (376 existing + 4 new)". Confirmed arithmetic + VERIFIED
  end-to-end on a temp copy (380/380, critical guard PASS, all 4 REG rows PASS).

## Scope boundaries
- Modify ONLY test_pattern_match.c (add 4 rows + a comment block in ONE section).
- DO NOT touch pattern_match.c (the matcher is correct — Issue 1 root cause).
- DO NOT touch run_all_tests.sh (auto-recompiles; no change needed).
- DO NOT touch PRD.md, tasks.json, prd_snapshot.md, README.md, other test_*.c.
- README test-count sync is a SEPARATE task (P1.M3.T2.S1, Mode B) — not this one.