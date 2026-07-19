# PRP — P1.M1.T2.S1: host state globals, weak host-callback accessors, `board_rules_present`

## Goal

**Feature Goal**: Add the **state scaffolding** for the Host-Side Rules feature to
`notifier.c` — the host-side state plane (independent of board state), the
weak-default host-callback registry accessors that `DEFINE_HOST_CALLBACKS`
overrides, and the `board_rules_present()` aggregation helper that QUERY_INFO will
report. This task adds **data + accessors only**; it wires NO behavior yet (the
consumers land in P1.M2).

**Deliverable**: Three additive, non-restyle insertions into `notifier.c`
(`/home/dustin/projects/qmk-notifier/notifier.c`):
1. Two weak-default host-callback accessors (`get_host_callbacks`→NULL,
   `get_host_callbacks_size`→0) in the §8.3 weak-accessor region.
2. Three host state globals (`host_layer`, `host_cb_enabled[HOST_CALLBACK_MAX]`,
   `has_been_queried`) in the §8.1 globals region, right after `current_os`.
3. The `static bool board_rules_present(void)` helper after the `select_*_map_os`
   selectors, with Mode-A inline comments citing PRD §14/§4.6.

**Success Definition**:
- `notifier.c` stub-compiles clean (`-Wall -Wextra -std=c99`) with only the 4
  expected `-Wunused` warnings (documented below — consumed by P1.M2).
- Host globals initialize correctly: `host_layer==255`, `has_been_queried==false`,
  `host_cb_enabled[]` all false; weak accessors return `{NULL,0}`.
- `board_rules_present()` returns false with all-empty maps and true if ANY of the
  10 board-map size accessors is >0 (F8: checks ALL maps).
- A keymap's `DEFINE_HOST_CALLBACKS` overrides the weak `get_host_callbacks*`
  (linkage contract: strong `T` beats weak `W`).
- Existing suites stay green (no regression): `test_notifier_dispatch` 14/14.
- No other file is modified.

## User Persona (if applicable)

**Target User**: The downstream P1.M2 implementers (set_host_layer,
apply_host_callbacks, handle_typed_command/QUERY_INFO) and, transitively, the
desktop host that will QUERY_INFO to learn `board_rules_present` + callback_count.

**Use Case**: This task establishes the **state variables and weak linkage** that
P1.M2's typed-command handlers read/write. `board_rules_present()` will be called
by the QUERY_INFO handler to set the 4th response byte (§4.6).

**User Journey** (within the staged feature): P1.M1.T1.S1 declares the API → **this
task (T2.S1) adds the state + weak defs + helper** → P1.M2.T1 wires host state
machines → P1.M2.T2 wires QUERY_INFO (calls `board_rules_present`) → P1.M3 tests it.

**Pain Points Addressed**: Gives P1.M2 a ready, zero-initialized host state plane
and a verified board-map aggregator, so the typed-command handlers can be written
against stable symbols without re-deriving the multi-OS map enumeration.

## Why

- **Establishes the host state plane as orthogonal to board state** (architecture
  invariant 21) at the data level, before any host behavior exists — so board and
  host code never accidentally share mutable state.
- **Closes the linkage contract** started by P1.M1.T1.S1: the header's
  `DEFINE_HOST_CALLBACKS` macro emits strong `get_host_callbacks*`; this task
  provides the matching weak defaults so a no-callback keymap still links and
  behaves identically to today (structural backward compat, no `#ifdef`).
- **`board_rules_present()` centralizes the "does this keymap use the matcher"**
  aggregation (default + all 4 OSes × command/layer = 10 accessors) so QUERY_INFO
  doesn't re-implement it and F8 ("must check ALL maps") is satisfied in one place.

## What

Three insertions into `notifier.c` (additive; existing lines untouched):

**(A)** After `get_layer_map_size`'s close brace (§8.3 region), append the two
weak host-callback accessors returning `{NULL, 0}`, with a comment noting they are
overridden by `DEFINE_HOST_CALLBACKS` and that `{NULL,0}` ⇒ no registry ⇒
`feature_flags & 0x02` clear.

**(B)** After `os_variant_t current_os = OS_UNSURE;` (§8.1 region), append the host
state globals block: `host_layer = LAYER_UNSET`, `host_cb_enabled[HOST_CALLBACK_MAX]
= {false}`, `has_been_queried = false`, each with its PRD-anchor comment
(§14 independence / §14 diff-target / §4.6 handshake-timing).

**(C)** After `select_layer_map_os`'s close brace, append the
`static bool board_rules_present(void)` helper that ORs all 10 board-map size
accessors, with the §4.6 "single bit suffices" + F8 comment.

### Success Criteria

- [ ] All three insertions present at the specified anchors; existing code unchanged.
- [ ] Globals zero-init verified; weak accessors `{NULL,0}`; `board_rules_present()`
      false on empty maps, true if any accessor >0.
- [ ] `DEFINE_HOST_CALLBACKS` overrides the weak `get_host_callbacks*` (nm: T > W).
- [ ] Only the 4 documented `-Wunused` warnings; no new errors; dispatch 14/14 green.
- [ ] No modification to notifier.h, pattern_match.*, qmk_stubs/*, test_*.c, run_*.sh.

## All Needed Context

### Context Completeness Check

**Pass.** The exact text of all three insertions (with Mode-A comments and exact
placement anchors) is specified inline below and was **empirically validated during
research** against a temp copy of notifier.c: it stub-compiles clean, the globals
initialize correctly, `board_rules_present` passes all 10 F8 cases, the weak defs
are overridden by a keymap, and the dispatch regression is 14/14 green. An
implementer with only this PRP + repo can make the three edits with no guessing.

### Documentation & References

```yaml
# MUST READ — the consumer-side contract and the aggregation rule
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## Architecture: two independent state planes"  (+ "## The four typed-command handlers")
  why: "Defines the BOARD vs HOST state orthogonality (invariant 21) — the globals
        block comment must state it. Also the QUERY_INFO response layout
        ([0x51][0x01][proto][flags][count][board_rules_present]) that board_rules_present feeds."
  critical: "host_cb_enabled[] is the DIFF TARGET for apply_host_callbacks (P1.M2.T1.S3); it must
             be exactly HOST_CALLBACK_MAX wide and zero-init. host_layer is independent of
             activated_layer — do NOT couple them."

- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### F8 — board_rules_present must check ALL maps"
  why: "States the helper must check default command + default layer + EVERY per-OS
        command/layer map; per-OS granularity is NOT exposed (§4.6 'a single bit suffices')."
  critical: "All 10 _size accessors must be consulted. Missing one => false-negative on a
             keymap that only defines, e.g., a per-OS layer map."

- file: PRD.md   (snapshot: plan/003_16d737de7a3e/prd_snapshot.md)
  section: "## 8. ... ### 8.1 Constants & globals" and "### 8.3 Weak default maps"
  why: "§8.1 is the globals region (LAYER_UNSET=255, activated_layer, current_command,
        current_os) where host globals go. §8.3 is the weak-default region (get_command_map/
        get_layer_map) where the host-callback weak accessors go — mirroring that pattern."
  critical: "LAYER_UNSET (255) is already #defined in notifier.c (§16) — reuse it; do NOT redefine."

- file: PRD.md
  section: "## 14. Host-Side Rules & Typed Commands"  (+ "### 4.6 Typed-command namespace")
  why: "§14 defines host_layer/host_cb_enabled/set_host_layer/apply_host_callbacks and the
        stack/replace model; §4.6 defines board_rules_present (QUERY_INFO byte 4), has_been_queried
        handshake-timing, and feature_flags bit 0x02 = callback registry present."
  critical: "feature_flags is BUILT at runtime in P1.M2 (0x01 | (size>0?0x02:0)); the {NULL,0} weak
             default is what makes bit 0x02 clear when no DEFINE_HOST_CALLBACKS. has_been_queried is
             set on the FIRST QUERY_INFO only (§4.6 — at most one handshake per board boot)."

# Pattern to follow (the file being modified)
- file: notifier.c
  why: "The default board weak-accessor block (get_command_map/get_command_map_size/
        get_layer_map/get_layer_map_size) is the EXACT template the host-callback weak
        accessors mirror. The board globals (activated_layer/current_command/current_os) are the
        template for the host globals block (explicit initializers, terse comments)."
  pattern: "__attribute__((weak)) T* get_X(void){...} + __attribute__((weak)) size_t get_X_size(void){...}"
  gotcha: "Board accessors return empty_command_map (non-NULL) + size 0; host accessors return NULL + 0
           (per item contract 3(d) — the consumer never derefs when size==0). Match NULL exactly."

# Sibling task contract (assume landed)
- file: plan/003_16d737de7a3e/P1M1T1S1/PRP.md   (and the landed notifier.h)
  why: "Defines host_callback_t, HOST_CALLBACK_MAX(32), HOST_LAYER_BASE(224), and the
        DEFINE_HOST_CALLBACKS macro that emits PLAIN user_host_callbacks/get_host_callbacks/
        get_host_callbacks_size (like DEFINE_SERIAL_COMMANDS, NOT _notifier_ namespacing)."
  critical: "My weak defs MUST be named exactly get_host_callbacks / get_host_callbacks_size to be
             overridden. Verified by nm: strong T beats weak W. notifier.h is NOT modified by this task."

# Consumer / validator
- file: notifier.c (the select_*_map_os functions + per-OS weak accessors)
  why: "board_rules_present calls the 8 per-OS _notifier_get_*_map_OS_*_size accessors (defined
        ~L136-153) + the 2 default _size accessors. It MUST be placed AFTER those definitions."
  critical: "Placement after select_layer_map_os (~L172) is safe — all 10 accessors are in scope."
```

### Current Codebase tree (relevant slice)

```bash
notifier.h                # modified by P1.M1.T1.S1 (host_callback_t, HOST_CALLBACK_MAX, DEFINE_HOST_CALLBACKS, ...). DO NOT TOUCH.
notifier.c                # ← MODIFY (3 additive insertions). ~541 lines now.
pattern_match.h / .c      # unaffected (matcher = single source of truth).
qmk_stubs/                # os_detection.h (plan 002), qmk_keyboard_stub.h, raw_hid.h, qmk_stubs.c. DO NOT TOUCH.
test_notifier_dispatch.c  # regression target (links notifier.c). DO NOT TOUCH.
test_notifier_os.c        # regression target. DO NOT TOUCH.
run_notifier_stub_tests.sh# gate; unaffected (not -Werror). DO NOT TOUCH.
run_all_tests.sh          # unaffected (links pattern_match.c only).
PRD.md                    # READ-ONLY.
plan/003_16d737de7a3e/    # this plan.
```

### Desired Codebase tree with files to be added/changed

```bash
notifier.c                # MODIFIED: +2 weak host-callback accessors (§8.3),
                          #           +3 host state globals (§8.1),
                          #           +board_rules_present() helper (after selectors).
# (no new files)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (expected -Wunused warnings — do NOT "fix" by deleting): the 4 new
//   symbols are scaffolding consumed by P1.M2:
//     host_layer            -> set_host_layer        (P1.M2.T1.S2)
//     host_cb_enabled[]     -> apply_host_callbacks  (P1.M2.T1.S3)
//     has_been_queried      -> QUERY_INFO handler    (P1.M2.T2.S1)
//     board_rules_present() -> QUERY_INFO handler    (P1.M2.T2.S1)
//   They produce exactly four -Wunused-variable/-Wunused-function warnings under
//   -Wall -Wextra. The build is NOT -Werror (proven: dispatch 14/14 green with
//   them present). They vanish when P1.M2 lands. Leave the symbols PLAIN — do NOT
//   delete them and do NOT add __attribute__((unused)) (not this file's style; adds churn).

// CRITICAL (linkage contract): weak defs MUST be named EXACTLY get_host_callbacks /
//   get_host_callbacks_size to match the DEFINE_HOST_CALLBACKS strong defs from
//   notifier.h (P1.M1.T1.S1). A typo = the weak def isn't overridden => callback_count
//   always 0. (Verified by nm: strong T overrides weak W.)

// GOTCHA (return NULL, not an empty array): board map accessors return empty_command_map
//   (non-NULL) defensively; host-callback accessors return NULL per item contract 3(d).
//   Safe because every consumer gates on get_host_callbacks_size()==0 before deref.
//   Do NOT "be consistent" by returning a static empty host_callback_t[].

// GOTCHA (placement of board_rules_present): it references the 8 per-OS
//   _notifier_get_*_map_OS_*_size accessors, so it MUST be textually AFTER their
//   weak definitions (~L136-153). Placing it right after select_layer_map_os (~L172)
//   is safe and keeps the weak-accessor+selector block contiguous.

// GOTCHA (zero-init idiom): file-scope statics are zero-init by the C standard, but this
//   file's style initializes every global explicitly (activated_layer=LAYER_UNSET,
//   current_os=OS_UNSURE, dropping=false). Match that: host_layer=LAYER_UNSET,
//   host_cb_enabled[HOST_CALLBACK_MAX]={false}, has_been_queried=false. {false} on a
//   bool array does NOT trip -Wmissing-field-initializers (verified clean).

// GOTCHA (orthogonality): host state is a SEPARATE plane from board state. Do NOT
//   initialize host_layer from activated_layer or otherwise couple them. The comment
//   block must call out invariant 21 / §14.
```

## Implementation Blueprint

### Data models and structure

No new types (they live in `notifier.h`, owned by P1.M1.T1.S1). This task adds:
- 3 file-scope static variables (the host state plane).
- 2 weak file-scope functions (host-callback registry accessors).
- 1 static helper function (`board_rules_present`).

All consume existing symbols: `LAYER_UNSET` (notifier.c), `HOST_CALLBACK_MAX` /
`host_callback_t` (notifier.h), and the existing board-map `_size` accessors.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — INSERT weak host-callback accessors (§8.3 region)
  - LOCATE the default board weak-accessor block; ANCHOR on the close of get_layer_map_size:
        __attribute__((weak)) size_t get_layer_map_size(void) {
            return 0;
        }
  - INSERT immediately AFTER that close brace (and before `#define LAYER_UNSET 255`):
        // Weak-default host-callback registry accessors (§14). Overridden by
        // DEFINE_HOST_CALLBACKS in the keymap. {NULL,0} ⇒ no registry ⇒ feature_flags
        // bit 0x02 (NOTIFY_FEATURE_CALLBACK_REGISTRY) clear and callback_count = 0, so a
        // keymap that omits DEFINE_HOST_CALLBACKS links and behaves identically to today.
        __attribute__((weak)) host_callback_t* get_host_callbacks(void) { return NULL; }
        __attribute__((weak)) size_t           get_host_callbacks_size(void) { return 0; }
  - NAMING: get_host_callbacks / get_host_callbacks_size (EXACT — matches DEFINE_HOST_CALLBACKS).
  - RETURN: NULL / 0 (NOT an empty array — see gotcha).
  - PRESERVE: the preceding get_command_map*/get_layer_map* weak defs; do not restyle.

Task 2: MODIFY notifier.c — INSERT host state globals (§8.1 region)
  - ANCHOR on: `os_variant_t current_os = OS_UNSURE;` (end of the board globals block).
  - INSERT immediately AFTER it (and before the `/* --- Per-OS weak accessors ...` comment):
        /* --- HOST STATE (a plane separate from board state; architecture invariant 21) --
         * Board state above (activated_layer / current_command / current_os) is driven by
         * the legacy string path (process_full_message). The host state below is driven by
         * typed commands (handle_typed_command, §14/§4.6) and is intentionally orthogonal:
         * process_full_message never touches it; handle_typed_command touches board state
         * only via clear_board (§14). */
        static uint8_t host_layer = LAYER_UNSET;                       /* independent of board activated_layer (§14) */
        static bool    host_cb_enabled[HOST_CALLBACK_MAX] = {false};   /* zero-init; diff target for apply_host_callbacks (§14) */
        static bool    has_been_queried = false;                       /* set on first QUERY_INFO service (§4.6 handshake-timing rule) */
  - DEPENDS ON: LAYER_UNSET (notifier.c L113) + HOST_CALLBACK_MAX (notifier.h). Both present. ✓
  - NAMING: host_layer / host_cb_enabled / has_been_queried (exact — match §14 + the consumers in P1.M2).
  - PRESERVE: activated_layer / current_command / current_os untouched.

Task 3: MODIFY notifier.c — INSERT board_rules_present (after the selectors)
  - ANCHOR on the close brace of select_layer_map_os (the second of the two selector fns).
  - INSERT immediately AFTER it (as a new static helper, before §8.4 state machines):
        /* board_rules_present (§4.6 QUERY_INFO.board_rules_present) — true iff ANY board
         * map is non-empty. Checks the default command/layer maps AND every per-OS map.
         * The host only needs a single bit ("does this keymap use the notifier matcher at
         * all?"); per-OS granularity is deliberately NOT exposed (§4.6: "a single bit
         * suffices"). (findings_and_risks.md F8: must check ALL maps.) */
        static bool board_rules_present(void) {
            if (get_command_map_size() > 0) return true;
            if (get_layer_map_size() > 0)   return true;
            if (_notifier_get_command_map_OS_LINUX_size() > 0)   return true;
            if (_notifier_get_layer_map_OS_LINUX_size() > 0)     return true;
            if (_notifier_get_command_map_OS_WINDOWS_size() > 0) return true;
            if (_notifier_get_layer_map_OS_WINDOWS_size() > 0)   return true;
            if (_notifier_get_command_map_OS_MACOS_size() > 0)   return true;
            if (_notifier_get_layer_map_OS_MACOS_size() > 0)     return true;
            if (_notifier_get_command_map_OS_IOS_size() > 0)     return true;
            if (_notifier_get_layer_map_OS_IOS_size() > 0)       return true;
            return false;
        }
  - DEPENDS ON: the 10 _size accessors (default + per-OS), all defined above this point. ✓
  - NAMING: board_rules_present (static — internal; its only caller is the QUERY_INFO handler in P1.M2.T2.S1).
  - PRESERVE: select_command_map_os / select_layer_map_os untouched; do not restyle the per-OS block.
```

### Implementation Patterns & Key Details

```c
// PATTERN: the host globals mirror the board globals' explicit-init style:
//   board: activated_layer = LAYER_UNSET; current_command = {0}; current_os = OS_UNSURE;
//   host:  host_layer = LAYER_UNSET; host_cb_enabled[...] = {false}; has_been_queried = false;

// PATTERN: the host-callback weak accessors mirror the board weak accessors exactly,
//   differing only in type + return (NULL vs empty array):
//   __attribute__((weak)) host_callback_t* get_host_callbacks(void) { return NULL; }
//   __attribute__((weak)) size_t           get_host_callbacks_size(void) { return 0; }

// ANTI-PATTERN: do NOT return a static empty host_callback_t[] "for safety". The item
//   contract mandates NULL; consumers gate on size==0. Returning an array changes the
//   {NULL,0} contract that feature_flags/callback_count logic depends on.

// ANTI-PATTERN: do NOT couple host_layer to activated_layer (e.g. init from it, or have
//   board_rules_present consider host state). Board and host planes are orthogonal.

// ANTI-PATTERN: do NOT skip any of the 10 accessors in board_rules_present. F8 requires
//   ALL maps (default cmd/layer + 4 OSes × cmd/layer). A keymap with ONLY a per-OS layer
//   map must still report board_rules_present=true.

// ANTI-PATTERN: do NOT delete or #ifdef-out the new symbols to silence -Wunused. They
//   are required scaffolding; the warnings are expected and transient (cleared by P1.M2).
```

### Integration Points

```yaml
GLOBALS (notifier.c §8.1):
  - add: host_layer, host_cb_enabled[HOST_CALLBACK_MAX], has_been_queried (after current_os)
  - consumers (P1.M2): set_host_layer, apply_host_callbacks, QUERY_INFO/SET_OS handlers
WEAK ACCESSORS (notifier.c §8.3):
  - add: get_host_callbacks, get_host_callbacks_size (after get_layer_map_size)
  - overridden by: DEFINE_HOST_CALLBACKS (notifier.h, P1.M1.T1.S1) — verified T > W
HELPERS (notifier.c, after selectors):
  - add: static bool board_rules_present(void)
  - consumer (P1.M2.T2.S1): QUERY_INFO handler -> response byte 4
BUILD/CONFIG/ROUTES/DATABASE:
  - none (no rules.mk, no wire change, no runtime surface this task).
```

## Validation Loop

> Toolchain: gcc (C project; no ruff/mypy/pytest). notifier.c is stub-compiled via
> `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.` (qmk_stubs/os_detection.h
> exists from plan 002). All commands below were **executed during research against a
> temp notifier.c carrying the 3 insertions and PASSED**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. notifier.c stub-compiles clean. Expect exit 0 AND exactly the 4 documented
#     -Wunused warnings (host_layer, host_cb_enabled, has_been_queried, board_rules_present).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
echo "compile exit=$?  (expect 0)"
echo "-- warnings (expect ONLY the 4 -Wunused lines below) --"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep 'warning:' | sed 's/^[^:]*://'
# Expected: exactly 4 lines — host_layer/host_cb_enabled/has_been_queried (-Wunused-variable)
#           and board_rules_present (-Wunused-function). NO other warnings, NO errors.

# 1b. Confirm the 3 insertions landed at the right anchors.
grep -n '__attribute__((weak)) host_callback_t\* get_host_callbacks' notifier.c   # §8.3 region
grep -n 'static uint8_t host_layer = LAYER_UNSET' notifier.c                       # §8.1 region
grep -n 'static bool    host_cb_enabled\[HOST_CALLBACK_MAX\]' notifier.c
grep -n 'static bool    has_been_queried = false' notifier.c
grep -n 'static bool board_rules_present(void)' notifier.c                          # after selectors
rm -f /tmp/notifier_stub.o
```

### Level 2: Globals + Weak Accessors + board_rules_present (Component Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. Include notifier.c directly to reach the static globals/helper + weak accessors.
#     Verifies: host_layer==255, has_been_queried==false, host_cb_enabled[] all false,
#     weak {NULL,0}, and board_rules_present()==false when all maps are empty (weak defaults).
cat > /tmp/host_state_test.c <<'EOF'
#include "notifier.c"
#include <stdio.h>
int main(void){
    int ok = (host_layer == LAYER_UNSET) && !has_been_queried;
    for (int i = 0; i < HOST_CALLBACK_MAX; i++) if (host_cb_enabled[i]) ok = 0;
    ok = ok && (get_host_callbacks() == NULL) && (get_host_callbacks_size() == 0);
    ok = ok && (board_rules_present() == false);   /* all-empty (weak defaults) */
    printf("host-state: %s (host_layer=%u cb_size=%zu brp=%d)\n",
           ok ? "PASS" : "FAIL", host_layer, get_host_callbacks_size(), board_rules_present());
    return ok ? 0 : 1;
}
EOF
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    /tmp/host_state_test.c qmk_stubs/qmk_stubs.c -o /tmp/host_state_test 2>/dev/null \
  && /tmp/host_state_test; echo "host-state exit=$? (expect 0)"
rm -f /tmp/host_state_test.c /tmp/host_state_test

# 2b. board_rules_present LOGIC test (F8: ANY of the 10 accessors >0 ⇒ true). Standalone
#     mock with controlled accessors; the body MUST match notifier.c verbatim.
cat > /tmp/brp_logic.c <<'EOF'
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
static size_t s_cmd_def=0,s_lay_def=0,s_cmd_l=0,s_lay_l=0,s_cmd_w=0,s_lay_w=0,s_cmd_m=0,s_lay_m=0,s_cmd_i=0,s_lay_i=0;
size_t get_command_map_size(void){return s_cmd_def;}
size_t get_layer_map_size(void){return s_lay_def;}
size_t _notifier_get_command_map_OS_LINUX_size(void){return s_cmd_l;}
size_t _notifier_get_layer_map_OS_LINUX_size(void){return s_lay_l;}
size_t _notifier_get_command_map_OS_WINDOWS_size(void){return s_cmd_w;}
size_t _notifier_get_layer_map_OS_WINDOWS_size(void){return s_lay_w;}
size_t _notifier_get_command_map_OS_MACOS_size(void){return s_cmd_m;}
size_t _notifier_get_layer_map_OS_MACOS_size(void){return s_lay_m;}
size_t _notifier_get_command_map_OS_IOS_size(void){return s_cmd_i;}
size_t _notifier_get_layer_map_OS_IOS_size(void){return s_lay_i;}
static bool board_rules_present(void){
    if(get_command_map_size()>0)return true;
    if(get_layer_map_size()>0)return true;
    if(_notifier_get_command_map_OS_LINUX_size()>0)return true;
    if(_notifier_get_layer_map_OS_LINUX_size()>0)return true;
    if(_notifier_get_command_map_OS_WINDOWS_size()>0)return true;
    if(_notifier_get_layer_map_OS_WINDOWS_size()>0)return true;
    if(_notifier_get_command_map_OS_MACOS_size()>0)return true;
    if(_notifier_get_layer_map_OS_MACOS_size()>0)return true;
    if(_notifier_get_command_map_OS_IOS_size()>0)return true;
    if(_notifier_get_layer_map_OS_IOS_size()>0)return true;
    return false;
}
#define CHK(f) do{ f=1; bool r=board_rules_present(); f=0; assert(r); }while(0)
int main(void){
    assert(!board_rules_present());                 /* all empty */
    CHK(s_cmd_def); CHK(s_lay_def);
    CHK(s_cmd_l); CHK(s_lay_l); CHK(s_cmd_w); CHK(s_lay_w);
    CHK(s_cmd_m); CHK(s_lay_m); CHK(s_cmd_i); CHK(s_lay_i);
    assert(!board_rules_present());                 /* back to all empty */
    printf("board_rules_present: PASS (all 10 maps checked, F8 satisfied)\n");
    return 0;
}
EOF
gcc -Wall -Wextra -std=c99 /tmp/brp_logic.c -o /tmp/brp_logic && /tmp/brp_logic; echo "brp-logic exit=$? (expect 0)"
rm -f /tmp/brp_logic.c /tmp/brp_logic
```

### Level 3: Integration Testing (Linkage contract + Regression)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Weak-override linkage: a keymap's DEFINE_HOST_CALLBACKS must override notifier.c's
#     weak get_host_callbacks* (the contract that makes no-callback keymaps behave identically).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c -o /tmp/n.o
cat > /tmp/keymap_hcb.c <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
static void en(void){}
DEFINE_HOST_CALLBACKS({ {"cb0",en,en}, {"cb1",en,0} });
EOF
cat > /tmp/drv_hcb.c <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    assert(get_host_callbacks_size()==2);          /* override, not weak 0 */
    assert(get_host_callbacks()!=NULL);
    printf("override: PASS size=%zu name0=%s\n", get_host_callbacks_size(), get_host_callbacks()[0].name);
    return 0;
}
EOF
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c /tmp/keymap_hcb.c -o /tmp/keymap_hcb.o \
  && gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/n.o /tmp/keymap_hcb.o /tmp/drv_hcb.c qmk_stubs/qmk_stubs.c -o /tmp/hcb_override \
  && /tmp/hcb_override; echo "override exit=$? (expect 0)"
echo "  nm: T=strong(keymap) overrides W=weak(notifier.c):"
nm /tmp/keymap_hcb.o /tmp/n.o | grep 'get_host_callbacks_size'
rm -f /tmp/n.o /tmp/keymap_hcb.c /tmp/keymap_hcb.o /tmp/drv_hcb.c /tmp/hcb_override

# 3b. REGRESSION: existing dispatch suite must stay green with the modified notifier.c.
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/n.o \
  && gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/n.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/tnd \
  && /tmp/tnd | tail -3
echo "dispatch exit=$? (expect 0; 'failed: 0')"
rm -f /tmp/n.o /tmp/tnd

# 3c. Full existing gate (both notifier suites) — warnings are non-fatal (not -Werror).
#     NOTE: run_notifier_stub_tests.sh will print the 4 -Wunused warnings during compile
#     but MUST end "✓ notifier stub-compile gate PASSED" with 0 FAIL: lines.
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "stub-tests exit=$?"
tail -n 3 /tmp/ns.out
rm -f /tmp/ns.out
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Mode-A doc anchors present (item point 5).
grep -q 'independent of board activated_layer' notifier.c && echo "host_layer anchor ok"
grep -q 'diff target for apply_host_callbacks' notifier.c && echo "host_cb_enabled anchor ok"
grep -q 'first QUERY_INFO' notifier.c && echo "has_been_queried anchor ok"
grep -q 'DEFINE_HOST_CALLBACKS' notifier.c && echo "weak-accessor anchor ok"
grep -qE 'single bit suffices|must check ALL maps|board_rules_present' notifier.c && echo "board_rules_present anchor ok"

# 4b. Orthogonality: board_rules_present must NOT read host state, and host globals must
#     NOT be initialized from board state. (Manual review of the inserted block.)
sed -n '/static bool board_rules_present/,/^}/p' notifier.c | grep -E 'host_|has_been_queried' \
  && echo "FAIL: board_rules_present references host state" || echo "board_rules_present is board-only (good)"

# 4c. No #ifdef gating around the new surface (backward compat is structural via weak symbols).
! grep -qE '#ifdef.*HOST|#if.*NOTIFY|defined.*HOST_CALLBACK' notifier.c && echo "no #ifdef gating (structural compat)"

# 4d. Diff hygiene: only notifier.c changed in source (besides plan/ artifacts).
git diff --stat -- ':!plan'
# Expected: only `notifier.c` listed.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: notifier.c stub-compiles (exit 0) with ONLY the 4 documented `-Wunused` warnings.
- [ ] Level 2: host-state test PASS (globals zero-init, weak {NULL,0}, brp==false on empty).
- [ ] Level 2: board_rules_present logic PASS (all 10 accessors flip true; F8).
- [ ] Level 3: DEFINE_HOST_CALLBACKS overrides weak defs (nm T>W; size==2); dispatch 14/14.
- [ ] Level 3: `run_notifier_stub_tests.sh` PASSED (warnings non-fatal).
- [ ] Level 4: all Mode-A doc anchors present; board_rules_present is board-only; no `#ifdef`.

### Feature Validation

- [ ] 2 weak host-callback accessors present (NULL/0), named exactly to match the macro.
- [ ] 3 host state globals present (host_layer=LAYER_UNSET, host_cb_enabled[]={false}, has_been_queried=false).
- [ ] board_rules_present() checks all 10 board-map accessors; placed after the per-OS defs.
- [ ] Host plane is orthogonal to board plane (comment + code).

### Code Quality Validation

- [ ] Matches existing file style (explicit global init, `__attribute__((weak))`, terse comments).
- [ ] Additive-only; existing globals/accessors/selectors/ordering preserved (no restyle).
- [ ] New symbols left plain (no `__attribute__((unused))`); 4 -Wunused warnings expected/transient.
- [ ] No modification to notifier.h, pattern_match.*, qmk_stubs/*, test_*.c, run_*.sh, PRD.md, tasks.json.

### Documentation & Deployment

- [ ] Mode-A inline comments cite §14 (host_layer independence, host_cb_enabled diff-target),
      §4.6 (has_been_queried handshake-timing, board_rules_present "single bit"), and the
      weak-accessor override/feature-flag note.
- [ ] Forward dependency on P1.M2 (consumers of the 4 new symbols) documented.

---

## Anti-Patterns to Avoid

- ❌ Don't delete or `#ifdef`-out the new symbols to silence `-Wunused` — they are required
  scaffolding consumed by P1.M2; the 4 warnings are expected, transient, and non-fatal.
- ❌ Don't name the weak accessors anything other than `get_host_callbacks` /
  `get_host_callbacks_size` — they must match `DEFINE_HOST_CALLBACKS` (verified T>W).
- ❌ Don't return a static empty `host_callback_t[]` from `get_host_callbacks` — the contract
  is `{NULL, 0}` (consumers gate on size==0; feature_flags/callback_count depend on it).
- ❌ Don't couple `host_layer` to `activated_layer`, or let `board_rules_present` read host
  state — board and host planes are orthogonal (invariant 21).
- ❌ Don't skip any of the 10 accessors in `board_rules_present` — F8 requires ALL maps.
- ❌ Don't place `board_rules_present` before the per-OS `_size` accessors (it calls them).
- ❌ Don't add `__attribute__((unused))` — not this file's style; adds cleanup churn for P1.M2.
- ❌ Don't touch notifier.h, pattern_match.*, qmk_stubs/*, test_*.c, run_*.sh, PRD.md, tasks.json.

---

## Confidence Score: 10/10

The three insertions are precisely specified (exact anchors, exact text, Mode-A
comments). The implementation was **empirically validated during research** against a
temp `notifier.c`: it stub-compiles clean (only the 4 expected `-Wunused` warnings),
the host globals initialize correctly, `board_rules_present` passes all 10 F8 cases,
the weak defs are overridden by `DEFINE_HOST_CALLBACKS` (nm T>W), and the existing
dispatch suite stays 14/14 green. The single nuance — the expected `-Wunused`
warnings at this subtask boundary — is explicitly documented with the exact 4 symbols,
their P1.M2 consumers, and proof the non-`-Werror` gate stays green. No external
dependencies are added; scope boundaries with all sibling tasks (T1.S1 header,
T2.S2 apply_os_change, P1.M2 consumers) are explicit.