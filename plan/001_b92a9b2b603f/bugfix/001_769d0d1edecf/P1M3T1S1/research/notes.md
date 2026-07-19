# Research Notes — P1.M3.T1.S1 (bugfix docs sync)

**Item**: Update README.md technical details and reply-parsing description.
**Scope**: docs-only. ONE file modified (`README.md`). No source, no Cargo.toml.

---

## F0 — Verified: this is a docs-only task touching README.md

- The bugfix plan (`bugfix/001_769d0d1edecf`) is "P1: v0.3.1 Reply-Capture & API
  Conformance Bug Fixes". All CODE fixes are COMPLETE per plan_status:
  - P1.M1.T2 (capture-LAST reply, Issue 1) — DONE.
  - P1.M1.T3 (pre-send IN-buffer drain, Issue 3) — DONE.
  - P1.M2.T1 (`parse_cli_args -> Result<RunParameters, QmkError>`, Issue 2) — DONE.
  - P1.M2.T2 (drop `-c` short flag, Issue 5) — DONE.
  - P1.M2.T3 (libc::signal, Issue 4) — in parallel (P1.M2.T3.S1); README has zero
    SIGPIPE mentions, so it is a no-op for the README regardless.
- The README was already updated for v0.3.0 in the MAIN plan (it already documents
  typed commands, `CommandResponse`, `run()` return type, auto-discovery). This
  bugfix task fixes the ONE stale mechanism description left by that update.

## F1 — THE ONLY STALE TEXT: README.md:160-163 (Technical Details, "Reply parsing")

Verified exact current bytes (awk line-numbered):
```
160|- **Reply parsing:** after each burst, one 32-byte IN report is read with a bounded
161|  timeout. `response[0] == 0x51` ⇒ typed reply (decoded by the command-echo byte
162|  into `Info`/`CallbackName`/`Ack`); `0`/`1` ⇒ legacy match-bool; no reply ⇒
163|  `Timeout` (a non-capable/offline device — the caller stays in string-only mode)
```
**Why it's wrong**: "one 32-byte IN report is read" describes the BUGGY
capture-FIRST behavior (the v0.3.0 bug from Issue 1: the first/intermediate reply
was captured and the real ETX-report result was drained). The fix (v0.3.1)
captures the LAST reply. This bullet must be rewritten.

**Siblings in the same bullet list** (Technical Details, lines 149-164): all
OTHER bullets are accurate (32-byte/30-payload framing, auto-discovery, typed-
command framing `v0.3.0`, "Configurable for any QMK keyboard"). Only the Reply
parsing bullet is stale. The "Typed-command transport (v0.3.0)" bullet is
CORRECT as v0.3.0 — typed-command FRAMING was a v0.3.0 feature; the bugfix only
changed how the REPLIES to those commands are captured.

## F2 — Verified: clause (c) `parse_cli_args` is a NO-OP (not in README)

```
grep -n "parse_cli_args" README.md  →  (no matches)
```
The README's §Programmatic Usage (lines ~108-143) documents `run()` only:
- L120: `// run() returns Result<CommandResponse, QmkError>.`  ← already correct
- L138: `` `run()` returns [`Result<CommandResponse, QmkError>`](PRD.md). ``  ← already correct

The actual signature (lib.rs:353) IS `pub fn parse_cli_args() ->
Result<RunParameters, QmkError>` (Issue 2 fix is in), but the README never showed
`parse_cli_args`, so there is nothing to update. The item clause (c) is
conditional ("if parse_cli_args is mentioned or its signature is shown") — the
condition is FALSE ⇒ no-op. Document this so the implementer doesn't hunt for a
non-existent edit.

## F3 — Verified: clause (d) Overview (L10) "reply parsing" is a NO-OP

L10: "...alongside the legacy window-string path, plus reply parsing. Transport
only — it does no matching." This is a high-level CAPABILITY claim (the crate does
reply parsing). It makes no single-vs-multi-report mechanism claim, so it is NOT
stale. The item says "update ... if needed to reflect multi-report correctness" —
not needed. No change. (Decision documented, not an oversight.)

## F4 — Verified: clause (a) CLI table (L71-86) is a NO-OP (Issue 5)

The CLI options table already omits `-c` and `--create-config` entirely (it lists
message, --vendor-id, --product-id, --usage-page, --usage, --verbose, --list,
--query-info, --list-callbacks, --help). The `--create-config` flag still EXISTS
in lib.rs (it returns a `RemovedFeature` error) but is undocumented — which is
exactly the Issue-5 fix's intent ("exposes an undocumented -c short flag" →
flag is now undocumented). No README change needed.

## F5 — Verified: README has ZERO SIGPIPE / unsafe / libc mentions

```
grep -niE "sigpipe|unsafe|libc|signal|broken pipe" README.md  →  (no matches)
```
So Issue 4 (the parallel P1.M2.T3.S1 libc::signal task) is a complete no-op for
the README. Do NOT add any SIGPIPE/unsafe documentation to the README.

## F6 — The ACTUAL implemented behavior the new bullet must describe (core.rs:355-446)

Read directly from the merged fix code (NOT just the design doc):

1. **Pre-send drain** (Issue 3, core.rs:365-374): BEFORE the burst-write, up to
   `IN_DRAIN_MAX` IN reports are read non-blocking (`read_timeout(0)`) and
   discarded, flushing stale replies a prior send may have left in the kernel IN
   buffer. Breaks on the first `Ok(0)` (no data) — cheap one-poll in the common case.
2. **Burst-write**: `batch_count` reports (unchanged).
3. **Capture-LAST** (Issue 1, core.rs:400-425): AFTER the burst-write, a loop
   `for _ in 0..batch_count.max(1)` reads each reply with a bounded
   `read_timeout(REPLY_READ_TIMEOUT_MS)` and OVERWRITES `reply` each iteration
   (`reply = Some(read_buf[..n].to_vec()) // overwrite ⇒ keep LAST (ETX) reply`),
   so the LAST non-empty reply is retained. A timeout/error (`Ok(0)`/`Err`)
   breaks the loop early. Rationale: firmware emits one reply per report; only
   the ETX-terminating report's reply carries the real result.
4. **Surplus drain** (core.rs:436-444): non-blocking `read_timeout(0)` bounded by
   `IN_DRAIN_MAX`, safety net for stragglers.

**Key phrases for the README** (from the code's own comments): "the firmware
emits one 32-byte reply per report processed"; "reads up to batch_count replies
and retains the LAST ... the ETX-report reply, which carries the real result";
"non-blocking IN-buffer drain before each send flushes stale replies from a prior
command". The item gives the target wording verbatim for (a) and (b) — transcribe it.

## F7 — Designed replacement bullet (lines 160-163 → new text)

```markdown
- **Reply capture & parsing (v0.3.1):** the firmware emits one 32-byte reply per
  report processed, so after each burst-write the crate reads up to `batch_count`
  replies and retains the **last** non-empty one — the reply to the ETX-terminating
  report, which carries the real result. (A non-blocking IN-buffer drain **before**
  each send first flushes any stale replies left by a prior command, so they cannot
  contaminate the capture.) `response[0] == 0x51` ⇒ typed reply (decoded by the
  command-echo byte into `Info`/`CallbackName`/`Ack`); `0`/`1` ⇒ legacy match-bool;
  no reply ⇒ `Timeout` (a non-capable/offline device — the caller stays in
  string-only mode)
```
- Covers item (a) capture-last + (b) pre-send drain + keeps the disambiguation.
- Heading refined `Reply parsing` → `Reply capture & parsing` (more accurate: the
  capture step is now the subject of the bugfix). Still ONE bullet in the existing
  Technical Details list — NOT a new section (item: "Do NOT create new sections").
- Continuation indent = 2 spaces (matches sibling bullets, e.g. L150-152, L154-159).
- `(v0.3.1)` label marks the bugfix milestone that introduced capture-last +
  pre-send drain, consistent with the sibling "Typed-command transport (v0.3.0)"
  label. NOTE: Cargo.toml is STILL at `version = "0.3.0"` (verified); version-
  bumping is OUT OF SCOPE for this docs task (no version-bump task exists in the
  bugfix plan). The label is prose about the behavior's origin, not a Cargo
  version claim — do NOT bump Cargo.toml.

## F8 — Scope boundary (what NOT to touch)

- README §Programmatic Usage (L108-143): accurate (run() return type). NO change.
- README L7 / L10 (Overview): capability-level, not stale. NO change.
- README CLI table (L71-86): already omits -c/--create-config. NO change.
- README "Typed-command transport (v0.3.0)" bullet: framing is v0.3.0; correct. NO change.
- Cargo.toml, src/*, PRD.md: DO NOT TOUCH. Cargo.toml version stays 0.3.0.
- Do NOT add new README sections, headings, or SIGPIPE/unsafe/parse_cli_args docs.

## F9 — Validation gates (docs task)

- Markdown well-formed: the rewritten bullet uses the same 2-space continuation
  indent + `- **bold:**` shape as its siblings; the bullet list still renders.
- `grep` confirmations: the buggy phrase "one 32-byte IN report is read with a
  bounded" is GONE; "retains the **last**" + "IN-buffer drain **before**" are PRESENT.
- Sweep: no OTHER stale "after each burst, one" / capture-first wording remains.
- Cargo smoke (sanity only — README cannot affect compilation): `cargo build` and
  `cargo test` still pass unchanged. Guards against an accidental source edit.
- Cross-check: README "reads up to batch_count replies and retains the last"
  matches core.rs `for _ in 0..batch_count.max(1)` + `reply = Some(...)` overwrite.