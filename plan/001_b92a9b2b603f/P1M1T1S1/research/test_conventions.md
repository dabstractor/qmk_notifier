# Research Notes — P1.M1.T1.S1 (Add HostOs enum to lib.rs)

## Codebase structure (confirmed)
- Rust crate `qmk_notifier` v0.2.1, edition 2021.
- Source files: `src/main.rs`, `src/core.rs`, `src/error.rs`, `src/lib.rs`.
- NO separate `tests/` directory. All unit tests are inline `#[cfg(test)] mod tests`
  blocks inside the module under test.
  - `lib.rs` → `mod tests` (uses `use super::*;`)
  - `core.rs` → `core::tests`
- Currently 22 tests pass (`cargo test --lib`).

## Test conventions
- Naming: `test_<thing>_<scenario>` (snake_case), e.g. `test_run_parameters_creation`.
- Idioms in use: `assert_eq!`, `assert!`, `match` with `_ => panic!(...)`.
- Tests live at the bottom of the same file they exercise.

## Placement of HostOs
- Task says: "after the existing RunCommand enum definition".
- In `src/lib.rs`, `RunCommand` (SendMessage/ListDevices) ends right before the
  `/// Parameters required for running QMK notifier operations` / `RunParameters`
  struct. HostOs goes between those two.

## Derive attributes — what exists vs. what we add
- Existing `RunCommand` derives only `Debug, Clone`.
- HostOs derives `Debug, Clone, Copy, PartialEq, Eq` + `#[repr(u8)]`.
  - `Copy` is safe/idiomatic for a fieldless repr(u8) enum.
  - `PartialEq, Eq` enables `assert_eq!(HostOs::Linux, HostOs::Linux)` style tests
    in later tasks.
  - `#[repr(u8)]` guarantees 1-byte layout so `as u8` yields the explicit
    discriminant (the wire byte).

## Will adding an unused-for-now pub enum cause warnings?
- No. `pub` items are part of the public API and do not trigger `dead_code`
  warnings. `cargo build` currently compiles with zero warnings; it stays that way.

## Scope boundaries (DO NOT cross in this subtask)
- Do NOT modify `RunCommand` — adding `SetOs(HostOs)` is P1.M1.T1.S2.
- Do NOT add `build_command_data` — that is P1.M2.T1.
- Do NOT add framing constants to `core.rs` — that is P1.M1.T2.S1.
- This subtask is purely: add the standalone `HostOs` type + its discriminant test.

## Wire contract (confirmed source of truth)
`plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md` "SET_OS request"
table + "Constants" section:
- `os_byte`: 0=UNSURE, 1=LINUX, 2=WINDOWS, 3=MACOS, 4=IOS (mirrors QMK `os_variant_t`).
- `NOTIFY_CMD_SET_OS = 0x03`.
- SET_OS request layout: `[0xF0][0x03][os_byte][0x03]` (the trailing 0x03 ETX is
  appended by this crate; HostOs only provides `os_byte`).
- SET_OS reply: `[0x51][0x03][ack]` (ack=1 ⇒ applied). Parsing is a later task.

## QMK os_variant_t cross-check
QMK defines `os_variant_t` with exactly: `OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2,
OS_MACOS=3, OS_IOS=4`. The HostOs discriminants are an exact mirror. (No need to
fetch external URL; the firmware_wire_contract.md already canonicalizes this.)

## Build/test commands (verified working)
- `cargo build` — compiles (0 warnings).
- `cargo test --lib` — runs all 22 tests, all pass.
- `cargo fmt` / `cargo fmt --check` — rustfmt (standard toolchain; no project
  rustfmt.toml present, so default style).
- `cargo clippy` — available in default toolchain; optional but recommended.