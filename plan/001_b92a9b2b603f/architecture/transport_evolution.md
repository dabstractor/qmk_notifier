# Transport Evolution: v0.2.1 → v0.3.0

This document describes the exact code changes needed to evolve the transport
layer from reply-discarding (v0.2.1) to reply-capturing + typed-command (v0.3.0).

## Data Flow Comparison

### v0.2.1 (current)

```
run(params)
  └─ match RunCommand::SendMessage(msg)
       └─ append ETX → data: Vec<u8>
            └─ send_raw_report(data, vid, pid, page, usage, verbose) → Result<(), QmkError>
                 └─ try_send_once(key, data, batch_count, verbose) → Result<SendOutcome, QmkError>
                      └─ ensure_cache(key)
                           └─ burst_to_one(interface, data, batch_count, verbose) → bool
                                ├─ write() each 33-byte report
                                └─ drain IN reports (ALL discarded, non-blocking)
```

### v0.3.0 (target)

```
run(params)
  └─ match RunCommand::*
       ├─ SendMessage(msg) → build_command_data → data
       ├─ QueryInfo        → build_command_data → data
       ├─ QueryCallback(i) → build_command_data → data
       ├─ SetOs(os)        → build_command_data → data
       ├─ ApplyHostContext → build_command_data → data
       └─ ListDevices      → list_hid_devices() → Ok(CommandResponse::Timeout)
            └─ send_raw_report(data, vid, pid, page, usage, verbose) → Result<Option<Vec<u8>>, QmkError>
                 └─ try_send_once(key, data, batch_count, verbose) → Result<(SendOutcome, Option<Vec<u8>>), QmkError>
                      └─ ensure_cache(key)
                           └─ burst_to_one(interface, data, batch_count, verbose) → (bool, Option<Vec<u8>>)
                                ├─ write() each 33-byte report
                                ├─ read_timeout(buf, REPLY_READ_TIMEOUT_MS) → capture FIRST reply
                                └─ drain surplus IN reports (non-blocking, discarded)
            └─ parse_reply(reply_bytes) → CommandResponse
```

## Signature Changes (exact)

### 1. `build_command_data` (NEW, pure function in core.rs)

```rust
/// Build the logical payload bytes for any RunCommand variant.
/// The payload goes AFTER the 0x81 0x9F magic header (burst_to_one adds those).
/// For strings: [string_bytes..., 0x03 ETX].
/// For typed: [0xF0, cmd_id, args..., 0x03 ETX].
fn build_command_data(command: &RunCommand) -> Vec<u8>
```

### 2. `parse_reply` (NEW, pure function in core.rs)

```rust
/// Parse a 32-byte device reply into a CommandResponse.
/// response[0]==0x51 → typed reply (decode by response[1])
/// response[0] in {0,1} → Legacy { matched }
/// empty/unknown → Timeout
fn parse_reply(response: &[u8]) -> CommandResponse
```

### 3. `burst_to_one` (EVOLVED)

```rust
// OLD:
fn burst_to_one(interface: &HidDevice, data: &[u8], batch_count: usize, verbose: bool) -> bool

// NEW:
fn burst_to_one(interface: &HidDevice, data: &[u8], batch_count: usize, verbose: bool) -> (bool, Option<Vec<u8>>)
//                                                       Returns (success, first_reply_report)
```

### 4. `try_send_once` (EVOLVED)

```rust
// OLD:
fn try_send_once(key: &MatchKey, data: &[u8], batch_count: usize, verbose: bool) -> Result<SendOutcome, QmkError>

// NEW:
fn try_send_once(key: &MatchKey, data: &[u8], batch_count: usize, verbose: bool) -> Result<(SendOutcome, Option<Vec<u8>>), QmkError>
```

### 5. `send_raw_report` (EVOLVED, public API)

```rust
// OLD:
pub fn send_raw_report(data: &[u8], vid: Option<u16>, pid: Option<u16>, page: u16, usage: u16, verbose: bool) -> Result<(), QmkError>

// NEW:
pub fn send_raw_report(data: &[u8], vid: Option<u16>, pid: Option<u16>, page: u16, usage: u16, verbose: bool) -> Result<Option<Vec<u8>>, QmkError>
```

### 6. `run` (EVOLVED, public API)

```rust
// OLD:
pub fn run(params: RunParameters) -> Result<(), QmkError>

// NEW:
pub fn run(params: RunParameters) -> Result<CommandResponse, QmkError>
```

## New Constants (core.rs)

```rust
/// Typed-command discriminator byte (first payload byte after 0x81 0x9F).
const CMD_DISCRIMINATOR: u8 = 0xF0;
/// Typed-response marker (response[0] == 0x51 means typed reply).
const RESPONSE_MARKER: u8 = 0x51;
/// Command IDs (§4.6 command table).
const CMD_QUERY_INFO: u8 = 0x01;
const CMD_QUERY_CALLBACK: u8 = 0x02;
const CMD_SET_OS: u8 = 0x03;
const CMD_APPLY_HOST_CONTEXT: u8 = 0x05;
/// Bounded timeout (ms) for reading the first reply after a burst.
/// Must be > 0 (unlike the drain's non-blocking 0).
const REPLY_READ_TIMEOUT_MS: i32 = 1000;
```

## burst_to_one Reply Capture Logic

The current drain loop:
```rust
// OLD: drain ALL, discard
let mut drain_buf = [0u8; REPORT_LENGTH + 1];
for _ in 0..IN_DRAIN_MAX {
    match interface.read_timeout(&mut drain_buf, 0) {
        Ok(n) if n > 0 => continue,
        _ => break,
    }
}
true
```

The new reply capture + drain:
```rust
// NEW: capture FIRST reply with bounded timeout, then drain surplus
let mut reply: Option<Vec<u8>> = None;

// 1. Read first reply (bounded wait, not non-blocking)
let mut read_buf = [0u8; REPORT_LENGTH + 1];
match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
    Ok(n) if n > 0 => {
        reply = Some(read_buf[..n].to_vec());
    }
    _ => {} // Ok(0) = timeout, Err = read failure; reply stays None
}

// 2. Drain surplus IN-side reports (non-blocking, same as before)
let mut drain_buf = [0u8; REPORT_LENGTH + 1];
for _ in 0..IN_DRAIN_MAX {
    match interface.read_timeout(&mut drain_buf, 0) {
        Ok(n) if n > 0 => continue,
        _ => break,
    }
}

(true, reply)
```

## Key Design Decisions

1. **build_command_data is pure**: No HID I/O. Takes `&RunCommand`, returns
   `Vec<u8>`. Fully unit-testable. The caller (`run()`) appends nothing — ETX
   is already in the payload.

2. **parse_reply is pure**: Takes `&[u8]`, returns `CommandResponse`. Fully
   unit-testable. Handles all reply shapes including malformed/truncated.

3. **Typed commands reuse ALL existing transport machinery**: Same MatchKey,
   same cache, same burst-write, same retry. The only difference is what `data`
   contains. `send_raw_report` is command-agnostic.

4. **Reply is captured per-device, first-success wins**: In the multi-device case,
   `try_send_once` takes the reply from the first device that succeeded. This is
   correct for the single-keyboard case (the only realistic deployment).

5. **ListDevices returns CommandResponse::Timeout**: ListDevices prints to stdout
   and doesn't send over the wire. There's no reply. `Timeout` is the least-wrong
   semantic match (it means "no device reply was received").

6. **SendMessage still returns Legacy{matched}**: The reply's `response[0]` (0/1)
   is parsed by `parse_reply` into `CommandResponse::Legacy { matched }`.

7. **`run()` for typed commands maps None → Timeout**: If `send_raw_report`
   returns `Ok(None)` (no reply captured), `run()` returns `Ok(CommandResponse::Timeout)`.