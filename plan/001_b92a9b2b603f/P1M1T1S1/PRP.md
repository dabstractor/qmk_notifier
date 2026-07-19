# PRP — P1.M1.T1.S1: Add HostOs enum to lib.rs

---

## Goal

**Feature Goal**: Add a standalone, public `HostOs` enum to `src/lib.rs` whose
variants and explicit `u8` discriminants exactly mirror QMK's `os_variant_t`
(`0=UNSURE, 1=LINUX, 2=WINDOWS, 3=MACOS, 4=IOS`). The enum must be a fieldless
`#[repr(u8)]` enum so that `HostOs::Windows as u8` yields `2` — the exact byte
that gets placed into a `SET_OS` report (`[0xF0][0x03][os_byte][0x03]`).

**Deliverable**: A new `pub enum HostOs` type (with enum-level + per-variant doc
comments and `#[derive(Debug, Clone, Copy, PartialEq, Eq)] #[repr(u8)]`) inserted
into `src/lib.rs` immediately after the existing `RunCommand` enum, plus one unit
test (`test_host_os_discriminants_match_firmware_contract`) inside the existing
`#[cfg(test)] mod tests` block asserting every discriminant value against the
firmware wire contract.

**Success Definition**: `cargo build` compiles with **zero warnings**; `cargo test --lib`
passes (23 tests, the new one included); `HostOs::Windows as u8 == 2` and likewise
for all five variants; no other file is modified and `RunCommand` is **untouched**
(extending it with `SetOs(HostOs)` is the *next* subtask, P1.M1.T1.S2).

## Why

- `HostOs` is the input type for the `SET_OS` (cmd `0x03`) typed command, which
  declares the host OS to the keyboard at connect time. It is the smallest
  building block of the v0.3.0 typed-command transport (PRD §3, §10).
- It is consumed downstream by `RunCommand::SetOs(HostOs)` (P1.M1.T1.S2) and by
  `build_command_data` (P1.M2.T1), which will emit `os_byte = HostOs::X as u8`
  as payload byte `[3]` of the SET_OS report.
- Defining it **first, in isolation** keeps the dependency chain clean:
  types (M1) → pure framing (M2) → transport (M3). It compiles and is testable on
  its own with no I/O and no hardware.
- It is **purely additive** — no existing code path, signature, or test changes.

## What

A new public enum in `src/lib.rs`:

```rust
/// Host operating system, mirrors QMK's `os_variant_t`.
/// Sent via SET_OS (cmd 0x03) to declare the host OS at connect.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HostOs {
    /// `0` — OS not yet detected / unknown. Mirrors QMK `OS_UNSURE`.
    Unsure = 0,
    /// `1` — Linux host. Mirrors QMK `OS_LINUX`.
    Linux = 1,
    /// `2` — Windows host. Mirrors QMK `OS_WINDOWS`.
    Windows = 2,
    /// `3` — macOS host. Mirrors QMK `OS_MACOS`.
    Macos = 3,
    /// `4` — iOS host. Mirrors QMK `OS_IOS`.
    Ios = 4,
}
```

Plus, in the existing `#[cfg(test)] mod tests` block (the one that already uses
`use super::*;`):

```rust
#[test]
fn test_host_os_discriminants_match_firmware_contract() {
    // Mirrors QMK os_variant_t and the SET_OS `os_byte` table in
    // plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md.
    assert_eq!(HostOs::Unsure as u8, 0);
    assert_eq!(HostOs::Linux as u8, 1);
    assert_eq!(HostOs::Windows as u8, 2);
    assert_eq!(HostOs::Macos as u8, 3);
    assert_eq!(HostOs::Ios as u8, 4);
}
```

### Success Criteria

- [ ] `pub enum HostOs` exists in `src/lib.rs` with all five variants and the
      exact discriminants `0..=4`.
- [ ] Enum carries `#[repr(u8)]` AND `#[derive(Debug, Clone, Copy, PartialEq, Eq)]`.
- [ ] Each variant has a `///` doc comment (Mode A documentation requirement).
- [ ] Enum is placed immediately after the existing `RunCommand` enum (before
      `RunParameters`).
- [ ] `RunCommand` is unchanged (no new variant added — that is S2).
- [ ] New unit test `test_host_os_discriminants_match_firmware_contract` exists
      in the existing `mod tests` block and asserts all five `as u8` casts.
- [ ] `cargo build` → zero warnings; `cargo test --lib` → all pass (23 tests).
- [ ] No file other than `src/lib.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact enum body,
 derives, placement anchor, the test to add, the validation commands, and the
 source-of-truth wire contract are all included below. The implementer does not
 need to read any QMK firmware source — `firmware_wire_contract.md`
 canonicalizes `os_variant_t`.

### Documentation & References

```yaml
# MUST READ — the canonical wire contract (os_byte table + SET_OS framing)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "Defines the SET_OS `os_byte` table (0=UNSURE,1=LINUX,2=WINDOWS,3=MACOS,
        4=IOS) and NOTIFY_CMD_SET_OS=0x03. This is the exact source of truth for
        every HostOs discriminant."
  section: "SET_OS request" and "Constants (from firmware notifier.h)"
  critical: "Discriminants must match the table EXACTLY. Where the firmware
             contract and any prose disagree, the firmware table wins."

# MUST READ — the file being edited (read before editing)
- file: src/lib.rs
  why: "Contains the `RunCommand` enum (the placement anchor) and the existing
        `#[cfg(test)] mod tests` block (where the new test goes)."
  pattern: "Enum style: `///` doc comments, `pub enum`, `#[derive(Debug, Clone)]`
            on the existing `RunCommand`. HostOs follows the same style but adds
            `Copy, PartialEq, Eq` and `#[repr(u8)]`."
  gotcha: "Do NOT modify `RunCommand`. This subtask adds a SEPARATE enum after it."

# REFERENCE — PRD public API contract (shows how HostOs is used downstream)
- file: PRD.md
  why: "§3 shows `SetOs(HostOs)` and `HostOs { Unsure=0, Linux=1, Windows=2,
        Macos=3, Ios=4 }`. Confirms this enum's role and naming."
  section: "3. Public API"

# REFERENCE — research notes compiled for this subtask
- docfile: plan/001_b92a9b2b603f/P1M1T1S1/research/test_conventions.md
  why: "Documents test placement/naming conventions, derive rationale, and the
        no-warnings / pub-item reasoning."
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
    ├── main.rs         # binary entrypoint (thin wrapper)
    ├── core.rs         # transport: list/send/parse helpers + core::tests
    ├── error.rs        # QmkError enum + Display + Error impl
    └── lib.rs          # <-- FILE TO EDIT: RunCommand, RunParameters, parse_cli_args, run, mod tests
```

### Desired Codebase tree with files to be added/modified

```bash
src/
├── lib.rs   # MODIFIED ONLY — insert HostOs enum + one unit test. No new files.
└── (unchanged) core.rs, error.rs, main.rs
```

> No new files are created in this subtask. `HostOs` lives in `lib.rs` (it is a
> public type re-exported from the crate root).

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: `#[repr(u8)]` is REQUIRED, not cosmetic.
//   - For a fieldless enum with explicit discriminants, `as u8` returns the
//     discriminant value. `#[repr(u8)]` additionally GUARANTEES the enum is
//     stored in exactly one byte (size_of::<HostOs>() == 1), which is what
//     the firmware wire contract assumes for `os_byte`. Omitting it leaves the
//     size implementation-defined. Keep `#[repr(u8)]`.

// CRITICAL: discriminants must EXACTLY mirror QMK os_variant_t.
//   Unsure=0, Linux=1, Windows=2, Macos=3, Ios=4.
//   Do NOT reorder or renumber. SET_OS sends `HostOs::X as u8` verbatim.

// NOTE: an unused-for-now `pub` enum does NOT trigger `dead_code` warnings.
//   `pub` items are part of the crate's public API. `cargo build` currently
//   emits zero warnings and will continue to do so. Do not add `#[allow(dead_code)]`.

// NOTE: `Copy` is idiomatic and safe here (fieldless, repr(u8)). It lets
//   downstream `match` and cast without borrowing. `PartialEq, Eq` enable
//   `assert_eq!(host_os_a, host_os_b)` in later tasks.

// NOTE: no serde in this subtask. `toml`/`serde`/`dirs` deps are unused legacy
//   deps (to be dropped in P1.M4.T2.S1) — do NOT wire HostOs to serde here.
```

## Implementation Blueprint

### Data models and structure

This subtask introduces exactly one type — a fieldless `#[repr(u8)]` enum. There
are no structs, no constructors, and no trait impls to add (the derives cover it).

```rust
// The enum (full body given in the "What" section above) is the entirety of the
// data model for this subtask. The `as u8` cast is the only conversion needed;
// no TryFrom<u8>/Display impl is required here (reply parsing is P1.M2.T2).
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/lib.rs and plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  - READ: the existing `RunCommand` enum (locate the exact insertion anchor —
          the `}` that closes `RunCommand`, which sits immediately before the
          `/// Parameters required for running QMK notifier operations` doc on
          `RunParameters`).
  - READ: the existing `#[cfg(test)] mod tests { use super::*; ... }` block
          (currently at the bottom of lib.rs, ending the file).
  - CONFIRM: the five os_byte values from the firmware contract (0..=4).
  - GOAL: know precisely where to insert code so the edit is surgical.

Task 2: INSERT the HostOs enum into src/lib.rs
  - INSERT: the full `pub enum HostOs { ... }` block (see "What" section).
  - PLACEMENT: immediately AFTER the closing `}` of the `RunCommand` enum and
    BEFORE the `/// Parameters required for running QMK notifier operations`
    doc comment on `RunParameters`. Leave one blank line of separation on each
    side (match the existing top-of-file spacing style).
  - ATTRIBUTES: exactly `#[repr(u8)]` then
    `#[derive(Debug, Clone, Copy, PartialEq, Eq)]` (attribute order as shown).
  - DOC: enum-level `///` doc comment (2 lines, as given) PLUS a `///` doc
    comment on EACH of the 5 variants (Mode A documentation requirement).
  - NAMING: variants are `Unsure, Linux, Windows, Macos, Ios` (note: `Macos`
    and `Ios` — not `MacOS`/`IOS` — to match the PRD §3 spelling).
  - DO NOT: touch `RunCommand`, `RunParameters`, `parse_cli_args`, or `run()`.
  - DO NOT: add any other variant, constant, or import.

Task 3: INSERT the discriminant unit test into the existing mod tests block
  - INSERT: `test_host_os_discriminants_match_firmware_contract` (see "What").
  - PLACEMENT: inside the existing `#[cfg(test)] mod tests { ... }` block in
    lib.rs, alongside the other `test_*` fns. Any position within the block is
    fine; placing it right after `test_run_parameters_creation` keeps related
    type-tests together.
  - PATTERN: `use super::*;` is already present — do NOT re-import.
  - NAMING: snake_case `test_host_os_discriminants_match_firmware_contract`
    follows the file's `test_<thing>_<scenario>` convention.
  - ASSERTIONS: exactly five `assert_eq!(HostOs::<Variant> as u8, <n>);` lines.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt` (format), then `cargo build`, then `cargo test --lib`.
  - EXPECT: build with 0 warnings; 23 tests pass (22 existing + 1 new).
  - IF a warning appears: read it; the most likely cause is a stray trailing
    comma or a missing attribute — fix and re-run.
```

### Implementation Patterns & Key Details

```rust
// === PLACEMENT ANCHOR (illustrative; match exact surrounding lines) ===
//
// pub enum RunCommand {
//     SendMessage(String),
//     ListDevices,
// }
//
// // >>> INSERT HostOs HERE (with a blank line above and below) <<<
//
// /// Parameters required for running QMK notifier operations
// pub struct RunParameters { ... }


// === THE TEST (goes inside the existing `#[cfg(test)] mod tests` block) ===
//
// The test mirrors the firmware contract's SET_OS `os_byte` table. If a future
// reader changes a discriminant, this test fails loudly — that is the intent.
#[test]
fn test_host_os_discriminants_match_firmware_contract() {
    assert_eq!(HostOs::Unsure as u8, 0);
    assert_eq!(HostOs::Linux as u8, 1);
    assert_eq!(HostOs::Windows as u8, 2);
    assert_eq!(HostOs::Macos as u8, 3);
    assert_eq!(HostOs::Ios as u8, 4);
}
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY"
  - add: "pub enum HostOs (after RunCommand, before RunParameters)"
  - add: "one #[test] fn inside the existing mod tests block"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps. (Do NOT add serde derives — see gotchas.)

PUBLIC API SURFACE:
  - adds: "qmk_notifier::HostOs (pub enum)"
  - unchanged: "RunCommand, RunParameters, parse_cli_args, run, all core:: re-exports"

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M1.T1.S2: "RunCommand::SetOs(HostOs) variant (next subtask)"
  - P1.M2.T1:    "build_command_data emits os_byte = HostOs::X as u8"
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (rustfmt, default style — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — must compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and no "warning:" lines.

# Optional but recommended: lint.
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors specific to HostOs.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0 (no diff). If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the new test in isolation first.
cargo test --lib test_host_os_discriminants_match_firmware_contract -- --nocapture
# Expected: 1 passed.

# Run the full lib test suite — confirms nothing regressed.
cargo test --lib
# Expected: "test result: ok. 23 passed; 0 failed; 0 ignored; ...".
# (22 pre-existing + 1 new.)
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
HostOs is a pure data type with no I/O, no HID calls, and no CLI surface. There
is no live-hardware path to exercise until build_command_data (P1.M2.T1) and
run() dispatch (P1.M3.T3) land. The discriminant unit test in Level 2 IS the
end-to-end contract verification for this type.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the type is publicly reachable from the crate root (the way
# downstream subtasks will consume it):
cargo build --lib 2>&1 | grep -i "HostOs" || echo "HostOs: no build diagnostics (good)"
# And a static sanity check that the discriminants cast as expected — this is
# already covered by the Level 2 test; no additional command needed.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 23 passed, 0 failed.
- [ ] New test `test_host_os_discriminants_match_firmware_contract` passes.

### Feature Validation

- [ ] `pub enum HostOs` present in `src/lib.rs` with variants Unsure/Linux/Windows/Macos/Ios.
- [ ] Discriminants are exactly `0,1,2,3,4` (matches firmware contract).
- [ ] `#[repr(u8)]` and `#[derive(Debug, Clone, Copy, PartialEq, Eq)]` both present.
- [ ] Each variant + the enum itself carry `///` doc comments.
- [ ] Enum placed after `RunCommand`, before `RunParameters`.
- [ ] `RunCommand` is byte-for-byte unchanged (S2 will extend it).
- [ ] Only `src/lib.rs` was modified.

### Code Quality Validation

- [ ] Follows existing enum doc/derive style in `lib.rs`.
- [ ] Test follows the file's `test_<thing>_<scenario>` naming + `assert_eq!` idiom.
- [ ] No `#[allow(dead_code)]` added (unnecessary for `pub` items).
- [ ] No serde/Display/TryFrom impls added (out of scope for this subtask).

### Documentation & Deployment

- [ ] Enum and variants are self-documenting via `///` (Mode A — no separate docs file).
- [ ] No new environment variables or config (pure type addition).
- [ ] No `Cargo.toml` change (no new deps).

---

## Anti-Patterns to Avoid

- ❌ Don't omit `#[repr(u8)]` — it guarantees the 1-byte layout the wire contract assumes.
- ❌ Don't reorder/renumber the variants to "look nicer" — discriminants are a wire contract.
- ❌ Don't modify `RunCommand` here — adding `SetOs(HostOs)` is P1.M1.T1.S2.
- ❌ Don't add serde derives / `Display` / `TryFrom<u8>` — reply parsing is P1.M2.T2.
- ❌ Don't create a separate `tests/` dir or a new module — inline test in the existing `mod tests` block matches the codebase convention.
- ❌ Don't add `#[allow(dead_code)]` — `pub` items don't trigger it.
- ❌ Don't rename variants to `MacOS`/`iOS` — use exactly `Macos`/`Ios` per PRD §3.
- ❌ Don't skip `cargo fmt` / `cargo test` because "it's just an enum" — the
  discriminant test is the contract check that protects every downstream task.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a single, fully-specified enum plus one test, placed against a precise anchor
in a single file, with the exact source-of-truth wire contract quoted and
verified working build/test commands.