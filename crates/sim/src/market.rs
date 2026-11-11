//! NPC trade and the outpost's processor (design sections 7 and 10).
//!
//! Prices are fixed and posted. Recipes turn a pilot's own haul into goods
//! worth more, but never turn goods bought from NPCs into goods NPCs buy for
//! more. Processing advances once per completed run, never with time: batches
//! queued at the dock finish when the pilot next docks from a run, up to the
//! processor's capacity. Every number here is a placeholder from section 10.

use serde::{Deserialize, Serialize};

use crate::goods::Good;

/// What NPCs pay for a good and what they charge for it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Price {
    pub good: Good,
    /// NPCs buy at this price: what a pilot is paid when selling.
    pub buys_at: u32,
    /// NPCs sell at this price: what a pilot pays when buying.
    pub sells_at: u32,
}

pub const PRICES: [Price; 5] = [
    Price {
        good: Good::Ore,
        buys_at: 10,
        sells_at: 16,
    },
    Price {
        good: Good::Salvage,
        buys_at: 18,
        sells_at: 28,
    },
    Price {
        good: Good::Volatiles,
        buys_at: 22,
        sells_at: 36,
    },
    Price {
        good: Good::Alloy,
        buys_at: 26,
        sells_at: 40,
    },
    Price {
        good: Good::Components,
        buys_at: 46,
        sells_at: 70,
    },
];

/// The posted price of `good`.
#[must_use]
pub fn price(good: Good) -> Price {
    PRICES[good.index()]
}

/// One batch: `input` units of one good become `output` units of another.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Recipe {
    pub id: &'static str,
    pub input: (Good, u32),
    pub output: (Good, u32),
}

pub const RECIPES: [Recipe; 2] = [
    Recipe {
        id: "alloy",
        input: (Good::Ore, 2),
        output: (Good::Alloy, 1),
    },
    Recipe {
        id: "components",
        input: (Good::Salvage, 2),
        output: (Good::Components, 1),
    },
];

/// The recipe called `id`.
#[must_use]
pub fn recipe(id: &str) -> Option<&'static Recipe> {
    RECIPES.iter().find(|recipe| recipe.id == id)
}

/// Batches waiting at the processor, by recipe. Their inputs have left the
/// home's stock already.
#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Queue {
    pub batches: Vec<Queued>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Queued {
    pub recipe: String,
    pub batches: u32,
}

impl Queue {
    /// Batches of `recipe` waiting.
    #[must_use]
    pub fn waiting(&self, recipe: &str) -> u32 {
        self.batches
            .iter()
            .find(|queued| queued.recipe == recipe)
            .map_or(0, |queued| queued.batches)
    }

    /// Batches waiting across every recipe.
    #[must_use]
    pub fn total(&self) -> u32 {
        self.batches.iter().map(|queued| queued.batches).sum()
    }

    /// Adds `batches` of `recipe` to the queue.
    pub fn add(&mut self, recipe: &str, batches: u32) {
        match self
            .batches
            .iter_mut()
            .find(|queued| queued.recipe == recipe)
        {
            Some(queued) => queued.batches += batches,
            None => self.batches.push(Queued {
                recipe: recipe.to_owned(),
                batches,
            }),
        }
    }

    /// Takes up to `batches` of `recipe` back off the queue; returns how many.
    pub fn take(&mut self, recipe: &str, batches: u32) -> u32 {
        let Some(queued) = self
            .batches
            .iter_mut()
            .find(|queued| queued.recipe == recipe)
        else {
            return 0;
        };
        let taken = queued.batches.min(batches);
        queued.batches -= taken;
        self.batches.retain(|queued| queued.batches > 0);
        taken
    }

    /// Finishes up to `capacity` batches, in the order they were queued, and
    /// returns how many of each recipe finished. The rest wait for the next
    /// completed run.
    pub fn finish(&mut self, capacity: u32) -> Vec<(&'static Recipe, u32)> {
        let mut left = capacity;
        let mut finished = Vec::new();
        for queued in &mut self.batches {
            let Some(recipe) = recipe(&queued.recipe) else {
                continue;
            };
            let done = queued.batches.min(left);
            if done > 0 {
                queued.batches -= done;
                left -= done;
                finished.push((recipe, done));
            }
        }
        self.batches.retain(|queued| queued.batches > 0);
        finished
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn npcs_sell_every_good_for_more_than_they_buy_it() {
        for price in PRICES {
            assert!(price.sells_at > price.buys_at, "{}", price.good.name());
        }
        for good in Good::ALL {
            assert_eq!(price(good).good, good, "PRICES is in Good order");
        }
    }

    #[test]
    fn no_recipe_is_an_arbitrage() {
        // Design section 7: inputs bought from NPCs cost more than NPCs pay
        // for the output (32 against 26; 56 against 46).
        for recipe in RECIPES {
            let (input, needed) = recipe.input;
            let (output, made) = recipe.output;
            let bought = price(input).sells_at * needed;
            let sold = price(output).buys_at * made;
            assert!(bought > sold, "{}: {bought} buys {sold}", recipe.id);
        }
    }

    #[test]
    fn processing_a_pilots_own_haul_still_gains() {
        // 20 becomes 26; 36 becomes 46.
        for recipe in RECIPES {
            let (input, needed) = recipe.input;
            let (output, made) = recipe.output;
            assert!(price(output).buys_at * made > price(input).buys_at * needed);
        }
        assert_eq!(price(Good::Ore).buys_at * 2, 20);
        assert_eq!(price(Good::Alloy).buys_at, 26);
    }

    #[test]
    fn a_completed_run_finishes_batches_up_to_capacity_in_order() {
        let mut queue = Queue::default();
        queue.add("alloy", 3);
        queue.add("components", 2);
        queue.add("alloy", 1);
        assert_eq!(queue.waiting("alloy"), 4);
        assert_eq!(queue.total(), 6);
        let finished = queue.finish(5);
        assert_eq!(
            finished
                .iter()
                .map(|(recipe, done)| (recipe.id, *done))
                .collect::<Vec<_>>(),
            [("alloy", 4), ("components", 1)]
        );
        assert_eq!(queue.total(), 1, "one batch waits for the next run");
        assert_eq!(queue.finish(4).len(), 1);
        assert_eq!(queue, Queue::default());
    }

    #[test]
    fn queued_batches_can_be_taken_back() {
        let mut queue = Queue::default();
        queue.add("alloy", 2);
        assert_eq!(queue.take("alloy", 5), 2);
        assert_eq!(queue.take("components", 1), 0);
        assert_eq!(queue, Queue::default());
    }
}
