name: "P1.M3.T1.S1 (bugfix docs) — Update README.md reply-capture & technical details to v0.3.1"
description: "Docs-only sync: rewrite the ONE stale 'Reply parsing' bullet in README.md Technical Details (currently describes the buggy capture-FIRST behavior) to describe the v0.3.1 capture-LAST + pre-send-drain behavior. README.md is the ONLY file modified. Item clauses (c) parse_cli_args and (d) Overview are verified NO-OPs (parse_cli_args isn't mentioned; the Overview mention is a capability claim, not a mechanism). Cargo.toml version stays 0.3.0 (version bump out of scope)."

---

## Goal

**Feature Goal**: Eliminate the single stale technical description left in
`README.md` by the v0.3.0 docs pass: the "Reply parsing" bullet in Technical
Details (README.md:160-163) still says *"after each burst, one 32-byte IN report
is read with a bounded timeout"* — which describes the **buggy capture-FIRST**
behavior fixed by Issue 1 (P1.M1.T2) and is silent on the pre-send drain fixed by
Issue 3 (P1.M1.T3). Rewrite it to describe the **v0.3.1 capture-LAST + pre-send-
drain** behavior that the merged code now implements.

**Deliverable**: A docs-only edit to **`README.md` ONLY** — rewrite the four-line
"Reply parsing" bullet (Technical Details, lines 160-163) into an accurate
"Reply capture & parsing (v0.3.1)" bullet covering (a) capture-last-reply and
(b) pre-send-drain, while preserving the existing `0x51`/`0`/`1`/`Timeout`
disambiguation tail. No new sections, no new files, no source edits, no
Cargo.toml change.

**Success Definition**: `grep "one 32-byte IN report is read with a bounded"`
README.md returns **zero** matches (buggy text gone); the new bullet states the
crate *reads up to `batch_count` replies and retains the LAST* and that *a
non-blocking IN-buffer drain before each send flushes stale replies*; the README
bullet list still renders (2-space continuation indent matching siblings); no
file other than `README.md` is modified; `cargo build`/`cargo test` unchanged
(sanity gate — README cannot affect compilation, but this proves no accidental
source edit).

## User Persona (if applicable)

**Target User**: A developer reading the README to understand how this crate
collects device replies — either a contributor wiring `qmkonnect` against
`CommandResponse`, or a maintainer auditing Issue 1/3 fixes. Today the README
lies to them (says "one report is read" = first reply, which is the bug).

**Use Case**: "I sent a 44-byte window string and got `Legacy { matched: false }`
even though it matched — does this crate capture the right reply?" The README must
answer: for a multi-report payload the firmware replies once per report; the crate
keeps the LAST reply (the ETX-report reply); a pre-send drain prevents stale
replies from a prior command contaminating the capture.

**Pain Points Addressed**: (1) The README contradicts the fixed code, undermining
trust in the docs and the fix. (2) Without the pre-send-drain note, a reader
re-introducing rapid sequential sends (or a future refactor) could re-introduce
Issue 3's nondeterminism with no documented warning.

## Why

- **PRD §h2.1/§h3.0 (Issue 1)** + **§h2.2/§h3.2 (Issue 3)** define the bugs this
  bullet must now describe as *fixed*. Issue 1: the firmware emits one 32-byte
  reply **per report**; the old code captured the FIRST (intermediate, `0`) reply
  and discarded the real ETX-report result. Issue 3: no pre-send drain let a prior
  send's late reply contaminate the next capture.
- **The fix code is merged** (P1.M1.T2 capture-last, P1.M1.T3 pre-send drain —
  both Complete per plan_status), so the README must match the actual `core.rs`
  behavior, not the superseded design.
- **Item DOCS = Mode B** — this IS the documentation-sync task for the bugfix
  changeset. No source/Cargo work is in scope here (that was M1/M2).
- **Scope discipline**: the item is explicit — *"Do NOT create new sections — only
  update existing text to match the fixed behavior."* The README was already
  rewritten for v0.3.0 (typed commands, `CommandResponse`, `run()` return type
  are all already documented correctly); only the reply-capture mechanism bullet
  remained stale.

## What

### The ONE substantive edit — README.md:160-163 → accurate bullet
Rewrite the "Reply parsing" bullet (Technical Details) so it describes the
implemented v0.3.1 behavior instead of the buggy v0.3.0 capture-first behavior.
The new bullet must cover:
- **(a) capture-last**: the firmware emits one 32-byte reply per report processed;
  the crate reads up to `batch_count` replies and retains the **last** non-empty
  one (the ETX-report reply), which carries the real result.
- **(b) pre-send drain**: a non-blocking IN-buffer drain **before** each send
  flushes stale replies left by a prior command so they cannot contaminate the
  capture.
- **(preserve) disambiguation**: `response[0] == 0x51` ⇒ typed reply;
  `0`/`1` ⇒ legacy match-bool; no reply ⇒ `Timeout`.

### Verified NO-OP clauses (do NOT edit — documented below so you don't hunt)
- **(c) `parse_cli_args`**: `grep "parse_cli_args" README.md` returns **zero**
  matches. The README §Programmatic Usage documents `run()` only (already returns
  `Result<CommandResponse, QmkError>`). The item clause is conditional ("if
  parse_cli_args is mentioned or its signature is shown") — the condition is FALSE
  ⇒ no-op. (`parse_cli_args`'s real signature `-> Result<RunParameters, QmkError>`
  lives in lib.rs:353; it's just never shown in the README.)
- **(d) Overview L10** "plus reply parsing": a high-level capability claim that
  makes no single-vs-multi-report mechanism assertion ⇒ not stale ⇒ no change.
- **CLI table (L71-86)**: already omits `-c`/`--create-config` (Issue 5 no-op).
- **SIGPIPE/unsafe/libc**: README has **zero** such mentions (grep-proven) ⇒
  Issue 4 (the parallel P1.M2.T3.S1 libc task) is a no-op for the README.
- **"Typed-command transport (v0.3.0)" bullet**: typed-command FRAMING is a v0.3.0
  feature (the bugfix changed reply CAPTURE, not framing) ⇒ stays v0.3.0 ⇒ no change.

### Success Criteria
- [ ] README.md:160-163 buggy text ("one 32-byte IN report is read with a bounded
      timeout") is GONE (`grep` returns zero matches).
- [ ] The rewritten bullet states the crate reads up to `batch_count` replies and
      retains the **last** (ETX-report) reply, AND mentions a pre-send IN-buffer
      drain that flushes stale replies.
- [ ] The `0x51`/`0`/`1`/`Timeout` disambiguation is preserved.
- [ ] The bullet list still renders with the same 2-space continuation indent as
      its siblings (no broken Markdown).
- [ ] ONLY `README.md` modified; no source/Cargo.toml/PRD change; Cargo.toml stays
      `0.3.0` (version bump is OUT OF SCOPE).
- [ ] `cargo build` + `cargo test` still pass (sanity — README cannot break them,
      but confirms no accidental source edit).

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact current buggy text
> (README.md:160-163, awk-verified with line numbers), the exact designed
> replacement bullet, the actual implemented behavior (read directly from the
> merged `core.rs` capture loop — not just the design doc), the verbatim target
> wording the item gives for (a) and (b), the Markdown style to match (2-space
> continuation indent + `- **bold:**` shape of sibling bullets), AND the four
> verified no-op clauses (so the implementer doesn't waste time hunting for
> non-existent edits) are all given below and in research `notes.md`. The
> validation greps are confirmed against the live README.

### Documentation & References

```yaml
# MUST READ — the ONLY file being edited
- file: README.md
  why: "Holds the stale 'Reply parsing' bullet at README.md:160-163 (Technical
        Details section) to be rewritten. ALL other README content is verified
        accurate (Overview L7/L10, CLI table L71-86, Programmatic Usage L108-143,
        the 'Typed-command transport (v0.3.0)' bullet L154-159)."
  pattern: "Technical Details is a `- **bold:** text` bullet list (L149-164) with
            2-space continuation indent (see L150-152, L154-159). The new bullet
            MUST match that shape exactly so the list renders."
  gotcha: "Anchor the edit by the unique bullet text 'after each burst, one 32-byte
           IN report is read with a bounded' — NOT by line number (line numbers
           drift if anything above changes). Do NOT touch any other bullet."

# MUST READ — research notes: verbatim buggy text + designed replacement + no-op proof
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M3T1S1/research/notes.md
  why: "F1 = the exact buggy bytes (L160-163); F2 = parse_cli_args grep-proof
        (no-op); F3 = Overview no-op rationale; F4 = CLI-table no-op; F5 = zero
        SIGPIPE mentions; F6 = the ACTUAL implemented behavior read from core.rs
        (capture loop `for _ in 0..batch_count.max(1)` + overwrite `reply`);
        F7 = designed replacement bullet; F8 = scope boundary; F9 = validation."

# MUST READ — the bug definitions this bullet must now describe-as-fixed
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 1 (§h3.0): firmware emits one 32-byte reply PER REPORT; only the
        LAST (ETX-report) reply carries the result; old code captured FIRST.
        Issue 3 (§h3.2): no pre-send drain ⇒ stale replies contaminated capture.
        These are the behaviors the new bullet must document accurately."
  section: "Issue 1 (Critical)", "Issue 3 (Major)"

# REFERENCE — the design doc (capture-last + pre-send-drain rationale + edge table)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§Fix Architecture Step 3 (pre-send drain) + Step 4 (capture-last loop) +
        §Edge Cases table give the precise semantics. The README prose is a
        plain-English distillation of this."

# REFERENCE — confirm the README prose matches the actual merged code
- file: src/core.rs
  why: "burst_to_one (core.rs:355) — the pre-send drain (core.rs:365-374), the
        capture-last loop `for _ in 0..batch_count.max(1)` with
        `reply = Some(read_buf[..n].to_vec())` overwrite (core.rs:400-425), and
        the surplus drain (core.rs:436-444). The README bullet MUST match this."
  section: "fn burst_to_one (lines 355-446)"

# REFERENCE — the PRD framing (the invariants the docs must honor)
- file: PRD.md
  why: "§4.4 (one reply per report), §8 (response[0] disambiguation), §14 inv 6.
        These are the source-of-truth semantics the rewritten bullet reflects."
  section: "4.4 Replies are received", "8. Response Handling", "14 (6)"
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # version = "0.3.0"  — UNCHANGED (version bump out of scope)
├── Cargo.lock
├── README.md           # <-- FILE TO EDIT (Technical Details "Reply parsing" bullet, L160-163)
├── PRD.md              # READ-ONLY reference
├── .gitignore
└── src
    ├── main.rs         # DO NOT TOUCH (parallel P1.M2.T3.S1 may edit it)
    ├── lib.rs          # DO NOT TOUCH (parse_cli_args already fixed; README doesn't show it)
    ├── error.rs        # DO NOT TOUCH
    └── core.rs         # DO NOT TOUCH (capture-last + drain already merged; reference only)
```

### Desired Codebase tree with files to be modified

```bash
README.md   # MODIFIED ONLY:
            #   rewrite the "Reply parsing" bullet (Technical Details, L160-163)
            #   → "Reply capture & parsing (v0.3.1)" bullet describing capture-last
            #     + pre-send drain, keeping the 0x51/0/1/Timeout disambiguation.
# (Cargo.toml, src/*, PRD.md unchanged; NO new files)
```

> No new files. One file modified (`README.md`). No new tests, no source changes.

### Known Gotchas of our codebase & Library Quirks

```markdown
<!-- CRITICAL (the ONLY stale text is the reply bullet; do NOT over-edit): a full
     grep sweep of README.md for burst/reply/capture/drain/timeout/IN report/v0.3
     found exactly ONE stale mechanism description — the "Reply parsing" bullet
     (L160-163). Every other mention is either accurate (Programmatic Usage run()
     return type; 32-byte/30-payload framing; typed-command framing) or a
     capability-level claim not making a mechanism assertion (Overview L10 "plus
     reply parsing"). Edit ONLY the reply bullet. -->

<!-- CRITICAL (item clauses (c) parse_cli_args and (d) Overview are NO-OPS):
     `grep "parse_cli_args" README.md` → zero matches (verified). The README never
     showed parse_cli_args, so there is nothing to update for Issue 2. The Overview
     "plus reply parsing" is a capability claim, not a mechanism claim — not stale.
     Document these as deliberate no-ops; do NOT fabricate edits to "address" them. -->

<!-- GOTCHA (anchor by TEXT, not line number): line numbers drift if anything above
     the bullet changes (e.g. the parallel P1.M2.T3.S1 task does NOT touch README,
     but be safe). Anchor the edit on the unique substring 'after each burst, one
     32-byte IN report is read with a bounded' and replace that whole 4-line bullet. -->

<!-- GOTCHA (Markdown continuation indent = 2 spaces): the Technical Details bullets
     use 2-space continuation indent (see L150-152, L154-159). The rewritten bullet
     MUST use 2-space indent for every continuation line, or the list breaks /
     renders nested. Match the sibling `- **bold:** ...` shape exactly. -->

<!-- GOTCHA (the "(v0.3.1)" label vs Cargo.toml "0.3.0"): Cargo.toml is STILL at
     `version = "0.3.0"` (verified). The "(v0.3.1)" label in the new bullet marks
     the BUGFIX MILESTONE that introduced capture-last + pre-send drain — consistent
     with the sibling "Typed-command transport (v0.3.0)" label. Version-bumping
     Cargo.toml to 0.3.1 is OUT OF SCOPE for this docs task (no version-bump task
     exists in the bugfix plan). Do NOT bump Cargo.toml. The label is prose about
     the behavior's origin, not a Cargo version claim. -->

<!-- GOTCHA (do NOT create new README sections): the item says "Do NOT create new
     sections — only update existing text." Keep the change to the existing bullet
     within the existing Technical Details list. Do NOT add a "Reply capture"
     heading, a sub-section, or a new bullet; rewrite the existing bullet in place. -->

<!-- NOTE (README cannot affect cargo): cargo build/test are SANITY gates only —
     they confirm you didn't accidentally edit a source file. A passing build does
     NOT validate the Markdown. Use the grep gates (Level 2) + the code cross-check
     (Level 4) for the real validation. -->

<!-- NOTE (do NOT add SIGPIPE/unsafe/parse_cli_args/-c docs): README has zero of
     these today (grep-proven). They are out of scope — Issue 4 lives in main.rs's
     code comment (P1.M2.T3.S1), Issue 2's parse_cli_args isn't surfaced in the
     README by design, Issue 5's --create-config is intentionally undocumented. -->
```

## Implementation Blueprint

### Data models and structure
None — this is a Markdown documentation edit. No types, no code, no config.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: EDIT README.md — rewrite the "Reply parsing" bullet (Technical Details)
  - LOCATE: the bullet beginning `- **Reply parsing:** after each burst, one 32-byte
          IN report is read with a bounded` (README.md:160-163). Anchor by this
          UNIQUE text, not the line number.
  - REPLACE the entire 4-line bullet (L160-163) with the new bullet VERBATIM from
          "Implementation Patterns" §A below:
            * heading: `- **Reply capture & parsing (v0.3.1):**`
            * (a) capture-last: "reads up to `batch_count` replies and retains the
              **last** non-empty one — the reply to the ETX-terminating report"
            * (b) pre-send drain: "A non-blocking IN-buffer drain **before** each
              send first flushes any stale replies left by a prior command"
            * (preserve) disambiguation: 0x51 ⇒ typed; 0/1 ⇒ legacy; no reply ⇒ Timeout
  - STYLE: 2-space continuation indent on every wrapped line (match sibling bullets
          L150-152, L154-159); keep the `- **bold:** ...` shape.
  - DO NOT: touch any other bullet, add a new section/heading, edit source, edit
          Cargo.toml, or change the "Typed-command transport (v0.3.0)" bullet.

Task 2: VALIDATE (the real gates for a docs task)
  - RUN (Level 2 greps): confirm buggy text GONE + new text PRESENT + no other
          stale "after each burst, one" / capture-first wording remains.
  - RUN (Level 1): eyeball the rendered bullet list — 2-space indent, list intact.
  - RUN (Level 3 sanity): `cargo build` + `cargo test` still pass (README cannot
          affect them; this only proves no accidental source edit).
  - RUN (Level 4 cross-check): the README prose "reads up to batch_count replies
          and retains the last" matches core.rs `for _ in 0..batch_count.max(1)`
          + `reply = Some(read_buf[..n].to_vec())` overwrite; the pre-send-drain
          mention matches core.rs:365-374.
```

### Implementation Patterns & Key Details

#### §A — the replacement bullet (copy-paste ready; replaces README.md:160-163)

```markdown
- **Reply capture & parsing (v0.3.1):** the firmware emits one 32-byte reply per
  report processed, so after each burst-write the crate reads up to `batch_count`
  replies and retains the **last** non-empty one — the reply to the ETX-terminating
  report, which carries the real result. (A non-blocking IN-buffer drain **before**
  each send first flushes any stale replies left by a prior command, so they cannot
  contaminate the capture.) `response[0] == 0x51` ⇒ typed reply (decoded by the
  command-echo byte into `Info`/`CallbackName`/`Ack`); `0`/`1` ⇒ legacy match-bool;
  no reply ⇒ `Timeout` (a non-capable/offline device — the caller stays in
  string-only mode)
```

#### §B — BEFORE / AFTER (exact, for a clean find-and-replace)

**BEFORE** (README.md:160-163, the stale/buggy text):
```markdown
- **Reply parsing:** after each burst, one 32-byte IN report is read with a bounded
  timeout. `response[0] == 0x51` ⇒ typed reply (decoded by the command-echo byte
  into `Info`/`CallbackName`/`Ack`); `0`/`1` ⇒ legacy match-bool; no reply ⇒
  `Timeout` (a non-capable/offline device — the caller stays in string-only mode)
```

**AFTER**: the §A bullet above.

**Why each change:**
- `Reply parsing` → `Reply capture & parsing (v0.3.1)`: the capture step is now
  the subject of the bugfix; "parsing" alone undersells it. Still ONE bullet.
- `one 32-byte IN report is read with a bounded timeout` → `reads up to batch_count
  replies and retains the LAST`: the old wording described capture-FIRST (the bug);
  the new wording describes capture-LAST (the fix).
- NEW pre-send-drain sentence: documents Issue 3's fix (stale-reply contamination).
- Disambiguation (`0x51`/`0`/`1`/`Timeout`) preserved verbatim — still accurate.

#### §C — the no-op clauses (proof they need NO edit)

```text
(c) parse_cli_args:    grep -n "parse_cli_args" README.md  →  (no matches)
                       README §Programmatic Usage documents run() only, which
                       already returns Result<CommandResponse, QmkError>.
(d) Overview L10:      "plus reply parsing" = capability claim (no mechanism
                       assertion) → not stale → no change.
CLI table L71-86:      already omits -c / --create-config (Issue 5 no-op).
SIGPIPE/unsafe:        grep -niE "sigpipe|unsafe|libc|signal" README.md → none.
Typed-cmd (v0.3.0):    framing is a v0.3.0 feature; bugfix changed CAPTURE not
                       framing → stays v0.3.0 → no change.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "README.md ONLY (the 'Reply parsing' bullet, Technical Details)."

NO OTHER CHANGES:
  - Cargo.toml:   "UNCHANGED — version stays 0.3.0 (version bump OUT OF SCOPE)."
  - src/*:        "UNCHANGED — capture-last + pre-send drain already merged; reference only."
  - PRD.md:       "READ-ONLY."

DEPENDENCIES / CONFIG:
  - none. No crate, env, or runtime change.

SCOPE BOUNDARY:
  - ONLY README.md is modified, and ONLY the "Reply parsing" bullet. Do NOT:
    * create new README sections/headings/bullets (item: "Do NOT create new sections").
    * edit the Programmatic Usage / Overview / CLI table / "Typed-command (v0.3.0)"
      bullet (all verified accurate or no-op).
    * add SIGPIPE/unsafe/parse_cli_args/-c docs (out of scope; grep-proven absent).
    * bump Cargo.toml (out of scope).
    * touch any source file or PRD.md.
```

## Validation Loop

### Level 1: Markdown Well-Formedness (Immediate Feedback)

```bash
# Eyeball the edited bullet list region — the bullet must use the SAME 2-space
# continuation indent as its siblings and keep the `- **bold:** ...` shape.
sed -n '148,166p' README.md
# Expected: a clean `- ` bullet list; the new bullet's continuation lines are
# indented exactly 2 spaces (matching L150-152, L154-159); no broken/nested list.

# (Optional, if a markdown linter is available — none is configured in this repo.)
# If `mdformat` or similar exists, run it on README.md; otherwise the eyeball +
# grep gates below are the validation.
```

### Level 2: Grep Gates (the real validation for a docs task)

```bash
# (1) THE PRIMARY GATE: the buggy capture-first text is GONE.
grep -n "one 32-byte IN report is read with a bounded" README.md && echo "FAIL: stale text still present" || echo "PASS: stale text removed"
# Expected: PASS (zero matches).

# (2) THE PRIMARY GATE: the new capture-last wording is PRESENT.
grep -n "retains the \*\*last\*\*" README.md
# Expected: exactly ONE match (the new bullet).

# (3) THE PRIMARY GATE: the pre-send-drain note is PRESENT.
grep -n "IN-buffer drain \*\*before\*\* each send" README.md
# Expected: exactly ONE match.

# (4) Sweep: NO other stale capture-first / "after each burst, one" wording anywhere.
grep -niE "after each burst, one|capture.*first|one 32-byte IN report is read" README.md && echo "FAIL: more stale wording" || echo "PASS: no other stale wording"
# Expected: PASS (zero matches).

# (5) The 0x51/0/1/Timeout disambiguation is PRESERVED in the new bullet.
grep -cE "0x51.*typed reply|legacy match-bool|Timeout.*non-capable" README.md
# Expected: ≥1 (the disambiguation survives the rewrite).

# (6) Heading refined and version-labeled.
grep -n "Reply capture & parsing (v0.3.1)" README.md
# Expected: exactly ONE match.

# (7) No accidental new section/heading was added (item: "Do NOT create new sections").
grep -cE "^#" README.md
# Expected: UNCHANGED from before the edit (count the H2/H3 headings — should be
#   the same as the pre-edit count; a new `##`/`###` would be a scope violation).
```

### Level 3: Sanity Gate (README cannot affect compilation)

```bash
# README.md is not compiled, so this ONLY proves you didn't accidentally edit a
# source file. (The parallel P1.M2.T3.S1 task may edit main.rs/Cargo.toml; that is
# unrelated to this README task.)
cargo build 2>&1 | tail -2
# Expected: "Finished `dev` profile ..." (no error).

cargo test 2>&1 | tail -3
# Expected: "test result: ok. … passed; 0 failed" (whatever the current count is).
#   A failure here is NOT caused by a README edit — investigate whether a source
#   file was touched by mistake (git status / git diff --stat).
```

### Level 4: Cross-Check the README Prose Against the Actual Code

```bash
# (1) The README "reads up to batch_count replies and retains the last" must match
#     the actual capture loop in core.rs.
grep -nE "for _ in 0\.\.batch_count.max\(1\)|overwrite .* keep LAST|reply = Some\(read_buf" src/core.rs
# Expected: matches proving the capture-last loop + overwrite semantics the README
#   now describes are the real implemented behavior.

# (2) The README "IN-buffer drain before each send" must match the actual
#     pre-send drain in core.rs.
grep -nE "stale_buf|prior send|pre-send|Drain any stale IN-side replies left by a prior send" src/core.rs
# Expected: matches proving the pre-send drain exists.

# (3) Confirm the README change is the ONLY diff (no stray source edits).
git diff --stat
# Expected: exactly ONE file changed — README.md. If src/* or Cargo.toml appear,
#   STOP — you accidentally edited a source file; revert it.
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1: the rewritten bullet renders with 2-space continuation indent
      matching its siblings; the bullet list is intact.
- [ ] Level 2: `grep "one 32-byte IN report is read with a bounded" README.md` →
      zero matches (buggy text gone).
- [ ] Level 2: new "retains the **last**" + "IN-buffer drain **before** each send"
      each present exactly once.
- [ ] Level 2: no other stale capture-first wording remains (sweep PASS).
- [ ] Level 2: the `0x51`/`0`/`1`/`Timeout` disambiguation is preserved.
- [ ] Level 3: `cargo build` + `cargo test` still pass (sanity; README-only edit).
- [ ] Level 4: `git diff --stat` shows ONLY `README.md` changed.

### Feature Validation
- [ ] The bullet describes capture-LAST (reads up to `batch_count`, retains the
      last = ETX-report reply) — matches core.rs capture loop.
- [ ] The bullet describes the pre-send drain (flushes stale replies from a prior
      command) — matches core.rs:365-374.
- [ ] The bullet notes the real result is the ETX-terminating report's reply.
- [ ] No stale "one report is read" / capture-first description remains anywhere.

### Code Quality Validation
- [ ] ONLY `README.md` modified; one bullet rewritten in place.
- [ ] No new README sections, headings, or bullets added (item constraint honored).
- [ ] The verified no-op clauses ((c) parse_cli_args, (d) Overview, CLI table,
      SIGPIPE, typed-command bullet) were left untouched with documented rationale.
- [ ] Markdown style matches sibling bullets (2-space indent, `- **bold:**` shape).

### Documentation & Deployment
- [ ] The new bullet references v0.3.1 behavior (capture-last + pre-send drain).
- [ ] Cargo.toml stays `0.3.0` (version bump out of scope — documented, not done).
- [ ] No new environment variables, config, or source changes.

---

## Anti-Patterns to Avoid

- ❌ Don't edit anything OTHER than the "Reply parsing" bullet. A full grep sweep
      proved every other README mention is accurate or a non-mechanism capability
      claim. Editing them is scope creep and risks introducing errors.
- ❌ Don't fabricate edits for the no-op clauses. `parse_cli_args` is NOT in the
      README (grep-proven); the Overview "reply parsing" is a capability claim, not
      stale. Document them as deliberate no-ops; don't invent edits to "address" them.
- ❌ Don't create a new README section/heading/bullet. The item explicitly says
      "Do NOT create new sections — only update existing text." Rewrite the existing
      bullet in place within the existing Technical Details list.
- ❌ Don't bump `Cargo.toml` to `0.3.1`. The `(v0.3.1)` label in the bullet is prose
      marking the bugfix milestone (consistent with the `Typed-command (v0.3.0)`
      label). Version-bumping is a separate release concern and is OUT OF SCOPE for
      this docs task (no version-bump task exists in the plan).
- ❌ Don't anchor the edit by line number. Line numbers drift (and the parallel
      P1.M2.T3.S1 task is active, though it doesn't touch README). Anchor on the
      unique substring `after each burst, one 32-byte IN report is read with a
      bounded` and replace that whole 4-line bullet.
- ❌ Don't change the Markdown indent. The Technical Details bullets use 2-space
      continuation indent; mismatching it breaks the rendered list (nested bullets).
- ❌ Don't drop the `0x51`/`0`/`1`/`Timeout` disambiguation. It's still accurate and
      valuable; the rewrite preserves it (only the capture mechanism changes).
- ❌ Don't describe the BUGGY behavior (capture-first) as a "feature" or leave any
      trace of "one report is read". The whole point is the OLD wording is wrong.
- ❌ Don't add SIGPIPE/unsafe/libc, `parse_cli_args`, or `-c`/`--create-config`
      documentation. README has none today (grep-proven); those live in code
      comments / are intentionally undocumented — out of scope.
- ❌ Don't touch `src/*`, `Cargo.toml`, or `PRD.md`. This is a README-only docs task.
      `cargo build`/`cargo test` are sanity gates to CATCH an accidental source
      edit, not to validate the prose.
- ❌ Don't conflate "the bullet is accurate" with "no validation needed." A README
      edit still requires the grep gates (buggy text gone, new text present, no
      other staleness) and the code cross-check (prose matches the merged core.rs).

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a **single Markdown bullet rewrite** with the exact buggy BEFORE text (awk-
verified, README.md:160-163) and the exact designed AFTER bullet both given
verbatim. The actual implemented behavior was read directly from the merged
`core.rs` capture loop (`for _ in 0..batch_count.max(1)` + `reply = Some(...)`
overwrite = capture-last) and pre-send drain (core.rs:365-374) — not just the
design doc — so the prose matches real code. Four item clauses were proven to be
**no-ops** by grep (parse_cli_args absent; Overview is a capability claim; CLI
table already correct; zero SIGPIPE mentions), each documented so the implementer
doesn't hunt for non-existent edits. The Markdown style to match (2-space
continuation indent + `- **bold:**` shape) is quoted from sibling bullets. The
Cargo.toml-stays-0.3.0 / version-bump-out-of-scope boundary is stated explicitly
to prevent scope creep. Validation is grep-driven (buggy text gone, new text
present, no other staleness, disambiguation preserved) plus a cargo sanity gate to
catch any accidental source edit. The only residual risk — a Markdown indentation
mistake breaking the rendered list — is eliminated by quoting the exact 2-space-
indented replacement and a Level-1 eyeball check.