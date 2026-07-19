# PRP — P1.M2.T1.S2 (bugfix): Update main.rs for the new `parse_cli_args` signature + env::args flag detection

---

## Goal

**Feature Goal**: Adapt the binary `src/main.rs` to the **new PRD-§3-documented
signature** `parse_cli_args() -> Result<RunParameters, QmkError>` delivered by
P1.M2.T1.S1 (which deleted the undocumented `CliArgs` wrapper). Concretely:
`main.rs` must (a) read the device-targeting scalars + `is_list` directly off the
returned `RunParameters` (no `.params` unwrap), (b) call `run(params)` with the
`RunParameters` by value, and (c) detect the CLI-only `--list-callbacks` sweep
signal **out-of-band via `std::env::args`** (since `RunParameters` carries no
sweep flag — Issue 2). This is the binary half of the Issue-2 fix; it completes
the milestone so that `cargo build` (full) compiles again.

**Deliverable**: **One contiguous edit to `src/main.rs`** (the block spanning the
`let cli = match parse_cli_args() {…}` call through `match run(cli.params) {`),
comprising: (1) a new `let list_callbacks = std::env::args().any(|a| a == "--list-callbacks");`
line placed **before** the `parse_cli_args()` call; (2) rename the binding
`cli` → `params`; (3) re-point the six scalar copies from `cli.params.*` →
`params.*`; (4) drop the now-dead `let list_callbacks = cli.list_callbacks;`;
(5) `match run(cli.params)` → `match run(params)`. The use statement (line 1),
the entire `match` body (the `--list-callbacks` sweep loop, the `--list`
suppression, the default print, the `Err` arm), and the `reset_sigpipe_to_default`
helper are all **unchanged**.

**Success Definition**: `cargo fmt --check` → exit 0; `cargo build` (full binary)
→ compiles with **zero warnings**; `cargo clippy` (default lib+bin) → zero
warnings; **zero compile errors point at `src/main.rs`**; the `--list-callbacks`
sweep behavior is preserved (env::args detection feeds the same
`if list_callbacks` guard on the `CommandResponse::Info` arm); the use statement
is unchanged; no file other than `src/main.rs` is modified. (`cargo test --lib`
**still fails** — that is P1.M2.T1.S3's scope, the lib test module referencing the
deleted `CliArgs`; expected, not fixed here.)

## User Persona (if applicable)

**Target User**: CLI users of the `qmk_notifier` binary (`qmk_notifier …`,
`qmk_notifier --list`, `qmk_notifier --query-info`, `qmk_notifier --list-callbacks`)
and any library consumer written to PRD §3's documented
`run(parse_cli_args()?)` shape.

**Use Case**: A user runs `qmk_notifier --list-callbacks` to enumerate the
keyboard's host-callback registry. `main.rs` detects the flag via `env::args`,
calls `parse_cli_args()` (now returning `RunParameters { command: QueryInfo, … }`),
runs the initial `QueryInfo`, then — because `list_callbacks` is true — sweeps
`QueryCallback(0..callback_count)` and prints each name. Identical user-facing
behavior to before; only the internal plumbing changed.

**User Journey**: `parse_cli_args()` → `RunParameters` → `run(params)` →
`Result<CommandResponse, QmkError>`; if `list_callbacks` (env::args) and the
reply is `Info { callback_count, .. }`, loop `QueryCallback(i)` and print names.

**Pain Points Addressed**: Removes the last `CliArgs` consumer (the binary),
which — combined with S1 (lib) and S3 (tests) — fully restores the PRD §3
contract: `run(parse_cli_args()?)` compiles against the documented signature.
Before this milestone, main.rs read `cli.params` / `cli.list_callbacks`, coupling
the binary to the undocumented `CliArgs` type.

## Why

- **PRD §3 contract (Issue 2, §h3.1)**: the spec mandates
  `pub fn parse_cli_args() -> Result<RunParameters, QmkError>` (PRD.md line 154)
  and §2/§11 describe `main.rs` as "a thin wrapper around `parse_cli_args` +
  `run`". S1 restored the library signature; S2 restores the binary to match —
  together they make documented-against-the-spec code (`run(parse_cli_args()?)`)
  compile and run.
- **The `--list-callbacks` sweep is a CLI-only concern (PRD §11)**, not a library
  one. Carrying it on `RunParameters` would pollute the type `qmkonnect` uses
  directly; carrying it on the deleted `CliArgs` wrapper deviated from §3.
  Detecting it out-of-band (`std::env::args`) in `main.rs` is the spec-preserving
  option (Issue 2 *Suggested Fix* (a)) — the library stays minimal, the sweep
  stays a binary convenience.
- **Two-subtask split (S1 lib ⟶ S2 bin) is deliberate**: landing the library API
  change first (S1) makes this binary adaptation a small, reviewable diff against
  a stable API. The intentional non-compiling intermediate (post-S1, pre-S2) is
  acceptable because **no current consumer breaks**: `qmkonnect` builds
  `RunParameters` directly and never calls `parse_cli_args` (Issue 2 confirms).
- **Low blast radius**: purely mechanical — rename one binding, re-point six
  field accesses, move one bool detection to `env::args`. No new types, no new
  deps, no logic change. The callback-sweep loop is untouched.

## What

One contiguous edit to `src/main.rs`. The exact old→new text is below. Locate the
block by content (it spans the `let cli = match parse_cli_args()` line through the
`match run(cli.params) {` line); the anchor is unique.

### The single edit (parse_cli_args call ⟶ match-run start)

**OLD** (the contiguous block in `src/main.rs`, lines ~12-33 — find via
`grep -n "match run(cli.params)" src/main.rs` and read upward):

```rust
    let cli = match parse_cli_args() {
        Ok(cli) => cli,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Copy out the device-targeting scalars (all Copy) BEFORE run() moves
    // cli.params by value — they're needed to rebuild per-callback params.
    // `is_list` records whether the action is `--list` (ListDevices) so that
    // after `run` we can suppress the library's `Timeout` sentinel for it
    // without re-borrowing the moved `params`.
    let is_list = cli.params.command == RunCommand::ListDevices;
    let vendor_id = cli.params.vendor_id;
    let product_id = cli.params.product_id;
    let usage_page = cli.params.usage_page;
    let usage = cli.params.usage;
    let verbose = cli.params.verbose;
    let list_callbacks = cli.list_callbacks;

    match run(cli.params) {
```

**NEW**:

```rust
    // --list-callbacks is a CLI-only sweep signal. The library no longer returns
    // it from parse_cli_args (PRD §3: parse_cli_args -> RunParameters, which has
    // no sweep flag — Issue 2). Detect it out-of-band from raw argv so main.rs
    // can run the multi-call callback sweep after `run` returns
    // `CommandResponse::Info`. Placed BEFORE parse_cli_args so the flag is
    // captured even if clap exits on --help/--version/an error (harmless: the
    // process exits either way).
    let list_callbacks = std::env::args().any(|a| a == "--list-callbacks");

    let params = match parse_cli_args() {
        Ok(params) => params,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Copy out the device-targeting scalars (all Copy) BEFORE run() moves
    // `params` by value — they're needed to rebuild per-callback params.
    // `is_list` records whether the action is `--list` (ListDevices) so that
    // after `run` we can suppress the library's `Timeout` sentinel for it
    // without re-borrowing the moved `params`.
    let is_list = params.command == RunCommand::ListDevices;
    let vendor_id = params.vendor_id;
    let product_id = params.product_id;
    let usage_page = params.usage_page;
    let usage = params.usage;
    let verbose = params.verbose;

    match run(params) {
```

> The match **BODY** that follows `match run(params) {` (the four arms: the
> `Ok(CommandResponse::Info { callback_count, .. }) if list_callbacks =>` sweep
> loop, `Ok(_) if is_list => {}`, `Ok(response) => println!("{:?}", response)`,
> and the `Err(e) =>` arm) is **byte-for-byte unchanged**. `list_callbacks` in the
> guard now refers to the top-of-`main` env::args-derived bool — same name, same
> `bool` type, same semantics, so the guard works unchanged.

> The `use` statement on **line 1** is **unchanged**:
> `use qmk_notifier::{parse_cli_args, run, CommandResponse, RunCommand, RunParameters};`
> `CliArgs` was never imported into main.rs, and all five remaining imports stay
> used (`RunParameters` in the sweep loop's `RunParameters::new(...)`,
> `RunCommand` in the `== ListDevices` check and `QueryCallback(index)`,
> `CommandResponse` in the match arms).

### Success Criteria

- [ ] `let list_callbacks = std::env::args().any(|a| a == "--list-callbacks");`
      appears **before** the `parse_cli_args()` call, with the explanatory comment.
- [ ] The `parse_cli_args()` match binds `params` (not `cli`): the binding is
      `let params = match parse_cli_args() { Ok(params) => params, … }`.
- [ ] The six scalar copies read `params.*` (not `cli.params.*`):
      `is_list`, `vendor_id`, `product_id`, `usage_page`, `usage`, `verbose`.
- [ ] The `let list_callbacks = cli.list_callbacks;` line is **gone** (replaced by
      the env::args line).
- [ ] The dispatch is `match run(params) {` (not `run(cli.params)`).
- [ ] The match body (4 arms + sweep loop) is unchanged; `list_callbacks` guard
      still works (same bool, now env::args-derived).
- [ ] The use statement (line 1) is unchanged; `reset_sigpipe_to_default` unchanged.
- [ ] `cargo fmt --check` → exit 0; `cargo build` (full) → **zero warnings** and
      **zero errors in `src/main.rs`**; `cargo clippy` → zero warnings.
- [ ] `cargo test --lib` still fails **only** in the lib `#[cfg(test)]` module
      (S3's scope) — NOT because of main.rs.
- [ ] No file other than `src/main.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim old→new text
> for the single contiguous edit is above, with a content-grep anchor. The
> unchanged match body and use statement are explicitly called out. The
> dependency on S1 (parse_cli_args now returns RunParameters; CliArgs deleted) is
> stated as a contract. The parallel-race failure-mode disambiguation (main.rs
> errors ⟹ S2 incomplete; lib.rs errors ⟹ S1 not landed) is documented. The
> validation gate (`cargo build` full passes, but `cargo test --lib` still fails
> ⟹ S3) is explained with the exact reason. `RunParameters` field names/types and
> `RunCommand`'s `PartialEq` derive are verified (research notes).

### Documentation & References

```yaml
# MUST READ — the file being edited (the single contiguous edit lives here)
- file: src/main.rs
  why: "The ONLY file S2 touches. Holds the parse_cli_args match (line ~12),
        the scalar-copy block (lines ~20-31), the `match run(cli.params)` dispatch
        (line ~33), the --list-callbacks sweep loop, and the SIGPIPE helper."
  pattern: "main.rs is a thin wrapper (PRD §2/§11): parse_cli_args -> copy scalars
            -> run(params) -> match on CommandResponse. Device-targeting scalars
            are copied BEFORE run() moves params (all Copy: Option<u16>, u16, bool);
            command is only compared (== ListDevices), not copied."
  gotcha: "The use statement does NOT import CliArgs (grep-proven), so it needs no
           edit. main.rs is UNCHANGED by S1 (S1 edits only lib.rs), so the main.rs
           you read is exactly the S2 starting point."

# MUST READ — the S1 contract (the dependency): what parse_cli_args returns after S1
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S1/PRP.md
  why: "S1 changes lib.rs: parse_cli_args() -> Result<RunParameters, QmkError>
        (was CliArgs); parse_matches -> RunParameters; select_command -> RunCommand
        (drops the bool tuple); DELETES pub struct CliArgs. The --list-callbacks
        sweep signal is NO LONGER returned by the library — its detection becomes
        main.rs's job via std::env::args (this PRP). Treat as a CONTRACT."

# MUST READ — the bug this fixes + the documented signature + qmkonnect impact
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 2 (§h3.1): PRD §3 line 154 mandates
        `pub fn parse_cli_args() -> Result<RunParameters, QmkError>`; the CliArgs
        return was undocumented drift; qmkonnect does NOT call parse_cli_args
        (builds RunParameters directly) so no current consumer breaks. Suggested
        Fix (a) = exactly S1+S2: restore the signature, move --list-callbacks
        out-of-band (env::args in main.rs)."
  section: "Major Issues / Issue 2"

# MUST READ — the PRD public-API contract being restored + main.rs's documented role
- file: PRD.md
  why: "§3 (line 154) is the target signature; §2/§11 call main.rs a 'thin wrapper
        around parse_cli_args + run'. S2 makes main.rs match that description."
  section: "3. Public API", "2. Repository Layout", "11. CLI"

# REFERENCE — the types main.rs consumes (defined in lib.rs — DO NOT EDIT)
- file: src/lib.rs
  why: "RunParameters (lib.rs:120) fields are all pub: command: RunCommand,
        vendor_id/product_id: Option<u16>, usage_page/usage: u16, verbose: bool
        — all Copy (command is only compared). RunCommand (lib.rs:19) derives
        PartialEq, Eq so `params.command == RunCommand::ListDevices` compiles.
        RunParameters::new(command, vid, pid, page, usage, verbose) is used in
        the sweep loop (unchanged). DO NOT EDIT lib.rs (S1/S3 own it)."
  pattern: "RunParameters is the documented single-command type (PRD §3). It has
            no sweep flag — hence the env::args detection in main.rs."
  gotcha: "POST-S1, CliArgs no longer exists in lib.rs. If your `cargo build`
           errors mention src/lib.rs and 'CliArgs', S1 hasn't landed yet (parallel
           race) — see Known Gotchas. main.rs itself must produce zero errors."

# REFERENCE — the sibling PRPs this task sequences against (do NOT implement them)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S1/PRP.md
  why: "The immediately-prior parallel item (lib.rs API change). Defines the exact
        post-S1 lib.rs state S2 consumes. Confirms S1 leaves main.rs + the lib
        test module BROKEN (S2/S3 scope) and `cargo build --lib` green."
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S3/PRP.md
  why: "The follow-up: rewrites the 5 CLI tests + the cli_for helper against
        RunParameters fields. After S2, `cargo test --lib` still fails in that
        test module — that is S3's scope, NOT S2's."

# REFERENCE — empirical evidence (current tree state, field types, race handling)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S2/research/notes.md
  why: "Documents the verified current tree state (PRE-S1: CliArgs at lib.rs:165,
        parse_cli_args returns CliArgs, 72 tests pass), RunParameters field types
        (all pub/Copy), RunCommand's PartialEq derive, the exact change-set table,
        the env::args-before-parse_cli_args rationale, and the parallel-race
        failure-mode disambiguation."
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.3.0"; deps: clap, hidapi "2.4.1"
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore
└── src
    ├── main.rs         # <-- FILE TO EDIT (one contiguous block; rest unchanged)
    ├── lib.rs          # parse_cli_args/RunParameters/CliArgs — S1/S3 own it; DO NOT TOUCH
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # transport (burst_to_one, send_raw_report, FakeHid...) — DO NOT TOUCH
```

### Desired Codebase tree with files to be modified

```bash
src/
└── main.rs   # MODIFIED ONLY (one contiguous edit):
              #   (1) + `let list_callbacks = std::env::args().any(|a| a == "--list-callbacks");`
              #        BEFORE parse_cli_args (+ explanatory comment)
              #   (2) `let cli = match parse_cli_args() { Ok(cli) => cli,` ->
              #       `let params = match parse_cli_args() { Ok(params) => params,`
              #   (3) six scalar copies: `cli.params.*` -> `params.*`
              #   (4) − `let list_callbacks = cli.list_callbacks;` (absorbed into (1))
              #   (5) `match run(cli.params) {` -> `match run(params) {`
              #   (comment "moves cli.params by value" -> "moves `params` by value")
              # use statement (line 1), match body, SIGPIPE helper: UNCHANGED.
# (lib.rs, error.rs, core.rs, Cargo.toml unchanged)
```

> No new files. One file modified (`src/main.rs`). No new dependencies, no new
> types, no new functions.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (DEPENDENCY ON S1): S2's main.rs edits only make `cargo build` (full)
//   pass once S1's lib.rs edits have landed (parse_cli_args -> RunParameters,
//   CliArgs deleted). The current working tree is PRE-S1 (CliArgs still at
//   lib.rs:165; parse_cli_args returns CliArgs; full build currently passes 72
//   tests). If you apply S2's main.rs edit WHILE S1 IS NOT YET APPLIED, `cargo
//   build` will fail — READ THE ERROR LOCATIONS to know whose scope it is:
//     - errors at src/main.rs (e.g. "no field `command` on type `CliArgs`"):
//       S2 is incomplete/wrong — fix main.rs.
//     - errors at src/lib.rs (e.g. "expected RunParameters, found CliArgs" or
//       "cannot find type `CliArgs`"): S1 has NOT landed yet — those are S1's
//       errors, NOT S2's. S2's success criterion is "zero errors in src/main.rs".
//   The orchestrator merges S1+S2 in milestone P1.M2.T1; the green `cargo build`
//   is the combined milestone gate.

// CRITICAL (env::args BEFORE parse_cli_args): the `--list-callbacks` detection
//   MUST precede the parse_cli_args() call (item contract). parse_cli_args may
//   std::process::exit on --help/--version/clap errors; placing env::args first
//   guarantees the flag is captured regardless. In the normal --list-callbacks
//   flow select_command maps it to QueryInfo (Ok, no exit), so order is
//   observationally equivalent — but before-placement is the contract and is
//   defensive. VERIFIED: std::env::args() is a lazy iterator; .any() short-
//   circuits; the String == &str comparison compiles (impl PartialEq<&str> for
//   String). Clippy-clean under default lints.

// CRITICAL (the match BODY is UNCHANGED): do NOT touch the four arms after
//   `match run(params) {`. The `if list_callbacks` guard on the Info arm refers
//   to the NEW top-of-main env::args bool — same identifier, same `bool` type,
//   same truth value, so the guard is unchanged. The sweep loop (QueryCallback
//   iteration, RunParameters::new per-callback, run, print) is identical.

// CRITICAL (use statement UNCHANGED): line 1 is
//   `use qmk_notifier::{parse_cli_args, run, CommandResponse, RunCommand, RunParameters};`
//   CliArgs was NEVER imported into main.rs (grep-proven). All five imports stay
//   used: parse_cli_args/run (called), CommandResponse (match arms), RunCommand
//   (== ListDevices, QueryCallback), RunParameters (sweep loop's RunParameters::new).
//   Do NOT add or remove any import.

// NOTE (all scalars are Copy): vendor_id/product_id are Option<u16>, usage_page/
//   usage are u16, verbose is bool — all Copy, so copying them before `run(params)`
//   moves params is sound. `command` is RunCommand (NOT Copy) but is only
//   COMPARED (`params.command == RunCommand::ListDevices`, needs PartialEq —
//   RunCommand derives it, verified), never copied/moved out. No PartialMove error.

// NOTE (no inner attributes): main.rs has no `#![deny(warnings)]`, so a stray
//   warning wouldn't escalate. (None expected — the edit removes references, adds
//   none.) No rustfmt.toml / clippy.toml exist → default configs; run `cargo fmt`.

// NOTE (SIGPIPE helper is out of scope): the `reset_sigpipe_to_default` fn and its
//   `extern "C" { fn signal(...) }` unsafe block (Issue 4, PRD §12 "No unsafe")
//   are owned by P1.M2.T3.S1 (isolate via the `libc` crate). Do NOT touch them.

// NOTE (qmkonnect unaffected): the downstream daemon builds RunParameters directly
//   and never calls parse_cli_args (Issue 2). The binary's internal rewire has no
//   downstream impact — only the CLI binary's own behavior, which is preserved.

// NOTE (test gate): after S2, `cargo test --lib` STILL FAILS — in the lib
//   #[cfg(test)] module (cli_for helper + 5 CLI tests reference CliArgs/cli.params).
//   That is P1.M2.T1.S3's scope. Do NOT "fix" it by editing lib.rs or main.rs.
//   Use plain `cargo clippy` (lib+bin) for the S2 lint gate, NOT `--all-targets`
//   (which lints tests too and will fail until S3).
```

## Implementation Blueprint

### Data models and structure

No new types, structs, enums, functions, or constants. This subtask is a
**mechanical re-plumbing** of one contiguous block in `main.rs`: rename a
binding, re-point six field accesses, relocate one bool detection to `env::args`,
and update the `run(...)` argument. No state, no I/O change, no logic change.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/main.rs and confirm the anchor (content grep, not line numbers)
  - `grep -n "match run(cli.params)" src/main.rs`  -> the END of the block to edit.
  - READ upward from there to the `let cli = match parse_cli_args() {` line —
          that is the contiguous block (OLD text in the "What" section).
  - READ the match BODY that follows `match run(cli.params) {` (the 4 arms +
          sweep loop) — confirm you will NOT touch it.
  - `grep -n "CliArgs" src/main.rs`  -> expect NO matches (CliArgs never imported
          into main.rs). If any match, STOP: something is unexpected.
  - GOAL: know the exact contiguous block to replace and confirm the use stmt +
          match body are out of scope.

Task 2: EDIT src/main.rs — the single contiguous block
  - REPLACE the OLD block (let cli = match parse_cli_args() { … } through
          match run(cli.params) {) with the NEW block in the "What" section.
  - CHANGES in the NEW block:
      (1) + `let list_callbacks = std::env::args().any(|a| a == "--list-callbacks");`
          with its explanatory comment, placed BEFORE the parse_cli_args match.
      (2) `let cli = match parse_cli_args() { Ok(cli) => cli,` ->
          `let params = match parse_cli_args() { Ok(params) => params,`
          (the Err arm `eprintln!`/`exit(1)` is unchanged).
      (3) comment "moves cli.params by value" -> "moves `params` by value".
      (4) six scalar copies: `cli.params.command/vendor_id/product_id/usage_page/
          usage/verbose` -> `params.command/...` (drop the `.params`).
      (5) − `let list_callbacks = cli.list_callbacks;` (gone; absorbed into (1)).
      (6) `match run(cli.params) {` -> `match run(params) {`.
  - DO NOT: touch the use statement (line 1), the match BODY (4 arms + sweep
          loop), or reset_sigpipe_to_default. Do NOT touch any other file.

Task 3: VALIDATE (scoped — see the S1-dependency gotcha)
  - RUN: `cargo fmt`, then `cargo fmt --check` (exit 0).
  - RUN: `cargo build` (full)   -> MUST compile with ZERO warnings and ZERO
          errors in src/main.rs. THIS IS THE GATE.
  - RUN: `cargo clippy`         -> zero warnings (plain clippy = lib+bin; do NOT
          use --all-targets, which lints tests and fails until S3).
  - EXPECT (do NOT "fix"): `cargo test --lib` FAILS in the lib #[cfg(test)] mod
          tests block (references CliArgs/cli.params) — that is S3's scope.
  - PARALLEL-RACE CHECK: if `cargo build` errors at src/lib.rs (CliArgs / expected
          RunParameters), S1 has not landed yet — those are S1's errors, NOT S2's.
          S2 is correct iff ZERO errors point at src/main.rs.
  - SANITY: `grep -n "cli\." src/main.rs` -> expect ZERO matches (no `cli.params`,
          no `cli.list_callbacks`; the binding is now `params`, and list_callbacks
          comes from env::args). `grep -n "cli.params\|cli.list_callbacks" src/main.rs`
          -> ZERO matches.
  - SANITY: `grep -n "std::env::args" src/main.rs` -> exactly ONE match (the new
          list_callbacks detection).
  - SANITY: `grep -n "match run(params)" src/main.rs` -> exactly ONE match.
```

### Implementation Patterns & Key Details

```rust
// === THE RE-PLUMBING SHAPE (CliArgs wrapper removed; RunParameters used directly) ===
//
//   BEFORE (CliArgs-based):               AFTER (RunParameters-based):
//   let cli = parse_cli_args()?;          let list_callbacks = env::args().any(...);
//   let is_list = cli.params.command==..; let params = parse_cli_args()?;
//   ...                                   let is_list = params.command == ..;
//   let list_callbacks = cli.list_callbacks;   // (gone — top-of-main env::args)
//   run(cli.params)                       run(params)
//
//   CliArgs was { params: RunParameters, list_callbacks: bool }. S1 deleted it;
//   S2 unwraps main.rs: params IS the RunParameters, list_callbacks comes from
//   env::args. Same downstream behavior (run + match), leaner plumbing.


// === WHY env::args().any(|a| a == "--list-callbacks") IS CORRECT + clippy-CLEAN ===
//
//   std::env::args() yields the program's argv as a lazy Iterator<Item=String>.
//   .any() short-circuits on the first match (no full scan cost in the common
//   case). `a == "--list-callbacks"` compares String to &str via
//   `impl PartialEq<&str> for String` — compiles, idiomatic, no clippy lint
//   under defaults. The flag is detected OUT-OF-BAND (the library sees no
//   difference between --query-info and --list-callbacks: both -> QueryInfo).


// === WHY THE MATCH BODY IS UNCHANGED ===
//
//   The `Ok(Info { callback_count, .. }) if list_callbacks =>` arm's guard reads
//   `list_callbacks`. Before S2 that was `cli.list_callbacks` (a bool on CliArgs);
//   after S2 it is the top-of-main env::args-derived bool. Same identifier name,
//   same `bool` type, same truth value for a given argv — so the guard behaves
//   identically. No body edit needed.


// === WHY cargo build (full) IS THE GATE (and cargo test --lib still fails) ===
//
//   `cargo build` compiles lib (non-test) + the main.rs binary. After S1 (lib
//   non-test done) + S2 (main.rs done), both compile -> `cargo build` is green.
//   `cargo test --lib` additionally compiles the lib #[cfg(test)] module, which
//   still references the deleted CliArgs (S3's scope) -> it fails until S3.
//   Plain `cargo clippy` lints lib+bin (passes); `--all-targets` lints tests too
//   (fails until S3). So the S2 gates are: cargo build (full) + cargo clippy
//   (default) + cargo fmt --check.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/main.rs ONLY — one contiguous block."

NO OTHER CHANGES:
  - imports:   "none — line 1 use statement unchanged (CliArgs never imported)."
  - types:     "none — consumes RunParameters/RunCommand/CommandResponse as-is."
  - match body: "UNCHANGED (the 4 arms + the --list-callbacks sweep loop)."
  - SIGPIPE:   "UNCHANGED (reset_sigpipe_to_default + unsafe block — Issue 4 /
                P1.M2.T3.S1 owns the libc cleanup)."
  - deps:      "none — no Cargo.toml change (std::env is std-prelude-adjacent)."

PUBLIC API SURFACE:
  - changes:  "(none — main.rs is a binary target, not library API. The library
              API change is S1's scope.)"
  - unchanged: "all of lib.rs's public surface (RunCommand, RunParameters,
               CommandResponse, parse_cli_args signature post-S1, run, etc.)."

EXPECTED DOWNSTREAM (S3 — do NOT implement here):
  - P1.M2.T1.S3 (tests): "rewrite the 5 CLI tests + the cli_for helper in lib.rs's
        #[cfg(test)] module to assert on RunParameters fields (cli.command,
        cli.usage_page, …) instead of cli.params.*/cli.list_callbacks. After S3,
        `cargo test --lib` passes and `cargo clippy --all-targets` is clean."

SCOPE BOUNDARY:
  - ONLY src/main.rs is modified, and ONLY the one contiguous block (parse_cli_args
    call ⟶ match run start). Do NOT:
    * edit src/lib.rs (S1/S3 own it).
    * edit the lib #[cfg(test)] module (S3).
    * touch the use statement, the match body, or reset_sigpipe_to_default.
    * edit error.rs, core.rs, or Cargo.toml.
```

## Validation Loop

### Level 1: Syntax & Style (THE GATE)

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# THE GATE: build the full crate (lib non-test + main.rs binary). MUST compile
# with ZERO warnings and ZERO errors in src/main.rs.
cargo build 2>&1 | tee /tmp/s2_build.log
# Expected: "Finished `dev` profile [unoptimized ...] target(s) in ..." and NO
#   "warning:" lines, NO "error" lines naming src/main.rs.
# PARALLEL-RACE: if errors name src/lib.rs (CliArgs / expected RunParameters),
#   S1 has not landed yet — those are S1's errors, NOT S2's. S2 is correct iff
#   zero errors name src/main.rs. (See Known Gotchas.)

# Lint the default targets (lib + bin). Do NOT use --all-targets (lints tests,
# which still fail until S3).
cargo clippy 2>&1 | tee /tmp/s2_clippy.log
# Expected: no warnings/errors.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (EXPECTED TO STILL FAIL — the S3 handoff)

```bash
# ⚠️ EXPECTED FAILURE — do NOT try to make this pass in S2.
cargo test --lib 2>&1 | tee /tmp/s2_test_lib.log
# Expected: COMPILE FAILURE in src/lib.rs's #[cfg(test)] mod tests block:
#   - E0433 "cannot find type `CliArgs`" at the `fn cli_for(...) -> CliArgs` helper.
#   - E0609 "no field `params`/`list_callbacks` on type `RunParameters`" in the
#     5 CLI test bodies.
# These are P1.M2.T1.S3's scope (rewrite the CLI tests against RunParameters).
# CRITICAL: NO error here may name src/main.rs — main.rs is a binary target, not
#   compiled by `cargo test --lib`. If a main.rs error appears, something is very
#   wrong (re-read your edit).
```

### Level 3: Integration Testing (the binary actually runs)

```bash
# The binary compiles (Level 1). Smoke-test its CLI surface (no keyboard needed
# for the arg-parsing paths; --help/--list-callbacks-without-device error cleanly).

# --help: clap prints help and exits 0 (does NOT reach the env::args/run path, but
# confirms the binary links and clap is configured).
cargo run --quiet -- --help >/dev/null 2>&1 && echo "help: ok" || echo "help: exit $?"

# No-action: clap's arg_required_else_help fires (exit 2) — confirms the binary
# runs far enough to invoke clap.
cargo run --quiet -- ; echo "no-action exit: $?"
# Expected: exit 2 (clap help-and-exit).

# --list-callbacks with no device: parse_cli_args returns Ok(QueryInfo), run()
# returns Err(DeviceNotFound) (no keyboard), main prints "Error: ..." exit 1.
# This exercises the env::args detection + run(params) + Err arm end-to-end
# WITHOUT needing the sweep to succeed.
cargo run --quiet -- --list-callbacks 2>&1 | head -3; echo "list-callbacks exit: ${PIPESTATUS[0]}"
# Expected: "Error: ..." (DeviceNotFound or similar) and exit 1. The env::args
#   detection ran (harmlessly, since run() errored before the sweep).
```

### Level 4: Creative & Domain-Specific Validation (the change-isolation proof)

```bash
# (1) ZERO stale `cli.*` references remain in main.rs (the binding is now `params`):
grep -nE "\bcli\.|\bcli\b" src/main.rs || echo "no 'cli' identifier in main.rs (good)"
# Expected: "no 'cli' identifier in main.rs (good)". (The old `cli` binding is
#   gone; `params` replaced it.)

# (2) ZERO stale `cli.params`/`cli.list_callbacks` accesses:
grep -nE "cli\.params|cli\.list_callbacks" src/main.rs || echo "no cli.params/cli.list_callbacks (good)"
# Expected: "no cli.params/cli.list_callbacks (good)".

# (3) Exactly ONE env::args detection + ONE `match run(params)`:
grep -c "std::env::args" src/main.rs        # Expected: 1
grep -c "match run(params)" src/main.rs     # Expected: 1

# (4) The use statement is unchanged (still 5 imports, no CliArgs):
grep -n "^use qmk_notifier" src/main.rs
# Expected: use qmk_notifier::{parse_cli_args, run, CommandResponse, RunCommand, RunParameters};

# (5) The match body (sweep loop) is intact — the list_callbacks guard + sweep:
grep -nE "if list_callbacks|callback_count|QueryCallback" src/main.rs
# Expected: the guard line + the sweep loop body (unchanged).

# (6) Confirm the full-build failure (if any) is confined to lib.rs/S3, NOT main.rs:
cargo build 2>&1 | grep -E "^error" | grep "main.rs" || echo "zero main.rs errors (good)"
# Expected: "zero main.rs errors (good)". (If S1 is applied, cargo build is fully
#   green; if not, errors name lib.rs, not main.rs.)
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 (THE GATE): `cargo build` (full) → **zero warnings**, **zero errors
      in `src/main.rs**.
- [ ] Level 1: `cargo clippy` (default lib+bin) → zero warnings.
- [ ] Level 1: `cargo fmt --check` → exit 0.
- [ ] Level 2: `cargo test --lib` → **fails ONLY in the lib `#[cfg(test)]` module**
      (E0433/E0609 on CliArgs/cli.params) — the S3 handoff. NOT in main.rs.
- [ ] Level 3: the binary runs (`--help`, no-action, `--list-callbacks` paths).

### Feature Validation

- [ ] `let list_callbacks = std::env::args().any(|a| a == "--list-callbacks");`
      is present **before** `parse_cli_args()`.
- [ ] The binding is `let params = match parse_cli_args() { Ok(params) => params, … }`.
- [ ] The six scalar copies read `params.*` (no `cli.params.*`).
- [ ] `let list_callbacks = cli.list_callbacks;` is removed.
- [ ] The dispatch is `match run(params) {`.
- [ ] The match body (4 arms + sweep loop) is unchanged; the `if list_callbacks`
      guard works against the env::args-derived bool.

### Code Quality Validation

- [ ] The use statement (line 1) is unchanged (no CliArgs imported; 5 imports stay used).
- [ ] `reset_sigpipe_to_default` + unsafe helper unchanged (Issue 4 / P1.M2.T3.S1).
- [ ] No `cli` identifier remains in main.rs.
- [ ] No file other than `src/main.rs` modified.

### Documentation & Deployment

- [ ] The env::args detection carries an explanatory `//` comment citing Issue 2 /
      PRD §3 / the out-of-band rationale.
- [ ] The "moves `params` by value" comment is updated (was "cli.params").
- [ ] No README/PRD/architecture/Cargo.toml change (those are P1.M3 / out of scope).

---

## Anti-Patterns to Avoid

- ❌ Don't "fix" `cargo test --lib`. After S2 it **still fails** — the lib
  `#[cfg(test)]` module references the deleted `CliArgs` (S3's scope). The S2 gate
  is `cargo build` (full) + `cargo clippy` (default), NOT the test suite.
- ❌ Don't use `cargo clippy --all-targets` as the S2 gate — it lints the test
  module (which still fails until S3). Use plain `cargo clippy` (lib+bin).
- ❌ Don't touch the match BODY (the 4 arms + the `--list-callbacks` sweep loop).
  The `if list_callbacks` guard works unchanged against the new env::args bool
  (same identifier, same `bool` type, same truth value). Only the block BEFORE
  `match run(params) {` changes.
- ❌ Don't change the use statement (line 1). `CliArgs` was never imported into
  main.rs (grep-proven), and all five imports (`parse_cli_args`, `run`,
  `CommandResponse`, `RunCommand`, `RunParameters`) stay used. Item note (f)
  ("Remove CliArgs if imported — verify") resolves to "verify: not imported; no
  change."
- ❌ Don't edit `src/lib.rs`, the lib `#[cfg(test)]` module, `error.rs`, `core.rs`,
  or `Cargo.toml`. S1 owns lib.rs; S3 owns the tests; Issue 4 (P1.M2.T3.S1) owns
  the unsafe cleanup; Issue 5 (P1.M2.T2.S1) owns the `-c` short flag.
- ❌ Don't touch `reset_sigpipe_to_default` or its `extern "C" { fn signal }`
  unsafe block. That is Issue 4 / P1.M2.T3.S1 (isolate via the `libc` crate).
- ❌ Don't rename the `--list-callbacks` string or change the sweep semantics. The
  env::args detection must match the exact flag clap registers (`--list-callbacks`,
  verbatim — see `build_cli_command` in lib.rs). A typo here silently disables the
  sweep with no compile error.
- ❌ Don't place the `list_callbacks` env::args line AFTER `parse_cli_args()`. The
  item mandates BEFORE (defensive against clap's `--help`/`--version`/error exits).
- ❌ Don't copy `command` out as a scalar. It is `RunCommand` (not `Copy`); only
  compare it (`params.command == RunCommand::ListDevices`) to derive `is_list`.
  Copying the five device-targeting scalars (all `Copy`) before `run(params)`
  moves `params` is correct.
- ❌ Don't conclude "S2 is broken" if `cargo build` fails while S1 hasn't landed.
  READ the error locations: main.rs errors ⟹ S2 issue; lib.rs errors ⟹ S1 not yet
  applied (parallel race, expected during parallel implementation).
- ❌ Don't skip `cargo build` "because the tree doesn't compile yet". `cargo build`
  (full) is precisely the gate that proves main.rs is self-consistent against the
  post-S1 lib.rs. If you only run `cargo build --lib`, you won't exercise main.rs
  at all.

---

**Confidence Score: 9/10** for one-pass implementation success. The deliverable is
a single, fully-specified contiguous edit to `src/main.rs` (verbatim old→new text,
content-grep anchor), with the unchanged match body / use statement / SIGPIPE
helper explicitly fenced off. The mechanics are trivial (rename a binding, re-point
six field accesses, relocate one bool to `env::args`) and all field types are
verified (RunParameters fields are `pub` + `Copy`; RunCommand derives `PartialEq`).
The one risk that keeps it from 10/10 is the **parallel dependency on S1**: S2's
`cargo build` only goes green once S1's lib.rs lands, so an implementer who applies
S2 in a pre-S1 tree will see build failures and must correctly attribute them
(main.rs ⟹ S2; lib.rs ⟹ S1-not-yet-applied) rather than "fixing" the wrong file.
The *Known Gotchas* + *Validation Level 1/4* sections are written to make that
attribution unambiguous. The downstream impact is zero (qmkonnect doesn't call
`parse_cli_args`, per Issue 2), and the `--list-callbacks` user-facing behavior is
preserved (env::args detection feeds the identical `if list_callbacks` guard). An
implementer who follows the blueprint produces a main.rs that compiles against
PRD §3's documented `run(parse_cli_args()?)` shape, with the S3 test-module failure
correctly isolated as the next subtask's scope.