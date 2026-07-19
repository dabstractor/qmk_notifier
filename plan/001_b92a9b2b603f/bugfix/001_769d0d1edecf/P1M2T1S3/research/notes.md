# Research Notes — P1.M2.T1.S3: Update existing parse_matches / CLI tests for new return type

## Working-tree state (verified this session)

- **S1 is LANDED (committed)**: `git status --short` does NOT list `src/lib.rs` as
  modified. The non-test CLI functions are already the new versions:
  - `src/lib.rs:262` → `fn select_command(matches: &ArgMatches) -> Result<RunCommand, QmkError>`
  - `src/lib.rs:289` → `fn parse_matches(matches: &ArgMatches) -> Result<RunParameters, QmkError>`
  - `src/lib.rs:322` → `let command = select_command(matches)?;` (no tuple destructure)
  - `src/lib.rs:356` → `parse_matches(&matches)` (body of parse_cli_args, unchanged)
  - **No `pub struct CliArgs` anywhere in non-test code** (grep-proven).
- **S2 is IN PROGRESS (uncommitted)**: `git status --short` shows ` M src/main.rs`.
  S2's PRP rewires main.rs off CliArgs + env::args sweep detection. S3 does NOT
  depend on S2 being committed (see "Gate independence" below), but the milestone
  P1.M2.T1 needs all three.
- **`cargo test --lib --no-run` currently FAILS** with exactly one first error:
  `error[E0412]: cannot find type 'CliArgs' in this scope --> src/lib.rs:868:34`
  (exit 101). The compiler aborts at this type-resolution error in the `cli_for`
  helper's return type BEFORE surfacing the downstream E0609 "no field
  `params`/`list_callbacks`" errors in the 4 test bodies. Fixing the helper return
  type (`CliArgs` → `RunParameters`) makes the compiler proceed and THEN surface
  the E0609s — so the implementer will see a short error cascade.

## Complete, grep-verified change set (ALL in `src/lib.rs` `#[cfg(test)] mod tests`)

Project-wide grep confirms the ONLY `CliArgs` references in the whole repo are:
```
./src/lib.rs:868:    fn cli_for(args: &[&str]) -> CliArgs {
./src/lib.rs:872:        parse_matches(&matches).expect("test args should resolve to CliArgs")
```
And the ONLY `cli.params` / `cli.list_callbacks` references are in lines 878-919.
No other `.rs` file, no `tests/` dir, no `examples/`, no `benches/`, no doctests.

### Sites requiring change (5 = 1 helper + 4 test fns)

| Line(s) | Test / helper | Change |
|---|---|---|
| 868 | `fn cli_for(args: &[&str]) -> CliArgs` | return type → `RunParameters` |
| 872 | `.expect("...CliArgs")` | message → "...RunParameters" (cosmetic/accuracy) |
| 877-884 | `test_parse_query_info_flag` | `let cli`→`let params`; drop `.params`; DELETE `assert!(!cli.list_callbacks)` |
| 889-892 | `test_parse_list_callbacks_flag` | `let cli`→`let params`; drop `.params`; DELETE `assert!(cli.list_callbacks)` (keep QueryInfo assert) |
| 898-908 | `test_query_info_combines_with_device_flags` | `let cli`→`let params`; drop `.params`; DELETE `assert!(!cli.list_callbacks)` |
| 913-919 | `test_message_and_list_still_parse` | `let cli`→`let params` (×2, shadows); drop `.params` (×2); DELETE `assert!(!cli.list_callbacks)` (×2) |

Total `cli.list_callbacks`/`!cli.list_callbacks` assertion deletions: **6 lines**
(lines 879, 892, 908, 915, 919 = 5 unique + 1 duplicate = actually 6 assertion
statements across the 4 tests — recount: 879, 892, 908, 915, 919 = 5 statements;
test_message has two (915 + 919), so 5 total. See PRP for exact verbatim.)

### Sites requiring NO change (verified)

- `test_action_selectors_are_mutually_exclusive` (~line 922-940): only checks
  `build_cli_command().try_get_matches_from(...).is_err()` for clap conflicts.
  Does NOT call parse_matches or touch the result type. The `"--list-callbacks"`
  strings in its arg slices (lines 928-929) are CLI FLAG strings (the flag still
  exists), NOT field references. → UNCHANGED.
- `test_no_action_given_is_missing_parameter` (~line 943-949): calls
  `parse_matches(&matches)` but only `matches!(result, Err(QmkError::MissingRequiredParameter(_)))`.
  The Ok type changed (CliArgs→RunParameters) but this test never inspects Ok. → UNCHANGED.

## RunParameters fields (lib.rs:120-131) — confirmed for the test assertions

```rust
#[derive(Debug, Clone)]
pub struct RunParameters {
    pub command: RunCommand,
    pub vendor_id: Option<u16>,
    pub product_id: Option<u16>,
    pub usage_page: u16,
    pub usage: u16,
    pub verbose: bool,
}
```
All fields `pub`. `command: RunCommand` (NOT Copy — only compared/pattern-matched).
The other five are `Copy` (Option<u16>, u16, bool) but the tests don't copy them out;
they access fields directly on the binding.

## RunCommand (lib.rs:18-45) — patterns used by the tests are valid

```rust
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RunCommand {
    SendMessage(String),
    ListDevices,
    QueryInfo,
    QueryCallback(u8),
    SetOs(HostOs),
    // ApplyHostContext { ... } (further down)
}
```
- Derives `PartialEq, Eq` (line 18) → `assert_eq!(params.command, X)` would compile
  (but the existing tests use `matches!`, which needs no PartialEq — keep `matches!`).
- Test patterns all valid post-S1: `RunCommand::QueryInfo`,
  `RunCommand::SendMessage(s) if s == "hello"`, `RunCommand::ListDevices`.

## Binding rename decision: `cli` → `params`

- Item description mandates: "Change the parse_matches result binding from
  `let cli = ...` to `let params = ...`."
- Consistent with S2's PRP (main.rs binding is `params`) and with the RunParameters
  type name. After rename, `cli.params.X` → `params.X` (drop the `.params`).
- In `test_message_and_list_still_parse`, two `let cli = ...` shadow each other;
  renaming both to `let params = ...` is valid Rust shadowing (same fn, sequential).
  No "unused variable" warning because each is used by the immediately-following
  assertion before the next binding shadows it.

## Gate independence: S3's gate is `cargo test --lib` (S2-independent)

- `cargo test --lib` compiles ONLY the library crate + its `#[cfg(test)]` harness.
  It does NOT compile `src/main.rs` (separate binary target). Therefore:
  - `cargo test --lib` passing depends on **S1 (lib non-test) + S3 (lib test mod)**
    only — NOT S2.
  - S1 is already committed (lib non-test is green). So once S3 lands, `cargo test
    --lib` is green regardless of whether S2 has been committed.
- `cargo build` (full) and `cargo test` (full) DO compile main.rs → they need S2
  too. These are the MILESTONE (P1.M2.T1) gates, not the S3-scoped gate.
- `cargo clippy --all-targets` lints the test module → it FAILS today (E0412) and
  PASSES once S3 lands (S2-independent for the lib-test portion). Use it as an S3
  gate. (`cargo clippy --lib` is S1's gate — already green.)

## list-callbacks prose vs field references (scope disambiguation)

`grep -n "list-callbacks\|list_callbacks" src/lib.rs` returns ~24 hits, but they
split cleanly:
- **`list-callbacks` (hyphenated CLI FLAG string)** — lines 161, 227, 228, 238,
  250, 251, 253, 275, 282, 338, 341, 346, 349, 865, 889, 928, 929. ALL CORRECT
  (the flag is still registered as `--list-callbacks` in build_cli_command; doc
  prose describing it is accurate). OUT OF SCOPE.
- **`list_callbacks` (underscore FIELD/identifier)** — lines 879, 892, 908, 915,
  919. ALL in the test module (the deleted `CliArgs.list_callbacks` field
  accesses). IN SCOPE — these 5 assertion statements get DELETED.

No `list_callbacks` identifier remains in non-test lib.rs (S1 already removed them
from select_command / parse_matches / parse_cli_args / the deleted struct).

## cargo doc safety

- No doctests in lib.rs (grep for ` ``` ` code fences → zero hits). So `cargo doc`
  runs no code; it only renders `///` prose.
- No `[`CliArgs`]` intra-doc-link references remain (the only `CliArgs` strings in
  the repo are the two test-module sites). S1 rewrote the doc comments on
  select_command/parse_matches/parse_cli_args to reference `[RunParameters]` /
  `[RunCommand]` instead. → `cargo doc --no-deps` is safe; not an S3 concern.

## No external research needed

This is a pure Rust unit-test mechanical refactor. No new libraries, no new
patterns, no clap API changes (the tests already use the stable
`try_get_matches_from` + `build_cli_command` API). The only "knowledge" required:
- `matches!` macro (std prelude, no PartialEq needed).
- Rust shadowing semantics (re-binding same name in a fn).
- The S1 contract (parse_matches → RunParameters) — fully documented in
  P1M2T1S1/PRP.md.

## Confidence: 10/10

The change set is line-precise, grep-verified across the entire repo, with
verbatim old→new text. No ambiguity, no hidden references, no type-design
decisions (RunParameters fields are all pub; RunCommand patterns unchanged). The
one thing to watch: the E0412→E0609 error cascade (fixing the helper return type
surfaces the body field errors) — but that's the natural compile flow, not a
problem. S3 is the most mechanical of the three subtasks.