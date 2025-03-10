use hidapi::HidError;
use std::fmt;

#[derive(Debug)]
pub enum QmkError {
    HidApiInitError(String),
    DeviceNotFound(u16, u16, u16, u16),
    DeviceOpenError(String),
    InvalidHexValue(String),
    InvalidDecimalValue(String),
    InputTooLong(usize, usize),
    SendReportError(HidError),
    ConfigError(String),
    ConfigReadError(String, String),
    ConfigParseError(String, String),
    ConfigWriteError(String, String),
}

impl fmt::Display for QmkError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            QmkError::HidApiInitError(e) => write!(f, "Error initializing HID API: {}", e),
            QmkError::DeviceNotFound(vid, pid, usage_page, usage) => write!(
                f,
                "No device found with VID: 0x{:04X}, PID: 0x{:04X}, Usage Page: 0x{:04X}, Usage: 0x{:04X}",
                vid, pid, usage_page, usage
            ),
            QmkError::DeviceOpenError(e) => write!(f, "Error opening device: {}", e),
            QmkError::InvalidHexValue(e) => write!(f, "Invalid hex value: {}", e),
            QmkError::InvalidDecimalValue(e) => write!(f, "Invalid decimal value: {}", e),
            QmkError::InputTooLong(input, max) => write!(
                f,
                "Input string exceeds maximum length of {} bytes (got {} bytes)",
                max, input
            ),
            QmkError::SendReportError(e) => write!(f, "Error sending report: {}", e),
            QmkError::ConfigError(e) => write!(f, "Configuration error: {}", e),
            QmkError::ConfigReadError(path, e) => write!(f, "Error reading config file {}: {}", path, e),
            QmkError::ConfigParseError(path, e) => write!(f, "Error parsing config file {}: {}", path, e),
            QmkError::ConfigWriteError(path, e) => write!(f, "Error writing config file {}: {}", path, e),
        }
    }
}

impl std::error::Error for QmkError {}

impl From<HidError> for QmkError {
    fn from(error: HidError) -> Self {
        QmkError::SendReportError(error)
    }
}
