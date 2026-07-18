# QMK-Notifier

QMK-Notifier is a powerful QMK module that enables dynamic keymap switching based on active applications. This module runs directly on your keyboard firmware, pattern-matching against window titles and application identifiers to automatically activate layers or execute commands when specific applications gain focus.

## Features

- Match window titles with powerful pattern matching (including wildcards)
- Automatically switch layers based on active applications
- Execute custom callback functions when applications are focused/unfocused
- Efficient implementation with minimal resource usage
- Handles strings longer than the 32-byte HID packet limit

## How It Works

QMK-Notifier watches for messages sent from your computer to your keyboard via Raw HID. When a string is received (typically an application class and window title), the module pattern-matches it against user-defined rules and activates the appropriate layer or executes callback functions when a match is found.

### Wire format

The companion app sends a logical message of the form
`<app_class>\x1D<window_title>`, where `\x1D` is the **GS_DELIMITER** = ASCII 29
= the **Group Separator** control character (`0x1D`) — not the `0x1F`
separator. The message is chunked into 32-byte Raw HID reports by the
transport crate, each prefixed with the magic header bytes `0x81 0x9F`, and
terminated with `ETX` (`0x03`).

The module first checks every incoming report for the magic header `0x81 0x9F`
and **ignores any report that does not match**, so it safely coexists with other
Raw HID modules on the same interface (see *Compatibility with Other Raw HID
Modules* below). The `WINDOW_TITLE(class, title)` / `WT()` macro builds the
`class\x1Dtitle` pattern the matcher compares against.

The module uses two primary data structures:
- `command_map` - Maps patterns to callback functions
- `layer_map` - Maps patterns to QMK layers

## Setup

### 1. Add QMK-Notifier as a submodule to your keymap

```bash
cd /path/to/qmk_firmware/keyboards/your_keyboard
git submodule add https://github.com/dabstractor/qmk-notifier.git qmk-notifier
```

### 2. Include the module in your keymap

In your `keymap.c` file:

```c
#include QMK_KEYBOARD_H
#include "./qmk-notifier/notifier.h"

// ...

void raw_hid_receive(uint8_t *data, uint8_t length) {
    hid_notify(data, length);
    /* other Raw HID modules can be called here too */
}
```

### 3. Update your rules.mk

Add the following line to your keymap's `rules.mk` file:

```
include keyboards/handwired/[manufacturer]/[keyboard_name]/qmk-notifier/rules.mk
```

## Usage

### Define Command Mappings

Create command mappings in your `keymap.c` file to execute functions when specific applications are active:

```c
// ...
void set_rotary_encoder_figma(void) {
    // Configure rotary encoder for Figma
}

DEFINE_SERIAL_COMMANDS({
    { "gedit", &enable_vim_mode, &disable_vim_mode, true },
    { WT("firefox", "^figma*"), &set_rotary_encoder_figma }
});
```

### Define Layer Mappings

Create layer mappings to activate specific layers when applications are focused:

```c
DEFINE_SERIAL_LAYERS({
    { "*calculator*", _NUMPAD },
    { WT("firefox", "*youtube*"), _YOUTUBE, true },
    { WINDOW_TITLE("Gimp", "*"), _GIMP, true }
});
```

### Pattern Matching Syntax

The matcher is a linear-time Thompson NFA (no backtracking), so even
pathological patterns like `a+a+a+a+a+a+a+a+a+a+b` against 199 `a`s finish in
under 2 ms (well under the 50 ms gate).

| Construct | Meaning |
|---|---|
| `*` | Wildcard — any sequence (incl. empty, incl. `\n`/`\r`) |
| `^` at start | Anchor to beginning of string |
| `$` at end | Anchor to end of string |
| `^…$` | Exact full-string match |
| *(no anchors)* | Substring match (backward-compatible default) |
| `.` | Any char **except** `\n` / `\r` |
| `X+` | One or more of element `X` (linear-time, no backtracking) |
| `\d`  `\D` | Digit `[0-9]` / non-digit |
| `\w`  `\W` | Word char `[A-Za-z0-9_]` / non-word char |
| `\s`  `\S` | Whitespace `[ \t\n\r\f\v]` / non-whitespace |
| `\b`  `\B` | Word boundary / non-boundary (zero-width) |
| `\^`  `\$`  `\*`  `\\` | Literal escaped metacharacters |
| `\.`  `\+` | Literal dot / literal plus |

The `WINDOW_TITLE(class, title)` (`WT()`) macro creates a two-part pattern that
matches both the window class and the title, joined by the `GS_DELIMITER`
(`\x1D`). A bare pattern (no `WT`) matches the **class part only** of the
incoming message — e.g. `neovide` does not match the title `main.rs`.


## Companion Projects

QMK-Notifier (this firmware C module) is the on-keyboard **receiver + matcher +
actor**. It is designed to work with companion desktop software that detects
the foreground window and sends `class\x1Dtitle` to your keyboard:

- [QMKonnect](https://github.com/dabstractor/qmkonnect) - Rust cross-platform **desktop daemon**. Detects the foreground window and sends the `class\x1Dtitle` string to the keyboard over Raw HID.
- [qmk_notifier](https://github.com/dabstractor/qmk_notifier) *(underscore)* - Rust **transport crate** that QMKonnect links. Owns the wire framing: the magic header, 32-byte report chunking, the `ETX` terminator, and the device cache.

> **Naming hazard:** `qmk-notifier` (hyphen) is this firmware C module.
> `qmk_notifier` (underscore) is the Rust transport crate. The two halves talk
> over the fixed wire protocol described in *How It Works*.

Community / third-party alternatives:

- [hyprland-qmk-window-notifier](https://github.com/dabstractor/hyprland-qmk-window-notifier) - Hyprland-specific utility that captures window changes.
- [zigotica/active-app-qmk-layer-updater](https://github.com/zigotica/active-app-qmk-layer-updater) - support for Windows, macOS, and X11.

macOS and other Wayland environments are planned for QMKonnect in the future.


## Compatibility with Other Raw HID Modules

QMK-Notifier is compatible with other raw HID modules. It identifies its own incoming messages by checking for the specific sequence of characters `0x81` followed by `0x9F` at the start of the message. Any messages that do not match this pattern are ignored, ensuring that it does not interfere with the operation of other modules on the same raw HID interface.

## Documentation

For more details on QMK Raw HID functionality, see the [official QMK RawHID documentation](https://docs.qmk.fm/#/feature_rawhid).

## Performance Considerations

The pattern matching is optimized for keyboard firmware, but complex patterns and large numbers of rules may impact performance. Keep your rule definitions concise for best results.

## Running Tests

### Quick Test

To run the main pattern matching tests:

```bash
gcc test_pattern_match.c pattern_match.c -o test_pattern_match
./test_pattern_match
```

### Comprehensive Test Suite

To build and run all 9 host-side suites plus a performance micro-benchmark:

```bash
./run_all_tests.sh
```

This builds and runs the 9 pattern-matcher suites (all linking `pattern_match.c`) and prints an aggregate summary. The live per-suite breakdown:

| Suite | Count | Covers |
|---|---|---|
| `test_pattern_match` | 376 | anchors, escapes, wildcards, case sensitivity, parsing, edge cases, metachars |
| `test_char_classification` | 179 | `\d \D \w \W \s \S` classification (indirect via metachars) |
| `test_word_boundary_basic` | 74 | `\b` / `\B` boundary semantics |
| `test_word_boundary_integration` | 189 | `\b` / `\B` integrated with anchors/wildcards/classes |
| `test_metachar_verification` | smoke | `\d \D \w \W \s \S` smoke test + combos (boolean PASS/FAIL) |
| `test_comprehensive_integration` | 10 | multi-feature combos, performance, memory management (compiled with `-DNOTIFIER_STUB`) |
| `test_error_handling` | 161 | NULL/garbage inputs, malformed escapes |
| `test_memory_stress` | 32 | long strings, repeated alloc/free (no leaks/crashes) |
| `test_invalid_patterns` | 1008 | 46 pathological patterns × many inputs |

A second, separate gate validates the **receiver/dispatcher** side of the module:

```bash
./run_notifier_stub_tests.sh
```

This stub-compiles `notifier.c` against the minimal `qmk_stubs/` and runs
`test_notifier_dispatch.c` (11 cases covering F4 delimiter matching, dispatcher
ordering, `hid_notify` reassembly, sanitization, acknowledgement, and NULL
safety).

### Current Test Status

The pattern matching library implements a full regex construct set:
- `\d`, `\D` - Digit and non-digit matching
- `\w`, `\W` - Word character and non-word character matching
- `\s`, `\S` - Whitespace and non-whitespace matching
- `\b`, `\B` - Word boundary and non-boundary matching (zero-width)
- `.` - Dot metacharacter (any character except `\n`/`\r`)
- `+` - One-or-more quantifier (linear-time, no backtracking)
- `*`, `^`, `$` - Wildcard, start anchor, end anchor

**Overall Test Results**: 2019/2019 tests passing (100% success rate, 0 failures)
**Performance Impact**: Negligible (~0.1 microseconds per `pattern_match` call)

All original functionality works identically (no breaking changes), and
performance remains excellent — the pathological NFA stress case
(`a+a+a+a+a+a+a+a+a+a+b` vs 199 `a`s) completes in under 2 ms. The matcher is
**production-ready**.

---

## Contributing

Contributions to QMK-Notifier are welcome and appreciated! Here's how you can help:

1. **Start a conversation:** Before working on a major feature, please open an issue to discuss your ideas. This ensures your time is well-spent and aligns with the project's direction.

2. **Report bugs:** If you find a bug, please create an issue with clear reproduction steps.

3. **Submit improvements:** Pull requests for bug fixes, documentation improvements, and new features are welcome.

4. **Code quality:** Please follow the existing code style and include tests for new functionality when possible.

The goal of QMK-Notifier is to create a powerful, flexible system for context-aware keyboards. Your contributions help!


