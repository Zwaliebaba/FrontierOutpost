//! Shared authored berth offsets. Parsing embedded data performs no I/O.
use serde::Deserialize;
use std::sync::OnceLock;

#[derive(Deserialize)]
struct Berth {
    x: f32,
}
#[derive(Deserialize)]
struct Layout {
    berths: [Berth; 4],
}

/// A stable slot for each launching pilot, independent of the current crew size.
#[must_use]
pub fn lateral(index: u32) -> f32 {
    static LAYOUT: OnceLock<Layout> = OnceLock::new();
    let layout = LAYOUT.get_or_init(|| {
        serde_json::from_str(include_str!("../../../Design/FrontierOutpost/berths.json"))
            .expect("the checked-in berth layout must contain four offsets")
    });
    layout.berths[usize::try_from(index).expect("a crew slot fits usize")].x
}

#[cfg(test)]
mod tests {
    use super::lateral;
    use crate::collision::SHIP_RADIUS;
    #[test]
    fn four_slots_clear_the_collision_envelopes() {
        for i in 0..3 {
            assert!(lateral(i + 1) - lateral(i) > 2.0 * SHIP_RADIUS + 0.15);
        }
    }
}
