name: "P1.M3.T1.S1 — CONSOLE_ENABLE layer-track print + print.h stub"
description: >
  Add the missing layer-track match/miss `uprintf` to the `#ifdef CONSOLE_ENABLE`
  debug block at the end of `process_full_message` (notifier.c) — closing the
  PRD §8.6 step 9 "print per-track match/miss" gap (findings Issue 3). Create a
  minimal `qmk_stubs/print.h` host stub so the entire CONSOLE_ENABLE code path
  can be compile-checked on a host. Debug-only, zero production behavior change.

---

## Goal

**Feature Goal**: In `process_full_message` (notifier.c), inside the existing
`#ifdef CONSOLE_ENABLE` block that already prints the **command**-track
match/miss, add the symmetric **layer**-track match/miss print so debug output
satisfies PRD §8.6 step 9's "print **per-track** match/miss" wording (today only
the command track prints; the layer track is silently omitted). Additionally,
provide a minimal host `print.h` stub so the CONSOLE_ENABLE code path — which
`#include "print.h"` (notifier.c:20-21) for `uprintf` — can actually be
compiled on a host for verification. Today no `print.h` exists in `qmk_stubs`,
so a `-DCONSOLE_ENABLE` build fails at that `#include`.

**Deliverable**: TWO files —
1. **MODIFY** `notifier.c`: insert a layer-track `if (layer_found != LAYER_UNSET)
   { uprintf(...) } else { uprintf(...) }` block inside the existing
   `#ifdef CONSOLE_ENABLE` guard, immediately AFTER the existing command-track
   `if/else` and immediately BEFORE the `#endif`. Do NOT touch the command-track
   print, the GS→`|` substitution loop, or any dispatch logic.
2. **CREATE** `qmk_stubs/print.h`: a 3-line host stub mapping `uprintf` to
   `printf` (`#pragma once` + `#include <stdio.h>` +
   `#define uprintf(...) printf(__VA_ARGS__)`).

Plus a ONE-OFF manual `gcc -DCONSOLE_ENABLE` compile-check command (NOT a
committed script — the automated gate deliberately builds without
CONSOLE_ENABLE).

**Success Definition**:
- `notifier.c` compiles **clean WITHOUT** `-DCONSOLE_ENABLE`: the committed gate
  `./run_notifier_stub_tests.sh` still passes — both `test_notifier_dispatch`
  and `test_notifier_os` exit 0, **0 `FAIL:`** lines, summary prints
  `✓ notifier stub-compile gate PASSED`. (Baseline at research time: 31/31 PASS.)
- `notifier.c` compiles **clean WITH** `-DCONSOLE_ENABLE` via the exact contract
  command (see Validation Loop Level 3): exit 0, **zero new warnings** under
  `-Wall -Wextra -std=c99`.
- The new layer-track `uprintf` calls appear inside the `#ifdef CONSOLE_ENABLE`
  guard (verified by reading the block), so they contribute nothing to the
  normal build.
- No edits to: `pattern_match.*`, `notifier.h`, `qmk_stubs/qmk_keyboard_stub.h`,
  `qmk_stubs/raw_hid.h`, `qmk_stubs/os_detection.h`, `qmk_stubs/qmk_stubs.c`,
  any `test_*.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`, `.gitignore`.

## User Persona (if applicable)

**Target User**: The firmware maintainer who (rarely) builds with
`-DCONSOLE_ENABLE` to debug dispatch decisions over USB console, and any
contributor who wants confidence the CONSOLE_ENABLE code path actually compiles.

**Use Case**: A dispatch misfire is suspected (wrong layer/command fires). The
maintainer flips `CONSOLE_ENABLE = yes` in the keymap, reflashes, sends a test
message, and reads the console. Today the console shows only the **command**
track ("Matched message … on command: …" / "Did not match … on any command");
the **layer** decision is invisible, forcing the maintainer to infer it from the
`layer_on` side effect. After this change the console also prints
"Matched message … on layer: <N>" / "Did not match … on any layer".

**User Journey**: maintainer enables CONSOLE_ENABLE → builds (this task's
`print.h` stub is irrelevant on real hardware — QMK provides the real `print.h`
— but the same `uprintf` calls now compile) → flashes → sends message → console
shows BOTH tracks → layer/command decision is fully visible.

**Pain Points Addressed**: (1) half-visible debug output (layer track missing);
(2) the CONSOLE_ENABLE code path was never host-compile-checked because
`print.h` was missing from the stub harness — a latent "does it even build?"
risk for the debug path.

## Why

- **PRD conformance.** §8.6 step 9 says "print **per-track** match/miss". The
  command track conforms; the layer track does not. This is findings Issue 3
  (Minor, debug-only).
- **Zero production risk.** Every line added is inside `#ifdef CONSOLE_ENABLE`,
  which is OFF in every automated gate and in the default keymap build
  (CONSOLE_ENABLE is an opt-in QMK debug feature). There is no code-path change
  in production firmware.
- **Closes a "never compiled" gap.** The `#include "print.h"` at notifier.c:20
  is guarded by `#ifdef CONSOLE_ENABLE`, so the normal host gate never pulls it
  in and never noticed `print.h` was absent. Creating the stub lets the whole
  CONSOLE_ENABLE path be compile-verified on a host with a one-off command,
  turning a silent "would-it-build?" risk into a checked property.
- **Contained and surgical.** Two files, ~10 added lines, no dispatch-logic or
  data-model change. No interface change (the stub is a macro, not a linkable
  symbol that could clash).

## What

**File 1 — CREATE `qmk_stubs/print.h`** (new file, exact contents):
```c
#pragma once
#include <stdio.h>
#define uprintf(...) printf(__VA_ARGS__)
```
- `#pragma once` matches every other stub header (`os_detection.h`,
  `qmk_keyboard_stub.h`, `raw_hid.h` all use `#pragma once`).
- `printf` is variadic; the `__VA_ARGS__` macro forwards ALL args, satisfying
  every existing `uprintf` call site in notifier.c (see "Known Gotchas").
- ONLY included when building with `-DCONSOLE_ENABLE` (notifier.c:20 guard).
  Normal builds never see this file.

**File 2 — MODIFY `notifier.c`**: inside the existing `#ifdef CONSOLE_ENABLE`
block at the end of `process_full_message`, AFTER the command-track `if/else`
and BEFORE the `#endif`, insert:
```c
    /* LAYER track (step 9 — mirrors the command track above): print the matched
     * layer index, or a miss. The index is sufficient for debugging; printing
     * the matched pattern would require capturing a layer_map_t* pointer in the
     * scan loops above, a dispatch change not warranted for a Minor debug
     * issue. GS was already substituted to '|' by the loop above. */
    if (layer_found != LAYER_UNSET) {
        uprintf("Matched message %s on layer: %d\n", received_command, layer_found);
    } else {
        uprintf("Did not match message %s on any layer\n", received_command);
    }
```

**Anchor for the edit (use THIS text, not line numbers)** — find the unique
command-track else close + `#endif`:
```c
        uprintf("Did not match message %s on any command\n", received_command);
    }
    #endif
```
Replace the trailing `    }\n    #endif` portion by inserting the layer-track
block between `}` and `#endif`. (See Implementation Tasks for the exact
oldText/newText.)

> **Line-number note:** the findings doc references "notifier.c:417-434" for
> this block, but P1.M2.T1.S1 (sanitize_string length param) has ALREADY landed
> and shifted the block down by ~7 lines (current block ≈ lines 424-441). Do
> NOT anchor on line numbers — anchor on the command-track `uprintf` text above.

**Do NOT change**: the command-track `if/else`, the GS→`|` `for` loop, the
`#ifdef`/`#endif` guards, `return` statement, or any dispatch/scanning logic.

### Success Criteria

- [ ] `qmk_stubs/print.h` created with exactly the 3 lines above (`#pragma once`,
      `#include <stdio.h>`, `#define uprintf(...) printf(__VA_ARGS__)`).
- [ ] Layer-track `if (layer_found != LAYER_UNSET) { uprintf("Matched message
      %s on layer: %d\n", received_command, layer_found); } else { uprintf("Did
      not match message %s on any layer\n", received_command); }` added inside
      the existing `#ifdef CONSOLE_ENABLE` block, after the command-track
      `if/else`, before `#endif`.
- [ ] Command-track print, GS→`|` loop, `#ifdef`/`#endif`, and `return` are
      byte-for-byte unchanged.
- [ ] `./run_notifier_stub_tests.sh` → both binaries exit 0, 0 `FAIL:`,
      `✓ notifier stub-compile gate PASSED`.
- [ ] The contract one-off compile-check (Level 3) → exit 0, no new warnings.
- [ ] No file other than `notifier.c` and `qmk_stubs/print.h` is modified.

## All Needed Context

### Context Completeness Check

**Pass.** This PRP gives the EXACT text of both edits and was **empirically
validated during research** by applying both changes to a /tmp copy of the
current `notifier.c` + a fresh `qmk_stubs/print.h` and running BOTH the normal
gate-replicating build and the contract `-DCONSOLE_ENABLE` compile-check:
**both compiled clean, exit 0, zero warnings under `-Wall -Wextra -std=c99`**.
The current baseline `./run_notifier_stub_tests.sh` passes 31/31, 0 FAIL. The
`uprintf`→`printf` macro was confirmed to satisfy all 5 pre-existing `uprintf`
call sites plus the 2 new ones. An implementer with only this PRP + repo access
can make the two edits and prove both gates green.

### Documentation & References

```yaml
# MUST READ — the finding this task closes (Issue 3) + its prescribed fix
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/architecture/findings_and_risks.md
  section: "## Issue 3: CONSOLE_ENABLE Layer Track Debug Print"
  why: "States the exact fix (the layer-track uprintf text), the exact print.h
        stub contents, the exact compile-check command, the variables in scope,
        and the LOW/LOW risk assessment. This task IS that prescribed fix."
  critical: "The findings doc's line numbers (417-434) are STALE — P1.M2.T1.S1
        shifted the block ~7 lines down. Anchor on the command-track uprintf
        TEXT, not line numbers. Current block ≈ lines 424-441."

# MUST READ — the ONLY source file being modified
- file: notifier.c
  section: "process_full_message -> #ifdef CONSOLE_ENABLE block (≈ lines 424-441)"
  why: "Contains the command-track if/else to mirror, the GS->'|' loop that
        already ran on received_command, the #ifdef/#endif guards to insert
        inside, and the uprintf #include at lines 20-21."
  pattern: "Mirror the EXACT format of the command-track if/else (same indent,
            same %s format-spec ordering, same 'Matched message %s on <track>'
            / 'Did not match message %s on any <track>' phrasing)."
  gotcha: "layer_found is a uint8_t INDEX (or LAYER_UNSET=255) — there is NO
            layer_map_t* pointer in scope. Print %d with layer_found. Do NOT try
            to print a pattern (would require changing the scan loops)."

# MUST READ — where print.h is consumed (unchanged, already correct)
- file: notifier.c
  section: "lines 20-21: #ifdef CONSOLE_ENABLE / #include \"print.h\""
  why: "Confirms print.h is pulled in ONLY under -DCONSOLE_ENABLE and resolved
        via -Iqmk_stubs. Today this include fails because qmk_stubs/print.h
        does not exist — this task creates it."

# MUST READ — the stub directory conventions to match (style + #pragma once)
- file: qmk_stubs/os_detection.h
  why: "Template for the new print.h: #pragma once guard, minimal surface,
        header comment explaining it is a HOST STUB consumed only under the
        stub harness (mirror that commenting style for print.h)."
  pattern: "Every qmk_stubs/*.h uses '#pragma once' as its include guard and a
            block comment stating it is a host-test stub. print.h MUST match."

# REFERENCE — the committed gate that must keep passing (DO NOT modify it)
- file: run_notifier_stub_tests.sh
  why: "The P2 stub-compile gate. It builds notifier.c WITHOUT -DCONSOLE_ENABLE
        (so print.h is never pulled in by the gate). Confirms this task's
        notifier.c edit must not break the normal build. The CONSOLE path is
        validated by a SEPARATE one-off command, NOT by editing this script."
  critical: "Do NOT add -DCONSOLE_ENABLE to run_notifier_stub_tests.sh — the
        contract (OUTPUT §4) requires CONSOLE_ENABLE stay OFF in all automated
        gates. The CONSOLE compile-check is a manual one-off command only."

# REFERENCE — no conflict with the parallel task
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/P1M2T2S1/PRP.md
  why: "P1.M2.T2.S1 (running in parallel) modifies ONLY test_notifier_dispatch.c
        (adds a callback + map row + test block). It does NOT touch notifier.c,
        print.h, qmk_stubs, or CONSOLE_ENABLE. Zero file overlap → safe to
        parallelize. This task neither depends on nor blocks it."
```

### Current Codebase tree (relevant subset)

```bash
notifier.c                 # MODIFY — add layer-track print in CONSOLE_ENABLE block
notifier.h                 # (read-only ref) GS_DELIMITER, command_map_t, layer_map_t, DEFINE_* macros
pattern_match.c / .h       # (untouched) pulled in by notifier.c
qmk_stubs/
  qmk_keyboard_stub.h      # (untouched) layer_on/off, RAW_EPSIZE
  raw_hid.h                # (untouched) raw_hid_send decl
  os_detection.h           # (untouched) os_variant_t
  qmk_stubs.c              # (untouched) layer_on/off/raw_hid_send impls + stub_get_active_layer
  print.h                  # CREATE — uprintf->printf host stub (NEW this task)
run_notifier_stub_tests.sh # (untouched) committed gate; builds WITHOUT CONSOLE_ENABLE
test_notifier_*.c          # (untouched — owned by parallel P1.M2.T2.S1)
```

### Desired Codebase tree with files to be added/modified

```bash
notifier.c                 # MODIFIED: +~8 lines (layer-track if/else + comment) in CONSOLE block
qmk_stubs/print.h          # NEW (3 lines): host stub so -DCONSOLE_ENABLE compiles
# nothing else changes
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL: line numbers in the findings doc (417-434) are STALE.
// P1.M2.T1.S1 (sanitize_string length param) ALREADY LANDED and shifted the
// CONSOLE_ENABLE block down ~7 lines. Current block ≈ notifier.c:424-441.
// ALWAYS anchor edits on the command-track uprintf TEXT, never on line numbers.

// CRITICAL: layer_found is a uint8_t, NOT a pointer.
//   uint8_t layer_found = LAYER_UNSET;   // LAYER_UNSET == 255 (notifier.c:113)
// There is no layer_map_t* in scope at the print site (the scan loops capture
// only the index into layer_found, not the entry address). Print "%d" with
// layer_found. Do NOT attempt layer_entry->pattern — no such variable exists.
// (findings Issue 3 explicitly approves index-only output.)

// CRITICAL: print.h is pulled in ONLY under -DCONSOLE_ENABLE (notifier.c:20-21).
//   #ifdef CONSOLE_ENABLE
//   #include "print.h"
//   #endif
// So the committed gate (run_notifier_stub_tests.sh, no -DCONSOLE_ENABLE) NEVER
// includes print.h and never noticed it was missing. Creating it does NOT change
// the normal build at all. The CONSOLE path is checked by a one-off manual gcc
// command (Validation Loop Level 3), NOT by editing the gate script.

// GOTCHA: uprintf has 5 pre-existing call sites (notifier.c:178,189,437,439,469)
// plus the 2 this task adds. ALL are printf-style. The variadic macro
//   #define uprintf(...) printf(__VA_ARGS__)
// forwards every call site's args correctly. Site 469 uses "%u" with explicit
// (unsigned) casts — printf handles that fine.

// GOTCHA: the GS->'|' substitution loop runs on received_command BEFORE the
// command-track print AND before the new layer-track print (it is above both,
// inside the same #ifdef). So both prints see GS as '|'. Do NOT re-run or move
// that loop — the new layer-track print just reuses the already-substituted
// received_command buffer. (PRD §8.6 step 9: "GS shown as '|'".)

// GOTCHA: insert the layer-track block STRICTLY INSIDE the #ifdef/#endif —
// after the command-track else's closing '}' and before the '#endif'. If placed
// after #endif, the uprintf calls would compile in the NORMAL build and break
// the gate (uprintf undefined without print.h). Keep it inside the guard.
```

## Implementation Blueprint

### Data models and structure

**None.** This task adds no types, structs, or data. It reuses the existing
`uint8_t layer_found` / `LAYER_UNSET` (notifier.c:113) and the existing
`received_command[256]` local buffer. The `print.h` stub is a 3-line macro
header with no types.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: CREATE qmk_stubs/print.h
  - CREATE file qmk_stubs/print.h with EXACTLY:
        #pragma once
        #include <stdio.h>
        #define uprintf(...) printf(__VA_ARGS__)
  - FOLLOW pattern: qmk_stubs/os_detection.h (uses '#pragma once'; add a short
        block comment stating this is a HOST STUB consumed only under
        -DCONSOLE_ENABLE, mirroring os_detection.h's comment style).
  - NAMING: print.h (lowercase, matches the #include "print.h" at notifier.c:21).
  - PLACEMENT: qmk_stubs/print.h (alongside the other stub headers, on the
        -Iqmk_stubs include path).
  - DEPENDENCIES: none (this is the prerequisite for Task 2's compile-check).
  - DO NOT: declare uprintf as a function — it MUST be a variadic MACRO so all
        printf-style call sites forward correctly. Do NOT add any other QMK
        print API (uprint, xprintf, etc.) — only uprintf is used by notifier.c.

Task 2: MODIFY notifier.c — add layer-track print in CONSOLE_ENABLE block
  - LOCATE: process_full_message (def at notifier.c:341) -> the #ifdef
        CONSOLE_ENABLE block near its end (≈ lines 424-441).
  - INSERT: between the command-track else close and the #endif. Exact edit:
      oldText (unique in file):
          uprintf("Did not match message %s on any command\n", received_command);
          }
          #endif
      newText:
          uprintf("Did not match message %s on any command\n", received_command);
          }

          /* LAYER track (step 9 - mirrors the command track above): print the
           * matched layer index, or a miss. The index is sufficient for
           * debugging; printing the matched pattern would require capturing a
           * layer_map_t* pointer in the scan loops above, a dispatch change
           * not warranted for a Minor debug issue. GS was already substituted
           * to '|' by the loop above. */
          if (layer_found != LAYER_UNSET) {
              uprintf("Matched message %s on layer: %d\n", received_command, layer_found);
          } else {
              uprintf("Did not match message %s on any layer\n", received_command);
          }
          #endif
    (Preserve the exact leading whitespace/indent of the surrounding block: the
     uprintf lines are indented to match the command-track uprintf lines above.)
  - MIRROR pattern: the existing command-track if/else immediately above
        (same phrasing: "Matched message %s on <track>..." / "Did not match
        message %s on any <track>").
  - DO NOT: touch the command-track if/else, the GS->'|' for loop, the
        #ifdef/#endif lines, or the `return command_found != NULL || layer_found
        != LAYER_UNSET;` line. Do NOT move or duplicate the GS substitution.
  - DEPENDENCIES: Task 1 (print.h) is needed for the Level 3 compile-check;
        the notifier.c edit itself is independent of print.h at edit time.

Task 3: VALIDATE — normal gate + CONSOLE compile-check
  - RUN: ./run_notifier_stub_tests.sh  → expect exit 0, 0 FAIL, "PASSED".
  - RUN: the one-off contract compile-check (Validation Loop Level 3) → expect
        exit 0, no new warnings.
  - READ: confirm both gates green; if any warning/error, fix root cause before
        declaring done.
```

### Implementation Patterns & Key Details

```c
// PATTERN: the command-track if/else to MIRROR (existing code, do not change):
    if (command_found != NULL) {
        uprintf("Matched message %s on command: %s\n", received_command, command_found->pattern);
    } else {
        uprintf("Did not match message %s on any command\n", received_command);
    }

// NEW layer-track if/else (this task) — symmetric structure, %d not %s:
    if (layer_found != LAYER_UNSET) {
        uprintf("Matched message %s on layer: %d\n", received_command, layer_found);
    } else {
        uprintf("Did not match message %s on any layer\n", received_command);
    }

// WHY %d not %s: command_found is command_map_t* (has ->pattern, a string);
// layer_found is uint8_t (an index, or LAYER_UNSET==255). No layer_map_t*
// exists in scope. Printing the index is the contract-approved output.

// PATTERN: the print.h stub (variadic macro, NOT a function):
#pragma once
#include <stdio.h>
#define uprintf(...) printf(__VA_ARGS__)
// Forwards ALL args to printf. Works for every call site in notifier.c,
// including the "%u"/(unsigned) cast site at line 469.
```

### Integration Points

```yaml
NO database / config / route / migration changes. This is a debug-output + 
test-infra change only.

BUILD:
  - the committed gate run_notifier_stub_tests.sh is UNCHANGED and still builds
    notifier.c WITHOUT -DCONSOLE_ENABLE (so print.h is never included by it).
  - a NEW one-off manual command validates the CONSOLE path (see Validation
    Loop Level 3). Do NOT commit this as a script unless a later task asks;
    the contract (OUTPUT §4) keeps CONSOLE_ENABLE off in all automated gates.

INCLUDE PATH:
  - print.h is found via the existing '-Iqmk_stubs' flag already used by both
    run_notifier_stub_tests.sh and the one-off compile-check. No new -I needed.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# After creating qmk_stubs/print.h and editing notifier.c:
# (no ruff/mypy here — this is C. gcc -Wall -Wextra IS the style gate.)
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. -c notifier.c -o /tmp/notifier_style_check.o
# Expected: clean, exit 0, ZERO warnings. If warnings appear, READ them and fix
# (common cause: wrong indent under -Wextra is not warned, but a stray
# non-CONSOLE uprintf outside the #ifdef would error as 'uprintf undefined').

rm -f /tmp/notifier_style_check.o
```

### Level 2: Unit Tests (Component Validation)

```bash
# There is NO unit test for CONSOLE_ENABLE output (it is debug-only and not
# part of the automated gates by design). The "component test" is that the
# committed stub-compile gate still passes (it exercises notifier.c WITHOUT
# CONSOLE_ENABLE — proves the edit did not break the normal build):
./run_notifier_stub_tests.sh
# Expected: exit 0; output includes:
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0  (exit=0)
#   ✓ notifier stub-compile gate PASSED
# Baseline at research time: "Total tests run: 31 / passed: 31 / failed: 0".
# (If the parallel P1.M2.T2.S1 has landed, dispatch count may be 14 not 11;
#  either way both binaries must show 0 FAIL and exit 0.)
```

### Level 3: Integration Testing (System Validation) — THE contract compile-check

```bash
# THIS is the one-off manual command that validates the CONSOLE_ENABLE path
# (the whole reason print.h exists). Run it from the repo root after the edits:
gcc -Wall -Wextra -std=c99 -DCONSOLE_ENABLE \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. -c notifier.c -o /tmp/notifier_console_check.o
echo "CONSOLE compile-check exit=$?"
# Expected: exit 0, ZERO warnings/errors. This proves:
#   - qmk_stubs/print.h is found and provides uprintf
#   - ALL 5 pre-existing uprintf call sites + the 2 NEW layer-track calls
#     compile under CONSOLE_ENABLE
#   - no symbol/type mismatch (e.g. %d vs uint8_t is fine; %s vs char* fine)
ls -la /tmp/notifier_console_check.o   # object must exist and be non-empty
rm -f /tmp/notifier_console_check.o

# Smoke-check the object actually defines process_full_message (sanity):
gcc -Wall -Wextra -std=c99 -DCONSOLE_ENABLE \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. -c notifier.c -o /tmp/notifier_console_check.o && \
    nm /tmp/notifier_console_check.o | grep -i process_full_message
# Expected: a 'T' (text/defined) symbol line for process_full_message.
rm -f /tmp/notifier_console_check.o
```

### Level 4: Creative & Domain-Specific Validation

```bash
# (Optional, debug-only — NOT required for the gate.) Prove the new layer-track
# print actually fires by compiling a tiny driver that includes the stub
# notifier and drives a layer match through process_full_message, capturing
# stdout. This is BEYOND the contract (which only requires clean compile) but
# confirms runtime behavior if desired:
cat > /tmp/console_smoke.c <<'EOF'
#include <stdio.h>
#include <string.h>
#define CONSOLE_ENABLE 1          /* force the prints active */
#include "notifier.h"
extern bool process_full_message(char *data);
/* minimal map: a layer rule that matches "L5" -> layer 5 */
DEFINE_SERIAL_LAYERS({ {"L5", 5} });
int main(void){
    process_full_message("L5");   /* should print: Matched message L5 on layer: 5 */
    process_full_message("zzz");  /* should print: Did not match message zzz on any layer */
    return 0;
}
EOF
# Build against the stub-compiled notifier WITH CONSOLE_ENABLE (object-only
# compile of notifier.c, then link the smoke driver):
gcc -Wall -Wextra -std=c99 -DCONSOLE_ENABLE \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_con.o && \
gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/notifier_con.o qmk_stubs/qmk_stubs.c \
    /tmp/console_smoke.c -o /tmp/console_smoke && \
    /tmp/console_smoke
# Expected stdout includes:
#   Matched message L5 on layer: 5
#   Did not match message zzz on any layer
rm -f /tmp/notifier_con.o /tmp/console_smoke /tmp/console_smoke.c
# NOTE: this Level 4 step is OPTIONAL verification — the contract's required
# gate is Level 3 (clean compile) only. If the DEFINE_SERIAL_LAYERS accessor
# names or map shape make this driver not link trivially, SKIP Level 4; it is
# not a success criterion.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 style build (`gcc -Wall -Wextra` WITHOUT CONSOLE_ENABLE) → 0 warnings.
- [ ] Level 2 `./run_notifier_stub_tests.sh` → exit 0, both binaries 0 FAIL,
      `✓ notifier stub-compile gate PASSED`.
- [ ] Level 3 contract compile-check (`gcc -DCONSOLE_ENABLE ... -c notifier.c`)
      → exit 0, 0 warnings, `/tmp/notifier_console_check.o` produced.
- [ ] (Optional) Level 4 smoke driver prints the layer match + miss lines.

### Feature Validation

- [ ] `qmk_stubs/print.h` exists with `#pragma once` + `<stdio.h>` +
      `#define uprintf(...) printf(__VA_ARGS__)`.
- [ ] Layer-track `if/else` present INSIDE the `#ifdef CONSOLE_ENABLE` block,
      after the command-track `if/else`, before `#endif`.
- [ ] Command-track print, GS→`|` loop, `#ifdef`/`#endif`, and `return` are
      unchanged (diff shows only an insertion, no modifications to them).
- [ ] Layer print uses `%d` with `layer_found` (a `uint8_t`), NOT a pattern ptr.
- [ ] No production behavior change (all new code is `#ifdef CONSOLE_ENABLE`-guarded).

### Code Quality Validation

- [ ] `print.h` matches the `#pragma once` + stub-comment style of the other
      `qmk_stubs/*.h` headers.
- [ ] Layer-track comment explains WHY only the index is printed (mirrors
      findings Issue 3 rationale).
- [ ] Indentation of the inserted block matches the surrounding command-track block.
- [ ] No new dependencies, no new linkable symbols (macro only), no interface change.

### Documentation & Deployment

- [ ] No user-facing docs change (CONSOLE_ENABLE debug output is not a config
      surface; print.h is test infrastructure). Contract DOCS §5: "none".
- [ ] (Out of scope for this task — owned by P1.M3.T2.S1) README test-count /
      cross-cutting docs sync happens in a later subtask; do NOT do it here.

---

## Anti-Patterns to Avoid

- ❌ Don't anchor the notifier.c edit on line numbers (417-434 in the findings
  doc are stale — P1.M2.T1.S1 shifted them). Anchor on the command-track
  `uprintf` text.
- ❌ Don't print `layer_entry->pattern` — no `layer_map_t*` is in scope. Use the
  `uint8_t layer_found` index with `%d`.
- ❌ Don't declare `uprintf` as a function in print.h — it must be a variadic
  MACRO (`#define uprintf(...) printf(__VA_ARGS__)`) so all call sites forward.
- ❌ Don't place the layer-track print OUTSIDE the `#ifdef CONSOLE_ENABLE` /
  `#endif` — that would make the normal gate fail (uprintf undefined without
  print.h, which is only included under CONSOLE_ENABLE).
- ❌ Don't re-run or duplicate the GS→`|` substitution loop — it already ran on
  `received_command` above; the layer print just reuses the substituted buffer.
- ❌ Don't add `-DCONSOLE_ENABLE` to `run_notifier_stub_tests.sh` — the contract
  requires CONSOLE_ENABLE stay OFF in all automated gates; the CONSOLE path is
  checked by the one-off Level 3 command only.
- ❌ Don't touch the command-track `if/else`, the dispatch/scan loops, or the
  `return` statement — this is a pure additive debug-print.
- ❌ Don't catch/suppress compile warnings — `-Wall -Wextra` must be clean in
  BOTH builds.