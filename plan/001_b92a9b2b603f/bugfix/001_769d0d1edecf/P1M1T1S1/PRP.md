# PRP — P1.M1.T1.S1: Define RawHid trait and impl for hidapi::HidDevice

---

## Goal

**Feature Goal**: Introduce a `pub(crate) trait RawHid` in `src/core.rs` that
abstracts the **only two** HID I/O methods `burst_to_one` calls — `write` and
`read_timeout` — and provide a blanket `impl RawHid for hidapi::HidDevice` that
delegates to hidapi's own methods. This is a **testability seam, not a behavior
change**: it exists so that the *next* subtasks (S2: genericize `burst_to_one`
over `impl RawHid`; then the Issue-1 multi-report capture fix and Issue-3
pre-send drain) can be unit-tested with a `FakeHid` test double instead of
requiring a physical QMK keyboard. No existing call site, signature, or test
changes in this subtask.

**Deliverable**: A `pub(crate) trait RawHid { fn write(...); fn read_timeout(...); }`
and `impl RawHid for hidapi::HidDevice { ... }` in `src/core.rs`, plus a small
forward-looking note appended to `burst_to_one`'s doc comment. The crate still
compiles and all existing tests still pass. `burst_to_one` is **untouched** (it
keeps its concrete `interface: &HidDevice` parameter — genericization is S2).

**Success Definition**:
- `cargo build` compiles. (See *Transitional Warning* below: exactly ONE expected
  `dead_code` warning on `RawHid` is acceptable in S1 and resolves in S2.)
- `cargo test` → **65 passed, 0 failed** (no new test added by S1; nothing regressed).
- `cargo clippy --all-targets` → no NEW clippy warnings attributed to this change.
- `grep -rn 'pub(crate) trait RawHid' src/core.rs` and `grep -rn 'impl RawHid for hidapi::HidDevice' src/core.rs` both hit.
- `burst_to_one`'s signature is byte-for-byte unchanged.

## Why

- This is the **foundational type** for fixing two critical/major defects from the
  bug investigation (see `prd_snapshot.md` §"Critical Issues"):
  - **Issue 1 (Critical)**: `burst_to_one` captures the *first* reply instead of
    the *last* (ETX-report) reply for multi-report payloads, returning the wrong
    `CommandResponse` for every payload > 30 bytes.
  - **Issue 3 (Major)**: no pre-send IN-buffer drain → stale replies bleed across
    rapid sequential sends.
- Both fixes live inside `burst_to_one`, which today takes a concrete
  `&hidapi::HidDevice`. Real hardware is required to exercise it, so the bug could
  only be proven with a live keyboard (per `TEST_RESULTS.md`). To get a **regression
  test**, `burst_to_one` must become generic over a trait (`RawHid`) so a `FakeHid`
  double (next milestone) can stand in for the device.
- Defining the trait + blanket impl **first, in isolation** keeps the dependency
  chain clean (design doc `reply_capture_design.md` §Step 1 → §Step 2 → §Step 3/4)
  and produces compilable, reviewable code with no behavior risk.

## What

Add the following block to `src/core.rs` (placement: after the imports, before the
`// Default constants` comment — see Implementation Tasks). This exact code was
**compiled and verified** against the resolved hidapi version (see *Known Gotchas*):

```rust
/// Minimal HID I/O surface used by [`burst_to_one`]: the two methods it actually
/// calls on an open HID handle. Abstracting these behind a trait lets `burst_to_one`
/// be genericized over `impl RawHid` (next subtask) so its reply-capture logic can
/// be unit-tested with a `FakeHid` double instead of requiring a physical keyboard.
///
/// `pub(crate)`: an internal testability seam — intentionally NOT exported from the
/// crate. The blanket impl below delegates to `hidapi::HidDevice` unchanged, so
/// runtime behavior is identical to calling hidapi directly.
pub(crate) trait RawHid {
    /// Write a raw HID OUT report. Mirrors `hidapi::HidDevice::write`.
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError>;
    /// Read one IN report, blocking up to `timeout` ms. `Ok(0)` = timeout/no-data
    /// (NOT an error); `Ok(n>0)` = real read. Mirrors `hidapi::HidDevice::read_timeout`.
    fn read_timeout(&self, buf: &mut [u8], timeout: i32) -> Result<usize, hidapi::HidError>;
}

impl RawHid for hidapi::HidDevice {
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError> {
        // Fully-qualified-by-type: REQUIRED. A bare `self.write(data)` would resolve
        // to `RawHid::write` (the method we are defining) and recurse infinitely —
        // see "Known Gotchas". `hidapi::HidDevice::write` calls hidapi's real impl.
        hidapi::HidDevice::write(self, data)
    }
    fn read_timeout(&self, buf: &mut [u8], timeout: i32) -> Result<usize, hidapi::HidError> {
        hidapi::HidDevice::read_timeout(self, buf, timeout)
    }
}
```

And append a forward-looking note to `burst_to_one`'s existing doc comment (the
block that begins `/// Burst-write \`data\` to a single device...`, ~line 279).
**Do not** claim it is already generic (it is not, until S2). Add one line such as:

```text
/// HID I/O note: the interface this operates on (`&HidDevice`) now also implements
/// the [`RawHid`] trait; this function is scheduled to be genericized over
/// `impl RawHid` in the next subtask to enable a `FakeHid` test double.
```

### Success Criteria

- [ ] `pub(crate) trait RawHid` exists in `src/core.rs` with exactly `write` and
      `read_timeout` (no extra methods).
- [ ] Method signatures match hidapi EXACTLY: `(&self, &[u8]) -> Result<usize, hidapi::HidError>`
      and `(&self, &mut [u8], i32) -> Result<usize, hidapi::HidError>`.
- [ ] `impl RawHid for hidapi::HidDevice` delegates via fully-qualified-by-type
      syntax (`hidapi::HidDevice::write(self, data)` / `...::read_timeout(self, buf, timeout)`).
- [ ] Trait is `pub(crate)` (NOT `pub`) — internal seam, not exported.
- [ ] `burst_to_one` signature/behavior unchanged; only its doc comment gains the note.
- [ ] `cargo build` compiles; the ONLY warning is the expected transitional
      `dead_code: trait \`RawHid\` is never used` (resolved by S2).
- [ ] `cargo test` → 65 passed, 0 failed (no regression; S1 adds no test).

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything needed
> to implement this successfully?"_ — **Yes.** The exact, pre-verified trait+impl
> code is given above; the resolved hidapi version and its private-trait internals
> are documented; the recursion hazard and the transitional-warning resolution are
> spelled out; placement anchors and validation commands are concrete. The
> implementer does NOT need a keyboard, does NOT need to read hidapi source, and
> does NOT need to guess call syntax.

### Documentation & References

```yaml
# MUST READ — the design doc that specifies this trait's surface + the fix it enables
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§Step 1 gives the exact trait surface (write + read_timeout) and the FQS
        delegation body; §Step 2 shows the S2 genericization this unblocks."
  section: "Step 1: RawHid Trait (testability enabler)"
  critical: "The doc's call syntax `hidapi::HidDevice::write(self, data)` is
             VERIFIED to compile against the RESOLVED hidapi 2.6.6 (see gotchas).
             Do not 'simplify' it to `self.write(data)` — that recurses."

# MUST READ — the file being edited
- file: src/core.rs
  why: "Contains the imports (lines 1-3) and burst_to_one (doc ~line 279, fn
        line 305) — the two edit sites."
  pattern: "Existing enum/const doc style: `///` doc comments, `pub(crate)` for
            internal items, explicit `// ...` rationale comments."
  gotcha: "burst_to_one calls interface.write(&request_data) (capture), and
           interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS) plus
           interface.read_timeout(&mut drain_buf, 0) (drain). The trait abstracts
           BOTH read sites with ONE method."

# MUST READ — the bug being fixed (context for why this trait exists)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 1 (multi-report reply capture) and Issue 3 (stale replies) are the
        defects this trait ultimately enables testing. Knowing them prevents
        over-scoping S1 (the actual fix is later subtasks)."
  section: "Critical Issues / Issue 1"

# REFERENCE — verified findings for this subtask (version drift, recursion, dead_code)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T1S1/research/rawhid_trait_findings.md
  why: "Documents the hidapi 2.4.1→2.6.6 version drift, the private
        HidDeviceBackendBase trait, the empirical compile tests, and the
        transitional dead_code warning resolution."

# REFERENCE — hidapi source (only if the impl refuses to compile)
- file: /home/dustin/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/hidapi-2.6.6/src/hidapi.rs
  why: "Lines 189-218: `impl HidDeviceBackendBase for HidDevice` defines the real
        write/read_timeout. Lines 152-174: inherent impls (do NOT contain write).
        Confirms write/read_timeout are trait methods of a PRIVATE trait."
  gotcha: "Do NOT try to name `HidDeviceBackendBase` in core.rs — it is private
           and not re-exported. Use `hidapi::HidDevice::write(self, data)` instead."
```

### Current Codebase tree (verified)

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.3.0"; deps: clap 4.5.31, hidapi "2.4.1"
├── Cargo.lock          # resolves hidapi to 2.6.6  <-- version drift (see gotchas)
└── src
    ├── main.rs         # binary entrypoint
    ├── lib.rs          # public API: RunCommand, RunParameters, run(), parse_cli_args(), build_command_data, parse_reply
    ├── core.rs         # <-- FILE TO EDIT: imports + constants + burst_to_one + device cache + send path + #[cfg(test)] mod tests
    └── error.rs        # QmkError
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── core.rs   # MODIFIED ONLY — insert pub(crate) trait RawHid + impl (after imports);
              #                     append one doc line to burst_to_one. No new files.
```

> No new files, no new dependencies, no `[dev-dependencies]`. `RawHid` lives in
> `core.rs` as an internal (`pub(crate)`) type.

### Known Gotchas of our codebase & Library Quirks

```rust
// === GOTCHA 1 (CRITICAL): hidapi version drift ===
//   Cargo.toml declares `hidapi = "2.4.1"` but Cargo.lock RESOLVES it to 2.6.6.
//   The contract's "2.4.1" line-number/signature assumptions are stale. The
//   verified-compiling code in this PRP targets the RESOLVED 2.6.6. If you
//   regenerate Cargo.lock and pin an older/different version, RE-VERIFY the
//   call syntax compiles before proceeding.

// === GOTCHA 2 (CRITICAL): write/read_timeout are PRIVATE-TRAIT methods ===
//   In hidapi 2.6.6, `HidDevice::write`/`read_timeout` are defined ONLY in
//   `impl HidDeviceBackendBase for HidDevice` (a PRIVATE trait, not re-exported).
//   There is NO inherent `write`/`read_timeout` on HidDevice. You CANNOT name
//   `HidDeviceBackendBase` from core.rs (private). Use the fully-qualified-by-type
//   form `hidapi::HidDevice::write(self, data)` — verified to compile and to call
//   the real hidapi impl (it resolves through the in-scope type, not the trait).

// === GOTCHA 3 (CRITICAL): infinite recursion if you use method-call syntax ===
//   Inside `impl RawHid for HidDevice { fn write(&self, d) { self.write(d) } }`,
//   a bare `self.write(d)` resolves to `RawHid::write` (the very method being
//   defined) because (a) there is no inherent `write` on HidDevice, and (b) the
//   private `HidDeviceBackendBase` trait is not in scope in core.rs. => STACK
//   OVERFLOW at runtime. ALWAYS use `hidapi::HidDevice::write(self, d)` /
//   `hidapi::HidDevice::read_timeout(self, buf, t)`. This is why the contract
//   insists on fully-qualified syntax.

// === GOTCHA 4: HidResult<usize> == Result<usize, HidError> ===
//   hidapi returns `HidResult<usize>` where `type HidResult<T> = Result<T, HidError>`.
//   The trait's `Result<usize, hidapi::HidError>` return type is IDENTICAL — no
//   `.map_err` or conversion is needed in the impl body; just return the hidapi
//   call directly.

// === GOTCHA 5: transitional dead_code warning (S1 only) ===
//   In S1 the trait has NO non-test consumer (burst_to_one is genericized in S2).
//   => `cargo build` emits ONE warning: `trait \`RawHid\` is never used`.
//   This is EXPECTED and TRANSIENT. A #[cfg(test)] bound-assertion does NOT silence
//   it under `cargo build`. Do NOT add `#[allow(dead_code)]` (the codebase avoids
//   it on principle; see core.rs constants comment). Instead: accept the single
//   warning for S1; S2's `fn burst_to_one<T: RawHid>(...)` makes the trait used in
//   non-test code and the warning vanishes. (No `-D warnings` config exists today.)

// === GOTCHA 6: pub(crate), not pub ===
//   The trait must be `pub(crate)` — it is an internal testability seam, NOT public
//   API. Making it `pub` would dodge the dead_code lint but VIOLATES the contract
//   ("not exported from the crate"). Keep `pub(crate)`.

// === NOTE: no recursion risk in method-call sites that ALREADY exist ===
//   burst_to_one's existing `interface.write(&request_data)` and
//   `interface.read_timeout(...)` calls compile unchanged (method-call syntax on a
//   concrete `&HidDevice` resolves fine). Do not touch them.
```

## Implementation Blueprint

### Data models and structure

No structs, enums, or newtypes. The only "model" is the two-method trait and its
blanket impl. The return type aliasing (`HidResult<usize>` ≡ `Result<usize, HidError>`)
means the impl bodies are single-line delegations with no conversion logic.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/core.rs (imports + burst_to_one) and the design doc
  - READ: src/core.rs lines 1-47 (imports + constants) and lines 279-430
          (burst_to_one doc + body + batches_for).
  - CONFIRM: imports are exactly
      `use crate::error::QmkError;`
      `use hidapi::{HidApi, HidDevice};`
      `use std::sync::{LazyLock, Mutex, MutexGuard};`
  - CONFIRM: burst_to_one signature is
      `fn burst_to_one(interface: &HidDevice, data: &[u8], batch_count: usize, verbose: bool) -> (bool, Option<Vec<u8>>)`
    and it calls interface.write(&request_data) once (in the batch loop) and
    interface.read_timeout(...) twice (capture + drain).
  - GOAL: know the two edit anchors precisely so the edits are surgical.

Task 2: INSERT the RawHid trait + impl into src/core.rs
  - INSERT: the full `pub(crate) trait RawHid { ... }` + `impl RawHid for
    hidapi::HidDevice { ... }` block (verbatim from the "What" section).
  - PLACEMENT: immediately AFTER the three `use ...;` imports and the blank line
    that follows them, and BEFORE the `// Default constants` comment. (Acceptable
    alternative per the contract: just above `fn burst_to_one`; pick ONE and keep
    a blank line of separation on each side.)
  - ATTRIBUTES: `pub(crate) trait RawHid` (NOT `pub`).
  - DELEGATION: use `hidapi::HidDevice::write(self, data)` and
    `hidapi::HidDevice::read_timeout(self, buf, timeout)` (fully-qualified-by-type).
    Do NOT use `self.write(...)` / `self.read_timeout(...)` (recursion — Gotcha 3).
  - DOC: `///` doc comments on the trait and both methods (see "What").
  - DO NOT: add any other trait method (only write + read_timeout).
  - DO NOT: modify imports (hidapi::HidDevice is already imported; the FQS path
    `hidapi::HidDevice::write` needs no new `use`).
  - DO NOT: touch burst_to_one's body or any other function.

Task 3: APPEND a forward-looking note to burst_to_one's doc comment
  - EDIT: the doc block starting `/// Burst-write \`data\` to a single device...`
    (~line 279). Append the one-line note from the "What" section.
  - TRUTHFULNESS: burst_to_one is NOT generic yet in S1. The note must say it is
    SCHEDULED to be genericized over `impl RawHid` in the next subtask — do NOT
    state it is already generic. (This resolves the contract's OUTPUT-vs-DOCS
    tension; see research notes.)
  - DO NOT: rewrite the rest of the existing doc comment.

Task 4: VALIDATE (do not skip — see Validation Loop for exact expectations)
  - RUN: `cargo build`, `cargo test`, `cargo clippy --all-targets`.
  - EXPECT: build compiles with exactly ONE warning (`dead_code` on RawHid);
            65 tests pass; clippy introduces no NEW warning for this change.
```

### Implementation Patterns & Key Details

```rust
// === PLACEMENT ANCHOR (illustrative; match exact surrounding lines) ===
//
// use crate::error::QmkError;
// use hidapi::{HidApi, HidDevice};
// use std::sync::{LazyLock, Mutex, MutexGuard};
//
// // >>> INSERT pub(crate) trait RawHid + impl HERE <<<
//
// // Default constants
// pub const DEFAULT_VENDOR_ID: u16 = 0xFEED;
// ...


// === THE IMPL BODY (single-line delegations; no conversion) ===
//
// Because `HidResult<usize>` is a type alias for `Result<usize, HidError>`,
// the hidapi call's return value flows straight out — no .map_err / .ok / ?.
impl RawHid for hidapi::HidDevice {
    fn write(&self, data: &[u8]) -> Result<usize, hidapi::HidError> {
        hidapi::HidDevice::write(self, data)          // NOT self.write(data)
    }
    fn read_timeout(&self, buf: &mut [u8], timeout: i32) -> Result<usize, hidapi::HidError> {
        hidapi::HidDevice::read_timeout(self, buf, timeout)  // NOT self.read_timeout(...)
    }
}
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - add: "pub(crate) trait RawHid + impl RawHid for hidapi::HidDevice (after imports)"
  - edit: "one appended doc line on burst_to_one"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps, no [dev-dependencies]. hidapi is already a dep.

PUBLIC API SURFACE:
  - unchanged. "RawHid is pub(crate) — NOT exported. No change to lib.rs re-exports."

DOWNSTREAM CONSUMERS (do NOT implement now — listed for sequencing awareness):
  - P1.M1.T1.S2: "genericize burst_to_one: fn burst_to_one<T: RawHid>(interface: &T, ...) — this is what makes RawHid 'used' and clears the dead_code warning."
  - P1.M1.T2.S1: "FakeHid test double: impl super::RawHid for FakeHid under #[cfg(test)] (needs no dev-dep; std::cell + VecDeque)."
  - P1.M1.T2.S2 / P1.M1.T3.S1: "the actual Issue-1 (capture-last) and Issue-3 (pre-send drain) fixes inside the now-generic burst_to_one."
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (rustfmt default style; no rustfmt.toml in the repo).
cargo fmt

# Build the whole crate.
cargo build 2>&1 | tee /tmp/s1_build.log

# EXPECTED for S1 (Option A — accept transitional warning):
#   Compiling qmk_notifier ...
#   warning: trait `RawHid` is never used
#    --> src/core.rs:NN:NN
#   Finished `dev` profile ...
#   ^ exactly ONE warning, on RawHid, nothing else. If you see a SECOND warning
#     or an error (e.g. "recursion", "cannot find trait HidDeviceBackendBase"),
#     STOP and re-check the call syntax (Gotchas 2 & 3).

# Lint.
cargo clippy --all-targets 2>&1 | tee /tmp/s1_clippy.log
# EXPECTED: no NEW clippy lint attributed to RawHid beyond the same dead_code note.

# Formatting gate.
cargo fmt --check
# EXPECTED: exit 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full suite — S1 adds NO test; confirms no regression.
cargo test
# EXPECTED: "test result: ok. 65 passed; 0 failed; 0 ignored; ...".

# (Optional, does NOT clear the cargo-build warning but documents the bound.)
# A compile-time trait-bound assertion can be added under #[cfg(test)] in core.rs
# to pin the impl, e.g.:
#   #[test] fn raw_hid_is_implemented_for_hid_device() {
#       fn _assert<T: super::RawHid>() {}
#       _assert::<hidapi::HidDevice>();
#   }
# This is OPTIONAL in S1 (FakeHid in S1.M1.T2.S1 is the real consumer). If added,
# `cargo test` → 66 passed. It does not silence the cargo-build dead_code warning.
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
S1 introduces a type + blanket impl with zero behavior change — burst_to_one is
still concrete and unmodified, so there is no new runtime path to exercise. Live
hardware is not required and not useful here (the trait's value is realized once
burst_to_one is genericized in S2 and a FakeHid double exists in S1.M1.T2.S1).
The Level 2 suite (all 65 existing tests, incl. the build_command_data / parse_reply
/ device_matches / batches_for / CLI pure-function tests) is the end-to-end gate.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the trait + impl are present and shaped correctly:
grep -n "pub(crate) trait RawHid" src/core.rs
grep -n "impl RawHid for hidapi::HidDevice" src/core.rs
grep -n "hidapi::HidDevice::write(self, data)" src/core.rs
grep -n "hidapi::HidDevice::read_timeout(self, buf, timeout)" src/core.rs
# EXPECTED: each grep hits exactly one line.

# Confirm burst_to_one is unchanged:
grep -n "fn burst_to_one" src/core.rs
# EXPECTED: still `fn burst_to_one(interface: &HidDevice, ...` (concrete, NOT `<T: RawHid>`).

# Sanity: confirm only ONE warning and that it is the expected one:
cargo clean -p qmk_notifier && cargo build 2>&1 | grep "warning:" | sort | uniq -c
# EXPECTED: a single `warning: trait \`RawHid\` is never used` (or 0 once S2 lands).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` compiles; the ONLY warning is `dead_code` on `RawHid`.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 1 passed: `cargo clippy --all-targets` → no NEW lint beyond the dead_code note.
- [ ] Level 2 passed: `cargo test` → 65 passed, 0 failed.
- [ ] Level 4 passed: the four `grep`s each hit exactly one line; `burst_to_one` still concrete.

### Feature Validation

- [ ] `pub(crate) trait RawHid` defined with exactly `write` + `read_timeout`.
- [ ] Signatures match hidapi exactly (return type `Result<usize, hidapi::HidError>`).
- [ ] `impl RawHid for hidapi::HidDevice` uses FQS `hidapi::HidDevice::write(self, data)`
      and `hidapi::HidDevice::read_timeout(self, buf, timeout)` — NO `self.write(...)` recursion.
- [ ] Trait is `pub(crate)` (not `pub`); not re-exported from lib.rs.
- [ ] `burst_to_one` body/signature unchanged; only its doc comment gained the forward note.
- [ ] No new deps, no new files, no `[dev-dependencies]`.

### Code Quality Validation

- [ ] Follows existing `///` doc + `pub(crate)` + rationale-comment conventions in core.rs.
- [ ] No `#[allow(dead_code)]` added (codebase convention; warning is transient to S2).
- [ ] Placement groups the abstraction near the imports (or, if chosen, just above burst_to_one).
- [ ] The appended doc note is TRUTHFUL about S1's state (concrete, genericization pending).

### Documentation & Deployment

- [ ] Trait + both methods carry `///` doc comments (Mode A — rides with the work).
- [ ] The recursion hazard is documented inline (the `// Fully-qualified-by-type: REQUIRED` comment).
- [ ] No external doc files changed (this is an internal type with no public surface).

---

## Anti-Patterns to Avoid

- ❌ Don't write `self.write(data)` / `self.read_timeout(...)` inside the impl —
  it resolves to the trait method you're defining and recurses infinitely. Use
  `hidapi::HidDevice::write(self, data)`.
- ❌ Don't try to name `hidapi::HidDeviceBackendBase` — it's a private trait, not
  re-exported. The FQS-by-type path is the correct, verified way to reach hidapi's impl.
- ❌ Don't make the trait `pub` to dodge the dead_code lint — the contract requires
  `pub(crate)` (internal seam, not public API).
- ❌ Don't add `#[allow(dead_code)]` — the codebase avoids it; the warning is
  transient and clears when S2 genericizes burst_to_one.
- ❌ Don't genericize `burst_to_one` in S1 (that's S2) and don't add `FakeHid`
  (that's S1.M1.T2.S1). S1 is ONLY the trait + blanket impl + one doc line.
- ❌ Don't add a third method to the trait (e.g. `read`, `set_blocking_mode`) —
  YAGNI; abstract ONLY what burst_to_one calls (`write`, `read_timeout`).
- ❌ Don't trust the contract's "hidapi 2.4.1" line numbers blindly — Cargo.lock
  resolves to 2.6.6. If you regenerate the lockfile, re-verify the call syntax compiles.
- ❌ Don't write a doc comment claiming burst_to_one is "generic over RawHid" while
  it is still concrete — that's untrue in S1. Phrase it as scheduled/pending.

---

**Confidence Score: 9/10** for one-pass implementation success. The exact,
pre-verified (compiled against resolved hidapi 2.6.6) trait+impl code is provided
verbatim, the recursion hazard and private-trait internals are documented from
source, and the one unavoidable wrinkle (the transient `dead_code` warning) is
explicitly scoped and given a clean resolution. The only residual risk is a future
Cargo.lock change pinning a different hidapi version — mitigated by the "re-verify
if lockfile changes" guidance and the grep-based Level 4 gate.