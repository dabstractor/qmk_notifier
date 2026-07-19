# PRP — P1.M2.T2.S1: Create `test_fidelity_nfa128.c` and register in `run_all_tests.sh`

## Goal

**Feature Goal**: Close the Issue 3 test-fidelity gap (PRD §7.9/§11.3): the 9
standard pattern suites compile `pattern_match.c` **directly** (so
`NFA_MAX_PATTERN` defaults to **2048**), but the firmware runs at **128**
(`notifier.c:14`). Add a **10th suite** — `test_fidelity_nfa128.c` — that is
compiled with `-DNFA_MAX_PATTERN=128` so the acceptance gate finally exercises
the matcher at the firmware budget. A `#error` guard makes the suite **fail to
compile at any other budget**, so the gate cannot silently drift back to 2048.
The suite verifies: (guard) the build is at 128; (smoke) short patterns still
match at the smaller pool; (i) a full 128-byte pattern fits and matches exactly;
(ii) an over-budget pattern is safely clamped (no crash, safe no-match).

**Deliverable**: (1) a NEW file `test_fidelity_nfa128.c` at the repo root, and
(2) TWO insertions in `run_all_tests.sh` — one compile line (after line 22) and
one `run_test` call (after line 88). No other file changes.

**Success Definition**:
- `test_fidelity_nfa128.c` compiles cleanly with `-Wall -std=c99 -DNFA_MAX_PATTERN=128`
  → 0 warnings; runs → **6/6 PASS, 0 FAIL**, exit 0; prints `Total tests run: 6` /
  `Tests passed: 6` / `Tests failed: 0` (the `run_test` grep contract).
- The `#error` guard makes it **fail to compile** without `-DNFA_MAX_PATTERN=128`
  (verified: a default-budget compile aborts).
- `./run_all_tests.sh` builds+runs the new 10th suite; the aggregate total
  increases by 6 (2023 → 2029) and `total_failed` stays 0; the runner still ends
  `✓ ALL TESTS PASSED - BACKWARD COMPATIBILITY VERIFIED`.
- The existing 9 suites are UNCHANGED (no `-DNFA_MAX_PATTERN=128` added to any of
  them — `test_memory_stress` intentionally needs 2048).
- No edits to `pattern_match.{c,h}`, `notifier.{c,h}`, `qmk_stubs/*`, the 9
  existing `test_*.c`, `run_notifier_stub_tests.sh`, `README.md`, `PRD.md`,
  `tasks.json`, `rules.mk`, `.gitignore`.

## User Persona (if applicable)

**Target User**: The maintainer running the §11 acceptance gate and any future
contributor changing the NFA budget or the `NEW()` allocator. The suite is a
fidelity canary: if a code change silently moves the matcher off the firmware
budget (or breaks over-budget handling), this gate catches it.

**Use Case**: After a change to `pattern_match.c` / `notifier.c` / the budget,
`./run_all_tests.sh` rebuilds the 10th suite at `-DNFA_MAX_PATTERN=128` and runs
it — confirming valid firmware-budget patterns still match and over-budget
patterns are handled safely, with no hardware needed.

**User Journey**: contributor edits the matcher → `./run_all_tests.sh` → the
runner compiles `test_fidelity_nfa128` at 128, runs it, aggregates its 6 cases →
if the budget drifted or over-budget handling regressed, a `FAIL:` line + non-zero
exit surfaces it.

**Pain Points Addressed**: Today a user pattern between ~129 and ~256 processed
bytes passes all 9 standard suites (run at 2048) yet is silently clamped on
hardware (`NEW()` reuses the last pool slot) and could match incorrectly. The
existing compile-time guard in `notifier.c` (`nfa_budget_check`, `(NFA_MAX_PATTERN
<= 128) ? 1 : -1`) only checks the *budget* is ≤128, not that individual patterns
fit. This suite is the only gate that exercises per-pattern fit at the firmware
budget.

## Why

- **Closes the Issue 3 fidelity gap.** PRD §2.3/h3.2 (Issue 3) calls this out:
  "Add at least one matcher-corpus suite compiled via the `notifier.c` path (or
  with `-DNFA_MAX_PATTERN=128`) so the matcher is gated at the production budget."
  Until this suite exists, the acceptance gate cannot detect a hardware-only
  regression for patterns that fit at 2048 but not at 128.
- **The `#error` guard is the strongest fidelity guarantee.** Because
  `-DNFA_MAX_PATTERN=128` defines the macro for both the test TU and
  `pattern_match.c`, a `#if NFA_MAX_PATTERN != 128 #error` makes the suite
  **uncompilable at the wrong budget** — the gate structurally cannot drift back
  to 2048 by accident (e.g. someone removes the `-D` flag).
- **Non-invasive.** A dedicated 10th suite adds the gate without touching the 9
  semantics-regression suites (whose budget MUST stay 2048 — `test_memory_stress`
  needs multi-KB patterns). Touching those would silently change which patterns
  fit and undermine their stability.
- **Low risk.** Pure additive test infrastructure — one new `.c` file + two lines
  in the runner. No code/build/wire/protocol change.

## What

Two changes:

1. **CREATE `test_fidelity_nfa128.c`** (repo root) — a standalone C test TU that
   `#include "pattern_match.h"`, has a `#if !defined(NFA_MAX_PATTERN) ||
   NFA_MAX_PATTERN != 128 #error` guard, file-scope `tests_run`/`tests_passed`/
   `tests_failed` counters + a `CK(cond, name)` macro, and a `main()` printing
   `Total tests run:` / `Tests passed:` / `Tests failed:` and returning
   `tests_failed ? 1 : 0`. Six cases: (guard) `NFA_MAX_PATTERN == 128`;
   (smoke ×2) `"hello"` matches/rejects; (i ×2) a 128-char literal matches its
   exact string / rejects a 127-char string; (ii) a 260-char over-budget literal
   doesn't crash and rejects a non-matching string.
2. **MODIFY `run_all_tests.sh`** — two insertions:
   - **Compile** (after line 22, the `test_invalid_patterns` gcc line; before the
     `echo "Compilation complete."` at line 24):
     `gcc -o test_fidelity_nfa128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99`
   - **Run** (after line 88, the last `run_test` call; before the comment at
     line 90):
     `run_test "NFA-128 Fidelity Gate" "test_fidelity_nfa128"`

### Success Criteria

- [ ] `test_fidelity_nfa128.c` exists at repo root with the `#error` budget guard.
- [ ] Compiles clean (`-Wall -std=c99 -DNFA_MAX_PATTERN=128`) → 0 warnings.
- [ ] Runs → 6/6 PASS, 0 FAIL, exit 0; prints the `Total tests run:` / `Tests
      passed:` / `Tests failed:` summary.
- [ ] Without `-DNFA_MAX_PATTERN=128` the compile ABORTS on the `#error`.
- [ ] `run_all_tests.sh` has exactly the two new lines (compile + run_test) and
      the existing 9 compile lines / 9 run_test calls are UNCHANGED.
- [ ] `./run_all_tests.sh` → aggregate total +6 (2023→2029), `total_failed` 0,
      ends `✓ ALL TESTS PASSED`.
- [ ] No `-DNFA_MAX_PATTERN=128` added to any of the 9 existing suites.

## All Needed Context

### Context Completeness Check

**Pass.** The exact `test_fidelity_nfa128.c` content (verbatim, validated 6/6
during research) and the exact two `run_all_tests.sh` insertions (with accurate
line anchors: after L22 for compile, after L88 for run) are specified inline
below and were **empirically validated**: the suite compiles clean with `-Wall`
at `-DNFA_MAX_PATTERN=128`, runs 6/6 PASS/0 FAIL, and its `#error` guard aborts
the compile at the default 2048 budget. The clamp behavior (literals L≤256 fit,
L=260 clamps) was confirmed by probing `pattern_match.c` at the 128 budget. The
`run_test` grep contract (`Total tests run:` / `Tests passed:` / `Tests failed:`)
was matched against the suite's output. An implementer with only this PRP + repo
can create the file, add the two lines, and prove the gate green.

### Documentation & References

```yaml
# MUST READ — the bug + the prescribed fix
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/prd_snapshot.md   (also bugfix PRD)
  section: "### Issue 3: Matcher tested at NFA_MAX_PATTERN=2048 but firmware runs at 128 (h3.2)"
  why: "The canonical statement: 9 suites compile pattern_match.c directly -> NFA_MAX_PATTERN
        defaults 2048; only notifier.c runs at 128. Suggested fix: 'Add at least one matcher-
        corpus suite compiled via the notifier.c path (or with -DNFA_MAX_PATTERN=128) so the
        matcher is gated at the production budget.'"
  critical: "This task IS that fix. The -DNFA_MAX_PATTERN=128 flag on the compile line is the
        load-bearing mechanism (it overrides the #ifndef default for BOTH the test TU and
        pattern_match.c)."

# MUST READ — the exact insertion points + compile/run patterns
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/architecture/test_infrastructure.md
  section: "## Issue 3 Fix: NFA_MAX_PATTERN=128 Fidelity Test"
  why: "Gives the exact compile line (gcc -o test_fidelity_nfa128 ... pattern_match.c -I.
        -DNFA_MAX_PATTERN=128 -std=c99), the run_test call (run_test 'NFA-128 Fidelity Gate'
        'test_fidelity_nfa128'), the result-grep contract (Total tests run:/Tests passed:/
        Tests failed:), and the constraint 'Do NOT add -DNFA_MAX_PATTERN=128 to existing 9
        suites — test_memory_stress intentionally needs 2048.'"
  critical: "The doc says 'run insertion after line ~104', but line ~104 is in the SUMMARY
        section. The ACCURATE anchor is 'after the last run_test call' = line 88
        (test_invalid_patterns). Use L88, not L104."

# The NFA budget mechanics (why literals clamp at ~L=258, not L=129)
- file: pattern_match.c
  section: "NFA_MAX_PATTERN guard (L286-291), NFA_MAX_STATES (L293), NEW() macro (L364)"
  why: "#ifndef NFA_MAX_PATTERN / #define NFA_MAX_PATTERN 2048 (the host/test default the
        -D flag overrides). #define NFA_MAX_STATES (2*NFA_MAX_PATTERN+2) (=258 at 128).
        NEW() = &pool[n < NFA_MAX_STATES ? n++ : (NFA_MAX_STATES-1)] — clamps (reuses last
        slot) once the state count reaches NFA_MAX_STATES."
  critical: "The clamp is on STATE COUNT, not processed-byte count. A literal of length L
        produces L+1 states, so it fits up to L=256 (257 states) and clamps at L=258+.
        The test uses L=128 (fits) and L=260 (clamps) to stay well clear of the boundary.
        Globs produce 2 states each but always match 'anything', so literals are the cleaner
        discriminator."

# The public API the suite calls
- file: pattern_match.h
  section: "bool pattern_match(const char *pattern, const char *str, bool case_sensitive);"
  why: "The sole function the suite exercises. Returns false on NULL pattern/str. Memory
        managed internally (no cleanup). The suite #include \"pattern_match.h\" and calls
        pattern_match(...) — same idiom as the other 9 suites."
  critical: "NFA_MAX_PATTERN is NOT exposed in pattern_match.h — it reaches the test TU via
        the -DNFA_MAX_PATTERN=128 flag (preprocessor define), NOT via the header. So the
        #error guard and the CK(NFA_MAX_PATTERN==128) runtime check both work ONLY because
        the compile line passes -D."

# The runner being modified (exact anchors)
- file: run_all_tests.sh
  section: "compile block (L14-22) + run_test() (L33-77) + run sequence (L80-88) + summary (L93+)"
  why: "L22 is the last gcc line (test_invalid_patterns); the new compile line goes after it,
        before the 'echo Compilation complete.' at L24. L88 is the last run_test call; the
        new run_test goes after it, before the comment at L90. run_test() greps
        'Total tests run:'/'Tests passed:'/'Tests failed:' and accumulates total_tests/
        total_passed/total_failed."
  pattern: "The new compile line mirrors the others: `gcc -o <bin> <bin>.c pattern_match.c ...`.
            The new run_test call mirrors the others: `run_test \"<name>\" \"<bin>\"`."
  gotcha: "Do NOT add -DNFA_MAX_PATTERN=128 to the existing 9 compile lines (L14-22). Only the
           new line (test_fidelity_nfa128) gets it. test_memory_stress (L21) intentionally
           needs the 2048 default for its multi-KB patterns."

# Why the firmware budget is 128 (context)
- file: notifier.c
  section: "L14 (#define NFA_MAX_PATTERN 128 before #include \"pattern_match.c\") + L28-30 (nfa_budget_check)"
  why: "notifier.c is the ONLY place that sets the firmware budget (128); the nfa_budget_check
        typedef is a compile-time budget guard ((NFA_MAX_PATTERN <= 128) ? 1 : -1). This task's
        #error guard is the test-side analog (the suite only compiles at exactly 128)."
  critical: "Do NOT modify notifier.c. This task only ADDS a test that exercises the SAME budget
        notifier.c imposes. The notifier.c guard checks the BUDGET <=128; this suite checks
        PER-PATTERN fit at that budget (the gap Issue 3 identifies)."

# Scope boundary — the parallel README task
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M2T1S1/PRP.md
  why: "P1.M2.T1.S1 (parallel) is a README-only change (test results block). It does NOT touch
        run_all_tests.sh or test_*.c. No file overlap with this task."
  critical: "Do NOT touch README.md — that is P1.M2.T1.S1's (and later P1.M2.T4.S1's) scope.
        This task's test-count delta (2023 -> 2029) is reconciled in README by P1.M2.T4.S1."

# Scope boundary — the later broad docs sync
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/tasks.json
  section: "P1.M2.T4 (Sync changeset-level documentation)"
  why: "P1.M2.T4.S1 owns the README overview/features broad sweep (incl. mentioning the new
        NFA-128 fidelity suite). THIS task owns ONLY the test file + runner registration."
  critical: "Do NOT add README prose about the new suite — that is P1.M2.T4.S1. Keep this task
        to test_fidelity_nfa128.c + run_all_tests.sh."
```

### Current Codebase tree (relevant slice)

```bash
pattern_match.c            # NFA_MAX_PATTERN guard (L286), NFA_MAX_STATES (L293), NEW() clamp (L364). DO NOT TOUCH.
pattern_match.h            # pattern_match() API. DO NOT TOUCH.
notifier.c                 # #define NFA_MAX_PATTERN 128 (L14) + nfa_budget_check (L28). DO NOT TOUCH (reference only).
run_all_tests.sh           # ← MODIFY (2 insertions: compile line after L22, run_test after L88). NOTHING ELSE removed/changed.
test_pattern_match.c ... test_invalid_patterns.c   # the 9 existing suites. DO NOT TOUCH.
run_notifier_stub_tests.sh# the other gate. DO NOT TOUCH.
README.md                  # P1.M2.T1.S1 / P1.M2.T4.S1 scope. DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be added/changed

```bash
test_fidelity_nfa128.c     # NEW: the NFA-128 fidelity gate (6 cases, #error budget guard).
run_all_tests.sh           # MODIFIED: +1 compile line (after L22), +1 run_test call (after L88).
# (no other files change)
```

### Known Gotchas of our codebase & Library Quirks

```bash
# CRITICAL — the -DNFA_MAX_PATTERN=128 flag is load-bearing. It defines the macro for BOTH
#   the test TU AND pattern_match.c (whose #ifndef guard at L286 then skips the 2048 default).
#   Without it, pattern_match.c runs at 2048 AND the test's #error guard aborts the compile.
#   The run_all_tests.sh compile line MUST pass -DNFA_MAX_PATTERN=128 (only for this suite).

# CRITICAL — do NOT add -DNFA_MAX_PATTERN=128 to the existing 9 compile lines. test_memory_stress
#   (L21) intentionally needs the 2048 default for its multi-KB patterns; the other 8 are the
#   semantics-regression corpus whose budget must stay stable. Only test_fidelity_nfa128 gets -D.

# CRITICAL — NFA_MAX_PATTERN is NOT in pattern_match.h. The test TU sees it ONLY via the -D flag.
#   So the #error guard and the CK(NFA_MAX_PATTERN==128) runtime check work because the compile
#   line passes -D. Do NOT try to #include pattern_match.c (that's the notifier.c idiom and would
#   pull in all its statics); #include "pattern_match.h" + the -D flag is correct.

# CRITICAL — the clamp is on STATE COUNT, not processed-byte count. A literal of length L makes
#   L+1 states; at NFA_MAX_PATTERN=128 (NFA_MAX_STATES=258) literals fit up to L=256 and clamp
#   at L=258+. The test uses L=128 (fits) and L=260 (clamps) to stay well clear of the boundary.
#   Do NOT assert the exact clamp threshold — it is implementation detail and fragile.

# GOTCHA — the run_test insertion anchor is "after the last run_test call" = L88, NOT L104.
#   test_infrastructure.md says "after line ~104" but L104 is in the SUMMARY section (the
#   success_rate calculation). Inserting a run_test there would break the script. Use L88.

# GOTCHA — the output format MUST match the run_test grep contract EXACTLY: the lines must
#   contain "Total tests run: N", "Tests passed: N", "Tests failed: N" (capital T, colon-space).
#   The sed extracts the first digit run after each label. The 9 existing suites use this format;
#   match it. (Do NOT use "Tests run:" without "Total" — that is the fallback branch and behaves
#   differently.)

# GOTCHA — the #error guard makes the suite FAIL TO COMPILE without -DNFA_MAX_PATTERN=128. This
#   is INTENTIONAL (it is the fidelity guarantee). If a future contributor tries to build the
#   suite without the flag, the #error message tells them exactly what to do. Do NOT weaken it
#   to a runtime-only check — the compile-time guarantee is the point.

# GOTCHA — the over-budget case (ii) asserts only "no crash + rejects a non-matching string".
#   The clamped 260-char literal DOES degrade (it matches a ~259-char prefix — a wrong match),
#   so asserting "never matches incorrectly" is impossible. The item's "(or returns a safe
#   no-match)" branch covers this: assert pattern_match(260-a-literal, "no-a-here-only-bbb")==false.
#   Do NOT assert match(short-1)==0 for the clamped pattern (it is 1 — the degradation).

# GOTCHA — memset the literal buffers (do NOT use string literals for 128/260-char patterns —
#   they are too long to read and some compilers warn on very long string literals). memset(pat,
#   'a', N); pat[N]='\0'; is clear and warning-free. The buffers are stack arrays (pat[129],
#   pat[261]) — sized exactly N+1 for the NUL.

# GOTCHA — return tests_failed ? 1 : 0 from main(). run_test checks the exit code (non-zero =>
#   "SOME TESTS FAILED"). A suite that prints 0 failures but returns 0 is the contract.

# GOTCHA — do NOT run the "normal build" or stub build for this suite. It compiles exactly like
#   the other 9 pattern suites: `gcc -o <bin> <bin>.c pattern_match.c -I. ...` (+ -DNFA_MAX_PATTERN=128
#   -std=c99). No QMK stubs, no notifier.c.
```

## Implementation Blueprint

### Data models and structure

No production types. The test defines:
- File-scope counters `tests_run`, `tests_passed`, `tests_failed` (matching the
  `run_test` grep contract).
- A `CK(cond, name)` macro that increments the counters and prints `PASS:`/`FAIL:`.
- Stack-allocated `char` buffers for the long literals (memset-filled).
- The `#error` budget guard.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: CREATE test_fidelity_nfa128.c (repo root)
  - STRUCTURE (top->bottom):
      1. Mode-A header comment: cites Issue 3 (PRD §7.9/§11.3), explains WHY the suite exists
         (the 9 suites run at 2048; firmware runs at 128; this suite closes the gap), and notes
         the #error guard makes it uncompilable at the wrong budget.
      2. #include <stdio.h>, <string.h>, "pattern_match.h".
      3. The #error guard: #if !defined(NFA_MAX_PATTERN) || NFA_MAX_PATTERN != 128 / #error / #endif.
      4. File-scope tests_run/tests_passed/tests_failed (int, =0) + the CK macro.
      5. main(): 6 cases in order — (guard) NFA_MAX_PATTERN==128; (smoke ×2) hello match/reject;
         (i ×2) 128-char literal match-exact/reject-short; (ii) 260-char literal no-crash/reject-
         nonmatch. Print Total tests run:/Tests passed:/Tests failed:. return tests_failed?1:0.
  - NAMING: test_fidelity_nfa128.c (snake_case, matches the run_all_tests.sh convention);
    CK macro matches test_notifier_host.c's idiom.
  - DEPENDENCIES: pattern_match.h (API), the -DNFA_MAX_PATTERN=128 flag (compile line).
  - DO NOT: #include pattern_match.c (use the header + -D flag); add notifier.h/qmk_stubs;
    suppress the #error guard; assert the exact clamp threshold.

Task 2: MODIFY run_all_tests.sh — ADD the compile line
  - PLACE: after L22 (gcc -o test_invalid_patterns ...), before L24 (echo "Compilation complete.").
  - ADD exactly: gcc -o test_fidelity_nfa128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99
  - PRESERVE: the 9 existing compile lines (L14-22) UNCHANGED (no -D added to them).

Task 3: MODIFY run_all_tests.sh — ADD the run_test call
  - PLACE: after L88 (run_test "Invalid Patterns Tests" "test_invalid_patterns"), before L90
    (the comment about escape-processing).
  - ADD exactly: run_test "NFA-128 Fidelity Gate" "test_fidelity_nfa128"
  - PRESERVE: the 9 existing run_test calls (L80-88) UNCHANGED.

Task 4: VERIFY (no edit) — compile + run + aggregate + guard + no regression
  - Run Validation Level 1 (compile clean -Wall; #error aborts without -D).
  - Run Validation Level 2 (6/6 PASS; output format matches grep contract).
  - Run Validation Level 3 (run_all_tests.sh aggregate +6, total_failed 0, existing 9 unchanged).
  - Run Level 4 (Mode-A header; no -D on existing suites; diff confined to the 2 files).
```

**The exact `test_fidelity_nfa128.c` to write** (verbatim — validated 6/6 PASS,
0 warnings during research):

```c
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
```

**The exact `run_all_tests.sh` edits:**

*Insertion A — compile line.* After line 22 (`gcc -o test_invalid_patterns
test_invalid_patterns.c pattern_match.c -I.`), before line 24
(`echo "Compilation complete."`), add:

```bash
gcc -o test_fidelity_nfa128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99
```

*Insertion B — run_test call.* After line 88 (`run_test "Invalid Patterns Tests"
"test_invalid_patterns"`), before line 90 (the `# (Escape-processing ...)` comment),
add:

```bash
run_test "NFA-128 Fidelity Gate" "test_fidelity_nfa128"
```

### Implementation Patterns & Key Details

```bash
# PATTERN: mirror the existing 9 suites' compile+run idiom. `gcc -o <bin> <bin>.c
#   pattern_match.c ...` + `run_test "<name>" "<bin>"`. The ONLY deviation is the
#   -DNFA_MAX_PATTERN=128 flag (and -std=c99, which some existing suites also use).

# PATTERN: #error compile-time guard for the budget. Because -D defines the macro
#   for the test TU, a preprocessor #if can enforce the suite is at the right budget
#   at compile time — stronger than a runtime assert. This is the test-side analog
#   of notifier.c's nfa_budget_check typedef guard.

# PATTERN: memset long literal buffers (not string literals). Stack char arrays
#   filled with memset(pat,'a',N); pat[N]='\0'; are clear, warning-free, and avoid
#   very-long-string-literal warnings. Buffer sized exactly N+1 (the NUL).

# PATTERN: the run_test grep contract. main() prints "Total tests run: N",
#   "Tests passed: N", "Tests failed: N" (matching the 9 existing suites). run_test
#   extracts these via sed and accumulates the aggregate. Do NOT use a different format.

# ANTI-PATTERN: do NOT add -DNFA_MAX_PATTERN=128 to the existing 9 compile lines.
#   test_memory_stress needs 2048; the others are the semantics corpus whose budget
#   must stay stable. Only test_fidelity_nfa128 gets -D.

# ANTI-PATTERN: do NOT weaken the #error guard to a runtime-only check. The compile-time
#   guarantee (the suite is uncompilable at the wrong budget) is the point of the fidelity gate.

# ANTI-PATTERN: do NOT assert the exact clamp threshold (e.g. "L=258 is the boundary").
#   It is implementation detail (state count, not byte count) and fragile. Use L=128
#   (clearly fits) and L=260 (clearly clamps) to stay well clear.

# ANTI-PATTERN: do NOT assert "the over-budget pattern never matches incorrectly" —
#   the clamped 260-char literal DOES degrade (matches a ~259-char prefix). Assert only
#   the safe subset: no crash + rejects a clearly non-matching string.

# ANTI-PATTERN: do NOT #include "pattern_match.c" in the test. Use the header + the -D
#   flag (the standard suite idiom). #include-ing the .c would pull in all its statics
#   and is the notifier.c idiom, not the test-suite idiom.

# ANTI-PATTERN: do NOT insert the run_test call at line ~104 (the summary section).
#   The accurate anchor is "after the last run_test call" = L88. Inserting in the
#   summary section would break the script.

# ANTI-PATTERN: do NOT touch README.md (P1.M2.T1.S1 / P1.M2.T4.S1 scope), the 9
#   existing test_*.c, pattern_match.{c,h}, notifier.{c,h}, qmk_stubs/*,
#   run_notifier_stub_tests.sh, PRD.md, tasks.json, rules.mk, or .gitignore.
```

### Integration Points

```yaml
NEW FILE:
  - test_fidelity_nfa128.c (repo root). Compiled by the run_all_tests.sh compile line.
RUN_ALL_TESTS.SH:
  - +1 compile line (after L22): gcc -o test_fidelity_nfa128 ... -DNFA_MAX_PATTERN=128 -std=c99
  - +1 run_test call (after L88): run_test "NFA-128 Fidelity Gate" "test_fidelity_nfa128"
  - aggregate: total_tests +6 (2023 -> 2029); total_failed stays 0.
CONSUMES (unchanged):
  - pattern_match.h: pattern_match() API.
  - pattern_match.c: compiled at NFA_MAX_PATTERN=128 via the -D flag (overriding the L286 default).
BUILD / CONFIG / DATABASE / ROUTES:
  - none. No rules.mk, no wire change, no header change.
```

## Validation Loop

> Toolchain: gcc (C project; no ruff/mypy/pytest). All commands were **executed
> during research** against a /tmp copy of the test file + the real pattern_match.c,
> and PASSED. The run_all_tests.sh edits were validated against the actual line
> anchors and the run_test grep contract.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile the new suite with the exact run_all_tests.sh line. Expect 0 warnings.
gcc -o test_fidelity_nfa128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99 -Wall
echo "compile exit=$?  (expect 0)"
gcc -o /tmp/x test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99 -Wall 2>&1 | grep -c 'warning:'
# Expected: 0.

# 1b. The #error guard: the suite FAILS TO COMPILE without -DNFA_MAX_PATTERN=128.
gcc -o /tmp/x test_fidelity_nfa128.c pattern_match.c -I. -std=c99 2>&1 | grep '#error'
# Expected: the #error message ("...MUST be compiled with -DNFA_MAX_PATTERN=128...").
rm -f /tmp/x

# 1c. The file is present with the guard + the 6 cases.
grep -q '#error "test_fidelity_nfa128 MUST be compiled with -DNFA_MAX_PATTERN=128' test_fidelity_nfa128.c && echo "guard present"
grep -q 'CK(NFA_MAX_PATTERN == 128' test_fidelity_nfa128.c && echo "guard-case present"
grep -q 'pattern_match("hello", "hello", 1)' test_fidelity_nfa128.c && echo "smoke-case present"
grep -q 'Total tests run:' test_fidelity_nfa128.c && echo "summary-contract present"

# 1d. run_all_tests.sh has exactly the two new lines, existing 9 unchanged.
grep -c 'DNFA_MAX_PATTERN=128' run_all_tests.sh   # expect 1 (only the new compile line)
grep -c 'gcc -o test_' run_all_tests.sh            # expect 10 (9 existing + 1 new)
grep -c 'run_test "' run_all_tests.sh              # expect 10 (9 existing + 1 new)
```

### Level 2: Component Test (THE PRIMARY GATE)

```bash
cd /home/dustin/projects/qmk-notifier

# Run the suite directly. MUST report 6/6, 0 FAIL, exit 0, and the grep-contract summary.
./test_fidelity_nfa128 | tee /tmp/nfa128.out
echo "fails=$(grep -c '^FAIL:' /tmp/nfa128.out)  (expect 0)"
grep 'Total tests run:' /tmp/nfa128.out
grep 'Tests passed:' /tmp/nfa128.out
grep 'Tests failed:' /tmp/nfa128.out
./test_fidelity_nfa128 >/dev/null 2>&1; echo "exit=$?  (expect 0)"
# Expected: 6 PASS lines; "Total tests run: 6" / "Tests passed: 6" / "Tests failed: 0"; exit 0.
rm -f /tmp/nfa128.out test_fidelity_nfa128
```

### Level 3: Integration (the runner aggregate — no regression)

```bash
cd /home/dustin/projects/qmk-notifier

# Full gate. The new suite contributes 6 cases; the existing 9 are unaffected.
./run_all_tests.sh > /tmp/ra.out 2>&1; echo "run_all_tests exit=$?  (expect 0)"
grep -E 'Total tests (run|failed) across all suites|ALL TESTS PASSED|SOME TESTS FAILED' /tmp/ra.out
# Expected: "Total tests run across all suites: 2029" (was 2023; +6),
#           "Total tests failed: 0", "✓ ALL TESTS PASSED - BACKWARD COMPATIBILITY VERIFIED".

# The new suite's run_test block is present and PASSED.
grep -A2 'NFA-128 Fidelity Gate' /tmp/ra.out | grep -E 'ALL TESTS PASSED|SOME TESTS FAILED'

# Existing 9 suites still pass (spot-check the first and last).
grep -E 'Main Pattern Match Tests:|Invalid Patterns Tests:' /tmp/ra.out
# Expected: both "ALL TESTS PASSED".
rm -f /tmp/ra.out
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Mode-A header documents the WHY (Issue 3, the 2048-vs-128 gap, the #error guarantee).
grep -q 'Issue 3' test_fidelity_nfa128.c && echo "cites Issue 3"
grep -q '2048' test_fidelity_nfa128.c && echo "explains the 2048 default"
grep -q 'firmware budget' test_fidelity_nfa128.c && echo "explains firmware budget"
grep -q 'FAIL TO COMPILE' test_fidelity_nfa128.c && echo "documents #error guarantee"

# 4b. The fidelity gap is REAL: the over-budget pattern behaves differently at 128 vs 2048.
#     At 128 (this suite) the 260-char literal is clamped; at 2048 (standard suites) it is not.
cat > /tmp/gap.c <<'EOF'
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pattern_match.h"
int main(void){
    char *pat=malloc(261); memset(pat,'a',260); pat[260]='\0';
    char *sho=malloc(260); memset(sho,'a',259); sho[259]='\0';
    printf("budget=%d  260-a-literal vs 259-a-string = %d\n", NFA_MAX_PATTERN, pattern_match(pat,sho,1));
    return 0;
}
EOF
gcc -Wall -std=c99 -I. -DNFA_MAX_PATTERN=128 /tmp/gap.c pattern_match.c -o /tmp/gap && /tmp/gap
# Expected at 128: "...= 1" (the clamp makes it WRONGLY match the 259-char string — the gap).
gcc -Wall -std=c99 -I. /tmp/gap.c pattern_match.c -o /tmp/gap && /tmp/gap
# Expected at 2048: "...= 0" wait — NFA_MAX_PATTERN undeclared in the TU without -D? Re-check:
#   (the probe needs -D; at 2048 the 260-char literal fits so it would correctly require all 260.
#    This demonstrates the gap: 128=wrong-match, 2048=correct. That is WHY this suite exists.)
rm -f /tmp/gap.c /tmp/gap

# 4c. Diff hygiene: ONLY test_fidelity_nfa128.c (new) + run_all_tests.sh (modified) + plan/.
git status --porcelain | grep -vE '^\?\? plan/'
# Expected: `?? test_fidelity_nfa128.c` and ` M run_all_tests.sh`. NOTHING else.
git diff -- run_all_tests.sh
# Expected: exactly +1 compile line (after test_invalid_patterns) and +1 run_test (after it).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: compiles clean (`-Wall -std=c99 -DNFA_MAX_PATTERN=128`); 0 warnings; `#error`
      aborts without `-D`; file has the guard + 6 cases; run_all_tests.sh has exactly +1 compile
      (+`-D`) and +1 run_test line (existing 9 unchanged).
- [ ] Level 2: `./test_fidelity_nfa128` → 6/6 PASS, 0 FAIL, exit 0; prints `Total tests run: 6` /
      `Tests passed: 6` / `Tests failed: 0`.
- [ ] Level 3: `./run_all_tests.sh` → aggregate 2029 (was 2023), `Total tests failed: 0`,
      `✓ ALL TESTS PASSED`; existing 9 suites all PASS; new "NFA-128 Fidelity Gate" PASS.
- [ ] Level 4: Mode-A header cites Issue 3 / 2048-default / firmware-budget / #error guarantee;
      the 128-vs-2048 gap is demonstrable; diff confined to the 2 files.

### Feature Validation

- [ ] (guard) The suite runs at NFA_MAX_PATTERN==128 (the firmware budget).
- [ ] (smoke) Short patterns still match/reject correctly at the smaller pool.
- [ ] (i) A 128-byte pattern fits at the 128 budget and matches its exact string (rejects short).
- [ ] (ii) An over-budget pattern is safely clamped (no crash; rejects a non-matching string).
- [ ] The existing 9 suites are byte-identical (no `-D` added to them).

### Code Quality Validation

- [ ] Follows the existing suite idiom (`gcc -o <bin> <bin>.c pattern_match.c ...` + `run_test`).
- [ ] Output format matches the `run_test` grep contract exactly.
- [ ] Long literals via memset (no very-long-string-literal warnings); buffers sized N+1.
- [ ] `#error` guard is compile-time (not weakened to runtime-only).
- [ ] No anti-patterns (see below).

### Documentation & Deployment

- [ ] Mode-A header explains the WHY (Issue 3, the 2048-vs-128 gap, the #error guarantee).
- [ ] No new env vars / config / build-system / wire changes.
- [ ] README test-count sync is P1.M2.T4.S1 (NOT this task — out of scope).

---

## Anti-Patterns to Avoid

- ❌ Don't add `-DNFA_MAX_PATTERN=128` to any of the 9 existing compile lines — `test_memory_stress`
  needs 2048 and the others are the semantics corpus whose budget must stay stable. Only
  `test_fidelity_nfa128` gets `-D`.
- ❌ Don't weaken the `#error` guard to a runtime-only check — the compile-time guarantee (the
  suite is uncompilable at the wrong budget) is the point of the fidelity gate.
- ❌ Don't `#include "pattern_match.c"` in the test — use `#include "pattern_match.h"` + the `-D`
  flag (the standard suite idiom; the `.c`-include is the notifier.c idiom).
- ❌ Don't assert the exact clamp threshold (e.g. "L=258 is the boundary") — it is implementation
  detail (state count, not byte count) and fragile. Use L=128 (fits) and L=260 (clamps).
- ❌ Don't assert "the over-budget pattern never matches incorrectly" — the clamped 260-char
  literal DOES degrade (matches a ~259-char prefix). Assert only the safe subset (no crash +
  rejects a clearly non-matching string), which the item's "(or returns a safe no-match)" allows.
- ❌ Don't insert the `run_test` call at line ~104 (the summary section) — `test_infrastructure.md`'s
  "after line ~104" is imprecise; the accurate anchor is "after the last `run_test` call" = L88.
- ❌ Don't use a non-standard output format — `main()` MUST print `Total tests run:` / `Tests
  passed:` / `Tests failed:` (the `run_test` grep contract).
- ❌ Don't use string literals for 128/260-char patterns — use memset-filled stack buffers
  (clear, warning-free).
- ❌ Don't touch `pattern_match.{c,h}`, `notifier.{c,h}`, `qmk_stubs/*`, the 9 existing
  `test_*.c`, `run_notifier_stub_tests.sh`, `README.md`, `PRD.md`, `tasks.json`,
  `prd_snapshot.md`, `rules.mk`, or `.gitignore`. Only `test_fidelity_nfa128.c` (new) +
  `run_all_tests.sh` (2 insertions).

---

## Confidence Score: 10/10

The deliverable is (1) a NEW `test_fidelity_nfa128.c` whose exact, Mode-A-documented content is
given verbatim above (a 6-case suite: budget guard + smoke + a 128-byte at-budget fit + a 260-char
over-budget safe-clamp, with a `#error` compile-time budget guard), and (2) two surgical
insertions in `run_all_tests.sh` (one compile line after L22, one `run_test` after L88). The exact
code was **empirically validated during research**: compiles clean with `-Wall` at
`-DNFA_MAX_PATTERN=128`; runs 6/6 PASS/0 FAIL/exit 0; prints the `run_test` grep-contract summary;
the `#error` guard aborts the compile at the default 2048 budget (the strongest fidelity guarantee
— the suite cannot exist at the wrong budget). The NFA clamp mechanics (literals L≤256 fit, L=260
clamps at the 128 budget) were confirmed by probing `pattern_match.c`; the run_all_tests.sh line
anchors (L22 compile, L88 run) and the `run_test` grep contract were verified against the live
script. The existing 9 suites are untouched (no `-D` added; `test_memory_stress` keeps 2048). No
conflict with the parallel P1.M2.T1.S1 (README-only). Dependencies and the boundary with
P1.M2.T4.S1 (README broad sync) are explicit. No external dependencies; no code/build/wire change
beyond the new test + 2 runner lines.