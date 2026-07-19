# PRP — P1.M1.T3.S2: `notifier_set_os` + `process_full_message` OS-first/default-fallback dispatch

## Goal

**Feature Goal**: Wire the multi-OS dispatch **consumer** into `notifier.c`:
rewrite `process_full_message` so each track (command, layer) scans the
OS-specific map for `current_os` FIRST and falls back to the default map ONLY if
the OS map is absent or matches nothing — the two tracks deciding INDEPENDENTLY
(PRD §8.6 / §2 F8.4/F8.5) — and add the `notifier_set_os(os_variant_t os)`
selector (PRD §8.7 / §2 F9) that is the sole mutation point for `current_os`,
idempotent on an unchanged value, and clears all notifier state (without
re-dispatch) on a change.

**Deliverable**: The modified file `notifier.c` at the repo root
(`/home/dustin/projects/qmk-notifier/notifier.c`), changed in exactly TWO
regions: (1) the body of `process_full_message` (the dispatch rewrite + the F2
CONSOLE-block fix), and (2) one new function `notifier_set_os` inserted between
`process_full_message` and `hid_notify`. All new code is byte-exact vs the PRD
§8.6/§8.7 semantics and was **empirically validated during research** (see
Validation Loop + research/notes.md).

**Success Definition**:
- `process_full_message` resolves all four maps (`os_cmd`/`os_layer` via
  `select_*_map_os(current_os,…)` from S1; `def_cmd`/`def_layer` via the existing
  `get_*_map`/`get_*_map_size`), scans each track OS-first-then-default-fallback,
  first-match-wins, and an OS-map match for a track PREVENTS that track's default
  map from being scanned — while the OTHER track decides independently.
- The ordering invariants are preserved: `disable_command()` before any scan;
  `deactivate_layer()` before any `activate_layer()`; exactly-one-active-layer;
  first-match-wins within whichever map(s) are consulted.
- The `#ifdef CONSOLE_ENABLE` debug print uses `command_found->pattern` (the
  pointer set to whichever entry matched) — NOT the stale
  `cmd_map[found_command_match].pattern` re-index (findings F2).
- `notifier_set_os(os)` is present, public (matches the `notifier.h` decl), the
  SOLE writer of `current_os`, idempotent on `os == current_os` (no-op), and on a
  change calls `disable_command()` then `deactivate_layer()` with NO re-dispatch.
- `notifier.c` **compiles** under the stub harness
  (`-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -Wall -Wextra -std=c99`)
  with **ZERO warnings** (the 2 `-Wunused-function` warnings S1 left are cleared —
  `process_full_message` now calls both selectors).
- Backward-compat preserved: `test_notifier_dispatch` links against the modified
  `notifier.c` and reports **11/11 passed, 0 FAIL** (no `_OS` macros ⇒
  `current_os` stays `OS_UNSURE` ⇒ OS scans run 0 iterations ⇒ default maps ⇒
  byte-identical to pre-multi-OS; invariant 19 / F3 / F5).

## User Persona (if applicable)

**Target User**: (1) A keymap author who, having `#include`d `notifier.h` and
defined `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {…})` etc., expects: when the host
OS is detected as macOS, a message is matched against the macOS map FIRST (a hit
there wins and the default is skipped for that track), with the default map as
fallback; and who calls `notifier_set_os(os)` from
`process_detected_host_os_kb`. (2) The NEXT milestone's test
`test_notifier_os.c` (P1.M2.T1.S1), which exercises the F8/F9 contract
end-to-end through `process_full_message` + `notifier_set_os`.

**Use Case**: Host sends `Google Chrome\x1DClaude`; `current_os == OS_MACOS`;
the macOS command map entry `WT("Google Chrome", "*claude*")` matches and fires
`vim_lazy_insert`; the macOS layer entry `WT("Google Chrome", "*")` activates
`_BROWSER`; neither default map is consulted (§10.3 worked example). On
`blender\x1D`, the macOS maps miss and BOTH tracks fall back to the default map
(`_BLENDER`), independently (§10.3 second worked example — F8.5).

**User Journey**: keymap `process_detected_host_os_kb(os)` → `notifier_set_os(os)`
(the sole call) → on the next focus-change message, `hid_notify` →
`process_full_message` resolves OS maps via `select_*_map_os(current_os)` → scans
OS-first/default-fallback per track → matches fire.

**Pain Points Addressed**: Same conceptual app reports different window-class
strings per OS (e.g. `iTerm`/`Terminal` on macOS vs `*alacritty*`/`*kitty*` on
Linux). Per-OS maps let each OS carry its own class strings, with shared rules in
the default map, WITHOUT breaking the default-only keymap (structural
backward-compat) and WITHOUT a link dependency on the OS-detection `.c`
(push-only OS signal).

## Why

- **Consumes the S1 provider**: P1.M1.T3.S1 landed `current_os`, the 16 per-OS
  weak accessors, and the 2 `static` selectors (`select_command_map_os` /
  `select_layer_map_os`) but deliberately did NOT call them (S2's
  `process_full_message` is the sole caller; S2's `notifier_set_os` is the sole
  mutator of `current_os`). This task closes the loop.
- **Implements the core multi-OS rule (§2 F8.4/F8.5)**: per-track,
  OS-first/default-fallback, independent tracks — the load-bearing dispatch
  semantics. Everything else in the multi-OS feature (header macros S1.T1,
  enum stub S1.T2, provider accessors S1.T3) exists to feed THIS dispatch.
- **Implements the OS-change clearing contract (§2 F9)**: a stable OS change
  clears any layer/command chosen under the previous OS's maps (F9.1), without
  re-dispatch (F9.2), idempotently (F9.3) — so no stale cross-OS state persists
  and repeated stable-detection callbacks do not flap.
- **Preserves backward compatibility structurally** (not as a special case): with
  zero `DEFINE_*_OS` macros every per-OS accessor returns `{NULL,0}`, the OS scan
  runs 0 iterations, and dispatch is byte-identical to today (invariant 19). No
  `#ifdef` guard is added or needed.
- **Fixes the F2 CONSOLE footgun**: the existing debug print indexed
  `cmd_map[found_command_match].pattern`; after the split the matched map could
  be OS or default, so it must use the `command_found` pointer.

## What

Two edits to `notifier.c` (the file as it exists AFTER S1 landed — S1 is
already in the repo; see research/notes.md §"Status of upstream dependency"):

1. **Rewrite the body of `process_full_message`** (currently starts at ~line 334):
   keep the signature, the local `received_command[256]` buffer, the strlen guard,
   the memcpy+NUL-terminate, and the leading `disable_command()`. Replace the
   single-map resolution + single-map scans with: resolve all four maps
   (`os_cmd`/`os_layer`/`def_cmd`/`def_layer`); OS-first/default-fallback scan per
   track (first-match-wins; an OS hit skips that track's default scan); keep
   `deactivate_layer()` → `enable_command`/`activate_layer` ordering; fix the
   CONSOLE print to `command_found->pattern`; return
   `command_found != NULL || layer_found != LAYER_UNSET`.
2. **Add `notifier_set_os(os_variant_t os)`** between `process_full_message` and
   `hid_notify`: idempotent guard → optional CONSOLE print → set `current_os` →
   `disable_command()` → `deactivate_layer()` → (no re-dispatch).

The exact validated code for both is given verbatim in the Implementation
Blueprint. No other file is touched.

### Success Criteria

- [ ] `process_full_message` resolves `os_cmd_map`/`os_layer_map` via
  `select_command_map_os`/`select_layer_map_os(current_os, …)` AND
  `def_cmd_map`/`def_layer_map` via `get_command_map`/`get_layer_map` (+`_size`).
- [ ] Each track scans OS map first; an OS-map match PREVENTS that track's default
  scan; OS-absent-or-no-match ⇒ default scanned. The two tracks do this
  INDEPENDENTLY.
- [ ] Ordering preserved: `disable_command()` before scans;
  `deactivate_layer()` before `activate_layer()`; first-match-wins; exactly one
  active layer.
- [ ] CONSOLE print uses `command_found->pattern` (F2 fix), not a re-index.
- [ ] `notifier_set_os` present, idempotent on `os == current_os`, clears state on
  change (`disable_command` + `deactivate_layer`), no re-dispatch.
- [ ] `notifier.c` stub-compiles with **0 warnings** (`-Wall -Wextra`).
- [ ] `test_notifier_dispatch` 11/11, 0 FAIL (backward-compat canary).
- [ ] No edits to `notifier.h`, `pattern_match.*`, `qmk_stubs/*`,
  `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact target bodies for BOTH edits (verbatim from a /tmp
construction validated during research), the exact anchors (unique strings in the
current file), the exact behavior contract (PRD §8.6/§8.7 + §2 F8/F9), and the
validation commands were all **empirically validated**: the end-state
(post-S1 + S2) compiles with 0 warnings, `test_notifier_dispatch` passes 11/11,
and a 30-assertion focused multi-OS test passes 30/30 covering F8.4/F8.5/F8.6 and
F9.1/F9.2/F9.3. An implementer with only this PRP + the repo can make the two
edits correctly with no guessing.

### Documentation & References

```yaml
# MUST READ — authoritative code (reproduce VERBATIM)
- file: plan/002_c243e735980a/prd_snapshot.md   (also PRD.md)
  section: "### 8.6 process_full_message(char *data) — the dispatcher"
  why: "The 10-step dispatch pseudocode (resolve 4 maps; COMMAND TRACK OS-first/
        default-fallback first-match-wins; LAYER TRACK same INDEPENDENTLY;
        deactivate-before-activate; CONSOLE print; return command_found||layer_found)."
  critical: "Step 4: 'an OS-map match PREVENTS the default command map from being
             scanned'. Step 5 note: 'the other track is unaffected (§2 F8.4/F8.5)'.
             The ordering note: 'Disable-before-scan, deactivate-before-activate
             still hold … exactly one active layer'."

- file: plan/002_c243e735980a/prd_snapshot.md   (also PRD.md)
  section: "### 8.7 notifier_set_os(os_variant_t os) — the OS selector"
  why: "The BYTE-EXACT notifier_set_os body (idempotent guard; CONSOLE uprintf;
        set current_os; disable_command(); deactivate_layer(); no re-dispatch comment)."
  critical: "Reproduce verbatim. It is the SOLE mutation point for current_os
             (invariant 17 / §2 F8.2). The 'do NOT re-dispatch' comment is load-
             bearing (F9.2). VERIFIED by research."

- file: plan/002_c243e735980a/prd_snapshot.md   (also PRD.md)
  section: "### 2 Functional Requirements → F8 (F8.4/F8.5/F8.6/F8.7) and F9 (F9.1/F9.2/F9.3)"
  why: "The semantics: per-track OS-first/default-fallback; independent tracks;
        OS_UNSURE ⇒ default only; weak-default guarantees; clear-on-change;
        no-re-dispatch; idempotent. These are what the inline comments must state."
  critical: "F8.4: 'A match there wins and the default map for that type is not
             consulted.' F8.5: 'command track and layer track each make this
             OS-vs-default decision INDEPENDENTLY.' F9.2: 'does not re-dispatch
             the last received message.'"

- file: plan/002_c243e735980a/prd_snapshot.md   (also PRD.md)
  section: "## 13 Key Invariants — 4,5,6,7,15,16,17,18,19"
  why: "disable-before-scan(4)/deactivate-before-activate(4)/first-match-wins(5)/
        exactly-one-active-layer(6)/unmatched-clears-state(7)/multi-OS-OS-first-
        per-track(15)/OS_UNSURE-default-only(16)/push-only-OS(17)/notifier_set_os-
        idempotent+clear-no-redispatch(18)/default-only-byte-identical(19)."
  critical: "invariant 19 is the prime backward-compat directive — validated by
             the 11/11 dispatch canary."

# The contract this task CONSUMES (already landed by S1 — treat as ground truth)
- file: plan/002_c243e735980a/P1M1T3S1/PRP.md
  why: "Defines what S1 inserted (current_os global; 16 per-OS weak accessors;
        2 static selectors select_command_map_os/select_layer_map_os). S2's
        process_full_message is the SOLE caller of those selectors; S2's
        notifier_set_os is the SOLE mutator of current_os. Read to confirm the
        selector signatures and the {NULL,0}-on-absent contract."
  critical: "S1 is ALREADY IN THE REPO (verified: notifier.c modified, 462 lines,
             17 S1 markers). Do NOT re-apply S1's block — it is present. S2 edits
             process_full_message (line ~334) and adds notifier_set_os; neither
             region overlaps S1's insertion (before activate_layer)."

- file: notifier.h
  section: "notifier_set_os declaration (line ~24) and DEFINE_SERIAL_*_OS macros (lines ~60–95)"
  why: "notifier_set_os is ALREADY DECLARED in notifier.h (S1.T1.S1 landed it); S2
        provides its DEFINITION in notifier.c (must match the decl signature
        exactly: void notifier_set_os(os_variant_t os)). The DEFINE_*_OS macros
        generate the strong _notifier_get_*_OS_* symbols that select_*_map_os
        (S1's static code) dispatches to — S2 does NOT reference these names
        directly (only calls the selectors)."
  gotcha: " notifier_set_os must NOT be static (it is called from the keymap TU).
           It is the ONLY public OS entry point (§2 F8.2)."

- file: qmk_stubs/os_detection.h
  why: "Defines os_variant_t = { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3,
        OS_IOS=4 }. current_os is of this type; notifier_set_os's parameter is of
        this type. Reused as-is (not redefined) — F8.1."
  critical: "OS_UNSURE (0) is the boot initializer of current_os; the OS scan runs
             0 iterations for it (selector default case ⇒ {NULL,0}) ⇒ default maps."

- file: notifier.c   (the file being modified — pattern + exact anchors)
  why: "process_full_message (~line 334) is the function body to rewrite; its
        current single-map scan (cmd_map/lyr_map + found_command_match/found_layer_match
        index vars + the cmd_map[found_command_match].pattern CONSOLE bug) is the
        TARGET of the rewrite. activate_layer/deactivate_layer/enable_command/
        disable_command (~lines 178–209) are defined BEFORE process_full_message,
        so notifier_set_os (inserted after process_full_message) can call them with
        no forward declaration."
  pattern: "Existing accessor style: get_command_map()/get_command_map_size() return
            the default map pair; S1's select_command_map_os(current_os,&map,&size)
            writes the OS pair through out-params. Mirror that for layers."
  gotcha: "F2: the CONSOLE block currently does cmd_map[found_command_match].pattern.
           After the split, cmd_map no longer exists (renamed) and the matched map
           could be OS or default. Use command_found->pattern (the pointer is
           already set to whichever entry matched). VERIFIED in research."

# Consumer / validator (existing host suite — must stay green; do NOT modify)
- file: test_notifier_dispatch.c
  why: "Backward-compat canary: #include \"notifier.h\", DEFINE_SERIAL_COMMANDS/LAYERS
        (NO _OS), never calls notifier_set_os. Proves current_os stays OS_UNSURE ⇒
        selectors return {NULL,0} ⇒ OS scans 0 iterations ⇒ default maps ⇒ 11/11."
  gotcha: "DO NOT modify it. It is the structural backward-compat proof (invariant 19)."

# Architecture grounding
- file: plan/002_c243e735980a/architecture/findings_and_risks.md
  section: "### F2 (CONSOLE block stale var), ### F3 (backward-compat structural),
            ### F5 (test_notifier_dispatch unchanged), ### F6 (test observation via
            distinguishable callbacks), RISK R2/R5"
  why: "F2 = the exact CONSOLE fix (command_found->pattern); F3 = zero-size-loop IS
        backward-compat (no #ifdef); F5 = dispatch must pass unchanged; F6 = how the
        NEXT test (P1.M2.T1.S1) will observe OS-vs-default (distinguishable callbacks
        + a layer observable); R5 = OS-no-match→default is BY DESIGN (F8.4)."
  critical: "F3: 'the zero-size loop IS the backward-compat guarantee (invariant 19).
             Do not add #ifdef guards.' F2: 'Use command_found->pattern pointer.'"

- file: plan/002_c243e735980a/architecture/external_deps.md
  section: "## detected_host_os() — the function the module must NOT call"
  why: "notifier_set_os is the push-only entry point; the module NEVER calls
        detected_host_os() (invariant 17). The comments in notifier_set_os encode
        this (no link dependency on os_detection.c)."

# Prior PRPs (the contract this builds on — treat as ground truth)
- file: plan/002_c243e735980a/P1M1T3S1/PRP.md
  why: "Defines the S1 provider this task consumes. Confirms current_os/selector
        signatures and the {NULL,0}-on-absent contract; documents the SAME
        run_notifier_stub_tests.sh [2/3] -Iqmk_stubs sequencing caveat (fixed by
        P1.M2.T2.S1) — use the corrected-flag harness."
- file: plan/002_c243e735980a/P1M1T1S1/PRP.md
  why: "Defines notifier.h's notifier_set_os decl + DEFINE_*_OS macros S2 matches."
- file: plan/002_c243e735980a/P1M1T2S1/PRP.md
  why: "Defines qmk_stubs/os_detection.h (landed) S2 compiles against."
```

### Current Codebase tree (relevant slice — POST-S1 state)

```bash
notifier.c                 # ← MODIFY (this task). S1 block ALREADY PRESENT (lines ~111–167).
                           #   S2 edits: process_full_message body (~line 334) + new notifier_set_os (before hid_notify).
notifier.h                 # LANDED: notifier_set_os DECL + DEFINE_*_OS macros. DO NOT TOUCH.
pattern_match.{c,h}        # unaffected.
qmk_stubs/
  os_detection.h           # LANDED: os_variant_t enum. DO NOT TOUCH.
  qmk_keyboard_stub.h      # QMK_KEYBOARD_H stand-in. DO NOT TOUCH.
  raw_hid.h                # raw_hid_send decl. DO NOT TOUCH.
  qmk_stubs.c              # layer_on/off + raw_hid_send impls. DO NOT TOUCH.
test_notifier_dispatch.c   # backward-compat canary. DO NOT TOUCH.
run_notifier_stub_tests.sh # stub gate; [2/3] link step needs -Iqmk_stubs (P1.M2.T2.S1). DO NOT TOUCH.
run_all_tests.sh           # 9-suite pattern_match corpus — unaffected. DO NOT TOUCH.
PRD.md                     # READ-ONLY.
plan/002_c243e735980a/     # this plan. DO NOT TOUCH (except your PRP/research/).
```

### Desired Codebase tree with files to be added/changed

```bash
notifier.c                 # MODIFIED: process_full_message body rewritten; +notifier_set_os function.
# (no new files created by this subtask)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (F2 — the CONSOLE footgun): the existing #ifdef CONSOLE_ENABLE block
//   prints `cmd_map[found_command_match].pattern`. After the split there is no
//   single `cmd_map` (it is now os_cmd_map OR def_cmd_map) and `found_command_match`
//   would index into whichever map matched — ambiguous. Use `command_found->pattern`
//   (the pointer is set to the matched entry in whichever map). VERIFIED. Same
//   discipline if a layer-track pattern print is ever added (use the matched
//   layer entry's pattern, not a re-index).

// CRITICAL (independent tracks — F8.5): the command track's OS-vs-default decision
//   MUST NOT influence the layer track, and vice versa. Implement as TWO separate
//   scan pairs (os-then-default for commands; os-then-default for layers), each
//   with its own `found` flag. Do NOT share a single "OS matched?" flag across
//   tracks. (A layer may resolve from the OS map while a command resolves from the
//   default map.) VERIFIED by the F8.5 test cases.

// CRITICAL (OS match PREVENTS default scan — F8.4): within a track, if the OS map
//   scan sets the `found` flag, the default map scan for THAT track must be
//   SKIPPED (guard it with `if (command_found == NULL)` / `if (layer_found == LAYER_UNSET)`).
//   Do NOT scan both maps unconditionally and "merge" — an OS hit wins exclusively.

// EXPECTED (zero warnings): S1 left 2 `-Wunused-function` warnings
//   (select_command_map_os / select_layer_map_os — static, not yet called). This
//   task's process_full_message CALLS both, so those warnings CLEAR. The build
//   must finish with 0 warnings. If a warning remains, you forgot to wire a selector.

// GOTCHA (backward-compat is structural — no #ifdef): do NOT wrap the new scan
//   logic in `#ifdef OS_DETECTION_ENABLE` or any guard. With zero DEFINE_*_OS
//   macros, the per-OS accessors return {NULL,0}, select_*_map_os returns {NULL,0},
//   the OS scan loops run 0 iterations (`i < 0`-equivalent), and the default scan
//   runs exactly as before. This IS invariant 19 (verified: dispatch 11/11).

// GOTCHA (drop the index vars): the current body uses `signed int found_command_match`
//   and `found_layer_match` as match-found signals. After the split these index into
//   an AMBIGUOUS map (OS or default?). The pointer `command_found` and the value
//   `layer_found != LAYER_UNSET` are unambiguous and are exactly what PRD §8.6
//   pseudocode uses. DROP the index vars; use command_found != NULL / layer_found !=
//   LAYER_UNSET everywhere (conditions, CONSOLE, return). Behavior is identical
//   (the signals were always set together). VERIFIED (research D1).

// GOTCHA (selector out-params): select_command_map_os(current_os, &map, &size)
//   writes through OUT-PARAMETERS (it returns void). Declare the map/size locals,
//   pass their addresses. The selectors are `static` (file-local, S1) — already in
//   scope; no extern/decl needed.

// GOTCHA (notifier_set_os placement): insert it AFTER process_full_message and
//   BEFORE hid_notify. It calls disable_command()/deactivate_layer() which are
//   defined ~line 178–209 (before process_full_message) — definition-before-use
//   satisfied; no forward declaration. Do NOT place it inside S1's block (before
//   activate_layer) — that region is S1's; placing it there would create a merge
//   dependency and put it before the functions it calls.

// GOTCHA (notifier_set_os is the SOLE current_os writer — invariant 17): do NOT
//   add any other write to current_os anywhere. The only read of current_os is in
//   process_full_message's select_*_map_os calls. notifier_set_os must NOT call
//   detected_host_os() (no link dependency on os_detection.c).

// GOTCHA (idempotent guard order): in notifier_set_os, the `if (os == current_os)
//   return;` MUST be the first statement — BEFORE the CONSOLE print and BEFORE
//   disable/deactivate. Otherwise a no-op same-OS call would spuriously clear state
//   (violating F9.3). VERIFIED by the F9.3 test.

// GOTCHA (no re-dispatch — F9.2): notifier_set_os must NOT call process_full_message
//   or otherwise re-evaluate the last message. It only clears state; the next host
//   focus-change message re-establishes it. VERIFIED by the F9.2 test.

// GOTCHA (runner sequencing, NOT this task's defect): run_notifier_stub_tests.sh
//   [2/3] link step has only -I. (not -Iqmk_stubs) — SAME pre-existing issue as S1
//   (documented in P1.M1.T2.S1/P1M1T3S1 PRPs, fixed by P1.M2.T2.S1). Validate THIS
//   task with the corrected-flag harness (Validation Level 2), not the bare runner.
```

## Implementation Blueprint

### Data models and structure

No new types. `process_full_message` gains four local map/size pairs
(`command_map_t*`/`size_t` × {os,default} for commands; `layer_map_t*`/`size_t`
× {os,default} for layers). `notifier_set_os` takes the existing `os_variant_t`
(from `os_detection.h`, via `notifier.h`) and writes the existing `current_os`
global (added by S1). No new `#include` is needed (`size_t` is in scope via
`<string.h>` already included; `os_variant_t` via `notifier.h`).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — REWRITE the process_full_message body
  - LOCATE process_full_message (verify: `grep -n '^bool process_full_message' notifier.c`).
  - REPLACE the ENTIRE body (from `char received_command[256] = {0};` through
    `return found_command_match != -1 || found_layer_match != -1;`) with the
    "Exact target body — process_full_message" block below (byte-exact, validated).
  - The new body, IN ORDER: declarations (drop found_command_match/found_layer_match);
    strlen guard; memcpy+NUL; disable_command(); resolve 4 maps; COMMAND TRACK
    OS-first/default-fallback scan; LAYER TRACK same INDEPENDENTLY;
    deactivate_layer(); enable_command if command_found; activate_layer if
    layer_found; CONSOLE block (GS->'|' + command_found->pattern print);
    return command_found != NULL || layer_found != LAYER_UNSET.
  - PRESERVE the function signature `bool process_full_message(char *data)` and
    the closing brace. Do not touch any other function.
  - DEPENDENCIES: select_command_map_os / select_layer_map_os (S1, in-scope static);
    current_os (S1 global); get_command_map/get_layer_map + _size (existing weak);
    match_pattern / disable_command / deactivate_layer / enable_command /
    activate_layer (existing). No new #include.

Task 2: MODIFY notifier.c — INSERT notifier_set_os between process_full_message and hid_notify
  - LOCATE the anchor: the `}` closing process_full_message immediately followed
    by `void hid_notify(uint8_t *data, uint8_t length) {`. (verify:
    `grep -n 'void hid_notify' notifier.c` — there is exactly one definition.)
  - INSERT the "Exact target body — notifier_set_os" block below (the function +
    its Mode-A doc comment) in the blank lines between the two.
  - PRESERVE both neighboring functions unchanged.
  - DEPENDENCIES: current_os (S1 global, read+write); disable_command /
    deactivate_layer (existing, defined earlier); CONSOLE_ENABLE / uprintf
    (conditional). os_variant_t via notifier.h.

Task 3: VERIFY (no edit) — compile + backward-compat + functional
  - Run Validation Level 1 (stub-compile; expect 0 warnings).
  - Run Validation Level 2 (link test_notifier_dispatch → 11/11; optional focused
    multi-OS test from research/test_notifier_os_val.c → 30/30).
  - Run Validation Level 4 (doc-point greps; F2 fix present; backward-compat structural).
```

**Exact target body — `process_full_message` (byte-exact, validated in research):**

```c
bool process_full_message(char *data) {
    char received_command[256] = {0};
    int length = strlen(data);
    command_map_t *command_found = NULL;
    uint8_t layer_found = LAYER_UNSET;

    if ((size_t)length >= sizeof(received_command)) {
        return false;
    }

    memcpy(received_command, data, length);
    received_command[length] = '\0';

    // Always disable current command first (step 3 — disable-before-scan).
    disable_command();

    /* Resolve the maps for current_os (§8.6 step 2):
     *   - OS-specific maps come from the per-OS accessors (select_*_map_os);
     *     they return {NULL, 0} when current_os has no per-OS map (the boot
     *     state OS_UNSURE, or no DEFINE_*_OS macro for this OS). A NULL map /
     *     size 0 makes the OS scan below run 0 iterations.
     *   - Default maps come from the user's DEFINE_SERIAL_* (or the weak
     *     {empty_*_map, 0} defaults).
     * When no per-OS map exists, the OS scan is a 0-iteration no-op and the
     * default map is scanned — this IS the backward-compat guarantee
     * (invariant 19), so no #ifdef is needed. */
    command_map_t *os_cmd_map;   size_t os_cmd_size;
    layer_map_t   *os_layer_map; size_t os_layer_size;
    select_command_map_os(current_os, &os_cmd_map,   &os_cmd_size);
    select_layer_map_os  (current_os, &os_layer_map, &os_layer_size);
    command_map_t *def_cmd_map   = get_command_map();   size_t def_cmd_size   = get_command_map_size();
    layer_map_t   *def_layer_map = get_layer_map();     size_t def_layer_size = get_layer_map_size();

    /* COMMAND TRACK — OS-first, default-fallback, first-match-wins (§8.6 step 4).
     * OS-specific map scanned FIRST; a match wins and the default map for this
     * track is NOT scanned. No OS map (or no match in it) => scan the default
     * map (§2 F8.4). The two tracks decide independently (§2 F8.5). */
    for (size_t i = 0; i < os_cmd_size; i++) {
        if (match_pattern(os_cmd_map[i].pattern, received_command, os_cmd_map[i].case_sensitive)) {
            command_found = &os_cmd_map[i];
            break;
        }
    }
    if (command_found == NULL) {
        for (size_t i = 0; i < def_cmd_size; i++) {
            if (match_pattern(def_cmd_map[i].pattern, received_command, def_cmd_map[i].case_sensitive)) {
                command_found = &def_cmd_map[i];
                break;
            }
        }
    }

    /* LAYER TRACK — same rule, INDEPENDENT of the command track (§8.6 step 5 /
     * §2 F8.5): a layer may resolve from the OS map while a command resolves
     * from the default map, or vice versa. */
    for (size_t i = 0; i < os_layer_size; i++) {
        if (match_pattern(os_layer_map[i].pattern, received_command, os_layer_map[i].case_sensitive)) {
            layer_found = os_layer_map[i].layer;
            break;
        }
    }
    if (layer_found == LAYER_UNSET) {
        for (size_t i = 0; i < def_layer_size; i++) {
            if (match_pattern(def_layer_map[i].pattern, received_command, def_layer_map[i].case_sensitive)) {
                layer_found = def_layer_map[i].layer;
                break;
            }
        }
    }

    // Always deactivate the current layer first (step 6 — deactivate-before-activate).
    deactivate_layer();

    // Enable new command if found (step 7 — fires on_enable).
    if (command_found != NULL) {
        enable_command(command_found);
    }

    // Activate new layer if found (step 8 — layer_on).
    if (layer_found != LAYER_UNSET) {
        activate_layer(layer_found);
    }

    #ifdef CONSOLE_ENABLE
    // replace all group separators (GS) with '|' for console readability
    for (size_t i = 0; i < strlen(received_command); i++) {
        if (received_command[i] == GS_DELIMITER[0]) {
            received_command[i] = '|';
        }
    }

    /* DEBUG (step 9): print per-track match/miss. Use the pointer
     * command_found->pattern (already set to whichever entry — OS or default —
     * matched) rather than re-indexing by a single-map variable name; after the
     * split the matched map could be either (findings F2). GS is shown as '|'. */
    if (command_found != NULL) {
        uprintf("Matched message %s on command: %s\n", received_command, command_found->pattern);
    } else {
        uprintf("Did not match message %s on any command\n", received_command);
    }
    #endif

    return command_found != NULL || layer_found != LAYER_UNSET;
}
```

**Exact target body — `notifier_set_os` (byte-exact vs PRD §8.7, validated):**

```c
/* notifier_set_os — the OS selector (§8.7). Sole mutation point for current_os
 * (invariant 17 / §2 F8.2): the module never calls detected_host_os(), so there
 * is no link dependency on the OS-detection subsystem — the OS is PUSHED in by
 * the keymap (conventionally from process_detected_host_os_kb, §10.1 step 3).
 *
 * Contract (§2 F9):
 *   - IDEMPOTENT on an unchanged value (no-op; F9.3): repeated stable-detection
 *     callbacks (e.g. macOS-on-ARM's delayed stability) do not flap state.
 *   - On a CHANGED value it CLEARS all notifier state before recording the new
 *     OS: disable_command() fires the previous command's on_disable if active,
 *     deactivate_layer() turns off the active notifier layer if any (F9.1). This
 *     guarantees no layer/command chosen under the previous OS's maps survives.
 *   - It does NOT re-dispatch the last message (F9.2): the next focus-change
 *     message from the host re-establishes state under the new maps.
 *
 * Symbol-name parity: the keymap's DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…) /
 * DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…) macros (##os token-paste in notifier.h)
 * generate the strong _notifier_get_*_map_OS_MACOS[_size] symbols that override
 * the weak defaults; this function only flips current_os so the next dispatch's
 * select_*_map_os() resolves the override. */
void notifier_set_os(os_variant_t os) {
    if (os == current_os) return;                 /* idempotent: no flap on repeat (F9.3) */
    #ifdef CONSOLE_ENABLE
    uprintf("notifier: OS %u -> %u; clearing state\n", (unsigned)current_os, (unsigned)os);
    #endif
    current_os = os;
    disable_command();      /* fires prev on_disable if a command was active (F9.1) */
    deactivate_layer();     /* turns off the active notifier layer if any (F9.1)    */
    /* Intentionally do NOT re-dispatch the last message. The next focus-change
     * message from the host re-establishes state under the new maps (F9.2). */
}
```

> **Style note:** `notifier.c` uses `//` line comments and `/* */` blocks
> interchangeably; both styles above are consistent with the file. The function
> BODIES must be byte-exact vs PRD §8.6/§8.7 semantics regardless of comment
> style. The `(unsigned)` casts in the CONSOLE uprintf match PRD §8.7 verbatim
> (avoids `%u` on an enum/uint8 width warning).

### Implementation Patterns & Key Details

```c
// The four-map resolution + per-track OS-first scan is the WHOLE feature. Note
// the symmetry: each track is (os scan) then (default scan guarded by "not found").
// Do NOT factor the two tracks into a shared helper that takes a "matched?" flag —
// they must be independent (a shared flag would couple them, violating F8.5).

// ANTI-PATTERN: do NOT scan both OS and default maps unconditionally and then
//   "prefer" the OS match. An OS-map match must PREVENT the default map from
//   being scanned at all (F8.4). Guard the default scan with the track's `found`
//   flag (command_found == NULL / layer_found == LAYER_UNSET).

// ANTI-PATTERN: do NOT keep found_command_match / found_layer_match as index
//   variables "to minimize the diff". After the split they index into an
//   ambiguous map and invite the F2 re-index bug. Use the pointer/value signals
//   (command_found != NULL / layer_found != LAYER_UNSET) — behaviorally identical,
//   structurally footgun-free, and spec-faithful (PRD §8.6 pseudocode).

// ANTI-PATTERN: do NOT reorder disable/scan/deactivate/activate. The ordering
//   (disable_command → scan → deactivate_layer → enable/activate) is invariant 4.
//   disable-before-scan fires the PREVIOUS command's on_disable cleanly;
//   deactivate-before-activate guarantees exactly-one-active-layer (invariant 6).

// ANTI-PATTERN: do NOT call select_*_map_os more than once per dispatch, or cache
//   the OS maps in a global. They are cheap (a switch + 2 accessor calls) and
//   must reflect current_os at dispatch time. Resolve them fresh each call.

// ANTI-PATTERN: do NOT add a `case OS_UNSURE:` anywhere — OS_UNSURE has no
//   per-OS map (selector default ⇒ {NULL,0}; F8.6 / invariant 16). S1 already
//   handles this; S2 just consumes the {NULL,0} result (0-iteration scan).

// ANTI-PATTERN: do NOT make notifier_set_os re-dispatch. F9.2 is explicit: the
//   next host message re-establishes state. Re-dispatching would (a) require
//   caching the last message (new global state + a buffer-save point) and
//   (b) risk dispatching a stale message under transient/wrong OS values.

// ANTI-PATTERN: do NOT move the `if (os == current_os) return;` guard below the
//   CONSOLE print or the disable/deactivate. Idempotency (F9.3) requires it FIRST.
```

### Integration Points

```yaml
PROCESS_FULL_MESSAGE (rewrite body):
  - file: notifier.c (function at ~line 334)
  - change: single-map scan → 4-map resolve + per-track OS-first/default-fallback
  - invariant: signature, locals buffer/guard/memcpy, disable_command lead,
    deactivate-before-activate, first-match-wins all preserved.
NOTIFIER_SET_OS (new function):
  - file: notifier.c (insert between process_full_message's `}` and `void hid_notify`)
  - decl: already in notifier.h (S1.T1.S1): `void notifier_set_os(os_variant_t os);`
  - linkage: external (non-static) — called from the keymap TU (process_detected_host_os_kb)
GLOBALS:
  - current_os: read in process_full_message (via select_*_map_os); written ONLY in notifier_set_os.
CALL GRAPH (new edges):
  - process_full_message → select_command_map_os, select_layer_map_os   (S1 statics; clears S1's unused-warning)
  - notifier_set_os      → disable_command, deactivate_layer             (existing; defined earlier in file)
BUILD / CONFIG / DATABASE / ROUTES:
  - none. No rules.mk edit, no new files, no includes. Pure C edit in notifier.c.
```

## Validation Loop

> Toolchain: gcc (C project — no ruff/mypy/pytest). All commands were **executed
> during research against a /tmp construction of the end-state (post-S1 + S2) and
> PASSED** (Level 1: 0 warnings; Level 2a: dispatch 11/11; Level 2b: 30/30
> functional F8/F9). Sequencing note: `run_notifier_stub_tests.sh`'s [2/3] link
> step currently lacks `-Iqmk_stubs` (pre-existing, documented in S1/T2 PRPs,
> fixed by P1.M2.T2.S1). Use the **corrected-flag harness** (Level 2) — it mirrors
> the post-fix runner and is the authoritative gate for THIS task.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile the modified notifier.c (mirrors run_notifier_stub_tests.sh [1/3]).
#     EXPECT: success with ZERO warnings. (S1's 2 -Wunused-function warnings for
#     select_command_map_os/select_layer_map_os are CLEARED — process_full_message
#     now calls both. A remaining warning means you forgot to wire a selector.)
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_t3s2.o 2>&1 | tee /tmp/t3s2_compile.log
test -f /tmp/notifier_t3s2.o && echo "✓ notifier.c compiles (object present)"
echo "total warnings: $(grep -c 'warning:' /tmp/t3s2_compile.log)  (expect 0)"
# Expected: object present; 0 warnings.
```

### Level 2: Backward-Compat + Functional (the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier
# /tmp/notifier_t3s2.o built in Level 1.

# 2a. BACKWARD-COMPAT canary: test_notifier_dispatch (no _OS macros, current_os
#     stays OS_UNSURE) must still pass 11/11. Uses -Iqmk_stubs on BOTH steps (the
#     post-fix runner; the current run_notifier_stub_tests.sh [2/3] lacks it).
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_t3s2.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o /tmp/test_notifier_dispatch_t3s2
/tmp/test_notifier_dispatch_t3s2 2>/dev/null | tee /tmp/t3s2_dispatch.log
echo "fails=$(grep -c '^FAIL:' /tmp/t3s2_dispatch.log)  (expect 0)"
grep 'Total tests run' /tmp/t3s2_dispatch.log   # expect "11 / passed: 11 / failed: 0"
# Expected: 11/11, 0 FAIL (invariant 19 — default-only byte-identical to pre-multi-OS).

# 2b. FUNCTIONAL multi-OS validation (OPTIONAL but recommended): the focused test
#     saved during research at plan/002_c243e735980a/P1M1T3S2/research/test_notifier_os_val.c.
#     It needs a 1-line layer observable in qmk_stubs.c (stub_get_active_layer) —
#     copy qmk_stubs.c to /tmp and append the accessor, OR skip 2b (it is a
#     research artifact; the OFFICIAL test_notifier_os.c is P1.M2.T1.S1's job).
#     To run it:
cp qmk_stubs/qmk_stubs.c /tmp/qmk_stubs_obs.c
cat >> /tmp/qmk_stubs_obs.c <<'EOF'
uint8_t stub_get_active_layer(void) { return g_active_layer; }
EOF
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_t3s2.o /tmp/qmk_stubs_obs.c \
    plan/002_c243e735980a/P1M1T3S2/research/test_notifier_os_val.c \
    -o /tmp/test_notifier_os_t3s2 2>/tmp/t3s2_os_link.log
/tmp/test_notifier_os_t3s2 2>/dev/null | tail -2
# Expected: "Total tests run: 30 / passed: 30 / failed: 0" (F8.4/F8.5/F8.6/F9.1/F9.2/F9.3).
# (Link may emit -Wmissing-field-initializers noise from the TEST TU's map rows —
#  irrelevant to notifier.c; add trailing `, false` to silence if desired.)
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# The FULL run_notifier_stub_tests.sh end-to-end pass is gated on P1.M2.T2.S1
# (it adds -Iqmk_stubs to the [2/3] link step). Running it NOW is EXPECTED to fail
# at [2/3] ONLY because of that missing flag — NOT because of this task (Level 2
# above proves the modified notifier.c is correct with the proper flag). After
# P1.M2.T2.S1 lands:
#     ./run_notifier_stub_tests.sh   # -> "✓ notifier stub-compile gate PASSED",
#                                    #    test_notifier_dispatch 0 FAIL
#                                    #    (test_notifier_os added by P1.M2.T1/T2)

# Diff hygiene: ONLY notifier.c changed (plus your PRP/research under plan/).
git status --porcelain
# Expected: ` M notifier.c` (and ?? plan/002.../P1M1T3S2/{PRP.md,research/}).
#           (S1's ` M notifier.c` + `A plan/.../P1M1T3S1/...` are also present if
#           S1 hasn't been committed yet — that's expected; S2 is additive on top.)
git diff --stat -- notifier.c
# Expected: notifier.c shows insertions + deletions confined to process_full_message
#           and the new notifier_set_os (no other region).
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Required inline-documentation points present (item point 5 / Mode A).
for needle in "current_os" "OS_UNSURE" "select_command_map_os" "select_layer_map_os" \
              "F8.4" "F8.5" "F9.1" "F9.2" "F9.3" "command_found->pattern" \
              "backward-compat guarantee" "DEFINE_SERIAL_COMMANDS_OS" "invariant 19"; do
  grep -qF "$needle" notifier.c && echo "doc present: $needle" \
    || { echo "MISSING doc token: $needle"; exit 1; }
done

# 4b. The F2 fix is present: the CONSOLE print uses command_found->pattern, NOT a
#     re-index by a (now-removed) single-map variable.
grep -q 'command_found->pattern' notifier.c && echo "✓ F2 fix: CONSOLE uses command_found->pattern"
! grep -q 'cmd_map\[found_command_match\]' notifier.c && echo "✓ F2: stale cmd_map[found_command_match] re-index is GONE"

# 4c. The OS-first/default-fallback structure: both selectors are CALLED, and each
#     track's default scan is guarded by its `found` flag.
grep -q 'select_command_map_os(current_os' notifier.c && echo "✓ command selector wired"
grep -q 'select_layer_map_os(current_os'  notifier.c && echo "✓ layer selector wired"
grep -q 'if (command_found == NULL)'      notifier.c && echo "✓ command default-scan guarded (F8.4)"
grep -q 'if (layer_found == LAYER_UNSET)' notifier.c && echo "✓ layer default-scan guarded (F8.4)"

# 4d. notifier_set_os contract: idempotent guard FIRST, clear-on-change, no re-dispatch.
grep -q 'if (os == current_os) return;' notifier.c && echo "✓ F9.3 idempotent guard first"
grep -q 'current_os = os;'              notifier.c && echo "✓ sole current_os write in notifier_set_os"
grep -q 'disable_command();'            notifier.c && echo "✓ F9.1 disable on change"
grep -q 'deactivate_layer();'           notifier.c && echo "✓ F9.1 deactivate on change"
! grep -q 'process_full_message' <(awk '/^void notifier_set_os/,/^}/' notifier.c) \
  && echo "✓ F9.2 no re-dispatch (notifier_set_os does not call process_full_message)"

# 4e. notifier_set_os is NON-static (public; matches notifier.h decl) and declared exactly once.
test "$(grep -c '^void notifier_set_os(os_variant_t os) {' notifier.c)" -eq 1 \
  && echo "✓ notifier_set_os defined exactly once, non-static"

# 4f. Backward-compat is STRUCTURAL (no #ifdef guards around the new scan logic).
! grep -qE '#if(n?def|ndef).*OS_DETECTION' notifier.c \
  && echo "✓ no OS_DETECTION_ENABLE guard (correct — structural compat, invariant 19)"

# 4g. The index vars are gone (clean approach — research D1).
! grep -q 'found_command_match' notifier.c && echo "✓ found_command_match removed (clean signals)"
! grep -q 'found_layer_match'   notifier.c && echo "✓ found_layer_match removed (clean signals)"
# Expected: all checks print "✓"; no MISSING/FAIL lines.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `notifier.c` stub-compiles (`-Wall -Wextra -std=c99 …`); object present; **0 warnings**.
- [ ] Level 2a: `test_notifier_dispatch` 11/11, 0 FAIL (backward-compat canary; invariant 19).
- [ ] Level 2b (optional): focused multi-OS test 30/30 (F8.4/F8.5/F8.6/F9.1/F9.2/F9.3).
- [ ] Level 3: `git status` shows `notifier.c` modified (+ plan/ PRP/research); diff confined to the two S2 regions.
- [ ] Level 4: all doc tokens present; F2 fix present; selectors wired + default-scans guarded; notifier_set_os contract intact; no `#ifdef` guard; index vars removed.

### Feature Validation

- [ ] `process_full_message` resolves all 4 maps; scans each track OS-first/default-fallback, first-match-wins; OS hit prevents that track's default scan.
- [ ] The two tracks decide independently (F8.5) — no shared "OS matched?" flag.
- [ ] Ordering preserved (disable-before-scan, deactivate-before-activate, exactly-one-active-layer).
- [ ] CONSOLE print uses `command_found->pattern` (F2 fix).
- [ ] `notifier_set_os` present, public, idempotent-first, clears state on change, no re-dispatch, sole `current_os` writer.
- [ ] No edits to `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`.

### Code Quality Validation

- [ ] Matches existing file style (`//` and `/* */` comments; terse accessors; consistent with surrounding code).
- [ ] Edits confined to the two regions; no restyle/reorder of unrelated code.
- [ ] No anti-patterns (see below): no unguarded double-scan, no index-var re-index, no reorder, no OS_UNSURE case, no re-dispatch, no `#ifdef` guard.
- [ ] No new `#include` added (selectors/current_os/os_variant_t/match_pattern all in scope).

### Documentation & Deployment

- [ ] Inline comments are self-documenting (Mode A) — F8.4/F8.5/F9.1/F9.2/F9.3, invariant 19, the F2 fix rationale, and the symbol-name parity all cited where relevant.
- [ ] Sequencing caveats documented: (1) S1's `-Wunused-function` warnings clear at S2 (0 warnings expected); (2) `run_notifier_stub_tests.sh` full pass gated on P1.M2.T2.S1 (`-Iqmk_stubs` in [2/3]).
- [ ] README multi-OS section is a LATER task (P1.M2.T3.S1) — not touched here.
- [ ] The official `test_notifier_os.c` is P1.M2.T1.S1's deliverable — not created here (the research/test_notifier_os_val.c is a validation artifact only).

---

## Anti-Patterns to Avoid

- ❌ Don't scan both OS and default maps unconditionally and then "prefer" the OS match — an OS-map match must PREVENT that track's default scan (F8.4). Guard the default scan with the track's `found` flag.
- ❌ Don't couple the two tracks with a shared "OS matched?" flag — they decide independently (F8.5). Implement two separate scan pairs.
- ❌ Don't keep `found_command_match`/`found_layer_match` index vars "to minimize the diff" — after the split they're ambiguous and invite the F2 re-index bug. Use `command_found != NULL` / `layer_found != LAYER_UNSET` (behaviorally identical, spec-faithful).
- ❌ Don't leave the CONSOLE print as `cmd_map[found_command_match].pattern` — that's the F2 bug. Use `command_found->pattern`.
- ❌ Don't reorder disable/scan/deactivate/activate — invariant 4 (disable-before-scan, deactivate-before-activate, exactly-one-active-layer).
- ❌ Don't add a `case OS_UNSURE:` anywhere — OS_UNSURE has no per-OS map (selector default ⇒ {NULL,0}; F8.6 / invariant 16).
- ❌ Don't make `notifier_set_os` re-dispatch or cache the last message — F9.2 (the next host message re-establishes state).
- ❌ Don't move the `if (os == current_os) return;` guard below the CONSOLE print or the disable/deactivate — idempotency (F9.3) requires it FIRST.
- ❌ Don't wrap the new scan logic in `#ifdef OS_DETECTION_ENABLE` (or any guard) — backward-compat is structural (the zero-size OS loop), not conditional (F3 / invariant 19).
- ❌ Don't call `select_*_map_os` more than once per dispatch or cache OS maps in a global — resolve them fresh each call (they must reflect `current_os` at dispatch time).
- ❌ Don't re-apply S1's block — S1 is ALREADY in the repo (verified). S2 edits only `process_full_message` and adds `notifier_set_os`.
- ❌ Don't run the *current* `run_notifier_stub_tests.sh` and treat a [2/3] link failure as a defect — its link step lacks `-Iqmk_stubs` until P1.M2.T2.S1. Use the Level-2 corrected-flag harness.
- ❌ Don't touch `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`, or `.gitignore`.

---

## Confidence Score: 10/10

The deliverable is a precise, two-region edit to `notifier.c` (rewrite
`process_full_message` body + add `notifier_set_os`), whose exact target code
(verbatim from PRD §8.6/§8.7, mapped to C in the style of the existing file),
exact anchors (the `process_full_message` body; the gap before `hid_notify`),
exact behavior contract (F8.4/F8.5/F8.6 + F9.1/F9.2/F9.3 + invariant 4/6/17/19),
and exact F2 fix (`command_found->pattern`) are fully specified above and were
**empirically validated during research**: the end-state (post-S1 + S2)
stub-compiles with **0 warnings** (S1's unused-selector warnings cleared),
`test_notifier_dispatch` passes **11/11** (backward-compat canary — invariant 19
proven structurally), and a **30/30** focused multi-OS test covers every §11.2D
category (F8.4 cmd/layer OS-prevents-default; F8.4 fallback; F8.5 independent
tracks both directions; F8.6 OS_UNSURE→default; F9.1 clear-on-change; F9.2
no-re-dispatch; F9.3 idempotent; OS_LINUX no-overrides→default). S1 is already
landed in the repo (verified), so the two S2 regions do not overlap S1's
insertion. The one sequencing caveat (the runner's `-Iqmk_stubs` link-step gap,
fixed by P1.M2.T2.S1) is explicitly documented and handled with a
corrected-flag harness that does not depend on it. No external dependencies are
added; scope boundaries with S1, P1.M2.T1.S1 (the official test), and
P1.M2.T2.S1 (the runner fix) are explicit.