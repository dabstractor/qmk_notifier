# PRP — P1.M1.T2.S2: Implement `parse_pattern()`, `free_parsed_pattern()`, `pattern_match()`, `get_escaped_char()`

## Goal

**Feature Goal**: Complete the **parsed-pattern pipeline + public entry point**
of the matcher. Append four functions (and one temporary stub) to
`pattern_match.c`:

1. `parse_pattern(pattern)` — detect `^`/`$` anchors (even-backslash-count rule
   for `$`), carve out the core, run it through `process_escapes()` (from S1),
   and return a `parsed_pattern_t`.
2. `free_parsed_pattern(&parsed)` — free the malloc'd processed buffer, NULL the
   pointers, NULL-safe.
3. `get_escaped_char(placeholder)` — reverse-map a placeholder byte
   (`0x01`→`^`, …, `0x0D`→`.`) for `pattern_char_matches` (P1.M3).
4. `pattern_match(pattern, str, case_sensitive)` — the **public** API declared
   in `pattern_match.h`: NULL-guard → parse → match → free.
5. `match_with_anchors(&parsed, str, cs)` — **temporary STUB** returning `false`
   (the real one is P1.M3.T2.S2).

The functions must reproduce the live source-of-truth behavior **byte-for-byte**
(existing `pattern_match.c` @ git `HEAD` is authoritative, PRD §17).

**Deliverable**: `pattern_match.c` with the above appended to S1's
`process_escapes()`. No other file is modified.

**Success Definition**:
- All five items present with the exact signatures below; `parse_pattern`,
  `free_parsed_pattern`, `get_escaped_char`, `match_with_anchors` are `static`;
  `pattern_match` is the sole non-static (public) function.
- A `#include "pattern_match.c"` test harness passes **all 14 `parse_pattern`
  truth cases**, the `get_escaped_char` reverse map, `free_parsed_pattern`
  NULL-safety, and the `pattern_match` NULL-guard.
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` exits 0 with **exactly one**
  expected warning (`get_escaped_char defined but not used`, self-resolves in
  P1.M3.T2.S1); no other warnings.
- A real committed test suite (e.g. `test_metachar_verification.c`) still
  **LINKS** cleanly with the new `pattern_match.c` (public API intact).
- `parse_pattern` carries inline comments documenting the even-backslash-count
  rule (Mode A, item-spec §6).

## User Persona (if applicable)

**Target User**: Callers of the public `pattern_match()` — primarily
`match_pattern()` in `notifier.c` (P2) and the host-side test suites (P1.M4).
End users never call the static helpers.

**Use Case**: `pattern_match("\\bword\\b", "a word here", false)` must (once the
NFA lands) return true. Today the entry point must at least be **safe**:
non-crashing on any input (NULL → false), allocating and freeing exactly once.

**User Journey**: `pattern_match()` → `parse_pattern()` strips `^`/`$` and calls
`process_escapes()` → `match_with_anchors()` (stub now, real in P1.M3.T2.S2) →
`free_parsed_pattern()` frees the processed buffer. Caller frees nothing.

**Pain Points Addressed**: Separates anchor detection from escape processing so
each stage is independently testable, and gives the (future) matcher a single
processed-pattern string plus two boolean flags. Guarantees one malloc / one
free per call (no leaks — the contract `test_memory_stress` will enforce).

## Why

- **Wires the public API**: until `pattern_match()` exists, the test suites and
  `notifier.c` cannot link. This subtask makes the symbol real (and safe), even
  though real matching arrives in P1.M3.T2.S2.
- **Anchor correctness is subtle**: the even-backslash-count rule for `$`
  (`\$` vs `\\$` vs `\\\$`) is easy to get wrong; getting it wrong silently
  mis-classifies anchors and breaks every anchored pattern. The rule is
  documented inline (Mode A) and locked by the truth table.
- **Memory contract**: `parse_pattern` owns the single `malloc` (via
  `process_escapes`); `free_parsed_pattern` is its guaranteed counterpart. A bug
  here = leak (`test_memory_stress` failure) or double-free/use-after-free.
- **Rebuild integrity**: appends cleanly to S1's `process_escapes`, removes S1's
  interim unused-function warning (parse_pattern now USES process_escapes), and
  sets up P1.M2 (NFA) / P1.M3 (match_with_anchors + classifiers) to append next.

## What

Append to `pattern_match.c` (which currently contains only the S1
`process_escapes()` plus `#include <stdbool.h> <string.h> <stdlib.h>`):

1. The `parsed_pattern_t` typedef.
2. A small forward-declaration block (so body ordering is robust and the
   cross-subtask `match_with_anchors` boundary is explicit).
3. `get_escaped_char`, `free_parsed_pattern`, `parse_pattern` (Mode-A doc'd),
   the `match_with_anchors` **stub**, and the public `pattern_match`.
4. **No new `#include`s** — `stdbool.h`/`string.h`/`stdlib.h` (already present)
   cover `strlen`, `strncpy`, `malloc`, `free`, `bool`.

### Success Criteria

- [ ] `parse_pattern`, `free_parsed_pattern`, `get_escaped_char`,
      `match_with_anchors` are `static`; `pattern_match` is non-static.
- [ ] All 14 `parse_pattern` truth cases + `get_escaped_char` map + free-safety +
      NULL-guard pass (Validation Level 2).
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0, sole warning is
      `get_escaped_char` unused.
- [ ] `gcc -Wall test_metachar_verification.c pattern_match.c` → links cleanly.
- [ ] Mode-A inline comments on `parse_pattern` document the even-backslash rule.

## All Needed Context

### Context Completeness Check

**Pass.** The exact implementation is the live source of truth
(`git show HEAD:pattern_match.c`, PRD §17) and is reproduced verbatim in
"Implementation Tasks" below. The complete 14-case `parse_pattern` truth table,
the `get_escaped_char` map, the free-safety behavior, and the NULL-guard were
**executed against the reference during research and passed**; the build warning
set and the test-suite link were **empirically confirmed**. An implementer with
only this PRP + repo access can produce the functions behavior-identically and
prove it.

### Documentation & References

```yaml
# MUST READ — authoritative spec
- file: PRD.md
  section: "### 7.3 Anchor detection (parse_pattern)"
  why: "Defines the anchor rules: leading ^ => start_anchored; trailing $ only
        when NOT escaped (even backslash count, including zero) => end_anchored;
        the substring between is the core fed to process_escapes."
  critical: "The even-backslash-count rule is THE subtle part. \$ (1 bs) is an
        escaped literal (core byte 0x02); \\$ (2 bs) is a real end anchor with
        an escaped backslash in the core (0x04). Item-spec §6 requires Mode-A
        inline comments on this rule."

- file: PRD.md
  section: "### 7.1 The processed-pattern byte contract (what the NFA consumes)"
  why: "parse_pattern's core_pattern output is exactly what process_escapes
        emits; the truth-table core bytes (0x01 escaped caret, 0x02 escaped
        dollar, 0x03 escaped star, 0x04 escaped backslash, 0x2A bare star, ...)
        come from here."
  critical: "core_pattern points at process_escapes() output. On malloc failure
        it falls back to the RAW pattern (no escape processing)."

- file: PRD.md
  section: "## 6. File Specification: pattern_match.h"
  why: "The public contract: pattern_match(pattern, str, case_sensitive);
        @note returns false if either arg is NULL; memory managed internally,
        caller frees nothing; thread-safe (no global state)."
  critical: "Do NOT change pattern_match.h (owned by P1.M1.T1.S1, COMPLETE).
        pattern_match is the ONLY public symbol; all S2 helpers are static."

- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "Rows confirm the anchor/escape semantics: '^$ -> empty -> true',
        '^hello -> starts with', 'world$ -> ends with', '\\^ -> literal ^'."
  critical: "'^$ -> empty -> true' is the key edge that the `end > start` guard
        in parse_pattern must preserve (both anchors, empty core)."

- file: PRD.md
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"
  why: "'the code + the passing tests win' — the existing pattern_match.c
        (git HEAD) is authoritative. Reproduce S2's functions byte-for-behavior."
  critical: "Comment drift is tolerated; correctness = passing tests + matching
        the reference behavior, not exact formatting."

# Architecture — call chain + data structure
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## Call Chain (Data Flow)" + "### parsed_pattern_t" + "### parse_pattern Anchor Detection" + "## Memory Model"
  why: "Spells out the pattern_match -> parse_pattern -> process_escapes ->
        match_with_anchors -> free_parsed_pattern chain, the struct fields, and
        the single-malloc memory model."
  critical: "core_pattern points INTO processed_pattern (or the raw pattern on
        fallback); free_parsed_pattern frees processed_pattern and NULLs BOTH
        pointers. This is the ownership boundary S2 must implement."

# Dependency PRPs (what exists when S2 starts)
- file: plan/001_e329fbe4ae4d/P1M1T2S1/PRP.md
  section: "## Implementation Tasks" (Task 2: process_escapes body)
  why: "S1 is being implemented in parallel and produces the static
        `char *process_escapes(const char *)` that parse_pattern calls, plus the
        three includes (stdbool/string/stdlib). Treat as a CONTRACT."
  critical: "parse_pattern depends on process_escapes returning a malloc'd,
        NUL-terminated string (or NULL on failure/NULL input). On NULL result,
        parse_pattern falls back to the raw pattern. Do NOT duplicate
        process_escapes — call it."

- file: plan/001_e329fbe4ae4d/P1M1T1S1/PRP.md
  section: "## Goal" / Implementation
  why: "pattern_match.h declares ONLY `bool pattern_match(...)`. Confirms the
        static helpers are NOT in the header and must stay file-local."
  critical: "Do NOT add parse_pattern/free_parsed_pattern/get_escaped_char/
        match_with_anchors to the header."

# Build convention
- file: run_all_tests.sh
  why: "Toolchain is plain gcc, no make. Each suite is built as
        `gcc -o test_X test_X.c pattern_match.c` (some add -std=c99/-DNOTIFIER_STUB/-I.).
        Suites #include \"pattern_match.h\" and call ONLY the public pattern_match()."
  critical: "Because the helpers are static, S2 validates them with a temporary
        #include harness (like S1), NOT a committed test file. The committed
        suites only prove the public API LINKS; they will not PASS until
        P1.M3.T2.S2 (stub returns false)."

# Downstream (informational; NOT this task)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast'. Cited by
        the reference for the NFA engine (P1.M2). S2 itself uses only libc
        (malloc/free/strlen/strncpy); no regexp theory is needed here."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # S1 output: includes (stdbool/string/stdlib) + process_escapes().
                        #   THIS task APPENDS the S2 functions to this file.
notifier.h notifier.c  # P2 scope — do not touch.
rules.mk               # P2 scope — do not touch.
test_*.c               # P1.M4 scope — do not touch. They #include "pattern_match.h".
run_all_tests.sh       # P1.M4 scope — do not touch.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # S2 APPENDS to it. After S2 it contains:
                       #   - includes (from S1)
                       #   - process_escapes()                 [S1, static]
                       #   - parsed_pattern_t typedef          [S2, NEW]
                       #   - forward decls                     [S2, NEW]
                       #   - get_escaped_char()                [S2, static, NEW]
                       #   - free_parsed_pattern()             [S2, static, NEW]
                       #   - parse_pattern()                   [S2, static, NEW]
                       #   - match_with_anchors()  STUB        [S2, static, NEW; real in P1.M3.T2.S2]
                       #   - pattern_match()      PUBLIC       [S2, NEW]
                       # Later subtasks APPEND:
                       #   P1.M2  -> State, nfa_compile, nfa_match, nfa_addstate, ...
                       #   P1.M3  -> classifiers, pattern_char_matches,
                       #             REAL match_with_anchors, match_string_with_start,
                       #             match_reaches_end_with_start
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — Rebuild model (same as S1): the working tree holds S1's minimal
//   pattern_match.c (process_escapes only). The FULL reference is at git HEAD
//   (git show HEAD:pattern_match.c, 514 lines). S2 APPENDS its functions to the
//   working file; do NOT overwrite process_escapes or the includes. PRESERVE all
//   existing content. The full matcher reappears incrementally across S2/P1.M2/P1.M3.

// CRITICAL — unused-function warning reality (verified during research):
//   gcc -Wall -Wextra -c WARNS for static fns defined-but-unused; -fsyntax-only does NOT.
//   * process_escapes currently warns (no caller yet) — AFTER S2 the warning
//     DISAPPEARS because parse_pattern now calls it. Good.
//   * get_escaped_char's only caller is pattern_char_matches (P1.M3.T2.S1), so
//     AFTER S2 the build emits EXACTLY ONE warning: 'get_escaped_char defined
//     but not used'. This is EXPECTED and self-resolves in P1.M3.T2.S1.
//     Forward declarations do NOT suppress it. Do NOT use __attribute__((unused))
//     (deviates from the reference; not this codebase's idiom). The S2 build gate
//     is: exit 0, sole permitted warning = get_escaped_char unused.

// GOTCHA — even-backslash-count rule for the trailing '$' (PRD §7.3):
//   Count CONSECUTIVE backslashes immediately before the final '$'. EVEN (0,2,4)
//   => real end anchor (drop it); ODD (1,3) => escaped (keep in core, becomes
//   0x02 via process_escapes). Examples (raw bytes):
//     "abc$"    : 0 backslashes => end anchor,  core="abc"
//     "abc\\$"  : 1 backslash   => escaped $,    core="abc"+0x02
//     "abc\\\\$": 2 backslashes => end anchor,   core="abc"+0x04 (the \\ -> 0x04)

// GOTCHA — the `end > start` guard BEFORE checking the trailing '$':
//   without it, a lone "^" (start anchor only) would be misread. After skipping
//   '^', start==end so the end-anchor block is skipped => start_anchored only.
//   "^$" still works: after skipping '^', start points at '$', end is one past
//   it, end>start holds => end anchor detected, empty core (PRD §15 '^$' row).

// GOTCHA — two malloc-failure / process_escapes-failure fallbacks (both verified):
//   (1) core temp malloc fails  => core_pattern = pattern (raw), processed_pattern = NULL.
//   (2) process_escapes returns NULL => core_pattern = pattern (raw).
//   In BOTH cases free_parsed_pattern must be a NO-OP (processed_pattern NULL) and
//   must NEVER free `pattern` (it belongs to the caller). The `if (parsed &&
//   parsed->processed_pattern)` guard guarantees this.

// GOTCHA — free_parsed_pattern NULLs BOTH pointers after free. core_pattern points
//   INTO processed_pattern (or at the caller's raw pattern on fallback), so leaving
//   it set after free would be a dangling pointer. NULL it.

// GOTCHA — strncpy needs <string.h> (present from S1). Do NOT add <stdio.h>,
//   <ctype.h>, <stdint.h>, <stddef.h>, or "notifier.h" — none are used by S2's
//   functions; they belong to P1.M2/P1.M3. Adding them now is scope creep.

// GOTCHA — the match_with_anchors STUB must cast its params to void:
//   `(void)parsed; (void)str; (void)case_sensitive;` to stay -Wall -Wextra clean.

// GOTCHA — do NOT run run_all_tests.sh to validate S2. The real matcher
//   (match_with_anchors) is P1.M3.T2.S2; with the stub, pattern_match returns
//   false for everything, so every true-expecting case FAILS. Validate S2 with
//   the #include harness (Level 2) + a single-suite LINK check (Level 3).
```

## Implementation Blueprint

### Data models and structure

One new file-scope typedef (the struct is owned by parse_pattern and consumed by
match_with_anchors / free_parsed_pattern):

```c
typedef struct {
    const char *core_pattern;      /* points into processed_pattern, or the raw pattern on fallback */
    bool        start_anchored;    /* true if the original pattern began with '^' */
    bool        end_anchored;      /* true if the original pattern ended with an unescaped '$' */
    char       *processed_pattern; /* malloc'd by process_escapes(); freed by free_parsed_pattern() */
} parsed_pattern_t;
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: APPEND the typedef + forward-declaration block to pattern_match.c
  - PLACE: immediately AFTER the existing process_escapes() (end of current file).
  - ADD:
      1. the parsed_pattern_t typedef (above)
      2. forward declarations for the static helpers whose definitions may be
         ordered after a caller, and to make the cross-subtask boundary explicit:
             static char *process_escapes(const char *pattern);   /* defined above by S1 (redundant but harmless) */
             static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive);
  - PRESERVE: the S1 includes and process_escapes() body verbatim.
  - DO NOT add other forward decls unless a body is actually placed before its caller.

Task 2: APPEND get_escaped_char() (static) — exact reference body
  - SIGNATURE: static char get_escaped_char(char placeholder)
  - BODY: the switch below (verbatim from reference). Maps 0x01-0x04 to the
    escaped literals; 0x05-0x0D to debug/diagnostic chars; default returns the
    placeholder unchanged.
  - DEPENDENCIES: none (pure lookup). NOTE: unused until P1.M3.T2.S1
    (pattern_char_matches) => emits the one EXPECTED unused-function warning.

Task 3: APPEND free_parsed_pattern() (static) — exact reference body
  - SIGNATURE: static void free_parsed_pattern(parsed_pattern_t *parsed)
  - BODY: guard on `parsed && parsed->processed_pattern`; free(processed_pattern);
    set processed_pattern=NULL and core_pattern=NULL.
  - DEPENDENCIES: free() (stdlib.h, present). NULL-safe by design.

Task 4: APPEND parse_pattern() (static) with Mode-A docs — exact reference body
  - SIGNATURE: static parsed_pattern_t parse_pattern(const char *pattern)
  - DOC: inline comments documenting the EVEN-BACKSLASH-COUNT rule for the '$'
    anchor (item-spec §6, Mode A). Keep the reference logic verbatim; only ADD
    clarifying comments (comment drift is tolerated, PRD §17).
  - BODY: the reference logic below (start-anchor skip; end>start && trailing
    '$' with even backslash count => end anchor; carve core; malloc temp; strncpy;
    process_escapes(core); free temp; set core_pattern to processed result or raw
    fallback). Returns parsed_pattern_t by value.
  - DEPENDENCIES: process_escapes() (S1), strlen/strncpy/malloc/free (string/stdlib).

Task 5: APPEND the match_with_anchors() STUB (static) — NOT the reference body
  - SIGNATURE: static bool match_with_anchors(const parsed_pattern_t *parsed,
              const char *str, bool case_sensitive)
  - BODY: `(void)parsed; (void)str; (void)case_sensitive; return false;`
  - COMMENT: mark clearly as a STUB replaced by P1.M3.T2.S2.
  - DO NOT copy the reference's real match_with_anchors / match_string_with_start /
    match_reaches_end_with_start / nfa_match — those are P1.M2/P1.M3.

Task 6: APPEND pattern_match() (PUBLIC, non-static) — exact reference body
  - SIGNATURE: bool pattern_match(const char *pattern, const char *str, bool case_sensitive)
  - BODY: NULL-guard (`!pattern || !str` => false); parsed = parse_pattern(pattern);
    result = match_with_anchors(&parsed, str, case_sensitive);
    free_parsed_pattern(&parsed); return result;
  - DEPENDENCIES: parse_pattern (Task 4), match_with_anchors (Task 5 stub),
    free_parsed_pattern (Task 3). Ensure these are all defined/declared above it.
  - This is the ONLY non-static function; it matches pattern_match.h.
```

**The exact code to write** (verbatim from `git show HEAD:pattern_match.c`,
source of truth per PRD §17; only the Mode-A comments on parse_pattern and the
match_with_anchors stub are authored here — both behavior-neutral). Append this
block after S1's `process_escapes()`:

```c
/* ===== P1.M1.T2.S2: parse_pattern, free_parsed_pattern, get_escaped_char,
 *                     pattern_match (public), match_with_anchors STUB ===== */

/* Holds the result of parsing a user pattern: anchor flags + the
 * process_escapes()-processed core the NFA consumes. */
typedef struct {
    const char *core_pattern;    /* points into processed_pattern, or the raw pattern on malloc failure */
    bool        start_anchored;  /* true if the original pattern began with '^' */
    bool        end_anchored;    /* true if the original pattern ended with an unescaped '$' */
    char       *processed_pattern; /* malloc'd by process_escapes(); freed by free_parsed_pattern() */
} parsed_pattern_t;

/* match_with_anchors is fully implemented in P1.M3.T2.S2. Until then a STUB
 * returning false is provided (see below) so pattern_match() links and the
 * public API is exercised; real matching (and the passing suites) arrive with
 * P1.M3.T2.S2. */
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive);

/* Reverse-map a process_escapes() placeholder byte back to the human-readable
 * character. Used by pattern_char_matches() (P1.M3.T2.S1) for the escaped-literal
 * branch (0x01-0x04); the class/assertion/dot entries (0x05-0x0D) are kept for
 * debug/diagnostic readability. (Item-spec §3c, debug-only.) */
static char get_escaped_char(char placeholder) {
    switch (placeholder) {
        case '\x01': return '^';   /* \^ */
        case '\x02': return '$';   /* \$ */
        case '\x03': return '*';   /* \* */
        case '\x04': return '\\';  /* \\ */
        /* The following metacharacters have no single literal equivalent; they
         * represent classes/assertions handled directly in pattern_char_matches.
         * Returned here only for debug/error messages. */
        case '\x05': return 'd';   /* \d */
        case '\x06': return 'D';   /* \D */
        case '\x07': return 'w';   /* \w */
        case '\x08': return 'W';   /* \W */
        case '\x09': return 's';   /* \s */
        case '\x0A': return 'S';   /* \S */
        case '\x0B': return 'b';   /* \b */
        case '\x0C': return 'B';   /* \B */
        case '\x0D': return '.';   /* .  (dot metacharacter) */
        default:     return placeholder;  /* ordinary literal byte */
    }
}

/* Release the malloc'd processed-pattern buffer (if any) and NULL both pointers.
 * Safe on a zero-initialized struct or when parsed == NULL. On the malloc-failure
 * fallback path processed_pattern is NULL and core_pattern points at the CALLER's
 * pattern, so we must NOT free core_pattern — the `processed_pattern` guard
 * ensures we only ever free what we allocated. */
static void free_parsed_pattern(parsed_pattern_t *parsed) {
    if (parsed && parsed->processed_pattern) {
        free(parsed->processed_pattern);
        parsed->processed_pattern = NULL;
        parsed->core_pattern = NULL;  /* it aliased processed_pattern (or the caller's pattern) */
    }
}

/* Detect a leading '^' (start anchor) and a trailing UNESCAPED '$' (end anchor),
 * carve out the core substring between them, and process its escapes.
 *
 * EVEN-BACKSLASH-COUNT RULE for the end anchor (PRD §7.3, item-spec §6 Mode A):
 * A trailing '$' is a real end anchor ONLY when an EVEN number of backslashes
 * (including zero) immediately precede it. An ODD count means the '$' is escaped
 * and is part of the core (process_escapes turns '\$' into the 0x02 literal).
 *   "abc$"     : 0 backslashes => end anchor,  core = "abc"
 *   "abc\$"    : 1 backslash   => escaped '$',  core = "abc" + 0x02
 *   "abc\\$"   : 2 backslashes => end anchor,   core = "abc" + 0x04 (the '\\' -> 0x04)
 *   "abc\\\$"  : 3 backslashes => escaped '$',  core = "abc" + 0x04 + 0x02
 * This is the standard "is the final metacharacter quoted?" test: walk left from
 * the '$' counting consecutive '\'; even => unquoted.
 *
 * The `end > start` guard rejects degenerate inputs: a lone "^" (start anchor
 * only) leaves start==end after skipping '^', so no end check runs; "^$" still
 * detects both anchors with an empty core (PRD §15: '^$' matches the empty string). */
static parsed_pattern_t parse_pattern(const char *pattern) {
    parsed_pattern_t parsed = {0};   /* all flags false, all pointers NULL */

    if (!pattern) {
        return parsed;
    }

    const char *start = pattern;
    const char *end   = pattern + strlen(pattern);

    /* Start anchor: a leading '^' is always a start anchor (it cannot be an
     * escape target as the first char; '\^' would be processed to 0x01 later). */
    if (*start == '^') {
        parsed.start_anchored = true;
        start++;                     /* skip the '^' */
    }

    /* End anchor: trailing '$' that is NOT escaped (even backslash count). */
    if (end > start && *(end - 1) == '$') {
        int backslash_count = 0;
        const char *check = end - 2;
        while (check >= start && *check == '\\') {
            backslash_count++;
            check--;
        }
        if (backslash_count % 2 == 0) {   /* even (0,2,4,...) => unescaped '$' */
            parsed.end_anchored = true;
            end--;                          /* drop the '$' */
        }
    }

    /* Carve the core (between anchors) and process its escapes. */
    size_t core_len = (size_t)(end - start);
    char *core_pattern = malloc(core_len + 1);
    if (!core_pattern) {
        /* malloc failure: fall back to the raw pattern, no escape processing.
         * processed_pattern stays NULL => free_parsed_pattern() is a no-op. */
        parsed.core_pattern      = pattern;
        parsed.processed_pattern = NULL;
        return parsed;
    }
    strncpy(core_pattern, start, core_len);
    core_pattern[core_len] = '\0';

    parsed.processed_pattern = process_escapes(core_pattern);
    free(core_pattern);              /* temp copy no longer needed */

    if (parsed.processed_pattern) {
        parsed.core_pattern = parsed.processed_pattern;
    } else {
        /* process_escapes failed (its own malloc): fall back to the raw pattern. */
        parsed.core_pattern = pattern;
    }

    return parsed;
}

/* STUB — replaced by P1.M3.T2.S2. Returns false so pattern_match() is safe to
 * link and call today; the real anchor-aware NFA matching (and the passing test
 * suites) arrive with P1.M3.T2.S2. Item-spec §5 explicitly permits this stub. */
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive) {
    (void)parsed;
    (void)str;
    (void)case_sensitive;
    return false;
}

/* ===== PUBLIC API (declared in pattern_match.h) =====
 * NULL-guard -> parse -> match -> free. Caller frees nothing (PRD §6). */
bool pattern_match(const char *pattern, const char *str, bool case_sensitive) {
    if (!pattern || !str) {
        return false;
    }

    parsed_pattern_t parsed = parse_pattern(pattern);

    bool result = match_with_anchors(&parsed, str, case_sensitive);

    free_parsed_pattern(&parsed);

    return result;
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: parse_pattern returns the struct BY VALUE; zero-init with {0} so a
//   NULL input or any early return yields a clean {NULL,false,false,NULL}.

// PATTERN: the even-backslash-count walk uses a raw `const char *check` pointer
//   scanning LEFT from end-2 while *check=='\\'. `check >= start` bounds it so
//   we never read before the (already start-anchor-stripped) core. This is the
//   ONLY correct way to decide if the final '$' is escaped.

// PATTERN: two-layer fallback. (1) temp-core malloc fails -> raw pattern.
//   (2) process_escapes returns NULL -> raw pattern. Both leave processed_pattern
//   NULL so free_parsed_pattern is a no-op and the caller's pattern is never freed.

// PATTERN: free_parsed_pattern frees processed_pattern and NULLs BOTH pointers.
//   It MUST NOT free core_pattern (it aliases processed_pattern or the caller's
//   pattern). The `if (parsed && parsed->processed_pattern)` guard is the safety.

// ANTI-PATTERN: do NOT make match_with_anchors real here (NFA is P1.M2/P1.M3).
//   The stub returns false; do not call nfa_match / classifiers (they don't exist yet).

// ANTI-PATTERN: do NOT add get_escaped_char/parse_pattern/etc. to pattern_match.h.
//   They are static/internal; the header exposes only pattern_match (P1.M1.T1.S1).

// ANTI-PATTERN: do NOT suppress the get_escaped_char unused-function warning with
//   __attribute__((unused)); accept it (it self-resolves in P1.M3.T2.S1).

// ANTI-PATTERN: do NOT add new includes. stdbool/string/stdlib (already present)
//   cover everything S2 uses.
```

### Integration Points

```yaml
OWNERSHIP / MEMORY:
  - parse_pattern() allocates ONE buffer (via process_escapes) per call and stores
    it in parsed.processed_pattern; parsed.core_pattern aliases it (or the raw
    pattern on fallback). free_parsed_pattern() is the sole freer.
  - pattern_match() always pairs parse_pattern() with free_parsed_pattern() — no
    leak path (the contract test_memory_stress will enforce once it links).

CONSUMERS (downstream, NOT this task):
  - pattern_match() <- notifier.c match_pattern() (P2), host test suites (P1.M4).
  - get_escaped_char() <- pattern_char_matches() (P1.M3.T2.S1).
  - parsed_pattern_t <- match_with_anchors() (P1.M3.T2.S2 — replaces the STUB).

BUILD:
  - No build-system change. Plain gcc (run_all_tests.sh style). S2 validates with
    a temporary #include harness (helpers are static) + a single-suite LINK check.
  - The committed suites only call the public pattern_match(); they LINK after S2
    but will not PASS until P1.M3.T2.S2 (stub returns false).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module).
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. The static helpers are reached via
> `#include "pattern_match.c"` in a throwaway harness (the committed suites only
> call the public `pattern_match()`).

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile the (now S2-extended) pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o
# Expected: exit 0. EXACTLY ONE warning is permitted and expected:
#   warning: 'get_escaped_char' defined but not used [-Wunused-function]
# (It self-resolves when P1.M3.T2.S1 adds pattern_char_matches.)
# IMPORTANT: 'process_escapes' must NOT be in the warning list anymore — parse_pattern
# now calls it. If process_escapes still warns, parse_pattern is not wired correctly.

# 1b. Confirm the public symbol is non-static and the helpers are static.
grep -nE '^(bool|static .*) (pattern_match|parse_pattern|free_parsed_pattern|get_escaped_char|match_with_anchors)\(' pattern_match.c
# Expected:
#   static char  get_escaped_char(...)
#   static void  free_parsed_pattern(...)
#   static parsed_pattern_t parse_pattern(...)
#   static bool  match_with_anchors(...)
#   bool pattern_match(...)            <- PUBLIC (no 'static')

# 1c. Confirm NO new includes were added beyond S1's three.
grep -nE '^#include' pattern_match.c
# Expected: exactly <stdbool.h>, <string.h>, <stdlib.h> (the S1 set).

rm -f /tmp/pm.o
```

### Level 2: Component Tests (THE PRIMARY GATE)

This harness was **verified against the source-of-truth reference** during
research (all cases pass; the get_escaped_char map, the 14 parse_pattern truth
rows, free-safety, and the NULL-guard all confirmed). Create it, run it, require
all-pass.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/s2_test.c <<'EOF'
/* Reach the static helpers by including the .c directly. */
#include "pattern_match.c"
#include <stdio.h>
#include <string.h>

static int failures = 0;

/* Verify parse_pattern anchor flags + processed core bytes. */
static void ck_parse(const char *desc, const char *in,
                     int exp_start, int exp_end,
                     const unsigned char *exp_core, int explen) {
    parsed_pattern_t p = parse_pattern(in);
    int cl = p.core_pattern ? (int)strlen(p.core_pattern) : -1;
    int bad = (p.start_anchored != exp_start) || (p.end_anchored != exp_end) ||
              (cl != explen) || (cl >= 0 && memcmp(p.core_pattern, exp_core, cl) != 0);
    if (bad) {
        printf("FAIL %-10s want s=%d e=%d core(%d)=", desc, exp_start, exp_end, explen);
        for (int i = 0; i < explen; i++) printf(" %02X", exp_core[i]);
        printf(" | got s=%d e=%d core(%d)=", p.start_anchored, p.end_anchored, cl);
        if (p.core_pattern) for (int i = 0; i < cl; i++) printf(" %02X", (unsigned char)p.core_pattern[i]);
        else printf(" NULL");
        printf("\n");
        failures++;
    } else {
        printf("ok   %-10s\n", desc);
    }
    free_parsed_pattern(&p);
}

int main(void) {
    /* get_escaped_char reverse map */
    struct { char in; char out; } gm[] = {
        {'\x01','^'},{'\x02','$'},{'\x03','*'},{'\x04','\\'},{'\x05','d'},
        {'\x06','D'},{'\x07','w'},{'\x08','W'},{'\x09','s'},{'\x0A','S'},
        {'\x0B','b'},{'\x0C','B'},{'\x0D','.'},{'Q','Q'}  /* default passthrough */
    };
    for (int i = 0; i < (int)(sizeof(gm)/sizeof(gm[0])); i++) {
        char g = get_escaped_char(gm[i].in);
        if (g != gm[i].out) { printf("FAIL getc %02X -> %c (want %c)\n", (unsigned char)gm[i].in, g, gm[i].out); failures++; }
        else printf("ok   getc %02X -> %c\n", (unsigned char)gm[i].in, g);
    }

    /* parse_pattern truth table (verified against reference) */
    ck_parse("abc",      "abc",         0,0,(const unsigned char*)"\x61\x62\x63",3);
    ck_parse("^abc",     "^abc",        1,0,(const unsigned char*)"\x61\x62\x63",3);
    ck_parse("abc$",     "abc$",        0,1,(const unsigned char*)"\x61\x62\x63",3);
    ck_parse("^abc$",    "^abc$",       1,1,(const unsigned char*)"\x61\x62\x63",3);
    ck_parse("^$",       "^$",          1,1,(const unsigned char*)"",           0);
    ck_parse("$",        "$",           0,1,(const unsigned char*)"",           0);
    ck_parse("^",        "^",           1,0,(const unsigned char*)"",           0);
    ck_parse("empty",    "",            0,0,(const unsigned char*)"",           0);
    ck_parse("abc\\$",   "abc\\$",      0,0,(const unsigned char*)"\x61\x62\x63\x02",4); /* 1 bs => escaped $ */
    ck_parse("abc\\\\$","abc\\\\$",     0,1,(const unsigned char*)"\x61\x62\x63\x04",4); /* 2 bs => real $ */
    ck_parse("abc\\\\\\$","abc\\\\\\$", 0,0,(const unsigned char*)"\x61\x62\x63\x04\x02",5); /* 3 bs => escaped $ */
    ck_parse("\\^",      "\\^",         0,0,(const unsigned char*)"\x01",1);
    ck_parse("\\$",      "\\$",         0,0,(const unsigned char*)"\x02",1);
    ck_parse("a*b",      "a*b",         0,0,(const unsigned char*)"\x61\x2A\x62",3);     /* bare * -> 0x2A */

    /* free_parsed_pattern NULL-safety */
    parsed_pattern_t empty = {0};
    free_parsed_pattern(&empty);          /* processed_pattern NULL => no-op */
    free_parsed_pattern(NULL);            /* parsed NULL => no-op */
    printf("ok   free NULL-safety\n");

    /* after free, both pointers are NULL */
    parsed_pattern_t p = parse_pattern("xyz");
    free_parsed_pattern(&p);
    if (p.processed_pattern != NULL || p.core_pattern != NULL) { printf("FAIL post-free NULL\n"); failures++; }
    else printf("ok   post-free NULL\n");

    /* pattern_match NULL-guard (public API) */
    if (pattern_match(NULL, "x", true))  { printf("FAIL pm(NULL,...)\n"); failures++; } else printf("ok   pm(NULL,...)=false\n");
    if (pattern_match("x", NULL, true))  { printf("FAIL pm(...,NULL)\n"); failures++; } else printf("ok   pm(...,NULL)=false\n");
    if (pattern_match(NULL, NULL, true)) { printf("FAIL pm(NULL,NULL)\n"); failures++; } else printf("ok   pm(NULL,NULL)=false\n");
    /* non-NULL with the STUB returns false (temporary; real matching is P1.M3.T2.S2) */
    if (pattern_match("x", "y", true))   { printf("FAIL pm stub should be false\n"); failures++; } else printf("ok   pm stub=false\n");

    printf("\n%s (%d failures)\n", failures ? "SOME FAILURES" : "ALL CASES CONFIRMED", failures);
    return failures ? 1 : 0;
}
EOF

gcc -Wall -Wextra -I. /tmp/s2_test.c -o /tmp/s2_test && /tmp/s2_test
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
# (The only permitted compiler warning is the expected get_escaped_char unused one.)

# Repeat alloc/free stress (mirrors the spirit of test_memory_stress for this stage).
cat > /tmp/s2_leak.c <<'EOF'
#include "pattern_match.c"
int main(void){
    for (int i = 0; i < 200000; i++) (void)pattern_match("a\\db^wc\\.x+\\z$", "anything", 1);
    printf("200000 parse/free cycles: no crash\n");
    return 0;
}
EOF
gcc -Wall -Wextra -I. /tmp/s2_leak.c -o /tmp/s2_leak && /tmp/s2_leak
# Expected: "200000 parse/free cycles: no crash", exit 0.

rm -f /tmp/s2_test.c /tmp/s2_test /tmp/s2_leak.c /tmp/s2_leak
```

### Level 3: Integration Testing (API Integrity — LINK, not run)

```bash
cd /home/dustin/projects/qmk-notifier

# The committed suites call ONLY the public pattern_match(). After S2 the symbol
# exists, so they LINK cleanly — proving the public API is intact. They will NOT
# pass (the stub returns false); that is expected and is fixed by P1.M3.T2.S2.
gcc -Wall test_metachar_verification.c pattern_match.c -o /tmp/tm 2>&1 | grep -v 'get_escaped_char'
# Expected: empty output (only the known get_escaped_char warning, filtered above).
echo "link exit (expect 0): $?"
rm -f /tmp/tm

# DO NOT run run_all_tests.sh to validate S2 — it will report mass failures by
# design (real matcher = P1.M3.T2.S2). Syntax-check the whole TU instead:
gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c
# Expected: exit 0 (-fsyntax-only does not emit the unused-function warning).
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract check (item-spec §6 Mode A): parse_pattern documents the
# even-backslash-count rule for the '$' anchor.
grep -nE 'even|backslash|anchor' pattern_match.c | grep -i parse -A2 || true
# (Manual eyeball: confirm an inline comment near the end-anchor block explains
#  the even-backslash-count rule. The block is present per Implementation Task 4.)

# Self-containment: parse_pattern may call process_escapes (S1) only; it must NOT
# call NFA/classifier/anchor helpers (those are later subtasks / the stub).
awk '/static parsed_pattern_t parse_pattern\(/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -nE 'nfa_|is_digit_char|is_word_char|is_whitespace_char|is_word_boundary|pattern_char_matches|match_string_with_start|match_reaches_end_with_start' \
  && { echo "ERROR: parse_pattern calls a not-yet-built helper"; exit 1; } \
  || echo "parse_pattern depends only on process_escapes (good)"

# pattern_match depends ONLY on parse_pattern + match_with_anchors + free_parsed_pattern.
awk '/^bool pattern_match\(/{f=1} f&&/^}/{exit} f' pattern_match.c \
  | grep -nE 'nfa_|is_|pattern_char_matches|match_string_with_start|match_reaches_end_with_start' \
  && { echo "ERROR: pattern_match calls a not-yet-built helper"; exit 1; } \
  || echo "pattern_match wiring is correct (good)"

# Semantics cross-check vs PRD §15: '^$' must yield both anchors + empty core
# (covered by Level 2 'ck_parse("^$", ...)'); 'abc$' end-anchor-only; '^abc'
# start-anchor-only. (These rows are encoded in the Level 2 truth table.)
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0; sole
      warning is `get_escaped_char defined but not used` (process_escapes no longer warns).
- [ ] Level 2: `/tmp/s2_test` prints "ALL CASES CONFIRMED (0 failures)".
- [ ] Level 2: `/tmp/s2_leak` completes 200000 parse/free cycles with no crash.
- [ ] Level 3: `test_metachar_verification.c pattern_match.c` LINKS cleanly.
- [ ] Level 3: `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0.
- [ ] Level 4: parse_pattern comments document the even-backslash rule; parse_pattern
      and pattern_match depend only on their declared helpers.

### Feature Validation

- [ ] Signatures exact: `static char get_escaped_char(char)`,
      `static void free_parsed_pattern(parsed_pattern_t *)`,
      `static parsed_pattern_t parse_pattern(const char *)`,
      `static bool match_with_anchors(const parsed_pattern_t *, const char *, bool)`,
      `bool pattern_match(const char *, const char *, bool)`.
- [ ] All 14 parse_pattern truth cases pass (every anchor/escape combo, incl. `^$`,
      lone `$`, lone `^`, empty, and the 0/1/2/3-backslash `$` variants).
- [ ] get_escaped_char maps 0x01–0x0D correctly; default passthrough.
- [ ] free_parsed_pattern is NULL-safe (parsed NULL and processed_pattern NULL)
      and NULLs both pointers after free.
- [ ] pattern_match returns false for NULL pattern and/or NULL str; false for
      non-NULL under the stub.

### Code Quality Validation

- [ ] Matches the reference implementation byte-for-behavior (source of truth, §17).
- [ ] No new `#include`s beyond S1's stdbool/string/stdlib.
- [ ] No calls to NFA / classifier / real-match helpers (those are P1.M2/P1.M3).
- [ ] No modification to `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`,
      `PRD.md`, `tasks.json`, `prd_snapshot.md`, `.gitignore`.

### Documentation & Deployment

- [ ] Mode-A inline comments on parse_pattern explain the even-backslash-count rule.
- [ ] match_with_anchors stub is clearly marked as temporary (replaced by P1.M3.T2.S2).
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't overwrite `pattern_match.c` — APPEND to S1's `process_escapes()`; preserve
  the includes and that function verbatim.
- ❌ Don't implement the REAL `match_with_anchors` here (NFA = P1.M2, anchors = P1.M3).
  Use the stub returning false.
- ❌ Don't run `run_all_tests.sh` to validate S2 — the stub makes every true-case fail;
  validate with the Level-2 `#include` harness + a Level-3 LINK check instead.
- ❌ Don't suppress the `get_escaped_char` unused warning with `__attribute__((unused))`;
  accept it (self-resolves in P1.M3.T2.S1).
- ❌ Don't add the static helpers to `pattern_match.h` — only `pattern_match` is public.
- ❌ Don't add new includes (notifier.h/ctype.h/stdio.h/stdint.h/stddef.h) — unused by S2.
- ❌ Don't free `core_pattern` in `free_parsed_pattern` — it aliases `processed_pattern`
  or the caller's raw pattern; only `processed_pattern` is owned.
- ❌ Don't drop the `end > start` guard in parse_pattern — it prevents misreading a lone
  `^` and is required for `^$` to work.
- ❌ Don't change the even-backslash-count logic — odd ⇒ escaped `$`, even ⇒ anchor.
- ❌ Don't touch `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
  `tasks.json`, `prd_snapshot.md`, or `.gitignore`.

---

## Confidence Score: 10/10

The exact implementation is the live source of truth (`git show HEAD:pattern_match.c`,
PRD §17) and is reproduced verbatim above (only Mode-A comments on parse_pattern and
the match_with_anchors stub are authored here, both behavior-neutral). The complete
14-case `parse_pattern` truth table, the `get_escaped_char` map, `free_parsed_pattern`
NULL-safety, and the `pattern_match` NULL-guard were **executed against the reference
during research and passed with zero failures**. The `-Wall -Wextra -c` warning set
(exactly one expected `get_escaped_char` warning; `process_escapes` warning gone) and
the clean LINK of a real test suite were **empirically confirmed** by building the
realistic merged file. Dependencies on S1 (`process_escapes`) and the boundaries with
P1.M2/P1.M3 are explicit. No external dependencies are needed (libc only).
