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
        u16::from_str_radix(&input[2..], 16).map_err(|e| QmkError::InvalidHexValue(e.to_string()))
    } else {
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
    let interfaces = get_raw_hid_interfaces(vendor_id, product_id, usage_page, usage)?;
    let mut successful_sends = 0;

    if verbose {
        println!("Found {} matching devices.", interfaces.len());
    }

    for (device_idx, interface) in interfaces.iter().enumerate() {
        if verbose {
            let device_path = match interface.get_device_info() {
                Ok(info) => format!("{:?}", info.path()),
                Err(_) => "N/A".to_string(),
            };
            println!(
                "Sending to device {}/{}: Path: {}",
                device_idx + 1,
                interfaces.len(),
                device_path
            );
        }

        let batch_count = (data.len() + REPORT_LENGTH - 3) / (REPORT_LENGTH - 2);

        if verbose {
            println!("Request data ({} bytes):", data.len());
            println!("{:?}", data);
        }

        let mut batch_errors = Vec::new();

        for batch in 0..batch_count {
            let start_idx = batch * (REPORT_LENGTH - 2);
            let end_idx = (start_idx + (REPORT_LENGTH - 2)).min(data.len());
            let batch_data = &data[start_idx..end_idx];

            let mut request_data = vec![0u8; REPORT_LENGTH + 1];
            request_data[1] = 0x81;
            request_data[2] = 0x9F;

            if !batch_data.is_empty() {
                request_data[3..3 + batch_data.len()].copy_from_slice(batch_data);
            }

            if verbose {
                println!("Sending batch {}/{}", batch + 1, batch_count);
                println!("{:?}", request_data);
            }

            if let Err(e) = interface.write(&request_data) {
                let error_msg = format!("Error on batch {}: {}", batch + 1, e);
                batch_errors.push(error_msg);
                if verbose {
                    println!("{}", e);
                }
                break; 
            }

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
                        println!("No response for batch {}: {}", batch + 1, e);
                    }
                }
            }
        }

        if batch_errors.is_empty() {
            successful_sends += 1;
        } else {
            if verbose {
                println!("Failed to send message to a device: {:?}", batch_errors);
            }
        }
    }

    if successful_sends == 0 && !interfaces.is_empty() {
        Err(QmkError::SendReportError(
            hidapi::HidError::HidApiError { message: "Failed to send to any devices".to_string() }
        ))
    } else if successful_sends < interfaces.len() {
        Err(QmkError::PartialSendError {
            succeeded: successful_sends,
            failed: interfaces.len() - successful_sends,
        })
    } else {
        Ok(())
    }
}


fn get_raw_hid_interfaces(
    vendor_id: u16,
    product_id: u16,
    usage_page: u16,
    usage: u16,
) -> Result<Vec<HidDevice>, QmkError> {
    let api = HidApi::new().map_err(|e| QmkError::HidApiInitError(e.to_string()))?;

    let device_infos: Vec<_> = api
        .device_list()
        .filter(|d| {
            d.vendor_id() == vendor_id
                && d.product_id() == product_id
                && d.usage_page() == usage_page
                && d.usage() == usage
        })
        .collect();

    // Debug output to see what devices match
    println!("Searching for devices with VID: 0x{:04X}, PID: 0x{:04X}, Usage Page: 0x{:04X}, Usage: 0x{:04X}", 
             vendor_id, product_id, usage_page, usage);
    println!("Found {} matching device interfaces:", device_infos.len());
    for (i, d) in device_infos.iter().enumerate() {
        println!("  {}. Path: {:?}, VID: 0x{:04X}, PID: 0x{:04X}, Usage Page: 0x{:04X}, Usage: 0x{:04X}", 
                 i+1, d.path(), d.vendor_id(), d.product_id(), d.usage_page(), d.usage());
    }

    if device_infos.is_empty() {
        return Err(QmkError::DeviceNotFound(
            vendor_id, product_id, usage_page, usage,
        ));
    }

    let opened_devices: Vec<HidDevice> = device_infos
        .into_iter()
        .filter_map(|info| info.open_device(&api).ok())
        .collect();

    if opened_devices.is_empty() {
        return Err(QmkError::DeviceOpenError(
            "Found matching HID devices, but could not open any of them for communication. Check permissions (udev rules on Linux)."
                .to_string(),
        ));
    }

    Ok(opened_devices)
}
