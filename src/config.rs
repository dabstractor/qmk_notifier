use crate::error::QmkError;
use serde::Deserialize;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Deserialize)]
pub struct Config {
    #[serde(default = "default_vendor_id")]
    pub vendor_id: u16,

    #[serde(default = "default_product_id")]
    pub product_id: u16,

    #[serde(default = "default_usage_page")]
    pub usage_page: u16,

    #[serde(default = "default_usage")]
    pub usage: u16,

    #[serde(default)]
    pub verbose: bool,
}

fn default_vendor_id() -> u16 {
    crate::core::DEFAULT_VENDOR_ID
}

fn default_product_id() -> u16 {
    crate::core::DEFAULT_PRODUCT_ID
}

fn default_usage_page() -> u16 {
    crate::core::DEFAULT_USAGE_PAGE
}

fn default_usage() -> u16 {
    crate::core::DEFAULT_USAGE
}

impl Default for Config {
    fn default() -> Self {
        Self {
            vendor_id: default_vendor_id(),
            product_id: default_product_id(),
            usage_page: default_usage_page(),
            usage: default_usage(),
            verbose: false,
        }
    }
}

impl Config {
    pub fn from_file(path: &Path) -> Result<Self, QmkError> {
        let content = fs::read_to_string(path)
            .map_err(|e| QmkError::ConfigReadError(path.display().to_string(), e.to_string()))?;

        let config: Config = toml::from_str(&content)
            .map_err(|e| QmkError::ConfigParseError(path.display().to_string(), e.to_string()))?;

        Ok(config)
    }

    pub fn load() -> Self {
        if let Some(config_path) = get_config_path(false) {
            if config_path.exists() {
                match Config::from_file(&config_path) {
                    Ok(config) => return config,
                    Err(e) => {
                        eprintln!("Warning: Failed to load config file: {}", e);
                        // Fall through to default
                    }
                }
            }
        }

        Config::default()
    }
}

fn get_config_path(create_dirs: bool) -> Option<PathBuf> {
    let config_dir = dirs::config_dir()?;
    let qmk_config_dir = config_dir.join("qmk-notifier");

    // Only create the directory if explicitly requested
    if create_dirs && !qmk_config_dir.exists() {
        if let Err(e) = fs::create_dir_all(&qmk_config_dir) {
            eprintln!("Warning: Could not create config directory: {}", e);
            return None;
        }
    }

    Some(qmk_config_dir.join("config.toml"))
}

pub fn create_example_config() -> Result<PathBuf, QmkError> {
    let config_path = get_config_path(true)
        .ok_or_else(|| QmkError::ConfigError("Could not determine config directory".to_string()))?;

    if !config_path.exists() {
        let example_config = format!(
            r#"# QMK Notifier Configuration

# USB Vendor ID (default: 0x{:04X})
vendor_id = 0x{:04X}

# USB Product ID (default: 0x{:04X})
product_id = 0x{:04X}

# HID Usage Page (default: 0x{:04X})
usage_page = 0x{:04X}

# HID Usage (default: 0x{:04X})
usage = 0x{:04X}

# Enable verbose output (default: false)
verbose = false
"#,
            crate::core::DEFAULT_VENDOR_ID,
            crate::core::DEFAULT_VENDOR_ID,
            crate::core::DEFAULT_PRODUCT_ID,
            crate::core::DEFAULT_PRODUCT_ID,
            crate::core::DEFAULT_USAGE_PAGE,
            crate::core::DEFAULT_USAGE_PAGE,
            crate::core::DEFAULT_USAGE,
            crate::core::DEFAULT_USAGE
        );

        let parent = config_path.parent().unwrap();

        println!("Creating config directory at: {}", parent.display());

        if !parent.exists() {
            fs::create_dir_all(parent).map_err(|e| {
                QmkError::ConfigError(format!("Failed to create config directory: {}", e))
            })?;
        }

        fs::write(&config_path, example_config).map_err(|e| {
            QmkError::ConfigWriteError(config_path.display().to_string(), e.to_string())
        })?;

        println!(
            "Created example configuration at: {}",
            config_path.display()
        );
    } else {
        println!("Configuration already exists at: {}", config_path.display());
    }

    Ok(config_path)
}
