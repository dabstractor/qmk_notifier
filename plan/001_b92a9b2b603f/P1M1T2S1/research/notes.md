# Research Notes — P1.M1.T2.S1: Add typed-command constants to core.rs

## Task recap
Add 7 constants to `src/core.rs` (6 `pub(crate)`, 1 private) defining the
v0.3.0 typed-command wire vocabulary, plus one unit test pinning them to the
firmware contract. No dependencies; no public API change.

The exact constant block is given verbatim in the work-item contract. This
research focused on (a) the gotcha that determines whether the literal snippet
compiles warning-free, and (b) the placement/test/visibility conventions.

## Canonical source of truth
`plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md` §Constants
(mirrors firmware `notifier.h`):

| Symbol in firmware              | Value | This crate's constant         |
|---------------------------------|-------|-------------------------------|
| NOTIFY_CMD_DISCRIMINATOR        | 0xF0  | CMD_DISCRIMINATOR             |
| NOTIFY_RESPONSE_MARKER          | 0x51  | RESPONSE_MARKER               |
| NOTIFY_CMD_QUERY_INFO           | 0x01  | CMD_QUERY_INFO                |
| NOTIFY_CMD_QUERY_CALLBACK       | 0x02  | CMD_QUERY_CALLBACK            |
| NOTIFY_CMD_SET_OS               | 0x03  | CMD_SET_OS                    |
| NOTIFY_CMD_APPLY_HOST_CONTEXT   | 0x05  | CMD_APPLY_HOST_CONTEXT        |
| (host-side, not in notifier.h)  | 1000  | REPLY_READ_TIMEOUT_MS (i32)   |

REPLY_READ_TIMEOUT_MS is a host-side choice (bounded blocking read of the first
reply after a burst), NOT a firmware value — so the test asserts the documented
invariant (`> 0`, unlike the drain loop's non-blocking `read_timeout(0)`) rather
than equality to a firmware constant.

## CRITICAL GOTCHA — dead_code warnings (empirically verified)

The work-item contract shows the constants with NO `#[allow(dead_code)]`. That
literal snippet produces 7 `dead_code` warnings, which violates the project-wide
"zero warnings" success bar shared with the sibling PRPs (P1.M1.T1.S3 etc.).

### Why they're dead
No non-test code consumes these constants yet:
- `build_command_data` → P1.M2.T1 (not yet implemented)
- `parse_reply`         → P1.M2.T2 (not yet implemented)
- `burst_to_one` evolve → P1.M3.T1 (not yet implemented)

### Experiments (fresh /tmp cargo projects, edition 2021)

1. Bare constants, no test, `cargo build` → ALL warn (pub(crate) AND private):
   `CMD_DISCRIMINATOR`, `RESPONSE_MARKER`, `REPLY_READ_TIMEOUT_MS`,
   `PRIVATE_UNUSED`. **pub(crate) does NOT exempt an item from dead_code.**

2. Constants referenced ONLY inside `#[cfg(test)] mod tests`, clean `cargo build`
   → STILL WARNS. (A first run reported "no warnings" — that was a cargo
   incremental-cache artifact; `cargo clean && cargo build` reproduced all 3
   warnings.) **A test-only reference does NOT silence dead_code in a non-test
   build**, because the `#[cfg(test)]` module is compiled out of `cargo build`.

3. Per-constant `#[allow(dead_code)]` → `cargo build` AND `cargo test` both
   report ZERO warnings. ✓ This is the chosen mitigation.

### Why not module-level `#![allow(dead_code)]`?
Too broad — would mask legitimate future dead_code anywhere in core.rs.
Per-constant `#[allow(dead_code)]` is surgical and matches the existing
`DeviceCache.api` field precedent (core.rs:295-296). Each allow is temporary;
when P1.M2.T1/T2/P1.M3.T1 consume a constant, that constant's allow becomes
redundant (harmless — Rust does not warn on a redundant `allow(dead_code)`)
and can be removed during those subtasks.

### Attribute ordering (matches existing codebase pattern, core.rs:295-296)
```rust
/// doc comment
#[allow(dead_code)]
pub(crate) const X: u8 = 0xF0;
```
doc comment first → attribute → item. rustfmt preserves this.

## Visibility rationale (follow contract exactly)
- 6 constants `pub(crate)`: they're the wire vocabulary; future pure functions
  in core.rs (`build_command_data`, `parse_reply`) consume them, and `pub(crate)`
  leaves them reachable to lib.rs/tests without exposing them as public API.
- `REPLY_READ_TIMEOUT_MS` plain `const`: consumed only inside `core::burst_to_one`
  (P1.M3.T1); never needs cross-module visibility.

## Placement
Anchor: insert immediately AFTER `pub const REPORT_LENGTH: usize = 32;` (core.rs
line 10) and BEFORE `pub fn parse_hex_or_decimal`. Satisfies the contract's
"near the existing constants (DEFAULT_USAGE_PAGE, REPORT_LENGTH, etc.)" and keeps
all constants co-located at the top of the file. Group under a section comment.

(Alternative — near the private transport consts PAYLOAD_PER_REPORT/IN_DRAIN_MAX
~line 50 — was rejected: the contract names REPORT_LENGTH specifically, and the
top-of-file location keeps the whole "constants" region together.)

## Test design
- ONE new `#[test]` appended at the END of core.rs's existing
  `#[cfg(test)] mod tests` block (after `match_key_equality_drives_cache_rebuild`,
  before the module's closing `}`). Distinct file from the parallel/landed
  P1.M1.T1.* work (lib.rs) — zero collision risk.
- Asserts the 6 wire values match firmware_wire_contract.md §Constants (with
  messages citing the firmware symbol) AND `REPLY_READ_TIMEOUT_MS > 0`.
- Naming follows the file convention `test_<thing>_<scenario>` is NOT used here;
  core.rs uses bare descriptive names (`batches_for_empty_is_zero`,
  `matches_when_all_four_equal`). So the new test is named
  `typed_command_constants_match_firmware_contract` (descriptive, matches the
  `test_host_os_discriminants_match_firmware_contract` spirit in lib.rs).
- Uses the already-present `use super::*;` (no new import).

## Baseline test counts (current HEAD)
- core.rs: 13 `#[test]` fns  (verified via grep)
- lib.rs:  16 `#[test]` fns  (S1 + S2's RunCommand variants + S3's CommandResponse
  have ALL landed — CommandResponse at lib.rs:86, RunCommand variants present,
  run() has the 4 `todo!()` arms)
- `cargo test --lib` today: 29 passed, 0 failed (verified)

After THIS task: core.rs → 14 tests; `cargo test --lib` → 30 passed, 0 failed.

## Build/lint/fmt commands (verified working in this repo)
- `cargo build`           → currently "Finished `dev` profile", 0 warnings
- `cargo clippy --lib`    → 0 warnings
- `cargo fmt --check`     → exit 0 (no rustfmt.toml; default style)
- `cargo test --lib`      → 29 passed
No rustfmt.toml / clippy.toml exist (default configs).

## Downstream consumers (do NOT implement now — listed for awareness)
- P1.M2.T1 `build_command_data` → CMD_DISCRIMINATOR, CMD_* (builds `[0xF0, cmd,
  args..., 0x03]` payloads)
- P1.M2.T2 `parse_reply`        → RESPONSE_MARKER, CMD_* (disambiguates 0x51,
  decodes by cmd echo)
- P1.M3.T1 `burst_to_one` evolve → REPLY_READ_TIMEOUT_MS (bounded first-reply
  capture before the drain loop)

## Scope boundaries / anti-collision
- ONLY `src/core.rs` is modified. NOT lib.rs (where CommandResponse/RunCommand
  live), NOT error.rs, NOT main.rs, NOT Cargo.toml.
- No public API change: constants are `pub(crate)`/private → no lib.rs re-export
  needed, no README/PRD change (matches contract DOCS: "none").