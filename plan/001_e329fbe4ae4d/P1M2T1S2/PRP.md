# PRP — P1.M2.T1.S2: Implement `nfa_compile()` — Thompson construction

## Goal

**Feature Goal**: Append the **Thompson-construction compiler**
`nfa_compile()` to `pattern_match.c`. It walks a processed-pattern byte string
(the output of `process_escapes`, P1.M1.T2.S1) and emits a linear chain of
`State` nodes into a caller-supplied pool, using the `NEW()` bounds-safe macro
and a `State **tail` threading pointer. The compiler must reproduce the live
source-of-truth behavior **byte-for-byte** (git commit `81df853` lines 79–121;
PRD §7.5, §7.8; architecture doc §"NFA Compilation") — most importantly the
**linear-scaling invariant**: `a+a+a+…` compiles to exactly `2·k + 1` states,
never exponential blow-up.

**Deliverable**: A new `===== P1.M2.T1.S2: nfa_compile() — Thompson construction =====`
block APPENDED to the end of the current `pattern_match.c` (which, after
P1.M2.T1.S1, ends with `static int nfa_gen = 0;`). The block contains exactly one
`static` function:

```c
static State *nfa_compile(const char *pat, State *pool, int *nstates_out);
```

plus Mode-A inline comments on each `if/else` branch explaining the Thompson
pattern (item-spec §5 DOCS). Nothing else is modified.

**Success Definition**:
- `nfa_compile` present with the exact signature above, `static`, file-local.
  Consumes only `State`/`OP_*`/`NFA_MAX_STATES` from P1.M2.T1.S1; no new
  `#include`s; no new globals.
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0** with **exactly
  three** permitted warnings (all self-resolve downstream):
  - `nfa_compile defined but not used` (new this task → P1.M2.T2.S2)
  - `nfa_gen defined but not used` (carried M2.T1.S1 → P1.M2.T2.S1)
  - `get_escaped_char defined but not used` (carried S2 → P1.M3.T2.S1)
- `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- A `#include "pattern_match.c"` harness (reaching the static `nfa_compile`)
  confirms the construct-to-NFA mapping for every processed-byte type: empty→
  `MATCH`; literals/classes/dot/escaped→`OP_CHAR`; `\b`/`\B`→`OP_ASSERT`;
  glob `*`→`OP_SPLIT`→`OP_ANY` loop; `X+`→`OP_CHAR`→`OP_SPLIT` loop; and
  **`a+`×50 → 101 states** (the linear invariant, PRD §7.8).
- A committed test suite (e.g. `test_metachar_verification.c`) still **LINKS**
  cleanly (public API intact; nothing above the append point touched).
- Each branch carries a Mode-A inline comment explaining the Thompson construction
  step and a reference to PRD §7.8 (why NFA, not backtracking).

## User Persona (if applicable)

**Target User**: The NFA simulation functions written in the *next* subtask —
`nfa_match()` (P1.M2.T2.S2), which is the sole caller of `nfa_compile`. End
users and the public API never call it directly (it is `static`, file-local to
`pattern_match.c`).

**Use Case**: `nfa_match()` declares `State pool[NFA_MAX_STATES]` on its stack,
calls `nfa_compile(pattern, pool, &nstates)` to fill it, seeds its current-state
list with the returned start state, and simulates char-by-char. None of the
simulation exists yet — this task only produces the *compiled graph* the
simulator will walk.

**User Journey**: `nfa_match` → `State pool[NFA_MAX_STATES]; int nstates;` →
`State *start = nfa_compile(pattern, pool, &nstates);` → epsilon-closure of
`start` → per-char step loop (P1.M2.T2.S1/S2). The `lastlist` field that this
task zeroes on every used state is what makes the simulator's de-dup work on the
very first closure (nfa_gen also starts at 0).

**Pain Points Addressed**: Replaces the former exponential-backtracking matcher's
fatal flaw (PRD §7.8: `a+a+a+...b` against a long run of `a` hung). The compiled
NFA is simulated in guaranteed `O(states × input_len)`. Locking the compile
*algorithm* (not just the data types from S1) is the load-bearing fix — the
linear `X+` shape is precisely what avoids catastrophic blow-up.

## Why

- **Completes P1.M2.T1 (Compilation)**: S1 (P1.M2.T1.S1) defined the vocabulary
  (`State`, `OP_*`, `NFA_MAX_STATES`, `nfa_gen`); this task defines the compiler
  that *uses* them. After this, only the simulator (P1.M2.T2) remains for a
  working NFA.
- **Correctness anchor — the tail-threading + NEW() pattern is subtle**: the
  `State **tail` pointer (write-next-unit's-start-into-the-previous-dangling-slot)
  and the bounds-safe `NEW()` macro (`&pool[n < NFA_MAX_STATES ? n++ : n]`) are
  easy to get subtly wrong (off-by-one in the slot, forgetting the `any->out = sp`
  loop-back, forgetting to `p++` past the `0x0E` marker, leaving `lastlist`
  uninitialized). Reproducing the reference verbatim removes that risk.
- **The linear invariant is the whole point** (PRD §7.8): `X+` MUST compile to
  exactly `OP_CHAR(X) → OP_SPLIT(loop-back / exit)` — 2 states — so `a+a+a+…`
  scales as `2k+1`, not `2^k`. A backtracking-style nested compile would reintroduce
  the exact hang the acceptance gate (§11.2B, <50 ms on the pathological case)
  exists to catch. This PRP makes that invariant a Level-2 test gate.
- **Rebuild integrity**: appends cleanly to S1's definitions block; introduces
  exactly one *new* expected unused-function warning (`nfa_compile`), mirroring
  the established S2/S1 pattern (expected → accepted → self-resolves downstream).

## What

Append **one** `static` function (plus a banner comment) to the end of
`pattern_match.c`, immediately after the `static int nfa_gen = 0;` line from
P1.M2.T1.S1. The function walks `pat` with a `for (const char *p = pat; *p; p++)`
loop, dispatching on the processed byte:

- `0x2A` (glob `*`) → `OP_ANY` + `OP_SPLIT` with `any->out = sp` loop-back (`.*`)
- `0x0B`/`0x0C` (`\b`/`\B`) → `OP_ASSERT`, `arg` = the byte, zero-width
- `0x0E` standalone → `continue` (skip defensively; should not occur)
- consuming element `X`:
  - if next byte is `0x0E` (`X+`) → `OP_CHAR(X)` then `OP_SPLIT` loop-back; `p++`
    to consume the marker
  - else → plain `OP_CHAR(X)`
- after the loop → append `OP_MATCH`; zero `lastlist` on every allocated state;
  write `*nstates_out = n`; return `start`.

Every branch carries a Mode-A inline comment (item-spec §5). No new `#include`s,
no new globals, no edits above the append point.

### Success Criteria

- [ ] `static State *nfa_compile(const char *pat, State *pool, int *nstates_out)`
      present verbatim (reference body), file-local, appended after the S1 block.
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0; **exactly three**
      warnings (`nfa_compile`, `nfa_gen`, `get_escaped_char` unused) — nothing else.
- [ ] `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- [ ] Level-2 harness confirms every construct→NFA mapping (empty, literals,
      escaped-literal/class/dot, `\b`/`\B`, glob `*`, `X+`, mixed) AND the linear
      `a+`×50 → 101 states invariant.
- [ ] `test_metachar_verification.c pattern_match.c` LINKS cleanly.
- [ ] Mode-A inline comments present on each branch, referencing PRD §7.8.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write is the live source of truth (`git show
81df853:pattern_match.c` lines 79–121; PRD §7.5 verbatim description; architecture
doc §"NFA Compilation") and is reproduced verbatim in "Implementation Tasks"
below (only Mode-A comments are authored here — all behavior-neutral per PRD §17).
The construct→NFA mapping table, the state-count arithmetic, the
`a+`×50=101 linear invariant, the `lastlist`-zeroing guarantee, the build warning
set (exit 0; exactly three expected warnings — `nfa_compile` new + `nfa_gen` +
`get_escaped_char` carried), the silent `-fsyntax-only`, and the clean test-suite
LINK were **all executed against a realistic post-S2 merged file during research
and passed**. An implementer with only this PRP + repo access can produce the
function behavior-identically and prove it.

### Documentation & References

```yaml
# MUST READ — authoritative spec for the compiler algorithm
- file: PRD.md
  section: "### 7.5 The NFA engine"
  why: "Gives the EXACT compile contract this task must reproduce: the tail-
        threading bullet list ('0x2A -> OP_ANY looping back through OP_SPLIT';
        '0x0B/0x0C -> OP_ASSERT'; 'consuming X + 0x0E -> OP_CHAR(X) then
        OP_SPLIT loop-back' LINEAR for a+a+a+; 'stray 0x0E -> skip defensively';
        'end -> OP_MATCH'; 'zero lastlist on every allocated state')."
  critical: "The X+ LINEARITY is load-bearing (PRD §7.8): X+ MUST be 2 states
        (CHAR+SPLIT) so a+a+a+... scales as 2k+1, never 2^k. A nested/backtracking
        compile reintroduces the exact catastrophic case the acceptance gate
        (§11.2B, <50ms) exists to catch."

- file: PRD.md
  section: "### 7.8 Why an NFA (not backtracking)"
  why: "Justifies the whole engine: the old backtracking matcher went EXPONENTIAL
        on 'a+a+a+a+a+a+a+a+a+a+b' against a long run of 'a'. Thompson NFA is
        O(states x input_len), always. Cite this in the Mode-A banner comment."
  critical: "The linear X+ compile shape is THE fix. Do not 'optimize' it into a
        nested/recursive quantifier expansion."

- file: PRD.md
  section: "### 7.1 The processed-pattern byte contract (what the NFA consumes)"
  why: "The exact byte meanings nfa_compile dispatches on: 0x2A glob; 0x0E +
        quantifier marker (follows a consuming element); 0x01-0x04 escaped
        literals; 0x05-0x0A classes; 0x0B/0x0C word-boundary assertions (zero-
        width); 0x0D dot; 0x2E/0x2B literal dot/plus (ordinary bytes); anything
        else ordinary literal; 0x00 NUL terminator."
  critical: "Every NON-special byte (0x01-0x0D ordinary/literal/class/dot, plus
        0x2E/0x2B and any plain ASCII) is a CONSUMING ELEMENT -> OP_CHAR. Only
        0x2A, 0x0B, 0x0C, 0x0E get special handling; 0x00 terminates the loop."

- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "Confirms the semantics the compile must encode: '*' matches newline
        (glob == .*), 'X+' = one-or-more, '.' excludes newline. The compile only
        builds the SHAPE; semantics live in the simulator + pattern_char_matches."
  critical: "Glob '*' compiles as OP_ANY (matches newline) — distinct from the dot
        0x0D (OP_CHAR, excludes newline). Do not conflate them."

- file: PRD.md
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"
  why: "'the code + the passing tests win' — git 81df853 pattern_match.c is
        authoritative. Reproduce nfa_compile byte-for-behavior; comment drift is
        tolerated, logic is not."
  critical: "If in doubt, `git show 81df853:pattern_match.c | sed -n '79,121p'`
        is the source of truth for this function."

# Architecture — the algorithm narrative
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## NFA Compilation (nfa_compile)" + "## NFA Simulation (nfa_match)"
  why: "Spells out the tail-threading + NEW() pattern and the consumer contract:
        nfa_match allocates `State pool[NFA_MAX_STATES]`, calls nfa_compile, and
        zeroes lastlist is done BY nfa_compile (not nfa_match)."
  critical: "The pool is caller-allocated (nfa_match's stack). nfa_compile only
        fills it and zeroes lastlist on used states. Do NOT declare the pool here."

# Dependency PRP — the definitions this task consumes (CONTRACT, implemented in parallel)
- file: plan/001_e329fbe4ae4d/P1M2T1S1/PRP.md
  section: "## Implementation Blueprint" (the exact block at EOF)
  why: "S1 defines, at the current EOF of pattern_match.c: NFA_MAX_PATTERN=128,
        NFA_MAX_STATES=(2*NFA_MAX_PATTERN+2)=258, enum {OP_CHAR,OP_ANY,OP_SPLIT,
        OP_ASSERT,OP_MATCH}, typedef struct State State; struct State{int op;char
        arg;State *out;State *out1;int lastlist;}; static int nfa_gen=0;. This
        task APPENDS nfa_compile immediately after that block."
  critical: "Treat S1 as a CONTRACT: assume State/OP_*/NFA_MAX_STATES/nfa_gen all
        exist with those exact names. Do NOT redefine them. Do NOT touch nfa_gen
        (this task only zeroes pool[].lastlist, which is its own field)."

# Dependency PRP — the file structure above the append point (COMPLETE)
- file: plan/001_e329fbe4ae4d/P1M1T2S2/PRP.md
  section: "## Goal"
  why: "S2 is COMPLETE: pattern_match.c ends with the public pattern_match() (and,
        after S1/M2.T1.S1 appends, the NFA defs block). This task APPENDS after the
        NFA defs. S2 also established the 'expected unused warning that self-
        resolves downstream' convention this task reuses for nfa_compile."
  critical: "Do NOT modify S2's content (process_escapes, parsed_pattern_t,
        get_escaped_char, parse_pattern, free_parsed_pattern, match_with_anchors
        stub, pattern_match). APPEND ONLY, after the M2.T1.S1 block."

# Downstream consumer (informational; NOT this task)
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## NFA Simulation (nfa_match)"
  why: "Shows how nfa_match calls `nfa_compile(pattern, pool, &nstates)`, seeds
        its clist with the start state, and bumps nfa_gen per step. This task's
        output (start pointer + zeroed lastlist + nstates) is exactly what that
        caller consumes."
  critical: "nfa_compile does NOT guard NULL pat (the for loop derefs *p). The
        caller (nfa_match) receives parse_pattern's non-NULL core; the PUBLIC
        pattern_match() NULL-guards upstream. Do NOT add a NULL guard here — it
        deviates from the reference and is not this layer's responsibility."

# Live reference implementation (the byte-for-behavior source of truth, PRD §17)
- file: git commit 81df853 ("implemented nfa matching engine")
  why: "HEAD's pattern_match.c was RESET to process_escapes-only for the rebuild;
        the full nfa_compile lives in history at 81df853 lines 79-121. Reproduce
        it verbatim."
  how:  "git show 81df853:pattern_match.c | sed -n '79,121p'"
  critical: "Reproduce the tail-threading slot writes, the any->out=sp loop-back,
        the p++ past the 0x0E marker, the *nstates_out=n, and the lastlist-zeroing
        loop EXACTLY. Comment wording may drift (PRD §17); logic may not."

# Build convention
- file: run_all_tests.sh
  why: "Toolchain is plain gcc, no make. Each suite is `gcc -o test_X test_X.c
        pattern_match.c`; suites #include \"pattern_match.h\" and call only the
        public pattern_match()."
  critical: "nfa_compile is static/file-local, so it is validated by compiling
        pattern_match.c (Level 1) + a #include-harness (Level 2) + one LINK
        check (Level 3) — NOT by a committed test file."

# External theory (informational)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast' — the
        Thompson-NFA design this engine follows (cited by the reference header).
        Mode-A banner comment should reference it + PRD §7.8."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # S2 + M2.T1.S1 COMPLETE: process_escapes + parsed_pattern_t
                       #   + get_escaped_char + free_parsed_pattern + parse_pattern
                       #   + match_with_anchors STUB + pattern_match (public)
                       #   + #define NFA_MAX_PATTERN/NFA_MAX_STATES
                       #   + enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH }
                       #   + typedef struct State State; struct State {...}
                       #   + static int nfa_gen = 0;
                       #   THIS task APPENDS nfa_compile() after the nfa_gen line.
notifier.h notifier.c  # P2 scope — do not touch.
rules.mk               # P2 scope — do not touch.
test_*.c               # P1.M4 scope — do not touch (only used to prove LINK).
run_all_tests.sh       # P1.M4 scope — do not touch.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # THIS task APPENDS one function. After S2 it additionally contains:
                       #   - static State *nfa_compile(const char *pat, State *pool, int *nstates_out)
                       #     (Thompson construction; consumes State/OP_*/NFA_MAX_STATES from S1)
                       # Later subtasks APPEND after this function:
                       #   P1.M2.T2.S1 -> nfa_addstate()  (consumes State/OP_*/nfa_gen; CALLS nothing of compile)
                       #   P1.M2.T2.S2 -> nfa_match() + nfa_has_match() (nfa_match CALLS nfa_compile)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — nfa_compile IS expected to warn 'defined but not used' until P1.M2.T2.S2
//   (VERIFIED during research). A static function defined-but-unused triggers
//   -Wunused-function under -Wall -Wextra (exit stays 0 — warnings don't fail
//   compilation). This mirrors the S2/S1 convention: ACCEPT the warning; do NOT
//   suppress it with __attribute__((unused)) (not this codebase's idiom). It
//   self-resolves the moment nfa_match (P1.M2.T2.S2) calls nfa_compile. So after
//   THIS task the permitted-warning set is EXACTLY:
//     * 'get_escaped_char defined but not used [-Wunused-function]' (carried, P1.M3.T2.S1)
//     * 'nfa_gen defined but not used [-Wunused-variable]'          (carried, P1.M2.T2.S1)
//     * 'nfa_compile defined but not used [-Wunused-function]'      (NEW,      P1.M2.T2.S2)
//   If ANY OTHER warning appears (e.g. 'process_escapes', 'parse_pattern'), the
//   wiring above is broken — investigate, do not ignore.

// CRITICAL — do NOT guard NULL `pat`. The reference does `for (const char *p =
//   pat; *p; p++)`, which derefs pat. nfa_compile's sole caller (nfa_match,
//   P1.M2.T2.S2) receives parse_pattern()'s non-NULL core_pattern; the PUBLIC
//   pattern_match() NULL-guards upstream. Adding a guard deviates from the
//   reference and is the wrong layer. (Verified: no caller passes NULL.)

// GOTCHA — the NEW() macro is bounds-safe: `#define NEW() (&pool[n < NFA_MAX_STATES ? n++ : n])`.
//   It returns &pool[n] and increments n ONLY if n < NFA_MAX_STATES; at/over the
//   limit it returns &pool[n] WITHOUT incrementing (clamps, reusing the last slot).
//   So a too-long pattern silently overwrites the last state rather than
//   overflowing. NFA_MAX_STATES=258 guarantees any valid (<=128-byte) processed
//   pattern fits (max 2 states/byte + MATCH). Do NOT change the macro. Do NOT
//   replace it with an open-coded allocator that could overflow.

// GOTCHA — the X+ loop-back requires c->out = sp AND sp->out = c (VERIFIED). The
//   consuming element OP_CHAR's `out` points to the OP_SPLIT, and the OP_SPLIT's
//   `out` points BACK to the OP_CHAR (the loop). Miss either arrow and the + is
//   broken. This 2-state shape is what makes a+a+a+... linear (PRD §7.8).

// GOTCHA — the glob `*` loop-back requires any->out = sp AND sp->out = any
//   (VERIFIED). OP_SPLIT's out -> OP_ANY; OP_ANY's out loops BACK to the OP_SPLIT.
//   The SPLIT's out1 is the dangled exit (threaded via tail). This encodes `.*`.

// GOTCHA — after emitting OP_CHAR(X) + OP_SPLIT for X+, you MUST `p++` to consume
//   the 0x0E marker (the for loop's own p++ then advances past X). Forget the
//   manual p++ and the next iteration re-reads 0x0E as a standalone marker,
//   silently skipping it — the NFA still parses but you've lost the quantifier.

// GOTCHA — `*tail = m` at the end closes the FINAL dangling slot (whichever
//   construct was last). Without it the last unit's exit pointer stays NULL and
//   the graph can never reach OP_MATCH. The tail-threading invariant is: every
//   construct sets *tail=<its start> then advances tail to ITS dangling exit slot.

// GOTCHA — zero lastlist on EVERY allocated state (the for i<n loop), not just
//   the ones you think matter. nfa_gen starts at 0; the simulator's FIRST
//   epsilon-closure (nfa_addstate) checks lastlist==nfa_gen (==0); if any used
//   state has garbage lastlist==0-by-accident it is fine, but if it has a NON-zero
//   value AND equals a future nfa_gen it would be wrongly skipped. Zeroing is
//   mandatory. (The pool is stack-fresh, but zeroing is belt-and-suspenders and
//   matches the reference + PRD §7.5 'Zero lastlist on every allocated state'.)

// GOTCHA — do NOT #undef NEW inside the loop or before the lastlist-zeroing loop.
//   The reference #undef NEW happens right before `return start;`, AFTER all uses.
//   Keep it at function end for macro hygiene (NEW is a function-scope #define;
//   #undef before return prevents leaking it past the function in some toolchains).

// GOTCHA — do NOT add #includes. nfa_compile uses only State/OP_*/NFA_MAX_STATES
//   (from M2.T1.S1) and built-in int/char/pointer types. No <string.h>, <stddef.h>,
//   <stdint.h>, <stdio.h>, or "notifier.h". The for loop reads bytes via unsigned
//   char — built-in.

// GOTCHA — do NOT touch nfa_gen. This task only zeroes pool[].lastlist (the
//   State field). nfa_gen is owned by M2.T1.S1 (definition) and bumped by
//   nfa_match (P1.M2.T2.S2). Zeroing lastlist + nfa_gen starting at 0 is what
//   makes the very first closure de-dup correctly.

// GOTCHA — `unsigned char b = (unsigned char)*p;` is required (not plain char):
//   the byte comparisons (b == 0x2A etc.) must be against an unsigned value, else
//   a high-bit-set byte (negative as signed char) compares wrong. The reference
//   casts explicitly; keep it.

// GOTCHA — `(unsigned char)p[1] == 0x0E` when p points at the last byte reads
//   p[1] == '\0' (the NUL terminator) — NOT 0x0E — so a trailing consuming
//   element correctly takes the plain-OP_CHAR branch. Safe by construction; do
//   not add an explicit end-of-string check (the reference does not).
```

## Implementation Blueprint

### Data models and structure

No new data models. This task consumes the `State`/`OP_*`/`NFA_MAX_STATES`
definitions from P1.M2.T1.S1 and adds a single `static` function that *produces*
a graph of `State` nodes in a caller-supplied pool.

```c
/* The types consumed (already defined by P1.M2.T1.S1 at the append point): */
#define NFA_MAX_PATTERN 128
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)   /* 258 */
enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };
typedef struct State State;
struct State { int op; char arg; State *out; State *out1; int lastlist; };
static int nfa_gen = 0;
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: APPEND nfa_compile() to pattern_match.c
  - PLACE: immediately AFTER the `static int nfa_gen = 0;` line (current EOF).
  - PREFIX the function with a banner comment referencing PRD §7.5/§7.8 + Russ Cox.
  - SIGNATURE: static State *nfa_compile(const char *pat, State *pool, int *nstates_out)
  - BODY: the reference body verbatim (see "The exact code to write" below),
    including:
      * int n = 0; State *start = NULL; State **tail = &start;
      * #define NEW() (&pool[n < NFA_MAX_STATES ? n++ : n])
      * the for-loop over pat with the 4-branch dispatch (0x2A / 0x0B|0x0C / 0x0E /
        consuming-element), each branch with its Mode-A comment
      * the X+ inner check: if (unsigned char)p[1] == 0x0E -> OP_CHAR + OP_SPLIT +
        p++ (the LINEAR shape; PRD §7.8)
      * after loop: State *m = NEW(); m->op = OP_MATCH; *tail = m;
      * the lastlist-zeroing loop: for (int i=0;i<n;i++) pool[i].lastlist = 0;
      * *nstates_out = n;
      * #undef NEW
      * return start;
  - NAMING: nfa_compile (static, snake_case); pat/pool/nstates_out params
    (snake_case, matching the reference + item-spec §3).
  - DEPENDENCIES: State, OP_*, NFA_MAX_STATES (all from P1.M2.T1.S1 — CONTRACT).
  - PRESERVE: all existing content (S1 process_escapes; S2 pipeline + pattern_match;
    M2.T1.S1 defs). APPEND ONLY — do not move, reorder, or edit anything above.
  - DO NOT: add #includes, add globals, NULL-guard pat, declare the State pool,
    implement nfa_addstate/nfa_match/nfa_has_match, or suppress the expected
    nfa_compile unused-function warning.

Task 2: VERIFY the build + link gates (run the Validation Loop, Levels 1, 2, 3).
```

**The exact code to write** (verbatim from `git show 81df853:pattern_match.c`
lines 79–121, source of truth per PRD §17; only the Mode-A branch comments are
authored here — all behavior-neutral). Append after `static int nfa_gen = 0;`:

```c
/* ===== P1.M2.T1.S2: nfa_compile() — Thompson construction =====
 * Compile a processed-pattern byte string (process_escapes output, P1.M1.T2.S1)
 * into a State pool via Thompson construction. The caller (nfa_match,
 * P1.M2.T2.S2) allocates `State pool[NFA_MAX_STATES]` on its stack and passes it
 * in; we fill it and return the start state.
 *
 * WHY A COMPILED NFA (not backtracking): the previous engine backtracked and went
 * EXPONENTIAL on patterns like a+a+a+...b against a long run of a (PRD §7.8).
 * Thompson construction compiles once and the later simulator runs in guaranteed
 * O(states x input_len). Crucially, X+ compiles to exactly TWO states (OP_CHAR +
 * OP_SPLIT loop-back), so a+a+a+... scales as 2k+1 — never 2^k. See Russ Cox,
 * "Regular Expression Matching Can Be Simple And Fast",
 * https://swtch.com/~rsc/regexp/regexp1.html . */

/* ---- compile: processed pattern -> State pool, returns start state ----
 * Threads `State **tail`: it points at the slot where the NEXT unit's start node
 * must be written (initially `&start`). Each construct writes *tail = <its start>
 * then advances tail to its own "dangling exit" slot (out1 for SPLIT, out for
 * CHAR/ASSERT). At the end we write the OP_MATCH into the final dangling slot. */
static State *nfa_compile(const char *pat, State *pool, int *nstates_out) {
    int n = 0;
    State *start = NULL;
    State **tail = &start;            /* slot to write the next unit's start into */

    /* Bounds-safe state allocator: return &pool[n] and advance n, but clamp at
     * NFA_MAX_STATES so a pathological pattern reuses the last slot instead of
     * overflowing. (2 per byte + MATCH + slack fits any <=128-byte pattern.) */
    #define NEW() (&pool[n < NFA_MAX_STATES ? n++ : n])   /* allocate one state */

    for (const char *p = pat; *p; p++) {
        unsigned char b = (unsigned char)*p;

        if (b == 0x2A) {                         /* (a) glob '*' == regex '.*' */
            /* Thompson construction for .*:  SPLIT -> ANY(loop back) -> exit.
             * OP_ANY consumes ANY byte incl. '\n'/'\r' (distinct from the dot,
             * which excludes them). The SPLIT's out1 is the dangled exit. */
            State *any = NEW(); any->op = OP_ANY;
            State *sp  = NEW(); sp->op  = OP_SPLIT; sp->out = any; sp->out1 = NULL;
            any->out = sp;                        /* loop back: ANY -> SPLIT */
            *tail = sp; tail = &sp->out1;         /* entry = SPLIT; exit via out1 */

        } else if (b == 0x0B || b == 0x0C) {      /* (b) \b / \B : zero-width assert */
            /* OP_ASSERT consumes no input; the simulator (nfa_addstate, P1.M2.T2.S1)
             * recurses into `out` only if is_word_boundary(...) matches. arg carries
             * 0x0B (\b, want boundary) or 0x0C (\B, want non-boundary). */
            State *a = NEW(); a->op = OP_ASSERT; a->arg = (char)b; a->out = NULL;
            *tail = a; tail = &a->out;

        } else if (b == 0x0E) {
            /* (c) standalone quantifier marker — should NOT occur: process_escapes
             * only emits 0x0E immediately after a consuming element (handled below).
             * Skip defensively to stay robust if it ever appears alone. */
            continue;

        } else {                                  /* (d) consuming element X */
            /* X is any byte that consumes one input char: an escaped literal
             * (0x01-0x04), a class (0x05-0x0A), the dot (0x0D), a literal '.'
             * (0x2E) / '+' (0x2B), or any ordinary ASCII byte. Compile to OP_CHAR. */
            State *c = NEW(); c->op = OP_CHAR; c->arg = (char)b; c->out = NULL;

            if ((unsigned char)p[1] == 0x0E) {    /* X+ : one-or-more, LINEAR (PRD §7.8) */
                /* Thompson 'plus': after matching one X (c), reach an OP_SPLIT whose
                 * `out` loops BACK to c (match more) and whose `out1` exits. This is
                 * exactly 2 states per X+, so a+a+a+... compiles as 2k+1 — the fix
                 * for the old exponential backtracker. */
                State *sp = NEW(); sp->op = OP_SPLIT; sp->out = c; sp->out1 = NULL;
                c->out = sp;                      /* after one X, reach the split */
                *tail = c; tail = &sp->out1;      /* entry = c; exit via split.out1 */
                p++;                              /* consume the 0x0E marker */
            } else {
                /* Plain single consuming element: entry = c, exit via c->out. */
                *tail = c; tail = &c->out;
            }
        }
    }

    /* (e) End: append the single accepting state into the final dangling slot. */
    State *m = NEW(); m->op = OP_MATCH;           /* accepting state */
    *tail = m;

    /* Zero lastlist on every allocated state. The pool is stack-fresh, but this is
     * mandatory: nfa_gen starts at 0 and the simulator's FIRST epsilon-closure
     * (nfa_addstate) guards on lastlist == nfa_gen (== 0); zeroing guarantees no
     * state is wrongly pre-marked. PRD §7.5: "Zero lastlist on every allocated
     * state (the pool is fresh each call)." */
    for (int i = 0; i < n; i++) pool[i].lastlist = 0;

    *nstates_out = n;

    #undef NEW
    return start;
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: tail-threading (Russ Cox style). `State **tail` always points at the
//   slot to receive the NEXT unit's start node. Each construct does:
//     *tail = <unit start>;
//     tail  = &<unit dangling exit slot>;   // &sp->out1 for SPLIT, &c->out otherwise
//   so units chain head-to-tail regardless of order. Final `*tail = m` closes it.

// PATTERN: bounds-safe macro allocator. `NEW()` returns &pool[n] and bumps n only
//   when in-bounds; otherwise it clamps (reuses the last slot). This makes overflow
//   impossible without an explicit check. Keep the macro verbatim; #undef at end.

// PATTERN: unsigned byte comparison. `unsigned char b = (unsigned char)*p;` then
//   `b == 0x2A`. Comparing a signed char against 0x2A works for ASCII but is
//   fragile for high-bit bytes; cast up front (the reference does).

// PATTERN: look-ahead for the quantifier. `(unsigned char)p[1] == 0x0E` peeks the
//   next byte; reading p[1] when p is at the last byte yields '\0' (the NUL), NOT
//   0x0E, so a trailing consuming element safely takes the plain-OP_CHAR branch.
//   On a hit, `p++` consumes the marker (the for loop's p++ then advances past X).

// PATTERN: linear X+ (PRD §7.8 invariant). X+ = OP_CHAR(X) -> OP_SPLIT{out=CHAR,
//   out1=exit}. 2 states. Never nest/recursively expand — that reintroduces the
//   exponential case. The Level-2 gate asserts a+ x50 == 101 states.

// ANTI-PATTERN: do NOT NULL-guard `pat`. The reference does `for (*p; *p; p++)`;
//   the caller (nfa_match) owns the non-NULL invariant (parse_pattern guarantees it;
//   the PUBLIC pattern_match NULL-guards upstream). A guard here is the wrong layer.

// ANTI-PATTERN: do NOT suppress the nfa_compile unused-function warning. Accept it;
//   it self-resolves in P1.M2.T2.S2 when nfa_match calls nfa_compile. (Same
//   convention S2/S1 used for get_escaped_char / nfa_gen.)

// ANTI-PATTERN: do NOT declare the State pool or touch nfa_gen. The pool is the
//   caller's (nfa_match's stack); nfa_gen is owned by M2.T1.S1/bumped by nfa_match.
//   This task only zeroes pool[].lastlist (the State field).

// ANTI-PATTERN: do NOT implement nfa_addstate / nfa_match / nfa_has_match here —
//   they are P1.M2.T2.S1 and P1.M2.T2.S2. Definitions only here (one function).

// ANTI-PATTERN: do NOT add #includes — nfa_compile uses only State/OP_* (from S1)
//   and built-in types. No string.h/stddef.h/stdint.h/stdio.h/notifier.h.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - APPEND one function (+ banner comment) at EOF of pattern_match.c, immediately
    after `static int nfa_gen = 0;` (the M2.T1.S1 block).
  - No insertion into existing code; no reordering; no edits above the append point.

CONSUMERS (downstream, NOT this task):
  - State* / nstates_out  <- nfa_match() calls `nfa_compile(pattern, pool, &nstates)`
                              [P1.M2.T2.S2]
  - (zeroed lastlist)     <- nfa_addstate() first closure guards on it [P1.M2.T2.S1]

BUILD:
  - No build-system change. Plain gcc (run_all_tests.sh style). Validate by
    compiling pattern_match.c (Level 1) + a #include harness (Level 2) + a
    single-suite LINK (Level 3).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; pure compile function, no runtime effect until the
    simulator in P1.M2.T2 is added).
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. `nfa_compile` is `static`/file-local,
> so it is reached via `#include "pattern_match.c"` in a throwaway harness (the
> committed suites only call the public `pattern_match()`). All commands were
> VERIFIED during research against a realistic post-S2 merged file.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o
# Expected: exit 0. EXACTLY THREE warnings are permitted and expected:
#   warning: 'nfa_compile' defined but not used [-Wunused-function]   (new this task -> P1.M2.T2.S2)
#   warning: 'nfa_gen' defined but not used [-Wunused-variable]       (carried M2.T1.S1 -> P1.M2.T2.S1)
#   warning: 'get_escaped_char' defined but not used [-Wunused-function] (carried S2 -> P1.M3.T2.S1)
# FAIL if: exit != 0, OR any OTHER warning appears, OR process_escapes/parse_pattern
#           appear in the warning list (those must be USED by now).

# 1b. Syntax-only (silent — fsyntax-only does not emit unused warnings).
gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c
# Expected: exit 0, NO output. (Confirms no syntax/type errors independent of the
#           expected unused-symbol warnings.)

# 1c. Confirm nfa_compile is present, static, with the exact signature.
grep -nE 'static State \*nfa_compile\(const char \*pat, State \*pool, int \*nstates_out\)' pattern_match.c
# Expected: exactly ONE match.

# 1d. Confirm the NEW() macro + bounds-safe form + the lastlist-zeroing loop.
grep -nE '#define NEW\(\) \(&pool\[n < NFA_MAX_STATES \? n\+\+ : n\]\)' pattern_match.c
grep -nE 'for \(int i = 0; i < n; i\+\+\) pool\[i\]\.lastlist = 0;' pattern_match.c
# Expected: each prints exactly ONE line.

# 1e. Confirm NO new #includes were added (still exactly stdbool/string/stdlib).
grep -nE '^#include' pattern_match.c
# Expected: <stdbool.h>, <string.h>, <stdlib.h> (the S1 set) — nothing else.

rm -f /tmp/pm.o
```

### Level 2: Component Tests (THE PRIMARY BEHAVIORAL GATE)

This harness was **verified against the source-of-truth reference** during
research (all construct mappings + the linear invariant pass). Create it, run it,
require all-pass. It reaches the static `nfa_compile` by `#include`-ing the `.c`.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/s2_compile_test.c <<'EOF'
/* Reach the static nfa_compile + State/OP_* by including the .c directly. */
#include "pattern_match.c"
#include <stdio.h>
#include <string.h>

static int failures = 0;

/* Follow the linear (out-pointer) spine and render it as a string for comparison.
 * For SPLIT nodes we follow `out` (the loop target), so quantifier/glob shapes
 * show their entry node then loop; the dedicated loop checks below verify the
 * back-edge structurally. */
static void dump_chain(State *s, char *buf, int max) {
    int i = 0; buf[0] = '\0';
    while (s && i < max) {
        char tag[24];
        const char *opn = (s->op==OP_CHAR)?"CHAR":(s->op==OP_ANY)?"ANY":
                          (s->op==OP_SPLIT)?"SPLIT":(s->op==OP_ASSERT)?"ASSERT":
                          (s->op==OP_MATCH)?"MATCH":"?";
        if (s->op == OP_CHAR || s->op == OP_ASSERT)
            snprintf(tag,sizeof tag,"%s(%02X)",opn,(unsigned char)s->arg);
        else
            snprintf(tag,sizeof tag,"%s",opn);
        strcat(buf, tag); strcat(buf, "->");
        if (s->op == OP_MATCH) break;          /* MATCH has no out; stop cleanly */
        s = s->out; i++;
    }
    /* strip a trailing "->" if present */
    size_t L = strlen(buf);
    if (L >= 2 && buf[L-2]=='-' && buf[L-1]=='>') buf[L-2] = '\0';
}

/* Verify the compiled NFA's primary chain matches an expected op sequence, that
 * nstates_out was set, and that lastlist is zeroed on every used state. */
static void ck_chain(const char *desc, const char *pat_in, const char *want) {
    State pool[NFA_MAX_STATES];
    int n = -1;
    State *start = nfa_compile(pat_in, pool, &n);
    char got[256]; dump_chain(start, got, 60);
    int lz_ok = 1;
    for (int i = 0; i < n; i++) if (pool[i].lastlist != 0) { lz_ok = 0; break; }
    if (strcmp(got, want) != 0) {
        printf("FAIL %-10s want '%s'  got '%s'\n", desc, want, got); failures++;
    } else if (!lz_ok) {
        printf("FAIL %-10s lastlist not zeroed (n=%d)\n", desc, n); failures++;
    } else if (n <= 0) {
        printf("FAIL %-10s nstates_out not set (n=%d)\n", desc, n); failures++;
    } else {
        printf("ok   %-10s n=%d  %s\n", desc, n, got);
    }
}

/* X+ shape: OP_CHAR(X).out -> OP_SPLIT, and that OP_SPLIT.out -> back to OP_CHAR. */
static int has_plus_loop(State *start, char x) {
    for (State *s = start; s; s = s->out) {
        if (s->op == OP_CHAR && s->arg == x) {
            State *sp = s->out;
            return (sp && sp->op == OP_SPLIT && sp->out == s) ? 1 : 0;
        }
    }
    return 0;
}
/* glob `*` shape: an OP_SPLIT whose out->OP_ANY and that OP_ANY.out -> back to SPLIT. */
static int has_glob_loop(State *start) {
    for (State *s = start; s; s = s->out) {
        if (s->op == OP_SPLIT) {
            State *any = s->out;
            if (any && any->op == OP_ANY && any->out == s) return 1;
        }
    }
    return 0;
}

int main(void) {
    /* (e) empty pattern -> single OP_MATCH, 1 state. */
    ck_chain("empty", "", "MATCH");

    /* (d-plain) ordinary literals: N CHAR + MATCH = N+1 states. */
    ck_chain("abc", "abc", "CHAR(61)->CHAR(62)->CHAR(63)->MATCH");
    ck_chain("abcdef", "abcdef", "CHAR(61)->CHAR(62)->CHAR(63)->CHAR(64)->CHAR(65)->CHAR(66)->MATCH");

    /* (d-plain) escaped-literal placeholders 0x01-0x04 are consuming -> OP_CHAR. */
    ck_chain("esc-lit", "\x01\x02\x03\x04", "CHAR(01)->CHAR(02)->CHAR(03)->CHAR(04)->MATCH");

    /* (d-plain) class bytes 0x05-0x0A are consuming -> OP_CHAR. */
    ck_chain("class", "\x05\x06", "CHAR(05)->CHAR(06)->MATCH");

    /* (d-plain) dot 0x0D is consuming -> OP_CHAR (NOT OP_ANY). */
    ck_chain("dot", "\x0D", "CHAR(0D)->MATCH");

    /* (b) \b / \B -> OP_ASSERT (zero-width), arg carries the byte. */
    ck_chain("assert-b", "\x0B", "ASSERT(0B)->MATCH");
    ck_chain("assert-B", "\x0C", "ASSERT(0C)->MATCH");
    ck_chain("bword", "\x0B""w", "ASSERT(0B)->CHAR(77)->MATCH");

    /* (a) glob '*' (0x2A): SPLIT -> ANY(loop) + MATCH = 3 states. */
    { State pool[NFA_MAX_STATES]; int n=-1;
      State *start = nfa_compile("\x2A", pool, &n);
      if (has_glob_loop(start) && n == 3) printf("ok   glob        n=%d (SPLIT->ANY loop + MATCH)\n", n);
      else { printf("FAIL glob n=%d loop=%d\n", n, has_glob_loop(start)); failures++; } }

    /* (d-plus) X+ : OP_CHAR(X) -> OP_SPLIT(loop) + MATCH = 3 states. */
    { State pool[NFA_MAX_STATES]; int n=-1;
      State *start = nfa_compile("a\x0E", pool, &n);
      if (has_plus_loop(start,'a') && n == 3) printf("ok   a+          n=%d (CHAR->SPLIT loop + MATCH)\n", n);
      else { printf("FAIL a+ n=%d loop=%d\n", n, has_plus_loop(start,'a')); failures++; } }

    /* mixed a+b+c : (CHAR+SPLIT)+(CHAR+SPLIT)+CHAR+MATCH = 2+2+1+1 = 6 states. */
    { State pool[NFA_MAX_STATES]; int n=-1;
      nfa_compile("a\x0E""b\x0E""c", pool, &n);
      if (n == 6) printf("ok   a+b+c      n=%d\n", n);
      else { printf("FAIL a+b+c n=%d want 6\n", n); failures++; } }

    /* *** LINEAR INVARIANT (PRD §7.8) *** a+ repeated 50x -> 50*(CHAR+SPLIT)+MATCH = 101. */
    { char pat[256]; int rep = 50;
      for (int i = 0; i < rep; i++) { pat[2*i]='a'; pat[2*i+1]='\x0E'; }
      pat[2*rep] = '\0';
      State pool[NFA_MAX_STATES]; int n=-1;
      nfa_compile(pat, pool, &n);
      int want = rep*2 + 1;                         /* 101 */
      if (n == want) printf("ok   a+x50      n=%d (LINEAR, want %d — no blow-up)\n", n, want);
      else { printf("FAIL a+x50 n=%d want %d (NOT LINEAR — PRD §7.8 regression!)\n", n, want); failures++; } }

    /* nstates_out + start->op correctness for the empty case. */
    { State pool[NFA_MAX_STATES]; int n=-1;
      State *start = nfa_compile("", pool, &n);
      if (n==1 && start && start->op==OP_MATCH) printf("ok   empty-n     n=%d start->op=MATCH\n", n);
      else { printf("FAIL empty-n n=%d start_op=%d\n", n, start?start->op:-1); failures++; } }

    printf("\n%s (%d failures)\n", failures ? "SOME FAILURES" : "ALL CASES CONFIRMED", failures);
    return failures ? 1 : 0;
}
EOF

gcc -Wall -Wextra -std=c99 -I. /tmp/s2_compile_test.c -o /tmp/s2_compile_test 2>&1 | grep -vE 'nfa_compile|nfa_gen|get_escaped_char' 
/tmp/s2_compile_test
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
# (The only permitted compiler warnings are the three expected unused-symbol ones,
#  filtered out above. The CRITICAL gate is "a+x50 n=101 (LINEAR)".)

rm -f /tmp/s2_compile_test.c /tmp/s2_compile_test
```

### Level 3: Integration Testing (API Integrity — LINK, not run)

```bash
cd /home/dustin/projects/qmk-notifier

# The committed suites call only the public pattern_match(). After this task the
# symbol still exists and nfa_compile is file-local, so a suite LINKS cleanly —
# proving nothing in the public path broke. (It will not PASS yet:
# match_with_anchors is still the S2 stub; real matching is P1.M3.T2.S2.)
gcc -Wall test_metachar_verification.c pattern_match.c -o /tmp/tm 2>&1 \
  | grep -v 'get_escaped_char\|nfa_gen\|nfa_compile'
# Expected: empty output (only the three known warnings, filtered above).
echo "link exit (expect 0): ${PIPESTATUS[0]}"
rm -f /tmp/tm

# DO NOT run run_all_tests.sh to validate this task — the stub matcher makes
# every true-expecting case fail by design. The compile + link above suffice.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract check (item-spec §5 DOCS, Mode A): each branch carries a comment
# explaining the Thompson step, and the banner references PRD §7.8 / Russ Cox.
awk '/static State \*nfa_compile/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE '0x2A.*glob|glob.*\*|SPLIT.*ANY|ANY.*SPLIT' \
  && echo "glob-branch Mode-A comment present (ok)" \
  || echo "WARN: glob-branch comment missing"
awk '/static State \*nfa_compile/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE 'ASSERT|zero-width|word.boundary' \
  && echo "assert-branch Mode-A comment present (ok)" \
  || echo "WARN: assert-branch comment missing"
awk '/static State \*nfa_compile/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE 'X\+|LINEAR|linear|loop.back' \
  && echo "plus-branch Mode-A comment present (ok)" \
  || echo "WARN: plus-branch comment missing"
grep -qE 'Russ Cox|swtch.com|§7.8|7\.8' pattern_match.c \
  && echo "PRD §7.8 / Russ Cox reference present (ok)" \
  || echo "WARN: §7.8/Russ Cox banner reference missing"

# Self-containment: nfa_compile must NOT call not-yet-built helpers.
awk '/static State \*nfa_compile/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -nE 'nfa_addstate|nfa_match|nfa_has_match|is_digit_char|is_word_char|is_whitespace_char|is_word_boundary|pattern_char_matches' \
  && { echo "ERROR: nfa_compile calls a not-yet-built helper (scope creep)"; exit 1; } \
  || echo "nfa_compile depends only on State/OP_*/NFA_MAX_STATES (good)"

# Confirm no functions beyond nfa_compile were added this task, and no pool/nfa_gen edits.
grep -nE 'static (void|bool|int|State) (nfa_addstate|nfa_match|nfa_has_match)' pattern_match.c \
  && echo "ERROR: leaked simulator functions (P1.M2.T2 scope)" || echo "no simulator leaked (good)"
grep -nE 'State pool\[|nfa_gen\s*\+\+|nfa_gen\s*=' pattern_match.c | grep -v 'static int nfa_gen = 0' \
  && echo "ERROR: this task declared a pool or mutated nfa_gen" || echo "no pool/nfa_gen edits (good)"

# Linear-invariant cross-check vs PRD §7.8: 50x(a+) MUST be 2*50+1 = 101 states.
python3 -c "assert 2*50+1==101; print('a+x50 == 101 states (PRD §7.8 linear, ok)')"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0; exactly
      three warnings (`nfa_compile`, `nfa_gen`, `get_escaped_char` unused), nothing else.
- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- [ ] Level 2: `/tmp/s2_compile_test` prints "ALL CASES CONFIRMED (0 failures)",
      including **`a+x50 n=101 (LINEAR)`** (the PRD §7.8 invariant).
- [ ] Level 3: `test_metachar_verification.c pattern_match.c` LINKS cleanly.
- [ ] Level 4: Mode-A comments present on each branch; banner references §7.8/Russ Cox;
      no simulator/pool/nfa_gen edits leaked.

### Feature Validation

- [ ] `static State *nfa_compile(const char *pat, State *pool, int *nstates_out)`
      present verbatim, appended after the M2.T1.S1 block.
- [ ] `0x2A` (glob) → `OP_SPLIT → OP_ANY` with `any->out = sp` loop-back (3 states).
- [ ] `0x0B`/`0x0C` → `OP_ASSERT`, `arg` = the byte, zero-width (2 states + chain).
- [ ] `0x0E` standalone → `continue` (skipped defensively).
- [ ] consuming `X` + `0x0E` → `OP_CHAR(X) → OP_SPLIT` with `sp->out = c` loop-back;
      `p++` consumes the marker; **2 states per X+** (linear).
- [ ] plain consuming `X` → `OP_CHAR(X)` chained via `out`.
- [ ] End → `OP_MATCH` written into the final dangling `*tail` slot.
- [ ] `lastlist` zeroed on every allocated state; `*nstates_out = n`; returns `start`.

### Code Quality Validation

- [ ] Matches the reference implementation (git `81df853` lines 79–121 / PRD §7.5)
      branch-for-branch and arrow-for-arrow (PRD §17: code wins; comment drift tolerated).
- [ ] APPEND ONLY — no modification to S1/S2/M2.T1.S1 content, `pattern_match.h`,
      `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`, `tasks.json`, `prd_snapshot.md`,
      `.gitignore`.
- [ ] No new `#include`s (only stdbool/string/stdlib already present).
- [ ] No new globals; no State pool declared; no NULL-guard on `pat`; no
      `__attribute__((unused))` suppression.
- [ ] `nfa_compile` unused-function warning accepted (self-resolves in P1.M2.T2.S2).

### Documentation & Deployment

- [ ] Mode-A inline comments explain each branch's Thompson step (glob/assert/plus/plain).
- [ ] Banner comment references PRD §7.8 (why NFA, not backtracking) + Russ Cox URL.
- [ ] The `lastlist`-zeroing loop carries a comment explaining why it is mandatory
      (first-closure de-dup; nfa_gen starts at 0).
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't overwrite or reorder `pattern_match.c` — APPEND one function after the
  M2.T1.S1 block (`static int nfa_gen = 0;`); preserve everything above.
- ❌ Don't NULL-guard `pat` — the reference does `for (*p; *p; p++)`; the caller
  (nfa_match, P1.M2.T2.S2) owns the non-NULL invariant (parse_pattern guarantees
  it; the PUBLIC pattern_match NULL-guards upstream). A guard is the wrong layer.
- ❌ Don't make `X+` non-linear — it MUST be exactly `OP_CHAR(X) → OP_SPLIT{out=c,
  out1=exit}` (2 states). Any nested/recursive expansion reintroduces the PRD §7.8
  exponential blow-up the acceptance gate exists to catch.
- ❌ Don't declare the State pool or touch `nfa_gen` — the pool is the caller's
  (nfa_match's stack); `nfa_gen` is owned by M2.T1.S1/bumped by nfa_match. This
  task only zeroes `pool[].lastlist`.
- ❌ Don't suppress the `nfa_compile` unused-function warning with
  `__attribute__((unused))` — accept it (self-resolves in P1.M2.T2.S2); same
  convention S2/S1 used for `get_escaped_char`/`nfa_gen`.
- ❌ Don't alter the `NEW()` macro — `(&pool[n < NFA_MAX_STATES ? n++ : n])` is the
  bounds-safe allocator. Replacing it with open-coded indexing risks overflow.
- ❌ Don't forget `any->out = sp` (glob) or `c->out = sp` + `sp->out = c` (plus) —
  the loop-back arrow is what makes the quantifier work. Miss it and the construct
  silently breaks.
- ❌ Don't forget `p++` after emitting the `X+` SPLIT — without it the next
  iteration re-reads the `0x0E` marker (harmlessly skipped) but you've lost the
  quantifier's association with X.
- ❌ Don't forget `*tail = m` at the end — it closes the final dangling slot so the
  graph can reach OP_MATCH.
- ❌ Don't forget the `lastlist`-zeroing loop — the simulator's first closure guards
  on `lastlist == nfa_gen` (==0); unzeroed garbage could equal a future nfa_gen and
  wrongly skip a state. PRD §7.5 mandates it.
- ❌ Don't add `#include`s (string.h/stddef.h/stdint.h/stdio.h/notifier.h) — unused
  by nfa_compile.
- ❌ Don't implement `nfa_addstate` / `nfa_match` / `nfa_has_match` here — they are
  P1.M2.T2.S1 and P1.M2.T2.S2. One function only.
- ❌ Don't run `run_all_tests.sh` to validate this task — the stub matcher makes
  true-cases fail by design; use the compile (Level 1) + #include harness (Level 2)
  + link (Level 3) gates.
- ❌ Don't touch `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
  `tasks.json`, `prd_snapshot.md`, or `.gitignore`.

---

## Confidence Score: 10/10

The exact code to write is the live source of truth (`git show 81df853:pattern_match.c`
lines 79–121; PRD §7.5 verbatim compile contract; architecture doc §"NFA Compilation")
and is reproduced branch-for-branch and arrow-for-arrow above (only Mode-A comments
are authored here, all behavior-neutral per PRD §17). The complete construct→NFA
mapping table, the state-count arithmetic for every construct, the **`a+`×50 → 101
states linear invariant**, the `lastlist`-zeroing guarantee, the build warning set
(exit 0; exactly three permitted warnings — `nfa_compile` new + `nfa_gen` +
`get_escaped_char` carried), the silent `-fsyntax-only`, and the clean test-suite
LINK were **all executed against a realistic post-S2 merged file during research
and passed** (the two initial harness "failures" were confirmed to be harness bugs —
a trailing `"->"` and a miscounted expected value — not nfa_compile defects).
Dependencies on M2.T1.S1 (State/OP_*/NFA_MAX_STATES/nfa_gen — treated as a CONTRACT)
and the boundaries with P1.M2.T2 (nfa_addstate/nfa_match/nfa_has_match — the sole
consumer) are explicit. No external dependencies are needed (libc only; the Russ
Cox URL is informational).
