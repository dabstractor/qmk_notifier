# Research Notes — P1.M1.T1.S1 (typed_awaiting_terminator watchdog)

## Task
Fix Major Issue 1 (typed reassembly desync) in notifier.c by adding a
`typed_awaiting_terminator` watchdog flag + reset logic in the hid_notify byte
loop. A malformed/truncated APPLY_HOST_CONTEXT (count != actual ids) must be
dropped silently and the NEXT well-formed legacy focus-change string must resume
normal board routing.

## Bug (CONFIRMED by reproduction against current notifier.c)
Built a stub probe (DEFINE_SERIAL_COMMANDS {"neovide"->on_en}; send malformed
AHC count=5/1id/ETX; then legacy "neovide\x03"). Current-code result:
- after malformed AHC: response[0]=0 (ETX consumed as literal id; no dispatch)
- after legacy 'neovide': response[0]=0x51 (81), board_on_enable_fires=0
  → VERDICT BROKEN. (Also observed a BOGUS layer_on(224): the stale buffer
  re-dispatched the AHC with leftover layer byte 0xE0.)
Matches bug_analysis.md byte-trace exactly.

Root cause: `typed_literal_remaining` is extended by host-supplied AHC.count
(notifier.c ~msg_index==5 branch, clamped to buffer ROOM not stream bytes).
When count > actual ids, it swallows the intended ETX + later bytes as fake
literal ids. typed_mode is only cleared at ETX (requires tlr==0) or overflow;
neither fires → typed_mode pinned true → legacy strings misrouted to
handle_typed_command (no board side effects).

## Fix (6 edits, ALL VERIFIED — applied to temp copy, compiles -Wall -Wextra
clean, bug RECOVERED, all 3 shipped drivers green incl. host 64/64)
Exact anchors (asserted unique in the 947-line notifier.c):

1. DECLARE — after `static uint16_t typed_literal_remaining = 0;` (line 115):
   add `static bool typed_awaiting_terminator = false;` + doc comment.

2. TYPED-MODE ENTRY — inside `if (msg_index==0 && length>=3 && data[2]==NOTIFY_CMD_DISCRIMINATOR) {`
   block, after `typed_literal_remaining = 2;`: add
   `typed_awaiting_terminator = false;`.

3. WATCHDOG CHECK — between `bool typed_literal = (typed_mode && typed_literal_remaining > 0);`
   and the `// End of text` / ETX gate. Condition:
   `if (typed_awaiting_terminator && !typed_literal && c != ETX_TERMINATOR[0])`
   → reset typed_mode=false, typed_literal_remaining=0, typed_awaiting_terminator=false,
   msg_index=0; then FALL THROUGH (no continue/break — byte appended normally).

4. LATCH — at END of the `if (typed_literal) { ... }` extension block (AFTER the
   msg_index==2 and msg_index==5 extension logic): `if (typed_literal_remaining == 0)
   { typed_awaiting_terminator = true; }`. CRITICAL: must be AFTER extension — tlr
   transiently hits 0 at msg_idx==2 (before fixed args) and msg_idx==5 (before ids).

5. ETX-BOUNDARY RESET — in the block ending `... typed_literal_remaining = 0; /* BUG-1/2 */ break;`:
   add `typed_awaiting_terminator = false;`.

6. OVERFLOW RESET — in the block ending `... typed_literal_remaining = 0; /* BUG-1/2 */ `
   (the `else { Buffer overflow ... }` branch): add `typed_awaiting_terminator = false;`.

## Why the watchdog works (bug_analysis verification trace, confirmed)
- Malformed (count=5,1id): tlr hits 0 at i=9 (after swallowing the ETX at i=6
  as id — RESIDUAL, one-frame); flag latched; i=10 byte=0x00≠ETX → watchdog resets.
  typed_mode=false. Next legacy "neovide" → process_full_message → on_en fires. ✓
- Legitimate multi-report AHC (count=28,2reps): last id at end of rep2 sets flag,
  immediately-following byte IS ETX → watchdog condition `c!=ETX` false → no reset
  → ETX gate dispatches normally. ✓ (host suite 64/64 incl. this case confirms.)
- Legitimate single-report (QUERY_INFO/QUERY_CALLBACK/SET_OS/AHC count==ids):
  flag latches after last arg, next byte IS ETX → dispatch. ✓

## Residual (ACCEPTABLE, per bug_analysis §Residual)
The fix does NOT prevent the INITIAL over-consumption of the intended ETX (at
i=6). It bounds the damage to one frame. This satisfies the PRD §1.3/§12 "at
minimum" requirement (malformed msg dropped, next well-formed legacy resumes).
The malformed AHC response stays response[0]=0 (no ack change) — fine.

## Validation gates (VERIFIED executable)
- Stub compile: `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c`
- Full gate: `./run_notifier_stub_tests.sh` → dispatch 0 fails, OS 0 fails, host 64/64. (verified equivalent against fixed copy)
- Repro probe (optional, for the implementer to confirm before/after): the
  /tmp/desync_probe.c pattern — DEFINE_SERIAL_COMMANDS {"neovide",cb,0}; malformed
  AHC then legacy "neovide\x03"; assert response[0]==1 && on_en fires.
- Compile-time guard at notifier.c ~28: `int _guard[(NFA_MAX_PATTERN <= 128) ? 1 : -1];`
  unchanged; still compiles.

## Scope boundaries
- Modify ONLY notifier.c (6 edits: 1 decl+doc, 1 entry reset, 1 watchdog check,
  1 latch, 2 existing-site resets). No header change. No new files.
- DO NOT touch notifier.h (typed-command constants already there from plan 003 P1.M1.T1.S1).
- DO NOT modify the AHC count clamp in handle_typed_command (~785-790) — it's
  defense-in-depth that only runs on dispatch; the watchdog is the reassembly fix.
- Adversarial TEST cases (count/ids mismatch, truncated, abandoned, recovery) are
  SIBLING task P1.M1.T2.S1 — NOT this task. This task is the notifier.c fix only.
- DO NOT touch PRD.md, tasks.json, prd_snapshot.md, run_*.sh, other test_*.c.