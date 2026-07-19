# Research Notes — P1.M2.T2.S1: typed response builder + handle_typed_command + QUERY_INFO + QUERY_CALLBACK

## What this task replaces

The `handle_typed_command` STUB at notifier.c:626 (P1.M2.T1.S1 landed it as a
linker placeholder):
```c
static bool handle_typed_command(char *buf) { (void)buf; return false; }
```
This task REPLACES that stub with: (1) a `send_typed_response` helper immediately
before it, and (2) the real `handle_typed_command` body (QUERY_INFO +
QUERY_CALLBACK + default). SET_OS (0x03) and APPLY_HOST_CONTEXT (0x05) are NOT
implemented here — they land in P1.M2.T2.S2/S3 and currently fall through to the
`default` case (safe placeholder: `[0x51][cmd_id]` no payload).

## The msg_buffer layout for a typed command (CRITICAL — drives data[1]/data[2])

`hid_notify` does, in order: coexistence guard on data[0..1] (0x81 0x9F) →
`data += 2; length -= 2` (STRIPS the magic header) → checks `data[2] == 0xF0` on
the first report (sets typed_mode) → reassembly loop appends bytes to msg_buffer
until ETX. So for a typed report `[0x81][0x9F][0xF0][cmd_id][args…][0x03]`:

```
msg_buffer = [0xF0][cmd_id][args…]   (magic header already stripped)
msg_index  = 2 + arg_len
```

Therefore inside `handle_typed_command(char *data)`:
- `data[0] == 0xF0` (discriminator — present but not switched on; the cmd is the key)
- `data[1] == cmd_id` (switch on `(uint8_t)data[1]`)
- `data[2..] == args` (QUERY_CALLBACK reads `data[2]` as the index)

msg_buffer is a static array (zero-init), and the fork guarantees msg_index >= 2
for typed commands (the 0xF0 check needs length >= 3 → at least [0xF0][cmd_id]
appended). So data[1] is always valid; data[2] reads 0 (NUL) if no arg was sent
→ QUERY_CALLBACK index 0 (safe, no crash).

## How hid_notify consumes handle_typed_command's work (CRITICAL — no double ack)

```c
if (typed_mode) {
    match = handle_typed_command(msg_buffer);   // sends the [0x51] response INSIDE
    typed_dispatched = true;                     // suppresses the legacy 0/1 ack below
}
...
if (!typed_dispatched) { /* legacy [matched(0|1)] ack */ }
```

So: **handle_typed_command sends its OWN response via send_typed_response →
raw_hid_send**, and hid_notify's `typed_dispatched` guard prevents the legacy
0/1 ack from also firing. The `bool` return is captured into `match` but is
VESTIGIAL for the typed path (the response is already sent). The contract says
return `true` ("typed path always succeeds — it sent a response"). Keep the
`static bool` signature (set by the stub) and `return true;`.

## The implementation (validated end-to-end through hid_notify)

`send_typed_response` (placed immediately before handle_typed_command):
```c
static void send_typed_response(uint8_t cmd_id, const uint8_t *payload, uint8_t payload_len) {
    uint8_t response[RAW_REPORT_SIZE] = {0};   /* zero-pads the unused tail */
    response[0] = NOTIFY_RESPONSE_MARKER;      /* 0x51 */
    response[1] = cmd_id;                      /* echo */
    if (payload != NULL && payload_len > 0) {
        uint8_t cap = (uint8_t)(RAW_REPORT_SIZE - 2);   /* 30 bytes after [0x51][cmd_id] */
        uint8_t n = (payload_len < cap) ? payload_len : cap;
        memcpy(response + 2, payload, n);
    }
    raw_hid_send(response, RAW_REPORT_SIZE);
}
```

`handle_typed_command` (replaces the stub):
```c
static bool handle_typed_command(char *data) {
    uint8_t cmd_id = (uint8_t)data[1];
    switch (cmd_id) {
        case NOTIFY_CMD_QUERY_INFO: {                 /* 0x01 */
            has_been_queried = true;                  /* §4.6 handshake */
            uint8_t payload[4];
            payload[0] = NOTIFY_PROTO_VER;            /* 2 */
            payload[1] = NOTIFY_FEATURE_APPLY_HOST_CONTEXT
                       | (get_host_callbacks_size() > 0 ? NOTIFY_FEATURE_CALLBACK_REGISTRY : 0);
            payload[2] = (uint8_t)get_host_callbacks_size();
            payload[3] = board_rules_present() ? 1 : 0;
            send_typed_response(NOTIFY_CMD_QUERY_INFO, payload, 4);
            break;
        }
        case NOTIFY_CMD_QUERY_CALLBACK: {             /* 0x02 */
            uint8_t index = (uint8_t)data[2];
            size_t cb_size = get_host_callbacks_size();
            host_callback_t *cbs = get_host_callbacks();
            if (cbs != NULL && index < cb_size && cbs[index].name != NULL) {
                uint8_t payload[30];                  /* [index] + up to 29 name bytes */
                payload[0] = index;
                const char *name = cbs[index].name;
                uint8_t n = 0;
                while (n < 29 && name[n] != '\0') { payload[1 + n] = (uint8_t)name[n]; n++; }
                send_typed_response(NOTIFY_CMD_QUERY_CALLBACK, payload, (uint8_t)(1 + n));
            } else {
                uint8_t payload[2] = { index, 0x00 };  /* name absent (§4.6) */
                send_typed_response(NOTIFY_CMD_QUERY_CALLBACK, payload, 2);
            }
            break;
        }
        default: {                                     /* unknown / 0x04 reserved / 0x03,0x05 until S2/S3 */
            send_typed_response(cmd_id, NULL, 0);
            break;
        }
    }
    return true;
}
```

## Response byte layouts (verified)

- QUERY_INFO: `[0x51][0x01][proto=2][flags][count][board_rules][zero-pad…]`.
  flags = `0x01 | (cb_size>0 ? 0x02 : 0)`. With a 2-entry registry: flags=0x03,
  count=2, board_rules=0 (no DEFINE_SERIAL_* in the test TU). Verified.
- QUERY_CALLBACK valid: `[0x51][0x02][index][name bytes][NUL-pad…]`. Name occupies
  response[3..31] (≤29 bytes). Verified "mute-discord" (12 bytes) + NUL pad at [15].
- QUERY_CALLBACK out-of-range: `[0x51][0x02][index][0x00]`. Verified index=99.
- unknown/0x04: `[0x51][cmd_id][zero-pad…]` (no payload). Verified.
- legacy string (data[2] printable): unchanged `[matched(0|1)][pad]` path
  (typed_dispatched stays false). Verified backward-compat.

## The 30-byte payload cap (response[2..31])

`response[RAW_REPORT_SIZE=32]`: [0x51][cmd_id] = 2 bytes, then 30 bytes for
payload. `send_typed_response` caps `payload_len` at `RAW_REPORT_SIZE - 2 = 30`
so a too-large payload can NEVER overflow the 32-byte report (defensive). For
QUERY_CALLBACK the name is copied up to 29 bytes (payload[1..29], payload_len =
1+n ≤ 30), fitting the cap exactly. The response is `{0}`-init so the unused
tail is zero (NUL-pad) automatically — no explicit pad loop needed.

## Warning-set transition (verified empirically)

Before this task (post P1.M2.T1.S3, the parallel task): the stub compile warns
on FOUR unused symbols: `apply_host_callbacks`, `set_host_layer`,
`board_rules_present`, `has_been_queried`.

After THIS task: `handle_typed_command` references `board_rules_present()` and
`has_been_queried` (in the QUERY_INFO case), so BOTH retire (no longer warn).
`handle_typed_command` itself does NOT warn (hid_notify calls it). The remaining
warnings are EXACTLY TWO: `apply_host_callbacks` + `set_host_layer` (both carried
→ P1.M2.T2.S2/S3, the SET_OS / APPLY_HOST_CONTEXT handlers).

Verified: `gcc -Wall -Wextra -std=c99 -c notifier_proto.c` → exactly 2 warnings
(apply_host_callbacks, set_host_layer), exit 0.

## Multi-TU test strategy (the static-vs-weak tension)

`handle_typed_command`/`send_typed_response` are `static` (file-local to
notifier.c), and the host-callback registry is provided via `DEFINE_HOST_CALLBACKS`
(a STRONG definition of get_host_callbacks/_size that overrides the weak default
in notifier.c). A single-TU `#include "notifier.c"` + `DEFINE_HOST_CALLBACKS`
FAILS with "redefinition of get_host_callbacks" (both weak and strong in the same
TU). So the test MUST be multi-TU:

1. Compile notifier.c → notifier.o (`-c`; weak accessors, static handlers inside).
2. Test TU: `DEFINE_HOST_CALLBACKS` (strong accessors) + a capturing `raw_hid_send`
   (writes the full 32-byte response to a global) + no-op `layer_on`/`layer_off`
   + `main` that builds full 32-byte typed HID reports and calls the PUBLIC
   `hid_notify`.
3. Link notifier.o + test TU → strong accessors override weak at link time
   (standard weak/strong semantics; P1.M1.T2.S1 verified "strong T beats weak W").

This drives the FULL path (hid_notify → discriminator check → reassembly →
handle_typed_command → send_typed_response → raw_hid_send capture), which is more
realistic than calling handle_typed_command directly. (NOTE: this multi-TU
capture harness is a RESEARCH artifact here — the OFFICIAL test_notifier_host.c
+ the stub_get_last_response accessor land in P1.M3.T1.S1/S2, NOT this task.
This task's Level-2 gate uses the throwaway /tmp harness.)

## Empirical validation summary (prototype, ALL CASES CONFIRMED)

Replaced the stub in a /tmp copy of notifier.c with the implementation above;
stub-compiled (0 errors, 2 carried warnings); built a multi-TU capture harness;
ran it end-to-end through hid_notify. Results (all PASS, 0 failures):

- QUERY_INFO → 1 response, [0x51][0x01], proto=2, flags=0x03, count=2,
  board_rules=0. has_been_queried flipped.
- QUERY_CALLBACK(0) → [0x51][0x02][00]"mute-discord" + NUL pad.
- QUERY_CALLBACK(1) → "open-terminal" (NULL on_disable, name present).
- QUERY_CALLBACK(99) → [0x51][0x02][63][00] (out-of-range → name absent).
- unknown 0x04 → [0x51][04] no payload.
- legacy string (data[2]='h' printable) → legacy 0/1 ack (backward-compat).
- Regression: test_notifier_dispatch 0 FAIL, test_notifier_os 0 FAIL against the
  prototype (legacy path byte-identical — handle_typed_command is never reached
  when data[2] != 0xF0).

## Scope / boundaries

- This task edits ONLY notifier.c: replaces the handle_typed_command stub (line
  626 region) with send_typed_response + the real handle_typed_command.
- It CONSUMES (LANDED, untouched): NOTIFY_* constants (notifier.h, P1.M1.T1.S1);
  board_rules_present(), has_been_queried, get_host_callbacks/_size weak
  accessors (P1.M1.T2.S1); the typed_mode fork + the stub (P1.M2.T1.S1);
  apply_host_callbacks (P1.M2.T1.S3, parallel — its body is irrelevant to these
  two handlers but lives in the same file).
- It does NOT implement SET_OS (0x03 → P1.M2.T2.S2) or APPLY_HOST_CONTEXT
  (0x05 → P1.M2.T2.S3). Those cmd_ids currently fall through to `default`
  ([0x51][cmd_id] no payload). S2/S3 will INSERT their cases before `default`.
- It does NOT touch notifier.h, pattern_match.*, qmk_stubs/*, test_notifier_*,
  run_*.sh, PRD.md, tasks.json, rules.mk, .gitignore.
- The OFFICIAL host test (test_notifier_host.c) + stub response capture
  (stub_get_last_response) land in P1.M3.T1.S1/S2 — NOT this task. This task's
  validation uses a throwaway /tmp multi-TU harness.