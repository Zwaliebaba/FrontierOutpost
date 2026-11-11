//! What a pilot does at home between runs (design sections 2, 6 and 7):
//! build, upgrade and reconfigure the outpost, trade with NPCs at posted
//! prices, queue processing, and outfit the ship.
//!
//! Each action edits the account document and returns the ledger transfers
//! that pay for it; [`crate::store::Store::update`] saves both together.

use frontier_sim::fitting::{self, Hangar, Launch};
use frontier_sim::goods::{Cost, Good};
use frontier_sim::market;
use frontier_sim::outpost::{Action as Change, STAGE1};
use frontier_sim::run::LAUNCH_COST;
use frontier_sim::site::Capability;

use crate::account::Account;
use crate::ledger::{Item, Npc, Party, Reason, Transfer};
use crate::store::AccountId;

/// Something a pilot asks for at home.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Action {
    Install {
        socket: String,
        facility: String,
    },
    Upgrade {
        socket: String,
    },
    Remove {
        socket: String,
    },
    Power {
        socket: String,
        run: u32,
    },
    Sell {
        good: Good,
        amount: u32,
    },
    Buy {
        good: Good,
        amount: u32,
    },
    Process {
        recipe: String,
        batches: u32,
    },
    Unqueue {
        recipe: String,
        batches: u32,
    },
    BuyFitting {
        fitting: String,
    },
    SaveFitting {
        rig: Option<String>,
        modules: Vec<String>,
    },
}

impl Action {
    /// Whether the action can change what the contract shows: the pockets
    /// the layout opens, or the tiers its unlocks open.
    #[must_use]
    pub const fn changes_outpost(&self) -> bool {
        matches!(
            self,
            Self::Install { .. } | Self::Upgrade { .. } | Self::Remove { .. } | Self::Power { .. }
        )
    }
}

/// What the home's powered layout grants the expedition.
#[must_use]
pub fn capabilities(account: &Account) -> Vec<Capability> {
    STAGE1.capabilities(&account.outpost)
}

/// The rig and module price discount the layout earns.
#[must_use]
pub fn discount(account: &Account) -> u32 {
    STAGE1.fitting_discount(&account.outpost)
}

/// What launching now would carry.
#[must_use]
pub fn launch(account: &Account) -> Launch {
    account.hangar.launch(&capabilities(account))
}

/// The highest threat tier this home has opened.
#[must_use]
pub fn open_tiers(account: &Account) -> u32 {
    STAGE1.open_tiers(&account.outpost)
}

/// The refusal for a count whose price or total does not fit the books.
/// Counts come from the client as any `u32`, so every product and sum of one
/// is checked: unchecked, a wrapped price is a free purchase.
fn too_large() -> String {
    "That amount is too large.".to_owned()
}

fn transfer(item: Item, amount: u32, from: Party, to: Party, reason: Reason) -> Option<Transfer> {
    (amount > 0).then_some(Transfer {
        item,
        amount,
        from,
        to,
        reason,
        run: None,
    })
}

fn describe(credits: u32, goods: &[(Good, u32)]) -> String {
    let mut parts = Vec::new();
    if credits > 0 || goods.is_empty() {
        parts.push(format!("{credits} cr"));
    }
    parts.extend(
        goods
            .iter()
            .map(|(good, amount)| format!("{amount} {}", good.name())),
    );
    parts.join(" and ")
}

/// Refuses, with what is missing, if `account` cannot pay `credits` and
/// `goods`.
fn afford(account: &Account, credits: u32, goods: &[(Good, u32)]) -> Result<(), String> {
    let short = account.credits < credits
        || goods
            .iter()
            .any(|&(good, amount)| account.stock.get(good) < amount);
    if short {
        let held: Vec<(Good, u32)> = goods
            .iter()
            .map(|&(good, _)| (good, account.stock.get(good)))
            .collect();
        return Err(format!(
            "That needs {}; you have {}.",
            describe(credits, goods),
            describe(account.credits, &held)
        ));
    }
    Ok(())
}

/// The transfers that pay `cost` from `account` to `sink`.
fn pay(id: AccountId, cost: &Cost, sink: Npc, reason: Reason) -> Vec<Transfer> {
    let from = Party::Account(id);
    std::iter::once(transfer(
        Item::Credits,
        cost.credits,
        from,
        Party::Npc(sink),
        reason,
    ))
    .chain(
        cost.goods.iter().map(|&(good, amount)| {
            transfer(Item::Good(good), amount, from, Party::Npc(sink), reason)
        }),
    )
    .flatten()
    .collect()
}

/// Applies `action` to `account`, numbered `id`, and returns the transfers
/// that pay for it.
///
/// # Errors
/// Refuses, with a reason to show the pilot, anything the rules or the
/// pilot's holdings do not allow. Nothing changes on refusal.
pub fn act(id: AccountId, account: &mut Account, action: &Action) -> Result<Vec<Transfer>, String> {
    let me = Party::Account(id);
    match action {
        Action::Install { socket, facility } => {
            let change = STAGE1
                .install(&account.outpost, socket, facility)
                .map_err(|refusal| sentence(&refusal))?;
            afford(account, change.cost.credits, change.cost.goods)?;
            account.outpost = change.outpost;
            if let Some(rig) = change.rig.and_then(fitting::fitting) {
                account.hangar.add(rig);
                // The first rig comes with its facility and flies next run.
                if account.hangar.saved.rig.is_none() {
                    account.hangar.saved.rig = Some(rig.id.to_owned());
                }
            }
            debug_assert!(matches!(
                change.action,
                Change::Bought | Change::Reinstalled
            ));
            Ok(pay(
                id,
                &change.cost,
                Npc::Construction,
                Reason::Construction,
            ))
        }
        Action::Upgrade { socket } => {
            let change = STAGE1
                .upgrade(&account.outpost, socket)
                .map_err(|refusal| sentence(&refusal))?;
            afford(account, change.cost.credits, change.cost.goods)?;
            account.outpost = change.outpost;
            Ok(pay(
                id,
                &change.cost,
                Npc::Construction,
                Reason::Construction,
            ))
        }
        Action::Remove { socket } => {
            let change = STAGE1
                .remove(&account.outpost, socket)
                .map_err(|refusal| sentence(&refusal))?;
            account.outpost = change.outpost;
            Ok(Vec::new())
        }
        Action::Power { socket, run } => {
            let change = STAGE1
                .power(&account.outpost, socket, *run)
                .map_err(|refusal| sentence(&refusal))?;
            account.outpost = change.outpost;
            Ok(Vec::new())
        }
        &Action::Sell { good, amount } => {
            if amount == 0 {
                return Err("Sell at least one.".to_owned());
            }
            afford(account, 0, &[(good, amount)])?;
            let paid = market::price(good)
                .buys_at
                .checked_mul(amount)
                .ok_or_else(too_large)?;
            let market = Party::Npc(Npc::Market);
            Ok([
                transfer(Item::Good(good), amount, me, market, Reason::Sold),
                transfer(Item::Credits, paid, market, me, Reason::Sold),
            ]
            .into_iter()
            .flatten()
            .collect())
        }
        &Action::Buy { good, amount } => {
            if amount == 0 {
                return Err("Buy at least one.".to_owned());
            }
            let cost = market::price(good)
                .sells_at
                .checked_mul(amount)
                .ok_or_else(too_large)?;
            afford(account, cost, &[])?;
            let market = Party::Npc(Npc::Market);
            Ok([
                transfer(Item::Credits, cost, me, market, Reason::Bought),
                transfer(Item::Good(good), amount, market, me, Reason::Bought),
            ]
            .into_iter()
            .flatten()
            .collect())
        }
        Action::Process { recipe, batches } => {
            let rule =
                market::recipe(recipe).ok_or_else(|| format!("There is no recipe {recipe}."))?;
            if *batches == 0 {
                return Err("Queue at least one batch.".to_owned());
            }
            let (input, per_batch) = rule.input;
            let needed = per_batch.checked_mul(*batches).ok_or_else(too_large)?;
            afford(account, 0, &[(input, needed)])?;
            account
                .processing
                .add(rule.id, *batches)
                .ok_or_else(too_large)?;
            Ok(transfer(
                Item::Good(input),
                needed,
                me,
                Party::Processor,
                Reason::Queued,
            )
            .into_iter()
            .collect())
        }
        Action::Unqueue { recipe, batches } => {
            let rule =
                market::recipe(recipe).ok_or_else(|| format!("There is no recipe {recipe}."))?;
            let (input, per_batch) = rule.input;
            // Priced before anything leaves the queue, so a refusal changes nothing.
            let returned = account
                .processing
                .waiting(rule.id)
                .min(*batches)
                .checked_mul(per_batch)
                .ok_or_else(too_large)?;
            let taken = account.processing.take(rule.id, *batches);
            if taken == 0 {
                return Err(format!("No {} batches are queued.", rule.id));
            }
            Ok(transfer(
                Item::Good(input),
                returned,
                Party::Processor,
                me,
                Reason::Unqueued,
            )
            .into_iter()
            .collect())
        }
        Action::BuyFitting { fitting } => {
            let rule = Hangar::may_buy(fitting, &capabilities(account))
                .map_err(|refusal| sentence(&refusal))?;
            let price = fitting::discounted(rule.price, discount(account));
            afford(account, price, &[])?;
            account.hangar.add(rule);
            Ok(pay(
                id,
                &Cost::credits(price),
                Npc::Outfitter,
                Reason::Outfitting,
            ))
        }
        Action::SaveFitting { rig, modules } => {
            let modules: Vec<&str> = modules.iter().map(String::as_str).collect();
            account
                .hangar
                .save(rig.as_deref(), &modules)
                .map_err(|refusal| sentence(&refusal))?;
            Ok(Vec::new())
        }
    }
}

/// Finishes processing after a completed run, up to the processor's
/// capacity, and returns the transfers of finished goods.
pub fn finish_processing(id: AccountId, account: &mut Account, run: &str) -> Vec<Transfer> {
    let capacity = STAGE1.processor_batches(&account.outpost);
    account
        .processing
        .finish(capacity)
        .into_iter()
        .filter_map(|(recipe, batches)| {
            let (output, per_batch) = recipe.output;
            let mut transfer = transfer(
                Item::Good(output),
                per_batch * batches,
                Party::Processor,
                Party::Account(id),
                Reason::Processed,
            )?;
            transfer.run = Some(run.to_owned());
            Some(transfer)
        })
        .collect()
}

/// What launching costs a pilot: the posted supplies price. A pilot short of
/// credits sells stock first; one with too little left to cover it pays what
/// they have, so nobody is ever locked out of play.
///
/// # Errors
/// Refuses a pilot who could cover the price by selling stock.
pub fn supplies_due(account: &Account) -> Result<u32, String> {
    if account.credits >= LAUNCH_COST {
        return Ok(LAUNCH_COST);
    }
    // In u64, where no stock a u32 can count overflows its value.
    let stock_value: u64 = Good::ALL
        .iter()
        .map(|&good| u64::from(account.stock.get(good)) * u64::from(market::price(good).buys_at))
        .sum();
    if u64::from(account.credits) + stock_value >= u64::from(LAUNCH_COST) {
        return Err(format!(
            "Supplies cost {LAUNCH_COST} cr and you have {} cr: sell some stock first.",
            account.credits
        ));
    }
    Ok(account.credits)
}

/// A refusal as a sentence for the pilot.
fn sentence(refusal: &impl std::fmt::Display) -> String {
    let text = refusal.to_string();
    let mut chars = text.chars();
    match chars.next() {
        Some(first) => format!("{}{}.", first.to_uppercase(), chars.as_str()),
        None => text,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const ME: AccountId = AccountId(1);

    fn rich() -> Account {
        let mut account = Account::new("w", "Vega");
        account.credits = 2_000;
        account.stock.ore = 40;
        account.stock.salvage = 20;
        account.stock.alloy = 30;
        account.stock.components = 10;
        account
    }

    /// Applies an action and its transfers, as the store would.
    fn run(account: &mut Account, action: Action) -> Result<Vec<Transfer>, String> {
        let transfers = act(ME, account, &action)?;
        for transfer in &transfers {
            transfer.apply(ME, account).unwrap();
        }
        Ok(transfers)
    }

    #[test]
    fn building_pays_the_price_and_brings_and_fits_the_rig() {
        let mut home = rich();
        let transfers = run(
            &mut home,
            Action::Install {
                socket: "a".into(),
                facility: "refinery".into(),
            },
        )
        .unwrap();
        assert_eq!(home.credits, 1_850);
        assert_eq!(home.stock.ore, 30);
        assert!(
            transfers
                .iter()
                .all(|t| t.to == Party::Npc(Npc::Construction) && t.reason == Reason::Construction)
        );
        assert_eq!(home.hangar.count("extraction_rig"), 1);
        assert_eq!(home.hangar.saved.rig.as_deref(), Some("extraction_rig"));
        assert!(capabilities(&home).contains(&Capability::WorkVolatiles));
    }

    #[test]
    fn a_first_run_funds_one_facility() {
        // Design section 10: after the first run a new player holds 260 cr
        // and 10 ore, enough for one tier 1 facility.
        let mut home = Account::new("w", "Vega");
        home.credits = 260;
        home.stock.ore = 10;
        run(
            &mut home,
            Action::Install {
                socket: "a".into(),
                facility: "sensor_array".into(),
            },
        )
        .unwrap();
        assert_eq!(home.credits, 110);
        assert_eq!(home.stock.ore, 0);
    }

    #[test]
    fn a_pilot_who_cannot_pay_is_told_what_is_missing() {
        let mut home = Account::new("w", "Vega");
        home.credits = 100;
        let before = home.clone();
        let refusal = run(
            &mut home,
            Action::Install {
                socket: "a".into(),
                facility: "refinery".into(),
            },
        )
        .unwrap_err();
        assert_eq!(
            refusal,
            "That needs 150 cr and 10 ore; you have 100 cr and 0 ore."
        );
        assert_eq!(home, before);
        let refusal = run(
            &mut home,
            Action::Install {
                socket: "b".into(),
                facility: "refinery".into(),
            },
        )
        .unwrap_err();
        assert_eq!(
            refusal,
            "Socket B opens when the first facility is installed."
        );
    }

    #[test]
    fn trades_at_the_posted_prices() {
        let mut home = rich();
        run(
            &mut home,
            Action::Sell {
                good: Good::Ore,
                amount: 10,
            },
        )
        .unwrap();
        assert_eq!((home.credits, home.stock.ore), (2_100, 30));
        run(
            &mut home,
            Action::Buy {
                good: Good::Components,
                amount: 2,
            },
        )
        .unwrap();
        assert_eq!((home.credits, home.stock.components), (1_960, 12));
        assert!(
            run(
                &mut home,
                Action::Sell {
                    good: Good::Volatiles,
                    amount: 1
                }
            )
            .is_err()
        );
    }

    #[test]
    fn processing_takes_inputs_now_and_gives_outputs_after_a_completed_run() {
        let mut home = rich();
        run(
            &mut home,
            Action::Process {
                recipe: "alloy".into(),
                batches: 6,
            },
        )
        .unwrap();
        assert_eq!(home.stock.ore, 28);
        assert_eq!(home.processing.waiting("alloy"), 6);
        run(
            &mut home,
            Action::Unqueue {
                recipe: "alloy".into(),
                batches: 1,
            },
        )
        .unwrap();
        assert_eq!(home.stock.ore, 30);
        // The core processor finishes 4 of the 5 batches.
        for transfer in finish_processing(ME, &mut home, "r1") {
            transfer.apply(ME, &mut home).unwrap();
        }
        assert_eq!(home.stock.alloy, 34);
        assert_eq!(home.processing.waiting("alloy"), 1);
    }

    #[test]
    fn modules_are_bought_at_list_price_less_the_adjacency_discount() {
        let mut home = rich();
        assert!(
            run(
                &mut home,
                Action::BuyFitting {
                    fitting: "cargo_frame".into()
                }
            )
            .is_err()
        );
        run(
            &mut home,
            Action::Install {
                socket: "a".into(),
                facility: "refinery".into(),
            },
        )
        .unwrap();
        run(
            &mut home,
            Action::Install {
                socket: "b".into(),
                facility: "fabrication_bay".into(),
            },
        )
        .unwrap();
        let credits = home.credits;
        run(
            &mut home,
            Action::BuyFitting {
                fitting: "cargo_frame".into(),
            },
        )
        .unwrap();
        assert_eq!(credits - home.credits, 24, "30 cr less 20%");
        run(
            &mut home,
            Action::SaveFitting {
                rig: Some("extraction_rig".into()),
                modules: vec!["cargo_frame".into()],
            },
        )
        .unwrap();
        let launch = launch(&home);
        assert_eq!(launch.loadout().capacity, 38);
        assert_eq!(launch.stake(discount(&home)), 72, "60 and 30, less 20%");
    }

    #[test]
    fn supplies_cost_the_posted_price_or_a_sale_or_what_is_left() {
        let mut home = Account::new("w", "Vega");
        home.credits = 100;
        assert_eq!(supplies_due(&home), Ok(40));
        home.credits = 25;
        assert_eq!(supplies_due(&home), Ok(25), "nothing to sell");
        home.stock.ore = 2;
        assert!(supplies_due(&home).is_err(), "two ore cover the rest");
        home.stock.ore = 1;
        assert_eq!(supplies_due(&home), Ok(25));
    }

    /// The counts a client may send for which an unchecked price wraps:
    /// ore sells at 16, so 2^28 of it once cost nothing.
    const HUGE: [u32; 4] = [1 << 28, (1 << 28) + 5, 1 << 31, u32::MAX];

    #[test]
    fn buying_more_than_any_price_can_count_is_refused_and_changes_nothing() {
        for amount in HUGE {
            let mut home = rich();
            let before = home.clone();
            for good in Good::ALL {
                assert!(
                    run(&mut home, Action::Buy { good, amount }).is_err(),
                    "{amount} {}",
                    good.name()
                );
            }
            assert_eq!(home, before, "{amount}");
        }
    }

    #[test]
    fn selling_a_hold_worth_more_than_credits_can_count_is_refused() {
        // Only a stock that is already wrong can reach this; it must still
        // not pay a wrapped price.
        let mut home = rich();
        home.stock.ore = u32::MAX;
        let before = home.clone();
        let sale = Action::Sell {
            good: Good::Ore,
            amount: u32::MAX,
        };
        assert!(run(&mut home, sale).is_err());
        assert_eq!(home, before);
    }

    #[test]
    fn queueing_more_batches_than_can_be_counted_is_refused_and_changes_nothing() {
        for batches in HUGE {
            let mut home = rich();
            let before = home.clone();
            let queue = Action::Process {
                recipe: "alloy".into(),
                batches,
            };
            assert!(run(&mut home, queue).is_err(), "{batches}");
            assert_eq!(home, before, "{batches}");
        }
        // A queue already at the limit takes no more, and keeps the ore.
        let mut home = rich();
        home.processing.add("alloy", u32::MAX);
        let before = home.clone();
        let queue = Action::Process {
            recipe: "alloy".into(),
            batches: 1,
        };
        assert!(run(&mut home, queue).is_err());
        assert_eq!(home, before);
    }

    #[test]
    fn taking_back_more_inputs_than_can_be_counted_is_refused_and_changes_nothing() {
        let mut home = rich();
        home.processing.add("alloy", u32::MAX);
        let before = home.clone();
        let unqueue = Action::Unqueue {
            recipe: "alloy".into(),
            batches: u32::MAX,
        };
        assert!(run(&mut home, unqueue).is_err());
        assert_eq!(home, before);
        // A count the stock can hold still comes back, two ore a batch.
        let unqueue = Action::Unqueue {
            recipe: "alloy".into(),
            batches: 500,
        };
        run(&mut home, unqueue).unwrap();
        assert_eq!(home.stock.ore, before.stock.ore + 1_000);
        assert_eq!(home.processing.waiting("alloy"), u32::MAX - 500);
    }

    #[test]
    fn supplies_are_priced_without_overflow_for_any_stock() {
        let mut home = Account::new("w", "Vega");
        home.credits = 0;
        for good in Good::ALL {
            *home.stock.get_mut(good) = u32::MAX;
        }
        assert!(supplies_due(&home).is_err(), "there is stock to sell");
    }
}
