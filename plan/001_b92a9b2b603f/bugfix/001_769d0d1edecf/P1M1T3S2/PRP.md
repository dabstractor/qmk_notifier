# PRP — P1.M1.T3.S2: Add Stale-Reply Regression Test

---

## Goal

**Feature Goal**: Add **two new unit tests** to the `#[cfg(test)] mod tests` block
in **`src/core.rs`** that lock in the **Issue-3 pre-send IN-buffer drain**
(landed in `P1.M1.T3.S1`): (1) `burst_to_one_drains_stale_replies_before_send` —
proves a stale reply queued in `FakeHid::pre_write_replies` is **consumed by the
pre-send drain** (and the *fresh* `post_write_replies` reply is the one captured),
and (2) `burst_to_one_empty_in_buffer_is_clean` — proves the drain is a **no-op**
in the common no-stale-data case (normal capture is undisturbed). These are the
regression tests that the `P1.M1.T3.S1` PRP explicitly deferred ("the stale-reply
test is `P1.M1.T3.S2`").

**Deliverable**: Two `#[test]` functions appended to `src/core.rs` `mod tests`
(after the existing `burst_to_one_single_report_unchanged` test, which is currently
the last test in the module). **Production code is untouched** — `burst_to_one`,
the drain, `FakeHid`, `impl RawHid`, and the capture loop are all consumed as-is
from prior subtasks. No new files, no new deps, no new types, no new constants,
no `Cargo.toml` change.

**Success Definition**: `cargo test --lib` → **72 passed; 0 failed** (70 baseline
+ 2 new); `cargo clippy --all-targets` → zero new warnings; `cargo fmt --check`
→ exit 0. The two new tests pass, AND removing the S1 drain block from
`burst_to_one` makes `burst_to_one_drains_stale_replies_before_send` **FAIL**
(that is what makes it a genuine regression test — see ⚠️ CRITICAL below). No
file other than `src/core.rs` is modified.

## User Persona (if applicable)

**Target User**: The maintainer of the `qmk_notifier` crate and its downstream
consumer `qmkonnect` — anyone who relies on a *deterministic* per-notification
`CommandResponse`.

**Use Case**: A future refactor (e.g. "optimizing" the drain, reordering the
write/drain/capture steps, or switching `read_timeout(0)` to a blocking read) must
be **caught by CI** before it can reintroduce the Issue-3 nondeterminism
(`0 1 0 0 1 1 1 1 1 1` for a fixed input). These two tests are that CI gate.

**User Journey**: `cargo test --lib` runs in CI → both new tests exercise the
`burst_to_one` ↔ `FakeHid` seam → the drain's contract (drain stale / leave clean)
is asserted → green build proves Issue 3 stays fixed.

**Pain Points Addressed**: Without `burst_to_one_drains_stale_replies_before_send`,
a regression that removes or mis-places the pre-send drain is **invisible** to the
existing test suite (every existing FakeHid test seeds an *empty* `pre_write_replies`,
so the drain is a no-op for all 70 of them — the S1 PRP proved this). This test
seeds a non-empty pre-queue for the **first time**, closing that coverage gap.

## Why

- **Issue 3 is a Major bug** (PRD §h2.2/§h3.2): nondeterministic `CommandResponse`
  across rapid sequential sends because the IN buffer was drained only *after*
  capture, never *before* the write. The S1 fix added the pre-send drain; **this
  subtask adds the regression test that proves it works** and guards it forever.
- **Closes a real coverage gap.** The S1 PRP's "No-op proof" shows the drain is
  invisible to all 70 existing tests (they seed only `post_write_replies`). Until
  a test seeds `pre_write_replies`, **no test can fail if the drain is deleted**.
  This subtask is what makes the Issue-3 fix actually *defended* by CI.
- **Symmetric contract framing.** Two tests pin both halves of the drain's
  contract: *stale data is drained* (test 1) and *no stale data is a clean no-op*
  (test 2). A future change that breaks either half fails a test.
- **Test-only code, zero blast radius.** Lives entirely under `#[cfg(test)]`;
  ships nothing to `cargo build --release`. No production code, no API, no deps.

## What

### Test 1 — `burst_to_one_drains_stale_replies_before_send` (the regression test)

Append to `mod tests` in `src/core.rs`. Mirrors the style of the existing sibling
`burst_to_one_captures_last_reply_for_multi_report` (core.rs ~1526). Sketch:

```rust
#[test]
fn burst_to_one_drains_stale_replies_before_send() {
    // Issue-3 regression: a stale reply left in the IN buffer by a PRIOR send
    // must be drained BEFORE this send's write(), so the capture loop reads only
    // THIS send's fresh reply. FakeHid models stale IN-buffer data with its
    // pre_write_replies queue (served by read_timeout while the `written` latch
    // is false, i.e. before any write()). The pre-send drain pops that queue; if
    // it did not, the stale reply would linger. (See architecture/reply_capture_
    // design.md §Test: Pre-Send Drain.)
    let fake = FakeHid::new();
    // Stale IN-buffer reply from a previous send (all-zero report: [0]==0).
    fake.push_pre(vec![0u8; 33]);
    // The FRESH reply to THIS send's single report: match-bool == 1.
    fake.push_post({
        let mut r = vec![0u8; 33];
        r[0] = 1;
        r
    });

    let payload = vec![0x41u8; 10]; // 10 bytes => 1 report (PAYLOAD_PER_REPORT = 30)
    let batch_count = batches_for(&payload);
    assert_eq!(batch_count, 1, "10-byte payload must fit in 1 report");

    let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
    assert!(success, "write path must succeed (FakeHid::write returns Ok)");
    let reply = reply.expect("must capture the fresh reply, not time out");

    // (a) Contract assertion (item clause 3): the captured reply is the FRESH
    //     one, whose [0]==1 — NOT the stale [0].
    assert_eq!(
        reply[0], 1,
        "must capture the fresh reply ([0]==1), not the stale reply ([0]==0)"
    );

    // (b) DECISIVE assertion: the pre-send drain must have CONSUMED the stale
    //     reply before write() flipped the read queue to post_write_replies.
    //     See the ⚠️ CRITICAL note below for why (a) alone is a false-passing
    //     test: FakeHid switches read queues on write(), so without the drain
    //     the stale reply would simply sit unread in pre_write_replies and
    //     reply[0] would STILL be 1. This is_empty() check is what makes the
    //     test fail if the drain block is removed — i.e. a genuine regression
    //     test.
    assert!(
        fake.pre_write_replies.borrow().is_empty(),
        "the pre-send drain must consume the stale reply before write() flips the read queue"
    );
    assert_eq!(
        fake.writes.borrow().len(),
        1,
        "must write exactly 1 report for a single-report payload"
    );
}
```

### Test 2 — `burst_to_one_empty_in_buffer_is_clean` (the clean-path baseline twin)

```rust
#[test]
fn burst_to_one_empty_in_buffer_is_clean() {
    // Issue-3 clean-path baseline: when there is NO stale IN-buffer data, the
    // pre-send drain is a no-op (reads Ok(0), breaks after one iteration) and
    // normal reply capture is undisturbed. This is the common case on the hot
    // path (a quiet device has nothing to drain), so the drain must not stall,
    // consume the fresh reply, or otherwise perturb capture. Pairs with
    // burst_to_one_drains_stale_replies_before_send to cover both halves of the
    // drain's contract.
    let fake = FakeHid::new();
    // pre_write_replies deliberately LEFT EMPTY (no stale data).
    fake.push_post({
        let mut r = vec![0u8; 33];
        r[0] = 1;
        r
    }); // the fresh (only) reply

    let payload = vec![0x41u8; 10]; // 1 report
    let batch_count = batches_for(&payload);
    assert_eq!(batch_count, 1);

    let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
    assert!(success, "write path must succeed");
    let reply = reply.expect("must capture the single fresh reply");
    assert_eq!(
        reply[0], 1,
        "the fresh reply must be captured intact (drain must not steal it)"
    );
    assert_eq!(
        fake.writes.borrow().len(),
        1,
        "must write exactly 1 report"
    );
}
```

### Success Criteria

- [ ] Two new `#[test]` fns exist in `src/core.rs` `mod tests`:
      `burst_to_one_drains_stale_replies_before_send` and
      `burst_to_one_empty_in_buffer_is_clean`, appended after
      `burst_to_one_single_report_unchanged` (the current last test).
- [ ] Test 1 seeds exactly one `push_pre(vec![0u8; 33])` (stale) + one
      `push_post({r[0]=1})` (fresh), uses `payload = vec![0x41u8; 10]`,
      `batch_count = batches_for(&payload)` (asserted `== 1`), and asserts
      `reply[0] == 1` **AND** `fake.pre_write_replies.borrow().is_empty()`
      **AND** `writes.len() == 1`.
- [ ] Test 2 seeds an EMPTY pre-queue + one `push_post({r[0]=1})`, same payload,
      and asserts `reply[0] == 1` and `writes.len() == 1`.
- [ ] `cargo test --lib` → **72 passed; 0 failed**.
- [ ] `cargo clippy --all-targets` → zero new warnings; `cargo fmt --check` → exit 0.
- [ ] **Genuine-regression proof**: temporarily deleting the S1 drain block
      (`let mut stale_buf = ...` loop in `burst_to_one`) makes test 1 **FAIL** on
      the `pre_write_replies.is_empty()` assertion (restore the drain before
      committing). If it still passes with the drain deleted, the test is wrong.
- [ ] No file other than `src/core.rs` is modified; no production code touched.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** Both tests are given as
> ready-to-paste sketches above, the exact insertion point (after the last test
> in `mod tests`, currently `burst_to_one_single_report_unchanged`) is specified,
> the FakeHid helpers (`new`/`push_pre`/`push_post`) and their queue-switching
> semantics are documented, the sibling tests to mirror are cited by name+line,
> the ⚠️ CRITICAL reason the `is_empty()` assertion is *required* (not optional)
> is fully explained, the batch_count convention (`batches_for`, asserted `==1`)
> is shown, and the verified validation commands + exact post-count (72) are
> below. The implementer needs no keyboard.

### Documentation & References

```yaml
# MUST READ — the canonical design (§Test: Pre-Send Drain is the test sketch;
# §FakeHid Test Double documents the two-queue model this test relies on).
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§Test: Pre-Send Drain (Issue 3 regression) gives the ORIGINAL test sketch
        (seed pre=[stale], post=[fresh], assert reply[0]==1). §FakeHid Test Double
        documents the pre_write_replies queue + the `written` latch that switches
        read_timeout to post_write_replies after the first write(). §Problem
        Statement explains WHY (firmware sends one reply per report; prior replies
        linger in the IN buffer)."
  section: "Test: Pre-Send Drain (Issue 3 regression)", "FakeHid Test Double",
           "Problem Statement"
  critical: "The design doc's sketch uses the STRUCT-LITERAL FakeHid form
             (`FakeHid { pre_write_replies: RefCell::new(...), ... }`). That form
             was REPLACED in P1.M1.T2.S1 by the `FakeHid::new()` + `push_pre()` /
             `push_post()` HELPER API. DO NOT copy the struct-literal form — it
             will not compile (fields are private to the struct, and the helper
             API is the established convention). Use `let fake = FakeHid::new();
             fake.push_pre(...); fake.push_post(...);` as the sibling tests do."

# MUST READ — the file being edited (the test module + the siblings to mirror +
# the drain/capture code under test + the FakeHid contract).
- file: src/core.rs
  why: "(1) INSERTION POINT: append both tests inside `mod tests` (opens at the
        `mod tests {` line ~787), immediately AFTER the LAST existing test
        `burst_to_one_single_report_unchanged` and BEFORE the closing `}` of
        `mod tests`. Locate by: `grep -n 'fn burst_to_one_single_report_unchanged'
        src/core.rs` then read to the matching test's closing brace.
        (2) SIBLINGS TO MIRROR: `fakehid_drives_generic_burst_to_one`,
        `burst_to_one_captures_last_reply_for_multi_report`, and
        `burst_to_one_single_report_unchanged` (~1443-1603) — copy their
        FakeHid::new()+push_post({mut r; r[0]=1; r}) idiom, their
        `let batch_count = batches_for(&payload); assert_eq!(batch_count, N, ...)`
        convention, and their `(success, reply) = burst_to_one(&fake, &payload,
        batch_count, false)` call shape.
        (3) CODE UNDER TEST: the pre-send drain (`let mut stale_buf = ...` loop,
        ~367-377), the capture-LAST loop (`for _ in 0..batch_count.max(1)` ~393),
        and `batches_for` (~437). Read these to confirm the behavior the tests
        assert.
        (4) FAKEHID CONTRACT: `struct FakeHid` + `impl RawHid for FakeHid` +
        `impl FakeHid { new, push_pre, push_post }` (~803-862). The `written`
        Cell<bool> latch in `read_timeout` is the crux of the ⚠️ CRITICAL note."
  pattern: "Test idiom (copy verbatim from sibling tests):
        `let fake = FakeHid::new();` then
        `fake.push_pre(vec![0u8; 33]);` (stale) and
        `fake.push_post({ let mut r = vec![0u8; 33]; r[0] = 1; r });` (fresh),
        then `let payload = vec![0x41u8; 10]; let batch_count =
        batches_for(&payload); assert_eq!(batch_count, 1, ...);` then
        `let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);`
        then `assert!(success, ...); let reply = reply.expect(...);
        assert_eq!(reply[0], 1, ...);`."
  gotcha: "(1) Use the HELPER API (FakeHid::new + push_pre/push_post), NOT the
           struct-literal form from the architecture doc. (2) The decisive
           assertion is `fake.pre_write_replies.borrow().is_empty()` — without it
           the test false-passes (see ⚠️ CRITICAL). (3) The new tests live INSIDE
           `mod tests`, so they CAN read `fake.pre_write_replies` even though it
           is a private field (same module)."

# MUST READ — the S1 PRP (defines the drain under test + the no-op proof +
# explicitly defers THIS test to S2).
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T3S1/PRP.md
  why: "Defines the pre-send drain block (`let mut stale_buf = [0u8; REPORT_LENGTH
        + 1]; for _ in 0..IN_DRAIN_MAX { match interface.read_timeout(&mut
        stale_buf, 0) { Ok(n) if n > 0 => continue, _ => break } }`) that these
        tests exercise. Its 'No-op proof' section explains WHY all 70 existing
        tests are invisible to the drain (they seed empty pre_write_replies) —
        which is exactly the coverage gap this PRP closes. Its Known Gotchas
        'DO NOT ADD A REGRESSION TEST' note explicitly states the stale-reply
        test is P1.M1.T3.S2 (this PRP)."
  section: "What (the drain block)", "Known Gotchas (no-op proof + DO NOT ADD A
           REGRESSION TEST)", "Success Criteria"
  critical: "Assume the S1 drain is LANDED exactly as its PRP specifies. The
             drain reads pre_write_replies (written still false) and pops stale
             replies until empty, then Ok(0) breaks the loop. Test 1 seeds
             exactly ONE stale reply → drain pops it → pre_write_replies becomes
             empty → write() flips latch → capture reads post → fresh [1]. The
             `is_empty()` assertion confirms the drain ran."

# MUST READ — the FakeHid PRP (defines the double's queue model + helpers).
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S1/PRP.md
  why: "Defines FakeHid's two-queue model (pre_write_replies served while
        written==false; post_write_replies after) and the `push_pre`/`push_post`
        helpers. This is the test seam the new tests use. Treat it as a landed
        contract — DO NOT modify FakeHid."
  section: "FakeHid struct + helpers (push_pre/push_post)", "read_timeout queue
           selection"
  critical: "FakeHid::read_timeout switches queues on the `written` latch at the
             moment write() is called — this is the crux of the ⚠️ CRITICAL note.
             push_pre queues a reply returned ONLY before the first write();
             push_post queues a reply returned ONLY after. The drain runs before
             write(), so it reads pre_write_replies."

# REFERENCE — the bug these tests guard + the live proof of nondeterminism.
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 3 documents the live nondeterminism ('1-report \"hello\" x10:
        0 1 0 0 1 1 1 1 1 1') and the Suggested Fix (pre-send drain). These tests
        are the CI guard that the fix stays in place."
  section: "Major Issues / Issue 3 (Expected/Actual/Suggested Fix)"

# REFERENCE — PRD invariants these tests uphold.
- file: PRD.md
  why: "§7 Send Path, §8 Response Handling, §14 invariant 4 (reply hygiene is
        part of correct send semantics). §4.4 (firmware sends one 32-byte reply
        per report — the source of stale replies)."
  section: "4.4 Replies are received", "7 Send Path", "8 Response Handling",
           "14 Key Invariants (4)"

# REFERENCE — research notes (verified baseline count 70, the ⚠️ CRITICAL
# false-passing analysis, validation expectations).
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T3S2/research/notes.md
  why: "Documents the verified working-tree state (POST-S1: drain present, 70
        tests pass), the FakeHid queue-switch analysis proving reply[0]==1 alone
        is a false-passing test, the decisive is_empty() assertion, and the
        expected post-count (72)."
```

### Current Codebase tree (verified this session)

```bash
.
├── Cargo.toml          # name="qmk_notifier"; deps: clap, hidapi "2.4.1" (NO [dev-dependencies])
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore
└── src
    ├── main.rs         # binary entrypoint — DO NOT TOUCH
    ├── lib.rs          # public API + 26 unit tests — DO NOT TOUCH
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # <-- FILE TO EDIT (TEST MODULE ONLY):
                         #     burst_to_one (355) — CONSUMED (drain ~367, capture ~393)
                         #     batches_for (~437) — CONSUMED (batch_count helper)
                         #     #[cfg(test)] mod tests (787+):
                         #       imports (790-791): Cell, RefCell, VecDeque — PRESENT
                         #       FakeHid + impl RawHid + impl FakeHid (803-862) — CONSUMED
                         #       ... 44 existing tests ...
                         #       burst_to_one_single_report_unchanged (~1571) — LAST TEST
                         #       } closes mod tests
                         #       >>> APPEND BOTH NEW TESTS HERE (before mod tests' `}`) <<<
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── core.rs   # MODIFIED ONLY inside `#[cfg(test)] mod tests`:
              #   append TWO new #[test] fns after burst_to_one_single_report_unchanged:
              #     (1) burst_to_one_drains_stale_replies_before_send
              #     (2) burst_to_one_empty_in_buffer_is_clean
              #   No new files, no new deps, no new types, no new constants,
              #   no imports (Cell/RefCell/VecDeque already imported), no production change.
```

> No new files. The only diff is two `#[test]` functions appended to `mod tests`.

### Known Gotchas of our codebase & Library Quirks

```rust
// ⚠️ CRITICAL — `assert_eq!(reply[0], 1)` ALONE IS A FALSE-PASSING TEST.
//   FakeHid::read_timeout (core.rs ~827-837) selects its queue by self.written.get():
//     false → pop pre_write_replies ; true → pop post_write_replies.
//   The `written` latch is set to true by write(). The capture loop runs AFTER
//   write(), so it ALWAYS reads post_write_replies — a stale reply sitting in
//   pre_write_replies is NEVER read by the capture loop, EVEN IF THE DRAIN DOES
//   NOT EXIST. Therefore:
//     WITHOUT drain: pre=[stale[0]] untouched, write() flips latch, capture reads
//                    post → fresh[1] → reply[0]==1. (TEST FALSELY PASSES.)
//     WITH drain:    drain pops stale[0] (pre now EMPTY), write() flips latch,
//                    capture reads post → fresh[1] → reply[0]==1. (PASSES.)
//   reply[0]==1 in BOTH cases ⇒ it does NOT prove the drain ran. The drain's
//   ONLY observable effect on the fake is that it EMPTIES pre_write_replies
//   before write() flips the latch. So the DECISIVE regression assertion is:
//       assert!(fake.pre_write_replies.borrow().is_empty(), "...");
//   Verify: deleting the S1 drain block makes that assertion FAIL (the stale
//   reply would linger in pre_write_replies because write() never reads pre).
//   Keep BOTH assertions: reply[0]==1 (contract: fresh captured) AND
//   pre_write_replies.is_empty() (decisive: drain ran). Do NOT drop the second.

// CRITICAL — USE THE HELPER API, NOT THE STRUCT-LITERAL FORM.
//   The architecture doc (reply_capture_design.md §Test: Pre-Send Drain) shows
//   the ORIGINAL test sketch building FakeHid via struct-literal:
//       let fake = FakeHid { pre_write_replies: RefCell::new(...), ... };
//   That form was REPLACED in P1.M1.T2.S1 by the helper API. The struct-literal
//   form will NOT compile cleanly / breaks the established convention. Use:
//       let fake = FakeHid::new();
//       fake.push_pre(vec![0u8; 33]);
//       fake.push_post({ let mut r = vec![0u8; 33]; r[0] = 1; r });
//   Exactly as the sibling tests (fakehid_drives_generic_burst_to_one,
//   burst_to_one_captures_last_reply_for_multi_report) do.

// CRITICAL — APPEND INSIDE `mod tests`, AFTER THE LAST TEST.
//   The new tests must live inside the `#[cfg(test)] mod tests { ... }` block
//   (opens ~787). Append them immediately after `burst_to_one_single_report_
//   unchanged` (the current last test, ~1571) and BEFORE the `}` that closes
//   `mod tests`. Being inside `mod tests` is what lets them (a) see `FakeHid`,
//   `burst_to_one`, `batches_for` without imports, and (b) read the private
//   field `fake.pre_write_replies` for the decisive assertion. Locate the slot
//   by content grep, not line number:
//       grep -n "fn burst_to_one_single_report_unchanged" src/core.rs
//   then read to that test's closing `}` and insert after it.

// CRITICAL — NO NEW IMPORTS NEEDED.
//   `mod tests` already imports `use std::cell::{Cell, RefCell};` (core.rs:790)
//   and `use std::collections::VecDeque;` (core.rs:791). The new tests use
//   `fake.pre_write_replies.borrow()` (RefCell → already imported) and
//   `.is_empty()` (VecDeque method → no import). `FakeHid`, `burst_to_one`,
//   `batches_for`, `REPORT_LENGTH`-sized literals (`vec![0u8; 33]`) are all
//   in-scope items in `mod tests`. Do NOT add any `use` statement.

// CRITICAL — DO NOT TOUCH PRODUCTION CODE.
//   This is TEST-ONLY (item DOCS clause: "none — test-only code"). Do NOT edit
//   burst_to_one, the drain block, FakeHid, impl RawHid, batches_for, lib.rs,
//   main.rs, error.rs, or Cargo.toml. If a production change seems needed, STOP
//   — the drain is already landed (S1); these tests only EXERCISE it.

// NOTE — REPLY BUFFER SIZE IS 33 BYTES (`vec![0u8; 33]`), matching
//   REPORT_LENGTH + 1 (REPORT_LENGTH=32). The sibling tests and the architecture
//   doc all use 33-byte reply vectors. Use `vec![0u8; 33]` for both the stale
//   and fresh replies (then mutate [0] for the fresh one). Do NOT use 32.

// NOTE — THE PAYLOAD IS `vec![0x41u8; 10]` (10 'A' bytes), NOT `vec![0u8; 10]`.
//   The item specifies 0x41 to visually distinguish from the all-zeros payloads
//   used by sibling tests (burst_to_one_single_report_unchanged uses vec![0u8;10]).
//   10 bytes ⇒ batches_for == 1 (single report) in both cases; the value is
//   cosmetic but follow the item's 0x41 for traceability. Either compiles.

// NOTE — batch_count IS COMPUTED VIA batches_for AND ASSERTED == 1, matching
//   the sibling tests' convention (not the literal `1` from the item's clause 3
//   sketch). `let batch_count = batches_for(&payload); assert_eq!(batch_count,
//   1, "10-byte payload must fit in 1 report");` — this self-documents the
//   batch math and guards against PAYLOAD_PER_REPORT drift.

// NOTE — `fake.writes.borrow().len()` asserts how many reports were written.
//   For a single-report payload, burst_to_one writes exactly 1 report. Assert
//   `== 1` in both tests (mirrors burst_to_one_single_report_unchanged). This
//   catches a drain that accidentally triggers a write or skips the payload.
```

## Implementation Blueprint

### Data models and structure

No new types. The tests construct a `FakeHid` (the existing test double) via its
`new()`/`push_pre()`/`push_post()` helpers, drive it through the generic
`burst_to_one(&fake, &payload, batch_count, false)`, and assert on the returned
`(bool, Option<Vec<u8>>)` tuple, `fake.writes`, and `fake.pre_write_replies`.

```text
Test data shapes (all pre-existing idioms — no new types):
  stale reply : vec![0u8; 33]                              // 33-byte report, [0]==0
  fresh reply : { let mut r = vec![0u8; 33]; r[0] = 1; r } // 33-byte report, [0]==1 (match)
  payload     : vec![0x41u8; 10]                           // 10 'A' bytes ⇒ 1 report
  batch_count : batches_for(&payload)  == 1                // single-report
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/core.rs mod tests and confirm the FakeHid helper API + siblings
  - CONFIRM the working tree is POST-S1 (pre-send drain landed):
          `grep -n "let mut stale_buf = \[0u8; REPORT_LENGTH + 1\]" src/core.rs`
          -> exactly ONE hit inside burst_to_one. If ABSENT, STOP: prerequisite
          S1 missing (the tests cannot prove a drain that does not exist).
  - CONFIRM the FakeHid helper API (do NOT use struct-literal form):
          `grep -n "fn push_pre\|fn push_post\|fn new() -> Self" src/core.rs`
          -> three hits inside impl FakeHid. These are the helpers to use.
  - READ the three sibling tests to internalize the EXACT style:
          `grep -n "fn fakehid_drives_generic_burst_to_one\|fn burst_to_one_captures_last_reply_for_multi_report\|fn burst_to_one_single_report_unchanged" src/core.rs`
          -> read each test body (FakeHid::new + push_post idiom, batches_for +
          assert pattern, (success, reply) destructuring, reply.expect +
          assert_eq!(reply[0], N) + writes.len() assertions).
  - LOCATE the insertion slot (append after the LAST test in mod tests):
          `grep -n "fn burst_to_one_single_report_unchanged" src/core.rs`
          -> read from that line to the test's closing `}` (then the `}` that
          closes mod tests). Insert the two new tests BETWEEN them.
  - GOAL: know the helper API, the sibling style, and the exact insertion slot.

Task 2: APPEND test 1 — burst_to_one_drains_stale_replies_before_send
  - WRITE the test exactly as sketched in the "What / Test 1" section:
          FakeHid::new(); push_pre(vec![0u8;33]); push_post({r[0]=1}); payload=
          vec![0x41u8;10]; batch_count=batches_for(&payload) asserted ==1;
          (success,reply)=burst_to_one(&fake,&payload,batch_count,false);
          assert success; reply.expect(...); assert_eq!(reply[0],1,...);
          assert!(fake.pre_write_replies.borrow().is_empty(),...);
          assert_eq!(fake.writes.borrow().len(),1,...).
  - DO NOT drop the `pre_write_replies.is_empty()` assertion — it is the
          DECISIVE regression check (see ⚠️ CRITICAL). reply[0]==1 alone
          false-passes.
  - DO NOT use the struct-literal FakeHid form from the architecture doc.

Task 3: APPEND test 2 — burst_to_one_empty_in_buffer_is_clean
  - WRITE the test exactly as sketched in the "What / Test 2" section:
          FakeHid::new() (pre-queue LEFT EMPTY); push_post({r[0]=1}); same
          payload/batch_count; (success,reply)=burst_to_one(...); assert success;
          reply.expect(...); assert_eq!(reply[0],1,...);
          assert_eq!(fake.writes.borrow().len(),1,...).
  - This test does NOT assert on pre_write_replies (it was empty to start);
          its job is to prove the drain is a no-op on the clean path.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo clippy --all-targets`, then `cargo fmt --check`,
          then `cargo test --lib`.
  - EXPECT: clippy zero new warnings; fmt --check exit 0; test result
          "72 passed; 0 failed" (70 baseline + 2 new).
  - RUN targeted: `cargo test --lib burst_to_one_drains_stale_replies_before_send
          -- --nocapture` -> 1 passed; `cargo test --lib
          burst_to_one_empty_in_buffer_is_clean -- --nocapture` -> 1 passed.
  - GENUINE-REGRESSION PROOF (the most important check): temporarily COMMENT OUT
          the S1 drain block in burst_to_one (the `let mut stale_buf = ...` loop,
          ~367-377), re-run `cargo test --lib
          burst_to_one_drains_stale_replies_before_send` -> it MUST FAIL on the
          `pre_write_replies.is_empty()` assertion. Then RESTORE the drain block
          and re-confirm 72 passed. (If it still passes with the drain deleted,
          the test is wrong — re-read the ⚠️ CRITICAL note and ensure the
          is_empty() assertion is present and the pre-queue is seeded via
          push_pre BEFORE write().)
  - COUNT CHECK: `grep -cE "^\s*#\[test\]" src/core.rs` -> 46 (was 44, +2).
```

### Implementation Patterns & Key Details

```rust
// === THE FAKEHID HELPER-API IDIOM (copy from sibling tests; NOT struct-literal) ===
//
//   let fake = FakeHid::new();
//   fake.push_pre(vec![0u8; 33]);                        // stale reply (pre-write queue)
//   fake.push_post({ let mut r = vec![0u8; 33]; r[0] = 1; r });  // fresh reply (post-write queue)
//
//   let payload = vec![0x41u8; 10];                       // 10 bytes => 1 report
//   let batch_count = batches_for(&payload);
//   assert_eq!(batch_count, 1, "10-byte payload must fit in 1 report");
//
//   let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
//   assert!(success, "write path must succeed (FakeHid::write returns Ok)");
//   let reply = reply.expect("must capture the fresh reply, not time out");
//   assert_eq!(reply[0], 1, "must capture the fresh reply, not the stale [0]");


// === THE DECISIVE ASSERTION (without this, the test false-passes) ===
//
//   assert!(
//       fake.pre_write_replies.borrow().is_empty(),
//       "the pre-send drain must consume the stale reply before write() flips the read queue"
//   );
//
// WHY: FakeHid switches read queues on write(). The capture loop runs AFTER
// write(), so it reads only post_write_replies — a stale reply in
// pre_write_replies is never captured even WITHOUT the drain. So reply[0]==1
// passes either way. The drain's only observable effect on the fake is that it
// EMPTIES pre_write_replies (it pops stale replies before write() flips the
// latch). Asserting the queue is empty after the call is the ONLY way to prove
// the drain ran. Deleting the drain block makes this assertion FAIL => genuine
// regression test.


// === WHY `writes.len() == 1` (not 0, not 2) ===
//
//   burst_to_one writes exactly `batch_count` reports. For a 10-byte payload,
//   batch_count == 1, so exactly one write() call is recorded. Asserting ==1
//   catches a drain that accidentally writes (it must only READ) or a payload
//   math bug that splits one report into two. (Mirrors
//   burst_to_one_single_report_unchanged.)


// === WHY TEST 2 DOES NOT ASSERT ON pre_write_replies ===
//
//   Test 2 leaves pre_write_replies EMPTY (the clean path). The drain reads
//   Ok(0) and breaks after one iteration — a no-op. There is nothing to assert
//   about the pre-queue (it was empty and stayed empty). Test 2's job is to
//   prove the drain does not DISTURB normal capture: the fresh post reply is
//   captured intact (reply[0]==1) and exactly one report is written. That is
//   the complement to test 1 (stale => drained) — together they cover both
//   halves of the drain's contract.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY — and ONLY inside #[cfg(test)] mod tests."
  - add:    "two #[test] fns appended after burst_to_one_single_report_unchanged
             (the current last test in mod tests): (1) burst_to_one_drains_
             stale_replies_before_send, (2) burst_to_one_empty_in_buffer_is_clean."

NO OTHER CHANGES:
  - imports:   "none — Cell, RefCell, VecDeque already imported in mod tests
                (core.rs:790-791); FakeHid, burst_to_one, batches_for in scope."
  - production:"NONE — burst_to_one, the drain, FakeHid, impl RawHid, batches_for,
                lib.rs, main.rs, error.rs, Cargo.toml all UNCHANGED."
  - types:     "none — no new struct/trait/enum."
  - constants: "none — REPORT_LENGTH (32), IN_DRAIN_MAX (32), REPLY_READ_TIMEOUT_MS,
                PAYLOAD_PER_REPORT (30) all consumed as-is."
  - deps:      "none — no Cargo.toml change."

CONSUMED (do NOT modify — treat as contracts):
  - P1.M1.T1.S2: "fn burst_to_one<T: RawHid> (core.rs:355) — generic signature
                  (drives FakeHid)."
  - P1.M1.T2.S1: "struct FakeHid + impl RawHid + impl FakeHid{new,push_pre,push_post}
                  (core.rs:803-862) — the test seam; the written-latch queue switch
                  is the crux of the ⚠️ CRITICAL note."
  - P1.M1.T2.S2: "the capture-LAST loop `for _ in 0..batch_count.max(1)` (core.rs
                  ~393) — reads post_write_replies after write()."
  - P1.M1.T3.S1: "the pre-send drain block `let mut stale_buf = ...` (core.rs
                  ~367-377) — the production code these tests defend."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified, and ONLY by appending two #[test] fns inside
    mod tests. Do NOT touch any production code, any other test, FakeHid, impl
    RawHid, the drain, the capture loop, batches_for, lib.rs, error.rs, main.rs,
    or Cargo.toml.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Lint ALL targets (catches any pattern lint in the new test code).
cargo clippy --all-targets 2>&1 | tee /tmp/s2_clippy.log
# Expected: no warnings/errors attributable to the new tests.
# If "unused variable" on `success`: you destructured but didn't assert on it —
#   add `assert!(success, ...)` (the sketches do). If "unnecessary_mut_passed":
#   you passed `&mut buf` instead of `&fake` somewhere — re-check the call shape.

# Formatting gate.
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full lib test suite — the PRIMARY gate. 70 baseline + 2 new = 72.
cargo test --lib
# Expected: "test result: ok. 72 passed; 0 failed; 0 ignored; ..." (+2 from baseline).

# Targeted run of each new test (confirm both are wired and passing):
cargo test --lib burst_to_one_drains_stale_replies_before_send -- --nocapture  # 1 passed
cargo test --lib burst_to_one_empty_in_buffer_is_clean -- --nocapture          # 1 passed

# Confirm the sibling tests still pass (the new tests must not disturb them):
cargo test --lib burst_to_one_captures_last_reply_for_multi_report -- --nocapture  # 1 passed
cargo test --lib burst_to_one_single_report_unchanged -- --nocapture              # 1 passed
cargo test --lib fakehid_ -- --nocapture                                          # 3 passed
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE as a separate step. There is no new external surface — two test
fns inside the existing #[cfg(test)] mod tests. The Level 2 `cargo test --lib`
(72 green) IS the integration validation: it proves the new tests compose
correctly with the FakeHid double and the burst_to_one drain+capture logic.

LIVE HARDWARE: not required (and not useful here — these are unit tests of the
drain via the fake, not hardware probes). The live Issue-3 reproduction
(send 1-report x10, observe stable reply[0]) is covered conceptually by these
unit tests locking the drain's contract.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# ⚠️ THE GENUINE-REGRESSION PROOF — the single most important check.
# Temporarily disable the S1 drain block and confirm test 1 FAILLS, then restore.
# 1. Comment out the drain block in burst_to_one (the `let mut stale_buf = ...`
#    loop, ~367-377). E.g. wrap it in `/* ... */`.
# 2. Re-run ONLY test 1:
cargo test --lib burst_to_one_drains_stale_replies_before_send -- --nocapture
# Expected: FAIL on `assert!(fake.pre_write_replies.borrow().is_empty(), ...)`
#   (the stale reply lingers because write() never reads the pre queue).
#   If it still PASSES, the test is a false-passing no-op — re-read the ⚠️ CRITICAL
#   note and ensure the is_empty() assertion is present and push_pre() is called.
# 3. RESTORE the drain block (uncomment).
# 4. Re-confirm the full suite is green:
cargo test --lib
# Expected: 72 passed; 0 failed.

# Confirm both new tests exist exactly once:
grep -nE "fn burst_to_one_drains_stale_replies_before_send|fn burst_to_one_empty_in_buffer_is_clean" src/core.rs
# Expected: exactly TWO hits, both inside mod tests.

# Confirm the test count went up by exactly 2:
grep -cE "^\s*#\[test\]" src/core.rs
# Expected: 46 (was 44 before this subtask).

# Confirm test 1 contains the DECISIVE assertion (not just reply[0]):
grep -n "pre_write_replies.borrow().is_empty" src/core.rs
# Expected: exactly ONE hit (inside burst_to_one_drains_stale_replies_before_send).

# Confirm no production code was touched (drain + capture loop still present):
grep -nE "let mut stale_buf|for _ in 0..batch_count.max\(1\)|let mut drain_buf" src/core.rs
# Expected: THREE hits (stale_buf drain, capture loop, drain_buf surplus) — unchanged.

# End-to-end count:
cargo test --lib 2>&1 | grep "test result"
# Expected: "72 passed; 0 failed".
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo clippy --all-targets` → zero new warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → **72 passed; 0 failed** (+2 from 70).

### Feature Validation

- [ ] `burst_to_one_drains_stale_replies_before_send` exists in `mod tests` and
      asserts `reply[0] == 1` **AND** `fake.pre_write_replies.borrow().is_empty()`
      **AND** `writes.len() == 1`.
- [ ] `burst_to_one_empty_in_buffer_is_clean` exists in `mod tests` and asserts
      `reply[0] == 1` and `writes.len() == 1` (empty pre-queue, no-op drain).
- [ ] Both tests use the `FakeHid::new()` + `push_pre`/`push_post` helper API
      (NOT the struct-literal form from the architecture doc).
- [ ] Both tests use `payload = vec![0x41u8; 10]` and
      `batch_count = batches_for(&payload)` asserted `== 1`.
- [ ] **Genuine-regression proof**: deleting the S1 drain block makes test 1 FAIL
      on `pre_write_replies.is_empty()` (then restored; 72 green).
- [ ] No production code modified; no file other than `src/core.rs` touched.

### Code Quality Validation

- [ ] Test style mirrors the sibling tests (`fakehid_drives_generic_burst_to_one`,
      `burst_to_one_captures_last_reply_for_multi_report`,
      `burst_to_one_single_report_unchanged`).
- [ ] No new imports (Cell/RefCell/VecDeque already imported in `mod tests`).
- [ ] No new types, constants, or dependencies.
- [ ] `burst_to_one`, the drain, `FakeHid`, `impl RawHid`, the capture loop,
      `batches_for`, all 70 existing tests are UNCHANGED.

### Documentation & Deployment

- [ ] Each test has a `//` doc comment explaining its intent (Issue-3 regression
      / clean-path baseline) and referencing the relevant design-doc section.
- [ ] No external doc files changed (test-only; README/architecture sync is
      `P1.M3`).
- [ ] No Cargo.toml / env / config change.

---

## Anti-Patterns to Avoid

- ❌ Don't assert **only** `reply[0] == 1` in test 1. FakeHid switches read queues
  on `write()`, so the capture loop (post-write) reads only `post_write_replies` —
  a stale reply in `pre_write_replies` is never captured even without the drain.
  `reply[0]==1` passes either way (false positive). You MUST also assert
  `fake.pre_write_replies.borrow().is_empty()` — that is the only observable
  proof the drain ran. (See ⚠️ CRITICAL.)
- ❌ Don't use the struct-literal `FakeHid { pre_write_replies: RefCell::new(...),
  ... }` form from `architecture/reply_capture_design.md`. It was replaced by the
  `FakeHid::new()` + `push_pre()`/`push_post()` helper API in P1.M1.T2.S1. Use
  the helpers, exactly as the sibling tests do.
- ❌ Don't seed the stale reply via `push_post` (or the fresh reply via `push_pre`).
  `push_pre` ⇒ pre-write queue (read before write, i.e. by the drain); `push_post`
  ⇒ post-write queue (read after write, i.e. by the capture loop). Getting these
  backwards makes the test assert the wrong thing (and likely false-pass).
- ❌ Don't place the new tests outside `mod tests`. They must live inside
  `#[cfg(test)] mod tests { ... }` so they (a) see `FakeHid`/`burst_to_one`/
  `batches_for` without imports, and (b) can read the private field
  `fake.pre_write_replies` for the decisive assertion.
- ❌ Don't add new `use` statements. `Cell`, `RefCell`, `VecDeque` are already
  imported in `mod tests` (core.rs:790-791). Adding a duplicate import trips
  clippy's `unused_imports` / duplicate-import lint.
- ❌ Don't touch any production code. This is test-only (item DOCS clause: "none
  — test-only code"). If `burst_to_one`, the drain, `FakeHid`, or `batches_for`
  seems to need a change, STOP — the drain is already landed (S1); these tests
  only EXERCISE it.
- ❌ Don't use a 32-byte reply buffer. The report buffer is `REPORT_LENGTH + 1` =
  33 bytes (all sibling tests and the architecture doc use `vec![0u8; 33]`). A
  32-byte vector would be read as `n=32` (still `>0`, so the drain/capture logic
  would still work) but breaks the established idiom and is misleading.
- ❌ Don't assert `writes.len() == 0` or `== 2` in these tests. A single-report
  payload (`vec![0x41u8; 10]`) yields `batch_count == 1`, so exactly ONE
  `write()` call is recorded. Assert `== 1` (mirrors
  `burst_to_one_single_report_unchanged`).
- ❌ Don't skip the Level-4 genuine-regression proof. "The test passes" is not
  enough — you must confirm it FAILS when the drain is deleted, or it is a
  false-passing no-op defending nothing. Comment out the S1 drain block, re-run
  test 1, confirm FAIL on `is_empty()`, restore, re-confirm 72 green.
- ❌ Don't duplicate `burst_to_one_single_report_unchanged` verbatim and rename
  it. Test 2 shares the "single-report, fresh reply captured" shape, but its
  INTENT (Issue-3 clean-path baseline — the drain is a no-op when there's nothing
  to drain) is distinct. Write it with that framing (doc comment + the 0x41
  payload) so its purpose is clear, not a copy-paste artifact.

---

**Confidence Score: 10/10** for one-pass implementation success. Both tests are
given as ready-to-paste sketches that copy an established idiom verbatim
(`FakeHid::new()` + `push_pre`/`push_post`, the `(success, reply) =
burst_to_one(&fake, &payload, batch_count, false)` call, the `reply.expect` +
`assert_eq!(reply[0], N)` + `writes.len()` assertions) from three sibling tests
already in the file. The insertion point is pinned by content grep (after
`burst_to_one_single_report_unchanged`, before `mod tests`'s closing `}`), no new
imports/types/constants/deps are needed, and the verified validation commands
confirm the baseline (`cargo test --lib` = 70 passed today, drain already landed).
The one non-obvious trap — that `reply[0]==1` alone is a false-passing test
because FakeHid switches read queues on `write()`, so the decisive assertion must
be `pre_write_replies.is_empty()` — is fully explained and is what elevates this
from a feel-good test to a genuine regression test (proven by the Level-4 check
that deleting the drain makes it fail). An implementer who follows the blueprint
produces a diff that is exactly two `#[test]` fns appended to `mod tests`, with
72 tests green.