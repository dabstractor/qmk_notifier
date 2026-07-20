name: "PRP — P1.M2.T3.S1: Sweep README and overview docs for stale submodule references"
description: |

  Mode B (changeset-level documentation sync), final §18 doc task. The firmware
  README's `## Setup` section was already rewritten to the Community Module flow by
  P1.M2.T1.S1. THIS task sweeps the REST of the README for any stale submodule-flow
  references that the Setup rewrite did not cover, and surfaces the Community Module
  distribution option. Findings: exactly ONE stale reference survives (a relative
  `#include "./qmk_notifier/notifier.h"` in the `## Multi-OS Configuration` snippet);
  the Companion Projects naming is already correct; there is no README file inventory
  to update. So the deliverable is a small, surgical README edit. Documentation only.

  > **⚠️ TARGET REPO:** `/home/dustin/projects/qmk_notifier` (firmware, underscore) —
  > NOT the harness cwd `/home/dustin/projects/qmk-notifier` (the Rust crate, hyphen).
  > The README under edit, the §18 artifacts, and this plan all live in the firmware repo.

---

## Goal

**Feature Goal**: Make the firmware README fully consistent with the §18 Community Module
Distribution migration end-to-end: no stale submodule-flow reference remains anywhere in
the README, and the Community Module distribution option is surfaced in the feature list.
After P1.M2.T1.S1 rewrote `## Setup`, the remaining cross-cutting doc surface must be
swept so a reader never sees a contradictory (submodule-flow) install path.

**Deliverable**: ONE file modified — `/home/dustin/projects/qmk_notifier/README.md`.
Three findings drive a small edit set:
1. **MANDATORY FIX** — the `## Multi-OS Configuration` → `### How to enable` step-2 code
   snippet (README ~line 188-205) still uses the submodule-flow relative include
   `#include "./qmk_notifier/notifier.h"`. Replace it with the Community Module flow
   (`#include "notifier.h"`, no relative path) and trim the duplicated `raw_hid_receive`
   shim (already documented in Setup step 3) so the snippet shows only the multi-OS-specific
   `process_detected_host_os_kb` override.
2. **RECOMMENDED ADD (item 3e)** — one bullet in the `## Features` list surfacing the
   Community Module distribution option, linking to `#setup`.
3. **VERIFY-ONLY (item 3c) / N/A (items 3d, 3f)** — the `## Companion Projects` naming
   note already uses the correct convention (`qmk_notifier` firmware / `qmk-notifier`
   Rust crate); there is no README file inventory to update (§17's file-size table is
   PRD-only); `qmk_module.json` is absent (M1.T1 gap). No change for these — just confirm.

**Success Definition**:
- `grep -n 'include "\./' README.md` returns ZERO hits (no relative include anywhere).
- `grep -n './qmk_notifier/notifier.h' README.md` returns ZERO hits.
- The Multi-OS "How to enable" snippet uses `#include "notifier.h"` and shows
  `process_detected_host_os_kb` (the raw_hid_receive shim is no longer duplicated there).
- The Features list has a Community Module bullet linking to `#setup`.
- The `## Setup` section is UNCHANGED (still the Community Module flow — no accidental
  regression); `## Companion Projects` naming note is UNCHANGED (still correct).
- `git status` shows ONLY `README.md` modified (besides plan/ PRP/research).

## User Persona (if applicable)

**Target User**: A keymap author reading the Multi-OS Configuration section to wire
`notifier_set_os`, and a prospective adopter skimming the Features list. Both must see a
single, consistent install story (Community Module flow), never a contradictory relative
`#include` that only works under the retired submodule flow.

**Use Case**: A user follows `## Setup` (Community Module: `keymap.json` + `#include "notifier.h"`),
then jumps to Multi-OS Configuration to add OS detection. The Multi-OS snippet must show the
SAME include style — not a stale `#include "./qmk_notifier/notifier.h"` that implies a
relative path the Community Module flow does not use.

**User Journey**: Features (sees "Installs as a Community Module") → Setup (module flow) →
Multi-OS Configuration (snippet now consistent: `#include "notifier.h"` + the OS push).

**Pain Points Addressed**: A reader who copies the Multi-OS snippet's relative include into a
Community Module keymap gets a broken build (the relative path only resolves in the submodule
flow). Sweeping it removes the last contradictory install instruction.

## Why

- **Closes the §18 doc migration (R5, final cross-cutting sweep).** P1.M2.T1.S1 rewrote
  `## Setup`; this task mops up the one other place the submodule-flow relative include
  survived (the Multi-OS snippet) and surfaces the distribution option in Features. Per the
  §18.5 acceptance "README documents only the module flow", NO submodule-flow install path
  may remain.
- **The relative include is the load-bearing stale reference.** Under the Community Module
  flow the module dir is on `-I` via VPATH (§18.2), so the include is `#include "notifier.h"`
  — exactly what Setup step 3 already documents. The Multi-OS snippet contradicting that is
  the bug; aligning it is the fix.
- **Low risk.** Markdown-only; no code/build/wire/protocol change (R6). The host gates are
  unaffected (a README edit cannot change behavior); R6 re-verification is P1.M2.T2.S1's scope.
- **Honest about the N/A items.** There is no README file inventory (so nothing to add
  `qmk_module.json` to), §17's file-size table is PRD-only, and `qmk_module.json` is absent
  (M1.T1 gap). Documenting these as N/A prevents the implementer from inventing an inventory
  or creating the manifest out of scope.

## What

A surgical edit to `/home/dustin/projects/qmk_notifier/README.md`:

1. **Fix the Multi-OS snippet** (`### How to enable` step 2): replace the stale
   `#include "./qmk_notifier/notifier.h"` + duplicated `raw_hid_receive` shim block with the
   Community Module-flow include (`#include "notifier.h"`) and just the
   `process_detected_host_os_kb` override (the multi-OS-specific part). Exact old/new text in
   "Implementation Tasks".
2. **Add a Features bullet** for the Community Module distribution option (exact text given).
3. **Confirm (no edit)** the Companion Projects naming note is already correct, and that no
   file-inventory change is needed.

### Success Criteria

- [ ] `grep -n 'include "\./' README.md` → 0 hits (no relative include anywhere).
- [ ] `grep -n './qmk_notifier/notifier.h' README.md` → 0 hits.
- [ ] The Multi-OS "How to enable" step-2 snippet uses `#include "notifier.h"` (no relative
      path) and shows `process_detected_host_os_kb`; the duplicated `raw_hid_receive` shim is
      removed from that snippet (it lives in Setup step 3).
- [ ] The `## Features` list has a Community Module bullet linking to `(#setup)`.
- [ ] `## Setup` is byte-identical to its post-P1.M2.T1.S1 state (no regression); the
      `## Companion Projects` naming note is unchanged and still correct.
- [ ] `git status` shows only `M README.md` (+ plan/ PRP/research); no source/build/test file changed.

## All Needed Context

### Context Completeness Check

**Pass.** The exact current text of the stale Multi-OS snippet (verified verbatim at firmware
README ~lines 188-205), the exact Features list (verified), the Companion Projects naming note
(verified already correct), and the §18 Community Module flow (PRD §18.2/§18.3 R4/R5) are all
captured. A whole-README grep confirmed the relative include is the ONLY stale submodule-flow
reference outside `## Setup`. The repo disambiguation (firmware vs crate) is resolved. An
implementer with only this PRP + the firmware repo can apply the surgical edit with no guessing.

### Documentation & References

```yaml
# MUST READ — the Community Module flow the snippet must match (R5)
- file: PRD.md   (firmware repo; snapshot: plan/004_76ea306f6be9/prd_snapshot.md)
  section: "### 18.3 Requirements → R5 (README install rewrite)" + "R4 (leaf contract)"
  why: "R5 'Gone vs. today': the in-keyboard clone, the #include \"./qmk_notifier/notifier.h\"
        relative path, the include .../rules.mk line, the SRC +=/RAW_ENABLE hand-wiring.
        'Still required': the raw_hid_receive shim. So the Multi-OS snippet's relative include
        is exactly the stale artifact to remove; the correct include is #include \"notifier.h\"
        (resolved via VPATH, §18.2)."
  critical: "Under the module flow the include is #include \"notifier.h\" (NO relative path).
        Do NOT reintroduce any ./qmk_notifier/ prefix."

# MUST READ — why the include has no relative path (VPATH)
- file: PRD.md
  section: "### 18.2 Verified build-system mechanics → 'VPATH += <module_path>'"
  why: "'VPATH is on the compiler include path ... so #include \"notifier.h\" from the user's
        keymap ... resolves.' This is why Setup step 3 (and now the Multi-OS snippet) use a
        bare #include \"notifier.h\" — the module dir is on -I automatically."
  critical: "The Multi-OS snippet must match Setup's include style. Setup step 3 already
        documents #include \"notifier.h\" + the raw_hid_receive shim; the Multi-OS snippet
        should NOT duplicate the shim."

# MUST READ — the exact stale site to fix (verified verbatim)
- file: /home/dustin/projects/qmk_notifier/README.md   (firmware repo)
  section: "## Multi-OS Configuration → ### How to enable → step 2 code block (~lines 188-205)"
  why: "The step-2 code block currently opens with `#include QMK_KEYBOARD_H` +
        `#include \"./qmk_notifier/notifier.h\"` and re-shows the full raw_hid_receive shim,
        then process_detected_host_os_kb. The relative include is the submodule-flow artifact;
        the shim duplicates Setup step 3."
  pattern: "Replace that code block with the Community Module-flow version (bare #include
            \"notifier.h\" + process_detected_host_os_kb only). The step-2 PROSE ('In keymap.c,
            push the detected OS into the module by overriding process_detected_host_os_kb.
            This is the one required call:') already describes just that override, so the
            trimmed snippet aligns with its own description."
  gotcha: "Anchor on the exact code-block text (the lines inside the ```c fence), NOT line
           numbers (the README shifts as edits land). Re-grep first."

# MUST READ — the Features list (where the Community Module bullet goes)
- file: /home/dustin/projects/qmk_notifier/README.md
  section: "## Features (the bullet list, ~lines 5-23)"
  why: "The list covers matching/layers/callbacks/multi-OS/host-rules but NOT the distribution
        option. Item 3e: 'Any feature list or capability overview that should mention the
        Community Module distribution option.' Add one bullet linking to #setup."
  pattern: "Existing opt-in bullets are bolded lead-ins ('**Optional per-OS maps** — ...').
            Match that style for the new bullet."
  gotcha: "Place the new bullet so it does not split the existing per-OS / host-rules bullets.
           Append it as the LAST Features bullet (after the host-rules bullet)."

# VERIFY-ONLY — the naming note is already correct (do NOT change)
- file: /home/dustin/projects/qmk_notifier/README.md
  section: "## Companion Projects → the '> **Naming note:**' blockquote"
  why: "It already states 'qmk_notifier (underscore) is this firmware C module. qmk-notifier
        (hyphen) is the Rust transport crate.' and links the crate to
        github.com/dabstractor/qmk-notifier. This matches §18 (firmware renamed to underscore).
        Confirmed against the crate README (which references qmk_notifier underscore as the
        firmware)."
  critical: "Item 3c is ALREADY satisfied. Do NOT reword the naming note or swap the slugs.
        If you are tempted to 'fix' it, STOP — it is correct."

# What P1.M2.T1.S1 already did (the boundary — do NOT re-edit Setup)
- file: plan/004_76ea306f6be9/P1M2T1S1/PRP.md   (firmware repo)
  why: "P1.M2.T1.S1 rewrote ## Setup to the Community Module flow and EXPLICITLY left 'other
        submodule mentions (repo overview bullets, etc.)' for THIS task. So: ## Setup is
        already correct; do NOT touch it."
  critical: "The grep hits for 'SRC +=', 'RAW_ENABLE', 'include .../rules.mk', 'git submodule
        add', 'qmk-notifier' INSIDE ## Setup are all CORRECT (negative phrasing / userspace
        clone / R4 leaf contract). Leave them. Only the Multi-OS snippet's relative include is stale."

# Scope boundary — the parallel verification task (no overlap)
- file: plan/004_76ea306f6be9/P1M2T2S1/PRP.md   (firmware repo)
  why: "P1.M2.T2.S1 runs the host gates + the R6 byte-identical check and writes no docs/source.
        This README edit is Markdown-only and cannot affect the gates or R6."
  critical: "Do NOT run/alter the gates — that is P1.M2.T2.S1. This task is README only."

# Scope boundary — qmk_module.json (item 3d) is N/A
- file: (firmware repo root)   `ls qmk_module.json`
  why: "qmk_module.json is ABSENT (P1.M1.T1 FAILED; not gitignored). And the README has NO file
        inventory / file-size table (only §17 in the PRD has one, and item 3f says no README
        change is needed for line counts). So item 3d (add qmk_module.json to a file inventory)
        is N/A — there is nothing to add to."
  critical: "Do NOT invent a file inventory in the README. Do NOT create qmk_module.json (that
        is M1.T1's deliverable, out of scope here)."

# §17 file-size table (item 3f) — PRD-only, no README change
- file: PRD.md   (firmware repo)
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"
  why: "The file-size table lives in the PRD, not the README. Item 3f: 'no README change needed
        for line counts unless the README references them.' The README does NOT reference them."
  critical: "Do NOT copy §17's table into the README or edit it. It is PRD-only."
```

### Current Codebase tree (relevant slice — firmware repo)

```bash
README.md                 # ← MODIFY (Multi-OS snippet include [mandatory] + Features bullet [recommended]). ONLY file changed.
notifier.c                # LANDED R3 guard + multi-OS/host-rules logic. DO NOT TOUCH.
notifier.h                # public API. DO NOT TOUCH.
pattern_match.{c,h}       # DO NOT TOUCH.
rules.mk                  # LANDED R2 module-context (RAW_ENABLE + SRC += notifier.c). DO NOT TOUCH.
qmk_module.json           # ABSENT (M1.T1 gap). DO NOT TOUCH / DO NOT CREATE (not this task).
run_all_tests.sh          # DO NOT TOUCH (P1.M2.T2.S1 owns the gate run).
run_notifier_stub_tests.sh# DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
README.md                 # MODIFIED: Multi-OS "How to enable" snippet → Community Module include (no relative path,
                           #   no duplicated shim); +1 Features bullet (Community Module distribution).
# (no other files change)
```

### Known Gotchas of our codebase & Library Quirks

```markdown
CRITICAL — TARGET REPO: edit /home/dustin/projects/qmk_notifier/README.md (firmware, underscore).
  The harness cwd /home/dustin/projects/qmk-notifier (hyphen) is the Rust CRATE repo — it has
  no notifier.c, no Setup section, no Multi-OS Configuration section. cd to the firmware repo.

CRITICAL — this is the FINAL cross-cutting sweep, NOT a Setup rewrite. P1.M2.T1.S1 already
  rewrote ## Setup (correctly). Do NOT touch ## Setup. The grep hits inside ## Setup for
  'SRC +=', 'RAW_ENABLE', 'include .../rules.mk', 'git submodule add', 'qmk-notifier' are all
  CORRECT (negative phrasing / the userspace clone / the R4 leaf contract). Leave them.

CRITICAL — the relative include #include "./qmk_notifier/notifier.h" is the ONLY stale ref
  outside Setup (whole-README grep confirmed). It lives in the Multi-OS "How to enable" step-2
  snippet. Replace it with #include "notifier.h" (Community Module flow, VPATH-resolved).

GOTCHA — anchor on exact text, not line numbers. The README shifts as edits land. Re-grep
  `grep -n 'include "\./' README.md` (or './qmk_notifier/notifier.h') to locate the stale site.

GOTCHA — do NOT reintroduce a relative #include anywhere. The Community Module flow uses a
  bare #include "notifier.h" (the module dir is on -I via VPATH, §18.2). Setup step 3 already
  documents this; the Multi-OS snippet must match.

GOTCHA — the Multi-OS snippet currently DUPLICATES the raw_hid_receive shim from Setup step 3.
  Trimming the snippet to just process_detected_host_os_kb (+ the bare include) both removes
  the stale relative path AND de-duplicates. Do NOT remove the prose; only the code block changes.

GOTCHA — item 3c (Companion Projects naming) is ALREADY correct. The naming note says
  'qmk_notifier (underscore) = firmware; qmk-notifier (hyphen) = crate'. This matches §18.
  Do NOT reword or swap it.

GOTCHA — items 3d (file inventory) and 3f (§17 line counts) are N/A. The README has no file
  inventory; §17 is PRD-only; qmk_module.json is absent (M1.T1 gap). Do NOT invent an inventory,
  do NOT copy §17, do NOT create qmk_module.json.

GOTCHA — markdown hygiene: keep the ```c fence balanced (open + close); keep heading levels
  unchanged; preserve the existing anchor links (#setup, #multi-os-configuration,
  #host-side-rules--typed-commands). A new (#setup) link in the Features bullet relies on the
  ## Setup heading slug (lowercase, spaces→hyphens).
```

## Implementation Blueprint

### Data models and structure

None. Pure Markdown text edit (one snippet replaced + one bullet appended).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: FIX the Multi-OS "How to enable" step-2 snippet (MANDATORY — item 3a/3b)
  - FILE: /home/dustin/projects/qmk_notifier/README.md (firmware repo).
  - LOCATE: the code block under "### How to enable" → step 2 (the one introduced by
    "2. In `keymap.c`, push the detected OS into the module by overriding
    `process_detected_host_os_kb`. This is the **one** required call:").
    Re-grep `grep -n './qmk_notifier/notifier.h' README.md` to find it (single hit).
  - oldText (EXACT current ```c block contents):
        #include QMK_KEYBOARD_H
        #include "./qmk_notifier/notifier.h"

        void raw_hid_receive(uint8_t *data, uint8_t length) {
            hid_notify(data, length);
            /* other Raw HID modules can be called here too */
        }

        /* Multi-OS only: the sole required call to feed the detected OS in. */
        bool process_detected_host_os_kb(os_variant_t os) {
            notifier_set_os(os);          /* enables DEFINE_*_OS map selection */
            /* …your existing OS-specific logic (e.g. enable_vim_for_mac())… */
            return true;
        }
  - newText (Community Module flow — bare include, no relative path, no duplicated shim):
        #include QMK_KEYBOARD_H
        #include "notifier.h"   /* module dir on the include path via VPATH (see Setup) — no relative path */

        /* Multi-OS only: the sole required call to feed the detected OS in.
         * (The raw_hid_receive → hid_notify shim from Setup step 3 is still required too.) */
        bool process_detected_host_os_kb(os_variant_t os) {
            notifier_set_os(os);          /* enables DEFINE_*_OS map selection */
            /* …your existing OS-specific logic (e.g. enable_vim_for_mac())… */
            return true;
        }
  - PRESERVE: the surrounding prose (step 1 OS_DETECTION_ENABLE block; step 2's lead sentence;
    the "### Defining per-OS maps" heading that follows). Only the ```c code block changes.
  - WHY: removes the last submodule-flow relative include; aligns the snippet with its own
    description ("override process_detected_host_os_kb. This is the one required call") and
    with Setup's Community Module flow.

Task 2: ADD a Features bullet for the Community Module distribution option (RECOMMENDED — item 3e)
  - FILE: same README.md.
  - LOCATE: the `## Features` bullet list; append the new bullet as the LAST item (after the
    "**Host-side rules & typed commands (opt-in)** — …" bullet, immediately before `## How It Works`).
  - oldText (anchor — the last lines of the host-rules bullet + the next heading):
          board `DEFINE_SERIAL_*` rules. Strictly opt-in: a keymap without
          `DEFINE_HOST_CALLBACKS` is byte-for-byte unchanged. See
          [Host-Side Rules & Typed Commands](#host-side-rules--typed-commands).

        ## How It Works
  - newText (append one bullet, then the heading):
          board `DEFINE_SERIAL_*` rules. Strictly opt-in: a keymap without
          `DEFINE_HOST_CALLBACKS` is byte-for-byte unchanged. See
          [Host-Side Rules & Typed Commands](#host-side-rules--typed-commands).
        - **Installs as a QMK Community Module** — a single `keymap.json` entry discovers the
          build wiring (`rules.mk`, sources, and include path) automatically; no hand-wired
          `SRC +=`, `RAW_ENABLE`, or `rules.mk` include in your keymap. See [Setup](#setup).

        ## How It Works
  - PRESERVE: all existing Features bullets and their order; the `## How It Works` heading.

Task 3: VERIFY (no edit) — Companion Projects naming already correct (item 3c); no file
        inventory / §17 change needed (items 3d, 3f). Run the Validation Loop.
```

### Implementation Patterns & Key Details

```markdown
PATTERN: one stale site in, one fixed site out. The whole-README grep proved the relative
  include appears exactly ONCE outside ## Setup (the Multi-OS snippet). Fix that one site;
  do not hunt for more (there are none). Re-grep after the edit to confirm 0 hits.

PATTERN: match the existing include style. Setup step 3 uses `#include "notifier.h"` with an
  inline comment about VPATH. The Multi-OS snippet now mirrors that exactly (bare include,
  VPATH note). Consistency is the goal.

PATTERN: de-duplicate, don't restate. The raw_hid_receive shim is fully documented in Setup
  step 3; the Multi-OS snippet references it in a comment rather than re-pasting it. This
  keeps a single source of truth for the shim.

PATTERN: bullet style for the Features add. Existing opt-in bullets use a bolded lead-in
  ("**Optional per-OS maps** — …") and often an anchor link. The new bullet matches that
  shape ("**Installs as a QMK Community Module** — … See [Setup](#setup).").

ANTI-PATTERN: do NOT reintroduce any relative #include (./qmk_notifier/…). The Community
  Module flow resolves `#include "notifier.h"` via VPATH; a relative path is the submodule-flow
  artifact being swept away.

ANTI-PATTERN: do NOT touch ## Setup. P1.M2.T1.S1 rewrote it correctly. The grep hits inside
  ## Setup (SRC +=, RAW_ENABLE, include .../rules.mk, git submodule add, qmk-notifier) are all
  intentional and correct — leave them.

ANTI-PATTERN: do NOT reword the Companion Projects naming note (item 3c). It is already
  correct (qmk_notifier firmware / qmk-notifier crate). Swapping the slugs would INTRODUCE a
  bug.

ANTI-PATTERN: do NOT invent a README file inventory or copy §17's table (items 3d/3f). There
  is no inventory to update; §17 is PRD-only; qmk_module.json is absent (M1.T1 gap). These
  items are N/A — document them as such, do not act on them.

ANTI-PATTERN: do NOT edit the crate repo (qmk-notifier, hyphen). The target is the firmware
  repo (qmk_notifier, underscore).

ANTI-PATTERN: do NOT run or alter the test gates — that is P1.M2.T2.S1 (parallel). This task
  is README-only; a Markdown edit cannot affect the gates or R6.
```

### Integration Points

```yaml
README.MD (firmware repo, /home/dustin/projects/qmk_notifier/):
  - replace: Multi-OS "How to enable" step-2 ```c block (relative include -> bare include; drop duplicated shim).
  - append: one Features bullet (Community Module distribution, links to #setup).
  - preserve: ## Setup (Community Module flow, P1.M2.T1.S1); ## Companion Projects naming note.
INTERNAL LINKS:
  - (#setup) in the new Features bullet -> ## Setup heading. Verify the slug resolves.
  - existing (#multi-os-configuration), (#host-side-rules--typed-commands) unchanged.
SCOPE / NOT TOUCHED:
  - notifier.{c,h}, pattern_match.*, rules.mk, qmk_module.json (absent), test_*.c, run_*.sh,
    PRD.md, tasks.json, prd_snapshot.md, .gitignore.
DOWNSTREAM:
  - P1.M2.T2.S1 (parallel): runs host gates + R6; unaffected by this Markdown edit.
BUILD / CONFIG / WIRE / DATABASE:
  - none. Pure Markdown; no code/build/wire/protocol change (R6).
```

## Validation Loop

> Markdown-only sweep. Validation is grep-based: the stale relative include is gone, the
> Multi-OS snippet uses the bare include, the Features bullet is present, Setup/Companion
> Projects are unchanged, and the diff is confined to README.md. Run from the **firmware repo**
> (`/home/dustin/projects/qmk_notifier`). A README edit cannot affect the host gates (R6);
> gate re-verification is P1.M2.T2.S1's parallel scope.

### Level 1: Stale submodule references GONE (the primary gate)

```bash
cd /home/dustin/projects/qmk_notifier   # firmware repo, underscore (NOT the crate repo)

# 1a. NO relative include remains ANYWHERE in the README.
grep -n 'include "\./' README.md && echo "FAIL: a relative include remains" || echo "OK: no relative include anywhere"
grep -n './qmk_notifier/notifier.h' README.md && echo "FAIL: stale submodule include remains" || echo "OK: stale submodule include gone"

# 1b. The Multi-OS "How to enable" snippet now uses the bare include.
awk '/^### How to enable/{f=1} /^### Defining per-OS maps/{f=0} f' README.md | grep -q '#include "notifier.h"' \
  && echo "OK: Multi-OS snippet uses bare #include \"notifier.h\"" || echo "FAIL: Multi-OS snippet missing bare include"
# And it must NOT show the duplicated raw_hid_receive shim:
awk '/^### How to enable/{f=1} /^### Defining per-OS maps/{f=0} f' README.md | grep -q 'raw_hid_receive' \
  && echo "WARN: Multi-OS snippet still shows raw_hid_receive (de-duplication optional but recommended)" \
  || echo "OK: Multi-OS snippet de-duplicated (shim lives in Setup)"

# 1c. Whole-README sweep: no submodule-flow install path survives OUTSIDE ## Setup.
awk '/^## Setup/{s=1} /^## Usage/{s=0} !s' README.md | grep -nE 'include "\./|SRC \+= qmk|include keyboards|qmk_notifier/rules\.mk' \
  && echo "FAIL: a submodule-flow ref survives outside Setup" || echo "OK: no submodule-flow ref outside Setup"
```

### Level 2: Recommended Features bullet present + links intact

```bash
cd /home/dustin/projects/qmk_notifier

# 2a. The Community Module bullet is in the Features list.
awk '/^## Features/{f=1} /^## How It Works/{f=0} f' README.md | grep -qiE 'Community Module' \
  && echo "OK: Features bullet mentions Community Module" || echo "MISSING: Features Community Module bullet"
awk '/^## Features/{f=1} /^## How It Works/{f=0} f' README.md | grep -q '(#setup)' \
  && echo "OK: Features bullet links to #setup" || echo "MISSING: #setup link"

# 2b. The anchor target exists.
grep -q '^## Setup' README.md && echo "OK: ## Setup heading exists (slug #setup resolves)"

# 2c. Existing internal links still resolve.
grep -q '(#multi-os-configuration)' README.md && echo "OK: multi-os anchor intact"
grep -q '(#host-side-rules--typed-commands)' README.md && echo "OK: host-rules anchor intact"
```

### Level 3: Preserved sections (no accidental regression)

```bash
cd /home/dustin/projects/qmk_notifier

# 3a. ## Setup is STILL the Community Module flow (P1.M2.T1.S1's work is intact).
awk '/^## Setup/{f=1} /^## Usage/{f=0} f' README.md > /tmp/setup.md
grep -q 'modules/<org>/qmk_notifier' /tmp/setup.md && echo "OK: Setup still module flow (userspace clone)"
grep -q '#include "notifier.h"' /tmp/setup.md && echo "OK: Setup still bare include"
grep -qi 'hard build failure\|hard.*failure' /tmp/setup.md && echo "OK: Setup R4 leaf callout intact"
rm -f /tmp/setup.md

# 3b. Companion Projects naming note is UNCHANGED and still correct (item 3c).
grep -A2 'Naming note' README.md | grep -q 'qmk_notifier (underscore) is this firmware' \
  && echo "OK: naming note intact (firmware=underscore)"
grep -q 'github.com/dabstractor/qmk-notifier' README.md \
  && echo "OK: crate still linked as qmk-notifier (hyphen)"
```

### Level 4: Diff hygiene (only README.md, confined to the two edits)

```bash
cd /home/dustin/projects/qmk_notifier

# 4a. Only README.md changed in source (besides plan/ artifacts).
git status --porcelain | grep -vE '^\?\? plan/' | grep -E 'README\.md|\.c|\.h|\.mk|\.json|\.sh' \
  | grep -v 'README.md' && echo "FAIL: a source file other than README.md changed" || echo "OK: only README.md (source)"
git diff --stat -- README.md

# 4b. The diff touches ONLY the Multi-OS snippet region + the Features bullet region.
git diff -- README.md | grep -E '^@@'
# Expected: hunks in the ## Features area and the ## Multi-OS Configuration ### How to enable area.
#           NO hunk in ## Setup, ## Companion Projects, ## Running Tests, etc.

# 4c. Sanity: no code/build/test file changed; qmk_module.json was NOT created out of scope.
git diff --stat -- notifier.c notifier.h pattern_match.c pattern_match.h rules.mk run_all_tests.sh run_notifier_stub_tests.sh
# Expected: empty.
ls qmk_module.json 2>/dev/null && echo "WARN: qmk_module.json exists (was it created here? it should be M1.T1's job)" || echo "OK: qmk_module.json not created by this task"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `grep -n 'include "\./' README.md` → 0 hits; `grep -n './qmk_notifier/notifier.h'` → 0 hits;
      Multi-OS snippet uses bare `#include "notifier.h"`; no submodule-flow ref outside `## Setup`.
- [ ] Level 2: Features bullet mentions Community Module + links `(#setup)`; `## Setup` heading exists;
      existing `(#multi-os-configuration)` / `(#host-side-rules--typed-commands)` links intact.
- [ ] Level 3: `## Setup` still the Community Module flow (userspace clone, bare include, R4 callout);
      Companion Projects naming note unchanged (firmware=underscore, crate=hyphen).
- [ ] Level 4: only `README.md` changed; diff confined to the Multi-OS snippet + Features bullet;
      no code/build/test file changed; `qmk_module.json` not created here.

### Feature Validation

- [ ] No submodule-flow install instruction remains anywhere in the README (the relative include is gone).
- [ ] The Multi-OS Configuration snippet is consistent with the Community Module flow (bare include).
- [ ] The Community Module distribution option is surfaced in the Features list (item 3e).
- [ ] Companion Projects naming is correct and unchanged (item 3c).
- [ ] Items 3d (file inventory) and 3f (§17 line counts) correctly handled as N/A.

### Code Quality Validation

- [ ] Only Markdown changed; no code/build/wire/protocol change (R6).
- [ ] The Multi-OS snippet matches Setup's include style (consistency).
- [ ] No anti-patterns (see below): no relative include reintroduced, no Setup edit, no naming-note
      reword, no invented inventory, no crate-repo edit, no gate run.

### Documentation & Deployment

- [ ] This IS the final changeset-level documentation sync (Mode B, item §6).
- [ ] A reader sees ONE consistent install story (Community Module flow) across Setup, Features, and Multi-OS.
- [ ] No new env vars / config / build-system changes; no README file-size table added.

---

## Anti-Patterns to Avoid

- ❌ Don't edit the crate repo (`qmk-notifier`, hyphen). The target is the firmware repo
  (`/home/dustin/projects/qmk_notifier`, underscore) — its README has the Multi-OS snippet.
- ❌ Don't reintroduce a relative `#include "./qmk_notifier/notifier.h"` anywhere. The Community
  Module flow uses `#include "notifier.h"` (VPATH puts the module dir on `-I`, §18.2).
- ❌ Don't touch `## Setup` — P1.M2.T1.S1 rewrote it correctly. The grep hits inside Setup
  (`SRC +=`, `RAW_ENABLE`, `include .../rules.mk`, `git submodule add`, `qmk-notifier`) are all
  intentional and correct (negative phrasing / userspace clone / R4 leaf contract).
- ❌ Don't reword or swap the Companion Projects naming note (item 3c). It is already correct
  (`qmk_notifier` firmware / `qmk-notifier` crate). Swapping the slugs would introduce a bug.
- ❌ Don't invent a README file inventory or copy §17's file-size table (items 3d/3f). The README
  has no inventory; §17 is PRD-only; `qmk_module.json` is absent (M1.T1 gap). These are N/A.
- ❌ Don't create `qmk_module.json` — that is M1.T1's deliverable, out of scope here.
- ❌ Don't run or alter the test gates — that is P1.M2.T2.S1 (parallel). This task is README-only.
- ❌ Don't anchor edits on line numbers — the README shifts. Re-grep
  (`grep -n './qmk_notifier/notifier.h' README.md`) to locate the stale site.
- ❌ Don't drop the Multi-OS step-2 prose or the surrounding sections — only the ```c code block
  (and the appended Features bullet) change.

---

## Confidence Score: 10/10

The deliverable is a surgical README edit in the firmware repo (`/home/dustin/projects/qmk_notifier`).
A whole-README grep (run during research) proved the relative include
`#include "./qmk_notifier/notifier.h"` is the **only** stale submodule-flow reference outside the
`## Setup` section (P1.M2.T1.S1 already rewrote Setup correctly). The exact current text of that
Multi-OS "How to enable" step-2 code block and the exact Community Module-flow replacement (bare
`#include "notifier.h"`, no relative path, no duplicated `raw_hid_receive` shim) are given verbatim
in Implementation Tasks. The Companion Projects naming note was verified already correct
(`qmk_notifier` firmware / `qmk-notifier` crate, matching §18), so item 3c is verify-only; items 3d
(no README file inventory; §17 is PRD-only) and 3f are documented as N/A; `qmk_module.json` is absent
(M1.T1 gap) and is NOT this task's to create. The one recommended add (item 3e: a Features bullet
surfacing the Community Module distribution) is given verbatim with a precise anchor. The repo
disambiguation (harness cwd is the Rust crate repo; the task targets the firmware repo) is documented
prominently and resolved by explicit absolute paths. The edit is Markdown-only (R6: no behavioral
change; the parallel P1.M2.T2.S1 owns gate re-verification). Validation is grep-based and fully
specified. No external dependencies; no code/build/wire change.