/**
 * The contract and the run as the pilot reads them: launching, and the words the briefing, the
 * run line and the summary show. Kept free of the DOM so it can be tested.
 */
import type {
  DominantThreat,
  Drive,
  ExtractRefusal,
  Hazard,
  OwnStatus,
  PocketKind,
  RunPhase,
  RunSummary,
  ScanRefusal,
  ServerMessage,
  WaveInfo,
} from "../protocol";

/** `OwnStatus.relay` once both relays have been held for 30 seconds (design §10). */
export const RELAY_LINKED = 255;
import type { Connection } from "../net/connection";

export type Contract = Extract<ServerMessage, { type: "contract" }>;
export type Joined = Extract<ServerMessage, { type: "joined" }>;
export type Scanned = Extract<ServerMessage, { type: "scanned" }>;

export class LaunchRefusedError extends Error {
  override name = "LaunchRefusedError";
}

/** Sends `launch` at `tier` and waits for the server to place the pilot in a new run. */
export function launch(connection: Connection, tier: number): Promise<Joined> {
  return new Promise((resolve, reject) => {
    let settled = false;
    connection.onMessage((message: ServerMessage) => {
      if (settled) return;
      if (message.type === "joined") {
        settled = true;
        resolve(message);
      } else if (message.type === "launchRefused") {
        settled = true;
        reject(new LaunchRefusedError(message.reason));
      }
    });
    void connection.closed.then((reason) => {
      if (!settled) reject(new Error(`disconnected before launching: ${reason}`));
    });
    connection.send({ type: "launch", tier }).catch((error: unknown) => {
      if (!settled) reject(error instanceof Error ? error : new Error(String(error)));
    });
  });
}

export const POCKET_NAME: Readonly<Record<PocketKind, string>> = {
  oreField: "Ore field",
  derelict: "Derelict",
  volatileVeins: "Volatile veins",
  nestCore: "Nest core",
};

/** What opens a locked pocket (design §10), named as the briefing shows it. */
export const POCKET_NEEDS: Readonly<Record<PocketKind, string | null>> = {
  oreField: null,
  derelict: "a sensor array",
  volatileVeins: "a refinery",
  nestCore: "a fabrication bay",
};

export const HAZARD_TEXT: Readonly<Record<Hazard, string>> = {
  thermalFlare: "Thermal flare: extraction heat builds 30% faster",
  debrisDrift: "Debris drift: hitting rock between pockets damages the hull",
  ionFog: "Ion fog: scan and lock ranges are 40% shorter",
};

export const THREAT_TEXT: Readonly<Record<DominantThreat, string>> = {
  drones: "Mostly drones, which swarm extracting ships",
  raiders: "Mostly raiders, which hunt laden ships",
  balanced: "Drones and raiders alike",
};

const PHASE_TEXT: Readonly<Record<RunPhase, string>> = {
  atDock: "At the dock",
  approach: "Approach",
  objective: "In the site",
  return: "Return leg",
  docked: "Docked",
  destroyed: "Destroyed",
  abandoned: "Abandoned",
};

const REFUSAL_TEXT: Readonly<Record<ExtractRefusal, string>> = {
  notAtDeposit: "Extractor: hold still beside a deposit",
  depleted: "Extractor: this deposit is empty",
  cargoFull: "Extractor: the hold is full",
  busy: "Extractor: wait for the vent or cut-out to finish",
};

export function refusalText(reason: ExtractRefusal): string {
  return REFUSAL_TEXT[reason];
}

// Each says what to do next, as the extractor's do: a key that did nothing used to say nothing.
const SCAN_REFUSAL_TEXT: Readonly<Record<ScanRefusal, string>> = {
  notFlying: "Scan: only in flight",
  revealed: "Scan: the site is already scanned · M for the map",
  scanning: "Scan: already scanning · hold still",
  outOfRange: "Scan: out of range · fly nearer the site's centre",
  moving: "Scan: slow to a stop first",
};

export function scanRefusalText(reason: ScanRefusal): string {
  return SCAN_REFUSAL_TEXT[reason];
}

/** Minutes and seconds, such as 3:07. */
export function clock(seconds: number): string {
  const whole = Math.max(0, Math.floor(seconds));
  return `${String(Math.floor(whole / 60))}:${String(whole % 60).padStart(2, "0")}`;
}

/** How long before the next wave its cell warns, seconds. */
const WAVE_WARNING_SECONDS = 30;

/** How urgent a cell of the run status is. Shown by a marker as well as colour. */
export type CellLevel = "ok" | "warn" | "danger";

/** One instrument of the run status in the top bar: what it measures, and its reading. */
export interface RunCell {
  key: "phase" | "time" | "alert" | "drive" | "relay" | "scan" | "wave";
  label: string;
  value: string;
  level: CellLevel;
}

// The drive state stops the cruise drive; a hostile lock or the extractor stops the jump drive
// too, so the cell is labelled for both.
const DRIVE_CELL: Readonly<Record<Drive, { value: string; level: CellLevel }>> = {
  ready: { value: "Ready", level: "ok" },
  inSite: { value: "Cruise off in the site", level: "ok" },
  hunted: { value: "Jammed: hostile lock", level: "danger" },
  extracting: { value: "Off while extracting", level: "warn" },
};

/**
 * The run status, one cell for each thing a pilot reads at a glance: where they are, the clock,
 * the alert, the drives, the relays while held, a scan in progress and the next wave once a scan
 * has found it. Hull, hold and heat are on the ship gauges (game/gauges); what a scan found
 * about the pockets is on the system map (game/chart).
 *
 * The alert is how far the site has escalated (design §10): it never falls, and each step brings
 * a bigger wave. A playtester read "1" with its warning marker as a hostile still out there after
 * the last one was destroyed, so the cell says how many `hostiles` are in the run, and warns only
 * while there are some.
 */
export function runCells(
  own: OwnStatus,
  reveal: Pick<Scanned, "waves"> | null,
  hostiles = 0,
): RunCell[] {
  const cells: RunCell[] = [
    { key: "phase", label: "Phase", value: PHASE_TEXT[own.phase], level: "ok" },
    { key: "time", label: "Time", value: clock(own.elapsedSeconds), level: "ok" },
    {
      key: "alert",
      label: "Alert",
      value:
        String(own.alert) +
        (hostiles > 0
          ? ` · ${String(hostiles)} ${hostiles === 1 ? "hostile" : "hostiles"}`
          : own.alert > 0
            ? " · clear"
            : ""),
      level: hostiles === 0 ? "ok" : own.alert >= 3 ? "danger" : "warn",
    },
    { key: "drive", label: "Drives", ...DRIVE_CELL[own.drive] },
  ];
  if (own.relay > 0) {
    cells.push({
      key: "relay",
      label: "Relays",
      value: own.relay === RELAY_LINKED ? "Linked" : `Held ${String(own.relay)}/30 s`,
      level: "ok",
    });
  }
  if (own.scan !== null) {
    cells.push({
      key: "scan",
      label: "Scan",
      value: `${String(Math.round((own.scan / 254) * 100))}% · hold still`,
      level: "ok",
    });
  } else if (reveal) {
    // The next wave is the first the alert has not reached: a cut-out can bring one early.
    const next: WaveInfo | undefined = reveal.waves.find((wave) => wave.level > own.alert);
    // A countdown, not the run-clock time it lands: a playtester read "4:58" beside "Time 2:47"
    // as a counter that did not run. Both clocks count from launch.
    const left = next ? next.atSeconds - own.elapsedSeconds : 0;
    cells.push({
      key: "wave",
      label: "Next wave",
      value: next
        ? `${left > 0 ? `in ${clock(left)}` : "due"} · ${String(next.drones)} drones · ` +
          `${String(next.raiders)} raiders`
        : "None before the limit",
      level: next && left <= WAVE_WARNING_SECONDS ? "warn" : "ok",
    });
  }
  return cells;
}

/** What a finished scan found about the pockets, for the notice that announces it. */
export function scanSummary(
  reveal: Pick<Scanned, "richPocket" | "relays">,
  pocketName: (index: number) => string,
): string {
  return (
    `Scan complete · rich pocket: ${pocketName(reveal.richPocket)} · ` +
    `relays: ${reveal.relays.map(pocketName).join(" and ")} · marked on the map`
  );
}

/** The summary panel's lines for a finished run (design §6). */
export function summaryLines(summary: RunSummary): string[] {
  const heading = {
    docked: "Docked at home",
    destroyed: "Destroyed. Returned home; the hull is reissued",
    abandoned: "Run abandoned. The ship is returned home",
  }[summary.ending];
  const banked = [
    summary.ore ? `${String(summary.ore)} ore` : null,
    summary.salvage ? `${String(summary.salvage)} salvage` : null,
    summary.volatiles ? `${String(summary.volatiles)} volatiles` : null,
  ].filter((part) => part !== null);
  const net = summary.payout - summary.launchCost;
  return [
    heading,
    summary.ending === "docked"
      ? `Banked: ${banked.length ? banked.join(", ") : "nothing"}`
      : `Lost: ${String(summary.cargoLost)} cargo, ${String(summary.modulesLost)} upgraded modules`,
    summary.settled
      ? `Your crew-pot share: ${String(summary.payout)} cr · supplies −${String(summary.launchCost)} cr · net ${net < 0 ? "−" : ""}${String(Math.abs(net))} cr`
      : `Crew payout pending · supplies −${String(summary.launchCost)} cr · cargo is banked separately`,
    `Approach ${clock(summary.approachSeconds)} · in the site ${clock(summary.objectiveSeconds)} · ` +
      `return ${clock(summary.returnSeconds)}`,
  ];
}
