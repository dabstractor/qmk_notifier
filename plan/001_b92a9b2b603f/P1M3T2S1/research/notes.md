# Research Notes — P1.M3.T2.S1 (propagate first reply through try_send_once → send_raw_report)

This subtask plumbs the FIRST device reply (captured by `burst_to_one` in
P1.M3.T1.S1) up two more levels of the call stack so it reaches `run()`. Only
**`src/core.rs`** is touched. `lib.rs`/`error.rs`/`main.rs` are NOT touched.

---

## F1 — verbatim CURRENT `send_raw_report` (src/core.rs:106-153)

```rust
pub fn send_raw_report(
    data: &[u8],
    vendor_id: Option<u16>,
    product_id: Option<u16>,
    usage_page: u16,
    usage: u16,
    verbose: bool,
) -> Result<(), QmkError> {                 // <-- CHANGES to Result<Option<Vec<u8>>, QmkError>
    let key = MatchKey {
        vendor_id,
        product_id,
        usage_page,
        usage,
    };
    let batch_count = batches_for(data);

    if verbose {
        println!("Request data ({} bytes):", data.len());
        println!("{:?}", data);
    }

    for attempt in 0..=SEND_RETRIES {
        match try_send_once(&key, data, batch_count, verbose)? {
            SendOutcome::AllSucceeded => return Ok(()),                       // <-- Ok(reply)
            SendOutcome::Partial { succeeded, failed } => {
                return Err(QmkError::PartialSendError { succeeded, failed });
            }
            SendOutcome::TotalFailure if attempt < SEND_RETRIES => {
                // PRESERVE THIS BLOCK (item pseudocode dropped it):
                if verbose {
                    println!(
                        "All sends failed; rebuilding device cache and retrying (attempt {}/{}).",
                        attempt + 2,
                        SEND_RETRIES + 1
                    );
                }
                continue;
            }
            SendOutcome::TotalFailure => {
                return Err(QmkError::SendReportError(hidapi::HidError::HidApiError {
                    message: "Failed to send to any devices".to_string(),
                }));
            }
        }
    }

    unreachable!("the retry loop always returns on its first or final iteration")
}
```

No doc comment exists above `send_raw_report` (verified: src/core.rs:107 is
`pub fn send_raw_report(` immediately after the `SEND_RETRIES` const block).
So the item's DOCS step ("Update the send_raw_report doc comment") is an **ADD**,
not an edit.

## F2 — verbatim CURRENT `try_send_once` (src/core.rs:170-237)

```rust
fn try_send_once(
    key: &MatchKey,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> Result<SendOutcome, QmkError> {        // <-- CHANGES to Result<(SendOutcome, Option<Vec<u8>>), QmkError>
    let mut cache = lock_cache();
    ensure_cache(&mut cache, key, verbose)?;

    let device_count = cache.as_ref().expect("cache populated").devices.len();
    if verbose {
        println!("Found {} matching device(s).", device_count);
    }

    let mut succeeded = 0usize;
    let mut failed = 0usize;

    {
        let devices: &Vec<HidDevice> = &cache.as_ref().expect("cache populated").devices;
        for (device_idx, interface) in devices.iter().enumerate() {
            if verbose {
                let device_path = match interface.get_device_info() {
                    Ok(info) => format!("{:?}", info.path()),
                    Err(_) => "N/A".to_string(),
                };
                println!(
                    "Sending to device {}/{}: Path: {}",
                    device_idx + 1,
                    device_count,
                    device_path
                );
            }

            // THIS COMMENT + .0 ACCESS GETS REPLACED:
            if burst_to_one(interface, data, batch_count, verbose).0 {
                succeeded += 1;
            } else {
                failed += 1;
                if verbose {
                    println!(
                        "Failed to send message to device {}/{}.",
                        device_idx + 1,
                        device_count
                    );
                }
            }
        }
    }

    // cache invalidation block — UNCHANGED
    if failed > 0 {
        *cache = None;
        if verbose {
            println!("Invalidating device cache after a write error.");
        }
    }

    Ok(if succeeded == 0 {                  // <-- becomes Ok((outcome, first_reply))
        SendOutcome::TotalFailure
    } else if failed > 0 {
        SendOutcome::Partial { succeeded, failed }
    } else {
        SendOutcome::AllSucceeded
    })
}
```

`enum SendOutcome` (src/core.rs:158-163) is UNCHANGED — only its wrapping changes.

## F3 — EMPIRICAL PROOF: `run()` in lib.rs compiles UNCHANGED

`run()` calls `send_raw_report(...)?;` as a statement at lib.rs:336 and lib.rs:368.
After the return type becomes `Result<Option<Vec<u8>>, QmkError>`, the `?`
evaluates to `Option<Vec<u8>>` (a `#[must_use]` type) which the statement
discards. Question: does this trigger `unused_must_use`?

**Tested in /tmp/must_use_test (cargo 1.x, clean build):** the pattern
`inner()?;` where `inner: fn() -> Result<Option<Vec<u8>>, ()>` produces **NO
`unused_must_use` warning** under `cargo build` NOR `RUSTFLAGS="-W unused"`, and
no clippy warning (the only clippy note was `Result<_, ()>` style, irrelevant
since the real crate uses `QmkError`). The `?` operator counts as a use of the
`Result`; the Ok payload is consumed by control flow, not a bare drop.

**Conclusion:** lib.rs needs NO edit to compile. This keeps the subtask scoped to
`src/core.rs` exactly as the item says ("3. LOGIC: In src/core.rs"). The stale
inline comment at lib.rs:332 ("send_raw_report STILL returns Result<(), QmkError>")
and the doc-comment placeholder notes in `run()` become stale, but lib.rs is
out of scope; they are corrected in P1.M3.T3.S1 when `run()` evolves to consume
the reply. (P1.M3.T3.S1 owns `run()`.)

## F4 — CRITICAL GOTCHA: preserve the verbose println in the retry arm

The item's pseudocode for the `send_raw_report` retry arm shows:
```rust
(SendOutcome::TotalFailure, _) if attempt < SEND_RETRIES => { continue; }
```
This DROPS the existing verbose logging block. That would be a silent behavior
regression (rebuild messages vanish under `--verbose`). The implementer MUST
preserve the `if verbose { println!("All sends failed; rebuilding device cache
and retrying (attempt {}/{}).", attempt + 2, SEND_RETRIES + 1); }` block in the
retry arm. The item's pseudocode is ILLUSTRATIVE of the tuple destructuring, not
a mandate to delete logging. Only the match-arm HEAD changes (add `, _` tuple
binding / `, reply` in the AllSucceeded arm) and the `Ok(())` → `Ok(reply)`
return.

## F5 — doc comment: it's an ADD (no existing doc on send_raw_report)

Verified src/core.rs:107: there is no `///` block above `pub fn send_raw_report`.
The item's DOCS step ("[Mode A] Update the send_raw_report doc comment") therefore
means ADD a new doc comment documenting:
- what the function does (burst-send data to all matching devices, with retry);
- the NEW return: `Result<Option<Vec<u8>>, QmkError>` where `Ok(Some(bytes))` =
  the first device's reply (captured by burst_to_one's bounded read), `Ok(None)` =
  no reply captured within `REPLY_READ_TIMEOUT_MS` (timeout / read failure /
  legacy device), `Err(...)` = transport failure (DeviceNotFound / PartialSend /
  total-failure-after-retry). Mention the reply is decoded downstream by
  `parse_reply` (P1.M3.T3.S1).

## F6 — run() call sites that make lib.rs compile-unchanged (lib.rs:336, 368)

Both arms of `run()` use the discarded-result pattern:
- lib.rs:336 — `SendMessage` arm
- lib.rs:368 — typed-command or-pattern arm
Both are `send_raw_report(...)?;` statements. Per F3 these compile unchanged.
`run()` still returns `CommandResponse::Legacy{matched:true}` / `Timeout`
placeholders; reply *consumption* is P1.M3.T3.S1. This subtask only makes the
reply AVAILABLE to run(); it does not change run().

## F7 — designed TARGET bodies

### try_send_once (target)

```rust
fn try_send_once(
    key: &MatchKey,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> Result<(SendOutcome, Option<Vec<u8>>), QmkError> {
    let mut cache = lock_cache();
    ensure_cache(&mut cache, key, verbose)?;

    let device_count = cache.as_ref().expect("cache populated").devices.len();
    if verbose {
        println!("Found {} matching device(s).", device_count);
    }

    let mut succeeded = 0usize;
    let mut failed = 0usize;
    let mut first_reply: Option<Vec<u8>> = None;   // <-- NEW: first-success reply

    {
        let devices: &Vec<HidDevice> = &cache.as_ref().expect("cache populated").devices;
        for (device_idx, interface) in devices.iter().enumerate() {
            if verbose {
                // ... unchanged device-path verbose log ...
            }

            // REPLACED: was burst_to_one(...).0 ; now destructures + captures reply
            let (success, reply) = burst_to_one(interface, data, batch_count, verbose);
            if success {
                succeeded += 1;
                if first_reply.is_none() {
                    first_reply = reply;   // first successful device wins
                }
            } else {
                failed += 1;
                if verbose {
                    println!("Failed to send message to device {}/{}.", device_idx + 1, device_count);
                }
            }
        }
    }

    // unchanged cache invalidation
    if failed > 0 {
        *cache = None;
        if verbose {
            println!("Invalidating device cache after a write error.");
        }
    }

    let outcome = if succeeded == 0 {
        SendOutcome::TotalFailure
    } else if failed > 0 {
        SendOutcome::Partial { succeeded, failed }
    } else {
        SendOutcome::AllSucceeded
    };
    Ok((outcome, first_reply))
}
```

Why `let outcome = ...; Ok((outcome, first_reply))` and not inline `Ok((if.., first_reply))`:
the `let` binding is used inside a tuple (not directly returned), so clippy's
`let_and_return` does NOT fire. Verified-safe under default clippy.

### send_raw_report (target)

```rust
/// ... new doc comment (see F5) ...
pub fn send_raw_report(
    data: &[u8],
    vendor_id: Option<u16>,
    product_id: Option<u16>,
    usage_page: u16,
    usage: u16,
    verbose: bool,
) -> Result<Option<Vec<u8>>, QmkError> {
    // ... unchanged key/batch_count/verbose block ...

    for attempt in 0..=SEND_RETRIES {
        match try_send_once(&key, data, batch_count, verbose)? {
            (SendOutcome::AllSucceeded, reply) => return Ok(reply),
            (SendOutcome::Partial { succeeded, failed }, _) => {
                return Err(QmkError::PartialSendError { succeeded, failed });
            }
            (SendOutcome::TotalFailure, _) if attempt < SEND_RETRIES => {
                if verbose {
                    println!(
                        "All sends failed; rebuilding device cache and retrying (attempt {}/{}).",
                        attempt + 2,
                        SEND_RETRIES + 1
                    );
                }
                continue;
            }
            (SendOutcome::TotalFailure, _) => {
                return Err(QmkError::SendReportError(hidapi::HidError::HidApiError {
                    message: "Failed to send to any devices".to_string(),
                }));
            }
        }
    }

    unreachable!("the retry loop always returns on its first or final iteration")
}
```

## F8 — first-reply propagation semantics (why "first success wins")

Multi-device case (theoretical — single keyboard is the realistic deployment):
`try_send_once` iterates cached handles; each `burst_to_one` returns
`(success, reply)`. We capture the reply from the FIRST device whose burst
succeeded (`if first_reply.is_none() { first_reply = reply; }`). Subsequent
successful devices' replies are dropped (drained but not surfaced). This matches
architecture/transport_evolution.md §Key Design Decisions #4 ("Reply is captured
per-device, first-success wins"). The retry loop in `send_raw_report` only retries
on `TotalFailure` (zero succeeded); a partial send returns an error immediately,
so a captured reply on a partial send is discarded (never retried, never
surfaced) — consistent with PRD §14 invariant 4 ("a partial send is never
retried").

## F9 — validation gate (verified working in this repo)

- `cargo fmt` then `cargo fmt --check` → exit 0
- `cargo build` → 0 warnings
- `cargo clippy --lib` → 0 warnings
- `cargo test --lib` → **57 passed; 0 failed** (unchanged baseline — NO new tests;
  these functions need real HID hardware). The dispatch tests
  (`test_run_*_dispatches_to_send`) deterministically return `DeviceNotFound`
  (bogus VID/PID 0xDEAD/0xBEEF) which fails in `ensure_cache` BEFORE any reply is
  read, so they're unaffected by the propagation change.

grep pinpoints for Level-4:
- `grep -nE "fn try_send_once|-> Result<\(SendOutcome, Option<Vec<u8>>\), QmkError>" src/core.rs`
- `grep -nE "pub fn send_raw_report|-> Result<Option<Vec<u8>>, QmkError>" src/core.rs`
- `grep -nE "\(SendOutcome::AllSucceeded, reply\) => return Ok\(reply\)" src/core.rs`
- `grep -nE "let \(success, reply\) = burst_to_one" src/core.rs`
- `grep -nE "first_reply" src/core.rs`  (multiple matches now)