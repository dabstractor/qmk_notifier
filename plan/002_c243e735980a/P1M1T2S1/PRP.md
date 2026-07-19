# PRP — P1.M1.T2.S1: `qmk_stubs/os_detection.h` (the `os_variant_t` stub)

## Goal

**Feature Goal**: Create the minimal host-test stub header
`qmk_stubs/os_detection.h` that provides **only** the `os_variant_t` enum type —
the single QMK type this module consumes — so that `notifier.h`/`notifier.c`
(which `#include "os_detection.h"`, per sibling task P1.M1.T1.S1) compile under
the host stub harness (`-Iqmk_stubs`) with plain gcc.

**Deliverable**: The new file `qmk_stubs/os_detection.h`
(`/home/dustin/projects/qmk-notifier/qmk_stubs/os_detection.h`), ~30 lines:
`#pragma once`, `#include <stdint.h>`, a `/* … */` header comment documenting
that this is a minimal type-only stub, and the `os_variant_t` enum **verbatim**
matching QMK's `quantum/os_detection.h`.

**Success Definition**:
- File exists at `qmk_stubs/os_detection.h`.
- `gcc -fsyntax-only -Wall -Wextra -std=c99 -x c qmk_stubs/os_detection.h` is clean.
- The enum defines exactly `OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3,
  OS_IOS=4` (verified by C99 compile-time asserts).
- The file contains **exactly one** `#include` directive (`<stdint.h>`); it does
  NOT `#include "usb_device_state.h"` and declares NO functions.
- `notifier.c` compiles AND `test_notifier_dispatch` links + runs **11/11 passed**
  with `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.` (corrected-flag
  harness below; mirrors what `run_notifier_stub_tests.sh` will be after P1.M2.T2.S1).

## User Persona (if applicable)

**Target User**: The host test harness — this file IS the mock. It is `#include`d
by `notifier.h`, which is `#include`d by `notifier.c` and the `test_notifier_*`
suites, resolved via `-Iqmk_stubs`.

**Use Case**: Host-compiling `notifier.c` (which depends on QMK symbols) with the
`qmk_stubs/` harness instead of the real QMK tree. The module needs the
`os_variant_t` *type* (for `notifier_set_os(os_variant_t)` and `current_os`), but
never calls `detected_host_os()`, so the stub supplies the type and nothing else.

**User Journey**: keymap/test `#include "notifier.h"` → `notifier.h` does
`#include "os_detection.h"` → host build finds `qmk_stubs/os_detection.h` via
`-Iqmk_stubs` → `os_variant_t` is defined → compilation proceeds. In a **real**
QMK build, the genuine `quantum/os_detection.h` is pulled in transitively via
`#include QMK_KEYBOARD_H` and provides the full (richer) header; this stub is
never seen.

**Pain Points Addressed**: Removes the host build's only remaining blocker for
the multi-OS API surface (P1.M1.T1.S1) without dragging in QMK's
`usb_device_state.h` or the OS-detection `.c` (which would cascade into
unresolvable symbols).

## Why

- **Leaf dependency**: it is the one file both P1.M1.T1.S1 (the header) and
  P1.M1.T3.S1 (`notifier.c` core) consume. Without it, `notifier.h` does not
  compile in the host harness (it currently references an unresolved
  `os_detection.h`).
- **Zero link cost**: by providing the type only and refusing to declare
  `detected_host_os()` etc., the module keeps invariant 17 / §2 F8.2 (the OS is
  *pushed in* by the keymap via `notifier_set_os`; the module never *pulls* it).
- **Faithful parity**: the enum values/names must match QMK exactly because the
  enumerator *names* are preprocessor tokens fed to `##os` in
  `DEFINE_SERIAL_*_OS` (generating `_notifier_get_command_map_OS_LINUX`, …) and
  the *values* underpin `current_os == OS_UNSURE` boot semantics.

## What

A header-only C file with, in order:
1. `#pragma once`.
2. `#include <stdint.h>` (matches the sibling stubs and the real header; harmless;
   the enum itself needs no stdint but item spec point 3 mandates it for parity).
3. A `/* … */` comment block stating: this is a **minimal host-test stub**
   providing only the `os_variant_t` TYPE; the module uses the type/enumerator
   names but **never** calls `detected_host_os()` (invariant 17 / §2 F8.2); it
   deliberately omits `#include "usb_device_state.h"` (architecture **F4**) and
   all function declarations; the real header is `quantum/os_detection.h`, pulled
   in transitively via `QMK_KEYBOARD_H` in production.
4. The `os_variant_t` enum, verbatim from QMK, with `/* = N */` inline comments.

### Success Criteria

- [ ] `qmk_stubs/os_detection.h` created (new file).
- [ ] `#pragma once` first; exactly one `#include` (`<stdint.h>`).
- [ ] Enum: `OS_UNSURE=0 … OS_IOS=4` (compile-time asserted).
- [ ] NO `#include "usb_device_state.h"`; NO function declarations.
- [ ] Header comment covers the required doc points (item spec §5).
- [ ] `notifier.c` + `test_notifier_dispatch` build & run 11/11 with the stub.

## All Needed Context

### Context Completeness Check

**Pass.** The exact file content (verbatim enum, exact comment requirements,
exact includes) is specified inline below and was **empirically validated during
research**: the stub parses clean, the enum values/names are asserted, and a full
`notifier.c` stub-compile + `test_notifier_dispatch` link+run passed 11/11 with
the stub in place. An implementer with only this PRP + repo can produce the file
byte-for-behavior-identically and prove it.

### Documentation & References

```yaml
# MUST READ — the authoritative enum + the things to OMIT
- file: plan/002_c243e735980a/architecture/external_deps.md
  section: "## QMK os_detection.h — VERIFIED from qmk/qmk_firmware"
  why: "Authoritative os_variant_t enum (OS_UNSURE=0..OS_IOS=4), the
        process_detected_host_os_kb hook signature, the 'detected_host_os() is the
        function the module must NOT call' rule, and the explicit note that the
        stub does NOT need usb_device_state.h."
  critical: "Enum values/names are the load-bearing contract (##os token-paste +
             current_os==OS_UNSURE). Reproduce verbatim."

- file: plan/002_c243e735980a/architecture/findings_and_risks.md
  section: "### F4. The stub os_detection.h must be minimal (NOT include usb_device_state.h)"
  why: "States the #1 gotcha: the real header starts with #include \"usb_device_state.h\";
        adding it to the stub cascades into undefined types (struct usb_device_state)
        in the host harness."
  critical: "Do NOT copy the real header's includes verbatim — only <stdint.h>."

- file: plan/002_c243e735980a/P1M1T1S1/PRP.md   (and the landed notifier.h)
  section: "Task 1: INSERT #include \"os_detection.h\""
  why: "The CONSUMER contract. notifier.h now does #include \"os_detection.h\" right
        after <stdbool.h>, and declares void notifier_set_os(os_variant_t os). This
        stub must make that include resolve and define os_variant_t."
  critical: "notifier.h is already modified in the working tree (P1.M1.T1.S1 landed);
             this stub is the missing piece that lets it compile. Do NOT modify notifier.h."

- file: PRD.md   (snapshot: plan/002_c243e735980a/prd_snapshot.md)
  section: "### 5.1 Type aliases & structs" (and "### 2 Functional Requirements → F8")
  why: "§5.1 shows the #include \"os_detection.h\" + header-only rationale comment;
        F8.1 says os_variant_t is 'reused as-is, not redefined'; F8.2 says the module
        never calls detected_host_os(); F8.6 says OS_UNSURE is the boot state (value 0)."
  critical: "Reuse the type as-is — do NOT re-define a local os_variant_t in notifier.h.
             This stub is what 'reuse as-is' resolves to in the host harness."

- file: PRD.md
  section: "### 16. Appendix B — Constants Reference"
  why: "Lists the os_variant_t mapping (OS_UNSURE/0 .. OS_IOS/4) as a project constant."
  critical: "Same value mapping as the enum — cross-check source of truth."

- url: https://github.com/qmk/qmk_firmware/blob/master/quantum/os_detection.h
  why: "The genuine upstream header. Verified verbatim during research (it begins
        #pragma once / <stdint.h> / <stdbool.h> / #include \"usb_device_state.h\",
        then the enum, then detected_host_os()/process_detected_host_os_kb()/
        os_detection_task()/process_wlength()/... declarations)."
  critical: "Our stub intentionally diverges: keep ONLY <stdint.h> + the enum. Do NOT
             copy the real header's other includes or function decls."

# Pattern to follow (existing sibling stubs)
- file: qmk_stubs/qmk_keyboard_stub.h
  why: "Style template: #pragma once → #include <stdint.h> (+ <stddef.h>/<stdbool.h>) →
        a /* Minimal QMK surface ... */ purpose comment → declarations."
  pattern: "Header stub = pragma + minimal includes + one purpose comment + content."
  gotcha: "qmk_keyboard_stub.h pulls <stdint.h>,<stddef.h>,<stdbool.h> because it
           declares functions using those types. os_detection.h needs only <stdint.h>
           (the enum uses no bool/size_t) — keep it minimal per item spec point 3."

- file: qmk_stubs/raw_hid.h
  why: "Smallest existing stub (pragma + <stdint.h> + one /* */ comment + one decl).
        Closest structural analog to what we are writing."
  pattern: "Minimal-header stub shape."

# Consumer / validator
- file: run_notifier_stub_tests.sh
  why: "The host stub-compile gate. Shows the exact flags: [1/3] compile uses
        -DQMK_KEYBOARD_H='\"qmk_keyboard_stub.h\"' -Iqmk_stubs -I.; [2/3] link uses
        ONLY -I. today."
  critical: "GOTCHA: the current [2/3] link step lacks -Iqmk_stubs, so it will FAIL to
             find os_detection.h even after this stub lands. That is EXPECTED and is
             fixed by sibling task P1.M2.T2.S1 (extend the runner). Validate THIS task
             with the corrected-flag harness in Validation Level 2, not the un-extended
             runner. See 'Sequencing risk' below."

- file: test_notifier_dispatch.c
  why: "Existing host suite that #include \"notifier.h\" (→ os_detection.h) and uses
        DEFINE_SERIAL_*. It is the integration target that proves the stub works."
```

### Current Codebase tree (relevant slice)

```bash
notifier.h                  # already modified by P1.M1.T1.S1 (has #include "os_detection.h"
                            #   + notifier_set_os decl + DEFINE_SERIAL_*_OS). DO NOT TOUCH.
notifier.c                  # consumer (P1.M1.T3 changes it). DO NOT TOUCH now.
pattern_match.{c,h}         # unaffected.
qmk_stubs/
  qmk_keyboard_stub.h       # QMK_KEYBOARD_H stand-in (style reference). DO NOT TOUCH.
  raw_hid.h                 # raw_hid_send decl stub (style reference). DO NOT TOUCH.
  qmk_stubs.c               # layer_on/off + raw_hid_send impls. DO NOT TOUCH.
  os_detection.h            # ← CREATE (this task). Does NOT exist yet.
test_notifier_dispatch.c    # integration validator (#include "notifier.h"). DO NOT TOUCH.
run_notifier_stub_tests.sh  # stub gate; link step needs -Iqmk_stubs (P1.M2.T2.S1). DO NOT TOUCH.
run_all_tests.sh            # 9-suite pattern_match corpus — unaffected (links pattern_match.c only).
PRD.md                      # READ-ONLY.
plan/002_c243e735980a/      # this plan — architecture/, prd_snapshot.md, tasks.json.
```

### Desired Codebase tree with files to be added

```bash
qmk_stubs/os_detection.h    # NEW. Minimal type-only stub of quantum/os_detection.h:
                            #   #pragma once + <stdint.h> + purpose comment + os_variant_t enum.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (F4): Do NOT add #include "usb_device_state.h". The REAL header starts
//   with it, but in the host harness it cascades into undefined 'struct
//   usb_device_state' and pulls in EEPROM/timer/USB-state types the stubs don't
//   provide. The stub provides the enum ONLY.

// CRITICAL (invariant 17 / §2 F8.2): Do NOT declare detected_host_os(),
//   process_detected_host_os_kb/_user, os_detection_task(), process_wlength(),
//   erase_wlength_data(), os_detection_notify_usb_device_state_change(), or
//   slave_update_detected_host_os(). Declaring any of them would obligate the
//   linker to resolve their implementations (in quantum/os_detection.c), which
//   the stub harness does NOT compile. The module never calls them anyway.

// GOTCHA (enum is the load-bearing contract): the enumerator NAMES are
//   preprocessor tokens fed to ##os in DEFINE_SERIAL_*_OS → they generate
//   _notifier_get_command_map_OS_LINUX etc., which notifier.c §8.3 must match
//   exactly. The VALUES back current_os==OS_UNSURE boot semantics (F8.6). Do not
//   reorder, rename, or renumber. Reproduce QMK's order verbatim.

// GOTCHA (minimal includes): use ONLY #include <stdint.h>. The enum needs no
//   <stdbool.h> (it has no bool field) and no <stddef.h>. Item spec point 3
//   mandates <stdint.h> for parity with sibling stubs / the real header. Adding
//   more is harmless but unnecessary and breaks the "minimal stub" intent.

// GOTCHA (sequencing / runner): the CURRENT run_notifier_stub_tests.sh [2/3] link
//   step has only -I. (not -Iqmk_stubs), so it will FAIL to locate os_detection.h
//   even after this stub is correctly placed. This is NOT a stub defect — sibling
//   task P1.M2.T2.S1 adds -Iqmk_stubs to the link step. Until then, validate with
//   the corrected-flag harness (Validation Level 2), which mirrors the post-fix
//   runner and proves the stub itself is correct.

// GOTCHA (comment grep false positives): the header comment LEGITIMATELY mentions
//   "usb_device_state.h" and "detected_host_os()" to document what is OMITTED.
//   When checking "forbidden content", grep for actual #include DIRECTIVES and
//   function DECLARATIONS (non-comment lines), not comment prose — see Level 4.
```

## Implementation Blueprint

### Data models and structure

One type only — the `os_variant_t` enum. No structs, no macros, no function
declarations. (`command_map_t`/`layer_map_t`/`callback_t` live in `notifier.h`;
`current_os` lives in `notifier.c`.)

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: CREATE qmk_stubs/os_detection.h
  - CREATE the file with EXACTLY this content, in this order:
      1. #pragma once
      2. #include <stdint.h>
      3. blank line
      4. the /* ... */ header comment (see "Exact comment block" below — covers all
         required doc points: minimal type-only stub; module never calls
         detected_host_os() (invariant 17 / §2 F8.2); omits usb_device_state.h (F4)
         and all function decls; real header = quantum/os_detection.h, pulled in
         transitively via QMK_KEYBOARD_H in production).
      5. the os_variant_t enum VERBATIM (see "Exact enum" below, with /* = N */ comments).
  - NAMING: file lowercase snake_case os_detection.h (matches the #include token in
      notifier.h and the real QMK path stem). Type os_variant_t (QMK name, reused as-is).
  - PLACEMENT: qmk_stubs/ directory (so -Iqmk_stubs resolves it; mirrors sibling stubs).
  - DO NOT add: <stdbool.h>, <stddef.h>, "usb_device_state.h", or ANY function decl.
  - DEPENDENCIES: none — this is the leaf.
```

**Exact enum (reproduce verbatim — verified against QMK + PRD §5.1/§16):**

```c
typedef enum {
    OS_UNSURE,   /* = 0  — boot state; no OS-specific map (§2 F8.6) */
    OS_LINUX,    /* = 1 */
    OS_WINDOWS,  /* = 2 */
    OS_MACOS,    /* = 3 */
    OS_IOS,      /* = 4 */
} os_variant_t;
```

**Exact comment block (covers all item-spec §5 doc points):**

```c
/*
 * Minimal host-test STUB of QMK's quantum/os_detection.h.
 *
 * Provides ONLY the os_variant_t TYPE — the single thing this module consumes.
 * The module uses the type (and the OS_UNSURE/OS_LINUX/OS_WINDOWS/OS_MACOS/OS_IOS
 * enumerator NAMES) but NEVER calls detected_host_os() or any other function
 * declared in the real header (invariant 17 / §13; §2 F8.2). The OS is PUSHED
 * in by the keymap via notifier_set_os(), so there is NO link dependency on the
 * OS-detection .c subsystem.
 *
 * This file deliberately OMITS:
 *   - #include "usb_device_state.h"  (the real header starts with it, but it
 *     would cascade into undefined types in the host harness — architecture F4)
 *   - detected_host_os() / process_detected_host_os_kb() / os_detection_task()
 *     / process_wlength() / etc. declarations (declaring them would require
 *     linking their implementations, which the stub harness does not provide).
 *
 * In a real QMK build, the genuine quantum/os_detection.h is pulled in
 * transitively via #include QMK_KEYBOARD_H and provides the full, richer header.
 * This stub exists solely so notifier.h/notifier.c (which #include
 * "os_detection.h") compile under the host harness with -Iqmk_stubs.
 */
```

> **Style note:** the sibling stubs use `/* … */` blocks for purpose comments, so
> this style is consistent. The `/* = N */` trailing comments on each enumerator
> are item-spec point 3's exact form — keep them.

### Implementation Patterns & Key Details

```c
// The whole file is: pragma + one include + one comment + one enum. Nothing else.

// ANTI-PATTERN: do NOT "be faithful to QMK" by copying the real header's other
//   includes/decls. Faithfulness here = the ENUM VERBATIM, nothing more. Every
//   other line of the real header is intentionally dropped (F4 + invariant 17).

// ANTI-PATTERN: do NOT add include guards (#ifndef). #pragma once is the convention
//   used by every file in qmk_stubs/ and by the real QMK header.

// ANTI-PATTERN: do NOT typedef a struct or add macros. The module's only need is
//   the enum type. Adding anything else risks the cascade F4 warns about.

// WHY <stdint.h> and not <stdbool.h>: the enum has no bool field. <stdint.h>
//   matches item spec point 3 and the sibling stubs; it is harmless (no symbols
//   pulled, header-only). The real header adds <stdbool.h> only because it
//   declares bool-returning functions — we declare none.
```

### Integration Points

```yaml
HEADERS:
  - create: qmk_stubs/os_detection.h
  - consumed by: notifier.h (#include "os_detection.h" — already present post P1.M1.T1.S1)
  - resolved via: -Iqmk_stubs (compile step already has it; link step gets it in P1.M2.T2.S1)
BUILD:
  - No build-system change. No rules.mk edit. The stub is header-only and discovered
    by the existing -Iqmk_stubs flag in the stub-compile path.
  - Real QMK builds never see this file (genuine quantum/os_detection.h wins via
    QMK_KEYBOARD_H transitive include).
CONFIG / DATABASE / ROUTES:
  - N/A.
```

## Validation Loop

> Toolchain: gcc (no ruff/mypy/pytest — C project). All commands below were
> **executed during research against a temp copy of the proposed stub and PASSED**.
> Note the sequencing caveat: the *current* `run_notifier_stub_tests.sh` link step
> lacks `-Iqmk_stubs`; validate with the corrected-flag harness (Level 2), which is
> exactly what the runner becomes after P1.M2.T2.S1.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. The stub header parses cleanly as a standalone translation unit.
gcc -fsyntax-only -Wall -Wextra -std=c99 -x c qmk_stubs/os_detection.h \
    -Wno-pragma-once-outside-header
# Expected: no output, exit 0. (-Wno-pragma-once-outside-header silences the benign
# warning gcc emits when a #pragma-once header is the main file.)

# 1b. Required scaffolding present.
grep -q '#pragma once'            qmk_stubs/os_detection.h && echo "pragma ok"
grep -q '#include <stdint.h>'     qmk_stubs/os_detection.h && echo "stdint ok"
grep -q 'typedef enum'            qmk_stubs/os_detection.h && echo "enum ok"
grep -q '} os_variant_t;'         qmk_stubs/os_detection.h && echo "typedef ok"
# Expected: all four "ok".

# 1c. Exactly ONE #include directive (the minimal-stub invariant).
test "$(grep -vE '^[[:space:]]*(//|\*)' qmk_stubs/os_detection.h | grep -cE '#[[:space:]]*include')" -eq 1 \
  && echo "exactly one #include (good)" || echo "WRONG #include count"
# Expected: "exactly one #include (good)".
```

### Level 2: Enum Contract + Full Integration (the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. Compile-time enum-value assertions (C99 — no C11 static_assert needed; the
#     build uses -std=c99). If any value is wrong, the negative array size fails.
cat > /tmp/os_enum_check.c <<'EOF'
#include "os_detection.h"
typedef int c0[(OS_UNSURE  == 0) ? 1 : -1];
typedef int c1[(OS_LINUX   == 1) ? 1 : -1];
typedef int c2[(OS_WINDOWS == 2) ? 1 : -1];
typedef int c3[(OS_MACOS   == 3) ? 1 : -1];
typedef int c4[(OS_IOS     == 4) ? 1 : -1];
int main(void){ os_variant_t os = OS_UNSURE; return (int)os; }
EOF
gcc -Wall -Wextra -std=c99 -Iqmk_stubs /tmp/os_enum_check.c -o /tmp/os_enum_check \
  && /tmp/os_enum_check ; echo "enum exit=$? (expect 0) -> values 0..4 confirmed"

# 2b. THE integration gate: stub-compile notifier.c, link test_notifier_dispatch,
#     run. Uses -Iqmk_stubs on BOTH steps (mirrors the runner AFTER P1.M2.T2.S1;
#     the current runner's link step lacks it — see Sequencing risk).
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o \
  && gcc -Wall -std=c99 -Iqmk_stubs -I. \
        /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
        -o /tmp/test_notifier_dispatch_val \
  && /tmp/test_notifier_dispatch_val
fails=$(/tmp/test_notifier_dispatch_val 2>/dev/null | grep -c '^FAIL:' || true)
echo "dispatch fails=$fails  (expect 0)"
rm -f /tmp/os_enum_check.c /tmp/os_enum_check /tmp/notifier_stub.o /tmp/test_notifier_dispatch_val
# Expected: enum exit=0; dispatch prints "Total tests run: 11 / passed: 11 / failed: 0";
#           fails=0. This proves the stub makes the real consumer + suite pass.
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# The FULL run_notifier_stub_tests.sh end-to-end pass is deferred to P1.M2.T2.S1
# (it extends the runner's link step with -Iqmk_stubs). Running it NOW is EXPECTED
# to fail at the [2/3] link step with "os_detection.h: No such file or directory"
# ONLY because of that missing flag — NOT because of this stub (Level 2c above
# proves the stub is correct with the proper flag). After P1.M2.T2.S1 lands:
#     ./run_notifier_stub_tests.sh   # -> "✓ notifier stub-compile gate PASSED"

# Diff hygiene: only the new stub file was added (plus your PRP/research under plan/).
git status --porcelain
# Expected: ?? qmk_stubs/os_detection.h  (and plan/002.../P1M1T2S1/{PRP.md,research/}).
#           No modifications to notifier.h, notifier.c, tests, or run_*.sh.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Forbidden-content checks at the DIRECTIVE / DECLARATION level (ignore comment
#     prose — the header comment legitimately names what it omits).
#   (i) NO real #include of usb_device_state.h:
! grep -vE '^[[:space:]]*(//|\*)' qmk_stubs/os_detection.h \
     | grep -qE '#[[:space:]]*include[[:space:]]*"usb_device_state.h"' \
  && echo "no usb_device_state.h include (good)"
#   (ii) NO function declarations (only the enum typedef should be a top-level decl):
! grep -vE '^[[:space:]]*(//|\*)' qmk_stubs/os_detection.h \
     | grep -qE '^\s*(void|bool|os_variant_t|uint[0-9]+_t|int|char)\s+\w+\s*\([^)]*\)\s*;' \
  && echo "no function declarations (good)"

# 4b. Required doc points present (item spec §5).
for needle in "minimal" "host-test" "os_variant_t" "detected_host_os" "usb_device_state.h" \
              "quantum/os_detection.h" "QMK_KEYBOARD_H"; do
  grep -qiF "$needle" qmk_stubs/os_detection.h && echo "doc present: $needle" \
    || { echo "MISSING doc token: $needle"; exit 1; }
done

# 4c. Cross-check enum against the architecture source of truth (external_deps.md).
diff <(grep -A6 'typedef enum' qmk_stubs/os_detection.h | sed 's/[[:space:]]*\/\*.*//') \
     <(sed -n '/### The .os_variant_t. enum/,/^```/p' plan/002_c243e735980a/architecture/external_deps.md \
        | grep -E 'OS_|os_variant_t' | sed 's@//.*@@')
# Expected: no diff lines (enumerator names match the verified source of truth).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -fsyntax-only -Wall -Wextra -std=c99 -x c qmk_stubs/os_detection.h` clean.
- [ ] Level 1: exactly one `#include` directive (`<stdint.h>`); pragma + enum present.
- [ ] Level 2: enum asserts compile (values 0..4); `notifier.c`+`test_notifier_dispatch` 11/11 pass.
- [ ] Level 3: `git status` shows only the new stub (+ plan/ PRP/research).
- [ ] Level 4: no usb_device_state.h include; no function decls; all doc tokens present.

### Feature Validation

- [ ] `qmk_stubs/os_detection.h` exists with `#pragma once` + `<stdint.h>` + enum.
- [ ] Enum is `OS_UNSURE=0 … OS_IOS=4`, verbatim vs QMK / PRD §5.1/§16.
- [ ] Header comment documents: minimal type-only stub, never calls detected_host_os
      (invariant 17 / §2 F8.2), omits usb_device_state.h (F4) + function decls, real
      header is quantum/os_detection.h (pulled via QMK_KEYBOARD_H in production).
- [ ] `notifier.c` (consumer) compiles and `test_notifier_dispatch` passes with the stub.

### Code Quality Validation

- [ ] Matches sibling-stub style (`#pragma once` + includes + `/* … */` purpose comment).
- [ ] Minimal — only the enum; no extra includes/decls/macros/structs.
- [ ] No modification to notifier.h, notifier.c, qmk_stubs/*.{h,c}, test_*.c,
      run_*.sh, PRD.md, tasks.json, prd_snapshot.md, .gitignore.

### Documentation & Deployment

- [ ] Header comment is self-documenting (Mode A) — no separate docs file needed.
- [ ] Sequencing dependency on P1.M2.T2.S1 (runner `-Iqmk_stubs` on link step) documented.

---

## Anti-Patterns to Avoid

- ❌ Don't copy the real QMK header verbatim — it drags in `usb_device_state.h`
  and declares functions you'd then have to link. Keep ONLY `<stdint.h>` + the enum.
- ❌ Don't add `#include "usb_device_state.h"` "to match QMK" — architecture F4:
  it cascades into undefined `struct usb_device_state` in the host harness.
- ❌ Don't declare `detected_host_os()` / `process_detected_host_os_kb()` / etc. —
  invariant 17 / §2 F8.2 (module never calls them) and declaring them forces linking.
- ❌ Don't renumber/rename/reorder the enumerators — names feed `##os` (linkage
  contract with notifier.c §8.3); values back `current_os == OS_UNSURE` (F8.6).
- ❌ Don't add `#include <stdbool.h>`/`<stddef.h>` or include guards — minimal stub;
  use `#pragma once` + `<stdint.h>` only (item spec point 3).
- ❌ Don't run the *current* `run_notifier_stub_tests.sh` and treat a link-step
  failure as a stub bug — its link step lacks `-Iqmk_stubs` until P1.M2.T2.S1.
  Use the Level-2 corrected-flag harness (which is what the runner will become).
- ❌ Don't touch notifier.h/notifier.c/qmk_stubs siblings/tests/run_*.sh/PRD/tasks.json.

---

## Confidence Score: 10/10

The deliverable is a ~30-line header whose exact content (verbatim enum from QMK's
`quantum/os_detection.h`, exact includes, exact doc requirements) is fully specified
above and was **empirically validated during research**: the stub parses clean
under `-Wall -Wextra -std=c99`, its enum values/names are compile-time asserted
(with a negative control proving the asserts are live), and — decisively — a full
`notifier.c` stub-compile + `test_notifier_dispatch` link+run passed **11/11** with
the stub in place. The single sequencing caveat (the current runner's link step
needs `-Iqmk_stubs`, added by sibling task P1.M2.T2.S1) is explicitly documented
and handled with a corrected-flag harness that does not depend on it. No external
dependencies are added; scope boundaries with all sibling tasks are explicit.