use crate::error::QmkError;
use hidapi::{HidApi, HidDevice};

// Default constants
pub const DEFAULT_VENDOR_ID: u16 = 0xFEED;
pub const DEFAULT_PRODUCT_ID: u16 = 0x0000;
pub const DEFAULT_USAGE_PAGE: u16 = 0xFF60;
pub const DEFAULT_USAGE: u16 = 0x61;
pub const REPORT_LENGTH: usize = 32;

pub fn parse_hex_or_decimal(input: &str) -> Result<u16, QmkError> {
    if input.starts_with("0x") || input.starts_with("0X") {
        // Parse as hexadecimal
        u16::from_str_radix(&input[2..], 16).map_err(|e| QmkError::InvalidHexValue(e.to_string()))
    } else {
        // Parse as decimal
        input
            .parse::<u16>()
            .map_err(|e| QmkError::InvalidDecimalValue(e.to_string()))
    }
}

pub fn list_hid_devices() -> Result<(), QmkError> {
    let api = HidApi::new().map_err(|e| QmkError::HidApiInitError(e.to_string()))?;

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

    Ok(())
}

pub fn send_raw_report(
    data: &[u8],
    vendor_id: u16,
    product_id: u16,
    usage_page: u16,
    usage: u16,
    verbose: bool,
) -> Result<(), QmkError> {
    let interface = get_raw_hid_interface(vendor_id, product_id, usage_page, usage)?;

    // Calculate number of batches needed (rounded up)
    let batch_count = (data.len() + REPORT_LENGTH - 3) / (REPORT_LENGTH - 2);

    if verbose {
        println!("Request data ({} bytes):", data.len());
        println!("{:?}", data);
    }

    for batch in 0..batch_count {
        let start_idx = batch * (REPORT_LENGTH - 2);
        let end_idx = (start_idx + (REPORT_LENGTH - 2)).min(data.len());
        let batch_data = &data[start_idx..end_idx];

        let mut request_data = vec![0u8; REPORT_LENGTH + 1]; // First byte is Report ID
        request_data[1] = 0x81;
        request_data[2] = 0x9F;
        // Copy batch_data into the appropriate position
        if !batch_data.is_empty() {
            request_data[3..3 + batch_data.len()].copy_from_slice(batch_data);
        }

        if verbose {
            println!("Sending batch {}/{}", batch + 1, batch_count);
            println!("{:?}", request_data);
        }

        // Send the data
        interface.write(&request_data)?;

        // Wait for response or acknowledgment
        let mut response_buffer = vec![0u8; REPORT_LENGTH + 1];
        match interface.read_timeout(&mut response_buffer, 100) {
            Ok(size) => {
                if verbose {
                    println!("Received response ({} bytes):", size);
                    println!("{:?}", &response_buffer[..size]);
                }
            }
            Err(e) => {
                if verbose {
                    println!("No response received: {}", e);
                }
                // Continue anyway, some devices might not send responses
            }
        }
    }

    Ok(())
}

fn get_raw_hid_interface(
    vendor_id: u16,
    product_id: u16,
    usage_page: u16,
    usage: u16,
) -> Result<HidDevice, QmkError> {
    let api = HidApi::new().map_err(|e| QmkError::HidApiInitError(e.to_string()))?;

    // Use iter() to create an iterator over the device list
    let raw_hid_interface = api.device_list().find(|d| {
        d.vendor_id() == vendor_id
            && d.product_id() == product_id
            && d.usage_page() == usage_page
            && d.usage() == usage
    });

    match raw_hid_interface {
        Some(interface_info) => interface_info
            .open_device(&api)
            .map_err(|e| QmkError::DeviceOpenError(e.to_string())),
        None => Err(QmkError::DeviceNotFound(
            vendor_id, product_id, usage_page, usage,
        )),
    }
}
