use hidapi::HidError;
use std::fmt;

#[derive(Debug)]
pub enum QmkError {
    HidApiInitError(String),
    DeviceNotFound(u16, u16),
    DeviceOpenError(String),
    InvalidHexValue(String),
    InvalidDecimalValue(String),
    InputTooLong(usize, usize),
    SendReportError(HidError),
}

impl fmt::Display for QmkError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            QmkError::HidApiInitError(e) => write!(f, "Error initializing HID API: {}", e),
            QmkError::DeviceNotFound(vid, pid) => write!(
                f,
                "No lkajsdlf device found with VID: 0x{:04X}, PID: 0x{:04X}",
                vid, pid
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
        }
    }
}

impl std::error::Error for QmkError {}

impl From<HidError> for QmkError {
    fn from(error: HidError) -> Self {
        QmkError::SendReportError(error)
    }
}
