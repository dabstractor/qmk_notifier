# Findings & Risks

## Research Findings

### F1: Codebase is clean and well-structured
The v0.2.1 codebase compiles without warnings and passes all 22 tests. The
architecture is clean: pure functions (`device_matches`, `batches_for`,
`parse_hex_or_decimal`) are separated from I/O functions (`burst_to_one`,
`send_raw_report`). Unit tests are thorough on the pure functions.

### F2: Typed-command transport is purely additive until `run()` changes
Adding `HostOs`, extending `RunCommand`, adding `CommandResponse`, and adding
`build_command_data`/`parse_reply` are all **additive** — they don't modify
existing code paths. The only **breaking** changes are:
- `run()` return type: `Result<(), QmkError>` → `Result<CommandResponse, QmkError>`
- `send_raw_report()` return type: `Result<(), QmkError>` → `Result<Option<Vec<u8>>, QmkError>`
- `burst_to_one()` return type: `bool` → `(bool, Option<Vec<u8>>)`

These should be done after the additive work, in a bottom-up order:
`burst_to_one` → `try_send_once` → `send_raw_report` → `run()`.

### F3: RunCommand exhaustiveness forces handling new variants
Adding new `RunCommand` variants makes the `match params.command` in `run()`
non-exhaustive. Rust's compiler enforces this — you MUST add arms for every new
variant. The clean approach: add `todo!()` stubs when adding variants, then
replace them with real dispatch logic in the wiring task.

### F4: The firmware now implements typed commands
The firmware `notifier.c` implements the §4.6 typed-command namespace: `hid_notify()`
routes `data[2] == 0xF0` (first report) to a typed-reassembly path, and
`handle_typed_command()` dispatches QUERY_INFO / QUERY_CALLBACK / SET_OS /
APPLY_HOST_CONTEXT, emitting `[0x51][cmd_echo][payload]` typed replies on the ETX
report. Confirmed via live hardware testing (Dactyl-Manuform) cross-checked against
the firmware source. The transport layer must STILL handle timeouts gracefully
(§10.2, §14 invariant #6) — a non-capable or offline device replies with a legacy
`0`/`1` or nothing, which parses to `Timeout` semantics. Unit tests for reply
parsing should continue to use synthetic byte buffers (deterministic, no hardware
dependency); typed replies are additionally observable on live hardware.

### F5: qmkonnect's retry string-matching has a latent gap
`QmkError::DeviceOpenError` Display outputs `"Error opening device: …"`, but
qmkonnect matches `"failed to open"`. This means `DeviceOpenError` is NOT
retried by qmkonnect. This is a qmkonnect bug, not this crate's problem, but the
v0.3.0 release should not change the Display output of `DeviceOpenError`.

### F6: Unused dependencies
`toml`, `serde`, and `dirs` are all completely unused. Dropping them is safe and
reduces compile time and binary size. The PRD §2 sanctions dropping `toml`/`serde`.

## Risks & Mitigations

### R1: Breaking `run()` return type
**Risk**: Any downstream consumer that pattern-matches on `run()`'s `Ok` value
will break.
**Mitigation**: qmkonnect uses `Ok(_) => ...` (discards). Verified in
`src/core/notifier.rs:156`. The change is source-compatible for qmkonnect.
**Residual**: qmkonnect will need a follow-up to bump the git tag to `v0.3.0`
and eventually capture `CommandResponse` for the handshake.

### R2: Reply timeout value
**Risk**: `REPLY_READ_TIMEOUT_MS` too high = latency; too low = false Timeout on
slow devices.
**Mitigation**: Use 1000ms (generous — USB HID replies are typically <50ms).
The constant is in core.rs and trivially tunable. The PRD says "bounded" without
specifying the value.

### R3: Multi-device reply ambiguity
**Risk**: If multiple matching devices exist, which reply is returned?
**Mitigation**: First-success device's reply wins. Documented in
`transport_evolution.md`. Realistic deployment is single-keyboard.

### R4: APPLY_HOST_CONTEXT callback list overflow
**Risk**: If `callbacks.len()` exceeds 255, the `count` byte overflows.
**Mitigation**: The firmware caps at `HOST_CALLBACK_MAX = 32`. The transport
should not cap (PRD §10.1 says "uncapped"), but a defensive `min(callbacks.len(), 255)`
on the count byte is prudent. The multi-report chunking handles >30 bytes.

### R5: Existing test breakage
**Risk**: Tests in `lib.rs` that call `run()` and check `result.is_ok()` still
compile (Result is still Result), but tests that match on `Ok(())` specifically
will break.
**Mitigation**: Verified — current tests use `result.is_ok() || result.is_err()`
and `Ok(()) => {}` patterns. The `Ok(())` pattern in `test_run_with_send_message_command`
WILL break because the Ok variant changes from `()` to `CommandResponse`.
This test must be updated as part of the `run()` signature change subtask.

## Task Sequencing Rationale

The decomposition follows a strict dependency chain:

```
Types (M1) → Pure Functions (M2) → Transport Evolution (M3) → CLI/Cleanup (M4)
    │              │                        │
    │              │                        ├─ burst_to_one (bottom of call stack)
    │              │                        ├─ try_send_once
    │              │                        ├─ send_raw_report
    │              │                        └─ run() dispatch (top of call stack)
    │              │
    │              ├─ build_command_data (depends on RunCommand variants)
    │              └─ parse_reply (depends on CommandResponse)
    │
    ├─ HostOs, CommandResponse (independent types)
    ├─ RunCommand variants (depends on HostOs)
    └─ run() signature stub (depends on CommandResponse)
```

Each milestone produces compilable, testable code. No milestone leaves the crate
in a non-compiling state.