# External Dependencies — Plan 003 (Host-Side Rules & Typed Commands)

## QMK Firmware API surface (consumed, not modified)

All of the following are already consumed by the existing codebase and are
**unchanged** by this delta. New consumption points are noted.

### Headers
- `QMK_KEYBOARD_H` — pulled via `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'` in tests
- `os_detection.h` — header-only `os_variant_t` enum (`OS_UNSURE=0, OS_LINUX=1,
  OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4`). Module uses the TYPE only, never calls
  `detected_host_os()`. (Invariants 17, 21.)
- `raw_hid.h` — `raw_hid_send(uint8_t*, uint8_t)` declaration.
- `<string.h>` — `strlen`, `memcpy`, `strncpy`, `strcpy`.
- `print.h` — `uprintf` (only under `CONSOLE_ENABLE`).

### Functions (provided by QMK or stubs)
| Symbol | Source | Used for |
|---|---|---|
| `layer_on(uint8_t)` | QMK action_layer.c / stub | Activate a QMK layer (board AND host trackers) |
| `layer_off(uint8_t)` | QMK action_layer.c / stub | Deactivate a QMK layer (board AND host trackers) |
| `raw_hid_send(uint8_t*, uint8_t)` | QMK raw_hid.c / stub | Send the 32-byte response report |

**New consumption (this delta):** `layer_on`/`layer_off` are called by the new
`set_host_layer()` — same functions, different tracker variable (`host_layer`
instead of `activated_layer`). No new QMK symbols needed.

## Wire protocol contract (§4 — the critical contract)

### Legacy string path (UNCHANGED)
```
[0x81][0x9F][payload bytes…][0x03 ETX]
  ^magic^         ^class\x1Dtitle^     ^terminator
```
Chunked at 30 payload bytes/report (2 magic + 30 payload = 32-byte report).
Response: `[matched(0|1)][padding…]` (32 bytes).

### Typed-command path (NEW — §4.6)
```
[0x81][0x9F][0xF0][cmd_id][args…][0x03 ETX]
  ^magic^   ^disc^  ^cmd^  ^args^   ^terminator
```
- `0xF0` = discriminator byte (after magic header, = `data[2]` on the first report).
- ETX-framed, may span multiple 32-byte reports (same reassembly as legacy).
- Response: `[0x51][cmd_id_echo][payload…][padding]` (32 bytes).
- `0x51` (≥2) is distinct from legacy `0`/`1` — host disambiguates without ambiguity.

### Command table (§4.6)
| cmd_id | Name | Request args | Response payload (after `[0x51][cmd_echo]`) |
|---|---|---|---|
| `0x01` | `QUERY_INFO` | none | `[proto_ver][feature_flags][callback_count][board_rules_present]` |
| `0x02` | `QUERY_CALLBACK` | `[index]` | `[index][name bytes, NUL-padded]` (or `[index][0x00]` if absent) |
| `0x03` | `SET_OS` | `[os_byte]` | `[ack]` (1=applied) |
| `0x04` | *(reserved — VIA-coexist dispatch)* | — | — |
| `0x05` | `APPLY_HOST_CONTEXT` | `[layer][flags][count][id0][id1]…` | `[ack]` (1=applied) |

### Field definitions (§4.6)
- `proto_ver`: `1` = legacy string-only; `2` = typed-command capable. **Owned by firmware.**
- `feature_flags` bitmask: `0x01` = APPLY_HOST_CONTEXT supported; `0x02` = callback registry present; `0x04` = reserved (VIA-coexist).
- `callback_count`: `get_host_callbacks_size()` (0 if no `DEFINE_HOST_CALLBACKS`).
- `board_rules_present`: `1` iff any board map (default or OS-specific) is non-empty.
- `SET_OS.os_byte`: `0 UNSURE · 1 LINUX · 2 WINDOWS · 3 MACOS · 4 IOS` (mirrors `os_variant_t`).
- `APPLY_HOST_CONTEXT.layer`: host layer number, or `0xFF` (LAYER_UNSET) to clear.
- `APPLY_HOST_CONTEXT.flags` bit 0 = `clear_board`: clear board layer+command before applying host context.
- `APPLY_HOST_CONTEXT.id…`: full desired enabled host-callback id set; firmware diffs against current.

### OS source (§4.7)
- **Firmware heuristic** (no host): `OS_DETECTION` feature → `notifier_set_os()`.
- **Host-authoritative** (host connected): `SET_OS` typed command → same seam.
- While host has sent `SET_OS`, the host's value is authoritative for `current_os`.
- An OS **change** (from either source) clears notifier state per F9 (disable command + deactivate layer).

## Ecosystem repos (verified 2025-07-19)

| Repo | Language | Current impl | Typed-command status |
|---|---|---|---|
| `dabstractor/qmk_notifier` (crate) | Rust | v0.2.1 — legacy wire framing (magic header, 32-byte chunking, ETX, device cache) | **Spec only** in PRD.md §10; not in code |
| `dabstractor/qmkonnect` (daemon) | Rust | v0.2.8 — string-only notifier (`Notifier::notify(String)`) | **Spec only** in spec/HOST_RULES.md, spec/PROTOCOL.md §8; not in code |
| `qmk/qmk_firmware` | C | Upstream QMK | Not applicable |

**Key finding:** The typed-command namespace is fully designed across all three
repos but **not implemented in any**. This delta implements the firmware side. The
host/transport repos will implement their side separately; the firmware must be
ready to handshake with them per §4.6.

**No spec drift found:** All three repos agree that the firmware `qmk-notifier/PRD.md`
§4.6 is canonical. The design is internally consistent.

## Key constants reference (§16)

| Constant | Value | Source |
|---|---|---|
| Magic header | `0x81 0x9F` | §4.5 |
| GS delimiter | `0x1D` (29) | §4.1, `GS_DELIMITER` |
| ETX terminator | `0x03` (3) | §4.1, `ETX_TERMINATOR` |
| `RAW_REPORT_SIZE` | `32` | §8.1 |
| `MSG_BUFFER_SIZE` | `256` | §8.1 |
| `LAYER_UNSET` | `255` | §8.1 |
| Typed discriminator | `0xF0` | §4.6 |
| Typed response marker | `0x51` | §4.6 |
| `proto_ver` | `2` (this build) | §4.6 |
| Host layer block | `≥ 224` | §14, §16 |
| `NFA_MAX_PATTERN` | `128` (MCU override in notifier.c) | §7.9 |