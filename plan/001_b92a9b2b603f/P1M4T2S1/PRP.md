name: "P1.M4.T2.S1 — Remove unused deps (toml/serde/dirs) and bump version to 0.3.0"
description: "Cargo.toml-only change. Bump `version` 0.2.1 → 0.3.0 and remove three dead dependencies (`toml`, `serde`, `dirs`) whose config-file consumer was deleted in commit 64d5f74. Then refresh Cargo.lock and verify build/test/release all stay green. NO source edits; NO README edits (P1.M4.T3.S1 owns version refs there); NO git tag (release step)."

---

## Goal

**Feature Goal**: Ship a leaner `Cargo.toml` containing only the two genuinely-used
crates (`clap`, `hidapi`), at version `0.3.0`, with a refreshed `Cargo.lock` that
no longer carries the removed deps or their transitive tail. The crate must build,
test, and release-build identically to before.

**Deliverable**: Two modified files, nothing else:
1. **`Cargo.toml`** — `version = "0.3.0"`; `[dependencies]` reduced to `clap` +
   `hidapi` (the `toml`, `dirs`, and `serde` lines deleted).
2. **`Cargo.lock`** — refreshed (the four direct entries `dirs`, `serde`,
   `serde_derive`, `toml` and their unique transitive deps — `dirs-sys`,
   `toml_edit`, `toml_datetime`, `winnow`, `serde_spanned` — are pruned).

**Success Definition**: `cargo build`, `cargo test` (all 22 unit tests pass),
`cargo build --release`, and a post-edit re-grep confirming zero `toml`/`serde`/`dirs`
references in `src/` all succeed. `git status` shows ONLY `Cargo.toml` and
`Cargo.lock` as modified.

## User Persona (if applicable)

**Target User**: Downstream consumer (`qmkonnect`) and future maintainers.
`qmk_notifier` is never published to crates.io (PRD §13); `qmkonnect` consumes it
as a git-tagged dependency. This item is the Cargo-manifest half of preparing the
`v0.3.0` release that the PRD §13 versioning contract targets.

**Use Case**: After this item lands (and the `v0.3.0` git tag is later cut as a
separate release step), `qmkonnect` can update its pin from `tag = "v0.2.1"` to
`tag = "v0.3.0"` and pull in the leaner, typed-command-capable transport crate.

**Pain Points Addressed**: Three dead dependencies bloat the dependency tree,
slower cold builds, and a larger `Cargo.lock` audit surface — all for crates the
code no longer touches. PRD §2 explicitly says toml/serde "may be dropped."

## Why

- **PRD §2 (Repository Layout)**: "`toml`/`serde` are currently listed but unused
  after config-file support was removed — they may be dropped." `dirs` is also dead
  (it was the config-file path resolver). This item completes that cleanup.
- **PRD §13 (Versioning)**: the spec pins the release to `tag = "v0.3.0"`. The
  version field in `Cargo.toml` must read `0.3.0` for the git tag to be coherent.
- **Architecture note** (`plan/001_b92a9b2b603f/architecture/system_context.md`
  §"Dependency note"): verified by grep that none of the three are referenced in
  `src/`; cites commit `64d5f74` as where config-file support was removed.
- **Audit surface**: removing these drops ~8–12 transitive packages from the
  lockfile (toml_edit, winnow, serde_spanned, dirs-sys, …), shrinking the
  supply-chain footprint.

## What

A single manifest edit and a lockfile refresh:

1. In `Cargo.toml`, change `version = "0.2.1"` → `version = "0.3.0"`.
2. In `Cargo.toml` `[dependencies]`, delete exactly these three lines:
   - `toml = "0.8.10"`
   - `dirs = "5.0.1"`
   - `serde = { version = "1.0", features = ["derive"] }`
3. Refresh `Cargo.lock` so it no longer resolves the removed crates.
4. Verify build + tests + release build + a re-grep all stay green.

### Success Criteria

- [ ] `Cargo.toml` `version = "0.3.0"` and `[dependencies]` lists ONLY `clap` +
      `hidapi`.
- [ ] `cargo build` succeeds with **0 warnings**.
- [ ] `cargo test` reports all unit tests passing (baseline ~22; unchanged since
      no source is edited).
- [ ] `cargo build --release` succeeds.
- [ ] `cargo update` runs clean (per work-item contract step d).
- [ ] Post-edit re-grep: `grep -rn -i "toml\|serde\|dirs" src/` → **exit 1** (no
      matches) — confirms nothing newly broke.
- [ ] `git status --porcelain` shows ONLY `M Cargo.toml` and `M Cargo.lock`.
- [ ] `grep` confirms `dirs`/`serde`/`serde_derive`/`toml`/`toml_edit`/
      `toml_datetime`/`winnow`/`serde_spanned`/`dirs-sys` are ABSENT from the new
      `Cargo.lock`.

## All Needed Context

### Context Completeness Check
> _"If someone knew nothing about this codebase, would they have everything needed
> to implement this successfully?"_ — **Yes.** The current `Cargo.toml` is quoted
> verbatim below (11 lines, the entire file). The exact three lines to delete and
> the single field to change are spelled out. The usage-safety claim (zero source
> references) was verified live during research (`research/notes.md` F1) and the
  implementer re-verifies it as step 1. The lockfile-refresh mechanics (modern
  cargo auto-prunes on build; `cargo update` additionally bumps) are documented in
  F4. The parallel boundary (P1.M4.T1.S1 owns lib.rs/main.rs/README; this item owns
  Cargo.toml/Cargo.lock) is documented in F5. No source-code understanding is
  required — this is a manifest-only edit.

### Documentation & References

```yaml
# MUST READ — the ONLY source file edited by this item (11 lines, entire file)
- file: Cargo.toml
  why: "Holds version = \"0.2.1\" (line 3) and the [dependencies] table with the
        three dead deps. This is the entire edit surface."
  pattern: "Standard Cargo manifest: [package] (name/version/edition) then
            [dependencies]. No [dev-dependencies], no [features], no [lib]/[[bin]]
            sections (they use defaults: lib crate qmk_notifier + bin qmk_notifier
            from src/main.rs)."
  gotcha: "DO NOT touch the clap or hidapi lines. DO NOT add/modify [features] or
           edition. The ONLY changes are: line 3 version, and deleting the three
           dep lines."

# MUST READ — research notes (verification proof + lockfile mechanics)
- docfile: plan/001_b92a9b2b603f/P1M4T2S1/research/notes.md
  why: "F1 = the usage grep (exit 1, zero matches — SAFE to remove); F2 = version-
        string inventory (0.2.1 lives ONLY in Cargo.toml:3, so one edit suffices);
        F3 = lockfile impact (67 packages → ~8-12 pruned); F4 = cargo build auto-
        prunes vs cargo update also bumps; F5 = parallel boundary with P1.M4.T1.S1
        (no Cargo.toml conflict); F6 = scope boundaries (no tag cut, no README edit,
        no source patch)."

# REFERENCE — architecture note that asserts the deps are dead
- docfile: plan/001_b92a9b2b603f/architecture/system_context.md
  why: "§'Dependency note' independently states toml/serde/dirs are unreferenced in
        src/ and cites commit 64d5f74 (config-file support removal). Cross-checks F1."
  section: "Dependency note"

# REFERENCE — external_deps.md table (corroborates the drop decision)
- docfile: plan/001_b92a9b2b603f/architecture/external_deps.md
  why: "§'Cargo.toml dependency state' table marks toml/dirs/serde as ❌ unused / Drop."
  section: "Cargo.toml dependency state"

# REFERENCE — PRD sections this item implements
- file: PRD.md
  why: "§2 (Repository Layout) says toml/serde may be dropped; §13 (Versioning) pins
        the release to tag = \"v0.3.0\". Both are quoted verbatim in the work item."
  section: "2. Repository Layout & Deliverables", "13. Versioning, Release & Cross-Repo Links"

# REFERENCE — parallel sibling item (owns lib.rs/main.rs/README; NOT Cargo.toml)
- docfile: plan/001_b92a9b2b603f/P1M4T1S1/PRP.md
  why: "P1.M4.T1.S1 is implemented concurrently. Its PRP's Integration Points +
        Known Gotchas (F6) state explicitly 'no Cargo.toml change'. Therefore this
        item owns Cargo.toml/Cargo.lock with zero merge-conflict risk."
  section: "Integration Points", "Known Gotchas of our codebase & Library Quirks"
```

### Current Codebase tree

```bash
.
├── Cargo.toml          # name="qmk_notifier", version="0.2.1", edition="2021".
│                       # [dependencies]: clap 4.5.31, hidapi 2.4.1, toml 0.8.10,
│                       # dirs 5.0.1, serde 1.0 (derive). <-- THIS ITEM EDITS THIS FILE.
├── Cargo.lock          # 67 packages; carries dirs/serde/serde_derive/toml + transitive.
│                       # <-- THIS ITEM REFRESHES THIS FILE.
├── README.md           # NOT edited here (P1.M4.T3.S1 owns version refs).
├── PRD.md              # READ-ONLY.
└── src/
    ├── lib.rs          # NOT edited (P1.M4.T1.S1 edits it concurrently).
    ├── core.rs         # NOT edited.
    ├── error.rs        # NOT edited.
    └── main.rs         # NOT edited (P1.M4.T1.S1 edits it concurrently).
```

### Desired Codebase tree with files to be modified

```bash
Cargo.toml   # MODIFIED: version "0.2.1" -> "0.3.0"; remove toml/dirs/serde lines.
Cargo.lock   # AUTO-REFRESHED by cargo build/test/update (prune removed deps).
# (No other files touched. No new files.)
```

### Known Gotchas of our codebase & Library Quirks

```toml
# CRITICAL (RE-VERIFY THE GREP BEFORE EDITING): the research-time grep returned
#   exit 1 (zero matches) for toml/serde/dirs in src/. BUT P1.M4.T1.S1 is editing
#   src/lib.rs concurrently. Re-run the grep as step 1 of implementation. P1.M4.T1.S1
#   adds clap ArgGroup/Arg code only — it does NOT reintroduce toml/serde/dirs — so
#   the grep will still be exit 1. If (unexpectedly) it is NOT, STOP, restore, and
#   investigate before removing any dep. (research/notes.md F1, F5.)

# CRITICAL (DO NOT cut the git tag here): the work item is the Cargo.toml version
#   FIELD + lockfile refresh. Cutting `git tag v0.3.0` is a separate release step.
#   qmkonnect updating its own `tag = "v0.2.1"` pin to `v0.3.0"` is the downstream
#   consumer's job — NOT this item's. (research/notes.md F6.)

# CRITICAL (DO NOT edit README.md): version references in README are owned by
#   P1.M4.T3.S1. The work item's DOCS note is explicit: "[Mode A] ... No separate
#   docs file needed — the README version references are handled by P1.M4.T3.S1."
#   (research/notes.md F6.)

# CRITICAL (if build/test fails, RESTORE the dep — do NOT patch source): the
#   work-item contract step (c) says: "If any test or code references these crates
#   (unexpected), restore the dep and investigate." The correct response to a
#   post-removal failure is to put the line back, NOT to edit src/*.rs to force the
#   dep out. (This branch is not expected to fire — see F1.) (research/notes.md F6.)

# GOTCHA (cargo build AUTO-PRUNES the lockfile): with cargo 1.92.0, after editing
#   Cargo.toml a plain `cargo build` regenerates Cargo.lock and drops the now-unused
#   dirs/serde/serde_derive/toml + transitive. So `cargo build` alone achieves the
#   lockfile goal. `cargo update` (step d of the contract) additionally bumps every
#   dep to latest semver-compatible — usually fine for release prep, but review
#   `git diff Cargo.lock` afterward to confirm changes are semver-minor/patch only.
#   (research/notes.md F4.)

# GOTCHA (serde is NOT pulled by clap): clap lists serde as an OPTIONAL dep in its
#   Cargo.lock entry, but optional deps only load when the matching cargo feature is
#   enabled. This crate uses clap with DEFAULT features (no `serde` feature), so clap
#   does NOT pull serde. serde/serde_derive are in the lockfile today ONLY because of
#   our direct `serde = { ..., features = ["derive"] }` line. Removing it removes
#   them. (research/notes.md F1.)

# GOTCHA (proc-macro2/quote/syn likely STAY): serde_derive pulls proc-macro2/quote/syn,
#   but clap's build also uses proc-macro2/quote/syn (via clap_derive / clap_builder).
#   So after removal those three may remain in the lockfile because clap still needs
#   them. That is EXPECTED and correct — do not try to force-remove them.

# GOTCHA (version string has exactly ONE copy): grep confirmed `0.2.1` appears only
#   at Cargo.toml:3 in this repo's source/docs (other matches are stale
#   .pi-subagents artifacts or the downstream qmkonnect repo). So a single line edit
#   is the complete version bump. (research/notes.md F2.)
```

## Implementation Blueprint

### Data models and structure

Not applicable — no data models, no types, no code. This is a pure manifest edit.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: RE-VERIFY the usage grep (defensive — src/ may have changed during parallel work)
  - RUN: `grep -rn -i "toml\|serde\|dirs" src/ ; echo "exit=$?"`
  - EXPECT: exit code 1 (NO matches). This confirms the three deps are still dead.
  - IF exit 0 (matches found): STOP. Do NOT remove any dep. Restore/investigate per
    the work-item contract. P1.M4.T1.S1's lib.rs edits should NOT reintroduce them,
    but verify rather than assume. (research/notes.md F1, F5.)

Task 2: EDIT Cargo.toml — bump version
  - CHANGE line 3: `version = "0.2.1"` -> `version = "0.3.0"`
  - PRESERVE name, edition, and all formatting.

Task 3: EDIT Cargo.toml — remove the three dead dep lines from [dependencies]
  - DELETE: `toml = "0.8.10"`
  - DELETE: `dirs = "5.0.1"`
  - DELETE: `serde = { version = "1.0", features = ["derive"] }`
  - PRESERVE: `clap = "4.5.31"` and `hidapi = "2.4.1"` (both actively used).
  - RESULT: [dependencies] contains exactly two lines (clap, hidapi). Ensure no
    trailing blank-line churn beyond what the deletions cause; keep the file ending
    consistent with the original (the original ends with a single trailing newline
    after the serde line).

Task 4: REFRESH the lockfile (auto-prune via build + explicit update)
  - RUN: `cargo build`   # regenerates Cargo.lock, prunes removed deps + transitive.
  - EXPECT: "Compiling ..." (fewer crates than before) then "Finished `dev` profile"
            with ZERO warnings. If a compile error references toml/serde/dirs,
            RESTORE that dep (do NOT patch src/) and investigate per contract step c.
  - RUN: `cargo test`    # contract step (c): verify nothing breaks. ~22 tests pass.
  - EXPECT: "test result: ok. <N> passed; 0 failed; 0 ignored; ..." with N >= 22
            (the count is unchanged from baseline because no source was edited).
  - RUN: `cargo update`  # contract step (d): explicit lockfile refresh.
  - EXPECT: exits 0. Then `git diff Cargo.lock` to review: dirs/serde/serde_derive/
            toml/toml_edit/toml_datetime/winnow/serde_spanned/dirs-sys GONE; any
            remaining changes are semver-minor/patch bumps of kept crates (e.g. clap
            4.5.47 -> 4.5.x). (research/notes.md F4.)

Task 5: VERIFY release build (contract step e)
  - RUN: `cargo build --release`
  - EXPECT: "Finished `release` profile [optimized]" with ZERO warnings.

Task 6: FINAL VERIFICATION (grep + git status + lockfile audit)
  - RUN: `grep -rn -i "toml\|serde\|dirs" src/ ; echo "exit=$?"` -> expect exit 1.
  - RUN: `git status --porcelain` -> expect ONLY:
         ` M Cargo.toml`
         ` M Cargo.lock`
         (NOTHING else. If src/*.rs or README.md appear modified, that is P1.M4.T1.S1's
          parallel work — this item must not touch them.)
  - RUN: `grep -cE '^name = "(dirs|dirs-sys|serde|serde_derive|toml|toml_edit|toml_datetime|winnow|serde_spanmed|serde_spanned)"' Cargo.lock`
         -> expect 0 (none of the removed dep family remains).
  - RUN: `grep -nE '^version = "0.3.0"' Cargo.toml` -> expect exactly 1 match (line 3).
```

### Implementation Patterns & Key Details

```toml
# ===== Cargo.toml — BEFORE (verbatim, the entire file) =====
[package]
name = "qmk_notifier"
version = "0.2.1"
edition = "2021"

[dependencies]
clap = "4.5.31"
hidapi = "2.4.1"
toml = "0.8.10"
dirs = "5.0.1"
serde = { version = "1.0", features = ["derive"] }

# ===== Cargo.toml — AFTER this item =====
[package]
name = "qmk_notifier"
version = "0.3.0"
edition = "2021"

[dependencies]
clap = "4.5.31"
hidapi = "2.4.1"
```

> The diff is exactly: one version-number change (`0.2.1` → `0.3.0`) and three
> deleted dependency lines. Nothing else in `Cargo.toml` changes — no `[features]`,
> no `[lib]`/`[[bin]]`, no edition, no metadata. Keep the file ending with a single
> trailing newline as in the original.

### Integration Points

```yaml
SOURCE FILES:
  - modify: "Cargo.toml (line 3 version; delete 3 dep lines)."
  - modify: "Cargo.lock (auto-regenerated by cargo build/test/update)."

PUBLIC API SURFACE:
  - unchanged: "Everything. No src/*.rs edit. The crate version field changes
                0.2.1 -> 0.3.0, but that is metadata, not an API surface change.
                (The run() return-type change to CommandResponse is P1.M3.T3.S1,
                already done — not this item.)"

DEPENDENCIES:
  - removes: ["toml 0.8.10 (direct)", "dirs 5.0.1 (direct)",
              "serde 1.0 + derive (direct)",
              "serde_derive, dirs-sys, toml_edit, toml_datetime, winnow, serde_spanned (transitive)"]
  - keeps:   ["clap 4.5.31 (used in src/lib.rs)", "hidapi 2.4.1 (used in src/core.rs)"]
  - net lockfile: "~67 packages -> ~55-59 packages (exact count depends on what clap/
                   hidapi still share; the precise number is not a success criterion)."

CONFIG / DATABASE / ROUTES:
  - none.

SCOPE BOUNDARY (do NOT implement now):
  - Do NOT cut the `v0.3.0` git tag (release step, downstream of this item).
  - Do NOT edit README.md (P1.M4.T3.S1 owns version references there).
  - Do NOT touch src/lib.rs, src/core.rs, src/error.rs, src/main.rs
    (P1.M4.T1.S1 owns lib.rs/main.rs; core.rs/error.rs are stable). If a post-removal
    build/test fails, RESTORE the offending dep per contract step (c) — do NOT patch
    source to force the removal.
  - Do NOT modify PRD.md, any tasks.json, prd_snapshot.md, or .gitignore.

PARALLEL BOUNDARY (P1.M4.T1.S1):
  - P1.M4.T1.S1 edits src/lib.rs, src/main.rs, README.md ONLY. Its PRP explicitly
    states "no Cargo.toml change" (its research F6). Therefore this item owns
    Cargo.toml + Cargo.lock with zero merge-conflict risk. The two items' edit sets
    are disjoint.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Cargo IS the linter/compiler for a manifest change. There is no rustfmt/ruff/mypy
# equivalent for Cargo.toml. The build below is the syntax check.
cargo build 2>&1 | tee /tmp/build.log
# Expected: "Compiling qmk_notifier v0.3.0 ..." then "Finished `dev` profile ..."
# with ZERO "warning:" lines. Fewer crates compiled than before (toml/serde/dirs
# and their tail are gone). A compile ERROR mentioning toml/serde/dirs means the
# grep (Task 1) was wrong or the codebase changed — restore the dep, do NOT patch src/.

# (No clippy/fmt action here: no source is edited. clippy/fmt are P1.M4.T1.S1's
# concern for the lib.rs/main.rs it edits.)
```

### Level 2: Unit Tests (Component Validation)

```bash
# Full test suite — must be unchanged from baseline (~22 tests pass).
cargo test 2>&1 | tail -8
# Expected: "test result: ok. <N> passed; 0 failed; 0 ignored; ..." with N >= 22.
# This item changes no source, so the test COUNT is identical to the pre-edit run.
# If ANY test fails: restore the dep it implicitly needed (per contract step c) and
# investigate — do NOT edit src/ to make a test pass without the dep.
```

### Level 3: Integration Testing (System Validation)

```bash
# Release build (contract step e) — proves the optimized artifact still links.
cargo build --release 2>&1 | tail -3
# Expected: "Finished `release` profile [optimized] target(s)" with no warnings.

# Binary still runs and reports the new version.
cargo run --release -- --version 2>&1
# Expected: prints "qmk_notifier 0.3.0" (clap .version() is set in lib.rs's
# Command builder; NOTE P1.M4.T1.S1 may change the .version() string literal —
# the important check is that the binary EXECUTES without a dependency/link error,
# not the exact version string text, which is a separate item's concern).

# Lockfile pruned as expected.
grep -cE '^name = "(dirs|dirs-sys|serde|serde_derive|toml|toml_edit|toml_datetime|winnow|serde_spanned)"' Cargo.lock
# Expected: 0 (the entire removed-dep family is gone from the lockfile).
```

### Level 4: Creative & Domain-Specific Validation

```bash
# Confirm the ONLY version field is 0.3.0 and there is exactly one copy.
grep -nE 'version = "0\.3\.0"' Cargo.toml
# Expected: exactly ONE match (line 3).

# Confirm no stale 0.2.1 remains in Cargo.toml.
grep -nE '0\.2\.1' Cargo.toml ; echo "exit=$?"
# Expected: exit 1 (no matches).

# Confirm [dependencies] is lean (exactly clap + hidapi).
sed -n '/^\[dependencies\]/,/^\[/p' Cargo.toml
# Expected:
#   [dependencies]
#   clap = "4.5.31"
#   hidapi = "2.4.1"

# Confirm the removal did not regress source references (re-run the safety grep).
grep -rn -i "toml\|serde\|dirs" src/ ; echo "exit=$?"
# Expected: exit 1 (no matches — unchanged from Task 1's pre-edit check).

# Confirm git status is exactly two files.
git status --porcelain
# Expected:
#    M Cargo.toml
#    M Cargo.lock
# (If src/lib.rs, src/main.rs, or README.md also appear modified, that is
#  P1.M4.T1.S1's concurrent work — leave those alone; this item did not touch them.)

# Full crate final gate.
cargo test 2>&1 | tail -3
# Expected: "test result: ok. <N> passed; 0 failed; ...".
```

## Final Validation Checklist

### Technical Validation
- [ ] Task 1 re-verification: `grep -rn -i "toml\|serde\|dirs" src/` → exit 1.
- [ ] `cargo build` → 0 warnings.
- [ ] `cargo test` → all unit tests pass (count unchanged from baseline).
- [ ] `cargo update` → exit 0; `git diff Cargo.lock` reviewed (pruning + semver bumps only).
- [ ] `cargo build --release` → 0 warnings.

### Feature Validation
- [ ] `Cargo.toml` line 3 reads `version = "0.3.0"`.
- [ ] `Cargo.toml` `[dependencies]` lists ONLY `clap` and `hidapi`.
- [ ] `Cargo.lock` no longer resolves `dirs`, `dirs-sys`, `serde`, `serde_derive`,
      `toml`, `toml_edit`, `toml_datetime`, `winnow`, `serde_spanned`.
- [ ] `cargo run --release -- --version` executes without a link/dep error.
- [ ] Post-edit re-grep: `grep -rn -i "toml\|serde\|dirs" src/` → exit 1.

### Code Quality Validation
- [ ] `git status --porcelain` shows ONLY `M Cargo.toml` and `M Cargo.lock`.
- [ ] No `src/*.rs`, `error.rs`, `README.md`, `PRD.md`, or `tasks.json` modified.
- [ ] No `[features]`/`[lib]`/`[[bin]]`/`edition`/`metadata` churn in Cargo.toml.
- [ ] No git tag `v0.3.0` created by this item (that is a separate release step).

### Documentation & Deployment
- [ ] No README.md edit (P1.M4.T3.S1 owns version references — work-item DOCS note).
- [ ] No new docs file (work-item DOCS: "[Mode A] ... No separate docs file needed").
- [ ] No `.gitignore` change.

---

## Anti-Patterns to Avoid

- ❌ Don't skip the Task 1 re-verification grep just because research said it's clean —
  P1.M4.T1.S1 is editing `src/lib.rs` concurrently; verify at implementation time.
- ❌ Don't patch `src/*.rs` to force a dep out if the build/test breaks — restore the
  dep and investigate (contract step c). The grep says this won't happen, but if it
  does, the contract is explicit.
- ❌ Don't run `cargo update` blindly and commit the result without `git diff
  Cargo.lock` review — it bumps ALL deps semver, not just prunes (research F4).
- ❌ Don't edit README.md's version references — that is P1.M4.T3.S1's scope.
- ❌ Don't cut the `v0.3.0` git tag in this item — tagging is a downstream release step.
- ❌ Don't remove `clap` or `hidapi` — both are actively used (lib.rs / core.rs).
- ❌ Don't add `[features]` or change `edition` while in the file — make ONLY the two
  intended changes (version + three deletions).