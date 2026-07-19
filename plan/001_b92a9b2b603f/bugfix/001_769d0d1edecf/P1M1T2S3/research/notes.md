# Research Notes — P1.M1.T2.S3 (Multi-report reply-capture regression test)

> Test-only subtask. Two new `#[test]` fns appended to `src/core.rs`'s
> `#[cfg(test)] mod tests` that lock the Issue-1 capture-last fix (P1.M1.T2.S2).

## 1. Working-tree reality (verified this session)

| Item | State | Evidence |
|------|-------|----------|
| `RawHid` trait + `impl RawHid for hidapi::HidDevice` (S1.T1) | LANDED | `grep "trait RawHid" src/core.rs` → core.rs:13–31 |
| Generic `burst_to_one<T: RawHid>` (S1.T2) | LANDED | core.rs:337 |
| **`FakeHid` double + 3 `fakehid_*` smoke tests (S1)** | **LANDED** | `struct FakeHid` core.rs:773; `impl RawHid` 781; `impl FakeHid` (helpers `new`/`push_pre`/`push_post`) 804–823; smoke tests 1408–1487 |
| **Capture-last rewrite (S2)** | **NOT YET LANDED** (parallel) | current capture block core.rs:370 still says "Capture the **FIRST** device reply" (single `match`) |
| `mod tests` closing `}` | core.rs:1489 | last test `fakehid_drives_generic_burst_to_one` ends 1487 |
| lib test baseline (S1-only tree) | **68 passed** | S1 added 3 → 65+3; S2 adds 0; S3 adds **2 → 70 final** |

**Hard prerequisite chain**: S1 (FakeHid) ✓ present → S2 (capture-last loop) assumed present
(treat S2 PRP as contract) → **S3 (this test)** asserts the post-fix byte value.

## 2. The landed `FakeHid` API (THIS is the contract — not the design doc's sketch)

The design doc (`reply_capture_design.md` §FakeHid Test Double) sketches `FakeHid`
with **struct-literal construction** (`FakeHid { pre_write_replies: ..., ... }`).
S1 landed something **better**: helper-method construction. The S3 tests MUST use
the landed helpers, because:

- The struct fields are private to `mod tests` and reachable by literal, but
  `new()` + `push_post()` is the idiomatic, clippy-clean, drift-proof API.
- S1's 3 smoke tests already establish `FakeHid::new()` + `push_post()` as the
  canonical pattern (`fakehid_drives_generic_burst_to_one`, core.rs:1464–1487).

```rust
// LANDED (core.rs:773–823). S3 consumes this — do NOT redefine or modify.
impl FakeHid {
    fn new() -> Self { ... }              // empty, written-latch false
    fn push_pre(&self, reply: Vec<u8>);   // queue a PRE-write (stale) reply
    fn push_post(&self, reply: Vec<u8>);  // queue a POST-write (firmware) reply
}
// impl RawHid: write() records + sets latch; read_timeout() pops post_write
//   queue after first write(), pre_write queue before; empty queue => Ok(0).
```

## 3. The exact test bodies (reconciled with the landed API)

Source: `architecture/reply_capture_design.md` §"Test: Multi-Report Capture".
Reconciliation: `FakeHid { ... struct literal ... }` → `FakeHid::new()` +
`push_post()` (the landed helpers). The byte assertions are unchanged.

### Test 1 — `burst_to_one_captures_last_reply_for_multi_report` (Issue-1 gate)
- 31-byte payload ⇒ `batches_for == 2` (math: `(31+32-3)/30 = 60/30 = 2`).
- Seed post_write queue with **2 replies**: `[0u8;33]` (intermediate, no ETX ⇒
  `match==false ⇒ response[0]==0`) then `{[0]=1, rest 0}` (ETX report ⇒ real
  match-bool `1`). This mirrors the firmware's proven `[0,1]` stream
  (TEST_RESULTS.md: "2-report 'hello'+25 dots: replies = [0, 1]").
- Assert: `success==true`, `reply.is_some()`, `reply[0]==1` (ETX reply, NOT the
  intermediate `0`), `fake.writes.borrow().len()==2`.

### Test 2 — `burst_to_one_single_report_unchanged` (anti-regression guard)
- 10-byte payload ⇒ `batches_for == 1`.
- Seed post_write queue with **1 reply**: `{[0]=1, rest 0}`.
- Assert: `success==true`, `reply.is_some()`, `reply[0]==1`,
  `fake.writes.borrow().len()==1`. Proves the capture-last loop (`batch_count.max(1)`)
  still captures the lone reply for `batch_count==1` — the pre-S2 path that was
  always correct stays correct.

## 4. WHY the tests FAIL before S2 and PASS after (the regression-gate contract)

This is the most important subtlety for the implementer. `FakeHid` seeded with
`post_write = [[0u8;33], {[0]=1,…}]`, `batch_count = 2`:

| Capture logic | Loop | reply captured | `reply[0]` | Test result |
|---|---|---|---|---|
| **OLD (capture-first)** — current tree | single `match` | 1st reply `[0u8;33]`; 2nd `[1…]` reply drained & discarded | `0` | **FAIL** `assert_eq!(reply[0], 1)` |
| **NEW (S2 capture-last)** — the fix | `for _ in 0..batch_count.max(1)` | iter0: `[0u8;33]` (overwrite); iter1: `[1…]` (overwrite ⇒ last wins) | `1` | **PASS** |

→ These tests are a **regression gate**: they are RED until S2 lands, GREEN
after. If the orchestrator runs S3 in a tree where S2 is absent, the 2 new tests
will fail — that is **correct** behavior (it means the bug is still present).
The expected FINAL state (S1+S2+S3 all landed) is **70 passing, 0 failed**.

This is why the S2 PRP explicitly defers this test to S3 ("This task delivers
the FIX; S3 locks it") and the S1 smoke tests are deliberately **capture-agnostic**
(assert only `success`/`writes.len()`/`reply.is_some()`, never `reply[0]`) so
they stay GREEN across both the capture-first and capture-last logic — leaving
the byte-value assertion to S3 where it belongs.

## 5. Conventions to match (from existing `mod tests`)

- `use super::*;` brings `RawHid`, `burst_to_one`, `batches_for` into scope (no extra import).
- Descriptive snake_case test names; top-of-body `//` comment explaining intent.
- Multi-line `push_post` closure (`let mut r = vec![0u8; 33]; r[0] = 1; r`) —
  matches S1's smoke-test style exactly (core.rs:1411–1416, 1466–1470).
- `assert!(success, "msg")` and `assert_eq!(actual, expected, "msg")` with messages.
- Append at END of `mod tests` (after `fakehid_drives_generic_burst_to_one`,
  before the closing `}` at core.rs:1489).

## 6. Verified commands (this repo, default toolchain)

```bash
cargo fmt                 # default rustfmt, no rustfmt.toml
cargo build               # 0 warnings
cargo clippy --all-targets # --all-targets lints #[cfg(test)] code where FakeHid lives
cargo fmt --check         # exit 0
cargo test --lib          # final: 70 passed, 0 failed (68 + 2 new)
cargo test --lib burst_to_one_captures_last_reply_for_multi_report -- --nocapture
cargo test --lib burst_to_one_single_report_unchanged -- --nocapture
```

## 7. Scope boundary (what NOT to do)

- Do NOT modify `FakeHid`, `burst_to_one`, `RawHid`, `batches_for`, or any
  non-test code — this subtask is **append-only** to `mod tests`.
- Do NOT add a 3rd test (e.g. typed-reply, or N=4 report). The contract is
  exactly 2 tests (multi-report + single-report-unchanged). T3's stale-reply
  test is a separate subtask (P1.M1.T3.S2).
- Do NOT use struct-literal `FakeHid { ... }` construction — use the landed
  `new()` + `push_post()` helpers (the design doc's literal form is stale).
- Do NOT add `#[dev-dependencies]` or a mocking crate (hand-rolled double only).