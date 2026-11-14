import type { AccountInfo, Catalog, CrewInfo, WingmanInfo } from "../protocol";

export function wingmanChoices(account: AccountInfo, catalog: Catalog, crew: CrewInfo | null) {
  const tier = account.outpost.sockets.find((s) => s.facility === "crew_quarters")?.run ?? 0;
  const canHire = tier > 0 && (crew?.members.length ?? 1) < 4;
  const rigs =
    tier < 2
      ? []
      : catalog.fittings.filter((fitting) => {
          if (fitting.slot !== "rig" || !account.outpost.opens.includes(fitting.needs))
            return false;
          const owned = account.hangar.filter((id) => id === fitting.id).length;
          const flown = account.launch.flown.filter((id) => id === fitting.id).length;
          return owned > flown;
        });
  return { tier, canHire, rigs };
}

export function wingmanText(info: WingmanInfo): string {
  return `Wingman · ${info.order.type} · ${info.state} · cargo ${String(info.cargo)}/${String(info.capacity)}`;
}

export function wingmanFinished(info: WingmanInfo): boolean {
  return ["docked", "destroyed", "abandoned"].includes(info.state);
}
