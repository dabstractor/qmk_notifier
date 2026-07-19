# Research Notes — P1.M1.T2.S1: `qmk_stubs/os_detection.h` (os_variant_t stub)

## What this task is

A **leaf dependency**: a minimal host-test stub header that provides ONLY the
`os_variant_t` TYPE. It is consumed by `notifier.h` (which now does
`#include "os_detection.h"` — sibling task P1.M1.T1.S1, already landed in the
working tree) and transitively by `notifier.c`, under the host stub harness
(`-Iqmk_stubs`). **This file IS the mock** — no further mocking needed.

## Ground truth — real QMK header (read verbatim from GitHub)

`qmk/qmk_firmware` → `quantum/os_detection.h` (fetched during research). Its
structure:

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "usb_device_state.h"      // ← cascades into struct usb_device_state

typedef enum {
    OS_UNSURE,
    OS_LINUX,
    OS_WINDOWS,
    OS_MACOS,
    OS_IOS,
} os_variant_t;

void         process_wlength(const uint16_t w_length);
os_variant_t detected_host_os(void);
void         erase_wlength_data(void);
void         os_detection_notify_usb_device_state_change(struct usb_device_state usb_device_state);
void         os_detection_task(void);
bool         process_detected_host_os_kb(os_variant_t os);
bool         process_detected_host_os_user(os_variant_t os);
#if defined(SPLIT_KEYBOARD) && defined(SPLIT_DETECTED_OS_ENABLE)
void slave_update_detected_host_os(os_variant_t os);
#endif
#ifdef OS_DETECTION_DEBUG_ENABLE ... store_setups_in_eeprom(void); print_stored_setups(void);
#endif
```

Our stub reproduces **only the enum**. It deliberately OMITS:
- `#include "usb_device_state.h"` (architecture **F4**: cascades into undefined
  `struct usb_device_state` in the host harness).
- ALL function declarations (declaring them would force linking their impls;
  invariant 17 / §2 F8.2 says the module never calls them anyway).

## Why the enum values/names matter (the load-bearing contract)

- **Names** (`OS_LINUX` etc.) are the *preprocessor tokens* passed to `##os` in
  `DEFINE_SERIAL_*_OS(os, …)`, producing symbols
  `_notifier_get_command_map_OS_LINUX`, etc. — the EXACT names notifier.c §8.3
  weak defaults + `select_*_map_os()` reference. A renamed/wrong enumerator ⇒
  link failure in the keymap.
- **Values** (`OS_UNSURE == 0`): `current_os` boots to `OS_UNSURE` (F8.6) and the
  matcher compares `current_os == OS_UNSURE`. The integer value must match QMK so
  a real-firmware `notifier_set_os(OS_MACOS)` and host-test expectations agree.

## Verified: exact enum (matches PRD §5.1/§16 + external_deps.md)

```c
typedef enum {
    OS_UNSURE,   /* = 0 */
    OS_LINUX,    /* = 1 */
    OS_WINDOWS,  /* = 2 */
    OS_MACOS,    /* = 3 */
    OS_IOS,      /* = 4 */
} os_variant_t;
```

## Existing stub style (consistency reference)

`qmk_stubs/qmk_keyboard_stub.h`, `raw_hid.h`, `qmk_stubs.c` all follow:
`#pragma once` → includes → a `/* … */` purpose comment → content. Item spec
point 3 mandates `#pragma once` + `#include <stdint.h>` (+ the enum + header
comment). `<stdint.h>` is harmless and matches the sibling stubs + the real
header; `<stdbool.h>` is NOT needed (enum uses no `bool`).

## Ground-truth validation run DURING research (all PASS)

Created the proposed stub in a temp mirror of `qmk_stubs/` and ran:

- **[A]** `gcc -fsyntax-only -Wall -Wextra -std=c99 -x c os_detection.h` → **parse OK**.
- **[B]** C99 compile-time enum asserts (file-scope `typedef int cN[(OS_X==N)?1:-1];`)
  → **compiled (values 0..4 confirmed)**; negative control injecting a wrong value
  was **correctly REJECTED** (proves the asserts are live).
- **[C]** **Full integration gate**: `gcc … -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'
  -Iqmk_stubs -I. -c notifier.c` + link `qmk_stubs.c test_notifier_dispatch.c`
  + run → **`Total tests run: 11 / passed: 11 / failed: 0`, exit 0**.
  ⇒ The stub makes the real dispatch suite pass end-to-end.
- **[D]** Exactly ONE `#include` directive (`<stdint.h>`); NO `#include
  "usb_device_state.h"`; NO function declarations; enum typedef present.

## CRITICAL sequencing note (the one real gotcha)

The CURRENT `run_notifier_stub_tests.sh` [2/3] **link** step uses ONLY `-I.`:
```sh
gcc -Wall -std=c99 -I. "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o "$DRV"
```
After this stub lands in `qmk_stubs/`, `#include "os_detection.h"` (issued from
`notifier.h`, included by `test_notifier_dispatch.c`) is found via the [1/3]
compile step (`-Iqmk_stubs` is present there), but the [2/3] link step's
preprocessor re-includes `notifier.h` with only `-I.` and FAILS to find
`os_detection.h`. **This is NOT a defect of the stub** — it is the reason sibling
task **P1.M2.T2.S1 ("Extend run_notifier_stub_tests.sh")** exists: that task adds
`-Iqmk_stubs` to the link step. Until then, validate the stub with the
self-contained corrected-flag harness embedded in the PRP (which mirrors what the
runner WILL be), not the un-extended runner. (Mirrors the previous PRP's own
documented sequencing dependency.)

## Scope boundaries (do NOT do here)
- Do NOT modify `notifier.h` (P1.M1.T1.S1 owns it; already landed).
- Do NOT modify `notifier.c` (P1.M1.T3).
- Do NOT extend `run_notifier_stub_tests.sh` (P1.M2.T2.S1).
- Do NOT add other qmk_stubs files or touch existing ones.
- Do NOT write `test_notifier_os.c` (P1.M2.T1).

## No external dependency added
The stub is pure C (one enum, one include). No new lib, no rules.mk change, no
wire-protocol change (host-provided OS is HELD, §4.7/§14.1 — not now).