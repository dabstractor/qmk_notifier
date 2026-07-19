# PRP — P1.M2.T2.S1 (bugfix): Drop `.short('c')` from create-config arg registration

> ⚠️ **READ THIS BANNER FIRST — parallel-execution context.**
>
> This task runs **in parallel with P1.M2.T1.S3** (updating the `#[cfg(test)] mod
> tests` block for the new `RunParameters` return type). S3 is mid-flight, so
> `cargo test --lib` is **currently broken** (`error[E0412]: cannot find type
> 'CliArgs'` in the test module). **That breakage is NOT this task's concern** —
> it lives entirely in the test module, which is S3's exclusive scope.
>
> THIS task (S1 of T2) edits **non-test production code** (`build_cli_command`,
> lib.rs:157-240) — specifically deleting the single `.short('c')` line on the
> `create-config` arg. The two edits are in **disjoint regions of lib.rs**
> (S3: ~lines 866-920 test block; this task: line 215 in `build_cli_command`),
> so there is **no overlap and no merge conflict**. My validation gate is
> `cargo build --lib` (non-test, verified clean now) + `cargo run -- --help`
> (verifies `-c` is gone) — both are **S3-independent**. Do NOT block this task
> on `cargo test --lib` (it's broken by S3, not by this change, and no test
> references `-c` anyway — verified).

---

## Goal

**Feature Goal**: Remove the **undocumented `-c` short flag** from the
`--create-config` argument registration in `build_cli_command`, so the CLI
surface and `--help` output no longer expose a short form that the PRD §11
option table and README CLI table never documented (Issue 5, PRD §h3.4). The
`--create-config` **long** form stays fully functional (still surfaces
`QmkError::RemovedFeature` via `select_command`).

**Deliverable**: A **single-line deletion** in `src/lib.rs` — remove
`                .short('c')\n` from the `Arg::new("create-config")` chain inside
`build_cli_command()` (lib.rs:215). `--create-config`, `.help(...)`, and
`.action(ArgAction::SetTrue)` all stay. **`src/lib.rs` is the ONLY file
modified** — no test, doc, or Cargo change.

**Success Definition**: `cargo build --lib` → zero warnings; `cargo run --quiet
-- --help` no longer renders `-c` (only `--create-config`); `grep -n "\.short('c')"
src/lib.rs` → zero matches; `--create-config` still works (`cargo run --quiet --
--create-config` surfaces the `RemovedFeature` error); no file other than
`src/lib.rs` modified.

## User Persona (if applicable)

**Target User**: CLI users / maintainers who read `--help` or the README option
table. The undocumented `-c` is doc drift — it appears in `--help` but nowhere in
the PRD §11 table or README, so a user relying on the docs would be surprised it
exists.

**Use Case**: A user runs `qmk_notifier --help` and sees only the documented
options; `--create-config` is listed (as the removed-feature trap) without a
spurious `-c` short alias.

**User Journey**: `qmk_notifier --help` → no `-c, --create-config` line; instead
`--create-config` appears with no short column. `qmk_notifier --create-config`
still errors with `RemovedFeature` (unchanged behavior).

**Pain Points Addressed**: Eliminates the cosmetic doc drift between `--help`
(which showed `-c`) and the PRD §11 / README option tables (which omit it).
Behavior is unchanged — `--create-config` remains a removed-feature trap.

## Why

- **PRD §h3.4 (Issue 5)** flags this as Minor doc drift: `--create-config`
  registers `.short('c')` which appears in `--help` but is undocumented in PRD §11
  and README. The PRD's *Suggested Fix*: "Drop the undocumented `-c` short flag."
  This PRP implements that fix (the "drop" branch, not the "add to docs" branch).
- **Trivial + surgical**: one line, no behavior change, no test impact, no doc
  update (README already omits `-c`). Lowest-risk change in the milestone.
- **Closes P1.M2.T2** (the only subtask under it): with this landed, Issue 5 is
  resolved and the milestone P1.M2 (Public API, CLI & NFR Conformance) loses its
  last CLI subtask (P1.M2.T3 SIGPIPE is a separate concern).
- **No downstream impact**: `qmkonnect` constructs `RunParameters` directly and
  never invokes the binary CLI; the `-c` removal affects only `--help` rendering
  and `-c` parsing (which no documented flow used).

## What

### The single edit (one-line deletion)

In `build_cli_command()` inside `src/lib.rs`, the `create-config` arg is
registered (lib.rs:213-220) as:

```rust
        .arg(
            Arg::new("create-config")
                .short('c')                                          // <-- DELETE THIS LINE
                .long("create-config")
                .help("Create example configuration file (REMOVED)")
                .action(ArgAction::SetTrue),
        )
```

Delete ONLY the `                .short('c')` line. Keep `.long("create-config")`,
`.help(...)`, and `.action(ArgAction::SetTrue)`. The arg ID `"create-config"` is
unchanged.

After the edit:

```rust
        .arg(
            Arg::new("create-config")
                .long("create-config")
                .help("Create example configuration file (REMOVED)")
                .action(ArgAction::SetTrue),
        )
```

### Success Criteria
- [ ] The `.short('c')` line is gone from the `create-config` arg in
      `build_cli_command` (lib.rs).
- [ ] `.long("create-config")`, `.help(...)`, `.action(ArgAction::SetTrue)` and the
      arg ID `"create-config"` are all UNCHANGED.
- [ ] `cargo build --lib` → zero warnings.
- [ ] `cargo run --quiet -- --help` → no `-c` in output (only `--create-config`).
- [ ] `cargo run --quiet -- --create-config` → still surfaces `RemovedFeature`
      (exits non-zero with the config-removed message).
- [ ] `grep -n "\.short('c')" src/lib.rs` → zero matches.
- [ ] No file other than `src/lib.rs` modified.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything
> needed to implement this successfully?"_ — **Yes.** The exact old→new block is
> quoted verbatim (a one-line deletion with its surrounding 4-line anchor, which
> is unique in the file). The reason `select_command` / the ArgGroup are
> unaffected (they key on the long-name arg ID, not the short char) is documented
> with grep evidence. The README already omits `-c` (grep-proven), so no doc edit.
> No test references `-c` (grep-proven). The parallel-S3 context (test module is
> currently broken, but `cargo build --lib` is clean and is my gate) is explained
> so the implementer doesn't chase a red herring. All validation commands are
> verified working in this repo.

### Documentation & References

```yaml
# MUST READ — the file being edited (the single .short('c') line lives here)
- file: src/lib.rs
  why: "Holds build_cli_command() (lib.rs:157) — the clap Command builder. The
        create-config arg is registered at lib.rs:213-220 with .short('c') at
        line 215 — the ONE line to delete. select_command (lib.rs:262) reads
        matches.get_flag(\"create-config\") using the LONG arg-id string, so it is
        UNAFFECTED by the short removal. The action ArgGroup (lib.rs:238) lists
        arg IDs (\"create-config\"), not short chars — also unaffected."
  pattern: "Each .arg(Arg::new(\"name\").short('x').long(\"name\").help(..)
            .action(..)) is an independent chain; .short() is purely additive
            (its presence only enables -x parsing and a --help short-column
            entry). Removing it leaves .long(), the arg ID, ArgGroup membership,
            and get_flag(\"name\") all intact."
  gotcha: "POST bugfix-refactor: the CLI is built in build_cli_command() (NOT the
           old inline parse_cli_args). parse_cli_args (lib.rs:354) now just calls
           build_cli_command().get_matches() then parse_matches(&matches). Do not
           hunt for an inline Command::new in parse_cli_args — it moved."

# MUST READ — the bug definition + the documented fix
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/prd_snapshot.md
  why: "Issue 5 (§h3.4): --create-config exposes an undocumented -c short flag.
        Suggested Fix: 'Drop the undocumented -c short flag' (this PRP) or 'add it
        to the PRD §11 table'. This PRP implements the DROP branch."
  section: "Minor Issues / Issue 5"

# REFERENCE — the parallel S3 PRP (disjoint edit region; explains the broken test module)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T1S3/PRP.md
  why: "S3 rewrites the #[cfg(test)] mod tests block (lib.rs ~866-920). It is
        mid-flight, so cargo test --lib currently fails E0412 in the TEST MODULE.
        THIS task edits build_cli_command (lib.rs ~213-220) — a DISJOINT region.
        Confirm no overlap; do not touch the test module (S3's exclusive scope)."

# REFERENCE — the PRD CLI spec (§11) — does not document -c
- file: PRD.md
  why: "§11 (CLI) is the canonical option table; it lists --create-config only as
        a removed-feature trap with NO short flag. The -c short was undocumented
        drift. This fix brings the code into agreement with §11."
  section: "11. CLI"

# REFERENCE — README CLI table (already omits -c AND --create-config)
- file: README.md
  why: "The 'Command Line Options' table (~lines 71-86) does NOT list --create-config
        or -c at all. So removing -c requires NO README change (grep-proven: zero
        -c/create-config matches in README). Item DOCS: 'none'."
  section: "Command Line Options"

# REFERENCE — empirical evidence (exact edit site, gate-independence, no -c refs)
- docfile: plan/001_b92a9b2b603f/bugfix/001_769d0d1edecf/P1M2T2S1/research/notes.md
  why: "Documents F1-F9: the exact .short('c') line + anchor, that select_command/
        ArgGroup use the long name (unaffected), that 'c' is used only by
        create-config (no collision), zero -c refs in src/, README omission, the
        current --help rendering, the parallel-S3 broken-test-module context, and
        the S3-independent validation gates (cargo build --lib clean now)."
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
    ├── main.rs         # post-S2 (committed) — DO NOT TOUCH
    ├── lib.rs          # <-- FILE TO EDIT (build_cli_command, lib.rs:213-220 only)
    │                    #     The #[cfg(test)] mod tests block (~866-920) is S3's scope.
    ├── error.rs        # QmkError — DO NOT TOUCH
    └── core.rs         # transport — DO NOT TOUCH
# No tests/ dir, no examples/, no benches/ (verified) — all tests live in lib.rs's #[cfg(test)] mod.
```

### Desired Codebase tree with files to be modified

```bash
src/
└── lib.rs   # MODIFIED ONLY at lib.rs:215 — DELETE the single line `                .short('c')`
             #   inside the Arg::new("create-config") chain of build_cli_command.
             #   All else (.long, .help, .action, arg ID) UNCHANGED.
             #   The #[cfg(test)] mod tests block is UNTOUCHED (S3's parallel scope).
# (main.rs, error.rs, core.rs, Cargo.toml, README.md, PRD.md unchanged)
```

> No new files. One file modified (`src/lib.rs`, one line). No new dependencies,
> no new tests, no new types, no doc changes.

### Known Gotchas of our codebase & Library Quirks

```rust
// CRITICAL (PARALLEL S3 — DON'T CHASE THE RED HERRING): `cargo test --lib`
//   currently FAILS with E0412 "cannot find type CliArgs" in the #[cfg(test)] mod
//   tests block. That is the P1.M2.T1.S3 task's in-flight state (it hasn't landed
//   its test rewrites yet) — NOT related to this change. This task edits NON-TEST
//   code (build_cli_command). The S3-scoped breakage does NOT affect:
//     - `cargo build --lib`  (VERIFIED clean now — compiles non-test lib).
//     - `cargo build`        (VERIFIED clean now — #[cfg(test)] is not compiled).
//     - `cargo run -- --help`(compiles main.rs [post-S2] + lib [post-S1]).
//   So MY gates are S3-independent. Do NOT run `cargo test --lib` as a gate for
//   this task and do NOT try to "fix" the test module — that is S3's job.

// CRITICAL (select_command / ArgGroup UNAFFECTED): removing .short('c') does NOT
//   change arg lookup. select_command uses matches.get_flag("create-config") —
//   the LONG arg-id STRING. The action ArgGroup lists arg IDs. clap resolves a
//   flag by its registered ID (here "create-config"), and the .long("create-config")
//   maps "--create-config" → id "create-config". The short was an ADDITIONAL alias
//   for the same id; removing it leaves the long form and the id fully wired.

// CRITICAL (DO NOT touch the #[cfg(test)] mod tests block): it is S3's EXCLUSIVE
//   parallel scope (lib.rs ~866-920). Editing it risks a merge conflict and is
//   out of scope. No test references `-c` anyway (grep-proven), so there is
//   nothing to change there for THIS task.

// NOTE ('c' is used ONLY by create-config): all .short() in lib.rs are 'i','p',
//   'u','a','v','l','c'. Removing 'c' frees it but nothing else claims it → no
//   collision. query-info/list-callbacks have NO short by design (diagnostic flags).

// NOTE (no rustfmt.toml / clippy.toml): default configs. Deleting one line from a
//   builder chain leaves the chain rustfmt-clean (each .method() call is on its
//   own line). Run `cargo fmt` then `cargo fmt --check` (exit 0).

// NOTE (README / PRD already agree): the README CLI table and PRD §11 table both
//   OMIT -c and --create-config. So removing -c requires NO doc edit. Item DOCS:
//   "none". Do not edit README.md or PRD.md.

// NOTE (moving target): P1.M2.T1.S3 edits lib.rs in parallel. LINE NUMBERS DRIFT
//   (esp. below line ~866). Anchor the edit by CONTENT (the unique
//   `Arg::new("create-config")` ... `.short('c')` ... `.long("create-config")`
//   block), NOT by line number. The create-config block is in build_cli_command
//   (lib.rs:157-240), well ABOVE S3's test-module region, so it is stable.
```

## Implementation Blueprint

### Data models and structure
No new types, structs, enums, functions, or constants. This subtask is a
**single-line deletion** from a clap arg-builder chain. No production logic change,
no new tests.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: EDIT src/lib.rs — delete the .short('c') line from the create-config arg
  - FIND: the Arg::new("create-config") block inside build_cli_command() (grep
          -n "create-config" src/lib.rs; the block is ~lib.rs:213-220, in the
          non-test region lib.rs:157-240).
  - DELETE exactly one line: `                .short('c')` (the line between
          `Arg::new("create-config")` and `.long("create-config")`).
  - KEEP: `.long("create-config")`, `.help("Create example configuration file
          (REMOVED)")`, `.action(ArgAction::SetTrue)`, and the arg ID
          "create-config".
  - DO NOT: touch any other arg, the ArgGroup, select_command, parse_matches,
          parse_cli_args, or the #[cfg(test)] mod tests block.

Task 2: VALIDATE (the S3-independent gates)
  - RUN: `cargo fmt`, then `cargo fmt --check` (exit 0).
  - RUN: `cargo build --lib`  -> MUST be zero warnings. (Primary gate; S3-independent.)
  - RUN: `cargo build`        -> MUST be zero warnings. (Full build; S3-independent
          because #[cfg(test)] isn't compiled by `cargo build`.)
  - RUN: `cargo run --quiet -- --help | grep -i create-config` -> MUST show
          `--create-config` with NO `-c` prefix (was `-c, --create-config`).
  - RUN: `cargo run --quiet -- --create-config; echo "exit=$?"` -> MUST surface the
          RemovedFeature message and exit non-zero (behavior preserved).
  - SANITY: `grep -n "\.short('c')" src/lib.rs` -> ZERO matches.
  - SANITY: `grep -n "create-config" src/lib.rs` -> the .long("create-config"),
          the select_command get_flag("create-config"), and the ArgGroup entry all
          STILL present (only the .short line is gone).
  - NOTE: `cargo test --lib` is CURRENTLY broken by the parallel S3 task (E0412 in
          the test module) — do NOT use it as a gate for this task. Once S3 lands,
          it will pass 26 tests with this change in place (no test references -c).
```

### Implementation Patterns & Key Details

```rust
// === THE EDIT (one-line deletion in a clap arg chain) ===
//   clap's Arg::new("id").short('x').long("name")... is a fluent builder. .short()
//   and .long() are INDEPENDENT aliases for the SAME arg id. Removing .short('c')
//   leaves the long form fully wired:
//
//     // BEFORE (lib.rs:213-220):
//     .arg(
//         Arg::new("create-config")
//             .short('c')              // <-- DELETE
//             .long("create-config")
//             .help("Create example configuration file (REMOVED)")
//             .action(ArgAction::SetTrue),
//     )
//
//     // AFTER:
//     .arg(
//         Arg::new("create-config")
//             .long("create-config")
//             .help("Create example configuration file (REMOVED)")
//             .action(ArgAction::SetTrue),
//     )


// === WHY select_command / ArgGroup ARE UNAFFECTED ===
//   select_command does `matches.get_flag("create-config")` — it looks up by the
//   arg ID ("create-config"), which is set by Arg::new("create-config") and mapped
//   from the LONG form by .long("create-config"). The short was an extra alias for
//   the same id; removing it doesn't change the id or the long→id mapping.
//   The ArgGroup::new("action").args([... "create-config" ...]) lists arg IDs too.
//   So clap's conflict detection and the RemovedFeature arm keep working unchanged.


// === WHY THE --help OUTPUT CHANGES (the user-visible deliverable) ===
//   clap renders an arg in --help as `-x, --long <DESC>` when a short exists, or
//   `    --long <DESC>` (indented, no short column) when it doesn't. BEFORE:
//     `  -c, --create-config            Create example configuration file (REMOVED)`
//   AFTER:
//     `      --create-config            Create example configuration file (REMOVED)`
//   That visible removal of `-c` IS the Issue-5 fix.


// === WHY NO DOC CHANGE IS NEEDED ===
//   README.md's CLI table omits BOTH -c and --create-config (grep-proven: zero
//   matches). PRD §11 likewise documents --create-config only as a removed-feature
//   trap with no short. So the docs already agree with the post-fix code. Item
//   DOCS: "none".
```

### Integration Points

```yaml
SOURCE FILES:
  - modify: "src/lib.rs ONLY — and ONLY the single .short('c') line at ~lib.rs:215
             inside build_cli_command's Arg::new('create-config') chain."

NO OTHER CHANGES:
  - select_command: "UNCHANGED — uses get_flag('create-config') (long-name id)."
  - ArgGroup:        "UNCHANGED — lists arg IDs, not short chars."
  - parse_matches:   "UNCHANGED."
  - tests:           "UNCHANGED — no test references -c (grep-proven); the
                      #[cfg(test)] block is the parallel S3 task's scope."
  - docs/deps:       "NONE — README/PRD already omit -c; no Cargo.toml change."

PUBLIC API SURFACE:
  - changes:  "(none in the library API — this is a CLI-surface/help-rendering change.)"
  - CLI:      "-c short alias for --create-config is REMOVED. --create-config long
               form is UNCHANGED (still surfaces RemovedFeature)."

SCOPE BOUNDARY:
  - ONLY the single .short('c') line in build_cli_command is deleted. Do NOT:
    * touch the #[cfg(test)] mod tests block (S3's parallel scope).
    * touch main.rs (S2's scope), error.rs, core.rs, Cargo.toml, README.md, PRD.md.
    * add shorts to query-info/list-callbacks or rearrange any other arg.
    * remove the --create-config long form or the RemovedFeature behavior.
```

## Validation Loop

### Level 1: Syntax & Style

```bash
# Format the edited file (default rustfmt; no rustfmt.toml in the repo).
cargo fmt

# Build the LIBRARY (non-test) — this is where the change lives. MUST be zero warnings.
cargo build --lib 2>&1 | tee /tmp/t2s1_build_lib.log
# Expected: "Finished `dev` profile ..." and NO "warning:"/"error" lines.
# (S3-INDEPENDENT: cargo build --lib does not compile the #[cfg(test)] harness, so
#   the parallel S3 test-module breakage does not affect this gate.)

# Full build (compiles main.rs too; does NOT compile #[cfg(test)]).
cargo build 2>&1 | tee /tmp/t2s1_build_full.log
# Expected: "Finished `dev` profile ..." zero warnings. (Also S3-independent.)

# Lint the lib.
cargo clippy --lib 2>&1 | tee /tmp/t2s1_clippy.log
# Expected: zero warnings/errors.
# NOTE: `cargo clippy --all-targets` / `cargo clippy --tests` will currently FAIL
#   with E0412 in the test module (S3 in flight) — NOT this task's concern. Use
#   `cargo clippy --lib` for this task's gate.

# Formatting check (CI-style gate).
cargo fmt --check
# Expected: exit code 0. If non-zero, re-run `cargo fmt`.
```

### Level 2: Unit Tests (the S3-coupled gate — NOT a blocker for this task)

```bash
# NOTE: `cargo test --lib` is CURRENTLY BROKEN by the parallel S3 task (E0412 in
# the test module: the tests still reference the deleted CliArgs). This is NOT
# caused by this task and is NOT this task's gate. Do NOT block on it.
cargo test --lib 2>&1 | tee /tmp/t2s1_test_lib.log
# Expected RIGHT NOW: fails to compile with E0412 in the #[cfg(test)] block
#   (S3's scope, not yours).
# Expected AFTER S3 LANDS: "test result: ok. 26 passed; 0 failed" — and this
#   task's change does NOT alter that (no test references -c; verified).

# Proof this task is test-agnostic: zero -c references anywhere in src/:
grep -rnE "\"-c\"|'-c'" src/ || echo "no -c references (good — change is test-agnostic)"
```

### Level 3: Integration Testing (the real gate for this task — CLI surface)

```bash
# THE PRIMARY GATE: --help no longer shows -c.
cargo run --quiet -- --help 2>&1 | grep -iE "create-config|^-c|  -c," || echo "(create-config not rendered — unexpected)"
# Expected BEFORE the fix: "  -c, --create-config            Create example configuration file (REMOVED)"
# Expected AFTER the fix:  "      --create-config            Create example configuration file (REMOVED)"
#   (no "-c" anywhere in the line — clap indents it since there's no short).
# Stronger check: assert -c does NOT appear anywhere in help:
cargo run --quiet -- --help 2>&1 | grep -E "^\s+-c," && echo "FAIL: -c still present" || echo "PASS: no -c short in help"

# BEHAVIOR PRESERVED: --create-config long form still surfaces RemovedFeature.
cargo run --quiet -- --create-config; echo "exit=$?"
# Expected: a stderr line about config-file removal being removed, exit code != 0.
#   (clap exits 2 on the RemovedFeature path? No — RemovedFeature is returned from
#    run/parse, surfaced as a normal error exit. The exact exit code is whatever
#    main.rs does with Err(QmkError::RemovedFeature). The KEY assertion: it does
#    NOT succeed silently and does NOT panic.)

# Negative control: -c no longer parsed as the create-config alias (it's now an
# unknown short → clap error exit 2). This proves the removal took effect at the
# parser level, not just the help renderer.
cargo run --quiet -- -c 2>&1 | grep -iE "unexpected argument|invalid" && echo "PASS: -c now rejected" || echo "(check -c handling)"
# Expected: clap rejects `-c` as an unexpected/unknown argument (exit 2).
```

### Level 4: Creative & Domain-Specific Validation

```bash
# (1) The .short('c') line is GONE, but everything else create-config-related stays:
grep -n "\.short('c')" src/lib.rs && echo "FAIL: .short('c') still present" || echo "PASS: .short('c') removed"
grep -n 'Arg::new("create-config")\|\.long("create-config")\|get_flag("create-config")' src/lib.rs
# Expected: the arg registration + .long + the select_command get_flag ALL still present.

# (2) 'c' is not claimed by any other arg (no collision introduced by freeing it):
grep -nE "\.short\(" src/lib.rs
# Expected: 'i','p','u','a','v','l' — NO 'c'. (query-info/list-callbacks have no short.)

# (3) README and PRD §11 already omit -c (no doc drift remains after the fix):
grep -rnE "\-c\b|create-config" README.md PRD.md || echo "no -c/create-config in docs (good — docs already agree)"

# (4) The #[cfg(test)] mod tests block is UNTOUCHED (S3's parallel scope — don't
#     introduce a merge conflict):
git diff --stat src/lib.rs   # (if you want to confirm the diff is tiny — 1 line)
# Expected: a 1-line deletion in build_cli_command; nothing in the test module.
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1: `cargo build --lib` → zero warnings (primary, S3-independent gate).
- [ ] Level 1: `cargo build` (full) → zero warnings.
- [ ] Level 1: `cargo clippy --lib` → zero warnings.
- [ ] Level 1: `cargo fmt --check` → exit 0.
- [ ] Level 3: `--help` no longer renders `-c` (only `--create-config`).
- [ ] Level 3: `-c` is now rejected by clap as an unknown argument.

### Feature Validation
- [ ] The `.short('c')` line is deleted from the create-config arg chain.
- [ ] `.long("create-config")`, `.help(...)`, `.action(ArgAction::SetTrue)`, and the
      arg ID `"create-config"` are all UNCHANGED.
- [ ] `--create-config` still surfaces `RemovedFeature` (behavior preserved).
- [ ] `select_command`'s `get_flag("create-config")` still resolves (long-name id).

### Code Quality Validation
- [ ] No other arg touched; no short added/removed elsewhere.
- [ ] The `#[cfg(test)] mod tests` block is UNTOUCHED (S3's parallel scope).
- [ ] No file other than `src/lib.rs` modified.

### Documentation & Deployment
- [ ] No README/PRD/Cargo.toml change (item DOCS: "none"; README already omits -c).
- [ ] No new environment variables or config.

---

## Anti-Patterns to Avoid

- ❌ Don't remove the `.long("create-config")` form or the `RemovedFeature`
      behavior — only the `.short('c')` alias goes. The long form is the documented
      (PRD §11) removed-feature trap and stays fully functional.
- ❌ Don't touch the `#[cfg(test)] mod tests` block. It is the parallel P1.M2.T1.S3
      task's exclusive scope; editing it risks a merge conflict. No test references
      `-c` anyway (grep-proven).
- ❌ Don't chase the `cargo test --lib` E0412 failure — it's caused by S3's
      in-flight state (test module still references the deleted `CliArgs`), NOT by
      this change. This task's gates (`cargo build --lib`, `cargo run -- --help`)
      are S3-independent and clean.
- ❌ Don't anchor the edit by line number — P1.M2.T1.S3 edits lib.rs in parallel
      (below ~line 866), so line numbers drift. Anchor by the unique CONTENT
      `Arg::new("create-config")` … `.short('c')` … `.long("create-config")` block,
      which sits in `build_cli_command` (stable region, lib.rs:157-240).
- ❌ Don't touch `select_command`, `parse_matches`, `parse_cli_args`, or the
      `action` ArgGroup — they key on the long-name arg ID `"create-config"` and are
      unaffected by removing the short.
- ❌ Don't add short flags to `--query-info`/`--list-callbacks` "while you're in
      there" — they are deliberately long-only diagnostic flags, and that's out of
      scope for Issue 5.
- ❌ Don't edit `README.md` or `PRD.md` to "document" the change — both already omit
      `-c` and `--create-config`, so the docs already agree with the post-fix code
      (item DOCS: "none").
- ❌ Don't run `cargo clippy --all-targets`/`--tests` as a gate — they compile the
      `#[cfg(test)]` block and will fail with S3's E0412. Use `cargo clippy --lib`.
- ❌ Don't conflate "the `-c` short is gone from `--help`" (the deliverable) with
      "`--create-config` is gone" (WRONG — the long form stays). Verify BOTH: `-c`
      absent AND `--create-config` present+functional.

---

**Confidence Score: 10/10** for one-pass implementation success. The deliverable
is a **single-line deletion** with its exact 4-line content anchor quoted verbatim
(unique in the file), and the change is provably isolated: `select_command` and the
`ArgGroup` key on the long-name arg id (unaffected), zero `-c` references exist in
`src/` (no test breaks), the README/PRD already omit `-c` (no doc change), and `'c'`
is claimed by no other arg (no collision). The only subtlety is the
parallel-execution context — the `#[cfg(test)]` module is currently broken by the
in-flight P1.M2.T1.S3 task, so `cargo test --lib` is red right now — but that
breakage is in a DISJOINT region of lib.rs (the test block, ~lines 866-920) versus
this task's edit (`build_cli_command`, line 215), and this task's gates
(`cargo build --lib`, `cargo run -- --help`) are S3-independent and verified clean.
An implementer who deletes the one line and runs the Level-1/Level-3 gates produces
a CLI whose `--help` no longer shows `-c` while `--create-config` keeps surfacing
`RemovedFeature`, fully resolving Issue 5.