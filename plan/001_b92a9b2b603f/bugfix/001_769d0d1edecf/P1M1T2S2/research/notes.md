# Research Notes — P1.M1.T2.S2: Rewrite capture logic to keep last reply (ETX-report reply)

Empirical verification of the working-tree state. Performed by direct read +
`grep -n` + `cargo test --lib`. **IMPORTANT:** line numbers below are the
*current working-tree* numbers — they shifted when S1 landed (and may shift
again); locate every edit anchor by **content grep**, not line number.

## Working-tree state correction (CRITICAL)

The parallel sibling S1 (FakeHid) is described as "currently being implemented",
but in the **actual working tree its changes are already present (uncommitted)**:

```
git status --short
 M src/core.rs                              # S1's FakeHid + 3 smoke tests live here
 M plan/.../tasks.json
?? plan/.../P1M1T2S1/                       # S1's PRP (untracked)
```

```
grep -n "struct FakeHid" src/core.rs   -> 773
grep -n "impl RawHid for FakeHid"      -> 781
grep -n "impl FakeHid"                 -> 804
grep -n "fn fakehid_" src/core.rs      -> 1408, 1450, 1464   (3 smoke tests)
```

Therefore S2's **real baseline is 68 tests**, NOT the 65 the design doc cites
(the doc predates S1). Verified:
```
cargo test --lib 2>&1 | tail -3
test result: ok. 68 passed; 0 failed; ...   (65 pure-fn + 3 fakehid smoke tests)
cargo test --lib fakehid_ -> 3 passed
```

S2 adds NO tests of its own (the Issue-1 regression test is S3) ⇒ expected
post-state is **still 68 passed, 0 failed**.

## S1's smoke tests survive the S2 capture rewrite (verified by trace)

All 3 S1 smoke tests are capture-**agnostic** (assert success / writes.len() /
reply.is_some(), never the reply byte). Tracing the NEW loop against them:

- `fakehid_write_records_calls_and_switches_read_queue` (1408): calls
  `fake.read_timeout` / `fake.write` DIRECTLY — never enters `burst_to_one`.
  Unaffected. ✓
- `fakehid_read_timeout_returns_zero_when_queue_empty` (1450): direct
  `read_timeout` call, no `burst_to_one`. Unaffected. ✓
- `fakehid_drives_generic_burst_to_one` (1464): 1 post reply, payload 10 bytes ⇒
  batch_count=1. New loop `for _ in 0..1.max(1)` = 1 iteration ⇒ reads the one
  reply ⇒ captures it ⇒ `reply.is_some()` true; writes.len()==1==batch_count.
  ✓ Still passes.

So S2's edit does NOT break any existing test.

## The capture logic to rewrite (current working-tree lines)

`fn burst_to_one<T: RawHid>` is at **core.rs:337** (LANDED, generic — from
P1.M1.T1.S2). Its capture block is:

```
370     // Capture the FIRST device reply with a bounded timeout (v0.3.0). ...
376     let mut reply: Option<Vec<u8>> = None;
377     let mut read_buf = [0u8; REPORT_LENGTH + 1];
378     match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
379         Ok(n) if n > 0 => {
380             reply = Some(read_buf[..n].to_vec());
381             if verbose { println!("Captured device reply: {} bytes", n); }
382         }
383         _ => {} // Ok(0) = timeout, Err = read failure ⇒ reply stays None
384     }
```

The drain loop (KEEP UNCHANGED — it's the surplus safety net) is:

```
400     let mut drain_buf = [0u8; REPORT_LENGTH + 1];
401     for _ in 0..IN_DRAIN_MAX {
402         match interface.read_timeout(&mut drain_buf, 0) {
403             Ok(n) if n > 0 => continue,
404             _ => break,
405         }
406     }
407     (true, reply)
```

## Doc comments that reference "first" (must be updated — Mode A, in-file only)

Locate by content grep. Current working-tree text:

```
grep -n "first" src/core.rs   (within the two doc blocks + REPLY_READ_TIMEOUT_MS)
```

1. **burst_to_one summary** (core.rs:309):
   `/// reports, then CAPTURE the first device reply (bounded wait), then drain any`
2. **burst_to_one #Return-ish** (core.rs:313):
   `/// `Option<Vec<u8>>` is the FIRST captured IN report, decoded downstream by`
3. **burst_to_one Reply-capture para** (core.rs:316-324): starts
   `/// Reply capture (v0.3.0): after the burst-write succeeds, the FIRST IN report is`
4. **send_raw_report summary** (~155-157):
   `/// total failure. Returns the FIRST device reply captured by the burst-write path.`
5. **send_raw_report #Ok(Some)** (~161-165):
   `///   first device replied within `REPLY_READ_TIMEOUT_MS` ... `bytes` is that first reply's raw IN report`
6. **REPLY_READ_TIMEOUT_MS doc** (~68-73, locate via `grep -n "first-reply capture"`):
   `/// Bounded timeout (ms) for reading the first typed reply after a burst`
   `/// (`burst_to_one`'s first-reply capture).`
   (This is a 3rd doc; contract item 5 scopes burst_to_one + send_raw_report, but
   this constant doc also says "first" — optional consistency fix.)

## The replacement loop (canonical, from design doc §Step 4)

```rust
let mut reply: Option<Vec<u8>> = None;
let mut read_buf = [0u8; REPORT_LENGTH + 1];
for _ in 0..batch_count.max(1) {
    match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
        Ok(n) if n > 0 => {
            reply = Some(read_buf[..n].to_vec()); // overwrite ⇒ keep LAST (ETX) reply
            if verbose {
                println!("Captured device reply: {} bytes", n);
            }
        }
        _ => break, // Ok(0) = timeout, Err = read failure ⇒ stop
    }
}
```

- `batch_count.max(1)` guards batch_count==0 (shouldn't happen; `batches_for`
  returns 0 only for empty data — `run()`/`send_raw_report` always have ≥1 byte
  of payload, but defensive `.max(1)` matches the design doc).
- Each iteration OVERWRITES `reply` ⇒ last non-empty reply wins = the ETX reply
  (firmware emits one reply per report; only the ETX report carries the result).
- `read_buf` reused across iterations is SAFE: `read_buf[..n].to_vec()` clones
  the prefix into an owned Vec each capture — no aliasing across iterations.
- KEEP the verbose `println!` (preserves existing debug behavior; now fires once
  per captured reply, which is accurate).

## Latency characteristic (by-design, not a blocker)

For a NON-responding device, worst case = `batch_count × REPLY_READ_TIMEOUT_MS`.
For batch_count=2 that's ~2s (2 × 1000ms) instead of ~1s. For a RESPONDING
device, each read returns within milliseconds (firmware replies promptly), so no
real latency cost. The design doc accepts REPLY_READ_TIMEOUT_MS=1000 as
"conservative". This is the spec'd behavior — do not "optimize" by lowering the
per-read timeout or breaking early; the loop must read up to batch_count replies.

## Why no external/web research was needed

Pure internal-Rust logic rewrite (single `match` → bounded `for` loop) + in-file
doc-comment text edits. No new crate, no new trait, no new API surface. The
canonical loop and the exact "keep last" semantics are given verbatim in
`reply_capture_design.md` §Step 4 + §Edge Cases (read first-hand). The RawHid
trait + generic `burst_to_one` it runs against are already landed and verified.
All non-obvious facts (baseline 68, S1-already-landed, surviving smoke tests,
read_buf reuse safety, latency characteristic) were verified empirically above.

## Parallel-execution (S1) interaction

S1's changes (FakeHid + 3 smoke tests) live in the `#[cfg(test)] mod tests`
block (core.rs:754-821, 1408-1480). S2 edits a DISJOINT region: `burst_to_one`
body (337-407) + its doc comment (307-336) + `send_raw_report` doc (~155-165) +
optionally REPLY_READ_TIMEOUT_MS doc (~68-73). **No textual overlap** ⇒ S1 and S2
compose cleanly regardless of commit order. S2 must NOT touch `mod tests`,
`FakeHid`, or the smoke tests.