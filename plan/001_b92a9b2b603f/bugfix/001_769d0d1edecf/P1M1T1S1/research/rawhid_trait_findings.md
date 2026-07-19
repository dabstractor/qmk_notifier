# Research Notes — P1.M1.T1.S1 (Define RawHid trait and impl for hidapi::HidDevice)

## Codebase state (verified)
- Crate is at **v0.3.0** (Cargo.toml), NOT v0.2.1. The typed-command transport
  (build_command_data, parse_reply, CommandResponse, reply-capture in burst_to_one)
  is ALREADY implemented. **65 unit/integration tests pass** (`cargo test`).
- Files: `src/main.rs`, `src/lib.rs` (38KB), `src/core.rs` (52KB), `src/error.rs`.
- **Zero `trait` usage in src today** (`grep -rn 'trait ' src/` → nothing).
- **No `[dev-dependencies]`** in Cargo.toml.
- Baseline `cargo build` and `cargo clippy --all-targets` → **0 warnings**.

## CRITICAL: hidapi version drift (declared ≠ resolved)
- `Cargo.toml` says `hidapi = "2.4.1"` (caret = ">=2.4.1, <3.0.0").
- `Cargo.lock` resolves to **hidapi 2.6.6**. The actually-compiled signatures are
  from **2.6.6**, not 2.4.1. The contract's "2.4.1" references are stale.
- Source: `/home/dustin/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/hidapi-2.6.6/`

## CRITICAL: write/read_timeout are PRIVATE-TRAIT methods, not inherent
In hidapi 2.6.6, `HidDevice::write` / `read_timeout` are defined ONLY in
`impl HidDeviceBackendBase for HidDevice` (src/hidapi.rs:189). There are TWO
inherent `impl HidDevice {}` blocks (lines 152, 174) but they contain only
`from_raw` and `check_size` — NOT write/read_timeout.
- `HidDeviceBackendBase` is a **private trait** (`trait HidDeviceBackendBase {`
  at src/lib.rs:487, no `pub`, NOT re-exported). Only `pub use error::HidError;`
  is publicly re-exported. Downstream CANNOT name `HidDeviceBackendBase`.

### How the existing `interface.write()` call works despite the private trait
Empirically verified (throwaway lib crate against 2.6.6):
- `interface.write(&request_data)` (method-call syntax) COMPILES even though
  `HidDeviceBackendBase` is not imported. (Rust resolves the method because the
  impl is in scope via the type; the trait need not be named for method-call
  syntax when unambiguous.)
- `hidapi::HidDevice::write(self, data)` (fully-qualified-by-type syntax) ALSO
  COMPILES inside `impl RawHid for hidapi::HidDevice`. ✅

### Recursion analysis (the contract's warning)
The contract warns "use fully-qualified syntax to avoid infinite recursion."
Inside `impl RawHid for HidDevice { fn write(&self, d) { self.write(d) } }`:
- There is NO inherent `write` on HidDevice. `self.write(d)` would resolve to a
  trait method. `RawHid` is in scope (being defined). `HidDeviceBackendBase` is
  NOT imported, so it is NOT in scope. ⇒ `self.write(d)` resolves to
  `RawHid::write` ⇒ **infinite recursion at runtime**.
- Therfor the FQS-by-type form `hidapi::HidDevice::write(self, data)` is REQUIRED
  (it bypasses the RawHid method and calls the HidDeviceBackendBase impl). Verified
  to compile cleanly against 2.6.6.

### Return-type equivalence
hidapi declares `HidResult<usize>`; `pub type HidResult<T> = Result<T, HidError>`
(src/lib.rs:126). So the trait return type `Result<usize, hidapi::HidError>` is
EXACTLY equivalent to `HidResult<usize>`. No conversion needed in the impl body;
the hidapi call's `HidResult<usize>` flows straight out as `Result<usize, HidError>`.

## CRITICAL: transitional dead_code warning (S1 cannot be zero-warning)
S1 defines `pub(crate) trait RawHid` + `impl RawHid for HidDevice`, but burst_to_one
is NOT yet generic (that is S2). So the trait has NO non-test consumer in S1.
Empirically verified (throwaway LIB crate, mirroring S1 exactly):
- `cargo build` → `warning: trait \`RawHid\` is never used`
- `cargo test` → same warning (+ "methods write/read_timeout are never used")
- A `#[cfg(test)]` bound-assertion test does NOT silence it under `cargo build`
  (cfg(test) code isn't compiled in non-test builds) and only partially under test.
- NOTE: `burst_to_one` itself does NOT warn in the real codebase (it is consumed by
  try_send_once → send_raw_report → run). Only the new `RawHid` trait warns.

### Resolution options (evaluated)
- **A (recommended): accept the single transient warning.** It is EXPECTED and
  disappears the instant S2 genericizes burst_to_one (`fn burst_to_one<T: RawHid>`),
  because the trait then has a non-test consumer. Aligns with the codebase's
  "no #[allow(dead_code)] when an item has a real consumer" philosophy — here it
  genuinely has none YET by design. No annotations to add/remove.
- **B: `#[allow(dead_code)]` on the trait with a `// TODO(S2)` comment.** Silences
  the warning but (1) conflicts with the documented convention that avoids
  `#[allow(dead_code)]`, and (2) requires removal in S2 (churn). Use only if CI
  ever adds `-D warnings` (none exists today).
- **C (rejected): make the trait `pub`** to dodge dead_code. Violates the contract
  ("not exported from the crate") — RawHid is an internal testability seam.

→ PRP prescribes Option A and reframes the validation gate honestly.

## Placement
Contract offers two placements: "near the top of core.rs (after imports, before
constants)" OR "just above burst_to_one". Both are acceptable.
- Imports: core.rs lines 1–3 (`use crate::error::QmkError; use hidapi::{HidApi,
  HidDevice}; use std::sync::{LazyLock, Mutex, MutexGuard};`)
- Constants begin at line 5 (`// Default constants`) → line 6 (`pub const ...`).
- burst_to_one: doc comment ~line 279; signature `fn burst_to_one(interface:
  &HidDevice, data: &[u8], batch_count: usize, verbose: bool) -> (bool,
  Option<Vec<u8>>)` at line 305.
RECOMMEND: place the trait + impl right after the imports block (after line 3's
blank line, before `// Default constants`). This groups the HID abstraction with
the imports it depends on and keeps burst_to_one's vicinity focused. It is also
the natural "foundational type" location.

## Test-module visibility
- core.rs has its own `#[cfg(test)] mod tests { ... }` (consts at line 723+).
- `tests` is a CHILD module of `core`, so items defined at `core` scope — even
  PRIVATE ones — are visible to `core::tests` via `super::`. `pub(crate)` also
  works and matches the contract. Either way FakeHid (S2/S1.M1.T2.S1) can
  `impl super::RawHid for FakeHid`.
- No new `[dev-dependencies]` are needed for S1 itself (FakeHid uses only
  std::cell + std::collections::VecDeque, both in std). FakeHid lands in a later
  subtask anyway.

## Contract tension: DOCS vs OUTPUT (resolved)
- OUTPUT: "burst_to_one still takes &HidDevice at this point — genericization is
  the next subtask" → NOT generic in S1.
- DOCS: "Update the doc comment on burst_to_one to mention it is generic over RawHid."
- These conflict: you cannot truthfully doc a not-yet-generic fn as "generic."
- RESOLUTION: in S1, add a FORWARD-LOOKING note to burst_to_one's doc that is
  truthful about S1's state (concrete `&HidDevice`, which now impls `RawHid`) and
  signals the upcoming S2 genericization. Do NOT claim it is already generic.

## Build/test commands (verified working)
- `cargo build` → compiles, 0 warnings (baseline).
- `cargo test` → 65 passed (baseline).
- `cargo clippy --all-targets` → clean (baseline).
- After S1: `cargo build` compiles with exactly ONE expected `dead_code` warning
  on `RawHid` (Option A); `cargo test` → 65 passed (S1 adds no test; the trait
  bound-assertion test is optional and only partially silences the warning).