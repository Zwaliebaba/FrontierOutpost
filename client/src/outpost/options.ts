/**
 * What the outpost view offers and what each choice would do (design §2: previews show the cost,
 * the power demand and the capability gained, naming what it opens). It mirrors the server's
 * rules in `frontier_sim::outpost` from the catalog, for previews only: the server decides.
 */
import type {
  AccountInfo,
  Catalog,
  ClientMessage,
  CostInfo,
  FacilityInfo,
  FittingInfo,
  Good,
} from "../protocol";

/** Something the pilot can ask for, with what it would do. */
export interface Choice {
  label: string;
  /** The preview: cost, power, what it opens or closes. */
  details: string[];
  message: ClientMessage;
  /** Why it cannot be done now; absent when it can. */
  blocked?: string;
}

export type SocketState = "closed" | "empty" | "powered" | "unpowered";

export interface SocketView {
  id: string;
  title: string;
  state: SocketState;
  /** What the socket holds and does now. */
  summary: string[];
  choices: Choice[];
}

interface Placed {
  socket: string;
  facility: string;
  run: number;
}

export function holding(account: AccountInfo, good: Good): number {
  return account[good];
}

export function costText(cost: CostInfo): string {
  const parts = cost.goods.map(({ good, amount }) => `${String(amount)} ${good}`);
  if (cost.credits > 0 || parts.length === 0) parts.unshift(`${String(cost.credits)} cr`);
  return parts.join(" + ");
}

/** What `account` lacks to pay `cost`, or null if it can. */
export function shortfall(account: AccountInfo, cost: CostInfo): string | null {
  const missing: string[] = [];
  if (account.credits < cost.credits) missing.push(`${String(cost.credits - account.credits)} cr`);
  for (const { good, amount } of cost.goods) {
    const held = holding(account, good);
    if (held < amount) missing.push(`${String(amount - held)} ${good}`);
  }
  return missing.length > 0 ? `Needs ${missing.join(" and ")} more` : null;
}

function facility(catalog: Catalog, id: string): FacilityInfo | undefined {
  return catalog.facilities.find((f) => f.id === id);
}

function owned(account: AccountInfo, id: string): number {
  return account.outpost.unlocks.find((unlock) => unlock.facility === id)?.tier ?? 0;
}

function draw(catalog: Catalog, id: string, run: number): number {
  return run > 0 ? (facility(catalog, id)?.tiers[run - 1]?.draw ?? 0) : 0;
}

function layoutOf(account: AccountInfo): Placed[] {
  return account.outpost.sockets.flatMap((socket) =>
    socket.facility === undefined
      ? []
      : [{ socket: socket.id, facility: socket.facility, run: socket.run }],
  );
}

function powerUsed(catalog: Catalog, layout: readonly Placed[]): number {
  return layout.reduce((sum, placed) => sum + draw(catalog, placed.facility, placed.run), 0);
}

/** The highest tier up to `upTo` that fits in `free` power, or 0. */
function bestRun(catalog: Catalog, id: string, upTo: number, free: number): number {
  for (let run = upTo; run >= 1; run -= 1) if (draw(catalog, id, run) <= free) return run;
  return 0;
}

/** Everything a layout opens, by name. */
function opens(catalog: Catalog, layout: readonly Placed[]): Set<string> {
  const names = new Set<string>();
  for (const placed of layout) {
    const tiers = facility(catalog, placed.facility)?.tiers ?? [];
    for (const tier of tiers.slice(0, placed.run)) for (const name of tier.opens) names.add(name);
  }
  return names;
}

/** Processor batches per run for a layout, as the server counts them. */
function processorBatches(catalog: Catalog, layout: readonly Placed[]): number {
  let best: number | null = null;
  let extra = 0;
  for (const placed of layout) {
    const tiers = facility(catalog, placed.facility)?.tiers ?? [];
    for (const tier of tiers.slice(0, placed.run)) {
      if (tier.processorBatches !== undefined) best = Math.max(best ?? 0, tier.processorBatches);
    }
    if (placed.run === 0) continue;
    for (const rule of catalog.adjacency) {
      const more = /processes (\d+) more batches/.exec(rule.bonus);
      if (more?.[1] && rule.facility === placed.facility && rule.socket === placed.socket) {
        extra += Number(more[1]);
      }
    }
  }
  return (best ?? catalog.processorBatches) + extra;
}

/** The lines that say what changing `before` into `after` does. */
function effects(catalog: Catalog, before: readonly Placed[], after: readonly Placed[]): string[] {
  const lines: string[] = [];
  const used = powerUsed(catalog, after);
  lines.push(`Power ${String(used)}/${String(catalog.powerCap)} after`);
  const [was, will] = [opens(catalog, before), opens(catalog, after)];
  const gained = [...will].filter((name) => !was.has(name));
  const lost = [...was].filter((name) => !will.has(name));
  if (gained.length > 0) lines.push(`Opens: ${gained.join(", ")}`);
  if (lost.length > 0) lines.push(`Closes: ${lost.join(", ")}`);
  const [batches, next] = [processorBatches(catalog, before), processorBatches(catalog, after)];
  if (batches !== next) {
    lines.push(`Processor: ${String(batches)} → ${String(next)} batches per run`);
  }
  return lines;
}

function tierName(run: number): string {
  return run === 0 ? "unpowered" : `tier ${String(run)}`;
}

/** What a socket holds and what the pilot can do with it. */
export function socketView(catalog: Catalog, account: AccountInfo, socketId: string): SocketView {
  const rule = catalog.sockets.find((s) => s.id === socketId);
  const state = account.outpost.sockets.find((s) => s.id === socketId);
  const title = rule ? `${rule.name}, beside the ${rule.beside}` : socketId;
  const before = layoutOf(account);
  const others = before.filter((placed) => placed.socket !== socketId);
  const free = catalog.powerCap - powerUsed(catalog, others);
  if (!state?.open) {
    return {
      id: socketId,
      title,
      state: "closed",
      summary: ["Opens when the first facility is installed."],
      choices: [],
    };
  }
  const installedId = state.facility;
  if (installedId === undefined) {
    const choices = catalog.facilities
      .filter((f) => !before.some((placed) => placed.facility === f.id))
      .map((f): Choice => {
        const tier = owned(account, f.id);
        const cost = tier > 0 ? catalog.reinstall : f.tiers[0]?.cost;
        const run = bestRun(catalog, f.id, Math.max(tier, 1), free);
        const after = [...others, { socket: socketId, facility: f.id, run }];
        const details = [
          `Cost ${cost ? costText(cost) : "?"}${tier > 0 ? " (installing again)" : ""}`,
          ...(run === 0
            ? ["Not enough power: it would be installed unpowered"]
            : [`Runs at ${tierName(run)}, drawing ${String(draw(catalog, f.id, run))}`]),
          ...effects(catalog, before, after),
          ...catalog.adjacency
            .filter((a) => a.facility === f.id && a.socket === socketId)
            .map((a) => `Here it ${a.bonus}`),
          ...(tier === 0 && f.rig !== undefined ? [`Comes with a ${rigName(catalog, f.rig)}`] : []),
        ];
        const lacking = cost ? shortfall(account, cost) : "Unknown cost";
        return {
          label: tier > 0 ? `Install the ${f.name} again` : `Build a ${f.name}`,
          details,
          message: { type: "install", socket: socketId, facility: f.id },
          ...(lacking ? { blocked: lacking } : {}),
        };
      });
    return {
      id: socketId,
      title,
      state: "empty",
      summary: ["Empty. Any facility fits."],
      choices,
    };
  }

  const f = facility(catalog, installedId);
  const name = f?.name ?? installedId;
  const tier = owned(account, installedId);
  const run = state.run;
  const choices: Choice[] = [];
  const next = f?.tiers[tier];
  if (next) {
    const upgraded = bestRun(catalog, installedId, tier + 1, free);
    const after = [...others, { socket: socketId, facility: installedId, run: upgraded }];
    const lacking = shortfall(account, next.cost);
    choices.push({
      label: `Upgrade to tier ${String(tier + 1)}`,
      details: [
        `Cost ${costText(next.cost)}`,
        upgraded === tier + 1
          ? `Draws ${String(next.draw)}`
          : `Not enough power to run tier ${String(tier + 1)}: it runs at ${tierName(upgraded)}`,
        ...effects(catalog, before, after),
        ...(tier + 1 >= 2 ? ["Counts toward opening the next threat tier"] : []),
      ],
      message: { type: "upgrade", socket: socketId },
      ...(lacking ? { blocked: lacking } : {}),
    });
  }
  for (let level = tier; level >= 0; level -= 1) {
    if (level === run) continue;
    const after = [...others, { socket: socketId, facility: installedId, run: level }];
    const needed = draw(catalog, installedId, level);
    choices.push({
      label: level === 0 ? "Unpower" : `Run at tier ${String(level)}`,
      details: effects(catalog, before, after),
      message: { type: "power", socket: socketId, run: level },
      ...(needed > free
        ? { blocked: `Draws ${String(needed)}; ${String(free)} power is free` }
        : {}),
    });
  }
  const without = others;
  choices.push({
    label: `Remove the ${name}`,
    details: [
      `Free; installing it again costs ${costText(catalog.reinstall)}`,
      ...effects(catalog, before, without),
    ],
    message: { type: "remove", socket: socketId },
  });
  const opened = (f?.tiers ?? []).slice(0, run).flatMap((t) => t.opens);
  return {
    id: socketId,
    title,
    state: run > 0 ? "powered" : "unpowered",
    summary: [
      `${name}, owned at tier ${String(tier)}, running ${tierName(run)}` +
        (run > 0 ? `, drawing ${String(draw(catalog, installedId, run))}` : ""),
      run > 0 ? `Opens: ${opened.join(", ")}` : "Unpowered: it opens nothing",
    ],
    choices,
  };
}

function rigName(catalog: Catalog, id: string): string {
  return catalog.fittings.find((f) => f.id === id)?.name.toLowerCase() ?? id;
}

export function fittingName(catalog: Catalog, id: string): string {
  return catalog.fittings.find((f) => f.id === id)?.name ?? id;
}

/** What the pilot's supplies come to if they launch now, as the server priced it (design §10). */
export function suppliesLine(account: AccountInfo): string {
  const { due, supplies } = account.launch;
  switch (due.type) {
    case "carried":
      return "Supplies: carried over from your last run, no charge";
    case "short":
      return (
        `Supplies: ${String(supplies)} cr · you have ${String(account.credits)} cr · ` +
        `sell ${String(due.missing)} cr of goods to launch`
      );
    case "pays":
      return due.credits < supplies
        ? `Supplies: ${String(due.credits)} cr, all you have (posted at ${String(supplies)} cr)`
        : `Supplies: ${String(supplies)} cr`;
  }
}

/** A sale that would cover the supplies a pilot is short of. */
export interface SupplySale {
  good: Good;
  amount: number;
  /** What the sale pays. */
  credits: number;
}

/**
 * The sales that would let a pilot who is short of supplies launch: for each good held, the
 * fewest units that cover what is missing, or all of it when even that falls short. Raw
 * materials come first: they are worth least. Empty when nothing need be sold.
 */
export function supplySales(catalog: Catalog, account: AccountInfo): SupplySale[] {
  const { due } = account.launch;
  if (due.type !== "short") return [];
  const sales: SupplySale[] = [];
  for (const price of catalog.prices) {
    const held = holding(account, price.good);
    if (held < 1 || price.buysAt < 1) continue;
    const amount = Math.min(held, Math.ceil(due.missing / price.buysAt));
    sales.push({ good: price.good, amount, credits: amount * price.buysAt });
  }
  // One sale that covers it is all the pilot needs to see.
  const enough = sales.filter((sale) => sale.credits >= due.missing);
  return enough.length > 0 ? enough.slice(0, 2) : sales;
}

/** The launch stake and what flies (design §6: the stake at NPC replacement price). */
export function launchLines(catalog: Catalog, account: AccountInfo): string[] {
  const { launch } = account;
  const flies =
    launch.flown.length > 0
      ? `Flies: ${launch.flown.map((id) => fittingName(catalog, id)).join(", ")}`
      : "Flies the basic fitting";
  return [
    `${flies} · hold ${String(launch.capacity)}`,
    `Stake: ${String(launch.stake)} cr of rigs and modules at replacement price`,
    suppliesLine(account),
    ...launch.missing.map((m) => `${fittingName(catalog, m.fitting)} stays home: ${m.reason}`),
  ];
}

export interface FittingRow {
  fitting: FittingInfo;
  owned: number;
  /** Price after the adjacency discount. */
  price: number;
  /** Why it cannot be bought now; absent when it can. */
  blocked?: string;
  saved: boolean;
}

/** Each rig and module: owned, price, whether it can be bought and is in the saved fitting. */
export function fittingRows(catalog: Catalog, account: AccountInfo): FittingRow[] {
  const discount = account.outpost.fittingDiscount;
  return catalog.fittings.map((fitting) => {
    const price = Math.ceil((fitting.price * (100 - discount)) / 100);
    const granted = account.outpost.opens.includes(fitting.needs);
    const blocked = !granted
      ? `Needs the ${fitting.needs}`
      : account.credits < price
        ? `Needs ${String(price - account.credits)} cr more`
        : undefined;
    return {
      fitting,
      owned: account.hangar.filter((id) => id === fitting.id).length,
      price,
      saved: account.rig === fitting.id || account.modules.includes(fitting.id),
      ...(blocked ? { blocked } : {}),
    };
  });
}

/** The fitting with `id` switched in or out: one rig, up to two different modules. */
export function toggleFitting(
  catalog: Catalog,
  account: AccountInfo,
  id: string,
): Extract<ClientMessage, { type: "saveFitting" }> | null {
  const fitting = catalog.fittings.find((f) => f.id === id);
  if (!fitting) return null;
  const rig = account.rig ?? null;
  if (fitting.slot === "rig") {
    return { type: "saveFitting", rig: rig === id ? null : id, modules: [...account.modules] };
  }
  const modules = account.modules.includes(id)
    ? account.modules.filter((m) => m !== id)
    : [...account.modules, id];
  if (modules.length > 2) return null;
  return { type: "saveFitting", rig, modules };
}
