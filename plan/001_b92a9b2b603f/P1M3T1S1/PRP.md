name: "P1.M3.T1.S1 — Evolve burst_to_one to capture first reply"
description: "Change burst_to_one's return type to (bool, Option<Vec<u8>>), add a bounded first-reply read before the existing drain, update the try_send_once call site to compile via a temporary .0 access, and remove the now-satisfied #[allow(dead_code)] on REPLY_READ_TIMEOUT_MS. Internal function change; src/core.rs is the ONLY file modified; no new tests."

---

## Goal

**Feature Goal**: Evolve `burst_to_one` from a reply-**discarding** function
(`-> bool`) into a reply-**capturing** one (`-> (bool, Option<Vec<u8>>)`) so the
v0.3.0 transport path can surface the FIRST 32-byte device reply up to callers
for typed-response parsing. After all burst-writes succeed, the function now
performs ONE bounded `read_timeout(REPLY_READ_TIMEOUT_MS)` to capture the first
reply, THEN runs the existing non-blocking drain (unchanged) to clear surplus IN
reports.

**Deliverable**: A clean, self-contained edit to **`src/core.rs` ONLY**:
1. `burst_to_one` signature `-> (bool, Option<Vec<u8>>)`.
2. Write-error early return becomes `(false, None)`; the write loop is otherwise
   byte-for-byte unchanged.
3. NEW bounded reply read inserted **between the write loop and the drain loop**
   (capture first reply, verbose-log its length).
4. The existing drain loop is preserved unchanged **after** the reply read.
5. Success return becomes `(true, reply)`.
6. The `try_send_once` call site is updated with a temporary `.0` access (the
   reply is discarded here; full propagation is **P1.M3.T2.S1**).
7. `burst_to_one`'s doc comment is updated to document the reply capture.
8. The now-satisfied `#[allow(dead_code)]` on `REPLY_READ_TIMEOUT_MS` is removed
   and the constants comment block is corrected to match.

**Success Definition**: `cargo build` → 0 warnings; `cargo clippy --lib` → 0
warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **57 passed; 0
failed** (NO new tests — `burst_to_one` needs real HID hardware and is verified
by compilation + the unchanged existing suite). The crate **COMPILES** after this
subtask (see *Gotchas* for the resolution of the item's "may not compile" note).

## User Persona (if applicable)

**Target User**: The v0.3.0 transport dispatch path. Today `burst_to_one` throws
the device reply away in its drain loop; `parse_reply` (P1.M2.T2, complete) has
no live byte source. This subtask makes `burst_to_one` the **producer** of the
first reply. The immediate consumer is **P1.M3.T2.S1** (which plumbs the reply
through `try_send_once` → `send_raw_report` → `run()`), and ultimately
**P1.M3.T3.S1** (`run()` hands the bytes to `parse_reply`).

**Use Case**: A typed-capable keyboard receives `QUERY_INFO`, bursts back a
`[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]`
reply. `burst_to_one` captures those bytes (instead of draining them) and returns
them in the `Option<Vec<u8>>`; `run()` later decodes them into
`CommandResponse::Info { … }`. A legacy device that sends no reply within the
bounded timeout returns `(true, None)` ⇒ the caller treats it as a non-capable
device and stays in string-only mode (PRD §8).

**Pain Points Addressed**: (1) The drain loop today DISCARDS every IN report, so
typed replies are irretrievably lost — no typed command can ever be answered.
(2) There is no bounded "wait for the first reply" read anywhere; the only
`read_timeout` calls use `0` (non-blocking poll), which would always miss a reply
that hasn't arrived yet. This subtask adds exactly that bounded read, once, at
the right point in the send sequence.

## Why

- **PRD §8 (Response Handling)** + **§14 invariant 6**: the host must
  disambiguate `0x51` (typed) from `0`/`1` (legacy match-bool) from no-reply
  (`Timeout`). That disambiguation needs the raw reply bytes; `parse_reply` can't
  run on bytes that were drained away. `burst_to_one` is the only place that
  holds an open `&HidDevice` right after the burst, so it is the correct capture
  site.
- **architecture/transport_evolution.md §burst_to_one Reply Capture Logic** gives
  the EXACT before/after code for this change — this PRP transcribes it faithfully
  (capture first reply with bounded timeout, THEN drain surplus).
- **architecture/external_deps.md §read_timeout semantics** pins the contract:
  `Ok(0)` = timeout/no-data (NOT an error), `Ok(n>0)` = data read. This drives
  the `Ok(n) if n > 0 =>` guard (the SAME guard the drain loop already uses, but
  with `REPLY_READ_TIMEOUT_MS` instead of `0`).
- **Dependency-chain integrity**: this is the bottom of the reply-capture chain
  (`burst_to_one` → `try_send_once` → `send_raw_report` → `run()`). Landing the
  capture here — and making the crate still compile via `.0` — lets P1.M3.T2.S1
  and P1.M3.T3.S1 build on a known-good producer without re-touching `burst_to_one`.

## What

### 0. Read this before coding — the "may not compile" note is RESOLVED here
The item's OUTPUT line says *"The crate may not compile until try_send_once (next
subtask) is updated."* Read against step **(f)** — *"Update the call site in
try_send_once … Add a temporary `.0` access"* — these reconcile as follows:
**step (f) IS the compile fix.** Adding `.0` makes the crate build. The OUTPUT
note describes the *logical* state (the captured reply is discarded at the call
site until `try_send_once` evolves), NOT a literal compile failure. **This PRP
directs the implementer to leave the crate COMPILING** (build + all 57 tests
pass). A non-compiling increment cannot run its own validation gate, which would
violate the PRP model. See *Gotchas* for the exact `.0` edit.

### 1. `burst_to_one` — signature + reply capture + drain (src/core.rs)
- Change the return type to `(bool, Option<Vec<u8>>)`.
- Write-error early return → `(false, None)`. Write loop body otherwise unchanged.
- Insert the bounded reply read AFTER the write loop, BEFORE the drain loop.
- Keep the existing drain loop unchanged (still non-blocking, still bounded by
  `IN_DRAIN_MAX`).
- Success return → `(true, reply)`.
- Add verbose logging of the captured reply length.

### 2. `try_send_once` call site — temporary `.0` (src/core.rs)
- The single `if burst_to_one(interface, data, batch_count, verbose) {` becomes
  `if burst_to_one(interface, data, batch_count, verbose).0 {`. Nothing else in
  `try_send_once` changes (its own signature stays `Result<SendOutcome, QmkError>`).

### 3. Doc comment + dead-code cleanup (src/core.rs)
- Rewrite `burst_to_one`'s doc comment to document the return tuple + reply
  capture (keep the burst-write-is-safe paragraph verbatim).
- Remove `#[allow(dead_code)]` from `REPLY_READ_TIMEOUT_MS` (now consumed) and
  correct the constants comment block to match.

### Success Criteria
- [ ] `burst_to_one` returns `(bool, Option<Vec<u8>>)`; write error ⇒ `(false,
      None)`; success ⇒ `(true, reply)`.
- [ ] The bounded reply read is placed AFTER the write loop and BEFORE the drain
      loop; the drain loop is byte-identical to before.
- [ ] `try_send_once`'s call site uses `.0` and compiles.
- [ ] `cargo build` → 0 warnings; `cargo clippy --lib` → 0 warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → **57 passed; 0 failed**.
- [ ] `REPLY_READ_TIMEOUT_MS` no longer carries `#[allow(dead_code)]`; its comment
      is accurate.
- [ ] ONLY `src/core.rs` is modified.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact current
> `burst_to_one` body (verbatim, with line numbers), the exact call site in
> `try_send_once`, the exact `REPLY_READ_TIMEOUT_MS` + constants-comment state,
> the exact target body (designed), the `read_timeout` return contract, and the
> `.0` compile fix are ALL given verbatim below and in research `notes.md`. The
> baseline (57 passing, verified by `cargo test --lib`) and the 4 validation
> commands are confirmed working in this repo. No firmware source is needed — the
> wire contract is fully captured in `firmware_wire_contract.md`.

### Documentation & References

```yaml
# MUST READ — the file being edited (the only file touched)
- file: src/core.rs
  why: "Holds burst_to_one (src/core.rs:248, -> bool) to be evolved; the
        try_send_once call site (~src/core.rs:215) to get .0; REPLY_READ_TIMEOUT_MS
        (src/core.rs:38, #[allow(dead_code)]) to have the allow removed; the
        constants comment block (src/core.rs:11-24) to be corrected. All exact
        current text is reproduced in 'Implementation Patterns' below."
  pattern: "burst_to_one's write loop uses a stack `request_data = [0u8;
            REPORT_LENGTH + 1]` with request_data[1]=0x81, request_data[2]=0x9F,
            fills request_data[3..] per batch, verbose-logs, and early-returns
            false on interface.write error. The drain loop uses the
            `Ok(n) if n > 0 => continue, _ => break` idiom with
            read_timeout(.., 0), bounded by IN_DRAIN_MAX."
  gotcha: "The new reply read REUSES that SAME `Ok(n) if n > 0` guard but with
           read_timeout(.., REPLY_READ_TIMEOUT_MS). Do NOT change the drain's
           timeout=0. read_timeout returns Ok(0) on timeout (NOT Err) — see F4."

# MUST READ — research notes: verbatim current code + designed target + gotchas
- docfile: plan/001_b92a9b2b603f/P1M3T1S1/research/notes.md
  why: "F1 = verbatim current burst_to_one; F2 = verbatim call site; F3 =
        REPLY_READ_TIMEOUT_MS + comment block state; F4 = read_timeout contract;
        F5 = the 'may not compile' reconciliation (step f = .0 compile fix); F6 =
        read_buf[..n] in-bounds proof; F7 = designed target body; F8 = scope
        boundary; F9 = validation gate."
  section: "F1, F2, F5, F7 (the core), F4, F6 (safety), F8 (boundary)"

# MUST READ — the architecture doc with the EXACT before/after for this change
- docfile: plan/001_b92a9b2b603f/architecture/transport_evolution.md
  why: "§burst_to_one Reply Capture Logic gives the verbatim OLD drain loop and
        the verbatim NEW capture+drain block this PRP transcribes. §Signature
        Changes (3) pins the new signature."
  section: "burst_to_one Reply Capture Logic", "Signature Changes (3,4,5,6)"

# MUST READ — the hidapi read_timeout contract (drives the Ok(n) if n>0 guard)
- docfile: plan/001_b92a9b2b603f/architecture/external_deps.md
  why: "§read_timeout semantics: timeout=0 non-blocking, >0 bounded wait;
        Ok(0)=timeout/no-data (NOT an error), Ok(n>0)=data read. This is why the
        reply capture uses `Ok(n) if n > 0` and treats `_ =>` (Ok(0) and Err) as
        no-reply."
  section: "read_timeout semantics (critical for reply capture)"

# REFERENCE — the canonical wire contract (why a reply exists at all)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "§Reply Disambiguation + §Constants (RESPONSE_MARKER=0x51). The bytes this
        function captures are decoded downstream by parse_reply per this table."

# REFERENCE — PRD framing + the invariant this change serves
- file: PRD.md
  why: "§8 (Response Handling) + §14 invariant 6 (reply disambiguation) explain
        WHY the first reply must be captured rather than drained."
  section: "8. Response Handling", "14. Key Invariants (6)"

# REFERENCE — the reply is discarded here on purpose; full propagation is S2
- docfile: plan/001_b92a9b2b603f/P1M3T2S1/PRP.md
  why: "Defines the NEXT subtask: evolve try_send_once to
        Result<(SendOutcome, Option<Vec<u8>>), QmkError> and send_raw_report to
        Result<Option<Vec<u8>>, QmkError>. Confirms that the `.0` discard done
        HERE is temporary and will be replaced by real propagation there."
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.2.1", edition="2021"
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore          # contains only: /target
└── src
    ├── main.rs         # binary entrypoint — DO NOT TOUCH
    ├── core.rs         # <-- FILE TO EDIT (burst_to_one, try_send_once call site,
    │                   #     REPLY_READ_TIMEOUT_MS, doc comment, constants comment)
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # run() + public types — DO NOT TOUCH
```

### Desired Codebase tree with files to be modified

```bash
src/
└── core.rs   # MODIFIED ONLY:
              #   (1) burst_to_one: -> (bool, Option<Vec<u8>>); add bounded reply
              #       read between write loop and drain loop; return (true, reply);
              #       write-error ⇒ (false, None); update doc comment.
              #   (2) try_send_once call site: burst_to_one(...) ⇒ burst_to_one(...).0
              #   (3) REPLY_READ_TIMEOUT_MS: remove #[allow(dead_code)];
              #       correct the constants comment block.
# (lib.rs, error.rs, main.rs, Cargo.toml unchanged; NO new files; NO new tests)
```

> No new files, no new tests, no new deps. One file modified (`src/core.rs`).

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (the "may not compile" note is RESOLVED by step f, NOT by leaving it
//   broken): the item's OUTPUT says the crate "may not compile until try_send_once
//   (next subtask) is updated." Step (f) IS the compile fix — add `.0` at the call
//   site. A non-compiling increment cannot run `cargo test --lib`, so this PRP
//   REQUIRES the crate to compile (build + 57 tests pass). The `.0` discards the
//   reply ON PURPOSE; full propagation is P1.M3.T2.S1. Do NOT leave the crate
//   broken "because the note said so."

// CRITICAL (read_timeout Ok(0) is NOT an error): hidapi read_timeout returns
//   Ok(0) on timeout/no-data, Ok(n>0) on a real read, and Err on read failure
//   (external_deps.md §read_timeout). So the reply-capture guard is
//   `Ok(n) if n > 0 => { capture } _ => {}` — the `_ =>` arm covers BOTH Ok(0)
//   (timeout) and Err (failure). Do NOT write `Ok(_) => ...` (that would treat a
//   timeout as a 0-byte reply) and do NOT write `Ok(n) => ...` without the `n > 0`
//   guard. Mirror the EXACT idiom the drain loop already uses.

// CRITICAL (read_buf[..n] is in-bounds — no defensive indexing needed): read_timeout
//   writes at most read_buf.len() bytes and returns n ≤ read_buf.len(), so
//   `read_buf[..n]` always slices in bounds and `.to_vec()` cannot panic. (Contrast
//   with parse_reply, where the FIRMWARE buffer may be shorter — there .get() is
//   mandatory. Here the host-allocated buffer is the bound, so plain indexing is safe.)

// CRITICAL (order matters): the bounded reply read MUST come AFTER the write loop
//   and BEFORE the drain loop. Reading before writing would capture a stale reply
//   from a prior command; draining before the bounded read would consume the very
//   reply you are trying to capture. The drain clears SURPLUS reports AFTER the
//   first is saved.

// GOTCHA (REPLY_READ_TIMEOUT_MS loses its #[allow(dead_code)] here): the const was
//   added in P1.M1.T2.S1 with the allow because its consumer was deferred — THIS
//   subtask is that consumer. Once burst_to_one references it, remove the allow AND
//   correct the constants comment block (src/core.rs:11-24) so it no longer claims
//   REPLY_READ_TIMEOUT_MS "still carries #[allow(dead_code)]". Leaving the allow is
//   harmless to the build but is an inaccurate maintenance smell the codebase
//   explicitly tracks — fix it.

// GOTCHA (do NOT touch try_send_once's SIGNATURE): only the internal call gets `.0`.
//   try_send_once stays `Result<SendOutcome, QmkError>`. Its evolution to
//   `Result<(SendOutcome, Option<Vec<u8>>), QmkError>` is P1.M3.T2.S1. Similarly
//   send_raw_report stays `Result<(), QmkError>` and run() in lib.rs is untouched.

// GOTCHA (no new tests): burst_to_one takes a live `&HidDevice` and performs real
//   HID I/O — it cannot be unit-tested without hardware. The item explicitly says
//   verify via compilation + the existing 57 tests still passing. Do NOT add a
//   #[test] that calls burst_to_one (it can't construct a HidDevice without a
//   device, and the existing run()/send tests already exercise the dispatch path
//   with bogus VID/PID that deterministically returns DeviceNotFound before
//   reaching burst_to_one).

// NOTE (verbose logging): the item mandates "verbose logging for the captured
//   reply length." Emit `println!("Captured device reply: {} bytes", n)` inside
//   the `Ok(n) if n > 0` arm, guarded by `if verbose`. No logging is required on
//   the no-reply path (the item specifies only the captured-length log); adding a
//   verbose "no reply within timeout" line is OPTIONAL and not required.

// NOTE (no rustfmt.toml / no clippy.toml ⇒ defaults): the new code (tuple return,
//   `.0` access, `let mut reply: Option<Vec<u8>> = None`, the match arm) is
//   fmt/clippy-clean under defaults. Run `cargo fmt` then `cargo clippy --lib` to
//   confirm 0 warnings.
```

## Implementation Blueprint

### Data models and structure
No new types, enums, structs, or constants. The return type is a built-in tuple
`(bool, Option<Vec<u8>>)`. `REPLY_READ_TIMEOUT_MS` (i32 = 1000) already exists;
this subtask only references it and removes its `#[allow(dead_code)]`. No public
API change. No state, no globals, no new deps.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY src/core.rs — burst_to_one signature + reply capture + drain
  - LOCATE: fn burst_to_one at src/core.rs:248 (signature `-> bool`).
  - CHANGE signature to:
        fn burst_to_one(
            interface: &HidDevice,
            data: &[u8],
            batch_count: usize,
            verbose: bool,
        ) -> (bool, Option<Vec<u8>>)
    (The existing one-line signature may already fit on one line; if so, keep it
    one line if rustfmt allows, else let rustfmt wrap it. Content is what matters.)
  - KEEP the write loop body UNCHANGED EXCEPT the error arm: change
        `return false;`   →   `return (false, None);`
    (the `request_data` setup, the per-batch fill, the verbose log, and the
    `interface.write(&request_data)` call all stay identical).
  - INSERT the bounded reply read AFTER the write loop's closing `}` and BEFORE
    the drain loop. Use the VERBATIM block in "Implementation Patterns" §A below.
  - KEEP the existing drain loop UNCHANGED (same `drain_buf`, same `read_timeout(.., 0)`,
    same `Ok(n) if n > 0 => continue, _ => break`, same `IN_DRAIN_MAX` bound).
  - CHANGE the final `true` → `(true, reply)`.
  - NAMING: `reply: Option<Vec<u8>>`, `read_buf: [0u8; REPORT_LENGTH + 1]`
    (mirror the existing `drain_buf` naming).
  - DEPENDENCIES: references `REPLY_READ_TIMEOUT_MS` (existing const, src/core.rs:38).
  - PLACEMENT: same location (src/core.rs); the function does not move.

Task 2: MODIFY src/core.rs — update the try_send_once call site (.0)
  - LOCATE: inside fn try_send_once (src/core.rs:~215), the line
        `if burst_to_one(interface, data, batch_count, verbose) {`
  - CHANGE to:
        `if burst_to_one(interface, data, batch_count, verbose).0 {`
  - ADD a short comment above it noting the reply is deliberately discarded here
    and full propagation is P1.M3.T2.S1 (see "Implementation Patterns" §B).
  - DO NOT: change try_send_once's signature, its SendOutcome return logic, the
    succeeded/failed accounting, or the cache-invalidation block.

Task 3: MODIFY src/core.rs — burst_to_one doc comment
  - LOCATE: the `///` doc block immediately above fn burst_to_one (src/core.rs:236-247).
  - REPLACE its first paragraph (the "Burst-write ... Returns `false` on the first
    write error." line) with the updated doc in "Implementation Patterns" §C.
  - KEEP the "Burst-write is safe without a per-report ack: ..." paragraph VERBATIM
    (it is unchanged; only the lead + a new reply-capture paragraph change).

Task 4: MODIFY src/core.rs — remove #[allow(dead_code)] + fix constants comment
  - LOCATE: `#[allow(dead_code)]` + `const REPLY_READ_TIMEOUT_MS: i32 = 1000;`
    (src/core.rs:37-38). REMOVE the `#[allow(dead_code)]` line; keep the const +
    its two `///` doc lines.
  - LOCATE: the constants comment block (src/core.rs:11-24). Update the sentence
    that says "Only RESPONSE_MARKER and REPLY_READ_TIMEOUT_MS still carry
    `#[allow(dead_code)]` — their consumers land in P1.M1.T3 (parse_reply + the
    reply reader)." to reflect that REPLY_READ_TIMEOUT_MS is now consumed by
    burst_to_one (P1.M3.T1.S1) and carries no allow. (RESPONSE_MARKER is consumed
    by parse_reply.) See "Implementation Patterns" §D for the exact replacement.
  - RATIONALE: REPLY_READ_TIMEOUT_MS now has a real consumer; leaving the allow is
    an inaccurate maintenance smell the codebase explicitly tracks.

Task 5: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, `cargo clippy --lib`, `cargo fmt --check`,
    `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0; 57 passed 0
    failed. If any existing test FAILS, the write loop or drain loop was
    accidentally altered — re-diff against the verbatim current code (F1) and fix.
```

### Implementation Patterns & Key Details

#### §A — the bounded reply read (INSERT between write loop and drain loop)

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

Full target body (for context; the write loop body is abbreviated — keep it
byte-identical to current):

```rust
fn burst_to_one(
    interface: &HidDevice,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> (bool, Option<Vec<u8>>) {
    let mut request_data = [0u8; REPORT_LENGTH + 1];
    request_data[1] = 0x81;
    request_data[2] = 0x9F;

    for batch in 0..batch_count {
        let start_idx = batch * PAYLOAD_PER_REPORT;
        let end_idx = (start_idx + PAYLOAD_PER_REPORT).min(data.len());
        let batch_data = &data[start_idx..end_idx];

        request_data[3..].fill(0);
        if !batch_data.is_empty() {
            request_data[3..3 + batch_data.len()].copy_from_slice(batch_data);
        }

        if verbose {
            println!("Sending batch {}/{}", batch + 1, batch_count);
            println!("{:?}", request_data);
        }

        if let Err(e) = interface.write(&request_data) {
            if verbose {
                println!("Error on batch {}: {}", batch + 1, e);
            }
            return (false, None);
        }
    }

    // ── NEW: bounded first-reply capture (§A) ──────────────────────────────
    let mut reply: Option<Vec<u8>> = None;
    let mut read_buf = [0u8; REPORT_LENGTH + 1];
    match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
        Ok(n) if n > 0 => {
            reply = Some(read_buf[..n].to_vec());
            if verbose {
                println!("Captured device reply: {} bytes", n);
            }
        }
        _ => {}
    }

    // ── UNCHANGED: drain surplus IN-side reports (non-blocking) ────────────
    let mut drain_buf = [0u8; REPORT_LENGTH + 1];
    for _ in 0..IN_DRAIN_MAX {
        match interface.read_timeout(&mut drain_buf, 0) {
            Ok(n) if n > 0 => continue,
            _ => break,
        }
    }

    (true, reply)
}
```

#### §B — the try_send_once call site (temporary `.0`)

```rust
            // burst_to_one now returns (bool, Option<Vec<u8>>). The bool is the
            // write-success flag; the captured reply is PROPAGATED up through
            // try_send_once / send_raw_report / run() in P1.M3.T2.S1. Until then
            // we take only the success flag via .0 and discard the reply here.
            if burst_to_one(interface, data, batch_count, verbose).0 {
                succeeded += 1;
            } else {
                failed += 1;
                if verbose {
                    println!(
                        "Failed to send message to device {}/{}.",
                        device_idx + 1,
                        device_count
                    );
                }
            }
```

#### §C — the updated burst_to_one doc comment (lead + reply-capture paragraph)

```rust
/// Burst-write `data` to a single device as `batch_count` back-to-back raw-HID
/// reports, then CAPTURE the first device reply (bounded wait), then drain any
/// surplus IN-side reports.
///
/// Returns `(false, None)` on the first write error; otherwise `(true, reply)`,
/// where `reply` is `Some(bytes)` when a reply arrived within
/// `REPLY_READ_TIMEOUT_MS`, or `None` on timeout / read failure. The bool is the
/// write-success flag (same semantics as the pre-v0.3.0 `-> bool` form); the
/// `Option<Vec<u8>>` is the FIRST captured IN report, decoded downstream by
/// `parse_reply` into a `CommandResponse` (PRD §8, §10.2).
///
/// Reply capture (v0.3.0): after the burst-write succeeds, the FIRST IN report is
/// read with a bounded `read_timeout(REPLY_READ_TIMEOUT_MS)` so the host can
/// parse the typed response. Surplus IN reports are then drained non-blocking
/// (bounded by `IN_DRAIN_MAX`) so a persistent handle does not stall on
/// accumulated replies. `read_timeout` returns `Ok(0)` on timeout/no-data (NOT an
/// error) and `Ok(n > 0)` on a real read; only `n > 0` yields a captured reply
/// (`external_deps.md` §read_timeout semantics).
///
/// Burst-write is safe without a per-report ack: QMK's raw-HID OUT endpoint
/// buffers up to `RAW_OUT_CAPACITY` (4) reports and drains them all in one
/// main-loop pass (`raw_hid_task`: `while (receive_report(...))
/// raw_hid_receive(...)`). The OUT endpoint provides its own backpressure — when
/// the device buffer is full it NAKs the transfer and the host's `write()`
/// blocks until space frees. Reports are never dropped, so burst-write is safe
/// for ANY title length. See IMPLEMENTATION_PLAN.md.
```

#### §D — the corrected constants comment block (src/core.rs:11-24)

Replace the sentence:
```
// no longer need an `#[allow(dead_code)]` (verified: a const referenced by an
// allow-dead fn's body does NOT warn). Only RESPONSE_MARKER and
// REPLY_READ_TIMEOUT_MS still carry `#[allow(dead_code)]` — their consumers land
// in P1.M1.T3 (parse_reply + the reply reader).
```
with:
```
// no longer need an `#[allow(dead_code)]` (verified: a const referenced by an
// allow-dead fn's body does NOT warn). RESPONSE_MARKER is consumed by `parse_reply`
// (P1.M2.T2.S1); REPLY_READ_TIMEOUT_MS is consumed by `burst_to_one`'s bounded
// reply capture (P1.M3.T1.S1). Neither carries `#[allow(dead_code)]`.
```
And remove the two lines `#[allow(dead_code)]` directly above
`const REPLY_READ_TIMEOUT_MS: i32 = 1000;` (keep the two `///` doc lines + the const).

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - change:  "burst_to_one (signature + body + doc comment);
              try_send_once (call site .0);
              REPLY_READ_TIMEOUT_MS (drop #[allow(dead_code)]);
              constants comment block (correct wording)"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps (hidapi 2.6.3 already provides read_timeout).

PUBLIC API SURFACE:
  - adds:    "(nothing — burst_to_one is private; try_send_once is private;
              REPLY_READ_TIMEOUT_MS is private)"
  - unchanged: "all lib.rs public symbols (HostOs, RunCommand, CommandResponse,
                RunParameters, parse_cli_args, run(), send_raw_report's signature,
                list_hid_devices, parse_hex_or_decimal, the DEFAULT_* consts,
                REPORT_LENGTH); all QmkError variants; lib.rs `pub use core::{...}`"

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M3.T2.S1 (try_send_once + send_raw_report): "evolves try_send_once to
        Result<(SendOutcome, Option<Vec<u8>>), QmkError> and send_raw_report to
        Result<Option<Vec<u8>>, QmkError>; replaces the temporary .0 discard
        landed here with real reply propagation."
  - P1.M3.T3.S1 (run dispatch): "hands the propagated reply to parse_reply and
        returns the real CommandResponse."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified. Do NOT:
    * change try_send_once's or send_raw_report's SIGNATURE (only the call site
      gets .0; their return types are P1.M3.T2.S1).
    * touch run() in lib.rs (still calls send_raw_report(...)? and returns
      placeholders — P1.M3.T3.S1 evolves it).
    * add any #[test] (burst_to_one needs real hardware; verify via build + the
      unchanged 57-test suite).
    * alter the write loop body, the drain loop, IN_DRAIN_MAX, REPORT_LENGTH, or
      PAYLOAD_PER_REPORT.
    * touch lib.rs, error.rs, main.rs, or Cargo.toml.
    * add burst_to_one / REPLY_READ_TIMEOUT_MS to any pub re-export (both internal).
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings. The .0 call-site fix
# is what makes this pass (without it, try_send_once won't type-check).
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.

# Lint (default clippy — no clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors. Tuple return + `.0` access + the match arm are
# clippy-clean under defaults.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 57 passed; 0 failed; 0 ignored; ..." — UNCHANGED
# from the verified baseline (core.rs 37 + lib.rs 20). This subtask adds NO tests.
#
# WHY no new tests: burst_to_one takes a live &HidDevice and performs real HID
# I/O — it cannot be constructed/driven without hardware. The item explicitly
# says verify via compilation + the existing suite still passing.

# Sanity: the dispatch-path tests still reach send_raw_report and deterministically
# return DeviceNotFound (bogus VID/PID 0xDEAD/0xBEEF matches nothing) — confirms
# the call-site .0 change didn't disturb the send path.
cargo test --lib test_run_query_info_dispatches_to_send -- --nocapture
cargo test --lib test_run_apply_host_context_dispatches_to_send -- --nocapture
# Expected: both pass (DeviceNotFound — the reply read is never reached because
# no device matches).

# Sanity: the typed-command + parse_reply suites still pass (these exercise the
# pure functions, which are untouched).
cargo test --lib parse_reply -- --nocapture
cargo test --lib build_typed_payload -- --nocapture
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
burst_to_one performs real HID read/write against a physical QMK keyboard; there
is no hardware in CI and no HID mock in the crate. The bounded reply read is
therefore verified INDIRECTLY: (1) the crate COMPILES with the new tuple return
and the `.0` call-site fix (Level 1), and (2) the existing 57 tests still pass
(Level 2), proving the write loop, drain loop, and dispatch path were not
disturbed. A live-hardware smoke test (capture a QUERY_INFO reply) belongs to
P1.M3.T3.S1 once run() plumbs the reply through to parse_reply.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the signature change landed exactly once (not duplicated).
grep -nE "fn burst_to_one" src/core.rs
# Expected: exactly ONE match, with the `-> (bool, Option<Vec<u8>>)` return.

# Confirm the write-error early return is the tuple form.
grep -nE "return \(false, None\);" src/core.rs
# Expected: exactly ONE match (inside burst_to_one).

# Confirm the success return is the tuple form.
grep -nE "\(true, reply\)" src/core.rs
# Expected: exactly ONE match (the final line of burst_to_one).

# Confirm the bounded reply read uses REPLY_READ_TIMEOUT_MS (not 0) and sits
# before the drain (which still uses 0).
grep -nE "read_timeout\(&mut (read_buf|drain_buf)," src/core.rs
# Expected: TWO matches — read_buf with REPLY_READ_TIMEOUT_MS, drain_buf with 0.

# Confirm the call-site .0 fix landed.
grep -nE "burst_to_one\(interface, data, batch_count, verbose\)\.0" src/core.rs
# Expected: exactly ONE match (inside try_send_once).

# Confirm REPLY_READ_TIMEOUT_MS no longer carries #[allow(dead_code)].
grep -nB1 "const REPLY_READ_TIMEOUT_MS" src/core.rs
# Expected: the line ABOVE the const is its `///` doc, NOT `#[allow(dead_code)]`.

# Confirm burst_to_one is NOT dead code (it's called) and has no stray allow.
grep -nE "#\[allow\(dead_code\)\]" src/core.rs
# Expected: the parse_reply allow (src/core.rs:408) remains (its live caller is
# P1.M3.T3); the REPLY_READ_TIMEOUT_MS allow is GONE. No new allows added.

# Full-crate final gate (the number that proves the task done).
cargo test --lib 2>&1 | tail -3
# Expected: "test result: ok. 57 passed; 0 failed; ..."
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1 passed: `cargo build` → 0 warnings.
- [ ] Level 1 passed: `cargo clippy --lib` → 0 warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → **57 passed; 0 failed** (unchanged
      baseline — no new tests).

### Feature Validation
- [ ] `burst_to_one` returns `(bool, Option<Vec<u8>>)`: `(false, None)` on write
      error; `(true, reply)` on success.
- [ ] The bounded reply read (`read_timeout(.., REPLY_READ_TIMEOUT_MS)`) sits
      AFTER the write loop and BEFORE the drain loop.
- [ ] The drain loop is byte-identical to before (still `read_timeout(.., 0)`,
      still bounded by `IN_DRAIN_MAX`).
- [ ] The reply-capture guard is `Ok(n) if n > 0 =>` (treats `Ok(0)` timeout AND
      `Err` failure as no-reply via the `_ =>` arm).
- [ ] Verbose logging emits the captured reply length (`n` bytes) on `n > 0`.
- [ ] `try_send_once`'s call site compiles via `.0` (reply discarded; propagation
      deferred to P1.M3.T2.S1).
- [ ] `REPLY_READ_TIMEOUT_MS` has no `#[allow(dead_code)]`; its comment block is
      corrected.
- [ ] The crate COMPILES (the item's "may not compile" note is resolved by step f).

### Code Quality Validation
- [ ] Only `src/core.rs` modified; no other file touched.
- [ ] The write loop body is byte-identical except `return false` → `return (false, None)`.
- [ ] `burst_to_one`'s doc comment documents the return tuple + reply capture.
- [ ] No new tests, no new deps, no public-API change, no new constants.

### Documentation & Deployment
- [ ] Doc comment explains the `(bool, Option<Vec<u8>>)` return + the bounded
      capture + the drain-ordering rationale.
- [ ] The temporary `.0` discard is documented in a comment at the call site
      (points to P1.M3.T2.S1).
- [ ] No README/PRD/Cargo.toml change (item DOCS = "none — internal function
      change"; no user-facing surface).

---

## Anti-Patterns to Avoid

- ❌ Don't leave the crate non-compiling "because the item said it may not
  compile." Step (f) IS the compile fix (`.0` at the call site). The PRP requires
  `cargo build` + `cargo test --lib` to pass. The reply is discarded at the call
  site ON PURPOSE; full propagation is P1.M3.T2.S1.
- ❌ Don't change `try_send_once`'s SIGNATURE or `send_raw_report`'s signature —
  only the internal `burst_to_one(...)` call gets `.0`. The
  `Result<(SendOutcome, Option<Vec<u8>>), QmkError>` / `Result<Option<Vec<u8>>, QmkError>`
  evolutions are P1.M3.T2.S1.
- ❌ Don't touch `run()` in lib.rs — it still calls `send_raw_report(...)?` and
  returns placeholders. P1.M3.T3.S1 evolves it.
- ❌ Don't write `Ok(_) => ...` or `Ok(n) => ...` for the reply capture — use the
  EXACT `Ok(n) if n > 0 =>` guard (so `Ok(0)` timeout is treated as no-reply, not
  a 0-byte reply). Mirror the drain loop's idiom, only swapping `0` →
  `REPLY_READ_TIMEOUT_MS`.
- ❌ Don't drain BEFORE the bounded reply read — that would consume the very reply
  you're trying to capture. Order is: write loop → bounded capture → drain.
- ❌ Don't change the drain loop's `read_timeout(.., 0)` — the drain stays
  non-blocking. Only the NEW reply read uses `REPLY_READ_TIMEOUT_MS`.
- ❌ Don't alter the write loop body (the `request_data` setup, per-batch fill,
  verbose log, `interface.write` call) — only the error arm's `return` changes.
- ❌ Don't add a `#[test]` that calls `burst_to_one` — it can't construct a
  `HidDevice` without hardware. Verify via compilation + the unchanged 57-test suite.
- ❌ Don't add defensive `.get()` indexing for `read_buf[..n]` — `read_timeout`
  guarantees `n ≤ read_buf.len()`, so the slice is always in bounds. (This is
  unlike `parse_reply`, where the FIRMWARE buffer can be short.)
- ❌ Don't leave `#[allow(dead_code)]` on `REPLY_READ_TIMEOUT_MS` — it now has a
  consumer (`burst_to_one`). Remove it AND correct the constants comment block.
- ❌ Don't touch `lib.rs`, `error.rs`, `main.rs`, or `Cargo.toml`.
- ❌ Don't add `burst_to_one` or `REPLY_READ_TIMEOUT_MS` to any `pub use` — both
  are internal (`burst_to_one` private; `REPLY_READ_TIMEOUT_MS` private const).
- ❌ Don't use `Ok(0)` as an error or `unwrap()` on the read — the `_ => {}` arm
  silently absorbs timeout/failure and leaves `reply = None`, which is the correct
  "no reply" semantic for the caller.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a small, surgical edit to ONE file with the exact current code (verbatim,
`src/core.rs:248`), the exact call site (`src/core.rs:~215`), the exact target
body, the exact `.0` compile fix, the exact `read_timeout` contract, and the exact
doc/comment corrections ALL given verbatim above and in research `notes.md`. The
only subtlety — the item's "may not compile" note — is explicitly reconciled (step
f = `.0` = compile fix) so the implementer isn't misled into shipping a broken
build. The baseline (57 passing, **verified by `cargo test --lib`**) and all four
validation commands are confirmed working in this repo. The change is mechanical
(no new tests, no new types, no new deps, no public-API change) and the existing
dispatch tests (bogus VID/PID ⇒ `DeviceNotFound`) prove the call-site `.0` change
doesn't disturb the send path. The residual risk — accidentally editing the write
or drain loop body — is eliminated by giving the verbatim current + target code
side-by-side and a Level-4 grep suite that pins each change to exactly one match.