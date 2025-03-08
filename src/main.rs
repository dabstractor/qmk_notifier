fn main() {
    // Call the run function and handle any errors
    if let Err(e) = qmk_notifier::run(None) {
        eprintln!("Error: {}", e);
    }
}
