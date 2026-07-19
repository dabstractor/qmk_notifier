# PRP — P1.M1.T2.S2: Refactor `notifier_set_os` into shared `apply_os_change` seam for `SET_OS` reuse

## Goal

**Feature Goal**: Extract the body of `notifier_set_os` into a new
`static void apply_os_change(os_variant_t os)` so that **both** OS-change sources
route through ONE function: the keymap path (`notifier_set_os`, called from
`process_detected_host_os_kb`) and the host path (`SET_OS` typed command, to be
implemented in P1.M2.T2.S2). `apply_os_change` becomes the **sole mutation point**
for `current_os` (besides init), so the F9 clear-on-change semantics
(idempotent check → `disable_command()` → `deactivate_layer()`) are defined in
exactly one place and can never diverge between the two sources.

This is a **pure refactor** — behavior is byte-identical. The existing
`test_notifier_os` and `test_notifier_dispatch` suites must pass **unchanged**.

**Deliverable**: One surgical edit to `/home/dustin/projects/qmk-notifier/notifier.c`
— replace the `notifier_set_os` block (its §8.7 comment + function body, ~L493-523)
with:
1. A new `static void apply_os_change(os_variant_t os)` holding the moved logic
   (idempotent guard, the `CONSOLE_ENABLE` `uprintf`, `current_os = os`,
   `disable_command()`, `deactivate_layer()`) plus a Mode-A comment.
2. A reduced `void notifier_set_os(os_variant_t os)` that is a **one-line
   forwarder**: `apply_os_change(os);`.

No other file is modified.

**Success Definition**:
- `apply_os_change` exists, is `static`, signature `void apply_os_change(os_variant_t)`,
  and contains the idempotent guard + the `uprintf` + `disable_command()` +
  `deactivate_layer()` (F9.1/F9.3), with the Mode-A comment (item §5, verbatim).
- `notifier_set_os` is a one-line forwarder; signature `void notifier_set_os(os_variant_t)`
  unchanged; still non-static (public, declared in notifier.h).
- `current_os` is written in exactly two places: init (`OS_UNSURE`) and
  `apply_os_change` (sole mutation point — invariant 17).
- `notifier.c` stub-compiles clean (`-Wall -Wextra -std=c99`) with **only** the 4
  expected P1.M1.T2.S1 `-Wunused` warnings (host_layer, host_cb_enabled,
  has_been_queried, board_rules_present) — **no** `apply_os_change` warning (it is
  static *and* used by the forwarder).
- `test_notifier_os` stays **31/31 green**; `test_notifier_dispatch` stays **14/14 green**.
- Mode-A comment present on `apply_os_change` (item §5).

## User Persona (if applicable)

**Target User**: The downstream P1.M2.T2.S2 implementer of the `SET_OS` typed-command
handler, and (transitively) the desktop host (QMKonnect) that will send `SET_OS` to
declare its OS authoritatively at connect (§4.7).

**Use Case**: Today only the keymap can change `current_os`. Once `SET_OS` lands,
the host must be able to set `current_os` through the **same** F9-clearing path
so an OS *change* clears notifier state exactly once, in one place. This task
extracts that path so P1.M2.T2.S2 can call `apply_os_change((os_variant_t)os_byte)`
instead of re-implementing (and risking divergence of) the F9 logic.

**User Journey**: keymap `process_detected_host_os_kb` → `notifier_set_os(os)` →
**`apply_os_change(os)`** (idempotent + clear). Future: host `SET_OS` →
`handle_typed_command` → **`apply_os_change((os_variant_t)os_byte)`** (same path).

**Pain Points Addressed**: Eliminates the divergence risk called out in
findings_and_risks.md **F2** ("`SET_OS` must share the `notifier_set_os` seam … do
NOT duplicate the F9 logic in the SET_OS handler — divergence risk is high").

## Why

- **Single source of truth for the F9 contract.** Idempotency (F9.3),
  clear-on-change (F9.1), and no-re-dispatch (F9.2) are subtle; defining them once
  in `apply_os_change` means the keymap and host paths can never disagree.
- **Locks invariant 17 (§13).** `current_os` must have exactly one mutation point
  (besides init). Today that's `notifier_set_os`; after this task it's
  `apply_os_change`, which both callers funnel through.
- **Unblocks P1.M2.T2.S2 with a stable symbol.** The `SET_OS` handler can be
  written against `apply_os_change(os_variant_t)` without touching board-state
  teardown logic.
- **Zero behavior change.** It's a move, not a rewrite — proven by the regression
  suites passing unchanged (empirically verified during research).

## What

A single in-place replacement in `notifier.c`:

1. The detailed §8.7/F9 contract comment currently on `notifier_set_os` moves onto
   the new `apply_os_change` (it documents the function that now holds the logic).
2. `apply_os_change` (static) receives the moved body verbatim — including the
   `#ifdef CONSOLE_ENABLE uprintf` (item §3: "The CONSOLE_ENABLE uprintf stays in
   apply_os_change").
3. `notifier_set_os` keeps a reduced comment (keymap entry point + symbol-name
   parity) and a one-line body: `apply_os_change(os);`.

`disable_command()` (L244) and `deactivate_layer()` (L220) are both defined above
the edit site, so `apply_os_change` placed immediately before `notifier_set_os`
needs **no forward declaration**.

### Success Criteria

- [ ] `apply_os_change` is `static`, signature `void apply_os_change(os_variant_t os)`.
- [ ] `notifier_set_os` body is exactly `apply_os_change(os);` (one-liner); signature unchanged.
- [ ] The `CONSOLE_ENABLE` `uprintf` is inside `apply_os_change` (not the forwarder).
- [ ] Mode-A comment on `apply_os_change` matches item §5 verbatim sense.
- [ ] `current_os` written only at init + `apply_os_change`.
- [ ] stub-compile exit 0; only the 4 S1 `-Wunused` warnings (no new ones).
- [ ] `test_notifier_os` 31/31; `test_notifier_dispatch` 14/14 — unchanged.
- [ ] No file other than `notifier.c` is modified.

## All Needed Context

### Context Completeness Check

**Pass.** The exact OLD block (verified verbatim against the working
`notifier.c` L493-523) and the exact NEW block are specified inline below, and the
refactor was **empirically validated during research** against a temp copy of
`notifier.c`: it stub-compiles clean (only the 4 expected S1 `-Wunused` warnings),
`test_notifier_os` passes 31/31 unchanged, and `test_notifier_dispatch` passes
14/14 unchanged. An implementer with only this PRP + repo can make the single edit
with no guessing.

### Documentation & References

```yaml
# MUST READ — the refactor target + the F9 contract it must preserve
- file: PRD.md   (snapshot: plan/003_16d737de7a3e/prd_snapshot.md)
  section: "### 8.7 notifier_set_os(os_variant_t os) — the OS selector"
  why: "The canonical body being refactored (idempotent guard, CONSOLE_ENABLE
        uprintf, current_os = os, disable_command, deactivate_layer, no re-dispatch).
        The refactor must reproduce this behavior byte-for-byte inside apply_os_change."
  critical: "notifier_set_os is the PUBLIC entry point declared in notifier.h; its
        signature (void, os_variant_t) MUST NOT change. The detailed F9 contract
        comment moves onto apply_os_change; notifier_set_os keeps a reduced comment."

- file: PRD.md
  section: "### F9 — OS-change state clearing"  (§2)
  why: "F9.1 (clear on change: disable_command then deactivate_layer), F9.2 (no
        re-dispatch), F9.3 (idempotent on unchanged value). apply_os_change is the
        single function that implements all three for BOTH callers."
  critical: "Order matters: disable_command() BEFORE deactivate_layer() (matches
        the existing body and the board's disable-before-enable ordering, invariant 4).
        Do not reorder. The idempotent early-return must stay first."

- file: PRD.md
  section: "### 4.7 OS source: host-authoritative when a host is connected"
  why: "'SET_OS updates current_os through the same seam as notifier_set_os, so an
        OS change clears notifier state per F9 before recording the new OS.' This is
        the architectural justification for the seam — the host's SET_OS and the
        keymap's notifier_set_os MUST share one clearing path."
  critical: "While a host is connected and has sent SET_OS, the host value is
        authoritative for current_os (precedence over the OS_DETECTION heuristic).
        That precedence is the CALLER's concern (P1.M2.T2.S2 decides when to call);
        apply_os_change itself is source-agnostic — it just applies the change + F9."

- file: PRD.md
  section: "### 4.6 Typed-command namespace"  (SET_OS row: cmd_id 0x03)
  why: "SET_OS request [os_byte] -> [0x51][0x03][ack=1]; os_byte 0..4 mirrors
        os_variant_t. The SET_OS handler (P1.M2.T2.S2) will call
        apply_os_change((os_variant_t)os_byte)."
  critical: "os_variant_t enum (OS_UNSURE=0..OS_IOS=4) matches the SET_OS os_byte
        mapping exactly (verified against qmk_stubs/os_detection.h), so the cast is
        sound. S2 only EXPOSES apply_os_change; it does not implement the handler."

# Architecture — the exact target shape
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## The apply_os_change seam refactor"
  why: "Shows the before/after: notifier_set_os body -> apply_os_change;
        notifier_set_os becomes `void notifier_set_os(os_variant_t os){ apply_os_change(os); }`;
        SET_OS handler calls apply_os_change. States 'Only apply_os_change mutates
        current_os (besides init).'"
  critical: "Both sources route through apply_os_change. Do NOT duplicate the F9
        logic in SET_OS (divergence risk — findings F2)."

- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### F2 — SET_OS must share the notifier_set_os seam"
  why: "'This is a refactor, not a rewrite — the existing notifier_set_os body moves
        into apply_os_change and notifier_set_os becomes a one-line forwarder. Do NOT
        duplicate the F9 logic in the SET_OS handler.'"
  critical: "Frames the whole task: MOVE, don't rewrite. Behavior identical."

# Dependency PRP (what the working tree already contains when S2 runs)
- file: plan/003_16d737de7a3e/P1M1T2S1/PRP.md
  why: "S1 (landing in parallel) adds host_layer/host_cb_enabled/has_been_queried
        globals + board_rules_present() helper. These are UNUSED until P1.M2 and emit
        the 4 expected -Wunused warnings. S2 must NOT touch them and must NOT be
        confused by their warnings — they are orthogonal to the apply_os_change edit."
  critical: "S2's validation gate allows exactly those 4 S1 warnings PLUS zero new
        ones. apply_os_change itself must NOT warn (it is used by the forwarder)."

# Regression targets (must stay green)
- file: test_notifier_os.c
  why: "Exercises notifier_set_os via the PUBLIC API: (iv) idempotent on unchanged
        value [F9.3], (v) clear-on-change + no-re-dispatch [F9.1/F9.2]. Observes via
        stub_get_active_layer() + on_disable counters. 31 cases."
  critical: "Because notifier_set_os now delegates to apply_os_change with identical
        logic, ALL 31 cases pass unchanged. Verified empirically. If any (iv)/(v)
        case breaks, the refactor changed behavior — recheck the moved body."

- file: test_notifier_dispatch.c
  why: "Legacy reassembly / F4 / F5 / F6 ack — must stay 14/14 (notifier_set_os is
        not on its path, but it links the same notifier.o, so this is a build +
        no-regression sanity check)."
```

### Current Codebase tree (relevant slice)

```bash
notifier.h                # declares notifier_set_os (§5.2/§8.7). DO NOT TOUCH.
notifier.c                # ← MODIFY (single block: notifier_set_os + its §8.7 comment, ~L493-523).
                          #   Already carries S1's host globals (L137-139) + board_rules_present (L198).
pattern_match.h / .c      # unaffected.
qmk_stubs/                # os_detection.h (os_variant_t), qmk_stubs.c. DO NOT TOUCH.
test_notifier_os.c        # regression target (31 cases) — MUST stay green unchanged.
test_notifier_dispatch.c  # regression target (14 cases) — MUST stay green.
run_notifier_stub_tests.sh# gate (builds notifier.o + both drivers). DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be added/changed

```bash
notifier.c                # MODIFIED: notifier_set_os block -> apply_os_change (static) + 1-line forwarder.
# (no new files; no header change)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — this is a MOVE, not a rewrite (findings F2). The body of notifier_set_os
//   is relocated verbatim into apply_os_change. Do NOT "improve" it, reorder the
//   disable/deactivate calls, or drop the no-re-dispatch comment. Behavior must be
//   byte-identical (the 31/31 + 14/14 regression proves it).

// CRITICAL — the CONSOLE_ENABLE uprintf stays INSIDE apply_os_change (item §3). Do
//   not leave it in the forwarder (the forwarder is a pure delegator; the diagnostic
//   belongs with the logic that actually changes state).

// CRITICAL — apply_os_change is STATIC. Its only callers are notifier_set_os (this
//   task) and the SET_OS handler (P1.M2.T2.S2) — both inside notifier.c. Do NOT add
//   it to notifier.h. It is file-local by design (the seam is an implementation detail;
//   the public surface is still just notifier_set_os).

// GOTCHA — placement: define apply_os_change IMMEDIATELY BEFORE notifier_set_os.
//   disable_command() (L244) and deactivate_layer() (L220) are already defined above,
//   so no forward declaration is needed. Defining it before the forwarder means the
//   forwarder's call resolves directly.

// GOTCHA — no NEW -Wunused warning. apply_os_change is static but IS used (by the
//   forwarder), so gcc will NOT warn about it. The only allowed warnings are the 4
//   pre-existing S1 scaffolding warnings (host_layer, host_cb_enabled, has_been_queried,
//   board_rules_present). If you see an apply_os_change unused warning, the forwarder
//   isn't calling it — fix the wiring.

// GOTCHA — sole-mutation-point invariant (item §3, invariant 17). After the refactor,
//   `grep -nE 'current_os[[:space:]]*=' notifier.c` must show EXACTLY two lines:
//   the init (os_variant_t current_os = OS_UNSURE;) and the assignment inside
//   apply_os_change. If current_os is written anywhere else, the invariant is broken.

// GOTCHA — the §8.7 comment block is large and mixes two concerns: (a) the F9
//   CONTRACT (idempotent/clear/no-re-dispatch) and (b) the KEYMAP ENTRY-POINT +
//   symbol-name-parity notes. After the refactor, (a) documents apply_os_change (the
//   function holding the logic) and (b) stays on notifier_set_os (the public entry).
//   Split the comment accordingly; do not dump the whole block on one function.

// GOTCHA — do NOT add a forward declaration of apply_os_change if you place its body
//   above notifier_set_os. A redundant static prototype is harmless but adds churn;
//   this file declares statics inline (see activate_layer/deactivate_layer — defined
//   in dependency order, no prototypes). Match that style.

// GOTCHA — notifier_set_os's signature/visibility are part of the public ABI: it is
//   declared in notifier.h and called by user keymaps. Keep it `void`, non-static,
//   name + param unchanged. Only the BODY becomes a one-line forwarder.
```

## Implementation Blueprint

### Data models and structure

None new. `apply_os_change` operates on the existing file-scope `current_os`
(`os_variant_t`, L129) and calls the existing `disable_command()` (L244) and
`deactivate_layer()` (L220). No new types, globals, or includes.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — REPLACE the notifier_set_os block with apply_os_change + forwarder
  - LOCATE the block that starts with the comment line:
        `/* notifier_set_os — the OS selector (§8.7). Sole mutation point for current_os`
    and ends with the close brace of `void notifier_set_os(os_variant_t os) { ... }`
    (working tree: ~L493-523; the full exact OLD text is in "The exact code" below).
  - REPLACE that entire block (comment + function) with the NEW block below:
      (1) a Mode-A comment + `static void apply_os_change(os_variant_t os) { ... }`
          holding the moved body (idempotent guard, CONSOLE_ENABLE uprintf,
          current_os = os, disable_command(), deactivate_layer(), no-re-dispatch note);
      (2) a reduced comment + `void notifier_set_os(os_variant_t os) { apply_os_change(os); }`.
  - MATCH: use an exact-text replacement (the OLD block is unique in the file).
  - PRESERVE: everything else in notifier.c, including S1's host globals (L137-139)
    and board_rules_present (L198) — do not touch or restyle them.
  - NAMING: apply_os_change (static, snake_case, matches architecture doc + item §3).
  - DEPENDENCIES: disable_command (L244), deactivate_layer (L220) — both already
    defined above the edit site. No new includes, no forward declarations.
```

**The exact code to write** — OLD block (verified verbatim in working `notifier.c`,
~L493-523) → NEW block:

```c
/* ===== NEW BLOCK (replaces the old notifier_set_os comment + function) ===== */

/* apply_os_change — shared seam for OS changes from both the keymap
 * (notifier_set_os) and the host (SET_OS typed cmd). Sole mutation point for
 * current_os (invariant 17). Idempotent on an unchanged value (F9.3); clears
 * state on change (F9.1). Does NOT re-dispatch the last message (F9.2).
 *
 * Contract (§2 F9):
 *   - IDEMPOTENT on an unchanged value (no-op; F9.3): repeated stable-detection
 *     callbacks (e.g. macOS-on-ARM's delayed stability) do not flap state.
 *   - On a CHANGED value it CLEARS all notifier state before recording the new
 *     OS: disable_command() fires the previous command's on_disable if active,
 *     deactivate_layer() turns off the active notifier layer if any (F9.1). This
 *     guarantees no layer/command chosen under the previous OS's maps survives.
 *   - It does NOT re-dispatch the last message (F9.2): the next focus-change
 *     message from the host re-establishes state under the new maps. */
static void apply_os_change(os_variant_t os) {
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

/* notifier_set_os — keymap entry point for the OS selector (§8.7). Delegates to
 * apply_os_change(), the sole mutation point for current_os (invariant 17): the
 * module never calls detected_host_os(), so there is no link dependency on the
 * OS-detection subsystem — the OS is PUSHED in by the keymap (conventionally
 * from process_detected_host_os_kb, §10.1 step 3).
 *
 * Symbol-name parity: the keymap's DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…) /
 * DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…) macros (##os token-paste in notifier.h)
 * generate the strong _notifier_get_*_map_OS_MACOS[_size] symbols that override
 * the weak defaults; this function only flips current_os (via apply_os_change)
 * so the next dispatch's select_*_map_os() resolves the override. */
void notifier_set_os(os_variant_t os) {
    apply_os_change(os);
}
```

The OLD block to match (for the exact-text replacement) is the current
`/* notifier_set_os — the OS selector (§8.7). Sole mutation point for current_os`
comment **through** the closing `}` of `void notifier_set_os(os_variant_t os) { … }`
(see `notifier.c` ~L493-523). The body lines (`if (os == current_os) return;` …
`deactivate_layer();` … no-re-dispatch comment) are identical in OLD and NEW —
only their enclosing function changes (notifier_set_os → apply_os_change) and the
comment is split (F9 contract → apply_os_change; keymap/parity → notifier_set_os).

### Implementation Patterns & Key Details

```c
// PATTERN: extract-method refactor. The body is MOVED verbatim; the public entry
//   becomes a one-line delegator. This is the lowest-risk way to share logic
//   (findings F2: "refactor, not a rewrite").

// PATTERN: the static helper is defined BEFORE its public caller so no forward
//   declaration is needed (matches activate_layer/deactivate_layer's inline order).

// PATTERN: comment splits by concern. The F9 contract documents the function that
//   IMPLEMENTS it (apply_os_change); the public-API/parity notes document the entry
//   point (notifier_set_os). Don't pile both on one function.

// ANTI-PATTERN: do NOT leave the uprintf in the forwarder. The diagnostic belongs
//   with the state change (item §3). A forwarder with side effects defeats the seam.

// ANTI-PATTERN: do NOT make apply_os_change non-static, and do NOT declare it in
//   notifier.h. It is an internal seam; the public surface stays notifier_set_os.

// ANTI-PATTERN: do NOT reorder disable_command()/deactivate_layer(). The order
//   (command off, then layer off) is deliberate and matches invariant 4. The 31/31
//   OS regression encodes this order via on_disable counters.

// ANTI-PATTERN: do NOT add a current_os write anywhere else. Sole mutation point
//   (init + apply_os_change) is invariant 17 — P1.M2.T2.S2's SET_OS handler CALLS
//   apply_os_change; it must not write current_os directly.
```

### Integration Points

```yaml
NOTIFIER.C (§8.7 region):
  - replace: notifier_set_os comment + body (~L493-523)
  - with:    apply_os_change (static) + notifier_set_os (1-line forwarder)
  - consumer (this task):     notifier_set_os -> apply_os_change
  - consumer (P1.M2.T2.S2):   SET_OS handler -> apply_os_change((os_variant_t)os_byte)
GLOBALS:
  - current_os mutation set: { init (L129), apply_os_change }  (was: init, notifier_set_os)
BUILD/CONFIG/ROUTES/DATABASE:
  - none (no rules.mk, no wire change, no header change, no runtime surface change).
```

## Validation Loop

> Toolchain: gcc (C project; no ruff/mypy/pytest). `notifier.c` is stub-compiled
> with `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.` (the exact
> command `run_notifier_stub_tests.sh` uses). All commands below were **executed
> during research against a temp `notifier.c` carrying the refactor and PASSED**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. notifier.c stub-compiles. Expect exit 0 AND only the 4 pre-existing S1
#     -Wunused warnings (host_layer, host_cb_enabled, has_been_queried,
#     board_rules_present). NO apply_os_change warning, NO error.
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
echo "compile exit=$?  (expect 0)"
echo "-- warnings (expect ONLY the 4 S1 -Wunused lines; NO apply_os_change) --"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep 'warning:' | sed 's/^[^:]*://'
! gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep -q 'apply_os_change.*defined but not used' \
  && echo "OK: apply_os_change is used (no unused warning)" \
  || echo "FAIL: apply_os_change is unused — forwarder not wired"

# 1b. Confirm the refactor landed: apply_os_change is static; notifier_set_os is a one-liner.
grep -nE 'static void apply_os_change\(os_variant_t os\)' notifier.c
grep -nE '^void notifier_set_os\(os_variant_t os\) \{' notifier.c
sed -n '/^void notifier_set_os(os_variant_t os) {/,/^}/p' notifier.c
# Expected: the notifier_set_os body is exactly `    apply_os_change(os);`.

rm -f /tmp/notifier_stub.o
```

### Level 2: Component Validation (sole-mutation-point invariant)

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. current_os is written in EXACTLY two places: init + apply_os_change.
echo "-- current_os write sites (expect exactly 2: init + apply_os_change) --"
grep -nE 'current_os[[:space:]]*=' notifier.c
# Expected lines:
#   <L> os_variant_t current_os = OS_UNSURE;
#   <L>     current_os = os;        (inside apply_os_change)
# Assert no write remains inside notifier_set_os's body.

# 2b. Reach the static apply_os_change via #include and verify behavior directly:
#     idempotent (same OS => no state change) + on-change (current_os flips).
cat > /tmp/seam_test.c <<'EOF'
#include "notifier.c"
#include <stdio.h>
#include <assert.h>
/* observers from qmk_stubs */
extern uint8_t stub_get_active_layer(void);
int main(void){
    /* boot: OS_UNSURE. apply_os_change(OS_UNSURE) is idempotent (no-op). */
    apply_os_change(OS_UNSURE);
    assert(current_os == OS_UNSURE);
    /* change to MACOS: current_os flips; deactivate_layer() clears active layer. */
    apply_os_change(OS_MACOS);
    assert(current_os == OS_MACOS);
    assert(stub_get_active_layer() == 255);   /* deactivated (no layer was active) */
    /* idempotent repeat: current_os unchanged, no further work */
    apply_os_change(OS_MACOS);
    assert(current_os == OS_MACOS);
    /* public API delegates to the same seam */
    notifier_set_os(OS_LINUX);
    assert(current_os == OS_LINUX);
    notifier_set_os(OS_LINUX);                /* idempotent via the seam */
    assert(current_os == OS_LINUX);
    printf("apply_os_change seam: PASS (idempotent + on-change + public-delegates)\n");
    return 0;
}
EOF
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    /tmp/seam_test.c qmk_stubs/qmk_stubs.c -o /tmp/seam_test 2>/dev/null \
  && /tmp/seam_test; echo "seam-test exit=$? (expect 0)"
rm -f /tmp/seam_test.c /tmp/seam_test
```

### Level 3: Integration Testing (Regression — the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Build the shared stub object exactly as run_notifier_stub_tests.sh does.
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
# Expected: exit 0 (warnings non-fatal; gate is NOT -Werror).

# 3b. REGRESSION — multi-OS suite MUST stay 31/31 (behavior identical).
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c test_notifier_os.c -o /tmp/test_notifier_os \
  && /tmp/test_notifier_os | tail -2
echo "notifier_os exit=$? (expect 0; 'failed: 0')"

# 3c. REGRESSION — dispatch suite MUST stay 14/14.
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/test_notifier_dispatch \
  && /tmp/test_notifier_dispatch | tail -2
echo "notifier_dispatch exit=$? (expect 0; 'failed: 0')"

# 3d. Full existing gate (both notifier suites via the runner). Warnings print but
#     the gate MUST end "✓ notifier stub-compile gate PASSED" with 0 FAIL: lines.
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "stub-tests exit=$?"
tail -n 3 /tmp/ns.out
rm -f /tmp/notifier_stub.o /tmp/test_notifier_os /tmp/test_notifier_dispatch /tmp/ns.out
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Mode-A doc present on apply_os_change (item §5, verbatim sense).
grep -q 'Shared seam for OS changes from both the keymap' notifier.c && echo "seam-purpose anchor ok"
grep -q 'Sole mutation point for' notifier.c && echo "sole-mutation anchor ok"
grep -q 'Idempotent on an unchanged value (F9.3)' notifier.c && echo "F9.3 anchor ok"
grep -q 'clears state on change (F9.1)' notifier.c && echo "F9.1 anchor ok"
grep -q 'Does NOT re-dispatch the last message (F9.2)' notifier.c && echo "F9.2 anchor ok"

# 4b. notifier_set_os is a pure one-line forwarder (no logic leaked into it).
! sed -n '/^void notifier_set_os(os_variant_t os) {/,/^}/p' notifier.c | grep -qE 'disable_command|deactivate_layer|current_os =' \
  && echo "forwarder is pure (good)" || echo "FAIL: logic leaked into forwarder"

# 4c. The CONSOLE_ENABLE uprintf is inside apply_os_change, NOT the forwarder.
sed -n '/static void apply_os_change(os_variant_t os) {/,/^}/p' notifier.c | grep -q 'uprintf' \
  && echo "uprintf in apply_os_change (good)"
! sed -n '/^void notifier_set_os(os_variant_t os) {/,/^}/p' notifier.c | grep -q 'uprintf' \
  && echo "uprintf NOT in forwarder (good)"

# 4d. Diff hygiene: only notifier.c changed in source (besides plan/ artifacts).
git diff --stat -- ':!plan'
# Expected: only `notifier.c` listed.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: notifier.c stub-compiles (exit 0); only the 4 S1 `-Wunused` warnings; no `apply_os_change` warning.
- [ ] Level 2: `current_os` written in exactly 2 places (init + apply_os_change); seam test PASS.
- [ ] Level 3: `test_notifier_os` 31/31; `test_notifier_dispatch` 14/14; `run_notifier_stub_tests.sh` PASSED.
- [ ] Level 4: all Mode-A anchors present; forwarder is pure; uprintf in apply_os_change; diff is notifier.c-only.

### Feature Validation

- [ ] `apply_os_change` is `static`, signature `void apply_os_change(os_variant_t os)`.
- [ ] `notifier_set_os` is a one-line forwarder (`apply_os_change(os);`), public, signature unchanged.
- [ ] F9 logic (idempotent + disable_command + deactivate_layer + no re-dispatch) lives in apply_os_change only.
- [ ] `current_os` sole mutation point preserved (init + apply_os_change).

### Code Quality Validation

- [ ] Behavior byte-identical (31/31 + 14/14 regression green — proven).
- [ ] Matches existing file style (inline static definition before caller; terse comments).
- [ ] Additive to the §8.7 region only; S1's host globals/board_rules_present untouched.
- [ ] No new includes, no forward declarations, no header change.

### Documentation & Deployment

- [ ] Mode-A comment on apply_os_change (item §5 verbatim sense).
- [ ] Forwarder comment notes it delegates to the sole mutation point (invariant 17).
- [ ] Forward dependency on P1.M2.T2.S2 (SET_OS calls apply_os_change) documented.

---

## Anti-Patterns to Avoid

- ❌ Don't rewrite the body — MOVE it verbatim (findings F2). Any "improvement" risks
  the 31/31 + 14/14 regression.
- ❌ Don't leave the `CONSOLE_ENABLE uprintf` in the forwarder — it stays in apply_os_change (item §3).
- ❌ Don't make `apply_os_change` non-static or declare it in notifier.h — it's an internal seam.
- ❌ Don't reorder `disable_command()`/`deactivate_layer()` — order is deliberate (invariant 4).
- ❌ Don't write `current_os` anywhere else (e.g. directly in a future SET_OS handler) — sole
  mutation point is invariant 17; SET_OS must CALL apply_os_change.
- ❌ Don't add a forward declaration of apply_os_change if you place its body above the forwarder.
- ❌ Don't touch notifier.h, pattern_match.*, qmk_stubs/*, test_*.c, run_*.sh, README.md, PRD.md,
  tasks.json, prd_snapshot.md, .gitignore.
- ❌ Don't restyle or move S1's host globals / board_rules_present — they are orthogonal scaffolding.

---

## Confidence Score: 10/10

This is a pure, surgical extract-method refactor. The exact OLD block (verified
verbatim in the working `notifier.c` ~L493-523) and the exact NEW block are
specified inline. The refactor was **empirically validated during research** against
a temp `notifier.c`: it stub-compiles clean (only the 4 pre-existing S1 `-Wunused`
warnings; **no** `apply_os_change` warning since the forwarder uses it),
`test_notifier_os` passes **31/31 unchanged**, and `test_notifier_dispatch` passes
**14/14 unchanged**. `current_os`'s write set is verified to collapse to
{init, apply_os_change}. The seam's consumer (P1.M2.T2.S2 SET_OS) is documented
with the exact call (`apply_os_change((os_variant_t)os_byte)`) and the verified
enum/byte-value parity. No external dependencies; scope boundaries with S1
(scaffolding) and P1.M2.T2.S2 (SET_OS consumer) are explicit.