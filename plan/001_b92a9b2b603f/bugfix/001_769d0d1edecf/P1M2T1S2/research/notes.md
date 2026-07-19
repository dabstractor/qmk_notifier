# Research Notes — P1.M2.T1.S2 (Update main.rs for new signature + env::args flag)

## S1 CONTRACT (the dependency — assume it lands exactly as specified)

P1.M2.T1.S1 (parallel) modifies **`src/lib.rs` only**:
- `parse_cli_args() -> Result<CliArgs, QmkError>`  ⟶  `Result<RunParameters, QmkError>`.
- `parse_matches(...) -> Result<CliArgs, QmkError>`  ⟶  `Result<RunParameters, QmkError>`.
- `select_command(...) -> Result<(RunCommand, bool), QmkError>`  ⟶  `Result<RunCommand, QmkError>`.
- The `pub struct CliArgs { params: RunParameters, list_callbacks: bool }` is **DELETED**.
- The `--list-callbacks` sweep signal is **no longer returned by the library**; its
  detection becomes `main.rs`'s responsibility out-of-band (via `std::env::args`).
- S1 leaves `main.rs` UNTOUCHED and the lib `#[cfg(test)]` module BROKEN (S3).

**Crucial corollary**: because S1 does not modify main.rs, the main.rs I read is
**identical** before and after S1 — it is the exact starting point for S2.

## CURRENT TREE STATE (verified this session)

- `grep -n "pub struct CliArgs" src/lib.rs`  ⟶  `165:pub struct CliArgs {`  (**PRE-S1**).
- `grep -n "pub fn parse_cli_args" src/lib.rs`  ⟶  `367:pub fn parse_cli_args() -> Result<CliArgs, QmkError> {`.
- `cargo build` (full)  ⟶  **PASSES** (against CliArgs); `cargo test --lib`  ⟶  **72 passed, 0 failed**.
- So the tree is PRE-S1 right now. After S1 lands (parallel): `cargo build` FAILS
  in main.rs (this PRP's scope); `cargo build --lib` passes; `cargo test --lib`
  fails in the lib test module (S3's scope).

## RunParameters FIELDS (verified — src/lib.rs:120-127)

```rust
pub struct RunParameters {
    pub command: RunCommand,       // PartialEq, Eq, Debug, Clone (NOT Copy — only compared)
    pub vendor_id: Option<u16>,    // Copy
    pub product_id: Option<u16>,   // Copy
    pub usage_page: u16,           // Copy
    pub usage: u16,                // Copy
    pub verbose: bool,             // Copy
}
```

`RunCommand` derives `#[derive(Debug, Clone, PartialEq, Eq)]` (lib.rs:18)  ⟹
`params.command == RunCommand::ListDevices` compiles. All five device-targeting
fields are `Copy`, so copying them out before the `run(params)` move is sound
(command is only compared, never copied/moved out — no Copy needed for it).

## main.rs — CURRENT STRUCTURE (the S2 starting point)

- **Line 1** (use statement): `use qmk_notifier::{parse_cli_args, run, CommandResponse,
  RunCommand, RunParameters};`  ⟶ **NO `CliArgs` imported**. RunParameters is still
  used (the callback-sweep loop builds `RunParameters::new(...)`), RunCommand is
  used (`ListDevices`, `QueryCallback`), CommandResponse is used (match arms).
  ⟹ **The use statement is UNCHANGED.** (Item note (f) "Remove CliArgs from the
  use statement if it was imported — verify" ⟶ it is NOT imported; nothing to do.)
- **Lines 12-18**: `let cli = match parse_cli_args() { Ok(cli) => cli, Err(e) => {exit} };`
- **Lines 20-26** (comment + scalar copies): reads `cli.params.command`,
  `cli.params.vendor_id`, …, `cli.params.verbose`, `cli.list_callbacks`.
- **Line 33**: `match run(cli.params) {`
- **Lines 34-60** (the match arms): the `Ok(Info{..}) if list_callbacks =>` sweep
  loop, `Ok(_) if is_list => {}`, `Ok(response) => println!`, `Err` arm.
  ⟶ **UNCHANGED** — `list_callbacks` is now the env::args-derived bool (defined at
  the top); the `if list_callbacks` guard works unchanged.
- **Lines 63+** (`reset_sigpipe_to_default` + unsafe helper): UNCHANGED (Issue 4 /
  P1.M2.T3.S1 owns the unsafe cleanup; out of S2's scope).

## THE EXACT CHANGE SET (one contiguous edit, lines ~12-33)

| Current                                              | After S2                                            |
|------------------------------------------------------|-----------------------------------------------------|
| *(nothing — parse_cli_args called first)*            | `let list_callbacks = std::env::args().any(\|a\| a == "--list-callbacks");` (BEFORE parse_cli_args, + comment) |
| `let cli = match parse_cli_args() { Ok(cli) => cli,` | `let params = match parse_cli_args() { Ok(params) => params,` |
| `// … moves cli.params by value …`                   | `// … moves \`params\` by value …` (comment updated) |
| `let is_list = cli.params.command == …;`             | `let is_list = params.command == …;`                |
| `let vendor_id = cli.params.vendor_id;`              | `let vendor_id = params.vendor_id;`                 |
| `let product_id = cli.params.product_id;`            | `let product_id = params.product_id;`               |
| `let usage_page = cli.params.usage_page;`            | `let usage_page = params.usage_page;`               |
| `let usage = cli.params.usage;`                      | `let usage = params.usage;`                         |
| `let verbose = cli.params.verbose;`                  | `let verbose = params.verbose;`                     |
| `let list_callbacks = cli.list_callbacks;`           | *(REMOVED — replaced by env::args line at top)*     |
| `match run(cli.params) {`                            | `match run(params) {`                               |

The match BODY (the four arms + the callback sweep loop) is byte-for-byte unchanged.

## WHY env::args DETECTION GOES BEFORE parse_cli_args

- `parse_cli_args()` may call `std::process::exit` (clap exits on `--help`/`--version`/
  unknown-flag/action-conflict). If the env::args line were AFTER it, those exits
  would skip the flag capture (harmless in those paths since we exit anyway, but
  the item mandates "Place this BEFORE the parse_cli_args call" so we honor it).
- In the NORMAL `--list-callbacks` flow, `select_command` maps it to
  `RunCommand::QueryInfo` (returns Ok, no exit), so order is observationally
  equivalent — but the before-placement is the contract and is defensive.

## PARALLEL RACE (S1 ⟷ S2) — IMPORTANT

S2's main.rs edits only make `cargo build` (full) pass ONCE S1's lib.rs edits have
landed. If the S2 implementer runs `cargo build` while S1 is NOT yet applied
(CliArgs still returns), the build fails — but the FAILURE MODE tells you whose
scope it is:

- If errors point to **src/main.rs** (e.g. `no field 'command' on type 'CliArgs'`):
  S2 is incomplete or wrong — fix main.rs.
- If errors point to **src/lib.rs** (e.g. `expected RunParameters, found CliArgs`,
  or `cannot find type 'CliArgs'`): S1 has NOT landed yet — those are S1's errors,
  NOT S2's. S2's success criterion is "zero errors in src/main.rs".

The orchestrator merges S1+S2 in milestone P1.M2.T1; the green `cargo build` is
the combined milestone gate, achievable only with both applied.

## VALIDATION GATES (post-S2, assuming S1 applied)

- `cargo fmt --check` ⟶ exit 0.
- `cargo build` (full) ⟶ **PASSES, zero warnings.** THIS IS THE GATE.
  (lib non-test from S1 + main.rs from S2 both compile.)
- `cargo clippy` (default = lib + bin, NOT `--all-targets`) ⟶ zero warnings.
- `cargo test --lib` ⟶ **STILL FAILS** in the lib `#[cfg(test)]` module (references
  CliArgs / cli.params) — that is **P1.M2.T1.S3's** scope, NOT S2's. EXPECTED.

`cargo clippy --all-targets` and `cargo test --lib` both lint/compile the test
module and will fail until S3; use plain `cargo clippy` (lib+bin) for the S2 gate.

## SCOPE BOUNDARIES (what S2 does NOT do)

- Does NOT touch `src/lib.rs` (S1's scope), the lib `#[cfg(test)]` module (S3),
  `error.rs`, `core.rs`, or `Cargo.toml`.
- Does NOT touch the `reset_sigpipe_to_default` / unsafe helper (Issue 4 /
  P1.M2.T3.S1).
- Does NOT change the use statement (no CliArgs imported; all 5 imports stay used).
- Does NOT change the match BODY / callback sweep loop / SIGPIPE helper.
- Does NOT add new tests (main.rs is a binary; its behavior is exercised via
  manual CLI runs; the existing 72 lib tests + the S3-rewritten CLI tests cover
  the library side).