# PRP — P1.M2.T2.S1: Implement `parse_reply` for typed `0x51` replies

> ⚠️ **READ THIS BANNER FIRST — it reconciles the S1/S2 scope split.**
>
> The item **title** is "parse_reply for typed 0x51 replies" and its successor
> (P1.M2.T2.S2) is "Add legacy/timeout/edge-case handling to parse_reply and
> finalize tests". A naïve read suggests S1 should implement ONLY the typed path
> and leave legacy/timeout to S2. **That split is impossible.** Rust requires the
> top-level `match response[0]` in `parse_reply` to be **exhaustive** — you cannot
> implement `parse_reply` without the `0`/`1`/`_` arms (the compiler rejects a
> non-exhaustive match). The item's **CONTRACT code** (authoritative) is the
> **complete** `parse_reply` including `empty→Timeout`, `0x51→typed`,
> `0/1→Legacy`, `_→Timeout`.
>
> **RECONCILED SCOPE (this PRP):**
> - **S1 (THIS task) implements the COMPLETE function body** — `parse_reply` +
>   `parse_typed_reply` + `parse_callback_name` — exactly as the item's contract
>   specifies (~40 lines, all defensive `.get()`). It is NOT a partial function.
> - **S1's TESTS are the EXACT 6 typed-path tests the item mandates** (all are
>   `0x51` happy paths; the item lists precisely 6).
> - **S2 (the NEXT task) ADDS the legacy/timeout/edge-case TESTS** — empty input,
>   unknown marker byte, legacy `0`/`1`, truncated Info/CallbackName replies,
>   unknown cmd echo — and "finalizes" the matrix. The function body is already
>   complete after S1; S2 only extends test coverage.
>
> This mirrors how `P1M2T1S2/PRP.md` reconciled its own title/contract divergence.

---

## Goal

**Feature Goal**: Add a pure, defensive, fully-tested reply parser to `src/core.rs`
that turns a single raw 32-byte IN report (`&[u8]`) into a typed
`crate::CommandResponse`, disambiguating the firmware's three reply shapes
(typed `0x51`, legacy match-bool `0`/`1`, and no/unknown reply → `Timeout`)
exactly per `firmware_wire_contract.md` §Reply Disambiguation and §Field
Definitions.

**Deliverable**: Three new functions in **`src/core.rs`** — `parse_reply`
(`pub(crate)`, the entry point), `parse_typed_reply` (private helper), and
`parse_callback_name` (private helper) — inserted immediately after
`build_typed_payload`; plus the **removal** of the now-redundant
`#[allow(dead_code)]` on `RESPONSE_MARKER`; plus **6 new `#[test]` functions**
appended to core.rs's `#[cfg(test)] mod tests` block (and one `use`-line edit to
bring `CommandResponse` into the test scope). **`src/core.rs` is the ONLY file
modified** — lib.rs, error.rs, main.rs, Cargo.toml are untouched.

**Success Definition**: `cargo build` → zero warnings; `cargo clippy --lib` →
zero warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **45 passed,
0 failed** (baseline 39 + 6 new); the 6 item-mandated typed-reply cases decode to
the exact `CommandResponse` variants; `parse_reply` is `#[allow(dead_code)]`
(no live caller until P1.M3.T3) with its private helpers NOT flagged; no file
other than `src/core.rs` is modified.

## User Persona (if applicable)

**Target User**: The v0.3.0 transport dispatch path (`run()`, P1.M3.T3.S1) and,
transitively, the downstream `qmkonnect` desktop daemon. Today nothing calls
`parse_reply` yet (`run()`'s typed arms are still `todo!()`); the function is
landed + tested so P1.M3.T3 can wire `run()` against a known-good parser.

**Use Case**: After `send_raw_report` returns the first captured IN report
(P1.M3.T2), `run()` hands those raw bytes to `core::parse_reply(&bytes)` and gets
back a `CommandResponse` — `Info` / `CallbackName` / `Ack` for a typed-capable
device, `Legacy { matched }` for a legacy device, or `Timeout` for an
offline/non-capable device. The caller then branches (e.g. a `Timeout`/`Legacy`
reply ⇒ stay in string-only mode per PRD §8).

**User Journey**: firmware receives `QUERY_INFO` → sends
`[0x51][0x01][proto_ver=2][feature_flags=0x03][callback_count=5][board_rules_present=1]`
→ `burst_to_one` captures the 32-byte report (P1.M3.T1) → `send_raw_report`
propagates it (P1.M3.T2) → `run()` calls `core::parse_reply(&reply)` →
`CommandResponse::Info { proto_ver: 2, feature_flags: 3, callback_count: 5,
board_rules_present: true }` → `qmkonnect` reads `feature_flags` to decide
whether the device supports `APPLY_HOST_CONTEXT`.

**Pain Points Addressed**: (1) There is currently **no** reply parser — the v0.2.x
drain loop (`burst_to_one`) discards every reply. Until `parse_reply` exists, the
typed-command handshake is impossible. (2) Firmware replies may be **truncated**
(shorter than the full 32 bytes), so naive `response[5]` indexing would panic and
wedge a live `run()`; defensive `.get(i).copied().unwrap_or(0)` makes that
impossible. (3) Legacy devices reply `0`/`1` (not `0x51`); conflating those with
typed replies would mis-decode a match-bool as garbage. The `response[0]`
disambiguation handles all three shapes in one function.

## Why

- **PRD §8 (Response Handling) + §10.2 (Reply parsing)** + the canonical
  `firmware_wire_contract.md` §Reply Disambiguation / §Field Definitions define
  exactly what bytes mean. This PRP transcribes those tables into tested Rust.
- **Dependency-chain integrity**: `parse_reply` is the reply-side counterpart of
  `build_typed_payload` (P1.M2.T1). P1.M3.T3 (`run()` dispatch) consumes BOTH —
  `build_typed_payload` to build the request, `send_raw_report` to send + capture,
  `parse_reply` to decode the reply. Landing a complete, exhaustively-tested
  parser now means P1.M3.T3 wires against a known-good decoder instead of
  discovering parse bugs during integration.
- **Pure + additive**: `parse_reply` has no I/O, no globals, no new deps, no
  public-API change (`pub(crate)`), and — until P1.M3.T3 — no live caller. It
  cannot break `run()` (whose typed arms remain `todo!()`) or any existing test.
- **Defensive by construction**: the firmware is NOT yet implemented
  (`findings_and_risks.md` F4 — typed commands will time out until the firmware
  ships §4.6). So every byte read must tolerate a truncated/garbage reply without
  panicking; the `.get().unwrap_or(0)` pattern guarantees that.

## What

### 0. The scope split (CONTEXT — understand before coding)
See the banner at the top. S1 lands the COMPLETE `parse_reply` body (required for
match-exhaustiveness) and the 6 typed-path tests. S2 adds the
legacy/timeout/edge-case tests. Do not "save" the legacy/timeout arms for S2 —
Rust won't let you compile `parse_reply` without them.

### 1. Insert the three functions (verbatim code in *Implementation Patterns*)
Insert `parse_reply` + `parse_typed_reply` + `parse_callback_name` in `src/core.rs`
**immediately after `build_typed_payload`'s closing brace and BEFORE the
`/// Match parameters a cached handle set was opened for.` doc comment above
`struct MatchKey`** (anchor by that doc comment — line numbers drift because
P1.M2.T1.S2 edits this file in parallel). `parse_reply` is `pub(crate)` and carries
`#[allow(dead_code)]` (no live caller until P1.M3.T3). The two helpers are private
`fn` and do NOT need their own `#[allow(dead_code)]` (empirically verified — see
*Known Gotchas*).

### 2. Remove the redundant `#[allow(dead_code)]` from `RESPONSE_MARKER`
Once `parse_reply` references `RESPONSE_MARKER`, the constant has a consumer.
Delete the `#[allow(dead_code)]` line directly above
`pub(crate) const RESPONSE_MARKER: u8 = 0x51;`. This mirrors how the 5 `CMD_*`
constants lost their allows when `build_typed_payload` consumed them (core.rs
lines 18-23 already document this rule: "a const referenced by an allow-dead fn's
body does NOT warn"). **Leave `REPLY_READ_TIMEOUT_MS`'s allow intact** — its
consumer (the reply reader) lands in P1.M3.T1, not here.

### 3. Edit the test-module `use` line + append the 6 tests
Change `use crate::{HostOs, RunCommand};` → `use crate::{CommandResponse, HostOs,
RunCommand};` inside core.rs's `#[cfg(test)] mod tests`. Then append the 6
`#[test]` functions (verbatim code in *Implementation Patterns*) at the **END of
the `mod tests` block** (after the current last test, which is
`build_typed_payload_apply_host_context_clamps_count_at_255`).

### Success Criteria
- [ ] `parse_reply`, `parse_typed_reply`, `parse_callback_name` exist in core.rs
      (inserted after `build_typed_payload`, before `MatchKey`).
- [ ] `parse_reply` is `pub(crate)`, carries `#[allow(dead_code)]`, and its two
      helpers are private `fn` with NO allow (and the build is warning-free).
- [ ] `RESPONSE_MARKER` no longer carries `#[allow(dead_code)]`; the build is
      still warning-free (it has a consumer now).
- [ ] All field reads in `parse_typed_reply` use `.get(i).copied().unwrap_or(0)`;
      the QUERY_CALLBACK name slice uses `&response[3.min(response.len())..]`.
- [ ] `parse_callback_name` returns `None` for empty/NUL-at-start and uses
      `String::from_utf8(...).ok()` (NOT `from_utf8_lossy`).
- [ ] The 6 mandated tests exist and pass (exact byte→variant mapping in research
      `notes.md` F8).
- [ ] `cargo build` → zero warnings; `cargo clippy --lib` → zero warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → **45 passed, 0 failed**.
- [ ] No file other than `src/core.rs` is modified.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The three function bodies are
> given verbatim (copy-paste ready) with their exact insertion anchor (landmark,
> not line number); the `RESPONSE_MARKER` allow-removal is a one-line delete with
> its surrounding context quoted; the 6 tests are given verbatim with the exact
> expected `CommandResponse` for each; the dead_code-propagation behavior is
> empirically proven (CASE A: zero warnings); the baseline (39 tests) and all
> build/clippy/fmt/test commands are verified working in this repo. The implementer
> needs no QMK firmware source — the wire contract is fully canonicalized in
> `firmware_wire_contract.md`.

### Documentation & References

```yaml
# MUST READ — the file being edited (the 3 new functions + 6 new tests + 1 use edit)
- file: src/core.rs
  why: "Holds build_typed_payload (the INSERTION ANCHOR — the new functions go
        immediately after its closing brace, before struct MatchKey's doc
        comment 'Match parameters a cached handle set was opened for'). Holds
        RESPONSE_MARKER (the #[allow(dead_code)] to delete is directly above it).
        Holds the #[cfg(test)] mod tests block whose use line is edited and whose
        END is the test-append anchor."
  pattern: "build_typed_payload is pub(crate) + #[allow(dead_code)] (no live
            caller yet) + fully-qualified crate::RunCommand in its signature +
            `use crate::RunCommand;` INSIDE its body for the match arms. Follow
            the SAME style for parse_reply (see Implementation Patterns). Tests
            are descriptive snake_case with NO test_ prefix and use
            `use super::*` + `use crate::{HostOs, RunCommand};`."
  gotcha: "parse_reply does NOT exist yet (VERIFIED: grep finds only a comment at
           ~line 24). This is an ADD, not a fix. Do NOT hunt for a todo!() or a
           stub. Do NOT rename anything. The 5 CMD_* constants and RESPONSE_MARKER
           already exist (P1.M1.T2.S1) — import them via `use super::*` in tests."

# MUST READ — the canonical wire contract (the bytes this parser decodes)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "§Reply Disambiguation gives the response[0] decision table (0x51⇒typed,
        0/1⇒legacy, else/none⇒Timeout). §Field Definitions gives the exact byte
        offsets for QUERY_INFO ([0x51][0x01][proto_ver][feature_flags]
        [callback_count][board_rules_present]) and QUERY_CALLBACK
        ([0x51][0x02][index][name, NUL-padded], name absent ⇒ [3]=0x00). §Command
        Table gives the ack shapes ([0x51][cmd_echo][ack], ack==1⇒applied). §Constants
        pins RESPONSE_MARKER=0x51 and the CMD_* ids. This is the single source of
        truth the tests assert against."
  section: "Reply Disambiguation", "Field Definitions", "Command Table", "Constants"
  critical: "board_rules_present is a u8 (0/1) on the wire but a bool in
             CommandResponse — coerce with `!= 0`. QUERY_CALLBACK's name is
             NUL-terminated/NUL-padded; an immediate 0x00 at the name position
             means 'no name' ⇒ None. SET_OS (0x03) and APPLY_HOST_CONTEXT (0x05)
             share the SAME ack shape ([0x51][cmd_echo][ack]) so they share one
             match arm."

# REFERENCE — the types this function produces (defined at crate root in lib.rs)
- file: src/lib.rs
  why: "Defines CommandResponse (the return type) with variants Legacy{matched},
        Info{proto_ver,feature_flags,callback_count,board_rules_present},
        CallbackName{index,name: Option<String>}, Ack{ok}, Timeout. ALREADY derives
        Debug, Clone, PartialEq, Eq (lib.rs ~line 85) so assert_eq! works. DO NOT
        EDIT lib.rs — parse_reply is pub(crate) and reached by run() via
        core::parse_reply, so it is NOT added to the `pub use core::{...}` line."
  pattern: "Return type is written fully-qualified in the signature
            (`-> crate::CommandResponse`) and a local `use crate::CommandResponse;`
            inside the fn body brings the variant names into scope for the match
            arms — mirrors build_typed_payload's `use crate::RunCommand;` style."

# REFERENCE — the previous (sibling) PRP: explains the divergence-reconciliation pattern
- docfile: plan/001_b92a9b2b603f/P1M2T1S2/PRP.md
  why: "S2-of-T1 faced the same shape of title/contract divergence and reconciled
        it with an explicit banner. Its 'Known Gotchas' also empirically proved the
        dead_code-propagation rule (a const referenced by an allow-dead fn does NOT
        warn) and the `as u8` truncation pitfall — both directly relevant here."

# REFERENCE — PRD framing + invariants
- file: PRD.md
  why: "§8 (Response Handling) + §10.2 (Reply parsing) define the disambiguation
        and the Timeout⇒string-only-mode fallback. §14 invariant 6 pins that
        reply parsing disambiguates 0x51 from 0/1 from no-reply."
  section: "8. Response Handling", "10.2 Reply parsing", "14. Key Invariants (6, 7)"

# REFERENCE — empirical evidence (dead_code CASE A/B, baseline test-count math)
- docfile: plan/001_b92a9b2b603f/P1M2T2S1/research/notes.md
  why: "Documents F1–F10: parse_reply's absence, the 39-test baseline (+6⇒45), the
        dead_code CASE A/B experiment (allow on the entry ALONE suffices), the
        RESPONSE_MARKER-allow-removal safety, the S1/S2 scope reconciliation, and
        the exact 6 test→variant mapping table."
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
    ├── core.rs         # <-- FILE TO EDIT (3 new fns + RESPONSE_MARKER allow del + 6 tests)
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # CommandResponse/RunCommand/run() — DO NOT TOUCH
```

### Desired Codebase tree with files to be modified

```bash
src/
└── core.rs   # MODIFIED ONLY:
              #   1. INSERT parse_reply + parse_typed_reply + parse_callback_name
              #      after build_typed_payload, before struct MatchKey.
              #   2. DELETE the #[allow(dead_code)] line above RESPONSE_MARKER.
              #   3. EDIT `use crate::{HostOs, RunCommand};` → add CommandResponse.
              #   4. APPEND 6 #[test] fns at END of #[cfg(test)] mod tests:
              #      parse_reply_info_reply
              #      parse_reply_info_board_rules_absent
              #      parse_reply_callback_name_named
              #      parse_reply_callback_name_unnamed
              #      parse_reply_ack_set_os_applied
              #      parse_reply_ack_apply_host_context_rejected
# (lib.rs, error.rs, main.rs, Cargo.toml unchanged)
```

> No new files. One file modified (`src/core.rs`). No new dependencies.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (dead_code propagation — EMPIRICALLY VERIFIED): a private helper fn
//   called ONLY by an #[allow(dead_code)] entry fn does NOT warn. Proof
//   (throwaway rustc --crate-type rlib):
//     CASE A: entry has #[allow(dead_code)], helpers bare  ⇒ ZERO warnings.
//     CASE B: no allow anywhere                              ⇒ 3 warnings.
//   CONCLUSION: put #[allow(dead_code)] on parse_reply ONLY; parse_typed_reply
//   and parse_callback_name do NOT need their own allow. This is the SAME pattern
//   as build_typed_payload (single allow on the entry). If you skip the allow on
//   parse_reply, the build WILL warn "function `parse_reply` is never used"
//   (CASE B) — it has no live caller until P1.M3.T3.

// CRITICAL (RESPONSE_MARKER allow removal is SAFE): once parse_reply references
//   RESPONSE_MARKER, the const has a consumer and its #[allow(dead_code)] becomes
//   redundant. core.rs lines 18-23 (an existing comment written for P1.M1.T2.S1)
//   already document: "a const referenced by an allow-dead fn's body does NOT
//   warn." So DELETE the allow above RESPONSE_MARKER. Do NOT touch
//   REPLY_READ_TIMEOUT_MS's allow — its consumer (the reply reader) is P1.M3.T1.

// CRITICAL (defensive .get() is MANDATORY, not optional): the firmware reply may
//   be TRUNCATED (shorter than 32 bytes). `response[5]` would PANIC on a 3-byte
//   reply and wedge a live run(). Use `response.get(i).copied().unwrap_or(0)`
//   everywhere in parse_typed_reply, and `&response[3.min(response.len())..]` for
//   the QUERY_CALLBACK name slice (3.min(len) guarantees the start index never
//   exceeds len, so the slice is never out of bounds). S1 IMPLEMENTS this
//   defensively; S2 TESTS the truncation paths.

// CRITICAL (String::from_utf8(...).ok(), NOT from_utf8_lossy): the item says "Use
//   String::from_utf8 with .ok()". `.ok()` returns None for invalid UTF-8 (a
//   corrupt name is treated as absent); `from_utf8_lossy` would substitute U+FFFD.
//   For the documented ASCII-only names (0x20–0x7E) both behave identically. Use
//   `.ok()` per the contract. NOTE: String::from_utf8 takes a Vec<u8>, so call it
//   as `String::from_utf8(name_bytes.to_vec()).ok()` (the .to_vec() is required).

// NOTE (scope split — see banner): the item's CONTRACT code is the COMPLETE
//   parse_reply (legacy/timeout arms included). Rust match-exhaustiveness makes
//   those arms UNAVOIDABLE — you cannot compile parse_reply without them. So S1
//   implements the full body; S1's 6 tests are the typed happy paths; S2 adds the
//   legacy/timeout/edge-case TESTS. Do not "defer" the legacy/timeout arms to S2.

// NOTE (return-type style): follow build_typed_payload — fully-qualify the return
//   type in the signature (`-> crate::CommandResponse`) and add a local
//   `use crate::CommandResponse;` INSIDE the fn body so the match arms can name
//   the variants unqualified. parse_callback_name returns Option<String> (no
//   CommandResponse reference) so it needs no such use.

// NOTE (no live caller): parse_reply has NO live caller yet (run()'s typed arms
//   are still todo!(); migration is P1.M3.T3.S1). It carries
//   #[allow(dead_code)] — do NOT remove it until P1.M3.T3 wires run() to call it.
//   (Same lifecycle as build_typed_payload.)

// NOTE (CommandResponse is in lib.rs, NOT core): the test module currently does
//   `use crate::{HostOs, RunCommand};` — ADD CommandResponse to that list so the
//   6 tests can name `CommandResponse::Info { ... }` unqualified. `use super::*`
//   brings parse_reply + the CMD_*/RESPONSE_MARKER constants into scope but does
//   NOT cross into lib.rs.

// NOTE: no rustfmt.toml / no clippy.toml exist → default config (verified by the
//   P1.M2.T1.S2 PRP). `.get(i).copied().unwrap_or(0)`, `String::from_utf8(..).ok()`,
//   and `&response[3.min(response.len())..]` are all clippy-clean under defaults.
//   Run `cargo clippy --lib` to confirm zero warnings after the edit.

// NOTE (moving target): P1.M2.T1.S2 was editing core.rs in parallel during this
//   PRP's research. LINE NUMBERS DRIFT. Anchor all edits by LANDMARK
//   (function/struct/doc-comment names), never by line number. Re-read core.rs
//   immediately before editing.
```

## Implementation Blueprint

### Data models and structure
No new types, structs, enums, or constants. `CommandResponse` (the return type)
already exists in lib.rs with all derives. This subtask adds **3 pure functions**
and **6 tests**. No state, no I/O, no globals, no new deps.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: INSERT src/core.rs — the 3 functions after build_typed_payload
  - FIND anchor: the closing `}` of `build_typed_payload` (the fn whose body ends
          with `payload.push(0x03); payload`), immediately BEFORE the doc comment
          `/// Match parameters a cached handle set was opened for.` above
          `#[derive(Clone, Copy, PartialEq, Eq, Debug)] struct MatchKey`.
  - INSERT: the 3 functions VERBATIM from "Implementation Patterns" §A below
          (parse_reply, parse_typed_reply, parse_callback_name — in that order,
          two blank lines between each, matching core.rs's fn spacing).
  - STYLE: parse_reply is `#[allow(dead_code)] pub(crate) fn` with signature
          `(response: &[u8]) -> crate::CommandResponse` and a local
          `use crate::CommandResponse;` inside the body. parse_typed_reply and
          parse_callback_name are private `fn` (NO allow — see Known Gotchas).
  - DO NOT: modify build_typed_payload, batches_for, burst_to_one, or any cache fn.

Task 2: DELETE src/core.rs — the redundant #[allow(dead_code)] above RESPONSE_MARKER
  - FIND: the block
          `/// Typed-response marker: response[0] == 0x51 means typed reply (PRD §10.2).\n#[allow(dead_code)]\npub(crate) const RESPONSE_MARKER: u8 = 0x51;`
  - DELETE ONLY the `#[allow(dead_code)]` line (keep the doc comment + the const).
  - DO NOT: touch REPLY_READ_TIMEOUT_MS's `#[allow(dead_code)]` (its consumer is
          P1.M3.T1, not this task).

Task 3: EDIT src/core.rs — the test-module use line + append 6 tests
  - EDIT: inside `#[cfg(test)] mod tests`, change
          `use crate::{HostOs, RunCommand};` → `use crate::{CommandResponse, HostOs, RunCommand};`
  - APPEND: the 6 `#[test]` functions VERBATIM from "Implementation Patterns" §B
          at the END of the `mod tests` block (after the current last test
          `build_typed_payload_apply_host_context_clamps_count_at_255`, before the
          module's closing `}`). Descriptive snake_case, NO `test_` prefix.
  - DO NOT: edit or remove any existing test.

Task 4: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --lib`, then
          `cargo fmt --check`, then `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0;
          test result "45 passed; 0 failed" (39 baseline + 6 new).
  - IF "function `parse_reply` is never used": you forgot its #[allow(dead_code)].
  - IF "function `parse_typed_reply`/`parse_callback_name` is never used": you
          added an unnecessary allow OR parse_reply lost its allow — confirm
          parse_reply still has #[allow(dead_code)] and references both helpers.
  - IF "unused import: `CommandResponse`": a test isn't using it yet — all 6 do.
```

### Implementation Patterns & Key Details

#### §A — the 3 functions (copy-paste ready; insert after `build_typed_payload`)

```rust
/// Parse a raw device reply into a [`crate::CommandResponse`].
///
/// `response[0]` disambiguates the reply
/// (`firmware_wire_contract.md` §Reply Disambiguation):
/// - [`crate::RESPONSE_MARKER`] (`0x51`) ⇒ typed reply, decoded by `response[1]`
///   (the command-echo byte) via [`parse_typed_reply`].
/// - `0` ⇒ [`crate::CommandResponse::Legacy`] `{ matched: false }`.
/// - `1` ⇒ [`crate::CommandResponse::Legacy`] `{ matched: true }`.
/// - empty, or any other marker ⇒ [`crate::CommandResponse::Timeout`] (treat as
///   a non-capable / legacy / offline device; the caller stays in string-only
///   mode — PRD §8, §10.2).
///
/// Every field access in the typed path uses defensive `.get(...)` indexing —
/// firmware replies may be truncated, so missing bytes default to `0` rather
/// than panicking. Consumer: the `run()` typed dispatch (P1.M3.T3.S1). Until then
/// this is referenced only by tests, hence `#[allow(dead_code)]` — remove it in
/// P1.M3.T3 once `run()` calls it (same lifecycle as [`build_typed_payload`]).
#[allow(dead_code)]
pub(crate) fn parse_reply(response: &[u8]) -> crate::CommandResponse {
    use crate::CommandResponse;
    if response.is_empty() {
        return CommandResponse::Timeout;
    }
    match response[0] {
        RESPONSE_MARKER => parse_typed_reply(response),
        0 => CommandResponse::Legacy { matched: false },
        1 => CommandResponse::Legacy { matched: true },
        _ => CommandResponse::Timeout, // unknown marker ⇒ treat as non-capable
    }
}

/// Decode a typed reply (`response[0] == RESPONSE_MARKER`) by its `response[1]`
/// command-echo byte. Field layouts per `firmware_wire_contract.md` §Field
/// Definitions; every byte is read with `.get(i).copied().unwrap_or(0)` so a
/// truncated reply never panics.
fn parse_typed_reply(response: &[u8]) -> crate::CommandResponse {
    use crate::CommandResponse;
    let cmd_echo = response.get(1).copied().unwrap_or(0);
    match cmd_echo {
        CMD_QUERY_INFO => CommandResponse::Info {
            proto_ver: response.get(2).copied().unwrap_or(0),
            feature_flags: response.get(3).copied().unwrap_or(0),
            callback_count: response.get(4).copied().unwrap_or(0),
            // u8 (0/1) on the wire ⇒ bool (the != 0 coercion).
            board_rules_present: response.get(5).copied().unwrap_or(0) != 0,
        },
        CMD_QUERY_CALLBACK => {
            let index = response.get(2).copied().unwrap_or(0);
            // Defensive slice: 3.min(len) guarantees the start index never exceeds
            // len, so a reply shorter than 4 bytes yields an empty name slice
            // (⇒ None) instead of panicking on `response[3..]`.
            let name = parse_callback_name(&response[3.min(response.len())..]);
            CommandResponse::CallbackName { index, name }
        }
        // SET_OS (0x03) and APPLY_HOST_CONTEXT (0x05) share the ack shape
        // [0x51][cmd_echo][ack]; ack == 1 ⇒ applied (ok: true).
        CMD_SET_OS | CMD_APPLY_HOST_CONTEXT => {
            CommandResponse::Ack {
                ok: response.get(2).copied().unwrap_or(0) != 0,
            }
        }
        _ => CommandResponse::Timeout, // unknown cmd echo ⇒ non-capable
    }
}

/// Decode a NUL-terminated ASCII callback name from `bytes`.
///
/// Reads up to the first `0x00` NUL or end of slice. Returns `None` when the name
/// is empty (NUL-at-start or empty slice) — the firmware emits an immediate `0x00`
/// when a callback has no name or the index is out of range
/// (`firmware_wire_contract.md` §QUERY_CALLBACK response). `String::from_utf8`
/// succeeds for the documented ASCII names (`0x20–0x7E`); invalid UTF-8 yields
/// `None` via `.ok()` (lossy substitution is deliberately NOT used).
fn parse_callback_name(bytes: &[u8]) -> Option<String> {
    let end = bytes.iter().position(|&b| b == 0x00).unwrap_or(bytes.len());
    let name_bytes = &bytes[..end];
    if name_bytes.is_empty() {
        return None;
    }
    String::from_utf8(name_bytes.to_vec()).ok()
}
```

#### §B — the 6 tests (copy-paste ready; append at END of `mod tests`)

```rust
    #[test]
    fn parse_reply_info_reply() {
        // QUERY_INFO typed reply (firmware_wire_contract.md §QUERY_INFO response):
        // [0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present].
        let response = [0x51, 0x01, 2, 0x03, 5, 1];
        assert_eq!(
            parse_reply(&response),
            CommandResponse::Info {
                proto_ver: 2,
                feature_flags: 0x03,
                callback_count: 5,
                board_rules_present: true,
            }
        );
    }

    #[test]
    fn parse_reply_info_board_rules_absent() {
        // Same shape, but board_rules_present byte == 0 ⇒ bool false (the != 0
        // coercion in parse_typed_reply's CMD_QUERY_INFO arm).
        let response = [0x51, 0x01, 2, 0x03, 5, 0];
        assert_eq!(
            parse_reply(&response),
            CommandResponse::Info {
                proto_ver: 2,
                feature_flags: 0x03,
                callback_count: 5,
                board_rules_present: false,
            }
        );
    }

    #[test]
    fn parse_reply_callback_name_named() {
        // QUERY_CALLBACK typed reply: [0x51][0x02][index][name bytes, NUL-padded].
        // "Vim" = [V, i, m] = [0x56, 0x69, 0x6d], then a NUL terminator ends the name.
        let response = [0x51, 0x02, 3, b'V', b'i', b'm', 0x00, 0x00];
        assert_eq!(
            parse_reply(&response),
            CommandResponse::CallbackName {
                index: 3,
                name: Some("Vim".to_string()),
            }
        );
    }

    #[test]
    fn parse_reply_callback_name_unnamed() {
        // NUL at the name start ⇒ None. The firmware emits an immediate 0x00 when
        // the callback has no name or the requested index is out of range.
        let response = [0x51, 0x02, 5, 0x00, 0x00];
        assert_eq!(
            parse_reply(&response),
            CommandResponse::CallbackName { index: 5, name: None }
        );
    }

    #[test]
    fn parse_reply_ack_set_os_applied() {
        // SET_OS (cmd 0x03) ack: [0x51][0x03][ack]; ack == 1 ⇒ ok: true.
        let response = [0x51, 0x03, 1];
        assert_eq!(parse_reply(&response), CommandResponse::Ack { ok: true });
    }

    #[test]
    fn parse_reply_ack_apply_host_context_rejected() {
        // APPLY_HOST_CONTEXT (cmd 0x05) ack: [0x51][0x05][ack]; ack == 0 ⇒ ok: false.
        // Shares the CMD_SET_OS | CMD_APPLY_HOST_CONTEXT arm with the test above.
        let response = [0x51, 0x05, 0];
        assert_eq!(parse_reply(&response), CommandResponse::Ack { ok: false });
    }
```

#### §C — key pattern notes

```rust
// === WHY #[allow(dead_code)] ON parse_reply ONLY ===
//   parse_reply has no live caller until P1.M3.T3 (run()'s typed arms are todo!()).
//   Empirically (CASE A/B in research/notes.md F4), an #[allow(dead_code)] entry
//   fn SUPPRESSES the dead_code lint for its private callees — so
//   parse_typed_reply and parse_callback_name need NO allow of their own. If you
//   omit parse_reply's allow, the build warns "function `parse_reply` is never
//   used" (CASE B). Mirror build_typed_payload's lifecycle exactly.


// === WHY board_rules_present USES `!= 0` (not `== 1`) ===
//   The wire field is "u8 (0/1)" per the contract, but parsing it as a bool via
//   `!= 0` is strictly more permissive (any nonzero ⇒ true) and matches how a C
//   firmware `if (board_rules_present)` would read it. The two mandated tests
//   pin both polarities (byte 1 ⇒ true, byte 0 ⇒ false).


// === WHY `&response[3.min(response.len())..]` AND NOT `response.get(3..)` ===
//   Both are correct and clippy-clean. The item's CONTRACT prescribes the
//   `3.min(response.len())` form, so we use it for contract fidelity (same
//   reasoning the P1.M2.T1.S2 PRP gave for keeping `.min(255) as u8`). The
//   idiomatic alternative `response.get(3..).unwrap_or(&[])` is equivalent;
//   do not "improve" it away from the contract without cause.


// === WHY THE ACK ARM MERGES SET_OS AND APPLY_HOST_CONTEXT ===
//   Both commands reply `[0x51][cmd_echo][ack]` with identical ack semantics
//   (ack==1 ⇒ applied). A single `CMD_SET_OS | CMD_APPLY_HOST_CONTEXT =>` arm
//   avoids duplicating the decode. The 6 tests cover BOTH cmd echoes
//   (0x03⇒true, 0x05⇒false) so the merged arm is exercised on both sides.


// === WHY parse_callback_name RETURNS None FOR EMPTY (not Some("")) ===
//   The CommandResponse::CallbackName variant is `{ index, name: Option<String> }`.
//   An empty/NUL-at-start name ⇒ None (semantically "no name"), NOT Some(""). The
//   `unnamed` test pins this: [0x51,0x02,5,0x00,...] ⇒ CallbackName{index:5, name:None}.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - insert: "3 fns (parse_reply, parse_typed_reply, parse_callback_name) after
             build_typed_payload, before struct MatchKey"
  - delete: "the #[allow(dead_code)] line above RESPONSE_MARKER"
  - edit:   "`use crate::{HostOs, RunCommand};` → add `CommandResponse` in mod tests"
  - add:    "6 #[test] fns at END of #[cfg(test)] mod tests"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps.

PUBLIC API SURFACE:
  - adds:    "(nothing public — parse_reply is pub(crate); the helpers are private)"
  - unchanged: "all lib.rs public types (HostOs, RunCommand, CommandResponse,
                RunParameters), parse_cli_args, run() signature/body, all core::
                pub re-exports, all QmkError variants/Display"

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M3.T3.S1 (run dispatch): "migrates run()'s typed todo!() arms to call
        build_typed_payload → send_raw_report → core::parse_reply. At that point
        parse_reply loses its #[allow(dead_code)] (gains a live caller)."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified, and ONLY the 4 edits in Tasks 1-3. Do NOT:
    * defer the legacy/timeout arms to S2 (Rust requires them for exhaustiveness).
    * add legacy/timeout/edge-case TESTS (that is S2's explicit scope).
    * touch build_typed_payload, batches_for, burst_to_one, or any cache code.
    * edit lib.rs, error.rs, main.rs, or Cargo.toml.
    * remove REPLY_READ_TIMEOUT_MS's allow (consumer lands in P1.M3.T1).
    * add parse_reply to lib.rs's `pub use core::{...}` (it's pub(crate), internal).
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# IF "function `parse_reply` is never used": you forgot its #[allow(dead_code)].
# IF "function `parse_typed_reply`/`parse_callback_name` is never used":
#   parse_reply lost its allow (or you added a stray allow) — recheck.

# Lint (default clippy — no clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors. `.get().copied().unwrap_or(0)`,
# `String::from_utf8(..).ok()`, and `&response[3.min(..)]` are clippy-clean.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the 6 new tests in isolation first.
cargo test --lib parse_reply -- --nocapture
# Expected: 6 passed (info_reply, info_board_rules_absent, callback_name_named,
#   callback_name_unnamed, ack_set_os_applied, ack_apply_host_context_rejected).

# Confirm each variant decode individually.
cargo test --lib parse_reply_info -- --nocapture               # Info true + false
cargo test --lib parse_reply_callback_name -- --nocapture      # named + unnamed
cargo test --lib parse_reply_ack -- --nocapture                # SET_OS + APPLY_HOST_CONTEXT

# Full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 45 passed; 0 failed; 0 ignored; ..." (39 baseline + 6 new).
# NOTE: if P1.M2.T1.S2 has NOT yet merged its 2 tests, the baseline is 37 and the
#   total is 43 instead of 45. Reconcile by counting: `grep -c "#\[test\]" src/core.rs`
#   (expect 29 = 23 + 6) and `grep -c "#\[test\]" src/lib.rs` (expect 16).

# Sanity: existing tests still pass unchanged.
cargo test --lib build_typed_payload -- --nocapture   # all build_typed_payload_* still green
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
parse_reply is a pure function (&[u8] -> CommandResponse) with no HID I/O, no CLI
surface, and no runtime call site yet (run()'s typed arms are still todo!();
migration is P1.M3.T3). There is no live-hardware or runtime path to exercise.
The exhaustive unit tests in Level 2 — which assert the exact CommandResponse for
each of the 6 typed-reply byte layouts mandated by firmware_wire_contract.md
§Field Definitions — ARE the end-to-end verification for this task. (The firmware
itself does not implement typed commands yet — findings_and_risks.md F4 — so even
a hardware test would only see Timeout; synthetic byte buffers are the designed
test vehicle, per PRD §10.2.)
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the 3 functions are present and correctly attributed:
grep -nE "fn (parse_reply|parse_typed_reply|parse_callback_name)" src/core.rs
# Expected: 3 matches — parse_reply (pub(crate), with #[allow(dead_code)] above),
# parse_typed_reply (private fn), parse_callback_name (private fn).

# Confirm RESPONSE_MARKER's allow is GONE but REPLY_READ_TIMEOUT_MS's remains:
grep -nB1 "const RESPONSE_MARKER\|const REPLY_READ_TIMEOUT_MS" src/core.rs
# Expected: RESPONSE_MARKER has NO #[allow(dead_code)] above it;
#           REPLY_READ_TIMEOUT_MS STILL has #[allow(dead_code)] above it.

# Confirm the defensive .get() pattern is used (no raw response[N] indexing):
grep -nE "response\.(get|\.get)\(|response\[" src/core.rs
# Expected: only `response.get(i).copied().unwrap_or(0)` reads and the single
# `&response[3.min(response.len())..]` slice inside parse_typed_reply — NO bare
# `response[2]`/`response[5]` indexing that could panic on a truncated reply.

# Cross-check the decoded fields against the canonical contract by eye:
grep -nE "0x51|board_rules_present|proto_ver|QUERY_CALLBACK|NUL" \
  plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
# (RESPONSE_MARKER=0x51, QUERY_INFO/QUERY_CALLBACK field tables, ack==1⇒applied,
#  name NUL-padded — all should be consistent with the test assertions above.)
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1 passed: `cargo build` → zero warnings.
- [ ] Level 1 passed: `cargo clippy --lib` → zero warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → 45 passed, 0 failed (or 43 if
      P1.M2.T1.S2's 2 tests haven't merged yet — reconcile by counting).
- [ ] The 6 `parse_reply_*` tests pass individually.

### Feature Validation
- [ ] `parse_reply`, `parse_typed_reply`, `parse_callback_name` exist in core.rs
      (after `build_typed_payload`, before `struct MatchKey`).
- [ ] `parse_reply` is `pub(crate)` + `#[allow(dead_code)]`; helpers are private
      `fn` with NO allow, and the build is warning-free.
- [ ] `RESPONSE_MARKER`'s `#[allow(dead_code)]` is removed (consumer now exists);
      `REPLY_READ_TIMEOUT_MS`'s allow is UNCHANGED.
- [ ] All typed-path field reads use `.get(i).copied().unwrap_or(0)`; the
      QUERY_CALLBACK name slice uses `&response[3.min(response.len())..]`.
- [ ] `board_rules_present` decodes via `!= 0` (byte 1 ⇒ true, byte 0 ⇒ false).
- [ ] `parse_callback_name` returns `None` for empty/NUL-at-start and uses
      `String::from_utf8(...).ok()` (not `from_utf8_lossy`).
- [ ] SET_OS + APPLY_HOST_CONTEXT share one ack arm (`CMD_SET_OS | CMD_APPLY_HOST_CONTEXT`).
- [ ] The 6 tests assert the exact `CommandResponse` variants from research F8.

### Code Quality Validation
- [ ] Follows core.rs conventions: descriptive snake_case tests with no `test_`
      prefix; `use super::*` + `use crate::{CommandResponse, HostOs, RunCommand}`.
- [ ] Return type fully-qualified in signature + local `use crate::CommandResponse;`
      inside fn bodies (mirrors `build_typed_payload`).
- [ ] No file other than `src/core.rs` modified.
- [ ] Did NOT add legacy/timeout/edge-case tests (that is S2's scope).

### Documentation & Deployment
- [ ] Doc-comments cite PRD §8/§10.2 and `firmware_wire_contract.md` sections.
- [ ] The `#[allow(dead_code)]` comment explains the P1.M3.T3 removal trigger.
- [ ] No new environment variables or config.
- [ ] No README/PRD/Cargo.toml/lib.rs change (contract DOCS = "none" — internal
      `pub(crate)` function, no user-facing surface).

---

## Anti-Patterns to Avoid

- ❌ Don't hunt for a `todo!()` or stub — `parse_reply` does NOT exist yet
      (VERIFIED: `grep -n "fn parse_reply" src/core.rs` → no match; only a comment
      reference). This is a fresh ADD, not a fix.
- ❌ Don't "defer" the legacy `0`/`1` or `_ => Timeout` arms to S2 — Rust's match
      exhaustiveness REQUIRES them to compile `parse_reply`. S1 implements the
      COMPLETE function body; S2 only adds the legacy/timeout/edge-case TESTS.
- ❌ Don't add legacy/timeout/edge-case TESTS in S1 — that is S2's explicit scope
      ("Add legacy/timeout/edge-case handling to parse_reply and finalize tests").
      S1's tests are the 6 typed happy paths only.
- ❌ Don't omit `#[allow(dead_code)]` on `parse_reply` — it has no live caller
      until P1.M3.T3, so the build WILL warn "function `parse_reply` is never used"
      (CASE B of the dead_code experiment). Mirror `build_typed_payload`.
- ❌ Don't add `#[allow(dead_code)]` to `parse_typed_reply`/`parse_callback_name` —
      an allow on the entry `parse_reply` ALONE suppresses the lint for its private
      callees (CASE A: zero warnings). Extra allows are noise.
- ❌ Don't leave `#[allow(dead_code)]` on `RESPONSE_MARKER` — once `parse_reply`
      references it, the allow is redundant (core.rs lines 18-23 document that a
      const referenced by an allow-dead fn does NOT warn). DELETE it. (But DO leave
      `REPLY_READ_TIMEOUT_MS`'s allow — its consumer is P1.M3.T1.)
- ❌ Don't use bare `response[N]` indexing in `parse_typed_reply` — a truncated
      reply would PANIC and wedge a live `run()`. Use `.get(N).copied().unwrap_or(0)`
      everywhere, and `&response[3.min(response.len())..]` for the name slice.
- ❌ Don't use `String::from_utf8_lossy` — the item prescribes
      `String::from_utf8(...).ok()` (None for invalid UTF-8, not U+FFFD substitution).
      And remember `String::from_utf8` takes a `Vec<u8>`: call it as
      `String::from_utf8(name_bytes.to_vec()).ok()`.
- ❌ Don't return `Some("")` for an empty/NUL-at-start callback name — return `None`
      (the variant is `name: Option<String>`; "no name" ⇒ None, not empty string).
- ❌ Don't decode `board_rules_present` with `== 1` — use `!= 0` (any nonzero ⇒
      true), matching how C firmware reads a u8 bool. The 2 Info tests pin both.
- ❌ Don't split SET_OS and APPLY_HOST_CONTEXT into two ack arms — they share the
      identical `[0x51][cmd_echo][ack]` shape; one `CMD_SET_OS | CMD_APPLY_HOST_CONTEXT`
      arm avoids duplication.
- ❌ Don't add `parse_reply` to lib.rs's `pub use core::{...}` — it's `pub(crate)`,
      internal (reached by `run()` via `core::parse_reply`). Adding it would leak
      an internal type into the public API.
- ❌ Don't edit `lib.rs`, `error.rs`, `main.rs`, or `Cargo.toml` — `CommandResponse`
      already exists with all derives; no new types/constants/deps are needed.
- ❌ Don't anchor edits by line number — P1.M2.T1.S2 edits core.rs in parallel, so
      line numbers drift. Anchor by LANDMARK (the `build_typed_payload` closing
      brace, the `/// Match parameters a cached handle set was opened for.` doc
      comment, the `RESPONSE_MARKER` const, the last test fn name). Re-read core.rs
      immediately before editing.

---

**Confidence Score: 9/10** for one-pass implementation success. The deliverable is
three small pure functions given **verbatim** (copy-paste ready), the exact
6 tests given verbatim with their exact expected `CommandResponse` variants pinned
to the canonical firmware wire contract, and two trivial mechanical edits
(delete one `#[allow(dead_code)]` line; add `CommandResponse` to one `use` line).
The one risk keeping it from 10/10 is the **S1/S2 scope ambiguity**: an implementer
who skims the item title ("typed 0x51 replies") without reading this PRP's banner
might try to implement only the typed path and hit a match-exhaustiveness error,
or might over-reach into S2's legacy/timeout tests. The banner + *Scope Boundary*
+ *Anti-Patterns* are written to make both failure modes impossible. The
dead_code-propagation behavior is **empirically proven** (CASE A/B), the
RESPONSE_MARKER-allow-removal safety is documented in the codebase's own comment
(core.rs lines 18-23), the baseline (39 tests) and the +6⇒45 math are verified
working in this repo, and no existing test is affected. No file other than
`src/core.rs` is touched; no public API changes; no new deps.