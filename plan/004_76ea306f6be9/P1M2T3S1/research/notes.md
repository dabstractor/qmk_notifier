# Research Notes — P1.M2.T3.S1 (Sweep README for stale submodule references)

## CRITICAL: target repo is the FIRMWARE repo, NOT the harness cwd
- Harness cwd: `/home/dustin/projects/qmk-notifier` (hyphen) = the **Rust transport crate**
  (Cargo.toml, src/, hidapi). It has NO notifier.c / Setup section / plan/004 source.
- TARGET: `/home/dustin/projects/qmk_notifier` (underscore) = the **firmware module repo**
  (notifier.c, pattern_match.c, rules.mk, qmk_stubs/, README.md, plan/004_76ea306f6be9).
- ALL outputs (PRP + research + the README edit) belong in the firmware repo. Write to
  absolute paths under `/home/dustin/projects/qmk_notifier/`.

## What P1.M2.T1.S1 already did (the boundary)
P1.M2.T1.S1 (COMPLETE) rewrote the firmware README **`## Setup` section** (the 3-step
submodule flow → the Community Module flow). It explicitly LEFT "other submodule mentions
(repo overview bullets, etc.)" for THIS task (P1.M2.T3.S1). So: do NOT touch `## Setup`
again — it is already correct.

## The §18 Community Module flow (ground truth, PRD §18.3 R4/R5)
- Install: clone to `modules/<org>/qmk_notifier` (userspace), add `"modules": ["<org>/qmk_notifier"]`
  to keymap.json, include `#include "notifier.h"` (NO relative path — VPATH puts the module
  dir on -I), define the `raw_hid_receive → hid_notify` shim.
- Leaf MUST be `qmk_notifier` (underscore); hyphenated leaf = hard build failure (R4).
- GONE vs submodule flow: in-keyboard clone, `#include "./qmk_notifier/notifier.h"`,
  `include keyboards/.../rules.mk`, `SRC +=`/`RAW_ENABLE` hand-wiring.
- Naming: firmware = `qmk_notifier` (underscore); Rust transport crate = `qmk-notifier` (hyphen).

## THE SWEEP RESULT (grep over the firmware README)
Comprehensive grep for stale patterns (`submodule | SRC += | include .*rules.mk |
include keyboards | RAW_ENABLE | "./qmk[-_]notifier | qmk-notifier | qmk_notifier`)
shows: every hit in the `## Setup` section is CORRECT (negative phrasing like "no
hand-wired `SRC +=`/`RAW_ENABLE`/`include .../rules.mk`"; the userspace `git submodule add
... modules/<org>/qmk_notifier`; the R4 leaf contract "never qmk-notifier"). Those stay.

### EXACTLY ONE stale reference outside Setup
README line ~191, inside `## Multi-OS Configuration` → `### How to enable` step 2:
```c
#include QMK_KEYBOARD_H
#include "./qmk_notifier/notifier.h"          // ← STALE submodule-flow relative path

void raw_hid_receive(uint8_t *data, uint8_t length) {
    hid_notify(data, length);
}
bool process_detected_host_os_kb(os_variant_t os) { notifier_set_os(os); ... }
```
This is the submodule-flow relative include that R5 says is GONE. The Multi-OS section was
NOT touched by P1.M2.T1.S1 (Setup-only scope), so this stale path survived. The final sweep
confirms it is the ONLY stale ref outside Setup:
`awk '/^## Setup/{s=1}/^## Usage/{s=0}!s' README.md | grep 'include "\./'` → exactly 1 hit.

### FIX (mandatory): update the Multi-OS snippet to the Community Module flow
- Change `#include "./qmk_notifier/notifier.h"` → `#include "notifier.h"` (VPATH, no relative path).
- The `raw_hid_receive` shim duplicates Setup step 3. Trim the snippet to the multi-OS-
  specific `process_detected_host_os_kb` override + the include, with a one-line comment
  noting the shim still comes from Setup step 3. (Step 2's prose already says "override
  process_detected_host_os_kb. This is the one required call" — so showing just that
  function aligns the snippet with its own description.)
- Exact replacement given in the PRP "Implementation Tasks".

## Companion Projects naming (item 3c) — ALREADY CORRECT, verify only
README `## Companion Projects`:
- `[qmk-notifier](https://github.com/dabstractor/qmk-notifier)` = Rust transport crate ✓
- naming note: `qmk_notifier` (underscore) = firmware; `qmk-notifier` (hyphen) = crate ✓
This matches §18 (firmware renamed to underscore). Confirmed against the crate README
(`/home/dustin/projects/qmk-notifier`), which references `qmk_notifier` (underscore) as the
firmware. **No change needed.** Do NOT alter the naming note.

## File inventory (item 3d) — N/A
The firmware README has NO file-inventory / file-size table (grep finds only
pattern_match.c/notifier.c mentions in the Running Tests prose, not an inventory). §17's
file-size table lives in the PRD (item 3f: "no README change needed for line counts unless
the README references them" — it does not). ALSO: `qmk_module.json` is ABSENT (P1.M1.T1
FAILED — `ls qmk_module.json` → not found; not gitignored, a genuine gap). So there is
nothing to add and nothing truthfully to list. **N/A — no change.**

## Feature list / capability overview (item 3e) — RECOMMENDED add
The `## Features` list (matching, layers, callbacks, multi-OS, host-rules) does not mention
the Community Module distribution option. 3e says "should mention". Recommend adding one
bullet surfacing the Community Module install + linking to Setup. Low-risk, genuinely useful.
Exact bullet given in the PRP.

## Sections verified CLEAN (no stale submodule refs)
- `## Compatibility with Other Raw HID Modules` (only the 0x81/0x9F coexistence guard) ✓
- `## Contributing` (generic) ✓
- `## Running Tests` / `## Documentation` / `## Performance` ✓
- `## How It Works` / `### Wire format` ✓
- `## Host-Side Rules & Typed Commands` (uses DEFINE_HOST_CALLBACKS, no include paths) ✓

## Scope boundaries (no conflicts)
- P1.M2.T1.S1 (COMPLETE): `## Setup` rewrite. DO NOT touch Setup.
- P1.M2.T2.S1 (parallel): runs host gates + R6 check, writes no docs. No overlap.
- P1.M1.T1 (qmk_module.json): FAILED/absent — not my task to create.
- THIS task: README only — (1) fix Multi-OS snippet include [mandatory], (2) add Features
  bullet [recommended], (3) verify Companion Projects naming [no change], (4) document 3d/3f N/A.

## Validation approach (markdown sweep)
1. grep: NO stale relative include anywhere (`grep -n 'include "\./'` → 0 hits;
   `grep -n './qmk_notifier/notifier.h'` → 0 hits).
2. grep: Multi-OS snippet now has `#include "notifier.h"`.
3. grep: Setup section unchanged (still the Community Module flow — no accidental regression).
4. grep: Companion Projects naming intact; Features bullet present.
5. git diff scope: only README.md changed; the diff is confined to the Multi-OS snippet + the
   Features bullet.
6. Gates unaffected by a markdown change (P1.M2.T2.S1 owns gate verification).