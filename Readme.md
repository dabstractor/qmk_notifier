# QMK Notifier Tool

A high-performance Rust application that sends string commands to QMK keyboards via Raw HID interface.

## Overview

I built `qmk_notifier` to communicate with QMK-powered keyboards from your desktop. It efficiently handles serializing messages and sending them in batches to overcome the 32-byte HID report size limitation, enabling seamless communication with your keyboard for dynamic layer and command management.

This tool is part of a broader ecosystem I've created:
- **qmk_notifier** (this tool): Desktop application that sends commands to your keyboard
- **[qmk-notifier](https://github.com/dabstractor/qmk-notifier)**: QMK module that receives commands and handles layer/feature toggling
- **[hyprland-qmk-notifier](https://github.com/dabstractor/hyprland-qmk-notifier)**: Wayland application that detects window changes and sends application info to the keyboard

## Installation

### From Source

```bash
# Clone the repository
git clone https://github.com/dabstractor/qmk_notifier.git
cd qmk_notifier

# Build the project
cargo build --release

# The binary will be available at target/release/qmk_notifier
```

### Dependencies

Make sure you have the HID API development libraries installed:

```bash
# For Debian/Ubuntu
sudo apt install libhidapi-dev libudev-dev

# For Fedora
sudo dnf install hidapi-devel

# For Arch Linux
sudo pacman -S hidapi

# For macOS
brew install hidapi
```

## Usage

```bash
# Send a message to your keyboard
qmk_notifier "your_message_here"

# List all connected HID devices
qmk_notifier --list

# Send a message with a specific vendor/product ID
qmk_notifier --vendor-id 0xFEED --product-id 0x6060 "your_message_here"

# Enable verbose output
qmk_notifier --verbose "your_message_here"
```

## Command Line Options

| Option | Short | Description |
|--------|-------|-------------|
| `message` | | Message to send to keyboard (positional argument) |
| `--vendor-id` | `-i` | USB vendor ID in decimal or hex format (e.g., `1234` or `0x04D9`) |
| `--product-id` | `-p` | USB product ID in decimal or hex format (e.g., `5678` or `0x0141`) |
| `--verbose` | `-v` | Enable verbose output for debugging |
| `--list` | `-l` | List all available HID devices |
| `--help` | | Display help information |

## Default Values

- Vendor ID: `0xFEED`
- Product ID: `0x0000`
- Messages are automatically terminated with ETX (End of Text, `0x03`) character

## Technical Details

- I wrote this in Rust for maximum performance and reliability
- Uses the `hidapi` crate for cross-platform HID communication
- Automatically batches messages larger than the 32-byte report size
- Includes proper error handling and device detection
- Configurable for any QMK keyboard with Raw HID support

## Integration with QMK Keyboards

This tool is designed to work with my [qmk-notifier](https://github.com/dabstractor/qmk-notifier) QMK module. For setup instructions, refer to the qmk-notifier README.

To use this with QMK, your keyboard firmware must:
1. Have Raw HID enabled ([QMK Raw HID Documentation](https://docs.qmk.fm/#/feature_rawhid))
2. Include the qmk-notifier module

## Example Use Cases

- Automatically change keyboard layers based on active application
- Toggle features like Vim mode when specific applications gain focus
- Create application-specific macros and shortcuts
- Set up dynamic key remapping based on context

## Why Rust?

I chose Rust for this project to ensure:
- Maximum performance with minimal overhead
- Memory safety without garbage collection
- Cross-platform compatibility
- Reliable and predictable behavior

## Related Projects

- **[qmk-notifier](https://github.com/dabstractor/qmk-notifier)**: My QMK module that receives and processes the commands from this tool
- **[hyprland-qmk-window-notifier](https://github.com/dabstractor/hyprland-qmk-window-notifier)**: My Wayland companion tool for automatic application detection
- **[zigotica/active-app-qmk-layer-updater](https://github.com/zigotica/active-app-qmk-layer-updater)**: Similar tool for Windows, macOS, and X11 environments

## Contributing

Feel free to submit issues or pull requests on GitHub.

## License

[MIT License](LICENSE)
