# Research Notes — P1.M3.T1.S1

**Item**: Change `burst_to_one` return type and add bounded reply capture.
**Status of research session**: baseline + code verified against live repo.

---

## F0 — Verified baseline (run on the live repo)

```
cargo build          → Finished, 0 warnings
cargo clippy --lib   → 0 warnings
cargo fmt --check    → exit 0
cargo test --lib     → test result: ok. 57 passed; 0 failed; 0 ignored
```

- core.rs holds **37** `#[test]` fns; lib.rs holds **20** ⇒ **57 total**.
- **P1.M2.T2.S2 is ALREADY MERGED** — the 8 `parse_reply_*` edge-case tests are
  present (core.rs lines 1046–1144). So the baseline for THIS subtask is **57**,
  NOT 49. (The P1.M2.T2.S2 PRP targeted 49→57; that landed.)
- No `rustfmt.toml` / `clippy.toml` exist ⇒ default toolchain configs.

## F1 — Current `burst_to_one` (src/core.rs:248, VERBATIM)

Signature is `-> bool`. Body structure (exact):

```rust
fn burst_to_one(interface: &HidDevice, data: &[u8], batch_count: usize, verbose: bool) -> bool {
    let mut request_data = [0u8; REPORT_LENGTH + 1]; // stack array
    request_data[1] = 0x81;
    request_data[2] = 0x9F;

    for batch in 0..batch_count {
        // ... fill request_data[3..] with batch payload, verbose-log ...
        if let Err(e) = interface.write(&request_data) {
            if verbose { println!("Error on batch {}: {}", batch + 1, e); }
            return false;            // ← write-error early return
        }
    }

    // --- drain loop (non-blocking, IN_DRAIN_MAX=32 bounded) ---
    let mut drain_buf = [0u8; REPORT_LENGTH + 1];
    for _ in 0..IN_DRAIN_MAX {
        match interface.read_timeout(&mut drain_buf, 0) {
            Ok(n) if n > 0 => continue,
            _ => break,
        }
    }

    true                              // ← success
}
```

Doc comment (lines 236–247) currently says: "Burst-write `data` ... then drain any
pending IN-side reports. Returns `false` on the first write error." + the
burst-write-is-safe paragraph. **Must be updated** to document reply capture.

## F2 — The call site in `try_send_once` (src/core.rs:~215, VERBATIM)

```rust
            if burst_to_one(interface, data, batch_count, verbose) {
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
```

After `burst_to_one` returns `(bool, Option<Vec<u8>>)`, this call site won't type-
check. **Fix (step f)**: append `.0` to take only the success flag; the reply is
discarded here. Full reply PROPAGATION through `try_send_once` →
`send_raw_report` → `run()` is **P1.M3.T2.S1** (not this subtask).

## F3 — `REPLY_READ_TIMEOUT_MS` + the constants comment block (src/core.rs:11–38)

```rust
// The 5 command constants (...) now have a real consumer: `build_typed_payload`
// (P1.M1.T2.S1) references them ... Only RESPONSE_MARKER and
// REPLY_READ_TIMEOUT_MS still carry `#[allow(dead_code)]` — their consumers land
// in P1.M1.T3 (parse_reply + the reply reader).
...
/// Bounded timeout (ms) for reading the first reply after a burst.
/// Must be > 0 (unlike the drain's non-blocking timeout=0).
#[allow(dead_code)]
const REPLY_READ_TIMEOUT_MS: i32 = 1000;
```

- `REPLY_READ_TIMEOUT_MS` was added in P1.M1.T2.S1 with `#[allow(dead_code)]`
  (its consumer was deferred). THIS subtask IS that consumer.
- Once `burst_to_one` references it, **remove the `#[allow(dead_code)]`** and
  update the comment block to reflect that REPLY_READ_TIMEOUT_MS is now consumed
  by burst_to_one (P1.M3.T1.S1). Leaving the allow on a used const is a harmless
  but inaccurate maintenance smell; the codebase explicitly tracks this lifecycle
  in the comment, so keeping it accurate is consistent with project convention.
- Note: the existing comment is ALREADY slightly stale — RESPONSE_MARKER has NO
  `#[allow(dead_code)]` (parse_reply consumes it). Don't try to "fix" that
  unrelated line beyond what's needed for REPLY_READ_TIMEOUT_MS; just make the
  REPLY_READ_TIMEOUT_MS sentence accurate.

## F4 — `read_timeout` semantics (from architecture/external_deps.md)

```rust
device.read_timeout(buf: &mut [u8], timeout: i32) -> Result<usize, HidError>
// timeout = 0  => non-blocking poll
// timeout < 0 => blocking (no timeout)
// timeout > 0 => wait up to N ms
// Ok(0)   => timeout / no data (NOT an error)
// Ok(n>0) => n bytes read
// Err(_)  => read failure
```

⇒ Reply-capture logic:
- `Ok(n) if n > 0` → captured reply: `read_buf[..n].to_vec()` (n ≤ buf.len(), in bounds).
- `Ok(0)` (timeout) or `Err` (failure) → reply stays `None`.

This is the SAME `Ok(n) if n > 0` guard the existing drain loop uses, but with a
**bounded non-zero timeout** (`REPLY_READ_TIMEOUT_MS = 1000`) instead of `0`.

## F5 — Reconciling the item's "may not compile" OUTPUT note vs step (f)

The item has two statements that read as a contradiction:
- **Step (f)**: "Update the call site in try_send_once — this WILL cause a
  compile error ... Add a temporary `.0` access or handle the tuple. The full
  try_send_once evolution is P1.M3.T2.S1."
- **OUTPUT (4)**: "The crate may not compile until try_send_once (next subtask)
  is updated. That is expected — the dependency chain is burst_to_one ->
  try_send_once -> send_raw_report -> run()."

**Resolution**: step (f) is the explicit, actionable instruction. Adding `.0` at
the call site makes the crate COMPILE. "Add a temporary `.0` access" is literally
a compile fix — you cannot add it and leave the crate broken. The OUTPUT note
describes the LOGICAL/intermediate state (the `Option<Vec<u8>>` reply is captured
but DISCARDED at the call site until try_send_once evolves), NOT a literal
compile failure. **PRP directive: make the crate compile** (build + 57 tests
pass). A non-compiling increment can't run its validation gate — that defeats the
PRP model. The PRP calls this out explicitly so the implementer is not confused.

## F6 — `read_buf` slicing is in-bounds (no panic risk)

`read_timeout(&mut read_buf, …)` writes at most `read_buf.len()` =
`REPORT_LENGTH + 1 = 33` bytes and returns `n ≤ read_buf.len()`. So
`read_buf[..n]` is always valid; `.to_vec()` cannot panic. No defensive `.get()`
needed here (unlike `parse_reply`, where the firmware may send a SHORTER buffer).

## F7 — Designed new `burst_to_one` body (target state)

```rust
fn burst_to_one(
    interface: &HidDevice,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> (bool, Option<Vec<u8>>) {
    let mut request_data = [0u8; REPORT_LENGTH + 1];
    request_data[1] = 0x81;
    request_data[2] = 0x9F;

    for batch in 0..batch_count {
        // ... UNCHANGED write loop ...
        if let Err(e) = interface.write(&request_data) {
            if verbose { println!("Error on batch {}: {}", batch + 1, e); }
            return (false, None);            // ← (b) write error ⇒ (false, None)
        }
    }

    // (c) Capture the FIRST device reply with a bounded timeout (v0.3.0).
    let mut reply: Option<Vec<u8>> = None;
    let mut read_buf = [0u8; REPORT_LENGTH + 1];
    match interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) {
        Ok(n) if n > 0 => {
            reply = Some(read_buf[..n].to_vec());
            if verbose {                     // ← (g) verbose reply-length log
                println!("Captured device reply: {} bytes", n);
            }
        }
        _ => {} // Ok(0)=timeout, Err=read failure ⇒ reply stays None
    }

    // (d) Drain surplus IN-side reports (UNCHANGED non-blocking loop).
    let mut drain_buf = [0u8; REPORT_LENGTH + 1];
    for _ in 0..IN_DRAIN_MAX {
        match interface.read_timeout(&mut drain_buf, 0) {
            Ok(n) if n > 0 => continue,
            _ => break,
        }
    }

    (true, reply)                            // ← (e)
}
```

## F8 — Scope boundary (what NOT to touch)

- `send_raw_report` (src/core.rs:107) stays `Result<(), QmkError>` — its evolution
  to `Result<Option<Vec<u8>>, QmkError>` is **P1.M3.T2.S1**.
- `run()` in lib.rs stays exactly as-is (still calls `send_raw_report(...)?` and
  returns placeholders). NOT this subtask.
- `try_send_once`'s own signature stays `Result<SendOutcome, QmkError>` — only the
  internal `burst_to_one(...)` call site gets `.0`. The full
  `Result<(SendOutcome, Option<Vec<u8>>), QmkError>` change is P1.M3.T2.S1.
- No new tests added (burst_to_one needs real hardware; item explicitly says
  verify via compilation + existing tests still passing).
- No lib.rs / error.rs / main.rs / Cargo.toml changes.

## F9 — Validation gate (this subtask)

```
cargo build         → 0 warnings
cargo clippy --lib  → 0 warnings
cargo fmt --check   → exit 0
cargo test --lib    → 57 passed; 0 failed   (NO new tests; count unchanged)
```