# External Dependencies — QMK Community Module Build System (§18.2 Ground Truth)

All findings verified directly from `~/projects/qmk_firmware` source.

## Community Module Generator (`lib/python/qmk/cli/generate/community_modules.py`)

The generator runs at build time and emits per-module Makefile fragments. Using
only the **leaf directory name** (e.g. `qmk_notifier`), it generates:

### 1. `SRC += $(wildcard <module_path>/<leaf>.c)`
Auto-compiles **only `<leaf>.c`**. Since `notifier.c` does NOT match the leaf
name `qmk_notifier`, it is **NOT** auto-compiled — it must be listed explicitly
in `rules.mk` via `SRC += notifier.c`.

`pattern_match.c` is pulled in by `notifier.c`'s `#include "pattern_match.c"`
(resolved via VPATH, below) — **not** a separate `SRC` entry.

### 2. `VPATH += <module_path>`
Confirmed in `builddefs/build_keyboard.mk:536`:
```
$(INTERMEDIATE_OUTPUT)_INC := $(VPATH) $(EXTRAINCDIRS) $(KEYBOARD_PATHS)
```
VPATH is on the compiler include path. So `#include "notifier.h"` from the
user's keymap and `#include "pattern_match.c"` from `notifier.c` both resolve
without relative paths.

### 3. `-include <module_path>/rules.mk`
So `RAW_ENABLE = yes` set in the module's `rules.mk` takes effect globally.
`RAW_ENABLE` is **NOT** a data-driven feature key (confirmed: no `rawhid` entry
in `data/schemas/`) — it MUST go in `rules.mk`, not in `qmk_module.json` features.

### 4. `-DCOMMUNITY_MODULE_<LEAF>_ENABLE=TRUE` + hooks `*_<leaf>`
Require `<leaf>` to be a valid C identifier. `qmk_notifier` (underscore) is valid.
A hyphenated leaf is a **hard failure** — the generated `-D` define and hook
names would be invalid C tokens.

## API Version Assertion (§18.3 R3)

The generator defines (at lines 252-254 of `community_modules.py`):
```c
#define COMMUNITY_MODULES_API_VERSION_BUILDER(maj,min,pat) \
    ((((uint32_t)(maj))&0xFF)<<24) | ((((uint32_t)(min))&0xFF)<<16) | (((uint32_t)(pat))&0xFF)
#define COMMUNITY_MODULES_API_VERSION COMMUNITY_MODULES_API_VERSION_BUILDER(<latest>)
#define ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(maj,min,pat) \
    STATIC_ASSERT(COMMUNITY_MODULES_API_VERSION_BUILDER(maj,min,pat) <= COMMUNITY_MODULES_API_VERSION, ...)
```

**Host-test safety (load-bearing):** In the stub harness (`-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'`),
neither `COMMUNITY_MODULES_API_VERSION` nor `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION`
is defined (the stub doesn't pull in `community_modules.h`). The `#ifdef` guard
skips the assert entirely → all stub binaries stay green unchanged.

**Target 1.0.0:** The `data/constants/module_hooks/1.0.0.hjson` provides
`housekeeping_task` and `process_detected_host_os`. Current latest is 1.1.2.

## Module Hooks Surface (§18.2 hard limit)

### Hooks that ARE available (from `data/constants/module_hooks/*.hjson`):
- `keyboard_pre_init`, `keyboard_post_init`
- `housekeeping_task`
- `process_record`, `pre_process_record`, `post_process_record`
- `suspend_power_down`, `suspend_wakeup_init`, `shutdown`
- `process_detected_host_os` (guard: `defined(OS_DETECTION_ENABLE)`)
- `layer_state_set`, `led_matrix`, `rgb_matrix`, etc.

### Hooks that are NOT available (the irreducible limit):
- **`raw_hid_receive`** — confirmed NOT in any module hook hjson. The user MUST
  still define `raw_hid_receive` in their keymap and call `hid_notify()`. This
  is the one piece of glue that cannot be auto-registered.

## qmk_module.json Schema

From `data/schemas/community_module.jsonschema`:
```json
{
    "required": ["module_name", "maintainer"],
    "properties": {
        "module_name": {"type": "text_identifier"},
        "maintainer": {"type": "text_identifier"},
        "license": {"type": "string"},
        "url": {"type": "string", "format": "uri"},
        "keycodes": {"type": "keycode_decl_array"},
        "features": {"type": "features_config"}
    }
}
```

**For this module:**
- `module_name`: "QMK Notifier"
- `maintainer`: "dabstractor"
- `license`: TBD — no LICENSE file in repo. QMK examples use `GPL-2.0-or-later`.
  QMK is GPLv2+, so GPL-2.0-or-later is the safe default.
- `url`: "https://github.com/dabstractor/qmk_notifier"
- `keycodes`: `[]` — public surface is macros + functions, not keymap-bindable keys
- `features`: **omit** — module declares no data-driven feature keys

### Example modules in `qmk_firmware/modules/qmk/`:
- `hello_world/qmk_module.json` — has features (console, deferred_exec) + keycodes
- `super_alt_tab/qmk_module.json` — keycodes only, no features
- Both use `"license": "GPL-2.0-or-later"`

## Resolved Design Decisions (§18.4)

| Decision | Resolution | Rationale |
|----------|-----------|-----------|
| OS auto-wiring | **Keep push-only** | Preserves §1.1/§8.7 invariant; avoids forcing OS_DETECTION_ENABLE on every user |
| Legacy submodule flow | **Retire** | rules.mk becomes module-context; submodule `include` line is incompatible |
| Configurator | **No action** | Module needs custom raw_hid_receive (JSON keymaps can't express); note in README |