# Research Notes — P1.M2.T2.S1 (parse_reply for typed 0x51 replies)

## Empirical findings (verified in-repo, 2025-07-19)

### F1 — `parse_reply` does NOT exist yet
`grep -n "parse_reply\|parse_typed_reply\|parse_callback_name" src/core.rs`
→ only match is a COMMENT at line 24 ("...consumers land in P1.M1.T3 (parse_reply
+ the reply reader)"). No function, no `todo!()`, no stub. So S1 ADDS the function
fresh — there is nothing to "fix" or "replace". (Contrast with P1.M2.T1.S2, which
edited an existing arm.)

### F2 — Current test baseline = 39 (P1.M2.T1.S2 already landed)
`cargo test --lib` → "test result: ok. 39 passed; 0 failed".
- `grep -c "#\[test\]" src/core.rs` → **23** (21 original + 2 from P1.M2.T1.S2:
  `build_typed_payload_apply_host_context_representative_ids` @850 and
  `..._clamps_count_at_255` @863).
- `grep -c "#\[test\]" src/lib.rs` → **16**.
- 23 + 16 = 39. ✓
- Line 379 now reads `payload.push(callbacks.len().min(255) as u8);` (the S2-of-T1
  clamp IS in). So when MY item begins implementation, the baseline is 39.
- After S1 adds 6 parse_reply tests → **45 total** (29 core + 16 lib).

NOTE: the codebase is a MOVING TARGET (P1.M2.T1.S2 was editing core.rs in parallel
during this research session — my first `read` showed the pre-clamp `as u8`; my
later `grep` showed the post-clamp `.min(255)`). Line numbers below are snapshots;
the implementing agent MUST re-anchor by landmark (function/struct/doc-comment
names), not by line number.

### F3 — `CommandResponse` already has the derives tests need
`src/lib.rs:85`: `#[derive(Debug, Clone, PartialEq, Eq)] pub enum CommandResponse`.
So `assert_eq!(parse_reply(&buf), CommandResponse::Info { ... })` compiles and
compares structurally. Eq is sound (all fields Eq: u8, bool, Option<String>).

### F4 — dead_code lint propagation (THE decisive experiment)
Two throwaway rlibs compiled with `rustc --edition 2021 --crate-type rlib`:
- **CASE A** (outer `#[allow(dead_code)]` ONLY; private `parse_typed`/`helper`
  bare) → **ZERO warnings, exit 0**.
- **CASE B** (NO allow anywhere) → 3 warnings (`parse_reply`, `parse_typed`,
  `helper` all flagged "never used").

CONCLUSION: `#[allow(dead_code)]` on `parse_reply` ALONE is sufficient — its
private callees `parse_typed_reply` and `parse_callback_name` do NOT need their
own allow. This is the SAME pattern as `build_typed_payload` (single
`#[allow(dead_code)]` on the entry; no live caller until P1.M3.T3).

### F5 — A const referenced by an allow-dead fn does NOT warn
core.rs lines 18-23 (existing comment, written for P1.M1.T2.S1) explicitly
documents: "verified: a const referenced by an allow-dead fn's body does NOT warn.
Only RESPONSE_MARKER and REPLY_READ_TIMEOUT_MS still carry `#[allow(dead_code)]`
— their consumers land in [P1.M2.T2]." → Once `parse_reply` references
`RESPONSE_MARKER`, it is safe to **REMOVE** `RESPONSE_MARKER`'s
`#[allow(dead_code)]` (line 29). `REPLY_READ_TIMEOUT_MS` (line 38) KEEPS its allow
(no consumer until the P1.M3.T1 reply reader).

### F6 — `parse_reply` visibility & how `run()` will call it
`mod core;` in lib.rs is PRIVATE; `pub(crate)` items in it are reachable from
lib.rs via the path `core::parse_reply(...)`. The existing `pub use core::{...}`
re-export list does NOT include `build_typed_payload` (it's `pub(crate)`,
internal). So `parse_reply` likewise stays **`pub(crate)`** and is NOT added to
the `pub use` line. `run()` (P1.M3.T3.S1) will call it as `core::parse_reply(...)`.
DO NOT touch lib.rs.

### F7 — S1 vs S2 scope split (reconciliation of the ambiguous title)
The item's title is "parse_reply for typed 0x51 replies" and its successor
(P1.M2.T2.S2) is "Add legacy/timeout/edge-case handling to parse_reply and
finalize tests". BUT the item's CONTRACT code is the COMPLETE `parse_reply`
(empty→Timeout, 0x51→typed, 0/1→Legacy, _→Timeout). Rust's match-exhaustiveness
makes the legacy/timeout arms UNAVOIDABLE — you cannot implement `parse_reply`
without them. RECONCILED INTERPRETATION:
- **S1 implements the COMPLETE function body** (the full item contract — it's
  only ~30 lines and the legacy/timeout arms are trivial one-liners required for
  exhaustiveness). S1 is NOT a partial/half-function.
- **S1's TESTS are the 6 typed-path tests the item mandates** (the item lists
  exactly 6, all typed `0x51` happy paths).
- **S2 adds the legacy/timeout/edge-case TESTS** (empty input, unknown marker
  byte, legacy 0/1, truncated Info/CallbackName replies, unknown cmd echo) and
  "finalizes" the test matrix. The function body is already complete after S1;
  S2 only extends test coverage.

This mirrors how the P1.M2.T1.S2 PRP reconciled a title/contract divergence with
an explicit banner.

### F8 — The 6 mandated tests → CommandResponse variants (exact mapping)
| # | Input bytes | Expected `CommandResponse` |
|---|-------------|----------------------------|
| 1 | `[0x51,0x01,2,0x03,5,1]` | `Info{proto_ver:2, feature_flags:0x03, callback_count:5, board_rules_present:true}` |
| 2 | `[0x51,0x01,2,0x03,5,0]` | `Info{..., board_rules_present:false}` (byte 0 ⇒ false) |
| 3 | `[0x51,0x02,3,b'V',b'i',b'm',0,0]` | `CallbackName{index:3, name:Some("Vim")}` |
| 4 | `[0x51,0x02,5,0x00,...]` | `CallbackName{index:5, name:None}` (NUL-at-start) |
| 5 | `[0x51,0x03,1]` | `Ack{ok:true}` (SET_OS, ack==1) |
| 6 | `[0x51,0x05,0]` | `Ack{ok:false}` (APPLY_HOST_CONTEXT, ack==0) |

### F9 — `String::from_utf8(...).ok()` vs `from_utf8_lossy`
The item says "Use String::from_utf8 with .ok() for lossy handling" (wording is
loose). `String::from_utf8(bytes.to_vec()).ok()` returns `None` for invalid
UTF-8; true lossy (`from_utf8_lossy`) substitutes U+FFFD. For the DOCUMENTED
ASCII-only names (`0x20–0x7E` per the firmware sanitizer) both behave identically
(ASCII ⊂ UTF-8, always Ok). The item PRESCRIBES `.ok()`, so we use it — and
document that invalid bytes ⇒ `None` (defensive: a corrupt name is treated as
absent rather than producing a garbage string). `.ok()` is clippy-clean by
default.

### F10 — Defensive `.get()` indexing is mandatory
Item: "Use defensive .get() indexing — replies may be truncated." Every field
read in `parse_typed_reply` uses `response.get(i).copied().unwrap_or(0)` so a
short reply (e.g. firmware sends only `[0x51, 0x01, 2]`) decodes to
`Info{proto_ver:2, feature_flags:0, callback_count:0, board_rules_present:false}`
instead of panicking. The QUERY_CALLBACK name slice uses
`&response[3.min(response.len())..]` (item's exact form) which never exceeds len
⇒ never panics. S1 IMPLEMENTS this defensively; S2 TESTS the truncation paths.

## No external research needed
This is a pure-Rust byte-parsing function with no new deps, no async, no I/O, no
unsafe. The wire contract is fully canonicalized in
`architecture/firmware_wire_contract.md`. `String::from_utf8`, slice indexing, and
`Option::unwrap_or` are stdlib. No library docs to cite beyond the Rust stdlib
(which the implementer's training data covers).

## Confirmed commands (verified working in this repo)
- `cargo test --lib` → currently "39 passed". After S1 → expect "45 passed".
- `cargo build` / `cargo clippy --lib` / `cargo fmt --check` — no rustfmt.toml,
  no clippy.toml (default config; verified by the P1.M2.T1.S2 PRP).