# PRP — P1.M1.T2.S1: Add FakeHid test double under #[cfg(test)]

---

## Goal

**Feature Goal**: Add a hand-rolled **`FakeHid` test double** inside the existing
`#[cfg(test)] mod tests` block in `src/core.rs` — a struct that implements the
`RawHid` trait (delivered by P1.M1.T1.S1) so the now-generic `burst_to_one<T:
RawHid>` (delivered by P1.M1.T1.S2) can be exercised in unit tests **without a
physical QMK keyboard**. `FakeHid` models the firmware's reply semantics with
**two reply queues** — `pre_write_replies` (stale IN-buffer data read before any
`write()`, which the upcoming Issue-3 pre-send drain will flush) and
`post_write_replies` (the firmware's fresh per-report replies read after
`write()`) — switched by a `Cell<bool>` "written" latch, plus a recorded-writes
log for call-count assertions. It is the prerequisite testability seam for the
Issue-1 capture fix (P1.M1.T2.S2/S3) and the Issue-3 drain fix (P1.M1.T3). **No
`[dev-dependencies]`, no mocking framework** — pure `std::cell` + `std::collections::VecDeque`.

**Deliverable**: The `FakeHid` struct (4 fields), `impl RawHid for FakeHid`
(2 methods), and `impl FakeHid` helpers (`new`, `push_pre`, `push_post`) inserted
into `src/core.rs`'s `mod tests` block after the existing `use` statements (plus
two new std imports: `std::cell::{Cell, RefCell}` and `std::collections::VecDeque`),
along with **3 smoke tests** that prove FakeHid is a faithful `RawHid` double and
silence the `cargo test` dead_code warnings its otherwise-unreferenced members
would emit. **`src/core.rs` is the only file modified.** `RawHid` trait/impl and
`burst_to_one` are consumed unchanged — NOT modified.

**Success Definition**: `cargo build` compiles with **zero warnings**; `cargo
clippy --all-targets` shows **zero new warnings**; `cargo fmt --check` exits 0;
`cargo test --lib` passes with **68 tests** (65 current + 3 new), 0 failed;
FakeHid implements `RawHid` and is drivable as `burst_to_one(&fake, &payload,
batch_count, false)`; the smoke tests assert `success` / write-count /
`reply.is_some()` but **never** the captured reply's byte value (stays decoupled
from the T2.S2 capture rewrite); no file other than `src/core.rs` is modified.

## User Persona (if applicable)

**Target User**: The implementer of the v0.3.1 reply-capture bug fixes —
specifically the engineer writing P1.M1.T2.S2 (rewrite capture to keep the ETX
reply) and P1.M1.T2.S3 / P1.M1.T3.S2 (the multi-report + stale-reply regression
tests that use FakeHid).

**Use Case**: Stand up a deterministic, in-process stand-in for
`hidapi::HidDevice` so the reply-capture/drain logic inside `burst_to_one` can be
asserted on without hardware. FakeHid lets a test pre-seed the exact reply stream
the firmware would emit (e.g. `[0, 1]` for a 2-report message) and inspect what
`burst_to_one` wrote + captured.

**User Journey**: `let mut fake = FakeHid::new(); fake.push_post(reply_a);
fake.push_post(reply_b); let (ok, captured) = burst_to_one(&fake, &payload,
batch_count, false); assert_eq!(fake.writes.borrow().len(), batch_count);` → the
test owns the "device's" reply stream and asserts on it deterministically.

**Pain Points Addressed**: Today `burst_to_one` takes `&HidDevice` (well, now the
generic `&T: RawHid`), but the only available `T` was the real hidapi type — so
the Issue-1/Issue-3 defects could only be reproduced on a live keyboard
(`TEST_RESULTS.md`). FakeHid is the in-CI double that makes those fixes
testable and regression-locked.

## Why

- FakeHid is the **direct consumer** of the `RawHid` trait (P1.M1.T1.S1) and the
  **first non-`HidDevice` `T`** passed to the generic `burst_to_one`
  (P1.M1.T1.S2). It is what makes the trait-abstraction seam pay off — without
  it, the trait is an unused abstraction (S2 cleared the trait's dead_code by
  making `burst_to_one` generic, but the *value* of genericizing is realized only
  when a second `T` exists).
- It is the **testability foundation** for ALL of milestone P1.M1's fixes:
  Issue-1 (multi-report capture returns the intermediate `[0]` reply instead of
  the ETX-report result) and Issue-3 (no pre-send IN-buffer drain) both live in
  `burst_to_one`'s body and need a controllable reply stream to assert against.
  FakeHid's two-queue design supports BOTH regression shapes (multi-report
  `post_write` stream `[0,…,0,match]`; stale `pre_write` data) without redesign.
- It is **additive and test-only** — compiled only under `#[cfg(test)]`, so it
  cannot affect the runtime/`cargo build` path or the public API. No new deps,
  no new files.

## What

### The FakeHid double (paste into `mod tests`)

Insert immediately after the existing `use crate::{CommandResponse, HostOs, RunCommand};`
line in the `#[cfg(test)] mod tests` block (current `src/core.rs:753`):

```rust
    use std::cell::{Cell, RefCell};
    use std::collections::VecDeque;

    // === FakeHid test double (P1.M1.T2.S1) =====================================
    // An in-process stand-in for `hidapi::HidDevice` that implements [`RawHid`],
    // so the (generic) [`burst_to_one`] can be unit-tested without a physical
    // keyboard. Models the firmware's reply semantics with two reply queues:
    //
    // - `pre_write_replies`  — stale IN-buffer data read BEFORE any `write()`
    //   (the kind a prior send leaves behind; flushed by the Issue-3 pre-send
    //   drain in P1.M1.T3.S1).
    // - `post_write_replies` — the firmware's fresh per-report replies read
    //   AFTER `write()` (one 32-byte reply per report; PRD §4.4).
    //
    // A `Cell<bool>` "written" latch selects which queue `read_timeout` pops:
    // before the first `write()` it serves `pre_write_replies`, after it serves
    // `post_write_replies` (one-shot latch — never reset; FakeHid is single-use
    // per test). An empty queue returns `Ok(0)` to mirror hidapi's
    // `read_timeout` timeout/no-data semantics (NOT an error).
    struct FakeHid {
        pre_write_replies: RefCell<VecDeque<Vec<u8>>>,
        post_write_replies: RefCell<VecDeque<Vec<u8>>>,
        written: Cell<bool>,
        /// Recorded `write()` calls, for asserting how many reports were sent.
        writes: RefCell<Vec<Vec<u8>>>,
    }

    impl RawHid for FakeHid {
        fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError> {
            self.writes.borrow_mut().push(data.to_vec());
            self.written.set(true);
            Ok(data.len())
        }
        fn read_timeout(&self, buf: &mut [u8], _timeout: i32) -> Result<usize, hidapi::HidError> {
            let queue = if self.written.get() {
                &self.post_write_replies
            } else {
                &self.pre_write_replies
            };
            match queue.borrow_mut().pop_front() {
                Some(reply) => {
                    let n = reply.len().min(buf.len());
                    buf[..n].copy_from_slice(&reply[..n]);
                    Ok(n)
                }
                None => Ok(0), // empty queue ⇒ timeout/no-data (matches hidapi)
            }
        }
    }

    impl FakeHid {
        /// Empty double, written-latch unset (reads serve the pre-write queue).
        fn new() -> Self {
            Self {
                pre_write_replies: RefCell::new(VecDeque::new()),
                post_write_replies: RefCell::new(VecDeque::new()),
                written: Cell::new(false),
                writes: RefCell::new(Vec::new()),
            }
        }
        /// Queue a reply that `read_timeout` will return BEFORE any `write()`
        /// (stale IN-buffer data from a prior send).
        fn push_pre(&self, reply: Vec<u8>) {
            self.pre_write_replies.borrow_mut().push_back(reply);
        }
        /// Queue a reply that `read_timeout` will return AFTER the first
        /// `write()` (a firmware reply to a sent report).
        fn push_post(&self, reply: Vec<u8>) {
            self.post_write_replies.borrow_mut().push_back(reply);
        }
    }
```

### The 3 smoke tests (append at the END of `mod tests`)

> Append these AFTER the last existing test and BEFORE the `mod tests` closing
> `}`. They are **capture-agnostic**: they assert on `success`, write-count, and
> `reply.is_some()` — NEVER on the captured reply's byte value (which is exactly
> what T2.S2 changes). This keeps them stable across the T2.S2 capture rewrite
> and the T3.S1 pre-send drain.

```rust
    #[test]
    fn fakehid_write_records_calls_and_switches_read_queue() {
        let fake = FakeHid::new();
        fake.push_pre(vec![0xAA; 33]);   // stale IN-buffer byte (Issue-3 shape)
        fake.push_post({ let mut r = vec![0u8; 33]; r[0] = 1; r }); // firmware match reply

        // BEFORE any write(): read_timeout serves the PRE-write (stale) queue.
        let mut buf = [0u8; 33];
        let n = fake.read_timeout(&mut buf, 0).expect("pre-write read Ok");
        assert_eq!(n, 33);
        assert_eq!(buf[0], 0xAA, "pre-write read must pop the stale pre-queue reply");

        // write() records the call and flips the written latch to the POST queue.
        let report = [0x00u8, 0x81, 0x9F, 0x03];
        fake.write(&report).expect("write Ok");
        assert_eq!(fake.writes.borrow().len(), 1, "write must record exactly one call");
        assert_eq!(
            fake.writes.borrow()[0],
            report.to_vec(),
            "write must record the exact bytes sent"
        );

        // AFTER write(): read_timeout serves the POST-write (firmware) queue.
        let n = fake.read_timeout(&mut buf, 0).expect("post-write read Ok");
        assert_eq!(n, 33);
        assert_eq!(buf[0], 1, "post-write read must pop the firmware reply, not stale data");
    }

    #[test]
    fn fakehid_read_timeout_returns_zero_when_queue_empty() {
        let fake = FakeHid::new();
        // Empty pre-write queue ⇒ Ok(0), mirroring hidapi read_timeout timeout
        // semantics (NOT an Err). burst_to_one's drain relies on this to stop.
        let mut buf = [0u8; 33];
        let n = fake.read_timeout(&mut buf, 0).expect("empty queue must be Ok, not Err");
        assert_eq!(n, 0, "empty queue ⇒ Ok(0) (hidapi timeout semantics)");
        // buf left untouched (no copy on the None arm).
        assert!(buf.iter().all(|&b| b == 0));
    }

    #[test]
    fn fakehid_drives_generic_burst_to_one() {
        // Proves FakeHid is usable as `burst_to_one(&fake, …)` (the T: RawHid
        // bound) and that one queued reply is captured. Asserts ONLY on success,
        // write-count, and reply presence — NOT the reply's byte value, which is
        // what the T2.S2 capture rewrite changes (keeps this test stable).
        let mut fake = FakeHid::new();
        fake.push_post({ let mut r = vec![0u8; 33]; r[0] = 1; r }); // one firmware reply

        let payload = vec![0u8; 10]; // 10 bytes ⇒ 1 report (PAYLOAD_PER_REPORT=30)
        let batch_count = batches_for(&payload);
        assert_eq!(batch_count, 1);

        let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
        assert!(success, "write must succeed (FakeHid::write returns Ok)");
        assert_eq!(
            fake.writes.borrow().len(),
            batch_count,
            "burst_to_one must write exactly batch_count reports"
        );
        assert!(reply.is_some(), "the one queued reply must be captured");
    }
```

### Success Criteria

- [ ] `struct FakeHid` exists in `mod tests` with exactly the 4 fields
      `pre_write_replies: RefCell<VecDeque<Vec<u8>>>`,
      `post_write_replies: RefCell<VecDeque<Vec<u8>>>`,
      `written: Cell<bool>`, `writes: RefCell<Vec<Vec<u8>>>`.
- [ ] `impl RawHid for FakeHid` provides `write` (records + sets written, returns
      `Ok(data.len())`) and `read_timeout` (pops the queue selected by `written`,
      copies into `buf`, returns `Ok(n)`; empty queue returns `Ok(0)`).
- [ ] `impl FakeHid` provides `new()` (empty, written=false), `push_pre(reply)`,
      `push_post(reply)`.
- [ ] `use std::cell::{Cell, RefCell};` and `use std::collections::VecDeque;` are
      added to `mod tests`'s use block.
- [ ] Exactly 3 new `#[test]` fns are appended at the END of `mod tests`.
- [ ] The smoke tests assert on `success`/write-count/`reply.is_some()` only —
      NEVER on the captured reply's byte value.
- [ ] `cargo build` → zero warnings; `cargo clippy --all-targets` → zero new
      warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → 68 passed, 0
      failed.
- [ ] `RawHid` trait/impl and `burst_to_one` are UNCHANGED (consumed, not edited).
- [ ] No file other than `src/core.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim FakeHid struct
> + both impl blocks + helpers, the verbatim 3 smoke tests, the exact placement
> anchor (a unique stable line), the two imports to add (and why they're absent
> today), the `RawHid` trait signature it must satisfy, the dead_code-in-test
> gotcha (with the mitigation = the smoke tests themselves), the capture-agnostic
> assertion rationale, and the verified build/clippy/fmt/test commands are all
> below. The implementer needs no keyboard and no hidapi source dive.

### Documentation & References

```yaml
# MUST READ — the canonical design for FakeHid (struct + impl, verbatim)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§FakeHid Test Double gives the exact struct fields, the impl RawHid body
        (two-queue + Cell<bool> latch, Ok(0) on empty), and the two regression
        tests this double will eventually drive (T2.S3 multi-report, T3.S2
        stale). §Step 4 shows the capture-last fix this enables. This is the
        source-of-truth design — FakeHid must match it."
  section: "FakeHid Test Double", "Step 4 (Capture-Last-Reply)", "Edge Cases"
  critical: "The two-queue + written-latch shape is REQUIRED to support BOTH the
             Issue-1 capture test (post_write stream [0,…,0,match]) and the
             Issue-3 stale-drain test (pre_write stale data). Do not collapse to
             one queue."

# MUST READ — the trait FakeHid implements (the interface contract)
- file: src/core.rs
  why: "Lines 13-31 hold `pub(crate) trait RawHid { write, read_timeout }` and
        `impl RawHid for hidapi::HidDevice` (both from P1.M1.T1.S1, LANDED).
        FakeHid's impl RawHid must match this exact signature (return type
        Result<usize, hidapi::HidError>). `use super::*;` in mod tests brings
        RawHid into scope — no extra import needed for the trait."
  pattern: "Trait methods return `Result<usize, hidapi::HidError>` (fully-qualified
            error type). hidapi read_timeout returns Ok(0) on timeout (NOT Err) —
            FakeHid mirrors that in the empty-queue arm."
  gotcha: "Do NOT modify the trait or its hidapi impl — consume them as-is."

# MUST READ — the generic function FakeHid must be drivable through
- file: src/core.rs
  why: "Line 337: `fn burst_to_one<T: RawHid>(interface: &T, data: &[u8],
        batch_count: usize, verbose: bool) -> (bool, Option<Vec<u8>>)`. Body
        calls interface.write() once per batch, then ONE read_timeout capture,
        then a non-blocking drain. (P1.M1.T1.S2, LANDED.) Smoke test #3 passes
        &FakeHid as the `&T` and asserts on success/write-count/is_some()."
  gotcha: "burst_to_one currently captures the FIRST reply (the Issue-1 bug) —
           do NOT assert on the captured reply's VALUE in smoke tests (T2.S2
           changes it). Assert only success / writes.len() / reply.is_some()."

# MUST READ — the file being edited (placement + imports + test module)
- file: src/core.rs
  why: "The `#[cfg(test)] mod tests` block starts at line 750 with
        `use super::*; use crate::{CommandResponse, HostOs, RunCommand};`.
        core.rs has NO `std::cell`/`std::collections` imports today (only
        `use std::sync::{...}`) — the two new imports must be added here.
        FakeHid inserts right after the `use crate::{...};` line; the 3 smoke
        tests append after the last existing test."
  pattern: "Tests use `use super::*;` and descriptive snake_case names
            (matches_when_*, batches_for_*, rejects_*). Private items in mod
            tests are referenced directly (no pub needed)."
  gotcha: "LOCATE the insertion/append anchors by content (`grep -n 'use crate::{
           CommandResponse'`, `grep -n 'mod tests'`), not by the contract's line
           numbers — earlier subtasks shifted them."

# MUST READ — the parallel sibling PRP whose output FakeHid consumes
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T1S2/PRP.md
  why: "Defines/landed the generic `burst_to_one<T: RawHid>` (the `&T` param
        FakeHid is passed as) and explicitly lists this task as its first
        downstream consumer. Confirms burst_to_one's body calls write +
        read_timeout (capture) + read_timeout (drain) — i.e. the exact RawHid
        methods FakeHid must implement faithfully."
  section: "What" (the signature) and "Integration Points / DOWNSTREAM CONSUMERS"
  critical: "Treat S1 (RawHid trait) + S2 (generic burst_to_one) as a CONTRACT.
             FakeHid must NOT modify either."

# REFERENCE — the bug context this double ultimately enables fixing
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 1 (multi-report capture returns the intermediate [0] reply) and
        Issue 3 (stale replies across sends) are the defects FakeHid exists to
        make testable. Knowing them prevents over-scoping THIS task (the actual
        fixes + their regression tests are T2.S2/S3 and T3.S1/S2 — this task
        delivers ONLY the double + its own smoke tests)."
  section: "Critical Issues / Issue 1" and "Major Issues / Issue 3"

# REFERENCE — research notes (design rationale, dead_code proof, baseline)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S1/research/notes.md
  why: "Documents the two-queue rationale, the dead_code-in-cargo-test behavior,
        the capture-agnostic assertion strategy, the compile gotchas (_timeout,
        queue binding type unification, return type), and the verified baseline
        (65 tests, zero warnings)."
```

### Current Codebase tree (verified post-S1/S2)

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.3.0"; deps: clap, hidapi "2.4.1" (NO [dev-dependencies])
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore
└── src
    ├── main.rs         # binary entrypoint — DO NOT TOUCH
    ├── lib.rs          # public API (26 tests) — DO NOT TOUCH
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # <-- FILE TO EDIT:
                         #     RawHid trait+impl (S1, lines 13-31) — CONSUMED
                         #     burst_to_one<T: RawHid> (S2, line 337) — CONSUMED
                         #     #[cfg(test)] mod tests (line 750) — EDITED (FakeHid + 3 tests)
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── core.rs   # MODIFIED ONLY — FakeHid struct + impl RawHid + impl FakeHid (helpers)
              #                     + 2 std imports in mod tests
              #                     + 3 smoke tests at the END of mod tests
              #   No new files, no new deps, no public-API change.
```

> No new files. All changes are inside `src/core.rs`'s `#[cfg(test)] mod tests`.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: dead_code in `cargo test`, NOT `cargo build`. FakeHid is #[cfg(test)]
//   so `cargo build` never compiles it (no build-time warning). But `cargo test`
//   DOES compile it, and an unreferenced private struct + unreferenced
//   fields/methods emit dead_code warnings ("field is never read", "method is
//   never used"). The 3 smoke tests MUST reference every member (all 4 fields
//   via new()+writes reads; new/push_pre/push_post; both RawHid methods) — that
//   is simultaneously the dead-code silencer AND the proof FakeHid is faithful.

// CRITICAL: smoke tests must be CAPTURE-AGNOSTIC. burst_to_one currently
//   captures the FIRST reply (the Issue-1 bug); T2.S2 rewrites it to keep the
//   LAST; T3.S1 adds a pre-send drain. Assert ONLY on success, writes.len(),
//   and reply.is_some() — NEVER on the captured reply's byte value (that is
//   T2.S3's regression-test job and would couple to the not-yet-fixed capture).
//   Stability proof: with one post_write reply + empty pre_write queue, success
//   + write-count + is_some() are identical before and after T2.S2/T3.S1.

// CRITICAL: RawHid method return type is `Result<usize, hidapi::HidError>`
//   (fully-qualified error). FakeHid never errors ⇒ `Ok(...)` suffices, but the
//   type must match the trait exactly (do NOT use a type alias or `Result<_, _>`).

// NOTE: `read_timeout`'s `timeout: i32` param is unused by FakeHid → name it
//   `_timeout` (leading underscore) so clippy `unused_variables` stays clean.

// NOTE: the `let queue = if … { &self.post_write_replies } else { &self.pre_write_replies };`
//   binding is NOT a needless borrow — both branches must unify to
//   `&RefCell<VecDeque<Vec<u8>>>` for the binding. clippy leaves it alone.

// NOTE: no `#[cfg(test)]` attribute on the FakeHid struct itself. It lives
//   INSIDE the already-`#[cfg(test)] mod tests` block, so a nested attr is
//   redundant (the design doc only shows one because it depicts the struct at
//   module level). Omit it.

// NOTE: no `#[derive(Debug)]` needed. burst_to_one never prints the interface,
//   and default clippy does not require Debug on structs.

// NOTE: RefCell borrow discipline — each FakeHid method borrows + drops within
//   the call; no nested/aliased borrows ⇒ no runtime BorrowMutError panic.

// NOTE: there is NO mocking framework and NO [dev-dependencies] in Cargo.toml.
//   FakeHid is hand-rolled with std::cell + std::collections::VecDeque. Do NOT
//   add a dev-dependency (e.g. mockall) — the contract forbids it and the
//   design doc specifies the hand-rolled approach.

// NOTE: `batches_for` (used in smoke test #3) is a private core.rs fn brought
//   into scope by `use super::*;`. 10 bytes ⇒ (10+32-3)/30 = 1 report.
```

## Implementation Blueprint

### Data models and structure

The only new "model" is the `FakeHid` struct (4 fields) — a pure in-memory state
machine with no parsing logic. Its two queues + written-latch encode the
firmware's reply timing; the writes log records call history. No new public
types, no enums, no trait edits.

```rust
// FakeHid is a private test struct (no pub). Interior mutability via
// RefCell/Cell so `impl RawHid` (which takes &self, NOT &mut self) can mutate
// queues/flags — matching hidapi::HidDevice's &self method signatures.
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/core.rs and confirm the inputs + anchors
  - READ: lines 13-31 — CONFIRM `pub(crate) trait RawHid` + `impl RawHid for
          hidapi::HidDevice` are present (P1.M1.T1.S1, LANDED). If ABSENT, STOP:
          S1 not landed; FakeHid cannot compile.
  - READ: line 337 — CONFIRM `fn burst_to_one<T: RawHid>(...)` (P1.M1.T1.S2,
          LANDED). Read its body's three `interface.*` call sites (write,
          read_timeout capture, read_timeout drain) — these are the RawHid
          methods FakeHid must implement.
  - LOCATE the `#[cfg(test)] mod tests` block via `grep -n "mod tests" src/core.rs`
          (expect ~750). Read its opening `use super::*; use crate::{CommandResponse,
          HostOs, RunCommand};` (the insertion anchor for FakeHid + the 2 imports).
  - LOCATE the LAST #[test] in mod tests (the append anchor for the 3 smoke tests).
  - CONFIRM core.rs has no `use std::cell` / `use std::collections` today (only
          `use std::sync::{...}`) — the 2 new imports are genuinely new.
  - GOAL: know the exact, unique anchors so both edits are surgical.

Task 2: ADD the two std imports + the FakeHid struct/impl/helpers to mod tests
  - INSERT (immediately after `use crate::{CommandResponse, HostOs, RunCommand};`):
          `use std::cell::{Cell, RefCell};`, `use std::collections::VecDeque;`,
          then the FakeHid struct + `impl RawHid for FakeHid` + `impl FakeHid`
          helpers, verbatim from the "What" section.
  - KEEP: every existing test, every const (DEV_*), every use statement, the
          RawHid trait/impl, burst_to_one, and all other functions unchanged.
  - NAMING: struct `FakeHid`; methods `write`, `read_timeout` (trait),
          `new`, `push_pre`, `push_post` (helpers). snake_case methods.
  - DO NOT: add `#[cfg(test)]` on the struct (redundant inside mod tests); add
            `#[derive(Debug)]`; add a dev-dependency; modify RawHid/burst_to_one.

Task 3: APPEND the 3 capture-agnostic smoke tests at the END of mod tests
  - ADD: `fakehid_write_records_calls_and_switches_read_queue`,
          `fakehid_read_timeout_returns_zero_when_queue_empty`,
          `fakehid_drives_generic_burst_to_one` (see "What").
  - PLACEMENT: after the last existing #[test], before the mod tests closing `}`.
  - PATTERN: use the already-present `use super::*;` (brings RawHid, burst_to_one,
          batches_for into scope). Match the file's descriptive snake_case style.
  - ASSERTIONS: success / writes.len() / reply.is_some() ONLY — NEVER the
          captured reply byte value (capture-agnostic; see Known Gotchas).

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --all-targets`,
          then `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 new warnings; fmt --check exit 0;
          test result "68 passed; 0 failed" (65 current + 3 new).
  - IF dead_code warnings appear in `cargo test` on FakeHid members: a member is
          unreferenced — extend a smoke test to use it (the 3 tests above already
          reference all members; verify none was dropped).
  - IF "the trait bound `FakeHid: RawHid` is not satisfied": you forgot or
          mis-spelled `impl RawHid for FakeHid` — re-check it matches the trait
          signature (return type `Result<usize, hidapi::HidError>`).
  - IF clippy `unused_variables` on `timeout`: rename to `_timeout`.
```

### Implementation Patterns & Key Details

```rust
// === PLACEMENT ANCHOR (illustrative; locate by content, not line number) ===
//
// #[cfg(test)]
// mod tests {
//     use super::*;
//     use crate::{CommandResponse, HostOs, RunCommand};
//     // >>> INSERT `use std::cell::{Cell, RefCell};` + `use std::collections::VecDeque;`
//     //     + the FakeHid struct + impl RawHid + impl FakeHid HERE <<<
//
//     const DEV_VID: u16 = 0xFEED;
//     ...
//     #[test]
//     fn <last existing test>() { ... }
//
//     // >>> APPEND the 3 fakehid_* smoke tests HERE, before the closing } <<<
// }


// === WHY TWO QUEUES + A CELL<bool> LATCH ===
//   The firmware emits one 32-byte reply per HID report (PRD §4.4). For a
//   multi-report message replies arrive as [0,…,0,match] (Issue 1). Separately,
//   a prior send can leave stale replies in the IN buffer (Issue 3). FakeHid's
//   pre_write queue models the stale buffer (read before write); post_write
//   models the firmware's fresh replies (read after write). The Cell<bool>
//   written-latch selects the queue — a one-shot flip on the first write().
//   This single shape drives BOTH the T2.S3 multi-report test and the T3.S2
//   stale-reply test without redesign.


// === WHY &self (not &mut self) for the RawHid METHODS ===
//   hidapi::HidDevice::write/read_timeout take &self. RawHid mirrors that.
//   FakeHid therefore MUST mutate interior state through RefCell/Cell (not
//   &mut self) so impl RawHid for FakeHid satisfies the &self signature.


// === WHY _timeout (unused) ===
//   FakeHid returns immediately from its queue (no real blocking), so the
//   timeout arg is unused. Leading-underscore name keeps clippy happy.


// === WHY ASSERT success / writes.len() / is_some() — NEVER reply value ===
//   burst_to_one's capture logic changes in T2.S2 (first→last). Asserting on the
//   captured byte would couple this smoke test to the not-yet-fixed logic and
//   force a rewrite when T2.S2 lands. success (write Ok), writes.len() (batch
//   count), and reply.is_some() (one reply captured) are invariant across that
//   rewrite. The reply-VALUE regression test is T2.S3's job.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY (inside #[cfg(test)] mod tests)"
  - add:    "2 std imports (std::cell::{Cell,RefCell}, std::collections::VecDeque)"
  - add:    "struct FakeHid (4 fields) + impl RawHid for FakeHid + impl FakeHid helpers"
  - add:    "3 capture-agnostic smoke tests at the END of mod tests"

DEPENDENCIES / Cargo.toml:
  - none. NO [dev-dependencies]. std::cell + std::collections are std-prelude
    (no Cargo change). Hand-rolled double per the contract/design doc.

PUBLIC API SURFACE:
  - unchanged. "FakeHid is a private test struct inside #[cfg(test)] mod tests;
    it is not pub, not exported, not visible outside core.rs's test build."

CONSUMED (do NOT modify — treat as contracts):
  - P1.M1.T1.S1: "pub(crate) trait RawHid { write(&self,&[u8])->Result<usize,HidError>;
                   read_timeout(&self,&mut[u8],i32)->Result<usize,HidError> } (src/core.rs:13)."
  - P1.M1.T1.S2: "fn burst_to_one<T: RawHid>(&T,&[u8],usize,bool)->(bool,Option<Vec<u8>>)
                   (src/core.rs:337). FakeHid is the first non-HidDevice T passed to it."

DOWNSTREAM CONSUMERS (do NOT implement now — listed for sequencing awareness):
  - P1.M1.T2.S2: "rewrite burst_to_one capture to KEEP THE LAST reply (ETX report)
                   — the Issue-1 fix this double exists to test."
  - P1.M1.T2.S3: "multi-report reply-capture regression test USING FakeHid (asserts
                   captured reply == ETX result for a 2-report payload)."
  - P1.M1.T3.S1: "pre-send IN-buffer drain inside burst_to_one (Issue-3 fix)."
  - P1.M1.T3.S2: "stale-reply regression test USING FakeHid (asserts a stale
                   pre_write reply is drained, not captured)."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified. Do NOT touch lib.rs, error.rs, main.rs, Cargo.toml.
  - Do NOT write the Issue-1/Issue-3 regression tests (T2.S3 / T3.S2) — this task
    delivers the double + its own smoke tests only.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
# (FakeHid is #[cfg(test)] ⇒ not compiled here; this confirms no collateral damage.)
cargo build 2>&1 | tee /tmp/s1_build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.

# Lint ALL targets (includes test code — this IS where FakeHid gets linted).
cargo clippy --all-targets 2>&1 | tee /tmp/s1_clippy.log
# Expected: no warnings/errors for FakeHid.
# If "unused variable `timeout`": rename to `_timeout`.
# If "field `X` is never read" / "method `Y` is never used": that FakeHid member
#   is unreferenced — a smoke test must exercise it (the 3 tests cover all members).

# Formatting gate.
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the 3 new smoke tests in isolation first.
cargo test --lib fakehid_ -- --nocapture
# Expected: 3 passed.

# Full lib test suite (core.rs #[cfg(test)] mod tests + lib.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 68 passed; 0 failed; 0 ignored; ..." (65 current + 3 new).

# Sanity: confirm the pre-existing 65 tests STILL pass untouched.
cargo test --lib batches_for_ -- --nocapture
cargo test --lib matches_when_all_four_equal -- --nocapture
# Expected: all pre-existing tests pass.
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
FakeHid is a test double compiled only under #[cfg(test)] — it has no runtime
path, no HID I/O, and no public surface. The "integration" it enables (driving
burst_to_one's reply-capture logic) is realized by the DOWNSTREAM regression
tests (P1.M1.T2.S3 multi-report, P1.M1.T3.S2 stale-reply), not by this task.
The 3 smoke tests in Level 2 ARE the verification that FakeHid faithfully
implements RawHid and is drivable through burst_to_one.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm FakeHid implements RawHid (the compiler is the proof: the smoke test
# that calls burst_to_one(&fake, ...) typechecks only if FakeHid: RawHid):
grep -n "impl RawHid for FakeHid" src/core.rs
# Expected: exactly one hit.

# Confirm the two-queue + latch shape is intact:
grep -nE "pre_write_replies|post_write_replies|written: Cell<bool>" src/core.rs
# Expected: pre_write_replies + post_write_replies (field defs + push_pre/push_post
#           + read_timeout queue select) and written: Cell<bool>.

# Confirm no dev-dependency was added (the double must be hand-rolled):
grep -nE "^\[dev-dependencies\]|mockall" Cargo.toml || echo "Cargo.toml: no dev-deps (good)"

# Confirm the capture-agnostic invariant (smoke tests do NOT assert on reply bytes):
grep -nE "reply.*\[|captured.*\[" src/core.rs | grep fakehid || \
  echo "smoke tests: no byte-value assertion on captured reply (good — capture-agnostic)"

# Confirm dead_code is clean in the test build (the proof the smoke tests
# reference every FakeHid member):
cargo test --lib 2>&1 | grep -iE "never read|never used|dead_code" || \
  echo "test build: no dead_code warnings (good)"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings (FakeHid not compiled here).
- [ ] Level 1 passed: `cargo clippy --all-targets` → zero new warnings (FakeHid linted).
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 68 passed, 0 failed.
- [ ] Level 2 passed: the 3 `fakehid_*` tests pass individually.
- [ ] Level 4 passed: `cargo test --lib` reports no dead_code warnings.

### Feature Validation

- [ ] `struct FakeHid` has exactly the 4 specified fields with correct types.
- [ ] `impl RawHid for FakeHid` matches the trait signature (return type
      `Result<usize, hidapi::HidError>`); empty queue returns `Ok(0)`.
- [ ] `impl FakeHid` provides `new()`, `push_pre(reply)`, `push_post(reply)`.
- [ ] FakeHid is drivable as `burst_to_one(&fake, &payload, batch_count, false)`.
- [ ] Smoke tests assert success / write-count / `reply.is_some()` only — never
      the captured reply byte value.
- [ ] Only `src/core.rs` is modified (inside `#[cfg(test)] mod tests`).

### Code Quality Validation

- [ ] Follows the existing `mod tests` style (`use super::*;`, descriptive snake_case).
- [ ] `timeout` param named `_timeout`; no `unused_variables` lint.
- [ ] No `#[cfg(test)]` on the struct (redundant inside the cfg-test module).
- [ ] No `#[derive(Debug)]` / dev-dependency / public visibility added.
- [ ] RawHid trait/impl and burst_to_one are UNCHANGED (consumed, not edited).
- [ ] 3 smoke tests appended at the END of `mod tests` (no disturbance to existing tests).

### Documentation & Deployment

- [ ] FakeHid has a `//` block doc comment explaining the two-queue + latch model.
- [ ] `writes` field has a `///` doc comment (it's the only non-obvious field).
- [ ] No external doc files changed (test-only code, no public surface).
- [ ] No Cargo.toml / env / config change.

---

## Anti-Patterns to Avoid

- ❌ Don't add a `[dev-dependencies]` mocking crate (mockall, etc.) — the contract
  forbids it and the design doc specifies a hand-rolled `std::cell`+`VecDeque`
  double. There is currently NO `[dev-dependencies]` section.
- ❌ Don't collapse to ONE reply queue. The two-queue + `Cell<bool>` latch shape is
  REQUIRED to support both the Issue-1 multi-report test (post_write stream) and
  the Issue-3 stale-drain test (pre_write stale data). One queue can't model both.
- ❌ Don't make the `RawHid` methods take `&mut self` — the trait (and
  `hidapi::HidDevice`) use `&self`. FakeHid MUST use interior mutability
  (`RefCell`/`Cell`) to mutate through `&self`.
- ❌ Don't assert on the captured reply's byte value in the smoke tests —
  `burst_to_one` currently captures the FIRST reply (the Issue-1 bug); T2.S2
  changes it to the LAST. Assert only `success` / `writes.len()` / `reply.is_some()`.
- ❌ Don't add `#[cfg(test)]` to the `FakeHid` struct — it's already inside the
  `#[cfg(test)] mod tests` block; a nested attr is redundant.
- ❌ Don't add `#[derive(Debug)]` — burst_to_one never prints the interface and
  default clippy doesn't require it.
- ❌ Don't modify the `RawHid` trait/impl (S1) or `burst_to_one` (S2) — consume
  them as-is. This task is purely additive inside `mod tests`.
- ❌ Don't trust the contract's line numbers — earlier subtasks (S1/S2) shifted
  them. Locate anchors by content (`grep -n "mod tests"`, `grep -n "use crate::{
  CommandResponse"`).
- ❌ Don't write the Issue-1 multi-report regression test or the Issue-3 stale
  regression test here — those are T2.S3 and T3.S2. This task delivers the double
  + its own smoke tests (which prove the double is faithful, not that the bug is
  fixed).
- ❌ Don't leave any FakeHid member unreferenced — `cargo test` will warn
  dead_code. The 3 smoke tests reference all 4 fields + all 5 methods; if you
  trim a test, re-verify every member is still exercised.
- ❌ Don't name the unused `timeout` param without the leading underscore — clippy
  `unused_variables` will fire. Use `_timeout`.
- ❌ Don't skip `cargo clippy --all-targets` (vs just `cargo clippy`) — the
  `--all-targets` form is what lints the `#[cfg(test)]` code where FakeHid lives.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a fully-specified test double (verbatim struct + both impl blocks + helpers
from the canonical design doc), placed against a unique stable anchor inside the
already-cfg-test `mod tests`, plus 3 ready-to-paste smoke tests that are
deliberately capture-agnostic (asserting only on invariants stable across the
upcoming T2.S2 capture rewrite and T3.S1 drain addition). The inputs it consumes
(`RawHid` trait, generic `burst_to_one`) are both already landed and verified in
the working tree. The two real risks — (a) dead_code in `cargo test` from
unreferenced members, and (b) coupling the smoke tests to the not-yet-fixed
capture logic — are both characterized with their mitigation (the smoke tests
reference every member AND avoid byte-value assertions). Baseline test count
(65 → 68) and all build/clippy/fmt/test commands are verified working in this repo.