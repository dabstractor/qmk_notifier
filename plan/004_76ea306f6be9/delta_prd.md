# DELTA PRD — Community Module Distribution (Planned Migration)

**Delta from:** plan `003_16d737de7a3e` (Host-Side Rules & Typed Commands — complete).
**Baseline gate (re-verified this session):** `run_all_tests.sh` green (pattern_match
corpus, ~0.11 µs/call); `run_notifier_stub_tests.sh` green — dispatch/os/host all 0
fails, `✓ gate PASSED`.

---

## What actually changed between the two PRDs (diff analysis)

A line-by-line comparison shows **three** classes of change. Two require **no code
work**; one is the genuine new feature.

### A. The `qmk-notifier` → `qmk_notifier` rename — ALREADY DONE (verify only)

The previous PRD named this repo `qmk-notifier` (hyphen) and the Rust transport
crate `qmk_notifier` (underscore). The current PRD **swaps** the convention: the
firmware C module (this repo) is now `qmk_notifier` (underscore); the Rust
transport crate is now `qmk-notifier` (hyphen). This rename is pervasive in the
spec (§1.1, §1.2 table, §1.2 naming note, §4 intro, §4.1, §4.3, §4.4, §4.5, §4.6,
§4.7, §9, §10.1, §16, §17, end-note) but **mechanical**.

**Verified this session — the rename is complete in the codebase; no work remains:**
- cwd is `/home/dustin/projects/qmk_notifier`; `git remote` is `dabstractor/qmk_notifier`.
- `rules.mk` already has `SRC += qmk_notifier/notifier.c`.
- `grep -rn "qmk-notifier"` over the source/tests (excluding `plan/`) → **0 hits**.
- `README.md` already uses the new convention (`qmk_notifier` firmware /
  `qmk-notifier` Rust crate) and the underscore submodule paths.

> **Per §18.1, the rename is the prerequisite for the community-module
> migration** — a hyphen is not a valid C identifier, so the old slug could never
> be a QMK module leaf name. The rename being done is what makes §18 implementable.

### B. §15 host-rules matcher parity note — DOC-ONLY, no firmware work

The previous §15 note said the host-side matcher (which lives in `qmkonnect`, **not
this repo**) needed only a "stable subset" (`*`, `^`, `$`, `WT`). The current §15
note says the host-side matcher is a **full-parity port** (adds `+` and the classes
`\d \w \s \b .`). This is an editorial correction about `qmkonnect`'s matcher.

**No firmware code change.** This repo's `pattern_match.c` is already the full
feature set; the note is informational. (No task created.)

### C. §18 "Community Module Distribution (Planned Migration)" — THE NEW FEATURE

An entirely new section (~120 lines) specifying a **planned migration** from
keymap-local git submodule to **QMK Community Module** distribution. It carries
6 requirements (R1–R6), verified build-system mechanics (§18.2, ground truth from
`qmk_firmware`), 3 open decisions (§18.4), and acceptance criteria (§18.5).

**Current state (verified): no part of §18 is implemented** — `qmk_module.json`
absent, no `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` guard in `notifier.c`.

---

## Scope of this delta

**One phase** — implement the §18 Community Module Distribution requirements
(R1–R6). The rename and the §15 note require no work (above). The wire protocol
(§4), the matcher (§7), the dispatch/OS/host-rules logic (§8/§14), and the public
API in `notifier.h` must stay **byte-for-byte unchanged** (§18.3 R6 / §18.5).

### Environment available (verified)
- `qmk` CLI at `/usr/bin/qmk`; `qmk_firmware` checkout at `~/projects/qmk_firmware`.
  ⇒ A community-module build **can** be validated locally given a userspace + a
  module-listing keymap. (If a userspace is not set up, fall back to `qmk lint` +
  the host-test gates, and mark the full-build criterion deferred.)

### Open decisions to resolve while implementing (§18.4)

| Decision | PRD recommendation | This delta's action |
|---|---|---|
| **OS auto-wiring** (`process_detected_host_os` is a module hook → could auto-call `notifier_set_os(detected_host_os())`) | **Keep push-only** (preserves the §1.1/§8.7 invariant; avoids forcing `OS_DETECTION_ENABLE` on every user) | **Do not auto-wire.** Keep `notifier_set_os` as the sole, keymap-driven OS seam. |
| **Legacy submodule flow** (R2's module-context `rules.mk` is incompatible with the old clone-into-keyboard + `SRC += path/...` flow) | **Retire the submodule flow** | **Retire it.** `rules.mk` becomes module-context (R2); README documents only the module flow (R5). |
| Configurator support | Irrelevant (this module needs a custom `raw_hid_receive`, which JSON keymaps can't express) | No action; note in README. |

---

# Phase P1 — Community Module Distribution (Planned Migration)

> **Status of the feature in the PRD:** "planned, not yet implemented … the
> current build (§9, §10.1) remains the submodule flow until this section is
> marked done." This phase **implements** §18's R1–R6 so the section can be marked
> done. Nothing here touches the wire protocol, the matcher, or runtime behavior.

## Milestone M1 — Module build-system plumbing (R1, R2, R3)

The three pieces that give the module its self-declaring QMK identity: the
manifest, the module-context `rules.mk`, and the API-version guard. After M1 the
module is structurally consumable as a Community Module; M2 adds docs + verifies
no behavioral regression.

### Task P1.M1.T1 — Add `qmk_module.json` manifest (R1)
**Subtask P1.M1.T1.S1 — Create `qmk_module.json` at repo root** (1 pt)
- **CONTEXT/LOGIC:** Per §18.3 R1, add `qmk_module.json` with fields
  `module_name` = `QMK Notifier`, `maintainer` = `dabstractor`,
  `url` = `https://github.com/dabstractor/qmk_notifier`, `keycodes` = `[]`, and
  `license` = the repo's real SPDX identifier.
  - **Confirm the SPDX license** from the repo's actual `LICENSE`/headers before
    writing the string (this is the one field the PRD leaves as TBD). Must be
    GPL-compatible (QMK is GPLv2+).
  - **No `features` block** — the module declares no data-driven feature keys
    (`RAW_ENABLE` is **not** data-driven; §18.2 confirms no `rawhid` schema entry
    exists, so it goes in `rules.mk`, not here).
  - **No `keycodes`** — the public surface is macros + functions invoked from the
    keymap, not keymap-bindable keys.
- **OUTPUT:** `qmk_module.json` present at root. `qmk lint` (against a userspace
  build that lists the module, if available) is clean.
- **DOCS (Mode A — rides with work):** The manifest fields are self-documenting.
  Use the repo's real SPDX identifier; do not leave any TBD placeholder.

### Task P1.M1.T2 — Rewrite `rules.mk` to module context + retire submodule flow (R2)
**Subtask P1.M1.T2.S1 — Change `rules.mk` to the module-context form** (1 pt)
- **CONTEXT/LOGIC:** Per §18.3 R2 and the §18.4 "retire submodule flow" decision,
  replace the current submodule-context line `SRC += qmk_notifier/notifier.c`
  with the bare module form: keep `RAW_ENABLE = yes` and change the SRC line to
  `SRC += notifier.c`.
  - `notifier.c` does **not** match the leaf name `qmk_notifier`, so the
    generator's wildcard over `{module_path}/{leaf}.c` does **not** auto-compile
    it (§18.2) — it must be listed explicitly.
  - `pattern_match.c` is pulled in by `notifier.c`'s `#include "pattern_match.c"`
    (resolved via the generator's `VPATH += {module_path}`, which §18.2 confirms
    is on the compiler include path) — **not** a separate `SRC` entry.
  - `RAW_ENABLE = yes` stays here (not in `qmk_module.json` features) because it
    is not a data-driven feature key (§18.2 hard limit).
- **IMPACT — this retires the legacy submodule flow:** the old
  `include keyboards/{...}/qmk_notifier/rules.mk` path no longer works (the
  module-context `SRC += notifier.c` is a bare name, not keyboard-relative).
  This is the §18.4 "retire" decision; README (P1.M2.T1) documents only the
  module flow.
- **OUTPUT:** `rules.mk` is module-context. A module-listing keymap build
  compiles `notifier.c` (pulling in `pattern_match.c`) and turns on `RAW_ENABLE`
  with **no manual `rules.mk` include line** in the keymap.
- **DOCS (Mode A — rides with work):** One-line comment in `rules.mk` noting this
  is the Community-Module context (`SRC += notifier.c` via VPATH) and that
  `pattern_match.c` comes in through `notifier.c`'s `#include`.

### Task P1.M1.T3 — Add API-version assertion guard to `notifier.c` (R3)
**Subtask P1.M1.T3.S1 — Add the `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` guard** (1 pt)
- **CONTEXT/LOGIC:** Per §18.3 R3, add immediately after `#include QMK_KEYBOARD_H`
  (currently `notifier.c:2`) a guarded block:
  `#ifdef COMMUNITY_MODULES_API_VERSION` … `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);` … `#endif`.
  - **Why target 1.0.0:** the floor that provides `housekeeping_task` and
    `process_detected_host_os` (§18.3 R3).
  - **Host-test safety (load-bearing):** the stub harness builds with
    `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'`, where neither
    `COMMUNITY_MODULES_API_VERSION` nor `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION`
    is defined — so the `#ifdef` skips the assert and **all stub binaries stay
    green unchanged**. Verify `run_notifier_stub_tests.sh` still reports 0 fails.
  - In a real module build, `QMK_KEYBOARD_H` → `quantum.h` → `community_modules.h`
    defines both the macro and the assert, so it fires (§18.3 R3).
- **OUTPUT:** The guard is present; the three notifier stub binaries (dispatch /
  os / host) re-pass with **0 FAIL:** lines unchanged; a real module build asserts
  API ≥ 1.0.0.
- **DOCS (Mode A — rides with work):** Comment the guard block: target version,
  why it is `#ifdef`-gated (host/stub tests define neither symbol), and cite
  §18.3 R3.

## Milestone M2 — Docs rewrite + verification (R4, R5, R6)

The README install rewrite (the only place the user-facing flow changes) and the
no-regression verification. This milestone **depends on M1** (the README documents
the manifest + module-context `rules.mk` + guard that M1 lands).

### Task P1.M2.T1 — Rewrite README install/setup for the module flow + leaf-naming contract (R4, R5)
**Subtask P1.M2.T1.S1 — Replace the submodule Setup flow with the Community Module flow** (2 pts)
- **DEPENDS ON:** P1.M1.T1, P1.M1.T2, P1.M1.T3 (the flow documents what M1 lands).
- **CONTEXT/LOGIC:** Per §18.3 R5, replace the existing "Setup" submodule flow
  (README currently shows `git submodule add ... qmk_notifier` into the keyboard
  dir + `include keyboards/{...}/qmk_notifier/rules.mk` + the `SRC +=`/`RAW_ENABLE`
  hand-wiring) with the module flow:
  - clone into the userspace at `modules/{org}/qmk_notifier`;
  - add one `"modules": ["{org}/qmk_notifier"]` entry to `keymap.json`;
  - in `keymap.c` include `notifier.h` with **no relative path** (the module dir
    is on `-I` via VPATH) and define the `raw_hid_receive → hid_notify` shim.
  - **Gone vs. today:** the in-keyboard clone, the
    `#include "./qmk_notifier/notifier.h"` relative path, the
    `include .../rules.mk` line, and the `SRC +=`/`RAW_ENABLE` hand-wiring.
  - **Still required (the §18.2 irreducible limit):** the `raw_hid_receive` shim —
    `raw_hid_receive` is **not** in the community-module hook surface, so the user
    must still define it and call `hid_notify()`. State this explicitly.
  - **Multi-OS users** still set `OS_DETECTION_ENABLE = yes` and call
    `notifier_set_os` from `process_detected_host_os_kb` — unchanged.
- **Leaf-directory naming contract (R4):** state explicitly that users **MUST**
  clone to a hyphen-free leaf (recommended `modules/{org}/qmk_notifier`). A
  hyphenated leaf is a **hard failure** (the generated
  `-DCOMMUNITY_MODULE_{LEAF}_ENABLE` define and `{leaf}` hooks would be invalid C
  tokens). This is the one setup mistake the README must call out.
- **Configurator note:** Community Modules cannot be built by the QMK Configurator;
  this module requires a custom `raw_hid_receive` in `keymap.c` (which JSON keymaps
  cannot express), so Configurator was never an option (§18.4).
- **OUTPUT:** README documents only the module flow; a user can install with one
  `"modules"` entry + the `raw_hid_receive` shim. The leaf-name contract is stated.
- **DOCS:** This **is** the Mode B (changeset-level) documentation task — it
  depends on all of M1 and rewrites the user-facing install narrative.

### Task P1.M2.T2 — Verify no behavioral change: host gates green + R6 invariant (R6)
**Subtask P1.M2.T2.S1 — Re-run the full §11.2 host gate; confirm module flow is byte-identical** (1 pt)
- **DEPENDS ON:** P1.M1.T3 (the guard must be host-test-safe), P1.M2.T1.
- **CONTEXT/LOGIC:** Per §18.3 R6 and §18.5, the wire protocol (§4), the matcher
  (§7), the dispatch/OS/host-rules logic (§8/§14), and the `notifier.h` public API
  are byte-for-byte unchanged. Verify:
  1. `./run_all_tests.sh` → 9 pattern_match suites, **0 failures**, perf sub-second.
  2. `./run_notifier_stub_tests.sh` → dispatch/os/host each **0 FAIL:**,
     `✓ gate PASSED`. (This is the direct proof that the R3 `#ifdef` guard did not
     perturb the stub build — the load-bearing host-test-safety claim.)
  3. If a QMK userspace + module-listing keymap is set up (`qmk` +
     `~/projects/qmk_firmware` are available): `qmk compile`/`qmk lint` the module
     keymap; confirm the `0x81 0x9F` endpoint is live with **no manual `rules.mk`
     include line** in the keymap, and that focus-change messages still switch
     layers / fire callbacks identically to today. **If no userspace is
     available**, mark this sub-criterion *deferred* and rely on (1)+(2) plus
     `qmk lint` manifest validity.
- **OUTPUT:** Full §11.2 gate green with the §18 changes in place; no regression.
  The module is structurally distributable and behaviorally identical.

---

## Acceptance (from §18.5, specialized to this delta)

- [ ] **R1** — `qmk_module.json` present at root, valid, GPL-compatible SPDX
      license confirmed from the repo; no `features`/`keycodes` blocks.
      `qmk lint` clean (or deferred if no userspace).
- [ ] **R2** — `rules.mk` is module-context (`RAW_ENABLE = yes` +
      `SRC += notifier.c`); the legacy submodule `include .../rules.mk` flow is
      retired.
- [ ] **R3** — `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0)` guard present
      in `notifier.c` after `#include QMK_KEYBOARD_H`, `#ifdef`-gated so host/stub
      binaries stay green.
- [ ] **R4** — README states the hyphen-free leaf-directory contract.
- [ ] **R5** — README documents only the Community Module flow (one `"modules"`
      entry + the irreducible `raw_hid_receive` shim); the submodule Setup flow,
      the relative `#include`, and the `rules.mk` include line are gone.
- [ ] **R6** — `run_all_tests.sh` and `run_notifier_stub_tests.sh` green with the
      §18 changes in place; wire protocol / matcher / dispatch / `notifier.h` API
      byte-for-byte unchanged. (Module-build validation run if a userspace is set
      up; else deferred with host gates as the local proof.)
- [ ] **Decisions resolved:** OS stays push-only (no auto-wire hook added);
      submodule flow retired.

## Out of scope for this delta (explicitly)

- **The rename** — already complete in the codebase (verified; §A above).
- **The §15 matcher-parity note** — host-side (`qmkonnect`) concern; no firmware
  change (§B above).
- **OS auto-wiring** — intentionally **not** added (§18.4; preserves the push-only
  invariant). If desired later, it would reverse §1.1/§8.7 and force
  `OS_DETECTION_ENABLE` on every user — a separate decision.
- Any change to the wire protocol (§4), the matcher (§7), or runtime behavior
  (§8/§14) — §18.3 R6 forbids it.