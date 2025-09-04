use hidapi::HidError;
use std::fmt;

#[derive(Debug)]
pub enum QmkError {
    HidApiInitError(String),
    DeviceNotFound(u16, u16, u16, u16),
    DeviceOpenError(String),
    InvalidHexValue(String),
    InvalidDecimalValue(String),
    SendReportError(HidError),
    HidReadError(String),
    NoResponseReceived(String),
    MissingRequiredParameter(String),
    RemovedFeature(String),
    PartialSendError {
        succeeded: usize,
        failed: usize,
    },
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
            QmkError::SendReportError(e) => write!(f, "Error sending report: {}", e),
            QmkError::HidReadError(e) => write!(f, "Error reading report: {}", e),
            QmkError::NoResponseReceived(e) => write!(f, "No response received: {}", e),
            QmkError::MissingRequiredParameter(param) => write!(f, "Missing required parameter: {}", param),
            QmkError::RemovedFeature(feature) => write!(f, "Feature removed: {}", feature),
            QmkError::PartialSendError { succeeded, failed } => {
                write!(
                    f,
                    "Message sent to {} devices, but failed for {} devices.",
                    succeeded, failed
                )
            }
        }
    }
}

impl std::error::Error for QmkError {}

impl From<HidError> for QmkError {
    fn from(error: HidError) -> Self {
        QmkError::SendReportError(error)
    }
}
