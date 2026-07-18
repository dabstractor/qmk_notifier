# PRP — P1.M3 (Milestone): Character Classification & Anchor Strategy

> **Milestone scope.** This PRP covers the **three** P1.M3 sub-tasks that finish
> the pattern matcher: the **character classifiers** (T1.S1: `is_digit_char`,
> `is_word_char`, `is_whitespace_char`, `is_word_boundary`), the **single-char
> predicate** (T2.S1: `pattern_char_matches`), and the **anchor-strategy layer**
> (T2.S2: real `match_with_anchors` + the two wrappers
> `match_string_with_start` / `match_reaches_end_with_start`). Together these
> **replace the stubs P1.M2 left behind** and wire the NFA (P1.M2) to the public
> `pattern_match()` (P1.M1), making the matcher fully behavioral.

## Goal

**Feature Goal**: Land the four file-local functions the NFA engine calls by name
(`is_word_boundary`, `pattern_char_matches`) plus the anchor-strategy dispatcher
(`match_with_anchors`) and its two thin NFA wrappers — so that the public
`pattern_match()` produces correct results for **every** construct in PRD §15
(wildcards, anchors, escapes, `\d\D\w\W\s\S`, `\b\B`, `.`, `X+`, two-part/empty
cores) with no crash on any input.

**Deliverable**: Four edits to `pattern_match.c` (APPEND/REPLACE, no new files):
1. The real `match_with_anchors` **replacing** the P1.M2 stub at its early site.
2. The three class predicates + real `is_word_boundary` **replacing** the P1.M2
   `is_word_boundary` stub immediately before `nfa_addstate`.
3. `pattern_char_matches` inserted between `nfa_addstate` and `nfa_match`.
4. The two wrappers appended after `nfa_match`.

**Success Definition**:
- All six functions present, `static`/file-local, with the exact signatures below.
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0, ZERO warnings**
  (P1.M2's transient "defined-but-not-used" warnings self-resolve once these land).
- A probe harness confirms every P1.M3 behavior (classifiers via metachars, dot vs
  glob newline, escaped literals, word-boundary absolute position, all 4 anchor
  strategies, empty-core special case) → **0 failures**.
- `./run_all_tests.sh` → **≥ 2018/2019** pass (the 1 residual failure is the G3
  ~40 KB memory-stress case — P1.M4/P3 scope, NOT a P1.M3 defect).
- §11.2B pathological stress prints `result=0` in **< 50 ms** (live: ~1.8 ms).
- ASan/UBSan-clean on the error/memory-stress corpus — no crash on any input.

## User Persona (if applicable)

**Target User**: The public `pattern_match()` (P1.M1.T2.S2), which is the sole
caller of `match_with_anchors`. End users and the keymap never name any P1.M3
function directly (all are `static`).

**Use Case**: `pattern_match` → `parse_pattern` (core + anchor flags) →
`match_with_anchors` picks a strategy from the flags → calls `nfa_match` (P1.M2)
directly or via a wrapper, looping offsets for the unanchored/suffix cases.
`nfa_match` simulates the compiled NFA, calling `pattern_char_matches` (OP_CHAR)
and — through `nfa_addstate` — `is_word_boundary` (OP_ASSERT).

**User Journey**:
```
pattern_match(pattern, str, cs)
 ├─ parse_pattern → core_pattern + start_anchored + end_anchored
 └─ match_with_anchors(&parsed, str, cs)              ← P1.M3.T2.S2 (THIS MILESTONE)
     ├─ ^…$ exact   → match_reaches_end_with_start(core, str, str, cs)   ← wrapper
     ├─ ^…  prefix  → match_string_with_start(core, str, str, cs)        ← wrapper
     ├─ …$  suffix  → loop i: match_reaches_end_with_start(core, str+i, str, cs)
     └─ …   substr  → empty-core guard; loop i: match_string_with_start(core, str+i, str, cs)
                                   ↓ both wrappers call
                              nfa_match(pattern, str, string_start, cs, full_match)  ← P1.M2
                                   │ OP_CHAR step → pattern_char_matches(arg, c, cs)  ← P1.M3.T2.S1
                                   │ OP_ASSERT (via nfa_addstate) → is_word_boundary  ← P1.M3.T1.S1
```

**Pain Points Addressed**: While P1.M2's stubs are in place, `pattern_match()`
always returns `false` (stub `match_with_anchors`) and `\b`/`\B` never match
(stub `is_word_boundary`). This milestone makes the matcher actually match,
restoring the strict-superset-of-glob backward compatibility (PRD §1.3) and all
regex features the test corpus encodes.

## Why

- **P1.M3 is the behavioral finish line of the matcher.** P1.M2 (NFA) and P1.M1
  (pipeline) are inert without these functions: the stub `match_with_anchors`
  returns `false`, so **no** pattern currently matches. Landing P1.M3 is what
  turns the compiled NFA into observable correct results.
- **Replaces two P1.M2 stubs that exist only to make the TU link.** P1.M2's PRP
  (Known Gotchas) deliberately left `is_word_boundary` (returns `false`) and
  `match_with_anchors` (returns `false`) as stubs because their bodies are P1.M3
  scope. This PRP removes both stubs and provides the real implementations.
- **Correctness anchors are subtle and easy to get wrong.** Four non-obvious
  details are encoded as hard requirements below: (a) the empty-original-string
  `\b`/`\B` "neither matches" semantics live in `nfa_addstate` (P1.M2), NOT in
  `is_word_boundary` — do not move them; (b) the empty-core substring special
  case (empty core matches only the empty string); (c) `tolower` requires
  `(unsigned char)` casts in `pattern_char_matches`; (d) `is_word_char` includes
  `_` and dot excludes **both** `\n` and `\r`.
- **Cohesion across the plan.** P1.M1 (complete) + P1.M2 (implementing) +
  P1.M3 (this) together finish the matcher; P1.M4 (tests) then validates it;
  P3 runs the acceptance gate. This milestone's function signatures are fixed by
  PRD §7.6/§7.7 and already forward-declared by P1.M2, so it lands without
  touching any other file.

## What

Four edits to `pattern_match.c`, all `static`/file-local. **APPEND/REPLACE only;
do not create new files, do not edit anything above `parse_pattern` or the P1.M2
engine block, do not touch `pattern_match.h`, `test_*.c`, `notifier.*`,
`rules.mk`, `PRD.md`, or `tasks.json`.**

**A. `match_with_anchors` (T2.S2) — REPLACE the P1.M2 stub** at its site right
after `parse_pattern` (before the public `pattern_match`). Signature (already
forward-declared by P1.M1.T2.S2):
```c
static bool match_with_anchors(const parsed_pattern_t *parsed,
                               const char *str, bool case_sensitive);
```
Body: NULL/`!parsed` guard; read `core_pattern`; dispatch on the 4 anchor
combinations (PRD §7.4 table). For `^…$` call
`match_reaches_end_with_start(core, str, str, cs)`; for `^…` call
`match_string_with_start(core, str, str, cs)`; for `…$` and unanchored, loop
`i = 0..strlen(str)` calling the matching wrapper. **Substring special case:**
if `strlen(core_pattern)==0` return `strlen(str)==0`.

**B. Classifiers (T1.S1) — REPLACE the P1.M2 stub block** immediately before
`nfa_addstate`. Four functions:
```c
static bool is_digit_char(char c);          /* '0'..'9'                                          */
static bool is_word_char(char c);           /* [A-Za-z0-9_]  (NOTE: includes '_')                */
static bool is_whitespace_char(char c);     /* ' ' '\t' '\n' '\r' '\f' '\v'  (6 chars)          */
static bool is_word_boundary(const char *str, size_t pos);
```
`is_word_boundary` body (PRD §7.6): `!str`→false; `len=strlen(str)`; `pos==0`→
`len>0 && is_word_char(str[0])`; `pos==len`→`len>0 && is_word_char(str[len-1])`;
`pos>len`→false; else `is_word_char(str[pos-1]) != is_word_char(str[pos])`.

**C. `pattern_char_matches` (T2.S1) — INSERT between `nfa_addstate` and
`nfa_match`.** Signature:
```c
static bool pattern_char_matches(char pc, char sc, bool case_sensitive);
```
Body (PRD §7.7): `0x01`–`0x04` escaped literal → decode via `get_escaped_char`,
case-fold with `tolower((unsigned char)...)`; `0x05`/`0x06` →
`is_digit_char`/negation; `0x07`/`0x08` → `is_word_char`/negation;
`0x09`/`0x0A` → `is_whitespace_char`/negation; `0x0D` →
`sc != '\n' && sc != '\r'` (dot excludes both); default ordinary literal,
case-folded unless `case_sensitive`.

**D. Two wrappers (T2.S2) — APPEND after `nfa_match`.** P1.M2 already
forward-declared both; define them now:
```c
static bool match_string_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive);
/*  → nfa_match(pattern, str, string_start, case_sensitive, false);  (reach-any)  */

static bool match_reaches_end_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive);
/*  → nfa_match(pattern, str, string_start, case_sensitive, true);   (full)       */
```

### Success Criteria

- [ ] The P1.M2 STUB `is_word_boundary` (returned `false`) is GONE; replaced by
      the 3 class predicates + real `is_word_boundary` before `nfa_addstate`.
- [ ] The P1.M2 STUB `match_with_anchors` (returned `false`) is GONE; replaced by
      the real dispatcher at its early site.
- [ ] `pattern_char_matches` present between `nfa_addstate` and `nfa_match`.
- [ ] The two wrappers present after `nfa_match`, calling `nfa_match` with
      `full_match` false/true respectively.
- [ ] No stub remains anywhere (grep for any temporary stub markers → none).
- [ ] Level-2 probe (below) prints **0 failures** across all P1.M3 behaviors.
- [ ] `./run_all_tests.sh` → **≥ 2018/2019**; the only failure is the G3 memory-
      stress 40 KB case (unchanged from the P1.M2 baseline).
- [ ] §11.2B pathological stress → `result=0` in < 50 ms.
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0, ZERO warnings.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write is the live source of truth
(`pattern_match.c` is implemented and passing 2018/2019 tests; PRD §17: "the code
+ the passing tests win"). The four function signatures (fixed by PRD §7.6/§7.7
and already forward-declared by P1.M2), the exact classifier semantics, the
`tolower` cast requirement, the empty-core special case, the dot-vs-glob newline
distinction (§13 #8), the absolute-position rule for `\b`/`\B` (§13 #10), the
placement/ordering constraints, and the behavioral + performance gates were
**all verified against the live code during research** (a 23-case probe + the
full `run_all_tests.sh` both pass). The cross-milestone contracts (`nfa_match`
from P1.M2; `get_escaped_char` from P1.M1) are stated verbatim below so an
implementer needs no access to those milestones.

### Documentation & References

```yaml
# MUST READ — authoritative spec for the three sub-tasks
- file: PRD.md
  section: "### 7.6 Character classification helpers (static)"
  why: "The EXACT is_word_boundary contract: pos==0 -> str[0] word; pos==strlen
        -> str[len-1] word; pos>strlen -> false; interior -> word(prev)!=word(cur);
        NULL -> false. Plus the three leaf predicates' character sets."
  critical: "is_word_boundary does ONLY the position test. The empty-original-
        string short-circuit ('neither \\b nor \\B matches') lives UPSTREAM in
        nfa_addstate's OP_ASSERT branch (P1.M2 scope). Do NOT move it here."

- file: PRD.md
  section: "### 7.7 The single-char predicate (pattern_char_matches)"
  why: "The EXACT pc-byte -> behavior table: 0x01-0x04 escaped literal (decoded,
        case-folded); 0x05-0x0A classes (\\d\\D\\w\\W\\s\\S -> is_*_char/negation);
        0x0D dot (excludes \\n AND \\r); default ordinary literal (case-folded)."
  critical: "Decode the escaped literal via get_escaped_char(pc) FIRST, then fold
        with tolower((unsigned char)literal) == tolower((unsigned char)sc). Cast
        BOTH operands to (unsigned char) — tolower on a negative (signed char)
        value is UB. NEVER fold the placeholder byte itself."

- file: PRD.md
  section: "### 7.4 Matching strategy (match_with_anchors)"
  why: "The 4-row anchor strategy table (^...$ exact; ^ prefix; ...$ suffix loop;
        neither substring loop). 'string_start is always the ORIGINAL string base
        (so \\b/\\B can compute absolute positions).' 'Special case: an empty
        core matches only the empty string.'"
  critical: "The empty-core guard (strlen(core)==0 -> strlen(str)==0) is REQUIRED
        in the substring branch; without it the empty NFA accepts at offset 0 and
        matches any string. The suffix branch needs no guard (a suffix of empty
        core is itself empty -> only empty str reaches the end)."

- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve"  (#8, #10)
  why: "#8 glob '*' matches \\n/\\r (OP_ANY), dot '.' excludes them (the 0x0D
        branch here); #10 absolute position for \\b/\\B computed from string_start
        — match_with_anchors passes str (==string_start at offset 0) and the
        wrappers forward string_start verbatim."
  critical: "Do NOT pass a per-offset pointer as string_start. Always forward the
        ORIGINAL str base so is_word_boundary sees absolute positions."

- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "The verified truth table (e.g. '*' vs 'a\\nb' -> true; 'a.b' vs 'a\\nb' ->
        false; '\\bword\\b' vs 'a word here' cs=0 -> true; '^hello$' vs 'hello
        world' -> false). Use these as probe assertions."

# Architecture — the narrative for these exact functions
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## Character Classification Helpers" + "## pattern_char_matches" +
           "## Matching Strategy (match_with_anchors)"
  why: "Spells out the classifier semantics, the predicate byte table, and the
        4-strategy table in prose. Matches PRD §7.4/§7.6/§7.7 verbatim."

# The parallel milestone contract — what EXISTS when P1.M3 starts
- file: plan/001_e329fbe4ae4d/P1M2/PRP.md
  section: "## What" (items A-D) + "### Known Gotchas" (the stub idiom)
  why: "Defines the stubs P1.M3 must replace: a STUB is_word_boundary returning
        false (placed before nfa_addstate so the TU links) and a STUB
        match_with_anchors returning false (placed at the early site). Also the
        forward declarations of nfa_match + the two wrappers that P1.M2 already
        added (so match_with_anchors can reference them)."
  critical: "P1.M2's STUB is_word_boundary MUST be deleted, not kept alongside.
        The STUB match_with_anchors MUST be replaced at the SAME site. Do not add
        a second forward declaration of the wrappers (P1.M2 already has them)."

# Live source of truth (PRD §17)
- file: pattern_match.c
  section: "===== P1.M3.T2.S2: match_with_anchors =====" (early site) +
           "===== P1.M3.T1.S1: character classifiers + real is_word_boundary ====="
           (before nfa_addstate) + "===== P1.M3.T2.S1: pattern_char_matches ====="
           (after nfa_addstate) + "===== P1.M3.T2.S2: anchor-strategy wrappers ====="
           (end, after nfa_match)
  why: "The implemented, ASan-clean, 2018/2019-passing functions. Reproduce them
        branch-for-branch."
  critical: "Note the ordering: classifiers BEFORE nfa_addstate; pattern_char_
        matches BEFORE nfa_match; wrappers AFTER nfa_match; match_with_anchors at
        its EARLY site (replacing the stub). Forward declarations for nfa_match
        and the two wrappers already exist near get_escaped_char (P1.M1/M2)."

# Cross-milestone CONTRACTS (NOT this milestone — but called BY this code)
- file: PRD.md
  section: "### 7.5 The NFA engine" (nfa_match signature) + item-spec get_escaped_char
  why: "match_string_with_start / match_reaches_end_with_start call
        nfa_match(pattern, str, string_start, case_sensitive, full_match) — the
        P1.M2 signature. pattern_char_matches calls get_escaped_char(pc)
        (P1.M1.T2.S2) to decode 0x01-0x04. Both signatures are fixed upstream."
  critical: "nfa_match takes string_start as a SEPARATE arg (so \\b/\\B get abspos).
        Forward the ORIGINAL str base from match_with_anchors, never str+i."

# Test framework (how P1.M3 is validated indirectly)
- file: PRD.md
  section: "### 11.3 Test inventory" + "### 11.4 The test framework"
  why: "test_char_classification (179), test_word_boundary_basic (74) +
        _integration (189), test_metachar_verification, test_pattern_match (376),
        test_invalid_patterns (1008) all exercise P1.M3 functions via the public
        pattern_match(). The static helpers are never named by tests."
  critical: "Acceptance is purely behavioral. No test links is_word_boundary etc.
        directly — they are reached through the NFA."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # P1.M1.T2 + P1.M2 COMPLETE EXCEPT: is_word_boundary is a
                       #   STUB (returns false), match_with_anchors is a STUB
                       #   (returns false), pattern_char_matches + the 2 wrappers
                       #   may not yet exist. THIS milestone replaces/adds them.
notifier.h notifier.c  # P2 (COMPLETE). notifier.c #include "pattern_match.c".
rules.mk               # P2 — do not touch.
test_*.c               # P1.M4 — call only the public pattern_match(); validate LINK.
run_all_tests.sh       # P1.M4 — gcc-based runner (no make).
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # THIS milestone edits 4 sites (APPEND/REPLACE). After it
                       # additionally contains (in source order):
                       #   [early site, after parse_pattern]
                       #   - REAL match_with_anchors(parsed, str, cs)   [replaces stub]
                       #   [before nfa_addstate, replacing the stub block]
                       #   - static bool is_digit_char(char c)
                       #   - static bool is_word_char(char c)
                       #   - static bool is_whitespace_char(char c)
                       #   - static bool is_word_boundary(const char *str, size_t pos)
                       #   [between nfa_addstate and nfa_match]
                       #   - static bool pattern_char_matches(char pc, char sc, bool cs)
                       #   [end, after nfa_match]
                       #   - static bool match_string_with_start(...)     -> nfa_match(...,false)
                       #   - static bool match_reaches_end_with_start(...) -> nfa_match(...,true)
                       # Forward declarations of nfa_match + both wrappers already
                       # exist near get_escaped_char (added by P1.M1/M2) — leave them.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — the empty-original-string \b/\B "neither matches" semantics live in
// nfa_addstate (P1.M2), NOT in is_word_boundary. P1.M2's nfa_addstate OP_ASSERT
// branch is: `if (*string_start != '\0' && is_word_boundary(...) == want_boundary)
// recurse`. The `*string_start != '\0' &&` short-circuit fires FIRST. So
// is_word_boundary itself must NOT special-case the empty string — doing so
// double-handles it and risks diverging from the test suite's "legacy semantics"
// (PRD §7.5: "Empty original string: neither boundary nor non-boundary"). Keep
// is_word_boundary a pure position test (with only NULL/len guards for safety).

// CRITICAL — the empty-core SUBSTRING special case. match_with_anchors' default
// (no anchors) branch: `if (strlen(core_pattern) == 0) return strlen(str) == 0;`
// Without this, the offset loop calls match_string_with_start with an empty core
// and the empty NFA accepts at offset 0 -> matches ANY string. The exact/suffix
// branches need no guard (empty core + full_match against non-empty str naturally
// fails; empty core + full_match against "" succeeds). Only the substring branch
// needs the explicit guard.

// CRITICAL — tolower needs (unsigned char) casts in pattern_char_matches.
//   WRONG: tolower(pc) == tolower(sc)          // pc/sc may be negative (signed char) -> UB
//   RIGHT: tolower((unsigned char)literal) == tolower((unsigned char)sc)
// Decode the escaped literal FIRST (get_escaped_char(pc) for 0x01-0x04), THEN cast.
// Never fold the placeholder byte (0x01 etc.) itself — its case-fold is meaningless.

// GOTCHA — dot 0x0D excludes BOTH '\n' AND '\r': `sc != '\n' && sc != '\r'`.
//   Glob '*' (OP_ANY, P1.M2) includes them. This is PRD §13 #8 — do not conflate.

// GOTCHA — is_word_char includes '_' (underscore): [A-Za-z0-9_]. Easy to omit,
//   and \w/\W + \b/\B all depend on it. The reference keymap and test suite assume it.

// GOTCHA — is_whitespace_char is exactly 6 chars: ' ' '\t' '\n' '\r' '\f' '\v'.
//   Matches isspace() for the C locale. Do NOT use isspace() itself (it is
//   locale-dependent + needs an unsigned char cast + pulls the same 6 chars).

// GOTCHA — the classifiers take plain `char` and need NO unsigned cast for their
//   ASCII range tests (`c >= '0' && c <= '9'` etc.). A high-bit byte (>0x7F) is
//   negative as signed char but still falls outside every ASCII range -> correctly
//   returns false. Only tolower() needs the cast (in pattern_char_matches).

// GOTCHA — placement is load-bearing (C needs definition before use for statics):
//   - 3 predicates + is_word_boundary: BEFORE nfa_addstate (it calls is_word_boundary).
//   - pattern_char_matches: AFTER nfa_addstate, BEFORE nfa_match (it calls the
//     predicate; nfa_match calls it). It also calls get_escaped_char (defined early).
//   - wrappers: AFTER nfa_match (they call it). P1.M2 forward-declared them so
//     the early match_with_anchors can reference them.
//   - match_with_anchors: at the EARLY site (after parse_pattern), REPLACING the
//     stub. P1.M2 forward-declared nfa_match + the wrappers near get_escaped_char.

// GOTCHA — do NOT add a second forward declaration of the wrappers or of
//   match_with_anchors. P1.M1.T2.S2 already forward-declared match_with_anchors;
//   P1.M2 already forward-declared nfa_match + both wrappers. Duplicate forward
//   declarations of static functions are legal in C but unnecessary and noisy.

// GOTCHA — string_start forwarding. match_with_anchors passes `str` (the original
//   base) as BOTH the match position AND string_start in the anchored cases, and
//   `str + i` as the position with `str` as string_start in the loop cases. The
//   wrappers forward string_start verbatim to nfa_match. NEVER pass str+i as
//   string_start — is_word_boundary would compute positions relative to the wrong
//   base (PRD §13 #10).

// GOTCHA — the suffix/substring loops run `i = 0 .. strlen(str)` INCLUSIVE of
//   strlen(str) (the `<=' form, or `< strlen(str)+1`). The final offset (i==len)
//   lets an empty-tail core match the end. Off-by-one here drops legitimate suffix
//   matches.

// GOTCHA — no new #includes. is_word_boundary needs strlen (<string.h>, already
//   included); pattern_char_matches needs tolower (<ctype.h>, already included
//   by P1.M2 for this exact purpose); all else is built-in.
```

## Implementation Blueprint

### Data models and structure

No new public data models. This milestone uses the `parsed_pattern_t` (P1.M1.T2.S2)
already in scope and adds only `static` leaf functions. The `match_with_anchors`
dispatcher reads `parsed->core_pattern`, `parsed->start_anchored`,
`parsed->end_anchored` — all defined by P1.M1.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: REPLACE the STUB is_word_boundary + ADD the 3 class predicates (T1.S1)
  - PLACE: immediately before nfa_addstate(), REPLACING the P1.M2 stub block
    (the stub `static bool is_word_boundary(...) { return false; }`).
  - DELETE: the stub is_word_boundary entirely.
  - IMPLEMENT (in this order, so each function is defined before its caller):
      static bool is_digit_char(char c)      { return c >= '0' && c <= '9'; }
      static bool is_word_char(char c)       { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')
                                                     ||(c>='0'&&c<='9')||(c=='_'); }
      static bool is_whitespace_char(char c) { return c==' '||c=='\t'||c=='\n'
                                                     ||c=='\r'||c=='\f'||c=='\v'; }
      static bool is_word_boundary(const char *str, size_t pos):
        if (!str) return false; size_t len = strlen(str);
        if (pos == 0)      return len > 0 && is_word_char(str[0]);
        if (pos == len)    return len > 0 && is_word_char(str[len-1]);
        if (pos > len)     return false;
        return is_word_char(str[pos-1]) != is_word_char(str[pos]);
  - NAMING: exact names above (called by nfa_addstate / pattern_char_matches).
  - DEPENDENCIES: strlen (<string.h>, already included). No new #include.
  - DO NOT: move the empty-string \b/\B short-circuit here (it stays in nfa_addstate);
    add an empty-string special case; cast args for the range tests; use isspace().

Task 2: ADD pattern_char_matches() between nfa_addstate and nfa_match (T2.S1)
  - PLACE: after nfa_addstate's closing brace, before `/* ===== nfa_has_match`.
  - SIGNATURE: static bool pattern_char_matches(char pc, char sc, bool case_sensitive)
  - BODY:
      if (pc >= '\x01' && pc <= '\x04') {            /* escaped literal */
          char literal = get_escaped_char(pc);
          return case_sensitive ? (literal == sc)
              : (tolower((unsigned char)literal) == tolower((unsigned char)sc));
      }
      switch (pc) {
          case '\x05': return is_digit_char(sc);          /* \d */
          case '\x06': return !is_digit_char(sc);         /* \D */
          case '\x07': return is_word_char(sc);           /* \w */
          case '\x08': return !is_word_char(sc);          /* \W */
          case '\x09': return is_whitespace_char(sc);     /* \s */
          case '\x0A': return !is_whitespace_char(sc);    /* \S */
          case '\x0D': return (sc != '\n' && sc != '\r'); /* .  */
          default:                                        /* ordinary literal */
              return case_sensitive ? (pc == sc)
                  : (tolower((unsigned char)pc) == tolower((unsigned char)sc));
      }
  - DEPENDENCIES: get_escaped_char (P1.M1.T2.S2, defined early); is_digit_char /
    is_word_char / is_whitespace_char (Task 1); tolower (<ctype.h>, included by P1.M2).
  - DO NOT: fold the placeholder byte; drop the (unsigned char) casts; forget the
    dot's '\r' exclusion; add a default that returns true.

Task 3: REPLACE the STUB match_with_anchors() at the EARLY site (T2.S2 part 1)
  - PLACE: at the existing match_with_anchors site (right after parse_pattern,
    before the public pattern_match), REPLACING the stub body. Keep the signature
    and any doc banner.
  - SIGNATURE: static bool match_with_anchors(const parsed_pattern_t *parsed,
                                              const char *str, bool case_sensitive)
  - BODY:
      if (!parsed || !str) return false;
      const char *core = parsed->core_pattern;
      if (parsed->start_anchored && parsed->end_anchored)          /* ^...$ exact */
          return match_reaches_end_with_start(core, str, str, case_sensitive);
      if (parsed->start_anchored)                                  /* ^ prefix */
          return match_string_with_start(core, str, str, case_sensitive);
      if (parsed->end_anchored) {                                  /* ...$ suffix */
          size_t n = strlen(str);
          for (size_t i = 0; i <= n; i++)
              if (match_reaches_end_with_start(core, str+i, str, case_sensitive)) return true;
          return false;
      }
      {                                                           /* substring default */
          if (strlen(core) == 0) return strlen(str) == 0;         /* empty-core guard */
          size_t n = strlen(str);
          for (size_t i = 0; i <= n; i++)
              if (match_string_with_start(core, str+i, str, case_sensitive)) return true;
          return false;
      }
  - DEPENDENCIES: match_string_with_start / match_reaches_end_with_start
    (Task 4, forward-declared by P1.M2); strlen (<string.h>).
  - DO NOT: pass str+i as string_start; drop the empty-core guard; use `< n` for
    the loop bound (must be `<= n`); reorder the anchor checks.

Task 4: ADD the two wrappers after nfa_match() (T2.S2 part 2)
  - PLACE: after nfa_match's closing brace, at the end of the file.
  - SIGNATURES:
      static bool match_string_with_start(const char *pattern, const char *str,
              const char *string_start, bool case_sensitive)
      static bool match_reaches_end_with_start(const char *pattern, const char *str,
              const char *string_start, bool case_sensitive)
  - BODY (one line each):
      return nfa_match(pattern, str, string_start, case_sensitive, false);  /* reach-any */
      return nfa_match(pattern, str, string_start, case_sensitive, true);   /* full     */
  - DEPENDENCIES: nfa_match (P1.M2.T2.S2). P1.M2 already forward-declared both
    wrappers, so no new declaration is needed.
  - DO NOT: add logic; the wrappers are intentionally trivial forwarders.

Task 5: VERIFY the build + behavior + acceptance gates (run the Validation Loop).
```

### Implementation Patterns & Key Details

```c
// PATTERN: leaf-first ordering for static functions. Define the 3 predicates,
//   then is_word_boundary (calls is_word_char), before nfa_addstate. Define
//   pattern_char_matches (calls predicates + get_escaped_char) before nfa_match.
//   Define the wrappers (call nfa_match) after nfa_match. C requires a static
//   function's definition (or a forward declaration) before any call site.

// PATTERN: forward declarations already cover the cross-block calls. P1.M1/M2
//   declared match_with_anchors, nfa_match, and both wrappers near the top. So
//   match_with_anchors (early site) can name the wrappers (end of file) and
//   pattern_char_matches (mid-file) can name get_escaped_char (early) without
//   adding any new declarations.

// PATTERN: absolute-position forwarding (PRD §13 #10). match_with_anchors always
//   passes the ORIGINAL `str` as string_start; only the match position advances
//   (str+i). The wrappers forward string_start verbatim. is_word_boundary then
//   sees absolute offsets regardless of where the substring match started.

// PATTERN: case-fold via tolower with (unsigned char) cast. The escaped-literal
//   and default-literal branches both fold ONLY when !case_sensitive, and BOTH
//   operands are cast: tolower((unsigned char)a) == tolower((unsigned char)b).
//   Class assertions (\d \w \s and their negations) are case-insensitive by
//   definition and never fold.

// ANTI-PATTERN: do NOT move the empty-string \b/\B check out of nfa_addstate.
//   It is an OP_ASSERT-branch concern (P1.M2). is_word_boundary is a pure
//   position test. Splitting the responsibility keeps "empty original string
//   matches neither \b nor \B" exactly as the test suite encodes it.

// ANTI-PATTERN: do NOT omit the empty-core substring guard. Without it, "" matches
//   any string (the empty compiled NFA reaches OP_MATCH immediately at offset 0).

// ANTI-PATTERN: do NOT pass str+i as string_start. \b/\B would then compute
//   positions relative to the substring, not the original string, breaking every
//   boundary test that relies on absolute position.

// ANTI-PATTERN: do NOT use isspace()/isdigit()/isalnum() from <ctype.h> for the
//   classifiers. They are locale-dependent and need (unsigned char) casts; the
//   explicit range tests are exact, locale-independent, and match the PRD sets.

// ANTI-PATTERN: do NOT add #includes (all needed headers are present); do NOT add
//   a second forward declaration of functions P1.M1/M2 already declared; do NOT
//   touch the P1.M2 engine or the P1.M1 pipeline.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - 4 edits to pattern_match.c only (REPLACE 2 stubs, INSERT 2 blocks). APPEND/
    REPLACE; do not move/reorder anything else.

CONSUMERS (upstream callers, NOT this milestone):
  - match_with_anchors  <- pattern_match() [P1.M1.T2.S2 public API]
  - is_word_boundary    <- nfa_addstate() OP_ASSERT branch [P1.M2.T2.S1]
  - pattern_char_matches<- nfa_match() OP_CHAR step [P1.M2.T2.S2]
  - wrappers            <- match_with_anchors [Task 3] -> nfa_match [P1.M2]

CROSS-MILESTONE CONTRACTS (signatures fixed upstream; do not change):
  - nfa_match(const char *pattern, const char *str, const char *string_start,
              bool case_sensitive, bool full_match) -> bool   [P1.M2.T2.S2]
  - get_escaped_char(char placeholder) -> char                [P1.M1.T2.S2]
  - parsed_pattern_t { core_pattern, start_anchored, end_anchored, processed_pattern }
                                                              [P1.M1.T2.S2]

BUILD:
  - No build-system change. Plain gcc (run_all_tests.sh style). Validate by
    compiling pattern_match.c (Level 1) + a probe harness (Level 2) + the
    committed suites (Level 3) + perf (Level 3B).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; pure algorithm. No runtime effect beyond making
    pattern_match() return correct booleans.)
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. P1.M3 functions are `static`, so
> they are reached via the public `pattern_match()` (the committed suites) and a
> small probe. All commands were VERIFIED against the live code during research.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o
# Expected: exit 0, ZERO warnings (P1.M2's transient "defined but not used"
# warnings self-resolve once these functions land and gain callers).
# FAIL if exit != 0 OR any warning remains.

# 1b. Syntax-only (silent).
gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c
# Expected: exit 0, NO output.

# 1c. The P1.M2 STUB is_word_boundary is GONE (replaced by the real one).
grep -nE 'static bool is_word_boundary' pattern_match.c
# Expected: exactly ONE match, whose body contains 'strlen' and 'is_word_char'
# (NOT a bare 'return false;' stub).

# 1d. The P1.M2 STUB match_with_anchors is GONE (replaced by the real dispatcher).
grep -nE 'static bool match_with_anchors' pattern_match.c
# Expected: the DEFINITION (not just the forward decl) whose body references
# match_string_with_start / match_reaches_end_with_start.

# 1e. All six P1.M3 functions present with exact signatures.
grep -nE 'static bool is_digit_char\(char c\)' pattern_match.c
grep -nE 'static bool is_word_char\(char c\)' pattern_match.c
grep -nE 'static bool is_whitespace_char\(char c\)' pattern_match.c
grep -nE 'static bool is_word_boundary\(const char \*str, size_t pos\)' pattern_match.c
grep -nE 'static bool pattern_char_matches\(char pc, char sc, bool case_sensitive\)' pattern_match.c
grep -nE 'static bool match_string_with_start\(' pattern_match.c
grep -nE 'static bool match_reaches_end_with_start\(' pattern_match.c
# Expected: each prints at least one line.

# 1f. tolower uses (unsigned char) casts in pattern_char_matches (UB avoidance).
awk '/static bool pattern_char_matches/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -qE 'tolower\(\(unsigned char\)' && echo "tolower cast (ok)" \
  || echo "FAIL: missing (unsigned char) cast in pattern_char_matches"

# 1g. Empty-core substring guard present (PRD §7.4 special case).
grep -nE 'strlen\(core.*\) == 0|strlen\(core_pattern\) == 0' pattern_match.c
# Expected: a match inside match_with_anchors' substring branch.

# 1h. No new #includes.
grep -nE '^#include' pattern_match.c   # <stdbool.h> <string.h> <stdlib.h> <ctype.h> only

rm -f /tmp/pm.o
```

### Level 2: Component Tests (THE PRIMARY BEHAVIORAL GATE)

This probe was verified against the live source-of-truth during research (all 23
cases pass). It exercises every P1.M3 responsibility through the public API.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/m3_probe.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  struct t{const char*p,*s;int cs,exp;const char*d;}T[]={
    /* pattern_char_matches: classes via metachars (is_*_char) */
    {"\\d","7",1,1,"\\d digit"},
    {"\\d","a",1,0,"\\d not digit"},
    {"\\D","a",1,1,"\\D non-digit"},
    {"\\w","_",1,1,"\\w underscore"},      /* is_word_char includes _ */
    {"\\w","-",1,0,"\\w not word"},
    {"\\W","-",1,1,"\\W non-word"},
    {"\\s"," ",1,1,"\\s space"},
    {"\\s","\t",1,1,"\\s tab"},
    {"\\s","\n",1,1,"\\s newline"},        /* whitespace incl \n */
    {"\\S","x",1,1,"\\S non-ws"},
    /* dot excludes newline; glob includes it (§13 #8) */
    {"a.b","a\nb",1,0,"dot excludes \\n"},
    {"a.b","a\rb",1,0,"dot excludes \\r"},
    {"*","a\nb",1,1,"glob includes \\n"},
    /* escaped literals (get_escaped_char + case-fold) */
    {"a\\+b","a+b",1,1,"\\+ literal plus"},
    {"v\\.code","v.code",1,1,"\\. literal dot"},
    /* word boundary ABSOLUTE position (§13 #10) via is_word_boundary */
    {"\\bword\\b","a word here",0,1,"\\b substring abspos"},
    {"^word\\b","word here",1,1,"\\b at start"},
    /* anchor strategies (§7.4) */
    {"^hello$","hello",1,1,"exact"},
    {"^hello$","hello world",1,0,"exact rejects non-exact"},
    {"^hello","hello world",0,1,"prefix"},
    {"world$","hello world",0,1,"suffix"},
    {"abc","ABC",0,1,"substring case-insensitive"},
    {"abc","ABC",1,0,"case-sensitive no-match"},
    {"^$","",1,1,"empty exact"},
    /* empty-core substring special case */
    {"","x",1,0,"empty core vs non-empty str -> false"},
    {"","",1,1,"empty core vs empty str -> true"},
  };
  int f=0;
  for(unsigned i=0;i<sizeof(T)/sizeof(T[0]);i++){
    int r=pattern_match(T[i].p,T[i].s,T[i].cs);
    printf("%s %-3d  %s\n",r==T[i].exp?"ok  ":"FAIL",r,T[i].d);
    if(r!=T[i].exp)f++;
  }
  printf("\n%d failures\n",f);
  return f?1:0;
}
EOF
gcc -Wall -std=c99 -I. /tmp/m3_probe.c pattern_match.c -o /tmp/m3_probe && /tmp/m3_probe
# Expected: a line of "ok" per case, then "0 failures", exit 0.
# CRITICAL gates: \w/_, dot excludes \n AND \r, \b abspos, empty-core guard.
rm -f /tmp/m3_probe.c /tmp/m3_probe
```

### Level 3: Integration & Acceptance

```bash
cd /home/dustin/projects/qmk-notifier

# 3A. Full suite via the runner (rebuilds all 9 binaries + runs them).
./run_all_tests.sh 2>&1 | tail -20
# Expected: "Total tests passed: 2018" of 2019. The ONE failure is the G3
# "Anchored huge pattern exact match" in test_memory_stress.c — a ~40 KB pattern
# vs fixed-stack-pool conflict (P1.M4/P3 scope, documented in P1M2/PRP.md §G3).
# Confirm it is the ONLY failure:
./run_all_tests.sh 2>&1 | grep -E "FAIL:|Tests failed: [1-9]" | sort -u
# Expected: exactly "FAIL: Anchored huge pattern exact match" + the two summary
# lines citing 1 failure. Anything else is a P1.M3 regression — investigate.

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
timeout 5 /tmp/nfa_stress   # MUST print result=0 in < 50 ms (live code: ~1.8 ms)
rm -f /tmp/nfa_stress.c /tmp/nfa_stress

# 3C. AddressSanitizer — no crash / no memory error on any input (PRD §1.3/§12).
gcc -O1 -g -fsanitize=address,undefined -w pattern_match.c test_memory_stress.c -I. -o /tmp/pm_asan
/tmp/pm_asan >/dev/null && echo "ASan/UBSan: clean (no crash on oversized/garbage input)"
rm -f /tmp/pm_asan

# 3D. PRD §11.2C realistic patterns (note: the `^\w+@\w+$` vs `user_host` case is
# a PRD doc error — the string has no '@', so the correct result is 0, not 1;
# do NOT alter P1.M3 to force a 1. The other five must print 1.)
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
# Expected: five 1s (the 6th PRD case, ^\w+@\w+$ vs user_host, is a doc error —
# correctly 0; omitted here to avoid a false red.)
rm -f /tmp/nfa_real.c /tmp/nfa_real
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract (Mode A): each P1.M3 function carries an explanatory banner
# referencing its PRD section (§7.4 / §7.6 / §7.7).
grep -qE '7\.6|Character classification|word.boundary' pattern_match.c && echo "classifier banner (ok)"
grep -qE '7\.7|single.char predicate|pattern_char_matches' pattern_match.c && echo "predicate banner (ok)"
grep -qE '7\.4|anchor strategy|match_with_anchors' pattern_match.c && echo "anchor banner (ok)"

# Backward-compat micro-benchmark (sub-microsecond per call for realistic patterns).
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

# Reference-keymap parity smoke: a few patterns from PRD §10.2 must behave.
cat > /tmp/keymap_smoke.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  /* class-only bare pattern matches the class half (notifier.c splits on GS,
     but here we test the matcher directly against a full class\x1Dtitle string) */
  printf("%d\n", pattern_match("neovide","neovide",0));                 /* 1 */
  printf("%d\n", pattern_match("*chrome*","Google Chrome",0));          /* 1 */
  printf("%d\n", pattern_match("cs2","CS2",0));                         /* 1 (ci) */
  printf("%d\n", pattern_match("Counter-Strike 2","counter-strike 2",0));/* 1 (ci) */
  return 0;
}
EOF
gcc -w /tmp/keymap_smoke.c pattern_match.c -I. -o /tmp/keymap_smoke && /tmp/keymap_smoke
# Expected: four 1s.
rm -f /tmp/keymap_smoke.c /tmp/keymap_smoke
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0, ZERO warnings.
- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → silent.
- [ ] Level 1: both P1.M2 stubs GONE (grep 1c/1d); all six functions present (1e).
- [ ] Level 1: `tolower((unsigned char)...)` casts present in `pattern_char_matches` (1f).
- [ ] Level 1: empty-core substring guard present (1g); no new `#include`s (1h).
- [ ] Level 2: `/tmp/m3_probe` prints **0 failures** (incl. `\w/_`, dot excludes
      `\n` AND `\r`, `\b` absolute position, empty-core guard).
- [ ] Level 3A: `run_all_tests.sh` → **2018/2019** pass; the only failure is the
      G3 `Anchored huge pattern exact match` (unchanged from baseline).
- [ ] Level 3B: pathological stress prints `result=0` in < 50 ms.
- [ ] Level 3C: ASan/UBSan clean on the memory-stress corpus.
- [ ] Level 3D: §11.2C prints five `1`s (the `@\w+$` case is a PRD doc error).

### Feature Validation

- [ ] `is_digit_char`/`is_word_char`/`is_whitespace_char` match the PRD sets
      (`\w` includes `_`; whitespace is the 6 chars; high-bit bytes → false).
- [ ] `is_word_boundary` implements the exact PRD §7.6 position rule (NULL→false,
      edges, interior XOR) with NO empty-string special case (that stays in `nfa_addstate`).
- [ ] `pattern_char_matches` decodes escaped literals first, folds only the decoded
      char + input char with `(unsigned char)` casts; dot excludes `\n` AND `\r`.
- [ ] `match_with_anchors` dispatches the 4 anchor strategies (exact/prefix/suffix/
      substring); substring branch has the empty-core guard; loops are `i <= strlen`.
- [ ] Both wrappers forward `string_start` verbatim (absolute position preserved).

### Code Quality Validation

- [ ] Matches the live source-of-truth branch-for-branch (PRD §17: code wins).
- [ ] APPEND/REPLACE only — no modification to P1.M1 content, the P1.M2 engine,
      `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
      `tasks.json`, `prd_snapshot.md`, `.gitignore`.
- [ ] Placement correct: classifiers before `nfa_addstate`; `pattern_char_matches`
      before `nfa_match`; wrappers after `nfa_match`; `match_with_anchors` at the
      early site (stub replaced).
- [ ] No new `#include`s; no new forward declarations (P1.M1/M2 already added them);
      no `__attribute__((unused))` suppression; no use of `<ctype.h>` isspace/isdigit.

### Documentation & Deployment

- [ ] Each P1.M3 function carries a Mode-A banner referencing its PRD section
      (§7.4 / §7.6 / §7.7) and the non-obvious detail it encodes.
- [ ] `is_word_boundary` comments note the empty-string semantics live upstream.
- [ ] `match_with_anchors` comments note the empty-core special case + abspos rule.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't move the empty-string `\b`/`\B` check into `is_word_boundary`. It lives
  in `nfa_addstate`'s OP_ASSERT branch (`*string_start != '\0' &&`). `is_word_boundary`
  is a pure position test.
- ❌ Don't omit the empty-core substring guard. An empty `core_pattern` would match
  any string at offset 0 via the empty-NFA-accepts-immediately path.
- ❌ Don't pass `str+i` as `string_start`. `\b`/`\B` need the ORIGINAL string base
  for absolute positions (PRD §13 #10).
- ❌ Don't call `tolower` without `(unsigned char)` casts — signed-char sign
  extension is UB. Cast BOTH operands; decode escaped literals before folding.
- ❌ Don't conflate dot and glob: dot `0x0D` excludes `\n` AND `\r`; glob `*`
  (OP_ANY) includes them (PRD §13 #8).
- ❌ Don't forget `_` in `is_word_char` (`[A-Za-z0-9_]`) — `\w`/`\W`/`\b`/`\B`
  all depend on it.
- ❌ Don't use `<ctype.h>`'s `isspace`/`isdigit`/`isalnum` for the classifiers —
  locale-dependent + need casts; the explicit ranges are exact and match the PRD.
- ❌ Don't use `< n` for the suffix/substring loop bound — it must be `<= strlen(str)`
  so a trailing empty-tail core can match the end.
- ❌ Don't add a second forward declaration of `match_with_anchors`, `nfa_match`, or
  the wrappers — P1.M1/M2 already declared them near `get_escaped_char`.
- ❌ Don't add `#include`s; don't touch the P1.M2 engine (it compiles/links as-is);
  don't "fix" the §11.2C `^\w+@\w+$` vs `user_host` case (it's a PRD doc error —
  the matcher correctly returns 0).

---

## Confidence Score

**9/10** — One-pass success is highly likely. The live code is the verified,
2018/2019-passing source of truth (PRD §17); every P1.M3 behavior was probed and
matches PRD §7.4/§7.6/§7.7/§15; the four edit sites, the ordering constraints, the
two stubs to replace, and the forward declarations already in place are all
documented. The only residual risk is an implementer misplacing the empty-string
`\b`/`\B` responsibility (into `is_word_boundary` instead of leaving it in
`nfa_addstate`) or dropping the empty-core guard — both are encoded as hard
requirements and gated by Level 1/2 checks.
