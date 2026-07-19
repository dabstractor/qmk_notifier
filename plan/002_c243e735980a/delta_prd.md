# DELTA PRD — Multi-OS Map Selection (opt-in overlay)

**Base:** the completed qmk-notifier codebase (original PRD, session
`001_e329fbe4ae4d`). Pattern matching engine, basic notifier, the 9-suite
corpus, and the stub harness (`qmk_stubs/`, `test_notifier_dispatch.c`,
`run_notifier_stub_tests.sh`) are all **done and green**. This delta layers a
single new capability on top; it does **not** re-implement anything.

---

## 1. What Changed (diff summary)

The revised PRD adds **one feature**: per-OS command/layer maps selected by the
detected host OS, with the default map as a per-track fallback. Everything else
in the PRD is either context, forward-looking (HELD), or acceptance criteria for
this feature.

**Added requirements (in scope):**
- **§2 F8** — Multi-OS map selection (opt-in overlay): `os_variant_t current_os`
  (QMK's enum, reused — `OS_UNSURE/0 … OS_IOS/4`), `notifier_set_os()` as the
  sole mutation point, `DEFINE_SERIAL_COMMANDS_OS`/`DEFINE_SERIAL_LAYERS_OS`
  macros, and the **per-track OS-first / default-fallback** dispatch rule.
- **§2 F9** — OS-change state clearing: `notifier_set_os` is idempotent on an
  unchanged value; on a changed value it clears state (disable_command +
  deactivate_layer) and does **not** re-dispatch.
- **§5.1/§5.2/§5.5** — `notifier.h`: `#include "os_detection.h"`, the
  `notifier_set_os` decl, and the two `_OS` map macros (token-paste `##os`).
- **§8.1/§8.3/§8.6/§8.7** — `notifier.c`: `current_os` global, per-OS weak
  accessor pairs + `select_command_map_os`/`select_layer_map_os` selectors,
  the OS-first/fallback scan in `process_full_message`, and `notifier_set_os`.
- **§10.3** — Multi-OS reference configuration (keymap wiring via
  `process_detected_host_os_kb`, `OS_DETECTION_ENABLE`).
- **§13 invariants 15–21** — design constraints that the implementation must
  preserve (merge/fallback per track, OS_UNSURE⇒default, push-only OS, set_os
  idempotence, default-only == pre-multi-OS byte-identical, per-OS weak
  symbol names must match `##os`, orthogonality to the `0xF0` namespace).

**Added test infrastructure (in scope):**
- `qmk_stubs/os_detection.h` — header-only `os_variant_t` enum stub (needed for
  `notifier.c` to compile in the stub harness; the module uses the **type only**,
  never `detected_host_os()`).
- `test_notifier_os.c` — F8/F9 host tests.
- `run_notifier_stub_tests.sh` — extend to build+run `test_notifier_os` too.
- Acceptance gate **§11.2D**.

**Documentation (in scope):**
- **Mode A** — doc comments ride with each implementing file (notifier.h macro
  comments, notifier.c inline comments on the OS dispatch and `##os` naming).
- **Mode B** — `README.md` multi-OS section (final, depends on all).

**Explicitly NOT in scope (HELD — do NOT implement):**
- **§4.7 / §14.1** — host-provided OS as authoritative source (requires a
  `0xF0` typed `SET_OS` command across all three repos + capability
  handshake). Documented only so the current design stays non-breaking.
- **§14.1 B2** — full host-rule replacement vs. stack semantics (unresolved
  divergence from `qmkonnect/PRPs/002`). OPEN, next cycle.
- The `0xF0` typed-command namespace, `host_layer`, host-callback trackers.

**Backward compatibility (prime directive):** a default-only keymap (zero
`DEFINE_*_OS` macros) MUST be byte-for-byte equivalent to the pre-multi-OS
firmware — all per-OS accessors return `{NULL,0}`, every dispatch uses the
default maps exactly as before (invariant 19).

---

## 2. Prior Work to Leverage

- **Original session research** lives in `plan/001_e329fbe4ae4d/architecture/`
  (`notifier_architecture.md`, `external_deps.md`, `findings_and_risks.md`).
  The QMK weak-symbol / `SRC +=` / `os_detection` claims there are all
  validated — do not re-research.
- **RISK-1 is already closed:** the original session created the `qmk_stubs/`
  harness, `test_notifier_dispatch.c`, and `run_notifier_stub_tests.sh`. This
  delta **extends** that harness (adds `os_detection.h` to the stubs, adds a
  second test binary, widens the runner) rather than building it anew.
- `notifier.c` currently `#include`s `pattern_match.c` directly and has no
  file-scope mutable state beyond `nfa_gen` (in pattern_match.c). Adding
  `current_os` is the first notifier-specific mutable global; that is fine
  (single-threaded in QMK, push-only).

---

## 3. DELTA SCOPE

### Phase P1 — Multi-OS Map Selection (opt-in overlay)

A single cohesive feature: per-OS maps + OS selector + state clearing, with
host-testable coverage and backward-compatible default behavior.

#### Milestone P1.M1 — Multi-OS API & Dispatch Core

The header surface, the stub header that lets `notifier.c` compile in the host
harness, and the `notifier.c` state machine / dispatch changes.

**Task P1.M1.T1 — `notifier.h` OS API surface** (grows ~42 → ~80 lines)
- Add `#include "os_detection.h"` immediately after `<stdbool.h>` (§5.1).
- Add `void notifier_set_os(os_variant_t os);` to the accessor block (§5.2).
- Add §5.5 macros `DEFINE_SERIAL_COMMANDS_OS(os, ...)` and
  `DEFINE_SERIAL_LAYERS_OS(os, ...)` using `##os` token-paste to emit
  `_notifier_command_map_##os`, `_notifier_get_command_map_##os`,
  `_notifier_get_command_map_##os##_size` (and the layer equivalents).
- *Mode A docs:* comment the `##os` naming contract (the generated symbols must
  match `notifier.c`'s `select_*_map_os()` and its weak defaults exactly), the
  row-struct parity with the default macros, and that `OS_UNSURE` has no
  OS-specific map by design.
- *Subtask P1.M1.T1.S1* — write the include, decl, and both macros.

**Task P1.M1.T2 — `qmk_stubs/os_detection.h`** (NEW, header-only)
- Minimal header-only stub of QMK's `os_detection.h` containing **only** the
  `os_variant_t` enum (`OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3,
  OS_IOS=4`). The module uses the **type only**; it never calls
  `detected_host_os()`, so no implementation is needed.
- This unblocks `notifier.c` (which now `#include`s it) in the stub-compile
  harness. The real QMK build pulls in the genuine header.
- *Subtask P1.M1.T2.S1* — write the enum stub.

**Task P1.M1.T3 — `notifier.c` multi-OS core** (grows ~404 → ~410 lines)
- Add `os_variant_t current_os = OS_UNSURE;` global with a comment noting it is
  push-only and the module never calls `detected_host_os()` (§8.1, invariant 17).
- Add **per-OS weak accessors** (8 pairs = 16 `__attribute__((weak))` functions
  returning `{NULL, 0}`) and the `select_command_map_os`/`select_layer_map_os`
  helpers that switch on `current_os` and resolve `{NULL,0}` for `OS_UNSURE` /
  unexpected (§8.3).
- Update `process_full_message` (§8.6): resolve OS + default maps for both
  tracks; **command track** scans the OS command map first (a match there wins
  and the default command map is **not** scanned), else falls back to the
  default command map; **layer track** makes the same decision **independently**.
  Ordering invariants are unchanged (disable-before-scan,
  deactivate-before-activate, first-match-wins).
- Add `notifier_set_os(os)` (§8.7): idempotent on `os == current_os` (no-op); on
  a change, set `current_os`, `disable_command()`, `deactivate_layer()`, and do
  **not** re-dispatch.
- *Mode A docs:* comment the OS-first/fallback rule per track, the `##os` symbol
  parity with the header macros, and the F9 clear-on-change/idempotence contract.
- *Subtask P1.M1.T3.S1* — per-OS weak accessors + `select_*_map_os` selectors.
- *Subtask P1.M1.T3.S2* — `notifier_set_os` + `process_full_message` dispatch
  update (depends on P1.M1.T3.S1, P1.M1.T1.S1, P1.M1.T2.S1).

#### Milestone P1.M2 — Host Test Harness, Acceptance & Docs

**Task P1.M2.T1 — `test_notifier_os.c`** (NEW, ~150 lines)
- Stub-link against `notifier.c` (same `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'
  -Iqmk_stubs -I.` flags as `test_notifier_dispatch`). Cover §11.2D minimums:
  (i) OS-specific map selected and default skipped when `current_os` is set and
  the OS map matches — per track; (ii) default map used as fallback when the OS
  map is absent, matches nothing, or `current_os == OS_UNSURE`; (iii) command and
  layer tracks fall back **independently**; (iv) `notifier_set_os` idempotent on
  an unchanged value (no spurious `on_disable`/deactivate); (v) `notifier_set_os`
  on a **changed** value clears state (prev `on_disable` fires, active layer
  deactivated) and does **not** re-dispatch; (vi) default-only config behaves
  identically with/without `notifier_set_os` (backward compat).
- Use the same `test_case_t`/`PASS:`/`FAIL:` + summary pattern as the other
  suites so `grep -c '^FAIL:'` works.
- *Subtask P1.M2.T1.S1* — write the suite (depends on P1.M1.T3.S2).

**Task P1.M2.T2 — Extend `run_notifier_stub_tests.sh` + acceptance gate**
- Add a compile+link+run step for `test_notifier_os` (mirroring the existing
  dispatch step) so the runner builds both binaries and asserts 0 `FAIL:` lines
  each, ending `✓ notifier stub-compile gate PASSED` (grows ~37 → ~45 lines).
- **Re-verify `test_notifier_dispatch.c` still passes** (its default-only
  behavior is unchanged by multi-OS; expect no edits, but confirm). If a tweak
  is needed, keep changes scoped to the new OS surface.
- Run the full gate: `./run_all_tests.sh` (the 9 pattern_match suites, still
  1826 assertions / 0 failures) **plus** `./run_notifier_stub_tests.sh`
  (dispatch + os, 0 failures) — §11.2A/B/C/D all green.
- *Subtask P1.M2.T2.S1* — extend the runner and validate §11.2D (depends on
  P1.M2.T1.S1).

**Task P1.M2.T3 — Sync changeset-level documentation** *(Mode B, final)*
- **`README.md`:** add a multi-OS section documenting the opt-in nature, the
  `DEFINE_SERIAL_*_OS` macros, `notifier_set_os` / `process_detected_host_os_kb`
  wiring, `OS_DETECTION_ENABLE = yes`, the per-track OS-first/default-fallback
  rule, and the **backward-compatibility guarantee** (default-only ==
  pre-multi-OS). Note that the OS signal is push-only (no `detected_host_os()`
  link dependency) and that host-provided OS is a planned future enhancement.
- Update the test/acceptance blurb to mention the `notifier` stub suites.
- Depends on all prior tasks (this is the final cross-cutting doc sync).
- *Subtask P1.M2.T3.S1* — update README.md (Mode B).

---

## 4. Dependencies

```
P1.M1.T1.S1 (notifier.h macros/decls) ──┐
P1.M1.T2.S1 (os_detection.h stub) ──────┤
                                        ├─► P1.M1.T3.S2 (set_os + dispatch)
                            P1.M1.T3.S1 (weak accessors + selectors) ┘
                                                       │
                                                       ▼
                                            P1.M2.T1.S1 (test_notifier_os)
                                                       │
                                                       ▼
                                            P1.M2.T2.S1 (runner + gate §11.2D)
                                                       │
                                                       ▼
                                            P1.M2.T3.S1 (README sync — Mode B)
```

Pattern-matching corpus (P1 of the original session) is **untouched** and must
stay green throughout.

---

## 5. Definition of Done (delta)

- [ ] `notifier.h` exposes `os_detection.h`, `notifier_set_os`, and both
      `DEFINE_SERIAL_*_OS` macros (§5.1/§5.2/§5.5).
- [ ] `notifier.c` has `current_os`, the per-OS weak accessors + selectors,
      `notifier_set_os` (idempotent + clear-on-change), and the per-track
      OS-first/default-fallback scan in `process_full_message` (§8.1/§8.3/§8.6/§8.7).
- [ ] `qmk_stubs/os_detection.h` exists (header-only `os_variant_t` enum).
- [ ] `test_notifier_os.c` exists and covers all six §11.2D minimums.
- [ ] `./run_notifier_stub_tests.sh` builds + runs **both** `test_notifier_dispatch`
      and `test_notifier_os` with 0 `FAIL:` lines each, and ends
      `✓ notifier stub-compile gate PASSED`.
- [ ] `./run_all_tests.sh` still reports 1826/1826, 0 failures (no regression).
- [ ] A **default-only** configuration (no `DEFINE_*_OS`) behaves byte-identically
      to the pre-multi-OS firmware (invariant 19) — verified by a stub test.
- [ ] Invariants 15–21 hold; §4.7 / §14.1 (host-provided OS, host-rule
      replacement) are **not** implemented.
- [ ] `README.md` documents the multi-OS feature and backward-compat guarantee.

---

*End of delta. The pattern-matching engine, basic receiver/dispatcher, rules.mk,
and the 9-suite corpus are unchanged and remain the living source of truth for
everything outside this feature.*