# Research Notes — P1.M1.T2.S2

**Work item:** Implement `parse_pattern()`, `free_parsed_pattern()`,
`pattern_match()`, `get_escaped_char()` (+ a temporary `match_with_anchors`
stub) inside `pattern_match.c`.

## 1. Source of truth

PRD §17 ("the code + the passing tests win"). The authoritative, full
`pattern_match.c` lives in git at `HEAD` (`git show HEAD:pattern_match.c`,
514 lines). The working tree currently has ONLY `process_escapes()` (S1's
incremental rebuild output, 80 lines). S2 **appends** its functions to that
working file, reproducing the reference byte-for-behavior.

The functions this subtask owns (verbatim from `HEAD:pattern_match.c`):
`parsed_pattern_t` typedef, `parse_pattern`, `free_parsed_pattern`,
`get_escaped_char`, `pattern_match` (public), plus a **stub**
`match_with_anchors` (the real one is P1.M3.T2.S2).

## 2. Dependencies consumed from S1

`parse_pattern` calls `process_escapes(const char *)` (provided by S1, already
the only function in the working `pattern_match.c`). Therefore S2 must make
that symbol visible to `parse_pattern` — either by appending below it (defined)
or via a forward declaration. No new `#include` is needed: `stdbool.h`,
`string.h`, `stdlib.h` (already present from S1) cover everything S2 uses
(`strlen`, `strncpy`, `malloc`, `free`, `bool`).

## 3. parse_pattern anchor logic (PRD §7.3) — verified truth table

Even-backslash-count rule for the trailing `$`: count consecutive `\`
immediately before the final `$`; **even (0,2,4,…) ⇒ real end anchor**, odd ⇒
escaped (part of core). Verified by probing the reference:

| input (raw bytes)        | start | end | core bytes            | note |
|--------------------------|:-----:|:---:|-----------------------|------|
| `abc`                    | 0 | 0 | `61 62 63`              | plain |
| `^abc`                   | 1 | 0 | `61 62 63`              | start anchor |
| `abc$`                   | 0 | 1 | `61 62 63`              | end anchor |
| `^abc$`                  | 1 | 1 | `61 62 63`              | both anchors |
| `^$`                     | 1 | 1 | `` (empty)              | empty exact (PRD §15) |
| `$`                      | 0 | 1 | `` (empty)              | lone `$`; `end>start` after no start-anchor skip |
| `^`                      | 1 | 0 | `` (empty)              | `end>start` is FALSE ⇒ no end check |
| `` (empty)               | 0 | 0 | `` (empty)              | |
| `abc\$` (a b c \ $)      | 0 | 0 | `61 62 63 02`           | 1 backslash ⇒ escaped $ → core 0x02 |
| `abc\\$` (a b c \ \ $)   | 0 | 1 | `61 62 63 04`           | 2 backslashes ⇒ real $ → core has 0x04 |
| `abc\\\$` (a b c \ \ \ $)| 0 | 0 | `61 62 63 04 02`        | 3 backslashes ⇒ escaped $ |
| `\^` (\ ^)               | 0 | 0 | `01`                    | escaped caret literal |
| `\$` (\ $)               | 0 | 0 | `02`                    | escaped dollar literal |
| `a*b`                    | 0 | 0 | `61 2A 62`              | bare `*` → 0x2A glob |

Key edge: the reference guards the end-anchor block with `end > start`. For
`"^"` alone, after skipping `^`, `start == end`, so the block is skipped
(correct: single `^` ⇒ start anchor + empty core). For `"^$"`, after skipping
`^`, `start` points at `$` and `end` is one past it, so `end > start` holds
and `$` is detected (correct: both anchors + empty core).

## 4. get_escaped_char reverse map — verified

`0x01→'^' 0x02→'$' 0x03→'*' 0x04→'\\' 0x05→'d' 0x06→'D' 0x07→'w' 0x08→'W'
0x09→'s' 0x0A→'S' 0x0B→'b' 0x0C→'B' 0x0D→'.' default→placeholder` (passthrough).
Items `0x05`–`0x0D` are debug/diagnostic only; the matcher branch for escaped
literals uses only `0x01`–`0x04` (in `pattern_char_matches`, P1.M3.T2.S1).

## 5. free_parsed_pattern — verified

- Frees `processed_pattern` only; sets BOTH `processed_pattern` and
  `core_pattern` to `NULL` (so `core_pattern` is not left dangling).
- No-op when `parsed == NULL` or `processed_pattern == NULL` (covers the
  malloc-failure fallback path, where `core_pattern` points at the caller's
  raw pattern and must NOT be freed).

## 6. pattern_match (public) — verified

`if (!pattern || !str) return false;` → `parse_pattern` → `match_with_anchors`
→ `free_parsed_pattern` → return result. With the STUB, returns `false` for all
non-NULL inputs until P1.M3.T2.S2. NULL inputs return `false` (verified).

## 7. Build reality — unused-function warnings (DECISIVE)

- `gcc -Wall -Wextra -std=c99 -c` WARNS for static functions defined-but-unused.
  `-fsyntax-only` does NOT (it skips that analysis pass).
- S1's `process_escapes` currently triggers that warning (its only caller,
  `parse_pattern`, did not exist yet). **After S2, that warning disappears**
  because `parse_pattern` now calls `process_escapes`.
- S2 introduces `get_escaped_char`, whose only caller is `pattern_char_matches`
  (P1.M3.T2.S1). So after S2 the build emits **exactly ONE warning**:
  `'get_escaped_char' defined but not used`. This is EXPECTED and temporary —
  it self-resolves when P1.M3.T2.S1 lands. Forward declarations do NOT suppress
  it (verified). `__attribute__((unused))` is deliberately NOT used (deviates
  from the reference source of truth; not portable idiom for this codebase).
- **Validation gate for S2**: build exits 0; the sole permitted warning is the
  `get_escaped_char` unused-function warning; no other warnings/errors.

## 8. Integration / API integrity — verified

`gcc -Wall test_metachar_verification.c pattern_match.c -o /tmp/tm` (and all
other suites in `run_all_tests.sh`) **LINK cleanly** (exit 0) with the S2 code:
the public `pattern_match` symbol is present and the signature matches
`pattern_match.h`. Running the suites will print FAILURES for every true-case
until P1.M3.T2.S2 lands (the stub returns `false`). ⇒ S2 does NOT run
`run_all_tests.sh` to validate; it only asserts a clean LINK of one suite.

## 9. Scope boundaries (do not violate)

- Append ONLY to `pattern_match.c`. Do not touch: `pattern_match.h`,
  `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`, `tasks.json`,
  `prd_snapshot.md`, `.gitignore`.
- Do NOT add the real `match_with_anchors` / `nfa_*` / classifiers /
  `pattern_char_matches` / `match_string_with_start` /
  `match_reaches_end_with_start` — those are P1.M2 / P1.M3.
- Do NOT add new `#include`s (notifier.h, ctype.h, stdio.h, stdint.h,
  stddef.h) — none are used by S2's functions; they belong to later subtasks.
