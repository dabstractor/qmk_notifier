# System Context — Plan 002 (Multi-OS Map Selection Delta)

## What this plan is

This is a **delta** on a green, production-ready codebase. Plan `001_e329fbe4ae4d`
built the full qmk-notifier firmware module (pattern matcher, receiver/dispatcher,
9-suite test corpus, stub harness). Plan `002_c243e735980a` adds **one feature**:
per-OS command/layer map selection (opt-in overlay) with `notifier_set_os`.

## Verified baseline (2025-07-18)

| Gate | Result |
|---|---|
| `./run_all_tests.sh` (9 pattern_match suites) | **2019/2019 passing, 0 failures** |
| `./run_notifier_stub_tests.sh` (dispatch stub) | **11/11 passing, `✓ notifier stub-compile gate PASSED`** |
| Performance micro-benchmark | ~0.099 µs per `pattern_match` call |

The pattern matcher (`pattern_match.c/h`), `rules.mk`, the reassembly/ETX/sanitize
logic, `match_pattern` (F4 delimiter wrapper), the basic `process_full_message`
ordering (disable→scan→deactivate→activate), and the 9-suite corpus are all
**untouched** by this delta and must stay green throughout.

## What exists today (file inventory, verified by reading source)

| File | Current state | Delta impact |
|---|---|---|
| `notifier.h` (48 lines) | structs, `DEFINE_SERIAL_*` macros, accessor decls, constants | **GROWS** ~48→~80: add `#include "os_detection.h"`, `notifier_set_os` decl, 2 `DEFINE_*_OS` macros |
| `notifier.c` (339 lines) | receiver, reassembler, dispatcher, weak defaults | **GROWS** ~339→~410: add `current_os`, 16 per-OS weak accessors, `select_*_map_os`, OS-first scan, `notifier_set_os` |
| `pattern_match.c` (33KB) | Thompson NFA matcher | **UNTOUCHED** |
| `pattern_match.h` | matcher decl | **UNTOUCHED** |
| `rules.mk` (2 lines) | `RAW_ENABLE` + `SRC +=` | **UNTOUCHED** |
| `qmk_stubs/qmk_keyboard_stub.h` | layer_on/off, RAW_EPSIZE | unchanged |
| `qmk_stubs/raw_hid.h` | raw_hid_send decl | unchanged |
| `qmk_stubs/qmk_stubs.c` | layer_on/off + raw_hid_send impl | unchanged |
| `qmk_stubs/os_detection.h` | **DOES NOT EXIST** | **NEW** — header-only `os_variant_t` enum stub |
| `test_notifier_dispatch.c` (11 cases) | F4 matrix, ordering, reassembly, ack | unchanged (must re-pass; may need OS-aware reset between cases) |
| `test_notifier_os.c` | **DOES NOT EXIST** | **NEW** (~150 lines) — F8/F9 host tests |
| `run_notifier_stub_tests.sh` (45 lines) | compiles notifier.c→.o, links dispatch test | **GROWS** to build+run a 2nd binary (`test_notifier_os`) |

## The existing dispatch flow (what the delta modifies)

Current `process_full_message` (verified in source):
```
1. strlen guard (>= 256 → false)
2. memcpy into received_command[256]
3. disable_command()                    ← ALWAYS
4. scan cmd_map (get_command_map)       ← first-match-wins
5. scan lyr_map (get_layer_map)         ← first-match-wins
6. deactivate_layer()                   ← ALWAYS
7. enable_command() if found
8. activate_layer() if found
9. CONSOLE_ENABLE print
10. return (command_found || layer_found)
```

**Delta change:** steps 4 and 5 each become "scan OS map first (if any), else
fall back to default map" — independently per track. The ordering invariants
(disable-before-scan, deactivate-before-activate, first-match-wins) are unchanged.

## The existing test pattern (what new tests must follow)

`test_notifier_dispatch.c` uses:
- `DEFINE_SERIAL_COMMANDS({...})` and `DEFINE_SERIAL_LAYERS({...})` at file scope
- `static void ck(pattern, msg, cs, want)` helper printing `PASS:`/`FAIL:`
- `static int g_pass, g_fail` counters + summary line `Total tests run: N / passed: P / failed: F`
- Return `g_fail ? 1 : 0`
- Runner greps `grep -c '^FAIL:'`

The new `test_notifier_os.c` must use the **same `DEFINE_*` + `DEFINE_*_OS` macros
at file scope** and the same PASS:/FAIL: + summary + grep-able pattern.