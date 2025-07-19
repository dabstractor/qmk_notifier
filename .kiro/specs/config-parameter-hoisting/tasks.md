# Implementation Plan

- [x] 1. Create new parameter structures and enums
  - Define `RunParameters` struct with all required fields
  - Define `RunCommand` enum for different operation types
  - Implement constructors and basic methods for parameter structures
  - _Requirements: 1.1, 2.1_

- [x] 2. Update error types for parameter-driven approach
  - Remove config-related error variants from `QmkError` enum
  - Add new error variants for missing parameters and removed features
  - Update error display implementations for new error types
  - Write unit tests for new error types
  - _Requirements: 4.1, 4.2, 4.3_

- [x] 3. Modify main run function signature and logic
  - Change `run()` function signature to accept `RunParameters`
  - Remove config loading logic from run function
  - Update parameter usage to use `RunParameters` fields directly
  - Ensure core HID function calls remain unchanged
  - _Requirements: 1.1, 1.3, 5.1, 5.2, 5.3_

- [x] 4. Update CLI argument parsing to require all parameters
  - Modify clap argument definitions to make all HID parameters required
  - Remove `--create-config` argument and add error handling for it
  - Update help text to clearly indicate required parameters
  - Implement parameter validation and error reporting for missing args
  - _Requirements: 2.1, 2.2, 2.3, 3.3_

- [x] 5. Create CLI-to-parameter conversion logic
  - Implement function to convert CLI matches to `RunParameters`
  - Handle hex/decimal parsing for CLI string inputs
  - Implement proper error handling for invalid parameter formats
  - Write unit tests for CLI parameter conversion
  - _Requirements: 2.1, 4.1, 4.2_

- [x] 6. Remove config module and update exports
  - Delete `src/config.rs` file entirely
  - Remove config-related exports from `lib.rs`
  - Remove config-related dependencies from `Cargo.toml` if no longer needed
  - Update module imports throughout codebase
  - _Requirements: 3.1, 3.2_

- [x] 7. Update main.rs for new parameter structure
  - Modify `main.rs` to handle CLI parsing and create `RunParameters`
  - Implement error handling for CLI parameter parsing
  - Ensure proper error reporting to stderr
  - _Requirements: 2.1, 2.2, 4.3_

- [x] 8. Write comprehensive unit tests for parameter handling
  - Test `RunParameters` creation with valid inputs
  - Test parameter validation edge cases
  - Test CLI argument parsing with various input combinations
  - Test error handling for missing and invalid parameters
  - _Requirements: 1.1, 2.1, 4.1, 4.2, 4.3_

- [x] 9. Write integration tests for programmatic usage
  - Test calling `run()` function with `RunParameters` directly
  - Test various parameter combinations for different use cases
  - Verify core HID functionality works with new parameter structure
  - Test error propagation for programmatic calls
  - _Requirements: 1.1, 1.2, 5.1, 5.2, 5.3_

- [x] 10. Update documentation and help text
  - Update CLI help text to reflect required parameters
  - Update function documentation for new `run()` signature
  - Add examples of programmatic usage with `RunParameters`
  - Document migration path for existing users
  - _Requirements: 2.3, 4.3_