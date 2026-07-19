# External Dependencies & API Surface

## hidapi Rust crate (2.4.1+, resolves to 2.6.3 in Cargo.lock)

**Verified against**: current source code in `src/core.rs` (compiles and tests
pass with hidapi 2.6.3).

### Key API signatures

```rust
// Context creation
hidapi::HidApi::new() -> Result<HidApi, HidError>

// Device enumeration
api.device_list() -> impl Iterator<Item = &DeviceInfo>
// DeviceInfo methods:
//   vendor_id() -> u16
//   product_id() -> u16
//   usage_page() -> u16
//   usage() -> u16
//   path() -> &OsStr (or &CStr depending on platform)
//   product_string() -> Option<&str>
//   open_device(&HidApi) -> Result<HidDevice, HidError>

// Writing (OUT endpoint)
device.write(data: &[u8]) -> Result<usize, HidError>
// data[0] = report ID (0x00 for single-report interfaces)
// Returns number of bytes actually written

// Reading (IN endpoint)
device.read_timeout(buf: &mut [u8], timeout: i32) -> Result<usize, HidError>
// timeout in milliseconds
// timeout = 0 => non-blocking poll
// timeout < 0 => blocking (no timeout)
// timeout > 0 => wait up to N ms
// Returns Ok(0) on timeout/no-data (NOT an error)
// Returns Ok(n) with n > 0 when data was read

// Device info from open handle
device.get_device_info() -> Result<DeviceInfo, HidError>
```

### HidError variants (used in this crate)

```rust
pub enum HidError {
    HidApiError { message: String },          // verified — used in core.rs
    HidApiErrorHandleError { message: String },
    HidApiErrorEmpty,
    FromWideCharError,
    ToWideCharError,
    CStrConvertError(std::ffi::NulError),
    StringConvertError(String),
    InitError,
    OpenHidDeviceFailed,
    NotImplemented,
    // ...
}
```

`QmkError` implements `From<HidError>` → wraps as `SendReportError(HidError)`.

### usage_page() / usage() note

These identify the HID **top-level collection**, which is the stable QMK raw-HID
key (`0xFF60` / `0x61`). They are **distinct from** `interface_number()` (USB
interface slot). The hidraw backend (linked as `-lhidapi-hidraw` on Arch) provides
correct usage/usage_page values; the libusb backend may not. This is why the PRD
§12 specifies `hidapi-hidraw`.

## clap Rust crate (4.5+)

**Verified against**: current `src/lib.rs` `parse_cli_args()`.

Uses builder API (`Command::new`, `Arg::new`, `ArgAction::SetTrue`). Positional
`message` arg uses `.index(1)`. Flag args use `.short()`, `.long()`,
`.value_parser(clap::value_parser!(String))`.

For optional diagnostic subcommands, the builder API supports `.subcommand()`.
However, the PRD §11 says subcommands are "optional conveniences" and not required.
The simpler approach is additional flags (`--query-info`, `--list-callbacks`)
that map to `RunCommand` variants, keeping the existing flat CLI structure.

## Cargo.toml dependency state

| Dependency | Version | Used? | Action |
|---|---|---|---|
| `hidapi` | 2.4.1 (resolves to 2.6.3) | ✅ core.rs | Keep |
| `clap` | 4.5.31 | ✅ lib.rs | Keep |
| `toml` | 0.8.10 | ❌ unused | **Drop** (PRD §2) |
| `dirs` | 5.0.1 | ❌ unused | **Drop** (config-file support removed) |
| `serde` | 1.0 + derive | ❌ unused | **Drop** (PRD §2) |

## read_timeout semantics (critical for reply capture)

The v0.3.0 reply-capture change requires reading with a **bounded non-zero timeout**
(unlike the current drain which uses `timeout=0`, non-blocking). The correct
approach:

1. After burst-write, call `read_timeout(&mut buf, REPLY_READ_TIMEOUT_MS)` where
   `REPLY_READ_TIMEOUT_MS` is a small bounded value (e.g., 1000ms). This blocks
   until either a reply arrives or the timeout elapses.
2. If `Ok(n)` with `n > 0`: reply captured — parse it.
3. If `Ok(0)` or `Err`: no reply — return `CommandResponse::Timeout`.
4. Then drain any surplus IN-side reports with `read_timeout(&mut buf, 0)`
   (non-blocking, bounded by `IN_DRAIN_MAX`), same as today.

This preserves the existing drain behavior while adding reply capture.