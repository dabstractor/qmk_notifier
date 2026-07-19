# PRP — P1.M2.T1.S1: Add `typed_mode` flag + `0xF0` discriminator routing fork in `hid_notify`

## Goal

**Feature Goal**: Add the typed-command routing fork to `hid_notify` so a message
whose `data[2] == NOTIFY_CMD_DISCRIMINATOR (0xF0)` routes to
`handle_typed_command()` (a STUB for now; real impl in P1.M2.T2) and
**bypasses `process_full_message`** — no board `disable_command`/`deactivate_layer`
side effects (PRD §4.6). Legacy string messages (`data[2]` printable, never 0xF0)
take the **unchanged** path. Multi-report typed commands reassemble via the
existing `msg_buffer`/`msg_index`/`dropping` machinery, governed by a persistent
`typed_mode` flag that resets at every ETX/overflow boundary (RISK-1).

**Deliverable**: The modified file `notifier.c` at the repo root — 7 surgical
edits, all in the `hid_notify` region plus one new file-scope global (`typed_mode`)
and one stub (`handle_typed_command`). No other file changes.

**Success Definition**:
- `typed_mode` (static bool) persists across `hid_notify` calls; set only when
  `msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR` (first
  report only); reset to `false` at the ETX boundary AND the overflow branch.
- On ETX with `typed_mode == true`: `match = handle_typed_command(msg_buffer)`
  runs and `typed_dispatched` is set; `process_full_message` is NOT called and
  `sanitize_string` is NOT called (typed payload is binary). The post-loop legacy
  0/1 ack is **suppressed** (handle_typed_command owns the [0x51] response).
- On ETX with `typed_mode == false`: the path is **byte-identical** to today
  (sanitize_string → process_full_message → `response[0]=match; raw_hid_send`).
- `notifier.c` stub-compiles `-Wall -Wextra -std=c99` → exit 0 with **only** the
  4 pre-existing P1.M1.T2.S1 `-Wunused` warnings (host_layer, host_cb_enabled,
  has_been_queried, board_rules_present). No new warnings.
- `test_notifier_dispatch` stays **14/14** and `test_notifier_os` stays **31/31**,
  0 FAIL each (they never send 0xF0 — findings F5). `run_notifier_stub_tests.sh`
  prints "✓ notifier stub-compile gate PASSED".

## User Persona (if applicable)

**Target User**: (1) The desktop host (QMKonnect) that, on connect, sends
`QUERY_INFO` (`0xF0 0x01 … 0x03`) to detect capability (§4.6 handshake) — typed
commands must reach `handle_typed_command` WITHOUT clearing board state.
(2) The P1.M2.T2 implementer who replaces the `handle_typed_command` stub with the
real dispatch + four handlers. (3) Every legacy keymap/user — their string
messages must behave exactly as before.

**Use Case**: Host sends `0x81 0x9F 0xF0 0x01 0x03` (QUERY_INFO).
`hid_notify`'s discriminator check (msg_index==0, data[2]==0xF0) sets
`typed_mode=true`; the bytes accumulate into `msg_buffer` (`0xF0 0x01`); at ETX,
`handle_typed_command(msg_buffer)` runs (P1.M2.T2 will answer `[0x51][0x01]…`);
the board is untouched and no legacy ack is sent.

**User Journey**: `raw_hid_receive` → `hid_notify` → coexistence guard →
discriminator check (first report) → strip magic → byte loop into `msg_buffer` →
ETX → `typed_mode` ? `handle_typed_command` : `process_full_message` →
`typed_mode`/`msg_index` reset → (legacy only) 0/1 ack.

**Pain Points Addressed**: Without the fork, a typed `0xF0` message would be
walked by `process_full_message` as a no-match string, which ALWAYS calls
`disable_command()`/`deactivate_layer()` first — clearing an active board layer
on every host handshake (§4.6 "harmless only when board state is fresh"). The fork
makes typed commands side-effect-free for board state (invariant 21).

## Why

- **Implements the §4.6 routing contract**: `data[2] == 0xF0` ⇒ typed path BEFORE
  any string processing. This is the entry point the entire host-rules feature
  flows through; every typed handler (QUERY_INFO/QUERY_CALLBACK/SET_OS/
  APPLY_HOST_CONTEXT, P1.M2.T2) is reached via this fork.
- **Preserves board-state orthogonality (invariant 21)**: typed commands touch
  ONLY host state; `process_full_message` (legacy) touches ONLY board state. The
  fork is the boundary that enforces this — a typed command never reaches the
  board dispatcher.
- **Reuses proven reassembly (findings F7)**: multi-report typed framing
  (30 payload bytes/report, ETX-terminated) works via the EXISTING
  `msg_buffer`/`msg_index`/`dropping` machinery. No new buffering; only the
  `typed_mode` classification flag is added.
- **Backward-compatible by construction (findings F3)**: legacy strings have a
  printable `data[2]` (0x20–0x7E), never 0xF0, so the typed branch never fires
  for them. No `#ifdef`; the discriminator check is a cheap, always-present
  `if`. Both regression suites (dispatch 14/14, os 31/31) pass unchanged.

## What

7 edits to `notifier.c`, all confined to the `hid_notify` region + the new
`typed_mode` global + the `handle_typed_command` stub:

1. Add `static bool typed_mode = false;` after the `dropping` block (~L91).
2. Add a stub `static bool handle_typed_command(char *buf)` just before
   `hid_notify` (returns false; `(void)buf`). Real impl is P1.M2.T2.
3. Insert the discriminator check after the coexistence guard, BEFORE
   `data += 2; length -= 2;`.
4. Add `bool typed_dispatched = false;` local before the byte loop.
5. Fork the ETX `if (!dropping)` block into typed (handle_typed_command) vs
   legacy (sanitize + process_full_message).
6. Add `typed_mode = false;` at the ETX reset AND the overflow branch (RISK-1).
7. Guard the post-loop legacy response with `if (!typed_dispatched)`.

### Success Criteria

- [ ] `typed_mode` is `static bool`, init false, set only at msg_index==0 on
      `data[2] == NOTIFY_CMD_DISCRIMINATOR`, reset at ETX + overflow.
- [ ] The ETX branch forks: typed → `handle_typed_command` (no sanitize, no
      process_full_message); legacy → sanitize + process_full_message (unchanged).
- [ ] `typed_dispatched` local suppresses the post-loop legacy 0/1 ack for typed.
- [ ] stub handle_typed_command present (static, returns bool) so the typed branch links.
- [ ] stub-compile exit 0; only the 4 S1 `-Wunused` warnings; no new warnings.
- [ ] dispatch 14/14 + os 31/31, 0 FAIL; "✓ notifier stub-compile gate PASSED".
- [ ] No edits outside notifier.c.

## All Needed Context

### Context Completeness Check

**Pass.** All 7 edits are specified verbatim below ("Implementation Tasks") and
were **empirically validated during research** by applying them to a /tmp copy of
notifier.c via a python surgical replace: stub-compiles exit 0 with only the 4
pre-existing S1 warnings; a routing probe confirms legacy messages ack + run the
board path while typed messages (`data[2]==0xF0`) bypass process_full_message and
suppress the legacy ack, including a multi-report typed case; and both regression
suites pass unchanged (dispatch 14/14, os 31/31). An implementer with only this
PRP + repo can make the 7 edits and prove them green.

### Documentation & References

```yaml
# MUST READ — the canonical routing contract
- file: PRD.md   (snapshot: plan/003_16d737de7a3e/prd_snapshot.md)
  section: "### 4.6 Typed-command namespace (canonical owner)"
  why: "The discriminator rule: data[2] == 0xF0 => typed, routed to
        handle_typed_command BEFORE any string processing, so NO process_full_message
        side effects. Framing: ETX-framed, may span multiple 32-byte reports. Legacy:
        data[2] printable (0x20-0x7E), never 0xF0. Responses: typed [0x51]… vs legacy
        [0|1]. Invariant: '0xF0 can never begin a real matched string (sanitizer allows
        only 0x20-0x7E)'."
  critical: "Typed commands 'bypass process_full_message, so they have no board side
        effects'. The handshake timing note: against a LEGACY firmware QUERY_INFO is
        walked as a no-match string and process_full_message ALWAYS disables/deactivates
        first — the fork is what makes the typed path side-effect-free on a capable
        firmware."

- file: PRD.md
  section: "## 14 Host-Side Rules & Typed Commands → 'Typed-command dispatch at the top of hid_notify()'"
  why: "'if (length >= 3 && data[2] == 0xF0, route to handle_typed_command() and return;
        else the legacy string path runs unchanged (§8.6).' Confirms the fork belongs in
        hid_notify and that QUERY_INFO/QUERY_CALLBACK are answerable before any string."
  critical: "'Legacy string sends have data[2] = a printable char (never 0xF0), so the
        dispatch is transparent to keymaps that don't use host rules.' This is the
        backward-compat guarantee (findings F3)."

- file: PRD.md
  section: "### 8.8 hid_notify(uint8_t *data, uint8_t length) — the entry point"
  why: "The CURRENT 5-step pseudocode (coexistence guard -> strip magic -> byte loop
        to ETX -> process_full_message -> 0/1 ack). The fork inserts between step 1 and
        step 2 (discriminator check) and forks step 4 (typed vs legacy), keeping step 5
        legacy-only."
  critical: "Preserve the multi-report reassembly contract: 'a single report may contain
        a partial message (no ETX) -> bytes accumulate in msg_buffer across successive
        hid_notify calls until ETX arrives.' typed_mode is the ONLY new persistent state."

# MUST READ — the exact target shape + RISK-1
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## Dispatch fork in hid_notify"
  why: "Shows the before/after pseudocode: coexistence guard -> discriminator check
        (msg_index==0 && length>=3 && data[2]==NOTIFY_CMD_DISCRIMINATOR) -> typed_mode=true
        -> strip + accumulate -> on ETX typed_mode ? handle_typed_command : process_full_message
        -> typed_mode=false reset. Explains WHY data[2] (after the guard, data still points
        at byte 0) and WHY msg_index==0 (discriminator only in the FIRST report)."
  critical: "'Why msg_index == 0: The discriminator appears only in the FIRST report of a
        multi-report message. Subsequent reports have payload bytes at data[2], which may
        coincidentally be 0xF0. Only check on the first report.' VERIFIED in the multi-report
        probe (report 2's data[2]==0xF0 did NOT re-trigger)."

- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### F7 (multi-report reassembly reuse) + ### RISK-1 (typed_mode lifecycle)"
  why: "F7: 'Typed commands reuse [msg_buffer/msg_index/dropping] — the discriminator 0xF0
        is at data[2] only in the FIRST report; the typed_mode flag (checked when msg_index==0)
        governs routing.' RISK-1: 'Reset typed_mode = false alongside msg_index = 0 in all
        three ETX branches (normal dispatch, dropping, typed dispatch)' AND on overflow."
  critical: "RISK-1 mitigation is MANDATORY: an oversized typed message must clear typed_mode
        so the next message starts fresh. Both reset points (ETX + overflow) are in this PRP."

# Dependency — what the working tree already contains
- file: plan/003_16d737de7a3e/P1M1T1S1/PRP.md
  why: "Landed NOTIFY_CMD_DISCRIMINATOR (0xF0) + NOTIFY_RESPONSE_MARKER (0x51) +
        HOST_LAYER_BASE (224) in notifier.h. This task references NOTIFY_CMD_DISCRIMINATOR
        directly (do NOT hardcode 0xF0 — use the macro)."
  critical: "notifier.h:44 `#define NOTIFY_CMD_DISCRIMINATOR 0xF0`. Use the macro name, not
        the literal, so a future constant change is automatic."

- file: plan/003_16d737de7a3e/P1M1T2S1/PRP.md
  why: "Landed host_layer/host_cb_enabled/has_been_queried globals (L137-139) +
        board_rules_present (L198). They are UNUSED and emit the 4 expected -Wunused
        warnings. This task ADDS typed_mode near them but must NOT touch/restyle them."
  critical: "The validation gate allows EXACTLY those 4 S1 warnings + 0 new. typed_mode is
        read+written (used) so it does NOT warn; the stub handle_typed_command is called
        (used) so it does NOT warn."

# Parallel task — no overlap
- file: plan/003_16d737de7a3e/P1M1T2S2/PRP.md
  why: "P1.M1.T2.S2 (parallel) refactors notifier_set_os -> apply_os_change seam. It edits
        the §8.7 region (~L493-523), NOT hid_notify (~L525-577) and NOT typed_mode (~L91).
        grep confirms zero overlap. Both can land independently."
  critical: "No shared edit region. This task's edits are: ~L91 (typed_mode), just-before-L525
        (stub), L525-577 (hid_notify body). S2's edit is ~L493-523. Disjoint."

# Regression targets (must stay green)
- file: test_notifier_dispatch.c
  why: "Legacy reassembly/F4/F5/F6 ack — 14 cases, all send printable data[2]. The typed
        branch is inert for them (findings F5). MUST stay 14/14."
- file: test_notifier_os.c
  why: "Multi-OS F8/F9 — 31 cases, all send printable data[2], never call notifier_set_os
        via typed. MUST stay 31/31."

# Build/test gate
- file: run_notifier_stub_tests.sh
  why: "Object-compiles notifier.c, links BOTH notifier drivers, runs them, asserts 0 FAIL.
        Must print '✓ notifier stub-compile gate PASSED'. [1/3] uses -Wall -Wextra — the
        4 S1 warnings are non-fatal (gate is not -Werror)."
```

### Current Codebase tree (relevant slice — post-P1.M1.T2.S1 state)

```bash
notifier.c                # ← MODIFY (7 edits: typed_mode global + stub + hid_notify fork). NOTHING ELSE.
notifier.h                # LANDED: NOTIFY_CMD_DISCRIMINATOR/RESPONSE_MARKER/HOST_LAYER_BASE. DO NOT TOUCH.
pattern_match.{c,h}       # unaffected. DO NOT TOUCH.
qmk_stubs/                # os_detection.h, qmk_stubs.c (raw_hid_send prints to stderr). DO NOT TOUCH.
test_notifier_dispatch.c  # regression (14 cases). DO NOT TOUCH.
test_notifier_os.c        # regression (31 cases). DO NOT TOUCH.
run_notifier_stub_tests.sh# gate. DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
notifier.c                # MODIFIED: +typed_mode global, +handle_typed_command stub, hid_notify routing fork.
# (no new files; no header change)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — typed_mode must PERSIST across hid_notify calls (it's set on the
//   first report of a multi-report message and read at ETX, which may be a later
//   report). So it is a FILE-SCOPE static, NOT a hid_notify local. Place it next
//   to msg_index/dropping (they have the same cross-call persistence semantics).

// CRITICAL — the msg_index == 0 guard on the discriminator check. The
//   discriminator is at data[2] only in the FIRST report. Continuation reports
//   carry payload at data[2] which may coincidentally be 0xF0. Only classify on
//   the first report (msg_index == 0). VERIFIED: a multi-report typed message
//   whose report-2 data[2]==0xF0 did NOT re-trigger (msg_index was >0).

// CRITICAL — data[2] is read BEFORE `data += 2; length -= 2;`. After the
//   coexistence guard, `data` still points at byte 0, so data[2] is the 3rd byte
//   (the byte after the 2-byte magic header). The discriminator check MUST go
//   between the guard and the strip. (Architecture doc: "After the coexistence
//   guard, data still points at byte 0. The discriminator is at byte index 2.")

// CRITICAL — the typed path must NOT call sanitize_string. sanitize_string keeps
//   only 0x20-0x7E + 9/10/13/GS/ETX; it would STRIP the 0xF0 discriminator and
//   any binary cmd_id/args. Typed payload is binary, not ASCII. Only the LEGACY
//   branch sanitizes. (The discriminator 0xF0 is the proof: it's outside the
//   sanitize allowlist, which is exactly why it "can never begin a real matched
//   string" — §4.6.)

// CRITICAL — suppress the legacy 0/1 ack for typed commands. handle_typed_command
//   owns the [0x51] response (§4.6). Since typed_mode is RESET at the ETX
//   boundary (RISK-1), it can't be read post-loop to decide suppression. Use a
//   LOCAL `bool typed_dispatched` set in the typed ETX branch, checked post-loop
//   with `if (!typed_dispatched)`. VERIFIED: typed messages produce NO raw_hid_send
//   call; legacy messages produce exactly one.

// GOTCHA — RISK-1: reset typed_mode = false at BOTH (a) the ETX boundary
//   (alongside msg_index=0; dropping=false) and (b) the overflow branch (when
//   dropping triggers mid-message). An oversized typed message is dropped; the
//   next message must start unclassified. Both resets are in this PRP.

// GOTCHA — the stub handle_typed_command must be DEFINED (not just declared) or
//   the test drivers won't LINK (the call is in the object regardless of runtime
//   path). A forward declaration alone => undefined-reference link error. The
//   stub returns false + (void)buf; P1.M2.T2 replaces it with the real dispatch.

// GOTCHA — no NEW -Wunused warning. typed_mode is read+written (used); the stub
//   handle_typed_command is called inside the if(typed_mode) branch (used). The
//   only allowed warnings are the 4 pre-existing S1 ones (host_layer,
//   host_cb_enabled, has_been_queried, board_rules_present). If a new warning
//   appears, the stub isn't being called or typed_mode isn't read.

// GOTCHA — do NOT validate NOTIFY_CMD_DISCRIMINATOR's value; use the macro name
//   from notifier.h (notifier.h:44). Hardcoding 0xF0 couples to the literal.

// GOTCHA — use `length >= 3` in the discriminator check (you need data[2] to
//   exist). A 2-byte report (magic only) can't be typed. The coexistence guard
//   already ensured length >= 2; this adds the >= 3 for the discriminator read.
```

## Implementation Blueprint

### Data models and structure

One new file-scope global (`typed_mode`) + one stub function
(`handle_typed_command`) + two locals in `hid_notify` (`match` exists; add
`typed_dispatched`). No new types, no new includes (`bool`/`true`/`false` are in
scope via `<stdbool.h>` already pulled by notifier.h/pattern_match).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — ADD the typed_mode global (~L91, after dropping)
  - PLACE: immediately after the `static bool dropping = false;` line (after its
    trailing comment block), before the `// Default empty map` comment.
  - ADD (with the Mode-A comment):
        /* typed_mode — set on the first report of a message whose data[2] ==
         * NOTIFY_CMD_DISCRIMINATOR (0xF0), read at ETX to route to
         * handle_typed_command, and reset at every ETX/overflow boundary
         * (RISK-1). Persists across hid_notify calls for multi-report msgs. */
        static bool typed_mode = false;
  - NAMING: typed_mode (static bool, snake_case). PERSISTS across calls (file-scope).

Task 2: MODIFY notifier.c — ADD the handle_typed_command STUB just before hid_notify
  - PLACE: immediately before `void hid_notify(uint8_t *data, uint8_t length) {`
    (after the preceding function's closing brace + blank line).
  - ADD:
        /* STUB — real handle_typed_command lands in P1.M2.T2 (typed response
         * builder + the four handlers). Present here so hid_notify's typed
         * branch links. The real version owns the [0x51] typed response (§4.6)
         * and routes cmd_id to QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT. */
        static bool handle_typed_command(char *buf) {
            (void)buf;
            return false;
        }
  - DEPENDENCIES: none (pure stub). Returns bool so `match = handle_typed_command(...)`
    type-checks. P1.M2.T2 replaces this body.

Task 3: MODIFY hid_notify — discriminator check (after guard, before strip)
  - LOCATE: the lines
        return; // Discard the message if it doesn't match
    }
    (blank)
    // Strip off those 2 identifying characters
  - INSERT between the guard's `}` and the `// Strip` comment the Mode-A block +
    the check (see "Exact code"). The check reads data[2] BEFORE data += 2.
  - CONDITION: msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR.

Task 4: MODIFY hid_notify — add typed_dispatched local
  - LOCATE: `bool match = false;` then `for (uint8_t i = 0; i < length; i++) {`.
  - INSERT `bool typed_dispatched = false;` between them.

Task 5: MODIFY hid_notify — typed/legacy fork in the ETX if(!dropping) block
  - LOCATE: the block (see "Exact code" OLD/NEW). It currently unconditionally
    calls sanitize_string + process_full_message.
  - REPLACE with an `if (typed_mode) { match = handle_typed_command(...);
    typed_dispatched = true; } else { sanitize_string(...); match = process_full_message(...); }`.
  - PRESERVE: the legacy branch body verbatim (sanitize comment + call + process_full_message).

Task 6: MODIFY hid_notify — reset typed_mode at ETX + overflow (RISK-1)
  - 6a: in the ETX branch, after `msg_index = 0;` + `dropping = false;`, ADD
        `typed_mode = false;`.
  - 6b: in the overflow `else` branch, after `msg_index = 0;` + `dropping = true;`,
        ADD `typed_mode = false;`.

Task 7: MODIFY hid_notify — guard the post-loop legacy response
  - LOCATE: the 3-line block
        uint8_t response[RAW_REPORT_SIZE] = {0};
        response[0] = match;
        raw_hid_send(response, RAW_REPORT_SIZE);
    (immediately before hid_notify's closing `}`).
  - WRAP in `if (!typed_dispatched) { … }` + the Mode-A comment (see "Exact code").

Task 8: VERIFY (no edit) — compile + regression + routing probe
  - Run Validation Level 1 (stub-compile; exit 0; only 4 S1 warnings).
  - Run Validation Level 2 (dispatch 14/14 + os 31/31, 0 FAIL).
  - Run Validation Level 3 (git diff confined to notifier.c).
  - Run Level 4 (routing probe: typed bypasses + ack suppression; legacy unchanged).
```

**The exact code** — the 7 edits. OLD → NEW for each (byte-exact, validated):

**Edit 1** — typed_mode global (insert after `static bool dropping = false;` + its comment block, before `// Default empty map`):
```c
/* typed_mode — set on the first report of a message whose data[2] ==
 * NOTIFY_CMD_DISCRIMINATOR (0xF0), read at ETX to route to
 * handle_typed_command, and reset at every ETX/overflow boundary (RISK-1).
 * Persists across hid_notify calls for multi-report messages. */
static bool typed_mode = false;
```

**Edit 2** — stub (insert immediately before `void hid_notify(...)`):
```c
/* STUB — real handle_typed_command lands in P1.M2.T2 (typed response builder +
 * the four handlers). Present here so hid_notify's typed branch links. The real
 * version owns the [0x51] typed response (§4.6) and routes cmd_id to
 * QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT. */
static bool handle_typed_command(char *buf) {
    (void)buf;
    return false;
}
```

**Edit 3** — discriminator check. OLD:
```c
        return; // Discard the message if it doesn't match
    }

    // Strip off those 2 identifying characters
```
NEW:
```c
        return; // Discard the message if it doesn't match
    }

    /* §4.6 typed-command discriminator: data[2] == 0xF0 routes to the typed
     * path (handle_typed_command), which bypasses process_full_message so it
     * has NO board disable/deactivate side effects. Checked ONLY on the first
     * report of a message (msg_index == 0): the discriminator is at data[2] in
     * the first report, but continuation reports carry payload there (which may
     * coincidentally be 0xF0). Legacy strings have a printable data[2]
     * (0x20-0x7E), never 0xF0, so this routing is transparent to them. */
    if (msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR) {
        typed_mode = true;
    }

    // Strip off those 2 identifying characters
```

**Edit 4** — typed_dispatched local. OLD: `    bool match = false;\n    for (uint8_t i = 0; i < length; i++) {`
NEW: `    bool match = false;\n    bool typed_dispatched = false;   /* true iff a typed msg was serviced on ETX this call */\n    for (uint8_t i = 0; i < length; i++) {`

**Edit 5** — typed/legacy fork. OLD (the `if (!dropping)` block):
```c
            if (!dropping) {
                // Sanitize the buffer in place, iterating by explicit length so an
                // embedded NUL is stripped (PRD F2.3) rather than truncating the scan.
                // sanitize_string NUL-terminates at write_ptr (<= str + msg_index).
                sanitize_string(msg_buffer, (size_t)msg_index);
                
                match = process_full_message(msg_buffer);
            }
```
NEW:
```c
            if (!dropping) {
                if (typed_mode) {
                    /* Typed path (§4.6): handle_typed_command owns the [0x51]
                     * response and bypasses process_full_message (no board
                     * disable/deactivate). NO sanitize — the typed payload is
                     * binary (cmd_id + args), not an ASCII string. */
                    match = handle_typed_command(msg_buffer);
                    typed_dispatched = true;   /* suppress the legacy 0/1 ack below */
                } else {
                    // Sanitize the buffer in place, iterating by explicit length so an
                    // embedded NUL is stripped (PRD F2.3) rather than truncating the scan.
                    // sanitize_string NUL-terminates at write_ptr (<= str + msg_index).
                    sanitize_string(msg_buffer, (size_t)msg_index);
                    match = process_full_message(msg_buffer);
                }
            }
```

**Edit 6** — typed_mode resets (RISK-1).
6a (ETX boundary). OLD: `            msg_index = 0; // Reset the buffer for the next message\n            dropping = false;\n            break;`
NEW:
```c
            msg_index = 0; // Reset the buffer for the next message
            dropping = false;
            typed_mode = false;   /* RISK-1: reset at every ETX boundary */
            break;
```
6b (overflow). OLD: `                msg_index = 0;\n                dropping = true;\n            }`
NEW:
```c
                msg_index = 0;
                dropping = true;
                typed_mode = false;   /* RISK-1: dropped msg clears typed classification */
            }
```

**Edit 7** — guard legacy response. OLD:
```c
    uint8_t response[RAW_REPORT_SIZE] = {0};
    response[0] = match;
    raw_hid_send(response, RAW_REPORT_SIZE);
}
```
NEW:
```c
    /* Legacy acknowledgement only. Typed responses are sent INSIDE
     * handle_typed_command ([0x51]...), so hid_notify must NOT also send the
     * legacy 0/1 ack for typed commands (typed_dispatched guards it). */
    if (!typed_dispatched) {
        uint8_t response[RAW_REPORT_SIZE] = {0};
        response[0] = match;
        raw_hid_send(response, RAW_REPORT_SIZE);
    }
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: persistent classification flag + local dispatch flag. typed_mode
//   (file-scope) classifies the message across reports (set report 1, read at
//   ETX). typed_dispatched (local) records whether THIS call serviced a typed
//   ETX, to suppress the post-loop legacy ack. Two distinct concerns => two vars.

// PATTERN: read data[2] BEFORE stripping the header. After the coexistence guard,
//   data still points at byte 0, so data[2] is the post-magic byte. The strip
//   (data += 2) happens AFTER the discriminator check.

// PATTERN: discriminator only on the first report (msg_index == 0). This is what
//   makes multi-report typed framing safe: report-2's data[2] is payload, not a
//   discriminator, even if it coincidentally equals 0xF0.

// ANTI-PATTERN: do NOT sanitize the typed payload. sanitize_string would strip
//   0xF0 + binary args. Only the legacy branch sanitizes.

// ANTI-PATTERN: do NOT send the legacy 0/1 ack for typed commands. handle_typed_command
//   owns the [0x51] response (§4.6). Guard the post-loop block with !typed_dispatched.
//   (Sending it would double-respond; the host would see both [0x51] and [0|1].)

// ANTI-PATTERN: do NOT forward-declare handle_typed_command without a definition.
//   The test drivers LINK notifier.o; a declaration-only stub => undefined reference.
//   Define the stub before hid_notify (definition-before-use, no forward decl needed).

// ANTI-PATTERN: do NOT forget a typed_mode reset point. RISK-1 requires reset at the
//   ETX boundary AND the overflow branch. A missed reset leaks classification into
//   the next message (e.g., a legacy message after a dropped typed one would be
//   mis-routed to handle_typed_command).

// ANTI-PATTERN: do NOT hardcode 0xF0. Use NOTIFY_CMD_DISCRIMINATOR (notifier.h:44).

// ANTI-PATTERN: do NOT touch the S1 host globals (host_layer/host_cb_enabled/
//   has_been_queried/board_rules_present) or the S2 apply_os_change seam. They are
//   orthogonal scaffolding/refactor with disjoint edit regions.
```

### Integration Points

```yaml
HID_NOTIFY (the fork):
  - file: notifier.c (L525-577)
  - change: +discriminator check (after guard), +typed/legacy fork (ETX branch),
            +typed_mode resets (ETX + overflow), +legacy-ack guard (post-loop).
NEW GLOBAL:
  - typed_mode (static bool, ~L91): written (discriminator check, ETX/overflow reset),
    read (ETX typed branch). Persists across hid_notify calls.
NEW STUB:
  - handle_typed_command (static, before hid_notify): returns bool. P1.M2.T2 replaces.
CONSUMERS (downstream, NOT this task):
  - handle_typed_command real impl (P1.M2.T2): QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT.
  - set_host_layer (P1.M2.T1.S2), apply_host_callbacks (P1.M2.T1.S3): called by APPLY_HOST_CONTEXT.
BUILD/CONFIG/ROUTES/DATABASE:
  - none. No rules.mk, no wire change, no header change, no runtime config.
```

## Validation Loop

> Toolchain: gcc (C project; no ruff/mypy/pytest). notifier.c is stub-compiled
> with `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.` (the exact
> command `run_notifier_stub_tests.sh` uses). All commands below were **executed
> during research against a temp notifier.c carrying all 7 edits and PASSED**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c. Expect exit 0 AND ONLY the 4 pre-existing S1
#     -Wunused warnings (host_layer, host_cb_enabled, has_been_queried,
#     board_rules_present). NO new warnings (typed_mode + stub are used).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
echo "compile exit=$?  (expect 0)"
echo "-- warnings (expect ONLY the 4 S1 -Wunused lines) --"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep 'warning:' | sed 's/^[^:]*://'
# Assert NO typed_mode / handle_typed_command warning:
! gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep -qE 'typed_mode|handle_typed_command' \
  && echo "OK: typed_mode + stub are used (no unused warning)" \
  || echo "FAIL: unexpected warning on typed_mode/handle_typed_command"

# 1b. Confirm the fork landed: discriminator check, typed branch, both resets, the guard.
grep -q 'msg_index == 0 && length >= 3 && data\[2\] == NOTIFY_CMD_DISCRIMINATOR' notifier.c && echo "OK discriminator check"
grep -q 'match = handle_typed_command(msg_buffer);' notifier.c && echo "OK typed branch"
grep -q 'bool typed_dispatched' notifier.c && echo "OK typed_dispatched local"
grep -q 'static bool typed_mode = false;' notifier.c && echo "OK typed_mode global"
test "$(grep -c 'typed_mode = false;' notifier.c)" -ge 2 && echo "OK both resets (ETX + overflow)"
grep -q 'if (!typed_dispatched)' notifier.c && echo "OK legacy-ack guard"

rm -f /tmp/notifier_stub.o
```

### Level 2: Component Validation (routing probe — typed bypasses + ack suppression)

```bash
cd /home/dustin/projects/qmk-notifier

gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o

# Probe: send a legacy msg (acks + board path) vs a typed msg (board bypassed,
# no ack) vs a multi-report typed msg (msg_index==0 rule). Observe raw_hid_send
# via the stub's stderr trace.
cat > /tmp/route_probe.c <<'EOF'
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"
void hid_notify(uint8_t *data, uint8_t length);
DEFINE_SERIAL_COMMANDS({ {"x", 0, 0, false} });
int main(void) {
    uint8_t leg[32]; memset(leg,0,32); leg[0]=0x81; leg[1]=0x9F; leg[2]='h'; leg[3]='i'; leg[4]=0x03;
    fprintf(stderr,"LEGACY:"); hid_notify(leg,32);
    uint8_t typ[32]; memset(typ,0,32); typ[0]=0x81; typ[1]=0x9F; typ[2]=0xF0; typ[3]=0x01; typ[4]=0x03;
    fprintf(stderr,"TYPED:"); hid_notify(typ,32);
    uint8_t r1[32]; memset(r1,0,32); r1[0]=0x81; r1[1]=0x9F; r1[2]=0xF0; r1[3]=0x05; r1[4]=0xAA; r1[5]=0xBB;
    fprintf(stderr,"MULTI1:"); hid_notify(r1,32);
    uint8_t r2[32]; memset(r2,0,32); r2[0]=0x81; r2[1]=0x9F; r2[2]=0xF0; r2[3]=0x01; r2[4]=0x03;
    fprintf(stderr,"MULTI2:"); hid_notify(r2,32);
    return 0;
}
EOF
gcc -w -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c /tmp/route_probe.c -o /tmp/route_probe 2>/dev/null
echo "-- raw_hid_send per marker (LEGACY: ack; TYPED: none; MULTI1: ack; MULTI2: none) --"
/tmp/route_probe 2>&1 | grep -E '^(LEGACY|TYPED|MULTI)|response\[0\]=' | grep -A1 -E '^(LEGACY|TYPED|MULTI)' \
  | grep -oE '(LEGACY|TYPED|MULTI[12])|response\[0\]=[0-9]+' | tr '\n' ' '; echo
# Expected: "LEGACY response[0]=0 TYPED MULTI1 response[0]=0 MULTI2 "
#   LEGACY  -> response[0]=0  (ack sent; board path ran)
#   TYPED   -> NO response    (board bypassed; ack suppressed by typed_dispatched)
#   MULTI1  -> response[0]=0  (partial report, no ETX: harmless ack; host ignores !=0x51)
#   MULTI2  -> NO response    (typed ETX; msg_index==0 rule prevented re-classification)
rm -f /tmp/route_probe.c /tmp/route_probe
```

### Level 3: Integration Testing (Regression — the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Full existing gate (both notifier suites via the runner). MUST end
#     "✓ notifier stub-compile gate PASSED" with 0 FAIL: lines for both suites.
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "stub-tests exit=$?"
tail -n 4 /tmp/ns.out
# Expected: "notifier dispatch fails=0 (exit=0)", "notifier os fails=0 (exit=0)",
#           "✓ notifier stub-compile gate PASSED".

# 3b. Explicit per-suite regression (belt-and-suspenders). Both never send 0xF0
#     => typed branch inert => behavior byte-identical (findings F5).
gcc -Wall -std=c99 -Iqmk_stubs -I. -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/td
gcc -Wall -std=c99 -Iqmk_stubs -I. -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -o /tmp/tos
echo "dispatch fails=$(/tmp/td 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
echo "os fails=$(/tmp/tos 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
rm -f /tmp/ns.out /tmp/td /tmp/tos
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Mode-A doc anchors (item §5).
grep -q '§4.6 typed-command discriminator' notifier.c && echo "discriminator doc ok"
grep -q 'NO board disable/deactivate side effects' notifier.c && echo "bypass doc ok"
grep -q 'first report of a message (msg_index == 0)' notifier.c && echo "first-report rule doc ok"
grep -q 'RISK-1' notifier.c && echo "RISK-1 mitigation doc ok"
grep -q 'Typed responses are sent INSIDE' notifier.c && echo "ack-suppression doc ok"

# 4b. Use the macro, not the literal.
grep -q 'data\[2\] == NOTIFY_CMD_DISCRIMINATOR' notifier.c && echo "uses macro (good)"
! grep -q 'data\[2\] == 0xF0' notifier.c && echo "no hardcoded literal (good)"

# 4c. typed_mode write sites: 1 set (discriminator) + 2 resets (ETX + overflow).
echo "typed_mode writes (expect 3: 1 set + 2 resets):"
grep -nE 'typed_mode = (true|false);' notifier.c

# 4d. Diff hygiene: only notifier.c changed (besides plan/ artifacts).
git status --porcelain | grep -vE 'plan/003_16d737de7a3e/P1M2T1S1/' | grep -E '\.c|\.h|\.sh|\.mk' \
  && echo "FAIL: source file other than notifier.c changed" || echo "OK: only notifier.c (+ plan/)"
git diff --stat -- notifier.c
# Expected: only notifier.c listed; hunks around L91 (global), ~L520 (stub), L525-580 (hid_notify).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: notifier.c stub-compiles (exit 0); only the 4 S1 `-Wunused` warnings; no new.
- [ ] Level 1: discriminator check, typed branch, typed_dispatched, both typed_mode resets, ack-guard all present.
- [ ] Level 2: routing probe — LEGACY acks (response[0]=0); TYPED no ack (board bypassed); MULTI1 acks (partial); MULTI2 no ack (typed ETX).
- [ ] Level 3: `run_notifier_stub_tests.sh` PASSED; dispatch 14/14 + os 31/31, 0 FAIL.
- [ ] Level 4: all doc anchors present; macro used (no literal); 3 typed_mode writes; only notifier.c changed.

### Feature Validation

- [ ] `data[2] == 0xF0` (first report) sets typed_mode; typed ETX routes to handle_typed_command (no process_full_message, no sanitize).
- [ ] Legacy `data[2]` printable path byte-identical to before (sanitize + process_full_message + 0/1 ack).
- [ ] Multi-report typed framing reuses msg_buffer/msg_index; msg_index==0 rule prevents coincidental-0xF0 mis-trigger.
- [ ] typed_mode reset at ETX + overflow (RISK-1); legacy 0/1 ack suppressed for typed (typed_dispatched).

### Code Quality Validation

- [ ] typed_mode is file-scope static (persists across calls); typed_dispatched is a hid_notify local.
- [ ] Stub handle_typed_command defined (not just declared) so test drivers link.
- [ ] Discriminator check reads data[2] before the strip; uses NOTIFY_CMD_DISCRIMINATOR macro.
- [ ] No anti-patterns (see below): no typed sanitize, no double-ack, no missing reset, no literal 0xF0.

### Documentation & Deployment

- [ ] Mode-A comments cite §4.6 (discriminator, ETX framing, board-side-effect bypass), the msg_index==0 first-report rule, and RISK-1.
- [ ] Stub comment marks it as P1.M2.T2's replacement target.
- [ ] handle_typed_command real impl + set_host_layer + apply_host_callbacks are LATER subtasks (boundaries clear).

---

## Anti-Patterns to Avoid

- ❌ Don't sanitize the typed payload — `sanitize_string` strips 0xF0 + binary args. Only the legacy branch sanitizes.
- ❌ Don't send the legacy 0/1 ack for typed commands — `handle_typed_command` owns the [0x51] response. Guard with `!typed_dispatched`.
- ❌ Don't read the discriminator AFTER `data += 2` — `data[2]` is the post-magic byte only while `data` still points at byte 0 (before the strip).
- ❌ Don't drop the `msg_index == 0` guard — continuation reports' `data[2]` may coincidentally be 0xF0; only classify on the first report.
- ❌ Don't forward-declare `handle_typed_command` without a definition — the test drivers LINK notifier.o; declaration-only ⇒ undefined reference. Define the stub before hid_notify.
- ❌ Don't miss a `typed_mode = false` reset — RISK-1 requires reset at BOTH the ETX boundary and the overflow branch. A missed reset leaks classification into the next message.
- ❌ Don't hardcode `0xF0` — use `NOTIFY_CMD_DISCRIMINATOR` (notifier.h:44).
- ❌ Don't touch the S1 host globals (host_layer/host_cb_enabled/has_been_queried/board_rules_present) or the S2 apply_os_change seam — disjoint edit regions.
- ❌ Don't touch notifier.h, pattern_match.*, qmk_stubs/*, test_notifier_*, run_*.sh, PRD.md, tasks.json, prd_snapshot.md, rules.mk, or .gitignore. Only `notifier.c` changes.

---

## Confidence Score: 10/10

The deliverable is 7 surgical edits to `notifier.c` (1 new global `typed_mode`, 1
stub `handle_typed_command`, the discriminator check, the typed/legacy ETX fork,
the two RISK-1 resets, and the `!typed_dispatched` legacy-ack guard), all
specified OLD→NEW verbatim above and **empirically validated during research** by
applying them to a /tmp copy via a python surgical replace: stub-compiles exit 0
with **only the 4 pre-existing S1 `-Wunused` warnings** (no new — `typed_mode` is
read+written, the stub is called); a routing probe confirms legacy messages ack
+ run the board path (`response[0]=0`) while typed messages (`data[2]==0xF0`)
**bypass process_full_message and suppress the legacy ack** (no response line),
including a multi-report typed case where report-2's coincidental `data[2]==0xF0`
did NOT re-trigger (the `msg_index==0` rule held); and both regression suites
pass unchanged (**dispatch 14/14, os 31/31, 0 FAIL**; "✓ notifier stub-compile
gate PASSED"). The dependencies (`NOTIFY_CMD_DISCRIMINATOR` macro, S1 host
globals) are LANDED; the parallel S2 (`apply_os_change` seam) has a disjoint edit
region; the stub is replaced by P1.M2.T2 with no API change. The §4.6 contract
(discriminator, ETX framing, board-side-effect bypass), the `msg_index==0`
first-report rule, and RISK-1 (typed_mode lifecycle) are all encoded. No external
dependencies added.