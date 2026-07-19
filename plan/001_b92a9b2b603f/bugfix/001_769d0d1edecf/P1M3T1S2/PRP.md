name: "P1.M3.T1.S2 (bugfix docs) — Correct stale architecture docs: firmware now implements typed commands"
description: "Docs-only sync: three architecture-doc passages still claim the qmk-notifier firmware does NOT implement typed commands (no 0xF0 branch, no handle_typed_command, 'NOT YET IMPLEMENTED'). The firmware (at /home/dustin/projects/qmk-notifier/notifier.c) now implements typed dispatch + emits 0x51 typed replies on the ETX report — confirmed via live hardware testing per the bugfix PRD. Rewrite the stale passages to reflect reality. Task contract names 2 files (firmware_wire_contract.md + findings_and_risks.md); PRP research discovered a 3rd stale passage in system_context.md that would violate the absolute OUTPUT success criterion, so it is included as a required discovery edit. ONLY plan/001_b92a9b2b603f/architecture/*.md modified; no source/Cargo/PRD change; no new files."

---

## Goal

**Feature Goal**: Eliminate every stale "typed-commands-unimplemented" claim in the
`plan/001_b92a9b2b603f/architecture/` docs. The firmware
(`/home/dustin/projects/qmk-notifier/notifier.c`) **now implements typed dispatch**:
`hid_notify()` routes `data[2] == 0xF0` (first report) to a typed-reassembly path,
`handle_typed_command()` dispatches QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT
and emits `[0x51][cmd_echo][payload]` replies, and a `typed_dispatched` guard
suppresses the legacy ack on the ETX report. This is confirmed via **live hardware
testing** (Dactyl-Manuform, VID 0xFEED / PID 0x0000) cross-checked against the C
source (bugfix PRD prd_snapshot.md §Overview + §Issue 1). The docs currently lie —
fix them.

**Deliverable**: A docs-only edit to **architecture Markdown files ONLY** — rewrite
three stale passages in `plan/001_b92a9b2b603f/architecture/`:
- **(A)** `firmware_wire_contract.md` — the trailing `## Firmware Implementation Status`
  section (contract clause a).
- **(B)** `findings_and_risks.md` — finding F4 (contract clause b).
- **(C)** `system_context.md` — the `### Upstream canonical: firmware` subsection
  (**discovered during PRP research**; not in the named contract, but required to
  satisfy the absolute OUTPUT criterion — see "Scope Tension" below).

No source edits, no Cargo.toml change, no PRD change, no new files.

**Success Definition**: `grep -rniE "NOT YET IMPLEMENTED|typed commands yet|does NOT yet implement|does not implement typed|no 0xF0 branch|not yet coded in firmware|will work against firmware once" plan/001_b92a9b2b603f/architecture/` returns **zero** matches; each rewritten passage states the firmware implements typed dispatch + emits 0x51 typed replies on the ETX report (confirmed via live hardware testing); no file outside `plan/001_b92a9b2b603f/architecture/*.md` is modified; `cargo build`/`cargo test` unchanged (sanity gate — plan/ markdown cannot affect compilation).

## User Persona (if applicable)

**Target User**: A developer or maintainer reading the architecture docs to
understand the firmware↔crate wire contract — e.g. a contributor wiring `qmkonnect`'s
capability handshake, or a future implementer deciding whether typed-command tests
need real hardware. Today the docs tell them typed commands are unimplemented (so
they'd write the tests as synthetic-only and assume typed sends time out), which is
false.

**Use Case**: "Does the firmware actually reply to typed commands, or will my
`QUERY_INFO` time out?" The docs must answer: the firmware implements typed
dispatch; it emits one 32-byte reply per `hid_notify` call; the typed `0x51` reply
fires on the ETX report and intermediate reports get a legacy `0` ack — exactly the
per-report reply model the bugfix PRD §Issue 1 documents.

**Pain Points Addressed**: (1) The docs contradict both the firmware source and the
bugfix PRD's live-hardware findings, undermining trust in the architecture record.
(2) Finding F4's stale rationale ("tests must use synthetic byte buffers, not live
hardware") misleads future test authors — live typed replies are now observable
(though synthetic buffers remain valid and preferred for unit tests).

## Why

- **Bugfix PRD §Overview / §Issue 1** (prd_snapshot.md): the bugfix investigation
  performed **live hardware testing** and **firmware-source cross-check** that
  PROVED the firmware emits per-report replies and typed `0x51` replies on ETX. The
  architecture docs predate this finding and were never updated.
- **The architecture docs are the durable record** of the firmware↔crate contract.
  `firmware_wire_contract.md` is explicitly titled "(Canonical)" and its Command
  Table / Constants / Reply-Disambiguation sections ALREADY describe typed commands
  in full — only its trailing "Firmware Implementation Status" section contradicts
  the rest of the file. Fixing it removes an internal contradiction.
- **Item DOCS = Mode B** — this IS the documentation-sync task for the bugfix
  changeset's architecture docs. No source/Cargo/PRD work is in scope.
- **Scope discipline** (clause 3c): "These are plan/ architecture docs, not shipped
  docs — they inform future development. Keep edits factual and concise." ⇒ rewrite
  the stale passages in place; do NOT re-architect the docs, add new sections, or
  restate the full wire contract (it is already documented correctly elsewhere in
  the same files).

## What

### The THREE edits

**(A) `firmware_wire_contract.md` — rewrite the `## Firmware Implementation Status` section (lines 118-130).**
Replace the "NOT YET IMPLEMENTED … no 0xF0 branch … will work against firmware once
the firmware implements §4.6" passage with a note that the firmware now implements
typed dispatch and emits 0x51 typed replies on the ETX report, confirmed via live
hardware testing. Update the code snippet to reflect the actual `if (!typed_dispatched)`
guard + the typed-dispatch description. Verbatim BEFORE/AFTER in "Implementation
Patterns" §A.

**(B) `findings_and_risks.md` — rewrite finding F4 (lines 28-31).**
Replace the heading + "does not implement typed commands yet / this is by design"
rationale with a note that the firmware now implements typed commands (the
timeout-only fallback is no longer the only path). Verbatim BEFORE/AFTER in
"Implementation Patterns" §B.

**(C) `system_context.md` — rewrite the `### Upstream canonical: firmware` subsection body (lines 90-94).**
Replace "does NOT yet implement the typed-command namespace … no 0xF0 branch … no
handle_typed_command() … not yet coded in firmware" with a note that the firmware
now implements the typed-command namespace. Verbatim BEFORE/AFTER in "Implementation
Patterns" §C.

### Scope Tension (CRITICAL — read before deciding to skip edit C)

- The task **CONTRACT (clauses a, b)** names exactly **two** files: A
  (firmware_wire_contract.md) and B (findings_and_risks.md). The bugfix's own
  `system_context.md` "Stale Architecture Docs to Update" inventory (bugfix
  architecture L170-175) also lists only those two.
- The task **OUTPUT (clause 4)** is **absolute**: *"Architecture docs no longer
  claim typed commands are unimplemented. They accurately reflect the firmware's
  current per-report reply model."*
- **Edit C** (`system_context.md:90-94`) is a stale claim **not** named in the
  contract, but it makes the SAME false assertion ("does NOT yet implement the
  typed-command namespace … no 0xF0 branch … no handle_typed_command()"). Leaving
  it would mean: after a strict A+B-only edit, an architecture doc STILL claims
  typed commands are unimplemented ⇒ the binding success criterion is NOT met.
- **Resolution adopted by this PRP**: include C as a **required** edit, flagged as
  a research discovery beyond the named (a)/(b). The cost is one paragraph and it
  is the same factual correction. If the orchestrator insists on strict A+B-only
  scope, C may be deferred to a follow-up task — but **then the OUTPUT criterion is
  knowingly violated and should be re-scoped**. The PRP default is: fix all three.

### Success Criteria
- [ ] `grep -rniE "NOT YET IMPLEMENTED|typed commands yet|does NOT yet implement|does not implement typed|no 0xF0 branch|not yet coded in firmware|will work against firmware once" plan/001_b92a9b2b603f/architecture/` → **zero matches** (covers A, B, C).
- [ ] Each rewritten passage states the firmware implements typed dispatch and emits `0x51` typed replies on the ETX report (the per-report reply model).
- [ ] At least one passage mentions the **live hardware testing** that confirmed it.
- [ ] The remaining (accurate) sections of each doc are untouched (Command Table, Constants, Reply Disambiguation in firmware_wire_contract.md; F1/F2/F3/F5/F6 + R1-R5 in findings_and_risks.md; surrounding crate-state sections in system_context.md).
- [ ] ONLY `plan/001_b92a9b2b603f/architecture/*.md` modified; no source/Cargo.toml/PRD/README change; no new files.
- [ ] `cargo build` + `cargo test` still pass (sanity — plan/ markdown cannot affect them; this gate only catches an accidental source edit).

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything needed
> to implement this successfully?"_ — **Yes.** The exact current stale text of all
> three passages (with line numbers, grep-verified), the exact designed replacement
> wording (verbatim, copy-paste ready), the verified firmware reality read directly
> from `/home/dustin/projects/qmk-notifier/notifier.c` + `notifier.h` (with exact
> line numbers for the 0xF0 branch, handle_typed_command, the typed_dispatched
> guard), the bugfix-PRD live-hardware confirmation (cited), the scope-tension
> resolution, AND the precise grep validation gates are all given below and in
> research `notes.md`. No guesses required.

### Documentation & References

```yaml
# MUST EDIT — the three stale passages (A, B, C)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "Holds stale passage A: the trailing '## Firmware Implementation Status'
        section (lines 118-130) says 'NOT YET IMPLEMENTED … no 0xF0 branch … will
        work against firmware once the firmware implements §4.6'. This CONTRADICTS
        the doc's own Command Table / Constants (NOTIFY_PROTO_VER=2) / Reply
        Disambiguation sections, which already describe typed commands in full."
  pattern: "Anchor the edit on the UNIQUE heading '## Firmware Implementation
            Status' (the LAST section of the file) + the stale body. Replace the
            whole section body. Keep the '## ' H2 heading (or refine it)."
  gotcha: "Do NOT touch the Command Table, Field Definitions, Reply Disambiguation,
           or Constants sections — they are ALREADY accurate. Only the trailing
           status section is stale."

- file: plan/001_b92a9b2b603f/architecture/findings_and_risks.md
  why: "Holds stale passage B: finding F4 (lines 28-31) '### F4: The firmware does
        not implement typed commands yet / This is by design … tests must use
        synthetic byte buffers, not live hardware.'"
  pattern: "Anchor on the UNIQUE '### F4:' heading + its body paragraph. Rewrite the
            heading + body in place. Keep the '### F4:' numbering so the F1..F6
            sequence stays intact."
  gotcha: "Do NOT renumber or delete F4 (breaks the F1..F6 sequence readers rely
           on). Rewrite it in place. F1/F2/F3/F5/F6 and R1-R5 are about THIS crate's
           Rust code and are unaffected — do not touch them."

- file: plan/001_b92a9b2b603f/architecture/system_context.md
  why: "Holds stale passage C (DISCOVERED, not in contract): the '### Upstream
        canonical: firmware (qmk-notifier)' subsection body (lines 90-94) says
        'does NOT yet implement the typed-command namespace … no 0xF0 branch … no
        handle_typed_command() … not yet coded in firmware'. Required to satisfy the
        absolute OUTPUT criterion."
  pattern: "Anchor on the UNIQUE heading '### Upstream canonical: firmware' + its
            body paragraph. Rewrite the body in place. Keep the heading."
  gotcha: "This passage was NOT named in the task contract (clauses a/b name only
           firmware_wire_contract.md + findings_and_risks.md). See 'Scope Tension'
           above — including it is the PRP default because leaving it violates the
           OUTPUT success criterion. Surrounding sections are about the Rust crate
           and are unaffected."

# MUST READ — research notes: the full 3-location sweep + scope-tension analysis
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M3T1S2/research/notes.md
  why: "F1 = the comprehensive grep sweep that FOUND all three stale claims (incl.
        the unnamed C); F2 = the verified firmware reality (exact line numbers for
        the 0xF0 branch notifier.c:854, handle_typed_command notifier.c:717, the
        typed_dispatched guard notifier.c:1017, notifier.h constants); F3 = per-doc
        internal-consistency check (what else is accurate, don't touch); F5 =
        validation approach for a plan-docs edit."

# MUST READ — the bugfix PRD's live-hardware confirmation (the authority for the correction)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "§Overview: 'Live hardware testing against a real QMK keyboard
        (Dactyl-Manuform, VID 0xFEED / PID 0x0000) … Cross-checked observed wire
        behavior against the firmware source (notifier.c::hid_notify).' §Issue 1:
        quotes the EXACT 'if (!typed_dispatched) { response[0]=match; ... }'
        firmware snippet and the per-report reply proof (1-report→[1],
        2-report→[0,1], 4-report→[0,0,0,1]). This is the authoritative basis for the
        correction."
  section: "Overview", "Issue 1 (Critical) — 'a typed reply (0x51…) is emitted only
           on the ETX report (firmware sets typed_dispatched there and skips the
           legacy ack)'"

# REFERENCE — the firmware source itself (ground truth, read-only)
- file: /home/dustin/projects/qmk-notifier/notifier.c
  why: "PROVES the firmware implements typed dispatch. The 0xF0 branch at line 854,
        handle_typed_command() at line 717 emitting response[0]=0x51 (line 690), the
        typed_dispatched=true at ETX (line 909), and the 'if (!typed_dispatched)'
        legacy-ack guard (line 1017). Read these to word the replacement accurately."
  section: "hid_notify (line 835)", "handle_typed_command (line 717)", "legacy ack
           guard (line 1017)"

- file: /home/dustin/projects/qmk-notifier/notifier.h
  why: "Constants confirm typed-capable firmware: NOTIFY_CMD_DISCRIMINATOR=0xF0
        (line 44), NOTIFY_RESPONSE_MARKER=0x51 (line 46), NOTIFY_PROTO_VER=2
        'typed-command capable' (line 53). These match firmware_wire_contract.md's
        existing Constants section verbatim."

# REFERENCE — the sibling PRP (zero file overlap; no coordination needed)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M3T1S1/PRP.md
  why: "P1.M3.T1.S1 (running in parallel) edits README.md ONLY. This task edits
        plan/.../architecture/*.md ONLY. No file overlap, no merge risk."
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # UNCHANGED — DO NOT TOUCH
├── Cargo.lock
├── README.md           # DO NOT TOUCH (parallel P1.M3.T1.S1 owns it)
├── PRD.md              # READ-ONLY (forbidden to modify)
├── .gitignore
├── src
│   ├── main.rs         # DO NOT TOUCH
│   ├── lib.rs          # DO NOT TOUCH
│   ├── error.rs        # DO NOT TOUCH
│   └── core.rs         # DO NOT TOUCH
└── plan/001_b92a9b2b603f/architecture/      # <-- ONLY dir touched
    ├── firmware_wire_contract.md   # EDIT A (## Firmware Implementation Status, L118-130)
    ├── findings_and_risks.md       # EDIT B (### F4, L28-31)
    ├── system_context.md           # EDIT C (### Upstream canonical: firmware, L90-94) [discovered]
    ├── external_deps.md            # DO NOT TOUCH (grep-swept: no stale claim)
    └── transport_evolution.md      # DO NOT TOUCH (grep-swept: no stale claim)
```

### Desired Codebase tree with files to be modified

```bash
plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md   # MODIFIED: rewrite "Firmware Implementation Status" body (A)
plan/001_b92a9b2b603f/architecture/findings_and_risks.md       # MODIFIED: rewrite finding F4 heading+body (B)
plan/001_b92a9b2b603f/architecture/system_context.md           # MODIFIED: rewrite "Upstream canonical: firmware" body (C) [discovered]
# (no other files changed; NO new files)
```

> No new files. Three existing architecture .md files edited in place. No tests, no
> source changes, no Cargo.toml/PRD/README change.

### Known Gotchas of our codebase & Library Quirks

```markdown
<!-- CRITICAL (the sweep found THREE stale claims, not two): the task contract (a/b)
     names firmware_wire_contract.md + findings_and_risks.md. PRP research discovered
     a THIRD stale claim in system_context.md:90-94. See "Scope Tension" — leaving C
     violates the absolute OUTPUT criterion. Fix all three unless the orchestrator
     explicitly re-scopes. -->

<!-- CRITICAL (anchor by TEXT, not line number): line numbers drift if anything above
     changes. Anchor each edit on a UNIQUE substring:
       A: the '## Firmware Implementation Status' heading + 'NOT YET IMPLEMENTED' opener
       B: the '### F4:' heading + 'does not implement typed commands yet'
       C: the '### Upstream canonical: firmware' heading + 'does NOT yet implement'
     (system_context.md uses **bold** 'NOT'; findings_and_risks.md F4 does not — both
     are matched by the case-insensitive grep gate.) -->

<!-- GOTCHA (do NOT touch accurate sections): each doc has ONE stale passage and many
     accurate ones. firmware_wire_contract.md's Command Table / Field Definitions /
     Reply Disambiguation / Constants are ALREADY correct (they fully describe typed
     commands) — editing them is scope creep and risks introducing errors.
     findings_and_risks.md's F1/F2/F3/F5/F6 + R1-R5 are about the Rust crate —
     unaffected. system_context.md's surrounding crate/consumer sections — unaffected. -->

<!-- GOTCHA (keep the F4 numbering): do NOT renumber or delete finding F4 — readers
     cross-reference F1..F6 by number. Rewrite F4 IN PLACE (heading text + body). -->

<!-- GOTCHA (these are plan/ docs, not shipped docs): they inform future development
     (clause 3c). Keep edits factual and concise — do NOT restate the full wire
     contract (it is already in firmware_wire_contract.md), do NOT add new sections,
     do NOT add code beyond a short illustrative snippet. A few accurate sentences per
     passage is the target. -->

<!-- GOTCHA (the "(qmk-notifier)" firmware path): the firmware source lives at
     /home/dustin/projects/qmk-notifier/ (HYPHEN), NOT /home/dustin/projects/qmk_notifier/
     (UNDERSCORE = this Rust crate). Do not confuse the two when referencing firmware
     reality. The architecture docs already reference the hyphen path correctly. -->

<!-- NOTE (plan/ markdown cannot affect cargo): cargo build/test are SANITY gates
     only — they confirm you didn't accidentally edit a source file. A passing build
     does NOT validate the prose. Use the grep gates (Level 2) for the real validation. -->

<!-- NOTE (no conflict with parallel P1.M3.T1.S1): that task edits README.md ONLY;
     this task edits plan/.../architecture/*.md ONLY. Zero file overlap. No merge risk. -->
```

## Implementation Blueprint

### Data models and structure
None — this is a Markdown documentation edit. No types, no code, no config.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: EDIT firmware_wire_contract.md — rewrite "Firmware Implementation Status" (passage A)
  - LOCATE: the trailing '## Firmware Implementation Status' section (file's LAST
          section, currently lines 118-130). Anchor on the heading + 'NOT YET
          IMPLEMENTED' opener (UNIQUE).
  - REPLACE the entire section body (the 'NOT YET IMPLEMENTED … no 0xF0 branch …
          will work against firmware once the firmware implements §4.6' text +
          the stale code snippet) with the §A replacement VERBATIM from
          "Implementation Patterns" below:
            * state the firmware now implements typed dispatch (0xF0 branch in
              hid_notify, handle_typed_command emitting 0x51 replies)
            * note the per-report reply model (0x51 typed reply on the ETX report;
              intermediate reports get a legacy 0 ack)
            * cite the live hardware testing confirmation
            * update/replace the code snippet to reflect the actual
              'if (!typed_dispatched)' guard
  - DO NOT: touch the Command Table / Field Definitions / Reply Disambiguation /
          Constants sections (all accurate). Do NOT add a new section — rewrite
          this one in place (may keep or refine the '## Firmware Implementation
          Status' heading).

Task 2: EDIT findings_and_risks.md — rewrite finding F4 (passage B)
  - LOCATE: '### F4: The firmware does not implement typed commands yet' (currently
          lines 28-31). Anchor on the '### F4:' heading (UNIQUE).
  - REPLACE the heading + body paragraph ('does not implement typed commands yet /
          This is by design … tests must use synthetic byte buffers, not live
          hardware') with the §B replacement VERBATIM.
  - KEEP: the '### F4:' numbering (rewrite in place; do NOT renumber/delete).
          Do NOT touch F1/F2/F3/F5/F6 or R1-R5.

Task 3: EDIT system_context.md — rewrite "Upstream canonical: firmware" body (passage C) [DISCOVERED]
  - LOCATE: '### Upstream canonical: firmware (qmk-notifier)' (currently lines
          89-94; stale body at lines 90-94). Anchor on the heading (UNIQUE).
  - REPLACE the body paragraph ('does NOT yet implement the typed-command namespace
          … no 0xF0 branch … no handle_typed_command() … not yet coded in firmware')
          with the §C replacement VERBATIM.
  - SCOPE NOTE: this passage was NOT named in the task contract. It is included
          because leaving it violates the absolute OUTPUT criterion (see "Scope
          Tension"). If the orchestrator explicitly defers it, skip Task 3 and
          re-scope the OUTPUT criterion — but the PRP default is to fix it.
  - DO NOT: touch surrounding sections (crate state, consumer qmkonnect impact).

Task 4: VALIDATE (the real gates for a plan-docs task)
  - RUN (Level 2 grep gates): confirm ALL stale claims GONE across the whole
          architecture dir + new accurate wording PRESENT in each passage.
  - RUN (Level 3 sanity): `cargo build` + `cargo test` still pass (plan/ markdown
          cannot affect them; this only catches an accidental source edit).
  - RUN (Level 4 cross-check): `git diff --stat` shows ONLY the three architecture
          .md files changed; spot-check each rewritten passage against the firmware
          reality in research notes.md F2.
```

### Implementation Patterns & Key Details

#### §A — replacement for passage A (firmware_wire_contract.md "Firmware Implementation Status")

**BEFORE** (current lines 118-130, the stale text):
```markdown
## Firmware Implementation Status

**NOT YET IMPLEMENTED.** The firmware `notifier.c` has no `0xF0` branch in
`hid_notify()`. Only the legacy path exists:
```c
uint8_t response[RAW_REPORT_SIZE] = {0};
response[0] = match;  // 0 or 1
raw_hid_send(response, RAW_REPORT_SIZE);  // RAW_REPORT_SIZE = 32
```

This crate's typed-command transport will work against firmware once the firmware
implements §4.6. Until then, typed commands will time out — which is the designed
fallback behavior.
```

**AFTER** (copy-paste ready replacement):
```markdown
## Firmware Implementation Status

**Implemented.** The firmware `notifier.c` now implements the §4.6 typed-command
namespace. `hid_notify()` routes the first report's `data[2] == 0xF0` into a
length-aware typed-reassembly path; at ETX, `handle_typed_command()` dispatches the
reassembled command (QUERY_INFO / QUERY_CALLBACK / SET_OS / APPLY_HOST_CONTEXT) and
emits the `[0x51][cmd_echo][payload]` typed reply. The legacy 0/1 acknowledgement
now runs under an `if (!typed_dispatched)` guard, so a typed message suppresses the
legacy ack on its ETX report:
```c
if (!typed_dispatched) {
    uint8_t response[RAW_REPORT_SIZE] = {0};
    response[0] = match;  // 0 or 1  (legacy path only; typed replies are sent inside handle_typed_command)
    raw_hid_send(response, RAW_REPORT_SIZE);  // RAW_REPORT_SIZE = 32
}
```

This was confirmed via **live hardware testing** against a real QMK keyboard
(Dactyl-Manuform, VID 0xFEED / PID 0x0000) running qmk-notifier firmware,
cross-checked against the `notifier.c::hid_notify` source. The firmware's reply
model is **per-report**: `hid_notify()` is invoked once per 32-byte report and sends
a 32-byte reply at the end of every call — so an N-report message produces N replies,
where only the LAST (ETX-report) reply carries the real result (typed `0x51…` or the
legacy match-bool), and intermediate reports reply with a legacy `0`. This crate's
reply capture must retain the ETX-report reply (see the bugfix PRD §Issue 1).
```

#### §B — replacement for passage B (findings_and_risks.md finding F4)

**BEFORE** (current lines 28-31, the stale text):
```markdown
### F4: The firmware does not implement typed commands yet
This is by design. The transport layer must handle timeouts gracefully. The PRD
explicitly designs for this (§10.2, §14 invariant #6). The tests for reply parsing
must use synthetic byte buffers, not live hardware.
```

**AFTER** (copy-paste ready replacement — keeps the `### F4:` numbering):
```markdown
### F4: The firmware now implements typed commands
The firmware `notifier.c` implements the §4.6 typed-command namespace: `hid_notify()`
routes `data[2] == 0xF0` (first report) to a typed-reassembly path, and
`handle_typed_command()` dispatches QUERY_INFO / QUERY_CALLBACK / SET_OS /
APPLY_HOST_CONTEXT, emitting `[0x51][cmd_echo][payload]` typed replies on the ETX
report. Confirmed via live hardware testing (Dactyl-Manuform) cross-checked against
the firmware source. The transport layer must STILL handle timeouts gracefully
(§10.2, §14 invariant #6) — a non-capable or offline device replies with a legacy
`0`/`1` or nothing, which parses to `Timeout` semantics. Unit tests for reply
parsing should continue to use synthetic byte buffers (deterministic, no hardware
dependency); typed replies are additionally observable on live hardware.
```

#### §C — replacement for passage C (system_context.md "Upstream canonical: firmware" body)

**BEFORE** (current lines 90-94, the stale text):
```markdown
The firmware `notifier.c` **does NOT yet implement the typed-command namespace**.
There is no `0xF0` branch in `hid_notify()`, no `handle_typed_command()`, no
`host_layer`/`host_cb_enabled` trackers. The typed-command wire contract is fully
specified in `qmk-notifier/PRD.md` §4.6 but not yet coded in firmware.
```

**AFTER** (copy-paste ready replacement):
```markdown
The firmware `notifier.c` **implements the typed-command namespace** (§4.6).
`hid_notify()` routes the first report's `data[2] == 0xF0` to a typed-reassembly
path; `handle_typed_command()` dispatches QUERY_INFO / QUERY_CALLBACK / SET_OS /
APPLY_HOST_CONTEXT and emits `[0x51][cmd_echo][payload]` replies on the ETX report,
with the `host_layer`/`host_cb_enabled` trackers updated accordingly. Confirmed via
live hardware testing (Dactyl-Manuform, VID 0xFEED / PID 0x0000) cross-checked
against the firmware source. The firmware uses a **per-report reply model**: one
32-byte reply per `hid_notify()` call, where only the ETX-report reply carries the
real (typed `0x51…` or legacy match-bool) result and intermediate reports reply with
a legacy `0`.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md (passage A only)."
  - modify: "plan/001_b92a9b2b603f/architecture/findings_and_risks.md (passage B / F4 only)."
  - modify: "plan/001_b92a9b2b603f/architecture/system_context.md (passage C only)."

NO OTHER CHANGES:
  - Cargo.toml:   "UNCHANGED."
  - src/*:        "UNCHANGED."
  - README.md:    "UNCHANGED (parallel P1.M3.T1.S1 owns it)."
  - PRD.md:       "READ-ONLY."
  - external_deps.md / transport_evolution.md: "UNCHANGED (grep-swept: no stale claim)."

DEPENDENCIES / CONFIG:
  - none. No crate, env, runtime, or build change.

SCOPE BOUNDARY:
  - ONLY the three architecture .md passages (A, B, C) are rewritten. Do NOT:
    * touch any accurate section of those docs (Command Table, Constants, Reply
      Disambiguation, F1/F2/F3/F5/F6, R1-R5, surrounding crate/consumer sections).
    * add new sections, headings, or files.
    * restate the full wire contract (already documented in firmware_wire_contract.md).
    * edit the bugfix-dir system_context.md (L171/L191 are tracking notes in the
      bugfix planning copy, not firmware-status claims — out of scope).
    * bump Cargo.toml, touch src/*, README.md, or PRD.md.
```

## Validation Loop

### Level 1: Markdown Well-Formedness (Immediate Feedback)

```bash
# Eyeball each rewritten passage — the heading level + paragraph structure must be
# preserved (A keeps its '## ' H2; B keeps '### F4:' H3; C keeps '### Upstream
# canonical: firmware' H3). Code fences in passage A must be balanced (```c … ```).
sed -n '118,140p' plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
sed -n '28,40p'   plan/001_b92a9b2b603f/architecture/findings_and_risks.md
sed -n '88,100p'  plan/001_b92a9b2b603f/architecture/system_context.md
# Expected: clean Markdown; no orphan code fence; heading levels unchanged.
```

### Level 2: Grep Gates (the real validation for a docs task)

```bash
# (1) THE PRIMARY GATE: ALL stale firmware-status claims are GONE across the dir.
grep -rniE "NOT YET IMPLEMENTED|typed commands yet|does NOT yet implement|does not implement typed|no 0xF0 branch|not yet coded in firmware|will work against firmware once" plan/001_b92a9b2b603f/architecture/ && echo "FAIL: stale claim still present" || echo "PASS: no stale firmware-status claim remains"
# Expected: PASS (zero matches).

# (2) Per-passage: passage A stale opener is GONE.
grep -n "NOT YET IMPLEMENTED" plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md && echo "FAIL A" || echo "PASS A"
# Expected: PASS (zero matches).

# (3) Per-passage: passage A accurate wording is PRESENT.
grep -nE "Implements|implements the .* typed-command namespace|handle_typed_command|0x51" plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
# Expected: ≥1 match (the new "Implemented." opener + handle_typed_command + 0x51).

# (4) Per-passage: passage B (F4) stale wording is GONE, accurate wording PRESENT.
grep -n "does not implement typed commands yet" plan/001_b92a9b2b603f/architecture/findings_and_risks.md && echo "FAIL B" || echo "PASS B (stale gone)"
grep -nE "### F4: The firmware now implements typed commands" plan/001_b92a9b2b603f/architecture/findings_and_risks.md
# Expected: "PASS B (stale gone)"; exactly ONE F4 heading match (renamed in place).

# (5) Per-passage: passage C (system_context) stale wording is GONE, accurate PRESENT.
grep -n "does NOT yet implement the typed-command namespace" plan/001_b92a9b2b603f/architecture/system_context.md && echo "FAIL C" || echo "PASS C (stale gone)"
grep -nE "implements the typed-command namespace" plan/001_b92a9b2b603f/architecture/system_context.md
# Expected: "PASS C (stale gone)"; ≥1 match for the new accurate wording.

# (6) At least one passage cites the live-hardware-testing confirmation.
grep -rilE "live hardware testing" plan/001_b92a9b2b603f/architecture/
# Expected: ≥1 file (A and/or B and/or C).

# (7) The per-report reply model (the OUTPUT criterion's second clause) is stated.
grep -rilE "per-report|one 32-byte reply per|ETX-report reply|per hid_notify" plan/001_b92a9b2b603f/architecture/
# Expected: ≥1 match (passage A states it; C may also).

# (8) No accidental new H2/H3 heading was added (count headings — scope discipline).
for f in firmware_wire_contract findings_and_risks system_context; do echo -n "$f headings: "; grep -cE "^#" plan/001_b92a9b2b603f/architecture/$f.md; done
# Expected: heading count UNCHANGED from before the edit for findings_and_risks and
#   system_context (rewritten in place). firmware_wire_contract may be +0 (heading
#   kept) — a net-new heading would be a scope smell.
```

### Level 3: Sanity Gate (plan/ markdown cannot affect compilation)

```bash
# These files are under plan/ and are NOT compiled, so this ONLY proves you didn't
# accidentally edit a source file.
cargo build 2>&1 | tail -2
# Expected: "Finished `dev` profile ..." (no error).

cargo test 2>&1 | tail -3
# Expected: "test result: ok. … passed; 0 failed". A failure here is NOT caused by a
#   plan-doc edit — investigate whether a source file was touched by mistake.
```

### Level 4: Cross-Check the Doc Prose Against the Firmware Source & PRD

```bash
# (1) The doc claim "0xF0 branch in hid_notify" must match the actual firmware.
grep -nE "data\[2\] == NOTIFY_CMD_DISCRIMINATOR|0xF0 branch" /home/dustin/projects/qmk-notifier/notifier.c
# Expected: notifier.c:854 proves the branch exists.

# (2) The doc claim "handle_typed_command emits 0x51" must match the firmware.
grep -nE "handle_typed_command|response\[0\] = NOTIFY_RESPONSE_MARKER|0x51" /home/dustin/projects/qmk-notifier/notifier.c
# Expected: notifier.c:717 (fn), :690 (response[0]=0x51).

# (3) The doc claim "if (!typed_dispatched) guard" must match the firmware.
grep -nE "if \(!typed_dispatched\)" /home/dustin/projects/qmk-notifier/notifier.c
# Expected: notifier.c:1017 (the legacy-ack guard the docs now describe).

# (4) Confirm the doc edits are the ONLY diff (no stray source/Cargo/README edits).
git diff --stat
# Expected: exactly THREE files changed, all under plan/.../architecture/. If src/*,
#   Cargo.toml, or README.md appear, STOP — accidental edit; revert it.
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1: each rewritten passage renders as clean Markdown (heading level
      preserved; passage A's code fence balanced).
- [ ] Level 2: the dir-wide stale-claim grep (gate 1) returns **zero** matches.
- [ ] Level 2: per-passage greps (gates 2-5) all PASS (stale gone, accurate present).
- [ ] Level 2: at least one passage cites live-hardware testing (gate 6) and the
      per-report reply model (gate 7).
- [ ] Level 3: `cargo build` + `cargo test` still pass (sanity; plan-doc-only edit).
- [ ] Level 4: `git diff --stat` shows ONLY the three architecture .md files.

### Feature Validation
- [ ] Passage A (firmware_wire_contract.md) states the firmware implements typed
      dispatch + emits 0x51 on the ETX report, with the corrected code snippet.
- [ ] Passage B (findings_and_risks.md F4) is renamed/rewritten in place (keeps the
      F4 number) and notes the firmware now implements typed commands.
- [ ] Passage C (system_context.md) is corrected (or, if explicitly deferred, the
      OUTPUT criterion is re-scoped — documented, not silently skipped).
- [ ] No stale "not yet implemented" / "does NOT yet implement" / "no 0xF0 branch"
      wording remains anywhere in `plan/001_b92a9b2b603f/architecture/`.

### Code Quality Validation
- [ ] ONLY the three architecture .md passages modified; edits are in-place rewrites.
- [ ] Accurate sibling sections (Command Table, Constants, F1/F2/F3/F5/F6, R1-R5,
      crate/consumer sections) left untouched with documented rationale.
- [ ] No new sections/headings/files added (clause 3c: "Keep edits factual and concise").
- [ ] Edits anchor on unique text, not line numbers (line-number drift safety).
- [ ] No confusion of the hyphen firmware repo (qmk-notifier) with the underscore
      Rust crate (qmk_notifier).

### Documentation & Deployment
- [ ] The corrected docs now match the firmware source AND the bugfix PRD's
      live-hardware findings (no internal contradiction in firmware_wire_contract.md).
- [ ] Cargo.toml unchanged; no source/Cargo/PRD/README change; no new files.
- [ ] No new environment variables, config, or runtime changes.

---

## Anti-Patterns to Avoid

- ❌ Don't leave passage C (system_context.md) unedited WITHOUT explicitly
      re-scoping. The absolute OUTPUT criterion ("Architecture docs no longer claim
      typed commands are unimplemented") is violated if any stale claim remains.
      Either fix C or document a deliberate deferral — never silently skip it.
- ❌ Don't touch the accurate sections. firmware_wire_contract.md's Command Table /
      Constants already describe typed commands in full; findings_and_risks.md's
      F1/F2/F3/F5/F6 + R1-R5 are about the Rust crate; system_context.md's crate/
      consumer sections are unaffected. Editing them is scope creep.
- ❌ Don't renumber or delete finding F4. Readers cross-reference F1..F6 by number.
      Rewrite F4 in place (rename its heading + body, keep `### F4:`).
- ❌ Don't anchor edits by line number. Line numbers drift. Anchor on unique text:
      `## Firmware Implementation Status`, `### F4:`, `### Upstream canonical: firmware`.
- ❌ Don't add new sections, headings, or files. Clause 3c: "These are plan/
      architecture docs, not shipped docs — they inform future development. Keep
      edits factual and concise." Rewrite the stale passages in place.
- ❌ Don't restate the full wire contract in passage A. The Command Table / Field
      Definitions / Constants sections already document it — passage A only needs to
      correct the implementation STATUS (implemented, per-report reply model).
- ❌ Don't over-claim. The firmware implements typed dispatch AND the transport still
      must handle timeouts (non-capable/offline devices). F4's "handle timeouts
      gracefully" point stays valid — only the "does not implement" premise is false.
- ❌ Don't confuse the two repos. `/home/dustin/projects/qmk-notifier/` (HYPHEN) =
      firmware (notifier.c, notifier.h). `/home/dustin/projects/qmk_notifier/`
      (UNDERSCORE) = this Rust crate. The architecture docs reference the hyphen path
      correctly; don't "fix" it.
- ❌ Don't edit the bugfix-dir `system_context.md` (L171/L191). Those are TRACKING
      notes inside the bugfix's planning copy (an inventory of stale docs to fix),
      not firmware-status claims about the firmware itself. They are out of scope.
- ❌ Don't touch src/*, Cargo.toml, README.md, or PRD.md. This is a plan-docs-only
      task. `cargo build`/`cargo test` are sanity gates to CATCH an accidental source
      edit, not to validate the prose.
- ❌ Don't conflate "the passage is accurate" with "no validation needed." A plan-doc
      edit still requires the grep gates (stale gone dir-wide, accurate present) and
      the firmware-source cross-check (Level 4).

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable is
**three small Markdown passage rewrites** with the exact stale BEFORE text (grep +
sed verified, with line numbers) and the exact designed AFTER text both given
verbatim and copy-paste ready. The verified firmware reality was read **directly**
from `/home/dustin/projects/qmk-notifier/notifier.c` (0xF0 branch at :854,
handle_typed_command at :717 emitting `response[0]=0x51` at :690, the
`typed_dispatched` guard at :1017) and `notifier.h` (NOTIFY_PROTO_VER=2) — not
inferred. The bugfix PRD's live-hardware confirmation is cited as the authority. The
one scope judgment call (passage C, discovered beyond the named contract) is
documented with both the strict-contract reading and the success-criterion reading,
with a clear default (fix it) and a clear escape hatch (defer + re-scope OUTPUT).
Validation is grep-driven (dir-wide stale-claim sweep + per-passage presence checks)
plus a cargo sanity gate to catch any accidental source edit and a `git diff --stat`
gate to confirm only the three architecture files changed. The only residual risk —
an implementer skipping passage C and silently violating the success criterion — is
eliminated by foregrounding the Scope Tension in Goal/What/tasks/anti-patterns.