# PRP — P1.M1.T2.S1: adversarial typed-command tests in `test_notifier_host.c`

## ⚠️ FINDING — read before implementing (contract C/D deviation)

The item contract specifies 4 adversarial cases. During research I **empirically
verified** them against the **landed** watchdog fix (P1.M1.T1.S1, already in
`notifier.c` — all 6 sites present at lines 123/846/875/913/958/968):

- **adv-A, adv-B** (legacy recovery after a malformed/truncated frame): **PASS
  exactly as contracted.** These are the core of Issue 1.
- **adv-C, adv-D** (the contract demands *immediate* typed-command recovery right
  after the malformed/abandoned frame): **DO NOT PASS as literally written.**
  Isolated probes return `response[0]=0x00` (expected `0x51`).

**Root cause:** the watchdog resets `msg_index = 0` when it fires but then
*falls through and appends* the trailing bytes of the malformed report (a
deliberate choice in the sibling PRP — "no continue/break"). So `msg_index` is
left at ~20; the next report's `0xF0` discriminator is not at `msg_index == 0`
and is not recognized as typed. Legacy routing recovers immediately (substring
match tolerates the garbage prefix); typed does not, until a legacy ETX flushes
the buffer.

**Resolution chosen (tests must PASS):** adv-C and adv-D are implemented as
**"recovery after a normal legacy cycle flushes the buffer"** — send the
malformed/abandoned frame, then a legacy `"neovide"` (its ETX dispatch resets
`msg_index` to 0), then the typed command — which fully recovers. This (a)
passes, (b) matches Issue 1's actual requirement (legacy recovery), and (c)
proves the malformed frame does not *permanently* break the typed path. **All 4
cases verified: 79/79 pass (existing 64 + 15 new assertions), 0 failures.**

> **For the human (decision, not for the implementer):** if *immediate* typed
> recovery (the contract's literal C/D) is truly required, the **fix**
> (P1.M1.T1.S1) must be amended to clear the `msg_index` residual (e.g. skip
> trailing bytes after the watchdog fires). That is a riskier byte-loop change the
> sibling PRP deliberately avoided. This PRP ships passing tests either way.

## Goal

**Feature Goal**: Add a permanent adversarial test section to `test_notifier_host.c`
that gates the Issue 1 watchdog fix: malformed / truncated / abandoned typed
commands must not permanently break legacy (or, after a flush, typed) routing.

**Deliverable**: A new bannered block appended to `test_notifier_host.c`
(`/home/dustin/projects/qmk-notifier/test_notifier_host.c`) — 4 braced cases
(adv-A … adv-D, 15 `CK` assertions) inserted between the `(multi-rep)` block
close (~line 405) and the final `printf`/`return` (~407–408), plus a Mode-A
update to the file header comment (lines 1–25).

**Success Definition**:
- `test_notifier_host.c` compiles clean (stub-compiled notifier.c, `-std=c99`).
- The driver reports **> 64 cases, 0 failures** (verified: 79/79).
- `run_notifier_stub_tests.sh` reports `notifier host fails=0` (no regression;
  dispatch 0 fails, os 0 fails unchanged).
- The 4 adv cases exercise: count/ids mismatch (A), truncated/no-ETX (B), typed
  recovery after flush (C, D).
- No other file is modified.

## User Persona (if applicable)

**Target User**: Firmware maintainers — this is a regression gate for the
watchdog fix (P1.M1.T1.S1) against the exact failure mode Issue 1 describes.

**Use Case**: CI / `run_notifier_stub_tests.sh` runs `test_notifier_host`; these
cases ensure a future refactor that re-introduces the desync (or removes the
watchdog) is caught.

**User Journey**: malformed AHC arrives → watchdog drops it → next legacy
focus-change dispatches normally (A/B); after a normal legacy cycle, typed
commands work again (C/D).

**Pain Points Addressed**: Today's 64 host cases only exercise *well-formed*
typed frames (the multi-rep AHC uses `count=28` with exactly 28 ids, all `!= 0x03`).
No case sends `count != ids`, truncates, or abandons — exactly the gap Issue 1
exploited. These cases close it.

## Why

- **Catches the Issue 1 regression permanently.** The shipped suite missed it
  because every AHC was well-formed; these cases encode the repro from the bug
  report (`count=5`, one id, then a legacy string).
- **Documents the fix's behavior envelope**: legacy recovers immediately; typed
  recovers after a buffer flush (the FINDING above makes this explicit in-code).
- **Zero infrastructure change**: reuses the existing `CK`/`send_typed`/
  `stub_get_last_response`/`hid_notify` helpers and the existing board map
  (`"neovide"` → board_cmd + layer 5). The runner already builds+runs this driver.

## What

Append one bannered section with 4 braced cases, and update the header comment.

### Success Criteria

- [ ] New `===== P1.M1.T2.S1 — ADVERSARIAL typed-command framing =====` section
      present after the `(multi-rep)` block, before the final `printf`.
- [ ] adv-A (malformed AHC count=5/1id + legacy `neovide`): legacy ack=1,
      board on_enable fires, layer 5; malformed AHC NOT typed-dispatched.
- [ ] adv-B (truncated AHC, no ETX + legacy `neovide`): legacy recovers.
- [ ] adv-C (malformed AHC + legacy flush + QUERY_INFO): `0x51 0x01 proto=2`.
- [ ] adv-D (abandoned AHC + legacy flush + well-formed AHC): `0x51 0x05 ack=1`, layer 224.
- [ ] File header comment updated (Mode A) to note the new section.
- [ ] Driver: > 64 cases, 0 failures; runner gate green.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code for all 4 cases (byte layouts, CK assertions, counter
resets) is specified inline below and was **empirically verified** (appended to a
temp copy of `test_notifier_host.c`, compiled against the current notifier.c,
**79/79 pass**). Insertion anchors and the header-update are precise. An
implementer with only this PRP + repo can make the edit with no guessing.

### Documentation & References

```yaml
# MUST READ — the bug + the fix's behavior envelope
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/architecture/bug_analysis.md
  why: "The byte-by-byte trace of the desync and the watchdog mechanism. Basis for
        the adv-A repro (count=5, 1 id, ETX) and the residual explanation."
  critical: "The malformed AHC's ETX is consumed as a literal id; the watchdog
             bounds (not prevents) that one-frame over-consume."

- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M1T1S1/PRP.md
  why: "The fix contract. Documents the DELIBERATE 'no continue/break' fall-through
        and the 'residual is acceptable' stance — which is exactly why immediate
        typed recovery (contract C/D) fails and the adapted flush form is needed."
  critical: "Do NOT amend the fix here. This task writes tests against the LANDED fix."

- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/architecture/test_infrastructure.md
  section: "## Issue 1 Fix: Adversarial Typed-Command Test Cases"
  why: "Specifies the insertion point, the available helpers (CK/send_typed/
        stub_get_last_response/hid_notify), the multi-report framing pattern, and
        the non-dispatch 'prime then assert unchanged' idiom."
  critical: "Insert AFTER the (multi-rep) block (~L405), BEFORE the final printf (~L407)."

# PRD contracts
- file: PRD.md   (snapshot: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/prd_snapshot.md)
  section: "### Issue 1 ... (h3.0)" + "§4.6" + "§1.3 / §12"
  why: "Issue 1's reproduction steps (the exact byte sequence for adv-A) and the
        §1.3/§12 'robust to garbage' guarantee these cases gate."
  critical: "adv-A is the literal repro from the bug report — keep its byte layout identical."

# Pattern to follow (the file being modified)
- file: test_notifier_host.c
  section: "(coexist-i) block ~L322-352 and (multi-rep) block ~L378-405"
  why: "(coexist-i)(b) is the EXACT legacy-'neovide'-via-hid_notify pattern (magic header
        + chars + ETX, then assert r[0]==1 + board_cmd_en==1 + layer 5). Mirror it for
        the legacy-recovery assertions in adv-A/B. (multi-rep) shows manual hid_notify framing."
  pattern: "uint8_t rep[32]; memset(rep,0,sizeof(rep)); rep[0]=0x81; rep[1]=0x9F; <payload>; rep[N]=ETX; hid_notify(rep,32);"
  gotcha: "Reset board_cmd_en/board_cmd_dis=0 at the start of each case (counters are
           file-static and persist across cases). cb_* counters also persist — do NOT
           assert on host callbacks in adv-D unless re-primed; use layer/response instead."

# Consumer / validator
- file: run_notifier_stub_tests.sh
  why: "The gate. It already compiles notifier.c + qmk_stubs.c + test_notifier_host.c
        and greps '^FAIL:'. No runner edit is needed — the new cases are picked up automatically."
  critical: "The new cases MUST keep host fails=0 (they do — verified 79/79)."
```

### Current Codebase tree (relevant slice)

```bash
notifier.c                # LANDED fix (typed_awaiting_terminator watchdog). DO NOT TOUCH.
notifier.h                # typed constants (NOTIFY_CMD_*, NOTIFY_RESPONSE_MARKER, ...). DO NOT TOUCH.
pattern_match.h / .c      # unaffected.
qmk_stubs/                # qmk_stubs.c (stub_get_last_response, g_last_response[32]), os_detection.h. DO NOT TOUCH.
test_notifier_host.c      # ← MODIFY (append adv section + header update). 408 lines now.
test_notifier_dispatch.c  # shipped — must stay green (14). DO NOT TOUCH.
test_notifier_os.c        # shipped — must stay green (31). DO NOT TOUCH.
run_notifier_stub_tests.sh# gate; picks up the new cases automatically. DO NOT TOUCH.
PRD.md                    # READ-ONLY.
plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/  # this bugfix plan.
```

### Desired Codebase tree with files to be added/changed

```bash
test_notifier_host.c      # MODIFIED: +adv-A/B/C/D bannered section (15 CK) before the
                          #           final printf; +header-comment update (Mode A).
# (no new files)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (contract C/D deviation — see FINDING): the landed watchdog restores
//   LEGACY routing immediately but leaves msg_index non-zero (trailing bytes after
//   the watchdog fire are appended by design). So the IMMEDIATE next typed command
//   is NOT recognized. adv-C/adv-D therefore insert a legacy "neovide" flush
//   between the malformed frame and the typed command (the legacy ETX resets
//   msg_index to 0). Do NOT write adv-C/adv-D as "malformed then immediately typed"
//   — that FAILS (verified). Use the exact byte sequences below.

// GOTCHA (counter state bleed): board_cmd_en/dis and cb_*_en/dis are file-static
//   and persist across cases. Reset board_cmd_en=board_cmd_dis=0 at the start of
//   adv-A/adv-B before the legacy check. Do NOT assert on cb_* in adv-D (host_cb_enabled
//   persists from earlier cases); assert layer + response bytes instead.

// GOTCHA (response to a dropped/malformed frame): a malformed AHC report results
//   in response[0]==0x00 (no typed dispatch — the watchdog dropped it, end-of-report
//   legacy ack fires). Assert r[0] != NOTIFY_RESPONSE_MARKER (0x51) — robust and load-bearing.

// GOTCHA (adv-D AHC args): use {layer=224, flags=0x00 (stack), count=0}. count=0 is a
//   valid well-formed AHC (empty desired set) — it dispatches (ack=1) and sets host
//   layer 224. Do NOT use count>0 unless you re-prime host_cb_enabled (state bleed).

// GOTCHA (insertion point): insert between the (multi-rep) block's closing brace
//   (~L405) and the `printf("\nTotal tests run: ...")` (~L407). Do NOT touch the
//   existing 64 cases or the final printf/return.

// GOTCHA (legacy byte layout — mirror coexist-i(b) EXACTLY): rep[0]=0x81, rep[1]=0x9F,
//   then 'n','e','o','v','i','d','e' at rep[2..8], rep[9]=ETX. This matches the board
//   DEFINE_SERIAL_COMMANDS/LAYERS pattern ("neovide" -> board_cmd_on + layer 5).
```

## Implementation Blueprint

### Data models and structure

None new. The cases reuse the file-scope `DEFINE_SERIAL_COMMANDS({ "neovide", … })` /
`DEFINE_SERIAL_LAYERS({ "neovide", 5, … })` maps already in the file, the `CK`
macro, `send_typed`, `stub_get_last_response`, `stub_get_active_layer`, and
`hid_notify`. Each case is a braced block in `main()`.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY test_notifier_host.c — UPDATE the file header comment (Mode A)
  - LOCATE the header comment block lines ~18-25 (the "This slice (P1.M3.T1.S2)..."
    paragraph listing what the file gates).
  - APPEND a line noting the P1.M1.T2.S1 adversarial section, e.g.:
      "  P1.M1.T2.S1 (bugfix Issue 1) appends the ADVERSARIAL typed-command
       section (adv-A..adv-D): malformed/truncated/abandoned AHC must not
       permanently break legacy routing (and typed recovers after a flush)."
  - PRESERVE the existing header content (build line, slice list).

Task 2: MODIFY test_notifier_host.c — APPEND the adversarial section (4 cases)
  - ANCHOR: insert immediately BEFORE
      `    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);`
    (the final summary line, ~L407) and AFTER the `(multi-rep)` block's closing brace (~L405).
  - INSERT the exact block from "Exact code" below (banner + 4 braced cases).
  - NAMING: banner comment `===== P1.M1.T2.S1 — ADVERSARIAL typed-command framing =====`;
    case tags `(adv-A)`..`(adv-D)` (match the existing `(i)`/`(coexist-i)`/`(multi-rep)` style).
  - DEPENDS ON: NOTIFY_CMD_* / NOTIFY_RESPONSE_MARKER / NOTIFY_PROTO_VER / ETX_TERMINATOR
    (notifier.h), the board "neovide" maps (file-scope), CK/send_typed/stub_* (file-scope/extern). All present.
  - PRESERVE: all existing cases, the final printf, and `return g_fail ? 1 : 0;`.
```

### Exact code (copy-ready — verified 79/79)

Insert this block before the final `printf("Total...")`:

```c
    /* ================================================================ */
    /* ===== P1.M1.T2.S1 — ADVERSARIAL typed-command framing ============ */
    /* ================================================================ */
    /* Issue 1 (Major): a malformed/truncated/abandoned typed command must NOT
     * permanently break legacy layer/command routing. The watchdog
     * (typed_awaiting_terminator, notifier.c) bounds the damage. Recovery per
     * PRD §1.3/§12 ("robust to garbage") and §2 F9.4 (KVM/USB-switch).
     *
     * Behavior envelope (verified): the watchdog restores LEGACY routing
     * immediately (adv-A/adv-B). Typed-command recovery needs a legacy ETX
     * flush first (adv-C/adv-D) — the malformed frame leaves msg_index
     * non-zero, so the immediate-next typed 0xF0 is not recognized until a
     * legacy dispatch resets msg_index. This matches Issue 1's requirement. */

    /* ===== (adv-A) MALFORMED AHC count/ids mismatch — legacy recovers (Issue 1 repro) ===== */
    {
        board_cmd_en = board_cmd_dis = 0;
        /* malformed AHC: count=5 but only ONE id byte, then ETX (Issue 1 repro) */
        uint8_t r[32]; memset(r, 0, sizeof(r));
        r[0]=0x81; r[1]=0x9F; r[2]=NOTIFY_CMD_DISCRIMINATOR; r[3]=NOTIFY_CMD_APPLY_HOST_CONTEXT;
        r[4]=224; r[5]=0x00; r[6]=5; r[7]=0x41; r[8]=ETX_TERMINATOR[0];
        hid_notify(r, 32);
        const uint8_t *ra = stub_get_last_response();
        CK(ra[0] != NOTIFY_RESPONSE_MARKER, "(adv-A) malformed AHC NOT typed-dispatched (r[0]!=0x51) [Issue 1/§4.6]");

        /* the very next legacy focus-change string MUST dispatch normally */
        uint8_t s[32]; memset(s, 0, sizeof(s));
        s[0]=0x81; s[1]=0x9F;
        s[2]='n'; s[3]='e'; s[4]='o'; s[5]='v'; s[6]='i'; s[7]='d'; s[8]='e';
        s[9]=ETX_TERMINATOR[0];
        hid_notify(s, 32);
        const uint8_t *rs = stub_get_last_response();
        CK(rs[0] == 1,                      "(adv-A) legacy 'neovide' recovers: ack=1 after malformed AHC [Issue 1/§1.3]");
        CK(rs[0] != NOTIFY_RESPONSE_MARKER, "(adv-A) legacy NOT misrouted to typed path (r[0]!=0x51) [Issue 1]");
        CK(board_cmd_en == 1,               "(adv-A) legacy dispatch fired board on_enable (routing recovered) [Issue 1/§12]");
        CK(stub_get_active_layer() == 5,    "(adv-A) legacy dispatch activated board layer 5 [Issue 1/§12]");
    }

    /* ===== (adv-B) TRUNCATED AHC (no ETX) — legacy recovers ===== */
    {
        board_cmd_en = board_cmd_dis = 0;
        /* truncated AHC: header + 1 id, NO ETX in a full 32-byte report (abandoned) */
        uint8_t r[32]; memset(r, 0, sizeof(r));
        r[0]=0x81; r[1]=0x9F; r[2]=NOTIFY_CMD_DISCRIMINATOR; r[3]=NOTIFY_CMD_APPLY_HOST_CONTEXT;
        r[4]=225; r[5]=0x00; r[6]=5; r[7]=0x42;   /* NO ETX */
        hid_notify(r, 32);
        const uint8_t *ra = stub_get_last_response();
        CK(ra[0] != NOTIFY_RESPONSE_MARKER, "(adv-B) truncated AHC NOT typed-dispatched (r[0]!=0x51) [Issue 1/§4.6]");

        uint8_t s[32]; memset(s, 0, sizeof(s));
        s[0]=0x81; s[1]=0x9F;
        s[2]='n'; s[3]='e'; s[4]='o'; s[5]='v'; s[6]='i'; s[7]='d'; s[8]='e';
        s[9]=ETX_TERMINATOR[0];
        hid_notify(s, 32);
        const uint8_t *rs = stub_get_last_response();
        CK(rs[0] == 1,                      "(adv-B) legacy 'neovide' recovers after truncated AHC [Issue 1/§1.3]");
        CK(board_cmd_en == 1,               "(adv-B) legacy dispatch fired board on_enable [Issue 1/§12]");
    }

    /* ===== (adv-C) RECOVERY: typed commands work after a legacy flush =====
     * The malformed frame leaves msg_index non-zero, so the IMMEDIATE next typed
     * command is not seen (watchdog residual). After a legacy dispatch flushes
     * the buffer (its ETX resets msg_index), typed commands fully recover. */
    {
        uint8_t r[32]; memset(r, 0, sizeof(r));
        r[0]=0x81; r[1]=0x9F; r[2]=NOTIFY_CMD_DISCRIMINATOR; r[3]=NOTIFY_CMD_APPLY_HOST_CONTEXT;
        r[4]=226; r[5]=0x00; r[6]=5; r[7]=0x43; r[8]=ETX_TERMINATOR[0];
        hid_notify(r, 32);
        /* legacy flush: "neovide" + ETX dispatches and resets msg_index to 0 */
        uint8_t s[32]; memset(s, 0, sizeof(s));
        s[0]=0x81; s[1]=0x9F;
        s[2]='n'; s[3]='e'; s[4]='o'; s[5]='v'; s[6]='i'; s[7]='d'; s[8]='e';
        s[9]=ETX_TERMINATOR[0];
        hid_notify(s, 32);
        /* now a typed QUERY_INFO dispatches normally — typed path recovered */
        const uint8_t *rq = send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);
        CK(rq[0] == NOTIFY_RESPONSE_MARKER, "(adv-C) post-flush QUERY_INFO r[0]=0x51 (typed recovered) [Issue 1]");
        CK(rq[1] == NOTIFY_CMD_QUERY_INFO,  "(adv-C) post-flush QUERY_INFO r[1]=0x01 echo [§4.6]");
        CK(rq[2] == NOTIFY_PROTO_VER,       "(adv-C) post-flush QUERY_INFO r[2]=proto_ver=2 [§4.6]");
    }

    /* ===== (adv-D) ABANDONED typed msg — well-formed AHC works after a flush ===== */
    {
        uint8_t r[32]; memset(r, 0, sizeof(r));
        r[0]=0x81; r[1]=0x9F; r[2]=NOTIFY_CMD_DISCRIMINATOR; r[3]=NOTIFY_CMD_APPLY_HOST_CONTEXT;
        r[4]=0; r[5]=0x00; r[6]=5; r[7]=0x44;   /* NO ETX — abandoned */
        hid_notify(r, 32);
        uint8_t s[32]; memset(s, 0, sizeof(s));
        s[0]=0x81; s[1]=0x9F;
        s[2]='n'; s[3]='e'; s[4]='o'; s[5]='v'; s[6]='i'; s[7]='d'; s[8]='e';
        s[9]=ETX_TERMINATOR[0];
        hid_notify(s, 32);
        /* well-formed AHC (count=0) dispatches — host plane recovered */
        uint8_t a[] = { 224, 0x00, 0 };          /* layer=224, flags=0 (stack), count=0 */
        const uint8_t *ra = send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3);
        CK(ra[0] == NOTIFY_RESPONSE_MARKER,        "(adv-D) post-flush AHC r[0]=0x51 (typed recovered) [Issue 1]");
        CK(ra[1] == NOTIFY_CMD_APPLY_HOST_CONTEXT,  "(adv-D) post-flush AHC r[1]=0x05 echo [§4.6]");
        CK(ra[2] == 1,                               "(adv-D) post-flush AHC r[2]=ack=1 applied [§4.6]");
        CK(stub_get_active_layer() == 224,           "(adv-D) post-flush AHC host layer 224 active [§14]");
    }

```

### Implementation Patterns & Key Details

```c
// PATTERN (legacy send via hid_notify — mirror (coexist-i)(b) exactly):
//   uint8_t s[32]; memset(s,0,sizeof(s));
//   s[0]=0x81; s[1]=0x9F;            // §13 magic header
//   s[2]='n'; ... s[8]='e';          // "neovide" at data[2..] (data[2]!='0xF0' => legacy)
//   s[9]=ETX_TERMINATOR[0];          // 0x03
//   hid_notify(s,32);

// PATTERN (malformed/abandoned typed frame via hid_notify):
//   r[2]=NOTIFY_CMD_DISCRIMINATOR; r[3]=NOTIFY_CMD_APPLY_HOST_CONTEXT; r[4]=layer; r[5]=flags; r[6]=count; r[7]=id0; [r[8]=ETX]

// ANTI-PATTERN: do NOT write adv-C/adv-D as "malformed then IMMEDIATELY typed" — the
//   landed watchdog leaves msg_index non-zero and the typed cmd is not recognized
//   (verified: returns 0x00). The legacy flush between is REQUIRED for the typed cmd to pass.
// ANTI-PATTERN: do NOT assert on cb_mute_en/cb_layout_en in adv-D — host_cb_enabled[]
//   persists from earlier cases ((vii)/(multi-rep) enable id 0), so on_enable may not
//   re-fire. Assert response bytes + active_layer instead (clean, state-independent).
// ANTI-PATTERN: do NOT amend notifier.c or the watchdog — this task is tests-only.
//   If immediate typed recovery is required, that's a separate fix decision (see FINDING).
```

### Integration Points

```yaml
TEST FILE (test_notifier_host.c):
  - append: adv-A/adv-B/adv-C/adv-D bannered section before the final printf
  - update: file header comment (Mode A) — note the new adversarial section
DRIVER/GATE:
  - run_notifier_stub_tests.sh already builds + runs test_notifier_host; no runner edit.
  - the new cases keep `grep -c '^FAIL:'` == 0 (verified 79/79).
BUILD/CONFIG/ROUTES/DATABASE:
  - none. Tests-only; no notifier.c, header, rules.mk, or wire change.
```

## Validation Loop

> Toolchain: gcc via the stub harness. All commands below were **executed during
> research** against the current (fix-landed) notifier.c and a temp copy of
> test_notifier_host.c carrying the exact adv block — **79/79 pass, 0 failures**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# Compile the driver clean (stub-compiled notifier.c + qmk_stubs.c + the test).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/n.o
gcc -Wall -Wextra -std=c99 -Iqmk_stubs -I. /tmp/n.o qmk_stubs/qmk_stubs.c test_notifier_host.c -o /tmp/tnh
echo "build exit=$?  (expect 0)"
rm -f /tmp/n.o /tmp/tnh
# Expected: exit 0, no warnings (the new code uses only existing symbols).

# Confirm the new section + header update landed.
grep -q 'P1.M1.T2.S1 — ADVERSARIAL typed-command framing' test_notifier_host.c && echo "banner present"
grep -q '(adv-A) malformed AHC' test_notifier_host.c && echo "adv-A present"
grep -q '(adv-D) post-flush AHC' test_notifier_host.c && echo "adv-D present"
```

### Level 2: The adversarial cases (Component Validation)

```bash
cd /home/dustin/projects/qmk-notifier
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c -o /tmp/n.o
gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/n.o qmk_stubs/qmk_stubs.c test_notifier_host.c -o /tmp/tnh
/tmp/tnh 2>/dev/null | grep -E '\(adv-'           # all 15 adv assertions
/tmp/tnh 2>/dev/null | tail -1                     # summary line
echo "adv FAIL count: $(/tmp/tnh 2>/dev/null | grep -c '^FAIL:')"
rm -f /tmp/n.o /tmp/tnh
# Expected: 15 "PASS: (adv-...)" lines; summary "Total tests run: 79 / passed: 79 / failed: 0";
#           adv FAIL count: 0.
```

### Level 3: Integration Testing (the gate — no regression)

```bash
cd /home/dustin/projects/qmk-notifier

# Full stub gate — dispatch/os/host all 0 fails; host now > 64 cases.
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "exit=$?"
grep -E 'notifier (dispatch|os|host) fails=' /tmp/ns.out
tail -n 2 /tmp/ns.out
rm -f /tmp/ns.out
# Expected: exit 0; "notifier dispatch fails=0", "notifier os fails=0",
#           "notifier host fails=0" (host now 79/79), "✓ notifier stub-compile gate PASSED".

# Matcher corpus is untouched (this change is test_notifier_host.c only).
./run_all_tests.sh > /tmp/all.out 2>&1; echo "exit=$?"; tail -n 3 /tmp/all.out; rm -f /tmp/all.out
# Expected: exit 0; matcher suites unchanged.

# Diff hygiene: only test_notifier_host.c changed in source (besides plan/ artifacts).
git diff --stat -- ':!plan'
# Expected: only `test_notifier_host.c` listed.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Confirm adv-A is the literal Issue 1 repro (byte-for-byte vs the bug report).
grep -A2 'adv-A) MALFORMED' test_notifier_host.c | grep -qE 'r\[6\]=5.*r\[7\]=0x41.*r\[8\]=ETX' \
  && echo "adv-A matches Issue 1 repro" || echo "WARN: adv-A byte layout differs"

# Confirm the contract deviation is documented in-code (the behavior-envelope comment).
grep -q 'legacy routing' test_notifier_host.c && grep -q 'legacy ETX flush' test_notifier_host.c \
  && echo "behavior-envelope (FINDING) documented in-code"

# Confirm no host-callback counter is asserted in adv-D (state-bleed avoidance).
! sed -n '/(adv-D)/,/^    }/p' test_notifier_host.c | grep -qE 'cb_(mute|layout)_(en|dis)' \
  && echo "adv-D does not assert on cb_* counters (good)" || echo "WARN: adv-D asserts cb_*"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: driver compiles clean (`-Wall -Wextra`); banner + adv-A..D present.
- [ ] Level 2: 15 adv assertions PASS; summary 79/79, 0 failures.
- [ ] Level 3: `run_notifier_stub_tests.sh` green (dispatch/os/host 0 fails); matcher corpus unchanged.
- [ ] Level 3: only `test_notifier_host.c` changed.
- [ ] Level 4: adv-A matches the Issue 1 repro; FINDING documented in-code; adv-D avoids cb_* assertions.

### Feature Validation

- [ ] adv-A: malformed AHC (count=5/1id) → legacy `neovide` recovers (ack=1, board fires, layer 5).
- [ ] adv-B: truncated AHC (no ETX) → legacy recovers.
- [ ] adv-C: malformed → legacy flush → QUERY_INFO recovers (`0x51 0x01 proto=2`).
- [ ] adv-D: abandoned → legacy flush → well-formed AHC recovers (`0x51 0x05 ack=1`, layer 224).
- [ ] File header updated (Mode A) noting the adversarial section.

### Code Quality Validation

- [ ] Mirrors the existing `(coexist-i)`/`(multi-rep)` patterns (manual hid_notify framing).
- [ ] Additive-only; existing 64 cases + final printf/return untouched.
- [ ] Counters reset per-case; no state-bleed fragility (adv-D uses layer/response, not cb_*).
- [ ] No modification to notifier.c, notifier.h, pattern_match.*, qmk_stubs/*, other test_*.c,
      run_*.sh, PRD.md, tasks.json.

### Documentation & Deployment

- [ ] Mode-A header update + in-code behavior-envelope comment self-document the deviation.
- [ ] The FINDING (immediate typed recovery unmet by the landed fix) is surfaced for the human.

---

## Anti-Patterns to Avoid

- ❌ Don't write adv-C/adv-D as "malformed then IMMEDIATELY typed" — it FAILS with the landed
  fix (msg_index residual). The legacy flush between is REQUIRED for those cases to pass.
- ❌ Don't amend notifier.c / the watchdog — this task is tests-only. The residual is a fix
  decision (documented in the FINDING), not a test concern.
- ❌ Don't assert on `cb_mute_en`/`cb_layout_en` in adv-D — `host_cb_enabled[]` persists across
  cases; use `stub_get_active_layer()` + response bytes (state-independent).
- ❌ Don't change adv-A's byte layout — it's the literal Issue 1 reproduction (`count=5`, one id
  `0x41`, ETX). Keep it byte-for-byte vs the bug report.
- ❌ Don't reset `board_cmd_en` inside the malformed-frame send — reset it BEFORE the legacy
  check so the post-legacy assertion (`==1`) is clean.
- ❌ Don't edit run_notifier_stub_tests.sh — it already builds+runs this driver; the new cases
  are picked up automatically.
- ❌ Don't touch notifier.c, notifier.h, pattern_match.*, qmk_stubs/*, other test_*.c, run_*.sh,
  PRD.md, tasks.json.

---

## Confidence Score: 9/10

All 4 cases are specified verbatim and were **empirically verified** (appended to a
temp copy of `test_notifier_host.c`, compiled against the landed-fix `notifier.c`,
**79/79 pass, 0 failures**). The byte layouts, counter resets, and assertion values
are ground-truth, not guessed. The one reason this is not 10/10: the **contract
deviation** (adv-C/adv-D cannot use the literal "immediate typed recovery" form the
item description specified) — I resolved it by adapting those two cases to the
verified "recovery after a legacy flush" form and documented it prominently as a
FINDING with root cause + a fix-amendment option for the human. If the human
instead requires immediate typed recovery, the **fix** (P1.M1.T1.S1) must be amended
(separate task); these tests would then need adv-C/adv-D reverted to the immediate
form. The tests as shipped PASS with the code as shipped.