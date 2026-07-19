# Research Notes — P1.M1.T2.S1: Add FakeHid test double under #[cfg(test)]

## Task recap
Add a hand-rolled `FakeHid` test double that implements the `RawHid` trait, so
the (now-generic) `burst_to_one` can be unit-tested without a physical QMK
keyboard. FakeHid has two reply queues (`pre_write_replies` = stale data read
before any write; `post_write_replies` = firmware replies read after write),
switched by a `Cell<bool>` "written" flag, plus a recorded-writes log. Lives
only under `#[cfg(test)]`. No `[dev-dependencies]`, no mocking framework.

## Inputs (already landed in the working tree)
- **`RawHid` trait** (P1.M1.T1.S1 — COMPLETE) at `src/core.rs:13`:
  ```rust
  pub(crate) trait RawHid {
      fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError>;
      fn read_timeout(&self, buf: &mut [u8], timeout: i32) -> Result<usize, hidapi::HidError>;
  }
  ```
  `impl RawHid for hidapi::HidDevice` at `src/core.rs:21` (FQS delegation).
- **Generic `burst_to_one`** (P1.M1.T1.S2 — LANDED in tree) at `src/core.rs:337`:
  ```rust
  fn burst_to_one<T: RawHid>(interface: &T, data: &[u8], batch_count: usize, verbose: bool) -> (bool, Option<Vec<u8>>)
  ```
  Body calls `interface.write(&request_data)`, then captures ONE reply via
  `interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS)`, then drains
  surplus via `interface.read_timeout(&mut drain_buf, 0)`. (The capture-FIRST
  behavior is the bug fixed in T2.S2; the pre-send drain is added in T3.S1.)

The P1.M1.T1.S2 PRP explicitly lists this task as its first downstream consumer:
"P1.M1.T2.S1: FakeHid test double: impl RawHid for FakeHid under #[cfg(test)]".
Treat S1+S2 as a CONTRACT — FakeHid must NOT modify them.

## Canonical design (from reply_capture_design.md §FakeHid Test Double)
The design doc gives the struct + impl verbatim:
```rust
#[cfg(test)]
struct FakeHid {
    pre_write_replies:  RefCell<VecDeque<Vec<u8>>>,  // stale data, drained before write (Issue 3)
    post_write_replies: RefCell<VecDeque<Vec<u8>>>,  // firmware replies, read after write
    written:            Cell<bool>,                  // write() called at least once?
    writes:             RefCell<Vec<Vec<u8>>>,       // recorded write() calls for assertion
}

impl RawHid for FakeHid {
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError> {
        self.writes.borrow_mut().push(data.to_vec());
        self.written.set(true);
        Ok(data.len())
    }
    fn read_timeout(&self, buf: &mut [u8], _timeout: i32) -> Result<usize, hidapi::HidError> {
        let queue = if self.written.get() { &self.post_write_replies } else { &self.pre_write_replies };
        match queue.borrow_mut().pop_front() {
            Some(reply) => { let n = reply.len().min(buf.len()); buf[..n].copy_from_slice(&reply[..n]); Ok(n) }
            None => Ok(0),  // empty queue ⇒ timeout semantics (matches hidapi)
        }
    }
}
```
Contract #3 additionally requires helpers: `FakeHid::new()` (empty queues,
written=false), `push_post(reply)`, `push_pre(reply)`.

## Two-queue + Cell<bool> rationale (why this shape)
- Models the firmware's reply semantics + the upcoming Issue-3 pre-send drain:
  - **Before write()**: reads return stale IN-buffer data (`pre_write_replies`),
    which T3.S1's pre-send drain will flush.
  - **After write()**: reads return the firmware's fresh per-report replies
    (`post_write_replies`).
- `written` is a one-shot latch (never reset) — correct for a single
  `burst_to_one` call per FakeHid instance (tests are single-use).
- This shape lets the SAME double drive BOTH the T2.S3 multi-report-capture
  regression and the T3.S2 stale-reply regression without redesign.

## Placement decision
- Inside the existing `#[cfg(test)] mod tests` block (`src/core.rs:750`),
  directly after the `use` statements (lines 751-753). Flat (NOT a nested
  `mod fake`) — simpler referencing, matches the design doc, and `use super::*;`
  already brings `RawHid` + all core.rs items into scope.
- Must ADD two std imports (core.rs has neither today):
  `use std::cell::{Cell, RefCell};` and `use std::collections::VecDeque;`.
- Omit the redundant `#[cfg(test)]` attribute on the struct itself — it's
  already inside a `#[cfg(test)]` module (a nested attr is harmless but noisy;
  the design doc only includes it because it shows the struct at module level).

## CRITICAL: dead_code in `cargo test`
FakeHid is `#[cfg(test)]` ⇒ NOT compiled by `cargo build` (no build-time
warning). But `cargo test` DOES compile it, and an unreferenced private struct +
its unreferenced fields/methods emit dead_code warnings (`field is never read`,
`method is never used`, etc.).
→ Mitigation: the smoke tests MUST reference every member (all 4 fields via
construction + `writes` reads; `new`/`push_pre`/`push_post`; both `RawHid`
methods). This is also the proof that FakeHid is a faithful double. (Verified
the codebase currently builds with zero warnings — must stay that way.)

## CRITICAL: smoke tests must be CAPTURE-AGNOSTIC
Current `burst_to_one` captures the FIRST reply (the Issue-1 bug). T2.S2
rewrites it to keep the LAST reply. T3.S1 adds a pre-send drain. So:
- DO assert: `success == true`, `fake.writes.borrow().len() == batch_count`,
  `reply.is_some()` (when a post_write reply is queued).
- DO NOT assert: the captured reply's byte value / which reply was captured
  (that is T2.S3's regression-test job and would couple to the not-yet-fixed
  capture logic).
- Stability proof of the chosen assertions across T2.S2/T3.S1: with ONE
  post_write reply queued and an EMPTY pre_write queue, the write succeeds (Ok),
  the capture yields Some (one reply), and (after T3.S1) the pre-send drain sees
  Ok(0) on the empty pre queue and breaks immediately — none of which changes
  success / write-count / is_some.

## Compile details / gotchas
- Return type of both `RawHid` methods is `Result<usize, hidapi::HidError>`
  (fully-qualified `hidapi::HidError` — matches the trait exactly). FakeHid
  never errors, so `Ok(...)` suffices.
- `read_timeout`'s `timeout: i32` param is unused by FakeHid → name it
  `_timeout` (clippy `unused_variables` clean).
- `queue` binding in read_timeout: both branches yield `&RefCell<VecDeque<Vec<u8>>>`
  (same type) → compiles; `&self.field` is NOT a needless borrow (it's needed to
  unify the branch types for the binding). clippy-clean.
- `write()` returns `Ok(data.len())`; burst_to_one only checks `if let Err(e)`,
  so the count is irrelevant — `data.len()` is a fine stand-in.
- RefCell borrow discipline: each method borrows + drops within the call; no
  nested/aliased borrows ⇒ no runtime panic.
- No `#[derive(Debug)]` needed (burst_to_one never prints the interface; default
  clippy doesn't require Debug).

## Baseline (verified)
- `cargo build` → zero warnings (S2 cleared the S1 `RawHid` dead_code warning).
- `cargo test --lib` → **65 passed** (core.rs 39 + lib.rs 26).
- `src/core.rs` imports today: `use crate::error::QmkError; use hidapi::{HidApi, HidDevice}; use std::sync::{LazyLock, Mutex, MutexGuard};` (no cell/collections).
- Test module opening (`src/core.rs:750-753`):
  `#[cfg(test)] mod tests { use super::*; use crate::{CommandResponse, HostOs, RunCommand};`
- After this task: core.rs 39 → 42 (3 smoke tests); total 65 → 68.

## Scope boundaries / anti-collision
- ONLY `src/core.rs` is modified (add FakeHid + helpers + imports + 3 smoke
  tests inside the existing `mod tests`). NOT lib.rs, error.rs, main.rs, Cargo.toml.
- Do NOT modify the `RawHid` trait/impl (S1) or `burst_to_one` (S2) — consume them.
- Do NOT write the Issue-1 multi-report regression test (that is T2.S3) or the
  Issue-3 stale-reply regression (T3.S2). This task only delivers the double +
  proves it is a faithful RawHid.
- DOCS: none — test-only code, no public surface, no README/PRD change.

## Smoke-test plan (3 tests)
1. `fakehid_write_records_calls_and_switches_read_queue` — new() + push_pre +
   push_post + write (records + flips written) + read_timeout (pre-queue before
   write, post-queue after). Covers: new, push_pre, push_post, write, read_timeout,
   queue switching, all 4 fields.
2. `fakehid_read_timeout_returns_zero_when_queue_empty` — empty queue ⇒ Ok(0)
   (hidapi timeout semantics, NOT an error). Covers the None arm.
3. `fakehid_drives_generic_burst_to_one` — FakeHid as `&impl RawHid` for
   burst_to_one; assert success + writes.len()==batch_count + reply.is_some()
   (capture-agnostic). Covers the "usable as burst_to_one(&fake,…)" contract.