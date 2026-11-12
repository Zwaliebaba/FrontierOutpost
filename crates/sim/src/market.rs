//! NPC trade and the outpost's processor (design sections 7 and 10).
//!
//! Prices are fixed and posted. Recipes turn a pilot's own haul into goods
//! worth more, but never turn goods bought from NPCs into goods NPCs buy for
//! more. The processor works one batch at a time and each takes a fixed time
//! (`OutpostRules::batch_seconds`), whether or not the pilot is flying or
//! even signed in. This module reads no clock: its caller says what time it
//! is. Every number here is a placeholder from section 10.

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

/// Batches at the processor, by recipe, in the order the recipes were first
/// loaded. Their inputs have left the home's stock. The processor works the
/// first batch of the first recipe; the rest wait their turn.
#[derive(Debug, Clone, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct Queue {
    pub batches: Vec<Queued>,
    /// When the batch being worked was started, in seconds on the caller's
    /// clock (the server's: Unix time). `None` while the processor is idle,
    /// and in documents written before processing took time, whose batches
    /// start when the processor is next looked at.
    #[serde(default)]
    pub started: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Queued {
    pub recipe: String,
    pub batches: u32,
}

/// The batch the processor is working.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Working {
    pub recipe: &'static Recipe,
    /// Seconds of work done on it, at most the time a batch takes.
    pub elapsed: u32,
}

impl Queue {
    /// Batches of `recipe` at the processor, the one being worked included.
    #[must_use]
    pub fn waiting(&self, recipe: &str) -> u32 {
        self.batches
            .iter()
            .find(|queued| queued.recipe == recipe)
            .map_or(0, |queued| queued.batches)
    }

    /// Batches at the processor across every recipe.
    #[must_use]
    pub fn total(&self) -> u32 {
        self.batches
            .iter()
            .fold(0, |total, queued| total.saturating_add(queued.batches))
    }

    /// The recipe whose batch is being worked: the first loaded. A recipe
    /// this build does not know is passed over and never worked.
    fn head(&self) -> Option<&'static Recipe> {
        self.batches
            .iter()
            .find_map(|queued| recipe(&queued.recipe))
    }

    /// Loads `batches` of `recipe` at `now` and returns how many of it are
    /// now at the processor, or `None`, changing nothing, if that is more
    /// than the queue can count. An idle processor starts on them at once.
    pub fn add(&mut self, recipe: &str, batches: u32, now: u64) -> Option<u32> {
        let loaded = match self
            .batches
            .iter_mut()
            .find(|queued| queued.recipe == recipe)
        {
            Some(queued) => {
                queued.batches = queued.batches.checked_add(batches)?;
                queued.batches
            }
            None => {
                self.batches.push(Queued {
                    recipe: recipe.to_owned(),
                    batches,
                });
                batches
            }
        };
        self.started.get_or_insert(now);
        Some(loaded)
    }

    /// Takes up to `batches` of `recipe` back at `now`; returns how many.
    /// Batches not yet started go first. Taking the one being worked loses
    /// the work done on it, and the next batch starts from nothing.
    pub fn take(&mut self, recipe: &str, batches: u32, now: u64) -> u32 {
        let working = self.head().map(|recipe| recipe.id);
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
        let next = self.head().map(|recipe| recipe.id);
        if next.is_none() {
            self.started = None;
        } else if next != working {
            self.started = Some(now);
        }
        taken
    }

    /// Finishes every batch whose time has come by `now`, each taking
    /// `batch_seconds`, in the order they were loaded, and returns how many
    /// of each recipe finished. Work on the next batch began the moment the
    /// one before it finished, so nothing is lost by looking late.
    pub fn advance(&mut self, now: u64, batch_seconds: u32) -> Vec<(&'static Recipe, u32)> {
        if self.head().is_none() {
            self.started = None;
            return Vec::new();
        }
        // Batches from before processing took time start now.
        let started = *self.started.get_or_insert(now);
        let each = u64::from(batch_seconds.max(1));
        // A clock set back finishes nothing; it never undoes a batch.
        let mut due = now.saturating_sub(started) / each;
        let mut done_in_all: u64 = 0;
        let mut finished = Vec::new();
        for queued in &mut self.batches {
            let Some(recipe) = recipe(&queued.recipe) else {
                continue;
            };
            let done = u64::from(queued.batches).min(due);
            if done > 0 {
                // No more than the batches queued, which a u32 counts.
                let done = u32::try_from(done).unwrap_or(u32::MAX);
                queued.batches -= done;
                due -= u64::from(done);
                done_in_all += u64::from(done);
                finished.push((recipe, done));
            }
        }
        self.batches.retain(|queued| queued.batches > 0);
        self.started = self
            .head()
            .map(|_| started.saturating_add(done_in_all.saturating_mul(each)));
        finished
    }

    /// Keeps the share done of the batch being worked when the time a batch
    /// takes changes from `from` to `to` seconds at `now`: a batch half done
    /// is still half done. Call [`Queue::advance`] at the old pace first.
    pub fn repace(&mut self, now: u64, from: u32, to: u32) {
        let Some(started) = self.started else {
            return;
        };
        let from = u64::from(from.max(1));
        let elapsed = now.saturating_sub(started).min(from);
        let scaled = elapsed.saturating_mul(u64::from(to)) / from;
        self.started = Some(now.saturating_sub(scaled));
    }

    /// The batch being worked at `now` and how far along it is. Call
    /// [`Queue::advance`] first: a batch whose time has come still shows
    /// here, complete, until it is finished.
    #[must_use]
    pub fn working(&self, now: u64, batch_seconds: u32) -> Option<Working> {
        let recipe = self.head()?;
        let elapsed = self
            .started
            .map_or(0, |started| now.saturating_sub(started))
            .min(u64::from(batch_seconds));
        Some(Working {
            recipe,
            elapsed: u32::try_from(elapsed).unwrap_or(batch_seconds),
        })
    }

    /// When the batch being worked finishes, on the caller's clock.
    #[must_use]
    pub fn next_due(&self, batch_seconds: u32) -> Option<u64> {
        self.head()?;
        self.started
            .map(|started| started.saturating_add(u64::from(batch_seconds.max(1))))
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
    fn the_processor_works_one_batch_at_a_time_in_the_order_loaded() {
        let mut queue = Queue::default();
        queue.add("alloy", 3, 1_000);
        queue.add("components", 2, 1_010);
        queue.add("alloy", 1, 1_020);
        assert_eq!(queue.waiting("alloy"), 4);
        assert_eq!(queue.total(), 6);
        // Work began when the first batch was loaded, not the last.
        assert_eq!(queue.next_due(240), Some(1_240));
        assert!(queue.advance(1_239, 240).is_empty(), "a second early");
        assert_eq!(queue.working(1_239, 240).map(|w| w.elapsed), Some(239));
        let done = |finished: Vec<(&'static Recipe, u32)>| {
            finished
                .iter()
                .map(|(recipe, done)| (recipe.id, *done))
                .collect::<Vec<_>>()
        };
        assert_eq!(done(queue.advance(1_240, 240)), [("alloy", 1)]);
        assert_eq!(queue.next_due(240), Some(1_480), "the next began at once");
        // Looked at late: five batches' time has passed since the first
        // started, so four more finish, alloy before components.
        assert_eq!(
            done(queue.advance(1_000 + 5 * 240 + 100, 240)),
            [("alloy", 3), ("components", 1)]
        );
        let working = queue.working(1_000 + 5 * 240 + 100, 240).unwrap();
        assert_eq!((working.recipe.id, working.elapsed), ("components", 100));
        assert_eq!(done(queue.advance(u64::MAX, 240)), [("components", 1)]);
        assert_eq!(queue, Queue::default(), "idle again, with no start time");
    }

    #[test]
    fn a_change_of_pace_keeps_the_share_done_of_the_batch_in_hand() {
        let mut queue = Queue::default();
        queue.add("alloy", 2, 1_000);
        // Half done at four minutes a batch; then two minutes a batch.
        assert!(queue.advance(1_120, 240).is_empty());
        queue.repace(1_120, 240, 120);
        assert_eq!(queue.working(1_120, 120).map(|w| w.elapsed), Some(60));
        assert_eq!(queue.next_due(120), Some(1_180));
        // The time already spent does not spill into the batch behind it.
        assert_eq!(queue.advance(1_180, 120).len(), 1);
        assert_eq!(queue.next_due(120), Some(1_300));
        // Slower again: the share done holds the other way too.
        queue.repace(1_240, 120, 240);
        assert_eq!(queue.working(1_240, 240).map(|w| w.elapsed), Some(120));
        Queue::default().repace(5, 240, 120);
    }

    #[test]
    fn a_clock_set_back_finishes_nothing_and_undoes_nothing() {
        let mut queue = Queue::default();
        queue.add("alloy", 1, 5_000);
        assert!(queue.advance(4_000, 240).is_empty());
        assert_eq!(queue.working(4_000, 240).map(|w| w.elapsed), Some(0));
        assert_eq!(queue.waiting("alloy"), 1);
    }

    #[test]
    fn batches_saved_before_processing_took_time_start_when_first_looked_at() {
        let mut queue: Queue =
            serde_json::from_str(r#"{"batches":[{"recipe":"alloy","batches":2}]}"#).unwrap();
        assert_eq!(queue.started, None);
        assert!(queue.advance(9_000, 240).is_empty());
        assert_eq!(queue.next_due(240), Some(9_240));
        assert_eq!(queue.advance(9_240, 240).len(), 1);
    }

    #[test]
    fn loaded_batches_can_be_taken_back_and_the_one_in_hand_loses_its_work() {
        let mut queue = Queue::default();
        queue.add("alloy", 2, 0);
        queue.add("components", 1, 0);
        // The alloy not yet started goes first: the one in hand keeps its work.
        assert_eq!(queue.take("alloy", 1, 100), 1);
        assert_eq!(queue.working(100, 240).map(|w| w.elapsed), Some(100));
        // Taking the one in hand: components start from nothing.
        assert_eq!(queue.take("alloy", 5, 150), 1);
        let working = queue.working(150, 240).unwrap();
        assert_eq!((working.recipe.id, working.elapsed), ("components", 0));
        assert_eq!(queue.take("alloy", 1, 150), 0);
        assert_eq!(queue.take("components", 1, 160), 1);
        assert_eq!(queue, Queue::default());
    }

    #[test]
    fn a_queue_refuses_more_batches_than_it_can_count() {
        let mut queue = Queue::default();
        assert_eq!(queue.add("alloy", u32::MAX - 1, 0), Some(u32::MAX - 1));
        assert_eq!(queue.add("alloy", 1, 0), Some(u32::MAX));
        let full = queue.clone();
        assert_eq!(queue.add("alloy", 1, 0), None);
        assert_eq!(queue, full, "a refusal changes nothing");
        assert_eq!(queue.add("components", 7, 0), Some(7));
        assert_eq!(queue.total(), u32::MAX, "the total saturates");
    }
}
