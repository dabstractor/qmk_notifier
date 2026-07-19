use crate::error::QmkError;
use hidapi::{HidApi, HidDevice};
use std::sync::{LazyLock, Mutex, MutexGuard};

// Default constants
pub const DEFAULT_VENDOR_ID: u16 = 0xFEED;
pub const DEFAULT_PRODUCT_ID: u16 = 0x0000;
pub const DEFAULT_USAGE_PAGE: u16 = 0xFF60;
pub const DEFAULT_USAGE: u16 = 0x61;
pub const REPORT_LENGTH: usize = 32;

// --- Typed-command transport constants (v0.3.0) -----------------------------
// Wire vocabulary for the typed-command path (PRD §10.1, §10.2). Mirror of the
// firmware `notifier.h` #defines (canonicalized in
// `plan/001_b92a9b2b603f/architecture/firmware_wire_contract.md` §Constants).
// `pub(crate)` (not `pub`): internal transport constants, NOT public API.
//
// NOTE: each constant carries a temporary `#[allow(dead_code)]`. The consumers
// land in later subtasks — build_command_data (P1.M2.T1), parse_reply
// (P1.M2.T2), and burst_to_one's reply capture (P1.M3.T1) — so until then no
// non-test code references them and rustc would otherwise emit dead_code
// warnings (verified: even `pub(crate)` items warn, and a `#[cfg(test)]`-only
// reference does NOT silence them in `cargo build`). REMOVE each allow when its
// constant gains a real consumer.

/// Typed-command discriminator: first payload byte after 0x81 0x9F (PRD §10.1).
#[allow(dead_code)]
pub(crate) const CMD_DISCRIMINATOR: u8 = 0xF0;
/// Typed-response marker: response[0] == 0x51 means typed reply (PRD §10.2).
#[allow(dead_code)]
pub(crate) const RESPONSE_MARKER: u8 = 0x51;
/// Command IDs from firmware PRD §4.6 command table.
#[allow(dead_code)]
pub(crate) const CMD_QUERY_INFO: u8 = 0x01;
#[allow(dead_code)]
pub(crate) const CMD_QUERY_CALLBACK: u8 = 0x02;
#[allow(dead_code)]
pub(crate) const CMD_SET_OS: u8 = 0x03;
#[allow(dead_code)]
pub(crate) const CMD_APPLY_HOST_CONTEXT: u8 = 0x05;
/// Bounded timeout (ms) for reading the first reply after a burst.
/// Must be > 0 (unlike the drain's non-blocking timeout=0).
#[allow(dead_code)]
const REPLY_READ_TIMEOUT_MS: i32 = 1000;

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

/// Payload bytes carried per 32-byte raw-HID report.
///
/// Each report is laid out as `[report_id = 0, 0x81, 0x9F, <30 payload bytes…>]`
/// inside a `REPORT_LENGTH + 1` byte buffer (the leading byte is the report ID
/// demanded by HIDAPI's `write()` contract). 2 of those bytes are header
/// overhead, leaving `REPORT_LENGTH - 2` = 30 bytes of payload per report.
const PAYLOAD_PER_REPORT: usize = REPORT_LENGTH - 2;

/// Ceiling on how many IN-side reports we drain after a burst. The firmware
/// sends a 32-byte reply per report (fixed in qmk-notifier commit `01a51935`,
/// which corrected the response size from the header-stripped `30` to the full
/// `RAW_REPORT_SIZE = 32`). v0.2.x of this crate only *drains/discards* those
/// replies; the v0.3.0 typed-command path reads and parses them. Draining is
/// still required with a persistent handle so replies don't accumulate in the
/// kernel IN buffer and stall `raw_hid_send` on the device. Bounded so a
/// misbehaving IN endpoint can't wedge the notifier.
const IN_DRAIN_MAX: usize = 32;

/// On a total send failure (e.g. the keyboard was unplugged/replugged and the
/// cached handle is now stale), rebuild the device cache and retry this many
/// times before giving up. Only retries when *zero* devices succeeded, so a
/// partial send is never re-sent (no duplicate notifications).
const SEND_RETRIES: usize = 1;

pub fn send_raw_report(
    data: &[u8],
    vendor_id: Option<u16>,
    product_id: Option<u16>,
    usage_page: u16,
    usage: u16,
    verbose: bool,
) -> Result<(), QmkError> {
    let key = MatchKey {
        vendor_id,
        product_id,
        usage_page,
        usage,
    };
    let batch_count = batches_for(data);

    if verbose {
        println!("Request data ({} bytes):", data.len());
        println!("{:?}", data);
    }

    for attempt in 0..=SEND_RETRIES {
        match try_send_once(&key, data, batch_count, verbose)? {
            SendOutcome::AllSucceeded => return Ok(()),
            SendOutcome::Partial { succeeded, failed } => {
                return Err(QmkError::PartialSendError { succeeded, failed });
            }
            SendOutcome::TotalFailure if attempt < SEND_RETRIES => {
                // The cache was already invalidated inside try_send_once; the
                // next iteration re-enumerates + reopens.
                if verbose {
                    println!(
                        "All sends failed; rebuilding device cache and retrying (attempt {}/{}).",
                        attempt + 2,
                        SEND_RETRIES + 1
                    );
                }
                continue;
            }
            SendOutcome::TotalFailure => {
                return Err(QmkError::SendReportError(hidapi::HidError::HidApiError {
                    message: "Failed to send to any devices".to_string(),
                }));
            }
        }
    }

    unreachable!("the retry loop always returns on its first or final iteration")
}

/// Outcome of a single send attempt against the cached device handles.
#[derive(Debug)]
enum SendOutcome {
    /// Every device accepted the full burst.
    AllSucceeded,
    /// Some devices succeeded, some failed.
    Partial { succeeded: usize, failed: usize },
    /// No device accepted the burst (the cache has been invalidated).
    TotalFailure,
}

/// One full attempt: ensure the cache is populated for `key`, then burst-write
/// `data` to every cached device. Invalidates the cache on any write error so
/// the next attempt/call re-enumerates + reopens.
fn try_send_once(
    key: &MatchKey,
    data: &[u8],
    batch_count: usize,
    verbose: bool,
) -> Result<SendOutcome, QmkError> {
    let mut cache = lock_cache();
    ensure_cache(&mut cache, key, verbose)?;

    let device_count = cache.as_ref().expect("cache populated").devices.len();
    if verbose {
        println!("Found {} matching device(s).", device_count);
    }

    let mut succeeded = 0usize;
    let mut failed = 0usize;

    {
        let devices: &Vec<HidDevice> = &cache.as_ref().expect("cache populated").devices;
        for (device_idx, interface) in devices.iter().enumerate() {
            if verbose {
                let device_path = match interface.get_device_info() {
                    Ok(info) => format!("{:?}", info.path()),
                    Err(_) => "N/A".to_string(),
                };
                println!(
                    "Sending to device {}/{}: Path: {}",
                    device_idx + 1,
                    device_count,
                    device_path
                );
            }

            if burst_to_one(interface, data, batch_count, verbose) {
                succeeded += 1;
            } else {
                failed += 1;
                if verbose {
                    println!(
                        "Failed to send message to device {}/{}.",
                        device_idx + 1,
                        device_count
                    );
                }
            }
        }
    }

    // A write error means the cached handle is suspect (e.g. a replug made the
    // old fd stale). Drop the cache so the next attempt/call re-enumerates.
    if failed > 0 {
        *cache = None;
        if verbose {
            println!("Invalidating device cache after a write error.");
        }
    }

    Ok(if succeeded == 0 {
        SendOutcome::TotalFailure
    } else if failed > 0 {
        SendOutcome::Partial { succeeded, failed }
    } else {
        SendOutcome::AllSucceeded
    })
}

/// Burst-write `data` to a single device as `batch_count` back-to-back raw-HID
/// reports, then drain any pending IN-side reports. Returns `false` on the
/// first write error.
///
/// Burst-write is safe without a per-report ack: QMK's raw-HID OUT endpoint
/// buffers up to `RAW_OUT_CAPACITY` (4) reports and drains them all in one
/// main-loop pass (`raw_hid_task`: `while (receive_report(...))
/// raw_hid_receive(...)`). The OUT endpoint provides its own backpressure — when
/// the device buffer is full it NAKs the transfer and the host's `write()`
/// blocks until space frees. Reports are never dropped, so burst-write is safe
/// for ANY title length. See IMPLEMENTATION_PLAN.md.
fn burst_to_one(interface: &HidDevice, data: &[u8], batch_count: usize, verbose: bool) -> bool {
    let mut request_data = [0u8; REPORT_LENGTH + 1]; // stack array (was vec!)
    request_data[1] = 0x81;
    request_data[2] = 0x9F;

    for batch in 0..batch_count {
        let start_idx = batch * PAYLOAD_PER_REPORT;
        let end_idx = (start_idx + PAYLOAD_PER_REPORT).min(data.len());
        let batch_data = &data[start_idx..end_idx];

        request_data[3..].fill(0); // clear reused payload tail
        if !batch_data.is_empty() {
            request_data[3..3 + batch_data.len()].copy_from_slice(batch_data);
        }

        if verbose {
            println!("Sending batch {}/{}", batch + 1, batch_count);
            println!("{:?}", request_data);
        }

        if let Err(e) = interface.write(&request_data) {
            if verbose {
                println!("Error on batch {}: {}", batch + 1, e);
            }
            return false;
        }
    }

    // Drain any pending IN-side reports (non-blocking). The firmware sends a
    // valid 32-byte reply per report (qmk-notifier commit `01a51935`, which
    // fixed the response size from the header-stripped `30` to `RAW_REPORT_SIZE`
    // = 32); the old "ack silently dropped by send_raw_hid because length ==
    // RAW_EPSIZE" note described the pre-fix firmware. v0.2.x of this crate
    // discards the reply here; the v0.3.0 typed-command path reads and parses
    // it instead. Draining keeps a persistent handle from stalling on
    // accumulated replies.
    //
    // Note: read_timeout(0) returns Ok(0) on "no data" (poll times out), not an
    // error, so we break on Ok(0)/Err and only keep draining on a real read
    // (n > 0). Bounded by IN_DRAIN_MAX so a flooding IN endpoint can't wedge us.
    let mut drain_buf = [0u8; REPORT_LENGTH + 1];
    for _ in 0..IN_DRAIN_MAX {
        match interface.read_timeout(&mut drain_buf, 0) {
            Ok(n) if n > 0 => continue,
            _ => break,
        }
    }

    true
}

/// Number of reports needed to carry `data.len()` payload bytes (0 when empty).
fn batches_for(data: &[u8]) -> usize {
    (data.len() + REPORT_LENGTH - 3) / PAYLOAD_PER_REPORT
}

/// Match parameters a cached handle set was opened for. The cache is rebuilt
/// whenever a request uses a different key.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct MatchKey {
    vendor_id: Option<u16>,
    product_id: Option<u16>,
    usage_page: u16,
    usage: u16,
}

/// A long-lived HID context plus the handles opened from it for a [`MatchKey`].
/// Kept in a global [`Mutex`] so repeated notifications reuse the same handles
/// (and the same enumerated `HidApi`) instead of re-scanning the whole HID bus
/// on every call. Enumerating + opening was, with the per-report reads gone,
/// the dominant per-notification cost.
struct DeviceCache {
    /// Long-lived HID context the handles were opened from. Retained (rather
    /// than dropped after opening) to pin the context's lifetime for the cached
    /// handles and to enable a cheaper `refresh_devices()`-based rebuild later.
    /// It is intentionally not read on the hot path.
    #[allow(dead_code)]
    api: HidApi,
    devices: Vec<HidDevice>,
    key: MatchKey,
}

static DEVICE_CACHE: LazyLock<Mutex<Option<DeviceCache>>> = LazyLock::new(|| Mutex::new(None));

/// Lock the global device cache, recovering from a poisoned mutex (a previous
/// holder panicked) rather than propagating the panic.
fn lock_cache() -> MutexGuard<'static, Option<DeviceCache>> {
    DEVICE_CACHE
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

/// Ensure the cache slot holds a handle set for `key`. (Re)builds it — full
/// `HidApi` enumeration + open — only when empty or when the key changes; the
/// hot path reuses the cached handles and returns immediately.
///
/// Note: a newly-plugged *additional* matching device is not picked up until a
/// write fails (forcing a rebuild) or the match key changes. This is the
/// intentional trade-off of caching and is fine for the single-keyboard use
/// case (the replug case is handled: a stale handle fails the write, which
/// invalidates the cache and triggers this rebuild).
fn ensure_cache(
    cache: &mut Option<DeviceCache>,
    key: &MatchKey,
    verbose: bool,
) -> Result<(), QmkError> {
    if let Some(existing) = cache.as_ref() {
        if &existing.key == key {
            if verbose {
                println!(
                    "Reusing cached device handle ({} device(s)).",
                    existing.devices.len()
                );
            }
            return Ok(());
        }
    }

    if verbose {
        println!("Building device cache (enumerating HID bus)...");
    }

    let api = HidApi::new().map_err(|e| QmkError::HidApiInitError(e.to_string()))?;
    let devices = open_matching_devices(&api, key)?;

    let count = devices.len();
    *cache = Some(DeviceCache {
        api,
        devices,
        key: *key,
    });

    if verbose {
        println!("Cached {} matching device(s).", count);
    }
    Ok(())
}

/// Enumerate `api` and open every interface matching `key`. Mirrors the old
/// per-call enumeration but reuses an existing `HidApi` so the cache can share a
/// single context across calls.
fn open_matching_devices(api: &HidApi, key: &MatchKey) -> Result<Vec<HidDevice>, QmkError> {
    let device_infos: Vec<_> = api
        .device_list()
        .filter(|d| {
            device_matches(
                d.vendor_id(),
                d.product_id(),
                d.usage_page(),
                d.usage(),
                key.vendor_id,
                key.product_id,
                key.usage_page,
                key.usage,
            )
        })
        .collect();

    if device_infos.is_empty() {
        return Err(QmkError::DeviceNotFound {
            vendor_id: key.vendor_id,
            product_id: key.product_id,
            usage_page: key.usage_page,
            usage: key.usage,
        });
    }

    let opened_devices: Vec<HidDevice> = device_infos
        .into_iter()
        .filter_map(|info| info.open_device(api).ok())
        .collect();

    if opened_devices.is_empty() {
        return Err(QmkError::DeviceOpenError(
            "Found matching HID devices, but could not open any of them for communication. Check permissions (udev rules on Linux)."
                .to_string(),
        ));
    }

    Ok(opened_devices)
}

/// Pure match predicate for the raw-HID interface filter.
///
/// A device matches when its usage page/usage equal the required values, and
/// its VID/PID equal the given values when those are `Some`. `None` VID/PID
/// means "match any" (the default auto-discovery path).
#[allow(clippy::too_many_arguments)]
fn device_matches(
    dev_vendor_id: u16,
    dev_product_id: u16,
    dev_usage_page: u16,
    dev_usage: u16,
    vendor_id: Option<u16>,
    product_id: Option<u16>,
    usage_page: u16,
    usage: u16,
) -> bool {
    dev_usage_page == usage_page
        && dev_usage == usage
        && vendor_id.is_none_or(|v| dev_vendor_id == v)
        && product_id.is_none_or(|p| dev_product_id == p)
}

#[cfg(test)]
mod tests {
    use super::*;

    const DEV_VID: u16 = 0xFEED;
    const DEV_PID: u16 = 0x0000;
    const DEV_PAGE: u16 = 0xFF60;
    const DEV_USAGE: u16 = 0x61;

    #[test]
    fn matches_when_all_four_equal() {
        assert!(device_matches(
            DEV_VID,
            DEV_PID,
            DEV_PAGE,
            DEV_USAGE,
            Some(DEV_VID),
            Some(DEV_PID),
            DEV_PAGE,
            DEV_USAGE,
        ));
    }

    #[test]
    fn matches_by_usage_page_alone_when_vid_pid_none() {
        // The keystone auto-discovery path: VID/PID omitted.
        assert!(device_matches(
            DEV_VID, DEV_PID, DEV_PAGE, DEV_USAGE, None, None, DEV_PAGE, DEV_USAGE,
        ));
    }

    #[test]
    fn matches_arbitrary_vid_pid_when_none() {
        // Any VID/PID device with the right usage/page matches.
        assert!(device_matches(
            0x1234, 0xABCD, DEV_PAGE, DEV_USAGE, None, None, DEV_PAGE, DEV_USAGE,
        ));
    }

    #[test]
    fn rejects_wrong_usage_page_even_when_vid_pid_match() {
        assert!(!device_matches(
            DEV_VID,
            DEV_PID,
            DEV_PAGE,
            DEV_USAGE,
            Some(DEV_VID),
            Some(DEV_PID),
            0x1234,
            DEV_USAGE,
        ));
    }

    #[test]
    fn rejects_wrong_usage() {
        assert!(!device_matches(
            DEV_VID, DEV_PID, DEV_PAGE, DEV_USAGE, None, None, DEV_PAGE, 0x99,
        ));
    }

    #[test]
    fn rejects_wrong_vid_when_some() {
        assert!(!device_matches(
            DEV_VID,
            DEV_PID,
            DEV_PAGE,
            DEV_USAGE,
            Some(0x1111),
            None,
            DEV_PAGE,
            DEV_USAGE,
        ));
    }

    #[test]
    fn rejects_wrong_pid_when_some() {
        assert!(!device_matches(
            DEV_VID,
            DEV_PID,
            DEV_PAGE,
            DEV_USAGE,
            None,
            Some(0x2222),
            DEV_PAGE,
            DEV_USAGE,
        ));
    }

    #[test]
    fn vid_can_disambiguate_while_pid_any() {
        assert!(device_matches(
            DEV_VID,
            DEV_PID,
            DEV_PAGE,
            DEV_USAGE,
            Some(DEV_VID),
            None,
            DEV_PAGE,
            DEV_USAGE,
        ));
    }

    #[test]
    fn batches_for_empty_is_zero() {
        assert_eq!(batches_for(&[]), 0);
    }

    #[test]
    fn batches_for_single_byte_is_one() {
        assert_eq!(batches_for(&[0x03]), 1);
    }

    #[test]
    fn batches_for_exact_report_multiple() {
        // 30 payload bytes = exactly one report; 60 = exactly two.
        assert_eq!(batches_for(&[0u8; 30]), 1);
        assert_eq!(batches_for(&[0u8; 60]), 2);
    }

    #[test]
    fn batches_for_one_over_splits() {
        // A single byte past a report boundary forces an extra report.
        assert_eq!(batches_for(&[0u8; 31]), 2);
        assert_eq!(batches_for(&[0u8; 61]), 3);
    }

    #[test]
    fn match_key_equality_drives_cache_rebuild() {
        let auto = MatchKey {
            vendor_id: None,
            product_id: None,
            usage_page: DEV_PAGE,
            usage: DEV_USAGE,
        };
        let explicit = MatchKey {
            vendor_id: Some(DEV_VID),
            product_id: Some(DEV_PID),
            usage_page: DEV_PAGE,
            usage: DEV_USAGE,
        };
        assert_eq!(auto, auto);
        assert_ne!(auto, explicit);
    }

    #[test]
    fn typed_command_constants_match_firmware_contract() {
        // Wire-protocol values are the canonical source of truth from the firmware
        // notifier.h (see firmware_wire_contract.md §Constants). Drift here would
        // silently break host<->firmware interop, so pin every value.
        assert_eq!(
            CMD_DISCRIMINATOR, 0xF0,
            "NOTIFY_CMD_DISCRIMINATOR: typed-command first payload byte after 0x81 0x9F"
        );
        assert_eq!(
            RESPONSE_MARKER, 0x51,
            "NOTIFY_RESPONSE_MARKER: response[0]==0x51 means typed reply"
        );
        assert_eq!(CMD_QUERY_INFO, 0x01, "NOTIFY_CMD_QUERY_INFO");
        assert_eq!(CMD_QUERY_CALLBACK, 0x02, "NOTIFY_CMD_QUERY_CALLBACK");
        assert_eq!(CMD_SET_OS, 0x03, "NOTIFY_CMD_SET_OS");
        assert_eq!(
            CMD_APPLY_HOST_CONTEXT, 0x05,
            "NOTIFY_CMD_APPLY_HOST_CONTEXT"
        );
        // REPLY_READ_TIMEOUT_MS is a host-side choice (bounded blocking read of the
        // first reply after a burst), NOT a firmware value. Its invariant is the
        // documented "Must be > 0" (unlike the drain loop's non-blocking 0).
        assert!(
            REPLY_READ_TIMEOUT_MS > 0,
            "reply read timeout must block for a real reply, not poll like the drain's 0"
        );
    }
}
