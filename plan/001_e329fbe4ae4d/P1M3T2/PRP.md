# PRP — P1.M3.T2: Single-Char Predicate & Anchor Strategy

> **Scope.** This PRP covers **two** steps that together wire the public
> `pattern_match()` to the NFA engine: **S1** `pattern_char_matches` (the
> single-byte match predicate called by `nfa_match`'s `OP_CHAR` step) and **S2**
> `match_with_anchors` + its two wrappers `match_string_with_start` /
> `match_reaches_end_with_start` (the anchor-strategy layer that picks the NFA
> mode and loops offsets). It does **not** implement the classifier predicates
> (`is_digit_char`/`is_word_char`/`is_whitespace_char`/`is_word_boundary` — that
> is **P1.M3.T1**, running in parallel and treated here as a hard contract), nor
> the NFA engine (`nfa_compile`/`nfa_addstate`/`nfa_match` — **P1.M2**, complete),
> nor the P1.M1 pipeline (`process_escapes`/`parse_pattern`/`parsed_pattern_t` —
> complete). Do only what this document specifies.

## Goal

**Feature Goal**: Replace the two pending STUBS that block the public
`pattern_match()` from actually matching anything — the P1.M1.T2 STUB
`match_with_anchors` (returns `false`) and the P1.M2 STUB
`pattern_char_matches` — with their real bodies, and add the two thin NFA
wrapper definitions (`match_string_with_start` / `match_reaches_end_with_start`)
that the P1.M2 engine forward-declared but did not define. After this, the full
public pipeline `pattern_match → parse_pattern → match_with_anchors → nfa_match →
pattern_char_matches` is live, and every construct in PRD §7/§15 (wildcards,
anchors, escapes, `\d\D\w\W\s\S`, `\b\B`, dot, `X+`) classifies and matches
correctly with **linear-time** guarantees (no catastrophic backtracking).

**Deliverable**: Three edits to `pattern_match.c` (REPLACE two STUB bodies,
APPEND two wrapper definitions). **No new files.** Specifically:

1. **REPLACE** the STUB `pattern_char_matches` body (PRD §7.7, site ≈ line 542,
   immediately after `nfa_addstate` and before `nfa_has_match`/`nfa_match`) with
   the real byte→predicate switch.
2. **REPLACE** the STUB `match_with_anchors` body (PRD §7.4, site ≈ line 233, in
   the P1.M1.T2 region, immediately before the public `pattern_match()`) with the
   real four-branch anchor strategy.
3. **APPEND** the two wrapper definitions (`match_string_with_start`,
   `match_reaches_end_with_start`) at the end of the file (site ≈ lines 621-625,
   after `nfa_match`), as thin forwarders to `nfa_match`.

Do **not** touch `pattern_match.h`, the P1.M1 pipeline, the P1.M2 engine, the
classifier functions (T1, parallel), `test_*.c`, `notifier.*`, `rules.mk`,
`PRD.md`, `tasks.json`, or `prd_snapshot.md`.

**Success Definition**:
- `pattern_char_matches`, `match_with_anchors`, `match_string_with_start`,
  `match_reaches_end_with_start` are all present, `static`, with the exact
  signatures in §"What".
- Both P1.M2/P1.M1.T2 STUBS are **gone** (grep finds the real bodies, not bare
  `return false;`).
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0, ZERO warnings**
  (every T2 function now has a live caller — the transient `defined-but-not-used`
  warnings that T1 tolerated are fully resolved here).
- **`./run_all_tests.sh` reports 0 failures across all 9 suites (~2 019
  assertions).** This is the primary gate — unlike T1, T2 wires the public API,
  so the full committed suite is now an authoritative behavioral gate.
- PRD §11.2B pathological stress `a+a+a+a+a+a+a+a+a+a+b` vs 199×`a` → `result=0`
  in **< 50 ms** (linear time confirmed).
- A direct `#include "pattern_match.c"` probe for `pattern_char_matches` (S1
  isolation) prints **0 failures**.

## User Persona (if applicable)

**Target User**: Other functions inside `pattern_match.c`, reached transitively.
No external code, no keymap, no test ever names a T2 function directly (all are
`static`, and the host test suites link `pattern_match.c` as a separate
translation unit — they cannot see `static` symbols).

**Use Case / Call graph at runtime (after T2 + T1 land)**:
```
pattern_match() [P1.M1.T2.S2, public]
 └─► match_with_anchors(&parsed, str, cs)  ◄── THIS TASK S2
       │  (picks strategy from parsed->start_anchored / end_anchored; loops offsets)
       ├─► match_string_with_start(core, str(+i), str, cs)      ◄── THIS TASK S2  (full_match=false)
       │     └─► nfa_match(core, str, string_start, cs, false)  [P1.M2.T2.S2]
       └─► match_reaches_end_with_start(core, str(+i), str, cs) ◄── THIS TASK S2  (full_match=true)
             └─► nfa_match(core, str, string_start, cs, true)   [P1.M2.T2.S2]
                   │ per-char step on OP_CHAR:
                   └─► pattern_char_matches(s->arg, c, cs)      ◄── THIS TASK S1
                          ├─► get_escaped_char(pc)   [P1.M1.T2.S2]  (0x01-0x04 escaped literals)
                          ├─► is_digit_char(sc)      [P1.M3.T1.S1]  (\d/\D)
                          ├─► is_word_char(sc)       [P1.M3.T1.S1]  (\w/\W)
                          ├─► is_whitespace_char(sc) [P1.M3.T1.S1]  (\s/\S)
                          └─► (sc != '\n' && sc != '\r')            (. dot)
```

**Pain Points Addressed**: While the two STUBS stand, the public `pattern_match()`
returns `false` for **every** input (match_with_anchors is `return false;`), so
the entire matcher is inert — no committed suite can pass, and the keyboard
module (P2, which calls `match_pattern` → `pattern_match`) matches nothing. T2 is
the step that makes the whole engine observable through the public API.

## Why

- **T2 is the connective tissue that makes the NFA reachable.** P1.M2 built a
  complete Thompson-NFA engine but left `pattern_char_matches` (its `OP_CHAR`
  leaf test) and `match_with_anchors` (the entry point the public API calls) as
  STUBS, because those belong to P1.M3. P1.M2 forward-declared the wrappers so
  the stub site could be filled in place without moving code. T2 fills those three
  bodies and adds the two wrapper definitions. Nothing else changes.
- **`pattern_char_matches` is the one place byte-semantics are decided.** Every
  `\d \D \w \W \s \S .` and escaped-literal/ordinary-literal classification flows
  through this single switch (PRD §7.7). Getting it right once makes every
  downstream NFA step correct; the table is small and exhaustive.
- **`match_with_anchors` encodes the four matching strategies as anchor flags.**
  PRD §7.4 maps `^`/`$` presence → exact / prefix / suffix / substring, choosing
  `full_match` (consume-whole-string vs reach-any) and whether to loop start
  offsets. The backward-compatible default (no anchors ⇒ substring) lives here —
  this is why existing glob rules keep matching identically (PRD §1.3, §12).
- **Subtle correctness anchors.** Four non-obvious details are encoded as hard
  requirements below: (a) `string_start` is ALWAYS the **original** string base,
  forwarded verbatim to every call — never the per-offset `str+i` (PRD §13 #10;
  `\b`/`\B` absolute position depends on it). (b) The substring branch's
  empty-core special case (`strlen(core)==0 ⇒ match only empty string`) MUST run
  before the offset loop, else an empty pattern spuriously matches everything
  (PRD §7.4). (c) `tolower` takes an `unsigned char` value — BOTH args to every
  `tolower(...)` call must be cast `(unsigned char)` or a high-bit byte is UB.
  (d) The dot (`0x0D`) excludes `\n`/`\r`; the glob `*` does NOT — do not
  conflate (PRD §13 #8).
- **Cohesion across the plan.** P1.M1 (pipeline) + P1.M2 (NFA engine) are
  complete; P1.M3.T1 (classifiers, parallel) supplies the predicates S1 calls by
  name; this task wires the strategy + leaf predicate; P1.M4 (test suite,
  already complete) validates the whole. T2's signatures are fixed by PRD §7.4/§7.7
  and the call sites already exist (forward declarations from P1.M2; the public
  `pattern_match` already calls `match_with_anchors`), so it lands with no change
  to any other file.

## What

**Three edits to `pattern_match.c`.** The exact bodies are the live source of
truth (PRD §17); reproduce them branch-for-branch.

### S1 — `pattern_char_matches` (REPLACE the P1.M2 STUB; site ≈ line 542)

```c
/* ===== P1.M3.T2.S1: pattern_char_matches — single-byte match predicate =====
 * Test whether a processed-pattern byte `pc` matches an input char `sc`. The
 * processed byte encodes either an escaped literal (0x01-0x04), a character
 * class (0x05-0x0A), or the dot (0x0D); any other byte is an ordinary literal.
 * Matching is case-folded via tolower() unless `case_sensitive` is set (PRD §7.7).
 * Escaped-literal placeholders are decoded via get_escaped_char() FIRST and then
 * folded — never fold the placeholder byte itself. tolower() takes an unsigned
 * char value, so args are cast to (unsigned char) to avoid sign-extension UB. */
static bool pattern_char_matches(char pc, char sc, bool case_sensitive) {
    if (pc >= '\x01' && pc <= '\x04') {                 /* escaped literal */
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
        case '\x0D': return (sc != '\n' && sc != '\r'); /* .  (dot excludes newline) */
        default:                                        /* ordinary literal */
            return case_sensitive ? (pc == sc)
                  : (tolower((unsigned char)pc) == tolower((unsigned char)sc));
    }
}
```

### S2a — `match_with_anchors` (REPLACE the P1.M1.T2 STUB; site ≈ line 233)

```c
/* ===== P1.M3.T2.S2: match_with_anchors — the anchor strategy (PRD §7.4) =====
 * Replaces the temporary STUB. Picks the NFA mode (full-match vs reach-any) and
 * whether to loop over start offsets based on the parsed anchor flags:
 *   ^...$ exact -> one full match from offset 0
 *   ^...       prefix -> one reach-any match from offset 0
 *   ...$       suffix -> loop offsets, full match from each
 *   ...        substring -> loop offsets, reach-any from each (empty core only
 *               matches the empty string). */
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive) {
    if (!parsed || !str) return false;
    const char *core_pattern = parsed->core_pattern;

    if (parsed->start_anchored && parsed->end_anchored) {        /* ^...$ exact */
        return match_reaches_end_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->start_anchored) {                         /* ^ prefix */
        return match_string_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->end_anchored) {                           /* $ suffix */
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++)
            if (match_reaches_end_with_start(core_pattern, str + i, str, case_sensitive))
                return true;
        return false;
    } else {                                                     /* substring (default) */
        if (strlen(core_pattern) == 0) return strlen(str) == 0;  /* empty core -> only empty string */
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++)
            if (match_string_with_start(core_pattern, str + i, str, case_sensitive))
                return true;
        return false;
    }
}
```

### S2b — the two wrappers (APPEND at end of file, after `nfa_match`; site ≈ lines 621-625)

```c
/* ===== P1.M3.T2.S2: anchor-strategy wrappers (thin forwarders to nfa_match) =====
 * match_string_with_start     -> reach-any (substring/prefix; full_match=false).
 * match_reaches_end_with_start -> consume-whole-remaining (suffix/exact; full_match=true).
 * Both forward the ORIGINAL string_start so \b/\B compute absolute positions. */
static bool match_string_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, false);
}
static bool match_reaches_end_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, true);
}
```

### Success Criteria

- [ ] `pattern_char_matches` is present, `static`, exact signature
      `(char pc, char sc, bool case_sensitive)`, body containing the `get_escaped_char`
      escaped-literal branch + the 6 class cases + the dot case + the default
      literal case (NOT a bare `return false;` stub).
- [ ] `match_with_anchors` is present, `static`, exact signature
      `(const parsed_pattern_t *parsed, const char *str, bool case_sensitive)`,
      body containing all four anchor branches (`start_anchored && end_anchored`,
      `start_anchored`, `end_anchored`, else) with the substring empty-core guard.
- [ ] `match_string_with_start` and `match_reaches_end_with_start` are defined
      (not just forward-declared), `static`, forwarding to `nfa_match` with
      `full_match` `false`/`true` respectively.
- [ ] The P1.M1.T2/P1.M2 STUBS for `match_with_anchors`/`pattern_char_matches`
      are **gone** (grep Level 1c).
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0, ZERO warnings**.
- [ ] `./run_all_tests.sh` → **0 failures** across all 9 suites (~2 019 assertions).
- [ ] PRD §11.2B: pathological stress → `result=0` in **< 50 ms**.
- [ ] No edit to any file other than `pattern_match.c`; no new `#include`; no new
      forward declarations (P1.M2 already provided them); no `__attribute__((unused))`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write is the live source of truth
(`pattern_match.c` is implemented and passing 2 019/2 019 assertions at HEAD; PRD
§17: "the code + the passing tests win"). The two STUB sites T2 replaces, the
exact bodies (verified at lines 233, 542, 621-625), the four anchor-strategy
branches with their `full_match`/loop mapping, the empty-core substring guard,
the `string_start` absolute-position rule, the `(unsigned char)`-cast-on-tolower
requirement, the dot-vs-glob newline distinction, the P1.M2 forward-declaration
block T2 relies on (lines 95-110), the stale §11.2C comment (do NOT "fix" the
correct `0`), and the full-suite validation approach were **all verified against
the live code and passing tests during research**. The cross-milestone contracts
(`parsed_pattern_t`, `get_escaped_char`, `nfa_match`, the three T1 classifiers)
are stated verbatim below so an implementer needs no access to P1.M1/P1.M2/T1.

### Documentation & References

```yaml
# MUST READ — authoritative spec for these three functions
- file: PRD.md
  section: "### 7.4 Matching strategy (match_with_anchors)"
  why: "The EXACT 4-branch table: ^+$ exact -> nfa_match(full=true) from offset 0;
        ^ only prefix -> nfa_match(full=false) from offset 0; $ only suffix -> loop
        offsets nfa_match(full=true); neither substring -> loop offsets
        nfa_match(full=false), with 'empty core matches only the empty string'."
  critical: "string_start is ALWAYS the original string base (forwarded to every
        nfa_match call), so \\b/\\B compute absolute positions (PRD §13 #10). The
        substring empty-core guard MUST precede the offset loop."

- file: PRD.md
  section: "### 7.7 The single-char predicate (pattern_char_matches)"
  why: "The EXACT byte->predicate dispatch: 0x01-0x04 escaped literal (decoded via
        get_escaped_char, case-folded); 0x05/0x06 -> is_digit_char / negation;
        0x07/0x08 -> is_word_char / negation; 0x09/0x0A -> is_whitespace_char /
        negation; 0x0D -> sc != '\\n' && sc != '\\r'; default ordinary literal,
        case-folded."
  critical: "tolower args MUST be cast (unsigned char) — a negative char is UB.
        Decode escaped-literal placeholders BEFORE folding, never fold the
        placeholder byte. The dot excludes newline; do NOT conflate with glob '*'."

- file: PRD.md
  section: "### 7.1 The processed-pattern byte contract (what the NFA consumes)"
  why: "The placeholder table pattern_char_matches switches on: 0x01-0x04 escaped
        literals, 0x05-0x0A classes, 0x0B/0x0C \\b/\\B (handled by nfa_addstate, NOT
        here), 0x0D dot, 0x0E + quantifier (handled by nfa_compile, NOT here),
        0x2A glob '*' (handled by nfa_compile as OP_ANY, NOT here)."
  critical: "pattern_char_matches only ever sees OP_CHAR bytes (0x01-0x0D and
        ordinary literals). The 0x0B/0x0C/0x0E/0x2A bytes are consumed by other
        engine stages and NEVER reach this function — do not add cases for them."

- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve" (#8 dot vs glob; #10 abs pos)
  why: "#8: 'Glob * matches any char including \\n/\\r; dot . matches any char
        except \\n/\\r.' This is the pattern_char_matches dot case. #10: 'Absolute
        position for \\b/\\B must be computed from string_start' — this is the
        match_with_anchors string_start-forwarding rule."
  critical: "Both are single-line invariants that are easy to invert by mistake."

- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "Verified truth table for every construct: '*slack*' (cs=0 -> true, case-
        fold substring), 'a.b' vs 'a\\nb' (false, dot), 'a+b' vs 'aaab' (true),
        '\\bword\\b' vs 'a word here' (true), '^hello$' exact, 'world$' suffix,
        'sear*term' substring. These are the behavioral expectations the committed
        test_pattern_match.c encodes."

# The parallel-milestone CONTRACT — what EXISTS when T2 starts
- file: plan/001_e329fbe4ae4d/P1M3T1/PRP.md
  section: "## What" (the four classifier functions)
  why: "Defines the three leaf predicates pattern_char_matches calls by name:
        static bool is_digit_char(char c); static bool is_word_char(char c)
        ([A-Za-z0-9_], underscore INCLUDED); static bool is_whitespace_char(char c)
        (' \\t \\n \\r \\f \\v', exactly 6). T2's \\d/\\D/\\w/\\W/\\s/\\S cases
        call these directly. is_word_boundary (also defined by T1) is NOT called
        by T2 (it is called by nfa_addstate's OP_ASSERT branch, P1.M2)."
  critical: "Assume these three predicates exist with the exact T1 semantics. T2
        does not redefine them. If any is missing, that is a T1 defect — do not
        paper over it by inlining; surface it."

# The P1.M2 CONTRACT — the engine T2 calls into + the forward declarations
- file: plan/001_e329fbe4ae4d/P1M2/PRP.md
  section: "## What D" (nfa_match) + "## Known Gotchas" (GOTCHA-6 forward decls)
  why: "nfa_match signature is FIXED: static bool nfa_match(const char *pattern,
        const char *str, const char *string_start, bool case_sensitive, bool
        full_match). full_match=false => MATCH reachable at any point (prefix/
        substring); full_match=true => MATCH reachable only after consuming the
        whole string (suffix/exact). GOTCHA-6: P1.M2 ALREADY forward-declared
        match_with_anchors, nfa_match, match_string_with_start, and
        match_reaches_end_with_start at lines 95-110 so the match_with_anchors
        body (line 233, before the engine) can call the wrappers+nfa_match
        (defined after the engine at 578-625)."
  critical: "Do NOT add a second forward-declaration block; do NOT move
        match_with_anchors below the engine. The existing forward declarations
        are the contract. T2 only fills bodies + appends the two wrapper defs."

# The P1.M1 CONTRACT — parsed_pattern_t + get_escaped_char
- file: PRD.md
  section: "### 7.3 Anchor detection (parse_pattern)" + pattern_match.c:~120-200
  why: "parsed_pattern_t { const char *core_pattern; bool start_anchored; bool
        end_anchored; char *processed_pattern; } is the struct match_with_anchors
        receives. core_pattern points at the process_escapes() output (the
        processed bytes the NFA consumes) OR the raw pattern on malloc-failure
        fallback. get_escaped_char(char placeholder) [P1.M1.T2.S2] reverses
        0x01-0x0D back to a readable char — pattern_char_matches calls it for the
        escaped-literal branch (0x01-0x04)."
  critical: "match_with_anchors reads parsed->core_pattern / start_anchored /
        end_anchored ONLY. Do not dereference processed_pattern (it may alias
        core_pattern or be NULL on the fallback path). NULL-guard parsed first."

# Live source of truth (PRD §17)
- file: pattern_match.c
  section: "===== P1.M3.T2.S2: match_with_anchors — the anchor strategy ====="
        (lines ~226-255), "===== P1.M3.T2.S1: pattern_char_matches ====="
        (lines ~534-563), "===== P1.M3.T2.S2: anchor-strategy wrappers ====="
        (lines ~614-627)
  why: "The implemented, clean functions. Reproduce them branch-for-branch."
  critical: "The STUBS (bare `return false;`) that T2 replaces sit at the
        match_with_anchors site (left by P1.M1.T2) and the pattern_char_matches
        site (left by P1.M2). The wrappers are forward-declared but NOT yet
        defined (P1.M2 left them for T2.S2)."

# Why a direct probe validates S1 in isolation (mirrors T1)
- file: test_char_classification.c
  section: "header comment (lines 1-15)"
  why: "Documents that the static functions cannot be tested across a translation-
        unit boundary; the committed suites exercise them INDIRECTLY via the
        public API. For S1 isolation, use a #include \"pattern_match.c\" probe."
  critical: "The full committed suite IS the gate for T2 (unlike T1), because T2
        wires the public API. The direct S1 probe is a supplement, not a
        substitute."

# External algorithm reference (reading only — no library)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast'. The
        anchor-strategy layer is the standard 'unanchored => try a match at each
        start position' loop on top of the NFA's anchored-from-given-start
        simulation (nfa_match)."
  critical: "Reference reading. No dependency to add. The engine + strategy are
        hand-written C; this is the cited justification for linear time."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # P1.M1.T2 + P1.M2 COMPLETE EXCEPT: match_with_anchors is a
                       #   STUB (return false) at ~line 233 [left by P1.M1.T2] and
                       #   pattern_char_matches is a STUB at ~line 542 [left by
                       #   P1.M2]. The two wrappers match_string_with_start /
                       #   match_reaches_end_with_start are FORWARD-DECLARED
                       #   (lines 95-110, by P1.M2) but NOT yet DEFINED. The P1.M2
                       #   forward-declaration block (GOTCHA-6) already lets
                       #   match_with_anchors call nfa_match + the wrappers.
                       #   is_digit_char/is_word_char/is_whitespace_char/
                       #   is_word_boundary are provided by P1.M3.T1 (parallel).
                       #   THIS task fills the 2 stub bodies + appends 2 wrappers.
notifier.h notifier.c  # P2 (COMPLETE). notifier.c #include "pattern_match.c".
rules.mk               # P2 — do not touch.
test_*.c               # P1.M4 — validate via the PUBLIC pattern_match() only.
run_all_tests.sh       # P1.M4 — gcc-based runner. THE primary T2 gate.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # THIS task edits THREE sites. After it:
                       #  (1) ~line 233: match_with_anchors — REAL 4-branch anchor
                       #      strategy (REPLACED the P1.M1.T2 stub).
                       #  (2) ~line 542: pattern_char_matches — REAL byte->predicate
                       #      switch (REPLACED the P1.M2 stub).
                       #  (3) ~lines 621-625: match_string_with_start +
                       #      match_reaches_end_with_start — DEFINED as thin
                       #      nfa_match forwarders (APPENDED; were forward-decl only).
                       # Nothing else changes. No new files.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — string_start is ALWAYS the original string base. match_with_anchors
//   forwards `str` (the original) as the string_start arg to EVERY nfa_match call
//   (via the wrappers), even in the suffix/substring loops where the per-iteration
//   input pointer is `str + i`. Passing `str + i` as string_start would rebase
//   abspos and BREAK \b/\B (PRD §13 #10). The live code passes `str` (not `str+i`)
//   in all four branches — reproduce exactly.

// CRITICAL — the substring empty-core guard MUST precede the offset loop:
//     if (strlen(core_pattern) == 0) return strlen(str) == 0;
//   Without it, match_string_with_start("", str+i, str, cs) returns true (empty
//   pattern = match-empty-prefix) at every offset, so "" would match EVERY string.
//   PRD §7.4: an empty core matches ONLY the empty string. The exact-anchored
//   (^+$) path does NOT need this guard (nfa_match handles empty via its start==
//   NULL defensive guard); only the unanchored substring branch does.

// CRITICAL — tolower() takes an unsigned char value. EVERY tolower(...) call in
//   pattern_char_matches must cast BOTH args: tolower((unsigned char)x). A high-bit
//   byte (>0x7F) is negative as signed char; passing it raw to tolower is UB
//   (PRD §7.7). This is the ONE place casts are needed in P1.M3 — the T1
//   classifiers use range tests and need none.

// CRITICAL — the dot (0x0D) excludes newline: return (sc != '\n' && sc != '\r').
//   The glob '*' (0x2A, compiled as OP_ANY) INCLUDES newline. Do NOT conflate
//   (PRD §13 #8). pattern_char_matches never sees 0x2A (nfa_compile turns it into
//   OP_ANY, handled by nfa_match's OP_ANY branch, not the OP_CHAR branch).

// CRITICAL — escaped-literal placeholders (0x01-0x04) are decoded via
//   get_escaped_char() FIRST, then case-folded. Never tolower() the placeholder
//   byte itself (it is a control byte, not a letter). get_escaped_char is P1.M1.T2.S2.

// GOTCHA — the P1.M2 forward-declaration block (lines 95-110) already declares
//   match_with_anchors, nfa_match, match_string_with_start, and
//   match_reaches_end_with_start. Do NOT re-declare any of them. T2 only: (a)
//   fill the match_with_anchors BODY at its existing site (line 233), (b) fill the
//   pattern_char_matches BODY at its existing site (line 542), (c) APPEND the two
//   wrapper DEFINITIONS at the end (after nfa_match). The wrappers MUST be defined
//   after nfa_match (they call it) — the forward decl makes the match_with_anchors
//   site compile, but the wrapper definitions themselves go at the bottom.

// GOTCHA — pattern_char_matches only ever receives OP_CHAR bytes: 0x01-0x0D
//   (escaped literals, classes, dot) and ordinary literals. The 0x0B/0x0C (\b/\B),
//   0x0E (+ quantifier), and 0x2A (glob '*') bytes are consumed by nfa_compile /
//   nfa_addstate / nfa_match's OP_ANY branch and NEVER reach this function. Do not
//   add switch cases for them (dead code); the `default` ordinary-literal arm is
//   correct for any unexpected byte.

// GOTCHA — EXPECTED clean compile (zero warnings) after T2. Unlike T1 (which
//   tolerated three transient -Wunused-function warnings until pattern_char_matches
//   landed), T2 gives every function a live caller: pattern_char_matches <- nfa_match
//   (P1.M2), match_with_anchors <- pattern_match (P1.M1.T2), wrappers <- match_with_
//   anchors (this task). If you see ANY -Wunused-function after T2, something is
//   wrong (a wrapper not appended, or a body not wired). Do NOT suppress with
//   __attribute__((unused)).

// GOTCHA — PRD §11.2C's comment is STALE for the case
//   pattern_match("^\\w+@\\w+$","user_host",1). The comment says /* 1 */ but the
//   correct result is 0: `user_host` contains no `@`, so `^\w+@\w+$` cannot match.
//   The live code prints 0 and that is RIGHT; the committed test_pattern_match.c
//   (376 cases, all pass) is authoritative. Do NOT "fix" the code to print 1.
//   (PRD §17 acknowledges stale figures in the doc.)

// GOTCHA — no new #includes. pattern_char_matches needs tolower (<ctype.h>, already
//   included by P1.M1). match_with_anchors needs strlen (<string.h>, already
//   included). Do not add anything.

// GOTCHA — the suffix/substring loops are `for (i = 0; i <= str_len; i++)`
//   (INCLUSIVE of str_len). This lets a trailing-$ or unanchored pattern match at
//   the final position (e.g. the empty-tail case). Verified by `^$` vs "" (true)
//   and `world$` vs "hello world" (true) in test_pattern_match.c.
```

## Implementation Blueprint

### Data models and structure

No new data models. T2 adds/edits only five `static` functions with primitive
arguments. It consumes (does not define) `parsed_pattern_t` (P1.M1.T2), the
`State`/`OP_*` vocabulary + `nfa_match` (P1.M2), `get_escaped_char` (P1.M1.T2.S2),
and the three leaf classifiers (P1.M3.T1.S1).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1 (S1): REPLACE the P1.M2 STUB pattern_char_matches body (site ~line 542)
  - FIND: the P1.M2 stub immediately after nfa_addstate() and before
    nfa_has_match()/nfa_match(). Its body is a bare `return false;` (or an
    equivalent placeholder) under the "===== P1.M3.T2.S1 =====" banner.
    (Confirm via: grep -nE 'static bool pattern_char_matches' pattern_match.c —
     the stub body is `return false;` with NO get_escaped_char/switch.)
  - DELETE: the stub body. Keep/refresh the banner comment so it documents PRD
    §7.7 + the tolower-cast + decode-before-fold notes.
  - IMPLEMENT: the real body (see "What" S1): escaped-literal branch
    (pc 0x01-0x04 -> get_escaped_char + case-fold with (unsigned char) casts),
    then a switch on pc: 0x05 is_digit_char; 0x06 !is_digit_char; 0x07
    is_word_char; 0x08 !is_word_char; 0x09 is_whitespace_char; 0x0A
    !is_whitespace_char; 0x0D (sc!='\n' && sc!='\r'); default ordinary literal
    (case-fold with (unsigned char) casts unless case_sensitive).
  - NAMING: exact name `pattern_char_matches` (nfa_match calls it by that name).
  - DEPENDENCIES: get_escaped_char [P1.M1.T2.S2, COMPLETE]; is_digit_char /
    is_word_char / is_whitespace_char [P1.M3.T1.S1, parallel — assume present];
    tolower [ctype.h, already included].
  - PLACEMENT: the existing site (~line 542), BEFORE nfa_match (which calls it).
    No forward declaration needed (definition-before-use).
  - DO NOT:
      * cast the classifier args to (unsigned char) — only tolower args need it;
      * add cases for 0x0B/0x0C/0x0E/0x2A (never received — see Gotchas);
      * tolower the placeholder byte (decode via get_escaped_char first);
      * change the dot to "any char" (it excludes \n/\r);
      * suppress any warning with __attribute__((unused));
      * edit any file other than pattern_match.c.

Task 2 (S2a): REPLACE the P1.M1.T2 STUB match_with_anchors body (site ~line 233)
  - FIND: the stub in the P1.M1.T2 region, immediately before the public
    pattern_match(). Its body is a bare `return false;` under the banner
    "===== P1.M3.T2.S2: match_with_anchors — the anchor strategy (PRD §7.4) =====".
    (Confirm: grep -nE 'static bool match_with_anchors\(const parsed_pattern_t'
     pattern_match.c — stub body `return false;`, NO if/else chain.)
  - DELETE: the stub body. Keep/refresh the banner (PRD §7.4 + the 4-strategy
    comment + the string_start note).
  - IMPLEMENT: the real body (see "What" S2a). NULL-guard parsed/str FIRST.
    Then read core_pattern = parsed->core_pattern. Four branches in order:
      if (start_anchored && end_anchored) -> match_reaches_end_with_start(core, str, str, cs);
      else if (start_anchored)            -> match_string_with_start(core, str, str, cs);
      else if (end_anchored)              -> loop i=0..len: if match_reaches_end_with_start(core, str+i, str, cs) return true; return false;
      else                                -> if (strlen(core)==0) return strlen(str)==0;
                                               loop i=0..len: if match_string_with_start(core, str+i, str, cs) return true; return false;
    NOTE: the 3rd arg to every wrapper call is `str` (original base), NEVER `str+i`.
  - NAMING: exact name `match_with_anchors` (pattern_match + the forward decl use it).
  - DEPENDENCIES: parsed_pattern_t [P1.M1.T2, COMPLETE]; strlen [string.h,
    included]; match_string_with_start / match_reaches_end_with_start [THIS TASK S2b,
    forward-declared by P1.M2 at lines 95-110 — call sites compile even before S2b
    lands the definitions].
  - PLACEMENT: the existing site (~line 233). Do NOT move it (the P1.M2 forward
    declarations make the call-to-wrappers resolve; moving it breaks the
    documented "match_with_anchors sits before the engine" layout).
  - DO NOT:
      * pass `str+i` as the string_start arg (breaks \b/\B absolute position);
      * omit the substring empty-core guard (else "" matches everything);
      * use `<` instead of `<=` in the offset loops (off-by-one at the tail);
      * dereference parsed->processed_pattern (may alias core_pattern or be NULL).

Task 3 (S2b): APPEND the two wrapper definitions (end of file, after nfa_match)
  - FIND: the end of nfa_match() (the last function in the file). The two
    wrappers are currently FORWARD-DECLARED ONLY (lines 95-110, by P1.M2); their
    definitions do not yet exist.
  - APPEND, after nfa_match, the banner "===== P1.M3.T2.S2: anchor-strategy
    wrappers =====" and the two definitions (see "What" S2b):
      static bool match_string_with_start(pattern, str, string_start, cs) {
          return nfa_match(pattern, str, string_start, cs, false); }
      static bool match_reaches_end_with_start(pattern, str, string_start, cs) {
          return nfa_match(pattern, str, string_start, cs, true); }
  - NAMING: exact names (the P1.M2 forward declarations + match_with_anchors use them).
  - DEPENDENCIES: nfa_match [P1.M2.T2.S2, COMPLETE — must be defined above this
    point; it is, at ~line 578].
  - PLACEMENT: AFTER nfa_match (definition-before-use). The forward declarations
    at lines 95-110 are what let match_with_anchors (line 233) call these before
    they are defined.
  - DO NOT:
      * add a second forward declaration (P1.M2's block suffices);
      * swap the full_match booleans (string_with_start=false, reaches_end=true);
      * transform/inspect the args (these are pure 1-line forwarders);
      * place them before nfa_match (would not compile without another forward decl).

Task 4: VERIFY the build + the direct S1 probe + the full committed suite (Validation).
```

### Implementation Patterns & Key Details

```c
// PATTERN: a single byte->predicate switch is the ONE place char semantics are
//   decided. Every \d\D\w\W\s\S . and literal classification flows through
//   pattern_char_matches. Get it right once; the NFA's OP_CHAR step just calls it.

// PATTERN: the 4 anchor strategies are a flat if/else-if chain on the two bool
//   flags, NOT a nested decision tree. Order matters only for readability (both-
//   anchored first, then start-only, then end-only, then default). The default
//   (no anchors) is the backward-compatible SUBSTRING match.

// PATTERN: forward the original string_start everywhere. match_with_anchors
//   receives `str` (the original base from pattern_match) and passes it as the
//   3rd arg to both wrappers in every branch, even when the 2nd arg is `str+i`.
//   nfa_match computes abspos = str - string_start from these.

// PATTERN: empty-core short-circuit in the substring branch only. The exact
//   (^+$) path relies on nfa_match's own empty handling; the prefix (^) path's
//   empty core legitimately matches any prefix (correct); only the unanchored
//   substring path needs the explicit `strlen(core)==0 => strlen(str)==0` guard.

// ANTI-PATTERN: do NOT conflate the dot (0x0D, excludes \n/\r) with the glob '*'
//   (0x2A, OP_ANY, includes \n/\r). They are different opcodes handled in
//   different nfa_match branches. pattern_char_matches handles the dot; nfa_match
//   handles OP_ANY directly. (PRD §13 #8.)

// ANTI-PATTERN: do NOT "fix" the §11.2C result for ^\w+@\w+$ vs user_host. It is
//   correctly 0 (no '@' in user_host). The PRD comment /* 1 */ is stale. The
//   committed test suite is authoritative.

// ANTI-PATTERN: do NOT add forward declarations, #includes, or
//   __attribute__((unused)). Do NOT move match_with_anchors or the wrappers. The
//   P1.M2 forward-declaration block is the contract; T2 only fills bodies + appends.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - THREE edits to pattern_match.c:
      (1) REPLACE the match_with_anchors stub body (~line 233, P1.M1.T2 region).
      (2) REPLACE the pattern_char_matches stub body (~line 542, P1.M2 engine region).
      (3) APPEND the two wrapper definitions (end of file, after nfa_match ~621-625).
    No other file changes. No new #include. No new forward declaration.

CONSUMERS (upstream callers, NOT this task):
  - pattern_char_matches <- nfa_match() OP_CHAR step     [P1.M2.T2.S2, COMPLETE]
                             (gains caller immediately; no warning; \d\D\w\W\s\S .
                              classification becomes correct)
  - match_with_anchors    <- pattern_match()             [P1.M1.T2.S2, COMPLETE]
                             (the public API now actually matches)
  - match_string_with_start     <- match_with_anchors    [THIS TASK]
  - match_reaches_end_with_start<- match_with_anchors    [THIS TASK]
  - (both wrappers)       -> nfa_match(full_match=false/true)  [P1.M2.T2.S2, COMPLETE]

CROSS-MILESTONE CONTRACTS (signatures fixed upstream; do not change):
  - parsed_pattern_t { const char *core_pattern; bool start_anchored;
                       bool end_anchored; char *processed_pattern; }  [P1.M1.T2]
  - char get_escaped_char(char placeholder);                          [P1.M1.T2.S2]
  - bool nfa_match(const char *pattern, const char *str,
                   const char *string_start, bool case_sensitive,
                   bool full_match);                                  [P1.M2.T2.S2]
  - bool is_digit_char(char c); bool is_word_char(char c);
    bool is_whitespace_char(char c);                                  [P1.M3.T1.S1, parallel]
  - The P1.M2 forward-declaration block (lines 95-110) declares
    match_with_anchors, nfa_match, match_string_with_start,
    match_reaches_end_with_start — T2 must not duplicate it.

BUILD:
  - No build-system change. Plain gcc. Validate by compiling pattern_match.c
    (Level 1), a direct S1 isolation probe (Level 2), and the FULL committed
    suite via ./run_all_tests.sh (Level 3 — the primary gate, since T2 wires the
    public API) + the PRD §11.2B/C micro-benchmarks (Level 4).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; pure functions. The runtime effect of T2 is that
    pattern_match() stops returning false for everything and starts matching per
    PRD §7/§15 — which unblocks the P2 firmware module's match_pattern() and the
    whole keyboard notifier.)
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. **T2 is what wires the public API**,
> so — unlike T1 — the **full committed suite IS the authoritative behavioral
> gate**: once T2 lands, `pattern_match()` actually matches, and all 9 test
> programs (≈2 019 assertions, via `./run_all_tests.sh`) exercise every T2 branch
> through the public API. A direct `#include "pattern_match.c"` probe (Level 2)
> additionally validates `pattern_char_matches` in isolation (it is `static`).

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o 2>/tmp/pm_warn.txt; echo "exit=$?"
cat /tmp/pm_warn.txt
# Expected: exit 0 AND /tmp/pm_warn.txt EMPTY (zero warnings). Unlike T1, T2 gives
# every function a live caller, so NO -Wunused-function may remain. FAIL if exit!=0
# or ANY warning appears (a warning means a body wasn't wired or a wrapper wasn't
# appended — do NOT suppress with __attribute__((unused))).
rm -f /tmp/pm.o /tmp/pm_warn.txt

# 1b. The two STUBS are GONE. Capture each function's full body and assert it
#     contains its real strategy token (a bare stub body is just `return false;`).
#     NOTE: the real match_with_anchors body legitimately contains `return false;`
#     inside its NULL guard (`if (!parsed || !str) return false;`), so a naive
#     `grep -A3 | grep 'return false;'` FALSELY flags the real code as a stub.
#     Assert the real tokens instead:
awk '/static bool match_with_anchors\(const parsed_pattern_t/{c=1} c&&/^\}/{c=0; print body} c{body=body $0 "\n"}' \
  pattern_match.c | grep -q 'start_anchored && parsed->end_anchored' \
  && echo "match_with_anchors real (ok)" || echo "FAIL: match_with_anchors still a stub"
awk '/static bool pattern_char_matches\(char pc/{c=1} c&&/^\}/{c=0; print body} c{body=body $0 "\n"}' \
  pattern_match.c | grep -q 'get_escaped_char(pc)' \
  && echo "pattern_char_matches real (ok)" || echo "FAIL: pattern_char_matches still a stub"

# 1c. All four T2 functions present with exact signatures + real bodies.
grep -qE 'static bool pattern_char_matches\(char pc, char sc, bool case_sensitive\)' pattern_match.c \
  && echo "pattern_char_matches sig (ok)"
grep -qE 'static bool match_with_anchors\(const parsed_pattern_t \*parsed, const char \*str, bool case_sensitive\)' pattern_match.c \
  && echo "match_with_anchors sig (ok)"
grep -qE 'static bool match_string_with_start\(const char \*pattern, const char \*str,' pattern_match.c \
  && echo "match_string_with_start sig (ok)"
grep -qE 'static bool match_reaches_end_with_start\(const char \*pattern, const char \*str,' pattern_match.c \
  && echo "match_reaches_end_with_start sig (ok)"

# 1d. The real bodies are wired (key tokens present).
grep -q 'get_escaped_char(pc)' pattern_match.c        && echo "escaped-literal branch (ok)"
grep -q 'is_digit_char(sc)' pattern_match.c           && echo "\\d case (ok)"
grep -q 'is_word_char(sc)' pattern_match.c            && echo "\\w case (ok)"
grep -q 'is_whitespace_char(sc)' pattern_match.c      && echo "\\s case (ok)"
grep -q "sc != '\\\\n' && sc != '\\\\r'" pattern_match.c && echo "dot newline-exclusion (ok)"
grep -q 'start_anchored && parsed->end_anchored' pattern_match.c && echo "exact branch (ok)"
grep -q 'strlen(core_pattern) == 0' pattern_match.c   && echo "substring empty-core guard (ok)"
grep -q 'nfa_match(pattern, str, string_start, case_sensitive, false)' pattern_match.c && echo "wrapper full_match=false (ok)"
grep -q 'nfa_match(pattern, str, string_start, case_sensitive, true)'  pattern_match.c && echo "wrapper full_match=true (ok)"

# 1e. No DUPLICATE forward declarations. Each of the 4 functions should appear
#     exactly TWICE: once as a forward declaration (P1.M2's block, lines ~95-110)
#     and once as its definition. (Do NOT count on single-line declarations —
#     nfa_match + the two wrappers have multi-line signatures.)
for fn in match_with_anchors nfa_match match_string_with_start match_reaches_end_with_start; do
  printf "%-32s occurrences=%s\n" "$fn" "$(grep -cE "static bool ${fn}\(" pattern_match.c)"
done
# Expected: 2 for EACH function (1 fwd-decl + 1 defn). FAIL if any is not 2
# (3+ => a duplicate block was added; 1 => a definition or declaration is missing).
# Belt-and-suspenders: the P1.M2 block sentinel appears exactly once.
test "$(grep -c 'GOTCHA-6' pattern_match.c)" -eq 1 && echo "single fwd-decl block (ok)" \
  || echo "FAIL: GOTCHA-6 block not unique"

# 1f. (unsigned char) cast on every tolower in pattern_char_matches (UB guard).
awk '/static bool pattern_char_matches/{f=1} f&&/^\}/{f=0} f' pattern_match.c \
  | grep -oE 'tolower\((unsigned char\))?[^)]*\)' | sort -u
# Expected: every tolower( ... ) has (unsigned char) inside. FAIL if any bare
# tolower(<var>) without the cast appears in the function body.

# 1g. No new #includes (only the pre-existing set).
grep -nE '^#include' pattern_match.c   # expect <stdbool.h> <string.h> <stdlib.h> <ctype.h> only
```

### Level 2: Direct S1 Isolation Probe (pattern_char_matches in isolation)

Because `pattern_char_matches` is `static`, this probe `#include`s the `.c` (NOT
the `.h`) so the symbol is reachable, then calls it directly. Verified against the
live source-of-truth. (The full committed suite covers it indirectly too — Level 3.)

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/t2_probe.c <<'EOF'
/* Direct isolation probe for pattern_char_matches (P1.M3.T2.S1).
   #include the .c (not .h) so the static function is visible. */
#include "pattern_match.c"
#include <stdio.h>
int main(void){
  int f = 0;
  #define CK(expr) do{ if(!(expr)){ printf("FAIL: %s\n", #expr); f++; } }while(0)

  /* ---- escaped literals 0x01-0x04 (decode via get_escaped_char, case-fold) ---- */
  CK( pattern_char_matches('\x01', '^', 1));            /* \^ vs ^        */
  CK(!pattern_char_matches('\x01', 'a', 1));            /* \^ vs a        */
  CK( pattern_char_matches('\x02', '$', 1));            /* \$ vs $        */
  CK( pattern_char_matches('\x03', '*', 1));            /* \* vs *        */
  CK( pattern_char_matches('\x04', '\\', 1));           /* \\ vs \        */
  CK( pattern_char_matches('\x01', '^', 0));            /* cs=0 still ^   */
  CK( pattern_char_matches('\x01', '^', 0));            /* case-fold noop for ^ */

  /* ---- \d (0x05) / \D (0x06) ---- */
  CK( pattern_char_matches('\x05', '5', 1));  CK( pattern_char_matches('\x05', '0', 1));
  CK(!pattern_char_matches('\x05', 'a', 1));  CK(!pattern_char_matches('\x05', '_', 1));
  CK( pattern_char_matches('\x06', 'a', 1));  CK(!pattern_char_matches('\x06', '5', 1));

  /* ---- \w (0x07) / \W (0x08) — underscore IS a word char ---- */
  CK( pattern_char_matches('\x07', 'a', 1));  CK( pattern_char_matches('\x07', 'Z', 1));
  CK( pattern_char_matches('\x07', '9', 1));  CK( pattern_char_matches('\x07', '_', 1)); /* <-- _ */
  CK(!pattern_char_matches('\x07', '-', 1));  CK(!pattern_char_matches('\x07', ' ', 1));
  CK( pattern_char_matches('\x08', '-', 1));  CK(!pattern_char_matches('\x08', 'a', 1));

  /* ---- \s (0x09) / \S (0x0A) — exactly ' \t\n\r\f\v' ---- */
  CK( pattern_char_matches('\x09', ' ', 1));  CK( pattern_char_matches('\x09', '\t', 1));
  CK( pattern_char_matches('\x09', '\n', 1)); CK( pattern_char_matches('\x09', '\r', 1));
  CK( pattern_char_matches('\x09', '\f', 1)); CK( pattern_char_matches('\x09', '\v', 1));
  CK(!pattern_char_matches('\x09', 'x', 1));
  CK( pattern_char_matches('\x0A', 'x', 1));  CK(!pattern_char_matches('\x0A', ' ', 1));

  /* ---- dot 0x0D : any char EXCEPT \n and \r (PRD §13 #8) ---- */
  CK( pattern_char_matches('\x0D', 'a', 1));  CK( pattern_char_matches('\x0D', ' ', 1));
  CK(!pattern_char_matches('\x0D', '\n', 1)); CK(!pattern_char_matches('\x0D', '\r', 1));

  /* ---- default ordinary literal, case-folded unless case_sensitive ---- */
  CK( pattern_char_matches('a', 'a', 1));  CK(!pattern_char_matches('a', 'A', 1));
  CK( pattern_char_matches('a', 'A', 0));  CK( pattern_char_matches('A', 'a', 0));
  CK( pattern_char_matches('z', 'z', 1));  CK(!pattern_char_matches('z', 'y', 1));

  printf("%d failures\n", f);
  return f ? 1 : 0;
}
EOF
# -Wno-unused-function: silence any OTHER still-unused static in pattern_match.c
# (there should be none after T2, but the flag is harmless and matches the T1 idiom).
gcc -Wall -std=c99 -Wno-unused-function -I. /tmp/t2_probe.c -o /tmp/t2_probe && /tmp/t2_probe
# Expected: a stream of nothing (all CK pass), then "0 failures", exit 0.
# CRITICAL gates: \w matches '_'; dot rejects \n/\r; \D/\W/\S are negations;
# case-fold only when case_sensitive==0; escaped literals decode then fold.
rm -f /tmp/t2_probe.c /tmp/t2_probe
```

### Level 3: Full Committed Suite (THE PRIMARY T2 BEHAVIORAL GATE)

Because T2 wires `pattern_match() → match_with_anchors → nfa_match →
pattern_char_matches`, the full suite now exercises every T2 branch through the
public API. **All 9 programs must report 0 failures.**

```bash
cd /home/dustin/projects/qmk-notifier

# 3A. Build + run all 9 suites via the canonical runner.
./run_all_tests.sh 2>&1 | tail -25
# Expected: "Total tests run across all suites: 2019 / Tests passed: 2019 /
# Tests failed: 0" and "ALL TESTS PASSED". FAIL if any suite reports failures.
# (If T1 hasn't landed its classifiers yet, the \d\D\w\W\s\S + \b\B suites will
# fail — coordinate with T1; do NOT paper over a missing classifier by changing
# pattern_char_matches' dispatch. The classifiers are T1's contract.)

# 3B. Per-suite fail-count breakdown (belt-and-suspenders).
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  gcc -o "$t" "$t.c" pattern_match.c 2>/dev/null \
    && printf "%-36s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')" \
    || echo "FAIL: $t did not build"
done
# Expected: fails=0 for every line.

# 3C. ASan/UBSan on the whole engine path (catches the tolower UB + any OOB).
gcc -O1 -g -fsanitize=address,undefined -std=c99 test_pattern_match.c pattern_match.c -I. -o /tmp/pm_asan 2>/dev/null \
  && /tmp/pm_asan >/dev/null 2>&1 && echo "ASan/UBSan test_pattern_match: clean" \
  || echo "ASan/UBSan: issue"
rm -f /tmp/pm_asan
```

### Level 4: Acceptance-Gate Micro-Benchmarks (PRD §11.2B/C)

```bash
cd /home/dustin/projects/qmk-notifier

# 4A. §11.2B — the pathological case that used to hang must finish in < 50 ms
#     (confirms pattern_char_matches/OP_CHAR + the linear X+ compile).
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
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress && timeout 5 /tmp/nfa_stress
# Expected: "result=0" and a time WELL under 50000 us (< 50 ms). FAIL if result!=0
# or if it takes >= 50 ms (a regression to catastrophic backtracking).

# 4B. §11.2C — realistic patterns. NOTE the stale-comment gotcha (see Known Gotchas):
#     the 3rd line prints 0 (CORRECT: 'user_host' has no '@'); the PRD /* 1 */ comment
#     is stale. The other five must print 1.
cat > /tmp/nfa_real.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));                       /* 1 */
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1)); /* 1 */
  printf("%d\n", pattern_match("^\\w+@\\w+$","user_host",1));            /* 0 (PRD comment stale) */
  printf("%d\n", pattern_match("v\\.code","v.code",1));                  /* 1 */
  printf("%d\n", pattern_match("a+b","aaab",1));                         /* 1 */
  printf("%d\n", pattern_match("*slack*","Slack - general",0));          /* 1 */
  return 0;
}
EOF
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real
# Expected output (verbatim):
#   1
#   1
#   0
#   1
#   1
#   1
# Do NOT alter the code to make line 3 print 1 — that would be a bug.

rm -f /tmp/nfa_stress /tmp/nfa_stress.c /tmp/nfa_real /tmp/nfa_real.c
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0, ZERO
      warnings** (no `-Wunused-function` may remain — T2 wires every function).
- [ ] Level 1: both STUBS gone (1b); all four T2 signatures present (1c); real
      bodies wired (1d — escaped-literal, `\d`,`\w`,`\s`, dot, exact branch,
      empty-core guard, both `full_match` values); no duplicate forward decls (1e);
      `(unsigned char)` on every `tolower` (1f); no new `#include` (1g).
- [ ] Level 2: `/tmp/t2_probe` prints **0 failures** (pattern_char_matches in
      isolation: `\w`/`_`, dot rejects `\n`/`\r`, `\D`/`\W`/`\S` negations,
      case-fold, escaped-literal decode-then-fold).
- [ ] Level 3: `./run_all_tests.sh` → **0 failures / 2019 passed**; per-suite
      `fails=0` for all 9; ASan/UBSan clean on `test_pattern_match`.
- [ ] Level 4: §11.2B `result=0` in **< 50 ms**; §11.2C prints `1 1 0 1 1 1`
      (the `0` on line 3 is correct — see Gotchas).

### Feature Validation

- [ ] `pattern_char_matches` implements the exact PRD §7.7 dispatch: `0x01-0x04`
      escaped literal (decode + fold), `0x05/0x06` `\d`/`\D`, `0x07/0x08`
      `\w`/`\W`, `0x09/0x0A` `\s`/`\S`, `0x0D` dot (excludes `\n`/`\r`), default
      ordinary literal (folded unless `case_sensitive`).
- [ ] `match_with_anchors` implements the exact PRD §7.4 four strategies
      (`^`+`$` exact, `^` prefix, `$` suffix loop, neither substring loop) with the
      substring empty-core guard, forwarding the **original** `str` as `string_start`
      in every call.
- [ ] Both wrappers forward to `nfa_match` with `full_match=false` (string_with_start)
      / `full_match=true` (reaches_end_with_start).
- [ ] All success criteria from "What" met; manual micro-benchmarks match Level 4.

### Code Quality Validation

- [ ] Matches the live source-of-truth branch-for-branch (PRD §17: code wins).
- [ ] REPLACE two stub bodies + APPEND two wrapper defs only — no modification to
      P1.M1 content, the P1.M2 engine, the T1 classifiers, `pattern_match.h`,
      `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`, `tasks.json`,
      `prd_snapshot.md`, `.gitignore`.
- [ ] No new `#include`; no duplicate/added forward declaration (P1.M2's block
      suffices); no `__attribute__((unused))`; no `(unsigned char)` cast on the
      classifier args (only on `tolower` args); no cases added for `0x0B/0x0C/
      0x0E/0x2A` (never received).

### Documentation & Deployment

- [ ] Banners reference PRD §7.4 / §7.7 and note the `string_start` absolute-position
      rule + the tolower-cast requirement + the decode-before-fold rule.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't pass `str+i` as the `string_start` arg in the suffix/substring loops.
  Always forward the original `str` — `\b`/`\B` need the absolute position
  (PRD §13 #10). The live code passes `str` in all four branches.
- ❌ Don't omit the substring empty-core guard (`strlen(core_pattern)==0 ⇒
  strlen(str)==0`). Without it, `""` matches every string. (Only the unanchored
  substring branch needs it.)
- ❌ Don't conflate the dot (`0x0D`, excludes `\n`/`\r`) with the glob `*` (`0x2A`,
  `OP_ANY`, includes `\n`/`\r`) — PRD §13 #8. They are different opcodes in
  different `nfa_match` branches.
- ❌ Don't call `tolower` without casting BOTH args to `(unsigned char)` — a
  high-bit byte is UB (PRD §7.7). This is the one place casts are needed in P1.M3.
- ❌ Don't `tolower` the escaped-literal placeholder byte (`0x01-0x04`) directly —
  decode it via `get_escaped_char` first, then fold the resulting literal.
- ❌ Don't add `switch` cases for `0x0B`/`0x0C` (`\b`/`\B`), `0x0E` (`+`), or
  `0x2A` (`*`) — those bytes never reach `pattern_char_matches`; the `default`
  ordinary-literal arm is correct for any unexpected byte.
- ❌ Don't "fix" the §11.2C `^\w+@\w+$` vs `user_host` result to `1`. It is
  correctly `0` (no `@`); the PRD comment is stale (PRD §17). The committed suite
  is authoritative.
- ❌ Don't add a duplicate forward-declaration block, a new `#include`, or
  `__attribute__((unused))`. Don't move `match_with_anchors` or the wrappers — the
  P1.M2 forward declarations (lines 95-110) are the contract; T2 only fills bodies
  and appends the two wrapper definitions after `nfa_match`.
- ❌ Don't suppress any `-Wunused-function` warning after T2 — there should be none.
  A warning means a body wasn't wired or a wrapper wasn't appended; fix the wiring,
  not the symptom.

---

## Confidence Score

**9/10** — One-pass success is highly likely. The live code is the verified source
of truth (PRD §17): the two stub sites T2 replaces, the exact bodies (verified at
lines 233, 542, 621-625), the P1.M2 forward-declaration block T2 relies on
(lines 95-110), the four anchor strategies with their `full_match`/loop mapping,
the substring empty-core-only-matches-empty special case, the `string_start`
absolute-position rule, the `(unsigned char)`-cast-on-`tolower` requirement, the
dot-vs-glob newline distinction, the stale §11.2C comment (don't "fix" correct
code), and the full-suite-as-gate validation approach are all documented and gated.
The only residual risks are an implementer (a) forwarding `str+i` as `string_start`
(breaking `\b`/`\B`), (b) dropping the substring empty-core guard, (c) omitting
the `(unsigned char)` cast on a `tolower` arg (UB), or (d) "correcting" the
§11.2C `0` to `1` — all encoded as hard anti-patterns and caught by Level 1/3/4
gates. The parallel dependency on T1 (the three classifiers) is a coordination
point, not a T2 risk: T2 calls them by name and assumes their exact T1 semantics;
if any is missing or wrong, that is a T1 defect to surface, not paper over.