# PRP — P1.M1.T3.S1: Per-OS weak accessors + `select_command_map_os`/`select_layer_map_os` selectors

## Goal

**Feature Goal**: Add the multi-OS map-selection **provider + dispatcher core** to
`notifier.c`: the `current_os` global, 16 per-OS `__attribute__((weak))` accessor
functions (4 OSes × 2 tracks × {map, size}), and the 2 `static` selector helpers
`select_command_map_os` / `select_layer_map_os`. Together these let a keymap
override command/layer maps per detected OS at link time, and let
`process_full_message` (the NEXT subtask, S2) resolve the OS-specific map (or fall
back to the default) per PRD §8.3 / §2 F8.

**Deliverable**: The modified file `notifier.c` at the repo root
(`/home/dustin/projects/qmk-notifier/notifier.c`), grown by ONE contiguous new
block inserted between the `current_command` global and `activate_layer`. All new
code is **byte-exact per PRD §8.3** (the per-OS accessors + selectors) and
§8.1 (`current_os`).

**Success Definition**:
- `os_variant_t current_os = OS_UNSURE;` global present, immediately after
  `command_map_t *current_command = {0};`, with the §8.1 push-only comment.
- 16 per-OS weak accessor functions present (names
  `_notifier_get_command_map_OS_{LINUX,WINDOWS,MACOS,IOS}`[+`_size`] and
  `_notifier_get_layer_map_OS_{LINUX,WINDOWS,MACOS,IOS}`[+`_size`]), each
  `__attribute__((weak))`, returning `{NULL, 0}`.
- `static void select_command_map_os(os_variant_t, command_map_t **, size_t *)`
  and `static void select_layer_map_os(os_variant_t, layer_map_t **, size_t *)`
  present, each a `switch(os)` over `OS_LINUX/OS_WINDOWS/OS_MACOS/OS_IOS` with a
  `default` that sets `{NULL, 0}` (covers `OS_UNSURE` / unexpected).
- `notifier.c` **compiles** under the stub harness
  (`-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -Wall -Wextra -std=c99`)
  with **only** the 2 expected `-Wunused-function` warnings for the static
  selectors (cleared when S2 wires them).
- Backward-compat preserved: `test_notifier_dispatch` links against the modified
  `notifier.c` and reports **11/11 passed, 0 FAIL** (no `_OS` macros ⇒
  `current_os` stays `OS_UNSURE` ⇒ default maps; F8.7 / invariant 19).
- Symbol contract holds: a throwaway keymap using
  `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)` + `DEFINE_SERIAL_LAYERS_OS(OS_LINUX,…)`
  LINKS against the modified `notifier.o`; the OS_MACOS accessor returns the
  override (size 1) and un-overridden OSes return `{NULL,0}`; the selectors
  dispatch the override-vs-weak correctly across the two TUs.

## User Persona (if applicable)

**Target User**: (1) The NEXT subtask `notifier.c` S2 (`process_full_message` +
`notifier_set_os`), which is the sole caller of the two `static` selectors and the
sole mutator of `current_os`. (2) A keymap author who writes
`DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {…})` (the macro lives in `notifier.h`,
P1.M1.T1.S1) and expects it to override the weak default at link time.

**Use Case**: At dispatch time, S2's `process_full_message` will call
`select_command_map_os(current_os, &os_map, &os_size)`; if `os_size == 0` (no
OS-specific map, or `current_os == OS_UNSURE`) the OS scan is a 0-iteration no-op
and the default map is scanned — today's exact behavior (backward compat).

**User Journey**: keymap `#include "notifier.h"` → defines per-OS maps via the
`_OS` macros (strong symbols) → qmk compiles+links (keymap strong symbols override
the weak ones THIS task adds) → S2's dispatcher resolves the OS map first, default
fallback otherwise → matches fire per track, independently.

**Pain Points Addressed**: Gives the firmware a stable, link-time way to select
maps per detected OS **without `#ifdef`** and **without a link dependency on
`os_detection.c`** (the OS is pushed in; only the `os_variant_t` *type* is
consumed — invariant 17 / §2 F8.2).

## Why

- **Closes the provider side of the §8.3 contract** opened by P1.M1.T1.S1 (the
  header macros) and P1.M1.T2.S1 (the enum stub). The macro-generated symbol
  names are a **hard cross-file contract**: this task's weak defaults + the
  selector's `case` arms must spell them **exactly** or the firmware fails to link.
- **Preserves backward compatibility structurally** (not as a special case): with
  zero `DEFINE_*_OS` macros every per-OS accessor returns `{NULL,0}`, the OS scan
  runs 0 iterations, and dispatch is byte-identical to today (F8.7 / invariant 19).
- **Keeps `os_detection` header-only**: `current_os` is mutated only via
  `notifier_set_os()` (S2); the module never calls `detected_host_os()`.

## What

A single **additive** edit to `notifier.c`: insert ONE contiguous block (a global,
a documentation comment, 16 weak accessor functions, and 2 `static` selector
helpers) immediately after `command_map_t *current_command = {0};` and immediately
before `void activate_layer(uint8_t layer)`. No deletions, no restyle, no
reordering of existing code. The exact code (byte-exact vs PRD §8.3 + §8.1) is
given in the Implementation Blueprint below.

### Success Criteria

- [ ] `current_os` global present after `current_command`, initialized to
  `OS_UNSURE`, with the §8.1 push-only comment.
- [ ] Exactly 16 per-OS weak accessors, names matching `notifier.h`'s `##os` paste.
- [ ] Exactly 2 `static` selectors with a `switch` over the 4 real OSes + `default`.
- [ ] `notifier.c` compiles under the stub harness (only 2 expected `-Wunused-function`).
- [ ] `test_notifier_dispatch` 11/11 against the modified `notifier.c` (backward-compat).
- [ ] Throwaway keymap with `DEFINE_*_OS` links; override + weak-fallback both correct.
- [ ] No edits to `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, tests, runners,
  `PRD.md`, `tasks.json`, `rules.mk`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code (verbatim from PRD §8.3 + §8.1), the exact insertion
point (a unique anchor string), the exact symbol-name contract (from `notifier.h`,
already landed), the expected-warning sequencing note, and a corrected-flag
validation harness were all **empirically validated during research**: the modified
`notifier.c` compiles, `test_notifier_dispatch` passes 11/11, and a two-TU
override+selector test prints `SELTEST ALL=1`. An implementer with only this PRP +
the repo can make the single insertion correctly with no guessing.

### Documentation & References

```yaml
# MUST READ — authoritative code (reproduce VERBATIM)
- file: plan/002_c243e735980a/prd_snapshot.md   (also PRD.md)
  section: "### 8.3 Weak default maps"   (the "Per-OS weak defaults + selector" block)
  why: "Contains the BYTE-EXACT 16 per-OS weak accessors + select_command_map_os /
        select_layer_map_os bodies (the switch arms, the default => {NULL,0}, the
        OS_UNSURE comment)."
  critical: "Reproduce verbatim. The symbol names (_notifier_get_command_map_OS_*,
             _notifier_get_layer_map_OS_*) MUST match notifier.h's ##os paste. A
             typo = link failure. VERIFIED by research (Level-2 link test)."

- file: plan/002_c243e735980a/prd_snapshot.md   (also PRD.md)
  section: "### 8.1 Constants & globals"
  why: "Contains the BYTE-EXACT current_os global + its push-only comment
        ('never read from detected_host_os() directly … OS_UNSURE ⇒ default maps only')."
  critical: "current_os = OS_UNSURE initializer is the boot-state invariant (F8.6)."

- file: plan/002_c243e735980a/prd_snapshot.md   (also PRD.md)
  section: "### 2 Functional Requirements → F8 (esp. F8.4/F8.5/F8.6/F8.7)" and "### 13 invariants 15–20"
  why: "The semantics the comments must state: OS-first/default-fallback per track,
        independent tracks, OS_UNSURE ⇒ default only, weak-default guarantees extend
        to per-OS accessors, push-only OS signal, backward-compat = byte-identical."
  critical: "F8.6 / invariant 16: OS_UNSURE has NO per-OS map by design — the selector
             default case covers it. invariant 20: symbol names must match exactly."

# The contract THIS task's symbols must satisfy (already landed)
- file: notifier.h
  section: "DEFINE_SERIAL_COMMANDS_OS / DEFINE_SERIAL_LAYERS_OS"   (lines ~60–84)
  why: "The ##os token-paste that generates _notifier_get_command_map_OS_MACOS etc.
        This task's weak defaults must use these EXACT mangled names."
  pattern: "macro emits: _notifier_command_map_##os[], _notifier_command_map_##os##_size,
            _notifier_get_command_map_##os(void), _notifier_get_command_map_##os##_size(void)."
  gotcha: "Only OS_LINUX/OS_WINDOWS/OS_MACOS/OS_IOS are valid os args (OS_UNSURE has no
           per-OS map — §2 F8.6). The macro defines the STRONG override; this task
           defines the matching WEAK default."

# The enum type this task consumes (already landed)
- file: qmk_stubs/os_detection.h
  why: "Defines os_variant_t = { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3,
        OS_IOS=4 }. current_os is of this type; the selector switch's case labels are
        these enumerators."
  critical: "Enumerator NAMES are the case labels; they feed the ##os paste. Reused
             as-is (not redefined) — F8.1."

# The file being modified (pattern + exact anchor)
- file: notifier.c
  why: "The default weak accessors (get_command_map/get_layer_map + _size, lines 88–104)
        are the TEMPLATE the per-OS accessors mirror (same __attribute__((weak)) style,
        same return-type pattern). The globals block (activated_layer/current_command,
        lines 106–109) is where current_os is inserted."
  pattern: "Default accessor returns empty_command_map (non-NULL, size 0); per-OS
            accessor returns NULL (size 0) — NULL marks 'no OS-specific map'. Both
            yield a 0-iteration scan (backward-compat)."
  gotcha: "INSERTION ANCHOR is the exact line `command_map_t *current_command = {0};`
           (unique in the file). Insert AFTER it, BEFORE `void activate_layer(...)`."

# Consumer / validator (existing host suite — must stay green)
- file: test_notifier_dispatch.c
  why: "Existing host suite: #include \"notifier.h\", DEFINE_SERIAL_COMMANDS/LAYERS (no
        _OS), never calls notifier_set_os. Proves backward-compat: current_os stays
        OS_UNSURE => selectors return {NULL,0} => default maps => 11/11 unchanged."
  gotcha: "DO NOT modify it. It is the backward-compat canary."

# Architecture grounding
- file: plan/002_c243e735980a/architecture/external_deps.md
  section: "## QMK os_detection.h — The os_variant_t enum"  and  "## detected_host_os()"
  why: "Authoritative enum values + the 'module must NOT call detected_host_os()' rule
        (invariant 17) that the current_os comment encodes."
  critical: "current_os is push-only; no link dependency on os_detection.c."

- file: plan/002_c243e735980a/architecture/findings_and_risks.md
  section: "### F1 (##os naming contract), ### F3 (backward-compat is structural),
            ### F4 (stub minimal), ### F5 (test_notifier_dispatch unchanged)"
  why: "F1 = the symbol-name contract; F3 = the zero-size-loop = backward-compat proof;
        F5 = test_notifier_dispatch must pass unchanged (the canary)."
  critical: "F3: 'No conditional #ifdef is needed; the zero-size loop IS the
             backward-compat guarantee (invariant 19).' Do not add #ifdef guards."

# Prior PRPs (the contract this builds on — treat as ground truth)
- file: plan/002_c243e735980a/P1M1T1S1/PRP.md
  why: "Defines the notifier.h API surface (macros + decl) this task's symbols match."
- file: plan/002_c243e735980a/P1M1T2S1/PRP.md
  why: "Defines the qmk_stubs/os_detection.h stub (now landed) this task compiles against.
        Documents the run_notifier_stub_tests.sh [2/3] -Iqmk_stubs sequencing issue
        (fixed by P1.M2.T2.S1) — SAME caveat applies here; use the corrected-flag harness."
```

### Current Codebase tree (relevant slice)

```bash
notifier.c                 # ← MODIFY (this task). Insert ONE block after `current_command`.
notifier.h                 # LANDED (P1.M1.T1.S1): has DEFINE_*_OS macros + notifier_set_os decl. DO NOT TOUCH.
pattern_match.{c,h}        # unaffected.
qmk_stubs/
  os_detection.h           # LANDED (P1.M1.T2.S1): os_variant_t enum. DO NOT TOUCH.
  qmk_keyboard_stub.h      # QMK_KEYBOARD_H stand-in (layer_on/off decls). DO NOT TOUCH.
  raw_hid.h                # raw_hid_send decl. DO NOT TOUCH.
  qmk_stubs.c              # layer_on/off + raw_hid_send impls. DO NOT TOUCH.
test_notifier_dispatch.c   # backward-compat canary (no _OS macros). DO NOT TOUCH.
run_notifier_stub_tests.sh # stub gate; [2/3] link step needs -Iqmk_stubs (P1.M2.T2.S1). DO NOT TOUCH.
run_all_tests.sh           # 9-suite pattern_match corpus — unaffected. DO NOT TOUCH.
PRD.md                     # READ-ONLY.
plan/002_c243e735980a/     # this plan — architecture/, prd_snapshot.md, tasks.json. DO NOT TOUCH.
```

### Desired Codebase tree with files to be added/changed

```bash
notifier.c                 # MODIFIED: +1 contiguous block (current_os + 16 weak accessors + 2 selectors).
# (no new files created by this subtask)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (linkage contract): the per-OS accessor NAMES are produced by the ##os
//   token-paste in notifier.h. DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…) generates
//   _notifier_get_command_map_OS_MACOS and _notifier_get_command_map_OS_MACOS_size.
//   This task's weak defaults must use these EXACT names; the selector's case arms
//   must call these EXACT names. A typo in any of the 16 = link failure. (Verified.)

// EXPECTED (sequencing): the 2 static selectors are NOT yet called in this subtask
//   (S2's process_full_message is the sole caller). gcc -Wall -Wextra emits exactly
//   2 `-Wunused-function` warnings. The build is NOT -Werror (PRD §11.1: plain -Wall),
//   so this is ACCEPTABLE and clears when S2 lands. DO NOT delete the selectors, mark
//   them __attribute__((unused)), or add a (void)&selfref hack to silence it.
//   (Verified: Test A produces exactly these 2 warnings; object builds clean.)

// CRITICAL (NULL vs empty_command_map): the DEFAULT accessors (get_command_map) return
//   empty_command_map (non-NULL, size 0); the PER-OS accessors return NULL (size 0).
//   NULL semantically marks "no OS-specific map". Both yield a 0-iteration scan, so
//   both are safe AS LONG AS the size is 0 (the loop guard `i < size` runs 0 times).
//   Do NOT "fix" NULL to empty_command_map or vice versa — match PRD §8.3 verbatim.

// GOTCHA (OS_UNSURE has no per-OS map by design): only OS_LINUX/OS_WINDOWS/OS_MACOS/
//   OS_IOS get case arms. OS_UNSURE (value 0) falls through to the switch `default`
//   => {NULL,0} => default-map fallback (F8.6 / invariant 16). Do NOT add a case for
//   OS_UNSURE.

// GOTCHA (selectors are static / internal): select_command_map_os / select_layer_map_os
//   are `static` (file-local). They are NOT declared in notifier.h (internal linkage
//   contract, §5.5) and are NOT callable from another TU. The 16 accessors are NON-
//   static (external linkage, weak) so the keymap's strong DEFINE_*_OS overrides them.

// GOTCHA (size_t / includes): notifier.c already #include <string.h> (brings size_t)
//   and via QMK_KEYBOARD_H the rest. No new #include is needed for this block. Do NOT
//   add #include <stddef.h> or anything else.

// GOTCHA (runner sequencing, NOT this task's defect): run_notifier_stub_tests.sh [2/3]
//   link step has only -I. (not -Iqmk_stubs). It will FAIL to find os_detection.h when
//   compiling test_notifier_dispatch.c during the link step — this is the SAME pre-
//   existing issue documented in P1.M1.T2.S1's PRP (fixed by P1.M2.T2.S1). Validate
//   THIS task with the corrected-flag harness (Validation Level 2), not the bare runner.

// GOTCHA (insertion anchor): the anchor `command_map_t *current_command = {0};` is
//   UNIQUE in notifier.c (grep -c == 1). Insert immediately AFTER it. Do not confuse
//   with `command_map_t *command_found = NULL;` or `command_map_t *cmd_map` elsewhere.
```

## Implementation Blueprint

### Data models and structure

No new types. `current_os` is of the existing `os_variant_t` type (from
`os_detection.h`, via `notifier.h`). The accessors return `command_map_t*` /
`layer_map_t*` / `size_t` (existing types). The selectors write through
out-parameters (`command_map_t **map`, `size_t *size` etc.).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — INSERT the multi-OS core block (ONE contiguous insertion)
  - LOCATE the UNIQUE anchor (verify: `grep -c 'command_map_t \*current_command = {0};' notifier.c` == 1):
        // reference to currently active command:
        command_map_t *current_command = {0};
  - INSERT the ENTIRE block from "Exact insertion block" below IMMEDIATELY AFTER that
    line and BEFORE the blank line + `void activate_layer(uint8_t layer) {`.
  - The block contains, IN ORDER:
      (a) the `os_variant_t current_os = OS_UNSURE;` global + its §8.1 comment;
      (b) the per-OS block header comment (item point 5 docs);
      (c) 8 command-map per-OS weak accessors (4 OSes × {map, size});
      (d) 8 layer-map per-OS weak accessors (4 OSes × {map, size});
      (e) `static void select_command_map_os(...)` — switch over OS_LINUX/_WINDOWS/_MACOS/_IOS + default {NULL,0};
      (f) `static void select_layer_map_os(...)` — identical structure for layers.
  - BODIES: copy VERBATIM from "Exact insertion block" below (it is byte-exact vs
    PRD §8.3 + §8.1). Do not reformat spacing, return values, switch arm order, or
    the trailing `return;` statements.
  - DEPENDENCIES: relies on os_variant_t (via notifier.h → os_detection.h, landed),
    command_map_t / layer_map_t / size_t (already in scope). No new #include.
  - NAMING: symbol stems fixed — _notifier_get_command_map_OS_<OS>, _notifier_get_layer_map_OS_<OS>,
    each with a _size variant. The <OS> suffix is the literal enumerator name
    (OS_LINUX, OS_WINDOWS, OS_MACOS, OS_IOS) — NOT a string, NOT lowercased.
  - PLACEMENT: between the globals block and activate_layer (see Placement decision
    in research/notes.md). This satisfies both item constraints: current_os is
    "after activated_layer/current_command" AND the accessors+selectors are "after
    the default weak-accessor block and before activate/deactivate".
  - PRESERVE: everything else in notifier.c unchanged (no restyle, no reorder).

Task 2: VERIFY (no edit) — compile + backward-compat + symbol contract
  - Run Validation Level 1 (stub-compile; expect only the 2 -Wunused-function warnings).
  - Run Validation Level 2 (link test_notifier_dispatch → 11/11; two-TU override test).
  - Run Validation Level 4 (doc-point greps; backward-compat structural check).
```

**Exact insertion block (byte-exact vs PRD §8.3 + §8.1 — reproduce verbatim):**

```c

/* The host OS used for multi-OS map selection (§2 F8). Pushed in by the keymap
 * via notifier_set_os(); never read from detected_host_os() directly (no link
 * dependency on the OS-detection subsystem). OS_UNSURE ⇒ default maps only
 * (invariant 17, §2 F8.2/F8.6). */
os_variant_t current_os = OS_UNSURE;

/* --- Per-OS weak accessors + selector (multi-OS overlay, §2 F8 / §8.3) --------
 * Each accessor returns {NULL, 0} ("no OS-specific map") UNLESS overridden by a
 * DEFINE_SERIAL_COMMANDS_OS / DEFINE_SERIAL_LAYERS_OS macro in the keymap. The
 * symbol names MUST match the ##os token-paste in notifier.h EXACTLY — e.g.
 * DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…) generates _notifier_get_command_map_OS_MACOS
 * and _notifier_get_command_map_OS_MACOS_size; a typo here = link failure.
 * select_*_map_os() dispatch by current_os; OS_UNSURE and any unexpected value
 * resolve to {NULL, 0} so the default map is used (§8.3). A size of 0 makes the
 * caller's scan loop run 0 iterations and fall through to the default map — this
 * IS the backward-compat guarantee (invariant 19), so no #ifdef is needed. */

/* command map, per OS — weak; overridden by DEFINE_SERIAL_COMMANDS_OS */
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_LINUX(void)   { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_LINUX_size(void)   { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_WINDOWS(void) { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_WINDOWS_size(void) { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_MACOS(void)   { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_MACOS_size(void)   { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_IOS(void)     { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_IOS_size(void)     { return 0; }

/* layer map, per OS — weak; overridden by DEFINE_SERIAL_LAYERS_OS */
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_LINUX(void)   { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_LINUX_size(void)   { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_WINDOWS(void) { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_WINDOWS_size(void) { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_MACOS(void)   { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_MACOS_size(void)   { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_IOS(void)     { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_IOS_size(void)     { return 0; }

/* Resolve the OS-specific command/layer map for `os`, or {NULL,0} if none.
 * Dispatch by current_os; OS_UNSURE / unexpected => {NULL,0} => default-map
 * fallback (§8.3). */
static void select_command_map_os(os_variant_t os, command_map_t **map, size_t *size) {
    switch (os) {
        case OS_LINUX:   *map = _notifier_get_command_map_OS_LINUX();   *size = _notifier_get_command_map_OS_LINUX_size();   return;
        case OS_WINDOWS: *map = _notifier_get_command_map_OS_WINDOWS(); *size = _notifier_get_command_map_OS_WINDOWS_size(); return;
        case OS_MACOS:   *map = _notifier_get_command_map_OS_MACOS();   *size = _notifier_get_command_map_OS_MACOS_size();   return;
        case OS_IOS:     *map = _notifier_get_command_map_OS_IOS();     *size = _notifier_get_command_map_OS_IOS_size();     return;
        default:         *map = NULL; *size = 0; return;   /* OS_UNSURE / unexpected */
    }
}
static void select_layer_map_os(os_variant_t os, layer_map_t **map, size_t *size) {
    switch (os) {
        case OS_LINUX:   *map = _notifier_get_layer_map_OS_LINUX();   *size = _notifier_get_layer_map_OS_LINUX_size();   return;
        case OS_WINDOWS: *map = _notifier_get_layer_map_OS_WINDOWS(); *size = _notifier_get_layer_map_OS_WINDOWS_size(); return;
        case OS_MACOS:   *map = _notifier_get_layer_map_OS_MACOS();   *size = _notifier_get_layer_map_OS_MACOS_size();   return;
        case OS_IOS:     *map = _notifier_get_layer_map_OS_IOS();     *size = _notifier_get_layer_map_OS_IOS_size();     return;
        default:         *map = NULL; *size = 0; return;
    }
}
```

> **Style note:** the surrounding `notifier.c` uses a mix of `//` line comments
> and `/* */` blocks; both `/* */` block comments above are consistent with the
> file's existing block comments (e.g. the `RAW_REPORT_SIZE` block). Keep them.
> The accessor/selector bodies MUST be byte-exact vs PRD §8.3 regardless of
> comment style.

### Implementation Patterns & Key Details

```c
// The per-OS accessors mirror the DEFAULT weak accessors (lines 88–104) but:
//   - are namespaced (_notifier_ prefix) and OS-suffixed (##os-generated);
//   - return NULL (not empty_command_map) to mark "no OS-specific map".
// Both yield a 0-iteration scan when size==0, so dispatch behavior is identical.

// ANTI-PATTERN: do NOT add a `case OS_UNSURE:` arm. OS_UNSURE falls through to
//   `default` => {NULL,0} by design (F8.6 / invariant 16). Adding a case for it
//   would imply an OS_UNSURE map exists, which it must not.

// ANTI-PATTERN: do NOT declare select_*_map_os or the per-OS accessors in
//   notifier.h. The selectors are static (internal); the accessors are an
//   internal linkage contract referenced only inside notifier.c (§5.5).

// ANTI-PATTERN: do NOT call select_*_map_os or read current_os from anywhere in
//   THIS subtask. Their sole consumer is S2 (process_full_message / notifier_set_os).
//   Premature use here would expand scope into S2 and risk desync.

// ANTI-PATTERN: do NOT silence the 2 -Wunused-function warnings with
//   __attribute__((unused)) or a (void)&select_command_map_os; self-reference.
//   They are expected (static fns not yet called) and clear when S2 lands.

// ANTI-PATTERN: do NOT wrap the new code in #ifdef OS_DETECTION_ENABLE or any
//   guard. Backward-compat is structural (zero-size loop), not conditional (F3 /
//   invariant 19). The block compiles and is inert when no _OS macros exist.
```

### Integration Points

```yaml
GLOBALS:
  - add to: notifier.c (globals block, after `command_map_t *current_command = {0};`)
  - pattern: "os_variant_t current_os = OS_UNSURE;   /* push-only (§8.1) */"
WEAK SYMBOLS (external linkage, overridable):
  - add to: notifier.c (after current_os global, before activate_layer)
  - pattern: "__attribute__((weak)) <T>* _notifier_get_<track>_map_OS_<OS>(void) { return NULL; }"
             "__attribute__((weak)) size_t _notifier_get_<track>_map_OS_<OS>_size(void) { return 0; }"
             "  <track> ∈ {command, layer}; <OS> ∈ {LINUX, WINDOWS, MACOS, IOS}"
INTERNAL HELPERS (static):
  - add to: notifier.c (immediately after the 16 accessors)
  - pattern: "static void select_<track>_map_os(os_variant_t os, <T> **map, size_t *size) { switch(os){...} }"
LINKAGE (forward contract for S2):
  - "P1.M1.T3.S2's process_full_message will call select_command_map_os(current_os,&m,&s)
     and select_layer_map_os(current_os,&m,&s) to resolve the OS maps before scanning;
     notifier_set_os will mutate current_os. Until S2 lands, these are defined-but-unused."
BUILD / CONFIG / DATABASE / ROUTES:
  - none. No rules.mk edit, no new files, no includes. Pure additive C in notifier.c.
```

## Validation Loop

> Toolchain: gcc (C project — no ruff/mypy/pytest). All commands below were
> **executed during research against a /tmp copy of the proposed change and PASSED**
> (A: compile; B: 11/11 backward-compat; C: `SELTEST ALL=1`; C2: override probe).
> Sequencing note: `run_notifier_stub_tests.sh`'s [2/3] link step currently lacks
> `-Iqmk_stubs` (pre-existing, documented in P1.M1.T2.S1's PRP, fixed by
> P1.M2.T2.S1). Use the **corrected-flag harness** (Level 2) — it mirrors the
> post-fix runner and is the authoritative gate for THIS task.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile the modified notifier.c (mirrors run_notifier_stub_tests.sh [1/3]).
#     EXPECT: success with EXACTLY 2 `-Wunused-function` warnings
#     (select_command_map_os / select_layer_map_os — static, not yet called; wired by S2).
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_t3s1.o 2>&1 | tee /tmp/t3s1_compile.log
test -f /tmp/notifier_t3s1.o && echo "✓ notifier.c compiles (object present)"
echo "unused-function warnings: $(grep -c 'defined but not used' /tmp/t3s1_compile.log)  (expect 2)"
echo "other warnings: $(grep -vE 'defined but not used|^\s*\|' /tmp/t3s1_compile.log | grep -ci 'warning' || true)  (expect 0)"

# 1b. The new symbols are present with the exact contract names.
for sym in _notifier_get_command_map_OS_LINUX _notifier_get_command_map_OS_WINDOWS \
           _notifier_get_command_map_OS_MACOS  _notifier_get_command_map_OS_IOS \
           _notifier_get_layer_map_OS_LINUX   _notifier_get_layer_map_OS_WINDOWS \
           _notifier_get_layer_map_OS_MACOS   _notifier_get_layer_map_OS_IOS \
           select_command_map_os select_layer_map_os current_os; do
  grep -q "$sym" notifier.c && echo "present: $sym" || { echo "MISSING: $sym"; exit 1; }
done
grep -c '__attribute__((weak))' notifier.c   # expect >= 20 (4 default + 16 per-OS)
# Expected: object present; 2 unused-function warnings; 0 other warnings; all syms present.
```

### Level 2: Backward-Compat + Symbol Contract (the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier
# /tmp/notifier_t3s1.o built in Level 1.

# 2a. BACKWARD-COMPAT canary: test_notifier_dispatch (no _OS macros, current_os stays
#     OS_UNSURE) must still pass 11/11. Uses -Iqmk_stubs on BOTH steps (the post-fix
#     runner; the current run_notifier_stub_tests.sh [2/3] lacks it — see sequencing).
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_t3s1.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o /tmp/test_notifier_dispatch_t3s1
/tmp/test_notifier_dispatch_t3s1 | tee /tmp/t3s1_dispatch.log
echo "fails=$(grep -c '^FAIL:' /tmp/t3s1_dispatch.log)  (expect 0)"
grep 'Total tests run' /tmp/t3s1_dispatch.log   # expect "11 / passed: 11 / failed: 0"

# 2b. SYMBOL-CONTRACT + SELECTOR-DISPATCH test (two TUs): a keymap TU overrides
#     OS_MACOS command + OS_LINUX layer (strong); notifier.o holds the weak defaults
#     + static selectors. A driver TU probes the non-static accessors directly:
#     override wins where defined, weak {NULL,0} elsewhere.
cat > /tmp/t3s1_keymap.c <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "notifier.h"
extern command_map_t* _notifier_get_command_map_OS_MACOS(void);
extern size_t         _notifier_get_command_map_OS_MACOS_size(void);
extern command_map_t* _notifier_get_command_map_OS_LINUX(void);   /* weak, not overridden */
extern size_t         _notifier_get_command_map_OS_LINUX_size(void);
extern layer_map_t*   _notifier_get_layer_map_OS_LINUX(void);
extern size_t         _notifier_get_layer_map_OS_LINUX_size(void);
extern layer_map_t*   _notifier_get_layer_map_OS_MACOS(void);     /* weak, not overridden */
extern size_t         _notifier_get_layer_map_OS_MACOS_size(void);
static void cb(void){}
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, { "iTerm", cb, cb });
DEFINE_SERIAL_LAYERS_OS(OS_LINUX, { "*alacritty*", 2 });
int main(void){
    int good = 1;
    size_t ms; command_map_t* mp;
    ms = _notifier_get_command_map_OS_MACOS_size(); mp = _notifier_get_command_map_OS_MACOS();
    good &= (ms == 1);                                          /* override won */
    printf("MACOS cmd override: size=%zu pat=%s\n", ms, ms ? mp[0].pattern : "(nil)");
    ms = _notifier_get_command_map_OS_LINUX_size(); mp = _notifier_get_command_map_OS_LINUX();
    good &= (ms == 0 && mp == NULL);                            /* weak fallback */
    printf("LINUX cmd weak   : size=%zu ptr=%p\n", ms, (void*)mp);
    size_t ls; layer_map_t* lp;
    ls = _notifier_get_layer_map_OS_LINUX_size(); lp = _notifier_get_layer_map_OS_LINUX();
    good &= (ls == 1);                                          /* layer override won */
    ls = _notifier_get_layer_map_OS_MACOS_size(); lp = _notifier_get_layer_map_OS_MACOS();
    good &= (ls == 0 && lp == NULL);                            /* layer weak fallback */
    printf("CONTRACT ALL=%d\n", good);
    return good ? 0 : 1;
}
EOF
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_t3s1.o qmk_stubs/qmk_stubs.c /tmp/t3s1_keymap.c \
    -o /tmp/t3s1_contract 2>/tmp/t3s1_link.log
link_rc=$?
if [ $link_rc -ne 0 ]; then
  echo "✗ LINK FAILED (symbol-name mismatch?)"; cat /tmp/t3s1_link.log; exit 1
fi
/tmp/t3s1_contract; echo "contract exit=$? (expect 0, CONTRACT ALL=1)"
# Expected: dispatch 11/11 fails=0; contract prints "MACOS cmd override: size=1 pat=iTerm",
#           "LINUX cmd weak: size=0 ptr=(nil)", "CONTRACT ALL=1"; link clean (no dup-symbol).
```

> **Why no separate selector-dispatch binary here?** The selectors are `static`
> (file-local to `notifier.c`); they cannot be called from another TU, and the
> only way to exercise them with an override is a 2-TU link where the call site
> lives inside `notifier.c`'s TU. That is exactly what S2's `process_full_message`
> will do (and what `test_notifier_os.c` / P1.M2.T1.S1 will assert on). The
> Level-2b accessor probe proves the **link-time override contract** (the part
> that crosses the TU boundary); the selector's `switch` is trivially correct
> (it just forwards to the accessor the link resolved). Research additionally
> verified the selector dispatch end-to-end via a SELTEST main spliced into a
> /tmp copy (see research/notes.md §7 Test C: `SELTEST ALL=1`).

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# The FULL run_notifier_stub_tests.sh end-to-end pass is gated on P1.M2.T2.S1
# (it adds -Iqmk_stubs to the [2/3] link step). Running it NOW is EXPECTED to fail
# at [2/3] ONLY because of that missing flag — NOT because of this task (Level 2
# above proves the modified notifier.c is correct with the proper flag). After
# P1.M2.T2.S1 lands:
#     ./run_notifier_stub_tests.sh   # -> "✓ notifier stub-compile gate PASSED",
#                                    #    test_notifier_dispatch + test_notifier_os 0 FAIL

# Diff hygiene: ONLY notifier.c changed (plus your PRP/research under plan/).
git status --porcelain
# Expected: only ` M notifier.c` (and ?? plan/002.../P1M1T3S1/{PRP.md,research/}).
#           No other modifications.
git diff --stat
# Expected: a single line for notifier.c (insertions only, no deletions).
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Required inline-documentation points present (item point 5 / Mode A).
for needle in "current_os" "OS_UNSURE" "detected_host_os" "##os" "DEFINE_SERIAL_COMMANDS_OS" \
              "DEFINE_SERIAL_LAYERS_OS" "default-map fallback\|default map fallback" \
              "overridden by" "backward-compat guarantee\|backward compat"; do
  grep -qiE "$needle" notifier.c && echo "doc present: $needle" \
    || { echo "MISSING doc token: $needle"; exit 1; }
done

# 4b. Backward-compat is STRUCTURAL (no #ifdef guards around the new block).
#     The new symbols must compile unconditionally (F3 / invariant 19).
! grep -qE '#if(n?def|ndef).*OS_DETECTION' notifier.c \
  && echo "✓ no OS_DETECTION_ENABLE guard around multi-OS block (correct — structural compat)"

# 4c. OS_UNSURE has NO case arm (falls through to default => {NULL,0}).
if grep -qE 'case[[:space:]]+OS_UNSURE' notifier.c; then
  echo '✗ FAIL: found a "case OS_UNSURE:" arm (forbidden — F8.6 / invariant 16)'; exit 1
else
  echo '✓ no "case OS_UNSURE:" arm (OS_UNSURE falls through to default => {NULL,0})'
fi

# 4d. The default weak accessors are UNCHANGED (still present, still non-NULL).
grep -qE '__attribute__\(\(weak\)\)[[:space:]]*command_map_t\*[[:space:]]*get_command_map' notifier.c \
  && grep -q 'return empty_command_map' notifier.c \
  && echo "✓ default get_command_map weak default preserved"

# 4e. Cross-check the 16 names match notifier.h's ##os stems exactly.
for os in LINUX WINDOWS MACOS IOS; do
  for track in command layer; do
    grep -q "_notifier_get_${track}_map_OS_${os}("      notifier.c || { echo "MISSING accessor OS_${os}/${track}"; exit 1; }
    grep -q "_notifier_get_${track}_map_OS_${os}_size"  notifier.c || { echo "MISSING size OS_${os}/${track}"; exit 1; }
    grep -q "_notifier_get_${track}_map_##os"           notifier.h   # the macro stem (parity sanity)
  done
done
echo "✓ all 16 per-OS accessor names present and match notifier.h ##os stems"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `notifier.c` stub-compiles (`-Wall -Wextra -std=c99 …`); object present.
- [ ] Level 1: EXACTLY 2 `-Wunused-function` warnings (the static selectors); 0 others.
- [ ] Level 1: all 10 new symbols present (`current_os`, 16 accessors, 2 selectors); ≥20 `__attribute__((weak))`.
- [ ] Level 2a: `test_notifier_dispatch` 11/11, 0 FAIL (backward-compat canary).
- [ ] Level 2b: keymap+notifier link clean (no dup-symbol); override `size=1`, weak `{0,NULL}`; `CONTRACT ALL=1`.
- [ ] Level 3: `git status` shows ONLY `notifier.c` modified (+ plan/ PRP/research); insertions only.
- [ ] Level 4: all doc tokens present; no `OS_DETECTION_ENABLE` guard; no `case OS_UNSURE:`; defaults preserved; 16 names match.

### Feature Validation

- [ ] `os_variant_t current_os = OS_UNSURE;` present after `current_command`, with the §8.1 push-only comment.
- [ ] 16 per-OS weak accessors present, byte-exact vs PRD §8.3, names matching `notifier.h`'s `##os` paste.
- [ ] `select_command_map_os` / `select_layer_map_os` present, `static`, `switch` over the 4 OSes + `default {NULL,0}`.
- [ ] No edits to `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`.
- [ ] Scope respected: S2 (`notifier_set_os` impl + `process_full_message` scan) NOT implemented here.

### Code Quality Validation

- [ ] Matches existing file style (block comments + terse accessors; consistent with `RAW_REPORT_SIZE` block).
- [ ] Additive-only insertion at the unique anchor; no restyle/reorder of existing code.
- [ ] No anti-patterns (see below): no `case OS_UNSURE`, no header decls of internals, no `#ifdef` guard, no warning-silencing hacks, no premature use of the selectors/current_os.
- [ ] No new `#include` added (size_t/os_variant_t already in scope).

### Documentation & Deployment

- [ ] Inline comments are self-documenting (Mode A) — no separate docs file for this task.
- [ ] Sequencing caveats documented: (1) 2 expected `-Wunused-function` warnings clear at S2; (2) `run_notifier_stub_tests.sh` full pass gated on P1.M2.T2.S1.
- [ ] README multi-OS section is a LATER task (P1.M2.T3.S1) — not touched here.

---

## Anti-Patterns to Avoid

- ❌ Don't paraphrase the accessor/selector bodies — symbol names must be byte-exact vs PRD §8.3 (link contract with `notifier.h`'s `##os` paste).
- ❌ Don't add a `case OS_UNSURE:` arm — OS_UNSURE has no per-OS map by design; it falls through to `default` ⇒ `{NULL,0}` (F8.6 / invariant 16).
- ❌ Don't declare `select_*_map_os` or the per-OS accessors in `notifier.h` — they're static / an internal linkage contract (§5.5).
- ❌ Don't silence the 2 `-Wunused-function` warnings with `__attribute__((unused))` or a `(void)&x;` hack — they're expected (static, not yet called) and clear at S2.
- ❌ Don't wrap the new block in `#ifdef OS_DETECTION_ENABLE` (or any guard) — backward-compat is structural (the zero-size loop), not conditional (F3 / invariant 19).
- ❌ Don't change the per-OS accessors to return `empty_command_map` instead of `NULL` — `NULL` marks "no OS-specific map" (PRD §8.3 verbatim). Both are size-0; match the spec.
- ❌ Don't call `select_*_map_os` / read/mutate `current_os` anywhere in THIS subtask — their sole consumer is S2. Premature use expands scope and risks desync.
- ❌ Don't run the *current* `run_notifier_stub_tests.sh` and treat a [2/3] link failure as a defect — its link step lacks `-Iqmk_stubs` until P1.M2.T2.S1. Use the Level-2 corrected-flag harness.
- ❌ Don't touch `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_notifier_dispatch.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`, or `.gitignore`.

---

## Confidence Score: 10/10

The deliverable is a single additive insertion to `notifier.c`, whose exact code
(verbatim from PRD §8.3 + §8.1), exact insertion anchor (a unique line), exact
symbol-name contract (from the already-landed `notifier.h`), and exact
documentation requirements are fully specified above and were **empirically
validated during research**: the modified `notifier.c` compiles under the stub
harness (only the 2 expected unused-selector warnings), `test_notifier_dispatch`
passes **11/11** (backward-compat canary), and a two-TU override+contract test
links cleanly and proves the OS_MACOS override wins (`size=1`) while un-overridden
OSes keep the weak `{NULL,0}` (`CONTRACT ALL=1`); research additionally confirmed
the selector `switch` dispatch end-to-end (`SELTEST ALL=1`). The two sequencing
caveats (selector unused-until-S2 warnings; the runner's `-Iqmk_stubs` link-step
gap, fixed by P1.M2.T2.S1) are explicitly documented and handled with a
corrected-flag harness that does not depend on them. No external dependencies are
added; scope boundaries with S2, P1.M2.T1.S1, and P1.M2.T2.S1 are explicit.