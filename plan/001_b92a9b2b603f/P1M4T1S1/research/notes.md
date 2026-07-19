# P1.M4.T1.S1 Research Notes — `--query-info` / `--list-callbacks` CLI flags

## F0 — Decision: introduce a CLI-only `CliArgs` wrapper (do NOT pollute `RunParameters`)

`--query-info` maps cleanly to `RunCommand::QueryInfo` (a single command — no API
change needed). `--list-callbacks` is the hard one: the item (§11 of the work
item) says it must "run QueryInfo first, then loop QueryCallback(0..count)" in
`main.rs`. That is a **multi-call** flow — but `parse_cli_args` returns a single
`RunParameters` (PRD §3), and `run(params)` executes ONE command. So `main.rs`
needs a channel to learn "`--list-callbacks` was given" beyond the single
`RunCommand`.

Options considered:
- **(A) Add `list_callbacks: bool` to `RunParameters`.** REJECTED: pollutes the
  library's core parameter type (PRD §3) that `qmkonnect` uses directly. A CLI
  flag is a leaky abstraction on a transport type.
- **(B) A new `RunCommand` variant** (`ListCallbacks`). REJECTED: `RunCommand` is
  the protocol command enum (PRD §3, §10); a CLI-only orchestration variant would
  corrupt the wire-protocol model.
- **(C) `CliArgs { params: RunParameters, list_callbacks: bool }` wrapper.**
  CHOSEN: keeps `RunParameters` library-pure; the CLI concern stays in a CLI type.
  `parse_cli_args` returns `Result<CliArgs, QmkError>` instead of
  `Result<RunParameters, QmkError>`.

PRD §3 deviation check: PRD §3 writes `parse_cli_args() -> Result<RunParameters,
QmkError>`. PRD §11 explicitly says diagnostics are "optional conveniences … not
required for `qmkonnect`, which uses the library directly." `qmkonnect` calls
`run()`, NOT `parse_cli_args`. So changing `parse_cli_args`'s return type affects
only `main.rs` (this item) and human CLI users — zero impact on `qmkonnect`. The
deviation is low-impact and is the item's natural consequence ("a CLI-level
concern, not a single RunCommand"). `CliArgs` STILL contains `RunParameters`, so
library consumers wanting the params keep full access.

## F1 — Empirical compile/parse proof (scratch crate, clap 4.5)

A faithful scratch crate (`/tmp/clap_scratch`) mirroring the real design —
`build_cli_command()` + `select_command(&ArgMatches)` + `parse_matches(&ArgMatches)`
+ `CliArgs` + the `main.rs` partial-move/sweep pattern — was compiled and tested:

- `cargo build` / `cargo clippy`: only scratch-only warnings (non-`pub`
  `RunParameters` in a binary → `private_interfaces`; unused variants). In the
  REAL crate `RunParameters`/`RunCommand`/`HostOs` are `pub`, so `pub struct
  CliArgs { pub params: RunParameters, .. }` produces NO such warning.
- `cargo test`: **6 tests pass** — query-info→QueryInfo, list-callbacks→QueryInfo
  + list_callbacks=true, query-info WITH --vendor-id/-v, message/list still work,
  mutual-exclusivity (3 conflict cases return `Err`), no-action → error.
- `cargo fmt`: scratch one-liners differ (expected); real multi-line code is
  fmt-clean.

This proves the 4 riskiest constructs behave as designed: (1) `ArgGroup` mutual
exclusivity, (2) `try_get_matches_from` for no-exit testing, (3) the pure
`build_cli_command`/`parse_matches` split, (4) main.rs extract-Copy-fields-then-
move-`cli.params`-into-`run` + sweep.

## F2 — Testability refactor: split parse_cli_args into pure, testable pieces

The CURRENT `parse_cli_args` calls `cmd.get_matches()` (reads `std::env::args`,
EXITS the process on any clap error — including `--help`). It is therefore NOT
unit-testable, and the existing test suite has ZERO `parse_cli_args` tests (all
construct `RunParameters` directly). This item is the FIRST where the new logic
(flag → command mapping) is PURE and hardware-free, so testing it is high-value.

Refactor into three pieces (all in `src/lib.rs`):
- `fn build_cli_command() -> Command` — pure; returns the clap `Command` with all
  args + the `ArgGroup`.
- `fn select_command(matches: &ArgMatches) -> Result<(RunCommand, bool), QmkError>`
  — pure; the if/else priority chain (create-config → list → query-info →
  list-callbacks → message → MissingRequiredParameter). Returns `(command,
  list_callbacks)`.
- `fn parse_matches(matches: &ArgMatches) -> Result<CliArgs, QmkError>` — pure;
  calls `select_command`, then extracts vendor_id/product_id/usage_page/usage/
  verbose (unchanged logic) and builds `CliArgs`.

`pub fn parse_cli_args()` becomes a thin wrapper:
`parse_matches(&build_cli_command().get_matches())` — `get_matches()` preserves
the real binary's UX (clap owns `--help`/`--version`/conflict printing + exit).
Tests call `build_cli_command().try_get_matches_from(args)` (NO exit) to build
`ArgMatches`, then feed them to the pure `parse_matches`/`select_command`. This
isolates the new mapping logic and is fully deterministic in CI.

CRITICAL: `try_get_matches_from` returns `Result` and never exits — so conflict
cases are assertable (`assert!(...try_get_matches_from(["q","--query-info","msg"]).is_err())`).
NEVER call `get_matches`/`get_matches_from` in tests — those call
`std::process::exit` on error and would kill the `cargo test` runner.

## F3 — clap `ArgGroup` semantics (authoritatively confirmed)

- `ArgGroup::new("action").args(["message","list","query-info","list-callbacks"])
  .multiple(false).required(false)` ⇒ **at most ONE** of these args may be
  present; zero is allowed. (clap docs: "By default `ArgGroup::multiple` is set to
  false" → mutually exclusive; `required(false)` → zero allowed.)
- The device-targeting flags (`--vendor-id`,`--product-id`,`--usage-page`,
  `--usage`,`--verbose`) are NOT in the group — they remain orthogonal and may
  combine with any single action (e.g. `--query-info --vendor-id 0xFEED -v`).
  Verified in scratch test `query_info_with_device_flags`.
- `--create-config` is intentionally NOT in the group (it is a removed-feature
  trap checked FIRST in `select_command`, matching current behavior).
- `arg_required_else_help(true)` + group `required(false)` coexist fine: NO args
  ⇒ help shown; one action ⇒ ok; multiple actions ⇒ clap conflict error; an arg
  but no action (e.g. `--verbose` alone) ⇒ group unsatisfied-but-not-required ⇒
  reaches `select_command`'s final `else` ⇒ `MissingRequiredParameter`. Verified
  in scratch test `no_action_errors`.

Priority order per the work item (§c): `--list` > `--query-info` >
`--list-callbacks` > `message`. Because the `ArgGroup` enforces exclusivity, only
one action is ever set, so the if/else order is defensive — but it is kept in the
stated priority for clarity.

## F4 — `main.rs` multi-call sweep design (the `--list-callbacks` behavior)

`main.rs` currently: `parse_cli_args()? → run(params)? → println!("{:?}", resp)`.
For `--list-callbacks` it must call `run()` multiple times (once for `QueryInfo`,
then once per callback index). New flow:

```rust
let cli = parse_cli_args()?;
// Copy out the device-targeting scalars BEFORE moving cli.params into run().
let (vid, pid, page, usage, verbose) = (cli.params.vendor_id, cli.params.product_id,
    cli.params.usage_page, cli.params.usage, cli.params.verbose);
let list_callbacks = cli.list_callbacks;

match run(cli.params) {                       // moves cli.params
    Ok(CommandResponse::Info { callback_count, .. }) if list_callbacks => {
        println!("callbacks ({callback_count}):");
        for index in 0..callback_count {
            let p = RunParameters::new(RunCommand::QueryCallback(index), vid, pid, page, usage, verbose);
            match run(p) {
                Ok(CommandResponse::CallbackName { index, name }) =>
                    println!("  {index}: {}", name.unwrap_or_else(|| "(unnamed)".into())),
                Ok(other) => eprintln!("  callback {index}: unexpected response {other:?}"),
                Err(e)    => eprintln!("  callback {index}: error: {e}"),
            }
        }
    }
    Ok(response) => println!("{response:?}"),  // --query-info, --list, message, or
                                               // a non-capable device (Timeout/Legacy)
    Err(e) => { eprintln!("Error: {e}"); std::process::exit(1); }
}
```

Notes:
- Extracting `Copy` fields (`Option<u16>`,`u16`,`bool`) before `run(cli.params)`
  is a standard partial setup; `cli.params` is then moved whole into `run`.
  Compiled cleanly in scratch (`main()` mirror).
- If `QueryInfo` returns `Timeout`/`Legacy` (non-typed-capable device) the guard
  fails → generic `Ok(response) => println!` — the user sees the raw response.
  Reasonable: can't enumerate callbacks on a non-capable device.
- The sweep re-opens the device per callback (each `run()` is independent). This
  is slightly inefficient but is EXACTLY what the work item specifies ("main.rs
  calls run() multiple times"). A persistent-connection API is out of scope.
- `main.rs` imports become `use qmk_notifier::{parse_cli_args, run, RunCommand,
  RunParameters, CommandResponse};` (currently only `parse_cli_args` + `run`).
- The sweep is NOT unit-testable (needs real HID hardware). The parse-side logic
  IS unit-tested (F2 tests). Same split as the rest of v0.3.0.

## F5 — Parallel-execution boundary vs P1.M3.T3.S1 (NO conflict)

P1.M3.T3.S1 (in progress) edits `src/lib.rs` `run()` ONLY: run() body, run() doc
comment, + a new private `build_payload` helper placed "just above `pub fn run`".
It does NOT touch `parse_cli_args`, `main.rs`, `error.rs`, or `README.md`.

THIS item edits, in `src/lib.rs`:
- the `use clap::{..}` line (add `ArgGroup`, `ArgMatches`);
- a NEW block placed **immediately before `pub fn parse_cli_args`**: `CliArgs`
  struct + `build_cli_command` + `select_command` + `parse_matches`;
- the `parse_cli_args` BODY (rewrite to call the new helpers);
- the `parse_cli_args` doc comment ([Mode A]).

The two items' lib.rs edits are in DISJOINT regions (parse_cli_args region vs
run/build_payload region, separated by parse_cli_args's closing brace + run's doc
comment). Rule for the implementer: **do NOT modify `run()`, run()'s doc comment,
`build_payload`, or anything at/after the blank line preceding `/// Execute …`** —
that is P1.M3.T3.S1's territory. `main.rs` and `README.md` are exclusively this
item's (P1.M3.T3.S1 does not touch them). `error.rs` and `Cargo.toml` are
untouched by both (no new `QmkError` variant needed — see F6).

## F6 — No `QmkError` variant change needed; clap errors keep clap's UX

The public `parse_cli_args()` keeps `build_cli_command().get_matches()`, so clap
owns `--help`/`--version`/conflict/unknown-flag printing + exit (perfect UX, exit
code 0/2). `parse_matches` returns `QmkError` only for post-parse logic errors:
- `--create-config` ⇒ `QmkError::RemovedFeature(..)` (unchanged).
- no action present ⇒ `QmkError::MissingRequiredParameter("one of message,
  --list, --query-info, or --list-callbacks")` (message extended; variant
  unchanged — PRD §9 pins it for exactly this "neither message nor --list" case).

So `error.rs` is NOT modified (no new variant; no PRD §9 deviation). `Cargo.toml`
is NOT modified (clap 4.5 default features already cover `Arg`/`ArgAction`/
`ArgGroup`/`Command`/builder API; hidapi/serde/toml/dirs unchanged).

## F7 — README §Command Line Options update scope

README lines 65-78: the options table + the note "*Either `message` or `--list`
must be provided." This item adds two table rows (`--query-info`, `--list-
callbacks`) and updates the note to "One of `message`, `--list`, `--query-info`,
or `--list-callbacks` must be provided." Optional: a usage example under §Usage
(line 47). Keep the README change to this table+note (the full v0.3.0 API-surface
README rewrite is P1.M4.T3.S1 — do not preempt it).

## F8 — clap authoritative references (URLs for the PRP)

- ArgGroup (mutual exclusivity): https://docs.rs/clap/latest/clap/struct.ArgGroup.html
  — "By default `ArgGroup::multiple` is set to false … mutually exclusive."
- `Command::try_get_matches_from` (testable, no-exit parse):
  https://docs.rs/clap/latest/clap/struct.Command.html#method.try_get_matches_from
- `ArgAction::SetTrue` (boolean flags):
  https://docs.rs/clap/latest/clap/enum.ArgAction.html#variant.SetTrue
- `ArgMatches::get_flag` (read SetTrue flags):
  https://docs.rs/clap/latest/clap/struct.ArgMatches.html#method.get_flag