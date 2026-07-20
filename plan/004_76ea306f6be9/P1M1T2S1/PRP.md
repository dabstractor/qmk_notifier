# PRP — P1.M1.T2.S1: Change `rules.mk` to the module-context form

## Goal

**Feature Goal**: Convert `rules.mk` at the repo root from the **submodule-context**
form (keyboard-relative `SRC` path) to the **Community-Module context** form (bare
`SRC += notifier.c` resolved via VPATH), keeping `RAW_ENABLE = yes`, and add one
Mode-A documentation comment. This is requirement **R2** of PRD §18.3 and
operationalizes the §18.4 "retire the submodule flow" decision for the build file.

**Deliverable**: A rewritten `rules.mk` at the repo root containing exactly one
comment line plus the two functional lines `RAW_ENABLE = yes` and
`SRC += notifier.c`. No other file is touched.

**Success Definition**: `cat rules.mk` shows the three lines below and nothing
else; the host test gates (`./run_all_tests.sh`, `./run_notifier_stub_tests.sh`)
remain green (they do not reference `rules.mk`, so this is a regression check).

```make
# Community Module context — notifier.c via VPATH; pattern_match.c pulled in by notifier.c #include
RAW_ENABLE = yes
SRC += notifier.c
```

## Why

- **Self-declaring module**: Under the planned Community-Module distribution
  (PRD §18), the user adds one `"modules"` entry to `keymap.json` and the QMK
  build generator discovers `rules.mk` and the sources automatically. The
  module's `rules.mk` must therefore be written for **module context**, where
  the module directory is already on the include path via `VPATH` — not for the
  submodule flow, where paths are keyboard-relative.
- **Retire the old flow**: Per §18.4 the submodule flow is intentionally retired.
  The old `SRC += qmk_notifier/notifier.c` is a keyboard-relative path that only
  resolves when a keymap does `include keyboards/<...>/qmk_notifier/rules.mk`.
  The new bare `SRC += notifier.c` resolves via VPATH in module context. The two
  are mutually exclusive; keeping both is not an option (§18.4 decision).
- **Sibling-correct plumbing**: This is one of three module build-system changes
  (R1 = `qmk_module.json`, R2 = this task, R3 = API-version assert in `notifier.c`).
  This PRP is scoped to **R2 only**.

## What

Rewrite the two functional lines of `rules.mk` and prepend one explanatory
comment. Specifically:

1. **Keep** `RAW_ENABLE = yes` (unchanged).
2. **Change** `SRC += qmk_notifier/notifier.c` → `SRC += notifier.c`
   (drop the `qmk_notifier/` path prefix).
3. **Add** one leading comment line documenting the Community-Module context.

### Success Criteria

- [ ] `rules.mk` contains `RAW_ENABLE = yes`.
- [ ] `rules.mk` contains `SRC += notifier.c` (bare, no `qmk_notifier/` prefix).
- [ ] `rules.mk` does **not** contain `SRC += qmk_notifier/notifier.c`.
- [ ] `rules.mk` does **not** contain any `SRC += ... pattern_match.c` line
      (`pattern_match.c` is pulled in via `#include`, never `SRC`).
- [ ] `rules.mk` begins with the Mode-A comment line shown in Success Definition.
- [ ] `rules.mk` is exactly 3 physical lines (1 comment + 2 functional).
- [ ] Host test gates unchanged-green: `./run_all_tests.sh` and
      `./run_notifier_stub_tests.sh` pass (they never read `rules.mk`).
- [ ] No other repository file is modified by this task.

## All Needed Context

### Context Completeness Check

**"If someone knew nothing about this codebase, would they have everything needed
to implement this successfully?"** — Yes. The change is a single static Makefile
fragment. The reasoning for each line (build mechanics, the `#include`
relationship, why `RAW_ENABLE` cannot move to `qmk_module.json`) is documented
below with ground-truth references so the implementer cannot accidentally
"improve" the file (e.g. by adding `pattern_match.c` to `SRC`, or moving
`RAW_ENABLE` into the manifest).

### Documentation & References

```yaml
# MUST READ - ground-truth build mechanics (verified from qmk_firmware source)
- docfile: plan/004_76ea306f6be9/architecture/external_deps.md
  why: Explains WHY notifier.c must be listed explicitly, WHY pattern_match.c
       must NOT be a SRC entry, and WHY RAW_ENABLE stays in rules.mk.
  section: "Community Module Generator" (items 1-3) and "Resolved Design Decisions"
  critical: >
    The generator auto-compiles ONLY <leaf>.c (= qmk_notifier.c) via wildcard.
    notifier.c does NOT match the leaf name -> NOT auto-compiled -> MUST be
    listed via SRC += notifier.c. RAW_ENABLE is NOT a data-driven feature key
    (no 'rawhid' entry in data/schemas/), so it MUST stay in rules.mk.

# PRD source for this requirement (R2) and the retire-submodule decision
- docfile: plan/004_76ea306f6be9/prd_snapshot.md
  why: Authoritative R2 wording and §18.4 "retire submodule flow" decision.
  section: "h2.18 / h3.51 (R2)" and "h3.52 (18.4 Tradeoffs)"

# The #include relationship that makes pattern_match.c NOT a SRC entry
- file: notifier.c
  why: Line ~16 contains `#include "pattern_match.c"`. This is what compiles
       pattern_match.c (textual include, resolved via VPATH/include path) —
       which is why it must never appear as a separate SRC += line.
  pattern: "#include \"pattern_match.c\"" near the top of notifier.c, after
           the NFA_MAX_PATTERN cap define and before notifier.h/raw_hid.h.
  gotcha: >
    A reader seeing only rules.mk might assume pattern_match.c is missing and
    add `SRC += pattern_match.c`. That would DOUBLE-compile it (once via
    #include into notifier.c, once as a translation unit) -> duplicate-symbol
    link errors. Do NOT add it.

# Current file being changed (input state)
- file: rules.mk
  why: The file under edit. Currently 2 lines.
  pattern: "RAW_ENABLE = yes\nSRC += qmk_notifier/notifier.c\n"
  gotcha: Preserve the trailing newline; the file currently ends with a newline.
```

### Current Codebase tree (relevant slice)

```bash
rules.mk          # <-- THE FILE BEING CHANGED (2 lines today)
notifier.c        # line ~16: #include "pattern_match.c"  (load-bearing relationship)
notifier.h
pattern_match.c   # pulled in via notifier.c #include, NOT via SRC
pattern_match.h
run_all_tests.sh  # host gate; does NOT reference rules.mk
run_notifier_stub_tests.sh  # host gate; does NOT reference rules.mk
```

### Desired Codebase tree after this task

```bash
rules.mk          # 3 lines: 1 comment + RAW_ENABLE + SRC += notifier.c
# (no files added or removed by this task)
```

### Known Gotchas of our codebase & Library Quirks

```make
# CRITICAL: QMK's community-module generator auto-compiles ONLY <leaf>.c
# (here qmk_notifier.c) via `SRC += $(wildcard <module_path>/<leaf>.c)`.
# notifier.c does NOT match the leaf name -> it is NOT auto-compiled and
# MUST be listed explicitly as `SRC += notifier.c` (bare, via VPATH).
#
# CRITICAL: pattern_match.c is compiled by textual #include from notifier.c
# (line ~16 of notifier.c). It must NEVER be a separate `SRC +=` entry, or
# you get duplicate-symbol link errors.
#
# CRITICAL: RAW_ENABLE is NOT a data-driven feature key (no `rawhid` entry in
# data/schemas/), so it MUST stay here in rules.mk, NOT in qmk_module.json
# `features`. The generator does `-include <module_path>/rules.mk`, so a
# variable set here applies globally to the build.
#
# INTENTIONAL BREAKAGE: The bare `SRC += notifier.c` resolves via VPATH (module
# dir on -I), NOT via a keyboard-relative path. This is incompatible with the
# old `include keyboards/<...>/qmk_notifier/rules.mk` submodule flow. That is
# the intended outcome of PRD §18.4 ("retire the submodule flow").
```

## Implementation Blueprint

### Data models and structure

_None._ This task modifies a static GNU Make fragment. No types, schemas, or
runtime data structures are involved.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: REWRITE rules.mk (repo root)
  - FILE: rules.mk
  - ACTION: Replace the entire file contents with exactly these 3 lines
            (preserve the trailing newline):
      # Community Module context — notifier.c via VPATH; pattern_match.c pulled in by notifier.c #include
      RAW_ENABLE = yes
      SRC += notifier.c
  - NET CHANGE from current 2-line file:
      * KEEP:    "RAW_ENABLE = yes"                              (unchanged)
      * CHANGE:  "SRC += qmk_notifier/notifier.c"
                 -> "SRC += notifier.c"                          (drop path prefix)
      * ADD:     the leading "#" comment line (Mode-A doc)       (new line 1)
  - PRESERVE: trailing newline at EOF (current file has one).
  - NAMING: bare filename `notifier.c` — NO `qmk_notifier/` prefix, NO `lib/` prefix,
            NO `./` prefix. VPATH supplies the directory.
  - COMMENT TEXT: copy verbatim from the contract — note the em-dash (—), not a hyphen.
  - DO NOT: add `SRC += pattern_match.c` (it comes in via #include — double-compile hazard).
  - DO NOT: move RAW_ENABLE into qmk_module.json (sibling task T1's file; RAW_ENABLE is
            not a data-driven feature key).
  - DO NOT: edit notifier.c, README.md, qmk_module.json, or any test file in this task.
  - DEPENDENCIES: none. rules.mk is a standalone static fragment; it does not
            require qmk_module.json to exist (the generator `-include`s rules.mk
            independently). Safe to land even though sibling T1 is in another state.
```

### Implementation Patterns & Key Details

```make
# ---- FINAL rules.mk (exact contents) ----
# Community Module context — notifier.c via VPATH; pattern_match.c pulled in by notifier.c #include
RAW_ENABLE = yes
SRC += notifier.c
```

Rationale per line (so the implementer understands, not just copies):

- **Line 1 (comment)**: Mode-A documentation required by the contract. States
  (a) this is Community-Module context, (b) `notifier.c` is found via VPATH,
  (c) `pattern_match.c` arrives through `notifier.c`'s `#include` — heading off
  the most likely mistaken "fix" of adding it to `SRC`.
- **Line 2 (`RAW_ENABLE = yes`)**: Enables QMK Raw HID (usage page `0xFF60`,
  usage `0x61`, 32-byte reports) — the transport the notifier speaks. Must stay
  in `rules.mk` because it is not a data-driven feature key.
- **Line 3 (`SRC += notifier.c`)**: Lists the one source file the generator's
  wildcard does not auto-compile. Bare filename resolves via the `VPATH`
  entry the generator emits for the module directory.

The contract's phrase "exactly two lines" refers to the two **active/functional**
lines (`RAW_ENABLE` and `SRC`). The comment is the Mode-A doc addition specified
in the same contract, so the physical file is **3 lines**.

### Integration Points

```yaml
DATABASE:
  - none  # static Makefile fragment; no persistence layer
CONFIG:
  - none  # no env vars introduced or changed
ROUTES:
  - none  # no API surface
BUILD SYSTEM (the real integration surface):
  - consumed by: QMK community-module generator at build time via
                 `-include <module_path>/rules.mk` (PRD §18.2 / external_deps.md §3).
  - depends on: notifier.c line ~16 `#include "pattern_match.c"` (unchanged by this task).
  - BREAKS (intentional): the old keymap-line `include keyboards/<...>/qmk_notifier/rules.mk`
                 submodule flow. README rewrite for that flow is task P1.M2.T1.S1, NOT this task.
```

## Validation Loop

There is **no automated test that exercises `rules.mk` directly**: the host test
scripts compile `notifier.c` / `pattern_match.c` with `gcc` and a stub
`QMK_KEYBOARD_H`, never reading `rules.mk` (`grep -l 'rules.mk' *.sh` → none).
The true positive gate is a QMK module build, which cannot run inside this repo
(no `qmk_firmware` build environment here). Validation here is therefore
**content inspection + do-no-harm regression on the host gates**.

### Level 1: Syntax & Style

```bash
# rules.mk is a GNU Make fragment — there is no linter/type-checker for it.
# Validate by inspection:
cat rules.mk
# Expected: exactly 3 lines, comment first, then RAW_ENABLE, then bare SRC.

# Confirm no stale submodule-context path survives:
grep -n 'qmk_notifier/notifier.c' rules.mk && echo "FAIL: old path still present" || echo "OK: old path removed"
grep -n 'pattern_match.c' rules.mk && echo "FAIL: pattern_match.c must not be in SRC" || echo "OK: pattern_match.c not in SRC (correct)"
grep -c '^SRC += ' rules.mk    # Expected: 1  (only notifier.c)
grep -c '^RAW_ENABLE = yes' rules.mk   # Expected: 1
```

### Level 2: Unit Tests

_None applicable._ No unit-test framework targets `rules.mk`. (The repo's host
tests live in `test_*.c`; none reference `rules.mk`.)

### Level 3: Integration / Regression (do-no-harm)

```bash
# Host gates must remain GREEN. They do not read rules.mk, so a green run
# confirms this change introduced no incidental damage to the tree.
./run_all_tests.sh            # Expected: all suites pass (exit 0)
./run_notifier_stub_tests.sh  # Expected: all suites pass (exit 0)

# If a qmk_firmware checkout + a userspace module build is available elsewhere,
# the positive integration gate (PRD §18.5) is: a clean keymap listing the
# module compiles `notifier.c` (pulling pattern_match.c via #include) with
# RAW_ENABLE on and NO manual `include .../rules.mk` line in the keymap.
# That full build is out of scope to run in this repo.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Structural sanity: confirm the file is exactly 3 physical lines and ends with a newline.
test "$(wc -l < rules.mk)" -eq 3 && echo "OK: 3 lines" || echo "FAIL: line count != 3"
tail -c1 rules.mk | od -An -c | grep -q '\\n' && echo "OK: trailing newline" || echo "FAIL: no trailing newline"

# Confirm the comment is present and first.
head -1 rules.mk | grep -q '^# Community Module context' && echo "OK: comment present/first" || echo "FAIL: comment missing"
```

## Final Validation Checklist

### Technical Validation

- [ ] `cat rules.mk` shows exactly the 3 lines in *Success Definition*.
- [ ] `grep 'SRC += ' rules.mk` returns exactly `SRC += notifier.c`.
- [ ] No `qmk_notifier/` path prefix anywhere in `rules.mk`.
- [ ] No `pattern_match.c` reference in `rules.mk` SRC.
- [ ] File is 3 lines and ends with a trailing newline.
- [ ] `./run_all_tests.sh` passes (exit 0).
- [ ] `./run_notifier_stub_tests.sh` passes (exit 0).

### Feature Validation

- [ ] `RAW_ENABLE = yes` present and unchanged.
- [ ] `SRC += notifier.c` present in bare (module-context) form.
- [ ] Mode-A comment present as line 1, verbatim (em-dash preserved).
- [ ] No other repository file modified by this task.

### Code Quality & Scope Validation

- [ ] Did NOT add `SRC += pattern_match.c` (double-compile hazard avoided).
- [ ] Did NOT move `RAW_ENABLE` to `qmk_module.json` (out of scope + wrong layer).
- [ ] Did NOT touch `notifier.c`, `README.md`, `qmk_module.json`, or any test.
- [ ] Did NOT attempt to maintain the old submodule flow (retired per §18.4).

### Documentation

- [ ] The leading comment self-documents the Community-Module context and the
      `pattern_match.c`-via-`#include` relationship for future readers.

---

## Anti-Patterns to Avoid

- ❌ Don't add `SRC += pattern_match.c` — it is textually `#include`d by
  `notifier.c`; a separate `SRC` entry double-compiles it (duplicate symbols).
- ❌ Don't keep or duplicate the `qmk_notifier/` path prefix — that is the
  submodule-flow form, retired by §18.4.
- ❌ Don't move `RAW_ENABLE` into `qmk_module.json` `features` — it is not a
  data-driven feature key and the generator only honors it from `rules.mk`.
- ❌ Don't "preserve backward compatibility" by keeping both SRC forms — they
  are mutually exclusive and §18.4 explicitly retires the old one.
- ❌ Don't edit the README install flow here — that is task P1.M2.T1.S1.
- ❌ Don't assume `rules.mk` is covered by a test — it is not; validate by
  inspection plus the do-no-harm host gates.

---

## Confidence Score

**9/10** for one-pass implementation success.

Rationale: The change is a single static file with the exact final contents
specified verbatim in the contract (including the comment text). The only
implementation risk is an agent "helpfully" over-editing (adding
`pattern_match.c` to SRC, prefixing the path, or rewriting the README). The
PRP above spends most of its length foreclosing exactly those mistakes by
explaining the build mechanics with ground-truth references. The one point of
residual uncertainty is that the positive validation (a real QMK module build)
cannot be executed in this repo — so success is asserted via content inspection
plus host-gate regression, which is the strongest validation available here.