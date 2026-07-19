# PRP — P1.M1.T3.S1: Add pre-send drain loop before the burst-write

---

## Goal

**Feature Goal**: Insert a **non-blocking pre-send IN-buffer drain** at the very
top of `burst_to_one` in **`src/core.rs`** — the same `read_timeout(buf, 0)`
loop bounded by `IN_DRAIN_MAX` that already exists *post-capture* (core.rs:400-415),
replicated to run *before* the burst-write loop. This flushes any stale IN-side
replies left behind by a *prior* send (whose reply arrived in the kernel IN
buffer *after* that prior send's drain loop ended — a USB-latency race), so each
send starts from an empty reply queue and the bounded capture read (Issue-1 fix)
sees only its *own* reply. Fixes **Issue 3** (PRD §7 *Send Path*, §8 *Response
Handling*, §14 invariant 4 — reply hygiene is part of correct send semantics).

**Deliverable**: A 7-line `for`-loop drain block (a `[0u8; REPORT_LENGTH + 1]`
`stale_buf` + a `0..IN_DRAIN_MAX` loop calling `interface.read_timeout(&mut
stale_buf, 0)` with `Ok(n) if n > 0 => continue, _ => break`), inserted into
`burst_to_one` **immediately after `request_data[2] = 0x9F;` and immediately
before `for batch in 0..batch_count {`**; PLUS a `burst_to_one` doc-comment
update mentioning the pre-send drain. **Function signature is UNCHANGED**
(`fn burst_to_one<T: RawHid>(&T, &[u8], usize, bool) -> (bool, Option<Vec<u8>>)`).
**No new tests** in this subtask — the stale-reply regression test is
`P1.M1.T3.S2`.

**Success Definition**: `cargo build` → zero warnings; `cargo clippy --all-targets`
→ zero new warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **70
passed, 0 failed** (the *unchanged* count — the drain is a **no-op** in every
existing test because FakeHid's `pre_write_replies` queue is empty in all 70; see
Known Gotchas). No file other than `src/core.rs` is modified. The drain block is
present and the doc comment mentions it.

## User Persona (if applicable)

**Target User**: The maintainer of the `qmk_notifier` crate and its downstream
consumer `qmkonnect` — anyone whose code acts on a per-notification
`CommandResponse` (e.g. qmkonnect's planned handshake, or the CLI in a script).

**Use Case**: A caller sends the same report 10× in a tight loop
(`send_raw_report` with no delay). Without the pre-send drain, the first few
iterations capture *stale* `[0]` replies left by the previous send's multi-report
burst, yielding nondeterministic results (`0 1 0 0 1 1 1 1 1 1` observed live).
With the drain, every iteration captures its *own* reply — deterministic for a
fixed input.

**User Journey**: `run()` → `send_raw_report` → `try_send_once` → `burst_to_one`
→ **(NEW) pre-send drain flushes stale replies** → burst-write → capture-last
reply → surplus drain → return. The user sees stable, correct results across
rapid sequential sends.

**Pain Points Addressed**: Timing-dependent `CommandResponse` (a real
correctness/reliability hazard on the single-keyboard hot path). The fix pairs
with Issue 1 (S2): capture the *correct* reply (last/ETX), AND ensure no *stale*
reply pollutes that capture.

## Why

- **Issue 3 is a Major bug** (PRD §h2.2/§h3.2): "Nondeterministic `CommandResponse`
  across rapid sequential sends (no pre-send IN-buffer drain)." The firmware
  emits one reply per report; a prior send's reply can arrive *after* that send's
  post-capture drain ended, so the *next* send's bounded read grabs the stale
  byte. The PRD's suggested fix is exactly this: "Drain the IN buffer **before**
  the burst-write in `burst_to_one` (same non-blocking `read_timeout(0)` loop
  already used post-capture)."
- **Restores the determinism invariant (PRD §14 inv 4)**: for a fixed input,
  `run()` must return the reply to *this* send, not a leftover. Reply hygiene is
  part of correct send semantics.
- **Minimal, symmetric, proven pattern**: the code to insert is byte-for-byte
  the same shape as the existing post-capture surplus drain (core.rs:400-415) —
  just relocated to the top with a `stale_buf` name. No new constants, no new
  types, no new imports, no signature change. Low blast radius.
- **Non-breaking by construction**: FakeHid's `read_timeout` serves the
  `pre_write_replies` queue while the `written` latch is `false` (i.e. before any
  `write()`). Every existing test seeds only `post_write_replies`, so the drain
  reads `Ok(0)` and breaks after one iteration — a no-op. The 70 existing tests
  stay green with zero edits (see Known Gotchas for the full proof).
- **The test seam already exists for this**: FakeHid's `push_pre()` helper +
  `pre_write_replies` queue were landed in `P1.M1.T2.S1` *specifically* to test
  this drain (FakeHid's own comment at core.rs:773-783 says `pre_write_replies`
  is "the kind a prior send leaves behind; **flushed by the Issue-3 pre-send
  drain in P1.M1.T3.S1**"). This subtask wires the production side; the next
  subtask (`P1.M1.T3.S2`) wires the regression test.

## What

### The drain block (insert verbatim)

Insert into `burst_to_one`, at the blank line between `request_data[2] = 0x9F;`
and `for batch in 0..batch_count {`:

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

This is the exact code from the item's LOGIC clause 3 and
`architecture/reply_capture_design.md` §Step 3. It mirrors the existing
post-capture surplus drain (core.rs:412-415) shape-for-shape: same `[REPORT_LENGTH + 1]`
buffer, same `0..IN_DRAIN_MAX` bound, same `read_timeout(&mut buf, 0)`
non-blocking call, same `Ok(n) if n > 0 => continue, _ => break` match arms.

### The doc-comment update (DOCS clause — Mode A, code-internal only)

`burst_to_one`'s doc comment begins `/// Burst-write \`data\` to a single device...`
(immediately above `fn burst_to_one<T: RawHid>`). Augment it so the pre-send
drain is documented:

1. **Overview sentence** — prepend the drain as the first step. Change the first
   sentence from:
   > `Burst-write \`data\` ... as \`batch_count\` back-to-back raw-HID reports,
   > then CAPTURE the last device reply ..., then drain any surplus IN-side
   > reports.`

   to:
   > `DRAIN any stale IN-side replies left by a prior send (Issue 3), then
   > burst-write \`data\` ... as \`batch_count\` back-to-back raw-HID reports,
   > then CAPTURE the last device reply ..., then drain any surplus IN-side
   > reports.`

2. **New paragraph** — add a dedicated block mirroring the existing "Reply
   capture (v0.3.1)" paragraph style. Suggested text (place it right after the
   "Reply capture (v0.3.1)" paragraph and before the "Burst-write is safe..."
   paragraph):

   ```text
   /// Pre-send drain (v0.3.1, Issue 3): BEFORE the burst-write, up to
   /// `IN_DRAIN_MAX` IN reports are read non-blocking (`read_timeout(0)`) and
   /// discarded. USB latency can deliver a *prior* send's reply into the kernel
   /// IN buffer *after* that prior send's own (post-capture) drain loop ended;
   /// without this pre-send flush, the current send's bounded capture read would
   /// grab that stale reply instead of its own, making `CommandResponse`
   /// nondeterministic across rapid sequential sends. Same loop shape as the
   /// surplus drain below; `read_timeout(0)` returns `Ok(0)` on no data (NOT an
   /// error), so the loop breaks after one `Ok(0)` read in the common
   /// no-stale-data case (cheap: one non-blocking poll).
   ```

### Success Criteria

- [ ] A pre-send drain `for`-loop (shape identical to the post-capture drain)
      exists in `burst_to_one` **between** `request_data[2] = 0x9F;` **and**
      `for batch in 0..batch_count {`.
- [ ] The loop uses `let mut stale_buf = [0u8; REPORT_LENGTH + 1];`, iterates
      `0..IN_DRAIN_MAX`, calls `interface.read_timeout(&mut stale_buf, 0)`, and
      has arms `Ok(n) if n > 0 => continue` / `_ => break`.
- [ ] A doc comment explains *why* (stale replies from a prior send + USB latency)
      — the comment text from the item's LOGIC clause 3 (or equivalent wording).
- [ ] `burst_to_one`'s `///` doc comment (above `fn burst_to_one<T: RawHid>`)
      mentions the pre-send drain (overview sentence + the new paragraph).
- [ ] `fn burst_to_one` signature is **unchanged**: `fn burst_to_one<T: RawHid>(
      interface: &T, data: &[u8], batch_count: usize, verbose: bool) ->
      (bool, Option<Vec<u8>>)`.
- [ ] `cargo build` → zero warnings; `cargo clippy --all-targets` → zero new
      warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **70 passed,
      0 failed** (the *unchanged* count).
- [ ] No new test is added (the stale-reply regression test is `P1.M1.T3.S2`).
      No file other than `src/core.rs` is touched.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim drain block
> (ready to paste), the exact placement anchor (the blank line between
> `request_data[2] = 0x9F;` at core.rs:351 and `for batch in 0..batch_count` at
> core.rs:353 — located by content grep, NOT the item's stale line numbers), the
> pattern to mirror (the existing post-capture drain at core.rs:400-415), the
> concrete doc-comment additions, the **critical** proof that the drain is a
> no-op in all 70 existing tests (so they stay green untouched), and the verified
> build/clippy/fmt/test commands are all below. The implementer needs no keyboard.

### Documentation & References

```yaml
# MUST READ — the canonical design for this exact drain (Step 3 is the verbatim code)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§Step 3 'Pre-Send Drain (Issue 3)' gives the EXACT code to insert (the
        stale_buf loop) and explains it sits BEFORE the write loop, replicating
        the post-capture drain. §Problem Statement explains WHY (firmware sends
        one reply per report; prior replies can linger). §FakeHid Test Double
        documents the pre_write_replies queue this drain consumes."
  section: "Step 3: Pre-Send Drain (Issue 3)", "Problem Statement"
  critical: "The drain code in §Step 3 is VERBATIM what to insert. It is
             byte-identical in shape to the post-capture drain — same buffer
             size, same loop bound, same match arms. Only the buffer name
             differs (stale_buf vs drain_buf). Insert at the TOP of burst_to_one,
             after request_data init, before the for-batch loop."

# MUST READ — the file being edited (placement + the pattern to mirror + doc comment)
- file: src/core.rs
  why: "(1) PLACEMENT: request_data init is at core.rs:349-351
        (`request_data[2] = 0x9F;` == line 351); the for-batch write loop is at
        core.rs:353. Insert the drain at the blank line 352 between them — anchor
        via `grep -n 'request_data\\[2\\] = 0x9F' src/core.rs`. (2) PATTERN TO
        MIRROR: the post-capture surplus drain is at core.rs:400-415
        (let mut drain_buf = [0u8; REPORT_LENGTH+1]; for _ in 0..IN_DRAIN_MAX {
        match interface.read_timeout(&mut drain_buf, 0) { Ok(n) if n > 0 =>
        continue, _ => break } }) — copy this shape, rename drain_buf→stale_buf.
        (3) DOC COMMENT: the /// block above fn burst_to_one<T: RawHid>
        (core.rs:343) starting '/// Burst-write `data` to a single device' —
        augment its overview sentence + add the Pre-send drain paragraph.
        (4) CONSTANTS: REPORT_LENGTH=32 (core.rs:38), IN_DRAIN_MAX=32
        (core.rs:136), so REPORT_LENGTH+1=33-byte buffer."
  pattern: "Non-blocking drain idiom: `let mut buf = [0u8; REPORT_LENGTH + 1];
            for _ in 0..IN_DRAIN_MAX { match interface.read_timeout(&mut buf, 0)
            { Ok(n) if n > 0 => continue, _ => break, } }` — already used twice
            in this file (post-capture drain + the capture loop's break-on-empty).
            Doc comments use /// with prose explaining the read_timeout Ok(0)
            semantics; see the 'Reply capture (v0.3.1)' paragraph for the style."
  gotcha: "The item description's line numbers (307, 315, 279) are STALE — they
           predate the S1/S2/S3 landings. The CURRENT anchors are 351/353/343.
           ALWAYS locate by content grep, never by those numbers. S3's parallel
           test appends could shift downstream lines further — anchor on the
           request_data[2]=0x9F content, not on line 352."

# MUST READ — the FakeHid contract this drain interacts with (do NOT redefine)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S1/PRP.md
  why: "Defines FakeHid's two-queue model: read_timeout pops pre_write_replies
        while the `written` Cell<bool> latch is false, and post_write_replies
        after the first write(). The pre-send drain runs BEFORE any write(), so
        it reads pre_write_replies — which is EMPTY in every existing test →
        Ok(0) → break after 1 iteration → NO-OP. This is why no existing test
        changes. FakeHid's comment at core.rs:773-783 EXPLICITLY says
        pre_write_replies is 'flushed by the Issue-3 pre-send drain in P1.M1.T3.S1'
        — the double was designed for this."
  section: "FakeHid struct + helpers (push_pre/push_post)", "Known Gotchas"
  critical: "Do NOT add a regression test here. The stale-reply test
             (push_pre(vec![0u8;33]) then assert reply[0]==1) is P1.M1.T3.S2.
             This subtask only adds production code + a doc comment. The drain
             must consume from pre_write_replies (read before write) — verify
             by tracing FakeHid::read_timeout, but you do NOT modify FakeHid."

# MUST READ — the capture-last fix this drain protects (the logic downstream)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S2/PRP.md
  why: "Defines the capture-LAST loop `for _ in 0..batch_count.max(1)` (core.rs
        388-401) that this drain feeds clean data to. The drain PREVENTS a stale
        prior-reply from being the first thing that capture loop reads. Together
        they fix Issue 1 (capture the correct/last reply) AND Issue 3 (no stale
        reply pollutes the capture). Treat S2's loop as a landed contract."
  section: "What (the capture-logic rewrite)", "Success Criteria"
  critical: "The drain goes BEFORE the write loop; the capture loop is AFTER.
             Do not reorder. The drain uses read_timeout(buf, 0) (non-blocking,
             discard); the capture loop uses read_timeout(buf,
             REPLY_READ_TIMEOUT_MS) (blocking, retain). Different timeouts,
             different intents — keep them separate."

# REFERENCE — the bug this fixes + the live proof
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 3 documents the live nondeterminism ('1-report \"hello\" x10:
        0 1 0 0 1 1 1 1 1 1' and '2-report x10: 0 1 0 1 0 1 1 1 1 1') and states
        the Suggested Fix verbatim: 'Drain the IN buffer before the burst-write
        in burst_to_one (same non-blocking read_timeout(0) loop already used
        post-capture), so each send starts from an empty reply queue.'"
  section: "Major Issues / Issue 3 (Expected/Actual/Suggested Fix)"

# REFERENCE — PRD invariants upheld
- file: PRD.md
  why: "§7 Send Path, §8 Response Handling, §14 invariant 4 (cache invalidated on
        write failure — reply hygiene is part of correct send semantics). §4.4
        (firmware sends one 32-byte reply per report — the source of the stale
        replies)."
  section: "4.4 Replies are received", "7 Send Path", "8 Response Handling",
           "14 Key Invariants (4)"

# REFERENCE — research notes (placement, proof, validation expectations)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T3S1/research/notes.md
  why: "Documents the verified working-tree state (POST-S3, 70 tests pass), the
        exact placement anchor (by content grep), the no-op proof for existing
        tests (FakeHid pre_write queue empty → Ok(0) → break), the verbatim
        drain code, the doc-comment edit plan, and the validation expectations."
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
    └── core.rs         # <-- FILE TO EDIT (production code + doc comment only):
                         #     RawHid trait+impl (13-30) — CONSUMED
                         #     burst_to_one<T: RawHid> (343) — EDITED:
                         #       doc comment (above 343) — ADD pre-send mention
                         #       request_data init (349-351) — anchor
                         #       >>> INSERT pre-send drain block HERE (line 352) <<<
                         #       for-batch write loop (353) — untouched
                         #       capture-last loop (388-401) — untouched (S2)
                         #       surplus drain (400-415) — PATTERN TO MIRROR
                         #     batches_for (424-426) — CONSUMED
                         #     #[cfg(test)] mod tests (763+):
                         #       FakeHid + helpers (785-836) — CONSUMED, UNTOUCHED
                         #       70 tests — ALL UNTOUCHED (drain is a no-op for them)
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── core.rs   # MODIFIED ONLY:
              #   (1) burst_to_one: insert pre-send drain block (7 lines) after
              #       request_data[2] = 0x9F; / before for batch in 0..batch_count.
              #   (2) burst_to_one doc comment: mention the pre-send drain
              #       (overview sentence + new paragraph).
              #   No new files, no new deps, no new types, no new constants,
              #   no signature change, no new tests, no FakeHid change.
```

> No new files. The only diff is inside `burst_to_one`: one drain block inserted
> + the doc comment augmented.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL — THE DRAIN IS A NO-OP IN EVERY EXISTING TEST (do not "fix" tests).
//   FakeHid::read_timeout (core.rs:793-814) selects its queue by self.written.get():
//     false → pop pre_write_replies ; true → pop post_write_replies.
//   The pre-send drain runs BEFORE the for-batch write loop → BEFORE any write()
//   → written is still false → it reads pre_write_replies.
//   ALL 70 existing tests seed ONLY post_write_replies (via push_post); NONE call
//   push_pre. So pre_write_replies is EMPTY → pop_front() = None → Ok(0) →
//   matches `_ => break` → loop runs ONE iteration and exits.
//   => The drain consumes nothing from post_write_replies (the capture tests'
//      queue — untouched because written is still false during the drain). The
//      70 tests stay GREEN with zero edits. If a test fails after you insert the
//      drain, you have a BUG in the drain (e.g. you used the post_write queue,
//      or you moved it after the write loop) — do NOT weaken the test.

// CRITICAL — LOCATE THE PLACEMENT ANCHOR BY CONTENT GREP, NOT LINE NUMBERS.
//   The item description cites core.rs:307 (request_data), :315 (for-batch),
//   :279 (doc comment). THESE ARE STALE — they predate the S1/S2/S3 landings.
//   The CURRENT anchors: request_data[2] = 0x9F; at core.rs:351; for batch in
//   0..batch_count at core.rs:353; fn burst_to_one<T: RawHid> at core.rs:343.
//   And S3's parallel test appends may shift downstream lines further. So:
//       grep -n "request_data\[2\] = 0x9F" src/core.rs   # insert AFTER this line
//       grep -n "for batch in 0..batch_count" src/core.rs # insert BEFORE this line
//   The blank line between them (currently 352) is the insertion point.

// CRITICAL — USE read_timeout(buf, 0), NOT read_timeout(buf, REPLY_READ_TIMEOUT_MS).
//   The pre-send drain is NON-BLOCKING (timeout=0): it polls and discards stale
//   data instantly, breaking on Ok(0)/Err. A blocking read here would HANG for
//   up to 1s on every send when there's no stale data — unacceptable on the hot
//   path. The capture loop below (core.rs:388) is the one that blocks
//   (REPLY_READ_TIMEOUT_MS); the drain must NOT. Mirror the post-capture drain
//   (core.rs:412), which uses timeout=0.

// CRITICAL — DO NOT ADD A REGRESSION TEST. The stale-reply test — which seeds
//   push_pre(vec![0u8;33]) (stale reply) + push_post({[0]=1}) (fresh reply), then
//   asserts reply[0]==1 (the fresh one, not the stale [0]) — is the deliverable of
//   P1.M1.T3.S2 (separate PRP). This subtask adds ONLY production code + a doc
//   comment. (The design doc's "Test: Pre-Send Drain (Issue 3 regression)" block
//   is the S2 sketch; it uses struct-literal FakeHid which S1 replaced with
//   push_pre() — S2's PRP reconciles that. Do not copy it here.)

// CRITICAL — BUFFER NAME IS stale_buf, NOT drain_buf. The existing post-capture
//   drain already owns the name `drain_buf` (core.rs:412). Use `stale_buf` for
//   the pre-send drain — it's semantically accurate (stale IN-side replies, not
//   surplus post-capture replies) and avoids shadowing confusion. Both the item
//   description and architecture/reply_capture_design.md §Step 3 specify
//   `stale_buf`. clippy/rustfmt treat them identically (both `let mut` + `&mut`
//   pass to read_timeout → no "unused" / "unnecessary_mut_passed" lint, same as
//   the existing drain_buf).

// CRITICAL — DO NOT TOUCH THE FUNCTION SIGNATURE. The contract (OUTPUT clause) is
//   explicit: `fn burst_to_one<T: RawHid>(interface: &T, data: &[u8], batch_count:
//   usize, verbose: bool) -> (bool, Option<Vec<u8>>)` is unchanged. The drain is
//   an internal statement block; it adds no parameters and changes no return type.
//   try_send_once (the caller) and FakeHid are untouched.

// NOTE — no new imports, no new constants, no new types. REPORT_LENGTH (core.rs:38)
//   and IN_DRAIN_MAX (core.rs:136) already exist and are in scope inside
//   burst_to_one. `interface` (the `&T: RawHid`) already exposes read_timeout.
//   The match arm `Ok(n) if n > 0 => continue, _ => break` is the file's
//   established idiom (used at core.rs:414 and implicitly at 389-401).

// NOTE — the match arm ORDER matters: `Ok(n) if n > 0 => continue` MUST come
//   before `_ => break`. `Ok(0)` (timeout/no-data) and `Err(_)` (read failure)
//   both fall through to `_ => break`. This is identical to the post-capture
//   drain — do not add a separate `Ok(0) => break` arm (clippy would flag
//   unreachable/duplicate patterns; the `_` arm already covers it).

// NOTE — on a REAL hidapi::HidDevice, read_timeout(buf, 0) with no pending data
//   returns Ok(0) quickly (non-blocking poll). So in the common no-stale-data
//   case the pre-send drain costs ONE non-blocking syscall then breaks. Cheap.
//   (The post-capture drain already does the same every send.)
```

## Implementation Blueprint

### Data models and structure

No new types, no new helpers, no new imports, no new constants. The drain is a
self-contained statement block operating on a stack-allocated 33-byte buffer and
the already-generic `interface: &T` (`T: RawHid`). It reads-and-discards up to
`IN_DRAIN_MAX` stale IN-side reports before the write loop begins.

```rust
// Data flowing through the pre-send drain:
//   stale_buf: [u8; REPORT_LENGTH + 1]   // 33-byte scratch (stack)
//   interface: &T where T: RawHid         // already in scope (the fn param)
//   loop: 0..IN_DRAIN_MAX (32)            // bounded so a flooding IN endpoint can't wedge us
//   per-iter: read_timeout(&mut stale_buf, 0)  // Ok(n>0)=discard&continue, Ok(0)/Err=break
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/core.rs and confirm the placement anchor + the pattern to mirror
  - CONFIRM the working tree is POST-S2 (capture-last loop landed):
          `grep -n "for _ in 0..batch_count.max(1)" src/core.rs` -> one hit inside
          burst_to_one. If ABSENT, STOP: prerequisite S2 missing.
  - CONFIRM the current placement anchors by CONTENT (NOT the item's stale 307/315):
          `grep -n "request_data\[2\] = 0x9F" src/core.rs`        -> insert AFTER this
          `grep -n "for batch in 0..batch_count" src/core.rs`     -> insert BEFORE this
          (Currently core.rs:351 and 353; the blank line 352 is the slot.)
  - READ the existing post-capture surplus drain (core.rs:400-415) to internalize
          the EXACT pattern to mirror (buffer size, loop bound, match arms, the
          comment style explaining read_timeout Ok(0) semantics).
  - READ burst_to_one's doc comment (the /// block above fn burst_to_one<T: RawHid>
          at core.rs:343, starting "/// Burst-write `data` to a single device") to
          find where to add the pre-send-drain mention.
  - GOAL: know the exact insertion slot, the pattern to copy, and the doc anchor.

Task 2: INSERT the pre-send drain block into burst_to_one
  - INSERT (at the blank line between request_data[2] = 0x9F; and
          for batch in 0..batch_count {): the verbatim drain block from the
          "What" section (the 2-line // comment + let mut stale_buf + for loop +
          match with Ok(n) if n>0 => continue / _ => break).
  - VERIFY shape == the post-capture drain (core.rs:412-415): same buffer size
          ([0u8; REPORT_LENGTH + 1]), same bound (0..IN_DRAIN_MAX), same
          read_timeout(buf, 0) non-blocking call, same match arms. Only the
          buffer NAME differs (stale_buf vs drain_buf).
  - DO NOT: change the function signature; use read_timeout(buf,
            REPLY_READ_TIMEOUT_MS) (must be 0, non-blocking); move it after the
            write loop; add a separate Ok(0) => break arm; add any test.

Task 3: UPDATE the burst_to_one doc comment (DOCS clause, Mode A)
  - AUGMENT the overview sentence to list "DRAIN any stale IN-side replies left
          by a prior send (Issue 3)" as the FIRST step (see "What" §doc-comment
          update for the exact wording).
  - ADD a dedicated "Pre-send drain (v0.3.1, Issue 3)" paragraph (the suggested
          text in "What") explaining WHY (USB latency delivers a prior send's
          reply after that send's drain ended; without the pre-send flush the
          current send's bounded read grabs the stale reply; same loop shape as
          the surplus drain; Ok(0) => one cheap non-blocking poll in the common
          case). Place it after the "Reply capture (v0.3.1)" paragraph and
          before the "Burst-write is safe..." paragraph.
  - KEEP the existing doc-comment paragraphs (reply-capture, burst-write-safety,
          HID-I/O note) intact — this is an ADDITION, not a rewrite.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --all-targets`,
          then `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 new warnings; fmt --check exit 0;
          test result "70 passed; 0 failed" (UNCHANGED — no new test; the drain
          is a no-op for all existing tests per the Known Gotchas proof).
  - RUN a targeted sanity check that the drain is wired correctly without a test:
          `grep -n "let mut stale_buf" src/core.rs` -> exactly ONE hit inside
          burst_to_one. `grep -n "Drain any stale IN-side replies" src/core.rs`
          -> exactly ONE hit (the drain comment). `grep -n "Pre-send drain"
          src/core.rs` -> exactly ONE hit (the doc-comment paragraph).
  - IF cargo test --lib shows < 70: a test broke — re-check the drain is BEFORE
          the write loop and uses timeout=0 (a blocking read or wrong placement
          would disturb FakeHid's written latch timing). Do NOT edit the test.
  - IF "unused variable: stale_buf" / "unnecessary_mut_passed" from clippy: you
          likely named it differently or passed it wrong — it must mirror the
          existing `let mut drain_buf = [0u8; REPORT_LENGTH+1];` + `&mut buf`
          exactly.
```

### Implementation Patterns & Key Details

```rust
// === THE PATTERN TO MIRROR (existing post-capture surplus drain, core.rs:412-415) ===
//
//   let mut drain_buf = [0u8; REPORT_LENGTH + 1];
//   for _ in 0..IN_DRAIN_MAX {
//       match interface.read_timeout(&mut drain_buf, 0) {
//           Ok(n) if n > 0 => continue,
//           _ => break,
//       }
//   }
//
// The pre-send drain is IDENTICAL except: (a) buffer name `stale_buf`, (b)
// placement at the TOP (before the write loop), (c) a // comment explaining
// the Issue-3 rationale. Copy the shape verbatim.


// === WHY read_timeout(buf, 0) AND NOT read_timeout(buf, REPLY_READ_TIMEOUT_MS) ===
//
//   timeout=0 ⇒ NON-BLOCKING. hidapi returns Ok(0) immediately if no data is
//   pending (poll). This is what we want for a drain: flush whatever stale data
//   is there right now, then bail. A blocking read (timeout=REPLY_READ_TIMEOUT_MS
//   = 1000ms) would WAIT up to a second on every send when the buffer is empty
//   — an unacceptable stall on the single-keyboard hot path. The capture loop
//   below (core.rs:388) is the one that blocks; the drain must not.


// === THE NO-OP PROOF FOR EXISTING TESTS (why 70 stays 70) ===
//
//   Every existing test builds a FakeHid::new() and calls push_post(...) only.
//   FakeHid::read_timeout pops pre_write_replies while written is false. The
//   pre-send drain calls read_timeout BEFORE any write() ⇒ written is false ⇒
//   it pops pre_write_replies (EMPTY) ⇒ Ok(0) ⇒ `_ => break` ⇒ ONE iteration.
//   post_write_replies (the capture tests' queue) is never touched during the
//   drain. ⇒ The drain is invisible to all 70 tests. (The stale-reply test that
//   DOES seed push_pre is P1.M1.T3.S2, a separate subtask.)


// === WHY A SEPARATE stale_buf (not reusing request_data or read_buf) ===
//
//   request_data (core.rs:349) holds the OUT payload being built; read_buf
//   (core.rs:387) holds the captured reply being retained. Neither is a free
//   scratch buffer for discarded stale data — reusing either would couple the
//   drain to another variable's lifetime and risk subtle bugs. A dedicated
//   `stale_buf` on the stack is 33 bytes, mirrors drain_buf, and is self-
//   documenting. (clippy: `let mut` + `&mut` pass to read_timeout is a use; no
//   "unused" warning — identical to the existing drain_buf.)


// === PLACEMENT: BEFORE THE WRITE LOOP, AFTER request_data INIT ===
//
//   The drain must run BEFORE the first interface.write() so it reads from the
//   pre-write state of the IN buffer (stale data from a PRIOR send). If it ran
//   after the write loop, it would compete with the capture loop for the fresh
//   replies of THIS send. The item's LOGIC clause 3 and design doc §Step 3 are
//   explicit: insert after request_data init, before the for-batch loop.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - add:    "pre-send drain block (7 lines) inside burst_to_one, after
             request_data[2] = 0x9F; / before for batch in 0..batch_count."
  - modify: "burst_to_one doc comment (overview sentence + new paragraph)."

NO OTHER CHANGES:
  - signature: "fn burst_to_one<T: RawHid>(&T,&[u8],usize,bool) -> (bool,Option<Vec<u8>>)
                UNCHANGED."
  - imports:   "none — REPORT_LENGTH, IN_DRAIN_MAX, interface.read_timeout all in scope."
  - constants: "none — REPORT_LENGTH (32) and IN_DRAIN_MAX (32) already exist."
  - types:     "none — no new struct/trait/enum."
  - tests:     "NONE added — the stale-reply regression test is P1.M1.T3.S2."
  - deps:      "none — no Cargo.toml change."

CONSUMED (do NOT modify — treat as contracts):
  - P1.M1.T1.S1: "pub(crate) trait RawHid + impl for hidapi::HidDevice (core.rs:13-30)."
  - P1.M1.T1.S2: "fn burst_to_one<T: RawHid> (core.rs:343) — generic signature."
  - P1.M1.T2.S1: "struct FakeHid + impl RawHid + impl FakeHid{new,push_pre,push_post}
                  (core.rs:785-836) — the test seam; pre_write_replies is the queue
                  this drain reads (empty in all existing tests ⇒ no-op)."
  - P1.M1.T2.S2: "the capture-LAST loop `for _ in 0..batch_count.max(1)` (core.rs
                  388-401) — the downstream logic this drain feeds clean data to."
  - P1.M1.T2.S3: "the two capture regression tests — MUST stay green untouched."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified, and ONLY inside burst_to_one (drain block) +
    its doc comment. Do NOT touch FakeHid / impl RawHid / the capture loop / the
    surplus drain / batches_for / RawHid / any test / lib.rs / error.rs / main.rs
    / Cargo.toml. Do NOT add the Issue-3 stale-reply test (P1.M1.T3.S2).
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
# (Confirms the drain block is valid Rust and the borrow/lifetime are fine.)
cargo build 2>&1 | tee /tmp/s1_build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.

# Lint ALL targets (catches any pattern lint in the new code + re-checks tests).
cargo clippy --all-targets 2>&1 | tee /tmp/s1_clippy.log
# Expected: no warnings/errors attributable to the new drain block.
# If "unnecessary_mut_passed" or "unused variable stale_buf": you diverged from
#   the existing drain_buf pattern — match it exactly (let mut + &mut pass).

# Formatting gate.
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full lib test suite (core.rs #[cfg(test)] mod tests + lib.rs unit tests).
# This is the PRIMARY gate: the drain must NOT break any existing test.
cargo test --lib
# Expected: "test result: ok. 70 passed; 0 failed; 0 ignored; ..." (UNCHANGED).
# If < 70: a test broke — re-check the drain is BEFORE the write loop and uses
#   timeout=0. Do NOT edit the test (the drain is supposed to be a no-op for it).

# Targeted re-runs to confirm the FakeHid-based tests (S1/S3) are unaffected:
cargo test --lib fakehid_ -- --nocapture                                       # 3 passed
cargo test --lib burst_to_one_captures_last_reply_for_multi_report -- --nocapture  # 1 passed
cargo test --lib burst_to_one_single_report_unchanged -- --nocapture            # 1 passed
# All green: the pre-send drain reads the EMPTY pre_write queue → Ok(0) → break,
# never touching the post_write replies these tests assert on.

# Confirm the pre-existing pure-function tests are untouched:
cargo test --lib batches_for_ -- --nocapture    # unchanged
cargo test --lib parse_reply_ -- --nocapture     # unchanged (if present)
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask as a separate step.
There is no new external surface — the drain is an internal statement block. The
Level 2 `cargo test --lib` (70 green) IS the integration validation: it proves
the drain composes correctly with the capture-last loop and the FakeHid double.

LIVE HARDWARE (optional, NOT required for the CI gate):
  The fix is observable end-to-end on a QMK keyboard with qmk-notifier firmware.
  If one is connected, reproduce the Issue-3 nondeterminism probe from the PRD:
    cargo build --release
    # Send the same 1-report payload 10x with no delay; BEFORE this fix the first
    # few reply[0] values are nondeterministic (0 1 0 0 1 ...). AFTER this fix
    # every reply[0] is the same (the device's real reply to this send).
  (Use a small test harness around send_raw_report; the CLI alone doesn't loop.)
  No keyboard? The fact that the 70 unit tests stay green is the CI equivalent —
  and the stale-reply REGRESSION test (P1.M1.T3.S2) will lock this behavior in.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the drain block exists exactly once, inside burst_to_one:
grep -n "let mut stale_buf = \[0u8; REPORT_LENGTH + 1\]" src/core.rs
# Expected: exactly ONE hit.

# Confirm the drain's // comment (the Issue-3 rationale) is present:
grep -n "Drain any stale IN-side replies left by a prior send (Issue 3)" src/core.rs
# Expected: exactly ONE hit.

# Confirm the drain sits BEFORE the write loop and AFTER request_data init:
grep -nE "request_data\[2\] = 0x9F|let mut stale_buf|for batch in 0..batch_count" src/core.rs
# Expected: the three hits appear IN THIS ORDER (request_data, stale_buf, for-batch).

# Confirm read_timeout uses timeout=0 (non-blocking) in the drain, NOT the
# blocking REPLY_READ_TIMEOUT_MS:
# (Manual: read the drain block — the read_timeout call's 2nd arg must be `0`.)

# Confirm the doc comment now mentions the pre-send drain:
grep -n "Pre-send drain" src/core.rs
# Expected: exactly ONE hit (the new doc paragraph). Plus the overview sentence
# should now start with "DRAIN any stale IN-side replies".

# Confirm the function signature is UNCHANGED:
grep -n "fn burst_to_one<T: RawHid>" src/core.rs
# Expected: one hit, signature identical to before (no new params/return type).

# Confirm NO new test was added (S2 owns the regression test):
grep -cE "^\s*#\[test\]" src/core.rs
# Expected: 44 (unchanged). If 45, you accidentally added the S2 test — remove it.

# Confirm no stray duplicate of the drain / no accidental edit to the surplus drain:
grep -nE "let mut (stale_buf|drain_buf)" src/core.rs
# Expected: TWO hits — stale_buf (pre-send) and drain_buf (post-capture). Both present.

# Confirm the test count end-to-end:
cargo test --lib 2>&1 | grep "test result"
# Expected: "70 passed; 0 failed".
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings.
- [ ] Level 1 passed: `cargo clippy --all-targets` → zero new warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → **70 passed, 0 failed** (unchanged).

### Feature Validation

- [ ] A pre-send drain `for`-loop exists in `burst_to_one` between
      `request_data[2] = 0x9F;` and `for batch in 0..batch_count {`.
- [ ] The loop uses `let mut stale_buf = [0u8; REPORT_LENGTH + 1];`, iterates
      `0..IN_DRAIN_MAX`, calls `interface.read_timeout(&mut stale_buf, 0)`
      (timeout **0**, non-blocking), with arms `Ok(n) if n > 0 => continue` /
      `_ => break`.
- [ ] The drain's `//` comment explains the Issue-3 rationale (stale replies from
      a prior send + USB latency).
- [ ] The `burst_to_one` `///` doc comment (above `fn burst_to_one<T: RawHid>`)
      mentions the pre-send drain (overview sentence lists it first + a new
      "Pre-send drain (v0.3.1, Issue 3)" paragraph).
- [ ] `fn burst_to_one` signature is **unchanged**.
- [ ] No new test added; no file other than `src/core.rs` modified.

### Code Quality Validation

- [ ] Drain block shape mirrors the existing post-capture surplus drain
      (core.rs:412-415) exactly — only the buffer name (`stale_buf`) differs.
- [ ] Drain placement is BEFORE the write loop, AFTER `request_data` init
      (anchored by content grep, not the item's stale line numbers).
- [ ] No new imports, constants, types, or dependencies.
- [ ] `FakeHid` / `impl RawHid` / the capture loop / the surplus drain /
      `batches_for` / `RawHid` / all 70 tests are UNCHANGED.

### Documentation & Deployment

- [ ] The drain has a `//` comment explaining *why* (Issue 3, USB-latency race).
- [ ] The `burst_to_one` doc comment mentions the pre-send drain.
- [ ] No external doc files changed (production-code + code-internal doc only;
      README/architecture sync is `P1.M3`).
- [ ] No Cargo.toml / env / config change.

---

## Anti-Patterns to Avoid

- ❌ Don't use `read_timeout(buf, REPLY_READ_TIMEOUT_MS)` (blocking) in the drain.
  It MUST be `read_timeout(buf, 0)` (non-blocking). A blocking read would stall
  up to 1s per send when the buffer is empty — the drain is a flush, not a wait.
  Mirror the post-capture drain (core.rs:412), which uses timeout=0.
- ❌ Don't place the drain AFTER the write loop. It must run BEFORE the first
  `interface.write()` so it reads stale data from a PRIOR send (pre-write state
  of the IN buffer). Placing it after would make it compete with the capture
  loop for THIS send's fresh replies.
- ❌ Don't add a regression test. The stale-reply test
  (`push_pre(vec![0u8;33])` + `push_post({[0]=1})`, assert `reply[0]==1`) is the
  deliverable of `P1.M1.T3.S2`. This subtask adds ONLY production code + a doc
  comment.
- ❌ Don't reuse `request_data`, `read_buf`, or `drain_buf` as the drain's
  scratch buffer. Use a dedicated `stale_buf` — it's self-documenting and avoids
  coupling the drain to another variable's lifetime. It mirrors `drain_buf`'
s shape so clippy/rustfmt are happy.
- ❌ Don't add a separate `Ok(0) => break` match arm. The `_ => break` arm already
  covers `Ok(0)` (timeout/no-data) and `Err(_)` (read failure). Adding a
  redundant arm trips clippy's unreachable/duplicate-pattern lint. Match the
  existing drain's two-arm shape (`Ok(n) if n > 0 => continue, _ => break`).
- ❌ Don't change the `burst_to_one` signature. The drain is an internal
  statement block — no new parameters, no return-type change. `try_send_once`
  and `FakeHid` are untouched.
- ❌ Don't trust the item description's line numbers (`core.rs:307`, `:315`,
  `:279`). They are STALE (predate the S1/S2/S3 landings). The current anchors
  are 351 (request_data[2]=0x9F), 353 (for-batch), 343 (fn). Locate by content
  grep: `grep -n "request_data\[2\] = 0x9F" src/core.rs`.
- ❌ Don't edit FakeHid, `impl RawHid`, the capture-last loop, the surplus drain,
  `batches_for`, `RawHid`, any test, `lib.rs`, `error.rs`, `main.rs`, or
  `Cargo.toml`. This subtask touches ONLY `burst_to_one`'s body + its doc comment.
- ❌ Don't "fix" a test that fails after inserting the drain. If a test breaks,
  the drain is mis-placed or blocking — fix the DRAIN (placement / timeout=0),
  never the test. The drain is a no-op for all 70 existing tests by construction.
- ❌ Don't skip `cargo clippy --all-targets` (vs just `cargo clippy`). The
  `--all-targets` form lints the `#[cfg(test)]` code too, confirming the drain
  composes correctly with the test double's trait impl.
- ❌ Don't rewrite the existing doc-comment paragraphs. The doc update is
  ADDITIVE: augment the overview sentence + append one new paragraph. Keep the
  reply-capture, burst-write-safety, and HID-I/O-note paragraphs intact.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a 7-line drain block whose code is given verbatim in two independent sources
(the item's LOGIC clause 3 and `architecture/reply_capture_design.md` §Step 3),
which is byte-for-byte the same shape as an existing block already in the file
(the post-capture surplus drain, core.rs:412-415). The placement is pinned by a
content grep (the blank line between `request_data[2] = 0x9F;` and
`for batch in 0..batch_count`), the function signature is explicitly unchanged,
no new test is required (the drain is provably a no-op for all 70 existing tests
because FakeHid's `pre_write_replies` queue is empty in every one of them, so it
reads `Ok(0)` and breaks after one iteration), and the validation commands are
verified against the working tree (`cargo test --lib` currently reports 70
passed). The one subtlety — that the item's line numbers are stale and must be
re-resolved by content grep — is fully called out. The doc-comment addition is
suggested concretely. An implementer who follows the blueprint will produce a
diff that is exactly: one drain block + one doc paragraph + one augmented
overview sentence, with 70 tests still green.