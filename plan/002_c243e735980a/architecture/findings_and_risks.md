# Findings & Risks — Plan 002 (Multi-OS Delta)

## Verified facts (grounding the breakdown)

### F1. The `##os` symbol naming contract is load-bearing

`DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {…})` generates symbols via preprocessor
token-paste:
- `_notifier_command_map_OS_MACOS` (the array)
- `_notifier_get_command_map_OS_MACOS()` (accessor)
- `_notifier_get_command_map_OS_MACOS_size()` (size accessor)

The weak defaults in `notifier.c` (P1.M1.T3.S1) must declare functions with
these EXACT mangled names. The `select_command_map_os()` switch references them
by enumerator → symbol mapping. If any name mismatches (e.g. typo in `##os`),
the link fails or the wrong weak default wins silently.

**Confirmed:** `os_variant_t` enumerator names from QMK source are exactly
`OS_UNSURE, OS_LINUX, OS_WINDOWS, OS_MACOS, OS_IOS`. Only LINUX/WINDOWS/MACOS/IOS
get per-OS accessors; `OS_UNSURE` has none by design (→ default fallback).

### F2. The dispatch modification is mechanical but touches the CONSOLE block

Current `process_full_message` uses a single `cmd_map`/`lyr_map` variable pair.
The delta splits each into (os_map, os_size) + (def_map, def_size) and adds an
OS-first-then-fallback loop per track.

**Gotcha:** the `#ifdef CONSOLE_ENABLE` debug block at the end currently indexes
`cmd_map[found_command_match].pattern`. After the split, the matched map could be
either OS or default. The implementer should use `command_found->pattern` (the
pointer is already set to whichever entry matched) instead of re-indexing by
the stale `cmd_map` variable name. (The layer track has no equivalent debug
print of the matched entry's pattern — it only prints the message + match/miss.)

### F3. Backward compatibility is structurally guaranteed (not a special case)

When no `DEFINE_*_OS` macros exist, all 16 per-OS weak accessors return
`{NULL, 0}`. `select_*_map_os()` returns `{NULL, 0}` for every OS including the
current one. The OS-first loop body runs 0 iterations (size 0), falls through to
the default-map scan — **identical machine behavior** to today. No conditional
`#ifdef` is needed; the zero-size loop IS the backward-compat guarantee
(invariant 19).

### F4. The stub `os_detection.h` must be minimal (NOT include usb_device_state.h)

The real QMK `os_detection.h` starts with `#include "usb_device_state.h"`. Our
stub must NOT — it only defines the `os_variant_t` enum (the sole thing
`notifier.c`/`notifier.h` consume). Adding the `usb_device_state.h` include
would cascade into undefined types in the stub harness.

### F5. test_notifier_dispatch.c should pass UNCHANGED

It defines `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS` (no `_OS` variants)
and never calls `notifier_set_os`. So `current_os` stays `OS_UNSURE` →
`select_*_map_os` returns `{NULL,0}` → OS scan runs 0 iterations → default map
scanned → identical to today. **No edits expected** (delta PRD §3 confirms).
If a tweak IS needed, keep it scoped.

### F6. Test observation strategy for test_notifier_os.c

The stub `layer_on`/`layer_off` write to a file-static `g_active_layer` in
`qmk_stubs.c` that the test cannot read directly. To verify WHICH map matched
(OS vs default), the test should use **distinguishable callback functions** that
set separate global flags:
```c
static int os_cmd_fired = 0, def_cmd_fired = 0;
static void os_en(void){ os_cmd_fired++; }
static void def_en(void){ def_cmd_fired++; }
```
For the layer track, the test can verify via `process_full_message`'s return
value (matched=true) and by checking that a DIFFERENT layer index is activated
than the default would produce — OR by adding a layer-on observable to the stub.
Simplest: use distinct layer numbers in OS vs default maps and verify the return
value distinguishes matched-vs-not; for layer identity, extend `qmk_stubs.c`
with a `stub_get_active_layer()` accessor (a 1-line addition, Mode A doc — it
enhances the test harness, not production code). The implementer decides the
cleanest observation path.

## Risks

| # | Risk | Likelihood | Mitigation |
|---|---|---|---|
| R1 | `##os` symbol mismatch between header macro and notifier.c weak default | Medium | Exact names documented in architecture/external_deps.md; test_notifier_os.c exercises DEFINE_*_OS (link failure = immediate signal) |
| R2 | CONSOLE_ENABLE debug print indexes stale map variable after split | Low | Use `command_found->pattern` pointer (already set correctly) |
| R3 | Stub os_detection.h accidentally includes usb_device_state.h | Low | Architecture doc F4 explicitly warns against it |
| R4 | test_notifier_dispatch.c breaks due to global `current_os` state leaking between cases | Low | current_os is OS_UNSURE (never set); each case calls process_full_message which disable/deactivate first |
| R5 | OS-first scan inadvertently scans default when OS map exists but matches nothing AND that's undesired | — | This is BY DESIGN (F8.4: OS map no-match → fall back to default). Tests must assert this explicitly (§11.2D ii). |

## No-ops confirmed (do NOT implement)

- §4.7 / §14.1 host-provided OS (HELD next cycle) — no `0xF0` SET_OS command.
- §14 host-layer / host-callback trackers — not touched.
- The wire protocol (§4.1–4.5) is unchanged — no OS byte is sent.
- `pattern_match.c/h`, `rules.mk` — untouched.