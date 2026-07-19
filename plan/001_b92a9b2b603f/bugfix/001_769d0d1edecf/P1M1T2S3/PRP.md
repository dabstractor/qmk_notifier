# PRP — P1.M1.T2.S3: Add multi-report reply-capture regression test

---

## Goal

**Feature Goal**: Lock the Issue-1 fix (P1.M1.T2.S2's capture-last rewrite) with
**two new `#[test]` functions** appended to `src/core.rs`'s existing
`#[cfg(test)] mod tests` block. The first proves a **2-report** payload captures
the **ETX-report reply** (the real match-bool `[1]`), not the intermediate `[0]`
reply the firmware emits for report 1 — i.e. it encodes the exact live failure
proven in `TEST_RESULTS.md` ("2-report 'hello'+25 dots: replies = [0, 1]"). The
second proves the **single-report** path (which was never buggy) still captures
its one reply correctly, guarding against the capture-last rewrite regressing
the `batch_count == 1` case. Both tests drive `burst_to_one` through the landed
`FakeHid` double (P1.M1.T2.S1) — **no hardware, no `dev-dependencies`**.

**Deliverable**: Two `#[test]` fns — `burst_to_one_captures_last_reply_for_multi_report`
and `burst_to_one_single_report_unchanged` — appended at the END of `mod tests`
in **`src/core.rs` only** (after `fakehid_drives_generic_burst_to_one`, before
the module's closing `}`). Each test builds a `FakeHid::new()` + `push_post(...)`
reply stream, calls `burst_to_one(&fake, &payload, batch_count, false)`, and
asserts on `success` / `reply.is_some()` / **`reply[0]`** (the byte value S1's
smoke tests deliberately left un-asserted) / `fake.writes.borrow().len()`.

**Success Definition**: `cargo build` → zero warnings; `cargo clippy --all-targets`
→ zero new warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **70
passed, 0 failed** (68 baseline from S1+S2 + 2 new). Both new tests pass
**individually**. No file other than `src/core.rs` is modified; `FakeHid`,
`burst_to_one`, `RawHid`, `batches_for`, and all existing tests are **untouched**.

## User Persona (if applicable)

**Target User**: The maintainer of the `qmk_notifier` crate and its downstream
consumer `qmkonnect` — anyone who needs assurance that the multi-report reply
bug (Issue 1) is fixed and will not silently regress.

**Use Case**: CI runs `cargo test --lib` on every commit; these two tests are
the automated proof that `run()` returns the ETX-report reply for payloads > 30
bytes (the dominant qmkonnect window-string shape, e.g.
`"code.Code — Bug Report — qmk_notifier"` ≈ 44 bytes ⇒ 2 reports).

**User Journey**: A future change touches `burst_to_one`'s capture/drain → CI
runs the suite → `burst_to_one_captures_last_reply_for_multi_report` fails the
instant capture reverts to first-reply semantics → the regression is caught
before merge, no keyboard required.

**Pain Points Addressed**: Before this, the Issue-1 fix (S2) had no in-CI
verification — it was provably correct only by reading the loop against the
design doc's Edge Cases table or by reproducing on live hardware. These tests
make correctness a compile-and-run check.

## Why

- **Regression-locking is the explicit deliverable**. S2 (the fix) deliberately
  adds **zero** tests ("S3 locks it") and S1's smoke tests are deliberately
  **capture-agnostic** (they assert only `success` / `writes.len()` / `is_some()`,
  never the captured byte, so they stay green across the capture rewrite). The
  byte-value assertion — the thing that actually distinguishes capture-first from
  capture-last — lives **here**, in S3. Without it, the fix is unverified in CI.
- **Firmware-wins contract (PRD §14 invariant 8, §4.4)**: the firmware emits one
  32-byte reply per report; for an N-report message replies arrive as
  `[0,…,0,result]`. The multi-report test encodes this exact shape
  (`post_write = [[0…], [1…]]` for `batch_count = 2`), so it fails the moment
  capture stops honoring the last reply.
- **Anti-regression guard for the single-report path**: the capture-last rewrite
  uses `batch_count.max(1)` and overwrites each iteration. For `batch_count == 1`
  that is exactly one read + one capture — but the second test proves it, so a
  future "optimization" that breaks the `max(1)` guard or the overwrite order is
  caught immediately.
- **Additive and test-only**: compiled only under `#[cfg(test)]`; no runtime,
  public-API, dependency, or non-test-code change. Cannot affect `cargo build`.

## What

### Test 1 — `burst_to_one_captures_last_reply_for_multi_report` (Issue-1 gate)

Append at the END of `mod tests` (after `fakehid_drives_generic_burst_to_one`,
before the module's closing `}`). Verbatim:

```rust
    #[test]
    fn burst_to_one_captures_last_reply_for_multi_report() {
        // Issue-1 regression: a multi-report payload makes the firmware emit one
        // 32-byte reply per report (PRD §4.4). The first N-1 replies have
        // response[0] == 0 (the message is incomplete — no ETX seen, so the
        // legacy match-bool stays false); only the LAST reply — the ETX report —
        // carries the real result. burst_to_one must capture the LAST reply,
        // not the first (the v0.3.0 capture-first bug returned the intermediate 0).
        // Proven live: 2-report send -> firmware replies = [0, 1] (TEST_RESULTS.md).
        let payload = vec![0u8; 31]; // 31 bytes => 2 reports (PAYLOAD_PER_REPORT = 30)
        let batch_count = batches_for(&payload);
        assert_eq!(batch_count, 2, "31-byte payload must span exactly 2 reports");

        let fake = FakeHid::new();
        // Reply to report 1: intermediate, message incomplete (no ETX) => match
        // is false => the per-report ack is response[0] == 0.
        fake.push_post(vec![0u8; 33]);
        // Reply to report 2: the ETX report => the firmware computes the real
        // match-bool here => response[0] == 1 (the result we must return).
        fake.push_post({
            let mut r = vec![0u8; 33];
            r[0] = 1;
            r
        });

        let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
        assert!(success, "write path must succeed (FakeHid::write returns Ok)");
        let reply = reply.expect("must capture the ETX-report reply, not time out");
        assert_eq!(
            reply[0], 1,
            "must capture the ETX-report reply (1), not the intermediate [0] reply"
        );
        assert_eq!(
            fake.writes.borrow().len(),
            2,
            "must write exactly 2 reports for a 2-report payload"
        );
    }
```

### Test 2 — `burst_to_one_single_report_unchanged` (anti-regression guard)

```rust
    #[test]
    fn burst_to_one_single_report_unchanged() {
        // Single-report payloads were never affected by Issue 1 (1 report => 1
        // reply => the only reply IS the ETX reply). This guards against the
        // capture-last rewrite (P1.M1.T2.S2) regressing the batch_count == 1
        // path: the loop must still capture the one reply it reads.
        let payload = vec![0u8; 10]; // 10 bytes => 1 report
        let batch_count = batches_for(&payload);
        assert_eq!(batch_count, 1, "10-byte payload must fit in 1 report");

        let fake = FakeHid::new();
        // The one (ETX) reply: match-bool == 1.
        fake.push_post({
            let mut r = vec![0u8; 33];
            r[0] = 1;
            r
        });

        let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
        assert!(success, "write path must succeed");
        let reply = reply.expect("must capture the single reply");
        assert_eq!(
            reply[0], 1,
            "must capture the single (ETX) reply's match-bool"
        );
        assert_eq!(
            fake.writes.borrow().len(),
            1,
            "must write exactly 1 report for a single-report payload"
        );
    }
```

### Success Criteria

- [ ] Exactly two new `#[test]` fns exist at the END of `mod tests`:
      `burst_to_one_captures_last_reply_for_multi_report` and
      `burst_to_one_single_report_unchanged`.
- [ ] Test 1 uses a **31-byte payload**, asserts `batches_for(&payload) == 2`,
      seeds `FakeHid` with **two** `push_post` replies (`[0u8;33]` then
      `{[0]=1,…}`), and asserts `success` / `reply.is_some()` /
      **`reply[0] == 1`** (the ETX reply, not the intermediate `0`) /
      `writes.len() == 2`.
- [ ] Test 2 uses a **10-byte payload**, asserts `batches_for(&payload) == 1`,
      seeds `FakeHid` with **one** `push_post` reply (`{[0]=1,…}`), and asserts
      `success` / `reply.is_some()` / **`reply[0] == 1`** / `writes.len() == 1`.
- [ ] Both tests call `burst_to_one(&fake, &payload, batch_count, false)` — the
      `batch_count` passed is the value returned by `batches_for`, **not** a
      hardcoded literal (defense-in-depth + matches the design-doc test body).
- [ ] `cargo build` → zero warnings; `cargo clippy --all-targets` → zero new
      warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → 70 passed, 0
      failed; both new tests pass individually.
- [ ] `FakeHid`, `impl RawHid for FakeHid`, `impl FakeHid`, `burst_to_one`,
      `RawHid`, `batches_for`, and **every existing test** are unchanged. The
      only diff is the two appended `#[test]` fns.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim test bodies
> (ready to paste), the exact placement anchor (after
> `fakehid_drives_generic_burst_to_one` / before the `mod tests` closing `}` at
> core.rs:1489), the landed `FakeHid` API to use (`new()` + `push_post()`), the
> `batches_for` math (31 → 2, 10 → 1), the **critical** regression-gate contract
> (these tests are RED until S2 lands, GREEN after — proven by tracing the
> capture loop against both the old and new logic), the conventions to match
> (`use super::*;`, snake_case, multi-line `push_post` closure, assertion
> messages), and the verified build/clippy/fmt/test commands are all below. The
> implementer needs no keyboard and no firmware source dive.

### Documentation & References

```yaml
# MUST READ — the canonical test body this PRP implements (reconciled to landed API)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§'Test: Multi-Report Capture (Issue 1 regression)' gives the exact
        assertions (batch_count==2, post_write=[0-reply, 1-reply], reply[0]==1,
        writes.len()==2). The design doc sketches FakeHid via STRUCT LITERAL
        (FakeHid { pre_write_replies: ..., ... }); S1 LANDED helper methods
        instead (new()/push_pre()/push_post()). This PRP's verbatim test bodies
        use the LANDED helpers — do NOT copy the design doc's struct literal."
  section: "Test: Multi-Report Capture (Issue 1 regression)", "Edge Cases"
  critical: "Use batches_for(&payload) and assert it equals 2 (not a hardcoded
             batch_count). Seed the [0] reply FIRST then the [1] reply (queue
             order = firmware emit order). Assert reply[0]==1, NOT reply[0]==0.
             The test is a regression gate: it FAILS under capture-first, PASSES
             under capture-last (S2)."

# MUST READ — the file being edited (placement + conventions + the API under test)
- file: src/core.rs
  why: "(1) mod tests starts at line 751 with `use super::*; use crate::{...};` —
        `use super::*;` brings RawHid, burst_to_one, batches_for into scope (no
        extra import needed; existing fakehid_drives_generic_burst_to_one already
        calls batches_for this way). (2) FakeHid + helpers are at 773-823
        (new/push_pre/push_post). (3) The last existing test is
        fakehid_drives_generic_burst_to_one (1464-1487); mod tests closes at 1489.
        APPEND the two new tests between 1487 and 1489. (4) burst_to_one is at
        337; its capture block (370-385) is what these tests exercise — READ it
        to confirm whether S2 (capture-last loop) has landed (see Known Gotchas)."
  pattern: "Tests use descriptive snake_case; a top-of-body // comment explains
            intent; multi-line push_post closure via `let mut r = vec![0u8; 33];
            r[0] = 1; r` (matches S1 smoke tests at 1411-1416, 1466-1470);
            assert!(cond, \"msg\") and assert_eq!(actual, expected, \"msg\")."
  gotcha: "LOCATE the append anchor by content (`grep -n 'fn
           fakehid_drives_generic_burst_to_one' src/core.rs`), not by line number
           — S2's parallel landing will shift line 1489. Append AFTER that test's
           closing `}` and BEFORE the `mod tests` closing `}`."

# MUST READ — the FakeHid contract these tests consume (do NOT redefine)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S1/PRP.md
  why: "Defines FakeHid::new() (empty, written-latch false), push_pre(reply)
        (stale pre-write queue), push_post(reply) (firmware post-write queue),
        and the RawHid impl (write records + flips the post-queue latch;
        read_timeout pops the post-queue after the first write, empty => Ok(0)).
        S3 consumes FakeHid AS-IS; it must NOT modify the struct, impls, or
        helpers. S1's 3 smoke tests are capture-agnostic on purpose — S3 is
        where the byte-value assertion finally lands."
  section: "What (FakeHid struct + helpers)", "Known Gotchas (capture-agnostic)"
  critical: "Use push_post() to seed the firmware reply stream. Do NOT use struct
             literal construction. The written-latch is one-shot: after the first
             write() inside burst_to_one, ALL read_timeout calls pop the
             post_write queue — exactly what these tests rely on."

# MUST READ — the fix these tests gate (the capture-last loop under test)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S2/PRP.md
  why: "Defines the capture-last rewrite (single match -> `for _ in
        0..batch_count.max(1) { match read_timeout { Ok(n>0) => reply =
        Some(...); _ => break } }`, keeping the LAST reply). This is the logic
        Test 1 asserts correct. Treat S2's PRP as a CONTRACT — assume its loop
        is landed. If S2 is NOT yet landed in the working tree, these two tests
        will FAIL (Test 1: reply[0]==0, not 1) — that is correct regression-gate
        behavior; do NOT 'fix' the tests to pass against capture-first."
  section: "What (the capture-logic rewrite)", "Success Criteria"
  critical: "These tests are RED under capture-first, GREEN under capture-last.
             If `grep -n 'Capture the FIRST device reply' src/core.rs` still
             matches, S2 is absent and the tests are expected to fail — surface
             this to the orchestrator rather than weakening the assertions."

# REFERENCE — the live proof this test encodes (firmware emits [0,1] for 2 reports)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 1 'Steps to Reproduce' step 4 documents the direct-hidapi proof:
        1-report => replies [1]; 2-report => [0,1]; 4-report => [0,0,0,1]. Test 1
        seeds exactly the 2-report shape ([0,1]); the real result is the 2nd
        reply. Confirms the boundary: any payload > PAYLOAD_PER_REPORT (30) is
        affected (31-byte payload => 2 reports => the bug)."
  section: "Critical Issues / Issue 1 (Steps to Reproduce, Boundary)"

# REFERENCE — PRD invariants these tests uphold
- file: PRD.md
  why: "§4.4 (one 32-byte reply per report — the model FakeHid emulates), §8
        (response[0] is the legacy match-bool, computed only at ETX — the byte
        Test 1 and Test 2 assert), §14 invariant 6 (reply disambiguation
        0x51/0/1) and 8 (firmware wins)."
  section: "4.4 Replies are received", "8 Response Handling", "14 Key Invariants (6, 8)"

# REFERENCE — research notes (baseline, API reconciliation, regression-gate trace)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S3/research/notes.md
  why: "Documents the working-tree reality (S1 landed, S2 parallel/not-yet),
        the FakeHid API reconciliation (helpers, not struct literal), the
        capture-loop trace proving RED-before-S2/GREEN-after-S2 for both tests,
        the conventions to match, and the verified commands."
```

### Current Codebase tree (verified this session)

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.3.0"; deps: clap, hidapi "2.4.1" (NO [dev-dependencies])
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore
└── src
    ├── main.rs         # binary entrypoint — DO NOT TOUCH
    ├── lib.rs          # public API + tests — DO NOT TOUCH
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # <-- FILE TO EDIT (append-only to mod tests):
                         #     RawHid trait+impl (13-31) — CONSUMED
                         #     burst_to_one<T: RawHid> (337) — CONSUMED (under test)
                         #     batches_for (412-414) — CONSUMED
                         #     #[cfg(test)] mod tests (751-1489):
                         #       FakeHid + helpers (773-823) — CONSUMED
                         #       3 fakehid_* smoke tests (1408-1487) — UNTOUCHED
                         #       >>> APPEND 2 new #[test] fns HERE (1487-1489 gap) <<<
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── core.rs   # MODIFIED ONLY — 2 new #[test] fns appended at the END of mod tests:
              #     - burst_to_one_captures_last_reply_for_multi_report
              #     - burst_to_one_single_report_unchanged
              #   No new files, no new deps, no non-test-code change, no FakeHid change.
```

> No new files. The only diff is two `#[test]` fns inserted into the existing
> `#[cfg(test)] mod tests` block, immediately before its closing `}`.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL — REGRESSION-GATE SEMANTICS. These two tests are RED under the
//   capture-FIRST logic (current tree if S2 hasn't landed) and GREEN under the
//   capture-LAST logic (S2). Trace for Test 1 with post_write=[[0…],[1…]], batch_count=2:
//     capture-FIRST (single match): reads reply0=[0…] -> reply[0]==0; reply1=[1…]
//                                   drained & discarded. assert_eq!(reply[0],1) FAILS.
//     capture-LAST  (S2 loop):      iter0 reads [0…] (overwrite); iter1 reads [1…]
//                                   (overwrite => last wins). assert_eq!(reply[0],1) PASSES.
//   This is BY DESIGN — they are the gate that proves S2 works. If you see them
//   fail in a tree where `grep -n 'Capture the FIRST device reply' src/core.rs`
//   still matches, S2 is absent: report it, do NOT weaken the assertions to pass.

// CRITICAL — USE THE LANDED FakeHid HELPERS, not struct-literal construction.
//   `FakeHid { pre_write_replies: ..., ... }` (as sketched in reply_capture_design.md)
//   is STALE — S1 landed `FakeHid::new()` + `push_pre()` + `push_post()` instead.
//   Build the double as:
//       let fake = FakeHid::new();
//       fake.push_post(vec![0u8; 33]);            // reply to report 1 (intermediate)
//       fake.push_post({ let mut r = vec![0u8;33]; r[0]=1; r }); // reply to report 2 (ETX)
//   This matches the existing fakehid_drives_generic_burst_to_one smoke test style.

// CRITICAL — QUEUE ORDER = FIRMWARE EMIT ORDER. push_post() appends to the BACK
//   of the post_write queue; read_timeout pops the FRONT. So push the
//   intermediate [0] reply FIRST and the ETX [1] reply SECOND. Reversing them
//   would make even capture-last return the wrong byte (and the test would pass
//   for the wrong reason / fail confusingly).

// CRITICAL — DO NOT pass a hardcoded batch_count. Compute it via batches_for:
//       let batch_count = batches_for(&payload);
//       assert_eq!(batch_count, 2);   // (or 1 for Test 2)
//   batches_for(31 bytes) = (31+32-3)/30 = 60/30 = 2. batches_for(10 bytes) =
//   (10+29)/30 = 39/30 = 1. Asserting the value ties the test to the real
//   framing math (PAYLOAD_PER_REPORT=30), not a magic number.

// CRITICAL — APPEND-ONLY. Insert the two tests AFTER fakehid_drives_generic_burst_to_one's
//   closing `}` and BEFORE the `mod tests` closing `}`. Do NOT reorder, edit, or
//   delete any existing test. The S1 smoke tests are capture-agnostic and MUST
//   stay untouched (they are S2's regression safety net; S3's byte assertions
//   are additive, not a replacement).

// NOTE — no extra imports. `use super::*;` (already at mod-tests top, line 752)
//   brings RawHid, burst_to_one, batches_for into scope. The std::cell /
//   std::collections imports FakeHid needs are already present (S1 added them).
//   Do NOT add any new `use` statement.

// NOTE — the `let mut r = vec![0u8; 33]; r[0] = 1; r` closure pattern is the
//   file's established idiom for "a 33-byte reply whose first byte is 1"
//   (S1 smoke tests use it at 1411-1416 and 1466-1470). Reuse it verbatim for
//   consistency and to keep clippy/rustfmt happy.

// NOTE — FakeHid is single-use per test (the written-latch is one-shot, never
//   reset). Each test creates its OWN `FakeHid::new()`. Do not share a double
//   across tests or reuse one after a burst_to_one call.

// NOTE — `cargo test --lib` final count is 70 (68 baseline + 2 new). If you see
//   68, the two tests were not compiled in (check they are inside mod tests and
//   each has `#[test]`). If you see 70 but 2 FAILED, S2 (capture-last) is not
//   landed — see the first gotcha. The expected GREEN state requires S1 AND S2
//   AND S3 all present.

// NOTE — no rustfmt.toml / clippy.toml — default configs. Run `cargo fmt` after
//   pasting (rustfmt will reflow the assert_eq! macro calls to match the file's
//   multi-line style; that's expected and fine).
```

## Implementation Blueprint

### Data models and structure

No new types, no new helpers, no new imports. The only "structure" is two
`#[test]` functions that orchestrate the **already-landed** `FakeHid` double and
the **already-generic** `burst_to_one`. Each test is self-contained: build a
payload → compute `batch_count` → seed a `FakeHid` reply stream → call
`burst_to_one` → assert on the `(bool, Option<Vec<u8>>)` tuple.

```rust
// Data flowing through each test:
//   payload: Vec<u8>            -> 31 bytes (Test 1) / 10 bytes (Test 2)
//   batch_count: usize          -> batches_for(&payload) == 2 (Test 1) / 1 (Test 2)
//   FakeHid post_write queue    -> [0-reply, 1-reply] (Test 1) / [1-reply] (Test 2)
//   (success, reply)            -> (true, Some(vec where [0]==1))
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/core.rs and confirm the inputs + anchors
  - CONFIRM (grep -n "struct FakeHid" src/core.rs) FakeHid + helpers exist
          (S1 LANDED). If ABSENT, STOP: prerequisite missing (S1 not landed).
  - CONFIRM the capture logic state: `grep -n "Capture the FIRST device reply\|
          for _ in 0..batch_count.max(1)" src/core.rs`. If the FIRST-match is
          present (S2 not yet landed), NOTE IT: the two tests are expected to
          FAIL until S2 lands — proceed to write them anyway (they are the gate).
          If the batch_count.max(1) loop is present (S2 landed), the tests should
          PASS once added.
  - CONFIRM batches_for math: read core.rs:412-414. (data.len() + REPORT_LENGTH
          - 3) / PAYLOAD_PER_REPORT. For 31 bytes => 2; for 10 bytes => 1.
  - LOCATE the append anchor: `grep -n "fn fakehid_drives_generic_burst_to_one"
          src/core.rs` -> the LAST existing test. Read through its closing `}`
          and the `mod tests` closing `}` immediately after.
  - GOAL: know the exact insertion point and confirm the prerequisites.

Task 2: APPEND Test 1 — burst_to_one_captures_last_reply_for_multi_report
  - INSERT (between fakehid_drives_generic_burst_to_one's closing `}` and the
          mod tests closing `}`): the verbatim Test 1 body from the "What"
          section.
  - KEY ASSERTIONS: batches_for(&31-byte)==2; success==true; reply.is_some();
          reply[0]==1 (ETX reply, NOT intermediate 0); writes.len()==2.
  - CONVENTIONS: top-of-body // comment; FakeHid::new() + push_post() (NOT
          struct literal); push the [0] reply FIRST then the [1] reply;
          multi-line `let mut r = vec![0u8;33]; r[0]=1; r` closure.
  - DO NOT: hardcode batch_count (use batches_for and assert_eq!); reorder the
            push_post calls; touch any existing test or FakeHid.

Task 3: APPEND Test 2 — burst_to_one_single_report_unchanged
  - INSERT (immediately after Test 1, still before the mod tests closing `}`):
          the verbatim Test 2 body from the "What" section.
  - KEY ASSERTIONS: batches_for(&10-byte)==1; success==true; reply.is_some();
          reply[0]==1; writes.len()==1.
  - PURPOSE: anti-regression guard — proves the capture-last rewrite keeps the
          batch_count==1 path correct (the pre-S2 behavior that was always right).
  - DO NOT: add a 3rd test (typed-reply, N=4, etc.) — the contract is exactly
            two tests.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --all-targets`,
          then `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT (S1+S2+S3 all landed): build 0 warnings; clippy 0 new warnings;
          fmt --check exit 0; test result "70 passed; 0 failed".
  - RUN both new tests in isolation:
          `cargo test --lib burst_to_one_captures_last_reply_for_multi_report -- --nocapture`
          `cargo test --lib burst_to_one_single_report_unchanged -- --nocapture`
  - IF Test 1 FAILS with `assert_eq!(reply[0], 1)` showing `left: 0`: S2
          (capture-last) is NOT landed in this tree. This is the expected
          regression-gate failure — surface it to the orchestrator; do NOT change
          the assertion to `== 0`. Verify: `grep -n "Capture the FIRST device
          reply" src/core.rs` should still match.
  - IF "cannot find function `batches_for`/`burst_to_one`/type `FakeHid` in
          this scope": the test is outside `mod tests` or `use super::*;` was
          disturbed. Re-check placement (must be inside mod tests, before its
          closing `}`).
  - IF "the trait bound `FakeHid: RawHid` is not satisfied": impossible if S1 is
          intact — do NOT touch FakeHid/impl RawHid; re-verify S1 is landed.
```

### Implementation Patterns & Key Details

```rust
// === THE LANDED FakeHid API TO USE (NOT the design doc's struct literal) ===
//
//   let fake = FakeHid::new();
//   fake.push_post(vec![0u8; 33]);                         // reply to report 1
//   fake.push_post({ let mut r = vec![0u8; 33]; r[0] = 1; r }); // reply to report 2
//
//   // then drive the generic burst_to_one:
//   let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
//
//   Why helpers over struct literal: S1 landed new()/push_pre()/push_post() as
//   the canonical API (the struct-literal form in reply_capture_design.md is a
//   pre-S1 sketch). The existing fakehid_drives_generic_burst_to_one smoke test
//   uses exactly this pattern — match it for consistency.


// === THE REGRESSION-GATE TRACE (why these tests are RED before S2, GREEN after) ===
//
//   Test 1 seed: post_write = [[0u8;33], {[0]=1,…}], batch_count = 2.
//
//   capture-FIRST (v0.3.0, current tree if S2 absent):
//     single match read_timeout -> pops reply0 = [0u8;33] -> reply = Some([0…])
//     surplus drain loop -> pops reply1 = [1…] -> DISCARDED (n>0, continue)
//     => reply[0] == 0  =>  assert_eq!(reply[0], 1) FAILS  (RED)
//
//   capture-LAST (S2 fix):
//     for _ in 0..2:
//       iter0: read_timeout -> [0u8;33] -> reply = Some([0…])   (overwrite)
//       iter1: read_timeout -> [1…]     -> reply = Some([1…])   (overwrite, last wins)
//     surplus drain loop -> queue empty -> Ok(0) -> break (nothing to discard)
//     => reply[0] == 1  =>  assert_eq!(reply[0], 1) PASSES  (GREEN)
//
//   This RED->GREEN flip on S2 landing is the entire point of S3. The test is
//   correct as written; a failure means the FIX is missing, not the test.


// === WHY batches_for (not a hardcoded batch_count) ===
//   Asserting batches_for(&[0u8;31]) == 2 ties the test to the real framing
//   constant PAYLOAD_PER_REPORT (30). If someone changes the report size
//   constant, the test's payload-size rationale stays legible and the
//   assert_eq! documents the expected report count at the assertion site.


// === WHY NO 3rd TEST ===
//   The contract (item description, OUTPUT clause) specifies exactly TWO tests:
//   multi-report capture + single-report-unchanged. Typed-reply (0x51) multi-
//   report capture and N>=3 report counts are out of scope — they would follow
//   the same FakeHid pattern but are not requested. The Issue-3 stale-reply
//   regression test is a separate subtask (P1.M1.T3.S2).
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY (append 2 #[test] fns inside #[cfg(test)] mod tests)"
  - add:    "fn burst_to_one_captures_last_reply_for_multi_report"
  - add:    "fn burst_to_one_single_report_unchanged"

NO OTHER CHANGES:
  - imports: "none — `use super::*;` (line 752) + S1's std::cell/std::collections
              imports already cover everything."
  - deps:    "none — NO [dev-dependencies]; FakeHid is hand-rolled std only."
  - types:   "none — FakeHid, RawHid, burst_to_one, batches_for all consumed as-is."

CONSUMED (do NOT modify — treat as contracts):
  - P1.M1.T1.S1: "pub(crate) trait RawHid + impl for hidapi::HidDevice (core.rs:13-31)."
  - P1.M1.T1.S2: "fn burst_to_one<T: RawHid>(&T,&[u8],usize,bool) -> (bool,Option<Vec<u8>>)
                  (core.rs:337)."
  - P1.M1.T2.S1: "struct FakeHid + impl RawHid + impl FakeHid{new,push_pre,push_post}
                  (core.rs:773-823)."
  - P1.M1.T2.S2: "the capture-last loop `for _ in 0..batch_count.max(1) { ... overwrite
                  reply ... }` inside burst_to_one — THE LOGIC UNDER TEST. Assumed landed
                  (treat S2 PRP as contract). If absent, these tests are the RED gate."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified, and ONLY by appending 2 #[test] fns. Do NOT
    touch FakeHid/burst_to_one/RawHid/batches_for/any existing test/lib.rs/
    error.rs/main.rs/Cargo.toml. Do NOT add the Issue-3 stale-reply test
    (P1.M1.T3.S2) or any typed-reply/N-report test.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
# (The new tests are #[cfg(test)] ⇒ not compiled here; this confirms no collateral.)
cargo build 2>&1 | tee /tmp/s3_build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.

# Lint ALL targets — this IS where the new test code gets linted.
cargo clippy --all-targets 2>&1 | tee /tmp/s3_clippy.log
# Expected: no warnings/errors for the new tests.
# If "unused variable": check for a stray binding; the test bodies have none.

# Formatting gate.
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the 2 new tests in isolation first (with full output).
cargo test --lib burst_to_one_captures_last_reply_for_multi_report -- --nocapture
cargo test --lib burst_to_one_single_report_unchanged -- --nocapture
# Expected (S1+S2+S3 landed): 2 passed.
# Expected (S2 NOT landed): Test 1 FAILS at `assert_eq!(reply[0], 1)` with
#   `left: 0` — this is the regression-gate failure; surface to orchestrator.
#   Test 2 still passes (single-report capture is correct under both logics).

# Full lib test suite (core.rs #[cfg(test)] mod tests + lib.rs unit tests).
cargo test --lib
# Expected (all landed): "test result: ok. 70 passed; 0 failed; 0 ignored; ..."
#   (68 baseline from S1+S2 + 2 new).

# Sanity: confirm the pre-existing tests (incl. S1's 3 smoke tests) STILL pass.
cargo test --lib fakehid_ -- --nocapture     # 3 passed (capture-agnostic, untouched)
cargo test --lib batches_for_ -- --nocapture # pre-existing pure-fn tests untouched
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
The two new tests ARE the in-CI verification of the capture-last fix — there is
no service to start, no endpoint to curl, no DB, no HID device. The Level 2
`cargo test --lib` (with the 2 new tests green) IS the integration validation.

LIVE HARDWARE (optional, NOT required for the CI gate):
  The fix these tests lock is observable end-to-end on a QMK keyboard with
  qmk-notifier firmware. If one is connected, reproduce TEST_RESULTS.md:
    cargo build --release
    ./target/release/qmk_notifier "hello"                          # 1 report  -> Legacy{matched:true}
    ./target/release/qmk_notifier "hello.............................."  # 2 reports
        # AFTER S2+S3: Legacy{matched:true} (captured the ETX [1] reply)
  No keyboard? The 2 FakeHid-seeded unit tests are the CI-proof equivalent.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm both new tests exist and are inside mod tests:
grep -nE "fn burst_to_one_captures_last_reply_for_multi_report|fn burst_to_one_single_report_unchanged" src/core.rs
# Expected: exactly one hit each, both at indent depth 4 (inside mod tests).

# Confirm the byte-value assertion is present (the thing S1's smoke tests omitted):
grep -nE "reply\[0\], 1" src/core.rs
# Expected: 2 hits (one per new test) — this is the capture-last proof.

# Confirm no struct-literal FakeHid construction leaked in (must use helpers):
grep -nE "FakeHid \{ pre_write_replies" src/core.rs || echo "good: uses FakeHid::new() + push_post()"
# Expected: the "good" message (no struct-literal construction).

# Confirm the capture-last loop (S2) is what the tests run against:
grep -n "for _ in 0..batch_count.max(1)" src/core.rs
# Expected: one hit inside burst_to_one. If ABSENT, S2 is not landed and Test 1
# is expected to fail (the RED gate) — see Known Gotchas.

# Confirm the S1 smoke tests are untouched (still capture-agnostic):
grep -cE "fn fakehid_" src/core.rs
# Expected: 3 (unchanged by S3).

# Confirm the test count:
cargo test --lib 2>&1 | grep "test result"
# Expected: "70 passed; 0 failed" (S1+S2+S3 all landed).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings.
- [ ] Level 1 passed: `cargo clippy --all-targets` → zero new warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → **70 passed, 0 failed** (S1+S2+S3 landed).
- [ ] Level 2 passed: both new tests pass individually (`--nocapture`).

### Feature Validation

- [ ] `burst_to_one_captures_last_reply_for_multi_report` exists, uses a 31-byte
      payload, asserts `batches_for == 2`, seeds `post_write = [[0…],[1…]]`, and
      asserts `success` / `reply.is_some()` / **`reply[0] == 1`** / `writes.len() == 2`.
- [ ] `burst_to_one_single_report_unchanged` exists, uses a 10-byte payload,
      asserts `batches_for == 1`, seeds `post_write = [[1…]]`, and asserts
      `success` / `reply.is_some()` / **`reply[0] == 1`** / `writes.len() == 1`.
- [ ] Both tests use `FakeHid::new()` + `push_post()` (the landed API, NOT
      struct-literal construction).
- [ ] `batch_count` is computed via `batches_for(&payload)` and asserted (not
      hardcoded).
- [ ] If S2 is absent, Test 1 fails at `reply[0]` with `left: 0` (the correct
      regression-gate RED state) — surfaced, not silenced.

### Code Quality Validation

- [ ] Tests appended at the END of `mod tests` (after `fakehid_drives_generic_burst_to_one`).
- [ ] Follows existing `mod tests` style (`use super::*;`, descriptive snake_case,
      `//` intent comment, multi-line `push_post` closure, `assert!`/`assert_eq!` with messages).
- [ ] No new imports, no new deps, no new types, no `#[dev-dependencies]`.
- [ ] `FakeHid` / `impl RawHid` / `impl FakeHid` / `burst_to_one` / `RawHid` /
      `batches_for` / all existing tests are UNCHANGED.
- [ ] Anchors located by content grep (not stale line numbers — S2's parallel
      landing shifts line 1489).

### Documentation & Deployment

- [ ] Each test has a top-of-body `//` comment explaining the firmware model
      (one reply per report, ETX carries the result) and which Issue it gates.
- [ ] No external doc files changed (test-only code, no public surface).
- [ ] No Cargo.toml / env / config / README change.

---

## Anti-Patterns to Avoid

- ❌ Don't weaken the assertions to make them pass against capture-FIRST. These
  tests are a **regression gate**: they are RED until S2 (capture-last) lands,
  GREEN after. If Test 1 fails with `reply[0] == 0`, that means S2 is absent —
  surface it; do NOT change `assert_eq!(reply[0], 1)` to `== 0` or remove it.
- ❌ Don't use struct-literal `FakeHid { pre_write_replies: ..., ... }`
  construction. S1 landed `FakeHid::new()` + `push_post()` helpers — use them.
  The struct-literal form in `reply_capture_design.md` is a pre-S1 sketch and is
  STALE. (It would also couple the test to private field names and bypass the
  one-shot written-latch initialization `new()` guarantees.)
- ❌ Don't reverse the `push_post` order. The intermediate `[0]` reply must be
  pushed FIRST and the ETX `[1]` reply SECOND — queue order = firmware emit
  order = pop order. Reversing breaks the test's fidelity to the real device.
- ❌ Don't hardcode `batch_count`. Compute `batches_for(&payload)` and
  `assert_eq!` it — this ties the test to `PAYLOAD_PER_REPORT=30` and documents
  the expected report count at the assertion.
- ❌ Don't touch `FakeHid`, `impl RawHid for FakeHid`, `impl FakeHid`,
  `burst_to_one`, `RawHid`, `batches_for`, or any existing test. This subtask is
  **append-only** to `mod tests`. The S1 smoke tests are capture-agnostic by
  design — S3's byte assertions are additive, not a replacement.
- ❌ Don't add a 3rd test (typed `0x51` reply, N≥3 reports, no-reply timeout).
  The contract (OUTPUT clause) is exactly TWO tests. The Issue-3 stale-reply
  regression test is a separate subtask (P1.M1.T3.S2).
- ❌ Don't add `[dev-dependencies]` or a mocking crate. `FakeHid` is hand-rolled
  with `std::cell` + `std::collections::VecDeque`; keep it that way.
- ❌ Don't add new `use` statements. `use super::*;` (line 752) already brings
  `RawHid`, `burst_to_one`, `batches_for` into scope, and S1 added the
  `std::cell`/`std::collections` imports `FakeHid` needs.
- ❌ Don't trust line number 1489 for the append anchor — S2's parallel landing
  shifts it. Locate via `grep -n "fn fakehid_drives_generic_burst_to_one" src/core.rs`
  and append after that test's closing `}`, before `mod tests`'s closing `}`.
- ❌ Don't proceed if `struct FakeHid` is absent (`grep -n "struct FakeHid"
  src/core.rs`) — S1 is a hard prerequisite. If absent, STOP and surface the
  missing prerequisite; these tests cannot compile without the landed double.
- ❌ Don't skip `cargo clippy --all-targets` (vs just `cargo clippy`) — the
  `--all-targets` form is what lints the `#[cfg(test)]` code where the new tests
  live.
- ❌ Don't assert on `reply.len()` or other incidental shapes — assert only the
  contract-relevant facts: `success`, `reply.is_some()`, `reply[0]`, and
  `writes.len()`. Asserting `reply.len() == 33` would couple to FakeHid's buffer
  size rather than the firmware's reply semantics.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is two ready-to-paste `#[test]` functions (verbatim bodies provided) appended to
a known location (after `fakehid_drives_generic_burst_to_one`, before the
`mod tests` closing `}`), consuming an already-landed API (`FakeHid::new()` +
`push_post()`), asserting on values derived from documented framing math
(`batches_for(31)==2`, `batches_for(10)==1`). The one subtlety — that these tests
are a RED-then-GREEN regression gate for S2 (capture-last) — is fully
characterized with the capture-loop trace proving `reply[0]` flips `0→1` when S2
lands, so an implementer who sees a failure against a capture-first tree will
recognize it as the expected gate rather than a bug in the test. The conventions
to match, the placement anchor (by content grep), the verified build/clippy/fmt/
test commands, and the explicit "do not weaken the assertion" guardrails make
this unambiguous. Inputs (`RawHid`, generic `burst_to_one`, `FakeHid`, S1's
capture-agnostic smoke tests) are all landed and verified in the working tree.