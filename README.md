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

All HID parameters are now required and must be provided explicitly:

```bash
# Send a message to your keyboard (all parameters required)
qmk_notifier --vendor-id 0xFEED --product-id 0x0000 --usage-page 0xFF60 --usage 0x61 "your_message_here"

# List all connected HID devices
qmk_notifier --vendor-id 0xFEED --product-id 0x0000 --usage-page 0xFF60 --usage 0x61 --list

# Enable verbose output
qmk_notifier --vendor-id 0xFEED --product-id 0x0000 --usage-page 0xFF60 --usage 0x61 --verbose "your_message_here"
```

## Command Line Options

| Option | Short | Required | Description |
|--------|-------|----------|-------------|
| `message` | | No* | Message to send to keyboard (positional argument) |
| `--vendor-id` | `-i` | Yes | USB vendor ID in decimal or hex format (e.g., `65261` or `0xFEED`) |
| `--product-id` | `-p` | Yes | USB product ID in decimal or hex format (e.g., `0` or `0x0000`) |
| `--usage-page` | `-u` | Yes | HID usage page in decimal or hex format (e.g., `65376` or `0xFF60`) |
| `--usage` | `-a` | Yes | HID usage in decimal or hex format (e.g., `97` or `0x61`) |
| `--verbose` | `-v` | No | Enable verbose output for debugging |
| `--list` | `-l` | No | List all available HID devices |
| `--help` | | No | Display help information |

*Either `message` or `--list` must be provided.

## Default Reference Values

These values are commonly used for QMK keyboards but must be provided explicitly:
- Vendor ID: `0xFEED` (65261 decimal)
- Product ID: `0x0000` (0 decimal)
- Usage Page: `0xFF60` (65376 decimal)
- Usage: `0x61` (97 decimal)
- Messages are automatically terminated with ETX (End of Text, `0x03`) character

## Programmatic Usage

This package can also be used as a library in other Rust projects:

```rust
use qmk_notifier::{RunParameters, RunCommand, run};

// Send a message
let params = RunParameters::new(
    RunCommand::SendMessage("Hello keyboard!".to_string()),
    0xFEED,  // vendor_id
    0x0000,  // product_id
    0xFF60,  // usage_page
    0x61,    // usage
    false    // verbose
);

match run(params) {
    Ok(()) => println!("Message sent successfully"),
    Err(e) => eprintln!("Error: {}", e),
}

// List devices
let list_params = RunParameters::new(
    RunCommand::ListDevices,
    0xFEED, 0x0000, 0xFF60, 0x61, true
);
run(list_params)?;
```

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
