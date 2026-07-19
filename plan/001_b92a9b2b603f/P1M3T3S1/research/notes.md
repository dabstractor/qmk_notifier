# P1.M3.T3.S1 — Research Notes (run() full dispatch)

Empirically verified research for the `run()` dispatch rewrite. All findings
below were confirmed against the ACTUAL current codebase (src/lib.rs, src/core.rs)
and a faithful scratch compile (`/tmp/run_dispatch_test`, see F6).

---

## F1 — THE CRITICAL NAMING DISCREPANCY (read first)

The item's pseudocode and `transport_evolution.md` call the payload builder
**`build_command_data`**. **The ACTUAL function in src/core.rs is named
`build_typed_payload`** (pub(crate), `fn build_typed_payload(cmd: &crate::RunCommand) -> Vec<u8>`).

More importantly, `build_typed_payload` does **NOT** handle `SendMessage`:

```rust
// core.rs — the trailing match arms of build_typed_payload:
RunCommand::SendMessage(_) | RunCommand::ListDevices => return Vec::new(),
```

Proven by the existing test `build_typed_payload_non_typed_returns_empty`
(src/core.rs) which asserts `build_typed_payload(&RunCommand::SendMessage(..))`
== `Vec::new()`.

**CONSEQUENCE:** The item's instruction *"Remove the old ETX-appending logic
from run() (it's now in build_command_data)"* is **WRONG given the current code**.
There is NO function that builds the SendMessage payload (string bytes + 0x03 ETX).
The ETX-appending for SendMessage MUST stay in lib.rs. The clean way: a small
private helper `build_payload(command, verbose)` in lib.rs that dispatches
(SendMessage → inline ETX; typed → `core::build_typed_payload`; ListDevices →
empty, never reached). See F5.

`parse_reply` IS correctly named (pub(crate), `fn parse_reply(response: &[u8])
-> crate::CommandResponse`) and handles ALL reply shapes including the legacy
0/1 match-bool for SendMessage. So `parse_reply` is the single correct decoder
for EVERY command's reply. Good — `run()` can call `core::parse_reply(&reply)`
for all send commands.

## F2 — lib.rs currently references core functions via the `core::` path

lib.rs has `mod core;` (lib.rs:1) and a `pub use core::{ list_hid_devices,
parse_hex_or_decimal, send_raw_report, DEFAULT_*, REPORT_LENGTH };` re-export
(lib.rs:2-7). build_typed_payload and parse_reply are NOT in that `use` list
(they're pub(crate), not re-exported). The EXISTING typed arm calls them via the
fully-qualified module path:

```rust
// lib.rs (current typed arm):
let payload = core::build_typed_payload(&params.command);
```

**CONSEQUENCE:** For `parse_reply`, follow the SAME convention — call
`core::parse_reply(&reply)`. Do NOT add parse_reply to the `pub use core::{...}`
list (it's pub(crate); re-exporting it would widen visibility, which is NOT this
item's job). Do NOT add a `use core::{parse_reply, ...}` import — the existing
code uses the `core::` path, stay consistent.

## F3 — parse_reply carries `#[allow(dead_code)]` (becomes INERT, leave it)

`parse_reply` (and its helpers `parse_typed_reply`, `parse_callback_name`) carry
`#[allow(dead_code)]` because until THIS item they were referenced only by tests.
Its own doc comment says: *"The `#[allow(dead_code)]` stays until `run()` goes
live (S2/P1.M3.T3) ... remove it in P1.M3.3 once `run()` calls it."*

**KEY DECISION (conflict avoidance):** P1.M3.T2.S1 is editing src/core.rs IN
PARALLEL right now (send_raw_report + try_send_once). To avoid a merge conflict
on core.rs, **this item edits src/lib.rs ONLY** and leaves the
`#[allow(dead_code)]` on parse_reply IN PLACE.

**Is that clippy-clean? YES — empirically proven (F6).** A `#[allow(dead_code)]`
attribute on a function that is actually USED is INERT: the `dead_code` lint
never fires (the fn isn't dead), and `#[allow(...)]` for a non-firing lint emits
NO warning under default rustc/clippy. So leaving it is a harmless no-op. Its
removal is a trivial one-line cleanup that can roll into the doc/cleanup task
(P1.M4) once core.rs is no longer being touched in parallel.

(Do NOT remove it here: that's a core.rs edit and risks colliding with
P1.M3.T2.S1. The PRP's hard scope is "src/lib.rs ONLY".)

## F4 — run() signature is ALREADY `Result<CommandResponse, QmkError>` (P1.M1.T2.S2 done)

The item's note *"test_run_with_send_message_command now expects CommandResponse
instead of ()"* is **already satisfied**. P1.M1.T2.S2 (Complete) changed
`run()` to `-> Result<CommandResponse, QmkError>` and fixed the existing tests.
Verified current state (src/lib.rs:294):

```rust
pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError> { ... }
```

So:
- `main.rs` already `match`es `run(params)` on `Ok(response) => println!("{:?}", response)`
  (compatible — no change to main.rs).
- `test_run_with_send_message_command` already does `match result { Ok(_) => {},
  Err(DeviceNotFound) => {}, ... }` — the Ok arm ignores the CommandResponse, so
  it compiles UNCHANGED regardless of what CommandResponse variant run() returns.

## F5 — The dispatch tests the item asks for ALREADY EXIST

The item says: *"Add a new test: test_run_query_info_dispatch ... expects
DeviceNotFound Err without hardware."* This test ALREADY EXISTS as
`test_run_query_info_dispatches_to_send` (src/lib.rs), added in P1.M1.T2.S2:

```rust
#[test]
fn test_run_query_info_dispatches_to_send() {
    let params = RunParameters::new(RunCommand::QueryInfo, Some(0xDEAD), Some(0xBEEF), ...);
    let result = run(params);
    assert!(matches!(result, Err(QmkError::DeviceNotFound { .. })), ...);
}
```

Sibling tests exist for QueryCallback, SetOs, ApplyHostContext — all assert
`Err(DeviceNotFound)` for bogus VID/PID. **After this item's run() change, these
still pass** (QueryInfo → build_typed_payload → send_raw_report → `?` propagates
`Err(DeviceNotFound)`). The dispatch path is ALREADY proven by these tests.

**DECISION:** Do NOT add a duplicate test. The requested coverage exists. Adding
a near-identical test would be redundant. The reply→parse_reply mapping cannot
be unit-tested without real HID hardware (send_raw_report needs a live device),
so the dispatch tests + parse_reply's own core.rs test suite are the maximal
testable coverage. Test count stays at the baseline (lib.rs 20 + core.rs 42 = 62;
see F8).

## F6 — EMPIRICAL COMPILE PROOF (the riskiest part: `command @ (or-pattern)`)

A faithful scratch model of the proposed run() + build_payload helper was compiled
at `/tmp/run_dispatch_test` (mirrors: pub(crate) build_typed_payload returns empty
for SendMessage/ListDevices; parse_reply with #[allow(dead_code)]; send_raw_report
returns `Result<Option<Vec<u8>>, QmkError>`; the exact `command @ (or-pattern)`
match arm + the `match send_raw_report(...)? { Some => parse_reply, None => Timeout }`
inner match).

**Results (edition 2021, default rustc/clippy/rustfmt):**
- `cargo build` → 0 errors, 0 warnings.
- `cargo clippy --lib` → **0 warnings** (confirms `command @ (or-pattern)` is
  clippy-clean AND `#[allow(dead_code)]` on a used parse_reply is INERT).
- `cargo test --lib` → 2 passed (both dispatch tests return DeviceNotFound via `?`).
- `cargo fmt --check` → exit 0.

**CONFIRMED SAFE:**
1. `command @ (RunCommand::SendMessage(_) | RunCommand::QueryInfo | ...)`
   binds `command: &RunCommand` under match ergonomics (matching `&params.command`).
2. `command` (a `&RunCommand`) is passable to `fn build_payload(command: &RunCommand,
   verbose: bool) -> Vec<u8>`, which then re-matches it exhaustively.
3. `match send_raw_report(&data, ...)? { Some(reply) => ..., None => ... }` — the `?`
   short-circuits the `Err` BEFORE the match arms run (so DeviceNotFound propagates
   unchanged; this is why the dispatch tests still pass).
4. The `#[allow(dead_code)]` left on parse_reply is clippy-silent.

## F7 — The verbose VID/PID/usage block must MOVE to the shared arm

Currently (lib.rs SendMessage arm):
```rust
if params.verbose {
    let vid = params.vendor_id.map(|v| format!("0x{v:04X}")).unwrap_or_else(|| "any".into());
    let pid = params.product_id.map(|p| format!("0x{p:04X}")).unwrap_or_else(|| "any".into());
    println!("Using VID: {vid}, PID: {pid}");
    println!("Using Usage Page: 0x{:04X}, Usage: 0x{:04X}", params.usage_page, params.usage);
}
```
The item says: *"Move it to the shared arm (it applies to all send commands)."*
So this block goes ABOVE `let data = build_payload(...)` in the shared or-pattern
arm. The SECOND verbose block (SendMessage-specific message-length log) stays with
the SendMessage data-building (move it INTO build_payload's SendMessage arm so the
behavior — "Message length: N bytes (including ETX terminator)" — is preserved).

## F8 — Baseline test count & validation gate

Verified in this repo (current HEAD): `grep -cE '^\s*#\[test\]' src/*.rs` →
lib.rs = 20, core.rs = 42 ⇒ **62 #[test] functions** (the P1.M3.T2.S1 PRP's "57"
was an older count; the parse_reply/build_typed_payload suites grew core.rs to
42). This item ADDS NO tests and CHANGES NO existing test's logic, so the count
is unchanged. Confirm with `cargo test --lib` (expect "62 passed; 0 failed" after
P1.M3.T2.S1 also lands; before it lands, build fails on the Option<Vec<u8>>
mismatch — that's expected and resolved by the dependency).

Validation commands (verified working in this repo): `cargo fmt`, `cargo build`,
`cargo clippy --lib`, `cargo fmt --check`, `cargo test --lib`.

## F9 — Scope boundary / conflict with the parallel item

- P1.M3.T2.S1 (parallel, core.rs): changes send_raw_report to
  `Result<Option<Vec<u8>>, QmkError>` + try_send_once tuple return. **This item
  ASSUMES that has landed** (it's the INPUT dependency). If `send_raw_report`
  still returns `Result<(), QmkError>`, this item's `match send_raw_report(...)?
  { Some(_) => ..., None => ... }` won't compile — STOP and confirm P1.M3.T2.S1
  landed first.
- This item edits **src/lib.rs ONLY** (run() body + run() doc comment + the new
  private build_payload helper). It does NOT touch core.rs / error.rs / main.rs /
  Cargo.toml. This keeps it conflict-free with P1.M3.T2.S1.