# Research Notes — P1.M4.T2.S1 (Remove unused deps, bump to 0.3.0)

This is a **Cargo.toml-only change** plus a Cargo.lock refresh. No source files
are edited. The research goal was to (a) confirm the three deps are truly unused,
(b) confirm the version string lives only in Cargo.toml, (c) map the lockfile
impact, and (d) confirm the parallel-work boundary.

---

## F0 — Current `Cargo.toml` (verbatim, the single file this item edits)

```toml
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
```

Target after this item:
```toml
[package]
name = "qmk_notifier"
version = "0.3.0"
edition = "2021"

[dependencies]
clap = "4.5.31"
hidapi = "2.4.1"
```

---

## F1 — Usage grep (THE critical safety check)

Command: `grep -rn -i "toml\|serde\|dirs::\|use dirs\|extern crate" src/`

**Result: exit code 1 (NO matches).** Confirmed live during this research session.
None of `toml`, `serde`, `serde_derive`, or `dirs` is referenced in `src/lib.rs`,
`src/core.rs`, `src/error.rs`, or `src/main.rs`. The architecture note
(`system_context.md` §"Dependency note") independently asserts the same, and cites
commit `64d5f74` as where config-file support (the only consumer of toml/serde/dirs)
was removed.

**Corollary (serde transitive concern, RESOLVED):** `clap` lists `serde` as an
*optional* dependency in its Cargo.lock entry (line 245), but optional deps are
only pulled when the matching cargo *feature* is enabled. This crate uses clap
with **default features** (no `serde` feature) — so clap does NOT pull serde.
serde/serde_derive are in the lockfile today *solely* because of our direct
`serde = { ..., features = ["derive"] }` dep. Removing the direct dep therefore
removes serde/serde_derive from the lockfile. Same for toml (which itself pulls
serde + toml_edit + toml_datetime + winnow + serde_spanned) and dirs (standalone,
no serde).

**Defensive note for the implementer:** because P1.M4.T1.S1 is editing `src/lib.rs`
concurrently, re-run the grep at implementation time (step 1 of the PRP). P1.M4.T1.S1
adds clap `ArgGroup`/`Arg` code — it does NOT add any toml/serde/dirs usage — so the
grep will still return exit 1.

---

## F2 — Version-string inventory (where `0.2.1` lives)

`grep -rn "0\.2\.1" --include='*.md' --include='*.toml' --include='*.rs'` excluding
`plan/` and `Cargo.lock`:

- `Cargo.toml:3:version = "0.2.1"` ← **the only in-repo source reference.**
- All other matches are under `.pi-subagents/artifacts/` (stale research output,
  not source of truth) or describe the *downstream* qmkonnect pin (`tag = "v0.2.1"`),
  which is a DIFFERENT repo and explicitly NOT this item's concern (the work item
  says qmkonnect updates its own tag after v0.3.0 is cut).

**Conclusion:** the version bump is a single-line edit. There is no second copy of
the version string in this repo's source/docs to keep in sync. README.md version
references (if any) are owned by **P1.M4.T3.S1** (the work item's DOCS note
explicitly defers them).

---

## F3 — Cargo.lock impact (what gets pruned)

Current lockfile: **67 packages** (`grep -c '^name = ' Cargo.lock`).

Direct deps being removed and their lockfile entries:
- `dirs` 5.0.1 (Cargo.lock:111) — standalone, pulls `dirs-sys`. Both pruned.
- `serde` 1.0.219 (Cargo.lock:270) + `serde_derive` 1.0.219 (Cargo.lock:279) +
  `serde_derive`'s proc-macro helpers (`proc-macro2`, `quote`, `syn` are ALSO used
  by clap's builder macros, so those likely STAY). Only serde/serde_derive + any
  uniquely-serde deps drop.
- `toml` 0.8.23 (Cargo.lock:342) + its transitive tail: `toml_edit`, `toml_datetime`,
  `winnow`, `serde_spanned`, `indexmap` (if not used elsewhere), `equator`/`hashbrown`
  (toml_edit's). These all drop.

**Net:** lockfile shrinks by roughly 8–12 packages. The exact count depends on what
clap/hidapi share, but the precise number is not important — what matters is that
the four DIRECT entries (dirs, serde, serde_derive, toml) are gone and `cargo build`
succeeds.

---

## F4 — Lockfile refresh mechanics (cargo build vs cargo update)

**Modern cargo (this repo: 1.92.0) auto-prunes on build.** When you remove a dep
from `Cargo.toml` and run `cargo build` (or `cargo test`), cargo regenerates
`Cargo.lock` and drops the now-unused entries. So after the Cargo.toml edit, a
plain `cargo build` already achieves "the lockfile drops the removed deps and their
transitive deps."

`cargo update` (no args) does MORE: it bumps EVERY dep to the latest
semver-compatible version in addition to pruning. For a release-prep task this is
usually fine (latest patches), and the work-item contract lists it as step (d).
**Recommended order (matches contract):** build → test → `cargo update` →
`cargo build --release`. After `cargo update`, run `git diff Cargo.lock` and
sanity-check that (a) dirs/serde/serde_derive/toml + toml_edit/toml_datetime/winnow/
serde_spanned are GONE, and (b) any remaining bumps are semver-minor/patch only
(e.g. clap 4.5.47 → 4.5.x), not surprising.

If the implementer prefers to avoid broad bumps, **skipping `cargo update` is safe**
because `cargo build`/`cargo test` already pruned the lockfile. But the contract
specifies `cargo update`, so it is included as the canonical refresh.

---

## F5 — Parallel-work boundary (P1.M4.T1.S1)

P1.M4.T1.S1 (Add `--query-info`/`--list-callbacks` flags) is being implemented
concurrently. Its PRP's "Known Gotchas" and "Integration Points" sections state
explicitly (F6 in its research): **"no Cargo.toml change"** — it relies on clap's
default features only and does not touch deps. Its file edits are confined to
`src/lib.rs`, `src/main.rs`, and `README.md`.

Therefore: **this item (P1.M4.T2.S1) owns `Cargo.toml` and `Cargo.lock` exclusively.**
There is no merge-conflict risk with P1.M4.T1.S1. The two items' edit sets are
disjoint.

P1.M4.T3.S1 (README v0.3.0 API-surface sync) will touch README version references
later; this item must NOT edit README.

---

## F6 — Scope boundaries (do NOT do these)

- **Do NOT cut the `v0.3.0` git tag.** Tagging is a release/separate step. The work
  item is the Cargo.toml version field + lockfile refresh only. qmkonnect updates
  its own `tag = "v0.2.1"` pin to `v0.3.0"` AFTER the tag exists — that is the
  downstream consumer's job, not this item's.
- **Do NOT edit README.md.** Version references in README are P1.M4.T3.S1.
- **Do NOT touch `src/*.rs` or `error.rs`.** If the post-removal build/test fails,
  the correct response (per the work item contract step c) is to RESTORE the
  offending dep and investigate — NOT to patch source code to force-remove a dep.
  (This branch is not expected to fire; see F1.)
- **Do NOT remove `clap` or `hidapi`** — both are actively used (clap in lib.rs,
  hidapi in core.rs).

---

## F7 — Build system & environment

- `cargo 1.92.0 (344c4567c 2025-10-21)` — available on PATH.
- No `.github/` workflows, no `Makefile`, no `build.rs`. Cargo is the sole build
  system. No release scripts to update.
- No `rust-toolchain.toml` pinning — edition 2021 is satisfied by 1.92.0.
- Existing test baseline: 22 unit tests (per system_context.md). After this item,
  `cargo test` must still report all 22 passing (this item changes no source, so
  the count is unchanged).