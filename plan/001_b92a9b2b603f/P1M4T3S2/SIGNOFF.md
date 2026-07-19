# v0.3.0 Invariant Sign-Off — qmk_notifier

**Date**: 2025-12-08
**HEAD**: [`0c3b4571b808043159a8d26e183fb421f14cfb61`] — Update README for v0.3.0 typed transport
**Crate version**: 0.3.0 (Cargo.toml)
**Verifier**: P1.M4.T3.S2 (final cross-document consistency gate)
**PRD reference**: §14 Key Invariants (8 items)

## Verdict

**ALL PASS** — all 8 PRD §14 Key Invariants hold in the final v0.3.0 tree; `cargo test` (65 passed, 0 failed) and `cargo clippy --all-targets -- -D warnings` (zero warnings, exit 0) are both green; the crate matches the canonical firmware wire contract byte-for-byte. No source-code edits were required or made.

## Per-invariant evidence

| # | Invariant (PRD §14) | Verdict | Evidence (file:symbol / command) |
|---|---------------------|---------|----------------------------------|
| 1 | Magic header 0x81 0x9F; ETX 0x03 | PASS | `core.rs:312-313` `burst_to_one` sets `request_data[1] = 0x81; request_data[2] = 0x9F;`. `ETX_TERMINATOR_BYTE: u8 = 0x03` (`core.rs:47`); pushed on the typed path (`core.rs:486` in `build_command_data`) and on the SendMessage path (`core.rs:434`). Both payload paths append ETX. |
| 2 | REPORT_LENGTH=32; buffer 33 | PASS | `core.rs:10` `pub const REPORT_LENGTH: usize = 32;`. Three 33-byte hidapi buffers: `core.rs:311` `let mut request_data = [0u8; REPORT_LENGTH + 1];`, `core.rs:345` `read_buf`, `core.rs:368` `drain_buf`. `core.rs:98` `const PAYLOAD_PER_REPORT: usize = REPORT_LENGTH - 2;` (= 30). |
| 3 | VID/PID None ⇒ match any | PASS | `core.rs:702` `fn device_matches`: `dev_usage_page == usage_page && dev_usage == usage && vendor_id.is_none_or(\|v\| dev_vendor_id == v) && product_id.is_none_or(\|p\| dev_product_id == p)` (`core.rs:713-715`). usage_page/usage use strict `==` (always required); VID/PID use `is_none_or` (None ⇒ match any). |
| 4 | MatchKey cache; invalidate on fail; partial never retried | PASS | `core.rs:573` `#[derive(Clone, Copy, PartialEq, Eq, Debug)] struct MatchKey`. `core.rs:263` `*cache = None;` in `try_send_once` (invalidated when `failed > 0`). `core.rs:114` `const SEND_RETRIES: usize = 1;`. Retry loop `core.rs:164`: `(SendOutcome::Partial { succeeded, failed }, _) => return Err(QmkError::PartialSendError { succeeded, failed });` (`core.rs:167-168`) — returns IMMEDIATELY, never retried; only `TotalFailure` with `attempt < SEND_RETRIES` (`core.rs:170`) continues. `error.rs:21` `PartialSendError { succeeded: usize, failed: usize }`. |
| 5 | Typed commands reuse framing+cache | PASS | `lib.rs:410` `fn run`: exactly 2 match arms — `ListDevices` (no send) and `command @ (SendMessage \| QueryInfo \| QueryCallback \| SetOs \| ApplyHostContext {.. })` (`lib.rs:423-427`) sharing ONE path: `build_payload` (`lib.rs:444`) → `send_raw_report` (`lib.rs:446`) → `core::parse_reply` (`lib.rs:456`). `build_payload` (`lib.rs:370`) delegates to `core::build_command_data` (`core.rs:427`), which emits `[CMD_DISCRIMINATOR(0xF0)][cmd_id][args][ETX]` for typed variants and appends ETX for SendMessage. Same `MatchKey`, same cache, same `burst_to_one`. |
| 6 | Reply disambiguates 0x51/0/1/timeout | PASS | `core.rs:508` `fn parse_reply`: `if response.is_empty() { return Timeout; }` then `match response[0] { RESPONSE_MARKER => parse_typed_reply(response), 0 => Legacy{matched:false}, 1 => Legacy{matched:true}, _ => Timeout }` (`core.rs:513-517`). All four cases distinct; `_` and empty ⇒ Timeout. `parse_typed_reply` (`core.rs:524`) decodes by `response[1]` cmd echo, reading every byte with `.get(i).copied().unwrap_or(0)`. |
| 7 | Transport-only (no window/pattern/rule) | PASS | `grep -rniE 'window\|foreground\|regex\|pattern\|rule\|detect\|focus\|toml\|serde' src/` returns ONLY `HostOs::Windows` (OS enum variant, `core.rs:925`, `lib.rs:71`) and doc-comment prose ("window string" at `lib.rs:20` describing what the CALLER passes). NO detection/matcher/rule/config logic. `grep 'toml\|serde\|regex\|glob' Cargo.toml` ⇒ no matches (deps are `clap` + `hidapi` only). |
| 8 | Firmware wins on disagreement | PASS | `core.rs` consts match `firmware_wire_contract.md` §Constants exactly: `CMD_DISCRIMINATOR=0xF0` (c:27), `RESPONSE_MARKER=0x51` (c:29), `CMD_QUERY_INFO=0x01` (c:31), `CMD_QUERY_CALLBACK=0x02` (c:32), `CMD_SET_OS=0x03` (c:33), `CMD_APPLY_HOST_CONTEXT=0x05` (c:34). `HostOs` discriminants (`lib.rs:67-75`): Unsure=0, Linux=1, Windows=2, Macos=3, Ios=4 — match firmware `os_variant_t`. `parse_typed_reply` offsets match §Field Definitions (QUERY_INFO `[2]=proto_ver,[3]=feature_flags,[4]=callback_count,[5]=board_rules_present`; QUERY_CALLBACK `[2]=index,[3..]=name`; SET_OS/APPLY_HOST_CONTEXT `[2]=ack`). **No drift.** |

## Test gate

- `cargo test` →
  ```
  test result: ok. 65 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.03s
  ```
  (lib unit tests) plus
  ```
  test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
  ```
  (src/main.rs unittests) and
  ```
  test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
  ```
  (doc-tests). All three test binaries green; 0 failures total. Hardware-free on the critical path (dispatch tests use bogus VID=0xDEAD/PID=0xBEEF ⇒ deterministic DeviceNotFound; parse/match tests are pure unit tests over byte arrays).

- `cargo clippy --all-targets -- -D warnings` →
  ```
      Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.03s
  ```
  Exit code 0. Zero warnings (`-D warnings` makes any warning a hard error, so a clean finish = zero warnings). clippy 0.1.92 (ded5c06cf2 2025-12-08).

## Drift log

| finding | severity | action taken |
|---------|----------|--------------|
| PRP research notes F2 ("build_command_data naming nit") is OBSOLETE: the PRP expected the real fn to be `build_typed_payload` with a stale `build_command_data` comment in lib.rs. In the actual tree, commit `bdd3343 "Unify payload builder as build_command_data"` RENAMED the function the other direction — `build_command_data` is now the REAL unified builder (`core.rs:427`) and the lib.rs doc comments referencing `core::build_command_data` are CORRECT. No `build_typed_payload` symbol remains. | minor (reconciliation — NOT a code defect) | No code edit. Recorded here for audit accuracy. The optional F2 comment-typo fix does not apply because the comments are already correct. |

**No code drift found.** All 8 invariants hold; the crate matches the firmware wire contract; the only entry above is a research-notes-vs-repo reconciliation (the repo renamed the builder to match the contract's generic name, making the anticipated nit moot).

## Cross-document consistency (README ↔ code ↔ firmware)

- README (post-S1) Technical Details byte sequence `[0x81][0x9F][0xF0][cmd_id][args…][0x03]` (`README.md:155`) matches core.rs framing (`request_data[1]=0x81`, `[2]=0x9F`, `CMD_DISCRIMINATOR=0xF0`, `ETX_TERMINATOR_BYTE=0x03`). README line 158 correctly lists command IDs `0x01` QueryInfo, `0x02` QueryCallback, `0x03` SetOs, `0x05` ApplyHostContext. README line 100/151 correctly state ETX = `0x03`; line 149/160 correctly state 32 logical bytes / 33-byte hidapi buffer. **CONFIRMED** — no byte-sequence typo to fix.
- core.rs consts match `firmware_wire_contract.md` §Constants. **CONFIRMED** (see invariant 8 row).
- HostOs discriminants match firmware `os_variant_t`. **CONFIRMED**.

## Scope confirmation

- `git status --porcelain`: `?? plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md` (this report) is the ONLY file added by this task.
- `git diff --stat src/` ⇒ empty (no source-code edits; the F2 nit was obsolete so no comment fix was warranted).
- `PRD.md`, `.gitignore`, `Cargo.toml`, `Cargo.lock`, `README.md` body ⇒ untouched by this task.
- NOTE: `plan/001_b92a9b2b603f/tasks.json` shows ` M` in `git status`, but that diff is the orchestrator's own status bookkeeping (`Researching` → `Ready`); this task never invoked `tsk` or any pipeline command and did not modify it.

## Sign-off

All 8 PRD §14 Key Invariants are preserved in the v0.3.0 codebase. The crate is
internally consistent and consistent with the canonical firmware wire contract
(`firmware_wire_contract.md`). `cargo test` (65 passed, 0 failed) and
`cargo clippy --all-targets -- -D warnings` (0 warnings) are green. Ready for the
v0.3.0 tag.

— P1.M4.T3.S2