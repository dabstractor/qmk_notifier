# QMK Notifier Tool

A high-performance Rust application that sends string commands to QMK keyboards via Raw HID interface.

## Overview

I built `qmk_notifier` to communicate with QMK-powered keyboards from your desktop. It efficiently handles serializing messages and sending them in batches to overcome the 32-byte HID report size limitation, enabling seamless communication with your keyboard for dynamic layer and command management.

This crate is the **transport layer** of a three-part ecosystem (see `SPEC.md` for the full contract):
- **qmk_notifier** (this crate): Rust library + CLI that owns Raw-HID wire framing, the device cache, and burst-write (round B adds typed-command transport + reply parsing). Transport only — it does no matching.
- **[qmkonnect](https://github.com/dabstractor/qmkonnect)**: Cross-platform desktop daemon that detects the foreground window, runs the host-side matcher (`rules.toml`), and sends via this crate.
- **[qmk-notifier](https://github.com/dabstractor/qmk-notifier)**: QMK firmware module that receives, pattern-matches, and toggles layers/features. It owns the canonical wire protocol.

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

VID/PID are optional (omit them for auto-discovery by usage page/usage — the zero-config path). Only the message (or `--list`) is required:

```bash
# Send a message — auto-discover any QMK keyboard (usage page 0xFF60 / usage 0x61)
qmk_notifier "your_message_here"

# Disambiguate among multiple QMK keyboards with explicit VID/PID
qmk_notifier --vendor-id 0xFEED --product-id 0x0000 "your_message_here"

# List all connected HID devices
qmk_notifier --list

# Enable verbose output
qmk_notifier -v "your_message_here"
```

## Command Line Options

| Option | Short | Default | Description |
|--------|-------|---------|-------------|
| `message` | | — | Message to send to keyboard (positional; ETX appended automatically). |
| `--vendor-id` | `-i` | auto (`None`) | USB vendor ID, decimal or `0xHEX` (e.g. `65261` or `0xFEED`). Omit ⇒ match any. |
| `--product-id` | `-p` | auto (`None`) | USB product ID. Omit ⇒ match any. |
| `--usage-page` | `-u` | `0xFF60` | HID usage page, decimal or `0xHEX`. |
| `--usage` | `-a` | `0x61` | HID usage, decimal or `0xHEX`. |
| `--verbose` | `-v` | off | Enable verbose transport logging. |
| `--list` | `-l` | off | List all available HID devices. |
| `--help` | | | Display help information. |

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

// Send a message — auto-discover (VID/PID = None ⇒ match any QMK keyboard)
let params = RunParameters::new(
    RunCommand::SendMessage("Hello keyboard!".to_string()),
    None,              // vendor_id  (Some(0xFEED) to disambiguate)
    None,              // product_id (Some(0x0000) to disambiguate)
    0xFF60,            // usage_page
    0x61,              // usage
    false,             // verbose
);

match run(params) {
    Ok(()) => println!("Message sent successfully"),   // v0.2.x: run() returns ()
    Err(e) => eprintln!("Error: {}", e),
}
```

> Round B (v0.3.0) changes `run()` to return `Result<CommandResponse, QmkError>`
> and adds typed-command variants (`QueryInfo`, `QueryCallback`, `SetOs`,
> `ApplyHostContext`). See `SPEC.md` §10.

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
