/**
 * The contract and the run as the pilot reads them: launching, and the words the briefing, the
 * run line and the summary show. Kept free of the DOM so it can be tested.
 */
import type {
  DominantThreat,
  Drive,
  ExtractRefusal,
  ExtractorMode,
  Hazard,
  OwnStatus,
  PocketKind,
  RunPhase,
  RunSummary,
  ServerMessage,
  WaveInfo,
} from "../protocol";
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

/** Minutes and seconds, such as 3:07. */
export function clock(seconds: number): string {
  const whole = Math.max(0, Math.floor(seconds));
  return `${String(Math.floor(whole / 60))}:${String(whole % 60).padStart(2, "0")}`;
}

/** A ten-cell bar for a share of 0 to 1, readable without colour. */
export function bar(share: number): string {
  const cells = Math.round(Math.min(1, Math.max(0, share)) * 10);
  return `[${"#".repeat(cells)}${".".repeat(10 - cells)}]`;
}

function extractorText(mode: ExtractorMode, seconds: number): string {
  switch (mode) {
    case "off":
      return "off";
    case "on":
      return "working";
    case "venting":
      return `venting ${String(seconds)} s`;
    case "cutOut":
      return `CUT OUT ${String(seconds)} s`;
  }
}

const DRIVE_TEXT: Readonly<Record<Drive, string>> = {
  ready: "drive ready (Shift)",
  inSite: "drive off in the site",
  hunted: "drive jammed: locked by a hostile",
  extracting: "drive off while extracting",
};

/** The run line: where the pilot is, the clock, the alert, the hold, the extractor, the drive. */
export function runLine(own: OwnStatus): string {
  const heat = own.heat / 255;
  return [
    PHASE_TEXT[own.phase],
    clock(own.elapsedSeconds),
    `alert ${String(own.alert)}`,
    `hold ${String(own.cargo)}/${String(own.capacity)}`,
    ...(own.shield > 0 ? [`shield ${String(own.shield)}`] : []),
    `heat ${bar(heat)} ${String(Math.round(heat * 100))}%`,
    `extractor ${extractorText(own.extractor, own.extractorSeconds)}`,
    DRIVE_TEXT[own.drive],
  ].join(" · ");
}

/** The scan line: progress while scanning, what a scan revealed after. */
export function scanLine(
  own: OwnStatus,
  reveal: Scanned | null,
  pocketName: (index: number) => string,
): string {
  if (own.scan !== null) return `Scanning ${bar(own.scan / 254)} · hold still`;
  if (!reveal) return "Site not scanned · C to scan, holding still inside the site";
  const next: WaveInfo | undefined = reveal.waves[0];
  const wave = next
    ? `next wave ${clock(next.atSeconds)}: ${String(next.drones)} drones, ` +
      `${String(next.raiders)} raiders`
    : "no more waves before the limit";
  return (
    `Scanned · rich: ${pocketName(reveal.richPocket)} · ` +
    `relays: ${reveal.relays.map(pocketName).join(" and ")} · ${wave}`
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
    `Contract: ${String(summary.payout)} cr · supplies −${String(summary.launchCost)} cr · ` +
      `net ${net < 0 ? "−" : ""}${String(Math.abs(net))} cr`,
    `Approach ${clock(summary.approachSeconds)} · in the site ${clock(summary.objectiveSeconds)} · ` +
      `return ${clock(summary.returnSeconds)}`,
  ];
}
