# Research Notes — P1.M1.T2.S1: Change rules.mk to module-context form

## Task scope (from contract)
Single-file change to `rules.mk` at repo root. Convert from submodule-context
SRC path to bare Community-Module form + one Mode-A doc comment.

## Verified current state (2025-07-20)
`rules.mk` is exactly 2 lines:
```
RAW_ENABLE = yes
SRC += qmk_notifier/notifier.c
```
- `wc -l rules.mk` → 2

## Verified #include relationship (load-bearing for "no pattern_match SRC entry")
`notifier.c` line ~16 (after the NFA_MAX_PATTERN cap define):
```c
#include "pattern_match.c"
```
So `pattern_match.c` is compiled *via textual include into notifier.c*, resolved
by the compiler's include path (VPATH in module context). It is **never** a
separate `SRC +=` entry. Confirmed.

## Test independence (validation scope)
- `grep -l 'rules.mk' *.sh` → NONE. No host test script reads or references
  `rules.mk`. Host tests compile with `gcc` + `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'`
  directly against `notifier.c` / `pattern_match.c` / `test_*.c`.
- Therefore the host gates (`./run_all_tests.sh`, `./run_notifier_stub_tests.sh`)
  are a **regression/do-no-harm** check for this task, NOT a positive assertion
  that rules.mk is correct. Positive validation = file-content inspection.
- A real QMK module build (the true positive gate, §18.5) cannot run in this
  repo (no qmk_firmware build env). Note in PRP.

## Why each final line is correct (build mechanics from external_deps.md §1-3)
- `RAW_ENABLE = yes` stays: NOT a data-driven feature key (no `rawhid` in
  `data/schemas/`), so it MUST live in rules.mk, not qmk_module.json features.
  Generator does `-include <module_path>/rules.mk`, so it applies globally.
- `SRC += notifier.c` (bare, no path prefix): generator auto-compiles ONLY
  `<leaf>.c` = `qmk_notifier.c` via wildcard. Our file is `notifier.c` →
  not auto-compiled → must list explicitly. Bare form resolves via `VPATH +=
  <module_path>` which build_keyboard.mk:536 puts on `-I`.
- `pattern_match.c` is NOT listed: pulled in by notifier.c's `#include`.

## Intentional incompatibility
Bare `notifier.c` resolves via VPATH (module dir on -I), NOT via the
keyboard-relative `qmk_notifier/notifier.c` path the old submodule flow used.
This breaks the old `include keyboards/<...>/qmk_notifier/rules.mk` keymap
flow — intentional per §18.4 "retire submodule flow" decision. README rewrite
(live submodule-flow instructions at README.md:51-91) is a SEPARATE task
(P1.M2.T1.S1), out of scope here.

## Sibling context (do not couple)
- P1.M1.T1 (qmk_module.json manifest) is marked **Failed** but is a separate
  file (`qmk_module.json`). This rules.mk change does NOT depend on
  qmk_module.json existing — rules.mk is consumed independently by the
  generator's `-include`. Safe to land standalone.

## Final file content (exact)
```
# Community Module context — notifier.c via VPATH; pattern_match.c pulled in by notifier.c #include
RAW_ENABLE = yes
SRC += notifier.c
```
3 physical lines = 1 comment + 2 functional. The contract's "exactly two lines"
refers to the two *active* functional lines; the comment is the Mode-A doc
addition. Preserve the em-dash (—) verbatim.