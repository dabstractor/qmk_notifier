# PRP — P1.M1.T1.S3: Add CommandResponse enum to lib.rs

---

## Goal

**Feature Goal**: Add a standalone `pub enum CommandResponse` (5 variants) to
`src/lib.rs` — the crate's **parsed device-reply type**. It represents the result
of reading one 32-byte IN report after a command burst and is the value that
`parse_reply` (P1.M2.T2) will produce and that `run()` (P1.M1.T2.S2) will return.
The 5 variants map 1:1 to the firmware wire contract's reply disambiguation
(`firmware_wire_contract.md` §Reply Disambiguation / §Field Definitions; PRD §8,
§10.2): `Legacy { matched }`, `Info { proto_ver, feature_flags, callback_count,
board_rules_present }`, `CallbackName { index, name }`, `Ack { ok }`, `Timeout`.
Each variant carries the *parsed, structured* fields (NOT raw bytes); byte
decoding is the job of `parse_reply` (later subtask). The type must derive
`Debug, Clone, PartialEq, Eq` (exactly — see Gotchas), be Mode-A doc-commented
with the wire byte layout, and come with unit tests verifying construction of
every variant.

**Deliverable**: The new `pub enum CommandResponse` block (5 struct/unit variants
+ `#[derive(Debug, Clone, PartialEq, Eq)]` + Mode-A `///` doc comments referencing
PRD §8/§10.2 and the firmware wire contract) inserted into **`src/lib.rs`**
between `HostOs` and `RunParameters`, plus 3 new `#[test]` functions appended at
the **end** of the existing `#[cfg(test)] mod tests` block. **`src/lib.rs` is the
only file modified.** `run()` is NOT touched (its return-type change is
P1.M1.T2.S2).

**Success Definition**: `cargo build` compiles with **zero warnings**; `cargo
clippy --lib` shows **zero new warnings**; `cargo fmt --check` exits 0; `cargo
test --lib` passes with **29 tests** (26 post-S2 + 3 new); every `CommandResponse`
variant is constructible and round-trips through `match`/`assert_eq!`; the
mandated `PartialEq`/`Eq` derives are exercised by the tests; no file other than
`src/lib.rs` is modified; `RunCommand`, `HostOs`, `RunParameters`, `parse_cli_args`,
`run()`, and all `core::` re-exports are untouched.

## User Persona (if applicable)

**Target User**: Downstream implementer of the v0.3.0 typed-command transport —
specifically `parse_reply` (P1.M2.T2), which constructs these variants from raw
reply bytes, and `run()` (P1.M1.T2.S2 / P1.M3.T3.S1), which returns one — and,
ultimately, the `qmkonnect` desktop app that consumes `run()`'s result.

**Use Case**: After sending a typed command, the host reads a 32-byte reply and
parses it into a `CommandResponse`; callers then `match` on the variant to react
(e.g. `Timeout` ⇒ stay in string-only mode; `Info` ⇒ inspect capability flags).

**User Journey**: `RunCommand::QueryInfo` → (P1.M3.T3) `run()` → burst-write →
read one IN report → (P1.M2.T2) `parse_reply(bytes)` → returns
`CommandResponse::Info { proto_ver: 2, feature_flags: 0x03, ... }` → caller
decides typed-capable path.

**Pain Points Addressed**: Gives the reply side of the typed-command protocol a
typed, exhaustively-matchable representation (replacing raw `[u8; 32]`), so the
`0x51`-vs-`0/1`-vs-timeout disambiguation is expressed in the type system and
mis-parsing is caught at compile time.

## Why

- `CommandResponse` is the **reply counterpart** to `RunCommand` (the request
  side, extended in S2). PRD §3 defines it as a first-class public type; §8 /
  §10.2 specify its semantics. Without it, the v0.3.0 transport has no typed
  return value — `parse_reply` (P1.M2.T2) and the `run()` return-type change
  (P1.M1.T2.S2) have nothing to produce/return.
- It is **additive and standalone**: no hard dependencies (uses only std-prelude
  `bool`/`u8`/`Option<String>`), so it lands cleanly in parallel with S2. Defining
  it *before* its consumers keeps the dependency chain clean (types → pure
  framing/parse → transport), exactly mirroring how S1 defined `HostOs` before S2
  consumed it.
- The mandated `PartialEq`/`Eq` derives make this a *value-comparable result type*
  — essential for the `parse_reply` unit tests (P1.M2.T2) and the `run()` return
  tests, which assert equality against expected `CommandResponse` values.

## What

### The new enum (full target body — paste verbatim into `src/lib.rs`)

```rust
/// Parsed device reply (see PRD §8 and §10.2; canonical byte layouts in
/// `firmware_wire_contract.md` §Field Definitions and §Reply Disambiguation).
///
/// Produced by `parse_reply` (P1.M2.T2) from a single 32-byte IN report read
/// after a command burst. `response[0]` disambiguates the reply: `0x51` ⇒ typed
/// reply (decoded by the `response[1]` cmd echo); `0`/`1` ⇒ legacy match-bool;
/// no reply within the bounded `read_timeout` ⇒ `Timeout`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CommandResponse {
    /// Legacy string reply: `response[0]` is `0` (no match) or `1` (matched).
    /// Returned for `SendMessage`, and for a typed command answered by a
    /// non-capable (legacy) device that walks the typed bytes as a no-match
    /// string. See PRD §8, §10.2.
    Legacy { matched: bool },
    /// `QUERY_INFO` (cmd `0x01`) typed reply:
    /// `[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]`.
    /// See `firmware_wire_contract.md` §QUERY_INFO response.
    Info {
        proto_ver: u8,
        feature_flags: u8,
        callback_count: u8,
        board_rules_present: bool,
    },
    /// `QUERY_CALLBACK` (cmd `0x02`) typed reply:
    /// `[0x51][0x02][index][name bytes, NUL-padded]`. `name` is `None` when the
    /// callback has no name or the index is out of range (the firmware emits an
    /// immediate `0x00` NUL at the name position). See `firmware_wire_contract.md`
    /// §QUERY_CALLBACK response.
    CallbackName { index: u8, name: Option<String> },
    /// `SET_OS` (cmd `0x03`) / `APPLY_HOST_CONTEXT` (cmd `0x05`) typed reply:
    /// `[0x51][cmd_echo][ack]`. `ok` is `true` when `ack == 1` (applied). Shared
    /// by both ack-style commands. See `firmware_wire_contract.md` §SET_OS /
    /// §APPLY_HOST_CONTEXT response.
    Ack { ok: bool },
    /// No reply arrived within the bounded `read_timeout` — the device is legacy
    /// or offline. The caller treats this as a non-capable device and stays in
    /// string-only mode. See PRD §10.2, §8.
    Timeout,
}
```

### Placement anchor (exact)

Insert the block above **immediately before** the line:

```rust
/// Parameters required for running QMK notifier operations
#[derive(Debug, Clone)]
pub struct RunParameters {
```

i.e. between the FROZEN `HostOs` enum (S1 output — its closing `}`) and the
`RunParameters` doc comment. This matches the PRD §3 layout
(`RunCommand → HostOs → CommandResponse → RunParameters`) and uses the
`HostOs`/`RunParameters` boundary — a region untouched by the parallel S2 work
(S2 edits the `RunCommand` body *above* `HostOs` and `run()`'s match *below*) —
so the edit anchors never collide.

### The 3 new unit tests (append at the END of the existing `#[cfg(test)] mod tests`)

> Place these AFTER the existing last test (`test_run_with_verbose_output`) and
> BEFORE the closing `}` of the `mod tests` block. This is a **different anchor**
> than S2's middle-of-module insertion (after `test_host_os_discriminants_...`),
> avoiding any collision with the parallel S2 test edits.

```rust
#[test]
fn test_command_response_info_construction() {
    // QUERY_INFO reply: proto_ver=2 (typed-capable), feature_flags=0x03
    // (APPLY_HOST_CONTEXT | callback registry), 5 callbacks, board map present.
    let info = CommandResponse::Info {
        proto_ver: 2,
        feature_flags: 0x03,
        callback_count: 5,
        board_rules_present: true,
    };
    match info {
        CommandResponse::Info {
            proto_ver,
            feature_flags,
            callback_count,
            board_rules_present,
        } => {
            assert_eq!(proto_ver, 2);
            assert_eq!(feature_flags, 0x03);
            assert_eq!(callback_count, 5);
            assert!(board_rules_present);
        }
        _ => panic!("expected Info"),
    }
    // PartialEq/Eq derive (mandated by the item) must hold for the result type.
    assert_eq!(
        info,
        CommandResponse::Info {
            proto_ver: 2,
            feature_flags: 0x03,
            callback_count: 5,
            board_rules_present: true,
        }
    );
}

#[test]
fn test_command_response_callback_name_construction() {
    // Named callback: index echoed back, ASCII name present.
    let named = CommandResponse::CallbackName {
        index: 3,
        name: Some("layer_tap".to_string()),
    };
    match named {
        CommandResponse::CallbackName { index, name } => {
            assert_eq!(index, 3);
            assert_eq!(name.as_deref(), Some("layer_tap"));
        }
        _ => panic!("expected CallbackName"),
    }

    // Unnamed / out-of-range callback: firmware emits an immediate NUL ⇒ None.
    let unnamed = CommandResponse::CallbackName { index: 99, name: None };
    assert_eq!(
        unnamed,
        CommandResponse::CallbackName { index: 99, name: None }
    );
    assert_ne!(named, unnamed, "distinct index/name must not compare equal");
}

#[test]
fn test_command_response_legacy_ack_timeout_construction() {
    // Legacy match-bool reply (response[0] ∈ {0,1}).
    let matched = CommandResponse::Legacy { matched: true };
    let no_match = CommandResponse::Legacy { matched: false };
    assert_eq!(matched, CommandResponse::Legacy { matched: true });
    assert_ne!(matched, no_match);

    // SET_OS / APPLY_HOST_CONTEXT ack reply (ack==1 ⇒ applied).
    let ok = CommandResponse::Ack { ok: true };
    let fail = CommandResponse::Ack { ok: false };
    assert_eq!(ok, CommandResponse::Ack { ok: true });
    assert_ne!(ok, fail);

    // No reply within read_timeout (device legacy/offline).
    let t = CommandResponse::Timeout;
    assert_eq!(t, CommandResponse::Timeout);

    // Cross-variant inequality: different variants must never compare equal
    // (sanity-check the derived PartialEq across the whole enum).
    assert_ne!(
        CommandResponse::Timeout,
        CommandResponse::Ack { ok: false }
    );
}
```

### Success Criteria

- [ ] `pub enum CommandResponse` exists in `src/lib.rs` with exactly these 5
      variants in this order: `Legacy { matched: bool }`,
      `Info { proto_ver: u8, feature_flags: u8, callback_count: u8, board_rules_present: bool }`,
      `CallbackName { index: u8, name: Option<String> }`, `Ack { ok: bool }`,
      `Timeout`.
- [ ] Derives are **exactly** `#[derive(Debug, Clone, PartialEq, Eq)]` — no
      `Copy`, no `#[repr]`, no serde.
- [ ] The enum has a top-level `///` doc comment and **each variant** has a `///`
      doc comment naming its wire layout and citing PRD §8/§10.2 and/or
      `firmware_wire_contract.md` (Mode A).
- [ ] The enum is placed between `HostOs` and `RunParameters` (before the
      `/// Parameters required for running QMK notifier operations` line).
- [ ] 3 new `#[test]` fns exist at the END of the existing `mod tests` block;
      together they construct all 5 variants and assert equality via the derived
      `PartialEq`/`Eq`.
- [ ] `run()`, `RunCommand`, `HostOs`, `RunParameters`, `parse_cli_args`, all
      `core::` re-exports, and every other file are **unchanged**.
- [ ] `cargo build` → zero warnings; `cargo clippy --lib` → zero new warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → 29 passed.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact target enum body
> (verbatim, including derives, doc comments, field order, and types), the exact
> placement anchor (a unique, stable line), the exact 3 tests (verbatim), the
> source-of-truth wire contract for every variant, the parallel-sibling
> (S2) collision-avoidance strategy, and the verified build/test commands are all
> below. The implementer does not need to read any QMK firmware source —
> `firmware_wire_contract.md` canonicalizes every reply layout.

### Documentation & References

```yaml
# MUST READ — the canonical wire contract (reply layouts + disambiguation rules)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "Defines §Reply Disambiguation (0x51 vs 0/1 vs no-reply) and the exact
        field layouts for QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT
        replies. The variant field shapes and the Ack-shared-by-two-cmds and
        CallbackName-name-None semantics come directly from here."
  section: "Reply Disambiguation" and "Field Definitions" (all four response tables)
  critical: "CommandResponse is the PARSED form; it carries structured fields, NOT
             raw bytes. The byte→field decoding happens in parse_reply (P1.M2.T2),
             NOT here. Where this contract and any prose disagree, the contract wins."

# MUST READ — the file being edited (read current state before editing)
- file: src/lib.rs
  why: "Contains HostOs (S1 output, ~line 22 — the FROZEN type just above the
        insertion point), RunParameters (the struct whose doc comment is the
        insertion anchor), run() (must NOT be touched), and the #[cfg(test)]
        mod tests block at file bottom (where the 3 new tests append)."
  pattern: "Enum style: top-level `///` doc comment + `pub enum` + per-variant
            `///` doc comments (mirror HostOs's doc style). Tests use the
            already-present `use super::*;` and `test_<thing>_<scenario>` naming."
  gotcha: "RunCommand's derives are Debug,Clone ONLY — do NOT copy them onto
           CommandResponse. CommandResponse's derives are Debug,Clone,PartialEq,Eq
           (item-specified). See Known Gotchas."

# MUST READ — the parallel sibling PRP whose edits S3 must not collide with
- file: plan/001_b92a9b2b603f/P1M1T1S2/PRP.md
  why: "Defines exactly what S2 changes in src/lib.rs (extends RunCommand body;
        adds 4 todo!() arms to run(); adds 3 tests after the HostOs discriminant
        test). S3 uses DIFFERENT anchors (insert before RunParameters; append
        tests at module END) so the two parallel edits don't overlap."
  section: "What" (the enum body + run() match) and "Implementation Tasks"
  critical: "S3 must NOT extend RunCommand, must NOT touch run(), must NOT add the
             RunCommand construction tests — those are S2. S3 adds ONLY
             CommandResponse + its own tests."

# REFERENCE — PRD public API contract + reply semantics
- file: PRD.md
  why: "§3 gives the exact CommandResponse variant shapes and shows the intended
        file layout (RunCommand, HostOs, CommandResponse, RunParameters). §8
        (Response Handling) and §10.2 (Reply parsing) give the disambiguation
        rules the doc comments cite. §14 invariants 5/6 confirm the 0x51 marker."
  section: "3. Public API", "8. Response Handling", "10.2 Reply parsing", "14. Key Invariants"

# REFERENCE — research notes compiled for this subtask
- docfile: plan/001_b92a9b2b603f/P1M1T1S3/research/notes.md
  why: "Documents the derive decision (why PartialEq/Eq, why no Copy/repr), the
        placement resolution, the parallel-sibling collision-avoidance strategy,
        the variant→wire-byte mapping table, the test-count math (26→29), and the
        scope boundaries."
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
    └── lib.rs          # <-- FILE TO EDIT (insert CommandResponse; append 3 tests)
```

### Desired Codebase tree with files to be added/modified

```bash
src/
├── lib.rs   # MODIFIED ONLY — add CommandResponse enum (between HostOs and RunParameters)
│            #                   + 3 tests at end of #[cfg(test)] mod tests
└── (unchanged) core.rs, error.rs, main.rs, Cargo.toml, README.md
```

> No new files are created. All changes are in `src/lib.rs`.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: derives are Debug, Clone, PartialEq, Eq — EXACTLY (item-specified).
//   Do NOT copy RunCommand's derives (Debug, Clone only) and do NOT copy HostOs's
//   (Debug, Clone, Copy, PartialEq, Eq + #[repr(u8)]). CommandResponse gets
//   PartialEq/Eq (it's a result type — downstream parse_reply tests assert_eq on
//   it) but NO Copy (CallbackName owns Option<String> ⇒ Copy is impossible, E0204)
//   and NO #[repr] (it's a parsed value, not a wire type).

// CRITICAL: do NOT add Copy. CallbackName { name: Option<String> } owns a heap
//   String. #[derive(Copy)] fails to compile (E0204). Eq/PartialEq are fine
//   (String: Eq). If the compiler rejects Eq, re-check that you didn't add Copy.

// CRITICAL: do NOT change run()'s return type or body. The change to
//   Result<CommandResponse, QmkError> is P1.M1.T2.S2. S3 only DEFINES the type;
//   run() stays Result<(), QmkError> (with S2's todo!() arms if S2 has landed,
//   or the 2 legacy arms if it hasn't). CommandResponse must not be referenced
//   in run() in this subtask.

// NOTE: a pub enum that no code consumes yet does NOT trigger dead_code.
//   `pub` items are public API. `cargo build` emits zero warnings today and
//   stays that way. Do NOT add `#[allow(dead_code)]`.

// NOTE: clippy will NOT flag this enum. The largest variant field is
//   Option<String> (~24 bytes), far under clippy::large_enum_variant's 200-byte
//   default threshold. No `#[allow(clippy::large_enum_variant)]` needed.

// NOTE: placement must be between HostOs and RunParameters (NOT inside RunCommand
//   and NOT after RunParameters). The anchor `/// Parameters required for running
//   QMK notifier operations` is unique and stable. S2 edits RunCommand's body
//   (above HostOs) — a different region — so there is no anchor collision.

// NOTE: test placement is at the END of mod tests (after test_run_with_verbose_output),
//   NOT after test_host_os_discriminants_... (that is S2's insertion point).
//   Distinct anchors ⇒ the two parallel edits compose cleanly.

// NOTE: toml/dirs/serde in Cargo.toml are unused legacy deps (dropped in
//   P1.M4.T2.S1). Do NOT wire CommandResponse to serde in S3.
```

## Implementation Blueprint

### Data models and structure

This subtask adds **one** new public type — the `CommandResponse` enum — and no
behavior. The "data model" is entirely the enum body given verbatim in the "What"
section (5 variants, exact field names/order/types, exact derives, Mode-A doc
comments). There are no new structs, no constructors, no trait impls, no parsing
logic.

```rust
// The only structural change is the new CommandResponse enum block (see "What").
// Variants use inline struct-variant syntax (named fields) for Legacy/Info/
// CallbackName/Ack; Timeout is a unit variant. Field order MUST match the item
// description and the wire-layout order (e.g. Info: proto_ver, feature_flags,
// callback_count, board_rules_present — the order bytes appear in the reply).
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ the current src/lib.rs and confirm anchors
  - READ: the HostOs enum (S1 output) and its closing `}` — this is the type
          immediately ABOVE the insertion point. CONFIRM it is unchanged from S1
          (Unsure=0..Ios=4, repr(u8), Debug+Clone+Copy+PartialEq+Eq). Do NOT
          modify it.
  - READ: the `/// Parameters required for running QMK notifier operations` doc
          comment + `pub struct RunParameters` — this is the insertion anchor
          (CommandResponse goes immediately BEFORE it).
  - READ: the `#[cfg(test)] mod tests` block at the file bottom; locate the LAST
          test (`test_run_with_verbose_output`) — S3 tests append after it.
  - READ: run() — confirm you will NOT touch it (regardless of whether S2's
          todo!() arms are present yet).
  - GOAL: know the exact, unique anchors so the edits are surgical and don't
          collide with any S2 changes that may have landed.

Task 2: INSERT the CommandResponse enum into src/lib.rs
  - ADD: the full enum block from the "What" section (top-level `///` doc comment,
          `#[derive(Debug, Clone, PartialEq, Eq)]`, `pub enum CommandResponse`
          with all 5 variants + per-variant `///` doc comments) immediately
          BEFORE the `/// Parameters required for running QMK notifier operations`
          line (i.e. after HostOs's closing `}`, before RunParameters).
  - KEEP: RunCommand, HostOs, RunParameters, parse_cli_args, run(), all imports,
          and all `core::` re-exports byte-for-byte unchanged.
  - NAMING: enum `CommandResponse`; variants exactly `Legacy`, `Info`,
            `CallbackName`, `Ack`, `Timeout` (PascalCase; matches PRD §3 / the
            firmware reply vocabulary). Struct-variant fields exactly
            `matched`; `proto_ver`, `feature_flags`, `callback_count`,
            `board_rules_present`; `index`, `name`; `ok` (snake_case), in the
            order shown.
  - DO NOT: add `Copy`, `#[repr(...)]`, serde derives, a `Display` impl, a
            constructor fn, or any `From<[u8;32]>`. This is a pure type
            definition.

Task 3: APPEND 3 construction unit tests to the existing mod tests block
  - ADD: test_command_response_info_construction,
         test_command_response_callback_name_construction,
         test_command_response_legacy_ack_timeout_construction (see "What").
  - PLACEMENT: at the END of the existing `#[cfg(test)] mod tests { use super::*; ... }`
          block — after `test_run_with_verbose_output`, before the block's closing `}`.
          (Distinct from S2's middle-of-module insertion point.)
  - PATTERN: use the already-present `use super::*;` — do NOT re-import.
  - NAMING: snake_case test_<thing>_<scenario> (matches file convention).
  - COVERAGE: all 5 variants are constructed (Info; CallbackName w/ Some and
          None; Legacy, Ack, Timeout); each is round-tripped via match or
          assert_eq; the mandated PartialEq/Eq derives are exercised (assert_eq
          and assert_ne against freshly-built values).

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --lib`, then
          `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 new warnings; fmt --check exit 0;
          test result "29 passed; 0 failed" (26 post-S2 + 3 new). If S2 has not
          landed yet, expect 23 + 3 = 26 instead — still 0 failed.
  - IF E0204 (the trait Copy cannot be implemented): you accidentally added
          `Copy` to the derives — remove it.
  - IF "cannot find type ...": you placed CommandResponse inside another item or
          missed the `pub` — re-check placement.
```

### Implementation Patterns & Key Details

```rust
// === PLACEMENT ANCHOR (illustrative; the real file may have S2's RunCommand
//     variants above, but HostOs and RunParameters are stable) ===
//
// #[repr(u8)]
// #[derive(Debug, Clone, Copy, PartialEq, Eq)]
// pub enum HostOs { ... }                      // S1 output — FROZEN, DO NOT TOUCH
//
// // >>> INSERT the CommandResponse enum HERE (verbatim from "What") <<<
//
// /// Parameters required for running QMK notifier operations
// #[derive(Debug, Clone)]
// pub struct RunParameters { ... }


// === TEST PLACEMENT ANCHOR (illustrative) ===
//
//     #[test]
//     fn test_run_with_verbose_output() { ... }
//
//     // >>> APPEND the 3 CommandResponse tests HERE, before the closing } <<<
// }


// === WHY PartialEq/Eq BUT NOT Copy/repr ===
//   PartialEq, Eq → CommandResponse is a RESULT type; parse_reply tests and the
//                    run() return tests assert_eq on it. Mandatory (item-specified).
//   Copy          → IMPOSSIBLE: CallbackName owns Option<String> (heap). Never add.
//   #[repr(u8)]   → WRONG here: this is a parsed value, not a wire enum. HostOs
//                    uses repr(u8) because its discriminant IS the wire byte;
//                    CommandResponse's variants are structured, not discriminants.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY"
  - add:    "pub enum CommandResponse (5 variants + derives + Mode-A doc comments)"
  - add:    "3 #[test] fns at the END of the existing #[cfg(test)] mod tests block"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps, no new imports (Option/String are std-prelude).
    (Do NOT add serde derives — see gotchas.)

PUBLIC API SURFACE:
  - adds:    "CommandResponse::{ Legacy{matched}, Info{proto_ver,feature_flags,
              callback_count,board_rules_present}, CallbackName{index,name},
              Ack{ok}, Timeout }"
  - unchanged: "RunCommand (S2 owns it), HostOs, RunParameters, parse_cli_args,
                run signature/body, all core:: re-exports"

PARALLEL-SIBLING CONTRACT (P1.M1.T1.S2 — running concurrently):
  - S2 edits: "RunCommand body (adds QueryInfo/QueryCallback/SetOs/ApplyHostContext),
               run() match (adds 4 todo!() arms), and 3 tests after
               test_host_os_discriminants_match_firmware_contract."
  - S3 must NOT: extend RunCommand, touch run(), or add S2's RunCommand tests.
  - Collision avoidance: "S3 inserts CommandResponse at the HostOs/RunParameters
               boundary and appends tests at the END of mod tests — both distinct
               from S2's anchors."

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M1.T2.S2: "run() return type Result<(),QmkError> -> Result<CommandResponse,QmkError>."
  - P1.M2.T2.S1: "parse_reply decodes 0x51 replies into Info/CallbackName/Ack by
                  the response[1] cmd echo."
  - P1.M2.T2.S2: "parse_reply maps response[0]∈{0,1}->Legacy{matched}, no-reply
                  ->Timeout (and non-0x51/0/1 ->Timeout semantics)."
  - P1.M3.T3.S1: "run() dispatch returns the parsed CommandResponse to the caller."
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (rustfmt default style — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — must compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# If E0204 (Copy cannot be implemented): remove Copy from the derives.

# Lint (default clippy — no .clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors specific to CommandResponse.
# (largest field Option<String> ~24B ⇒ no large_enum_variant lint.)

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0 (no diff). If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the 3 new tests in isolation first.
cargo test --lib test_command_response_info_construction -- --nocapture
cargo test --lib test_command_response_callback_name_construction -- --nocapture
cargo test --lib test_command_response_legacy_ack_timeout_construction -- --nocapture
# Expected: 1 passed each.

# Run the full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 29 passed; 0 failed; 0 ignored; ..." (26 post-S2 + 3 new).
# (If S2 has NOT landed yet, expect 23 + 3 = 26 instead — still 0 failed.)

# Sanity: confirm the existing run()/RunCommand tests STILL pass untouched.
cargo test --lib test_run_with_ -- --nocapture
# Expected: test_run_with_list_devices_command, test_run_with_send_message_command,
#           test_run_with_verbose_output all pass.
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
CommandResponse is a pure data type with no I/O, no parsing logic, and no CLI
surface. There is no live-hardware path to exercise until parse_reply (P1.M2.T2)
and run() typed dispatch (P1.M3.T3.S1) land. Nothing constructs CommandResponse
from real bytes yet. The construction unit tests in Level 2 ARE the end-to-end
type-surface verification for this task.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the type surface is publicly reachable and the derives are correct
# (the compiler is the proof: a clean build + the assert_eq tests passing means
# Debug+Clone+PartialEq+Eq all derive cleanly and Copy was correctly omitted):
cargo build --lib 2>&1 | grep -iE "CommandResponse|error\[E0204\]|warning" || \
  echo "CommandResponse: no build diagnostics (good — derives valid, no warnings)"

# Optional: confirm clippy sees nothing to complain about for the new enum:
cargo clippy --lib 2>&1 | grep -iE "CommandResponse|large_enum_variant" || \
  echo "clippy: CommandResponse clean (good)"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings (no E0204 Copy error).
- [ ] Level 1 passed: `cargo clippy --lib` → zero new warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 29 passed, 0 failed (or 26 if S2 hasn't landed).
- [ ] The 3 new tests pass individually.

### Feature Validation

- [ ] `CommandResponse` has all 5 variants in the correct order with correct field types.
- [ ] `Legacy`/`Info`/`CallbackName`/`Ack` use inline struct-variant syntax (named fields);
      `Timeout` is a unit variant.
- [ ] Derives are **exactly** `Debug, Clone, PartialEq, Eq` (no Copy, no repr).
- [ ] The enum + each variant have `///` doc comments citing the wire layout (Mode A).
- [ ] The enum is placed between `HostOs` and `RunParameters`.
- [ ] `run()` is unchanged (return type stays `Result<(), QmkError>`).
- [ ] Only `src/lib.rs` is modified.

### Code Quality Validation

- [ ] Follows existing enum doc style (top-level `///` + per-variant `///`) — mirrors HostOs.
- [ ] New tests follow the file's `test_<thing>_<scenario>` naming + `use super::*`.
- [ ] Tests are appended at the END of `mod tests` (distinct from S2's anchor).
- [ ] No `#[allow(dead_code)]` / serde / Display / Copy / repr / constructor added.
- [ ] RunCommand, HostOs, RunParameters, parse_cli_args not modified (consumed as contracts).

### Documentation & Deployment

- [ ] Variants are self-documenting via `///` (Mode A — no separate docs file).
- [ ] Doc comments reference PRD §8/§10.2 and `firmware_wire_contract.md` sections.
- [ ] No new environment variables or config.
- [ ] No `Cargo.toml` change (no new deps).

---

## Anti-Patterns to Avoid

- ❌ Don't copy `RunCommand`'s derives (`Debug, Clone` only) — `CommandResponse`
  is item-specified as `Debug, Clone, PartialEq, Eq`. And don't copy `HostOs`'s
  derives (which include `Copy` + `#[repr(u8)]`) — `Copy` is impossible here and
  `repr` is meaningless for a parsed value.
- ❌ Don't add `Copy` — `CallbackName { name: Option<String> }` owns a heap
  `String`; `#[derive(Copy)]` fails to compile (E0204).
- ❌ Don't add `#[repr(u8)]` (or any `#[repr(...)]`) — this is a parsed value type,
  not a wire enum. Only `HostOs` uses `repr(u8)` because its discriminant IS the
  wire byte.
- ❌ Don't change `run()` — neither its signature nor its body. The return-type
  change to `Result<CommandResponse, QmkError>` is P1.M1.T2.S2. S3 only DEFINES
  the type.
- ❌ Don't extend `RunCommand` or add `RunCommand` tests — that's the parallel
  sibling S2. S3 adds only `CommandResponse` + its own tests.
- ❌ Don't implement `parse_reply` / any byte→variant decoding here — that's
  P1.M2.T2. S3 is type-surface only.
- ❌ Don't place `CommandResponse` inside `RunCommand`, above `HostOs`, or after
  `RunParameters`. It goes between `HostOs` and `RunParameters` (PRD §3 layout).
- ❌ Don't append tests after `test_host_os_discriminants_...` — that's S2's
  insertion point. Append at the END of the `mod tests` block instead.
- ❌ Don't add `#[allow(dead_code)]` — a `pub` enum with no consumers yet does not
  trigger `dead_code` (confirmed: S1's `HostOs` and S2's variants compile clean).
- ❌ Don't add serde derives — `toml`/`dirs`/`serde` are unused legacy deps
  (dropped in P1.M4.T2.S1); do not wire the new type to them.
- ❌ Don't reorder `Info`'s fields — the order `proto_ver, feature_flags,
  callback_count, board_rules_present` matches the item description and the
  byte order in the QUERY_INFO reply.
- ❌ Don't skip `cargo fmt` / `cargo test` because "it's just an enum" — the
  construction + equality tests are the contract check that protects every
  downstream `parse_reply`/`run()` task.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a fully-specified standalone enum (verbatim body, derives, Mode-A doc comments,
field order/types), placed against a unique stable anchor that cannot collide with
the parallel S2 edits, plus 3 ready-to-paste construction/equality tests appended
at a distinct anchor, with the source-of-truth wire contract quoted per variant and
verified build/clippy/fmt/test commands (and the exact 26→29 test-count math). The
one real risk — mis-deriving (`Copy`/`repr`) by copying a sibling — is called out
repeatedly with the compile-time failure mode (E0204) so the implementer catches
it at the first `cargo build`.