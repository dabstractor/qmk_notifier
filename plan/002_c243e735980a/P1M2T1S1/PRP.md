# PRP — P1.M2.T1.S1: Write `test_notifier_os.c` covering F8/F9 (six §11.2D categories)

## Goal

**Feature Goal**: Create the OFFICIAL host-side test `test_notifier_os.c` that
stub-compiles the full multi-OS `notifier.c` and verifies the F8 (multi-OS map
selection) and F9 (OS-change state clearing) contract end-to-end through
`process_full_message` + `notifier_set_os`, covering all six PRD §11.2D
categories (i–vi) with **0 `FAIL:` lines**. The test follows the
`test_notifier_dispatch.c` framework pattern and uses **distinguishable
callbacks** + **distinct layer numbers** + a 1-line test-harness layer accessor
to observe WHICH map matched (findings F6).

**Deliverable**: (1) a NEW file `test_notifier_os.c` at the repo root, and
(2) a 1-line addition to `qmk_stubs/qmk_stubs.c` — the `stub_get_active_layer()`
observable (test-harness code, Mode-A documented; NOT production code).

**Success Definition**:
- `test_notifier_os.c` compiles + links + runs with the EXACT §11.1 command:
  `gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99`
  → exit 0.
- The run prints **0 `FAIL:`** lines and a summary
  `Total tests run: N / passed: P / failed: F` with `F == 0`, exit 0.
- All six §11.2D categories are exercised (each mapped to a labeled test block
  in the header comment, Mode A): (i) OS map wins + default skipped per track;
  (ii) default fallback {OS-absent / OS-matches-nothing / OS_UNSURE}; (iii)
  command & layer tracks fall back independently; (iv) `notifier_set_os`
  idempotent on unchanged value; (v) `notifier_set_os` on changed value clears
  state without re-dispatch; (vi) default-only behavior for a no-override OS
  (backward-compat).
- `test_notifier_dispatch` (the backward-compat canary) STILL passes 11/11, 0
  FAIL (this task does not regress it).
- No edits to `notifier.c`, `notifier.h`, `pattern_match.*`,
  `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`.

## User Persona (if applicable)

**Target User**: The maintainer running the acceptance gate (PRD §11.2D) and any
future contributor changing the multi-OS dispatch. The suite is a regression gate:
if it flips red, the dispatch logic broke (and the test is right — invariant 12).

**Use Case**: After `notifier.c` lands the OS-first/default-fallback dispatch +
`notifier_set_os`, `./run_notifier_stub_tests.sh` (extended by P1.M2.T2.S1)
builds and runs `test_notifier_os`; it must report 0 `FAIL:` for the §11.2D gate
to pass.

**User Journey**: developer edits `notifier.c` → `gcc … test_notifier_os.c …` →
the suite drives `process_full_message` with crafted messages under controlled
`current_os` values (set via `notifier_set_os`), observing callback flags +
active layer → any regression surfaces as a `FAIL:` line + non-zero exit.

**Pain Points Addressed**: The multi-OS rule (F8.4/F8.5) is subtle — per-track
OS-first/default-fallback with independent tracks — and `notifier_set_os`'s
clear-on-change/idempotent/no-re-dispatch contract (F9) is easy to break silently.
A deterministic host test (no real QMK / HID hardware) is the only practical way
to lock these invariants before firmware flashes.

## Why

- **Closes the §11.2D acceptance gate**: PRD §11.2D requires
  `test_notifier_os` to verify criteria i–vi; until this suite exists, the
  multi-OS feature has no regression gate and the §11.2D gate cannot be marked
  complete (it is the last code deliverable before P1.M2.T2.S1 wires the runner
  and P1.M2.T3.S1 updates the README).
- **Locks the F8/F9 invariants deterministically**: the dispatch is OS-first per
  track (F8.4), tracks are independent (F8.5), OS_UNSURE ⇒ default (F8.6), and
  `notifier_set_os` is idempotent (F9.3) / clears-on-change (F9.1) /
  no-re-dispatch (F9.2). Each is a one-line code change away from regression;
  this suite makes every one of them a hard, observable assertion.
- **Consumes the now-landed S2 contract**: P1.M1.T3.S1 (provider) and
  P1.M1.T3.S2 (consumer: `notifier_set_os` + rewritten `process_full_message`)
  are both COMPLETE and verified (selectors wired at notifier.c:362;
  `notifier_set_os` at notifier.c:459; `test_notifier_dispatch` still 11/11).
  This test exercises that contract through its public surface.
- **Proves backward-compat structurally** (invariant 19): criterion (vi) asserts
  that an OS with no `DEFINE_*_OS` overrides behaves identically with/without
  `notifier_set_os` (default maps still consulted); the fully-default-only
  keymap case is `test_notifier_dispatch` (separate binary, 11/11, never calls
  `set_os`).

## What

Two file changes:

1. **CREATE `test_notifier_os.c`** (repo root): a standalone C test TU that
   `#include`s `notifier.h`, declares the non-static `notifier.c` entry points
   (`match_pattern`, `process_full_message`, `hid_notify`, `notifier_set_os`),
   defines `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS` (defaults) +
   `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)`/`DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…)`
   (overrides) at file scope with **distinguishable callbacks** (separate
   `os_cmd_on/off` vs `def_cmd_on/off` flags) and **distinct layer numbers**
   (OS: 11/44; default: 22/33), and drives `process_full_message` under
   controlled `current_os` values, asserting return value + callback flags +
   `stub_get_active_layer()`. Six labeled blocks map to §11.2D (i)–(vi).

2. **MODIFY `qmk_stubs/qmk_stubs.c`**: append ONE observable accessor
   `uint8_t stub_get_active_layer(void) { return g_active_layer; }` (with a
   Mode-A comment marking it test-harness-only). This exposes the file-static
   `g_active_layer` so the test can assert which layer won (findings F6).

### Success Criteria

- [ ] `test_notifier_os.c` exists at repo root; builds+runs with the exact §11.1
      command (exit 0); prints 0 `FAIL:`; summary `failed: 0`; exit 0.
- [ ] Header comment (Mode A) maps each test block to its §11.2D criterion (i–vi)
      and the PRD F8/F9 section it validates.
- [ ] All six §11.2D categories exercised (see "Test-case → criterion map").
- [ ] Distinguishable callbacks present (`os_cmd_*` vs `def_cmd_*`); distinct
      layer numbers (OS≠default); `stub_get_active_layer()` used for layer asserts.
- [ ] `qmk_stubs/qmk_stubs.c` has the `stub_get_active_layer()` accessor (1 line +
      Mode A comment); no other change to that file.
- [ ] `test_notifier_dispatch` still 11/11, 0 FAIL (no regression).
- [ ] No edits to `notifier.c`, `notifier.h`, `pattern_match.*`,
      `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact test TU (a clean, Mode-A-documented version of the
research-validated `test_notifier_os_val.c` which passed **30/30**) is given
verbatim in "Implementation Tasks". The maps design, the distinguishable-callback
observation (F6), the `stub_get_active_layer()` accessor, the build command, and
the §11.2D (i)–(vi) coverage were **all executed against the current post-S2
`notifier.c` during research and passed with 0 FAIL**. An implementer with only
this PRP + repo access can produce the test and prove it green.

### Documentation & References

```yaml
# MUST READ — the test-framework pattern to follow EXACTLY
- file: test_notifier_dispatch.c
  why: "The canonical stub-harness test: #include \"notifier.h\"; extern decls for
        the non-static notifier.c entry points (match_pattern/process_full_message/
        hid_notify); DEFINE_SERIAL_* at FILE SCOPE; static int g_pass/g_fail;
        a ck-style helper printing PASS:/FAIL:; final 'Total tests run: N /
        passed: P / failed: F'; return g_fail?1:0. The runner greps 'grep -c ^FAIL:'."
  pattern: "DEFINE_* at file scope override the weak get_*_map accessors for THIS
            TU only. test_notifier_os.c is a SEPARATE binary, so its DEFINE_* do
            NOT collide with test_notifier_dispatch.c's."
  critical: "Add `void notifier_set_os(os_variant_t os);` to the extern-style
             decls (it is the OS entry point under test). Use a CK(cond,name)
             macro (not the match_pattern-only ck) because these tests assert on
             process_full_message return + callback flags + active layer."

# MUST READ — the contract under test (F8 + F9)
- file: PRD.md   (also plan/002_c243e735980a/prd_snapshot.md)
  section: "### F8 — Multi-OS map selection (opt-in overlay)" + "### F9 — OS-change state clearing"
  why: "The exact semantics each block asserts: F8.4 (OS match PREVENTS default
        scan, per track); F8.5 (tracks decide INDEPENDENTLY); F8.6 (OS_UNSURE ⇒
        default only); F9.1 (changed value clears state: disable_command +
        deactivate_layer); F9.2 (no re-dispatch); F9.3 (idempotent on same value)."
  critical: "F8.4: 'A match there wins and the default map for that type is not
             consulted.' F8.5: 'command track and layer track each make this
             decision INDEPENDENTLY.' F9.2: 'does not re-dispatch the last message.'

- file: PRD.md
  section: "## 11.2 (D) Multi-OS selection" + "## 11.3 Test inventory (test_notifier_os row)"
  why: "§11.2D lists the six MUST-verify criteria (i–vi) — the literal spec this
        test encodes. §11.3 says test_notifier_os covers 'F8 merge/fallback per
        track, per-map-type independence, OS_UNSURE→default, F9 clear-on-change
        idempotence', PASS/FAIL style."
  critical: "(vi) 'a default-only configuration (no DEFINE_*_OS) behaves identically
             with/without notifier_set_os'. In a single TU that DOES define
             OS_MACOS overrides, demonstrate (vi) by switching to an OS with NO
             overrides defined (OS_WINDOWS) and asserting defaults still fire; the
             fully-default-only keymap case is test_notifier_dispatch.c."

# MUST READ — the exact build command
- file: PRD.md
  section: "### 11.1 Build all suites (the test_notifier_os line)"
  why: "Authoritative one-shot compile+link+run command (copy/paste). NOTE: it has
        NO -Wall/-Wextra — only -std=c99. It compiles all sources in one gcc call
        (unlike run_notifier_stub_tests.sh which object-compiles notifier.c first)."
  critical: "Flags: -DQMK_KEYBOARD_H='\"qmk_keyboard_stub.h\"' -Iqmk_stubs -I. -std=c99.
             -Iqmk_stubs is REQUIRED (notifier.h includes os_detection.h). This
             §11.1 command already has it, so this test is unaffected by the
             run_notifier_stub_tests.sh [2/3] -Iqmk_stubs gap (fixed by P1.M2.T2.S1)."

# The macros this test uses
- file: notifier.h
  section: "DEFINE_SERIAL_COMMANDS / DEFINE_SERIAL_LAYERS / DEFINE_SERIAL_COMMANDS_OS / DEFINE_SERIAL_LAYERS_OS"
  why: "DEFINE_*_OS(os, {…}) generate the strong _notifier_*_map_OS_<os> symbols
        (##os token-paste) that override the weak per-OS accessors select_*_map_os
        dispatches to. A row has the SAME shape as the default macro row:
        { pattern, on_enable, on_disable, case_sensitive? } (commands);
        { pattern, layer, case_sensitive? } (layers)."
  critical: "case_sensitive is the LAST field and OPTIONAL (omits→false). To stay
             -Wall-clean add an explicit `, false` to every row (or compile without
             -Wall, which §11.1 does). os is an os_variant_t ENUMERATOR token
             (OS_MACOS), NOT a string. OS_UNSURE has NO _OS macro (F8.6)."

# The observation-strategy doc (F6)
- file: plan/002_c243e735980a/architecture/findings_and_risks.md
  section: "### F6. Test observation strategy for test_notifier_os.c"
  why: "Dictates HOW the test observes which map matched: distinguishable callbacks
        (separate os_cmd_* / def_cmd_* functions setting separate flags) for the
        command track; distinct layer numbers + stub_get_active_layer() for the
        layer track. The accessor is 'a 1-line addition, Mode A — enhances the test
        harness, not production code; the implementer decides the cleanest path.'"
  critical: "The stub layer_on/layer_off write to a FILE-STATIC g_active_layer the
             test cannot read. Without the accessor you can only assert return value,
             not WHICH layer won — insufficient for F8.4 layer / F8.5 / F9.1."

# The stub harness (qmk_stubs.c is MODIFIED by this task — append the accessor)
- file: qmk_stubs/qmk_stubs.c
  why: "Provides layer_on/layer_off (write file-static g_active_layer) and
        raw_hid_send (the ack the dispatcher calls). THIS task APPENDS one
        accessor: `uint8_t stub_get_active_layer(void) { return g_active_layer; }`."
  pattern: "g_active_layer is `static uint8_t` (line 6), init 255; layer_on sets it,
            layer_off resets to 255. The accessor just returns it."
  critical: "Append ONLY. Do not change layer_on/layer_off/raw_hid_send or the
             existing stderr traces (test_notifier_dispatch relies on the current
             behavior). The accessor is test-harness code, NOT production."

# The validated blueprint (research artifact — the official test's structural basis)
- file: plan/002_c243e735980a/P1M1T3S2/research/test_notifier_os_val.c
  why: "A 30/30-passing focused multi-OS test (research artifact, NOT the official
        test). Its maps design (blender collision / neovide default-cmd-only /
        iTerm OS-layer-only / calculator default-layer-only) and its per-criterion
        assertions are the structural basis for test_notifier_os.c. Clean it up:
        remove the 'thinking out loud' comments, add the Mode-A §11.2D header
        mapping, add the explicit (vi) block."
  critical: "It declared `extern uint8_t stub_get_active_layer(void);` and used a
             #define CK(cond,name) macro — follow that. It got 30/30 against the
             CURRENT notifier.c (verified during this research)."

# The contract this test exercises (LANDED — treat as ground truth)
- file: plan/002_c243e735980a/P1M1T3S2/PRP.md
  why: "Defines the exact notifier_set_os + process_full_message bodies this test
        drives. Confirms they are LANDED (notifier.c:334 process_full_message,
        notifier.c:459 notifier_set_os) and test_notifier_dispatch is 11/11."
  critical: "Do NOT modify notifier.c — S2 is complete. This test only CONSUMES it."

# The OS enum (for the OS_WINDOWS / OS_LINUX / OS_MACOS / OS_UNSURE tokens)
- file: qmk_stubs/os_detection.h
  why: "os_variant_t = { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4 }.
        current_os boots to OS_UNSURE. notifier_set_os takes this type."
  critical: "OS_UNSURE (boot state) ⇒ selectors return {NULL,0} ⇒ default maps. This
             is the (ii)c and F8.6 assertion."
```

### Current Codebase tree (relevant slice — POST-S2 state)

```bash
notifier.c                 # LANDED (post-S2): process_full_message (OS-first/default-fallback,
                           #   line 334) + notifier_set_os (line 459). DO NOT TOUCH.
notifier.h                 # LANDED: DEFINE_*_OS macros + notifier_set_os decl. DO NOT TOUCH.
pattern_match.{c,h}        # unaffected. DO NOT TOUCH.
qmk_stubs/
  os_detection.h           # os_variant_t enum. DO NOT TOUCH.
  qmk_keyboard_stub.h      # QMK_KEYBOARD_H stand-in. DO NOT TOUCH.
  raw_hid.h                # raw_hid_send decl. DO NOT TOUCH.
  qmk_stubs.c              # ← MODIFY (append stub_get_active_layer accessor). ONLY this.
test_notifier_dispatch.c   # backward-compat canary (11/11). DO NOT TOUCH.
run_notifier_stub_tests.sh # stub gate; P1.M2.T2.S1 will extend it to build+run test_notifier_os. DO NOT TOUCH.
run_all_tests.sh           # 9-suite pattern_match corpus — unaffected. DO NOT TOUCH.
test_notifier_os.c         # ← CREATE (this task). DOES NOT EXIST YET.
PRD.md                     # READ-ONLY.
```

### Desired Codebase tree with files to be added/changed

```bash
test_notifier_os.c         # NEW: official §11.2D F8/F9 host test (6 categories, 0 FAIL).
qmk_stubs/qmk_stubs.c      # MODIFIED: +1 accessor stub_get_active_layer() (test-harness observable).
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (separate binary): test_notifier_os.c is compiled as its OWN binary
//   (§11.1 line), NOT linked into test_notifier_dispatch. So the file-scope
//   DEFINE_SERIAL_* here do NOT collide with test_notifier_dispatch.c's. Each
//   TU defines its own user_command_map/user_layer_map globals + its own
//   _notifier_*_map_OS_MACOS overrides. This is why you can define OS_MACOS
//   overrides here while test_notifier_dispatch defines none.

// CRITICAL (g_active_layer is file-static — F6): the stub layer_on/layer_off in
//   qmk_stubs.c track the active layer in a `static uint8_t g_active_layer` the
//   test cannot read. You CANNOT assert which layer won without an accessor.
//   The contract-permitted fix: append `stub_get_active_layer()` to qmk_stubs.c
//   (1 line + Mode A comment). Do NOT remove the `static` from g_active_layer
//   (that would change its linkage and could mask bugs) — just add the accessor.

// CRITICAL (current_os is global + persistent across test cases): current_os
//   is a single global in notifier.c (OS_UNSURE at boot). It persists across
//   your process_full_message calls. So test cases are ORDER-DEPENDENT: you
//   set the OS with notifier_set_os and it STAYS until the next set_os. Design
//   the test sequence to flow: OS_UNSURE (boot) → OS_MACOS → (idempotent) →
//   OS_LINUX/OS_WINDOWS (no overrides). reset_flags() before each assertion
//   block; do NOT assume current_os resets between blocks.

// CRITICAL (disable-before-scan pollutes flags): process_full_message ALWAYS
//   calls disable_command() first (fires the PREVIOUS command's on_disable).
//   So after dispatching message A (cmd active) then message B, the on_disable
//   for A fires during B's dispatch. To assert cleanly, reset_flags() right
//   before the dispatch you are measuring, and assert the flags reflect ONLY
//   that one dispatch (enable fires once, disable of the PREV may also fire —
//   account for it, or assert enable-count == 1 which is unambiguous).

// GOTCHA (notifier_set_os clears state — F9.1/F9.2): when set_os changes the OS,
//   it calls disable_command() (fires prev on_disable) + deactivate_layer()
//   (active layer → 255), and does NOT re-dispatch. So after set_os(CHANGED):
//   on_disable of the prev cmd fired (os_cmd_dis==1), active layer == 255, and
//   NO on_enable re-fired (os_cmd_en==0, def_cmd_en==0). Assert all three.

// GOTCHA (idempotency FIRST — F9.3): notifier_set_os(SAME) returns immediately
//   BEFORE disable/deactivate. So set_os(OS_MACOS) while already OS_MACOS: no
//   on_disable, active layer UNCHANGED. Assert on_disable count == 0 and
//   stub_get_active_layer() == (the layer that was active before the call).

// GOTCHA (-Wall and missing-field-initializers): with -Wall -Wextra, map rows
//   that omit the trailing case_sensitive field warn (-Wmissing-field-initializers,
//   7× in the validated test). §11.1's build command has NO -Wall, so it is silent.
//   For cleanliness, add an explicit `, false` to every map row (the field IS
//   optional and zero-fills to false — behavior identical either way).

// GOTCHA (case_sensitive field is LAST, optional): command_map_t row shape is
//   { pattern, on_enable, on_disable, case_sensitive? }; layer_map_t is
//   { pattern, layer, case_sensitive? }. Omitting case_sensitive → false.
//   All test patterns are case-insensitive by design (matches the reference
//   keymap style), so `, false` or omitted are equivalent.

// GOTCHA (WT() / GS_DELIMITER): WT(class, title) expands to class "\x1D" title.
//   A WT pattern is a TWO-PART pattern; match_pattern splits on GS (0x1D). For
//   single-part messages (no GS), a WT pattern matches its class half against
//   the whole message (F4.3). The validated test uses bare patterns mostly;
//   WT("blender","*") is used for the collision case and matches any message
//   whose class half matches "blender".

// GOTCHA (build is one-shot, no object step): §11.1 compiles notifier.c +
//   qmk_stubs.c + test_notifier_os.c in ONE gcc invocation. Do NOT pre-compile
//   notifier.c to an object with different flags then link — just use the exact
//   §11.1 line. (run_notifier_stub_tests.sh object-compiles first, but that is
//   P1.M2.T2.S1's concern, not this task's.)
```

## Implementation Blueprint

### Data models and structure

No production types. The test defines:
- Four file-scope **callback functions** setting distinct flags:
  `os_cmd_on/os_cmd_off` (OS command track) and `def_cmd_on/def_cmd_off`
  (default command track).
- Four file-scope **map globals** via the `DEFINE_*` / `DEFINE_*_OS` macros
  (default commands + default layers + OS_MACOS commands + OS_MACOS layers).
- The `stub_get_active_layer()` observable in `qmk_stubs.c`.
- A `#define CK(cond, name)` assertion macro + `g_pass`/`g_fail` counters + a
  `reset_flags()` helper.

### Map design (validated 30/30 — each message targets a §11.2D criterion)

```c
/* DEFAULT (OS-agnostic). Distinct layer numbers from OS maps (22/33 vs 11/44). */
DEFINE_SERIAL_COMMANDS({
    { "neovide",          def_cmd_on, def_cmd_off, false },   /* default-only cmd        */
    { WT("blender", "*"), def_cmd_on, def_cmd_off, false },   /* collides w/ OS map (i)  */
});
DEFINE_SERIAL_LAYERS({
    { "blender",   22, false },   /* default layer; OS layer for same msg = 11 */
    { "calculator", 33, false },  /* default-only layer                        */
});
/* OS_MACOS (strong overrides; symbols via ##os paste in notifier.h). */
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { WT("blender", "*"), os_cmd_on, os_cmd_off, false },     /* collides w/ default (i) */
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "blender", 11, false },   /* collides w/ default 22 (i) */
    { "iTerm",   44, false },   /* OS-only layer (iii)        */
});
```

### Test-case → §11.2D criterion map

| Block | current_os | message | Asserts | Criterion |
|---|---|---|---|---|
| (i) cmd | OS_MACOS | `"blender"` | `os_cmd_en==1 && def_cmd_en==0` | OS cmd wins, default skipped |
| (i) layer | OS_MACOS | `"blender"` | `stub_get_active_layer()==11` | OS layer wins over default 22 |
| (ii)b | OS_MACOS | `"calculator"` | `stub_get_active_layer()==33` | OS exists, no match → default |
| (iii) L-from-OS | OS_MACOS | `"iTerm"` | layer==44, `os_cmd_en==0 && def_cmd_en==0` | layer from OS, cmd from nothing |
| (iii) C-from-default | OS_MACOS | `"neovide"` | `def_cmd_en==1`, layer==255 | cmd from default, layer from nothing |
| (iv) idempotent | OS_MACOS→OS_MACOS | (prev: blender active) | `os_cmd_dis==0`, layer==11 (unchanged) | F9.3 no-op |
| (v) clear+no-redispatch | OS_MACOS→OS_LINUX | (prev: blender active) | `os_cmd_dis==1`, layer==255, `os_cmd_en==0 && def_cmd_en==0` | F9.1 + F9.2 |
| (ii)c OS_UNSURE | OS_UNSURE (boot) | `"iTerm"` | `r==0`, no cmd fired | F8.6 OS map inert |
| (ii)c OS_UNSURE | OS_UNSURE (boot) | `"calculator"` | `r==1`, layer==33 | F8.6 default matches |
| (ii)a / (vi) no-override OS | OS_WINDOWS | `"blender"` | `def_cmd_en==1`, layer==22 | default-only behavior (backward-compat) |

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY qmk_stubs/qmk_stubs.c — APPEND the layer observable
  - PLACE: at END of the file (after raw_hid_send's closing brace).
  - ADD exactly (Mode A comment + 1-line function):
        /* Test-harness observable (NOT production code): exposes the file-static
         * g_active_layer so host tests (test_notifier_os.c) can assert WHICH layer
         * won. In a real QMK build layer_state provides this; the stub tracks it
         * locally. Added for the multi-OS test harness (findings F6). */
        uint8_t stub_get_active_layer(void) { return g_active_layer; }
  - PRESERVE: g_active_layer declaration, layer_on/layer_off/raw_hid_send verbatim.
  - DO NOT remove `static` from g_active_layer (just expose via accessor).

Task 2: CREATE test_notifier_os.c (repo root) — the official §11.2D test
  - STRUCTURE (top→bottom):
      1. Mode-A header comment mapping each block to §11.2D (i)–(vi) + the F8/F9
         section each validates (item-spec §DOCS).
      2. #include <stdint.h> <stdbool.h> <string.h> <stdio.h> "notifier.h".
      3. extern-style decls: bool match_pattern(...); bool process_full_message(char*);
         void hid_notify(uint8_t*,uint8_t); void notifier_set_os(os_variant_t);
         uint8_t stub_get_active_layer(void);   /* from qmk_stubs.c (Task 1) */
      4. distinguishable callbacks: static int os_cmd_en/os_cmd_dis/def_cmd_en/def_cmd_dis;
         four functions incrementing them.
      5. DEFINE_SERIAL_COMMANDS / DEFINE_SERIAL_LAYERS (defaults) +
         DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…) / DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…) (overrides),
         using the map design above (each row with trailing `, false`).
      6. static int g_pass, g_fail; #define CK(cond,name) do{…}while(0); reset_flags().
      7. int main(void): the six labeled test blocks in dependency order (OS_UNSURE
         boot cases FIRST since current_os starts OS_UNSURE; then set_os(OS_MACOS)
         and the OS_MACOS cases; then idempotent; then clear-on-change; then
         no-override-OS backward-compat). Print the Total summary; return g_fail?1:0.
  - FOLLOW pattern: test_notifier_dispatch.c (framework) +
    P1M1T3S2/research/test_notifier_os_val.c (validated assertions, cleaned up).
  - NAMING: g_pass/g_fail/CK consistent with the dispatch suite; os_cmd_* / def_cmd_*
    flag names encode which map matched (F6).
  - DEPENDENCIES: notifier.c (LANDED), qmk_stubs.c (Task 1 accessor), notifier.h
    (DEFINE_*_OS macros, LANDED), os_detection.h (os_variant_t, LANDED).

Task 3: VERIFY — build + run + no regression
  - Run Validation Level 1 (exact §11.1 build; exit 0).
  - Run Validation Level 2 (run; 0 FAIL; summary failed: 0).
  - Run Validation Level 3 (test_notifier_dispatch still 11/11).
  - Run Validation Level 4 (Mode-A header greps; all 6 criteria present).
```

**The exact `test_notifier_os.c` to write** (cleaned + Mode-A-documented version
of the research-validated `test_notifier_os_val.c`; behavior-identical, 30/30
proven). This is the authoritative content:

```c
/* test_notifier_os.c — multi-OS map-selection (F8) + OS-change clearing (F9)
 * host test. Stub-compiles notifier.c and drives process_full_message +
 * notifier_set_os, covering the SIX PRD §11.2D categories:
 *
 *   (i)   OS-specific map selected + DEFAULT SKIPPED when current_os is set and
 *         the OS map matches — per track (command AND layer). [F8.4]
 *   (ii)  DEFAULT map used as fallback when (a) the OS map is ABSENT for
 *         current_os, (b) the OS map exists but matches NOTHING, or
 *         (c) current_os == OS_UNSURE. [F8.4 fallback / F8.6]
 *   (iii) command and layer tracks fall back INDEPENDENTLY: a message may match
 *         an OS layer but only a default command, or vice versa. [F8.5]
 *   (iv)  notifier_set_os IDEMPOTENT on an unchanged value — no spurious
 *         on_disable / deactivate. [F9.3]
 *   (v)   notifier_set_os on a CHANGED value clears state (prev on_disable fires,
 *         active layer deactivated) and does NOT re-dispatch. [F9.1 / F9.2]
 *   (vi)  a no-override OS (no DEFINE_*_OS for it) behaves identically with/
 *         without notifier_set_os — default maps still consulted (backward-compat).
 *         [invariant 19; the fully-default-only keymap is test_notifier_dispatch.c]
 *
 * Observation (findings F6): distinguishable callbacks (os_cmd_* vs def_cmd_*)
 * reveal WHICH command map matched; distinct layer numbers (OS: 11/44 vs default:
 * 22/33) + stub_get_active_layer() reveal WHICH layer won. Build (PRD §11.1):
 *   gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
 *       notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"

/* Non-static entry points implemented in notifier.c (#includes pattern_match.c). */
bool  match_pattern(const char *pattern, const char *message, bool case_sensitive);
bool  process_full_message(char *data);
void  hid_notify(uint8_t *data, uint8_t length);
void  notifier_set_os(os_variant_t os);
uint8_t stub_get_active_layer(void);   /* test-harness observable in qmk_stubs.c */

/* --- distinguishable command callbacks (findings F6) --- */
static int os_cmd_en = 0, os_cmd_dis = 0, def_cmd_en = 0, def_cmd_dis = 0;
static void os_cmd_on(void)  { os_cmd_en++;  }
static void os_cmd_off(void) { os_cmd_dis++; }
static void def_cmd_on(void) { def_cmd_en++; }
static void def_cmd_off(void){ def_cmd_dis++; }

/* DEFAULT maps (OS-agnostic). Distinct layer numbers from the OS maps. */
DEFINE_SERIAL_COMMANDS({
    { "neovide",          def_cmd_on, def_cmd_off, false },   /* default-only cmd          */
    { WT("blender", "*"), def_cmd_on, def_cmd_off, false },   /* collides with OS cmd (i)  */
});
DEFINE_SERIAL_LAYERS({
    { "blender",   22, false },   /* default layer; OS layer for same msg = 11 */
    { "calculator", 33, false },  /* default-only layer                        */
});

/* OS_MACOS maps (strong overrides; symbol names via ##os paste in notifier.h). */
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { WT("blender", "*"), os_cmd_on, os_cmd_off, false },     /* collides with default cmd (i) */
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "blender", 11, false },   /* collides with default layer 22 (i) */
    { "iTerm",   44, false },   /* OS-only layer (iii)                */
});

static int g_pass = 0, g_fail = 0;
#define CK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS: %s\n", name); } \
    else      { g_fail++; printf("FAIL: %s\n", name); } \
} while (0)

static void reset_flags(void) {
    os_cmd_en = os_cmd_dis = def_cmd_en = def_cmd_dis = 0;
}

int main(void) {
    /* current_os boots to OS_UNSURE (notifier.c §8.1). Do the OS_UNSURE cases
     * FIRST, before any notifier_set_os call. */

    /* ===== (ii)c / F8.6: OS_UNSURE => OS map inert, default maps only =====
     * "iTerm" exists ONLY in the OS_MACOS layer map; at OS_UNSURE the OS map is
     * not consulted (select_*_map_os returns {NULL,0}) so it must NOT match. */
    {
        char m[] = "iTerm";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 0, "(ii)c OS_UNSURE: OS-only pattern (iTerm) does NOT match (OS map inert) [F8.6]");
        CK(os_cmd_en == 0 && def_cmd_en == 0, "(ii)c OS_UNSURE: no command fired for OS-only pattern [F8.6]");
    }
    {
        char m[] = "calculator";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(ii)c OS_UNSURE: default layer (calculator->33) matches [F8.6]");
        CK(stub_get_active_layer() == 33, "(ii)c OS_UNSURE: default layer 33 activated [F8.6]");
    }

    /* ===== switch to OS_MACOS (this also exercises F9.1 clear-on-change) ===== */
    {
        /* Pre-condition: layer 33 active from the calculator case above. */
        notifier_set_os(OS_MACOS);
        CK(stub_get_active_layer() == 255, "(v setup) notifier_set_os(OS_MACOS) deactivated prev layer (clear-on-change) [F9.1]");
    }

    /* ===== (i) OS map wins + DEFAULT SKIPPED — command track [F8.4] =====
     * "blender" matches BOTH the OS_MACOS cmd (os_cmd_on) AND the default cmd
     * (def_cmd_on). OS map scanned first => os_cmd fires, default NOT scanned. */
    {
        char m[] = "blender";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(i) cmd: blender matches (return true) [F8.4]");
        CK(os_cmd_en == 1, "(i) cmd: OS_MACOS command callback fired (os_cmd_on) [F8.4]");
        CK(def_cmd_en == 0, "(i) cmd: default command NOT scanned (OS match prevents default) [F8.4 core]");
    }

    /* ===== (i) OS map wins + DEFAULT SKIPPED — layer track [F8.4] =====
     * "blender" layer: OS_MACOS=11 vs default=22. OS wins => active layer 11. */
    {
        char m[] = "blender";
        reset_flags();
        process_full_message(m);
        CK(stub_get_active_layer() == 11, "(i) layer: OS_MACOS layer 11 won over default 22 (OS prevents default) [F8.4]");
    }

    /* ===== (iii) tracks fall back INDEPENDENTLY [F8.5] =====
     * "iTerm": OS_MACOS layer 44 fires (no default layer for iTerm => layer track
     *          resolved from OS) while the command track resolves to NOTHING
     *          (no OS cmd, no default cmd). The two tracks decided independently. */
    {
        char m[] = "iTerm";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(iii) iTerm matches (OS layer) [F8.5]");
        CK(stub_get_active_layer() == 44, "(iii) layer resolved from OS_MACOS map (44) [F8.5]");
        CK(os_cmd_en == 0 && def_cmd_en == 0, "(iii) command track decided independently (no cmd match) [F8.5]");
    }
    /* "neovide": default cmd fires (no OS cmd for neovide => command track fell
     *            back to default) while the layer track resolves to NOTHING. */
    {
        char m[] = "neovide";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(iii) neovide matches (default cmd) [F8.5]");
        CK(def_cmd_en == 1, "(iii) command resolved from DEFAULT map (def_cmd_on) [F8.5]");
        CK(os_cmd_en == 0, "(iii) OS cmd not matched for neovide [F8.5]");
        CK(stub_get_active_layer() == 255, "(iii) layer track decided independently (no layer match) [F8.5]");
    }

    /* ===== (ii)b DEFAULT fallback: OS map exists but matches NOTHING [F8.4] =====
     * "calculator": no OS_MACOS layer entry => OS layer scan misses => fall back
     * to default layer 33. (current_os is still OS_MACOS, so the OS map IS
     * consulted, it just matches nothing.) */
    {
        char m[] = "calculator";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(ii)b fallback: calculator matches via default layer [F8.4]");
        CK(stub_get_active_layer() == 33, "(ii)b fallback: OS no-match => default layer 33 activated [F8.4]");
    }

    /* ===== (iv) notifier_set_os IDEMPOTENT on unchanged value [F9.3] ===== */
    {
        char m[] = "blender";
        reset_flags();
        process_full_message(m);                 /* os_cmd_on fired, layer 11 active */
        CK(os_cmd_en == 1, "(iv) setup: os_cmd active");
        notifier_set_os(OS_MACOS);               /* SAME os => idempotent no-op */
        CK(os_cmd_dis == 0, "(iv) idempotent: on_disable NOT fired on same-OS call [F9.3]");
        CK(def_cmd_dis == 0, "(iv) idempotent: no spurious disable [F9.3]");
        CK(stub_get_active_layer() == 11, "(iv) idempotent: layer NOT cleared on same-OS call [F9.3]");
    }

    /* ===== (v) notifier_set_os on CHANGED value clears state + no re-dispatch [F9.1/F9.2] ===== */
    {
        char m[] = "blender";
        process_full_message(m);                 /* re-establish: os_cmd active, layer 11 */
        reset_flags();
        CK(stub_get_active_layer() == 11, "(v) setup: layer 11 active");
        notifier_set_os(OS_LINUX);               /* CHANGED os => clear state */
        CK(os_cmd_dis == 1, "(v) on-change: prev command on_disable fired [F9.1]");
        CK(stub_get_active_layer() == 255, "(v) on-change: active layer deactivated (cleared) [F9.1]");
        CK(os_cmd_en == 0 && def_cmd_en == 0, "(v) no-re-dispatch: on_enable NOT re-fired after OS change [F9.2]");
        CK(stub_get_active_layer() == 255, "(v) no-re-dispatch: no layer re-activated by notifier_set_os [F9.2]");
    }

    /* ===== (ii)a / (vi) no-override OS behaves default-only (backward-compat) [invariant 19] =====
     * OS_WINDOWS has NO DEFINE_*_OS in this TU, so its selectors return {NULL,0}
     * and dispatch uses the default maps — identical to never having called
     * set_os. This is the per-OS backward-compat guarantee; the fully-default-only
     * keymap (zero DEFINE_*_OS at all) is proven by test_notifier_dispatch.c (11/11). */
    {
        notifier_set_os(OS_WINDOWS);             /* no overrides for WINDOWS */
        char m[] = "blender";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(vi)/(ii)a no-override OS: blender matches default cmd [invariant 19]");
        CK(def_cmd_en == 1, "(vi)/(ii)a no-override OS: DEFAULT command fired (no OS_WINDOWS map) [invariant 19]");
        CK(os_cmd_en == 0, "(vi)/(ii)a no-override OS: OS cmd map inert for WINDOWS [invariant 19]");
        CK(stub_get_active_layer() == 22, "(vi)/(ii)a no-override OS: default layer 22 (no OS_WINDOWS layer) [invariant 19]");
    }

    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: distinguishable callbacks are the ONLY way to observe WHICH command
//   map matched (the command track has no return-value distinguishing OS vs
//   default). Four functions setting four flags: os_cmd_on/off, def_cmd_on/off.
//   reset_flags() zeroes all four before each measured dispatch.

// PATTERN: distinct layer numbers (OS 11/44 vs default 22/33) + stub_get_active_layer()
//   observe the layer track. The value uniquely identifies the winning map.
//   255 (LAYER_UNSET) means no layer matched.

// PATTERN: test-case ORDERING matters — current_os is a persistent global.
//   OS_UNSURE cases run FIRST (boot state, before any set_os). Then set_os to
//   the target OS for subsequent blocks. Each block resets flags before its
//   measured dispatch.

// PATTERN: disable-before-scan means the PREVIOUS command's on_disable fires at
//   the START of each dispatch. To assert cleanly, reset_flags() immediately
//   before the dispatch you measure, then assert the flags for THAT dispatch
//   (e.g. os_cmd_en==1 unambiguously means the OS cmd matched this message,
//   regardless of what the prev message's disable did).

// ANTI-PATTERN: do NOT remove the `static` from g_active_layer to "expose" it —
//   add the accessor function instead. Removing static changes linkage and could
//   mask bugs (or collide if another TU ever defines g_active_layer).

// ANTI-PATTERN: do NOT assert on the stderr "[stub] layer_on(...)" traces — they
//   are human-readable diagnostics, not a stable API. Use stub_get_active_layer().

// ANTI-PATTERN: do NOT try to test (vi) by removing the DEFINE_*_OS macros — a
//   single TU has one fixed set of maps. Demonstrate (vi) by switching to an OS
//   (OS_WINDOWS) with no overrides defined; the fully-default-only case is
//   test_notifier_dispatch.c (separate binary, 11/11).

// ANTI-PATTERN: do NOT call detected_host_os() or any os_detection function —
//   the module is push-only (invariant 17); the test drives current_os solely
//   via notifier_set_os (mirroring the keymap's process_detected_host_os_kb).

// ANTI-PATTERN: do NOT add -Wall/-Wextra to the build command — §11.1 uses none.
//   (If you DO, expect -Wmissing-field-initializers from map rows unless every row
//   has an explicit trailing `, false` — which the code above already does.)
```

### Integration Points

```yaml
NEW FILE:
  - test_notifier_os.c (repo root). Compiled by the §11.1 one-shot command and,
    later, by run_notifier_stub_tests.sh (extended in P1.M2.T2.S1).
STUB HARNESS (test-only):
  - qmk_stubs/qmk_stubs.c: APPEND stub_get_active_layer() (1 line + Mode A comment).
    Enhances the test harness, NOT production code.
CONSUMES (LANDED, unchanged):
  - notifier.c: process_full_message (line 334), notifier_set_os (line 459), the
    per-OS weak accessors + select_*_map_os (S1 block).
  - notifier.h: DEFINE_*_OS macros, notifier_set_os decl.
  - qmk_stubs/os_detection.h: os_variant_t enum.
BUILD:
  - Exact §11.1: gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
        notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99
RUNNER (downstream, P1.M2.T2.S1):
  - run_notifier_stub_tests.sh will be extended to build+run test_notifier_os and
    assert 0 FAIL for the §11.2D gate. This task does NOT touch the runner.
CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module host test).
```

## Validation Loop

> Toolchain: gcc (C project — no ruff/mypy/pytest). All commands were
> **executed during research** against the current post-S2 `notifier.c` (with the
> accessor appended to a /tmp copy of qmk_stubs.c) and **PASSED 30/30, 0 FAIL**.
> The official test is a cleaned-up, Mode-A-documented version of that validated
> test — behavior-identical.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Build test_notifier_os with the EXACT §11.1 command (one-shot, no -Wall).
gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99 \
    -o test_notifier_os
# Expected: exit 0, no errors. (If the accessor wasn't added to qmk_stubs.c,
# you get an 'undefined reference to stub_get_active_layer' LINK error — fix Task 1.)

# 1b. (Optional, stricter) -Wall -Wextra build — should be clean if every map row
#     has an explicit trailing `, false` (the code above does).
gcc -Wall -Wextra -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99 \
    -o /tmp/test_notifier_os_strict 2>&1 | grep 'warning:' || echo "0 warnings"
# Expected: "0 warnings" (or no output). Any -Wmissing-field-initializers means a
# map row is missing `, false` — add it.
rm -f /tmp/test_notifier_os_strict
```

### Level 2: Component Tests (THE PRIMARY GATE)

```bash
cd /home/dustin/projects/qmk-notifier

# Run test_notifier_os. MUST report 0 FAIL: and summary failed: 0.
./test_notifier_os 2>/dev/null | tee /tmp/os_run.log
echo "fails=$(grep -c '^FAIL:' /tmp/os_run.log)  (expect 0)"
grep 'Total tests run' /tmp/os_run.log
# Expected: fails=0; "Total tests run: N / passed: N / failed: 0"; exit 0.
./test_notifier_os >/dev/null 2>&1; echo "exit code: $?  (expect 0)"

# Confirm all six §11.2D categories are labeled in the output.
for cat in '(i)' '(ii)' '(iii)' '(iv)' '(v)' '(vi)'; do
  n=$(grep -c "^PASS: $cat" /tmp/os_run.log)
  echo "$cat : $n PASS lines  (expect >=1)"
done
# Expected: each category has >=1 PASS line.
```

### Level 3: Integration Testing (No Regression)

```bash
cd /home/dustin/projects/qmk-notifier

# Backward-compat canary: test_notifier_dispatch MUST still pass 11/11, 0 FAIL.
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o /tmp/test_notifier_dispatch
/tmp/test_notifier_dispatch 2>/dev/null | tail -1
echo "dispatch fails=$(/tmp/test_notifier_dispatch 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
# Expected: "Total tests run: 11 / passed: 11 / failed: 0"; fails=0.
# (The stub_get_active_layer accessor is additive — it does not change layer_on/off
#  behavior, so test_notifier_dispatch is unaffected.)

# Diff hygiene: ONLY test_notifier_os.c (new) + qmk_stubs/qmk_stubs.c (modified)
# changed, plus your PRP/research under plan/.
git status --porcelain
# Expected: `?? test_notifier_os.c`, ` M qmk_stubs/qmk_stubs.c`, and
#           `?? plan/002_c243e735980a/P1M2T1S1/`. NOTHING else.
git diff --stat -- qmk_stubs/qmk_stubs.c
# Expected: qmk_stubs.c shows 1-3 insertions (the accessor + comment) at EOF.
rm -f /tmp/test_notifier_dispatch
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Mode-A header maps each block to §11.2D (item-spec §DOCS).
grep -q '11.2D' test_notifier_os.c && echo "✓ header references §11.2D"
for cat in '(i)' '(ii)' '(iii)' '(iv)' '(v)' '(vi)'; do
  grep -q "$cat" test_notifier_os.c && echo "✓ category $cat documented in header" \
    || { echo "MISSING category $cat in header"; exit 1; }
done
grep -qE 'F8\.[456]|F9\.[123]' test_notifier_os.c && echo "✓ F8/F9 sections cited"

# 4b. The accessor is present in qmk_stubs.c and is test-harness-documented.
grep -q 'stub_get_active_layer' qmk_stubs/qmk_stubs.c && echo "✓ accessor present"
grep -qE 'test-harness|NOT production|findings F6' qmk_stubs/qmk_stubs.c && echo "✓ accessor Mode-A documented"

# 4c. Distinguishable callbacks + distinct layer numbers present.
grep -q 'os_cmd_on' test_notifier_os.c && grep -q 'def_cmd_on' test_notifier_os.c && echo "✓ distinguishable callbacks"
grep -q '{ "blender", 11' test_notifier_os.c && grep -q '{ "blender",   22' test_notifier_os.c && echo "✓ distinct layer numbers (OS 11 vs default 22)"

# 4d. The test does NOT call detected_host_os() (push-only — invariant 17).
! grep -q 'detected_host_os' test_notifier_os.c && echo "✓ push-only: no detected_host_os() call"

# 4e. Every map row has an explicit case_sensitive (clean -Wall) — optional check.
awk '/DEFINE_SERIAL_(COMMANDS|LAYERS)(_OS)?\(/{f=1} f&&/\}/{f=0} f' test_notifier_os.c \
  | grep -E '^\s*\{' | grep -cvE ', false \}' | grep -q '^0$' \
  && echo "✓ all map rows explicit ', false'" || echo "(note: some rows omit trailing false — fine under §11.1's flag-less build)"
# Expected: all checks print ✓; no MISSING/FAIL lines.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: exact §11.1 build → exit 0 (link requires the qmk_stubs accessor).
- [ ] Level 1b (optional): `-Wall -Wextra` build → 0 warnings (rows have `, false`).
- [ ] Level 2: `./test_notifier_os` → 0 `FAIL:`; summary `failed: 0`; exit 0.
- [ ] Level 2: all six categories `(i)`–`(vi)` have ≥1 PASS line.
- [ ] Level 3: `test_notifier_dispatch` still 11/11, 0 FAIL (no regression).
- [ ] Level 3: `git status` shows only `test_notifier_os.c` (new) +
      `qmk_stubs/qmk_stubs.c` (modified) + plan/ PRP/research.
- [ ] Level 4: Mode-A header present; accessor documented; distinguishable
      callbacks + distinct layers; no `detected_host_os()` call.

### Feature Validation

- [ ] (i) OS map wins + default skipped, per track (cmd AND layer).
- [ ] (ii) default fallback: OS-absent (vi block), OS-matches-nothing (calculator),
      OS_UNSURE (iTerm/calculator boot cases).
- [ ] (iii) tracks fall back independently (iTerm: layer-from-OS/cmd-none;
      neovide: cmd-from-default/layer-none).
- [ ] (iv) `notifier_set_os(same)` idempotent — no disable, layer unchanged.
- [ ] (v) `notifier_set_os(changed)` clears state + no re-dispatch.
- [ ] (vi) no-override OS (WINDOWS) uses default maps (backward-compat).

### Code Quality Validation

- [ ] Follows the `test_notifier_dispatch.c` framework pattern (g_pass/g_fail,
      PASS:/FAIL:, Total summary, `return g_fail?1:0`).
- [ ] Map design lets few messages cover all six criteria (collision/neovide/iTerm/calculator).
- [ ] Every map row has explicit `, false` (clean under -Wall).
- [ ] No anti-patterns (see below): no static-stripping, no stderr-trace asserts,
      no detected_host_os, no -Wall in the official build.

### Documentation & Deployment

- [ ] Mode-A header maps each block to §11.2D (i)–(vi) and the F8/F9 section.
- [ ] `stub_get_active_layer()` accessor carries a Mode-A "test-harness, not
      production" comment in qmk_stubs.c.
- [ ] `run_notifier_stub_tests.sh` extension is P1.M2.T2.S1 (not this task);
      README multi-OS section is P1.M2.T3.S1 (not this task).

---

## Anti-Patterns to Avoid

- ❌ Don't strip the `static` from `g_active_layer` to expose it — add the
  `stub_get_active_layer()` accessor function (changes linkage safely).
- ❌ Don't assert on the stub's stderr `[stub] layer_on(...)` traces — use the
  accessor (stable API).
- ❌ Don't try to test (vi) by omitting the `DEFINE_*_OS` macros — a single TU has
  one fixed map set. Switch to a no-override OS (OS_WINDOWS) instead; the
  fully-default-only keymap is `test_notifier_dispatch.c` (separate binary).
- ❌ Don't call `detected_host_os()` or any os_detection function — the module is
  push-only (invariant 17); drive `current_os` solely via `notifier_set_os`.
- ❌ Don't add `-Wall`/`-Wextra` to the official build command — §11.1 uses none.
  (Stricter builds are fine for local checking but the gate command is flag-less.)
- ❌ Don't assume `current_os` resets between test cases — it is a persistent
  global. Order cases (OS_UNSURE boot first) and `reset_flags()` before each block.
- ❌ Don't forget that `disable_command()` runs at the START of every dispatch
  (fires the PREV on_disable) — reset flags right before the measured dispatch and
  assert enable-counts (unambiguous) rather than disable-counts across blocks.
- ❌ Don't modify `notifier.c`, `notifier.h`, `pattern_match.*`,
  `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`,
  or `.gitignore`. Only `test_notifier_os.c` (new) + `qmk_stubs/qmk_stubs.c`
  (append accessor) change.
- ❌ Don't touch `layer_on`/`layer_off`/`raw_hid_send` in qmk_stubs.c — append the
  accessor only; `test_notifier_dispatch` depends on their current behavior.

---

## Confidence Score: 10/10

The deliverable is a precise two-file change: (1) a NEW `test_notifier_os.c`
whose exact, Mode-A-documented content is given verbatim above (a cleaned-up
version of the research-validated `test_notifier_os_val.c`, which passed
**30/30, 0 FAIL** against the CURRENT post-S2 `notifier.c` using the EXACT §11.1
build command — verified during this research), and (2) a 1-line
`stub_get_active_layer()` accessor appended to `qmk_stubs/qmk_stubs.c` (the
contract-permitted, findings-F6-sanctioned observation mechanism). All six
§11.2D categories (i–vi) are mapped to explicit labeled test blocks; the map
design (blender collision / neovide default-cmd-only / iTerm OS-layer-only /
calculator default-layer-only / OS_WINDOWS no-override) lets a handful of
messages cover the full F8.4/F8.5/F8.6 + F9.1/F9.2/F9.3 + invariant-19 contract.
Dependencies (S1 provider, S2 consumer, notifier.h macros, os_detection.h enum)
are all LANDED and verified; the backward-compat canary (`test_notifier_dispatch`
11/11) is unaffected by the additive accessor. The build command, the observation
strategy, and the no-regression gate were all empirically confirmed. No external
dependencies are added; scope boundaries with P1.M2.T2.S1 (runner extension) and
P1.M2.T3.S1 (README) are explicit.