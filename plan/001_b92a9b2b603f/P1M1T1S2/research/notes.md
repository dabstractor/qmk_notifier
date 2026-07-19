# Research Notes — P1.M1.T1.S2: Extend RunCommand enum with typed-command variants

## Scope (from item description + PRD §3 + firmware wire contract)

Add four new variants to the existing `pub enum RunCommand` in `src/lib.rs`:

| Variant | cmd_id | Fields | Wire role |
|---|---|---|---|
| `QueryInfo` | `0x01` | (unit) | QUERY_INFO — no args |
| `QueryCallback(u8)` | `0x02` | `index` | QUERY_CALLBACK — arg = index |
| `SetOs(HostOs)` | `0x03` | `os_byte` source | SET_OS — arg = `HostOs::X as u8` |
| `ApplyHostContext { layer, callbacks, clear_board }` | `0x05` | see below | APPLY_HOST_CONTEXT |

`ApplyHostContext` field semantics (translated to wire in P1.M2.T1, NOT here):
- `layer: Option<u8>` → `None` = `0xFF` (clear host layer); `Some(n)` = host-layer # (`>=224` by convention, `HOST_LAYER_BASE`).
- `callbacks: Vec<u8>` → full desired enabled callback-id set (firmware diffs). Uncapped.
- `clear_board: bool` → `true` sets firmware `flags` bit 0 (`clear_board`).

## Contract dependency (parallel sibling P1.M1.T1.S1) — VERIFIED in working tree

S1 adds `pub enum HostOs { Unsure=0, Linux=1, Windows=2, Macos=3, Ios=4 }`
with `#[repr(u8)] #[derive(Debug, Clone, Copy, PartialEq, Eq)]` placed
**after `RunCommand`, before `RunParameters`**. Verified: `HostOs` is present at
`src/lib.rs:24` in the current working tree (S1 landed). S2's `SetOs(HostOs)`
variant therefore compiles against `HostOs`, and `HostOs: Clone` satisfies the
existing `#[derive(Debug, Clone)]` on `RunCommand`.

## The compile-exhaustiveness gotcha (the central risk for S2)

`run()` at `src/lib.rs:196` is:
```rust
pub fn run(params: RunParameters) -> Result<(), QmkError> {
    match params.command {
        RunCommand::ListDevices => list_hid_devices(),
        RunCommand::SendMessage(message) => { /* ~40 lines */ }
    }
}
```
This match is exhaustive over the current 2 variants. Adding 4 new variants
**breaks exhaustiveness** → compile error E0004. The item description explicitly
addresses this: *"If you need the crate to compile before T2, add temporary
`todo!()` arms."* → S2 must add 4 `todo!()` arms to keep `cargo build`/`cargo test`
green. This is **temporary scaffolding** removed/rewritten in:
- P1.M1.T2.S2 (run() return type → `Result<CommandResponse, QmkError>`)
- P1.M3.T3.S1 (real typed dispatch)

**Decision: add explicit per-variant `todo!()` arms** (not a `_ =>` wildcard —
a wildcard would silently swallow a future variant; explicit arms keep the
exhaustiveness check meaningful as the enum evolves). `todo!()` returns `!`,
coerces to any arm return type, so the `Result<(), QmkError>` arm type is fine.

**Why `todo!()` and not `unimplemented!()`:** `todo!()` is the idiomatic
"dispatch lands later" placeholder; same panic semantics, clearer intent.

**No clippy issue:** `clippy::todo` is in the `restriction` group (allow by
default), so default `cargo clippy` stays green.

## Why existing run() tests stay green after adding todo!() arms

Existing run() integration tests only construct `ListDevices`/`SendMessage`:
- `test_run_with_list_devices_command` (→ ListDevices arm, unchanged)
- `test_run_with_send_message_command` (→ SendMessage arm, unchanged)
- `test_run_with_verbose_output` (→ SendMessage arm, unchanged)

None hit a `todo!()` arm, so no panic. New S2 tests construct variants and
pattern-match the value — they do **NOT** call `run()` (typed dispatch is T3,
out of scope). So no `#[should_panic]` test is added (it would only test
temporary scaffolding).

## Derives — no change needed

Existing `RunCommand` has `#[derive(Debug, Clone)]`. All new fields satisfy
these traits:
- `QueryInfo` — unit variant ✓
- `QueryCallback(u8)` — u8: Debug+Clone ✓
- `SetOs(HostOs)` — HostOs: Debug+Clone (S1 added both) ✓
- `ApplyHostContext { Option<u8>, Vec<u8>, bool }` — all Debug+Clone ✓

Item description says "Add #[derive(Debug, Clone)] to match existing derives" →
**keep the existing `#[derive(Debug, Clone)]` unchanged**; do NOT add
PartialEq/Eq/Copy (not required; RunCommand owns a `String` so Copy is
impossible anyway).

## Test plan (construction tests — per item description)

Add 3 tests to the existing `#[cfg(test)] mod tests` block (alongside the S1
HostOs discriminant test and existing RunParameters tests), all using the
already-present `use super::*;`:

1. `test_run_command_query_variants_construction` — QueryInfo + QueryCallback(5);
   verify via `match` (including `QueryCallback(index)` binds `index == 5`).
2. `test_run_command_set_os_variant_construction` — SetOs(HostOs::Windows);
   verify the HostOs payload rounds-trips (`HostOs::Windows == 2` reuse).
3. `test_run_command_apply_host_context_construction` — both `layer: None`
   (clear-host-layer path) and `layer: Some(224)` (HOST_LAYER_BASE); verify
   `callbacks` Vec and `clear_board` flag via field binding.

## Validation math (verified against current working tree)

- `cargo test --lib` currently runs lib.rs (10) + core.rs (13) = **23** tests
  (post-S1). After S2 adds 3 tests → lib.rs (13) + core.rs (13) = **26**.
- `cargo build` / `cargo clippy --lib`: zero warnings expected (pub enum
  variants don't trip dead_code; todo!() not flagged by default clippy).
- `cargo fmt --check`: exit 0 (no rustfmt.toml → default style).

## File boundary (forbidden elsewhere)

Only `src/lib.rs` is modified:
- Extend `RunCommand` enum body (add 4 variants + `///` doc comments).
- Add 4 `todo!()` arms to the `match params.command` in `run()`.
- Add 3 `#[test]` fns to the existing `mod tests`.

No change to: HostOs, RunParameters, parse_cli_args, core.rs, error.rs, main.rs,
Cargo.toml, README, PRD.md, any tasks.json/prd_snapshot.

## Documentation approach (Mode A)

Item description: *"Doc-comment each variant referencing the cmd_id and
PRD §10.1."* PRD §10.1 = "Framing" (index h3.12). Each new variant gets a `///`
doc comment naming the cmd_id (0x01/0x02/0x03/0x05) and pointing at
`firmware_wire_contract.md` §Command Table as the canonical wire layout.
ApplyHostContext's doc also encodes the `layer=None⇒0xFF` / `clear_board⇒flag
bit 0` semantics (these are NOT enforced here — just documented for the
build_command_data consumer in P1.M2.T1).