# Firmware Wire Contract (Canonical)

**Source of truth**: `dabstractor/qmk-notifier/PRD.md` §4.6 (local clone:
`/home/dustin/projects/qmk-notifier/PRD.md`).

> Where this crate's PRD and the firmware PRD disagree, **the firmware wins**.

## Typed-Command Framing

All typed commands share the `0x81 0x9F` magic header namespace. The discriminator
is the **first payload byte** (byte[3] of the 33-byte hidapi buffer):

```
[0x00][0x81][0x9F][0xF0][cmd_id][ args… ][0x03]
  ^      ^     ^    ^       ^       ^       ^
  |      |     |    |       |       |       ETX terminator (appended by this crate)
  |      |     |    |       |       command-specific args
  |      |     |    |       command ID byte
  |      |     |    discriminator: 0xF0 = typed command
  |      |     magic header byte 2
  |      magic header byte 1
  hidapi report-ID leading byte (0x00 for single-report interface)
```

`0xF0` can never begin a real matched string (the firmware sanitizer allows only
`0x20–0x7E`), so legacy firmware safely walks typed bytes as a no-match string.

### Multi-report chunking

Identical to legacy strings: 30 payload bytes per report (`PAYLOAD_PER_REPORT`).
`APPLY_HOST_CONTEXT` may span multiple reports when the callback-id list exceeds
30 payload bytes. The callback-id list is uncapped.

## Command Table

| `cmd_id` | Name | Request args | Response payload (after `[0x51][cmd_echo]`) |
|---|---|---|---|
| `0x01` | `QUERY_INFO` | none | `[proto_ver][feature_flags][callback_count][board_rules_present]` |
| `0x02` | `QUERY_CALLBACK` | `[index]` | `[index][name bytes, NUL-padded]` (name absent ⇒ `[index][0x00]`) |
| `0x03` | `SET_OS` | `[os_byte]` | `[ack]` (`1`=applied) |
| `0x04` | *(reserved — VIA-coexist)* | — | — |
| `0x05` | `APPLY_HOST_CONTEXT` | `[layer][flags][count][id0][id1]…` | `[ack]` (`1`=applied) |

## Field Definitions

### QUERY_INFO response `[0x51][0x01][proto_ver][feature_flags][callback_count][board_rules_present]`

| Byte offset (from start of 32-byte reply) | Field | Type | Values |
|---|---|---|---|
| `[0]` | response marker | u8 | `0x51` (typed reply) |
| `[1]` | cmd echo | u8 | `0x01` (QUERY_INFO) |
| `[2]` | `proto_ver` | u8 | `1` = legacy string-only firmware; `2` = typed-command capable |
| `[3]` | `feature_flags` | u8 bitmask | `0x01` = APPLY_HOST_CONTEXT supported; `0x02` = callback registry present; `0x04` = (reserved) VIA-coexist |
| `[4]` | `callback_count` | u8 | Number of entries in firmware's host-callback registry (0 if none) |
| `[5]` | `board_rules_present` | u8 (0/1) | `1` iff any board map (default or any OS-specific) is non-empty |

### QUERY_CALLBACK response `[0x51][0x02][index][name bytes, NUL-padded]`

| Byte offset | Field | Type | Values |
|---|---|---|---|
| `[0]` | response marker | u8 | `0x51` |
| `[1]` | cmd echo | u8 | `0x02` (QUERY_CALLBACK) |
| `[2]` | index | u8 | Echo of requested index |
| `[3…]` | name | ASCII bytes, NUL-padded | Name string. If callback has no name or index is out of range: `[3]=0x00` (NUL immediately) |

### SET_OS request `[0xF0][0x03][os_byte][0x03]`

`os_byte` values (mirrors QMK `os_variant_t`):

| Value | OS |
|---|---|
| `0` | UNSURE |
| `1` | LINUX |
| `2` | WINDOWS |
| `3` | MACOS |
| `4` | IOS |

SET_OS response: `[0x51][0x03][ack]` where `ack = 1` means applied.

### APPLY_HOST_CONTEXT request `[0xF0][0x05][layer][flags][count][id0][id1]…`

| Field | Offset (payload) | Type | Values |
|---|---|---|---|
| `layer` | `[0]` (after `[0xF0][0x05]`) | u8 | Host-layer number (`≥ 224` by convention), or `0xFF` (255) to clear host layer |
| `flags` | `[1]` | u8 bitmask | Bit 0 = `clear_board`: when set, firmware clears board layer/command before applying host context |
| `count` | `[2]` | u8 | Number of callback IDs following |
| `id…` | `[3…]` | u8 each | Full desired enabled callback-id set; firmware diffs against current set (disable-before-enable) |

APPLY_HOST_CONTEXT response: `[0x51][0x05][ack]` where `ack = 1` means applied.

## Reply Disambiguation

| `response[0]` | Interpretation |
|---|---|
| `0x51` | Typed reply — decode by `response[1]` (cmd echo) |
| `0` | Legacy match-bool: no match |
| `1` | Legacy match-bool: matched |
| *(no reply within timeout)* | Timeout — device is legacy/offline; caller stays in string-only mode |
| *(any other value)* | Treated as non-capable device → Timeout semantics |

## Constants (from firmware notifier.h)

```c
#define NOTIFY_CMD_DISCRIMINATOR      0xF0
#define NOTIFY_RESPONSE_MARKER        0x51
#define NOTIFY_CMD_QUERY_INFO         0x01
#define NOTIFY_CMD_QUERY_CALLBACK     0x02
#define NOTIFY_CMD_SET_OS             0x03
#define NOTIFY_CMD_APPLY_HOST_CONTEXT 0x05
#define NOTIFY_PROTO_VER              2      // this firmware is typed-capable
#define NOTIFY_FEATURE_APPLY_HOST_CONTEXT 0x01
#define NOTIFY_FEATURE_CALLBACK_REGISTRY  0x02
#define NOTIFY_FEATURE_VIA_COEXIST        0x04  // reserved
#define HOST_CALLBACK_MAX              32
#define HOST_LAYER_BASE                224
```

## Firmware Implementation Status

**Implemented.** The firmware `notifier.c` now implements the §4.6 typed-command
namespace. `hid_notify()` routes the first report's `data[2] == 0xF0` into a
length-aware typed-reassembly path; at ETX, `handle_typed_command()` dispatches the
reassembled command (QUERY_INFO / QUERY_CALLBACK / SET_OS / APPLY_HOST_CONTEXT) and
emits the `[0x51][cmd_echo][payload]` typed reply. The legacy 0/1 acknowledgement
now runs under an `if (!typed_dispatched)` guard, so a typed message suppresses the
legacy ack on its ETX report:
```c
if (!typed_dispatched) {
    uint8_t response[RAW_REPORT_SIZE] = {0};
    response[0] = match;  // 0 or 1  (legacy path only; typed replies are sent inside handle_typed_command)
    raw_hid_send(response, RAW_REPORT_SIZE);  // RAW_REPORT_SIZE = 32
}
```

This was confirmed via **live hardware testing** against a real QMK keyboard
(Dactyl-Manuform, VID 0xFEED / PID 0x0000) running qmk-notifier firmware,
cross-checked against the `notifier.c::hid_notify` source. The firmware's reply
model is **per-report**: `hid_notify()` is invoked once per 32-byte report and sends
a 32-byte reply at the end of every call — so an N-report message produces N replies,
where only the LAST (ETX-report) reply carries the real result (typed `0x51…` or the
legacy match-bool), and intermediate reports reply with a legacy `0`. This crate's
reply capture must retain the ETX-report reply (see the bugfix PRD §Issue 1).