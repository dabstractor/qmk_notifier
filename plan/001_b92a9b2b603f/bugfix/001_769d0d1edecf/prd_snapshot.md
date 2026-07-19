# Bug Fix Requirements

## Overview

End-to-end validation of the `qmk_notifier` v0.3.0 implementation against
`PRD.md` (§3 Public API, §4 Wire Protocol, §7 Send Path, §8 Response Handling,
§10 Typed-Command Transport, §11 CLI, §12 NFRs, §14 Invariants).

Testing performed:
- Read all source (`lib.rs`, `core.rs`, `error.rs`, `main.rs`) and all
  architecture docs (`firmware_wire_contract.md`, `transport_evolution.md`,
  `system_context.md`, `findings_and_risks.md`).
- `cargo build`, `cargo build --release`, `cargo test` (65 tests pass),
  `cargo clippy --all-targets` (clean).
- Adversarial probing of `parse_hex_or_decimal` (24 edge cases), CLI parsing
  (help/version/conflicts/missing-action), and the full `RunCommand` surface.
- **Live hardware testing** against a real QMK keyboard
  (Dactyl-Manuform, VID 0xFEED / PID 0x0000, usage page 0xFF60 / usage 0x61,
  running the qmk-notifier firmware at `/home/dustin/projects/qmk-notifier`).
- Cross-checked observed wire behavior against the firmware source
  (`notifier.c::hid_notify`) to establish canonical reply semantics.

Overall quality: the crate is clean, well-documented, and the **single-report**
happy path (legacy string ≤29 bytes, and single-report typed commands
QUERY_INFO / QUERY_CALLBACK / SET_OS) works correctly end-to-end, including
reply capture and parsing. `parse_hex_or_decimal`, `build_command_data`, and
`parse_reply` are robust against all probed edge cases (truncation, non-UTF8,
unknown markers, overflow, count-clamp).

However, the **centerpiece v0.3.0 feature — reply capture/parsing — is
incorrect for any payload that spans more than one HID report**, and there are
two further spec deviations. Findings below.

---

## Critical Issues (Must Fix)

### Issue 1: Multi-report reply capture returns the wrong `CommandResponse` (captures an intermediate reply, discards the real result)

**Severity**: Critical
**PRD Reference**: §4.4 ("The firmware sends a 32-byte reply **per report**"),
  §8 ("For a SendMessage, `response[0]` is the legacy match-bool"), §10.2
  (reply disambiguation), §14 invariant 6.
**Affects**: `src/core.rs::burst_to_one` (the v0.3.0 reply-capture logic),
  and therefore `send_raw_report` / `run()` for **every payload > 30 bytes**.

**Expected Behavior**
For a multi-report message, `run()` must return the match/typed result that
corresponds to the **complete, ETX-terminated** message — i.e. the reply to the
report that contains the `0x03` ETX terminator. PRD §8 ties `response[0]` to
"the legacy match-bool", which the firmware only computes once the full message
is reassembled at ETX.

**Actual Behavior**
The firmware's `hid_notify()` (`/home/dustin/projects/qmk-notifier/notifier.c`)
is invoked **once per 32-byte report** and sends a 32-byte reply **at the end of
every call**:
```c
// end of hid_notify(), runs once PER REPORT:
if (!typed_dispatched) {
    uint8_t response[RAW_REPORT_SIZE] = {0};
    response[0] = match;          // match is false until the ETX report sets it
    raw_hid_send(response, RAW_REPORT_SIZE);
}
```
For an N-report message the device therefore emits **N replies**, where the
first N−1 have `response[0] == 0` (the message is incomplete — no ETX seen, so
`match` stays `false`) and only the **last** reply carries the real result.

`burst_to_one` captures the **FIRST** reply (`read_timeout` immediately after
the burst) and then **drains/discards** the rest:
```rust
// core.rs burst_to_one, after the write loop:
match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
    Ok(n) if n > 0 => { reply = Some(read_buf[..n].to_vec()); }   // <-- FIRST reply
    _ => {}
}
// ... then drains surplus reports non-blocking (discards the real result)
```
So for any multi-report payload the crate returns the intermediate `0` reply as
`CommandResponse::Legacy { matched: false }`, and throws away the real result.

**Steps to Reproduce** (requires a QMK keyboard with qmk-notifier firmware;
observed on the connected Dactyl-Manuform)

1. Build: `cargo build --release`.
2. Send a short (single-report) string that the firmware matches:
   ```
   ./target/release/qmk_notifier "hello"
   # -> Legacy { matched: true }      ✅ correct (1 report -> 1 reply)
   ```
3. Send the SAME matching prefix padded past the 30-byte payload boundary
   (30-char string ⇒ 31-byte payload ⇒ 2 reports):
   ```
   ./target/release/qmk_notifier "hello.............................."
   # -> Legacy { matched: false }     ❌ WRONG — same matching content, but
   #                                     the intermediate [0] reply is captured
   ```
4. Direct hidapi proof that the firmware emits one reply per report (bypassing
   the crate, draining all replies after a single burst):
   ```
   1-report "hello":            replies = [1]                              (1 reply)
   2-report "hello"+25 dots:    replies = [0, 1]    <- real result is 2nd   (2 replies)
   4-report "hello"+85 dots:    replies = [0,0,0,1]  <- real result is 4th   (4 replies)
   ```
   The crate captures the first element of each list (`0`) and discards the
   trailing real result.

**Boundary**: any payload whose length exceeds `PAYLOAD_PER_REPORT` (30) is
affected. For `SendMessage` this means strings of **30+ bytes** (payload ≥ 31 ⇒
2+ reports). Real-world window strings `"class\x1Dtitle"` routinely exceed this
(e.g. `"code.Code — Bug Report — qmk_notifier"` ≈ 44 bytes ⇒ 2 reports), so the
bug hits the dominant qmkonnect notification shape.

**Additional impact — multi-report typed commands**: a typed reply (`0x51…`) is
emitted **only** on the ETX report (firmware sets `typed_dispatched` there and
skips the legacy ack). For intermediate reports the firmware sends `0`. So an
`APPLY_HOST_CONTEXT` that spans reports — e.g. the documented full callback set
of 32 ids ⇒ `5 (header) + 32 (ids) + 1 (ETX) = 38` bytes ⇒ 2 reports — has its
`[0x51][0x05][ack]` typed reply discarded and returns `Legacy { matched: false }`
instead, masking the device as non-capable. (Single-report typed commands
QUERY_INFO / QUERY_CALLBACK / SET_OS are unaffected: one report ⇒ one reply ⇒
captured correctly.)

**Ecosystem caveat**: `qmkonnect` currently discards `run()`'s `Ok` value
(`Ok(_) => return Ok(())`, per `system_context.md`), so this does not break the
daemon *today*. It does break (a) the CLI's printed result for any 30+ byte
message, (b) the library's correctness contract for `CommandResponse`, and (c)
the planned qmkonnect capability handshake once it starts consuming
`CommandResponse`.

**Suggested Fix**
Capture the reply that corresponds to the ETX report rather than the first
reply. Concretely, in `burst_to_one`, after the burst-write, read replies in a
loop (bounded by `IN_DRAIN_MAX` and a total deadline) and **keep the last
non-empty reply** instead of the first — because reports are processed and
replied to in order, the ETX report's reply is always the final one. Equivalent
options: read exactly `batch_count` replies and use the last; or read until the
drain comes back empty and retain the final buffer. Either way, the *current*
"capture first, drain the rest" ordering in `core.rs` lines ~340–470 must be
inverted. Add a regression test that, given a 2-report payload, asserts the
captured reply equals the ETX-report result (a mock/fake `HidDevice` that emits
`[0]` then `[1]` is sufficient — the existing pure-function tests already
isolate the parsing layer).

---

## Major Issues (Should Fix)

### Issue 2: `parse_cli_args()` returns `CliArgs`, not the PRD-documented `RunParameters`

**Severity**: Major (public-API contract drift; low real-world blast radius)
**PRD Reference**: §3 *Public API* — `pub fn parse_cli_args() -> Result<RunParameters, QmkError>;` (PRD.md line 154), and §2/§11 ("main.rs is a thin wrapper around `parse_cli_args` + `run`").
**Expected Behavior**: `parse_cli_args()` returns `Result<RunParameters, QmkError>` so that `main.rs` (and any library consumer) can do `run(parse_cli_args()?)` directly, exactly as PRD §3 documents.
**Actual Behavior**: `src/lib.rs` defines a new public `CliArgs { params: RunParameters, list_callbacks: bool }` and returns `Result<CliArgs, QmkError>`:
```rust
pub fn parse_cli_args() -> Result<CliArgs, QmkError> { ... }   // lib.rs:367
```
`CliArgs` is not mentioned anywhere in PRD §3. Any consumer written to the
documented signature (`let params = qmk_notifier::parse_cli_args()?; run(params)`)
fails to compile — it must use `run(cli.params)` instead.
**Steps to Reproduce**: `grep -n "pub fn parse_cli_args" src/lib.rs` → returns `Result<CliArgs, QmkError>`; compare to PRD.md line 154 (`Result<RunParameters, QmkError>`).
**Impact**: `qmkonnect` does not call `parse_cli_args` (it builds `RunParameters`
directly), so no current consumer breaks. But this is an undocumented deviation
from the explicit §3 public-API contract and will surprise any third-party user
of the library API.
**Suggested Fix**: Either (a) restore the documented signature and move the
`--list-callbacks` sweep signal out-of-band (e.g. parse raw `env::args` in
`main.rs`, or have `main.rs` re-detect the flag), or (b) update PRD §3 to
document `CliArgs` as part of the public API. Option (a) preserves the spec.

### Issue 3: Nondeterministic `CommandResponse` across rapid sequential sends (no pre-send IN-buffer drain)

**Severity**: Major
**PRD Reference**: §7 *Send Path*, §8 *Response Handling*, §14 invariant 4
  ("the device cache … invalidated on any write failure" — reply hygiene is
  part of correct send semantics).
**Expected Behavior**: For a fixed input, `run()` should be deterministic — the
captured reply must be the reply to *this* send, not a leftover from a previous
one.
**Actual Behavior**: `burst_to_one` drains the IN buffer **only after** it
captures a reply, and only **non-blocking** (`read_timeout(0)`, bounded by
`IN_DRAIN_MAX = 32`). There is **no drain before the write**. If a reply from a
prior send arrives in the kernel IN buffer *after* that send's drain loop ended
(USB latency makes this common), the next send's `read_timeout(REPLY_READ_TIMEOUT_MS)`
captures that **stale** reply instead of its own.
Observed by sending the same single-report input 10× in a tight loop through the
public `send_raw_report`:
```
1-report "hello" x10:  0 1 0 0 1 1 1 1 1 1   <- same input, mixed results
2-report x10:          0 1 0 1 0 1 1 1 1 1   <- same input, mixed results
```
The first few iterations capture stale `[0]` replies left by the *previous*
test's multi-report bursts; results only stabilize once the buffer happens to
drain. (Note how this compounds Issue 1: for 2-report sends the *correct*
capture would always be the final `[1]`, but staleness sometimes delivers an
even-worse `[0]` and sometimes a lucky stale `[1]`.)
**Steps to Reproduce**: Loop `send_raw_report(&payload, None, None, 0xFF60, 0x61, false)` 10× with no delay for a fixed single-report payload; observe `reply[0]` is not constant.
**Impact**: Any caller that acts on `CommandResponse` per-notification
(qmkonnect's planned handshake, or the CLI in a script) can get a wrong,
timing-dependent answer. For the single-keyboard hot path this is a real
correctness/reliability hazard.
**Suggested Fix**: Drain the IN buffer **before** the burst-write in
`burst_to_one` (same non-blocking `read_timeout(0)` loop already used
post-capture), so each send starts from an empty reply queue. This pairs with
the Issue 1 fix (capture the correct reply, not just any reply).

---

## Minor Issues (Nice to Fix)

### Issue 4: `unsafe` block in `main.rs` contradicts the literal PRD §12 "No `unsafe`" NFR

**Severity**: Minor
**PRD Reference**: §12 *Non-Functional Requirements* — "**No `unsafe`.** All
  HID I/O goes through the `hidapi` crate."
**Expected Behavior**: No `unsafe` code in the crate (the NFR is stated
unconditionally).
**Actual Behavior**: `src/main.rs:101` contains an `unsafe { signal(SIGPIPE, SIG_DFL); }`
block (a hand-rolled `extern "C"` SIGPIPE reset so piping into `head` exits
cleanly instead of panicking on broken pipe).
**Mitigating context**: This is in the **binary** (`main.rs`), not the library,
and is unrelated to HID I/O (which is the actual subject of the §12 sentence).
The SIGPIPE behavior itself is a reasonable Unix-CLI improvement. So this is a
literal-wording violation, not a safety regression in the transport layer.
**Suggested Fix**: Either (a) gate the SIGPIPE shim behind a small safe wrapper
or a crate like `libc`/`nix` (still `unsafe` internally but isolated), (b)
rephrase PRD §12 to "No `unsafe` in HID I/O paths", or (c) accept the deviation
and document it. Lowest priority.

### Issue 5: `--create-config` exposes an undocumented `-c` short flag

**Severity**: Minor
**PRD Reference**: §11 *CLI* table (lists `--create-config` only as a
  removed-feature trap; no short flag documented).
**Actual Behavior**: `src/lib.rs` `build_cli_command` registers `.short('c')`
for `--create-config`, which appears in `--help` output but is not in the PRD
§11 option table.
**Impact**: Cosmetic / doc drift only; behavior is correct (surfaces
`RemovedFeature`).
**Suggested Fix**: Drop the undocumented `-c` short flag, or add it to the PRD
§11 table. (Pre-existing, not introduced by v0.3.0.)

---

## Testing Summary

- Total tests performed: ~45 distinct probes across build/test/lint, pure-function
  edge cases, CLI parsing matrix, and live hardware bursts (direct hidapi + via
  the public crate API), plus firmware source cross-check.
- Passing: 65 unit/integration tests (`cargo test`) + `cargo clippy` clean +
  `cargo build --release` clean. All `parse_hex_or_decimal`,
  `build_command_data`, `parse_reply`, `device_matches`, `batches_for`, and CLI
  edge cases behave correctly.
- Failing / incorrect runtime behavior: 1 critical (multi-report reply capture),
  observed live and proven via direct hidapi reads + firmware source.
- Areas with good coverage:
  - Pure framing functions (`build_command_data` for every `RunCommand` variant
    incl. count-clamp at 255, layer=None⇒0xFF, clear_board bit 0).
  - Pure reply parser (`parse_reply` across 0x51/0/1/empty/unknown/truncated/
    non-UTF8).
  - Device matching predicate (auto-discovery via `None` VID/PID).
  - CLI mutual-exclusivity, defaults, missing-action, and `parse_hex_or_decimal`
    overflow/whitespace/sign handling.
  - Single-report end-to-end live send + reply capture (legacy device).
- Areas needing attention:
  - **Multi-report reply semantics** (Issue 1) — the v0.3.0 capture-first
    strategy is fundamentally mismatched to the firmware's per-report reply
    model; this is the headline defect.
  - **Reply-queue hygiene across sends** (Issue 3) — no pre-send drain.
  - **Public-API conformance** of `parse_cli_args` (Issue 2) and the literal
    `unsafe` NFR (Issue 4).

### Key invariants re-checked (PRD §14)
1. Magic `0x81 0x9F`, ETX `0x03` — ✅ intact (`burst_to_one`, `ETX_TERMINATOR_BYTE`).
2. `REPORT_LENGTH=32`, 33-byte buffer, 30 payload bytes — ✅ intact.
3. VID/PID `None` ⇒ match any — ✅ intact (`device_matches` `is_none_or`).
4. Cache keyed by `MatchKey`, invalidated on write failure, partial send never retried — ✅ intact.
5. Typed commands reuse same framing + cache — ✅ framing/cache intact, **but reply capture is wrong for multi-report typed commands** (see Issue 1).
6. Reply disambiguation `0x51` / `0` / `1` / timeout — ✅ the *parser* is correct; **but the *captured byte* is the wrong report's byte for multi-report sends** (see Issue 1).
7. Transport-only (no matching/window-detection in this crate) — ✅ intact.
8. Firmware-wins on disagreement — the firmware sends **one reply per report**;
   this crate assumed one reply per burst. **Drift confirmed in the reply-capture
   layer** (Issue 1) — the firmware behavior (PRD §4.4) wins and the crate must
   be corrected to match.