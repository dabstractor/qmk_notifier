name: "P1.M4.T3.S2 — Final cross-document consistency verification (8 invariants sign-off)"
description: "Verification-only task (Mode B, the final consistency gate). NO feature code is written. The deliverable is a signed-off verification report confirming all 8 PRD §14 Key Invariants hold in the v0.3.0 codebase, backed by a green `cargo test` and `cargo clippy`. All 8 invariants were PRE-VERIFIED during research (see research/notes.md F1) and currently HOLD; this task re-confirms them against the final tree (the repo is live, so the agent re-runs the greps rather than trusting stale line numbers) and records the sign-off. The ONLY code edits permitted are minor comment-typo fixes (e.g., the `build_command_data` vs `build_typed_payload` naming nit in a lib.rs doc comment) — and only if the agent confirms the nit is real after re-grepping. Any SIGNIFICANT drift must be FLAGGED, not silently fixed."

---

## Goal

**Feature Goal**: Produce a signed-off, evidence-backed verification that all 8
PRD §14 Key Invariants hold in the final v0.3.0 `qmk_notifier` codebase, and that
the crate's three sources of truth — **PRD §14**, the **source code** (`src/`), and
the **canonical wire contract** (`firmware_wire_contract.md`) — are mutually
consistent. This is the closeout gate of the v0.3.0 changeset.

**Deliverable**: Two artifacts:
1. **`plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md`** — a new sign-off report with a
   per-invariant evidence table (file:symbol citations + the actual grep/test
   output), an explicit PASS/FAIL verdict per invariant, a `cargo test` transcript
   excerpt, a `cargo clippy` transcript excerpt, and a signed-off conclusion.
   (Template provided in Implementation Tasks → Task 11.)
2. **A clean `cargo test` + `cargo clippy` run** (green) — captured in the report.

The ONLY permitted source-code edits are **minor comment-typo fixes** explicitly
authorized by the contract ("Fix if minor (typos in comments); flag if
significant"). No logic, no constants, no API changes — those would re-open
invariants this gate is meant to *freeze*.

**Success Definition**: `SIGNOFF.md` exists at the path above; its per-invariant
verdicts are all `PASS` (or any drift is explicitly flagged with severity); the
embedded `cargo test` and `cargo clippy` transcripts are green; `git status` shows
at most `SIGNOFF.md` plus zero-or-one trivial comment-fix in a `src/*.rs` file (and
README.md if S1's parallel edit is still uncommitted — that is S1's, not ours).

## User Persona (if applicable)

**Target User**: The v0.3.0 release approver / maintainer — the person who cuts the
`v0.3.0` git tag and wants an auditable record that the release gate passed before
the tag is pushed to the two downstream consumers (`qmkonnect`, `qmk-notifier`
firmware).

**Use Case**: Before tagging `v0.3.0`, the maintainer opens `SIGNOFF.md`, reads the
8-invariant evidence table, confirms `cargo test`/`clippy` are green, and sees the
signed-off line — then tags. A future contributor touching the transport layer
reads the same file to understand which invariants are load-bearing and why.

**User Journey**: PRP (this doc) → agent runs the 8-invariant sweep → runs
`cargo test` + `cargo clippy` → writes `SIGNOFF.md` with evidence → maintainer
reviews → tag is cut.

**Pain Points Addressed**: v0.3.0 is a large delta (typed-command transport, reply
capture, CLI flags). Without a final consistency gate, a silent drift in any of the
8 invariants (e.g., someone "tidies" the magic header, or re-introduces a partial-
send retry) would break host↔firmware interop with no compile error. This gate
makes drift visible and auditable before the tag.

## Why

- **PRD §14**: the spec explicitly enumerates 8 invariants "a Dev Agent Must
  Preserve." This task is the contractual verification of that section — it is the
  named closeout step ("OUTPUT: Signed-off verification that all 8 key invariants
  hold").
- **Release safety**: `qmkonnect` links this crate as a `git`-tagged dep; the
  firmware repo implements the other end of the wire. A constant-value drift
  (e.g., `RESPONSE_MARKER` becomes `0x52`) would silently break the handshake with
  zero compiler help. The cross-doc check (Invariant 8) catches exactly this.
- **Freeze, don't extend**: this is the LAST task of P1.M4.T3 (documentation). Its
  job is to certify the tree is internally consistent and then STOP — not to add
  features. All prior tasks (M1–M4.T2) are COMPLETE; the API surface is final.

## What

A verification sweep with 10 deterministic checks (invariants 1–8 plus `cargo test`
plus `cargo clippy`), each producing captured evidence, consolidated into one
signed-off `SIGNOFF.md`. Each invariant check is a concrete grep or code-reading
step against the FINAL tree (re-located by symbol, not by line number — the repo is
live, see Known Gotchas). Where a check finds drift: **comment typos ⇒ fix;
anything else ⇒ flag in SIGNOFF.md with severity, do NOT silently rewrite logic.**

### Success Criteria

- [ ] `SIGNOFF.md` exists at `plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md`.
- [ ] All 8 invariants have an explicit `PASS`/`FLAG` verdict with file:symbol evidence.
- [ ] `cargo test` passes (0 failures) — transcript excerpt embedded in SIGNOFF.md.
- [ ] `cargo clippy` passes with **zero warnings** — transcript excerpt embedded.
- [ ] Any drift found is recorded in SIGNOFF.md with a severity label
      (`minor-comment` / `significant`); only `minor-comment` drift was fixed in-tree.
- [ ] `git status` shows no logic/constant/API edits in `src/` (only an optional
      comment-typo fix, if any); `PRD.md`, `tasks.json`, `prd_snapshot.md`,
      `.gitignore` untouched.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything needed
> to implement this successfully?"_ — **Yes.** All 8 invariants are quoted verbatim
> from PRD §14 (in the selected_prd_content). Each is mapped to (a) the exact
> source symbol to grep and (b) the **pre-verified current-state evidence** from
> `research/notes.md` F1 (file:symbol citations + expected grep output), so the
> agent knows what a PASS looks like before it runs anything. The canonical wire
> contract (`firmware_wire_contract.md`) is summarized in F6 with a full
> constant-by-constant cross-check table. The sign-off report TEMPLATE is provided
> verbatim in Task 11. The repo is confirmed to have `cargo 1.92` + `clippy 0.1.92`
> installed (F3) and a hardware-free test suite. No domain assumptions are left to
> the agent's training data.

### Documentation & References

```yaml
# MUST READ — the 8 invariants being verified (authoritative source)
- file: PRD.md
  why: "§14 'Key Invariants a Dev Agent Must Preserve' — the 8 invariants this task
        certifies. §4 (Wire Protocol), §5 (Discovery), §6 (Cache), §7 (Send Path),
        §8 (Response Handling), §10 (Typed-Command Transport) are the backing detail
        for each invariant."
  section: "14. Key Invariants", plus 4/5/6/7/8/10 for backing detail
  gotcha: "PRD.md is READ-ONLY — never edit it, even if you think the spec is wrong.
           Invariant 8 says 'where SPEC and firmware disagree, firmware wins' — so a
           real SPEC↔firmware conflict is REPORTED, and the fix (if any) is to the
           crate, not to PRD.md."

# MUST READ — the source under verification (the whole src/ tree, 4 files)
- file: src/core.rs
  why: "Holds the byte-level truth for invariants 1,2,4,5,6,7,8: the framing consts
        (REPORT_LENGTH, ETX_TERMINATOR_BYTE, CMD_*, RESPONSE_MARKER), burst_to_one
        (magic header + 33-byte buffer + reply capture), send_raw_report/try_send_once
        (cache + MatchKey + partial-never-retry), build_typed_payload (typed framing),
        parse_reply/parse_typed_reply (reply disambiguation), device_matches (None⇒any)."
  pattern: "Transport crate, single core.rs (~700 LoC + ~500 LoC tests). pub(crate) framing
            consts are internal; only DEFAULT_* and REPORT_LENGTH are pub (re-exported
            from lib.rs)."
  gotcha: "The repo is LIVE — line numbers drift. Re-locate every finding by SYMBOL/value
           via grep, never by line number. research/notes.md F1 gives the symbol names."

- file: src/lib.rs
  why: "Public API surface + run() dispatch. Invariant 5 lives here: run() routes ALL
        commands (SendMessage + the 4 typed variants) through ONE path — build_payload →
        send_raw_report → core::parse_reply. Also holds RunCommand/HostOs/CommandResponse
        enums (invariant 6 variant shapes) and parse_cli_args (CLI)."
  pattern: "run() is a 2-arm match: ListDevices (no send) vs everything-else (shared send
            path). build_payload is the thin wrapper: SendMessage appends ETX here; typed
            variants delegate to core::build_typed_payload."
  gotcha: "A lib.rs doc comment (~line 422) references 'core::build_command_data' — that
           is a GENERIC name; the real fn is core::build_typed_payload. This is the known
           minor-comment nit (research/notes.md F2). Do NOT grep for a fn literally named
           build_command_data and conclude invariant 5 fails — that is a false negative."

- file: src/error.rs
  why: "QmkError enum — invariant 4's PartialSendError variant lives here. Confirm it
        still has the { succeeded, failed } shape that send_raw_report returns on a
        partial send (never retried)."
  pattern: "Plain enum + manual Display + std::error::Error impl. No logic."

- file: src/main.rs
  why: "Binary entry point. Relevant to invariant 7 (transport-only): confirm main.rs
        does NOT do window detection / pattern matching / rule evaluation — it only
        parses CLI args, calls run(), and (for --list-callbacks) loops QueryCallback.
        The callback sweep is CLI convenience, NOT rule evaluation."

# MUST READ — the canonical wire contract (invariant 8's other side)
- docfile: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "The byte-level source of truth (mirrors firmware PRD §4.6). Invariant 8 cross-
        checks core.rs consts against its §Constants, §Command Table, §Field Definitions,
        and §Reply Disambiguation. research/notes.md F6 has the full pre-verified
        cross-check table (all match)."
  section: "Typed-Command Framing", "Command Table", "Field Definitions",
           "Reply Disambiguation", "Constants"
  gotcha: "The firmware itself is documented as NOT YET IMPLEMENTED (only legacy path in
           notifier.c). Typed commands timing out against current firmware is the DESIGNED
           fallback — NOT drift. Do not flag it."

# MUST READ — research notes (pre-verified evidence for all 8 invariants)
- docfile: plan/001_b92a9b2b603f/P1M4T3S2/research/notes.md
  why: "F1 = the 8-invariant evidence table (file:symbol + grep command for each, all
        PASS at research time). F2 = the build_command_data naming nit. F3 = toolchain +
        test-suite-is-hardware-free. F4 = parallel-execution README note. F5 = sign-off
        report location decision. F6 = firmware contract cross-check detail."
  section: "F1", "F2", "F3", "F4", "F5", "F6"

# REFERENCE — the dependency PRP (defines the post-S1 README state this task verifies)
- docfile: plan/001_b92a9b2b603f/P1M4T3S1/PRP.md
  why: "S1 (parallel, editing README.md) defines what the post-S1 README contains.
        S2 verifies the post-S1 README is consistent with the code (byte sequences in
        README's Technical Details must match core.rs consts). S2 does NOT re-do S1's
        internal-section edits; it only fixes a byte-sequence typo in README if one
        slipped through (cross-doc consistency is S2's scope)."
  section: "Goal", "Success Criteria", "Implementation Tasks (Task 4 Technical Details)"

# REFERENCE — the v0.3.0 PRD top-level (scope boundaries)
- file: PRD.md
  why: "§13 Versioning pins the release to tag v0.3.0 (this gate precedes the tag).
        §1.2 ecosystem split (transport / firmware / desktop) frames invariant 7."
  section: "1.2", "13"
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # version = "0.3.0" (P1.M4.T2.S1, COMPLETE). NOT edited by this task.
├── Cargo.lock          # P1.M4.T2.S1's territory. NOT edited.
├── README.md           # post-S1 state (S1 parallel, edits 4 sections). Verified, not re-edited (except an optional byte-typo fix).
├── PRD.md              # READ-ONLY.
├── src/
│   ├── lib.rs          # public API + run() dispatch + CLI.        <-- VERIFIED (inv 5,6,7)
│   ├── core.rs         # framing consts + send path + parse_reply. <-- VERIFIED (inv 1,2,4,5,6,8)
│   ├── error.rs        # QmkError (PartialSendError).              <-- VERIFIED (inv 4)
│   └── main.rs         # binary entry + --list-callbacks sweep.    <-- VERIFIED (inv 7)
└── plan/001_b92a9b2b603f/
    ├── architecture/firmware_wire_contract.md  # canonical wire contract (inv 8 other side)
    └── P1M4T3S2/
        ├── PRP.md        # <-- THIS FILE
        ├── research/notes.md
        └── SIGNOFF.md    # <-- CREATED BY THIS TASK (the deliverable)
```

### Desired Codebase tree with files to be added/modified

```bash
plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md   # NEW — the signed-off verification report (deliverable).
# OPTIONAL (only if a real minor comment typo is confirmed by re-grep):
#   src/lib.rs   # one-line doc-comment fix (the build_command_data → build_typed_payload nit), IF confirmed.
# NOTHING ELSE. No logic, no consts, no API, no Cargo.toml, no README body (S1 owns README).
```

### Known Gotchas of our codebase & Library Quirks

```markdown
<!-- CRITICAL (REPO IS LIVE — line numbers drift): the qmk_notifier tree is being
     actively edited by parallel/completed tasks. Between research time and
     implementation, core.rs was refactored to extract `ETX_TERMINATOR_BYTE` (was
     bare `0x03`) and S1 landed README edits. THEREFORE: re-locate EVERY finding
     by SYMBOL or VALUE via grep (the commands are in research/notes.md F1 and in
     each Task below), never by line number. A "not found at line 312" result is
     meaningless; a "not found by grep for `request_data\[1\] = 0x81`" result is a
     REAL invariant failure. -->

<!-- CRITICAL (build_command_data is NOT a real symbol): the item contract (step e)
     says "Verify build_command_data output goes through send_raw_report." But the
     REAL code uses `core::build_typed_payload` (the typed builder) and
     `lib.rs::build_payload` (the thin wrapper). `build_command_data` is the
     contract's GENERIC name. Do NOT grep for a fn named `build_command_data` and
     declare invariant 5 failed — you will get a false negative. Grep for
     `build_typed_payload` (core) and `build_payload` (lib) instead.
     (research/notes.md F2.) -->

<!-- CRITICAL (FREEZE, don't extend): this is the closeout gate. The ONLY permitted
     src/ edit is a minor comment-typo fix. Do NOT "improve" logic, rename consts,
     refactor, or touch the API — that would re-open the very invariants this gate
     freezes, and would require re-running the whole sweep. If you find significant
     drift (a real constant changed, a logic path altered), FLAG it in SIGNOFF.md
     with severity `significant` and STOP — do not fix it here. Significant fixes
     are a new work item, not a verification task. -->

<!-- GOTCHA (firmware NOT YET IMPLEMENTED is not drift): firmware_wire_contract.md
     says the firmware `notifier.c` has no 0xF0 branch yet — only the legacy path.
     Typed commands timing out against current firmware is the DESIGNED fallback
     (Timeout ⇒ string-only mode, PRD §10.2/§8). This is staged rollout, NOT a
     SPEC↔firmware disagreement. Do not flag it under invariant 8.
     (research/notes.md F6.) -->

<!-- GOTCHA (cargo test needs libhidapi): the `hidapi` crate (v2.4.1) links the
     system lib. `target/` exists so deps are installed on this machine. On a clean
     machine install `libhidapi-dev libudev-dev` (Debian/Ubuntu) first, or `cargo
     test` fails at LINK time — that is an environment issue, NOT a code defect.
     The test SUITE itself is hardware-free: dispatch tests use bogus VID/PID ⇒
     deterministic DeviceNotFound; parse/match tests are pure unit tests over byte
     arrays. (research/notes.md F3.) -->

<!-- GOTCHA (clippy must be ZERO warnings, not just no errors): the contract says
     "no warnings." Run `cargo clippy --all-targets -- -D warnings` to treat
     warnings as errors and get a hard pass/fail. If clippy emits warnings, they
     are almost certainly pre-existing (the crate was clippy-clean at v0.2.1 and
     the v0.3.0 tasks ran clippy locally). If a NEW warning appears, fix it ONLY
     if it is a trivial clippy nit (e.g., a needless return) — otherwise flag it. -->

<!-- GOTCHA (README is S1's territory): S1 (parallel) edits README.md's Overview,
     Programmatic Usage, Default Reference Values, Technical Details. S2 verifies
     cross-doc consistency (e.g., README's `[0x81][0x9F][0xF0]...` byte sequence
     matches core.rs). If you find a byte-sequence TYPO in README that S1 missed,
     you MAY fix that one byte-sequence (it's cross-doc consistency, S2's scope).
     Do NOT re-do S1's prose/section edits — that's S1's scope and would conflict. -->
```

## Implementation Blueprint

### Data models and structure

Not applicable — no data models, no new code. This is a verification task. The
single structured artifact is `SIGNOFF.md`, whose template is given in Task 11.

### Verification Tasks (ordered; each produces evidence for SIGNOFF.md)

> **Discipline**: every task below is a READ/grep/test step. Run it, capture the
> output, paste the relevant excerpt into SIGNOFF.md. The pre-verified expected
> result (from research/notes.md F1) is stated for each — if your run matches,
> verdict = PASS; if not, verdict = FLAG (with severity).

```yaml
Task 0: GROUND the sweep — confirm the tree is the final v0.3.0 state
  - RUN: `git log --oneline -5`
    EXPECT: HEAD at or after `bdd3343 Unify payload builder as build_command_data`
    (or a later commit). The implementing tasks P1.M1–P1.M4.T2 are all COMPLETE
    per plan_status; this confirms no half-applied intermediate state.
  - RUN: `grep '^version' Cargo.toml`
    EXPECT: `version = "0.3.0"` (P1.M4.T2.S1, COMPLETE).
  - CAPTURE: the HEAD commit hash + Cargo.toml version line → SIGNOFF.md header.

Task 1: VERIFY Invariant 1 — Magic header 0x81 0x9F; ETX 0x03
  - RUN: `grep -n 'request_data\[1\] = 0x81\|request_data\[2\] = 0x9F' src/core.rs`
    EXPECT: exactly 2 matches in `burst_to_one` (the magic header is set per report).
  - RUN: `grep -n 'ETX_TERMINATOR_BYTE\b' src/core.rs src/lib.rs`
    EXPECT: the const def (`= 0x03`) in core.rs AND its uses in build_typed_payload
    (core.rs) + the SendMessage arm of build_payload (lib.rs). Every payload path
    appends ETX (0x03).
    ALT (if the const was inlined again): `grep -n 'push(0x03)' src/*.rs` — confirm
    both the typed builder and the SendMessage wrapper append ETX.
  - PASS CRITERION: magic header bytes are 0x81/0x9F AND ETX byte is 0x03 on BOTH
    the legacy (SendMessage) and typed paths. (Pre-verified: HOLDS. F1 inv 1.)

Task 2: VERIFY Invariant 2 — REPORT_LENGTH=32; hidapi buffer is 33
  - RUN: `grep -n 'pub const REPORT_LENGTH' src/core.rs`
    EXPECT: `pub const REPORT_LENGTH: usize = 32;`
  - RUN: `grep -n 'REPORT_LENGTH + 1' src/core.rs`
    EXPECT: ≥3 matches (the `[0u8; REPORT_LENGTH + 1]` stack buffer in burst_to_one,
    read_buf, drain_buf) = 33-byte hidapi buffers.
  - RUN: `grep -n 'PAYLOAD_PER_REPORT' src/core.rs`
    EXPECT: `const PAYLOAD_PER_REPORT: usize = REPORT_LENGTH - 2;` (= 30).
  - PASS CRITERION: REPORT_LENGTH==32, all write/read buffers sized REPORT_LENGTH+1,
    payload-per-report==30. (Pre-verified: HOLDS. F1 inv 2.)

Task 3: VERIFY Invariant 3 — VID/PID None ⇒ match any
  - RUN: `grep -n 'is_none_or' src/core.rs`
    EXPECT: 2 matches in `device_matches`: `vendor_id.is_none_or(|v| dev_vendor_id == v)`
    AND `product_id.is_none_or(|p| dev_product_id == p)`.
  - RUN: `grep -n 'fn device_matches' src/core.rs` then read the function body.
    EXPECT: `usage_page` and `usage` compared with strict `==` (always required),
    while VID/PID use `is_none_or` (None ⇒ match any). This is the keystone
    auto-discovery path.
  - PASS CRITERION: VID/PID None ⇒ match any; usage_page/usage always required.
    (Pre-verified: HOLDS. F1 inv 3.)

Task 4: VERIFY Invariant 4 — Cache keyed by MatchKey; invalidated on write failure; partial never retried
  - RUN: `grep -n 'struct MatchKey' src/core.rs` and confirm `#[derive(... PartialEq, Eq ...)]`.
  - RUN: `grep -n '\*cache = None' src/core.rs`
    EXPECT: 1 match in `try_send_once` (cache invalidated when `failed > 0`).
  - RUN: read `send_raw_report`'s retry loop (grep `for attempt in 0..=SEND_RETRIES`).
    EXPECT: `(SendOutcome::Partial { succeeded, failed }, _) => return Err(QmkError::PartialSendError { succeeded, failed })`
    — Partial returns IMMEDIATELY (no retry). Only `TotalFailure` with
    `attempt < SEND_RETRIES` continues to the cache-rebuild+retry branch.
  - RUN: `grep -n 'SEND_RETRIES' src/core.rs` → `const SEND_RETRIES: usize = 1;`.
  - PASS CRITERION: (a) MatchKey keyed cache, (b) invalidation on any write failure,
    (c) partial-send returns an error and is NEVER re-sent. (Pre-verified: HOLDS. F1 inv 4.)

Task 5: VERIFY Invariant 5 — Typed commands reuse same framing + cache
  - RUN: `grep -n 'fn build_payload\|build_typed_payload\|send_raw_report' src/lib.rs`
    EXPECT: `run()` has ONE shared send arm that calls `build_payload(command, ...)`
    then `send_raw_report(...)`. `build_payload` delegates typed variants
    (QueryInfo/QueryCallback/SetOs/ApplyHostContext) to `core::build_typed_payload`.
  - RUN: read the `run()` match in lib.rs (grep `fn run`). EXPECT exactly 2 arms:
    `ListDevices` (no send) and a `command @ (SendMessage | QueryInfo | ...)`
    OR-pattern arm that shares ONE path: build_payload → send_raw_report → parse_reply.
  - RUN: `grep -n 'CMD_DISCRIMINATOR\|payload.push(CMD_' src/core.rs`
    EXPECT: build_typed_payload emits `[CMD_DISCRIMINATOR(0xF0)][cmd_id][args][ETX]`,
    and send_raw_report/burst_to_one prepend the SAME `[0x81][0x9F]` header and use
    the SAME MatchKey/cache as SendMessage.
  - PASS CRITERION: typed and legacy paths converge on ONE send_raw_report/burst_to_one
    (same framing, same cache, same MatchKey). (Pre-verified: HOLDS. F1 inv 5.)
  - NOTE: the contract's name `build_command_data` is GENERIC; the real fn is
    `build_typed_payload`/`build_payload` (research/notes.md F2). Do not false-negative.

Task 6: VERIFY Invariant 6 — Reply parsing disambiguates 0x51 / 0 / 1 / timeout
  - RUN: `grep -n 'fn parse_reply' src/core.rs` and read the function.
    EXPECT the match on `response[0]`:
      empty                       ⇒ CommandResponse::Timeout
      RESPONSE_MARKER (0x51)      ⇒ parse_typed_reply(response)
      0                           ⇒ CommandResponse::Legacy { matched: false }
      1                           ⇒ CommandResponse::Legacy { matched: true }
      _ (any other)               ⇒ CommandResponse::Timeout
  - PASS CRITERION: all four cases (0x51 typed / 0 / 1 / everything-else-incl-empty
    ⇒ Timeout) are distinct arms; legacy/timeout ⇒ caller stays string-only.
    (Pre-verified: HOLDS. F1 inv 6.)

Task 7: VERIFY Invariant 7 — Crate is transport-only (no window/pattern/rule)
  - RUN: `grep -rniE 'window|foreground|active[ _-]?app|regex|pattern[ _-]?match|rule[ _-]?eval|detect|focus|app[ _-]?id|toml|serde' src/`
    EXPECT: the ONLY hits are (a) `HostOs::Windows` (the OS enum variant — NOT window
    detection) and (b) doc-comment prose like "window string" describing what the
    CALLER passes (the crate receives a pre-built `class\x1Dtitle` string; it does
    not detect windows). NO actual window-detection / pattern-matching / rule-
    evaluation / config-file code.
  - RUN: `grep -rn 'toml\|serde\|regex\|glob' Cargo.toml`
    EXPECT: no matches (the crate depends only on `clap` + `hidapi` — no
    matcher/config deps. The matcher is ported to `qmkonnect`, not here).
  - PASS CRITERION: zero window/pattern/rule/detection logic in src/; zero matcher
    deps in Cargo.toml. (Pre-verified: HOLDS. F1 inv 7.)

Task 8: VERIFY Invariant 8 — Where SPEC and firmware disagree, firmware wins
  - CROSS-CHECK core.rs consts vs firmware_wire_contract.md §Constants:
      CMD_DISCRIMINATOR        == 0xF0   (contract: NOTIFY_CMD_DISCRIMINATOR 0xF0)
      RESPONSE_MARKER          == 0x51   (contract: NOTIFY_RESPONSE_MARKER   0x51)
      CMD_QUERY_INFO           == 0x01
      CMD_QUERY_CALLBACK       == 0x02
      CMD_SET_OS               == 0x03
      CMD_APPLY_HOST_CONTEXT   == 0x05
  - CROSS-CHECK HostOs discriminants (lib.rs) vs contract §SET_OS os_variant_t:
      Unsure=0, Linux=1, Windows=2, Macos=3, Ios=4.
  - CROSS-CHECK parse_typed_reply field offsets vs contract §Field Definitions
    (QUERY_INFO: [2]=proto_ver,[3]=feature_flags,[4]=callback_count,[5]=board_rules_present;
     QUERY_CALLBACK: [2]=index,[3..]=NUL-padded name; SET_OS/APPLY_HOST_CONTEXT: [2]=ack).
  - PASS CRITERION: every const/offset matches the firmware contract (firmware is
    canonical). Any mismatch ⇒ FLAG severity `significant` (do NOT silently "fix" the
    crate to match the SPEC — invariant 8 says firmware wins, so the crate is the one
    that must conform; if it already conforms, PASS; if not, flag for a code fix in a
    NEW work item). (Pre-verified: ALL MATCH, no drift. F1 inv 8 + F6.)

Task 9: RUN `cargo test` — all tests pass (contract step i)
  - RUN: `cargo test 2>&1 | tail -40`
    EXPECT: `test result: ok. N passed; 0 failed; ...` for EACH test binary
    (lib unit tests + integration). The dispatch tests use bogus VID/PID ⇒
    deterministic DeviceNotFound; parse/match tests are pure unit tests. No hardware
    needed. (Build needs libhidapi — see Known Gotchas.)
  - CAPTURE: the `test result: ok.` line(s) → SIGNOFF.md. If ANY failure, do NOT
    fabricate a pass — paste the failure and FLAG severity `significant`.

Task 10: RUN `cargo clippy` — no warnings (contract step j)
  - RUN: `cargo clippy --all-targets -- -D warnings 2>&1 | tail -30`
    EXPECT: `Finished` with zero warnings (`-D warnings` makes any warning a hard
    error, so a clean exit = zero warnings). clippy 0.1.92 is installed (F3).
  - CAPTURE: the `Finished` line → SIGNOFF.md. If clippy emits warnings, paste them
    and FLAG. Fix ONLY if it is a trivial clippy nit in code this changeset touched;
    otherwise flag (pre-existing warnings are out of scope).

Task 11: WRITE the sign-off report — plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md
  - CREATE the file with the template below, filling in:
      - HEAD commit hash + Cargo.toml version (from Task 0).
      - The per-invariant verdict table (Tasks 1–8) with file:symbol evidence +
        the one-line grep/test output that proves it.
      - The cargo test result line (Task 9) + clippy result line (Task 10).
      - The drift log (empty if none; else one row per finding with severity).
      - The signed-off conclusion.
  - TEMPLATE (copy verbatim, fill the [...]):
    ----------------------------------------------------------------
    # v0.3.0 Invariant Sign-Off — qmk_notifier

    **Date**: [YYYY-MM-DD]
    **HEAD**: [`commit-hash`] — [commit subject]
    **Crate version**: [0.3.0] (Cargo.toml)
    **Verifier**: P1.M4.T3.S2 (final cross-document consistency gate)
    **PRD reference**: §14 Key Invariants (8 items)

    ## Verdict

    **[ ALL PASS | N PASS / M FLAGGED ]** — [one-sentence conclusion].

    ## Per-invariant evidence

    | # | Invariant (PRD §14) | Verdict | Evidence (file:symbol / command) |
    |---|---------------------|---------|----------------------------------|
    | 1 | Magic header 0x81 0x9F; ETX 0x03 | PASS | core.rs burst_to_one `request_data[1]=0x81, request_data[2]=0x9F`; ETX via `ETX_TERMINATOR_BYTE=0x03` on both paths |
    | 2 | REPORT_LENGTH=32; buffer 33 | PASS | core.rs `REPORT_LENGTH=32`, `[0u8; REPORT_LENGTH+1]`, `PAYLOAD_PER_REPORT=30` |
    | 3 | VID/PID None ⇒ match any | PASS | core.rs device_matches `is_none_or` for VID/PID; `==` for usage_page/usage |
    | 4 | MatchKey cache; invalidate on fail; partial never retried | PASS | core.rs `MatchKey{PartialEq,Eq}`, `*cache=None` on failed>0, Partial⇒Err (no retry) |
    | 5 | Typed commands reuse framing+cache | PASS | lib.rs run() single send arm → build_payload → send_raw_report → burst_to_one; build_typed_payload emits [0xF0][cmd][args][0x03] |
    | 6 | Reply disambiguates 0x51/0/1/timeout | PASS | core.rs parse_reply: empty⇒Timeout, 0x51⇒typed, 0⇒Legacy{false}, 1⇒Legacy{true}, _⇒Timeout |
    | 7 | Transport-only (no window/pattern/rule) | PASS | grep of src/ finds only HostOs::Windows + doc prose; Cargo.toml has no toml/serde/regex deps |
    | 8 | Firmware wins on disagreement | PASS | core.rs consts (0xF0/0x51/0x01/0x02/0x03/0x05) + HostOs 0-4 + parse offsets all match firmware_wire_contract.md |

    ## Test gate

    - `cargo test` → [paste `test result: ok. N passed; 0 failed` line(s)]
    - `cargo clippy --all-targets -- -D warnings` → [paste `Finished` line / zero warnings]

    ## Drift log

    [If none: "No drift found. All 8 invariants hold; crate matches firmware contract."]
    [If any: one row per finding — `| finding | severity (minor-comment/significant) | action taken (fixed/flagged) |`]

    ## Cross-document consistency (README ↔ code ↔ firmware)

    - README (post-S1) Technical Details byte sequence `[0x81][0x9F][0xF0][cmd][args][0x03]`
      matches core.rs framing. [CONFIRMED | typo fixed at README.md:line N]
    - core.rs consts match firmware_wire_contract.md §Constants. [CONFIRMED]

    ## Sign-off

    All 8 PRD §14 Key Invariants are preserved in the v0.3.0 codebase. The crate is
    internally consistent and consistent with the canonical firmware wire contract.
    Ready for the v0.3.0 tag.

    — [P1.M4.T3.S2]
    ----------------------------------------------------------------

Task 12: FINAL SCOPE CHECK — confirm no forbidden edits
  - RUN: `git status --porcelain`
    EXPECT: `?? plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md` (the new report).
    OPTIONALLY: one modified `src/lib.rs` (a one-line doc-comment fix, IF you
    confirmed and applied the build_command_data nit from F2).
    OPTIONALLY: `M README.md` (S1's parallel edit — NOT yours; leave it).
    OPTIONALLY: `M Cargo.toml`/`M Cargo.lock` (P1.M4.T2.S1's — NOT yours; leave it).
  - RUN: `git status --porcelain | grep -E ' PRD.md|tasks.json|prd_snapshot.md|.gitignore' ; echo exit=$?`
    EXPECT: exit 1 (none of these were touched).
  - RUN: `git diff --stat src/ 2>/dev/null` — if any src/ change appears, confirm it
    is the ONE allowed comment-typo fix and nothing else (no logic/const/API).
```

### Implementation Patterns & Key Details

```markdown
<!-- ===== The sweep discipline (read → capture → paste; do NOT mutate) ===== -->
Every task is a grep/read/test whose OUTPUT becomes SIGNOFF.md evidence. The agent
must NOT "fix" anything it finds unless it is (a) a minor comment typo AND (b)
explicitly the build_command_data nit (or equally trivial). Significant drift ⇒ FLAG.

<!-- ===== How to re-locate a finding when line numbers drifted (they will) ===== -->
grep for the SYMBOL or the VALUE, not the line number:
  - magic header:  grep -n 'request_data\[1\] = 0x81' src/core.rs   (symbolic)
  - REPORT_LENGTH: grep -n 'pub const REPORT_LENGTH' src/core.rs    (symbolic)
  - is_none_or:    grep -n 'is_none_or' src/core.rs                 (symbolic)
A miss on a line number is meaningless; a miss on the symbol/value grep is a REAL
invariant failure → FLAG severity `significant`.

<!-- ===== The one permitted comment fix (build_command_data nit, F2) ===== -->
lib.rs ~line 422 has a doc comment: `// straight to build_payload / core::build_command_data.`
The real fn is `core::build_typed_payload`. This is a defensible generic reference,
so fixing it is OPTIONAL. If you fix it, the change is exactly:
  - "core::build_command_data"  →  "core::build_typed_payload"
(one token, one comment, zero logic). Record it in the Drift log as `minor-comment`.
Do NOT touch any other comment, doc, or code.

<!-- ===== What "FLAG significant" means (and does NOT mean) ===== -->
If a const genuinely drifted (e.g., RESPONSE_MARKER == 0x52), or the partial-send
path now retries, or device_matches lost is_none_or — that is a REAL invariant
breach. DO NOT fix it in this task (that's a new work item). Instead:
  1. Paste the grep output proving the breach into SIGNOFF.md.
  2. Mark that invariant FLAG with severity `significant`.
  3. State in the Sign-off section that the v0.3.0 tag is BLOCKED pending a fix.
(Pre-verified expectation: this does NOT happen — all 8 hold at research time.)

<!-- ===== cargo test / clippy capture format ===== -->
Paste the literal `test result: ok.` line and the literal `Finished` line. Do NOT
paraphrase "all tests pass" without the captured output — the sign-off is auditable,
so the evidence must be the real command output, trimmed to the relevant line(s).
```

### Integration Points

```yaml
FILES READ (the verification surface — none are edited except optional comment fix):
  - read: "src/core.rs (invariants 1,2,4,5,6,8), src/lib.rs (5,6,7),
           src/error.rs (4), src/main.rs (7), PRD.md §14 (the invariants),
           plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md (8)."

FILES CREATED:
  - create: "plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md (the deliverable)."

FILES OPTIONALLY EDITED (only if a confirmed minor-comment nit):
  - maybe-edit: "src/lib.rs — the one-line build_command_data → build_typed_payload
                 doc-comment fix (research/notes.md F2). Nothing else in src/."

NOT TOUCHED (hard scope boundary):
  - never: "PRD.md, **/tasks.json, **/prd_snapshot.md, .gitignore."
  - never: "Cargo.toml / Cargo.lock (P1.M4.T2.S1's territory; COMPLETE)."
  - never: "README.md body edits (S1's territory; S2 only fixes a byte-sequence typo
            if S1 missed one — cross-doc consistency is S2's scope, prose is S1's)."
  - never: "Any src/ logic, const, or API signature (this task FREEZES the tree)."
  - never: "The v0.3.0 git tag (separate release step; this gate PRECEDES the tag)."

DEPENDENCY ON S1 (parallel):
  - consumes: "The post-S1 README.md. S2 verifies README↔code consistency (the byte
               sequence in README's Technical Details must match core.rs). If S1 is
               still mid-edit when S2 runs, S2 verifies against whatever README state
               exists at git HEAD and notes the state in SIGNOFF.md."
  - no-conflict: "S1 edits README.md only; S2 creates SIGNOFF.md only (disjoint files)."
```

## Validation Loop

> This is a verification task. The "validation" is the sweep itself: each Level
> below IS one of the verification tasks, and its PASS is the evidence that goes
> into SIGNOFF.md. There is no separate "did the code compile" step beyond
> `cargo test`/`cargo clippy` (Tasks 9–10).

### Level 1: Sweep Readiness (Immediate Feedback)

```bash
# Confirm the tree is the final v0.3.0 state before sweeping.
git log --oneline -1                      # EXPECT: bdd3343 or later
grep '^version' Cargo.toml                # EXPECT: version = "0.3.0"
cargo --version && cargo clippy --version # EXPECT: both present (1.92 / 0.1.92)

# If cargo is missing or version != 0.3.0, STOP — the tree is not ready to sign off.
```

### Level 2: Per-Invariant Greps (Component Validation)

```bash
# Invariant 1 — magic header + ETX
grep -n 'request_data\[1\] = 0x81\|request_data\[2\] = 0x9F' src/core.rs   # EXPECT: 2 matches
grep -n 'ETX_TERMINATOR_BYTE' src/core.rs src/lib.rs                       # EXPECT: def + uses

# Invariant 2 — REPORT_LENGTH=32, buffer 33, payload 30
grep -n 'pub const REPORT_LENGTH' src/core.rs        # EXPECT: = 32
grep -n 'REPORT_LENGTH + 1' src/core.rs              # EXPECT: ≥3 (the 33-byte buffers)
grep -n 'PAYLOAD_PER_REPORT' src/core.rs             # EXPECT: = REPORT_LENGTH - 2

# Invariant 3 — None ⇒ match any
grep -n 'is_none_or' src/core.rs                     # EXPECT: 2 (VID + PID in device_matches)

# Invariant 4 — MatchKey cache, invalidation, partial-never-retry
grep -n 'struct MatchKey' src/core.rs                # EXPECT: derive PartialEq, Eq
grep -n '\*cache = None' src/core.rs                 # EXPECT: 1 (try_send_once, failed>0)
grep -n 'SEND_RETRIES' src/core.rs                   # EXPECT: const = 1
# + read send_raw_report retry loop: Partial ⇒ Err (immediate); only TotalFailure retries.

# Invariant 5 — typed reuse same path
grep -n 'build_payload\|send_raw_report\|build_typed_payload' src/lib.rs  # EXPECT: run() single arm

# Invariant 6 — reply disambiguation
grep -n 'RESPONSE_MARKER => parse_typed_reply\|=> CommandResponse::Legacy\|=> CommandResponse::Timeout' src/core.rs

# Invariant 7 — transport-only
grep -rniE 'window|foreground|active[ _-]?app|regex|pattern[ _-]?match|rule[ _-]?eval|detect|focus|app[ _-]?id|toml|serde' src/
# EXPECT: only HostOs::Windows + doc-comment prose "window string". NO detection logic.
grep -n 'toml\|serde\|regex\|glob' Cargo.toml        # EXPECT: no matches

# Invariant 8 — firmware contract cross-check (const values)
grep -n 'CMD_DISCRIMINATOR\|RESPONSE_MARKER\|CMD_QUERY_INFO\|CMD_QUERY_CALLBACK\|CMD_SET_OS\|CMD_APPLY_HOST_CONTEXT' src/core.rs | grep 'const'
# EXPECT: 0xF0 / 0x51 / 0x01 / 0x02 / 0x03 / 0x05 — all match firmware_wire_contract.md §Constants.
```

### Level 3: Build & Test Gates (System Validation)

```bash
# Task 9 — cargo test (all tests pass; hardware-free on the critical path).
cargo test 2>&1 | tail -40
# EXPECT: `test result: ok. N passed; 0 failed; ...` for every test binary.
# If a link error mentions hidapi, install libhidapi-dev (env issue, not a code defect).

# Task 10 — cargo clippy (zero warnings; -D warnings makes it a hard gate).
cargo clippy --all-targets -- -D warnings 2>&1 | tail -30
# EXPECT: `Finished` with zero warnings (exit 0).
```

### Level 4: Sign-Off Artifact & Scope (Domain-Specific Validation)

```bash
# SIGNOFF.md exists and is well-formed.
test -f plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md && echo "SIGNOFF present"
grep -c '^| [1-8] |' plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md   # EXPECT: 8 (one row per invariant)
grep -c 'test result: ok' plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md  # EXPECT: ≥1 (cargo test evidence)
grep -ci 'clippy' plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md      # EXPECT: ≥1 (clippy evidence)

# Scope: no forbidden files touched; src/ changes (if any) are comment-only.
git status --porcelain | grep -E ' PRD.md|tasks.json|prd_snapshot.md|.gitignore' ; echo "forbidden exit=$?"
# EXPECT: exit 1 (none touched).
git diff --stat src/ 2>/dev/null   # if non-empty, confirm it is the ONE allowed comment fix only.

# If a src/ logic/const/API change appears, REVERT it — this task does not modify logic.
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1: tree at v0.3.0 (Cargo.toml `version = "0.3.0"`, HEAD ≥ bdd3343).
- [ ] Level 2: all 8 invariant greps produce the expected matches (Tasks 1–8).
- [ ] Level 3: `cargo test` → `test result: ok. … 0 failed`; `cargo clippy -- -D warnings` → 0 warnings.
- [ ] Level 4: `SIGNOFF.md` present, 8 invariant rows, cargo test + clippy evidence embedded.

### Feature (Verification) Validation
- [ ] All 8 invariants have an explicit PASS/FLAG verdict with file:symbol evidence.
- [ ] Invariant 8 firmware cross-check table complete (consts + HostOs + field offsets).
- [ ] Any drift recorded with severity (`minor-comment` fixed; `significant` flagged).
- [ ] Signed-off conclusion line present in SIGNOFF.md.

### Code Quality Validation (Scope Discipline)
- [ ] No logic/const/API edit in `src/` (only an optional one-line comment fix in lib.rs).
- [ ] No edit to PRD.md, tasks.json, prd_snapshot.md, .gitignore.
- [ ] No edit to Cargo.toml/Cargo.lock (P1.M4.T2.S1's territory).
- [ ] No re-do of S1's README prose edits (only an optional byte-sequence typo fix).
- [ ] No v0.3.0 git tag created (separate release step).

### Documentation & Deployment
- [ ] `SIGNOFF.md` is self-contained (a reader can audit the release without the PRP).
- [ ] Evidence is the LITERAL command output (not paraphrased "all pass").
- [ ] Drift log is explicit (empty-state sentence if none; rows if any).

---

## Anti-Patterns to Avoid

- ❌ Don't pin findings to line numbers — the repo is live (core.rs was refactored
      and S1 edited README mid-research). Re-locate by SYMBOL/VALUE via grep. A
      stale line number yields a false negative or a misread.
- ❌ Don't grep for a fn literally named `build_command_data` and conclude invariant
      5 failed — that is the contract's GENERIC name; the real fn is
      `build_typed_payload`/`build_payload`. (research/notes.md F2.)
- ❌ Don't "fix" significant drift in this task. A real constant change, a partial-
      send retry, a lost `is_none_or` are FLAG-severity-significant — they need a
      new work item. This task FREEZES and CERTIFIES; it does not re-open.
- ❌ Don't paraphrase test/clippy results as "all pass" — paste the literal
      `test result: ok.` / `Finished` line. The sign-off is auditable.
- ❌ Don't touch PRD.md even if you believe the SPEC is wrong — invariant 8 says
      firmware wins, so the crate conforms to firmware, not vice-versa; PRD.md is
      human-owned and READ-ONLY.
- ❌ Don't flag "firmware not yet implemented" as drift — typed commands timing out
      against current firmware is the DESIGNED fallback (Timeout ⇒ string-only mode).
      That is staged rollout, not a SPEC↔firmware disagreement. (F6.)
- ❌ Don't run `cargo test` on a machine missing `libhidapi-dev` and call the link
      error a code defect — it's an environment precondition (F3).
- ❌ Don't create the v0.3.0 git tag — this gate PRECEDES the tag; tagging is a
      separate release step owned by the maintainer after reading SIGNOFF.md.
- ❌ Don't re-do S1's README section edits — S2 owns cross-doc CONSISTENCY only
      (README byte sequences matching core.rs); S1 owns README prose/structure.
- ❌ Don't skip the optional `build_command_data` comment nit "because it's too
      small" if you've already confirmed it — recording it (fixed or left-as-is)
      in the Drift log is the point; the audit trail matters more than the fix.