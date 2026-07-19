# PRP — P1.M2.T1.S3: Implement `apply_host_callbacks` (disable-before-enable diff against `host_cb_enabled[]`)

## Goal

**Feature Goal**: Append the **host-callback diff** `apply_host_callbacks()` to
`notifier.c` — the host-side mirror of the board `enable_command()`/
`disable_command()` pair. Given the host's **full desired enabled** callback-id
set (`ids[0..count)`), it diffs against the current `host_cb_enabled[]` state
(P1.M1.T2.S1) and fires `on_enable`/`on_disable` for only the **changed** ids,
**disable-before-enable** (§13 invariant 4 / architecture "Host callback diff
algorithm"). Phase 1 disables newly-out-of-set ids (fires `on_disable`), Phase 2
enables newly-in-set ids (fires `on_enable`); unchanged ids fire neither. Defensive
id-range checks (RISK-3) guard every registry dereference, and NULL `on_enable`/
`on_disable` are guarded (same pattern as `enable_command`/`disable_command`).

**Deliverable**: One new `static void apply_host_callbacks(const uint8_t *ids,
uint8_t count)` function **inserted into `notifier.c`** between `set_host_layer()`'s
closing brace (~L265) and `enable_command()` (L267) — i.e. inside the §8.4 "Layer &
command state machines" region, immediately after its host sibling `set_host_layer`,
keeping the two host state machines contiguous. Plus a Mode-A block comment citing
§14, invariant 4, RISK-3, and the disable-before-enable ordering (item-spec §5
DOCS). Nothing else is modified.

**Success Definition**:
- `apply_host_callbacks` present with the exact signature
  `static void apply_host_callbacks(const uint8_t *ids, uint8_t count)`, file-local,
  inserted at the specified anchor (after `set_host_layer`, before `enable_command`).
- `gcc -Wall -Wextra -std=c99 -c notifier.c` (stub flags) → **exit 0** with **exactly
  four** permitted warnings: `set_host_layer`, `board_rules_present`,
  `has_been_queried`, and **`apply_host_callbacks`** (new this task →
  P1.M2.T2.S3). The previously-listed **`host_cb_enabled`** warning **disappears**
  (now read+written by `apply_host_callbacks`).
- A standalone verbatim-body logic harness (recording-callback registry) confirms:
  (1) disable-before-enable **ordering** (`on_disable` fires before `on_enable`
  across a transition); (2) newly-in fires `on_enable`, newly-out fires `on_disable`,
  unchanged fires neither; (3) **idempotent** re-apply fires nothing; (4) **RISK-3**:
  out-of-range ids (`>= cb_size`, `>= HOST_CALLBACK_MAX`) are skipped with no deref;
  (5) NULL `on_enable`/`on_disable` guarded (no call).
- A `#include "notifier.c"` smoke harness confirms the REAL function is callable and
  **NULL-registry-safe** (weak `{NULL,0}` default ⇒ no-op, no crash,
  `host_cb_enabled[]` stays all false).
- `test_notifier_dispatch` stays **14/14** and `test_notifier_os` stays **31/31**,
  0 FAIL each. `run_notifier_stub_tests.sh` prints "✓ notifier stub-compile gate PASSED".
- Mode-A block comment documents: "Mirrors board disable-before-enable ordering
  (§14, invariant 4). Phase 1 disables newly-out ids (fires on_disable), Phase 2
  enables newly-in ids (fires on_enable). Defensive id range check (RISK-3)."
  (item-spec §5).

## User Persona (if applicable)

**Target User**: The `APPLY_HOST_CONTEXT` typed-command handler (P1.M2.T2.S3),
which is the sole caller of `apply_host_callbacks`. Transitively: the desktop host
(QMKonnect) that, on a window change, sends `APPLY_HOST_CONTEXT{layer, callbacks,
clear_board}` carrying the **full desired enabled** callback-id set for the matched
window's rule.

**Use Case**: Host matches a window for app "code"; its `rules.toml` maps that to
host-callback ids {0, 2} (e.g. "mute-discord" + "open-terminal"). It sends
`0x81 0x9F 0xF0 0x05 [layer] [flags] [count=2] [0] [2] 0x03` (`APPLY_HOST_CONTEXT`).
The handler (P1.M2.T2.S3) optionally clears board state (`clear_board`) then calls
`set_host_layer(layer)` and `apply_host_callbacks({0,2}, 2)`. The firmware diffs:
previously-enabled {0,1} vs desired {0,2} → Phase 1 disables id 1 (fires its
`on_disable`), Phase 2 enables id 2 (fires its `on_enable`); id 0 is unchanged,
fires neither. On the next window the host may send `{}` to disable all.

**User Journey**: `APPLY_HOST_CONTEXT` handler → `apply_host_callbacks(ids, count)`
→ Phase 1 (disable newly-out: `on_disable` + `host_cb_enabled[id]=false`) → Phase 2
(enable newly-in: `on_enable` + `host_cb_enabled[id]=true`). After it returns,
`host_cb_enabled[]` is the single source of truth for which host callbacks are
enabled, and no callback was ever briefly in both states during the transition.

**Pain Points Addressed**: Gives the host-rules feature its own callback enable-set
tracker so host-driven callbacks (mute/launch apps, toggle features) fire in a
deterministic, board-orthogonal way. The diff (not blind re-enable-all) means only
genuinely-changed callbacks fire — no spurious `on_enable`/`on_disable` churn on an
unchanged set (idempotent). Disable-before-enable prevents a callback being briefly
in both states when an id moves between sets (invariant 4).

## Why

- **Completes the host state-machine half of P1.M2.T1**: T1.S1 added the
  typed-routing fork in `hid_notify`; T1.S2 added the host layer tracker
  (`set_host_layer`); T1.S3 (this) adds the host callback diff. Together they are
  the host state machines the four typed handlers (P1.M2.T2) drive.
- **Enforces board-orthogonal host-callback state (invariant 21)**: the board
  `current_command` (mutated by `enable_command`/`disable_command`) and the host
  `host_cb_enabled[]` (mutated by `apply_host_callbacks`) are distinct. This
  function touches ONLY `host_cb_enabled[]` — never `current_command`. The
  board's `enable_command`/`disable_command` is the pattern it mirrors, for a
  different state array.
- **Disable-before-enable (invariant 4)**: mirrors the board
  `disable_command()`-before-`enable_command()` sequence that `process_full_message`
  uses (it calls `disable_command()` first, then `enable_command()` on the match).
  The host callback diff does the same at the id-set level: disable the leaving ids
  before enabling the arriving ids, so an id never holds two states mid-transition.
- **Reuses the proven NULL-callback guard**: `enable_command` guards
  `command->on_enable != NULL`; `disable_command` guards
  `current_command->on_disable != NULL`. `apply_host_callbacks` guards
  `cbs[id].on_enable`/`on_disable` the same way — `host_callback_t.on_disable` is
  documented "may be NULL" (notifier.h:20).
- **Rebuild integrity**: inserts into the §8.4 region (disjoint from the
  `APPLY_HOST_CONTEXT` handler which lands in P1.M2.T2.S3 ~`handle_typed_command`/
  `hid_notify`); introduces exactly one *new* expected unused-function warning
  (`apply_host_callbacks`), which mirrors the established S1/S2 convention
  (expected → accepted → self-resolves downstream in P1.M2.T2.S3) and *removes* the
  `host_cb_enabled` unused-variable warning (now used).

## What

Insert **one** `static` function (`apply_host_callbacks`) plus its Mode-A block
comment into `notifier.c`, between `set_host_layer()`'s closing `}` (~L265) and
`void enable_command(command_map_t *command) {` (L267). The function:

```c
static void apply_host_callbacks(const uint8_t *ids, uint8_t count) {
    host_callback_t *cbs     = get_host_callbacks();     /* NULL when no DEFINE_HOST_CALLBACKS (weak default) */
    size_t           cb_size = get_host_callbacks_size(); /* 0 when no registry -> no derefs below (RISK-3) */

    /* PHASE 1 — DISABLE (newly-out-of-set): for each currently-enabled id NOT in
     * the desired set, fire on_disable then clear. Done BEFORE enables so a
     * callback is never briefly in both states (board disable-before-enable,
     * §13 invariant 4). */
    for (uint8_t id = 0; id < HOST_CALLBACK_MAX; id++) {
        if (!host_cb_enabled[id]) continue;
        bool still_desired = false;
        for (uint8_t i = 0; i < count; i++) {
            if (ids[i] == id) { still_desired = true; break; }
        }
        if (still_desired) continue;
        if (id < cb_size && cbs[id].on_disable != NULL) {   /* RISK-3 guard before deref */
            cbs[id].on_disable();
        }
        host_cb_enabled[id] = false;
    }

    /* PHASE 2 — ENABLE (newly-in-set): for each desired id not already enabled,
     * fire on_enable then set. Skips out-of-range ids (RISK-3) and ids already
     * enabled (the diff fires on_enable only for newly-in-set ids). */
    for (uint8_t i = 0; i < count; i++) {
        uint8_t id = ids[i];
        if (id >= HOST_CALLBACK_MAX) continue;             /* RISK-3: host_cb_enabled[] bounds */
        if (id >= cb_size) continue;                       /* RISK-3: registry bounds */
        if (host_cb_enabled[id]) continue;                 /* already enabled (diff) */
        if (cbs[id].on_enable != NULL) {                   /* NULL guard (enable_command pattern) */
            cbs[id].on_enable();
        }
        host_cb_enabled[id] = true;
    }
}
```

- Caches `get_host_callbacks()`/`get_host_callbacks_size()` ONCE at the top (the
  established pattern — `process_full_message` caches `def_cmd_map`/
  `def_cmd_size` the same way), then indexes `cbs[id]`.
- Two-phase: **Phase 1 disables** (iterate the enabled array, skip ids still
  desired, fire `on_disable` for the rest, clear unconditionally); **Phase 2
  enables** (iterate the desired set, skip out-of-range / already-enabled, fire
  `on_enable`, set).
- **Phase 1's clear is unconditional** — the `id < cb_size` guard protects ONLY the
  dereference; `host_cb_enabled[id] = false` runs for any newly-out id (RISK-3
  nuance: an enabled id `>= cb_size` shouldn't exist, but if it did it's still
  newly-out and must clear, without a dangerous deref).
- **Phase 2's three guards gate the whole op** — an out-of-range id is skipped
  entirely (never set), per the item contract verbatim.
- Touches ONLY `host_cb_enabled[]` — never `current_command`, never `host_layer`.

### Success Criteria

- [ ] `static void apply_host_callbacks(const uint8_t *ids, uint8_t count)` present
      verbatim, inserted after `set_host_layer` (~L265) and before `enable_command` (L267).
- [ ] `gcc -Wall -Wextra -std=c99 -c notifier.c` (stub flags) → exit 0; **exactly
      four** warnings (`apply_host_callbacks`, `set_host_layer`,
      `board_rules_present`, `has_been_queried`); **`host_cb_enabled` no longer**
      warns; no other warnings.
- [ ] Level-2 logic harness: disable-before-enable ordering + newly-in/out +
      idempotency + RISK-3 + NULL-guard all pass.
- [ ] Level-2 smoke harness: real function NULL-registry-safe (no-op, no crash).
- [ ] `test_notifier_dispatch` 14/14 + `test_notifier_os` 31/31, 0 FAIL; runner
      prints "✓ notifier stub-compile gate PASSED".
- [ ] Mode-A block comment cites §14, invariant 4, RISK-3, disable-before-enable.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write (the `apply_host_callbacks` body, verbatim from
the item contract's Phase 1 / Phase 2) and its exact placement anchor (between
`set_host_layer` ~L265 and `enable_command` L267) are specified inline below and
were **empirically validated during research** by inserting the function into a
/tmp copy of notifier.c via a python surgical replace: stub-compiles exit 0 with
**exactly four** warnings (the `host_cb_enabled` warning **gone** — now used;
`apply_host_callbacks` warning **new** — self-resolves in P1.M2.T2.S3); a
standalone verbatim-body logic harness passes all cases (disable-before-enable
ordering `di0` before `en2`, idempotency, RISK-3 out-of-range skip, NULL-callback
guards) and a `#include` smoke harness proves the real function NULL-registry-safe;
both regression suites pass unchanged. An implementer with only this PRP + repo can
make the one insertion and prove it green.

### Documentation & References

```yaml
# MUST READ — the apply_host_callbacks contract
- file: PRD.md   (snapshot: plan/003_16d737de7a3e/prd_snapshot.md)
  section: "## 14 Host-Side Rules & Typed Commands -> 'Second layer tracker host_layer ...'"
  why: "Declares host_cb_enabled[] + apply_host_callbacks: 'apply_host_callbacks(const uint8_t *ids,
        uint8_t count); /* disable-before-enable diff */'. And: 'apply_host_callbacks mirrors the
        board's disable-before-enable ordering: disable newly-out-of-set ids (fire on_disable), then
        enable newly-in-set ids (fire on_enable).'"
  critical: "'disable-before-enable diff' is load-bearing: Phase 1 (disable newly-out, fire
        on_disable) MUST precede Phase 2 (enable newly-in, fire on_enable). host_cb_enabled[] is
        the DIFF TARGET and the single source of truth for enabled host callbacks."

- file: PRD.md
  section: "### 4.6 Typed-command namespace -> APPLY_HOST_CONTEXT.id…"
  why: "'the FULL desired enabled host-callback id set; the firmware diffs against its current
        enabled set and calls on_enable/on_disable accordingly (disable-before-enable).' Confirms
        ids[] is the full desired set (not a delta) and the diff semantics."
  critical: "ids[] is the FULL desired set — apply_host_callbacks must NOT treat it as incremental.
        Diffing against host_cb_enabled[] (not a stored previous-set) is what makes it idempotent."

# Architecture — the diff algorithm pseudocode
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## Host callback diff algorithm (apply_host_callbacks)"
  why: "The canonical pseudocode: PHASE 1 DISABLE (for id in 0..HOST_CALLBACK_MAX: if enabled AND
        not in desired: call on_disable if non-NULL; clear), PHASE 2 ENABLE (for id in desired: if
        not enabled AND id < size: call on_enable if non-NULL; set). And 'Ordering: disable-before-
        enable mirrors the board disable_command()/enable_command() ordering (invariant 4).'"
  critical: "Phase 1 iterates the HOST_CALLBACK_MAX-wide array; Phase 2 iterates the count-wide
        desired set. The still-desired check (is id in ids[]?) gates Phase 1. The defensive
        id < get_host_callbacks_size() guard (RISK-3) appears in BOTH phases."

- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### RISK-3: apply_host_callbacks id range validation (LOW)"
  why: "States the defensive requirement: 'The host sends callback ids; the firmware must
        defensively skip ids >= get_host_callbacks_size() (malformed host data should not crash).
        Both the disable and enable phases must range-check.' Mitigation: 'Explicit if (id <
        get_host_callbacks_size()) guard before dereferencing get_host_callbacks()[id].'"
  critical: "BOTH phases range-check. Phase 2 additionally needs id < HOST_CALLBACK_MAX (host_cb_enabled[]
        bounds) before indexing it. Phase 1's id loop is bounded by HOST_CALLBACK_MAX so its array
        index is always safe; only the DEREFERENCE (cbs[id]) needs the id < cb_size guard. Phase 1
        still clears host_cb_enabled[id] for a newly-out id regardless of range (the clear is safe
        because the loop bound already guarantees id < HOST_CALLBACK_MAX)."

# Constants reference
- file: notifier.h
  section: "Host-Side Rules & Typed Commands (§4.6 / §14) — HOST_CALLBACK_MAX, host_callback_t"
  why: "HOST_CALLBACK_MAX = 32 (bounds host_cb_enabled[] and the Phase 1 loop). callback_t =
        void(*)(void). host_callback_t = {name, on_enable, on_disable; /* may be NULL */}. get_host_callbacks()/
        get_host_callbacks_size() accessor signatures."
  critical: "host_callback_t.on_disable is documented 'may be NULL' (notifier.h:20) — the NULL guard is
        MANDATORY, not defensive over-engineering. HOST_CALLBACK_MAX (32) fits in uint8_t, so the
        Phase 1 loop var and the Phase 2 id var are both uint8_t. Do NOT redefine any of these — they
        are LANDED in notifier.h (P1.M1.T1.S1)."

# Dependency PRPs — what exists when this task starts (CONTRACTS)
- file: plan/003_16d737de7a3e/P1M1T2S1/PRP.md
  why: "LANDED (notifier.c): host_cb_enabled[HOST_CALLBACK_MAX] = {false} (the diff target this task
        mutates), host_layer, has_been_queried; the weak get_host_callbacks()->NULL /
        get_host_callbacks_size()->0 accessors; board_rules_present()."
  critical: "host_cb_enabled[] exists, zero-init, and currently warns 'defined but not used'. This task
        makes it used (apply_host_callbacks reads+writes it) so that warning DISAPPEARS. Do NOT
        redeclare host_cb_enabled or change its initializer. The weak accessors return NULL/0 when no
        DEFINE_HOST_CALLBACKS — apply_host_callbacks MUST be safe against NULL cbs (RISK-3: cb_size==0
        means no derefs)."

- file: plan/003_16d737de7a3e/P1M1T1S1/PRP.md
  why: "LANDED (notifier.h): host_callback_t, HOST_CALLBACK_MAX(32), HOST_LAYER_BASE(224),
        DEFINE_HOST_CALLBACKS macro (emits strong get_host_callbacks/get_host_callbacks_size)."
  critical: "This task references host_callback_t + HOST_CALLBACK_MAX only. DEFINE_HOST_CALLBACKS is
        what a keymap uses to OVERRIDE the weak accessors — verified strong T beats weak W (P1.M1.T2.S1
        Level 3). apply_host_callbacks consumes whatever get_host_callbacks() returns at runtime."

- file: plan/003_16d737de7a3e/P1M2T1S2/PRP.md
  why: "LANDED (notifier.c): set_host_layer (the host layer tracker) at notifier.c:252, immediately
        before enable_command. This task inserts apply_host_callbacks right after set_host_layer."
  critical: "set_host_layer is the IMMEDIATE PRECEDING SIBLING (host layer tracker). apply_host_callbacks
        (host callback diff) goes right after it, so the two host state machines stay contiguous in
        §8.4. set_host_layer touches host_layer; apply_host_callbacks touches host_cb_enabled[] — fully
        disjoint globals, no overlap. Do NOT modify set_host_layer."

- file: plan/003_16d737de7a3e/P1M2T1S1/PRP.md   (parallel — landed)
  why: "P1.M2.T1.S1 added the typed_mode flag + 0xF0 discriminator routing fork in hid_notify +
        a handle_typed_command stub. Its edit region is DISJOINT from this task's (§8.4 ~L252-285)."
  critical: "Do NOT edit hid_notify, typed_mode, or handle_typed_command — those are S1's scope. This
        task's only insertion is apply_host_callbacks in §8.4. grep confirms zero overlap."

# The pattern to mirror (the file being modified)
- file: notifier.c
  section: "§8.4 Layer & command state machines — enable_command / disable_command (L267-285)"
  why: "enable_command/disable_command are the EXACT template apply_host_callbacks mirrors at the
        callback level: guard NULL on_enable/on_disable before calling, mutate the enable-state last."
  pattern: "enable_command: current_command=command; if(command->on_enable!=NULL) command->on_enable();
        disable_command: if(current_command!=NULL && current_command->on_disable!=NULL) current_command->on_disable();
        current_command=NULL;"
  gotcha: "apply_host_callbacks guards BOTH cbs[id].on_enable and cbs[id].on_disable (host_callback_t.on_disable
        is documented 'may be NULL'). The guard is `!= NULL` (matches enable_command/disable_command exactly).
        Do NOT crash on a NULL callback."

# Stub semantics — matter for the test
- file: qmk_stubs/qmk_stubs.c
  why: "Provides layer_on/layer_off/raw_hid_send. apply_host_callbacks calls NONE of these (it calls
        the registry callbacks directly). The stub is therefore irrelevant to apply_host_callbacks's
        logic; the logic test uses a standalone mock with recording callbacks instead."
  critical: "The logic test is a STANDALONE program (verbatim body + controlled registry) because
        apply_host_callbacks + host_cb_enabled[] are both static AND the registry override needs a
        separate TU (the static-vs-weak tension — see Test Strategy). This mirrors P1.M1.T2.S1's
        board_rules_present approach exactly."

# Regression targets (must stay green)
- file: test_notifier_dispatch.c
  why: "Legacy reassembly/F4/F5/F6 ack — 14 cases, all printable data[2]. apply_host_callbacks is
        never called by the legacy path. MUST stay 14/14."
- file: test_notifier_os.c
  why: "Multi-OS F8/F9 — 31 cases, all printable data[2], never call apply_host_callbacks. MUST stay 31/31."

# Build/test gate
- file: run_notifier_stub_tests.sh
  why: "Object-compiles notifier.c with -Wall -Wextra (NOT -Werror), links BOTH notifier drivers,
        runs them, asserts 0 FAIL. The 4 expected -Wunused warnings are non-fatal. Must print
        '✓ notifier stub-compile gate PASSED'."
```

### Current Codebase tree (relevant slice — post-P1.M1.T2.S1 + P1.M2.T1.S1 + P1.M2.T1.S2 state)

```bash
notifier.c                # ← MODIFY (1 insertion: apply_host_callbacks after set_host_layer, before enable_command).
                          # Current state: S1 host globals (L143-145) + board_rules_present (L204)
                          # + set_host_layer (L252, LANDED by S2) + enable_command/disable_command (L267-285)
                          # + typed_mode fork in hid_notify (L525-620).
notifier.h                # LANDED: host_callback_t/HOST_CALLBACK_MAX(32)/callback_t/typed-cmd consts/DEFINE_HOST_CALLBACKS. DO NOT TOUCH.
pattern_match.{c,h}       # unaffected. DO NOT TOUCH.
qmk_stubs/                # os_detection.h, print.h, qmk_stubs.c. DO NOT TOUCH.
test_notifier_dispatch.c  # regression (14 cases). DO NOT TOUCH.
test_notifier_os.c        # regression (31 cases). DO NOT TOUCH.
run_notifier_stub_tests.sh# gate. DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
notifier.c                # MODIFIED: +apply_host_callbacks() (§8.4 region, between set_host_layer and enable_command).
# (no new files; no header change)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — apply_host_callbacks IS expected to warn 'defined but not used' until P1.M2.T2.S3
//   (VERIFIED during research). It is a static function whose only caller is the
//   APPLY_HOST_CONTEXT handler (not yet implemented). -Wunused-function fires under
//   -Wall -Wextra (exit stays 0 — the gate is NOT -Werror). This mirrors the S1/S2
//   convention: ACCEPT the warning; do NOT suppress it with __attribute__((unused))
//   (not this codebase's idiom). It self-resolves the moment APPLY_HOST_CONTEXT calls it.
//   After THIS task the permitted-warning set is EXACTLY four:
//     * 'apply_host_callbacks defined but not used [-Wunused-function]'  (NEW this task -> P1.M2.T2.S3)
//     * 'set_host_layer defined but not used [-Wunused-function]'        (carried S2 -> P1.M2.T2.S3)
//     * 'board_rules_present defined but not used [-Wunused-function]'   (carried S1 -> P1.M2.T2.S1)
//     * 'has_been_queried defined but not used [-Wunused-variable]'      (carried S1 -> P1.M2.T2.S1)
//   AND 'host_cb_enabled defined but not used' is GONE (apply_host_callbacks now reads+writes it).
//   If host_cb_enabled STILL warns, apply_host_callbacks isn't referencing it. If any OTHER
//   warning appears, investigate.

// CRITICAL — touch ONLY host_cb_enabled[], NEVER current_command or host_layer (invariant 21).
//   current_command (board command state) is mutated only by enable_command/disable_command
//   (the legacy string path). host_cb_enabled[] (host callback state) is mutated only by
//   apply_host_callbacks (the typed path). host_layer is mutated only by set_host_layer.
//   VERIFIED: apply_host_callbacks has zero references to current_command or host_layer.

// CRITICAL — disable-before-enable (invariant 4). Phase 1 (disable) MUST run BEFORE Phase 2
//   (enable). Do NOT reorder or merge the phases. An id that leaves set A and joins set B
//   fires on_disable (Phase 1) before any on_enable (Phase 2), so it is never briefly "both".
//   The board process_full_message does the same: disable_command() THEN enable_command().

// CRITICAL — RISK-3: BOTH phases range-check ids against cb_size before dereferencing cbs[id].
//   Malformed host data (id >= registry size) must NOT crash. Phase 1's loop is bounded by
//   HOST_CALLBACK_MAX so host_cb_enabled[id] is always a safe index — but cbs[id] needs the
//   `id < cb_size` guard. Phase 2 needs BOTH `id < HOST_CALLBACK_MAX` (host_cb_enabled[] bounds)
//   AND `id < cb_size` (registry bounds) because its source is the untrusted ids[] array.

// CRITICAL — RISK-3 nuance: Phase 1's clear is UNCONDITIONAL. The `id < cb_size` guard protects
//   ONLY the cbs[id] dereference; host_cb_enabled[id] = false runs for EVERY newly-out id. (An
//   enabled id >= cb_size is impossible in normal operation — you can't enable what the registry
//   doesn't have — but defensively, if one existed, it's still newly-out and must clear, WITHOUT
//   a dangerous deref.) Do NOT wrap the clear in the `if (id < cb_size && ...)` guard.
//   Phase 2 is different: ALL THREE guards (HOST_CALLBACK_MAX, cb_size, !enabled) gate the WHOLE
//   operation including the set, per the item contract verbatim.

// GOTCHA — cache the accessors ONCE at the top, not per-iteration. process_full_message caches
//   def_cmd_map/def_cmd_size; apply_host_callbacks caches cbs/cb_size the same way. The accessors
//   may be the weak {NULL,0} defaults or a keymap's strong override — either way, call once, index
//   into the result. (Calling per-iteration is wasteful and diverges from the established pattern.)

// GOTCHA — NULL-registry safety. When no DEFINE_HOST_CALLBACKS, cbs==NULL and cb_size==0. Phase 1
//   finds host_cb_enabled[] all false (never enabled) → no disable branch. Phase 2 hits
//   `id >= cb_size` (0) → every id continues. So cbs (NULL) is NEVER dereferenced. Do NOT add a
//   separate `if (cbs == NULL) return;` — the existing `id < cb_size` guards already make it safe,
//   and a NULL check would diverge from the symmetric Phase-1/Phase-2 structure. (VERIFIED: smoke
//   test confirms no crash + host_cb_enabled[] stays all false with the weak default.)

// GOTCHA — use the macros/types from notifier.h, not redefinitions. host_callback_t, HOST_CALLBACK_MAX,
//   callback_t, get_host_callbacks/get_host_callbacks_size are ALL already declared (LANDED). Do NOT
//   redefine or redeclare any of them. Do NOT hardcode 32 — use HOST_CALLBACK_MAX.

// GOTCHA — placement is BETWEEN set_host_layer (~L265) and enable_command (L267), i.e. the §8.4
//   state-machine region stays contiguous and the two HOST state machines (set_host_layer +
//   apply_host_callbacks) are adjacent. Do NOT place apply_host_callbacks near hid_notify or the
//   typed fork (that's P1.M2.T1.S1's region) — it belongs with its state-machine siblings.

// GOTCHA — apply_host_callbacks is STATIC. Its only caller (APPLY_HOST_CONTEXT, P1.M2.T2.S3) is in
//   this same file. enable_command/disable_command are NON-static (called from process_full_message
//   + possibly a keymap) — do NOT copy that; apply_host_callbacks must not leak into the global
//   namespace. (A non-static apply_host_callbacks would also not warn 'unused', masking the S3->S2
//   boundary — keep it static so the warning correctly signals the pending consumer.)

// GOTCHA — ids is `const uint8_t *` (read-only view of the desired set). count is uint8_t (max 255).
//   The host's APPLY_HOST_CONTEXT id list may span multiple 32-byte reports (§4.6 multi-report
//   framing), but by the time apply_host_callbacks is called, the handler has reassembled the full
//   ids[] array — apply_host_callbacks just consumes it. count==0 means "disable all" (Phase 1
//   disables every currently-enabled id; Phase 2 is a 0-iteration no-op).

// GOTCHA — the still_desired lookup in Phase 1 is O(HOST_CALLBACK_MAX * count). With HOST_CALLBACK_MAX=32
//   and count typically small (a handful of callbacks), this is negligible on any MCU. Do NOT
//   "optimize" with a bitset — the straightforward nested loop is clear and correct, and HOST_CALLBACK_MAX
//   is small. (If HOST_CALLBACK_MAX ever grows large, revisit; for 32 this is fine.)

// GOTCHA — do NOT touch set_host_layer, enable_command, disable_command, hid_notify, typed_mode, or
//   handle_typed_command. This task's single insertion is apply_host_callbacks in §8.4. grep confirms
//   zero overlap with S1 (typed fork ~L525-620) and S2 (set_host_layer L252).
```

## Implementation Blueprint

### Data models and structure

No new types, no new globals, no new includes. This task consumes:
- `host_cb_enabled[HOST_CALLBACK_MAX]` (notifier.c, static bool[], zero-init from
  P1.M1.T2.S1) — the diff target / single source of truth for enabled host callbacks.
- `get_host_callbacks()` / `get_host_callbacks_size()` (notifier.c weak accessors
  from P1.M1.T2.S1, overridable via `DEFINE_HOST_CALLBACKS` in notifier.h) — the
  registry to resolve callbacks from.
- `host_callback_t` (notifier.h from P1.M1.T1.S1) — `{name, on_enable, on_disable}`.
- `HOST_CALLBACK_MAX` (notifier.h from P1.M1.T1.S1, = 32) — the array bound.

And adds one `static void apply_host_callbacks(const uint8_t *ids, uint8_t count)`
function.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — INSERT apply_host_callbacks() between set_host_layer and enable_command
  - LOCATE the anchor: the close brace of set_host_layer (~L265), immediately
    followed by a blank line and then 'void enable_command(command_map_t *command) {' (L267).
  - INSERT (between set_host_layer's '}' and enable_command) the Mode-A block comment +
    the function (see "The exact code to write").
  - SIGNATURE: static void apply_host_callbacks(const uint8_t *ids, uint8_t count)
  - BODY: two-phase diff — cache accessors; Phase 1 disable loop (HOST_CALLBACK_MAX-wide,
    skip still-desired, guard+deref on_disable, clear unconditionally); Phase 2 enable loop
    (count-wide, skip out-of-range/already-enabled, guard+deref on_enable, set).
  - NAMING: apply_host_callbacks (static, snake_case). Params 'ids' (const uint8_t*) +
    'count' (uint8_t) — match the §14 declaration verbatim.
  - DEPENDENCIES: host_cb_enabled[] (L144), get_host_callbacks/get_host_callbacks_size
    (weak, L~140), host_callback_t + HOST_CALLBACK_MAX (notifier.h). All present.
  - PRESERVE: set_host_layer (L252-265) untouched; enable_command/disable_command
    (L267-285) untouched; the S1 host globals (L143-145) untouched; the typed_mode fork
    (L525-620) untouched.
  - DO NOT: make it non-static; touch current_command/host_layer; suppress the unused
    warning; add #includes; hardcode 32; add a separate NULL-cbs early return; reorder
    the phases; wrap Phase 1's clear in the deref guard.

Task 2: VERIFY (no edit) — compile + regression + logic + NULL-safety
  - Run Validation Level 1 (stub-compile; exit 0; exactly 4 warnings; host_cb_enabled gone).
  - Run Validation Level 2a (real-function #include smoke: NULL-registry no-op, no crash).
  - Run Validation Level 2b (standalone verbatim-body logic: ordering/diff/idempotency/RISK-3/NULL).
  - Run Validation Level 3 (dispatch 14/14 + os 31/31, 0 FAIL; runner PASSED).
  - Run Level 4 (Mode-A doc anchors; diff confined to notifier.c).
```

**The exact code to write** — insert between `set_host_layer()`'s closing `}`
(~L265) and `void enable_command(command_map_t *command) {` (L267):

```c
/* apply_host_callbacks (§14) — Host callback diff against host_cb_enabled[].
 * Given the host's FULL desired enabled id set (ids[0..count)), this mirrors the
 * board disable-before-enable ordering (§13 invariant 4): Phase 1 disables newly-
 * out-of-set ids (fires on_disable), THEN Phase 2 enables newly-in-set ids (fires
 * on_enable). Doing disables first prevents a callback being briefly in both
 * states during a transition (an id moving out of set A and into set B fires its
 * on_disable before any on_enable). Unchanged ids fire neither callback.
 *
 * Defensive id range check (RISK-3, findings_and_risks.md): malformed host data
 * must not crash the firmware, so both phases range-check ids against
 * get_host_callbacks_size() before dereferencing the registry, and Phase 2 also
 * checks id < HOST_CALLBACK_MAX (bounds for host_cb_enabled[]). Phase 1 still
 * clears host_cb_enabled[id] for an out-of-set id even if it is >= the registry
 * size (the array, not the dereference, is what we clear). NULL on_enable/
 * on_disable are guarded (same pattern as enable_command/disable_command). The
 * sole caller is the APPLY_HOST_CONTEXT handler (P1.M2.T2.S3). */
static void apply_host_callbacks(const uint8_t *ids, uint8_t count) {
    host_callback_t *cbs     = get_host_callbacks();     /* NULL when no DEFINE_HOST_CALLBACKS (weak default) */
    size_t           cb_size = get_host_callbacks_size(); /* 0 when no registry -> no derefs below (RISK-3) */

    /* PHASE 1 — DISABLE (newly-out-of-set): for each currently-enabled id NOT in
     * the desired set, fire on_disable then clear. Done BEFORE enables so a
     * callback is never briefly in both states (board disable-before-enable,
     * §13 invariant 4). */
    for (uint8_t id = 0; id < HOST_CALLBACK_MAX; id++) {
        if (!host_cb_enabled[id]) continue;
        bool still_desired = false;
        for (uint8_t i = 0; i < count; i++) {
            if (ids[i] == id) { still_desired = true; break; }
        }
        if (still_desired) continue;
        if (id < cb_size && cbs[id].on_disable != NULL) {   /* RISK-3 guard before deref */
            cbs[id].on_disable();
        }
        host_cb_enabled[id] = false;
    }

    /* PHASE 2 — ENABLE (newly-in-set): for each desired id not already enabled,
     * fire on_enable then set. Skips out-of-range ids (RISK-3) and ids already
     * enabled (the diff fires on_enable only for newly-in-set ids). */
    for (uint8_t i = 0; i < count; i++) {
        uint8_t id = ids[i];
        if (id >= HOST_CALLBACK_MAX) continue;             /* RISK-3: host_cb_enabled[] bounds */
        if (id >= cb_size) continue;                       /* RISK-3: registry bounds */
        if (host_cb_enabled[id]) continue;                 /* already enabled (diff) */
        if (cbs[id].on_enable != NULL) {                   /* NULL guard (enable_command pattern) */
            cbs[id].on_enable();
        }
        host_cb_enabled[id] = true;
    }
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: mirror the board enable_command/disable_command NULL-guard. Both guard the
//   callback pointer before calling: enable_command does 'if (command->on_enable != NULL)';
//   disable_command does 'if (current_command != NULL && current_command->on_disable != NULL)'.
//   apply_host_callbacks guards BOTH cbs[id].on_enable and cbs[id].on_disable the same way.
//   host_callback_t.on_disable is documented 'may be NULL' (notifier.h:20) — the guard is mandatory.

// PATTERN: cache the accessor result once. process_full_message caches
//   'command_map_t *def_cmd_map = get_command_map(); size_t def_cmd_size = get_command_map_size();'
//   at the top. apply_host_callbacks caches 'host_callback_t *cbs = get_host_callbacks();
//   size_t cb_size = get_host_callbacks_size();' the same way. Single call, index the result.

// PATTERN: disable-before-enable (invariant 4). The board process_full_message calls
//   disable_command() (step 3) THEN enable_command() (step 7). apply_host_callbacks runs
//   Phase 1 (disable) THEN Phase 2 (enable). Same ordering at the id-set level.

// PATTERN: still_desired membership test. Phase 1 must know whether each enabled id remains
//   in the desired set. A nested loop (for i in 0..count: if ids[i]==id) is the clear, correct
//   approach for HOST_CALLBACK_MAX=32 and small count. (See GOTCHA on complexity.)

// ANTI-PATTERN: do NOT touch current_command or host_layer. apply_host_callbacks mutates
//   host_cb_enabled[] ONLY. Any reference to the board command state or the host layer
//   violates invariant 21 and would couple the planes. (VERIFIED absent by the Level-4 grep.)

// ANTI-PATTERN: do NOT wrap Phase 1's clear in the deref guard. The contract: clear is
//   UNCONDITIONAL for newly-out ids; only the cbs[id] DEREFERENCE is guarded by id < cb_size.
//   'if (id < cb_size && cbs[id].on_disable != NULL) cbs[id].on_disable();
//    host_cb_enabled[id] = false;'  <-- the clear is OUTSIDE the if. Do NOT write
//   'if (id < cb_size) { if (cbs[id].on_disable != NULL) cbs[id].on_disable(); host_cb_enabled[id]=false; }'.

// ANTI-PATTERN: do NOT add a separate 'if (cbs == NULL) return;' at the top. The per-id
//   'id < cb_size' guards already make a NULL cbs safe (cb_size==0 -> no deref). A separate
//   NULL check diverges from the symmetric two-phase structure and is dead code.

// ANTI-PATTERN: do NOT make apply_host_callbacks non-static. Its only caller (APPLY_HOST_CONTEXT,
//   P1.M2.T2.S3) is in THIS file. A non-static function wouldn't warn 'unused', hiding the S3->S2
//   boundary, and would leak an internal symbol.

// ANTI-PATTERN: do NOT suppress the apply_host_callbacks unused-function warning. Accept it; it
//   self-resolves in P1.M2.T2.S3. (Same convention S1/S2 used.)

// ANTI-PATTERN: do NOT reorder/merge the phases. Disable-before-enable is invariant 4. A merged
//   single-pass (e.g. iterate ids[] and decide enable/disable inline) would NOT guarantee all
//   on_disable fire before any on_enable — keep the two distinct phases.

// ANTI-PATTERN: do NOT hardcode 32. Use HOST_CALLBACK_MAX (notifier.h) so a future cap change
//   propagates. Do NOT redefine host_callback_t/callback_t/HOST_CALLBACK_MAX — all LANDED.

// ANTI-PATTERN: do NOT edit set_host_layer / enable_command / disable_command / hid_notify /
//   typed_mode / handle_typed_command. This task's single insertion is apply_host_callbacks in §8.4.
```

### Integration Points

```yaml
§8.4 STATE MACHINES (notifier.c):
  - insert: apply_host_callbacks (static) between set_host_layer (~L265) and enable_command (L267)
  - siblings: set_host_layer (host layer tracker); activate_layer/deactivate_layer (board, non-static);
    enable_command/disable_command (board command, non-static)
CONSUMERS (downstream, NOT this task):
  - APPLY_HOST_CONTEXT handler (P1.M2.T2.S3): calls set_host_layer(layer) then
    apply_host_callbacks(ids, count) after optional clear_board
GLOBALS (read+written by apply_host_callbacks):
  - host_cb_enabled[] (L144): the host callback enable set. (Was unused->warning; now used->warning gone.)
ACCESSORS (called):
  - get_host_callbacks(), get_host_callbacks_size() — weak default {NULL,0} or keymap override
BUILD/CONFIG/ROUTES/DATABASE:
  - none. No rules.mk, no wire change, no header change, no runtime config.
```

## Validation Loop

> Toolchain: gcc (C project; no ruff/mypy/pytest). notifier.c is stub-compiled via
> `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.` (the exact command
> `run_notifier_stub_tests.sh` uses). All commands below were **executed during
> research against a temp notifier.c carrying the apply_host_callbacks insertion
> and PASSED**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c. Expect exit 0 AND EXACTLY FOUR -Wunused warnings.
#     CRITICAL: 'host_cb_enabled' must NO LONGER warn (apply_host_callbacks now uses it);
#     'apply_host_callbacks' must be the NEW warning (self-resolves P1.M2.T2.S3).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
echo "compile exit=$?  (expect 0)"
echo "-- warnings (expect EXACTLY these 4; host_cb_enabled ABSENT) --"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep 'warning:' | sed 's/^[^:]*:notifier.c://'
# Expected: exactly 4 lines — apply_host_callbacks, set_host_layer, board_rules_present
#           (-Wunused-function); has_been_queried (-Wunused-variable). NO 'host_cb_enabled' line.
# FAIL if: exit != 0, OR a 5th warning appears, OR 'host_cb_enabled defined but not used' is present.

# 1b. Confirm apply_host_callbacks landed at the right anchor with the exact signature.
grep -nE 'static void apply_host_callbacks\(const uint8_t \*ids, uint8_t count\)' notifier.c
# Expected: exactly ONE match, at a line between set_host_layer (~L265) and enable_command (L267).

# 1c. Confirm it references host_cb_enabled + the accessors (not current_command/host_layer).
awk '/static void apply_host_callbacks\(const uint8_t/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'host_cb_enabled|get_host_callbacks' && echo "refs host state ok"
awk '/static void apply_host_callbacks\(const uint8_t/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'current_command|host_layer' \
  && { echo "FAIL: apply_host_callbacks references board/host-layer state"; exit 1; } \
  || echo "no cross-plane refs (good)"

rm -f /tmp/notifier_stub.o
```

### Level 2: Component Validation (behavior — THE PRIMARY GATE)

**2a. Real-function NULL-safety smoke** (the literal `apply_host_callbacks` in
notifier.c, reached via `#include`; weak `{NULL,0}` registry). This was **verified
against a temp notifier.c** during research (ALL CASES CONFIRMED):

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/ahc_smoke.c <<'EOF'
/* #include the REAL notifier.c to reach static apply_host_callbacks + host_cb_enabled.
 * Registry is the weak {NULL,0} default -> apply_host_callbacks must be a safe no-op
 * (RISK-3: never derefs cbs[] when cb_size==0) and leave host_cb_enabled[] all false. */
#include "notifier.c"
#include <stdio.h>
int main(void){
    int fails = 0;
    for (int i = 0; i < HOST_CALLBACK_MAX; i++) if (host_cb_enabled[i]) { printf("FAIL: %d set at start\n", i); fails++; }
    uint8_t want[] = {0, 3, 7};
    apply_host_callbacks(want, 3);   /* must NOT crash, must NOT enable anything (cb_size==0) */
    for (int i = 0; i < HOST_CALLBACK_MAX; i++) if (host_cb_enabled[i]) { printf("FAIL: %d enabled despite NULL registry\n", i); fails++; }
    apply_host_callbacks(NULL, 0);   /* count==0: no-op against empty registry */
    printf("%s (%d failures) — real fn callable + NULL-registry-safe\n", fails?"SOME FAIL":"ALL CASES CONFIRMED", fails);
    return fails ? 1 : 0;
}
EOF
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    /tmp/ahc_smoke.c qmk_stubs/qmk_stubs.c -o /tmp/ahc_smoke 2>&1 | grep -vE 'defined but not used'
/tmp/ahc_smoke; echo "smoke exit=$? (expect 0)"
rm -f /tmp/ahc_smoke.c /tmp/ahc_smoke
```

**2b. Algorithm logic** (standalone verbatim-body mock with a recording-callback
registry). The body is COPIED VERBATIM from notifier.c (a comment asserts this).
This was **verified during research** (ALL CASES CONFIRMED, including the
load-bearing **disable-before-enable ordering** case `di0` before `en2`):

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/ahc_logic.c <<'EOF'
/* Standalone: apply_host_callbacks body is COPIED VERBATIM from notifier.c. A real
 * registry with recording callbacks verifies: disable-before-enable ORDER, newly-in/
 * newly-out, idempotency, RISK-3 (out-of-range ids skipped), NULL-callback guards. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HOST_CALLBACK_MAX 32
typedef void (*callback_t)(void);
typedef struct { const char *name; callback_t on_enable; callback_t on_disable; } host_callback_t;

/* --- test registry + recording log --- */
#define LOG_CAP 64
static char log_book[LOG_CAP][32];
static int log_len = 0;
#define REC(fmt, ...) do{ if(log_len<LOG_CAP) snprintf(log_book[log_len++],32,fmt,##__VA_ARGS__); }while(0)
static void en0(void){REC("en0");}
static void di0(void){REC("di0");}
static void en1(void){REC("en1");}
static void di1(void){REC("di1");}
static void en2(void){REC("en2");}                 /* on_disable NULL for id 2 */
static void en3(void){REC("en3");}                 /* on_disable NULL for id 3 */
static host_callback_t g_cbs[] = {
    {"c0", en0, di0}, {"c1", en1, di1}, {"c2", en2, NULL}, {"c3", en3, NULL},
};
static host_callback_t* get_host_callbacks(void){ return g_cbs; }
static size_t get_host_callbacks_size(void){ return sizeof(g_cbs)/sizeof(g_cbs[0]); } /* = 4 */

static bool host_cb_enabled[HOST_CALLBACK_MAX] = {false};

/* ===== VERBATIM COPY from notifier.c (Task 1) — keep in sync ===== */
static void apply_host_callbacks(const uint8_t *ids, uint8_t count) {
    host_callback_t *cbs     = get_host_callbacks();
    size_t           cb_size = get_host_callbacks_size();
    for (uint8_t id = 0; id < HOST_CALLBACK_MAX; id++) {
        if (!host_cb_enabled[id]) continue;
        bool still_desired = false;
        for (uint8_t i = 0; i < count; i++) {
            if (ids[i] == id) { still_desired = true; break; }
        }
        if (still_desired) continue;
        if (id < cb_size && cbs[id].on_disable != NULL) {
            cbs[id].on_disable();
        }
        host_cb_enabled[id] = false;
    }
    for (uint8_t i = 0; i < count; i++) {
        uint8_t id = ids[i];
        if (id >= HOST_CALLBACK_MAX) continue;
        if (id >= cb_size) continue;
        if (host_cb_enabled[id]) continue;
        if (cbs[id].on_enable != NULL) {
            cbs[id].on_enable();
        }
        host_cb_enabled[id] = true;
    }
}
/* ===== END VERBATIM COPY ===== */

static int fails = 0;
#define CK(cond) do{ if(!(cond)){printf("FAIL line %d: %s\n",__LINE__,#cond); fails++;} else printf("ok   %s\n",#cond);}while(0)
static int enabled_only(const uint8_t *want, int n){
    for (int i=0;i<HOST_CALLBACK_MAX;i++){ bool in=false; for(int j=0;j<n;j++) if(want[j]==i){in=true;break;} if(host_cb_enabled[i]!=in) return 0; }
    return 1;
}

int main(void){
    /* 1. Enable {0,1} from empty -> en0,en1 fire (id order). */
    log_len=0; uint8_t a01[]={0,1};
    apply_host_callbacks(a01,2);
    CK(enabled_only(a01,2));
    CK(log_len==2 && !strcmp(log_book[0],"en0") && !strcmp(log_book[1],"en1"));

    /* 2. Idempotent re-apply {0,1} -> NOTHING fires. */
    log_len=0; apply_host_callbacks(a01,2);
    CK(log_len==0);
    CK(enabled_only(a01,2));

    /* 3. Transition {0,1} -> {1,2}: disable-before-enable => di0 BEFORE en2. */
    log_len=0; uint8_t a12[]={1,2};
    apply_host_callbacks(a12,2);
    CK(enabled_only(a12,2));
    CK(log_len==2 && !strcmp(log_book[0],"di0") && !strcmp(log_book[1],"en2"));

    /* 4. Transition {1,2} -> {2,3}: di1 fires; en3 fires. */
    log_len=0; uint8_t a23[]={2,3};
    apply_host_callbacks(a23,2);
    CK(enabled_only(a23,2));
    CK(log_len==2 && !strcmp(log_book[0],"di1") && !strcmp(log_book[1],"en3"));

    /* 5. Transition {2,3} -> {0,3}: id2 disabled but on_disable NULL -> guarded; en0 fires. */
    log_len=0; uint8_t a03[]={0,3};
    apply_host_callbacks(a03,2);
    CK(enabled_only(a03,2));
    CK(log_len==1 && !strcmp(log_book[0],"en0"));

    /* 6. Disable ALL: {} -> on_disable fires for {0,3} in id order; id3 on_disable NULL guarded. */
    log_len=0; apply_host_callbacks(NULL,0);
    uint8_t empty[]={99}; CK(enabled_only(empty,0));
    CK(log_len==1 && !strcmp(log_book[0],"di0"));

    /* 7. RISK-3: out-of-range ids (>= cb_size=4 and >= HOST_CALLBACK_MAX) skipped, no deref. */
    log_len=0; uint8_t bad[]={3, 5, 99};
    apply_host_callbacks(bad,3);
    uint8_t just3[]={3}; CK(enabled_only(just3,1));
    CK(log_len==1 && !strcmp(log_book[0],"en3"));

    /* 8. Clean slate. */
    apply_host_callbacks(NULL,0);
    for(int i=0;i<HOST_CALLBACK_MAX;i++) CK(!host_cb_enabled[i]);

    printf("\n%s (%d failures)\n", fails?"SOME FAILURES":"ALL CASES CONFIRMED", fails);
    return fails?1:0;
}
EOF
gcc -Wall -Wextra -std=c99 /tmp/ahc_logic.c -o /tmp/ahc_logic && /tmp/ahc_logic; echo "logic exit=$? (expect 0)"
rm -f /tmp/ahc_logic.c /tmp/ahc_logic
# Expected: a line of "ok" per check (incl. the load-bearing case 3 'di0' before 'en2'),
#           then "ALL CASES CONFIRMED (0 failures)", exit 0.
```

### Level 3: Integration Testing (Regression — the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Full existing gate (both notifier suites via the runner). MUST end
#     "✓ notifier stub-compile gate PASSED" with 0 FAIL: lines for both suites.
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "stub-tests exit=$?"
tail -n 4 /tmp/ns.out
# Expected: "notifier dispatch fails=0 (exit=0)", "notifier os fails=0 (exit=0)",
#           "✓ notifier stub-compile gate PASSED".

# 3b. Explicit per-suite regression (belt-and-suspenders). Neither suite sends 0xF0
#     or calls apply_host_callbacks, so behavior is byte-identical (the function is
#     inert for them; it's only reached via APPLY_HOST_CONTEXT in P1.M2.T2.S3).
gcc -Wall -std=c99 -Iqmk_stubs -I. -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/td
gcc -Wall -std=c99 -Iqmk_stubs -I. -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -o /tmp/tos
echo "dispatch fails=$(/tmp/td 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
echo "os fails=$(/tmp/tos 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
rm -f /tmp/ns.out /tmp/td /tmp/tos
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Mode-A doc anchors (item-spec §5 DOCS).
grep -q 'Host callback diff against host_cb_enabled' notifier.c && echo "diff-target anchor ok"
grep -q 'disable-before-enable' notifier.c && echo "disable-before-enable anchor ok"
grep -q 'Phase 1 disables newly' notifier.c && echo "phase-1 anchor ok"
grep -q 'Phase 2 enables newly' notifier.c && echo "phase-2 anchor ok"
grep -q 'RISK-3' notifier.c && echo "RISK-3 anchor ok"
grep -q 'invariant 4' notifier.c && echo "invariant-4 anchor ok"

# 4b. Orthogonality at the source level: apply_host_callbacks must NOT reference
#     current_command or host_layer (invariant 21).
awk '/static void apply_host_callbacks\(const uint8_t/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'current_command|host_layer' \
  && { echo "FAIL: apply_host_callbacks references board/host-layer state (invariant 21 violation)"; exit 1; } \
  || echo "apply_host_callbacks is host_cb_enabled-only (good)"

# 4c. Disable-before-enable: Phase 1 (disable) textually precedes Phase 2 (enable).
awk '/static void apply_host_callbacks/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -nE 'PHASE 1|PHASE 2' \
  | awk 'NR==1{p1=$0} NR==2{p2=$0} END{ if(split(p1,a,":")[1] < split(p2,b,":")[1]) print "disable-before-enable order ok"; else print "FAIL: phases out of order" }'

# 4d. Phase 1's clear is OUTSIDE the deref guard (RISK-3 nuance).
awk '/static void apply_host_callbacks/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'on_disable\(\);\s*$' \
  && echo "Phase-1 clear is separate (good — unconditional clear)" \
  || echo "WARN: check Phase-1 clear placement"

# 4e. Uses HOST_CALLBACK_MAX (not literal 32) and the accessor types.
awk '/static void apply_host_callbacks/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'HOST_CALLBACK_MAX' && echo "uses HOST_CALLBACK_MAX macro (good)"
awk '/static void apply_host_callbacks/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'id\s*<\s*32' && { echo "FAIL: hardcoded 32 instead of HOST_CALLBACK_MAX"; exit 1; } || echo "no hardcoded 32 (good)"

# 4f. No separate NULL-cbs early return (the per-id guards make it redundant).
awk '/static void apply_host_callbacks/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'cbs\s*==\s*NULL' \
  && { echo "FAIL: redundant NULL-cbs early return diverges from symmetric structure"; exit 1; } \
  || echo "no redundant NULL-cbs return (good)"

# 4g. Diff hygiene: only notifier.c changed (besides plan/ artifacts).
git status --porcelain | grep -vE 'plan/003_16d737de7a3e/P1M2T1S3/' | grep -E '\.c|\.h|\.sh|\.mk' \
  && { echo "FAIL: source file other than notifier.c changed"; exit 1; } || echo "OK: only notifier.c (+ plan/)"
git diff --stat -- notifier.c
# Expected: only notifier.c listed; one hunk in the §8.4 region (~L266).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: notifier.c stub-compiles (exit 0); **exactly four** -Wunused warnings
      (`apply_host_callbacks`, `set_host_layer`, `board_rules_present`,
      `has_been_queried`); **`host_cb_enabled` no longer warns**; no other warnings.
- [ ] Level 1: `apply_host_callbacks` present at the §8.4 anchor (between set_host_layer
      and enable_command) with the exact signature.
- [ ] Level 2a: `/tmp/ahc_smoke` prints "ALL CASES CONFIRMED (0 failures)" — real fn
      callable + NULL-registry-safe.
- [ ] Level 2b: `/tmp/ahc_logic` prints "ALL CASES CONFIRMED (0 failures)", including
      **case 3 disable-before-enable ordering** (`di0` before `en2`).
- [ ] Level 3: `run_notifier_stub_tests.sh` PASSED; dispatch 14/14 + os 31/31, 0 FAIL.
- [ ] Level 4: all Mode-A doc anchors present; apply_host_callbacks is host_cb_enabled-only
      (no `current_command`/`host_layer` ref); disable-before-enable order; Phase-1 clear
      unconditional; uses `HOST_CALLBACK_MAX`; no redundant NULL-cbs return; only notifier.c changed.

### Feature Validation

- [ ] Phase 1 disables newly-out ids: skips still-desired, fires `on_disable` (if non-NULL)
      under RISK-3 guard, clears `host_cb_enabled[id]` UNCONDITIONALLY.
- [ ] Phase 2 enables newly-in ids: skips out-of-range (`>= HOST_CALLBACK_MAX` and
      `>= cb_size`) and already-enabled, fires `on_enable` (if non-NULL), sets `host_cb_enabled[id]`.
- [ ] Disable-before-enable: all `on_disable` fire before any `on_enable` across a transition.
- [ ] Idempotent: re-applying the same set fires nothing.
- [ ] Unchanged ids fire neither `on_enable` nor `on_disable`.
- [ ] NULL `on_enable`/`on_disable` guarded (no call) — mirrors enable_command/disable_command.
- [ ] NULL registry (weak default): no-op, no crash, `host_cb_enabled[]` stays all false.
- [ ] apply_host_callbacks NEVER reads/writes `current_command` or `host_layer` (invariant 21).

### Code Quality Validation

- [ ] `static void apply_host_callbacks(const uint8_t *ids, uint8_t count)` — file-local, snake_case.
- [ ] Mirrors `enable_command`/`disable_command` NULL-guard pattern; caches accessors once (process_full_message pattern).
- [ ] Two distinct phases (disable then enable), not merged.
- [ ] Uses `HOST_CALLBACK_MAX` macro (not literal 32); reuses LANDED types (no redefinition).
- [ ] Inserted in §8.4 (contiguous with set_host_layer); no restyle of existing code.
- [ ] No `__attribute__((unused))` suppression; no new #includes; no header change.

### Documentation & Deployment

- [ ] Mode-A block comment cites §14, invariant 4, RISK-3, and disable-before-enable (item-spec §5).
- [ ] Comment states: "Host callback diff against host_cb_enabled[]" and "Phase 1 disables newly-out
      ids (fires on_disable), Phase 2 enables newly-in ids (fires on_enable)."
- [ ] Consumer boundary clear: APPLY_HOST_CONTEXT handler (P1.M2.T2.S3) is the sole caller;
      set_host_layer (S2) is the immediate sibling.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't touch `current_command` or `host_layer` — apply_host_callbacks mutates `host_cb_enabled[]` ONLY (invariant 21). Any reference to board command state or the host layer couples the planes.
- ❌ Don't wrap Phase 1's `host_cb_enabled[id] = false` in the `id < cb_size` deref guard — the clear is UNCONDITIONAL for newly-out ids; only the `cbs[id]` dereference is range-guarded (RISK-3 nuance).
- ❌ Don't reorder or merge the phases — disable-before-enable is invariant 4. A single merged pass cannot guarantee all `on_disable` fire before any `on_enable`.
- ❌ Don't add a separate `if (cbs == NULL) return;` — the per-id `id < cb_size` guards already make a NULL cbs safe (`cb_size==0` ⇒ no deref). A separate NULL check diverges from the symmetric structure and is dead code.
- ❌ Don't hardcode `32` — use `HOST_CALLBACK_MAX` (notifier.h). Don't redefine `host_callback_t`/`callback_t`/`HOST_CALLBACK_MAX` — all LANDED.
- ❌ Don't make `apply_host_callbacks` non-static — its only caller (`APPLY_HOST_CONTEXT`, P1.M2.T2.S3) is in this file; non-static would leak the symbol and hide the S3→S2 boundary warning.
- ❌ Don't suppress the `apply_host_callbacks` unused-function warning — accept it (self-resolves in P1.M2.T2.S3); same convention S1/S2 used.
- ❌ Don't call `get_host_callbacks()`/`get_host_callbacks_size()` per-iteration — cache once at the top (process_full_message pattern).
- ❌ Don't omit the NULL guard on `on_enable`/`on_disable` — `host_callback_t.on_disable` is documented "may be NULL" (notifier.h:20); the guard is mandatory, mirroring `enable_command`/`disable_command`.
- ❌ Don't edit `set_host_layer` / `enable_command` / `disable_command` / `hid_notify` / `typed_mode` / `handle_typed_command` — disjoint regions. This task's single insertion is `apply_host_callbacks` in §8.4.
- ❌ Don't touch the S1 host globals (`host_cb_enabled`/`host_layer`/`has_been_queried`) beyond referencing `host_cb_enabled[]` — don't redeclare or re-init them.
- ❌ Don't touch `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_notifier_*`, `run_*.sh`, `PRD.md`, `tasks.json`, `prd_snapshot.md`, `rules.mk`, or `.gitignore`. Only `notifier.c` changes.

---

## Confidence Score: 10/10

The deliverable is one surgical insertion into `notifier.c` — `static void
apply_host_callbacks(const uint8_t *ids, uint8_t count)` placed between
`set_host_layer` (~L265) and `enable_command` (L267), with a Mode-A block comment
— specified verbatim above and **empirically validated during research** by
inserting it into a /tmp copy via a python surgical replace: stub-compiles exit 0
with **exactly four** -Wunused warnings (the `host_cb_enabled` warning **gone** —
now used; `apply_host_callbacks` **new** — self-resolves in P1.M2.T2.S3); a
standalone verbatim-body logic harness passes all cases (disable-before-enable
ordering `di0` before `en2`, idempotency, RISK-3 out-of-range skip, NULL-callback
guards); and a `#include` smoke harness proves the real function NULL-registry-safe
(no-op, no crash, `host_cb_enabled[]` stays all false); both regression suites pass
unchanged (**dispatch 14/14, os 31/31, 0 FAIL**; "✓ notifier stub-compile gate
PASSED"). The dependencies (`host_cb_enabled[]` L144, the weak accessors,
`host_callback_t`/`HOST_CALLBACK_MAX` in notifier.h) are LANDED; the parallel
P1.M2.T1.S1 (typed_mode fork) and P1.M2.T1.S2 (set_host_layer) have **disjoint
edit regions**; the consumer (`APPLY_HOST_CONTEXT`, P1.M2.T2.S3) is a later subtask
with a clear boundary. The §14 contract (full-desired-set diff,
disable-before-enable), invariant 4 (board disable-before-enable), invariant 21
(two-plane orthogonality), and RISK-3 (defensive id range check, with the
Phase-1-unconditional-clear nuance) are all encoded and tested. No external
dependencies are added. The static-vs-weak test tension is resolved by mirroring
the S1 precedent (real-function `#include` smoke + standalone verbatim-body logic
mock).