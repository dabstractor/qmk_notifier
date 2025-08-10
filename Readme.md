# QMK-Notifier

QMK-Notifier is a powerful QMK module that enables dynamic keymap switching based on active applications. This module runs directly on your keyboard firmware, pattern-matching against window titles and application identifiers to automatically activate layers or execute commands when specific applications gain focus.

## Features

- Match window titles with powerful pattern matching (including wildcards)
- Automatically switch layers based on active applications
- Execute custom callback functions when applications are focused/unfocused
- Efficient implementation with minimal resource usage
- Handles strings longer than the 32-byte HID packet limit

## How It Works

QMK-Notifier watches for messages sent from your computer to your keyboard via Raw HID. When a string is received (typically an application name and window title), the module pattern-matches it against user-defined rules and activates the appropriate layer or executes callback functions when a match is found.

The module uses two primary data structures:
- `command_map` - Maps patterns to callback functions
- `layer_map` - Maps patterns to QMK layers

## Setup

### 1. Add QMK-Notifier as a submodule to your keymap

```bash
cd /path/to/qmk_firmware/keyboards/your_keyboard
git submodule add https://github.com/dabstractor/qmk-notifier.git
```

### 2. Include the module in your keymap

In your `keymap.c` file:

```c
#include "qmk-notifier/notifier.h"

// ...

void raw_hid_receive(uint8_t *data, uint8_t length) {
    hid_notify(data, length);
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

- `*` - Wildcard matching any characters
- `^` - Anchor to start of string
- `$` - Anchor to end of string

The `WINDOW_TITLE(class, title)` (`WT()`) macro creates patterns that match both window class and title. Theoretically it could be any 2 sequential pieces of string data if a use case presents itself for that.


## Companion Projects

QMK-Notifier is designed to work with companion applications that send window information to your keyboard:

- [qmk_notifier](https://github.com/dabstractor/qmk_notifier) - Rust application that captures window information and sends it to your keyboard
- [hyprland-qmk-window-notifier](https://github.com/dabstractor/hyprland-qmk-window-notifier) - Hyprland-specific utility that captures window changes

For other environments, [zigotica/active-app-qmk-layer-updater](https://github.com/zigotica/active-app-qmk-layer-updater) offers support for Windows, macOS, and X11. MacOS and other Wayland environment support is planned for this project in the future.


## Compatibility with Other Raw HID Modules

QMK-Notifier is compatible with other raw HID modules. It identifies its own incoming messages by checking for the specific sequence of characters `0x81` followed by `0x9F` at the start of the message. Any messages that do not match this pattern are ignored, ensuring that it does not interfere with the operation of other modules on the same raw HID interface.

## Documentation

For more details on QMK Raw HID functionality, see the [official QMK RawHID documentation](https://docs.qmk.fm/#/feature_rawhid).

## Performance Considerations

The pattern matching is optimized for keyboard firmware, but complex patterns and large numbers of rules may impact performance. Keep your rule definitions concise for best results.

## Running Tests

To run the pattern matching tests:

```bash
gcc test_pattern_match.c pattern_match.c -o test_pattern_match
./test_pattern_match
```

---

## Contributing

Contributions to QMK-Notifier are welcome and appreciated! Here's how you can help:

1. **Start a conversation:** Before working on a major feature, please open an issue to discuss your ideas. This ensures your time is well-spent and aligns with the project's direction.

2. **Report bugs:** If you find a bug, please create an issue with clear reproduction steps.

3. **Submit improvements:** Pull requests for bug fixes, documentation improvements, and new features are welcome.

4. **Code quality:** Please follow the existing code style and include tests for new functionality when possible.

The goal of QMK-Notifier is to create a powerful, flexible system for context-aware keyboards. Your contributions help!


