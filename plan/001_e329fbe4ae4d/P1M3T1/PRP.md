# PRP — P1.M3.T1: Character Classification Helpers

> **Scope.** This PRP covers **one** step — `P1.M3.T1.S1`: implement the four
> file-local classifier functions the NFA engine and the single-char predicate
> call by name. It is the leaf-layer foundation of the pattern matcher. It does
> **not** implement `pattern_char_matches` (that is `P1.M3.T2.S1`), nor
> `match_with_anchors` + the two wrappers (that is `P1.M3.T2.S2`). Those are
> separate tasks with their own PRPs. Do only what this document specifies.

## Goal

**Feature Goal**: Land the four `static` classifier functions — `is_digit_char`,
`is_word_char`, `is_whitespace_char`, and a real position-based
`is_word_boundary` — by **replacing the STUB `is_word_boundary`** that P1.M2 left
behind (and adding the three leaf predicates that did not yet exist), so that the
NFA's `\b`/`\B` assertion (`OP_ASSERT` via `nfa_addstate`) and — once T2.S1 lands
`pattern_char_matches` — the `\d \D \w \W \s \S` classes classify characters
exactly per PRD §7.6.

**Deliverable**: A single edit to `pattern_match.c` — REPLACE the P1.M2 stub
block sitting immediately before `nfa_addstate()` with the four real functions
(in leaf-first order: the three predicates, then `is_word_boundary`). **No new
files, no other edits.** Do not touch `pattern_match.h`, the P1.M1 pipeline, the
P1.M2 engine, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`, `tasks.json`, or
`prd_snapshot.md`.

**Success Definition**:
- All four functions present, `static`, with the exact signatures in §"What".
- The P1.M2 STUB `is_word_boundary` (returned `false`) is **gone** (grep finds
  exactly one definition whose body calls `strlen` + `is_word_char`).
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0**. The three leaf
  predicates (`is_digit_char`/`is_word_char`/`is_whitespace_char`) MAY emit
  transient `-Wunused-function` warnings (see "Known Gotchas"); tolerate **only**
  those three until T2.S1 lands `pattern_char_matches`. `is_word_boundary` must
  **not** warn (its caller, `nfa_addstate`, already exists).
- A direct unit probe (Level 2 below) that `#include`s `pattern_match.c` and
  calls the four `static` functions reports **0 failures**, including: `\w`
  matches `_`; whitespace is exactly the 6 chars; the four `is_word_boundary`
  edge/interior/NULL/over-len rules; the empty-string guard returns `false`.

## User Persona (if applicable)

**Target User**: Other functions inside `pattern_match.c`, reached transitively.
No external code, no keymap, no test ever names any T1 function directly (all are
`static`, and the host test suites link `pattern_match.c` as a separate
translation unit — they cannot see `static` symbols).

**Use Case / Call graph at runtime (after the full P1.M3 milestone lands)**:
```
pattern_match() ─► match_with_anchors() [T2.S2] ─► nfa_match() [P1.M2.T2.S2]
   │                                                   │ OP_ASSERT (via nfa_addstate)
   │                                                   └─► is_word_boundary(string_start, abspos)   ◄── THIS TASK
   │                                                   │ OP_CHAR step
   │                                                   └─► pattern_char_matches(arg, c, cs) [T2.S1]
   │                                                          ├─► is_digit_char(sc)        ◄── THIS TASK
   │                                                          ├─► is_word_char(sc)         ◄── THIS TASK
   │                                                          └─► is_whitespace_char(sc)   ◄── THIS TASK
```

**Pain Points Addressed**: While P1.M2's STUB `is_word_boundary` (returns
`false`) is in place, `\b`/`\B` assertions silently match nothing. This task
makes boundary detection correct (the logic is live the moment T1 lands, though
it is not yet observable through the public API until T2.S2 — see "Why"). It also
provides the three class predicates that T2.S1's `pattern_char_matches` will call.

## Why

- **T1 is the leaf foundation; T2 builds on it.** `pattern_char_matches` (T2.S1)
  dispatches on the processed-pattern placeholder byte (`0x05`→`\d`,
  `0x07`→`\w`, `0x09`→`\s`, …) and calls exactly these three predicates. Landing
  correct, well-named predicates now makes T2.S1 a trivial switch statement.
- **Replaces the one P1.M2 stub this task owns.** P1.M2 deliberately left
  `is_word_boundary` as a `return false;` stub so the translation unit would link
  while the real classifier was pending (P1M2/PRP.md "Known Gotchas" + lines
  406–412, 493–495). The stub's value is never observed by any test while it
  stands. This task removes the stub and provides the real body. The signature
  `(const char *str, size_t pos)` is fixed by PRD §7.6 and already called by
  `nfa_addstate`, so the call site never changes.
- **Subtle correctness anchors.** Four non-obvious details are encoded as hard
  requirements below: (a) `is_word_boundary` is a **pure position test** — the
  empty-original-string `\b`/`\B` "neither matches" short-circuit lives
  **upstream** in `nfa_addstate`'s `OP_ASSERT` branch (`*string_start != '\0'`),
  NOT here; do not move it. (b) `is_word_char` **includes `_`** (`[A-Za-z0-9_]`).
  (c) `is_whitespace_char` is **exactly 6 chars** (`' ' \t \n \r \f \v`); do not
  use `<ctype.h>` `isspace`. (d) plain-`char` args need **no** unsigned cast for
  the ASCII range tests (a high-bit byte is negative as `signed char` but still
  correctly falls outside every ASCII range).
- **Cohesion across the plan.** P1.M1 (pipeline) + P1.M2 (NFA engine) are
  complete; this task supplies the classifier leaf layer; P1.M3.T2 (predicate +
  anchor strategy) then wires everything to the public `pattern_match()`; P1.M4
  (test suite, already complete) validates the whole. T1's signatures are fixed
  by PRD §7.6 and the call sites already exist in the M2 engine, so it lands with
  no change to any other file.

## What

**One edit to `pattern_match.c`** — REPLACE the P1.M2 stub block immediately
before `nfa_addstate()` with these four functions, in this order (leaf-first, so
each is defined before its caller — `is_word_boundary` calls `is_word_char`):

```c
static bool is_digit_char(char c) { return c >= '0' && c <= '9'; }

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || (c == '_');          /* underscore is a word char */
}

static bool is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n'
        || c == '\r' || c == '\f' || c == '\v';           /* exactly these 6 chars */
}

/* Word-boundary test against the ORIGINAL string (PRD §7.6, §13 #10). A boundary
 * exists at `pos` when exactly one of the neighboring characters is a word char.
 * Edge positions use an implicit non-word char on the off-string side. The
 * empty-original-string case is short-circuited inside nfa_addstate's OP_ASSERT
 * branch BEFORE this is called, but we keep the NULL + len guards defensive. */
static bool is_word_boundary(const char *str, size_t pos) {
    if (!str) return false;
    size_t str_len = strlen(str);
    if (pos == 0)        return (str_len > 0 && is_word_char(str[0]));
    if (pos == str_len)  return (str_len > 0 && is_word_char(str[str_len - 1]));
    if (pos > str_len)   return false;
    return is_word_char(str[pos - 1]) != is_word_char(str[pos]);
}
```

Preserve the explanatory banner the stub site already carries (it documents the
PRD §7.6 contract and the "empty-string short-circuit lives upstream" note).
`strlen` needs `<string.h>`, which `pattern_match.c` already `#include`s — **no
new `#include`**.

### Success Criteria

- [ ] The P1.M2 STUB `is_word_boundary` (`return false;`) is **deleted**; the
      real definition is in its place, body containing `strlen` + `is_word_char`.
- [ ] `is_digit_char`, `is_word_char`, `is_whitespace_char`, `is_word_boundary`
      are all present, `static`, with the exact signatures above, before
      `nfa_addstate`.
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0**. Tolerate
      **only** the three transient `-Wunused-function` warnings named in "Known
      Gotchas" (self-resolve at T2.S1). `is_word_boundary` must not warn.
- [ ] Level-2 direct unit probe (`#include "pattern_match.c"`) prints
      **0 failures** (covers all four functions, incl. `\w`/`_`, 6 whitespace
      chars, and every `is_word_boundary` branch).
- [ ] No new `#include`; no `__attribute__((unused))` suppression; no edit to any
      file other than `pattern_match.c`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code is the live source of truth (`pattern_match.c` is
implemented and the full suite passes at the milestone level; PRD §17: "the code
+ the passing tests win"). The four signatures (fixed by PRD §7.6 and already
called by `nfa_addstate`), the exact classifier semantics, the leaf-first
ordering, the placement-before-`nfa_addstate` constraint, the "empty-string
short-circuit stays upstream" rule, the `_`-in-word-char and 6-whitespace-char
details, and the T1-isolation validation approach (direct probe, since
`match_with_anchors` is still a stub) were **all verified against the live code**
(`pattern_match.c:448–477`) and the P1M2 PRP stub idiom during research. The
cross-milestone contract (`nfa_addstate`'s `OP_ASSERT` branch, P1.M2.T2.S1) is
stated verbatim below so an implementer needs no access to P1.M2.

### Documentation & References

```yaml
# MUST READ — authoritative spec for these four functions
- file: PRD.md
  section: "### 7.6 Character classification helpers (static)"
  why: "The EXACT is_word_boundary contract: pos==0 -> str[0] word; pos==strlen ->
        str[len-1] word; pos>strlen -> false; interior -> word(prev)!=word(cur);
        NULL -> false. Plus the three leaf predicates' character sets
        (digit '0'..'9'; word [A-Za-z0-9_]; whitespace ' \\t \\n \\r \\f \\v')."
  critical: "is_word_boundary does ONLY the position test. The empty-original-
        string short-circuit ('neither \\b nor \\B matches on \"\"') lives
        UPSTREAM in nfa_addstate's OP_ASSERT branch (P1.M2 scope). Do NOT move it
        into is_word_boundary; doing so double-handles it and diverges from the
        test suite's 'legacy semantics' (PRD §7.5)."

- file: PRD.md
  section: "### 7.5 The NFA engine" (OP_ASSERT / nfa_addstate epsilon-closure)
  why: "Documents the CALLER: nfa_addstate's OP_ASSERT branch is
        `int want_boundary = (s->arg == 0x0B);
         if (*string_start != '\\0' && is_word_boundary(string_start, abspos) == want_boundary)
             nfa_addstate(..., s->out, string_start, abspos);`
        So is_word_boundary is reached with abspos = absolute offset from the
        ORIGINAL string_start (PRD §13 #10), and the empty-string guard fires
        BEFORE the call."
  critical: "The signature (const char *str, size_t pos) and the abspos-from-
        string_start contract are FIXED. Do not change the parameter types. Do
        not add an empty-string special case (the caller already guards it)."

- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve" (#10 absolute position)
  why: "#10: 'Absolute position for \\b/\\B must be computed from string_start
        (the original string base), not from the per-offset str.' is_word_boundary
        receives string_start directly from nfa_addstate, so its `pos` is already
        absolute. Nothing for T1 to do here except NOT second-guess the caller."
  critical: "Do not rebase pos inside is_word_boundary. str IS string_start."

- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "Verified truth table, e.g. '\\bword\\b' vs 'a word here' (cs=0 -> true).
        These cases become the behavioral expectations once T2.S2 wires the
        public API; for T1 they are encoded as direct is_word_boundary probe
        assertions (interior XOR, start/end edges)."

- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## Character Classification Helpers" (lines ~156–177)
  why: "Narrative restatement of the §7.6 contract in prose, plus the
        pattern_char_matches byte->predicate dispatch table (0x05->is_digit_char,
        0x07->is_word_char, 0x09->is_whitespace_char) — the T2.S1 consumer."

# The parallel-milestone contract — what EXISTS when T1 starts
- file: plan/001_e329fbe4ae4d/P1M2/PRP.md
  section: "## What" + "### Known Gotchas" (the stub idiom) + items that call T1
  why: "Defines the STUB this task replaces: a `static bool is_word_boundary(...)
        { return false; }` placed immediately before nfa_addstate so the TU links
        while the real classifier is pending (P1M2 PRP lines 406–412, 493–495,
        592–593). Also confirms nfa_addstate ALREADY calls is_word_boundary, so
        the real body gains its caller instantly (no warning, \\b/\\B logic
        becomes correct immediately)."
  critical: "DELETE the stub; do not keep a second definition. Place the four
        functions at the SAME site (immediately before nfa_addstate). Do not add
        a forward declaration — nfa_addstate is defined AFTER this block, so a
        definition-before-use ordering needs no forward decl."

# Live source of truth (PRD §17)
- file: pattern_match.c
  section: "===== P1.M3.T1.S1: character classifiers + real is_word_boundary ====="
        (lines ~448–477, immediately before nfa_addstate)
  why: "The implemented, clean functions. Reproduce them branch-for-branch."
  critical: "Ordering: the three predicates FIRST, then is_word_boundary (it calls
        is_word_char). Keep the banner comment that states the empty-string
        short-circuit lives in nfa_addstate."

# Cross-milestone CONTRACT (NOT this task — but the caller of these functions)
- file: PRD.md
  section: "### 7.5 The NFA engine" (nfa_addstate OP_ASSERT branch)
  why: "The only runtime caller of is_word_boundary. Stated verbatim in the Why
        section so the implementer knows the empty-string guard is already done."
  critical: "is_word_boundary must NOT duplicate the empty-string guard."

# Why a direct probe (not the committed suites) validates T1
- file: test_char_classification.c
  section: "header comment (lines 1–15)"
  why: "Documents WHY the static classifiers cannot be tested by the committed
        suites: they are unreachable across a translation-unit boundary, so the
        suite exercises them INDIRECTLY via the public API. That indirect path is
        dead until match_with_anchors is real (T2.S2). Hence T1 needs a DIRECT
        probe that #includes the .c."
  critical: "Do NOT expect test_char_classification / test_word_boundary_* to pass
        after T1 alone — they require T2.S2. Validate T1 with the Level-2 probe."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # P1.M1.T2 + P1.M2 COMPLETE EXCEPT: is_word_boundary is a
                       #   STUB (returns false) sitting before nfa_addstate, and
                       #   the three leaf predicates do not yet exist. pattern_char_
                       #   matches (T2.S1) and match_with_anchors (T2.S2) are also
                       #   pending stubs. THIS task replaces the stub + adds the 3.
notifier.h notifier.c  # P2 (COMPLETE). notifier.c #include "pattern_match.c".
rules.mk               # P2 — do not touch.
test_*.c               # P1.M4 — validate via the PUBLIC pattern_match() only.
run_all_tests.sh       # P1.M4 — gcc-based runner. (Not a valid T1 gate until T2.)
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # THIS task edits ONE site (REPLACE the stub). After it,
                       # immediately before nfa_addstate(), the file contains (in
                       # source order):
                       #   - static bool is_digit_char(char c)        -> '0'..'9'
                       #   - static bool is_word_char(char c)         -> [A-Za-z0-9_]
                       #   - static bool is_whitespace_char(char c)   -> 6 ws chars
                       #   - static bool is_word_boundary(const char *str, size_t pos)
                       #                                            -> pure position test
                       # (The P1.M2 stub is_word_boundary is DELETED.)
                       # Nothing else changes. No new files.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — the empty-original-string \b/\B "neither matches" semantics live in
// nfa_addstate (P1.M2), NOT in is_word_boundary. P1.M2's nfa_addstate OP_ASSERT
// branch is:
//     int want_boundary = (s->arg == 0x0B);
//     if (*string_start != '\0' && is_word_boundary(string_start, abspos) == want_boundary)
//         nfa_addstate(..., s->out, string_start, abspos);
// The `*string_start != '\0' &&` short-circuit fires FIRST. So is_word_boundary
// itself must NOT special-case the empty string — keep it a pure position test
// (with only NULL/len guards for safety). Moving the check here double-handles
// "" and diverges from the test suite (PRD §7.5 "legacy semantics").

// CRITICAL — is_word_char includes '_' (underscore): [A-Za-z0-9_]. Easy to omit.
//   \w/\W AND \b/\B all depend on it. The reference keymap + test suite assume it.

// CRITICAL — is_whitespace_char is EXACTLY 6 chars: ' ' '\t' '\n' '\r' '\f' '\v'.
//   Do NOT use <ctype.h> isspace() — it is locale-dependent, needs an (unsigned
//   char) cast, and (for the C locale) returns the same 6 chars anyway. The
//   explicit range test is exact and locale-independent.

// GOTCHA — the classifiers take plain `char` and need NO unsigned cast for their
//   ASCII range tests (`c >= '0' && c <= '9'` etc.). A high-bit byte (>0x7F) is
//   negative as signed char but still falls outside every ASCII range -> correctly
//   returns false. Only tolower() needs the (unsigned char) cast — and that is
//   T2.S1's concern (pattern_char_matches), NOT T1's.

// GOTCHA — EXPECTED transient -Wunused-function after T1 ALONE. The three leaf
//   predicates are called ONLY by pattern_char_matches (T2.S1). Until T2.S1 lands,
//   gcc -Wall -Wextra emits exactly three warnings:
//     warning: 'is_digit_char' defined but not used
//     warning: 'is_word_char' defined but not used
//     warning: 'is_whitespace_char' defined but not used
//   These are EXPECTED and self-resolve the moment T2.S1 lands pattern_char_matches.
//   Do NOT suppress them with __attribute__((unused)) — that idiom is FORBIDDEN by
//   the P1M2 PRP (lines 571, 892) and would mask a real regression.
//   is_word_boundary must NOT warn — its caller nfa_addstate already exists.

// GOTCHA — placement is load-bearing (C needs definition before use for statics).
//   Place the block immediately BEFORE nfa_addstate (which calls is_word_boundary).
//   Within the block: the three predicates first, then is_word_boundary (it calls
//   is_word_char). No forward declaration is needed because nfa_addstate is AFTER
//   this block.

// GOTCHA — do NOT add a forward declaration of any T1 function. nfa_addstate (the
//   only caller) sits below this block, so definition-before-use suffices. A
//   redundant forward decl of a static function is legal but noisy and unnecessary.

// GOTCHA — no new #includes. is_word_boundary needs strlen (<string.h>, already
//   included by P1.M1). The predicates need nothing. Do not add <ctype.h> for the
//   classifiers.
```

## Implementation Blueprint

### Data models and structure

No new data models. T1 adds only four `static` leaf functions with primitive
arguments. It does not touch `parsed_pattern_t`, `State`, or any struct.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: REPLACE the P1.M2 stub block before nfa_addstate with the 4 real functions
  - FIND: the P1.M2 stub immediately before nfa_addstate(). It looks like:
        /* (banner) ... is_word_boundary pending ... */
        static bool is_word_boundary(const char *str, size_t pos) {
            return false;   /* STUB — real classifier is P1.M3.T1.S1 */
        }
    (Confirm via: grep -nE 'static bool is_word_boundary' pattern_match.c —
     the stub body is a bare `return false;` with NO strlen / is_word_char.)
  - DELETE: the stub body (the `return false;`). Keep or refresh the banner
    comment so it documents the PRD §7.6 contract + the "empty-string short-
    circuit lives upstream in nfa_addstate" note.
  - IMPLEMENT, in this leaf-first order so each is defined before its caller:
      static bool is_digit_char(char c)      { return c >= '0' && c <= '9'; }
      static bool is_word_char(char c)       { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')
                                                     ||(c>='0'&&c<='9')||(c=='_'); }
      static bool is_whitespace_char(char c) { return c==' '||c=='\t'||c=='\n'
                                                     ||c=='\r'||c=='\f'||c=='\v'; }
      static bool is_word_boundary(const char *str, size_t pos):
          if (!str) return false;
          size_t len = strlen(str);
          if (pos == 0)   return len > 0 && is_word_char(str[0]);
          if (pos == len) return len > 0 && is_word_char(str[len-1]);
          if (pos > len)  return false;
          return is_word_char(str[pos-1]) != is_word_char(str[pos]);
  - NAMING: exact names above (nfa_addstate / pattern_char_matches call them).
  - DEPENDENCIES: strlen (<string.h>, already included). No new #include.
  - PLACEMENT: immediately before nfa_addstate(). Nothing else moves.
  - DO NOT:
      * move/duplicate the empty-string \b/\B short-circuit into is_word_boundary
        (it stays in nfa_addstate's OP_ASSERT branch);
      * add an empty-string special case to is_word_boundary;
      * cast args to (unsigned char) for the range tests (unnecessary here);
      * use isspace()/isdigit()/isalnum() from <ctype.h>;
      * forget '_' in is_word_char;
      * suppress the expected transient unused-function warnings;
      * add a forward declaration or a new #include;
      * edit any file other than pattern_match.c.

Task 2: VERIFY the build + the direct unit probe (run the Validation Loop).
```

### Implementation Patterns & Key Details

```c
// PATTERN: leaf-first ordering for static functions. Define is_digit_char,
//   is_word_char, is_whitespace_char, THEN is_word_boundary (it calls
//   is_word_char), all before nfa_addstate (which calls is_word_boundary). C
//   requires a static function's definition (or a forward declaration) before
//   any call site; the definition-before-use order needs no forward decl.

// PATTERN: pure position test for is_word_boundary. str IS the original
//   string_start (the caller, nfa_addstate, passes it verbatim), and pos IS the
//   absolute offset. So is_word_boundary does not rebase anything — it just
//   compares the word-ness of str[pos-1] and str[pos] (with edge handling).

// PATTERN: explicit ASCII ranges, not <ctype.h>. The three predicates are exact,
//   locale-independent range tests. (isspace etc. are locale-dependent and need
//   (unsigned char) casts; the ranges avoid both footguns.)

// ANTI-PATTERN: do NOT move the empty-string \b/\B check into is_word_boundary.
//   It is an OP_ASSERT-branch concern (P1.M2). Splitting the responsibility keeps
//   "empty original string matches neither \b nor \B" exactly as the test suite
//   encodes it (PRD §7.5).

// ANTI-PATTERN: do NOT suppress the transient -Wunused-function warnings with
//   __attribute__((unused)). They are expected for the three leaf predicates
//   until T2.S1 lands pattern_char_matches, and self-resolve then.

// ANTI-PATTERN: do NOT add #includes (all needed headers are present); do NOT add
//   a forward declaration (definition-before-use suffices); do NOT touch the
//   P1.M2 engine, the P1.M1 pipeline, or any other file.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - ONE edit to pattern_match.c (REPLACE the stub block before nfa_addstate).
    APPEND/REPLACE only; do not move/reorder anything else.

CONSUMERS (upstream callers, NOT this task):
  - is_word_boundary  <- nfa_addstate() OP_ASSERT branch [P1.M2.T2.S1, COMPLETE]
                         (gain caller immediately; no warning; \b/\B logic correct)
  - is_digit_char     <- pattern_char_matches() \d/\D case  [P1.M3.T2.S1, PENDING]
  - is_word_char      <- pattern_char_matches() \w/\W case  [P1.M3.T2.S1, PENDING]
                         AND is_word_boundary() (internal, same file)
  - is_whitespace_char<- pattern_char_matches() \s/\S case  [P1.M3.T2.S1, PENDING]

CROSS-MILESTONE CONTRACT (signature fixed upstream; do not change):
  - nfa_addstate(State **list, int *n, State *s, const char *string_start,
                 size_t abspos)  [P1.M2.T2.S1] — calls is_word_boundary(string_start,
                 abspos) only inside its OP_ASSERT branch, guarded by
                 *string_start != '\0'.

BUILD:
  - No build-system change. Plain gcc. Validate by compiling pattern_match.c
    (Level 1) + a direct unit probe (Level 2). The committed suites are NOT a
    valid T1 gate until T2.S2 (match_with_anchors) lands — see Validation Loop.

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; pure functions. No runtime effect beyond making
    is_word_boundary return correct booleans for the already-wired OP_ASSERT
    branch; the leaf predicates take effect once T2.S1 calls them.)
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. The four T1 functions are
> `static`, so they are **unreachable** from a host test that links
> `pattern_match.c` as a separate translation unit (see `test_char_classification.c`
> header comment). And because `match_with_anchors` is still a P1.M2 STUB
> (returns `false`), the public `pattern_match()` is inert — the committed suites
> cannot validate T1 until T2.S2. **Therefore T1 is validated by a DIRECT UNIT
> PROBE that `#include`s `pattern_match.c`** (not `.h`) so the `static`
> functions are visible. All commands were reasoned against the live code.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o 2>/tmp/pm_warn.txt; echo "exit=$?"
# Expected: exit 0. Warnings (if any) must be ONLY the three known-transient
# -Wunused-function warnings for is_digit_char / is_word_char / is_whitespace_char
# (they vanish once T2.S1 lands pattern_char_matches). FAIL if exit != 0, or if
# ANY warning other than those three appears.
grep -E 'warning:' /tmp/pm_warn.txt | sort -u
# Expected (until T2.S1): at most the three lines:
#   pattern_match.c:NNN:7: warning: 'is_digit_char' defined but not used [-Wunused-function]
#   pattern_match.c:NNN:7: warning: 'is_word_char' defined but not used [-Wunused-function]
#   pattern_match.c:NNN:7: warning: 'is_whitespace_char' defined but not used [-Wunused-function]
# is_word_boundary MUST NOT appear here (its caller nfa_addstate already exists).

# 1b. The P1.M2 STUB is_word_boundary is GONE.
grep -nE 'static bool is_word_boundary' pattern_match.c
# Expected: exactly ONE definition whose body contains 'strlen' AND 'is_word_char'
# (NOT a bare 'return false;'). Confirm the stub is gone:
! grep -A2 'static bool is_word_boundary' pattern_match.c | grep -q 'return false;' \
  && echo "stub gone (ok)" || echo "FAIL: stub return false still present"

# 1c. All four functions present with exact signatures.
grep -qE 'static bool is_digit_char\(char c\)' pattern_match.c       && echo "is_digit_char (ok)"
grep -qE 'static bool is_word_char\(char c\)' pattern_match.c        && echo "is_word_char (ok)"
grep -qE 'static bool is_whitespace_char\(char c\)' pattern_match.c  && echo "is_whitespace_char (ok)"
grep -qE 'static bool is_word_boundary\(const char \*str, size_t pos\)' pattern_match.c \
  && echo "is_word_boundary (ok)"

# 1d. Placement: the four functions sit BEFORE nfa_addstate (definition-before-use).
awk '/static bool is_digit_char/{d=NR} /static void nfa_addstate/{a=NR}
     END{print "is_digit_char line="d"  nfa_addstate line="a; exit !(d>0 && a>0 && d<a)}' \
     pattern_match.c && echo "placement (ok)" || echo "FAIL: classifiers must precede nfa_addstate"

# 1e. is_word_char includes the underscore; is_whitespace_char has all 6 chars.
grep -qE "c == '_'" pattern_match.c                 && echo "underscore (ok)"
grep -qE "\\\\f.*\\\\v|\\\\v.*\\\\f" pattern_match.c && echo "formfeed+vtab (ok)"

# 1f. No new #includes (only the pre-existing set).
grep -nE '^#include' pattern_match.c   # expect <stdbool.h> <string.h> <stdlib.h> <ctype.h> only

rm -f /tmp/pm.o /tmp/pm_warn.txt
```

### Level 2: Direct Unit Probe (THE PRIMARY T1 BEHAVIORAL GATE)

Because the four functions are `static` and `match_with_anchors` is still a stub,
this probe `#include`s the `.c` (NOT the `.h`) so the `static` symbols are
reachable, then calls them directly. Verified against the live source-of-truth.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/t1_probe.c <<'EOF'
/* Direct unit probe for the four P1.M3.T1 classifiers.
   #include the .c (not .h) so the static functions are visible. */
#include "pattern_match.c"
#include <stdio.h>
int main(void){
  int f = 0;
  #define CK(expr) do{ if(!(expr)){ printf("FAIL: %s\n", #expr); f++; } }while(0)

  /* ---- is_digit_char : '0'..'9' only ---- */
  CK( is_digit_char('0'));  CK( is_digit_char('5'));  CK( is_digit_char('9'));
  CK(!is_digit_char('a'));  CK(!is_digit_char('A'));  CK(!is_digit_char('_'));
  CK(!is_digit_char('-'));  CK(!is_digit_char(' '));  CK(!is_digit_char('/'));
  CK(!is_digit_char(':'));  /* just past '9' */

  /* ---- is_word_char : [A-Za-z0-9_] (underscore is critical) ---- */
  CK( is_word_char('a'));  CK( is_word_char('z'));  CK( is_word_char('A'));
  CK( is_word_char('Z'));  CK( is_word_char('0'));  CK( is_word_char('9'));
  CK( is_word_char('_'));   /* <-- the easy-to-forget case */
  CK(!is_word_char('-'));  CK(!is_word_char(' '));  CK(!is_word_char('.'));
  CK(!is_word_char('\n')); CK(!is_word_char('@'));

  /* ---- is_whitespace_char : exactly ' ' \t \n \r \f \v ---- */
  CK( is_whitespace_char(' '));  CK( is_whitespace_char('\t'));
  CK( is_whitespace_char('\n')); CK( is_whitespace_char('\r'));
  CK( is_whitespace_char('\f')); CK( is_whitespace_char('\v'));
  CK(!is_whitespace_char('x'));  CK(!is_whitespace_char('_'));
  CK(!is_whitespace_char('\0'));

  /* ---- is_word_boundary : pure POSITION test (str == original string_start) ---- */
  /* pos==0 : boundary iff str[0] is a word char */
  CK( is_word_boundary("foo", 0));       /* str[0]='f' word -> true  */
  CK(!is_word_boundary(" foo", 0));      /* str[0]=' '      -> false */
  CK(!is_word_boundary("", 0));          /* len 0           -> false */
  /* pos==len : boundary iff last char is a word char */
  CK( is_word_boundary("foo", 3));       /* last 'o' word -> true   */
  CK(!is_word_boundary("fo ", 3));       /* last ' '      -> false  */
  CK( is_word_boundary("a",   1));       /* single word char        */
  /* pos>len -> false */
  CK(!is_word_boundary("foo", 4));
  CK(!is_word_boundary("foo", 99));
  /* interior : boundary iff word(prev) != word(cur) */
  CK( is_word_boundary("ab cd", 2));     /* 'b'(w) vs ' '(n) -> true  */
  CK( is_word_boundary("foo bar", 3));   /* 'o'(w) vs ' '(n) -> true  */
  CK(!is_word_boundary("abcd", 2));      /* 'b'(w) vs 'c'(w) -> false */
  CK( is_word_boundary("word.", 4));     /* 'd'(w) vs '.'(n) -> true  */
  /* NULL -> false */
  CK(!is_word_boundary(NULL, 0));
  CK(!is_word_boundary(NULL, 3));

  printf("%d failures\n", f);
  return f ? 1 : 0;
}
EOF
# -Wno-unused-function: silence the expected transient warnings for OTHER static
# functions in pattern_match.c (the still-stub match_with_anchors/wrappers/etc.).
gcc -Wall -std=c99 -Wno-unused-function -I. /tmp/t1_probe.c -o /tmp/t1_probe && /tmp/t1_probe
# Expected: a stream of nothing (all CK pass), then "0 failures", exit 0.
# CRITICAL gates: is_word_char('_'), all 6 whitespace chars, every is_word_boundary
# branch (start/end/interior-XOR/over-len/NULL/empty).
rm -f /tmp/t1_probe.c /tmp/t1_probe
```

### Level 3: Integration (informational — NOT a hard T1 gate)

These pass only after **T2.S2** lands `match_with_anchors`. Run them to confirm
T1 did not regress the build/link of the committed suites; expect the suites
themselves to still report the pre-T2 baseline (they cannot exercise T1 yet).

```bash
cd /home/dustin/projects/qmk-notifier

# 3A. The committed suites still BUILD with T1 in place (no link/syntax break).
for t in test_char_classification test_word_boundary_basic test_word_boundary_integration \
         test_metachar_verification; do
  gcc -o "$t" "$t.c" pattern_match.c 2>/dev/null && echo "$t builds (ok)" \
    || echo "FAIL: $t no longer builds"
done

# 3B. NOTE — do NOT assert these suites PASS yet. Until match_with_anchors (T2.S2)
# is real, pattern_match() returns false for every input, so the \b/\B and class
# cases will FAIL. That is the expected pre-T2 baseline, not a T1 regression.
# T1's behavioral correctness is established by the Level-2 direct probe alone.

# 3C. ASan/UBSan — the probe must be clean (no UB in the classifiers).
gcc -O1 -g -fsanitize=address,undefined -std=c99 -Wno-unused-function -I. \
    /tmp/t1_probe.c -o /tmp/t1_asan 2>/dev/null && /tmp/t1_asan >/dev/null \
  && echo "ASan/UBSan: clean" || echo "ASan/UBSan: issue (rebuild probe first)"
rm -f /tmp/t1_asan
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract (Mode A): the classifier block carries an explanatory banner
# referencing PRD §7.6 and the "empty-string short-circuit lives upstream" note.
grep -qE '7\.6|Character classification|word.boundary' pattern_match.c \
  && echo "classifier banner (ok)" || echo "FAIL: missing banner"
grep -qE 'empty.*string|nfa_addstate|short.circuit|upstream' pattern_match.c \
  && echo "empty-string-responsibility note (ok)" || echo "note missing (cosmetic)"

# High-bit byte safety: a byte > 0x7F is negative as signed char but must return
# false from every predicate (it falls outside every ASCII range). Extend the
# probe mentally: is_digit_char((char)0x80) == false, is_word_char((char)0xFF)
# == false, is_whitespace_char((char)0xA0) == false. (Already covered by the
# range tests — no signed-char cast needed. Documented for confidence.)
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0**, with
      **at most** the three transient `-Wunused-function` warnings for
      `is_digit_char`/`is_word_char`/`is_whitespace_char` (self-resolve at T2.S1).
- [ ] Level 1: the P1.M2 STUB `is_word_boundary` is GONE (grep 1b); all four
      functions present with exact signatures (1c); placed before `nfa_addstate`
      (1d); `is_word_char` has `_` and `is_whitespace_char` has `\f`/`\v` (1e);
      no new `#include` (1f).
- [ ] Level 2: `/tmp/t1_probe` prints **0 failures** (all four functions, incl.
      `\w`/`_`, 6 whitespace chars, every `is_word_boundary` branch).
- [ ] Level 3: the four committed classification suites still **build** with T1
      in place (they are not expected to *pass* until T2.S2 — that is the
      pre-T2 baseline, not a T1 defect).
- [ ] Level 4: the classifier banner documents PRD §7.6 and the empty-string
      responsibility note.

### Feature Validation

- [ ] `is_digit_char` matches `'0'..'9'` exactly.
- [ ] `is_word_char` matches `[A-Za-z0-9_]` — underscore **included**.
- [ ] `is_whitespace_char` matches exactly `' ' \t \n \r \f \v` — no `<ctype.h>`.
- [ ] `is_word_boundary` implements the exact PRD §7.6 position rule
      (NULL→false, `pos==0`→first-is-word, `pos==len`→last-is-word,
      `pos>len`→false, interior→word(prev)!=word(cur)) with **no** empty-string
      special case (that stays in `nfa_addstate`).

### Code Quality Validation

- [ ] Matches the live source-of-truth branch-for-branch (PRD §17: code wins).
- [ ] REPLACE only — no modification to P1.M1 content, the P1.M2 engine,
      `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
      `tasks.json`, `prd_snapshot.md`, `.gitignore`.
- [ ] Leaf-first ordering (predicates before `is_word_boundary`); block before
      `nfa_addstate`.
- [ ] No new `#include`; no forward declaration; no `__attribute__((unused))`;
      no use of `<ctype.h>` `isspace`/`isdigit`/`isalnum`; no `(unsigned char)`
      cast on the range tests.

### Documentation & Deployment

- [ ] The classifier banner references PRD §7.6 and states the empty-string
      `\b`/`\B` short-circuit lives upstream in `nfa_addstate`.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't move the empty-string `\b`/`\B` check into `is_word_boundary`. It
  lives in `nfa_addstate`'s `OP_ASSERT` branch (`*string_start != '\0' &&`).
  `is_word_boundary` is a pure position test.
- ❌ Don't forget `_` in `is_word_char` (`[A-Za-z0-9_]`) — `\w`/`\W`/`\b`/`\B`
  all depend on it.
- ❌ Don't use `<ctype.h>`'s `isspace`/`isdigit`/`isalnum` — locale-dependent and
  need casts; the explicit ranges are exact and match the PRD sets.
- ❌ Don't suppress the transient `-Wunused-function` warnings with
  `__attribute__((unused))` — they are expected for the three leaf predicates
  until T2.S1 lands `pattern_char_matches`, and self-resolve then.
- ❌ Don't cast the predicate args to `(unsigned char)` — unnecessary for the
  ASCII range tests (a high-bit byte is negative as `signed char` but correctly
  falls outside every range). Only `tolower` needs the cast — and that is T2.S1.
- ❌ Don't expect the committed suites to pass after T1 alone — `match_with_anchors`
  is a stub until T2.S2, so the public API is inert. Validate T1 with the direct
  Level-2 probe.
- ❌ Don't add a second `is_word_boundary` definition (delete the stub); don't add
  a forward declaration (definition-before-use suffices); don't add `#include`s;
  don't touch the P1.M2 engine.

---

## Confidence Score

**9/10** — One-pass success is highly likely. The live code is the verified
source of truth (PRD §17); the exact bodies, the leaf-first ordering, the
placement-before-`nfa_addstate`, the stub to delete, the empty-string-responsibility
boundary, the `_`-in-word-char and 6-whitespace-char details, and the
T1-isolation validation approach (direct `#include` probe, since the public API
is inert until T2.S2) are all documented and gated. The only residual risk is an
implementer either (a) moving the empty-string `\b`/`\B` check into
`is_word_boundary` (double-handling `""`), or (b) mis-reading the expected
transient unused-function warnings as a failure and "fixing" them with
`__attribute__((unused))` — both are encoded as hard anti-patterns and gated by
Level 1/2 checks.