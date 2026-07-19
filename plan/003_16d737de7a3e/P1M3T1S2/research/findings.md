# Research findings — P1.M3.T1.S2 (test_notifier_host.c: QUERY_INFO + QUERY_CALLBACK)

## Source files read
- `notifier.c` — full read. Typed handlers `handle_typed_command` (~lines 622-720),
  `send_typed_response` (~628-637), `hid_notify` typed fork (~830-900), host state
  globals (`has_been_queried`, `host_layer`, `host_cb_enabled[]`).
- `notifier.h` — full read. `DEFINE_HOST_CALLBACKS` macro, `DEFINE_SERIAL_*` macros,
  `NOTIFY_*` constants, `host_callback_t` struct.
- `qmk_stubs/qmk_stubs.c` — current state (g_active_layer, layer_on/off, raw_hid_send,
  stub_get_active_layer). S1 will add g_last_response + stub_get_last_response.
- `test_notifier_dispatch.c` — the file-scope DEFINE_* + ck() + PASS/FAIL pattern.
- `test_notifier_os.c` — the manual-extern convention (line 37: `stub_get_active_layer`),
  the `CK(cond,name)` macro, board-state setup via process_full_message.
- `run_notifier_stub_tests.sh` — build chain: `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.`,
  compiles notifier.c (with -Wall -Wextra -std=c99), links test + qmk_stubs.c (with -Wall -std=c99),
  greps `grep -c '^FAIL:'`.
- `architecture/host_rules_architecture.md` §Test strategy — test_notifier_host.c follows
  EXACTLY the test_notifier_dispatch.c pattern.
- `architecture/findings_and_risks.md` F6 — stub observables precedent (stub_get_last_response).
- Predecessor PRP `plan/003_16d737de7a3e/P1M3T1S1/PRP.md` — CONTRACT for stub_get_last_response.

## Verified response byte layouts (traced from notifier.c, NOT guessed)

### QUERY_INFO (cmd_id 0x01) — handle_typed_command QUERY_INFO case
```
payload[4] = { NOTIFY_PROTO_VER(=2),
               NOTIFY_FEATURE_APPLY_HOST_CONTEXT(0x01) | (cb_size>0 ? 0x02 : 0),
               (uint8_t)cb_size,
               board_rules_present() ? 1 : 0 }
send_typed_response(0x01, payload, 4)
```
→ response via stub_get_last_response():
- `r[0] = 0x51` (NOTIFY_RESPONSE_MARKER)
- `r[1] = 0x01` (QUERY_INFO echo)
- `r[2] = 2` (proto_ver)
- `r[3] = feature_flags` — with DEFINE_HOST_CALLBACKS present (cb_size>0) → `0x01 | 0x02 = 0x03`
       (bit 0 AND bit 1 set). Matches contract "response[3] has bit 0 and bit 1 set".
- `r[4] = callback_count` (= get_host_callbacks_size(); with 2 entries → 2)
- `r[5] = board_rules_present` (= 1 if ANY board map non-empty; DEFINE_SERIAL_* present → 1)

### QUERY_CALLBACK (cmd_id 0x02) — valid index
```
payload[0] = index;
name = cbs[index].name;
n = 0; while (n<29 && name[n]) { payload[1+n] = name[n]; n++; }
send_typed_response(0x02, payload, 1+n)
```
→ response: `r[0]=0x51, r[1]=0x02, r[2]=index, r[3..3+n-1]=name bytes, r[3+n]=0 (NUL pad)`.
For "mute" (4 chars): r[2]=0, r[3]='m', r[4]='u', r[5]='t', r[6]='e', r[7]=0.
For "layout" (6 chars): r[2]=1, r[3]='l', r[4]='a', r[5]='y', r[6]='o', r[7]='u', r[8]='t', r[9]=0.

### QUERY_CALLBACK (cmd_id 0x02) — out-of-range index
```
payload[2] = { index, 0x00 };   // name absent
send_typed_response(0x02, payload, 2)
```
→ response: `r[0]=0x51, r[1]=0x02, r[2]=index, r[3]=0x00`.
For index=2 with count=2 (out of range): r[2]=2, r[3]=0x00. Matches contract (iv).

## Wire input (traced through hid_notify)

QUERY_INFO single report: `[0x81][0x9F][0xF0][0x01][0x03]`
- guard passes (0x81,0x9F); msg_index==0 && data[2]==0xF0 → typed_mode=true
- strip magic → data=[0xF0,0x01,0x03]; loop appends 0xF0→buf[0], 0x01→buf[1]; 0x03=ETX→dispatch
- handle_typed_command: cmd_id=data[1]=0x01 → QUERY_INFO. (data[2] not read for QUERY_INFO — no args.)

QUERY_CALLBACK(0): `[0x81][0x9F][0xF0][0x02][0x00][0x03]`
- strip magic → data=[0xF0,0x02,0x00,0x03]; append buf[0..2]; 0x03=ETX→dispatch
- cmd_id=data[1]=0x02, index=data[2]=0x00. ✓

## has_been_queried — NOT directly observable
`has_been_queried` is `static bool` in notifier.c, set in QUERY_INFO handler, NEVER read
by any gate in the current code (grep: only write site + declaration). Its semantic purpose
(§4.6 handshake timing) is defensive bookkeeping. The OBSERVABLE consequence is: the typed
path bypasses process_full_message → QUERY_INFO NEVER calls disable_command()/deactivate_layer()
→ board state set by a prior legacy dispatch SURVIVES QUERY_INFO (even repeated). The contract's
"(ii) verify indirectly — e.g. by checking it doesn't clear board state on a second QUERY_INFO"
maps exactly to: dispatch a board layer via process_full_message, assert stub_get_active_layer()
unchanged across QUERY_INFO #1 and #2. This is the only host-testable face of has_been_queried.

## Build chain (from run_notifier_stub_tests.sh — DO NOT modify; that's P1.M3.T2.S1)
- compile notifier.c: `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c`
- link test: `gcc -Wall -std=c99 -Iqmk_stubs -I. notifier.o qmk_stubs/qmk_stubs.c test_notifier_host.c -o test_notifier_host`
- run + count: `grep -c '^FAIL:'` must be 0; binary exits 0.
- Contract bare command (this task's OUTPUT gate, no -Wall): identical flags minus -Wall -Wextra.

## Manual-extern convention (F6)
stub_get_active_layer / stub_get_last_response are NOT in any header. Test TUs declare them
as `extern` manually (test_notifier_os.c:37 pattern). S2's TU declares BOTH:
```c
uint8_t stub_get_active_layer(void);          /* qmk_stubs.c */
const uint8_t *stub_get_last_response(void);  /* qmk_stubs.c (added by S1) */
```

## S3/S4 extensibility (this file is created once, extended later)
- S3 adds SET_OS + APPLY_HOST_CONTEXT test cases to this SAME file.
- S4 adds coexistence/backward-compat + multi-report typed framing cases.
- So DEFINE_HOST_CALLBACKS callbacks should have on_enable/on_disable handlers (S3-ready),
  not just names. Distinguishable flags (F6) let S3 assert which fired.

## Gotchas
- File-scope `static int` flags that are written-but-not-read: gcc -Wall -Wextra does NOT warn
  (only -Wunused-but-set-variable for locals). BUT to be safe AND meaningful, the (ii) test reads
  board command flags (verify no spurious on_disable) and a final assertion reads host callback
  flags (verify QUERY_* fired no host callback — side-effect-free queries).
- `(void)length` not relevant here (test code).
- memset needs <string.h> (include it, mirroring test_notifier_os.c).
- Avoid compound literals for arg arrays (use local uint8_t vars) — maximally clean under -Wextra.
- This task writes ONLY QUERY_INFO + QUERY_CALLBACK cases. SET_OS/APPLY_HOST_CONTEXT/coexistence/
  multi-report are S3/S4 — do NOT write them here.
- DO NOT modify run_notifier_stub_tests.sh (P1.M3.T2.S1) or qmk_stubs.c (S1) or notifier.* (P1.M2).