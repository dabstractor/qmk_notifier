# Research Notes — P1.M4.T3.S1 (README v0.3.0 documentation)

## F1 — CRITICAL FINDING: item description is STALE; README already PARTIALLY updated

The work-item description asserts: *"The current README.md documents v0.2.1
behavior. It has a 'Programmatic Usage' section showing `match run(params) { Ok(()) => ... }` and a note about 'Round B (v0.3.0) changes run() to return Result<CommandResponse, QmkError>'."*

**This is no longer accurate.** `git log -- README.md` shows the last README
edit was commit **99ec8a8 "Add CLI flags for typed diagnostics"** (P1.M4.T1.S1,
already COMPLETE). That commit touched `README.md` (+34 lines) and rewrote
parts of it to the v0.3.0 surface. The current committed README (`git diff HEAD
-- README.md` = empty, so working tree == HEAD) ALREADY contains:

- The `--query-info` and `--list-callbacks` rows in the **Command Line Options**
  table (contract point **d — ALREADY DONE**).
- A **Programmatic Usage** section that already returns `Result<CommandResponse,
  QmkError>` and matches on `CommandResponse::Legacy` / `::Info` / `::Timeout`
  (contract point **b — LARGELY DONE**, though it uses `SendMessage` not the
  `QueryInfo` typed example the contract specifies).

**Therefore this item is NOT a full README rewrite.** It is a targeted set of
edits to the sections that P1.M4.T1.S1 did NOT touch (the forward-reference
language, the misleading Default Reference Values, and the Technical Details
omission). See F2 for the exact section-by-section map.

## F2 — Section-by-section map: contract point (a–f) → DONE / LEFT

Contract points (a–f) from the item description vs. current committed README:

| Point | Section | Contract requirement | Current README state | Status |
|---|---|---|---|---|
| **a** | Overview | Describe typed-command transport alongside legacy string path | Says "(round B adds typed-command transport + reply parsing)" — **forward-reference** ("round B" = future) | **LEFT** — rewrite to present-tense v0.3.0 |
| **b** | Programmatic Usage | v0.3.0 example using `RunCommand::QueryInfo`, `HostOs` import | Already v0.3.0 / `CommandResponse`, but example uses `SendMessage` (not the typed `QueryInfo` the contract specifies) | **LEFT (light)** — align to showcase typed-command path per contract example |
| **c** | (Overview/note) | Remove the "Round B (v0.3.0)" forward-reference note | "round B adds typed-command transport + reply parsing" still present in Overview bullet | **LEFT** — same edit as (a) |
| **d** | Command Line Options | Add `--query-info` / `--list-callbacks` to options table | Both rows already present (P1.M4.T1.S1) | **DONE** — verify only |
| **e** | Default Reference Values | Clarify VID/PID default to `None` (auto-discovery); DEFAULT_* consts are reference values, NOT matching defaults | Lists "Vendor ID: `0xFEED`", "Product ID: `0x0000`" as the FIRST two "values commonly used … must be provided explicitly" — implies they are defaults. MISLEADING. | **LEFT** — rewrite to state VID/PID default to `None`/auto; 0xFEED/0x0000 are reference/typical values |
| **f** | Technical Details | Mention typed-command framing `[0x81][0x9F][0xF0][cmd][args][0x03]` + reply parsing (0x51 typed vs 0/1 legacy) | Current Technical Details is the v0.2.1 bullet list (hidapi, batching, error handling) — NO typed-command framing or reply parsing | **LEFT** — add typed-command framing + reply-parsing bullets |

**Net remaining edits**: points (a/c), (b light), (e), (f). Point (d) is already
done — the implementer only verifies it.

## F3 — Exact technical details for the Technical Details section (point f)

Verified from `src/core.rs` (constants + `build_typed_payload` + `parse_reply`) and
the CANONICAL `plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md`
(§Typed-Command Framing, §Reply Disambiguation):

**Framing (request):** every report to hidapi is 33 bytes:
`[0x00][0x81][0x9F][<30 payload bytes>]` — the leading `0x00` is the hidapi
report-ID byte; `0x81 0x9F` is the magic header (PRD §14 invariant 1); 30 payload
bytes per report (`PAYLOAD_PER_REPORT = REPORT_LENGTH - 2 = 30`).

- Legacy strings: payload = `"{class}\x1D{title}" + 0x03 (ETX)`.
- Typed commands: payload = `[0xF0][cmd_id][args…][0x03]` — so the firmware-side
  byte layout is `[0x81][0x9F][0xF0][cmd][args][0x03]`. `0xF0` is the typed
  discriminator (`CMD_DISCRIMINATOR`, first payload byte = `data[2]` to firmware).
  `0xF0` can never begin a real matched string (firmware sanitizer allows only
  `0x20–0x7E`), so legacy firmware safely walks typed bytes as a no-match string.

**Reply parsing:** after a burst, read ONE 32-byte IN report with a bounded
`read_timeout` (`REPLY_READ_TIMEOUT_MS = 1000` ms). `response[0]` disambiguates:
- `0x51` (`RESPONSE_MARKER`) ⇒ typed reply; decode by `response[1]` (cmd echo)
  into `Info` / `CallbackName` / `Ack`.
- `0` ⇒ `Legacy { matched: false }`; `1` ⇒ `Legacy { matched: true }`.
- no reply within timeout ⇒ `Timeout` (legacy/offline device; caller stays in
  string-only mode — PRD §8, §10.2).

**Typed command IDs** (`firmware_wire_contract.md` §Command Table):
`0x01` QUERY_INFO, `0x02` QUERY_CALLBACK, `0x03` SET_OS, `0x05`
APPLY_HOST_CONTEXT. (0x04 reserved for VIA-coexist.)

## F4 — Default Reference Values correction (point e)

The current section header "must be provided explicitly" is FALSE for VID/PID.
The real semantics (verified `src/lib.rs` `RunParameters::new` doc + `parse_matches`):

- `vendor_id` / `product_id` are `Option<u16>`; CLI omits them ⇒ `None` ⇒
  "match any" device (auto-discovery by usage page/usage — the keystone
  zero-config path). PRD §14 invariant 3.
- `usage_page` / `usage` are ALWAYS required and default to the QMK Raw-HID
  convention `0xFF60` / `0x61` (`DEFAULT_USAGE_PAGE` / `DEFAULT_USAGE`).
- `DEFAULT_VENDOR_ID = 0xFEED` and `DEFAULT_PRODUCT_ID = 0x0000` (`src/core.rs:6-7`)
  are **reference/typical values**, NOT the CLI/library matching default. They
  exist for documentation and for callers who want to disambiguate a specific
  board. The CLI help string already says `[default: auto (match any)]` for both
  VID and PID — the README prose must match that.

**Fix**: rewrite the section so VID/PID are listed as "reference values (omit
for auto-discovery)", and usage page/usage are the actual always-required
defaults. Keep the ETX note.

## F5 — Parallel boundary with P1.M4.T2.S1 (running concurrently)

P1.M4.T2.S1 (Remove unused deps + bump version to 0.3.0) edits **`Cargo.toml`**
and **`Cargo.lock`** ONLY. Its PRP (read in full) explicitly states: "DO NOT edit
README.md (P1.M4.T3.S1 owns version references there)." Therefore:

- **This item owns README.md with ZERO merge-conflict risk** vs P1.M4.T2.S1 —
  disjoint edit sets (Cargo.toml/Cargo.lock vs README.md).
- README mentions no version number in a way that Cargo.toml touches — the only
  version string in README is implicit (none; README has no `0.2.1`/`0.3.0`
  literal today — verified: `grep -n "0.2.1\|0.3.0" README.md` → no matches).
  So there is no version-string coordination needed between the two items.
- P1.M4.T1.S1 (already COMPLETE, committed 99ec8a8) was the LAST item to touch
  README. Its edits are merged at HEAD. This item starts from that HEAD state.

## F6 — Scope boundaries (do NOT do these)

- Do NOT edit Cargo.toml/Cargo.lock (P1.M4.T2.S1 owns them).
- Do NOT edit src/*.rs (lib.rs/core.rs/main.rs/error.rs are stable / owned by
  P1.M4.T1.S1 which is already complete).
- Do NOT edit PRD.md, any tasks.json, prd_snapshot.md, .gitignore.
- Do NOT rewrite README from scratch — it is already 90% v0.3.0; make TARGETED
  edits to the four sections in F2 marked LEFT. Wholesale rewrite risks
  regressing the already-correct sections (CLI table, Programmatic Usage match).
- Do NOT cut the `v0.3.0` git tag (separate release step).
- Do NOT change the README's existing correct content (Installation, Dependencies,
  Usage examples, Integration, Example Use Cases, Why Rust, Related Projects,
  Contributing, License) — those are out of scope and already accurate.
- Point (d) CLI flags are ALREADY in the table — VERIFY only, do not re-add
  (would create duplicate rows).

## F7 — Validation approach for a docs-only change

README.md is Markdown — there is no compiler, no `cargo test`, no type check. The
validation gates are:
1. **Markdown rendering sanity**: optional `markdownlint` if available; otherwise
   visual review of the rendered diff. (Project has no MD linter configured —
   `ls -a` shows no `.markdownlint*`. So manual review is the gate.)
2. **Accuracy grep**: confirm the forward-reference "round B" string is GONE;
   confirm framing byte sequence appears; confirm "must be provided explicitly"
   no longer applies to VID/PID.
3. **No code churn**: `git status --porcelain` shows ONLY `M README.md` (plus
   whatever P1.M4.T2.S1's Cargo.toml/Cargo.lock show, since it runs concurrently).
4. **Build still passes** (sanity — README edit cannot break a build, but confirm
   `cargo build` is still green so nothing else drifted): not strictly required
   for a docs change, but cheap reassurance.

The "Level 1/2/3" gates in the template map to: Level 1 = markdown sanity;
Level 2/3 = N/A (no tests, no service); Level 4 = accuracy grep + manual review.