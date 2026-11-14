/**
 * Decoder for snapshot datagrams. The layout is defined by `frontier_protocol::snapshot`;
 * the tests check this decoder against vectors written by the Rust encoder.
 */
import type {
  Drive,
  EntityKind,
  EntityState,
  ExtractorMode,
  OwnStatus,
  RunPhase,
  Snapshot,
  SnapshotEvent,
} from "../protocol";

export const SNAPSHOT_KIND = 1;
const HEADER_BYTES = 7;
const OWN_STATUS_BYTES = 13;
const ENTITY_BYTES = 14;
const NO_SCAN = 0xff;
const PHASES: readonly RunPhase[] = [
  "atDock",
  "approach",
  "objective",
  "return",
  "docked",
  "destroyed",
  "abandoned",
];
const EXTRACTOR_MODES: readonly ExtractorMode[] = ["off", "on", "venting", "cutOut"];
const DRIVES: readonly Drive[] = ["ready", "inSite", "hunted", "extracting"];
const EVENT_BYTES = 6;
const FIRED = 1;
const DESTROYED = 2;
const INTERCEPTED = 3;
const NO_SHIP = 0xffff;
const ENTITY_KINDS: readonly EntityKind[] = [
  "playerShip",
  "drone",
  "raider",
  "warden",
  "core",
  "wingman",
];

export const POSITION_UNITS_PER_METRE = 32;
export const VELOCITY_UNITS_PER_METRE_PER_SECOND = 64;
export const HEADING_UNITS_PER_TURN = 65_536;

export class SnapshotError extends Error {
  override name = "SnapshotError";
}

/** Decodes one datagram. Throws `SnapshotError` if it is not a well-formed snapshot. */
export function decodeSnapshot(bytes: Uint8Array): Snapshot {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < 1 || view.getUint8(0) !== SNAPSHOT_KIND) {
    throw new SnapshotError("not a snapshot datagram");
  }
  if (bytes.length < HEADER_BYTES + OWN_STATUS_BYTES + 1) {
    throw new SnapshotError(`snapshot header is ${String(bytes.length)} bytes`);
  }
  const own = decodeOwn(view);
  const depositCount = view.getUint8(HEADER_BYTES + OWN_STATUS_BYTES);
  const depositsAt = HEADER_BYTES + OWN_STATUS_BYTES + 1;
  const countAt = depositsAt + depositCount;
  if (bytes.length <= countAt) {
    throw new SnapshotError(`snapshot ends in its deposits, at ${String(bytes.length)} bytes`);
  }
  const deposits = Array.from(bytes.subarray(depositsAt, countAt));
  const entitiesAt = countAt + 1;
  const count = view.getUint8(countAt);
  const eventsAt = entitiesAt + ENTITY_BYTES * count;
  if (bytes.length <= eventsAt) {
    throw new SnapshotError(
      `snapshot ends before its event count, at ${String(bytes.length)} bytes`,
    );
  }
  const eventCount = view.getUint8(eventsAt);
  const expected = eventsAt + 1 + EVENT_BYTES * eventCount;
  if (bytes.length !== expected) {
    throw new SnapshotError(
      `snapshot is ${String(bytes.length)} bytes; its header implies ${String(expected)}`,
    );
  }
  const entities: EntityState[] = [];
  for (let i = 0; i < count; i += 1) {
    const at = entitiesAt + ENTITY_BYTES * i;
    const code = view.getUint8(at + 2);
    const kind = ENTITY_KINDS[code];
    if (kind === undefined) throw new SnapshotError(`unknown entity kind ${String(code)}`);
    entities.push({
      id: view.getUint16(at, true),
      kind,
      x: view.getInt16(at + 3, true),
      z: view.getInt16(at + 5, true),
      heading: view.getUint16(at + 7, true),
      vx: view.getInt16(at + 9, true),
      vz: view.getInt16(at + 11, true),
      hull: view.getUint8(at + 13),
    });
  }
  const events: SnapshotEvent[] = [];
  for (let i = 0; i < eventCount; i += 1) {
    const at = eventsAt + 1 + EVENT_BYTES * i;
    const a = view.getUint16(at + 1, true);
    const b = view.getUint16(at + 3, true);
    const c = view.getUint8(at + 5);
    switch (view.getUint8(at)) {
      case FIRED:
        events.push({ type: "fired", shooter: a, target: b, speed: c });
        break;
      case DESTROYED: {
        const kind = ENTITY_KINDS[c];
        if (kind === undefined) throw new SnapshotError(`unknown entity kind ${String(c)}`);
        events.push({ type: "destroyed", ship: a, by: b === NO_SHIP ? null : b, kind });
        break;
      }
      case INTERCEPTED:
        events.push({ type: "intercepted", shooter: a, target: b });
        break;
      default:
        throw new SnapshotError(`unknown event kind ${String(view.getUint8(at))}`);
    }
  }
  return {
    tick: view.getUint32(1, true),
    inputAck: view.getUint16(5, true),
    own,
    deposits,
    entities,
    events,
  };
}

function decodeOwn(view: DataView): OwnStatus {
  const at = HEADER_BYTES;
  const phase = PHASES[view.getUint8(at)];
  if (phase === undefined)
    throw new SnapshotError(`unknown run phase ${String(view.getUint8(at))}`);
  const extractor = EXTRACTOR_MODES[view.getUint8(at + 4)];
  if (extractor === undefined) {
    throw new SnapshotError(`unknown extractor mode ${String(view.getUint8(at + 4))}`);
  }
  const scan = view.getUint8(at + 6);
  const drive = DRIVES[view.getUint8(at + 10)];
  if (drive === undefined) {
    throw new SnapshotError(`unknown drive state ${String(view.getUint8(at + 10))}`);
  }
  return {
    phase,
    cargo: view.getUint8(at + 1),
    capacity: view.getUint8(at + 2),
    heat: view.getUint8(at + 3),
    extractor,
    extractorSeconds: view.getUint8(at + 5),
    scan: scan === NO_SCAN ? null : scan,
    alert: view.getUint8(at + 7),
    elapsedSeconds: view.getUint16(at + 8, true),
    drive,
    shield: view.getUint8(at + 11),
    relay: view.getUint8(at + 12),
  };
}

/** Position on the plane in metres. */
export function positionMetres(entity: EntityState): { x: number; z: number } {
  return { x: entity.x / POSITION_UNITS_PER_METRE, z: entity.z / POSITION_UNITS_PER_METRE };
}

/** Velocity on the plane in metres per second. */
export function velocityMetresPerSecond(entity: EntityState): { x: number; z: number } {
  return {
    x: entity.vx / VELOCITY_UNITS_PER_METRE_PER_SECOND,
    z: entity.vz / VELOCITY_UNITS_PER_METRE_PER_SECOND,
  };
}

/** Heading in radians, 0 to 2π; 0 faces +x and the angle grows toward +z. */
export function headingRadians(entity: EntityState): number {
  return (entity.heading / HEADING_UNITS_PER_TURN) * 2 * Math.PI;
}
