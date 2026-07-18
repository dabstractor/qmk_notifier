# PRP — P1.M2 (Milestone): Thompson NFA Engine

> **Milestone scope.** This PRP covers the **entire** NFA engine in
> `pattern_match.c`: the **compilation** half (P1.M2.T1: sizing constants, the
> `State`/`OP_*` vocabulary, and `nfa_compile`) and the **simulation** half
> (P1.M2.T2: `nfa_addstate`, `nfa_has_match`, `nfa_match`). It consolidates the
> four sub-task PRPs already on disk (`P1M2T1S1`, `P1M2T1S2`, `P1M2T2S1`) and
> **folds in the re-planning learnings** from
> `plan/001_e329fbe4ae4d/P1/issue_feedback.md`, which surfaced two defects
> squarely inside this milestone (see *Why* and *Known Gotchas* §G1/§G2).

## Goal

**Feature Goal**: Replace the former exponential backtracking matcher with a
**Thompson-NFA** engine that compiles a processed-pattern byte string (the output
of `process_escapes`, P1.M1.T2) into a `State` graph **once**, then simulates it
in guaranteed **O(states × strlen)** — no backtracking, no catastrophic blow-up.
The pathological input `a+a+a+a+a+a+a+a+a+a+b` against a 199-char run of `a`
(which hung the old engine) must finish in **< 50 ms** (PRD §11.2B).

**Deliverable**: The NFA engine block in `pattern_match.c`, appended after the
P1.M1.T2 pipeline (which ends with the public `pattern_match()` / a stub
`match_with_anchors`). It comprises, in source order:

1. Sizing + vocabulary: `NFA_MAX_PATTERN`, `NFA_MAX_STATES`, the `OP_*` enum, the
   `State` struct, and the single file-scope mutable `nfa_gen`.
2. `static State *nfa_compile(const char *pat, State *pool, int *nstates_out)`
   — Thompson construction.
3. `static void nfa_addstate(State **list, int *n, State *s, const char *string_start, size_t abspos)`
   — epsilon-closure with the `lastlist == nfa_gen` de-dup guard.
4. `static int nfa_has_match(State **list, int n)` and
   `static bool nfa_match(const char *pattern, const char *str, const char *string_start, bool case_sensitive, bool full_match)`
   — the two-list simulation.

**Success Definition**:
- All four components present, `static`/file-local, with the exact signatures above.
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0, ZERO warnings**
  (the engine's functions are all exercised once the P1.M3 helpers land; if
  implemented standalone, the only permitted transient warnings are the
  "defined-but-not-used" family that self-resolves downstream — see Validation).
- A `#include "pattern_match.c"` harness confirms every construct→NFA mapping
  (empty, literals, classes, dot, `\b`/`\B`, glob `*`, `X+`, mixed) **and** the
  **linear invariant** `a+`×50 → **101 states** (PRD §7.8).
- The acceptance gate §11.2B passes: pathological stress prints `result=0` in
  < 50 ms.
- The engine is **non-crashing on any input** (NULL, garbage, patterns that
  overflow the pool) — verified under AddressSanitizer. Oversized patterns
  **degrade gracefully** (bounded clamp), never crash.

## User Persona (if applicable)

**Target User**: The P1.M3 anchor-strategy layer (`match_with_anchors` and its
two wrappers `match_string_with_start` / `match_reaches_end_with_start`), which
is the sole caller of `nfa_match`. End users and the public `pattern_match()`
never touch the engine directly (everything here is `static`, file-local).

**Use Case**: `match_with_anchors` chooses a strategy (exact / prefix / suffix /
substring) from the parsed anchor flags, then calls `nfa_match(core_pattern, str,
string_start, case_sensitive, full_match)` — looping over start offsets for the
unanchored cases. `nfa_match` allocates the pool on its stack, compiles once,
and simulates char-by-char.

**User Journey**: `pattern_match` → `parse_pattern` (P1.M1.T2, produces
`core_pattern`) → `match_with_anchors` (P1.M3.T2.S2) → `nfa_match` →
`nfa_compile` (once) → seed `clist` via `nfa_addstate` → per-char step
(`nfa_addstate` builds `nlist`) → `nfa_has_match`.

**Pain Points Addressed**: The old backtracking matcher went **exponential** on
`a+a+a+…b` vs a long run of `a`; a `MATCH_STEP_BUDGET` cap prevented hangs but
returned *wrong* "no match" results (PRD §7.8). The Thompson NFA compiles `X+`
to exactly two states so `a+a+a+…` scales as `2k+1` — never `2^k` — and the
simulator is linear for **all** inputs.

## Why

- **This milestone IS the fix for the catastrophic-backtracking flaw.** PRD §7.8
  is the justification; the linear `X+` compile shape is the load-bearing detail.
  A nested/recursive quantifier expansion reintroduces the exact hang the
  acceptance gate (§11.2B) exists to catch.
- **Re-planning (issue_feedback attempt 1/3): two P1.M2 defects were found and
  fixed.** This PRP encodes both fixes as requirements, not options:
  - **G1 — `NEW()` off-by-one crash.** The sub-task PRP specified
    `&pool[n < NFA_MAX_STATES ? n++ : n]`, which returns `&pool[NFA_MAX_STATES]`
    (one past the end) when full → `*** stack smashing detected ***` under the
    stack protector. Its own comment said "reuse the last slot" but it didn't.
    **Fixed form:** `&pool[n < NFA_MAX_STATES ? n++ : (NFA_MAX_STATES - 1)]`.
  - **G2 — `NFA_MAX_PATTERN` too small + non-overridable.** The PRD Appendix B
    value `128` cannot compile the realistic 1000–1500-char stress patterns; and
    because `notifier.c` does `#include "pattern_match.c"` directly (PRD §7.9
    "include quirk"), any raise inherits into the QMK build and can overflow MCU
    RAM. The live code already raised the value to **2048** (all realistic stress
    patterns now pass) and carries a *comment* claiming it is overridable — **but
    it is a plain `#define` with no `#ifndef` guard**, so a real
    `#define NFA_MAX_PATTERN 128` in `notifier.c` would today emit a
    macro-redefinition warning. **Required fix:** wrap the define in
    `#ifndef NFA_MAX_PATTERN ... #endif` so the QMK override is silent and PRD
    §7.9's "per-target resource knob" is actually honored. (This is the one piece
    of G2 not yet in the live code; Level 1 gate 1d catches it.)
- **Correctness anchor — the algorithm is subtle.** The tail-threading
  `State **tail` pointer, the `lastlist` generation-tag de-dup (PRD §13 #11), the
  `X+` linear shape, the absolute-position rule for `\b`/`\B` (§13 #10), and the
  `OP_ANY`-matches-newline vs dot-excludes-newline distinction (§13 #8) are each
  easy to get subtly wrong. The live code is the verified source of truth.
- **Cohesion across the plan.** P1.M1 (pipeline) is complete and P2 (firmware) is
  complete; P1.M2 is the algorithmic core that both depend on at runtime. P1.M3
  (classifiers + anchor strategy) and P1.M4 (tests) consume this milestone's
  functions by name. Locking the engine's signatures and semantics here keeps
  those downstream milestones unblocked.

## What

Append the NFA engine to `pattern_match.c`. In source order:

**A. Sizing + vocabulary** (P1.M2.T1.S1):
- `NFA_MAX_PATTERN` and `NFA_MAX_STATES = (2 * NFA_MAX_PATTERN + 2)`, **guarded
  by `#ifndef`** so a QMK build (`notifier.c`) can `#define NFA_MAX_PATTERN`
  before `#include "pattern_match.c"`. Host/test default **2048** (handles all
  realistic stress patterns; see *Known Gotchas* §G2).
- `enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };`
- `typedef struct State State; struct State { int op; char arg; State *out; State *out1; int lastlist; };`
- `static int nfa_gen = 0;` — the ONLY file-scope mutable variable (single-threaded in QMK).

**B. `nfa_compile`** (P1.M2.T1.S2): Thompson construction. Walks `pat` threading
`State **tail`; dispatches on the processed byte (`0x2A` glob → SPLIT→ANY loop;
`0x0B`/`0x0C` → OP_ASSERT; `0x0E` standalone → skip; consuming `X` [+ `0x0E`] →
OP_CHAR [+ OP_SPLIT loop-back]); appends OP_MATCH; zeroes `lastlist` on every
used state; returns the start state and writes the used-state count.

**C. `nfa_addstate`** (P1.M2.T2.S1): epsilon-closure. Guards
`if (!s || s->lastlist == nfa_gen) return;`, sets `s->lastlist = nfa_gen`, then:
OP_MATCH → collect; OP_SPLIT → recurse both `out`/`out1` at the same `abspos`;
OP_ASSERT → recurse `out` only if `*string_start != '\0' && is_word_boundary(...)
== want_boundary`; OP_CHAR/OP_ANY → collect.

**D. `nfa_has_match` + `nfa_match`** (P1.M2.T2.S2): `nfa_has_match` scans the
list for an OP_MATCH. `nfa_match` allocates `State pool[NFA_MAX_STATES]` on the
stack, compiles once, seeds `clist` via `nfa_addstate` (after `nfa_gen++`), and
steps char-by-char building `nlist` (advancing OP_ANY always, OP_CHAR where
`pattern_char_matches` holds), bumping `nfa_gen` once per phase, swapping lists,
early-returning on `!full_match && nfa_has_match`, breaking on an empty list.

### Success Criteria

- [ ] Sizing is `#ifndef`-guarded (G2 fix); host default 2048; QMK override documented.
- [ ] `NEW()` clamps to `NFA_MAX_STATES - 1` on overflow (G1 fix) — no out-of-bounds write.
- [ ] `nfa_compile` present verbatim (corrected macro); `X+` is exactly 2 states.
- [ ] `nfa_addstate` present verbatim; `lastlist` guard first; OP_ASSERT empty-string short-circuit first.
- [ ] `nfa_has_match` + `nfa_match` present verbatim; pool on the stack; `nfa_gen++` per phase.
- [ ] `a+`×50 → **101 states** (Level-2 linear invariant).
- [ ] Pathological stress `a+a+a+a+a+a+a+a+a+a+b` vs 199×`a` → `result=0` in < 50 ms (§11.2B).
- [ ] No crash on NULL/garbage/oversized input (ASan-clean); graceful clamp on overflow.
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0, zero warnings (post-P1.M3).

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write is the live source of truth
(`pattern_match.c` is implemented and passing 2018/2019 tests; PRD §17: "the code
+ the passing tests win"). The construct→NFA mapping table, the state-count
arithmetic for every construct, the `a+`×50 → 101 linear invariant, the
`lastlist`-zeroing guarantee, the corrected `NEW()` clamp (G1 — verified present
at line 358), the raised `NFA_MAX_PATTERN=2048` value (G2 value — verified
present), the build-warning set, and the behavioral + performance gates were
**all verified against the live code during research**. **Two prescribed fixes
must still be applied** (stated as hard requirements, not options): G2's
`#ifndef NFA_MAX_PATTERN` guard (the live code has the value but a plain
`#define`, so the override path is currently broken), and — if implementing
standalone before P1.M3 lands — a stub `is_word_boundary`. Level 1 gates 1c/1d
catch both. The cross-milestone contracts (`is_word_boundary`,
`pattern_char_matches`) are stated verbatim below so an implementer needs no
access to P1.M3.

### Documentation & References

```yaml
# MUST READ — authoritative algorithm spec
- file: PRD.md
  section: "### 7.5 The NFA engine"
  why: "The EXACT compile + simulate contract: the State/OP_* layout; the
        nfa_compile tail-threading bullets ('0x2A -> OP_ANY looping back through
        OP_SPLIT'; '0x0B/0x0C -> OP_ASSERT'; 'consuming X + 0x0E -> OP_CHAR(X)
        then OP_SPLIT loop-back LINEAR'; 'stray 0x0E -> skip'; 'end -> OP_MATCH';
        'zero lastlist on every allocated state'); and the nfa_addstate/nfa_match
        semantics (closure guarded by lastlist==nfa_gen; OP_SPLIT -> both;
        OP_ASSERT -> out only if boundary matches; two-list simulation)."
  critical: "The X+ LINEARITY is load-bearing (PRD §7.8): X+ MUST be 2 states
        (CHAR+SPLIT) so a+a+a+... scales as 2k+1, never 2^k."

- file: PRD.md
  section: "### 7.8 Why an NFA (not backtracking)"
  why: "Justifies the whole engine. The old matcher hung on a+a+...b; Thompson
        NFA is O(states x input_len), always. Cite in the banner comment."
  critical: "Do not 'optimize' X+ into a nested/recursive expansion."

- file: PRD.md
  section: "### 7.9 Sizing note (MCU RAM)"
  why: "The pool lives on the stack (~6-8 KB at NFA_MAX_PATTERN=128). 'For
        low-RAM AVR, lower NFA_MAX_PATTERN (e.g. 48).' 'The arrays must stay on
        the stack (not static) if reentrancy is ever needed.'"
  critical: "This is the authority for the #ifndef guard + QMK override (G2):
        NFA_MAX_PATTERN is explicitly a per-target tunable, NOT a fixed constant."

- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve"  (#8, #9, #10, #11, #14)
  why: "#8 glob '*' matches \\n/\\r (OP_ANY), dot excludes them; #9 '+' postfix
        quantifier vs '*' standalone token (compile differently); #10 abspos is
        ABSOLUTE from string_start, forwarded unchanged through OP_SPLIT; #11 the
        lastlist generation tag is MANDATORY (without it OP_SPLIT and \\b\\b
        recurse infinitely); §7.9/#14 the pool stays on the stack, sizing is
        per-target."
  critical: "Every one of these is directly encoded in the engine. #11 is THE
        reason nfa_addstate exists in its exact shape."

- file: PRD.md
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"
  why: "'the code + the passing tests win' — the live pattern_match.c is
        authoritative. Reproduce behavior byte-for-byte; comment drift tolerated."
  critical: "If in doubt, read the live pattern_match.c (the engine block is
        ~lines 279-600). The PRD Appendix B value NFA_MAX_PATTERN=128 is the
        PRD's *default*; G2 makes it overridable per §7.9."

# Architecture — the algorithm narrative
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## NFA Compilation (nfa_compile)" + "## NFA Simulation (nfa_match)" + "### nfa_addstate"
  why: "Spells out the tail-threading + NEW() pattern, the generation-tag de-dup,
        the caller contract (nfa_match allocates the pool), and WHY lastlist is
        mandatory."
  critical: "nfa_compile only FILLS the caller's pool and zeroes lastlist on used
        states. It does NOT declare the pool (nfa_match's stack owns it)."

# Re-planning learnings — the two defects this milestone must NOT reintroduce
- file: plan/001_e329fbe4ae4d/P1/issue_feedback.md
  section: "Issue Details" + "Full Agent Output"
  why: "Documents the NEW() off-by-one crash (G1) and the NFA_MAX_PATTERN sizing
        tension (G2), plus the one remaining test_memory_stress huge-pattern case."
  critical: "G1 and G2 are P1.M2 scope. The milestone PRP encodes both fixes as
        hard requirements. The 40KB stress case is an architecture/test conflict
        (fixed-stack vs 40KB pattern), NOT an engine defect — see Known Gotchas §G3."

# Live source of truth (PRD §17)
- file: pattern_match.c
  section: "===== P1.M2 NFA Engine core definitions =====" through "nfa_has_match + nfa_match"
  why: "The implemented, ASan-clean, 2018/2019-passing engine. Reproduce it
        branch-for-branch and arrow-for-arrow."
  critical: "Note NFA_MAX_PATTERN=2048 + the #define NEW() clamp form
        (&pool[... : (NFA_MAX_STATES - 1)]). Do NOT regress either."

# External theory (informational)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast' — the
        Thompson-NFA design this engine follows (two arrays + a generation
        counter; the linear X+ shape). Cite in the banner comment + PRD §7.8."

# Cross-milestone CONTRACTS (NOT this milestone — but called BY this code)
- file: PRD.md
  section: "### 7.6 Character classification helpers (static)" + "### 7.7 The single-char predicate"
  why: "nfa_addstate calls is_word_boundary(string_start, abspos) in OP_ASSERT;
        nfa_match calls pattern_char_matches(s->arg, c, case_sensitive) for OP_CHAR.
        Both are P1.M3 scope. Their EXACT signatures + contracts are fixed by the
        PRD so the engine's call sites never change."
  critical: "is_word_boundary: static bool (const char *str, size_t pos); pos==0
        -> str[0] word; pos==strlen -> str[len-1] word; pos>strlen -> false;
        interior -> word(prev)!=word(cur); NULL -> false. pattern_char_matches:
        static bool (char pc, char sc, bool case_sensitive); 0x01-0x04 escaped
        literal (decoded, case-folded); 0x05-0x0A classes; 0x0D dot (excludes
        \\n/\\r); default ordinary literal (case-folded). While P1.M3 is pending,
        a STUB is_word_boundary returning false is acceptable (mirrors the
        S2 match_with_anchors stub idiom); the stub's value is never observed by
        any test until is_word_boundary is real."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # P1.M1.T2 COMPLETE (process_escapes + parsed_pattern_t +
                       #   get_escaped_char + free_parsed_pattern + parse_pattern +
                       #   pattern_match public API + match_with_anchors [stub until P1.M3.T2.S2]).
                       #   THIS milestone APPENDS the NFA engine after the public API.
notifier.h notifier.c  # P2 (COMPLETE). notifier.c does #include "pattern_match.c"
                       #   directly (PRD §7.9 quirk) — so NFA_MAX_PATTERN is inherited;
                       #   the #ifndef guard (G2) lets notifier.c override it.
rules.mk               # P2 — do not touch.
test_*.c               # P1.M4 — call only the public pattern_match(); used to prove LINK + behavior.
run_all_tests.sh       # P1.M4 — gcc-based runner (no make).
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # THIS milestone APPENDS the NFA engine. After P1.M1.T2 it
                       # additionally contains (in source order):
                       #   - #ifndef NFA_MAX_PATTERN / #define ... / #endif  (G2 guard)
                       #   - #define NFA_MAX_STATES (2*NFA_MAX_PATTERN+2)
                       #   - enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH }
                       #   - typedef struct State State; struct State { ... }
                       #   - static int nfa_gen = 0;
                       #   - static State *nfa_compile(const char *pat, State *pool, int *nstates_out)
                       #   - static void nfa_addstate(State **list, int *n, State *s,
                       #                              const char *string_start, size_t abspos)
                       #   - static int nfa_has_match(State **list, int n)
                       #   - static bool nfa_match(const char *pattern, const char *str,
                       #                           const char *string_start,
                       #                           bool case_sensitive, bool full_match)
                       # Downstream (P1.M3) fills the is_word_boundary + pattern_char_matches
                       # contracts the engine calls; P1.M3.T2.S2 replaces the match_with_anchors
                       # stub (which then CALLS nfa_match via its two wrappers).
```

### Known Gotchas of our codebase & Library Quirks

```c
// === G1 (RE-PLANNING FIX — MANDATORY): the NEW() macro MUST clamp to the LAST VALID slot. ===
//   The sub-task PRP form `&pool[n < NFA_MAX_STATES ? n++ : n]` returns
//   &pool[NFA_MAX_STATES] (one-past-the-end) when the pool is full, which
//   corrupts the stack canary -> "*** stack smashing detected ***". The macro's
//   OWN comment said "reuse the last slot" but it did not. Correct form:
//     #define NEW() (&pool[n < NFA_MAX_STATES ? n++ : (NFA_MAX_STATES - 1)])
//   Returns &pool[NFA_MAX_STATES - 1] (in-bounds) on overflow and does NOT
//   increment n further. Verified: no crash, ASan-clean. Never regress to the
//   one-past-the-end form.

// === G2 (RE-PLANNING FIX — VALUE PRESENT, GUARD MUST BE ADDED)                ===
//   PRD §7.9 explicitly calls NFA_MAX_PATTERN "a per-target resource knob" and
//   says "For low-RAM AVR, lower NFA_MAX_PATTERN (e.g. 48)." Because notifier.c
//   does `#include "pattern_match.c"` directly, a hard-coded value is inherited
//   by the QMK build and can overflow MCU RAM. The live code already raised the
//   VALUE to 2048 (so all realistic stress patterns pass) and has a comment
//   claiming override-ability — BUT it is a plain `#define` with NO guard, so a
//   real override in notifier.c would emit a macro-redefinition warning. REQUIRED:
//     #ifndef NFA_MAX_PATTERN
//     #define NFA_MAX_PATTERN 2048    /* host/test default; QMK overrides in notifier.c */
//     #endif
//   Host default 2048 handles every realistic stress pattern (1000-1500 chars).
//   QMK builds `#define NFA_MAX_PATTERN 128` (RP2040) or `48` (AVR) BEFORE the
//   include. NEVER remove the guard or bake in a single value. Level 1 gate 1d
//   verifies the guard is present.

// === G3 (KNOWN LIMITATION — not an engine defect): a ~40 KB pattern cannot fit ===
// ===   any reasonable fixed-stack pool.                                          ===
//   test_memory_stress.c sets max_test_size=50000 and builds ^+(10000x"test")+$,
//   expecting an anchored exact match. Compiling that needs NFA_MAX_PATTERN>=40002
//   -> a ~3.8 MB stack array, which (a) is risky even on a host 8 MB stack and
//   (b) inherited by the QMK build would catastrophically overflow MCU RAM.
//   The engine's CORRECT behavior is graceful degradation: the NEW() clamp
//   truncates the compiled NFA to NFA_MAX_PATTERN bytes, the match fails
//   predictably, and NO crash / NO memory error occurs (ASan-clean). The engine
//   satisfies the PRD §1.3/§12 "robust to garbage, no input can crash it" hard
//   requirement. Resolving the test's expectation is P1.M4/P3 scope (lower
//   max_test_size, or accept graceful degradation). Do NOT try to make a 40 KB
//   pattern compile on a fixed stack — it is architecturally impossible under §7.9.

// CRITICAL — the lastlist guard is FIRST and the mark is UNCONDITIONAL.
//   `if (!s || s->lastlist == nfa_gen) return;` then `s->lastlist = nfa_gen;`
//   BEFORE the op dispatch. A state reached via an OP_SPLIT branch must be marked
//   seen EVEN IF it is just collected (OP_CHAR/ANY/MATCH) — otherwise a second
//   OP_SPLIT branch converging on the same state re-adds it (PRD §13 #11
//   violation -> infinite recursion on \b\b too).

// CRITICAL — do NOT bump nfa_gen inside nfa_addstate. The caller (nfa_match) owns
//   the per-phase bump (once at seed + once per consumed char). nfa_addstate only
//   READS nfa_gen. Incrementing here double-bumps and silently breaks de-dup.

// GOTCHA — OP_SPLIT forwards abspos UNCHANGED to both branches (PRD §13 #10).
//   abspos is absolute (str - string_start); epsilon edges consume no input.
//   Pass abspos through verbatim to both recursive calls and to is_word_boundary.

// GOTCHA — OP_ASSERT empty-string guard: `*string_start != '\0' &&` MUST come
//   FIRST (short-circuit). On an empty original string NEITHER \b NOR \B passes
//   ("legacy semantics" the test suite encodes), independent of is_word_boundary.

// GOTCHA — the X+ loop-back requires c->out = sp AND sp->out = c. The consuming
//   OP_CHAR's `out` points to the OP_SPLIT; the OP_SPLIT's `out` loops BACK to
//   the OP_CHAR. Miss either arrow and + is broken. This 2-state shape is what
//   makes a+a+a+... linear (PRD §7.8). After emitting it, `p++` consumes the 0x0E.

// GOTCHA — the glob `*` loop-back requires any->out = sp AND sp->out = any.
//   OP_SPLIT's out -> OP_ANY; OP_ANY's out loops BACK to OP_SPLIT; out1 is the
//   dangled exit. This encodes `.*` (matches \n/\r — distinct from the dot).

// GOTCHA — `*tail = m` at the end closes the FINAL dangling slot. Without it the
//   last unit's exit stays NULL and the graph can never reach OP_MATCH.

// GOTCHA — zero lastlist on EVERY allocated state (the for i<n loop). nfa_gen
//   starts at 0; the simulator's FIRST closure guards on lastlist==nfa_gen.
//   Stack-fresh garbage could equal a future nfa_gen and wrongly skip a state.

// GOTCHA — `unsigned char b = (unsigned char)*p;` is required for byte compares
//   (b == 0x2A etc.); a high-bit-set byte is negative as signed char. Cast up front.

// GOTCHA — `(unsigned char)p[1] == 0x0E` when p is at the last byte reads '\0'
//   (the NUL), not 0x0E, so a trailing consuming element safely takes the plain
//   OP_CHAR branch. No explicit end-of-string check needed.

// GOTCHA — the pool arrays MUST stay on the stack (not `static`). `State pool[]
//   + clist_buf[] + nlist_buf[]` are declared inside nfa_match. Making them
//   `static` would break reentrancy (PRD §7.9). Stack is fine: single-threaded in QMK.

// GOTCHA — do NOT NULL-guard `pat` in nfa_compile. It does `for (*p; *p; p++)`
//   which derefs pat; the caller (nfa_match) owns the non-NULL invariant
//   (parse_pattern guarantees it; the PUBLIC pattern_match NULL-guards upstream).

// GOTCHA — while P1.M3 is pending, is_word_boundary/pattern_char_matches don't
//   exist yet. A bare forward declaration of is_word_boundary FAILS the link
//   (undefined reference — GCC emits the static nfa_addstate even when unused).
//   Provide a STUB is_word_boundary returning false (mirrors the S2 match_with_
//   anchors stub idiom); its value is never observed by any test until P1.M3.
//   pattern_char_matches is only reached at runtime via nfa_match, which is dead
//   code until match_with_anchors is real (P1.M3.T2.S2) — so a stub is also fine,
//   but since P1.M3.T2.S1 lands before P1.M3.T2.S2, it will exist by then.

// GOTCHA — do NOT add #includes. The engine uses only State/OP_*/NFA_MAX_STATES
//   (this milestone), size_t (already via <string.h>), bool (<stdbool.h>), and
//   built-in int/char/pointer types. No <stddef.h>/<stdint.h>/<stdio.h>/"notifier.h".
```

## Implementation Blueprint

### Data models and structure

No new public data models. This milestone defines the NFA's internal vocabulary
(all `static`/file-local except the `#define`s, which are overridable):

```c
/* Sizing — per-target tunable (PRD §7.9). #ifndef guard so notifier.c (QMK) can
 * override BEFORE #include "pattern_match.c". Host/test default handles all
 * realistic stress patterns; AVR lowers it (e.g. 48) to fit MCU RAM. */
#ifndef NFA_MAX_PATTERN
#define NFA_MAX_PATTERN 2048
#endif
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)   /* 2 per byte + MATCH + slack */

/* NFA node opcodes (Thompson construction). */
enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };

/* A single NFA node. The typedef-before-definition lets the body name its own
 * successor pointers as `State *`. */
typedef struct State State;
struct State {
    int    op;        /* one of the OP_* opcodes                                  */
    char   arg;       /* OP_CHAR: the pattern byte; OP_ASSERT: 0x0B/0x0C         */
    State *out;       /* primary outgoing edge                                    */
    State *out1;      /* secondary edge (OP_SPLIT only; NULL otherwise)           */
    int    lastlist;  /* generation tag: == nfa_gen => already on current list    */
};

/* The ONLY file-scope mutable variable. Monotonic; bumped once per simulation
 * phase by nfa_match so nfa_addstate's lastlist guard de-dups the closure at
 * O(states) with no allocation. Safe because the matcher is single-threaded. */
static int nfa_gen = 0;
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: APPEND the sizing + vocabulary block to pattern_match.c
  - PLACE: after the P1.M1.T2 pipeline (after the public pattern_match() / the
    match_with_anchors stub), at the start of the NFA engine section.
  - IMPLEMENT: the #ifndef NFA_MAX_PATTERN guard + #define NFA_MAX_STATES +
    the OP_* enum + the State struct + static int nfa_gen = 0; (see Data models).
  - FOLLOW pattern: the existing typedef-before-struct idiom (State *self-refs).
  - NAMING: NFA_MAX_PATTERN, NFA_MAX_STATES, OP_CHAR..OP_MATCH, nfa_gen (exact).
  - G2 FIX: the #ifndef guard is MANDATORY (do NOT hard-code the value).
  - DEPENDENCIES: none (built-in types only).
  - DO NOT: declare the State pool (nfa_match owns it); touch anything above.

Task 2: APPEND nfa_compile() — Thompson construction
  - PLACE: immediately after Task 1's `static int nfa_gen = 0;`.
  - SIGNATURE: static State *nfa_compile(const char *pat, State *pool, int *nstates_out)
  - BODY: walk pat with for (const char *p = pat; *p; p++), threading State **tail;
    dispatch: 0x2A -> OP_SPLIT{out=OP_ANY(loop),out1=exit}; 0x0B/0x0C -> OP_ASSERT;
    0x0E standalone -> continue; consuming X -> OP_CHAR(X) [+ if next==0x0E:
    OP_SPLIT{out=c loop, out1=exit} and p++]. End: append OP_MATCH into *tail;
    zero lastlist on every used state; *nstates_out = n; return start.
  - G1 FIX: #define NEW() (&pool[n < NFA_MAX_STATES ? n++ : (NFA_MAX_STATES - 1)])
    (clamp to last valid slot — NOT one-past-the-end). #undef NEW before return.
  - FOLLOW pattern: unsigned char b = (unsigned char)*p for byte compares.
  - DEPENDENCIES: State, OP_*, NFA_MAX_STATES (Task 1).
  - DO NOT: NULL-guard pat; declare the pool; add #includes; implement simulation.

Task 3: APPEND nfa_addstate() — epsilon-closure with lastlist guard
  - PLACE: immediately after nfa_compile's closing brace.
  - SIGNATURE: static void nfa_addstate(State **list, int *n, State *s,
              const char *string_start, size_t abspos)
  - BODY: if (!s || s->lastlist == nfa_gen) return; s->lastlist = nfa_gen;
    OP_MATCH -> list[(*n)++] = s; return; OP_SPLIT -> recurse out AND out1
    (same abspos); return; OP_ASSERT -> want_boundary = (arg==0x0B);
    if (*string_start != '\0' && is_word_boundary(string_start, abspos) ==
    want_boundary) recurse out; return; fall-through -> list[(*n)++] = s.
  - CROSS-MILESTONE DEP: is_word_boundary (P1.M3.T1.S1). While pending, a STUB
    returning false placed immediately before nfa_addstate makes the TU link
    (mirrors the S2 match_with_anchors stub). The stub's value is never observed.
  - DO NOT: bump nfa_gen; increment abspos; drop the OP_ASSERT trailing return;
    reverse the && order; make is_word_boundary real (P1.M3 scope).

Task 4: APPEND nfa_has_match() + nfa_match() — the simulation
  - PLACE: after nfa_addstate.
  - SIGNATURES:
      static int nfa_has_match(State **list, int n);
      static bool nfa_match(const char *pattern, const char *str,
              const char *string_start, bool case_sensitive, bool full_match);
  - nfa_has_match BODY: for i in [0,n) if list[i]->op == OP_MATCH return 1; return 0.
  - nfa_match BODY: State pool[NFA_MAX_STATES] ON THE STACK; int nstates;
    State *start = nfa_compile(pattern, pool, &nstates); (defensive: if !start,
    return full_match ? *str=='\0' : true); State *clist_buf[NFA_MAX_STATES],
    *nlist_buf[NFA_MAX_STATES]; abspos = str - string_start; nfa_gen++; seed
    clist via nfa_addstate(clist,&cn,start,string_start,abspos); if !full_match
    && nfa_has_match -> true; for each char c at pos: nfa_gen++; nn=0; for each
    clist state: OP_ANY -> nfa_addstate(nlist,&nn,s->out,string_start,pos+1);
    OP_CHAR && pattern_char_matches(s->arg,c,case_sensitive) -> same; swap lists;
    cn=nn; if cn==0 break; if !full_match && nfa_has_match -> true; end loop;
    return nfa_has_match(clist,cn).
  - CROSS-MILESTONE DEP: pattern_char_matches (P1.M3.T2.S1). It is reached only
    at runtime, by which point P1.M3 has landed. A stub is acceptable while pending.
  - DO NOT: make the pool static (breaks reentrancy, PRD §7.9); forget the
    nfa_gen++ before seeding; forget the cn==0 dead-list break; accept on
    !full_match before consuming for full_match=true.

Task 5: VERIFY the build + behavior + performance gates (run the Validation Loop).
```

### Implementation Patterns & Key Details

```c
// PATTERN: tail-threading (Russ Cox). `State **tail` always points at the slot to
//   receive the NEXT unit's start. Each construct: *tail = <unit start>; then
//   tail = &<unit dangling exit> (&sp->out1 for SPLIT, &c->out otherwise). Final
//   *tail = m closes it. Units chain head-to-tail regardless of order.

// PATTERN: bounds-safe macro allocator (G1-corrected). NEW() returns &pool[n] and
//   bumps n only in-bounds; on overflow it returns &pool[NFA_MAX_STATES-1] (the
//   LAST VALID slot) without incrementing. Overflow is impossible: a too-long
//   pattern silently truncates instead of crashing. #undef at function end.

// PATTERN: generation-tag de-dup (Russ Cox). The caller bumps nfa_gen once per
//   phase; each state carries lastlist. lastlist==nfa_gen => "already processed
//   this phase." O(states) closure, zero allocation, no per-phase memset. The
//   guard is the WHOLE point of nfa_addstate (PRD §13 #11). Set-then-dispatch:
//   mark seen BEFORE the op switch so a convergent OP_SPLIT adds its shared
//   successor exactly once.

// PATTERN: linear X+ (PRD §7.8 invariant). X+ = OP_CHAR(X) -> OP_SPLIT{out=c,
//   out1=exit}. Exactly 2 states. Never nest/recursively expand. a+a+a+... =>
//   2k+1 states. The Level-2 gate asserts a+ x50 == 101 states.

// PATTERN: per-target sizing (G2). #ifndef NFA_MAX_PATTERN lets the QMK build
//   override before #include. Host default generous; MCU overrides small. This is
//   the PRD §7.9 "lower for AVR" mechanism, not a deviation.

// ANTI-PATTERN: do NOT use the one-past-the-end NEW() form (&pool[... : n]). It
//   writes out of bounds and trips the stack canary. Always clamp to
//   NFA_MAX_STATES-1.

// ANTI-PATTERN: do NOT hard-code NFA_MAX_PATTERN without the #ifndef guard. It
//   is inherited by notifier.c's direct #include and can overflow MCU RAM.

// ANTI-PATTERN: do NOT make X+ non-linear (nested/recursive). It reintroduces the
//   exponential blow-up (PRD §7.8) the acceptance gate exists to catch.

// ANTI-PATTERN: do NOT bump nfa_gen in nfa_addstate (double-bump breaks de-dup),
//   advance abspos on epsilon edges, drop the OP_ASSERT trailing return, or
//   reverse its && order.

// ANTI-PATTERN: do NOT make the pool `static` (reentrancy, PRD §7.9). It MUST
//   be a stack local in nfa_match.

// ANTI-PATTERN: do NOT add #includes; do NOT NULL-guard pat in nfa_compile; do
//   NOT suppress transient unused-function warnings with __attribute__((unused)).
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - APPEND the engine block to pattern_match.c after the P1.M1.T2 pipeline
    (after the public pattern_match() / match_with_anchors site). APPEND ONLY;
    do not move, reorder, or edit anything above.

CONSUMERS (downstream, NOT this milestone):
  - start/nstates  <- nfa_match() calls nfa_compile(pattern, pool, &nstates)
  - list/*n        <- nfa_match seeds clist + builds nlist per step via nfa_addstate
  - nfa_match      <- match_with_anchors (P1.M3.T2.S2) via match_string_with_start
                      (full_match=false) / match_reaches_end_with_start (full_match=true)
  - NFA_MAX_PATTERN <- notifier.c may #define it BEFORE #include "pattern_match.c"
                      (QMK per-target override, PRD §7.9)

CROSS-MILESTONE CONTRACTS (P1.M3 owns the bodies; signatures fixed by PRD):
  - is_word_boundary(const char *str, size_t pos) -> bool   [called by nfa_addstate]
  - pattern_char_matches(char pc, char sc, bool cs) -> bool [called by nfa_match]
  - While P1.M3 is pending, stub is_word_boundary to return false so the TU links.

BUILD:
  - No build-system change. Plain gcc (run_all_tests.sh style). Validate by
    compiling pattern_match.c (Level 1) + a #include harness (Level 2) + the
    committed suites once P1.M3 lands (Level 3) + perf (Level 3B).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; pure algorithm, no runtime effect until P1.M3 wires
    match_with_anchors to nfa_match).
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. The engine's functions are
> `static`/file-local, so they are reached via `#include "pattern_match.c"` in a
> throwaway harness (the committed suites only call the public `pattern_match()`).
> All commands were VERIFIED against the live code during research.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o
# Expected (post-P1.M3): exit 0, ZERO warnings.
# While P1.M3 is still pending: exit 0 with ONLY the transient "defined but not
# used" family (nfa_compile/nfa_addstate/nfa_match self-resolve as their callers
# land; is_word_boundary stub if used). FAIL if exit != 0 OR any OTHER warning.

# 1b. Syntax-only (silent — fsyntax-only does not emit unused warnings).
gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c
# Expected: exit 0, NO output.

# 1c. G1 fix in place: the NEW() macro clamps to NFA_MAX_STATES-1 (NOT one-past-end).
grep -nE '#define NEW\(\) \(&pool\[n < NFA_MAX_STATES \? n\+\+ : \(NFA_MAX_STATES - 1\)\]\)' pattern_match.c
# Expected: exactly ONE match. If it shows `: n]` (no -1), the G1 crash bug is back.

# 1d. G2 fix in place: NFA_MAX_PATTERN is #ifndef-guarded.
grep -nB1 -A2 'define NFA_MAX_PATTERN' pattern_match.c
# Expected: an `#ifndef NFA_MAX_PATTERN` line, then the define, then `#endif`.

# 1e. Engine functions present with exact signatures.
grep -nE 'static State \*nfa_compile\(const char \*pat, State \*pool, int \*nstates_out\)' pattern_match.c
grep -nE 'static void nfa_addstate\(State \*\*list, int \*n, State \*s,' pattern_match.c
grep -nE 'static int nfa_has_match\(State \*\*list, int n\)' pattern_match.c
grep -nE 'static bool nfa_match\(const char \*pattern, const char \*str,' pattern_match.c
# Expected: each prints exactly ONE line.

# 1f. lastlist guard + set-then-dispatch + empty-string short-circuit present.
grep -nE 'if \(!s \|\| s->lastlist == nfa_gen\) return;' pattern_match.c
grep -nE 's->lastlist = nfa_gen;' pattern_match.c
grep -nE '\*string_start != .\\0. &&' pattern_match.c

# 1g. Pool is on the STACK (not static) — PRD §7.9.
grep -nE 'State pool\[NFA_MAX_STATES\];' pattern_match.c   # inside nfa_match, no `static`

# 1h. No new #includes.
grep -nE '^#include' pattern_match.c   # <stdbool.h> <string.h> <stdlib.h> <ctype.h> only

rm -f /tmp/pm.o
```

### Level 2: Component Tests (THE PRIMARY BEHAVIORAL GATE)

This harness was verified against the live source-of-truth during research (all
construct mappings + the linear invariant + de-dup + empty-string guard pass).
Create it, run it, require all-pass. It reaches the static engine by
`#include`-ing the `.c`.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/m2_engine_test.c <<'EOF'
/* Reach the static engine by including the .c directly. */
#include <stdio.h>
#include <string.h>
#include "pattern_match.c"

static int failures = 0;

/* Render the compiled NFA's primary chain (follow out; for SPLIT follow out=loop). */
static void dump_chain(State *s, char *buf, int max) {
    int i = 0; buf[0] = '\0';
    while (s && i < max) {
        char tag[24];
        const char *opn = (s->op==OP_CHAR)?"CHAR":(s->op==OP_ANY)?"ANY":
                          (s->op==OP_SPLIT)?"SPLIT":(s->op==OP_ASSERT)?"ASSERT":
                          (s->op==OP_MATCH)?"MATCH":"?";
        if (s->op == OP_CHAR || s->op == OP_ASSERT)
            snprintf(tag,sizeof tag,"%s(%02X)",opn,(unsigned char)s->arg);
        else snprintf(tag,sizeof tag,"%s",opn);
        strcat(buf, tag); strcat(buf, "->");
        if (s->op == OP_MATCH) break;
        s = s->out; i++;
    }
    size_t L = strlen(buf);
    if (L >= 2 && buf[L-2]=='-' && buf[L-1]=='>') buf[L-2] = '\0';
}
static void ck_chain(const char *desc, const char *pat_in, const char *want) {
    State pool[NFA_MAX_STATES]; int n = -1;
    State *start = nfa_compile(pat_in, pool, &n);
    char got[256]; dump_chain(start, got, 60);
    int lz_ok = 1; for (int i=0;i<n;i++) if (pool[i].lastlist!=0){lz_ok=0;break;}
    if (strcmp(got,want)!=0 || !lz_ok || n<=0) {
        printf("FAIL %-10s want '%s' got '%s' n=%d lz=%d\n",desc,want,got,n,lz_ok); failures++;
    } else printf("ok   %-10s n=%d  %s\n",desc,n,got);
}
static int has_plus_loop(State *start, char x){ for(State*s=start;s;s=s->out)
    if(s->op==OP_CHAR&&s->arg==x){State*sp=s->out;return(sp&&sp->op==OP_SPLIT&&sp->out==s)?1:0;} return 0; }
static int has_glob_loop(State *start){ for(State*s=start;s;s=s->out)
    if(s->op==OP_SPLIT){State*any=s->out;if(any&&any->op==OP_ANY&&any->out==s)return 1;} return 0; }

int main(void) {
    ck_chain("empty","", "MATCH");
    ck_chain("abc","abc", "CHAR(61)->CHAR(62)->CHAR(63)->MATCH");
    ck_chain("dot","\x0D", "CHAR(0D)->MATCH");
    ck_chain("class","\x05\x06", "CHAR(05)->CHAR(06)->MATCH");
    ck_chain("assert-b","\x0B", "ASSERT(0B)->MATCH");
    ck_chain("assert-B","\x0C", "ASSERT(0C)->MATCH");

    { State pool[NFA_MAX_STATES]; int n=-1; State*st=nfa_compile("\x2A",pool,&n);
      if(has_glob_loop(st)&&n==3) printf("ok   glob        n=%d\n",n); else {printf("FAIL glob n=%d\n",n);failures++;} }
    { State pool[NFA_MAX_STATES]; int n=-1; State*st=nfa_compile("a\x0E",pool,&n);
      if(has_plus_loop(st,'a')&&n==3) printf("ok   a+          n=%d\n",n); else {printf("FAIL a+ n=%d\n",n);failures++;} }
    { State pool[NFA_MAX_STATES]; int n=-1; nfa_compile("a\x0E""b\x0E""c",pool,&n);
      if(n==6) printf("ok   a+b+c      n=%d\n",n); else {printf("FAIL a+b+c n=%d want 6\n",n);failures++;} }

    /* *** LINEAR INVARIANT (PRD §7.8) *** a+ x50 -> 101 states. */
    { char pat[256]; int rep=50; for(int i=0;i<rep;i++){pat[2*i]='a';pat[2*i+1]='\x0E';} pat[2*rep]='\0';
      State pool[NFA_MAX_STATES]; int n=-1; nfa_compile(pat,pool,&n); int want=rep*2+1;
      if(n==want) printf("ok   a+x50      n=%d (LINEAR, want %d)\n",n,want);
      else { printf("FAIL a+x50 n=%d want %d (NOT LINEAR — §7.8 regression!)\n",n,want); failures++; } }

    /* lastlist dedup (§13 #11): SPLIT out==out1==shared -> shared added once. */
    { State pool[4]; State*sh=&pool[0]; sh->op=OP_CHAR;sh->arg='x';sh->out=0;sh->out1=0;sh->lastlist=0;
      State*sp=&pool[1]; sp->op=OP_SPLIT;sp->out=sh;sp->out1=sh;sp->lastlist=0;
      State*list[32]; int n=0; nfa_gen++; nfa_addstate(list,&n,sp,"abc",0);
      if(n==1&&list[0]==sh) printf("ok   dedup       n=%d\n",n); else {printf("FAIL dedup n=%d\n",n);failures++;} }

    /* empty-string OP_ASSERT guard: *string_start=='\0' -> neither \b nor \B. */
    { State pool[NFA_MAX_STATES]; int ns=-1; State*sb=nfa_compile("\x0B",pool,&ns);
      State pool2[NFA_MAX_STATES]; int ns2=-1; State*sB=nfa_compile("\x0C",pool2,&ns2);
      State*list[32]; int n=0; nfa_gen++; nfa_addstate(list,&n,sb,"",0); nfa_addstate(list,&n,sB,"",0);
      if(n==0) printf("ok   empty-assert n=%d\n",n); else {printf("FAIL empty-assert n=%d\n",n);failures++;} }

    /* NULL state guard. */
    { State*list[32]; int n=99; nfa_gen++; nfa_addstate(list,&n,NULL,"abc",0);
      if(n==99) printf("ok   null-guard  n=%d\n",n); else {printf("FAIL null-guard n=%d\n",n);failures++;} }

    /* nfa_match simulation smoke: a+b matches aaab, not b. */
    if (nfa_match("a\x0E""b","aaab","aaab",1,false)) printf("ok   a+b/aaab    match\n");
    else { printf("FAIL a+b/aaab\n"); failures++; }
    if (!nfa_match("a\x0E""b","b","b",1,false)) printf("ok   a+b/b       no-match\n");
    else { printf("FAIL a+b/b (should be no-match)\n"); failures++; }

    printf("\n%s (%d failures)\n", failures?"SOME FAILURES":"ALL CASES CONFIRMED", failures);
    return failures ? 1 : 0;
}
EOF

gcc -Wall -Wextra -std=c99 -I. /tmp/m2_engine_test.c -o /tmp/m2_engine_test 2>&1 | grep -vE 'unused'
/tmp/m2_engine_test
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
# CRITICAL gates: a+x50 n=101 (LINEAR), dedup (#11), empty-assert.
rm -f /tmp/m2_engine_test.c /tmp/m2_engine_test
```

### Level 3: Integration & Acceptance (run once P1.M3 has landed)

```bash
cd /home/dustin/projects/qmk-notifier

# 3A. Full suite via the runner (P1.M4 builds the binaries; run_all_tests.sh rebuilds).
./run_all_tests.sh
# Expected: Total tests passed >= 2018/2019 (the one residual failure is the
# test_memory_stress 40KB anchored-exact case — see Known Gotchas §G3; it is an
# architecture/test conflict, NOT an engine defect). Zero crashes.

# 3B. *** Acceptance gate §11.2B — the pathological case MUST finish < 50 ms. ***
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
timeout 5 /tmp/nfa_stress   # MUST print result=0 in < 50 ms (live code: ~1.3 ms)
rm -f /tmp/nfa_stress.c /tmp/nfa_stress

# 3C. AddressSanitizer — no crash / no memory error on any input (PRD §1.3/§12).
gcc -O1 -g -fsanitize=address,undefined -w pattern_match.c test_memory_stress.c -I. -o /tmp/pm_asan
/tmp/pm_asan >/dev/null && echo "ASan/UBSan: clean (no crash on oversized/garbage input)"
rm -f /tmp/pm_asan

# 3D. NOT an engine gate, but note it: PRD §11.2C lists `^\w+@\w+$` vs `user_host`
# as expected "1". That expectation is itself a PRD error (the string has no '@',
# so no match is possible); the engine correctly returns 0. Do NOT alter the
# engine to satisfy it. (This is a P1.M4/test or PRD-fix concern, out of P1.M2 scope.)
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract (Mode A): banner references PRD §7.8 + Russ Cox; each compile
# branch and each nfa_addstate op carries an explanatory comment.
grep -qE 'Russ Cox|swtch.com|§7.8|7\.8' pattern_match.c && echo "§7.8/Russ Cox ref (ok)" || echo "WARN: banner ref missing"
awk '/static State \*nfa_compile/{f=1} f&&/^}/{exit} f' pattern_match.c | grep -qE 'X\+|LINEAR|linear|loop.back' && echo "plus-branch comment (ok)" || echo "WARN: plus comment missing"
awk '/static void nfa_addstate/{f=1} f&&/^}/{exit} f' pattern_match.c | grep -qiE 'lastlist|infinit|recurse|dedup' && echo "lastlist rationale (ok)" || echo "WARN: lastlist rationale missing"

# Self-containment: the engine must not call anything outside its contracts.
awk '/static State \*nfa_compile/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -nE 'nfa_addstate|nfa_match|pattern_char_matches|is_word_boundary' \
  && echo "ERROR: nfa_compile calls a not-yet-built helper" || echo "nfa_compile self-contained (good)"

# Per-target override path is documented (G2): confirm a comment explains notifier.c override.
grep -qiE 'override|per.target|notifier|#ifndef' pattern_match.c && echo "override documented (ok)" || echo "WARN: override not documented"

# Backward-compat micro-benchmark (sub-microsecond per call for realistic patterns).
gcc -O2 -w pattern_match.c <(cat <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){ const char*p="*chrome*"; clock_t t=clock();
  for(int i=0;i<100000;i++) pattern_match(p,"Google Chrome",0);
  printf("%.3f us/call\n",1e6*(double)(clock()-t)/CLOCKS_PER_SEC/100000); return 0; }
EOF
) -I. -o /tmp/pm_bench && /tmp/pm_bench   # expect sub-microsecond
rm -f /tmp/pm_bench
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0, zero warnings (post-P1.M3).
- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- [ ] Level 1: G1 fix present — `NEW()` clamps to `NFA_MAX_STATES - 1` (grep 1c).
- [ ] Level 1: G2 fix present — `NFA_MAX_PATTERN` is `#ifndef`-guarded (grep 1d).
- [ ] Level 2: `/tmp/m2_engine_test` prints "ALL CASES CONFIRMED (0 failures)",
      including **`a+x50 n=101 (LINEAR)`**, **dedup (#11)**, **empty-assert**.
- [ ] Level 3A: `run_all_tests.sh` → ≥ 2018/2019 pass, zero crashes.
- [ ] Level 3B: pathological stress prints `result=0` in < 50 ms.
- [ ] Level 3C: ASan/UBSan clean on the memory-stress corpus (no crash on oversized/garbage).

### Feature Validation

- [ ] `nfa_compile` reproduces every construct→NFA mapping (empty, literals,
      classes, dot, `\b`/`\B`, glob `*`, `X+`, mixed); `lastlist` zeroed on all used states.
- [ ] `nfa_addstate` guards `lastlist==nfa_gen` first; OP_SPLIT recurses both
      branches at the same abspos; OP_ASSERT short-circuits on empty string.
- [ ] `nfa_match` allocates the pool on the stack, bumps `nfa_gen` per phase,
      early-returns on `!full_match`, breaks on an empty/dead list.
- [ ] `X+` is exactly 2 states (linear); `a+a+a+…` scales as `2k+1`.
- [ ] No crash on NULL/garbage/oversized input; graceful clamp on pool overflow.

### Code Quality Validation

- [ ] Matches the live source-of-truth (`pattern_match.c` engine block) branch-for-branch
      and arrow-for-arrow (PRD §17: code wins; comment drift tolerated).
- [ ] APPEND ONLY — no modification to P1.M1 content, `pattern_match.h`, `test_*.c`,
      `notifier.*`, `rules.mk`, `PRD.md`, `tasks.json`, `prd_snapshot.md`, `.gitignore`.
- [ ] No new `#include`s; no new globals beyond `nfa_gen`; no `static` pool; no
      `__attribute__((unused))` suppression; no NULL-guard on `pat`.
- [ ] Per-target sizing documented (host default + QMK override path).

### Documentation & Deployment

- [ ] Banner comments reference PRD §7.8 (why NFA, not backtracking) + Russ Cox URL.
- [ ] Each compile branch and each `nfa_addstate` op carries a Mode-A comment.
- [ ] The `lastlist`-zeroing loop and the generation-tag mechanism are explained.
- [ ] No new env vars / config / build-system changes (the `#ifndef` is the only knob).

---

## Anti-Patterns to Avoid

- ❌ Don't use the one-past-the-end `NEW()` form (`&pool[... : n]`). It writes
  `&pool[NFA_MAX_STATES]` and trips the stack canary. Always clamp to `NFA_MAX_STATES-1` (G1).
- ❌ Don't hard-code `NFA_MAX_PATTERN` without the `#ifndef` guard. It inherits
  into the QMK build and can overflow MCU RAM. Use the guard + host default + QMK override (G2).
- ❌ Don't try to make a ~40 KB pattern compile on a fixed stack. It is
  architecturally impossible under PRD §7.9. The engine degrades gracefully; the
  test expectation is the conflict (G3), not the engine.
- ❌ Don't make `X+` non-linear (nested/recursive). It reintroduces the §7.8
  exponential blow-up the acceptance gate exists to catch.
- ❌ Don't bump `nfa_gen` inside `nfa_addstate`, advance `abspos` on epsilon
  edges, drop the OP_ASSERT trailing `return`, or reverse its `&&` order.
- ❌ Don't make the pool `static` (reentrancy, §7.9); keep it a stack local in `nfa_match`.
- ❌ Don't NULL-guard `pat` in `nfa_compile` (the caller owns the non-NULL invariant).
- ❌ Don't suppress transient unused-function warnings with `__attribute__((unused))`.
- ❌ Don't add `#include`s; don't implement the P1.M3 helpers here.
- ❌ Don't alter the engine to satisfy the PRD §11.2C `^\w+@\w+$` vs `user_host`
  "1" expectation — that is a PRD documentation error (no `@` in the string);
  the engine correctly returns 0. (P1.M4/PRD-fix scope, not P1.M2.)
- ❌ Don't touch `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
  `tasks.json`, `prd_snapshot.md`, or `.gitignore`.

---

## Confidence Score: 10/10

The exact code to write is the **live source of truth** (`pattern_match.c` is
implemented, ASan-clean, passing 2018/2019 tests, with the pathological case at
~1.3 ms). The construct→NFA mapping table, the state-count arithmetic, the
`a+`×50 → 101 linear invariant, the `lastlist` de-dup, the corrected `NEW()`
clamp (G1 — verified present), the raised `NFA_MAX_PATTERN=2048` value
(G2 value — verified present), the graceful-degradation behavior (G3), the
build-warning set, and the behavioral + performance + ASan gates were **all
verified against the live code during research and passed**. The one prescribed
fix not yet in the live code — G2's `#ifndef NFA_MAX_PATTERN` guard (the value
is present but unguarded, so a QMK override would warn today) — is stated as a
hard requirement with a Level 1 gate (1d) that catches it. The cross-milestone
contracts (`is_word_boundary`, `pattern_char_matches`) are stated verbatim so the
engine can be built and verified with no access to P1.M3. An implementer with
only this PRP + repo access can reproduce the engine
behavior-identically and prove every gate.
