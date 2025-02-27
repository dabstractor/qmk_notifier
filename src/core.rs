use hidapi::{HidApi, HidDevice, HidError};
//
// Default constants
pub const DEFAULT_VENDOR_ID: u16 = 0xFEED;
pub const DEFAULT_PRODUCT_ID: u16 = 0x0000;
const USAGE_PAGE: u16 = 0xFF60;
const USAGE: u16 = 0x61;
pub const REPORT_LENGTH: usize = 32;
use std::process;

pub fn parse_hex_or_decimal(input: &str) -> Result<u16, String> {
    if input.starts_with("0x") || input.starts_with("0X") {
        // Parse as hexadecimal
        u16::from_str_radix(&input[2..], 16).map_err(|e| format!("Invalid hex value: {}", e))
    } else {
        // Parse as decimal
        input
            .parse::<u16>()
            .map_err(|e| format!("Invalid decimal value: {}", e))
    }
}

pub fn list_hid_devices() {
    let api = match HidApi::new() {
        Ok(api) => api,
        Err(e) => {
            eprintln!("Error initializing HID API: {}", e);
            process::exit(1);
        }
    };

    println!("Available HID devices:");
    for device in api.device_list() {
        println!(
            "VID: 0x{:04X}, PID: 0x{:04X}, Usage Page: 0x{:04X}, Usage: 0x{:04X}, Path: {:?}",
            device.vendor_id(),
            device.product_id(),
            device.usage_page(),
            device.usage(),
            device.path()
        );

        // Try to get the product and manufacturer strings
        match device.open_device(&api) {
            Ok(opened_device) => {
                if let Ok(Some(manufacturer)) = opened_device.get_manufacturer_string() {
                    println!("  Manufacturer: {}", manufacturer);
                }
                if let Ok(Some(product)) = opened_device.get_product_string() {
                    println!("  Product: {}", product);
                }
            }
            Err(_) => {
                println!("  (Unable to open device for more details)");
            }
        }
        println!();
    }
}

pub fn send_raw_report(
    data: &[u8],
    vendor_id: u16,
    product_id: u16,
    verbose: bool,
) -> Result<(), HidError> {
    let interface = match get_raw_hid_interface(vendor_id, product_id) {
        Some(interface) => interface,
        None => {
            eprintln!(
                "No device found with VID: 0x{:04X}, PID: 0x{:04X}",
                vendor_id, product_id
            );
            process::exit(1);
        }
    };

    let mut request_data = vec![0u8; REPORT_LENGTH + 1]; // First byte is Report ID
    for (i, &byte) in data.iter().enumerate().take(REPORT_LENGTH) {
        request_data[i + 1] = byte;
    }

    if verbose {
        println!("Request data:");
        println!("{:?}", request_data);
    }

    match interface.write(&request_data) {
        Ok(_) => Ok(()),
        Err(e) => Err(e),
    }
}

fn get_raw_hid_interface(vendor_id: u16, product_id: u16) -> Option<HidDevice> {
    let api = match HidApi::new() {
        Ok(api) => api,
        Err(e) => {
            eprintln!("Error initializing HID API: {}", e);
            return None;
        }
    };

    // Use iter() to create an iterator over the device list
    let raw_hid_interface = api.device_list().find(|d| {
        d.vendor_id() == vendor_id
            && d.product_id() == product_id
            && d.usage_page() == USAGE_PAGE
            && d.usage() == USAGE
    });

    match raw_hid_interface {
        Some(interface_info) => match interface_info.open_device(&api) {
            Ok(device) => Some(device),
            Err(e) => {
                eprintln!("Error opening device: {}", e);
                None
            }
        },
        None => None,
    }
}
