# Research Notes — P1.M2.T2.S1: `nfa_addstate()` epsilon-closure

## The function (verbatim from source of truth)

git `81df853:pattern_match.c` lines 127–147:

```c
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
        if (*string_start != '\0' &&
            is_word_boundary(string_start, abspos) == want_boundary)
            nfa_addstate(list, n, s->out, string_start, abspos);
        return;
    }
    list[(*n)++] = s;                             /* OP_CHAR / OP_ANY : wait for a char */
}
```

## CRITICAL DEPENDENCY FINDING (load-bearing — drove the PRP design)

`nfa_addstate` calls `is_word_boundary(string_start, abspos)` in the OP_ASSERT
branch. But `is_word_boundary` is owned by **P1.M3.T1.S1** (a LATER task per the
plan: P1.M2.T2 → P1.M2.T2.S2 → P1.M3.T1 → P1.M3.T2). It does not exist when
this task runs. This MUST be resolved or the build/link gates break.

### Tested options against a realistic post-P1.M2.T1.S2 file (nfa_compile present):

**Option A — forward declaration only** (`static bool is_word_boundary(...);`,
no body):
- `gcc -Wall -Wextra -std=c99 -c` → exit 0, BUT adds a 4th warning:
  `'is_word_boundary' used but never defined`.
- LINK (`gcc test_metachar_verification.c pattern_match.c`) → **FAILS**:
  `/usr/bin/ld: undefined reference to 'is_word_boundary'`.
  GCC emits the static `nfa_addstate` even though it is unused (that is why
  `-Wunused-function` fires), so the linker tries to resolve `is_word_boundary`.
- ❌ REJECTED — breaks the Level-3 LINK gate that every prior PRP validated.

**Option D — STUB `is_word_boundary` returning false** (defined immediately
before `nfa_addstate`, clearly marked temporary, replaced by P1.M3.T1.S1):
- `gcc -Wall -Wextra -std=c99 -c` → exit 0, **exactly 3** warnings:
  - `nfa_addstate defined but not used` (NEW — self-resolves P1.M2.T2.S2)
  - `nfa_compile defined but not used` (carried — self-resolves P1.M2.T2.S2)
  - `get_escaped_char defined but not used` (carried — self-resolves P1.M3.T2.S1)
- `gcc -fsyntax-only` → exit 0, silent.
- LINK `test_metachar_verification.c pattern_match.c` → exit 0.
- ✅ **ACCEPTED.** Mirrors the S2 `match_with_anchors` stub convention exactly:
  S2 stubbed `match_with_anchors` (replaced by P1.M3.T2.S2); this task stubs
  `is_word_boundary` (replaced by P1.M3.T1.S1).

### Why the stub value (false) is safe / never observed

- `nfa_addstate` is dead code until `nfa_match` exists (P1.M2.T2.S2).
- Even after `nfa_match` lands, no test SUITE passes until `match_with_anchors`
  is the real implementation (P1.M3.T2.S2), which is AFTER `is_word_boundary`
  is real (P1.M3.T1.S1). So the stub's `false` return is invisible to every
  test. Only the **empty-string guard** (`*string_start != '\0'`) is observable
  at this layer, and it is independent of the stub (short-circuits before the
  call). Verified: OP_ASSERT with empty string → 0 states for both `\b` and `\B`.

## nfa_gen self-resolution (verified)

Before this task: `nfa_gen defined but not used [-Wunused-variable]` (from
P1.M2.T1.S1). After appending `nfa_addstate` (which reads `nfa_gen`), that
warning **disappears** — exactly as the P1.M2.T1.S1 and P1.M2.T1.S2 PRPs
predicted ("self-resolves in P1.M2.T2.S1"). So the post-task warning set
shrinks by one (nfa_gen) and grows by one (nfa_addstate): net 3 warnings.

## Behavioral harness — ALL 8 CASES PASS

Built a `#include "pattern_match.c"` harness (Option D file) exercising:

1. plain OP_CHAR closure → exactly that 1 state, no recursion.
2. empty-pattern closure → OP_MATCH added (1 state).
3. glob `*` (SPLIT→ANY loop, SPLIT→out1=MATCH) → closure collects ANY + MATCH
   (2 states); ANY is consuming so we do NOT recurse through its loop-back.
4. **lastlist dedup (PRD §13 #11)**: manually-built SPLIT with `out == out1 ==
   shared` → shared added EXACTLY ONCE. (This is the infinite-recursion guard.)
5. re-adding an already-seen state (same nfa_gen) is a no-op.
6. NULL state guard: `nfa_addstate(NULL,...)` does not crash, does not touch `*n`.
7. **empty-string OP_ASSERT guard**: `*string_start == '\0'` → NEITHER `\b` NOR
   `\B` recurses (independent of the stub).
8. OP_ASSERT branch is wired: with the stub (`is_word_boundary()==false`), `\B`
   (want_boundary=false) recurses to MATCH; `\b` (want=true) does not. Proves
   abspos is forwarded and the branch recurses into `out`.

Result: `ALL CASES CONFIRMED (0 failures)`, exit 0.

## Append point

Current `pattern_match.c` is 386 lines, ending with `nfa_compile`'s closing
brace (P1.M2.T1.S2 just landed). This task APPENDS after that brace:
  (1) the `is_word_boundary` STUB, then
  (2) the `nfa_addstate` function + banner comment.
`nfa_gen` lives at line 293 (above nfa_compile); nfa_addstate reads it
(file-scope, visible). No edits above the append point.

## PRD / architecture anchors

- PRD §7.5 "Epsilon-closure (nfa_addstate)" — the EXACT contract (guard
  `lastlist == nfa_gen`; OP_SPLIT→both; OP_ASSERT→out only if
  `is_word_boundary(string_start, abspos) == want_boundary`, want=true for 0x0B;
  empty string → neither; OP_CHAR/OP_ANY→add).
- PRD §7.6 — `static bool is_word_boundary(const char *str, size_t pos);`
  signature (owned by P1.M3.T1.S1).
- PRD §13 invariant #11 — lastlist generation tag is MANDATORY (without it
  OP_SPLIT and `\b\b` recurse infinitely).
- PRD §13 invariant #10 — abspos is absolute (from string_start), forwarded
  unchanged through OP_SPLIT recursion (same abspos for both branches).
- Architecture doc §"NFA Simulation" → "nfa_addstate" + §"Global State"
  (nfa_gen is the sole mutable file-scope var; single-threaded in QMK).
- git `81df853` lines 127–147 — byte-for-behavior source of truth (PRD §17).

## Empty-string special case (Mode-A doc requirement)

The reference guards `*string_start != '\0' && is_word_boundary(...)`. On an
empty original string, the `&&` short-circuits BEFORE calling
is_word_boundary, so neither `\b` nor `\B` ever recurses — "legacy semantics"
the test suite encodes (architecture doc). This is independent of the stub and
is the one OP_ASSERT behavior fully testable at this layer.
