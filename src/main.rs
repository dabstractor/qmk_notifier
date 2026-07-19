use qmk_notifier::{parse_cli_args, run, CommandResponse, RunCommand, RunParameters};

fn main() {
    // Restore the default SIGPIPE disposition so piping into `head`/`less`
    // (which close stdout early) terminates the process cleanly instead of
    // panicking with "failed printing to stdout: Broken pipe" (exit 101).
    // Rust's std ignores SIGPIPE by default; this puts it back to SIG_DFL so
    // the process exits with 141 (128 + SIGPIPE) like a normal Unix tool.
    #[cfg(unix)]
    reset_sigpipe_to_default();

    let cli = match parse_cli_args() {
        Ok(cli) => cli,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Copy out the device-targeting scalars (all Copy) BEFORE run() moves
    // cli.params by value — they're needed to rebuild per-callback params.
    // `is_list` records whether the action is `--list` (ListDevices) so that
    // after `run` we can suppress the library's `Timeout` sentinel for it
    // without re-borrowing the moved `params`.
    let is_list = cli.params.command == RunCommand::ListDevices;
    let vendor_id = cli.params.vendor_id;
    let product_id = cli.params.product_id;
    let usage_page = cli.params.usage_page;
    let usage = cli.params.usage;
    let verbose = cli.params.verbose;
    let list_callbacks = cli.list_callbacks;

    match run(cli.params) {
        // --list-callbacks: after QueryInfo succeeds, sweep the callback registry.
        Ok(CommandResponse::Info { callback_count, .. }) if list_callbacks => {
            println!("callbacks ({callback_count}):");
            for index in 0..callback_count {
                let params = RunParameters::new(
                    RunCommand::QueryCallback(index),
                    vendor_id,
                    product_id,
                    usage_page,
                    usage,
                    verbose,
                );
                match run(params) {
                    Ok(CommandResponse::CallbackName { index, name }) => {
                        println!(
                            "  {}: {}",
                            index,
                            name.unwrap_or_else(|| "(unnamed)".to_string())
                        );
                    }
                    Ok(other) => eprintln!("callback {index}: unexpected response {other:?}"),
                    Err(e) => eprintln!("callback {index}: error: {e}"),
                }
            }
        }
        // --list: device enumeration is already printed by `list_hid_devices`
        // inside `run`. The library returns `CommandResponse::Timeout` for
        // `ListDevices` ("nothing was sent over the wire"), which is a fine
        // library-level semantic but confusing to surface to the user — so we
        // suppress it here instead of printing a bare `Timeout` line.
        Ok(_) if is_list => {}
        // --query-info / message, OR a non-capable device (Timeout/Legacy)
        // when --list-callbacks was asked for: just print the raw response.
        Ok(response) => println!("{:?}", response),
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    }
}

/// Reset the SIGPIPE disposition to its default (`SIG_DFL`).
///
/// Rust's runtime sets SIGPIPE to `SIG_IGN`, which turns the next `println!` to
/// a closed pipe into a panic (exit 101). Unix CLI tools are expected to die
/// quietly with SIGPIPE (exit 141) when a downstream consumer exits early
/// (e.g. `qmk_notifier --list | head -1`). This restores that behavior.
///
/// Uses raw `libc`-style syscalls via `extern "C"` so no extra dependency is
/// required. On non-Unix targets this is a no-op (the helper is only called
/// behind `#[cfg(unix)]`).
#[cfg(unix)]
fn reset_sigpipe_to_default() {
    /// Signal disposition is the address of a handler function, `SIG_DFL` (0)
    /// or `SIG_IGN` (1).
    type SigHandler = usize;
    extern "C" {
        fn signal(signum: i32, handler: SigHandler) -> SigHandler;
    }

    const SIGPIPE: i32 = 13;
    const SIG_DFL: SigHandler = 0;

    // SAFETY: `signal()` is a thread-safe-enough POSIX call for one-shot
    // process-global disposition reset performed before any threads are spawned.
    // `SIG_DFL` is a well-known sentinel value. Ignoring the return value is
    // fine: worst case the disposition stays unchanged, which is benign.
    unsafe {
        signal(SIGPIPE, SIG_DFL);
    }
}
