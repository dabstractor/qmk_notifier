name: "P1.M3.T2.S1 — Propagate first reply through try_send_once and send_raw_report"
description: "Change try_send_once's return type to Result<(SendOutcome, Option<Vec<u8>>), QmkError> (capture the FIRST successful device's reply) and send_raw_report's return type to Result<Option<Vec<u8>>, QmkError> (return that reply). The reply was already captured by burst_to_one in P1.M3.T1.S1 and discarded via .0; this subtask plumbs it up two more levels so run() (P1.M3.T3.S1) can hand it to parse_reply. Public API change to send_raw_report (re-exported in lib.rs). ONLY src/core.rs is modified; lib.rs compiles unchanged (empirically verified). No new tests (hardware-bound send path)."

---

## Goal

**Feature Goal**: Plumb the FIRST device reply — already captured by `burst_to_one`
in P1.M3.T1.S1 (and currently discarded at the `try_send_once` call site via `.0`)
— up two more levels of the send call stack so it reaches `run()`:

```
burst_to_one      -> (bool, Option<Vec<u8>>)   [DONE in P1.M3.T1.S1]
try_send_once     -> Result<(SendOutcome, Option<Vec<u8>>), QmkError>   [THIS TASK]
send_raw_report   -> Result<Option<Vec<u8>>, QmkError>                  [THIS TASK]
run()             consumes the reply via parse_reply                    [P1.M3.T3.S1]
```

`try_send_once` captures the reply from the FIRST device whose burst-write
succeeded (first-success-wins; matches `transport_evolution.md` §Key Design
Decisions #4) and returns it alongside the existing `SendOutcome`. `send_raw_report`
returns that reply as `Ok(Some(bytes))` on a clean send, or `Ok(None)` when no
device replied within `REPLY_READ_TIMEOUT_MS` (timeout / read failure / legacy
device). Transport errors remain `Err(QmkError::...)` exactly as before.

**Deliverable**: A surgical edit to **`src/core.rs` ONLY**:
1. `try_send_once` signature `-> Result<(SendOutcome, Option<Vec<u8>>), QmkError>`;
   the device loop destructures `burst_to_one`'s tuple, captures the first
   successful reply into a new `first_reply`, and returns the `(outcome,
   first_reply)` tuple. The `.0` discard landed in P1.M3.T1.S1 is removed.
2. `send_raw_report` signature `-> Result<Option<Vec<u8>>, QmkError>`; the retry
   loop's `match` destructures the tuple (`(SendOutcome::AllSucceeded, reply) =>
   return Ok(reply)`; all other arms bind `_` for the reply). The existing verbose
   retry-log block is PRESERVED.
3. ADD a doc comment above `send_raw_report` documenting the new
   `Result<Option<Vec<u8>>, QmkError>` return (there is no doc comment today).

**Success Definition**: `cargo build` → 0 warnings; `cargo clippy --lib` → 0
warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **57 passed; 0
failed** (UNCHANGED baseline — NO new tests; the send path needs real HID
hardware and is verified by compilation + the existing suite). The full call stack
is now type-consistent end-to-end (`burst_to_one` → `try_send_once` →
`send_raw_report`). `lib.rs` compiles UNCHANGED (empirically verified — see
*Gotchas*).

## User Persona (if applicable)

**Target User**: The v0.3.0 transport dispatch path. P1.M3.T1.S1 made
`burst_to_one` the **producer** of the first reply but discarded it at the
`try_send_once` call site (`.0`). This subtask makes `send_raw_report` the
**deliverer** of that reply to `run()`. The immediate consumer is **P1.M3.T3.S1**
(`run()` hands the bytes to `parse_reply` → `CommandResponse`).

**Use Case**: A typed-capable keyboard receives `QUERY_INFO`, bursts back a
`[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]`
reply. `burst_to_one` captures those bytes; this subtask carries them through
`try_send_once` → `send_raw_report` → back to `run()`, which (in P1.M3.T3.S1)
decodes them into `CommandResponse::Info { … }`. A legacy device that sends no
reply within the bounded timeout yields `Ok(None)` all the way up ⇒ `run()`
maps it to `CommandResponse::Timeout` and the host stays in string-only mode
(PRD §8).

**Pain Points Addressed**: The reply-capture chain was broken at the middle
(`try_send_once` discarded `burst_to_one`'s reply via `.0`); typed replies could
be captured but never reached a caller. This subtask completes the plumbing from
the capture site up to the crate's public send entrypoint.

## Why

- **PRD §8 (Response Handling)** + **§14 invariant 6**: the host must
  disambiguate `0x51` (typed) from `0`/`1` (legacy match-bool) from no-reply
  (`Timeout`). That needs the raw reply bytes delivered to the caller.
  `parse_reply` (P1.M2.T2, complete) can't run on bytes that never leave
  `burst_to_one`. This subtask is the conveyor.
- **architecture/transport_evolution.md §Signature Changes (4,5)** gives the
  EXACT before/after signatures for `try_send_once` and `send_raw_report` — this
  PRP transcribes them faithfully. §Key Design Decisions #4 pins first-success-wins.
- **PRD §3 (Public API)** lists `send_raw_report` in the `lib.rs` re-export —
  changing its return type is an intentional public-API evolution (item 3c).
- **Dependency-chain integrity**: this is the middle of the reply-capture chain.
  Landing the propagation here — while leaving `run()` (P1.M3.T3.S1) to consume
  the reply — lets P1.M3.T3.S1 build on a known-good deliverer without
  re-touching `send_raw_report`/`try_send_once`.

## What

### 1. `try_send_once` — capture + propagate first reply (src/core.rs:170-237)
- Change the return type to `Result<(SendOutcome, Option<Vec<u8>>), QmkError>`.
- Add `let mut first_reply: Option<Vec<u8>> = None;` alongside the
  `succeeded`/`failed` counters (OUTER scope — it's read after the device block).
- Replace the `.0` call site with a destructure: `let (success, reply) =
  burst_to_one(interface, data, batch_count, verbose);` then `if success {` and,
  inside the success arm, `if first_reply.is_none() { first_reply = reply; }`.
- Keep ALL existing verbose logging (device path, failed-to-send message),
  cache-invalidation block, and the `SendOutcome` computation UNCHANGED.
- Return `Ok((outcome, first_reply))` where `outcome` is the existing
  `if succeeded == 0 { TotalFailure } else if failed > 0 { Partial } else {
  AllSucceeded }` value (bind it to a `let outcome = ...;` then return the tuple).

### 2. `send_raw_report` — return the propagated reply (src/core.rs:106-153)
- Change the return type to `Result<Option<Vec<u8>>, QmkError>`.
- The retry-loop `match` arms gain tuple destructuring:
  `(SendOutcome::AllSucceeded, reply) => return Ok(reply)` (was `return Ok(())`);
  `(SendOutcome::Partial { succeeded, failed }, _) =>` (unchanged error);
  `(SendOutcome::TotalFailure, _) if attempt < SEND_RETRIES =>` (PRESERVE the
  verbose retry-log block, then `continue;`);
  `(SendOutcome::TotalFailure, _) =>` (unchanged error).
- The trailing `unreachable!(...)` stays.
- ADD a doc comment above the function (see *Implementation Patterns* §C).

### 3. `lib.rs` — NOT touched (compiles unchanged)
- `run()` calls `send_raw_report(...)?;` at lib.rs:336 and lib.rs:368. After the
  signature change, the `?` evaluates to `Option<Vec<u8>>` (a `#[must_use]`
  type) which the statement discards. **Empirically verified** (see research
  `notes.md` F3) this produces NO `unused_must_use` warning and NO clippy
  warning — the `?` operator counts as a use. So `lib.rs` needs no edit to
  compile. `run()` still returns `CommandResponse` placeholders; reply
  *consumption* is P1.M3.T3.S1 (which also refreshes the now-stale inline comment
  at lib.rs:332).

### Success Criteria
- [ ] `try_send_once` returns `Result<(SendOutcome, Option<Vec<u8>>), QmkError>`;
      the FIRST successful device's reply is captured into `first_reply` (later
      successes' replies are dropped); failures still increment `failed` and
      invalidate the cache exactly as before.
- [ ] `send_raw_report` returns `Result<Option<Vec<u8>>, QmkError>`; on
      `AllSucceeded` it returns `Ok(reply)` (the first-success reply, possibly
      `None`); transport errors are unchanged.
- [ ] The verbose retry-log block in `send_raw_report` is PRESERVED (NOT dropped).
- [ ] `send_raw_report` carries a doc comment documenting the `Option<Vec<u8>>`
      return semantics (Some = first reply; None = no reply captured).
- [ ] `cargo build` → 0 warnings; `cargo clippy --lib` → 0 warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → **57 passed; 0 failed**.
- [ ] ONLY `src/core.rs` is modified (lib.rs/error.rs/main.rs/Cargo.toml untouched).
- [ ] No new tests added.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact current
> `try_send_once` (verbatim, src/core.rs:170-237) and `send_raw_report` (verbatim,
> src/core.rs:106-153) bodies are reproduced in research `notes.md` F1/F2 with the
  change-points annotated. The exact target bodies are given in F7. The
  empirical proof that `lib.rs` compiles unchanged (F3), the verbose-println
  preservation gotcha (F4), and the doc-comment-is-an-ADD fact (F5) are all
  captured. The baseline (57 passing, verified by `cargo test --lib`) and all
  four validation commands are confirmed working in this repo. The call-site
  `.0` from P1.M3.T1.S1 (the starting point) is shown at src/core.rs:207.

### Documentation & References

```yaml
# MUST READ — the ONLY file edited
- file: src/core.rs
  why: "Holds send_raw_report (src/core.rs:106, -> Result<(), QmkError>) and
        try_send_once (src/core.rs:170, -> Result<SendOutcome, QmkError>); the
        .0 discard call site to be replaced (src/core.rs:207); the retry match
        block (src/core.rs:128-152). All exact current text is in research
        notes.md F1/F2 with change-points annotated."
  pattern: "send_raw_report's retry loop matches on try_send_once's result; the
            TotalFailure-if-attempt<SEND_RETRIES arm carries a verbose println
            block that MUST be preserved. try_send_once iterates cached
            HidDevice handles, counts succeeded/failed, invalidates the cache on
            any failure, and computes the SendOutcome."
  gotcha: "send_raw_report has NO existing doc comment (src/core.rs:107 is the
           fn directly) — the DOCS step is an ADD. lib.rs's pub use of
           send_raw_report (lib.rs:4) needs NO change (name re-export; signature
           follows)."

# MUST READ — research notes: verbatim current code + designed target + the
# empirically-verified lib.rs-unchanged proof + the verbose-println gotcha
- docfile: plan/001_b92a9b2b603f/P1M3T2S1/research/notes.md
  why: "F1 = verbatim current send_raw_report; F2 = verbatim current
        try_send_once; F3 = EMPIRICAL PROOF lib.rs compiles unchanged (the
        #[must_use] Option discarded by `?` triggers no warning); F4 = CRITICAL
        verbose-println-preservation gotcha; F5 = doc-comment-is-an-ADD; F6 =
        run() call sites that make lib.rs compile-unchanged; F7 = designed
        target bodies; F8 = first-success-wins semantics; F9 = validation gate."
  section: "F1, F2, F7 (the core edits), F3, F4 (gotchas), F8 (semantics)"

# MUST READ — the architecture doc with the EXACT before/after signatures
- docfile: plan/001_b92a9b2b603f/architecture/transport_evolution.md
  why: "§Signature Changes items 4 (try_send_once) and 5 (send_raw_report) pin
        the new signatures verbatim. §Key Design Decisions #4 pins
        first-success-wins for the captured reply. §Data Flow Comparison (v0.3.0)
        shows the full consistent call stack this subtask completes the middle of."
  section: "Signature Changes (4,5)", "Key Design Decisions (#4)", "Data Flow
            Comparison (v0.3.0)"

# REFERENCE — the previous subtask that produced the input contract (burst_to_one
# now returns (bool, Option<Vec<u8>>) and discards via .0 at the call site)
- docfile: plan/001_b92a9b2b603f/P1M3T1S1/PRP.md
  why: "Defines the .0 discard landed at src/core.rs:207 — the EXACT starting
        point for this subtask's try_send_once edit. The comment block at the
        call site ('burst_to_one now returns (bool, Option<Vec<u8>>) ... PROPAGATED
        up through try_send_once / send_raw_report / run() in P1.M3.T2.S1') is
        the one THIS task replaces with real propagation."

# REFERENCE — PRD framing + the invariants this change serves
- file: PRD.md
  why: "§8 (Response Handling) + §14 invariant 6 (reply disambiguation) explain
        WHY the reply must reach the caller; §3 (Public API) lists send_raw_report
        in the re-export (so changing its return type is an intentional public-API
        evolution — item 3c)."
  section: "8. Response Handling", "14. Key Invariants (4, 6)", "3. Public API"

# REFERENCE — the hidapi read_timeout contract (drives the Option<Vec<u8>>
# semantics downstream, not changed here but documents why None means timeout)
- docfile: plan/001_b92a9b2b603f/architecture/external_deps.md
  why: "§read_timeout semantics: Ok(0)=timeout/no-data (NOT an error), Ok(n>0)=data.
        This is why a None reply (timeout in burst_to_one) propagates as Ok(None)
        rather than Err — the caller distinguishes 'sent but no reply' from
        'send failed'."
  section: "read_timeout semantics (critical for reply capture)"

# REFERENCE — the next subtask that consumes this output
- docfile: plan/001_b92a9b2b603f/P1M3T3S1/PRP.md
  why: "(May not exist yet at research time.) Will evolve run() to destructure
        send_raw_report's Result<Option<Vec<u8>>, QmkError> and hand Some(bytes)
        to parse_reply, and will refresh the now-stale lib.rs:332 comment. This
        task's job is to MAKE that reply available; it does not touch run()."
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
    ├── core.rs         # <-- FILE TO EDIT (send_raw_report, try_send_once,
    │                   #     the .0 call site, add send_raw_report doc comment)
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # run() + public types — DO NOT TOUCH (compiles unchanged)
```

### Desired Codebase tree with files to be modified

```bash
src/
└── core.rs   # MODIFIED ONLY:
              #   (1) try_send_once: -> Result<(SendOutcome, Option<Vec<u8>>), QmkError>;
              #       destructure burst_to_one tuple; capture first_reply; return
              #       (outcome, first_reply). Remove the .0 discard + its comment.
              #   (2) send_raw_report: -> Result<Option<Vec<u8>>, QmkError>; tuple-
              #       match arms ((AllSucceeded, reply) => Ok(reply)); PRESERVE the
              #       verbose retry-log block.
              #   (3) ADD doc comment above send_raw_report (Option<Vec<u8>> return).
# (lib.rs, error.rs, main.rs, Cargo.toml unchanged; NO new files; NO new tests)
```

> No new files, no new tests, no new deps. One file modified (`src/core.rs`).

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (lib.rs compiles UNCHANGED — empirically proven): run() calls
//   send_raw_report(...)?; as a statement at lib.rs:336 and lib.rs:368. After the
//   return type becomes Result<Option<Vec<u8>>, QmkError>, the `?` evaluates to
//   Option<Vec<u8>> (a #[must_use] type) which the statement discards. Verified
//   in /tmp/must_use_test (clean cargo build + RUSTFLAGS="-W unused" + clippy):
//   NO unused_must_use warning fires — the `?` operator counts as a use of the
//   Result; the Ok payload is consumed by control flow. So lib.rs needs NO edit
//   to compile. Do NOT touch lib.rs. (The stale inline comment at lib.rs:332 and
//   the run() doc-comment placeholder notes are refreshed in P1.M3.T3.S1, which
//   owns run().)

// CRITICAL (PRESERVE the verbose retry-log block): the item's pseudocode for the
//   send_raw_report retry arm shows `(SendOutcome::TotalFailure, _) if attempt <
//   SEND_RETRIES => { continue; }` — which DROPS the existing verbose println
//   ("All sends failed; rebuilding device cache and retrying (attempt {}/{}).").
//   That would be a silent behavior regression (rebuild messages vanish under
//   --verbose). PRESERVE the `if verbose { println!(...); }` block in the retry
//   arm; only the match-arm HEAD changes (add `, _` tuple binding).

// CRITICAL (first-success-wins, not last): capture the reply from the FIRST device
//   whose burst succeeded: `if first_reply.is_none() { first_reply = reply; }`.
//   Subsequent successful devices' replies are dropped. This matches
//   transport_evolution.md §Key Design Decisions #4 and is correct for the
//   single-keyboard deployment. Do NOT overwrite first_reply on later successes.

// CRITICAL (None reply propagates as Ok(None), NOT Err): burst_to_one returns
//   (true, None) when the write succeeded but no reply arrived within
//   REPLY_READ_TIMEOUT_MS (timeout / read failure / legacy device). That None
//   must flow up as Ok(None) — the caller distinguishes "sent but no reply" from
//   "send failed". Do NOT convert None into an error. (external_deps.md
//   §read_timeout semantics: Ok(0)=timeout is NOT an error.)

// GOTCHA (doc comment is an ADD, not an edit): src/core.rs:107 is
//   `pub fn send_raw_report(` directly — there is NO existing `///` block. The
//   item's DOCS step ("Update the send_raw_report doc comment") means ADD a new
//   doc comment documenting the Option<Vec<u8>> return. See Implementation
//   Patterns §C for the exact text.

// GOTCHA (no `let_and_return` clippy issue): in try_send_once, write
//   `let outcome = if succeeded == 0 { ... } else { ... }; Ok((outcome,
//   first_reply))`. The `outcome` binding is used inside a tuple (not directly
//   returned), so clippy's `let_and_return` does NOT fire. Verified-safe under
//   default clippy. Do NOT inline as `Ok((if.. {..} else {..}, first_reply))`
//   (uglier, and unnecessary).

// GOTCHA (the retry loop's `unreachable!` stays correct): after the match arms
//   change, the loop still always returns on its first iteration (AllSucceeded /
//   Partial) or its final iteration (TotalFailure after SEND_RETRIES). The
//   `unreachable!("the retry loop always returns on its first or final
//   iteration")` tail is still accurate. Do NOT remove it.

// GOTCHA (no new tests): try_send_once and send_raw_report take a live
//   &HidDevice / real HID bus and cannot be unit-tested without hardware. The
//   existing dispatch tests (test_run_*_dispatches_to_send) deterministically
//   return DeviceNotFound (bogus VID/PID 0xDEAD/0xBEEF fails in ensure_cache
//   BEFORE any reply is read), so they're unaffected by the propagation change.
//   Verify via build + the unchanged 57-test suite.

// NOTE (the .0 comment block gets removed, not "updated"): src/core.rs:~200-206
//   has a multi-line comment ("burst_to_one now returns (bool, Option<Vec<u8>>).
//   ... PROPAGATED up through try_send_once / send_raw_report / run() in
//   P1.M3.T2.S1. Until then we take only the success flag via .0 ..."). This
//   subtask IS P1.M3.T2.S1 — that comment + the `.0` are REPLACED by real
//   propagation. Remove the comment entirely (or replace with a one-liner noting
//   the reply is now captured into first_reply); do not leave the stale "until
//   then" text.

// NOTE (no rustfmt.toml / no clippy.toml ⇒ defaults): the tuple return type, the
//   `(SendOutcome::AllSucceeded, reply)` / `(SendOutcome::Partial{..}, _)` /
//   `(SendOutcome::TotalFailure, _)` match arms, the `let (success, reply) = ...`
//   destructure, and `first_reply` are all fmt/clippy-clean under defaults. Run
//   `cargo fmt` then `cargo clippy --lib` to confirm 0 warnings.
```

## Implementation Blueprint

### Data models and structure
No new types, enums, structs, or constants. `SendOutcome` (src/core.rs:158) is
UNCHANGED — only its wrapping into a `(SendOutcome, Option<Vec<u8>>)` tuple by
`try_send_once` changes. The reply type is a built-in `Option<Vec<u8>>`. No
public API type changes beyond `send_raw_report`'s return signature. No state, no
globals, no new deps.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY src/core.rs — try_send_once signature + reply capture
  - LOCATE: fn try_send_once at src/core.rs:170 (signature `-> Result<SendOutcome, QmkError>`).
  - CHANGE signature to:
        fn try_send_once(
            key: &MatchKey,
            data: &[u8],
            batch_count: usize,
            verbose: bool,
        ) -> Result<(SendOutcome, Option<Vec<u8>>), QmkError>
  - ADD (alongside the succeeded/failed counters, OUTER scope so it's readable
        after the device block):
        let mut first_reply: Option<Vec<u8>> = None;
  - REPLACE the call site (src/core.rs:~200-210) — remove the multi-line "until
    then" comment AND the `.0`; destructure + capture:
        let (success, reply) = burst_to_one(interface, data, batch_count, verbose);
        if success {
            succeeded += 1;
            if first_reply.is_none() {
                first_reply = reply;   // first successful device wins
            }
        } else {
            failed += 1;
            if verbose {
                println!("Failed to send message to device {}/{}.", device_idx + 1, device_count);
            }
        }
  - KEEP UNCHANGED: the device-path verbose log (the `if verbose { ... get_device_info
    ... }` block above the call site), the cache-invalidation `if failed > 0 { *cache
    = None; ... }` block.
  - CHANGE the return: bind the outcome then return the tuple:
        let outcome = if succeeded == 0 {
            SendOutcome::TotalFailure
        } else if failed > 0 {
            SendOutcome::Partial { succeeded, failed }
        } else {
            SendOutcome::AllSucceeded
        };
        Ok((outcome, first_reply))
  - NAMING: `first_reply: Option<Vec<u8>>`, `success: bool`, `reply: Option<Vec<u8>>`
    (the burst_to_one tuple arms).
  - DEPENDENCIES: consumes `burst_to_one`'s new `(bool, Option<Vec<u8>>)` return
    (landed in P1.M3.T1.S1).
  - PLACEMENT: same location (src/core.rs); the function does not move.

Task 2: MODIFY src/core.rs — send_raw_report signature + tuple match
  - LOCATE: pub fn send_raw_report at src/core.rs:106 (signature `-> Result<(), QmkError>`).
  - CHANGE signature to `-> Result<Option<Vec<u8>>, QmkError>`.
  - CHANGE the match arms (src/core.rs:128-152) to destructure the tuple returned
    by try_send_once:
        match try_send_once(&key, data, batch_count, verbose)? {
            (SendOutcome::AllSucceeded, reply) => return Ok(reply),
            (SendOutcome::Partial { succeeded, failed }, _) => {
                return Err(QmkError::PartialSendError { succeeded, failed });
            }
            (SendOutcome::TotalFailure, _) if attempt < SEND_RETRIES => {
                if verbose {
                    println!(
                        "All sends failed; rebuilding device cache and retrying (attempt {}/{}).",
                        attempt + 2,
                        SEND_RETRIES + 1
                    );
                }
                continue;
            }
            (SendOutcome::TotalFailure, _) => {
                return Err(QmkError::SendReportError(hidapi::HidError::HidApiError {
                    message: "Failed to send to any devices".to_string(),
                }));
            }
        }
  - PRESERVE: the key/batch_count/verbose-println block above the loop (UNCHANGED),
    the verbose retry-log block inside the TotalFailure-if arm (UNCHANGED — do NOT
    drop it even though the item's pseudocode omits it), and the trailing
    `unreachable!(...)` (UNCHANGED).
  - DO NOT: change the retry-loop structure (`for attempt in 0..=SEND_RETRIES`),
    the verbose request-data log, or any error variant/wording.

Task 3: MODIFY src/core.rs — ADD send_raw_report doc comment
  - LOCATE: src/core.rs:107 — the line `pub fn send_raw_report(` has NO `///`
    block above it (the preceding lines are the `SEND_RETRIES` const's doc).
  - INSERT the doc comment from "Implementation Patterns" §C directly above
    `pub fn send_raw_report(`.
  - DOCUMENT: what the function does; the NEW `Result<Option<Vec<u8>>, QmkError>`
    return (Some = first device reply from burst_to_one's bounded read; None = no
    reply within REPLY_READ_TIMEOUT_MS / legacy device; Err = transport failure);
    that the reply is decoded downstream by `parse_reply` (P1.M3.T3.S1); that
    transport errors/retry semantics are unchanged.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, `cargo clippy --lib`, `cargo fmt --check`,
    `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0; 57 passed 0
    failed. If any existing test FAILS, the retry-loop or try_send_once logic was
    accidentally altered beyond the tuple plumbing — re-diff against the verbatim
    current code (notes.md F1/F2) and fix.
```

### Implementation Patterns & Key Details

#### §A — the new `try_send_once` device loop + return (full, with unchanged parts abbreviated)

```rust
fn try_send_once(
    key: &MatchKey,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> Result<(SendOutcome, Option<Vec<u8>>), QmkError> {
    let mut cache = lock_cache();
    ensure_cache(&mut cache, key, verbose)?;

    let device_count = cache.as_ref().expect("cache populated").devices.len();
    if verbose {
        println!("Found {} matching device(s).", device_count);
    }

    let mut succeeded = 0usize;
    let mut failed = 0usize;
    let mut first_reply: Option<Vec<u8>> = None; // NEW: first-success reply

    {
        let devices: &Vec<HidDevice> = &cache.as_ref().expect("cache populated").devices;
        for (device_idx, interface) in devices.iter().enumerate() {
            if verbose {
                // ── UNCHANGED device-path verbose log ──
                let device_path = match interface.get_device_info() {
                    Ok(info) => format!("{:?}", info.path()),
                    Err(_) => "N/A".to_string(),
                };
                println!(
                    "Sending to device {}/{}: Path: {}",
                    device_idx + 1,
                    device_count,
                    device_path
                );
            }

            // ── CHANGED: was `burst_to_one(...).0`; now destructure + capture ──
            let (success, reply) = burst_to_one(interface, data, batch_count, verbose);
            if success {
                succeeded += 1;
                if first_reply.is_none() {
                    first_reply = reply; // first successful device wins (transport_evolution.md §KDD #4)
                }
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
        }
    }

    // ── UNCHANGED cache invalidation ──
    if failed > 0 {
        *cache = None;
        if verbose {
            println!("Invalidating device cache after a write error.");
        }
    }

    // ── CHANGED: bind outcome, return the tuple ──
    let outcome = if succeeded == 0 {
        SendOutcome::TotalFailure
    } else if failed > 0 {
        SendOutcome::Partial { succeeded, failed }
    } else {
        SendOutcome::AllSucceeded
    };
    Ok((outcome, first_reply))
}
```

#### §B — the new `send_raw_report` retry loop (full)

```rust
    // ── UNCHANGED: key / batch_count / verbose request-data log ──
    let key = MatchKey {
        vendor_id,
        product_id,
        usage_page,
        usage,
    };
    let batch_count = batches_for(data);

    if verbose {
        println!("Request data ({} bytes):", data.len());
        println!("{:?}", data);
    }

    for attempt in 0..=SEND_RETRIES {
        match try_send_once(&key, data, batch_count, verbose)? {
            (SendOutcome::AllSucceeded, reply) => return Ok(reply),
            (SendOutcome::Partial { succeeded, failed }, _) => {
                return Err(QmkError::PartialSendError { succeeded, failed });
            }
            (SendOutcome::TotalFailure, _) if attempt < SEND_RETRIES => {
                // PRESERVE this verbose block (item pseudocode dropped it):
                if verbose {
                    println!(
                        "All sends failed; rebuilding device cache and retrying (attempt {}/{}).",
                        attempt + 2,
                        SEND_RETRIES + 1
                    );
                }
                continue;
            }
            (SendOutcome::TotalFailure, _) => {
                return Err(QmkError::SendReportError(hidapi::HidError::HidApiError {
                    message: "Failed to send to any devices".to_string(),
                }));
            }
        }
    }

    unreachable!("the retry loop always returns on its first or final iteration")
```

#### §C — the NEW `send_raw_report` doc comment (ADD above the fn)

```rust
/// Burst-send `data` to every raw-HID interface matching the VID/PID/usage-page/
/// usage predicate, retrying the whole send (with a cache rebuild) once on a
/// total failure. Returns the FIRST device reply captured by the burst-write path.
///
/// # Return
/// - `Ok(Some(bytes))` — every matched device accepted the burst AND at least the
///   first device replied within [`REPLY_READ_TIMEOUT_MS`] (the bounded read in
///   [`burst_to_one`]). `bytes` is that first reply's raw IN report (up to
///   [`REPORT_LENGTH`] + 1 bytes). Decode it downstream via `parse_reply`
///   (P1.M3.T3.S1) into a [`crate::CommandResponse`] (PRD §8, §10.2).
/// - `Ok(None)` — the burst succeeded but NO device replied within
///   [`REPLY_READ_TIMEOUT_MS`] (timeout / read failure / a legacy device that
///   sends no typed reply). The caller treats `None` as a non-capable device and
///   stays in string-only mode (PRD §10.2, §8).
/// - `Err(QmkError::DeviceNotFound)` — no interface matched the predicate.
/// - `Err(QmkError::PartialSendError { succeeded, failed })` — some devices
///   accepted the burst, some did not. A partial send is NEVER retried (PRD §14
///   invariant 4), so any captured reply is discarded on this path.
/// - `Err(QmkError::SendReportError(..))` — every device failed after exhausting
///   retries.
///
/// `data` carries ONLY the payload after the `0x81 0x9F` magic header:
/// [`burst_to_one`] prepends that header (and the leading report-ID byte) per
/// 33-byte report. For legacy strings the caller appends the `0x03` ETX terminator
/// first; for typed commands `build_typed_payload` produces the `[0xF0][cmd][args]
/// [0x03]` payload (PRD §4, §10.1). Multi-report burst-write and the device cache
/// are shared by all command types.
pub fn send_raw_report(
    data: &[u8],
    vendor_id: Option<u16>,
    product_id: Option<u16>,
    usage_page: u16,
    usage: u16,
    verbose: bool,
) -> Result<Option<Vec<u8>>, QmkError> {
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - change:  "try_send_once (signature + device loop: destructure burst_to_one,
              capture first_reply, return (outcome, first_reply));
              send_raw_report (signature + tuple match arms; PRESERVE verbose
              retry-log); ADD send_raw_report doc comment"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps (hidapi 2.6.3 already provides read_timeout).

PUBLIC API SURFACE:
  - adds:    "(nothing new — send_raw_report's SIGNATURE changes from
              Result<(), QmkError> to Result<Option<Vec<u8>>, QmkError>; this is
              an intentional public-API evolution per item 3c / PRD §3)."
  - unchanged: "all lib.rs public symbols EXCEPT send_raw_report's return type
                (HostOs, RunCommand, CommandResponse, RunParameters,
                parse_cli_args, run()'s signature, list_hid_devices,
                parse_hex_or_decimal, the DEFAULT_* consts, REPORT_LENGTH); all
                QmkError variants; lib.rs `pub use core::{...}` line (name
                re-export — no edit needed); run()'s body (compiles unchanged)."

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M3.T3.S1 (run dispatch): "destructures send_raw_report's
        Result<Option<Vec<u8>>, QmkError>; on Ok(Some(bytes)) hands bytes to
        parse_reply → real CommandResponse; on Ok(None) → CommandResponse::Timeout;
        refreshes the now-stale lib.rs:332 comment and run() doc placeholders."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified. Do NOT:
    * touch run() in lib.rs (still calls send_raw_report(...)?; and returns
      placeholders — compiles UNCHANGED per the F3 proof; P1.M3.T3.S1 evolves it).
    * change burst_to_one (DONE in P1.M3.T1.S1 — its (bool, Option<Vec<u8>>)
      return is this task's INPUT contract).
    * change SendOutcome enum or any QmkError variant.
    * add any #[test] (try_send_once / send_raw_report need real hardware).
    * touch error.rs, main.rs, lib.rs, or Cargo.toml.
    * drop the verbose retry-log block in send_raw_report (PRESERVE it).
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings. The tuple-propagation
# in try_send_once + the tuple-match in send_raw_report must type-check against
# burst_to_one's (bool, Option<Vec<u8>>) return (landed in P1.M3.T1.S1).
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines. (lib.rs compiles
# unchanged — the send_raw_report(...)? statements discard the Option without
# warning, verified empirically.)

# Lint (default clippy — no clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors. The tuple return, tuple match arms, `let
# (success, reply) = ...` destructure, and `let outcome = ...; Ok((outcome,
# first_reply))` are clippy-clean under defaults.

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
# WHY no new tests: try_send_once / send_raw_report take a live &HidDevice / real
# HID bus and cannot be constructed/driven without hardware. The item explicitly
# says verify via compilation + the existing suite still passing.

# Sanity: the dispatch-path tests still reach send_raw_report and deterministically
# return DeviceNotFound (bogus VID/PID 0xDEAD/0xBEEF matches nothing) — confirms
# the tuple plumbing didn't disturb the send path. (DeviceNotFound fires in
# ensure_cache BEFORE any reply is read, so the propagation change is inert here.)
cargo test --lib test_run_query_info_dispatches_to_send -- --nocapture
cargo test --lib test_run_apply_host_context_dispatches_to_send -- --nocapture
# Expected: both pass (DeviceNotFound — the reply path is never reached because
# no device matches).

# Sanity: the typed-command + parse_reply suites still pass (these exercise the
# pure functions, which are untouched).
cargo test --lib parse_reply -- --nocapture
cargo test --lib build_typed_payload -- --nocapture
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
try_send_once / send_raw_report perform real HID read/write against a physical
QMK keyboard; there is no hardware in CI and no HID mock in the crate. The
tuple propagation is therefore verified INDIRECTLY: (1) the crate COMPILES with
the new (SendOutcome, Option<Vec<u8>>) / Option<Vec<u8>> returns end-to-end
(Level 1), and (2) the existing 57 tests still pass (Level 2), proving the
retry loop, the device loop, the cache-invalidation block, and the dispatch path
were not disturbed. A live-hardware smoke test (capture a QUERY_INFO reply all
the way up to run()) belongs to P1.M3.T3.S1 once run() plumbs the reply through
to parse_reply.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm try_send_once's new signature landed exactly once.
grep -nE "fn try_send_once" src/core.rs
# Expected: ONE match, with `-> Result<(SendOutcome, Option<Vec<u8>>), QmkError>`.

# Confirm send_raw_report's new signature landed exactly once (public fn).
grep -nE "pub fn send_raw_report" src/core.rs
# Expected: ONE match, with `-> Result<Option<Vec<u8>>, QmkError>`.

# Confirm the AllSucceeded arm returns Ok(reply) (was Ok(())).
grep -nE "\(SendOutcome::AllSucceeded, reply\) => return Ok\(reply\)" src/core.rs
# Expected: exactly ONE match.

# Confirm the .0 discard from P1.M3.T1.S1 is GONE (replaced by destructuring).
grep -nE "burst_to_one\(interface, data, batch_count, verbose\)\.0" src/core.rs
# Expected: ZERO matches (the .0 is removed).

# Confirm the destructure + first_reply capture landed.
grep -nE "let \(success, reply\) = burst_to_one" src/core.rs
# Expected: exactly ONE match (inside try_send_once).

grep -nE "first_reply" src/core.rs
# Expected: at least THREE matches (declare, the is_none check, the Ok tuple).

# Confirm the verbose retry-log block is PRESERVED (not dropped).
grep -nE "All sends failed; rebuilding device cache and retrying" src/core.rs
# Expected: exactly ONE match (still inside the TotalFailure-if retry arm).

# Confirm the doc comment was ADDED above send_raw_report (there was none before).
grep -nB3 "pub fn send_raw_report" src/core.rs | grep "///"
# Expected: at least one `///` doc line appears in the 3 lines above the fn.

# Confirm lib.rs was NOT modified (send_raw_report call sites unchanged).
grep -nE "send_raw_report\(" src/lib.rs
# Expected: the same call sites at lib.rs:336 and lib.rs:368 (statements with `?;`),
# unchanged (the lib.rs:4 `pub use` line also unchanged).

# Confirm the trailing unreachable! is still present and accurate.
grep -nE "unreachable!\(\"the retry loop always returns" src/core.rs
# Expected: exactly ONE match.

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
- [ ] `try_send_once` returns `Result<(SendOutcome, Option<Vec<u8>>), QmkError>`;
      the FIRST successful device's reply is captured into `first_reply`.
- [ ] `send_raw_report` returns `Result<Option<Vec<u8>>, QmkError>`; on
      `AllSucceeded` returns `Ok(reply)` (the first-success reply, possibly None).
- [ ] The verbose retry-log block in `send_raw_report` is PRESERVED (NOT dropped).
- [ ] A `None` reply (no device replied) propagates as `Ok(None)`, NOT an error.
- [ ] The `.0` discard + its "until then" comment from P1.M3.T1.S1 are REMOVED
      (replaced by real propagation).
- [ ] `send_raw_report` carries a new doc comment documenting the `Option<Vec<u8>>`
      return (Some = first reply; None = no reply captured; Err = transport failure).
- [ ] The cache-invalidation block, the device-path verbose log, and the
      `SendOutcome` computation in `try_send_once` are byte-identical to before.
- [ ] The crate COMPILES end-to-end (burst_to_one → try_send_once → send_raw_report).

### Code Quality Validation
- [ ] Only `src/core.rs` modified; lib.rs/error.rs/main.rs/Cargo.toml untouched.
- [ ] `lib.rs`'s `run()` compiles UNCHANGED (the `send_raw_report(...)?;`
      statements discard the `Option` without warning — verified).
- [ ] No new tests, no new deps, no new types/constants.
- [ ] The retry-loop structure (`for attempt in 0..=SEND_RETRIES`) and the
      `unreachable!` tail are unchanged.

### Documentation & Deployment
- [ ] New `send_raw_report` doc comment explains the `Option<Vec<u8>>` return
      semantics (Some/None/Err) and points to `parse_reply` (P1.M3.T3.S1) as the
      downstream decoder.
- [ ] The stale "until then" `.0` comment in `try_send_once` is removed (this
      task IS P1.M3.T2.S1).
- [ ] No README/PRD/Cargo.toml change (public-API return-type change is tracked by
      the item's DOCS=Mode A doc comment; the v0.3.0 API-surface README update is
      P1.M4.T3.S1).

---

## Anti-Patterns to Avoid

- ❌ Don't touch `lib.rs`. `run()`'s `send_raw_report(...)?;` statements compile
  UNCHANGED after the return-type change (empirically verified — the discarded
  `#[must_use]` `Option` triggers no `unused_must_use` warning because `?` counts
  as a use). Evolving `run()` to consume the reply is P1.M3.T3.S1.
- ❌ Don't DROP the verbose retry-log block in `send_raw_report`. The item's
  pseudocode shows `(SendOutcome::TotalFailure, _) if attempt < SEND_RETRIES =>
  { continue; }` (no logging) — that is ILLUSTRATIVE of the tuple destructuring,
  not a mandate to delete logging. PRESERVE the `if verbose { println!(...); }`
  block.
- ❌ Don't capture the LAST successful device's reply — capture the FIRST
  (`if first_reply.is_none() { first_reply = reply; }`). First-success-wins is the
  documented contract (`transport_evolution.md` §Key Design Decisions #4).
- ❌ Don't convert a `None` reply into an error. A timeout / no-reply is a
  legitimate `Ok(None)` (the burst SUCCEEDED; the device just didn't reply). Only
  send failures are `Err`.
- ❌ Don't change `SendOutcome`, any `QmkError` variant, `burst_to_one`, or the
  cache-invalidation block. Only the two function signatures + the
  tuple-propagation plumbing change.
- ❌ Don't leave the stale "until then we take only the success flag via .0"
  comment in `try_send_once`. This task IS P1.M3.T2.S1 — replace the `.0` + comment
  with real destructure + capture.
- ❌ Don't add a `#[test]` that calls `try_send_once` / `send_raw_report` — they
  need a live `&HidDevice` / real HID bus. Verify via compilation + the unchanged
  57-test suite. (The dispatch tests return `DeviceNotFound` before any reply is
  read, so they're unaffected.)
- ❌ Don't inline `Ok((if succeeded == 0 { ... } else { ... }, first_reply))` —
  use `let outcome = ...; Ok((outcome, first_reply))`. The `let` binding is used
  inside a tuple (not directly returned), so clippy's `let_and_return` does NOT
  fire; the `let` form is cleaner.
- ❌ Don't touch `lib.rs`'s `pub use core::{... send_raw_report ...}` line — it's
  a name re-export; the new signature follows automatically. No edit needed.
- ❌ Don't remove the trailing `unreachable!("the retry loop always returns on its
  first or final iteration")` — it's still accurate after the tuple match change.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a small, surgical edit to ONE file with the exact current `send_raw_report`
(verbatim, src/core.rs:106-153) and `try_send_once` (verbatim, src/core.rs:170-237)
bodies, the exact `.0` call site to replace (src/core.rs:207), the exact target
bodies, and the new doc comment ALL given verbatim above and in research
`notes.md`. The one genuine subtlety — whether `lib.rs` compiles unchanged — is
**resolved empirically** (the `send_raw_report(...)?;` pattern discards the
`#[must_use]` `Option` with no warning, verified by a clean cargo build + clippy
on a scratch crate), so the implementer isn't misled into touching `lib.rs`. The
second subtlety — the item's pseudocode dropping the verbose retry-log block — is
explicitly flagged as a PRESERVE gotcha. The baseline (57 passing, **verified by
`cargo test --lib`**) and all four validation commands are confirmed working in
this repo. The change is mechanical (no new tests, no new types, no new deps) and
the existing dispatch tests (bogus VID/PID ⇒ `DeviceNotFound` before any reply is
read) prove the tuple plumbing doesn't disturb the send path. The residual risk —
accidentally altering the cache-invalidation block or the device-path verbose log
— is eliminated by giving the verbatim current + target code side-by-side and a
Level-4 grep suite that pins each change to exactly one match.