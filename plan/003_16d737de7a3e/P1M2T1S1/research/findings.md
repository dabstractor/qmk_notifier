# Research Notes — P1.M2.T1.S1 (typed_mode flag + 0xF0 discriminator routing fork)

## Task scope
Add the typed-command routing fork to `hid_notify` so a `data[2] == 0xF0`
message routes to `handle_typed_command()` (a stub for now; real impl in
P1.M2.T2) and BYPASSES `process_full_message` (no board disable/deactivate side
effects). Legacy string messages take the unchanged path. The fork reuses the
existing `msg_buffer`/`msg_index`/`dropping` reassembly (multi-report typed
framing — findings F7). A `typed_mode` flag (RISK-1) governs routing and resets
at every ETX/overflow boundary.

7 edits, all in `notifier.c`. No other file.

## Status of dependencies (all LANDED before this task)
- **P1.M1.T1.S1**: `NOTIFY_CMD_DISCRIMINATOR` (0xF0) + `NOTIFY_RESPONSE_MARKER`
  (0x51) + `HOST_LAYER_BASE` (224) + `host_callback_t`/`DEFINE_HOST_CALLBACKS`
  in `notifier.h` — LANDED (notifier.h:42-46, 63, 69-72).
- **P1.M1.T2.S1**: host state globals `host_layer` (L137), `host_cb_enabled`
  (L138), `has_been_queried` (L139) + `board_rules_present()` helper (L198) —
  LANDED, currently UNUSED (the 4 expected `-Wunused` warnings). This task does
  NOT touch them; they stay unused until P1.M2.T2.
- **P1.M1.T2.S2** (parallel): the `apply_os_change` seam refactor — being
  implemented now. It does NOT touch `hid_notify` or `typed_mode`. No overlap.

## Ground-truth locations (current notifier.c, 577 lines)
- `msg_index` (L83), `msg_buffer` (L82), `dropping` (L90) — reassembly machinery;
  `typed_mode` is added right after `dropping`.
- `hid_notify` at **L525-577**. Structure (confirmed by full read): coexistence
  guard (L527) → `data += 2; length -= 2;` strip (L532-533) → `bool match=false`
  → byte loop with ETX/dropping/append branches → post-loop
  `response[0]=match; raw_hid_send(...)` (L575-576).
- `handle_typed_command` is NOT yet declared/defined (only mentioned in the L134
  comment). This task adds a STUB definition so the typed branch links.
- `notifier.h:44` `#define NOTIFY_CMD_DISCRIMINATOR 0xF0`.

## The 7 edits (all validated end-to-end)
1. **`static bool typed_mode = false;`** added after the `dropping` block (~L91).
   Persists across hid_notify calls (set on first report, read at ETX).
2. **Stub `handle_typed_command(char *buf)`** added just before `hid_notify`
   (returns false, `(void)buf`). Lets the typed branch LINK; real impl is P1.M2.T2.
3. **Discriminator check** inserted after the coexistence guard, BEFORE
   `data += 2`: `if (msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR) { typed_mode = true; }`.
   The `msg_index == 0` guard ensures the discriminator is read only from the
   FIRST report (continuation reports' data[2] may coincidentally be 0xF0).
4. **`bool typed_dispatched = false;`** local added before the byte loop.
5. **Typed/legacy fork in the ETX `if (!dropping)` block**: typed →
   `match = handle_typed_command(msg_buffer); typed_dispatched = true;` (NO
   sanitize — typed payload is binary, not ASCII); legacy → unchanged
   `sanitize_string` + `process_full_message`.
6. **`typed_mode = false;`** added at the ETX boundary reset (alongside
   `msg_index = 0; dropping = false;`) AND in the overflow branch (RISK-1).
7. **Post-loop legacy response guarded**: `if (!typed_dispatched) { ... }` so
   typed commands do NOT get a spurious legacy 0/1 ack (handle_typed_command
   owns the [0x51] response).

## Key design decision: suppressing the legacy ack for typed commands
The contract says "hid_notify does NOT send the legacy 0/1 ack for typed
commands; the response-sending is owned by handle_typed_command". Since
`typed_mode` is RESET at the ETX boundary (RISK-1), it cannot be read post-loop
to decide suppression. Solution: a LOCAL `bool typed_dispatched` set in the ETX
typed branch, checked post-loop. This cleanly separates "was a typed message
serviced on this call" from the persistent `typed_mode` classification flag.
- Legacy ETX → typed_dispatched=false → send [0|1] (unchanged).
- Typed ETX → typed_dispatched=true → skip (handle_typed_command sent [0x51]).
- Intermediate (no-ETX) any → typed_dispatched=false → send [0x00...] (unchanged
  multi-report behavior; harmless — host treats response[0]!=0x51 as non-typed).

## Empirical validation (PASSED)
Applied all 7 edits to a /tmp copy via a python surgical replace, then:
1. **Stub-compile** (`-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=… -Iqmk_stubs -I.`):
   rc=0; **only the 4 pre-existing S1 warnings** (host_layer, host_cb_enabled,
   has_been_queried, board_rules_present). NO new warnings — `typed_mode` is
   read+written (used); the stub `handle_typed_command` is called (used).
2. **Routing probe** (legacy vs typed vs multi-report, observing raw_hid_send):
   - LEGACY `data[2]='h'` → `response[0]=0` sent (ack + board path ran). ✓
   - TYPED `data[2]=0xF0` (QUERY_INFO) → NO response line (board BYPASSED, ack
     SUPPRESSED). ✓
   - MULTI-report typed (r1: 0xF0+payload no ETX; r2: ETX with data[2]=0xF0
     coincidentally) → r1 sends `response[0]=0` (harmless partial); r2 sends NO
     response (typed path, msg_index==0 rule prevented re-classification). ✓
3. **Regression**: `test_notifier_dispatch` **14/14, 0 FAIL**; `test_notifier_os`
   **31/31, 0 FAIL** (both suites never send 0xF0, so the typed branch is inert
   for them — findings F5 confirmed).

## RISK-1 (typed_mode lifecycle) — MITIGATED
Reset `typed_mode = false` at:
- (a) the ETX boundary (normal/typed/dropped dispatch) — edit 6a.
- (b) the overflow branch (an oversized typed msg is dropped; next msg starts
  unclassified) — edit 6b.
Both verified present in the /tmp build. The `msg_index == 0` first-report guard
(edit 3) ensures typed_mode is set only from the first report of a message.

## Files touched / NOT touched
- TOUCH: `notifier.c` (7 edits, all in the hid_notify region + the new global + stub).
- DO NOT TOUCH: notifier.h, pattern_match.*, qmk_stubs/*, test_notifier_*,
  run_*.sh, PRD.md, tasks.json, prd_snapshot.md, rules.mk, .gitignore.
- handle_typed_command real impl + set_host_layer + apply_host_callbacks are
  LATER subtasks (P1.M2.T2, P1.M2.T1.S2/S3). This task's stub is replaced then.