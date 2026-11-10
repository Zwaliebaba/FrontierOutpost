use frontier_protocol::PROTOCOL_VERSION;
use frontier_sim::Tick;

fn main() {
    // Networking is not implemented yet; this binary only proves the crate graph links.
    println!(
        "frontier-server {} (protocol {PROTOCOL_VERSION}, starting at tick {})",
        env!("CARGO_PKG_VERSION"),
        Tick::ZERO.get()
    );
}
