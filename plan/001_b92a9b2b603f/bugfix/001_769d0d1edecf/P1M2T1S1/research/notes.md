# Research Notes — P1.M2.T1.S1 (bugfix): Change parse_cli_args → RunParameters, restructure parse_matches/select_command, remove CliArgs

## Task in one line

Refactor `src/lib.rs` so the three CLI-parsing functions return the PRD-§3
documented type (`Result<RunParameters, QmkError>`) instead of the undocumented
`CliArgs` wrapper, and **delete `CliArgs`**. `src/lib.rs` is the only file
modified. **The crate intentionally does NOT compile after this subtask** —
`main.rs` (S2) and the lib's test module (S3) still reference `CliArgs`; those
are fixed by the immediately-following subtasks.

## Why it's safe to land a non-compiling intermediate

| Consumer of parse_cli_args / CliArgs | Impact | Owned by |
|---|---|---|
| `qmkonnect` (downstream) | **None** — qmkonnect builds `RunParameters` directly and never calls `parse_cli_args` (Issue 2: "qmkonnect does not call parse_cli_args"). | n/a |
| `src/main.rs` (binary) | **Breaks**: `let cli = parse_cli_args()?` binds `RunParameters` but main reads `cli.params.*` / `cli.list_callbacks` (E0609) and calls `run(cli.params)` (E0308/E0609). | **P1.M2.T1.S2** (next) |
| `src/lib.rs` `#[cfg(test)] mod tests` | **Breaks**: `fn cli_for(...) -> CliArgs` (E0433) + test bodies `cli.params.*` / `cli.list_callbacks` (E0609). | **P1.M2.T1.S3** (after S2) |
| `src/lib.rs` non-test code | **Clean** — every non-test `CliArgs`/`list_callbacks` reference is in this task's change set (verified by grep, below). | **This task** |

So `cargo build --lib` (non-test) compiles cleanly and is the **success gate**;
`cargo build` (full bin) and `cargo test --lib` failing is **expected** and is the
S2/S3 handoff.

## Current lib.rs CLI-parsing surface (verified live, this session)

```
CliArgs struct        lib.rs ~156-171   pub struct CliArgs { params: RunParameters, list_callbacks: bool }  (+ doc block)
select_command        lib.rs ~264-301   fn select_command(&ArgMatches) -> Result<(RunCommand, bool), QmkError>
parse_matches         lib.rs ~301-345   fn parse_matches(&ArgMatches) -> Result<CliArgs, QmkError>
parse_cli_args        lib.rs ~347-369   pub fn parse_cli_args() -> Result<CliArgs, QmkError>
```

## Grep: ALL non-test lib.rs references to CliArgs / list_callbacks (the change set)

```
CliArgs:        ~165 (struct), ~301-302 (parse_matches doc), ~304 (parse_matches sig),
                ~341 (Ok(CliArgs{..}) construction), ~347+349 (parse_cli_args doc),
                ~367 (parse_cli_args sig)
list_callbacks: ~157+170 (CliArgs doc + field — removed with CliArgs),
                ~265+270 (select_command doc — rewritten),
                ~337 (let (command, list_callbacks) = select_command(matches)? — destructure dropped),
                ~343 (CliArgs field in construction — removed),
                ~353+355 (parse_cli_args doc — rewritten)
TEST module:    ~881 (fn cli_for -> CliArgs), ~885 (.expect("...CliArgs")),
                ~887-932 (cli.params.* / cli.list_callbacks in 5 tests)  → S3, untouched here
```

Every non-test reference is in the change set ⇒ `cargo build --lib` is clean.

## The 4 verbatim changes (this is the entire deliverable)

### (a) select_command: `Result<(RunCommand, bool), QmkError>` → `Result<RunCommand, QmkError>`
- Signature: drop `, bool` from the return type.
- Each `Ok((Variant, false))` → `Ok(Variant)`. `Ok((QueryInfo, true))` → `Ok(QueryInfo)`.
- The `Err(...)` arms (create-config, missing) are unchanged.
- Rewrite the `///` doc: "into a `(RunCommand, list_callbacks)` pair" → "into a single [`RunCommand`]"; note both `--query-info` and `--list-callbacks` map to `QueryInfo`, and the sweep flag is main.rs's job out-of-band.

### (b) parse_matches: `Result<CliArgs, QmkError>` → `Result<RunParameters, QmkError>`
- Tail change ONLY: `let (command, list_callbacks) = select_command(matches)?;` → `let command = select_command(matches)?;`
- `Ok(CliArgs { params: RunParameters::new(command, ...), list_callbacks })` → `Ok(RunParameters::new(command, vendor_id, product_id, usage_page, usage, verbose))`
- The vendor_id/product_id/usage_page/usage parsing block in the middle is UNCHANGED.
- Rewrite the `///` doc: "Build [`CliArgs`]" → "Build [`RunParameters`]"; drop the `Result<CliArgs,..>` mention.

### (c) parse_cli_args: `Result<CliArgs, QmkError>` → `Result<RunParameters, QmkError>`
- Signature only + doc comment. Body (`let matches = build_cli_command().get_matches(); parse_matches(&matches)`) is UNCHANGED (parse_matches now returns RunParameters, so it propagates).
- Rewrite the `///` doc block to: (1) cite PRD §3 *Public API* + §11 *CLI*; (2) document the documented signature `Result<RunParameters, QmkError>` enabling `run(parse_cli_args()?)`; (3) state `--query-info` and `--list-callbacks` BOTH → `QueryInfo` and the sweep is main.rs's job via `std::env::args` out-of-band (the DOCS clause requirement).

### (d) DELETE the CliArgs struct + its doc comment (lib.rs ~156-171)
- Remove the entire block from `/// Parsed command-line arguments (PRD §11 *CLI*).` through the struct's closing `}`.
- This includes the two field doc comments and `#[derive(Debug, Clone)]`.

## Empirical verifications (this session)

1. **`cargo build --lib` compiles clean** — reasoning: every non-test lib.rs
   `CliArgs`/`list_callbacks` reference is in the change set (grep above); the
   test-module references are `#[cfg(test)]` (absent from a non-test build);
   main.rs is a separate target. No other file references CliArgs.
2. **No clippy lint on the two identical `return Ok(RunCommand::QueryInfo)` if-arms**
   — verified in `/tmp/select_cmd_test`: clippy reports ONLY dead_code warnings
   (the scratch fn has no caller; irrelevant in the real crate where
   `parse_matches` calls it). `if_same_then_else`/`match_same_arms` do NOT fire
   (this is an if-chain with early returns, not an if/else-if chain or match).
3. **No new imports needed / no unused-import warning** — `RunCommand` and
   `RunParameters` are defined in the SAME file (lib.rs); the existing
   `use clap::{Arg, ArgAction, ArgGroup, ArgMatches, Command};` is still fully
   used by `build_cli_command` + the `&ArgMatches` params. No `use` added/removed.
4. **`lib.rs` has no `#![deny(warnings)]` / no inner attributes** (line 1 is
   `mod core;`) — so a stray warning would not turn into an error in
   `cargo build --lib`. (None is expected anyway.)

## Expected breakage (the S2/S3 handoffs — NOT to be "fixed" here)

After this task:
- `cargo build` (full) → **fails** in `src/main.rs`:
  - `main.rs:28` `cli.params.command` → E0609 (RunParameters has no field `params`)
  - `main.rs:29-33` `cli.params.*` → E0609
  - `main.rs:34` `cli.list_callbacks` → E0609 (no field `list_callbacks`)
  - `main.rs:41` `run(cli.params)` → E0609 (should become `run(cli)`)
  These are the **P1.M2.T1.S2** scope (main.rs env::args re-detection).
- `cargo test --lib` → **fails** in the lib's `#[cfg(test)] mod tests`:
  - `cli_for` helper returns `CliArgs` (E0433) + test bodies use `cli.params.*`/
    `cli.list_callbacks` (E0609). These are the **P1.M2.T1.S3** scope (rewrite the
    5 CLI tests against `RunParameters`).
- `cargo build --lib` → **compiles clean, zero warnings** ← THIS is the gate.

## Test-count math (informational — NOT validated by this task)

Today `cargo test --lib` = 72 (per P1.M1.T3.S2 PRP baseline: 70 + 2 stale-reply
tests). This task does NOT add or run tests (the suite is broken until S3). After
S3 lands, the 5 existing CLI tests are rewritten (still 5) and the count returns
to 72 (or 72+ if S3 adds cases). This task's gate is `cargo build --lib`, not
`cargo test`.

## The one subtlety the implementing agent MUST internalize

**Do not "fix" the broken build by touching main.rs or the tests, and do not
re-introduce a CliArgs stub.** The non-compiling crate is the deliberate
hand-off state: S1 ships the lib API change, S2 adapts the binary, S3 adapts the
tests. If you make `cargo build` pass here, you have (a) duplicated S2/S3 work and
(b) very likely re-introduced the very `CliArgs` wrapper this task exists to
remove. The ONLY build that must pass after S1 is `cargo build --lib`.