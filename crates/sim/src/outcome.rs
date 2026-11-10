//! What a pilot keeps and loses when an expedition ends: the Stage 1 loss table
//! of design section 6. The home, its facilities, its stock and every unlock
//! are never at risk; an expedition risks only what the ship carries.

/// How a pilot's run ended.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ending {
    /// The ship docked at home.
    Docked,
    /// The ship was destroyed.
    Destroyed,
    /// The pilot disconnected and the instance ended before they returned.
    Abandoned,
}

/// What happens to one part of the stake.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Fate {
    Kept,
    Lost,
    /// A destroyed hull is replaced at home.
    ReissuedAtHome,
    /// An abandoned ship is brought home.
    ReturnedHome,
    /// The basic fitting can never be lost or sold.
    AlwaysAvailable,
    /// Cargo docked at home joins the home's stock.
    Banked,
    /// Launch supplies are used up by launching, whatever follows.
    Spent,
}

/// One column of the design's table.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Outcome {
    pub hull: Fate,
    pub basic_fitting: Fate,
    pub upgraded_modules: Fate,
    pub carried_cargo: Fate,
    pub launch_supplies: Fate,
}

/// The fate of each part of the stake for an ending (design section 6).
#[must_use]
pub const fn outcome(ending: Ending) -> Outcome {
    match ending {
        Ending::Docked => Outcome {
            hull: Fate::Kept,
            basic_fitting: Fate::Kept,
            upgraded_modules: Fate::Kept,
            carried_cargo: Fate::Banked,
            launch_supplies: Fate::Spent,
        },
        Ending::Destroyed => Outcome {
            hull: Fate::ReissuedAtHome,
            basic_fitting: Fate::AlwaysAvailable,
            upgraded_modules: Fate::Lost,
            carried_cargo: Fate::Lost,
            launch_supplies: Fate::Spent,
        },
        Ending::Abandoned => Outcome {
            hull: Fate::ReturnedHome,
            basic_fitting: Fate::Kept,
            upgraded_modules: Fate::Kept,
            carried_cargo: Fate::Lost,
            launch_supplies: Fate::Spent,
        },
    }
}

/// What a ship carries out on an expedition that could be lost.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Loadout {
    /// Fitted modules beyond the basic fitting (design section 10).
    pub upgraded_modules: u32,
    /// Cargo units in the hold.
    pub cargo: u32,
}

/// How much of a loadout an ending loses.
#[must_use]
pub const fn losses(ending: Ending, loadout: Loadout) -> Loadout {
    let fates = outcome(ending);
    Loadout {
        upgraded_modules: if matches!(fates.upgraded_modules, Fate::Lost) {
            loadout.upgraded_modules
        } else {
            0
        },
        cargo: if matches!(fates.carried_cargo, Fate::Lost) {
            loadout.cargo
        } else {
            0
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The table in design section 6, row by row: docked, destroyed, abandoned.
    #[test]
    fn matches_the_stage_1_loss_table() {
        let [docked, destroyed, abandoned] =
            [Ending::Docked, Ending::Destroyed, Ending::Abandoned].map(outcome);
        let rows = [
            (
                "hull",
                [docked.hull, destroyed.hull, abandoned.hull],
                [Fate::Kept, Fate::ReissuedAtHome, Fate::ReturnedHome],
            ),
            (
                "basic fitting",
                [
                    docked.basic_fitting,
                    destroyed.basic_fitting,
                    abandoned.basic_fitting,
                ],
                [Fate::Kept, Fate::AlwaysAvailable, Fate::Kept],
            ),
            (
                "upgraded modules",
                [
                    docked.upgraded_modules,
                    destroyed.upgraded_modules,
                    abandoned.upgraded_modules,
                ],
                [Fate::Kept, Fate::Lost, Fate::Kept],
            ),
            (
                "carried cargo",
                [
                    docked.carried_cargo,
                    destroyed.carried_cargo,
                    abandoned.carried_cargo,
                ],
                [Fate::Banked, Fate::Lost, Fate::Lost],
            ),
            (
                "launch supplies",
                [
                    docked.launch_supplies,
                    destroyed.launch_supplies,
                    abandoned.launch_supplies,
                ],
                [Fate::Spent, Fate::Spent, Fate::Spent],
            ),
        ];
        for (row, actual, expected) in rows {
            assert_eq!(actual, expected, "{row}");
        }
    }

    #[test]
    fn a_destroyed_ship_loses_its_modules_and_cargo() {
        let loadout = Loadout {
            upgraded_modules: 2,
            cargo: 30,
        };
        assert_eq!(losses(Ending::Destroyed, loadout), loadout);
        assert_eq!(losses(Ending::Docked, loadout), Loadout::default());
        assert_eq!(
            losses(Ending::Abandoned, loadout),
            Loadout {
                upgraded_modules: 0,
                cargo: 30
            }
        );
    }
}
