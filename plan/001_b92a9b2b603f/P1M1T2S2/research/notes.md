# Research Notes — P1.M1.T2.S2: Change run() to return CommandResponse

Empirical verification of the codebase state **after** P1.M1.T1.S1/S2/S3 landed
(HostOs, extended RunCommand, CommandResponse all exist in lib.rs). Performed by
direct grep + reading the full source.

## Baseline (verified `cargo test --lib`)

```
test result: ok. 30 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

**30 tests pass** (NOT 22 — system_context.md predates the S2/S3 work which added
the CommandResponse construction tests). No tests are added or removed by this
task → expected post-state is still **30 passed, 0 failed**.

## Call-site census for `run(` (verified `grep -rn "run(" src/`)

| Location | Code | Breaks on signature change? |
|---|---|---|
| `src/main.rs:12` | `if let Err(e) = qmk_notifier::run(params) {…}` | **No** — ignores Ok value; still compiles. Contract wants it to print the response on Ok (Debug print). |
| `src/lib.rs:277` | `pub fn run(params: RunParameters) -> Result<(), QmkError>` | This IS the signature being changed. |
| `src/lib.rs:506` (`test_run_with_list_devices_command`) | `let result = run(params); assert!(result.is_ok() \|\| result.is_err());` | **No** — `is_ok()\|\|is_err()` is a tautology, type-agnostic, always compiles. |
| `src/lib.rs:525` (`test_run_with_send_message_command`) | `let result = run(params); match result { Ok(()) => {…} …}` | **YES — line 527 `Ok(()) =>`** is the only compile-breaking site. |
| `src/lib.rs:555` (`test_run_with_verbose_output`) | `let result = run(params); assert!(result.is_ok() \|\| result.is_err());` | **No** — tautology, type-agnostic. Contract item 3f mentions it ("Similarly…") but it does NOT break; no change strictly required. |

## All `Ok(())` patterns in src/ (verified `grep -rn "Ok(())" src/`)

```
src/core.rs:86   Ok(())                          # inside parse_hex_or_decimal — unrelated
src/core.rs:136  SendOutcome::AllSucceeded => return Ok(()),   # try_send_once — unrelated
src/core.rs:368  return Ok(());                  # list_hid_devices — unrelated
src/core.rs:389  Ok(())                          # send_raw_report tail — unrelated
src/lib.rs:527   Ok(()) => {                     # <<< THE ONE THAT BREAKS (match arm)
```

Only `lib.rs:527` is a pattern-match on run()'s Ok value. The core.rs ones are
return expressions of functions whose signatures are NOT changing in this task.

## Downstream signatures that MUST stay unchanged this task

- `src/core.rs:56`  `pub fn list_hid_devices() -> Result<(), QmkError>` → still `Result<(), QmkError>`
- `src/core.rs:113` `pub fn send_raw_report(...) -> Result<(), QmkError>` → still `Result<(), QmkError>`
  (its evolution to `Result<Option<Vec<u8>>, QmkError>` is **P1.M3.T2**, not this task)

Implication for run(): use `?` then construct the placeholder, e.g.
`send_raw_report(...)?; Ok(CommandResponse::Legacy { matched: true })`.
Do **not** rewrite to `send_raw_report(...).map(|_| …)` in a way that implies
send_raw_report's Ok type changed — it didn't.

## Typed-command arms already exist (verified, lib.rs ~lines 322-327)

```rust
RunCommand::QueryInfo => todo!("typed dispatch lands in P1.M3.T3.S1"),
RunCommand::QueryCallback(_) => todo!("typed dispatch lands in P1.M3.T3.S1"),
RunCommand::SetOs(_) => todo!("typed dispatch lands in P1.M3.T3.S1"),
RunCommand::ApplyHostContext { .. } => { todo!("typed dispatch lands in P1.M3.T3.S1") }
```

`todo!()` expands to `!` (never type) which coerces to **any** type — including
`Result<CommandResponse, QmkError>`. So when run()'s match changes from unifying
on `Result<(), QmkError>` to `Result<CommandResponse, QmkError>`, these four arms
compile **unchanged**. Do NOT add, remove, or modify them.

## Doc-comment / crate-doc state (verified)

- `src/lib.rs` line 1 = `mod core;` → **NO crate-level `//!` doc exists**.
- run()'s current doc comment (lib.rs ~276): `/// Core function that executes the notifier logic with explicit parameters.`
- Therefore contract item 5's "lib.rs-level doc comment should mention CommandResponse"
  resolves to **run()'s `///` doc comment** (the primary lib.rs public item), not a
  crate-level doc. Scope = rewrite run()'s doc comment only.

## Parallel-execution (P1.M1.T2.S1) interaction

S1's PRP constrains itself to **src/core.rs only** (adds constants + 1 test).
This task (S2) touches **src/lib.rs + src/main.rs only**. **Zero file overlap →
no merge conflict.** S1 does not change send_raw_report/list_hid_devices/run().
The two can land independently in either order.

## main.rs full content (verified, 16 lines)

```rust
fn main() {
    let params = match qmk_notifier::parse_cli_args() {
        Ok(params) => params,
        Err(e) => { eprintln!("Error: {}", e); std::process::exit(1); }
    };
    if let Err(e) = qmk_notifier::run(params) {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
```

The `if let Err` form discards the Ok value. Contract item 3e wants the response
printed on Ok. Minimal rewrite = convert to `match` + `println!("{:?}", response)`
(Debug print; CommandResponse already derives Debug — no Display impl needed).

## CommandResponse variant check (verified, lib.rs)

```rust
pub enum CommandResponse {
    Legacy { matched: bool },
    Info { proto_ver: u8, feature_flags: u8, callback_count: u8, board_rules_present: bool },
    CallbackName { index: u8, name: Option<String> },
    Ack { ok: bool },
    Timeout,
}
```

`#[derive(Debug, Clone, PartialEq, Eq)]` — Debug is available for main.rs
`println!("{:?}", response)`. No new derive needed. `Legacy { matched: true }`
and `Timeout` are the two variants this task constructs as placeholders.

## Why no external/web research was needed

This is a pure internal-Rust signature change (return type widening +
placeholder construction) with no new libraries, no new API surface, and no
ambiguous semantics. All five relevant files (lib.rs, main.rs, core.rs,
error.rs, the architecture docs) were read in full first-hand. The only
non-obvious facts (which `todo!()` coerces, which test breaks, which downstream
sigs are frozen, Debug-derive availability) were verified empirically above
rather than assumed.