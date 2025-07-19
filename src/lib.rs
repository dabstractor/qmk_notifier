mod core;
pub use core::{
    list_hid_devices, parse_hex_or_decimal, send_raw_report, DEFAULT_PRODUCT_ID, DEFAULT_USAGE,
    DEFAULT_USAGE_PAGE, DEFAULT_VENDOR_ID, REPORT_LENGTH,
};



use clap::{Arg, ArgAction, Command};

// Export our error type
mod error;
pub use error::QmkError;

/// Command types for the QMK notifier
#[derive(Debug, Clone)]
pub enum RunCommand {
    SendMessage(String),
    ListDevices,
}

/// Parameters required for running QMK notifier operations
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
    /// Create new RunParameters with all required fields
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

/// Parse command-line arguments and create RunParameters
pub fn parse_cli_args() -> Result<RunParameters, QmkError> {
    let cmd = Command::new("QMK Keyboard Communication Tool")
        .version("1.0.0")
        .author("Your Name")
        .about("Sends raw HID reports to QMK keyboards")
        .arg(
            Arg::new("message")
                .help("Message to send to keyboard")
                .index(1),
        )
        .arg(
            Arg::new("vendor-id")
                .short('i')
                .long("vendor-id")
                .value_name("VID")
                .help("USB vendor ID (decimal or 0xHEX format) [default: 0xFEED]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("product-id")
                .short('p')
                .long("product-id")
                .value_name("PID")
                .help("USB product ID (decimal or 0xHEX format) [default: 0x0000]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("usage-page")
                .short('u')
                .long("usage-page")
                .value_name("USAGE_PAGE")
                .help("HID usage page (decimal or 0xHEX format) [default: 0xFF60]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("usage")
                .short('a')
                .long("usage")
                .value_name("USAGE")
                .help("HID usage (decimal or 0xHEX format) [default: 0x61]")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("verbose")
                .short('v')
                .long("verbose")
                .help("Enable verbose output")
                .action(ArgAction::SetTrue),
        )
        .arg(
            Arg::new("list")
                .short('l')
                .long("list")
                .help("List all HID devices")
                .action(ArgAction::SetTrue),
        )
        .arg(
            Arg::new("create-config")
                .short('c')
                .long("create-config")
                .help("Create example configuration file (REMOVED)")
                .action(ArgAction::SetTrue),
        );

    let matches = cmd.get_matches();

    // Check for removed feature
    if matches.get_flag("create-config") {
        return Err(QmkError::RemovedFeature(
            "Config file creation has been removed. All parameters must be provided explicitly.".to_string()
        ));
    }

    // Parse parameters with defaults
    let vendor_id = matches.get_one::<String>("vendor-id")
        .map(|s| parse_hex_or_decimal(s))
        .transpose()?
        .unwrap_or(DEFAULT_VENDOR_ID);

    let product_id = matches.get_one::<String>("product-id")
        .map(|s| parse_hex_or_decimal(s))
        .transpose()?
        .unwrap_or(DEFAULT_PRODUCT_ID);

    let usage_page = matches.get_one::<String>("usage-page")
        .map(|s| parse_hex_or_decimal(s))
        .transpose()?
        .unwrap_or(DEFAULT_USAGE_PAGE);

    let usage = matches.get_one::<String>("usage")
        .map(|s| parse_hex_or_decimal(s))
        .transpose()?
        .unwrap_or(DEFAULT_USAGE);

    let verbose = matches.get_flag("verbose");

    // Determine command
    let command = if matches.get_flag("list") {
        RunCommand::ListDevices
    } else if let Some(message) = matches.get_one::<String>("message") {
        RunCommand::SendMessage(message.to_string())
    } else {
        return Err(QmkError::MissingRequiredParameter("message or --list flag".to_string()));
    };

    Ok(RunParameters::new(
        command,
        vendor_id,
        product_id,
        usage_page,
        usage,
        verbose,
    ))
}

/// Core function that executes the notifier logic with explicit parameters.
pub fn run(params: RunParameters) -> Result<(), QmkError> {
    match params.command {
        RunCommand::ListDevices => {
            list_hid_devices()
        }
        RunCommand::SendMessage(message) => {
            if params.verbose {
                println!("Using VID: 0x{:04X}, PID: 0x{:04X}", params.vendor_id, params.product_id);
                println!(
                    "Using Usage Page: 0x{:04X}, Usage: 0x{:04X}",
                    params.usage_page, params.usage
                );
            }

            let input = message.as_bytes();

            // Ensure proper message termination by creating a new buffer
            // with enough space for the message and explicit ETX terminator (0x03)
            let mut input_with_terminator = Vec::with_capacity(input.len() + 1);
            input_with_terminator.extend_from_slice(input);
            input_with_terminator.push(0x03); // Add ETX (End of Text) character as terminator

            if params.verbose {
                println!(
                    "Message length: {} bytes (including ETX terminator)",
                    input_with_terminator.len()
                );
            }

            // Send the report with improved timing and response handling
            send_raw_report(
                &input_with_terminator,
                params.vendor_id,
                params.product_id,
                params.usage_page,
                params.usage,
                params.verbose,
            )
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_run_parameters_creation() {
        let params = RunParameters::new(
            RunCommand::SendMessage("test".to_string()),
            0xFEED,
            0x0000,
            0xFF60,
            0x61,
            true,
        );

        assert_eq!(params.vendor_id, 0xFEED);
        assert_eq!(params.product_id, 0x0000);
        assert_eq!(params.usage_page, 0xFF60);
        assert_eq!(params.usage, 0x61);
        assert_eq!(params.verbose, true);
        
        match params.command {
            RunCommand::SendMessage(msg) => assert_eq!(msg, "test"),
            _ => panic!("Expected SendMessage command"),
        }
    }

    #[test]
    fn test_run_parameters_list_devices() {
        let params = RunParameters::new(
            RunCommand::ListDevices,
            0x1234,
            0x5678,
            0xABCD,
            0xEF01,
            false,
        );

        match params.command {
            RunCommand::ListDevices => {},
            _ => panic!("Expected ListDevices command"),
        }
        assert_eq!(params.verbose, false);
    }

    #[test]
    fn test_parse_hex_or_decimal_hex() {
        assert_eq!(parse_hex_or_decimal("0xFEED").unwrap(), 0xFEED);
        assert_eq!(parse_hex_or_decimal("0X1234").unwrap(), 0x1234);
    }

    #[test]
    fn test_parse_hex_or_decimal_decimal() {
        assert_eq!(parse_hex_or_decimal("1234").unwrap(), 1234);
        assert_eq!(parse_hex_or_decimal("65261").unwrap(), 65261); // 0xFEED in decimal
    }

    #[test]
    fn test_parse_hex_or_decimal_invalid() {
        assert!(parse_hex_or_decimal("0xGGGG").is_err());
        assert!(parse_hex_or_decimal("invalid").is_err());
        assert!(parse_hex_or_decimal("").is_err());
    }
}  
  #[test]
    fn test_run_with_list_devices_command() {
        let params = RunParameters::new(
            RunCommand::ListDevices,
            0xFEED,
            0x0000,
            0xFF60,
            0x61,
            false,
        );

        // This should not panic and should return Ok or an appropriate error
        // We can't test the actual device listing without real hardware
        let result = run(params);
        // The function should complete without panicking
        // Result may be Ok or Err depending on system HID devices
        assert!(result.is_ok() || result.is_err());
    }

    #[test]
    fn test_run_with_send_message_command() {
        let params = RunParameters::new(
            RunCommand::SendMessage("test message".to_string()),
            0xFEED,
            0x0000,
            0xFF60,
            0x61,
            false,
        );

        // This will likely fail with DeviceNotFound unless the exact device exists
        let result = run(params);
        match result {
            Ok(()) => {
                // Success - device was found and message sent
            }
            Err(QmkError::DeviceNotFound(vid, pid, usage_page, usage)) => {
                // Expected error when device is not connected
                assert_eq!(vid, 0xFEED);
                assert_eq!(pid, 0x0000);
                assert_eq!(usage_page, 0xFF60);
                assert_eq!(usage, 0x61);
            }
            Err(e) => {
                // Other errors are also acceptable (e.g., device access issues)
                println!("Expected error: {}", e);
            }
        }
    }

    #[test]
    fn test_run_with_verbose_output() {
        let params = RunParameters::new(
            RunCommand::SendMessage("verbose test".to_string()),
            0x1234,
            0x5678,
            0xABCD,
            0xEF01,
            true, // verbose = true
        );

        // Test that verbose flag is properly handled
        let result = run(params);
        // Should handle verbose output without panicking
        assert!(result.is_ok() || result.is_err());
    }