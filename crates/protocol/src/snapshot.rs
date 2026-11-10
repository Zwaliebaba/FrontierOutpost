//! Snapshots: the server's view of every entity, sent each tick as one
//! unreliable datagram.
//!
//! A snapshot must fit a single datagram, so it is binary, not JSON. Values are
//! quantised into small integers; the types below hold those integers, so a
//! snapshot round-trips exactly. [`Snapshot::encode`] and [`Snapshot::decode`]
//! define the layout; the TypeScript decoder in `client/src/net/snapshot.ts`
//! is checked against vectors written by this module's tests.
//!
//! Layout, little-endian:
//!
//! | Bytes | Field |
//! | --- | --- |
//! | 1 | message kind, [`SNAPSHOT_KIND`] |
//! | 4 | tick, `u32` |
//! | 2 | last input the server applied for this client, `u16` |
//! | 10 | the receiving pilot's own status, [`OwnStatus`]: phase `u8`, cargo `u8`, capacity `u8`, heat `u8`, extractor `u8`, extractor seconds `u8`, scan `u8` (`0xFF` for none), alert `u8`, elapsed seconds `u16` |
//! | 1 | deposit count |
//! | 1 each | units left in each deposit, in the order the client was sent them |
//! | 1 | entity count |
//! | 14 each | entities: id `u16`, kind `u8`, x `i16`, z `i16`, heading `u16`, vx `i16`, vz `i16`, hull `u8` |
//! | 1 | event count |
//! | 6 each | events: kind `u8`, then two `u16` and a `u8` that depend on the kind |
//!
//! Events report what happened during the tick, so the client can draw it. They
//! are not repeated: a lost snapshot loses its events, which only costs a
//! visual. A fired event is kind 1: shooter, target, projectile speed in m/s.
//! A destroyed event is kind 2: ship, destroyer (`0xFFFF` for none), entity kind.

use ts_rs::TS;

/// First byte of every snapshot datagram, so other datagram kinds can follow.
pub const SNAPSHOT_KIND: u8 = 1;

/// Largest datagram payload the game sends. Chromium's limit is 1,024 bytes;
/// the margin leaves room for path differences.
pub const MAX_DATAGRAM_BYTES: usize = 1_000;

/// Kind, tick and input ack.
pub const HEADER_BYTES: usize = 7;
pub const OWN_STATUS_BYTES: usize = 10;
pub const ENTITY_BYTES: usize = 14;
/// Most deposits a snapshot reports.
pub const MAX_DEPOSITS: usize = 16;
pub const EVENT_BYTES: usize = 6;
/// Most events one snapshot carries. A server with more drops the rest: events
/// only drive visuals.
pub const MAX_EVENTS: usize = 32;

/// Most entities one snapshot can carry within [`MAX_DATAGRAM_BYTES`] with
/// room for [`MAX_EVENTS`].
pub const MAX_ENTITIES: usize = (MAX_DATAGRAM_BYTES
    - HEADER_BYTES
    - OWN_STATUS_BYTES
    - 1
    - MAX_DEPOSITS
    - 1
    - 1
    - MAX_EVENTS * EVENT_BYTES)
    / ENTITY_BYTES;

/// Bytes before the first entity, for a snapshot reporting `deposits` deposits.
#[must_use]
pub const fn entities_at(deposits: usize) -> usize {
    HEADER_BYTES + OWN_STATUS_BYTES + 1 + deposits + 1
}

const NO_SCAN: u8 = u8::MAX;

const FIRED: u8 = 1;
const DESTROYED: u8 = 2;
const NO_SHIP: u16 = u16::MAX;

/// Position units per metre: 1/32 m resolution over ±1,024 m.
pub const POSITION_UNITS_PER_METRE: f32 = 32.0;
/// Velocity units per metre per second: 1/64 m/s resolution over ±512 m/s.
pub const VELOCITY_UNITS_PER_METRE_PER_SECOND: f32 = 64.0;
/// Heading units per full turn.
pub const HEADING_UNITS_PER_TURN: f32 = 65_536.0;

/// What an entity is. The client draws and labels it from this.
#[derive(Debug, Clone, Copy, PartialEq, Eq, TS)]
#[ts(export, rename_all = "camelCase")]
pub enum EntityKind {
    PlayerShip,
    Drone,
    Raider,
    Warden,
}

impl EntityKind {
    const ALL: [Self; 4] = [Self::PlayerShip, Self::Drone, Self::Raider, Self::Warden];

    #[must_use]
    pub const fn code(self) -> u8 {
        self as u8
    }

    #[must_use]
    pub fn from_code(code: u8) -> Option<Self> {
        Self::ALL.get(usize::from(code)).copied()
    }
}

/// One entity, quantised. Positions are on the gameplay plane (x, z).
#[derive(Debug, Clone, Copy, PartialEq, Eq, TS)]
#[ts(export, rename_all = "camelCase")]
pub struct EntityState {
    pub id: u16,
    pub kind: EntityKind,
    /// [`POSITION_UNITS_PER_METRE`].
    pub x: i16,
    pub z: i16,
    /// [`HEADING_UNITS_PER_TURN`]; 0 faces +x and the angle grows toward +z.
    pub heading: u16,
    /// [`VELOCITY_UNITS_PER_METRE_PER_SECOND`].
    pub vx: i16,
    pub vz: i16,
    /// Remaining hull, 0 (destroyed) to 255 (intact).
    pub hull: u8,
}

/// Where the receiving pilot is in their run.
#[derive(Debug, Clone, Copy, PartialEq, Eq, TS)]
#[ts(export, rename_all = "camelCase")]
pub enum RunPhase {
    AtDock,
    Approach,
    Objective,
    Return,
    Docked,
    Destroyed,
    Abandoned,
}

impl RunPhase {
    const ALL: [Self; 7] = [
        Self::AtDock,
        Self::Approach,
        Self::Objective,
        Self::Return,
        Self::Docked,
        Self::Destroyed,
        Self::Abandoned,
    ];
}

/// What the receiving pilot's extractor is doing.
#[derive(Debug, Clone, Copy, PartialEq, Eq, TS)]
#[ts(export, rename_all = "camelCase")]
pub enum ExtractorMode {
    Off,
    On,
    Venting,
    CutOut,
}

impl ExtractorMode {
    const ALL: [Self; 4] = [Self::Off, Self::On, Self::Venting, Self::CutOut];
}

/// The receiving pilot's own ship and run, which only they are sent.
#[derive(Debug, Clone, Copy, PartialEq, Eq, TS)]
#[ts(export, rename_all = "camelCase")]
pub struct OwnStatus {
    pub phase: RunPhase,
    pub cargo: u8,
    pub capacity: u8,
    /// Extractor heat, 0 (cold) to 255 (cut-out).
    pub heat: u8,
    pub extractor: ExtractorMode,
    /// Whole seconds left of a vent or cut-out, rounded up.
    pub extractor_seconds: u8,
    /// Progress of a site scan, 0 to 254, or none.
    pub scan: Option<u8>,
    pub alert: u8,
    /// Seconds since launch.
    pub elapsed_seconds: u16,
}

impl OwnStatus {
    /// A pilot at the dock at launch, with nothing to report.
    pub const IDLE: Self = Self {
        phase: RunPhase::AtDock,
        cargo: 0,
        capacity: 0,
        heat: 0,
        extractor: ExtractorMode::Off,
        extractor_seconds: 0,
        scan: None,
        alert: 0,
        elapsed_seconds: 0,
    };
}

/// Something that happened during a tick.
#[derive(Debug, Clone, Copy, PartialEq, Eq, TS)]
#[ts(
    export,
    tag = "type",
    rename_all = "camelCase",
    rename_all_fields = "camelCase"
)]
pub enum SnapshotEvent {
    /// `shooter` fired a projectile that homes on `target` at `speed` m/s.
    Fired {
        shooter: u16,
        target: u16,
        speed: u8,
    },
    /// `ship`, of kind `kind`, was destroyed, by a shot from `by` if any.
    Destroyed {
        ship: u16,
        by: Option<u16>,
        kind: EntityKind,
    },
}

/// Every entity the client should draw at `tick`, and what happened during it.
#[derive(Debug, Clone, PartialEq, Eq, TS)]
#[ts(export, rename_all = "camelCase")]
pub struct Snapshot {
    pub tick: u32,
    /// Sequence number of the last input from this client that the server
    /// applied. The client replays its later inputs on top (prediction).
    pub input_ack: u16,
    pub own: OwnStatus,
    /// Units left in each deposit, in the order the pilot was sent them.
    pub deposits: Vec<u8>,
    pub entities: Vec<EntityState>,
    pub events: Vec<SnapshotEvent>,
}

/// Why a snapshot could not be encoded or decoded.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum SnapshotError {
    #[error("{0} entities exceed the limit of {MAX_ENTITIES} per snapshot")]
    TooManyEntities(usize),
    #[error("{0} events exceed the limit of {MAX_EVENTS} per snapshot")]
    TooManyEvents(usize),
    #[error("unknown event kind {0}")]
    UnknownEventKind(u8),
    #[error("not a snapshot datagram")]
    WrongKind,
    #[error("snapshot is {actual} bytes; its header implies {expected}")]
    WrongLength { expected: usize, actual: usize },
    #[error("unknown entity kind {0}")]
    UnknownEntityKind(u8),
    #[error("{0} deposits exceed the limit of {MAX_DEPOSITS} per snapshot")]
    TooManyDeposits(usize),
    #[error("unknown run phase {0}")]
    UnknownPhase(u8),
    #[error("unknown extractor mode {0}")]
    UnknownExtractorMode(u8),
}

impl Snapshot {
    /// Encodes the snapshot as one datagram payload.
    ///
    /// # Errors
    /// Fails if there are more than [`MAX_ENTITIES`] entities or
    /// [`MAX_EVENTS`] events.
    pub fn encode(&self) -> Result<Vec<u8>, SnapshotError> {
        let count = u8::try_from(self.entities.len())
            .ok()
            .filter(|_| self.entities.len() <= MAX_ENTITIES)
            .ok_or(SnapshotError::TooManyEntities(self.entities.len()))?;
        let event_count = u8::try_from(self.events.len())
            .ok()
            .filter(|_| self.events.len() <= MAX_EVENTS)
            .ok_or(SnapshotError::TooManyEvents(self.events.len()))?;
        let deposit_count = u8::try_from(self.deposits.len())
            .ok()
            .filter(|_| self.deposits.len() <= MAX_DEPOSITS)
            .ok_or(SnapshotError::TooManyDeposits(self.deposits.len()))?;
        let mut bytes = Vec::with_capacity(
            entities_at(self.deposits.len())
                + ENTITY_BYTES * self.entities.len()
                + 1
                + EVENT_BYTES * self.events.len(),
        );
        bytes.push(SNAPSHOT_KIND);
        bytes.extend_from_slice(&self.tick.to_le_bytes());
        bytes.extend_from_slice(&self.input_ack.to_le_bytes());
        let own = &self.own;
        bytes.extend_from_slice(&[
            own.phase as u8,
            own.cargo,
            own.capacity,
            own.heat,
            own.extractor as u8,
            own.extractor_seconds,
            // Progress tops out at 254, so 255 can mean no scan.
            own.scan.map_or(NO_SCAN, |scan| scan.min(NO_SCAN - 1)),
            own.alert,
        ]);
        bytes.extend_from_slice(&own.elapsed_seconds.to_le_bytes());
        bytes.push(deposit_count);
        bytes.extend_from_slice(&self.deposits);
        bytes.push(count);
        for entity in &self.entities {
            bytes.extend_from_slice(&entity.id.to_le_bytes());
            bytes.push(entity.kind.code());
            bytes.extend_from_slice(&entity.x.to_le_bytes());
            bytes.extend_from_slice(&entity.z.to_le_bytes());
            bytes.extend_from_slice(&entity.heading.to_le_bytes());
            bytes.extend_from_slice(&entity.vx.to_le_bytes());
            bytes.extend_from_slice(&entity.vz.to_le_bytes());
            bytes.push(entity.hull);
        }
        bytes.push(event_count);
        for event in &self.events {
            let (kind, a, b, c) = match *event {
                SnapshotEvent::Fired {
                    shooter,
                    target,
                    speed,
                } => (FIRED, shooter, target, speed),
                SnapshotEvent::Destroyed { ship, by, kind } => {
                    (DESTROYED, ship, by.unwrap_or(NO_SHIP), kind.code())
                }
            };
            bytes.push(kind);
            bytes.extend_from_slice(&a.to_le_bytes());
            bytes.extend_from_slice(&b.to_le_bytes());
            bytes.push(c);
        }
        Ok(bytes)
    }

    /// Decodes a datagram payload.
    ///
    /// # Errors
    /// Fails if the payload is not a snapshot, its length does not match its
    /// entity count, or an entity kind is unknown.
    pub fn decode(bytes: &[u8]) -> Result<Self, SnapshotError> {
        let mut reader = Reader(bytes);
        if reader.u8() != Some(SNAPSHOT_KIND) {
            return Err(SnapshotError::WrongKind);
        }
        let wrong_length = |expected| SnapshotError::WrongLength {
            expected,
            actual: bytes.len(),
        };
        let header = (reader.u32(), reader.u16());
        let (Some(tick), Some(input_ack)) = header else {
            return Err(wrong_length(HEADER_BYTES));
        };
        let own_bytes = reader
            .take::<OWN_STATUS_BYTES>()
            .ok_or_else(|| wrong_length(HEADER_BYTES + OWN_STATUS_BYTES))?;
        let [
            phase,
            cargo,
            capacity,
            heat,
            extractor,
            extractor_seconds,
            scan,
            alert,
            e0,
            e1,
        ] = own_bytes;
        let own = OwnStatus {
            phase: RunPhase::ALL
                .get(usize::from(phase))
                .copied()
                .ok_or(SnapshotError::UnknownPhase(phase))?,
            cargo,
            capacity,
            heat,
            extractor: ExtractorMode::ALL
                .get(usize::from(extractor))
                .copied()
                .ok_or(SnapshotError::UnknownExtractorMode(extractor))?,
            extractor_seconds,
            scan: (scan != NO_SCAN).then_some(scan),
            alert,
            elapsed_seconds: u16::from_le_bytes([e0, e1]),
        };
        let deposit_count = reader
            .u8()
            .ok_or_else(|| wrong_length(HEADER_BYTES + OWN_STATUS_BYTES + 1))?;
        let deposits = reader
            .0
            .get(..usize::from(deposit_count))
            .ok_or_else(|| wrong_length(entities_at(usize::from(deposit_count))))?
            .to_vec();
        reader.0 = &reader.0[usize::from(deposit_count)..];
        let count_at = entities_at(usize::from(deposit_count)) - 1;
        let count = reader.u8().ok_or_else(|| wrong_length(count_at + 1))?;
        let events_at = count_at + 1 + ENTITY_BYTES * usize::from(count);
        let event_count = bytes
            .get(events_at)
            .copied()
            .ok_or_else(|| wrong_length(events_at + 1))?;
        let expected = events_at + 1 + EVENT_BYTES * usize::from(event_count);
        if bytes.len() != expected {
            return Err(wrong_length(expected));
        }

        // The length is checked, so every read below succeeds.
        let mut entities = Vec::with_capacity(usize::from(count));
        for _ in 0..count {
            let id = reader.u16().ok_or_else(|| wrong_length(expected))?;
            let code = reader.u8().ok_or_else(|| wrong_length(expected))?;
            let kind = EntityKind::from_code(code).ok_or(SnapshotError::UnknownEntityKind(code))?;
            let mut i16s = [0_i16; 5];
            for value in &mut i16s {
                *value = reader
                    .u16()
                    .ok_or_else(|| wrong_length(expected))?
                    .cast_signed();
            }
            let [x, z, heading, vx, vz] = i16s;
            let hull = reader.u8().ok_or_else(|| wrong_length(expected))?;
            entities.push(EntityState {
                id,
                kind,
                x,
                z,
                heading: heading.cast_unsigned(),
                vx,
                vz,
                hull,
            });
        }
        reader.u8();
        let mut events = Vec::with_capacity(usize::from(event_count));
        for _ in 0..event_count {
            let fields = (reader.u8(), reader.u16(), reader.u16(), reader.u8());
            let (Some(kind), Some(a), Some(b), Some(c)) = fields else {
                return Err(wrong_length(expected));
            };
            events.push(match kind {
                FIRED => SnapshotEvent::Fired {
                    shooter: a,
                    target: b,
                    speed: c,
                },
                DESTROYED => SnapshotEvent::Destroyed {
                    ship: a,
                    by: (b != NO_SHIP).then_some(b),
                    kind: EntityKind::from_code(c).ok_or(SnapshotError::UnknownEntityKind(c))?,
                },
                other => return Err(SnapshotError::UnknownEventKind(other)),
            });
        }
        Ok(Self {
            tick,
            input_ack,
            own,
            deposits,
            entities,
            events,
        })
    }
}

/// Reads little-endian integers from the front of a byte slice.
struct Reader<'a>(&'a [u8]);

impl Reader<'_> {
    fn take<const N: usize>(&mut self) -> Option<[u8; N]> {
        let (head, rest) = self.0.split_first_chunk::<N>()?;
        self.0 = rest;
        Some(*head)
    }
    fn u8(&mut self) -> Option<u8> {
        self.take::<1>().map(|[byte]| byte)
    }
    fn u16(&mut self) -> Option<u16> {
        self.take().map(u16::from_le_bytes)
    }
    fn u32(&mut self) -> Option<u32> {
        self.take().map(u32::from_le_bytes)
    }
}

/// Quantises a position in metres, saturating at the representable range.
#[must_use]
pub fn quantize_position(metres: f32) -> i16 {
    saturate(metres * POSITION_UNITS_PER_METRE)
}

/// Quantises a velocity in metres per second, saturating at the representable range.
#[must_use]
pub fn quantize_velocity(metres_per_second: f32) -> i16 {
    saturate(metres_per_second * VELOCITY_UNITS_PER_METRE_PER_SECOND)
}

/// Quantises a heading in radians; any angle wraps into one turn.
#[must_use]
pub fn quantize_heading(radians: f32) -> u16 {
    let turns = (radians / std::f32::consts::TAU).rem_euclid(1.0);
    // `as` saturates, and 1.0 turn wraps to 0 through the modulo below.
    ((turns * HEADING_UNITS_PER_TURN).round() as u32 % 65_536) as u16
}

fn saturate(units: f32) -> i16 {
    // Float-to-int `as` saturates and maps NaN to 0.
    units.round() as i16
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entity(id: u16, kind: EntityKind) -> EntityState {
        EntityState {
            id,
            kind,
            x: -1234,
            z: 4321,
            heading: 16_384,
            vx: 300,
            vz: -300,
            hull: 200,
        }
    }

    fn working() -> OwnStatus {
        OwnStatus {
            phase: RunPhase::Objective,
            cargo: 13,
            capacity: 20,
            heat: 180,
            extractor: ExtractorMode::On,
            extractor_seconds: 0,
            scan: Some(77),
            alert: 2,
            elapsed_seconds: 1_234,
        }
    }

    /// Five deposits, as the ore field has.
    const DEPOSITS: usize = 5;

    /// The Stage 1 worst case of design section 10: four pilots and the NPC cap.
    fn full_expedition() -> Snapshot {
        let kinds = [EntityKind::Drone, EntityKind::Raider, EntityKind::Warden];
        let ships = (0..4).map(|id| entity(id, EntityKind::PlayerShip));
        let npcs = (4..28).map(|id| entity(id, kinds[usize::from(id) % 3]));
        Snapshot {
            tick: 123_456,
            input_ack: 789,
            own: working(),
            deposits: vec![8, 8, 0, 12, 3],
            entities: ships.chain(npcs).collect(),
            events: vec![
                SnapshotEvent::Fired {
                    shooter: 0,
                    target: 5,
                    speed: 30,
                },
                SnapshotEvent::Destroyed {
                    ship: 6,
                    by: Some(1),
                    kind: EntityKind::Raider,
                },
                SnapshotEvent::Destroyed {
                    ship: 2,
                    by: None,
                    kind: EntityKind::PlayerShip,
                },
            ],
        }
    }

    /// The most a snapshot may carry: every limit together.
    fn largest() -> Snapshot {
        Snapshot {
            deposits: vec![1; MAX_DEPOSITS],
            entities: (0..MAX_ENTITIES)
                .map(|i| entity(u16::try_from(i).unwrap(), EntityKind::Drone))
                .collect(),
            events: vec![
                SnapshotEvent::Fired {
                    shooter: 1,
                    target: 2,
                    speed: 30,
                };
                MAX_EVENTS
            ],
            ..full_expedition()
        }
    }

    #[test]
    fn four_ships_and_twenty_four_npcs_fit_one_datagram() {
        let bytes = full_expedition().encode().unwrap();
        assert!(bytes.len() <= MAX_DATAGRAM_BYTES, "{} bytes", bytes.len());
        assert_eq!(
            bytes.len(),
            entities_at(DEPOSITS) + 28 * ENTITY_BYTES + 1 + 3 * EVENT_BYTES
        );
    }

    #[test]
    fn round_trips_exactly() {
        let snapshot = full_expedition();
        assert_eq!(
            Snapshot::decode(&snapshot.encode().unwrap()).unwrap(),
            snapshot
        );
    }

    #[test]
    fn round_trips_the_extremes_of_every_field() {
        let snapshot = Snapshot {
            tick: u32::MAX,
            input_ack: u16::MAX,
            own: OwnStatus {
                phase: RunPhase::Abandoned,
                cargo: u8::MAX,
                capacity: u8::MAX,
                heat: u8::MAX,
                extractor: ExtractorMode::CutOut,
                extractor_seconds: u8::MAX,
                scan: Some(254),
                alert: u8::MAX,
                elapsed_seconds: u16::MAX,
            },
            deposits: vec![u8::MAX; MAX_DEPOSITS],
            entities: vec![
                EntityState {
                    id: u16::MAX,
                    kind: EntityKind::Warden,
                    x: i16::MIN,
                    z: i16::MAX,
                    heading: u16::MAX,
                    vx: i16::MAX,
                    vz: i16::MIN,
                    hull: u8::MAX,
                },
                EntityState {
                    id: 0,
                    kind: EntityKind::PlayerShip,
                    x: 0,
                    z: 0,
                    heading: 0,
                    vx: 0,
                    vz: 0,
                    hull: 0,
                },
            ],
            events: vec![SnapshotEvent::Destroyed {
                ship: u16::MAX - 1,
                by: Some(0),
                kind: EntityKind::Warden,
            }],
        };
        assert_eq!(
            Snapshot::decode(&snapshot.encode().unwrap()).unwrap(),
            snapshot
        );
    }

    #[test]
    fn the_largest_snapshot_still_fits() {
        let mut snapshot = largest();
        assert!(snapshot.encode().unwrap().len() <= MAX_DATAGRAM_BYTES);
        snapshot.entities.push(entity(999, EntityKind::Drone));
        assert_eq!(
            snapshot.encode(),
            Err(SnapshotError::TooManyEntities(MAX_ENTITIES + 1))
        );
        let mut snapshot = largest();
        snapshot.events.push(snapshot.events[0]);
        assert_eq!(
            snapshot.encode(),
            Err(SnapshotError::TooManyEvents(MAX_EVENTS + 1))
        );
        let mut snapshot = largest();
        snapshot.deposits.push(1);
        assert_eq!(
            snapshot.encode(),
            Err(SnapshotError::TooManyDeposits(MAX_DEPOSITS + 1))
        );
    }

    #[test]
    fn rejects_malformed_datagrams() {
        let bytes = full_expedition().encode().unwrap();
        assert_eq!(Snapshot::decode(&[]), Err(SnapshotError::WrongKind));
        let mut other = bytes.clone();
        other[0] = 2;
        assert_eq!(Snapshot::decode(&other), Err(SnapshotError::WrongKind));
        assert!(matches!(
            Snapshot::decode(&bytes[..bytes.len() - 1]),
            Err(SnapshotError::WrongLength { .. })
        ));
        assert!(matches!(
            Snapshot::decode(&bytes[..5]),
            Err(SnapshotError::WrongLength { .. })
        ));
        let mut padded = bytes.clone();
        padded.push(0);
        assert!(matches!(
            Snapshot::decode(&padded),
            Err(SnapshotError::WrongLength { .. })
        ));
        let mut unknown = bytes.clone();
        unknown[entities_at(DEPOSITS) + 2] = 9;
        assert_eq!(
            Snapshot::decode(&unknown),
            Err(SnapshotError::UnknownEntityKind(9))
        );
        let mut phase = bytes.clone();
        phase[HEADER_BYTES] = 9;
        assert_eq!(
            Snapshot::decode(&phase),
            Err(SnapshotError::UnknownPhase(9))
        );
        let mut mode = bytes.clone();
        mode[HEADER_BYTES + 4] = 9;
        assert_eq!(
            Snapshot::decode(&mode),
            Err(SnapshotError::UnknownExtractorMode(9))
        );
        let first_event = entities_at(DEPOSITS) + 28 * ENTITY_BYTES + 1;
        let mut unknown_event = bytes;
        unknown_event[first_event] = 7;
        assert_eq!(
            Snapshot::decode(&unknown_event),
            Err(SnapshotError::UnknownEventKind(7))
        );
    }

    #[test]
    fn quantises_positions_and_velocities_with_saturation() {
        assert_eq!(quantize_position(1.0), 32);
        assert_eq!(quantize_position(-0.5), -16);
        assert_eq!(quantize_position(10_000.0), i16::MAX);
        assert_eq!(quantize_position(-10_000.0), i16::MIN);
        assert_eq!(quantize_position(f32::NAN), 0);
        assert_eq!(quantize_velocity(9.0), 576);
        assert_eq!(quantize_velocity(1_000.0), i16::MAX);
    }

    #[test]
    fn quantises_headings_into_one_turn() {
        use std::f32::consts::{PI, TAU};
        assert_eq!(quantize_heading(0.0), 0);
        assert_eq!(quantize_heading(PI / 2.0), 16_384);
        assert_eq!(quantize_heading(PI), 32_768);
        assert_eq!(quantize_heading(-PI / 2.0), 49_152);
        assert_eq!(quantize_heading(TAU), 0);
        assert_eq!(quantize_heading(TAU + PI), 32_768);
        assert_eq!(quantize_heading(TAU - 1e-7), 0);
    }

    #[test]
    fn entity_kind_codes_round_trip() {
        for kind in EntityKind::ALL {
            assert_eq!(EntityKind::from_code(kind.code()), Some(kind));
        }
        assert_eq!(EntityKind::from_code(4), None);
    }

    /// Writes vectors for the TypeScript decoder's tests beside the generated
    /// types. CI fails if the committed file is stale.
    #[test]
    fn exports_test_vectors_to_typescript() {
        let Ok(dir) = std::env::var("TS_RS_EXPORT_DIR") else {
            return;
        };
        let hex = |bytes: &[u8]| bytes.iter().map(|b| format!("{b:02x}")).collect::<String>();
        let kind = |kind: EntityKind| match kind {
            EntityKind::PlayerShip => "playerShip",
            EntityKind::Drone => "drone",
            EntityKind::Raider => "raider",
            EntityKind::Warden => "warden",
        };
        let json = |snapshot: &Snapshot| {
            let entities = snapshot
                .entities
                .iter()
                .map(|e| {
                    format!(
                        "{{ id: {}, kind: \"{}\", x: {}, z: {}, heading: {}, vx: {}, vz: {}, hull: {} }}",
                        e.id,
                        kind(e.kind),
                        e.x,
                        e.z,
                        e.heading,
                        e.vx,
                        e.vz,
                        e.hull
                    )
                })
                .collect::<Vec<_>>()
                .join(", ");
            let events = snapshot
                .events
                .iter()
                .map(|event| match *event {
                    SnapshotEvent::Fired {
                        shooter,
                        target,
                        speed,
                    } => format!(
                        "{{ type: \"fired\", shooter: {shooter}, target: {target}, speed: {speed} }}"
                    ),
                    SnapshotEvent::Destroyed { ship, by, kind: k } => format!(
                        "{{ type: \"destroyed\", ship: {ship}, by: {}, kind: \"{}\" }}",
                        by.map_or_else(|| "null".to_owned(), |by| by.to_string()),
                        kind(k)
                    ),
                })
                .collect::<Vec<_>>()
                .join(", ");
            let own = &snapshot.own;
            let camel = |debug: String| {
                let mut chars = debug.chars();
                chars.next().map_or_else(String::new, |first| {
                    first.to_lowercase().chain(chars).collect()
                })
            };
            let own = format!(
                "{{ phase: \"{}\", cargo: {}, capacity: {}, heat: {}, extractor: \"{}\", extractorSeconds: {}, scan: {}, alert: {}, elapsedSeconds: {} }}",
                camel(format!("{:?}", own.phase)),
                own.cargo,
                own.capacity,
                own.heat,
                camel(format!("{:?}", own.extractor)),
                own.extractor_seconds,
                own.scan
                    .map_or_else(|| "null".to_owned(), |scan| scan.to_string()),
                own.alert,
                own.elapsed_seconds
            );
            let deposits = snapshot
                .deposits
                .iter()
                .map(u8::to_string)
                .collect::<Vec<_>>()
                .join(", ");
            format!(
                "{{ tick: {}, inputAck: {}, own: {own}, deposits: [{deposits}], entities: [{entities}], events: [{events}] }}",
                snapshot.tick, snapshot.input_ack
            )
        };

        let empty = Snapshot {
            tick: 0,
            input_ack: 0,
            own: OwnStatus::IDLE,
            deposits: vec![],
            entities: vec![],
            events: vec![],
        };
        let extremes = Snapshot {
            tick: u32::MAX,
            input_ack: u16::MAX,
            own: OwnStatus {
                phase: RunPhase::Abandoned,
                cargo: u8::MAX,
                capacity: u8::MAX,
                heat: u8::MAX,
                extractor: ExtractorMode::CutOut,
                extractor_seconds: u8::MAX,
                scan: Some(254),
                alert: u8::MAX,
                elapsed_seconds: u16::MAX,
            },
            deposits: vec![u8::MAX; MAX_DEPOSITS],
            entities: vec![EntityState {
                id: u16::MAX,
                kind: EntityKind::Warden,
                x: i16::MIN,
                z: i16::MAX,
                heading: u16::MAX,
                vx: i16::MAX,
                vz: i16::MIN,
                hull: u8::MAX,
            }],
            events: vec![SnapshotEvent::Destroyed {
                ship: 1,
                by: None,
                kind: EntityKind::Drone,
            }],
        };
        let valid = [
            ("empty", empty),
            ("extremes", extremes),
            ("full expedition", full_expedition()),
        ]
        .iter()
        .map(|(name, snapshot)| {
            format!(
                "  {{ name: \"{name}\", hex: \"{}\", snapshot: {} }},\n",
                hex(&snapshot.encode().unwrap()),
                json(snapshot)
            )
        })
        .collect::<String>();

        let good = full_expedition().encode().unwrap();
        let mut wrong_kind = good.clone();
        wrong_kind[0] = 2;
        let mut unknown_entity = good.clone();
        unknown_entity[entities_at(DEPOSITS) + 2] = 9;
        let mut unknown_phase = good.clone();
        unknown_phase[HEADER_BYTES] = 9;
        let mut unknown_mode = good.clone();
        unknown_mode[HEADER_BYTES + 4] = 9;
        let first_event = entities_at(DEPOSITS) + 28 * ENTITY_BYTES + 1;
        let mut unknown_event = good.clone();
        unknown_event[first_event] = 7;
        let mut unknown_destroyed_kind = good.clone();
        unknown_destroyed_kind[first_event + EVENT_BYTES + 5] = 9;
        let invalid = [
            ("empty datagram", vec![]),
            ("wrong kind", wrong_kind),
            ("truncated header", good[..5].to_vec()),
            (
                "truncated own status",
                good[..HEADER_BYTES + OWN_STATUS_BYTES - 2].to_vec(),
            ),
            (
                "truncated deposits",
                good[..entities_at(DEPOSITS) - 3].to_vec(),
            ),
            (
                "truncated entity",
                good[..entities_at(DEPOSITS) + 3 * ENTITY_BYTES + 4].to_vec(),
            ),
            ("missing event count", good[..first_event - 1].to_vec()),
            ("truncated event", good[..good.len() - 1].to_vec()),
            ("trailing byte", [good.as_slice(), &[0]].concat()),
            ("unknown entity kind", unknown_entity),
            ("unknown run phase", unknown_phase),
            ("unknown extractor mode", unknown_mode),
            ("unknown event kind", unknown_event),
            ("unknown destroyed kind", unknown_destroyed_kind),
        ]
        .iter()
        .map(|(name, bytes)| format!("  {{ name: \"{name}\", hex: \"{}\" }},\n", hex(bytes)))
        .collect::<String>();

        let contents = format!(
            "// This file was generated by frontier-protocol's tests. Do not edit this file manually.\n\n\
             import type {{ Snapshot }} from \"./Snapshot\";\n\n\
             export const MAX_DATAGRAM_BYTES = {MAX_DATAGRAM_BYTES};\n\n\
             /** Datagrams and the snapshots they decode to, from the Rust encoder. */\n\
             export const VALID_SNAPSHOTS: {{ name: string; hex: string; snapshot: Snapshot }}[] = [\n{valid}];\n\n\
             /** Datagrams the decoder must reject. */\n\
             export const INVALID_SNAPSHOTS: {{ name: string; hex: string }}[] = [\n{invalid}];\n"
        );
        let path = std::path::Path::new(&dir).join("snapshotVectors.ts");
        if std::fs::read_to_string(&path).ok().as_deref() != Some(contents.as_str()) {
            std::fs::write(path, contents).unwrap();
        }
    }
}
