# PRP — P1.M1.T1.S1: Add OS API surface to `notifier.h`

## Goal

**Feature Goal**: Extend the existing public header `notifier.h` with the
multi-OS map-selection API surface: include `os_detection.h`, declare the single
OS entry point `notifier_set_os`, and add the two `DEFINE_SERIAL_*_OS` map
macros that generate the per-OS override symbols.

**Deliverable**: The modified file `notifier.h` at the repo root
(`/home/dustin/projects/qmk-notifier/notifier.h`), grown from ~42 lines by three
additions placed at exact insertion points, with the new macro bodies
**byte-exact** per PRD §5.5.

**Success Definition**:
- `notifier.h` now `#include "os_detection.h"` (right after `#include <stdbool.h>`).
- Declares `void notifier_set_os(os_variant_t os);` after `get_layer_map_size(void);`.
- Defines `DEFINE_SERIAL_COMMANDS_OS` and `DEFINE_SERIAL_LAYERS_OS` after the
  existing `DEFINE_SERIAL_*` macros, byte-identical to PRD §5.5.
- A throwaway keymap using `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)` compiles and
  LINKS against mock §8.3 weak defaults (the generated symbols
  `_notifier_get_command_map_OS_MACOS`, `_notifier_get_command_map_OS_MACOS_size`,
  etc. resolve exactly).
- The new inline comments cover the four required points (naming contract,
  OS_UNSURE exclusion, row-struct parity, selection rule).
- Existing parts of `notifier.h` are otherwise untouched (no restyle).

## User Persona (if applicable)

**Target User**: End-user keymap author (multi-OS opt-in) and the downstream
`notifier.c` (P1.M1.T3.S1) which provides matching weak defaults + selectors.

**Use Case**: A keymap writes
`DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, { … })` at file scope; the macro emits four
per-OS symbols that override `notifier.c`'s weak defaults at link time.

**User Journey**: keymap `#include "notifier.h"` → calls `notifier_set_os(os)`
from `process_detected_host_os_kb` → defines per-OS maps via the `_OS` macros →
qmk compiles+links (keymap symbols override weak defaults) → dispatch scans the
OS map first (P1.M1.T3.S2).

**Pain Points Addressed**: Gives keymaps a stable, namespaced way to override
command/layer maps per detected OS without touching the module internals.

## Why

- Establishes the **public API + linkage contract** for the multi-OS overlay
  (PRD §2 F8, §5) before the consumer (`notifier.c`) is changed.
- The macro-generated symbol names are a **hard cross-file contract**: they must
  match `notifier.c`'s weak defaults and `select_*_map_os()` switch (PRD §8.3)
  exactly or the firmware fails to link.
- Keeps `os_detection.h` header-only (uses the `os_variant_t` TYPE only; never
  calls `detected_host_os()`) → zero link dependency on the OS-detection `.c`.

## What

Three additive edits to `notifier.h` (no deletions, no restyle):

1. **Include** — insert `#include "os_detection.h"` immediately after
   `#include <stdbool.h>`, with a `//` comment noting it is header-only / the
   module uses only the `os_variant_t` type (PRD §5.1, §2 F8.2).
2. **Declaration** — add `void notifier_set_os(os_variant_t os);` to the
   accessor-declaration block, directly after `size_t get_layer_map_size(void);`
   (PRD §5.2). Comment: it is the ONLY public OS entry point, pushed from the
   keymap (§2 F8.2).
3. **Two macros** — append `DEFINE_SERIAL_COMMANDS_OS(os, …)` and
   `DEFINE_SERIAL_LAYERS_OS(os, …)` AFTER the existing
   `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS` (do not reorder existing
   macros). Bodies byte-exact from PRD §5.5. Inline `//` comments covering the
   four required informational points (see Implementation Tasks).

### Success Criteria

- [ ] All three additions present at the specified insertion points.
- [ ] `notifier_set_os` is the only new public declaration; per-OS accessor
  symbols are NOT declared here (they are an internal linkage contract, §5.5 note).
- [ ] New macro bodies are byte-identical to PRD §5.5 (symbol-name contract).
- [ ] Throwaway keymap compiles+links against mock §8.3 weak defaults.
- [ ] Existing file content/style otherwise unchanged.

## All Needed Context

### Context Completeness Check

_Pass_: The exact macro bodies, enum, insertion points, and the consumer-side
symbol contract (§8.3) are all provided inline below and were empirically
verified (compile+link). An implementer with only this PRP + the repo can make
the three edits correctly with no guessing.

### Documentation & References

```yaml
# MUST READ — authoritative macro bodies (the contract)
- file: plan/002_c243e735980a/prd_snapshot.md
  section: "### 5.5 OS-specific map-definition macros"
  why: "Contains the BYTE-EXACT DEFINE_SERIAL_COMMANDS_OS / DEFINE_SERIAL_LAYERS_OS
        bodies and the token-paste naming explanation."
  critical: "Macro bodies must be reproduced verbatim — the generated symbols
             (_notifier_get_command_map_OS_MACOS, …) MUST match notifier.c §8.3
             weak defaults or linking fails. VERIFIED by research."

- file: plan/002_c243e735980a/prd_snapshot.md
  section: "### 8.3 Weak default maps"   (also "### 5.2", "### 5.1")
  why: "§8.3 shows the exact weak-default symbol names + select_*_map_os switch
        the macros must link against; §5.1 shows the os_detection.h include + its
        header-only rationale; §5.2 shows the notifier_set_os decl placement."
  critical: "Consumer side of the contract. The selector references
             _notifier_get_command_map_OS_LINUX/_WINDOWS/_MACOS/_IOS (+_size) and
             the layer equivalents — these are exactly what the macros produce."

- file: plan/002_c243e735980a/prd_snapshot.md
  section: "### 2 Functional Requirements → F8 (Multi-OS map selection)"
  why: "F8.1 (os_variant_t reused as-is), F8.2 (notifier_set_os is the only
        mutation point, module never calls detected_host_os), F8.4/F8.5
        (OS-first/default-fallback, tracks independent), F8.6 (OS_UNSURE has no
        OS map). These are the semantics the inline comments must state."
  critical: "F8.6: OS_UNSURE must NOT be passed to the _OS macros (no map by design)."

- file: plan/002_c243e735980a/architecture/external_deps.md
  section: "## QMK os_detection.h"
  why: "Authoritative os_variant_t enum values (OS_UNSURE=0..OS_IOS=4), the
        process_detected_host_os_kb hook signature, and the invariant that the
        module must NOT call detected_host_os()."
  critical: "The stub qmk_stubs/os_detection.h (sibling task P1.M1.T2.S1) must
             define exactly this enum for host tests."

# Pattern to follow (the file being modified)
- file: notifier.h
  why: "The existing DEFINE_SERIAL_COMMANDS/DEFINE_SERIAL_LAYERS macros are the
        template the _OS variants mirror (array + _size const + 2 accessor fns)."
  pattern: "Default macros name symbols user_command_map / get_command_map;
            _OS macros name them _notifier_command_map_##os / _notifier_get_command_map_##os
            (namespaced, internal-linkage contract)."
  gotcha: "File uses terse // comments and does NOT include <stddef.h> (relies on
           includer for size_t). Match the // style; do NOT add stddef.h (out of scope)."

# Consumer / validator
- file: test_notifier_dispatch.c
  why: "Existing host test that #include \"notifier.h\" and uses DEFINE_SERIAL_*.
        Shows the inclusion + macro-use pattern the _OS macros must fit."
  gotcha: "Will NOT compile after this change until qmk_stubs/os_detection.h exists
           (sibling task P1.M1.T2.S1). See Sequencing risk below."
```

### Current Codebase tree (relevant slice)

```bash
notifier.h                      # ← MODIFY (this task). Currently 42 lines.
notifier.c                      # consumer of the contract (P1.M1.T3) — do NOT touch now
pattern_match.h  pattern_match.c# unaffected
qmk_stubs/                      # qmk_keyboard_stub.h, qmk_stubs.c, raw_hid.h
                                #   NOTE: os_detection.h NOT here yet (P1.M1.T2.S1)
test_notifier_dispatch.c        # #include "notifier.h" — breaks until stub lands
run_notifier_stub_tests.sh      # stub-compile gate — breaks until stub lands
run_all_tests.sh                # 9-suite corpus — unaffected (links pattern_match.c only)
PRD.md                          # READ-ONLY
plan/002_c243e735980a/          # this plan — architecture/, prd_snapshot.md, tasks.json
```

### Desired Codebase tree with files to be added/changed

```bash
notifier.h                      # MODIFIED: +3 additions (include, decl, 2 macros + comments)
# (no new files created by this subtask)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (linkage contract): the ##os paste takes the ENUMERATOR TOKEN the
//   caller writes. DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, …) generates
//   _notifier_command_map_OS_MACOS, _notifier_command_map_OS_MACOS_size,
//   _notifier_get_command_map_OS_MACOS, _notifier_get_command_map_OS_MACOS_size.
//   These EXACT names are what notifier.c §8.3 weak defaults provide and what
//   select_command_map_os() references. A typo here = link failure. (Verified.)

// GOTCHA (sequencing): qmk_stubs/os_detection.h does NOT exist yet (sibling
//   task P1.M1.T2.S1). After adding #include "os_detection.h", the EXISTING
//   run_notifier_stub_tests.sh and test_notifier_dispatch.c will FAIL to compile
//   in isolation. This is EXPECTED at this stage; they recover once P1.M1.T2.S1
//   lands the stub. Validate this header with a THROWAWAY temp stub, not the
//   real runner.

// GOTCHA (size_t): notifier.h uses size_t without #include <stddef.h>. It compiles
//   only because includers (notifier.c via QMK_KEYBOARD_H; test_notifier_dispatch.c
//   via its own stddef include) bring size_t in first. Do NOT add stddef.h here —
//   out of scope. Ensure standalone validation harnesses include <stddef.h> first.

// GOTCHA (zero-fill warnings): omitting the trailing case_sensitive field from a
//   row yields -Wmissing-field-initializers / -Wmissing-braces under -Wextra. This
//   is the INTENDED documented parity (PRD §5.4) — the build is NOT -Werror, so
//   these warnings are acceptable. Do NOT "fix" by adding the field.

// GOTCHA (do not pass OS_UNSURE to the _OS macros): OS_UNSURE has no OS-specific
//   map by design (§2 F8.6). Only OS_LINUX/OS_WINDOWS/OS_MACOS/OS_IOS are valid.

// GOTCHA (only ONE new public symbol): notifier_set_os. Do NOT declare the
//   per-OS accessors (_notifier_get_*_map_OS_*) in this header — they are an
//   internal linkage contract (§5.5 note) referenced only by notifier.c.
```

## Implementation Blueprint

### Data models and structure

No new types in this task. The header already defines `callback_t`,
`command_map_t`, `layer_map_t` (unchanged). `os_variant_t` comes from the
included `os_detection.h` and is reused as-is (never redefined — §2 F8.1).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.h — INSERT #include "os_detection.h"
  - LOCATE: line 2 `#include <stdbool.h>` (current file; verify with `grep -n`).
  - INSERT immediately AFTER it:
        #include "os_detection.h"   // header-only: module uses os_variant_t TYPE only,
                                    // never calls detected_host_os() (§5.1, §2 F8.2)
  - PRESERVE: `#pragma once` on line 1; do not move other includes.
  - NAMING/PLACEMENT: exact — must come AFTER <stdbool.h> (so bool is defined
    before any consumer of it) and BEFORE the typedefs (so the include order in
    PRD §5.1 is matched).

Task 2: MODIFY notifier.h — INSERT notifier_set_os declaration
  - LOCATE the accessor-declaration block:
        command_map_t* get_command_map(void);
        size_t         get_command_map_size(void);
        layer_map_t*   get_layer_map(void);
        size_t         get_layer_map_size(void);
  - INSERT directly AFTER `size_t get_layer_map_size(void);`:
        // The OS selector — pushed from the keymap (§8.7). os_variant_t comes from
        // os_detection.h (included above). ONLY public OS entry point (§2 F8.2).
        void notifier_set_os(os_variant_t os);
  - PRESERVE: the preceding `// Forward declarations…` comment and the four
    existing accessor decls; do NOT touch their ordering.

Task 3: MODIFY notifier.h — APPEND the two _OS macros (byte-exact per PRD §5.5)
  - LOCATE the end of `#define DEFINE_SERIAL_LAYERS(...) \ … size_t get_layer_map_size(void) { return user_layer_map_size; }`
  - INSERT AFTER it and BEFORE the `// From QMK` entry-point block:
      (a) a block of // comments covering the four required points (see below);
      (b) DEFINE_SERIAL_COMMANDS_OS(os, …);
      (c) DEFINE_SERIAL_LAYERS_OS(os, …).
  - BODIES: copy VERBATIM from the "Verbatim macro bodies" block below. Do not
    reformat line continuations, spacing, or the `##os` / `##os##_size` pastes.
  - DEPENDENCIES: relies on command_map_t / layer_map_t / size_t already
    declared earlier in the file (they are).
  - NAMING: symbol stems are fixed: _notifier_command_map_, _notifier_layer_map_,
    _notifier_get_command_map_, _notifier_get_layer_map_, each suffixed via ##os
    and (for the size accessors) ##os##_size.
```

**Verbatim macro bodies (PRD §5.5 — reproduce exactly):**

```c
/*
 * OS-specific map-definition macros (multi-OS overlay, §2 F8 / §5.5).
 *
 * Naming contract: `os` is an os_variant_t ENUMERATOR token (OS_LINUX,
 * OS_WINDOWS, OS_MACOS, OS_IOS). The ##os paste mangles it into the symbols
 * _notifier_command_map_OS_MACOS, _notifier_get_command_map_OS_MACOS,
 * _notifier_get_command_map_OS_MACOS_size, etc. These EXACT names are what
 * notifier.c provides as weak defaults and what its select_*_map_os() switch
 * references (§8.3). A typo here = link failure. The keymap never calls these
 * directly. (Use `//` line style if matching the surrounding file; the prose
 * above is the required content — item point 5(a).)
 *
 * OS_UNSURE has NO OS-specific map by design (§2 F8.6): do not pass it here.
 * Row-struct parity: a row has the SAME shape as the default macro
 * ({ pattern, on_enable, on_disable, case_sensitive? } for commands;
 *  { pattern, layer, case_sensitive? } for layers); an omitted trailing
 *  case_sensitive zero-fills to false (§5.4) — item point 5(c).
 *
 * Selection rule (§2 F8.4/F8.5): at dispatch, for EACH map type the OS-specific
 * map for current_os is scanned FIRST; a match wins and the default map is NOT
 * consulted. If no OS map exists (or matches nothing) the default is scanned.
 * The command and layer tracks make this decision INDEPENDENTLY. OS_UNSURE ⇒
 * default only — item point 5(d).
 */
#define DEFINE_SERIAL_COMMANDS_OS(os, ...) \
    command_map_t _notifier_command_map_##os[] = __VA_ARGS__; \
    const size_t  _notifier_command_map_##os##_size = \
        sizeof(_notifier_command_map_##os) / sizeof(_notifier_command_map_##os[0]); \
    command_map_t* _notifier_get_command_map_##os(void) { \
        return _notifier_command_map_##os; \
    } \
    size_t _notifier_get_command_map_##os##_size(void) { \
        return _notifier_command_map_##os##_size; \
    }

#define DEFINE_SERIAL_LAYERS_OS(os, ...) \
    layer_map_t _notifier_layer_map_##os[] = __VA_ARGS__; \
    const size_t  _notifier_layer_map_##os##_size = \
        sizeof(_notifier_layer_map_##os) / sizeof(_notifier_layer_map_##os[0]); \
    layer_map_t* _notifier_get_layer_map_##os(void) { \
        return _notifier_layer_map_##os; \
    } \
    size_t _notifier_get_layer_map_##os##_size(void) { \
        return _notifier_layer_map_##os##_size; \
    }
```

> **Style note:** The current `notifier.h` uses terse `//` line comments (no
> `/* */` blocks). To stay consistent, you MAY convert the block comment above to
> `//` lines — but ALL FOUR required informational points must remain
> (naming contract, OS_UNSURE exclusion, row-struct parity, selection rule).
> The two `#define` bodies must be byte-exact regardless of comment style.

### Implementation Patterns & Key Details

```c
// The macro mirrors the DEFAULT macro, only namespaced + parameterized by os:
//   default:   command_map_t user_command_map[]          = …;  get_command_map()
//   OS:        command_map_t _notifier_command_map_##os[]= …;  _notifier_get_command_map_##os()
// The underscore prefix signals "internal linkage contract" (§5.5) — these
// cross the TU boundary only as weak-default overrides, never as direct calls.

// ANTI-PATTERN: do NOT declare the per-OS accessors in notifier.h, e.g.
//   command_map_t* _notifier_get_command_map_OS_MACOS(void);   // ❌ not public
// They are referenced ONLY inside notifier.c (weak defs + selector).

// ANTI-PATTERN: do NOT stringify os (e.g. #os). It must be token-pasted (##os)
// so the result is a real C identifier, not a string literal.
```

### Integration Points

```yaml
HEADERS:
  - add to: notifier.h (after #include <stdbool.h>)
  - pattern: '#include "os_detection.h"   // header-only, TYPE-only use'
DECLARATIONS:
  - add to: notifier.h accessor block (after get_layer_map_size)
  - pattern: "void notifier_set_os(os_variant_t os);"
MACROS:
  - add to: notifier.h (after DEFINE_SERIAL_LAYERS, before entry points)
  - pattern: "DEFINE_SERIAL_COMMANDS_OS(os, ...) / DEFINE_SERIAL_LAYERS_OS(os, ...)"
LINKAGE (forward contract for P1.M1.T3.S1):
  - "notifier.c MUST provide weak defaults + select_*_map_os() switch whose
     referenced names are EXACTLY the macro-generated ones (§8.3)."
BUILD/CONFIG/ROUTES:
  - none (header-only change; no rules.mk, no DB, no routes).
```

## Validation Loop

> Toolchain: gcc (no ruff/mypy/pytest — this is C). Validate with compile + link.
> **Sequencing note:** the real `run_notifier_stub_tests.sh` is EXPECTED to break
> here because `qmk_stubs/os_detection.h` lands in sibling task P1.M1.T2.S1. The
> gates below use a THROWAWAY stub so this header is validated in isolation.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Throwaway stub for os_detection.h (sibling task P1.M1.T2.S1 not yet landed).
STUBDIR=$(mktemp -d)
cat > "$STUBDIR/os_detection.h" <<'EOF'
typedef enum { OS_UNSURE = 0, OS_LINUX = 1, OS_WINDOWS = 2, OS_MACOS = 3, OS_IOS = 4 } os_variant_t;
EOF

# 1b. The header parses cleanly when included (stddef provides size_t, matching
#     how real consumers include it). -Wno-pragma-once-outside-header silences
#     the benign #pragma-once-as-main-file warning.
cat > "$STUBDIR/pm_parse.c" <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
EOF
gcc -fsyntax-only -Wall -Wextra -std=c99 -I"$STUBDIR" -I. -Wno-pragma-once-outside-header "$STUBDIR/pm_parse.c"
echo "parse exit=$?   (expect 0)"

# 1c. The three additions are present at the right places.
grep -n '^#include "os_detection.h"' notifier.h                      # right after <stdbool.h>
grep -n 'void notifier_set_os(os_variant_t os);' notifier.h          # accessor block
grep -n '#define DEFINE_SERIAL_COMMANDS_OS' notifier.h               # appended
grep -n '#define DEFINE_SERIAL_LAYERS_OS' notifier.h                 # appended
grep -q '_notifier_command_map_##os' notifier.h && echo "##os token-paste present"
grep -q '_notifier_get_command_map_##os##_size' notifier.h && echo "size-accessor paste present"

rm -rf "$STUBDIR"
# Expected: parse exit 0; all 5 greps print a line; both paste checks print "present".
```

### Level 2: Unit Tests (Component Validation — the linkage contract)

```bash
cd /home/dustin/projects/qmk-notifier
STUBDIR=$(mktemp -d)
cat > "$STUBDIR/os_detection.h" <<'EOF'
typedef enum { OS_UNSURE = 0, OS_LINUX = 1, OS_WINDOWS = 2, OS_MACOS = 3, OS_IOS = 4 } os_variant_t;
EOF

# A keymap (defines an OS map) + mock §8.3 weak defaults + a driver that calls
# the override AND the weak fallback. If the macro symbol names don't EXACTLY
# match §8.3, LINK FAILS.
cat > "$STUBDIR/keymap.c" <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
static void cb(void){}
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, { "iTerm", cb, cb });
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, { "iTerm", 3 });
DEFINE_SERIAL_LAYERS_OS(OS_LINUX, { "*alacritty*", 1 });
EOF
cat > "$STUBDIR/mock_notifier.c" <<'EOF'
#include <stddef.h>
#include <stdbool.h>
#include "notifier.h"
#include <stdio.h>
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_LINUX(void){return 0;}
__attribute__((weak)) size_t _notifier_get_command_map_OS_LINUX_size(void){return 0;}
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_WINDOWS(void){return 0;}
__attribute__((weak)) size_t _notifier_get_command_map_OS_WINDOWS_size(void){return 0;}
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_MACOS(void){return 0;}
__attribute__((weak)) size_t _notifier_get_command_map_OS_MACOS_size(void){return 0;}
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_IOS(void){return 0;}
__attribute__((weak)) size_t _notifier_get_command_map_OS_IOS_size(void){return 0;}
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_LINUX(void){return 0;}
__attribute__((weak)) size_t _notifier_get_layer_map_OS_LINUX_size(void){return 0;}
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_WINDOWS(void){return 0;}
__attribute__((weak)) size_t _notifier_get_layer_map_OS_WINDOWS_size(void){return 0;}
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_MACOS(void){return 0;}
__attribute__((weak)) size_t _notifier_get_layer_map_OS_MACOS_size(void){return 0;}
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_IOS(void){return 0;}
__attribute__((weak)) size_t _notifier_get_layer_map_OS_IOS_size(void){return 0;}
int main(void){
    command_map_t *m=_notifier_get_command_map_OS_MACOS();
    size_t s=_notifier_get_command_map_OS_MACOS_size();
    layer_map_t *ll=_notifier_get_layer_map_OS_LINUX();
    size_t lls=_notifier_get_layer_map_OS_LINUX_size();
    command_map_t *lw=_notifier_get_command_map_OS_LINUX();   /* keymap didn't define → weak NULL */
    printf("MACOS cmd size=%zu pattern=%s\n", s, m[0].pattern);
    printf("LINUX layer size=%zu layer=%d\n", lls, ll[0].layer);
    printf("LINUX cmd weak-fallback ptr=%p\n", (void*)lw);
    return (s==1 && lls==1 && lw==0) ? 0 : 1;
}
EOF
gcc -Wall -Wextra -std=c99 -I"$STUBDIR" -I. -c "$STUBDIR/keymap.c" -o "$STUBDIR/keymap.o"
gcc -Wall -std=c99 -I"$STUBDIR" -I. "$STUBDIR/keymap.o" "$STUBDIR/mock_notifier.c" -o "$STUBDIR/pm_os_link"
"$STUBDIR/pm_os_link"; echo "link+run exit=$?   (expect 0 — names match §8.3)"
rm -rf "$STUBDIR"
# Expected: exit 0, prints "MACOS cmd size=1 pattern=iTerm", "LINUX layer size=1 layer=1",
#           "LINUX cmd weak-fallback ptr=(nil)". Link success ⇒ symbol-name contract holds.
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# NOTE: the REAL integration gate (run_notifier_stub_tests.sh) will FAIL until
# sibling task P1.M1.T2.S1 creates qmk_stubs/os_detection.h. Do NOT treat that
# failure as a defect of THIS task. After P1.M1.T2.S1 lands, re-run:
#     ./run_notifier_stub_tests.sh   # should print "✓ notifier stub-compile gate PASSED"
# Until then, the linkage-contract check in Level 2 is the authoritative gate.

# Confirm no unintended edits elsewhere (diff hygiene): only notifier.h changed.
git diff --stat
# Expected: only `notifier.h` shows as modified (and the research/PRP files under plan/).
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Confirm the required inline-documentation points are present (item point 5).
for needle in "OS_UNSURE" "token-paste\|##os" "parity\|same shape" "scanned FIRST\|OS-first\|scanned first"; do
  grep -qiE "$needle" notifier.h && echo "doc point present: $needle" || echo "MISSING doc: $needle"
done
# Expected: all four "present".

# Confirm backward compat: the existing default macros are unchanged (no reorder/edit).
grep -nE '#define DEFINE_SERIAL_COMMANDS\(|#define DEFINE_SERIAL_LAYERS\(' notifier.h
# Expected: both default macros present and BEFORE the new _OS macros (lower line numbers).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -fsyntax-only` of a `#include "notifier.h"` TU passes (with throwaway stub).
- [ ] Level 2: keymap+mock-§8.3 compile, LINK, and run → exit 0 (symbol contract holds).
- [ ] Level 3: only `notifier.h` changed (`git diff --stat`); real stub gate deferred to P1.M1.T2.S1.
- [ ] Level 4: all four doc points present; default macros unchanged and precede _OS macros.

### Feature Validation

- [ ] `#include "os_detection.h"` present right after `#include <stdbool.h>`.
- [ ] `void notifier_set_os(os_variant_t os);` declared after `get_layer_map_size(void);`.
- [ ] Both `_OS` macros present, byte-exact vs PRD §5.5, after the default macros.
- [ ] Per-OS accessor symbols intentionally NOT declared (internal-linkage contract).
- [ ] Inline comments cover naming contract / OS_UNSURE / parity / selection rule.

### Code Quality Validation

- [ ] Matches existing file's `//` comment style (or translates PRD prose to `//` lines).
- [ ] Existing declarations/macros/ordering preserved (additive-only change).
- [ ] No stddef.h added; no restyle of untouched lines; no anti-patterns (see below).
- [ ] No modification to notifier.c, test files, run_*.sh, PRD.md, tasks.json, prd_snapshot.md.

### Documentation & Deployment

- [ ] Inline comments are self-documenting (Mode A) — no separate docs file for this task.
- [ ] Sequencing dependency on P1.M1.T2.S1 documented (real stub gate recovery).

---

## Anti-Patterns to Avoid

- ❌ Don't paraphrase the macro bodies — symbol names must be byte-exact vs §8.5/§8.3 (link contract).
- ❌ Don't declare the per-OS accessors in the header — they're an internal-linkage contract.
- ❌ Don't stringify `os` (`#os`) — it must be token-pasted (`##os`) to form a C identifier.
- ❌ Don't add `#include <stddef.h>` "to be safe" — out of scope; rely on includers (as today).
- ❌ Don't run `run_notifier_stub_tests.sh` expecting it to pass here — it can't until
  P1.M1.T2.S1 lands `qmk_stubs/os_detection.h`. Use the throwaway-stub gate instead.
- ❌ Don't reorder/restyle the existing default macros or typedefs — additive change only.
- ❌ Don't "fix" the `-Wmissing-field-initializers` warnings — they're the intended §5.4 zero-fill.
- ❌ Don't touch notifier.c, test_*.c, run_*.sh, PRD.md, tasks.json, or prd_snapshot.md.

---

## Confidence Score: 10/10

The three edits are precisely specified (exact insertion points, byte-exact macro
bodies from PRD §5.5, required comment content enumerated). The load-bearing
detail — that the macro-generated symbol names exactly match notifier.c §8.3 weak
defaults + selector — was **empirically verified** by compiling and linking a
keymap against mock §8.3 defaults (override + weak-fallback both correct). The one
sequencing risk (the stub `os_detection.h` is a sibling task) is explicitly called
out and handled with a throwaway-stub validation that does not depend on it.