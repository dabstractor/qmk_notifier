# PRP — P1.M2.T1.S2: Implement `set_host_layer` (host layer tracker, independent of board `activated_layer`)

## Goal

**Feature Goal**: Append the **host-layer tracker** `set_host_layer()` to
`notifier.c` — the host-side mirror of the existing board
`activate_layer()`/`deactivate_layer()` pair. It mutates ONLY the `host_layer`
global (P1.M1.T2.S1), calling QMK `layer_on`/`layer_off` exactly as the board
functions do, but it **never touches the board `activated_layer`** (architecture
invariant 21 / PRD §14). `0xFF` (`LAYER_UNSET`) clears; any other byte sets the
host layer (reserved ≥ `HOST_LAYER_BASE` 224 per §14/§4.6). No range validation
— the host is trusted (RISK-2).

**Deliverable**: One new `static void set_host_layer(uint8_t layer)` function
**inserted into `notifier.c`** between `deactivate_layer()` (notifier.c:235) and
`enable_command()` (notifier.c:237) — i.e. inside the §8.4 "Layer & command state
machines" region, next to its board siblings. Plus a Mode-A block comment citing
§14, invariant 21, `HOST_LAYER_BASE`, and RISK-2 (item-spec §5 DOCS). Nothing else
is modified.

**Success Definition**:
- `set_host_layer` present with the exact signature `static void set_host_layer(uint8_t layer)`,
  file-local, inserted at the specified anchor (after `deactivate_layer`, before `enable_command`).
- `gcc -Wall -Wextra -std=c99 -c notifier.c` (stub flags) → **exit 0** with **exactly four**
  permitted warnings: `board_rules_present`, `has_been_queried`, `host_cb_enabled`,
  and **`set_host_layer`** (new this task → P1.M2.T2.S3). The previously-listed
  `host_layer` warning **disappears** (now read+written by `set_host_layer`).
- A `#include "notifier.c"` harness confirms: (1) `0xFF` clears; (2) a real byte sets
  the host layer with `layer_off(old)`→`layer_on(new)`; (3) re-clear is a guarded
  no-op; and (4) **orthogonality** — after `activate_layer(5)` sets the board
  tracker, `set_host_layer(231)` leaves `activated_layer == 5` unchanged (only
  `host_layer` moves).
- `test_notifier_dispatch` stays **14/14** and `test_notifier_os` stays **31/31**,
  0 FAIL each. `run_notifier_stub_tests.sh` prints "✓ notifier stub-compile gate PASSED".
- Mode-A block comment documents: "Host layer tracker ONLY — operates on
  `host_layer`, independent of board `activated_layer` (§14). `0xFF` clears the
  host layer. Host layers reserved ≥ 224 (`HOST_LAYER_BASE`)." (item-spec §5).

## User Persona (if applicable)

**Target User**: The `APPLY_HOST_CONTEXT` typed-command handler (P1.M2.T2.S3),
which is the sole caller of `set_host_layer`. Transitively: the desktop host
(QMKonnect) that, on a window change, sends `APPLY_HOST_CONTEXT{layer, callbacks,
clear_board}` to push a host layer (≥ 224) above the board layer stack.

**Use Case**: Host detects a window for app "code", its `rules.toml` maps that to
host layer 224. It sends `0x81 0x9F 0xF0 0x05 [E0=224] [flags] [count] [ids...] 0x03`
(`APPLY_HOST_CONTEXT`). The handler (P1.M2.T2.S3) optionally clears board state
(`clear_board` flag) then calls `set_host_layer(224)` → `layer_on(224)`; QMK's
highest-layer-wins rule makes 224 active. On the next window it may send layer
`0xFF` to clear → `layer_off(224)`.

**User Journey**: `APPLY_HOST_CONTEXT` handler → `set_host_layer(layer)` →
`layer_off(host_layer)` (if one was set) → `layer_on(layer)` → `host_layer = layer`.
The board `activated_layer` is untouched throughout (unless `clear_board` ran first,
which is the handler's separate responsibility).

**Pain Points Addressed**: Gives the host-rules feature its own layer tracker so
host-pushed layers don't collide with or clobber board `DEFINE_*`-driven layers
(RISK-2). Without a separate tracker, a host layer and a board layer would share
state and the host couldn't stack/replace independently (architecture invariant 21).

## Why

- **Completes the host state-machine half of P1.M2.T1**: T1.S1 (parallel) added the
  typed-routing fork in `hid_notify`; T1.S2 (this) adds the host layer tracker;
  T1.S3 will add `apply_host_callbacks`. Together they are the host state machines
  the four typed handlers (P1.M2.T2) drive.
- **Enforces two-plane orthogonality at the function level (invariant 21)**: the
  board functions (`activate_layer`/`deactivate_layer`) mutate `activated_layer`;
  `set_host_layer` mutates `host_layer`. Neither reads the other's variable. This
  is the code-level guarantee that "board and host state are orthogonal" — verified
  by an explicit test (Level 2, case 6).
- **Reuses the proven `layer_on`/`layer_off` pattern**: the board functions already
  wrap these QMK calls safely (2023+45 stub cases green). `set_host_layer` is the
  same wrapper for a different tracker — zero new mechanism, just a second caller.
- **Rebuild integrity**: inserts into the §8.4 region (disjoint from P1.M2.T1.S1's
  `hid_notify` edits ~L525-620); introduces exactly one *new* expected unused-function
  warning (`set_host_layer`), which mirrors the established S1 convention (expected →
  accepted → self-resolves downstream in P1.M2.T2.S3) and *removes* the `host_layer`
  unused-variable warning (now used).

## What

Insert **one** `static` function (`set_host_layer`) plus its Mode-A block comment
into `notifier.c`, between `deactivate_layer()`'s closing brace (L235) and
`enable_command()` (L237). The function:

```c
static void set_host_layer(uint8_t layer) {
    if (layer == LAYER_UNSET) {                 /* (a) clear the host layer */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);
        }
        host_layer = LAYER_UNSET;
    } else {                                    /* (b) real host layer (>= 224) */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);              /* turn off old host layer first */
        }
        layer_on(layer);
        host_layer = layer;
    }
}
```

- Uses the `LAYER_UNSET` macro (notifier.c:126), not a literal `0xFF`.
- Two-branch structure mirrors the item contract (a)/(b) exactly.
- Touches ONLY `host_layer` — never `activated_layer`. No board clear, no range
  validation (RISK-2: trusts the host).

### Success Criteria

- [ ] `static void set_host_layer(uint8_t layer)` present verbatim, inserted after
      `deactivate_layer` (L235) and before `enable_command` (L237).
- [ ] `gcc -Wall -Wextra -std=c99 -c notifier.c` (stub flags) → exit 0; **exactly four**
      warnings (`set_host_layer`, `board_rules_present`, `has_been_queried`,
      `host_cb_enabled`); `host_layer` **no longer** warns; no other warnings.
- [ ] Level-2 harness: set / change / clear / re-clear / orthogonality all pass.
- [ ] `test_notifier_dispatch` 14/14 + `test_notifier_os` 31/31, 0 FAIL; runner
      prints "✓ notifier stub-compile gate PASSED".
- [ ] Mode-A block comment cites §14, invariant 21, `HOST_LAYER_BASE` (224), RISK-2.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write (the `set_host_layer` body, verbatim from the
item contract (a)/(b)) and its exact placement anchor (between `deactivate_layer`
L235 and `enable_command` L237) are specified inline below and were **empirically
validated during research** by inserting the function into a /tmp copy of
notifier.c via a python surgical replace: stub-compiles exit 0 with **exactly four**
warnings (the `host_layer` warning gone, `set_host_layer` warning new); a
`#include` harness passes all 16 assertions including the load-bearing
**orthogonality** case (`activate_layer(5)` then `set_host_layer(231)` leaves
`activated_layer == 5`); and both regression suites pass unchanged. An implementer
with only this PRP + repo can make the one insertion and prove it green.

### Documentation & References

```yaml
# MUST READ — the host-layer contract
- file: PRD.md   (snapshot: plan/003_16d737de7a3e/prd_snapshot.md)
  section: "## 14 Host-Side Rules & Typed Commands -> 'Second layer tracker host_layer ...'"
  why: "Defines set_host_layer: 'layer_on/layer_off only the host tracker; 0xFF -> clear.
        Host layers are reserved >= 224 (LAYER_UNSET = 255).' Gives the exact behavior
        (a)/(b) this task implements."
  critical: "'set_host_layer(layer): layer_on/layer_off only the host tracker; 0xFF => clear.'
        The word 'only' is load-bearing: set_host_layer MUST NOT touch board activated_layer
        (that separation is invariant 21). And 'host layers reserved >= 224' is a CONVENTION
        enforced by the host, NOT validated by the firmware (RISK-2)."

- file: PRD.md
  section: "### 4.6 Typed-command namespace -> APPLY_HOST_CONTEXT.layer"
  why: "'the host's desired host-layer number, or 0xFF (255 = LAYER_UNSET) to clear the host
        layer. Host layers are reserved >= 224 so they resolve above board layers under QMK's
        highest-layer-wins rule.' Confirms the byte set_host_layer receives."
  critical: "The 0xFF clear semantics + the >= 224 reservation rationale (highest-layer-wins)
        both come from here. set_host_layer does NOT enforce >= 224 — it trusts APPLY_HOST_CONTEXT."

# Architecture — the two-plane model + RISK-2
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## Architecture: two independent state planes" + "## The four typed-command handlers -> APPLY_HOST_CONTEXT"
  why: "The BOARD vs HOST orthogonality diagram: activated_layer (board) vs host_layer (host),
        both funneling into layer_on/off but on different layer ranges. The APPLY_HOST_CONTEXT
        handler pseudocode shows the call sequence: (clear_board?) -> set_host_layer(layer) ->
        apply_host_callbacks(ids,count)."
  critical: "invariant 21: 'process_full_message (legacy) touches ONLY board state;
        handle_typed_command touches ONLY host state (except clear_board + SET_OS).' set_host_layer
        is the host-state toucher for layers. It is the embodiment of 'host tracker only'."

- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### RISK-2: Host layer / board layer collision (LOW)"
  why: "States the no-collision reasoning: board layers 0-31, host layers >= 224, LAYER_UNSET=255
        sentinel for both, and 'They are different variables tracking different QMK layers.'
        Mitigation: 'Document the >= 224 reservation in code comments. The set_host_layer function
        does NOT validate the layer range — it trusts the host.'"
  critical: "Do NOT add a range check (e.g. 'if (layer < HOST_LAYER_BASE) return;'). RISK-2 is
        explicit: set_host_layer trusts the host. A range check would reject legitimate clears
        (0xFF) and deviate from the contract. The >= 224 reservation is a HOST-side convention."

# Constants reference
- file: PRD.md
  section: "## 16. Appendix B — Constants Reference"
  why: "LAYER_UNSET = 255 (notifier.c); 'Host layer block >= 224 (§4.6, §14): host layers resolve
        above board layers (255 = LAYER_UNSET).' Confirms the values set_host_layer deals with."
  critical: "LAYER_UNSET (255) is ALREADY #defined in notifier.c:126 — reuse it; do NOT redefine
        and do NOT hardcode 0xFF in the comparison. Use the macro name (matches activate_layer)."

# Dependency PRPs — what exists when this task starts (CONTRACTS)
- file: plan/003_16d737de7a3e/P1M1T2S1/PRP.md
  why: "LANDED: notifier.c:126 #define LAYER_UNSET 255; notifier.c:127 activated_layer=LAYER_UNSET
        (BOARD); notifier.c:143 static uint8_t host_layer = LAYER_UNSET (HOST — the variable this
        task mutates). Also host_cb_enabled/has_been_queried (untouched by this task)."
  critical: "host_layer exists, init to LAYER_UNSET, and currently warns 'defined but not used'.
        This task makes it used (set_host_layer reads+writes it) so that warning DISAPPEARS. Do NOT
        redeclare host_layer or change its initializer."

- file: plan/003_16d737de7a3e/P1M1T1S1/PRP.md
  why: "LANDED (notifier.h): HOST_LAYER_BASE (224), HOST_CALLBACK_MAX, host_callback_t. This task
        references HOST_LAYER_BASE only in the Mode-A COMMENT (not in code logic — set_host_layer
        does not validate against it)."
  critical: "HOST_LAYER_BASE is for documentation, not a runtime guard. Do NOT add 'if (layer >=
        HOST_LAYER_BASE)' — that would reject the 0xFF clear and deviate from RISK-2."

- file: plan/003_16d737de7a3e/P1M2T1S1/PRP.md   (parallel — being implemented)
  why: "P1.M2.T1.S1 adds the typed_mode flag + 0xF0 discriminator routing fork in hid_notify
        (~L525-620) + a handle_typed_command stub. Its edit region is DISJOINT from this task's
        (§8.4 ~L218-237). Both can land independently."
  critical: "Do NOT edit hid_notify, typed_mode, or handle_typed_command — those are S1's scope.
        This task's only insertion is set_host_layer in §8.4. grep confirms zero overlap."

# The pattern to mirror (the file being modified)
- file: notifier.c
  section: "§8.4 Layer & command state machines (activate_layer / deactivate_layer, L218-235)"
  why: "activate_layer/deactivate_layer are the EXACT template set_host_layer mirrors: wrap QMK
        layer_on/layer_off, guard before layer_off, assign the tracker last, optional #ifdef
        CONSOLE_ENABLE uprintf logging."
  pattern: "void activate_layer(uint8_t){ layer_on(l); activated_layer=l; } + void deactivate_layer(void){ if(activated_layer==LAYER_UNSET) return; layer_off(activated_layer); activated_layer=LAYER_UNSET; }"
  gotcha: "activate_layer/deactivate_layer are NON-static (called from notifier.c process_full_message
        AND potentially a keymap). set_host_layer is STATIC — its only caller is the APPLY_HOST_CONTEXT
        handler in this same file (P1.M2.T2.S3). Do NOT make it non-static."

# Stub semantics — matter for the test
- file: qmk_stubs/qmk_stubs.c
  why: "layer_on(L) sets g_active_layer=L; layer_off(L) IGNORES its arg and resets g_active_layer=255.
        stub_get_active_layer() exposes g_active_layer. The stub models a SINGLE active layer (no
        stacking), so g_active_layer is NOT a board-vs-host discriminator — the test asserts the two
        static globals (host_layer, activated_layer) directly via #include notifier.c."
  critical: "The stub layer_off ignores its argument — that's fine for testing set_host_layer's LOGIC
        (we assert host_layer and the call sequence, not QMK's real stacking). Do NOT 'fix' the stub."

# Regression targets (must stay green)
- file: test_notifier_dispatch.c
  why: "Legacy reassembly/F4/F5/F6 ack — 14 cases, all printable data[2]. set_host_layer is never
        called by the legacy path. MUST stay 14/14."
- file: test_notifier_os.c
  why: "Multi-OS F8/F9 — 31 cases, all printable data[2], never call set_host_layer. MUST stay 31/31."

# Build/test gate
- file: run_notifier_stub_tests.sh
  why: "Object-compiles notifier.c with -Wall -Wextra (NOT -Werror), links BOTH notifier drivers,
        runs them, asserts 0 FAIL. The 4 expected -Wunused warnings are non-fatal. Must print
        '✓ notifier stub-compile gate PASSED'."
```

### Current Codebase tree (relevant slice — post-P1.M1.T2.S1 + P1.M2.T1.S1 state)

```bash
notifier.c                # ← MODIFY (1 insertion: set_host_layer after deactivate_layer L235).
                          # Current state: S1 host globals (L143-145) + board_rules_present (L204)
                          # + activate_layer/deactivate_layer (L218-235) + typed_mode fork (L525-620).
notifier.h                # LANDED: HOST_LAYER_BASE/HOST_CALLBACK_MAX/host_callback_t/typed-cmd consts. DO NOT TOUCH.
pattern_match.{c,h}       # unaffected. DO NOT TOUCH.
qmk_stubs/                # os_detection.h, print.h (uprintf=printf), qmk_stubs.c (layer_on/off/raw_hid_send). DO NOT TOUCH.
test_notifier_dispatch.c  # regression (14 cases). DO NOT TOUCH.
test_notifier_os.c        # regression (31 cases). DO NOT TOUCH.
run_notifier_stub_tests.sh# gate. DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
notifier.c                # MODIFIED: +set_host_layer() (§8.4 region, between deactivate_layer and enable_command).
# (no new files; no header change)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — set_host_layer IS expected to warn 'defined but not used' until P1.M2.T2.S3
//   (VERIFIED during research). It is a static function whose only caller is the
//   APPLY_HOST_CONTEXT handler (not yet implemented). -Wunused-function fires under
//   -Wall -Wextra (exit stays 0 — the gate is NOT -Werror). This mirrors the S1
//   convention: ACCEPT the warning; do NOT suppress it with __attribute__((unused))
//   (not this codebase's idiom). It self-resolves the moment APPLY_HOST_CONTEXT calls it.
//   After THIS task the permitted-warning set is EXACTLY four:
//     * 'set_host_layer defined but not used [-Wunused-function]'     (NEW this task -> P1.M2.T2.S3)
//     * 'board_rules_present defined but not used [-Wunused-function]' (carried S1 -> P1.M2.T2.S1)
//     * 'has_been_queried defined but not used [-Wunused-variable]'    (carried S1 -> P1.M2.T2.S1)
//     * 'host_cb_enabled defined but not used [-Wunused-variable]'     (carried S1 -> P1.M2.T1.S3)
//   AND 'host_layer defined but not used' is GONE (set_host_layer now reads+writes it).
//   If host_layer STILL warns, set_host_layer isn't referencing it. If any OTHER warning
//   appears, investigate.

// CRITICAL — touch ONLY host_layer, NEVER activated_layer (invariant 21 / §14).
//   activated_layer (L127) is the BOARD tracker, mutated only by activate_layer/
//   deactivate_layer (the legacy string path). host_layer (L143) is the HOST tracker,
//   mutated only by set_host_layer (the typed path). VERIFIED by the orthogonality test:
//   activate_layer(5) then set_host_layer(231) leaves activated_layer==5. Do NOT add any
//   line in set_host_layer that reads or writes activated_layer.

// CRITICAL — NO range validation (RISK-2). set_host_layer MUST NOT check 'layer >=
//   HOST_LAYER_BASE' or 'layer < 224'. The item contract (b) says "expected >= 224" —
//   "expected" means the HOST respects the reservation, not that the firmware enforces it.
//   RISK-2 is explicit: "The set_host_layer function does NOT validate the layer range —
//   it trusts the host." A range check would also reject the 0xFF clear path. HOST_LAYER_BASE
//   appears ONLY in the Mode-A comment, never in code logic.

// GOTCHA — use the LAYER_UNSET macro, not the literal 0xFF/255. activate_layer/
//   deactivate_layer both compare against LAYER_UNSET (notifier.c:126). Match that idiom:
//   'if (layer == LAYER_UNSET)'. Hardcoding 0xFF couples to the literal and diverges from
//   the siblings.

// GOTCHA — keep the two-branch (a)/(b) structure. The item contract specifies them as
//   distinct cases (clear vs set). Do NOT factor out the common
//   'if (host_layer != LAYER_UNSET) layer_off(host_layer);' guard into a single pre-check —
//   the two-branch form mirrors the spec exactly and is more readable. (Both forms are
//   behavior-identical; spec fidelity + readability win.)

// GOTCHA — placement is BETWEEN deactivate_layer (L235) and enable_command (L237), i.e.
//   the §8.4 state-machine region stays contiguous (board layer fns + host layer fn +
//   command fns grouped). Do NOT place set_host_layer near hid_notify or the typed fork
//   (that's P1.M2.T1.S1's region) — it belongs with its board siblings.

// GOTCHA — set_host_layer is STATIC. Its only caller (APPLY_HOST_CONTEXT, P1.M2.T2.S3)
//   is in this same file. activate_layer/deactivate_layer are NON-static (called from
//   process_full_message + possibly a keymap) — do NOT copy that; set_host_layer must not
//   leak into the global namespace. (A non-static set_host_layer would also not warn
//   'unused', masking the S2->S3 boundary — keep it static so the warning correctly signals
//   the pending consumer.)

// GOTCHA — uprintf under #ifdef CONSOLE_ENABLE is safe. qmk_stubs/print.h:19 defines
//   uprintf(...) as printf(...); CONSOLE_ENABLE is NOT defined in the stub build, so any
//   #ifdef CONSOLE_ENABLE uprintf(...) block is compiled out. activate_layer (L218) already
//   uses this pattern and stub-compiles clean. You MAY mirror the logging for visual
//   consistency with activate_layer, or omit it (the item contract doesn't require logging).
//   If you add it, keep messages terse ('Setting host layer %d\n' / 'Clearing host layer\n').

// GOTCHA — the stub layer_off IGNORES its argument (qmk_stubs.c:12-16: always resets
//   g_active_layer=255). So the test cannot rely on g_active_layer to distinguish board vs
//   host. Assert the two static globals (host_layer, activated_layer) directly via
//   #include notifier.c. The stub's g_active_layer is still useful to confirm layer_on ran
//   (it takes the layer's value).

// GOTCHA — do NOT touch the typed_mode fork / handle_typed_command / hid_notify. Those are
//   P1.M2.T1.S1's scope (parallel, disjoint region ~L525-620). This task's single insertion
//   is in §8.4 (~L236). grep confirms zero overlap.
```

## Implementation Blueprint

### Data models and structure

No new types, no new globals, no new includes. This task consumes:
- `host_layer` (notifier.c:143, static uint8_t, init `LAYER_UNSET`) — the variable mutated.
- `LAYER_UNSET` (notifier.c:126, `#define ... 255`) — the sentinel.
- QMK `layer_on(uint8_t)` / `layer_off(uint8_t)` — provided by QMK (action_layer.c) in
  production, by `qmk_stubs/qmk_stubs.c` in tests.

And adds one `static void set_host_layer(uint8_t layer)` function.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — INSERT set_host_layer() between deactivate_layer and enable_command
  - LOCATE the anchor: the close brace of deactivate_layer (notifier.c:235), immediately
    followed by a blank line and then 'void enable_command(command_map_t *command) {' (L237).
  - INSERT (between deactivate_layer's '}' and enable_command) the Mode-A block comment +
    the function (see "The exact code to write").
  - SIGNATURE: static void set_host_layer(uint8_t layer)
  - BODY: the two-branch (a)/(b) logic verbatim (LAYER_UNSET clear; else layer_off(old)+layer_on(new)).
  - NAMING: set_host_layer (static, snake_case). Param 'layer' (snake_case, matches activate_layer).
  - DEPENDENCIES: host_layer (L143), LAYER_UNSET (L126), layer_on/layer_off (QMK/stub). All present.
  - PRESERVE: activate_layer/deactivate_layer (L218-235) untouched; enable_command+ (L237+) untouched;
    the S1 host globals (L143-145) untouched; the typed_mode fork (L525-620) untouched.
  - DO NOT: make it non-static; add a range check; touch activated_layer; suppress the unused
    warning; add #includes; hardcode 0xFF.

Task 2: VERIFY (no edit) — compile + regression + behavior + orthogonality
  - Run Validation Level 1 (stub-compile; exit 0; exactly 4 warnings; host_layer gone).
  - Run Validation Level 2 (#include harness: set/change/clear/re-clear/orthogonality).
  - Run Validation Level 3 (dispatch 14/14 + os 31/31, 0 FAIL; runner PASSED).
  - Run Level 4 (Mode-A doc anchors; diff confined to notifier.c).
```

**The exact code to write** — insert between `deactivate_layer()`'s closing `}`
(notifier.c:235) and `void enable_command(command_map_t *command) {` (notifier.c:237):

```c
/* set_host_layer (§14) — Host layer tracker ONLY. Operates on host_layer,
 * independent of the board activated_layer (architecture "two independent state
 * planes" / invariant 21). It wraps QMK layer_on/layer_off exactly as the board
 * activate_layer/deactivate_layer do, but for the HOST tracker:
 *   - layer == LAYER_UNSET (0xFF): clear the host layer (turn it off if one was
 *     set, then mark host_layer unset).
 *   - otherwise (a real host layer, reserved >= HOST_LAYER_BASE 224 per §14/§4.6
 *     so it resolves above board layers under QMK's highest-layer-wins rule):
 *     turn off the previous host layer first, then layer_on the new one.
 * This function touches ONLY host_layer — it NEVER reads or writes the board
 * activated_layer, and it does NOT check or clear board state (that is clear_board's
 * job in the APPLY_HOST_CONTEXT handler, P1.M2.T2.S3, which calls this). Per RISK-2
 * there is NO layer-range validation here: the >= 224 reservation is a host-side
 * convention the firmware trusts (no collision because host_layer and
 * activated_layer are distinct variables tracking distinct layer ranges). */
static void set_host_layer(uint8_t layer) {
    if (layer == LAYER_UNSET) {                 /* (a) clear the host layer */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);
        }
        host_layer = LAYER_UNSET;
    } else {                                    /* (b) set a real host layer (>= 224) */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);              /* turn off the old host layer first */
        }
        layer_on(layer);
        host_layer = layer;
    }
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: mirror the board activate_layer/deactivate_layer wrapper. Both wrap QMK
//   layer_on/layer_off, guard before layer_off, and assign the tracker last. set_host_layer
//   does the same for host_layer. Same mechanism, different tracker.

// PATTERN: LAYER_UNSET sentinel guard before layer_off. deactivate_layer does
//   'if (activated_layer == LAYER_UNSET) return; layer_off(activated_layer);'. set_host_layer
//   does 'if (host_layer != LAYER_UNSET) layer_off(host_layer);' — same guard, inlined into
//   both branches so the clear path (a) and set path (b) both skip a redundant layer_off
//   when no host layer is currently set.

// PATTERN: use the macro, not the literal. 'layer == LAYER_UNSET' (not 'layer == 0xFF'),
//   matching activate_layer/deactivate_layer. A future change to the sentinel value then
//   propagates automatically.

// ANTI-PATTERN: do NOT touch activated_layer. set_host_layer mutates host_layer ONLY.
//   Any reference to activated_layer here violates invariant 21 and would couple the two
//   planes. (VERIFIED absent by the Level-4 grep.)

// ANTI-PATTERN: do NOT validate the layer range. RISK-2: "does NOT validate the layer range —
//   it trusts the host." A 'if (layer < HOST_LAYER_BASE) return;' guard would (1) deviate from
//   the contract, (2) reject the 0xFF clear if written as 'if (layer != LAYER_UNSET && layer <
//   HOST_LAYER_BASE)', and (3) be dead code in practice (the host respects the reservation).

// ANTI-PATTERN: do NOT make set_host_layer non-static. Its only caller is APPLY_HOST_CONTEXT
//   (P1.M2.T2.S3) in THIS file. A non-static function wouldn't warn 'unused', hiding the S2->S3
//   boundary, and would leak an internal symbol into the keymap namespace.

// ANTI-PATTERN: do NOT suppress the set_host_layer unused-function warning. Accept it; it
//   self-resolves in P1.M2.T2.S3 when APPLY_HOST_CONTEXT calls set_host_layer. (Same convention
//   S1 used for host_layer/host_cb_enabled/has_been_queried/board_rules_present.)

// ANTI-PATTERN: do NOT edit hid_notify / typed_mode / handle_typed_command (P1.M2.T1.S1's
//   region). This task's single insertion is in §8.4. grep confirms zero overlap.

// ANTI-PATTERN: do NOT factor the two branches into one (e.g. a single pre-branch
//   'if (host_layer != LAYER_UNSET) layer_off(host_layer);' then 'if (layer == LAYER_UNSET)
//   host_layer = LAYER_UNSET; else { layer_on(layer); host_layer = layer; }'). Both forms are
//   behavior-identical, but the two-branch (a)/(b) form mirrors the item contract verbatim and
//   is more readable. Keep the spec's structure.
```

### Integration Points

```yaml
§8.4 STATE MACHINES (notifier.c):
  - insert: set_host_layer (static) between deactivate_layer (L235) and enable_command (L237)
  - siblings: activate_layer, deactivate_layer (board, non-static); enable_command, disable_command
CONSUMERS (downstream, NOT this task):
  - APPLY_HOST_CONTEXT handler (P1.M2.T2.S3): calls set_host_layer(layer) after optional clear_board,
    before apply_host_callbacks(ids, count).
GLOBALS (read+written by set_host_layer):
  - host_layer (L143): the host tracker. (Was unused->warning; now used->warning gone.)
QMK SYMBOLS (called):
  - layer_on(uint8_t), layer_off(uint8_t) — production: QMK action_layer.c; tests: qmk_stubs.c
BUILD/CONFIG/ROUTES/DATABASE:
  - none. No rules.mk, no wire change, no header change, no runtime config.
```

## Validation Loop

> Toolchain: gcc (C project; no ruff/mypy/pytest). notifier.c is stub-compiled via
> `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.` (the exact command
> `run_notifier_stub_tests.sh` uses). All commands below were **executed during
> research against a temp notifier.c carrying the set_host_layer insertion and PASSED**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c. Expect exit 0 AND EXACTLY FOUR -Wunused warnings.
#     CRITICAL: 'host_layer' must NO LONGER warn (set_host_layer now uses it);
#     'set_host_layer' must be the NEW warning (self-resolves P1.M2.T2.S3).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
echo "compile exit=$?  (expect 0)"
echo "-- warnings (expect EXACTLY these 4; host_layer ABSENT) --"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep 'warning:' | sed 's/^[^:]*:notifier.c://'
# Expected: exactly 4 lines — set_host_layer, board_rules_present (-Wunused-function);
#           has_been_queried, host_cb_enabled (-Wunused-variable). NO 'host_layer' line.
# FAIL if: exit != 0, OR a 5th warning appears, OR 'host_layer defined but not used' is present.

# 1b. Confirm set_host_layer landed at the right anchor with the exact signature.
grep -nE 'static void set_host_layer\(uint8_t layer\)' notifier.c
# Expected: exactly ONE match, at a line between deactivate_layer (L235) and enable_command (L237).

# 1c. Confirm it uses LAYER_UNSET (not 0xFF) and references host_layer (not activated_layer).
grep -nE 'if \(layer == LAYER_UNSET\)' notifier.c
grep -nE 'layer_off\(host_layer\)|layer_on\(layer\)|host_layer = ' notifier.c | head
# Expected: the LAYER_UNSET compare + the host_layer mutations. NO 'activated_layer' inside set_host_layer.

rm -f /tmp/notifier_stub.o
```

### Level 2: Component Validation (behavior + orthogonality — THE PRIMARY GATE)

This harness was **verified against a temp notifier.c** during research (all 16
assertions pass, including the load-bearing orthogonality case). It reaches the
static `host_layer` global + `set_host_layer` + the board `activate_layer`/
`deactivate_layer` by `#include`-ing the `.c`.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/set_host_layer_test.c <<'EOF'
/* Reach the static host_layer global + set_host_layer + board fns via #include. */
#include "notifier.c"
#include <stdio.h>

extern uint8_t activated_layer;                 /* board tracker (file-scope in notifier.c) */
extern uint8_t stub_get_active_layer(void);     /* qmk_stubs observable (last layer_on) */

static int fails = 0;
#define CK(cond) do{ if(!(cond)){printf("FAIL line %d: %s\n",__LINE__,#cond); fails++;} \
                     else printf("ok   %s\n",#cond);}while(0)

int main(void) {
    /* 1. Initial state: both trackers unset. */
    CK(host_layer == LAYER_UNSET);
    CK(activated_layer == LAYER_UNSET);

    /* 2. Set a host layer (230) from unset: layer_on(230); host_layer=230. */
    set_host_layer(230);
    CK(host_layer == 230);
    CK(stub_get_active_layer() == 230);         /* layer_on ran */

    /* 3. Change host layer (230 -> 240): layer_off(230) then layer_on(240). */
    set_host_layer(240);
    CK(host_layer == 240);
    CK(stub_get_active_layer() == 240);

    /* 4. Clear host layer (LAYER_UNSET): layer_off(240); host_layer=LAYER_UNSET. */
    set_host_layer(LAYER_UNSET);
    CK(host_layer == LAYER_UNSET);
    CK(stub_get_active_layer() == 255);         /* stub layer_off resets to 255 */

    /* 5. Clear when already unset: guarded no-op (no layer_off, host_layer stays unset). */
    set_host_layer(LAYER_UNSET);
    CK(host_layer == LAYER_UNSET);

    /* 6. ORTHOGONALITY (invariant 21) — the load-bearing case:
     *    set_host_layer must NEVER touch the board activated_layer. */
    activate_layer(5);                          /* board: activated_layer=5 */
    CK(activated_layer == 5);
    set_host_layer(231);                        /* host: must NOT touch activated_layer */
    CK(activated_layer == 5);                   /* board tracker UNCHANGED */
    CK(host_layer == 231);                      /* host tracker changed */
    set_host_layer(LAYER_UNSET);                /* host clear */
    CK(activated_layer == 5);                   /* board STILL untouched after host clear */
    deactivate_layer();                         /* board teardown */
    CK(activated_layer == LAYER_UNSET);
    CK(host_layer == LAYER_UNSET);

    /* 7. RISK-2: no range validation. set_host_layer trusts the host; a board-range
     *    layer (5) is accepted as-is (no HOST_LAYER_BASE check). */
    set_host_layer(5);
    CK(host_layer == 5);
    set_host_layer(LAYER_UNSET);

    printf("\n%s (%d failures)\n", fails ? "SOME FAILURES" : "ALL CASES CONFIRMED", fails);
    return fails ? 1 : 0;
}
EOF

gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    /tmp/set_host_layer_test.c qmk_stubs/qmk_stubs.c -o /tmp/set_host_layer_test 2>&1 \
  | grep -vE 'defined but not used'
/tmp/set_host_layer_test
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
# (The only permitted compiler warnings are the 4 expected -Wunused ones, filtered above.
#  CRITICAL gate: case 6 'activated_layer == 5' after set_host_layer(231) — proves orthogonality.)

rm -f /tmp/set_host_layer_test.c /tmp/set_host_layer_test
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
#     or calls set_host_layer, so behavior is byte-identical (the function is inert
#     for them; it's only reached via APPLY_HOST_CONTEXT in P1.M2.T2.S3).
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
grep -q 'Host layer tracker ONLY' notifier.c && echo "tracker-only anchor ok"
grep -q 'independent of the board activated_layer' notifier.c && echo "independence anchor ok"
grep -q '0xFF' notifier.c && echo "0xFF-clear doc ok"
grep -qE 'HOST_LAYER_BASE|>= 224|reserved.*224' notifier.c && echo "host-layer-base doc ok"
grep -q '§14' notifier.c && echo "§14 anchor ok"
grep -q 'RISK-2' notifier.c && echo "RISK-2 no-validation doc ok"

# 4b. Orthogonality at the source level: set_host_layer must NOT reference activated_layer.
awk '/static void set_host_layer\(uint8_t layer\)/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'activated_layer' \
  && { echo "FAIL: set_host_layer references activated_layer (invariant 21 violation)"; exit 1; } \
  || echo "set_host_layer is host_layer-only (good)"

# 4c. No range validation (RISK-2): set_host_layer must NOT compare against HOST_LAYER_BASE.
awk '/static void set_host_layer\(uint8_t layer\)/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'HOST_LAYER_BASE|layer\s*<\s*224|layer\s*>=\s*224' \
  && { echo "FAIL: set_host_layer validates layer range (RISK-2 violation)"; exit 1; } \
  || echo "no range validation (good — trusts host)"

# 4d. Uses the macro, not the literal.
awk '/static void set_host_layer\(uint8_t layer\)/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -qE 'layer == 0xFF|layer == 255' \
  && { echo "FAIL: hardcoded 0xFF/255 instead of LAYER_UNSET"; exit 1; } \
  || echo "uses LAYER_UNSET macro (good)"

# 4e. Diff hygiene: only notifier.c changed (besides plan/ artifacts).
git status --porcelain | grep -vE 'plan/003_16d737de7a3e/P1M2T1S2/' | grep -E '\.c|\.h|\.sh|\.mk' \
  && { echo "FAIL: source file other than notifier.c changed"; exit 1; } || echo "OK: only notifier.c (+ plan/)"
git diff --stat -- notifier.c
# Expected: only notifier.c listed; one hunk in the §8.4 region (~L236).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: notifier.c stub-compiles (exit 0); **exactly four** -Wunused warnings
      (`set_host_layer`, `board_rules_present`, `has_been_queried`, `host_cb_enabled`);
      **`host_layer` no longer warns**; no other warnings.
- [ ] Level 1: `set_host_layer` present at the §8.4 anchor (between deactivate_layer and enable_command).
- [ ] Level 2: `/tmp/set_host_layer_test` prints "ALL CASES CONFIRMED (0 failures)",
      including **case 6 orthogonality** (`activated_layer == 5` unchanged after `set_host_layer(231)`).
- [ ] Level 3: `run_notifier_stub_tests.sh` PASSED; dispatch 14/14 + os 31/31, 0 FAIL.
- [ ] Level 4: all Mode-A doc anchors present; set_host_layer is host_layer-only (no
      `activated_layer` ref); no range validation; uses `LAYER_UNSET` macro; only notifier.c changed.

### Feature Validation

- [ ] `layer == LAYER_UNSET` (0xFF) clears: `layer_off(host_layer)` if set, then `host_layer = LAYER_UNSET`.
- [ ] real layer sets: `layer_off(old)` if set, then `layer_on(layer)`, then `host_layer = layer`.
- [ ] re-clear when already unset is a guarded no-op (no spurious `layer_off`).
- [ ] set_host_layer NEVER reads/writes `activated_layer` (invariant 21) — verified by test + grep.
- [ ] NO layer-range validation (RISK-2) — `set_host_layer(5)` is accepted.

### Code Quality Validation

- [ ] `static void set_host_layer(uint8_t layer)` — file-local, snake_case, matches the board fns' style.
- [ ] Mirrors `activate_layer`/`deactivate_layer` (QMK layer_on/off wrapper, guard before layer_off, tracker assign last).
- [ ] Uses `LAYER_UNSET` macro (not literal 0xFF/255).
- [ ] Inserted in §8.4 (contiguous with board layer fns); no restyle of existing code.
- [ ] No `__attribute__((unused))` suppression; no new #includes; no header change.

### Documentation & Deployment

- [ ] Mode-A block comment cites §14, invariant 21, `HOST_LAYER_BASE` (224), and RISK-2 (no validation).
- [ ] Comment states: "Host layer tracker ONLY — operates on host_layer, independent of board activated_layer."
- [ ] Consumer boundary clear: APPLY_HOST_CONTEXT handler (P1.M2.T2.S3) is the sole caller; apply_host_callbacks (P1.M2.T1.S3) is a separate sibling.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't touch `activated_layer` — set_host_layer mutates `host_layer` ONLY (invariant 21). Any reference to the board tracker here couples the two planes.
- ❌ Don't validate the layer range — RISK-2: "does NOT validate the layer range — it trusts the host." No `if (layer >= HOST_LAYER_BASE)` / `if (layer < 224)`; `HOST_LAYER_BASE` appears ONLY in the comment.
- ❌ Don't hardcode `0xFF`/`255` — use the `LAYER_UNSET` macro (matches `activate_layer`/`deactivate_layer`).
- ❌ Don't make `set_host_layer` non-static — its only caller (`APPLY_HOST_CONTEXT`, P1.M2.T2.S3) is in this file; non-static would leak the symbol and hide the S2→S3 boundary warning.
- ❌ Don't suppress the `set_host_layer` unused-function warning — accept it (self-resolves in P1.M2.T2.S3); same convention S1 used for the 4 host-state symbols.
- ❌ Don't factor the two (a)/(b) branches into a single pre-check — keep the spec's two-branch structure for fidelity + readability (both forms are behavior-identical).
- ❌ Don't add board-state clearing — `clear_board` (deactivate board layer + disable board command) is the `APPLY_HOST_CONTEXT` handler's job, NOT `set_host_layer`'s. This function is layer-tracker-only.
- ❌ Don't edit `hid_notify` / `typed_mode` / `handle_typed_command` — that's P1.M2.T1.S1's region (~L525-620). This task's insertion is in §8.4 (~L236). Disjoint.
- ❌ Don't touch the S1 host globals (`host_layer`/`host_cb_enabled`/`has_been_queried`) beyond referencing `host_layer` — don't redeclare or re-init them.
- ❌ Don't touch `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_notifier_*`, `run_*.sh`, `PRD.md`, `tasks.json`, `prd_snapshot.md`, `rules.mk`, or `.gitignore`. Only `notifier.c` changes.

---

## Confidence Score: 10/10

The deliverable is one surgical insertion into `notifier.c` — `static void
set_host_layer(uint8_t layer)` placed between `deactivate_layer` (L235) and
`enable_command` (L237), with a Mode-A block comment — specified verbatim above and
**empirically validated during research** by inserting it into a /tmp copy via a
python surgical replace: stub-compiles exit 0 with **exactly four** -Wunused
warnings (the `host_layer` warning **gone** — now used; `set_host_layer` **new** —
self-resolves in P1.M2.T2.S3); a `#include` harness passes all **16 assertions**
including the load-bearing **orthogonality case** (`activate_layer(5)` then
`set_host_layer(231)` leaves `activated_layer == 5`, `host_layer == 231`); and both
regression suites pass unchanged (**dispatch 14/14, os 31/31, 0 FAIL**; "✓ notifier
stub-compile gate PASSED"). The dependencies (`host_layer` L143, `LAYER_UNSET` L126,
`HOST_LAYER_BASE` in notifier.h) are LANDED; the parallel P1.M2.T1.S1 (typed_mode
fork) has a **disjoint edit region** (~L525-620 vs §8.4 ~L236); the consumer
(`APPLY_HOST_CONTEXT`, P1.M2.T2.S3) and the sibling (`apply_host_callbacks`,
P1.M2.T1.S3) are later subtasks with clear boundaries. The §14 contract
(host-tracker-only, 0xFF-clear, ≥224 reservation), invariant 21 (two-plane
orthogonality), and RISK-2 (no range validation) are all encoded and tested. No
external dependencies are added (QMK `layer_on`/`layer_off` are stub-provided).