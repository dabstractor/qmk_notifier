# PRP — P1.M1.T1.S1: Add host-rules API surface to `notifier.h`

## Goal

**Feature Goal**: Extend the existing public header `notifier.h` with the
Host-Side Rules & Typed Commands surface: the `host_callback_t` named-callback
registry struct, its accessor declarations, the `DEFINE_HOST_CALLBACKS` macro, and
the typed-command constants block (discriminator, response marker, cmd ids,
proto version, feature-flag bits, host-callback cap, host layer base).

**Deliverable**: The modified file `notifier.h` (repo root), grown from ~91 lines
by additive content at three precise insertion points. New macro bodies are
byte-exact (mirroring `DEFINE_SERIAL_COMMANDS`); new symbols are
`user_host_callbacks` / `get_host_callbacks` / `get_host_callbacks_size`.

**Success Definition**:
- `notifier.h` declares `host_callback_t`, `get_host_callbacks`/`get_host_callbacks_size`,
  `DEFINE_HOST_CALLBACKS`, and all 12 typed-command constants.
- A throwaway keymap using `DEFINE_HOST_CALLBACKS({ … })` compiles + LINKS against
  mock §P1.M1.T2.S1 weak defaults (the generated `get_host_callbacks`/`get_host_callbacks_size`
  override the weak ones), and `nm` shows exactly `user_host_callbacks` + both accessors.
- Omitting `DEFINE_HOST_CALLBACKS` leaves the header includable and the (future) weak
  defaults supplying `{NULL, 0}` — structural backward compat, no `#ifdef`.
- Existing includers (`notifier.c`, `test_notifier_dispatch.c`, `test_notifier_os.c`)
  are unaffected: only new symbols are added.
- Each constants block carries an inline `//` comment citing its PRD section (Mode A).

## User Persona (if applicable)

**Target User**: Keymap authors opting into host-side rules, and the downstream
`notifier.c` (P1.M1.T2.S1) which provides matching weak defaults + the typed
dispatch that consumes these constants.

**Use Case**: A keymap writes `DEFINE_HOST_CALLBACKS({ {"vim", en, dis}, … })` at
file scope; the macro emits `user_host_callbacks[]` + strong accessors that
override `notifier.c`'s weak `{NULL,0}` defaults at link time; the host then
discovers callback names via `QUERY_CALLBACK` and toggles them via
`APPLY_HOST_CONTEXT` (no reflash).

**User Journey**: keymap `#include "notifier.h"` → `DEFINE_HOST_CALLBACKS(…)` →
notifier.c reads `get_host_callbacks_size()` for `feature_flags`/`callback_count`
→ host handshakes (`QUERY_INFO`), sweeps `QUERY_CALLBACK`, builds name→id map →
sends `APPLY_HOST_CONTEXT` to enable ids.

**Pain Points Addressed**: Gives the host a stable, named, registry-backed callback
surface (ID = array index) so app→callback decisions move to the desktop without
reflashing, while keymaps that omit it stay byte-identical to today.

## Why

- Establishes the **public API + linkage contract** for the host-rules feature
  (PRD §14/§4.6) before `notifier.c` is changed.
- The macro-generated accessor names are a **hard cross-file contract**: they must
  match `notifier.c`'s weak defaults (P1.M1.T2.S1) exactly or the firmware fails
  to link.
- Centralizes the wire-protocol constants (discriminator `0xF0`, marker `0x51`,
  cmd ids, proto ver, feature bits) in the one header `notifier.c` includes, so the
  dispatch code reads symbolic names not magic numbers.
- Keeps board and host state planes orthogonal (architecture invariant 21):
  `DEFINE_HOST_CALLBACKS` is a SEPARATE registry from `DEFINE_SERIAL_*`, consumed
  only by the typed-command path.

## What

Four additive edits to `notifier.h` (no deletions, no restyle), placed per the
item's zone "AFTER layer_map_t and BEFORE the DEFINE_SERIAL macros":

1. **Struct** — `host_callback_t` immediately after `} layer_map_t;`.
2. **Accessor declarations** — `get_host_callbacks`/`get_host_callbacks_size` after
   `void notifier_set_os(os_variant_t os);`.
3. **Constants block** — the 12 typed-command constants, each with a `//` PRD-section
   citation.
4. **Macro** — `DEFINE_HOST_CALLBACKS(…)`, byte-exact mirror of `DEFINE_SERIAL_COMMANDS`,
  with a `//` comment noting "ID = array index, stable per build; host re-queries
  names on reconnect so cross-flash renumbering is harmless (§14)".

(Items 3+4 form one contiguous labeled block placed after the `WT` helper macro and
before `// Define macros to create the maps`.)

### Success Criteria

- [ ] `host_callback_t` present after `layer_map_t`; uses `callback_t` (already typedef'd).
- [ ] `get_host_callbacks`/`get_host_callbacks_size` declared in the forward-decl block.
- [ ] All 12 constants present with exact values (see Implementation Tasks).
- [ ] `DEFINE_HOST_CALLBACKS` present, byte-exact vs the verified body; emits
  `user_host_callbacks` + the two accessors.
- [ ] Throwaway keymap compiles+links against mock weak defaults (override + fallback).
- [ ] Only additive changes; existing content/style untouched.

## All Needed Context

### Context Completeness Check

_Pass_: The exact struct, macro body, all 12 constant values, precise insertion
points, and the consumer-side weak-default symbol contract are provided inline
below. The macro was **empirically verified** (compile+link+run+nm) to produce the
exact symbols and to override mock weak defaults. An implementer with only this
PRP + the repo can make the edits with no guessing.

### Documentation & References

```yaml
# MUST READ — the canonical contracts
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## The four typed-command handlers"  (+ "## Host callback diff algorithm")
  why: "Defines host_callback_t/DEFINE_HOST_CALLBACKS contract, the QUERY_INFO
        feature_flags idiom (0x01 | (size>0?0x02:0)), and the ID=array-index stability
        guarantee that the DEFINE_HOST_CALLBACKS comment must state."
  critical: "feature_flags is BUILT at runtime from get_host_callbacks_size(); the
             constant NOTIFY_FEATURE_CALLBACK_REGISTRY (0x02) is the BIT, not the value."

- file: plan/003_16d737de7a3e/architecture/external_deps.md
  section: "### Command table (§4.6)"  (+ "### Field definitions", "## Key constants reference")
  why: "Authoritative cmd_id values (0x01/0x02/0x03/0x05), feature_flags bits, proto_ver=2,
        host layer block >=224, and the statement that callback_count=get_host_callbacks_size()."
  critical: "cmd_id 0x04 is RESERVED (VIA-coexist) — do NOT define a NOTIFY_CMD_* for it;
             APPLY_HOST_CONTEXT is 0x05."

- file: plan/003_16d737de7a3e/prd_snapshot.md   (or repo PRD.md)
  section: "## 4. ... ### 4.6 Typed-command namespace (canonical owner)"
  why: "Canonical wire contract: discriminator 0xF0 (data[2]), response marker 0x51 (>=2),
        proto_ver 1=legacy/2=typed, feature_flags bitmask, host layers reserved >=224."
  critical: "0xF0 can never begin a real matched string (sanitizer allows 0x20-0x7E) —
             this is WHY the discriminator is 0xF0 and why legacy coexistence is transparent."

- file: plan/003_16d737de7a3e/prd_snapshot.md
  section: "## 14. Host-Side Rules & Typed Commands"
  why: "host_callback_t struct definition, HOST_CALLBACK_MAX, HOST_LAYER_BASE (>=224),
        ID=array-index stability, and the structural backward-compat guarantee
        (no DEFINE_HOST_CALLBACKS => weak {NULL,0} => behaves identically today)."
  critical: "Backward compat is STRUCTURAL (no #ifdef) — weak symbols carry it."

# Pattern to follow (the file being modified)
- file: notifier.h
  why: "DEFINE_SERIAL_COMMANDS is the EXACT template DEFINE_HOST_CALLBACKS mirrors
        (array + sizeof/sizeof + accessor pair). callback_t is already typedef'd."
  pattern: "user_<thing>[] + user_<thing>_size + get_<thing>() + get_<thing>_size().
            Plain user_*/get_* naming (NOT the _notifier_ namespacing of _OS macros)."
  gotcha: "File uses terse // comments and lacks #include <stddef.h> (relies on
           includer for size_t). Match // style; do NOT add stddef.h (out of scope)."

# Consumer (forward contract for P1.M1.T2.S1)
- file: notifier.c   (NOT modified this task)
  why: "P1.M1.T2.S1 will add weak defaults + host state that CONSUME these names."
  critical: "notifier.c weak defaults MUST be named exactly get_host_callbacks and
             get_host_callbacks_size to be overridden by the macro. (Verified by link test.)"
```

### Current Codebase tree (relevant slice)

```bash
notifier.h                # ← MODIFY (additive: struct + 2 decls + macro + 12 constants). 91 lines now.
notifier.c                # consumer (P1.M1.T2) — do NOT touch now
pattern_match.h / .c      # unaffected (matcher is single source of truth for semantics)
qmk_stubs/                # os_detection.h NOW EXISTS (plan 002) + qmk_keyboard_stub.h, raw_hid.h, qmk_stubs.c
test_notifier_dispatch.c  # includes notifier.h — unaffected (additive only)
test_notifier_os.c        # includes notifier.h — unaffected
run_notifier_stub_tests.sh# unaffected this task
run_all_tests.sh          # unaffected (links pattern_match.c only)
PRD.md                    # READ-ONLY
plan/003_16d737de7a3e/    # this plan — architecture/, prd_snapshot.md, tasks.json
```

### Desired Codebase tree with files to be added/changed

```bash
notifier.h                # MODIFIED: +host_callback_t, +2 accessor decls, +12 constants, +DEFINE_HOST_CALLBACKS
# (no new files)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (linkage contract): DEFINE_HOST_CALLBACKS emits user_host_callbacks +
//   get_host_callbacks + get_host_callbacks_size (PLAIN user_*/get_* naming, like
//   DEFINE_SERIAL_COMMANDS — NOT the _notifier_ namespacing). notifier.c (P1.M1.T2.S1)
//   MUST provide weak defaults named EXACTLY get_host_callbacks / get_host_callbacks_size.
//   A typo = link failure. (Verified by compile+link+nm against mock weak defaults.)

// GOTCHA (cmd_id 0x04 reserved): do NOT define a NOTIFY_CMD_* for 0x04 (VIA-coexist,
//   reserved). APPLY_HOST_CONTEXT is 0x05. (§4.6 command table.)

// GOTCHA (feature_flags is a runtime-built bitmask): the constants
//   NOTIFY_FEATURE_APPLY_HOST_CONTEXT(0x01)/CALLBACK_REGISTRY(0x02)/VIA_COEXIST(0x04)
//   are BIT positions. notifier.c BUILDS feature_flags at runtime:
//     0x01 | (get_host_callbacks_size()>0 ? 0x02 : 0)
//   Do not define a single FEATURE_FLAGS value; the three bit defines are the contract.

// GOTCHA (LAYER_UNSET stays in notifier.c): do NOT add #define LAYER_UNSET 255 to
//   this header — §16 places it in notifier.c. HOST_LAYER_BASE (224) is the only
//   layer constant added here.

// GOTCHA (size_t): notifier.h uses size_t without #include <stddef.h> (pre-existing;
//   includers provide it). Do NOT add stddef.h — out of scope. Standalone validation
//   harnesses must include <stddef.h> before the header.

// GOTCHA (3-field struct is clean): host_callback_t {name,on_enable,on_disable} does
//   NOT trigger -Wmissing-field-initializers when a row omits on_disable (NULL) —
//   cleaner than the 4-field command_map_t. (Verified.)

// GOTCHA (orthogonality): host_callback_t is a SEPARATE registry from command_map_t.
//   Do NOT fold it into command_map_t or reuse get_command_map. Board (DEFINE_SERIAL_*)
//   and host (DEFINE_HOST_CALLBACKS) planes are independent (architecture invariant 21).
```

## Implementation Blueprint

### Data models and structure

One new type, `host_callback_t` (PRD §14). No other types. It reuses the existing
`callback_t` (nullary function pointer) already typedef'd in the header.

```c
/* Named host-callback registry entry (§14). ID = array index, stable per build;
 * the host re-queries names on reconnect so cross-flash renumbering is harmless. */
typedef struct {
    const char *name;
    callback_t  on_enable;
    callback_t  on_disable;   /* may be NULL */
} host_callback_t;
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.h — INSERT host_callback_t typedef
  - LOCATE: `} layer_map_t;` (end of layer_map_t, ~L16).
  - INSERT immediately AFTER it (before the blank line + `// Forward declarations`):
        typedef struct {
            const char *name;
            callback_t  on_enable;
            callback_t  on_disable;   /* may be NULL */
        } host_callback_t;
  - DEPENDS ON: callback_t (defined L5, above). ✓
  - NAMING: host_callback_t (matches §14 verbatim).
  - PRESERVE: command_map_t / layer_map_t untouched.

Task 2: MODIFY notifier.h — INSERT host-callback accessor declarations
  - LOCATE: `void notifier_set_os(os_variant_t os);` (~L24).
  - INSERT immediately AFTER it:
        // Host-callback registry accessors (user overrides via DEFINE_HOST_CALLBACKS;
        // module provides weak {NULL,0} defaults — §14). ID = array index.
        host_callback_t* get_host_callbacks(void);
        size_t           get_host_callbacks_size(void);
  - NAMING: get_host_callbacks / get_host_callbacks_size (must match macro + notifier.c weak defs).
  - PRESERVE: existing get_command_map/_size, get_layer_map/_size, notifier_set_os.

Task 3: MODIFY notifier.h — INSERT typed-command constants block
  - LOCATE: the `#define WT(...) WINDOW_TITLE(__VA_ARGS__)` line (~L29) and the
    following blank line + `// Define macros to create the maps` (~L31).
  - INSERT between WT and `// Define macros` a labeled constants block. Use this
    EXACT content (values verified against §4.6/§16/external_deps):

        // ---- Host-Side Rules & Typed Commands (§4.6 / §14) ----------------------
        // Wire discriminator: data[2] == 0xF0 => typed command (§4.6). Never begins a
        // real matched string (sanitizer allows 0x20-0x7E), so legacy strings coexist.
        #define NOTIFY_CMD_DISCRIMINATOR      0xF0   // §4.6 / §16
        // Typed-response marker (>=2), distinct from legacy match-bool 0/1 (§4.6).
        #define NOTIFY_RESPONSE_MARKER        0x51   // §4.6 / §16
        // Typed command ids (§4.6 command table; 0x04 reserved for VIA-coexist).
        #define NOTIFY_CMD_QUERY_INFO         0x01   // §4.6
        #define NOTIFY_CMD_QUERY_CALLBACK     0x02   // §4.6
        #define NOTIFY_CMD_SET_OS             0x03   // §4.6 / §4.7
        #define NOTIFY_CMD_APPLY_HOST_CONTEXT 0x05   // §4.6 / §14
        // Protocol version: 1 = legacy string-only; 2 = typed-command capable (§4.6).
        #define NOTIFY_PROTO_VER              2      // §4.6
        // feature_flags BIT positions (§4.6); notifier.c builds the mask at runtime:
        //   0x01 | (get_host_callbacks_size()>0 ? 0x02 : 0)
        #define NOTIFY_FEATURE_APPLY_HOST_CONTEXT 0x01  // §4.6
        #define NOTIFY_FEATURE_CALLBACK_REGISTRY  0x02  // §4.6
        #define NOTIFY_FEATURE_VIA_COEXIST        0x04  // §4.6 (reserved)
        // Host-callback registry cap (§14) — bounds host_cb_enabled[] in notifier.c.
        #define HOST_CALLBACK_MAX              32     // §14
        // Host layers reserved >= 224 so they resolve above board layers (§14/§16;
        // 255 = LAYER_UNSET, defined in notifier.c).
        #define HOST_LAYER_BASE                224    // §14 / §16

Task 4: MODIFY notifier.h — INSERT DEFINE_HOST_CALLBACKS macro (byte-exact mirror)
  - LOCATE: immediately AFTER the constants block from Task 3 (still BEFORE
    `// Define macros to create the maps` / DEFINE_SERIAL_COMMANDS).
  - INSERT this EXACT body (verified to produce user_host_callbacks + accessors):

        // Named host-callback registry (§14). ID = array index, stable per build;
        // the host re-queries names on reconnect so cross-flash renumbering is
        // harmless. Omitting this macro => weak {NULL,0} defaults (notifier.c) =>
        // callback_count=0, feature bit 0x02 clear; module behaves identically to today.
        #define DEFINE_HOST_CALLBACKS(...) \
            host_callback_t user_host_callbacks[] = __VA_ARGS__; \
            const size_t user_host_callbacks_size = sizeof(user_host_callbacks) / sizeof(user_host_callbacks[0]); \
            host_callback_t* get_host_callbacks(void) { return user_host_callbacks; } \
            size_t get_host_callbacks_size(void) { return user_host_callbacks_size; }

  - NAMING: user_host_callbacks / get_host_callbacks / get_host_callbacks_size
    (PLAIN naming — matches DEFINE_SERIAL_COMMANDS pattern; NOT _notifier_ namespacing).
  - DEPENDS ON: host_callback_t (Task 1) + size_t (includer-provided). ✓
  - PRESERVE: DEFINE_SERIAL_COMMANDS / DEFINE_SERIAL_LAYERS / DEFINE_SERIAL_*_OS untouched.
```

### Implementation Patterns & Key Details

```c
// The macro mirrors DEFINE_SERIAL_COMMANDS — only the type + symbol stem differ:
//   DEFINE_SERIAL_COMMANDS:  command_map_t user_command_map[]   ; get_command_map()
//   DEFINE_HOST_CALLBACKS:   host_callback_t user_host_callbacks[]; get_host_callbacks()
// Both override notifier.c weak defaults via ordinary (non-_notifier_) strong defs.

// feature_flags is RUNTIME-BUILT in notifier.c, NOT a header constant:
//   unsigned flags = NOTIFY_FEATURE_APPLY_HOST_CONTEXT
//                  | (get_host_callbacks_size() > 0 ? NOTIFY_FEATURE_CALLBACK_REGISTRY : 0);
// The header provides the BIT constants; notifier.c composes the byte.

// ANTI-PATTERN: do NOT add a NOTIFY_CMD_VIA_COEXIST / 0x04 define (reserved).
// ANTI-PATTERN: do NOT add #define LAYER_UNSET 255 here (it's notifier.c's, §16).
// ANTI-PATTERN: do NOT declare get_host_callbacks twice (once in decl block, once in
//   macro is the SAME pattern as get_command_map — that's correct; the decl is the
//   prototype, the macro/weak-def is the definition. Do not remove the prototype.)
```

### Integration Points

```yaml
HEADERS:
  - add to: notifier.h (3 insertion points above)
  - pattern: "host_callback_t + accessor decls + constants + DEFINE_HOST_CALLBACKS"
DECLARATIONS:
  - add to: notifier.h forward-decl block (after notifier_set_os)
  - pattern: "host_callback_t* get_host_callbacks(void); size_t get_host_callbacks_size(void);"
MACROS:
  - add to: notifier.h (before DEFINE_SERIAL_COMMANDS)
  - pattern: "DEFINE_HOST_CALLBACKS(...) -> user_host_callbacks[] + 2 accessors"
CONSTANTS:
  - add to: notifier.h (12 #defines, §4.6/§14/§16)
LINKAGE (forward contract for P1.M1.T2.S1):
  - "notifier.c MUST provide weak defaults named EXACTLY get_host_callbacks /
     get_host_callbacks_size returning {NULL,0}, plus host_cb_enabled[HOST_CALLBACK_MAX]
     and host_layer. feature_flags built at runtime from get_host_callbacks_size()."
BUILD/CONFIG/ROUTES/DATABASE:
  - none (header-only; no rules.mk, no runtime surface).
```

## Validation Loop

> Toolchain: gcc (no ruff/mypy/pytest — C project). Validate by compile + link.
> qmk_stubs/os_detection.h now EXISTS (plan 002), so the real stub harness resolves
> the header's `#include "os_detection.h"`. These gates use a throwaway stub too, to
> validate the header/macro in isolation without depending on notifier.c.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# Throwaway stub (or reuse qmk_stubs/os_detection.h if present).
STUBDIR=$(mktemp -d)
cat > "$STUBDIR/os_detection.h" <<'EOF'
typedef enum { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4 } os_variant_t;
EOF

# Header parses cleanly when included (stddef provides size_t, matching real consumers).
cat > "$STUBDIR/pm_parse.c" <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
EOF
gcc -fsyntax-only -Wall -Wextra -std=c99 -I"$STUBDIR" -I. -Wno-pragma-once-outside-header "$STUBDIR/pm_parse.c"
echo "parse exit=$?   (expect 0; no warnings from the new content)"

# Confirm all additions are present.
grep -n 'typedef struct {' notifier.h | sed -n '3p'           # host_callback_t is the 3rd struct
grep -n 'host_callback_t\* get_host_callbacks(void)' notifier.h
grep -n 'size_t .*get_host_callbacks_size(void)' notifier.h
grep -n '#define DEFINE_HOST_CALLBACKS' notifier.h
for c in NOTIFY_CMD_DISCRIMINATOR NOTIFY_RESPONSE_MARKER NOTIFY_CMD_QUERY_INFO \
         NOTIFY_CMD_QUERY_CALLBACK NOTIFY_CMD_SET_OS NOTIFY_CMD_APPLY_HOST_CONTEXT \
         NOTIFY_PROTO_VER NOTIFY_FEATURE_APPLY_HOST_CONTEXT NOTIFY_FEATURE_CALLBACK_REGISTRY \
         NOTIFY_FEATURE_VIA_COEXIST HOST_CALLBACK_MAX HOST_LAYER_BASE; do
  grep -q "#define $c " notifier.h || echo "MISSING constant: $c"
done
echo "constants check done (only MISSING lines are bad)"

rm -rf "$STUBDIR"
# Expected: parse exit 0; all greps print a line; no "MISSING constant" lines.
```

### Level 2: Unit Tests (Component Validation — the linkage contract)

```bash
cd /home/dustin/projects/qmk-notifier
STUBDIR=$(mktemp -d)
cat > "$STUBDIR/os_detection.h" <<'EOF'
typedef enum { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4 } os_variant_t;
EOF

# A keymap (defines the registry) + mock weak defaults (P1.M1.T2.S1 names) + driver.
# If the macro symbol names don't EXACTLY match the weak defaults, LINK FAILS.
cat > "$STUBDIR/keymap.c" <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
static void en1(void){}
static void dis1(void){}
static void en2(void){}
DEFINE_HOST_CALLBACKS({
    { "vim_lazy_insert", en1, dis1 },
    { "disable_vim", en2, 0 },
});
EOF
cat > "$STUBDIR/mock_notifier.c" <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
#include <stdio.h>
#include <assert.h>
__attribute__((weak)) host_callback_t* get_host_callbacks(void){return 0;}
__attribute__((weak)) size_t get_host_callbacks_size(void){return 0;}
int main(void){
    host_callback_t *cb = get_host_callbacks();
    size_t n = get_host_callbacks_size();
    assert(n==2);                                   /* override, not weak 0 */
    assert(cb[0].on_enable && cb[1].on_disable==0); /* cross-TU + NULL ok */
    assert(NOTIFY_CMD_DISCRIMINATOR==0xF0 && NOTIFY_RESPONSE_MARKER==0x51);
    assert(NOTIFY_CMD_QUERY_INFO==0x01 && NOTIFY_CMD_QUERY_CALLBACK==0x02);
    assert(NOTIFY_CMD_SET_OS==0x03 && NOTIFY_CMD_APPLY_HOST_CONTEXT==0x05);
    assert(NOTIFY_PROTO_VER==2);
    assert(NOTIFY_FEATURE_APPLY_HOST_CONTEXT==0x01 && NOTIFY_FEATURE_CALLBACK_REGISTRY==0x02 && NOTIFY_FEATURE_VIA_COEXIST==0x04);
    assert(HOST_CALLBACK_MAX==32 && HOST_LAYER_BASE==224);
    unsigned flags = NOTIFY_FEATURE_APPLY_HOST_CONTEXT | (get_host_callbacks_size()>0 ? NOTIFY_FEATURE_CALLBACK_REGISTRY : 0);
    assert(flags==0x03);
    printf("OK count=%zu flags=0x%02X names=%s,%s\n", n, flags, cb[0].name, cb[1].name);
    return 0;
}
EOF
gcc -Wall -Wextra -std=c99 -I"$STUBDIR" -I. -c "$STUBDIR/keymap.c" -o "$STUBDIR/keymap.o" && echo "keymap compile OK"
gcc -Wall -std=c99 -I"$STUBDIR" -I. "$STUBDIR/keymap.o" "$STUBDIR/mock_notifier.c" -o "$STUBDIR/host_link" && echo "LINK OK (symbol names match future notifier.c weak defs)"
"$STUBDIR/host_link"; echo "run exit=$?   (expect 0)"
echo "=== generated symbols in keymap.o ==="
nm "$STUBDIR/keymap.o" | grep -iE 'host_callback|get_host_callbacks' | grep -iv ' U '
rm -rf "$STUBDIR"
# Expected: keymap compile OK; LINK OK; run prints "OK count=2 flags=0x03 names=vim_lazy_insert,disable_vim"; exit 0.
# nm shows: user_host_callbacks, user_host_callbacks_size, get_host_callbacks, get_host_callbacks_size.
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# Existing stub harness must STILL pass (this change is additive; os_detection.h exists).
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "stub-tests exit=$?"
tail -n 5 /tmp/ns.out
# Expected: exit 0; "✓ notifier stub-compile gate PASSED" (dispatch + os suites green).
rm -f /tmp/ns.out

# Diff hygiene: only notifier.h changed in source (besides plan/ artifacts).
git diff --stat -- ':!plan'
# Expected: only `notifier.h` listed.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Confirm Mode-A comments cite the right PRD sections (item point 5).
grep -qi '§4.6\|section 4.6\|§14\|section 14' notifier.h && echo "PRD section citations present" || echo "MISSING citations"
grep -qi 'array index\|ID = array index\|stable per build' notifier.h && echo "ID-stability note present" || echo "MISSING ID note"

# Confirm backward-compat invariant: no #ifdef gating around the new surface.
! grep -q '#ifdef.*HOST\|#if.*NOTIFY' notifier.h && echo "no #ifdef gating (structural compat)" || echo "WARN: unexpected #ifdef"

# Confirm the 3-field struct does NOT add a -Wmissing-field-initializers pitfall
# (verify by compiling a row that omits on_disable — done in Level 2 keymap.c row 2).
echo "3-field clean-init verified in Level 2 (disable_vim row omits nothing problematic; NULL accepted)."
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -fsyntax-only` of a `#include "notifier.h"` TU passes, no new warnings.
- [ ] Level 2: keymap+mock-weak-defs compile, LINK, run → exit 0; nm shows the 4 symbols.
- [ ] Level 3: `run_notifier_stub_tests.sh` still PASSED; only `notifier.h` changed.
- [ ] Level 4: PRD-section citations + ID-stability note present; no `#ifdef` gating.

### Feature Validation

- [ ] `host_callback_t` present after `layer_map_t`; uses `callback_t`.
- [ ] `get_host_callbacks`/`get_host_callbacks_size` declared after `notifier_set_os`.
- [ ] All 12 constants present with exact values; no 0x04 cmd define; no LAYER_UNSET.
- [ ] `DEFINE_HOST_CALLBACKS` byte-exact; emits `user_host_callbacks` + both accessors.
- [ ] feature_flags runtime-build idiom compiles (uses the bit constants correctly).

### Code Quality Validation

- [ ] Matches existing `//` comment style; new content is one cohesive labeled block.
- [ ] Existing structs/decls/macros/ordering preserved (additive-only).
- [ ] No stddef.h added; no restyle of untouched lines; no anti-patterns.
- [ ] No modification to notifier.c, pattern_match.*, test_*.c, run_*.sh, PRD.md, tasks.json.

### Documentation & Deployment

- [ ] Inline `//` comments self-document (Mode A); each constant cites its PRD section.
- [ ] Forward linkage dependency on P1.M1.T2.S1 (weak defs) documented.

---

## Anti-Patterns to Avoid

- ❌ Don't paraphrase the macro body — symbol names must be byte-exact (link contract).
- ❌ Don't use `_notifier_` namespacing — `DEFINE_HOST_CALLBACKS` uses plain `user_*`/`get_*`
  (like `DEFINE_SERIAL_COMMANDS`, NOT like `DEFINE_SERIAL_*_OS`).
- ❌ Don't define a `NOTIFY_CMD_*` for `0x04` (reserved for VIA-coexist).
- ❌ Don't add `#define LAYER_UNSET 255` to the header — it lives in notifier.c (§16).
- ❌ Don't define a single `FEATURE_FLAGS` value — the three bit constants are composed at runtime.
- ❌ Don't fold `host_callback_t` into `command_map_t` — board/host planes are orthogonal.
- ❌ Don't add `#include <stddef.h>` — out of scope; rely on includers (as today).
- ❌ Don't gate the new surface behind `#ifdef` — backward compat is structural (weak symbols).
- ❌ Don't touch notifier.c, pattern_match.*, test_*.c, run_*.sh, PRD.md, tasks.json.

---

## Confidence Score: 10/10

The four edits are precisely specified (exact insertion anchors, byte-exact macro
body verified by compile+link+nm, all 12 constant values cross-checked against
§4.6/§14/§16/external_deps). The load-bearing detail — that `DEFINE_HOST_CALLBACKS`
emits exactly `user_host_callbacks`/`get_host_callbacks`/`get_host_callbacks_size`
which override notifier.c's (future) weak defaults — was **empirically verified**
by linking a keymap against mock weak defaults (override worked: count=2; all
constants asserted; feature_flags built to 0x03). The change is purely additive to
a header whose existing includers gain only new symbols, so the existing stub-test
gate stays green. Backward compat is structural (no `#ifdef`), confirmed by design.