//! The ledger (design section 7): every change to a pilot's stock is a transfer
//! that names its item, amount, source, sink and reason. [`Store::transfer`]
//! is the only way stock changes: it applies transfers to the account
//! documents and appends them to the ledger in one transaction, so the ledger
//! folded always equals the stock saved.
//!
//! [`Store::transfer`]: crate::store::Store::transfer

use std::fmt;

use crate::account::Account;
use crate::store::AccountId;

/// What moves.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Item {
    Credits,
    Ore,
    Salvage,
    Volatiles,
}

impl Item {
    pub const ALL: [Self; 4] = [Self::Credits, Self::Ore, Self::Salvage, Self::Volatiles];

    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            Self::Credits => "credits",
            Self::Ore => "ore",
            Self::Salvage => "salvage",
            Self::Volatiles => "volatiles",
        }
    }

    /// The account's holding of this item.
    #[must_use]
    pub fn held(self, account: &Account) -> u32 {
        match self {
            Self::Credits => account.credits,
            Self::Ore => account.stock.ore,
            Self::Salvage => account.stock.salvage,
            Self::Volatiles => account.stock.volatiles,
        }
    }

    fn holding(self, account: &mut Account) -> &mut u32 {
        match self {
            Self::Credits => &mut account.credits,
            Self::Ore => &mut account.stock.ore,
            Self::Salvage => &mut account.stock.salvage,
            Self::Volatiles => &mut account.stock.volatiles,
        }
    }
}

/// The NPC side of the economy: where credits and goods come from and go
/// (design section 7: sources and sinks).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Npc {
    /// A new pilot's starting balance.
    Grant,
    /// Launch supplies: a sink.
    Supplies,
    /// Contract payouts: a source.
    Contract,
}

/// One side of a transfer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Party {
    Account(AccountId),
    Npc(Npc),
    /// An expedition's site: where banked cargo was extracted.
    Site,
}

impl fmt::Display for Party {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Account(id) => write!(f, "account:{}", id.0),
            Self::Npc(Npc::Grant) => f.write_str("npc:grant"),
            Self::Npc(Npc::Supplies) => f.write_str("npc:supplies"),
            Self::Npc(Npc::Contract) => f.write_str("npc:contract"),
            Self::Site => f.write_str("site"),
        }
    }
}

/// Why something moved.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reason {
    StartingBalance,
    LaunchSupplies,
    /// Cargo docked at home (design section 6).
    Banked,
    ContractPayout,
}

impl Reason {
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            Self::StartingBalance => "starting balance",
            Self::LaunchSupplies => "launch supplies",
            Self::Banked => "banked cargo",
            Self::ContractPayout => "contract payout",
        }
    }
}

/// One movement of one item.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Transfer {
    pub item: Item,
    /// Always positive: a transfer of nothing is not recorded.
    pub amount: u32,
    pub from: Party,
    pub to: Party,
    pub reason: Reason,
    /// The expedition run it belongs to, if any.
    pub run: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum TransferError {
    #[error("a transfer of no {0} is not recorded")]
    Empty(&'static str),
    #[error("account {account} holds {held} {item}, not the {amount} to transfer")]
    Insufficient {
        account: i64,
        item: &'static str,
        held: u32,
        amount: u32,
    },
    #[error("the balance would overflow")]
    Overflow,
}

impl Transfer {
    /// Applies the transfer to `account`, numbered `id`, if it is a party.
    ///
    /// # Errors
    /// Fails if the amount is zero, the account cannot cover it, or the
    /// result would overflow. Nothing changes on failure.
    pub fn apply(&self, id: AccountId, account: &mut Account) -> Result<(), TransferError> {
        if self.amount == 0 {
            return Err(TransferError::Empty(self.item.name()));
        }
        if self.from == Party::Account(id) {
            let holding = self.item.holding(account);
            *holding = holding
                .checked_sub(self.amount)
                .ok_or(TransferError::Insufficient {
                    account: id.0,
                    item: self.item.name(),
                    held: *holding,
                    amount: self.amount,
                })?;
        }
        if self.to == Party::Account(id) {
            let holding = self.item.holding(account);
            *holding = holding
                .checked_add(self.amount)
                .ok_or(TransferError::Overflow)?;
        }
        Ok(())
    }

    /// The accounts this transfer touches.
    pub(crate) fn accounts(&self) -> impl Iterator<Item = AccountId> {
        [self.from, self.to]
            .into_iter()
            .filter_map(|party| match party {
                Party::Account(id) => Some(id),
                _ => None,
            })
    }
}

/// What launching costs a pilot who holds `credits`: the posted supplies
/// price, or everything they have if that is less, so no pilot is ever
/// locked out of play (the owner's rule until trading arrives in plan M5).
#[must_use]
pub fn supplies_due(credits: u32) -> u32 {
    credits.min(frontier_sim::run::LAUNCH_COST)
}

#[cfg(test)]
mod tests {
    use super::*;

    const ME: AccountId = AccountId(1);

    fn transfer(item: Item, amount: u32, from: Party, to: Party) -> Transfer {
        Transfer {
            item,
            amount,
            from,
            to,
            reason: Reason::Banked,
            run: None,
        }
    }

    #[test]
    fn credits_and_debits_only_the_account_named() {
        let mut account = Account::new("w", "Vega");
        transfer(Item::Ore, 5, Party::Site, Party::Account(ME))
            .apply(ME, &mut account)
            .unwrap();
        assert_eq!(account.stock.ore, 5);
        transfer(Item::Ore, 2, Party::Account(ME), Party::Npc(Npc::Supplies))
            .apply(ME, &mut account)
            .unwrap();
        assert_eq!(account.stock.ore, 3);
        transfer(Item::Ore, 9, Party::Site, Party::Account(AccountId(2)))
            .apply(ME, &mut account)
            .unwrap();
        assert_eq!(account.stock.ore, 3, "another account's transfer");
    }

    #[test]
    fn refuses_to_overdraw_or_to_record_nothing() {
        let mut account = Account::new("w", "Vega");
        let error = transfer(
            Item::Credits,
            1,
            Party::Account(ME),
            Party::Npc(Npc::Supplies),
        )
        .apply(ME, &mut account)
        .unwrap_err();
        assert!(matches!(error, TransferError::Insufficient { held: 0, .. }));
        assert_eq!(
            transfer(Item::Ore, 0, Party::Site, Party::Account(ME)).apply(ME, &mut account),
            Err(TransferError::Empty("ore"))
        );
    }

    #[test]
    fn supplies_cost_the_posted_price_or_what_is_left() {
        assert_eq!(supplies_due(100), 40);
        assert_eq!(supplies_due(40), 40);
        assert_eq!(supplies_due(25), 25);
        assert_eq!(supplies_due(0), 0);
    }

    #[test]
    fn names_each_party_for_the_ledger() {
        assert_eq!(Party::Account(AccountId(7)).to_string(), "account:7");
        assert_eq!(Party::Npc(Npc::Supplies).to_string(), "npc:supplies");
        assert_eq!(Party::Site.to_string(), "site");
    }
}
