# Design Document

## Overview

This design removes all configuration file handling from the QMK notifier package, transforming it into a parameter-driven library that can be called programmatically or via command-line with explicit parameters. The design maintains backward compatibility for core HID functions while eliminating config file dependencies.

## Architecture

### Current Architecture
```
CLI Args -> Config File -> Defaults -> Core Functions
```

### New Architecture
```
CLI Args -> Parameter Validation -> Core Functions
Programmatic Calls -> Parameter Validation -> Core Functions
```

The new architecture removes the config file layer entirely, requiring all parameters to be explicitly provided through either CLI arguments or programmatic function calls.

## Components and Interfaces

### Modified Components

#### 1. Main Run Function
- **Current**: `pub fn run(command: Option<String>) -> Result<(), QmkError>`
- **New**: `pub fn run(params: RunParameters) -> Result<(), QmkError>`

#### 2. New Parameter Structure
```rust
#[derive(Debug, Clone)]
pub struct RunParameters {
    pub command: RunCommand,
    pub vendor_id: u16,
    pub product_id: u16,
    pub usage_page: u16,
    pub usage: u16,
    pub verbose: bool,
}

#[derive(Debug, Clone)]
pub enum RunCommand {
    SendMessage(String),
    ListDevices,
}
```

#### 3. CLI Interface
- All parameters become required CLI arguments (no defaults from config)
- Remove `--create-config` flag entirely
- Maintain existing parameter names and formats

### Removed Components

#### 1. Config Module (`src/config.rs`)
- Remove entire `Config` struct
- Remove `Config::load()`, `Config::from_file()` methods
- Remove `create_example_config()` function
- Remove `get_config_path()` function

#### 2. Config-Related Error Types
- Remove `ConfigError`, `ConfigReadError`, `ConfigParseError`, `ConfigWriteError`
- Keep parameter validation errors (`InvalidHexValue`, `InvalidDecimalValue`)

### Unchanged Components

#### 1. Core HID Functions
- `send_raw_report()` - No changes to signature or behavior
- `list_hid_devices()` - No changes
- `get_raw_hid_interface()` - No changes
- `parse_hex_or_decimal()` - No changes

#### 2. Default Constants
- Keep all `DEFAULT_*` constants in `core.rs` for reference
- These become fallback values only when parameters are not provided

## Data Models

### RunParameters Structure
```rust
#[derive(Debug, Clone)]
pub struct RunParameters {
    pub command: RunCommand,
    pub vendor_id: u16,
    pub product_id: u16,
    pub usage_page: u16,
    pub usage: u16,
    pub verbose: bool,
}

impl RunParameters {
    pub fn new(
        command: RunCommand,
        vendor_id: u16,
        product_id: u16,
        usage_page: u16,
        usage: u16,
        verbose: bool,
    ) -> Self {
        Self {
            command,
            vendor_id,
            product_id,
            usage_page,
            usage,
            verbose,
        }
    }
}
```

### RunCommand Enum
```rust
#[derive(Debug, Clone)]
pub enum RunCommand {
    SendMessage(String),
    ListDevices,
}
```

## Error Handling

### Removed Error Types
- `ConfigError`
- `ConfigReadError` 
- `ConfigParseError`
- `ConfigWriteError`

### Enhanced Parameter Validation
- Improve error messages for missing required parameters
- Maintain existing hex/decimal parsing error handling
- Add specific error for removed `--create-config` functionality

### CLI Error Handling
```rust
// When required parameters are missing
QmkError::MissingRequiredParameter(String) // New error type

// When --create-config is used
QmkError::RemovedFeature(String) // New error type
```

## Testing Strategy

### Unit Tests
1. **Parameter Validation Tests**
   - Test `RunParameters::new()` with valid parameters
   - Test parameter validation edge cases
   - Test hex/decimal parsing with various inputs

2. **CLI Argument Tests**
   - Test CLI parsing with all required parameters
   - Test CLI parsing with missing parameters
   - Test error messages for invalid parameters

3. **Core Function Tests**
   - Verify `send_raw_report()` behavior unchanged
   - Verify `list_hid_devices()` behavior unchanged
   - Verify `parse_hex_or_decimal()` behavior unchanged

### Integration Tests
1. **Programmatic Usage Tests**
   - Test calling `run()` with `RunParameters`
   - Test various parameter combinations
   - Test error handling for invalid parameters

2. **CLI Usage Tests**
   - Test command-line execution with all parameters
   - Test help output shows required parameters
   - Test error messages for missing parameters

### Backward Compatibility Tests
1. **Core Function Compatibility**
   - Verify existing HID communication works identically
   - Test with real HID devices if available
   - Verify error handling remains consistent

## Implementation Notes

### Migration Strategy
1. Create new `RunParameters` struct and `RunCommand` enum
2. Modify `run()` function signature to accept `RunParameters`
3. Update CLI parsing to require all parameters
4. Remove config module entirely
5. Update error types and remove config-related errors
6. Update documentation and help text

### CLI Backward Compatibility
- Parameter names and formats remain identical
- Only change is that parameters become required instead of optional
- Help text updated to reflect required nature of parameters

### Default Value Handling
- Default constants remain in `core.rs` for reference
- No automatic fallback to defaults - all parameters must be explicit
- Documentation can reference default values for user convenience