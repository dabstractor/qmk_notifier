# Research Notes — P1.M2.T1.S1: build_command_data (re-planning, attempt 2)

## STATUS CHANGE SINCE ATTEMPT 1 (CRITICAL)

Attempt 1 HALTED because the uncommitted working tree contained a divergent
`build_typed_payload`. **That divergence is now COMMITTED** (`git status` clean;
HEAD = `828998e Initialize README v0.3.0 documentation sync plan`). The whole
P1.M3 layer (parse_reply, burst_to_one reply capture, send_raw_report, run()
dispatch) is **Complete** and built on top of `build_typed_payload`.

=> Option B (git checkout to restore HEAD) is now **IMPOSSIBLE** — downstream
   tasks would be destroyed.
=> Option A (accept build_typed_payload, skip PRP) contradicts the architecture
   doc's canonical `build_command_data` design + the task title/contract.
=> **Option C (reconcile) is the only sound path** — and it is now safe because
   the code is committed + tested (63 tests pass), not mid-flight.

## Current committed state (verified `git show HEAD`-equivalent)

| Concern | Current committed reality |
|---|---|
| Payload builder fn | `pub(crate) fn build_typed_payload(cmd: &RunCommand) -> Vec<u8>` in core.rs (~line 416) |
| SendMessage framing | Done INLINE in `fn build_payload(command, verbose)` in **lib.rs** (~line 372): `msg.as_bytes()` + push `0x03`; NOT in the builder |
| Typed arms | QueryInfo/QueryCallback/SetOs/**ApplyHostContext fully implemented** in build_typed_payload |
| ListDevices / SendMessage in builder | return empty `Vec::new()` (defensive) |
| ETX const | NONE — literal `0x03` everywhere |
| dead_code | All 5 command constants already have `#[allow(dead_code)]` REMOVED (consumed). build_typed_payload has a live caller (lib.rs build_payload) → no dead_code. |
| tests | 9 `build_typed_payload_*` tests incl. `build_typed_payload_non_typed_returns_empty` (asserts SendMessage=empty + ListDevices=empty) |
| total lib tests | **63 pass** (`cargo test --lib`) |

## Architecture-doc target (transport_evolution.md §build_command_data — canonical)

> Design decision #1: "**build_command_data is pure** ... Takes `&RunCommand`,
> returns `Vec<u8>`. ... **The caller (`run()`) appends nothing — ETX is already
> in the payload.**"

=> ONE function `build_command_data` handling ALL variants (incl. SendMessage),
   run()/build_payload delegates everything, appends nothing.

## Reconciliation (what the revised PRP does) — REFACTOR of working code

1. core.rs: `const ETX_TERMINATOR_BYTE: u8 = 0x03;` (pub(crate))
2. core.rs: RENAME `build_typed_payload` → `build_command_data`
3. core.rs: ADD `SendMessage(msg)` arm → `msg.as_bytes()` + `ETX_TERMINATOR_BYTE`
4. core.rs: REPLACE literal `0x03` ETX pushes with `ETX_TERMINATOR_BYTE`
5. core.rs: KEEP `ListDevices => Vec::new()`; KEEP ApplyHostContext FULLY implemented (do NOT todo!())
6. lib.rs: SIMPLIFY `build_payload` to delegate ALL arms to `core::build_command_data`, keep only the SendMessage verbose-length println as a side-effect
7. tests: rename `build_typed_payload_*` → `build_command_data_*`; SPLIT `..._non_typed_returns_empty` into `build_command_data_send_message` (asserts [bytes,0x03]) + `build_command_data_list_devices_empty`

Result: 63 → 64 lib tests, all pass; zero dead_code; code matches architecture doc.

## Why this is NOT "fail"

- No fundamental impossibility — pure mechanical rename + relocate-one-arm +
  add-const + test rename. All changes are covered by existing+adjusted tests.
- ApplyHostContext STAYS implemented (directly answers the feedback's open
  question "whether ApplyHostContext stays implemented or reverts" → it STAYS).

## ETX / framing reference (firmware_wire_contract.md, verified)

- Wire layout: `[0x00][0x81][0x9F][payload][0x03]`. The `0x81 0x9F` magic + `0x00`
  report-ID byte are added by `burst_to_one` (buffer positions [0]/[1]/[2]); the
  payload builder emits only what comes AFTER [0x81][0x9F].
- SendMessage payload = raw string bytes + `0x03` (NO discriminator — legacy path).
- Typed payload = `[0xF0][cmd_id][args…][0x03]`.
- PRD §14 invariant 1: caller builds `class\x1Dtitle`; crate appends ETX.