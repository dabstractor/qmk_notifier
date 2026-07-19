# Research Notes — P1.M1.T2.S1: host state globals, weak host-callback accessors, board_rules_present

## What this task is

Add three additive pieces to `notifier.c` (the consumer side of the host-rules
API that sibling task P1.M1.T1.S1 just added to `notifier.h`):

1. **Host state globals** (§8.1 region, near `activated_layer`/`current_os`):
   `host_layer`, `host_cb_enabled[HOST_CALLBACK_MAX]`, `has_been_queried`.
2. **Weak-default host-callback accessors** (§8.3 region, mirroring the board-map
   weak block): `get_host_callbacks`→NULL, `get_host_callbacks_size`→0.
3. **`board_rules_present()` static helper** (§4.6 QUERY_INFO bit): true iff ANY
   board map (default + all per-OS) is non-empty.

This is pure state scaffolding — it adds NO behavior yet. The consumers land in
P1.M2 (set_host_layer, apply_host_callbacks, handle_typed_command/QUERY_INFO).

## Dependency on sibling task P1.M1.T1.S1 (CONTRACT — assume landed)

notifier.h will contain (per that PRP, treated as a hard contract):
- `typedef struct { const char *name; callback_t on_enable; callback_t on_disable; } host_callback_t;`
- `host_callback_t* get_host_callbacks(void);` + `size_t get_host_callbacks_size(void);` (decls)
- `#define HOST_CALLBACK_MAX 32` and `#define HOST_LAYER_BASE 224`
- `#define DEFINE_HOST_CALLBACKS(...)` → emits `user_host_callbacks[]` + strong
  `get_host_callbacks`/`get_host_callbacks_size` (PLAIN user_*/get_* naming, like
  DEFINE_SERIAL_COMMANDS — NOT the `_notifier_` namespacing).
- `callback_t` already typedef'd (nullary fn pointer).

**My weak defs must be named EXACTLY `get_host_callbacks` / `get_host_callbacks_size`**
or the macro's strong defs won't override them. (Verified by link+nm: strong `T`
beats weak `W`.)

## Current notifier.c structure (verified — line numbers from working tree)

```
L1–11   includes (QMK_KEYBOARD_H, NFA_MAX_PATTERN=128 guard, pattern_match.c, notifier.h, raw_hid.h, string.h, print.h)
L29–35  compile-time NFA guard typedef
L37–51  RAW_REPORT_SIZE 32 (+ load-bearing comment)
L53–92  sanitize_string()
L79     #define MSG_BUFFER_SIZE 256
L81–82  msg_buffer, msg_index
L88–90  static bool dropping
L93–94  empty_command_map / empty_layer_map
L97–110 DEFAULT weak accessors: get_command_map/_size, get_layer_map/_size   ← §8.3 region (INSERTION A here)
L113    #define LAYER_UNSET 255
L114    uint8_t activated_layer = LAYER_UNSET;
L116    command_map_t *current_command = {0};
L120–122 os_variant_t current_os = OS_UNSURE;                                 ← §8.1 region (INSERTION B here)
L124–   per-OS weak accessors comment block
L136–153 _notifier_get_{command,layer}_map_OS_{LINUX,WINDOWS,MACOS,IOS}[_size] weak defs
L157–172 select_command_map_os / select_layer_map_os                          ← (INSERTION C after this)
L181+   activate_layer/deactivate_layer/enable_command/disable_command (§8.4)
...     match_pattern, process_full_message, notifier_set_os, hid_notify
```

**Insertion plan (3 sites, additive, no restyle):**
- **A** (weak host-callback accessors): after `get_layer_map_size` close-brace
  (L110), before `#define LAYER_UNSET` (L113). Mirrors the default board-accessor
  block directly above it.
- **B** (host state globals): after `os_variant_t current_os = OS_UNSURE;` (L122),
  before the per-OS comment block (L124). Groups with board state globals.
- **C** (board_rules_present): after `select_layer_map_os` close-brace (L172), as a
  new static helper. Must come AFTER the per-OS `_size` accessors (L136–153) it
  calls; placing after the selectors keeps the weak-accessor+selector block intact.

## Why the globals are a SEPARATE plane (architecture invariant 21)

`host_rules_architecture.md` "two independent state planes": board state
(activated_layer/current_command/current_os) is driven ONLY by the legacy string
path (process_full_message); host state (host_layer/host_cb_enabled/has_been_queried)
is driven ONLY by typed commands (handle_typed_command). They are orthogonal. The
inline comment on the globals block must state this (item point 5 / Mode A).

## board_rules_present — F8: must check ALL maps

`findings_and_risks.md` F8: the bit is `1` iff ANY board map (default command,
default layer, OR any per-OS command/layer map) is non-empty. Implementation calls
all 10 `_size` accessors. Per-OS granularity is NOT exposed (§4.6: "a single bit
suffices"). I verified the logic with a mock TU: setting each of the 10 accessors
non-empty in turn flips the result true; all-empty → false.

## Ground-truth validation (run DURING research against a temp notifier.c with the 3 insertions)

- **[1] stub-compile** (`-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=… -Iqmk_stubs -I.`):
  exit 0. Adds ONLY 4 expected `-Wunused` warnings (see below) — no errors.
- **[2] globals + weak + brp (via `#include "notifier.c"`)**: `host_layer==255`,
  `has_been_queried==false`, `host_cb_enabled[]` all false, `get_host_callbacks()==NULL`,
  `get_host_callbacks_size()==0`, `board_rules_present()==false` (all-empty). **PASS**.
- **[3] board_rules_present LOGIC (standalone mock, all 10 accessors)**: every
  accessor alone → true; all-empty → false. **ALL PASS** (F8 coverage).
- **[4] weak-override linkage**: keymap `DEFINE_HOST_CALLBACKS({2 entries})` +
  notifier.o → `nm` shows strong `T get_host_callbacks_size` (keymap) overriding
  weak `W` (notifier.c); runtime `get_host_callbacks_size()==2`. **PASS**.
- **[5] REGRESSION**: modified notifier.c + `test_notifier_dispatch` → **14/14
  passed**, exit 0. No regression.

## The 4 expected -Wunused warnings (CRITICAL to document, not "fix")

The modified notifier.c produces exactly:
```
warning: 'host_layer' defined but not used [-Wunused-variable]
warning: 'host_cb_enabled' defined but not used [-Wunused-variable]
warning: 'has_been_queried' defined but used [-Wunused-variable]
warning: 'board_rules_present' defined but not used [-Wunused-function]
```
**These are EXPECTED and correct** at this subtask boundary:
- `host_layer` → consumed by `set_host_layer` (P1.M2.T1.S2).
- `host_cb_enabled[]` → consumed by `apply_host_callbacks` (P1.M2.T1.S3).
- `has_been_queried` → set by QUERY_INFO handler (P1.M2.T2.S1).
- `board_rules_present()` → called by QUERY_INFO handler (P1.M2.T2.S1).

The build is NOT `-Werror` (confirmed: dispatch suite compiles+links+runs 14/14
green with these warnings present), so `run_notifier_stub_tests.sh` and the §11.2
gate stay green. The warnings vanish automatically once P1.M2 wires the consumers.
**Do NOT "fix" them by deleting the symbols** — that would break the contract this
task establishes for P1.M2.

Recommendation: leave the symbols PLAIN (no `__attribute__((unused))`) to match the
existing file style (which uses only `__attribute__((weak))`, never `unused`).
`__attribute__((unused))` is mentioned as an OPTIONAL alternative but adds a new
pattern + cleanup churn; not recommended.

## Scope boundaries (do NOT do here)
- Do NOT implement `set_host_layer`/`apply_host_callbacks`/`handle_typed_command`
  (P1.M2). The globals just EXIST this task.
- Do NOT wire `board_rules_present` into QUERY_INFO (P1.M2.T2.S1).
- Do NOT modify `notifier.h` (P1.M1.T1.S1 owns it).
- Do NOT touch the `apply_os_change` refactor (that's sibling P1.M1.T2.S2).
- Do NOT modify pattern_match.*, qmk_stubs/*, test_*.c, run_*.sh.

## Zero new external deps
Additions use only: `host_callback_t`/`HOST_CALLBACK_MAX` (notifier.h), the
existing `_notifier_get_*_map_OS_*_size` accessors (already in notifier.c), and
libc types (uint8_t, bool, size_t — already included). No rules.mk change.