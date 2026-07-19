# PRP — P1.M1.T1.S2: Genericize burst_to_one over impl RawHid

---

## Goal

**Feature Goal**: Change `burst_to_one`'s first parameter from the concrete
`interface: &hidapi::HidDevice` to a generic `interface: &T` bounded by the
`RawHid` trait delivered by sibling P1.M1.T1.S1 — i.e.
`fn burst_to_one<T: RawHid>(interface: &T, data: &[u8], batch_count: usize, verbose: bool) -> (bool, Option<Vec<u8>>)`.
The function body is **byte-for-byte unchanged**: its three method calls
(`interface.write(&request_data)`, `interface.read_timeout(&mut read_buf, …)`,
`interface.read_timeout(&mut drain_buf, 0)`) now resolve through the `RawHid`
trait bound instead of the concrete hidapi type, with identical runtime behavior.
This is a **testability seam, not a behavior change**: it is the prerequisite that
lets a `FakeHid` test double (P1.M1.T2.S1) stand in for the device so the
Issue-1 (multi-report reply capture) and Issue-3 (stale-reply drain) fixes can be
unit-tested without a physical QMK keyboard.

**Deliverable**: The single-line signature edit on `burst_to_one` (add `<T: RawHid>`
generic + change the first param type from `&HidDevice` to `&T`), plus a one-line
tense fix to the forward-looking doc note S1 appended (it currently says
genericization is "scheduled in the next subtask" — which becomes false the moment
this subtask lands). Body unchanged. Caller `try_send_once` unchanged (Rust infers
`T = HidDevice` at the call site). No new tests, no new files, no new deps.

**Success Definition**:
- `cargo build` compiles with **ZERO warnings** — the S1 transitional `dead_code`
  warning (`trait \`RawHid\` is never used`) is now CLEARED, because `burst_to_one`
  consumes the trait in non-test code. **This zero-warnings build is the headline
  signal that the genericization took effect.**
- `cargo test` → **65 passed, 0 failed** (no new test; nothing regressed).
- `cargo clippy --all-targets` → no NEW lint attributed to this change.
- `cargo fmt --check` → exit 0.
- `burst_to_one`'s signature reads `fn burst_to_one<T: RawHid>(interface: &T, …)`.
- `try_send_once`, `send_raw_report`, and the `RawHid` trait/impl are unchanged.

## User Persona (if applicable)

**Target User**: The implementer of the v0.3.1 reply-capture bug fixes (P1.M1.T2 /
P1.M1.T3) — specifically the engineer who will write `FakeHid` and the
multi-report regression test.

**Use Case**: Make `burst_to_one` callable with any type implementing `RawHid`
(not just `hidapi::HidDevice`), so a test double can be passed in from a
`#[cfg(test)]` context.

**User Journey**: S2 genericizes the seam → S1.M1.T2.S1 defines
`impl RawHid for FakeHid` under `#[cfg(test)]` → S1.M1.T2.S2/S3 rewrite the
capture logic ("keep last reply") and add a regression test asserting a 2-report
payload yields the ETX-report result → Issue 1 is fixed and regression-locked.

**Pain Points Addressed**: Today `burst_to_one` can only be exercised with a live
keyboard (it takes `&HidDevice`), so the multi-report capture bug could only be
proven on hardware (see `TEST_RESULTS.md`). This seam makes the bug-fix
testable in CI.

## Why

- `burst_to_one` is the function containing BOTH defects targeted by milestone
  P1.M1: **Issue 1** (captures the FIRST reply instead of the ETX-report reply for
  multi-report payloads — `prd_snapshot.md` §"Critical Issues") and **Issue 3**
  (no pre-send IN-buffer drain — §"Major Issues"). Both fixes live inside this one
  function's body.
- Today `burst_to_one(interface: &HidDevice, …)` is hard-wired to hidapi, so it
  cannot be unit-tested without hardware. Genericizing over `T: RawHid` is the
  minimal, behavior-preserving change that opens the function to a `FakeHid` double.
- It is the **direct consumer** of the `RawHid` trait added in S1. S1's trait has
  no non-test consumer yet (it currently emits `warning: trait \`RawHid\` is never
  used`); S2 is what makes the trait "used" and clears that warning.
- Sequencing follows `reply_capture_design.md`: trait (S1) → genericize (S2) →
  FakeHid (S1.M1.T2.S1) → capture-last fix (S1.M1.T2.S2). S2 is purely mechanical
  and risk-free (no logic change), so it ships in isolation for reviewability.

## What

### The signature edit (the entire behavioral change)

```rust
// === BEFORE (src/core.rs, fn burst_to_one, current line ~337) ===
fn burst_to_one(
    interface: &HidDevice,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> (bool, Option<Vec<u8>>) {
    // ... body unchanged ...
}

// === AFTER ===
fn burst_to_one<T: RawHid>(
    interface: &T,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> (bool, Option<Vec<u8>>) {
    // ... body UNCHANGED — interface.write()/read_timeout() now dispatch via the
    //     RawHid trait bound; identical behavior for the real T = HidDevice call site ...
}
```

That is the **only** behavioral change. The body (the write loop, the
first-reply capture, the non-blocking drain) is NOT touched.

### The one-line doc-note tense fix

S1 appended a forward-looking note to `burst_to_one`'s `///` doc block (current
line ~336). It currently reads (paraphrased): *"this function is **scheduled to be
genericized** over `impl RawHid` in the next subtask to enable a `FakeHid` test
double."* Once S2 lands, that future-tense claim is **false**. Update it to past
tense, e.g.:

```text
/// HID I/O note: `interface` is generic over the [`RawHid`] trait (both
/// `hidapi::HidDevice` and the `FakeHid` test double implement it), so this
/// function's reply-capture logic is unit-testable without a physical keyboard.
```

(See *Anti-Patterns* for why leaving the stale "scheduled" wording is a defect.)

### Success Criteria

- [ ] `burst_to_one` signature is exactly `fn burst_to_one<T: RawHid>(interface: &T, data: &[u8], batch_count: usize, verbose: bool) -> (bool, Option<Vec<u8>>)`.
- [ ] `burst_to_one` body is byte-for-byte unchanged (no edit to the write loop,
      the capture read, or the drain loop).
- [ ] `try_send_once`'s call site `burst_to_one(interface, data, batch_count, verbose)`
      is unchanged and compiles (T=HidDevice inferred).
- [ ] The `RawHid` trait + `impl RawHid for hidapi::HidDevice` (S1 output) are
      unchanged (consumed, not modified).
- [ ] The S1 forward-looking doc note is updated from future tense to present/past.
- [ ] `cargo build` → **zero warnings** (the S1 `dead_code` warning is gone).
- [ ] `cargo test` → 65 passed, 0 failed.
- [ ] `cargo clippy --all-targets` → no NEW lint.
- [ ] `cargo fmt --check` → exit 0.
- [ ] Only `src/core.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything needed
> to implement this successfully?"_ — **Yes.** The exact before/after signature is
> given; the body-unchanged guarantee is justified call-by-call; the single call
> site is pinpointed; the now-stale doc line is called out; the headline
> zero-warnings gate is stated; and the current (post-S1) line numbers are given
> with an explicit warning that the contract's numbers are stale. The implementer
> needs no keyboard, no hidapi source dive, and no guessing.

### Documentation & References

```yaml
# MUST READ — the sibling PRP whose output S2 consumes (the RawHid trait + impl)
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T1S1/PRP.md
  why: "Defines the exact pub(crate) trait RawHid { write, read_timeout } and
        impl RawHid for hidapi::HidDevice (FQS delegation) that S2 genericizes
        over. Treat as a CONTRACT: RawHid already exists in src/core.rs and must
        NOT be recreated or modified in S2."
  section: "What" (the trait+impl) and "Known Gotchas" (FQS recursion, dead_code)
  critical: "S1 is already LANDED in the working tree (trait at src/core.rs:13,
             impl at :21). S2 does NOT add the trait — it makes burst_to_one
             consume it (clearing S1's transitional dead_code warning)."

# MUST READ — the design doc that sequences trait → genericize → FakeHid → fix
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/reply_capture_design.md
  why: "§Step 2 is this exact genericization; §Step 3/4 are the FakeHid double
        + capture-last fix it unblocks. Confirms the body is unchanged and T is
        inferred at the call site."
  section: "Step 2"

# MUST READ — the file being edited (read current state before editing)
- file: src/core.rs
  why: "Contains the RawHid trait+impl (S1 output, lines 13–31) and burst_to_one
        (doc ~300–336, fn signature line ~337, body through ~380). The call site
        is in try_send_once at line ~269."
  pattern: "Private fns are `fn` (not `pub fn`); generics use `<T: Trait>`
            explicit-form (NOT `impl Trait` in params — clippy prefers the
            explicit generic form)."
  gotcha: "LOCATE burst_to_one by its signature string `fn burst_to_one(`, NOT by
           the contract's line number 305 — S1's insertion shifted it to ~337."

# MUST READ — the bug context this seam ultimately enables fixing
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 1 (multi-report capture returns the intermediate [0] reply) and
        Issue 3 (stale replies across sends) are the defects; both live in
        burst_to_one's body. Knowing them prevents over-scoping S2 (the actual
        fix is P1.M1.T2/T3 — S2 is ONLY the genericization)."
  section: "Critical Issues / Issue 1" and "Major Issues / Issue 3"

# REFERENCE — research notes compiled for this subtask
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T1S2/research/notes.md
  why: "Documents S1-landed confirmation, the stale-line-number shift, the live
        dead_code warning, the body-unchanged compilation reasoning, the
        inference-at-call-site reasoning, and the doc-tense-staleness finding."
```

### Current Codebase tree (verified post-S1)

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.3.0"; deps: clap 4.5.31, hidapi "2.4.1" (Cargo.lock resolves 2.6.6)
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore
└── src
    ├── main.rs         # binary entrypoint — DO NOT TOUCH
    ├── lib.rs          # public API (26 tests) — DO NOT TOUCH
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # <-- FILE TO EDIT: RawHid trait+impl (S1, lines 13-31),
                         #                      burst_to_one (fn ~337, body ~338-380),
                         #                      try_send_once call site (~269)
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── core.rs   # MODIFIED ONLY — burst_to_one signature (1 line) + 1 doc-note line.
              #                     No new files, no new types, no new tests.
```

> No new files. The change is two lines in `src/core.rs`.

### Known Gotchas of our codebase & Library Quirks

```rust
// === GOTCHA 1 (CRITICAL): S1 has ALREADY landed — do NOT recreate the trait. ===
//   The working tree already contains `pub(crate) trait RawHid` (src/core.rs:13)
//   and `impl RawHid for hidapi::HidDevice` (src/core.rs:21). S2 only EDITS
//   burst_to_one to consume it. Adding/modifying the trait is out of scope and
//   would conflict with S1.

// === GOTCHA 2 (CRITICAL): the contract's line numbers are STALE. ===
//   The item description cites burst_to_one at core.rs:305 and the call at :241.
//   S1's ~28-line trait insertion shifted them: burst_to_one is now ~337, the
//   call site is ~269. LOCATE both by content (`grep -n "fn burst_to_one"`,
//   `grep -n "burst_to_one(interface"`), never by the contract's numbers.

// === GOTCHA 3 (THE SUCCESS SIGNAL): S2 must CLEAR the dead_code warning. ===
//   `cargo build` currently emits ONE warning: `trait \`RawHid\` is never used`.
//   That is S1's expected transient state. S2 genericizing burst_to_one makes the
//   trait "used" in non-test code, so the warning VANISHES. If after S2 `cargo
//   build` STILL shows that warning, the genericization did not take effect (you
//   edited the wrong function, the bound is wrong, or T isn't actually used) —
//   re-check. ZERO warnings is the gate.

// === GOTCHA 4: the body compiles UNCHANGED — do NOT edit it. ===
//   burst_to_one's three `interface.*` calls resolve to RawHid trait methods once
//   the param is `interface: &T, T: RawHid`:
//     - interface.write(&request_data)      // &[u8;33] coerces to &[u8]  -> RawHid::write
//     - interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS)        -> RawHid::read_timeout
//     - interface.read_timeout(&mut drain_buf, 0)                           -> RawHid::read_timeout
//   The S1 recursion hazard (bare `self.write` inside `impl RawHid`) does NOT
//   apply here — inside burst_to_one, `interface` is the caller's concrete &T
//   (T=HidDevice at the real site), so the calls dispatch to the trait, then to
//   S1's FQS body, then to hidapi. No loop.

// === GOTCHA 5: try_send_once needs NO change (type inference). ===
//   At the call site `burst_to_one(interface, …)` with `interface: &HidDevice`,
//   Rust infers T=HidDevice and checks HidDevice: RawHid (S1's impl). Do NOT add
//   a turbofish, do NOT change try_send_once's signature, do NOT touch its
//   verbose-block `interface.get_device_info()` (that's a HidDevice-specific
//   method OUTSIDE burst_to_one and is unaffected).

// === GOTCHA 6: use `<T: RawHid>(interface: &T)`, NOT `impl RawHid`. ===
//   The item description and design doc specify the explicit-generic form. clippy
//   actually PREFERS it: `clippy::impl_trait_in_params` flags `fn f(x: impl Trait)`
//   and recommends `fn f<T: Trait>(x: T)`. So the explicit form keeps clippy clean.

// === GOTCHA 7: the S1 doc note becomes stale on S2 landing — fix its tense. ===
//   S1 appended (src/core.rs ~336): "...scheduled to be genericized over impl
//   RawHid in the next subtask...". After S2 that future-tense claim is FALSE.
//   Update it to present/past tense (one line). Leaving a doc comment that
//   contradicts the code is a defect, even though "DOCS: none" means no separate
//   docs deliverable. This is the ONLY doc touch.

// === GOTCHA 8: HidResult<usize> ≡ Result<usize, HidError> (S1, still true). ===
//   The trait methods already return Result<usize, hidapi::HidError>; burst_to_one's
//   `if let Err(e) = interface.write(...)` and `match interface.read_timeout(...)`
//   arms bind the same error type. No conversion needed anywhere.
```

## Implementation Blueprint

### Data models and structure

No new types, no new structs/enums, no trait edits. The only "model" change is a
type parameter on one function signature. There is no data-model work in S2.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/core.rs (RawHid trait + burst_to_one + the call site)
  - READ: src/core.rs lines 13-31 (CONFIRM the RawHid trait + impl from S1 are
          present — `pub(crate) trait RawHid`, `impl RawHid for hidapi::HidDevice`
          with FQS `hidapi::HidDevice::write(self, data)`). If they are ABSENT,
          STOP: S1 has not landed; S2 cannot compile. Do NOT add the trait here.
  - LOCATE burst_to_one via `grep -n "fn burst_to_one" src/core.rs` (expect ~337,
          NOT the contract's 305 — S1 shifted it). Read its signature line and
          the three `interface.*` call sites in the body (~340-380).
  - LOCATE the call site via `grep -n "burst_to_one(interface" src/core.rs`
          (expect ~269). CONFIRM `interface` there is `&HidDevice`.
  - READ burst_to_one's doc block (~300-336) — find the S1 forward-looking note
          ("scheduled to be genericized... in the next subtask") to edit in Task 3.
  - GOAL: know the exact two edit anchors so edits are surgical.

Task 2: EDIT the burst_to_one signature in src/core.rs
  - CHANGE the signature from:
        fn burst_to_one(
            interface: &HidDevice,
            data: &[u8],
            batch_count: usize,
            verbose: bool,
        ) -> (bool, Option<Vec<u8>>)
    to:
        fn burst_to_one<T: RawHid>(
            interface: &T,
            data: &[u8],
            batch_count: usize,
            verbose: bool,
        ) -> (bool, Option<Vec<u8>>)
  - EDIT SCOPE: the `fn burst_to_one(` line (add `<T: RawHid>`) AND the first
          param line (`interface: &HidDevice` -> `interface: &T`). NOTHING else.
  - DO NOT: touch the body (write loop / capture read / drain loop).
  - DO NOT: touch try_send_once, send_raw_report, the RawHid trait, or any import
          (RawHid is already in scope in this module; `HidDevice` import stays —
          it's still used by try_send_once's `Vec<HidDevice>` and other fns).
  - DO NOT: add a turbofish or annotation at the call site (inferred).

Task 3: FIX the S1 forward-looking doc note's tense
  - EDIT the doc line (current ~336) that says burst_to_one is "scheduled to be
          genericized over impl RawHid in the next subtask". Rewrite it in
          present/past tense to state burst_to_one IS generic over RawHid (see
          "What" section for the suggested wording). Keep it to ~2 lines.
  - TRUTHFULNESS: after Task 2, burst_to_one IS generic — the doc must reflect
          that. Do NOT leave a future-tense claim that contradicts the code.
  - DO NOT: rewrite the rest of burst_to_one's doc comment.

Task 4: VALIDATE (do not skip — the zero-warnings gate is the proof of success)
  - RUN: `cargo fmt`, then `cargo build`, then `cargo clippy --all-targets`,
          then `cargo fmt --check`, then `cargo test`.
  - EXPECT: `cargo build` -> ZERO warnings (the S1 `dead_code` warning is GONE —
          this is the headline gate; if it persists, Task 2 didn't take effect).
  - EXPECT: `cargo test` -> "65 passed; 0 failed".
  - EXPECT: clippy -> no NEW lint; fmt --check -> exit 0.
```

### Implementation Patterns & Key Details

```rust
// === THE FULL SIGNATURE AFTER S2 (verbatim) ===
fn burst_to_one<T: RawHid>(
    interface: &T,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> (bool, Option<Vec<u8>>) {
    // ... EXISTING BODY, UNCHANGED ...
    //   interface.write(&request_data)                         // -> RawHid::write
    //   interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS)  // -> RawHid::read_timeout
    //   interface.read_timeout(&mut drain_buf, 0)              // -> RawHid::read_timeout
}


// === WHY `<T: RawHid>(interface: &T)` and NOT `impl RawHid` ===
//   Explicit-generic form is (a) what the contract specifies, (b) clippy-preferred
//   (`clippy::impl_trait_in_params` flags the impl-trait form), and (c) what lets
//   a later caller pass either `&HidDevice` or `&FakeHid` without signature churn.


// === WHY THE CALL SITE COMPILES WITH NO CHANGE ===
//   // try_send_once, line ~269, UNCHANGED:
//   let (success, reply) = burst_to_one(interface, data, batch_count, verbose);
//   //   interface: &HidDevice  =>  Rust infers T = HidDevice, checks HidDevice: RawHid (S1 impl). Done.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/core.rs ONLY"
  - edit:   "burst_to_one signature (add <T: RawHid>; param &HidDevice -> &T)"
  - edit:   "one doc-note line (S1 forward-looking note -> present/past tense)"
  - unchanged: "burst_to_one body, try_send_once, send_raw_report, RawHid trait+impl"

DEPENDENCIES / Cargo.toml:
  - none. No new crate deps, no [dev-dependencies]. hidapi already a dep.

PUBLIC API SURFACE:
  - unchanged. "burst_to_one is PRIVATE; RawHid is pub(crate). No lib.rs re-export change."

PARALLEL-SIBLING CONTRACT (P1.M1.T1.S1 — already LANDED):
  - consumes: "pub(crate) trait RawHid { write(&self,&[u8])->Result<usize,HidError>;
                read_timeout(&self,&mut[u8],i32)->Result<usize,HidError> } and
                impl RawHid for hidapi::HidDevice (FQS delegation). Both present
                in src/core.rs (lines 13-31). Do NOT modify."

DOWNSTREAM CONSUMERS (do NOT implement now — listed for sequencing awareness):
  - P1.M1.T2.S1: "FakeHid test double: impl RawHid for FakeHid under #[cfg(test)]
                  (std::cell + VecDeque; no dev-dep). This is the first non-HidDevice
                  T passed to burst_to_one."
  - P1.M1.T2.S2: "rewrite capture logic to KEEP THE LAST reply (ETX-report reply)
                  inside the now-generic burst_to_one (the Issue-1 fix)."
  - P1.M1.T2.S3: "multi-report reply-capture regression test using FakeHid."
  - P1.M1.T3.S1: "pre-send IN-buffer drain inside burst_to_one (the Issue-3 fix)."
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited file (rustfmt default style; no rustfmt.toml in the repo).
cargo fmt

# Build the whole crate.
cargo build 2>&1 | tee /tmp/s2_build.log
# EXPECTED for S2: ZERO warnings.
#   Compiling qmk_notifier ...
#   Finished `dev` profile ...
# The S1 warning `trait \`RawHid\` is never used` MUST BE GONE. If it persists,
# Task 2 did not take effect (wrong function edited, bound wrong, T unused) —
# re-check the signature. If you see a NEW error (e.g. "cannot infer type",
# "the trait bound ... is not satisfied"), the call site or body needs review.

# Lint (default clippy; no .clippy.toml).
cargo clippy --all-targets 2>&1 | tee /tmp/s2_clippy.log
# EXPECTED: no NEW lint. (clippy::impl_trait_in_params flags `impl Trait` in
# params, NOT the explicit `<T: Trait>` form we used, so we stay clean.)

# Formatting gate.
cargo fmt --check
# EXPECTED: exit 0. If non-zero, re-run `cargo fmt`.

# Headline zero-warnings proof (single command):
cargo clean -p qmk_notifier && cargo build 2>&1 | grep -c "warning:" || true
# EXPECTED: prints 0 (or no output). Non-zero means a warning leaked through.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full suite — S2 adds NO test; confirms no regression and that the generic
# burst_to_one still works end-to-end via run()/send_raw_report.
cargo test
# EXPECTED: "test result: ok. 65 passed; 0 failed; 0 ignored; ...".
# (core.rs 39 + lib.rs 26 = 65; none call burst_to_one directly.)

# Targeted sanity: the pure framing/parser/matcher tests that exercise the
# send path's data layer still pass (they don't touch the generic, but confirm
# nothing collateral moved):
cargo test --lib
# EXPECTED: 65 passed (lib unit tests include core.rs's #[cfg(test)] mod tests).
```

### Level 3: Integration Testing (System Validation)

```text
NOT APPLICABLE for this subtask.
S2 is a pure type-signature genericization with ZERO behavior change — burst_to_one
still takes the same hidapi::HidDevice at the one real call site, so its runtime
behavior is identical to pre-S2. There is no new runtime path to exercise on
hardware. The seam's VALUE is realized once FakeHid exists (P1.M1.T2.S1) and the
capture logic is rewritten + regression-tested (P1.M1.T2.S2/S3). The Level 2
suite (65 tests) plus the Level 1 zero-warnings build gate ARE the end-to-end
verification for this task.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the genericization took effect structurally:
grep -n "fn burst_to_one<T: RawHid>" src/core.rs
# EXPECTED: exactly one hit (the new signature).

grep -n "interface: &T," src/core.rs
# EXPECTED: exactly one hit (burst_to_one's first param).

# Confirm the body's interface calls are UNCHANGED (still method-call syntax,
# now resolving through the trait):
grep -n "interface.write(&request_data)" src/core.rs
grep -n "interface.read_timeout(&mut read_buf, REPLY_READ_TIMEOUT_MS)" src/core.rs
grep -n "interface.read_timeout(&mut drain_buf, 0)" src/core.rs
# EXPECTED: each hits exactly one line (unchanged from pre-S2).

# Confirm the trait+impl are UNCHANGED (S1 output, consumed not modified):
grep -n "pub(crate) trait RawHid" src/core.rs
grep -n "impl RawHid for hidapi::HidDevice" src/core.rs
# EXPECTED: each hits exactly one line, unchanged.

# Confirm the call site is UNCHANGED (no turbofish added):
grep -n "burst_to_one(interface, data, batch_count, verbose)" src/core.rs
# EXPECTED: exactly one hit at ~line 269.

# Confirm the dead_code warning is GONE (the headline signal):
cargo build 2>&1 | grep "RawHid" || echo "RawHid: no warnings (good — trait is now used)"
# EXPECTED: "RawHid: no warnings (good ...)".
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `cargo build` → **ZERO warnings** (S1 `dead_code` cleared).
- [ ] Level 1 passed: `cargo clippy --all-targets` → no NEW lint.
- [ ] Level 1 passed: `cargo fmt --check` → exit 0.
- [ ] Level 2 passed: `cargo test` → 65 passed, 0 failed.
- [ ] Level 4 passed: the five `grep`s each hit exactly the expected lines.

### Feature Validation

- [ ] `burst_to_one` signature is `fn burst_to_one<T: RawHid>(interface: &T, …)`.
- [ ] `burst_to_one` body is byte-for-byte unchanged.
- [ ] `try_send_once`'s call site is unchanged and compiles (T inferred).
- [ ] The `RawHid` trait + impl are unchanged (S1 output, consumed).
- [ ] The S1 forward-looking doc note is updated to present/past tense.
- [ ] No new deps, no new files, no new tests, no lib.rs change.
- [ ] Only `src/core.rs` modified.

### Code Quality Validation

- [ ] Generic form is `<T: RawHid>(interface: &T)` (clippy-preferred; contract-specified).
- [ ] Doc note is truthful (no future-tense claim contradicting the now-generic code).
- [ ] No `#[allow(dead_code)]` added (the warning is genuinely cleared by use, not suppressed).
- [ ] No turbofish or type annotation forced at the call site (inference is the design).

### Documentation & Deployment

- [ ] burst_to_one's doc comment reflects its now-generic nature (one-line tense fix).
- [ ] No external doc files changed (private fn, pub(crate) trait — no public surface).
- [ ] No Cargo.toml / env / config change.

---

## Anti-Patterns to Avoid

- ❌ Don't recreate or modify the `RawHid` trait/impl — S1 already delivered them
  (they're in `src/core.rs:13-31`). S2 only edits `burst_to_one`.
- ❌ Don't trust the contract's line numbers (305 / 241 / 207) — S1 shifted them.
  Locate by signature via `grep -n "fn burst_to_one"`.
- ❌ Don't edit the body of `burst_to_one`. The three `interface.*` calls compile
  unchanged because they resolve through the trait bound. Touching them is both
  unnecessary and out of scope.
- ❌ Don't change `try_send_once`, add a turbofish, or annotate the call site —
  Rust infers `T = HidDevice` and the bound is satisfied by S1's impl.
- ❌ Don't use `impl RawHid` in the parameter position — use the explicit-generic
  `<T: RawHid>(interface: &T)` form (contract-specified and clippy-preferred).
- ❌ Don't add `#[allow(dead_code)]` to suppress the S1 warning — the correct fix
  is to make the trait genuinely USED, which Task 2 does, clearing it for real.
- ❌ Don't add a test in S2. No existing test calls `burst_to_one` directly, and
  the `FakeHid` double + regression test are P1.M1.T2.S1 / P1.M1.T2.S3 (later).
- ❌ Don't leave the S1 forward-looking doc note in future tense after making
  `burst_to_one` generic — that contradicts the code and is a doc defect. Fix the
  tense (Task 3).
- ❌ Don't treat "DOCS: none" as "leave a known-false doc comment in place" — it
  means no separate docs deliverable (no new files, no public-API doc surface);
  a one-line tense correction to an existing private-fn doc comment is accuracy
  maintenance, in scope.
- ❌ Don't skip the zero-warnings `cargo build` check — the disappearance of the
  `dead_code` warning is the cheapest, strongest proof that the genericization
  actually took effect.
- ❌ Don't conflate S2's recursion-free body with S1's recursion hazard. The
  infinite-recursion risk was specific to `impl RawHid for HidDevice { self.write }`
  (S1); inside `burst_to_one`, `interface: &T` dispatches to the trait then to
  hidapi with no loop.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a single, fully-specified signature edit (before/after given verbatim) plus a
one-line doc-tense fix, against code I have read in full. The body-unchanged
guarantee is justified call-by-call (each `interface.*` site compiles via the
trait bound with identical coercion). The caller compiles unchanged by type
inference. S1 is already landed and verified in the working tree (trait at
`src/core.rs:13`). The headline success signal — `cargo build` going from one
warning to zero — is concrete and cheap to check. The only subtlety (stale line
numbers from the contract) is flagged and worked around with locate-by-signature
guidance.