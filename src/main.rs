use qmk_notifier::{parse_cli_args, run, CommandResponse, RunCommand, RunParameters};

fn main() {
    let cli = match parse_cli_args() {
        Ok(cli) => cli,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Copy out the device-targeting scalars (all Copy) BEFORE run() moves
    // cli.params by value — they're needed to rebuild per-callback params.
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
        // --query-info / --list / message, OR a non-capable device (Timeout/Legacy)
        // when --list-callbacks was asked for: just print the raw response.
        Ok(response) => println!("{:?}", response),
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    }
}
