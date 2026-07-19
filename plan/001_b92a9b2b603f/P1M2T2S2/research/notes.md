# Research Notes — P1.M2.T2.S2

## Add legacy/timeout/edge-case handling to `parse_reply` and finalize tests

---

## F1 — S1 is already MERGED (not "in progress")

The `parallel_execution_context` banner says S1 is "currently being implemented."
At the time of this PRP's research that is **no longer true** — S1's code is
**already in `src/core.rs`** (verified by reading the file):

- `parse_reply` at `src/core.rs:409` — `pub(crate)`, `#[allow(dead_code)]`,
  `fn parse_reply(response: &[u8]) -> crate::CommandResponse`. **COMPLETE body**
  (all 4 top-level arms: `RESPONSE_MARKER`, `0`, `1`, `_`).
- `parse_typed_reply` at `src/core.rs:426` — private, decodes by `response[1]`
  cmd echo via `.get(i).copied().unwrap_or(0)` on every field.
- `parse_callback_name` at `src/core.rs:462` — private, NUL-truncated slice +
  `String::from_utf8(...).ok()`.
- `RESPONSE_MARKER`'s `#[allow(dead_code)]` already **removed** (S1 Task 2 done).
- The test-module `use` line already reads `use crate::{CommandResponse, HostOs,
  RunCommand};` (S1 added `CommandResponse`).
- The 6 S1 typed-path tests are present (`parse_reply_info_reply`,
  `parse_reply_info_board_rules_absent`, `parse_reply_callback_name_named`,
  `parse_reply_callback_name_unnamed`, `parse_reply_ack_set_os_applied`,
  `parse_reply_ack_apply_host_context_rejected`).

**Implication for S2:** the function body is COMPLETE and correct. S2 is **purely
additive test coverage** — append the legacy/timeout/edge-case tests. There is no
function code to write and (per the trace in F4) no bug to fix. This matches the
S1 PRP's reconciled-scope banner exactly: *"S2 only extends test coverage."*

## F2 — Baseline test count (VERIFIED by running the suite)

```
grep -c "#\[test\]" src/core.rs   →  29   (23 pre-M2 + 6 S1 typed tests)
grep -c "#\[test\]" src/lib.rs    →  20
cargo test --lib                  →  "test result: ok. 49 passed; 0 failed"
cargo build                       →  zero warnings
```

The S1 PRP predicted 45 (it assumed lib.rs had 16 tests); lib.rs actually has
20, so the real baseline is **49**. S2 adds **8** tests (one per item-mandated
edge case, see F4) ⇒ **expected final total: 57 passed, 0 failed.**

## F3 — Conventions to follow (VERIFIED in the existing core.rs test block)

- Test fns are **descriptive snake_case with NO `test_` prefix**. Existing
  reply-parser tests are all `parse_reply_*` (e.g. `parse_reply_info_reply`,
  `parse_reply_callback_name_unnamed`). The 8 new tests use the SAME
  `parse_reply_*` prefix so the whole matrix sorts together.
- Test module header is already `use super::*;` + `use crate::{CommandResponse,
  HostOs, RunCommand};`. `parse_reply` reaches the tests via `use super::*`;
  `CommandResponse` reaches them via the crate use. **No edit to the `use` line
  is needed** (S1 already added `CommandResponse`).
- Tests assert with `assert_eq!(parse_reply(&[...]), CommandResponse::Variant {...})`
  — byte-array literals on the left, the exact expected `CommandResponse` on the
  right. `CommandResponse` derives `PartialEq, Eq` (lib.rs), so this compiles.
- **Placement:** append at the END of `#[cfg(test)] mod tests`, immediately after
  the current last test `parse_reply_ack_apply_host_context_rejected` (keeps the
  whole `parse_reply_*` family contiguous). Anchor by fn name, not line number.
- No `rustfmt.toml` / no `clippy.toml` ⇒ default config. Byte-array literals and
  `assert_eq!` are clippy/fmt-clean under defaults.

## F4 — The 8 edge cases traced through the REAL S1 code (no bug found)

Each row below traces the item-mandated input byte-for-byte through the actual
`src/core.rs` `parse_reply` / `parse_typed_reply` / `parse_callback_name`
bodies. **All 8 pass against the current implementation** — there is no S1 bug
to fix. S2's job is to pin these 8 behaviors as regression tests (the "safety
net" the item names).

### Case 1 — empty slice `&[]` ⇒ Timeout
```
parse_reply(&[]):
  response.is_empty() == true  →  return CommandResponse::Timeout
```
**Expected:** `CommandResponse::Timeout`. **Panics?** No (`is_empty()` guards).

### Case 2 — `response[0] == 0` ⇒ Legacy{matched:false}
```
parse_reply(&[0]):
  not empty; match response[0]=0  →  CommandResponse::Legacy { matched: false }
```
**Expected:** `CommandResponse::Legacy { matched: false }`.

### Case 3 — `response[0] == 1` ⇒ Legacy{matched:true}
```
parse_reply(&[1]):
  match response[0]=1  →  CommandResponse::Legacy { matched: true }
```
**Expected:** `CommandResponse::Legacy { matched: true }`.

### Case 4 — `response[0] == 0x51` but too short (len < 3) ⇒ no panic
Representative input: `[0x51]` (len 1, the most degenerate "too short" form).
```
parse_reply(&[0x51]):
  match response[0]=0x51  →  parse_typed_reply(&[0x51]):
    cmd_echo = response.get(1).copied().unwrap_or(0)   // .get(1) on len-1 ⇒ None ⇒ 0
    match 0: 0 ≠ CMD_QUERY_INFO(1)/QUERY_CALLBACK(2)/SET_OS(3)/APPLY_HOST_CONTEXT(5)
      _  →  CommandResponse::Timeout
```
**Expected:** `CommandResponse::Timeout`. **Panics?** No — `response.get(1)` is
the defensive guard (a bare `response[1]` would panic on a len-1 slice). This
exercises the cmd-echo `.get(1)` default path.

### Case 5 — `response[0]==0x51, response[1]==0xFF` (unknown cmd echo) ⇒ Timeout
```
parse_reply(&[0x51, 0xFF]):
  → parse_typed_reply: cmd_echo = response.get(1)=0xFF
    match 0xFF: unknown  →  _  ⇒  CommandResponse::Timeout
```
**Expected:** `CommandResponse::Timeout`. (0x04 VIA-reserved is also "unknown" to
this crate; 0xFF is the item's chosen representative.)

### Case 6 — `response[0] == 0x42` (unknown marker) ⇒ Timeout
```
parse_reply(&[0x42]):
  match response[0]=0x42: not 0x51/0/1  →  _  ⇒  CommandResponse::Timeout
```
**Expected:** `CommandResponse::Timeout`. Exercises the top-level `_` arm.

### Case 7 — `response[0]==0x51, response[1]==0x02`, non-UTF8 name ⇒ name None
Input: `[0x51, 0x02, 1, 0xFF, 0xFE, 0x00]` (index=1; name bytes 0xFF 0xFE then NUL).
```
parse_reply([0x51,0x02,1,0xFF,0xFE,0x00]):
  → parse_typed_reply: cmd_echo=0x02 (CMD_QUERY_CALLBACK)
    index = response.get(2)=1
    name = parse_callback_name(&response[3.min(6)..]) = parse_callback_name(&[0xFF,0xFE,0x00])
      end = position(0x00)=2  →  name_bytes=&[0xFF,0xFE]  (non-empty)
      String::from_utf8(vec![0xFF,0xFE]).ok()
        0xFF & 0xFE are NEVER legal UTF-8 bytes (UTF-8 range is 0x00–0xF4)
        ⇒ from_utf8 returns Err  ⇒ .ok() ⇒ None
    ⇒ CommandResponse::CallbackName { index:1, name:None }
```
**Expected:** `CommandResponse::CallbackName { index: 1, name: None }`.
**Panics?** No. **Why None, not Some("�"):** S1 uses `.ok()` (per the item
contract), NOT `from_utf8_lossy` — a corrupt name is treated as absent.

### Case 8 — truncated Info reply (len=4, missing board_rules_present) ⇒ false
Input: `[0x51, 0x01, 2, 0x03]` (proto_ver=2, feature_flags=0x03; no callback_count, no board_rules byte).
```
parse_reply([0x51,0x01,2,0x03]):
  → parse_typed_reply: cmd_echo=0x01 (CMD_QUERY_INFO)
    proto_ver           = response.get(2)=2
    feature_flags       = response.get(3)=0x03
    callback_count      = response.get(4)  // .get(4) on len-4 ⇒ None ⇒ 0
    board_rules_present = response.get(5).copied().unwrap_or(0) != 0
                        // .get(5) on len-4 ⇒ None ⇒ 0 ⇒ false
    ⇒ CommandResponse::Info { proto_ver:2, feature_flags:3, callback_count:0, board_rules_present:false }
```
**Expected:** `CommandResponse::Info { proto_ver: 2, feature_flags: 0x03,
callback_count: 0, board_rules_present: false }`. **Panics?** No — the `.get()`
defaults are the guard. The two PRESENT fields still decode normally.

---

## F5 — Test→item bullet mapping (1:1, 8 tests)

| # | item bullet | test fn name | expected `CommandResponse` |
|---|---|---|---|
| 1 | empty `&[]` ⇒ Timeout | `parse_reply_empty_slice_is_timeout` | `Timeout` |
| 2 | `response[0]==0` ⇒ Legacy{false} | `parse_reply_legacy_zero_is_no_match` | `Legacy{matched:false}` |
| 3 | `response[0]==1` ⇒ Legacy{true} | `parse_reply_legacy_one_is_matched` | `Legacy{matched:true}` |
| 4 | `0x51` too short (len<3) ⇒ no panic | `parse_reply_typed_marker_only_is_timeout` | `Timeout` |
| 5 | `0x51,0xFF` unknown cmd echo ⇒ Timeout | `parse_reply_unknown_cmd_echo_is_timeout` | `Timeout` |
| 6 | `0x42` unknown marker ⇒ Timeout | `parse_reply_unknown_marker_is_timeout` | `Timeout` |
| 7 | `0x51,0x02,<idx>,non-UTF8…` ⇒ name None | `parse_reply_callback_name_non_utf8_is_none` | `CallbackName{index:1,name:None}` |
| 8 | truncated Info len=4 ⇒ board_rules false | `parse_reply_truncated_info_defaults_board_rules_false` | `Info{proto_ver:2,feature_flags:3,callback_count:0,board_rules_present:false}` |

## F6 — "Safety net" semantics (item contract)

The item says: *"If any test reveals a bug in S1's implementation, fix it here.
This subtask is the safety net."* F4's trace proves **no bug exists** against the
current code, so the deliverable is **purely the 8 tests**. The PRP nonetheless
instructs the implementer to (a) append the tests, (b) run them, (c) if any fail,
diagnose against the F4 trace and fix the offending line in `parse_reply` /
`parse_typed_reply` / `parse_callback_name`. The expected values in F4/F5 are the
diagnostic oracle.

## F7 — Scope boundaries (do NOT exceed)

- ONLY `src/core.rs` is modified, and ONLY by appending 8 `#[test]` fns at the
  end of `mod tests`. No `lib.rs`, no `error.rs`, no `main.rs`, no `Cargo.toml`.
- Do NOT touch the `parse_reply` / `parse_typed_reply` / `parse_callback_name`
  bodies UNLESS a test fails (per F6). They are correct as-is.
- Do NOT add a 9th "bonus" test or refactor existing tests. 8 tests, 1:1 with the
  item bullets.
- Do NOT remove or alter S1's 6 typed-path tests.
- Do NOT edit the test-module `use` line — `CommandResponse` is already imported.
- Do NOT add `parse_reply` to `lib.rs`'s `pub use core::{...}` (it's `pub(crate)`).
- Do NOT change the `#[allow(dead_code)]` on `parse_reply` — its live caller lands
  in P1.M3.T3 (run dispatch), not here.

## F8 — Downstream consumer (awareness only)

`parse_reply` is consumed by `run()`'s typed dispatch in **P1.M3.T3.S1**. Until
then it has no live caller (hence `#[allow(dead_code)]`). The exhaustive test
matrix this task lands means P1.M3.T3 wires `run()` against a **known-good**
decoder — a regression in any of the 8 edge cases would be caught here, not in
integration. This is exactly the "safety net" role the item describes.