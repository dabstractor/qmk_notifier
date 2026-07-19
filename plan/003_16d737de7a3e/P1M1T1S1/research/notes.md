# Research Notes — P1.M1.T1.S1 (notifier.h host-rules API surface)

## Task
Add the Host-Side Rules & Typed Commands public surface to the existing
`notifier.h` (plan 003): host_callback_t struct, host-callback accessor decls,
DEFINE_HOST_CALLBACKS macro, and the typed-command constants block. Header-only.

## Current file state (notifier.h, 91 lines)
- Plan-002 multi-OS surface ALREADY MERGED: `#include "os_detection.h"`,
  `notifier_set_os`, DEFINE_SERIAL_*_OS macros all present.
- Comment style: terse `//` line comments (not `/* */`).
- Structure (line-anchored):
  L1   `#pragma once`
  L2-4 includes (`<stdbool.h>`, `os_detection.h`)
  L5   `typedef void (*callback_t)(void);`
  L6-11 command_map_t
  L12-16 layer_map_t  (ends `} layer_map_t;`)
  L17  blank
  L18  `// Forward declarations - implementation provided in notifier.c`
  L19-22 get_command_map/_size, get_layer_map/_size
  L23-24 notifier_set_os comment + `void notifier_set_os(os_variant_t os);`
  L25  blank
  L26-29 GS_DELIMITER, ETX_TERMINATOR, WINDOW_TITLE, WT
  L30  blank
  L31  `// Define macros to create the maps`
  L32-36 DEFINE_SERIAL_COMMANDS
  ... DEFINE_SERIAL_LAYERS, DEFINE_SERIAL_*_OS, entry points
- `size_t` is used in decls WITHOUT `#include <stddef.h>` — relies on includer.
  (Pre-existing latent trait, plan 002. Out of scope; don't "fix".)

## The macro (VERIFIED — mirrors DEFINE_SERIAL_COMMANDS exactly)
```c
#define DEFINE_HOST_CALLBACKS(...) \
    host_callback_t user_host_callbacks[] = __VA_ARGS__; \
    const size_t user_host_callbacks_size = sizeof(user_host_callbacks) / sizeof(user_host_callbacks[0]); \
    host_callback_t* get_host_callbacks(void) { return user_host_callbacks; } \
    size_t get_host_callbacks_size(void) { return user_host_callbacks_size; }
```
EMPIRICALLY VERIFIED (built keymap.c + mock weak defaults, linked, ran, nm'd):
- Produces exactly: `user_host_callbacks` (data), `user_host_callbacks_size` (rodata),
  `get_host_callbacks` (text), `get_host_callbacks_size` (text).
- A keymap defining the registry OVERRIDES the weak defaults (count=2, not weak 0).
- NULL on_disable works (cb[1].on_disable==NULL accepted).
- KEY DIFFERENCE from DEFINE_SERIAL_*_OS: this uses the plain `user_*`/`get_*`
  naming (like DEFINE_SERIAL_COMMANDS), NOT the `_notifier_` namespacing.
  ⇒ notifier.c (P1.M1.T2.S1) weak defaults MUST be named exactly
  `get_host_callbacks` and `get_host_callbacks_size`. A mismatch = link failure.
- 3-field struct {name,on_enable,on_disable} does NOT trigger
  -Wmissing-field-initializers (unlike the 4-field command_map_t). Cleaner.

## The struct (§14)
```c
typedef struct { const char *name; callback_t on_enable; callback_t on_disable; } host_callback_t;
```
Uses `callback_t` (defined L5, above the insertion point). Field order matters
only for initializer parity with keymap rows; match §14 exactly.

## Constants (exact values — VERIFIED from §4.6/§16/external_deps)
NOTIFY_CMD_DISCRIMINATOR 0xF0   (§4.6 — data[2] typed marker)
NOTIFY_RESPONSE_MARKER   0x51   (§4.6 — typed response marker, >=2, vs legacy 0/1)
NOTIFY_CMD_QUERY_INFO    0x01   (§4.6 cmd table)
NOTIFY_CMD_QUERY_CALLBACK 0x02  (§4.6 cmd table)
NOTIFY_CMD_SET_OS        0x03   (§4.6 cmd table)
NOTIFY_CMD_APPLY_HOST_CONTEXT 0x05  (§4.6 cmd table; 0x04 reserved for VIA-coexist)
NOTIFY_PROTO_VER         2      (§4.6 — 1=legacy string-only, 2=typed-capable)
NOTIFY_FEATURE_APPLY_HOST_CONTEXT 0x01  (§4.6 feature_flags bit)
NOTIFY_FEATURE_CALLBACK_REGISTRY  0x02  (§4.6 feature_flags bit)
NOTIFY_FEATURE_VIA_COEXIST        0x04  (§4.6 feature_flags bit, reserved)
HOST_CALLBACK_MAX        32     (§14 — static array bound for host_cb_enabled[])
HOST_LAYER_BASE          224    (§14/§16 — host layers reserved >= 224)
NOTE: LAYER_UNSET (255) is a notifier.c constant (§16) — NOT added to this header.

## feature_flags build idiom (host_rules_arch §QUERY_INFO, used by notifier.c)
`feature_flags = 0x01 | (get_host_callbacks_size() > 0 ? 0x02 : 0)`  → 0x03 if a
registry is defined, 0x01 if not. The constant defines enable this; verified.

## Insertion points (precise)
1. host_callback_t typedef → immediately AFTER `} layer_map_t;` (L16), before
   the blank + `// Forward declarations`. (Keeps structs grouped; uses callback_t.)
2. accessor decls `get_host_callbacks`/`get_host_callbacks_size` → in the
   forward-decl block, right AFTER `void notifier_set_os(os_variant_t os);` (L24).
   (Keeps accessor decls grouped.)
3. constants block + DEFINE_HOST_CALLBACKS macro → one labeled contiguous block
   placed AFTER the `#define WT(...)` helper (L29) and BEFORE
   `// Define macros to create the maps` (L31). (Satisfies item's "AFTER layer_map_t,
   BEFORE the DEFINE_SERIAL macros"; keeps new host-rules surface cohesive.)

## Mode-A comments required (item point 5)
- Each constants block: cite its PRD section (§4.6 for discriminator/marker/
  cmd_ids/proto_ver/feature_flags; §14 for HOST_CALLBACK_MAX + HOST_LAYER_BASE;
  §16 for values).
- DEFINE_HOST_CALLBACKS: note "ID = array index, stable per build; host re-queries
  names on reconnect so cross-flash renumbering is harmless (§14)".
- Match the file's terse `//` style.

## Backward compatibility (structural — host_rules_arch §Backward compat #1)
- A keymap that OMITS DEFINE_HOST_CALLBACKS → notifier.c weak defaults supply
  {NULL, 0} → feature_flags bit 0x02 clear → callback_count=0. Links + behaves
  identically to today. NO #ifdef needed (verified structurally).
- Existing includers (notifier.c, test_notifier_dispatch.c, test_notifier_os.c)
  get NEW symbols only; nothing existing is removed/renamed → they stay green.

## Toolchain / validation
- gcc, plain (no make/cmake). Header parses via include; macro validated by
  compile+link+run against mock weak defaults (done).
- qmk_stubs/os_detection.h now EXISTS (landed in plan 002 P1.M1.T2.S1), so the
  real run_notifier_stub_tests.sh is expected to still pass after this additive
  change (existing stub tests unchanged).
- No -Wall/-Wextra warnings from the new content (3-field struct; integer hex
  constants; clean macro). Pre-existing -Wsign-compare warnings in TEST files are
  unrelated.

## Scope boundaries
- Modify ONLY notifier.h (additive: 1 struct, 2 decls, 1 macro, 12 constants,
  Mode-A comments).
- DO NOT touch notifier.c (host globals + weak accessors are P1.M1.T2.S1),
  pattern_match.*, test_*.c, run_*.sh, PRD.md, tasks.json, prd_snapshot.md.
- DO NOT add LAYER_UNSET to the header (it's notifier.c's, §16).
- DO NOT reformat/restyle existing content.