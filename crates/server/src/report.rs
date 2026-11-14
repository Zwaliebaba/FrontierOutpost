//! The Stage 1 gate report (design section 9, plan 7.2): each gate's measures
//! computed from the recorded gate events, set beside the provisional
//! thresholds. Gate 2's trials happen in the readability greybox, which
//! downloads them as JSON; the report reads those files too.
//!
//! A play session is a pilot's sign-ins joined across gaps of less than
//! [`SESSION_GAP_MS`], so a reload does not start a new one. Days are UTC.

use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::fmt::Write as _;

use serde::Deserialize;

use frontier_sim::choice::{self, SeedCheck};
use frontier_sim::outpost::STAGE1;
use frontier_sim::site::SiteTemplate;

use crate::events::{GateEvent, Placed, Recorded};

/// Sign-ins closer together than this are one play session: half an hour.
pub const SESSION_GAP_MS: i64 = 30 * 60 * 1_000;
const DAY_MS: i64 = 24 * 60 * 60 * 1_000;

/// The provisional thresholds of design section 9.
pub mod thresholds {
    /// Gate 1: share of testers who reconfigure in their first three sessions.
    pub const RECONFIGURE_SHARE: f64 = 0.5;
    /// Gate 2: median seconds to select a named hostile.
    pub const SELECT_SECONDS: f64 = 1.0;
    /// Gate 2: share of selections that pick the wrong ship.
    pub const WRONG_TARGET_SHARE: f64 = 0.05;
    /// Gate 2: share of headings judged within 22.5 degrees.
    pub const HEADING_SHARE: f64 = 0.9;
    /// Gate 2: share of nearer targets chosen correctly.
    pub const NEARER_SHARE: f64 = 0.85;
    /// Gate 3: share of defeats followed by a launch in that session or the next.
    pub const RELAUNCH_SHARE: f64 = 0.6;
    /// Gate 4: travel's share of session time.
    pub const TRAVEL_SHARE: f64 = 0.2;
    /// Gate 4: minutes from a loss to flying an equivalent fitting.
    pub const RECOVERY_MINUTES: f64 = 15.0;
    /// Gate 4: seconds from launch to the first decision.
    pub const FIRST_DECISION_SECONDS: f64 = 120.0;
}

/// Whether a measure must be at most, at least or above its threshold.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Target {
    AtMost(f64),
    AtLeast(f64),
    /// Strictly above: "does it happen at all" is above zero.
    Above(f64),
}

/// The playtest's seed pool by run number (playtest script section 3): the
/// seeds gate 1's rules check runs on when `FRONTIER_SEEDS` sets no other.
pub const PLAYTEST_POOL: [u32; 7] = [9, 10, 20, 16, 8, 3, 5];

/// Who the pilots in the saves are (plan M7). The report cannot tell the
/// owner's accounts from anyone else's, so it assumes the owner unless told
/// otherwise: a forgotten flag then errs towards unproven, never towards met.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Players {
    /// Every pilot is the owner, on one account or several.
    Owner,
    /// The pilots are testers other than the owner: a playtest.
    Testers,
}

/// What a measure's figure can show when the owner plays alone (plan M7).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Basis {
    /// Solo play counts in full: the figure comes from the game's structure,
    /// such as time spent travelling.
    Solo,
    /// Solo play, but the player's expertise flatters the figure: on the
    /// owner's play a miss is a failure and a pass is the owner's only.
    Skill,
    /// Computed from the rules, with no one playing.
    Rules,
    /// Needs players other than the owner: on the owner's play it is
    /// unproven, neither met nor failed.
    Others,
}

impl Basis {
    const fn shown(self) -> &'static str {
        match self {
            Self::Solo => "solo",
            Self::Skill => "solo, skill",
            Self::Rules => "rules",
            Self::Others => "other players",
        }
    }
}

/// A measure's or a gate's outcome.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Verdict {
    Met,
    /// Met on the owner's play, where the owner's expertise flatters it.
    MetByOwnerOnly,
    Failed,
    /// Needs players other than the owner.
    Unproven,
}

impl Verdict {
    const fn shown(self) -> &'static str {
        match self {
            Self::Met => "met",
            Self::MetByOwnerOnly => "met by the owner only",
            Self::Failed => "failed",
            Self::Unproven => "unproven",
        }
    }
}

/// One measure of a gate.
#[derive(Debug, Clone, PartialEq)]
pub struct Measure {
    pub name: String,
    /// `None` when the events hold nothing to measure.
    pub value: Option<f64>,
    /// How to show the value: "%" for a share, or a unit.
    pub unit: &'static str,
    pub target: Option<Target>,
    pub basis: Basis,
    /// The pilots (accounts) the value rests on. Gate 2's greybox trials
    /// carry no account, so there it counts files: one per tester and vision
    /// mode in the playtest script.
    pub pilots: usize,
    /// What the value was computed from, such as "12 defeats".
    pub from: String,
}

impl Measure {
    fn new(
        name: &str,
        value: Option<f64>,
        unit: &'static str,
        basis: Basis,
        pilots: usize,
        from: String,
    ) -> Self {
        Self {
            name: name.to_owned(),
            value,
            unit,
            target: None,
            basis,
            pilots,
            from,
        }
    }

    fn target(mut self, target: Target) -> Self {
        self.target = Some(target);
        self
    }

    /// Whether the value meets its threshold, if it has both, regardless of
    /// who played.
    #[must_use]
    pub fn met(&self) -> Option<bool> {
        let value = self.value?;
        Some(match self.target? {
            Target::AtMost(limit) => value <= limit,
            Target::AtLeast(limit) => value >= limit,
            Target::Above(limit) => value > limit,
        })
    }

    /// What the measure shows, given who played. A measure that needs other
    /// players is unproven on the owner's play whatever its value, even with
    /// nothing measured; otherwise a measure without a value or a target
    /// shows nothing.
    #[must_use]
    pub fn verdict(&self, players: Players) -> Option<Verdict> {
        if players == Players::Owner && self.basis == Basis::Others {
            return Some(Verdict::Unproven);
        }
        Some(match (self.met()?, self.basis, players) {
            (false, ..) => Verdict::Failed,
            (true, Basis::Skill, Players::Owner) => Verdict::MetByOwnerOnly,
            (true, ..) => Verdict::Met,
        })
    }

    fn shown(value: f64, unit: &str) -> String {
        if unit == "%" {
            format!("{:.0}%", value * 100.0)
        } else {
            format!("{value:.1} {unit}")
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Gate {
    pub title: &'static str,
    pub measures: Vec<Measure>,
}

impl Gate {
    /// The gate's outcome (plan 7.6): failed if any measure failed; else
    /// unproven if any is; else met, by the owner only if any measure is
    /// only that; `None` when nothing was measured against a threshold.
    #[must_use]
    pub fn verdict(&self, players: Players) -> Option<Verdict> {
        let verdicts: Vec<Verdict> = self
            .measures
            .iter()
            .filter_map(|measure| measure.verdict(players))
            .collect();
        [
            Verdict::Failed,
            Verdict::Unproven,
            Verdict::MetByOwnerOnly,
            Verdict::Met,
        ]
        .into_iter()
        .find(|verdict| verdicts.contains(verdict))
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Report {
    pub players: Players,
    pub pilots: usize,
    pub runs: usize,
    pub sessions: usize,
    pub gates: Vec<Gate>,
    /// Gate 1's rules check, seed by seed (plan 7.4).
    pub rules: Vec<SeedCheck>,
}

/// One greybox trial, as the greybox downloads it.
#[derive(Debug, Clone, Deserialize)]
pub struct Trial {
    pub kind: String,
    pub ms: f64,
    pub outcome: String,
}

#[derive(Debug, Clone, Deserialize)]
struct GreyboxConfig {
    #[serde(default)]
    vision: Option<String>,
}

/// A downloaded greybox result file.
#[derive(Debug, Clone, Deserialize)]
pub struct Greybox {
    config: GreyboxConfig,
    pub records: Vec<Trial>,
}

impl Greybox {
    /// The colour-vision mode the trials ran under.
    #[must_use]
    pub fn vision(&self) -> &str {
        self.config.vision.as_deref().unwrap_or("normal")
    }
}

/// A launched run as the events tell it.
#[derive(Debug, Clone, Default)]
struct Run {
    account: i64,
    launched_at: i64,
    seed: u32,
    layout: Vec<Placed>,
    unlocks: Vec<String>,
    fitted: Vec<String>,
    stake: u32,
    first_pocket: Option<String>,
    first_decision_at: Option<i64>,
    ended: Option<(i64, String, Vec<String>, u32)>,
}

impl Run {
    /// What the pilot chose on the seed: the rig flown and the first pocket.
    fn choice(&self) -> (Option<String>, Option<String>) {
        let rig = self.fitted.iter().find(|id| id.ends_with("_rig")).cloned();
        (rig, self.first_pocket.clone())
    }
}

#[derive(Debug, Clone, Copy)]
struct Session {
    start: i64,
    end: i64,
}

fn median(values: &mut [f64]) -> Option<f64> {
    if values.is_empty() {
        return None;
    }
    values.sort_by(f64::total_cmp);
    let mid = values.len() / 2;
    Some(if values.len().is_multiple_of(2) {
        f64::midpoint(values[mid - 1], values[mid])
    } else {
        values[mid]
    })
}

fn share(part: usize, whole: usize) -> Option<f64> {
    // Counts of a few thousand at most, so `as` loses nothing.
    (whole > 0).then(|| part as f64 / whole as f64)
}

/// Each account's play sessions: sign-ins joined across short gaps.
fn sessions(events: &[Recorded]) -> HashMap<i64, Vec<Session>> {
    let mut spans: HashMap<i64, Vec<Session>> = HashMap::new();
    for recorded in events {
        if let GateEvent::SessionEnded { millis } = recorded.event {
            spans.entry(recorded.account).or_default().push(Session {
                start: recorded.at - i64::try_from(millis).unwrap_or(i64::MAX),
                end: recorded.at,
            });
        }
    }
    for list in spans.values_mut() {
        list.sort_by_key(|session| session.start);
        let mut merged: Vec<Session> = Vec::new();
        for session in list.drain(..) {
            match merged.last_mut() {
                Some(last) if session.start - last.end < SESSION_GAP_MS => {
                    last.end = last.end.max(session.end);
                }
                _ => merged.push(session),
            }
        }
        *list = merged;
    }
    spans
}

/// The index of the session holding `at`, or the last one starting before it.
fn session_of(list: &[Session], at: i64) -> Option<usize> {
    list.iter().rposition(|session| session.start <= at)
}

fn runs(events: &[Recorded]) -> Vec<Run> {
    let mut by_key: BTreeMap<(i64, i64, String), Run> = BTreeMap::new();
    let mut key_of: HashMap<(i64, String), (i64, i64, String)> = HashMap::new();
    for recorded in events {
        let Some(run_id) = &recorded.run else {
            continue;
        };
        let id = (recorded.account, run_id.clone());
        match &recorded.event {
            GateEvent::Launched {
                seed,
                layout,
                unlocks,
                fitted,
                stake,
                ..
            } => {
                let key = (recorded.at, recorded.account, run_id.clone());
                key_of.insert(id, key.clone());
                by_key.insert(
                    key,
                    Run {
                        account: recorded.account,
                        launched_at: recorded.at,
                        seed: *seed,
                        layout: layout.clone(),
                        unlocks: unlocks.clone(),
                        fitted: fitted.clone(),
                        stake: *stake,
                        ..Run::default()
                    },
                );
            }
            event => {
                let Some(run) = key_of.get(&id).and_then(|key| by_key.get_mut(key)) else {
                    continue;
                };
                if event.is_decision() && run.first_decision_at.is_none() {
                    run.first_decision_at = Some(recorded.at);
                }
                match event {
                    GateEvent::PocketEntered { pocket } if run.first_pocket.is_none() => {
                        run.first_pocket = Some(pocket.clone());
                    }
                    GateEvent::RunEnded {
                        ending,
                        lost,
                        approach_seconds,
                        return_seconds,
                        ..
                    } => {
                        run.ended = Some((
                            recorded.at,
                            ending.clone(),
                            lost.clone(),
                            approach_seconds + return_seconds,
                        ));
                    }
                    _ => {}
                }
            }
        }
    }
    by_key.into_values().collect()
}

/// Runs gate 1's rules check on each of `seeds` of `template` under the
/// Stage 1 outpost.
#[must_use]
pub fn rules_check(template: SiteTemplate, seeds: &[u32]) -> Vec<SeedCheck> {
    seeds
        .iter()
        .map(|&seed| choice::check(&STAGE1, template, seed))
        .collect()
}

/// Computes the gates from `events`, greybox `trials` and gate 1's `rules`
/// check, played by `players`.
#[must_use]
pub fn compute(
    events: &[Recorded],
    greybox: &[Greybox],
    players: Players,
    rules: Vec<SeedCheck>,
) -> Report {
    let sessions = sessions(events);
    let runs = runs(events);
    let pilots: BTreeSet<i64> = runs.iter().map(|run| run.account).collect();
    Report {
        players,
        pilots: pilots.len(),
        runs: runs.len(),
        sessions: sessions.values().map(Vec::len).sum(),
        gates: vec![
            gate_1(events, &runs, &sessions, &rules),
            gate_2(events, greybox),
            gate_3(&runs, &sessions),
            gate_4(&runs, &sessions),
            crew_evidence(events),
        ],
        rules,
    }
}

/// Gate 1's rules measures: how often a layout change alters the best choice
/// open under both, and how firmly the firmest such change steers a pilot.
fn rules_measures(rules: &[SeedCheck]) -> Vec<Measure> {
    let pairs: usize = rules.iter().map(|check| check.pairs).sum();
    let changed: Vec<&choice::Changed> = rules.iter().flat_map(|check| &check.changed).collect();
    let firmest = changed
        .iter()
        .max_by(|a, b| a.gain_share().total_cmp(&b.gain_share()));
    vec![
        Measure::new(
            "Rules: layout pairs where a facility changes the best choice open under both",
            share(changed.len(), pairs),
            "%",
            Basis::Rules,
            0,
            format!(
                "{} of {pairs} pairs on {} seeds; per-run take, time and risk left out",
                changed.len(),
                rules.len()
            ),
        )
        .target(Target::Above(0.0)),
        Measure::new(
            "Rules: how firmly the firmest such change steers a pilot",
            firmest.map(|changed| changed.gain_share()),
            "%",
            Basis::Rules,
            0,
            firmest.map_or_else(
                || "no change".to_owned(),
                |changed| {
                    format!(
                        "{} cr of {} cr: the smaller of the two layouts' preference",
                        changed.gain, changed.of
                    )
                },
            ),
        ),
    ]
}

fn gate_1(
    events: &[Recorded],
    runs: &[Run],
    sessions: &HashMap<i64, Vec<Session>>,
    rules: &[SeedCheck],
) -> Gate {
    // Among pilots with the same unlocks, do active layouts differ?
    let mut by_unlocks: BTreeMap<&[String], BTreeMap<i64, &[Placed]>> = BTreeMap::new();
    for run in runs.iter().filter(|run| !run.unlocks.is_empty()) {
        by_unlocks
            .entry(&run.unlocks)
            .or_default()
            .insert(run.account, &run.layout);
    }
    let groups: Vec<_> = by_unlocks.values().filter(|g| g.len() >= 2).collect();
    let differing = groups
        .iter()
        .filter(|group| group.values().collect::<BTreeSet<_>>().len() > 1)
        .count();
    let grouped: BTreeSet<i64> = groups.iter().flat_map(|g| g.keys().copied()).collect();

    // On the same seed, does the layout predict the rig and the first pocket?
    let (mut same, mut same_agree, mut other, mut other_agree) = (0, 0, 0, 0);
    let (mut same_pilots, mut other_pilots) = (BTreeSet::new(), BTreeSet::new());
    let mut by_seed: BTreeMap<u32, Vec<&Run>> = BTreeMap::new();
    for run in runs.iter().filter(|run| run.first_pocket.is_some()) {
        by_seed.entry(run.seed).or_default().push(run);
    }
    for list in by_seed.values() {
        for (i, a) in list.iter().enumerate() {
            for b in &list[i + 1..] {
                if a.account == b.account {
                    continue;
                }
                let agree = usize::from(a.choice() == b.choice());
                if a.layout == b.layout {
                    same += 1;
                    same_agree += agree;
                    same_pilots.extend([a.account, b.account]);
                } else {
                    other += 1;
                    other_agree += agree;
                    other_pilots.extend([a.account, b.account]);
                }
            }
        }
    }

    // Reconfigurations: installing again, removing, or changing power.
    let reconfigurations: Vec<&Recorded> = events
        .iter()
        .filter(|r| {
            matches!(&r.event, GateEvent::OutpostChanged { action, .. }
                if matches!(action.as_str(), "reinstalled" | "removed" | "powered"))
        })
        .collect();
    let mut per_ten: Vec<f64> = Vec::new();
    let mut runs_by: BTreeMap<i64, usize> = BTreeMap::new();
    for run in runs {
        *runs_by.entry(run.account).or_default() += 1;
    }
    for (&account, &count) in &runs_by {
        let changes = reconfigurations
            .iter()
            .filter(|r| r.account == account)
            .count();
        per_ten.push(changes as f64 * 10.0 / count as f64);
    }
    let pilots = runs_by.len();
    let early = runs_by
        .keys()
        .filter(|&&account| {
            let Some(list) = sessions.get(&account) else {
                return false;
            };
            reconfigurations.iter().any(|r| {
                r.account == account && session_of(list, r.at).is_some_and(|index| index < 3)
            })
        })
        .count();
    let per_ten_median = median(&mut per_ten);
    // Every measure but the reconfiguration rate compares pilots, or is a
    // share of testers; one pilot's own rate says how the owner plays.
    Gate {
        title: "Gate 1 · Outpost enhancements change expedition decisions",
        measures: vec![
            Measure::new(
                "Pilots with the same unlocks whose layouts differ",
                share(differing, groups.len()),
                "%",
                Basis::Others,
                grouped.len(),
                format!("{} groups of two or more pilots", groups.len()),
            ),
            Measure::new(
                "Same seed, same layout: same rig and first pocket",
                share(same_agree, same),
                "%",
                Basis::Others,
                same_pilots.len(),
                format!("{same} pairs of runs"),
            ),
            Measure::new(
                "Same seed, different layout: same rig and first pocket",
                share(other_agree, other),
                "%",
                Basis::Others,
                other_pilots.len(),
                format!(
                    "{other} pairs of runs; lower than the line above means layout predicts choice"
                ),
            ),
            Measure::new(
                "Reconfigurations per ten runs (median pilot)",
                per_ten_median,
                "per 10 runs",
                Basis::Skill,
                pilots,
                format!(
                    "{} reconfigurations; should be neither 0 nor 10",
                    reconfigurations.len()
                ),
            ),
            Measure::new(
                "Pilots who reconfigure in their first three sessions",
                share(early, pilots),
                "%",
                Basis::Others,
                pilots,
                format!("{pilots} pilots"),
            )
            .target(Target::AtLeast(thresholds::RECONFIGURE_SHARE)),
        ]
        .into_iter()
        .chain(rules_measures(rules))
        .collect(),
    }
}

fn gate_2(events: &[Recorded], greybox: &[Greybox]) -> Gate {
    let mut measures = Vec::new();
    let mut by_vision: BTreeMap<&str, (usize, Vec<&Trial>)> = BTreeMap::new();
    for file in greybox {
        let (files, trials) = by_vision.entry(file.vision()).or_default();
        *files += 1;
        trials.extend(file.records.iter());
    }
    // Readability is a skill the owner has practised more than anyone.
    for (vision, (files, trials)) in by_vision {
        let measure = |name: &str, value, unit, from| {
            Measure::new(
                &format!("{name} ({vision} vision)"),
                value,
                unit,
                Basis::Skill,
                files,
                from,
            )
        };
        let of =
            |kind: &str| -> Vec<&&Trial> { trials.iter().filter(|t| t.kind == kind).collect() };
        let (acquire, heading, range) = (of("acquire"), of("heading"), of("range"));
        let mut times: Vec<f64> = acquire
            .iter()
            .filter(|t| t.outcome == "correct")
            .map(|t| t.ms / 1_000.0)
            .collect();
        let answered = acquire
            .iter()
            .filter(|t| t.outcome == "correct" || t.outcome == "wrong")
            .count();
        let wrong = acquire.iter().filter(|t| t.outcome == "wrong").count();
        let correct = |list: &[&&Trial]| list.iter().filter(|t| t.outcome == "correct").count();
        let selections = times.len();
        measures.push(
            measure(
                "Median time to select a named hostile",
                median(&mut times),
                "s",
                format!("{selections} selections"),
            )
            .target(Target::AtMost(thresholds::SELECT_SECONDS)),
        );
        measures.push(
            measure(
                "Wrong-target selections",
                share(wrong, answered),
                "%",
                format!("{answered} answered"),
            )
            .target(Target::AtMost(thresholds::WRONG_TARGET_SHARE)),
        );
        measures.push(
            measure(
                "Headings judged within 22.5°",
                share(correct(&heading), heading.len()),
                "%",
                format!("{} trials", heading.len()),
            )
            .target(Target::AtLeast(thresholds::HEADING_SHARE)),
        );
        measures.push(
            measure(
                "Nearer target chosen",
                share(correct(&range), range.len()),
                "%",
                format!("{} trials", range.len()),
            )
            .target(Target::AtLeast(thresholds::NEARER_SHARE)),
        );
    }
    if measures.is_empty() {
        measures.push(Measure::new(
            "Greybox trials",
            None,
            "",
            Basis::Skill,
            0,
            "none given: pass the greybox's downloaded JSON files to the report".to_owned(),
        ));
    }
    // In the game itself: how often a lock attempt is beyond lock range.
    let locks: Vec<(i64, bool)> = events
        .iter()
        .filter_map(|r| match r.event {
            GateEvent::Locked { taken, .. } => Some((r.account, taken)),
            _ => None,
        })
        .collect();
    let refused = locks.iter().filter(|(_, taken)| !*taken).count();
    let locking: BTreeSet<i64> = locks.iter().map(|(account, _)| *account).collect();
    measures.push(Measure::new(
        "In play: lock attempts beyond lock range",
        share(refused, locks.len()),
        "%",
        Basis::Skill,
        locking.len(),
        format!("{} lock attempts", locks.len()),
    ));
    Gate {
        title: "Gate 2 · Angled-view targeting stays readable",
        measures,
    }
}

fn gate_3(runs: &[Run], sessions: &HashMap<i64, Vec<Session>>) -> Gate {
    let mut defeats = 0;
    let mut relaunched = 0;
    let mut stake_ratios: Vec<f64> = Vec::new();
    let (mut defeated, mut staking) = (BTreeSet::new(), BTreeSet::new());
    for (index, run) in runs.iter().enumerate() {
        let Some((ended_at, ending, ..)) = &run.ended else {
            continue;
        };
        if ending != "destroyed" {
            continue;
        }
        defeats += 1;
        defeated.insert(run.account);
        let next = runs[index + 1..]
            .iter()
            .find(|later| later.account == run.account && later.launched_at > *ended_at);
        let list = sessions.get(&run.account).map_or(&[][..], Vec::as_slice);
        if let Some(next) = next {
            let (lost_in, next_in) = (
                session_of(list, *ended_at),
                session_of(list, next.launched_at),
            );
            if let (Some(a), Some(b)) = (lost_in, next_in)
                && b <= a + 1
            {
                relaunched += 1;
            }
            if run.stake > 0 {
                stake_ratios.push(f64::from(next.stake) / f64::from(run.stake));
                staking.insert(run.account);
            }
        }
    }

    // Next-day return after a session that ended in defeat, and in success.
    let (mut after_defeat, mut back_after_defeat, mut after_success, mut back_after_success) =
        (0, 0, 0, 0);
    let (mut defeat_pilots, mut success_pilots) = (BTreeSet::new(), BTreeSet::new());
    for (account, list) in sessions {
        for (index, session) in list.iter().enumerate() {
            let last = runs
                .iter()
                .filter(|run| run.account == *account)
                .filter_map(|run| run.ended.as_ref())
                .filter(|(at, ..)| *at >= session.start && *at <= session.end)
                .max_by_key(|(at, ..)| *at);
            let Some((_, ending, ..)) = last else {
                continue;
            };
            let day = session.end.div_euclid(DAY_MS);
            let back = list[index + 1..]
                .iter()
                .any(|later| later.start.div_euclid(DAY_MS) == day + 1);
            match ending.as_str() {
                "destroyed" => {
                    after_defeat += 1;
                    back_after_defeat += usize::from(back);
                    defeat_pilots.insert(*account);
                }
                "docked" => {
                    after_success += 1;
                    back_after_success += usize::from(back);
                    success_pilots.insert(*account);
                }
                _ => {}
            }
        }
    }

    // The spread of choices across pilots on one seed at their third and
    // tenth run.
    let spread = |nth: usize| -> (Option<f64>, usize) {
        let mut by_account: BTreeMap<i64, Vec<&Run>> = BTreeMap::new();
        for run in runs {
            by_account.entry(run.account).or_default().push(run);
        }
        let picked: Vec<&Run> = by_account
            .values()
            .filter_map(|list| list.get(nth - 1).copied())
            .collect();
        let mut by_seed: BTreeMap<u32, Vec<&Run>> = BTreeMap::new();
        for run in picked {
            by_seed.entry(run.seed).or_default().push(run);
        }
        let Some(list) = by_seed.into_values().max_by_key(Vec::len) else {
            return (None, 0);
        };
        let distinct: BTreeSet<_> = list
            .iter()
            .map(|run| (run.layout.clone(), run.choice()))
            .collect();
        (share(distinct.len(), list.len()), list.len())
    };
    let (third, third_n) = spread(3);
    let (tenth, tenth_n) = spread(10);
    let mut stake_ratio = stake_ratios.clone();
    // No measure of one's own return means anything (plan M7): the owner
    // comes back to their game whatever happens.
    Gate {
        title: "Gate 3 · Players voluntarily repeat after loss",
        measures: vec![
            Measure::new(
                "Defeats followed by a launch in that session or the next",
                share(relaunched, defeats),
                "%",
                Basis::Others,
                defeated.len(),
                format!("{defeats} defeats"),
            )
            .target(Target::AtLeast(thresholds::RELAUNCH_SHARE)),
            Measure::new(
                "Next-day return after a session ending in defeat",
                share(back_after_defeat, after_defeat),
                "%",
                Basis::Others,
                defeat_pilots.len(),
                format!("{after_defeat} sessions"),
            ),
            Measure::new(
                "Next-day return after a session ending docked",
                share(back_after_success, after_success),
                "%",
                Basis::Others,
                success_pilots.len(),
                format!("{after_success} sessions"),
            ),
            Measure::new(
                "Stake on the launch after a defeat, against the one lost (median)",
                median(&mut stake_ratio),
                "×",
                Basis::Others,
                staking.len(),
                format!(
                    "{} relaunches after a defeat with a stake",
                    stake_ratios.len()
                ),
            ),
            Measure::new(
                "Distinct choices per pilot on one seed at run 3",
                third,
                "%",
                Basis::Others,
                third_n,
                format!("{third_n} pilots on the most played seed"),
            ),
            Measure::new(
                "Distinct choices per pilot on one seed at run 10",
                tenth,
                "%",
                Basis::Others,
                tenth_n,
                format!("{tenth_n} pilots on the most played seed"),
            ),
        ],
    }
}

fn gate_4(runs: &[Run], sessions: &HashMap<i64, Vec<Session>>) -> Gate {
    let travel: u64 = runs
        .iter()
        .filter_map(|run| run.ended.as_ref())
        .map(|(.., travel)| u64::from(*travel))
        .sum();
    let played: i64 = sessions
        .values()
        .flatten()
        .map(|session| session.end - session.start)
        .sum();
    let travel_share = (played > 0).then(|| travel as f64 * 1_000.0 / played as f64);

    let travelling: BTreeSet<i64> = runs
        .iter()
        .filter(|run| run.ended.is_some())
        .map(|run| run.account)
        .collect();

    let mut recovery: Vec<f64> = Vec::new();
    let mut losses = 0;
    let mut losing = BTreeSet::new();
    for (index, run) in runs.iter().enumerate() {
        let Some((ended_at, _, lost, _)) = &run.ended else {
            continue;
        };
        if lost.is_empty() {
            continue;
        }
        losses += 1;
        losing.insert(run.account);
        let back = runs[index + 1..].iter().find(|later| {
            later.account == run.account
                && later.launched_at > *ended_at
                && lost.iter().all(|piece| later.fitted.contains(piece))
        });
        if let Some(back) = back {
            recovery.push((back.launched_at - ended_at) as f64 / 60_000.0);
        }
    }

    let mut decisions: Vec<f64> = Vec::new();
    let mut deciding = BTreeSet::new();
    for run in runs {
        if let Some(at) = run.first_decision_at {
            decisions.push((at - run.launched_at) as f64 / 1_000.0);
            deciding.insert(run.account);
        }
    }
    let recovered = recovery.len();
    // Travel and recovery come from the site's distances and the economy's
    // prices, so solo play counts in full; how soon a pilot decides is skill.
    Gate {
        title: "Gate 4 · Travel and recovery meet targets",
        measures: vec![
            Measure::new(
                "Travel (undock to first pocket, last pocket to dock) as a share of session time",
                travel_share,
                "%",
                Basis::Solo,
                travelling.len(),
                format!(
                    "{} runs ended",
                    runs.iter().filter(|r| r.ended.is_some()).count()
                ),
            )
            .target(Target::AtMost(thresholds::TRAVEL_SHARE)),
            Measure::new(
                "Minutes from losing a fitting to flying it again (median)",
                median(&mut recovery),
                "min",
                Basis::Solo,
                losing.len(),
                format!("{losses} losses, {recovered} recovered"),
            )
            .target(Target::AtMost(thresholds::RECOVERY_MINUTES)),
            Measure::new(
                "Seconds from launch to the first decision (median)",
                median(&mut decisions),
                "s",
                Basis::Skill,
                deciding.len(),
                format!("{} runs with a decision", decisions.len()),
            )
            .target(Target::AtMost(thresholds::FIRST_DECISION_SECONDS)),
        ],
    }
}

/// Early evidence for the Stage 2 crew gate, not itself a Stage 1 gate.
fn crew_evidence(events: &[Recorded]) -> Gate {
    let mut by_crew: BTreeMap<u32, (Vec<f64>, BTreeSet<i64>)> = BTreeMap::new();
    for recorded in events {
        if let GateEvent::PotShare { share, crew } = recorded.event {
            let (shares, pilots) = by_crew.entry(crew).or_default();
            shares.push(f64::from(share));
            pilots.insert(recorded.account);
        }
    }
    // A pot's split follows the crew rules, so the owner on two clients
    // shows it in full; how a newcomer helps a veteran stays with others.
    let measures = if by_crew.is_empty() {
        vec![Measure::new(
            "Pot share per pilot",
            None,
            "cr",
            Basis::Solo,
            0,
            "no pots paid".to_owned(),
        )]
    } else {
        by_crew
            .into_iter()
            .map(|(crew, (mut shares, pilots))| {
                let count = shares.len();
                Measure::new(
                    &format!("Pot share per pilot, crew of {crew} (median)"),
                    median(&mut shares),
                    "cr",
                    Basis::Solo,
                    pilots.len(),
                    format!("{count} shares"),
                )
            })
            .collect()
    };
    Gate {
        title: "Evidence for the Stage 2 crew gate · per-head reward by crew size",
        measures,
    }
}

impl Report {
    /// The report as Markdown.
    #[must_use]
    pub fn markdown(&self) -> String {
        let mut out = String::new();
        let _ = writeln!(out, "# Stage 1 gate report\n");
        let _ = writeln!(
            out,
            "{} pilots · {} runs · {} play sessions. Thresholds are design section 9's provisional figures.\n",
            self.pilots, self.runs, self.sessions
        );
        let _ = writeln!(
            out,
            "{}\n",
            match self.players {
                Players::Owner => {
                    "Every pilot is taken to be the owner, so a measure that needs other players is \
                     unproven, and a pass the owner's expertise flatters is the owner's only. Run \
                     the report with `--testers` for a playtest with other players."
                }
                Players::Testers => "The pilots are testers other than the owner.",
            }
        );
        let _ = writeln!(
            out,
            "Basis: *solo* counts the owner's play in full; *solo, skill* is flattered by \
             expertise; *rules* is computed from the simulation; *other players* needs players \
             other than the owner. Pilots counts the accounts a value rests on (greybox files \
             for gate 2's trials).\n"
        );
        for gate in &self.gates {
            let _ = writeln!(out, "## {}\n", gate.title);
            let verdict = gate
                .verdict(self.players)
                .map_or("not measured", Verdict::shown);
            let _ = writeln!(out, "Gate: **{verdict}**\n");
            let _ = writeln!(
                out,
                "| Measure | Value | Provisional target | Verdict | Basis | Pilots | From |"
            );
            let _ = writeln!(out, "| --- | --- | --- | --- | --- | --- | --- |");
            for measure in &gate.measures {
                let value = measure
                    .value
                    .map_or_else(|| "–".to_owned(), |v| Measure::shown(v, measure.unit));
                let target = match measure.target {
                    Some(Target::AtMost(limit)) => {
                        format!("≤ {}", Measure::shown(limit, measure.unit))
                    }
                    Some(Target::AtLeast(limit)) => {
                        format!("≥ {}", Measure::shown(limit, measure.unit))
                    }
                    Some(Target::Above(limit)) => {
                        format!("> {}", Measure::shown(limit, measure.unit))
                    }
                    None => String::new(),
                };
                let verdict = measure.verdict(self.players).map_or("", Verdict::shown);
                // No one plays a rules measure.
                let pilots = if measure.basis == Basis::Rules {
                    "–".to_owned()
                } else {
                    measure.pilots.to_string()
                };
                let _ = writeln!(
                    out,
                    "| {} | {value} | {target} | {verdict} | {} | {pilots} | {} |",
                    measure.name,
                    measure.basis.shown(),
                    measure.from
                );
            }
            out.push('\n');
        }
        self.rules_table(&mut out);
        out
    }

    /// Gate 1's rules check, seed by seed: the check's table for the pool
    /// (plan 7.4).
    fn rules_table(&self, out: &mut String) {
        let Some(first) = self.rules.first() else {
            return;
        };
        let _ = writeln!(out, "## Gate 1 rules check\n");
        let _ = writeln!(
            out,
            "On each seed, every layout of the facilities ({} layouts) is compared with every \
             other ({} pairs). Under each layout of a pair, the best choice is the pocket and rig \
             with the best per-run take among the options open under both, so an unlock alone \
             never counts. The take: a solo pilot at threat tier I fills the hold from one \
             pocket and sells the haul raw or processed, the processor working for one \
             {}-minute run. Time and risk are left out. A change's firmness is the smaller of \
             the two layouts' preference for its own choice.\n",
            first.layouts,
            first.pairs,
            choice::RUN_SECONDS / 60
        );
        let _ = writeln!(
            out,
            "| Seed | Hazard | Rich pocket | Best choice, any layout | Pairs where the best choice changes | Firmest change | Example |"
        );
        let _ = writeln!(out, "| --- | --- | --- | --- | --- | --- | --- |");
        for check in &self.rules {
            let site = &check.site;
            let rich = site
                .pockets
                .get(site.values.rich_pocket)
                .map_or("–", |pocket| pocket.template.kind.name());
            let firmest = check
                .changed
                .iter()
                .max_by(|a, b| a.gain_share().total_cmp(&b.gain_share()));
            let (firmness, example) = firmest.map_or_else(
                || ("–".to_owned(), "–".to_owned()),
                |changed| {
                    (
                        format!("{} cr of {} cr", changed.gain, changed.of),
                        format!(
                            "{}: {} ({} cr); {}: {} ({} cr)",
                            choice::describe(&STAGE1, &changed.first),
                            changed.under_first.choice.name(),
                            changed.under_first.take,
                            choice::describe(&STAGE1, &changed.second),
                            changed.under_second.choice.name(),
                            changed.under_second.take
                        ),
                    )
                },
            );
            let _ = writeln!(
                out,
                "| {} | {} | {rich} | {} ({} cr) | {} of {} | {firmness} | {example} |",
                site.seed,
                site.values.hazard.name(),
                check.best_anywhere.choice.name(),
                check.best_anywhere.take,
                check.changed.len(),
                check.pairs
            );
        }
        out.push('\n');
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const M: i64 = 60_000;
    const DAY: i64 = DAY_MS;

    fn at(at: i64, account: i64, run: Option<&str>, event: GateEvent) -> Recorded {
        Recorded {
            at,
            account,
            run: run.map(str::to_owned),
            event,
        }
    }

    fn launched(
        layout: &[(&str, &str)],
        unlocks: &[&str],
        fitted: &[&str],
        stake: u32,
    ) -> GateEvent {
        GateEvent::Launched {
            seed: 29,
            tier: 1,
            crew: 1,
            role: "solo".into(),
            layout: layout
                .iter()
                .map(|(socket, facility)| Placed {
                    socket: (*socket).into(),
                    facility: (*facility).into(),
                    run: 1,
                })
                .collect(),
            unlocks: unlocks.iter().map(|u| (*u).to_owned()).collect(),
            fitted: fitted.iter().map(|f| (*f).to_owned()).collect(),
            stake,
            supplies: 40,
        }
    }

    fn ended(ending: &str, travel: u32, lost: &[&str]) -> GateEvent {
        GateEvent::RunEnded {
            ending: ending.into(),
            banked: 0,
            cargo_lost: 0,
            lost: lost.iter().map(|l| (*l).to_owned()).collect(),
            approach_seconds: travel / 2,
            objective_seconds: 600,
            return_seconds: travel - travel / 2,
        }
    }

    fn pocket(name: &str) -> GateEvent {
        GateEvent::PocketEntered {
            pocket: name.into(),
        }
    }

    fn outpost(action: &str) -> GateEvent {
        GateEvent::OutpostChanged {
            action: action.into(),
            socket: "a".into(),
            facility: "refinery".into(),
            run: 1,
        }
    }

    /// Two pilots' play, scripted so every figure can be worked out by hand.
    ///
    /// Pilot 1, day one, one session of an hour: a docked run in the basic
    /// fitting; buys a refinery; a run with the extraction rig, destroyed,
    /// losing the rig; removes the refinery (a reconfiguration); a docked run.
    /// Day two, half an hour: the refinery back on, the rig flown, docked.
    /// Pilot 2, day one, half an hour: one run, destroyed, and no other.
    fn history() -> Vec<Recorded> {
        let refinery_a = [("a", "refinery")];
        let refinery_b = [("b", "refinery")];
        let unlocked = ["refinery:1"];
        let rig = ["extraction_rig"];
        let mut events = vec![
            at(
                60 * M,
                1,
                None,
                GateEvent::SessionEnded { millis: 3_600_000 },
            ),
            at(M, 1, Some("r1"), launched(&[], &[], &[], 0)),
            at(
                M + 90_000,
                1,
                Some("r1"),
                GateEvent::Locked {
                    target: "drone".into(),
                    distance: 40.0,
                    taken: true,
                },
            ),
            at(M + 150_000, 1, Some("r1"), pocket("OreField")),
            at(20 * M, 1, Some("r1"), ended("docked", 300, &[])),
            at(21 * M, 1, None, outpost("bought")),
            at(
                22 * M,
                1,
                Some("r2"),
                launched(&refinery_a, &unlocked, &rig, 60),
            ),
            at(22 * M + 30_000, 1, Some("r2"), GateEvent::ScanStarted),
            at(22 * M + 100_000, 1, Some("r2"), pocket("VolatileVeins")),
            at(40 * M, 1, Some("r2"), ended("destroyed", 200, &rig)),
            at(41 * M, 1, None, outpost("removed")),
            at(45 * M, 1, Some("r3"), launched(&[], &unlocked, &[], 0)),
            at(55 * M, 1, Some("r3"), ended("docked", 100, &[])),
            at(
                DAY + 30 * M,
                1,
                None,
                GateEvent::SessionEnded { millis: 1_800_000 },
            ),
            at(
                DAY + 5 * M,
                1,
                Some("r4"),
                launched(&refinery_a, &unlocked, &rig, 60),
            ),
            at(DAY + 5 * M + 40_000, 1, Some("r4"), pocket("OreField")),
            at(DAY + 25 * M, 1, Some("r4"), ended("docked", 400, &[])),
            at(
                DAY + 25 * M,
                1,
                Some("r4"),
                GateEvent::PotShare {
                    share: 100,
                    crew: 1,
                },
            ),
            at(
                30 * M,
                2,
                None,
                GateEvent::SessionEnded { millis: 1_800_000 },
            ),
            at(
                2 * M,
                2,
                Some("r5"),
                launched(&refinery_b, &unlocked, &rig, 60),
            ),
            at(2 * M + 60_000, 2, Some("r5"), pocket("OreField")),
            at(
                2 * M + 70_000,
                2,
                Some("r5"),
                GateEvent::Locked {
                    target: "raider".into(),
                    distance: 75.0,
                    taken: false,
                },
            ),
            at(10 * M, 2, Some("r5"), ended("destroyed", 0, &[])),
            at(
                10 * M,
                2,
                Some("r5"),
                GateEvent::PotShare { share: 80, crew: 2 },
            ),
        ];
        events.sort_by_key(|recorded| recorded.at);
        events
    }

    fn greybox() -> Greybox {
        let trial = |kind: &str, ms: f64, outcome: &str| {
            format!(r#"{{"kind":"{kind}","ms":{ms},"outcome":"{outcome}"}}"#)
        };
        let mut records = vec![
            trial("acquire", 800.0, "correct"),
            trial("acquire", 1_200.0, "correct"),
            trial("acquire", 900.0, "correct"),
            trial("acquire", 500.0, "wrong"),
            trial("acquire", 4_000.0, "timeout"),
            trial("heading", 2_000.0, "wrong"),
            trial("range", 1_000.0, "wrong"),
        ];
        records.extend((0..9).map(|_| trial("heading", 2_000.0, "correct")));
        records.extend((0..4).map(|_| trial("range", 1_000.0, "correct")));
        let json = format!(
            r#"{{"config":{{"vision":"normal","scene":"station"}},"summary":{{}},"records":[{}]}}"#,
            records.join(",")
        );
        serde_json::from_str(&json).unwrap()
    }

    fn value(report: &Report, gate: usize, name: &str) -> Option<f64> {
        report.gates[gate]
            .measures
            .iter()
            .find(|m| m.name.starts_with(name))
            .unwrap_or_else(|| panic!("no measure {name}"))
            .value
    }

    fn close(actual: Option<f64>, expected: f64) -> bool {
        actual.is_some_and(|actual| (actual - expected).abs() < 1e-9)
    }

    #[test]
    fn reproduces_the_hand_checked_figures() {
        let report = compute(&history(), &[greybox()], Players::Testers, Vec::new());
        assert_eq!((report.pilots, report.runs, report.sessions), (2, 5, 3));

        // Gate 1. Both pilots own a refinery at tier 1 and install it in
        // different sockets: their layouts differ.
        assert!(close(
            value(&report, 0, "Pilots with the same unlocks"),
            1.0
        ));
        // Seed 29, pairs across pilots: r1-r5, r2-r5, r4-r5, all with
        // different layouts; only r4 and r5 agree (extraction rig, ore field).
        assert_eq!(value(&report, 0, "Same seed, same layout"), None);
        assert!(close(
            value(&report, 0, "Same seed, different layout"),
            1.0 / 3.0
        ));
        // Pilot 1: one reconfiguration in four runs, 2.5 per ten; pilot 2:
        // none in one run. The median of 2.5 and 0.
        assert!(close(
            value(&report, 0, "Reconfigurations per ten runs"),
            1.25
        ));
        // Pilot 1 reconfigures in their first session; pilot 2 never: 1 of 2.
        let early = &report.gates[0].measures[4];
        assert!(close(early.value, 0.5));
        assert_eq!(early.met(), Some(true), "at least half");

        // Gate 2. Acquire: correct in 0.8, 1.2 and 0.9 s, one wrong, one
        // timed out: median 0.9 s, wrong 1 of 4 answered. Heading 9 of 10;
        // nearer 4 of 5. In play, one lock of two was beyond range.
        assert!(close(value(&report, 1, "Median time to select"), 0.9));
        assert!(close(value(&report, 1, "Wrong-target selections"), 0.25));
        assert!(close(value(&report, 1, "Headings judged"), 0.9));
        assert!(close(value(&report, 1, "Nearer target chosen"), 0.8));
        assert!(close(value(&report, 1, "In play: lock attempts"), 0.5));
        let met: Vec<Option<bool>> = report.gates[1].measures[..4]
            .iter()
            .map(Measure::met)
            .collect();
        assert_eq!(met, [Some(true), Some(false), Some(true), Some(false)]);

        // Gate 3. Two defeats: pilot 1 relaunches in the same session, pilot
        // 2 never does: 1 of 2. Pilot 1's day-one session ends docked and
        // they return the next day; their day-two session ends docked with no
        // later one: 1 of 2. Pilot 2's session ends in defeat: 0 of 1. The
        // relaunch after r2 stakes nothing against 60. At run 3 only pilot 1
        // has played three runs: one choice for one pilot.
        let relaunch = &report.gates[2].measures[0];
        assert!(close(relaunch.value, 0.5));
        assert_eq!(relaunch.met(), Some(false), "short of 60%");
        assert!(close(
            value(
                &report,
                2,
                "Next-day return after a session ending in defeat"
            ),
            0.0
        ));
        assert!(close(
            value(&report, 2, "Next-day return after a session ending docked"),
            0.5
        ));
        assert!(close(
            value(&report, 2, "Stake on the launch after a defeat"),
            0.0
        ));
        assert!(close(
            value(
                &report,
                2,
                "Distinct choices per pilot on one seed at run 3"
            ),
            1.0
        ));
        assert_eq!(
            value(
                &report,
                2,
                "Distinct choices per pilot on one seed at run 10"
            ),
            None
        );

        // Gate 4. Travel 300 + 200 + 100 + 400 + 0 = 1,000 s of 7,200 s
        // played. The rig lost at minute 40 of day one flies again at minute
        // 5 of day two: 1,405 minutes. First decisions 90, 30, 40 and 60 s
        // (r3 has none): median 50 s.
        assert!(close(value(&report, 3, "Travel"), 1_000.0 / 7_200.0));
        assert!(close(value(&report, 3, "Minutes from losing"), 1_405.0));
        assert!(close(value(&report, 3, "Seconds from launch"), 50.0));
        let met: Vec<Option<bool>> = report.gates[3].measures.iter().map(Measure::met).collect();
        assert_eq!(met, [Some(true), Some(false), Some(true)]);

        // Crew evidence: one share in a crew of one, one in a crew of two.
        assert!(close(
            value(&report, 4, "Pot share per pilot, crew of 1"),
            100.0
        ));
        assert!(close(
            value(&report, 4, "Pot share per pilot, crew of 2"),
            80.0
        ));
    }

    #[test]
    fn a_reload_does_not_start_a_new_session() {
        let events = vec![
            at(10 * M, 1, None, GateEvent::SessionEnded { millis: 600_000 }),
            at(25 * M, 1, None, GateEvent::SessionEnded { millis: 840_000 }),
            at(90 * M, 1, None, GateEvent::SessionEnded { millis: 300_000 }),
        ];
        let spans = sessions(&events);
        let list = &spans[&1];
        assert_eq!(
            list.len(),
            2,
            "eleven minutes apart joins; over half an hour does not"
        );
        assert_eq!((list[0].start, list[0].end), (0, 25 * M));
    }

    #[test]
    fn an_empty_world_reports_nothing_measured_and_renders() {
        let report = compute(&[], &[], Players::Owner, Vec::new());
        assert_eq!((report.pilots, report.runs), (0, 0));
        let markdown = report.markdown();
        assert!(markdown.starts_with("# Stage 1 gate report"));
        assert!(markdown.contains("none given: pass the greybox's downloaded JSON files"));
        assert!(
            report
                .gates
                .iter()
                .flat_map(|g| &g.measures)
                .all(|m| m.met().is_none())
        );
    }

    #[test]
    fn the_markdown_shows_values_targets_verdicts_and_bases() {
        let markdown = compute(&history(), &[greybox()], Players::Testers, Vec::new()).markdown();
        assert!(markdown.contains(
            "| Median time to select a named hostile (normal vision) | 0.9 s | ≤ 1.0 s | met | solo, skill | 1 | 3 selections |"
        ));
        assert!(markdown.contains(
            "| Defeats followed by a launch in that session or the next | 50% | ≥ 60% | failed | other players | 2 | 2 defeats |"
        ));
        assert!(markdown.contains("The pilots are testers other than the owner."));
    }

    /// Pilot 1's play alone: the owner's.
    fn owner_alone() -> Vec<Recorded> {
        history().into_iter().filter(|r| r.account == 1).collect()
    }

    fn measure<'a>(report: &'a Report, gate: usize, name: &str) -> &'a Measure {
        report.gates[gate]
            .measures
            .iter()
            .find(|m| m.name.starts_with(name))
            .unwrap_or_else(|| panic!("no measure {name}"))
    }

    #[test]
    fn one_pilot_leaves_gates_1_and_3_unproven_however_the_figures_fall() {
        // Plan 7.3. Pilot 1 reconfigures in their first session and relaunches
        // after their only defeat: both figures clear their thresholds, and
        // neither may count, because no one but the owner played.
        let report = compute(&owner_alone(), &[greybox()], Players::Owner, Vec::new());
        assert_eq!(report.pilots, 1);
        let early = measure(&report, 0, "Pilots who reconfigure");
        let relaunch = measure(&report, 2, "Defeats followed by a launch");
        assert_eq!((early.met(), relaunch.met()), (Some(true), Some(true)));
        for measure in [early, relaunch] {
            assert_eq!(measure.verdict(Players::Owner), Some(Verdict::Unproven));
        }
        assert_eq!(
            report.gates[0].verdict(Players::Owner),
            Some(Verdict::Unproven)
        );
        assert_eq!(
            report.gates[2].verdict(Players::Owner),
            Some(Verdict::Unproven)
        );
        // Even a measure with nothing to measure is unproven, not blank.
        let tenth = measure(
            &report,
            2,
            "Distinct choices per pilot on one seed at run 10",
        );
        assert_eq!(tenth.value, None);
        assert_eq!(tenth.verdict(Players::Owner), Some(Verdict::Unproven));

        let markdown = report.markdown();
        assert!(!markdown.contains("| met | other players |"));
        assert_eq!(markdown.matches("Gate: **unproven**").count(), 2);
        assert!(markdown.contains("Every pilot is taken to be the owner"));
    }

    #[test]
    fn the_owner_can_fail_a_gate_but_pass_a_skill_measure_only_as_the_owner() {
        let report = compute(&owner_alone(), &[greybox()], Players::Owner, Vec::new());
        // Gate 2: a 0.9 s median selection passes, by the owner only; a
        // quarter of selections wrong fails outright.
        let select = measure(&report, 1, "Median time to select");
        let wrong = measure(&report, 1, "Wrong-target selections");
        assert_eq!(
            select.verdict(Players::Owner),
            Some(Verdict::MetByOwnerOnly)
        );
        assert_eq!(wrong.verdict(Players::Owner), Some(Verdict::Failed));
        assert_eq!(
            report.gates[1].verdict(Players::Owner),
            Some(Verdict::Failed)
        );
        // Gate 4: travel, 1,000 s of 5,400 s played, counts in full; the
        // first decision is skill; the 1,405-minute recovery fails the gate.
        let travel = measure(&report, 3, "Travel");
        assert!(close(travel.value, 1_000.0 / 5_400.0));
        assert_eq!(travel.verdict(Players::Owner), Some(Verdict::Met));
        assert_eq!(
            measure(&report, 3, "Seconds from launch").verdict(Players::Owner),
            Some(Verdict::MetByOwnerOnly)
        );
        assert_eq!(
            report.gates[3].verdict(Players::Owner),
            Some(Verdict::Failed)
        );
    }

    #[test]
    fn a_gate_met_on_skill_alone_is_met_by_the_owner_only() {
        let gate = Gate {
            title: "test",
            measures: vec![
                Measure::new("a", Some(1.0), "s", Basis::Solo, 1, String::new())
                    .target(Target::AtMost(2.0)),
                Measure::new("b", Some(1.0), "s", Basis::Skill, 1, String::new())
                    .target(Target::AtMost(2.0)),
                Measure::new("c", None, "s", Basis::Skill, 0, String::new()),
            ],
        };
        assert_eq!(gate.verdict(Players::Owner), Some(Verdict::MetByOwnerOnly));
        assert_eq!(gate.verdict(Players::Testers), Some(Verdict::Met));
    }

    #[test]
    fn testers_can_meet_what_the_owner_cannot() {
        let report = compute(&owner_alone(), &[], Players::Testers, Vec::new());
        assert_eq!(
            report.gates[0].verdict(Players::Testers),
            Some(Verdict::Met)
        );
        assert_eq!(
            report.gates[2].verdict(Players::Testers),
            Some(Verdict::Met)
        );
    }

    #[test]
    fn each_measure_counts_the_pilots_it_rests_on() {
        let report = compute(&history(), &[greybox()], Players::Testers, Vec::new());
        // Both pilots were destroyed once; only pilot 1 lost a fitting; the
        // different-layout pairs span both; one greybox file.
        assert_eq!(measure(&report, 2, "Defeats followed").pilots, 2);
        assert_eq!(measure(&report, 3, "Minutes from losing").pilots, 1);
        assert_eq!(measure(&report, 0, "Same seed, different layout").pilots, 2);
        assert_eq!(measure(&report, 0, "Same seed, same layout").pilots, 0);
        assert_eq!(measure(&report, 1, "Median time to select").pilots, 1);
        assert_eq!(measure(&report, 1, "In play: lock attempts").pilots, 2);
    }

    #[test]
    fn gate_1_carries_the_rules_check_and_its_table_for_the_pool() {
        let rules = rules_check(frontier_sim::site::FRONTIER_SITE, &PLAYTEST_POOL);
        let report = compute(&owner_alone(), &[], Players::Owner, rules);
        // On every seed of the pool, 96 of 1,378 pairs change the best choice,
        // the firmest by 2 credits of 260 (frontier-sim's choice tests).
        let changes = measure(&report, 0, "Rules: layout pairs");
        assert!(close(changes.value, 96.0 / 1378.0));
        assert_eq!(changes.verdict(Players::Owner), Some(Verdict::Met));
        let firmest = measure(&report, 0, "Rules: how firmly");
        assert!(close(firmest.value, 2.0 / 260.0));
        assert_eq!(firmest.verdict(Players::Owner), None, "no threshold");
        // The rules measure is met, but gate 1 stays unproven on the owner's
        // play: the measures that compare players are not.
        assert_eq!(
            report.gates[0].verdict(Players::Owner),
            Some(Verdict::Unproven)
        );

        let markdown = report.markdown();
        assert!(markdown.contains("## Gate 1 rules check"));
        assert!(markdown.contains("(53 layouts) is compared with every other (1378 pairs)"));
        assert!(markdown.contains(
            "| 8 | Thermal flare | Derelict | Derelict, extraction rig (616 cr) | 96 of 1378 | 2 cr of 260 cr |"
        ));
        assert!(markdown.contains(
            "| Rules: layout pairs where a facility changes the best choice open under both | 7% | > 0% | met | rules | – |"
        ));
    }
}
