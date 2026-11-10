//! Collision on the gameplay plane. Ships and obstacles are circles.
//!
//! Collisions push apart and never damage; only the debris-drift hazard does
//! (design §10, plan M3). A ship that hits something keeps the part of its
//! speed that runs along the surface, so it slides rather than stopping dead.

use crate::rng::Rng;
use crate::ship::ShipState;

/// The starter hull's collision radius, from the frigate's 2.67 m length.
pub const SHIP_RADIUS: f32 = 1.3;

/// A fixed circular obstacle, such as an asteroid.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Obstacle {
    pub x: f32,
    pub z: f32,
    pub radius: f32,
}

/// Pushes a ship out of every obstacle it overlaps.
#[must_use]
pub fn resolve_obstacles(state: ShipState, radius: f32, obstacles: &[Obstacle]) -> ShipState {
    resolve_obstacles_with_impact(state, radius, obstacles).0
}

/// Pushes a ship out of every obstacle it overlaps, and returns the hardest
/// impact: the speed it drove into a surface at, m/s, or 0.
#[must_use]
pub fn resolve_obstacles_with_impact(
    mut state: ShipState,
    radius: f32,
    obstacles: &[Obstacle],
) -> (ShipState, f32) {
    let mut impact: f32 = 0.0;
    for obstacle in obstacles {
        let (dx, dz) = (state.x - obstacle.x, state.z - obstacle.z);
        let distance = dx.hypot(dz);
        let overlap = radius + obstacle.radius - distance;
        if overlap <= 0.0 {
            continue;
        }
        let (nx, nz) = normal(dx, dz, distance);
        state.x += nx * overlap;
        state.z += nz * overlap;
        let slid = slide(&state, nx, nz);
        impact = impact.max(state.speed - slid);
        state.speed = slid;
    }
    (state, impact)
}

/// Pushes two overlapping ships apart, the larger moving less.
pub fn separate(a: &mut ShipState, a_radius: f32, b: &mut ShipState, b_radius: f32) {
    let (dx, dz) = (a.x - b.x, a.z - b.z);
    let distance = dx.hypot(dz);
    let overlap = a_radius + b_radius - distance;
    if overlap <= 0.0 {
        return;
    }
    let (nx, nz) = normal(dx, dz, distance);
    let (a_mass, b_mass) = (a_radius * a_radius, b_radius * b_radius);
    let a_share = b_mass / (a_mass + b_mass);
    let b_share = 1.0 - a_share;
    a.x += nx * overlap * a_share;
    a.z += nz * overlap * a_share;
    b.x -= nx * overlap * b_share;
    b.z -= nz * overlap * b_share;
    a.speed = slide(a, nx, nz);
    b.speed = slide(b, -nx, -nz);
}

/// The unit vector from the other body to this one; any direction if they coincide.
fn normal(dx: f32, dz: f32, distance: f32) -> (f32, f32) {
    if distance > 1e-6 {
        (dx / distance, dz / distance)
    } else {
        (1.0, 0.0)
    }
}

/// Speed left after losing the part that drove into a surface with outward normal (nx, nz).
fn slide(state: &ShipState, nx: f32, nz: f32) -> f32 {
    let (sin, cos) = state.heading.sin_cos();
    let into = -(cos * nx + sin * nz);
    if into > 0.0 {
        state.speed * (1.0 - into)
    } else {
        state.speed
    }
}

/// Asteroids around the spawn, generated from `seed`. A placeholder field until
/// the site is generated from its seed (plan M3).
#[must_use]
pub fn asteroid_field(seed: u32) -> Vec<Obstacle> {
    const COUNT: usize = 60;
    const HALF_WIDTH: f32 = 220.0;
    /// Keeps the spawn ring and its surroundings clear.
    const CLEAR_RADIUS: f32 = 18.0;
    let mut rng = Rng::new(seed);
    let mut field = Vec::with_capacity(COUNT);
    while field.len() < COUNT {
        let x = rng.between(-HALF_WIDTH, HALF_WIDTH);
        let z = rng.between(-HALF_WIDTH, HALF_WIDTH);
        let radius = rng.between(0.6, 4.0);
        if x.hypot(z) >= CLEAR_RADIUS {
            field.push(Obstacle { x, z, radius });
        }
    }
    field
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ship(x: f32, z: f32, heading: f32, speed: f32) -> ShipState {
        ShipState {
            x,
            z,
            heading,
            speed,
        }
    }

    const ROCK: Obstacle = Obstacle {
        x: 0.0,
        z: 0.0,
        radius: 2.0,
    };

    #[test]
    fn reports_how_hard_a_ship_hit() {
        // Head-on at 8 m/s: all of it drives into the rock.
        let head_on = ship(-2.5, 0.0, 0.0, 8.0);
        let (_, impact) = resolve_obstacles_with_impact(head_on, SHIP_RADIUS, &[ROCK]);
        assert!((impact - 8.0).abs() < 1e-4, "{impact}");
        let clear = ship(-10.0, 0.0, 0.0, 8.0);
        assert_eq!(
            resolve_obstacles_with_impact(clear, SHIP_RADIUS, &[ROCK]).1,
            0.0
        );
    }

    #[test]
    fn pushes_a_ship_out_of_an_obstacle() {
        let inside = ship(2.0, 0.0, 0.0, 0.0);
        let out = resolve_obstacles(inside, SHIP_RADIUS, &[ROCK]);
        assert!((out.x - (2.0 + SHIP_RADIUS)).abs() < 1e-5);
        assert_eq!(out.z, 0.0);
    }

    #[test]
    fn leaves_a_clear_ship_alone() {
        let clear = ship(10.0, 0.0, 1.0, 5.0);
        assert_eq!(resolve_obstacles(clear, SHIP_RADIUS, &[ROCK]), clear);
    }

    #[test]
    fn stops_a_head_on_ship_and_lets_a_glancing_one_slide() {
        // Flying straight at the rock along -x.
        let head_on = ship(3.0, 0.0, std::f32::consts::PI, 8.0);
        assert!(resolve_obstacles(head_on, SHIP_RADIUS, &[ROCK]).speed < 1e-4);
        // Flying past it along +z, grazing its edge.
        let glancing = ship(3.0, 0.0, std::f32::consts::FRAC_PI_2, 8.0);
        assert!((resolve_obstacles(glancing, SHIP_RADIUS, &[ROCK]).speed - 8.0).abs() < 1e-4);
    }

    #[test]
    fn moving_away_keeps_its_speed() {
        let leaving = ship(3.0, 0.0, 0.0, 8.0);
        assert!((resolve_obstacles(leaving, SHIP_RADIUS, &[ROCK]).speed - 8.0).abs() < 1e-5);
    }

    #[test]
    fn separates_overlapping_ships_by_size() {
        let (mut small, mut large) = (ship(0.0, 0.0, 0.0, 0.0), ship(1.0, 0.0, 0.0, 0.0));
        separate(&mut small, 1.0, &mut large, 2.0);
        let gap = (large.x - small.x).abs();
        assert!((gap - 3.0).abs() < 1e-5, "{gap}");
        // The small ship moves four times as far as the large one.
        assert!((small.x.abs() - 4.0 * (large.x - 1.0)).abs() < 1e-5);
    }

    #[test]
    fn separates_ships_that_coincide_exactly() {
        let (mut a, mut b) = (ship(5.0, 5.0, 0.0, 0.0), ship(5.0, 5.0, 0.0, 0.0));
        separate(&mut a, SHIP_RADIUS, &mut b, SHIP_RADIUS);
        assert!((a.x - b.x).hypot(a.z - b.z) >= 2.0 * SHIP_RADIUS - 1e-5);
    }

    #[test]
    fn the_asteroid_field_repeats_from_its_seed_and_keeps_the_spawn_clear() {
        let field = asteroid_field(2266);
        assert_eq!(field, asteroid_field(2266));
        assert_ne!(field, asteroid_field(2267));
        assert_eq!(field.len(), 60);
        assert!(field.iter().all(|rock| rock.x.hypot(rock.z) >= 18.0));
    }
}
