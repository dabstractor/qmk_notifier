fn main() {
    // Parse CLI arguments and create parameters
    let params = match qmk_notifier::parse_cli_args() {
        Ok(params) => params,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Call the run function with parsed parameters
    if let Err(e) = qmk_notifier::run(params) {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
