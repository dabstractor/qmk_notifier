name: "P1.M2.T1.S1 — Reconcile payload builder into build_command_data (SendMessage + single-report typed commands)"
description: |

---

> ## ⚠️ READ THIS FIRST — THIS IS A RE-PLANNING (attempt 2) PRP, NOT A GREENFIELD ADD
>
> **Attempt 1 HALTED** because the uncommitted working tree held a divergent
> function `build_typed_payload`. That divergence is **now COMMITTED** (working
> tree clean, HEAD `828998e`) and the entire P1.M3 dispatch layer (parse_reply,
> burst_to_one reply capture, send_raw_report, run() dispatch — all **Complete**)
> is built on top of it. Therefore:
> - This PRP is a **REFACTOR / RECONCILIATION of already-working, committed,
>   tested code** toward the architecture doc's canonical `build_command_data`
>   design. It is NOT "add a brand-new function to an empty file".
> - **Do NOT create a second builder function.** There is exactly ONE payload
>   builder; you are RENAMING + EXTENDING it.
> - **Do NOT revert `ApplyHostContext` to a `todo!()` stub.** S2 (P1.M2.T1.S2)
>   is **Complete** and its `ApplyHostContext` arm is correct, committed, and
>   tested. It MUST stay fully implemented. (This is the explicit resolution to
>   attempt-1's open question.)
> - **Do NOT `git checkout`/revert anything.** The committed state is the
>   starting line. Start from `git status` == clean.

---

## Goal

**Feature Goal**: Reconcile the existing payload-builder into a single pure
`build_command_data(&RunCommand) -> Vec<u8>` in `src/core.rs` that produces the
correct logical payload for **every** `RunCommand` variant — including
`SendMessage` (which today is framed inline in `lib.rs`, not by the builder) —
matching the canonical design in
`plan/001_b92a9b2b603f/architecture/transport_evolution.md` §build_command_data.

**Deliverable**: One renamed/extended function `build_command_data` in
`src/core.rs`; a new named `ETX_TERMINATOR_BYTE` const; a simplified
`build_payload` in `src/lib.rs` that delegates ALL arms to `build_command_data`;
renamed/adjusted unit tests. `ApplyHostContext` stays fully implemented.

**Success Definition**: `cargo test --lib` reports **64 tests passing** (was 63;
net +1 from splitting the non-typed test into SendMessage + ListDevices), zero
dead-code/clippy warnings, and the on-wire bytes for every variant are exactly
as specified by `firmware_wire_contract.md`. No behavior change on the wire —
the refactor is pure code-organization + naming alignment with the architecture
doc; SendMessage still emits `[string_bytes…, 0x03]`, typed commands still emit
`[0xF0][cmd][args…][0x03]`.

## Why

- **Architecture-doc alignment.** `transport_evolution.md` §build_command_data +
  Design Decision #1 mandate ONE pure function handling ALL variants ("The
  caller (`run()`) appends nothing — ETX is already in the payload"). The
  committed code split this responsibility (`build_typed_payload` for typed,
  inline framing in `lib.rs::build_payload` for SendMessage). This PRP reunifies
  them so there is a single source of truth for wire payloads — the form the
  architecture doc, the task title, and every downstream spec reference.
- **One-pass safety.** Attempt 1 failed because the PRP assumed a greenfield add
  against a clean HEAD; the codebase had moved on. This PRP starts from the real
  committed state and performs only mechanical, fully-test-covered edits, so a
  single pass produces a coherent result.
- **Preserves completed work.** S2 (`ApplyHostContext`) and all of P1.M3 stay
  intact. Nothing is reverted; nothing downstream is broken.

## What

The single payload-builder function in `src/core.rs` is renamed
`build_typed_payload` → `build_command_data`, gains a real `SendMessage` arm and
a `ListDevices` (empty) arm, and gains a named `ETX_TERMINATOR_BYTE` const that
replaces the literal `0x03`. `src/lib.rs::build_payload` is simplified to
delegate every arm to `core::build_command_data`, preserving only the
SendMessage verbose "Message length" println as a side-effect. All builder unit
tests are renamed `build_typed_payload_*` → `build_command_data_*`, and the one
test that asserted `SendMessage` returns empty is split into a SendMessage test
(asserting `[bytes, 0x03]`) and a ListDevices test (asserting empty).

### Success Criteria

- [ ] `src/core.rs` has `pub(crate) const ETX_TERMINATOR_BYTE: u8 = 0x03;`
- [ ] `src/core.rs` has `pub(crate) fn build_command_data(command: &RunCommand) -> Vec<u8>` and NO function named `build_typed_payload` remains anywhere
- [ ] `build_command_data` handles ALL six `RunCommand` variants: `SendMessage` → `[msg.as_bytes(), ETX]`; `QueryInfo`/`QueryCallback`/`SetOs`/`ApplyHostContext` → `[0xF0, cmd, args, ETX]`; `ListDevices` → empty `Vec`
- [ ] The `ApplyHostContext` arm is **unchanged from the current committed implementation** (still builds `[0xF0][0x05][layer][flags][count.clamp(255)][ids…]`) — NOT a `todo!()`
- [ ] No literal `0x03` ETX push remains in `build_command_data`; all use `ETX_TERMINATOR_BYTE`
- [ ] `src/lib.rs::build_payload` delegates ALL arms to `core::build_command_data` and still prints the SendMessage "Message length" line when `verbose`
- [ ] `cargo test --lib` → 64 passed, 0 failed
- [ ] `cargo build` + `cargo clippy --all-targets` → zero warnings (no dead_code: `build_command_data` has a live caller via `build_payload` → `run`)

## All Needed Context

### Context Completeness Check

_If someone knew nothing about this codebase, would they have everything needed
to implement this successfully?_ **Yes** — every file, line range, exact byte
sequence, and test is pinned below. The refactor is mechanical.

### Documentation & References

```yaml
# MUST READ — the canonical design this PRP reconciles TOWARD
- file: plan/001_b92a9b2b603f/architecture/transport_evolution.md
  why: §build_command_data (Signature Changes #1) + §Key Design Decisions #1
  critical: |
    Design Decision #1, verbatim: "build_command_data is pure ... Takes
    &RunCommand, returns Vec<u8>. ... The caller (run()) appends nothing —
    ETX is already in the payload." This is the WHOLE POINT of the
    reconciliation: SendMessage framing must move INTO build_command_data.

# MUST READ — exact byte layout for every command (the source of truth)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: §Typed-Command Framing (wire diagram) + §Command Table + §Field Definitions
  critical: |
    Wire layout = [0x00][0x81][0x9F][payload][0x03]. The builder emits ONLY the
    [payload][0x03] part; burst_to_one prepends 0x00/0x81/0x9F at buffer [0..3].
    SendMessage payload = raw string bytes + 0x03 (NO 0xF0 discriminator).
    Typed payload = [0xF0][cmd_id][args…][0x03]. ETX 0x03 is appended by THIS
    crate (PRD §14 invariant 1).

# MUST READ — the function being renamed/extended (STARTING STATE)
- file: src/core.rs
  why: lines ~416-462 hold the current `build_typed_payload` to be renamed
  pattern: |
    push CMD_DISCRIMINATOR (0xF0) first, then match arm pushes cmd-id + args,
    then a single trailing `payload.push(0x03)` after the match. SendMessage |
    ListDevices early-`return Vec::new()`. This trailing-push structure is
    EXACTLY what we keep for typed arms; SendMessage needs its OWN terminator
    (it returns BEFORE the typed discriminator push).

# MUST READ — the caller to simplify (STARTING STATE)
- file: src/lib.rs
  why: lines ~365-396 hold `fn build_payload(command, verbose)` whose SendMessage
        arm builds the payload inline; lines ~430-485 hold `run()` dispatch.
  pattern: |
    build_payload currently: SendMessage => inline [bytes,0x03] + verbose print;
    typed => core::build_typed_payload(command); ListDevices => empty.
    AFTER: every arm delegates to core::build_command_data; only the SendMessage
    verbose println survives as a post-call side-effect.

# PRD invariants (selected for this task)
- doc: PRD §4 (wire protocol) + §14 invariants 1 & 5
  why: |
    §14.1: magic 0x81 0x9F, ETX 0x03 — caller builds class\x1Dtitle, crate
    appends ETX. §14.5: typed commands reuse same framing as strings,
    [0x81][0x9F][0xF0][cmd][args][0x03].
```

### Current Codebase tree (relevant slice)

```bash
src/
├── core.rs        # constants (L13-43), build_typed_payload (L416-462) ← RENAME, tests (L839-1037)
├── lib.rs         # RunCommand/HostOs/CommandResponse enums, build_payload (L372-396) ← SIMPLIFY, run() (L425-485)
└── error.rs       # QmkError (unchanged)
```

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL: SendMessage must NOT get the 0xF0 discriminator. It is the LEGACY
// string path: payload = raw bytes + 0x03 only. The current build_typed_payload
// early-returns empty for SendMessage precisely because it always pushes 0xF0
// first. When you add the SendMessage arm to build_command_data, it must
// return BEFORE the discriminator push, e.g.:
//   match command {
//       RunCommand::SendMessage(msg) => {
//           let mut data = msg.as_bytes().to_vec();
//           data.push(ETX_TERMINATOR_BYTE);
//           return data;          // <-- return here, do NOT fall through to push(0xF0)
//       }
//       ...
//   }

// GOTCHA: the typed arms share a single trailing `payload.push(0x03)` AFTER the
// match. SendMessage must terminate INSIDE its arm (it returns early). Make
// sure you don't accidentally double-push ETX for SendMessage.

// GOTCHA: dead_code is a NON-ISSUE here. build_command_data is consumed by
// lib.rs::build_payload -> run(), so it already has a live caller. Do NOT add
// #[allow(dead_code)] to it. The 5 command constants already have their
// #[allow(dead_code)] removed (consumed by the builder). Leave them as-is.

// GOTCHA: ApplyHostContext MUST stay fully implemented. S2 (P1.M2.T1.S2) is
// Complete. Reverting it to todo!() would be a regression. Do not touch its
// body except to rename the enclosing function.

// GOTCHA: do not rename the ETX literal in burst_to_one (0x81/0x9F magic
// header) — that is a different constant concern. Only the builder's 0x03 ETX
// becomes ETX_TERMINATOR_BYTE.
```

## Implementation Blueprint

### Data models and structure

No data-model changes. `RunCommand`, `HostOs`, `CommandResponse` (all in
`src/lib.rs`) are unchanged. This task only reorganizes the pure payload-builder
function + one const + tests.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: ADD the named ETX const in src/core.rs (near the typed-command constants, ~line 43)
  - ADD: `/// ETX terminator byte appended to every on-wire payload (PRD §14 invariant 1).`
         `pub(crate) const ETX_TERMINATOR_BYTE: u8 = 0x03;`
  - PLACEMENT: immediately after the existing `REPLY_READ_TIMEOUT_MS` const block (L~43)
  - NAMING: ETX_TERMINATOR_BYTE (snake_case const, matches item contract)
  - NO #[allow(dead_code)]: it is consumed by build_command_data the instant Task 2 lands

Task 2: RENAME build_typed_payload -> build_command_data in src/core.rs (L416)
  - FIND: `pub(crate) fn build_typed_payload(cmd: &crate::RunCommand) -> Vec<u8> {` (~L416)
  - REPLACE signature line with: `pub(crate) fn build_command_data(command: &crate::RunCommand) -> Vec<u8> {`
  - RENAME the parameter `cmd` -> `command` throughout the function body for readability (optional but matches architecture doc)
  - UPDATE the doc-comment block above it (L~379-415): replace every `build_typed_payload` mention with `build_command_data`; revise the "Non-typed variants ... return an empty payload" sentence — SendMessage now returns a REAL payload; only ListDevices returns empty.
  - DO NOT touch the ApplyHostContext arm body (S2 code stays verbatim)
  - DEPENDENCIES: Task 1 (uses ETX_TERMINATOR_BYTE)

Task 3: EXTEND build_command_data with the SendMessage arm + replace literal 0x03
  - In the `match cmd {` body, the FIRST arm (before QueryInfo) becomes:
      RunCommand::SendMessage(msg) => {
          let mut data = msg.as_bytes().to_vec();
          data.push(ETX_TERMINATOR_BYTE);
          return data;            // early return: NO discriminator, single ETX
      }
  - REMOVE the old `RunCommand::SendMessage(_) | RunCommand::ListDevices => return Vec::new(),` line
  - ADD (replaces the ListDevices half of the removed line): `RunCommand::ListDevices => return Vec::new(),`  (kept defensive; run() never calls the builder for ListDevices, but keep it inert)
  - REPLACE the trailing `payload.push(0x03);` (typed-arm terminator, ~L461) with `payload.push(ETX_TERMINATOR_BYTE);`
  - GOTCHA: SendMessage returns BEFORE `payload.push(CMD_DISCRIMINATOR)` — verify no 0xF0 leaks into a SendMessage payload
  - DEPENDENCIES: Tasks 1+2

Task 4: SIMPLIFY build_payload in src/lib.rs (L372-396)
  - FIND: `fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8> {` (~L372)
  - REPLACE the entire match body with a delegation + verbose side-effect:
      fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8> {
          let data = core::build_command_data(command);
          if verbose {
              if let RunCommand::SendMessage(_) = command {
                  println!(
                      "Message length: {} bytes (including ETX terminator)",
                      data.len()
                  );
              }
          }
          data
      }
  - UPDATE the doc-comment above build_payload (~L365-371): it now delegates ALL arms to `core::build_command_data`; remove the claim that SendMessage framing happens "HERE, not by build_typed_payload" (that was the divergence being fixed). New text: "Delegates every arm to `core::build_command_data` (the single source of truth for on-wire payloads); preserves the SendMessage verbose length print as a side-effect."
  - FIND the stale comment in run() (~L437): `// straight to build_payload / core::build_typed_payload.` -> change `build_typed_payload` -> `build_command_data`
  - FIND run() doc (~L406-411) references to build_typed_payload if any -> build_command_data
  - DEPENDENCIES: Task 3

Task 5: RENAME + ADJUST unit tests in src/core.rs (L839-1037)
  - RENAME every test fn `build_typed_payload_*` -> `build_command_data_*` (9 fns):
      build_typed_payload_query_info                          -> build_command_data_query_info
      build_typed_payload_query_callback                      -> build_command_data_query_callback
      build_typed_payload_set_os                              -> build_command_data_set_os
      build_typed_payload_apply_host_context_set_layer        -> build_command_data_apply_host_context_set_layer
      build_typed_payload_apply_host_context_clear_layer      -> build_command_data_apply_host_context_clear_layer
      build_typed_payload_multi_report_chunking               -> build_command_data_multi_report_chunking
      build_typed_payload_apply_host_context_representative_ids -> build_command_data_apply_host_context_representative_ids
      build_typed_payload_apply_host_context_clamps_count_at_255 -> build_command_data_apply_host_context_clamps_count_at_255
    In each, replace every call `build_typed_payload(` -> `build_command_data(`. Bodies/assertions UNCHANGED (byte outputs identical).
  - SPLIT the test `build_typed_payload_non_typed_returns_empty` (L~939-948) into TWO tests:
      // NEW — SendMessage now produces a REAL payload (reconciliation with architecture doc)
      #[test]
      fn build_command_data_send_message() {
          // Legacy string path: raw bytes + ETX, NO 0xF0 discriminator.
          let payload = build_command_data(&RunCommand::SendMessage("App\x1DTitle".to_string()));
          let mut expected = "App\x1DTitle".as_bytes().to_vec();
          expected.push(ETX_TERMINATOR_BYTE);
          assert_eq!(payload, expected);
          assert!(!payload.contains(&CMD_DISCRIMINATOR), "SendMessage must NOT carry the 0xF0 discriminator");
          assert_eq!(*payload.last().unwrap(), ETX_TERMINATOR_BYTE, "must end with ETX");
      }
      // KEPT — ListDevices is not a wire command; builder returns empty defensively.
      #[test]
      fn build_command_data_list_devices_empty() {
          assert_eq!(build_command_data(&RunCommand::ListDevices), Vec::new());
      }
  - ALSO add an explicit ETX-const value test near the existing constants tests (optional but cheap):
      #[test] fn etx_terminator_byte_is_0x03() { assert_eq!(ETX_TERMINATOR_BYTE, 0x03); }
  - Net test delta: -1 (split source) +2 (split targets) +1 (ETX const) = +2 over the 9 renamed; total lib tests 63 -> 64... -> actually 65 with the ETX test. Use 64 as the floor; 65 acceptable. (See Validation: assert >= 64.)
  - DEPENDENCIES: Tasks 1-4

Task 6: VERIFY no stale references remain
  - RUN: `rg -n "build_typed_payload" src/` -> MUST return nothing
  - RUN: `rg -n "build_command_data" src/` -> confirms renames landed in core.rs + lib.rs
  - RUN: `cargo build` -> zero warnings
  - DEPENDENCIES: Task 5
```

### Implementation Patterns & Key Details

```rust
// PATTERN — the reconciled build_command_data (target shape). NOTE: SendMessage
// returns EARLY (no discriminator); typed arms share one trailing ETX push.
pub(crate) fn build_command_data(command: &crate::RunCommand) -> Vec<u8> {
    use crate::RunCommand;

    // Legacy string path: raw bytes + single ETX. NO 0xF0 discriminator.
    if let RunCommand::SendMessage(msg) = command {
        let mut data = msg.as_bytes().to_vec();
        data.push(ETX_TERMINATOR_BYTE);
        return data;
    }
    // Not a wire command — builder is defensive (run() never calls us for this).
    if matches!(command, RunCommand::ListDevices) {
        return Vec::new();
    }

    // Typed path: [0xF0][cmd_id][args…][ETX]
    let mut payload = Vec::new();
    payload.push(CMD_DISCRIMINATOR);
    match command {
        RunCommand::QueryInfo => { payload.push(CMD_QUERY_INFO); }
        RunCommand::QueryCallback(index) => { payload.push(CMD_QUERY_CALLBACK); payload.push(*index); }
        RunCommand::SetOs(os) => { payload.push(CMD_SET_OS); payload.push(*os as u8); }
        RunCommand::ApplyHostContext { layer, callbacks, clear_board } => {
            // UNCHANGED from S2 — keep this body verbatim (rename enclosing fn only).
            payload.push(CMD_APPLY_HOST_CONTEXT);
            payload.push(layer.unwrap_or(0xFF));
            payload.push(if *clear_board { 0x01 } else { 0x00 });
            payload.push(callbacks.len().min(255) as u8);
            payload.extend_from_slice(callbacks);
        }
        RunCommand::SendMessage(_) | RunCommand::ListDevices => unreachable!("handled above"),
    }
    payload.push(ETX_TERMINATOR_BYTE);
    payload
}

// (Alternative: keep the original early-return match shape — both are correct.
//  The key invariants are: SendMessage gets NO 0xF0 and exactly one ETX;
//  typed arms get 0xF0 + cmd + args + one ETX; ListDevices empty.)

// PATTERN — reconciled build_payload in lib.rs (thin verbose wrapper):
fn build_payload(command: &RunCommand, verbose: bool) -> Vec<u8> {
    let data = core::build_command_data(command);   // single source of truth
    if verbose {
        if let RunCommand::SendMessage(_) = command {
            println!("Message length: {} bytes (including ETX terminator)", data.len());
        }
    }
    data
}
```

### Integration Points

```yaml
CONSTANTS (src/core.rs):
  - add: "pub(crate) const ETX_TERMINATOR_BYTE: u8 = 0x03;" after REPLY_READ_TIMEOUT_MS
  - unchanged: CMD_DISCRIMINATOR, CMD_QUERY_INFO, CMD_QUERY_CALLBACK, CMD_SET_OS,
    CMD_APPLY_HOST_CONTEXT, RESPONSE_MARKER, REPLY_READ_TIMEOUT_MS (already consumed; no allow(dead_code) changes)

FUNCTIONS (src/core.rs):
  - rename: build_typed_payload -> build_command_data (+ SendMessage/ListDevices arms)

CALLER (src/lib.rs):
  - simplify: build_payload delegates all arms to core::build_command_data
  - update comments/doc-strings referencing build_typed_payload

NO CHANGES TO: run() dispatch logic, send_raw_report, burst_to_one, parse_reply,
  RunCommand/HostOs/CommandResponse enums, error.rs, CLI, Cargo.toml.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# After every task — fix before proceeding.
cargo build                         # compiles; ZERO warnings (no dead_code)
cargo clippy --all-targets -- -D warnings   # zero warnings
cargo fmt --check                   # formatting clean (run `cargo fmt` if not)

# Expected: clean build, zero warnings. If dead_code fires on build_command_data,
#   you accidentally removed its caller — check build_payload still calls it.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full lib suite — the authoritative gate.
cargo test --lib
# Expected: >= 64 passed, 0 failed (was 63; net +1..+2 from test split + ETX const test).

# Targeted: just the builder tests.
cargo test --lib build_command_data
# Expected: 10 build_command_data_* tests pass (incl. new send_message + list_devices_empty).

# Targeted: SendMessage now builds a payload (the headline behavior change).
cargo test --lib build_command_data_send_message
# Expected: pass — payload == "App\x1DTitle" bytes + 0x03, NO 0xF0.
```

### Level 3: Regression — typed paths unchanged on the wire

```bash
# Prove the reconciliation did NOT change typed payloads (byte-for-byte).
cargo test --lib build_command_data_query_info
cargo test --lib build_command_data_query_callback
cargo test --lib build_command_data_set_os
cargo test --lib build_command_data_apply_host_context
# Expected: all pass with identical byte assertions as before the rename
#   (these tests' bodies are UNCHANGED — only the fn name + call sites changed).

# Prove run() still dispatches every typed arm to send_raw_report.
cargo test --lib test_run_query_info_dispatches_to_send
cargo test --lib test_run_set_os_dispatches_to_send
# Expected: pass (DeviceNotFound with bogus VID/PID — dispatch wiring intact).
```

### Level 4: No stale references (one-pass cleanliness)

```bash
# MUST return nothing — the old name is fully eradicated.
rg -n "build_typed_payload" src/ && echo "FAIL: stale reference" || echo "OK: no stale refs"

# Confirm new name is wired into both files.
rg -n "build_command_data" src/core.rs src/lib.rs
# Expected: definition + tests in core.rs; delegation in lib.rs::build_payload.
```

## Final Validation Checklist

### Technical Validation

- [ ] `cargo build` — zero warnings
- [ ] `cargo clippy --all-targets -- -D warnings` — zero warnings
- [ ] `cargo fmt --check` — clean
- [ ] `cargo test --lib` — >= 64 passed, 0 failed
- [ ] `rg -n "build_typed_payload" src/` — empty (old name eradicated)

### Feature Validation

- [ ] `build_command_data(SendMessage("App\x1DTitle"))` == `b"App\x1DTitle" + [0x03]` (no 0xF0)
- [ ] `build_command_data(QueryInfo)` == `[0xF0, 0x01, 0x03]` (unchanged bytes)
- [ ] `build_command_data(QueryCallback(0))` == `[0xF0, 0x02, 0x00, 0x03]` (unchanged)
- [ ] `build_command_data(QueryCallback(255))` == `[0xF0, 0x02, 0xFF, 0x03]` (unchanged)
- [ ] `build_command_data(SetOs(HostOs::Linux))` == `[0xF0, 0x03, 0x01, 0x03]` (unchanged; all 5 HostOs variants covered by existing parametric test)
- [ ] `build_command_data(ApplyHostContext{..})` == `[0xF0, 0x05, layer, flags, count.clamp(255), ids…]` — **unchanged from S2** (NOT todo!())
- [ ] `build_command_data(ListDevices)` == empty `Vec`
- [ ] `ETX_TERMINATOR_BYTE == 0x03`
- [ ] `lib.rs::build_payload` delegates every arm to `core::build_command_data`; SendMessage verbose length print still fires when `verbose`

### Code Quality Validation

- [ ] Single source of truth for wire payloads (`build_command_data`) — no inline ETX framing left in `lib.rs`
- [ ] `ApplyHostContext` arm body is byte-identical to the committed S2 implementation (only the enclosing fn renamed)
- [ ] No `#[allow(dead_code)]` added or removed on the command constants
- [ ] Doc-comments updated: no remaining claim that "ETX is appended in build_payload, not by the builder" (that was the divergence)

### Documentation & Deployment

- [ ] No new public API surface (function stays `pub(crate)`)
- [ ] No user-facing behavior change — identical bytes on the wire for every command

---

## Anti-Patterns to Avoid

- ❌ Do NOT create a SECOND builder function alongside `build_typed_payload`. Rename/extend the EXISTING one.
- ❌ Do NOT revert `ApplyHostContext` to `todo!()`. S2 is Complete; its implementation stays.
- ❌ Do NOT add `#[allow(dead_code)]` to `build_command_data` — it has a live caller (`build_payload`).
- ❌ Do NOT push the `0xF0` discriminator into a `SendMessage` payload — that is the legacy string path (raw bytes + ETX only).
- ❌ Do NOT double-push ETX for `SendMessage` (early-return before the typed terminator).
- ❌ Do NOT `git checkout`/revert anything — the committed state is the start line.
- ❌ Do NOT touch `run()`, `send_raw_report`, `burst_to_one`, `parse_reply`, the enums, or `Cargo.toml`.

---

## Confidence Score: 9/10

This is a mechanical, fully-test-covered rename + one-arm-relocation + one-const
refactor of committed working code. The only residual risk is a copy-paste slip
in the test-rename sweep, which the `rg "build_typed_payload"` cleanliness gate
(Level 4) catches deterministically. The headline behavior (SendMessage now
framed by the builder, matching the architecture doc) is a single small,
well-specified arm.