# PRP — P1.M2.T2.S2: Add legacy/timeout/edge-case handling to `parse_reply` and finalize tests

> ⚠️ **READ THIS BANNER FIRST — it reconciles the S1/S2 scope split.**
>
> S1's title is "parse_reply for typed `0x51` replies" and S2's title is "Add
> legacy/timeout/edge-case handling to parse_reply and finalize tests." A naïve
> read suggests S2 implements the legacy/timeout ARMS of `parse_reply`. **That is
> wrong.** Rust requires the top-level `match response[0]` to be **exhaustive**, so
> S1 already landed the COMPLETE function body (legacy `0`/`1` arms + the `_ =>
> Timeout` arm + the typed `0x51` arm). **S2 adds ONLY the legacy/timeout/edge-case
> TESTS** — the function body is already complete and correct (verified: S1's code
> is merged in `src/core.rs`; see Context §0).
>
> **RECONCILED SCOPE (this PRP):** append **8 `#[test]` functions** to core.rs's
> `#[cfg(test)] mod tests` block — one per item-mandated edge case (empty slice,
> legacy `0`, legacy `1`, too-short typed, unknown cmd echo, unknown marker,
> non-UTF8 callback name, truncated Info). If a test FAILS it reveals an S1 bug —
> fix the offending line in `parse_reply`/`parse_typed_reply`/`parse_callback_name`
> (this task is the "safety net"). If all 8 pass (expected), the deliverable is
> purely the 8 tests. **`src/core.rs` is the ONLY file modified.**

---

## Goal

**Feature Goal**: Land the legacy/timeout/edge-case half of `parse_reply`'s test
matrix so the reply parser is **exhaustively tested across every wire-protocol
reply shape** (empty/no-reply, legacy match-bool `0`/`1`, typed `0x51`, unknown
marker, unknown cmd echo, truncated/garbage payloads). This "finalizes" the
matrix begun by S1's 6 typed happy-path tests and turns `parse_reply` into a
known-good decoder that P1.M3.T3's `run()` dispatch can wire against without
surprises.

**Deliverable**: **8 new `#[test]` functions** appended to the END of
`src/core.rs`'s `#[cfg(test)] mod tests` block (immediately after S1's last test
`parse_reply_ack_apply_host_context_rejected`). Each maps 1:1 to an item-mandated
edge case and asserts the exact `CommandResponse`. No function code is written
unless a test fails (safety-net fix). **`src/core.rs` is the ONLY file modified.**

**Success Definition**: `cargo build` → zero warnings; `cargo clippy --lib` →
zero warnings; `cargo fmt --check` → exit 0; `cargo test --lib` → **57 passed, 0
failed** (current baseline 49 + 8 new); the 8 new tests pass individually and
each asserts the exact `CommandResponse` documented in research `notes.md` F4/F5;
no file other than `src/core.rs` is modified.

## User Persona (if applicable)

**Target User**: The v0.3.0 transport dispatch path (`run()`, P1.M3.T3.S1) and,
transitively, the downstream `qmkonnect` desktop daemon. Today nothing calls
`parse_reply` at runtime yet (`run()`'s typed arms still return a `Timeout`
placeholder); the function + its FULL test matrix are landed so P1.M3.T3 wires
`run()` against an exhaustively-validated decoder.

**Use Case**: After `send_raw_report` returns the first captured IN report
(P1.M3.T2), `run()` hands those raw bytes to `core::parse_reply(&bytes)`. The
reply may be any of: an empty/no-reply (device offline ⇒ `Timeout`), a legacy
match-bool (`0`/`1` ⇒ `Legacy`), a typed reply (`0x51` ⇒ `Info`/`CallbackName`/
`Ack`), OR a truncated/garbage/unknown byte stream. The 8 tests in this task pin
the behavior of the **non-happy-path** shapes so a regression in defensive
`.get()` indexing, the unknown-marker/unknown-cmd-echo fallbacks, or
`String::from_utf8(...).ok()` UTF-8 handling is caught here — not in P1.M3.T3
integration.

**User Journey**: a legacy (pre-v0.3) keyboard receives `QUERY_INFO` → replies
`[0x00]` (match-bool `0`) → `run()` calls `parse_reply(&[0x00])` →
`CommandResponse::Legacy { matched: false }` → caller stays in string-only mode
(PRD §8). A typed-capable keyboard that crashes mid-reply sends a truncated
`[0x51, 0x01, 2, 0x03]` → `parse_reply` returns `Info { proto_ver: 2,
feature_flags: 0x03, callback_count: 0, board_rules_present: false }` (defaults)
**without panicking** — the defensive `.get()` is the guard this task certifies.

**Pain Points Addressed**: (1) S1's 6 tests cover ONLY the typed happy paths; the
legacy, timeout, truncation, and unknown-byte shapes are **untested**, so a
future refactor could silently break the string-only-mode fallback or introduce a
panic on a short reply. (2) The firmware is NOT yet implemented
(`findings_and_risks.md` F4), so a live device will most often reply `Timeout` or
`Legacy` — exactly the paths this task certifies. (3) A truncated/garbage IN
report must never panic `run()`; the 8 tests make the defensive `.get()` contract
executable and regression-proof.

## Why

- **PRD §8 (Response Handling) + §10.2 (Reply parsing)** + `firmware_wire_contract.md`
  §Reply Disambiguation define the full decision table (`0x51`⇒typed,
  `0`/`1`⇒legacy, no-reply/unknown⇒Timeout). S1 transcribed the **typed** column
  into tests; S2 transcribes the **legacy + timeout + unknown + truncated**
  columns. Together they exhaust the table.
- **"Safety net" role (item contract):** the item explicitly says *"If any test
  reveals a bug in S1's implementation, fix it here."* Research (`notes.md` F4)
  traces all 8 cases through the actual code and confirms **no bug exists**, but
  the tests are the executable proof — they pin the behavior so a future change
  to `parse_reply` cannot silently regress any reply shape.
- **Dependency-chain integrity**: P1.M3.T3 (`run()` dispatch) consumes
  `parse_reply`. Landing the full matrix now means P1.M3.T3 wires against a
  decoder whose **every** output is pinned by a test, eliminating a whole class
  of integration-time parse bugs.
- **Pure + additive**: the 8 tests are `assert_eq!` calls over byte-array
  literals. No I/O, no globals, no new deps, no public-API change, no edit to the
  function bodies (expected). They cannot break `run()` or any existing test.

## What

### 0. The scope split (CONTEXT — understand before coding)
See the banner at the top. S1 landed the COMPLETE `parse_reply` body (required
for Rust match-exhaustiveness) and 6 typed happy-path tests. **S2 adds ONLY the
8 legacy/timeout/edge-case tests.** Do NOT re-implement or "extend" the function
arms — they already exist and are correct. Do NOT add `#[allow(...)]`, edit the
`use` line (CommandResponse is already imported), or touch any other file.

### 1. Append the 8 tests (verbatim code in *Implementation Patterns* §B)
Append the 8 `#[test]` functions at the **END of the `#[cfg(test)] mod tests`
block** in `src/core.rs` — immediately after the current last test
`parse_reply_ack_apply_host_context_rejected`. Anchor by that fn NAME (line
numbers drift). Descriptive snake_case, `parse_reply_*` prefix (matches S1's 6),
NO `test_` prefix. `CommandResponse` is already in the test-module `use` list
(S1 added it); `parse_reply` reaches the tests via `use super::*`.

### 2. Run the matrix; fix S1 ONLY if a test fails (safety net)
Run `cargo test --lib parse_reply`. All 14 reply tests (S1's 6 + S2's 8) must
pass. **If any of the 8 new tests FAILS**, it reveals an S1 bug: compare the
actual `CommandResponse` against the expected value in research `notes.md` F4/F5,
read the offending line in `parse_reply`/`parse_typed_reply`/`parse_callback_name`,
and apply the **minimal** fix (do NOT rewrite the function). Research confirms
all 8 should pass against the current code, so a fix is **not expected** — but
the item mandates the safety net.

### Success Criteria
- [ ] The 8 mandated tests exist in core.rs's `mod tests`, appended after
      `parse_reply_ack_apply_host_context_rejected`, named per F5.
- [ ] Each test asserts the **exact** `CommandResponse` from research F4/F5.
- [ ] `cargo build` → zero warnings; `cargo clippy --lib` → zero warnings;
      `cargo fmt --check` → exit 0; `cargo test --lib` → **57 passed, 0 failed**.
- [ ] No file other than `src/core.rs` is modified (and within core.rs, no
      change to the function bodies, the `use` line, or the constants — UNLESS a
      test failed and triggered the safety-net fix).

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The 8 tests are given
> **verbatim** (copy-paste ready) with their exact byte inputs and exact expected
> `CommandResponse` values, all traced byte-for-byte through the actual `src/core.rs`
> `parse_reply`/`parse_typed_reply`/`parse_callback_name` bodies in research
> `notes.md` F4. The insertion anchor is a named fn (no line-number drift risk).
> The test-naming convention is quoted from S1's 6 existing tests. The baseline
> (49 tests, verified by running `cargo test --lib`) and the +8⇒57 math are
> verified working in this repo. The implementer needs no QMK firmware source —
> the wire contract is fully canonicalized in `firmware_wire_contract.md`.

### Documentation & References

```yaml
# MUST READ — the file being edited (the 8 new tests append to its mod tests block)
- file: src/core.rs
  why: "Holds parse_reply (src/core.rs:409), parse_typed_reply (line 426), and
        parse_callback_name (line 462) — the COMPLETE, correct S1 implementation
        these tests exercise. Holds the #[cfg(test)] mod tests block whose END
        (the parse_reply_ack_apply_host_context_rejected fn) is the APPEND anchor.
        Holds S1's 6 typed tests (parse_reply_info_reply … parse_reply_ack_*):
        follow their EXACT style for the 8 new tests."
  pattern: "S1's tests are: descriptive snake_case fn with NO test_ prefix;
            parse_reply_* prefix; a // comment explaining the wire shape; then a
            single assert_eq!(parse_reply(&byte_array_literal),
            CommandResponse::Variant { ... }). CommandResponse is imported via
            the existing `use crate::{CommandResponse, HostOs, RunCommand};`;
            parse_reply via `use super::*`."
  gotcha: "parse_reply, parse_typed_reply, parse_callback_name ALREADY EXIST and
           are CORRECT (verified — see Context §0 / notes.md F1). This task is an
           ADD of 8 tests, NOT a re-implementation. Do NOT edit the function
           bodies unless a test fails (safety-net). Do NOT edit the mod-tests
           `use` line — CommandResponse is already imported."

# MUST READ — research notes: the byte-for-byte trace of all 8 edge cases
- docfile: plan/001_b92a9b2b603f/P1M2T2S2/research/notes.md
  why: "F4 traces each of the 8 item-mandated inputs through the REAL parse_reply
        code and states the exact expected CommandResponse (the diagnostic oracle
        for the safety net). F5 is the 1:1 test→item-bullet mapping table. F2
        pins the verified baseline (49 tests) and the +8⇒57 expected total. F7
        is the scope boundary."
  section: "F4 (the 8 traces), F5 (mapping table), F2 (baseline), F6 (safety net), F7 (scope)"

# MUST READ — the canonical wire contract (the bytes these tests assert against)
- file: plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
  why: "§Reply Disambiguation gives the response[0] decision table (0x51⇒typed,
        0/1⇒legacy, else/none⇒Timeout) that cases 1/2/3/5/6 assert. §Field
        Definitions gives the QUERY_INFO offsets (case 8 truncated at offset 4)
        and QUERY_CALLBACK name shape (case 7 non-UTF8). §Constants pins
        RESPONSE_MARKER=0x51 and the cmd ids."
  section: "Reply Disambiguation", "Field Definitions", "Constants"

# REFERENCE — the sibling PRP that landed the function under test
- docfile: plan/001_b92a9b2b603f/P1M2T2S1/PRP.md
  why: "Defines parse_reply's contract and the S1/S2 scope reconciliation banner
        (S1 = complete body + 6 typed tests; S2 = 8 legacy/timeout/edge tests).
        Its 'Known Gotchas' empirically proved the defensive .get() mandate and
        the from_utf8().ok() (not lossy) rule — both EXERCISED by S2's cases 4/7/8."

# REFERENCE — the types these tests assert against (defined at crate root)
- file: src/lib.rs
  why: "Defines CommandResponse (Timeout, Legacy{matched}, Info{…},
        CallbackName{index,name:Option<String>}, Ack{ok}) and derives
        Debug, Clone, PartialEq, Eq (so assert_eq! works). DO NOT EDIT lib.rs."

# REFERENCE — PRD framing + invariants
- file: PRD.md
  why: "§8 (Response Handling) + §10.2 (Reply parsing) define the
        disambiguation and the Timeout/legacy ⇒ string-only-mode fallback that
        cases 1/2/3/5/6 certify. §14 invariant 6 pins that reply parsing
        disambiguates 0x51 from 0/1 from no-reply."
  section: "8. Response Handling", "10.2 Reply parsing", "14. Key Invariants (6)"
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
    ├── core.rs         # <-- FILE TO EDIT (append 8 tests to #[cfg(test)] mod tests)
    ├── error.rs        # QmkError enum — DO NOT TOUCH
    └── lib.rs          # CommandResponse/parse_reply's return type — DO NOT TOUCH
```

### Desired Codebase tree with files to be modified

```bash
src/
└── core.rs   # MODIFIED ONLY:
              #   APPEND 8 #[test] fns at the END of #[cfg(test)] mod tests
              #   (immediately after parse_reply_ack_apply_host_context_rejected):
              #     parse_reply_empty_slice_is_timeout
              #     parse_reply_legacy_zero_is_no_match
              #     parse_reply_legacy_one_is_matched
              #     parse_reply_typed_marker_only_is_timeout
              #     parse_reply_unknown_cmd_echo_is_timeout
              #     parse_reply_unknown_marker_is_timeout
              #     parse_reply_callback_name_non_utf8_is_none
              #     parse_reply_truncated_info_defaults_board_rules_false
              #   (NO other change — unless a test fails and triggers a safety-net fix
              #    in parse_reply/parse_typed_reply/parse_callback_name.)
# (lib.rs, error.rs, main.rs, Cargo.toml unchanged)
```

> No new files. One file modified (`src/core.rs`). No new dependencies.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (the function bodies are ALREADY COMPLETE & CORRECT): parse_reply,
//   parse_typed_reply, parse_callback_name landed in S1 (src/core.rs:409/426/462)
//   with the FULL match-exhaustive body (legacy 0/1 arms + _ => Timeout + typed
//   0x51 arm). This task ADDS 8 TESTS; it does NOT extend or re-implement the
//   function. Research notes.md F4 traces all 8 inputs through the real code and
//   confirms zero bugs. Only edit a function body if a test FAILS (safety net).

// CRITICAL (defensive .get() is MANDATORY, not optional — these tests CERTIFY it):
//   the firmware reply may be TRUNCATED (shorter than 32 bytes). Case 4 ([0x51],
//   len 1) exercises response.get(1) for cmd_echo (defaults to 0 ⇒ Timeout).
//   Case 8 ([0x51,0x01,2,0x03], len 4) exercises response.get(4)/.get(5) for
//   callback_count/board_rules_present (default to 0 ⇒ callback_count 0,
//   board_rules false). Both MUST return the documented CommandResponse WITHOUT
//   panicking. If either test panics, the function lost its .get() guard — FIX
//   parse_typed_reply to restore .get(i).copied().unwrap_or(0).

// CRITICAL (String::from_utf8(...).ok(), NOT from_utf8_lossy): case 7's non-UTF8
//   name bytes (0xFF,0xFE — never legal UTF-8) MUST decode to name: None, NOT to
//   Some("\u{FFFD}"). parse_callback_name uses .ok() per the item contract. If
//   case 7 returns Some(replacement_string), the function switched to
//   from_utf8_lossy — FIX it back to String::from_utf8(name_bytes.to_vec()).ok().

// NOTE (use line is already correct): the test module header already reads
//   `use super::*;` + `use crate::{CommandResponse, HostOs, RunCommand};`
//   (S1 added CommandResponse). DO NOT edit it. parse_reply reaches the tests via
//   `use super::*`; CommandResponse reaches them via the crate use.

// NOTE (naming): S1's 6 tests all use the `parse_reply_*` prefix with NO `test_`
//   prefix (e.g. parse_reply_info_reply). The 8 new tests use the SAME prefix so
//   the whole 14-test matrix sorts together under `parse_reply`. Descriptive
//   snake_case, one assert_eq! per test (matching S1's style).

// NOTE (placement): APPEND at the END of mod tests, after the fn
//   parse_reply_ack_apply_host_context_rejected (S1's last test). Keeps the
//   parse_reply_* family contiguous. Anchor by fn NAME — line numbers drift.

// NOTE (expected totals): baseline is 49 passed (core.rs 29 + lib.rs 20; VERIFIED
//   by running cargo test --lib — the S1 PRP's "45" assumed lib.rs had 16). S2
//   adds 8 ⇒ 57 passed, 0 failed. If you see 49 after editing, you forgot to
//   append; if you see a panic, a defensive guard regressed (safety-net fix).

// NOTE: no rustfmt.toml / no clippy.toml exist → default config (verified by the
//   S1 PRP). Byte-array literals and assert_eq!(…) are clippy/fmt-clean under
//   defaults. Run `cargo clippy --lib` to confirm zero warnings after the edit.
```

## Implementation Blueprint

### Data models and structure
No new types, structs, enums, functions, or constants. `CommandResponse` (the
asserted-against type) already exists in lib.rs with all derives. `parse_reply`
already exists and is correct. This subtask adds **8 tests** only. No state, no
I/O, no globals, no new deps.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: APPEND src/core.rs — the 8 edge-case tests at END of mod tests
  - FIND anchor: the closing `}` of the fn `parse_reply_ack_apply_host_context_rejected`
          (S1's last test — its body is `let response = [0x51, 0x05, 0]; assert_eq!(
          parse_reply(&response), CommandResponse::Ack { ok: false });`).
  - INSERT: the 8 #[test] functions VERBATIM from "Implementation Patterns" §B
          below, in the order listed (matches research F5's table), two blank
          lines between each (matching core.rs's fn spacing), BEFORE the mod
          tests block's closing `}`.
  - STYLE: descriptive snake_case, `parse_reply_*` prefix, NO `test_` prefix.
          One `assert_eq!(parse_reply(&[...]), CommandResponse::Variant {...})`
          per test, preceded by a `//` comment citing the wire shape / item case.
  - DO NOT: edit parse_reply/parse_typed_reply/parse_callback_name bodies (yet),
          edit the use line, edit the constants, or touch any other file.

Task 2: VALIDATE (do not skip) — this is the safety-net check
  - RUN: `cargo fmt`, then `cargo test --lib parse_reply` (runs all 14 reply tests).
  - EXPECT: "14 passed" for the parse_reply filter; overall `cargo test --lib` ⇒
          "57 passed; 0 failed" (49 baseline + 8 new).
  - IF ALL 8 NEW TESTS PASS: the function is confirmed correct; you are DONE.
          Proceed to Task 3.
  - IF ANY NEW TEST FAILS (safety net): compare the actual CommandResponse to the
          expected value in research notes.md F4/F5, read the failing line in
          parse_reply/parse_typed_reply/parse_callback_name, and apply the
          MINIMAL fix (do NOT rewrite). Re-run until green. Document the fix in
          the test's comment. (Research confirms this branch is NOT expected.)

Task 3: FINAL GATES
  - RUN: `cargo build`, `cargo clippy --lib`, `cargo fmt --check`, `cargo test --lib`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0; 57 passed 0 failed.
  - IF "unused import": impossible (no import added). IF a panic in a new test:
          a defensive .get() guard regressed — see Known Gotchas / apply safety net.
```

### Implementation Patterns & Key Details

#### §A — the function under test (for reference; DO NOT re-implement)

```rust
// These three fns ALREADY EXIST in src/core.rs (landed by S1). They are shown
// here ONLY so the implementer can read the behavior each test certifies. DO NOT
// paste them into the file — they are already there. Only the 8 tests in §B are new.

#[allow(dead_code)]
pub(crate) fn parse_reply(response: &[u8]) -> crate::CommandResponse {
    use crate::CommandResponse;
    if response.is_empty() {
        return CommandResponse::Timeout;                       // ← case 1
    }
    match response[0] {
        RESPONSE_MARKER => parse_typed_reply(response),
        0 => CommandResponse::Legacy { matched: false },        // ← case 2
        1 => CommandResponse::Legacy { matched: true },         // ← case 3
        _ => CommandResponse::Timeout,                          // ← case 6 (0x42)
    }
}

fn parse_typed_reply(response: &[u8]) -> crate::CommandResponse {
    use crate::CommandResponse;
    let cmd_echo = response.get(1).copied().unwrap_or(0);       // ← case 4 default (0 ⇒ Timeout)
    match cmd_echo {
        CMD_QUERY_INFO => CommandResponse::Info {               // ← case 8 truncated (len 4)
            proto_ver: response.get(2).copied().unwrap_or(0),
            feature_flags: response.get(3).copied().unwrap_or(0),
            callback_count: response.get(4).copied().unwrap_or(0),     // default 0
            board_rules_present: response.get(5).copied().unwrap_or(0) != 0, // default false
        },
        CMD_QUERY_CALLBACK => {
            let index = response.get(2).copied().unwrap_or(0);
            let name = parse_callback_name(&response[3.min(response.len())..]);
            CommandResponse::CallbackName { index, name }       // ← case 7 non-UTF8 ⇒ None
        }
        CMD_SET_OS | CMD_APPLY_HOST_CONTEXT => CommandResponse::Ack {
            ok: response.get(2).copied().unwrap_or(0) != 0,
        },
        _ => CommandResponse::Timeout,                          // ← case 5 (0xFF)
    }
}

fn parse_callback_name(bytes: &[u8]) -> Option<String> {
    let end = bytes.iter().position(|&b| b == 0x00).unwrap_or(bytes.len());
    let name_bytes = &bytes[..end];
    if name_bytes.is_empty() {
        return None;
    }
    String::from_utf8(name_bytes.to_vec()).ok()                 // ← case 7: 0xFF,0xFE ⇒ Err ⇒ None
}
```

#### §B — the 8 tests (copy-paste ready; append at END of `mod tests`)

```rust
    #[test]
    fn parse_reply_empty_slice_is_timeout() {
        // Empty input ⇒ Timeout. A zero-byte IN report means no reply arrived
        // within the bounded read_timeout (the caller passes &[] when
        // read_timeout returned Ok(0)). firmware_wire_contract.md §Reply
        // Disambiguation: "no reply within timeout ⇒ Timeout"; the caller treats
        // Timeout as a legacy/non-capable device and stays in string-only mode
        // (PRD §8, §10.2). Item edge case #1.
        assert_eq!(parse_reply(&[]), CommandResponse::Timeout);
    }

    #[test]
    fn parse_reply_legacy_zero_is_no_match() {
        // Legacy string-mode reply: response[0] == 0 is the match-bool "no
        // match" (the firmware wrote the bool, NOT a typed 0x51 marker). Item
        // edge case #2. firmware_wire_contract.md §Reply Disambiguation.
        assert_eq!(parse_reply(&[0]), CommandResponse::Legacy { matched: false });
    }

    #[test]
    fn parse_reply_legacy_one_is_matched() {
        // Legacy string-mode reply: response[0] == 1 is the match-bool "matched".
        // Item edge case #3.
        assert_eq!(parse_reply(&[1]), CommandResponse::Legacy { matched: true });
    }

    #[test]
    fn parse_reply_typed_marker_only_is_timeout() {
        // Degenerate "too short" typed reply: [0x51] alone (len 1 < 3).
        // parse_typed_reply reads cmd_echo via response.get(1).copied().unwrap_or(0)
        // ⇒ None ⇒ 0 (the slice has no index 1). 0 is not a known cmd id, so the
        // unknown-cmd-echo arm yields Timeout. CRITICAL: this MUST NOT panic — a
        // bare response[1] index would panic on a len-1 slice. The defensive
        // .get() is the guard this test certifies. Item edge case #4.
        assert_eq!(parse_reply(&[0x51]), CommandResponse::Timeout);
    }

    #[test]
    fn parse_reply_unknown_cmd_echo_is_timeout() {
        // Typed marker present, but response[1] == 0xFF is not a known command
        // echo (valid ids are 0x01/0x02/0x03/0x05; 0x04 is VIA-reserved and also
        // unknown to this crate). Treated as a non-capable / future-firmware
        // reply ⇒ Timeout. Exercises the `_ => Timeout` arm in parse_typed_reply.
        // Item edge case #5.
        assert_eq!(parse_reply(&[0x51, 0xFF]), CommandResponse::Timeout);
    }

    #[test]
    fn parse_reply_unknown_marker_is_timeout() {
        // response[0] == 0x42 is neither 0x51 (typed), 0 (legacy no-match), nor
        // 1 (legacy match). Per firmware_wire_contract.md §Reply Disambiguation,
        // any other marker byte ⇒ treat as a non-capable device ⇒ Timeout.
        // Exercises the `_ => Timeout` arm in the top-level parse_reply match.
        // Item edge case #6.
        assert_eq!(parse_reply(&[0x42]), CommandResponse::Timeout);
    }

    #[test]
    fn parse_reply_callback_name_non_utf8_is_none() {
        // QUERY_CALLBACK reply whose name bytes are NOT valid UTF-8. 0xFF and
        // 0xFE are never legal UTF-8 bytes (UTF-8 uses 0x00–0xF4), so
        // String::from_utf8 fails ⇒ .ok() ⇒ None. parse_callback_name
        // deliberately uses .ok() (NOT from_utf8_lossy) so a corrupt name is
        // treated as ABSENT (None) rather than replaced with U+FFFD.
        // Layout: [0x51][0x02][index=1][0xFF][0xFE][0x00 NUL terminator].
        // Item edge case #7.
        let response = [0x51, 0x02, 1, 0xFF, 0xFE, 0x00];
        assert_eq!(
            parse_reply(&response),
            CommandResponse::CallbackName { index: 1, name: None }
        );
    }

    #[test]
    fn parse_reply_truncated_info_defaults_board_rules_false() {
        // Truncated QUERY_INFO reply: only 4 bytes, so board_rules_present
        // (offset [5]) AND callback_count (offset [4]) are absent. Both must
        // default via .get(i).copied().unwrap_or(0): callback_count ⇒ 0,
        // board_rules_present ⇒ 0 ⇒ false (the != 0 coercion). The PRESENT
        // fields (proto_ver=2, feature_flags=0x03) must still decode normally.
        // CRITICAL: this MUST NOT panic on a bare response[5] index — the
        // defensive .get() is the guard this test certifies. Item edge case #8.
        let response = [0x51, 0x01, 2, 0x03];
        assert_eq!(
            parse_reply(&response),
            CommandResponse::Info {
                proto_ver: 2,
                feature_flags: 0x03,
                callback_count: 0,
                board_rules_present: false,
            }
        );
    }
```

#### §C — key pattern notes

```rust
// === WHY ONE assert_eq! PER TEST (not a parametrized table) ===
//   S1's 6 tests each use a single assert_eq! with an inline byte array. The 8
//   new tests mirror that EXACTLY for consistency and so each failure points at
//   one item bullet. A single big test would obscure which edge case regressed.
//   (This crate uses no test-macro/parametrize crate; plain #[test] fns only.)


// === WHY CASE 4 ([0x51]) YIELDS Timeout, NOT a typed response ===
//   parse_typed_reply's FIRST line is `let cmd_echo = response.get(1)...unwrap_or(0)`.
//   On a len-1 slice, .get(1) is None ⇒ cmd_echo defaults to 0. 0 matches no
//   CMD_* arm, so the unknown-cmd-echo `_ => Timeout` arm fires. So a marker-only
//   reply is Timeout — the defensive default. This is correct and certifies the
//   .get(1) guard. Do NOT "fix" this to return a typed response; Timeout is right.


// === WHY CASE 7 USES 0xFF/0xFE (not e.g. 0x80 alone) ===
//   0x80 is a UTF-8 continuation byte that is invalid ALONE, but a reviewer might
//   misremember. 0xFF and 0xFE are NEVER legal UTF-8 bytes under any circumstance
//   (the UTF-8 spec excludes 0xF8–0xFF entirely; 0xFE/0xFF can never start or
//   continue a sequence). So [0xFF, 0xFE] is unambiguously invalid — String::
//   from_utf8 returns Err, .ok() ⇒ None. This makes the test's intent unmistakable.


// === WHY CASE 8 ASSERTS callback_count: 0 TOO (not just board_rules) ===
//   The item names only board_rules_present, but the same truncation (len 4) also
//   omits callback_count (offset [4]). The defensive .get(4) defaults it to 0.
//   Asserting BOTH the present fields (proto_ver=2, feature_flags=3 decode
//   normally) AND the defaulted fields (callback_count=0, board_rules=false)
//   gives the strongest certification that .get() defaults work without losing
//   the real data. If the test asserted only board_rules, a regression that also
//   zeroed proto_ver would slip through.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - append: "8 #[test] fns at END of #[cfg(test)] mod tests, immediately after
             the fn parse_reply_ack_apply_host_context_rejected"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps.

PUBLIC API SURFACE:
  - adds:    "(nothing — tests are cfg(test); no public symbol added)"
  - unchanged: "all lib.rs public types (HostOs, RunCommand, CommandResponse,
                RunParameters), parse_cli_args, run() signature/body, all core::
                pub re-exports, parse_reply/parse_typed_reply/parse_callback_name
                (pub(crate)/private), all QmkError variants/Display"

DOWNSTREAM CONSUMERS (do NOT implement now — listed for awareness):
  - P1.M3.T3.S1 (run dispatch): "migrates run()'s typed placeholder to call
        build_typed_payload → send_raw_report → core::parse_reply. The 8 tests
        landed here mean every reply shape parse_reply can return is pinned, so
        P1.M3.T3 wires run() against a known-good decoder."

SCOPE BOUNDARY:
  - ONLY src/core.rs is modified, and ONLY by appending the 8 tests. Do NOT:
    * re-implement or "extend" parse_reply/parse_typed_reply/parse_callback_name
      (they are complete & correct — UNLESS a test fails, then minimal fix only).
    * edit the mod-tests `use` line (CommandResponse is already imported).
    * add a 9th "bonus" test or refactor S1's 6 typed tests.
    * touch lib.rs, error.rs, main.rs, or Cargo.toml.
    * add parse_reply to lib.rs's `pub use core::{...}` (it's pub(crate), internal).
    * change the #[allow(dead_code)] on parse_reply (live caller is P1.M3.T3).
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate — MUST compile with ZERO warnings.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.
# (No new code outside cfg(test) ⇒ no new warnings possible.)

# Lint (default clippy — no clippy.toml exists).
cargo clippy --lib 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors. Byte-array literals + assert_eq! are clippy-clean.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run all 14 parse_reply tests (S1's 6 + S2's 8) in isolation first.
cargo test --lib parse_reply -- --nocapture
# Expected: 14 passed (6 typed happy paths + 8 legacy/timeout/edge cases).

# Confirm each of the 8 NEW tests individually.
cargo test --lib parse_reply_empty_slice_is_timeout -- --nocapture
cargo test --lib parse_reply_legacy_zero_is_no_match -- --nocapture
cargo test --lib parse_reply_legacy_one_is_matched -- --nocapture
cargo test --lib parse_reply_typed_marker_only_is_timeout -- --nocapture
cargo test --lib parse_reply_unknown_cmd_echo_is_timeout -- --nocapture
cargo test --lib parse_reply_unknown_marker_is_timeout -- --nocapture
cargo test --lib parse_reply_callback_name_non_utf8_is_none -- --nocapture
cargo test --lib parse_reply_truncated_info_defaults_board_rules_false -- --nocapture

# Full lib test suite (lib.rs unit tests + core.rs unit tests).
cargo test --lib
# Expected: "test result: ok. 57 passed; 0 failed; 0 ignored; ..." (49 baseline + 8 new).

# SAFETY-NET SIGNAL: if any of the 8 new tests FAILS or PANICS, it reveals an S1
#   bug in parse_reply/parse_typed_reply/parse_callback_name. Compare the actual
#   CommandResponse to research notes.md F4/F5, read the failing line, apply the
#   MINIMAL fix, re-run. (Research confirms all 8 should PASS — a fix is not expected.)

# Sanity: S1's tests + all other tests still pass unchanged.
cargo test --lib parse_reply_info -- --nocapture        # S1 Info tests still green
cargo test --lib build_typed_payload -- --nocapture     # build_typed_payload_* still green
cargo test --lib                                        # whole crate green
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
parse_reply is a pure function (&[u8] -> CommandResponse) with no HID I/O, no CLI
surface, and no runtime call site yet (run()'s typed arms still return a Timeout
placeholder; migration is P1.M3.T3). There is no live-hardware or runtime path to
exercise. The exhaustive unit tests in Level 2 — which assert the exact
CommandResponse for each of the 8 legacy/timeout/edge-case byte layouts mandated
by the item + firmware_wire_contract.md §Reply Disambiguation — ARE the
end-to-end verification for this task. (The firmware itself does not implement
typed commands yet — findings_and_risks.md F4 — so even a hardware test would
only see Timeout/Legacy; synthetic byte buffers are the designed test vehicle,
per PRD §10.2.)
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the 8 new tests are present and named per the item mapping (research F5):
grep -nE "fn parse_reply_(empty_slice_is_timeout|legacy_zero_is_no_match|legacy_one_is_matched|typed_marker_only_is_timeout|unknown_cmd_echo_is_timeout|unknown_marker_is_timeout|callback_name_non_utf8_is_none|truncated_info_defaults_board_rules_false)" src/core.rs
# Expected: exactly 8 matches.

# Confirm the parse_reply_* family is now 14 (S1's 6 + S2's 8):
grep -cE "fn parse_reply_" src/core.rs
# Expected: 14 (6 typed happy paths + 8 legacy/timeout/edge cases).

# Confirm NO function bodies were touched (safety-net should be a no-op):
# parse_reply/parse_typed_reply/parse_callback_name still each appear exactly once.
grep -nE "fn (parse_reply|parse_typed_reply|parse_callback_name)\b" src/core.rs
# Expected: 3 matches (one definition each — unchanged by this task unless the
#   safety net fired, in which case the implementer documented the minimal fix).

# Cross-check the edge-case expectations against the canonical contract by eye:
grep -nE "0x51|Timeout|Legacy|board_rules_present|QUERY_CALLBACK|disambiguation" \
  plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md
# (RESPONSE_MARKER=0x51, the response[0] disambiguation table, NUL-padded names —
#  all should be consistent with the 8 test assertions above.)

# Full-crate final gate (the number that proves the task done):
cargo test --lib 2>&1 | tail -3
# Expected: "test result: ok. 57 passed; 0 failed; ..."
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1 passed: `cargo build` → zero warnings.
- [ ] Level 1 passed: `cargo clippy --lib` → zero warnings.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test --lib` → **57 passed, 0 failed** (49 + 8).
- [ ] The 8 new `parse_reply_*` tests pass individually (`cargo test --lib
      parse_reply` ⇒ 14 passed).

### Feature Validation
- [ ] The 8 mandated edge-case tests exist in core.rs's `mod tests`, appended
      after `parse_reply_ack_apply_host_context_rejected`, named exactly per
      research F5.
- [ ] Each test asserts the exact `CommandResponse` from research F4/F5.
- [ ] **No test panicked** (certifies the defensive `.get()` guards in
      `parse_typed_reply` and the `3.min(response.len())` slice clamp).
- [ ] Case 7 returns `name: None` (certifies `String::from_utf8(...).ok()`, NOT
      `from_utf8_lossy`).
- [ ] Case 8 returns `board_rules_present: false` AND `callback_count: 0` while
      `proto_ver`/`feature_flags` decode normally (certifies `.get()` defaults
      without losing present fields).
- [ ] **Safety net honored:** if any test failed, the minimal fix was applied to
      `parse_reply`/`parse_typed_reply`/`parse_callback_name` and documented
      (research F4 predicts NO fix is needed).

### Code Quality Validation
- [ ] Follows core.rs conventions: descriptive snake_case tests with NO `test_`
      prefix; `parse_reply_*` prefix; one `assert_eq!` per test; `//` comment
      citing the item case + wire shape (mirrors S1's 6 tests).
- [ ] The 14 `parse_reply_*` tests form a contiguous family at the end of
      `mod tests`.
- [ ] No file other than `src/core.rs` modified.
- [ ] Did NOT re-implement or extend the function bodies (pure test addition —
      unless the safety net fired).
- [ ] Did NOT edit the test-module `use` line (CommandResponse already imported).

### Documentation & Deployment
- [ ] Each test's `//` comment cites the item edge-case number and the
      `firmware_wire_contract.md` section it certifies.
- [ ] No new environment variables or config.
- [ ] No README/PRD/Cargo.toml/lib.rs change (contract DOCS = "none" — internal
      `pub(crate)` function, no user-facing surface; tests are `cfg(test)`).

---

## Anti-Patterns to Avoid

- ❌ Don't re-implement or "extend" `parse_reply`/`parse_typed_reply`/
      `parse_callback_name` — they are **already complete and correct** (S1
      landed them; verified at src/core.rs:409/426/462). This task ADDS 8 tests.
      The function bodies are touched ONLY if a test fails (safety net).
- ❌ Don't edit the test-module `use` line — `CommandResponse` was added by S1
      (`use crate::{CommandResponse, HostOs, RunCommand};`). The 8 new tests reach
      it via that line and `parse_reply` via `use super::*`. No import edit needed.
- ❌ Don't add a 9th "bonus" test, a parametrized table, or a helper fn — 8 tests,
      1:1 with the item bullets, each a single `assert_eq!` (matches S1's style).
- ❌ Don't refactor or alter S1's 6 typed happy-path tests — append AFTER them.
- ❌ Don't expect case 4 (`[0x51]`) to return a typed response — it correctly
      returns `Timeout` (cmd_echo `.get(1)` defaults to 0 ⇒ unknown ⇒ Timeout).
      Asserting `Timeout` is RIGHT; do not "fix" the function to return typed.
- ❌ Don't use `from_utf8_lossy` semantics for case 7 — the contract is `.ok()`
      (None for invalid UTF-8). Case 7 MUST assert `name: None`, NOT
      `Some("\u{FFFD}...")`. If it returns Some, the function regressed to lossy.
- ❌ Don't assert ONLY `board_rules_present` in case 8 — assert the FULL `Info`
      (proto_ver=2, feature_flags=0x03 present; callback_count=0, board_rules=false
      defaulted). A partial assertion would miss a regression that also zeroed a
      present field.
- ❌ Don't anchor the append by line number — anchor by the fn NAME
      `parse_reply_ack_apply_host_context_rejected` (S1's last test). Line numbers
      drift. Re-read core.rs's `mod tests` tail immediately before editing.
- ❌ Don't touch `lib.rs`, `error.rs`, `main.rs`, or `Cargo.toml` — `CommandResponse`
      already exists with all derives; no new types/constants/deps/tests-in-other-
      files are needed. Tests live in `core.rs` alongside the function they test.
- ❌ Don't add `parse_reply` to `lib.rs`'s `pub use core::{...}` — it's `pub(crate)`,
      internal (reached by `run()` via `core::parse_reply`). Tests reach it via
      `use super::*`, not via the public re-export.
- ❌ Don't remove `#[allow(dead_code)]` from `parse_reply` — its live caller is
      P1.M3.T3 (run dispatch), not this task. Removing it now would warn.
- ❌ Don't panic on a failing test — that IS the safety net firing. Read the
      actual vs expected `CommandResponse` (research F4/F5), apply the MINIMAL
      fix to the offending function line, document it, re-run. (Research predicts
      this branch is NOT taken.)

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is **8 `assert_eq!` tests given verbatim** (copy-paste ready), each with its exact
byte input and exact expected `CommandResponse` pinned to a byte-for-byte trace of
the **actual, already-merged** `parse_reply`/`parse_typed_reply`/`parse_callback_name`
code (research `notes.md` F4 — all 8 pass, zero bugs found). There is no function
code to write (unless the safety net fires, which research says it won't). The
naming convention, insertion anchor, `use`-line state, baseline test count (49,
**verified by running `cargo test --lib`**), and the +8⇒57 total are all confirmed
against the live repo. No file other than `src/core.rs` is touched; no public API
changes; no new deps; no edit to S1's 6 tests. The only residual risk — an S1 bug
that the trace missed — is exactly what the 8 tests are designed to catch, and the
PRP gives the implementer the diagnostic oracle (F4/F5) plus the minimal-fix
instruction to resolve it in one pass.