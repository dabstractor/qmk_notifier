# PRP — P1.M1.T2.S2: Rewrite capture logic to keep last reply (ETX-report reply)

---

## Goal

**Feature Goal**: Fix **Issue 1** (critical) — `burst_to_one` currently captures
the **FIRST** device reply after a burst-write, but the firmware emits **one
32-byte reply per report** (PRD §4.4), so for any multi-report payload
(> 30 bytes ⇒ ≥ 2 reports) the first reply is an *intermediate* `[0]` reply and
the real result (the legacy match-bool or a typed `0x51…` reply) is on the
**LAST** reply — the ETX report — which is currently *drained and discarded*.
The fix replaces the single bounded `read_timeout` with a loop that reads up to
`batch_count` replies and **overwrites `reply` each iteration so the last
non-empty reply wins**. This makes `run()` return the correct `CommandResponse`
for the dominant qmkonnect notification shape (real window strings routinely
exceed 30 bytes ⇒ 2 reports) and for multi-report typed commands (e.g.
`APPLY_HOST_CONTEXT` with a full 32-callback set ⇒ 2 reports).

**Deliverable**: A single logic edit + doc-comment syncs in **`src/core.rs` only**:
1. Replace the single-`match` capture block in `burst_to_one` (the body of the
   generic `fn burst_to_one<T: RawHid>`, ~core.rs:370-385) with a
   `for _ in 0..batch_count.max(1) { … overwrite reply … }` loop. **KEEP** the
   existing non-blocking surplus drain (~core.rs:400-406) as a safety net.
2. Update `burst_to_one`'s `///` doc comment (~core.rs:307-324): "CAPTURE the
   **first**" → "CAPTURE the **last** (ETX-report reply)".
3. Update `send_raw_report`'s `///` doc comment (~core.rs:155-165) where it says
   "the FIRST device reply captured" / "that first reply's raw IN report".
4. (Optional consistency) Update the `REPLY_READ_TIMEOUT_MS` `///` doc
   (~core.rs:68-73) which says "first-reply capture".

**Success Definition**: `cargo build` compiles with **zero warnings**;
`cargo clippy --all-targets` → zero new warnings; `cargo fmt --check` → exit 0;
`cargo test --lib` → **68 passed, 0 failed** (baseline is 68 with S1's FakeHid
landed; S2 adds NO tests — the Issue-1 regression test is S3's job). `burst_to_one`'s
signature is **unchanged** (`fn burst_to_one<T: RawHid>(&T, &[u8], usize, bool) ->
(bool, Option<Vec<u8>>)`). The 3 S1 `fakehid_*` smoke tests still pass (they are
capture-agnostic). No file other than `src/core.rs` is modified.

## User Persona (if applicable)

**Target User**: The daemon `qmkonnect` and the crate's CLI — i.e. anyone who
reads the `CommandResponse` returned by `run()` for a multi-report message.

**Use Case**: Send a real window string `"code.Code — Bug Report — qmk_notifier"`
(~44 bytes ⇒ 2 reports) and get back `Legacy { matched: true }` (the ETX reply's
match-bool), not `Legacy { matched: false }` (the intermediate `[0]` reply that
the current capture-first logic returns).

**User Journey**: `run(SendMessage(long_string))` → `send_raw_report` →
`try_send_once` → `burst_to_one` (burst-write 2 reports) → **capture loop reads
reply 1 (`[0]`), then reply 2 (`[1]`), keeps reply 2** → returns
`Some([1,0,0,…])` → `parse_reply` → `CommandResponse::Legacy { matched: true }`.

**Pain Points Addressed**: Today every multi-report message returns the wrong
result (intermediate `[0]` reply). This silently masks typed devices as
non-capable (their `0x51…` reply is on the ETX report, discarded) and prints
`Legacy { matched: false }` for matching 30+ byte strings. PRD §14 invariant 8
("firmware wins on disagreement") mandates the crate match the firmware's
one-reply-per-report model.

## Why

- **Correctness contract** (PRD §8, §10.2, §14 invariant 6): `response[0]` is the
  legacy match-bool, computed by the firmware **only at ETX**. Returning any
  other report's `[0]` byte is a wrong answer, not a degraded one.
- **Firmware-wins drift** (PRD §14 invariant 8): the crate assumed one reply per
  *burst*; the firmware emits one reply per *report* (`hid_notify()` runs once
  per 32-byte report and `raw_hid_send`s at the end of every call — proven live
  in `TEST_RESULTS.md`: 2-report send ⇒ replies `[0, 1]`; 4-report ⇒ `[0,0,0,1]`).
  The crate must follow the firmware.
- **Unblocks the typed-command capability handshake**: a typed `0x51` reply is
  emitted only on the ETX report. With capture-first, a multi-report
  `APPLY_HOST_CONTEXT` (e.g. 32 callbacks ⇒ 2 reports) has its `[0x51][0x05][ack]`
  discarded and returns `Legacy { matched: false }`, masking the device as
  non-capable. Capture-last fixes this for free.
- **Minimal, surgical, testable**: the rewrite is one block (single `match` →
  bounded `for`) inside an already-generic, already-test-via-`FakeHid` function.
  It does not touch the public signature, the trait, the cache, the drain, or
  any pure function. S3 will regression-lock it; this task delivers the fix.

## What

### 1. The capture-logic rewrite (the core edit)

In the body of `fn burst_to_one<T: RawHid>` (core.rs:337), find the block that
begins with the comment `// Capture the FIRST device reply with a bounded timeout`
and replace **the comment + the single `match`** (keeping `let mut reply` and
`let mut read_buf`) with the loop. Verbatim **old → new**:

**OLD** (current working-tree, ~core.rs:370-385):
```rust
    // Capture the FIRST device reply with a bounded timeout (v0.3.0). Unlike the
    // drain below (non-blocking, discards surplus), this read WAITS up to
    // REPLY_READ_TIMEOUT_MS for the first reply so the host can parse the typed
    // response. read_timeout returns Ok(0) on timeout/no-data (NOT an error) and
    // Ok(n>0) when data was read; only n>0 counts as a captured reply.
    // (external_deps.md §read_timeout semantics.)
    let mut reply: Option<Vec<u8>> = None;
    let mut read_buf = [0u8; REPORT_LENGTH + 1];
    match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
        Ok(n) if n > 0 => {
            reply = Some(read_buf[..n].to_vec());
            if verbose {
                println!("Captured device reply: {} bytes", n);
            }
        }
        _ => {} // Ok(0) = timeout, Err = read failure ⇒ reply stays None
    }
```

**NEW**:
```rust
    // Capture the LAST device reply with a bounded timeout (v0.3.1 fix for
    // Issue 1). The firmware emits one 32-byte reply per report processed (PRD
    // §4.4); for a multi-report message only the LAST reply — the ETX report —
    // carries the real result (legacy match-bool, or a typed 0x51 reply). So we
    // read up to batch_count replies and OVERWRITE `reply` each iteration,
    // keeping the last non-empty one. Unlike the drain below (non-blocking,
    // discards surplus), each read here WAITS up to REPLY_READ_TIMEOUT_MS for a
    // real reply. read_timeout returns Ok(0) on timeout/no-data (NOT an error)
    // and Ok(n>0) when data was read; a timeout/error breaks the loop early.
    // (PRD §4.4, §8; external_deps.md §read_timeout semantics.)
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
            _ => break, // Ok(0) = timeout, Err = read failure ⇒ stop (no more replies)
        }
    }
```

> `batch_count.max(1)` guards `batch_count == 0` (defensive; `run()`/`send_raw_report`
> always supply ≥1 payload byte, but `.max(1)` matches the canonical design doc and
> keeps the loop body honest). `read_buf` reuse across iterations is safe because
> `read_buf[..n].to_vec()` clones the prefix into an owned `Vec` each capture.

### 2. KEEP the surplus drain unchanged

The non-blocking drain loop immediately after the capture block (~core.rs:400-406)
stays **byte-for-byte identical** — it is the safety net for any straggler replies
beyond `batch_count` (e.g. a device that echoes, or a slow extra reply). Do NOT
edit, remove, or merge it. Its return `(true, reply)` is unchanged.

### 3. Doc-comment syncs (Mode A — in-file only)

Update every "first reply" reference in the two function doc comments (locate by
`grep -n "first" src/core.rs` within the doc blocks; do NOT touch unrelated
comments like the drain's "v0.2.x discards…" historical note).

**(a) `burst_to_one` doc comment** (~core.rs:307-324):

- Summary line — `/// reports, then CAPTURE the first device reply (bounded wait), then drain any`
  → `/// reports, then CAPTURE the last device reply (the ETX-report reply, which`
  (continue on the next line: `/// carries the real result), then drain any`)
- `# Return`-ish paragraph — `/// `Option<Vec<u8>>` is the FIRST captured IN report, decoded downstream by`
  → `/// `Option<Vec<u8>>` is the LAST captured IN report (the ETX-report reply),`
- Reply-capture paragraph — replace the whole
  `/// Reply capture (v0.3.0): after the burst-write succeeds, the FIRST IN report is …`
  block with:

```rust
/// Reply capture (v0.3.1): after the burst-write succeeds, up to `batch_count`
/// IN reports are read with a bounded `read_timeout(REPLY_READ_TIMEOUT_MS)` each,
/// and the LAST non-empty reply is retained. The firmware emits one 32-byte reply
/// per report processed (PRD §4.4); for a multi-report message only the final
/// reply — the ETX report — carries the real result (legacy match-bool or a
/// typed `0x51` reply), so overwriting `reply` each iteration keeps the correct
/// one. Surplus IN reports (beyond `batch_count`) are then drained non-blocking
/// (bounded by `IN_DRAIN_MAX`) as a safety net so a persistent handle does not
/// stall on accumulated replies. `read_timeout` returns `Ok(0)` on timeout/no-data
/// (NOT an error) and `Ok(n > 0)` on a real read; a timeout/error breaks the
/// capture loop early (`external_deps.md` §read_timeout semantics).
```

**(b) `send_raw_report` doc comment** (~core.rs:155-165):

- Summary — `/// total failure. Returns the FIRST device reply captured by the burst-write path.`
  → `/// total failure. Returns the LAST device reply captured by the burst-write path`
  (continue: `/// (the ETX-report reply).`)
- `# Ok(Some(bytes))` bullet —
  `///   first device replied within `REPLY_READ_TIMEOUT_MS` (the bounded read in`
  → `///   one device replied within `REPLY_READ_TIMEOUT_MS` (the bounded reads in`
  and
  `///   `burst_to_one`). `bytes` is that first reply's raw IN report (up to`
  → `///   `burst_to_one`). `bytes` is the last (ETX-report) reply's raw IN report (up to`

**(c) `REPLY_READ_TIMEOUT_MS` doc** (~core.rs:68-73 — locate via
`grep -n "first-reply capture"`; OPTIONAL consistency fix, since it references
the now-changed behavior):

- `/// Bounded timeout (ms) for reading the first typed reply after a burst`
  → `/// Bounded timeout (ms) for reading each device reply after a burst`
- `/// (`burst_to_one`'s first-reply capture).`
  → `/// (`burst_to_one` reads up to `batch_count` replies at this timeout each).`

> Leave the rest of the `REPLY_READ_TIMEOUT_MS` doc (the "Must be > 0" rationale,
> the "1000 ms is conservative" note) unchanged — those statements remain accurate
> (each read still blocks for a real reply).

### Success Criteria

- [ ] The single `match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS)`
      capture is replaced by `for _ in 0..batch_count.max(1) { match … overwrite reply … }`.
- [ ] Each loop iteration with `Ok(n) if n > 0` does `reply = Some(read_buf[..n].to_vec())`
      (overwrites); a timeout/err (`_`) arm does `break`.
- [ ] The non-blocking surplus drain loop (`for _ in 0..IN_DRAIN_MAX { read_timeout(0) }`)
      and the `(true, reply)` return are UNCHANGED.
- [ ] `burst_to_one`'s signature is UNCHANGED (`fn burst_to_one<T: RawHid>(&T, &[u8], usize, bool) -> (bool, Option<Vec<u8>>)`).
- [ ] The verbose `println!("Captured device reply: {} bytes", n)` is preserved (now fires once per captured reply).
- [ ] `burst_to_one` + `send_raw_report` (and optionally `REPLY_READ_TIMEOUT_MS`)
      doc comments no longer say "first reply"; they describe capture-last / ETX-report semantics.
- [ ] `cargo build` → zero warnings; `cargo clippy --all-targets` → zero new warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → **68 passed, 0 failed**.
- [ ] No file other than `src/core.rs` is modified; the `#[cfg(test)] mod tests`
      block / `FakeHid` / the 3 `fakehid_*` smoke tests are UNTOUCHED.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim old→new
> capture block, the verbatim replacement doc-comment text for both functions,
> the canonical loop (matching the design doc §Step 4), the "locate by content
> grep, not line number" rule (with the exact grep strings), the "KEEP the drain
> unchanged" instruction, the verified baseline (68 tests, with S1 FakeHid
> already in the working tree), the trace proving the 3 S1 smoke tests survive,
> the read_buf-reuse safety note, and the latency characteristic are all below.
> The implementer needs no keyboard and no firmware source dive — the design doc
> + this PRP contain the entire loop and all doc strings.

### Documentation & References

```yaml
# MUST READ — the canonical design for this exact fix (the loop + edge-case table)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§Step 4 (Capture-Last-Reply) gives the verbatim loop this PRP implements
        (for _ in 0..batch_count.max(1) { ... overwrite reply ... } + the keep-last
        comment). §Edge Cases is the truth table proving the fix is correct for
        every (batch_count, reply-stream) shape (single/multi legacy, single/multi
        typed, no-reply). §FakeHid Test Double + the two regression tests show the
        downstream S3 test this enables. This is the source-of-truth design."
  section: "Step 4 (Capture-Last-Reply)", "Edge Cases", "FakeHid Test Double"
  critical: "The loop MUST use batch_count.max(1) (not bare batch_count) and MUST
             overwrite reply each iteration (keep LAST), and MUST keep the
             post-capture non-blocking drain as a safety net. Do not 'optimize'
             by lowering REPLY_READ_TIMEOUT_MS or breaking early."

# MUST READ — the file being edited (the function + doc comments to change)
- file: src/core.rs
  why: "burst_to_one<T: RawHid> is at line 337 (LANDED). Its capture block
        (comment + let mut reply + single match) is ~370-385; the drain loop +
        (true, reply) return is ~400-406 and must be KEPT. The burst_to_one doc
        comment is ~307-336; the send_raw_report doc comment is ~155-165;
        REPLY_READ_TIMEOUT_MS doc is ~68-73. ALL 'first reply' references to
        update live in these three doc blocks."
  pattern: "burst_to_one is generic over the RawHid trait (P1.M1.T1.S1/S2,
            LANDED); FakeHid (P1.M1.T2.S1) implements it and is present in the
            working tree's #[cfg(test)] mod tests (lines 754-821). S2 edits
            burst_to_one's BODY + doc comments only — DISJOINT from mod tests."
  gotcha: "LOCATE every anchor by content grep (grep -n 'Capture the FIRST',
           'first device reply', 'FIRST captured IN report', 'first-reply
           capture', 'let mut reply', 'fn burst_to_one'), NOT by the line
           numbers here — S1's landing shifted them and they will shift again."

# MUST READ — the bug this fixes (live reproduction + firmware proof)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 1 documents the live failure: 2-report send -> replies [0,1], crate
        captures [0]; 4-report -> [0,0,0,1], crate captures [0]. Confirms the
        firmware's hid_notify() runs once PER REPORT and raw_hid_sends at the end
        of every call (response[0]=match, false until ETX). Establishes the
        boundary: any payload > PAYLOAD_PER_REPORT (30) is affected."
  section: "Critical Issues / Issue 1 (Steps to Reproduce, Boundary)"

# MUST READ — the parallel sibling PRP whose output S2 consumes (FakeHid)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S1/PRP.md
  why: "Delivered the FakeHid test double + 3 capture-AGNOSTIC smoke tests
        (assert success/writes.len()/is_some(), NEVER reply bytes). Those smoke
        tests are S2's regression safety net: they must STILL PASS after the
        capture rewrite. Treat FakeHid + the smoke tests as a CONTRACT — do NOT
        touch mod tests."
  section: "What (The 3 smoke tests)", "Known Gotchas (capture-agnostic)"

# REFERENCE — the trait the loop runs through (already landed, do NOT modify)
- file: src/core.rs
  why: "Lines 13-31 hold pub(crate) trait RawHid { write, read_timeout } and the
        blanket impl for hidapi::HidDevice (P1.M1.T1.S1). The capture loop calls
        interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) — its
        Ok(0)=timeout/Ok(n>0)=read contract is what the loop's arms depend on."
  pattern: "read_timeout returns Ok(0) on timeout (NOT Err); Ok(n>0) on a real
            read. The loop's `Ok(n) if n > 0 =>` arm and `_ => break` arm map
            exactly to this contract."

# REFERENCE — PRD invariants the fix upholds
- file: PRD.md
  why: "§4.4 (one 32-byte reply per report), §8 (response[0] is the legacy
        match-bool, computed at ETX), §10.2 (reply disambiguation 0x51/0/1),
        §14 invariants 6 (disambiguation) and 8 (firmware wins on disagreement)."
  section: "4.4 Replies are received", "8 Response Handling", "10.2 Reply parsing",
           "14 Key Invariants (6, 8)"

# REFERENCE — empirical verification (baseline, surviving tests, latency)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T2S2/research/notes.md
  why: "Documents the CRITICAL working-tree correction (S1 FakeHid ALREADY landed
        uncommitted -> baseline is 68, not 65), the trace proving the 3 S1 smoke
        tests survive the rewrite, the exact current doc-comment text + grep
        strings, the read_buf-reuse safety, and the latency characteristic."
```

### Current Codebase tree (verified post-S1, working tree)

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.3.0"; deps: clap, hidapi "2.4.1"
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore
└── src
    ├── main.rs         # binary entrypoint — DO NOT TOUCH
    ├── lib.rs          # public API + tests — DO NOT TOUCH
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # <-- FILE TO EDIT:
                         #     RawHid trait+impl (13-31) — CONSUMED, unchanged
                         #     burst_to_one<T: RawHid> (337) — EDIT body (capture loop) + doc (307-336)
                         #     send_raw_report (171) — EDIT doc comment (~155-165) only
                         #     REPLY_READ_TIMEOUT_MS (~72) — OPTIONAL doc edit (~68-73)
                         #     #[cfg(test)] mod tests + FakeHid (754+) + smoke tests (1408+) — UNTOUCHED
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── core.rs   # MODIFIED ONLY:
              #   - burst_to_one capture block: single match -> batch_count.max(1) loop (keep last)
              #   - burst_to_one doc comment: "first" -> "last (ETX-report reply)"
              #   - send_raw_report doc comment: "first reply" -> "last (ETX-report) reply"
              #   - REPLY_READ_TIMEOUT_MS doc (optional): "first-reply capture" -> "each reply"
# No new files, no new deps, no signature changes, no test changes.
```

> No new files. All changes are inside `src/core.rs`.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: the working-tree baseline is 68 tests, NOT 65. The parallel sibling
//   S1 (FakeHid + 3 smoke tests) is ALREADY present (uncommitted src/core.rs).
//   Verified: `cargo test --lib` -> 68 passed; `grep -n "struct FakeHid"` -> 773.
//   The design doc cites 65 because it predates S1. S2 adds no tests, so the
//   expected post-state is STILL 68. (If you see 65, the tree was reset — S1 is
//   a prerequisite; do NOT proceed against a 65 baseline without FakeHid.)

// CRITICAL: locate EVERY edit anchor by content grep, NOT line number. S1's
//   landing shifted core.rs line numbers (burst_to_one capture moved to ~370,
//   not the contract's ~344). Use:
//     grep -n "Capture the FIRST device reply" src/core.rs   # capture comment
//     grep -n "let mut reply" src/core.rs                    # capture vars
//     grep -n "fn burst_to_one" src/core.rs                  # the function
//     grep -n "CAPTURE the first\|FIRST captured IN\|first device reply\|first-reply capture" src/core.rs
//   The verbatim OLD text in the "What" section matches the current tree exactly.

// CRITICAL: KEEP the non-blocking surplus drain loop (for _ in 0..IN_DRAIN_MAX
//   { read_timeout(0) }) and the (true, reply) return UNCHANGED. It is the
//   safety net for straggler replies beyond batch_count. Do NOT merge it into
//   the capture loop or remove it. The contract + design doc both mandate
//   "keep the existing drain as a safety net."

// CRITICAL: do NOT change burst_to_one's SIGNATURE. It stays
//   fn burst_to_one<T: RawHid>(&T, &[u8], usize, bool) -> (bool, Option<Vec<u8>>).
//   Only the body's capture block + doc comments change.

// CRITICAL: do NOT touch the #[cfg(test)] mod tests block, FakeHid, or the 3
//   fakehid_* smoke tests. S1 owns that region (core.rs:754-821, 1408-1480).
//   S2's edits (burst_to_one body/docs, send_raw_report doc) are DISJOINT from
//   mod tests — no overlap, composes cleanly regardless of commit order.

// NOTE: read_buf reuse across loop iterations is SAFE. Each capture does
//   read_buf[..n].to_vec() which CLONES the prefix into an owned Vec. A later
//   iteration overwriting read_buf does not affect the earlier owned Vec. No
//   aliasing, no stale slice.

// NOTE: batch_count.max(1) — use it (do not write bare `0..batch_count`). It
//   guards batch_count==0. batches_for returns 0 only for empty data; run()/
//   send_raw_report always have >=1 payload byte, but the .max(1) matches the
//   canonical design doc and is defensive.

// NOTE: the verbose println!("Captured device reply: {} bytes", n) now fires
//   once PER captured reply (e.g. twice for a 2-report send). That is accurate
//   debug output — keep it inside the loop's Ok(n>0) arm. Do NOT move it outside.

// NOTE: latency characteristic (by-design, NOT a bug). For a NON-responding
//   device, worst case = batch_count × REPLY_READ_TIMEOUT_MS (e.g. 2 reports ⇒
//   ~2s). For a responding device each read returns in milliseconds. Do NOT
//   "optimize" by lowering REPLY_READ_TIMEOUT_MS or breaking early — the loop
//   must read up to batch_count replies to reach the ETX report.

// NOTE: no rustfmt.toml / clippy.toml — default configs. The proposed code is
//   rustfmt-clean, but always run `cargo fmt` after editing (the loop reflow +
//   the `// overwrite ⇒ keep LAST (ETX) reply` comment may want reformatting).

// NOTE: this fix relies on the RawHid::read_timeout Ok(0)=timeout / Ok(n>0)=read
//   contract. FakeHid mirrors it (empty queue ⇒ Ok(0)). The loop's `Ok(n) if
//   n > 0 =>` and `_ => break` arms map exactly to that contract — do not invert
//   them or treat Ok(0) as an error.
```

## Implementation Blueprint

### Data models and structure

No new types, no signature changes. This task rewrites the *body* of one
function (`burst_to_one`) and syncs two (optionally three) doc comments. The
data flowing through is unchanged: `(bool, Option<Vec<u8>>)` where the
`Option<Vec<u8>>` now holds the **last** captured reply instead of the first.

```rust
// Structural change = the single `match` capture block becomes a `for` loop.
// reply stays `Option<Vec<u8>>`; read_buf stays `[0u8; REPORT_LENGTH + 1]`.
// The overwrite semantic (reply = Some(...)) is what flips first→last.
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/core.rs and confirm the inputs + anchors (by content, not line)
  - CONFIRM (grep -n "fn burst_to_one" src/core.rs) the generic signature
          `fn burst_to_one<T: RawHid>(interface: &T, data: &[u8], batch_count:
          usize, verbose: bool) -> (bool, Option<Vec<u8>>)` is present (S2 of
          M1.T1, LANDED). If ABSENT, STOP: prerequisite missing.
  - CONFIRM (grep -n "struct FakeHid" src/core.rs) FakeHid exists (S1, LANDED in
          the working tree) — this is S2's baseline (68 tests). If ABSENT, STOP:
          S1 not landed; the capture fix is untestable in CI.
  - LOCATE the capture block: `grep -n "Capture the FIRST device reply" src/core.rs`
          → the comment; immediately after are `let mut reply` + `let mut
          read_buf` + the single `match interface.read_timeout(&mut read_buf,
          REPLY_READ_TIMEOUT_MS)`. Read this whole block (~10 lines).
  - LOCATE the drain loop: `grep -n "let mut drain_buf" src/core.rs` → the
          `for _ in 0..IN_DRAIN_MAX` loop + `(true, reply)` that MUST stay.
  - LOCATE the doc strings: `grep -n "CAPTURE the first\|FIRST captured IN\|first
          device reply\|first-reply capture" src/core.rs` → the 3 doc blocks.
  - GOAL: know the exact, current-tree OLD text so the edit matches verbatim.

Task 2: EDIT burst_to_one — replace the single-match capture with the loop
  - REPLACE: the comment `// Capture the FIRST device reply ...` + the single
          `match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
          Ok(n) if n > 0 => { reply = Some(...); if verbose {...} } _ => {} }`
          with the NEW comment + `for _ in 0..batch_count.max(1) { match ... {
          Ok(n) if n > 0 => { reply = Some(...); if verbose {...} } _ => break, }
          }` (verbatim from the "What" section).
  - KEEP: `let mut reply: Option<Vec<u8>> = None;` and `let mut read_buf =
          [0u8; REPORT_LENGTH + 1];` immediately above (they're the loop's
          accumulator + buffer). KEEP the verbose println! inside the capture arm.
  - KEEP: the entire drain loop + `(true, reply)` return UNCHANGED.
  - DO NOT: change the signature, touch the write loop above, touch the drain,
            or add early returns.

Task 3: EDIT burst_to_one doc comment — "first" -> "last (ETX-report reply)"
  - REPLACE the summary line "CAPTURE the first device reply (bounded wait)" ->
          "CAPTURE the last device reply (the ETX-report reply, which carries
          the real result)".
  - REPLACE "the FIRST captured IN report" -> "the LAST captured IN report (the
          ETX-report reply)".
  - REPLACE the "Reply capture (v0.3.0): ... the FIRST IN report is read ..."
          paragraph with the "Reply capture (v0.3.1): ... up to batch_count IN
          reports are read ... LAST non-empty reply is retained" paragraph
          (verbatim from the "What" section).

Task 4: EDIT send_raw_report doc comment — "first reply" -> "last (ETX-report)"
  - REPLACE "Returns the FIRST device reply captured by the burst-write path." ->
          "Returns the LAST device reply captured by the burst-write path (the
          ETX-report reply).".
  - REPLACE "at least the first device replied within REPLY_READ_TIMEOUT_MS ...
          bytes is that first reply's raw IN report" -> "at least one device
          replied ... bytes is the last (ETX-report) reply's raw IN report".

Task 5 (OPTIONAL): EDIT REPLY_READ_TIMEOUT_MS doc — consistency
  - REPLACE "Bounded timeout (ms) for reading the first typed reply after a burst
          (`burst_to_one`'s first-reply capture)." -> "Bounded timeout (ms) for
          reading each device reply after a burst (`burst_to_one` reads up to
          batch_count replies at this timeout each).".
  - KEEP the "Must be > 0" / "1000 ms is conservative" lines (still accurate).
  - (This is a 3rd doc not in the contract's explicit scope, but it references
          the now-changed behavior; updating it prevents stale docs.)

Task 6: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --all-targets`,
          then `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 new warnings; fmt --check exit 0;
          test result "68 passed; 0 failed".
  - RUN the 3 S1 smoke tests in isolation to confirm they survived:
          `cargo test --lib fakehid_` -> 3 passed (capture-agnostic; unchanged).
  - IF "cannot find value `batch_count` in this scope": you placed the loop
          outside burst_to_one's body, or above the batch_count param use — it's
          a fn parameter, in scope everywhere in the body.
  - IF the 3 fakehid_* tests FAIL: you changed capture semantics in a way that
          breaks the capture-agnostic invariants (success / writes.len() /
          is_some()). Re-check: with 1 post reply + batch_count=1, the loop
          reads once, captures, reply.is_some()==true. Do NOT assert reply bytes
          here (that's S3).
```

### Implementation Patterns & Key Details

```rust
// === THE ONE STRUCTURAL CHANGE: single match -> bounded for loop ===
//
//   OLD (captures FIRST reply — wrong for multi-report):
//       match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
//           Ok(n) if n > 0 => { reply = Some(read_buf[..n].to_vec()); ... }
//           _ => {}
//       }
//
//   NEW (captures LAST reply = ETX report — correct for all batch_counts):
//       for _ in 0..batch_count.max(1) {
//           match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
//               Ok(n) if n > 0 => { reply = Some(read_buf[..n].to_vec()); ... }  // overwrite
//               _ => break,   // timeout/err -> stop (no more replies available)
//           }
//       }
//
//   Why overwrite keeps the RIGHT reply: the firmware processes + replies to
//   reports IN ORDER, and only the ETX report's reply carries the result
//   (legacy match-bool or 0x51 typed). So replies arrive as [0,…,0,result] and
//   the LAST non-empty one is always the ETX reply. (PRD §4.4, §8.)


// === WHY batch_count.max(1) (not bare batch_count) ===
//   batches_for(data) returns 0 only for empty data. run()/send_raw_report
//   always supply >=1 payload byte, so batch_count >= 1 in practice. The .max(1)
//   is defensive (matches the canonical design doc) — it guarantees at least one
//   capture attempt even if a future caller passes an empty payload.


// === WHY KEEP THE DRAIN (safety net) ===
//   The capture loop reads exactly up to batch_count replies. A misbehaving or
//   echoing device could emit MORE than batch_count replies; the non-blocking
//   drain loop (read_timeout(0) × IN_DRAIN_MAX) flushes those so a persistent
//   handle doesn't stall on accumulated IN-buffer data on the NEXT send. It is
//   NOT redundant with the capture loop — different bound (IN_DRAIN_MAX=32 vs
//   batch_count), different timeout (0 non-blocking vs 1000ms blocking).


// === WHY read_buf REUSE IS SAFE ===
//   read_buf is a stack array reused across iterations. Each capture clones the
//   prefix: read_buf[..n].to_vec() -> owned Vec stored in `reply`. A subsequent
//   iteration overwrites read_buf but cannot affect the previously-cloned Vec.
//   No aliasing; no use-after-overwrite.


// === WHY THE S1 SMOKE TESTS SURVIVE (the regression safety net) ===
//   S1's 3 fakehid_* tests assert ONLY on success, writes.len(), and
//   reply.is_some() — NEVER the captured byte. Tracing the new loop:
//     fakehid_drives_generic_burst_to_one: 1 post reply, batch_count=1.
//       loop 0..1: 1 iter -> reads reply -> captures -> reply.is_some()==true.
//       writes.len()==1==batch_count. PASS (unchanged from before).
//   The reply-BYTE regression test is S3's job (it seeds [0,1] for batch_count=2
//   and asserts reply[0]==1). This task delivers the FIX; S3 locks it.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - edit:   "burst_to_one capture block (single match -> batch_count.max(1) loop)"
  - edit:   "burst_to_one doc comment (first -> last/ETX)"
  - edit:   "send_raw_report doc comment (first reply -> last ETX reply)"
  - edit:   "REPLY_READ_TIMEOUT_MS doc comment (optional; first-reply -> each-reply)"

UNCHANGED (do NOT touch):
  - "burst_to_one signature: fn burst_to_one<T: RawHid>(&T,&[u8],usize,bool) -> (bool,Option<Vec<u8>>)"
  - "the write loop (for batch in 0..batch_count { interface.write(...); })"
  - "the non-blocking surplus drain loop (for _ in 0..IN_DRAIN_MAX { read_timeout(0) })"
  - "the (true, reply) / (false, None) returns"
  - "RawHid trait + impl (core.rs:13-31)"
  - "FakeHid + 3 smoke tests (core.rs #[cfg(test)] mod tests, 754-821 / 1408-1480)"
  - "try_send_once, send_raw_report, DeviceCache, MatchKey, all pure fns"
  - "lib.rs, error.rs, main.rs, Cargo.toml"

PUBLIC API SURFACE:
  - unchanged. "burst_to_one is private; send_raw_report's signature is unchanged
    (Result<Option<Vec<u8>>, QmkError>). Only the WHICH-reply semantics of the
    captured Option<Vec<u8>> change (first -> last/ETX), which is the bug fix."

DOWNSTREAM CONSUMERS (awareness; do NOT implement here):
  - P1.M1.T2.S3: "the Issue-1 regression test — seeds FakeHid post_write=[0,1] for
    a 2-report payload and asserts captured reply[0]==1 (the ETX result). This
    task's fix makes that assertion pass."
  - P1.M1.T3.S1: "pre-send IN-buffer drain (Issue 3) — adds a drain BEFORE the
    write loop. Independent of this capture fix; composes cleanly."
  - qmkonnect: "currently discards run()'s Ok value (Ok(_) => ...), so this fix
    is not daemon-breaking today, but it is REQUIRED for the planned capability
    handshake once qmkonnect consumes CommandResponse."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified, and ONLY: burst_to_one capture block + 2 (or 3)
    doc comments. Do NOT add the regression test (S3), do NOT add the pre-send
    drain (T3.S1), do NOT touch mod tests/FakeHid, do NOT change any signature.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/s2_build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# If "cannot find value `batch_count`": the loop is outside burst_to_one's body
#   — it's a fn param, in scope everywhere in the body; check your edit placement.
# If "value assigned to `reply` is never read": unreachable — the loop's last
#   assignment IS read by the (true, reply) return. Re-check you kept the return.

# Lint ALL targets (FakeHid + smoke tests are compiled here too).
cargo clippy --all-targets 2>&1 | tee /tmp/s2_clippy.log
# Expected: no warnings/errors for the new loop. (A `needless_range_loop` lint is
#   NOT triggered because the loop body uses interface.read_timeout, not the
#   index — `for _ in 0..N` with an unused index is the correct idiom here.)

# Formatting gate.
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Confirm the 3 S1 smoke tests survived the rewrite (they are capture-agnostic).
cargo test --lib fakehid_ -- --nocapture
# Expected: 3 passed (success / writes.len() / is_some() — unchanged).

# Full lib test suite (core.rs #[cfg(test)] mod tests + lib.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 68 passed; 0 failed; 0 ignored; ..." (baseline 68;
# S2 adds no tests — the Issue-1 regression test is S3).

# Sanity: confirm the pre-existing pure-fn tests still pass untouched.
cargo test --lib batches_for_ -- --nocapture
cargo test --lib parse_reply -- --nocapture
# Expected: all pre-existing tests pass (this fix touches no pure function).

# NOTE: a passing cargo test here does NOT prove the capture fix is correct —
# it proves nothing regressed. The CORRECTNESS proof is the S3 regression test
# (seed [0,1] for a 2-report payload, assert reply[0]==1). Do NOT add that test
# in this task; verify the fix logic by reading the loop against the Edge Cases
# table in reply_capture_design.md.
```

### Level 3: Integration Testing (System Validation)

```text
LIVE HARDWARE (optional, not required for CI gate):
  The fix is observable end-to-end only on a QMK keyboard with qmk-notifier
  firmware. If one is connected, reproduce the TEST_RESULTS.md cases:
    cargo build --release
    ./target/release/qmk_notifier "hello"                       # 1 report -> Legacy{matched:true}
    ./target/release/qmk_notifier "hello.............................."  # 2 reports
        # BEFORE fix: Legacy{matched:false} (captured the [0] reply)  ❌
        # AFTER  fix: Legacy{matched:true}  (captured the ETX [1] reply) ✅
  No keyboard? The S3 regression test (FakeHid-seeded [0,1]) is the CI-proof
  equivalent — this task delivers the fix; S3 locks it.

NOT APPLICABLE for the CI gate:
  burst_to_one is exercised through FakeHid in unit tests; there is no service
  to start, no endpoint to curl, no DB. The Level 2 `cargo test --lib` + the S1
  smoke-test survival check IS the integration validation for this task.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the capture block is now a loop (not a single match):
grep -nA2 "for _ in 0..batch_count.max(1)" src/core.rs
# Expected: the loop header + the match with Ok(n>0)=>overwrite / _=>break arms.

# Confirm the surplus drain is still present (the safety net was NOT removed):
grep -nA3 "for _ in 0..IN_DRAIN_MAX" src/core.rs
# Expected: the non-blocking drain loop, unchanged.

# Confirm no "first reply" / "FIRST captured" / "first-reply capture" remains in
# the two (optionally three) doc blocks:
grep -niE "first (reply|captured|device reply|IN report|typed reply|reply capture)|first-reply capture" src/core.rs \
  || echo "core.rs docs: no stale 'first reply' references (good)"
# (The drain comment's historical "v0.2.x discards" note is unrelated — leave it.
#  If the only remaining hit is unrelated prose, that's fine; re-read it to confirm.)

# Confirm burst_to_one's signature is unchanged:
grep -n "fn burst_to_one<T: RawHid>" src/core.rs
# Expected: exactly one hit, signature identical to before.

# Confirm no doc-comment edit accidentally leaked into mod tests / FakeHid:
grep -nE "FakeHid|push_pre|push_post|fn fakehid_" src/core.rs | wc -l
# Expected: the same count as the S1 landing (struct + 2 impls + helpers + 3 tests);
# S2 must NOT have added/removed any of these.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings.
- [ ] Level 1 passed: `cargo clippy --all-targets` → zero new warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 68 passed, 0 failed.
- [ ] Level 2 passed: the 3 `fakehid_*` smoke tests pass individually (survived the rewrite).

### Feature Validation

- [ ] The capture block is a `for _ in 0..batch_count.max(1)` loop (not a single `match`).
- [ ] Each `Ok(n) if n > 0` iteration overwrites `reply` (keep last); `_ => break`.
- [ ] The non-blocking surplus drain + `(true, reply)` return are UNCHANGED.
- [ ] `burst_to_one`'s signature is unchanged.
- [ ] The verbose `println!("Captured device reply: {} bytes", n)` is preserved.
- [ ] `burst_to_one` + `send_raw_report` (and optionally `REPLY_READ_TIMEOUT_MS`) docs no longer say "first reply".

### Code Quality Validation

- [ ] Loop uses `batch_count.max(1)` (defensive; matches design doc).
- [ ] `read_buf` reused safely (`.to_vec()` clones each capture).
- [ ] No early returns / signature changes / drain edits / write-loop edits.
- [ ] Edits confined to `burst_to_one` body + doc comments (+ optional REPLY_READ_TIMEOUT_MS doc).
- [ ] `mod tests` / `FakeHid` / smoke tests UNTOUCHED (disjoint region from S1).
- [ ] Anchors located by content grep (not stale line numbers).

### Documentation & Deployment

- [ ] Doc comments updated in-file (Mode A — no external doc files changed).
- [ ] Doc comments cite PRD §4.4 / §8 and the ETX-report rationale.
- [ ] No Cargo.toml / env / config / README change (out of scope).
- [ ] No new public-API surface (private function body + doc edits only).

---

## Anti-Patterns to Avoid

- ❌ Don't change `burst_to_one`'s **signature**. It stays
  `fn burst_to_one<T: RawHid>(&T, &[u8], usize, bool) -> (bool, Option<Vec<u8>>)`.
  Only the body's capture block + doc comments change.
- ❌ Don't remove or merge the non-blocking surplus **drain** loop. It is the
  safety net for straggler replies beyond `batch_count`; the contract + design
  doc both mandate keeping it. Different bound (`IN_DRAIN_MAX=32`), different
  timeout (0 non-blocking) than the capture loop — not redundant.
- ❌ Don't use bare `0..batch_count` — use `0..batch_count.max(1)`. The `.max(1)`
  guards `batch_count==0` and matches the canonical design doc.
- ❌ Don't invert the loop arms or treat `Ok(0)` as an error. The
  `RawHid::read_timeout` contract is `Ok(0)`=timeout/`Ok(n>0)`=read (NOT Err on
  timeout). Map `Ok(n) if n > 0 => capture` and `_ => break`.
- ❌ Don't "optimize" by lowering `REPLY_READ_TIMEOUT_MS` or breaking early on
  the first reply. The loop MUST read up to `batch_count` replies to reach the
  ETX report. The latency characteristic (batch_count × 1000ms for a silent
  device) is by-design and accepted by the spec.
- ❌ Don't add the Issue-1 regression test here — that is **S3**. This task
  delivers the FIX; S3 locks it with a FakeHid-seeded `[0,1]` test asserting
  `reply[0]==1`. Adding it now crosses the S1/S2/S3 boundary.
- ❌ Don't touch `mod tests`, `FakeHid`, or the 3 `fakehid_*` smoke tests. S1
  owns that region (disjoint from S2's edits); the smoke tests are your
  regression safety net and must survive untouched.
- ❌ Don't trust the contract's line numbers (~344-354, ~368-374, ~279-304,
  ~110-140). S1's landing shifted them (capture is now ~370, docs ~307-336 /
  ~155-165). Locate every anchor by content grep.
- ❌ Don't leave stale "first reply" / "FIRST captured IN report" / "first-reply
  capture" text in the doc comments after the fix — Mode A docs must describe the
  new capture-last / ETX-report semantics.
- ❌ Don't move the verbose `println!` outside the loop or remove it. It now fires
  once per captured reply (accurate); keep it in the `Ok(n>0)` arm.
- ❌ Don't touch `send_raw_report`'s/`try_send_once`'s/`run()`'s logic — only
  `send_raw_report`'s **doc comment** (it says "first reply"). The "first
  successful device wins" comment in `try_send_once` is about MULTI-DEVICE
  selection, NOT reply capture — leave it (that's a different "first").
- ❌ Don't proceed against a 65-test baseline — if `cargo test --lib` shows 65,
  S1 (FakeHid) is NOT in the tree and this fix is untestable in CI. S1 is a hard
  prerequisite (the real baseline is 68 with S1 landed).

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is one surgical body edit (verbatim old→new capture block from the canonical
design doc §Step 4) plus doc-comment text syncs in two functions (verbatim
old→new strings provided, locatable by content grep). The inputs it consumes
(`RawHid` trait, generic `burst_to_one`, `FakeHid` + capture-agnostic smoke
tests) are all already landed and verified in the working tree (68-test
baseline). The two real risks — (a) stale line numbers after S1's landing, and
(b) accidentally breaking the S1 smoke tests — are both characterized with
mitigations (locate-by-content-grep; the smoke tests are traced to survive the
loop for batch_count=1). The fix logic itself is provably correct against the
design doc's Edge Cases table (every batch_count/reply-stream shape keeps the
ETX reply). The regression test that locks it is explicitly deferred to S3, so
this task stays in its lane.