mod core;
pub use core::{
    list_hid_devices, parse_hex_or_decimal, send_raw_report, DEFAULT_PRODUCT_ID, DEFAULT_VENDOR_ID,
    REPORT_LENGTH,
};

use clap::{Arg, ArgAction, Command};

// Export our error type
mod error;
pub use error::QmkError;

/// Core function that executes the notifier logic.
/// It can be called either with Some(command) for programmatic use,
/// or with None to parse command-line arguments.
pub fn run(command: Option<String>) -> Result<(), QmkError> {
    let args: Vec<String> = if command.is_some() {
        // If command is provided, create minimal args vector with just the command
        vec!["qmk_notifiertool".to_string(), command.unwrap()]
    } else {
        // Otherwise, collect command line arguments
        let mut args: Vec<String> = std::env::args().collect();
        if args.len() <= 1 {
            args.push("--help".to_string());
        }
        args
    };

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
                .help("USB vendor ID (decimal or 0xHEX format)")
                .value_parser(clap::value_parser!(String)),
        )
        .arg(
            Arg::new("product-id")
                .short('p')
                .long("product-id")
                .value_name("PID")
                .help("USB product ID (decimal or 0xHEX format)")
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
        );

    // Clone for help display if needed
    let mut cmd_for_help = cmd.clone();

    let matches = cmd.get_matches_from(args);

    if matches.get_flag("list") {
        return list_hid_devices();
    }

    let vendor_id = matches
        .get_one::<String>("vendor-id")
        .map(|vid| parse_hex_or_decimal(vid))
        .transpose()?
        .unwrap_or(DEFAULT_VENDOR_ID);

    let product_id = matches
        .get_one::<String>("product-id")
        .map(|pid| parse_hex_or_decimal(pid))
        .transpose()?
        .unwrap_or(DEFAULT_PRODUCT_ID);

    let verbose = matches.get_flag("verbose");

    let message = if let Some(msg) = matches.get_one::<String>("message") {
        msg.to_string()
    } else {
        cmd_for_help.print_help().expect("Failed to display help");
        println!();
        return Ok(());
    };

    if verbose {
        println!("Using VID: 0x{:04X}, PID: 0x{:04X}", vendor_id, product_id);
    }

    let input = message.as_bytes();

    // Ensure proper message termination by creating a new buffer
    // with enough space for the message and explicit ETX terminator (0x03)
    let mut input_with_terminator = Vec::with_capacity(input.len() + 1);
    input_with_terminator.extend_from_slice(input);
    input_with_terminator.push(0x03); // Add ETX (End of Text) character as terminator

    if verbose {
        println!(
            "Message length: {} bytes (including ETX terminator)",
            input_with_terminator.len()
        );
    }

    // Send the report with improved timing and response handling
    send_raw_report(&input_with_terminator, vendor_id, product_id, verbose)
}
