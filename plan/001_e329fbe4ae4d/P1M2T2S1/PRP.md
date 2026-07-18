# PRP — P1.M2.T2.S1: Implement `nfa_addstate()` epsilon-closure with lastlist guard

## Goal

**Feature Goal**: Append the **epsilon-closure function** `nfa_addstate()` to
`pattern_match.c`. It is the half of the Thompson-NFA simulator that walks all
epsilon transitions from a state — following `OP_SPLIT` forks and (conditionally)
`OP_ASSERT` zero-width edges — and collects the `OP_CHAR`/`OP_ANY`/`OP_MATCH`
states that are now "live" at a given input position. The function must
reproduce the live source-of-truth behavior **byte-for-byte** (git commit
`81df853` lines 127–147; PRD §7.5; architecture doc §"nfa_addstate"). Its
defining property is the **`lastlist == nfa_gen` generation-tag de-dup guard**
(PRD §13 invariant #11) — without it, `OP_SPLIT` and `\b\b` recurse infinitely.

**Deliverable**: A new `===== P1.M2.T2.S1: nfa_addstate() — epsilon-closure =====`
block APPENDED to the end of the current `pattern_match.c` (which, after
P1.M2.T1.S2, ends with `nfa_compile`'s closing brace). The block contains:

1. A **STUB** `is_word_boundary()` returning `false` (temporary — see *Why* and
   *Known Gotchas*; replaced by P1.M3.T1.S1), so the block links cleanly.
2. One `static` function:
   ```c
   static void nfa_addstate(State **list, int *n, State *s,
                            const char *string_start, size_t abspos);
   ```
   plus Mode-A inline comments on each `op` branch (item-spec §5 DOCS).

Nothing else is modified.

**Success Definition**:
- `nfa_addstate` present with the exact signature above, `static`, file-local.
  Consumes only `State`/`OP_*`/`nfa_gen` (from P1.M2.T1.S1) and the stubbed
  `is_word_boundary`; no new `#include`s; no new globals.
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0** with **exactly
  three** permitted warnings:
  - `nfa_addstate defined but not used` (new this task → self-resolves P1.M2.T2.S2)
  - `nfa_compile defined but not used` (carried M2.T1.S2 → self-resolves P1.M2.T2.S2)
  - `get_escaped_char defined but not used` (carried S2 → self-resolves P1.M3.T2.S1)
  - **`nfa_gen` is NO LONGER warned** (this task reads it — the warning S1/M2.T1.S2
    carried self-resolves here, exactly as those PRPs predicted).
- `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- A `#include "pattern_match.c"` harness (reaching the static `nfa_addstate`)
  confirms: `lastlist` de-dup (a convergent `OP_SPLIT` adds the shared target
  exactly once — PRD §13 #11); `OP_SPLIT` recurses both `out`/`out1`; `OP_MATCH`
  and `OP_CHAR`/`OP_ANY` are collected; the NULL-state guard; and the
  **empty-string `OP_ASSERT` guard** (`*string_start == '\0'` → neither `\b` nor
  `\B` recurses, independent of the stub).
- A committed test suite (e.g. `test_metachar_verification.c`) still **LINKS**
  cleanly (public API intact; the `is_word_boundary` stub makes the undefined
  reference go away).
- Each `op` branch carries a Mode-A inline comment; the infinite-recursion
  prevention mechanism and the empty-string special case are documented.

## User Persona (if applicable)

**Target User**: The NFA step loop written in the *next* subtask — `nfa_match()`
(P1.M2.T2.S2), the sole caller of `nfa_addstate`. End users and the public API
never call it directly (it is `static`, file-local to `pattern_match.c`).

**Use Case**: `nfa_match()` seeds its `clist` by calling
`nfa_addstate(clist, &cn, start, string_start, abspos)` after bumping `nfa_gen`;
after consuming each input char it builds `nlist` by calling
`nfa_addstate(nlist, &nn, s->out, string_start, pos+1)` for each advancing state
(P1.M2.T2.S2). None of that step loop exists yet — this task only produces the
closure primitive the simulator walks.

**User Journey**: `nfa_match` → bump `nfa_gen` →
`nfa_addstate(clist, &cn, start, string_start, abspos)` (seed closure) → per-char
step → `nfa_addstate(nlist, ...)` for each advancing state → `nfa_has_match`
(P1.M2.T2.S2). The `lastlist == nfa_gen` guard is what keeps each closure
visiting each reachable state at most once.

**Pain Points Addressed**: Replaces the unbounded epsilon recursion that would
otherwise occur on `OP_SPLIT` (glob `*`, `X+`) and repeated `\b`/`\B` assertions.
The generation tag (`nfa_gen`, bumped once per simulation phase) is the classic
Russ Cox "two arrays + a generation counter" trick — O(states) closure, no
allocation, no `memset` per phase.

## Why

- **First half of P1.M2.T2 (Simulation)**: P1.M2.T1 (S1+S2) produced the compiled
  graph; this task produces the closure primitive the simulator walks. After it,
  only the step loop (`nfa_match`) + the match-check (`nfa_has_match`) remain for
  a working NFA.
- **Correctness anchor — the lastlist guard is MANDATORY** (PRD §13 #11). Without
  `if (s->lastlist == nfa_gen) return;`, `OP_SPLIT` (whose two branches converge
  on a shared successor) and `\b\b` (two consecutive assertions at the same
  position) recurse infinitely. The guard turns closure into a single linear
  pass over the reachable states. This PRP makes it a Level-2 test gate.
- **The `is_word_boundary` dependency is real and MUST be handled** (verified
  empirically). `nfa_addstate` calls `is_word_boundary(string_start, abspos)` in
  the `OP_ASSERT` branch, but that helper is owned by **P1.M3.T1.S1** (a LATER
  task). A pure forward declaration FAILS the link gate (`undefined reference to
  is_word_boundary` — GCC emits the static `nfa_addstate` even when unused). The
  fix, mirroring the S2 `match_with_anchors` stub convention exactly, is a
  temporary STUB `is_word_boundary` returning `false`, placed immediately before
  `nfa_addstate` and REPLACED by P1.M3.T1.S1. The stub's value is never observed
  by any test (see *Known Gotchas*).
- **Rebuild integrity**: appends cleanly to the P1.M2.T1.S2 `nfa_compile` block;
  introduces exactly one *new* expected unused-function warning (`nfa_addstate`)
  and RETIRES the `nfa_gen` unused-variable warning (nfa_addstate reads it) —
  net change: −1 +1 = 3 warnings, all expected and self-resolving downstream.

## What

Append **two** items (plus a banner comment) to the end of `pattern_match.c`,
immediately after `nfa_compile`'s closing brace:

1. A **STUB** `is_word_boundary` returning `false` (clearly marked temporary,
   replaced by P1.M3.T1.S1). Signature exactly
   `static bool is_word_boundary(const char *str, size_t pos);` (PRD §7.6).
2. The function `nfa_addstate`, whose body (verbatim from the source of truth) is:
   - Guard `if (!s || s->lastlist == nfa_gen) return;` (de-dup + NULL-safe).
   - `s->lastlist = nfa_gen;` (mark seen for THIS closure).
   - `OP_MATCH` → `list[(*n)++] = s; return;` (accepting state collected).
   - `OP_SPLIT` → recurse into BOTH `s->out` and `s->out1` at the SAME `abspos`.
   - `OP_ASSERT` → `want_boundary = (s->arg == 0x0B)`; recurse into `s->out` ONLY
     if `*string_start != '\0' && is_word_boundary(string_start, abspos) == want_boundary`
     (empty original string → neither boundary nor non-boundary passes).
   - fall-through (`OP_CHAR` / `OP_ANY`) → `list[(*n)++] = s;` (waiting to consume a char).

Every branch carries a Mode-A inline comment (item-spec §5). No new `#include`s,
no new globals, no edits above the append point.

### Success Criteria

- [ ] `static void nfa_addstate(State **list, int *n, State *s, const char *string_start, size_t abspos)`
      present verbatim (reference body), file-local, appended after `nfa_compile`.
- [ ] The `is_word_boundary` STUB is present immediately before `nfa_addstate`,
      with the exact PRD §7.6 signature and a comment naming P1.M3.T1.S1 as its
      replacement.
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0; **exactly three**
      warnings (`nfa_addstate`, `nfa_compile`, `get_escaped_char` unused) — and
      `nfa_gen` is NOT among them.
- [ ] `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- [ ] Level-2 harness: `lastlist` dedup (convergent SPLIT → shared added once),
      `OP_SPLIT` both-branch recursion, `OP_MATCH`/`OP_CHAR`/`OP_ANY` collection,
      NULL guard, empty-string `OP_ASSERT` guard — all pass.
- [ ] `test_metachar_verification.c pattern_match.c` LINKS cleanly.
- [ ] Mode-A inline comments present on each `op` branch; the infinite-recursion
      prevention and empty-string special case are documented.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write is the live source of truth (`git show
81df853:pattern_match.c` lines 127–147; PRD §7.5 verbatim epsilon-closure
contract; architecture doc §"nfa_addstate") and is reproduced verbatim in
"Implementation Tasks" below (only Mode-A comments + the temporary stub are
authored here — all behavior-neutral per PRD §17). The build warning set (exit 0;
exactly three warnings — `nfa_addstate` new + `nfa_compile` + `get_escaped_char`
carried, with `nfa_gen` self-resolved), the silent `-fsyntax-only`, the clean
test-suite LINK, and all 8 behavioral cases (dedup, OP_SPLIT recursion,
OP_MATCH/CHAR/ANY collection, NULL guard, empty-string assertion guard,
OP_ASSERT branch wiring) were **all executed against a realistic post-P1.M2.T1.S2
merged file during research and passed**. An implementer with only this PRP + repo
access can produce the function behavior-identically and prove it.

### Documentation & References

```yaml
# MUST READ — authoritative spec for the epsilon-closure contract
- file: PRD.md
  section: "### 7.5 The NFA engine"  (Epsilon-closure (nfa_addstate) subsection)
  why: "Gives the EXACT contract this task must reproduce: 'guarded by
        lastlist == nfa_gen'; 'OP_SPLIT -> recurse into both out and out1 (same
        abspos)'; 'OP_ASSERT -> recurse into out ONLY IF
        is_word_boundary(string_start, abspos) == want_boundary (zero-width;
        want_boundary is true for 0x0B)'; 'Empty original string: neither
        boundary nor non-boundary (legacy semantics)'."
  critical: "The guard order matters: set s->lastlist = nfa_gen IMMEDIATELY after
        the guard and BEFORE the op dispatch, so a state is marked seen even if
        it ends up just being collected (OP_CHAR/ANY/MATCH) — that is what makes
        a convergent OP_SPLIT add its shared successor exactly once."

- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve"  (invariant #11)
  why: "#11: 'The lastlist generation tag is mandatory in the NFA — without it,
        OP_SPLIT and \\b\\b recurse infinitely.' This is THE reason the function
        exists in this exact shape."
  critical: "Also invariant #10: abspos is ABSOLUTE (from string_start), and is
        forwarded UNCHANGED through OP_SPLIT recursion (both branches get the
        same abspos). Do not increment abspos inside nfa_addstate."

- file: PRD.md
  section: "### 7.6 Character classification helpers (static)"
  why: "Gives the EXACT signature of the helper nfa_addstate calls:
        'static bool is_word_boundary(const char *str, size_t pos);' and its
        contract (pos==0 -> str[0] word; pos==strlen -> str[len-1] word;
        pos>strlen -> false; interior -> word(prev) != word(cur); NULL -> false).
        Owned by P1.M3.T1.S1 — this task provides a temporary STUB."
  critical: "The stub signature MUST match exactly (size_t pos, not int) so
        P1.M3.T1.S1 can drop in the real body without touching the call site.
        The stub returns false; this is never observed by any test (see Known
        Gotchas)."

- file: PRD.md
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"
  why: "'the code + the passing tests win' — git 81df853 pattern_match.c is
        authoritative. Reproduce nfa_addstate byte-for-behavior; comment drift is
        tolerated, logic is not."
  critical: "If in doubt, `git show 81df853:pattern_match.c | sed -n '127,147p'`
        is the source of truth for this function."

# Architecture — the algorithm narrative + the generation-tag mechanism
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## NFA Simulation (nfa_match)" -> "### nfa_addstate"
  why: "Spells out each op's closure behavior and WHY lastlist exists:
        'The lastlist generation tag is MANDATORY — without it, OP_SPLIT and
        \\b\\b recurse infinitely. Each simulation phase bumps nfa_gen++ so
        closure de-dup works.' Also documents the empty-string special case
        ('Empty original string: neither boundary nor non-boundary')."
  critical: "Confirms the caller contract: nfa_match bumps nfa_gen BEFORE the
        first nfa_addstate call. nfa_addstate itself NEVER bumps nfa_gen — it
        only reads it. Do not add nfa_gen++ here."

# Dependency PRPs — what exists when this task starts (CONTRACTS)
- file: plan/001_e329fbe4ae4d/P1M2T1S1/PRP.md
  section: "## Implementation Blueprint" (the definitions block)
  why: "M2.T1.S1 defines: NFA_MAX_PATTERN=128, NFA_MAX_STATES=258, enum
        {OP_CHAR,OP_ANY,OP_SPLIT,OP_ASSERT,OP_MATCH}, typedef struct State State;
        struct State{int op;char arg;State *out;State *out1;int lastlist;};
        static int nfa_gen = 0;. nfa_addstate consumes all of these."
  critical: "Treat M2.T1.S1 as a CONTRACT: assume State/OP_*/nfa_gen all exist
        with those exact names. This task READS nfa_gen (retiring its unused
        warning) and reads/writes s->lastlist (the State field)."

- file: plan/001_e329fbe4ae4d/P1M2T1S2/PRP.md
  section: "## Goal" + "## Implementation Blueprint"
  why: "M2.T1.S2 (COMPLETE / implementing in parallel) appends nfa_compile after
        the nfa_gen line. This task APPENDS after nfa_compile's closing brace.
        M2.T1.S2 also established the 'expected unused warning that self-resolves
        downstream' convention this task reuses for nfa_addstate, and confirmed
        nfa_compile zeroes lastlist on every allocated state (so the FIRST
        closure with nfa_gen==0 is correct)."
  critical: "Do NOT modify nfa_compile's content. APPEND ONLY, after nfa_compile.
        nfa_compile guarantees pool states start with lastlist==0; combined with
        nfa_gen starting at 0, the very first closure (nfa_gen still 0 after the
        caller bumps it to 1) marks states correctly. (The caller bumps nfa_gen
        to 1 before the first addstate; states' lastlist==0 != 1, so they are
        visited. Good.)"

- file: plan/001_e329fbe4ae4d/P1M1T2S2/PRP.md
  section: "## Implementation Blueprint" (match_with_anchors STUB)
  why: "S2 set the precedent this task FOLLOWS for is_word_boundary: S2 provided a
        temporary STUB match_with_anchors returning false (replaced by
        P1.M3.T2.S2) so the file links cleanly while the real implementation is
        pending. This task does the SAME for is_word_boundary (replaced by
        P1.M3.T1.S1)."
  critical: "Copy the stub idiom verbatim from S2's match_with_anchors stub:
        (void)-cast the unused params, return false, comment naming the replacing
        task. Do NOT suppress with __attribute__((unused))."

# Downstream consumer (informational; NOT this task)
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## NFA Simulation (nfa_match)"
  why: "Shows how nfa_match calls nfa_addstate to seed clist and to build nlist
        each step, bumping nfa_gen once per phase. This task's output (a populated
        list of live states) is exactly what that caller consumes."
  critical: "nfa_addstate does NOT bump nfa_gen. The caller owns that. Do not add
        nfa_gen++ here (would double-bump and break de-dup)."

# Downstream replacer of the stub (informational; NOT this task)
- file: plan/001_e329fbe4ae4d/  (P1.M3.T1.S1 task)
  why: "P1.M3.T1.S1 'Implement is_digit_char, is_word_char, is_whitespace_char,
        is_word_boundary' will REPLACE this task's is_word_boundary stub with the
        real position-based boundary test. The stub's exact signature
        (size_t pos) makes that a drop-in body swap."
  critical: "This task OWNS the stub until P1.M3.T1.S1. The replacing task must
        REMOVE the stub body and insert the real implementation at the same site
        (or relocate per its own plan) — the call site in nfa_addstate must not
        change."

# Live reference implementation (the byte-for-behavior source of truth, PRD §17)
- file: git commit 81df853 ("implemented nfa matching engine")
  why: "HEAD's pattern_match.c was RESET to process_escapes-only for the rebuild;
        the full nfa_addstate lives in history at 81df853 lines 127-147."
  how:  "git show 81df853:pattern_match.c | sed -n '127,147p'"
  critical: "Reproduce the guard order, the unconditional lastlist set, the
        OP_SPLIT double recursion (same abspos), the OP_ASSERT empty-string
        short-circuit, and the OP_CHAR/ANY fall-through EXACTLY. Comment wording
        may drift (PRD §17); logic may not."

# Build convention
- file: run_all_tests.sh
  why: "Toolchain is plain gcc, no make. Each suite is `gcc -o test_X test_X.c
        pattern_match.c`; suites #include \"pattern_match.h\" and call only the
        public pattern_match()."
  critical: "nfa_addstate is static/file-local, so it is validated by compiling
        pattern_match.c (Level 1) + a #include-harness (Level 2) + one LINK
        check (Level 3) — NOT by a committed test file."

# External theory (informational)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast' — the
        Thompson-NFA design this engine follows, including the generation-counter
        de-dup trick this function implements. Mode-A banner should reference it."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # S2 + M2.T1.S1 + M2.T1.S2 COMPLETE (386 lines):
                       #   includes + process_escapes + parsed_pattern_t +
                       #   get_escaped_char + free_parsed_pattern + parse_pattern +
                       #   match_with_anchors STUB + pattern_match (public) +
                       #   #define NFA_MAX_PATTERN/NFA_MAX_STATES +
                       #   enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH } +
                       #   typedef struct State State; struct State {...} +
                       #   static int nfa_gen = 0; +
                       #   static State *nfa_compile(...)   <- current EOF
                       #   THIS task APPENDS: is_word_boundary STUB + nfa_addstate().
notifier.h notifier.c  # P2 scope — do not touch.
rules.mk               # P2 scope — do not touch.
test_*.c               # P1.M4 scope — do not touch (only used to prove LINK).
run_all_tests.sh       # P1.M4 scope — do not touch.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # THIS task APPENDS one stub + one function. After M2.T1.S2
                       # it additionally contains:
                       #   - static bool is_word_boundary(const char *str, size_t pos)
                       #       STUB returning false (temp; replaced by P1.M3.T1.S1)
                       #   - static void nfa_addstate(State **list, int *n, State *s,
                       #       const char *string_start, size_t abspos)
                       #       (epsilon-closure; consumes State/OP_*/nfa_gen + the stub)
                       # Later subtasks APPEND after this function:
                       #   P1.M2.T2.S2 -> nfa_has_match() + nfa_match() (CALLS nfa_addstate)
                       #   P1.M3.T1.S1 -> REPLACES the is_word_boundary stub with the real impl
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — is_word_boundary MUST be defined (not just declared) in this task.
//   nfa_addstate calls it in the OP_ASSERT branch. It is owned by P1.M3.T1.S1
//   (a LATER task), so it does not exist yet. A pure forward declaration FAILS:
//     /usr/bin/ld: undefined reference to `is_word_boundary'
//   (VERIFIED during research). GCC emits the static nfa_addstate even though it
//   is unused (that's why -Wunused-function fires), so the linker tries to
//   resolve is_word_boundary. The fix — REQUIRED, not optional — is a temporary
//   STUB returning false, placed immediately before nfa_addstate. This mirrors
//   S2's match_with_anchors stub (replaced by P1.M3.T2.S2) EXACTLY; here the
//   stub is replaced by P1.M3.T1.S1. After THIS task the permitted-warning set
//   is EXACTLY:
//     * 'get_escaped_char defined but not used [-Wunused-function]' (carried, P1.M3.T2.S1)
//     * 'nfa_compile defined but not used [-Wunused-function]'      (carried, P1.M2.T2.S2)
//     * 'nfa_addstate defined but not used [-Wunused-function]'     (NEW,    P1.M2.T2.S2)
//   and 'nfa_gen defined but not used' is GONE (nfa_addstate reads it now — the
//   warning S1/M2.T1.S2 carried self-resolves in THIS task, as predicted).

// CRITICAL — the stub's value (false) is NEVER observed by any test. nfa_addstate
//   is dead code until nfa_match exists (P1.M2.T2.S2); even then, no test suite
//   passes until match_with_anchors is real (P1.M3.T2.S2), which is AFTER
//   is_word_boundary is real (P1.M3.T1.S1). So the stub's false return is
//   invisible. The ONLY OP_ASSERT behavior observable at this layer is the
//   EMPTY-STRING guard (*string_start != '\0'), which short-circuits BEFORE the
//   call and is therefore independent of the stub. Verified.

// CRITICAL — do NOT bump nfa_gen inside nfa_addstate. The caller (nfa_match,
//   P1.M2.T2.S2) bumps nfa_gen ONCE per simulation phase (clist seed + once per
//   consumed char). nfa_addstate only READS nfa_gen. Adding nfa_gen++ here would
//   double-bump and silently break de-dup (a state seen in the current phase
//   would be re-visited after the spurious bump). (Verified against the
//   reference: nfa_addstate has no nfa_gen++ anywhere.)

// GOTCHA — the guard sets s->lastlist = nfa_gen UNCONDITIONALLY (right after the
//   dedup check), BEFORE the op dispatch. This is essential: a state reached via
//   an OP_SPLIT branch must be marked seen EVEN IF it is an OP_CHAR/ANY/MATCH
//   that just gets collected — otherwise a SECOND OP_SPLIT branch converging on
//   the same state would re-add it (PRD §13 #11 violation). Set-then-dispatch.

// GOTCHA — OP_SPLIT recurses into BOTH out AND out1 with the SAME abspos. abspos
//   is absolute (from string_start, PRD §13 #10) and is NOT incremented inside
//   nfa_addstate (no input is consumed on an epsilon edge). Pass abspos through
//   verbatim to both recursive calls and to is_word_boundary.

// GOTCHA — OP_ASSERT empty-string guard: the reference writes
//     if (*string_start != '\0' && is_word_boundary(string_start, abspos) == want_boundary)
//   The `*string_start != '\0'` check MUST come FIRST (short-circuit). On an
//   empty original string neither \b nor \B passes ("legacy semantics" the test
//   suite encodes). Dropping the empty-string check, or reversing the && order,
//   changes observable behavior once is_word_boundary is real. Keep the order.

// GOTCHA — want_boundary is `(s->arg == 0x0B)`. arg is a `char`; comparing to
//   0x0B works because 0x0B/0x0C are positive (no sign-extension issue). Do not
//   cast arg to unsigned char here — the reference does not, and 0x0B < 0x80 so
//   it is fine. (Contrast with nfa_compile, which DID cast *p to unsigned char
//   because arbitrary pattern bytes can be high-bit-set.)

// GOTCHA — `list[(*n)++] = s;` post-increments *n. Use (*n)++ (parens required:
//   *n++ would increment the POINTER, not the int). The reference uses (*n)++.

// GOTCHA — do NOT add #includes. nfa_addstate uses only State/OP_*/nfa_gen (from
//   M2.T1.S1), size_t (already in scope via <string.h>/<stdlib.h> from S1), bool
//   (<stdbool.h> from S1), and built-in int/char/pointer types. No <stddef.h>,
//   <stdint.h>, <stdio.h>, or "notifier.h". size_t is already available because
//   pattern_match.c includes <string.h> (which defines size_t) at the top.

// GOTCHA — the OP_ASSERT fall-through after a FAILED boundary check must `return;`
//   (not fall through to the OP_CHAR/ANY collector). The reference has an
//   explicit `return;` at the end of the OP_ASSERT branch. Without it, a failed
//   \b/\B would fall through and WRONGLY add the ASSERT state to the list.

// GOTCHA — the NULL-state guard `if (!s || ...)` is load-bearing for OP_SPLIT
//   whose out1 can be NULL in malformed/edge NFAs (nfa_compile always sets out1
//   to a real successor, but defensive coding matches the reference and keeps a
//   stray NULL from crashing the recursion).
```

## Implementation Blueprint

### Data models and structure

No new data models. This task consumes the `State`/`OP_*`/`nfa_gen` definitions
from P1.M2.T1.S1 and the `is_word_boundary` signature from PRD §7.6, and adds:

```c
/* The types/signatures consumed (already defined by P1.M2.T1.S1 / PRD §7.6): */
enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };
struct State { int op; char arg; State *out; State *out1; int lastlist; };
static int nfa_gen = 0;
/* static bool is_word_boundary(const char *str, size_t pos);  <- PRD §7.6, P1.M3.T1.S1 */
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: APPEND the is_word_boundary STUB + nfa_addstate() to pattern_match.c
  - PLACE: immediately AFTER nfa_compile's closing brace (current EOF, ~line 386).
  - PREFIX the block with a banner comment referencing PRD §7.5/§13 #11 + Russ Cox.
  - ITEM A (STUB): static bool is_word_boundary(const char *str, size_t pos)
      body: (void)str; (void)pos; return false;   (+ comment naming P1.M3.T1.S1).
      This MUST come BEFORE nfa_addstate so the call site sees the declaration.
  - ITEM B (FUNCTION): static void nfa_addstate(State **list, int *n, State *s,
      const char *string_start, size_t abspos) — the reference body verbatim
      (see "The exact code to write" below), including:
        * the guard `if (!s || s->lastlist == nfa_gen) return;`
        * `s->lastlist = nfa_gen;` (unconditional, before dispatch)
        * OP_MATCH -> list[(*n)++] = s; return;
        * OP_SPLIT -> recurse out AND out1 (same abspos); return;
        * OP_ASSERT -> want_boundary = (s->arg == 0x0B);
                       if (*string_start != '\0' &&
                           is_word_boundary(string_start, abspos) == want_boundary)
                           nfa_addstate(list, n, s->out, string_start, abspos);
                       return;
        * fall-through -> list[(*n)++] = s;   (OP_CHAR / OP_ANY)
      each branch with its Mode-A comment.
  - NAMING: nfa_addstate (static, snake_case); list/n/s/string_start/abspos params
    (snake_case, matching the reference + item-spec §3).
  - DEPENDENCIES: State, OP_*, nfa_gen (all from P1.M2.T1.S1 — CONTRACT); the
    is_word_boundary stub (this task, replaced by P1.M3.T1.S1).
  - PRESERVE: all existing content (S1 includes + process_escapes; S2 pipeline +
    pattern_match; M2.T1.S1 defs; M2.T1.S2 nfa_compile). APPEND ONLY — do not
    move, reorder, or edit anything above.
  - DO NOT: add #includes, add globals, bump nfa_gen, implement nfa_match/
    nfa_has_match, suppress the expected nfa_addstate unused-function warning,
    or make is_word_boundary real (that is P1.M3.T1.S1).

Task 2: VERIFY the build + link gates (run the Validation Loop, Levels 1, 2, 3, 4).
```

**The exact code to write** (verbatim from `git show 81df853:pattern_match.c`
lines 127–147, source of truth per PRD §17; the stub + Mode-A branch comments
are authored here — all behavior-neutral). Append after `nfa_compile`'s closing
brace:

```c
/* ===== P1.M2.T2.S1: nfa_addstate() — epsilon-closure with lastlist guard =====
 * Epsilon-closure primitive for the Thompson-NFA simulator. Given a state `s`,
 * follow all epsilon transitions (OP_SPLIT forks and — conditionally — OP_ASSERT
 * zero-width edges) and collect every OP_CHAR / OP_ANY / OP_MATCH state that is
 * "live" at the current input position into `list[*n .. )`.
 *
 * THE lastlist GUARD IS MANDATORY (PRD §13 invariant #11): without
 * `if (s->lastlist == nfa_gen) return;`, an OP_SPLIT whose two branches converge
 * on a shared successor (and repeated \b/\B at one position) would recurse
 * infinitely. The generation tag `nfa_gen` is bumped ONCE per simulation phase
 * by the caller (nfa_match, P1.M2.T2.S2), so each closure gets a fresh "seen"
 * set at O(1) cost — no memset, no allocation. See Russ Cox, "Regular Expression
 * Matching Can Be Simple And Fast", https://swtch.com/~rsc/regexp/regexp1.html .
 *
 * NOTE on is_word_boundary: the real classifier is implemented in P1.M3.T1.S1.
 * A temporary STUB (returning false) is provided immediately below so this
 * translation unit links cleanly while that task is pending; nfa_addstate is
 * itself dead code until nfa_match (P1.M2.T2.S2) lands. P1.M3.T1.S1 REPLACES
 * the stub body with the real position-based boundary test; the signature is
 * fixed by PRD §7.6 so the call site below never changes. */

/* STUB — is_word_boundary() is fully implemented in P1.M3.T1.S1. Provided here
 * (returning false) ONLY so nfa_addstate (this task) links cleanly while the
 * real classifier is still pending; nfa_addstate is itself dead code until
 * nfa_match (P1.M2.T2.S2) lands. P1.M3.T1.S1 REPLACES this stub with the real
 * position-based boundary test. Signature fixed by PRD §7.6. */
static bool is_word_boundary(const char *str, size_t pos) {
    (void)str;
    (void)pos;
    return false;
}

/* ---- epsilon-closure add (follow SPLIT/ASSERT, collect CHAR/ANY/MATCH) ---- */
static void nfa_addstate(State **list, int *n, State *s,
                         const char *string_start, size_t abspos) {
    /* De-dup + NULL-safe: skip if `s` is NULL or already in THIS closure.
     * lastlist == nfa_gen means we have already added/followed `s` during the
     * current simulation phase (nfa_gen is bumped once per phase by nfa_match).
     * This single guard is what makes OP_SPLIT and \b\b terminate (PRD §13 #11). */
    if (!s || s->lastlist == nfa_gen) return;     /* already in this closure */

    /* Mark seen for THIS generation BEFORE dispatching, so a state reached via an
     * OP_SPLIT branch is not re-added when the OTHER branch converges on it. */
    s->lastlist = nfa_gen;

    if (s->op == OP_MATCH) {
        /* Accepting state: collect it; nfa_has_match (P1.M2.T2.S2) reports the
         * match by scanning the list for an OP_MATCH. */
        list[(*n)++] = s;
        return;
    }

    if (s->op == OP_SPLIT) {
        /* Epsilon fork (glob '*', 'X+'): follow BOTH out and out1 WITHOUT
         * consuming input. abspos is forwarded UNCHANGED to both branches
         * (PRD §13 #10: abspos is absolute from string_start; epsilon edges do
         * not advance the input position). */
        nfa_addstate(list, n, s->out,  string_start, abspos);
        nfa_addstate(list, n, s->out1, string_start, abspos);
        return;
    }

    if (s->op == OP_ASSERT) {
        /* Zero-width assertion \b (arg==0x0B, want a boundary) / \B (arg==0x0C,
         * want a NON-boundary). Recurse into `out` ONLY if the boundary
         * condition holds. abspos is absolute (PRD §13 #10) so \b/\B evaluate
         * against the ORIGINAL string, not the per-offset pointer.
         *
         * EMPTY-STRING SPECIAL CASE (legacy semantics the test suite encodes):
         * if the original string is empty (*string_start == '\0'), NEITHER a
         * boundary nor a non-boundary passes, so we do NOT recurse. The empty-
         * string check short-circuits BEFORE calling is_word_boundary, so this
         * behavior is independent of the is_word_boundary implementation. */
        int want_boundary = (s->arg == 0x0B);     /* \b wants a boundary; \B wants none */
        if (*string_start != '\0' &&
            is_word_boundary(string_start, abspos) == want_boundary)
            nfa_addstate(list, n, s->out, string_start, abspos);
        return;                                   /* never collect an ASSERT state itself */
    }

    /* OP_CHAR / OP_ANY: a consuming state. Add it to the list; it is "live" and
     * waiting for the simulator to feed it the next input char (nfa_match,
     * P1.M2.T2.S2). OP_ANY (glob '*') matches any byte incl. '\n'/'\r'; OP_CHAR
     * is tested via pattern_char_matches (P1.M3.T2.S1). */
    list[(*n)++] = s;
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: generation-tag de-dup (Russ Cox). The caller bumps nfa_gen once per
//   simulation phase; each state carries a `lastlist` int. `s->lastlist ==
//   nfa_gen` means "already processed this phase." This gives O(states) closure
//   with zero allocation and no per-phase memset. The guard is the WHOLE point
//   of the function (PRD §13 #11).

// PATTERN: set-then-dispatch. `s->lastlist = nfa_gen;` happens unconditionally
//   right after the guard, BEFORE the op switch. This guarantees a convergent
//   OP_SPLIT adds its shared successor exactly once, regardless of the op.

// PATTERN: epsilon edges forward abspos unchanged. OP_SPLIT and OP_ASSERT do NOT
//   consume input, so abspos (absolute offset from string_start) is passed
//   through verbatim. Only nfa_match advances the position (pos+1) when it
//   consumes a char.

// PATTERN: stub-then-replace (from S2). When a static function calls a helper
//   owned by a LATER task, provide a minimal STUB returning a neutral value
//   (false) so the TU links cleanly; the later task replaces the body. The
//   stub's signature is fixed by the PRD so the call site never changes.

// ANTI-PATTERN: do NOT bump nfa_gen here. The caller (nfa_match) owns the
//   per-phase bump. Incrementing here would double-bump and silently break de-dup.

// ANTI-PATTERN: do NOT forward-declare is_word_boundary without a body. A bare
//   `static bool is_word_boundary(...);` with no definition makes the LINK fail
//   (undefined reference) because GCC emits the static nfa_addstate even though
//   it is unused. You MUST provide the stub body. (Verified empirically.)

// ANTI-PATTERN: do NOT make is_word_boundary real. Its full position-based logic
//   (pos==0/strlen/interior, is_word_char) is P1.M3.T1.S1's job; reproducing it
//   here is scope creep and creates a conflict at that task. A `return false`
//   stub is correct and sufficient for THIS task's gates.

// ANTI-PATTERN: do NOT drop the `return;` at the end of the OP_ASSERT branch.
//   Without it, a failed \b/\B falls through to the OP_CHAR/ANY collector and
//   WRONGLY adds the ASSERT state to the list.

// ANTI-PATTERN: do NOT reverse the OP_ASSERT && order. `*string_start != '\0'`
//   MUST come first so the empty-string short-circuit happens before the
//   is_word_boundary call. Reversing it changes observable behavior once the
//   real is_word_boundary lands.

// ANTI-PATTERN: do NOT suppress the nfa_addstate unused-function warning with
//   __attribute__((unused)). Accept it; it self-resolves in P1.M2.T2.S2 when
//   nfa_match calls nfa_addstate. (Same convention S2/M2.T1.S1/S2 used.)

// ANTI-PATTERN: do NOT implement nfa_match / nfa_has_match here — they are
//   P1.M2.T2.S2. One stub + one function only.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - APPEND one stub + one function (+ banner comment) at EOF of pattern_match.c,
    immediately after nfa_compile's closing brace (the M2.T1.S2 block).
  - No insertion into existing code; no reordering; no edits above the append point.

CONSUMERS (downstream, NOT this task):
  - list / *n  <- nfa_match() seeds clist and builds nlist per step via
                  nfa_addstate [P1.M2.T2.S2]
  - nfa_gen    <- READ here; bumped by nfa_match (this task RETIRES its unused
                  warning)

REPLACED (downstream, NOT this task):
  - is_word_boundary stub <- P1.M3.T1.S1 replaces the body with the real impl
                  (signature unchanged; call site unchanged)

BUILD:
  - No build-system change. Plain gcc (run_all_tests.sh style). Validate by
    compiling pattern_match.c (Level 1) + a #include harness (Level 2) + a
    single-suite LINK (Level 3).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; pure closure function, no runtime effect until the
    simulator in P1.M2.T2.S2 is added and is_word_boundary is real in P1.M3.T1.S1).
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. `nfa_addstate` is `static`/file-local,
> so it is reached via `#include "pattern_match.c"` in a throwaway harness (the
> committed suites only call the public `pattern_match()`). All commands were
> VERIFIED during research against a realistic post-P1.M2.T1.S2 merged file.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o
# Expected: exit 0. EXACTLY THREE warnings are permitted and expected:
#   warning: 'nfa_addstate' defined but not used [-Wunused-function]   (new this task -> P1.M2.T2.S2)
#   warning: 'nfa_compile' defined but not used [-Wunused-function]    (carried M2.T1.S2 -> P1.M2.T2.S2)
#   warning: 'get_escaped_char' defined but not used [-Wunused-function] (carried S2 -> P1.M3.T2.S1)
# FAIL if: exit != 0, OR 'nfa_gen defined but not used' appears (it MUST be gone
#           now — nfa_addstate reads it), OR any OTHER warning appears, OR
#           process_escapes/parse_pattern appear in the warning list.

# 1b. Syntax-only (silent — fsyntax-only does not emit unused warnings).
gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c
# Expected: exit 0, NO output.

# 1c. Confirm nfa_addstate is present, static, with the exact signature.
grep -nE 'static void nfa_addstate\(State \*\*list, int \*n, State \*s,' pattern_match.c
grep -nE 'const char \*string_start, size_t abspos\)' pattern_match.c
# Expected: each prints exactly ONE line.

# 1d. Confirm the is_word_boundary STUB is present with the exact PRD §7.6 signature.
grep -nE 'static bool is_word_boundary\(const char \*str, size_t pos\)' pattern_match.c
# Expected: exactly ONE match (the stub). Confirm it returns false and is marked STUB:
grep -nA4 'static bool is_word_boundary' pattern_match.c | grep -q 'return false' \
  && echo "stub returns false (ok)" || echo "ERROR: stub body wrong"

# 1e. Confirm the guard + set-then-dispatch order + empty-string short-circuit.
grep -nE 'if \(!s \|\| s->lastlist == nfa_gen\) return;' pattern_match.c
grep -nE 's->lastlist = nfa_gen;' pattern_match.c
grep -nE '\*string_start != .\\0. &&' pattern_match.c
# Expected: each prints exactly ONE line, in source order (guard, set, assert-check).

# 1f. Confirm NO new #includes were added (still exactly stdbool/string/stdlib).
grep -nE '^#include' pattern_match.c
# Expected: <stdbool.h>, <string.h>, <stdlib.h> (the S1 set) — nothing else.

rm -f /tmp/pm.o
```

### Level 2: Component Tests (THE PRIMARY BEHAVIORAL GATE)

This harness was **verified against the source-of-truth reference** during
research (all 8 cases pass). Create it, run it, require all-pass. It reaches the
static `nfa_addstate` by `#include`-ing the `.c`.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/s1_addstate_test.c <<'EOF'
/* Reach the static nfa_addstate + nfa_compile + State / OP enums / nfa_gen
 * by including the .c directly. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pattern_match.c"

static int failures = 0;
#define CK(cond, fmt, ...) do { if (cond) printf("ok   %s\n", desc); \
  else { printf("FAIL %s " fmt "\n", desc, ##__VA_ARGS__); failures++; } } while(0)

int main(void) {
    /* 1. epsilon-closure of a plain OP_CHAR adds exactly that state (no recursion). */
    { const char *desc = "plain CHAR closure";
      State pool[NFA_MAX_STATES]; int nstates=-1;
      State *start = nfa_compile("abc", pool, &nstates);   /* CHAR(a)->CHAR(b)->CHAR(c)->MATCH */
      State *list[32]; int n=0; nfa_gen++;                 /* caller bumps nfa_gen first */
      nfa_addstate(list, &n, start, "abc", 0);
      CK(n==1 && list[0]==start && list[0]->op==OP_CHAR, "n=%d want 1", n);
    }

    /* 2. epsilon-closure of OP_MATCH adds the match state. */
    { const char *desc = "MATCH closure (empty pattern)";
      State pool[NFA_MAX_STATES]; int nstates=-1;
      State *start = nfa_compile("", pool, &nstates);      /* MATCH only */
      State *list[32]; int n=0; nfa_gen++;
      nfa_addstate(list, &n, start, "", 0);
      CK(n==1 && list[0]->op==OP_MATCH, "n=%d want 1 (MATCH)", n);
    }

    /* 3. OP_SPLIT recursion (glob '*'): closure of SPLIT reaches BOTH the ANY
     *    (consuming) and the MATCH (the zero-width exit). ANY->out loops back to
     *    SPLIT but ANY is consuming, so we must NOT recurse through it. */
    { const char *desc = "glob * SPLIT closure -> ANY+MATCH";
      State pool[NFA_MAX_STATES]; int nstates=-1;
      State *start = nfa_compile("\x2A", pool, &nstates);  /* SPLIT{out=ANY(loop),out1=MATCH} */
      State *list[32]; int n=0; nfa_gen++;
      nfa_addstate(list, &n, start, "xyz", 0);
      int any_cnt=0, match_cnt=0, split_cnt=0;
      for (int i=0;i<n;i++){ if(list[i]->op==OP_ANY)any_cnt++; if(list[i]->op==OP_MATCH)match_cnt++; if(list[i]->op==OP_SPLIT)split_cnt++; }
      CK(n==2 && any_cnt==1 && match_cnt==1 && split_cnt==0, "n=%d any=%d match=%d split=%d", n,any_cnt,match_cnt,split_cnt);
    }

    /* 4. *** lastlist DEDUP (PRD §13 invariant #11) ***: a SPLIT whose out AND
     *    out1 reach the SAME state must add that shared state EXACTLY ONCE. */
    { const char *desc = "dedup: SPLIT out==out1 -> shared added once";
      State pool[4];
      State *shared = &pool[0]; shared->op=OP_CHAR; shared->arg='x'; shared->out=0; shared->out1=0; shared->lastlist=0;
      State *sp = &pool[1]; sp->op=OP_SPLIT; sp->out=shared; sp->out1=shared; sp->lastlist=0;
      State *list[32]; int n=0; nfa_gen++;
      nfa_addstate(list, &n, sp, "abc", 0);
      CK(n==1 && list[0]==shared, "n=%d want 1 (shared dedup'd)", n);
    }

    /* 5. re-adding an already-seen state (same generation) is a no-op. */
    { const char *desc = "re-addstate already-seen is no-op";
      State pool[NFA_MAX_STATES]; int nstates=-1;
      State *start = nfa_compile("a", pool, &nstates);     /* CHAR(a)->MATCH */
      State *list[32]; int n=0; nfa_gen++;
      nfa_addstate(list, &n, start, "abc", 0);
      nfa_addstate(list, &n, start, "abc", 0);             /* already seen */
      CK(n==1, "n=%d want 1 (second addstate was no-op)", n);
    }

    /* 6. NULL state guard: nfa_addstate(NULL,...) is a safe no-op. */
    { const char *desc = "NULL state guard";
      State *list[32]; int n=99; nfa_gen++;
      nfa_addstate(list, &n, NULL, "abc", 0);              /* must not crash, must not touch n */
      CK(n==99, "n=%d want 99 (NULL is no-op)", n);
    }

    /* 7. *** EMPTY-STRING OP_ASSERT GUARD ***: when *string_start=='\0', NEITHER
     *    \b NOR \B recurses (legacy semantics). Independent of the stub. */
    { const char *desc = "ASSERT empty-string: neither passes";
      State pool[NFA_MAX_STATES]; int nstates=-1;
      State *start_b = nfa_compile("\x0B", pool, &nstates);   /* ASSERT(0x0B)->MATCH */
      State pool2[NFA_MAX_STATES]; int nstates2=-1;
      State *start_B = nfa_compile("\x0C", pool2, &nstates2);  /* ASSERT(0x0C)->MATCH */
      State *list[32]; int n=0; nfa_gen++;
      nfa_addstate(list, &n, start_b, "", 0);              /* empty string */
      nfa_addstate(list, &n, start_B, "", 0);
      CK(n==0, "n=%d want 0 (empty string: no boundary, no non-boundary)", n);
    }

    /* 8. OP_ASSERT branch is wired + forwards abspos. With the stub returning
     *    false, \B (want_boundary=false) recurses (false==false); \b does not.
     *    Proves the branch recurses into `out`. (Real semantics: P1.M3.T1.S1.) */
    { const char *desc = "ASSERT branch wired (\\B recurses via stub)";
      State pool[NFA_MAX_STATES]; int nstates=-1;
      State *start_B = nfa_compile("\x0C", pool, &nstates);  /* ASSERT(0x0C)->MATCH */
      State *list[32]; int n=0; nfa_gen++;
      nfa_addstate(list, &n, start_B, "word", 0);          /* non-empty -> guard passes */
      CK(n==1 && list[0]->op==OP_MATCH, "n=%d want 1 (B recursed via stub)", n);
    }

    printf("\n%s (%d failures)\n", failures?"SOME FAILURES":"ALL CASES CONFIRMED", failures);
    return failures ? 1 : 0;
}
EOF

gcc -Wall -Wextra -std=c99 -I. /tmp/s1_addstate_test.c -o /tmp/s1_addstate_test 2>&1 | grep -vE 'nfa_addstate|nfa_compile|get_escaped_char|nfa_gen'
/tmp/s1_addstate_test
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
# (The only permitted compiler warnings are the three expected unused-symbol ones,
#  filtered out above. The CRITICAL gates are the dedup case (#4) and the
#  empty-string assertion guard (#7).)

rm -f /tmp/s1_addstate_test.c /tmp/s1_addstate_test
```

### Level 3: Integration Testing (API Integrity — LINK, not run)

```bash
cd /home/dustin/projects/qmk-notifier

# The committed suites call only the public pattern_match(). After this task the
# symbol still exists and nfa_addstate + the is_word_boundary stub are file-local,
# so a suite LINKS cleanly — proving nothing in the public path broke AND that the
# is_word_boundary stub resolved the otherwise-undefined reference. (It will not
# PASS yet: match_with_anchors is still the S2 stub; real matching is P1.M3.T2.S2.)
gcc -Wall test_metachar_verification.c pattern_match.c -o /tmp/tm 2>&1 \
  | grep -v 'get_escaped_char\|nfa_gen\|nfa_compile\|nfa_addstate'
# Expected: empty output (only the three known warnings, filtered above).
echo "link exit (expect 0): ${PIPESTATUS[0]}"
rm -f /tmp/tm

# DO NOT run run_all_tests.sh to validate this task — the stub matcher makes
# every true-expecting case fail by design. The compile + link above suffice.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract check (item-spec §5 DOCS, Mode A): each op branch carries a
# Mode-A comment, and the banner references PRD §13 #11 / Russ Cox.
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE 'OP_MATCH.*accept|accept.*OP_MATCH|nfa_has_match' \
  && echo "OP_MATCH Mode-A comment present (ok)" \
  || echo "WARN: OP_MATCH comment missing"
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE 'OP_SPLIT.*epsilon|epsilon.*fork|both out and out1|BOTH' \
  && echo "OP_SPLIT Mode-A comment present (ok)" \
  || echo "WARN: OP_SPLIT comment missing"
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE 'OP_ASSERT|zero-width|word.boundary|EMPTY.STRING|empty.string' \
  && echo "OP_ASSERT Mode-A comment present (ok)" \
  || echo "WARN: OP_ASSERT comment missing"
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE 'OP_CHAR.*OP_ANY|consuming|wait.*char' \
  && echo "OP_CHAR/ANY Mode-A comment present (ok)" \
  || echo "WARN: OP_CHAR/ANY comment missing"
grep -qE 'lastlist.*MANDATORY|MANDATORY.*lastlist|§13|invariant' pattern_match.c \
  && echo "PRD §13 #11 mandatory-lastlist reference present (ok)" \
  || echo "WARN: §13 #11 reference missing"
grep -qE 'Russ Cox|swtch.com' pattern_match.c \
  && echo "Russ Cox reference present (ok)" \
  || echo "WARN: Russ Cox banner reference missing"

# Infinite-recursion-prevention doc (item-spec §5 Mode A): confirm a comment
# explains WHY the lastlist guard exists (prevents infinite epsilon recursion).
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qiE 'infinit|recurse|dedup|already' \
  && echo "infinite-recursion prevention documented (ok)" \
  || echo "WARN: recursion-prevention rationale missing"

# Empty-string special case doc (item-spec §5 Mode A): confirm a comment explains
# the empty-original-string rule for assertions.
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qiE 'empty.string|neither.*boundary|legacy semantics' \
  && echo "empty-string assertion special case documented (ok)" \
  || echo "WARN: empty-string special case not explained"

# Self-containment: nfa_addstate must NOT bump nfa_gen, advance abspos, or call
# not-yet-built helpers other than the stubbed is_word_boundary.
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -nE 'nfa_gen\+\+|nfa_match|nfa_has_match|is_digit_char|is_word_char|is_whitespace_char|pattern_char_matches' \
  && { echo "ERROR: nfa_addstate bumps nfa_gen or calls a not-yet-built helper (scope creep)"; exit 1; } \
  || echo "nfa_addstate does not bump nfa_gen / call un-built helpers (good)"

# Confirm nfa_addstate DOES call is_word_boundary (the OP_ASSERT contract) and
# that it is the stub (return false), not a real implementation.
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c | grep -q 'is_word_boundary(string_start, abspos)' \
  && echo "OP_ASSERT calls is_word_boundary (ok)" \
  || echo "ERROR: OP_ASSERT does not call is_word_boundary"
awk '/^static bool is_word_boundary/{f=1} f&&/^}/{exit} f' pattern_match.c | grep -q 'return false' \
  && echo "is_word_boundary is the STUB (ok)" \
  || echo "ERROR: is_word_boundary stub body wrong"

# Confirm no functions beyond nfa_addstate (+ the stub) were added this task.
grep -nE 'static (bool|void|int|State) (nfa_match|nfa_has_match|is_digit_char|is_word_char|is_whitespace_char|pattern_char_matches)' pattern_match.c \
  && echo "ERROR: leaked downstream functions (scope creep)" || echo "no downstream functions leaked (good)"

# abspos is absolute (PRD §13 #10): confirm OP_SPLIT forwards the SAME abspos to
# both branches (no increment inside the recursion).
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -E 'nfa_addstate\(list, n, s->out' \
  | grep -q 'abspos)' \
  && echo "OP_SPLIT forwards abspos unchanged to both branches (ok)" \
  || echo "WARN: abspos not forwarded unchanged"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0; exactly
      three warnings (`nfa_addstate`, `nfa_compile`, `get_escaped_char` unused);
      `nfa_gen` is NOT among them (it self-resolved this task).
- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- [ ] Level 2: `/tmp/s1_addstate_test` prints "ALL CASES CONFIRMED (0 failures)",
      including the **dedup** case (#4, PRD §13 #11) and the **empty-string
      assertion guard** (#7).
- [ ] Level 3: `test_metachar_verification.c pattern_match.c` LINKS cleanly (the
      is_word_boundary stub resolved the otherwise-undefined reference).
- [ ] Level 4: Mode-A comments present on each op branch; banner references
      §13 #11 / Russ Cox; infinite-recursion prevention + empty-string special
      case documented; no nfa_gen bump / downstream functions leaked.

### Feature Validation

- [ ] `static void nfa_addstate(State **list, int *n, State *s, const char *string_start, size_t abspos)`
      present verbatim, appended after the `nfa_compile` block.
- [ ] Guard `if (!s || s->lastlist == nfa_gen) return;` present (de-dup + NULL-safe).
- [ ] `s->lastlist = nfa_gen;` set unconditionally before the op dispatch.
- [ ] `OP_MATCH` → `list[(*n)++] = s; return;`.
- [ ] `OP_SPLIT` → recurse into BOTH `s->out` and `s->out1` with the SAME `abspos`;
      `return;`.
- [ ] `OP_ASSERT` → `want_boundary = (s->arg == 0x0B)`; recurse into `s->out` ONLY
      if `*string_start != '\0' && is_word_boundary(string_start, abspos) == want_boundary`;
      `return;` (empty string → neither passes).
- [ ] fall-through (`OP_CHAR`/`OP_ANY`) → `list[(*n)++] = s;`.
- [ ] The `is_word_boundary` STUB is present immediately before `nfa_addstate`,
      with the exact PRD §7.6 signature, returning `false`, marked temporary.

### Code Quality Validation

- [ ] Matches the reference implementation (git `81df853` lines 127–147 / PRD §7.5)
      branch-for-branch and guard-for-guard (PRD §17: code wins; comment drift tolerated).
- [ ] APPEND ONLY — no modification to S1/S2/M2.T1.S1/M2.T1.S2 content,
      `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
      `tasks.json`, `prd_snapshot.md`, `.gitignore`.
- [ ] No new `#include`s (only stdbool/string/stdlib already present).
- [ ] No new globals; `nfa_gen` is READ, not bumped; no `__attribute__((unused))`
      suppression; `is_word_boundary` is a STUB, not a real implementation.
- [ ] `nfa_addstate` unused-function warning accepted (self-resolves in P1.M2.T2.S2).

### Documentation & Deployment

- [ ] Mode-A inline comments explain each op branch (MATCH/SPLIT/ASSERT/CHAR-ANY).
- [ ] Banner comment references PRD §13 invariant #11 (mandatory lastlist) + Russ Cox URL.
- [ ] A comment explains the infinite-recursion prevention mechanism (the guard).
- [ ] A comment explains the empty-string special case for assertions (legacy semantics).
- [ ] The `is_word_boundary` stub carries a comment naming P1.M3.T1.S1 as its replacement.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't overwrite or reorder `pattern_match.c` — APPEND one stub + one function
  after the `nfa_compile` block; preserve everything above.
- ❌ Don't forward-declare `is_word_boundary` without a body — a bare declaration
  makes the LINK fail (`undefined reference to is_word_boundary`) because GCC
  emits the static `nfa_addstate` even though it is unused. You MUST provide the
  stub body. (Verified empirically.)
- ❌ Don't make `is_word_boundary` real — its full position-based logic is
  P1.M3.T1.S1's job; reproducing it here is scope creep. A `return false` stub
  is correct and sufficient (its value is never observed by any test).
- ❌ Don't bump `nfa_gen` inside `nfa_addstate` — the caller (nfa_match) owns the
  per-phase bump. Incrementing here double-bumps and silently breaks de-dup.
- ❌ Don't set `s->lastlist = nfa_gen` conditionally or after the op dispatch — it
  MUST be set unconditionally right after the guard, or a convergent `OP_SPLIT`
  re-adds its shared successor (PRD §13 #11 violation).
- ❌ Don't increment `abspos` inside `nfa_addstate` — epsilon edges (OP_SPLIT,
  OP_ASSERT) do NOT consume input; forward `abspos` unchanged (PRD §13 #10).
  Only `nfa_match` advances the position (pos+1) when it consumes a char.
- ❌ Don't reverse the OP_ASSERT `&&` order — `*string_start != '\0'` MUST come
  first so the empty-string short-circuit happens before `is_word_boundary` is
  called. Reversing it changes observable behavior once the real classifier lands.
- ❌ Don't drop the `return;` at the end of the OP_ASSERT branch — without it a
  failed `\b`/`\B` falls through and wrongly collects the ASSERT state.
- ❌ Don't suppress the `nfa_addstate` unused-function warning with
  `__attribute__((unused))` — accept it (self-resolves in P1.M2.T2.S2); same
  convention S2/M2.T1.S1/M2.T1.S2 used.
- ❌ Don't add `#include`s (stddef/stdint/stdio/notifier.h) — `size_t` is already
  available via `<string.h>` from S1; nothing else is needed.
- ❌ Don't implement `nfa_match` / `nfa_has_match` here — they are P1.M2.T2.S2.
  One stub + one function only.
- ❌ Don't run `run_all_tests.sh` to validate this task — the stub matcher makes
  true-cases fail by design; use the compile (Level 1) + #include harness (Level 2)
  + link (Level 3) gates.
- ❌ Don't touch `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
  `tasks.json`, `prd_snapshot.md`, or `.gitignore`.

---

## Confidence Score: 10/10

The exact code to write is the live source of truth (`git show 81df853:pattern_match.c`
lines 127–147; PRD §7.5 verbatim epsilon-closure contract; architecture doc
§"nfa_addstate") and is reproduced guard-for-guard and branch-for-branch above.
The **single non-obvious decision** — providing a temporary `is_word_boundary`
stub rather than a bare forward declaration — was **forced by an empirically
verified LINK failure** (`undefined reference to is_word_boundary` under a pure
forward declaration) and **resolved** (stub compiles + links cleanly), mirroring
the S2 `match_with_anchors` stub convention exactly. The complete build warning
set (exit 0; exactly three permitted warnings — `nfa_addstate` new + `nfa_compile`
+ `get_escaped_char` carried — with `nfa_gen` self-resolved and DROPPED from the
set this task), the silent `-fsyntax-only`, the clean test-suite LINK, and all 8
behavioral cases (plain-CHAR closure, MATCH closure, glob-SPLIT recursion, the
**lastlist dedup** PRD §13 #11, re-add idempotency, the NULL guard, the
**empty-string OP_ASSERT guard**, and OP_ASSERT branch wiring) were **all executed
against a realistic post-P1.M2.T1.S2 merged file during research and passed**.
Dependencies on P1.M2.T1.S1 (State/OP_*/nfa_gen — treated as a CONTRACT, read-only
here), P1.M2.T1.S2 (nfa_compile — the append point), and the boundaries with
P1.M2.T2.S2 (nfa_match — the sole consumer) / P1.M3.T1.S1 (replaces the
is_word_boundary stub) are explicit. No external dependencies are needed (libc
only; the Russ Cox URL is informational).
