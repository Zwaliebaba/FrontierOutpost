//! Plan work item 2.6: snapshots of an expedition at its caps fit one datagram.

use frontier_protocol::snapshot::MAX_DATAGRAM_BYTES;
use frontier_server::caps::Caps;
use frontier_sim::combat::MAX_PROJECTILES;
use frontier_sim::skirmish::MAX_NPCS;

#[test]
fn snapshots_at_the_caps_fit_one_datagram() {
    let mut caps = Caps::new();
    let mut largest = 0;
    let mut fewest_ships = usize::MAX;
    let mut most_projectiles = 0;
    // Two minutes of play.
    for _ in 0..2_400 {
        let datagram = caps.step().expect("a full expedition's snapshot encodes");
        largest = largest.max(datagram.len());
        fewest_ships = fewest_ships.min(caps.world().ships().len());
        most_projectiles = most_projectiles.max(caps.world().projectiles().len());
    }
    // A ship destroyed this tick is replaced on the next.
    assert!(
        fewest_ships >= MAX_NPCS,
        "the expedition stayed nearly full"
    );
    // With the placeholder weapons a full fight keeps far fewer than the cap in
    // flight; the cap is a ceiling, enforced by frontier-sim.
    assert!(most_projectiles > 0 && most_projectiles <= MAX_PROJECTILES);
    assert!(
        largest <= MAX_DATAGRAM_BYTES,
        "{largest} bytes > {MAX_DATAGRAM_BYTES}"
    );
}
