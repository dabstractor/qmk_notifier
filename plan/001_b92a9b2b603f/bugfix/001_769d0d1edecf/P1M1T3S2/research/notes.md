# Research Notes — P1.M1.T3.S2 (Stale-Reply Regression Test)

## Working-tree baseline (verified this session)

- `cargo test --lib` → **70 passed; 0 failed** (44 tests in `src/core.rs` + 26 in `src/lib.rs`).
- The pre-send drain block (S1) is **already present** in `src/core.rs`
  `burst_to_one` at the slot between `request_data[2] = 0x9F;` (line 363) and
  `for batch in 0..batch_count {` (line 377) — `let mut stale_buf = ...` loop,
  `read_timeout(&mut stale_buf, 0)` non-blocking, `Ok(n) if n > 0 => continue / _ => break`.
  ⇒ This PRP assumes POST-S1 state (drain present). After S2 adds 2 tests → **72**.
- The capture-LAST loop (T2.S2) is present (core.rs ~393-408): `for _ in 0..batch_count.max(1)`
  reading with `REPLY_READ_TIMEOUT_MS`, overwriting `reply` each iteration.
- The post-capture surplus drain (`drain_buf`) is present (core.rs ~425-433).

## FakeHid contract (the test seam — DO NOT MODIFY)

`src/core.rs` `mod tests` (lines ~803-862):

- `struct FakeHid { pre_write_replies, post_write_replies, written: Cell<bool>, writes }`
- `impl RawHid for FakeHid`:
  - `write()` → pushes to `writes`, **sets `written=true`**, returns `Ok(len)`.
  - `read_timeout(buf, _timeout)` → picks queue by `written`:
    `false` ⇒ pop `pre_write_replies`; `true` ⇒ pop `post_write_replies`.
    Empty queue ⇒ `Ok(0)` (mirrors hidapi timeout semantics, NOT Err).
- Helpers: `FakeHid::new()` (empty, latch false), `push_pre(Vec<u8>)`, `push_post(Vec<u8>)`.
- Imports already present in `mod tests`: `use std::cell::{Cell, RefCell};` + `use std::collections::VecDeque;`.
  ⇒ New tests can reference `fake.pre_write_replies` directly (same module → private fields visible).

## Sibling tests to mirror (exact style) — `src/core.rs` ~1443-1603

- `fakehid_drives_generic_burst_to_one` — `FakeHid::new()` + `push_post({mut r; r[0]=1; r})`,
  `payload = vec![0u8; 10]`, `batch_count = batches_for(&payload)` asserted `== 1`,
  asserts `success`, `writes.len() == batch_count`, `reply.is_some()`.
- `burst_to_one_captures_last_reply_for_multi_report` — the Issue-1 regression test;
  seeds two `push_post` replies (`[0]` then `[1]`), asserts `reply[0]==1`, `writes.len()==2`.
- `burst_to_one_single_report_unchanged` — single-report, empty pre, post `[1]`,
  asserts `reply[0]==1`, `writes.len()==1`. (This is the existing "clean single-report" test.)

Pattern to follow for the new tests:
- `let fake = FakeHid::new();` then `push_pre(...)` / `push_post(...)`.
- `let payload = vec![0x41u8; 10];` (item specifies 0x41 'A' bytes; 10 bytes ⇒ 1 report).
- `let batch_count = batches_for(&payload); assert_eq!(batch_count, 1, "...");`
- `let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);`
- assert `success`, `reply.expect(...)`, `reply[0]`, and (decisive — see below) pre-queue state.

## ⚠️ CRITICAL INSIGHT — why `reply[0]==1` ALONE is a false-passing test

FakeHid switches the `read_timeout` queue on the `written` latch **at the moment
`write()` is called**. The capture loop runs **after** `write()`, so it reads
**only** from `post_write_replies`. A stale reply sitting in `pre_write_replies`
is therefore **never read by the capture loop** — even if the drain did NOT exist.

Trace WITHOUT the drain (i.e. regression reintroduced):
1. pre = [stale `[0]`], post = [fresh `[1]`].
2. `write()` → `written=true`.
3. capture loop reads `post_write_replies` → fresh `[1]` → `reply[0]==1`. ✅ PASSES.
   (The stale `[0]` in `pre_write_replies` is never touched.)

Trace WITH the drain:
1. pre = [stale `[0]`], post = [fresh `[1]`].
2. **drain** reads `pre_write_replies` (written still false) → pops stale `[0]` (n=33>0 → continue),
   next iter pops empty → `Ok(0)` → break. **pre_write_replies now EMPTY.**
3. `write()` → `written=true`.
4. capture loop reads `post_write_replies` → fresh `[1]` → `reply[0]==1`. ✅ PASSES.

⇒ `assert_eq!(reply[0], 1)` passes in BOTH cases — it does **not** distinguish
drain-present from drain-absent. It is a **necessary** assertion (contract +
confirms capture works) but **not sufficient** to prove the drain ran.

The drain's ONLY observable effect on the fake is that it **empties
`pre_write_replies`** before `write()` flips the latch. Therefore the DECISIVE
regression assertion is:

```rust
assert!(fake.pre_write_replies.borrow().is_empty(),
    "the pre-send drain must consume the stale reply before write() flips the read queue");
```

Verify this is a true regression test: if the drain block is REMOVED, the stale
reply stays in `pre_write_replies` (write() never reads pre) ⇒ `is_empty()` is
`false` ⇒ assertion FAILS. With the drain, it passes. ✓ genuine regression test.

So the stale-drain test asserts BOTH:
1. `reply[0] == 1` (contract: fresh reply captured — item clause 3).
2. `pre_write_replies.is_empty()` (decisive: proves the stale reply was drained pre-write).

This is the single most important finding for this PRP. Without #2 the test is
a false-passing no-op that would let the Issue-3 bug silently return.

## The "empty is clean" companion test

`burst_to_one_empty_in_buffer_is_clean` — empty `pre_write_replies` (the common
no-stale-data case). The drain reads `Ok(0)` and breaks after one iteration (no-op);
normal capture then reads the fresh post reply. Asserts `reply[0]==1` and that the
post reply was captured in full (drain did not steal it). This is the clean-path
baseline twin of the stale-drain test, locking in that the drain (which runs on
EVERY send) does not disturb the no-stale-data common case.

(Overlaps slightly with the existing `burst_to_one_single_report_unchanged`, but
its INTENT is distinct: it is framed as the Issue-3 clean-path baseline. Uses
0x41 'A' payload to visually distinguish from the all-zeros single_report test.)

## Validation expectations

- After adding both tests: `cargo test --lib` → **72 passed; 0 failed**.
- `cargo fmt --check` → exit 0 (rustfmt default; no rustfmt.toml).
- `cargo clippy --all-targets` → zero new warnings.
- Targeted: `cargo test --lib burst_to_one_drains_stale_replies_before_send -- --nocapture` → 1 passed.
- Targeted: `cargo test --lib burst_to_one_empty_in_buffer_is_clean -- --nocapture` → 1 passed.
- `grep -cE "^\s*#\[test\]" src/core.rs` → **46** (was 44, +2).