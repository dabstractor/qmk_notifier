# Research Notes — P1.M1.T3.S1

**Item**: Add the `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` guard to `notifier.c`
**Plan**: 004 (Community Module Distribution, §18) — requirement **R3**
**Repo**: This task belongs to the **firmware module** `/home/dustin/projects/qmk_notifier`
(underscore). The orchestrator launched the agent in `/home/dustin/projects/qmk-notifier`
(hyphen = the **Rust transport crate**, a different repo with no `notifier.c`).
The relative output paths (`plan/004_.../P1M1T3S1/`) resolve to the firmware repo
where `notifier.c`, `run_notifier_stub_tests.sh`, the architecture docs, the
sibling P1.M1.T2.S1 PRP, and `tasks.json` actually live. PRP + research are
written there (absolute paths).

## 1. The change (verbatim, per PRD §18.3 R3)

Insert immediately AFTER `#include QMK_KEYBOARD_H` (notifier.c line 2) and BEFORE
the `/* Cap the Thompson NFA ... */` comment block (starts line 4):

```c
/* Community Module API version guard (PRD §18.3 R3).
 * In a real QMK module build, QMK_KEYBOARD_H → quantum.h → the generated
 * community_modules header defines both COMMUNITY_MODULES_API_VERSION and the
 * ASSERT_COMMUNITY_MODULES_MIN_API_VERSION macro (community_modules.py:252-254),
 * so this STATIC_ASSERT fires and enforces module API >= 1.0.0 — the floor that
 * provides housekeeping_task and process_detected_host_os
 * (data/constants/module_hooks/1.0.0.hjson). In host/stub tests
 * (-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"') neither symbol is defined, so the
 * #ifdef skips this block harmlessly and all stub binaries stay green. */
#ifdef COMMUNITY_MODULES_API_VERSION
ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);
#endif
```

## 2. Ground-truth verification (from `/home/dustin/projects/qmk_firmware`)

| Claim | Source | Verified |
|---|---|---|
| Generator emits `COMMUNITY_MODULES_API_VERSION` + `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(maj,min,pat)` | `lib/python/qmk/cli/generate/community_modules.py:252-254` | ✅ exact text confirmed |
| The assert expands to `STATIC_ASSERT(BUILDER(maj,min,pat) <= COMMUNITY_MODULES_API_VERSION, "...needs newer QMK modules API...")` | `community_modules.py:254` | ✅ compile-time check; FAILS (negative array size) if build API < target |
| `COMMUNITY_MODULES_API_VERSION` interpolates the build's current version (e.g. 1.1.2) | `community_modules.py:253` (f-string) | ✅ |
| 1.0.0 is the floor providing `housekeeping_task` + `process_detected_host_os` | `data/constants/module_hooks/1.0.0.hjson` line 2 (`housekeeping_task`) + line 19 (`process_detected_host_os`) | ✅ |
| In stub context neither symbol is defined → `#ifdef` skips the assert | `qmk_stubs/qmk_keyboard_stub.h` (grep: neither symbol present) | ✅ |

`housekeeping_task` is the QMK periodic tick the host-rules/typed-command logic
hooks; `process_detected_host_os` is where a keymap calls `notifier_set_os`
(§8.7). Both are >= 1.0.0, so `1, 0, 0` is the correct minimum.

## 3. Exact insertion site (verified line numbers)

`/home/dustin/projects/qmk_notifier/notifier.c`:
```
1  // notifier.c
2  #include QMK_KEYBOARD_H       ← anchor (unique: grep -c '^#include QMK_KEYBOARD_H$' == 1)
3  (blank)
4  /*                            ← NFA_MAX_PATTERN comment block begins here
...                                   (insert the guard between line 2 and line 4)
13 */
14 #define NFA_MAX_PATTERN 128
15 (blank)
16 #include "pattern_match.c"
```
The item's prose ("NFA comment at lines 4-16") is slightly imprecise (the comment
is 4-13; the `#define` is line 14), but the insertion directive — "immediately
after `#include QMK_KEYBOARD_H`, before the NFA comment block" — is unambiguous
and matches the unique line-2 anchor.

**Insertion decision**: place the guard block in the existing blank line 3 region
(after line 2, before the `/*` at line 4). Keep one blank line before the NFA
comment so the file reads cleanly. Net: +11 lines (8 comment + `#ifdef` + assert
+ `#endif`), no deletions, no restyle.

## 4. Empirical validation (all PASS — /tmp copies, repo untouched)

| Test | What | Result |
|---|---|---|
| Baseline | `./run_notifier_stub_tests.sh` on unmodified notifier.c | ✅ dispatch/os/host each fails=0; 94/94 pass; "✓ gate PASSED" |
| Stub no-op | apply guard to /tmp copy, stub-compile | ✅ compiles, **0 warnings** (#ifdef skipped) |
| Stub re-green | link + run all THREE drivers against modified object | ✅ dispatch fails=0, os fails=0, host fails=0 |
| Real-build sim | define the symbols (sim 1.1.2 build), require 1.0.0 | ✅ STATIC_ASSERT compiles (satisfied) |

The stub re-green test (test 3) IS the item's required OUTPUT proof: "The three
notifier stub binaries (dispatch / os / host) re-pass with 0 FAIL: lines
unchanged." Confirmed empirically.

## 5. Why 1.0.0 (not higher)

The module's only QMK-API dependencies that matter for module distribution are:
- `housekeeping_task` (host-rules / typed-command periodic work) — present at 1.0.0
- `process_detected_host_os` (keymap calls `notifier_set_os` here) — present at 1.0.0

Targeting the floor (1.0.0) maximizes compatibility with older QMK checkouts
while still rejecting pre-module-API builds. Current latest is 1.1.2 (per
external_deps.md); `1, 0, 0` passes against it (verified in test 4).

## 6. Scope boundaries (do NOT do in this subtask)

- ❌ Do NOT add `qmk_module.json` (that's R1 / P1.M1.T1.S1 — currently Failed).
- ❌ Do NOT rewrite `rules.mk` (that's R2 / P1.M1.T2.S1 — currently Implementing).
- ❌ Do NOT rewrite the README (R5 / P1.M2.T1.S1) or run the full §11.2 gate sweep (R6 / P1.M2.T2.S1).
- ❌ Do NOT define `COMMUNITY_MODULES_API_VERSION` / `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` in the stub — the whole point is the symbols are ABSENT in stub context so the `#ifdef` skips. Defining them would make the stub harness diverge from real QMK.
- ❌ Do NOT touch `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, test files, or `run_notifier_stub_tests.sh`.

## 7. Dependencies / sequencing

- This task is independent of R1 (`qmk_module.json`) and R2 (`rules.mk`). The
  guard is a self-contained `notifier.c` edit; it lands cleanly regardless of the
  R1/R2 states. (R1 is currently Failed, R2 Implementing — neither blocks this.)
- The guard has NO effect until the module is distributed as a Community Module
  (a real QMK build pulls in the symbols via `quantum.h`). In the current
  submodule/host-test flow it is inert (the `#ifdef` is always false). This is by
  design (§18 is "planned migration").