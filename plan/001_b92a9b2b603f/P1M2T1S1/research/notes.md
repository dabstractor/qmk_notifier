# Research Notes — P1.M2.T1.S1: build_command_data (SendMessage + single-report typed)

## Task in one line

Add a pure `pub(crate) fn build_command_data(command: &RunCommand) -> Vec<u8>` to
`src/core.rs` that produces the logical payload bytes for every `RunCommand`
variant **except `ApplyHostContext`** (that arm is a `todo!()` stub for S2).
Define a private `const ETX_TERMINATOR_BYTE: u8 = 0x03`. Add exhaustive unit
tests. `src/core.rs` is the only file modified.

## Dependency state assumed at implementation time (from `<plan_status>` + live code)

| Item | Status | What it gives this task |
|---|---|---|
| P1.M1.T1.S2 (RunCommand variants) | **Complete** | `RunCommand` enum with `SendMessage(String)`, `ListDevices`, `QueryInfo`, `QueryCallback(u8)`, `SetOs(HostOs)`, `ApplyHostContext{..}` — all in `src/lib.rs`. |
| P1.M1.T1.S1 (HostOs) | **Complete** | `#[repr(u8)] HostOs { Unsure=0, Linux=1, Windows=2, Macos=3, Ios=4 }` — so `*os as u8` is a valid cast. |
| P1.M1.T2.S1 (framing constants) | **Complete** | `CMD_DISCRIMINATOR=0xF0`, `RESPONSE_MARKER=0x51`, `CMD_QUERY_INFO=0x01`, `CMD_QUERY_CALLBACK=0x02`, `CMD_SET_OS=0x03`, `CMD_APPLY_HOST_CONTEXT=0x05`, `REPLY_READ_TIMEOUT_MS=1000` — all in `core.rs`, each with a temporary `#[allow(dead_code)]`. |
| P1.M1.T2.S2 (run() → CommandResponse) | **Implementing** (parallel; treat as contract) | `run()` returns `Result<CommandResponse, QmkError>`; SendMessage/ListDevices arms build placeholder responses; the 4 typed arms stay `todo!()`. **run() is NOT migrated to call build_command_data here** (that's P1.M3.T3). |

Verified live in `src/core.rs` (constants block at lines ~13-43) and `src/lib.rs`
(`run()` returns `Result<CommandResponse, _>`; `RunCommand`/`HostOs` defined at
crate root). The only inconsistency vs. the S2 PRP contract is the test arm
`Ok(()) =>` in `test_run_with_send_message_command` — that is S2's open work and
will be `Ok(_) =>` by the time this task starts. **This task does not touch
lib.rs**, so it is unaffected.

## The one real technical risk: dead_code (EMPIRICALLY RESOLVED)

`build_command_data` has **no live caller** in this subtask — `run()` still builds
its ETX inline (S2) and is migrated to call `build_command_data` only in P1.M3.T3.
So in a non-test `cargo build`, `build_command_data` is unreachable. A
`pub(crate)` item with no live caller → `dead_code` warning (same root cause as
the constants in P1.M1.T2.S1).

**Two experiments (`/tmp/deadcode_test`, `/tmp/deadcode_test2`) established:**

1. Marking the function `#[allow(dead_code)]` silences the warning for the
   function itself.
2. **A `#[allow(dead_code)]` function that references a constant makes that
   constant "used"** — the constant emits **no** `dead_code` warning in
   `cargo build`, `cargo clippy`, or `cargo test`, **even after its own
   `#[allow(dead_code)]` is removed.** Deadness does not propagate "downward"
   through an allow-dead item to the items it references.
3. A redundant/leftover `#[allow(dead_code)]` does NOT itself warn.

**Conclusion — the correct, verified-clean strategy:**

| Item | Action | Reason |
|---|---|---|
| `build_command_data` | ADD `#[allow(dead_code)]` | no live caller until P1.M3.T3 |
| `CMD_DISCRIMINATOR` | REMOVE `#[allow(dead_code)]` | consumed by build_command_data |
| `CMD_QUERY_INFO` | REMOVE `#[allow(dead_code)]` | consumed (QueryInfo arm) |
| `CMD_QUERY_CALLBACK` | REMOVE `#[allow(dead_code)]` | consumed (QueryCallback arm) |
| `CMD_SET_OS` | REMOVE `#[allow(dead_code)]` | consumed (SetOs arm) |
| `RESPONSE_MARKER` | KEEP `#[allow(dead_code)]` | consumer = parse_reply (P1.M2.T2), not yet present |
| `CMD_APPLY_HOST_CONTEXT` | KEEP `#[allow(dead_code)]` | consumer = the ApplyHostContext arm, which is `todo!()` here; filled in S2 |
| `REPLY_READ_TIMEOUT_MS` | KEEP `#[allow(dead_code)]` | consumer = burst_to_one reply capture (P1.M3.T1) |
| `ETX_TERMINATOR_BYTE` (NEW) | NO allow needed | consumed by build_command_data the moment it's defined |

Verified clean across `cargo build`, `cargo clippy`, `cargo test` in the
`/tmp/deadcode_test2` mirror crate (exact same structure: allow-dead fn
referencing 4 constants, 3 of which lost their allow; a todo!() arm).

## Cross-module scope: RunCommand is in lib.rs, not core.rs

`RunCommand` and `HostOs` are defined at the **crate root** (`src/lib.rs`).
`build_command_data` lives in `src/core.rs`. Experiment (`/tmp/scope_test`)
confirmed:

- Adding `use crate::RunCommand;` (private import) to `core.rs` brings
  `RunCommand` into scope for the function signature.
- `use super::*;` in core.rs's `#[cfg(test)] mod tests` **does** re-import that
  private `use crate::RunCommand;`, so tests can write `RunCommand::QueryInfo`
  bare with no extra import.

`HostOs` is not named explicitly in `build_command_data` (the `SetOs(os)` arm
uses `*os as u8`), so it needs no import — only `RunCommand`.

## Exact byte layouts (canonical source: firmware_wire_contract.md §Command Table)

```
SendMessage(s)   → [ s.as_bytes()..., 0x03 ]                 (ETX_TERMINATOR_BYTE)
QueryInfo        → [ 0xF0, 0x01, 0x03 ]                       [CMD_DISCRIMINATOR, CMD_QUERY_INFO,    ETX]
QueryCallback(i) → [ 0xF0, 0x02, i, 0x03 ]                    [CMD_DISCRIMINATOR, CMD_QUERY_CALLBACK, i, ETX]
SetOs(os)        → [ 0xF0, 0x03, os as u8, 0x03 ]            [CMD_DISCRIMINATOR, CMD_SET_OS,         os, ETX]
ListDevices      → [ ]                                        (empty — never sent over the wire)
ApplyHostContext → todo!() (S2)                              [0xF0,0x05,layer,flags,count,ids...,0x03]
```

Edge case worth an explicit test assertion: **`SetOs(HostOs::Macos)`** (os=3) with
`CMD_SET_OS=0x03` and `ETX=0x03` yields `[0xF0, 0x03, 0x03, 0x03]` — three
consecutive `0x03` bytes. Asserting it proves the three distinct `0x03` sources
(cmd id, os byte, terminator) are not conflated.

NOTE (PRD §4.2 / §10.1): the magic header `0x81 0x9F` is **NOT** part of this
payload. `burst_to_one` writes it into buffer positions `[1]` and `[2]`
(`request_data[1] = 0x81; request_data[2] = 0x9F;` in core.rs:~223). The
discriminator `0xF0` is the **first payload byte** → buffer position `[3]`.

## SendMessage byte-identity with run()'s current inline build

`run()` (lib.rs, SendMessage arm) currently does:
```rust
let mut input_with_terminator = Vec::with_capacity(input.len() + 1);
input_with_terminator.extend_from_slice(input);
input_with_terminator.push(0x03);
```
`build_command_data(SendMessage(msg))` produces **byte-identical** output
(`msg.as_bytes().to_vec()` + `push(ETX_TERMINATOR_BYTE)`). This is the
"verify the existing run() ETX-append logic matches" contract item — it matches.
run() is migrated to *call* `build_command_data` in P1.M3.T3 (out of scope here);
until then the literal `0x03` in run() and the constant here coexist (verified
equal). `ETX_TERMINATOR_BYTE` stays a **private `const` in core.rs** (only
build_command_data uses it).

## Test count math

Current baseline (verified by counting `#[test]` in core.rs + lib.rs):
- core.rs: 14 tests (incl. `typed_command_constants_match_firmware_contract` from S1)
- lib.rs: 16 tests
- **Total: 30** (S1 added 1; S2 adds/fixes 0).

This task adds **5** new `#[test]` functions to core.rs's `mod tests` → new
total **35**.

New tests (core.rs naming convention = descriptive snake_case, NO `test_` prefix):
1. `build_command_data_send_message_appends_etx_terminator`
2. `build_command_data_query_info_is_discriminator_cmd_etx`
3. `build_command_data_query_callback_echoes_index`
4. `build_command_data_set_os_encodes_each_host_os`
5. `build_command_data_list_devices_is_empty`

## Commands verified working in this repo

`cargo fmt` / `cargo fmt --check` (default rustfmt, no rustfmt.toml);
`cargo build` (edition 2021); `cargo clippy --lib` (default, no clippy.toml);
`cargo test --lib`. No `rustfmt.toml`/`clippy.toml` exist. The crate is
`qmk_notifier` v0.2.1 (version bump to 0.3.0 is P1.M4.T2.S1 — out of scope).