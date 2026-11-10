/**
 * Decoder for snapshot datagrams. The layout is defined by `frontier_protocol::snapshot`;
 * the tests check this decoder against vectors written by the Rust encoder.
 */
import type { EntityKind, EntityState, Snapshot } from "../protocol";

export const SNAPSHOT_KIND = 1;
const HEADER_BYTES = 8;
const ENTITY_BYTES = 14;
const ENTITY_KINDS: readonly EntityKind[] = ["playerShip", "drone", "raider", "warden"];

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
  if (bytes.length < HEADER_BYTES) {
    throw new SnapshotError(`snapshot header is ${String(bytes.length)} bytes`);
  }
  const count = view.getUint8(7);
  const expected = HEADER_BYTES + ENTITY_BYTES * count;
  if (bytes.length !== expected) {
    throw new SnapshotError(
      `snapshot is ${String(bytes.length)} bytes; its header implies ${String(expected)}`,
    );
  }
  const entities: EntityState[] = [];
  for (let i = 0; i < count; i += 1) {
    const at = HEADER_BYTES + ENTITY_BYTES * i;
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
  return { tick: view.getUint32(1, true), inputAck: view.getUint16(5, true), entities };
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
