# PRP — P1.M1.T1.S1: `typed_awaiting_terminator` watchdog in `notifier.c`

## Goal

**Feature Goal**: Fix Major Issue 1 (typed reassembly desync) by adding a
`typed_awaiting_terminator` watchdog flag and reset logic in `notifier.c`'s
`hid_notify()` byte loop. A malformed/truncated/abandoned typed command
(`APPLY_HOST_CONTEXT.count` != actual id bytes) must be dropped silently (like an
oversized legacy message, PRD F2.2), and the *next* well-formed legacy
focus-change string must resume normal board layer/command routing.

**Deliverable**: The modified file `notifier.c` — six surgical edits: one
declaration (+doc comment), one typed-mode-entry reset, one watchdog check, one
latch, and two additions at the existing ETX-boundary and overflow reset sites.

**Success Definition**:
- `notifier.c` stub-compiles clean under `-Wall -Wextra -std=c99`.
- The repro probe (malformed AHC count=5/1id, then legacy `neovide`) flips from
  `BROKEN` (current: legacy → `response[0]=0x51`, board `on_enable` never fires)
  to `RECOVERED` (legacy → `response[0]=1`, `on_enable` fires once).
- `run_notifier_stub_tests.sh` stays fully green: dispatch 0 fails, OS 0 fails,
  **host 64/64** (including the legitimate multi-report AHC count=28).
- The compile-time guard at `notifier.c:~28` (`NFA_MAX_PATTERN <= 128`) still compiles.
- (All three above were **verified** against a temp copy with the exact edits below.)

## User Persona (if applicable)

**Target User**: Firmware maintainers and any host (QMKonnect) that may
temporarily send a malformed/truncated `APPLY_HOST_CONTEXT` (host bug, mid-frame
USB/KVM disconnect per §2 F9.4).

**Use Case**: A host sends `APPLY_HOST_CONTEXT` with `count=5` but only one id
byte (or the stream is interrupted). The board must not have its primary
layer/command function permanently broken.

**User Journey**: malformed AHC arrives → watchdog detects the count/ids mismatch
at the byte after the last consumed literal → resets typed state → next legacy
focus-change string dispatches via `process_full_message` normally.

**Pain Points Addressed**: Today the desync pins `typed_mode=true` permanently
(every later legacy string misroutes to `handle_typed_command`, emitting `[0x51]`
with no board side effects) until a buffer overflow (hundreds of focus changes)
or a replug. The watchdog bounds the damage to one frame.

## Why

- Restores the PRD §1.3/§12 robustness guarantee ("robust to garbage … no input
  can crash it") for the typed-command path, which the length-aware reassembly
  broke by trusting `APPLY_HOST_CONTEXT.count` unconditionally.
- Directly relevant to the KVM/USB-switch world (§2 F9.4): mid-`APPLY_HOST_CONTEXT`
  disconnects are a realistic failure mode, not a theoretical one.
- Minimal, surgical, additive: one new `static bool` + reset logic mirroring the
  existing `typed_mode`/`typed_literal_remaining` reset sites. No header, API,
  wire-protocol, or build-system change.

## What

Six edits to `notifier.c` (exact anchors verified against the 947-line file):

**(A) Declare** `static bool typed_awaiting_terminator = false;` immediately after
`static uint16_t typed_literal_remaining = 0;` (line 115), with a doc comment
explaining the watchdog (Mode A — update the `typed_literal_remaining` doc block).

**(B) Watchdog check** at the top of the byte loop, *after* `typed_literal` is
computed (line 858) and *before* the ETX gate (line 862):
```c
if (typed_awaiting_terminator && !typed_literal && c != ETX_TERMINATOR[0]) {
    typed_mode = false;
    typed_literal_remaining = 0;
    typed_awaiting_terminator = false;
    msg_index = 0;
}
```
Then **fall through** (no `continue`/`break`) so the byte is appended normally as
a potential new message start.

**(C) Latch** the flag at the END of the `if (typed_literal) { … }` extension
block (after the `msg_index == 2` / `msg_index == 5` logic), INSIDE that block:
```c
if (typed_literal_remaining == 0) { typed_awaiting_terminator = true; }
```

**(D)** Add `typed_awaiting_terminator = false;` at the three existing reset sites:
typed-mode entry (~836), ETX boundary (~889), overflow (~934).

### Success Criteria

- [ ] All six edits present at the exact anchors below.
- [ ] Repro probe flips BROKEN → RECOVERED.
- [ ] `run_notifier_stub_tests.sh`: dispatch/OS/host all 0 fails (host 64/64).
- [ ] `-Wall -Wextra` clean; `NFA_MAX_PATTERN<=128` guard still compiles.

## All Needed Context

### Context Completeness Check

_Pass_: The exact bug, byte-trace, fix mechanism, all six edit anchors (with the
exact `oldText`→`newText` for the `edit` tool), the repro probe, and the
validation commands are provided below. The fix was **empirically verified**
(reproduced the bug, applied the exact edits to a temp copy, confirmed RECOVERED +
64/64 host + clean compile). An implementer with only this PRP + the repo can
apply the six edits and validate with no guessing.

### Documentation & References

```yaml
# MUST READ — the authoritative bug analysis + fix
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/architecture/bug_analysis.md
  why: "Contains the byte-by-byte trace, the exact fix mechanism, the CRITICAL
        'latch AFTER extension' constraint, and the correctness verification for
        malformed / legitimate / cross-report-boundary cases."
  critical: "The flag-set MUST be AFTER the extension block (tlr transiently hits
             0 at msg_idx==2 and msg_idx==5 before fixed/ids counts are added).
             Setting it before would false-reset well-formed commands."

- file: notifier.c
  section: "hid_notify() byte loop (lines 816-946) + typed state vars (92-115)"
  why: "The exact code being modified. typed_mode=96, typed_literal_remaining=115,
        typed-mode entry=835-837, typed_literal compute=858, ETX gate=862,
        ETX reset=887-890, extension block=917-926, overflow reset=941-943."
  pattern: "Mirror the EXISTING reset idiom: typed_mode=false; typed_literal_remaining=0;
            at ETX-boundary and overflow. Add the new flag to the same lines."
  gotcha: "The watchdog check must come AFTER `typed_literal` is computed (uses
           !typed_literal) and BEFORE the ETX gate. Do NOT use continue/break in it."

- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/architecture/scout_typed_reassembly.md
  why: "Background on the length-aware reassembly (BUG-1/BUG-2/BUG-3 lineage) this
        watchdog hardens."
  critical: "Do NOT remove the length-aware reassembly (it fixes SET_OS/AHC 0x03
             collisions). The watchdog is ADDITIVE defense, not a replacement."

# PRD contracts
- file: plan/003_16d737de7a3e/prd_snapshot.md   (or repo PRD.md)
  section: "### 4.6 Typed-command namespace" + "## 12 Non-Functional Requirements" + §1.3
  why: "§4.6 framing contract (ETX-framed typed cmds); §1.3/§12 'robust to garbage';
        F2.2 oversized-message-drop precedent the watchdog mirrors."
  critical: "The malformed message is DROPPED (no ack change, response[0]=0 like
             today) — the watchdog bounds damage, it does not dispatch the partial frame."

# Validator
- file: run_notifier_stub_tests.sh
  why: "The acceptance gate. Builds notifier.c with stubs and runs dispatch/os/host."
  gotcha: "It hardcodes notifier.c at repo root. To validate a temp copy, replicate
           its gcc lines against your copy (see Validation Loop)."
```

### Current Codebase tree (relevant slice)

```bash
notifier.c                # ← MODIFY (6 surgical edits in hid_notify + 1 decl). 947 lines.
notifier.h                # unaffected (typed constants already present)
pattern_match.h / .c      # unaffected
qmk_stubs/                # qmk_stubs.c (stub_get_last_response, g_last_response[32]), os_detection.h, ...
test_notifier_dispatch.c  # shipped suite — must stay green (14 cases)
test_notifier_os.c        # shipped suite — must stay green (31 cases)
test_notifier_host.c      # shipped suite — must stay green (64 cases; incl. legit multi-rep AHC)
run_notifier_stub_tests.sh# acceptance gate
PRD.md                    # READ-ONLY
plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/  # this bugfix plan
```

### Desired Codebase tree with files to be added/changed

```bash
notifier.c                # MODIFIED: +typed_awaiting_terminator decl+doc, +watchdog check, +latch, +3 reset-site additions
# (no new files)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL (latch ordering): set typed_awaiting_terminator AFTER the extension
//   block, not before. tlr transiently == 0 at msg_index==2 (cmd_id consumed,
//   before fixed args added) and msg_index==5 (count consumed, before ids added).
//   Latching before the extension would false-reset every well-formed command.

// GOTCHA (watchdog placement): the check runs at the TOP of the loop, AFTER
//   `bool typed_literal = ...` is computed and BEFORE the ETX gate. It uses
//   !typed_literal (redundant-but-safe: when the flag is set, tlr==0 so
//   typed_literal is false). Do NOT move it into the append branch.

// GOTCHA (no continue/break): after the watchdog resets state, FALL THROUGH so
//   the current byte is appended as a potential new message start. Stray bytes
//   (e.g. 0x00 filler) are later stripped by sanitize_string on the legacy path.

// GOTCHA (residual is acceptable): the watchdog does NOT prevent the INITIAL
//   over-consumption of the intended ETX (byte i=6 in the malformed trace). It
//   bounds the damage to ONE frame. The malformed AHC still acks response[0]=0
//   (no change). This satisfies PRD §1.3/§12 "at minimum" — do not try to also
//   "save" the swallowed ETX; that would require a different, riskier fix.

// GOTCHA (typed_fixed_arg_bytes): the existing per-command fixed-arg map at
//   msg_index==2 (QUERY_INFO=0, QUERY_CALLBACK=1, SET_OS=1, AHC=3) is UNCHANGED.
//   Do not modify it; the watchdog rides on top of its tlr transitions.

// GOTCHA (do not touch handle_typed_command's count clamp ~785-790): that is
//   defense-in-depth that only runs on dispatch; the watchdog is the reassembly
//   fix. Two separate concerns.
```

## Implementation Blueprint

### Data models and structure

One new file-scope variable: `static bool typed_awaiting_terminator = false;`
No new types, structs, or functions. All edits are control-flow + state in the
existing byte loop.

### Implementation Tasks (ordered by dependencies)

Apply the six edits below with the `edit` tool (each `oldText` is verified unique
in the current `notifier.c`):

```yaml
Task 1: DECLARE the flag + doc (after typed_literal_remaining, line 115)
  oldText: "static uint16_t typed_literal_remaining = 0;"
  newText: the same line, then the typed_awaiting_terminator doc block + decl
           (see "Exact edits" → EDIT 1). Mode-A: this extends the
           typed_literal_remaining documentation block (~98-115).

Task 2: TYPED-MODE ENTRY reset (inside the msg_index==0 discriminator block)
  Anchor: the 3-line block `typed_mode = true; typed_literal_remaining = 2; ...}`.
  Add `typed_awaiting_terminator = false;` before the closing brace. (EDIT 2)

Task 3: WATCHDOG CHECK (between typed_literal compute and ETX gate)
  Anchor: `bool typed_literal = (...)` + blank + `// End of text`. Insert the
  watchdog if-block between them. (EDIT 3)

Task 4: LATCH at end of the if(typed_literal) extension block
  Anchor: the full `if (typed_literal) { ... }` block. Add the
  `if (typed_literal_remaining == 0) { typed_awaiting_terminator = true; }`
  AFTER the inner msg_index==2/==5 logic, still inside the outer block. (EDIT 4)

Task 5: ETX-BOUNDARY reset (the block ending ...break;)
  Anchor: `typed_mode = false; /* RISK-1: reset at every ETX boundary */ ...
           typed_literal_remaining = 0; /* BUG-1/2 */ break;`. Add the flag
           reset before `break;`. (EDIT 5)

Task 6: OVERFLOW reset (the else { Buffer overflow ... } branch)
  Anchor: `typed_mode = false; /* RISK-1: dropped msg clears typed classification */ ...
           typed_literal_remaining = 0; /* BUG-1/2 */`. Add the flag reset. (EDIT 6)
```

### Exact edits (copy-ready for the `edit` tool)

**EDIT 1 — declaration + doc (Mode A):**
- oldText: `static uint16_t typed_literal_remaining = 0;`
- newText:
```c
static uint16_t typed_literal_remaining = 0;
/* typed_awaiting_terminator — watchdog for typed reassembly desync (Issue 1).
 * Latched when typed_literal_remaining transitions to 0 AFTER the extension
 * block (all declared literal bytes consumed). Checked at the top of the byte
 * loop: if the NEXT byte is not ETX, the typed message was malformed (count/ids
 * mismatch or a truncated/abandoned frame) — reset all typed state + msg_index
 * and drop the frame, exactly like the overflow path, so the next well-formed
 * legacy string resumes normal routing (§1.3/§12 robustness). */
static bool typed_awaiting_terminator = false;
```

**EDIT 2 — typed-mode entry reset:**
- oldText:
```c
        typed_mode = true;
        typed_literal_remaining = 2;   /* consume discriminator + cmd_id literally */
    }
```
- newText:
```c
        typed_mode = true;
        typed_literal_remaining = 2;   /* consume discriminator + cmd_id literally */
        typed_awaiting_terminator = false; /* fresh typed message: clear watchdog */
    }
```

**EDIT 3 — watchdog check (with explanatory inline comment, Mode A):**
- oldText:
```c
        bool typed_literal = (typed_mode && typed_literal_remaining > 0);

        // End of text
```
- newText:
```c
        bool typed_literal = (typed_mode && typed_literal_remaining > 0);

        /* WATCHDOG (Issue 1): the byte after the last consumed literal must be
         * ETX. If we are awaiting the terminator and this byte is NOT ETX, the
         * typed message is malformed (count/ids mismatch or a truncated frame):
         * reset all typed reassembly state + msg_index, then fall through so
         * the current byte is treated as a potential new message start (no
         * continue/break — the byte is appended normally below). */
        if (typed_awaiting_terminator && !typed_literal && c != ETX_TERMINATOR[0]) {
            typed_mode = false;
            typed_literal_remaining = 0;
            typed_awaiting_terminator = false;
            msg_index = 0;
        }

        // End of text
```

**EDIT 4 — latch at end of extension block:** replace the whole `if (typed_literal) { … }`
block; the only change is the trailing `if (typed_literal_remaining == 0) { … }`
added inside it. (oldText = the existing block; newText = same block + the latch
+ its comment, exactly as in the verified temp-copy edit.)

**EDIT 5 — ETX-boundary reset:**
- oldText:
```c
            typed_mode = false;          /* RISK-1: reset at every ETX boundary */
            typed_literal_remaining = 0; /* BUG-1/2: clear typed reassembly state */
            break;
```
- newText: same + `            typed_awaiting_terminator = false; /* Issue 1: clear watchdog */`
  inserted before `break;`.

**EDIT 6 — overflow reset:**
- oldText:
```c
                typed_mode = false;          /* RISK-1: dropped msg clears typed classification */
                typed_literal_remaining = 0; /* BUG-1/2: clear typed reassembly state */
```
- newText: same + `                typed_awaiting_terminator = false; /* Issue 1: clear watchdog */`
  appended.

> All six `oldText` blocks were asserted unique against the current `notifier.c`
> during research and applied cleanly to a temp copy.

### Implementation Patterns & Key Details

```c
// The watchdog mirrors the EXISTING typed-state reset idiom — it is just a 4th
// reset site (the "abandoned frame" site) plus the latch + the top-of-loop check.
// All three existing reset sites (entry/ETX/overflow) gain the same one line.

// CRITICAL ordering: latch (EDIT 4) is AFTER the msg_index==2/5 extension, so it
// only fires when tlr is genuinely 0 post-extension. The check (EDIT 3) then
// fires on the NEXT byte. This two-beat (latch-then-check) design is what makes
// legitimate commands (flag set, next byte IS ETX → dispatch) coexist with
// malformed ones (flag set, next byte NOT ETX → reset).

// ANTI-PATTERN: do NOT clamp typed_literal_remaining to stream bytes instead.
//   That is a different, riskier fix (changes AHC count semantics, may break
//   legit multi-report). The watchdog is the approved, minimal, verified fix.
// ANTI-PATTERN: do NOT `continue`/`break` in the watchdog — the byte must be
//   appended so a real new message can start on it.
// ANTI-PATTERN: do NOT touch handle_typed_command's count clamp or the
//   length-aware reassembly itself — the watchdog is additive hardening.
```

### Integration Points

```yaml
STATE (notifier.c file-scope):
  - add: "static bool typed_awaiting_terminator = false;  /* near typed_literal_remaining */"
CONTROL FLOW (hid_notify byte loop):
  - add: watchdog check (top of loop, after typed_literal, before ETX gate)
  - add: latch (end of if(typed_literal) extension block)
RESET SITES (mirror typed_mode/typed_literal_remaining):
  - typed-mode entry, ETX boundary, overflow  (+ the new watchdog self-reset)
BUILD/CONFIG/WIRE/ROUTES:
  - none. No header, no rules.mk, no wire-protocol, no API change.
```

## Validation Loop

> Toolchain: gcc via the stub harness (notifier.c cannot compile standalone — it
> `#include QMK_KEYBOARD_H` and needs layer_on/off/raw_hid_send from stubs).

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# Stub-compile notifier.c clean under -Wall -Wextra (the exact flags the runner uses).
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
echo "compile exit=$?   (expect 0, no warnings)"

# Confirm the new flag is present at all six sites.
grep -c 'typed_awaiting_terminator' notifier.c   # expect: >= 9 (1 decl + 1 entry + 4 in watchdog + 1 latch + 2 resets, roughly)
grep -n 'static bool typed_awaiting_terminator' notifier.c       # the declaration
grep -n 'if (typed_awaiting_terminator && !typed_literal' notifier.c  # the watchdog check
grep -n 'typed_awaiting_terminator = true' notifier.c            # the latch
# Confirm the NFA_MAX_PATTERN compile-time guard still compiles (it's in the same TU).
echo "guard line:"; grep -n 'NFA_MAX_PATTERN <= 128' notifier.c
rm -f /tmp/notifier_stub.o
# Expected: compile exit 0, no warnings; all greps print lines.
```

### Level 2: Unit Tests (Component Validation — the bug fix)

```bash
cd /home/dustin/projects/qmk-notifier

# Repro probe: malformed AHC (count=5, 1 id) then legacy "neovide".
cat > /tmp/desync_probe.c <<'EOF'
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"
const uint8_t *stub_get_last_response(void);
void hid_notify(uint8_t *data, uint8_t length);
static int board_on_enable_fires = 0;
static void on_en(void){ board_on_enable_fires++; }
DEFINE_SERIAL_COMMANDS({ { "neovide", on_en, 0 }, });
int main(void){
    uint8_t r[32] = {0};
    r[0]=0x81; r[1]=0x9F; r[2]=0xF0; r[3]=0x05; r[4]=224; r[5]=0; r[6]=5;
    r[7]=0x41; r[8]=0x03;
    hid_notify(r, 32);
    uint8_t s[32] = {0}; s[0]=0x81; s[1]=0x9F;
    memcpy(s+2, "neovide\x03", 8);
    hid_notify(s, 32);
    printf("legacy response[0]=%u board_on_enable_fires=%d\n",
           stub_get_last_response()[0], board_on_enable_fires);
    return (board_on_enable_fires == 1) ? 0 : 1;
}
EOF
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c /tmp/desync_probe.c -o /tmp/desync_probe
/tmp/desync_probe; echo "exit=$?   (expect 0 — RECOVERED; response[0]=1, fires=1)"
rm -f /tmp/desync_probe.c /tmp/desync_probe /tmp/notifier_stub.o
# Before the fix this prints: legacy response[0]=81 board_on_enable_fires=0, exit=1 (BROKEN).
# After the fix:               legacy response[0]=1  board_on_enable_fires=1, exit=0 (RECOVERED).
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# Full stub gate — must stay fully green (no regression from the watchdog).
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "exit=$?"
tail -n 6 /tmp/ns.out
# Expected: exit 0; "notifier dispatch fails=0", "notifier os fails=0",
#           "notifier host fails=0" (host 64/64 incl. legit multi-report AHC count=28),
#           "✓ notifier stub-compile gate PASSED".
rm -f /tmp/ns.out

# Also confirm the matcher corpus is untouched (this change is notifier.c only).
./run_all_tests.sh > /tmp/all.out 2>&1; echo "exit=$?"
tail -n 5 /tmp/all.out
# Expected: exit 0; matcher suites unchanged (2023/2023 or current count).
rm -f /tmp/all.out
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Confirm the latch is AFTER the extension (the critical constraint): grep the
# if(typed_literal) block and verify the latch line comes AFTER both the
# msg_index==2 and msg_index==5 branches.
awk '/if \(typed_literal\) \{/{f=1} f{print NR": "$0} /typed_awaiting_terminator = true/{if(f){print ">>> LATCH OK (after extension)"; f=0}}' notifier.c

# Confirm diff hygiene: only notifier.c changed in source (besides plan/ artifacts).
git diff --stat -- ':!plan'
# Expected: only `notifier.c` listed.

# (Optional) extra adversarial: abandoned AHC mid-stream (count=28, only 10 ids,
# then a NEW legacy message) — should also recover. The sibling test task
# P1.M1.T2.S1 adds these as permanent cases; here a manual probe is enough.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: stub `-Wall -Wextra` compile clean; flag present at all 6 sites.
- [ ] Level 2: repro probe RECOVERED (response[0]=1, fires=1, exit 0).
- [ ] Level 3: `run_notifier_stub_tests.sh` green (dispatch/OS/host 0 fails, host 64/64).
- [ ] Level 3: `run_all_tests.sh` matcher corpus unchanged.
- [ ] Level 4: latch confirmed AFTER extension; only `notifier.c` changed.

### Feature Validation

- [ ] Malformed AHC (count≠ids) → dropped; next legacy string → board on_enable fires.
- [ ] Legitimate multi-report AHC (count=28) still dispatches (host suite covers it).
- [ ] QUERY_INFO/QUERY_CALLBACK/SET_OS (flag latches, next byte IS ETX) unaffected.
- [ ] No false reset on the transient tlr==0 at msg_index==2/5 (latch-after-extension).

### Code Quality Validation

- [ ] Mirrors existing typed-state reset idiom; one new static bool.
- [ ] Mode-A doc comment on the declaration + inline comment on the watchdog check.
- [ ] No header/wire/API/build change; no anti-patterns (no continue/break in watchdog).
- [ ] No modification to notifier.h, pattern_match.*, handle_typed_command clamp, test_*.c,
      run_*.sh, PRD.md, tasks.json.

### Documentation & Deployment

- [ ] Declaration doc block + watchdog inline comment self-document (Mode A).
- [ ] Adversarial TEST cases are explicitly deferred to sibling task P1.M1.T2.S1.

---

## Anti-Patterns to Avoid

- ❌ Don't latch the flag BEFORE the extension block — tlr transiently hits 0 at
  msg_index==2/5; latching early false-resets every well-formed command.
- ❌ Don't `continue`/`break` in the watchdog — the current byte must be appended.
- ❌ Don't replace the length-aware reassembly or clamp tlr to stream bytes — the
  watchdog is the approved minimal fix; changing reassembly risks regressions.
- ❌ Don't touch `handle_typed_command`'s count clamp (~785-790) — different concern.
- ❌ Don't try to "save" the initially-swallowed ETX (byte i=6) — the residual
  one-frame over-consume is acceptable per PRD §1.3/§12 "at minimum".
- ❌ Don't add adversarial test cases here — that's sibling task P1.M1.T2.S1.
- ❌ Don't modify notifier.h, test_*.c, run_*.sh, PRD.md, tasks.json.

---

## Confidence Score: 10/10

The bug was **reproduced** against the current `notifier.c` (legacy `neovide`
→ response[0]=0x51, on_enable=0, plus a bogus layer_on(224) from the stale AHC).
The **exact six edits** were applied to a temp copy and **verified**: clean
`-Wall -Wextra` compile, the probe flipped to **RECOVERED** (response[0]=1,
on_enable=1), and **all three shipped drivers stayed green (host 64/64)** —
including the legitimate multi-report `APPLY_HOST_CONTEXT` count=28 that proves
the watchdog does not false-trigger on well-formed commands. The critical
"latch-after-extension" constraint is documented and grep-verified. The fix is
surgical (one new `static bool` + reset logic mirroring the existing idiom) with
no header/wire/API/build change.