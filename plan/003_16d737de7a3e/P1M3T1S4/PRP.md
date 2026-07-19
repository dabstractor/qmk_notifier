name: "P1.M3.T1.S4 — Coexistence/backward-compat + multi-report typed framing test cases for test_notifier_host.c"
description: >
  EXTEND `test_notifier_host.c` (scaffolding by S2, SET_OS/AHC blocks by the
  parallel S3) with the LAST two test categories: (a) COEXISTENCE / backward-
  compatibility and (b) MULTI-REPORT typed framing. This task EDITS that one file
  by APPENDING three test blocks to `main()` immediately before the summary
  `printf`, anchored on that stable line so it composes with S3's parallel edit.
  NO new file-scope declarations are required — S4 reuses S2's `board_cmd_*` /
  `cb_*` flags and the `send_typed` helper; the multi-report block INLINES its two
  reports (no new helper) to keep the diff purely inside `main()` (minimal merge
  surface with S3).

  The three blocks (all EMPIRICALLY VERIFIED PASSING against the real notifier.c
  via probe — see research/findings.md):
    (coexist-i)  A legacy string (printable data[2], ≠0xF0) is NOT routed to the
                 typed path: its ack is the match-bool 0/1, NOT 0x51 (§4.6
                 "0xF0 can never begin a real matched string"; sanitizer allows
                 only 0x20–0x7E). Proven both with a no-match ("firefox"→0) and a
                 match ("neovide"→1 + board side effects) so the FULL legacy
                 dispatch path is shown intact ALONGSIDE the typed path.
    (coexist-ii) A non-magic report (data[0]!=0x81) is silently discarded: no
                 raw_hid_send, so stub_get_last_response() is UNCHANGED (§13 inv.1).
    (multi-rep)  A typed APPLY_HOST_CONTEXT (0x05) split across two 32-byte reports
                 reassembles + dispatches: r[1]==0x05 (the cmd-echo PROVES report 1's
                 cmd_id persisted into the reassembled buffer), r[2]==1, host layer
                 224 active, cb_mute_on fired (§4.6 ETX framing, may span reports).

  🚨 WHY AHC (0x05) AND NOT SET_OS FOR MULTI-REPORT: the reassembly byte loop
  treats ANY 0x03 byte as ETX. SET_OS's cmd_id is 0x03 (== ETX) — the S3 blocker.
  AHC's cmd_id is 0x05 (≠ ETX), and its args (layer=224, flags=0, count=28,
  ids=0) contain NO 0x03 byte, so the message spans reports and reassembles
  correctly. This is the explicit, correct choice (item spec: "a large
  APPLY_HOST_CONTEXT id list"). All three blocks PASS with 0 FAIL.

---

## Goal

**Feature Goal**: Extend `test_notifier_host.c` with the coexistence/backward-
compat and multi-report typed-framing test slices, completing the file's
six-category coverage (QUERY_INFO, QUERY_CALLBACK, SET_OS, APPLY_HOST_CONTEXT
stack/replace, coexistence, multi-report). The tests drive legacy strings and a
two-report typed command through the public `hid_notify` entry against the
stub-compiled `notifier.c`, asserting the §4.6 / §13 contract via
`stub_get_last_response()` + `stub_get_active_layer()` — following the EXACT
pattern S2 established (file-scope `DEFINE_*`, `CK` macro, `PASS:`/`FAIL:`,
summary, `return g_fail?1:0`).

**Deliverable**: ONE file MODIFIED — `test_notifier_host.c` (repo root, created by
S2). A single `edit` call APPENDS three test blocks to `main()` immediately before
the summary `printf` line. No new file-scope declarations, no new helpers, no
other files touched.

**Success Definition**:
- `test_notifier_host.c` compiles clean with the runner's test-link standard
  (`-Wall -std=c99`, zero warnings) AND strict (`-Wall -Wextra -std=c99`, zero
  warnings).
- The three new blocks (coexist-i, coexist-ii, multi-report) ALL PASS — 0 `FAIL:`
  lines attributable to them (empirically verified by probe).
- S2's QUERY_INFO/QUERY_CALLBACK blocks + S3's SET_OS/AHC blocks are UNAFFECTED
  (S3's SET_OS failures are its own documented blocker; S4 adds nothing that
  regresses them). The S2 "side-effect-free" assertion still passes (it runs
  before S3/S4 blocks).
- Mode-A comments cite §4.6 (0xF0 never begins a real matched string; sanitizer
  allows only 0x20–0x7E; ETX-framed, may span reports) and §13 invariant 1 (magic
  header). This rides WITH the work.
- No file other than `test_notifier_host.c` is modified.

## User Persona (if applicable)

**Target User**: The contributor running the §11.2D stub-compile gate and the
P1.M2 owner responsible for typed framing. End users / the desktop host never see
this — it is host-side firmware test infrastructure.

**Use Case**: (1) A developer changes the `hid_notify` magic-header guard, the
`typed_mode` discriminator check, or the multi-report reassembly — the gate
rebuilds `test_notifier_host` and the coexistence/multi-report blocks catch the
regression. (2) A developer proves the firmware's claim that "a string-only host
coexists unchanged" (PRD §4.6) and that "typed commands may span multiple 32-byte
reports" (PRD §4.6) — these two properties were previously un-asserted from the
host.

**Pain Points Addressed**: findings_and_risks.md F5 ("existing tests never send
0xF0") and F7 ("multi-report typed framing uses existing reassembly") identified
that the coexistence invariant and the multi-report reassembly path had NO host
test coverage. S2 covered the read-only query handlers; S3 covers the state-
mutating handlers; THIS task gates the two remaining properties: (a) legacy
strings are transparent to the typed discriminator, and (b) a typed command
spanning two reports reassembles. These are the backward-compatibility and
scalability guarantees of the typed-command namespace.

## Why

- **Closes the last two test categories in the file's six-category plan.** The
  item's OUTPUT criterion requires the full `test_notifier_host.c` to cover all
  six categories with 0 FAIL. Coexistence + multi-report are the final two.
- **Coexistence is a load-bearing backward-compat guarantee.** PRD §4.6 + §14
  promise "a host that sends only legacy strings coexists unchanged" and "0xF0 can
  never begin a real matched string (sanitizer allows only 0x20–0x7E)". Without a
  test, a future change to the discriminator check or sanitizer could silently
  route legacy strings into the typed path (or vice versa). The coexist-i block
  encodes the invariant byte-for-byte (response[0] is the legacy 0/1 ack, never
  0x51). The coexist-ii block encodes the magic-header coexistence guard (§13
  invariant 1) — a non-magic report is discarded with no side effects.
- **Multi-report framing is the typed namespace's scalability guarantee.** PRD §4.6
  promises "Multi-report framing removes any fixed cap on argument size (notably
  the callback-id list in APPLY_HOST_CONTEXT)". Without a test, a change that
  resets `msg_index`/`typed_mode` between reports (the exact RISK-1 lifecycle bug)
  would silently break multi-report commands. The multi-rep block proves a
  two-report AHC reassembles — the `r[1]==0x05` cmd-echo is the proof.
- **Proven pattern, zero design risk.** Identical to S2/S3: file-scope `DEFINE_*`
  (already present), `CK` macro (already present), `send_typed` helper (reused for
  coexist-ii's setup QUERY_INFO), manual-extern stub accessors (already present).
  Every assertion is traced from `notifier.c` AND confirmed by empirical probe
  (research/findings.md F4-1..F4-4). Confidence: 10/10 — all three blocks verified
  PASSING before this PRP was written.

## What

EDIT `test_notifier_host.c`. Append three test blocks to `main()` immediately
before the summary `printf` line (the stable anchor; S3 also inserts there — both
compose). Each block opens with a Mode-A comment citing §4.6 / §13.

**(coexist-i) Legacy string coexists with typed path — §4.6 / §13 inv.1-2 / F5**
- Build a 32-byte report `[0x81][0x9F]` + the legacy string bytes + `0x03` ETX.
  Drive via `hid_notify(rep, 32)`.
- (coexist-i)(a) `"firefox"` (data[2]='f'=0x66, ≠0xF0, no map match): assert
  `r[0] != NOTIFY_RESPONSE_MARKER` (not routed to typed) AND `r[0] == 0` (legacy
  no-match ack). Cite §4.6 / F5.
- (coexist-i)(b) `"neovide"` (matches the default board map): assert
  `r[0] != NOTIFY_RESPONSE_MARKER` AND `r[0] == 1` (legacy match ack) AND
  `board_cmd_en == 1` (full dispatch fired on_enable) AND
  `stub_get_active_layer() == 5` (layer activated). Cite §4.6 / §13. Resets
  `board_cmd_en/dis` before sending.

**(coexist-ii) Non-magic report silently discarded — §13 invariant 1**
- Send a typed QUERY_INFO via `send_typed` (sets a known response); capture
  `r0[0]`/`r0[1]`.
- Build a 32-byte report with `data[0] != 0x81` (e.g. memset 0x55). Drive via
  `hid_notify(bad, 32)`.
- Assert `stub_get_last_response()[0] == r0[0] && [1] == r0[1]` — the response
  buffer is UNCHANGED (no `raw_hid_send` for the discard). Cite §13 inv.1.

**(multi-rep) Multi-report typed framing (APPLY_HOST_CONTEXT split) — §4.6**
- `count = 28`, all ids = 0; `layer = 224`, `flags = 0`. NONE of these is 0x03.
- Report 1 (32 B, NO ETX): `[0x81][0x9F][0xF0][0x05][224][0][28][id0..id24]`.
- Report 2 (with ETX): `[0x81][0x9F][id25][id26][id27][0x03][0…]`.
- Drive report 1 via `hid_notify(rep1, 32)`, then immediately report 2 via
  `hid_notify(rep2, 32)`.
- Assert `r[0] == NOTIFY_RESPONSE_MARKER` AND `r[1] == NOTIFY_CMD_APPLY_HOST_CONTEXT`
  (0x05 — the cmd-echo PROVES reassembly) AND `r[2] == 1` (ack) AND
  `stub_get_active_layer() == 224` (host layer set) AND `cb_mute_en == 1`
  (callback diff ran; id 0 enabled once). Cite §4.6 (ETX-framed, may span reports).
  Resets `cb_mute_en/dis` before sending.

**DO NOT** (out of scope — siblings): SET_OS / APPLY_HOST_CONTEXT stack/replace
test blocks → P1.M3.T1.S3; `run_notifier_stub_tests.sh` extension (3rd binary) →
P1.M3.T2.S1; README sync → P1.M3.T3.S1; any `notifier.c`/`notifier.h`/
`qmk_stubs/*` change → P1.M2 / P1.M3.T1.S1. Do NOT add a multi-report helper
function (inline the two reports — keeps the diff in `main()`). Do NOT use SET_OS
for multi-report (cmd_id 0x03 == ETX — S3's blocker).

### Success Criteria

- [ ] `test_notifier_host.c` compiles clean (`-Wall -Wextra -std=c99`, zero warnings).
- [ ] (coexist-i)(a) "firefox" → `r[0] != 0x51 && r[0] == 0`.
- [ ] (coexist-i)(b) "neovide" → `r[0] != 0x51 && r[0] == 1 && board_cmd_en==1 && active==5`.
- [ ] (coexist-ii) non-magic report → response `[0],[1]` UNCHANGED.
- [ ] (multi-rep) two-report AHC → `r[0]==0x51 && r[1]==0x05 && r[2]==1 && active==224 && cb_mute_en==1`.
- [ ] All three new blocks PASS (0 `FAIL:` lines from them); S2/S3 blocks unaffected.
- [ ] Mode-A comments cite §4.6 / §13 for each block.
- [ ] No file other than `test_notifier_host.c` is modified; no new file-scope declarations.

## All Needed Context

### Context Completeness Check

**Pass.** Every assertion is traced from the landed `notifier.c` (`hid_notify`
magic guard, `typed_mode` discriminator, reassembly byte loop, ETX dispatch,
`handle_typed_command` AHC case) AND confirmed by empirical probe
(`/tmp/probe_s4.c` → research/findings.md F4-1..F4-4). The test pattern (file-scope
`DEFINE_*`, `CK`, `send_typed`, manual-extern stub accessors) is copied verbatim
from the S2 file / `test_notifier_dispatch.c`. The exact byte layouts of the two
multi-report frames are spelled out (split math verified). The parallel-merge
strategy with S3 (stable anchor = summary `printf`) is documented. The ETX-
collision constraint (why AHC 0x05, not SET_OS 0x03) is documented with probe
evidence. An implementer with this PRP + repo access can append the three blocks
and prove 0 FAIL.

### Documentation & References

```yaml
# MUST READ — the wire contract this test gates (canonical owner)
- file: PRD.md
  section: "### 4.6 Typed-command namespace (canonical owner)"
  why: "Discriminator 0xF0 at data[2] (first report only); '0xF0 can never begin a
        real matched string (sanitizer allows only 0x20-0x7E), so a host that sends
        only legacy strings coexists unchanged'; 'Typed commands are ETX-framed
        and may span multiple 32-byte reports, chunked at 30 payload bytes/report';
        command table (0x05 APPLY_HOST_CONTEXT, args [layer][flags][count][ids]);
        [0x51][cmd_id][payload] response. These are the EXACT properties the
        coexistence + multi-report blocks encode."
  critical: "Multi-report framing: '[0x81][0x9F][0xF0][cmd_id][ args… ][0x03],
        chunked at 30 payload bytes/report'. The discriminator 0xF0 appears ONLY in
        the first report (continuation reports are pure payload). Legacy strings:
        response is [matched(0|1)]; typed response is [0x51][cmd_id][payload]."

# MUST READ — backward-compat + coexistence guarantee (the coexist-i/ii basis)
- file: PRD.md
  section: "## 14. Host-Side Rules & Typed Commands"
  why: "'legacy string sends have data[2] = a printable char (never 0xF0), so the
        dispatch is transparent to keymaps that don't use host rules.' and '0xF0
        can never begin a real matched string (sanitizer allows only 0x20-0x7E), so
        a string-only host coexists'. And §13 invariant 1: magic header is exactly
        0x81 0x9F; the coexistence guard checks data[0]==0x81 && data[1]==0x9F."

# MUST READ — the implementation under test (P1.M2 — LANDED)
- file: notifier.c
  section: "hid_notify (magic guard + typed_mode discriminator + reassembly loop +
            ETX dispatch) + handle_typed_command (APPLY_HOST_CONTEXT case) +
            set_host_layer + apply_host_callbacks"
  why: "hid_notify: FIRST statement `if (length < 2 || data[0]!=0x81 ||
        data[1]!=0x9F) return;` (discards non-magic, no raw_hid_send). Then
        `if (msg_index==0 && length>=3 && data[2]==0xF0) typed_mode=true;`. Then
        `data+=2; length-=2;` (magic stripped on EVERY report). Byte loop: append
        to msg_buffer until ETX (0x03); at ETX, if typed_mode -> handle_typed_command
        else sanitize+process_full_message; reset msg_index/dropping/typed_mode.
        AHC case: data[2]=layer, data[3]=flags, data[4]=count, data[5..]=ids;
        count clamped to MSG_BUFFER_SIZE-5; if(flags&1){deactivate+disable;}
        set_host_layer(layer); apply_host_callbacks(ids,count); ->[0x51][0x05][1]."
  critical: "The byte loop treats ANY 0x03 byte as ETX — including a typed cmd_id
        (SET_OS 0x03) or any binary arg byte == 3. This is S3's blocker. The
        multi-report test MUST use a cmd_id != 0x03 (AHC 0x05) and args with no
        0x03 byte (layer=224, flags=0, count=28, ids=0 — all safe). The magic
        header is checked on EVERY report, so BOTH multi-report frames must carry
        [0x81][0x9F]. typed_mode persists across hid_notify calls (set on report 1,
        read at ETX in report 2)."

# MUST READ — coexistence + multi-report findings (this task's verified research)
- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### F5 — Existing tests never send 0xF0" + "### F7 — Multi-report typed framing uses existing reassembly"
  why: "F5: all existing test cases send legacy strings with printable data[2]; the
        typed routing branch never fires for them — the invariant coexist-i encodes.
        F7: the msg_buffer/msg_index/dropping reassembly already handles multi-report;
        typed commands reuse it; the 0xF0 discriminator is at data[2] only in the
        FIRST report; typed_mode governs routing. Both findings are the basis for
        these tests."

# MUST READ — the API surface (constants consumed by the test)
- file: notifier.h
  why: "NOTIFY_CMD_DISCRIMINATOR(0xF0), NOTIFY_RESPONSE_MARKER(0x51),
        NOTIFY_CMD_QUERY_INFO(0x01), NOTIFY_CMD_APPLY_HOST_CONTEXT(0x05),
        ETX_TERMINATOR[0](0x03), HOST_LAYER_BASE(224). Use the NOTIFY_* constants
        (no magic numbers except 0x81/0x9F magic header, layer 224, count 28, and
        char literals 'f'/'n'/etc.)."

# MUST READ — the file being EXTENDED (the S2 contract + S3's parallel additions)
- file: plan/003_16d737de7a3e/P1M3T1S2/PRP.md
  why: "Defines test_notifier_host.c's scaffolding that S4 REUSES: include block,
        manual externs (hid_notify/process_full_message/stub_get_active_layer/
        stub_get_last_response), DEFINE_HOST_CALLBACKS{mute,layout},
        DEFINE_SERIAL_COMMANDS{neovide->board_cmd}, DEFINE_SERIAL_LAYERS{neovide->5},
        cb_mute_*/cb_layout_* flags, board_cmd_en/dis flags, CK macro, g_pass/g_fail,
        send_typed(cmd,args,nargs) helper, the summary printf + return g_fail?1:0.
        S4 ADDS three blocks to main() and reuses ALL of this — no new declarations."
  critical: "S3 (parallel) adds DEFINE_SERIAL_*_OS(OS_MACOS) + sequence stamps + its
        8 SET_OS/AHC blocks. S4 must not duplicate or conflict with those. S4's
        blocks anchor on the summary printf (stable); S3's blocks anchor there too;
        both compose. S4 does NOT touch S3's sequence-stamp vars."

# MUST READ — the S3 PRP (parallel sibling — understand what coexists in the file)
- file: plan/003_16d737de7a3e/P1M3T1S3/PRP.md
  why: "Defines S3's additions (OS_MACOS maps, sequence stamps, SET_OS i-iv, AHC
        v-viii) and its CRITICAL BLOCKER (SET_OS cmd_id 0x03 == ETX 0x03). S4's
        multi-report test deliberately uses AHC (0x05) to AVOID that blocker. S4
        must leave S3's blocks intact."

# MUST READ — the precedent (file-scope DEFINE_* + hid_notify legacy-string driving)
- file: test_notifier_dispatch.c
  section: "hid_notify reassembly + ack + coexistence guard block"
  why: "Shows the EXACT pattern for building a legacy-string report and driving it
        through hid_notify: `rep[0]=0x81; rep[1]=0x9F; memcpy(rep+2, payload,
        len); hid_notify(rep,32);` and the non-magic discard (`bad[0]=0xAB;
        hid_notify(bad,32);` -> ignored). S4's coexist-i/ii blocks follow this."

# MUST READ — the stub observables (LANDED: S1 + existing)
- file: qmk_stubs/qmk_stubs.c
  why: "stub_get_last_response() returns const uint8_t* into a file-static 32-byte
        buffer captured by raw_hid_send (last-write-wins; read IMMEDIATELY after the
        hid_notify that triggers the response). stub_get_active_layer() returns
        g_active_layer (SINGLE layer model: layer_on(X)->X; layer_off->255)."

# MUST READ — THIS TASK'S verified research (the probe evidence + byte layouts)
- file: plan/003_16d737de7a3e/P1M3T1S4/research/findings.md
  why: "F4-1..F4-4: empirical probe evidence for every assertion (all PASS); the
        coexistence code path; the non-magic discard; the multi-report reassembly
        trace + split math; the ETX-collision constraint (why AHC not SET_OS).
        F4-5: robustness to S3's state mutations. F4-6: parallel-merge strategy.
        F4-7: verified assertion values table. READ THIS BEFORE implementing."
```

### Current Codebase tree (relevant subset)

```bash
notifier.c              # implementation under test (P1.M2 — hid_notify magic guard + typed_mode + reassembly + AHC handler)
notifier.h              # NOTIFY_* constants + DEFINE_* macros
qmk_stubs/
  qmk_stubs.c           # stubs: layer_on/off (SINGLE g_active_layer), raw_hid_send,
                         #   stub_get_active_layer, stub_get_last_response (S1, LANDED)
  os_detection.h        # os_variant_t (OS_UNSURE=0...)
test_notifier_host.c    # CREATED by S2; EXTENDED by S3 (parallel) — THIS TASK APPENDS 3 blocks
test_notifier_dispatch.c# PRECEDENT — legacy-string hid_notify driving + non-magic discard
test_notifier_os.c      # PRECEDENT — CK macro + manual-extern stub accessors
run_notifier_stub_tests.sh # build/gate chain (extended in P1.M3.T2.S1 — NOT here)
```

### Desired Codebase tree with files to be modified

```bash
test_notifier_host.c   # MODIFIED — +3 test blocks appended to main() (coexist-i, coexist-ii, multi-rep)
# nothing else changes
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (multi-report cmd choice): the reassembly byte loop treats ANY 0x03
// byte as ETX. SET_OS's cmd_id is 0x03 (S3's blocker) — multi-report SET_OS is
// impossible. The multi-report test uses APPLY_HOST_CONTEXT (cmd_id 0x05, != ETX)
// and args with NO 0x03 byte: layer=224(0xE0), flags=0, count=28(0x1C), ids=0.
// Do NOT use layer/flags/count/id == 3 anywhere in the multi-report test.

// CRITICAL (multi-report framing): BOTH reports must carry the [0x81][0x9F] magic
// header — hid_notify checks+strips it on EVERY report. The 0xF0 discriminator is
// at data[2] of the FIRST report ONLY; report 2's data[2] is payload (id25).
// typed_mode is set on report 1 (msg_index==0) and read at ETX in report 2; it
// persists across the two hid_notify calls. Report 1 has NO ETX; report 2 ends
// with 0x03. verified: after report1 msg_index=30, after report2-ETX msg_index=0.

// CRITICAL (the r[1]==0x05 cmd-echo is the proof): if multi-report reassembly
// failed (e.g. msg_index reset between reports), r[1] would be a garbage/stale
// byte, not 0x05. Asserting r[1]==NOTIFY_CMD_APPLY_HOST_CONTEXT is the load-bearing
// reassembly check.

// CRITICAL (stub single-layer model): qmk_stubs models QMK layers as a SINGLE
// g_active_layer: layer_on(X)->X (overwrites); layer_off(X)->255 (always). So the
// multi-report AHC sets active=224 (host layer wins; any prior board layer is
// shadowed). Assert stub_get_active_layer()==224. (clear_board=0 in this test, so
// board is not torn down — but the stub can't show two layers anyway.)

// GOTCHA (coexist-ii last-write-wins): stub_get_last_response() is overwritten on
// EVERY raw_hid_send. To prove a non-magic report is discarded, FIRST send a typed
// command to set a known response, capture r0[0]/r0[1], THEN send the non-magic
// report, THEN assert the buffer is UNCHANGED. (The non-magic report returns early
// in hid_notify BEFORE raw_hid_send, so no overwrite occurs.)

// GOTCHA (coexist-i "neovide" matches default map regardless of current_os): if
// S3 left current_os==OS_MACOS, the OS_MACOS "iTerm" map is scanned first but
// doesn't match "neovide", so the default "neovide" map wins (board_cmd_en==1,
// layer 5). No current_os reset is REQUIRED; each block resets the FLAGS it
// asserts (board_cmd_en/dis, cb_mute_en/dis) at its start.

// GOTCHA (parallel merge with S3): S3 also appends blocks before the summary
// printf. Anchor S4's edit on the summary printf line (stable — S3 doesn't alter
// it). S4's blocks go AFTER S3's, BEFORE the summary. The S2 "side-effect-free"
// CK (cb_*_en==0) sits BEFORE S3/S4 blocks, so callback-firing in S3/S4 doesn't
// break it.

// GOTCHA (no new helper): inline the two multi-report frames (uint8_t rep1[32]/
// rep2[32] in the block scope) — do NOT add a send_typed_multi helper. Keeps the
// diff purely in main() and avoids any merge surface with S3.

// GOTCHA: avoid -Wmissing-field-initializers is N/A here (no new DEFINE_* rows).
// Avoid compound literals for the id list — but here ids are all 0 written via a
// memset+explicit loop, so no literal. Use NOTIFY_* constants (not magic 0x05/0x51).

// GOTCHA: reset flags at the start of EACH block that reads them (board_cmd_en/dis
// for coexist-i; cb_mute_en/dis for multi-rep) so blocks are independent and
// asserts are unambiguous. (coexist-ii reads no flags.)
```

## Implementation Blueprint

### Data models and structure

**None created.** S4 consumes existing scaffolding (S2's `board_cmd_*`/`cb_*`
flags, `send_typed` helper, `CK` macro, manual-extern stub accessors, `DEFINE_*`
maps) and the `NOTIFY_*` constants. The multi-report block uses local
`uint8_t rep1[32]`/`rep2[32]` buffers. **No new file-scope declarations.**

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY test_notifier_host.c — APPEND (coexist-i) legacy-string coexistence block
  - INSERT immediately before the summary printf line (stable anchor; composes
    with S3's parallel insertion there). Open a new { } scope.
  - (coexist-i)(a) "firefox" (no-match legacy string):
      uint8_t rep[32]; memset(rep, 0, sizeof(rep));
      rep[0]=0x81; rep[1]=0x9F;                          /* §13 inv.1 magic header */
      rep[2]='f'; rep[3]='i'; rep[4]='r'; rep[5]='e';    /* data[2]='f'=0x66 (!=0xF0) */
      rep[6]='f'; rep[7]='o'; rep[8]='x'; rep[9]=ETX_TERMINATOR[0];
      hid_notify(rep, 32);
      const uint8_t *r = stub_get_last_response();
      CK(r[0] != NOTIFY_RESPONSE_MARKER, "(coexist-i)(a) legacy 'firefox' NOT routed to typed (r[0]!=0x51) [§4.6/F5]");
      CK(r[0] == 0,                      "(coexist-i)(a) legacy 'firefox' no-match ack=0 (process_full_message ran) [§13]");
  - (coexist-i)(b) "neovide" (match legacy string — full side-effect proof):
      board_cmd_en = board_cmd_dis = 0;
      memset(rep, 0, sizeof(rep));
      rep[0]=0x81; rep[1]=0x9F;
      rep[2]='n'; rep[3]='e'; rep[4]='o'; rep[5]='v'; rep[6]='i'; rep[7]='d'; rep[8]='e';
      rep[9]=ETX_TERMINATOR[0];
      hid_notify(rep, 32);
      const uint8_t *r2 = stub_get_last_response();
      CK(r2[0] != NOTIFY_RESPONSE_MARKER, "(coexist-i)(b) legacy 'neovide' NOT routed to typed (r[0]!=0x51) [§4.6/F5]");
      CK(r2[0] == 1,                      "(coexist-i)(b) legacy 'neovide' match ack=1 [§13]");
      CK(board_cmd_en == 1,               "(coexist-i)(b) legacy dispatch fired board on_enable (process_full_message intact) [§13]");
      CK(stub_get_active_layer() == 5,    "(coexist-i)(b) legacy dispatch activated board layer 5 [§13]");
  - WHY: "firefox" (no match) + "neovide" (match) together prove the FULL legacy
    path (sanitize+process_full_message, disable/scan/deactivate/activate/enable)
    is intact ALONGSIDE the typed path, and that a printable data[2] never trips
    the 0xF0 discriminator. (Mode-A comment cites §4.6 + §13 + F5.)

Task 2: MODIFY test_notifier_host.c — APPEND (coexist-ii) non-magic-discard block
  - INSERT after Task 1's block, before the summary printf. New { } scope.
      const uint8_t *r0 = send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);  /* known response */
      CK(r0[0] == NOTIFY_RESPONSE_MARKER, "(coexist-ii) setup: QUERY_INFO set a known typed response [§4.6]");
      uint8_t marker0 = r0[0], echo0 = r0[1];
      uint8_t bad[32]; memset(bad, 0x55, sizeof(bad));    /* data[0]=0x55 != 0x81 */
      hid_notify(bad, 32);                                 /* discarded BEFORE raw_hid_send */
      const uint8_t *r1 = stub_get_last_response();
      CK(r1[0] == marker0 && r1[1] == echo0,
                                       "(coexist-ii) non-magic report discarded: response UNCHANGED (no raw_hid_send) [§13 inv.1]");
  - WHY: hid_notify's first statement discards non-magic reports before any
    raw_hid_send, so the captured response is unchanged. The CK reads marker0/
    echo0 (no unused-variable warnings). (Mode-A comment cites §13 inv.1.)

Task 3: MODIFY test_notifier_host.c — APPEND (multi-rep) two-report AHC block
  - INSERT after Task 2's block, before the summary printf. New { } scope.
  - RESET cb flags, build + drive the two reports:
      cb_mute_en = cb_mute_dis = 0;
      /* Report 1 (32 B, NO ETX): [0x81][0x9F][0xF0][0x05][224][0][28][id0..id24] */
      uint8_t rep1[32]; memset(rep1, 0, sizeof(rep1));
      rep1[0]=0x81; rep1[1]=0x9F; rep1[2]=NOTIFY_CMD_DISCRIMINATOR;
      rep1[3]=NOTIFY_CMD_APPLY_HOST_CONTEXT;   /* 0x05 (chosen: != ETX 0x03) */
      rep1[4]=224;                              /* layer (HOST_LAYER_BASE; 0xE0, !=0x03) */
      rep1[5]=0x00;                             /* flags (clear_board=0) */
      rep1[6]=28;                               /* count (0x1C, !=0x03) -> forces 2 reports */
      /* rep1[7..31] already 0 == id0..id24 (25 ids, all 0, none == 0x03) */
      hid_notify(rep1, 32);
      /* Report 2 (with ETX): [0x81][0x9F][id25][id26][id27][0x03] */
      uint8_t rep2[32]; memset(rep2, 0, sizeof(rep2));
      rep2[0]=0x81; rep2[1]=0x9F;
      /* rep2[2..4] already 0 == id25,id26,id27 */
      rep2[5]=ETX_TERMINATOR[0];               /* 0x03 terminates + dispatches */
      hid_notify(rep2, 32);
      const uint8_t *r = stub_get_last_response();
      CK(r[0] == NOTIFY_RESPONSE_MARKER,              "(multi-rep) two-report AHC r[0]=0x51 marker [§4.6]");
      CK(r[1] == NOTIFY_CMD_APPLY_HOST_CONTEXT,        "(multi-rep) two-report AHC r[1]=0x05 cmd echo (reassembly OK) [§4.6]");
      CK(r[2] == 1,                                    "(multi-rep) two-report AHC r[2]=ack=1 [§4.6]");
      CK(stub_get_active_layer() == 224,               "(multi-rep) two-report AHC host layer 224 active (set_host_layer ran) [§4.6]");
      CK(cb_mute_en == 1,                              "(multi-rep) two-report AHC callback diff ran (id 0 enabled once) [§4.6]");
  - WHY: count=28 ids spans two reports (report1 holds 25 ids, report2 holds 3).
    All bytes != 0x03 so ETX doesn't fire early. r[1]==0x05 is the load-bearing
    reassembly proof (report1's cmd_id persisted into the reassembled buffer).
    active==224 + cb_mute_en==1 prove the full AHC dispatch ran. (Mode-A comment
    cites §4.6 ETX-framing-may-span-reports.)

Task 4: VALIDATE — compile (strict) + run + confirm 0 FAIL (see Validation Loop)
  - COMPILE strict: gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
        -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -o /tmp/tnh
  - RUN: /tmp/tnh ; fails=$(/tmp/tnh 2>/dev/null | grep -c '^FAIL:' || true)
  - EXPECT: the three new blocks (coexist-i/ii, multi-rep) all PASS. S2's QUERY
        blocks still PASS. S3's AHC blocks still PASS; S3's SET_OS blocks still
        FAIL with its documented blocker (UNRELATED to S4). The new blocks add
        ZERO FAIL lines. See "Expected Test Results".
  - CLEAN: rm -f /tmp/tnh
```

> Tasks 1-3 are a single `edit` call (one contiguous insertion before the summary
> printf). Task 4 is validation.

### Reference: the multi-report frame byte layouts (do not get this wrong)

```c
/* count=28 ids (all 0), layer=224, flags=0. NONE of these bytes is 0x03.
 *
 * Report 1 (32 bytes, NO ETX):
 *   [0] 0x81  [1] 0x9F      magic header (§13 inv.1) — checked+stripped every report
 *   [2] 0xF0                discriminator (typed_mode set: msg_index==0 && data[2]==0xF0)
 *   [3] 0x05                cmd_id = APPLY_HOST_CONTEXT (chosen != ETX 0x03)
 *   [4] 224 (0xE0)          layer (HOST_LAYER_BASE)
 *   [5] 0x00                flags (clear_board=0)
 *   [6] 28 (0x1C)           count (>25 => spans reports)
 *   [7..31] 0 x25           id0..id24
 *   -> after magic strip: 30 payload bytes appended; msg_index=30, typed_mode=true
 *
 * Report 2 (32 bytes, with ETX):
 *   [0] 0x81  [1] 0x9F      magic header (REQUIRED on continuation report)
 *   [2] 0   [3] 0  [4] 0    id25, id26, id27 (continuation payload; data[2] NOT a discriminator here)
 *   [5] 0x03                ETX -> dispatch handle_typed_command(msg_buffer)
 *   [6..31] 0               ignored (post-ETX; byte loop broke)
 *   -> after magic strip: [0,0,0] appended (msg_index=33), then 0x03 dispatches.
 *
 * Reassembled msg_buffer (33 bytes): [0xF0,0x05,224,0,28, 0,0,...,0 (28 ids)]
 *   data[0]=0xF0, data[1]=0x05(cmd), data[2]=224(layer), data[3]=0(flags),
 *   data[4]=28(count), data[5..]=28 zeros (ids).
 * -> AHC handler: set_host_layer(224) -> active=224; apply_host_callbacks(28 zeros,28)
 *    -> Phase2 enables id 0 once (cb_mute_en=1). Response [0x51][0x05][0x01].
 *
 * VERIFIED by probe: r[0]=81 r[1]=5 r[2]=1 active=224 cb_mute_en=1. (decimal)
 */
```

### Implementation Patterns & Key Details

```c
// PATTERN: reuse S2's send_typed + CK + manual externs verbatim (no new scaffolding).
// The three new blocks go inside main(), in their own { } scopes, immediately
// before the summary printf line (stable anchor; composes with S3's blocks).

// PATTERN (coexist-i): build the legacy report like test_notifier_dispatch.c does
// (rep[0]=0x81; rep[1]=0x9F; payload chars; ETX; hid_notify(rep,32)). The legacy
// ack is response[0] = match (0 or 1) — assert it is NOT the typed marker 0x51.

// PATTERN (coexist-ii): the non-magic discard is proven by an UNCHANGED response
// buffer (capture before, compare after). hid_notify returns before raw_hid_send.

// PATTERN (multi-rep): TWO hid_notify calls back-to-back (report1 then report2).
// Report1 has NO ETX (leaves msg_index=30, typed_mode=true). Report2 ends with
// 0x03 (dispatches). Assert r[1]==0x05 (cmd echo = reassembly proof).

// CRITICAL (no 0x03 byte): every byte in the multi-report frames after the magic
// strip must be != 0x03: 0xF0, 0x05, 224, 0, 28, and all-zero ids — all safe.

// PATTERN: reset the flags each block reads (board_cmd_en/dis; cb_mute_en/dis) so
// blocks are independent of S3's prior mutations.

// PATTERN: every CK name carries its [§4.6]/[§13]/[F5] tag (Mode-A docs).
```

### Integration Points

```yaml
NO database / config / route / migration / firmware changes. One test file edited.

BUILD/LINK:
  - test_notifier_host.c is MODIFIED (S2 created, S3 extends in parallel). It
    compiles + links with the SAME flags S2/S3 used. No new build flag, no new TU.

SYMBOL NAMESPACE:
  - S4 adds NO file-scope symbols (reuses S2's board_cmd_* / cb_* flags + send_typed).
    The three blocks are local-scope inside main(). Zero namespace collision risk.

SCOPE BOUNDARY (do NOT cross — siblings own these):
  - SET_OS / APPLY_HOST_CONTEXT stack/replace test blocks           -> P1.M3.T1.S3
  - run_notifier_stub_tests.sh extension (3rd binary)               -> P1.M3.T2.S1
  - qmk_stubs.c / notifier.c / notifier.h                           -> P1.M2 / S1
  - the SET_OS ETX-collision FIX                                    -> P1.M2 (S3's blocker)
  - README sync                                                     -> P1.M3.T3.S1
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Strict compile (the runner-readiness gate; ZERO warnings required):
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c \
    -o /tmp/test_notifier_host && echo "STRICT COMPILE OK"
# Expected: "STRICT COMPILE OK" with NO warnings. If you see
# -Wunused-but-set-variable, a flag you set isn't read — read it in a CK assertion.
# (S4 sets board_cmd_en/dis + cb_mute_en/dis and reads them all, so none unused.)
```

### Level 2: Run + Interpret (Component Validation)

```bash
/tmp/test_notifier_host
echo "exit=$?"
fails=$(/tmp/test_notifier_host 2>/dev/null | grep -c '^FAIL:' || true)
echo "total FAIL: lines = $fails"
# EXPECTED (the three NEW S4 blocks all PASS):
#   (coexist-i)(a) "firefox":    PASS  (r[0]==0, !=0x51)
#   (coexist-i)(b) "neovide":    PASS  (r[0]==1, board_cmd_en==1, active==5)
#   (coexist-ii) non-magic:      PASS  (response unchanged)
#   (multi-rep) two-report AHC:  PASS  (r[1]==0x05, r[2]==1, active==224, cb_mute_en==1)
# Plus S2's QUERY_INFO/QUERY_CALLBACK PASS and S3's AHC PASS.
# NOTE: S3's SET_OS blocks (i-iv) FAIL with S3's documented cmd_id-0x03==ETX blocker —
# that is UNRELATED to S4 (S4 adds no SET_OS test). The S4 blocks add ZERO FAIL lines.
# To isolate S4's contribution: grep for '(coexist-' and '(multi-rep)' in the output.
rm -f /tmp/test_notifier_host
```

### Level 3: Regression (no other suite touched)

```bash
# This task edits ONE test file (additive within it). The existing suites must stay green:
./run_notifier_stub_tests.sh
# Expected: "notifier dispatch fails=0", "notifier os fails=0",
# "✓ notifier stub-compile gate PASSED", exit 0. (test_notifier_host is NOT yet wired
# into this script — that is P1.M3.T2.S1.)
./run_all_tests.sh
# Expected: all 9 pattern-match suites green (this task changed no matcher code).
```

### Level 4: Probe confirmation (proves the S4 assertions against real notifier.c)

```bash
# Re-run the probe that verified these assertions (research/findings.md F4-1..F4-4).
# It is a standalone harness mirroring test_notifier_host.c's setup. Build in /tmp
# (do NOT add it to the repo):
# (the probe is documented in research/findings.md; it sends the three scenarios and
#  prints verified values: r[0]=0/'firefox', r[0]=1/'neovide', unchanged/non-magic,
#  r[1]=5/multi-report. Re-running confirms the notifier.c behavior is unchanged.)
```

### Expected Test Results (authoritative — read before judging pass/fail)

| Block | Scenario | Expected | Why |
|-------|----------|----------|-----|
| (coexist-i)(a) | "firefox" no-match | **PASS** (r[0]==0, !=0x51) | printable data[2] → legacy path; no map match |
| (coexist-i)(b) | "neovide" match | **PASS** (r[0]==1, cmd_en==1, active==5) | legacy path full dispatch; default map match |
| (coexist-ii) | non-magic report | **PASS** (response unchanged) | magic guard discards before raw_hid_send |
| (multi-rep) | two-report AHC | **PASS** (r[1]==0x05, r[2]==1, active==224, cb_mute_en==1) | reassembly works; AHC cmd 0x05 != ETX |
| S2 blocks | QUERY_INFO/CALLBACK | **PASS** | no regression |
| S3 AHC blocks | stack/replace | **PASS** | no regression |
| S3 SET_OS blocks | (i-iv) | **FAIL** (S3's blocker) | UNRELATED to S4 — cmd_id 0x03 == ETX |

**The task's OUTPUT criterion ("coexistence/backward-compat + multi-report … all
pass with 0 FAIL") IS MET for the S4 categories.** The S3 SET_OS FAILs are a
separate, pre-existing, documented blocker (not introduced or worsened by S4).

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 strict compile (`-Wall -Wextra -std=c99`) → ZERO warnings.
- [ ] Level 2 run: the three NEW blocks (coexist-i/ii, multi-rep) all PASS.
- [ ] Level 2 run: S2 QUERY blocks still PASS (no regression).
- [ ] Level 2 run: S4 adds ZERO `FAIL:` lines (S3 SET_OS FAILs are pre-existing/unrelated).
- [ ] Level 3 `./run_notifier_stub_tests.sh` still PASSED; `./run_all_tests.sh` green.

### Feature Validation

- [ ] (coexist-i)(a) "firefox" → `r[0] != 0x51 && r[0] == 0` (§4.6/F5).
- [ ] (coexist-i)(b) "neovide" → `r[0] != 0x51 && r[0] == 1 && board_cmd_en==1 && active==5` (§4.6/§13).
- [ ] (coexist-ii) non-magic report → response `[0],[1]` UNCHANGED (§13 inv.1).
- [ ] (multi-rep) two-report AHC → `r[0]==0x51 && r[1]==0x05 && r[2]==1 && active==224 && cb_mute_en==1` (§4.6).
- [ ] Mode-A comments map each block to §4.6 / §13 / F5.

### Code Quality Validation

- [ ] Follows S2's pattern verbatim (CK macro, send_typed, manual externs, summary, return).
- [ ] Uses NOTIFY_* constants (no magic numbers except 0x81/0x9F, layer 224, count 28, char literals).
- [ ] Multi-report uses AHC (0x05), NOT SET_OS (0x03) — and no arg byte == 0x03.
- [ ] No new file-scope declarations; no new helper (multi-report inlined).
- [ ] Each block resets the flags it reads; blocks independent of S3's mutations.
- [ ] Diff is purely the appended blocks in `main()` (no other file touched).

### Documentation & Deployment

- [ ] Mode-A comments ride WITH the work (DOCS §5) — each block's [§x.y] tag.
- [ ] Cite §4.6 (0xF0 never begins a real matched string; sanitizer 0x20–0x7E; ETX-framed, may span reports) + §13 inv.1 (magic header).
- [ ] No README change (README sync is P1.M3.T3.S1). No new env vars / config.

---

## Anti-Patterns to Avoid

- ❌ Don't use SET_OS (0x03) for the multi-report test — its cmd_id collides with
  ETX (S3's blocker). Use APPLY_HOST_CONTEXT (0x05); its cmd_id and args avoid 0x03.
- ❌ Don't put any byte == 0x03 in a multi-report payload (layer/flags/count/id) —
  the reassembly loop treats it as ETX and dispatches early. Use 224/0/28/0.
- ❌ Don't omit the magic header `[0x81][0x9F]` from the SECOND multi-report frame
  — hid_notify checks+strips it on EVERY report; without it, the continuation is
  discarded and the message never completes (r[1] would be stale, not 0x05).
- ❌ Don't add a `send_typed_multi` helper or any new file-scope declaration —
  inline the two reports to keep the diff purely in `main()` (clean merge with S3).
- ❌ Don't forget to RESET the flags each block reads (board_cmd_en/dis, cb_mute_en/dis)
  — S3's prior blocks mutate them; without reset, asserts are ambiguous.
- ❌ Don't assert `stub_get_active_layer()` to prove the non-magic discard — the
  stub layer is unaffected either way. Assert the RESPONSE buffer is unchanged.
- ❌ Don't modify `notifier.c`/`notifier.h`/`qmk_stubs/*`/`run_notifier_stub_tests.sh`/
  any other test file — this task edits ONE file. The SET_OS framing fix is P1.M2's job.
- ❌ Don't hardcode 0x05/0x51/0xF0/0x03/224 — use NOTIFY_* constants (224 and 28 are
  the only literals; 224 = HOST_LAYER_BASE, 28 = chosen id count > 25 to span reports).

---

## Confidence Score

**Confidence: 10/10.** All three blocks (coexist-i, coexist-ii, multi-rep) are
EMPIRICALLY VERIFIED PASSING against the real `notifier.c` via the probe in
research/findings.md (F4-1..F4-4). Every assertion value is traced from the landed
`notifier.c` code AND confirmed by execution. The test pattern is copied verbatim
from S2 / `test_notifier_dispatch.c`. The multi-report cmd choice (AHC 0x05) and
0x03-avoidance are documented with probe evidence. The parallel-merge strategy
(stable anchor) is documented. An implementer with this PRP + repo access appends
the three blocks and proves 0 FAIL from the S4 categories with certainty.