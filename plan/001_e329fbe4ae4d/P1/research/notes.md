# Research Notes — P1: Pattern Matching Engine (consolidated)

Source of truth for all remaining P1 functions: **git commit `81df853`
("implemented nfa matching engine")** — extracted via
`git show 81df853:pattern_match.c`. PRD §17: "the code + the passing tests win".

## What is ALREADY DONE (treat as CONTRACTS — do not rewrite)

Verified present in current `pattern_match.c` (473 lines):
- `#include <stdbool.h>`, `<string.h>`, `<stdlib.h>` (NO `<ctype.h>` — see GOTCHA-1)
- `process_escapes()` — complete (P1.M1.T2.S1)
- `parsed_pattern_t`, `get_escaped_char()`, `free_parsed_pattern()`,
  `parse_pattern()` — complete (P1.M1.T2.S2)
- `match_with_anchors()` — **STUB returning false** (P1.M3.T2.S2 replaces)
- `pattern_match()` public entry — complete
- `NFA_MAX_PATTERN`/`NFA_MAX_STATES`, `enum {OP_CHAR,OP_ANY,OP_SPLIT,OP_ASSERT,OP_MATCH}`,
  `struct State`, `static int nfa_gen = 0;` — complete (P1.M2.T1.S1)
- `nfa_compile()` — complete (P1.M2.T1.S2)
- `is_word_boundary()` — **STUB returning false** (P1.M3.T1.S1 replaces)
- `nfa_addstate()` — complete (P1.M2.T2.S1)

## What REMAINS (this PRP's scope)

1. **P1.M3.T1.S1** — `is_digit_char`, `is_word_char`, `is_whitespace_char` (new) +
   REPLACE `is_word_boundary` stub with real body.
2. **P1.M3.T2.S1** — `pattern_char_matches(pc, sc, case_sensitive)` (new).
3. **P1.M2.T2.S2** — `nfa_has_match()` + `nfa_match()` (new; the step loop).
4. **P1.M3.T2.S2** — REPLACE `match_with_anchors` stub; add
   `match_string_with_start` + `match_reaches_end_with_start`.
5. **P1.M4** — verify the 9 existing suites pass via `./run_all_tests.sh` +
   pathological + realistic acceptance gates (PRD §11.2 A/B/C).

## Exact reference bodies (from `git show 81df853:pattern_match.c`)

### nfa_has_match + nfa_match (commit 81df853 lines ~167-206)
```c
static int nfa_has_match(State **list, int n) {
    for (int i = 0; i < n; i++) if (list[i]->op == OP_MATCH) return 1;
    return 0;
}

static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive,
                      bool full_match) {
    State pool[NFA_MAX_STATES];
    int nstates;
    State *start = nfa_compile(pattern, pool, &nstates);
    if (!start) return full_match ? (*str == '\0') : true;  /* empty pattern */
    (void)nstates;

    State *clist_buf[NFA_MAX_STATES];
    State *nlist_buf[NFA_MAX_STATES];
    State **clist = clist_buf, **nlist = nlist_buf;
    int cn = 0, nn;
    size_t abspos = (size_t)(str - string_start);

    nfa_gen++;
    nfa_addstate(clist, &cn, start, string_start, abspos);
    if (!full_match && nfa_has_match(clist, cn)) return true;   /* empty prefix */

    size_t pos = abspos;
    for (const char *p = str; *p; p++, pos++) {
        char c = *p;
        nfa_gen++; nn = 0;
        for (int i = 0; i < cn; i++) {
            State *s = clist[i];
            if (s->op == OP_ANY) {
                nfa_addstate(nlist, &nn, s->out, string_start, pos + 1);
            } else if (s->op == OP_CHAR && pattern_char_matches(s->arg, c, case_sensitive)) {
                nfa_addstate(nlist, &nn, s->out, string_start, pos + 1);
            }
        }
        State **tmp = clist; clist = nlist; nlist = tmp; cn = nn;  /* swap */
        if (cn == 0) break;                                       /* dead */
        if (!full_match && nfa_has_match(clist, cn)) return true; /* prefix matched */
    }
    return nfa_has_match(clist, cn) ? true : false;               /* full: only at end */
}
```

### pattern_char_matches (commit 81df853 lines ~38-54)
```c
static bool pattern_char_matches(char pc, char sc, bool case_sensitive) {
    if (pc >= '\x01' && pc <= '\x04') {
        char literal = get_escaped_char(pc);
        return case_sensitive ? (literal == sc)
                              : (tolower((unsigned char)literal) == tolower((unsigned char)sc));
    }
    switch (pc) {
        case '\x05': return is_digit_char(sc);           /* \d */
        case '\x06': return !is_digit_char(sc);          /* \D */
        case '\x07': return is_word_char(sc);            /* \w */
        case '\x08': return !is_word_char(sc);           /* \W */
        case '\x09': return is_whitespace_char(sc);      /* \s */
        case '\x0A': return !is_whitespace_char(sc);     /* \S */
        case '\x0D': return (sc != '\n' && sc != '\r');  /* . */
        default:  return case_sensitive ? (pc == sc)
                                  : (tolower((unsigned char)pc) == tolower((unsigned char)sc));
    }
}
```

### classifiers (commit 81df853 lines ~441-520)
```c
static bool is_digit_char(char c)      { return c >= '0' && c <= '9'; }
static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || (c == '_');
}
static bool is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static bool is_word_boundary(const char *str, size_t pos) {
    if (!str) return false;
    size_t str_len = strlen(str);
    if (pos == 0)        return (str_len > 0 && is_word_char(str[0]));
    if (pos == str_len)  return (str_len > 0 && is_word_char(str[str_len - 1]));
    if (pos > str_len)   return false;
    return is_word_char(str[pos - 1]) != is_word_char(str[pos]);
}
```

### match_with_anchors + helpers (commit 81df853 lines ~258-330)
```c
static bool match_reaches_end_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, true);
}
static bool match_string_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, false);
}
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive) {
    if (!parsed || !str) return false;
    const char *core_pattern = parsed->core_pattern;
    if (parsed->start_anchored && parsed->end_anchored) {           /* ^...$ exact */
        return match_reaches_end_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->start_anchored) {                            /* ^ prefix */
        return match_string_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->end_anchored) {                              /* $ suffix */
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++)
            if (match_reaches_end_with_start(core_pattern, str + i, str, case_sensitive)) return true;
        return false;
    } else {                                                        /* substring */
        if (strlen(core_pattern) == 0) return strlen(str) == 0;     /* empty -> only empty */
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++)
            if (match_string_with_start(core_pattern, str + i, str, case_sensitive)) return true;
        return false;
    }
}
```

## Dependency order (for task sequencing)
classifiers → pattern_char_matches (needs get_escaped_char [exists] + classifiers)
→ nfa_match/nfa_has_match (needs nfa_compile [exists], nfa_addstate [exists],
   pattern_char_matches) → match_with_anchors/helpers (needs nfa_match).

## GOTCHAS (load-bearing for one-pass success)

- **GOTCHA-1 (`<ctype.h>` REQUIRED):** `pattern_char_matches` and the case-fold
  branch use `tolower()`. Current `pattern_match.c` does NOT include `<ctype.h>`
  (only stdbool/string/stdlib). Reference 81df853 DOES. Add `#include <ctype.h>`.
  Without it: implicit-declaration of `tolower` → wrong/warning, and on some
  toolchains a link error. `tolower` takes `(unsigned char)` or `EOF`; always cast
  arg to `(unsigned char)` to avoid sign-extension UB on high-bit chars.
- **GOTCHA-2 (empty pattern in nfa_match):** `nfa_compile("")` returns `NULL`
  (it allocates only the OP_MATCH and writes it into `*tail`, but `tail=&start`
  and no unit ran, so start stays NULL — wait, actually it DOES append MATCH into
  *tail=&start). RE-CHECK: with empty pat the for-loop body never runs, then
  `*tail = m` writes MATCH into `&start`, so start IS the MATCH state (non-NULL).
  => the `if (!start)` branch is NOT the empty-pattern path in the reference; it
  is a defensive guard. The empty-pattern semantics are carried by the seed
  closure: `nfa_addstate(clist,&cn,start,...)` adds MATCH, and for !full_match
  `nfa_has_match` returns true immediately. KEEP the `if (!start)` guard anyway
  (reference has it) but do not rely on it for empty patterns.
- **GOTCHA-3 (`nfa_gen++` ownership):** bump happens INSIDE nfa_match — once
  before the seed closure, once per consumed char. NEVER in nfa_addstate.
- **GOTCHA-4 (abspos vs pos):** `abspos = str - string_start` is the seed offset;
  `pos` starts at abspos and increments per char. BOTH are passed to
  nfa_addstate as the absolute position so `\b`/`\B` see the ORIGINAL string.
  Consuming states forward `pos + 1`.
- **GOTCHA-5 (OP_ANY vs dot):** OP_ANY (glob `*`) consumes ANY byte incl `\n`/`\r`;
  dot (0x0D via pattern_char_matches) excludes them. Do not conflate.
- **GOTCHA-6 (forward decls):** match_with_anchors is currently a STUB up top
  (near pattern_match). The real helpers (match_string_with_start,
  match_reaches_end_with_start) must be DECLARED before match_with_anchors OR
  match_with_anchors moved below them. Reference forward-declares all statics at
  top. Simplest: forward-declare the two helpers, keep match_with_anchors where
  the stub is (replace body in place), define the two helpers + nfa_match at the
  bottom after nfa_addstate.
- **GOTCHA-7 (`is_word_boundary` empty-string):** the stub short-circuits on
  `*string_start=='\0'` inside nfa_addstate BEFORE calling is_word_boundary, so
  the empty-string semantics are independent of the real impl. Keep the real
  impl's NULL guard (`if (!str) return false;`).
- **GOTCHA-8 (case-insensitive default):** `case_sensitive` defaults to false in
  the reference keymap; pattern_char_matches handles folding via tolower.
- **GOTCHA-9 (warning budget):** when all functions land, ZERO `-Wunused-function`
  warnings should remain (get_escaped_char consumed by pattern_char_matches;
  nfa_compile by nfa_match; nfa_addstate by nfa_match; nfa_has_match by nfa_match;
  classifiers by pattern_char_matches; helpers by match_with_anchors).

## Test framework (already exists — P1.M4 is VERIFY, not AUTHOR)
- 9 `test_*.c` files link ONLY `pattern_match.c`, call only public `pattern_match()`.
- Framework: `test_case_t {pattern, input, case_sensitive, expected, description}`;
  `run_test()` prints PASS/FAIL; summary `Total tests run: N / passed / failed`.
- `run_all_tests.sh` compiles all 9 (exact flags in PRD §11.1), runs them, greps
  summaries, then builds+runs a perf micro-benchmark. Exit non-zero on any failure.
- Current state (stub matcher): 630/2019 fail by design. After P1 completes: 0 fail.
