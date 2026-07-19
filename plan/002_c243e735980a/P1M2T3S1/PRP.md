name: "PRP — P1.M2.T3.S1: Update README.md with the multi-OS feature section + backward-compat guarantee"
description: |

  Mode B (cross-cutting changeset documentation). The multi-OS feature
  (P1.M1.T1.S1 → P1.M2.T2.S1) is fully implemented and verified green. This task
  sweeps README.md so it documents the feature end-to-end: what it does, how to
  opt in, the dispatch rule, the push-only design, the backward-compat guarantee,
  the updated test gates, and a "what this does NOT change" note. Documentation
  ONLY — no code changes. The single file touched is `README.md`.

---

## Goal

**Feature Goal**: Update `README.md` (repo root) so it documents the multi-OS
opt-in overlay (PRD §2 F8 / F9, §10.1, §10.3) end-to-end and reflects the new
dual-binary notifier stub test gate. A reader who knows nothing about the
multi-OS feature must, after reading the README, understand (a) what multi-OS
does, (b) how to opt in, (c) that a default-only keymap is byte-identical to
before (backward-compat guarantee), and (d) what the test gates now cover.

**Deliverable**: ONE file modified — `README.md`. Four edits:
1. A new bullet in the **Features** list announcing optional per-OS maps.
2. An optional **multi-OS note** added to the **Setup** section (step 3 rules.mk
   + step 2 keymap wiring), clearly marked optional/multi-OS-only, with a forward
   reference to the new section.
3. A new **`## Multi-OS Configuration`** section inserted between the
   **Pattern Matching Syntax** subsection and **Companion Projects**, containing:
   the opt-in nature + backward-compat guarantee, how to enable (rules.mk +
   `process_detected_host_os_kb` wiring snippet from PRD §10.1 step 3), the
   `DEFINE_SERIAL_*_OS` macros with the PRD §10.3 macOS+Linux reference excerpt,
   the per-track OS-first/default-fallback dispatch rule, the push-only design,
   and a "What this does NOT change" note (item 3d: wire protocol unchanged,
   matcher untouched, host-provided OS/host-rules are planned future).
4. An update to the **Running Tests** section: the stub-gate paragraph now says
   `run_notifier_stub_tests.sh` stub-compiles `notifier.c` once and links+runs
   BOTH `test_notifier_dispatch` (11 cases) AND `test_notifier_os` (31 cases, the
   multi-OS suite); the "Current Test Status" blurb uses the LIVE counts
   (pattern_match 2019, dispatch 11, os 31 — all 0 failures).

**Success Definition**:
- README.md contains a `## Multi-OS Configuration` section (after Usage, before
  Companion Projects) documenting all of: opt-in nature, enable steps, the two
  code snippets (wiring + `DEFINE_*_OS`), the OS-first/default-fallback rule, the
  push-only design, the backward-compat guarantee, and the "does NOT change" note.
- Features list has a multi-OS bullet.
- Setup section marks multi-OS as optional and references the new section.
- Running Tests reflects BOTH notifier stub binaries with the live case counts.
- `git status` shows ONLY `README.md` modified (plus plan/ PRP/research).
- No code, build, or test files touched.

## User Persona (if applicable)

**Target User**: Two readers — (1) an end user wiring multi-OS into their keymap
(needs the enable steps + `DEFINE_*_OS` macro example + the dispatch rule
explained); (2) a prospective contributor assessing whether the feature is safe
to adopt (needs the backward-compat guarantee + what does/does not change).

**Use Case**: A user whose keyboard travels between macOS and Linux wants the
*same conceptual app* (a terminal, a browser) to trigger the *same* layer/command
even though each OS reports a different `application_class`. They read the README,
see Multi-OS is opt-in, add `OS_DETECTION_ENABLE = yes`, wire
`process_detected_host_os_kb → notifier_set_os`, and define per-OS maps — without
breaking their existing default rules.

**User Journey**: skim Features → see "Optional per-OS maps" → read Multi-OS
Configuration → copy the enable steps + `DEFINE_*_OS` excerpt → (single-OS users
skip it entirely, confirmed by the "byte-identical" guarantee) → run the test
gate to confirm.

**Pain Points Addressed**: The multi-OS feature is shipped but invisible in the
README; the backward-compat guarantee (the prime directive, invariant 19) is
nowhere stated; the stub test gate grew a second binary but the README still
names only `test_notifier_dispatch`.

## Why

- **This IS the changeset-level documentation task (Mode B).** It depends on
  every implementing subtask (P1.M1.T1.S1 through P1.M2.T2.S1) and summarizes the
  whole multi-OS delta. Per-file/in-file docs (Mode A) were already handled by
  those subtasks; this task sweeps the README/overview.
- **The backward-compat guarantee is the selling point and must be stated.**
  A default-only keymap is byte-for-byte equivalent to the pre-multi-OS firmware
  (invariant 19). Without this statement in the README, adopters fear breaking
  their existing setup.
- **The test gate changed.** `run_notifier_stub_tests.sh` now builds+runs TWO
  binaries (`test_notifier_dispatch` 11 + `test_notifier_os` 31). The README's
  stub paragraph and the "2019/2019" status blurb are stale.
- **The push-only design and the "not implemented yet" host-OS note** prevent a
  common misconception (users may expect the module to call `detected_host_os()`
  itself, or expect a host-provided OS today). Documenting these avoids issues.

## What

Four edits to `README.md`, described precisely below. Each is keyed to the
current README line numbers (verified: `grep -n '^#' README.md`).

### Success Criteria

- [ ] `## Multi-OS Configuration` section exists between the Pattern Matching
      Syntax subsection (ends ~line 125) and `## Companion Projects` (line 127).
- [ ] That section contains ALL of: the "opt-in overlay" framing; the
      backward-compat guarantee (zero `DEFINE_*_OS` ⇒ byte-identical); the two
      code snippets (wiring + `DEFINE_*_OS` excerpt); the per-track
      OS-first/default-fallback dispatch rule; the push-only design statement;
      the "What this does NOT change" note (wire protocol, matcher, host-OS
      planned).
- [ ] Features list has a multi-OS bullet ("Optional per-OS maps" or equivalent).
- [ ] Setup section 3 (rules.mk) carries an optional `OS_DETECTION_ENABLE = yes`
      block, marked multi-OS-only, with a forward reference to Multi-OS
      Configuration; step 2 (keymap.c) notes the optional
      `process_detected_host_os_kb` wiring.
- [ ] Running Tests stub-gate paragraph names BOTH `test_notifier_dispatch`
      (11 cases) AND `test_notifier_os` (31 cases).
- [ ] "Current Test Status" blurb uses LIVE counts: pattern_match 2019, plus the
      notifier stub gate (dispatch 11 + os 31), all 0 failures.
- [ ] `git status --porcelain` shows only ` M README.md` and `?? plan/...`.
- [ ] No code/build/test files changed; no new dependencies.

## All Needed Context

### Context Completeness Check

**Pass.** The current README structure (every heading + line number), the exact
stub paragraph and status blurb to replace (captured verbatim), the live test
counts (run during research: 2019 / 11 / 31, all green), the LANDED API surface
(`notifier_set_os`, `DEFINE_SERIAL_*_OS`, `os_variant_t`), the dispatch rule
(F8.4/F8.5), and the canonical code snippets (PRD §10.1 step 3 + §10.3) are all
captured in `plan/002_c243e735980a/P1M2T3S1/research/notes.md` and below. The
exact markdown for each new/changed README section is given verbatim in
"Implementation Tasks". An implementer with only this PRP + repo access can
produce the updated README with no further research.

### Documentation & References

```yaml
# MUST READ — the canonical integration steps + the wiring snippet to quote
- file: PRD.md   (also plan/002_c243e735980a/prd_snapshot.md)
  section: "### 10.1 Integration (the end user's steps)"  (esp. step 2 + step 3)
  why: "Step 3 adds `OS_DETECTION_ENABLE = yes` (multi-OS only) to rules.mk;
        step 3's keymap.c block shows the `process_detected_host_os_kb` wiring
        that calls `notifier_set_os(os)` — the ONE required push call. Quote
        these snippets verbatim in the Setup note + the Multi-OS Configuration
        section."
  critical: "The wiring block also keeps the everyone-does-this `raw_hid_receive
        → hid_notify` wiring. Keep both in the snippet."

# MUST READ — the canonical multi-OS reference config (the DEFINE_*_OS example)
- file: PRD.md
  section: "### 10.3 Multi-OS reference configuration (opt-in)"
  why: "The reference excerpt with default maps + OS_MACOS command/layer maps +
        OS_LINUX layer maps. This is the example to put in the README. It also
        contains two worked examples (Google Chrome\x1DClaude; blender\x1D) that
        crisply illustrate the OS-first/default-fallback + independent-tracks
        rule — adapt at least the blender one into the README prose."
  critical: "Use the §10.3 excerpt verbatim (macOS + Linux). Do NOT rewrite the
        README's existing Usage examples (they use enable_vim_mode style); only
        ADD the new section. The §10.3 example uses disable_vim/vim_lazy_insert —
        that is the canonical reference style; keep it."

# MUST READ — the feature semantics (F8 dispatch rule + F9 state clearing)
- file: PRD.md
  section: "### F8 — Multi-OS map selection (opt-in overlay)" + "### F9 — OS-change state clearing"
  why: "The exact dispatch rule to paraphrase in the README: per map type, scan
        the OS map FIRST; a match wins and the default is NOT consulted; no OS
        map / no match => default fallback; the two tracks decide independently;
        OS_UNSURE => default only. And notifier_set_os clears state on change
        (F9.1), is idempotent (F9.3), and never re-dispatches (F9.2)."
  critical: "F8.4 is THE core rule: 'A match there wins and the default map for
        that type is not consulted.' State this plainly. F8.5: tracks are
        independent (one window can take a layer from the OS map and a command
        from the default map)."

# MUST READ — the backward-compat prime directive + push-only design
- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve"  (invariants 17, 19, 20)
  why: "Invariant 19 (a default-only keymap is byte-identical to pre-multi-OS:
        zero DEFINE_*_OS => all per-OS accessors return {NULL,0} => defaults
        used) is the guarantee to state. Invariant 17 (the module never calls
        detected_host_os(); OS is push-only via notifier_set_os) is the design
        point to state."
  critical: "Phrase the guarantee in user terms: 'if you define only the default
        DEFINE_SERIAL_* maps, nothing changes — multi-OS is strictly opt-in.' Do
        NOT expose internal symbol mangling to end users."

# MUST READ — the planned-future framing (the 'does NOT change' note)
- file: PRD.md
  section: "### 4.7 OS source: firmware-side today; host-provided OS RESERVED (next cycle)"
        + "### 14.1 Host-provided OS & full host-rule replacement (HELD — next cycle, NOT now)"
  why: "Source for the 'host-provided OS is PLANNED, not implemented' note. Today
        the OS comes only from QMK's firmware-side OS_DETECTION, pushed in by the
        keymap. A future cycle may add a host-declared OS (0xF0 typed command)
        and full host-rule replacement; both are OUT OF SCOPE now."
  critical: "State clearly that the wire protocol is UNCHANGED (no OS byte sent)
        and the pattern matcher is untouched. This reassures users the feature
        is additive and reversible."

# MUST READ — the current README (the file being edited)
- file: README.md
  why: "The file under edit. Current structure: Features (line 5), How It Works +
        Wire format (13-34), Setup steps 1/2/3 (36-67), Usage with command/layer
        mapping + Pattern Matching Syntax table (69-125), Companion Projects
        (127), Compatibility (148), Documentation (152), Performance (156),
        Running Tests (Quick Test / Comprehensive Test Suite + table / Current
        Test Status) (160-223), Contributing (225). The Multi-OS Configuration
        section inserts BEFORE Companion Projects (line 127)."
  pattern: "Follow the README's existing voice and formatting: `## `/`### `
        headings, triple-backtick ``` fences (bash/c blocks), tables with pipes,
        bold for emphasis, blank lines around blocks. The per-suite test table
        (lines ~182-193) is already accurate — DO NOT change its numbers."
  critical: "The 9-suite pattern_match table is ALREADY correct (376/179/74/189/
        smoke/10/161/32/1008 = 2019). Leave it. Only the stub paragraph + the
        'Current Test Status' blurb need updating."

# The API surface (LANDED — quote the names exactly)
- file: notifier.h
  why: "Confirms the exact public names to use in the README: `notifier_set_os`,
        `DEFINE_SERIAL_COMMANDS_OS`, `DEFINE_SERIAL_LAYERS_OS`, `os_variant_t`,
        the `OS_DETECTION_ENABLE` is a QMK rules.mk var (not in this header).
        The header includes os_detection.h header-only (push-only design proof)."
  critical: "Do NOT invent API names. The four macros are DEFINE_SERIAL_COMMANDS,
        DEFINE_SERIAL_LAYERS, DEFINE_SERIAL_COMMANDS_OS, DEFINE_SERIAL_LAYERS_OS.
        The single OS entry point is notifier_set_os(os_variant_t os)."

# Live test counts (VERIFIED by running the gates during research)
- file: run_notifier_stub_tests.sh  (now 4-step, dual-binary — P1.M2.T2.S1 LANDED)
  why: "Source of the LIVE counts: it builds notifier.c once, links BOTH
        test_notifier_dispatch and test_notifier_os, runs both, prints
        'notifier dispatch fails=0' and 'notifier os fails=0'. dispatch=11 cases,
        os=31 cases. run_all_tests.sh prints 'Total tests run across all suites:
        2019'."
  critical: "Use these EXACT live numbers: pattern_match corpus 2019, dispatch
        11, os 31. The README's old '2019/2019' blurb covered only pattern_match;
        it must now also mention the notifier stub gate. Re-run the gates to
        confirm before finalizing — the counts are printed live (item 3c)."

# The upstream P1.M2.T2.S1 PRP (the runner this task's test blurb describes)
- file: plan/002_c243e735980a/P1M2T2S1/PRP.md
  why: "Defines the runner contract this task documents: stub-compile notifier.c
        ONCE, link BOTH binaries, run both, print both fail counts, PASSED iff
        both 0 fails. The README description must match this behavior."
  critical: "The runner is ALREADY implemented (verified: it is the 4-step version
        and passes). This task only DOCUMENTS it in the README. Do not modify the
        runner."

# The upstream P1.M2.T1.S1 PRP (the os test this task's blurb names)
- file: plan/002_c243e735980a/P1M2T1S1/PRP.md
  why: "Defines test_notifier_os.c (31 cases) covering the six §11.2D categories
        (F8 merge/fallback per track, per-map-type independence, OS_UNSURE→default,
        F9 clear-on-change idempotence). The README's one-line description of
        this suite should mirror this scope."
  critical: "test_notifier_os is LANDED (31 cases, 0 FAIL). This task only names
        it in the README. Do not modify the test."
```

### Current Codebase tree (relevant slice)

```bash
README.md                  # ← MODIFY (this task). Multi-OS is entirely absent.
notifier.h                 # LANDED: notifier_set_os decl + DEFINE_*_OS macros. DO NOT TOUCH.
notifier.c                 # LANDED: process_full_message (OS-first/fallback) + notifier_set_os. DO NOT TOUCH.
pattern_match.{c,h}        # UNAFFECTED (P1 matcher, complete). DO NOT TOUCH.
rules.mk                   # UNAFFECTED (OS_DETECTION_ENABLE lives in the USER's keymap rules.mk, not here). DO NOT TOUCH.
test_notifier_dispatch.c   # LANDED (11 cases). DO NOT TOUCH.
test_notifier_os.c         # LANDED (31 cases, multi-OS F8/F9). DO NOT TOUCH.
run_notifier_stub_tests.sh # LANDED (4-step, dual-binary). DO NOT TOUCH.
run_all_tests.sh           # LANDED (9 suites, 2019 total). DO NOT TOUCH.
PRD.md                     # READ-ONLY.
```

### Desired Codebase tree with files to be changed

```bash
README.md                  # MODIFIED: +1 Features bullet; +optional multi-OS note in Setup;
                           #   +new ## Multi-OS Configuration section; updated Running Tests
                           #   (stub paragraph names BOTH binaries; status blurb uses live counts).
# (nothing else changes)
```

### Known Gotchas of our codebase & Library Quirks

```markdown
<!-- CRITICAL: This is DOCUMENTATION ONLY. Do not touch any .c/.h/.sh file, rules.mk,
     PRD.md, tasks.json, prd_snapshot.md, or .gitignore. The ONLY source file
     changed is README.md. (The multi-OS code, the test, and the runner are all
     ALREADY LANDED by prior subtasks; this task just sweeps the README.) -->

<!-- CRITICAL: Use the LIVE test counts. The numbers are printed by the runners
     themselves — RE-RUN them and use what they print (item 3c). At research time
     they were: run_all_tests.sh "Total tests run across all suites: 2019";
     run_notifier_stub_tests.sh "notifier dispatch fails=0" (11 cases) and
     "notifier os fails=0" (31 cases). If the counts differ at implementation
     time, use the live values. Do NOT hard-code a stale "2019/2019" that omits
     the notifier stub gate. -->

<!-- GOTCHA: the 9-suite pattern_match table in the README is ALREADY accurate
     (376+179+74+189+smoke+10+161+32+1008 = 2019). Do NOT edit those numbers.
     Only (a) the stub paragraph below it (which still says only
     test_notifier_dispatch.c) and (b) the "Current Test Status" blurb need changes. -->

<!-- GOTCHA: `OS_DETECTION_ENABLE = yes` goes in the USER's keymap `rules.mk`,
     NOT in this module's rules.mk. This module's rules.mk just does
     `RAW_ENABLE = yes` + `SRC += ...`. The README must be clear the flag is
     added by the user (single-OS users skip it entirely). -->

<!-- GOTCHA: README example styles differ. The existing Usage examples use
     enable_vim_mode/disable_vim_mode/set_rotary_encoder_figma. The PRD §10.3
     reference (what to quote in the NEW section) uses disable_vim/vim_lazy_insert.
     Do NOT rewrite the existing Usage examples; ADD the new section using the
     §10.3 excerpt verbatim. Mixing styles across sections is fine and expected
     (the §10.3 excerpt is the canonical maintainer reference). -->

<!-- GOTCHA: phrase the backward-compat guarantee for END USERS, not developers.
     Say "a keymap that defines only the default DEFINE_SERIAL_* maps behaves
     exactly as before" — NOT "all 16 per-OS weak accessors return {NULL,0}".
     The internal symbol-mangling detail belongs in the PRD, not the README. -->

<!-- GOTCHA: keep the "What this does NOT change" note crisp and honest.
     - Wire protocol UNCHANGED (no OS byte is sent; still class\x1Dtitle over
       32-byte Raw HID reports with the 0x81 0x9F magic header + ETX).
     - Pattern matcher UNTOUCHED.
     - Host-provided OS and host-rules are PLANNED (§4.7/§14.1), not implemented.
     Do NOT promise host-OS support that doesn't exist yet. -->

<!-- GOTCHA: maintain markdown hygiene — blank line before/after every heading,
     code fence, and table; balanced ``` fences; the Pattern Matching Syntax
     table pipes aligned. The Multi-OS Configuration section uses the same
     heading depth (## for the section, ### for any subsection) as the rest of
     the README. -->
```

## Implementation Blueprint

### Data models and structure

None. This task edits markdown prose.

### Implementation Tasks (ordered by dependencies)

The four edits below are independent and may be applied in any order, but are
listed top-to-bottom of the README. Each gives the EXACT markdown to insert or
the EXACT text to replace, so the implementer can apply them with the `edit`
tool. **Line numbers refer to the CURRENT README** (verified via
`grep -n '^#' README.md`); they will shift as edits are applied, so match on the
given unique text, not line numbers.

```yaml
Task 1: ADD a multi-OS bullet to the Features list (README ## Features, ~line 5-11)
  - FIND: the Features bullet list (5 bullets ending with "Handles strings longer
    than the 32-byte HID packet limit").
  - ADD as the LAST bullet of that list (verbatim):
      - **Optional per-OS maps** — define OS-specific rules (macOS, Windows,
        Linux) that take precedence over the default map, so an app reported with
        different `application_class` strings on different OSes just works.
        Strictly opt-in: a keymap that defines only the default maps behaves
        exactly as before. See [Multi-OS Configuration](#multi-os-configuration).
  - PRESERVE: the existing 5 bullets and their order.
  - WHY: announces the feature where readers skim first; links to the detail.

Task 2: ADD an optional multi-OS note to the Setup section (README ## Setup)
  - This is TWO small additions, both clearly marked OPTIONAL / multi-OS-only:
  (a) In step 3 "### 3. Update your rules.mk" (the `include .../qmk-notifier/rules.mk`
      code block, ~line 63-67), ADD an optional sub-block AFTER the include line:
        ```make
        # Optional — Multi-OS support only (single-OS users skip this).
        # Enables QMK's OS detection so the keyboard can select per-OS maps.
        OS_DETECTION_ENABLE = yes
        ```
      Label it clearly as optional.
  (b) In step 2 "### 2. Include the module in your keymap" (~line 45-59), ADD a
      short note after the raw_hid_receive block: "Multi-OS users also override
      `process_detected_host_os_kb` to push the detected OS into the module —
      see [Multi-OS Configuration](#multi-os-configuration) for the one-line
      wiring." (Keep step 2's existing raw_hid_receive block unchanged.)
  - WHY: the Setup steps are the integration checklist; multi-OS users need to
    know the two extra (optional) things to add without it cluttering the
    single-OS path.
  - PRESERVE: the existing step 1/2/3 content; this is additive and clearly
    marked optional.

Task 3: CREATE a new "## Multi-OS Configuration" section (INSERT between the end
        of the Pattern Matching Syntax subsection, ~line 125, and
        "## Companion Projects", line 127)
  - This is the LARGEST edit. Insert the FULL markdown block given verbatim in
    "Multi-OS Configuration section (verbatim markdown)" below.
  - It contains: (1) opt-in framing + backward-compat guarantee; (2) "How to
    enable" with the PRD §10.1 step-3 wiring snippet; (3) the DEFINE_*_OS macros
    with the PRD §10.3 macOS+Linux reference excerpt + a worked dispatch example;
    (4) the per-track OS-first/default-fallback dispatch rule; (5) the push-only
    design note; (6) a "What this does NOT change" note (item 3d).
  - PLACEMENT: immediately BEFORE "## Companion Projects". After insertion, the
    order is: ... Pattern Matching Syntax table → (Multi-OS Configuration) →
    Companion Projects → ...
  - WHY: this is the heart of the documentation task; all the item-spec 3a
    requirements land here.

Task 4: UPDATE the Running Tests section (README ## Running Tests)
  (a) REPLACE the stub-gate paragraph (the one starting "This stub-compiles
      `notifier.c` against the minimal `qmk_stubs/` and runs
      `test_notifier_dispatch.c` (11 cases ...)") with a paragraph stating it
      stub-compiles `notifier.c` ONCE and links+runs BOTH `test_notifier_dispatch`
      (11 cases) AND `test_notifier_os` (31 cases — the multi-OS F8/F9 suite).
      Use the exact replacement text in "Running Tests replacement (verbatim)".
  (b) UPDATE the "Current Test Status" blurb line
      "**Overall Test Results**: 2019/2019 tests passing (100% success rate, 0 failures)"
      to reflect the live breakdown: pattern_match corpus 2019, plus the notifier
      stub gate (dispatch 11 + os 31), all 0 failures. Use the exact replacement
      text in "Running Tests replacement (verbatim)". RE-RUN the two gates first
      and use whatever they print (item 3c).
  - PRESERVE: the 9-suite pattern_match table (it is already accurate); the rest
    of the Running Tests prose (Quick Test, Comprehensive Test Suite intro, the
    performance/pathological-NFA note).

Task 5: VERIFY (run the Validation Loop) — markdown sanity + presence checks +
        git diff hygiene. No code/tests to run, but re-run the two test gates to
        capture live counts and confirm nothing else changed.
```

### Multi-OS Configuration section (verbatim markdown)

Insert this block (between the Pattern Matching Syntax subsection and
`## Companion Projects`). It is self-contained and follows the README's existing
voice/formatting.

````markdown
## Multi-OS Configuration

Multi-OS support is an **opt-in overlay**: a keymap that defines only the default
`DEFINE_SERIAL_*` maps behaves **exactly as before** — multi-OS adds per-OS maps
*on top of* the defaults, never instead of them. You opt in by (1) enabling QMK's
OS detection, (2) pushing the detected OS into the module, and (3) defining
per-OS maps. Single-OS users skip all three and observe zero change.

### How to enable

1. In your keymap's `rules.mk`, enable QMK's OS-detection feature:

   ```make
   OS_DETECTION_ENABLE = yes
   ```

2. In `keymap.c`, push the detected OS into the module by overriding
   `process_detected_host_os_kb`. This is the **one** required call:

   ```c
   #include QMK_KEYBOARD_H
   #include "./qmk-notifier/notifier.h"

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
   ```

### Defining per-OS maps

Use `DEFINE_SERIAL_COMMANDS_OS(os, { ... })` and
`DEFINE_SERIAL_LAYERS_OS(os, { ... })` to define maps that apply only when the
detected OS matches. `os` is an `os_variant_t` enumerator token — `OS_LINUX`,
`OS_WINDOWS`, `OS_MACOS`, or `OS_IOS` (not a string, and not `OS_UNSURE`, which
has no per-OS map by design). Any subset of OSes × {commands, layers} may be
defined; the rest fall back to the defaults.

Rules shared across all OSes stay in the **default** `DEFINE_SERIAL_*` maps:

```c
/* Default maps: OS-AGNOSTIC rules live here (gaming, calculator, …). */
DEFINE_SERIAL_COMMANDS({
    { WT("steam_app*", "*"), &disable_vim },
    { WT("cs2", "Counter-Strike 2"), &disable_vim },
});
DEFINE_SERIAL_LAYERS({
    { "*calculator", _NUMPAD },
    { "blender", _BLENDER },
    { "steam_app*", _GAMING },
});

/* macOS-specific: same conceptual apps report different class strings
 * (Terminal/iTerm, "Google Chrome"). Scanned FIRST when the OS is macOS;
 * a match here prevents the default map for that track from running. */
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { "iTerm", &disable_vim },
    { "Terminal", &disable_vim },
    { WT("Google Chrome", "*claude*"), &vim_lazy_insert, &disable_vim },
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "iTerm", _TERMINAL },
    { "Terminal", _TERMINAL },
    { WT("Google Chrome", "*"), _BROWSER },
});

/* Linux-specific (Hyprland/X11 class names). */
DEFINE_SERIAL_LAYERS_OS(OS_LINUX, {
    { "*alacritty*", _TERMINAL },
    { "*kitty*", _TERMINAL },
    { "firefox", _BROWSER },
});
```

### How matching works (per-OS, then default)

For **each** map type (commands and layers), independently:

1. The **OS-specific map** for the currently-detected OS is scanned **first**
   (first-match-wins, in definition order).
2. If it matches, that match wins and the **default map for that track is not
   consulted**.
3. If there is no OS-specific map for the current OS, or one exists but matches
   nothing, the **default** map is scanned (first-match-wins).

The two tracks decide **independently** — a single window may take its layer from
the OS map and its command from the default map, or vice versa. Until the OS is
detected (or on a board that does not enable `OS_DETECTION_ENABLE`), the module
behaves as `OS_UNSURE` and uses the **default maps only** — identical to the
pre-multi-OS firmware.

When the detected OS **changes**, the module clears any active layer/command
chosen under the previous OS before recording the new one (so nothing from the
old OS's maps persists). Repeated detection of the *same* OS is a no-op (no
flapping). The next focus-change message from the host re-establishes state under
the new maps.

### Backward compatibility (the guarantee)

If you define **only** the default `DEFINE_SERIAL_*` maps — no `DEFINE_*_OS`
macros, and no `notifier_set_os` call — the module behaves **byte-for-byte
identically** to the pre-multi-OS firmware. Multi-OS is strictly additive: the
per-OS maps simply do not exist, so every dispatch uses the defaults exactly as
before. You cannot break an existing keymap by *not* opting in.

### Push-only by design

The module **never** calls QMK's `detected_host_os()` itself. The OS is *pushed*
in by your keymap via `notifier_set_os(os)` (conventionally from
`process_detected_host_os_kb`). This means the module carries no link dependency
on the OS-detection subsystem — it only consumes the `os_variant_t` *type*.

### What this does NOT change

- **The wire protocol is unchanged.** No OS byte is sent; the companion app still
  sends `class\x1Dtitle` as 32-byte Raw HID reports with the `0x81 0x9F` magic
  header and `ETX` terminator.
- **The pattern matcher is untouched.** Multi-OS only selects *which* map is
  scanned; the matching engine (`pattern_match`) is identical.
- **Host-provided OS and host-side rules are planned future work**, not
  implemented. Today the OS comes only from QMK's firmware-side `OS_DETECTION`,
  pushed in by the keymap. A future version may let the host declare its OS
  (authoritatively) and/or ship rules from the desktop — both are out of scope
  here.
````

### Running Tests replacement (verbatim)

**(a) Replace** the current stub-gate paragraph:

> This stub-compiles `notifier.c` against the minimal `qmk_stubs/` and runs
> `test_notifier_dispatch.c` (11 cases covering F4 delimiter matching, dispatcher
> ordering, `hid_notify` reassembly, sanitization, acknowledgement, and NULL
> safety).

**with**:

```markdown
This stub-compiles `notifier.c` once against the minimal `qmk_stubs/`, links it
into **two** host test binaries, and runs both:

- **`test_notifier_dispatch`** (11 cases) — F4 delimiter matching, dispatcher
  ordering, `hid_notify` reassembly, sanitization, acknowledgement, and NULL
  safety (the backward-compat canary).
- **`test_notifier_os`** (31 cases) — the multi-OS map-selection (F8) and
  OS-change-clearing (F9) contract: OS-specific maps win over defaults per track,
  default fallback when an OS map is absent/matches nothing/`OS_UNSURE`,
  independent command vs layer tracks, and `notifier_set_os` idempotence +
  clear-on-change.
```

**(b) Replace** the status blurb line:

> **Overall Test Results**: 2019/2019 tests passing (100% success rate, 0 failures)

**with** (re-run the gates first; the counts are printed live):

```markdown
**Overall Test Results**: all suites green with 0 failures:
- Pattern-match corpus (`./run_all_tests.sh`, 9 suites): **2019/2019** tests passing.
- Notifier stub gate (`./run_notifier_stub_tests.sh`): `test_notifier_dispatch`
  **11/11** + `test_notifier_os` **31/31** cases passing.
```

> **Note:** the exact assertion counts are printed live by the runners — re-run
> `./run_all_tests.sh` and `./run_notifier_stub_tests.sh` and use whatever they
> report. The numbers above are the live values at the time of writing.

### Implementation Patterns & Key Details

```markdown
<!-- PATTERN: match on UNIQUE text, not line numbers. The README will shift as
     edits land; use the `edit` tool with the exact oldText strings given above
     (e.g. the whole "This stub-compiles ..." paragraph; the exact blurb line).
     For insertions (Features bullet, Setup note, the new section), anchor on a
     unique surrounding line and insert before/after it. -->

<!-- PATTERN: the new ## Multi-OS Configuration section uses the README's existing
     heading depth (## for the section, ### for "How to enable" / "Defining
     per-OS maps" / "How matching works" / "Backward compatibility" /
     "Push-only by design" / "What this does NOT change"). Blank line before and
     after every heading, fence, and table. -->

<!-- PATTERN: code fences are language-tagged (```make, ```c, ```bash) to match
     the README's existing style. The multi-OS reference excerpt uses ```c. -->

<!-- PATTERN: anchor links ([Multi-OS Configuration](#multi-os-configuration))
     rely on GitHub's heading-slug rule (lowercase, spaces→hyphens, punctuation
     stripped). "## Multi-OS Configuration" → #multi-os-configuration. Verify by
     eye; both Features bullet and Setup note link to it. -->

<!-- CRITICAL: re-run BOTH test gates immediately before finalizing the status
     blurb so the counts are current. ./run_all_tests.sh prints "Total tests run
     across all suites: N"; ./run_notifier_stub_tests.sh prints "notifier dispatch
     fails=N" and "notifier os fails=N". The case counts (11 / 31) come from each
     binary's "Total tests run: N" summary line. Use the live numbers. -->

<!-- ANTI-PATTERN: do NOT edit the 9-suite pattern_match table — it is already
     accurate (376+179+74+189+smoke+10+161+32+1008 = 2019). -->

<!-- ANTI-PATTERN: do NOT rewrite the existing Usage examples (enable_vim_mode
     style). Only ADD the new Multi-OS Configuration section using the PRD §10.3
     excerpt (disable_vim style). -->

<!-- ANTI-PATTERN: do NOT expose internal implementation (weak symbols, ##os
     token-paste, {NULL,0} accessors) to end users in the README. State the
     backward-compat guarantee in user terms ("behaves exactly as before"). -->

<!-- ANTI-PATTERN: do NOT promise host-provided-OS support. It is PLANNED, not
     implemented. The "does NOT change" note must say so plainly. -->
```

### Integration Points

```yaml
FILES MODIFIED:
  - README.md (repo root). The ONLY file this task touches.

CONTENT INSERTED:
  - Features: +1 bullet (optional per-OS maps, with anchor link).
  - Setup step 2: +1 optional multi-OS note (forward link).
  - Setup step 3: +1 optional `OS_DETECTION_ENABLE = yes` block.
  - NEW `## Multi-OS Configuration` section (6 subsections) before Companion Projects.
  - Running Tests: stub paragraph rewritten (dual-binary); status blurb uses live counts.

CONTENT PRESERVED (DO NOT TOUCH):
  - All 5 existing Features bullets (the new one is appended).
  - The 9-suite pattern_match test table (already accurate).
  - The existing Usage command/layer examples + Pattern Matching Syntax table.
  - Companion Projects, Compatibility, Documentation, Performance, Contributing sections.
  - The pathological-NFA / "production-ready" note in Current Test Status.

DEPENDENCIES (LANDED, unchanged by this task — documented, not modified):
  - notifier.h: notifier_set_os decl + DEFINE_SERIAL_*_OS macros   [P1.M1.T1.S1 — COMPLETE]
  - notifier.c: process_full_message (OS-first/fallback) + notifier_set_os  [P1.M1.T3.S2 — COMPLETE]
  - test_notifier_os.c (31 cases)                                    [P1.M2.T1.S1 — COMPLETE]
  - run_notifier_stub_tests.sh (4-step, dual-binary)                 [P1.M2.T2.S1 — COMPLETE]
  - run_all_tests.sh (9 suites, 2019)                                [unchanged]

BUILD / CONFIG / DATABASE / ROUTES:
  - N/A (markdown documentation; the OS_DETECTION_ENABLE flag lives in the USER's
    keymap rules.mk, not this module's rules.mk).
```

## Validation Loop

> This is a documentation task — there is no compiler for markdown. Validation is
> (1) re-running the two test gates to capture live counts, (2) grepping the
> updated README for every required element, (3) markdown sanity checks, and
> (4) `git diff` hygiene. All commands were designed to run from the repo root.

### Level 1: Live counts (re-run the gates; the status blurb must match)

```bash
cd /home/dustin/projects/qmk-notifier

# Pattern-match corpus count (for the status blurb).
./run_all_tests.sh 2>/dev/null | grep 'Total tests run across all suites'
# Expected: "Total tests run across all suites: 2019"  (use whatever it prints)

# Notifier stub gate — BOTH binaries + their case counts.
./run_notifier_stub_tests.sh 2>/dev/null | grep -E 'fails=|PASSED|FAILED'
# Expected: "notifier dispatch fails=0  (exit=0)", "notifier os fails=0  (exit=0)",
#           "✓ notifier stub-compile gate PASSED".

# Per-binary case counts (the "Total tests run: N" summary lines).
gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -std=c99 -o /tmp/tdis
/tmp/tdis 2>/dev/null | grep 'Total tests run'   # expect: 11
gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99 -o /tmp/tnos
/tmp/tnos 2>/dev/null | grep 'Total tests run'   # expect: 31
rm -f /tmp/tdis /tmp/tnos
# Use these live numbers (2019 / 11 / 31) in the status blurb. If they differ,
# use the live values.
```

### Level 2: Presence checks (every required element is in the README)

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. The new section exists, between Usage and Companion Projects.
grep -n '^## Multi-OS Configuration' README.md
# Expected: one line. Its line number must be AFTER the Pattern Matching Syntax
# table and BEFORE "## Companion Projects".
awk '/^## Multi-OS Configuration/{m=NR} /^## Companion Projects/{if(m&&NR>m){print "order OK: Multi-OS before Companion"; m=0}}' README.md

# 2b. All item-spec 3a/3d content is present (each phrase should match once).
for phrase in \
  'opt-in overlay' \
  'OS_DETECTION_ENABLE = yes' \
  'process_detected_host_os_kb' \
  'notifier_set_os' \
  'DEFINE_SERIAL_COMMANDS_OS' \
  'DEFINE_SERIAL_LAYERS_OS' \
  'default map for that track is not' \
  'OS_UNSURE' \
  'never' \
  'byte-for-byte' \
  'What this does NOT change' \
  'wire protocol is unchanged' \
  'pattern matcher is untouched' \
  'planned future work'; do
  grep -qF "$phrase" README.md && echo "present: $phrase" || echo "MISSING: $phrase"
done
# Expected: every line prints "present:". Fix any "MISSING:".

# 2c. The two canonical snippets are present.
grep -qF 'bool process_detected_host_os_kb(os_variant_t os)' README.md && echo "wiring snippet present"
grep -qF 'DEFINE_SERIAL_LAYERS_OS(OS_LINUX,' README.md && echo "linux example present"
grep -qF 'WT("Google Chrome", "*claude*")' README.md && echo "macOS chrome example present"

# 2d. Features bullet + Setup forward links.
grep -qF 'Optional per-OS maps' README.md && echo "features bullet present"
grep -qE '\(#multi-os-configuration\)' README.md && echo "anchor link(s) present"

# 2e. Running Tests now names BOTH binaries with live counts.
grep -qF 'test_notifier_dispatch' README.md && grep -qF 'test_notifier_os' README.md && echo "both binaries named"
grep -qE '11/11|11 cases' README.md && echo "dispatch count present"
grep -qE '31/31|31 cases' README.md && echo "os count present"

# 2f. Backward-compat guarantee stated in user terms.
grep -qEi 'behaves exactly as before|byte-for-byte' README.md && echo "backward-compat guarantee present"
```

### Level 3: Markdown sanity (renders cleanly)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Balanced code fences (count of ``` must be even).
fences=$(grep -c '```' README.md)
[ $((fences % 2)) -eq 0 ] && echo "code fences balanced ($fences)" || echo "ERROR: unbalanced code fences ($fences)"

# 3b. Headings are well-formed (## or ### , no dangling #).
grep -nE '^#{1,6} ' README.md | grep -vE '^#[0-9]*:#{1,6} (## |### )' | head   # inspect; expect clean headings

# 3c. No accidental duplicate section titles.
grep -nE '^## ' README.md | awk '{print $2}' | sort | uniq -d   # expect: no output

# 3d. (Optional) render check if a markdown tool is available.
command -v markdown >/dev/null && markdown README.md >/dev/null 2>&1 && echo "markdown parses OK" || echo "(no markdown renderer; skip)"
```

### Level 4: Diff hygiene (ONLY README.md changed)

```bash
cd /home/dustin/projects/qmk-notifier

git status --porcelain
# Expected: ` M README.md` and `?? plan/002_c243e735980a/P1M2T3S1/`.
# NOTHING else: notifier.c/h, pattern_match.*, qmk_stubs/*, test_*.c,
# run_*.sh, rules.mk, PRD.md, tasks.json, prd_snapshot.md, .gitignore untouched.

git diff --stat -- README.md
# Expected: README.md shows net additions (the new section + bullet + notes +
# updated paragraphs). Inspect `git diff README.md` to confirm no accidental
# deletion of existing content (the 9-suite table and existing examples must
# remain intact).

# Confirm the NOT-touched files are really untouched.
git diff --name-only | grep -vE '^README.md$|^plan/' && echo "ERROR: unexpected changes" || echo "scope clean: only README.md"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: re-ran `./run_all_tests.sh` (2019) and `./run_notifier_stub_tests.sh`
      (dispatch 11 + os 31, PASSED); status blurb uses the LIVE numbers.
- [ ] Level 2: `## Multi-OS Configuration` present before `## Companion Projects`;
      all 14 presence-check phrases present; both snippets present; Features bullet
      + anchor links present; both binaries named with counts; backward-compat
      guarantee stated.
- [ ] Level 3: code fences balanced; headings well-formed; no duplicate `## ` titles.
- [ ] Level 4: `git status` shows only ` M README.md` + plan/; no other file changed.

### Feature Validation

- [ ] Features list has the multi-OS bullet with an anchor link.
- [ ] Setup step 3 has the optional `OS_DETECTION_ENABLE = yes` block (marked
      optional); step 2 has the optional wiring note; both link to the section.
- [ ] `## Multi-OS Configuration` documents: opt-in nature, enable steps (wiring
      snippet), `DEFINE_*_OS` macros (§10.3 macOS+Linux excerpt), the per-track
      OS-first/default-fallback rule, the push-only design, the backward-compat
      guarantee, and the "What this does NOT change" note (item 3a + 3d).
- [ ] Running Tests names BOTH `test_notifier_dispatch` (11) and `test_notifier_os`
      (31); status blurb reflects the live breakdown (item 3c).
- [ ] The "What this does NOT change" note covers: wire protocol unchanged, matcher
      untouched, host-provided OS / host-rules planned (item 3d).

### Code Quality Validation

- [ ] Follows the README's existing voice, heading depth, and formatting
      (```make/```c/```bash fences, tables, bold emphasis, blank lines).
- [ ] No internal implementation detail (weak symbols, `##os`, `{NULL,0}`)
      leaked into end-user prose.
- [ ] Anchor links use correct GitHub slugs (`#multi-os-configuration`).
- [ ] The 9-suite pattern_match table and existing Usage examples are preserved
      verbatim (additive edits only).

### Documentation & Deployment

- [ ] README is self-contained: a reader can opt into multi-OS using only the
      README (no need to read the PRD).
- [ ] The backward-compat guarantee is unambiguous (a non-opting keymap is
      unaffected).
- [ ] No new env vars, config, or build-system changes documented as if they
      exist (`OS_DETECTION_ENABLE` is the user's keymap flag, clearly marked).

---

## Anti-Patterns to Avoid

- ❌ Don't edit any file other than `README.md` (no `.c`/`.h`/`.sh`, no `rules.mk`,
  no `PRD.md`, no `tasks.json`, no `.gitignore`). This is documentation only; all
  code/tests/runner are already LANDED.
- ❌ Don't change the 9-suite pattern_match test table — it is already accurate
  (2019 total). Only the stub paragraph and the status blurb need updating.
- ❌ Don't rewrite the existing Usage examples (the `enable_vim_mode` style). Add
  the new Multi-OS Configuration section using the PRD §10.3 excerpt
  (`disable_vim` style) verbatim.
- ❌ Don't expose internal implementation (weak symbols, `##os` token-paste,
  per-OS accessor names, `{NULL,0}`) to end users. State the backward-compat
  guarantee in plain user terms.
- ❌ Don't hard-code a stale "2019/2019" that omits the notifier stub gate. Re-run
  the gates and report the live counts (pattern_match 2019 + dispatch 11 + os 31).
- ❌ Don't promise host-provided-OS or host-side-rules support — both are PLANNED
  (§4.7/§14.1), not implemented. The "does NOT change" note must say so.
- ❌ Don't let line-number anchors drift — match edits on unique text, not line
  numbers (the README shifts as edits land).
- ❌ Don't drop the "What this does NOT change" note (item 3d) — it is explicitly
  required and prevents the most common misconception.
- ❌ Don't add the Multi-OS section in the wrong place — it goes AFTER Usage
  (Pattern Matching Syntax) and BEFORE Companion Projects.

---

## Confidence Score: 9/10

The deliverable is a single-file documentation edit (`README.md`) whose exact
markdown for every new/changed section is given verbatim above — including the
two canonical code snippets (PRD §10.1 step-3 wiring + §10.3 macOS/Linux
reference excerpt), the per-track OS-first/default-fallback dispatch rule, the
push-only design, the backward-compat guarantee, the "does NOT change" note, and
the live test counts (pattern_match 2019, dispatch 11, os 31 — **all re-verified
by running the actual gates during research**: `run_all_tests.sh` → "Total tests
run across all suites: 2019"; `run_notifier_stub_tests.sh` → both binaries
`fails=0`, `✓ notifier stub-compile gate PASSED`; per-binary summaries → 11 and
31). The current README structure (every heading + line number), the exact stub
paragraph and status blurb to replace (captured verbatim), and the LANDED API
surface (`notifier_set_os`, `DEFINE_SERIAL_*_OS`, `os_variant_t`) are all
recorded. The single residual uncertainty is non-deterministic: the exact
assertion counts are printed live by the runners, so the implementer must re-run
them and use whatever they print (item 3c) — the PRP instructs this explicitly
and gives the research-time values as the baseline. All dependencies
(notifier.c/h, test_notifier_os.c, run_notifier_stub_tests.sh, run_all_tests.sh)
are COMPLETE and verified green; no code changes are required, so there is no
risk of breaking the build. Scope boundaries (this task touches ONLY README.md;
per-file Mode-A docs were handled by the implementing subtasks) are explicit.