//! What each kind of ship can do. Every number here is a placeholder to be tuned
//! in play: design section 10 describes the NPCs in words but gives no values.

use crate::ship::{Handling, STARTER_HANDLING};

/// Who a ship is.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ShipKind {
    /// A player's ship: the starter hull.
    Pilot,
    /// Swarms the nearest pilot; fragile, short range (design §10).
    Drone,
    /// Hunts the pilot carrying the most cargo (design §10).
    Raider,
    /// Guards a post; slow, heavy, long range; never leaves it (design §10).
    Warden,
}

impl ShipKind {
    #[must_use]
    pub fn is_hostile_to(self, other: Self) -> bool {
        (self == Self::Pilot) != (other == Self::Pilot)
    }

    #[must_use]
    pub fn profile(self) -> &'static Profile {
        match self {
            Self::Pilot => &PILOT,
            Self::Drone => &DRONE,
            Self::Raider => &RAIDER,
            Self::Warden => &WARDEN,
        }
    }
}

/// A lock-on weapon. Projectiles home on the locked target and hit unless it is
/// destroyed first; the server resolves the hit (design §5).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Weapon {
    /// Metres; the weapon fires only at a target this close.
    pub range: f32,
    /// Ticks between shots.
    pub cooldown_ticks: u32,
    /// Hull removed by one hit.
    pub damage: f32,
    /// Metres per second.
    pub projectile_speed: f32,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Profile {
    pub handling: Handling,
    /// Collision radius, metres.
    pub radius: f32,
    pub max_hull: f32,
    pub weapon: Weapon,
    /// NPCs notice pilots this far away; unused for pilots.
    pub awareness: f32,
}

const PILOT: Profile = Profile {
    handling: STARTER_HANDLING,
    radius: crate::collision::SHIP_RADIUS,
    max_hull: 100.0,
    weapon: Weapon {
        range: 22.0,
        cooldown_ticks: 10,
        damage: 10.0,
        projectile_speed: 30.0,
    },
    awareness: 0.0,
};

const DRONE: Profile = Profile {
    handling: Handling {
        max_speed: 11.0,
        acceleration: 10.0,
        turn_rate: 3.5,
        drag: 0.5,
    },
    radius: 0.9,
    max_hull: 20.0,
    weapon: Weapon {
        range: 8.0,
        cooldown_ticks: 6,
        damage: 2.0,
        projectile_speed: 25.0,
    },
    awareness: 60.0,
};

const RAIDER: Profile = Profile {
    handling: Handling {
        max_speed: 10.0,
        acceleration: 7.0,
        turn_rate: 2.6,
        drag: 0.5,
    },
    radius: 1.3,
    max_hull: 60.0,
    weapon: Weapon {
        range: 18.0,
        cooldown_ticks: 12,
        damage: 6.0,
        projectile_speed: 28.0,
    },
    awareness: 90.0,
};

const WARDEN: Profile = Profile {
    handling: Handling {
        max_speed: 4.0,
        acceleration: 3.0,
        turn_rate: 1.2,
        drag: 0.5,
    },
    radius: 2.2,
    max_hull: 220.0,
    weapon: Weapon {
        range: 35.0,
        cooldown_ticks: 30,
        damage: 15.0,
        projectile_speed: 20.0,
    },
    // Notices pilots near its post: its leash plus its weapon's reach.
    awareness: crate::npc::WARDEN_LEASH + 35.0,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pilots_and_npcs_are_hostile_to_each_other_only() {
        assert!(ShipKind::Pilot.is_hostile_to(ShipKind::Drone));
        assert!(ShipKind::Warden.is_hostile_to(ShipKind::Pilot));
        assert!(!ShipKind::Pilot.is_hostile_to(ShipKind::Pilot));
        assert!(!ShipKind::Drone.is_hostile_to(ShipKind::Raider));
    }

    #[test]
    fn the_profiles_match_design_section_10_in_words() {
        let (drone, raider, warden) = (
            ShipKind::Drone.profile(),
            ShipKind::Raider.profile(),
            ShipKind::Warden.profile(),
        );
        // Drones are fragile and short-ranged; wardens slow, heavy and long-ranged.
        assert!(drone.max_hull < raider.max_hull && raider.max_hull < warden.max_hull);
        assert!(
            drone.weapon.range < raider.weapon.range && raider.weapon.range < warden.weapon.range
        );
        assert!(warden.handling.max_speed < ShipKind::Pilot.profile().handling.max_speed);
        assert!(warden.radius > raider.radius && raider.radius > drone.radius);
    }
}
