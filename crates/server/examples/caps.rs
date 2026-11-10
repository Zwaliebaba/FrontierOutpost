//! Plan work item 2.6: what one tick costs the server with the expedition at
//! its provisional caps, and how large its snapshots get.
//!
//! ```sh
//! cargo run --release -p frontier-server --example caps
//! ```

use std::time::{Duration, Instant};

use frontier_server::caps::Caps;
use frontier_sim::TICKS_PER_SECOND;

/// Ten minutes of play.
const TICKS: usize = 12_000;

fn main() {
    let mut caps = Caps::new();
    let mut ticks = Vec::with_capacity(TICKS);
    let mut sizes = Vec::with_capacity(TICKS);
    let mut projectiles = Vec::with_capacity(TICKS);
    let mut ships = Vec::with_capacity(TICKS);
    for _ in 0..TICKS {
        let start = Instant::now();
        let datagram = caps.step().expect("a full expedition's snapshot encodes");
        ticks.push(start.elapsed());
        sizes.push(datagram.len());
        projectiles.push(caps.world().projectiles().len());
        ships.push(caps.world().ships().len());
    }
    ticks.sort_unstable();
    sizes.sort_unstable();
    projectiles.sort_unstable();
    ships.sort_unstable();

    let budget = Duration::from_secs(1) / TICKS_PER_SECOND;
    let mean = ticks.iter().sum::<Duration>() / u32::try_from(TICKS).expect("fits");
    println!("{TICKS} ticks; one tick may take {budget:?}");
    println!(
        "tick (step + snapshot encode): mean {mean:?}, median {:?}, p99 {:?}, max {:?}",
        at(&ticks, 0.5),
        at(&ticks, 0.99),
        at(&ticks, 1.0),
    );
    println!(
        "snapshot bytes: median {}, p99 {}, max {}",
        at(&sizes, 0.5),
        at(&sizes, 0.99),
        at(&sizes, 1.0),
    );
    println!(
        "projectiles in flight: median {}, p99 {}, max {}",
        at(&projectiles, 0.5),
        at(&projectiles, 0.99),
        at(&projectiles, 1.0),
    );
    println!("ships: median {}, max {}", at(&ships, 0.5), at(&ships, 1.0));
}

fn at<T: Copy>(sorted: &[T], quantile: f64) -> T {
    let last = sorted.len() - 1;
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        reason = "a quantile of an index is a small non-negative index"
    )]
    let index = (last as f64 * quantile).round() as usize;
    sorted[index.min(last)]
}
