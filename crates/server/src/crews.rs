//! Crews (design section 2): two to four pilots who form by invite code. The
//! host chooses the contract and the threat tier and sets the split; each
//! pilot launches from their own home. By the owner's choice the host
//! launches first and the others follow into the same instance within the
//! join window.
//!
//! The registry lives in memory: a crew lasts until its members leave or the
//! server stops. A member whose page reloads stays in the crew.

use std::collections::HashMap;
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::{Duration, Instant};

use frontier_sim::TICKS_PER_SECOND;
use frontier_sim::crew::{MAX_CREW, equal_shares, valid_split};
use frontier_sim::run::JOIN_WINDOW_TICKS;
use frontier_sim::site::ThreatTier;
use tokio::sync::mpsc;

use crate::expedition::{Expedition, PilotEvent};
use crate::store::AccountId;

/// Letters and digits that cannot be misread for one another.
const CODE_ALPHABET: &[u8] = b"ABCDEFGHJKMNPQRSTUVWXYZ23456789";
const CODE_LENGTH: usize = 6;

/// How long after the host launches the others may follow.
#[must_use]
pub fn join_window() -> Duration {
    Duration::from_millis(u64::from(JOIN_WINDOW_TICKS) * 1_000 / u64::from(TICKS_PER_SECOND))
}

/// One pilot in a crew.
#[derive(Clone)]
struct Member {
    account: AccountId,
    callsign: String,
    /// Where to tell their session that the crew changed.
    events: mpsc::UnboundedSender<PilotEvent>,
}

/// The instance the host launched, while the others may still follow.
#[derive(Clone)]
pub struct Launch {
    /// Names the run in the log, the ledger and the checkpoints.
    pub run: String,
    pub expedition: Expedition,
    pub tier: ThreatTier,
    pub opened: Instant,
}

impl Launch {
    /// Seconds left to follow, or `None` once the window has closed.
    #[must_use]
    pub fn seconds_left(&self, now: Instant) -> Option<u32> {
        let left = join_window().checked_sub(now.duration_since(self.opened))?;
        Some(u32::try_from(left.as_secs()).unwrap_or(u32::MAX))
    }
}

struct Crew {
    /// The host first.
    members: Vec<Member>,
    /// Whole percentages, one per member, in the same order.
    split: Vec<u32>,
    /// The contract on offer to the crew: the host's.
    seed: u32,
    launch: Option<Launch>,
}

/// A crew as one member sees it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct View {
    pub code: String,
    /// Callsign, share and whether they host, the host first.
    pub members: Vec<(AccountId, String, u32)>,
    pub you: AccountId,
    pub seed: u32,
    /// The tier the host launched at and the seconds left to follow.
    pub launch: Option<(ThreatTier, u32)>,
}

impl View {
    #[must_use]
    pub fn host(&self) -> Option<AccountId> {
        self.members.first().map(|&(account, ..)| account)
    }

    #[must_use]
    pub fn is_host(&self) -> bool {
        self.host() == Some(self.you)
    }

    /// The share of `account`, if a member.
    #[must_use]
    pub fn share(&self, account: AccountId) -> Option<u32> {
        self.members
            .iter()
            .find(|&&(member, ..)| member == account)
            .map(|&(.., share)| share)
    }

    #[must_use]
    pub fn accounts(&self) -> Vec<AccountId> {
        self.members.iter().map(|&(account, ..)| account).collect()
    }
}

#[derive(Default)]
struct Inner {
    crews: HashMap<String, Crew>,
    member_of: HashMap<AccountId, String>,
}

/// Cheap to clone; every clone shares the same crews.
#[derive(Clone, Default)]
pub struct Crews {
    inner: Arc<Mutex<Inner>>,
}

impl Crews {
    fn lock(&self) -> MutexGuard<'_, Inner> {
        self.inner
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }

    /// Forms a crew hosted by `account`, offering the contract `seed`, and
    /// returns its code. The pilot leaves any crew they were in.
    pub fn create(
        &self,
        account: AccountId,
        callsign: &str,
        events: mpsc::UnboundedSender<PilotEvent>,
        seed: u32,
        code: impl FnMut() -> u8,
    ) -> String {
        self.leave(account);
        let mut inner = self.lock();
        let mut code = code;
        let code = loop {
            let candidate: String = (0..CODE_LENGTH)
                .map(|_| char::from(CODE_ALPHABET[usize::from(code()) % CODE_ALPHABET.len()]))
                .collect();
            if !inner.crews.contains_key(&candidate) {
                break candidate;
            }
        };
        inner.crews.insert(
            code.clone(),
            Crew {
                members: vec![Member {
                    account,
                    callsign: callsign.to_owned(),
                    events,
                }],
                split: equal_shares(1),
                seed,
                launch: None,
            },
        );
        inner.member_of.insert(account, code.clone());
        code
    }

    /// Adds `account` to the crew called `code`. The split goes back to equal
    /// shares, so nobody launches on a split made before they joined.
    ///
    /// # Errors
    /// Refuses an unknown code, a full crew, or one that has launched.
    pub fn join(
        &self,
        code: &str,
        account: AccountId,
        callsign: &str,
        events: mpsc::UnboundedSender<PilotEvent>,
    ) -> Result<(), String> {
        let code = code.trim().to_uppercase();
        let already = self.lock().member_of.get(&account) == Some(&code);
        if already {
            self.reattach(account, events);
            return Ok(());
        }
        {
            let inner = self.lock();
            let crew = inner
                .crews
                .get(&code)
                .ok_or_else(|| format!("No crew has the code {code}."))?;
            if crew.members.len() >= MAX_CREW {
                return Err(format!("That crew is full: {MAX_CREW} pilots."));
            }
            if crew
                .launch
                .as_ref()
                .is_some_and(|launch| launch.seconds_left(Instant::now()).is_some())
            {
                return Err("That crew has just launched; join them after the run.".to_owned());
            }
        }
        self.leave(account);
        let mut inner = self.lock();
        let Some(crew) = inner.crews.get_mut(&code) else {
            return Err(format!("No crew has the code {code}."));
        };
        crew.members.push(Member {
            account,
            callsign: callsign.to_owned(),
            events,
        });
        crew.split = equal_shares(crew.members.len());
        inner.member_of.insert(account, code);
        Ok(())
    }

    /// Takes `account` out of their crew. A leaving host hands the crew to
    /// the next member; the last to leave ends it. Returns the members left,
    /// to tell.
    pub fn leave(&self, account: AccountId) -> Vec<AccountId> {
        let mut inner = self.lock();
        let Some(code) = inner.member_of.remove(&account) else {
            return Vec::new();
        };
        let Some(crew) = inner.crews.get_mut(&code) else {
            return Vec::new();
        };
        crew.members.retain(|member| member.account != account);
        crew.split = equal_shares(crew.members.len());
        let left: Vec<AccountId> = crew.members.iter().map(|m| m.account).collect();
        if left.is_empty() {
            inner.crews.remove(&code);
        }
        left
    }

    /// Points a member's notices at their new session, after a reload.
    pub fn reattach(&self, account: AccountId, events: mpsc::UnboundedSender<PilotEvent>) {
        let mut inner = self.lock();
        let Some(code) = inner.member_of.get(&account).cloned() else {
            return;
        };
        if let Some(member) = inner
            .crews
            .get_mut(&code)
            .and_then(|crew| crew.members.iter_mut().find(|m| m.account == account))
        {
            member.events = events;
        }
    }

    /// Sets the split: the host's call, one whole percentage per member,
    /// summing to 100.
    ///
    /// # Errors
    /// Refuses anyone but the host, and a split that does not add up.
    pub fn set_split(&self, account: AccountId, shares: &[u32]) -> Result<(), String> {
        let mut inner = self.lock();
        let code = inner
            .member_of
            .get(&account)
            .cloned()
            .ok_or("You are not in a crew.")?;
        let crew = inner.crews.get_mut(&code).ok_or("You are not in a crew.")?;
        if crew.members.first().map(|m| m.account) != Some(account) {
            return Err("Only the host sets the split.".to_owned());
        }
        if !valid_split(shares, crew.members.len()) {
            return Err(format!(
                "Give each of the {} pilots a whole percentage, adding up to 100.",
                crew.members.len()
            ));
        }
        crew.split = shares.to_vec();
        Ok(())
    }

    /// The host's contract changed: the crew is offered it.
    pub fn set_seed(&self, account: AccountId, seed: u32) {
        let mut inner = self.lock();
        let Some(code) = inner.member_of.get(&account).cloned() else {
            return;
        };
        if let Some(crew) = inner.crews.get_mut(&code)
            && crew.members.first().map(|m| m.account) == Some(account)
        {
            crew.seed = seed;
            crew.launch = None;
        }
    }

    /// Records the host's launch, which the others may now follow.
    pub fn launched(&self, account: AccountId, launch: Launch) {
        let mut inner = self.lock();
        let Some(code) = inner.member_of.get(&account).cloned() else {
            return;
        };
        if let Some(crew) = inner.crews.get_mut(&code) {
            crew.launch = Some(launch);
        }
    }

    /// The host's launch that `account` may still follow.
    #[must_use]
    pub fn open_launch(&self, account: AccountId) -> Option<Launch> {
        let inner = self.lock();
        let code = inner.member_of.get(&account)?;
        let launch = inner.crews.get(code)?.launch.clone()?;
        launch.seconds_left(Instant::now())?;
        Some(launch)
    }

    /// The crew as `account` sees it, if they are in one.
    #[must_use]
    pub fn view(&self, account: AccountId) -> Option<View> {
        let inner = self.lock();
        let code = inner.member_of.get(&account)?;
        let crew = inner.crews.get(code)?;
        let now = Instant::now();
        Some(View {
            code: code.clone(),
            members: crew
                .members
                .iter()
                .zip(&crew.split)
                .map(|(member, &share)| (member.account, member.callsign.clone(), share))
                .collect(),
            you: account,
            seed: crew.seed,
            launch: crew
                .launch
                .as_ref()
                .and_then(|launch| Some((launch.tier, launch.seconds_left(now)?))),
        })
    }

    /// Tells every member of the crew of `account` (or of the crew they just
    /// left, given its remaining `members`) that it changed.
    pub fn notify(&self, members: &[AccountId]) {
        let inner = self.lock();
        for account in members {
            let member = inner
                .member_of
                .get(account)
                .and_then(|code| inner.crews.get(code))
                .and_then(|crew| crew.members.iter().find(|m| m.account == *account));
            if let Some(member) = member {
                let _ = member.events.send(PilotEvent::CrewChanged);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn events() -> mpsc::UnboundedSender<PilotEvent> {
        mpsc::unbounded_channel().0
    }

    fn counter() -> impl FnMut() -> u8 {
        let mut next = 0_u8;
        move || {
            next = next.wrapping_add(7);
            next
        }
    }

    #[test]
    fn a_crew_forms_by_code_with_equal_shares_and_the_host_first() {
        let crews = Crews::default();
        let code = crews.create(AccountId(1), "Vega", events(), 29, counter());
        assert_eq!(code.len(), 6);
        assert!(code.bytes().all(|b| CODE_ALPHABET.contains(&b)));
        crews
            .join(&code.to_lowercase(), AccountId(2), "Rigel", events())
            .unwrap();
        crews.join(&code, AccountId(3), "Deneb", events()).unwrap();
        let view = crews.view(AccountId(3)).unwrap();
        assert_eq!(
            view.members
                .iter()
                .map(|(_, name, share)| (name.as_str(), *share))
                .collect::<Vec<_>>(),
            [("Vega", 34), ("Rigel", 33), ("Deneb", 33)]
        );
        assert_eq!(view.host(), Some(AccountId(1)));
        assert!(!view.is_host());
        assert_eq!(view.seed, 29, "the host's contract");
    }

    #[test]
    fn refuses_unknown_codes_and_full_crews() {
        let crews = Crews::default();
        assert!(crews.join("NOPE42", AccountId(9), "X", events()).is_err());
        let code = crews.create(AccountId(1), "A", events(), 1, counter());
        for id in 2..=4 {
            crews.join(&code, AccountId(id), "P", events()).unwrap();
        }
        let full = crews.join(&code, AccountId(5), "E", events()).unwrap_err();
        assert_eq!(full, "That crew is full: 4 pilots.");
    }

    #[test]
    fn only_the_host_sets_a_split_that_adds_up() {
        let crews = Crews::default();
        let code = crews.create(AccountId(1), "A", events(), 1, counter());
        crews.join(&code, AccountId(2), "B", events()).unwrap();
        assert!(crews.set_split(AccountId(2), &[50, 50]).is_err());
        assert!(crews.set_split(AccountId(1), &[60, 30]).is_err());
        crews.set_split(AccountId(1), &[60, 40]).unwrap();
        assert_eq!(
            crews.view(AccountId(2)).unwrap().share(AccountId(1)),
            Some(60)
        );
        // A new member resets the split to equal shares.
        crews.join(&code, AccountId(3), "C", events()).unwrap();
        assert_eq!(
            crews.view(AccountId(1)).unwrap().share(AccountId(1)),
            Some(34)
        );
    }

    #[test]
    fn a_leaving_host_hands_over_and_the_last_to_leave_ends_the_crew() {
        let crews = Crews::default();
        let code = crews.create(AccountId(1), "A", events(), 1, counter());
        crews.join(&code, AccountId(2), "B", events()).unwrap();
        assert_eq!(crews.leave(AccountId(1)), [AccountId(2)]);
        assert!(crews.view(AccountId(2)).unwrap().is_host());
        assert!(crews.view(AccountId(1)).is_none());
        crews.leave(AccountId(2));
        assert!(crews.join(&code, AccountId(3), "C", events()).is_err());
    }

    #[test]
    fn joining_another_crew_leaves_the_first() {
        let crews = Crews::default();
        let first = crews.create(AccountId(1), "A", events(), 1, counter());
        let second = crews.create(AccountId(2), "B", events(), 2, counter());
        crews.join(&first, AccountId(3), "C", events()).unwrap();
        crews.join(&second, AccountId(3), "C", events()).unwrap();
        assert_eq!(crews.view(AccountId(1)).unwrap().members.len(), 1);
        assert_eq!(crews.view(AccountId(2)).unwrap().members.len(), 2);
    }
}
