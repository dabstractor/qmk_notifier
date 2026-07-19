# Research Notes — P1.M1.T1.S1 (notifier.h OS API surface)

## Task
Modify the existing `notifier.h` (repo root) to add the multi-OS public API:
1. `#include "os_detection.h"` immediately after `#include <stdbool.h>`.
2. `void notifier_set_os(os_variant_t os);` in the accessor-decl block (after
   `size_t get_layer_map_size(void);`).
3. Two new macros `DEFINE_SERIAL_COMMANDS_OS(os, …)` and
   `DEFINE_SERIAL_LAYERS_OS(os, …)` AFTER the existing DEFINE_SERIAL_* macros.

## Current file state (READ at repo root)
- `wc -l notifier.h` → **42 lines** (item description said "48"; actual is 42 —
  trust the file on disk).
- Comment style: terse C++-style `//` line comments throughout (NOT `/* */`).
- Layout order: `#pragma once` → `#include <stdbool.h>` → typedefs (callback_t,
  command_map_t, layer_map_t) → accessor decls (get_*_map / get_*_map_size) →
  `// Forward declarations…` comment → GS_DELIMITER/ETX/WINDOW_TITLE/WT macros →
  `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS` → `// From QMK` raw_hid_receive/hid_notify.
- EXISTING default macros use `user_command_map` (no underscore prefix). The new
  `_OS` macros use `_notifier_command_map_##os` (underscore-prefixed, namespaced).

## os_variant_t enum (from architecture/external_deps.md — VERIFIED from qmk/qmk_firmware)
```c
typedef enum { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4 } os_variant_t;
```
The caller passes the enumerator NAME (e.g. `OS_MACOS`) to the macro; `##os`
pastes that token. Integer values are not used by the macro.

## EMPIRICALLY VERIFIED macro symbol contract (the load-bearing detail)
Built a throwaway harness: a keymap defining `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)`
+ `DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…)` + `…_OS(OS_LINUX,…)`, linked against
mock `__attribute__((weak))` defaults copied from PRD §8.3. RESULT:
- COMPILES clean (`-Wall -Wextra -std=c99`), LINKS, RUNS.
- `nm keymap.o` shows EXACTLY the names §8.3 expects:
  `_notifier_command_map_OS_MACOS`, `_notifier_command_map_OS_MACOS_size`,
  `_notifier_get_command_map_OS_MACOS`, `_notifier_get_command_map_OS_MACOS_size`,
  and the layer equivalents + OS_LINUX.
- A keymap-defined OS (MACOS) overrides the weak default; an OS the keymap did
  NOT define (LINUX command) falls back to weak `{NULL,0}` (ptr=(nil)).
⇒ The PRD §5.5 macro bodies are CORRECT and byte-exact as written. Use them VERBATIM.

## The 4 informational comments required (item point 5 / DOCS Mode A)
The macro bodies carry inline comments covering:
(a) ##os token-paste naming contract (generated names must match notifier.c §8.3
    weak defaults + select_*_map_os switch).
(b) OS_UNSURE has NO OS-specific map by design (§2 F8.6) — do not pass it.
(c) Row-struct parity with default macros (case_sensitive omittable → false).
(d) Selection rule: OS map scanned first per track; match wins, default not
    consulted; tracks decide independently (§2 F8.4/F8.5, §5.5 note).
Render these as `//` line comments to match the existing file's style (PRD §5.5's
`>` blockquotes are markdown prose, not C — translate to `//`).

## CRITICAL sequencing risk (must be in the PRP)
- The stub `qmk_stubs/os_detection.h` does NOT exist yet. It is created by
  SIBLING task P1.M1.T2.S1 (a later subtask in this same milestone).
- The EXISTING consumers `notifier.c` (incl via run_notifier_stub_tests.sh) and
  `test_notifier_dispatch.c` `#include "notifier.h"`. After this change, both
  will FAIL to compile because `os_detection.h` cannot be found
  (`-Iqmk_stubs -I.` has nothing to resolve).
- ⇒ `run_notifier_stub_tests.sh` is EXPECTED to break in isolation at THIS stage.
  It will recover automatically once P1.M1.T2.S1 lands `qmk_stubs/os_detection.h`.
- ⇒ THIS subtask's validation must NOT depend on the sibling stub. Use a
  THROWAWAY temp stub for the syntax/link check; document that the real gate is
  re-enabled by P1.M1.T2.S1.

## Pre-existing latent trait (NOT in scope to fix)
`notifier.h` uses `size_t` in its accessor decls but does NOT `#include <stddef.h>`.
It compiles today only because every includer (notifier.c via QMK_KEYBOARD_H;
test_notifier_dispatch.c via its own `#include <stddef.h>`) pulls size_t in
first. The new macros also use `size_t`. Do NOT "fix" this by adding stddef.h —
it is out of scope and would be an unrelated change. Just ensure any standalone
validation harness includes `<stddef.h>` before the header.

## Toolchain
- gcc 16.1.1, plain gcc (no make/cmake). `-std=c99` used by stub harness.
- Build does NOT use `-Werror`, so the expected `-Wmissing-field-initializers`
  / `-Wmissing-braces` warnings from omitting trailing `case_sensitive` are
  acceptable (documented PRD §5.4 zero-fill parity; reference keymap relies on it).

## Scope boundaries
- Modify ONLY `notifier.h`.
- Do NOT create `qmk_stubs/os_detection.h` (P1.M1.T2.S1).
- Do NOT modify notifier.c, test files, run_notifier_stub_tests.sh, PRD, tasks.json.
- Do NOT reformat/restyle existing parts of notifier.h (preserve `//` style, spacing).