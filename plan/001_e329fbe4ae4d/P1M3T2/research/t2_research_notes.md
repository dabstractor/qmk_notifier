# Research Notes — P1.M3.T2 (Single-Char Predicate & Anchor Strategy)

> Scope: S1 (`pattern_char_matches`) + S2 (`match_with_anchors` +
> `match_string_with_start` + `match_reaches_end_with_start`). The live
> `pattern_match.c` is the verified source of truth (PRD §17: "the code + the
> passing tests win"). All 2019 committed assertions pass at HEAD; compile is
> clean (`gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0).

## 1. What T2 produces and what it replaces (the stub idiom)

The plan builds `pattern_match.c` incrementally, each milestone leaving STUBS that
later milestones replace. P1.M2 (PRP lines 22-23, 264-279, 406-412, 493-517, 585-593)
documented these pending contracts explicitly:

| Symbol | Left by | State before T2 | T2 action |
|---|---|---|---|
| `is_word_boundary` | P1.M2 (stub) | STUB `return false;` | **P1.M3.T1** (parallel) → real position test |
| `is_digit_char` / `is_word_char` / `is_whitespace_char` | (new in T1) | absent | **P1.M3.T1** (parallel) → real predicates |
| `pattern_char_matches` | P1.M2 (stub) | STUB — "reached only at runtime via nfa_match, which is dead until match_with_anchors is real (P1.M3.T2.S2) — so a stub is also fine" | **THIS TASK S1** → real switch predicate |
| `match_with_anchors` | P1.M1.T2 (stub) | STUB `return false;` (PRP P1M2: "ends with the public pattern_match() / a stub match_with_anchors") | **THIS TASK S2** → real 4-branch anchor strategy |
| `match_string_with_start` / `match_reaches_end_with_start` | P1.M2 (forward-decl only) | forward-declared, NOT defined | **THIS TASK S2** → add the two definitions (thin nfa_match forwarders) |

**Cross-milestone contract that T2 depends on (must be present when T2 runs):**
- **P1.M2** provided the **forward-declaration block** at lines 95-110 ("GOTCHA-6")
  so `match_with_anchors` (sitting in the P1.M1.T2 region at line 233, BEFORE the
  NFA engine) can call `nfa_match` + the two wrappers (defined AFTER the engine at
  lines 578-625). T2 does NOT add these forward declarations — they exist. T2 fills
  the bodies that they reference.
- **P1.M2** provided `nfa_match(const char *pattern, const char *str, const char
  *string_start, bool case_sensitive, bool full_match)` — the function T2's
  wrappers forward to. `full_match=false` ⇒ MATCH reachable at any point
  (prefix/substring); `full_match=true` ⇒ MATCH reachable only after consuming the
  whole string (suffix/exact). (P1M2/PRP "What D"; pattern_match.c:578.)
- **P1.M3.T1** (parallel) provides the three leaf classifiers
  (`is_digit_char`/`is_word_char`/`is_whitespace_char`) that S1's
  `pattern_char_matches` calls by name for the `\d/\D/\w/\W/\s/\S` cases, and the
  real `is_word_boundary` (unrelated to T2 except both feed the NFA).

## 2. Exact deliverable (verified against live code at HEAD)

### S1 — `pattern_char_matches` (pattern_match.c:542)

```c
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

Dispatch table (processed-pattern byte `pc` → predicate), PRD §7.7 + Appendix B:
- `0x01-0x04` → escaped literal `\^ \$ \* \\`, decoded via `get_escaped_char` (P1.M1.T2.S2) FIRST, then case-folded.
- `0x05`/`0x06` → `\d`/`\D` = `is_digit_char` / negation.
- `0x07`/`0x08` → `\w`/`\W` = `is_word_char` / negation.
- `0x09`/`0x0A` → `\s`/`\S` = `is_whitespace_char` / negation.
- `0x0D` → dot = `sc != '\n' && sc != '\r'` (PRD §13 #8: dot excludes newline; glob `*` does NOT).
- default → ordinary literal byte, case-folded.

**Non-obvious correctness anchors:**
- `(unsigned char)` cast on BOTH `tolower` args is MANDATORY — `tolower`'s arg is
  `int`; a negative `char` (high-bit byte) is UB. This is the ONE place casts are
  needed (the T1 classifiers need none — they use range tests). PRD §7.7.
- Escaped-literal placeholders (`0x01-0x04`) are decoded via `get_escaped_char`
  BEFORE case-folding — never fold the placeholder byte itself (it would collide
  with `tolower` semantics on a control byte).
- The dot case must be `sc != '\n' && sc != '\r'`, NOT "any char" (that's `OP_ANY`
  / glob `*`, a different NFA opcode). PRD §13 #8.

### S2 — `match_with_anchors` (pattern_match.c:233) + 2 wrappers (621-625)

```c
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

static bool match_string_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, false);   /* reach-any */
}
static bool match_reaches_end_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, true);    /* consume-whole */
}
```

The 4-strategy table (PRD §7.4), all verified against test_pattern_match.c (376 cases):
| Anchors | Strategy | wrapper | full_match | loops offsets? |
|---|---|---|---|---|
| `^`+`$` | exact | `match_reaches_end_with_start` | true | no (offset 0 only) |
| `^` | prefix | `match_string_with_start` | false | no |
| `$` | suffix | `match_reaches_end_with_start` | true | yes `i=0..len` |
| neither | substring | `match_string_with_start` | false | yes `i=0..len` |

**Non-obvious correctness anchors:**
- `string_start` is ALWAYS the original `str` base forwarded to every call —
  NEVER the per-offset `str+i`. This is PRD §13 #10: absolute position for
  `\b`/`\B` must be from the original string base. `nfa_match` computes
  `abspos = str - string_start` from these.
- Substring empty-core special case: `if (strlen(core_pattern)==0) return
  strlen(str)==0;` — an empty pattern matches ONLY the empty string, NOT every
  string (PRD §7.4). This MUST run before the offset loop (an empty core would
  otherwise trivially "match" at every offset). The `^`+`$` exact path does NOT
  need this guard because `match_reaches_end_with_start("", str, str, true)`
  itself handles it via nfa_match's start==NULL defensive guard... actually no —
  the live code relies on nfa_match's `if (!start) return full_match ? (*str==
  '\0') : true;` for the exact-empty case. The substring branch needs the explicit
  guard because `match_string_with_start("", ...)` returns `true` unconditionally
  (empty pattern = match-empty-prefix at any offset). This is why the empty-core
  guard is in the substring branch only.
- NULL guards FIRST (`!parsed || !str` → false) — `pattern_match` already guards,
  but `match_with_anchors` is also defensively safe.
- Suffix/substring loops are `i <= str_len` (inclusive of `len`): the empty-core
  case aside, this lets a trailing-`$` or unanchored pattern match at the very end
  position (e.g. suffix match of `""`). Verified by `^$`-vs-`""` (true) and
  `world$`-vs-`hello world` (true) in test_pattern_match.c.

## 3. Validation approach (why the committed suites ARE the gate, unlike T1)

Unlike T1 (whose functions were inert until match_with_anchors landed), **T2 is
what wires the public API**: `pattern_match` → `match_with_anchors` → `nfa_match`
→ `pattern_char_matches`. So once T2 lands, the FULL committed suite (2019
assertions across 9 programs, run via `./run_all_tests.sh`) is the authoritative
behavioral gate — it exercises every T2 branch through the public API.

For S1 isolation, a direct `#include "pattern_match.c"` probe (mirrors the T1
Level-2 approach) validates `pattern_char_matches` in isolation, since it's
`static` and unreachable across translation units.

### Acceptance gate verified at HEAD (PRD §11.2)
- §11.2B pathological stress `a+a+a+a+a+a+a+a+a+a+b` vs 199×`a` → `result=0` in
  ~1090 µs (< 50 ms ✓). Confirms `pattern_char_matches`/`OP_CHAR` correctness and
  the linear `X+` compile (no catastrophic backtracking).
- §11.2C realistic patterns → `1 1 0 1 1 1` (six prints). **The third `0` is
  CORRECT**, despite the PRD §11.2C source comment `/* 1 */`:
  `pattern_match("^\\w+@\\w+$","user_host",1)` — `user_host` has NO `@`, so
  `^\w+@\w+$` cannot match. The PRD comment is STALE (PRD §17 acknowledges stale
  figures). The live code's `0` is right; the committed test_pattern_match.c
  (376 cases, all pass) is authoritative. **Do not "fix" the code to print `1`.**

## 4. Test-coverage mapping for T2 branches (which suite covers what)

- `pattern_char_matches` `\d/\D/\w/\W/\s/\S` → `test_char_classification.c` (179
  cases), `test_metachar_verification.c` (smoke), and the metachar sections of
  `test_pattern_match.c`.
- `pattern_char_matches` dot (`0x0D`) newline-exclusion → `test_pattern_match.c`
  (`a.b` vs `a\nb` → false; `*` vs `a\nb` → true) per PRD Appendix A.
- `pattern_char_matches` escaped-literal + case-fold → `test_pattern_match.c`
  escape/case sections + Appendix A (`\^`, `a\+b`, `v\.code`, `abc` vs `ABC`).
- `match_with_anchors` exact/prefix/suffix/substring → `test_pattern_match.c`
  `test_start_anchor`/`test_end_anchor`/`test_full_anchor`/`test_anchor_wildcard`
  + Appendix A (`^hello$` exact, `world$` suffix, `sear*term` substring).
- empty-core substring special case → `^$` vs `""` (true), `""` vs `""` semantics.
- `\b`/`\B` integration with anchors → `test_word_boundary_integration.c` (189 cases).
- Robustness (NULL, garbage) → `test_error_handling.c`, `test_memory_stress.c`,
  `test_invalid_patterns.c` (1008 cases).

## 5. External reference (already cited in the code/PRP)

Russ Cox, *"Regular Expression Matching Can Be Simple And Fast"*,
https://swtch.com/~rsc/regexp/regexp1.html — the Thompson-NFA two-list
simulation. The anchor-strategy layer (`match_with_anchors`) is the standard
"unanchored ⇒ try a match starting at each position" loop layered on top of the
NFA's anchored-from-a-given-start simulation (`nfa_match`). No new external
dependency; this is reference reading, not a library.

## 6. Confidence

9/10. The exact bodies, the forward-declaration mechanism they rely on (P1.M2),
the four anchor strategies with their full_match/loop mapping, the
empty-core-only-matches-empty special case, the `string_start` absolute-position
rule, the `(unsigned char)` cast for `tolower`, the dot-vs-glob newline
distinction, the stale §11.2C comment (don't "fix" correct code), and the
validation approach (full committed suite as the gate + an S1 isolation probe) are
all verified against the live, passing code. Residual risk: an implementer (a)
forgetting the substring empty-core guard, (b) forwarding `str+i` as `string_start`
(breaking `\b`/`\B`), or (c) "correcting" the §11.2C `0` to `1` — all encoded as
hard anti-patterns/gates below.