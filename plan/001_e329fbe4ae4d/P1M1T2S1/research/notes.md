# Research Notes — P1.M1.T2.S1: `process_escapes()`

## Project model

This is a **REBUILD** of `qmk-notifier`. The existing working-tree files
(`pattern_match.c`, `pattern_match.h`, `notifier.*`, `test_*.c`, `run_all_tests.sh`)
are the **live source of truth** (PRD §17: "the code + the passing tests win").
The orchestrator is recreating the matcher file-by-file across subtasks.

`process_escapes()` already exists verbatim in `pattern_match.c` (the reference).
This task's deliverable is that exact function, with inline documentation of the
placeholder-byte contract (Mode A docs, per item spec §5).

## Source-of-truth facts (verified by reading the repo)

### Exact function signature & placement
- `static char *process_escapes(const char *pattern)` — **static**, file-scope in
  `pattern_match.c`. Not declared in `pattern_match.h` (header exposes only
  `pattern_match`, per P1.M1.T1.S1 contract).
- It is **self-contained**: it calls only `strlen`, `malloc`, and uses `bool`.
  It does NOT call any other static helper (`get_escaped_char`, `parse_pattern`,
  NFA funcs, char-classifiers). Confirmed by reading the body.
- It is the **single malloc per `pattern_match` call**; the returned buffer is
  freed by `free_parsed_pattern()` (task P1.M1.T2.S2).

### Includes it needs (and only these)
- `<stdbool.h>` — `bool`
- `<string.h>`  — `strlen` (also brings `size_t`)
- `<stdlib.h>`  — `malloc`, `free`

It does NOT need `<stdio.h>`, `<ctype.h>`, `<stdint.h>`, `<stddef.h>`, or
`notifier.h`. (The reference file pulls all of those in for OTHER functions.)

### malloc-size invariant (critical, non-obvious)
`process_escapes` allocates `malloc(strlen(pattern) + 1)`. This is safe because
the output is **never longer than the input**:
- recognized `\X`  → 2 input bytes, **1** output byte
- unrecognized `\x`→ 2 input bytes, **2** output bytes (`\` + char)  ← max
- bare `*`/`+`/`.` / ordinary char → 1 in, 1 out
So `out_len ≤ in_len` always; `in_len + 1` covers output + NUL terminator.
Verified with a 64×`\d` stress case (128 in → 64 out).

### The byte contract (PRD §7.1 + architecture "Processed-Pattern Byte Contract")

| Input       | Output byte | `last_consumable` after |
|-------------|-------------|-------------------------|
| `\^`        | 0x01        | true  |
| `\$`        | 0x02        | true  |
| `\*`        | 0x03        | true  |
| `\\`        | 0x04        | true  |
| `\d`        | 0x05        | true  |
| `\D`        | 0x06        | true  |
| `\w`        | 0x07        | true  |
| `\W`        | 0x08        | true  |
| `\s`        | 0x09        | true  |
| `\S`        | 0x0A        | true  |
| `\b`        | 0x0B        | **false** (zero-width) |
| `\B`        | 0x0C        | **false** (zero-width) |
| bare `.`    | 0x0D        | true  |
| bare `*`    | 0x2A        | **false** (wildcard) |
| `\.`        | 0x2E (`.`)  | true  |
| `\+`        | 0x2B (`+`)  | true  |
| bare `+` (prev consumable) | 0x0E marker | false |
| bare `+` (prev NOT consumable) | 0x2B literal `+` | true |
| unrecognized `\x`,`\z` | `\\`+char (2 bytes) | true |
| trailing lone `\`       | `\` (0x5C)          | true |
| ordinary char | itself     | true |
| NULL input   | returns NULL | — |
| end of input | NUL terminator appended | — |

### The `+` quantifier logic (subtle — most error-prone part)
`last_consumable` is a running flag. Bare `+` behavior depends on it:
- after a consuming element (literal/class/dot/escaped-literal/literal-`+`/ordinary):
  emit `0x0E`, set flag false.
- after a non-consuming element (`*`, `\b`, `\B`, a previous `0x0E`, or start):
  emit literal `+`, set flag true.
Worked examples (all verified against reference, see truth-table run):
- `a+`   → `61 0E`
- `++`   → `2B 0E`      (1st +: flag false→literal `+`, flag true; 2nd +: flag true→0x0E)
- `a++`  → `61 0E 2B`   (a: true; +:0x0E,false; +:false→literal `+`)
- `\d+`  → `05 0E`
- `.+`   → `0D 0E`
- `*+`   → `2A 2B`      (*:false; +:false→literal `+`)
- `\b+`  → `0B 2B`      (\b:false; +:false→literal `+`, i.e. \b is NOT quantifiable)

## Ground-truth validation (run during research)

`/tmp/pe_truth.c` does `#include "pattern_match.c"` (to reach the static fn) and
asserts exact byte output for 36 cases spanning every category above, plus NULL,
empty, trailing-backslash, unrecognized escapes, and a 64×`\d` length-invariant
stress. Compiled with `gcc -Wall -Wextra -I.` and run against the existing
reference `pattern_match.c`:

```
ALL TRUTH-CASES CONFIRMED (0 failures)
```
and zero compile warnings. → The validation harness + expected outputs embedded
in the PRP are verified correct against the source of truth.

## Scope boundaries (do NOT do in this task)
- Do NOT implement: `parse_pattern`, `free_parsed_pattern`, `pattern_match`,
  `get_escaped_char` (those are P1.M1.T2.S2).
- Do NOT implement: NFA engine (P1.M2), char classifiers (P1.M3), tests (P1.M4).
- Do NOT modify `pattern_match.h` (P1.M1.T1.S1 owns it).
- Do NOT add `process_escapes` to the header (it is static/private).

## Build/test conventions (from run_all_tests.sh)
- Plain `gcc`, no make/cmake. Tests compile as `gcc -o test_X test_X.c pattern_match.c`.
- Because `process_escapes` is static, it is NOT directly callable by the
  committed test suite (which only exercises public `pattern_match()`). The
  committed suites cover it *indirectly* once `pattern_match()` is wired up
  (P1.M1.T2.S2). For THIS task, validation uses a temporary `#include` harness.
- All existing test files `#include "pattern_match.h"` and link `pattern_match.c`.

## Decision: incremental file creation
`pattern_match.c` is being rebuilt incrementally. P1.M1.T2.S1 is the **first
subtask to create `pattern_match.c`** (the rebuild target), seeded with the
minimal include block + `process_escapes()`. Later subtasks (S2, P1.M2, P1.M3)
append their functions. Function order in the final file may differ from the
reference — acceptable per PRD §17 (passing tests are the gate, not byte-order).

## External references
- Russ Cox, "Regular Expression Matching Can Be Simple And Fast"
  (https://swtch.com/~rsc/regexp/regexp1.html) — already cited in the reference
  `pattern_match.c`; relevant to the NFA engine (P1.M2), not to this escape
  processor. No other external dependency needed (stdlib only).
