# Research Notes — P1.M1.T3.S1: Add pre-send drain loop before the burst-write

## Working-tree state at research time (VERIFIED)

The working tree is **POST-S3** (P1.M1.T2.S3 landed). `cargo test --lib` →
**70 passed; 0 failed** (44 tests in core.rs `mod tests`, 26 in lib.rs).

Already landed (CONSUMED — do NOT modify):
- **S1** (`P1.M1.T1.S1` + `P1.M1.T1.S2`): `pub(crate) trait RawHid` (core.rs:13-30)
  + blanket impl for `hidapi::HidDevice` + `fn burst_to_one<T: RawHid>(...)` (core.rs:343).
- **S2** (`P1.M1.T2.S2`): the capture-LAST loop `for _ in 0..batch_count.max(1)`
  (core.rs:388-401) that overwrites `reply` each iteration, keeping the ETX reply.
- **FakeHid double** (`P1.M1.T2.S1`, core.rs:785-836): two-queue model
  (`pre_write_replies` / `post_write_replies`) + one-shot `written` `Cell<bool>`
  latch + helpers `new()` / `push_pre()` / `push_post()`. **FakeHid's own doc
  comment (core.rs:773-783) already anticipates THIS subtask**: the
  `pre_write_replies` comment says it is "the kind a prior send leaves behind;
  **flushed by the Issue-3 pre-send drain in P1.M1.T3.S1**." So the test seam
  was designed for this drain from day one.
- **S3** (`P1.M1.T2.S3`): two capture regression tests
  (`burst_to_one_captures_last_reply_for_multi_report`,
  `burst_to_one_single_report_unchanged`) appended to `mod tests`.

Confirmed: **NO pre-send drain code exists yet** —
`grep -n "stale_buf\|before the write" src/core.rs` returns only the FakeHid
comment at core.rs:775 (the only `pre-send` string anywhere in the file).

## Exact placement anchor (located by CONTENT grep, NOT the item's stale line numbers)

The item description cites `core.rs:307` (request_data init), `core.rs:315`
(for-batch loop), `core.rs:279` (doc comment). These are **STALE** — they predate
the S1/S2/S3 landings and no longer match the working tree. The CURRENT anchors
(verified this session):

```
core.rs:343   fn burst_to_one<T: RawHid>(
core.rs:348       ) -> (bool, Option<Vec<u8>>) {
core.rs:349       let mut request_data = [0u8; REPORT_LENGTH + 1]; // stack array (was vec!)
core.rs:350       request_data[1] = 0x81;
core.rs:351       request_data[2] = 0x9F;
core.rs:352       (blank)
core.rs:353       for batch in 0..batch_count {
```

**The pre-send drain block is inserted at the blank line 352** — i.e. immediately
after `request_data[2] = 0x9F;` (line 351) and immediately before
`for batch in 0..batch_count {` (line 353). Locate the insertion point with:

```bash
grep -n "request_data\[2\] = 0x9F" src/core.rs   # → 351
```

(Do NOT hardcode line 352 in the edit — S3's parallel test appends could shift
downstream lines; anchor on the content above.)

## The pattern to replicate (the existing POST-capture surplus drain)

core.rs:400-415 — the canonical non-blocking drain already in `burst_to_one`:

```rust
let mut drain_buf = [0u8; REPORT_LENGTH + 1];
for _ in 0..IN_DRAIN_MAX {
    match interface.read_timeout(&mut drain_buf, 0) {
        Ok(n) if n > 0 => continue,
        _ => break,
    }
}
```

The pre-send drain is the SAME shape, with a `stale_buf` (semantic name — it
holds stale IN-side replies, not surplus post-capture replies). Both the item
description (LOGIC clause 3) and `architecture/reply_capture_design.md` §Step 3
give the identical verbatim code.

Constants (verified): `REPORT_LENGTH = 32` (core.rs:38), `REPORT_LENGTH + 1 = 33`
(buffer), `IN_DRAIN_MAX = 32` (core.rs:136), `PAYLOAD_PER_REPORT = 30`
(core.rs:126), `REPLY_READ_TIMEOUT_MS = 1000` (core.rs:69).

## CRITICAL correctness proof — the drain is a NO-OP in every existing test

Why the 70 existing tests remain GREEN with zero modification:

1. FakeHid's `read_timeout` selects its queue by `self.written.get()`:
   - `false` → pops `pre_write_replies`
   - `true`  → pops `post_write_replies`
2. The pre-send drain runs BEFORE the `for batch` write loop, i.e. BEFORE any
   `write()` call. So `written` is still `false` → the drain reads from
   **`pre_write_replies`**.
3. **Every existing test** (all 70) uses ONLY `push_post(...)` to seed replies.
   NONE call `push_pre(...)`. So `pre_write_replies` is **empty** in every test.
4. Popping an empty queue returns `Ok(0)` (FakeHid mirrors hidapi's
   timeout/no-data semantics — NOT an error).
5. `Ok(0)` matches the `_ => break` arm → the drain loop executes **exactly one
   read** (returning `Ok(0)`) and breaks. One iteration, zero data consumed.

Therefore the pre-send drain:
- Consumes **nothing** from `post_write_replies` (the capture tests' queue is
  untouched — `written` is still false during the drain).
- Asserts on `fake.writes.borrow().len()`, NOT on read counts, so the extra
  pre-write `read_timeout(0)` call doesn't perturb any existing assertion.

**Net effect on existing tests: one extra `Ok(0)` read before the write loop,
discarded. 70 passed → 70 passed.** This is the single most important thing the
implementer must understand (and is why NO existing test needs updating).

## The regression test for this drain is a SEPARATE subtask (P1.M1.T3.S2)

This subtask (S1) adds ONLY production code (the drain block) + a doc-comment
update. The **stale-reply regression test** — which uses `push_pre(vec![0u8;33])`
to seed stale data into `pre_write_replies`, then asserts `reply[0]==1` (the
fresh post-write reply, not the stale `[0]`) — is the contract of
**`P1.M1.T3.S2`** (separate PRP). Do NOT add that test here. (The design doc's
"Test: Pre-Send Drain (Issue 3 regression)" block is the S2 deliverable; it uses
struct-literal FakeHid construction which S1 replaced with `push_pre()` — the S2
PRP will reconcile that.)

## Verbatim drain block to insert (from item LOGIC clause + design doc §Step 3)

```rust
    // Drain any stale IN-side replies left by a prior send (Issue 3). USB latency
    // can deliver a prior send's reply after that send's drain loop ended; without
    // this pre-send flush, the next send's bounded read would capture the stale
    // reply instead of its own.
    let mut stale_buf = [0u8; REPORT_LENGTH + 1];
    for _ in 0..IN_DRAIN_MAX {
        match interface.read_timeout(&mut stale_buf, 0) {
            Ok(n) if n > 0 => continue,
            _ => break,
        }
    }
```

## Doc comment update (DOCS clause — Mode A, code-internal only)

`burst_to_one`'s existing doc comment starts at "/// Burst-write `data` to a
single device..." (immediately above `fn burst_to_one<T: RawHid>` at core.rs:343).
The item requires the comment to "mention the pre-send drain." Cleanest edit:
(a) augment the overview sentence to list drain-stale as the FIRST step, and
(b) add a dedicated "Pre-send drain (v0.3.1)" paragraph mirroring the existing
"Reply capture (v0.3.1)" paragraph style (both describe the read_timeout
semantics + the loop bound). Concrete suggested text is in the PRP's
"Implementation Blueprint".

## Validation expectations (verified against working tree)

- `cargo build` → 0 warnings (drain code is identical shape to existing drain;
  `stale_buf` is `&mut`-used by `read_timeout`, same as `drain_buf`, so no
  "unused" / "unnecessary_mut_passed" lint).
- `cargo clippy --all-targets` → 0 new warnings (the match arm
  `Ok(n) if n > 0 => continue, _ => break` is the established file idiom).
- `cargo fmt --check` → exit 0 (after a `cargo fmt`).
- `cargo test --lib` → **70 passed; 0 failed** (UNCHANGED — no new tests in this
  subtask; existing tests are no-op-affected per the proof above).

## Why this is the right, minimal fix (Issue 3, PRD §7/§8/§14 inv 4)

The firmware emits one 32-byte reply per report processed (PRD §4.4). The
post-capture drain (core.rs:400-415) already discards *surplus* replies left by
the *current* send. But a prior send's reply can arrive in the kernel IN buffer
*after* that prior send's drain loop ended (USB latency). Without a pre-send
flush, the next send's bounded `read_timeout(REPLY_READ_TIMEOUT_MS)` captures
that stale reply instead of its own — the nondeterminism observed live
("1-report 'hello' x10: 0 1 0 0 1 1 1 1 1 1"). Replicating the SAME non-blocking
`read_timeout(0)` loop BEFORE the write makes each send start from an empty reply
queue, restoring determinism for a fixed input. This pairs with the Issue-1 fix
(S2): capture the correct reply, AND ensure no stale reply pollutes the capture.