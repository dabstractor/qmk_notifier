# External Dependencies — Plan 002 (Multi-OS Delta)

## QMK `os_detection.h` — VERIFIED from qmk/qmk_firmware (the one new dependency)

**Source:** `quantum/os_detection.h` in `qmk/qmk_firmware` (read verbatim via GitHub).

### The `os_variant_t` enum (authoritative — matches PRD §5.1/§16 exactly)

```c
typedef enum {
    OS_UNSURE,    // = 0
    OS_LINUX,     // = 1
    OS_WINDOWS,   // = 2
    OS_MACOS,     // = 3
    OS_IOS,       // = 4
} os_variant_t;
```

These integer values and enumerator names are the exact tokens passed to the
`##os` preprocessor token-paste in `DEFINE_SERIAL_*_OS(os, …)`. The caller writes
the enumerator NAME (e.g. `OS_MACOS`), and the generated symbols are
`_notifier_command_map_OS_MACOS`, `_notifier_get_command_map_OS_MACOS`,
`_notifier_get_command_map_OS_MACOS_size`. The weak defaults in `notifier.c`
must use these EXACT mangled names.

### The OS-detection hook callback signatures

```c
bool process_detected_host_os_kb(os_variant_t os);
bool process_detected_host_os_user(os_variant_t os);
```

The keymap conventionally overrides `_kb` (§10.1 step 3), calls
`notifier_set_os(os)`, then returns `true`. These are weak QMK hooks (default
returns `true`); the user's keymap provides the real implementation.

### `detected_host_os()` — the function the module must NOT call

```c
os_variant_t detected_host_os(void);
```

This is a **real function** (implemented in `quantum/os_detection.c`). Calling it
requires linking against `os_detection.c` (which pulls in USB state tracking,
EEPROM, timers). **The module deliberately avoids this** — it only uses the TYPE
`os_variant_t`, so it has **zero link dependency** on the OS-detection subsystem.
The OS is **pushed in** by the keymap via `notifier_set_os()`. This is invariant
17 (§13) and the load-bearing design choice for the stub harness.

### `OS_DETECTION_ENABLE` rules.mk flag

- Enables QMK's OS-detection feature (compiles `os_detection.c`, runs the
  USB-fingerprint heuristics, calls `process_detected_host_os_kb`).
- **Multi-OS users add this; single-OS / default-only users skip it.** Without
  it, `process_detected_host_os_kb` is never called, `current_os` stays
  `OS_UNSURE`, and both tracks use the default maps — byte-identical to today.

### `usb_device_state.h` include (stub concern)

The real `os_detection.h` begins with `#include "usb_device_state.h"`. The
**stub** `os_detection.h` we create does NOT need this include — it only defines
the `os_variant_t` enum (the only thing `notifier.c` and `notifier.h` consume).
The real QMK build pulls in the genuine header transitively via
`#include QMK_KEYBOARD_H`.

## Companion repos (unchanged, documented for context)

- **qmk_notifier** (underscore, Rust) — transport crate. Sends `class\x1Dtitle` +
  ETX, 30 payload bytes/report. **Does NOT send an OS byte today** (§4.7).
- **QMKonnect** (Rust) — desktop daemon. **Does NOT send an OS byte today.**
- Host-provided OS is **HELD for next cycle** (§4.7, §14.1) — not implemented.

## No other new external dependencies

The weak-symbol pattern, `__attribute__((weak))`, `SRC +=`, `layer_on/off`,
`raw_hid_send`, the 32-byte report, the `0x81 0x9F` magic, GS `0x1D`, ETX `0x03`
— all unchanged from plan 001 and validated in its `external_deps.md`.