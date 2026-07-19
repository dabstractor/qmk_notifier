# Research Notes — P1.M1.T1.S3 (Add CommandResponse enum to lib.rs)

## Task in one line
Add a standalone `pub enum CommandResponse` (5 variants) to `src/lib.rs` that
represents the parsed result of reading a device reply. Add unit tests verifying
construction of every variant. This is the **reply/result type** of the
typed-command transport.

## Standalone-ness (confirmed)
- "INPUT: No hard dependencies (standalone type)." `CommandResponse` uses only
  `bool`, `u8`, `Option<String>`, and `String` — all std-prelude. **No new
  imports, no new Cargo deps.**
- It does NOT depend on S2's new `RunCommand` variants; it only needs to live in
  the same file. Safe to implement in parallel with S2.
- Nothing in the current crate consumes it yet (consumers: `run()` return type in
  P1.M1.T2.S2; `parse_reply` in P1.M2.T2). `pub` enum ⇒ no `dead_code` warning
  (confirmed pattern: S1's `HostOs` and S2's new `RunCommand` variants compile
  clean with no consumers).

## Current `src/lib.rs` shape (read 2025-07-19)
Order of top-level items:
1. `mod core; pub use core::{...};`
2. `use clap::{...};`
3. `mod error; pub use error::QmkError;`
4. `/// Command types...` + `#[derive(Debug, Clone)] pub enum RunCommand { SendMessage(String), ListDevices }`
5. `/// Host operating system...` + `#[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)] pub enum HostOs {...}`  ← S1 output (FROZEN)
6. `/// Parameters required for running QMK notifier operations` + `pub struct RunParameters {...}` + `impl RunParameters`
7. `pub fn parse_cli_args() -> Result<RunParameters, QmkError>`
8. `pub fn run(params: RunParameters) -> Result<(), QmkError>` (2-arm match today; S2 makes it 6-arm with `todo!()` stubs)
9. `#[cfg(test)] mod tests { use super::*; ... }` — 10 tests today.

## What S2 (parallel sibling) changes — DO NOT duplicate / collide
Per `plan/001_b92a9b2b603f/P1M1T1S2/PRP.md`:
- Extends `RunCommand` IN PLACE (adds `QueryInfo`, `QueryCallback(u8)`,
  `SetOs(HostOs)`, `ApplyHostContext{layer,callbacks,clear_board}`) — edits
  INSIDE the enum body and the enum doc comment.
- Adds 4 `todo!()` arms to `run()`'s `match params.command`. Does **not** change
  `run()`'s return type (stays `Result<(), QmkError>`).
- Adds 3 construction tests **right after** `test_host_os_discriminants_match_firmware_contract`
  (i.e. a MIDDLE-of-module insertion).
- Does NOT touch `HostOs`, `RunParameters`, or `parse_cli_args`.

**Collision-avoidance strategy for S3:**
- S3 inserts `CommandResponse` **before** `pub struct RunParameters` (after the
  FROZEN `HostOs` enum). This anchor is untouched by S2. (S2 edits RunCommand's
  body, which is *above* HostOs — a different region.)
- S3 appends its tests at the **END** of the `mod tests` block (after the last
  existing test, `test_run_with_verbose_output`), i.e. a different anchor than
  S2's middle insertion. No collision.

## Placement decision (resolved)
Item says "after RunCommand" / "alongside RunCommand". PRD §3 source-of-truth
layout is:
```
RunCommand → HostOs → CommandResponse → RunParameters
```
So "after RunCommand" = "after HostOs, before RunParameters" — matches PRD §3 AND
uses the FROZEN HostOs boundary as a stable anchor for parallel execution. Both
readings of "after RunCommand" are satisfied (CommandResponse is below
RunCommand in the file; it's in the same command-vocabulary type family).

## Derive attributes — CRITICAL, differs from siblings
Item specifies EXACTLY: `#[derive(Debug, Clone, PartialEq, Eq)]`.
- `RunCommand` (S2) derives only `Debug, Clone`. Do NOT copy RunCommand's derives.
- `HostOs` (S1) derives `Debug, Clone, Copy, PartialEq, Eq` + `#[repr(u8)]`. Do NOT copy.
- `CommandResponse` derives `Debug, Clone, PartialEq, Eq` — NO `Copy`, NO `#[repr]`.
  - **Why no `Copy`:** `CallbackName { name: Option<String> }` owns a heap
    `String`; `Copy` is impossible (E0204). Do NOT add it.
  - **Why no `#[repr]`:** this is a Rust-side *parsed* value, not a wire type.
    The wire bytes are read by `parse_reply` (P1.M2.T2) and used to *construct*
    these variants. `#[repr(u8)]` (like HostOs) would be meaningless here.
  - **Why `PartialEq, Eq`:** CommandResponse is a *result* type. Downstream
    `parse_reply` unit tests and the `run()` return-type tests assert
    `assert_eq!(got, CommandResponse::Info{...})`. Equality comparison is
    essential for a response/result enum; that's why the item mandates it.
  - **Eq is legal here:** every field is `Eq` (`bool`, `u8`, `Option<String>`
    where `String: Eq`, unit). `#[derive(Eq)]` compiles cleanly.

## Variant → wire-byte mapping (canonical source: firmware_wire_contract.md)
| Variant | Trigger (`response[0]`) | Wire layout | Field semantics |
|---|---|---|---|
| `Legacy { matched }` | `0` or `1` | `response[0]` ∈ {0,1} | `matched = response[0]==1` |
| `Info {..}` | `0x51`, echo `0x01` | `[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]` | proto_ver(1=legacy,2=typed); feature_flags bitmask(0x01=APPLY_HOST_CONTEXT,0x02=callback registry,0x04=VIA reserved); callback_count; board_rules_present(0/1) |
| `CallbackName {..}` | `0x51`, echo `0x02` | `[0x51][0x02][index][name NUL-padded]` | index=echo; name=Some(ascii) unless name slot starts with `0x00` ⇒ `None` (no name OR out-of-range) |
| `Ack { ok }` | `0x51`, echo `0x03` or `0x05` | `[0x51][cmd_echo][ack]` | `ok = ack==1` (applied). Shared by SET_OS and APPLY_HOST_CONTEXT. |
| `Timeout` | *(no reply)* | — | device legacy/offline; caller stays in string-only mode |

Reply disambiguation also says: any `response[0]` that is NOT `0x51`/`0`/`1` is
treated as a non-capable device → `Timeout` semantics (parse_reply enforces this
later; S3 only defines the type).

## Does S3 touch `run()`?
**NO.** `run()` stays `Result<(), QmkError>` with S2's `todo!()` arms. The
return-type change to `Result<CommandResponse, QmkError>` is **P1.M1.T2.S2**
(explicitly noted in the item: "Consumed by run() return type (P1.M1.T2.S2)").
S3 only DEFINES the type; it wires nothing.

## Tests (convention + design)
- Convention: `#[cfg(test)] mod tests { use super::*; ... }` at file bottom;
  names `test_<thing>_<scenario>`; idioms `assert_eq!`, `match _ => panic!`.
- Currently 10 tests in lib.rs; core.rs has 13 → 23 total. After S2: lib.rs=13,
  total=26. After S3: lib.rs=13+N, total=26+N.
- Design: cover construction of ALL 5 variants + exercise the mandated
  `PartialEq`/`Eq` derives (downstream parse_reply tests depend on equality).
  3 focused tests, appended at END of `mod tests`:
  1. `test_command_response_info_construction`
  2. `test_command_response_callback_name_construction`
  3. `test_command_response_legacy_ack_timeout_construction`
  → N=3, total becomes 29.

## Build/test commands (verified working in this repo)
- `cargo build` → 0 warnings (today). Stays 0 after S3 (pub enum).
- `cargo clippy --lib` → no new warnings (no `large_enum_variant`: biggest field
  is `Option<String>` ~24B, well under the 200B lint threshold).
- `cargo fmt` / `cargo fmt --check` → default rustfmt (no rustfmt.toml).
- `cargo test --lib` → all pass.

## Scope boundaries (DO NOT cross in S3)
- Do NOT modify `RunCommand` — that's S2.
- Do NOT change `run()`'s signature/body — return-type change is P1.M1.T2.S2.
- Do NOT implement `parse_reply` — that's P1.M2.T2.
- Do NOT add framing constants to `core.rs` — that's P1.M1.T2.S1.
- Do NOT add serde / Display / TryFrom / any constructor fn — out of scope.
- Do NOT touch `HostOs`, `RunParameters`, `parse_cli_args`, or any other file.
- ONLY: add the `CommandResponse` enum to `src/lib.rs` + its 3 construction tests.