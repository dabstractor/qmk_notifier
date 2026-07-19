# PRP — P1.M2.T2.S3: Implement APPLY_HOST_CONTEXT handler (clear_board + set_host_layer + apply_host_callbacks)

## Goal

**Feature Goal**: Add the **`APPLY_HOST_CONTEXT` (cmd_id `0x05`) typed-command
handler** — the **fourth and final** handler in `handle_typed_command`'s switch in
`notifier.c`. It honors the host's per-window **stack/replace** decision via the
`clear_board` flag (§4.6/§14): when set, it tears down **board** state
(`deactivate_layer()` + `disable_command()`) **before** applying; then it applies
the **host** layer (`set_host_layer`, 0xFF clears) and the **host** callback set
(`apply_host_callbacks`, disable-before-enable diff; count==0 disables all); then
replies `[0x51][0x05][ack=1]`. Board and host state planes are otherwise orthogonal
(invariant 21). This **retires the last two carried `-Wunused-function` warnings**
(`set_host_layer` + `apply_host_callbacks` finally have a caller), completing the
typed-command milestone.

**Deliverable**: A new `case NOTIFY_CMD_APPLY_HOST_CONTEXT:` (0x05) block **inserted
before the `default:` case** in the existing `handle_typed_command` switch in
`notifier.c` (the file already carries the `QUERY_INFO`/`QUERY_CALLBACK`/`SET_OS`
cases + the placeholder `default` — P1.M2.T2.S1/S2 landed). The case is ~22 lines
(+ a Mode-A block comment) and includes a **defensive `count` clamp** so a
malformed/garbled count never reads past `msg_buffer`. Two **comment-only updates**
ride with the work: (1) the `default:` comment drops the now-stale "0x05" (0x05 is
now handled; only 0x04 reserved remains in `default`); (2) the `handle_typed_command`
header comment is updated to list all four implemented handlers (it currently
wrongly claims SET_OS/APPLY_HOST_CONTEXT "land in S2/S3"). No new files, no new
constants, no new functions, no new globals, no `#include`s.

**Success Definition**:
- `case NOTIFY_CMD_APPLY_HOST_CONTEXT:` is present, placed immediately before
  `default:`, with `case NOTIFY_CMD_SET_OS:` still before it (switch order:
  `0x01 → 0x02 → 0x03 → 0x05 → default`).
- It parses `layer=(uint8_t)data[2]`, `flags=(uint8_t)data[3]`,
  `count=(uint8_t)data[4]`, clamps `count` to `MSG_BUFFER_SIZE - 5` (251) so the
  `ids` tail never reads past `msg_buffer`, and takes `ids=(uint8_t*)&data[5]`.
- If `flags & 0x01` (clear_board = replace): it calls `deactivate_layer()` then
  `disable_command()` — board teardown **before** applying. Else (stack): board
  state untouched.
- It then calls `set_host_layer(layer)` (0xFF clears the host layer) and
  `apply_host_callbacks(ids, count)` (disable-before-enable diff).
- It builds `uint8_t payload[1] = {0x01}` and calls
  `send_typed_response(NOTIFY_CMD_APPLY_HOST_CONTEXT, payload, 1)` then `break;`
  (the single post-switch `return true;` is the only exit).
- `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c`
  → **exit 0, ZERO warnings** (the `set_host_layer` + `apply_host_callbacks`
  warnings retire this task — **VERIFIED empirically** on a `/tmp` copy).
- `./run_notifier_stub_tests.sh` → **dispatch fails=0, os fails=0** (no regression).
- A `/tmp` **multi-TU** harness (strong `DEFINE_HOST_CALLBACKS` override; capturing
  `raw_hid_send`; a layer-call log; driving the PUBLIC `hid_notify`) confirms ALL of:
  response `[0x51][0x05][0x01]`+zero-pad; **stack** (clear_board=0) leaves board
  state untouched while applying host layer+cb; **replace** (clear_board=1) clears
  the board layer+command BEFORE applying the host layer+cb; **ordering** (board
  `on_disable` before host `on_enable` across a replace); `layer=0xFF` clears the
  host layer; `count==0` disables all host callbacks; the disable-before-enable
  diff (`{0}→{1}` fires `di0` then `en1`); and the **defensive clamp** (count=0xFF
  → no crash / no OOB read). **(VERIFIED — ALL CASES CONFIRMED during research.)**
- Mode-A comment present on the case (item-spec §5): "Per-window stack/replace via
  clear_board flag (§14). clear_board=1: deactivate board layer + disable board
  command BEFORE applying (replace). clear_board=0: board state untouched (stack).
  Then set_host_layer + apply_host_callbacks."

## User Persona (if applicable)

**Target User**: The QMKonnect desktop host. End users never send typed commands
directly; the host sends `APPLY_HOST_CONTEXT` on every window change, after the
§4.6 capability handshake (`QUERY_INFO` → `proto_ver == 2`, `flags & 0x01`).

**Use Case**: Host matches a focused window for app "code"; its `rules.toml` maps
that to host layer 224 + host-callback ids {0, 2}. Per its per-rule
`disable_firmware_config` flags it picks **stack** (clear_board=0, the board's
own string rule should also run) or **replace** (clear_board=1, suppress the board).
It sends `[0x81][0x9F][0xF0][0x05][E0=224][flags][02][00][02][0x03]`. The firmware
(optionally clears board), sets host layer 224, diffs callbacks {0,2}, replies
`[0x51][0x05][0x01]`. On the next window it may send `[0xFF]` to clear the host
layer and/or `count=0` to disable all callbacks.

**User Journey**: `QUERY_INFO` (S1) detects capability → `SET_OS` (S2) declares OS
→ on each window change the host sends `APPLY_HOST_CONTEXT` (this task) →
firmware applies stack/replace + host layer + callbacks → reply `[0x51][0x05][0x01]`.

**Pain Points Addressed**: Lets the host push layer/callback decisions per-window
without reflashing, choosing whether the board's own `DEFINE_*` rules keep running
(stack) or go inert (replace) for that window — §14's coexistence design.

## Why

- **Completes the typed-command milestone.** P1.M2.T2.S1 landed the dispatch +
  `QUERY_INFO`/`QUERY_CALLBACK`; S2 landed `SET_OS`; S3 (this) lands the final
  handler `APPLY_HOST_CONTEXT`. After this, only `0x04` (reserved for VIA-coexist)
  falls through to `default`.
- **Retires the last two carried warnings.** `set_host_layer` (P1.M2.T1.S2) and
  `apply_host_callbacks` (P1.M2.T1.S3) have been `defined but not used` since they
  landed — APPLY_HOST_CONTEXT is their sole caller. After this task `gcc -Wall
  -Wextra ... notifier.c` is **warning-free** (the clean, predicted self-resolution).
- **Encodes the per-window stack/replace decision in firmware (§14).** The firmware
  offers BOTH semantics; the host selects per window via the `clear_board` flag.
  This handler is the single place that bridges the two state planes — `clear_board`
  is the ONE action that legitimately touches board state from the typed path
  (invariant 21), and it does so only to clear, BEFORE applying host state.
- **No new mechanism.** `set_host_layer`, `apply_host_callbacks`,
  `deactivate_layer`, `disable_command`, `send_typed_response`, the switch skeleton,
  and `NOTIFY_CMD_APPLY_HOST_CONTEXT` are ALL already present. This task is a pure
  case insertion (with a defensive clamp + a comment edit).

## What

Insert one `case` block + two comment edits into the existing `handle_typed_command`
switch in `notifier.c`. No other file changes.

The handler behavior (msg_buffer layout after magic-strip: `data[0]=0xF0
discriminator, data[1]=cmd_id, data[2..]=args`):
1. **Parse**: `layer=(uint8_t)data[2]`, `flags=(uint8_t)data[3]`,
   `count=(uint8_t)data[4]`, `ids=(uint8_t*)&data[5]` (pointer into the reassembled
   buffer).
2. **Clamp** `count` to `MSG_BUFFER_SIZE - 5` (251) so `ids[i]` never reads past
   `msg_buffer[255]`. (The item contract's "count exceeds available bytes → clamp
   or skip callbacks defensively." See Known Gotchas for why `strlen` is unusable.)
3. **clear_board (replace)**: if `flags & 0x01`, call `deactivate_layer()` then
   `disable_command()` — board teardown BEFORE applying. Else (**stack**): board
   state untouched.
4. **Apply host state**: `set_host_layer(layer)` (0xFF clears the host layer) then
   `apply_host_callbacks(ids, count)` (disable-before-enable diff; `count==0`
   disables all currently-enabled host callbacks).
5. **Reply**: `uint8_t payload[1] = {0x01}; send_typed_response(NOTIFY_CMD_APPLY_HOST_CONTEXT, payload, 1);`
   → on the wire `[0x51][0x05][0x01]` + zero-pad. `break;` (the post-switch
   `return true;` is the only exit).

Plus two comment edits:
- **default-case comment**: drop the now-stale "0x05 until P1.M2.T2.S3 lands" →
  only "0x04 reserved for VIA-coexist" remains. The default **logic**
  (`send_typed_response(cmd_id, NULL, 0); break;`) is unchanged.
- **handle_typed_command header comment**: it currently claims "SET_OS (0x03) and
  APPLY_HOST_CONTEXT (0x05) land in P1.M2.T2.S2/S3; until then they fall through to
  the default". After this task all four are implemented; update to list
  QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT as implemented, leaving only
  reserved 0x04 + unknown ids in `default`.

### Success Criteria

- [ ] `case NOTIFY_CMD_APPLY_HOST_CONTEXT:` block present, placed immediately before
      `default:`, after `case NOTIFY_CMD_SET_OS:`.
- [ ] Body: parse `layer/flags/count`; clamp `count` to `MSG_BUFFER_SIZE - 5`;
      `ids=(uint8_t*)&data[5]`; `if(flags&0x01){deactivate_layer();disable_command();}`;
      `set_host_layer(layer);`; `apply_host_callbacks(ids,count);`;
      `uint8_t payload[1]={0x01}; send_typed_response(NOTIFY_CMD_APPLY_HOST_CONTEXT,payload,1);`;
      `break;`.
- [ ] No `return true;` inside the case (use `break;`); no F9/clear logic duplicated
      beyond the explicit `clear_board` deactivate+disable; no `host_layer=` or
      `host_cb_enabled[]=` direct write (those live in set_host_layer /
      apply_host_callbacks); no `strlen`/NUL-scan of `data`.
- [ ] Mode-A comment present (wording in Goal).
- [ ] `default:` comment updated (drop "0x05"); `default:` logic unchanged.
- [ ] `handle_typed_command` header comment updated (all four handlers listed).
- [ ] Stub-compile of `notifier.c` → exit 0, **ZERO warnings**.
- [ ] `./run_notifier_stub_tests.sh` → dispatch fails=0, os fails=0.
- [ ] Level-2 `/tmp` multi-TU harness: all 8 scenarios pass (response, stack,
      replace, replace-ordering, layer-clear, count0-disable-all, diff-ordering,
      defensive-clamp).

## All Needed Context

### Context Completeness Check

**Pass.** The handler is a ~22-line insertion into a switch whose structure, call
targets, constants, and types are **all already present in the current `notifier.c`**
(verified by direct read): `handle_typed_command` (static, switching on
`(uint8_t)data[1]`) with `QUERY_CALLBACK`/`SET_OS` as the exact structural template
and `default:` as the insertion anchor; `set_host_layer` (static, notifier.c:252 —
P1.M2.T1.S2 COMPLETE) and `apply_host_callbacks` (static, notifier.c:283 —
P1.M2.T1.S3 COMPLETE) as the state machines to drive; `deactivate_layer` (extern)
and `disable_command` (extern) as the board teardown; `send_typed_response` (static,
P1.M2.T2.S1) as the response builder; `NOTIFY_CMD_APPLY_HOST_CONTEXT` (notifier.h)
and `MSG_BUFFER_SIZE` (notifier.c) as the constants. The msg_buffer byte layout
(`data[2]=layer, data[3]=flags, data[4]=count, data[5..]=ids`) is confirmed against
PRD §4.6 + external_deps.md "Field definitions". The exact handler code, the
defensive count-clamp, the `default`/header comment edits, and the Level-2 multi-TU
validation harness were **all empirically validated during research** against a
`/tmp` copy of notifier.c: stub-compile → exit 0, **ZERO warnings**; behavior
harness → **ALL CASES CONFIRMED (0 failures)** across 8 scenarios; regression gate
→ dispatch 0 fail, os 31/31. An implementer with only this PRP + repo access can
reproduce the handler behavior-identically and prove it.

### Documentation & References

```yaml
# MUST READ — the authoritative wire contract for APPLY_HOST_CONTEXT
- file: PRD.md
  section: "### 4.6 Typed-command namespace (canonical owner)"
  why: "The command-table row: 0x05 APPLY_HOST_CONTEXT, Request args [layer][flags][count][id0][id1]…,
        Response payload after [0x51][cmd_echo] = [ack] (1=applied). And the field defs:
        'APPLY_HOST_CONTEXT.layer: the host's desired host-layer number, or 0xFF (255=LAYER_UNSET)
        to clear the host layer. Host layers reserved >= 224.' 'APPLY_HOST_CONTEXT.flags bit 0 =
        clear_board: when set, the firmware deactivate_layer()s its board activated_layer and
        disable_command()s its current board command BEFORE applying the host context.' And
        'APPLY_HOST_CONTEXT.id…: the FULL desired enabled host-callback id set; the firmware
        diffs against its current enabled set and calls on_enable/on_disable accordingly
        (disable-before-enable).'"
  critical: "msg_buffer layout after magic-strip: data[0]=0xF0, data[1]=cmd_id(=0x05),
        data[2]=layer, data[3]=flags, data[4]=count, data[5..]=ids. ack byte is 0x01 (=applied).
        clear_board is bit 0 of flags. The response echoes cmd_id 0x05: [0x51][0x05][0x01]."

- file: PRD.md
  section: "## 14. Host-Side Rules & Typed Commands"
  why: "The stack/replace coexistence design the handler implements. 'Replace (clear_board=1):
        the firmware deactivate_layer()s its board layer and disable_command()s its board command,
        then applies the host layer + callbacks. Board rules are inert for that window.' 'Stack
        (clear_board=0): ... Board layer/command remain active; the host layer stacks above.'
        And: 'APPLY_HOST_CONTEXT honors clear_board (above) then calls set_host_layer() +
        apply_host_callbacks().'"
  critical: "clear_board is the ONE typed action that touches board state (it clears it BEFORE
        applying host state). Otherwise board and host are independent (invariant 21). The handler
        must do clear_board FIRST, then set_host_layer, then apply_host_callbacks — in that order."

- file: PRD.md
  section: "## 16. Appendix B — Constants Reference"
  why: "Locks: APPLY_HOST_CONTEXT clear_board = flags bit 0; Host layer block >= 224 (255 =
        LAYER_UNSET); response marker 0x51; SET_OS 0x03 / APPLY_HOST_CONTEXT 0x05; RAW_REPORT_SIZE 32.
        The constants are #define'd in notifier.h / notifier.c — consume by name, do not hardcode."

# Architecture — the handler pseudocode + the two-plane model
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "### APPLY_HOST_CONTEXT (0x05) — layer + callbacks + stack/replace"
  why: "The single-paragraph spec: 'Request [layer][flags][count][id0][id1]...; Response
        [0x51][0x05][ack=1]. flags & 0x01 = clear_board: if set, deactivate_layer() (board) +
        disable_command() (board) BEFORE applying — replace mode. If clear_board not set: stack
        mode — board state untouched. Then: set_host_layer(layer) (0xFF clears host layer) +
        apply_host_callbacks(ids, count).'"
  critical: "This is the canonical behavior paragraph. Mirror its wording + ordering exactly:
        clear_board (deactivate+disable) → set_host_layer → apply_host_callbacks → respond."

- file: plan/003_16d737de7a3e/architecture/external_deps.md
  section: "### Field definitions (§4.6)"
  why: "Confirms the byte-by-byte args layout this handler parses: layer / flags(clear_board bit 0)
        / count / ids (full desired enabled set)."
  critical: "ids is the FULL desired set (not a delta) — apply_host_callbacks diffs it. count==0
        means 'disable all' (handled natively by apply_host_callbacks Phase 1)."

# Dependency PRPs — what exists when this task starts (CONTRACTS)
- file: plan/003_16d737de7a3e/P1M2T1S2/PRP.md
  why: "P1.M2.T1.S2 (COMPLETE) produced static void set_host_layer(uint8_t layer) at notifier.c:252.
        Its contract: 0xFF (LAYER_UNSET) clears (layer_off old if set); else layer_off(old if set)
        + layer_on(new); touches ONLY host_layer. This task CALLS it; it does not modify it."
  critical: "Treat set_host_layer as a black box: 'static void set_host_layer(uint8_t layer);'.
        Pass the wire layer byte directly (0xFF clears). Do NOT re-implement layer_on/off here."

- file: plan/003_16d737de7a3e/P1M2T1S3/PRP.md
  why: "P1.M2.T1.S3 (COMPLETE) produced static void apply_host_callbacks(const uint8_t *ids,
        uint8_t count) at notifier.c:283. Its contract: two-phase disable-before-enable diff
        against host_cb_enabled[]; count==0 disables all; RISK-3 range-checks ids (skips id >=
        cb_size). This task CALLS it; it does not modify it."
  critical: "Treat apply_host_callbacks as a black box: 'static void apply_host_callbacks(const
        uint8_t *ids, uint8_t count);'. Pass ids=(uint8_t*)&data[5] and the (clamped) count. Do
        NOT re-implement the diff here — and do NOT pre-filter ids; RISK-3 inside apply_host_callbacks
        already skips garbage ids (id >= cb_size), which is what makes the defensive clamp safe."

- file: plan/003_16d737de7a3e/P1M2T2S1/PRP.md
  why: "P1.M2.T2.S1 (COMPLETE) produced send_typed_response(cmd_id, payload, payload_len) +
        handle_typed_command(char *data) + QUERY_INFO + QUERY_CALLBACK + the default placeholder.
        Its contract: switch on (uint8_t)data[1]; each case builds a payload, calls
        send_typed_response, then break; the post-switch return true; is the only exit. S2/S3
        insert their case branches BEFORE default."
  critical: "QUERY_CALLBACK / SET_OS are the structural template: read (uint8_t)data[2..], build
        a uint8_t payload[], send_typed_response(<this cmd>, payload, N), break. INSERT the
        APPLY_HOST_CONTEXT case BEFORE default (C switches are top-to-bottom — appending after
        default would make it dead code). The default-case comment (notifier.c:~701) currently
        says '...and 0x05 until P1.M2.T2.S3 lands' — this task drops the '0x05' clause."

- file: plan/003_16d737de7a3e/P1M2T2S2/PRP.md   (preceding sibling — landed)
  why: "P1.M2.T2.S2 produced the SET_OS (0x03) case immediately before default. The live
        notifier.c already carries it (VERIFIED: case at notifier.c:693). So this task inserts
        APPLY_HOST_CONTEXT between the SET_OS case and default."
  critical: "Do NOT modify the SET_OS case. Its insertion convention (case before default; break
        not return; payload {0x01}; comment-only default update) is exactly what this task mirrors
        for 0x05. After this task, default catches only 0x04 (reserved) + unknown ids."

# The live implementation (the source of truth the implementer edits)
- file: notifier.c
  section: "static bool handle_typed_command(char *data)" (~lines 645-706)
  why: "The exact switch to modify. SET_OS (case ~693) is the immediate-preceding sibling; the
        default comment (~701) is the anchor + the comment to tweak; the function header comment
        (~640) is the second comment to update."
  pattern: "case NOTIFY_CMD_SET_OS: { uint8_t os_byte = (uint8_t)data[2]; ...; uint8_t payload[1]
        = { 0x01 }; send_typed_response(NOTIFY_CMD_SET_OS, payload, 1); break; }"
  gotcha: "INSERT before default, not after — otherwise 0x05 still hits default and the case is
        dead. C switch cases are evaluated top-to-bottom."

- file: notifier.c
  section: "deactivate_layer() / disable_command() / set_host_layer() / apply_host_callbacks()"
  why: "The four functions the handler calls (all already present, all reachable from
        handle_typed_command in the same TU). deactivate_layer/disable_command are NON-static
        (extern) — the board teardown. set_host_layer/apply_host_callbacks are STATIC — the host
        state machines."
  critical: "clear_board order: deactivate_layer() THEN disable_command() (PRD §4.6/§14 list
        deactivate before disable). They touch independent board state so the order is functionally
        irrelevant, but match the contract. (This DIFFERS from process_full_message/apply_os_change,
        which do disable-then-deactivate — different context; APPLY_HOST_CONTEXT's clear_board
        contract is explicit.)"

- file: notifier.h
  section: "#define NOTIFY_CMD_APPLY_HOST_CONTEXT 0x05" (~line 53)
  why: "The constant this case switches on. Already #defined — do not re-define. There is NO
        named constant for the clear_board flag bit; §4.6 defines it as 'flags bit 0'."
  critical: "Use the literal (flags & 0x01) with an inline comment '/* bit 0 = clear_board (§4.6) */'
        — do NOT add a NOTIFY_FLAG_CLEAR_BOARD #define (no header change; matches S2's literal-0x01
        ack style). NOTIFY_CMD_APPLY_HOST_CONTEXT is the only new symbol referenced, and it is LANDED."

# The build/test gate
- file: run_notifier_stub_tests.sh
  why: "Object-compiles notifier.c (-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=... -Iqmk_stubs -I.),
        links test_notifier_dispatch + test_notifier_os, runs both, asserts 0 FAIL. After this
        task the notifier.c object has ZERO warnings (the 2 carried ones retire)."
  critical: "This task does NOT touch the runner. The official test_notifier_host.c (with
        APPLY_HOST_CONTEXT stack/replace cases) is P1.M3.T1.S3 (Planned) — NOT this task. This
        task's Level-2 gate uses a throwaway /tmp multi-TU capture harness."

# External (informational)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "(N/A for this task — matcher-context only.) The §4.6/§14 wire design is self-contained in
        the PRD + architecture docs; no external doc is needed for this ~22-line handler."
```

### Current Codebase tree (run `ls` at repo root)

```bash
notifier.c             # P1.M1..P1.M2.T2.S2 COMPLETE. Contains, in order:
                       #   globals (current_os, activated_layer, host_layer, host_cb_enabled,
                       #            has_been_queried, typed_mode) +
                       #   select_*_map_os() + board_rules_present() +
                       #   layer/command state machines + set_host_layer() [P1.M2.T1.S2] +
                       #   apply_host_callbacks() [P1.M2.T1.S3] + match_pattern + process_full_message +
                       #   apply_os_change() [P1.M1.T2.S2] + notifier_set_os() +
                       #   send_typed_response() [P1.M2.T2.S1] +
                       #   handle_typed_command() [P1.M2.T2.S1+S2 — QUERY_INFO+QUERY_CALLBACK+SET_OS+default;
                       #                            THIS task adds the APPLY_HOST_CONTEXT (0x05) case
                       #                            before default; updates default + header comments] +
                       #   hid_notify() [P1.M2.T1.S1 — typed routing fork]
notifier.h             # P1.M1.T1.S1 COMPLETE. NOTIFY_CMD_APPLY_HOST_CONTEXT=0x05 already #defined.
pattern_match.{c,h}    # Untouched (matcher is the single source of truth for semantics).
qmk_stubs/             # os_detection.h (os_variant_t), qmk_stubs.c (layer_on/off, raw_hid_send,
                       #   stub_get_active_layer), print.h, raw_hid.h, qmk_keyboard_stub.h.
                       #   stub_get_last_response does NOT exist yet (P1.M3.T1.S1).
test_notifier_*.c      # test_notifier_dispatch.c, test_notifier_os.c (exist). test_notifier_host.c
                       #   is P1.M3.T1.S2/S3 (Planned) — NOT this task.
run_notifier_stub_tests.sh  # Builds notifier.o (stub) + links the 2 existing test drivers.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
notifier.c             # THIS task MODIFIES handle_typed_command only:
                       #   - INSERT case NOTIFY_CMD_APPLY_HOST_CONTEXT (0x05) before default
                       #     (parses layer/flags/count/ids; clamps count; clear_board→deactivate+
                       #      disable; set_host_layer; apply_host_callbacks; reply [0x51][0x05][0x01])
                       #   - UPDATE default's comment (drop "0x05"; logic unchanged)
                       #   - UPDATE handle_typed_command header comment (all four handlers listed)
# No new files. No new constants. No new functions. No new globals. No #includes.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — INSERT BEFORE default, not after. C switches evaluate cases top-to-bottom; the
//   default: at the END currently catches 0x04 (reserved) and 0x05 (APPLY_HOST_CONTEXT). If you
//   append the APPLY_HOST_CONTEXT case AFTER default, 0x05 still hits default and your case is
//   dead code. Place `case NOTIFY_CMD_APPLY_HOST_CONTEXT:` BETWEEN `case NOTIFY_CMD_SET_OS:`
//   and `default:`.

// CRITICAL — the defensive count CLAMP is mandatory (item contract: "count exceeds available
//   bytes → clamp or skip callbacks defensively"). handle_typed_command gets `char *data` (= the
//   reassembled msg_buffer) but NOT a length, and the typed path does NOT NUL-terminate msg_buffer.
//   Worse, a typed payload CAN contain a 0x00 byte (callback id 0 is valid), so strlen(data) is
//   UNRELIABLE for finding the real message end (it would stop at an id==0 mid-list). A garbled
//   count (e.g. 0xFF=255) would read data[5+i] past the message; with count=255, data[5+254]=
//   data[259] is OUT OF BOUNDS past msg_buffer[255] (UB / crash). FIX: clamp count to
//   (uint8_t)(MSG_BUFFER_SIZE - 5) = 251 so every ids[i] read stays in msg_buffer[0..255]. Stale
//   garbage ids are then filtered by apply_host_callbacks's RISK-3 (id >= cb_size skipped) — inert.

// CRITICAL — clear_board is flag bit 0, independent of count. The item contract's "If count > 0
//   and data length is sufficient" is loose phrasing; the architecture doc + §14 are explicit:
//   clear_board is `flags & 0x01`, set_host_layer is governed by `layer` (0xFF clears), and
//   apply_host_callbacks handles count==0 natively (disable-all). So the handler runs
//   clear_board-if-flagged → set_host_layer → apply_host_callbacks → respond UNCONDITIONALLY;
//   count==0 is the coherent "disable all callbacks" path, NOT a reason to skip set_host_layer.

// CRITICAL — clear_board order: deactivate_layer() THEN disable_command(). PRD §4.6 AND §14 both
//   list deactivate before disable ("deactivate_layer()s its board activated_layer and
//   disable_command()s its board command, ... BEFORE applying"). They touch independent board
//   state so the order is functionally irrelevant, but contract fidelity picks deactivate-first.
//   (Note: process_full_message + apply_os_change do disable-THEN-deactivate — that is a DIFFERENT
//   context; do NOT "harmonize" APPLY_HOST_CONTEXT to match them. Match the clear_board contract.)

// CRITICAL — clear_board runs BEFORE set_host_layer/apply_host_callbacks. The whole point of
//   replace mode (§14) is the board is inert for the window; tearing it down after applying host
//   state would let the board briefly coexist with the new host state. Order: clear_board →
//   set_host_layer → apply_host_callbacks.

// GOTCHA — ids is (uint8_t*)&data[5]. data is `char *` (msg_buffer is char[]); casting char* to
//   uint8_t* and reading is safe (both are char-types; no strict-aliasing violation). apply_host_callbacks
//   takes `const uint8_t *ids` — the cast matches its signature. Do NOT copy ids into a separate
//   array — pass the pointer into the reassembled buffer directly (the architecture doc shows
//   'ids = (uint8_t*)&data[5] (pointer into the reassembled buffer)').

// GOTCHA — the ack byte is 0x01, NOT 0x05 or true. §4.6: APPLY_HOST_CONTEXT response payload is
//   [ack] where "1=applied". The payload is a single byte 0x01. (cmd_id 0x05 is echoed by
//   send_typed_response in response[1]; the ack is response[2].) Full wire response: [0x51][0x05]
//   [0x01] then zero-padding to 32 bytes (send_typed_response does the pad via {0}-init).

// GOTCHA — do NOT duplicate F9 / state logic. set_host_layer owns host_layer; apply_host_callbacks
//   owns host_cb_enabled[]; deactivate_layer owns activated_layer; disable_command owns
//   current_command. The handler is a thin orchestrator: it CALLS these four and builds the
//   response. No direct `host_layer =`, `host_cb_enabled[] =`, `activated_layer =`, or
//   `current_command =` writes belong in this case.

// GOTCHA — do NOT touch host state outside set_host_layer/apply_host_callbacks, and do NOT touch
//   board state outside the clear_board branch. SET_OS is the only other typed handler that
//   touches board state (via current_os); APPLY_HOST_CONTEXT touches board state ONLY via
//   clear_board. Orthogonality (§14, invariant 21).

// GOTCHA — reading data[2..4] without a per-byte length check is SAFE and INTENTIONAL, matching
//   the established (non-validating) style of QUERY_CALLBACK (reads data[2]) and SET_OS (reads
//   data[2]). For a well-formed APPLY_HOST_CONTEXT the host always sends [F0][05][layer][flags]
//   [count]...; a malformed/truncated command leaves stale buffer bytes there (benign: layer/flags
//   /count resolve to benign values, and the variable-length ids tail is covered by the count
//   clamp). Do NOT add per-byte bounds checks on data[2..4] — they diverge from the sibling cases
//   and prevent nothing.

// GOTCHA — handle_typed_command already returns true AFTER the switch (notifier.c:~706). Do NOT
//   add `return true;` inside the APPLY_HOST_CONTEXT case — use `break;` so the single post-switch
//   `return true;` is the only return path (matches QUERY_INFO/QUERY_CALLBACK/SET_OS).

// GOTCHA — no #include changes. set_host_layer, apply_host_callbacks, deactivate_layer,
//   disable_command, send_typed_response, NOTIFY_CMD_APPLY_HOST_CONTEXT, MSG_BUFFER_SIZE are all
//   already in scope in notifier.c. Adding an #include is scope creep.

// GOTCHA — the default-case comment must be UPDATED, not left stale. notifier.c:~701 currently
//   reads "Default / unknown cmd_id (incl 0x04 reserved for VIA-coexist, and 0x05 until P1.M2.T2.S3
//   lands): ...". After this task 0x05 is handled, so drop the "and 0x05 until P1.M2.T2.S3 lands"
//   clause. The default LOGIC (send_typed_response(cmd_id, NULL, 0); break;) is UNCHANGED — it
//   remains the safe placeholder for 0x04 (reserved) and truly-unknown ids.

// GOTCHA — the handle_typed_command HEADER comment is stale (claims SET_OS/APPLY_HOST_CONTEXT
//   "land in P1.M2.T2.S2/S3"). After this task all four handlers are implemented; update it to
//   list QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT as implemented, leaving only reserved
//   0x04 + unknown ids in default. (SET_OS is ALREADY in the file — the header comment was not
//   updated when S2 landed; this task fixes both at once.)
```

## Implementation Blueprint

### Data models and structure

No new data models. This task consumes the existing types/symbols:

```c
/* Already in scope (notifier.c + notifier.h): */
static void set_host_layer(uint8_t layer);                       /* notifier.c:252 (P1.M2.T1.S2) */
static void apply_host_callbacks(const uint8_t *ids, uint8_t count);  /* notifier.c:283 (P1.M2.T1.S3) */
void        deactivate_layer(void);                              /* extern (board) */
void        disable_command(void);                               /* extern (board) */
static void send_typed_response(uint8_t cmd_id,
                                const uint8_t *payload, uint8_t payload_len);   /* notifier.c (P1.M2.T2.S1) */
#define NOTIFY_CMD_APPLY_HOST_CONTEXT 0x05                       /* notifier.h */
#define MSG_BUFFER_SIZE 256                                       /* notifier.c */
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — insert the APPLY_HOST_CONTEXT case in handle_typed_command
  - PLACE: inside `static bool handle_typed_command(char *data)`, in the
    `switch (cmd_id)` block. INSERT the new case BETWEEN the existing
    `case NOTIFY_CMD_SET_OS: { ... }` block and the `default: { ... }` block.
  - TEMPLATE to mirror: the SET_OS case (notifier.c:693) — parse wire bytes, build a
    uint8_t payload[], call send_typed_response(<this cmd>, payload, N), break.
  - ITEM (the exact block to insert, with its Mode-A comment):
        /* APPLY_HOST_CONTEXT (0x05) — per-window stack/replace via the clear_board
         * flag (§14). clear_board=1 (flags bit 0): deactivate the board layer +
         * disable the board command BEFORE applying (replace mode — board rules
         * are inert for this window). clear_board=0 (stack): board state is left
         * untouched. Then set_host_layer(layer) — 0xFF (LAYER_UNSET) clears the
         * host layer — and apply_host_callbacks(ids, count) diffs the host
         * callback enable set (disable-before-enable; count==0 disables all).
         * The board and host state planes are otherwise independent (§14,
         * invariant 21). Response: [0x51][0x05][ack=1]. */
        case NOTIFY_CMD_APPLY_HOST_CONTEXT: {
            uint8_t layer = (uint8_t)data[2];
            uint8_t flags = (uint8_t)data[3];
            uint8_t count = (uint8_t)data[4];
            /* ids[] is the variable-length tail starting at data[5]. Clamp count to
             * the buffer bound so a malformed/garbled count (e.g. 0xFF) never reads
             * past msg_buffer (MSG_BUFFER_SIZE). The reassembled tail may carry
             * stale bytes from a prior message (the typed path does not
             * NUL-terminate), but apply_host_callbacks skips any id >=
             * get_host_callbacks_size() (RISK-3), so garbage ids are inert. */
            uint8_t max_ids = (uint8_t)(MSG_BUFFER_SIZE - 5);
            if (count > max_ids) {
                count = max_ids;
            }
            uint8_t *ids = (uint8_t *)&data[5];

            if (flags & 0x01) {                 /* bit 0 = clear_board (§4.6): replace */
                deactivate_layer();             /* board: turn off activated_layer   */
                disable_command();              /* board: turn off current_command    */
            }
            set_host_layer(layer);              /* host: 0xFF (LAYER_UNSET) clears host_layer */
            apply_host_callbacks(ids, count);   /* host: disable-before-enable diff            */

            uint8_t payload[1] = { 0x01 };      /* ack = 1 (applied) */
            send_typed_response(NOTIFY_CMD_APPLY_HOST_CONTEXT, payload, 1);
            break;
        }
  - NAMING: layer/flags/count/ids/max_ids (locals, snake_case); payload (matches
    sibling cases); cmd echo via NOTIFY_CMD_APPLY_HOST_CONTEXT (the existing constant).
  - DEPENDENCIES: set_host_layer (P1.M2.T1.S2); apply_host_callbacks (P1.M2.T1.S3);
    deactivate_layer/disable_command (board, extern); send_typed_response (P1.M2.T2.S1);
    NOTIFY_CMD_APPLY_HOST_CONTEXT (notifier.h); MSG_BUFFER_SIZE (notifier.c). All present.
  - DO NOT: add a `return true;` (use break); add direct host_layer=/host_cb_enabled[]=
    /activated_layer=/current_command= writes; pre-filter ids; add per-byte bounds checks
    on data[2..4]; use strlen/NUL-scan; reorder clear_board after apply; add #includes;
    add a NOTIFY_FLAG_CLEAR_BOARD #define.

Task 2: MODIFY notifier.c — update the default-case comment (drop the stale "0x05")
  - PLACE: the comment immediately above `default: {` in the same switch (~notifier.c:701).
  - CHANGE: "Default / unknown cmd_id (incl 0x04 reserved for VIA-coexist, and 0x05
             until P1.M2.T2.S3 lands): reply with just [0x51][cmd_id] (no payload) ..."
         -> "Default / unknown cmd_id (incl 0x04 reserved for VIA-coexist): reply with
             just [0x51][cmd_id] (no payload) ..."
  - PRESERVE: the default-case BODY (send_typed_response(cmd_id, NULL, 0); break;) is
    UNCHANGED — it remains the safe placeholder for 0x04 (reserved) + unknown ids.
  - WHY: keep the comment honest post-S3 (0x05 is now handled).

Task 3: MODIFY notifier.c — update the handle_typed_command HEADER comment (all four)
  - PLACE: the block comment directly above `static bool handle_typed_command(char *data)`
    (~notifier.c:640), specifically the trailing lines:
       " * QUERY_INFO (0x01) and QUERY_CALLBACK (0x02) are implemented here. SET_OS (0x03)
        * and APPLY_HOST_CONTEXT (0x05) land in P1.M2.T2.S2/S3; until then they fall through
        * to the default ([0x51][cmd_id] no payload) — a safe placeholder. "
  - CHANGE to:
       " * QUERY_INFO (0x01), QUERY_CALLBACK (0x02), SET_OS (0x03), and APPLY_HOST_CONTEXT
        * (0x05) are implemented here. The reserved 0x04 (VIA-coexist) and any unknown
        * id fall through to the default ([0x51][cmd_id] no payload) — a safe placeholder. "
  - WHY: SET_OS is ALREADY implemented (S2 landed) and APPLY_HOST_CONTEXT lands here; the
    old comment claiming both "land in S2/S3" is doubly stale.

Task 4: VERIFY the build + behavior gates (run the Validation Loop, Levels 1-4).
```

### Implementation Patterns & Key Details

```c
// PATTERN: thin orchestrator. The handler does NOT implement state logic — it CALLS the four
//   state machines (deactivate_layer, disable_command, set_host_layer, apply_host_callbacks) and
//   builds the response. Each owns its global:
//     deactivate_layer()  -> activated_layer      (board)
//     disable_command()   -> current_command      (board)
//     set_host_layer()    -> host_layer           (host)
//     apply_host_callbacks() -> host_cb_enabled[] (host)
//   No direct global write belongs in this case.

// PATTERN: mirror the sibling case structure. SET_OS is the template: read (uint8_t)data[2..];
//   build a stack uint8_t payload[]; call send_typed_response with the case's own cmd constant;
//   break; (the post-switch `return true;` covers it). Copy that shape; the body differs.

// PATTERN: stack-allocated fixed-size payload. `uint8_t payload[1] = {0x01};` — no malloc, no
//   dynamic size. send_typed_response copies it into the 32-byte response and zero-pads. Matches
//   SET_OS (`uint8_t payload[1] = { 0x01 }`).

// PATTERN: clear_board is the explicit board/host bridge (invariant 21). It is the ONE typed
//   action that touches board state. deactivate+disable FIRST, then apply host state, so the
//   board is inert before the host layer/callbacks land.

// ANTI-PATTERN: do NOT append the case after default. INSERT before default or 0x05 hits the
//   placeholder and the case is dead code.

// ANTI-PATTERN: do NOT skip the count clamp. Without it a garbled count (0xFF) reads past
//   msg_buffer (OOB). Clamp to MSG_BUFFER_SIZE-5. (And do NOT use strlen — typed payloads can
//   contain 0x00.)

// ANTI-PATTERN: do NOT gate clear_board / set_host_layer on count > 0. clear_board is a flag bit;
//   set_host_layer is governed by layer; apply_host_callbacks handles count==0 natively. The
//   handler runs unconditionally; count==0 is the "disable all" path.

// ANTI-PATTERN: do NOT reorder: clear_board (deactivate+disable) MUST precede set_host_layer +
//   apply_host_callbacks. And within clear_board, deactivate_layer() precedes disable_command()
//   (PRD §4.6/§14 contract).

// ANTI-PATTERN: do NOT duplicate state logic. No host_layer=/host_cb_enabled[]=/activated_layer=/
//   current_command= direct writes; no re-implementation of layer_on/off or the callback diff.

// ANTI-PATTERN: do NOT add `return true;` inside the case. Use `break;` (the single post-switch
//   return is the only exit, matching QUERY_INFO/QUERY_CALLBACK/SET_OS).

// ANTI-PATTERN: do NOT add a NOTIFY_FLAG_CLEAR_BOARD #define. Use the literal (flags & 0x01) with
//   an inline comment; no header change (matches S2's literal-0x01 ack style).

// ANTI-PATTERN: do NOT pre-filter ids before apply_host_callbacks. RISK-3 inside
//   apply_host_callbacks already skips id >= cb_size (and id >= HOST_CALLBACK_MAX). Pre-filtering
//   would duplicate that logic and diverge.

// ANTI-PATTERN: do NOT touch SET_OS / QUERY_INFO / QUERY_CALLBACK / send_typed_response /
//   set_host_layer / apply_host_callbacks / deactivate_layer / disable_command / hid_notify /
//   typed_mode. The ONLY edits are the APPLY_HOST_CONTEXT case insertion + the two comment updates.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - MODIFY notifier.c ONLY, inside handle_typed_command's switch:
      * INSERT case NOTIFY_CMD_APPLY_HOST_CONTEXT (0x05) between NOTIFY_CMD_SET_OS and default.
      * UPDATE the default-case comment (drop "0x05"; logic unchanged).
      * UPDATE the handle_typed_command header comment (all four handlers listed).
  - No new files; no edits to notifier.h, pattern_match.*, qmk_stubs/*, test_*,
    run_notifier_stub_tests.sh, PRD.md, tasks.json, prd_snapshot.md, .gitignore.

CONSUMERS (this task's output):
  - The desktop host (qmk_notifier crate) reads [0x51][0x05][ack] to confirm APPLY_HOST_CONTEXT
    applied; it then relies on the firmware having the host layer + host callbacks active (and,
    for replace, the board inert).

SEAM REUSE (the core integration):
  - clear_board -> deactivate_layer() + disable_command() (board teardown, extern).
  - APPLY_HOST_CONTEXT -> set_host_layer() (host layer tracker) + apply_host_callbacks() (host
    callback diff). Both were 'defined but not used' until now; this task retires both warnings.

DOWNSTREAM HAND-OFFS (NOT this task):
  - P1.M3.T1.S1: adds stub_get_last_response() to qmk_stubs.c (my Level-2 /tmp harness uses its
    OWN local raw_hid_send capture, NOT this accessor — no dependency).
  - P1.M3.T1.S3: adds the committed APPLY_HOST_CONTEXT (stack/replace) test cases to
    test_notifier_host.c.

BUILD:
  - No build-system change. Validate by stub-compiling notifier.c (Level 1), a /tmp multi-TU
    harness (Level 2), and the existing runner (Level 3).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; one switch-case insertion + two comment edits).
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc` with the QMK stub harness.
> `handle_typed_command`, `set_host_layer`, `apply_host_callbacks`, `deactivate_layer`,
> `disable_command`, and the file-scope host globals are NOT exposed via any stub accessor,
> and `stub_get_last_response` does not exist yet (P1.M3.T1.S1). So the APPLY_HOST_CONTEXT
> case is validated by a `/tmp` **multi-TU** harness (the S1 PRP's idiom for the static-vs-weak
> tension): object-compile `notifier.c`, link a separate test TU that defines a STRONG
> `DEFINE_HOST_CALLBACKS` (overriding the weak accessors) + a capturing `raw_hid_send` + a
> recording `layer_on`/`layer_off`, and drive the PUBLIC `hid_notify`. The harness skeleton was
> **compiled and run against a `/tmp` copy of `notifier.c` carrying the insertion during
> research** (ALL CASES CONFIRMED, 0 failures, ZERO compile warnings) — proven feasible.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c exactly as run_notifier_stub_tests.sh does.
#     CRITICAL: expect ZERO warnings. The two carried warnings (set_host_layer,
#     apply_host_callbacks) RETIRE this task (APPLY_HOST_CONTEXT now calls both).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_s3.o
echo "compile exit=$?  (expect 0)"
echo "-- warnings (expect NONE) --"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_s3.o 2>&1 | grep 'warning:' | sed 's/^[^:]*:notifier.c://'
# Expected: NO output (zero warnings). FAIL if: exit != 0, OR any warning appears.

# 1b. Confirm the APPLY_HOST_CONTEXT case is present, before default, after SET_OS.
grep -nE 'case NOTIFY_CMD_APPLY_HOST_CONTEXT:' notifier.c
# Expected: exactly ONE line, whose line number is BETWEEN NOTIFY_CMD_SET_OS and `default:`.

# 1c. Confirm the handler body (parse, clamp, clear_board, apply, reply, break).
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'uint8_t layer = \(uint8_t\)data\[2\]' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'uint8_t flags = \(uint8_t\)data\[3\]' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'uint8_t count = \(uint8_t\)data\[4\]' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'MSG_BUFFER_SIZE - 5' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'flags & 0x01' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'deactivate_layer\(\)' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'disable_command\(\)' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'set_host_layer\(layer\)' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'apply_host_callbacks\(ids, count\)' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'payload\[1\] = \{ 0x01 \}' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'send_typed_response\(NOTIFY_CMD_APPLY_HOST_CONTEXT, payload, 1\)' \
  && echo "APPLY_HOST_CONTEXT body correct (ok)" || echo "ERROR: APPLY_HOST_CONTEXT body wrong"

# 1d. Confirm NO duplicated state logic leaked into the case (no direct global writes, no strlen,
#      no `return true` inside the case).
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'host_layer =|host_cb_enabled\[|activated_layer =|current_command =|strlen\(data\)|return true;' \
  && echo "ERROR: duplicated state logic / strlen / return-true in APPLY_HOST_CONTEXT case" \
  || echo "no duplicated logic in APPLY_HOST_CONTEXT case (good)"

# 1e. Confirm the default-case comment dropped "0x05" AND its logic is unchanged.
grep -nE 'Default / unknown cmd_id' notifier.c | grep -q '0x04 reserved for VIA-coexist' \
  && grep -nE 'Default / unknown cmd_id' notifier.c | grep -qv '0x05 until' \
  && echo "default comment honest post-S3 (ok)" || echo "WARN: default comment not updated"
awk '/default: \{/{f=1} f&&/break;/{print; exit} f' notifier.c | grep -q 'send_typed_response(cmd_id, NULL, 0)' \
  && echo "default logic unchanged (ok)" || echo "ERROR: default logic changed"

# 1f. Confirm the header comment lists all four handlers (no stale "land in S2/S3").
sed -n '/static bool handle_typed_command/,/switch (cmd_id)/p' notifier.c | grep -q 'APPLY_HOST_CONTEXT' \
  && sed -n '/static bool handle_typed_command/,/switch (cmd_id)/p' notifier.c | grep -qv 'land in P1.M2.T2.S2/S3' \
  && echo "header comment updated (ok)" || echo "WARN: header comment still stale"

rm -f /tmp/notifier_s3.o
```

### Level 2: Component Tests (THE PRIMARY BEHAVIORAL GATE)

This `/tmp` **multi-TU** harness object-compiles `notifier.c`, then links a separate
test TU that provides a STRONG `DEFINE_HOST_CALLBACKS` (overriding the weak accessors),
a capturing `raw_hid_send` (full 32-byte response), a recording `layer_on`/`layer_off`,
and drives the PUBLIC `hid_notify`. It was **compiled and run against a `/tmp` copy of
notifier.c with the insertion during research** (ALL CASES CONFIRMED). Create it, run it,
require all-pass.

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. Object-compile the (now-modified) notifier.c.
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_s3.o

# 2b. The behavior harness TU.
cat > /tmp/s3_test.c <<'EOF'
/* Multi-TU: links notifier_s3.o (weak get_host_callbacks) + this TU's STRONG
 * DEFINE_HOST_CALLBACKS override. Drives the PUBLIC hid_notify end-to-end. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"
void hid_notify(uint8_t *data, uint8_t length);
extern void activate_layer(uint8_t layer);
extern void deactivate_layer(void);
extern void enable_command(command_map_t *command);
extern void disable_command(void);

static uint8_t g_resp[32]; static int g_sends = 0;
void raw_hid_send(uint8_t *data, uint8_t length){ (void)length; memcpy(g_resp,data,32); g_sends++; }

static char llog[64][24]; static int llen = 0;
#define LR(fmt,...) do{ if(llen<64) snprintf(llog[llen++],24,fmt,##__VA_ARGS__); }while(0)
static uint8_t g_active = 255;
void layer_on(uint8_t l) { g_active=l;  LR("on(%u)",l); }
void layer_off(uint8_t l){ g_active=255; LR("off(%u)",l); }

static char clog[64][16]; static int clen = 0;
#define CR(fmt,...) do{ if(clen<64) snprintf(clog[clen++],16,fmt,##__VA_ARGS__); }while(0)
static void en0(void){CR("en0");} static void di0(void){CR("di0");}
static void en1(void){CR("en1");} static void di1(void){CR("di1");}
static void b_en(void){CR("en_cmd");} static void b_di(void){CR("di_cmd");}
DEFINE_HOST_CALLBACKS({ {"c0",en0,di0}, {"c1",en1,di1} });

static int fails = 0;
#define CK(cond) do{ if(cond) printf("ok   %s\n",#cond); else { printf("FAIL %s\n",#cond); fails++; } }while(0)

/* [0x81][9F][F0][05][layer][flags][count][ids..][ETX] */
static void mk(uint8_t *rep, uint8_t layer, uint8_t flags, uint8_t count, const uint8_t *ids){
    memset(rep,0,32); rep[0]=0x81; rep[1]=0x9F; rep[2]=0xF0; rep[3]=0x05;
    rep[4]=layer; rep[5]=flags; rep[6]=count;
    for(uint8_t i=0;i<count && i<20;i++) rep[7+i]=ids[i];
    rep[7+count]=0x03;
}
/* reset host state (clear host layer + disable all callbacks) then clear the logs. */
static void reset_host(uint8_t *rep){ uint8_t z[1]={0}; mk(rep,0xFF,0x00,0,z); hid_notify(rep,32); llen=clen=0; }

int main(void){
    uint8_t rep[32], ids[8];

    /* 1. Response shape: [0x51][0x05][0x01] + zero pad, exactly 1 send. */
    { reset_host(rep); ids[0]=0; mk(rep,224,0x00,1,ids); g_sends=0; hid_notify(rep,32);
      int padok=1; for(int i=3;i<32;i++) if(g_resp[i]!=0){padok=0;break;}
      CK(g_sends==1 && g_resp[0]==0x51 && g_resp[1]==0x05 && g_resp[2]==0x01 && padok); }

    /* 2. STACK (clear_board=0): board untouched; host layer + cb applied. */
    { reset_host(rep); g_active=255;
      activate_layer(5);
      command_map_t cmd = {"x", NULL, NULL, false}; enable_command(&cmd);
      llen=clen=0;
      ids[0]=0; mk(rep,224,0x00,1,ids); hid_notify(rep,32);
      CK(clen==1 && !strcmp(clog[0],"en0"));
      CK(llen==1 && !strcmp(llog[0],"on(224)"));        /* host layer on; NO board off */
      llen=0; deactivate_layer();                        /* board teardown still fires -> untouched */
      CK(llen==1 && !strcmp(llog[0],"off(5)")); }

    /* 3. REPLACE (clear_board=1): board cleared BEFORE host layer+cb. */
    { reset_host(rep); g_active=255;
      activate_layer(5);
      command_map_t cmd = {"x", NULL, NULL, false}; enable_command(&cmd);
      llen=clen=0;
      ids[0]=1; mk(rep,230,0x01,1,ids); hid_notify(rep,32);
      CK(llen==2 && !strcmp(llog[0],"off(5)") && !strcmp(llog[1],"on(230)")); /* board off BEFORE host on */
      CK(clen==1 && !strcmp(clog[0],"en1"));
      llen=0; deactivate_layer();                        /* board already cleared -> NO-OP */
      CK(llen==0); }

    /* 3b. REPLACE with recording board command: di_cmd BEFORE host en0 (ordering). */
    { reset_host(rep); g_active=255;
      activate_layer(7);
      command_map_t cmd = {"x", b_en, b_di, false}; enable_command(&cmd);
      llen=clen=0;
      ids[0]=0; mk(rep,224,0x01,1,ids); hid_notify(rep,32);
      CK(clen==2 && !strcmp(clog[0],"di_cmd") && !strcmp(clog[1],"en0")); }

    /* 4. Host layer clear (0xFF). */
    { reset_host(rep); g_active=255;
      ids[0]=0; mk(rep,224,0x00,1,ids); hid_notify(rep,32); CK(g_active==224);
      mk(rep,0xFF,0x00,0,ids); hid_notify(rep,32);       CK(g_active==255); }

    /* 5. count==0 disables all currently-enabled host callbacks. */
    { reset_host(rep);
      ids[0]=0; ids[1]=1; mk(rep,0xFF,0x00,2,ids); hid_notify(rep,32);
      clen=0; mk(rep,0xFF,0x00,0,ids); hid_notify(rep,32);
      CK(clen==2 && !strcmp(clog[0],"di0") && !strcmp(clog[1],"di1")); }

    /* 6. Diff {0}->{1}: disable-before-enable => di0 BEFORE en1. */
    { reset_host(rep);
      ids[0]=0; mk(rep,0xFF,0x00,1,ids); hid_notify(rep,32);
      clen=0; ids[0]=1; mk(rep,0xFF,0x00,1,ids); hid_notify(rep,32);
      CK(clen==2 && !strcmp(clog[0],"di0") && !strcmp(clog[1],"en1"));
      clen=0; mk(rep,0xFF,0x00,0,ids); hid_notify(rep,32); }

    /* 7. Defensive clamp: count=0xFF, message ends right after count byte. No crash/OOB. */
    { reset_host(rep);
      memset(rep,0,32); rep[0]=0x81;rep[1]=0x9F;rep[2]=0xF0;rep[3]=0x05;
      rep[4]=0xFF; rep[5]=0x00; rep[6]=0xFF; rep[7]=0x03;
      hid_notify(rep,32);
      CK(1); }   /* reached => no crash; garbage ids inert (RISK-3) */

    /* 8. Always replies [0x51][0x05][0x01] even with count=0. */
    { reset_host(rep); g_sends=0; mk(rep,0xFF,0x00,0,ids); hid_notify(rep,32);
      CK(g_sends==1 && g_resp[0]==0x51 && g_resp[1]==0x05 && g_resp[2]==0x01); }

    printf("\n%s (%d failures)\n", fails?"SOME FAILURES":"ALL CASES CONFIRMED", fails);
    return fails?1:0;
}
EOF

# 2c. Compile the harness + link the notifier object. Expect exit 0 (the only permitted
#     compiler output is the harmless -Wunused set from the harness TU itself, filtered).
gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/s3_test.c /tmp/notifier_s3.o -o /tmp/s3_test 2>/tmp/s3_build.log
echo "harness compile exit=$?"
grep -iE 'error' /tmp/s3_build.log | head   # expect none
/tmp/s3_test; echo "behavior exit=$?  (expect 0 / ALL CASES CONFIRMED)"
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
# CRITICAL gates: case 1 (response [0x51][0x05][0x01]); case 2 (stack: board untouched);
# case 3 (replace: board off BEFORE host on); case 7 (clamp: no crash).

rm -f /tmp/s3_test.c /tmp/s3_test /tmp/s3_build.log /tmp/notifier_s3.o
```

### Level 3: Integration Testing (Regression — the existing runner)

```bash
cd /home/dustin/projects/qmk-notifier

# The existing stub-compile gate. APPLY_HOST_CONTEXT touches handle_typed_command (typed path),
# which the legacy dispatch/os tests do NOT exercise (they never send 0xF0) — so they must stay
# byte-for-byte green. This proves no regression in the reassembler, F4 delimiter matcher, F5
# ordering, F8 multi-OS selection, F9 OS-clear, or the hid_notify routing fork.
./run_notifier_stub_tests.sh
# Expected: builds notifier.o + test_notifier_dispatch + test_notifier_os, runs both,
# prints "notifier dispatch fails=0 (exit=0)" and "notifier os fails=0 (exit=0)",
# then "✓ notifier stub-compile gate PASSED", exit 0.

# DO NOT expect test_notifier_host.c to exist or run — it is P1.M3.T1.S2/S3 (Planned).
# The APPLY_HOST_CONTEXT behavior is fully covered by the Level-2 /tmp harness until that lands.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Doc-contract check (item-spec §5 DOCS, Mode A): the APPLY_HOST_CONTEXT case carries a
#     comment that names §14, the stack/replace semantics, clear_board=1 (deactivate+disable
#     BEFORE applying), clear_board=0 (board untouched), set_host_layer, apply_host_callbacks,
#     and the response layout.
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c | grep -qE '§14|stack/replace' \
  && echo "§14 / stack-replace reference present (ok)" || echo "WARN: §14 reference missing"
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c | grep -qiE 'clear_board=1|deactivate.*board.*BEFORE|replace' \
  && echo "clear_board=replace documented (ok)" || echo "WARN: replace semantics missing"
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c | grep -qiE 'clear_board=0|board state.*untouch|stack' \
  && echo "clear_board=stack documented (ok)" || echo "WARN: stack semantics missing"

# 4b. Scope-creep guard: the case must NOT duplicate state logic or touch state outside its seams.
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'host_layer =|host_cb_enabled\[|activated_layer =|current_command =' \
  && echo "ERROR: direct state write in APPLY_HOST_CONTEXT (scope creep)" || echo "no direct state writes (good)"
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'layer_on\(|layer_off\(' \
  && echo "ERROR: handler calls layer_on/off directly (should go via set_host_layer/deactivate)" \
  || echo "no direct layer_on/off (good)"

# 4c. clear_board ordering: deactivate_layer + disable_command textually precede set_host_layer +
#     apply_host_callbacks within the case body.
awk '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/{f=1} f&&/^        }/{exit} f' notifier.c \
  | awk '/deactivate_layer/{d=NR} /disable_command/{x=NR} /set_host_layer/{s=NR} /apply_host_callbacks/{a=NR}
         END{ if(d>0 && x>0 && s>0 && a>0 && d<s && x<s) print "clear_board BEFORE apply (ok)"; else print "FAIL: ordering wrong" }'

# 4d. Defensive clamp present: the case references MSG_BUFFER_SIZE - 5 and clamps count.
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c | grep -qE 'MSG_BUFFER_SIZE - 5' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c | grep -qE 'count = max_ids|if \(count > max_ids\)' \
  && echo "defensive count clamp present (ok)" || echo "WARN: count clamp missing"

# 4e. Wire-contract check: the response echoes cmd_id 0x05 with ack 0x01.
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c | grep -qE 'send_typed_response\(NOTIFY_CMD_APPLY_HOST_CONTEXT, payload, 1\)' \
  && sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c | grep -qE 'payload\[1\] = \{ 0x01 \}' \
  && echo "wire response [0x51][0x05][0x01] constructed (ok)" || echo "ERROR: response construction wrong"

# 4f. Orthogonality: APPLY_HOST_CONTEXT touches board state ONLY via the clear_board branch
#     (deactivate_layer/disable_command). set_host_layer/apply_host_callbacks are host-only seams.
#     Confirm no other board-state touch in the case (no process_full_message, no enable_command,
#     no activate_layer, no apply_os_change).
sed -n '/case NOTIFY_CMD_APPLY_HOST_CONTEXT:/,/^        }/p' notifier.c \
  | grep -qE 'process_full_message|enable_command|activate_layer|apply_os_change|notifier_set_os' \
  && echo "ERROR: unexpected board-state touch in APPLY_HOST_CONTEXT" || echo "board touch limited to clear_board seams (good)"

# 4g. Diff hygiene: ONLY notifier.c changed (plus plan/ PRP/research).
git status --porcelain | grep -vE '^\?\? plan/'
# Expected: ` M notifier.c` only. NOTHING else.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: stub-compile `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c`
      → exit 0, **ZERO warnings** (the set_host_layer + apply_host_callbacks warnings retire).
- [ ] Level 1: `case NOTIFY_CMD_APPLY_HOST_CONTEXT:` present exactly once, between
      `NOTIFY_CMD_SET_OS` and `default:` in `handle_typed_command`.
- [ ] Level 2: `/tmp/s3_test` prints "ALL CASES CONFIRMED (0 failures)", including
      **response `[0x51][0x05][0x01]`** (case 1), **stack leaves board untouched** (case 2),
      **replace clears board BEFORE applying** (case 3), and **defensive clamp no-crash** (case 7).
- [ ] Level 3: `./run_notifier_stub_tests.sh` → dispatch fails=0, os fails=0, exit 0 (no regression).

### Feature Validation

- [ ] clear_board=1 (replace): `deactivate_layer()` + `disable_command()` fire BEFORE
      `set_host_layer()` + `apply_host_callbacks()`.
- [ ] clear_board=0 (stack): board `activated_layer` + `current_command` are untouched; host
      layer + callbacks still apply.
- [ ] `layer=0xFF` clears the host layer (set_host_layer LAYER_UNSET path).
- [ ] `count==0` disables all currently-enabled host callbacks (apply_host_callbacks Phase 1).
- [ ] A garbled `count` (e.g. 0xFF) is clamped to `MSG_BUFFER_SIZE - 5`; no crash, no OOB read;
      garbage ids are inert (RISK-3 inside apply_host_callbacks).
- [ ] Response is exactly `[0x51][0x05][0x01]` + zero-pad, sent exactly once per command.
- [ ] Board and host state planes are otherwise independent (orthogonality, invariant 21) —
      APPLY_HOST_CONTEXT touches board state ONLY via the clear_board branch.

### Code Quality Validation

- [ ] Mirrors the `SET_OS` case structure (parse wire bytes, build payload, send, break).
- [ ] Thin orchestrator — no direct `host_layer=`/`host_cb_enabled[]=`/`activated_layer=`/
      `current_command=` writes; calls the four state-machine seams.
- [ ] INSERT ONLY into `handle_typed_command` + the two comment edits; nothing else in
      `notifier.c` is touched.
- [ ] No new files, constants, functions, globals, or `#include`s.
- [ ] Anti-patterns avoided (no append-after-default, no strlen, no `return true` inside the
      case, no count-gating of clear_board/set_host_layer, no NOTIFY_FLAG_CLEAR_BOARD #define,
      no pre-filtering of ids).

### Documentation & Deployment

- [ ] Mode-A comment on the APPLY_HOST_CONTEXT case names §14, stack/replace, clear_board=1
      (deactivate+disable BEFORE applying), clear_board=0 (board untouched), set_host_layer +
      apply_host_callbacks, and the `[0x51][0x05][ack=1]` response.
- [ ] `default:` comment updated to drop the stale "0x05" (only 0x04 reserved remains).
- [ ] `handle_typed_command` header comment updated (all four handlers listed; no stale
      "land in S2/S3").
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't append the case after `default:` — INSERT before `default:` or 0x05 still hits the
  placeholder. Place it between `NOTIFY_CMD_SET_OS` and `default:`.
- ❌ Don't skip the `count` clamp — a garbled count (0xFF) would read past `msg_buffer` (OOB).
  Clamp to `MSG_BUFFER_SIZE - 5`. (And don't use `strlen` — typed payloads can contain 0x00.)
- ❌ Don't gate `clear_board` / `set_host_layer` on `count > 0` — clear_board is a flag bit,
  set_host_layer is governed by `layer`, and `apply_host_callbacks` handles `count==0` natively
  (disable-all). The handler runs unconditionally.
- ❌ Don't reorder — clear_board (deactivate+disable) MUST precede set_host_layer +
  apply_host_callbacks; and within clear_board, `deactivate_layer()` precedes `disable_command()`
  (PRD §4.6/§14). Do NOT "harmonize" with process_full_message's disable-then-deactivate.
- ❌ Don't duplicate state logic — no direct `host_layer=`/`host_cb_enabled[]=`/`activated_layer=`/
  `current_command=` writes; no `layer_on`/`layer_off` calls (go via set_host_layer/
  deactivate_layer). The handler is a thin orchestrator.
- ❌ Don't add `return true;` inside the case — use `break;` (the single post-switch `return true;`
  is the only exit, matching QUERY_INFO/QUERY_CALLBACK/SET_OS).
- ❌ Don't pre-filter `ids` before `apply_host_callbacks` — RISK-3 inside it already skips
  `id >= cb_size`; pre-filtering duplicates that logic and diverges.
- ❌ Don't add a `NOTIFY_FLAG_CLEAR_BOARD` `#define` — use the literal `(flags & 0x01)` with an
  inline comment; no header change (matches S2's literal-0x01 ack style).
- ❌ Don't add per-byte bounds checks on `data[2..4]` — the established (non-validating) style of
  QUERY_CALLBACK/SET_OS reads `data[2]` directly; the variable-length ids tail is the only part
  that needs the defensive clamp.
- ❌ Don't add `#include`s — `set_host_layer`, `apply_host_callbacks`, `deactivate_layer`,
  `disable_command`, `send_typed_response`, `NOTIFY_CMD_APPLY_HOST_CONTEXT`, `MSG_BUFFER_SIZE`
  are all already in scope.
- ❌ Don't leave the `default:` comment or the `handle_typed_command` header comment stale —
  update both (drop "0x05" from default; list all four handlers in the header).
- ❌ Don't touch `set_host_layer`, `apply_host_callbacks`, `deactivate_layer`, `disable_command`,
  `send_typed_response`, the SET_OS/QUERY_INFO/QUERY_CALLBACK cases, `hid_notify`, `typed_mode`,
  `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_*.c`, `run_*.sh`, `PRD.md`,
  `tasks.json`, `prd_snapshot.md`, `rules.mk`, or `.gitignore`. Only the APPLY_HOST_CONTEXT
  case insertion + the two comment edits in `notifier.c`.
- ❌ Don't write the official `test_notifier_host.c` or the `stub_get_last_response` accessor
  here — those are P1.M3.T1.S1/S3. This task's validation uses a throwaway `/tmp` multi-TU
  capture harness.

---

## Confidence Score: 10/10

The handler is a ~22-line insertion + two comment edits into a switch whose structure, call
targets, constants, and types are **all already present in the current `notifier.c`** (verified
by direct read): `handle_typed_command` (static) with `QUERY_CALLBACK`/`SET_OS` as the exact
structural template and `default:` as the insertion anchor; `set_host_layer` (static,
P1.M2.T1.S2 COMPLETE) + `apply_host_callbacks` (static, P1.M2.T1.S3 COMPLETE) as the host state
machines to drive; `deactivate_layer` (extern) + `disable_command` (extern) as the board teardown;
`send_typed_response` (static, P1.M2.T2.S1) as the response builder; `NOTIFY_CMD_APPLY_HOST_CONTEXT`
(notifier.h) + `MSG_BUFFER_SIZE` (notifier.c) as the constants. The msg_buffer byte layout
(`data[2]=layer, data[3]=flags, data[4]=count, data[5..]=ids`) is confirmed against PRD §4.6 +
external_deps.md "Field definitions". The **single non-obvious requirement** — the defensive
`count` clamp (because `handle_typed_command` gets no length and the typed path doesn't
NUL-terminate, and typed payloads can contain 0x00) — is resolved and explained. The **Level-2
multi-TU validation harness was compiled and run against a `/tmp` copy of `notifier.c` carrying
the insertion during research**: stub-compile → **exit 0, ZERO warnings** (the carried
`set_host_layer` + `apply_host_callbacks` warnings retire); behavior harness → **ALL CASES
CONFIRMED (0 failures)** across 8 scenarios (response shape, stack board-untouched, replace
board-cleared-before-apply, board-on_disable-before-host-on_enable ordering, layer-clear,
count0-disable-all, diff ordering, defensive clamp no-crash); regression baseline → **dispatch 0
fail, os 31/31, gate PASSED**. Dependencies on P1.M2.T1.S2 (`set_host_layer` — CONTRACT, called
not modified), P1.M2.T1.S3 (`apply_host_callbacks` — CONTRACT, called not modified), and
P1.M2.T2.S1 (the switch skeleton + `send_typed_response` — CONTRACT, inserted into not rewritten)
are explicit, and the hand-off to P1.M3.T1.S3 (official APPLY_HOST_CONTEXT test cases) and
P1.M3.T1.S1 (`stub_get_last_response`, not depended on) is documented. No external dependencies
are needed (libc + the in-repo stubs only).