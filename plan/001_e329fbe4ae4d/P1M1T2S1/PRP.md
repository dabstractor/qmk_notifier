# PRP — P1.M1.T2.S1: Implement `process_escapes()` — escape/metachar processor

## Goal

**Feature Goal**: Implement the **static** `process_escapes()` function inside
`pattern_match.c` — the byte-level escape/metacharacter processor that converts a
human pattern (`\d`, `\b`, `.`, `a+`, `\^`, …) into a NUL-terminated string of
control-byte placeholders and literals that the Thompson NFA consumes. This is
the **single `malloc` per `pattern_match` call** (freed later by
`free_parsed_pattern`).

**Deliverable**:
1. Create the rebuild target **`pattern_match.c`** (it does not exist as a
   rebuild artifact yet — see "Rebuild model" below), seeded with the **minimal
   include block** plus the complete `process_escapes()` function, including
   **inline comments documenting the `0x01`–`0x0E` placeholder-byte mapping** and
   referencing **PRD §7.1** (Mode A docs, per item spec §5).
2. The function must reproduce the live source-of-truth behavior **byte-for-byte**
   (the existing `pattern_match.c` is authoritative, PRD §17).

**Success Definition**:
- `process_escapes()` exists in `pattern_match.c` as a `static` function with the
  exact signature `static char *process_escapes(const char *pattern)`.
- A temporary `#include "pattern_match.c"` test harness passes **all 36 truth
  cases** in the table below (NULL, empty, every escape class, bare `*`/`+`/`.`,
  `+`-quantifier logic, unrecognized escapes, trailing lone `\`, length invariant).
- `gcc -Wall -Wextra` on the harness produces **zero warnings/errors**.
- No other file is modified (header, tests, notifier.*, rules.mk untouched).

## User Persona (if applicable)

**Target User**: The matcher itself — `process_escapes()` is called **only** by
`parse_pattern()` (task P1.M1.T2.S2), which carves out the anchored "core" and
feeds it here. End users never call it directly.

**Use Case**: Transform a core pattern like `sear*term` or `\bword\b` into the
internal placeholder stream (`…\x2A…`, `\x0Bword\x0B`) that `nfa_compile` turns
into States.

**User Journey** (within the eventual full matcher):
`pattern_match()` → `parse_pattern()` strips `^`/`$` → **`process_escapes(core)`**
→ `nfa_compile(processed)` → `nfa_match()` → `free_parsed_pattern()` frees this
buffer.

**Pain Points Addressed**: Gives the NFA a single, unambiguous internal alphabet
(separating "literal `*`" 0x2A vs "escaped `\*`" 0x03 vs nothing-else) so the
matcher never has to re-parse escapes, and supports the one-or-more quantifier
(`+`) in linear time without backtracking.

## Why

- **Foundation of the matcher**: every consuming element/class/assertion the NFA
  matches is *created* here. Getting a single byte wrong silently breaks matching.
- **Memory contract**: this is the ONLY heap allocation on the match path
  (the NFA state pool is stack-allocated — see architecture "Memory Model").
  Getting the `malloc` size wrong = buffer overflow; leaking it = `test_memory_stress` failure.
- **Rebuild integrity**: this is the first `.c` subtask; it seeds `pattern_match.c`
  for all later subtasks (S2, P1.M2, P1.M3) to append to.

## What

A `static` function in `pattern_match.c` that:
1. Returns `NULL` for `NULL` input.
2. `malloc(strlen(pattern) + 1)` (output is never longer than input — see Gotchas).
3. Walks the source byte-by-byte maintaining `bool last_consumable`, emitting the
   placeholder bytes per the **§7.1 contract table** below.
4. Returns the malloc'd, NUL-terminated result; caller owns and frees it.

### Success Criteria

- [ ] `process_escapes()` present in `pattern_match.c`, signature exactly
      `static char *process_escapes(const char *pattern)`.
- [ ] All 36 truth-table cases pass (Validation Level 2).
- [ ] Inline comments document the `0x01`–`0x0E` mapping and cite PRD §7.1.
- [ ] `gcc -Wall -Wextra` clean on the harness.
- [ ] `pattern_match.c` includes only `<stdbool.h>`, `<string.h>`, `<stdlib.h>`
      at this stage (plus the function). No `notifier.h`, no other helpers yet.

## All Needed Context

### Context Completeness Check

**Pass.** The exact implementation is the live source of truth (existing
`pattern_match.c`, PRD §17) and is reproduced verbatim in "Implementation Tasks"
below. The complete expected-output truth table (36 cases) was **verified against
the reference** during research and is embedded in the Validation section. An
implementer with only this PRP + repo access can produce the function
byte-identically and prove it.

### Documentation & References

```yaml
# MUST READ — authoritative spec
- file: PRD.md
  section: "### 7.1 The processed-pattern byte contract (what the NFA consumes)"
  why: "Defines EVERY output byte the function may emit and its width. This is
        the contract the implementation + tests must match."
  critical: "Output bytes are a CLOSED SET: 0x01-0x0E placeholders, 0x2A (bare *),
             0x2E (\\. literal dot), 0x2B (\\+ / non-quant bare +), ordinary chars,
             and 0x00 terminator. Anything else is a bug."

- file: PRD.md
  section: "### 7.2 Escape & metacharacter processing rules (process_escapes)"
  why: "The step-by-step logic: the last_consumable flag, every \\X case, bare
        */+/.  handling, unrecognized escapes, trailing lone backslash."
  critical: "Two non-obvious rules: (1) \\b/\\B set last_consumable=FALSE (zero-width);
             (2) bare '+' emits 0x0E ONLY when last_consumable is true, else literal '+'."

- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## Processed-Pattern Byte Contract" and "### process_escapes Logic"
  why: "Architecture-level restatement of the same contract + the memory model
        ('One malloc per pattern_match call ... freed by free_parsed_pattern')."
  critical: "Confirms the single-malloc / free_parsed_pattern ownership boundary
             and that this function is self-contained (no calls to other statics)."

- file: plan/001_e329fbe4ae4d/P1M1T1S1/PRP.md
  section: "## Goal" / "Implementation Tasks"
  why: "Defines pattern_match.h — the ONLY public symbol is pattern_match().
        process_escapes is NOT declared there (it is static/private in the .c)."
  critical: "Do NOT add process_escapes to the header; do NOT modify the header."

- file: PRD.md
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"
  why: "'the code + the passing tests win' — the existing pattern_match.c is
        authoritative; reproduce process_escapes byte-for-byte."
  critical: "Tolerates line/order drift; correctness = passing tests, not file order."

# Build convention
- file: run_all_tests.sh
  why: "Shows the toolchain: plain gcc, no make. Tests link pattern_match.c and
        call only the public pattern_match() — they never call process_escapes
        directly (it is static)."
  critical: "Because process_escapes is static, THIS task validates it with a
             temporary #include harness, NOT by adding a committed test file."

# Semantics cross-check (do not contradict)
- file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "Rows like '\\x → \\x (unknown escape kept literal, 2 bytes)', 'a\\+b → a+b',
        'v\\.code → v.code' confirm the unrecognized-escape and \\./\\+ rules."
  critical: "The '\\x kept literal (2 bytes)' row is the spec basis for the
             unrecognized-escape → backslash+char behavior."

# External (informational; no dependency added)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast' — already
        cited in the reference pattern_match.c. Relevant to the NFA engine
        (P1.M2); process_escapes itself needs only libc malloc/strlen."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # created by P1.M1.T1.S1 — public contract (pattern_match only). DO NOT TOUCH here.
pattern_match.c        # EXISTS as reference/source-of-truth (PRD §17). This task
                       #   creates the REBUILD target. See "Rebuild model" note.
notifier.h notifier.c  # P2 scope — do not touch.
rules.mk               # P2 scope — do not touch.
test_*.c               # P1.M4 scope — do not touch. They #include "pattern_match.h".
run_all_tests.sh       # P1.M4 scope — do not touch.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — only write your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # CREATED (seeded) by THIS task. Contains:
                       #   - minimal includes (stdbool/string/stdlib)
                       #   - process_escapes() static function + inline doc
                       # Later subtasks APPEND to this file:
                       #   P1.M1.T2.S2 → parse_pattern, free_parsed_pattern,
                       #                  pattern_match, get_escaped_char
                       #   P1.M2       → NFA (State, nfa_compile, nfa_match, ...)
                       #   P1.M3       → char classifiers, pattern_char_matches,
                       #                  match_with_anchors
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — Rebuild model:
//   The working tree already contains a full pattern_match.c (the source of truth,
//   PRD §17). This task recreates the REBUILD TARGET pattern_match.c incrementally:
//   create the file with ONLY the minimal includes + process_escapes(). Reproduce
//   the function byte-for-byte from the reference. Do NOT copy the rest of the
//   reference file — later subtasks own those functions.
//   If a partially-built pattern_match.c already exists from a prior subtask run,
//   APPEND/INSERT process_escapes() into it instead of overwriting (preserve any
//   includes/functions already present; add only missing includes this function needs).

// CRITICAL — malloc size: malloc(strlen(pattern) + 1) is SAFE because output
//   length <= input length ALWAYS. Worst case is an unrecognized escape: 2 input
//   bytes (\,x) -> 2 output bytes (\,x). Every recognized \X collapses 2->1;
//   bare metachars and ordinary chars are 1->1. So len+1 covers output+NUL.
//   Do NOT "be safe" with a bigger buffer and do NOT shrink it.

// GOTCHA — \b and \B set last_consumable = FALSE (they are zero-width asserts).
//   This matters for a following bare '+': '\b+' -> [0x0B, 0x2B] (you CANNOT
//   quantify a boundary with +; the + becomes a literal). Getting this wrong
//   breaks the '+' branch.

// GOTCHA — bare '*': emit the LITERAL '*' byte (0x2A), NOT a placeholder, and set
//   last_consumable = FALSE. The matcher (nfa_compile) recognizes 0x2A specially.

// GOTCHA — bare '+': branch on last_consumable. TRUE -> 0x0E marker (set FALSE);
//   FALSE -> literal '+' 0x2B (set TRUE). Order of the if/else matters.

// GOTCHA — process_escapes is SELF-CONTAINED. It must NOT call get_escaped_char,
//   parse_pattern, or any NFA/classifier helper. (get_escaped_char is the REVERSE
//   mapping used by pattern_char_matches, a different task.)

// GOTCHA — 'size_t' comes from <string.h> (via strlen). Do not need <stddef.h>.
//   Do not include <ctype.h>/<stdio.h>/<stdint.h>/notifier.h here — none are used
//   by this function. Later subtasks add their own includes.

// GOTCHA — unrecognized escape (\x, \z, \q, ...): emit BACKSLASH then the char
//   (2 bytes), so e.g. "\x" matches the literal two-byte string \x. NOT an error.

// GOTCHA — trailing lone backslash (backslash then NUL): emit a single literal
//   backslash. The first `if (*src == '\\' && *(src+1))` is FALSE when *(src+1)
//   is NUL, so the dedicated `else if (*src == '\\' && *(src+1) == '\0')` arm
//   catches it. Do not merge these arms.
```

## Implementation Blueprint

### Data models and structure

None new. `process_escapes` takes/returns plain `char *` / `const char *`. The
`parsed_pattern_t` struct and `bool last_consumable` flag live at function scope
(`last_consumable`) or are owned by `parse_pattern` (the struct — task S2).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: CREATE (or seed) pattern_match.c  at repo root
  - IF pattern_match.c does NOT exist yet (typical — S1 is first .c subtask):
      CREATE it with, in order:
        1. a one-line file header comment: /* pattern_match.c — Thompson-NFA
             pattern matcher (rebuilt incrementally; see PRD §7). */
        2. #include <stdbool.h>
        3. #include <string.h>
        4. #include <stdlib.h>
        5. blank line
        6. the process_escapes() function from Task 2 below
  - IF a partially-built pattern_match.c ALREADY exists (re-run / ordering):
      INSERT process_escapes() into it (location is irrelevant — it is static and
      self-contained; forward declarations are added by S2). Ensure the three
      includes above are present (add any that are missing). PRESERVE all existing
      content; do not duplicate includes.
  - DO NOT add: notifier.h, stdio.h, ctype.h, stdint.h (unused by this function;
    later subtasks add what they need). DO NOT add other functions (parse_pattern,
    nfa_*, classifiers — those are other tasks).
  - PLACEMENT: repository root, beside pattern_match.h.

Task 2: WRITE process_escapes()  (the exact source-of-truth body, with Mode-A docs)
  - SIGNATURE: static char *process_escapes(const char *pattern)
  - DOC COMMENT: reproduce the reference comment block verbatim (it already
      documents 0x01-0x04 / 0x05-0x0A / 0x0B 0x0C / 0x0D / 0x0E — satisfying
      item-spec §5 "Mode A inline comments documenting the placeholder byte
      mapping"). Add one line referencing "PRD §7.1 byte contract".
  - BODY: reproduce EXACTLY (verified against reference; see block below).
  - DEPENDENCIES: none (self-contained; libc only).
  - NAMING: lowercase snake_case `process_escapes`, matching the reference and
      the forward-declaration that parse_pattern (S2) will rely on.
```

**The exact code to write** (verbatim from the reference `pattern_match.c`,
source of truth per PRD §17; body confirmed by the 36-case truth run):

```c
/*
 * Process escape sequences, the dot metacharacter, and the '+' quantifier in a
 * pattern. Uses control-byte placeholders for elements the matcher must treat
 * specially (see PRD §7.1 "processed-pattern byte contract"):
 *   \x01-\x04 : escaped literals ^ $ * \         \x05-\x0A : classes \d \D \w \W \s \S
 *   \x0B \x0C : zero-width \b \B                 \x0D      : dot metacharacter
 *   \x0E      : '+' quantifier (follows the element it quantifies)
 * A literal '.' (from \.) and literal '+' (from \+ or a bare '+' not following a
 * consumable element) are emitted as their ordinary ASCII bytes (0x2E / 0x2B);
 * a bare '*' is emitted as its ordinary byte 0x2A (the glob wildcard).
 *
 * This is the single malloc per pattern_match() call; the result is freed by
 * free_parsed_pattern(). Output length is always <= input length, so
 * malloc(strlen(pattern)+1) is sufficient.
 */
static char *process_escapes(const char *pattern) {
    if (!pattern) return NULL;

    size_t len = strlen(pattern);
    char *processed = malloc(len + 1);
    if (!processed) return NULL;

    const char *src = pattern;
    char *dst = processed;
    bool last_consumable = false;   /* did the previous emitted element consume a char? */

    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++;  /* skip the backslash */
            switch (*src) {
                case '^':  *dst++ = '\x01'; src++; last_consumable = true;  break;  /* \^ */
                case '$':  *dst++ = '\x02'; src++; last_consumable = true;  break;  /* \$ */
                case '*':  *dst++ = '\x03'; src++; last_consumable = true;  break;  /* \* */
                case '\\': *dst++ = '\x04'; src++; last_consumable = true;  break;  /* \\ */
                case '.':  *dst++ = '.';    src++; last_consumable = true;  break;  /* \.  -> literal dot  (0x2E) */
                case '+':  *dst++ = '+';    src++; last_consumable = true;  break;  /* \+  -> literal plus (0x2B) */
                case 'd':  *dst++ = '\x05'; src++; last_consumable = true;  break;  /* \d */
                case 'D':  *dst++ = '\x06'; src++; last_consumable = true;  break;  /* \D */
                case 'w':  *dst++ = '\x07'; src++; last_consumable = true;  break;  /* \w */
                case 'W':  *dst++ = '\x08'; src++; last_consumable = true;  break;  /* \W */
                case 's':  *dst++ = '\x09'; src++; last_consumable = true;  break;  /* \s */
                case 'S':  *dst++ = '\x0A'; src++; last_consumable = true;  break;  /* \S */
                case 'b':  *dst++ = '\x0B'; src++; last_consumable = false; break;  /* \b zero-width */
                case 'B':  *dst++ = '\x0C'; src++; last_consumable = false; break;  /* \B zero-width */
                default:   /* unrecognized escape: keep backslash + char literally (2 bytes) */
                    *dst++ = '\\'; *dst++ = *src++; last_consumable = true; break;
            }
        } else if (*src == '\\' && *(src + 1) == '\0') {
            *dst++ = *src++;        /* trailing lone backslash -> literal '\' */
            last_consumable = true;
        } else if (*src == '*') {
            *dst++ = '*'; src++;    /* bare '*' -> 0x2A glob wildcard (handled by nfa_compile) */
            last_consumable = false;
        } else if (*src == '+') {
            if (last_consumable) {
                *dst++ = '\x0E';    /* quantifier: one-or-more of the previous element */
                last_consumable = false;
            } else {
                *dst++ = '+';       /* literal '+' (not after a consumable element) */
                last_consumable = true;
            }
            src++;
        } else if (*src == '.') {
            *dst++ = '\x0D'; src++; /* bare '.' dot metacharacter */
            last_consumable = true;
        } else {
            *dst++ = *src++;        /* ordinary literal */
            last_consumable = true;
        }
    }

    *dst = '\0';
    return processed;
}
```

> **Note on `case '.':` / `case '+':` inside the switch:** these emit the
> *ordinary ASCII byte* (`0x2E` / `0x2B`), written as the char literal `'.'` /
> `'+'` for readability. They are NOT placeholders. This matches the reference
> exactly and the §7.1 table rows "`0x2E .` from `\.`" and "`0x2B +` from `\+`".

### Implementation Patterns & Key Details

```c
// PATTERN: single-pass byte rewrite with a running flag.
//   - Recognized \X collapses 2 input bytes -> 1 placeholder.
//   - Unrecognized \X preserves 2 bytes (so "\x" still matches "\x").
//   - Bare metachars (* . +) are 1->1; '+' is context-sensitive via last_consumable.

// PATTERN: malloc(len+1) where len = strlen(pattern). Safe because out_len <= in_len.
//   NEVER scan the output to size it; NEVER realloc. One malloc, one free (by S2).

// PATTERN: NULL guard FIRST (`if (!pattern) return NULL;`), then malloc, then
//   malloc-failure guard (`if (!processed) return NULL;`). Both NULL returns are
//   legal; callers (parse_pattern, S2) handle a NULL result by falling back to
//   the raw pattern.

// ANTI-PATTERN: do NOT call get_escaped_char() here. That function REVERSES the
//   placeholder mapping (0x01->'^') and is used only by pattern_char_matches
//   (P1.M3). process_escapes goes the OTHER direction and must be standalone.

// ANTI-PATTERN: do NOT merge the two backslash arms. The first arm requires a
//   NON-NUL char after '\'; the second arm specifically handles '\' then NUL
//   (trailing lone backslash). Merging would mis-handle "abc\".
```

### Integration Points

```yaml
OWNERSHIP / MEMORY:
  - process_escapes() returns a malloc'd buffer. Owner = parse_pattern (S2),
    which stores it in parsed.processed_pattern. Freer = free_parsed_pattern (S2).
  - THIS task does NOT implement the owner/freer; it only guarantees the malloc
    succeeds-or-returns-NULL contract that S2 depends on.

CONSUMERS (downstream, NOT this task):
  - parse_pattern() (S2): the sole caller.
  - nfa_compile() (P1.M2): consumes the placeholder bytes (0x2A, 0x0B/0x0C, 0x0E,
    consuming elements).
  - pattern_char_matches() (P1.M3): interprets 0x01-0x0D via get_escaped_char +
    the class helpers.

BUILD:
  - No build-system change. Plain gcc (run_all_tests.sh style). This task does
    not add a committed test file (process_escapes is static; committed suites
    cover it indirectly via pattern_match() once S2 lands). Validation uses a
    temporary #include harness (see Validation Loop).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module).
```

## Validation Loop

> NOTE: C project — no ruff/mypy/pytest. Use `gcc`. Because `process_escapes` is
> `static`, it is reached via `#include "pattern_match.c"` in a throwaway harness
> (the committed suites only call the public `pattern_match()`).

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. The new pattern_match.c must compile standalone as a translation unit.
#     (No main() yet — that's fine; -c just produces an object with a static fn.)
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o
# Expected: no output, exit 0, /tmp/pm.o created. (Warnings = fix before proceeding.)

# 1b. Confirm the function is static (private) and not accidentally non-static.
grep -nE 'char \*process_escapes\(' pattern_match.c
# Expected: a line beginning with "static char *process_escapes(".

# 1c. Confirm the minimal-include contract (no over-inclusion at this stage).
for h in stdbool.h string.h stdlib.h; do
  grep -q "#include <$h>" pattern_match.c && echo "has $h" || echo "MISSING $h"
done
# Expected: has stdbool.h / has string.h / has stdlib.h.
# (Extra includes added by later subtasks are fine; this just checks the 3 needed now exist.)

rm -f /tmp/pm.o
```

### Level 2: Byte-Level Unit Tests (Component Validation) — THE PRIMARY GATE

This harness was **verified against the source-of-truth reference** during
research (all 36 cases pass, zero `-Wall -Wextra` warnings). Create it, run it,
and require all-pass.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/pe_test.c <<'EOF'
/* Reach the static process_escapes() by including the .c directly. */
#include "pattern_match.c"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char *desc, const char *input,
                  const unsigned char *exp, int explen) {
    char *got = process_escapes(input);
    if (!got) { printf("FAIL %-26s -> NULL (exp %d bytes)\n", desc, explen); failures++; return; }
    int gotlen = (int)strlen(got);
    if (gotlen != explen || memcmp(got, exp, explen) != 0) {
        printf("FAIL %-26s -> got:", desc); for (int i=0;i<gotlen;i++) printf(" %02X",(unsigned char)got[i]);
        printf(" | exp:"); for (int i=0;i<explen;i++) printf(" %02X",exp[i]); printf("\n"); failures++;
    } else printf("ok   %-26s\n", desc);
    free(got);
}
static void check_null(const char *desc) {
    char *got = process_escapes(NULL);
    if (got) { printf("FAIL %-26s -> expected NULL\n", desc); failures++; free(got); }
    else printf("ok   %-26s\n", desc);
}

int main(void) {
    check_null("NULL");
    check("empty", "", (const unsigned char *)"", 0);
    check("abc", "abc", (const unsigned char *)"\x61\x62\x63", 3);
    /* escaped literals 0x01-0x04 */
    check("\\^", "\\^", (const unsigned char *)"\x01", 1);
    check("\\$", "\\$", (const unsigned char *)"\x02", 1);
    check("\\*", "\\*", (const unsigned char *)"\x03", 1);
    check("\\\\", "\\\\", (const unsigned char *)"\x04", 1);
    /* escaped literal dot/plus (ordinary bytes) */
    check("\\.", "\\.", (const unsigned char *)"\x2E", 1);
    check("\\+", "\\+", (const unsigned char *)"\x2B", 1);
    /* classes 0x05-0x0A */
    check("\\d","\\d",(const unsigned char *)"\x05",1); check("\\D","\\D",(const unsigned char *)"\x06",1);
    check("\\w","\\w",(const unsigned char *)"\x07",1); check("\\W","\\W",(const unsigned char *)"\x08",1);
    check("\\s","\\s",(const unsigned char *)"\x09",1); check("\\S","\\S",(const unsigned char *)"\x0A",1);
    /* zero-width asserts 0x0B/0x0C */
    check("\\b","\\b",(const unsigned char *)"\x0B",1); check("\\B","\\B",(const unsigned char *)"\x0C",1);
    check("\\b\\B","\\b\\B",(const unsigned char *)"\x0B\x0C",2);
    /* bare dot / star */
    check("bare .", ".", (const unsigned char *)"\x0D", 1);
    check("bare *", "*", (const unsigned char *)"\x2A", 1);
    /* '+' quantifier logic (the subtle part) */
    check("bare + alone", "+", (const unsigned char *)"\x2B", 1);
    check("a+", "a+", (const unsigned char *)"\x61\x0E", 2);
    check("++", "++", (const unsigned char *)"\x2B\x0E", 2);
    check("a++", "a++", (const unsigned char *)"\x61\x0E\x2B", 3);
    check("\\d+", "\\d+", (const unsigned char *)"\x05\x0E", 2);
    check(".+", ".+", (const unsigned char *)"\x0D\x0E", 2);
    check("*+", "*+", (const unsigned char *)"\x2A\x2B", 2);
    check("\\b+ (no quant)", "\\b+", (const unsigned char *)"\x0B\x2B", 2);
    check("a\\.+", "a\\.+", (const unsigned char *)"\x61\x2E\x0E", 3);
    /* unrecognized escapes: 2 bytes (backslash + char) */
    check("\\x", "\\x", (const unsigned char *)"\x5C\x78", 2);
    check("\\z", "\\z", (const unsigned char *)"\x5C\x7A", 2);
    /* trailing lone backslash */
    check("a\\ (trail)", "a\\", (const unsigned char *)"\x61\x5C", 2);
    check("\\ (lone)", "\\", (const unsigned char *)"\x5C", 1);
    /* mixed real-world */
    check("sear*term", "sear*term", (const unsigned char *)"\x73\x65\x61\x72\x2A\x74\x65\x72\x6D", 9);
    check("\\bword\\b", "\\bword\\b", (const unsigned char *)"\x0B\x77\x6F\x72\x64\x0B", 6);
    /* malloc-size invariant stress: 64x \d (128 in -> 64 out, fits len+1) */
    { char in[129]; unsigned char exp[64]; memset(in,0,sizeof(in));
      for (int i=0;i<64;i++){ in[2*i]='\\'; in[2*i+1]='d'; exp[i]=0x05; }
      check("64x \\d (len inv.)", in, exp, 64); }

    printf("\n%s (%d failures)\n", failures?"SOME FAILURES":"ALL TRUTH-CASES CONFIRMED", failures);
    return failures ? 1 : 0;
}
EOF

gcc -Wall -Wextra -I. /tmp/pe_test.c -o /tmp/pe_test && /tmp/pe_test
# Expected: 36 "ok" lines, then "ALL TRUTH-CASES CONFIRMED (0 failures)", exit 0.

# Also confirm no memory errors leak/crash under repeat alloc/free (mirrors the
# spirit of test_memory_stress, scoped to this function).
cat > /tmp/pe_leak.c <<'EOF'
#include "pattern_match.c"
#include <string.h>
int main(void){
    for (int i=0;i<200000;i++){ char *p=process_escapes("a\\db\\w*c\\.x+\\z");
        if(p){ size_t l=strlen(p); (void)l; free(p);} }
    printf("200000 alloc/free cycles: no crash\n");
    return 0;
}
EOF
gcc -Wall -Wextra -I. /tmp/pe_leak.c -o /tmp/pe_leak && /tmp/pe_leak
# Expected: "200000 alloc/free cycles: no crash", exit 0.

rm -f /tmp/pe_test.c /tmp/pe_test /tmp/pe_leak.c /tmp/pe_leak
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# Full end-to-end (pattern_match() public API) is NOT possible yet — it is
# implemented in P1.M1.T2.S2 and needs the NFA (P1.M2). So at THIS stage:
#   - Do NOT run run_all_tests.sh (it would fail to build pattern_match.c into
#     working tests, by design — only process_escapes exists).
#   - The committed suites will validate process_escapes INDIRECTLY once S2 +
#     P1.M2 land (e.g. test_metachar_verification.c exercises \d \w \s etc. via
#     pattern_match()).
#
# Integration readiness check: confirm pattern_match.c is syntactically valid as
# part of a larger build (it must not break when later functions are appended).
gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c
# Expected: no output, exit 0.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract check (item spec §5: Mode A inline comments document 0x01-0x0E
# and reference PRD §7.1).
for needle in '\x01-\x04' '\x05-\x0A' '\x0B \x0C' '\x0D' '\x0E' '7.1'; do
  grep -qF "$needle" pattern_match.c || { echo "MISSING doc token: $needle"; exit 1; }
done
echo "all byte-mapping doc tokens present"

# Confirm process_escapes does NOT depend on functions from other (not-yet-built)
# tasks: grep its body for forbidden calls.
awk '/static char \*process_escapes\(/{f=1} f&&/^}/{print; exit} f' pattern_match.c \
  | grep -nE 'get_escaped_char|parse_pattern|nfa_|is_digit_char|is_word_char|is_whitespace_char|is_word_boundary|pattern_char_matches|match_' \
  && { echo "ERROR: process_escapes calls another task's helper"; exit 1; } \
  || echo "process_escapes is self-contained (good)"

# Semantics cross-check vs PRD §15 rows (manual): the unrecognized-escape rule
# ('\x' kept literal, 2 bytes) and '\.'/'\+' literal rules implemented above must
# agree with PRD §15 rows '\x -> \x' and 'v\.code -> v.code' and 'a\+b -> a+b'.
grep -nE "default:.*unrecognized|case '\.'" pattern_match.c
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → clean, produces object.
- [ ] Level 2: `/tmp/pe_test` prints "ALL TRUTH-CASES CONFIRMED (0 failures)".
- [ ] Level 2: `/tmp/pe_leak` completes 200000 alloc/free cycles with no crash.
- [ ] Level 3: `gcc -Wall -Wextra -fsyntax-only pattern_match.c` → clean.
- [ ] Level 4: all byte-mapping doc tokens present; function is self-contained.

### Feature Validation

- [ ] Signature is exactly `static char *process_escapes(const char *pattern)`.
- [ ] NULL input → returns NULL.
- [ ] All 36 truth-table cases pass (every escape class, bare `*`/`+`/`.`, `+`
      quantifier logic incl. `\b+`, unrecognized escapes, trailing lone `\`).
- [ ] `malloc(strlen+1)` used (output never exceeds input length).
- [ ] Inline comments document `0x01`–`0x0E` and cite PRD §7.1.

### Code Quality Validation

- [ ] Matches the reference implementation byte-for-behavior (source of truth, §17).
- [ ] Only `<stdbool.h>`, `<string.h>`, `<stdlib.h>` added at this stage.
- [ ] No calls to other tasks' helpers (self-contained).
- [ ] No modification to `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`,
      `PRD.md`, `tasks.json`, `prd_snapshot.md`, `.gitignore`.

### Documentation & Deployment

- [ ] Mode-A inline doc comment present and references PRD §7.1.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't invent new placeholder bytes — the set is closed: `0x01`–`0x0E`, `0x2A`,
  `0x2E`, `0x2B`, ordinary chars, `0x00` (PRD §7.1).
- ❌ Don't make the buffer bigger "to be safe" — `malloc(strlen+1)` is provably
  sufficient and is the documented single-malloc contract.
- ❌ Don't set `last_consumable = true` for `\b`/`\B` — they are zero-width (false).
- ❌ Don't merge the "backslash + char" and "trailing lone backslash" arms.
- ❌ Don't call `get_escaped_char` / `parse_pattern` / NFA helpers here — this
  function must be standalone.
- ❌ Don't add `process_escapes` to `pattern_match.h` — it is `static`/private.
- ❌ Don't implement `parse_pattern`, `free_parsed_pattern`, `pattern_match`, or
  `get_escaped_char` here — those are P1.M1.T2.S2.
- ❌ Don't run `run_all_tests.sh` to validate this task — the full matcher isn't
  built yet (S2 + P1.M2 needed); use the Level-2 `#include` harness instead.
- ❌ Don't touch the existing reference `pattern_match.c` beyond creating the
  rebuild target with this one function (preserve anything a prior subtask added).

---

## Confidence Score: 10/10

The exact implementation is the live source of truth (PRD §17) and is reproduced
verbatim above. The complete 36-case expected-output truth table was **executed
against the reference `pattern_match.c` during research and passed with zero
warnings under `-Wall -Wextra`**, so the Validation section is ground-truth
verified, not guessed. The function is self-contained (libc only), the malloc
invariant is proven (`out_len ≤ in_len`), and scope boundaries with sibling
subtasks (S2, P1.M2, P1.M3) are explicit. No external dependencies are needed.
