# PRP — P1.M2.T1.S1: Replace the submodule Setup flow with the Community Module flow

> **⚠️ TARGET REPO — read first.** This task targets the **firmware module repo**
> `/home/dustin/projects/qmk_notifier` (remote `dabstractor/qmk_notifier`, underscore),
> NOT the Rust transport-crate repo (`dabstractor/qmk-notifier`, hyphen). The
> `## Setup` section to rewrite, the module-context `rules.mk` (P1.M1.T2.S1), the
> `notifier.c` API guard (P1.M1.T3.S1), and this plan's `tasks.json` + prior PRPs all
> live in the firmware repo. (The crate repo's `plan/004` is a stray copy.) All paths
> below are relative to the firmware repo root. **The single file edited is
> `/home/dustin/projects/qmk_notifier/README.md`.**

## Goal

**Feature Goal**: Replace the firmware module README's `## Setup` section (currently
the **submodule flow**, README lines 49-98) with the **QMK Community Module flow** per
PRD §18.3 R5. A user installs the module with one `keymap.json` `"modules"` entry +
the `raw_hid_receive → hid_notify` shim — no in-keyboard clone, no relative `#include`,
no hand-wired `include .../rules.mk` / `SRC +=` / `RAW_ENABLE`. The leaf-directory
naming contract (R4), the irreducible `raw_hid_receive` shim (§18.2), and the
Configurator limitation (§18.4) are stated explicitly. Multi-OS and host-rules notes
are preserved unchanged.

**Deliverable**: The modified file `/home/dustin/projects/qmk_notifier/README.md` —
one section rewrite: the `## Setup` block (lines 49-98, from the `## Setup` heading
through the `OS_DETECTION_ENABLE` block, immediately before `## Usage` at line 100).
No other file changes; no code/build change (R6).

**Success Definition**:
- The Setup section documents ONLY the Community Module flow: clone to
  `modules/<org>/qmk_notifier`, one `keymap.json` `"modules"` entry, and
  `#include "notifier.h"` (no relative path) + the `raw_hid_receive` shim in `keymap.c`.
- The old submodule artifacts are GONE from the Setup section: the in-keyboard
  `git submodule add`, the `#include "./qmk_notifier/notifier.h"` relative path, the
  `include keyboards/.../rules.mk` line, and the `SRC +=`/`RAW_ENABLE` hand-wiring.
- A prominent callout states the leaf-directory naming contract (R4): clone to a
  hyphen-free leaf (`qmk_notifier`); a hyphenated leaf is a HARD build failure.
- A callout states the `raw_hid_receive` shim is STILL required (§18.2: it is not a
  Community Module hook).
- A note states Community Modules can't be built by the QMK Configurator (§18.4).
- The Multi-OS (`process_detected_host_os_kb` → `notifier_set_os`,
  `OS_DETECTION_ENABLE`) and Host-rules (`DEFINE_HOST_CALLBACKS`) notes are preserved,
  with their `#multi-os-configuration` / `#host-side-rules--typed-commands` anchor
  links intact.
- No other README section is moved or altered; `## Usage` (line 100) follows the new
  Setup section unchanged.

## User Persona (if applicable)

**Target User**: A keymap author installing qmk_notifier. Under the old flow they had
to (a) clone into their keyboard dir, (b) hand-write a keyboard-relative
`include .../rules.mk`, and (c) hand-wire `SRC +=`/`RAW_ENABLE`. Under the new flow
they add one `"modules"` entry and the build discovers everything automatically.

**Use Case**: A user with a QMK userspace clones the module to
`modules/<org>/qmk_notifier`, adds `"modules": ["<org>/qmk_notifier"]` to `keymap.json`,
includes `notifier.h`, defines the `raw_hid_receive` shim, and `qmk compile`s — done.

**User Journey**: read Setup → clone (underscore leaf) → edit `keymap.json` → edit
`keymap.c` (include + shim) → (optional) `OS_DETECTION_ENABLE`/`DEFINE_HOST_CALLBACKS`
→ compile.

**Pain Points Addressed**: removes the error-prone hand-wiring (wrong relative path,
missing `rules.mk` include, forgotten `RAW_ENABLE`) and documents the one setup mistake
that is a hard failure (a hyphenated leaf). Makes the module self-declaring.

## Why

- **Documents the §18 migration user-facing change (R5).** M1 landed the
  module-context `rules.mk` (P1.M1.T2.S1) and the API guard (P1.M1.T3.S1); the README
  must now tell users the new install flow. The old submodule instructions are
  incompatible with the module-context `rules.mk` (§18.4: "retire the submodule flow
  entirely (recommended)").
- **Prevents the one hard-failure setup mistake (R4).** A hyphenated leaf
  (`qmk-notifier`) produces invalid C tokens (`-DCOMMUNITY_MODULE_qmk-notifier_ENABLE`).
  The README must call this out prominently — it is the setup error most likely to
  waste a user's time.
- **States the irreducible limit honestly (§18.2).** `raw_hid_receive` is not a
  Community Module hook, so the shim is mandatory. A user who expects "one
  `keymap.json` entry and done" must learn the shim is still required.
- **Preserves the feature flags' documentation.** Multi-OS and host-rules notes move
  with the Setup section but stay accurate (R6: no behavioral change).

## What

One section rewrite in `/home/dustin/projects/qmk_notifier/README.md`: replace the
`## Setup` section (lines 49-98, the submodule 3-step flow) with the Community Module
3-step flow + the R4 leaf callout + the §18.2 shim callout + the §18.4 Configurator
note. Preserve the Multi-OS and Host-rules notes (with their anchor links). Remove the
old submodule/relative-include/rules.mk-include artifacts.

### Success Criteria

- [ ] Setup documents clone to `modules/<org>/qmk_notifier` (userspace, underscore leaf).
- [ ] Setup documents one `keymap.json` `"modules": ["<org>/qmk_notifier"]` entry.
- [ ] Setup documents `#include "notifier.h"` (NO relative path) + the `raw_hid_receive` shim.
- [ ] Leaf-naming contract (R4) present as a prominent callout (hyphen = hard failure).
- [ ] `raw_hid_receive`-shim-still-required callout present (§18.2).
- [ ] Configurator note present (§18.4).
- [ ] Multi-OS + Host-rules notes preserved; `#multi-os-configuration` /
      `#host-side-rules--typed-commands` anchor links intact.
- [ ] Old submodule artifacts gone from Setup (in-keyboard clone, relative `#include`,
      `include keyboards`/`rules.mk` line, `SRC +=`/`RAW_ENABLE` hand-wiring).
- [ ] `## Usage` (line 100) follows the new Setup section unchanged; no other section moved.

## All Needed Context

### Context Completeness Check

**Pass.** The exact OLD section (verified verbatim at firmware README lines 49-98) and
the exact NEW section are specified inline below. The boundaries (`## Setup` at 49,
`## Usage` at 100) and the preserved anchor links (`#multi-os-configuration` at the
`## Multi-OS Configuration` heading, `#host-side-rules--typed-commands`) were verified
by grep. The §18.3 R5 canonical clone/include/keymap.json snippets (from the PRD) are
reproduced. The M1 artifact state (rules.mk module-context ✓, API guard ✓,
qmk_module.json pending) was confirmed. An implementer with only this PRP + the
firmware repo can make the single section edit with no guessing.

### Documentation & References

```yaml
# MUST READ — the canonical install flow (R5) to reproduce
- file: PRD.md   (firmware repo; snapshot: plan/004_76ea306f6be9/prd_snapshot.md)
  section: "### 18.3 Requirements → R5 (README install rewrite)" + "R4 (leaf contract)" + "§18.4 (Configurator)"
  why: "R5 gives the EXACT snippets: `git submodule add ... modules/<org>/qmk_notifier`,
        `{ \"modules\": [\"<org>/qmk_notifier\"] }`, and `#include \"notifier.h\"` +
        the raw_hid_receive shim. R4 states the hyphen-free leaf contract (hard failure
        on a hyphenated leaf — invalid C tokens). §18.4 states Configurator can't build
        Community Modules."
  critical: "R5 'Gone vs. today': the in-keyboard clone, the relative #include, the
        include .../rules.mk line, the SRC +=/RAW_ENABLE hand-wiring. 'Still required':
        the raw_hid_receive shim. 'Multi-OS users still set OS_DETECTION_ENABLE = yes
        and call notifier_set_os from process_detected_host_os_kb.'"

# MUST READ — WHY the shim is still required (the §18.2 hard limit)
- file: PRD.md
  section: "### 18.2 Verified build-system mechanics → 'Hard subsystem limit'"
  why: "'raw_hid_receive is NOT in the community-module hook surface ... the module
        therefore cannot auto-register on the Raw HID endpoint; the user must still
        define raw_hid_receive in their keymap and call hid_notify(). This is the one
        piece of glue that is irreducible.' Also: 'VPATH += <module_path> — and VPATH
        is on the compiler include path ... so #include \"notifier.h\" ... resolves.'"
  critical: "The no-relative-path #include works BECAUSE VPATH puts the module dir on -I.
        Cite this so the user trusts `#include \"notifier.h\"` (not a relative path)."

# The architecture grounding (cwd confirmation + artifact status)
- file: plan/004_76ea306f6be9/architecture/system_context.md
  section: "Current Codebase State" + "Key Files" + "Rename Verification"
  why: "Confirms cwd = /home/dustin/projects/qmk_notifier (firmware repo); README ~560
        lines, Setup at 49-98 (submodule flow) → rewrite to module flow (R5); rules.mk
        now module-context; the underscore rename is what makes §18 implementable
        (valid C identifier for the leaf)."
  critical: "The leaf MUST be qmk_notifier (underscore) — the repo slug itself. A user
        who clones to modules/<org>/qmk-notifier (hyphen) hits the R4 hard failure."

# The file being edited — exact anchors
- file: /home/dustin/projects/qmk_notifier/README.md   (firmware repo)
  section: "## Setup (lines 49-98); next heading ## Usage at line 100"
  why: "The section to replace. OLD = 3-step submodule flow (clone into keyboard dir;
        relative #include; include keyboards/.../rules.mk + OS_DETECTION_ENABLE). The
        Multi-OS note (74-76) and Host-rules note (78-84) inside step 2 are PRESERVED."
  pattern: "Numbered subsections (### 1./2./3.) with fenced bash/c/make/json blocks.
            Keep that style; only the content changes."
  gotcha: "PRESERVE the two in-README anchor links ([Multi-OS Configuration]
          (#multi-os-configuration) and [Host-Side Rules & Typed Commands]
          (#host-side-rules--typed-commands)) — they resolve to ## headings elsewhere
          in the README (## Multi-OS Configuration at line 158). Dropping them breaks
          internal navigation."

# What M1 already landed (the flow documents M1's outputs)
- file: plan/004_76ea306f6be9/P1M1T2S1/PRP.md   (firmware repo)
  why: "rules.mk is now the module-context form (RAW_ENABLE = yes + SRC += notifier.c).
        The README's NEW flow therefore tells the user NOTHING about SRC/RAW_ENABLE —
        the module's own rules.mk + discovery handle them. (Old flow told the user to
        write `include .../rules.mk` — that line is gone.)"
- file: plan/004_76ea306f6be9/P1M1T3S1/PRP.md   (firmware repo)
  why: "The API-version assertion guard landed in notifier.c. It is INTERNAL — the
        README does not mention it. Zero overlap with this README-only task."

# Scope boundary — don't overlap the later sweep task
- file: plan/004_76ea306f6be9/tasks.json   (firmware repo)
  section: "P1.M2.T3 (Sync changeset-level documentation)"
  why: "P1.M2.T3.S1 sweeps the REST of the README/overview for any OTHER stray
        submodule references. THIS task rewrites the Setup SECTION only. Clean division:
        rewrite Setup here; P1.M2.T3.S1 mops up other mentions."
  critical: "Do NOT rewrite the repo overview bullets / other sections here — only the
        ## Setup section. Leave other submodule mentions for P1.M2.T3.S1."
```

### Current Codebase tree (relevant slice — firmware repo)

```bash
README.md                 # ← MODIFY (## Setup section, lines 49-98 ONLY). NOTHING ELSE.
notifier.c                # LANDED API guard (P1.M1.T3.S1). DO NOT TOUCH.
notifier.h                # public API. DO NOT TOUCH.
pattern_match.{c,h}       # DO NOT TOUCH.
rules.mk                  # LANDED module-context (RAW_ENABLE + SRC += notifier.c). DO NOT TOUCH.
qmk_module.json           # P1.M1.T1.S1 (Planned — not yet created). DO NOT TOUCH (not this task).
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
README.md                 # MODIFIED: ## Setup section rewritten (submodule -> Community Module flow).
# (no new files; no code/build/wire change — R6)
```

### Known Gotchas of our codebase & Library Quirks

```markdown
CRITICAL — TARGET REPO: edit /home/dustin/projects/qmk_notifier/README.md (firmware
  module repo, underscore). Do NOT edit the crate repo's README (qmk-notifier, hyphen)
  — it has no Setup section and is the wrong product (the Rust transport crate).

CRITICAL — the leaf MUST be qmk_notifier (underscore), never qmk-notifier (hyphen).
  QMK derives -DCOMMUNITY_MODULE_<LEAF>_ENABLE and <leaf> hooks from the leaf dir name;
  a hyphen makes those invalid C tokens => hard build failure. The README callout must
  state this. The recommended clone path is modules/<org>/qmk_notifier.

CRITICAL — PRESERVE the two in-README anchor links: [Multi-OS Configuration]
  (#multi-os-configuration) and [Host-Side Rules & Typed Commands]
  (#host-side-rules--typed-commands). They point at ## headings elsewhere in the README
  (e.g. ## Multi-OS Configuration at line 158). The Multi-OS and Host-rules notes move
  into the new Setup section but KEEP these links.

CRITICAL — the raw_hid_receive shim is STILL required (§18.2). It is NOT a Community
  Module hook, so the module can't auto-register on Raw HID. State this as a callout so
  a user who reads "one keymap.json entry" isn't surprised the shim is mandatory.

GOTCHA — #include "notifier.h" has NO relative path. It resolves because the QMK build
  adds VPATH += <module_path> and VPATH is on -I (§18.2). Do NOT write
  #include "./qmk_notifier/notifier.h" (that was the submodule-flow relative path).

GOTCHA — the user no longer writes SRC += / RAW_ENABLE / include .../rules.mk. The
  module's own rules.mk (RAW_ENABLE = yes + SRC += notifier.c) + the keymap.json
  "modules" discovery handle all of that. The new Setup must NOT mention SRC/RAW_ENABLE.

GOTCHA — Multi-OS: OS_DETECTION_ENABLE = yes goes in the KEYMAP's rules.mk (the user's
  keymap rules.mk, not the module's). State "add to your keymap's rules.mk".

GOTCHA — boundaries: replace lines 49-98 (## Setup through the OS_DETECTION_ENABLE
  block). The next heading ## Usage is at line 100. Do NOT touch ## Usage or anything
  after it. Line numbers may drift by edit time — anchor on the TEXT (## Setup heading;
  ## Usage heading).

GOTCHA — leave OTHER submodule mentions (e.g. repo overview bullets) for P1.M2.T3.S1.
  This task rewrites ONLY the ## Setup section.
```

## Implementation Blueprint

### Data models and structure

None. Pure Markdown text edit (one section).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY README.md — REPLACE the ## Setup section (lines 49-98)
  - FILE: /home/dustin/projects/qmk_notifier/README.md (firmware repo).
  - LOCATE the section from the `## Setup` heading (line 49) through the end of the
    `OS_DETECTION_ENABLE = yes` fenced block (line ~98), immediately before `## Usage`
    (line 100). Anchor on the TEXT, not line numbers (they may drift).
  - REPLACE that entire section with the "Exact code — new ## Setup" block below.
  - PRESERVE: the `## Usage` heading and everything after it (unchanged); the two
    in-README anchor links (#multi-os-configuration, #host-side-rules--typed-commands)
    inside the new Multi-OS/Host-rules notes.
  - REMOVE (gone vs. today): the in-keyboard `git submodule add ... qmk_notifier`;
    the `#include "./qmk_notifier/notifier.h"` relative path; the
    `include keyboards/handwired/.../rules.mk` line; the `SRC +=`/`RAW_ENABLE`
    hand-wiring instruction.
  - ADD: the R4 leaf-naming callout; the §18.2 raw_hid_receive-shim callout; the
    §18.4 Configurator note.

Task 2: VERIFY (no edit) — old artifacts gone, new artifacts present, links intact
  - Run Validation Level 1 (grep: no submodule/relative-include/rules.mk-include in
    Setup; module-flow artifacts present; leaf + shim + Configurator callouts present).
  - Run Validation Level 2 (anchor links still resolve; ## Usage follows Setup).
  - Run Validation Level 3 (only README.md changed; only the Setup region).
```

**Exact code — new `## Setup` section (Task 1).** This replaces the current
`## Setup` … (through the `OS_DETECTION_ENABLE` block) … `## Usage`-adjacent content.
Reproduce verbatim (Mode B — user-facing install narrative):

````markdown
## Setup

qmk_notifier is installed as a **QMK Community Module**: the build discovers the
module's `rules.mk`, its sources, and its include path automatically from a single
`keymap.json` entry — no hand-wired `SRC +=`, `RAW_ENABLE`, or `include .../rules.mk`
line in your keymap.

> **⚠️ Clone to a hyphen-free leaf directory.** QMK derives a module's compile-time
> identity from the *leaf directory name* (`-DCOMMUNITY_MODULE_<LEAF>_ENABLE` and the
> `<leaf>` hook suffixes). A hyphen is **not** a valid C identifier, so the leaf
> directory MUST be `qmk_notifier` (underscore) — **never** `qmk-notifier`. The
> recommended clone path is `modules/<org>/qmk_notifier`. A hyphenated leaf is a
> **hard build failure** (the generated define/hooks would be invalid C tokens), and
> it is the one setup mistake most likely to cost you time.

### 1. Clone the module into your userspace

```bash
cd /path/to/your/userspace
git submodule add https://github.com/dabstractor/qmk_notifier.git modules/<org>/qmk_notifier
```

### 2. Add the module to your keymap

Add one entry to the `modules` array in your `keymap.json`:

```json
{
    "modules": ["<org>/qmk_notifier"]
}
```

### 3. Wire `raw_hid_receive` in `keymap.c`

> **This shim is still required.** `raw_hid_receive` is **not** a Community Module
> hook (it is not in QMK's module hook surface), so the module cannot auto-register on
> the Raw HID endpoint. You must define `raw_hid_receive` yourself and forward to
> `hid_notify()` — this is the one irreducible piece of glue.

```c
#include QMK_KEYBOARD_H
#include "notifier.h"   /* the module dir is on the include path via VPATH — no relative path needed */

void raw_hid_receive(uint8_t *data, uint8_t length) {
    hid_notify(data, length);
    /* other Raw HID modules can be called here too */
}
```

**Multi-OS users** add `OS_DETECTION_ENABLE = yes` to the keymap's `rules.mk` and
override `process_detected_host_os_kb` to push the detected OS into the module — see
[Multi-OS Configuration](#multi-os-configuration) for the one-line wiring.

**Host-rules users** additionally define a named callback registry with
`DEFINE_HOST_CALLBACKS({ … })` in `keymap.c` (see
[Host-Side Rules & Typed Commands](#host-side-rules--typed-commands)). No `rules.mk`
change is required for host rules — the host (QMKonnect) negotiates capability
automatically at connect via the `QUERY_INFO` typed command, then drives `SET_OS` +
`APPLY_HOST_CONTEXT`. Omit the macro and the module behaves exactly as before.

> **Configurator.** Community Modules cannot be built by the QMK Configurator, and
> this module also requires a custom `raw_hid_receive` in `keymap.c` (which JSON
> keymaps cannot express) — so Configurator was never an option. Use a source
> `keymap.c` together with a `keymap.json`.
````

> The OLD block being replaced is the current `## Setup` heading + the three
> `### 1./2./3.` subsections (clone-into-keyboard-dir; `#include "./qmk_notifier/notifier.h"`
> + shim + Multi-OS/Host-rules notes; `include keyboards/handwired/.../rules.mk` +
> `OS_DETECTION_ENABLE`). Match on the `## Setup` and `## Usage` headings (the section
> boundaries), not on line numbers.

### Implementation Patterns & Key Details

```markdown
PATTERN: one section in, one section out. Replace exactly the ## Setup .. (pre-## Usage)
  region. Do not touch ## Usage or later sections.

PATTERN: keep the numbered-subsection + fenced-block style of the existing README.
  The new flow is 3 steps (clone / keymap.json / keymap.c shim) — same shape, new content.

PATTERN: callouts for the load-bearing limits. Three blockquotes: (1) R4 leaf contract
  (hyphen = hard failure); (2) §18.2 shim-still-required; (3) §18.4 Configurator.

ANTI-PATTERN: do NOT reintroduce a relative #include. The module dir is on -I via
  VPATH; use #include "notifier.h" (no ./qmk_notifier/ prefix).

ANTI-PATTERN: do NOT tell the user to write SRC += / RAW_ENABLE / include .../rules.mk.
  Those are gone — the module's rules.mk + discovery handle them.

ANTI-PATTERN: do NOT drop the Multi-OS/Host-rules notes or their anchor links. They
  move into the new step-3 area but keep #multi-os-configuration /
  #host-side-rules--typed-commands.

ANTI-PATTERN: do NOT edit the crate repo (qmk-notifier, hyphen). The target is the
  firmware repo (qmk_notifier, underscore).

ANTI-PATTERN: do NOT rewrite other README sections (overview bullets, etc.) — that is
  P1.M2.T3.S1's sweep. Only ## Setup.
```

### Integration Points

```yaml
README.MD (firmware repo, /home/dustin/projects/qmk_notifier/):
  - replace: ## Setup section (lines 49-98)
  - preserve: ## Usage (line 100) + all later sections; the two anchor links.
INTERNAL LINKS:
  - #multi-os-configuration  -> ## Multi-OS Configuration (README:158) — keep.
  - #host-side-rules--typed-commands -> the Host-Side Rules ## heading — keep.
DOWNSTREAM (NOT this task):
  - P1.M2.T2.S1: re-run host gates to confirm R6 (no behavioral change). README-only
    change can't affect the gates, but R6 verification is that task's scope.
  - P1.M2.T3.S1: sweep the REST of the README/overview for other stray submodule refs.
BUILD/CONFIG/ROUTES/DATABASE:
  - none. Pure Markdown documentation; no code/build/wire/protocol change (R6).
```

## Validation Loop

> No compiler/tests are affected (Markdown-only; R6 = no behavioral change). Validation
> is grep-based: old artifacts gone, new artifacts present, links intact, scope clean.
> Run from the **firmware repo** (`/home/dustin/projects/qmk_notifier`).

### Level 1: Old artifacts GONE / new artifacts PRESENT

```bash
cd /home/dustin/projects/qmk_notifier

# 1a. OLD submodule-flow artifacts are GONE from the Setup section.
#   (Allow the SAME strings elsewhere in the README only if P1.M2.T3.S1 hasn't swept
#    them yet — but they must NOT appear in the ## Setup region.)
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md > /tmp/setup_section.md
! grep -qE 'git submodule add.*qmk_notifier$|qmk_notifier\.git qmk_notifier' /tmp/setup_section.md \
  && echo "OK: in-keyboard clone gone" || echo "FAIL: in-keyboard clone still in Setup"
! grep -q './qmk_notifier/notifier.h' /tmp/setup_section.md \
  && echo "OK: relative #include gone" || echo "FAIL: relative #include still in Setup"
! grep -qE 'include keyboards' /tmp/setup_section.md \
  && echo "OK: rules.mk include line gone" || echo "FAIL: rules.mk include line still in Setup"
! grep -qE 'SRC \+=' /tmp/setup_section.md \
  && echo "OK: no SRC += hand-wiring in Setup" || echo "FAIL: SRC += still in Setup"

# 1b. NEW module-flow artifacts are PRESENT in Setup.
grep -q 'modules/<org>/qmk_notifier' /tmp/setup_section.md && echo "OK: userspace clone path"
grep -q '"modules"' /tmp/setup_section.md && echo "OK: keymap.json modules entry"
grep -q '#include "notifier.h"' /tmp/setup_section.md && echo "OK: no-relative-path include"
grep -q 'raw_hid_receive' /tmp/setup_section.md && echo "OK: raw_hid_receive shim"

# 1c. The three callouts are present.
grep -qi 'hyphen' /tmp/setup_section.md && echo "OK: R4 leaf-naming callout"
grep -qi 'not.*a Community Module hook\|not in QMK.*module hook\|still required' /tmp/setup_section.md && echo "OK: §18.2 shim callout"
grep -qi 'Configurator' /tmp/setup_section.md && echo "OK: §18.4 Configurator note"
rm -f /tmp/setup_section.md
```

### Level 2: Preserved notes + anchor links + structure

```bash
cd /home/dustin/projects/qmk_notifier

# 2a. Multi-OS + Host-rules notes preserved WITH their anchor links.
grep -q 'process_detected_host_os_kb' README.md && echo "OK: Multi-OS note preserved"
grep -q 'DEFINE_HOST_CALLBACKS' README.md && echo "OK: Host-rules note preserved"
grep -q '(#multi-os-configuration)' README.md && echo "OK: multi-os anchor link intact"
grep -q '(#host-side-rules--typed-commands)' README.md && echo "OK: host-rules anchor link intact"

# 2b. The anchor targets still exist as ## headings.
grep -q '^## Multi-OS Configuration' README.md && echo "OK: ## Multi-OS Configuration heading exists"
grep -q '^## Host-Side Rules' README.md && echo "OK: ## Host-Side Rules heading exists"

# 2c. Structure: ## Setup is immediately followed (after its body) by ## Usage; nothing else moved.
grep -nE '^## (Setup|Usage|Multi-OS Configuration)' README.md
# Expected: ## Setup, then ## Usage, then ## Multi-OS Configuration — in that order, unchanged relative positions.
```

### Level 3: Scope hygiene (only README.md, only the Setup region)

```bash
cd /home/dustin/projects/qmk_notifier

# 3a. Only README.md changed in source (besides plan/ artifacts).
git status --porcelain | grep -vE '^\?\? plan/' | grep -E 'README\.md|\.c|\.h|\.mk|\.json|\.sh' \
  | grep -v README.md && echo "FAIL: a source file other than README.md changed" || echo "OK: only README.md (source)"
git diff --stat -- README.md
# Expected: only README.md; 1 file changed.

# 3b. The diff is confined to the Setup region (no other section altered).
git diff -- README.md | grep -E '^@@' 
# Expected: ONE hunk covering the ## Setup .. ## Usage region. No hunk in ## Usage body or later.

# 3c. Sanity: the API guard (P1.M1.T3.S1) and module-context rules.mk are UNTOUCHED.
git diff --stat -- notifier.c rules.mk qmk_module.json 2>/dev/null
# Expected: no output (these files are not changed by this README task).
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk_notifier

# 4a. A reader can install from the Setup section alone (the 3 steps are self-sufficient).
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md | \
  grep -qE 'git submodule add.*modules/' && echo "step1 clone: present"
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md | \
  grep -q '"modules"' && echo "step2 keymap.json: present"
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md | \
  grep -q 'hid_notify' && echo "step3 shim: present"

# 4b. The leaf contract is unambiguous (states BOTH the requirement and the failure).
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md | \
  grep -qiE 'hard.*failure|hard build failure' && echo "OK: hard-failure consequence stated"
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md | \
  grep -qiE 'qmk_notifier.*underscore|never.*qmk-notifier' && echo "OK: underscore-required stated"

# 4c. The no-relative-path include is explained (VPATH), not just asserted.
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md | grep -qi 'VPATH\|include path' \
  && echo "OK: VPATH rationale present" || echo "WARN: VPATH rationale missing"

# 4d. No behavioral-change claim leaked in (R6 — this is install docs only).
git diff -- README.md | grep -iE 'wire protocol|matcher|dispatch' \
  && echo "WARN: diff touches behavior topics (should be install-only)" \
  || echo "OK: diff is install-docs only (R6 respected)"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: old submodule artifacts gone from Setup (clone-into-keyboard, relative `#include`, `include keyboards`/`rules.mk`, `SRC +=`).
- [ ] Level 1: new module-flow artifacts present (userspace clone, `keymap.json` `"modules"`, no-relative `#include`, shim).
- [ ] Level 1: three callouts present (R4 leaf, §18.2 shim, §18.4 Configurator).
- [ ] Level 2: Multi-OS + Host-rules notes + their anchor links preserved; anchor targets exist.
- [ ] Level 2: `## Setup` → `## Usage` → `## Multi-OS Configuration` order unchanged.
- [ ] Level 3: only README.md changed; diff confined to the Setup region.
- [ ] Level 4: install is self-sufficient; leaf contract states requirement + failure; VPATH rationale present; R6 respected.

### Feature Validation

- [ ] A user can install from the Setup section alone (clone + keymap.json + shim).
- [ ] The leaf-naming contract (R4) is stated prominently (hyphen = hard failure).
- [ ] The `raw_hid_receive` shim is documented as still required (§18.2).
- [ ] The Configurator limitation is noted (§18.4).
- [ ] Multi-OS (`OS_DETECTION_ENABLE` + `notifier_set_os`) and Host-rules (`DEFINE_HOST_CALLBACKS`) notes are accurate and unchanged.

### Code Quality Validation

- [ ] Only Markdown changed; no code/build/wire/protocol change (R6).
- [ ] Edit confined to the `## Setup` section.
- [ ] No anti-patterns (see below): no relative include, no SRC/RAW_ENABLE instruction, no crate-repo edit, no other-section rewrite.

### Documentation & Deployment

- [ ] This IS the changeset-level documentation task (Mode B, item §6).
- [ ] The new flow documents what M1 landed (module-context rules.mk, API guard).
- [ ] P1.M2.T3.S1 (other submodule-mention sweep) + P1.M2.T2.S1 (R6 gate re-run) are left to their own scope.

---

## Anti-Patterns to Avoid

- ❌ Don't edit the crate repo (`qmk-notifier`, hyphen). The target is the firmware repo (`qmk_notifier`, underscore) — its README has the `## Setup` section.
- ❌ Don't reintroduce a relative `#include "./qmk_notifier/notifier.h"`. Use `#include "notifier.h"` (VPATH puts the module dir on `-I`).
- ❌ Don't tell the user to write `SRC +=` / `RAW_ENABLE` / `include .../rules.mk`. Those are gone — the module's own `rules.mk` + discovery handle them.
- ❌ Don't drop or reword the Multi-OS / Host-rules notes or their `#multi-os-configuration` / `#host-side-rules--typed-commands` anchor links — preserve them.
- ❌ Don't omit the leaf-naming callout (R4) — a hyphenated leaf is the one hard-failure setup mistake; it must be prominent.
- ❌ Don't omit the `raw_hid_receive`-shim-still-required callout (§18.2) — users will assume "one entry and done."
- ❌ Don't rewrite other README sections (overview bullets, etc.) — that is P1.M2.T3.S1's sweep. Only `## Setup`.
- ❌ Don't touch notifier.c, notifier.h, pattern_match.*, rules.mk, qmk_module.json, tests, run_*.sh, PRD.md, tasks.json — README only.
- ❌ Don't anchor the edit on line numbers — anchor on the `## Setup` and `## Usage` headings (lines may drift).

---

## Confidence Score: 9/10

The deliverable is a single Markdown section rewrite (`## Setup`, firmware README
lines 49-98) whose exact OLD text (verified verbatim) and exact NEW text (reproducing
the PRD §18.3 R5 canonical snippets + the R4/§18.2/§18.4 callouts) are specified inline.
The boundaries (`## Setup` at 49, `## Usage` at 100) and the preserved anchor links
were verified by grep; the M1 artifact state (module-context `rules.mk` ✓, API guard ✓)
was confirmed. The one reason this is not 10/10: **the repo disambiguation** — the
harness cwd is the Rust *crate* repo, but the task unambiguously targets the *firmware*
repo (`/home/dustin/projects/qmk_notifier`, where `tasks.json`, the prior PRPs, the
`## Setup` section, and the M1 artifacts all live). This PRP is written to the firmware
repo's `plan/004` path and explicitly targets `/home/dustin/projects/qmk_notifier/README.md`;
the implementer must operate in the firmware repo. Aside from that environmental note,
the section edit is fully specified and low-risk (Markdown-only; R6 = no behavioral
change; host gates unaffected — R6 re-verification is P1.M2.T2.S1's separate scope).