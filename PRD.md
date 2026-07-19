# SPECIFICATION — qmk_notifier (Rust Raw-HID transport crate)

**Master Product Requirements & Engineering Specification**
Target: a single document complete enough for a developer agent to reimplement
this crate from scratch. Read top to bottom before writing code.

> **Product scope.** This is the complete product & engineering specification
> for the `qmk_notifier` Rust crate (library + CLI). The crate is the
> **transport layer** of the QMKonnect ecosystem: it owns Raw-HID wire framing,
> device discovery/caching, burst-write with retry, typed-command framing, and
> device-reply parsing. It sends the legacy `class\x1Dtitle` string **and** the
> typed commands defined canonically in the firmware spec (`qmk-notifier/PRD.md`
> §4.6), and returns each parsed reply to the caller. **The host-side pattern
> matcher lives in `qmkonnect`, not here** — this crate is transport-only.

> **Scope of "this crate".** This document specifies **`qmk_notifier`**
> (underscore) — the Rust **library + CLI** that owns Raw-HID wire framing,
> device discovery/caching, burst-write, and typed-command
> transport + response parsing. It is *not* the desktop app (`qmkonnect`,
> `dabstractor/qmkonnect`) and *not* the firmware (`qmk-notifier`, hyphen,
> `dabstractor/qmk-notifier`). Those are the other nodes of the ecosystem.

---

## Table of Contents

1. [Product Overview](#1-product-overview)
2. [Repository Layout & Deliverables](#2-repository-layout--deliverables)
3. [Public API](#3-public-api)
4. [Wire Protocol — Transport Side](#4-wire-protocol--transport-side)
5. [Device Discovery & Matching](#5-device-discovery--matching)
6. [Device Cache](#6-device-cache)
7. [Send Path, Burst-Write & Retry](#7-send-path-burst-write--retry)
8. [Response Handling](#8-response-handling)
9. [Error Model](#9-error-model)
10. [Typed-Command Transport](#10-typed-command-transport)
11. [CLI](#11-cli)
12. [Non-Functional Requirements](#12-non-functional-requirements)
13. [Versioning, Release & Cross-Repo Links](#13-versioning-release--cross-repo-links)
14. [Key Invariants a Dev Agent Must Preserve](#14-key-invariants-a-dev-agent-must-preserve)

---

## 1. Product Overview

### 1.1 What qmk_notifier is

`qmk_notifier` is a small, dependency-light Rust crate (also built as a CLI) that
moves bytes between a desktop process and a QMK keyboard over the **Raw HID**
interface (usage page `0xFF60` / usage `0x61`). It owns **transport only**:

- the `0x81 0x9F` magic header and 32-byte report framing (with the hidapi
  leading report-ID byte),
- chunking of payloads longer than one report, the `0x03` (ETX) terminator,
- device discovery by usage page/usage (with optional VID/PID disambiguation),
- a process-global cache of opened HID handles,
- burst-write with one-rebuild retry, and
- typed-command framing + reply parsing.

It deliberately owns **no behavior**: it does not detect windows, does not match
patterns, and does not decide layers or callbacks. The host-side matcher is
ported into **`qmkonnect`**, not here.

### 1.2 The broader ecosystem (a dev must understand all three)

| Project | Repo | Language | Role |
|---|---|---|---|
| **qmk_notifier** ← *this repo* | `dabstractor/qmk_notifier` | Rust | **Transport crate** (+ CLI). Wire framing, device cache, burst-write, typed-command transport + reply parsing. Linked by `qmkonnect`. |
| **QMKonnect** | `dabstractor/qmkonnect` | Rust | Cross-platform **desktop daemon**. Detects the foreground window, runs the host-side matcher (`rules.toml`), and orchestrates the handshake + per-window sends via this crate. |
| **qmk-notifier** | `dabstractor/qmk-notifier` | C | On-keyboard **receiver + matcher + actor** (firmware). Owns the canonical wire contract. |
| **qmk_firmware** | `qmk/qmk_firmware` | C | Upstream QMK; qmk-notifier plugs into it via `RAW_ENABLE`. |

> **Naming hazard (read once):** `qmk_notifier` (underscore) = this Rust crate.
> `qmk-notifier` (hyphen) = the firmware C module. They talk over the fixed wire
> protocol in §4; the firmware's `PRD.md` §4 is the **canonical** byte-level
> contract — this document mirrors the transport side and defers to it on
> disagreement.

---

## 2. Repository Layout & Deliverables

```
qmk_notifier/
├── Cargo.toml          # package qmk_notifier, edition 2021; lib + bin
├── Cargo.lock
├── README.md           # user-facing README (kept in sync with this SPEC)
├── PRD.md              # this document
└── src/
    ├── lib.rs          # public API re-exports, RunCommand, RunParameters, parse_cli_args, run()
    ├── core.rs         # framing, constants, device discovery, cache, burst-write, retry
    ├── error.rs        # QmkError enum + Display + Error + From<HidError>
    └── main.rs         # CLI entry (parse_cli_args -> run)
```

**One library crate, one binary** (`Cargo.toml` has `[lib]` + `[[bin]]`). The
binary `qmk_notifier` is a thin wrapper around `parse_cli_args` + `run`
(`main.rs`).

**Dependencies** (`Cargo.toml`): `hidapi = "2.4.1"` (HID I/O), `clap = "4.5"`
(CLI), `dirs = "5.0.1"` (home/config dirs). (`toml`/`serde` are currently listed
but unused after config-file support was removed — they may be dropped.)

---

## 3. Public API

`lib.rs` re-exports the transport surface from `core.rs`:

```rust
pub use core::{
    list_hid_devices, parse_hex_or_decimal, send_raw_report,
    DEFAULT_PRODUCT_ID, DEFAULT_USAGE, DEFAULT_USAGE_PAGE, DEFAULT_VENDOR_ID,
    REPORT_LENGTH,
};
pub use error::QmkError;

/// What to send over the wire. The typed variants carry the host-side-rules
/// protocol (detailed in §10); SendMessage is the legacy window-string path.
pub enum RunCommand {
    SendMessage(String),   // legacy "{class}\x1D{title}" string
    ListDevices,
    QueryInfo,             // cmd 0x01
    QueryCallback(u8),     // cmd 0x02, arg = index
    SetOs(HostOs),         // cmd 0x03
    ApplyHostContext { layer: Option<u8>, callbacks: Vec<u8>, clear_board: bool }, // cmd 0x05
}

#[repr(u8)]
pub enum HostOs { Unsure = 0, Linux = 1, Windows = 2, Macos = 3, Ios = 4 }  // mirrors QMK os_variant_t

/// Parsed device reply (see §10.2).
pub enum CommandResponse {
    Legacy { matched: bool },              // response[0] in {0,1}
    Info { proto_ver: u8, feature_flags: u8, callback_count: u8, board_rules_present: bool },
    CallbackName { index: u8, name: Option<String> },
    Ack { ok: bool },
    Timeout,                               // no reply within the read timeout
}

pub struct RunParameters {
    pub command: RunCommand,
    pub vendor_id:  Option<u16>,   // None = match any (auto-discovery)
    pub product_id: Option<u16>,   // None = match any
    pub usage_page: u16,           // required (DEFAULT_USAGE_PAGE = 0xFF60)
    pub usage:      u16,           // required (DEFAULT_USAGE = 0x61)
    pub verbose: bool,
}

impl RunParameters {
    pub fn new(command, vendor_id, product_id, usage_page, usage, verbose) -> Self;
}

pub fn parse_cli_args() -> Result<RunParameters, QmkError>;
pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError>;
```

**Semantics:** VID/PID are `Option<u16>`; `None` ⇒ "match any" (auto-discovery by
usage page/usage — the keystone zero-config path). `usage_page`/`usage` are always
required and default to the QMK Raw-HID convention (`0xFF60` / `0x61`). The
typed-command variants and `CommandResponse` transport behavior are in §10.

## 4. Wire Protocol — Transport Side

> **Canonical owner: the firmware spec** (`dabstractor/qmk-notifier`, `PRD.md`
> §4). This section describes the transport side as this crate implements it; if
> the two ever disagree, **the firmware PRD wins** and this crate is wrong.

### 4.1 The logical payload

```
<application_class>\x1D<window_title>     (+ \x03 ETX, appended here, not by the caller)
```

`0x1D` = ASCII **Group Separator** (decimal 29); `0x03` = ASCII **End of Text**
(ETX). The caller (`qmkonnect`) builds `class\x1Dtitle` **without** a terminator;
`run()` / `send_raw_report()` appends ETX before framing.

### 4.2 On-the-wire report layout (what `hidapi::HidDevice::write` receives)

Each logical HID report is **32 bytes** (`REPORT_LENGTH = 32`). hidapi's `write()`
demands a leading report-ID byte (the interface has no report ID ⇒ `0x00`), so the
crate builds a **33-byte** buffer per report:

```
byte[0]      = 0x00              (report-ID leading byte for hidapi write)
byte[1]      = 0x81              (magic header byte 1)
byte[2]      = 0x9F              (magic header byte 2)
byte[3..33]  = <up to 30 payload bytes>  (zero-filled on the final report)
```

`PAYLOAD_PER_REPORT = REPORT_LENGTH - 2 = 30`. A payload longer than 30 bytes is
split into `ceil(len/30)` back-to-back reports (`batches_for`):
`(len + REPORT_LENGTH - 3) / PAYLOAD_PER_REPORT`.

### 4.3 Burst-write is safe without per-report ACK

QMK's raw-HID **OUT** endpoint buffers up to 4 reports and drains them in one
main-loop pass; when full it NAKs and the host's `write()` blocks. **Reports are
never dropped**, so all reports of a long message are burst-written back-to-back
without per-report ack. See §7.

### 4.4 Replies are received (historical note)

The firmware sends a 32-byte reply per report via `raw_hid_send(response,
RAW_REPORT_SIZE)` (`RAW_REPORT_SIZE = 32`). This is **received** by the host — the
reply is a full 32-byte logical report satisfying the `length == 32` guard on every
QMK USB protocol. (An earlier firmware build reused the header-stripped `length`
of `30` and the reply was rejected; fixed in qmk-notifier commit `01a51935`. The
stale "ack silently dropped because `length == RAW_EPSIZE`" wording that appeared
in older revisions of this crate's comments and the sibling specs has been
corrected.) This crate reads + parses the reply (§8).

---

## 5. Device Discovery & Matching

### 5.1 The match predicate (pure)

`device_matches(dev_vid, dev_pid, dev_page, dev_usage, vid, pid, page, usage)`:

```
dev_usage_page == page
  && dev_usage == usage
  && vid.is_none_or(|v| dev_vid == v)
  && pid.is_none_or(|p| dev_pid == p)
```

`usage_page`/`usage` are **always required** (primary identifier). `vid`/`pid` are
**optional**; `None` ⇒ wildcard for that axis ⇒ auto-discovery.

### 5.2 `open_matching_devices(api, key)`

Enumerate `api.device_list()`, keep those satisfying the predicate, `open_device`
each. Empty filter result ⇒ `QmkError::DeviceNotFound { vid, pid, page, usage }`.
All matches failed to open ⇒ `QmkError::DeviceOpenError(..)` (permissions — point
users at the udev rule on Linux).

### 5.3 `list_hid_devices()`

Diagnostic: enumerates **all** HID devices (no filter), opens each for
manufacturer/product strings, prints `VID/PID/usage page/usage/path`. Used by
`qmkonnect --list-devices` and the `--list` CLI.

---

## 6. Device Cache

Enumerating the HID bus + opening handles was the dominant per-notification cost.
The crate keeps a process-global cache:

```rust
struct DeviceCache { api: HidApi, devices: Vec<HidDevice>, key: MatchKey }
static DEVICE_CACHE: LazyLock<Mutex<Option<DeviceCache>>> = LazyLock::new(|| Mutex::new(None));
```

- **`MatchKey { vendor_id, product_id, usage_page, usage }`** keys the cache. The
  hot path reuses cached handles whenever the key is unchanged.
- **`ensure_cache`** rebuilds (full `HidApi::new()` + `open_matching_devices`) only
  when the slot is empty or the key changed.
- **Invalidation:** any write error drops the cache (`*cache = None`) so the next
  attempt/call re-enumerates. This is how a replug (stale fd) is recovered.
- **`lock_cache`** recovers from a poisoned mutex rather than propagating a panic.
- **Intentional trade-off:** a newly-plugged *additional* matching device is not
  picked up until a write fails or the key changes. Fine for the single-keyboard
  case; the replug case is handled via write-failure invalidation.

---

## 7. Send Path, Burst-Write & Retry

### 7.1 `send_raw_report(data, vid, pid, page, usage, verbose)`

Top-level send. Builds the `MatchKey`, computes `batch_count = batches_for(data)`,
then loops `0..=SEND_RETRIES`:

- `AllSucceeded` ⇒ `Ok(())`.
- `Partial { succeeded, failed }` ⇒ `Err(PartialSendError { succeeded, failed })`
  (a partial send is **never retried** — no duplicate notifications).
- `TotalFailure` (zero devices succeeded) and attempts remain ⇒ loop (the cache
  was already invalidated inside `try_send_once`, so the next iteration
  re-enumerates). On the final attempt ⇒ `Err(SendReportError(..))`.

`SEND_RETRIES = 1` ⇒ at most one rebuild+retry, only on total failure.

### 7.2 `try_send_once(key, data, batch_count, verbose)`

`ensure_cache`, then `burst_to_one` every cached device. Tallies
succeeded/failed; on any failure invalidates the cache. Returns the `SendOutcome`.

### 7.3 `burst_to_one(interface, data, batch_count, verbose) -> bool`

Fills a stack `[0u8; REPORT_LENGTH + 1]` buffer (`[0x00, 0x81, 0x9F, payload…]`),
`write()`s each 33-byte report back-to-back, then **drains** pending IN-side
reports (§8). Returns `false` on the first `write()` error.

---

## 8. Response Handling

The firmware sends a 32-byte reply per burst (`raw_hid_send(response,
RAW_REPORT_SIZE)`, §4.4). This crate **reads and parses** it:

- For a `SendMessage`, `response[0]` is the legacy match-bool (`0`/`1`) ⇒
  `CommandResponse::Legacy { matched }`.
- For a typed command, `response[0] == 0x51` ⇒ typed reply, decoded by the
  command-echo byte into `Info` / `CallbackName` / `Ack` (§10.2). No reply within
  the bounded `read_timeout` ⇒ `Timeout` (the caller treats a non-`0x51` reply or
  `Timeout` as a legacy / non-capable device and stays in string-only mode).

`burst_to_one` also **drains** any surplus IN-side reports non-blocking
(`read_timeout(&mut buf, 0)`, bounded by `IN_DRAIN_MAX = 32`) so a persistent
handle does not stall on accumulated replies. `run()` returns the parsed
`CommandResponse`.

## 9. Error Model

`QmkError` ( `error.rs`, `impl std::error::Error`, `impl From<HidError>` ):

| Variant | Meaning |
|---|---|
| `HidApiInitError(String)` | `HidApi::new()` failed. |
| `DeviceNotFound { vid, pid, usage_page, usage }` | No interface matched the filter. |
| `DeviceOpenError(String)` | Matches found but none openable (permissions). |
| `SendReportError(HidError)` | A write failed (wrapped hidapi error). |
| `HidReadError(String)` | a reply read failed. |
| `NoResponseReceived(String)` | no reply within the timeout. |
| `PartialSendError { succeeded, failed }` | Some devices succeeded, some failed (not retried). |
| `InvalidHexValue(String)` / `InvalidDecimalValue(String)` | CLI ID parse failure. |
| `MissingRequiredParameter(String)` | CLI invoked with neither message nor `--list`. |
| `RemovedFeature(String)` | CLI `--create-config` (config-file creation removed). |

`qmkonnect`'s `QmkNotifier::notify` retries only on error strings containing
`"no device found"`, `"permission denied"`, or `"failed to open"` (i.e.
`DeviceNotFound` / `DeviceOpenError` / open failures); other errors propagate.

---

## 10. Typed-Command Transport

The typed commands carry the host-side-rules protocol (host-side design canonical
in `qmkonnect/spec/HOST_RULES.md`; wire contract in the firmware
`qmk-notifier/PRD.md` §4.6). The `RunCommand` variants, `HostOs`, and
`CommandResponse` are defined in §3; this section covers transport behavior.

### 10.1 Framing

Typed commands share the `0x81 0x9F` namespace; the discriminator is `data[2] == 0xF0`:

```
[0x81][0x9F][0xF0][cmd_id][ args… ][0x03]
```

The crate builds this as `data` and reuses the **same ETX-framed, multi-report
chunking** as strings (`batches_for`, `burst_to_one`). Single-report commands
(`QUERY_INFO`, `QUERY_CALLBACK`, `SET_OS`) fit one report; `APPLY_HOST_CONTEXT`
may span multiple reports when the callback list exceeds 30 payload bytes — so the
callback-id list is uncapped. The device cache and retry logic (§6–§7) are
**unchanged** — typed commands reuse the cached handles for the same `MatchKey`;
retry/cache behavior matches `SendMessage`.

- `ApplyHostContext { layer, callbacks, clear_board }`: `layer = None` ⇒ `0xFF`
  (clear host layer); `Some(n)` ⇒ the host-layer number (host layers are reserved
  ≥ 224 by convention). `callbacks` = the **full desired enabled** id set.
  `clear_board = true` ⇒ set the firmware's `clear_board` flag bit (the firmware
  clears its board layer/command before applying the host context — the per-window
  "replace" semantics).
- `SetOs(HostOs)`: the host declares its OS at connect; the host OS is
  authoritative while connected (`HostOs` mirrors QMK's `os_variant_t`).

### 10.2 Reply parsing

After a typed-command burst, read **one** 32-byte IN report with a bounded
`read_timeout`:

- `response[0] == 0x51` ⇒ typed reply; decode by `response[1]` (cmd echo) into
  `Info` / `CallbackName` / `Ack`.
- `response[0] in {0,1}` ⇒ `Legacy { matched }` (a non-capable device answering a
  typed command as a no-match string, or the reply to a `SendMessage`).
- no reply within timeout ⇒ `Timeout` ⇒ the caller stays in string-only mode (per
  the firmware handshake, `PRD.md` §4.6).

The drain loop (§8) is retained to keep the handle from stalling on extra replies.

### 10.3 What stays out of this crate

- **Pattern matching** lives in `qmkonnect` (ported from the firmware
  `pattern_match.c`, full parity: `* ^ $ WT + \d \D \w \W \s \S \b \B .`). This
  crate does not match.
- **Rule evaluation, handshake orchestration, desktop OS detection** — all
  `qmkonnect`. This crate only transports bytes and parses replies.

## 11. CLI

`main.rs` calls `parse_cli_args()` then `run(params)`. `parse_cli_args` (clap):

| Option | Short | Default | Description |
|---|---|---|---|
| `message` (positional) | — | — | Message to send (ETX appended). |
| `--vendor-id` | `-i` | auto (`None`) | USB VID, decimal or `0xHEX`. |
| `--product-id` | `-p` | auto (`None`) | USB PID. |
| `--usage-page` | `-u` | `0xFF60` | HID usage page. |
| `--usage` | `-a` | `0x61` | HID usage. |
| `--verbose` | `-v` | off | Verbose transport logging. |
| `--list` | `-l` | off | List all HID devices. |

`--create-config` exists only to print a "removed feature" error. Either `message`
or `--list` is required. `parse_hex_or_decimal` accepts `0x..`/`0X..` (hex) or bare
decimal.

*(Diagnostic subcommands — e.g. `query-info`, `list-callbacks` — are optional
conveniences over the library API and are not required for `qmkonnect`, which
uses the library directly.)*

---

## 12. Non-Functional Requirements

- **Performance.** No per-report enumeration on the hot path (cached handles); a
  33-byte stack buffer per report (no per-report heap). Typical notification is
  sub-millisecond beyond the USB write.
- **No `unsafe`.** All HID I/O goes through the `hidapi` crate.
- **Cross-platform.** Builds on Linux, macOS, Windows (hidapi-sys underneath).
  Linux needs libhidapi + libudev; the Arch build links `-lhidapi-hidraw` (not
  `-lhidapi-libusb`) so usage/usage_page matching works.
- **Robustness.** A replug invalidates the cache on the next write failure and is
  recovered by one re-enumeration. A poisoned cache mutex is recovered, not
  propagated. Partial sends are reported, never retried (no duplicates).
- **MSRV.** Kept current with `qmkonnect`'s toolchain; edition 2021.

---

## 13. Versioning, Release & Cross-Repo Links

- **Never published to crates.io** (`qmkonnect` consumes it as a git-tagged dep).
  Releases are git tags `v<x.y.z>`.
- `qmkonnect/Cargo.toml` pins:
  ```toml
  qmk_notifier = { package = "qmk_notifier",
                   git = "https://github.com/dabstractor/qmk_notifier",
                   tag = "v0.3.0" }   # the release implementing this spec
  ```
- **Cross-repo source of truth:**
  - Canonical wire contract → `dabstractor/qmk-notifier` `PRD.md` §4 (firmware).
  - Host-side orchestration / `rules.toml` → `dabstractor/qmkonnect`
    `spec/HOST_RULES.md`.
  - Transport (this crate) → this `PRD.md`.

---

## 14. Key Invariants a Dev Agent Must Preserve

1. **Magic header is `0x81 0x9F`; ETX is `0x03`.** The caller builds
   `class\x1Dtitle`; this crate appends ETX before framing. Never change without
   coordinating all three repos.
2. **`REPORT_LENGTH = 32`; the hidapi buffer is 33** (leading `0x00` report-ID
   byte). 30 payload bytes per report.
3. **VID/PID `None` ⇒ match any** (auto-discovery). `usage_page`/`usage` always
   required.
4. **The device cache is keyed by `MatchKey`** and invalidated on any write
   failure; a partial send is never retried.
5. **Typed commands reuse the same framing + cache** as strings —
   `[0x81][0x9F][0xF0][cmd][args][0x03]`, multi-report, same `MatchKey`.
6. **Reply parsing disambiguates `0x51` (typed) from `0`/`1` (legacy match-bool)**
   from no-reply (`Timeout`). Legacy/timeout ⇒ the host stays in string-only mode.
7. **This crate is transport-only.** No window detection, no pattern matching, no
   rule evaluation — those are `qmkonnect`'s job. The matcher is ported into
   `qmkonnect`, not here.
8. **Where this SPEC and the firmware `PRD.md` §4 disagree, the firmware wins.**
   Report the drift.

---

*End of specification. This crate is the transport third of a three-part system;
see `dabstractor/qmk-notifier/PRD.md` (firmware, canonical wire contract) and
`dabstractor/qmkonnect/spec/PRD.md` + `spec/HOST_RULES.md` (desktop) for the
other two thirds.*