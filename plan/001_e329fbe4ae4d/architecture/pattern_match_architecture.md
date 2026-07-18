# Pattern Matcher Architecture — pattern_match.c / pattern_match.h

## Overview

A Thompson-NFA regex engine supporting a small, fixed token set. The public API
is a single function:

```c
bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
```

It must be **non-crashing on any input** (NULL → false) and **linear-time**
(O(states × strlen), no catastrophic backtracking).

## Call Chain (Data Flow)

```
pattern_match(pattern, str, case_sensitive)              [PUBLIC API, line ~198]
 ├─ NULL guard: if (!pattern || !str) return false
 ├─ parsed = parse_pattern(pattern)                       [~line 365]
 │     ├─ detect leading ^  → parsed.start_anchored
 │     ├─ detect trailing $ (unescaped: even backslash count) → parsed.end_anchored
 │     ├─ carve out core = pattern[after ^ .. before $]
 │     └─ parsed.processed_pattern = process_escapes(core) [~line 301]
 │        parsed.core_pattern points at the processed bytes
 ├─ result = match_with_anchors(&parsed, str, cs)         [~line 222]
 └─ free_parsed_pattern(&parsed)                          [~line 426]
```

## Key Data Structures

### parsed_pattern_t (file-scope typedef)
```c
typedef struct {
    const char *core_pattern;    // Points to processed_pattern
    bool start_anchored;         // true if original started with ^
    bool end_anchored;           // true if original ended with $
    char *processed_pattern;     // malloc'd, freed by free_parsed_pattern
} parsed_pattern_t;
```

### NFA State (file-scope)
```c
#define NFA_MAX_PATTERN 128
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)  // = 258

enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };

typedef struct State State;
struct State {
    int    op;
    char   arg;       // OP_CHAR: pattern byte; OP_ASSERT: 0x0B or 0x0C
    State *out;
    State *out1;
    int    lastlist;  // generation tag for de-dup during simulation
};
```

### Global State
- `static int nfa_gen = 0;` — the ONLY file-scope mutable variable. A monotonic
  generation tag bumped on each simulation step. Safe because the matcher is
  single-threaded in QMK.

## Processed-Pattern Byte Contract

`process_escapes()` emits a NUL-terminated byte string consumed by the NFA:

| Byte(s) | Source | Meaning | Width |
|---|---|---|---|
| `0x2A` `*` | bare `*` | glob wildcard — any sequence incl. `\n`/`\r` | variable |
| `0x0E` | `+` after a consuming element | `+` quantifier marker (`X+` = ≥1 of X) | marker |
| `0x01`–`0x04` | `\^` `\$` `\*` `\\` | escaped literals | 1 consuming |
| `0x05`–`0x0A` | `\d` `\D` `\w` `\W` `\s` `\S` | character classes | 1 consuming |
| `0x0B` `0x0C` | `\b` `\B` | word-boundary / non-boundary assertions | 0 (zero-width) |
| `0x0D` | bare `.` | dot — any char **except `\n`/`\r`** | 1 consuming |
| `0x2E` `.` | `\.` | literal dot | 1 consuming |
| `0x2B` `+` | `\+`, or bare `+` not after consuming element | literal plus | 1 consuming |
| any other | ordinary char | literal byte | 1 consuming |
| `0x00` | end | NUL terminator | — |

### process_escapes Logic

Maintains `bool last_consumable` while walking the source pattern:

- `\X` (backslash + next char):
  - `\^ \$ \* \\` → placeholder `0x01`–`0x04`; `last_consumable = true`
  - `\. \+` → literal `.`/`+` (ordinary bytes); `last_consumable = true`
  - `\d \D \w \W \s \S` → placeholder `0x05`–`0x0A`; `last_consumable = true`
  - `\b \B` → placeholder `0x0B`/`0x0C`; **`last_consumable = false`** (zero-width)
  - **Unrecognized** (`\x`, `\z`) → emit `\\` + char literally; `last_consumable = true`
- Trailing lone `\` → emit `\` literally
- Bare `*` → emit `0x2A`; `last_consumable = false`
- Bare `+` → if `last_consumable`: emit `0x0E` (quantifier), set false; else literal `+`, set true
- Bare `.` → emit `0x0D`; `last_consumable = true`
- Anything else → emit literally; `last_consumable = true`

### parse_pattern Anchor Detection

- Leading `^` → `start_anchored = true`; skip it
- Trailing `$` not escaped (even backslash count before it) → `end_anchored = true`; drop it
- Between anchors is the core pattern, fed to `process_escapes`
- Allocates a temp core string, calls `process_escapes`, frees the temp

## NFA Compilation (nfa_compile)

Walks the processed pattern, threading a `State **tail` pointer:

- `0x2A` (glob `*`) → `OP_ANY` looping back through `OP_SPLIT` (matches `.*` semantics)
- `0x0B`/`0x0C` → `OP_ASSERT` (zero-width)
- `0x0E` standalone → skipped defensively (should not occur)
- Consuming element `X`:
  - If followed by `0x0E` (`X+`) → `OP_CHAR(X)` then `OP_SPLIT` (loop back) — **linear** for `a+a+a+...`
  - Else → `OP_CHAR(X)`
- End → append `OP_MATCH`
- Zero `lastlist` on every allocated state (pool is stack-fresh each call)

## NFA Simulation (nfa_match)

```c
static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive,
                      bool full_match);
```

- `abspos = str - string_start` (absolute offset into the ORIGINAL string — needed for `\b`/`\B`)
- Seed `clist` with epsilon-closure of start state at `abspos`
- If `!full_match` and MATCH reachable → return true (matches empty prefix)
- For each input char: build `nlist` by advancing `OP_ANY` (any non-NUL incl newline) and
  `OP_CHAR` where `pattern_char_matches(arg, c)` holds; bump `nfa_gen`; swap lists
- Early return true when `!full_match && nfa_has_match(clist)`
- Break when list empty (dead)
- Return `nfa_has_match(clist)` (for full_match=true, accept only at end)

### nfa_addstate (epsilon-closure, guarded by lastlist == nfa_gen)

- `OP_MATCH` → added to list
- `OP_SPLIT` → recurse into both `out` and `out1` (same abspos)
- `OP_ASSERT` → recurse into `out` only if boundary condition matches
  - Empty original string: neither boundary nor non-boundary (legacy semantics)
- `OP_CHAR`/`OP_ANY` → added to list, waiting to consume a char

**The lastlist generation tag is MANDATORY** — without it, `OP_SPLIT` and `\b\b`
recurse infinitely. Each simulation phase bumps `nfa_gen++` so closure de-dup works.

## Matching Strategy (match_with_anchors)

| Anchors | Strategy |
|---|---|
| `^` + `$` | Exact: `nfa_match(core, str, str, full=true)` |
| `^` only | Prefix: `nfa_match(core, str, str, full=false)` |
| `$` only | Suffix: loop `i=0..len`, `nfa_match(core, str+i, str, full=true)` |
| neither | Substring (backward-compat). Empty core → matches only empty string. Loop offsets. |

`string_start` is ALWAYS the original string base (so `\b`/`\B` compute absolute positions).

## Character Classification Helpers

```c
static bool is_digit_char(char c);        // '0'..'9'
static bool is_word_char(char c);         // [A-Za-z0-9_]
static bool is_whitespace_char(char c);   // ' ' \t \n \r \f \v
static bool is_word_boundary(const char *str, size_t pos);
```

`is_word_boundary(str, pos)`:
- `pos == 0` → true iff `str[0]` is word char
- `pos == strlen` → true iff `str[len-1]` is word char
- `pos > strlen` → false
- interior → true iff `is_word_char(str[pos-1]) != is_word_char(str[pos])`
- `str == NULL` → false

## pattern_char_matches(char pc, char sc, bool case_sensitive)

- `pc` in `0x01`–`0x04` → literal (decoded via `get_escaped_char`), case-folded
- `0x05`/`0x06` → `is_digit_char(sc)` / negation (`\d`/`\D`)
- `0x07`/`0x08` → `is_word_char(sc)` / negation (`\w`/`\W`)
- `0x09`/`0x0A` → `is_whitespace_char(sc)` / negation (`\s`/`\S`)
- `0x0D` → `sc != '\n' && sc != '\r'` (the dot)
- default → ordinary literal, case-folded

## Memory Model

- **One malloc per pattern_match call** — `process_escapes()` allocates the
  processed-pattern string; `free_parsed_pattern()` frees it.
- **NFA state pool is stack-allocated** — `State pool[NFA_MAX_STATES]` lives on
  the stack (~6-8 KB at NFA_MAX_PATTERN=128). No dynamic allocation on the NFA path.
- No leaks: verified by `test_memory_stress` (repeated alloc/free, no crashes).

## Sizing for MCU

With `NFA_MAX_PATTERN = 128`: ~256 States (~6-8 KB) + ~2 KB of pointer lists.
Fine on desktop and RP2040. For low-RAM AVR, lower `NFA_MAX_PATTERN` (e.g. 48).
Arrays MUST stay on the stack (not static) if reentrancy is ever needed.
In QMK the matcher is single-threaded.
