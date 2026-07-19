# Research Notes — P1.M4.T3.S2 (Final cross-document consistency verification)

> Status as of research time. The repo is LIVE (parallel S1 was editing README and
> a prior task refactored `core.rs` mid-session), so **line numbers are
> time-sensitive**. The implementing agent MUST re-locate each finding by
> *symbol/value* via the grep commands in the PRP, not by line number.

---

## F1 — All 8 PRD §14 Invariants currently HOLD (pre-verified evidence)

Each invariant was grepped against `src/`. All pass at research time.

| Inv | Claim | Verified location (re-locate by symbol) | Evidence |
|----|-------|------------------------------------------|----------|
| 1 | Magic header `0x81 0x9F`; ETX `0x03` | `burst_to_one`: `request_data[1] = 0x81; request_data[2] = 0x9F;` (core.rs ~311-313). ETX via `ETX_TERMINATOR_BYTE: u8 = 0x03` const (core.rs ~47), pushed in `build_typed_payload` AND in lib.rs `build_payload` SendMessage arm. | grep `request_data\[1\]\|request_data\[2\]\|ETX_TERMINATOR_BYTE` |
| 2 | `REPORT_LENGTH=32`; hidapi buffer 33 | `pub const REPORT_LENGTH: usize = 32;` (core.rs:10). Buffer `[0u8; REPORT_LENGTH + 1]` = 33 in `burst_to_one` (~311), `read_buf` (~345), `drain_buf` (~368). `PAYLOAD_PER_REPORT = REPORT_LENGTH - 2` = 30 (~98). | grep `REPORT_LENGTH` |
| 3 | VID/PID `None` ⇒ match any | `device_matches` (~702): `vendor_id.is_none_or(\|v\| dev_vendor_id == v) && product_id.is_none_or(\|p\| dev_product_id == p)`. `usage_page`/`usage` compared with strict `==` (always required). | grep `is_none_or\|device_matches` |
| 4 | Cache keyed by `MatchKey`; invalidated on write failure; partial never retried | `MatchKey` struct derives `PartialEq`/`Eq` (~573). `ensure_cache` rebuilds when `existing.key != key`. `try_send_once`: `if failed > 0 { *cache = None; }` (~263). `send_raw_report` retry loop (~164): `(SendOutcome::Partial {..}, _) => return Err(PartialSendError{..})` — returns IMMEDIATELY, never retried. Only `TotalFailure` with `attempt < SEND_RETRIES` continues. `SEND_RETRIES = 1` (~114). | grep `MatchKey\|\*cache = None\|SendOutcome::Partial\|SEND_RETRIES` |
| 5 | Typed commands reuse same framing + cache | `run()` (lib.rs ~420-455) routes ALL of SendMessage/QueryInfo/QueryCallback/SetOs/ApplyHostContext through ONE path: `build_payload` → `send_raw_report` → `burst_to_one` (which prepends `0x81 0x9F`). Typed payload from `core::build_typed_payload` (`[0xF0][cmd][args][0x03]`). Same `MatchKey`, same cache, same `burst_to_one`. | grep `build_payload\|send_raw_report\|build_typed_payload` in lib.rs |
| 6 | Reply parsing disambiguates `0x51` / `0` / `1` / timeout | `parse_reply` (core.rs ~508-517): empty ⇒ `Timeout`; `RESPONSE_MARKER`(0x51) ⇒ `parse_typed_reply`; `0` ⇒ `Legacy{matched:false}`; `1` ⇒ `Legacy{matched:true}`; `_` ⇒ `Timeout`. | grep `parse_reply\|RESPONSE_MARKER\|CommandResponse::Legacy` in core.rs |
| 7 | Crate is transport-only (no window/pattern/rule) | grep `-rniE "window\|foreground\|regex\|pattern.match\|rule.eval\|detect\|focus\|app.id\|toml\|serde"` in `src/` returns ONLY `HostOs::Windows` (the OS enum, not window-detection) + the doc-comment phrase "window string" describing what the CALLER passes. NO actual window-detection / pattern-matching / rule-evaluation code. | re-run the grep; all hits are the OS enum or descriptive prose |
| 8 | Where SPEC and firmware disagree, firmware wins | Cross-checked core.rs consts vs `firmware_wire_contract.md` §Constants: `CMD_DISCRIMINATOR=0xF0`, `RESPONSE_MARKER=0x51`, `CMD_QUERY_INFO=0x01`, `CMD_QUERY_CALLBACK=0x02`, `CMD_SET_OS=0x03`, `CMD_APPLY_HOST_CONTEXT=0x05` — ALL MATCH. `HostOs` discriminants 0/1/2/3/4 match firmware `os_variant_t` table. Field offsets in `parse_typed_reply` match §Field Definitions. **No drift found.** | grep consts in core.rs; diff against firmware_wire_contract.md §Constants + §Command Table |

**Conclusion**: all 8 invariants hold. The sign-off should PASS unless a later edit
introduces drift (the agent re-runs the greps to confirm).

---

## F2 — Naming nit: contract says `build_command_data`, code says `build_typed_payload`

The item contract (step e) says: "Verify `build_command_data` output goes through
the same `send_raw_report` path." But the ACTUAL code uses:
- `core::build_typed_payload(cmd) -> Vec<u8>` — the typed-payload builder (core.rs).
- `lib.rs::build_payload(command, verbose) -> Vec<u8>` — the thin wrapper that
  handles SendMessage (append ETX) AND delegates typed variants to
  `core::build_typed_payload`.

`build_command_data` is the CONTRACT's generic name for this builder; it is NOT a
real symbol in the codebase. A grep for `build_command_data` will find only a
stale doc-comment reference in `lib.rs` (~line 422: `// straight to build_payload
/ core::build_command_data.`) — that comment uses the generic name while the real
fn is `build_typed_payload`.

**This is a candidate for the "fix if minor (typos in comments)" allowance.** The
agent should either (a) leave it (it's a defensible generic reference) or (b) align
the comment to `core::build_typed_payload`. Either way it does NOT affect
correctness — invariant 5 holds because the CALL is `core::build_typed_payload`.
The agent must NOT grep for a fn literally named `build_command_data` and conclude
invariant 5 fails — that would be a false negative.

---

## F3 — Toolchain + test-suite characteristics

- `cargo 1.92.0` — available.
- `clippy 0.1.92` — available as a component (`cargo clippy --version` works).
- The test suite is **hardware-free** on the critical path:
  - Dispatch tests (`test_run_query_info_dispatches_to_send` etc.) use bogus
    `VID=0xDEAD, PID=0xBEEF` ⇒ `device_matches` matches nothing ⇒ deterministic
    `Err(DeviceNotFound)`. No real keyboard needed.
  - `test_parse_*` / `build_typed_payload_*` / `device_matches_*` are pure unit
    tests over byte arrays — no I/O.
  - The handful of `run()`-on-real-HID tests (`test_run_with_send_message_command`,
    `test_run_with_list_devices_command`, `test_run_with_verbose_output`) are
    tolerant: they `assert!(result.is_ok() || result.is_err())` and accept the
    `DeviceNotFound`/`PartialSendError`/`Ok` outcomes. They pass with no hardware.
- **Build precondition**: `hidapi` (v2.4.1) links the system `libhidapi-dev`. The
  `target/` dir exists, so deps are already installed on this machine
  (Debian/Ubuntu: `libhidapi-dev libudev-dev`). If a clean machine is used, install
  those first or `cargo test` fails at LINK time (not a code defect).

---

## F4 — Parallel-execution note (README state)

S1 (P1.M4.T3.S1) edits README.md and runs concurrently. At the START of this
research the README still contained `"round B adds ..."` and `"must be provided
explicitly"`; by the END of research both greps returned `0` — **S1 landed its
edits mid-session**. This is the expected parallel-execution reality.

Implication for the implementing agent: by the time S2 runs, README is in its
**post-S1** state. The agent should verify the post-S1 README is *internally
consistent* and *consistent with the code* (the byte sequences in README's
Technical Details must match core.rs consts). S1's PRP is the contract for what
the post-S1 README contains; S2 does NOT re-edit README except to fix a
byte-sequence typo if one slipped through (cross-doc consistency is S2's job,
internal-section edits are S1's).

---

## F5 — Sign-off report location decision

`.gitignore` ignores only `/target`, `.env`, `.DS_Store` — so `plan/` IS committed.
Decision: the sign-off report lives at
`plan/001_b92a9b2b603f/P1M4T3S2/SIGNOFF.md` (co-located with this PRP, committed,
auditable). Rationale: it's a plan-task deliverable, it doesn't pollute the
shipped crate root, and it's discoverable next to the PRP that produced it. The
task's `OUTPUT: Signed-off verification` is satisfied by this file.

---

## F6 — firmware_wire_contract.md cross-check detail (Invariant 8)

`plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md` is the canonical
wire contract (mirrors firmware PRD §4.6; "Where this crate's PRD and the firmware
PRD disagree, the firmware wins"). Cross-check results:

| Concept | firmware_wire_contract.md | core.rs / lib.rs | Match |
|---------|---------------------------|-------------------|-------|
| Discriminator | `0xF0` (§Typed-Command Framing) | `CMD_DISCRIMINATOR = 0xF0` | ✅ |
| Response marker | `0x51` (§Reply Disambiguation) | `RESPONSE_MARKER = 0x51` | ✅ |
| QUERY_INFO id | `0x01` (§Command Table) | `CMD_QUERY_INFO = 0x01` | ✅ |
| QUERY_CALLBACK id | `0x02` | `CMD_QUERY_CALLBACK = 0x02` | ✅ |
| SET_OS id | `0x03` | `CMD_SET_OS = 0x03` | ✅ |
| APPLY_HOST_CONTEXT id | `0x05` | `CMD_APPLY_HOST_CONTEXT = 0x05` | ✅ |
| os_variant_t | UNSURE=0,LINUX=1,WINDOWS=2,MACOS=3,IOS=4 (§SET_OS) | `HostOs` enum same discriminants | ✅ |
| QUERY_INFO response layout | `[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]` | `parse_typed_reply` CMD_QUERY_INFO arm reads `.get(2..5)` in same order | ✅ |
| Reply disambiguation | `0x51`/`0`/`1`/no-reply/other | `parse_reply` match arms | ✅ |
| 33-byte buffer | `[0x00][0x81][0x9F][30 payload]` (§Typed-Command Framing diagram) | `burst_to_one` `[0u8; REPORT_LENGTH+1]` + `request_data[1]=0x81,[2]=0x9F` | ✅ |

**No discrepancies found.** The firmware is documented as NOT YET IMPLEMENTED
(only legacy path in `notifier.c`), so typed commands will time out against
current firmware — which is the DESIGNED fallback (`Timeout` ⇒ string-only mode).
This is not drift; it's the staged-rollout plan.