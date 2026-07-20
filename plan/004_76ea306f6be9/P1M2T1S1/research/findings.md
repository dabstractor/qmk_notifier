# Research Notes — P1.M2.T1.S1 (Replace submodule Setup flow with Community Module flow)

## ⚠️ REPO DISAMBIGUATION (read first)
The harness cwd is `/home/dustin/projects/qmk-notifier` (the **Rust transport-crate**
repo, remote `dabstractor/qmk-notifier`, hyphen; Cargo.toml + src/*.rs; its README is
a CLI/crate install guide with NO firmware Setup section). **This task targets the
sibling FIRMWARE module repo** `/home/dustin/projects/qmk_notifier` (remote
`dabstractor/qmk_notifier`, underscore). Evidence:
- `plan/004_76ea306f6be9/tasks.json` + the prior PRPs (P1M1T2S1, P1M1T3S1) + the
  `architecture/` dir (system_context.md, external_deps.md) live ONLY in the firmware
  repo. The crate repo's plan/004 is a stray (only a misplaced P1M1T1S1/PRP.md).
- The firmware repo HAS the `## Setup` section (README:49-98) the contract cites, the
  module-context `rules.mk` (M1.T2 done), and the `notifier.c` API guard (M1.T3 done).
- `system_context.md` confirms: "cwd: `/home/dustin/projects/qmk_notifier`".

**Action:** operate in the firmware repo. PRP + research are written to
`/home/dustin/projects/qmk_notifier/plan/004_76ea306f6be9/P1M2T1S1/` (where the
orchestrator's tasks.json + prior PRPs live — where the implementer will look).

## Task scope
Rewrite the firmware module README's `## Setup` section (currently the **submodule
flow**, README:49-98) to the **Community Module flow** per PRD §18.3 R5. README-only;
no code/build change (R6). This is the changeset-level doc task (Mode B) that
documents the user-facing install narrative spanning the whole §18 delta.

## Ground-truth: current Setup section (firmware README.md, 574 lines)
- `## Setup` at **line 49**; next heading `## Usage` at **line 100**. So the section
  body to replace is **lines 49-98** (the contract's range is exact).
- **Step 1 (51-56)**: `git submodule add https://github.com/dabstractor/qmk_notifier.git qmk_notifier`
  cloned INTO the keyboard dir (`/path/to/qmk_firmware/keyboards/your_keyboard`).
- **Step 2 (58-84)**: `#include "./qmk_notifier/notifier.h"` (RELATIVE path) + the
  `raw_hid_receive → hid_notify` shim (60-72); a Multi-OS note (74-76) linking
  `#multi-os-configuration`; a Host-rules note (78-84) linking
  `#host-side-rules--typed-commands` + DEFINE_HOST_CALLBACKS.
- **Step 3 (86-97)**: `include keyboards/handwried/[manufacturer]/[keyboard_name]/qmk_notifier/rules.mk`
  + `OS_DETECTION_ENABLE = yes` (multi-OS only).
- The Multi-OS (74-84) + Host-rules notes are ACCURATE and must be PRESERVED (the
  contract: "Multi-OS users still set OS_DETECTION_ENABLE... Host-rules users still
  define DEFINE_HOST_CALLBACKS — unchanged").

## What changes (R5) — submodule flow → module flow
- **Step 1 (clone):** clone into the USERSPACE at `modules/<org>/qmk_notifier`
  (NOT into the keyboard dir).
- **Step 2 (keymap.json):** one `"modules": ["<org>/qmk_notifier"]` entry.
- **Step 3 (keymap.c):** `#include "notifier.h"` with NO relative path (module dir is
  on `-I` via VPATH — §18.2) + the `raw_hid_receive → hid_notify` shim (STILL required).
- **Gone:** the in-keyboard clone, the `#include "./qmk_notifier/notifier.h"` relative
  path, the `include .../rules.mk` line, the `SRC +=`/`RAW_ENABLE` hand-wiring.
- **Still required (§18.2 hard limit):** the `raw_hid_receive` shim — `raw_hid_receive`
  is NOT a Community Module hook, so the user MUST still define it. State as a callout.
- **Leaf-directory naming contract (R4):** the leaf MUST be hyphen-free
  (`qmk_notifier`, underscore). A hyphenated leaf is a HARD FAILURE (the generated
  `-DCOMMUNITY_MODULE_<LEAF>_ENABLE` + `<leaf>` hooks would be invalid C tokens).
  Prominent callout required.
- **Configurator note (§18.4):** Community Modules can't be built by the QMK
  Configurator; this module also needs a custom `raw_hid_receive` in keymap.c (which
  JSON keymaps can't express), so Configurator was never an option.
- **Multi-OS:** still `OS_DETECTION_ENABLE = yes` (in the keymap's rules.mk) +
  `notifier_set_os` from `process_detected_host_os_kb`. PRESERVE the existing note
  (just move it under the new step 3 / wire section).
- **Host-rules:** still `DEFINE_HOST_CALLBACKS`. PRESERVE the existing note.

## M1 artifact status (firmware repo, verified this session)
- `rules.mk` (P1.M1.T2.S1 — DONE): module-context form `RAW_ENABLE = yes` +
  `SRC += notifier.c`. ✓ (The README no longer tells users to write the SRC/RAW_ENABLE
  lines — the module's own rules.mk + discovery handle them.)
- `notifier.c` API guard (P1.M1.T3.S1 — LANDED): `#ifdef COMMUNITY_MODULES_API_VERSION`
  + `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0)` present. ✓ (Internal — the README
  doesn't mention it.)
- `qmk_module.json` (P1.M1.T1.S1 — Planned, NOT yet created): absent. The README
  documents the flow regardless; qmk_module.json's creation is a separate task. (The
  contract INPUT claims "all M1 artifacts in place" — qmk_module.json is the exception;
  it doesn't block the README rewrite.)

## Cross-task (no conflict)
- **P1.M1.T3.S1** (parallel, LANDED): added the API guard to `notifier.c`. README-only
  task does NOT touch notifier.c. Zero overlap.
- **P1.M2.T2.S1** (LATER): re-run the host gates to confirm R6 (no behavioral change).
  This README task is documentation-only; R6 verification is P1.M2.T2.S1's job.
- **P1.M2.T3.S1** (LATER): "Sweep README and overview docs for stale submodule
  references." My task rewrites the Setup SECTION; P1.M2.T3.S1 sweeps the REST of the
  README/overview for any other stray submodule mentions (e.g. the repo-overview
  bullets). Clean division — I rewrite Setup; P1.M2.T3.S1 mops up other mentions.

## The exact new Setup content (drafted, validated against §18.3 R5)
See the PRP "Implementation Tasks → Exact code". Key invariants:
- One `git submodule add ... modules/<org>/qmk_notifier` (userspace, underscore leaf).
- One `keymap.json` `"modules": ["<org>/qmk_notifier"]` entry.
- `#include "notifier.h"` (no relative path) + the `raw_hid_receive` shim.
- Leaf-naming callout (R4), raw_hid_receive-shim callout (§18.2), Configurator note (§18.4).
- Multi-OS + Host-rules notes preserved (same `#multi-os-configuration` /
  `#host-side-rules--typed-commands` anchor links, so existing in-README links stay valid).

## Validation
- README is Markdown — no compiler. Validation = (a) the old submodule artifacts are
  GONE (grep for `git submodule add`/`./qmk_notifier/notifier.h`/`include keyboards`/
  `SRC +=` in the Setup region), (b) the new module-flow artifacts are PRESENT
  (`modules/<org>/qmk_notifier`, `keymap.json`, `#include "notifier.h"`, the leaf
  callout, the raw_hid_receive callout, the Configurator note), (c) the preserved
  Multi-OS/Host-rules anchor links still resolve, (d) no other README section moved.
- Host gates (run_all_tests.sh / run_notifier_stub_tests.sh) are UNAFFECTED (README
  change only) — R6 verification is P1.M2.T2.S1's scope, not this task's.

## Files touched / NOT touched
- TOUCH: firmware repo `README.md` (the `## Setup` section, lines 49-98 ONLY).
- DO NOT TOUCH: notifier.c, notifier.h, pattern_match.*, rules.mk, qmk_module.json,
  any test, run_*.sh, PRD.md, tasks.json, prd_snapshot.md, .gitignore.
- This IS the documentation task (item §6 Mode B).