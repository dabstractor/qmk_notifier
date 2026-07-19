name: "P1.M4.T1.S1 — Add --query-info and --list-callbacks flags to parse_cli_args"
description: "Add two diagnostic CLI flags to src/lib.rs's clap builder, plus the main.rs output handling and a README table update. --query-info maps cleanly to RunCommand::QueryInfo. --list-callbacks is a multi-call flow (QueryInfo then a QueryCallback sweep) that needs a CLI-only CliArgs { params, list_callbacks } wrapper so RunParameters stays library-pure. The clap parsing is refactored into pure, hardware-free, unit-testable pieces (build_cli_command + select_command + parse_matches). NO change to run(), error.rs, Cargo.toml, or core.rs (P1.M3.T3.S1 owns run())."

---

## Goal

**Feature Goal**: Give the `qmk_notifier` binary two diagnostic flags (PRD §11):
`--query-info` and `--list-callbacks`. A user can run `qmk_notifier --query-info`
to see a typed-capable device's capability info (`CommandResponse::Info`), and
`qmk_notifier --list-callbacks` to enumerate the firmware's callback registry
(`QueryInfo` → `callback_count`, then sweep `QueryCallback(0..count)`).

**Deliverable**: Edits to THREE files (and a new CLI type):
1. **`src/lib.rs`** — add a CLI-only `pub struct CliArgs { params: RunParameters,
   list_callbacks: bool }`; refactor `parse_cli_args` into pure testable pieces
   (`build_cli_command`, `select_command`, `parse_matches`); change
   `parse_cli_args()` to return `Result<CliArgs, QmkError>`; register the two new
   `ArgAction::SetTrue` flags and a mutually-exclusive `ArgGroup`; rewrite the
   `parse_cli_args` doc comment ([Mode A]); add unit tests for the flag→command
   mapping (hardware-free).
2. **`src/main.rs`** — consume `CliArgs`; when `list_callbacks` is set and
   `run()` returns `CommandResponse::Info`, loop `QueryCallback(0..count)` and
   print each name.
3. **`README.md`** — add the two flags to the §Command Line Options table and
   update the "must be provided" note.

**Success Definition**: `cargo build` → 0 warnings; `cargo clippy` (all targets)
→ 0 warnings; `cargo fmt --check` → exit 0; `cargo test` → all pass INCLUDING the
new flag-parsing tests. `qmk_notifier --query-info` parses to
`RunCommand::QueryInfo`; `qmk_notifier --list-callbacks` parses to
`RunCommand::QueryInfo` + `list_callbacks == true`; the action flags are mutually
exclusive with each other and with the `message` positional.

## User Persona (if applicable)

**Target User**: A human running the `qmk_notifier` binary (developer / keyboard
tinkerer). `qmkonnect` uses the library `run()` directly and is NOT affected
(PRD §11: "not required for qmkonnect, which uses the library directly").

**Use Case**: Plug in a QMK keyboard with the qmk-notifier module; run
`qmk_notifier --query-info` to confirm it is typed-capable (`proto_ver`,
`feature_flags`, `callback_count`, `board_rules_present`); run
`qmk_notifier --list-callbacks` to see the names of the firmware callback
registry slots (so the host knows which callback ids `APPLY_HOST_CONTEXT` can
enable).

**User Journey**: `qmk_notifier --list-callbacks` → (lib) parses to
`CliArgs { params: QueryInfo, list_callbacks: true }` → (main) `run(QueryInfo)`
→ device replies `Info { callback_count: N, .. }` → (main) loops
`run(QueryCallback(i))` for `i in 0..N` → prints `callback i: <name>`.

**Pain Points Addressed**: Today the CLI can only send a legacy string or list
HID devices — there is no way from the command line to query a typed-capable
board's capabilities or enumerate its callback registry.

## Why

- **PRD §11 (CLI)** explicitly lists these as (optional) diagnostic conveniences.
  This item implements them as **flags** (keeping the flat CLI structure) rather
  than subcommands — the simplest approach per the work item's contract note.
- **PRD §3 (Public API) + §10 (Typed-Command Transport)**: the typed-command
  transport (`run() -> CommandResponse`) landed in P1.M3.T3.S1; this item exposes
  `QueryInfo`/`QueryCallback` to the CLI so a human can drive them.
- **Closes the v0.3.0 CLI surface**: P1.M4 is "CLI, Cleanup & Documentation". This
  is the CLI half; the README sync + version bump are sibling items (P1.M4.T3 /
  P1.M4.T2).

## What

### 1. New `pub struct CliArgs` in `src/lib.rs` (CLI-only; keeps `RunParameters` library-pure)
A thin wrapper `{ params: RunParameters, list_callbacks: bool }`. `params` holds
the single command + device-targeting fields for `run()`; `list_callbacks` is the
CLI-only signal that `main.rs` must perform the callback sweep after `QueryInfo`.
`RunParameters` (PRD §3) is NOT modified — a CLI flag would be a leaky abstraction
on the transport type that `qmkonnect` uses directly.

### 2. `parse_cli_args()` signature change: `Result<RunParameters, QmkError>` → `Result<CliArgs, QmkError>`
PRD §3 writes `Result<RunParameters, QmkError>`; this item changes it to
`Result<CliArgs, QmkError>`. **Justification**: PRD §11 says diagnostics are
optional CLI conveniences "not required for qmkonnect, which uses the library
directly" — `qmkonnect` calls `run()`, not `parse_cli_args()`, so this affects
only `main.rs` (this item) and human CLI users. `CliArgs` still contains
`RunParameters`, so library consumers keep full access to the params. No `qmkonnect`
impact. (See research F0.)

### 3. Two new clap flags (`src/lib.rs` `build_cli_command`)
- `--query-info` (`ArgAction::SetTrue`, long-only — no short, to avoid clashing
  with existing `-i/-p/-u/-a/-v/-l/-c`).
- `--list-callbacks` (`ArgAction::SetTrue`, long-only).

### 4. Mutual exclusivity via `ArgGroup`
`ArgGroup::new("action").args(["message","list","query-info","list-callbacks"])
.multiple(false).required(false)` — at most ONE action; zero allowed. The
device-targeting flags (`--vendor-id`, `--product-id`, `--usage-page`, `--usage`,
`--verbose`) stay orthogonal. `--create-config` stays outside the group (handled
first as a removed-feature trap, unchanged).

### 5. `select_command` priority chain
`create-config` (RemovedFeature) → `--list` (ListDevices) → `--query-info`
(QueryInfo, list_callbacks=false) → `--list-callbacks` (QueryInfo,
list_callbacks=true) → `message` (SendMessage) → else
(MissingRequiredParameter). Priority per work item §c.

### 6. `main.rs` callback sweep
When `list_callbacks && run(QueryInfo) == Ok(Info { callback_count, .. })`, loop
`run(QueryCallback(i))` for `i in 0..callback_count` and print each
`CallbackName`. Otherwise print the `CommandResponse` via `{:?}` (unchanged for
`--query-info`/`--list`/`message`, and for a non-capable device that yields
`Timeout`/`Legacy`).

### 7. README §Command Line Options table + note (and the parse_cli_args doc comment)

### Success Criteria
- [ ] `qmk_notifier --query-info` ⇒ `CliArgs { params.command: QueryInfo,
      list_callbacks: false }`; `run()` returns `CommandResponse::Info` on a
      typed-capable board (printed via `{:?}`).
- [ ] `qmk_notifier --list-callbacks` ⇒ `CliArgs { params.command: QueryInfo,
      list_callbacks: true }`; `main.rs` sweeps `QueryCallback(0..count)`.
- [ ] `--query-info`/`--list-callbacks`/`--list`/`message` are mutually exclusive
      (clap rejects combos with a conflict error).
- [ ] Device-targeting flags still combine with `--query-info`
      (e.g. `--query-info --vendor-id 0xFEED -v`).
- [ ] New unit tests pass (flag→command mapping); existing tests unchanged.
- [ ] `cargo build`/`clippy`/`fmt --check`/`test` all clean.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything needed
> to implement this successfully?"_ — **Yes.** The current `parse_cli_args` body
> (verbatim) and `main.rs` (verbatim) are reproduced in research `notes.md`; the
> EXACT target `build_cli_command` / `select_command` / `parse_matches` / `CliArgs`
> / `main.rs` sweep are given verbatim in *Implementation Patterns* below. The
> riskiest constructs — `ArgGroup` mutual exclusivity, `try_get_matches_from`
> testability, the pure `build_cli_command`/`parse_matches` split, and the
> `main.rs` extract-Copy-then-move-into-`run` sweep — are **empirically
> compile/clippy/fmt/test-verified** on a faithful scratch model (research `notes.md`
> F1: 6 tests pass, 0 build/clippy warnings on the real-crate-relevant code). The
> `parse_cli_args` signature change and its PRD §3 justification are captured in
> F0; the no-`error.rs`-change decision in F6; the parallel boundary with
> P1.M3.T3.S1 in F5.

### Documentation & References

```yaml
# MUST READ — the primary file edited
- file: src/lib.rs
  why: "Holds parse_cli_args (the clap builder Command + the if/else command
        selection) right before run(). Currently returns Result<RunParameters,
        QmkError>; uses `use clap::{Arg, ArgAction, Command};` and
        `cmd.get_matches()`. The command selection (list > message > else
        MissingRequiredParameter) and the create-config RemovedFeature trap must
        be PRESERVED inside the new select_command. The verbose/vid/pid/usage
        extraction block (currently inline in parse_cli_args) moves verbatim into
        parse_matches. RunCommand/CommandResponse/RunParameters/HostOs are all
        `pub` here (so `pub struct CliArgs { pub params: RunParameters, .. }`
        needs NO #[allow(private_interfaces)] — unlike the scratch demo)."
  pattern: "parse_cli_args builds a clap Command, calls get_matches(), then maps
            matches → RunParameters. Each Arg uses .short('x').long('x').help(..)
            .value_parser(..) or .action(ArgAction::SetTrue)."
  gotcha: "DO NOT touch run(), run()'s doc comment, or build_payload — P1.M3.T3.S1
           owns them (research F5). Place the new CliArgs + build_cli_command +
           select_command + parse_matches block IMMEDIATELY BEFORE `pub fn
           parse_cli_args`; rewrite only parse_cli_args's body + doc comment.
           parse_cli_args currently still has the OLD placeholder returns in run()
           (Legacy{matched:true}/Timeout) — that is P1.M3.T3.S1's to fix, NOT this
           item's; leave run() exactly as-is."

# MUST READ — research notes (the design decisions + compile proof)
- docfile: plan/001_b92a9b2b603f/P1M4T1S1/research/notes.md
  why: "F0 = WHY CliArgs wrapper (not RunParameters field / not new RunCommand
        variant) + PRD §3 deviation justification; F1 = EMPIRICAL COMPILE PROOF
        (6 tests pass); F2 = the testability refactor (build_cli_command /
        select_command / parse_matches; try_get_matches_from, never get_matches
        in tests); F3 = ArgGroup semantics; F4 = main.rs sweep design; F5 =
        parallel boundary with P1.M3.T3.S1 (do NOT touch run()); F6 = no
        error.rs / Cargo.toml change; F7 = README scope; F8 = clap URLs."
  section: "F0, F1, F2, F5, F6 (critical)"

# MUST READ — the binary entrypoint edited by this item
- file: src/main.rs
  why: "Currently: `let params = parse_cli_args()?; match run(params) { Ok(r) =>
        println!(\"{:?}\", r), Err(e) => exit(1) }`. Must change to consume CliArgs
        and (when list_callbacks) perform the QueryCallback sweep. The sweep needs
        RunCommand/RunParameters/CommandResponse imported from qmk_notifier."
  pattern: "Thin binary: parse → run → print. Error path: eprintln + exit(1)."

# MUST READ — the docs file edited by this item
- file: README.md
  why: "§Command Line Options table (README.md:65-76) + the note (line 78:
        '*Either message or --list must be provided.'). Add two rows + update the
        note. Do NOT rewrite the whole README (P1.M4.T3.S1 owns the v0.3.0
        API-surface sync — research F7)."
  section: "Command Line Options"

# REFERENCE — the contract this item consumes (the INPUT dependency)
- docfile: plan/001_b92a9b2b603f/P1M3T3S1/PRP.md
  why: "Defines run() full dispatch (build → send_raw_report → parse_reply →
        CommandResponse). This item's main.rs calls run() (unchanged signature)
        possibly multiple times for the sweep. If run() still returns placeholders
        at merge time (P1.M3.T3.S1 not yet landed), this item STILL COMPILES AND
        PASSES — main.rs matches on CommandResponse::Info{..} which the enum
        already defines (P1.M1.T1.S3). The sweep just won't fire until a real
        Info reply arrives (needs hardware). No hard dependency."

# REFERENCE — PRD sections this change implements
- file: PRD.md
  why: "§11 (CLI) pins the option table + the 'diagnostics are optional
        conveniences, not required for qmkonnect' note (justifies the parse_cli_args
        signature change); §3 (Public API) pins RunCommand::QueryInfo/QueryCallback
        + CommandResponse::Info{callback_count}/CallbackName{index,name}; §10
        (Typed-Command Transport) is the protocol the flags drive."
  section: "11. CLI", "3. Public API", "10. Typed-Command Transport"

# REFERENCE — clap 4.x API (authoritatively + empirically verified)
- url: https://docs.rs/clap/latest/clap/struct.ArgGroup.html
  why: "ArgGroup::new(..).args([..]).multiple(false).required(false) ⇒ at most one
        arg present (mutually exclusive), zero allowed. Default multiple=false."
- url: https://docs.rs/clap/latest/clap/struct.Command.html#method.try_get_matches_from
  why: "try_get_matches_from returns Result (NO process exit) — use it in TESTS to
        build ArgMatches. NEVER call get_matches/get_matches_from in tests (they
        exit the process on error and kill the cargo test runner)."
- url: https://docs.rs/clap/latest/clap/enum.ArgAction.html#variant.SetTrue
  why: "ArgAction::SetTrue makes a flag default to false; read via
        ArgMatches::get_flag(..) (returns bool; panics only if the arg lacks a
        flag action — SetTrue satisfies that)."
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.2.1", edition="2021"
│                       # deps: clap 4.5.31 (default features), hidapi, toml, dirs, serde.
│                       # NOT modified by this item.
├── Cargo.lock
├── README.md           # §Command Line Options table (lines 65-78). EDITED (table + note).
├── PRD.md              # READ-ONLY.
├── .gitignore
└── src
    ├── main.rs         # binary: parse_cli_args → run → print. EDITED (CliArgs + sweep).
    ├── core.rs         # send_raw_report/build_typed_payload/parse_reply. DO NOT TOUCH.
    ├── error.rs        # QmkError (PRD §9). DO NOT TOUCH (no new variant — F6).
    └── lib.rs          # parse_cli_args + RunCommand/CommandResponse/RunParameters/run.
                        # EDITED: + CliArgs, + build_cli_command/select_command/parse_matches,
                        #         parse_cli_args body+doc rewrite, + tests. run() UNCHANGED.
```

### Desired Codebase tree with files to be modified

```bash
src/
├── lib.rs   # MODIFIED:
│            #   (1) `use clap::{Arg, ArgAction, ArgGroup, ArgMatches, Command};`
│            #       (add ArgGroup + ArgMatches).
│            #   (2) ADD `pub struct CliArgs { pub params: RunParameters,
│            #       pub list_callbacks: bool }` (+ doc comment) — place just
│            #       BEFORE `pub fn parse_cli_args`.
│            #   (3) ADD private `fn build_cli_command() -> Command`,
│            #       `fn select_command(&ArgMatches) -> Result<(RunCommand,bool),QmkError>`,
│            #       `fn parse_matches(&ArgMatches) -> Result<CliArgs,QmkError>`.
│            #   (4) REWRITE `parse_cli_args` body to:
│            #       `parse_matches(&build_cli_command().get_matches())`; change
│            #       signature to `-> Result<CliArgs, QmkError>`; rewrite doc comment.
│            #   (5) ADD unit tests (flag→command mapping; mutual exclusivity).
│            #   run(), run()'s doc comment, build_payload: UNCHANGED (P1.M3.T3.S1).
├── main.rs  # MODIFIED: consume CliArgs; callback sweep when list_callbacks && Info.
└── ../README.md  # MODIFIED: §Command Line Options table (+2 rows) + note.
# (core.rs, error.rs, Cargo.toml, Cargo.lock UNCHANGED; NO new files)
```

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (PARALLEL ITEM — do NOT touch run()): P1.M3.T3.S1 is editing src/lib.rs
//   run() (body + doc comment + a new build_payload helper just above run()) RIGHT
//   NOW. This item's lib.rs edits are confined to the parse_cli_args region
//   (CliArgs + the three new helpers placed IMMEDIATELY BEFORE `pub fn
//   parse_cli_args`, the parse_cli_args body, its doc comment, and tests). Do NOT
//   modify run(), run()'s doc comment, build_payload, or the blank line that
//   precedes run()'s `/// Execute` doc comment. The two items' regions are
//   disjoint (research F5). NOTE: at merge time run() may STILL contain the old
//   placeholder returns (Legacy{matched:true}/Timeout) if P1.M3.T3.S1 hasn't
//   landed — that is FINE: this item compiles and tests pass regardless; the
//   sweep simply won't fire without a real Info reply (hardware).

// CRITICAL (parse_cli_args RETURN TYPE CHANGES — PRD §3 deviation): the signature
//   becomes `pub fn parse_cli_args() -> Result<CliArgs, QmkError>` (was
//   `Result<RunParameters, QmkError>`). This is justified (research F0): PRD §11
//   says diagnostics are CLI conveniences "not required for qmkonnect, which uses
//   the library directly" (qmkonnect calls run(), not parse_cli_args()). main.rs
//   (this item) is the only in-tree caller and is updated here. No existing TEST
//   calls parse_cli_args (all construct RunParameters directly) — verified by
//   grep — so nothing else breaks.

// CRITICAL (NEVER call get_matches / get_matches_from in TESTS): clap's
//   get_matches* call std::process::exit on ANY parse error (including --help) and
//   would kill the `cargo test` runner. Tests MUST build ArgMatches via
//   `build_cli_command().try_get_matches_from(args)` (returns Result, no exit) and
//   then feed them to the pure `parse_matches`/`select_command`. (research F2.)

// CRITICAL (RunParameters must NOT gain a list_callbacks field): a CLI flag on the
//   library's transport parameter type (used directly by qmkonnect) is a leaky
//   abstraction. Use the CliArgs wrapper instead. Likewise do NOT add a CLI-only
//   variant to RunCommand (it is the wire-protocol enum, PRD §3/§10). (research F0.)

// GOTCHA (no error.rs change — PRD §9 pins QmkError): clap owns --help/--version/
//   conflict/unknown-flag printing+exit via the public parse_cli_args's
//   get_matches() (perfect UX). parse_matches returns QmkError only for post-parse
//   logic: --create-config ⇒ RemovedFeature (unchanged); no action ⇒
//   MissingRequiredParameter (message EXTENDED to "one of message, --list,
//   --query-info, or --list-callbacks"; variant unchanged). Do NOT add a
//   "CliParseError" variant — none is needed. (research F6.)

// GOTCHA (no Cargo.toml change): clap 4.5.31 default features already provide
//   Arg/ArgAction/ArgGroup/Command + the builder API. hidapi/serde/toml/dirs are
//   untouched. (research F6.)

// GOTCHA (ArgGroup with a positional + SetTrue flags works): verified empirically
//   (research F1). `ArgGroup::new("action").args(["message","list","query-info",
//   "list-callbacks"]).multiple(false).required(false)` ⇒ at most one present.
//   arg_required_else_help(true) + required(false) coexist (no-args ⇒ help;
//   one-action ⇒ ok; --verbose-alone ⇒ reaches select_command's else ⇒
//   MissingRequiredParameter). (research F3.)

// GOTCHA (main.rs partial move): extract the Copy scalars (vendor_id, product_id,
//   usage_page, usage, verbose) AND list_callbacks from cli BEFORE `run(cli.params)`
//   (which moves cli.params by value). Reading cli.list_callbacks after the move is
//   legal (separate field), but copying it out first is clearer. Verified in
//   scratch F1/F4.

// GOTCHA (the sweep re-opens the device per callback): each run() is independent
//   (open → send → read → close). This is what the work item specifies ("main.rs
//   calls run() multiple times"). Slightly inefficient but correct and in-scope;
//   a persistent-connection API is out of scope.

// GOTCHA (CliArgs is `pub`, RunParameters is `pub` ⇒ no private_interfaces
//   warning): the scratch demo warned because its RunParameters was non-pub. In
//   the real crate both are pub, so `pub struct CliArgs { pub params:
//   RunParameters, pub list_callbacks: bool }` is clean. (research F1.)
```

## Implementation Blueprint

### Data models and structure

One new public type, no changes to existing types:

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

`RunCommand`, `CommandResponse`, `RunParameters`, `HostOs`, `QmkError` are all
**unchanged**. No new constants. No new deps. No `error.rs` change.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY the clap import line in src/lib.rs
  - CHANGE: `use clap::{Arg, ArgAction, Command};`
         ⇒ `use clap::{Arg, ArgAction, ArgGroup, ArgMatches, Command};`
  - WHY: ArgGroup (mutual exclusivity) + ArgMatches (the pure helpers' param type).

Task 2: ADD `pub struct CliArgs` to src/lib.rs (place IMMEDIATELY BEFORE `pub fn parse_cli_args`)
  - IMPLEMENT exactly the struct + doc comment in "Data models" above.
  - VISIBILITY: `pub` (returned by pub parse_cli_args; used by main.rs via
    qmk_notifier::). RunParameters is already `pub` ⇒ no private_interfaces warning.
  - DERIVE: `Debug, Clone` (matches RunParameters).

Task 3: ADD private `fn build_cli_command() -> Command` (just before parse_cli_args)
  - IMPLEMENT: move the ENTIRE existing `Command::new("QMK Keyboard Communication Tool")
    ….arg_required_else_help(true)` builder OUT of parse_cli_args into this fn, then:
    (a) add `--query-info` (ArgAction::SetTrue, long-only, descriptive .help);
    (b) add `--list-callbacks` (ArgAction::SetTrue, long-only, descriptive .help);
    (c) append `.group(ArgGroup::new("action").args(["message","list","query-info",
        "list-callbacks"]).multiple(false).required(false))` AFTER all .arg(..) calls
        and BEFORE .arg_required_else_help(true).
  - PRESERVE: every existing arg (message, vendor-id, product-id, usage-page, usage,
    verbose, list, create-config), their short/long/help/value_parser, the version/
    author/about strings, and `.arg_required_else_help(true)`. Byte-for-byte except
    the two additions + the group.
  - NAMING: `fn build_cli_command() -> Command` (private).

Task 4: ADD private `fn select_command(matches: &ArgMatches) -> Result<(RunCommand, bool), QmkError>`
  - IMPLEMENT the priority chain (verbatim in Implementation Patterns §B):
    create-config ⇒ RemovedFeature; list ⇒ (ListDevices,false); query-info ⇒
    (QueryInfo,false); list-callbacks ⇒ (QueryInfo,true); message ⇒ (SendMessage(s),false);
    else ⇒ MissingRequiredParameter("one of message, --list, --query-info, or --list-callbacks").
  - PRESERVE the existing create-config error MESSAGE verbatim ("Config file
    creation has been removed. All parameters must be provided explicitly.").
  - NAMING: `fn select_command(matches: &ArgMatches) -> Result<(RunCommand, bool), QmkError>`.

Task 5: ADD private `fn parse_matches(matches: &ArgMatches) -> Result<CliArgs, QmkError>`
  - IMPLEMENT (verbatim in §C): call select_command, then extract vendor_id /
    product_id / usage_page (unwrap_or DEFAULT_USAGE_PAGE) / usage (unwrap_or
    DEFAULT_USAGE) / verbose using the SAME .get_one::<String>(..).map(parse_hex_or_decimal)
    .transpose()? pattern as today, then build CliArgs { params: RunParameters::new(..),
    list_callbacks }.
  - PRESERVE: the VID/PID `None`-default + usage defaults + the Option<u16> semantics.

Task 6: REWRITE `pub fn parse_cli_args` (body + signature + doc comment)
  - SIGNATURE: `pub fn parse_cli_args() -> Result<CliArgs, QmkError>`.
  - BODY: `Ok(parse_matches(&build_cli_command().get_matches())?)` — or
    `let matches = build_cli_command().get_matches(); parse_matches(&matches)`.
    (get_matches preserves --help/--version/conflict UX via clap's exit.)
  - DELETE: the now-moved inline Command builder + the inline extraction + the
    inline if/else command selection (all relocated to Tasks 3-5).
  - DOC COMMENT ([Mode A]): document that it parses CLI args into CliArgs; that
    --query-info ⇒ QueryInfo; --list-callbacks ⇒ QueryInfo + list_callbacks=true
    (sweep handled by main.rs); that action flags are mutually exclusive via
    ArgGroup; reference PRD §11. (See §D.)

Task 7: MODIFY src/main.rs (consume CliArgs + callback sweep)
  - CHANGE the parse step: `let cli = match qmk_notifier::parse_cli_args() { … }`.
  - ADD imports: `use qmk_notifier::{parse_cli_args, run, CommandResponse,
    RunCommand, RunParameters};`.
  - IMPLEMENT the sweep flow verbatim in §E (extract Copy scalars + list_callbacks
    before run(cli.params); match on Info{callback_count} guarded by list_callbacks;
    loop QueryCallback; else println!("{:?}", response); err ⇒ exit(1)).
  - PRESERVE the error-path UX (eprintln!("Error: {}", e); exit(1)).

Task 8: MODIFY README.md §Command Line Options
  - ADD two table rows (after the `--list` row, before `--help`):
      | `--query-info` | | off | Query device capability info (QUERY_INFO, cmd 0x01). |
      | `--list-callbacks` | | off | List firmware callback registry (QueryInfo + QueryCallback sweep). |
  - UPDATE the note (line 78) from "*Either `message` or `--list` must be
    provided." to "*One of `message`, `--list`, `--query-info`, or
    `--list-callbacks` must be provided."
  - OPTIONAL: add a `# Query device info` / `# List firmware callbacks` example
    under §Usage (lines 47-63). Keep it minimal.

Task 9: ADD unit tests to src/lib.rs `mod tests`
  - IMPLEMENT the tests in §F (query-info, list-callbacks, query-info+device-flags,
    message/list still work, mutual exclusivity x3, no-action error).
  - PATTERN: `let m = build_cli_command().try_get_matches_from(["qmk_notifier",
    "--query-info"]).unwrap(); let cli = parse_matches(&m).unwrap(); assert!(..)`.
  - NAMING: test_parse_query_info_flag, test_parse_list_callbacks_flag, etc.
  - NEVER use get_matches/get_matches_from in tests (they exit the process).
  - COVERAGE: flag→command mapping (positive), mutual exclusivity (negative),
    device-flag orthogonality, no-action error. The main.rs sweep is NOT tested
    (needs HID hardware) — same constraint as the rest of v0.3.0.

Task 10: VALIDATE (do not skip)
  - RUN: `cargo fmt`, then `cargo build`, `cargo clippy --all-targets`,
    `cargo fmt --check`, `cargo test`.
  - EXPECT: build 0 warnings; clippy 0 warnings; fmt --check exit 0; ALL tests
    pass (existing + new).
```

### Implementation Patterns & Key Details

#### §A — `build_cli_command()` (the relocated + extended clap builder)

```rust
/// Build the clap `Command` for `qmk_notifier` (PRD §11 *CLI*). Pure: it only
/// configures the parser; the actual `get_matches()`/`try_get_matches_from()`
/// call is made by the caller (so tests can use the no-exit `try_*` form).
fn build_cli_command() -> Command {
    Command::new("QMK Keyboard Communication Tool")
        .version("1.0.0")
        .author("Your Name")
        .about("Sends raw HID reports to QMK keyboards; --query-info / --list-callbacks diagnose typed-capable boards")
        .arg(
            Arg::new("message")
                .help("Message to send to keyboard")
                .index(1),
        )
        .arg(
            Arg::new("vendor-id")
                .short('i')
                .long("vendor-id")
                .value_name("VID")
                .help("USB vendor ID (decimal or 0xHEX format) [default: auto (match any)]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("product-id")
                .short('p')
                .long("product-id")
                .value_name("PID")
                .help("USB product ID (decimal or 0xHEX format) [default: auto (match any)]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("usage-page")
                .short('u')
                .long("usage-page")
                .value_name("USAGE_PAGE")
                .help("HID usage page (decimal or 0xHEX format) [default: 0xFF60]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("usage")
                .short('a')
                .long("usage")
                .value_name("USAGE")
                .help("HID usage (decimal or 0xHEX format) [default: 0x61]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("verbose")
                .short('v')
                .long("verbose")
                .help("Enable verbose output")
                .action(ArgAction::SetTrue),
        )
        .arg(
            Arg::new("list")
                .short('l')
                .long("list")
                .help("List all HID devices")
                .action(ArgAction::SetTrue),
        )
        .arg(
            Arg::new("create-config")
                .short('c')
                .long("create-config")
                .help("Create example configuration file (REMOVED)")
                .action(ArgAction::SetTrue),
        )
        .arg(
            Arg::new("query-info")
                .long("query-info")
                .help("Query device capability info (QUERY_INFO, cmd 0x01)")
                .action(ArgAction::SetTrue),
        )
        .arg(
            Arg::new("list-callbacks")
                .long("list-callbacks")
                .help("List the firmware callback registry (runs QUERY_INFO, then sweeps QUERY_CALLBACK)")
                .action(ArgAction::SetTrue),
        )
        // The four "action" selectors are mutually exclusive: at most one may be
        // present (clap rejects combinations). `required(false)` lets zero through
        // so `--verbose`-alone reaches select_command's MissingRequiredParameter
        // arm (and no-args at all still triggers arg_required_else_help above).
        .group(
            ArgGroup::new("action")
                .args(["message", "list", "query-info", "list-callbacks"])
                .multiple(false)
                .required(false),
        )
        .arg_required_else_help(true)
}
```

> `arg_required_else_help(true)` MUST come after `.group(..)` (builder order). All
> existing args/help strings are preserved byte-for-byte; only the two new flags +
> the group + a slightly richer `.about(..)` are added.

#### §B — `select_command()` (the priority chain)

```rust
/// Resolve the parsed CLI matches into a `(RunCommand, list_callbacks)` pair.
///
/// Priority (PRD §11; the `action` ArgGroup makes these mutually exclusive, so
/// the order is defensive): `--create-config` (removed-feature trap) > `--list` >
/// `--query-info` > `--list-callbacks` > `message` positional. `--list-callbacks`
/// maps to `RunCommand::QueryInfo` with `list_callbacks = true` — the actual
/// callback sweep is a multi-call flow handled by `main.rs`, not a single command.
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

#### §C — `parse_matches()` (the relocated extraction + CliArgs assembly)

```rust
/// Build [`CliArgs`] from already-parsed clap matches (PRD §11). Pure: takes
/// `&ArgMatches`, returns `Result<CliArgs, QmkError>` — never exits the process.
/// This is the testable core of [`parse_cli_args`].
fn parse_matches(matches: &ArgMatches) -> Result<CliArgs, QmkError> {
    let (command, list_callbacks) = select_command(matches)?;

    // VID/PID default to None (auto: match any by usage page/usage). Usage page/
    // usage default to the QMK raw-HID convention. (Unchanged from the old
    // inline parse_cli_args extraction.)
    let vendor_id = matches
        .get_one::<String>("vendor-id")
        .map(parse_hex_or_decimal)
        .transpose()?;
    let product_id = matches
        .get_one::<String>("product-id")
        .map(parse_hex_or_decimal)
        .transpose()?;
    let usage_page = matches
        .get_one::<String>("usage-page")
        .map(parse_hex_or_decimal)
        .transpose()?
        .unwrap_or(DEFAULT_USAGE_PAGE);
    let usage = matches
        .get_one::<String>("usage")
        .map(parse_hex_or_decimal)
        .transpose()?
        .unwrap_or(DEFAULT_USAGE);
    let verbose = matches.get_flag("verbose");

    Ok(CliArgs {
        params: RunParameters::new(command, vendor_id, product_id, usage_page, usage, verbose),
        list_callbacks,
    })
}
```

> NOTE: `.map(parse_hex_or_decimal)` — in the current code this is written
> `.map(|s| parse_hex_or_decimal(s))`; both compile. Use whichever rustfmt prefers
> (the closure form is what's there today; keep it to minimize diff if you like).

#### §D — `parse_cli_args()` (the thin public wrapper + [Mode A] doc comment)

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
/// The four action selectors (`message`, `--list`, `--query-info`,
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

#### §E — `src/main.rs` (consume CliArgs + callback sweep)

```rust
use qmk_notifier::{parse_cli_args, run, CommandResponse, RunCommand, RunParameters};

fn main() {
    let cli = match parse_cli_args() {
        Ok(cli) => cli,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Copy out the device-targeting scalars (all Copy) BEFORE run() moves
    // cli.params by value — they're needed to rebuild per-callback params.
    let vendor_id = cli.params.vendor_id;
    let product_id = cli.params.product_id;
    let usage_page = cli.params.usage_page;
    let usage = cli.params.usage;
    let verbose = cli.params.verbose;
    let list_callbacks = cli.list_callbacks;

    match run(cli.params) {
        // --list-callbacks: after QueryInfo succeeds, sweep the callback registry.
        Ok(CommandResponse::Info { callback_count, .. }) if list_callbacks => {
            println!("callbacks ({callback_count}):");
            for index in 0..callback_count {
                let params = RunParameters::new(
                    RunCommand::QueryCallback(index),
                    vendor_id,
                    product_id,
                    usage_page,
                    usage,
                    verbose,
                );
                match run(params) {
                    Ok(CommandResponse::CallbackName { index, name }) => {
                        println!(
                            "  {}: {}",
                            index,
                            name.unwrap_or_else(|| "(unnamed)".to_string())
                        );
                    }
                    Ok(other) => {
                        eprintln!("callback {index}: unexpected response {other:?}")
                    }
                    Err(e) => eprintln!("callback {index}: error: {e}"),
                }
            }
        }
        // --query-info / --list / message, OR a non-capable device (Timeout/Legacy)
        // when --list-callbacks was asked for: just print the raw response.
        Ok(response) => println!("{:?}", response),
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    }
}
```

#### §F — unit tests (add to `mod tests` in src/lib.rs)

```rust
// ---- P1.M4.T1.S1: --query-info / --list-callbacks flag parsing ----
// Build ArgMatches via the no-exit try_get_matches_from (NEVER get_matches* in a
// test — it exits the process on error), then exercise the pure parse_matches.
fn cli_for(args: &[&str]) -> CliArgs {
    let matches = build_cli_command()
        .try_get_matches_from(args)
        .expect("test args should parse");
    parse_matches(&matches).expect("test args should resolve to CliArgs")
}

#[test]
fn test_parse_query_info_flag() {
    let cli = cli_for(&["qmk_notifier", "--query-info"]);
    assert!(matches!(cli.params.command, RunCommand::QueryInfo));
    assert!(!cli.list_callbacks);
    // Defaults preserved.
    assert_eq!(cli.params.usage_page, DEFAULT_USAGE_PAGE);
    assert_eq!(cli.params.usage, DEFAULT_USAGE);
    assert_eq!(cli.params.vendor_id, None);
    assert_eq!(cli.params.product_id, None);
}

#[test]
fn test_parse_list_callbacks_flag() {
    let cli = cli_for(&["qmk_notifier", "--list-callbacks"]);
    // list-callbacks maps to QueryInfo + the sweep signal.
    assert!(matches!(cli.params.command, RunCommand::QueryInfo));
    assert!(cli.list_callbacks);
}

#[test]
fn test_query_info_combines_with_device_flags() {
    // Device-targeting flags are orthogonal to the action group.
    let cli = cli_for(&["qmk_notifier", "--query-info", "--vendor-id", "0xFEED", "-v"]);
    assert!(matches!(cli.params.command, RunCommand::QueryInfo));
    assert_eq!(cli.params.vendor_id, Some(0xFEED));
    assert!(cli.params.verbose);
    assert!(!cli.list_callbacks);
}

#[test]
fn test_message_and_list_still_parse() {
    let cli = cli_for(&["qmk_notifier", "hello"]);
    assert!(matches!(cli.params.command, RunCommand::SendMessage(s) if s == "hello"));
    assert!(!cli.list_callbacks);

    let cli = cli_for(&["qmk_notifier", "--list"]);
    assert!(matches!(cli.params.command, RunCommand::ListDevices));
    assert!(!cli.list_callbacks);
}

#[test]
fn test_action_selectors_are_mutually_exclusive() {
    // Each combination must be a clap conflict error (Err, NOT a process exit —
    // try_get_matches_from returns Result).
    let cases: &[&[&str]] = &[
        &["qmk_notifier", "--query-info", "msg"],
        &["qmk_notifier", "--query-info", "--list-callbacks"],
        &["qmk_notifier", "--list-callbacks", "msg"],
        &["qmk_notifier", "--list", "--query-info"],
    ];
    for args in cases {
        assert!(
            build_cli_command().try_get_matches_from(args.iter().copied()).is_err(),
            "expected clap to reject conflicting actions: {args:?}",
        );
    }
}

#[test]
fn test_no_action_given_is_missing_parameter() {
    // --verbose alone: not a clap error (group is required(false)), but
    // select_command finds no action ⇒ MissingRequiredParameter.
    let matches = build_cli_command()
        .try_get_matches_from(["qmk_notifier", "--verbose"])
        .expect("--verbose alone parses at the clap level");
    let result = parse_matches(&matches);
    assert!(
        matches!(result, Err(QmkError::MissingRequiredParameter(_))),
        "expected MissingRequiredParameter, got {result:?}",
    );
}
```

> `cli_for` uses `.expect(..)` only on inputs known to parse (happy paths). The
> exclusivity test calls `try_get_matches_from` directly and asserts `is_err()`
> (no expect, no exit). `["qmk_notifier", ..]` supplies the program name clap
> expects as `args[0]`.

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs (clap import; + CliArgs; + build_cli_command/select_command/
             parse_matches; parse_cli_args body+signature+doc; + tests). run() UNCHANGED."
  - modify: "src/main.rs (consume CliArgs; callback sweep)."
  - modify: "README.md (§Command Line Options table + note)."

PUBLIC API SURFACE:
  - adds:    "pub struct CliArgs { pub params: RunParameters, pub list_callbacks: bool }."
  - changes: "parse_cli_args() signature: Result<RunParameters, QmkError>
              ⇒ Result<CliArgs, QmkError>. (PRD §3 deviation — justified, research F0:
              qmkonnect uses run() directly, not parse_cli_args; only main.rs calls it.)"
  - unchanged: "RunCommand, CommandResponse, RunParameters, HostOs, RunParameters::new,
                run() (signature + body — P1.M3.T3.S1), all QmkError variants, the
                `pub use core::{..}` re-export, send_raw_report/build_typed_payload/
                parse_reply, the DEFAULT_* consts, REPORT_LENGTH."

DEPENDENCIES (soft — this item compiles even if not yet landed):
  - P1.M1.T1.S3 (DONE): "CommandResponse::Info { callback_count, .. } +
                         CallbackName { index, name } exist — main.rs matches them."
  - P1.M3.T3.S1 (in progress): "run() full dispatch. NOT a hard dependency: this item
                                compiles + tests pass with run() in ANY state; the
                                sweep simply won't fire until a real Info reply
                                arrives (needs HID hardware)."

CONFIG / DATABASE / ROUTES:
  - none. (No Cargo.toml change — clap default features suffice; research F6.)

SCOPE BOUNDARY (do NOT implement now):
  - Do NOT touch run(), run()'s doc comment, or build_payload (P1.M3.T3.S1 — F5).
  - Do NOT modify error.rs (no new QmkError variant — F6) or Cargo.toml.
  - Do NOT add `list_callbacks` to RunParameters or a CLI variant to RunCommand (F0).
  - Do NOT rewrite the whole README (P1.M4.T3.S1 owns the v0.3.0 API-surface sync;
    this item touches only the §Command Line Options table + note — F7).
  - Do NOT bump the version (P1.M4.T2.S1).
  - Do NOT use get_matches/get_matches_from in tests (they exit the process — F2).
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Format the edited files (default rustfmt — no rustfmt.toml exists).
cargo fmt

# Build the whole crate (lib + bin) — MUST compile with ZERO warnings.
# Verifies: CliArgs visibility (RunParameters is pub ⇒ no private_interfaces),
# the ArgGroup builder, select_command/parse_matches type-check against
# ArgMatches, and main.rs's CommandResponse match + partial-move sweep.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Finished `dev` profile ..." and NO "warning:" lines.

# Lint all targets (lib + bin + tests). The ArgGroup, try_get_matches_from in
# tests, the extract-Copy-then-move pattern, and the match guard are all
# clippy-clean under defaults (empirically verified F1).
cargo clippy --all-targets 2>&1 | tee /tmp/clippy.log
# Expected: no warnings/errors.

# CI-style formatting gate.
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full test suite (lib unit tests incl. the new flag tests + core.rs unit tests).
cargo test 2>&1 | tail -15
# Expected: "test result: ok. <N> passed; 0 failed; 0 ignored; ..." — the baseline
# grows by the new tests (test_parse_query_info_flag, test_parse_list_callbacks_flag,
# test_query_info_combines_with_device_flags, test_message_and_list_still_parse,
# test_action_selectors_are_mutually_exclusive, test_no_action_given_is_missing_parameter).

# Targeted: the new flag-parsing tests.
cargo test --lib test_parse_query_info_flag test_parse_list_callbacks_flag -- --nocapture
cargo test --lib test_action_selectors_are_mutually_exclusive -- --nocapture
cargo test --lib test_no_action_given_is_missing_parameter -- --nocapture
# Expected: all pass.

# Sanity: the pre-existing dispatch + parse_reply suites still green (untouched).
cargo test --lib test_run_query_info_dispatches_to_send -- --nocapture
cargo test --lib parse_reply -- --nocapture
# Expected: pass.
```

### Level 3: Integration Testing (System Validation)

```text
PARTIALLY APPLICABLE. The end-to-end flag → run() → CommandResponse → printed
output path needs a real QMK keyboard on the HID bus. Without hardware:

(1) The crate COMPILES end-to-end (Level 1) — proving CliArgs, the ArgGroup,
    select_command→parse_matches→parse_cli_args, and main.rs's sweep type-check.
(2) The flag→command mapping is FULLY unit-tested (Level 2) — --query-info ⇒
    QueryInfo, --list-callbacks ⇒ QueryInfo+list_callbacks, mutual exclusivity,
    device-flag orthogonality, no-action error.
(3) `cargo run -- --help` prints the help INCLUDING the two new flags (manual).
(4) `cargo run -- --query-info --list-callbacks` exits with a clap CONFLICT error
    (manual — proves the ArgGroup fires at runtime).

A live-hardware smoke test (`qmk_notifier --query-info` ⇒ Info fields printed;
`qmk_notifier --list-callbacks` ⇒ enumerated callback names) is a manual QA step,
not a CI gate — same constraint as the rest of v0.3.0.
```

Manual runtime checks (no hardware needed for the parse/error paths):
```bash
# Help lists the new flags.
cargo run -- --help | grep -E "query-info|list-callbacks"
# Expected: both flag help lines present.

# Mutual exclusivity at runtime (clap conflict, exit 2).
cargo run -- --query-info --list-callbacks ; echo "exit=$?"
# Expected: a clap "error: the argument '--query-info' cannot be used with
# '--list-callbacks'" message and exit=2.

# No-action error flows through QmkError (exit 1) — NOTE: --verbose-alone reaches
# select_command's MissingRequiredParameter only if clap doesn't short-circuit;
# verify the actual behavior and adjust the message path if clap errors first.
cargo run -- --verbose ; echo "exit=$?"
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm CliArgs is the parse_cli_args return type.
grep -nE "pub fn parse_cli_args\(\) -> Result<CliArgs" src/lib.rs
# Expected: exactly ONE match.

# Confirm the two new flags are registered.
grep -nE "Arg::new\(\"(query-info|list-callbacks)\"\)" src/lib.rs
# Expected: TWO matches.

# Confirm the mutually-exclusive ArgGroup.
grep -nE "ArgGroup::new\(\"action\"\)" src/lib.rs
# Expected: exactly ONE match.

# Confirm select_command maps list-callbacks to (QueryInfo, true).
grep -nE "list-callbacks" src/lib.rs
grep -nA2 'get_flag\("list-callbacks"\)' src/lib.rs
# Expected: the (RunCommand::QueryInfo, true) arm.

# Confirm main.rs imports + sweep.
grep -nE "use qmk_notifier::\{parse_cli_args, run, CommandResponse, RunCommand, RunParameters\}" src/main.rs
grep -nE "CommandResponse::Info \{ callback_count, \.\. \} if list_callbacks" src/main.rs
grep -nE "RunCommand::QueryCallback\(index\)" src/main.rs
# Expected: one match each.

# Confirm run() was NOT touched (P1.M3.T3.S1 territory).
# (run() may still have placeholder returns if P1.M3.T3.S1 hasn't landed — that's fine.)
grep -nE "pub fn run\(params: RunParameters\)" src/lib.rs
# Expected: exactly ONE match; its signature unchanged.

# Confirm README table + note updated.
grep -nE "\`--query-info\`|\`--list-callbacks\`" README.md
grep -nE "One of \`message\`, \`--list\`, \`--query-info\`, or \`--list-callbacks\`" README.md
# Expected: matches present.

# Confirm error.rs + Cargo.toml untouched.
git status --porcelain
# Expected: only "M src/lib.rs", "M src/main.rs", "M README.md".

# Full-crate final gate.
cargo test 2>&1 | tail -3
# Expected: "test result: ok. <N> passed; 0 failed; ...".
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1: `cargo build` → 0 warnings.
- [ ] Level 1: `cargo clippy --all-targets` → 0 warnings.
- [ ] Level 1: `cargo fmt --check` → exit 0.
- [ ] Level 2: `cargo test` → all pass (baseline + 6 new tests).

### Feature Validation
- [ ] `pub struct CliArgs { params: RunParameters, list_callbacks: bool }` added.
- [ ] `parse_cli_args() -> Result<CliArgs, QmkError>` (signature changed; doc [Mode A]).
- [ ] `--query-info` ⇒ `(RunCommand::QueryInfo, false)`; `--list-callbacks` ⇒
      `(RunCommand::QueryInfo, true)`.
- [ ] `ArgGroup("action")` makes message/--list/--query-info/--list-callbacks
      mutually exclusive; device-targeting flags stay orthogonal.
- [ ] `main.rs` sweeps `QueryCallback(0..callback_count)` when `list_callbacks`
      and `run(QueryInfo) == Ok(Info{..})`; else prints `{:?}`.
- [ ] README §Command Line Options table has the two new rows; note updated.

### Code Quality Validation
- [ ] Only `src/lib.rs`, `src/main.rs`, `README.md` modified (git status).
- [ ] `run()`, run()'s doc comment, `build_payload`, `error.rs`, `Cargo.toml`,
      `core.rs` UNCHANGED (P1.M3.T3.S1 boundary — F5).
- [ ] Tests use `try_get_matches_from` (NEVER `get_matches*` — F2).
- [ ] Existing tests unchanged; new tests cover positive + negative (exclusivity)
      + orthogonality + no-action cases.
- [ ] `CliArgs` is `pub`; `RunParameters` is `pub` ⇒ no `private_interfaces` warning.

### Documentation & Deployment
- [ ] `parse_cli_args` doc comment documents both flags, the sweep semantics, the
      ArgGroup exclusivity, and references PRD §11.
- [ ] README §Command Line Options updated (table + note).
- [ ] No version bump (P1.M4.T2.S1); no full README rewrite (P1.M4.T3.S1).

---

## Anti-Patterns to Avoid

- ❌ Don't add `list_callbacks` to `RunParameters`. It's the library's transport
  parameter type used directly by `qmkonnect` (PRD §3); a CLI flag is a leaky
  abstraction. Use the `CliArgs` wrapper. (F0.)
- ❌ Don't add a `ListCallbacks` variant to `RunCommand`. `RunCommand` is the
  wire-protocol command enum (PRD §3/§10); a CLI orchestration variant corrupts
  it. `--list-callbacks` maps to `QueryInfo` + a CLI-side sweep flag. (F0.)
- ❌ Don't touch `run()`, run()'s doc comment, or `build_payload`. P1.M3.T3.S1
  edits them in parallel right now. This item's lib.rs edits are confined to the
  `parse_cli_args` region (CliArgs + the three new helpers placed just before
  `pub fn parse_cli_args`, the parse_cli_args body/doc, and tests). (F5.)
- ❌ Don't use `get_matches()`/`get_matches_from()` in tests. They call
  `std::process::exit` on any parse error (incl. `--help`) and kill the `cargo
  test` runner. Build `ArgMatches` with `build_cli_command().try_get_matches_from(..)`
  (returns `Result`, no exit) and feed them to the pure `parse_matches`. (F2.)
- ❌ Don't add a new `QmkError` variant for clap parse errors. The public
  `parse_cli_args` uses `get_matches()`, so clap owns `--help`/`--version`/
  conflict/unknown-flag printing+exit (perfect UX). `parse_matches` returns
  `QmkError` only for `--create-config` (`RemovedFeature`) and no-action
  (`MissingRequiredParameter`, message extended). No `error.rs` change. (F6.)
- ❌ Don't drop the `create-config` RemovedFeature trap or change its message. It
  stays FIRST in `select_command` and outside the `ArgGroup`, exactly as today.
- ❌ Don't change `run()`'s signature or the `pub use core::{..}` re-export. This
  item adds `CliArgs` and changes `parse_cli_args`'s return type ONLY.
- ❌ Don't rewrite the whole README or bump the version. Those are P1.M4.T3.S1 /
  P1.M4.T2.S1. This item touches ONLY the §Command Line Options table + note. (F7.)
- ❌ Don't make `--query-info`/`--list-callbacks` take a short flag that clashes
  with the existing `-i/-p/-u/-a/-v/-l/-c`. They are long-only.
- ❌ Don't forget that `arg_required_else_help(true)` must come AFTER `.group(..)`
  in the builder chain (builder order matters).

---

**Confidence Score: 9/10** for one-pass implementation success. The deliverable
spans three files but each change is given **verbatim** in Implementation Patterns
§A–§F, and the four genuinely risky constructs — `ArgGroup` mutual exclusivity,
`try_get_matches_from` no-exit testing, the pure `build_cli_command`/`parse_matches`
split, and the `main.rs` extract-Copy-then-move-into-`run` sweep — are
**empirically compile/clippy/fmt/test-verified** on a faithful scratch model
(research `notes.md` F1: 6 tests pass, 0 warnings on the real-crate-relevant code).
The one intentional PRD §3 deviation (`parse_cli_args` return type) is
low-impact and well-justified (qmkonnect uses `run()` directly, not
`parse_cli_args`; only `main.rs` calls it and it is updated here — F0). The
parallel-execution boundary with P1.M3.T3.S1 is precisely scoped (do NOT touch
`run()`; disjoint lib.rs regions — F5), and this item compiles + tests green even
if P1.M3.T3.S1 has not yet landed (the sweep simply won't fire without hardware).
No `error.rs`/`Cargo.toml` change (F6). The single residual uncertainty — whether
`--verbose`-alone flows to `select_command`'s `MissingRequiredParameter` or clap
errors first — is empirically resolved in the scratch test (`no_action_errors`
passes) and re-checked in Level 3's manual runtime step. Deducting 1 point only
for the inherent inability to integration-test the live callback sweep without HID
hardware (a manual QA step, consistent with the rest of v0.3.0).