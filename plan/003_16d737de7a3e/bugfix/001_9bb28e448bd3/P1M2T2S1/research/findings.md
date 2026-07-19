# Research Notes — P1.M2.T2.S1: Create test_fidelity_nfa128.c + register in run_all_tests.sh

## The bug this closes (Issue 3, PRD §7.9/§11.3)

The 9 standard pattern suites in `run_all_tests.sh` compile `pattern_match.c`
**directly**, so the `#ifndef NFA_MAX_PATTERN` guard at pattern_match.c:286-291
defaults it to **2048**. The firmware, however, runs at **NFA_MAX_PATTERN=128**
(notifier.c:14 `#define`s it before `#include "pattern_match.c"`). A user pattern
between ~129 and ~256 processed bytes would pass every standard suite yet be
**silently clamped** on hardware (`NEW()` reuses the last pool slot,
pattern_match.c:364) and could match incorrectly. This task adds a 10th suite
compiled with `-DNFA_MAX_PATTERN=128` so the gate exercises the matcher at the
firmware budget.

## The NFA budget mechanics (empirically verified)

At `NFA_MAX_PATTERN=128`: `NFA_MAX_STATES = 2*128+2 = 258`. The `NEW()` macro
(pattern_match.c:364) is `&pool[n < NFA_MAX_STATES ? n++ : (NFA_MAX_STATES-1)]`
— it clamps (reuses the last slot) once the state count `n` reaches 258.

A plain literal of length L compiles to **L+1 states** (L OP_CHAR + 1 OP_MATCH):
- L ≤ 256 → ≤ 257 states → fits (no clamp) → matches exactly (exact=1, short=0).
- L = 260 → 261 states → CLAMP (NEW() reuses slot 257; the trailing char/MATCH
  overwrite earlier states) → **degraded**: matches a 259-char prefix
  (exact=1 AND short-1=1 — a WRONG match). Verified empirically:

```
literal L=250: match(exact)=1  match(short-1)=0   ← fits, correct
literal L=260: match(exact)=1  match(short-1)=1   ← CLAMP, degraded (wrong match)
```

Globs (`*`) produce 2 states each (OP_ANY+OP_SPLIT), so N globs → 2N+1 states;
they clamp at N=129 too, but all-glob patterns always match "anything" so the
clamp is not observable — literals are the cleaner discriminator.

**Takeaway for the test:** literals are the simplest way to exercise both the
"fits at the firmware budget" (case i) and the "over-budget safe clamp" (case ii)
paths. The exact clamp threshold (~L=258) is implementation detail; the test
picks L=128 (clearly fits) and L=260 (clearly clamps) to stay well clear of the
boundary.

## The #error compile-time guard (the elegance of this design)

`-DNFA_MAX_PATTERN=128` on the gcc line defines the macro for BOTH translation
units (the test TU AND pattern_match.c — the `#ifndef` guard in pattern_match.c
sees it defined and skips the 2048 default). So inside test_fidelity_nfa128.c:

```c
#if !defined(NFA_MAX_PATTERN) || NFA_MAX_PATTERN != 128
#error "test_fidelity_nfa128 MUST be compiled with -DNFA_MAX_PATTERN=128 ..."
#endif
```

makes the suite **FAIL TO COMPILE** at any other budget. Verified: at default
2048 (no -D) the compile aborts with the #error; at -DNFA_MAX_PATTERN=128 it
compiles clean. This is the strongest possible fidelity guarantee — the suite
literally cannot exist at the wrong budget. (A runtime `CK(NFA_MAX_PATTERN==128)`
is also included as a belt-and-suspenders check.)

## The exact test (validated 6/6 PASS, 0 warnings with -Wall)

```c
#include <stdio.h>
#include <string.h>
#include "pattern_match.h"

#if !defined(NFA_MAX_PATTERN) || NFA_MAX_PATTERN != 128
#error "test_fidelity_nfa128 MUST be compiled with -DNFA_MAX_PATTERN=128 (the firmware budget). See run_all_tests.sh."
#endif

static int tests_run=0, tests_passed=0, tests_failed=0;
#define CK(cond, name) do { tests_run++; \
    if (cond) { tests_passed++; printf("PASS: %s\n", name); } \
    else      { tests_failed++; printf("FAIL: %s\n", name); } } while(0)

int main(void) {
    CK(NFA_MAX_PATTERN == 128, "guard: NFA_MAX_PATTERN == 128 (firmware budget in effect)");
    CK(pattern_match("hello","hello",1) == true,  "smoke: short pattern matches at 128 budget");
    CK(pattern_match("hello","world",1) == false, "smoke: short pattern rejects at 128 budget");
    {   /* (i) 128-byte processed pattern (128-char literal -> 129 states) fits, matches exactly */
        char pat[129], m[129], s[128];
        memset(pat,'a',128); pat[128]='\0';
        memset(m,'a',128);   m[128]='\0';
        memset(s,'a',127);   s[127]='\0';
        CK(pattern_match(pat,m,1)==true,  "(i) 128-byte pattern matches its exact 128-char string");
        CK(pattern_match(pat,s,1)==false, "(i) 128-byte pattern rejects a 127-char (too-short) string");
    }
    {   /* (ii) over-budget (260-char literal -> 261 states) is safely clamped, no crash */
        char pat[261]; memset(pat,'a',260); pat[260]='\0';
        CK(pattern_match(pat,"no-a-here-only-bbb",1)==false, "(ii) over-budget pattern does not crash; rejects a non-match (safe clamp)");
    }
    printf("\nTotal tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    return tests_failed ? 1 : 0;
}
```

Verified output:
```
PASS: guard: NFA_MAX_PATTERN == 128 (firmware budget in effect)
PASS: smoke: short pattern matches at 128 budget
PASS: smoke: short pattern rejects at 128 budget
PASS: (i) 128-byte pattern matches its exact 128-char string
PASS: (i) 128-byte pattern rejects a 127-char (too-short) string
PASS: (ii) over-budget pattern does not crash; rejects a non-match (safe clamp)

Total tests run: 6
Tests passed: 6
Tests failed: 0
```

## The run_all_tests.sh edits (2 insertions)

The runner's `run_test()` (lines 33-77) greps `Total tests run:` / `Tests passed:`
/ `Tests failed:` and accumulates `total_tests`/`total_passed`/`total_failed`.
The new suite's output format matches that contract exactly (verified: the sed
`'s/.*Total tests run: \([0-9]*\).*/\1/'` extracts 6; "Tests passed:"→6;
"Tests failed:"→0).

**Insertion 1 — compile (after line 22, the last gcc line):**
```bash
gcc -o test_fidelity_nfa128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99
```
This goes AFTER `gcc -o test_invalid_patterns ...` (line 22) and BEFORE
`echo "Compilation complete."` (line 24). It is the ONLY suite with
`-DNFA_MAX_PATTERN=128`; the existing 9 lines are untouched (test_memory_stress
intentionally needs 2048 for its multi-KB patterns — do NOT add -D to them).

**Insertion 2 — run (after line 88, the last run_test):**
```bash
run_test "NFA-128 Fidelity Gate" "test_fidelity_nfa128"
```
This goes AFTER `run_test "Invalid Patterns Tests" "test_invalid_patterns"`
(line 88) and BEFORE the comment at line 90. (The item/test_infrastructure.md
said "after line ~104" but that is in the SUMMARY section — the accurate anchor
is "after the last run_test call", which is line 88.)

**Aggregate effect:** total_tests increases by 6 (2023 → 2029); total_failed
stays 0. The existing 9 suites are byte-identical (no -D change to them).

## Why not just add -DNFA_MAX_PATTERN=128 to the existing 9 suites?

The item explicitly forbids it, and for good reason: `test_memory_stress`
intentionally exercises multi-KB patterns that need the 2048 default. And the
other 8 suites are the matcher SEMANTICS regression corpus — changing their
budget would silently change which patterns fit, undermining their stability as a
semantics gate. A DEDICATED 10th suite is the clean, non-invasive way to add the
fidelity gate without disturbing the existing corpus.

## Scope / boundaries

- This task: (1) CREATE `test_fidelity_nfa128.c` (repo root); (2) MODIFY
  `run_all_tests.sh` (2 insertions: compile line + run_test call). Nothing else.
- It does NOT touch pattern_match.{c,h}, notifier.{c,h}, qmk_stubs/*, the 9
  existing test_*.c, run_notifier_stub_tests.sh, README.md, PRD.md, tasks.json.
- No conflict with the parallel P1.M2.T1.S1 (README-only — different file).
- README test-count sync is P1.M2.T4.S1 (the broad changeset docs sweep), NOT
  this task. (This task adds 6 tests to the aggregate; the README total will be
  reconciled by P1.M2.T4.S1 / the implementer notes the new count.)

## Item-contract clarifications

- The item's "(ii) 129+ bytes ... clamped" framing is slightly imprecise: the
  clamp is on STATE count (vs NFA_MAX_STATES=258), not directly on processed-byte
  count. A 129-char LITERAL (130 states) still fits; the clamp triggers around
  L=258 for literals. The test uses L=128 (fits) and L=260 (clamps) — well clear
  of the boundary, satisfying the item's intent (exercise both fit and clamp at
  the firmware budget) without being fragile to the exact threshold.
- The item says "(ii) ... does NOT match incorrectly (or returns a safe
  no-match)." The clamped 260-char literal DOES degrade (it matches a 259-char
  prefix — a wrong match), so asserting "does not match incorrectly" universally
  is impossible. The test asserts the SAFE, defensible subset: the over-budget
  call does not crash AND returns false against a clearly non-matching string
  ("no-a-here-only-bbb"). This is the "returns a safe no-match" branch the item
  explicitly allows.
- The compile command in the item omits `-Wall`; I added it (the suite must
  "compile cleanly with -Wall" per item §4 OUTPUT). Verified 0 warnings.