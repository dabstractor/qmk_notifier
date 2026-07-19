# Research Note — P1.M2.T2.S2 (SET_OS handler)

## Task
Add a `case NOTIFY_CMD_SET_OS:` (0x03) branch to `handle_typed_command` in
`notifier.c` that routes the host's `os_byte` through the shared `apply_os_change`
seam and replies `[0x51][0x03][0x01]`.

## Contracts confirmed (read directly from current source)

### `apply_os_change` (P1.M1.T2.S2 — COMPLETE, in notifier.c:581-604)
```c
static void apply_os_change(os_variant_t os) {
    if (os == current_os) return;   /* F9.3 idempotent */
    current_os = os;
    disable_command();              /* F9.1 */
    deactivate_layer();             /* F9.1 */
}
```
- `static`, sole mutation point for `current_os` (invariant 17).
- Idempotent on unchanged value → SET_OS must NOT add its own idempotency check.

### `send_typed_response` (P1.M2.T2.S1 — in notifier.c:628-641)
```c
static void send_typed_response(uint8_t cmd_id, const uint8_t *payload, uint8_t payload_len);
```
- Builds `response[0]=0x51`, `response[1]=cmd_id`, payload at `[2..]`, zero-pads,
  caps payload at 30, always sends 32 bytes via `raw_hid_send`.

### `handle_typed_command` (P1.M2.T2.S1 — in notifier.c:651-697)
- `static bool handle_typed_command(char *data)`; switches on `(uint8_t)data[1]`.
- `data[0]`=0xF0 discriminator, `data[1]`=cmd_id, `data[2..]`=args.
- Has `case NOTIFY_CMD_QUERY_INFO:` (0x01), `case NOTIFY_CMD_QUERY_CALLBACK:` (0x02),
  then `default:` (catches 0x03/0x04/0x05 → `send_typed_response(cmd_id, NULL, 0)`).
- Default comment currently says "0x03/0x05 until P1.M2.T2.S2/S3 land" → S2 must
  update it to "0x05 until P1.M2.T2.S3 lands" (remove the 0x03 mention).
- Returns `true` after the switch (SET_OS inherits this — no explicit return needed).

### Constants (notifier.h)
- `NOTIFY_CMD_SET_OS = 0x03`
- `NOTIFY_RESPONSE_MARKER = 0x51`

### `os_variant_t` (qmk_stubs/os_detection.h — mirrors QMK)
OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4. Cast uint8_t→enum
is well-defined for any byte; out-of-range values just fall to default-map
selection (select_*_map_os handles "unexpected value"). No range validation needed.

## Observability / validation constraints

- `current_os` is a file-scope global in notifier.c — NOT exposed via any stub
  accessor. Cannot be read from a driver that links `notifier.o`.
- `handle_typed_command` is `static` — unreachable from a linked driver.
- `stub_get_last_response` does NOT exist yet (planned P1.M3.T1.S1); the current
  `qmk_stubs.c` `raw_hid_send` only prints `response[0]` to stderr (no capture).
- → The SET_OS case MUST be validated via a `#include "notifier.c"` throwaway
  harness that provides its OWN local `raw_hid_send` (32-byte capture) +
  `layer_on`/`layer_off` stubs. This mirrors the S1 NFA `#include`-harness idiom.

## Feasibility VERIFIED (run against current code)
A `/tmp` harness that `#include`s `notifier.c` (compiled with
`-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.`), provides local
`raw_hid_send`+`layer_on`+`layer_off`, and calls the EXISTING `QUERY_INFO` case:
- compiles clean (only expected `-Wunused-function` dead-code warnings);
- runs and prints `ret=1 resp=[51][01][02] current_os=0 layer=255`;
- `current_os` and the captured 32-byte response are both observable.
→ The identical skeleton with a SET_OS buffer will validate S2 once the case is
added. (Captured response layout confirms: `[0x51][cmd_id][payload...]`.)

## Out of scope (hand-offs)
- APPLY_HOST_CONTEXT (0x05) → P1.M2.T2.S3 (inserts its case AFTER SET_OS, before
  default; updates default comment to drop the "0x05" mention).
- `stub_get_last_response` → P1.M3.T1.S1 (my /tmp harness uses a LOCAL capture,
  not this accessor — no dependency).
- `test_notifier_host.c` committed suite → P1.M3.T1.S2.