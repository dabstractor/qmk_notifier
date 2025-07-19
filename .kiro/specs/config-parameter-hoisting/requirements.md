# Requirements Document

## Introduction

This feature removes the configuration file dependency from the QMK notifier package, requiring all parameters to be explicitly passed either programmatically or via command-line arguments. This ensures the package can be reliably called by other packages with explicit parameters while maintaining command-line usability.

## Requirements

### Requirement 1

**User Story:** As a developer using this package as a dependency, I want to call the package functions with explicit parameters, so that I don't rely on external config files and have predictable behavior.

#### Acceptance Criteria

1. WHEN the package is called programmatically THEN the system SHALL accept all required parameters as function arguments
2. WHEN no config file exists THEN the system SHALL still function correctly with provided parameters
3. WHEN parameters are provided programmatically THEN the system SHALL NOT attempt to read any config files
4. WHEN parameters are not provided THEN the system SHALL use QMK default values:
   - Vendor ID: `0xFEED` (65261 decimal)
   - Product ID: `0x0000` (0 decimal)
   - Usage Page: `0xFF60` (65376 decimal)
   - Usage: `0x61` (97 decimal)

### Requirement 2

**User Story:** As a command-line user, I want to provide all necessary parameters via CLI arguments, so that I can use the tool without managing config files.

#### Acceptance Criteria

1. WHEN all required parameters are provided via CLI THEN the package SHALL execute successfully
2. WHEN required parameters are missing from CLI THEN the package SHALL display helpful error messages indicating which parameters are required
3. WHEN the --help flag is used THEN the package SHALL clearly show all required parameters and their formats

### Requirement 3

**User Story:** As a developer, I want the package to have no config file dependencies, so that deployment and usage is simplified across different environments.

#### Acceptance Criteria

1. WHEN the package is invoked THEN it SHALL NOT attempt to read any config files
2. WHEN the package is invoked THEN it SHALL NOT create any config directories or files
3. WHEN the --create-config flag is used THEN the package SHALL return an error indicating this functionality has been removed

### Requirement 4

**User Story:** As a developer, I want clear parameter validation and error messages, so that I can quickly identify and fix parameter issues.

#### Acceptance Criteria

1. WHEN invalid hex values are provided THEN the package SHALL return specific error messages about the invalid hex format
2. WHEN invalid decimal values are provided THEN the package SHALL return specific error messages about the invalid decimal format
3. WHEN required parameters are missing THEN the package SHALL list exactly which parameters are required

### Requirement 5

**User Story:** As a developer maintaining backward compatibility, I want the core HID communication functions to remain unchanged, so that existing functionality continues to work.

#### Acceptance Criteria

1. WHEN send_raw_report is called with parameters THEN it SHALL function identically to the current implementation
2. WHEN list_hid_devices is called THEN it SHALL function identically to the current implementation
3. WHEN parse_hex_or_decimal is called THEN it SHALL function identically to the current implementation