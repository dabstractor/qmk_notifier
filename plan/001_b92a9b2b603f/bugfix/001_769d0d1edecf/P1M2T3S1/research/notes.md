# Research Notes — P1.M2.T3.S1 (Replace extern C signal binding with libc::signal)

> Scope: replace the hand-rolled `extern "C"` SIGPIPE FFI in `src/main.rs` with
> the maintained `libc` crate binding; add `libc` as a Unix-only direct dep in
> `Cargo.toml`; update the doc comment to note the binding source + the accepted,
> isolated PRD §12 "No unsafe" deviation.

## F1 — Exact current code (the edit target), src/main.rs:82-111

Read directly. The doc comment is lines 82-91; the function is lines 93-111:

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
/// behind `#[cfg(unix)`]).
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

The call site is `main()`, line 10 (first statement of `main`), already gated `#[cfg(unix)]`:
```rust
    #[cfg(unix)]
    reset_sigpipe_to_default();
```
**Call site is UNCHANGED by this task** — only the function body + doc comment change.

To be REMOVED from the function body:
- inner doc `/// Signal disposition is the address of a handler function...` (docs the deleted `SigHandler` type)
- `type SigHandler = usize;`
- `extern "C" { fn signal(signum: i32, handler: SigHandler) -> SigHandler; }`
- `const SIGPIPE: i32 = 13;`
- `const SIG_DFL: SigHandler = 0;`

To be ADDED: `libc::signal(libc::SIGPIPE, libc::SIG_DFL);` inside the (kept) `unsafe {}`.

## F2 — libc crate API (authoritative)

- **Function**: `libc::signal` — `pub unsafe extern "C" fn signal(signum: c_int, handler: sighandler_t) -> sighandler_t`
  - URL: https://docs.rs/libc/latest/libc/fn.signal.html
  - Confirmed via docs.rs search result: "signal( signum: c_int, handler: sighandler_t, ) -> sighandler_t"
- **Type aliases**: `c_int = i32`; `sighandler_t = size_t = usize` (on 64-bit Unix).
- **Constants** (both exist in `libc`, unix cfg):
  - `libc::SIGPIPE: c_int` (= 13)  — https://docs.rs/libc/latest/libc/constant.SIGPIPE.html
  - `libc::SIG_DFL: sighandler_t` (= 0) — https://docs.rs/libc/latest/libc/constant.SIG_DFL.html
- **Therefore** `libc::signal(libc::SIGPIPE, libc::SIG_DFL)` type-checks exactly:
  arg1 `c_int` ← `SIGPIPE:c_int` ✓ ; arg2 `sighandler_t` ← `SIG_DFL:sighandler_t` ✓ ;
  returns `sighandler_t` (intentionally discarded — same as current code).
- **`libc::signal` is itself declared `unsafe`** (it is an FFI function). So the
  call MUST be inside an `unsafe {}` block. The `unsafe` is inherent and
  unavoidable — this is the crux of the §12 deviation note (see F5).

## F3 — libc is ALREADY a transitive dependency (zero new supply-chain surface)

- `Cargo.lock` already contains `libc 0.2.186` (registry).
- It is pulled transitively by `hidapi` (Cargo.lock lines 111-119):
  `name = "hidapi"`, version 2.6.6, dependencies include `"libc"`.
- **Implication**: adding `libc = "0.2"` as a direct dep in `[target.'cfg(unix)'.dependencies]`
  does NOT add a new crate to the build or a new download — it merely **promotes an
  already-present transitive dep to a direct one**. The contract's "Mock: none —
  libc is a widely-used, well-audited crate" is reinforced: `hidapi` itself trusts it.
- `[target.'cfg(unix)'.dependencies]` is supported by cargo 1.92.0 (verified
  `cargo --version` → 1.92.0). This is the idiomatic way to keep Windows builds
  from even resolving libc.

## F4 — Current Cargo.toml [dependencies] (the other edit target)

```toml
[dependencies]
clap = "4.5.31"
hidapi = "2.4.1"
```

No `[target.*]` section exists yet. Append:
```toml

[target.'cfg(unix)'.dependencies]
libc = "0.2"
```
(One blank line before the new section, per TOML/cargo convention.)

## F5 — PRD §12 NFR + the deviation framing

- `PRD.md:422` — **§12 Non-Functional Requirements**: "**No `unsafe`.** All HID
  I/O goes through the `hidapi` crate." (The NFR's actual SUBJECT is HID I/O.)
- `src/main.rs:109` is the **ONLY `unsafe` block in the entire `src/` tree**
  (verified: `grep -rn "unsafe" src/` → exactly one match). So the claim
  "isolated, single unsafe block" is empirically true.
- The item contract DOCS requirement: update the doc comment to (a) mention it
  now uses the libc crate binding, and (b) document the §12 deviation as
  accepted + isolated (unsafe is inherent to POSIX `signal()`; no safe Rust API
  can reset SIGPIPE disposition pre-std-init). This implements PRD suggested-fix
  option (a) ("gate the SIGPIPE shim behind ... a crate like `libc`/`nix`
  (still `unsafe` internally but isolated)").

## F6 — README / docs surface (DOCS scope = code-internal comment ONLY)

- `grep -niE "sigpipe|broken pipe|head -|exit 141|unsafe" README.md` → **zero
  matches**. The README never mentions the SIGPIPE shim. So there is NOTHING to
  update in README.md. Item DOCS: "[Mode A] Update the doc comment ... Code-
  internal doc." Confirmed: no external doc file changes.

## F7 — Scope isolation from the PARALLEL task (P1.M2.T2.S1)

- The parallel task P1.M2.T2.S1 edits **`src/lib.rs`** only (deletes the
  `.short('c')` line in `build_cli_command`). 
- THIS task (P1.M2.T3.S1) edits **`src/main.rs`** (function body + doc comment)
  and **`Cargo.toml`** (add `[target.'cfg(unix)'.dependencies] libc`).
- **Disjoint files** — no overlap, no merge conflict. main.rs is not touched by
  S1; lib.rs is not touched by S1's libc work (there is none); Cargo.toml is not
  touched by S1.
- `cargo test --lib` status: P1.M2.T1.S3 (test-module rewrite) is now COMPLETE
  (per plan_status), so the lib test module compiles. P1.M2.T2.S1's one-line
  deletion cannot break compilation. Therefore `cargo test` is a valid smoke
  gate for this task (unaffected by either parallel task).

## F8 — Validation gates (verified baseline)

- Baseline `cargo build` → "Finished `dev` profile ... in 0.01s", zero warnings.
- No `rustfmt.toml` / `clippy.toml` / `.rustfmt.toml` in repo → default configs.
  `cargo fmt` + `cargo fmt --check` (exit 0) is the format gate.
- `rustc 1.92.0` / `cargo 1.92.0` — full libc + target-cfg support.
- **Behavior preservation proof**: the new call `libc::signal(libc::SIGPIPE,
  libc::SIG_DFL)` invokes the IDENTICAL POSIX `signal(2)` syscall with IDENTICAL
  arguments (signum=SIGPIPE=13, handler=SIG_DFL=0) as the old
  `signal(SIGPIPE, SIG_DFL)`. Behavior (process exits 141 on broken pipe) is
  unchanged BY CONSTRUCTION — the only difference is the binding source
  (maintained libc crate vs hand-rolled extern). Compiling successfully is
  therefore strong evidence of behavioral equivalence.

## F9 — Design decision: fully-qualified `libc::` prefix (NOT `use libc;`)

- The entire `reset_sigpipe_to_default` fn is already `#[cfg(unix)]`-gated, so
  any `libc::` reference inside it is compiled ONLY on Unix automatically.
- Adding `#[cfg(unix)] use libc;` at module top would work but is redundant
  (the fn gate already covers it) and risks an `unused import` clippy warning if
  anything changes. The **fully-qualified `libc::signal(libc::SIGPIPE,
  libc::SIG_DFL)`** form is cleaner, self-documenting (visibly FFI), and needs
  no top-level import. **Chosen approach.** (Contract explicitly permits either.)