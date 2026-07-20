# Validation Report — qmk_notifier

**Validation date:** current session · **Validator:** automated (`./validate.sh`) + manual QMK E2E
**Repo state at validation:** branch `main`, 2 commits ahead of `origin/main`, working tree clean (aside from an external orchestrator touching `plan/004`, which is out of scope for this validation and was **not** caused by the validation commands).

## TL;DR

The firmware core is **excellent and production-ready**: the 10-suite pattern-matcher corpus (2029/2029 assertions), the 3-binary notifier stub gate (dispatch/os/host, all green), the PRD §11.2 acceptance micro-tests (pathological NFA = 1.5 ms < 50 ms gate; six realistic patterns all match; NULL-safe), strict `-Wall -Wextra` compiles with **zero warnings**, and a **real end-to-end QMK firmware build** (ATmega32U4/Pro Micro) all pass — including the full public API surface (`DEFINE_SERIAL_*`, `WT()`, per-OS maps, `DEFINE_HOST_CALLBACKS`).

The **only** problems are in the §18 *Community Module distribution* surface — specifically the packaging/docs layer that is mid-migration. Three hard failures and one warning were found, all concentrated there. The firmware logic itself has no defects found.

| # | Severity | Area | Issue |
|---|---|---|---|
| 1 | 🔴 **Blocker** | §18.3 R1 | `qmk_module.json` manifest is **MISSING** — the documented Community Module install flow does not work |
| 2 | 🟠 High | §18.3 R1 / task tracker | The provided backlog marks P1.M1.T1 "Complete"; ground truth (`plan/004/tasks.json`) says **"Failed"** |
| 3 | 🟡 Medium | PRD §18.3 R1 | PRD's manifest example (`"keycodes": []`) is **schema-invalid** |
| 4 | 🟠 High | §18.3 R5 / README | Stale submodule relative include in README Multi-OS section → **hard build failure** |
| 5 | 🟡 Low | §18.3 R1 / legal | No `LICENSE` file despite GPL license claim |
| — | ℹ️ Note | process | `has_been_queried` is intentionally write-only (by design, not a bug) |

---

## Methodology

The validation combined six layers:

1. **Existing host test suites** (`run_all_tests.sh` + `run_notifier_stub_tests.sh`) — the project's own gates.
2. **Strict compiler hygiene** — `gcc -Wall -Wextra -std=c99` on both `pattern_match.c` (standalone) and `notifier.c` (stub-compiled).
3. **PRD §11.2 acceptance micro-tests** — pathological NFA timing, realistic-pattern matches, NULL/garbage robustness.
4. **Real QMK firmware build** — set up a temp userspace per the README's documented flow and ran `qmk compile` against `~/projects/qmk_firmware` (QMK CLI 1.2.0) with `handwired/onekey`, exercising the full public API in-firmware.
5. **QMK module-discovery E2E** — invoked QMK's actual `community_modules.py` discovery/`load_module_json`/schema-validation code paths to capture the exact user-facing errors.
6. **Wire-protocol & doc-contract audit** — verified every §4/§16 constant against the source and swept the README for submodule-flow residue.

A reusable, executable gate encoding all of the above was written to **`./validate.sh`** (read-only to the repo; rebuilds into `/tmp`). It was verified to be a *true* gate: it fails on the current repo (exit 1) and goes fully green (exit 0) when the two real fixes are applied to a temp copy.

---

## 🔴 ISSUE 1 (Blocker) — `qmk_module.json` manifest is MISSING

**PRD ref:** §18.3 R1, §18.5 acceptance ("`qmk_module.json` present and valid"), §18.2 ground truth.
**Task ref:** P1.M1.T1 / P1.M1.T1.S1 ("Create qmk_module.json at repo root").

### What's wrong
The `qmk_module.json` manifest does **not exist** anywhere in the repo:

```
$ ls qmk_module.json
ls: cannot access 'qmk_module.json': No such file or directory
$ git ls-files | grep -i module   # nothing tracked
```

### Why it matters (proof)
The README's **entire Setup section** documents the Community Module install flow:

> *"qmk_notifier is installed as a QMK Community Module: the build discovers the module's rules.mk, its sources, and its include path automatically from a single keymap.json entry."*

That claim is **false as shipped**, because QMK's module discovery globs for `qmk_module.json` to enumerate available modules (`lib/python/qmk/community_modules.py:55`, `COMMUNITY_MODULE_JSON_FILENAME = 'qmk_module.json'`). I set up a real userspace exactly per the README and ran QMK's discovery:

```
QMK module discovery → Discovered: [ ...5 built-in qmk modules... ]
                      → qmk_notifier discoverable: False
load_module_json('dabstractor/qmk_notifier') → FileNotFoundError: Module not found
```

A user who follows the README hits `FileNotFoundError: Module not found: dabstractor/qmk_notifier` at the very first `qmk compile`. The documented install path is **non-functional**.

### Fix verification (isolated, non-mutating)
I added a correct manifest to a *temp copy* and re-ran the identical QMK flow:
- Module discovery → **found** `qmk_notifier`.
- `qmk compile -kb handwired/onekey -km notifiertest` → **full firmware build succeeded** (19298/28672 bytes, `.hex` produced), `notifier.c` compiled `[OK]`, the R3 `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0)` passed against the real QMK 1.1.2 API, and the full public API (`DEFINE_SERIAL_*`, `WT()`, per-OS maps, `DEFINE_HOST_CALLBACKS`, `process_detected_host_os_kb`) compiled and linked clean.

**Conclusion:** the manifest is the *single* missing piece for the Community Module distribution; everything else (R2 rules.mk, R3 API guard, VPATH includes, hook limits) is already correct and proven by a real build.

### Recommended fix
Create `qmk_module.json` at the repo root. **Omit `keycodes`** (see ISSUE 3):

```json
{
    "module_name": "QMK Notifier",
    "maintainer": "dabstractor",
    "license": "GPL-2.0-or-later",
    "url": "https://github.com/dabstractor/qmk_notifier"
}
```

---

## 🟠 ISSUE 2 (High) — Task tracker says "Complete"; ground truth says "Failed"

**Where:** the `backlog` provided in this validation request.

### What's wrong
The backlog provided for this validation marks:

> `P1.M1.T1 [Complete]: Add qmk_module.json manifest (R1)` · `P1.M1.T1.S1 [Complete]: Create qmk_module.json at repo root`

But the **authoritative** `plan/004_76ea306f6be9/tasks.json` records both as **`"Failed"`**:

```
P1.M1.T1:     status=Failed  Add qmk_module.json manifest (R1)
P1.M1.T1.S1:  status=Failed  Create qmk_module.json at repo root
```

…and the filesystem confirms the file is absent (ISSUE 1). The backlog snapshot used as input is **stale/incorrect** relative to ground truth.

### Why it matters
Acting on the stale "Complete" status, an implementer could believe the manifest work is done and ship a broken Community Module distribution. This is the root cause of ISSUE 1 going uncaught: the task was marked done without the artifact ever landing (the `f76478e`/`66f215f` commits cover README + verification, but no commit ever added the manifest).

### Recommended fix
Reconcile the task tracker with ground truth (P1.M1.T1 → re-open/`Failed` or `Planned`), then implement ISSUE 1.

---

## 🟡 ISSUE 3 (Medium) — PRD §18.3 R1 manifest example is schema-invalid

**PRD ref:** §18.3 R1 (example shows `"keycodes": []`).

### What's wrong
The PRD specifies the manifest with an empty keycodes array:

```json
"keycodes": []
```

But QMK's real schema (`data/schemas/definitions.jsonschema`) defines `keycode_decl_array` with `"minItems": 1`. Submitting `"keycodes": []` fails schema validation:

```
jsonschema.exceptions.ValidationError: [] should be non-empty
Failed validating 'minItems' in schema['properties']['keycodes']
```

### Why it matters
If ISSUE 1 is "fixed" by copying the PRD example verbatim, the manifest still won't load — `load_module_json()` runs `validate(module_json, 'qmk.community_module.v1')` which raises, aborting the build. (Notably, the *task context_scope* for P1.M1.T1.S1 already says "Do NOT include keycodes" — contradicting the PRD example. The PRD example is the stale artifact.)

### Recommended fix
Omit the `keycodes` field entirely (it's optional — only `module_name` + `maintainer` are required). Verified clean against the schema in isolation.

---

## 🟠 ISSUE 4 (High) — Stale submodule-flow `#include` in README Multi-OS section

**PRD ref:** §18.3 R5 ("README install rewrite"); this is exactly the `P1.M2.T3` "Sync changeset-level documentation" task, whose status is still `Researching`/`Ready` (not done).
**Location:** `README.md` line ~193, the "Multi-OS Configuration → How to enable" code block.

### What's wrong
The Setup section was correctly migrated to the Community Module form (`#include "notifier.h"`, resolved via VPATH), but the **Multi-OS section was not**. It still shows the old submodule relative path:

```c
#include QMK_KEYBOARD_H
#include "./qmk_notifier/notifier.h"   // ← OLD submodule path
```

### Why it matters (proof)
This is a **hard build failure** under the Community Module flow, not a stylistic nit. I built a real keymap using exactly that include:

```
fatal error: ./qmk_notifier/notifier.h: No such file or directory
    2 | #include "./qmk_notifier/notifier.h"
make: *** Error 1
```

The relative path resolves against the keymap directory, but in the module flow the module lives in `userspace/modules/<org>/qmk_notifier`, not inside the keyboard dir — so `./qmk_notifier/notifier.h` does not exist. A user who reads the Multi-OS example and copies the include gets a broken build.

### Recommended fix
Change line ~193 to match the Setup section:
```c
#include "notifier.h"   /* module dir on -I via VPATH — no relative path */
```
Then sweep the whole README (P1.M2.T3) for any other `./qmk_notifier/...` residue. (A grep confirms line ~193 is the only remaining stale relative include, but a full sweep is the safe completion of the task.)

---

## 🟡 ISSUE 5 (Low) — No `LICENSE` file despite GPL license claim

**PRD ref:** §18.3 R1 ("license must use the repo's real SPDX identifier … GPL-compatible").

### What's wrong
The manifest (once added per ISSUE 1) will declare `"license": "GPL-2.0-or-later"`, but there is **no `LICENSE`/`COPYING` file** anywhere in the repo:

```
$ ls LICENSE* COPYING*   → no such file
$ git ls-files | grep -iE 'license|copying'   → (none)
```

### Why it matters
A GPL license declaration without the actual license text is a real legal/distribution gap. QMK itself is GPLv2+, and its example modules ship with license headers + a `LICENSE`. Distributing this module (especially once it's a Community Module consumed by others' builds) without the license text is improper.

### Recommended fix
Add a `LICENSE` file (GPL-2.0-or-later full text) and add the standard SPDX header comment to `notifier.c`/`pattern_match.c`. (Low severity — does not block function or build.)

---

## ℹ️ NOTE (not a bug) — `has_been_queried` is intentionally write-only

`notifier.c` declares `static bool has_been_queried = false;`, sets it `true` on the first `QUERY_INFO`, and **never reads it**. This is flagged here only because a linter/static-analyzer might raise it. It is **correct by design** (PRD §4.6 handshake-timing rule): the firmware is required only to *set* the flag; the *host* enforces "at most once per board boot". The flag is reserved for future firmware-side observability and is deliberately not exposed in the (fixed 4-byte) `QUERY_INFO` reply. The code comment documents this thoroughly. **No action required.**

---

## What passed (confidence areas)

These were validated and are solid — listed so the reader knows the firmware core is not in question:

- **Pattern matcher (Thompson NFA):** 2029/2029 assertions across 10 suites; pathological `a+a+a+a+a+a+a+a+a+a+b` vs 199 `a`s → `result=0` in **~1.5 ms** (gate is 50 ms); six realistic patterns all match; linear-time, no catastrophic backtracking.
- **Receiver / dispatcher / host-rules:** stub gate green — `test_notifier_dispatch` (14 cases: reassembly, F4 delimiter matrix, ordering, ack, NULL safety), `test_notifier_os` (31 cases: F8 multi-OS selection/fallback per track, F9 clear-on-change, idempotence), `test_notifier_host` (94 cases: `QUERY_INFO`/`QUERY_CALLBACK`/`SET_OS`/`APPLY_HOST_CONTEXT` STACK vs REPLACE, callback-diff disable-before-enable, the §4.6 `0x03` cmd_id/ETX collision reassembly fix, adversarial framing recovery).
- **Build hygiene:** both TUs compile with `-Wall -Wextra -std=c99`, **zero warnings**.
- **Real firmware build:** `qmk compile` on `handwired/onekey` (ATmega32U4) succeeds; `notifier.c` compiles `[OK]`; the R3 API-version guard fires and passes (QMK API 1.1.2 ≥ 1.0.0); the R2 `rules.mk` (`SRC += notifier.c` + `RAW_ENABLE = yes`) and VPATH include resolution work; the full public API surface compiles and links in-firmware.
- **Wire-protocol contract:** every §4/§16 constant verified present in source — magic header `0x81 0x9F`, GS `0x1D`, ETX `0x03`, `RAW_REPORT_SIZE` = 32, `MSG_BUFFER_SIZE` = 256, `LAYER_UNSET` = 255, discriminator `0xF0`, response marker `0x51`.
- **Robustness:** NULL pattern/string and garbage bytes all return `false` without crashing (PRD §12 / F3).
- **NFA MCU budget:** the `#define NFA_MAX_PATTERN 128` override (with a compile-time `typedef ... [-1]` guard) lands before `#include "pattern_match.c"`, and a full firmware build on a stack-constrained AVR proves it is safe on hardware (fixes the earlier ~128 KB/call stack blowup).

---

## How to reproduce

```bash
./validate.sh      # read-only to the repo; rebuilds into /tmp; exits non-zero on failure
```

Current result: **3 hard failures, 1 warning, exit 1** — all concentrated in the §18 Community Module distribution surface (the manifest + a README stale include). With the two real fixes applied (add `qmk_module.json` without `keycodes`; fix the README relative include), the same script reports **0 failures, exit 0** (verified on an isolated temp copy).