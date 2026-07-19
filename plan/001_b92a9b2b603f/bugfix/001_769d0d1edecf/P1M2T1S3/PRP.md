# PRP — P1.M2.T1.S3 (bugfix): Update existing `parse_matches` / CLI tests for the new `RunParameters` return type

---

## Goal

**Feature Goal**: Fix the **last broken compile site** left by P1.M2.T1.S1
(Issue 2): the library's own `#[cfg(test)] mod tests` block in `src/lib.rs`
still references the **deleted** `CliArgs` type and the old `.params` /
`.list_callbacks` field paths. After S1, `parse_matches` returns
`Result<RunParameters, QmkError>` (not `CliArgs`), so the test module fails to
compile (`E0412 cannot find type 'CliArgs'` at the `cli_for` helper, then a
cascade of `E0609 no field 'params'/'list_callbacks'` once the helper is fixed).
This task rewrites the affected tests to assert on `RunParameters` fields
**directly** (`params.command`, `params.usage_page`, …) and **deletes** every
`assert!(…cli.list_callbacks)` line — the sweep flag no longer exists on the
return type (it moved out-of-band to `main.rs` via `std::env::args` in
P1.M2.T1.S2). The two CLI tests that don't inspect the `Ok` value are left
untouched. This completes the P1.M2.T1 milestone: with S1 (lib) + S2 (main.rs)
+ S3 (tests) all landed, the crate compiles and `cargo test` is fully green.

**Deliverable**: **Edits confined to the `#[cfg(test)] mod tests` block of
`src/lib.rs`** (lines ~866–920), touching exactly **1 helper + 4 test
functions** with verbatim old→new text (below). Specifically:
1. `cli_for` helper — return type `CliArgs` → `RunParameters`; `.expect()` message
   `"…CliArgs"` → `"…RunParameters"`.
2. `test_parse_query_info_flag` — rename binding `cli` → `params`; drop `.params`;
   delete `assert!(!cli.list_callbacks);`.
3. `test_parse_list_callbacks_flag` — rename binding `cli` → `params`; drop
   `.params`; **delete** `assert!(cli.list_callbacks);` (keep the `QueryInfo`
   assertion — `--list-callbacks` still maps to `RunCommand::QueryInfo`).
4. `test_query_info_combines_with_device_flags` — rename binding `cli` → `params`;
   drop `.params`; delete `assert!(!cli.list_callbacks);`.
5. `test_message_and_list_still_parse` — rename binding `cli` → `params` (×2,
   valid shadowing); drop `.params` (×2); delete `assert!(!cli.list_callbacks);`
   (×2).

No new tests, no new types, no new imports, no logic change. Pure mechanical
re-plumbing of test assertions to match S1's return-type change.

**Success Definition**: `cargo fmt --check` → exit 0; **`cargo test --lib` →
all 26 tests pass** (the S3 gate, S2-independent — see *Gate independence*);
`cargo clippy --all-targets` → zero warnings (now possible since the test module
compiles); `grep -rn "CliArgs" --include="*.rs" .` → **zero matches anywhere**;
`grep -n "cli\.params\|cli\.list_callbacks" src/lib.rs` → zero matches; no file
other than `src/lib.rs` modified; the two CLI tests that need no change
(`test_action_selectors_are_mutually_exclusive`,
`test_no_action_given_is_missing_parameter`) remain byte-for-byte identical.

## User Persona (if applicable)

**Target User**: The `qmk_notifier` crate's own maintainers and CI — the test
suite is the safety net that guards the PRD §3 public-API contract restored by
S1/S2. (End users of the binary are unaffected: this is test-only code.)

**Use Case**: A maintainer runs `cargo test` before merging. Post-S1 (and before
S3), that command **fails to compile** because the test module still names the
deleted `CliArgs`. After S3, `cargo test` is green and the CLI-parsing coverage
(query-info, list-callbacks, device-flag combinations, message/list actions,
mutual exclusivity, missing-action) is preserved — now asserting against the
documented `RunParameters` type.

**User Journey**: `cargo test --lib` → compiles the lib + `#[cfg(test)]` harness
→ 26 tests run → all pass (4 rewritten CLI tests now assert on `RunParameters`
fields; the `--list-callbacks` sweep bool is no longer asserted because it left
the type in S1 and now lives in `main.rs` via `env::args`).

**Pain Points Addressed**: Restores a compiling, passing test suite after S1's
deliberate API change (S1 left the test module broken as the S3 handoff). Keeps
the CLI coverage meaningful without the removed `list_callbacks` bool.

## Why

- **Closes the S1/S2/S3 chain (Issue 2, PRD §h3.1)**: S1 restored the
  PRD-§3-documented signature `parse_cli_args() -> Result<RunParameters, QmkError>`
  and deleted `CliArgs`; S2 rewired `main.rs` off `CliArgs`; **S3 rewires the
  tests**. The three together make the crate fully compile and test-green against
  the documented contract. S3 is the final, smallest piece.
- **The `--list-callbacks` sweep is no longer observable from the library**
  (Issue 2 *Suggested Fix* (a)): after S1, `--list-callbacks` and `--query-info`
  BOTH map to `RunCommand::QueryInfo` and `RunParameters` carries no sweep flag.
  The sweep is detected out-of-band by `main.rs` via `std::env::args` (S2). So the
  test `test_parse_list_callbacks_flag` can no longer assert `list_callbacks ==
  true` — it asserts only that the flag parses to `QueryInfo` (still meaningful:
  it verifies clap accepts `--list-callbacks` and `select_command` maps it
  correctly). The `assert!(!cli.list_callbacks)` lines in the other tests are
  simply removed (there is nothing to assert).
- **S1/S2 PRPs explicitly defer this work to S3**: both prior PRPs' *Anti-Patterns*
  say "Don't edit the `#[cfg(test)] mod tests` block — S3 owns it" and predict the
  exact errors (`E0433 cannot find type CliArgs` at `cli_for`; `E0609` on
  `cli.params`/`cli.list_callbacks`). This PRP resolves precisely those errors.
- **Low blast radius, high mechanical certainty**: the change set is grep-verified
  across the entire repo (see *Context Completeness Check*). Every `CliArgs` and
  `cli.params`/`cli.list_callbacks` reference lives in these 5 sites; nothing else
  in the project references the old API. No design decisions remain — `RunParameters`
  fields are all `pub`, `RunCommand` derives `PartialEq`/`Eq`, and the existing
  `matches!`-based assertions need no `PartialEq` anyway.

## What

All edits are in `src/lib.rs`, inside the `#[cfg(test)] mod tests { … }` block
(lines ~866–920). The exact old→new text for each of the 5 sites is below. Locate
each block by content grep (line numbers drift); the anchors are unique.

### Edit 1 — `cli_for` helper (return type + `.expect()` message)

**OLD** (find via `grep -n "fn cli_for" src/lib.rs`):

```rust
    fn cli_for(args: &[&str]) -> CliArgs {
        let matches = build_cli_command()
            .try_get_matches_from(args)
            .expect("test args should parse");
        parse_matches(&matches).expect("test args should resolve to CliArgs")
    }
```

**NEW**:

```rust
    fn cli_for(args: &[&str]) -> RunParameters {
        let matches = build_cli_command()
            .try_get_matches_from(args)
            .expect("test args should parse");
        parse_matches(&matches).expect("test args should resolve to RunParameters")
    }
```

> Only the return type and the `.expect()` message string change. The body logic
> (build clap matches via the no-exit `try_get_matches_from`, then call the pure
> `parse_matches`) is unchanged. `parse_matches` now returns
> `Result<RunParameters, QmkError>` (S1), so the binding type follows automatically.

### Edit 2 — `test_parse_query_info_flag`

**OLD**:

```rust
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
```

**NEW**:

```rust
    #[test]
    fn test_parse_query_info_flag() {
        let params = cli_for(&["qmk_notifier", "--query-info"]);
        assert!(matches!(params.command, RunCommand::QueryInfo));
        // Defaults preserved.
        assert_eq!(params.usage_page, DEFAULT_USAGE_PAGE);
        assert_eq!(params.usage, DEFAULT_USAGE);
        assert_eq!(params.vendor_id, None);
        assert_eq!(params.product_id, None);
    }
```

> Changes: `let cli` → `let params`; `cli.params.*` → `params.*` (×6);
> **delete** `assert!(!cli.list_callbacks);` (the bool left the type in S1;
> `--query-info` was never the sweep, and the sweep is now `main.rs`'s concern —
> nothing to assert here). The 4 default-value assertions stay (they verify the
> clap defaults flow through `parse_matches` into `RunParameters`).

### Edit 3 — `test_parse_list_callbacks_flag`

**OLD**:

```rust
    #[test]
    fn test_parse_list_callbacks_flag() {
        let cli = cli_for(&["qmk_notifier", "--list-callbacks"]);
        // list-callbacks maps to QueryInfo + the sweep signal.
        assert!(matches!(cli.params.command, RunCommand::QueryInfo));
        assert!(cli.list_callbacks);
    }
```

**NEW**:

```rust
    #[test]
    fn test_parse_list_callbacks_flag() {
        let params = cli_for(&["qmk_notifier", "--list-callbacks"]);
        // --list-callbacks maps to QueryInfo (the library sees no difference from
        // --query-info). The callback sweep is now a CLI-only concern detected
        // out-of-band by main.rs via std::env::args; RunParameters carries no
        // sweep flag, so there is nothing further to assert here.
        assert!(matches!(params.command, RunCommand::QueryInfo));
    }
```

> Changes: `let cli` → `let params`; `cli.params.command` → `params.command`;
> rewrite the comment to explain why the sweep is no longer asserted; **delete**
> `assert!(cli.list_callbacks);`. The `QueryInfo` assertion stays — it is the
> meaningful post-S1 coverage (verifies clap accepts `--list-callbacks` and
> `select_command` maps it to `RunCommand::QueryInfo`). This test is intentionally
> kept (not merged into `test_parse_query_info_flag`) so each CLI flag has its own
> parse test.

### Edit 4 — `test_query_info_combines_with_device_flags`

**OLD**:

```rust
    #[test]
    fn test_query_info_combines_with_device_flags() {
        // Device-targeting flags are orthogonal to the action group.
        let cli = cli_for(&[
            "qmk_notifier",
            "--query-info",
            "--vendor-id",
            "0xFEED",
            "-v",
        ]);
        assert!(matches!(cli.params.command, RunCommand::QueryInfo));
        assert_eq!(cli.params.vendor_id, Some(0xFEED));
        assert!(cli.params.verbose);
        assert!(!cli.list_callbacks);
    }
```

**NEW**:

```rust
    #[test]
    fn test_query_info_combines_with_device_flags() {
        // Device-targeting flags are orthogonal to the action group.
        let params = cli_for(&[
            "qmk_notifier",
            "--query-info",
            "--vendor-id",
            "0xFEED",
            "-v",
        ]);
        assert!(matches!(params.command, RunCommand::QueryInfo));
        assert_eq!(params.vendor_id, Some(0xFEED));
        assert!(params.verbose);
    }
```

> Changes: `let cli` → `let params`; `cli.params.*` → `params.*` (×3);
> **delete** `assert!(!cli.list_callbacks);`. The orthogonal-flag coverage
> (`--vendor-id 0xFEED` + `-v` flow into `RunParameters.vendor_id`/`.verbose`) stays.

### Edit 5 — `test_message_and_list_still_parse`

**OLD**:

```rust
    #[test]
    fn test_message_and_list_still_parse() {
        let cli = cli_for(&["qmk_notifier", "hello"]);
        assert!(matches!(cli.params.command, RunCommand::SendMessage(s) if s == "hello"));
        assert!(!cli.list_callbacks);

        let cli = cli_for(&["qmk_notifier", "--list"]);
        assert!(matches!(cli.params.command, RunCommand::ListDevices));
        assert!(!cli.list_callbacks);
    }
```

**NEW**:

```rust
    #[test]
    fn test_message_and_list_still_parse() {
        let params = cli_for(&["qmk_notifier", "hello"]);
        assert!(matches!(params.command, RunCommand::SendMessage(s) if s == "hello"));

        let params = cli_for(&["qmk_notifier", "--list"]);
        assert!(matches!(params.command, RunCommand::ListDevices));
    }
```

> Changes: `let cli` → `let params` (×2 — valid shadowing, same function, each
> binding used by the immediately-following assertion before the next shadows it);
> `cli.params.command` → `params.command` (×2); **delete** both
> `assert!(!cli.list_callbacks);` lines and the blank line between them. The
> `SendMessage(s) if s == "hello"` and `ListDevices` patterns are unchanged
> (valid `RunCommand` variants, lib.rs:21–24).

### Success Criteria

- [ ] `cli_for` helper return type is `RunParameters` (not `CliArgs`); the
      `.expect()` message says `RunParameters`.
- [ ] All 4 rewritten tests bind the `cli_for(...)` result to `params` (not `cli`)
      and access fields directly (`params.command`, `params.vendor_id`, etc.) with
      no `.params` intermediate.
- [ ] **Zero** `assert!(…cli.list_callbacks)` / `assert!(…params.list_callbacks)`
      lines remain in `src/lib.rs`.
- [ ] `test_parse_list_callbacks_flag` still asserts
      `matches!(params.command, RunCommand::QueryInfo)` (the flag-to-command
      mapping coverage is preserved).
- [ ] `test_action_selectors_are_mutually_exclusive` and
      `test_no_action_given_is_missing_parameter` are **byte-for-byte unchanged**.
- [ ] `grep -rn "CliArgs" --include="*.rs" .` → **zero matches** anywhere in the repo.
- [ ] `grep -n "cli\.params\|cli\.list_callbacks" src/lib.rs` → zero matches.
- [ ] `cargo fmt --check` → exit 0; `cargo test --lib` → **26 tests pass**;
      `cargo clippy --all-targets` → zero warnings.
- [ ] No file other than `src/lib.rs` modified.

## All Needed Context

### Context Completeness Check

> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The verbatim old→new text
> for all 5 sites is above, each located by a unique content grep. The two
> unchanged CLI tests are explicitly named and described. The dependency on S1
> (parse_matches now returns RunParameters; CliArgs deleted) is stated as a
> contract, and the **verified working-tree state** (S1 landed/committed; S2 in
> progress; `cargo test --lib` currently fails with E0412 at lib.rs:868:34) is
> documented so the implementer knows exactly where they are. The error cascade
> (fixing the helper return type surfaces the E0609 body errors) is explained so
> it is not mistaken for a new problem. `RunParameters` field names/types and
> `RunCommand`'s variants + `PartialEq`/`Eq` derive are verified (research notes).
> The gate independence (`cargo test --lib` does not compile main.rs, so S3's gate
> is S2-independent) is documented so an implementer can validate even if S2 is
> still in flight.

### Documentation & References

```yaml
# MUST READ — the file being edited (all 5 edits live in its #[cfg(test)] mod tests block)
- file: src/lib.rs
  why: "Holds the cli_for helper (lib.rs:868) and the 4 CLI tests to rewrite
        (test_parse_query_info_flag:876, test_parse_list_callbacks_flag:888,
        test_query_info_combines_with_device_flags:896, test_message_and_list_still_parse:912),
        PLUS the 2 CLI tests to leave unchanged (test_action_selectors_are_mutually_exclusive:923,
        test_no_action_given_is_missing_parameter:943). Also holds RunParameters (lib.rs:120)
        and RunCommand (lib.rs:19) — defined here, consumed unchanged."
  pattern: "The CLI tests use a private helper `cli_for(args)` that calls
            build_cli_command().try_get_matches_from(args) (the no-exit variant — NEVER
            get_matches* in a test, it process-exits on error) then the pure parse_matches,
            and assert on the returned struct's fields with matches!/assert_eq!."
  gotcha: "POST-S1, parse_matches returns RunParameters (not CliArgs). The tests still
           reference the deleted CliArgs type (lib.rs:868) and the removed .params/.list_callbacks
           field paths — those are EXACTLY the 5 sites to fix. S1 is already committed; S2
           (main.rs) is in progress but IRRELEVANT to cargo test --lib (lib test harness does
           not compile main.rs)."

# MUST READ — the S1 contract (the completed dependency): what parse_matches returns now
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S1/PRP.md
  why: "S1 changed parse_matches -> Result<RunParameters, QmkError> (was CliArgs),
        select_command -> RunCommand (dropped the bool tuple), and DELETED pub struct CliArgs.
        S1 explicitly DEFERRED the test-module rewrite to S3 (this task) — its Anti-Patterns
        say 'Don't edit #[cfg(test)] mod tests — S3 owns it' and predict the exact errors
        (E0433 cannot find CliArgs at cli_for; E0609 on cli.params/cli.list_callbacks).
        Treat as the CONTRACT defining the post-S1 lib.rs state S3 consumes."

# MUST READ — the S2 PRP (the parallel/in-progress sibling): confirms the sweep moved to env::args
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S2/PRP.md
  why: "S2 rewires main.rs: parse_cli_args() now returns RunParameters, so main.rs binds it
        to `params` (not `cli`), reads device scalars off `params.*` directly, and detects
        --list-callbacks via std::env::args().any(|a| a == \"--list-callbacks\") OUT-OF-BAND
        (RunParameters carries no sweep flag). This is WHY S3 deletes the
        assert!(cli.list_callbacks) lines: the sweep bool left the library type entirely.
        S2 also confirms the binding-rename convention `params` (which S3 follows for
        consistency). S2 does NOT touch the test module — that is S3's exclusive scope."

# MUST READ — the bug this fixes + the documented target signature
- file: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 2 (§h3.1): PRD §3 line 154 mandates parse_cli_args -> Result<RunParameters, QmkError>;
        the CliArgs return was undocumented drift. Suggested Fix (a) = S1+S2+S3: restore the
        signature, move --list-callbacks out-of-band, update the tests. S3 is the test half."
  section: "Major Issues / Issue 2"

# MUST READ — the PRD public-API contract being restored
- file: PRD.md
  why: "§3 (Public API) line 154 is the target signature the tests now assert against
        (RunParameters, not CliArgs). §11 (CLI) documents --list-callbacks as a CLI action."
  section: "3. Public API", "11. CLI"

# REFERENCE — empirical evidence (verified working-tree state, complete grep-verified change set,
#             RunParameters field types, RunCommand PartialEq derive, gate independence)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S3/research/notes.md
  why: "Documents: S1 is committed (lib.rs not in git status), S2 is in progress (main.rs
        modified), cargo test --lib currently fails E0412 at lib.rs:868:34; the COMPLETE
        grep-verified change set (every CliArgs / cli.params / cli.list_callbacks reference
        in the repo is one of the 5 sites); RunParameters fields are all pub (command,
        vendor_id/product_id: Option<u16>, usage_page/usage: u16, verbose: bool); RunCommand
        derives PartialEq/Eq; the 5 list_callbacks assertion deletions; the 2 unchanged CLI
        tests; gate independence (cargo test --lib is S2-independent); cargo doc safety
        (no doctests, no [CliArgs] intra-doc links remain)."
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
    ├── main.rs         # S2's scope (in progress) — DO NOT TOUCH
    ├── lib.rs          # <-- FILE TO EDIT (only the #[cfg(test)] mod tests block, lines ~866-920)
    │                    #     NON-TEST lib.rs is already POST-S1 (committed) — DO NOT TOUCH it.
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # transport (burst_to_one, send_raw_report, FakeHid...) — DO NOT TOUCH
# No tests/ dir, no examples/, no benches/ (verified) — all tests live in lib.rs's #[cfg(test)] mod.
```

### Desired Codebase tree with files to be modified

```bash
src/
└── lib.rs   # MODIFIED ONLY in its #[cfg(test)] mod tests block (~lines 866-920):
             #   (1) cli_for: return type CliArgs -> RunParameters; .expect msg -> RunParameters
             #   (2) test_parse_query_info_flag: cli->params, drop .params, del list_callbacks assert
             #   (3) test_parse_list_callbacks_flag: cli->params, drop .params, del list_callbacks assert
             #       (keep QueryInfo assert; rewrite comment)
             #   (4) test_query_info_combines_with_device_flags: cli->params, drop .params, del assert
             #   (5) test_message_and_list_still_parse: cli->params (x2), drop .params (x2), del 2 asserts
             # test_action_selectors_are_mutually_exclusive: UNCHANGED
             # test_no_action_given_is_missing_parameter: UNCHANGED
             # All non-test code (select_command/parse_matches/parse_cli_args/RunParameters/RunCommand):
             #   UNCHANGED (already POST-S1).
# (main.rs, error.rs, core.rs, Cargo.toml unchanged)
```

> No new files. One file modified (`src/lib.rs`, test module only). No new
> dependencies, no new types, no new tests, no new imports.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (DEPENDENCY ON S1): the non-test lib.rs is ALREADY post-S1 (committed).
//   parse_matches -> Result<RunParameters, QmkError>; CliArgs is DELETED. The ONLY
//   remaining CliArgs references in the entire repo are the 5 test-module sites this
//   task fixes (grep-proven). Do NOT re-add CliArgs or touch non-test lib.rs.
//
// CRITICAL (THE ERROR CASCADE): `cargo test --lib` currently fails at the FIRST
//   error — E0412 "cannot find type `CliArgs`" at the cli_for helper return type
//   (lib.rs:868). The compiler aborts there and does NOT yet show the downstream
//   E0609 "no field `params`/`list_callbacks` on type `RunParameters`" errors in
//   the 4 test bodies. After you apply Edit 1 (helper return type -> RunParameters),
//   the compiler proceeds and THEN surfaces the E0609s — these are EXPECTED and are
//   fixed by Edits 2-5. Do not be alarmed by a short error burst after Edit 1; it
//   shrinks to zero as you apply Edits 2-5.
//
// CRITICAL (GATE INDEPENDENCE — cargo test --lib does NOT need S2): `cargo test --lib`
//   compiles ONLY the library crate + its #[cfg(test)] harness. It does NOT compile
//   src/main.rs (a separate binary target). Therefore:
//     - `cargo test --lib` passing depends on S1 (lib non-test, DONE) + S3 (this task)
//       ONLY — NOT S2.
//     - Even if S2 is still in flight (main.rs not yet committed / still broken),
//       `cargo test --lib` will be green once S3 lands. THIS IS THE S3 GATE.
//   `cargo build` (full) and `cargo test` (full) DO compile main.rs and need S2 too —
//   those are the MILESTONE (P1.M2.T1) gates, not the S3-scoped gate. If `cargo build`
//   (full) fails only in src/main.rs while `cargo test --lib` passes, S3 is DONE and
//   the main.rs failure is S2's scope (not yours).
//
// CRITICAL (DELETE, DON'T CONVERT, the list_callbacks asserts): the `list_callbacks`
//   bool field no longer exists on RunParameters (S1 removed it; S2 moved sweep
//   detection to main.rs env::args). There is NOTHING to assert about it in the
//   library tests. So:
//     - assert!(!cli.list_callbacks)  -> DELETE the line entirely (5 of these).
//     - assert!(cli.list_callbacks)   -> DELETE the line (1 of these, in
//       test_parse_list_callbacks_flag); KEEP the QueryInfo assertion above it
//       (the flag still maps to RunCommand::QueryInfo — that IS testable).
//   Do NOT try to "convert" them to assert on env::args — env::args is a main.rs
//   runtime concern, not observable from a pure parse_matches unit test.
//
// CRITICAL (THE 2 UNCHANGED CLI TESTS): do NOT touch
//   test_action_selectors_are_mutually_exclusive (it checks build_cli_command()
//   .try_get_matches_from(...).is_err() for clap conflicts — never calls parse_matches
//   nor inspects RunParameters) or test_no_action_given_is_missing_parameter (it
//   calls parse_matches but only matches on the Err(QmkError::MissingRequiredParameter)
//   variant — the Ok type changed but this test never inspects Ok). Editing them is
//   unnecessary and out of scope.
//
// NOTE (binding rename cli -> params): the item mandates `let params = ...` (not `cli`).
//   This matches S2's main.rs convention and the RunParameters type name. After rename,
//   `cli.params.X` becomes `params.X` (drop the `.params`). In test_message_and_list_still_parse
//   the two `let params = ...` bindings shadow each other within the same fn — valid Rust,
//   no "unused variable" warning (each is used by the immediately-following assertion
//   before the next shadows it). VERIFIED pattern in the existing code (the two `let cli`
//   already shadowed each other pre-S3).
//
// NOTE (matches! needs no PartialEq): the tests use `assert!(matches!(params.command,
//   RunCommand::QueryInfo))` etc. The matches! macro desugars to a `match` expression,
//   so it compiles regardless of whether RunCommand implements PartialEq. (It DOES
//   derive PartialEq/Eq — lib.rs:18 — but that's incidental.) Keep matches!; do not
//   switch to assert_eq! unless you want to (the item suggests `params.command ==
//   RunCommand::QueryInfo` as an alternative, but matches! is the existing style and
//   is strictly more general — it already handles SendMessage(s) if s == "hello"). 
//
// NOTE (NO new imports): RunParameters and RunCommand are defined in the SAME file
//   (lib.rs), in scope within the #[cfg(test)] mod tests block (the mod is inside the
//   crate root, so it sees all crate items). The existing `use super::*;` (or
//   equivalent) at the top of mod tests already brings RunParameters/RunCommand into
//   scope — confirm via `grep -n "use super" src/lib.rs`. Do NOT add imports.
//
// NOTE (no rustfmt.toml / clippy.toml): default configs. Run `cargo fmt` then
//   `cargo fmt --check`. The deletions of `assert!(!cli.list_callbacks);` lines may
//   leave a blank line (e.g. test_message_and_list_still_parse had a blank line between
//   the two asserts); rustfmt collapses/normalizes blank lines — let it. The NEW text
//   in each edit is already rustfmt-clean.
//
// NOTE (qmkonnect unaffected): the downstream daemon builds RunParameters directly and
//   never calls parse_cli_args (Issue 2). Test-only changes have zero downstream impact.
```

## Implementation Blueprint

### Data models and structure

No new types, structs, enums, functions, or constants. This subtask is a
**mechanical rewrite of test assertions** to match S1's return-type change:
rename one binding (`cli` → `params`), drop the `.params` intermediate on ~12
field accesses, change one helper return type (`CliArgs` → `RunParameters`), fix
one `.expect()` message string, and **delete 6 `list_callbacks` assertion
statements**. No production code changes, no new tests.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: READ src/lib.rs #[cfg(test)] mod tests block and confirm the anchors
  - `grep -n "fn cli_for\|fn test_parse_query_info_flag\|fn test_parse_list_callbacks_flag\|fn test_query_info_combines_with_device_flags\|fn test_message_and_list_still_parse" src/lib.rs`
          -> the 5 sites to edit (helper + 4 tests).
  - `grep -n "fn test_action_selectors_are_mutually_exclusive\|fn test_no_action_given_is_missing_parameter" src/lib.rs`
          -> the 2 CLI tests to LEAVE UNCHANGED (confirm by reading them).
  - READ lines ~866-951 in full — confirm the OLD text in the "What" section matches
          the working tree byte-for-byte (S1 is committed, so it should).
  - `grep -rn "CliArgs" --include="*.rs" .`  -> expect EXACTLY 2 hits (lib.rs:868 return
          type + lib.rs:872 expect message). If any other hit appears, STOP: scope is
          larger than expected — re-read research/notes.md.
  - GOAL: know the 5 exact blocks to replace and confirm the 2 unchanged tests + the
          complete-repo CliArgs grep.

Task 2: EDIT src/lib.rs — Edit 1 (cli_for helper)
  - REPLACE the helper (OLD block in "What / Edit 1") with the NEW block.
  - CHANGES: return type CliArgs -> RunParameters; .expect("...CliArgs") ->
          .expect("...RunParameters"). Body logic unchanged.
  - NOTE: after this edit, `cargo test --lib` will surface the E0609 "no field
          `params`/`list_callbacks`" errors in the 4 test bodies — EXPECTED; fixed
          by Tasks 3-6.

Task 3: EDIT src/lib.rs — Edit 2 (test_parse_query_info_flag)
  - REPLACE the test (OLD in "What / Edit 2") with NEW.
  - CHANGES: let cli -> let params; cli.params.* -> params.* (x6); DELETE
          assert!(!cli.list_callbacks);.

Task 4: EDIT src/lib.rs — Edit 3 (test_parse_list_callbacks_flag)
  - REPLACE the test (OLD in "What / Edit 3") with NEW.
  - CHANGES: let cli -> let params; cli.params.command -> params.command; rewrite
          the comment; DELETE assert!(cli.list_callbacks);. KEEP the QueryInfo
          assertion (params.command maps to QueryInfo — still testable + meaningful).

Task 5: EDIT src/lib.rs — Edit 4 (test_query_info_combines_with_device_flags)
  - REPLACE the test (OLD in "What / Edit 4") with NEW.
  - CHANGES: let cli -> let params; cli.params.* -> params.* (x3); DELETE
          assert!(!cli.list_callbacks);.

Task 6: EDIT src/lib.rs — Edit 5 (test_message_and_list_still_parse)
  - REPLACE the test (OLD in "What / Edit 5") with NEW.
  - CHANGES: let cli -> let params (x2, valid shadowing); cli.params.command ->
          params.command (x2); DELETE both assert!(!cli.list_callbacks); lines and
          the blank line between them.

Task 7: VALIDATE (the S3 gate is cargo test --lib — S2-independent)
  - RUN: `cargo fmt`, then `cargo fmt --check` (exit 0).
  - RUN: `cargo test --lib`   -> MUST pass: "26 passed". THIS IS THE S3 GATE.
          (S2-independent — see Known Gotchas. If it fails, READ the errors: any
          error naming a CliArgs/cli.params/cli.list_callbacks reference you missed
          -> fix it. Errors naming src/main.rs -> S2's scope, NOT yours, and do NOT
          appear in `cargo test --lib` anyway.)
  - RUN: `cargo clippy --all-targets`  -> zero warnings (now possible: the test
          module compiles). `cargo clippy --lib` is S1's gate (already green).
  - RUN: `cargo build --lib`   -> still passes (non-test lib.rs unchanged by S3).
  - OPTIONAL MILESTONE GATE (needs S2 too): `cargo build` (full) + `cargo test`
          (full) -> green only if S2 has also landed. If these fail ONLY in
          src/main.rs, S3 is DONE (S2's scope). If they fail in src/lib.rs, S3 is
          incomplete.
  - SANITY: `grep -rn "CliArgs" --include="*.rs" .` -> ZERO matches anywhere.
  - SANITY: `grep -n "cli\.params\|cli\.list_callbacks\|\.list_callbacks" src/lib.rs`
          -> ZERO matches (the field is gone from tests; non-test code has none).
  - SANITY: `grep -n "let cli\b" src/lib.rs` -> ZERO matches in the test module
          (all bindings renamed to params).
  - SANITY: the 2 unchanged tests still present verbatim:
          `grep -n "fn test_action_selectors_are_mutually_exclusive\|fn test_no_action_given_is_missing_parameter" src/lib.rs`.
```

### Implementation Patterns & Key Details

```rust
// === THE REWRITE SHAPE (CliArgs wrapper gone; assert on RunParameters directly) ===
//
//   BEFORE (CliArgs-based):                       AFTER (RunParameters-based):
//   fn cli_for(...) -> CliArgs                    fn cli_for(...) -> RunParameters
//   let cli = cli_for(...);                       let params = cli_for(...);
//   matches!(cli.params.command, QueryInfo)       matches!(params.command, QueryInfo)
//   assert_eq!(cli.params.usage_page, D)          assert_eq!(params.usage_page, D)
//   assert!(cli.list_callbacks)     // bool       // (deleted — bool left the type in S1)
//   assert!(!cli.list_callbacks)    // bool       // (deleted)
//
//   CliArgs was { params: RunParameters, list_callbacks: bool }. S1 deleted it;
//   parse_matches now returns RunParameters directly. So the test binding IS the
//   RunParameters (rename cli->params), field access drops the .params hop, and the
//   list_callbacks bool has no library representation to assert on (it moved to
//   main.rs env::args in S2).


// === WHY list_callbacks ASSERTS ARE DELETED, NOT CONVERTED ===
//
//   The sweep flag is no longer part of the library's return type (S1 removed it
//   from RunParameters; S2 detects it in main.rs via env::args). A pure parse_matches
//   unit test cannot observe env::args behavior — that's a main.rs integration
//   concern. So there is literally nothing to assert about list_callbacks in these
//   library tests. DELETE the lines. The QueryInfo mapping (--list-callbacks ->
//   RunCommand::QueryInfo) IS still observable and stays asserted in
//   test_parse_list_callbacks_flag.


// === WHY cargo test --lib IS THE GATE (S2-independent) ===
//
//   `cargo test --lib` compiles the library crate + its #[cfg(test)] harness ONLY.
//   It does not compile src/main.rs (separate bin target). So:
//     - S1 (lib non-test, DONE) + S3 (lib test mod, this task) => cargo test --lib green.
//     - S2 (main.rs) is NOT required for cargo test --lib.
//   This lets S3 be validated immediately, even if S2 is still in flight. The full
//   `cargo build` / `cargo test` (which DO compile main.rs) are the milestone gates
//   and need S1+S2+S3 all landed.


// === WHY matches! STAYS (no switch to assert_eq!) ===
//
//   The existing tests use `assert!(matches!(cli.params.command, Pattern))`. matches!
//   desugars to a match expression — no PartialEq needed (though RunCommand derives
//   it). It also handles guards: `SendMessage(s) if s == "hello"`. assert_eq! cannot
//   express that guard. Keep matches! for all command assertions. (The item mentions
//   `params.command == RunCommand::QueryInfo` as an alternative for the simple cases,
//   but matches! is the established, strictly-more-general style — use it.)


// === WHY THE 2 OTHER CLI TESTS ARE UNCHANGED ===
//
//   test_action_selectors_are_mutually_exclusive: only checks
//     build_cli_command().try_get_matches_from(args).is_err() for clap ArgGroup
//     conflicts. Never calls parse_matches, never inspects RunParameters. The
//     "--list-callbacks" strings in its arg slices are CLI FLAG strings (the flag
//     still exists), not field references. -> UNCHANGED.
//   test_no_action_given_is_missing_parameter: calls parse_matches(&matches) but
//     only `matches!(result, Err(QmkError::MissingRequiredParameter(_)))`. The Ok
//     type changed (CliArgs->RunParameters) but this test never inspects Ok.
//     -> UNCHANGED.
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY — and ONLY the #[cfg(test)] mod tests block (lines ~866-920)."

NO OTHER CHANGES:
  - imports:       "none — RunParameters/RunCommand are in-file, already in scope in mod tests."
  - non-test code: "UNCHANGED — select_command/parse_matches/parse_cli_args are already POST-S1."
  - types:         "none — consumes RunParameters/RunCommand as-is; CliArgs stays deleted."
  - clap config:   "UNCHANGED — build_cli_command / ArgGroup untouched (Issue 5 is P1.M2.T2.S1)."
  - deps:          "none — no Cargo.toml change."

PUBLIC API SURFACE:
  - changes:  "(none — test-only code; the library public API was changed by S1, not S3.)"
  - unchanged: "all of lib.rs's public surface (RunCommand, RunParameters, CommandResponse,
               parse_cli_args signature post-S1, run, etc.)."

SCOPE BOUNDARY:
  - ONLY src/lib.rs's #[cfg(test)] mod tests block is modified (5 sites). Do NOT:
    * edit non-test lib.rs (S1 already did; it's committed and green).
    * edit src/main.rs (S2's scope).
    * edit error.rs, core.rs, or Cargo.toml.
    * touch build_cli_command (Issue 5 / P1.M2.T2.S1) or run() (different milestone).
    * add new tests (the item is "update existing tests", not "add coverage").
```

## Validation Loop

### Level 1: Syntax & Style

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Non-test library still compiles (S3 doesn't touch it, but confirm no accidental edit).
cargo build --lib 2>&1 | tee /tmp/s3_build_lib.log
# Expected: "Finished `dev` profile ..." and NO "warning:"/"error" lines. (S1's gate,
#   re-verified — should be unchanged by S3.)

# Lint EVERYTHING including tests (now possible: the test module compiles post-S3).
cargo clippy --all-targets 2>&1 | tee /tmp/s3_clippy.log
# Expected: zero warnings/errors. (Pre-S3 this failed with E0412 in the test module;
#   post-S3 it is clean.) If a warning fires on a test, READ it and fix.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (THE S3 GATE)

```bash
# THE GATE: compile + run the library test suite. MUST pass all 26 tests.
cargo test --lib 2>&1 | tee /tmp/s3_test_lib.log
# Expected: "test result: ok. 26 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out".
# (S2-INDEPENDENT: cargo test --lib does NOT compile src/main.rs. If S2 is still in
#   flight, this still passes once S3 lands. THIS IS THE S3 GATE.)
# The 4 rewritten CLI tests must appear as PASSED:
#   - test_parse_query_info_flag
#   - test_parse_list_callbacks_flag
#   - test_query_info_combines_with_device_flags
#   - test_message_and_list_still_parse
# And the 2 unchanged CLI tests:
#   - test_action_selectors_are_mutually_exclusive
#   - test_no_action_given_is_missing_parameter
# If any test FAILS, READ the failure: an assertion mismatch means a field rename was
#   missed or a value changed unexpectedly (it shouldn't — S3 only renames/deletes).
```

### Level 3: Integration Testing (the milestone gate — needs S2)

```bash
# Full build (compiles main.rs too). Needs S1 + S2 + S3 all landed.
cargo build 2>&1 | tee /tmp/s3_build_full.log
# Expected (if S2 landed): "Finished `dev` profile ..." zero warnings.
# Expected (if S2 still in flight): FAILS ONLY in src/main.rs (E0609/E0308 on
#   cli.params/cli.list_callbacks/run(cli.params)) — those are S2's scope, NOT S3's.
#   If errors name src/lib.rs, S3 is incomplete; if they name only src/main.rs, S3 is DONE.

# Full test suite (compiles + runs lib tests AND any bin/integration tests).
cargo test 2>&1 | tee /tmp/s3_test_full.log
# Expected (if S2 landed): all tests pass (26 lib tests; no separate bin tests in this crate).
# Expected (if S2 still in flight): compile failure in src/main.rs — S2's scope.
# Scoped alternative that bypasses S2 entirely: `cargo test --lib` (Level 2).

# Smoke-test the CLI parse paths via the binary (needs S2 landed). No keyboard required
# for the arg-parsing/error paths:
cargo run --quiet -- --help >/dev/null 2>&1 && echo "help: ok" || echo "help: exit $?"
# Expected: "help: ok" (clap prints help, exit 0).
```

### Level 4: Creative & Domain-Specific Validation (the change-isolation proof)

```bash
# (1) ZERO CliArgs references remain ANYWHERE in the repo:
grep -rn "CliArgs" --include="*.rs" . || echo "no CliArgs anywhere (good)"
# Expected: "no CliArgs anywhere (good)". (Pre-S3: 2 hits in src/lib.rs test module.
#   Post-S3: zero. This is the definitive "S3 complete" grep.)

# (2) ZERO cli.params / cli.list_callbacks / .list_callbacks references in lib.rs:
grep -nE "cli\.params|cli\.list_callbacks|\.list_callbacks" src/lib.rs || echo "no list_callbacks field access (good)"
# Expected: "no list_callbacks field access (good)". (The list-callbacks FLAG string
#   "--list-callbacks" still appears in build_cli_command + doc prose + test arg
#   slices — that's correct, the flag exists. Only the FIELD access is gone.)

# (3) ZERO `let cli` bindings remain in the test module (all renamed to params):
grep -nE "let cli\b" src/lib.rs || echo "no 'let cli' bindings (good)"
# Expected: "no 'let cli' bindings (good)".

# (4) The 2 unchanged CLI tests are still present and verbatim:
grep -nE "fn test_action_selectors_are_mutually_exclusive|fn test_no_action_given_is_missing_parameter" src/lib.rs
# Expected: both function signatures present.

# (5) The cli_for helper return type is RunParameters:
grep -n "fn cli_for" src/lib.rs
# Expected: "    fn cli_for(args: &[&str]) -> RunParameters {"

# (6) The test count is unchanged (S3 rewrites, doesn't add/remove tests):
grep -c "#\[test\]" src/lib.rs
# Expected: 26 (same as pre-S3; S3 rewrites 4 tests in place, deletes 0, adds 0).

# (7) Confirm the full-build failure (if any) is confined to main.rs (S2), NOT lib.rs:
cargo build 2>&1 | grep -E "^error" | grep "lib.rs" || echo "zero lib.rs errors (good — S3 done)"
# Expected: "zero lib.rs errors (good — S3 done)". (If S2 landed, cargo build is fully
#   green; if not, errors name main.rs only.)
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `cargo build --lib` → zero warnings (non-test lib unchanged).
- [ ] Level 1: `cargo clippy --all-targets` → zero warnings (test module now compiles).
- [ ] Level 1: `cargo fmt --check` → exit 0.
- [ ] Level 2 (**THE S3 GATE**): `cargo test --lib` → **26 passed; 0 failed**.
- [ ] Level 3 (milestone, needs S2): `cargo build` (full) + `cargo test` (full) green,
      OR failing only in `src/main.rs` (S2's scope, not S3's).

### Feature Validation

- [ ] `cli_for` helper returns `RunParameters`; `.expect()` message says `RunParameters`.
- [ ] All 4 rewritten tests bind to `params` and access fields directly (`params.*`).
- [ ] Zero `assert!(…cli.list_callbacks)` / `assert!(…params.list_callbacks)` lines remain.
- [ ] `test_parse_list_callbacks_flag` still asserts `--list-callbacks` → `QueryInfo`.
- [ ] `test_action_selectors_are_mutually_exclusive` + `test_no_action_given_is_missing_parameter`
      are byte-for-byte unchanged.
- [ ] Test count unchanged (26 `#[test]` fns).

### Code Quality Validation

- [ ] Binding-rename convention `params` matches S2's main.rs and the type name.
- [ ] `matches!` retained for command assertions (no unnecessary switch to `assert_eq!`).
- [ ] No new imports, no new types, no new tests, no non-test code changes.
- [ ] No file other than `src/lib.rs` modified.

### Documentation & Deployment

- [ ] `test_parse_list_callbacks_flag` comment explains why the sweep is no longer
      asserted (moved to main.rs env::args in S2; RunParameters carries no sweep flag).
- [ ] No README/PRD/architecture/Cargo.toml change (those are P1.M3 / out of scope).

---

## Anti-Patterns to Avoid

- ❌ Don't re-add `CliArgs` (or any wrapper) to make a test compile. S1 deliberately
  deleted it; S3 makes the tests assert on `RunParameters` directly. Re-adding
  `CliArgs` undoes S1 and duplicates the bug (Issue 2).
- ❌ Don't try to assert on `list_callbacks` via `env::args` in the library tests.
  `env::args` is a `main.rs` runtime concern (S2); a pure `parse_matches` unit test
  cannot observe it, and shouldn't. There is simply **nothing** to assert about the
  sweep flag in these tests — DELETE the assertion lines. (The `--list-callbacks` →
  `QueryInfo` command mapping IS still testable and stays asserted.)
- ❌ Don't touch the 2 CLI tests that need no change
  (`test_action_selectors_are_mutually_exclusive`,
  `test_no_action_given_is_missing_parameter`). The first only checks clap's `.is_err()`;
  the second only matches on the `Err` variant. Editing them is unnecessary scope creep.
- ❌ Don't edit non-test `src/lib.rs`. S1 already refactored
  `select_command`/`parse_matches`/`parse_cli_args` and deleted `CliArgs`; that work is
  **committed**. S3 edits ONLY the `#[cfg(test)] mod tests` block.
- ❌ Don't edit `src/main.rs`. Its rewire (env::args sweep detection, `run(params)`) is
  **P1.M2.T1.S2**. Touching it duplicates S2 and risks a merge conflict.
- ❌ Don't switch `matches!` to `assert_eq!` "for consistency". `matches!` handles the
  `SendMessage(s) if s == "hello"` guard in `test_message_and_list_still_parse`, which
  `assert_eq!` cannot express. Keep `matches!` for all command-pattern assertions.
- ❌ Don't rename the binding to anything other than `params`. The item mandates
  `let params = ...` (matching S2's main.rs convention and the `RunParameters` type
  name). Using `cli` (with `.params` dropped) would work but diverges from the item spec
  and S2 — use `params`.
- ❌ Don't be alarmed by the E0412 → E0609 error cascade. `cargo test --lib` currently
  aborts at the first error (E0412 at the `cli_for` return type). After Edit 1, the
  compiler proceeds and surfaces E0609 "no field `params`/`list_callbacks`" in the 4
  test bodies — those are EXPECTED and are cleared by Edits 2–5. It is not a new problem.
- ❌ Don't conclude "S3 is broken" if `cargo build` (full) fails while S2 hasn't landed.
  READ the error locations: `src/main.rs` errors ⟹ S2's scope (not S3's);
  `src/lib.rs` errors ⟹ S3 is incomplete. The S3-scoped gate is `cargo test --lib`,
  which does not compile `main.rs` and is therefore S2-independent.
- ❌ Don't delete `test_parse_list_callbacks_flag` entirely "because it now duplicates
  `test_parse_query_info_flag`". They assert on **different CLI flags** (`--list-callbacks`
  vs `--query-info`); each deserves its own parse test. Keep it, just drop the sweep
  assertion and keep the `QueryInfo` assertion.
- ❌ Don't add new tests. The item is "update existing parse_matches / CLI tests", not
  "add coverage". Scope is strictly the 5 existing sites.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable is
five fully-specified, verbatim old→new edits to a single file's `#[cfg(test)]` block,
each with a unique content-grep anchor. The change set is **grep-verified across the
entire repository** — the only `CliArgs` references in the whole repo are the 2
test-module sites in Edit 1, and the only `cli.params`/`cli.list_callbacks` references
are the 4 test bodies in Edits 2–5. No design decisions remain: `RunParameters` fields
are all `pub` (verified), `RunCommand` derives `PartialEq`/`Eq` (verified, though
`matches!` doesn't even need it), the binding-rename convention (`params`) is fixed by
the item + S2, and the 2 unchanged CLI tests are explicitly identified and explained.
The one subtlety — the E0412→E0609 error cascade after Edit 1 — is documented so the
implementer doesn't mistake it for a regression. The S3 gate (`cargo test --lib`) is
cleanly S2-independent (the lib test harness does not compile `main.rs`), so S3 can be
validated even while S2 is in flight. An implementer who follows the blueprint produces
a `src/lib.rs` whose test module compiles and passes 26 tests against S1's documented
`RunParameters` return type, completing the P1.M2.T1 milestone's test half.