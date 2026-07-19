# PRP — P1.M2.T1.S1 (bugfix): Change parse_cli_args return type and restructure parse_matches/select_command

---

## Goal

**Feature Goal**: Fix **Issue 2** (PRD §h3.1) — restore the PRD-§3-documented
public signature `pub fn parse_cli_args() -> Result<RunParameters, QmkError>`
(PRD.md line 154) by removing the undocumented `CliArgs` wrapper. This is a
pure refactor of the three CLI-parsing functions in `src/lib.rs`:
`select_command` drops the `list_callbacks` bool from its return type (returning
a bare `RunCommand`); `parse_matches` and `parse_cli_args` both return
`Result<RunParameters, QmkError>` directly; the `CliArgs` struct (and its doc
comment) are deleted. The CLI-only `--list-callbacks` sweep signal — previously
carried on `CliArgs.list_callbacks` — is **no longer returned by the library**;
its detection becomes `main.rs`'s responsibility out-of-band (via `std::env::args`).
`src/lib.rs` is the only file modified.

> ⚠️ **THIS TASK INTENTIONALLY LEAVES THE CRATE NON-COMPILING.** After this
> subtask, `cargo build` (full binary) and `cargo test --lib` **will fail**:
> `main.rs` still reads `cli.params` / `cli.list_callbacks` (fixed in
> **P1.M2.T1.S2**), and the lib's `#[cfg(test)]` module still references `CliArgs`
> (fixed in **P1.M2.T1.S3**). The **only** build that must pass is
> `cargo build --lib` (the non-test library). Do NOT "fix" the broken build by
> touching `main.rs`, the tests, or re-adding a `CliArgs` stub — see
> *Anti-Patterns*.

**Deliverable**: Four edits to **`src/lib.rs`** only:
1. `select_command` — return type `(RunCommand, bool)` → `RunCommand`; drop the
   bool from every `Ok` arm; rewrite its `///` doc.
2. `parse_matches` — return type `CliArgs` → `RunParameters`; change the
   `select_command`?` destructure + final `Ok(...)`; rewrite its `///` doc.
3. `parse_cli_args` — return type `CliArgs` → `RunParameters`; body unchanged;
   rewrite its `///` doc to cite PRD §3 + §11 and document the env::args handoff.
4. **Delete** the `pub struct CliArgs { ... }` definition and its `///` doc block.

**Success Definition**: `cargo fmt --check` → exit 0; `cargo build --lib` →
compiles with **zero warnings**; `cargo clippy --lib` → zero warnings;
`parse_cli_args()` has signature `pub fn parse_cli_args() -> Result<RunParameters, QmkError>`;
`select_command`/`parse_matches`/`parse_cli_args` no longer mention `CliArgs`;
the `CliArgs` struct is gone; **no code outside `src/lib.rs` is touched**;
`cargo build` (full) and `cargo test --lib` **fail, as expected**, only in
`main.rs` (S2 scope) and the lib test module (S3 scope) respectively.

## User Persona (if applicable)

**Target User**: Library consumers of `qmk_notifier`'s public API — primarily
`main.rs` (this repo's binary) and any third-party caller written to PRD §3.
(`qmkonnect`, the desktop daemon, builds `RunParameters` directly and never calls
`parse_cli_args`, so it is unaffected — confirmed by Issue 2.)

**Use Case**: A caller writes `run(parse_cli_args()?)` exactly as PRD §3 line 154
documents. Today that fails to compile (`parse_cli_args` returns `CliArgs`, not
`RunParameters`); after this task (and the S2/S3 follow-ups) it compiles.

**User Journey**: `parse_cli_args()` → `Result<RunParameters, QmkError>` →
`run(params)` → `Result<CommandResponse, QmkError>`. The `--list-callbacks`
multi-call sweep, if wanted, is detected by `main.rs` itself via `env::args`
(out-of-band) and orchestrated there — it is NOT a library concern.

**Pain Points Addressed**: Removes the undocumented public `CliArgs` type that
deviated from PRD §3's explicit contract; lets documented-against-the-spec code
compile unchanged; restores `main.rs` as the "thin wrapper around
`parse_cli_args` + `run`" that PRD §2/§11 describe.

## Why

- **PRD §3 contract drift (Issue 2)**: the spec mandates
  `pub fn parse_cli_args() -> Result<RunParameters, QmkError>;` (PRD.md line 154).
  The current `Result<CliArgs, QmkError>` is an undocumented deviation; any
  consumer written to the spec fails to compile. `CliArgs` is not mentioned
  anywhere in PRD §3.
- **`RunParameters` is the documented single-command type** (PRD §3); the
  `list_callbacks` sweep flag is a CLI-only convenience (PRD §11) that does not
  belong on a type `qmkonnect` uses directly. Carrying it on `CliArgs` forced an
  extra wrapper; moving it out-of-band (env::args in `main.rs`) keeps the library
  API spec-aligned and minimal.
- **Three-subtask split is deliberate**: S1 ships the library API change (this
  task); S2 adapts the binary (`main.rs`); S3 adapts the unit tests. Landing the
  library change first, in isolation, makes each follow-up a small, reviewable
  diff against a stable API. The intentional non-compiling intermediate is
  acceptable because **no current consumer breaks**: `qmkonnect` doesn't call
  `parse_cli_args`, and this repo's `main.rs` is fixed by the immediately-
  following S2 (same milestone, no release between them).
- **Low blast radius**: Issue 2 is filed "Major (public-API contract drift; low
  real-world blast radius)". The fix is mechanical and fully covered by the
  existing 5 CLI tests (rewritten in S3).

## What

All four edits are in `src/lib.rs`. The exact old→new text for each is below.
Locate each block by content grep (line numbers drift); the anchors given are
unique.

### Edit 1 — `select_command` (signature + every `Ok` arm + `///` doc)

**OLD** (the whole function; find it via `grep -n "fn select_command" src/lib.rs`):

```rust
/// Resolve the parsed CLI matches into a `(RunCommand, list_callbacks)` pair.
///
/// Priority (PRD §11; the `action` ArgGroup makes these mutually exclusive, so
/// the order is defensive): `--create-config` (removed-feature trap) > `--list` >
/// `--query-info` > `--list-callbacks` > `message` positional. `--list-callbacks`
/// maps to `RunCommand::QueryInfo` with `list_callbacks = true` — the actual
/// callback sweep is a multi-call flow handled by `main.rs`, not a single command.
///
/// `--create-config` is part of the `action` ArgGroup, so combining it with any
/// other action is rejected by clap as a conflict (exit 2) — consistent with the
/// rest of the group. Supplied alone it still surfaces the `RemovedFeature`
/// diagnostic.
fn select_command(matches: &ArgMatches) -> Result<(RunCommand, bool), QmkError> {
    if matches.get_flag("create-config") {
        return Err(QmkError::RemovedFeature(
            "Config file creation has been removed. All parameters must be provided explicitly."
                .to_string(),
        ));
    }
    if matches.get_flag("list") {
        return Ok((RunCommand::ListDevices, false));
    }
    if matches.get_flag("query-info") {
        return Ok((RunCommand::QueryInfo, false));
    }
    if matches.get_flag("list-callbacks") {
        return Ok((RunCommand::QueryInfo, true));
    }
    if let Some(message) = matches.get_one::<String>("message") {
        return Ok((RunCommand::SendMessage(message.to_string()), false));
    }
    Err(QmkError::MissingRequiredParameter(
        "one of message, --list, --query-info, or --list-callbacks".to_string(),
    ))
}
```

**NEW**:

```rust
/// Resolve the parsed CLI matches into a single [`RunCommand`].
///
/// Priority (PRD §11; the `action` ArgGroup makes these mutually exclusive, so
/// the order is defensive): `--create-config` (removed-feature trap) > `--list` >
/// `--query-info` > `--list-callbacks` > `message` positional. `--query-info` and
/// `--list-callbacks` BOTH map to [`RunCommand::QueryInfo`] — the library sees no
/// difference (both run a single QUERY_INFO). The CLI-only callback sweep that
/// distinguishes `--list-callbacks` is a multi-call flow owned by `main.rs`, not
/// the library's single-command [`run`] API; `main.rs` detects the flag itself
/// (out-of-band, via `std::env::args`) because [`RunParameters`] (PRD §3) carries
/// no sweep flag.
///
/// `--create-config` is part of the `action` ArgGroup, so combining it with any
/// other action is rejected by clap as a conflict (exit 2) — consistent with the
/// rest of the group. Supplied alone it still surfaces the `RemovedFeature`
/// diagnostic.
fn select_command(matches: &ArgMatches) -> Result<RunCommand, QmkError> {
    if matches.get_flag("create-config") {
        return Err(QmkError::RemovedFeature(
            "Config file creation has been removed. All parameters must be provided explicitly."
                .to_string(),
        ));
    }
    if matches.get_flag("list") {
        return Ok(RunCommand::ListDevices);
    }
    if matches.get_flag("query-info") {
        return Ok(RunCommand::QueryInfo);
    }
    if matches.get_flag("list-callbacks") {
        return Ok(RunCommand::QueryInfo);
    }
    if let Some(message) = matches.get_one::<String>("message") {
        return Ok(RunCommand::SendMessage(message.to_string()));
    }
    Err(QmkError::MissingRequiredParameter(
        "one of message, --list, --query-info, or --list-callbacks".to_string(),
    ))
}
```

> The two `return Ok(RunCommand::QueryInfo)` arms (query-info / list-callbacks)
> are intentionally kept separate for documentation clarity. **Verified**: this
> does NOT trip `clippy::if_same_then_else` or `match_same_arms` — those lints
> apply to if/else-if chains and match arms, not to separate `if` statements with
> early `return`. (Confirmed in a scratch crate this session; only expected
> dead-code warnings appeared, which don't apply here since `parse_matches`
> calls `select_command`.)

### Edit 2 — `parse_matches` (`///` doc + signature + tail; middle unchanged)

**OLD doc + signature** (find via `grep -n "fn parse_matches" src/lib.rs`):

```rust
/// Build [`CliArgs`] from already-parsed clap matches (PRD §11). Pure: takes
/// `&ArgMatches`, returns `Result<CliArgs, QmkError>` — never exits the process.
/// This is the testable core of [`parse_cli_args`].
fn parse_matches(matches: &ArgMatches) -> Result<CliArgs, QmkError> {
```

**NEW doc + signature**:

```rust
/// Build [`RunParameters`] from already-parsed clap matches (PRD §3, §11). Pure:
/// takes `&ArgMatches`, returns `Result<RunParameters, QmkError>` — never exits
/// the process. This is the testable core of [`parse_cli_args`].
fn parse_matches(matches: &ArgMatches) -> Result<RunParameters, QmkError> {
```

**OLD tail** (the block AFTER the vendor_id/product_id/usage_page/usage parsing,
which stays byte-for-byte unchanged):

```rust
    let (command, list_callbacks) = select_command(matches)?;

    let verbose = matches.get_flag("verbose");

    Ok(CliArgs {
        params: RunParameters::new(command, vendor_id, product_id, usage_page, usage, verbose),
        list_callbacks,
    })
}
```

**NEW tail**:

```rust
    let command = select_command(matches)?;

    let verbose = matches.get_flag("verbose");

    Ok(RunParameters::new(
        command,
        vendor_id,
        product_id,
        usage_page,
        usage,
        verbose,
    ))
}
```

> The middle of `parse_matches` (the four `let vendor_id = …`, `let product_id =
> …`, `let usage_page = …`, `let usage = …` blocks with their `?`/`unwrap_or`
> logic) is **untouched**. Only the `select_command`? destructure and the final
> `Ok(...)` change.

### Edit 3 — `parse_cli_args` (`///` doc + signature; body unchanged)

**OLD** (find via `grep -n "fn parse_cli_args" src/lib.rs`):

```rust
/// Parse command-line arguments into [`CliArgs`] (PRD §11 *CLI*).
///
/// The returned [`CliArgs::params`] holds a single [`RunCommand`] plus the
/// device-targeting fields, ready for [`run`]. Two diagnostic flags are exposed
/// on top of the legacy `message` / `--list` actions:
///
/// - `--query-info` ⇒ [`RunCommand::QueryInfo`] (`list_callbacks = false`).
///   `run` returns [`CommandResponse::Info`] on a typed-capable board.
/// - `--list-callbacks` ⇒ [`RunCommand::QueryInfo`] with `list_callbacks = true`.
///   The binary (`main.rs`) then performs a follow-up sweep: after `run` returns
///   [`CommandResponse::Info`], it loops `QueryCallback(0..callback_count)` and
///   prints each name. This multi-call flow is a CLI convenience (PRD §11) and is
///   NOT part of the library's single-command [`run`] API.
///
/// The action selectors (`message`, `--list`, `--create-config`, `--query-info`,
/// `--list-callbacks`) are mutually exclusive (clap `ArgGroup`). `--help`,
/// `--version`, unknown flags, and action conflicts are handled by clap's own
/// print-and-exit UX; post-parse logic errors surface as [`QmkError`]
/// (`RemovedFeature` for `--create-config`; `MissingRequiredParameter` when no
/// action is given).
pub fn parse_cli_args() -> Result<CliArgs, QmkError> {
    let matches = build_cli_command().get_matches();
    parse_matches(&matches)
}
```

**NEW**:

```rust
/// Parse command-line arguments into [`RunParameters`] (PRD §3 *Public API*,
/// §11 *CLI*).
///
/// Returns the documented `Result<RunParameters, QmkError>` so a caller can do
/// `run(parse_cli_args()?)` directly. The returned [`RunParameters`] holds a
/// single [`RunCommand`] plus the device-targeting fields, ready for [`run`].
///
/// `--query-info` and `--list-callbacks` BOTH resolve to [`RunCommand::QueryInfo`]
/// (the library sees no difference — both run a single QUERY_INFO; `run` returns
/// [`CommandResponse::Info`] on a typed-capable board). The CLI-only follow-up
/// sweep that distinguishes `--list-callbacks` (loop
/// `QueryCallback(0..callback_count)` after `run` returns
/// [`CommandResponse::Info`]) is a multi-call flow owned by the binary
/// (`main.rs`), NOT the library's single-command [`run`] API. Because
/// [`RunParameters`] (PRD §3) carries no such sweep flag, `main.rs` detects
/// `--list-callbacks` itself by inspecting raw `std::env::args` out-of-band.
///
/// The action selectors (`message`, `--list`, `--create-config`, `--query-info`,
/// `--list-callbacks`) are mutually exclusive (clap `ArgGroup`). `--help`,
/// `--version`, unknown flags, and action conflicts are handled by clap's own
/// print-and-exit UX; post-parse logic errors surface as [`QmkError`]
/// (`RemovedFeature` for `--create-config`; `MissingRequiredParameter` when no
/// action is given).
pub fn parse_cli_args() -> Result<RunParameters, QmkError> {
    let matches = build_cli_command().get_matches();
    parse_matches(&matches)
}
```

> The two-line body (`let matches = …; parse_matches(&matches)`) is **unchanged** —
> `parse_matches` now returns `RunParameters`, so it propagates automatically.

### Edit 4 — DELETE the `CliArgs` struct + its doc comment

**DELETE the entire block** (find via `grep -n "pub struct CliArgs" src/lib.rs`,
then read upward to the doc comment's first `///` line and downward to the
struct's closing `}`):

```rust
/// Parsed command-line arguments (PRD §11 *CLI*).
///
/// Wraps [`RunParameters`] (the single command + device-targeting fields that
/// [`run`] consumes) plus a CLI-only `list_callbacks` flag. When `list_callbacks`
/// is true, `command` is [`RunCommand::QueryInfo`] and the binary (`main.rs`)
/// performs a follow-up callback sweep: after `run` returns
/// [`CommandResponse::Info`], it loops `QueryCallback(0..callback_count)` and
/// prints each name. This multi-call flow is a CLI convenience (PRD §11) and is
/// NOT part of the library's single-command [`run`] API — hence the wrapper
/// rather than a field on `RunParameters` (which `qmkonnect` uses directly).
#[derive(Debug, Clone)]
pub struct CliArgs {
    /// The command + device-targeting fields, ready for [`run`].
    pub params: RunParameters,
    /// `true` when `--list-callbacks` was given. Signals `main.rs` to sweep
    /// callbacks after the initial `QueryInfo` returns `CommandResponse::Info`.
    pub list_callbacks: bool,
}

```

> Delete the doc comment, the `#[derive(Debug, Clone)]`, the struct, both fields,
> and the trailing blank line. Leave the surrounding code (`build_cli_command`
> below it; `RunParameters::new` impl above it) intact.

### Success Criteria

- [ ] `parse_cli_args` signature is exactly
      `pub fn parse_cli_args() -> Result<RunParameters, QmkError>`.
- [ ] `parse_matches` signature is exactly
      `fn parse_matches(matches: &ArgMatches) -> Result<RunParameters, QmkError>`.
- [ ] `select_command` signature is exactly
      `fn select_command(matches: &ArgMatches) -> Result<RunCommand, QmkError>`,
      and every `Ok` arm is a bare `Ok(RunCommand::…)` (no tuple, no bool).
- [ ] The `query-info` and `list-callbacks` arms both `return Ok(RunCommand::QueryInfo)`.
- [ ] `parse_matches` calls `let command = select_command(matches)?;` (no tuple
      destructure) and returns `Ok(RunParameters::new(command, vid, pid, page, usage, verbose))`.
- [ ] The `CliArgs` struct, its `#[derive(Debug, Clone)]`, its doc comment, and
      both fields are **gone**.
- [ ] `grep -n "CliArgs" src/lib.rs` returns matches **only inside the
      `#[cfg(test)] mod tests` block** (the `cli_for` helper + test bodies) —
      i.e. zero non-test references. (Those test references are S3's scope.)
- [ ] The three `///` doc comments are rewritten per Edits 1/2/3 (no `CliArgs`
      mention; parse_cli_args doc cites PRD §3 + §11 and documents the env::args
      handoff per the DOCS clause).
- [ ] `cargo fmt --check` → exit 0; `cargo build --lib` → **zero warnings**;
      `cargo clippy --lib` → zero warnings.
- [ ] `cargo build` (full) **fails** with E0609/E0308 errors **only in
      `src/main.rs`** (the S2 handoff) — NOT in lib.rs.
- [ ] `cargo test --lib` **fails** with E0433/E0609 errors **only in the lib
      `#[cfg(test)] mod tests` block** (the S3 handoff) — NOT in non-test lib.rs.
- [ ] No file other than `src/lib.rs` is modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim old→new text
> for all four edits is above; each block is located by a unique content grep;
> the unchanged middle of `parse_matches` is explicitly called out; the one
> subtle lint risk (two identical `return Ok(QueryInfo)` arms) is empirically
> resolved (no clippy warning); the intentional non-compiling intermediate is
> fully explained with the exact expected main.rs/test errors and their owning
> subtasks (S2/S3); and the validation gate (`cargo build --lib` clean) is
> verified by reasoning (every non-test `CliArgs` reference is in the change set).

### Documentation & References

```yaml
# MUST READ — the file being edited (all four edits live here)
- file: src/lib.rs
  why: "Holds the three functions to refactor (select_command, parse_matches,
        parse_cli_args) and the CliArgs struct to delete, plus the test module
        that BREAKS (S3's scope). Holds RunCommand/RunParameters/CommandResponse
        (defined here, consumed unchanged) and build_cli_command (the clap
        Command builder — UNCHANGED)."
  pattern: "Three pure parsing fns layered: parse_cli_args (public, calls
            build_cli_command().get_matches() then parse_matches) -> parse_matches
            (pure, parses vid/pid/page/usage then select_command) -> select_command
            (pure, maps action group to RunCommand). run() is separate and
            UNCHANGED."
  gotcha: "After this task ONLY `cargo build --lib` compiles. `cargo build`
           (full) fails in main.rs (S2); `cargo test --lib` fails in the lib test
           module (S3). Both failures are EXPECTED — do not 'fix' them here."

# MUST READ — the bug this fixes + the documented target signature + qmkonnect impact
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 2 (§h3.1) states the contract: PRD §3 line 154 mandates
        `pub fn parse_cli_args() -> Result<RunParameters, QmkError>;`; the current
        `Result<CliArgs, QmkError>` is undocumented drift; qmkonnect does NOT call
        parse_cli_args (builds RunParameters directly) so no current consumer
        breaks. The Suggested Fix (option a) is exactly this task: restore the
        documented signature and move the --list-callbacks sweep out-of-band."
  section: "Major Issues / Issue 2"

# MUST READ — the PRD public-API contract being restored
- file: PRD.md
  why: "§3 (Public API) line 154 is the exact target signature; §2/§11 document
        main.rs as a 'thin wrapper around parse_cli_args + run'. This task makes
        the library match §3; S2 makes main.rs match §2/§11."
  section: "3. Public API", "11. CLI"

# MUST READ — the binary that breaks (S2's scope) — understand WHY cargo build fails
- file: src/main.rs
  why: "main.rs:12 binds parse_cli_args() to `cli` (now RunParameters); lines
        28-34 read cli.params.* and cli.list_callbacks (E0609: RunParameters has
        no such fields); line 41 calls run(cli.params) (should be run(cli)).
        These are the EXACT errors `cargo build` will surface after S1 — they
        are S2's scope, NOT yours. Reading this file confirms the breakage is
        isolated to main.rs and that main.rs's logic (the callback sweep) will be
        re-anchored on env::args detection in S2."
  gotcha: "DO NOT edit main.rs in this task (P1.M2.T1.S2 owns it)."

# REFERENCE — the downstream consumer impact (none) + ecosystem
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/architecture/system_context.md
  why: "Documents qmkonnect's usage: it builds RunParameters directly and does
        not call parse_cli_args, confirming the API change is source-compatible
        for the one downstream consumer. Bounds the blast radius of this task."
  section: "Ecosystem Dependencies -> Downstream consumer: qmkonnect"

# REFERENCE — the sibling PRPs this task sequences against (do NOT implement them)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M1T3S2/PRP.md
  why: "The immediately-prior parallel item (stale-reply regression tests in
        core.rs). Confirms the working tree is POST-P1.M1 (72 tests, drain
        landed) and that core.rs/main.rs/error.rs are untouched by THIS task.
        Treat as the baseline state."

# REFERENCE — empirical evidence (change set, lint check, expected breakage, validation)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S1/research/notes.md
  why: "Documents the grep-proven complete change set (every non-test CliArgs/
        list_callbacks reference is in scope), the scratch-crate proof that the
        two identical Ok(QueryInfo) arms trip NO clippy lint, the airtight
        reasoning that cargo build --lib compiles clean, and the exact expected
        main.rs/test errors with their owning subtasks (S2/S3)."
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # name="qmk_notifier"; deps: clap, hidapi "2.4.1"
├── Cargo.lock
├── README.md
├── PRD.md
├── .gitignore
└── src
    ├── main.rs         # binary — BREAKS after this task (S2 fixes); DO NOT TOUCH
    ├── lib.rs          # <-- FILE TO EDIT (4 edits: 3 fn refactors + delete CliArgs)
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # transport (burst_to_one, send_raw_report, FakeHid...) — DO NOT TOUCH
```

### Desired Codebase tree with files to be added/modified

```bash
src/
└── lib.rs   # MODIFIED ONLY:
             #   (1) select_command: (RunCommand,bool) -> RunCommand  (+ doc rewrite)
             #   (2) parse_matches:   CliArgs -> RunParameters         (+ doc rewrite + tail)
             #   (3) parse_cli_args:  CliArgs -> RunParameters         (+ doc rewrite)
             #   (4) DELETE pub struct CliArgs { ... } + its doc comment
             # NOTE: the #[cfg(test)] mod tests block is LEFT BROKEN (S3's scope).
# (main.rs, error.rs, core.rs, Cargo.toml unchanged)
```

> No new files. One file modified (`src/lib.rs`). No new dependencies, no new
> types (CliArgs is removed; RunCommand/RunParameters already exist).

### Known Gotchas of our codebase & Library Quirks

```rust
// ⚠️ CRITICAL — THE CRATE INTENTIONALLY DOES NOT COMPILE AFTER THIS TASK.
//   parse_cli_args is the ONLY thing main.rs (and the lib tests) call that
//   changes type. After S1:
//     - `cargo build --lib`          -> COMPILES CLEAN (the success gate).
//     - `cargo build` (full bin)     -> FAILS in src/main.rs (E0609 on cli.params/
//       cli.list_callbacks; E0308 on run(cli.params)). This is P1.M2.T1.S2's scope.
//     - `cargo test --lib`           -> FAILS in the #[cfg(test)] mod tests block
//       (E0433 on `fn cli_for(...) -> CliArgs`; E0609 on cli.params/cli.list_callbacks).
//       This is P1.M2.T1.S3's scope.
//   DO NOT "fix" these by editing main.rs or the tests, and DO NOT re-add a
//   CliArgs stub to make the build green — that would undo this task's entire
//   purpose and duplicate S2/S3. The ONLY build that must pass is `cargo build --lib`.

// CRITICAL — select_command's two `return Ok(RunCommand::QueryInfo)` arms are
//   intentionally SEPARATE (one for --query-info, one for --list-callbacks),
//   kept for documentation clarity even though they return the same value.
//   VERIFIED (scratch crate /tmp/select_cmd_test): this trips NO clippy lint —
//   `if_same_then_else` and `match_same_arms` apply to if/else-if chains and
//   match arms, NOT to separate `if` statements with early `return`. Do NOT
//   collapse them into one arm "to satisfy clippy" — there is nothing to satisfy.

// CRITICAL — the MIDDLE of parse_matches is UNCHANGED. Only the `select_command`?
//   destructure (drop the bool) and the final `Ok(...)` (CliArgs{..} ->
//   RunParameters::new(..)) change. The four `let vendor_id/product_id/usage_page/
//   usage = ...` blocks with their `.transpose()?` / `.unwrap_or(DEFAULT_*)` logic
//   stay byte-for-byte identical. Do not rewrite the whole function.

// NOTE — `cargo build --lib` compiles the NON-TEST library only. The
//   `#[cfg(test)] mod tests` block (which still references CliArgs) is absent
//   from a non-test build, so it does not break `cargo build --lib`. main.rs is
//   a separate binary target, also absent from `--lib`. Hence `--lib` is clean.

// NOTE — NO new imports. RunCommand and RunParameters are defined in the SAME
//   file (lib.rs). The existing `use clap::{Arg, ArgAction, ArgGroup, ArgMatches,
//   Command};` stays fully used (build_cli_command + the &ArgMatches params). Do
//   not add or remove any `use` statement.

// NOTE — lib.rs line 1 is `mod core;`; there is NO `#![deny(warnings)]` or other
//   inner attribute, so a stray warning would not escalate to an error in
//   `cargo build --lib`. (None is expected anyway — verified by reasoning.)

// NOTE — qmkonnect (the one downstream consumer) does NOT call parse_cli_args
//   (Issue 2: it builds RunParameters directly), so this public-signature change
//   is source-compatible for qmkonnect. No coordination release needed.

// NOTE — `--create-config` is part of the `action` ArgGroup (mutually exclusive
//   with the other actions). select_command keeps returning Err(RemovedFeature)
//   for it UNCHANGED. The .short('c') flag on it is a SEPARATE issue (Issue 5,
//   P1.M2.T2.S1) — do NOT touch build_cli_command in this task.
```

## Implementation Blueprint

### Data models and structure

No new types. This task **removes** one type (`CliArgs`) and changes three
function signatures. `RunCommand`, `RunParameters`, `CommandResponse`,
`build_cli_command`, and the clap `ArgGroup` configuration are all consumed
unchanged.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/lib.rs and confirm the anchors (content grep, not line numbers)
  - `grep -n "pub struct CliArgs" src/lib.rs`        -> the struct to DELETE (+ its
          doc comment above, from the `/// Parsed command-line arguments...` line).
  - `grep -n "fn select_command" src/lib.rs`         -> the function to refactor
          (Edit 1); read its full body + doc comment.
  - `grep -n "fn parse_matches" src/lib.rs`          -> Edit 2; read its doc +
          signature AND its tail (the `select_command`? destructure + final Ok).
  - `grep -n "fn parse_cli_args" src/lib.rs`         -> Edit 3; read its doc +
          signature (body is 2 lines, unchanged).
  - CONFIRM main.rs + the lib test module reference CliArgs (so you EXPECT the
          post-task breakage): `grep -rn "CliArgs" src/main.rs` and the test-
          module hits from `grep -n "CliArgs" src/lib.rs` (the cli_for helper +
          test bodies). These are the S2/S3 break sites — do NOT touch them.
  - GOAL: know the four exact blocks to change and the expected breakage sites.

Task 2: EDIT src/lib.rs — Edit 1 (select_command)
  - REPLACE the whole select_command function + its /// doc with the NEW version
          in the "What / Edit 1" section. Changes: return type (RunCommand,bool)
          -> RunCommand; every `Ok((Variant, bool))` -> `Ok(Variant)`; doc rewrite.
  - KEEP: the Err(RemovedFeature) and Err(MissingRequiredParameter) arms, the
          priority order, and the action-group explanation (rephrased).

Task 3: EDIT src/lib.rs — Edit 2 (parse_matches doc + signature + tail)
  - REPLACE the parse_matches doc + signature line (CliArgs -> RunParameters).
  - REPLACE the parse_matches tail: `let (command, list_callbacks) =
          select_command(matches)?;` + `Ok(CliArgs { params: ..., list_callbacks })`
          -> `let command = select_command(matches)?;` + `Ok(RunParameters::new(..))`.
  - KEEP: the four middle `let vid/pid/page/usage = ...` blocks byte-for-byte
          identical. Do not touch them.

Task 4: EDIT src/lib.rs — Edit 3 (parse_cli_args doc + signature)
  - REPLACE the parse_cli_args doc block + signature (CliArgs -> RunParameters)
          with the NEW version in "What / Edit 3".
  - KEEP: the 2-line body (`let matches = build_cli_command().get_matches();
          parse_matches(&matches)`) UNCHANGED.

Task 5: EDIT src/lib.rs — Edit 4 (DELETE CliArgs + doc comment)
  - DELETE the entire CliArgs block: the `/// Parsed command-line arguments...`
          doc comment, `#[derive(Debug, Clone)]`, the struct, both fields, and the
          trailing blank line. Leave build_cli_command (below) and the
          RunParameters impl (above) intact.

Task 6: VALIDATE (scoped — see ⚠️ CRITICAL)
  - RUN: `cargo fmt`, then `cargo fmt --check` (exit 0).
  - RUN: `cargo build --lib`   -> MUST compile with ZERO warnings. THIS IS THE GATE.
  - RUN: `cargo clippy --lib`  -> zero warnings.
  - EXPECT (do NOT "fix"): `cargo build` (full) FAILS in src/main.rs only;
          `cargo test --lib` FAILS in the lib #[cfg(test)] mod tests only.
  - SANITY: `grep -n "CliArgs" src/lib.rs` -> hits ONLY inside the #[cfg(test)]
          mod tests block (S3's scope). Zero non-test references remain.
  - SANITY: `grep -n "pub fn parse_cli_args\|fn parse_matches\|fn select_command" src/lib.rs`
          -> the three signatures read RunParameters / RunParameters / RunCommand.
  - IF `cargo build --lib` warns "function `select_command` is never used" or
          "`parse_matches` is never used": they ARE used (parse_matches calls
          select_command; parse_cli_args calls parse_matches) — re-check you did
          not break the call chain.
```

### Implementation Patterns & Key Details

```rust
// === THE REFACTOR SHAPE (each layer drops one indirection) ===
//
//   BEFORE:                          AFTER:
//   parse_cli_args -> CliArgs        parse_cli_args -> RunParameters
//     -> parse_matches -> CliArgs      -> parse_matches -> RunParameters
//       -> select_command -> (RunCommand, bool)   -> select_command -> RunCommand
//
//   CliArgs was a { params: RunParameters, list_callbacks: bool } wrapper. The
//   list_callbacks bool is DROPPED from the library return path entirely; its
//   detection moves to main.rs out-of-band (env::args) in P1.M2.T1.S2.


// === WHY select_command NOW HAS TWO IDENTICAL Ok(QueryInfo) ARMS (and that's fine) ===
//
//   `--query-info`  -> Ok(RunCommand::QueryInfo)
//   `--list-callbacks` -> Ok(RunCommand::QueryInfo)
//
//   They look identical, but they document two distinct CLI intents. Kept
//   separate for readability. VERIFIED: no clippy lint fires (if-chain with early
//   returns, not an if/else-if chain or match). Do not collapse them.


// === WHY parse_cli_args BODY IS UNCHANGED ===
//
//   `let matches = build_cli_command().get_matches(); parse_matches(&matches)`
//   just forwards. parse_matches's return type changed (CliArgs -> RunParameters),
//   so parse_cli_args's return type follows automatically — no body edit needed,
//   only the signature + doc.


// === WHY cargo build --lib IS THE GATE (not cargo build / cargo test) ===
//
//   `cargo build --lib` compiles ONLY the non-test library (src/lib.rs minus its
//   #[cfg(test)] block). Every non-test CliArgs/list_callbacks reference is in
//   this task's change set (grep-proven), so --lib compiles clean. The test
//   block (cfg-gated out) and main.rs (separate binary target) are NOT compiled
//   by --lib, so their CliArgs references don't break it. The full `cargo build`
//   and `cargo test --lib` failures are the deliberate S2/S3 handoffs.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY — 3 fn refactors (select_command, parse_matches,
             parse_cli_args) + delete the CliArgs struct."

NO OTHER CHANGES:
  - imports:   "none — RunCommand/RunParameters are in-file; clap use-block unchanged."
  - types:     "CliArgs REMOVED; nothing added."
  - build_cli_command: "UNCHANGED (the ArgGroup/clap config is not touched; the
                        undocumented .short('c') is Issue 5 / P1.M2.T2.S1)."
  - run():     "UNCHANGED."
  - deps:      "none — no Cargo.toml change."

PUBLIC API SURFACE:
  - changes:  "parse_cli_args: Result<CliArgs,QmkError> -> Result<RunParameters,QmkError>
              (RESTORES the PRD §3 documented signature — Issue 2). CliArgs is REMOVED
              from the public API. This is BREAKING for any consumer that bound the Ok
              value as CliArgs, but the only consumer (qmkonnect) does not call it."
  - unchanged: "RunCommand, HostOs, CommandResponse, RunParameters, run, send_raw_report,
               list_hid_devices, parse_hex_or_decimal, all core:: re-exports, all
               QmkError variants/Display."

EXPECTED DOWNSTREAM (S2/S3 — do NOT implement here):
  - P1.M2.T1.S2 (main.rs): "rewire main.rs off CliArgs: `run(parse_cli_args()?)`
        directly, copy device-targeting scalars from the RunParameters, and detect
        --list-callbacks via std::env::args (out-of-band) for the callback sweep."
  - P1.M2.T1.S3 (tests):   "rewrite the 5 CLI tests (test_parse_query_info_flag,
        test_parse_list_callbacks_flag, test_query_info_combines_with_device_flags,
        test_message_and_list_still_parse, and the cli_for helper) to assert on
        RunParameters fields directly (e.g. cli.command, cli.usage_page) instead of
        cli.params.* / cli.list_callbacks. --list-callbacks can no longer be
        observed via the return value (it's now env::args in main.rs); S3 adjusts
        those assertions accordingly."

SCOPE BOUNDARY:
  - ONLY src/lib.rs is modified. Do NOT edit main.rs (S2), the #[cfg(test)] mod
    tests block (S3 — leave it broken), error.rs, core.rs, or Cargo.toml. Do NOT
    touch build_cli_command (Issue 5 is P1.M2.T2.S1). Do NOT touch run() (the
    typed-command dispatch is a different milestone).
```

## Validation Loop

### Level 1: Syntax & Style (the ONLY level that fully passes — the gate)

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# THE GATE: build the non-test library only. MUST compile with ZERO warnings.
cargo build --lib 2>&1 | tee /tmp/s1_build_lib.log
# Expected: "Finished `dev` profile [unoptimized ...] target(s) in ..." and NO
#   "warning:" lines.
# WHY --lib (not `cargo build`): the full build pulls in main.rs, which still
#   references CliArgs (S2's scope) and WILL fail. That is expected. --lib
#   compiles only src/lib.rs minus its #[cfg(test)] block, where every CliArgs
#   reference is now gone. If --lib warns "never used" on select_command /
#   parse_matches, you broke the call chain — re-check Edits 1-3.

# Lint the non-test library (default clippy).
cargo clippy --lib 2>&1 | tee /tmp/s1_clippy_lib.log
# Expected: no warnings/errors. (If a lint fires on the two identical
#   Ok(QueryInfo) arms — it won't, verified — do NOT collapse them; see gotchas.)

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (EXPECTED TO FAIL — the S3 handoff)

```bash
# ⚠️ EXPECTED FAILURE — do NOT try to make this pass in S1.
cargo test --lib 2>&1 | tee /tmp/s1_test_lib.log
# Expected: COMPILE FAILURE in the #[cfg(test)] mod tests block:
#   - E0433 "cannot find type `CliArgs`" at the `fn cli_for(...) -> CliArgs` helper.
#   - E0609 "no field `params`/`list_callbacks` on type `RunParameters`" in the
#     5 CLI test bodies.
# These are P1.M2.T1.S3's scope (rewrite the CLI tests against RunParameters).
# The lib.rs NON-TEST code (select_command/parse_matches/parse_cli_args) must NOT
# be the source of any error here — if it is, Edit 1/2/3 is wrong.
```

### Level 3: Integration Testing (EXPECTED TO FAIL — the S2 handoff)

```bash
# ⚠️ EXPECTED FAILURE — do NOT try to make this pass in S1.
cargo build 2>&1 | tee /tmp/s1_build_full.log
# Expected: COMPILE FAILURE in src/main.rs:
#   - main.rs: `cli.params.command` / `cli.params.*` -> E0609 (RunParameters has
#     no field `params`).
#   - main.rs: `cli.list_callbacks` -> E0609 (no field `list_callbacks`).
#   - main.rs: `run(cli.params)` -> E0308/E0609 (should be `run(cli)`).
# These are P1.M2.T1.S2's scope (rewire main.rs off CliArgs + env::args sweep
# detection). src/lib.rs must NOT appear in the error list — if it does, your
# edits are incomplete (re-run the grep sanity check in Level 4).
```

### Level 4: Creative & Domain-Specific Validation (the breakage-isolation proof)

```bash
# PROVE the breakage is correctly ISOLATED to the S2/S3 handoff sites.
# (1) Zero NON-TEST CliArgs references remain in lib.rs:
grep -n "CliArgs" src/lib.rs
# Expected: hits ONLY inside the `#[cfg(test)] mod tests { ... }` block
#   (the `fn cli_for(...) -> CliArgs` helper + `.expect("...CliArgs")` + the 5
#   test bodies). There must be NO hit on select_command/parse_matches/
#   parse_cli_args/the deleted struct. (If any non-test hit remains, you missed
#   an edit — fix it; --lib would also have warned.)

# (2) The three signatures read the new types:
grep -nE "fn select_command|fn parse_matches|pub fn parse_cli_args" src/lib.rs
# Expected:
#   fn select_command(matches: &ArgMatches) -> Result<RunCommand, QmkError> {
#   fn parse_matches(matches: &ArgMatches) -> Result<RunParameters, QmkError> {
#   pub fn parse_cli_args() -> Result<RunParameters, QmkError> {

# (3) No tuple-return / no list_callbacks field remain in non-test lib.rs:
grep -nE "Result<\(RunCommand, bool\)|list_callbacks" src/lib.rs
# Expected: the ONLY hits are doc-comment prose mentions you intentionally kept
#   (none required) AND the test-module references (S3). Critically, NO
#   `Ok((RunCommand::..., ` tuple and NO `pub list_callbacks: bool` field.

# (4) Confirm the full-build failure is confined to main.rs (not lib.rs):
cargo build 2>&1 | grep -E "^error" | grep -v "main.rs" || echo "all errors are in main.rs (good — S2 scope)"
# Expected: "all errors are in main.rs (good — S2 scope)" (i.e. no error line
#   names src/lib.rs).

# (5) Confirm the test-build failure is confined to the test module:
cargo test --lib 2>&1 | grep -E "^error\[" | grep -oE "src/lib.rs:[0-9]+" | sort -u
# Expected: all error locations are at line numbers INSIDE the #[cfg(test)] mod
#   tests block (the cli_for helper + the 5 CLI tests). None in select_command /
#   parse_matches / parse_cli_args.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 (THE GATE): `cargo build --lib` → **zero warnings**.
- [ ] Level 1: `cargo clippy --lib` → zero warnings.
- [ ] Level 1: `cargo fmt --check` → exit 0.
- [ ] Level 2: `cargo test --lib` → **fails ONLY in the `#[cfg(test)] mod tests`
      block** (E0433/E0609 on CliArgs/cli.params/cli.list_callbacks) — the S3
      handoff. NOT in non-test lib.rs.
- [ ] Level 3: `cargo build` (full) → **fails ONLY in `src/main.rs`** (E0609/E0308
      on cli.params/cli.list_callbacks/run(cli.params)) — the S2 handoff. NOT in
      lib.rs.

### Feature Validation

- [ ] `parse_cli_args` signature is `pub fn parse_cli_args() -> Result<RunParameters, QmkError>`.
- [ ] `parse_matches` signature is `fn parse_matches(matches: &ArgMatches) -> Result<RunParameters, QmkError>`.
- [ ] `select_command` signature is `fn select_command(matches: &ArgMatches) -> Result<RunCommand, QmkError>`.
- [ ] Both `--query-info` and `--list-callbacks` arms `return Ok(RunCommand::QueryInfo)`.
- [ ] `parse_matches` tail is `let command = select_command(matches)?; ... Ok(RunParameters::new(command, vid, pid, page, usage, verbose))`.
- [ ] The `CliArgs` struct, `#[derive(Debug, Clone)]`, its doc comment, and both
      fields are deleted.
- [ ] The three `///` doc comments are rewritten (no CliArgs mention;
      parse_cli_args cites PRD §3 + §11 and documents the env::args sweep handoff).
- [ ] `grep -n "CliArgs" src/lib.rs` → non-test hits = 0.

### Code Quality Validation

- [ ] The middle of `parse_matches` (vid/pid/page/usage parsing) is byte-for-byte unchanged.
- [ ] No new imports added or removed.
- [ ] `build_cli_command`, `run()`, `RunCommand`, `RunParameters`, `CommandResponse` unchanged.
- [ ] No file other than `src/lib.rs` modified.

### Documentation & Deployment

- [ ] parse_cli_args `///` doc cites PRD §3 *Public API* + §11 *CLI* (Mode A,
      code-internal doc).
- [ ] parse_cli_args doc documents that `--list-callbacks` detection is now
      `main.rs`'s responsibility via `std::env::args` (the DOCS clause requirement).
- [ ] No README/PRD/architecture/Cargo.toml change (those are P1.M3 / out of scope).

---

## Anti-Patterns to Avoid

- ❌ Don't try to make `cargo build` (full) or `cargo test --lib` pass. After S1
  they **must fail** — main.rs (S2) and the lib tests (S3) still reference CliArgs.
  The ONLY build that passes is `cargo build --lib`. Making the full build green
  here means you've either edited main.rs/tests (out of scope) or re-added a
  CliArgs stub (defeating the task). Both are wrong.
- ❌ Don't re-add `CliArgs` (or any wrapper) to "keep the crate compiling". The
  whole point of this task is to **remove** CliArgs. The intermediate non-compiling
  state is deliberate and is resolved by S2 + S3 in the same milestone.
- ❌ Don't edit `src/main.rs`. Its rewire (run(cli) directly, copy scalars, env::args
  sweep detection) is **P1.M2.T1.S2**. Touching it here duplicates S2 and risks a
  merge conflict (S2 runs after S1).
- ❌ Don't edit the `#[cfg(test)] mod tests` block. Rewriting the 5 CLI tests
  against `RunParameters` is **P1.M2.T1.S3**. Leave the broken `cli_for` helper and
  `cli.params.*`/`cli.list_callbacks` assertions exactly as they are.
- ❌ Don't rewrite the whole `parse_matches` function. Only the doc+signature and
  the tail (`select_command`?` destructure + final `Ok`) change. The four
  vid/pid/page/usage parsing blocks in the middle stay byte-for-byte identical.
- ❌ Don't collapse the two `return Ok(RunCommand::QueryInfo)` arms (query-info /
  list-callbacks) "to satisfy clippy". Verified: no clippy lint fires on them
  (if-chain with early returns, not an if/else-if chain). They're intentionally
  separate for documentation clarity.
- ❌ Don't touch `build_cli_command` or the clap `ArgGroup` config. The
  undocumented `.short('c')` on `--create-config` is **Issue 5 / P1.M2.T2.S1**,
  not this task. The `action` ArgGroup and all `.arg(...)` registrations are
  unchanged here.
- ❌ Don't change `run()`, `RunCommand`, `RunParameters`, `CommandResponse`,
  `send_raw_report`, or anything in core.rs/error.rs. This is a library-API
  signature refactor confined to the three CLI-parsing functions + the CliArgs
  deletion, all in lib.rs.
- ❌ Don't add `#![deny(warnings)]` or any inner attribute. lib.rs has none (line 1
  is `mod core;`); keep it that way. (None is needed — `cargo build --lib` is
  already zero-warning by construction.)
- ❌ Don't skip `cargo build --lib` "because the crate doesn't compile". That
  command is precisely the scoped gate that DOES compile and proves your lib.rs
  edits are self-consistent. If you only ever run `cargo build` (full), you'll see
  the expected main.rs failures and might wrongly conclude your edits are broken.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is four fully-specified edits to a single file (`src/lib.rs`), each with verbatim
old→new text and a content-grep anchor. The one subtle lint risk (two identical
`return Ok(QueryInfo)` arms) is empirically resolved in a scratch crate (no
clippy warning). The unusual aspect — a deliberately non-compiling intermediate —
is fully explained: `cargo build --lib` is the airtight success gate (every
non-test `CliArgs` reference is grep-proven to be in the change set), while the
full-build and test failures are precisely scoped to main.rs (S2) and the lib
test module (S3) with their exact expected errors enumerated. The downstream
impact is zero (qmkonnect doesn't call parse_cli_args, per Issue 2). An
implementer who follows the blueprint produces a lib.rs whose `parse_cli_args`
matches PRD §3 line 154 exactly, with `cargo build --lib` green and the breakage
correctly isolated to the documented S2/S3 handoff sites.