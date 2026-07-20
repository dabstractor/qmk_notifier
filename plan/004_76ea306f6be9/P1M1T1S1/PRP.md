# PRP — P1.M1.T1.S1: Create `qmk_module.json` at repo root

## Goal

**Feature Goal**: Add the QMK Community Module manifest `qmk_module.json` at the
firmware repo root so the module is self-declaring (PRD §18.3 R1): a user adds one
`"modules": ["<org>/qmk_notifier"]` entry to `keymap.json` and QMK's build discovers
the module's `rules.mk`, sources, and include path automatically. This is **R1**
(R2 `rules.mk` and R3 `notifier.c` guard are already complete; R1 is the remaining
plumbing piece).

**Deliverable**: The file `qmk_module.json` at the firmware repo root — well-formed
JSON, valid against the `qmk.community_module.v1` schema, containing exactly five
keys (`module_name`, `maintainer`, `license`, `url`, `keycodes`) and **no** `features` block.

**Success Definition**:
- `qmk_module.json` exists at the repo root and `python3 -m json.tool` parses it.
- Fields (per the task contract / PRD §18.3 R1):
  `module_name="QMK Notifier"`, `maintainer="dabstractor"`,
  `license="GPL-2.0-or-later"`, `url="https://github.com/dabstractor/qmk_notifier"`,
  `keycodes=[]`.
- No `features` key (RAW_ENABLE is a `rules.mk` variable, not a data-driven feature key).
- `license` is GPL-compatible (GPL-2.0-or-later is), satisfying QMK's GPLv2+ requirement.

## User Persona (if applicable)

**Target User**: End users installing the module via the Community Module flow, and
the QMK build system (`lib/python/qmk/cli/generate/community_modules.py`).

**Use Case**: User clones the repo to `modules/<org>/qmk_notifier`, adds
`"modules": ["<org>/qmk_notifier"]` to `keymap.json`; QMK reads the manifest to
register the module (auto-compiles `<leaf>.c`, adds the dir to VPATH, sets
`-DCOMMUNITY_MODULE_<LEAF>_ENABLE`).

**Pain Points Addressed**: Removes the hand-written `include .../rules.mk` line, the
`SRC +=` / `RAW_ENABLE` wiring, and the relative `#include` path (PRD §18.1 / R5).

## Why

- **R1 (this task)** is the manifest prerequisite for the §18 Community Module migration;
  without it QMK will not recognize the directory as a Community Module.
- Self-declaring install (one `keymap.json` line) vs. today's multi-step submodule flow.
- `GPL-2.0-or-later` matches `qmk/qmk_firmware` itself and every example module in
  `qmk_firmware/modules/` (`hello_world`, `super_alt_tab`), and is GPL-compatible —
  exactly what QMK requires for inclusion.
- **Verified**: this firmware repo currently has **no** `LICENSE` file, so
  `GPL-2.0-or-later` is the correct safe SPDX default (the task note is accurate for
  this repo; it was only "stale" when mistakenly checked against the separate
  `qmk-notifier` Rust transport-crate repo, which is MIT and out of scope).

## What

A static JSON manifest at the firmware repo root:

```json
{
    "module_name": "QMK Notifier",
    "maintainer": "dabstractor",
    "license": "GPL-2.0-or-later",
    "url": "https://github.com/dabstractor/qmk_notifier",
    "keycodes": []
}
```

- **No `features` block** — the module declares no data-driven feature keys. `RAW_ENABLE`
  is set in `rules.mk`, not here; PRD §18.2 confirms there is no `rawhid` schema entry.
- **`keycodes: []`** — the public surface is macros (`DEFINE_SERIAL_*`) + functions invoked
  from `keymap.c`, not keymap-bindable keys.
- **`url` uses the underscore firmware repo** (`dabstractor/qmk_notifier`) per PRD §18.3 R1.

### Success Criteria

- [ ] `qmk_module.json` at repo root, valid JSON.
- [ ] Exactly 5 top-level keys: `module_name`, `maintainer`, `license`, `url`, `keycodes`.
- [ ] `license="GPL-2.0-or-later"`; no `features` key; `keycodes` is `[]`.
- [ ] `url` is the underscore firmware repo (`dabstractor/qmk_notifier`).

## All Needed Context

### Context Completeness Check

_Pass_: The exact JSON content (per the task contract + PRD §18.3 R1), the schema rules,
the current repo state (no LICENSE; R2 done; qmk_module.json absent), and executable
validation commands are all specified below.

### Documentation & References

```yaml
# MUST READ — the requirement (authoritative)
- file: PRD.md
  section: "## 18. Community Module Distribution → ### 18.3 Requirements → R1"
  why: "R1 is the canonical manifest content (module_name/maintainer/license/url/keycodes)."
  critical: "No `features` block; no keycodes entries. RAW_ENABLE lives in rules.mk (R2)."

- file: PRD.md
  section: "### 18.2 Verified build-system mechanics"
  why: "Explains WHY there is no `features` entry: RAW_ENABLE is NOT a data-driven feature
        key (no `rawhid` schema in data/schemas/), so it must be in rules.mk."
  critical: "Do not invent a `features: {rawhid: ...}` entry — it does not exist in the schema."

- file: rules.mk
  why: "R2 is ALREADY complete here: it is in module-context form
        (RAW_ENABLE = yes; SRC += notifier.c). Confirms this is the firmware repo where
        the manifest belongs; do NOT modify rules.mk in this task."
  critical: "notifier.c does NOT match the leaf name qmk_notifier, so it is listed
        explicitly in SRC; pattern_match.c is pulled in via notifier.c's #include."

# Schema reference (upstream QMK)
- url: https://github.com/qmk/qmk_firmware/blob/master/data/schemas/qmk.community_module.v1.json
  why: "The jsonschema the manifest must satisfy. REQUIRED: module_name, maintainer."
  critical: "license/url/keycodes/features are all OPTIONAL; omitting features is valid."

# Pattern to follow (real example modules)
- url: https://github.com/qmk/qmk_firmware/tree/master/modules
  why: "hello_world and super_alt_tab both ship qmk_module.json with
        \"license\": \"GPL-2.0-or-later\". Mirror their field set and 4-space indent."
  pattern: "5-key manifest, 4-space indent, trailing newline."
```

### Current Codebase tree (firmware repo root)

```bash
notifier.c  notifier.h  pattern_match.c  pattern_match.h   # firmware sources
rules.mk                 # R2 DONE — module-context: RAW_ENABLE=yes; SRC += notifier.c
qmk_stubs/               # host-test stub harness
test_*.c  run_all_tests.sh  run_notifier_stub_tests.sh     # host test gates
PRD.md  README.md  .gitignore  plan/
qmk_module.json          # ← CREATE (this task). Absent today.
# NOTE: this repo currently has NO LICENSE file → license defaults to GPL-2.0-or-later
#       (matches qmk_firmware + example modules; GPL-compatible).
```

### Desired Codebase tree with files to be added

```bash
qmk_module.json          # NEW — static manifest at firmware repo root (this task's only deliverable).
```

### Known Gotchas of our codebase & Library Quirks

```python
# CRITICAL (license value): use "GPL-2.0-or-later". This firmware repo has NO LICENSE
#   file, so the safe GPL-compatible default applies — it matches qmk/qmk_firmware and
#   all example modules in qmk_firmware/modules/. (Do NOT confuse this with the separate
#   qmk-notifier Rust transport-crate repo, which is MIT — that is a different repo.)

# GOTCHA (no features block): do NOT add a `features` map. RAW_ENABLE is a rules.mk
#   variable, not a data-driven feature key — there is no `rawhid` schema entry
#   (PRD §18.2). Adding an unknown feature key FAILS `qmk lint`.

# GOTCHA (no keycodes): the public surface is macros (DEFINE_SERIAL_*) + functions
#   invoked from keymap.c, NOT keymap-bindable keys. keycodes MUST be [] (or omitted).

# GOTCHA (url uses UNDERSCORE): https://github.com/dabstractor/qmk_notifier — per
#   PRD §18.3 R1 (the firmware module repo), not a hyphenated URL.

# GOTCHA (validation needs qmk for full schema check): `python3 -m json.tool` validates
#   JSON SYNTAX only. Full qmk.community_module.v1 schema validation requires `qmk lint`
#   in a qmk_userspace build that lists the module (outside this task's scope).

# GOTCHA (host gates unaffected): this is a static JSON file; it does not change any .c
#   file, so ./run_all_tests.sh and ./run_notifier_stub_tests.sh are byte-identical.
```

## Implementation Blueprint

### Data models and structure

None beyond the JSON manifest (static, 5 keys).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: CREATE qmk_module.json at the firmware repo root
  - CONTENT (4-space indent, single trailing newline):
        {
            "module_name": "QMK Notifier",
            "maintainer": "dabstractor",
            "license": "GPL-2.0-or-later",
            "url": "https://github.com/dabstractor/qmk_notifier",
            "keycodes": []
        }
  - KEYS: exactly module_name, maintainer, license, url, keycodes (in that order).
  - license VALUE: "GPL-2.0-or-later" (no LICENSE file → GPL-compatible QMK default).
  - OMIT: `features` (no data-driven feature keys); do not add keycodes entries.
  - NAMING: module_name is the human display name ("QMK Notifier"); the leaf/identifier
    "qmk_notifier" comes from the clone directory, NOT from this file (PRD §18.2).
  - PLACEMENT: firmware repo root (top level, alongside rules.mk / notifier.c).
  - DEPENDENCIES: none.

Task 2: DO NOT touch anything else
  - rules.mk is R2 (complete — already module-context). notifier.c guard is R3 (complete).
  - README install rewrite is R5 (separate task P1.M2.T1).
  - Do not modify PRD.md, notifier.c, rules.mk, tasks.json, prd_snapshot.md, .gitignore.
```

### Implementation Patterns & Key Details

```python
# The manifest is deliberately minimal — qmk.community_module.v1 only REQUIRES
# module_name + maintainer. We add license/url/keycodes for completeness and GPL
# compliance, and OMIT features because the module declares no data-driven features.
#
# ANTI-PATTERN: do not add "features": {"rawhid": ...}. No such schema entry exists;
#   RAW_ENABLE is set in rules.mk. Adding it fails `qmk lint`.
# ANTI-PATTERN: do not list keycodes. The API is macros + C functions, not bindable keys.
# ANTI-PATTERN: do not use a hyphenated URL — use the underscore firmware repo per R1.
# ANTI-PATTERN: do not write the file into the qmk-notifier (hyphen) Rust transport-crate
#   repo — it belongs HERE in qmk_notifier (underscore, firmware).
```

### Integration Points

```yaml
BUILD (QMK community-module generator):
  - read by: "lib/python/qmk/cli/generate/community_modules.py (in qmk_firmware)"
  - effect: "registers the module dir so SRC/VPATH/rules.mk are discovered (§18.2)."
LICENSE:
  - field: "license = \"GPL-2.0-or-later\" (no LICENSE file; GPL-compatible QMK default)."
CONFIG/ROUTES/DATABASE:
  - none (static JSON manifest).
```

## Validation Loop

> Run in the firmware repo root. Full schema validation additionally needs a
> `qmk_userspace` build (outside this task); `qmk` is not required for Levels 1 & 4.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk_notifier

# Valid JSON syntax.
python3 -m json.tool qmk_module.json > /dev/null && echo "JSON OK"

# Exactly the expected key set, correct values, no extras.
python3 - <<'PY'
import json
d = json.load(open("qmk_module.json"))
assert list(d.keys()) == ["module_name","maintainer","license","url","keycodes"], d.keys()
assert d["module_name"] == "QMK Notifier"
assert d["maintainer"] == "dabstractor"
assert d["license"] == "GPL-2.0-or-later"
assert d["url"] == "https://github.com/dabstractor/qmk_notifier"
assert d["keycodes"] == []
assert "features" not in d
print("fields OK")
PY

# License is GPL-compatible (QMK is GPLv2+).
python3 - <<'PY'
import json
lic = json.load(open("qmk_module.json"))["license"]
GPL_COMPAT = {"MIT","GPL-2.0-only","GPL-2.0-or-later","GPL-3.0-only","GPL-3.0-or-later",
              "BSD-3-Clause","BSD-2-Clause","ISC","Apache-2.0","LGPL-2.1-or-later","MPL-2.0"}
assert lic in GPL_COMPAT, f"license {lic!r} not in known GPL-compatible set"
print("license GPL-compatible OK:", lic)
PY
# Expected: "JSON OK"; "fields OK"; "license GPL-compatible OK: GPL-2.0-or-later".
```

### Level 2: Unit Tests (Component Validation)

```bash
# A static JSON manifest has no unit-test harness beyond Level 1's structural checks.
# Its behavioral effect (module registration) is validated at Level 3 via `qmk lint`.
echo "Level 2: N/A for a static JSON manifest (covered by Level 1 + Level 3)."
```

### Level 3: Integration Testing (System Validation)

```bash
# Authoritative gate (PRD §18.5): qmk lint must be clean with the module registered.
# Requires: the repo cloned to modules/<org>/qmk_notifier inside a qmk_userspace checkout
# and listed in a keymap.json. If such an environment is available:
#   qmk lint -kb <keyboard> -km <keymap>    # expect exit 0
# This is environment-dependent and outside the repo-local validation; run if available.
echo "Level 3: run 'qmk lint' in a userspace build that lists the module (§18.5)."
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk_notifier

# Cross-check field values against PRD §18.3 R1 + the task contract.
python3 - <<'PY'
import json
d=json.load(open("qmk_module.json"))
assert d["module_name"]=="QMK Notifier" and d["maintainer"]=="dabstractor"
assert d["url"].endswith("dabstractor/qmk_notifier")   # underscore firmware repo
assert d["keycodes"]==[] and "features" not in d
assert d["license"]=="GPL-2.0-or-later"
print("R1 conformance OK")
PY

# Diff hygiene: only qmk_module.json is added in this task (no .c/.mk/README changes).
git status --short
# Expected: only `qmk_module.json` (new/untracked). No other source changes.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `python3 -m json.tool` parses; field-set + license checks pass.
- [ ] Level 3 (if env available): `qmk lint` clean in a userspace build listing the module (§18.5).
- [ ] Level 4: R1 conformance; diff hygiene OK.

### Feature Validation

- [ ] `qmk_module.json` at the firmware repo root with the 5 required keys.
- [ ] `license="GPL-2.0-or-later"` (no LICENSE file; GPL-compatible QMK default).
- [ ] No `features` block; `keycodes` is `[]`.
- [ ] `url` uses the underscore firmware repo.

### Code Quality Validation

- [ ] 4-space indent + trailing newline, matching `qmk_firmware/modules/*` style.
- [ ] No anti-patterns (no features block, no keycodes, correct URL, correct repo).
- [ ] Only `qmk_module.json` created — rules.mk/notifier.c/README/PRD.md untouched.

### Documentation & Deployment

- [ ] Fields self-document (Mode A); `license` follows the task contract (GPL-2.0-or-later).

---

## Anti-Patterns to Avoid

- ❌ **Don't write the file into the `qmk-notifier` (hyphen) Rust transport-crate repo.**
  It belongs HERE, in `qmk_notifier` (underscore, firmware). The §18 Community Module
  work is firmware-only.
- ❌ Don't add a `features` block — no `rawhid` schema entry exists; RAW_ENABLE is a rules.mk var.
- ❌ Don't list keycodes — the surface is macros + functions, not bindable keys.
- ❌ Don't use a hyphenated URL — `url` is the underscore firmware repo per R1.
- ❌ Don't touch rules.mk (R2 done), notifier.c (R3 done), README (R5/P1.M2.T1), PRD.md,
  tasks.json, prd_snapshot.md, or .gitignore.

---

## Confidence Score: 9/10

The deliverable is a single 5-key static JSON file whose content is fully pinned to the task
contract / PRD §18.3 R1 and verified against this firmware repo's actual state (no LICENSE
file → `GPL-2.0-or-later` is correct; R2 rules.mk already in module-context; qmk_module.json
absent). Levels 1 and 4 validate locally with `python3`; the only gate requiring external
tooling (`qmk lint`) needs a qmk_userspace build that is outside this task's scope (hence 9,
not 10).