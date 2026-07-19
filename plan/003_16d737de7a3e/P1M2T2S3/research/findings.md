# Research Findings — P1.M2.T2.S3 (APPLY_HOST_CONTEXT handler)

## Task

Add the `APPLY_HOST_CONTEXT` (cmd_id `0x05`) case to `handle_typed_command`'s
switch in `notifier.c`. It honors the per-window `clear_board` flag (replace vs
stack), then applies the host layer (`set_host_layer`) + host callbacks
(`apply_host_callbacks`), and replies `[0x51][0x05][ack=1]`.

## Codebase state verified by direct read (notifier.c, current HEAD)

- `handle_typed_command(char *data)` (static) switches on `(uint8_t)data[1]`.
  Current cases: `QUERY_INFO` (658), `QUERY_CALLBACK` (672), `SET_OS` (693),
  `default` (703). **The SET_OS case is already present** (the parallel P1.M2.T2.S2
  work is reflected in the live file), so the insertion point is unambiguous:
  BETWEEN the `SET_OS` case's closing `}` and the `default:` comment.
- msg_buffer layout seen by `handle_typed_command` (magic header stripped by
  `hid_notify` via `data += 2; length -= 2;`): `data[0]=0xF0 discriminator,
  data[1]=cmd_id, data[2..]=args`. For APPLY_HOST_CONTEXT:
  `data[2]=layer, data[3]=flags, data[4]=count, data[5..5+count-1]=ids`.
  **VERIFIED** against the wire frame in external_deps.md §"Field definitions".
- Dependencies all LANDED & reachable from inside `handle_typed_command`:
  - `set_host_layer(uint8_t)` (static, notifier.c:252) — 0xFF clears; else layer_off(old)+layer_on(new).
  - `apply_host_callbacks(const uint8_t *ids, uint8_t count)` (static, notifier.c:283) — two-phase disable-before-enable diff; count==0 disables all.
  - `deactivate_layer()` (non-static, extern) — board: `if(activated_layer==LAYER_UNSET)return; layer_off(activated_layer); activated_layer=LAYER_UNSET;`
  - `disable_command()` (non-static, extern) — board: fires `current_command->on_disable` then `current_command=NULL`.
  - `send_typed_response(cmd_id, payload, len)` (static) — builds `[0x51][cmd_id][payload capped at 30][zero-pad]`.
- `MSG_BUFFER_SIZE` (256) #defined in notifier.c — the buffer bound for the count clamp.
- `NOTIFY_CMD_APPLY_HOST_CONTEXT` (0x05) is #defined in notifier.h. There is NO
  named constant for the `clear_board` flag bit; §4.6 defines it as "flags bit 0".
  (Use the literal `0x01` with an inline `/* bit 0 = clear_board (§4.6) */` comment,
  matching the S2 SET_OS handler's literal-`0x01`-ack style. No header change.)

## Compile-warning baseline (gcc -Wall -Wextra, stub flags)

- BEFORE this task: exactly TWO `-Wunused-function` warnings:
  `apply_host_callbacks` (283) + `set_host_layer` (252). All other previously-carried
  symbols (`board_rules_present`, `has_been_queried`, `host_layer`, `host_cb_enabled`)
  are already USED (QUERY_INFO / set_host_layer / apply_host_callbacks landed them).
- AFTER this task (VERIFIED on a /tmp copy with the insertion): **ZERO warnings**.
  APPLY_HOST_CONTEXT calls both `set_host_layer` and `apply_host_callbacks`,
  retiring the last two carried warnings. This is the clean signal.

## Defensive count-clamp (the one non-trivial design decision)

- `handle_typed_command` receives `char *data` (= msg_buffer) but NOT a length.
  The typed path does NOT NUL-terminate msg_buffer (no sanitize), and a typed
  payload CAN contain a 0x00 byte (callback id 0 is valid), so `strlen(data)` is
  UNRELIABLE for finding the real message end.
- A malformed/garbled `count` (e.g. 0xFF=255) would make `data[5 + i]` read past the
  message into stale buffer bytes; with count=255, `data[5+254]=data[259]` is an
  OUT-OF-BOUNDS read past msg_buffer[255] (UB / potential crash).
- Resolution: **clamp count to `MSG_BUFFER_SIZE - 5` (= 251)** so every `ids[i]`
  read stays within msg_buffer[0..255]. Stale-but-in-bounds garbage ids are then
  filtered by `apply_host_callbacks`'s RISK-3 guards (it skips any `id >=
  get_host_callbacks_size()`), so they are inert. This satisfies the item contract's
  "count exceeds available bytes → clamp or skip callbacks defensively".

## Logic / ordering decisions (resolved against §14 + architecture doc)

- **clear_board governs board teardown, NOT count.** The item contract's "If count > 0
  and data length is sufficient" is loose phrasing; the architecture doc + §14 make
  clear clear_board is a flag bit (independent of count), set_host_layer is governed
  by `layer` (0xFF clears), and apply_host_callbacks handles count==0 natively
  (disable-all). So the handler runs: clear_board-if-flagged → set_host_layer →
  apply_host_callbacks → respond, UNCONDITIONALLY (count==0 is the "disable all"
  path, fully coherent).
- **clear_board order (BEFORE applying):** `deactivate_layer()` then
  `disable_command()`. PRD §4.6 + §14 BOTH list deactivate_layer() before
  disable_command() ("deactivate_layer()s its board activated_layer and
  disable_command()s its board command"). These touch independent board state, so
  the order is functionally irrelevant, but contract fidelity picks deactivate-first.
  (Note: this DIFFERS from the board's process_full_message/apply_os_change which do
  disable-then-deactivate — but that is a different context; APPLY_HOST_CONTEXT's
  clear_board contract is explicit: deactivate then disable.)
- Response payload = `{0x01}` (ack=applied); `send_typed_response(NOTIFY_CMD_APPLY_HOST_CONTEXT, payload, 1)`.

## Empirical validation (on a /tmp copy of notifier.c with the insertion)

- Stub-compile: exit 0, **ZERO warnings**.
- Multi-TU behavior harness (strong `DEFINE_HOST_CALLBACKS` override; capturing
  `raw_hid_send`; layer-call log; drives PUBLIC `hid_notify`): **ALL CASES CONFIRMED
  (0 failures)** across 8 scenarios / 14 assertions:
  1. response `[0x51][0x05][0x01]`+zero pad, 1 send;
  2. STACK clear_board=0 → board untouched (follow-up deactivate_layer still fires), host on(224)+cb0 enabled;
  3. REPLACE clear_board=1 → board off(5) BEFORE host on(230), follow-up deactivate_layer is a NO-OP;
  3b. REPLACE ordering → board di_cmd BEFORE host en0;
  4. layer=0xFF clears host layer (g_active 224→255);
  5. count==0 disables all (di0,di1);
  6. diff {0}→{1} → di0 before en1;
  7. count=0xFF clamped → no crash/OOB;
  8. always replies even count=0.
- Regression baseline (`./run_notifier_stub_tests.sh`): dispatch fails=0, os 31/31,
  "✓ notifier stub-compile gate PASSED". The legacy suites never send 0xF0, so the
  typed path is never reached → byte-identical behavior after the insertion.

## Test-harness idiom

- The official `test_notifier_host.c` (with APPLY_HOST_CONTEXT stack/replace cases)
  is P1.M3.T1.S3 (Planned) — NOT this task. This task's Level-2 gate uses a throwaway
  `/tmp` MULTI-TU harness (compile notifier.c → object; link a test TU that defines a
  strong `DEFINE_HOST_CALLBACKS` + capturing `raw_hid_send` + layer_on/off; drive the
  PUBLIC `hid_notify`). This is the S1 PRP's idiom for the static-vs-weak tension
  (`DEFINE_HOST_CALLBACKS` strong defs conflict with the weak accessors if both are
  in one TU). It does NOT touch qmk_stubs.c.

## Scope (DO NOT)

- Only `notifier.c` changes: INSERT the APPLY_HOST_CONTEXT case before `default`;
  UPDATE the default-case comment (drop the now-stale "0x05"); UPDATE the stale
  `handle_typed_command` header comment (SET_OS is already implemented; list all four).
- No header change, no new constant, no new function, no new global, no #include.
- Do NOT touch `set_host_layer`, `apply_host_callbacks`, `deactivate_layer`,
  `disable_command`, `hid_notify`, `send_typed_response`, the typed_mode fork,
  notifier.h, pattern_match.*, qmk_stubs/*, test_*, run_*.sh, PRD.md, tasks.json,
  prd_snapshot.md, rules.mk, or .gitignore.