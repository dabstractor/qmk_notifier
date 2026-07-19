# PRP — P1.M2.T1.S2: APPLY_HOST_CONTEXT payload in `build_typed_payload`

> ⚠️ **READ THIS BANNER FIRST — the codebase diverged from the item's premise.**
>
> The item description (and the S1 PRP, and `architecture/transport_evolution.md`)
> all assume a function named **`build_command_data`** exists in `src/core.rs`
> with a **`todo!()`** arm for `ApplyHostContext`, and that S2's job is to
> *replace the `todo!()`*. **That is not the current state of the code.** The
> implemented function is **`build_typed_payload`** (typed-only scope; returns an
> empty `Vec` for `SendMessage`/`ListDevices`), and its **`ApplyHostContext` arm
> is already fully implemented** — there is no `todo!()`.
>
> This PRP reconciles the item's **byte-layout contract** (which is correct and
> authoritative) with the **real codebase** (which already emits those bytes).
> The actual remaining work is small but real: **one defensive count-clamp fix**
> + **two missing tests**. Do not "re-implement" the arm from scratch and do not
> rename the function — see *Scope Boundaries*.

---

## Goal

**Feature Goal**: Make the `ApplyHostContext` arm of `build_typed_payload`
**fully conform to the P1.M2.T1.S2 wire contract** — specifically the
**defensive `count`-byte clamp at 255** the item mandates, plus close the two
gaps in the item's required test matrix (the `[1,5,10]` representative case and
the `>255` clamp edge case). The byte layout `[0xF0][0x05][layer][flags][count][id…][0x03]`
is already produced correctly by the existing arm; the only behavioral defect is
that `callbacks.len() as u8` **truncates** (256 → 0 = catastrophic parse drift)
instead of **clamping** (256 → 255) per the contract.

**Deliverable**: A **one-line edit** to the `ApplyHostContext` arm of
`build_typed_payload` in **`src/core.rs`** (`callbacks.len() as u8` →
`callbacks.len().min(255) as u8`, with the comment updated to explain the
clamp), plus **two new `#[test]` functions** appended to core.rs's existing
`#[cfg(test)] mod tests` block. `src/core.rs` is the only file modified —
lib.rs, error.rs, main.rs, Cargo.toml are untouched.

**Success Definition**: `cargo build` compiles with **zero warnings**;
`cargo clippy --lib` → zero warnings; `cargo fmt --check` → exit 0;
`cargo test --lib` → **39 passed, 0 failed** (baseline 37 + 2 new); the
`ApplyHostContext` arm emits the exact bytes mandated by
`firmware_wire_contract.md` §APPLY_HOST_CONTEXT request AND clamps `count` to
255 for `callbacks.len() > 255` (no truncation); no file other than `src/core.rs`
is modified.

## User Persona (if applicable)

**Target User**: The v0.3.0 transport dispatch path (`run()`, P1.M3.T3.S1) and,
transitively, the downstream `qmkonnect` desktop daemon that constructs
`RunCommand::ApplyHostContext { layer, callbacks, clear_board }` from its host
layer/callback rules. Today nothing calls `build_typed_payload` from `run()`
yet (the typed-dispatch arms are still `todo!()` in `run()`); the function is
landed and tested so P1.M3.T3 wires `run()` against a known-good builder.

**Use Case**: `qmkonnect` resolves the host's desired layer + enabled-callback
set, constructs `RunCommand::ApplyHostContext`, hands it to `run()` which calls
`build_typed_payload` to get the `[0xF0,0x05,…]` payload `Vec<u8>`, sends it via
`send_raw_report`/`burst_to_one`, then `parse_reply`s the `[0x51][0x05][ack]`
response. The builder is the single source of truth for "what bytes go on the
wire for APPLY_HOST_CONTEXT."

**User Journey**: `ApplyHostContext { layer: Some(224), callbacks: vec![1,5,10], clear_board: false }`
→ `build_typed_payload` emits `[0xF0, 0x05, 224, 0x00, 3, 1, 5, 10, 0x03]` →
`burst_to_one` prepends `[0x00][0x81][0x9F]` and chunk-sends → firmware applies
layer 224 + enables callbacks {1,5,10} → `[0x51][0x05][ack=1]` reply.

**Pain Points Addressed**: (1) The current `as u8` truncation is a latent
land-mine — if a future caller ever passes >255 callbacks (the transport is
uncapped per PRD §10.1), the firmware would be told "zero callbacks" while N id
bytes follow, causing silent parse drift. The clamp makes that impossible. (2)
The item's required edge-case test (`>255 ⇒ 255`) documents and pins the clamp
so a future refactor can't silently regress it.

## Why

- **PRD §10.1 (Framing) + `firmware_wire_contract.md` §APPLY_HOST_CONTEXT request**
  define the layout `[0xF0][0x05][layer][flags][count][id…]` — the existing arm
  already emits this correctly. The item's additional mandate is the
  **defensive `count` clamp** (`.min(255)`), which the current `as u8` cast does
  NOT satisfy.
- **The truncation bug is real and empirically proven** (see *Known Gotchas*):
  `callbacks.len() == 256` → count byte `0` (not 255). The firmware would then
  read `count=0` and mis-parse the trailing id bytes. `.min(255)` guarantees the
  count byte never lies *below* the real intent.
- **Dependency-chain integrity**: `build_typed_payload` is the framing entry
  point that P1.M3.T3.S1 wires into `run()`. Landing the clamp + full test
  matrix now means P1.M3.T3 wires against a contract-complete, exhaustively-
  tested builder instead of discovering the truncation during integration.
- **The `ApplyHostContext` arm is the most complex** of the typed arms (variable-
  length, possibly multi-report, three sub-fields). It deserves the most thorough
  test coverage; S2 closes the two remaining gaps in that coverage.
- **Additive + minimal**: a one-line behavior fix and two tests. No new types,
  no new constants, no new deps, no public-API change (`pub(crate)`), no new
  caller. Cannot break `run()` (which still `todo!()`s the typed arms) or any
  existing test (all existing `ApplyHostContext` tests use ≤40 callbacks, well
  under 255).

## What

### 0. The divergence reconciliation (CONTEXT — no code action, but understand it)

The implemented function is `build_typed_payload` (NOT `build_command_data`).
Its `ApplyHostContext` arm currently reads (verbatim from `src/core.rs`):

```rust
RunCommand::ApplyHostContext {
    layer,
    callbacks,
    clear_board,
} => {
    payload.push(CMD_APPLY_HOST_CONTEXT);
    // layer: Some(n) ⇒ host-layer number (≥224 by convention);
    // None ⇒ 0xFF (clear host layer). See firmware_wire_contract.md.
    payload.push(layer.unwrap_or(0xFF));
    // flags: bit 0 = clear_board (firmware clears board layer/command
    // before applying host context). No other bits defined yet.
    payload.push(if *clear_board { 0x01 } else { 0x00 });
    // count: u8. Host invariant — callbacks.len() ≤ 255 (the firmware
    // callback registry is itself u8-bounded, so unreachable in
    // practice). `as u8` truncates if ever violated; validate upstream.
    payload.push(callbacks.len() as u8);          // <-- THE BUG: truncates
    payload.extend_from_slice(callbacks);
}
```

The byte layout is already correct (`[0xF0,0x05,layer,flags,count,ids…]` — the
discriminator is pushed before the `match`, the ETX `0x03` is pushed after the
`match`). The ONLY defect is the `count`-byte truncation. Everything else
(layer `unwrap_or(0xFF)`, flags `clear_board ? 0x01 : 0x00`, `extend_from_slice`)
already matches the item contract.

### 1. The one-line fix (the `count` clamp)

Replace the `count` push line + its comment in the `ApplyHostContext` arm of
`build_typed_payload`. **Exact `oldText` → `newText`** (see *Implementation
Tasks* Task 1 for the precise edit):

```rust
// BEFORE:
            // count: u8. Host invariant — callbacks.len() ≤ 255 (the firmware
            // callback registry is itself u8-bounded, so unreachable in
            // practice). `as u8` truncates if ever violated; validate upstream.
            payload.push(callbacks.len() as u8);

// AFTER:
            // count: u8. Defensive CLAMP at 255 (P1.M2.T1.S2 contract; PRD §10.1
            // says the transport is uncapped while the firmware registry is
            // u8-bounded, HOST_CALLBACK_MAX=32). `as u8` alone WRAPS — e.g.
            // len==256 ⇒ count 0, telling the firmware "zero callbacks" while
            // 256 id bytes follow ⇒ parse drift. `.min(255)` guarantees the
            // count byte never lies below the real intent (255 == u8::MAX).
            payload.push(callbacks.len().min(255) as u8);
```

> **Do NOT** change any other line in the arm. The layer, flags, and
> `extend_from_slice(callbacks)` lines are already contract-correct.

### 2. The two new unit tests (append at END of core.rs's `mod tests`)

Append these after the existing `build_typed_payload_multi_report_chunking` test
(and before the `typed_command_constants_match_firmware_contract` test, or simply
at the very end of the `mod tests` block — placement among tests is not
load-bearing). They use core.rs's descriptive snake_case style (**no** `test_`
prefix — matches `batches_for_*`, `build_typed_payload_*`):

```rust
    #[test]
    fn build_typed_payload_apply_host_context_representative_ids() {
        // Item-contract representative case: callbacks=[1,5,10] ⇒ count=3 then
        // the three id bytes verbatim. Full byte sequence (firmware_wire_contract.md
        // §APPLY_HOST_CONTEXT request): [0xF0,0x05,layer,flags,count=3,1,5,10,0x03].
        let payload = build_typed_payload(&RunCommand::ApplyHostContext {
            layer: Some(224), // HOST_LAYER_BASE
            callbacks: vec![1, 5, 10],
            clear_board: false,
        });
        assert_eq!(payload, vec![0xF0, 0x05, 224, 0x00, 3, 1, 5, 10, 0x03]);
    }

    #[test]
    fn build_typed_payload_apply_host_context_clamps_count_at_255() {
        // Edge case (P1.M2.T1.S2 contract): callbacks.len() > 255 must CLAMP the
        // count byte to 255 — NOT truncate. `256u8 as u8 == 0`, which would tell
        // the firmware "zero callbacks follow" while 256 id bytes + ETX actually
        // follow ⇒ catastrophic parse drift. `.min(255)` prevents that.
        // In practice callbacks.len() <= HOST_CALLBACK_MAX (32), so this path is
        // unreachable on the happy path; the clamp is the defensive contract and
        // this test pins it against regression.
        let callbacks: Vec<u8> = (0..256u8).collect(); // 256 elements
        let payload = build_typed_payload(&RunCommand::ApplyHostContext {
            layer: Some(224),
            callbacks,
            clear_board: false,
        });
        // Header: [0xF0, 0x05, layer=224, flags=0x00], then COUNT MUST BE 255.
        assert_eq!(&payload[..5], &[0xF0, 0x05, 224, 0x00, 255]);
        // All 256 ids copied verbatim (item reference: extend_from_slice(callbacks)
        // copies the full slice; only the count byte is clamped).
        assert_eq!(payload.len(), 5 + 256 + 1, "header(5) + ids(256) + ETX(1)");
        assert_eq!(*payload.last().unwrap(), 0x03, "ETX must remain the final byte");
        // PROOF OF FIX: with the old `callbacks.len() as u8`, the count byte here
        // would have been 0 (256 as u8 == 0) and this assertion would fail.
    }
```

> **Do NOT** remove or alter the existing `ApplyHostContext` tests
> (`…_set_layer`, `…_clear_layer`, `…_multi_report_chunking`). They remain valid
> and cover the layer/flags/full-sequence requirements; the two new tests only
> ADD the `[1,5,10]` representative case and the `>255` clamp edge case.

### Success Criteria

- [ ] The `ApplyHostContext` arm's `count` push is `callbacks.len().min(255) as u8`
      (clamp), with an updated comment explaining why (wraps otherwise).
- [ ] No other line in the `ApplyHostContext` arm changed (layer/flags/extend
      unchanged).
- [ ] `build_typed_payload_apply_host_context_representative_ids` test exists and
      asserts `[0xF0, 0x05, 224, 0x00, 3, 1, 5, 10, 0x03]`.
- [ ] `build_typed_payload_apply_host_context_clamps_count_at_255` test exists,
      passes (count byte == 255 for 256 callbacks), and would have FAILED before
      the fix (count byte would have been 0).
- [ ] All existing `ApplyHostContext` tests still pass unchanged.
- [ ] `cargo build` → zero warnings; `cargo clippy --lib` → zero warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → **39 passed, 0 failed**.
- [ ] No file other than `src/core.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact one-line edit
> (oldText → newText) is given verbatim with its surrounding context anchor;
> both new tests are given verbatim with the exact expected byte vectors; the
> divergence from the item's premise is explained up front so the implementer
> doesn't go hunting for a nonexistent `build_command_data`/`todo!()`; the
> truncation-vs-clamp behavior is empirically proven with a table; the baseline
> test count (37) and all build/clippy/fmt/test commands are verified working.
> The implementer needs no QMK firmware source — the wire contract is fully
> canonicalized in `firmware_wire_contract.md`.

### Documentation & References

```yaml
# MUST READ — the file being edited (the ApplyHostContext arm + the 2 new tests)
- file: src/core.rs
  why: "Holds build_typed_payload (the function whose ApplyHostContext arm is
        edited — search for `RunCommand::ApplyHostContext`). Holds the
        #[cfg(test)] mod tests block whose END is the test-append anchor (after
        build_typed_payload_multi_report_chunking)."
  pattern: "build_typed_payload pushes CMD_DISCRIMINATOR BEFORE the match and the
            0x03 ETX AFTER the match — so each arm only pushes [cmd, args…].
            Tests are descriptive snake_case with NO test_ prefix and use
            `use super::*` (which brings RunCommand, build_typed_payload, and the
            CMD_* constants into scope)."
  gotcha: "The function is named build_typed_payload, NOT build_command_data
           (the item/S1-PRP/architecture-doc name). Do NOT rename it. Do NOT
           create a separate build_command_data. The SendMessage/ListDevices arms
           return empty Vec BY DESIGN — leave them alone."

# MUST READ — the canonical wire contract (the bytes this arm emits)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "§APPLY_HOST_CONTEXT request gives the exact layout
        [0xF0][0x05][layer][flags][count][id0][id1]… with the field table:
        layer = host-layer number or 0xFF (clear); flags bit 0 = clear_board;
        count = number of callback IDs; id… = full desired enabled set. §Constants
        pins HOST_CALLBACK_MAX=32 and HOST_LAYER_BASE=224. This is the single
        source of truth the tests assert against."
  section: "APPLY_HOST_CONTEXT request", "Command Table", "Constants"
  critical: "The firmware caps callbacks at HOST_CALLBACK_MAX=32, but the
             transport is uncapped (PRD §10.1). The count byte is u8 (0..255).
             The item mandates a defensive clamp at 255 for >255 callbacks (not
             truncation). 0xFF (255) is ALSO the layer-clear sentinel — do not
             confuse the layer byte's 0xFF with the count byte's 255."

# REFERENCE — the item's exact reference implementation (the contract source)
- docfile: "(this work item's description — the APPLY_HOST_CONTEXT arm reference)"
  why: "The item gives the canonical arm:
        data.push(callbacks.len().min(255) as u8);  // count, defensive clamp
        data.extend_from_slice(callbacks);           // id0, id1, ...
        and the 7 required test cases. This PRP maps each onto the real codebase."

# REFERENCE — the S1 PRP (explains the build_command_data vs build_typed_payload divergence)
- docfile: plan/001_b92a9b2b603f/P1M2T1S1/PRP.md
  why: "S1's PRP specified a function named build_command_data handling ALL
        RunCommand variants (incl. SendMessage ETX) with ApplyHostContext as a
        todo!(). The ACTUAL implemented function is build_typed_payload
        (typed-only; empty Vec for SendMessage/ListDevices; ApplyHostContext
        already implemented). S2 works within the ACTUAL design, not the PRP's
        plan. Reading this PRP explains WHY the names differ."

# REFERENCE — the types this function consumes (defined at crate root in lib.rs)
- file: src/lib.rs
  why: "Defines RunCommand::ApplyHostContext { layer: Option<u8>, callbacks:
        Vec<u8>, clear_board: bool } (the variant this arm matches on). DO NOT
        EDIT lib.rs in this task."
  pattern: "Matching `RunCommand::ApplyHostContext { layer, callbacks,
            clear_board }` on a `&RunCommand` binds layer: &Option<u8>,
            callbacks: &Vec<u8>, clear_board: &bool (match ergonomics).
            layer.unwrap_or(0xFF) works on &Option<u8>; *clear_board derefs to
            bool; callbacks.len() and extend_from_slice(callbacks) work on
            &Vec<u8>."

# REFERENCE — PRD framing + invariants
- file: PRD.md
  why: "§10.1 (Framing) defines the typed-command wire layout; §14 invariant 5
        (typed commands reuse the same framing + 0xF0 discriminator) bounds
        scope; §10.1 note that the callback-id list is uncapped motivates the
        defensive clamp."
  section: "10.1 Framing", "14. Key Invariants (5, 7)"

# REFERENCE — empirical evidence (truncation-vs-clamp table, test-count math)
- docfile: plan/001_b92a9b2b603f/P1M2T1S2/research/notes.md
  why: "Documents the divergence finding, the empirically-proven
        truncation-vs-clamp table (256→0 vs 256→255), the full test-coverage
        matrix, and the 37→39 test-count math."
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
    ├── core.rs         # <-- FILE TO EDIT (ApplyHostContext arm count-clamp + 2 tests)
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # RunCommand/HostOs/CommandResponse/run() — DO NOT TOUCH
```

### Desired Codebase tree with files to be modified

```bash
src/
└── core.rs   # MODIFIED ONLY:
              #   1. ApplyHostContext arm: `callbacks.len() as u8`
              #      → `callbacks.len().min(255) as u8` (+ comment update)
              #   2. + 2 #[test] functions appended to #[cfg(test)] mod tests:
              #      build_typed_payload_apply_host_context_representative_ids
              #      build_typed_payload_apply_host_context_clamps_count_at_255
# (lib.rs, error.rs, main.rs, Cargo.toml unchanged)
```

> No new files. One file modified (`src/core.rs`). No new dependencies.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (DIVERGENCE): the item + S1 PRP + transport_evolution.md all name the
//   function `build_command_data`. The IMPLEMENTED function is `build_typed_payload`
//   (typed-only scope; empty Vec for SendMessage/ListDevices; ApplyHostContext
//   already implemented — NO todo!()). This PRP works within the ACTUAL design.
//   Do NOT: create build_command_data, rename build_typed_payload, or hunt for a
//   todo!(). The ApplyHostContext arm EXISTS; S2 fixes its count-byte clamp + adds
//   2 tests. (VERIFIED: `grep -n "build_command_data\|build_typed_payload" src/core.rs`.)

// CRITICAL (THE BUG — empirically proven): `callbacks.len() as u8` TRUNCATES for
//   len > 255. Proof (throwaway rustc):
//     len=255 → 255 | len=256 → 0 | len=257 → 1 | len=300 → 44 | len=511 → 255
//   len==256 ⇒ count byte 0 ⇒ firmware reads "zero callbacks" while 256 id bytes
//   + ETX follow ⇒ parse drift. The FIX is `.min(255) as u8` (clamp ⇒ always 255
//   for len >= 255). Reachability: in practice len <= HOST_CALLBACK_MAX (32), so
//   the happy path is unaffected; the clamp is the defensive contract the item
//   mandates and the >255 test pins.

// CRITICAL (clamp semantics — documented, NOT "fixed" further): the item reference
//   clamps the COUNT byte to 255 but still copies ALL callback bytes
//   (`extend_from_slice(callbacks)`). For >255 callbacks this makes count (255)
//   inconsistent with the actual byte count. This is FAITHFUL to the item contract
//   (count never wraps to a dangerously small value) and is unreachable in practice.
//   S2 does NOT truncate the copied bytes — that would deviate from the contract.

// NOTE (0xFF overload): the byte 0xFF (255) is used in TWO places in this payload —
//   layer=None ⇒ 0xFF (clear-host-layer sentinel), AND count clamp ⇒ 255. They are
//   at different offsets (layer is payload[2], count is payload[4]) so there is no
//   ambiguity on the wire, but be careful reading/writing tests not to conflate them.

// NOTE (header is NOT here): the magic header 0x81 0x9F is written by burst_to_one
//   into request_data[1]/[2]. build_typed_payload's payload starts at buffer[3]
//   with 0xF0 (pushed before the match). Do NOT prepend 0x81 0x9F. The ETX 0x03 is
//   pushed AFTER the match (one universal push for all typed arms).

// NOTE (no live caller): build_typed_payload has NO live caller yet (run()'s typed
//   arms are still todo!(); migration is P1.M3.T3.S1). It carries
//   #[allow(dead_code)] — do NOT remove it (removing would warn dead_code). The
//   function is exercised only by tests.

// NOTE: `callbacks.len().min(255) as u8` is clippy-clean under DEFAULT lints
//   (`clippy::as_conversions` is pedantic-only; the codebase already uses
//   `*os as u8`). No clippy.toml exists → default config. Run `cargo clippy --lib`
//   to confirm zero warnings after the edit.

// NOTE: existing ApplyHostContext tests use <=40 callbacks (set_layer=3,
//   clear_layer=0, multi_report_chunking=40). All are < 255, so the clamp is a
//   no-op for them — they remain valid and unaffected by the fix. Do NOT edit them.

// NOTE: no rustfmt.toml exists — default config. Run `cargo fmt` after editing;
//   `cargo fmt --check` is the CI gate. The proposed code is already rustfmt-clean.
```

## Implementation Blueprint

### Data models and structure

No new types, structs, enums, constants, or functions. This subtask is **one
behavioral one-liner** (`as u8` → `.min(255) as u8`) plus a comment update, and
**two new `#[test]` functions**. No state, no I/O, no globals, no new deps.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: EDIT src/core.rs — the count clamp in the ApplyHostContext arm
  - FIND: the `RunCommand::ApplyHostContext { layer, callbacks, clear_board }`
          arm inside `build_typed_payload` (search `callbacks.len() as u8`).
  - REPLACE (exact oldText → newText):
      oldText (3 lines: 2-line comment + the push):
        "            // count: u8. Host invariant — callbacks.len() ≤ 255 (the firmware\n            // callback registry is itself u8-bounded, so unreachable in\n            // practice). `as u8` truncates if ever violated; validate upstream.\n            payload.push(callbacks.len() as u8);"
      newText:
        "            // count: u8. Defensive CLAMP at 255 (P1.M2.T1.S2 contract; PRD §10.1\n            // says the transport is uncapped while the firmware registry is\n            // u8-bounded, HOST_CALLBACK_MAX=32). `as u8` alone WRAPS — e.g.\n            // len==256 ⇒ count 0, telling the firmware \"zero callbacks\" while\n            // 256 id bytes follow ⇒ parse drift. `.min(255)` guarantees the\n            // count byte never lies below the real intent (255 == u8::MAX).\n            payload.push(callbacks.len().min(255) as u8);"
  - DO NOT: touch the layer (`layer.unwrap_or(0xFF)`), flags
            (`if *clear_board { 0x01 } else { 0x00 }`), or
            `extend_from_slice(callbacks)` lines. Do NOT touch any other arm.
  - DO NOT: remove/relocate the `payload.push(0x03)` ETX (it is AFTER the match,
            shared by all typed arms — out of scope).

Task 2: EDIT src/core.rs — append the 2 new unit tests
  - APPEND: the 2 #[test] functions (verbatim from the "What" §2) at the END of
          #[cfg(test)] mod tests (after build_typed_payload_multi_report_chunking,
          before the module closing }). Placement among tests is not load-bearing.
  - PATTERN: use the already-present `use super::*;` and `use crate::{HostOs,
          RunCommand};` (brings build_typed_payload + CMD_* constants + RunCommand
          into scope). Descriptive snake_case, NO test_ prefix.
  - COVERAGE:
      representative_ids: callbacks=[1,5,10], layer=Some(224), clear_board=false
        → asserts full [0xF0,0x05,224,0x00,3,1,5,10,0x03].
      clamps_count_at_255: callbacks=0..256 (256 elems) → asserts count byte==255
        (the clamp), payload.len()==262, ETX last. Would FAIL pre-fix (count==0).
  - DO NOT: edit or remove existing ApplyHostContext tests.

Task 3: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --lib`, then
          `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0;
          test result "39 passed; 0 failed" (37 baseline + 2 new).
  - IF a test panics on the clamp test with "assertion failed: left != right"
          showing count byte 0: the edit in Task 1 didn't apply — confirm the
          line now reads `callbacks.len().min(255) as u8`.
  - IF "function `build_typed_payload` is never used" (dead_code) fires: you
          accidentally removed its `#[allow(dead_code)]` — restore it (it has no
          live caller until P1.M3.T3).
```

### Implementation Patterns & Key Details

```rust
// === THE ONE-LINE FIX (truncation → clamp) ===
//   The ApplyHostContext arm builds [0xF0, 0x05, layer, flags, count, ids…, 0x03].
//   Everything except `count` is already correct. The count byte was
//   `callbacks.len() as u8` which WRAPS for len > 255 (256 → 0). The fix is
//   `.min(255) as u8` which CLAMPS (256 → 255). The byte layout is otherwise
//   unchanged:
//
//     payload.push(CMD_APPLY_HOST_CONTEXT);
//     payload.push(layer.unwrap_or(0xFF));              // None ⇒ 0xFF clear
//     payload.push(if *clear_board { 0x01 } else { 0x00 }); // flags bit 0
//     payload.push(callbacks.len().min(255) as u8);     // <-- THE FIX
//     payload.extend_from_slice(callbacks);             // all ids copied
//   ... then after the match: payload.push(0x03);       // ETX (shared)


// === WHY .min(255) AND NOT u8::try_from(...).unwrap_or(u8::MAX) ===
//   Both are correct and clippy-clean. `.min(255) as u8` matches the item's
//   reference EXACTLY (`callbacks.len().min(255) as u8`), so the test assertions
//   and the implementation agree byte-for-byte. The idiomatic alternative
//   (`u8::try_from(callbacks.len()).unwrap_or(u8::MAX)`) avoids the `as` cast but
//   deviates from the contract's reference; prefer the literal `.min(255)` for
//   contract fidelity.


// === WHY THE CLAMP TEST COPIES ALL 256 IDS (not 255) ===
//   The item reference is `extend_from_slice(callbacks)` (full slice) + a clamped
//   count byte. So for 256 callbacks the payload carries all 256 id bytes but
//   reports count=255. This is the documented (slightly inconsistent but
//   contract-faithful and unreachable-in-practice) semantics. The test asserts
//   payload.len() == 5 + 256 + 1 to pin this exact behavior.


// === WHY EXISTING TESTS DON'T BREAK ===
//   set_layer (len 3), clear_layer (len 0), multi_report_chunking (len 40): all
//   < 255, so `.min(255)` is a no-op for them (3.min(255)=3, 0.min(255)=0,
//   40.min(255)=40). Their exact-eq assertions are unaffected.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - edit:   "1 line in the ApplyHostContext arm of build_typed_payload
             (`callbacks.len() as u8` → `callbacks.len().min(255) as u8`)
             + the 2-line comment above it"
  - add:    "2 #[test] functions at END of #[cfg(test)] mod tests"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps.

PUBLIC API SURFACE:
  - adds:    "(nothing public — build_typed_payload is pub(crate); the fix is a
              private internal behavior change)"
  - unchanged: "all lib.rs public types (HostOs, RunCommand, CommandResponse,
                RunParameters), parse_cli_args, run signature/body, all core::
                re-exports, all QmkError variants/Display"

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M3.T3.S1 (run dispatch): "migrates run()'s ApplyHostContext todo!() arm to
        call build_typed_payload → send_raw_report → parse_reply. At that point
        build_typed_payload loses its #[allow(dead_code)] (gains a live caller)."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified, and ONLY the ApplyHostContext arm's count push
    (+ comment) and 2 appended tests. Do NOT:
    * create or rename build_command_data (the item's name; the real function is
      build_typed_payload — work within it).
    * touch the SendMessage/ListDevices arms (empty Vec by design).
    * introduce ETX_TERMINATOR_BYTE (S1 territory; out of scope).
    * wire build_typed_payload into run() (P1.M3.T3).
    * edit lib.rs, error.rs, main.rs, or Cargo.toml.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# IF "function `build_typed_payload` is never used": you removed its
#   #[allow(dead_code)] — restore it.

# Lint (default clippy — no clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors. `.min(255) as u8` is clippy-clean under defaults.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the 2 new tests in isolation first.
cargo test --lib build_typed_payload_apply_host_context -- --nocapture
# Expected: 4 passed (set_layer, clear_layer, representative_ids, clamps_count_at_255).

# Specifically confirm the clamp test passes (it would have FAILED pre-fix):
cargo test --lib build_typed_payload_apply_host_context_clamps_count_at_255 -- --nocapture
# Expected: 1 passed; count byte asserted == 255 (not 0).

# Full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 39 passed; 0 failed; 0 ignored; ..." (37 baseline + 2 new).

# Sanity: the existing ApplyHostContext tests still pass unchanged.
cargo test --lib build_typed_payload_apply_host_context_set_layer -- --nocapture
cargo test --lib build_typed_payload_apply_host_context_clear_layer -- --nocapture
cargo test --lib build_typed_payload_multi_report_chunking -- --nocapture
# Expected: each 1 passed.
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
build_typed_payload is a pure function with no HID I/O, no CLI surface, and no
runtime call site yet (run()'s ApplyHostContext arm is still todo!(); migration
is P1.M3.T3). There is no live-hardware or runtime path to exercise. The
exhaustive unit tests in Level 2 — which assert the exact bytes mandated by
firmware_wire_contract.md §APPLY_HOST_CONTEXT request, including the >255 clamp
edge case — ARE the end-to-end verification for this task.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the clamp is in place (not the truncating cast):
grep -n "callbacks.len()" src/core.rs
# Expected: a line containing "callbacks.len().min(255) as u8" inside the
# ApplyHostContext arm, AND "callbacks.len()" inside extend_from_slice. The
# count line MUST show ".min(255)".

# Cross-check the emitted bytes against the canonical contract by eye:
grep -nE "0xF0|0x05|0xFF|HOST_CALLBACK_MAX|HOST_LAYER_BASE|APPLY_HOST_CONTEXT" \
  plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
# (layer=0xFF clear sentinel, cmd 0x05, HOST_CALLBACK_MAX=32, HOST_LAYER_BASE=224
#  — all should be consistent with the test assertions above.)

# Proof-of-fix sanity (optional, no code change): confirm 256 as u8 == 0 vs clamp:
#   the clamps_count_at_255 test asserting count byte == 255 IS this proof — if it
#   passes, the clamp works; if it had been the old `as u8`, the assertion would
#   fail with left=0 right=255.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → zero warnings.
- [ ] Level 1 passed: `cargo clippy --lib` → zero warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 39 passed, 0 failed.
- [ ] The 2 new `build_typed_payload_apply_host_context_*` tests pass individually.
- [ ] The clamp test passes (count byte == 255 for 256 callbacks).

### Feature Validation

- [ ] The `ApplyHostContext` arm's count push is `callbacks.len().min(255) as u8`.
- [ ] The comment above the count push explains the wrap-vs-clamp rationale.
- [ ] No other line in the `ApplyHostContext` arm changed.
- [ ] `representative_ids` test asserts `[0xF0, 0x05, 224, 0x00, 3, 1, 5, 10, 0x03]`.
- [ ] `clamps_count_at_255` test asserts count byte == 255 (would fail pre-fix).
- [ ] All existing `ApplyHostContext` tests still pass unchanged.

### Code Quality Validation

- [ ] Follows core.rs conventions: descriptive snake_case tests with no `test_`
      prefix; `use super::*` in tests.
- [ ] Magic header `0x81 0x9F` is NOT in the payload (burst_to_one owns it).
- [ ] No file other than `src/core.rs` modified.
- [ ] Did NOT create/rename `build_command_data` (worked within `build_typed_payload`).

### Documentation & Deployment

- [ ] The count-clamp comment is self-documenting (cites P1.M2.T1.S2, PRD §10.1,
      HOST_CALLBACK_MAX, and the wrap-vs-clamp rationale).
- [ ] No new environment variables or config.
- [ ] No README/PRD/Cargo.toml/lib.rs change (contract DOCS = "none" — internal
      function, no user-facing surface).

---

## Anti-Patterns to Avoid

- ❌ Don't hunt for a `build_command_data` function or a `todo!()` arm — the
  function is `build_typed_payload` and the `ApplyHostContext` arm is already
  implemented. The work is the count-clamp fix + 2 tests, NOT a from-scratch
  implementation. (VERIFIED: `grep -n "build_command_data" src/core.rs` → no match;
  `grep -n "todo!" src/core.rs` → no match.)
- ❌ Don't rename `build_typed_payload` → `build_command_data` to "match the docs" —
  that's out of S2's scope, affects all `build_typed_payload_*` tests, and conflicts
  with S1's actual delivered output. The name divergence is a known, accepted
  implementation choice.
- ❌ Don't create a separate `build_command_data` function — it would duplicate
  `build_typed_payload` and create two sources of truth for the typed payload.
- ❌ Don't use `callbacks.len() as u8` (truncation) — it wraps for len > 255
  (256 → 0), causing catastrophic firmware parse drift. Use `.min(255) as u8`
  (clamp). (VERIFIED empirically: 256 as u8 == 0.)
- ❌ Don't truncate the copied callback bytes (`extend_from_slice(&callbacks[..255])`)
  to "match" the clamped count — the item reference copies the FULL slice and
  clamps only the count byte. Follow the contract; the inconsistency is documented
  and unreachable in practice.
- ❌ Don't touch the `layer.unwrap_or(0xFF)`, `if *clear_board { 0x01 } else { 0x00 }`,
  or `extend_from_slice(callbacks)` lines — they are already contract-correct.
- ❌ Don't remove `build_typed_payload`'s `#[allow(dead_code)]` — it has no live
  caller until P1.M3.T3 (run()'s typed arms are still `todo!()`).
- ❌ Don't edit or remove the existing `ApplyHostContext` tests (`…_set_layer`,
  `…_clear_layer`, `…_multi_report_chunking`) — they remain valid (all use ≤40
  callbacks, under 255, so the clamp is a no-op for them).
- ❌ Don't conflate the layer byte's `0xFF` (clear-host-layer sentinel, payload[2])
  with the count byte's `255` (clamp ceiling, payload[4]) — same value, different
  offsets and meanings.
- ❌ Don't prepend the `0x81 0x9F` magic header — `burst_to_one` writes it into
  buffer `[1]`/`[2]`. The payload here starts at buffer `[3]` with `0xF0`.
- ❌ Don't touch `lib.rs` (the `RunCommand::ApplyHostContext` variant and `run()`'s
  `todo!()` dispatch arm live there; both are out of S2's scope).
- ❌ Don't skip `cargo test --lib` because "it's a one-line change" — the clamp
  test is the proof-of-fix; running it is the verification.

---

**Confidence Score: 9/10** for one-pass implementation success. The deliverable
is a single, precisely-specified one-line behavioral fix (exact `oldText` →
`newText` given, including the updated comment) plus two verbatim tests with
exact expected byte vectors pinned to the canonical firmware wire contract. The
one risk that keeps it from 10/10 is the **codebase divergence**: an implementer
who skims the item description (which says "replace the todo!() in
`build_command_data`") without reading this PRP's banner may go hunting for a
nonexistent function/arm and stall. The banner + *Scope Boundaries* +
*Anti-Patterns* are written to make that impossible. The truncation-vs-clamp
behavior is **empirically proven** (throwaway `rustc`: 256 → 0 vs 256 → 255), the
baseline (37 tests) and all build/clippy/fmt/test commands are verified working
in this repo, and no existing test is affected by the clamp (all existing
`ApplyHostContext` tests use ≤40 callbacks). No file other than `src/core.rs` is
touched; no public API changes; no new deps.