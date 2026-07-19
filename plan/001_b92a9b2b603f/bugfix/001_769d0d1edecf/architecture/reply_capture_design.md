# Reply Capture & Trait Abstraction Design

## Problem Statement

The firmware sends **one 32-byte reply per HID report processed**. For an N-report
message, the device emits N replies where:
- Replies 1..N-1 have `response[0] == 0` (incomplete message, no ETX seen).
- Reply N (the ETX report) carries the real result: legacy match-bool (0/1) or
  a typed `0x51...` reply.

The current `burst_to_one` (core.rs:305) captures the **FIRST** reply and drains
the rest — returning the wrong result for any payload > 30 bytes (≥ 2 reports).

## Fix Architecture

### Step 1: RawHid Trait (testability enabler)

```rust
// core.rs — new private trait
pub(crate) trait RawHid {
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError>;
    fn read_timeout(&self, buf: &mut [u8], timeout: i32) -> Result<usize, hidapi::HidError>;
}

impl RawHid for hidapi::HidDevice {
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError> {
        hidapi::HidDevice::write(self, data)
    }
    fn read_timeout(&self, buf: &mut [u8], timeout: i32) -> Result<usize, hidapi::HidError> {
        hidapi::HidDevice::read_timeout(self, buf, timeout)
    }
}
```

### Step 2: Genericize burst_to_one

```rust
fn burst_to_one<T: RawHid>(
    interface: &T,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> (bool, Option<Vec<u8>>)
```

`try_send_once` (caller) passes `&HidDevice` — Rust infers `T = HidDevice`. No
change to `try_send_once`, `DeviceCache`, or `send_raw_report`.

### Step 3: Pre-Send Drain (Issue 3)

Add BEFORE the write loop (same non-blocking drain pattern as existing post-capture):

```rust
// Drain stale IN-side replies left by a prior send (Issue 3).
let mut stale_buf = [0u8; REPORT_LENGTH + 1];
for _ in 0..IN_DRAIN_MAX {
    match interface.read_timeout(&mut stale_buf, 0) {
        Ok(n) if n > 0 => continue,
        _ => break,
    }
}
```

### Step 4: Capture-Last-Reply (Issue 1)

Replace the single bounded read (core.rs:346) with a loop that reads up to
`batch_count` replies, keeping the LAST non-empty one:

```rust
let mut reply: Option<Vec<u8>> = None;
let mut read_buf = [0u8; REPORT_LENGTH + 1];

// The firmware sends one reply per report. For a multi-report message,
// only the LAST reply (ETX report) carries the real result. Read up to
// batch_count replies, keeping the last non-empty one (Issue 1).
for _ in 0..batch_count.max(1) {
    match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
        Ok(n) if n > 0 => {
            reply = Some(read_buf[..n].to_vec());  // overwrite → keep last
        }
        _ => break,  // timeout/error → stop (have all replies available)
    }
}
```

The existing surplus drain (core.rs:370) stays as a safety net for any
straggler replies.

### Edge Cases

| Scenario | batch_count | Replies emitted | Captured |
|----------|-------------|-----------------|----------|
| Single-report legacy | 1 | [match] | match ✅ |
| 2-report legacy | 2 | [0, match] | match ✅ |
| N-report legacy | N | [0×(N-1), match] | match ✅ |
| Single-report typed | 1 | [0x51...] | 0x51... ✅ |
| 2-report typed (AHC) | 2 | [0, 0x51...] | 0x51... ✅ |
| No device reply | any | [] | None → Timeout ✅ |

## FakeHid Test Double

```rust
#[cfg(test)]
struct FakeHid {
    /// Replies returned by read_timeout BEFORE any write() call (stale data).
    pre_write_replies: std::cell::RefCell<std::collections::VecDeque<Vec<u8>>>,
    /// Replies returned by read_timeout AFTER the first write() call.
    post_write_replies: std::cell::RefCell<std::collections::VecDeque<Vec<u8>>>,
    /// Whether write() has been called at least once.
    written: std::cell::Cell<bool>,
    /// Recorded write() calls for assertion.
    writes: std::cell::RefCell<Vec<Vec<u8>>>,
}

#[cfg(test)]
impl RawHid for FakeHid {
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError> {
        self.writes.borrow_mut().push(data.to_vec());
        self.written.set(true);
        Ok(data.len())
    }
    fn read_timeout(&self, buf: &mut [u8], _timeout: i32) -> Result<usize, hidapi::HidError> {
        let queue = if self.written.get() {
            &self.post_write_replies
        } else {
            &self.pre_write_replies
        };
        match queue.borrow_mut().pop_front() {
            Some(reply) => {
                let n = reply.len().min(buf.len());
                buf[..n].copy_from_slice(&reply[..n]);
                Ok(n)
            }
            None => Ok(0),  // empty queue ⇒ timeout semantics (matches hidapi)
        }
    }
}
```

### Test: Multi-Report Capture (Issue 1 regression)

```rust
#[test]
fn burst_to_one_captures_last_reply_for_multi_report() {
    // 2-report payload: firmware emits [0, 1] — the ETX reply (1) is the real result.
    let payload = vec![0u8; 31];  // 31 bytes ⇒ 2 reports (PAYLOAD_PER_REPORT=30)
    let batch_count = batches_for(&payload);  // == 2

    let mut post = VecDeque::new();
    post.push_back(vec![0u8; 33]);   // reply to report 1 (intermediate, [0])
    post.push_back({ let mut r = vec![0u8; 33]; r[0] = 1; r });  // reply to report 2 (ETX, [1])

    let fake = FakeHid {
        pre_write_replies: RefCell::new(VecDeque::new()),
        post_write_replies: RefCell::new(post),
        written: Cell::new(false),
        writes: RefCell::new(vec![]),
    };

    let (success, reply) = burst_to_one(&fake, &payload, batch_count, false);
    assert!(success);
    let reply = reply.expect("should have captured a reply");
    assert_eq!(reply[0], 1, "must capture the ETX-report reply, not the intermediate [0]");
    assert_eq!(fake.writes.borrow().len(), 2, "must write exactly 2 reports");
}
```

### Test: Pre-Send Drain (Issue 3 regression)

```rust
#[test]
fn burst_to_one_drains_stale_replies_before_send() {
    // Single-report payload: firmware emits [1] (match). But a stale [0] reply
    // from a prior send is sitting in the IN buffer.
    let payload = vec![0u8; 10];  // 1 report

    let mut pre = VecDeque::new();
    pre.push_back(vec![0u8; 33]);  // stale reply from previous send

    let mut post = VecDeque::new();
    post.push_back({ let mut r = vec![0u8; 33]; r[0] = 1; r });  // fresh match reply

    let fake = FakeHid {
        pre_write_replies: RefCell::new(pre),
        post_write_replies: RefCell::new(post),
        written: Cell::new(false),
        writes: RefCell::new(vec![]),
    };

    let (success, reply) = burst_to_one(&fake, &payload, 1, false);
    assert!(success);
    let reply = reply.expect("should have captured a reply");
    assert_eq!(reply[0], 1, "must capture the fresh reply, not the stale [0]");
}
```

## Impact on Existing Tests

All 65 existing tests exercise **pure functions** (`device_matches`, `batches_for`,
`build_command_data`, `parse_reply`, `parse_hex_or_decimal`, CLI parsing). None
touch `burst_to_one` directly. The trait abstraction and capture fix do not
change any pure function signatures, so **zero existing tests break**.

The `run()` tests in lib.rs use bogus VID/PID (`Some(0xDEAD)`/`Some(0xBEEF)`)
which return `DeviceNotFound` before reaching `burst_to_one` — unaffected.