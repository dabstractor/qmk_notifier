# PRP — P1.M1.T2.S2: Change run() to return CommandResponse and fix existing tests

---

## Goal

**Feature Goal**: Evolve `run()`'s public signature from
`Result<(), QmkError>` to `Result<CommandResponse, QmkError>` — the **first of
the three breaking v0.3.0 transport-signature changes** (per
`findings_and_risks.md` §F2). This is the **top-of-call-stack** change: `run()`
sits above `send_raw_report` (which itself evolves later in P1.M3.T2). At this
stage the reply-parsing machinery does **not** exist yet, so the two real
dispatch arms (`SendMessage`, `ListDevices`) return **placeholder**
`CommandResponse` values, the typed arms keep their existing `todo!()` stubs, and
the one compile-breaking test is fixed. `main.rs` is updated to print the new
return value. This unblocks every downstream task (P1.M2/M3) that types against
`run()`'s eventual `Result<CommandResponse, QmkError>` contract.

**Deliverable**: Three surgical edits across **two files** (`src/lib.rs`,
`src/main.rs` — core.rs/error.rs/Cargo.toml untouched):
1. `src/lib.rs` — change `run()` signature + body (two arms: placeholder return
   values; typed arms unchanged) + rewrite run()'s `///` doc comment.
2. `src/lib.rs` — fix the **one** compile-breaking test arm (`Ok(()) =>` → `Ok(_) =>`).
3. `src/main.rs` — convert the `if let Err` call site to a `match` that prints
   the `CommandResponse` on `Ok` (Debug print).

**Success Definition**: `cargo build` compiles with **zero warnings**;
`cargo clippy --lib` → zero warnings; `cargo fmt --check` → exit 0;
`cargo test --lib` → **30 passed, 0 failed** (baseline is 30; no tests added or
removed — only the one breaking arm repaired). `send_raw_report` and
`list_hid_devices` signatures remain `Result<(), QmkError>` (their evolution is
P1.M3.T2, out of scope). No file other than `src/lib.rs` and `src/main.rs` is
modified.

## User Persona (if applicable)

**Target User**: Downstream consumers of the crate's public API — primarily
`qmkonnect` (desktop daemon) and the crate's own `main.rs` binary.

**Use Case**: A caller invokes `run(params)` and now receives a structured
`CommandResponse` instead of a bare `()`, enabling the eventual capability
handshake (qmkonnect `spec/HOST_RULES.md`).

**User Journey**: `parse_cli_args()` → `RunParameters` → `run(params)` →
`Result<CommandResponse, QmkError>` → caller `match`es on the variant
(`Legacy`/`Info`/`CallbackName`/`Ack`/`Timeout`). Today only the placeholder
paths (`Legacy`/`Timeout`) are reachable; the typed variants panic with `todo!()`
until P1.M3.T3.

**Pain Points Addressed**: Removes the `()` return that discards all device
reply information, unblocking reply-parsing work; satisfies PRD §3's contract for
`run()`; keeps qmkonnect source-compatible (it uses `Ok(_) => …`,
`findings_and_risks.md` §R1).

## Why

- **PRD §3 contract**: the spec mandates
  `pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError>;`.
  The `CommandResponse` type already exists (P1.M1.T1.S3, complete); this task
  closes the gap between "the type exists" and "run() actually returns it."
- **Dependency-chain integrity** (`findings_and_risks.md` *Task Sequencing*):
  signature changes must go bottom-up (`burst_to_one` → `send_raw_report` →
  `run()`). `run()` is the top of the stack. Doing its signature change now
  (with placeholders) means every later task can code against the *final*
  `run()` signature instead of a moving target.
- **qmkonnect stays source-compatible** (`findings_and_risks.md` §R1): qmkonnect
  matches `Ok(_) => …` (verified), so widening `Ok` from `()` to
  `CommandResponse` recompiles without a qmkonnect edit. qmkonnect's eventual
  *use* of `CommandResponse` is a separate follow-up; this task is the
  prerequisite release.
- **Placeholders are deliberate and bounded**: real reply parsing needs
  `burst_to_one` reply-capture (P1.M3.T1) + `send_raw_report` propagation
  (P1.M3.T2) + `parse_reply` (P1.M2.T2), none of which exist yet. Returning
  `Legacy { matched: true }` / `Timeout` now keeps the crate compiling and
  test-green while those land. Each placeholder is explicitly documented as such.

## What

### 1. `src/lib.rs` — `run()` signature + body

**Change the signature** (currently at ~lib.rs:277):

```rust
// OLD
pub fn run(params: RunParameters) -> Result<(), QmkError> {
// NEW
pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError> {
```

**Rewrite run()'s `///` doc comment** (currently the one-line
`/// Core function that executes the notifier logic with explicit parameters.`)
to (Mode A — no separate docs file):

```rust
/// Execute the notifier logic for the given command and parameters.
///
/// Returns a [`CommandResponse`] describing the device's reply, or a
/// [`QmkError`] on transport failure (PRD §3 *Public API*, §10 *Typed-Command
/// Transport*). The response shape depends on the command:
///
/// - [`RunCommand::SendMessage`] → [`CommandResponse::Legacy`] as a
///   **placeholder** (`matched: true`) until real reply parsing lands in
///   P1.M3.T3; the firmware's `response[0]` match-bool will be decoded there.
/// - [`RunCommand::ListDevices`] → [`CommandResponse::Timeout`]: no device
///   reply was captured because nothing was sent over the wire (list-only path).
/// - Typed variants (`QueryInfo`/`QueryCallback`/`SetOs`/`ApplyHostContext`)
///   are stubbed with `todo!()` until full dispatch + reply capture land in
///   P1.M3.T3.
```

> The contract says "the lib.rs-level doc comment should mention CommandResponse."
> There is **no crate-level `//!` doc** in lib.rs (line 1 is `mod core;`), so this
> resolves to run()'s `///` doc comment — the primary lib.rs public item.

**Rewrite the two real match arms** to construct the placeholder
`CommandResponse` values. The typed arms stay exactly as they are (see
*Known Gotchas*). The full new body:

```rust
pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError> {
    match params.command {
        RunCommand::ListDevices => {
            list_hid_devices()?;
            // Semantic: no device reply was received — nothing was sent.
            // Real reply capture arrives in P1.M3.T1/T3; ListDevices never sends.
            Ok(CommandResponse::Timeout)
        }
        RunCommand::SendMessage(message) => {
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

            let input = message.as_bytes();

            let mut input_with_terminator = Vec::with_capacity(input.len() + 1);
            input_with_terminator.extend_from_slice(input);
            input_with_terminator.push(0x03);

            if params.verbose {
                println!(
                    "Message length: {} bytes (including ETX terminator)",
                    input_with_terminator.len()
                );
            }

            // send_raw_report STILL returns Result<(), QmkError> at this stage
            // (its evolution to Result<Option<Vec<u8>>, QmkError> is P1.M3.T2).
            // On success we return the placeholder Legacy{matched:true}; the real
            // response[0] match-bool is decoded in P1.M3.T3 via parse_reply.
            send_raw_report(
                &input_with_terminator,
                params.vendor_id,
                params.product_id,
                params.usage_page,
                params.usage,
                params.verbose,
            )?;

            Ok(CommandResponse::Legacy { matched: true })
        }

        // --- Typed-command stubs. Dispatch + reply handling land in P1.M3.T3.S1.
        // todo!() expands to `!` (never), which coerces to CommandResponse, so
        // these arms compile UNCHANGED under the new signature. Do NOT wire real
        // logic here. Existing tests only construct ListDevices/SendMessage and
        // never reach these arms. ---
        RunCommand::QueryInfo => todo!("typed dispatch lands in P1.M3.T3.S1"),
        RunCommand::QueryCallback(_) => todo!("typed dispatch lands in P1.M3.T3.S1"),
        RunCommand::SetOs(_) => todo!("typed dispatch lands in P1.M3.T3.S1"),
        RunCommand::ApplyHostContext { .. } => {
            todo!("typed dispatch lands in P1.M3.T3.S1")
        }
    }
}
```

The verbose-printing block, ETX-terminator construction, and the
`send_raw_report` argument list are **byte-for-byte identical to today** — only
the trailing return (and the `?` to consume the `()` Ok) changes. Preserve the
existing inline comments about the typed stubs (or keep equivalent wording).

### 2. `src/lib.rs` — fix the one compile-breaking test arm

In `test_run_with_send_message_command` (~lib.rs:527), change the match arm:

```rust
// OLD (BREAKS: Ok type is now CommandResponse, not ())
        match result {
            Ok(()) => {
                // This is also acceptable if a device is connected
            }
            Err(QmkError::DeviceNotFound { .. }) => {
// ... rest unchanged
// NEW
        match result {
            Ok(_) => {
                // Success is also acceptable if a device is connected. The
                // placeholder path returns CommandResponse::Legacy { matched: true };
                // we deliberately do NOT assert its shape here (no hardware in CI).
            }
            Err(QmkError::DeviceNotFound { .. }) => {
// ... rest unchanged
```

`Ok(_)` is type-agnostic and future-proof — when P1.M3.T3 makes the variant
real, this test still compiles. (An equally-valid alternative is
`Ok(CommandResponse::Legacy { matched: true }) => {}`; `Ok(_)` is preferred
because the test runs without hardware and should not over-bind.)

> **`test_run_with_verbose_output` and `test_run_with_list_devices_command` do
> NOT need editing** — verified: both use
> `assert!(result.is_ok() || result.is_err())`, a type-agnostic tautology that
> compiles unchanged for any `Ok` type. Contract item 3f's "Similarly for
> `test_run_with_verbose_output`" is satisfied by *confirming* it compiles; do
> not edit it unless a compile error appears (it won't).

### 3. `src/main.rs` — print the response on Ok

Convert the `if let Err` form to a `match` that Debug-prints the `CommandResponse`
on the success path (contract item 3e — "Use Debug print for now"; `CommandResponse`
already `#[derive(Debug)]`, so **no Display impl is required**):

```rust
fn main() {
    // Parse CLI arguments and create parameters
    let params = match qmk_notifier::parse_cli_args() {
        Ok(params) => params,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Call the run function with parsed parameters and print the CommandResponse.
    match qmk_notifier::run(params) {
        Ok(response) => println!("{:?}", response),
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    }
}
```

(The `parse_cli_args` match above is unchanged — shown for context.)

### Success Criteria

- [ ] `run()` signature is exactly `pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError>`.
- [ ] `ListDevices` arm calls `list_hid_devices()?;` then returns `Ok(CommandResponse::Timeout)`.
- [ ] `SendMessage` arm calls `send_raw_report(...)?;` (unchanged arg list) then returns `Ok(CommandResponse::Legacy { matched: true })`.
- [ ] The four typed-command arms remain `todo!(...)` stubs (unchanged text).
- [ ] run()'s `///` doc comment describes the new return type, mentions `CommandResponse`, and links PRD §3 and §10.
- [ ] `test_run_with_send_message_command`'s breaking arm is `Ok(_) => {}` (or the explicit `Legacy` variant).
- [ ] `main.rs` prints the response via `println!("{:?}", response)` on `Ok` and still exits 1 on `Err`.
- [ ] `send_raw_report` and `list_hid_devices` signatures are UNCHANGED (still `Result<(), QmkError>`).
- [ ] `cargo build` → zero warnings; `cargo clippy --lib` → zero warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **30 passed, 0 failed**.
- [ ] Only `src/lib.rs` and `src/main.rs` are modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim old→new code
> for all three edits (signature, both arms, the typed-arm non-change, the
> breaking test arm, main.rs) is above; the exact line anchors (~277, ~527,
> main.rs:12) are pinned; the baseline test count (30) and all build/clippy/
> fmt/test commands are verified working; the one non-obvious fact (which test
> actually breaks vs. which merely compiles) is empirically documented with the
> grep evidence. The frozen downstream signatures (`send_raw_report`/
> `list_hid_devices` stay `Result<(), QmkError>`) are called out so the
> implementer does not over-reach into P1.M3.T2 scope.

### Documentation & References

```yaml
# MUST READ — the file being edited (signature + body + the breaking test)
- file: src/lib.rs
  why: "Holds run() (sig at ~line 277), the RunCommand match, RunParameters,
        CommandResponse (already defined by P1.M1.T1.S3), and the tests block
        whose test_run_with_send_message_command (~line 525) has the only
        compile-breaking Ok(()) arm at ~line 527."
  pattern: "run() is a single match on params.command; verbose block + ETX build
            + send_raw_report(...) call are the SendMessage arm body. Typed arms
            are already todo!() stubs (P1.M1.T1.S2). Tests use
            `let result = run(params); match/ assert!(result.is_ok()...)`."
  gotcha: "ONLY test_run_with_send_message_command breaks (line 527 Ok(())).
           test_run_with_list_devices_command (506) and test_run_with_verbose_output
           (555) use is_ok()||is_err() and compile unchanged. Do NOT edit them."

# MUST READ — the other file being edited
- file: src/main.rs
  why: "16-line binary entrypoint. Line 12 `if let Err(e) = qmk_notifier::run(params)`
        discards the Ok value — must become a match that Debug-prints on Ok."
  pattern: "Existing pattern: match on parse_cli_args() result, eprintln+exit(1)
            on Err. Mirror it for run()'s result."
  gotcha: "CommandResponse already #[derive(Debug)] — NO Display impl needed.
           Use {:?} (Debug), not {} (Display)."

# MUST READ — why the Ok type widening is safe for qmkonnect
- file: plan/001_b92a9b2b603f/architecture/findings_and_risks.md
  why: "§R1 (Breaking run() return type) verifies qmkonnect matches Ok(_) => ...
        so the widening is source-compatible. §F2 lists the three breaking
        signature changes and their bottom-up order (burst_to_one ->
        send_raw_report -> run()); this task does ONLY run() (the top), so
        send_raw_report stays Result<(), QmkError> here. §F3 explains why the
        typed arms already need (and have) todo!() stubs. §R5 names the exact
        breaking test arm."
  section: "R1", "F2", "F3", "R5"

# MUST READ — the ecosystem impact + frozen downstream signatures
- file: plan/001_b92a9b2b603f/architecture/system_context.md
  why: "§qmkonnect consumer documents the Ok(_) => ... match (source-compatible)
        and the symbols qmkonnect uses. Confirms send_raw_report/list_hid_devices
        signatures are NOT part of this task."
  section: "Ecosystem Dependencies -> Downstream consumer: qmkonnect"

# MUST READ — the target data-flow + exact placeholder semantics
- file: plan/001_b92a9b2b603f/architecture/transport_evolution.md
  why: "§Data Flow Comparison (v0.3.0 target) shows ListDevices ->
        Ok(CommandResponse::Timeout) and the run() signature target. §Key Design
        Decisions #5/#6 state the EXACT placeholder semantics this task uses
        (ListDevices -> Timeout; SendMessage -> Legacy{matched}). Confirms
        send_raw_report's evolution to Result<Option<Vec<u8>>, QmkError> is a
        LATER task (P1.M3.T2) — out of scope here."
  section: "Data Flow Comparison (v0.3.0 target)", "Key Design Decisions (5,6)"

# REFERENCE — PRD contract for run() + CommandResponse + invariants
- file: PRD.md
  why: "§3 (Public API) gives the exact target signature
        `pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError>;`
        and the CommandResponse enum. §8 (Response Handling) and §10 (Typed-
        Command Transport) explain the reply semantics the doc comment must
        reference. §14 invariant 7 (transport-only) bounds scope."
  section: "3. Public API", "8. Response Handling", "10. Typed-Command Transport"

# REFERENCE — empirical verification of every claim above (call-site census,
# which test breaks, frozen signatures, todo!() coercion, Debug availability)
- docfile: plan/001_b92a9b2b603f/P1M1T2S2/research/notes.md
  why: "Grep-backed evidence for: 30-test baseline, the single Ok(()) break site,
        the two tautology tests that survive, send_raw_report/list_hid_devices
        staying Result<(), QmkError>, todo!() coercing to CommandResponse, and
        CommandResponse already deriving Debug."
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
    ├── main.rs         # <-- FILE TO EDIT (convert if let Err -> match + Debug print)
    ├── core.rs         # DO NOT TOUCH (S1 in parallel adds constants here; send_raw_report/list_hid_devices sigs FROZEN this task)
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # <-- FILE TO EDIT (run() sig+body+doc, fix test arm)
```

### Desired Codebase tree with files to be added/modified

```bash
src/
├── lib.rs   # MODIFIED — run() signature + two placeholder arms + doc comment + 1 test arm fix
└── main.rs  # MODIFIED — if let Err -> match; println!("{:?}", response) on Ok
# (core.rs, error.rs, Cargo.toml unchanged)
```

> No new files are created. No new dependencies. Two files modified.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: send_raw_report (core.rs:113) and list_hid_devices (core.rs:56)
//   STILL return Result<(), QmkError> in this task. Their evolution to
//   Result<Option<Vec<u8>>, QmkError> / reply capture is P1.M3.T1/P1.M3.T2,
//   NOT this task. So in run()'s SendMessage arm you MUST write
//   `send_raw_report(...)?; Ok(CommandResponse::Legacy { matched: true })`
//   (consume the () Ok with ?, then build the placeholder). Do NOT rewrite it
//   as `send_raw_report(...).map(|_| ...)` in a way that implies the Ok type
//   changed, and do NOT change send_raw_report's signature.

// CRITICAL: ONLY ONE test arm breaks — `src/lib.rs:527` `Ok(()) => {` inside
//   test_run_with_send_message_command. The Ok type widens from `()` to
//   `CommandResponse`, so `Ok(())` is a type mismatch (E0308). Fix = `Ok(_) =>`.
//   The other two run()-calling tests use `assert!(result.is_ok() || result.is_err())`
//   which is type-agnostic and compiles UNCHANGED — leave them alone.

// CRITICAL: the four typed-command arms (QueryInfo/QueryCallback/SetOs/
//   ApplyHostContext) ALREADY exist as `todo!(...)` stubs from P1.M1.T1.S2
//   (complete). `todo!()` expands to the never type `!`, which coerces to ANY
//   type including `Result<CommandResponse, QmkError>`. So when run()'s match
//   unifies on the new return type, these arms COMPILE UNCHANGED. Do NOT add,
//   remove, rewrite, or "fix" them. (If you delete one, the match becomes
//   non-exhaustive and fails to compile — that is the only way to break them.)

// NOTE: CommandResponse already derives Debug (lib.rs #[derive(Debug, Clone,
//   PartialEq, Eq)]). main.rs can `println!("{:?}", response)` with NO new
//   impl. Do NOT add a Display impl — contract says "Use Debug print for now."

// NOTE: there is NO crate-level `//!` doc in lib.rs (line 1 is `mod core;`).
//   Contract item 5 "the lib.rs-level doc comment should mention CommandResponse"
//   therefore means run()'s `///` doc comment (the primary lib.rs public item),
//   NOT a new crate-level doc. Scope = rewrite run()'s doc comment only.

// NOTE: no rustfmt.toml / clippy.toml exist — default configs. `cargo fmt`
//   reformats to default style; `cargo fmt --check` is the CI gate. The
//   proposed code is already rustfmt-clean, but always run `cargo fmt` after
//   editing to be safe (e.g., the `?` + placeholder reflow).

// NOTE: qmkonnect (downstream) matches `Ok(_) => ...` (findings §R1), so
//   widening the Ok type is source-compatible — qmkonnect recompiles without
//   edits. qmkonnect's eventual USE of CommandResponse is a separate follow-up;
//   this crate's v0.3.0 release is the prerequisite. Do not change QmkError
//   Display strings (findings §F5 — qmkonnect string-matches them).

// NOTE: this task runs in PARALLEL with P1.M1.T2.S1 (adds typed-command
//   constants to core.rs ONLY). Zero file overlap (S2 touches lib.rs+main.rs,
//   S1 touches core.rs) -> no merge conflict, either order lands cleanly.
```

## Implementation Blueprint

### Data models and structure

No new types. `CommandResponse` already exists (P1.M1.T1.S3, complete) with
exactly the variants this task needs: `Legacy { matched: bool }` and `Timeout`
(the placeholders), plus `Info`/`CallbackName`/`Ack` (unreachable until
P1.M3.T3). `RunCommand`, `HostOs`, `RunParameters` are all already complete from
P1.M1.T1. This task consumes existing types; it does not define any.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ current state and confirm anchors
  - READ src/lib.rs run() (~line 277): confirm signature, the SendMessage arm
          body (verbose block + ETX build + send_raw_report(...) tail), the
          ListDevices arm (`=> list_hid_devices()`), and the four todo!() typed
          arms (~322-327). Confirm run()'s one-line `///` doc comment above it.
  - READ src/lib.rs test_run_with_send_message_command (~525): confirm the
          `Ok(()) => {` arm at ~line 527 (the ONLY break site).
  - READ src/main.rs (16 lines): confirm line 12 `if let Err(e) = qmk_notifier::run(params)`.
  - READ src/core.rs:56 and src/core.rs:113: confirm list_hid_devices and
          send_raw_report STILL return Result<(), QmkError> (frozen this task).
  - GOAL: know exact anchors so all edits are surgical and you do not touch
          frozen signatures or the surviving tautology tests.

Task 2: EDIT src/lib.rs — run() signature + doc comment
  - CHANGE: `pub fn run(params: RunParameters) -> Result<(), QmkError>` ->
          `pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError>`.
  - REPLACE: the one-line `///` doc comment with the multi-line `///` block in
          the "What" section (describes return type, mentions CommandResponse,
          links PRD §3 + §10, documents the placeholders).
  - DO NOT: touch RunCommand/HostOs/CommandResponse/RunParameters definitions
            (all complete from P1.M1.T1), parse_cli_args, or any other function.

Task 3: EDIT src/lib.rs — the two real match arms (placeholders)
  - CHANGE ListDevices arm: `RunCommand::ListDevices => list_hid_devices(),` ->
          a block: `list_hid_devices()?; Ok(CommandResponse::Timeout)`.
          (Add a one-line comment: "Semantic: no device reply was received —
          nothing was sent.")
  - CHANGE SendMessage arm: after the existing verbose block + ETX build, change
          the trailing `send_raw_report(...)` (returned directly) to
          `send_raw_report(...)?; Ok(CommandResponse::Legacy { matched: true })`.
          KEEP the verbose println! blocks and the input_with_terminator build
          byte-for-byte identical. (Add a comment noting send_raw_report still
          returns Result<(),QmkError> and real parsing is P1.M3.T3.)
  - KEEP: the four todo!() typed arms UNCHANGED (they coerce via `!`).
  - DO NOT: change send_raw_report's or list_hid_devices's signatures, add
            real typed dispatch, or call build_command_data/parse_reply (those
            are P1.M2/P1.M3 and don't exist yet).

Task 4: EDIT src/lib.rs — fix the breaking test arm
  - CHANGE: in test_run_with_send_message_command, `Ok(()) => {` (~line 527) ->
          `Ok(_) => {`. Update the inline comment to note the placeholder path
          returns Legacy{matched:true} and we deliberately don't assert shape
          (no hardware in CI).
  - DO NOT: edit test_run_with_list_devices_command (506) or
            test_run_with_verbose_output (555) — they use is_ok()||is_err() and
            compile unchanged. Only touch them IF a compile error appears (none will).

Task 5: EDIT src/main.rs — print the response on Ok
  - CHANGE: `if let Err(e) = qmk_notifier::run(params) { ... }` (lines 12-15) ->
          `match qmk_notifier::run(params) { Ok(response) => println!("{:?}", response),
          Err(e) => { eprintln!("Error: {}", e); std::process::exit(1); } }`.
  - KEEP: the parse_cli_args() match above (lines 3-9) byte-for-byte identical.
  - DO NOT: add a Display impl for CommandResponse (Debug is enough; contract
            says "Use Debug print for now"). Do NOT add verbose/pretty printing.

Task 6: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --lib`, then
          `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0;
          test result "30 passed; 0 failed" (baseline 30; no tests added/removed).
  - IF "mismatched types: expected `CommandResponse`, found `()`" (E0308) fires
          anywhere: you left an `Ok(())` match arm or a `=> list_hid_devices()`
          arm returning `()` into the new CommandResponse-typed match — convert
          to the placeholder form.
  - IF "variant `RunCommand::X` not covered" (E0004) fires: you accidentally
          deleted a todo!() typed arm — restore it.
  - IF a `dead_code`/`unused` warning fires on `response`: main.rs isn't using
          the bound Ok value — confirm the `println!("{:?}", response)` arm.
```

### Implementation Patterns & Key Details

```rust
// === THE ONLY PLACEHOLDER PATTERN THIS TASK USES (consume () Ok with ?, then
//     build the CommandResponse). send_raw_report/list_hid_devices return
//     Result<(), QmkError> — `?` strips the Ok(()) and propagates any Err. ===
//
//   list_hid_devices()?;                              // () Ok discarded
//   Ok(CommandResponse::Timeout)                      // placeholder built
//
//   send_raw_report(/* exact old arg list */)?;       // () Ok discarded
//   Ok(CommandResponse::Legacy { matched: true })     // placeholder built


// === WHY todo!() ARMS NEED NO CHANGE ===
//   todo!() -> !  (never type). `!` coerces to any type, so
//   `RunCommand::QueryInfo => todo!(...)` type-checks whether the match unifies
//   on Result<(), QmkError> (today) or Result<CommandResponse, QmkError> (after
//   this task). Removing an arm breaks exhaustiveness (E0004); leaving them is
//   correct. They become real dispatch in P1.M3.T3.S1.


// === WHY Ok(_) IN THE FIXED TEST (not the explicit variant) ===
//   test_run_with_send_message_command runs in CI without hardware, so run()
//   usually returns Err(DeviceNotFound). When a device IS present it returns the
//   placeholder Legacy{matched:true}. Binding `Ok(_)` (not
//   `Ok(CommandResponse::Legacy{..})`) keeps the test forward-compatible when
//   P1.M3.T3 changes the real variant shape, and avoids over-asserting on a
//   no-hardware path. Both forms compile; `Ok(_)` is preferred.


// === WHY {:?} (Debug) IN main.rs (not {} Display) ===
//   CommandResponse derives Debug but NOT Display. Implementing Display now is
//   out of scope (contract: "Use Debug print for now"). `println!("{:?}", response)`
//   prints e.g. `Legacy { matched: true }` / `Timeout` — sufficient for the
//   diagnostic CLI. A human-friendly Display can land with the CLI subcommands
//   (P1.M4.T1) if desired.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY — run() sig + 2 arms + doc comment + 1 test arm"
  - modify: "src/main.rs ONLY — run() call site: if let Err -> match + Debug print"

FROZEN THIS TASK (DO NOT TOUCH — evolution is later milestones):
  - src/core.rs:56  list_hid_devices() -> Result<(), QmkError>   # never sends; no reply
  - src/core.rs:113 send_raw_report(...) -> Result<(), QmkError>  # evolves in P1.M3.T2
  - src/core.rs     burst_to_one(...) -> bool                     # evolves in P1.M3.T1
  - src/error.rs    QmkError Display strings                      # qmkonnect string-matches (§F5)
  - Cargo.toml      deps cleanup is P1.M4.T2.S1

PUBLIC API SURFACE:
  - changes:  "run() return type Result<(),QmkError> -> Result<CommandResponse,QmkError>
              (BREAKING for any consumer that binds the Ok value; qmkonnect uses Ok(_)
              so it is source-compatible — findings §R1)."
  - unchanged: "HostOs, RunCommand (+variants), CommandResponse (+variants), RunParameters,
               parse_cli_args, all core:: re-exports, all QmkError variants/Display."

DOWNSTREAM CONSUMERS (awareness; do NOT edit them here):
  - qmkonnect (src/core/notifier.rs:~156): `match qmk_notifier::run(params) { Ok(_) => ... }`
        — recompiles unchanged. A follow-up qmkonnect task will capture
        CommandResponse for the capability handshake.
  - main.rs (this repo): updated in Task 5 to Debug-print the response.

SCOPE BOUNDARY:
  - ONLY src/lib.rs and src/main.rs are modified. Do NOT edit core.rs (S1 in
    parallel owns it this milestone), error.rs, or Cargo.toml. Do NOT implement
    build_command_data / parse_reply / burst_to_one reply capture — those are
    P1.M2/P1.M3. Do NOT add CLI subcommands — that is P1.M4.T1.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited files (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# If you see E0308 "mismatched types: expected `CommandResponse`, found `()`":
#   an arm still returns `()` into the new CommandResponse-typed match —
#   convert it to the placeholder form (see Task 3).
# If you see E0004 "... not covered": you deleted a todo!() typed arm — restore it.

# Lint (default clippy — no clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the repaired test in isolation first.
cargo test --lib test_run_with_send_message_command -- --nocapture
# Expected: 1 passed (the Ok(_) arm now type-checks).

# Run the full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 30 passed; 0 failed; 0 ignored; ..." (baseline 30;
# no tests added or removed — only the one breaking arm repaired).

# Sanity: confirm the two surviving run()-caller tests still pass untouched.
cargo test --lib test_run_with_list_devices_command -- --nocapture
cargo test --lib test_run_with_verbose_output -- --nocapture
# Expected: both pass (they use the type-agnostic is_ok()||is_err() assertion).

# Sanity: the CommandResponse construction tests (from P1.M1.T1.S3) still pass.
cargo test --lib test_command_response -- --nocapture
# Expected: all CommandResponse tests pass.
```

### Level 3: Integration Testing (System Validation)

```bash
# Binary builds (main.rs change must compile into the binary).
cargo build --bin qmk_notifier
# Expected: "Finished `dev` profile ..." (the binary target name is qmk_notifier
# per Cargo.toml; verify with `cargo build` if unsure).

# CLI smoke: --list exercises the ListDevices -> Ok(CommandResponse::Timeout)
# path and main.rs now Debug-prints it. (No keyboard needed.)
cargo run -- --list 2>&1 | tail -5
# Expected: device-listing output (from list_hid_devices) followed by a final
# "Timeout" line printed by main.rs's new println!("{:?}", response).
# (If the system has no HID devices, list_hid_devices may still succeed and
# print "Timeout"; an Err path prints "Error: ..." and exits 1 — also fine.)

# CLI smoke: a SendMessage to a non-existent device exercises the Err path
# (send_raw_report -> DeviceNotFound), which main.rs prints and exits 1 on.
cargo run -- "test" -i 0x0000 -p 0x0000 ; echo "exit=$?"
# Expected: "Error: No device found matching criteria: ..." (or similar) and
# exit=1. This confirms the Err arm of the new match is wired identically to
# the old `if let Err` form.

# CLI smoke: typed command must NOT be reachable from CLI yet (it isn't wired),
# but confirm the library todo!() panics are isolated to the library path:
#   (no CLI flag exists for QueryInfo etc. — P1.M4.T1 adds them.)
# Nothing to run here; this is a negative-confirmation note.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the signature change is the ONLY public-API break and nothing else
# regressed: diff the public symbols before/after via the compiler's view.
# (No special tooling — rely on `cargo build` + the qmkonnect-compat note.)
cargo build 2>&1 | grep -iE "warning|error" || echo "build: clean (good)"

# Cross-check: the four typed arms still panic on todo!() (not silently
# returning a wrong variant). This is a guard against accidentally wiring a
# placeholder return for a typed command.
cargo test --lib test_run_command 2>&1 | tail -3
# Expected: the construction tests (test_run_command_query_variants_construction,
# _set_os_variant_construction, _apply_host_context_construction) pass — they
# construct variants but do NOT call run(), so no todo!() is hit. (run() is only
# called with ListDevices/SendMessage in the existing tests.)

# qmkonnect-compat mental check (no command to run — documented invariant):
# qmkonnect matches `Ok(_) => ...` (findings §R1). Our widening keeps `Ok(_) =>
# ...` valid. If a future qmkonnect bump captures the variant, THIS crate's
# `Legacy{matched:true}`/`Timeout` placeholders are what it will initially see.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings (no E0308/E0004, no dead_code).
- [ ] Level 1 passed: `cargo clippy --lib` → zero warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 30 passed, 0 failed.
- [ ] Level 3 passed: `cargo build --bin qmk_notifier` compiles; `cargo run -- --list` prints `Timeout`.

### Feature Validation

- [ ] `run()` signature is `Result<CommandResponse, QmkError>`.
- [ ] `ListDevices` arm returns `Ok(CommandResponse::Timeout)` (after `list_hid_devices()?;`).
- [ ] `SendMessage` arm returns `Ok(CommandResponse::Legacy { matched: true })` (after `send_raw_report(...)?;`).
- [ ] Typed arms remain `todo!()` stubs (unchanged).
- [ ] run()'s doc comment describes the return type, mentions `CommandResponse`, links PRD §3 + §10.
- [ ] `test_run_with_send_message_command` fixed to `Ok(_) => {}`.
- [ ] `main.rs` Debug-prints the response on `Ok`, exits 1 on `Err`.
- [ ] `send_raw_report` and `list_hid_devices` signatures unchanged.

### Code Quality Validation

- [ ] Follows existing run()-match + verbose-block conventions ( SendMessage arm body preserved).
- [ ] Placeholder comments explain WHY each value is a placeholder + which task replaces it.
- [ ] No new patterns invented (reuses `?` + construct-Ok, existing match idiom).
- [ ] No Display impl added (Debug-only, per contract).
- [ ] Frozen signatures (core.rs:56, core.rs:113) untouched.
- [ ] No file other than `src/lib.rs` and `src/main.rs` modified.

### Documentation & Deployment

- [ ] run()'s `///` doc comment updated (Mode A — no separate docs file).
- [ ] Doc comment cites PRD §3 (Public API) and §10 (Typed-Command Transport).
- [ ] Placeholder behavior documented inline (Legacy/Timeout semantics + P1.M3.T3 successor).
- [ ] No new environment variables or config.
- [ ] No README/PRD/Cargo.toml change (out of scope).

---

## Anti-Patterns to Avoid

- ❌ Don't change `send_raw_report`'s or `list_hid_devices`'s signatures — they
  stay `Result<(), QmkError>` this task (their evolution is P1.M3.T1/P1.M3.T2).
  Use `?` to consume the `()` Ok, then build the placeholder.
- ❌ Don't add `build_command_data`/`parse_reply`/real typed dispatch or reply
  capture — those are P1.M2/P1.M3 and don't exist yet. The typed arms MUST stay
  `todo!()`.
- ❌ Don't delete or "clean up" the four `todo!()` typed arms — removing one makes
  the match non-exhaustive (E0004). They compile unchanged because `!` coerces to
  `CommandResponse`.
- ❌ Don't edit `test_run_with_list_devices_command` or `test_run_with_verbose_output`
  — verified: their `assert!(result.is_ok() || result.is_err())` compiles for any
  Ok type. Only `test_run_with_send_message_command`'s `Ok(())` breaks.
- ❌ Don't add a `Display` impl for `CommandResponse` — Debug (`{:?}`) is
  sufficient and is what the contract specifies ("Use Debug print for now").
- ❌ Don't introduce a crate-level `//!` doc — none exists and the contract's
  "lib.rs-level doc comment" means run()'s `///` doc. Scope = run()'s doc only.
- ❌ Don't change any `QmkError` `Display` string — qmkonnect string-matches them
  (findings §F5). This task doesn't touch error.rs anyway.
- ❌ Don't over-assert in the repaired test — `Ok(CommandResponse::Legacy{..})`
  is valid but `Ok(_)` is preferred (no hardware in CI; forward-compatible with
  P1.M3.T3's real variant shape).
- ❌ Don't touch `core.rs` — P1.M1.T2.S1 is running in parallel and owns it this
  milestone. This task's edits are confined to `lib.rs` + `main.rs`.
- ❌ Don't skip `cargo build` because "it's just a signature change" — the E0308
  mismatch on the test arm is exactly the kind of thing the compiler catches, and
  a missed `Ok(())` anywhere else would surface here.
- ❌ Don't bump the crate version or edit Cargo.toml — version 0.2.1→0.3.0 is
  P1.M4.T2.S1, not this task.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is three small surgical edits (one signature + two placeholder arms + one doc
comment in lib.rs; one test-arm fix in lib.rs; one `if let`→`match` rewrite in
main.rs) with verbatim old→new code provided for each. Every claim is
grep-verified: exactly one test arm breaks (`lib.rs:527`), the other two
run()-caller tests compile unchanged, `send_raw_report`/`list_hid_devices` stay
`Result<(), QmkError>`, the typed `todo!()` arms coerce via `!`, and
`CommandResponse` already derives Debug. The parallel task (P1.M1.T2.S1) owns
core.rs exclusively — zero file overlap, no merge risk. Baseline (30 tests) and
all build/clippy/fmt/test commands are verified working in this repo.