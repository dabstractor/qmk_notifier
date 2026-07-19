# Research Notes — P1.M2.T1.S1 (test_notifier_os.c — six §11.2D categories)

## Task scope
Create the OFFICIAL host test `test_notifier_os.c` that stub-compiles
`notifier.c` and verifies the multi-OS F8/F9 contract (PRD §11.2D criteria i–vi).
Compiles + links + runs with the exact §11.1 command; all 6 categories pass with
0 `FAIL:` lines. Follows the `test_notifier_dispatch.c` test-framework pattern.

## Status of dependencies (all LANDED before this task runs)
- **P1.M1.T3.S1** (provider): `current_os` global, 16 per-OS weak accessors, 2
  `static` selectors — PRESENT in `notifier.c` (S1 block).
- **P1.M1.T3.S2** (consumer): `notifier_set_os()` (line 459) + the rewritten
  `process_full_message` (line 334, OS-first/default-fallback per track,
  `command_found->pattern` CONSOLE fix) — **LANDED and VERIFIED**:
  `grep` confirms `select_command_map_os(current_os,…)` at line 362,
  `if (os == current_os) return;` at line 460, `current_os = os;` at line 464.
  `test_notifier_dispatch` still 11/11 (backward-compat canary).
- **P1.M1.T1.S1 / P1.M1.T2.S1**: `notifier.h` (DEFINE_*_OS macros + decls) and
  `qmk_stubs/os_detection.h` (os_variant_t enum) — LANDED.

## The test-framework pattern to follow (from test_notifier_dispatch.c)
1. `#include <stdint.h> … #include "notifier.h"` at top.
2. `extern`-style declarations for the non-static `notifier.c` entry points:
   `bool match_pattern(...)`, `bool process_full_message(char*)`,
   `void hid_notify(uint8_t*,uint8_t)`. Plus `void notifier_set_os(os_variant_t)`.
3. `DEFINE_SERIAL_*` macros at FILE SCOPE (define the map globals + override the
   weak `get_*_map` accessors for THIS translation unit only).
4. `static int g_pass = 0, g_fail = 0;` counters.
5. A check helper printing `PASS:`/`FAIL:` lines (runner greps `grep -c '^FAIL:'`).
6. Final summary `Total tests run: N / passed: P / failed: F`; `return g_fail?1:0;`.
- **CRITICAL**: `test_notifier_os.c` is a SEPARATE binary from
  `test_notifier_dispatch.c` (runner compiles each independently), so the
  file-scope `DEFINE_*` do NOT collide (each TU has its own map globals).

## Observation strategy (findings F6) — RESOLVED
- The stub `layer_on/layer_off` write to a **file-static** `g_active_layer` in
  `qmk_stubs.c` (line 6) that the test CANNOT read directly.
- **Solution chosen (contract-permitted, Mode A)**: add a 1-line test-harness
  accessor to `qmk_stubs.c`:
    `uint8_t stub_get_active_layer(void) { return g_active_layer; }`
  (with a Mode A comment: "test-harness observable, NOT production code; in
  production QMK provides `layer_state`"). The test declares it `extern` and
  calls it to assert WHICH layer won.
- **Distinguishable callbacks** observe WHICH command map matched: separate
  `os_cmd_on/off` vs `def_cmd_on/off` functions incrementing separate flags.
  This is the F6 mechanism and lets the test assert "OS cmd fired, default NOT
  scanned" (the F8.4 core rule).
- **Distinct layer numbers** in OS maps (11, 44) vs default maps (22, 33) so the
  active-layer value unambiguously identifies the winning track/map.

## Validated blueprint: maps design (from P1.M1.T3.S2/research/test_notifier_os_val.c)
This map set was designed so SIX messages cover all §11.2D criteria:
```c
/* DEFAULT (OS-agnostic). Distinct layer numbers from OS maps. */
DEFINE_SERIAL_COMMANDS({
    { "neovide",          def_cmd_on, def_cmd_off },   /* default-only cmd            */
    { WT("blender", "*"), def_cmd_on, def_cmd_off },   /* collides w/ OS map (F8.4)   */
});
DEFINE_SERIAL_LAYERS({
    { "blender",   22 },   /* default layer; OS layer for same msg = 11 (collision) */
    { "calculator", 33 },  /* default-only layer                                   */
});
/* OS_MACOS (strong overrides; symbols via ##os paste in notifier.h). */
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { WT("blender", "*"), os_cmd_on, os_cmd_off },     /* collides w/ default (F8.4) */
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "blender", 11 },   /* collides w/ default 22 (F8.4 layer) */
    { "iTerm",   44 },   /* OS-only layer (F8.5 independence)   */
});
```
Message → criterion coverage:
- `"blender"` (OS_MACOS) → **(i)** OS cmd+layer win, default NOT scanned (collision).
- `"calculator"` (OS_MACOS) → **(ii)b** OS map exists, matches nothing → default.
- `"iTerm"` (OS_MACOS) → **(iii)** layer from OS, command resolves to nothing.
- `"neovide"` (OS_MACOS) → **(iii)** command from default, layer resolves to nothing.
- boot `OS_UNSURE` + `"iTerm"` → **(ii)c** OS_UNSURE ⇒ OS map inert (no match).
- boot `OS_UNSURE` + `"calculator"` → default matches (baseline).
- `notifier_set_os(OS_MACOS)` while already OS_MACOS → **(iv)** idempotent no-op.
- `notifier_set_os(OS_LINUX)` after blender active → **(v)** clear + no re-dispatch.
- `notifier_set_os(OS_WINDOWS)` (no overrides) + `"blender"` → **(vi)** default-only
  behavior for a no-override OS (backward-compat: set_os changes nothing).
- (ii)a "OS map absent" == the OS_WINDOWS / OS_LINUX cases (no DEFINE_*_OS for them).

## Empirical validation performed (PASSED 30/30)
Built `test_notifier_os_val.c` against the CURRENT (post-S2) `notifier.c` with
`stub_get_active_layer()` appended to a /tmp copy of `qmk_stubs.c`, using the
EXACT contract build command:
```
gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c /tmp/qmk_stubs_obs.c test_notifier_os_val.c -std=c99 -o /tmp/test_os
```
**Result: build rc=0; `Total tests run: 30 / passed: 30 / failed: 0`; `grep -c
'^FAIL:'` == 0.** With `-Wall -Wextra` the only warnings are 7×
`-Wmissing-field-initializers` from the TEST TU (map rows omitting trailing
`case_sensitive`); the official test should add `, false` to be clean. NO
warnings originate from `notifier.c` (S2 cleared S1's unused-selector warnings).

## Build-command facts (§11.1 — exact, copy/paste)
- The §11.1 `test_notifier_os` line has **NO `-Wall`/`-Wextra`** — only
  `-std=c99`. It is a ONE-SHOT compile+link (all sources together), unlike
  `run_notifier_stub_tests.sh` which object-compiles `notifier.c` first.
- Flags: `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -std=c99`.
- `-Iqmk_stubs` is REQUIRED (notifier.h includes os_detection.h; qmk_keyboard_stub.h
  lives there). The current `run_notifier_stub_tests.sh` [2/3] step LACKS it
  (pre-existing; fixed by P1.M2.T2.S1) — but §11.1's one-shot command already
  has it, so THIS test is unaffected.

## Files this task touches
1. **CREATE** `test_notifier_os.c` (repo root) — the official test.
2. **MODIFY** `qmk_stubs/qmk_stubs.c` — append `stub_get_active_layer()` (1 line +
   Mode A doc). This is TEST-HARNESS code (the contract explicitly permits it;
   findings F6 sanctioned it; it does NOT touch production notifier.c/.h).
- Do NOT touch notifier.c, notifier.h, pattern_match.*, test_notifier_dispatch.c,
  run_*.sh, PRD.md, tasks.json, rules.mk, .gitignore.