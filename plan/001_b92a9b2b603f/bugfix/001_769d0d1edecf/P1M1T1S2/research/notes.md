# Research Notes — P1.M1.T1.S2: Genericize burst_to_one over impl RawHid

## Task (from item description)

Change `burst_to_one`'s first parameter from a concrete `&HidDevice` to a generic
`&T` bounded by the `RawHid` trait (delivered by sibling S1):

```rust
// BEFORE
fn burst_to_one(interface: &HidDevice, data: &[u8], batch_count: usize, verbose: bool)
    -> (bool, Option<Vec<u8>>)

// AFTER
fn burst_to_one<T: RawHid>(interface: &T, data: &[u8], batch_count: usize, verbose: bool)
    -> (bool, Option<Vec<u8>>)
```

Body UNCHANGED. Caller `try_send_once` UNCHANGED (T=HidDevice inferred). DOCS:
none (private fn, pub(crate) trait). The purpose is a **testability seam** — this
unblocks `FakeHid` (P1.M1.T2.S1) and the Issue-1/Issue-3 fixes (T2/T3).

## CRITICAL FINDING #1 — S1 has ALREADY landed in the working tree

The plan status said S1 was "Implementing", but the working tree shows S1's
deliverable is fully present:

- `pub(crate) trait RawHid { fn write(...); fn read_timeout(...); }` — `src/core.rs:13`
- `impl RawHid for hidapi::HidDevice { ... }` — `src/core.rs:21`, using the
  verified fully-qualified-by-type delegation:
  - `hidapi::HidDevice::write(self, data)`
  - `hidapi::HidDevice::read_timeout(self, buf, timeout)`
- S1's forward-looking doc note appended to `burst_to_one`'s doc comment — `src/core.rs:336`

**Implication for S2:** do NOT recreate or modify the trait/impl. S2 CONSUMES
them. The `RawHid` symbol is already in scope in `core.rs` (same module).

## CRITICAL FINDING #2 — line numbers in the contract are STALE (S1 shifted them)

The item description references PRE-S1 line numbers. After S1 inserted the
~28-line trait+impl block after the imports (lines 4–31), everything shifted down:

| Item | Contract (pre-S1) | Actual now (post-S1) |
|---|---|---|
| `fn try_send_once` | 207 | ~227 |
| `burst_to_one(...)` CALL inside try_send_once | 241 | **269** |
| `fn burst_to_one` DEFINITION | 305 | **337** |

**Action:** the PRP tells the implementer to LOCATE these by SIGNATURE/content,
not by the contract's line numbers. (rust-goto on `fn burst_to_one` is robust.)

## CRITICAL FINDING #3 — the transitional dead_code warning is LIVE right now

`cargo build` (verified) currently emits:
```
warning: trait `RawHid` is never used
warning: `qmk_notifier` (lib) generated 1 warning
```
This is S1's EXPECTED transient state (the trait has no non-test consumer yet).

**S2's genericization is exactly what makes the trait "used" in non-test code.**
Therefore the HEADLINE success signal for S2 is: `cargo build` → **ZERO
warnings**. If the warning persists after S2, the genericization did not actually
take effect (wrong function edited, T shadowed, etc.). This zero-warnings gate is
the strongest cheap verification available.

## Why the body compiles UNCHANGED (verified by reading the current body)

`burst_to_one`'s body makes exactly three method calls on `interface`:
1. `interface.write(&request_data)`  — `request_data: [u8; 33]`; `&request_data`
   coerces `&[u8;33] → &[u8]`. With `interface: &T, T: RawHid`, this resolves to
   `RawHid::write`. ✓ (identical coercion to the pre-generic `&HidDevice` form)
2. `interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS)` —
   `read_buf: [u8;33]`, `&mut read_buf → &mut [u8]`; `REPLY_READ_TIMEOUT_MS: i32`
   (const = 1000, line 69). Resolves to `RawHid::read_timeout`. ✓
3. `interface.read_timeout(&mut drain_buf, 0)` — `drain_buf: [u8;33]`; `0` is an
   `i32` literal matching `timeout: i32`. Resolves to `RawHid::read_timeout`. ✓

→ The body needs NO edit. The method-call syntax `interface.write(...)` /
`interface.read_timeout(...)` resolves through the trait bound automatically.

**Why NO recursion hazard in burst_to_one's body:** the infinite-recursion risk
documented in S1's gotchas existed ONLY inside `impl RawHid for HidDevice { ... }`
(where a bare `self.write` would call the method being defined). Inside
`burst_to_one`, `interface` is `&T` (the caller's concrete type), and for the one
real call site `T = HidDevice`, so `interface.write(...)` → `RawHid::write` →
S1's FQS body → real hidapi. No loop. The body is safe as-is.

## Why the caller (try_send_once) needs NO change (type inference)

At `src/core.rs:269`:
```rust
let (success, reply) = burst_to_one(interface, data, batch_count, verbose);
```
where `interface: &HidDevice` (from `for (_, interface) in devices.iter()` with
`devices: &Vec<HidDevice>`). Rust infers `T = HidDevice` from the argument type,
and checks the bound `HidDevice: RawHid` (satisfied by S1's impl). The verbose
block's `interface.get_device_info()` call in try_send_once is on the CONCRETE
`&HidDevice` (a HidDevice-specific method) and is outside burst_to_one —
unaffected by the genericization. So try_send_once is byte-for-byte unchanged.

## No test calls burst_to_one directly (verified)

`grep burst_to_one` over `src/core.rs` shows the only CODE reference outside the
definition is the try_send_once call site (line 269). All other hits are doc
comments (`/// ... burst_to_one ...`). The 39 tests in core.rs + 26 in lib.rs
exercise burst_to_one only INDIRECTLY through `run()`/`send_raw_report`, and only
on the pure functions (`build_command_data`, `parse_reply`, `device_matches`,
`batches_for`, CLI) — none construct or call burst_to_one. So genericizing adds
zero test churn; all 65 tests pass unchanged. **S2 adds NO test** (the FakeHid
double + multi-report regression test are P1.M1.T2.S1 / P1.M1.T2.S3 — later).

## DOCS tension — S1's forward-looking note becomes stale on S2 landing

S1 appended to burst_to_one's doc comment (line 336):
> "...this function is scheduled to be genericized over `impl RawHid` in the next
> subtask to enable a `FakeHid` test double."

The instant S2 lands, this statement is FALSE (it IS now generic). The item says
"DOCS: none" (no separate docs deliverable), but the S1 PRP itself framed this
note as forward-looking-pending-S2. **Decision:** S2 updates that ONE doc line's
tense (scheduled→now) to keep the codebase truthful. This is a 1-line edit,
justified by accuracy, NOT scope creep — and it's the only doc touch. It does
not violate "DOCS: none" (no new doc files, no public-API surface change).

## Test count math (verified)

- `src/core.rs`: 39 `#[test]` fns
- `src/lib.rs`: 26 `#[test]` fns
- `src/error.rs`, `src/main.rs`: 0
- `cargo test` total: **65** (matches item description + prd_snapshot §Testing).

S2 adds 0 tests → 65 after S2.

## Validation gates (verified commands)

- `cargo build` → **0 warnings** (dead_code CLEARED — the headline gate).
- `cargo test` → "65 passed; 0 failed".
- `cargo clippy --all-targets` → no NEW lint (the `<T: RawHid>(interface: &T)`
  explicit-generic form is the clippy-PREFERRED shape vs `impl RawHid` in params;
  `clippy::impl_trait_in_params` actually flags the impl-trait form, not this one).
- `cargo fmt --check` → exit 0 (no rustfmt.toml → default style).

## File boundary

Only `src/core.rs` is modified:
- Task 2: edit `burst_to_one` signature (line 338: `interface: &HidDevice` →
  add `<T: RawHid>` to fn generics, change param to `interface: &T`).
- Task 3: fix the S1 forward-looking doc note's tense (line 336).

No other file. No Cargo.toml change. No new deps. No new tests. No lib.rs re-export
change (RawHid stays pub(crate), burst_to_one stays private).