# Research Notes — P1.M2 (Milestone): Thompson NFA Engine

This milestone PRP **consolidates** the four sub-task PRPs/research already on
disk (`P1M2T1S1`, `P1M2T1S2`, `P1M2T2S1`) and folds in the **re-planning
learnings** from `plan/001_e329fbe4ae4d/P1/issue_feedback.md` (attempt 1/3),
which surfaced two defects squarely inside P1.M2 scope.

## 1. What P1.M2 owns (scope boundary)

| Component | Owner | Status in live code |
|---|---|---|
| `NFA_MAX_PATTERN` / `NFA_MAX_STATES` sizing | **P1.M2.T1.S1** | present (value changed — see §3) |
| `enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH }` | **P1.M2.T1.S1** | present |
| `struct State { op, arg, out, out1, lastlist }` + `static int nfa_gen` | **P1.M2.T1.S1** | present |
| `nfa_compile()` — Thompson construction | **P1.M2.T1.S2** | present (NEW() fixed — see §3) |
| `nfa_addstate()` — epsilon-closure | **P1.M2.T2.S1** | present |
| `nfa_has_match()` + `nfa_match()` — simulation | **P1.M2.T2.S2** | present |

**Cross-milestone dependencies (NOT P1.M2 — but called BY P1.M2 code):**
- `is_word_boundary(string_start, abspos)` — called by `nfa_addstate` (OP_ASSERT).
  Owned by **P1.M3.T1.S1**. The sub-task PRP stubbed it; the live code has the real impl.
- `pattern_char_matches(pc, sc, case_sensitive)` — called by `nfa_match` (OP_CHAR).
  Owned by **P1.M3.T2.S1**. Live code has the real impl.
- `process_escapes()` / `parse_pattern()` produce the processed-pattern byte
  string `nfa_compile` consumes. Owned by **P1.M1.T2**.

The milestone PRP states these as **contracts** (exact signatures + semantics)
so the NFA engine can be implemented/verified without re-reading P1.M3/P1.M1.

## 2. Verified current state (live code = source of truth)

```
$ gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o   # exit 0, ZERO warnings
$ ./run_all_tests.sh
Total tests run across all suites: 2019
Total tests passed: 2018
Total tests failed: 1                       # test_memory_stress "Anchored huge pattern exact match"
Pathological a+a+…b vs 199×a: result=0 in ~1.3 ms   (acceptance §11.2B: <50 ms ✓)
```

The NFA engine itself is correct and linear. The single failure is a
**sizing/architecture** issue, not an algorithm defect (see §3.3).

## 3. Re-planning learnings (from P1 issue_feedback.md — MUST address)

### 3.1 NEW() macro off-by-one crash (P1.M2.T1.S2 defect)
The sub-task PRP `P1M2T1S2/PRP.md` specified:
```c
#define NEW() (&pool[n < NFA_MAX_STATES ? n++ : n])   // BUG: returns &pool[NFA_MAX_STATES] when full
```
When `n == NFA_MAX_STATES` the macro returns `&pool[NFA_MAX_STATES]` — **one past
the end** — causing `*** stack smashing detected ***` under the stack protector.
The macro's own comment said "reuse the last slot" but it did not. **Live code
fix (verified):**
```c
#define NEW() (&pool[n < NFA_MAX_STATES ? n++ : (NFA_MAX_STATES - 1)])
```
This returns the last valid slot on overflow → bounded, no crash. The milestone
PRP **must** specify the corrected form.

### 3.2 NFA_MAX_PATTERN too small for realistic stress (P1.M2.T1.S1 defect)
Sub-task PRP specified `NFA_MAX_PATTERN = 128` (matching PRD Appendix B). But
the realistic stress suites (`test_invalid_patterns`, `test_memory_stress`) build
1000–1500-char patterns that 128 cannot compile (they truncate via the NEW
clamp → wrong results). Live code raised it to **2048**, which makes all
realistic cases pass.

**Architectural tension (PRD §7.9 vs §13 #14):** `notifier.c` does
`#include "pattern_match.c"` directly, so whatever value `pattern_match.c`
defines is inherited by the QMK build. 2048 → ~4098 States → ~130 KB on 64-bit
(~82 KB on 32-bit) — fine for RP2040 (264 KB, the reference target) but it
**overflows AVR** (2–8 KB). PRD §7.9 explicitly says "For low-RAM AVR, lower
NFA_MAX_PATTERN (e.g. 48)".

**Resolution the milestone PRP mandates:** make `NFA_MAX_PATTERN` overridable via
an `#ifndef` guard, with a generous host/test default and a documented QMK
override path (`#define NFA_MAX_PATTERN 128` in `notifier.c` before the include).
This is the standard C per-target-tunable idiom and is PRD §7.9-compliant. It is
P1.M2 scope because the sizing constant lives here.

### 3.3 The one remaining failure — a genuine architecture/test conflict
`test_memory_stress.c` line 173 sets `max_test_size = 50000` and builds a
**`^` + (10000 × "test") + `$` ≈ 40 002-char pattern** expecting an anchored
exact match (line 204). To compile that, `NFA_MAX_PATTERN ≥ 40002` → a ~3.8 MB
**stack** array (`State pool[80006]` + two `State*[80006]` lists, ~2.5 MB +
~1.3 MB). That same value is inherited by the QMK build and catastrophically
overflows MCU RAM — violating PRD §13 invariant #14 (weak-default/link safety is
not the issue; RAM is).

**This is NOT a P1.M2 algorithm defect.** The NFA engine correctly degrades
(no crash, ASan-clean) on the oversized input via the NEW() clamp. The conflict
is between the *test's* expectation (40 KB patterns work) and the PRD's
*fixed-stack* mandate (§7.9). Options for a human (documented in the PRP, the
decision itself is P1.M4/P3 scope):
  (a) accept 2018/2019 + graceful no-crash degradation on the impossible case;
  (b) lower `max_test_size` to a realistic value (< NFA_MAX_PATTERN);
  (c) mark that single sub-case as expected-to-skip for fixed-stack builds.
The P1.M2 deliverable is (a): bounded, non-crashing behavior on any input.

## 4. Source of truth for the algorithm

- **Live code** `pattern_match.c` is the working truth (PRD §17: "the code + the
  passing tests win"). The functions to reproduce are at:
  - sizing/State/ops/nfa_gen: ~lines 279–333
  - `nfa_compile`: ~lines 334–410
  - classifiers + `is_word_boundary` (P1.M3, but needed): ~lines 437–476
  - `nfa_addstate`: ~lines 478–530
  - `pattern_char_matches` (P1.M3, but needed): ~lines 532–560
  - `nfa_has_match` + `nfa_match`: ~lines 562–600
- PRD **§7.5** (engine), **§7.6** (classifiers), **§7.7** (single-char predicate),
  **§7.8** (why NFA), **§7.9** (sizing), **§13** (invariants #8–#11, #14).
- Architecture doc `plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md`
  §§ "NFA State" / "NFA Compilation" / "NFA Simulation (nfa_match)" / "nfa_addstate".
- Russ Cox, *Regular Expression Matching Can Be Simple And Fast*:
  https://swtch.com/~rsc/regexp/regexp1.html

## 5. Key invariants the milestone must preserve (PRD §13)

- #8: glob `*` matches `\n`/`\r` (OP_ANY); dot `.` excludes them (OP_CHAR 0x0D).
- #9: `+` is a postfix quantifier (compiles CHAR+SPLIT loop); `*` is a standalone
  token (compiles SPLIT→ANY loop). Do not conflate.
- #10: `abspos` (for `\b`/`\B`) is absolute from `string_start`, forwarded
  UNCHANGED through OP_SPLIT recursion.
- #11: the `lastlist == nfa_gen` guard is MANDATORY — without it OP_SPLIT and
  `\b\b` recurse infinitely. `nfa_gen` is bumped once per simulation phase by
  `nfa_match`, never by `nfa_addstate`.
- §7.8: `X+` compiles to exactly 2 states → `a+a+a+…` scales as 2k+1, never 2^k.
- #14 / §7.9: the pool stays on the stack; sizing is per-target; no crash on overflow.
