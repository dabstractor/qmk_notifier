# PRP — P1.M1.T1.S2: Extend RunCommand enum with typed-command variants

---

## Goal

**Feature Goal**: Extend the existing `pub enum RunCommand` in `src/lib.rs` (the
crate's typed-command surface) with four new variants — `QueryInfo`, `QueryCallback(u8)`,
`SetOs(HostOs)`, and `ApplyHostContext { layer, callbacks, clear_board }` — that
carry the host-side-rules typed-commands defined in PRD §3 / §10 and the firmware
wire contract §Command Table (cmd_ids `0x01`, `0x02`, `0x03`, `0x05`). Each
variant carries its typed, structured inputs (NOT pre-serialized wire bytes);
byte serialization is the job of `build_command_data` (P1.M2.T1). The variant
shapes must match the firmware wire contract's per-command argument layout so a
downstream payload builder can translate them 1:1 to `[0xF0][cmd_id][args…][0x03]`.

**Deliverable**: The extended `RunCommand` enum (4 new variants + `#[derive(Debug, Clone)]`
unchanged + Mode-A `///` doc comments on each variant referencing the cmd_id and
PRD §10.1), the 4 temporary `todo!()` arms added to the `match params.command`
in `run()` to restore exhaustiveness, and 3 new unit tests in the existing
`#[cfg(test)] mod tests` block verifying variant construction/pattern-matching.
All changes in **`src/lib.rs` only**.

**Success Definition**: `cargo build` compiles with **zero warnings** (including
`cargo clippy --lib`); `cargo test --lib` passes with **26 tests** (23 post-S1 +
3 new); each new variant is constructible and round-trips through `match`; the
existing `run()` tests (ListDevices / SendMessage / verbose) still pass; no file
other than `src/lib.rs` is modified; `HostOs`, `RunParameters`, `parse_cli_args`
are untouched.

## User Persona (if applicable)

**Target User**: Downstream implementer of the v0.3.0 typed-command transport
(`build_command_data` in P1.M2.T1, `parse_reply` in P1.M2.T2, `run()` dispatch in
P1.M3.T3) and, ultimately, the `qmkonnect` desktop app that calls this crate's
public API.

**Use Case**: Construct a typed command in host code, hand it to `RunParameters`,
and let the transport layer serialize+frame+send it. Today (S2) the API can
express every typed command; tomorrow (M2/M3) the transport knows how to send it.

**User Journey**: `RunCommand::SetOs(HostOs::Windows)` →
`RunParameters::new(...)` → (P1.M3.T3) `run()` → (P1.M2.T1)
`build_command_data` emits `[0xF0][0x03][0x02][0x03]` → `send_raw_report` →
firmware applies OS.

**Pain Points Addressed**: Removes the "everything is a magic string" limitation;
gives the type system (and the compiler's exhaustiveness check) jurisdiction over
the typed-command surface so wire-encoding bugs are caught at compile time.

## Why

- `RunCommand` is the **single source of truth** for "what can this crate ask the
  keyboard to do." Today it can only send a legacy `{class}\x1D{title}` string or
  list devices. The v0.3.0 milestone (PRD §3, §10) introduces four typed commands
  (QUERY_INFO, QUERY_CALLBACK, SET_OS, APPLY_HOST_CONTEXT) — this subtask makes
  them expressible in the public API.
- It is the **type layer** of the M1 "Type Contracts" milestone and depends only
  on S1's `HostOs` (for the `SetOs(HostOs)` variant). It is consumed downstream by
  `build_command_data` (P1.M2.T1), `parse_reply` (P1.M2.T2 — via the cmd echo),
  and `run()` dispatch (P1.M3.T3).
- Defining the variants **before** the serializer/dispatcher keeps the dependency
  chain clean (types → pure framing → transport) and lets each later subtask be
  validated against a fixed, compiling type surface.
- It is **additive to the type surface** — the only behavior change is the
  `todo!()` scaffolding in `run()` (necessary to keep the match exhaustive; see
  Gotchas), which is explicitly sanctioned by the item description and removed in
  P1.M1.T2.S2 / P1.M3.T3.S1.

## What

### The extended enum (full target body for `RunCommand`)

```rust
/// Command types for the QMK notifier.
///
/// `SendMessage`/`ListDevices` are the legacy path. The typed variants carry the
/// host-side-rules typed-command protocol (PRD §3, §10; framing in §10.1;
/// canonical wire layout in `firmware_wire_contract.md` §Command Table).
#[derive(Debug, Clone)]
pub enum RunCommand {
    /// Legacy path: send the `"{class}\x1D{title}"` window string (this crate
    /// appends the `0x03` ETX terminator before framing). Not a typed command.
    SendMessage(String),
    /// List all HID devices visible to hidapi (no keyboard I/O).
    ListDevices,

    /// Typed command `0x01` — `QUERY_INFO`. No request args. Replies with
    /// `[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]`.
    /// See PRD §10.1 (Framing) and `firmware_wire_contract.md` §Command Table.
    QueryInfo,
    /// Typed command `0x02` — `QUERY_CALLBACK`. `index` is the firmware callback
    /// registry slot to read. Replies with `[0x51][0x02][index][name, NUL-padded]`.
    /// See PRD §10.1 and `firmware_wire_contract.md` §Command Table.
    QueryCallback(u8),
    /// Typed command `0x03` — `SET_OS`. Declares the host OS to the keyboard at
    /// connect time. Serialized as `[0xF0][0x03][os_byte][0x03]` where
    /// `os_byte = HostOs::X as u8` (build_command_data, P1.M2.T1).
    /// See PRD §10.1 and `firmware_wire_contract.md` §SET_OS request.
    SetOs(HostOs),
    /// Typed command `0x05` — `APPLY_HOST_CONTEXT`. Pushes the host's desired
    /// layer + enabled-callback set + clear-board flag to the firmware in one
    /// atomic command. Serialized as
    /// `[0xF0][0x05][layer][flags][count][id0][id1]…[0x03]` (build_command_data,
    /// P1.M2.T1).
    ///
    /// - `layer: Option<u8>` — `None` ⇒ wire byte `0xFF` (clear host layer);
    ///   `Some(n)` ⇒ host-layer number (`>= 224` by convention, `HOST_LAYER_BASE`).
    /// - `callbacks: Vec<u8>` — the FULL desired enabled callback-id set; the
    ///   firmware diffs this against the current set (disable-before-enable).
    ///   Uncapped; may span multiple reports.
    /// - `clear_board: bool` — `true` ⇒ set firmware `flags` bit 0
    ///   (`clear_board`): firmware clears the board layer/command before applying.
    ///
    /// See PRD §10.1 and `firmware_wire_contract.md` §APPLY_HOST_CONTEXT request.
    ApplyHostContext {
        layer: Option<u8>,
        callbacks: Vec<u8>,
        clear_board: bool,
    },
}
```

### The `run()` match (4 new `todo!()` arms added — temporary scaffolding)

```rust
pub fn run(params: RunParameters) -> Result<(), QmkError> {
    match params.command {
        RunCommand::ListDevices => list_hid_devices(),
        RunCommand::SendMessage(message) => {
            /* EXISTING BODY — UNCHANGED (verbose print, ETX append, send_raw_report) */
        }

        // --- Typed-command stubs. Dispatch + reply handling land in P1.M3.T3.S1;
        // run()'s return type changes to `Result<CommandResponse, QmkError>` in
        // P1.M1.T2.S2. `todo!()` keeps this match exhaustive so the crate
        // compiles today. Existing tests only construct ListDevices/SendMessage
        // and never reach these arms. Do NOT wire real logic here. ---
        RunCommand::QueryInfo => todo!("typed dispatch lands in P1.M3.T3.S1"),
        RunCommand::QueryCallback(_) => todo!("typed dispatch lands in P1.M3.T3.S1"),
        RunCommand::SetOs(_) => todo!("typed dispatch lands in P1.M3.T3.S1"),
        RunCommand::ApplyHostContext { .. } => {
            todo!("typed dispatch lands in P1.M3.T3.S1")
        }
    }
}
```

> **NOTE**: Do **not** change `run()`'s return type (`Result<(), QmkError>`).
> The `CommandResponse` return type is P1.M1.T2.S2 (and `CommandResponse` itself
> is defined in P1.M1.T1.S3, a later sibling). `todo!()` (type `!`) coerces to
> `Result<(), QmkError>`, so the existing signature stays valid.

### The 3 new unit tests (inside the existing `#[cfg(test)] mod tests`)

```rust
#[test]
fn test_run_command_query_variants_construction() {
    // QueryInfo: unit variant — construct + match.
    let q = RunCommand::QueryInfo;
    assert!(matches!(q, RunCommand::QueryInfo));

    // QueryCallback(index): the u8 is the firmware callback-registry slot.
    let c = RunCommand::QueryCallback(5);
    match c {
        RunCommand::QueryCallback(index) => assert_eq!(index, 5),
        _ => panic!("expected QueryCallback"),
    }
}

#[test]
fn test_run_command_set_os_variant_construction() {
    // SetOs(HostOs): HostOs carries the os_byte source (verified separately by
    // test_host_os_discriminants_match_firmware_contract). Here we confirm the
    // payload round-trips through the variant.
    let s = RunCommand::SetOs(HostOs::Windows);
    match s {
        RunCommand::SetOs(os) => assert_eq!(os, HostOs::Windows),
        _ => panic!("expected SetOs"),
    }
}

#[test]
fn test_run_command_apply_host_context_construction() {
    // layer == None ⇒ clear-host-layer path (wire byte 0xFF).
    let clear = RunCommand::ApplyHostContext {
        layer: None,
        callbacks: vec![1, 2, 3],
        clear_board: true,
    };
    match clear {
        RunCommand::ApplyHostContext { layer, callbacks, clear_board } => {
            assert_eq!(layer, None, "None must mean clear-host-layer (0xFF)");
            assert_eq!(callbacks, vec![1, 2, 3]);
            assert!(clear_board, "clear_board flag must round-trip");
        }
        _ => panic!("expected ApplyHostContext"),
    }

    // layer == Some(n) ⇒ host-layer number (>= 224 by convention).
    let set = RunCommand::ApplyHostContext {
        layer: Some(224), // HOST_LAYER_BASE
        callbacks: Vec::new(),
        clear_board: false,
    };
    match set {
        RunCommand::ApplyHostContext { layer, callbacks, clear_board } => {
            assert_eq!(layer, Some(224));
            assert!(callbacks.is_empty());
            assert!(!clear_board);
        }
        _ => panic!("expected ApplyHostContext"),
    }
}
```

### Success Criteria

- [ ] `RunCommand` has exactly the 6 variants in the order: `SendMessage`,
      `ListDevices`, `QueryInfo`, `QueryCallback(u8)`, `SetOs(HostOs)`,
      `ApplyHostContext { layer: Option<u8>, callbacks: Vec<u8>, clear_board: bool }`.
- [ ] Each of the 4 new variants has a `///` doc comment naming its cmd_id and
      referencing PRD §10.1 / `firmware_wire_contract.md` §Command Table (Mode A).
- [ ] `#[derive(Debug, Clone)]` on `RunCommand` is **unchanged** (no PartialEq/Eq/Copy added).
- [ ] `run()`'s `match params.command` has 4 new `todo!()` arms (one per new
      variant, no `_ =>` wildcard) and compiles; `run()` return type is unchanged.
- [ ] 3 new `#[test]` fns exist in the existing `mod tests`; each constructs a
      variant and verifies via `match`/`matches!`.
- [ ] `cargo build` → zero warnings; `cargo clippy --lib` → zero new warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → 26 passed.
- [ ] Only `src/lib.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact target enum body,
> the exact `run()` match edit (with the non-obvious `todo!()` rationale), the
> exact 3 tests, the precise placement anchors, the source-of-truth wire
> contract, and the verified validation commands are all below. The implementer
> does not need to read any QMK firmware source — `firmware_wire_contract.md`
> canonicalizes every cmd_id and argument layout.

### Documentation & References

```yaml
# MUST READ — the canonical wire contract (cmd_id table + per-command layouts)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "Defines the Command Table (cmd_ids 0x01/0x02/0x03/0x05) and the exact
        argument layouts for SET_OS (os_byte) and APPLY_HOST_CONTEXT
        (layer/flags/count/id…). The variant field types/shapes and the
        ApplyHostContext layer=None⇒0xFF / clear_board⇒flags-bit-0 semantics
        come directly from here."
  section: "Command Table" and "SET_OS request" and "APPLY_HOST_CONTEXT request"
  critical: "ApplyHostContext field semantics are DOCUMENTED here in S2 but
             ENFORCED (translated to wire bytes) in P1.M2.T1. Do not serialize
             in S2. Where this contract and any prose disagree, the contract wins."

# MUST READ — the sibling PRP whose output S2 consumes (HostOs)
- file: plan/001_b92a9b2b603f/P1M1T1S1/PRP.md
  why: "Defines the exact HostOs enum (Unsure=0..Ios=4, repr(u8),
        Debug+Clone+Copy+PartialEq+Eq) that the SetOs(HostOs) variant depends on.
        Treat as a contract: HostOs will exist in src/lib.rs between RunCommand
        and RunParameters when S2 runs."
  section: "What" (the enum body) and "Integration Points"
  critical: "S2 must NOT redefine HostOs, must NOT re-add the HostOs test, and
             must NOT modify HostOs. S2 only extends RunCommand and adds run()
             todo!() arms + its own construction tests."

# MUST READ — the file being edited (read current state before editing)
- file: src/lib.rs
  why: "Contains RunCommand (the enum to extend, ~line 15), HostOs (S1 output,
        ~line 24), run() (the match to make exhaustive again, ~line 196), and the
        existing #[cfg(test)] mod tests block (where the 3 new tests go)."
  pattern: "Enum style: `///` doc comments + `pub enum` + `#[derive(Debug, Clone)]`.
            The new variants follow the same doc style. The new tests follow the
            existing test_<thing>_<scenario> naming + use the already-present
            `use super::*;`."
  gotcha: "Adding variants makes the run() match non-exhaustive (E0004). You MUST
           add the 4 todo!() arms shown above — without them `cargo build` fails."

# REFERENCE — PRD public API contract (shows the variant shapes)
- file: PRD.md
  why: "§3 gives the exact RunCommand variant shapes (incl. ApplyHostContext's
        three fields). §14 invariants 5/6 confirm typed commands reuse the
        0x81 0x9F 0xF0 framing and reply-marker 0x51."
  section: "3. Public API" and "14. Key Invariants"

# REFERENCE — research notes compiled for this subtask
- docfile: plan/001_b92a9b2b603f/P1M1T1S2/research/notes.md
  why: "Documents the compile-exhaustiveness gotcha, the todo!() decision, the
        derive math (why Debug+Clone survives all 4 variants), the test-count
        math (23→26), and the file boundary."
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
    ├── core.rs         # transport: list/send/parse helpers + core::tests (13 tests) — DO NOT TOUCH
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # <-- FILE TO EDIT (RunCommand, run(), mod tests)
```

### Desired Codebase tree with files to be added/modified

```bash
src/
├── lib.rs   # MODIFIED ONLY — extend RunCommand, add 4 todo!() arms in run(), add 3 tests
└── (unchanged) core.rs, error.rs, main.rs, Cargo.toml, README.md
```

> No new files are created. All changes are in `src/lib.rs`.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: adding variants to RunCommand breaks run()'s match exhaustiveness.
//   `run()` currently matches exactly `ListDevices` and `SendMessage`. Adding
//   QueryInfo/QueryCallback/SetOs/ApplyHostContext triggers E0004 (non-exhaustive
//   match). You MUST add the 4 todo!() arms shown in the "What" section. This is
//   explicitly sanctioned by the item description and is temporary scaffolding
//   removed in P1.M1.T2.S2 (return type) / P1.M3.T3.S1 (real dispatch).

// CRITICAL: use `todo!()`, and use ONE explicit arm per variant (NOT `_ =>`).
//   - `todo!()` (not `unimplemented!()`) is the idiomatic "dispatch lands later"
//     placeholder; it returns `!`, which coerces to the arm's `Result<(), QmkError>`.
//   - Explicit arms keep the compiler's exhaustiveness check meaningful as the
//     enum grows. A `_ =>` wildcard would silently swallow a future variant.
//   - `clippy::todo` is in clippy's `restriction` group (allow by default), so
//     `cargo clippy --lib` stays green. Do NOT add `#[allow(clippy::todo)]`.

// CRITICAL: do NOT change run()'s return type in S2.
//   It stays `Result<(), QmkError>`. Changing to `Result<CommandResponse, _>`
//   is P1.M1.T2.S2, and CommandResponse is defined in P1.M1.T1.S3 (later sibling).
//   todo!() arms make the current signature compile unchanged.

// NOTE: `#[derive(Debug, Clone)]` on RunCommand stays AS-IS.
//   All 4 new variants satisfy Debug+Clone: QueryInfo (unit), QueryCallback(u8),
//   SetOs(HostOs) — HostOs derives Debug+Clone (S1), and ApplyHostContext's
//   Option<u8>/Vec<u8>/bool all impl Debug+Clone. Do NOT add PartialEq/Eq/Copy
//   (RunCommand owns a String → Copy is impossible; item description says
//   "match existing derives" = Debug+Clone only).

// NOTE: an unused-for-now `pub` enum variant does NOT trigger `dead_code`.
//   `pub` items are public API. `cargo build` currently emits zero warnings and
//   will continue to do so. Do NOT add `#[allow(dead_code)]`.

// NOTE: the 3 new tests must NOT call run() with a typed variant.
//   run() dispatch for typed commands is todo!() (panics). Constructing a
//   typed RunCommand and handing it to run() would panic. Tests are CONSTRUCTION
//   tests only (per item description). Do NOT add a #[should_panic] test — it
//   would only test temporary scaffolding.

// NOTE: HostOs must already exist (S1 output) before S2 compiles.
//   Verified present in the current working tree at src/lib.rs ~line 24. S2
//   reuses it as-is; do NOT redefine, reorder, or re-derive HostOs.

// NOTE: toml/dirs/serde in Cargo.toml are unused legacy deps (dropped in
//   P1.M4.T2.S1). Do NOT wire any new variant to serde in S2.
```

## Implementation Blueprint

### Data models and structure

This subtask adds **no new types** — it extends an existing enum with variants
and adds temporary dispatch stubs. The "data model" is entirely the `RunCommand`
variant shapes given in the "What" section (verbatim, including field order and
types for `ApplyHostContext`). There are no new structs, no constructors, no
trait impls.

```rust
// The only structural change is the RunCommand enum body (see "What").
// ApplyHostContext uses an inline struct-variant (named fields) — NOT a tuple
// variant and NOT a separate struct. Field order MUST be layer, callbacks,
// clear_board (matches PRD §3 and the wire layout order layer/flags/count/id).
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ the current src/lib.rs and confirm S1 output
  - READ: the RunCommand enum (~line 15) — confirm it has exactly SendMessage,
          ListDevices and `#[derive(Debug, Clone)]`.
  - READ: the HostOs enum (~line 24) — CONFIRM it exists with Unsure=0..Ios=4
          and `#[derive(Debug, Clone, Copy, PartialEq, Eq)]`. (If HostOs is
          ABSENT, STOP: S1 has not landed; S2 cannot compile. Do NOT define
          HostOs yourself — that duplicates S1.)
  - READ: run() (~line 196) — confirm `match params.command` has exactly 2 arms.
  - READ: the `#[cfg(test)] mod tests` block at file bottom.
  - GOAL: know the exact anchors so edits are surgical.

Task 2: EXTEND the RunCommand enum body in src/lib.rs
  - ADD: 4 new variants (QueryInfo, QueryCallback(u8), SetOs(HostOs),
          ApplyHostContext { layer, callbacks, clear_board }) AFTER ListDevices,
          in that order, with the `///` doc comments from the "What" section.
  - KEEP: the existing `#[derive(Debug, Clone)]` and the existing doc comment on
          the enum (extend the top doc comment to mention the typed variants —
          see "What").
  - KEEP: SendMessage(String) and ListDevices byte-for-byte unchanged.
  - NAMING: variants exactly QueryInfo, QueryCallback, SetOs, ApplyHostContext
            (PascalCase; matches PRD §3 / firmware cmd names). ApplyHostContext
            fields exactly `layer`, `callbacks`, `clear_board` (snake_case), in
            that order.
  - DO NOT: touch HostOs, RunParameters, parse_cli_args, or any import.

Task 3: RESTORE run() match exhaustiveness with todo!() arms
  - ADD: 4 arms to `match params.command` in run() — one per new variant, each
          `=> todo!("typed dispatch lands in P1.M3.T3.S1")`. See "What" for the
          exact code (note ApplyHostContext uses the `{ .. }` pattern).
  - KEEP: run()'s signature `pub fn run(params: RunParameters) -> Result<(), QmkError>`.
  - KEEP: the existing ListDevices and SendMessage arms byte-for-byte unchanged.
  - DO NOT: add a `_ =>` wildcard arm. DO NOT add real dispatch logic.
  - DO NOT: change the return type (CommandResponse is P1.M1.T2.S2).

Task 4: ADD 3 construction unit tests to the existing mod tests block
  - ADD: test_run_command_query_variants_construction,
         test_run_command_set_os_variant_construction,
         test_run_command_apply_host_context_construction (see "What").
  - PLACEMENT: inside the existing `#[cfg(test)] mod tests { use super::*; ... }`
          block; place them right after the existing HostOs discriminant test
          (test_host_os_discriminants_match_firmware_contract) to group the new
          type-surface tests together.
  - PATTERN: use the already-present `use super::*;` — do NOT re-import.
  - NAMING: snake_case test_<thing>_<scenario> (matches file convention).
  - DO NOT: add a test that calls run() with a typed variant (would panic on
          todo!()). DO NOT add #[should_panic].

Task 5: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --lib`, then
          `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 new warnings; fmt --check exit 0;
          test result "26 passed; 0 failed" (23 post-S1 + 3 new).
  - IF E0004 (non-exhaustive match): you forgot a todo!() arm — add it.
  - IF "cannot find type HostOs": S1 did not land — STOP and surface it; do NOT
          define HostOs in S2.
```

### Implementation Patterns & Key Details

```rust
// === PLACEMENT ANCHOR (illustrative; match exact surrounding lines) ===
//
// #[derive(Debug, Clone)]
// pub enum RunCommand {
//     SendMessage(String),
//     ListDevices,
//     // >>> ADD the 4 new variants HERE, with /// doc comments <<<
//     // >>> (after ListDevices, before the closing }) <<<
// }
//
// pub enum HostOs { ... }   // S1 output — DO NOT TOUCH
//
// pub struct RunParameters { ... }


// === run() MATCH ANCHOR (illustrative) ===
//
// pub fn run(params: RunParameters) -> Result<(), QmkError> {
//     match params.command {
//         RunCommand::ListDevices => list_hid_devices(),
//         RunCommand::SendMessage(message) => { /* unchanged body */ }
//         // >>> ADD 4 todo!() arms HERE, before the closing } <<<
//     }
// }


// === WHY todo!() NOT unimplemented!() / _ => ===
//   todo!()        → idiomatic "dispatch later"; type `!`; coerces anywhere.
//   unimplemented!→ same panic, weaker intent (use todo! for "planned").
//   `_ =>`         → AVOID: silently hides a future 7th variant from the
//                    exhaustiveness check. Explicit arms keep the check sharp.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY"
  - extend: "pub enum RunCommand (4 new variants + doc comments)"
  - modify: "run() — add 4 todo!() match arms (no signature change)"
  - add:    "3 #[test] fns inside the existing #[cfg(test)] mod tests block"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps. (Do NOT add serde derives — see gotchas.)

PUBLIC API SURFACE:
  - adds:    "RunCommand::QueryInfo, RunCommand::QueryCallback(u8),
              RunCommand::SetOs(HostOs),
              RunCommand::ApplyHostContext { layer, callbacks, clear_board }"
  - unchanged: "SendMessage, ListDevices, HostOs, RunParameters, parse_cli_args,
                run signature, all core:: re-exports"

PARALLEL-SIBLING CONTRACT (P1.M1.T1.S1):
  - consumes: "HostOs enum (Unsure=0..Ios=4, repr(u8), Debug+Clone+Copy+PartialEq+Eq).
               Must already exist in src/lib.rs between RunCommand and RunParameters."

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M2.T1.S1: "build_command_data serializes QueryInfo/QueryCallback/SetOs to
                  [0xF0][cmd][args][0x03]; single report each."
  - P1.M2.T1.S2: "build_command_data serializes ApplyHostContext: layer None⇒0xFF,
                  Some(n)⇒n; clear_board⇒flags bit 0; callbacks as [count][id…]
                  (may span multiple reports)."
  - P1.M2.T2.S1: "parse_reply decodes 0x51 replies; response[1] cmd echo maps back
                  to these variants."
  - P1.M3.T3.S1: "run() dispatch: replaces the 4 todo!() arms with real
                  build_command_data → send_raw_report → parse_reply."
  - P1.M1.T2.S2: "run() return type Result<(),QmkError> → Result<CommandResponse,QmkError>;
                  removes/replaces these todo!() arms."
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (rustfmt default style — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — must compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# If you see error[E0004]: non-exhaustive patterns — you forgot a todo!() arm.

# Lint (default clippy — no .clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors specific to the new variants or todo!() arms.
# (clippy::todo is allow-by-default, so todo!() is NOT flagged.)

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0 (no diff). If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the 3 new tests in isolation first.
cargo test --lib test_run_command_query_variants_construction -- --nocapture
cargo test --lib test_run_command_set_os_variant_construction -- --nocapture
cargo test --lib test_run_command_apply_host_context_construction -- --nocapture
# Expected: 1 passed each.

# Run the full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 26 passed; 0 failed; 0 ignored; ...".
# (23 post-S1: 10 in lib.rs + 13 in core.rs; + 3 new in lib.rs = 26.)

# Sanity: confirm the existing run() integration tests STILL pass (they must not
# hit a todo!() arm).
cargo test --lib test_run_with_ -- --nocapture
# Expected: test_run_with_list_devices_command, test_run_with_send_message_command,
#           test_run_with_verbose_output all pass (Ok or benign Err, never panic).
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
The new variants carry structured data only — they have no I/O and no CLI surface
yet. There is no live-hardware path to exercise until build_command_data
(P1.M2.T1) and run() typed dispatch (P1.M3.T3.S1) land. Calling run() with a
typed variant today would hit todo!() and panic (by design). The construction
unit tests in Level 2 ARE the end-to-end type-surface verification for this task.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the type surface is publicly reachable and the match is exhaustive
# (the compiler enforces exhaustiveness; a clean build IS the proof):
cargo build --lib 2>&1 | grep -iE "RunCommand|non-exhaustive|warning" || \
  echo "RunCommand: no build diagnostics (good — exhaustive match, no warnings)"

# Optional: confirm clippy sees the todo!() arms as acceptable (no warning):
cargo clippy --lib 2>&1 | grep -i "todo" || echo "clippy: todo!() not flagged (good)"

# Optional: static proof the new variants pattern-match correctly — already
# covered by the Level 2 construction tests; no additional command needed.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings (no E0004 non-exhaustive).
- [ ] Level 1 passed: `cargo clippy --lib` → zero new warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 26 passed, 0 failed.
- [ ] The 3 new tests pass individually.
- [ ] The 3 existing `test_run_with_*` tests still pass (no todo!() panic).

### Feature Validation

- [ ] `RunCommand` has all 6 variants in the correct order with correct field types.
- [ ] `ApplyHostContext` is an inline struct variant with fields `layer: Option<u8>`,
      `callbacks: Vec<u8>`, `clear_board: bool` in that order.
- [ ] Each new variant has a `///` doc comment naming its cmd_id and PRD §10.1.
- [ ] `run()` has 4 new `todo!()` arms (explicit, one per variant; no `_ =>`).
- [ ] `run()` signature unchanged (`Result<(), QmkError>`).
- [ ] `#[derive(Debug, Clone)]` on RunCommand unchanged (no new derives added).
- [ ] Only `src/lib.rs` modified.

### Code Quality Validation

- [ ] Follows existing enum doc style (`///` per variant) and derive conventions.
- [ ] New tests follow the file's `test_<thing>_<scenario>` naming + `use super::*`.
- [ ] No `#[allow(dead_code)]` / `#[allow(clippy::todo)]` added (unnecessary).
- [ ] No serde/Display/TryFrom/serialization logic added (out of scope — P1.M2.T1).
- [ ] HostOs not redefined/rederived (consumed from S1 as a contract).

### Documentation & Deployment

- [ ] Variants are self-documenting via `///` (Mode A — no separate docs file).
- [ ] ApplyHostContext doc encodes layer=None⇒0xFF and clear_board⇒flags-bit-0 semantics.
- [ ] No new environment variables or config.
- [ ] No `Cargo.toml` change (no new deps).

---

## Anti-Patterns to Avoid

- ❌ Don't forget the 4 `todo!()` arms — the match becomes non-exhaustive (E0004)
  and `cargo build` fails. Add them in the same change as the variants.
- ❌ Don't use `_ => todo!()` — a wildcard hides future variants from the
  exhaustiveness check. Use one explicit arm per variant.
- ❌ Don't change `run()`'s return type to `CommandResponse` — that's P1.M1.T2.S2,
  and `CommandResponse` is defined in P1.M1.T1.S3 (later sibling).
- ❌ Don't add real typed-command dispatch logic in the `todo!()` arms — dispatch
  is P1.M3.T3.S1. S2 only makes the match compile.
- ❌ Don't add `PartialEq`/`Eq`/`Copy` to RunCommand's derives — `RunCommand`
  owns a `String` (Copy impossible) and the item says "match existing derives".
- ❌ Don't redefine/re-derive/modify `HostOs` — consume it from S1 as a contract.
- ❌ Don't add `#[allow(dead_code)]` — `pub` enum variants don't trigger it.
- ❌ Don't serialize the variants to wire bytes here — `build_command_data` is
  P1.M2.T1. S2 is type-surface only.
- ❌ Don't add a test that calls `run()` with a typed variant (it would panic on
  `todo!()`), and don't add a `#[should_panic]` test (it only tests scaffolding).
- ❌ Don't reorder `ApplyHostContext` fields to flags-first — the order MUST be
  `layer, callbacks, clear_board` (matches PRD §3 and the wire layout).
- ❌ Don't skip `cargo fmt` / `cargo test` because "it's just enum variants" —
  the construction tests are the contract check that protects every downstream task.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a fully-specified enum extension (verbatim variant bodies + doc comments), a
fully-specified `run()` edit (with the non-obvious `todo!()` rationale and the
explicit "don't change the return type" guard), and 3 ready-to-paste tests,
placed against precise anchors in a single file, with the source-of-truth wire
contract quoted and verified working build/clippy/fmt/test commands (and the
exact 23→26 test-count math). The one real risk — the exhaustiveness break — is
called out three times and fixed by the same edit.