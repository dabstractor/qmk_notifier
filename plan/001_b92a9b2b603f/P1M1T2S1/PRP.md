# PRP — P1.M1.T2.S1: Add typed-command constants to core.rs

---

## Goal

**Feature Goal**: Add the v0.3.0 **typed-command wire-vocabulary constants** to
`src/core.rs` — the 7 named byte/timeout values that `build_command_data`
(P1.M2.T1), `parse_reply` (P1.M2.T2), and the evolved `burst_to_one` (P1.M3.T1)
will reference. They are the Rust-side mirror of the firmware `notifier.h`
`#define`s (canonicalized in `firmware_wire_contract.md` §Constants): the
`0xF0` discriminator, the `0x51` typed-response marker, the four command IDs
(`0x01`/`0x02`/`0x03`/`0x05`), and a `1000 ms` bounded first-reply read timeout.
Visibility is `pub(crate)` for the 6 wire constants and plain `const` for the
host-side timeout — these are **internal transport constants, not public API**.
One unit test pins every wire value to the firmware contract (drift ⇒ silent
interop break) and asserts the timeout invariant (`> 0`).

**Deliverable**: A single new **typed-command constants block** inserted into
**`src/core.rs`** immediately after the existing `pub const REPORT_LENGTH`
line (near `DEFAULT_USAGE_PAGE`/`REPORT_LENGTH`, per the contract), plus **one**
new `#[test]` appended at the END of core.rs's existing `#[cfg(test)] mod tests`
block. **`src/core.rs` is the only file modified.** No lib.rs re-export, no
README/PRD change, no Cargo.toml change.

**Success Definition**: `cargo build` compiles with **zero warnings**;
`cargo clippy --lib` shows **zero warnings**; `cargo fmt --check` exits 0;
`cargo test --lib` passes with **30 tests** (the current 29 + 1 new), 0 failed;
every new constant has a `///` doc comment citing PRD §10.1/§10.2/§4.6; the new
test asserts all 6 wire values equal their firmware-contract values and
`REPLY_READ_TIMEOUT_MS > 0`; no file other than `src/core.rs` is modified.

## User Persona (if applicable)

**Target User**: Downstream implementers of the v0.3.0 typed-command transport —
`build_command_data` (P1.M2.T1) emits `CMD_DISCRIMINATOR`/`CMD_*` into payloads;
`parse_reply` (P1.M2.T2) keys off `RESPONSE_MARKER`/`CMD_*`; `burst_to_one`
(P1.M3.T1) reads the first reply with `REPLY_READ_TIMEOUT_MS`.

**Use Case**: When framing a typed command, the builder writes
`[0x81][0x9F][CMD_DISCRIMINATOR=0xF0][CMD_*][args…][0x03]`; when parsing a reply,
the parser checks `response[0] == RESPONSE_MARKER (0x51)` then dispatches on the
`response[1]` cmd echo against `CMD_*`. Naming the bytes as constants (instead of
magic numbers) makes the wire layout grep-able and drift-detectable.

**User Journey**: `RunCommand::QueryInfo` → (P1.M2.T1) `build_command_data` emits
`[0xF0, CMD_QUERY_INFO, 0x03]` → burst-write → (P1.M3.T1) `burst_to_one` reads
reply with `REPLY_READ_TIMEOUT_MS` → (P1.M2.T2) `parse_reply` sees
`response[0]==RESPONSE_MARKER` → returns `CommandResponse::Info{…}`.

**Pain Points Addressed**: Replaces bare `0xF0`/`0x51`/`0x01` magic numbers with
named, documented, test-pinned constants so the wire contract is expressed in
code and any value drift is caught by a failing test, not a silent interop bug.

## Why

- These constants are the **foundational vocabulary** for the entire v0.3.0
  typed-command milestone (P1). Every framing/parsing/transport subtask in P1.M2
  and P1.M3 references them. Defining them first (before their consumers) keeps
  the dependency chain clean (constants → pure framing/parse → transport capture),
  mirroring how P1.M1.T1 defined `HostOs`/`RunCommand` variants before this work.
- They are **additive and dependency-free** (no HID I/O, no new crate deps, no
  public-API surface) so they land cleanly and cannot break anything. They reuse
  the existing `core.rs` constants region and existing `#[allow(dead_code)]`
  pattern.
- Pinning them in a test against the firmware contract is the **cheapest possible
  drift detector**: the firmware `notifier.h` is the source of truth, and a
  mismatched constant here would silently break host↔firmware interop. PRD §14
  invariant 5/6 codify `0xF0` and `0x51`; this test enforces it at compile-test
  time.

## What

### The new constant block (paste into core.rs)

Insert this block **immediately after** the line `pub const REPORT_LENGTH: usize = 32;`
and **before** `pub fn parse_hex_or_decimal`:

```rust
// --- Typed-command transport constants (v0.3.0) -----------------------------
// Wire vocabulary for the typed-command path (PRD §10.1, §10.2). Mirror of the
// firmware `notifier.h` #defines (canonicalized in
// `plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md` §Constants).
// `pub(crate)` (not `pub`): internal transport constants, NOT public API.
//
// NOTE: each constant carries a temporary `#[allow(dead_code)]`. The consumers
// land in later subtasks — build_command_data (P1.M2.T1), parse_reply
// (P1.M2.T2), and burst_to_one's reply capture (P1.M3.T1) — so until then no
// non-test code references them and rustc would otherwise emit dead_code
// warnings (verified: even `pub(crate)` items warn, and a `#[cfg(test)]`-only
// reference does NOT silence them in `cargo build`). REMOVE each allow when its
// constant gains a real consumer.

/// Typed-command discriminator: first payload byte after 0x81 0x9F (PRD §10.1).
#[allow(dead_code)]
pub(crate) const CMD_DISCRIMINATOR: u8 = 0xF0;
/// Typed-response marker: response[0] == 0x51 means typed reply (PRD §10.2).
#[allow(dead_code)]
pub(crate) const RESPONSE_MARKER: u8 = 0x51;
/// Command IDs from firmware PRD §4.6 command table.
#[allow(dead_code)]
pub(crate) const CMD_QUERY_INFO: u8 = 0x01;
#[allow(dead_code)]
pub(crate) const CMD_QUERY_CALLBACK: u8 = 0x02;
#[allow(dead_code)]
pub(crate) const CMD_SET_OS: u8 = 0x03;
#[allow(dead_code)]
pub(crate) const CMD_APPLY_HOST_CONTEXT: u8 = 0x05;
/// Bounded timeout (ms) for reading the first reply after a burst.
/// Must be > 0 (unlike the drain's non-blocking timeout=0).
#[allow(dead_code)]
const REPLY_READ_TIMEOUT_MS: i32 = 1000;
```

> **Why `#[allow(dead_code)]` is present (deviation from the bare contract
> snippet):** the work-item contract shows the constants with no attributes, but
> that literal snippet emits 7 `dead_code` warnings in `cargo build` (empirically
> verified — see *Known Gotchas*). The project's success bar is *zero warnings*
> (consistent with the sibling PRPs), so the per-constant allows are required.
> They follow the exact precedent already in this file — `DeviceCache.api`
> (core.rs:295-296) uses `///` doc → `#[allow(dead_code)]` → field.

### The new unit test (append at the END of core.rs's `mod tests`)

Append this **one** test after the last existing test
(`match_key_equality_drives_cache_rebuild`) and before the `mod tests` closing
`}`:

```rust
#[test]
fn typed_command_constants_match_firmware_contract() {
    // Wire-protocol values are the canonical source of truth from the firmware
    // notifier.h (see firmware_wire_contract.md §Constants). Drift here would
    // silently break host<->firmware interop, so pin every value.
    assert_eq!(
        CMD_DISCRIMINATOR, 0xF0,
        "NOTIFY_CMD_DISCRIMINATOR: typed-command first payload byte after 0x81 0x9F"
    );
    assert_eq!(
        RESPONSE_MARKER, 0x51,
        "NOTIFY_RESPONSE_MARKER: response[0]==0x51 means typed reply"
    );
    assert_eq!(CMD_QUERY_INFO, 0x01, "NOTIFY_CMD_QUERY_INFO");
    assert_eq!(CMD_QUERY_CALLBACK, 0x02, "NOTIFY_CMD_QUERY_CALLBACK");
    assert_eq!(CMD_SET_OS, 0x03, "NOTIFY_CMD_SET_OS");
    assert_eq!(CMD_APPLY_HOST_CONTEXT, 0x05, "NOTIFY_CMD_APPLY_HOST_CONTEXT");
    // REPLY_READ_TIMEOUT_MS is a host-side choice (bounded blocking read of the
    // first reply after a burst), NOT a firmware value. Its invariant is the
    // documented "Must be > 0" (unlike the drain loop's non-blocking 0).
    assert!(
        REPLY_READ_TIMEOUT_MS > 0,
        "reply read timeout must block for a real reply, not poll like the drain's 0"
    );
}
```

### Success Criteria

- [ ] Exactly 7 new constants exist in `src/core.rs`: `CMD_DISCRIMINATOR`,
      `RESPONSE_MARKER`, `CMD_QUERY_INFO`, `CMD_QUERY_CALLBACK`, `CMD_SET_OS`,
      `CMD_APPLY_HOST_CONTEXT` (all `pub(crate) const …: u8`), and
      `REPLY_READ_TIMEOUT_MS` (`const …: i32`), with the exact values above.
- [ ] Each constant has a `///` doc comment (the 4 CMD_* constants may share the
      one `/// Command IDs from firmware PRD §4.6 command table.` line above the
      first of them — i.e. exactly the block shown).
- [ ] Each constant has a temporary `#[allow(dead_code)]` (doc comment → allow →
      item ordering, matching core.rs:295-296).
- [ ] The block is placed immediately after `pub const REPORT_LENGTH: usize = 32;`
      and before `pub fn parse_hex_or_decimal`.
- [ ] Exactly 1 new `#[test]` (`typed_command_constants_match_firmware_contract`)
      is appended at the END of the existing `#[cfg(test)] mod tests` block.
- [ ] `cargo build` → zero warnings; `cargo clippy --lib` → zero warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → 30 passed, 0 failed.
- [ ] No file other than `src/core.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim constant block
> (values, types, visibility, doc comments, `#[allow(dead_code)]` rationale), the
> exact placement anchor (a unique stable line), the verbatim test, the
> canonical firmware-contract source table, the empirically-verified dead_code
> gotcha (with the compile-time failure mode), the baseline test count, and the
> verified build/clippy/fmt/test commands are all below. The implementer does not
> need to read any QMK firmware source — `firmware_wire_contract.md` canonicalizes
> every value.

### Documentation & References

```yaml
# MUST READ — the canonical wire contract (the values this task mirrors)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "§Constants lists the firmware notifier.h #defines this crate mirrors
        (NOTIFY_CMD_DISCRIMINATOR=0xF0, NOTIFY_RESPONSE_MARKER=0x51, the four
        NOTIFY_CMD_* ids). §Typed-Command Framing and §Reply Disambiguation
        explain WHY each value matters. This is the single source of truth the
        unit test asserts against."
  section: "Constants", "Typed-Command Framing", "Reply Disambiguation"
  critical: "Where this contract and any prose disagree, the contract wins.
             REPLY_READ_TIMEOUT_MS (1000) is host-side, NOT in notifier.h — its
             test asserts the >0 invariant, not equality."

# MUST READ — the file being edited (read current state before editing)
- file: src/core.rs
  why: "Holds the existing constants region (DEFAULT_*/REPORT_LENGTH, lines 6-10)
        where the new block inserts AFTER line 10; the private transport consts
        (PAYLOAD_PER_REPORT/IN_DRAIN_MAX/SEND_RETRIES) showing the rich-///-doc
        style to mirror; the #[allow(dead_code)] precedent on DeviceCache.api
        (lines 295-296: doc comment -> allow -> field); and the #[cfg(test)]
        mod tests block (starts ~line 425) whose END is the test append anchor."
  pattern: "Constants at top are `pub const` (public API); private consts lower
            down have `///` doc comments. Existing test names are descriptive
            snake_case (matches_when_*, batches_for_*, rejects_*)."
  gotcha: "Do NOT change the existing pub DEFAULT_* constants, the private
           PAYLOAD_PER_REPORT/IN_DRAIN_MAX/SEND_RETRIES, or any function."

# MUST READ — the transport-evolution plan that these constants feed into
- file: plan/001_b92a9b2b603f/architecture/transport_evolution.md
  why: "§New Constants (core.rs) shows the intended constant set and §burst_to_one
        Reply Capture Logic shows REPLY_READ_TIMEOUT_MS feeding read_timeout().
        Confirms the downstream consumers (build_command_data P1.M2.T1,
        parse_reply P1.M2.T2, burst_to_one P1.M3.T1) that justify the names and
        the temporary #[allow(dead_code)]."
  section: "New Constants (core.rs)", "burst_to_one Reply Capture Logic"

# REFERENCE — PRD typed-command framing + invariants
- file: PRD.md
  why: "§10.1 (Framing) and §10.2 (Reply parsing) define the wire layout the doc
        comments cite; §14 invariants 5 (0xF0 discriminator) and 6 (0x51 marker)
        are the rules the test enforces."
  section: "10.1 Framing", "10.2 Reply parsing", "14. Key Invariants (5,6)"

# REFERENCE — research notes (dead_code experiments, placement rationale)
- docfile: plan/001_b92a9b2b603f/P1M1T2S1/research/notes.md
  why: "Documents the empirically-verified dead_code behavior (why allows are
        mandatory), the firmware-symbol->constant mapping table, the placement
        decision, and the baseline test counts."
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.2.1", edition="2021"
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore          # contains only: /target
└── src
    ├── main.rs         # binary entrypoint (thin wrapper) — DO NOT TOUCH
    ├── core.rs         # <-- FILE TO EDIT (constants + 1 test)
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # HostOs/RunCommand/CommandResponse/RunParameters/run() — DO NOT TOUCH
```

### Desired Codebase tree with files to be added/modified

```bash
src/
├── core.rs   # MODIFIED ONLY — add typed-command constant block (after REPORT_LENGTH)
│             #                   + 1 test at end of #[cfg(test)] mod tests
└── (unchanged) lib.rs, error.rs, main.rs, Cargo.toml, README.md
```

> No new files are created. All changes are in `src/core.rs`.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: the bare contract snippet (constants with NO #[allow(dead_code)])
//   EMITS 7 dead_code WARNINGS in `cargo build`. Verified empirically with clean
//   /tmp cargo projects (edition 2021): `pub(crate)` items are NOT exempt from
//   dead_code, and referencing a constant only inside `#[cfg(test)] mod tests`
//   does NOT silence the warning in a NON-test build (the test module is
//   compiled out of `cargo build`). The ONLY consumers land later
//   (build_command_data P1.M2.T1, parse_reply P1.M2.T2, burst_to_one P1.M3.T1),
//   so per-constant `#[allow(dead_code)]` is mandatory NOW. Remove each allow
//   when its constant gains a real consumer. (A leftover/redundant
//   allow(dead_code) does NOT itself warn — Rust tolerates it.)

// CRITICAL: attribute order MUST be `///` doc comment -> `#[allow(dead_code)]`
//   -> item. This matches the existing DeviceCache.api precedent (core.rs:295-
//   296). Do NOT put the attribute above the doc comment.

// NOTE: do NOT use a module-level `#![allow(dead_code)]` — it is far too broad
//   (would mask legitimate future dead_code anywhere in core.rs). Per-constant
//   allows are surgical.

// NOTE: visibility is EXACTLY as the contract specifies — 6 `pub(crate)` and
//   REPLY_READ_TIMEOUT_MS plain `const`. Do NOT promote REPLY_READ_TIMEOUT_MS to
//   pub(crate) (it's consumed only inside core::burst_to_one) and do NOT demote
//   the wire constants to private (parse_reply/build_command_data may be tested
//   from elsewhere and benefit from crate visibility).

// NOTE: REPLY_READ_TIMEOUT_MS is `i32`, NOT `u64`/`Duration`. hidapi's
//   `HidDevice::read_timeout(buf, timeout: i32)` takes an `i32` milliseconds
//   arg; keep the type matched so P1.M3.T1 can pass it directly.

// NOTE: `pub(crate)` constants are NOT re-exported via lib.rs and are NOT public
//   API. Do NOT add anything to lib.rs. (lib.rs already has its own completed
//   P1.M1.T1.* work — HostOs, the extended RunCommand, CommandResponse; this
//   task does not touch it.)

// NOTE: no rustfmt.toml / clippy.toml exist — default configs. `cargo fmt`
//   reformats to default style; `cargo fmt --check` is the CI gate.
```

## Implementation Blueprint

### Data models and structure

No new types, structs, or enums. This subtask adds **7 `const` items** (data
only) and **1 test function** (behavior: assertion). The constants are flat
module-level items; no grouping struct/module is introduced.

```rust
// Structural change = the single constant block in "What" + the single test in
// "What". Values are literals; types are u8 (wire bytes) and i32 (hidapi timeout).
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ the current src/core.rs and confirm the anchors
  - READ: the existing constants region (lines 6-10): DEFAULT_VENDOR_ID,
          DEFAULT_PRODUCT_ID, DEFAULT_USAGE_PAGE, DEFAULT_USAGE, REPORT_LENGTH.
          The new block inserts IMMEDIATELY AFTER `pub const REPORT_LENGTH: usize
          = 32;` (line 10) and BEFORE `pub fn parse_hex_or_decimal`.
  - READ: the private transport consts (PAYLOAD_PER_REPORT, IN_DRAIN_MAX,
          SEND_RETRIES) ~lines 50-70 — confirm the rich-`///`-doc style to mirror.
  - READ: the `#[allow(dead_code)]` precedent on `DeviceCache.api` (lines 295-
          296) — confirm the `///` -> `#[allow(dead_code)]` -> item ordering.
  - READ: the `#[cfg(test)] mod tests` block (starts ~line 425); locate the LAST
          test `match_key_equality_drives_cache_rebuild` — the new test appends
          after it, before the module's closing `}`.
  - GOAL: know the exact unique anchors so both edits are surgical.

Task 2: INSERT the typed-command constant block into src/core.rs
  - ADD: the full block from the "What" section (section comment + 7 constants,
          each with its `///` doc comment + temporary `#[allow(dead_code)]`)
          immediately AFTER `pub const REPORT_LENGTH: usize = 32;` and BEFORE
          `pub fn parse_hex_or_decimal`.
  - KEEP: all existing pub DEFAULT_* constants, all private transport consts,
          every function (parse_hex_or_decimal, list_hid_devices,
          send_raw_report, try_send_once, burst_to_one, batches_for,
          open_matching_devices, ensure_cache, lock_cache, device_matches), the
          MatchKey/DeviceCache/SendOutcome types, and the DEVICE_CACHE static
          byte-for-byte unchanged.
  - NAMING: UPPER_SNAKE_CASE constants (Rust convention); exactly
          CMD_DISCRIMINATOR, RESPONSE_MARKER, CMD_QUERY_INFO, CMD_QUERY_CALLBACK,
          CMD_SET_OS, CMD_APPLY_HOST_CONTEXT, REPLY_READ_TIMEOUT_MS.
  - TYPES: u8 for the 6 wire values, i32 for REPLY_READ_TIMEOUT_MS (matches
          hidapi read_timeout's signature for P1.M3.T1).
  - VISIBILITY: `pub(crate)` for the 6 wire constants; plain `const` for
          REPLY_READ_TIMEOUT_MS.
  - DO NOT: wrap them in a module, add `#![allow(dead_code)]` at module level,
            change any existing constant, or touch lib.rs/error.rs/main.rs.

Task 3: APPEND the contract-pinning unit test to core.rs's mod tests
  - ADD: `typed_command_constants_match_firmware_contract` (see "What").
  - PLACEMENT: at the END of the existing `#[cfg(test)] mod tests { use super::*;
          ... }` block — after `match_key_equality_drives_cache_rebuild`, before
          the block's closing `}`.
  - PATTERN: use the already-present `use super::*;` — do NOT re-import. Match
          the file's descriptive test-naming style (no `test_` prefix needed —
          see batches_for_*, matches_*, rejects_*).
  - COVERAGE: all 6 wire values asserted equal to their firmware-contract value
          (messages cite the NOTIFY_* firmware symbol); REPLY_READ_TIMEOUT_MS
          asserted > 0.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --lib`, then
          `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0;
          test result "30 passed; 0 failed" (29 current + 1 new).
  - IF dead_code warnings appear on any new constant: you forgot its
          `#[allow(dead_code)]` — add it (doc comment -> allow -> item).
  - IF rustfmt reorders your attributes: you put `#[allow(dead_code)]` ABOVE the
          `///` doc comment — move the allow BELOW the doc comment.
```

### Implementation Patterns & Key Details

```rust
// === PLACEMENT ANCHOR (illustrative; the real file has S1/S2/S3 lib.rs work
//     but core.rs's constants region is stable) ===
//
// pub const DEFAULT_VENDOR_ID:  u16 = 0xFEED;
// pub const DEFAULT_PRODUCT_ID: u16 = 0x0000;
// pub const DEFAULT_USAGE_PAGE: u16 = 0xFF60;
// pub const DEFAULT_USAGE:      u16 = 0x61;
// pub const REPORT_LENGTH: usize = 32;
//
// // >>> INSERT the typed-command constant block HERE <<<
//
// pub fn parse_hex_or_decimal(input: &str) -> Result<u16, QmkError> { ... }


// === TEST PLACEMENT ANCHOR (illustrative) ===
//
//     #[test]
//     fn match_key_equality_drives_cache_rebuild() { ... }
//
//     // >>> APPEND typed_command_constants_match_firmware_contract HERE,
//     //     before the mod's closing } <<<
// }


// === WHY #[allow(dead_code)] (the one non-obvious requirement) ===
//   Consumers land in later subtasks (P1.M2.T1/T2, P1.M3.T1). Until then no
//   non-test code references these constants. Empirically verified (clean
//   cargo build on a /tmp lib crate, edition 2021):
//     * pub(crate) const with no consumer  -> WARNS "constant ... is never used"
//     * const referenced ONLY in #[cfg(test)] -> STILL WARNS in `cargo build`
//       (the test module is absent in a non-test build)
//     * per-constant #[allow(dead_code)]    -> ZERO warnings (chosen fix)
//   Matches the existing DeviceCache.api precedent (core.rs:295-296).


// === WHY i32 (not u64/Duration) for REPLY_READ_TIMEOUT_MS ===
//   hidapi::HidDevice::read_timeout(&mut self, buf: &mut [u8], timeout: i32)
//   takes an i32 milliseconds arg. P1.M3.T1 will pass this constant directly;
//   matching the type now avoids a cast later.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - add:    "7 typed-command constants (block after REPORT_LENGTH)"
  - add:    "1 #[test] at the END of core.rs's #[cfg(test)] mod tests block"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps, no new imports (u8/i32 are std-prelude).

PUBLIC API SURFACE:
  - adds:    "(nothing public — constants are pub(crate)/private)"
  - unchanged: "all lib.rs public types (HostOs, RunCommand, CommandResponse,
                RunParameters), parse_cli_args, run signature/body, all core::
                re-exports"

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness; their landing
is what lets you REMOVE the corresponding #[allow(dead_code)]):
  - P1.M2.T1 build_command_data: "emits CMD_DISCRIMINATOR + CMD_* into [0xF0, cmd,
        args, 0x03] payloads (pure fn in core.rs)."
  - P1.M2.T2 parse_reply:        "keys off RESPONSE_MARKER (response[0]==0x51) and
        dispatches on CMD_* via the response[1] cmd echo (pure fn in core.rs)."
  - P1.M3.T1 burst_to_one evolve: "uses REPLY_READ_TIMEOUT_MS for the bounded
        first-reply read_timeout before the non-blocking drain loop."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified. Do NOT edit lib.rs (P1.M1.T1.* work is complete
    there: HostOs, extended RunCommand, CommandResponse), error.rs, main.rs, or
    Cargo.toml. Do NOT add a lib.rs re-export — these are not public API.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt style — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# If you see "constant `CMD_*` is never used" / "... `REPLY_READ_TIMEOUT_MS` is
# never used": that constant is missing its #[allow(dead_code)] — add it.

# Lint (default clippy — no clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors for the new constants.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0 (no diff). If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the new test in isolation first.
cargo test --lib typed_command_constants_match_firmware_contract -- --nocapture
# Expected: 1 passed.

# Run the full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 30 passed; 0 failed; 0 ignored; ..." (29 current + 1 new).

# Sanity: confirm the existing core.rs tests STILL pass untouched.
cargo test --lib batches_for_ -- --nocapture
cargo test --lib matches_when_all_four_equal -- --nocapture
# Expected: all pre-existing core.rs tests pass.
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
These are pure constants with no I/O, no parsing logic, and no CLI surface.
There is no live-hardware or runtime path to exercise until build_command_data
(P1.M2.T1), parse_reply (P1.M2.T2), and the burst_to_one reply capture
(P1.M3.T1) land. Nothing reads these constants at runtime yet. The contract-
pinning unit test in Level 2 IS the end-to-end verification for this task.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the constants are dead-code-clean in BOTH build and test configs
# (the compiler is the proof: a warning-free build means the #[allow(dead_code)]
# mitigation is correctly applied):
cargo build 2>&1 | grep -iE "CMD_DISCRIMINATOR|RESPONSE_MARKER|CMD_QUERY|CMD_SET_OS|CMD_APPLY|REPLY_READ_TIMEOUT|warning.*never used" \
  || echo "core.rs constants: no dead_code diagnostics (good)"

# Optional: confirm clippy sees nothing for the new constants:
cargo clippy --lib 2>&1 | grep -iE "CMD_DISCRIMINATOR|REPLY_READ" \
  || echo "clippy: constants clean (good)"

# Cross-check the values against the canonical contract by eye:
#   grep -nE "0xF0|0x51|0x01|0x02|0x03|0x05|1000" \
#     plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
# (REPLY_READ_TIMEOUT_MS=1000 is host-side, NOT in that file — expect no match.)
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings (no dead_code on new consts).
- [ ] Level 1 passed: `cargo clippy --lib` → zero warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 30 passed, 0 failed.
- [ ] The new test passes individually.

### Feature Validation

- [ ] All 7 constants exist with the exact names, values, and types specified.
- [ ] 6 wire constants are `pub(crate) const …: u8`; `REPLY_READ_TIMEOUT_MS` is
      `const …: i32`.
- [ ] Each constant has a `///` doc comment + a temporary `#[allow(dead_code)]`
      (doc → allow → item order).
- [ ] The block is placed immediately after `REPORT_LENGTH`, before
      `parse_hex_or_decimal`.
- [ ] The single contract-pinning test asserts all 6 wire values and
      `REPLY_READ_TIMEOUT_MS > 0`.
- [ ] Only `src/core.rs` is modified.

### Code Quality Validation

- [ ] Follows existing constants-region + rich-doc-comment conventions in core.rs.
- [ ] New test uses the file's descriptive naming style + existing `use super::*`.
- [ ] Test appended at the END of `mod tests` (no disturbance to existing tests).
- [ ] `#[allow(dead_code)]` ordering matches the core.rs:295-296 precedent.
- [ ] No module-level `#![allow(dead_code)]`; no lib.rs re-export; no Cargo.toml change.

### Documentation & Deployment

- [ ] Constants are self-documenting via `///` (Mode A — no separate docs file).
- [ ] Doc comments cite PRD §10.1/§10.2/§4.6 and the firmware wire contract.
- [ ] No new environment variables or config.
- [ ] No README/PRD change (constants are internal; contract DOCS = "none").

---

## Anti-Patterns to Avoid

- ❌ Don't paste the bare contract snippet **without** `#[allow(dead_code)]` — it
  emits 7 `dead_code` warnings in `cargo build` (empirically verified). The
  project's bar is zero warnings.
- ❌ Don't use a module-level `#![allow(dead_code)]` — it's far too broad (masks
  legitimate future dead_code across all of core.rs). Use per-constant allows.
- ❌ Don't put `#[allow(dead_code)]` ABOVE the `///` doc comment — rustfmt will
  fight you and it breaks the established core.rs:295-296 ordering (doc → allow
  → item).
- ❌ Don't drop the `#[allow(dead_code)]` "because the test references the
  constant" — a `#[cfg(test)]`-only reference does NOT silence dead_code in a
  non-test `cargo build` (the test module is absent there). Verified.
- ❌ Don't change `REPLY_READ_TIMEOUT_MS`'s type to `u64` or `Duration` — hidapi's
  `read_timeout` takes `i32`; keep it `i32` so P1.M3.T1 needs no cast.
- ❌ Don't promote `REPLY_READ_TIMEOUT_MS` to `pub(crate)` (or demote the 6 wire
  constants to private) — follow the contract's exact visibility.
- ❌ Don't add a lib.rs re-export or any lib.rs change — these are `pub(crate)`/
  private internal constants, not public API. (`CommandResponse`/`RunCommand`/
  `HostOs` in lib.rs are already complete from P1.M1.T1.* — leave them alone.)
- ❌ Don't modify any existing constant (`DEFAULT_*`, `REPORT_LENGTH`,
  `PAYLOAD_PER_REPORT`, `IN_DRAIN_MAX`, `SEND_RETRIES`) or any function — this
  task is purely additive.
- ❌ Don't implement `build_command_data` / `parse_reply` / the `burst_to_one`
  reply capture here — those are P1.M2/P1.M3. This task defines constants only.
- ❌ Don't add more than one test (the contract says "a unit test", singular) —
  one test covering all constants is the right scope; splitting adds noise.
- ❌ Don't skip `cargo build` / `cargo test` because "it's just constants" — the
  dead_code gotcha is exactly the kind of thing `cargo build` catches, and the
  contract-pin test is the drift guard that protects every downstream subtask.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is 7 fully-specified constants (verbatim values, types, visibility, doc comments)
plus one ready-to-paste contract-pinning test, placed against a unique stable
anchor that cannot collide with the already-landed lib.rs work (P1.M1.T1.* —
this task touches only core.rs). The single real risk — the bare contract snippet
producing `dead_code` warnings — is empirically characterized (with the exact
compile-time failure mode and the proven per-constant `#[allow(dead_code)]` fix,
matching an existing in-file precedent) so the implementer catches and resolves
it at the first `cargo build`. Baseline test count (29 → 30) and all
build/clippy/fmt/test commands are verified working in this repo.