//! Who is flying what, across sessions: a pilot's connection can drop and come
//! back, or the server can restart, while their run goes on (design section
//! 6). A signing-in pilot finds their run here and rejoins it.
//!
//! It also keeps one session per pilot: signing in again elsewhere tells the
//! older session to leave, so two pages never fly one ship.

use std::collections::HashMap;
use std::sync::{Arc, Mutex, MutexGuard};

use frontier_sim::world::ShipId;
use tokio::sync::Notify;

use crate::expedition::Expedition;
use crate::store::AccountId;

/// A pilot's place in a live run.
#[derive(Clone)]
pub struct Flying {
    /// Names the run in the log, the ledger and the checkpoints.
    pub run: String,
    pub expedition: Expedition,
    pub ship: ShipId,
}

#[derive(Default)]
struct Inner {
    flying: HashMap<AccountId, Flying>,
    sessions: HashMap<AccountId, Arc<Notify>>,
}

/// Cheap to clone; every clone shares the same records.
#[derive(Clone, Default)]
pub struct Runs {
    inner: Arc<Mutex<Inner>>,
}

impl Runs {
    fn lock(&self) -> MutexGuard<'_, Inner> {
        // A panic while holding the lock leaves plain maps behind, still usable.
        self.inner
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }

    /// Records that `account` flies `ship` in `run`.
    pub fn insert(&self, account: AccountId, flying: Flying) {
        self.lock().flying.insert(account, flying);
    }

    /// The run `account` is in, if any.
    #[must_use]
    pub fn get(&self, account: AccountId) -> Option<Flying> {
        self.lock().flying.get(&account).cloned()
    }

    /// Forgets that `account` flies in `run`, if that is still where they are.
    pub fn remove(&self, account: AccountId, run: &str) {
        let mut inner = self.lock();
        if inner
            .flying
            .get(&account)
            .is_some_and(|flying| flying.run == run)
        {
            inner.flying.remove(&account);
        }
    }

    /// Registers a session for `account` and returns what tells it to leave.
    /// An older session for the same account is told to leave now.
    pub fn sign_in(&self, account: AccountId) -> Arc<Notify> {
        let leave = Arc::new(Notify::new());
        if let Some(older) = self.lock().sessions.insert(account, leave.clone()) {
            older.notify_one();
        }
        leave
    }

    /// Forgets a session that has ended, unless a newer one has replaced it.
    pub fn sign_out(&self, account: AccountId, session: &Arc<Notify>) {
        let mut inner = self.lock();
        if inner
            .sessions
            .get(&account)
            .is_some_and(|current| Arc::ptr_eq(current, session))
        {
            inner.sessions.remove(&account);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn signing_in_again_tells_the_older_session_to_leave() {
        let runs = Runs::default();
        let first = runs.sign_in(AccountId(1));
        let second = runs.sign_in(AccountId(1));
        tokio::time::timeout(std::time::Duration::from_secs(1), first.notified())
            .await
            .expect("the older session is told to leave");
        runs.sign_out(AccountId(1), &first);
        assert!(
            runs.lock().sessions.contains_key(&AccountId(1)),
            "the newer stays"
        );
        runs.sign_out(AccountId(1), &second);
        assert!(runs.lock().sessions.is_empty());
    }
}
