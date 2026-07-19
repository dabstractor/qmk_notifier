# System Context — Bugfix 001_4a8e381fbf10

## Overview

This bugfix addresses three issues identified during end-to-end PRD validation
of the qmk-notifier implementation (session `002_c243e735980a`, multi-OS delta).
All three are specification/robustness/debug-observability fixes — **no critical
bugs exist** and the core matching/dispatch/reassembly functionality is verified
correct.

## Current State (verified 2025-07-18)

| Gate | Result |
|---|---|
| `./run_all_tests.sh` (9 pattern_match suites) | **2019/2019 assertions pass, 0 failures** |
| `./run_notifier_stub_tests.sh` (dispatch + multi-OS stubs) | **PASSED — 0 FAIL lines** |
| §11.2B pathological NFA stress (~199×`a` vs `a+a+...+b`) | ~1.7 ms (gate: < 50 ms) |
| §11.2C snippet with corrected `"user@host"` | **Six `1`s — PASS** |
| Fuzz/adversarial probing (~9300 iterations) | No crashes |

## Issue Summary

### Issue 1 (Major): PRD §11.2C acceptance gate wrong expected value
- **Root cause:** The PRD §11.2C example used `"user_host"` where it meant
  `"user@host"`. Pattern `^\w+@\w+$` correctly returns 0 for `user_host`
  (underscore, not `@`) and 1 for `user@host`.
- **Doc fix STATUS: ALREADY APPLIED.** PRD.md line 1261 was corrected to
  `"user@host"` at commit `4d49460` ("correct documentation examples").
  Running the snippet with the corrected string produces six `1`s.
- **Remaining work:** Add a regression test to `test_pattern_match.c` that
  locks in the correct semantics (both directions) so a future "rebuild to
  spec" cannot regress the matcher toward the wrong expectation.
- **Constraint:** Do NOT modify `pattern_match.c` — the matcher is correct.

### Issue 2 (Minor): sanitize_string truncates at NUL instead of stripping
- **Root cause:** `sanitize_string` (notifier.c:46-69) uses `while (*read_ptr)`
  which stops at the first NUL byte, abandoning everything after it. The PRD
  spec (§8.2, §F2.3) says "Strip every other byte" — NUL is "every other byte."
- **Reachability:** NOT reachable in normal operation (transport appends ETX
  before zero-fill; `msg_buffer` never contains an embedded NUL before its
  terminator in spec'd operation). Requires malformed/hostile USB frame.
- **Fix:** Change `sanitize_string` to accept a `size_t len` parameter and
  iterate by index (`for (size_t i = 0; i < len; i++)`) instead of
  `while (*read_ptr)`. Update the single call site in `hid_notify` to pass
  `msg_index`.
- **Spec drift:** PRD §8.2 currently says `sanitize_string(char *str)`.
  After the fix it will be `sanitize_string(char *str, size_t len)`. This is
  a `static` file-local function with exactly one caller. The PRD says "where
  this spec and the code disagree, the code + passing tests win; report the
  drift" — this drift is reported here.

### Issue 3 (Minor): CONSOLE_ENABLE debug omits layer-track match/miss print
- **Root cause:** The `#ifdef CONSOLE_ENABLE` block at notifier.c:417-434
  prints only the command track (`command_found`), not the layer track
  (`layer_found`). The PRD §8.6 step 9 says "print per-track match/miss."
- **Fix:** Add a layer-track `uprintf` in the CONSOLE_ENABLE block, after the
  existing command-track print. Mirror the command track: print the layer
  index on match, or a miss message.
- **Testing challenge:** `uprintf` requires QMK's `print.h`, not available in
  the stub harness. A minimal `print.h` stub (mapping `uprintf` to `printf`)
  is needed to compile-check with `-DCONSOLE_ENABLE`.

## File Inventory (what changes, what doesn't)

| File | Changes | Issue |
|---|---|---|
| `test_pattern_match.c` | **ADD** regression test cases for `^\w+@\w+$` semantics | Issue 1 |
| `notifier.c` | **MODIFY** `sanitize_string` (line 46-69) + call site (line 495) | Issue 2 |
| `notifier.c` | **MODIFY** CONSOLE_ENABLE block (line 417-434) — add layer track | Issue 3 |
| `test_notifier_dispatch.c` | **ADD** stub test for embedded-NUL sanitization | Issue 2 |
| `qmk_stubs/print.h` | **NEW** minimal stub for CONSOLE_ENABLE compile-check | Issue 3 |
| `README.md` | **UPDATE** test count table (test_pattern_match: 376→376+N) | All |
| `pattern_match.c` | **DO NOT TOUCH** — matcher is correct | — |
| `pattern_match.h` | **DO NOT TOUCH** | — |
| `run_all_tests.sh` | **DO NOT TOUCH** — auto-recompiles existing test files | — |
| `PRD.md` | **DO NOT TOUCH** — human-owned; doc fix already applied | — |

## Dependencies Between Issues

All three issues are **independent** — they touch different code paths:
- Issue 1: `test_pattern_match.c` (pattern_match test corpus)
- Issue 2: `notifier.c` sanitize_string + `hid_notify` call site
- Issue 3: `notifier.c` CONSOLE_ENABLE block

Issues 2 and 3 both modify `notifier.c` but in different functions and at
different line ranges — no merge conflict risk if done sequentially.