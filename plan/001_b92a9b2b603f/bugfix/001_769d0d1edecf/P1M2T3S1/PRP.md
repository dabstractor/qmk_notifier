# PRP — P1.M2.T3.S1 (bugfix): Replace hand-rolled `extern "C"` SIGPIPE binding with `libc::signal`

> ⚠️ **READ THIS BANNER FIRST — parallel-execution context.**
>
> This task runs **in parallel with P1.M2.T2.S1** (dropping the undocumented
> `-c` short flag from `--create-config` in `src/lib.rs`). S1 edits
> **`src/lib.rs`** only; **THIS task (S1 of T3)** edits **`src/main.rs`** (the
> `reset_sigpipe_to_default` function body + doc comment) and **`Cargo.toml`**
> (add a Unix-only `libc` dependency). The two tasks touch **disjoint files** —
> no overlap, no merge conflict. Neither task reads the other's output.
> `cargo test --lib` is currently valid (P1.M2.T1.S3's test-module rewrite is
> COMPLETE per plan_status, and S1-of-T2's one-line `.short('c')` deletion cannot
> break compilation), so it is a usable smoke gate for this task.

---

## Goal

**Feature Goal**: Eliminate the hand-rolled `extern "C"` SIGPIPE FFI block in
`src/main.rs` by routing the POSIX `signal(SIGPIPE, SIG_DFL)` call through the
maintained **`libc`** crate — resolving PRD §h2.3/§h3.3 Issue 4's "literal-wording
violation of §12 *No `unsafe`*" by adopting the PRD's suggested-fix **option (a)**
("gate the SIGPIPE shim behind ... a crate like `libc`/`nix` (still `unsafe`
internally but isolated)"). The `unsafe` block itself is inherent to POSIX
`signal(2)` and cannot be removed; it is documented as an accepted, isolated
deviation (the **only** `unsafe` block in the crate, in the **binary**, unrelated
to HID I/O — the actual subject of §12).

**Deliverable**: Two file edits —
1. **`Cargo.toml`**: append `[target.'cfg(unix)'.dependencies]\nlibc = "0.2"`
   (promotes an **already-present transitive dep** of `hidapi` to a direct dep;
   no new supply-chain surface).
2. **`src/main.rs`**: inside `reset_sigpipe_to_default()` (lines 93-111),
   **delete** the `type SigHandler`, the `extern "C" { fn signal(...) }` block,
   the `const SIGPIPE`/`const SIG_DFL`, and the now-orphaned inner doc line;
   **replace** the `unsafe { signal(SIGPIPE, SIG_DFL); }` body with
   `unsafe { libc::signal(libc::SIGPIPE, libc::SIG_DFL); }`; **update** the
   outer `///` doc comment (lines 82-91) to state the binding is now via the
   `libc` crate and to document the accepted §12 "No `unsafe`" deviation.

**Success Definition**: `cargo build --release` succeeds with zero warnings;
`grep -nE "extern \"C\"|type SigHandler|const SIGPIPE|const SIG_DFL" src/main.rs`
returns **zero matches** (hand-rolled FFI fully gone); `grep -n "libc::signal"
src/main.rs` returns **one** match; SIGPIPE behavior is unchanged (identical
syscall, identical args → process still exits 141 on broken pipe); the doc
comment names the `libc` crate and records the §12 deviation.

## User Persona (if applicable)

**Target User**: Maintainers / auditors reading `main.rs` who expect FFI to go
through a maintained, well-audited crate (`libc`) rather than a bespoke
`extern "C"` block with magic constants (`const SIGPIPE: i32 = 13`). Also: the
PRD §12 NFR conformance reviewer.

**Use Case**: Code review or security audit — the reviewer sees `libc::signal`
(a familiar, audited binding) instead of a hand-rolled FFI declaration, and the
doc comment explicitly accounts for the §12 "No `unsafe`" NFR.

**User Journey**: (binary runtime) `qmk_notifier --list | head -1` → process
exits 141 (SIGPIPE) cleanly, exactly as before — **no user-visible change**. The
change is purely internal (binding source + docs).

**Pain Points Addressed**: Removes a literal-wording §12 NFR violation (hand-
rolled `unsafe` FFI) by isolating it behind the maintained `libc` crate and
documenting the unavoidable residual `unsafe` as an accepted, binary-only,
HID-unrelated deviation.

## Why

- **PRD §h2.3 / §h3.3 (Issue 4)** flags `src/main.rs:101`'s hand-rolled
  `extern "C"` SIGPIPE `unsafe` block as a literal violation of §12 ("No
  `unsafe`. All HID I/O goes through the `hidapi` crate."). The PRD's
  *Suggested Fix* option (a): "gate the SIGPIPE shim behind ... a crate like
  `libc`/`nix` (still `unsafe` internally but isolated)". This PRP implements
  **option (a)** with the `libc` crate.
- **Why `libc` over `nix`**: `libc` is **already a transitive dependency** of
  `hidapi` (Cargo.lock: `libc 0.2.186`, pulled by `hidapi 2.6.6`). Promoting it
  to a direct dep adds **zero new crates** to the build / supply chain. `nix`
  would be a new, heavier dep (it wraps libc). `libc` is the minimal, idiomatic
  choice — exactly what the item contract specifies.
- **The `unsafe` is unavoidable**: POSIX `signal(2)` is an FFI call; the `libc`
  crate declares it `pub unsafe extern "C" fn signal(...)`. There is **no safe
  Rust std API** to reset SIGPIPE disposition before std initializes. The §12
  NFR's literal wording is stricter than its intent (its subject is "HID I/O");
  this fix documents the deviation rather than pretending the `unsafe` can be
  eliminated. This is the single `unsafe` block in the entire `src/` tree
  (verified — see F5 in research/notes.md).
- **Closes P1.M2.T3** (the only subtask): with it landed, Issue 4 is resolved
  and milestone P1.M2 (Public API, CLI & NFR Conformance) loses its NFR subtask.

## What

### Edit 1 — `Cargo.toml` (append a Unix-only dependency section)

Current `[dependencies]`:
```toml
[dependencies]
clap = "4.5.31"
hidapi = "2.4.1"
```

Append (with one blank line separator):
```toml

[target.'cfg(unix)'.dependencies]
libc = "0.2"
```

`[target.'cfg(unix)'.dependencies]` keeps Windows builds from even resolving
`libc`. `0.2` is the current release line (Cargo.lock already has `0.2.186`).

### Edit 2 — `src/main.rs`: rewrite the function body (lines 93-111)

**BEFORE** (current, src/main.rs:93-111):
```rust
#[cfg(unix)]
fn reset_sigpipe_to_default() {
    /// Signal disposition is the address of a handler function, `SIG_DFL` (0)
    /// or `SIG_IGN` (1).
    type SigHandler = usize;
    extern "C" {
        fn signal(signum: i32, handler: SigHandler) -> SigHandler;
    }

    const SIGPIPE: i32 = 13;
    const SIG_DFL: SigHandler = 0;

    // SAFETY: `signal()` is a thread-safe-enough POSIX call for one-shot
    // process-global disposition reset performed before any threads are spawned.
    // `SIG_DFL` is a well-known sentinel value. Ignoring the return value is
    // fine: worst case the disposition stays unchanged, which is benign.
    unsafe {
        signal(SIGPIPE, SIG_DFL);
    }
}
```

**AFTER**:
```rust
#[cfg(unix)]
fn reset_sigpipe_to_default() {
    // SAFETY: `signal()` is a thread-safe-enough POSIX call for one-shot
    // process-global disposition reset performed before any threads are spawned.
    // `SIG_DFL` is a well-known sentinel value. Ignoring the return value is
    // fine: worst case the disposition stays unchanged, which is benign.
    //
    // The FFI binding is provided by the maintained `libc` crate (a transitive
    // dependency of `hidapi`, already resolved in Cargo.lock) rather than a
    // hand-rolled `extern "C"` block.
    unsafe {
        libc::signal(libc::SIGPIPE, libc::SIG_DFL);
    }
}
```

**Removed**: the inner `/// Signal disposition...` doc line, `type SigHandler`,
the `extern "C" { fn signal ... }` block, `const SIGPIPE`, `const SIG_DFL`.
**Kept**: the `#[cfg(unix)]` gate, the function signature, the SAFETY comment
(extended), the `unsafe {}` block. **Changed**: the call is now fully-qualified
`libc::signal(libc::SIGPIPE, libc::SIG_DFL)`.

### Edit 3 — `src/main.rs`: update the doc comment (lines 82-91)

**BEFORE** (current, src/main.rs:82-91):
```rust
/// Reset the SIGPIPE disposition to its default (`SIG_DFL`).
///
/// Rust's runtime sets SIGPIPE to `SIG_IGN`, which turns the next `println!` to
/// a closed pipe into a panic (exit 101). Unix CLI tools are expected to die
/// quietly with SIGPIPE (exit 141) when a downstream consumer exits early
/// (e.g. `qmk_notifier --list | head -1`). This restores that behavior.
///
/// Uses raw `libc`-style syscalls via `extern "C"` so no extra dependency is
/// required. On non-Unix targets this is a no-op (the helper is only called
/// behind `#[cfg(unix)]`).
```

**AFTER** (note: line "Uses raw `libc`-style syscalls via `extern "C"` ..." is now
**factually wrong** and must be replaced; a new `# Deviation from PRD §12`
subsection is added per the item's DOCS requirement):
```rust
/// Reset the SIGPIPE disposition to its default (`SIG_DFL`).
///
/// Rust's runtime sets SIGPIPE to `SIG_IGN`, which turns the next `println!` to
/// a closed pipe into a panic (exit 101). Unix CLI tools are expected to die
/// quietly with SIGPIPE (exit 141) when a downstream consumer exits early
/// (e.g. `qmk_notifier --list | head -1`). This restores that behavior.
///
/// The FFI binding goes through the maintained [`libc`] crate (PRD Issue-4
/// suggested-fix option (a)) rather than a hand-rolled `extern "C"` block.
/// `libc` is already a transitive dependency of `hidapi`, so this adds no new
/// supply-chain surface. On non-Unix targets this function is absent (only
/// compiled behind `#[cfg(unix)]`, and only called from `main` behind the same
/// gate).
///
/// # Deviation from PRD §12 ("No `unsafe`")
///
/// The `unsafe` block below is inherent to POSIX `signal(2)` — calling any FFI
/// function is `unsafe` by definition, and there is no safe Rust API to reset
/// the SIGPIPE disposition before std initializes. This is an **accepted,
/// isolated deviation** from the literal §12 wording: the NFR's intent is "no
/// `unsafe` in the HID I/O transport path" (the sentence's actual subject), and
/// this code lives in the **binary** (`main.rs`), is unrelated to HID, and uses
/// only the well-audited `libc` binding. It is the single `unsafe` block in the
/// entire crate.
```

### Success Criteria

- [ ] `Cargo.toml` has `[target.'cfg(unix)'.dependencies]` with `libc = "0.2"`;
      `[dependencies]` still lists `clap` and `hidapi` unchanged.
- [ ] `src/main.rs` contains **no** `extern "C"`, **no** `type SigHandler`,
      **no** `const SIGPIPE`, **no** `const SIG_DFL` (hand-rolled FFI gone).
- [ ] `src/main.rs` contains exactly one `libc::signal(libc::SIGPIPE, libc::SIG_DFL)`
      call inside the (kept) `unsafe {}` block of `reset_sigpipe_to_default`.
- [ ] The call site in `main()` (line 10, `#[cfg(unix)] reset_sigpipe_to_default();`)
      is **unchanged**.
- [ ] The doc comment names the `libc` crate and documents the §12 deviation.
- [ ] `cargo build --release` → zero warnings (the contract's explicit gate).
- [ ] SIGPIPE behavior unchanged (identical syscall args → exit 141 on broken pipe).

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact BEFORE→AFTER text
> for both edits (Cargo.toml append + main.rs function rewrite + main.rs doc
> comment rewrite) is quoted **verbatim** with current line numbers. The libc
> API is confirmed from docs.rs (`signal(signum: c_int, handler: sighandler_t)
> -> sighandler_t`; `SIGPIPE: c_int`; `SIG_DFL: sighandler_t`) with the
> type-check reasoning spelled out. The call site is shown to be unchanged.
> Scope isolation from the parallel P1.M2.T2.S1 task is proven (disjoint files).
> libc's status as an already-resolved transitive dep (no new download) is
> evidenced from Cargo.lock. All validation commands are verified working.

### Documentation & References

```yaml
# MUST READ — the file being edited (function body + doc comment)
- file: src/main.rs
  why: "reset_sigpipe_to_default() lives at main.rs:93-111 (fn body) with its
        doc comment at main.rs:82-91. It is called from main() at main.rs:10
        behind #[cfg(unix)] (CALL SITE UNCHANGED). This is the ONLY unsafe block
        in src/ (verified: grep -rn unsafe src/ → one match at main.rs:109)."
  pattern: "The fn is already #[cfg(unix)]-gated, so any libc:: reference inside
            it compiles ONLY on Unix — no separate #[cfg(unix)] use libc; import
            is needed. Use the fully-qualified libc:: prefix (self-documenting
            FFI; no unused-import risk)."
  gotcha: "Do NOT add a top-level `use libc;` — redundant with the fn gate and
           risks an unused-import clippy lint. Fully-qualify as libc::signal."

# MUST READ — the file being edited (add the Unix-only libc dep)
- file: Cargo.toml
  why: "[dependencies] currently has only clap and hidapi. Append a NEW section
        [target.'cfg(unix)'.dependencies] with libc = \"0.2\". libc 0.2.186 is
        ALREADY in Cargo.lock as a transitive dep of hidapi — promoting it to a
        direct dep changes nothing about the build graph except making the
        direct dependency explicit."
  pattern: "[target.'cfg(unix)'.dependencies] is the idiomatic cargo syntax for
            Unix-only deps (supported by cargo 1.92.0, verified). It keeps
            Windows/cross builds from resolving libc at all."
  gotcha: "Append AFTER the existing [dependencies] table with one blank line
           separator. Do NOT add libc to the plain [dependencies] table (that
           would make it non-Unix-gated). Version '0.2' (caret) resolves to the
           already-locked 0.2.186."

# MUST READ — the authoritative libc::signal API
- url: https://docs.rs/libc/latest/libc/fn.signal.html
  why: "Confirms the exact signature: pub unsafe extern \"C\" fn signal(signum:
        c_int, handler: sighandler_t) -> sighandler_t. libc::signal is itself
        declared unsafe (FFI), so the call REQUIRES an unsafe block — the
        unavoidable core of the §12 deviation."
  critical: "c_int = i32; sighandler_t = size_t = usize (64-bit Unix). Therefore
             libc::signal(libc::SIGPIPE, libc::SIG_DFL) type-checks: arg1 c_int
             <- SIGPIPE:c_int; arg2 sighandler_t <- SIG_DFL:sighandler_t; return
             sighandler_t discarded. No casts needed."

# MUST READ — the constants being used (confirm they exist + their types)
- url: https://docs.rs/libc/latest/libc/constant.SIGPIPE.html
  why: "libc::SIGPIPE: c_int (= 13). Replaces the hand-rolled const SIGPIPE: i32 = 13."
- url: https://docs.rs/libc/latest/libc/constant.SIG_DFL.html
  why: "libc::SIG_DFL: sighandler_t (= 0). Replaces the hand-rolled const
        SIG_DFL: SigHandler = 0 and the type SigHandler = usize alias."

# MUST READ — the bug definition + the chosen fix branch
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 4 (§h2.3/§h3.3): unsafe block in main.rs contradicts literal §12
        'No unsafe' NFR. Suggested Fix option (a): 'gate the SIGPIPE shim behind
        ... a crate like libc/nix (still unsafe internally but isolated)'. This
        PRP implements option (a) with libc. Mitigating context: the unsafe is
        in the BINARY (main.rs), not the library, and is unrelated to HID I/O
        (the actual subject of §12)."
  section: "Minor Issues / Issue 4"

# MUST READ — the §12 NFR (the literal wording being deviated from)
- file: PRD.md
  why: "Line 422 (§12 Non-Functional Requirements): 'No unsafe. All HID I/O goes
        through the hidapi crate.' The deviation doc-comment must quote this and
        explain why the binary-only SIGPIPE unsafe is accepted (subject of the
        sentence is HID I/O; this is unrelated; single isolated block)."
  section: "12. Non-Functional Requirements"

# REFERENCE — empirical evidence (exact text, isolation, baseline)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T3S1/research/notes.md
  why: "F1: verbatim current main.rs:82-111. F2: libc API + type-check proof.
        F3: libc already in Cargo.lock (transitive of hidapi). F4: current
        Cargo.toml [dependencies]. F5: §12 wording + single-unsafe-block proof.
        F6: README has zero SIGPIPE mentions (no doc change beyond the comment).
        F7: disjoint from parallel P1.M2.T2.S1 (edits lib.rs; this edits
        main.rs + Cargo.toml). F8: baseline build clean + validated gates. F9:
        why fully-qualified libc:: prefix over `use libc;`."
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # [dependencies]: clap "4.5.31", hidapi "2.4.1" — EDIT (append target-cfg libc)
├── Cargo.lock          # already contains libc 0.2.186 (transitive of hidapi) — UNCHANGED (auto-updated)
├── README.md           # zero SIGPIPE/broken-pipe mentions — UNCHANGED
├── PRD.md              # §12 NFR (line 422) — READ-ONLY reference
├── .gitignore
└── src
    ├── main.rs         # <-- EDIT: reset_sigpipe_to_default body (93-111) + doc comment (82-91)
    ├── lib.rs          # parallel P1.M2.T2.S1's scope — DO NOT TOUCH
    ├── error.rs        # DO NOT TOUCH
    └── core.rs         # DO NOT TOUCH
# No tests/ dir, no examples/, no benches/ — all tests live in lib.rs's #[cfg(test)] mod.
```

### Desired Codebase tree with files to be modified

```bash
Cargo.toml   # MODIFIED: append `[target.'cfg(unix)'.dependencies]\nlibc = "0.2"`
             #   after the existing [dependencies] table. [dependencies] itself unchanged.
src/main.rs  # MODIFIED: (1) rewrite reset_sigpipe_to_default() body (93-111) to use
             #   libc::signal(libc::SIGPIPE, libc::SIG_DFL); remove SigHandler/extern/
             #   consts. (2) rewrite the /// doc comment (82-91) to mention libc + §12
             #   deviation. Call site at main.rs:10 UNCHANGED.
# (lib.rs, error.rs, core.rs, README.md, PRD.md unchanged)
```

> No new files. Two files modified (`Cargo.toml`, `src/main.rs`). No new tests,
> no new types, no new functions, no README change.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (DO NOT add `use libc;`): the entire reset_sigpipe_to_default fn is
//   already #[cfg(unix)]-gated, so libc:: references inside it compile ONLY on
//   Unix — a top-level `#[cfg(unix)] use libc;` is redundant and risks an
//   unused-import clippy lint. Use the FULLY-QUALIFIED libc:: prefix
//   (libc::signal, libc::SIGPIPE, libc::SIG_DFL). This is also self-documenting
//   (visibly an FFI call).

// CRITICAL (the unsafe is UNAVOIDABLE): libc::signal is declared
//   `pub unsafe extern "C" fn signal(...)` — calling it REQUIRES an unsafe block.
//   There is no safe Rust API to reset SIGPIPE before std init. The §12 NFR's
//   literal "No unsafe" cannot be fully satisfied here; the fix isolates the
//   unsafe behind the maintained libc crate (PRD option a) and documents the
//   deviation. Do NOT attempt to wrap it in a "safe" fn that hides the unsafe —
//   the fn reset_sigpipe_to_default() IS already the safe wrapper (callers need
//   no unsafe); the unsafe stays inside, which is correct.

// CRITICAL (libc is NOT a new dep): Cargo.lock already has libc 0.2.186,
//   pulled transitively by hidapi 2.6.6 (Cargo.lock:111-119). Adding it to
//   [target.'cfg(unix)'.dependencies] just PROMOTES an existing transitive dep
//   to a direct one — zero new crates downloaded, zero new supply-chain surface.
//   `cargo build` will NOT re-fetch anything.

// CRITICAL (Unix-only gating — use the target table, NOT #[cfg]): put libc in
//   `[target.'cfg(unix)'.dependencies]` in Cargo.toml, NOT in plain
//   `[dependencies]`. This keeps Windows/cross builds from resolving libc. The
//   code reference (libc::signal) is already covered by the fn's #[cfg(unix)].

// NOTE (behavior is UNCHANGED by construction): the new call
//   libc::signal(libc::SIGPIPE, libc::SIG_DFL) invokes the IDENTICAL POSIX
//   signal(2) syscall with IDENTICAL args (signum=13, handler=0) as the old
//   signal(SIGPIPE, SIG_DFL). SIGPIPE behavior (exit 141 on broken pipe) is
//   preserved. Compiling successfully == behavioral equivalence (same syscall).

// NOTE (call site UNCHANGED): main.rs:10 is `#[cfg(unix)] reset_sigpipe_to_default();`.
//   Do NOT touch main()'s body — only the function definition + its doc comment.

// NOTE (no rustfmt.toml/clippy.toml): default configs. The rewritten fn is
//   shorter and rustfmt-clean. Run `cargo fmt` then `cargo fmt --check` (exit 0).

// NOTE (parallel P1.M2.T2.S1): edits src/lib.rs only (drops .short('c')). This
//   task edits main.rs + Cargo.toml — DISJOINT files, no merge conflict.
//   cargo test --lib is valid (P1.M2.T1.S3 test rewrite is COMPLETE) and is a
//   usable smoke gate (this change touches no test; main.rs has no tests).
```

## Implementation Blueprint

### Data models and structure

No new types, structs, enums, functions, or constants. This subtask **removes**
local FFI declarations (`type SigHandler`, `extern "C" { fn signal }`, `const
SIGPIPE`, `const SIG_DFL`) and routes the call through the `libc` crate's
pre-existing equivalents. No production logic change, no new tests.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: EDIT Cargo.toml — add the Unix-only libc dependency
  - FIND: the [dependencies] table (clap, hidapi) — currently the last section.
  - APPEND (with one leading blank line):
        [target.'cfg(unix)'.dependencies]
        libc = "0.2"
  - DO NOT: add libc to the plain [dependencies] table (it must be Unix-gated).
  - DO NOT: change clap/hidapi versions or any other section.
  - VERIFY (after Task 3): `cargo build --release` resolves libc 0.2.x (already
          locked at 0.2.186; no fetch).

Task 2: EDIT src/main.rs — rewrite reset_sigpipe_to_default() body (lines 93-111)
  - FIND: the `#[cfg(unix)] fn reset_sigpipe_to_default() { ... }` block
          (grep -n "fn reset_sigpipe_to_default" src/main.rs).
  - DELETE from inside the fn body:
        * the inner `/// Signal disposition is the address of a handler...` doc line
        * `type SigHandler = usize;`
        * `extern "C" { fn signal(signum: i32, handler: SigHandler) -> SigHandler; }`
        * `const SIGPIPE: i32 = 13;`
        * `const SIG_DFL: SigHandler = 0;`
  - KEEP: `#[cfg(unix)]`, the fn signature, the SAFETY comment, the `unsafe { }` block.
  - REPLACE the call `signal(SIGPIPE, SIG_DFL);` with `libc::signal(libc::SIGPIPE, libc::SIG_DFL);`.
  - EXTEND the SAFETY comment with two lines noting the binding is via the maintained libc crate.
  - DO NOT: add `use libc;` at the top (fully-qualify instead). DO NOT touch main().

Task 3: EDIT src/main.rs — rewrite the /// doc comment (lines 82-91)
  - FIND: the doc comment immediately above `#[cfg(unix)] fn reset_sigpipe_to_default`.
  - REPLACE the line `/// Uses raw `libc`-style syscalls via `extern "C"` so no extra dependency is required. On non-Unix targets this is a no-op (the helper is only called behind `#[cfg(unix)`]).`
          with two new paragraphs: (a) one stating the binding goes through the
          maintained libc crate (PRD option a), that libc is already a transitive
          dep of hidapi (no new surface), and the non-Unix absence note; (b) a
          `# Deviation from PRD §12 ("No unsafe")` subsection documenting that the
          unsafe is inherent to POSIX signal(2), accepted + isolated, binary-only,
          HID-unrelated, and the single unsafe block in the crate.
  - KEEP the first two paragraphs of the doc comment (the SIG_IGN/exit-141/head explanation) UNCHANGED.
  - SEE the "AFTER" block in the What section for the exact verbatim replacement.

Task 4: VALIDATE (Level 1-3 gates)
  - RUN: `cargo fmt`, then `cargo fmt --check` (exit 0).
  - RUN: `cargo build`            -> zero warnings.
  - RUN: `cargo build --release`  -> zero warnings (the contract's explicit gate).
  - RUN: `cargo clippy`           -> zero warnings (watch for unused-import if `use libc` was added by mistake).
  - RUN: `cargo test`             -> all tests pass (smoke gate; this change touches no test).
  - SANITY: `grep -nE 'extern "C"|type SigHandler|const SIGPIPE|const SIG_DFL' src/main.rs` -> ZERO matches.
  - SANITY: `grep -n 'libc::signal' src/main.rs` -> exactly ONE match.
  - SANITY: `grep -n 'use libc' src/main.rs` -> ZERO matches (fully-qualified approach).
  - BEHAVIOR (best-effort): build the release binary and confirm SIGPIPE exit on a
          closed pipe (see Level 3).
```

### Implementation Patterns & Key Details

```rust
// === THE FUNCTION BODY REWRITE (extern "C" -> libc::) ===
//   The old block declared its own FFI: a `type SigHandler = usize` alias, an
//   `extern "C" { fn signal(...) }` import, and magic constants SIGPIPE=13 /
//   SIG_DFL=0. The libc crate provides ALL of these as audited, platform-correct
//   items, so the entire local declaration collapses to one call:
//
//     // BEFORE (main.rs:109-111):
//     unsafe {
//         signal(SIGPIPE, SIG_DFL);
//     }
//
//     // AFTER:
//     unsafe {
//         libc::signal(libc::SIGPIPE, libc::SIG_DFL);
//     }
//
//   libc::signal is `pub unsafe extern "C" fn signal(c_int, sighandler_t)
//   -> sighandler_t`. SIGPIPE is c_int, SIG_DFL is sighandler_t -> no casts,
//   no type aliases, no consts. The discarded sighandler_t return is fine
//   (same as before — worst case the disposition is unchanged, which is benign).


// === WHY FULLY-QUALIFIED libc:: (NOT `use libc;`) ===
//   The fn is already #[cfg(unix)]-gated, so libc:: inside it is Unix-only
//   automatically. A top-level `#[cfg(unix)] use libc;` would be redundant and
//   could trip clippy::unused_import if the fn ever changed. libc::signal is
//   also self-documenting (the reader sees it's an FFI call at the call site).


// === WHY THE unsafe CANNOT BE REMOVED ===
//   POSIX signal(2) is an FFI function; libc declares it unsafe. Rust std
//   provides NO safe API to reset SIGPIPE before std initializes (std sets
//   SIG_IGN very early). So an unsafe block is intrinsic. The §12 NFR's literal
//   "No unsafe" is stricter than its intent ("no unsafe in HID I/O paths" — the
//   sentence's subject). This fix: (1) isolates the unsafe behind libc (PRD
//   option a), (2) documents it as the single, binary-only, HID-unrelated
//   accepted deviation. reset_sigpipe_to_default() itself is a SAFE fn — callers
//   need no unsafe; the unsafe is confined inside it.


// === WHY libc (NOT nix) ===
//   libc is ALREADY a transitive dep of hidapi (Cargo.lock: libc 0.2.186).
//   Promoting it to a direct dep adds zero new crates. nix would be a new,
//   heavier dep. libc is the minimal idiomatic choice and exactly what the item
//   contract specifies.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "Cargo.toml — append [target.'cfg(unix)'.dependencies] libc = \"0.2\"."
  - modify: "src/main.rs — rewrite reset_sigpipe_to_default() body + its /// doc comment."

NO OTHER CHANGES:
  - main() call site:  "UNCHANGED — main.rs:10 `#[cfg(unix)] reset_sigpipe_to_default();`."
  - lib.rs:            "UNCHANGED — parallel P1.M2.T2.S1's scope; disjoint from this task."
  - tests:             "UNCHANGED — main.rs has no tests; lib tests unaffected (smoke gate only)."
  - README.md:         "UNCHANGED — zero SIGPIPE/broken-pipe mentions (grep-proven); item DOCS = code-internal comment only."

DEPENDENCIES:
  - add to: "Cargo.toml → [target.'cfg(unix)'.dependencies]"
  - entry:  'libc = "0.2"'
  - rationale: "Already a transitive dep of hidapi (0.2.186 in Cargo.lock); promotes existing dep, no new surface."

PUBLIC API SURFACE:
  - changes: "(none — main.rs is the binary; no library API change. No CLI change. No behavior change.)"
  - runtime: "SIGPIPE disposition reset is byte-for-byte the same POSIX signal(2) call (signum=SIGPIPE, handler=SIG_DFL); exit-141-on-broken-pipe behavior preserved."

SCOPE BOUNDARY:
  - ONLY Cargo.toml (one section appended) and src/main.rs (fn body + doc comment)
    are modified. Do NOT:
    * touch src/lib.rs (parallel P1.M2.T2.S1 scope).
    * add `use libc;` (fully-qualify).
    * wrap the call in a new safe helper fn (reset_sigpipe_to_default IS the helper).
    * edit README.md / PRD.md / error.rs / core.rs.
    * change the main() call site or any other main.rs code.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited files (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Full build (compiles main.rs with the new libc::signal + lib). MUST be zero warnings.
cargo build 2>&1 | tee /tmp/t3s1_build.log
# Expected: "Finished `dev` profile ..." and NO "warning:"/"error" lines.

# THE CONTRACT'S EXPLICIT GATE — release build.
cargo build --release 2>&1 | tee /tmp/t3s1_build_release.log
# Expected: "Finished `release` profile ..." zero warnings. (libc 0.2.186 is
#   already in Cargo.lock, so no fetch occurs — confirms the transitive-promotion
#   claim: `Adding libc v0.2.186` must NOT appear; it should resolve instantly.)

# Lint (all targets — valid now that P1.M2.T1.S3 test rewrite is COMPLETE).
cargo clippy 2>&1 | tee /tmp/t3s1_clippy.log
# Expected: zero warnings/errors. WATCH specifically for:
#   - clippy::unused_import if `use libc;` was mistakenly added (it should NOT be).
#   - clippy::unnecessary_cast / type-complexity (none expected — libc types match).

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (smoke gate — this change touches no test)

```bash
# main.rs has NO tests; lib tests are unaffected by this change (it touches only
# the binary's SIGPIPE shim + Cargo.toml). This is a SMOKE gate to confirm
# nothing else broke. (Valid now: P1.M2.T1.S3 test rewrite is COMPLETE.)
cargo test 2>&1 | tee /tmp/t3s1_test.log
# Expected: "test result: ok. 26 passed; 0 failed" (or whatever the current count is).
#   If a test FAILS, it is NOT caused by this change (grep the failure — it will
#   be in lib.rs's test module, unrelated to SIGPIPE/libc). Confirm by reverting
#   only main.rs/Cargo.toml mentally: no test references reset_sigpipe_to_default.
grep -rn "reset_sigpipe_to_default\|SIGPIPE\|libc::signal" src/  # proof: only main.rs, no test refs
```

### Level 3: Integration Testing (the real gates — FFI removal + behavior)

```bash
# (1) THE PRIMARY GATE: hand-rolled FFI is GONE from main.rs.
grep -nE 'extern "C"|type SigHandler|const SIGPIPE|const SIG_DFL' src/main.rs && echo "FAIL: hand-rolled FFI still present" || echo "PASS: hand-rolled FFI removed"
# Expected: PASS (zero matches).

# (2) THE PRIMARY GATE: libc::signal is present (exactly once).
grep -n 'libc::signal(libc::SIGPIPE, libc::SIG_DFL)' src/main.rs
# Expected: exactly ONE match, inside reset_sigpipe_to_default's unsafe block.

# (3) PROOF of fully-qualified approach (no `use libc;` added).
grep -n 'use libc' src/main.rs && echo "WARN: use libc present (should be fully-qualified)" || echo "PASS: fully-qualified libc:: prefix, no use import"

# (4) PROOF the call site is UNCHANGED.
grep -n '#\[cfg(unix)\]' src/main.rs | head -3
# Expected: two matches — the call site in main() (~line 10) and the fn def
#   (~line 93, shifted after the doc-comment rewrite). The line
#   `    reset_sigpipe_to_default();` must still be present unchanged.

# (5) Cargo.toml has the Unix-only libc dep.
grep -nA1 'target.*cfg.unix' Cargo.toml
# Expected:
#   [target.'cfg(unix)'.dependencies]
#   libc = "0.2"

# (6) BEHAVIOR PRESERVED (best-effort SIGPIPE exit check). Build the release
#     binary and pipe a command that produces multi-line output into `head -1`.
#     With SIGPIPE reset to SIG_DFL (our shim), the process should die on the
#     broken pipe (exit 141), NOT panic (exit 101) and NOT silently succeed.
cargo build --release --quiet
./target/release/qmk_notifier --help 2>/dev/null | head -1 >/dev/null
echo "exit=${PIPESTATUS[0]}"
# Expected: exit code 141 (128 + SIGPIPE=13) — confirming SIGPIPE is SIG_DFL and
#   the shim works. (If clap/swallowing yields 0, the shim is still correct by
#   construction — identical syscall — treat 141 as confirmation, 0 as
#   inconclusive-but-not-a-failure. A PANIC exit 101 would be a real failure.)
# Negative control: confirm the BINARY EXISTS and runs:
./target/release/qmk_notifier --help >/dev/null 2>&1; echo "help-exit=$?"
# Expected: 0 (help renders fine to a non-broken sink).
```

### Level 4: Creative & Domain-Specific Validation

```bash
# (1) The doc comment documents the §12 deviation (the item's DOCS requirement).
sed -n '82,120p' src/main.rs | grep -E "libc|§12|No .unsafe|deviation|POSIX signal|single .unsafe|binary"
# Expected: matches for 'libc' (binding source), '§12'/'No unsafe' (deviation),
#   'POSIX signal' (why unavoidable), 'binary' (where it lives), 'single' (isolation).

# (2) The §12 deviation text quotes/paraphrases the NFR accurately.
grep -n "Deviation from PRD" src/main.rs
# Expected: one match (the `# Deviation from PRD §12` doc subsection heading).

# (3) Cross-platform sanity (if a Windows target is configured): libc must NOT
#     be resolved for non-unix. (Only run if you cross-check; skip on a Unix host.)
# cargo tree -e no-dev --target x86_64-pc-windows-gnu 2>/dev/null | grep libc && echo "WARN" || echo "PASS: libc not in windows tree"

# (4) Confirm no NEW crates entered the lockfile (libc was already transitive).
git diff --stat Cargo.lock
# Expected: either NO change to Cargo.lock (libc already pinned at 0.2.186) or
#   at most a reordering — NO new [[package]] entry for libc. This proves the
#   "promotes an existing transitive dep, zero new surface" claim.
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1: `cargo build` → zero warnings.
- [ ] Level 1: `cargo build --release` → zero warnings (**contract's explicit gate**).
- [ ] Level 1: `cargo clippy` → zero warnings (no `unused_import` from a stray `use libc`).
- [ ] Level 1: `cargo fmt --check` → exit 0.
- [ ] Level 2: `cargo test` → all tests pass (smoke gate; unrelated failures are not this task's).

### Feature Validation
- [ ] `src/main.rs` has **no** `extern "C"`, `type SigHandler`, `const SIGPIPE`, `const SIG_DFL`.
- [ ] `src/main.rs` has exactly one `libc::signal(libc::SIGPIPE, libc::SIG_DFL)` in the `unsafe` block.
- [ ] The `main()` call site (`#[cfg(unix)] reset_sigpipe_to_default();`) is unchanged.
- [ ] `Cargo.toml` has `[target.'cfg(unix)'.dependencies]` → `libc = "0.2"`; `[dependencies]` unchanged.
- [ ] SIGPIPE behavior preserved (Level 3.6 exit-code check → 141, not 101).

### Code Quality Validation
- [ ] Fully-qualified `libc::` prefix used (no `use libc;` import).
- [ ] Doc comment names the `libc` crate and documents the §12 "No `unsafe`" deviation.
- [ ] `Cargo.lock` gains no new `[[package]]` (libc was already transitive).
- [ ] No file other than `Cargo.toml` and `src/main.rs` modified.

### Documentation & Deployment
- [ ] Doc comment updated (item DOCS: Mode A, code-internal) — mentions libc + §12 deviation.
- [ ] No README.md change (zero SIGPIPE mentions there; DOCS scope is the comment).
- [ ] No new environment variables or runtime config.

---

## Anti-Patterns to Avoid

- ❌ Don't add `use libc;` (or `#[cfg(unix)] use libc;`) at the top of `main.rs`.
      The fn is already `#[cfg(unix)]`-gated, so the import is redundant and risks
      a clippy `unused_import` lint. **Fully-qualify** as `libc::signal(libc::SIGPIPE,
      libc::SIG_DFL)`.
- ❌ Don't add `libc` to the plain `[dependencies]` table — it must go in
      `[target.'cfg(unix)'.dependencies]` so Windows/cross builds don't resolve it.
- ❌ Don't try to eliminate the `unsafe` block. `libc::signal` is declared `unsafe`
      (it's FFI); there is no safe Rust API to reset SIGPIPE before std init. The
      fix ISOLATES the unsafe behind libc (PRD option a) and DOCUMENTS it as the
      accepted, single, binary-only, HID-unrelated §12 deviation. Wrapping it in a
      "safe" helper that hides the unsafe gains nothing — `reset_sigpipe_to_default`
      is ALREADY the safe wrapper (callers need no unsafe).
- ❌ Don't touch `src/lib.rs` — it is the parallel P1.M2.T2.S1 task's scope (drops
      `.short('c')`). This task's files are `src/main.rs` + `Cargo.toml` only.
- ❌ Don't touch the `main()` body / call site (main.rs:10). Only the function
      definition and its doc comment change.
- ❌ Don't edit `README.md` or `PRD.md`. The README has zero SIGPIPE mentions
      (grep-proven); item DOCS scope is the code-internal doc comment only.
      PRD.md is read-only.
- ❌ Don't introduce `nix` or `signal-hook` "while you're in there". The item
      contract specifies the `libc` crate (minimal, already-transitive). `nix`/
      `signal-hook` would be heavier new deps — out of scope and unnecessary for a
      one-shot SIG_DFL reset.
- ❌ Don't anchor the main.rs edits by line number alone. The parallel tasks drift
      lib.rs, but `main.rs` line numbers are stable for THIS task (no one else
      edits main.rs in this milestone). Still, **anchor by content** — the unique
      `fn reset_sigpipe_to_default` signature and its `extern "C"` block — to be safe.
- ❌ Don't conflate "behavior unchanged" with "no validation needed". The syscall
      is identical BY CONSTRUCTION, but you MUST still run `cargo build --release`
      (the contract's gate) and the grep sanity checks to prove the hand-rolled FFI
      is actually gone and libc::signal is actually wired.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a **mechanical binding-source swap** with both edits (Cargo.toml append +
main.rs function/doc rewrite) quoted **verbatim** BEFORE→AFTER. The `libc::signal`
API is confirmed from docs.rs (`signal(c_int, sighandler_t) -> sighandler_t`;
`SIGPIPE: c_int`; `SIG_DFL: sighandler_t`) with the full type-check reasoning, so
the replacement call compiles with **no casts and no new types**. `libc` is
**already a transitive dep** of `hidapi` (Cargo.lock: 0.2.186), so the
Cargo.toml change adds **zero new crates** — `cargo build --release` resolves
instantly with no fetch. Behavior is preserved **by construction** (identical
POSIX `signal(2)` call, identical args). The `unsafe` block is correctly
identified as unavoidable (FFI) and is documented as the **single, isolated,
binary-only** §12 deviation (verified: exactly one `unsafe` in `src/`). Scope is
provably disjoint from the parallel P1.M2.T2.S1 task (which edits `lib.rs`).
An implementer who makes the two edits and runs the Level-1/Level-3 gates
produces a binary whose SIGPIPE shim runs through the maintained `libc` crate
with unchanged runtime behavior, fully resolving Issue 4 (option a).