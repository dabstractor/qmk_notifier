# Research Notes — P1.M2.T3.S1 (README multi-OS section)

## Task
Update `README.md` (Mode B — cross-cutting changeset documentation) to document
the multi-OS opt-in overlay end-to-end + backward-compat guarantee + updated
test gates. Documentation ONLY — no code changes.

## Current README structure (verified, headings + line numbers)
```
# QMK-Notifier                                  (line 1)
## Features                                     (line 5)   <- ADD multi-OS bullet
## How It Works                                 (line 13)
### Wire format                                 (line 17)
## Setup                                        (line 36)  <- ADD optional multi-OS note
  ### 1. Add QMK-Notifier as a submodule        (line 38)
  ### 2. Include the module in your keymap      (line 45)
  ### 3. Update your rules.mk                   (line 61)
## Usage                                        (line 69)  <- NEW SECTION AFTER THIS
  ### Define Command Mappings                   (line 71)
  ### Define Layer Mappings                     (line 87)
  ### Pattern Matching Syntax                   (line 99)
## Companion Projects                           (line 127) <- Multi-OS Config goes BEFORE this
## Compatibility with Other Raw HID Modules     (line 148)
## Documentation                                (line 152)
## Performance Considerations                   (line 156)
## Running Tests                                (line 160) <- UPDATE stub paragraph + status
  ### Quick Test                                (line 162)
  ### Comprehensive Test Suite                  (line 171)
  ### Current Test Status                       (line 204)
## Contributing                                 (line 225)
```
New section `## Multi-OS Configuration` inserts between `### Pattern Matching
Syntax` block end (line ~125) and `## Companion Projects` (line 127).

## Live test counts (VERIFIED by running the actual gates, 2025-07-18)
| Gate | Binary | Cases | Status |
|---|---|---|---|
| `./run_all_tests.sh` (9 pattern_match suites) | — | **2019 total** | 0 failures |
| `./run_notifier_stub_tests.sh` | test_notifier_dispatch | **11** | 0 FAIL |
| `./run_notifier_stub_tests.sh` | test_notifier_os | **31** (NEW) | 0 FAIL |

Runner is ALREADY the 4-step dual-binary version (P1.M2.T2.S1 implemented):
```
[1/4] stub-compile notifier.c ...
[2/4] link dispatch driver (test_notifier_dispatch) ...
[3/4] link multi-OS driver (test_notifier_os) ...
[4/4] run both ...
notifier dispatch fails=0  (exit=0)
notifier os fails=0  (exit=0)
✓ notifier stub-compile gate PASSED
```
The README's current "2019/2019" blurb covers ONLY the pattern_match corpus and
its stub paragraph mentions ONLY `test_notifier_dispatch.c` (11 cases). BOTH
need updating for the multi-OS suite.

## Exact stub paragraph to REPLACE (README lines ~189-197, verbatim)
> This stub-compiles `notifier.c` against the minimal `qmk_stubs/` and runs
> `test_notifier_dispatch.c` (11 cases covering F4 delimiter matching, dispatcher
> ordering, `hid_notify` reassembly, sanitization, acknowledgement, and NULL
> safety).

## Exact status blurb to UPDATE (README line ~220)
> **Overall Test Results**: 2019/2019 tests passing (100% success rate, 0 failures)

## Key API surface (verified in notifier.h — LANDED by P1.M1.T1.S1)
- `void notifier_set_os(os_variant_t os);` — the ONE required push call (notifier.h).
- `#include "os_detection.h"` — header-only; module uses `os_variant_t` TYPE only,
  NEVER calls `detected_host_os()` (push-only, no link dep on OS-detection .c).
- `os_variant_t` enum: `OS_UNSURE/0, OS_LINUX/1, OS_WINDOWS/2, OS_MACOS/3, OS_IOS/4`.
- `DEFINE_SERIAL_COMMANDS_OS(os, { ... })` / `DEFINE_SERIAL_LAYERS_OS(os, { ... })`.
  `os` = enumerator token (OS_MACOS), NOT a string. Row shape identical to default
  macro. `OS_UNSURE` has NO _OS map by design.
- Backward-compat guarantee (structural): zero `DEFINE_*_OS` macros => all 16 per-OS
  weak accessors return `{NULL,0}` => `select_*_map_os` returns `{NULL,0}` for every
  OS => OS scan runs 0 iterations => default maps scanned => byte-identical to
  pre-multi-OS firmware (invariant 19, findings F3).

## Dispatch rule to document (PRD §2 F8.4/F8.5 — verified in notifier.c:334-363)
Per map type (command AND layer), INDEPENDENTLY:
1. scan the OS-specific map for `current_os` FIRST;
2. a match there WINS and the default map for that track is NOT consulted;
3. no OS map (or no match in it) => fall back to the DEFAULT map, first-match-wins.
The command and layer tracks each decide independently (F8.5). `OS_UNSURE` =>
default only (F8.6). `notifier_set_os(changed)` clears state (disable_command +
deactivate_layer) without re-dispatch (F9); idempotent on unchanged value (F9.3).

## Code snippets to use (from PRD §10.1 step 3 + §10.3 — the canonical references)

### Integration snippet (§10.1 step 3) — process_detected_host_os_kb wiring
```c
#include QMK_KEYBOARD_H
#include "./qmk-notifier/notifier.h"

void raw_hid_receive(uint8_t *data, uint8_t length) {
    hid_notify(data, length);
    /* other Raw HID modules can be called here too */
}

/* Multi-OS only: the sole required call to feed the detected OS in. */
bool process_detected_host_os_kb(os_variant_t os) {
    notifier_set_os(os);          /* enables DEFINE_*_OS map selection */
    /* your existing OS-specific logic (e.g. enable_vim_for_mac()) */
    return true;
}
```

### Multi-OS reference config (§10.3 excerpt) — DEFINE_*_OS with macOS + Linux
```c
/* Default maps: OS-AGNOSTIC rules live here (gaming, calculator, ...). */
DEFINE_SERIAL_COMMANDS({
    { WT("steam_app*", "*"), &disable_vim },
    { WT("cs2", "Counter-Strike 2"), &disable_vim },
});
DEFINE_SERIAL_LAYERS({
    { "*calculator", _NUMPAD },
    { "blender", _BLENDER },
    { "steam_app*", _GAMING },
});

/* macOS-specific: scanned FIRST when current_os == OS_MACOS; a match here
 * prevents the default map for that track from running. */
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { "iTerm", &disable_vim },
    { "Terminal", &disable_vim },
    { WT("Google Chrome", "*claude*"), &vim_lazy_insert, &disable_vim },
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "iTerm", _TERMINAL },
    { "Terminal", _TERMINAL },
    { WT("Google Chrome", "*"), _BROWSER },
});

/* Linux-specific (Hyprland/X11 class names). */
DEFINE_SERIAL_LAYERS_OS(OS_LINUX, {
    { "*alacritty*", _TERMINAL },
    { "*kitty*", _TERMINAL },
    { "firefox", _BROWSER },
});
```

## "What this does NOT change" note (item 3d — must include)
- The wire protocol is UNCHANGED (no OS byte is sent; `class\x1Dtitle` as before).
- The pattern matcher is UNTOUCHED (P1 scope, complete).
- Host-provided OS (host declares its OS via a typed `0xF0` command) and full
  host-rule replacement are PLANNED future work (PRD §4.7 / §14.1), NOT implemented.
  Today the OS comes only from QMK's firmware-side `OS_DETECTION`, pushed in by
  the keymap.

## README example-style note
The README's existing Usage examples use `enable_vim_mode`/`disable_vim_mode`/
`set_rotary_encoder_figma` (its own illustrative style), while the PRD §10.3
reference uses `disable_vim`/`vim_lazy_insert`. For the NEW multi-OS section, use
the PRD §10.3 excerpt verbatim (it is the canonical reference and is internally
consistent). Do NOT rewrite the existing Usage examples.

## Validation approach (no compiler for markdown)
- grep for all required elements (every §10.3 macro, the wiring snippet, the
  backward-compat statement, the "does NOT change" note, live counts).
- markdown sanity (heading levels, code fences balanced, table pipes).
- `git diff --stat` shows ONLY README.md changed (+ plan/ PRP/research).