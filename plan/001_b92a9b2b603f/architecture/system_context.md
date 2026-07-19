# System Context: qmk_notifier v0.3.0

## Current State (v0.2.1)

The crate is at **v0.2.1**, builds clean, and passes all 22 tests. It implements
**only the legacy string-send path** — typed commands and reply parsing do not
exist yet.

### Source files

| File | Lines | Responsibility |
|---|---|---|
| `src/lib.rs` | 371 | Public API re-exports, `RunCommand` (SendMessage/ListDevices only), `RunParameters`, `parse_cli_args()`, `run()` returning `Result<(), QmkError>`, unit tests |
| `src/core.rs` | 567 | Framing constants, `parse_hex_or_decimal`, `list_hid_devices`, `send_raw_report`, `burst_to_one`, device cache (`DeviceCache`, `MatchKey`, `ensure_cache`), `open_matching_devices`, `device_matches`, unit tests |
| `src/error.rs` | 75 | `QmkError` enum (11 variants), `Display`, `Error`, `From<HidError>` |
| `src/main.rs` | 16 | CLI entry: `parse_cli_args()` → `run()` |

### What's already working (must not regress)

1. **Wire framing**: 33-byte buffer `[0x00, 0x81, 0x9F, payload…]` per report.
   `PAYLOAD_PER_REPORT = 30`. `batches_for(data)` = `(len + 29) / 30`.
2. **Device cache**: `DeviceCache { api, devices, key }` behind
   `LazyLock<Mutex<Option<DeviceCache>>>`. Rebuilt only on key change or empty.
   Invalidated on any write failure. Mutex poisoning recovered.
3. **Burst-write + retry**: `send_raw_report` loops `0..=SEND_RETRIES(=1)`.
   `try_send_once` → `burst_to_one` per device. `SendOutcome`:
   `AllSucceeded` / `Partial { succeeded, failed }` / `TotalFailure`.
   Partial sends never retried.
4. **IN drain**: After burst, `burst_to_one` drains pending IN-side reports
   non-blocking (`read_timeout(0)`, bounded by `IN_DRAIN_MAX = 32`). **All
   replies are currently discarded** — this is the core change v0.3.0 makes.
5. **CLI**: clap-based, positional `message`, `--vendor-id/-i`, `--product-id/-p`,
   `--usage-page/-u`, `--usage/-a`, `--verbose/-v`, `--list/-l`,
   `--create-config/-c` (prints removed-feature error).
6. **Error model**: All 11 `QmkError` variants from the PRD §9 are implemented.

### What's missing (the v0.3.0 delta)

| Gap | PRD §  | Impact |
|---|---|---|
| `HostOs` enum | §3 | New type — mirrors QMK `os_variant_t` |
| `RunCommand` variants (QueryInfo, QueryCallback, SetOs, ApplyHostContext) | §3, §10 | Extends enum; makes `run()` match non-exhaustive |
| `CommandResponse` enum | §3, §8, §10.2 | New return type for `run()` |
| `run()` return type change | §3 | `Result<(), QmkError>` → `Result<CommandResponse, QmkError>` — **breaking API change** |
| Typed-command payload builder | §10.1 | Pure function: `RunCommand` → `Vec<u8>` payload bytes |
| Reply parser | §8, §10.2 | Pure function: `&[u8]` → `CommandResponse` |
| `burst_to_one` reply capture | §7.3, §8 | Must capture first 32-byte reply (bounded timeout) instead of discarding all |
| `send_raw_report` reply propagation | §7.1 | Return type: `Result<(), QmkError>` → `Result<Option<Vec<u8>>, QmkError>` |
| Unused deps (`toml`, `serde`, `dirs`) | §2 | Cargo.toml cleanup |
| Version bump | §13 | 0.2.1 → 0.3.0 |
| CLI diagnostic subcommands | §11 | Optional: `--query-info`, `--list-callbacks` |

### Dependency note

`Cargo.toml` currently lists `toml = "0.8.10"`, `serde = { version = "1.0", features = ["derive"] }`,
and `dirs = "5.0.1"`. **None of these are referenced in any source file** (verified
by grep). The PRD §2 explicitly says `toml`/`serde` may be dropped. `dirs` is also
dead — config-file support was removed in commit `64d5f74`.

## Ecosystem Dependencies

### Downstream consumer: qmkonnect

`qmkonnect` pins `qmk_notifier` at git tag `v0.2.1`:
```toml
qmk_notifier = { package = "qmk_notifier",
                 git = "https://github.com/dabstractor/qmk_notifier",
                 tag = "v0.2.1" }
```

**Symbols used** (all in `src/core/notifier.rs`):
- `qmk_notifier::run(params)` — called in `QmkNotifier::notify`, result is `Ok(_) => ...` (discarded)
- `qmk_notifier::RunParameters::new(...)`
- `qmk_notifier::RunCommand::SendMessage(message.clone())`
- `qmk_notifier::DEFAULT_USAGE_PAGE`
- `qmk_notifier::DEFAULT_USAGE`

**Impact of `run()` return change**: qmkonnect's `match qmk_notifier::run(params) { Ok(_) => return Ok(()), ... }`
will **still compile** (the Ok variant binds to `_`). But qmkonnect will need to
update to capture and use `CommandResponse` for the capability handshake (planned
in `qmkonnect/spec/HOST_RULES.md`). This crate's v0.3.0 release is the prerequisite.

**Retry logic**: qmkonnect matches error Display strings containing
`"no device found"`, `"permission denied"`, `"failed to open"`. Current
`QmkError::DeviceOpenError` Display contains `"Error opening device"` — note that
`"failed to open"` does NOT appear in the current Display output. This is a latent
string-matching gap in qmkonnect, not this crate's problem, but worth flagging.

### Upstream canonical: firmware (qmk-notifier)

The firmware `notifier.c` **implements the typed-command namespace** (§4.6).
`hid_notify()` routes the first report's `data[2] == 0xF0` to a typed-reassembly
path; `handle_typed_command()` dispatches QUERY_INFO / QUERY_CALLBACK / SET_OS /
APPLY_HOST_CONTEXT and emits `[0x51][cmd_echo][payload]` replies on the ETX report,
with the `host_layer`/`host_cb_enabled` trackers updated accordingly. Confirmed via
live hardware testing (Dactyl-Manuform, VID 0xFEED / PID 0x0000) cross-checked
against the firmware source. The firmware uses a **per-report reply model**: one
32-byte reply per `hid_notify()` call, where only the ETX-report reply carries the
real (typed `0x51…` or legacy match-bool) result and intermediate reports reply with
a legacy `0`.

**Implication for this crate**: The transport must handle the case where the
device does not reply to typed commands (legacy firmware). `CommandResponse::Timeout`
is the expected result, and the caller (qmkonnect) treats it as "stay in
string-only mode." This is explicitly designed for in the PRD §10.2 and §14
invariant #6.