# External Dependencies — qmk-notifier

## QMK Firmware Integration

Verified against local clone at `/home/dustin/projects/qmk_firmware` (2025 copyright dates).

### RAW_ENABLE Feature Flag
- **File:** `tmk_core/protocol/usb_descriptor_common.h`
- `RAW_USAGE_PAGE 0xFF60` and `RAW_USAGE_ID 0x61` are QMK defaults
- `RAW_ENABLE = yes` in `rules.mk` gates all Raw HID code via `#ifdef RAW_ENABLE`
- Exposes a vendor-defined HID interface (usage page 0xFF60 / usage 0x61)

### raw_hid_receive / raw_hid_send
- **Header:** `quantum/raw_hid.h` — declares both functions with "Always 32 bytes" documentation
- **Implementation:** `quantum/raw_hid.c`:
  - `raw_hid_send(data, length)` → delegates to `host_raw_hid_send(data, length)`
  - `raw_hid_receive(data, length)` is a **weak** no-op default — users override in keymap
- All three protocol drivers (LUFA, ChibiOS, V-USB) call `raw_hid_receive` with 32 bytes

### Report Size: 32 on Every Platform

| Protocol | File | Receive buffer | Send guard |
|---|---|---|---|
| LUFA (ATmega32U4) | `tmk_core/protocol/lufa/lufa.c` | `uint8_t data[RAW_EPSIZE]` = 32 | `if (length != RAW_EPSIZE) return;` |
| ChibiOS (STM32/RP2040) | `tmk_core/protocol/chibios/usb_main.c` | `uint8_t buffer[RAW_EPSIZE]` = 32 | `if (length != RAW_EPSIZE) return;` |
| V-USB (low-speed AVR) | `tmk_core/protocol/vusb/vusb.c` | `RAW_BUFFER_SIZE` = 32 | `if (length != RAW_BUFFER_SIZE) return;` |

**V-USB detail:** USB packets are 8 bytes, but V-USB reassembles into 32-byte logical
reports before calling `raw_hid_receive`. The firmware ALWAYS sees `length == 32`.
On V-USB, `RAW_EPSIZE` is locally 8 but the send guard checks `RAW_BUFFER_SIZE` (32).

> **PRD §4.4 correction:** The PRD says "QMK silently drops this ack" but
> qmk-notifier passes `RAW_REPORT_SIZE = 32` to `raw_hid_send`, which passes
> all platform guards. The ack IS sent successfully. The guard only drops
> responses with length ≠ 32. (This is a PRD documentation issue, not a code bug.)

### layer_on / layer_off
- **Header:** `quantum/action_layer.h` (under `#ifndef NO_ACTION_LAYER`)
- `void layer_on(uint8_t layer);` / `void layer_off(uint8_t layer);`
- Available transitively via `#include QMK_KEYBOARD_H` → `quantum.h` → `action_layer.h`
- When `NO_ACTION_LAYER` is defined, these become no-op macros

### QMK_KEYBOARD_H
- Defined as a compiler `-D` flag in `builddefs/build_keyboard.mk`:
  `-DQMK_KEYBOARD_H="..."` pointing to a generated keyboard header
- Every QMK keymap begins with `#include QMK_KEYBOARD_H`

### SRC += in rules.mk
- `SRC` is the primary Make variable for C source files
- `builddefs/common_rules.mk` converts all `SRC` entries to object files
- Keyboard/keymap `rules.mk` files are included via `-include` and `SRC +=` accumulates
- `SRC += qmk-notifier/notifier.c` is the standard pattern for adding a module

### __attribute__((weak)) Pattern
- Used pervasively in QMK (raw_hid_receive default, layer hooks, send functions)
- The weak definition is overridden at link time by a non-weak definition in user code
- qmk-notifier uses this for `get_command_map()`, `get_command_map_size()`,
  `get_layer_map()`, `get_layer_map_size()` — the `DEFINE_SERIAL_COMMANDS` /
  `DEFINE_SERIAL_LAYERS` macros generate non-weak overrides

## Companion Repos (External, Not in This Codebase)

### qmk_notifier (underscore) — Rust Transport Crate
- **Repo:** `dabstractor/qmk_notifier`
- Owns wire framing: magic header (0x81 0x9F), 32-byte chunking, ETX terminator
- Sends 33-byte buffers to hidapi (leading 0x00 report-ID + 32 bytes)
- Appends ETX (0x03) before framing
- 30 payload bytes per report (32 − 2)
- **Status:** Could not be directly verified (no web access). Wire protocol
  confirmed from the C firmware consumer side (notifier.c).

### QMKonnect — Rust Desktop Daemon
- **Repo:** `dabstractor/qmkonnect`
- Detects foreground window, sends `application_class\x1Dwindow_title`
- Links the `qmk_notifier` transport crate
- Never decides behavior — just sends window identity
- **Status:** Could not be directly verified. README mentions `qmk_notifier`
  and `hyprland-qmk-window-notifier` as companions but does NOT reference
  `qmkonnect` by name — possible repo rename or newer addition.

## ASCII Character Verification

| Name | Abbreviation | Hex | Decimal | Used For |
|---|---|---|---|---|
| **Group Separator** | GS | 0x1D | 29 | class\|title delimiter (GS_DELIMITER) |
| End of Text | ETX | 0x03 | 3 | message terminator (ETX_TERMINATOR) |
| Unit Separator | US | 0x1F | 31 | NOT USED — but notifier.h comment wrongly says "ASCII 31 (Unit Separator)" |

> **Known drift:** `notifier.h:22` says `// ASCII 31 (Unit Separator)` but 0x1D
> = decimal 29 = Group Separator. The byte value 0x1D is correct and authoritative;
> only the comment is wrong. PRD §5.3 documents this explicitly.
