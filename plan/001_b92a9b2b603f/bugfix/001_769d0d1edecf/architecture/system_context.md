# System Context — v0.3.1 Bug Fix

## Project State

`qmk_notifier` v0.3.0 is a Rust library + CLI that owns Raw-HID wire framing,
the device cache, burst-write, and typed-command transport for the QMKonnect
ecosystem. The crate is clean, compiles without warnings, and passes 65 tests
+ clippy. However, live hardware testing revealed three behavioral bugs and two
spec deviations that this bugfix release addresses.

## Crate Layout

```
src/
├── lib.rs     (957 lines) — public API: RunCommand, HostOs, CommandResponse,
│                              RunParameters, CliArgs, parse_cli_args, run
├── core.rs   (1304 lines) — transport: constants, device discovery, cache,
│                              burst-write, reply capture, parse_reply
├── error.rs    (83 lines) — QmkError enum + Display + Error + From<HidError>
└── main.rs    (131 lines) — CLI binary entry point (thin wrapper)
```

Dependencies: `clap = "4.5.31"`, `hidapi = "2.4.1"`. No `[dev-dependencies]`.

## Key Architecture Decisions Relevant to This Bugfix

### A1: No trait abstraction over HID I/O (MUST BE INTRODUCED)

`burst_to_one` (core.rs:305) takes `interface: &hidapi::HidDevice` — a concrete
type, not generic. There is zero `trait` usage in the crate (`grep -rn 'trait ' src/`
returns nothing). No mocking framework is configured (no `[dev-dependencies]`).

This means the reply-capture/drain logic in `burst_to_one` is **untestable without
real hardware** today. A `RawHid` trait abstraction must be introduced to enable
regression tests for Issues 1 and 3.

**Required trait surface** (only what `burst_to_one` calls):
```rust
pub(crate) trait RawHid {
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError>;
    fn read_timeout(&self, buf: &mut [u8], timeout: i32) -> Result<usize, hidapi::HidError>;
}
```

`impl RawHid for hidapi::HidDevice` delegates to the existing methods.
`burst_to_one` becomes `fn burst_to_one<T: RawHid>(interface: &T, ...)`.
`try_send_once` calls it with `&HidDevice` (Rust infers the type param — no change
to `try_send_once` or `DeviceCache` needed).

### A2: Firmware reply model — one reply PER REPORT (not per burst)

The firmware's `hid_notify()` (`notifier.c`) is invoked once per 32-byte report
and sends a 32-byte reply at the end of every call. For an N-report message the
device emits **N replies**:

- Reports 1..N-1 (no ETX seen): `response[0] == 0` (match stays false).
- Report N (contains ETX terminator): `response[0]` carries the real result
  (legacy match-bool `0`/`1`, OR a typed `0x51...` reply if the message was a
  typed command).

**CRITICAL**: The existing architecture doc `firmware_wire_contract.md` (in
`plan/001_b92a9b2b603f/architecture/`) states typed commands are "NOT YET
IMPLEMENTED" — this is **STALE**. The bugfix PRD confirms via live hardware
testing that the firmware now implements typed dispatch. The firmware emits
`0x51` typed replies on the ETX report and `0` on intermediate reports.

### A3: burst_to_one current flow (THE BUG)

```
burst_to_one(interface: &HidDevice, data, batch_count, verbose) -> (bool, Option<Vec<u8>>)
  │
  ├─ WRITE LOOP (core.rs:315-336): write batch_count reports back-to-back
  │    NO pre-send drain ←── Issue 3 (stale replies not flushed)
  │
  ├─ CAPTURE FIRST REPLY (core.rs:346): read_timeout(buf, 1000ms) × 1
  │    Keeps the FIRST reply ←── Issue 1 (wrong reply for multi-report)
  │
  ├─ DRAIN SURPLUS (core.rs:370): read_timeout(buf, 0) × ≤32
  │    Discards remaining replies (including the REAL result)
  │
  └─ return (true, Some(first_reply))
```

### A4: Fix design for burst_to_one

**Pre-send drain (Issue 3):** Add a non-blocking `read_timeout(0)` drain loop
BEFORE the write loop (same pattern as the existing post-capture drain).

**Capture-last-reply (Issue 1):** Replace the single bounded read with a loop
that reads up to `batch_count` replies (or until timeout), **overwriting** the
captured reply each iteration so the LAST one wins. Then keep the existing
post-capture surplus drain as a safety net.

```
NEW burst_to_one flow:
  ├─ PRE-SEND DRAIN: read_timeout(buf, 0) × ≤32  ← NEW (Issue 3)
  ├─ WRITE LOOP: write batch_count reports
  ├─ CAPTURE LAST REPLY: read_timeout(buf, timeout) × batch_count
  │    reply = latest non-empty read  ← CHANGED (Issue 1)
  ├─ SURPLUS DRAIN: read_timeout(buf, 0) × ≤32  (unchanged safety net)
  └─ return (true, Some(last_reply))
```

### A5: FakeHid test double design

Under `#[cfg(test)]` in core.rs, a `FakeHid` implementing `RawHid` enables
deterministic regression tests:

```rust
struct FakeHid {
    // Replies returned by read_timeout BEFORE any write (stale data to drain).
    pre_write_replies: RefCell<VecDeque<Vec<u8>>>,
    // Replies returned by read_timeout AFTER the first write (firmware replies).
    post_write_replies: RefCell<VecDeque<Vec<u8>>>,
    // Whether write() has been called at least once.
    written: Cell<bool>,
    // Recorded write() calls for assertion.
    writes: RefCell<Vec<Vec<u8>>>,
}
```

`read_timeout` checks `written`: if false, pops from `pre_write_replies`; if true,
pops from `post_write_replies`. Empty queue ⇒ `Ok(0)` (matches hidapi timeout
semantics). `write()` sets `written = true` and records the data.

This cleanly separates stale (pre-send) data from fresh (post-send) firmware
replies, enabling both the multi-report capture test and the stale-drain test.

### A6: parse_cli_args signature drift (Issue 2)

PRD §3 (line 154) documents: `pub fn parse_cli_args() -> Result<RunParameters, QmkError>`.
Actual: returns `Result<CliArgs, QmkError>` where `CliArgs { params: RunParameters, list_callbacks: bool }`.

**Fix (option a — preserves spec):** Restore the documented return type. Move
`--list-callbacks` detection to `main.rs` via `std::env::args().any(|a| a == "--list-callbacks")`.
The `select_command` function still maps `--list-callbacks` → `RunCommand::QueryInfo`
(command part stays in `RunParameters`); main.rs separately detects the flag string
to decide whether to sweep callbacks after `run` returns `Info`.

`CliArgs` struct can be removed entirely. `parse_matches` returns `RunParameters`
directly. `select_command` can drop the `bool` from its return type (or keep it
internally and discard it at the `parse_cli_args` boundary).

### A7: Unsafe SIGPIPE handling (Issue 4)

`main.rs:101` contains `unsafe { signal(SIGPIPE, SIG_DFL); }` via hand-rolled
`extern "C"`. PRD §12 says "No `unsafe`."

**Fix:** Replace the hand-rolled FFI with the `libc` crate's maintained binding
(`libc::signal`). Add `libc` to `[dependencies]` (or `[target.'cfg(unix)'.dependencies]`).
The `unsafe` block remains (POSIX `signal()` is inherently unsafe), but the FFI
declaration is no longer hand-rolled — it uses a widely-audited binding. The helper
function `reset_sigpipe_to_default()` already provides the safe wrapper; the
improvement is replacing `extern "C" { fn signal(...) }` with `libc::signal`.

### A8: Undocumented -c short flag (Issue 5)

`build_cli_command` (lib.rs:232-239) registers `.short('c')` for `--create-config`.
Neither PRD §11 nor README's CLI table documents this short flag.

**Fix:** Remove `.short('c')` from the `create-config` arg registration. The
long form `--create-config` remains (it's a backward-compat trap that returns
`RemovedFeature`).

## Stale Architecture Docs to Update

- `plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md` — states "NOT
  YET IMPLEMENTED" for typed commands. The firmware now implements them. This
  should be corrected as part of the documentation sync.
- `plan/001_b92a9b2b603f/architecture/findings_and_risks.md` — finding F4 says
  "The firmware does not implement typed commands yet." Stale.

## Consumer Impact

`qmkonnect` (the daemon consumer):
- Does NOT call `parse_cli_args` (builds `RunParameters` directly) — Issue 2 fix
  has zero blast radius on qmkonnect.
- Discards `run()`'s `Ok` value (`Ok(_) => return Ok(())`) — Issue 1 fix has no
  functional impact today but corrects the library contract for the planned
  capability handshake.
- Issues 3, 4, 5 have no consumer impact.

## Documentation Files Requiring Updates

| File | Change | Mode |
|------|--------|------|
| `src/core.rs` doc comments on `burst_to_one` | Update to reflect capture-last + pre-send drain | A (with work) |
| `src/lib.rs` doc comment on `parse_cli_args` | Update return type docs | A (with work) |
| `README.md` §Technical Details "Reply parsing" | "one 32-byte IN report" → "the LAST reply (ETX report's reply)" | B (final sweep) |
| `README.md` §Overview | Mention multi-report reply fix if applicable | B (final sweep) |
| `plan/.../architecture/firmware_wire_contract.md` | Remove "NOT YET IMPLEMENTED" stale note | B (final sweep) |
| `plan/.../architecture/findings_and_risks.md` | Correct finding F4 | B (final sweep) |