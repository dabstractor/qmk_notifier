fn main() {
    // Parse CLI arguments and create parameters
    let params = match qmk_notifier::parse_cli_args() {
        Ok(params) => params,
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    };

    // Call the run function with parsed parameters and print the CommandResponse.
    match qmk_notifier::run(params) {
        Ok(response) => println!("{:?}", response),
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
    }
}
