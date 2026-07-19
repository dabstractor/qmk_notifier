# Research Notes — P1.M3.T1.S2 (Correct stale architecture docs: firmware typed-command status)

**Task**: Fix architecture docs that still claim the firmware does NOT implement
typed commands. The firmware (at `/home/dustin/projects/qmk-notifier/notifier.c`)
**now implements typed dispatch** — confirmed via live hardware testing against a
real QMK keyboard (Dactyl-Manuform, VID 0xFEED / PID 0x0000), per the bugfix PRD
(prd_snapshot.md §Overview + §Issue 1).

---

## F1 — Comprehensive sweep: EVERY stale "typed-commands-unimplemented" claim

`grep -rniE "NOT YET IMPLEMENTED|typed commands yet|does not implement typed|no 0xF0 branch|not.*implement.*typed|will work against firmware once" plan/001_b92a9b2b603f/architecture/` found **THREE** stale claims (NOT two):

| # | File:line | Section | Stale claim (verbatim) | Named in task? |
|---|-----------|---------|------------------------|----------------|
| **A** | `firmware_wire_contract.md:118-130` | `## Firmware Implementation Status` (LAST section) | "**NOT YET IMPLEMENTED.** The firmware `notifier.c` has no `0xF0` branch in `hid_notify()`… This crate's typed-command transport will work against firmware once the firmware implements §4.6." | **YES (clause a)** |
| **B** | `findings_and_risks.md:28-31` | `### F4: The firmware does not implement typed commands yet` | "This is by design. The transport layer must handle timeouts gracefully… The tests for reply parsing must use synthetic byte buffers, not live hardware." | **YES (clause b)** |
| **C** | `system_context.md:90-94` | `### Upstream canonical: firmware (qmk-notifier)` | "The firmware `notifier.c` **does NOT yet implement the typed-command namespace**. There is no `0xF0` branch in `hid_notify()`, no `handle_typed_command()`, no `host_layer`/`host_cb_enabled` trackers… not yet coded in firmware." | **NO — DISCOVERED** |

### Scope tension & resolution
- The task **CONTRACT (clauses a, b)** names exactly **two** files: firmware_wire_contract.md and findings_and_risks.md. (Clause 3's "(c)" is a META-NOTE — "these are plan/ architecture docs, keep edits factual and concise" — NOT a third file.)
- The bugfix's own `system_context.md` "Stale Architecture Docs to Update" inventory (bugfix architecture L170-175) ALSO lists only those two — it missed (C).
- BUT the task **OUTPUT (clause 4)** is **absolute**: *"Architecture docs no longer claim typed commands are unimplemented."*
- Leaving (C) system_context.md:90-94 in place would **violate the binding success criterion** — after a strict (a)+(b)-only edit, an architecture doc would STILL claim typed commands are unimplemented.

**Resolution**: PRP primary scope = the two named files (A, B) — honors the explicit
contract. ADD a clearly-flagged **REQUIRED discovery task** for (C)
system_context.md:90-94, documented as "discovered during PRP research, beyond the
named (a)/(b), but required to satisfy OUTPUT clause 4." Cost is one paragraph and
it is the same factual correction. The implementer/orchestrator can make the final
call, but the PRP must not silently leave a known false claim that blocks the
success definition.

> NOTE: a near-identical stale phrase also appears in the **bugfix** architecture's
> own `system_context.md` (bugfix dir L171, L191), but those are TRACKING notes
> (an inventory of stale docs to fix) inside the bugfix's planning copy — they
> reference the MAIN architecture files as targets, not the firmware itself. They
> are arguably self-resolving once (A)/(B)/(C) are fixed (the inventory would be
> empty), but they are OUT of this task's scope (they live under the bugfix dir,
> not the main architecture dir named in the contract). Flagged here for awareness
> only — do NOT edit the bugfix system_context.md.

---

## F2 — Verified firmware reality (`/home/dustin/projects/qmk-notifier/notifier.c` + `.h`)

The firmware **DOES** implement typed dispatch. Read directly from the C source
(file mtime 2025-07-19 05:42, post-v0.3.0):

1. **0xF0 discriminator routing** — `hid_notify()` at notifier.c:835; the branch is at **notifier.c:854**:
   ```c
   if (msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR) {
       typed_mode = true;
       typed_literal_remaining = 2;   /* consume discriminator + cmd_id literally */
       typed_awaiting_terminator = false;
   }
   ```
   This is checked ONLY on the first report (msg_index==0) — exactly as the
   `firmware_wire_contract.md` "Typed-Command Framing" diagram already documents.

2. **handle_typed_command()** — exists at notifier.c:717. It dispatches reassembled
   typed commands (QUERY_INFO 0x01 / QUERY_CALLBACK 0x02 / SET_OS 0x03 /
   APPLY_HOST_CONTEXT 0x05) and emits the typed reply (notifier.c:690):
   ```c
   response[0] = NOTIFY_RESPONSE_MARKER;      /* 0x51 */
   ...
   raw_hid_send(response, RAW_REPORT_SIZE);   // notifier.c:697
   ```

3. **typed_dispatched guard** — at ETX, typed commands set `typed_dispatched = true`
   (notifier.c:909) which suppresses the legacy ack. The legacy ack is now guarded
   (notifier.c:1017):
   ```c
   if (!typed_dispatched) {
       uint8_t response[RAW_REPORT_SIZE] = {0};
       response[0] = match;
       raw_hid_send(response, RAW_REPORT_SIZE);
   }
   ```
   This is the EXACT snippet the bugfix PRD §Issue 1 quotes (prd_snapshot.md:55-64).

4. **notifier.h constants** confirm typed-capable firmware:
   - `NOTIFY_CMD_DISCRIMINATOR 0xF0` (notifier.h:44)
   - `NOTIFY_RESPONSE_MARKER 0x51` (notifier.h:46)
   - `NOTIFY_PROTO_VER 2  // typed-command capable` (notifier.h:53)

### Per-report reply model (the canonical behavior these docs must reflect)
`hid_notify()` is invoked **once per 32-byte report** and replies at the **end of
every call**. For an N-report message the device emits **N replies**:
- Reports 1..N-1 (no ETX seen): `typed_dispatched` stays false ⇒ legacy `0` ack sent.
- Report N (ETX): typed command dispatches ⇒ `[0x51][cmd_echo][payload]` emitted,
  legacy ack suppressed.

This is the model confirmed by the bugfix PRD's live-hardware probing
(prd_snapshot.md §Issue 1 "Direct hidapi proof": 1-report→[1], 2-report→[0,1],
4-report→[0,0,0,1]) and cross-checked against `notifier.c::hid_notify`.

---

## F3 — Each doc's internal consistency check (what ELSE is accurate, don't touch)

- **firmware_wire_contract.md**: the Command Table, Field Definitions, Reply
  Disambiguation, and Constants sections all ALREADY describe typed commands in
  full detail (cmd_ids, 0x51 replies, NOTIFY_PROTO_VER=2). The ONLY stale part is
  the trailing "Firmware Implementation Status" section — which directly
  contradicts the rest of the file. Fixing it makes the doc internally consistent.
- **findings_and_risks.md**: F4 is a one-off finding. Findings F1/F2/F3/F5/F6 and
  all Risks R1-R5 are about THIS crate's Rust code, unaffected by firmware status.
  Edit ONLY F4.
- **system_context.md**: the "### Upstream canonical: firmware" subsection (L89-94)
  is the stale part. Surrounding sections (crate state, consumer qmkonnect impact)
  are about the Rust crate, unaffected. Edit ONLY L90-94.

---

## F4 — No conflict with parallel P1.M3.T1.S1 (README.md)

P1.M3.T1.S1 touches **README.md ONLY** (read its PRP — confirmed). This task touches
**only `plan/001_b92a9b2b603f/architecture/*.md`**. **Zero file overlap.** No
coordination needed. No merge risk.

---

## F5 — Validation approach for a plan-docs edit

These are Markdown files under `plan/` — they are NOT compiled, NOT shipped, NOT
part of any test suite. Therefore:
- The **primary** validation = grep gates: stale claims GONE, accurate claims PRESENT,
  no remaining "not yet implemented"/"does NOT yet implement" wording anywhere in
  `plan/001_b92a9b2b603f/architecture/`.
- The **secondary** validation = `cargo build` + `cargo test` sanity gate — these
  CANNOT be affected by a plan/ markdown edit, so a passing build only PROVES no
  accidental source edit (catches a stray touch to src/* or Cargo.toml).
- The **tertiary** validation = `git diff --stat` shows ONLY the architecture .md
  files changed.

---

## F6 — Replacement content (factual, concise — per clause 3c)

The docs should now state the firmware implements typed dispatch and emits 0x51
typed replies on the ETX report, confirmed via live hardware testing. Key facts to
convey (do NOT over-claim — these are architecture/planning docs):
- Firmware `hid_notify()` now routes `data[2] == 0xF0` (first report) to a typed
  reassembly path.
- `handle_typed_command()` dispatches QUERY_INFO / QUERY_CALLBACK / SET_OS /
  APPLY_HOST_CONTEXT and emits `[0x51][cmd_echo][payload]` replies.
- Per-report reply model: one 32-byte reply per `hid_notify` call; the typed 0x51
  reply is emitted only on the ETX report; intermediate reports get a legacy `0` ack.
- Confirmed via live hardware testing (Dactyl-Manuform) against qmk-notifier firmware.
- If these docs were used to justify a test strategy ("tests must use synthetic
  buffers, not live hardware" — F4), that rationale is now SUPERSEDED: live hardware
  typed replies are now observable. (But synthetic-buffer tests remain valid and
  preferred for unit tests; the point is the firmware is no longer a stub.)

Exact replacement wording is given verbatim in the PRP "Implementation Patterns" §A/§B/§C.