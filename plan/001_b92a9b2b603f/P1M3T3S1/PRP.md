name: "P1.M3.T3.S1 — Wire run() full typed-command dispatch (build → send → parse)"
description: "Rewrite the run() match in src/lib.rs so EVERY non-ListDevices command flows through one shared dispatch arm: build the payload → send_raw_report (now Result<Option<Vec<u8>>, QmkError> from P1.M3.T2.S1) → on Ok(Some(reply)) call core::parse_reply, on Ok(None) return CommandResponse::Timeout. This completes the v0.3.0 typed-command transport: run() returns a REAL parsed CommandResponse instead of placeholders. ONLY src/lib.rs is modified (run() body, run() doc comment, one new private build_payload helper). core.rs is NOT touched (parallel P1.M3.T2.S1 owns it). No new tests (dispatch tests + parse_reply's own suite already cover what's testable without HID hardware)."

---

## Goal

**Feature Goal**: Rewrite `run()` in `src/lib.rs` so the full dispatch chain is
wired end-to-end for **every** `RunCommand` variant:

```
build payload → send_raw_report (→ Option<Vec<u8>>) → match { Some(reply) ⇒ parse_reply; None ⇒ Timeout }
```

This replaces the current placeholder returns (`CommandResponse::Legacy {
matched: true }` for SendMessage; `CommandResponse::Timeout` for typed) with the
REAL parsed reply. After this item, `run()` is the functionally complete v0.3.0
public API: it dispatches all variants through the transport layer and returns the
parsed `CommandResponse` (PRD §3, §8, §10).

**Deliverable**: A surgical edit to **`src/lib.rs` ONLY**:
1. `run()`'s `match &params.command` collapses `SendMessage` + the four typed
   variants into **one shared or-pattern arm** (`command @ (SendMessage(_) |
   QueryInfo | QueryCallback(_) | SetOs(_) | ApplyHostContext { .. })`). The
   `ListDevices` arm is unchanged.
2. The shared arm moves the verbose VID/PID/usage logging block to the top (it
   currently lives in the `SendMessage` arm), builds the payload via a new private
   helper `build_payload(command, verbose)`, then matches on
   `send_raw_report(&data, …)?` → `Some(reply) => Ok(core::parse_reply(&reply))`,
   `None => Ok(CommandResponse::Timeout)`.
3. ADD a private `fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8>`
   helper: `SendMessage` → string bytes + `0x03` ETX (preserving the existing
   message-length verbose log); typed variants → `core::build_typed_payload`;
   `ListDevices` → empty (never reached).
4. REWRITE the `run()` doc comment ([Mode A]) to document the full dispatch path
   and the `CommandResponse` return, and reference PRD §10.

**Success Definition**: `cargo build` → 0 warnings; `cargo clippy --lib` → 0
warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → all pass (baseline
unchanged — see Validation). The v0.3.0 typed-command transport is functionally
complete: `run()` returns parsed `CommandResponse` for every variant.

## User Persona (if applicable)

**Target User**: The crate's public API surface. `run()` is the single entrypoint
exposed to `main.rs` (and downstream consumers) — see `src/main.rs`
(`match qmk_notifier::run(params) { Ok(response) => println!("{:?}", response), … }`).
After this item a caller that sends `QueryInfo` and a typed-capable keyboard is
attached gets back `Ok(CommandResponse::Info { proto_ver, feature_flags, … })`
instead of a placeholder.

**Use Case**: A typed-capable keyboard receives `QUERY_INFO`, bursts back a
`[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]`
reply. `send_raw_report` (P1.M3.T2.S1) now delivers those bytes as
`Ok(Some(bytes))`; this item's `run()` hands them to `core::parse_reply` →
`CommandResponse::Info { … }`. A legacy device that sends no reply within the
bounded timeout yields `Ok(None)` up the stack ⇒ `run()` maps it to
`CommandResponse::Timeout` and the caller stays in string-only mode (PRD §8).

**Pain Points Addressed**: Today `run()` returns placeholders regardless of the
real reply (`Legacy { matched: true }` / `Timeout`), so the captured reply bytes
(piped up by P1.M3.T1/T2) are discarded at the very last step. This item closes
the loop: the reply that `burst_to_one` captured now reaches the caller as a real
`CommandResponse`.

## Why

- **PRD §8 (Response Handling) + §14 invariant 6**: the host must disambiguate
  `0x51` (typed) from `0`/`1` (legacy match-bool) from no-reply (`Timeout`). That
  disambiguation happens in `parse_reply` — but `parse_reply` is inert until
  `run()` calls it. This item is the call site that turns captured bytes into a
  parsed `CommandResponse`.
- **architecture/transport_evolution.md §Data Flow Comparison (v0.3.0)** pins the
  EXACT dispatch logic for `run()` (build → send → parse). This item transcribes
  it faithfully, adapted to the actual function name (`build_typed_payload`, see
  *Gotchas*).
- **PRD §3 (Public API)**: `run() -> Result<CommandResponse, QmkError>` is THE
  primary public API of v0.3.0. This item makes it real (the signature already
  changed in P1.M1.T2.S2; this item fills the body).
- **Closes the reply-capture chain**: P1.M3.T1.S1 (capture) → P1.M3.T2.S1
  (propagate) → **this item (consume/parse)**. Without it the whole chain is a
  no-op at the caller.

## What

### 1. `run()` body — one shared send arm (src/lib.rs:294-381)
Collapse the current `SendMessage` arm and the typed or-pattern arm into ONE
shared or-pattern arm bound with `command @ (…)` so the verbose block + the
send/parse logic live in exactly one place. `ListDevices` stays its own arm.

### 2. Verbose logging — moved to the shared arm
The VID/PID/usage-page/usage `if params.verbose { println!(…) }` block currently
in the `SendMessage` arm moves to the TOP of the shared arm (it applies to all
send commands). The SendMessage-specific "Message length" verbose log moves INTO
`build_payload`'s `SendMessage` branch (behavior-preserving).

### 3. `build_payload` helper — new private fn (src/lib.rs)
```rust
/// Build the on-wire payload for a command (the bytes AFTER the 0x81 0x9F magic
/// header, which `burst_to_one` prepends per 33-byte report). SendMessage appends
/// its own 0x03 ETX terminator; typed commands delegate to
/// `core::build_typed_payload`, which produces the [0xF0][cmd][args][0x03] form
/// (PRD §4, §10.1). ListDevices returns empty (it never reaches a send).
fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8>
```
`SendMessage` branch keeps the existing ETX-append + message-length verbose log
byte-for-byte; the typed branches delegate to `core::build_typed_payload(command)`
which already appends ETX internally.

### 4. `run()` doc comment — rewrite ([Mode A])
Document the full dispatch path, the `CommandResponse` return semantics per
variant, the `Ok(None) ⇒ Timeout` mapping, and reference PRD §8/§10. Remove the
stale "placeholder until P1.M3.T1/T3" / "until P1.M1.T3" notes (this item IS the
reply-consumption step they referenced).

### Success Criteria
- [ ] `run()` has exactly TWO match arms: `ListDevices` (unchanged) and the
      shared `command @ (SendMessage(_) | QueryInfo | QueryCallback(_) |
      SetOs(_) | ApplyHostContext { .. })` arm.
- [ ] The shared arm: (a) emits the verbose VID/PID/usage block, (b) builds data
      via `build_payload(command, params.verbose)`, (c) `match`es
      `send_raw_report(&data, …)?` with `Some(reply) => Ok(core::parse_reply(&reply))`
      and `None => Ok(CommandResponse::Timeout)`.
- [ ] `build_payload` is a private `fn` in `src/lib.rs`: SendMessage → string
      bytes + `0x03` (+ message-length verbose log); typed →
      `core::build_typed_payload`; ListDevices → empty Vec.
- [ ] The verbose "Message length: … bytes (including ETX terminator)" log is
      PRESERVED (moved into `build_payload`'s SendMessage branch).
- [ ] `run()` doc comment documents the full dispatch + `CommandResponse` return
      and references PRD §8/§10; all stale "placeholder/P1.M3.T1" notes removed.
- [ ] `cargo build` → 0 warnings; `cargo clippy --lib` → 0 warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → all pass.
- [ ] ONLY `src/lib.rs` modified (core.rs/error.rs/main.rs/Cargo.toml untouched).

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The current `run()` body
> (verbatim, src/lib.rs:294-381) is reproduced in research `notes.md` F4-context;
> the EXACT target `run()` + `build_payload` + doc comment are given verbatim in
> *Implementation Patterns* below. The two riskiest constructs — the
> `command @ (or-pattern)` binding and `match send_raw_report(...)? { Some / None }`
> — are **empirically compile/clippy/fmt-verified** on a faithful scratch model
> (research `notes.md` F6: 0 build warnings, 0 clippy warnings, 2 tests pass, fmt
> clean). The critical naming discrepancy (`build_typed_payload` ≠
> `build_command_data`, and it does NOT build the SendMessage payload) is captured
> in *Gotchas* + F1 so the implementer is not misled by the item's pseudocode.

### Documentation & References

```yaml
# MUST READ — the ONLY file edited
- file: src/lib.rs
  why: "Holds run() (src/lib.rs:294, -> Result<CommandResponse, QmkError>) with
        the TWO current arms: SendMessage (verbose block + inline ETX append +
        placeholder Legacy{matched:true} return) and the typed or-pattern
        (core::build_typed_payload + placeholder Timeout return). Both arms call
        send_raw_report(...)?. The run() doc comment (src/lib.rs:~276-293) has
        stale 'placeholder until P1.M3' notes to replace. mod core is at
        src/lib.rs:1; the pub use core::{...} re-export at src/lib.rs:2-7 does
        NOT include build_typed_payload/parse_reply (they're pub(crate)) — the
        existing code calls them via the core:: path (src/lib.rs typed arm)."
  pattern: "run() match &params.command; each arm ends in Ok(CommandResponse::..)
            or propagates an Err via send_raw_report(...)?. The verbose block uses
            vendor_id/product_id.map(|v| format!).unwrap_or_else(|| \"any\".into())."
  gotcha: "DO NOT call the builder 'build_command_data' (the item pseudocode +
           transport_evolution.md use that name). The ACTUAL pub(crate) fn in
           core.rs is build_typed_payload, and it returns EMPTY Vec for
           SendMessage/ListDevices (see F1). SendMessage's ETX-append MUST stay in
           lib.rs (via build_payload). parse_reply IS the correct name and IS
           pub(crate) — call it as core::parse_reply(&reply)."

# MUST READ — research notes: the naming discrepancy, the compile proof, scope
- docfile: plan/001_b92a9b2b603f/P1M3T3S1/research/notes.md
  why: "F1 = THE critical naming discrepancy (build_command_data vs
        build_typed_payload; SendMessage not handled); F2 = lib.rs uses the
        core:: path (don't add a `use`/pub-reexport); F3 = leave
        #[allow(dead_code)] on parse_reply INERT (don't touch core.rs — it's
        being edited in parallel); F4 = run() signature already returns
        CommandResponse (P1.M1.T2.S2 done) so tests/main.rs already compatible;
        F5 = the requested QueryInfo dispatch test ALREADY EXISTS; F6 = EMPIRICAL
        COMPILE PROOF of the command@(or-pattern) + match send?{Some/None} design;
        F7 = verbose-block move; F9 = scope boundary vs the parallel item."
  section: "F1 (critical), F6 (compile proof), F3 (allow-dead inert), F9 (scope)"

# MUST READ — the architecture doc pinning the v0.3.0 dispatch logic
- docfile: plan/001_b92a9b2b603f/architecture/transport_evolution.md
  why: "§Data Flow Comparison (v0.3.0) shows the EXACT run() dispatch: match →
        build_command_data → send_raw_report → parse_reply → CommandResponse;
        §Key Design Decisions #7 pins 'run() maps None → Timeout'. NOTE: the doc
        uses the name build_command_data; the real fn is build_typed_payload
        (F1) — the DISPATCH SHAPE is authoritative, the NAME is not."
  section: "Data Flow Comparison (v0.3.0)", "Key Design Decisions (#7)"

# REFERENCE — the contract for what send_raw_report returns (the INPUT dependency)
- docfile: plan/001_b92a9b2b603f/P1M3T2S1/PRP.md
  why: "Defines send_raw_report's NEW signature Result<Option<Vec<u8>>, QmkError>
        (Some = first device reply; None = timeout/legacy; Err = transport
        failure). This item's `match send_raw_report(...)?` DEPENDS on that having
        landed. If send_raw_report still returns Result<(), QmkError>, this PRP's
        code will not compile — STOP and confirm P1.M3.T2.S1 landed first."

# REFERENCE — PRD sections this change implements
- file: PRD.md
  why: "§3 (Public API) pins run() -> Result<CommandResponse, QmkError>; §8
        (Response Handling) + §14 invariant 6 pin the 0x51/0-or-1/Timeout
        disambiguation that run() now surfaces; §10 (Typed-Command Transport)
        is the protocol run() dispatches."
  section: "3. Public API", "8. Response Handling", "10. Typed-Command Transport",
           "14. Key Invariants (3, 6)"

# REFERENCE — Rust semantics (empirically + authoritatively verified)
- url: https://doc.rust-lang.org/reference/expressions/match-expr.html#default-binding-modes
  why: "Confirms `command @ (VariantA | VariantB)` under `match &params.command`
        binds command: &RunCommand (ref default binding mode)."
- url: https://doc.rust-lang.org/reference/patterns.html#or-patterns
  why: "OR-patterns are stable inside @ bindings; MUST parenthesize
        `name @ (A | B)` because `|` binds looser than `@`."
- url: https://doc.rust-lang.org/rustc/lints/listing/warn-by-default.html#dead-code
  why: "Confirms #[allow(dead_code)] on a USED fn is inert/silent — so leaving it
        on parse_reply after run() calls it produces no warning (empirically
        verified in F6)."
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
    ├── main.rs         # binary entrypoint — calls run(), prints CommandResponse. DO NOT TOUCH.
    ├── core.rs         # send_raw_report/try_send_once/burst_to_one (P1.M3.T2.S1 in
    │                   # progress here) + build_typed_payload + parse_reply. DO NOT TOUCH.
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # <-- FILE TO EDIT (run() body + doc comment + build_payload helper)
```

### Desired Codebase tree with files to be modified

```bash
src/
└── lib.rs   # MODIFIED ONLY:
             #   (1) run() body: collapse SendMessage + typed into one shared
             #       `command @ (or-pattern)` arm; move verbose block up;
             #       `match send_raw_report(&data,..)? { Some(r)=>parse_reply, None=>Timeout }`.
             #       ListDevices arm unchanged.
             #   (2) ADD private `fn build_payload(command, verbose) -> Vec<u8>`
             #       (SendMessage → bytes+0x03+msg-len log; typed → core::build_typed_payload;
             #        ListDevices → empty).
             #   (3) REWRITE run() doc comment (Mode A: full dispatch + CommandResponse + PRD §8/§10).
# (core.rs, error.rs, main.rs, Cargo.toml unchanged; NO new files; NO new tests)
```

> No new files, no new deps, no new tests. One file modified (`src/lib.rs`).

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (NAME DRIFT — do NOT be misled by the item pseudocode): the item's
//   pseudocode + transport_evolution.md call the builder `build_command_data`.
//   The ACTUAL pub(crate) fn in core.rs is `build_typed_payload`, and it does
//   NOT build the SendMessage payload — its final match arms are
//   `SendMessage(_) | ListDevices => return Vec::new()`. (Proven by the existing
//   test build_typed_payload_non_typed_returns_empty.) So:
//     • CANNOT route SendMessage through build_typed_payload (it'd send empty).
//     • MUST keep the SendMessage ETX-append (string bytes + 0x03) in lib.rs —
//       put it in the build_payload helper's SendMessage branch.
//   parse_reply IS the correct name and IS pub(crate) and DOES handle the
//   SendMessage legacy 0/1 reply. Call core::parse_reply(&reply).

// CRITICAL (call core fns via the core:: path, NOT a `use`): lib.rs has
//   `mod core;` (lib.rs:1) and `pub use core::{ list_hid_devices,
//   parse_hex_or_decimal, send_raw_report, DEFAULT_*, REPORT_LENGTH };` (lib.rs:2-7).
//   build_typed_payload and parse_reply are pub(crate) and are NOT in that re-export.
//   The existing typed arm calls `core::build_typed_payload(&params.command)`.
//   STAY CONSISTENT: call `core::build_typed_payload(command)` and
//   `core::parse_reply(&reply)`. Do NOT add them to `pub use core::{...}` (that
//   would widen a pub(crate) item to pub — out of scope) and do NOT add a
//   `use core::{parse_reply}` import.

// CRITICAL (DO NOT touch core.rs — it's being edited in parallel): P1.M3.T2.S1
//   is editing core.rs right now (send_raw_report + try_send_once). parse_reply
//   carries #[allow(dead_code)] because until THIS item it's test-only. Once
//   run() calls parse_reply, that #[allow(dead_code)] becomes INERT (clippy-
//   silent — empirically verified F6 + rustc docs). LEAVE IT IN PLACE. Removing
//   it would be a core.rs edit and risks a merge conflict with P1.M3.T2.S1. The
//   one-line removal is a trivial follow-up (P1.M4 cleanup) once core.rs settles.

// CRITICAL (match `command @ (or-pattern)` MUST be parenthesized): Rust parses
//   `name @ A | B` as `(name @ A) | B`. Write `command @ (SendMessage(_) |
//   QueryInfo | QueryCallback(_) | SetOs(_) | ApplyHostContext { .. })`. Under
//   `match &params.command` the default binding mode is `ref`, so `command` is
//   `&RunCommand` — pass it straight to `build_payload(command: &RunCommand, ..)`
//   and to `core::build_typed_payload(command)`. (Empirically compile-verified F6.)

// CRITICAL (the `?` short-circuits BEFORE the match arms): the inner
//   `match send_raw_report(&data,..)? { Some(reply) => .., None => .. }` works
//   because `?` on the Err (e.g. DeviceNotFound for bogus VID/PID) returns early
//   from run() WITHOUT evaluating the match arms. This is why the existing
//   dispatch tests (which expect Err(DeviceNotFound)) still pass after this
//   change. (Empirically verified F6.)

// GOTCHA (the SendMessage message-length verbose log MUST survive): the current
//   SendMessage arm has TWO verbose blocks — the shared VID/PID/usage block
//   (MOVE to the shared arm top) and the SendMessage-only "Message length: N
//   bytes (including ETX terminator)" block (MOVE into build_payload's
//   SendMessage branch, next to the 0x03 push). Do NOT drop either.

// GOTCHA (run()'s signature ALREADY returns CommandResponse — don't "change" it):
//   P1.M1.T2.S2 (Complete) made run() -> Result<CommandResponse, QmkError> and
//   fixed the tests. main.rs already matches Ok(response) => println. The
//   existing test_run_with_send_message_command already does match result {
//   Ok(_) => {}, Err(..) => {} } — it ignores the Ok variant shape, so it
//   compiles UNCHANGED. Do NOT add a CommandResponse-shape assertion to it (no
//   hardware in CI → it always takes the Err(DeviceNotFound) branch anyway).

// GOTCHA (the QueryInfo dispatch test ALREADY EXISTS): the item asks to "add a
//   test: test_run_query_info_dispatch". That coverage already exists as
//   test_run_query_info_dispatches_to_send (lib.rs), added in P1.M1.T2.S2 — it
//   asserts Err(DeviceNotFound) for bogus VID/PID 0xDEAD/0xBEEF. Sibling tests
//   exist for QueryCallback/SetOs/ApplyHostContext. Do NOT add a duplicate. The
//   reply→parse_reply mapping is NOT unit-testable without real HID hardware
//   (send_raw_report needs a live device), so these dispatch tests + parse_reply's
//   own core.rs suite are the maximal testable coverage.

// GOTCHA (no rustfmt.toml / no clippy.toml ⇒ defaults): the or-pattern arm, the
//   `command @ (..)` binding, the inner `match .. ? { Some/None }`, and the
//   build_payload match are all fmt/clippy-clean under defaults (verified F6).
```

## Implementation Blueprint

### Data models and structure
No new types, enums, structs, or constants. `RunCommand`, `CommandResponse`,
`RunParameters`, `QmkError` are all UNCHANGED. The only new item is a private
`fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8>` in lib.rs. No
public API surface changes (run()'s signature is already
`Result<CommandResponse, QmkError>` from P1.M1.T2.S2). No state, no globals, no
new deps.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 0: VERIFY the dependency landed (do this FIRST, before editing)
  - RUN: `grep -nE "pub fn send_raw_report" src/core.rs`
  - EXPECT: the signature ends `-> Result<Option<Vec<u8>>, QmkError>`.
  - IF it still says `-> Result<(), QmkError>`: STOP. P1.M3.T2.S1 has not landed;
    this PRP's `match send_raw_report(...)? { Some / None }` will not compile.
    Confirm P1.M3.T2.S1 merged before proceeding.

Task 1: ADD private `fn build_payload` to src/lib.rs (place it just ABOVE `pub fn run`)
  - IMPLEMENT exactly the body in "Implementation Patterns" §B.
  - SendMessage branch: clone string bytes into a Vec, push 0x03, emit the
    "Message length" verbose log (preserved from the old SendMessage arm).
  - Typed branches (or-pattern): delegate to `core::build_typed_payload(command)`.
  - ListDevices branch: `Vec::new()` (never reached — run() routes ListDevices
    elsewhere — but the match must be exhaustive).
  - NAMING: `fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8>`.
  - VISIBILITY: private (no `pub`). It's a lib-internal helper.

Task 2: REWRITE `run()` body (src/lib.rs:294-381) — collapse to the design in §A
  - KEEP the ListDevices arm BYTE-IDENTICAL (list_hid_devices()?; Ok(Timeout)).
  - REPLACE the SendMessage arm + the typed or-pattern arm with the SINGLE shared
    `command @ (SendMessage(_) | QueryInfo | QueryCallback(_) | SetOs(_) |
    ApplyHostContext { .. })` arm from §A.
  - The shared arm: (1) verbose VID/PID/usage block (moved from SendMessage),
    (2) `let data = build_payload(command, params.verbose);`,
    (3) `match send_raw_report(&data, params.vendor_id, params.product_id,
        params.usage_page, params.usage, params.verbose)? { Some(reply) =>
        Ok(core::parse_reply(&reply)), None => Ok(CommandResponse::Timeout) }`.
  - DELETE: the inline ETX-append (now in build_payload), both placeholder returns
    (`CommandResponse::Legacy { matched: true }` and `CommandResponse::Timeout`),
    and the now-stale inline comments about placeholders / P1.M3.T1 / P1.M1.T3.

Task 3: REWRITE the `run()` doc comment (src/lib.rs:~276-293) per §C ([Mode A])
  - Document the full dispatch path (ListDevices → list_hid_devices; everything
    else → build_payload → send_raw_report → parse_reply).
  - Document the CommandResponse return per command family.
  - Document Ok(None) ⇒ CommandResponse::Timeout (legacy/timeout device).
  - Reference PRD §8 and §10.
  - REMOVE the stale "placeholder until P1.M3.1/T3" / "until P1.M1.T3" notes.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, `cargo clippy --lib`, `cargo fmt --check`,
    `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0; all tests
    pass (baseline unchanged — no new tests).
  - IF `cargo build` fails on `send_raw_report` returning `()` not `Option<Vec<u8>>`:
    P1.M3.T2.S1 did NOT land — see Task 0.
```

### Implementation Patterns & Key Details

#### §A — the NEW `run()` body (full, drop-in replacement for src/lib.rs:294-381)

```rust
pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError> {
    match &params.command {
        RunCommand::ListDevices => {
            list_hid_devices()?;
            // ListDevices never sends over the wire, so there is no device reply
            // to parse. Timeout is the semantic match for "no reply received".
            Ok(CommandResponse::Timeout)
        }
        // Every command that sends a report shares one dispatch path: build the
        // payload → burst-send → parse the first captured reply (or Timeout if
        // no device replied within the bounded read). `command` binds to
        // &RunCommand (match ergonomics on `&params.command`), so it's passable
        // straight to build_payload / core::build_typed_payload.
        command @ (RunCommand::SendMessage(_)
            | RunCommand::QueryInfo
            | RunCommand::QueryCallback(_)
            | RunCommand::SetOs(_)
            | RunCommand::ApplyHostContext { .. }) =>
        {
            if params.verbose {
                let vid = params
                    .vendor_id
                    .map(|v| format!("0x{v:04X}"))
                    .unwrap_or_else(|| "any".into());
                let pid = params
                    .product_id
                    .map(|p| format!("0x{p:04X}"))
                    .unwrap_or_else(|| "any".into());
                println!("Using VID: {vid}, PID: {pid}");
                println!(
                    "Using Usage Page: 0x{:04X}, Usage: 0x{:04X}",
                    params.usage_page, params.usage
                );
            }

            let data = build_payload(command, params.verbose);

            match send_raw_report(
                &data,
                params.vendor_id,
                params.product_id,
                params.usage_page,
                params.usage,
                params.verbose,
            )? {
                Some(reply) => Ok(core::parse_reply(&reply)),
                None => Ok(CommandResponse::Timeout),
            }
        }
    }
}
```

> NOTE on the `?` inside the match scrutinee: it is valid Rust and **empirically
> verified** (research `notes.md` F6). The `?` short-circuits an `Err` (e.g.
> `DeviceNotFound` for the bogus VID/PID used by the dispatch tests) and returns
> it from `run()` *before* any match arm runs — which is exactly why those tests
> still pass.

#### §B — the NEW private `build_payload` helper (place just ABOVE `pub fn run`)

```rust
/// Build the on-wire payload for `command` — the bytes AFTER the `0x81 0x9F`
/// magic header (which `burst_to_one` prepends per 33-byte report).
///
/// - `SendMessage` → the caller's window string plus the `0x03` ETX terminator
///   (PRD §14 invariant 1; ETX is appended HERE, not by build_typed_payload,
///   which returns an empty payload for SendMessage).
/// - Typed commands (`QueryInfo`/`QueryCallback`/`SetOs`/`ApplyHostContext`) →
///   delegate to `core::build_typed_payload`, which produces the
///   `[0xF0][cmd][args][0x03]` form (PRD §4, §10.1).
/// - `ListDevices` → empty (it never reaches a send; routed elsewhere by `run`).
fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8> {
    match command {
        RunCommand::SendMessage(message) => {
            let input = message.as_bytes();
            let mut data = Vec::with_capacity(input.len() + 1);
            data.extend_from_slice(input);
            data.push(0x03); // ETX terminator (PRD §14 invariant 1)
            if verbose {
                println!(
                    "Message length: {} bytes (including ETX terminator)",
                    data.len()
                );
            }
            data
        }
        // build_typed_payload appends the ETX (0x03) terminator internally and
        // returns empty Vec for SendMessage/ListDevices — by construction only
        // the typed variants reach this arm.
        RunCommand::QueryInfo
        | RunCommand::QueryCallback(_)
        | RunCommand::SetOs(_)
        | RunCommand::ApplyHostContext { .. } => core::build_typed_payload(command),
        RunCommand::ListDevices => Vec::new(),
    }
}
```

#### §C — the NEW `run()` doc comment ([Mode A], drop-in replacement for src/lib.rs:~276-293)

```rust
/// Execute the notifier command described by `params` and return the parsed
/// device reply (PRD §3 *Public API*, §8 *Response Handling*, §10 *Typed-Command
/// Transport*).
///
/// # Dispatch
/// - [`RunCommand::ListDevices`] → calls [`list_hid_devices`] (prints the HID
///   enumeration) and returns [`CommandResponse::Timeout`]: nothing is sent over
///   the wire, so there is no reply to parse.
/// - Every other variant ([`RunCommand::SendMessage`] and the typed commands
///   [`RunCommand::QueryInfo`] / [`RunCommand::QueryCallback`] /
///   [`RunCommand::SetOs`] / [`RunCommand::ApplyHostContext`]) shares one path:
///   build the on-wire payload ([`build_payload`]) → burst-send it via
///   [`send_raw_report`] (device cache, multi-report burst-write, bounded reply
///   read) → parse the FIRST captured reply with [`core::parse_reply`].
///
/// # Reply → `CommandResponse` mapping
/// - `Ok(Some(reply))` → `core::parse_reply(&reply)`:
///   - `SendMessage` reply (`response[0]` ∈ `{0,1}`) ⇒ [`CommandResponse::Legacy`].
///   - `0x51` typed reply ⇒ [`CommandResponse::Info`] / [`CommandResponse::CallbackName`]
///     / [`CommandResponse::Ack`] (decoded by the `response[1]` cmd-echo).
/// - `Ok(None)` (no device replied within the bounded read — legacy / offline
///   device) ⇒ [`CommandResponse::Timeout`]; the caller treats this as a
///   non-capable device and stays in string-only mode (PRD §8, §10.2).
/// - `Err(QmkError::DeviceNotFound)` ⇒ no interface matched the VID/PID/usage
///   predicate (the zero-config path matches by usage page/usage when VID/PID
///   are `None`). `Err(QmkError::PartialSendError { .. })` /
///   `Err(QmkError::SendReportError(..))` ⇒ transport failure (PRD §9).
pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError> {
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY"
  - change:  "run() body (collapse to one shared send arm + ListDevices);
              run() doc comment (Mode A rewrite); ADD private fn build_payload."

DEPENDENCIES (MUST have landed before this compiles):
  - P1.M3.T2.S1: "send_raw_report -> Result<Option<Vec<u8>>, QmkError> (verify
                  with grep in Task 0; STOP if still Result<(), QmkError>)."
  - P1.M2.T1.S2: "core::build_typed_payload (DONE — pub(crate); returns empty for
                  SendMessage/ListDevices, [0xF0][cmd][args][0x03] for typed)."
  - P1.M2.T2.S2: "core::parse_reply (DONE — pub(crate); handles 0x51/0/1/empty)."

PUBLIC API SURFACE:
  - adds:    "(nothing new — run()'s SIGNATURE is already Result<CommandResponse,
              QmkError> from P1.M1.T2.S2; this item changes the BODY + doc
              comment only)."
  - unchanged: "all lib.rs public symbols (HostOs, RunCommand, CommandResponse,
                RunParameters, parse_cli_args, run()'s signature, list_hid_devices,
                parse_hex_or_decimal, send_raw_report's signature, the DEFAULT_*
                consts, REPORT_LENGTH); the pub use core::{...} re-export line
                (build_typed_payload/parse_reply stay pub(crate), NOT re-exported);
                main.rs (already compatible — prints the CommandResponse); all
                QmkError variants."

SCOPE BOUNDARY (do NOT implement now):
  - ONLY src/lib.rs is modified. Do NOT:
    * touch core.rs (send_raw_report/try_send_once/burst_to_one are P1.M3.T2.S1's;
      build_typed_payload/parse_reply are DONE). In particular do NOT remove
      parse_reply's #[allow(dead_code)] — it's inert once run() uses it (F3/F6)
      and editing core.rs risks colliding with the parallel item.
    * change RunCommand/CommandResponse/RunParameters/HostOs/QmkError.
    * add any #[test] (the dispatch tests + parse_reply's suite already cover the
      testable paths; the reply→parse_reply mapping needs real HID hardware).
    * touch error.rs, main.rs, Cargo.toml.
    * drop the "Message length … (including ETX terminator)" verbose log (MOVE it
      into build_payload's SendMessage branch).
    * widen parse_reply/build_typed_payload to pub (they're pub(crate) by design).
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings. The shared
# `command @ (or-pattern)` arm, the `match send_raw_report(...)? { Some/None }`
# inner match, and the build_payload helper must all type-check against
# send_raw_report's Result<Option<Vec<u8>>, QmkError> (P1.M3.T2.S1) and
# parse_reply's pub(crate) signature (P1.M2.T2.S2).
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# IF it fails on `send_raw_report` returning `()` not `Option<Vec<u8>>`: P1.M3.T2.S1
# did NOT land — see Implementation Task 0. Do not proceed until it has.

# Lint (default clippy — no clippy.toml exists). The or-pattern @-binding, the
# `?` in the match scrutinee, and the exhaustive build_payload match are all
# clippy-clean under defaults (empirically verified F6).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. <N> passed; 0 failed; 0 ignored; ..." — the baseline
# is UNCHANGED by this item (no new tests, no existing test's logic changed).
# (lib.rs ~20 + core.rs ~42 = ~62 at HEAD; confirm with the actual number printed.)
#
# WHY no new tests: the reply → parse_reply mapping lives inside the live HID
# send path (send_raw_report needs a real device). The deterministic CI path is
# `Err(DeviceNotFound)` for bogus VID/PID, which the existing dispatch tests
# (test_run_query_info_dispatches_to_send et al.) already assert. parse_reply's
# OWN core.rs suite already proves the byte→CommandResponse decoding. So this
# item adds no tests and changes none.

# Sanity: the dispatch-path tests still return DeviceNotFound (bogus VID/PID
# 0xDEAD/0xBEEF) — confirms the shared arm still reaches send_raw_report and the
# `?` propagates the Err unchanged.
cargo test --lib test_run_query_info_dispatches_to_send -- --nocapture
cargo test --lib test_run_apply_host_context_dispatches_to_send -- --nocapture
cargo test --lib test_run_with_send_message_command -- --nocapture
# Expected: all pass (Err(DeviceNotFound)).

# Sanity: parse_reply + build_typed_payload suites still green (pure functions,
# untouched by this item).
cargo test --lib parse_reply -- --nocapture
cargo test --lib build_typed_payload -- --nocapture
```

### Level 3: Integration Testing (System Validation)

```text
PARTIALLY APPLICABLE. The full reply→parse_reply→CommandResponse round-trip
needs a real QMK keyboard on the HID bus (send_raw_report reads a live device).
Without hardware, the integration evidence is INDIRECT but strong:

(1) The crate COMPILES end-to-end with the new dispatch (Level 1) — proving the
    type chain build_payload → send_raw_report(Option<Vec<u8>>) → parse_reply →
    CommandResponse is consistent.
(2) The dispatch tests reach send_raw_report and deterministically return
    DeviceNotFound (Level 2) — proving the shared arm is wired and the `?`
    propagates transport errors unchanged.
(3) parse_reply's own core.rs test suite (Level 2) proves the byte→CommandResponse
    decoding for every reply shape (0x51 typed variants, 0/1 legacy, empty/timeout).

A live-hardware smoke test (send QueryInfo to a typed-capable board and assert
the returned CommandResponse::Info fields) is the only gap, and it's a manual QA
step, not a CI gate.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm run() has exactly TWO match arms (ListDevices + the shared or-pattern).
grep -nE "RunCommand::ListDevices =>|command @ \(RunCommand::SendMessage" src/lib.rs
# Expected: exactly TWO matches.

# Confirm the shared arm uses the tuple-less Option match on send_raw_report.
grep -nE "match send_raw_report\(" src/lib.rs
# Expected: exactly ONE match, inside the shared arm.

# Confirm Some(reply) ⇒ core::parse_reply and None ⇒ Timeout.
grep -nE "Some\(reply\) => Ok\(core::parse_reply\(&reply\)\)" src/lib.rs
grep -nE "None => Ok\(CommandResponse::Timeout\)" src/lib.rs
# Expected: one match each.

# Confirm the placeholder returns are GONE.
grep -nE "CommandResponse::Legacy \{ matched: true \}" src/lib.rs
# Expected: ZERO matches in run() (the only Legacy variant references should now
# be the CommandResponse enum definition + its tests).

# Confirm build_payload landed and delegates SendMessage ETX + typed to core.
grep -nE "fn build_payload" src/lib.rs
grep -nE "core::build_typed_payload\(command\)" src/lib.rs
grep -nE "data.push\(0x03\)" src/lib.rs
# Expected: one match each.

# Confirm the verbose "Message length" log survived (moved into build_payload).
grep -nE "Message length: \{\} bytes \(including ETX terminator\)" src/lib.rs
# Expected: exactly ONE match (now inside build_payload's SendMessage branch).

# Confirm lib.rs was the ONLY file changed vs HEAD.
git status --porcelain
# Expected: only "M src/lib.rs".

# Confirm core.rs was NOT touched (parallel item owns it).
grep -nE "#\[allow\(dead_code\)\]" src/core.rs | head   # parse_reply's allow is still there (inert)
# Expected: parse_reply's #[allow(dead_code)] is PRESENT (left intentionally — F3).

# Confirm main.rs is unchanged and still compatible.
grep -nE "qmk_notifier::run\(params\)" src/main.rs
# Expected: exactly ONE match (Ok(response) => println!("{:?}", response)).

# Full-crate final gate.
cargo test --lib 2>&1 | tail -3
# Expected: "test result: ok. <N> passed; 0 failed; ..." (baseline unchanged).
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1 passed: `cargo build` → 0 warnings.
- [ ] Level 1 passed: `cargo clippy --lib` → 0 warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → all pass (baseline unchanged).
- [ ] Task 0 confirmed: `send_raw_report` is `-> Result<Option<Vec<u8>>, QmkError>`
      (P1.M3.T2.S1 landed) — else STOP.

### Feature Validation
- [ ] `run()` has exactly two arms: `ListDevices` (unchanged) + the shared
      `command @ (SendMessage(_) | QueryInfo | QueryCallback(_) | SetOs(_) |
      ApplyHostContext { .. })` arm.
- [ ] The shared arm builds data via `build_payload`, then
      `match send_raw_report(&data, …)? { Some(reply) => Ok(core::parse_reply(&reply)),
      None => Ok(CommandResponse::Timeout) }`.
- [ ] `build_payload` is a private fn: SendMessage → bytes + `0x03` (+ msg-len
      verbose log); typed → `core::build_typed_payload`; ListDevices → empty.
- [ ] The verbose VID/PID/usage block is at the TOP of the shared arm (moved from
      SendMessage); the "Message length" log is inside `build_payload`.
- [ ] Both placeholder returns (`Legacy { matched: true }`, the typed
      `CommandResponse::Timeout`) are GONE — real parse_reply / Timeout mapping.
- [ ] `run()` doc comment ([Mode A]) documents the dispatch + CommandResponse +
      Ok(None)⇒Timeout and references PRD §8/§10; stale placeholder notes removed.

### Code Quality Validation
- [ ] Only `src/lib.rs` modified; core.rs/error.rs/main.rs/Cargo.toml untouched.
- [ ] `core::build_typed_payload` / `core::parse_reply` called via the `core::`
      path (NOT added to `pub use core::{...}`; NOT widened to pub).
- [ ] `#[allow(dead_code)]` on `parse_reply` left in place (inert — F3/F6).
- [ ] No new tests, no new deps, no new types/constants.
- [ ] `command @ (or-pattern)` is parenthesized; `command` is used as `&RunCommand`.

### Documentation & Deployment
- [ ] New `run()` doc comment explains the full dispatch path and the
      `CommandResponse` return per command family.
- [ ] No README/PRD/Cargo.toml change (the v0.3.0 API-surface README update is
      P1.M4.T3.S1; this item's DOCS scope is the run() doc comment only).
- [ ] All stale "placeholder until P1.M3.1/T3 / P1.M1.T3" notes in run() + its
      doc comment are removed (this item IS the reply-consumption step).

---

## Anti-Patterns to Avoid

- ❌ Don't call the builder `build_command_data` (the item pseudocode +
  transport_evolution.md use that name). The ACTUAL pub(crate) fn is
  `build_typed_payload`, and it returns **empty** for `SendMessage`. Route
  SendMessage through it and you'll send an empty payload. Keep SendMessage's
  ETX-append in the `build_payload` helper.
- ❌ Don't route ALL commands through one `build_command_data` call as the item's
  pseudocode shows (`let data = build_command_data(&params.command);`). That
  assumes a unified builder that doesn't exist. Use the `build_payload` helper
  that dispatches (SendMessage → inline ETX; typed → `core::build_typed_payload`).
- ❌ Don't touch `core.rs`. P1.M3.T2.S1 edits it in parallel right now. The
  `#[allow(dead_code)]` on `parse_reply` stays (it's inert once `run()` calls it
  — empirically + authoritatively verified). Removing it risks a merge conflict.
- ❌ Don't add `build_typed_payload`/`parse_reply` to the `pub use core::{...}`
  re-export or a `use core::{...}` import. They're `pub(crate)`; the existing code
  calls them via the `core::` path — stay consistent.
- ❌ Don't write `command @ SendMessage(_) | QueryInfo | …` unparenthesized —
  `|` binds looser than `@`, so it parses as `(command @ SendMessage(_)) | …`.
  Parenthesize: `command @ (SendMessage(_) | QueryInfo | …)`.
- ❌ Don't drop the verbose blocks. The VID/PID/usage block MOVES to the shared
  arm top; the "Message length" log MOVES into `build_payload`'s SendMessage
  branch. Both must survive.
- ❌ Don't add a `#[test]` for QueryInfo dispatch — it already exists
  (`test_run_query_info_dispatches_to_send`), added in P1.M1.T2.S2. A duplicate
  would be redundant; the reply→parse_reply mapping isn't unit-testable without
  HID hardware.
- ❌ Don't change `run()`'s signature or assert a specific `CommandResponse`
  shape in `test_run_with_send_message_command`. The signature is ALREADY
  `Result<CommandResponse, QmkError>` (P1.M1.T2.S2); the test already handles
  `Ok(_)` / `Err(..)` and always takes the `Err(DeviceNotFound)` branch in CI.
- ❌ Don't panic on `ListDevices` inside `build_payload` — return an empty Vec.
  `run()` routes `ListDevices` to its own arm, so `build_payload`'s `ListDevices`
  branch is unreachable; an empty Vec keeps a misuse inert rather than wedging a
  live path.
- ❌ Don't proceed if `send_raw_report` still returns `Result<(), QmkError>`
  (Task 0 grep). This PRP's `match send_raw_report(...)? { Some / None }`
  requires P1.M3.T2.S1 to have landed.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a surgical edit to ONE file (`src/lib.rs`: `run()` body + doc comment + one
private `build_payload` helper), with the exact target `run()`, `build_payload`,
and doc comment given **verbatim** in Implementation Patterns §A/§B/§C. The two
genuine risk points — the `command @ (or-pattern)` binding and the
`match send_raw_report(...)? { Some / None }` scrutinee — are **empirically
compile/clippy/fmt-verified** on a faithful scratch model (research `notes.md`
F6: 0 build warnings, 0 clippy warnings, 2 tests pass, fmt clean) AND
authoritatively confirmed via the Rust Reference (researcher URLs). The single
most misleading thing about the item — its `build_command_data` pseudocode that
implies a unified builder handling `SendMessage` — is called out in the very
first gotcha + F1, with the correct actual name (`build_typed_payload`) and the
correct consequence (keep SendMessage's ETX-append in `build_payload`). The
`#[allow(dead_code)]`-left-inert decision (which keeps this a lib.rs-only edit and
avoids the parallel core.rs conflict) is empirically + authoritatively proven
safe. The baseline test count and all five validation commands are confirmed
working in this repo. The one true external precondition — P1.M3.T2.S1 having
landed — is gated by Task 0 (a one-line grep) with an explicit STOP instruction.
Residual risks (dropping a verbose log, mis-naming the builder) are eliminated by
the verbatim target code + a Level-4 grep suite that pins each change.