# Research Notes — P1.M2.T2.S1 (bugfix): Drop `.short('c')` from create-config arg

## Empirical findings (verified in-repo)

### F1 — The exact edit site (lib.rs:213-220, single line to delete)
`build_cli_command()` (lib.rs:157) registers the create-config arg:
```rust
        .arg(
            Arg::new("create-config")
                .short('c')            // <-- lib.rs:215 — THE LINE TO DELETE
                .long("create-config")
                .help("Create example configuration file (REMOVED)")
                .action(ArgAction::SetTrue),
        )
```
The fix is a ONE-LINE deletion: remove `                .short('c')\n`.
Everything else (`.long("create-config")`, `.help(...)`, `.action(ArgAction::SetTrue)`)
stays. The arg ID `"create-config"` is unchanged.

### F2 — `select_command` + the ArgGroup use the LONG name (unaffected)
- `select_command` (lib.rs:262) reads `matches.get_flag("create-config")` — the
  LONG arg-id STRING, NOT the short char. Removing `.short('c')` has zero effect.
- The `action` ArgGroup (lib.rs:238) lists
  `.args(["message", "list", "create-config", "query-info", "list-callbacks"])` —
  arg IDs, not short chars. Unaffected.
- `parse_matches` / `parse_cli_args` never reference the short. Verified:
  `grep -nE "create-config|get_flag" src/lib.rs` shows ONLY the long-name usages.

### F3 — `'c'` is used ONLY by create-config (no collision risk)
All `.short(...)` in lib.rs: `'i'`(vendor-id), `'p'`(product-id), `'u'`(usage-page),
`'a'`(usage), `'v'`(verbose), `'l'`(list), `'c'`(create-config). Removing `'c'`
frees the char but no other arg claims it → no collision. query-info/list-callbacks
have NO short by design (they are diagnostic flags; the item confirms they stay
long-only).

### F4 — Zero `-c` references in src/ (no test breaks)
`grep -rnE "\"-c\"|'-c'" src/` → no matches. The action-group exclusivity tests
(P1.M2.T1.S3 territory) use the LONG form `--create-config` in their arg slices,
never `-c`. So removing the short breaks NO test. (Confirmed also by the item:
"No test asserts the -c short flag specifically.")

### F5 — README CLI table already omits -c AND --create-config
README.md "Command Line Options" table (lines ~71-86) lists: message, --vendor-id,
--product-id, --usage-page, --usage, --verbose, --list, --query-info, --list-callbacks,
--help. It does NOT list --create-config OR -c (--create-config is an undocumented
removed-feature trap; PRD §11 likewise omits it).
`grep -rnE "\-c\b|create-config" README.md` → no matches.
⇒ DOCS: none. No README/PRD change needed. (Item DOCS: "none".)

### F6 — `--help` currently shows `-c, --create-config` (will become `--create-config`)
`cargo run --quiet -- --help | grep create-config` currently shows:
`  -c, --create-config            Create example configuration file (REMOVED)`
After the fix, clap renders it as `      --create-config  ...` (indented, no short
column entry). This is the user-visible deliverable.

### F7 — PARALLEL-EXECUTION CONTEXT: the test module is currently broken (NOT my concern)
`cargo test --lib` currently FAILS: `error[E0412]: cannot find type 'CliArgs'` in
the `#[cfg(test)] mod tests` block (the S3 task, P1.M2.T1.S3, is mid-flight and
hasn't landed its test rewrites yet). THIS IS NOT RELATED TO MY CHANGE:
- My change is in NON-TEST code (`build_cli_command`, lib.rs:157-240).
- The test-module E0412 is S3's scope (it rewrites tests to use RunParameters).
- No test references `-c`, so once S3 lands, `cargo test --lib` passes 26 tests
  with my change in place — my change is test-agnostic.

### F8 — My validation gates (S3-independent)
- `cargo build --lib` → VERIFIED clean now (0.01s, zero warnings). Compiles my
  change's region (non-test lib). This is my primary gate.
- `cargo build` (full) → VERIFIED clean now. (The `#[cfg(test)]` breakage does NOT
  affect `cargo build` — test code is only compiled under `cargo test`/`--cfg test`.)
- `cargo run --quiet -- --help` → compiles main.rs (post-S2, committed) + lib
  (post-S1) and renders help. VERIFIES `-c` is gone from the CLI surface.
- `grep -n "\.short('c')" src/lib.rs` → must return ZERO matches after the edit.
- `cargo test --lib` → currently broken by S3 (not me); will be green (26 tests)
  once S3 lands. Do NOT block my task on this.

### F9 — Scope boundaries (what NOT to touch)
- Do NOT touch `select_command`, `parse_matches`, `parse_cli_args` — they use the
  long name and are unaffected. (They are S1's post-refactor code, committed.)
- Do NOT touch the `action` ArgGroup — it keys on arg IDs, not shorts.
- Do NOT edit the `#[cfg(test)] mod tests` block — that is the parallel S3 task's
  exclusive scope (will cause a merge conflict).
- Do NOT edit main.rs (S2's scope), error.rs, core.rs, Cargo.toml, README.md, PRD.md.
- Do NOT add a short to query-info/list-callbacks or rearrange other args.

## No external research needed
This is a one-line clap arg-chain edit. clap's `Arg::short()` is purely additive —
its presence/absence only affects `-c` parsing and `--help` rendering; the long
form, arg ID, ArgGroup membership, and `get_flag` behavior are all independent of
it. No library docs to cite beyond clap's (the implementer's training data covers
`Arg::short`/`Arg::long`).