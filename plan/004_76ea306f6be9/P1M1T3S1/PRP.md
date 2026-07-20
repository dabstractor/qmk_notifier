# PRP — P1.M1.T3.S1: Add the `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` guard to `notifier.c`

> **Repo note (read first):** This task belongs to the **firmware module** repo
> `/home/dustin/projects/qmk_notifier` (underscore) — that is where `notifier.c`,
> `run_notifier_stub_tests.sh`, the plan's `architecture/`, the sibling
> `P1M1T2S1/PRP.md`, and `tasks.json` live. All paths below are relative to that
> repo root. (A sibling repo `/home/dustin/projects/qmk-notifier` hyphen is the
> Rust transport crate — unrelated; do not edit there.)

## Goal

**Feature Goal**: Add a **compile-time API-version assertion** to the top of
`notifier.c` so that, when this module is built as a **QMK Community Module**
(PRD §18), the build fails early if the host QMK's Community-Module API is older
than the minimum the module requires (1.0.0). The assertion is `#ifdef`-guarded
on `COMMUNITY_MODULES_API_VERSION` so it is a **no-op in the host/stub test
harness** (where that symbol is undefined), leaving all stub binaries byte-for-byte
unchanged. This is requirement **R3** of PRD §18.3.

**Deliverable**: A single additive block inserted into `notifier.c` immediately
after `#include QMK_KEYBOARD_H` (line 2) and before the `/* Cap the Thompson NFA
… */` comment block (line 4): one Mode-A documentation comment, an
`#ifdef COMMUNITY_MODULES_API_VERSION`, a single
`ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);` line, and `#endif`. **No
other file is touched.**

**Success Definition**:
- `notifier.c` contains the block above at the specified site (after line 2,
  before the NFA comment), verbatim (including the `1, 0, 0` spacing).
- The block is `#ifdef`-guarded on `COMMUNITY_MODULES_API_VERSION` (NOT on
  anything else, and NOT unguarded).
- `./run_notifier_stub_tests.sh` still ends `✓ notifier stub-compile gate PASSED`
  with `test_notifier_dispatch`, `test_notifier_os`, and `test_notifier_host`
  **each reporting `fails=0`** — the direct proof (item OUTPUT) that the guard did
  not perturb the stub build.
- No edits to `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `qmk_module.json`,
  `rules.mk`, `README.md`, any test file, or `run_notifier_stub_tests.sh`.

## User Persona (if applicable)

**Target User**: (1) A future **Community-Module build** of this module (PRD §18)
— there, `QMK_KEYBOARD_H → quantum.h → community_modules.h` defines
`COMMUNITY_MODULES_API_VERSION` and `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION`
(emitted by `lib/python/qmk/cli/generate/community_modules.py:252-254`), so the
`STATIC_ASSERT` fires and rejects a too-old QMK at compile time. (2) The
**host/stub test harness** today — where neither symbol is defined and the
`#ifdef` harmlessly skips the block.

**Use Case**: A user clones this module into a QMK userspace whose QMK checkout
predates the module API; the build fails with a clear
`"Community module requires a newer version of QMK modules API -- needs: 1.0.0 …"`
message instead of a confusing later error.

**User Journey**: QMK build generator runs → emits the API-version macros into the
generated community_modules header → `notifier.c`'s top-of-file `#ifdef` expands
the assert → `STATIC_ASSERT(1.0.0 <= build_version)` → passes (build proceeds) or
fails (compile error, clear message).

**Pain Points Addressed**: Silent breakage on an old QMK checkout (missing
`housekeeping_task` / `process_detected_host_os` hooks the module relies on) — now
a loud, version-pinned compile-time error.

## Why

- **R3 of the Community-Module migration (§18.3)**: the third build-system
  plumbing change (R1 manifest, R2 rules.mk, **R3 this assert**). It pins the
  module to the API floor it actually needs.
- **1.0.0 is the floor**: `data/constants/module_hooks/1.0.0.hjson` provides
  `housekeeping_task` (line 2) and `process_detected_host_os` (line 19) — the two
  module hooks this firmware depends on (`notifier_set_os` is called from
  `process_detected_host_os_kb`, §8.7). Verified from `qmk_firmware` source.
- **Self-isolating in tests**: the `#ifdef` makes the guard invisible to the host
  stub harness, so the §11 acceptance gate stays green with zero behavioral
  change (R6). No mocking, no test edits required.

## What

One additive block at the top of `notifier.c` (insert between the existing line 2
`#include QMK_KEYBOARD_H` and line 4 `/* Cap the Thompson NFA …`). The exact text
is given in the Implementation Blueprint. No deletions, no restyle, no other
file changes.

### Success Criteria

- [ ] The guard block is present at the specified site, verbatim.
- [ ] It is `#ifdef`-guarded on exactly `COMMUNITY_MODULES_API_VERSION`.
- [ ] Target is `1, 0, 0`.
- [ ] `./run_notifier_stub_tests.sh` → `✓ notifier stub-compile gate PASSED`;
      dispatch / os / host each `fails=0`.
- [ ] `notifier.c` compiles in the stub harness with **0 new warnings**
      (the `#ifdef` is skipped — neither symbol is defined there).
- [ ] No other repository file is modified.

## All Needed Context

### Context Completeness Check

**Pass.** The exact block text, the exact insertion anchor (a unique line), the
exact target version and its justification (verified against
`qmk_firmware/data/constants/module_hooks/1.0.0.hjson`), the generator source of
the macros (`community_modules.py:252-254`, read verbatim), and the validation
command were all **empirically validated during research**: applying the guard to
a `/tmp` copy of `notifier.c` compiled with 0 warnings and re-linked all three
stub drivers green (dispatch/os/host each `fails=0`); a real-build simulation
(symbols defined) confirmed `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0)`
compiles (STATIC_ASSERT satisfied). An implementer with only this PRP + the repo
can make the single insertion correctly with no guessing.

### Documentation & References

```yaml
# MUST READ — the requirement this implements (R3) + the guard's exact form
- file: plan/004_76ea306f6be9/prd_snapshot.md   (also PRD.md)
  section: "### 18.3 Requirements -> R3"   (and "### 18.5 Acceptance")
  why: "Authoritative R3 wording: the #ifdef, the (1,0,0) target, why it is
        host-test-guarded, and the §18.5 acceptance bullet that the host gates
        stay green with the guard in place."
  critical: "Reproduce the #ifdef + ASSERT line verbatim. The guard symbol is
             COMMUNITY_MODULES_API_VERSION (not MODULES_API_VERSION, not version).

# Ground-truth generator source (verified from qmk_firmware)
- docfile: plan/004_76ea306f6be9/architecture/external_deps.md
  section: "## API Version Assertion (§18.3 R3)"
  why: "Quotes community_modules.py:252-254 verbatim: COMMUNITY_MODULES_API_VERSION
        and ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(maj,min,pat) -> STATIC_ASSERT(
        BUILDER(maj,min,pat) <= COMMUNITY_MODULES_API_VERSION, ...). States the
        stub-safety invariant and that 1.0.0 is the floor (housekeeping_task +
        process_detected_host_os)."
  critical: "The assert is a COMPILE-TIME STATIC_ASSERT (negative-array-size on
             failure), not a runtime check. In stub context neither symbol is
             defined -> the #ifdef skips it. Do NOT define the symbols in the stub."

# The file under edit (target + anchor)
- file: notifier.c
  why: "Line 2 '#include QMK_KEYBOARD_H' is the insertion anchor (unique in the
        file). The block goes immediately after it, before the '/* Cap the
        Thompson NFA ...' comment at line 4."
  pattern: "Top-of-file order today: (1) '// notifier.c', (2) '#include QMK_KEYBOARD_H',
            (3) blank, (4-13) NFA comment, (14) '#define NFA_MAX_PATTERN 128', (16)
            '#include \"pattern_match.c\"'. The guard slots into the line-3 region."
  gotcha: "Insert AFTER '#include QMK_KEYBOARD_H' (the macros are defined by headers
           pulled in transitively from QMK_KEYBOARD_H -> quantum.h, so they must be in
           scope BEFORE the assert is expanded). Do NOT move it below the NFA block."

# The stub that makes the guard a no-op (confirmed: defines NEITHER symbol)
- file: qmk_stubs/qmk_keyboard_stub.h
  why: "The stub QMK_KEYBOARD_H stand-in. It does NOT define COMMUNITY_MODULES_API_VERSION
        or ASSERT_COMMUNITY_MODULES_MIN_API_VERSION, which is WHY the #ifdef skips the
        block in stub context. (grep-verified during research.)"
  gotcha: "Do NOT add these symbols to the stub to 'test the assert' — that would make
           the stub harness diverge from real QMK and is explicitly out of scope."

# The validation gate (builds 3 drivers from one stub-compiled notifier.o)
- file: run_notifier_stub_tests.sh
  why: "The [1/5] step stub-compiles notifier.c with '-DQMK_KEYBOARD_H=...'. The guard
        must not perturb it. [2/5]-[4/5] link the three drivers; [5/5] runs them. The
        gate passes iff all three report fails=0."
  critical: "This IS the item's required OUTPUT proof. It must end
             '✓ notifier stub-compile gate PASSED'."

# Sibling PRP (parallel) — confirms this is firmware-repo scope, not Rust-crate scope
- file: plan/004_76ea306f6be9/P1M1T2S1/PRP.md
  why: "Sibling R2 task (rules.mk rewrite). Establishes that plan 004 operates on the
        firmware repo (notifier.c / rules.mk / qmk_stubs), NOT the Rust transport crate.
        Keep this task's scope to notifier.c only; do not touch rules.mk."
```

### Current Codebase tree (relevant slice)

```bash
notifier.c                 # ← MODIFY (this task). Insert the guard block after line 2.
notifier.h                 # DO NOT TOUCH.
pattern_match.{c,h}        # DO NOT TOUCH.
qmk_stubs/                 # qmk_keyboard_stub.h / qmk_stubs.c / raw_hid.h / os_detection.h — DO NOT TOUCH.
qmk_module.json            # (R1 / P1.M1.T1.S1) — DO NOT TOUCH.
rules.mk                   # (R2 / P1.M1.T2.S1) — DO NOT TOUCH.
README.md                  # (R5 / P1.M2.T1.S1) — DO NOT TOUCH.
run_notifier_stub_tests.sh # validation gate — DO NOT TOUCH (just RUN it).
test_notifier_{dispatch,os,host}.c  # DO NOT TOUCH.
PRD.md                     # READ-ONLY.
```

### Desired Codebase tree after this task

```bash
notifier.c                 # +11 lines (8 comment + #ifdef + assert + #endif) near the top.
# (no files added or removed)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (guard symbol): the #ifdef MUST be on COMMUNITY_MODULES_API_VERSION.
//   This is the symbol the generator emits at community_modules.py:253 (verified).
//   Do NOT guard on COMMUNITY_MODULES_API_VERSION_BUILDER (an internal helper) or
//   on QMK_KEYBOARD_H or on __has_include. Only COMMUNITY_MODULES_API_VERSION is
//   defined iff the module-API header was pulled in.

// CRITICAL (placement MUST be after #include QMK_KEYBOARD_H): the assert macro and
//   the version symbol are defined by headers reached transitively from
//   QMK_KEYBOARD_H (-> quantum.h -> community_modules.h). If the #ifdef/assert
//   appeared BEFORE the include, the symbols would never be in scope and the guard
//   would be a permanent no-op even in real builds. Insert AFTER line 2.

// CRITICAL (stub no-op, do not break it): qmk_stubs/qmk_keyboard_stub.h defines
//   NEITHER symbol. That is intentional and load-bearing — it is what makes the
//   #ifdef skip in the host test harness so all stub binaries stay green. Do NOT
//   add the symbols to the stub "to exercise the assert"; that diverges from real
//   QMK and is out of scope.

// GOTCHA (STATIC_ASSERT is compile-time): ASSERT_COMMUNITY_MODULES_MIN_API_VERSION
//   expands (community_modules.py:254) to STATIC_ASSERT(BUILDER(maj,min,pat) <=
//   COMMUNITY_MODULES_API_VERSION, "..."). On failure it is a negative-array-size
//   typedef -> COMPILE ERROR, not a runtime abort. In a passing build it emits no
//   code. Either way the stub harness never sees it (the #ifdef is false there).

// GOTCHA (target version 1.0.0): do not bump to 1.1.x. 1.0.0 is the verified floor
//   (data/constants/module_hooks/1.0.0.hjson provides housekeeping_task +
//   process_detected_host_os — the two hooks this module needs). Higher would
//   needlessly reject valid older QMK checkouts. The call uses (1, 0, 0) with
//   spaces; the macro binds positionally so spacing is cosmetic but match the spec.

// GOTCHA (item prose line numbers slightly off): the item says "NFA comment lines
//   4-16" but the comment is actually lines 4-13 (#define is line 14). The
//   insertion directive ("after #include QMK_KEYBOARD_H, before the NFA comment
//   block") is still unambiguous — anchor on the unique line-2 include.
```

## Implementation Blueprint

### Data models and structure

None. This is a static preprocessor fragment. No types, globals, or runtime
behavior are introduced or changed.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — INSERT the API-version guard block
  - FILE: notifier.c   (firmware repo /home/dustin/projects/qmk_notifier/notifier.c)
  - LOCATE the UNIQUE anchor (verify: `grep -c '^#include QMK_KEYBOARD_H$' notifier.c` == 1):
        // notifier.c
        #include QMK_KEYBOARD_H
  - INSERT the "Exact insertion block" below IMMEDIATELY AFTER that line and
    BEFORE the blank line + the `/* Cap the Thompson NFA ... */` comment.
  - The block is: (a) the Mode-A /* */ comment; (b) `#ifdef COMMUNITY_MODULES_API_VERSION`;
    (c) `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);`; (d) `#endif`.
  - Keep one blank line after `#endif` before the NFA comment so the file reads cleanly.
  - NET CHANGE: +11 lines (8 comment + 3 preprocessor). No deletions. No restyle.
  - DO NOT: move the block below the NFA comment / below other #includes (the
    symbols must be in scope, which requires being after #include QMK_KEYBOARD_H;
    placing it later still works but the spec says "immediately after").
  - DO NOT: guard on a different symbol, or omit the #ifdef (an unguarded assert
    would break the stub harness, which lacks the macro).
  - DO NOT: touch any other file (notifier.h, pattern_match.*, qmk_stubs/*,
    qmk_module.json, rules.mk, README.md, tests, run_notifier_stub_tests.sh).

Task 2: VERIFY (no edit) — stub gate stays green
  - Run: ./run_notifier_stub_tests.sh
  - Expect: ends "✓ notifier stub-compile gate PASSED"; dispatch/os/host each fails=0.
  - (This is the item's required OUTPUT proof.)
```

**Exact insertion block (reproduce verbatim):**

```c
/* Community Module API version guard (PRD §18.3 R3).
 * In a real QMK module build, QMK_KEYBOARD_H → quantum.h → the generated
 * community_modules header defines both COMMUNITY_MODULES_API_VERSION and the
 * ASSERT_COMMUNITY_MODULES_MIN_API_VERSION macro (community_modules.py:252-254),
 * so this STATIC_ASSERT fires and enforces module API >= 1.0.0 — the floor that
 * provides housekeeping_task and process_detected_host_os
 * (data/constants/module_hooks/1.0.0.hjson). In host/stub tests
 * (-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"') neither symbol is defined, so the
 * #ifdef skips this block harmlessly and all stub binaries stay green. */
#ifdef COMMUNITY_MODULES_API_VERSION
ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);
#endif
```

> **Style note:** `notifier.c` uses `/* */` block comments for multi-line
> rationale (see the `RAW_REPORT_SIZE` and NFA blocks) — the comment above matches
> that style. The arrow `→` and section refs (§18.3 R3) match the existing file's
> comment idiom (see the NFA block's `PRD §7.9/§16` references).

### Implementation Patterns & Key Details

```c
// The guard is a pure preprocessor no-op-or-assert. Two execution contexts:
//
//   REAL MODULE BUILD: QMK_KEYBOARD_H -> quantum.h -> community_modules.h
//     defines COMMUNITY_MODULES_API_VERSION (= BUILDER(<build version>), e.g. 1.1.2)
//     and ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(maj,min,pat) -> STATIC_ASSERT(
//       BUILDER(maj,min,pat) <= COMMUNITY_MODULES_API_VERSION, "...")
//     => our call ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0) becomes
//        STATIC_ASSERT(BUILDER(1,0,0) <= build_version, "..."). Passes if
//        build >= 1.0.0; else a negative-array-size compile error.
//
//   HOST/STUB TEST: -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'; the stub defines
//     neither symbol => #ifdef is false => the assert line is not even seen by
//     the compiler => 0 warnings, 0 code, all stub binaries byte-identical.

// ANTI-PATTERN: do NOT drop the #ifdef and write the bare
//   ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);
//   The stub harness does not define the macro -> the bare call would be an
//   "implicit function declaration"/undefined macro -> the stub build breaks.

// ANTI-PATTERN: do NOT guard on `#if defined(COMMUNITY_MODULES_API_VERSION) &&
//   defined(ASSERT_COMMUNITY_MODULES_MIN_API_VERSION)`. The two are always defined
//   together (same generated header); guarding on the version symbol alone is the
//   spec (R3) and is sufficient. The over-broad guard just adds noise.

// ANTI-PATTERN: do NOT "test the assert" by defining the symbols in
//   qmk_stubs/qmk_keyboard_stub.h. That defeats the stub's job (to mimic a
//   minimal QMK surface) and diverges the harness from real QMK. The assert's
//   live behavior is verified by the real QMK module build (§18.5), out of scope
//   here; research confirmed the mechanism via a /tmp real-build simulation.
```

### Integration Points

```yaml
HEADERS / PREPROCESSOR:
  - add to: notifier.c (immediately after `#include QMK_KEYBOARD_H`, line 2)
  - pattern: "#ifdef COMMUNITY_MODULES_API_VERSION / ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0); / #endif"
  - symbols consumed (NOT defined here; provided by QMK in real builds):
      COMMUNITY_MODULES_API_VERSION           (community_modules.py:253)
      ASSERT_COMMUNITY_MODULES_MIN_API_VERSION (community_modules.py:254)
BUILD SYSTEM:
  - no change. The guard is inert until the module is distributed as a Community
    Module (§18); in the current submodule/host-test flow the #ifdef is always false.
DATABASE / CONFIG / ROUTES:
  - none.
```

## Validation Loop

> Toolchain: gcc + bash (C project — no ruff/mypy/pytest). All commands were
> **executed during research** against the real firmware repo and a `/tmp` copy of
> the proposed change; all PASSED. Work in the firmware repo root
> (`/home/dustin/projects/qmk_notifier`).

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk_notifier

# 1a. The guard block is present at the right site and is well-formed.
grep -n 'Community Module API version guard' notifier.c                 # the comment
grep -n '#ifdef COMMUNITY_MODULES_API_VERSION' notifier.c               # the guard
grep -n 'ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);' notifier.c # the assert (exact spacing)
grep -n '#endif' notifier.c | head                                      # one #endif for the guard (plus the existing CONSOLE_ENABLE one)

# 1b. The guard sits AFTER #include QMK_KEYBOARD_H and BEFORE the NFA comment.
awk '/^#include QMK_KEYBOARD_H$/{a=NR} /Cap the Thompson NFA/{b=NR}
     /Community Module API version guard/{g=NR}
     END{ print "include="a, "guard="g, "nfa="b; exit !(a<g && g<b) }' notifier.c \
  && echo "✓ guard is between #include QMK_KEYBOARD_H and the NFA comment" \
  || echo "✗ guard placement wrong"

# 1c. Anchor uniqueness (sanity).
test "$(grep -c '^#include QMK_KEYBOARD_H$' notifier.c)" -eq 1 && echo "✓ unique anchor"
```

### Level 2: Stub Compile + No-New-Warnings (the #ifdef-skip proof)

```bash
cd /home/dustin/projects/qmk_notifier

# Stub-compile notifier.c exactly as run_notifier_stub_tests.sh [1/5] does.
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_r3.o 2>&1 | tee /tmp/r3_compile.log
test -f /tmp/notifier_r3.o && echo "✓ notifier.c compiles in stub harness"
echo "warnings: $(grep -c 'warning:' /tmp/r3_compile.log)  (expect 0 — the #ifdef is skipped)"
# Expected: object present; 0 warnings. A non-zero warning count OR an error here
# means the #ifdef is leaking (e.g. the guard symbol is somehow defined in the stub,
# or the assert was placed where the macro isn't available) -> fix before proceeding.
```

### Level 3: Integration — Full Stub Gate (the item's OUTPUT proof)

```bash
cd /home/dustin/projects/qmk_notifier

# THE acceptance command for this task (PRD §11.2D / §18.5).
./run_notifier_stub_tests.sh | tee /tmp/r3_gate.log
# Expected final lines:
#   notifier dispatch fails=0  (exit=0)
#   notifier os      fails=0  (exit=0)
#   notifier host    fails=0  (exit=0)
#   ✓ notifier stub-compile gate PASSED
grep -q '✓ notifier stub-compile gate PASSED' /tmp/r3_gate.log && echo "✓ gate PASSED" || echo "✗ gate FAILED"

# Cross-check: same three drivers each report 0 FAIL: lines.
grep -E 'notifier (dispatch|os|host) fails=' /tmp/r3_gate.log
# Expected: all three show fails=0.

# Diff hygiene: ONLY notifier.c changed (plus your PRP/research under plan/).
git status --porcelain
# Expected: only ` M notifier.c` (and ?? plan/004.../P1M1T3S1/{PRP.md,research/}).
git diff --stat notifier.c
# Expected: insertions only (no deletions).
```

### Level 4: Creative & Domain-Specific Validation (assert-is-live proof, optional but decisive)

```bash
cd /home/dustin/projects/qmk_notifier

# 4a. Confirm the stub defines NEITHER guard symbol (the load-bearing no-op invariant).
! grep -qE 'COMMUNITY_MODULES_API_VERSION|ASSERT_COMMUNITY_MODULES_MIN_API_VERSION' qmk_stubs/qmk_keyboard_stub.h \
  && echo "✓ stub defines neither guard symbol (guard skips in stub context — correct)"

# 4b. SIMULATE a real module build: synthesize the macros the generator emits
#     (community_modules.py:252-254) for a 1.1.2 build, and confirm our exact call
#     ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0) COMPILES (STATIC_ASSERT passes).
cat > /tmp/r3_realsim.c <<'EOF'
#include <stdint.h>
#define STATIC_ASSERT(cond, msg) typedef char _sa_##__LINE__[(cond) ? 1 : -1]
#define COMMUNITY_MODULES_API_VERSION_BUILDER(maj,min,pat) (((((uint32_t)(maj))&0xFF)<<24)|((((uint32_t)(min))&0xFF)<<16)|(((uint32_t)(pat))&0xFF))
#define COMMUNITY_MODULES_API_VERSION COMMUNITY_MODULES_API_VERSION_BUILDER(1,1,2)   /* build is 1.1.2 */
#define ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(maj,min,pat) STATIC_ASSERT(COMMUNITY_MODULES_API_VERSION_BUILDER(maj,min,pat) <= COMMUNITY_MODULES_API_VERSION, "needs newer QMK modules API")
#ifdef COMMUNITY_MODULES_API_VERSION
ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);    /* require >= 1.0.0 against 1.1.2 -> PASS */
#endif
int main(void){ return 0; }
EOF
gcc -Wall -Wextra -std=c99 -c /tmp/r3_realsim.c -o /tmp/r3_realsim.o 2>&1 \
  && echo "✓ real-build sim: require 1.0.0 against a 1.1.2 build COMPILES (STATIC_ASSERT satisfied)"
rm -f /tmp/r3_realsim.c /tmp/r3_realsim.o /tmp/notifier_r3.o /tmp/r3_compile.log /tmp/r3_gate.log

# 4c. Mode-A doc points present (item point 6).
for needle in "1.0.0" "housekeeping_task" "process_detected_host_os" \
              "COMMUNITY_MODULES_API_VERSION" "ASSERT_COMMUNITY_MODULES_MIN_API_VERSION" \
              "host/stub\|stub" "§18.3 R3\|18.3 R3"; do
  grep -qiE "$needle" notifier.c && echo "doc present: $needle" \
    || { echo "MISSING doc token: $needle"; exit 1; }
done
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: guard block present after `#include QMK_KEYBOARD_H`, before the NFA comment; anchor unique.
- [ ] Level 2: `notifier.c` stub-compiles with **0 warnings** (the `#ifdef` is skipped).
- [ ] Level 3: `./run_notifier_stub_tests.sh` ends `✓ notifier stub-compile gate PASSED`; dispatch/os/host each `fails=0`.
- [ ] Level 3: `git status` shows ONLY `notifier.c` modified (+ plan/ PRP/research); insertions only.
- [ ] Level 4: stub defines neither guard symbol; real-build sim compiles; all doc tokens present.

### Feature Validation

- [ ] Guard is `#ifdef COMMUNITY_MODULES_API_VERSION` … `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);` … `#endif`.
- [ ] Placed immediately after `#include QMK_KEYBOARD_H` (symbols must be in scope).
- [ ] Target is 1.0.0 (floor for `housekeeping_task` + `process_detected_host_os`).
- [ ] Mode-A comment covers: target 1.0.0 + why, the `#ifdef` rationale (stub defines neither symbol), §18.3 R3 cite.

### Code Quality & Scope Validation

- [ ] Matches existing `notifier.c` `/* */` block-comment style; additive only (no restyle).
- [ ] No edits to `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `qmk_module.json`, `rules.mk`, `README.md`, tests, or `run_notifier_stub_tests.sh`.
- [ ] No anti-patterns (see below): no unguarded assert, no wrong guard symbol, no stub edits to "test" the macro, no version bump above 1.0.0.

### Documentation & Deployment

- [ ] Inline comment is self-documenting (Mode A) — no separate docs file.
- [ ] Dependency on the (planned) Community-Module distribution noted: the guard is inert in the current submodule/host-test flow by design.

---

## Anti-Patterns to Avoid

- ❌ Don't drop the `#ifdef` — the stub harness lacks `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION`, so a bare call breaks the stub build. The guard is load-bearing.
- ❌ Don't guard on the wrong symbol — it MUST be `COMMUNITY_MODULES_API_VERSION` (the symbol `community_modules.py:253` emits; verified).
- ❌ Don't place the guard **before** `#include QMK_KEYBOARD_H` — the symbols are defined transitively from that include; before it, the `#ifdef` would be permanently false even in real builds, making the guard dead.
- ❌ Don't define `COMMUNITY_MODULES_API_VERSION` / `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` in `qmk_stubs/qmk_keyboard_stub.h` "to exercise the assert" — that diverges the stub from real QMK and is out of scope. The assert's live behavior is the real QMK module build's job (§18.5).
- ❌ Don't bump the target above 1.0.0 — 1.0.0 is the verified floor (`housekeeping_task` + `process_detected_host_os`); higher needlessly rejects valid older QMK checkouts.
- ❌ Don't touch `rules.mk` (R2 / P1.M1.T2.S1), `qmk_module.json` (R1 / P1.M1.T1.S1), or the README (R5) — those are sibling tasks.
- ❌ Don't edit in the wrong repo — this is the **firmware** module (`qmk_notifier`, underscore), NOT the Rust transport crate (`qmk-notifier`, hyphen).

---

## Confidence Score: 10/10

The deliverable is a single 11-line additive block whose exact text (Mode-A
comment + `#ifdef`/assert/`#endif`), exact placement (after the unique
`#include QMK_KEYBOARD_H` anchor), exact target version (1.0.0, justified by the
verified `1.0.0.hjson` hooks), and exact validation command are fully specified
and were **empirically validated during research**: the unmodified gate is green
(dispatch/os/host 94/94, `fails=0` each); applying the guard to a `/tmp` copy
compiled with **0 warnings** (the `#ifdef` is skipped because the stub defines
neither symbol — grep-confirmed) and re-linked **all three** stub drivers green
(`fails=0` each); and a real-build simulation (synthesizing the generator's exact
macro output for a 1.1.2 build) confirmed `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0)`
compiles (STATIC_ASSERT satisfied). No external dependencies are added; scope
boundaries with the R1/R2/R5/R6 sibling tasks are explicit.