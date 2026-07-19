name: "P1.M4.T3.S1 — Update README.md with v0.3.0 API surface and typed-command documentation"
description: "README.md-only change. The README is already ~90% v0.3.0 (P1.M4.T1.S1, commit 99ec8a8, already added the --query-info/--list-callbacks CLI rows and a v0.3.0 Programmatic Usage example). This item completes the remaining four targeted edits: (a/c) replace the stale 'round B' forward-reference note in the Overview with present-tense v0.3.0 typed-command description; (b) align the Programmatic Usage example to showcase the typed-command path (QueryInfo) per the contract example; (e) correct the misleading 'Default Reference Values' section so VID/PID read as auto-discovery-defaults (None), with 0xFEED/0x0000 demoted to reference values; (f) add typed-command framing + reply parsing to the Technical Details section. Point (d) CLI flags are ALREADY DONE — verify only. NO source edits; NO Cargo.toml edit (P1.M4.T2.S1 owns it concurrently); NO git tag."

---

## Goal

**Feature Goal**: Ship a README.md that fully and accurately documents the **final
v0.3.0 API surface** as a self-contained overview of the whole delta. Every
remaining trace of v0.2.1-only framing (forward-reference notes, misleading
default-value wording, missing typed-command technical detail) is replaced with
correct, present-tense v0.3.0 documentation.

**Deliverable**: One modified file, nothing else: **`README.md`** — four targeted
section edits (Overview, Programmatic Usage, Default Reference Values, Technical
Details). The CLI options table (point d) is already correct and is verified, not
re-edited.

**Success Definition**: `grep` confirms the forward-reference "round B" string is
gone; the Overview describes typed-command transport in present tense; the Default
Reference Values section states VID/PID default to `None`/auto-discovery; the
Technical Details section contains the typed-command framing byte sequence
`0x81 0x9F 0xF0` and the reply-parsing disambiguation (`0x51` typed vs `0`/`1`
legacy). `git status` shows ONLY `README.md` modified by this item (P1.M4.T2.S1's
Cargo.toml/Cargo.lock changes are its own concurrent work and must be left alone).

## User Persona (if applicable)

**Target User**: Two audiences, both reading README as the entry point:
1. **Downstream consumer (`qmkonnect`)** — a Rust dev linking this crate as a
   git-tagged dep, who needs the public API surface (`RunCommand`, `HostOs`,
   `CommandResponse`, `RunParameters`, `run`) documented accurately to write
   correct calls.
2. **CLI user / tinkerer** — someone who clones the repo, builds the binary, and
   runs `qmk_notifier` to talk to a QMK keyboard. Needs the CLI flags table and
   the zero-config auto-discovery story to be unambiguous.

**Use Case**: A `qmkonnect` contributor opens README to confirm the `run()` return
type and the `CommandResponse` variants before wiring the typed-command handshake.
A tinkerer reads README to discover they can run `qmk_notifier --query-info` to
probe a typed-capable board without specifying VID/PID.

**User Journey**: README → Overview (what the crate is + ecosystem) → Installation
→ Usage (CLI examples) → Command Line Options (table) → Default Reference Values
(what the numbers mean) → Programmatic Usage (Rust example) → Technical Details
(wire framing). This item touches Overview, Programmatic Usage, Default Reference
Values, Technical Details — the sections that carry v0.3.0 technical content.

**Pain Points Addressed**: The current README still says "round B adds …" (a
future-tense note about work that has ALREADY shipped), lists VID/PID as if they
are required defaults (they are actually `None`/auto), and omits the typed-command
framing entirely. A reader following it today would misunderstand both the
zero-config path and the typed-command transport.

## Why

- **PRD §13 (Versioning)**: the spec pins the release to `tag = "v0.3.0"`; README
  is the user-facing overview that must match the shipped API, not describe it as
  future work.
- **Work-item contract**: this is the explicit **[Mode B] changeset-level
  documentation task** — "README.md is the overview doc that spans the whole
  delta." All implementing subtasks (M1 type contracts, M2 framing/parser, M3
  transport reply capture, M4.T1 CLI flags) are COMPLETE; the API surface is final
  and this item documents it.
- **Correctness hazard**: the "Default Reference Values" section currently implies
  VID/PID must be provided, contradicting the keystone zero-config auto-discovery
  feature (PRD §1.1, §14 invariant 3, and the CLI help strings which already say
  `[default: auto (match any)]`). A docs/implementation mismatch is a real bug.
- **Ecosystem coherence** (PRD §1.2): README is the first doc a `qmkonnect`
  contributor reads; it must correctly describe the typed-command transport that
  `qmkonnect` will consume, so the reader understands the three-part split.

## What

Four targeted edits to `README.md` (the other sections — Installation,
Dependencies, Usage examples, CLI table, Integration, Use Cases, Why Rust, Related
Projects, Contributing, License — are already accurate and are NOT touched):

1. **Overview bullet** (point a/c): rewrite the `(round B adds typed-command
   transport + reply parsing)` parenthetical into present-tense v0.3.0 prose
   naming the typed commands alongside the legacy string path.
2. **Programmatic Usage** (point b): the existing example already returns
   `CommandResponse`; align it so the HEADLINE example showcases the typed-command
   path (`RunCommand::QueryInfo` → `CommandResponse::Info`) per the contract's
   required example, while keeping the `Timeout`/`Err` arms.
3. **Default Reference Values** (point e): rewrite so VID/PID read as
   "auto-discovery (`None`) — omit to match any"; demote `0xFEED`/`0x0000` to
   reference/typical values (matching `DEFAULT_VENDOR_ID`/`DEFAULT_PRODUCT_ID`),
   not the matching default. Keep usage page/usage as the actual always-required
   defaults.
4. **Technical Details** (point f): add bullets for typed-command framing
   `[0x81][0x9F][0xF0][cmd][args][0x03]` and reply parsing (`0x51` typed vs
   `0`/`1` legacy vs no-reply `Timeout`).

**Point (d) — CLI options table**: the `--query-info` and `--list-callbacks` rows
are ALREADY present (added by P1.M4.T1.S1). VERIFY they exist; do NOT re-add
(would create duplicate rows).

### Success Criteria

- [ ] `grep -ni "round b" README.md` → **no matches** (forward-reference note removed).
- [ ] Overview names typed-command transport in present tense (QueryInfo /
      QueryCallback / SetOs / ApplyHostContext) alongside the legacy string path.
- [ ] Programmatic Usage headline example uses a typed command (`RunCommand::QueryInfo`)
      and imports `HostOs`, matching the contract example; `CommandResponse` arms
      (Info / Timeout / Err at minimum) are correct.
- [ ] Default Reference Values states VID/PID default to `None` (auto-discovery);
      `0xFEED`/`0x0000` are labeled as reference/typical values, NOT required defaults.
- [ ] Technical Details contains the framing byte sequence `0x81 0x9F 0xF0` (or
      `[0x81][0x9F][0xF0]`) AND mentions the `0x51` typed-vs-`0`/`1`-legacy reply
      disambiguation.
- [ ] CLI options table still has exactly ONE `--query-info` row and ONE
      `--list-callbacks` row (verified, not duplicated).
- [ ] `git status --porcelain` shows `README.md` modified by this item; no
      `src/*.rs`, no `Cargo.toml`/`Cargo.lock` change attributable to this item.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything needed
> to implement this successfully?"_ — **Yes.** The current README is reproduced in
> the implementer's working tree (single file, ~130 lines); the exact four sections
> to edit are named with their current text quoted in the Implementation Tasks.
> The technical facts (framing bytes, reply markers, CLI defaults) are verified
> against `src/core.rs` constants and the canonical
> `firmware_wire_contract.md` and quoted verbatim below — the implementer does not
> need to re-derive them. The stale-vs-actual README state (P1.M4.T1.S1 already
> did points b/d) is documented in `research/notes.md` F1/F2 so the implementer
> does not redo finished work. No code understanding beyond "which API types
> exist" is required — and those are listed in F-quick-ref below.

### Documentation & References

```yaml
# MUST READ — the ONLY file edited by this item (current HEAD copy, ~130 lines)
- file: README.md
  why: "The entire edit surface. Current state is already ~90% v0.3.0 (P1.M4.T1.S1).
        The four remaining sections to edit are quoted verbatim in Implementation Tasks."
  pattern: "User-facing Markdown README: Overview → Installation → Usage → CLI table →
            Default Reference Values → Programmatic Usage → Technical Details → … → License.
            Conversational first-person voice ('I built', 'I chose Rust') is the house style —
            match it in any prose you add."
  gotcha: "Do NOT rewrite from scratch. Make TARGETED edits to the four sections marked
           LEFT in research/notes.md F2. Wholesale rewrite risks regressing the already-correct
           CLI table and Programmatic Usage match arms."

# MUST READ — the public API surface (to document Programmatic Usage accurately)
- file: src/lib.rs
  why: "Defines RunCommand (SendMessage/ListDevices/QueryInfo/QueryCallback/SetOs/
        ApplyHostContext), HostOs (Unsure/Linux/Windows/Macos/Ios), CommandResponse
        (Legacy/Info/CallbackName/Ack/Timeout), RunParameters, run() -> Result<CommandResponse,
        QmkError>. The Programmatic Usage example MUST match these signatures exactly."
  pattern: "The contract's required example: `use qmk_notifier::{RunParameters, RunCommand,
            HostOs, run};` then `RunCommand::QueryInfo` + match on `CommandResponse::Info` /
            `CommandResponse::Timeout` / `Err`. lib.rs L1-L160 is the authoritative enum/struct
            source."
  gotcha: "RunParameters::new takes (command, vendor_id: Option<u16>, product_id: Option<u16>,
           usage_page: u16, usage: u16, verbose: bool) IN THAT ORDER. The contract example passes
           `RunCommand::QueryInfo, None, None, 0xFF60, 0x61, false` — that is CORRECT and must
           not be 'corrected' to Some(0xFEED). None = auto-discovery is the keystone path."

# MUST READ — framing constants + reply parsing (to document Technical Details accurately)
- file: src/core.rs
  why: "Holds the byte-level truth: DEFAULT_VENDOR_ID=0xFEED, DEFAULT_PRODUCT_ID=0x0000,
        DEFAULT_USAGE_PAGE=0xFF60, DEFAULT_USAGE=0x61, REPORT_LENGTH=32 (L6-L10);
        CMD_DISCRIMINATOR=0xF0, RESPONSE_MARKER=0x51, CMD_QUERY_INFO=0x01,
        CMD_QUERY_CALLBACK=0x02, CMD_SET_OS=0x03, CMD_APPLY_HOST_CONTEXT=0x05 (L27-L34);
        build_typed_payload emits [0xF0][cmd][args][0x03] (L416+); parse_reply
        disambiguates response[0]: 0x51 typed / 0|1 legacy / else Timeout (L483+)."
  pattern: "Technical Details bullets must cite: 33-byte hidapi report = [0x00][0x81][0x9F][30
            payload]; typed payload = [0xF0][cmd][args][0x03]; reply disambiguation by
            response[0] (0x51 vs 0/1 vs no-reply)."
  gotcha: "These consts are pub(crate) (internal transport), NOT public API — do NOT tell README
           readers to import them. Document them as 'internal framing' / wire-level facts, not as
           things to call. The PUBLIC constants are DEFAULT_VENDOR_ID/DEFAULT_PRODUCT_ID/
           DEFAULT_USAGE_PAGE/DEFAULT_USAGE/REPORT_LENGTH (re-exported from lib.rs)."

# MUST READ — canonical wire contract (byte layouts, command table)
- docfile: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "The byte-level source of truth (PRD §10 mirrors it). §Typed-Command Framing has the
        annotated 33-byte report diagram; §Command Table has cmd_id 0x01/0x02/0x03/0x05;
        §Reply Disambiguation has the response[0] table (0x51/0/1/no-reply). Cite this for
        any byte sequence you put in Technical Details."
  section: "Typed-Command Framing", "Command Table", "Reply Disambiguation"

# REFERENCE — research notes (stale-vs-actual README map + scope boundaries)
- docfile: plan/001_b92a9b2b603f/P1M4T3S1/research/notes.md
  why: "F1 = the CRITICAL finding that the item description is stale (README already 90% v0.3.0
        via P1.M4.T1.S1); F2 = section-by-section DONE/LEFT map for points a-f; F3 = exact
        technical facts; F4 = Default Reference Values correction; F5 = parallel boundary with
        P1.M4.T2.S1; F6 = scope boundaries; F7 = validation approach for a docs-only change."

# REFERENCE — PRD sections this item implements (the API surface being documented)
- file: PRD.md
  why: "§3 Public API (enums/signatures), §10 Typed-Command Transport (framing + reply parsing),
        §11 CLI (the flags table), §14 Key Invariants. README must be consistent with these."
  section: "3. Public API", "10. Typed-Command Transport", "11. CLI", "14. Key Invariants"

# REFERENCE — concurrent sibling item (owns Cargo.toml/Cargo.lock, NOT README)
- docfile: plan/001_b92a9b2b603f/P1M4T2S1/PRP.md
  why: "P1.M4.T2.S1 runs concurrently and edits Cargo.toml + Cargo.lock ONLY. Its PRP explicitly
        says 'DO NOT edit README.md'. Therefore this item owns README.md with zero merge-conflict
        risk. Confirm git status later may show BOTH README.md (this item) and Cargo.toml/Cargo.lock
        (P1.M4.T2.S1) — that is expected, not a conflict."
  section: "Integration Points", "PARALLEL BOUNDARY"

# REFERENCE — already-complete sibling that last touched README (for context only)
- docfile: plan/001_b92a9b2b603f/P1M4T1S1/PRP.md
  why: "P1.M4.T1.S1 (commit 99ec8a8, COMPLETE) added the --query-info/--list-callbacks CLI rows
        and rewrote Programmatic Usage to v0.3.0. Its README edits are merged at HEAD — this item
        starts from that state. Confirms point (d) is DONE and point (b) is mostly done."
  section: "Goal", "Implementation Tasks (README section)"
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # version="0.2.1" (P1.M4.T2.S1 bumps to 0.3.0 concurrently — NOT this item).
├── Cargo.lock          # P1.M4.T2.S1's territory — NOT this item.
├── README.md           # <-- THIS ITEM EDITS THIS FILE (four targeted sections).
├── PRD.md              # READ-ONLY.
└── src/
    ├── lib.rs          # public API (RunCommand/HostOs/CommandResponse/RunParameters/run). NOT edited.
    ├── core.rs         # framing consts + build_typed_payload + parse_reply. NOT edited (source of byte facts).
    ├── error.rs        # QmkError. NOT edited.
    └── main.rs         # CLI entry + callback sweep. NOT edited.
```

### Desired Codebase tree with files to be modified

```bash
README.md   # MODIFIED: four targeted section edits (Overview, Programmatic Usage,
            #   Default Reference Values, Technical Details). Point (d) CLI table verified only.
# (No other files touched. No new files.)
```

### Known Gotchas of our codebase & Library Quirks

```markdown
<!-- CRITICAL (ITEM DESCRIPTION IS STALE): the work item says "The current README.md
     documents v0.2.1 behavior" — that is NO LONGER TRUE. P1.M4.T1.S1 (commit 99ec8a8,
     COMPLETE) already (1) added the --query-info/--list-callbacks CLI rows, and
     (2) rewrote Programmatic Usage to return CommandResponse. So points (b) and (d)
     are mostly/fully DONE. This item is the REMAINING targeted edits (a, c, e, f),
     NOT a full rewrite. (research/notes.md F1, F2.) -->

<!-- CRITICAL (DO NOT rewrite README from scratch): making a wholesale rewrite risks
     regressing the already-correct sections (CLI table, Programmatic Usage match arms,
     Usage examples). Use TARGETED edits — change only the four sections marked LEFT.
     Verify point (d) exists; do NOT re-add the CLI rows (would duplicate). -->

<!-- CRITICAL (RunParameters::new arg order): (command, vendor_id: Option<u16>,
     product_id: Option<u16>, usage_page: u16, usage: u16, verbose: bool). The contract's
     required example passes `RunCommand::QueryInfo, None, None, 0xFF60, 0x61, false`.
     That is CORRECT — None for VID/PID is the keystone auto-discovery path. Do NOT
     'fix' it to Some(0xFEED). (research/notes.md F4; PRD §14 invariant 3.) -->

<!-- CRITICAL (DEFAULT_VENDOR_ID/DEFAULT_PRODUCT_ID are NOT the CLI default): they are
     0xFEED/0x0000 reference constants in core.rs, but the CLI and RunParameters default
     VID/PID to None (auto). The README's "Default Reference Values" section currently
     implies 0xFEED/0x0000 are required — that is the bug point (e) fixes. (F4.) -->

<!-- GOTCHA (typed framing consts are pub(crate), NOT public API): CMD_DISCRIMINATOR,
     RESPONSE_MARKER, CMD_QUERY_INFO, etc. are internal to core.rs. README should
     describe them as wire-level facts ("the typed discriminator byte is 0xF0"), NOT
     as things users import. The PUBLIC consts re-exported from lib.rs are
     DEFAULT_VENDOR_ID, DEFAULT_PRODUCT_ID, DEFAULT_USAGE_PAGE, DEFAULT_USAGE,
     REPORT_LENGTH. (core.rs L16-L34 doc comment says these are 'NOT public API'.) -->

<!-- GOTCHA (33-byte hidapi buffer vs 32-byte report): REPORT_LENGTH=32 is the logical
     report; hidapi requires a LEADING 0x00 report-ID byte, so the actual write buffer
     is 33 bytes: [0x00][0x81][0x9F][<30 payload>]. PRD §14 invariant 2. When documenting
     framing, show the full 33-byte form (with 0x00) OR the logical [0x81][0x9F][...]
     form — be explicit about which. The firmware_wire_contract.md diagram shows both. -->

<!-- GOTCHA (house style is first-person): README uses "I built", "I wrote this in Rust",
     "I chose Rust". Match that voice in any prose you ADD (Overview/Technical Details).
     The CLI table and code blocks stay impersonal. -->

<!-- GOTCHA (P1.M4.T2.S1 runs concurrently on Cargo.toml): after your edit, `git status`
     may show `M README.md` (yours) AND `M Cargo.toml` + `M Cargo.lock` (theirs). That
     is EXPECTED and NOT a conflict — disjoint files. Do NOT touch Cargo.toml/Cargo.lock
     even if they appear modified; they are the other item's territory. (F5.) -->
```

## Implementation Blueprint

### Data models and structure

Not applicable — no data models, no code. This is a pure Markdown documentation edit
to a single file (`README.md`).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 0: VERIFY current README state (defensive — confirm the F2 map before editing)
  - RUN: `git log -1 --format="%h %s" -- README.md`
    EXPECT: `99ec8a8 Add CLI flags for typed diagnostics` (P1.M4.T1.S1 — the last edit).
    This confirms the README is already ~90% v0.3.0 and points (b)/(d) are done.
  - RUN: `grep -ni "round b" README.md ; echo "exit=$?"`
    EXPECT: exit 0, one match in the Overview bullet (the forward-reference note to remove).
  - RUN: `grep -nc "query-info" README.md`
    EXPECT: 2 (one in Usage examples, one in CLI table) — confirms point (d) ALREADY DONE.
    If it's 0, point (d) was reverted — STOP and investigate. If >2, a duplicate exists.
  - READ README.md fully (it is ~130 lines) to ground the targeted edits below.

Task 1: EDIT Overview bullet — replace "round B" forward-reference (point a + c)
  - FIND (exact current text in the Overview's "transport layer" bullet):
        - **qmk_notifier** (this crate): Rust library + CLI that owns Raw-HID wire framing, the device cache, and burst-write (round B adds typed-command transport + reply parsing). Transport only — it does no matching.
  - REPLACE the "(round B adds typed-command transport + reply parsing)" clause with
    present-tense v0.3.0 prose naming the typed commands. Suggested replacement text
    (match the first-person/house style of the surrounding bullets):
        - **qmk_notifier** (this crate): Rust library + CLI that owns Raw-HID wire framing, the device cache, burst-write, and the v0.3.0 typed-command transport (`QueryInfo`, `QueryCallback`, `SetOs`, `ApplyHostContext`) alongside the legacy window-string path, plus reply parsing. Transport only — it does no matching.
  - VERIFY after edit: `grep -ni "round b" README.md ; echo "exit=$?"` → exit 1 (no matches).
  - GOTCHA: do NOT delete the whole bullet or change the qmkonnect/qmk-notifier bullets.
    Edit ONLY the "(round B adds …)" clause within the qmk_notifier bullet.

Task 2: EDIT Programmatic Usage — align headline example to the typed-command path (point b)
  - CONTEXT: the existing example already returns CommandResponse and matches on
    Legacy/Info/Timeout/other/Err (it is v0.3.0-correct, added by P1.M4.T1.S1). The
    contract's REQUIRED example showcases the typed-command path specifically:
    ```rust
    use qmk_notifier::{RunParameters, RunCommand, HostOs, run};

    let params = RunParameters::new(
        RunCommand::QueryInfo, None, None, 0xFF60, 0x61, false,
    );
    match run(params) {
        Ok(qmk_notifier::CommandResponse::Info { proto_ver, feature_flags, .. }) => {
            println!("typed-capable: proto {proto_ver}, flags 0x{feature_flags:02X}");
        }
        Ok(qmk_notifier::CommandResponse::Timeout) => { /* legacy / offline device */ }
        Err(e) => eprintln!("Error: {}", e),
    }
    ```
  - ACTION: replace the current code block with the typed-command example above (it
    imports HostOs, uses RunCommand::QueryInfo, and matches Info/Timeout/Err — the
    v0.3.0 headline). You MAY keep a brief second example showing the legacy
    SendMessage path if helpful, but the HEADLINE example must be the typed one.
  - PRESERVE: the prose paragraph after the code block that describes the
    CommandResponse variants (it is accurate: Legacy/Info/CallbackName/Ack/Timeout).
    Verify the variant list still matches src/lib.rs (it does). If you keep the
    "See PRD.md §7 and §10" reference, leave it.
  - GOTCHA: the contract example passes `None, None` for VID/PID — that is the
    auto-discovery path and is CORRECT. Do NOT change to Some(0xFEED).
  - GOTCHA: `run()` returns `Result<CommandResponse, QmkError>` (NOT `Result<(), _>`).
    The example must reflect that. (src/lib.rs `pub fn run`.)

Task 3: EDIT Default Reference Values — fix the misleading VID/PID wording (point e)
  - FIND (exact current text):
        ## Default Reference Values

        These values are commonly used for QMK keyboards but must be provided explicitly:
        - Vendor ID: `0xFEED` (65261 decimal)
        - Product ID: `0x0000` (0 decimal)
        - Usage Page: `0xFF60` (65376 decimal)
        - Usage: `0x61` (97 decimal)
        - Messages are automatically terminated with ETX (End of Text, `0x03`) character
  - REPLACE with a corrected version that makes the None/auto-discovery default explicit.
    Suggested replacement text:
        ## Default Reference Values

        VID/PID are **optional** — omit them for **auto-discovery** (the library and CLI
        default to `None`, matching any device by usage page/usage). The `0xFEED`/`0x0000`
        values below are **reference/typical values** (the `DEFAULT_VENDOR_ID` /
        `DEFAULT_PRODUCT_ID` constants), useful when you need to disambiguate among
        multiple QMK keyboards — they are NOT the matching default.

        - Vendor ID: `0xFEED` (65261 decimal) — *reference value; default is `None` (match any)*
        - Product ID: `0x0000` (0 decimal) — *reference value; default is `None` (match any)*
        - Usage Page: `0xFF60` (65376 decimal) — always required; this is the QMK Raw-HID default
        - Usage: `0x61` (97 decimal) — always required; this is the QMK Raw-HID default
        - Typed-command and legacy-string messages are both terminated with ETX (`0x03`)
  - VERIFY: the corrected prose no longer says VID/PID "must be provided explicitly".
    `grep -ni "must be provided explicitly" README.md ; echo "exit=$?"` → exit 1.
  - GOTCHA: usage_page/usage ARE always required (they default to 0xFF60/0x61 but are
    the primary identifier). Keep them as "always required". Only VID/PID are None-by-default.

Task 4: EDIT Technical Details — add typed-command framing + reply parsing (point f)
  - FIND (exact current text):
        ## Technical Details

        - I wrote this in Rust for maximum performance and reliability
        - Uses the `hidapi` crate for cross-platform HID communication
        - Automatically batches messages larger than the 32-byte report size
        - Includes proper error handling and device detection
        - Configurable for any QMK keyboard with Raw HID support
  - ADD bullets (keep the existing five; append typed-command facts). Match first-person
    house style where natural, but the framing bullets are technical/impersonal:
        ## Technical Details

        - I wrote this in Rust for maximum performance and reliability
        - Uses the `hidapi` crate for cross-platform HID communication
        - Each report is 32 logical bytes (a 33-byte hidapi buffer with a leading `0x00`
          report-ID byte); messages longer than 30 payload bytes are automatically batched
          across multiple reports, terminated by ETX (`0x03`)
        - Includes proper error handling and device detection (zero-config auto-discovery
          by usage page/usage when VID/PID are omitted)
        - **Typed-command transport (v0.3.0):** commands are framed as
          `[0x81][0x9F][0xF0][cmd_id][args…][0x03]` — the `0x81 0x9F` magic header, a `0xF0`
          typed discriminator (which can never begin a real matched string, so legacy
          firmware safely no-matches it), the command byte (`0x01` QueryInfo, `0x02`
          QueryCallback, `0x03` SetOs, `0x05` ApplyHostContext), args, and ETX. Typed
          commands reuse the same multi-report burst-write and device cache as legacy strings
        - **Reply parsing:** after each burst, one 32-byte IN report is read with a bounded
          timeout. `response[0] == 0x51` ⇒ typed reply (decoded by the command-echo byte
          into `Info`/`CallbackName`/`Ack`); `0`/`1` ⇒ legacy match-bool; no reply ⇒
          `Timeout` (a non-capable/offline device — the caller stays in string-only mode)
        - Configurable for any QMK keyboard with Raw HID support
  - VERIFY: `grep -ni "0x81.*0x9F.*0xF0\|0x81, 0x9F, 0xF0\|\[0x81\]" README.md` finds the
    framing sequence; `grep -ni "0x51" README.md` finds the typed-reply marker mention.
  - GOTCHA: keep the framing byte order EXACT — `[0x81][0x9F][0xF0][cmd][args][0x03]`.
    Do NOT write `[0xF0][0x81]...` — 0x81 0x9F is the magic header, 0xF0 is the
    discriminator that comes AFTER it. (firmware_wire_contract.md §Typed-Command Framing.)

Task 5: VERIFY point (d) — CLI options table already has the flags (do NOT re-add)
  - RUN: `grep -n "query-info\|list-callbacks" README.md`
    EXPECT: exactly 4 matches (2 each: Usage section + CLI table). Confirm the CLI table
    has ONE --query-info row and ONE --list-callbacks row.
  - IF a row is missing or duplicated: fix it (add/remove to get exactly one of each).
    (Expected: already correct from P1.M4.T1.S1 — this is a verify step, not an edit.)

Task 6: FINAL VERIFICATION (accuracy + scope)
  - RUN: `grep -ni "round b\|round b (v0.3.0)" README.md ; echo "exit=$?"` → exit 1.
  - RUN: `grep -ni "0\.2\.1" README.md ; echo "exit=$?"` → exit 1 (no stale version refs).
  - RUN: `grep -ni "must be provided explicitly" README.md ; echo "exit=$?"` → exit 1.
  - RUN: `grep -ni "0x81" README.md` → ≥1 match (framing in Technical Details).
  - RUN: `grep -ni "0x51" README.md` → ≥1 match (reply parsing in Technical Details).
  - RUN: `grep -ni "QueryInfo\|SetOs\|ApplyHostContext" README.md` → matches in Overview +
    Programmatic Usage (the typed commands are named).
  - RUN: `git status --porcelain README.md` → ` M README.md`.
  - RUN: `git diff README.md` → review the diff: four sections changed, nothing else.
    (P1.M4.T2.S1's Cargo.toml/Cargo.lock may ALSO appear in `git status --porcelain`
    — that is its concurrent work, leave it alone.)
```

### Implementation Patterns & Key Details

```markdown
<!-- ===== README.md Overview bullet — BEFORE (Task 1) ===== -->
- **qmk_notifier** (this crate): Rust library + CLI that owns Raw-HID wire framing, the device cache, and burst-write (round B adds typed-command transport + reply parsing). Transport only — it does no matching.

<!-- ===== README.md Overview bullet — AFTER (Task 1) ===== -->
- **qmk_notifier** (this crate): Rust library + CLI that owns Raw-HID wire framing, the device cache, burst-write, and the v0.3.0 typed-command transport (`QueryInfo`, `QueryCallback`, `SetOs`, `ApplyHostContext`) alongside the legacy window-string path, plus reply parsing. Transport only — it does no matching.

<!-- ===== Default Reference Values — the ONE conceptual correction (Task 3) ===== -->
The current section treats 0xFEED/0x0000 as required defaults. The CORRECT model:
  - VID/PID: Option<u16>, default None (auto-discovery by usage page/usage).
  - usage_page/usage: always required, default to the QMK Raw-HID 0xFF60/0x61.
  - 0xFEED/0x0000 are the DEFAULT_VENDOR_ID/DEFAULT_PRODUCT_ID reference constants
    (src/core.rs L6-L7) — typical values for disambiguation, NOT the matching default.
The CLI help strings ALREADY say "[default: auto (match any)]" for VID and PID
(src/lib.rs build_cli_command) — the README prose must match that, not contradict it.

<!-- ===== Technical Details — the framing fact that MUST be byte-accurate (Task 4) ===== -->
Full hidapi write buffer (33 bytes): [0x00][0x81][0x9F][<30 payload bytes>]
  - 0x00 = hidapi report-ID leading byte
  - 0x81 0x9F = magic header (PRD §14 invariant 1)
  - 30 payload bytes (PAYLOAD_PER_REPORT = REPORT_LENGTH - 2 = 30)
Typed payload (what build_typed_payload returns, before burst_to_one adds the header):
  [0xF0][cmd_id][args…][0x03]
Firmware-side logical view: [0x81][0x9F][0xF0][cmd_id][args…][0x03]
Reply disambiguation (parse_reply, response[0]):
  0x51 (RESPONSE_MARKER) → typed reply (decode by response[1] cmd echo)
  0 → Legacy { matched: false }; 1 → Legacy { matched: true }
  no reply within 1000ms (REPLY_READ_TIMEOUT_MS) → Timeout
(Source: src/core.rs L6-L10, L27-L34, L416+, L483+; firmware_wire_contract.md.)
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "README.md (four targeted sections: Overview, Programmatic Usage,
             Default Reference Values, Technical Details)."

PUBLIC API SURFACE (documented, not changed):
  - documented: "RunCommand (SendMessage/ListDevices/QueryInfo/QueryCallback/SetOs/
                 ApplyHostContext), HostOs, CommandResponse (Legacy/Info/CallbackName/
                 Ack/Timeout), RunParameters, run() -> Result<CommandResponse, QmkError>,
                 parse_cli_args. All already exist in src/lib.rs — this item DOCUMENTS them,
                 it does not change any code."
  - unchanged: "Every src/*.rs file. Zero code edits."

CONFIG / DATABASE / ROUTES:
  - none.

PARALLEL BOUNDARY (P1.M4.T2.S1 — running concurrently):
  - P1.M4.T2.S1 edits: "Cargo.toml (version 0.2.1 → 0.3.0; remove toml/dirs/serde) +
                         Cargo.lock refresh."
  - This item edits: "README.md ONLY."
  - Disjoint files: "Yes — zero merge-conflict risk. After both land, `git status` may
                      show `M README.md` (this) + `M Cargo.toml` + `M Cargo.lock` (theirs).
                      That is expected. Do NOT touch Cargo.toml/Cargo.lock even if they
                      appear in `git status`."
  - No version-string coupling: "README has no 0.2.1/0.3.0 literal today (verified), so
                                  there is no version-string coordination needed."

SCOPE BOUNDARY (do NOT implement now):
  - Do NOT edit Cargo.toml/Cargo.lock (P1.M4.T2.S1 owns them).
  - Do NOT edit any src/*.rs (all source is stable / P1.M4.T1.S1 is COMPLETE).
  - Do NOT edit PRD.md, any tasks.json, prd_snapshot.md, .gitignore.
  - Do NOT cut the v0.3.0 git tag (separate release step).
  - Do NOT rewrite README from scratch — targeted edits only (F2 map).
  - Do NOT touch the out-of-scope README sections (Installation, Dependencies, Usage
    examples, Integration, Example Use Cases, Why Rust, Related Projects, Contributing,
    License) — they are already accurate.
  - P1.M4.T3.S2 (Final cross-document consistency verification) is a SEPARATE planned
    item — do NOT perform full cross-doc verification here; just make README internally
    consistent and accurate. (If you spot a glaring PRD↔README contradiction, note it
    for P1.M4.T3.S2 rather than editing PRD.md.)
```

## Validation Loop

> README.md is Markdown — there is no compiler, no `cargo test`, no type check for it.
> The gates below are the docs-appropriate equivalents of the template's Level 1–4.

### Level 1: Markdown Sanity (Immediate Feedback)

```bash
# Visual review of the rendered diff — the primary gate for a docs change.
git diff README.md
# Expected: four sections changed (Overview bullet, Programmatic Usage code block,
# Default Reference Values, Technical Details bullets). No other hunks.

# Optional: if a markdown linter is installed, run it (project has none configured,
# so this is best-effort).
command -v markdownlint >/dev/null 2>&1 && markdownlint README.md || echo "no markdownlint (OK)"
# Expected: if installed, 0 errors. If not installed, the echo confirms it's skipped.

# Confirm no broken code-fence balance (a stray ``` breaks GitHub rendering).
grep -c '```' README.md
# Expected: an EVEN number (every opening ``` has a closing ```).
```

### Level 2: Accuracy Greps (Component Validation)

```bash
# Forward-reference note removed (points a, c).
grep -ni "round b" README.md ; echo "exit=$?"
# Expected: exit 1 (no matches).

# No stale v0.2.1 version reference.
grep -ni "0\.2\.1" README.md ; echo "exit=$?"
# Expected: exit 1.

# Misleading "must be provided explicitly" wording removed (point e).
grep -ni "must be provided explicitly" README.md ; echo "exit=$?"
# Expected: exit 1.

# Typed-command framing present in Technical Details (point f).
grep -niE "0x81.{0,4}0x9F|0x81, 0x9F|\[0x81\]\[0x9F\]" README.md
# Expected: ≥1 match (the [0x81][0x9F][0xF0]... framing sequence).

# Typed-reply marker present (point f).
grep -ni "0x51" README.md
# Expected: ≥1 match (reply parsing: 0x51 typed vs 0/1 legacy).

# Typed command names present (points a, b).
grep -niE "QueryInfo|SetOs|ApplyHostContext" README.md
# Expected: ≥1 match in Overview and/or Programmatic Usage.

# Auto-discovery / None default is stated (point e).
grep -niE "auto-discovery|default is .None.|match any" README.md
# Expected: ≥1 match in Default Reference Values (and/or Programmatic Usage/Usage).
```

### Level 3: Scope & Non-Regression (System Validation)

```bash
# This item touched ONLY README.md (P1.M4.T2.S1's Cargo changes are its own — leave them).
git status --porcelain
# Expected (this item's contribution): ` M README.md`.
# NOTE: ` M Cargo.toml` and ` M Cargo.lock` may ALSO appear — those are P1.M4.T2.S1's
# concurrent edits; they are NOT this item's concern and must NOT be reverted.

# Confirm no source file was accidentally edited.
git status --porcelain | grep -E "src/|error\.rs|main\.rs|lib\.rs|core\.rs" ; echo "exit=$?"
# Expected: exit 1 (no src/ changes from this item).

# CLI table integrity: exactly one --query-info and one --list-callbacks row (point d verify).
grep -c "\-\-query-info" README.md
# Expected: 2 (Usage examples + CLI table). Confirm the CLI table has exactly ONE row.
grep -c "\-\-list-callbacks" README.md
# Expected: 2 (Usage examples + CLI table). Confirm the CLI table has exactly ONE row.

# Sanity: the crate still builds (a README edit cannot break it, but confirm nothing
# else drifted while you had the tree open). NOT a requirement — cheap reassurance.
cargo build 2>&1 | tail -2
# Expected: "Finished `dev` profile ..." with 0 warnings. (If this fails, it is NOT due
# to README — investigate P1.M4.T2.S1's concurrent Cargo.toml edit, do not 'fix' it here.)
```

### Level 4: Content Accuracy Review (Domain-Specific Validation)

```bash
# Verify the Programmatic Usage example compiles conceptually against the real API.
# (Cannot run rustdoc on a README code block without extraction, but eyeball-check the
# types against src/lib.rs:)

# Confirm the API types named in README actually exist in the public API.
grep -nE "pub (enum|struct|fn) (RunCommand|HostOs|CommandResponse|RunParameters|run|parse_cli_args)" src/lib.rs
# Expected: matches for each — they are the real public surface.

# Confirm the RunParameters::new signature the example uses is real.
grep -nA6 "pub fn new" src/lib.rs | head -10
# Expected: fn new(command, vendor_id: Option<u16>, product_id: Option<u16>, usage_page: u16,
#          usage: u16, verbose: bool) — matches the example's (RunCommand::QueryInfo, None,
#          None, 0xFF60, 0x61, false).

# Confirm the framing byte sequence in Technical Details matches the canonical contract.
grep -nE "0x81|0x9F|0xF0|0x51|0x03" src/core.rs | head -20
# Expected: 0x81/0x9F in burst_to_one (L306-L307), 0xF0 = CMD_DISCRIMINATOR (L27),
#          0x51 = RESPONSE_MARKER (L29), 0x03 = ETX. Cross-check the README bytes against these.

# Manual content review (the real Level 4 gate for docs): re-read the four edited sections
# in full and confirm:
#   (1) Overview: present tense, names all four typed commands + legacy path.
#   (2) Programmatic Usage: QueryInfo headline example, imports HostOs, matches Info/Timeout/Err.
#   (3) Default Reference Values: VID/PID = None/auto; 0xFEED/0x0000 labeled reference values.
#   (4) Technical Details: [0x81][0x9F][0xF0][cmd][args][0x03] framing + 0x51/0/1 reply parsing.
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1: `git diff README.md` reviewed — four sections changed, code fences balanced.
- [ ] Level 2: all six accuracy greps pass (round-b gone, no 0.2.1, no "must be provided
      explicitly", framing bytes present, 0x51 present, typed command names present).
- [ ] Level 3: `git status --porcelain` shows `M README.md` only (this item); no src/ change.
- [ ] Level 4: README types cross-checked against `src/lib.rs`; framing bytes cross-checked
      against `src/core.rs` + `firmware_wire_contract.md`.

### Feature Validation
- [ ] Point (a): Overview describes typed-command transport in present tense.
- [ ] Point (b): Programmatic Usage headline example uses `RunCommand::QueryInfo` + `HostOs`.
- [ ] Point (c): `grep -ni "round b" README.md` → no matches.
- [ ] Point (d): CLI table has exactly one `--query-info` and one `--list-callbacks` row (verified).
- [ ] Point (e): Default Reference Values states VID/PID default to `None`/auto-discovery.
- [ ] Point (f): Technical Details has the `[0x81][0x9F][0xF0][cmd][args][0x03]` framing AND
      the `0x51`-typed-vs-`0`/`1`-legacy reply disambiguation.

### Code Quality Validation
- [ ] README prose matches house style (first-person "I built/I wrote" where natural).
- [ ] No wholesale rewrite — only the four targeted sections changed.
- [ ] No out-of-scope sections (Installation, Dependencies, Usage examples, Integration,
      Use Cases, Why Rust, Related Projects, Contributing, License) modified.
- [ ] Code blocks are syntactically valid Rust (the Programmatic Usage example).
- [ ] No duplicate CLI rows introduced (point d was verify-only).

### Documentation & Deployment
- [ ] README is internally consistent (no section contradicts another).
- [ ] No Cargo.toml/Cargo.lock change by this item (P1.M4.T2.S1's territory).
- [ ] No `.gitignore` change; no PRD.md / tasks.json / prd_snapshot.md change.
- [ ] No `v0.3.0` git tag created (separate release step).

---

## Anti-Patterns to Avoid

- ❌ Don't trust the stale item description that says "README documents v0.2.1" — it
  doesn't anymore (P1.M4.T1.S1 updated it). Run Task 0's verification greps FIRST.
- ❌ Don't rewrite README from scratch — make targeted edits to the four sections marked
  LEFT in `research/notes.md` F2. Wholesale rewrite regresses already-correct sections.
- ❌ Don't re-add the `--query-info`/`--list-callbacks` CLI rows (point d is DONE) —
  that creates duplicate rows. Verify-only.
- ❌ Don't "correct" the contract's `None, None` VID/PID in the Programmatic Usage
  example to `Some(0xFEED)` — `None` is the keystone auto-discovery path and is correct.
- ❌ Don't document the `pub(crate)` framing constants (CMD_DISCRIMINATOR,
  RESPONSE_MARKER, CMD_*) as if users import them — they are internal transport facts,
  not public API. Describe them as wire-level byte values.
- ❌ Don't get the framing byte order wrong — it is `[0x81][0x9F][0xF0][cmd][args][0x03]`
  (magic header THEN discriminator). Never `[0xF0][0x81]...`.
- ❌ Don't touch Cargo.toml/Cargo.lock even if they appear modified in `git status` —
  P1.M4.T2.S1 owns them concurrently; disjoint files, not a conflict.
- ❌ Don't edit any `src/*.rs` — this is a docs-only item; all source is stable.
- ❌ Don't cut the `v0.3.0` git tag — that is a separate release step.
- ❌ Don't perform full cross-document (PRD↔README) verification — that is
  P1.M4.T3.S2's scope. Just make README internally consistent and accurate here.