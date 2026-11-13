/**
 * The home panel (design §2 and §7): the selected socket and its choices with their previews,
 * NPC trade at posted prices, the processor, and the hangar. It only builds DOM from
 * `options.ts`, which holds the rules; every action goes to the server, which decides.
 */
import type { AccountInfo, Catalog, ClientMessage, Good } from "../protocol";
import {
  affordableFacilities,
  fittingRows,
  holding,
  socketFacts,
  socketView,
  toggleFitting,
} from "./options";
import { pace, processorView, progressLine } from "./processor";

export type HomeTab = "overview" | "facilities" | "market" | "processor" | "hangar";

export interface HomeHandlers {
  tab(tab: HomeTab): void;
  send(message: ClientMessage): void;
  select(socket: string, facility?: string): void;
  receipt?(): void;
}

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  text?: string,
  className?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (text !== undefined) node.textContent = text;
  if (className !== undefined) node.className = className;
  return node;
}

function button(
  text: string,
  action: string,
  onClick: () => void,
  blocked?: string,
): HTMLButtonElement {
  const node = el("button", text);
  node.dataset.action = action;
  if (blocked !== undefined) {
    node.disabled = true;
    node.title = blocked;
  }
  node.addEventListener("click", onClick);
  return node;
}

const GOODS: readonly Good[] = ["ore", "salvage", "volatiles", "alloy", "components"];

/**
 * Moves the processor's progress bar and countdown on, `since` seconds after `account` arrived,
 * without rebuilding the panel: called every second while the home is open.
 */
export function tickProcessor(panel: HTMLElement, account: AccountInfo, since: number): void {
  const status = panel.querySelector<HTMLElement>(".processor-status");
  if (!status) return;
  const view = processorView(account, since);
  const fill = status.querySelector<HTMLElement>(".processor-fill");
  if (fill) fill.style.width = `${(view.share * 100).toFixed(1)}%`;
  const left = status.querySelector<HTMLElement>(".processor-left");
  if (left) left.textContent = progressLine(view);
}

/**
 * Rebuilds the home panel's contents for `account`, with `selected` socket open. `since` is how
 * many seconds ago the account arrived, for the processor's progress.
 */
export function renderHome(
  panel: HTMLElement,
  catalog: Catalog,
  account: AccountInfo,
  selected: string,
  on: HomeHandlers,
  active: HomeTab = "overview",
  since = 0,
): void {
  const focused = document.activeElement;
  const focusAction =
    focused instanceof HTMLElement && panel.contains(focused) ? focused.dataset.action : undefined;
  const { outpost } = account;
  panel.replaceChildren();
  panel.dataset.selected = selected;
  panel.dataset.installed = outpost.sockets
    .map((s) => `${s.id}:${s.facility ?? "-"}:${String(s.run)}`)
    .join(" ");

  panel.append(el("h2", "Outpost"));
  panel.append(
    el(
      "p",
      `Power ${String(outpost.powerUsed)}/${String(catalog.powerCap)} · processor ` +
        `${pace(outpost.batchSeconds)} a batch · threat tiers up to ` +
        (["", "I", "II", "III"][outpost.openTiers] ?? String(outpost.openTiers)),
      "facts",
    ),
  );
  panel.append(
    el(
      "p",
      outpost.opens.length > 0
        ? `Opens on the expedition: ${outpost.opens.join(", ")}`
        : "Opens nothing yet: build a facility",
      "facts",
    ),
  );

  const sockets = el("div", undefined, "row");
  for (const socket of outpost.sockets) {
    const view = socketView(catalog, account, socket.id);
    const name = catalog.facilities.find((f) => f.id === socket.facility)?.name;
    const label =
      catalog.sockets.find((s) => s.id === socket.id)?.name.replace("Socket ", "") ?? socket.id;
    const tab = button(
      `${label}: ${name ?? (view.state === "closed" ? "closed" : "empty")}`,
      `socket:${socket.id}`,
      () => {
        on.select(socket.id);
      },
    );
    tab.dataset.state = view.state;
    if (socket.id === selected) tab.dataset.selected = "true";
    sockets.append(tab);
  }
  panel.append(sockets);

  const view = socketView(catalog, account, selected);
  const section = el("section");
  section.dataset.socket = selected;
  section.dataset.state = view.state;
  section.append(el("h3", view.title));
  for (const line of view.summary) section.append(el("p", line, "facts"));
  for (const choice of view.choices) {
    const item = el("div", undefined, "choice");
    if (choice.message.type === "install") {
      item.dataset.facility = choice.message.facility;
      item.tabIndex = -1;
    }
    const action =
      choice.message.type === "install"
        ? `install:${choice.message.facility}`
        : choice.message.type === "power"
          ? `power:${String(choice.message.run)}`
          : choice.message.type;
    item.append(
      button(
        choice.label,
        action,
        () => {
          on.send(choice.message);
        },
        choice.blocked,
      ),
    );
    const details = el("ul");
    for (const line of [...choice.details, ...(choice.blocked ? [choice.blocked] : [])]) {
      details.append(el("li", line));
    }
    item.append(details);
    section.append(item);
  }
  panel.append(section);

  panel.append(el("h3", "Market · posted prices"));
  const market = el("table");
  for (const good of GOODS) {
    const price = catalog.prices.find((p) => p.good === good);
    if (!price) continue;
    const held = holding(account, good);
    const row = el("tr");
    row.dataset.good = good;
    row.append(
      el("td", `${String(held)} ${good}`),
      el("td", `sells ${String(price.buysAt)} · buys ${String(price.sellsAt)}`),
    );
    const actions = el("td");
    actions.append(
      button(
        "Sell 1",
        `sell:${good}`,
        () => {
          on.send({ type: "sell", good, amount: 1 });
        },
        held < 1 ? `No ${good} to sell` : undefined,
      ),
      button(
        "Sell all",
        `sell-all:${good}`,
        () => {
          on.send({ type: "sell", good, amount: held });
        },
        held < 1 ? `No ${good} to sell` : undefined,
      ),
      button(
        "Buy 1",
        `buy:${good}`,
        () => {
          on.send({ type: "buy", good, amount: 1 });
        },
        account.credits < price.sellsAt ? `Needs ${String(price.sellsAt)} cr` : undefined,
      ),
    );
    row.append(actions);
    market.append(row);
  }
  panel.append(market);

  panel.append(el("h3", "Processor"));
  // What it is doing now: the batch in hand with its progress, or that it is idle.
  const making = processorView(account, since);
  const status = el("div", undefined, "processor-status");
  status.dataset.state = making.recipe === undefined ? "idle" : "working";
  if (making.recipe === undefined) {
    status.append(el("strong", "Idle"), el("p", "Load a batch and it starts at once.", "facts"));
  } else {
    const output =
      catalog.recipes.find((r) => r.id === making.recipe)?.output.good ?? making.recipe;
    status.dataset.recipe = making.recipe;
    status.append(
      el(
        "strong",
        `Making ${output} · ${String(making.batches)} ` +
          `${making.batches === 1 ? "batch" : "batches"} left`,
      ),
      el("div", undefined, "processor-track"),
      el("p", progressLine(making), "processor-left"),
    );
    const fill = el("div", undefined, "processor-fill");
    fill.style.width = `${(making.share * 100).toFixed(1)}%`;
    status.querySelector(".processor-track")?.append(fill);
  }
  panel.append(status);
  panel.append(
    el(
      "p",
      `One batch at a time, ${pace(outpost.batchSeconds)} each. ` +
        "It keeps working while you fly and while you are away.",
      "facts",
    ),
  );
  for (const recipe of catalog.recipes) {
    const held = holding(account, recipe.input.good);
    const waiting = account.processing.find((q) => q.recipe === recipe.id)?.batches ?? 0;
    const most = Math.floor(held / recipe.input.amount);
    const needs = `Needs ${String(recipe.input.amount)} ${recipe.input.good}`;
    const row = el("div", undefined, "choice recipe");
    row.dataset.recipe = recipe.id;
    row.dataset.loaded = String(waiting);
    const title = el("p", undefined, "recipe-title");
    title.append(
      el(
        "span",
        `${String(recipe.input.amount)} ${recipe.input.good} → ` +
          `${String(recipe.output.amount)} ${recipe.output.good}`,
      ),
      el("span", waiting > 0 ? `${String(waiting)} loaded` : "none loaded", "facts"),
    );
    const actions = el("div", undefined, "recipe-actions");
    actions.append(
      button(
        "Load 1",
        `process:${recipe.id}`,
        () => {
          on.send({ type: "process", recipe: recipe.id, batches: 1 });
        },
        most < 1 ? needs : undefined,
      ),
      button(
        most > 1 ? `Load all ${String(most)}` : "Load all",
        `process-all:${recipe.id}`,
        () => {
          on.send({ type: "process", recipe: recipe.id, batches: most });
        },
        // With one batch's worth, "Load 1" is the same thing.
        most < 2 ? (most < 1 ? needs : "Only one batch's worth: use Load 1") : undefined,
      ),
      button(
        "Take back 1",
        `unqueue:${recipe.id}`,
        () => {
          on.send({ type: "unqueue", recipe: recipe.id, batches: 1 });
        },
        waiting < 1 ? "Nothing loaded" : undefined,
      ),
    );
    row.append(title, actions);
    panel.append(row);
  }

  panel.append(el("h3", "Hangar · one rig, up to two modules"));
  for (const row of fittingRows(catalog, account)) {
    const item = el("div", undefined, "choice");
    item.dataset.fitting = row.fitting.id;
    item.dataset.owned = String(row.owned);
    item.dataset.saved = String(row.saved);
    const toggle = toggleFitting(catalog, account, row.fitting.id);
    item.append(
      el(
        "span",
        `${row.fitting.name} (${row.fitting.slot}) · ${row.fitting.effect || "no change"} · ` +
          `${String(row.owned)} owned `,
      ),
      button(
        `Buy ${String(row.price)} cr`,
        `buy-fitting:${row.fitting.id}`,
        () => {
          on.send({ type: "buyFitting", fitting: row.fitting.id });
        },
        row.blocked,
      ),
      button(
        row.saved ? "Remove from plan" : "Save to fitting",
        `fit:${row.fitting.id}`,
        () => {
          if (toggle) on.send(toggle);
        },
        toggle ? undefined : "A hull carries two modules",
      ),
    );
    const flown = account.launch.flown.includes(row.fitting.id);
    item.append(
      el(
        "p",
        row.saved ? (flown ? "✓ Will fly" : "Saved · stays home") : "Not in the saved fitting",
        "fitting-state",
      ),
    );
    if (row.blocked) item.append(el("p", row.blocked, "blocked-reason"));
    else if (row.saved && !flown)
      item.append(el("p", "Buy a replacement to fly this saved fitting", "blocked-reason"));
    panel.append(item);
  }

  const contents = [...panel.children];
  const panes = new Map<HomeTab, HTMLElement>();
  for (const name of ["overview", "facilities", "market", "processor", "hangar"] as const) {
    const pane = el("div", undefined, "service-pane");
    pane.id = `service-${name}`;
    pane.hidden = name !== active;
    panes.set(name, pane);
  }
  let group: HomeTab = "facilities";
  for (const node of contents) {
    if (node.tagName === "H3") {
      if (node.textContent.startsWith("Market")) group = "market";
      else if (node.textContent === "Processor") group = "processor";
      else if (node.textContent.startsWith("Hangar")) group = "hangar";
    }
    panes.get(group)?.append(node);
  }
  const overview = panes.get("overview");
  overview?.append(el("p", "HOME / OPERATIONS", "eyebrow"), el("h2", "Station overview"));
  overview?.append(el("p", "Four berths. One expedition.", "facts"));
  if (on.receipt)
    overview?.append(
      button("Expedition receipts", "view-receipts", () => {
        on.receipt?.();
      }),
    );
  const power = el("div", undefined, "readout");
  power.append(
    el("span", "POWER ALLOCATION"),
    el("strong", `${String(outpost.powerUsed)} / ${String(catalog.powerCap)}`),
  );
  overview?.append(power);
  const meter = el("meter");
  meter.min = 0;
  meter.max = catalog.powerCap;
  meter.value = outpost.powerUsed;
  meter.setAttribute("aria-label", "Station power allocation");
  overview?.append(meter, el("h3", "Construction sockets"));
  for (const socket of outpost.sockets) {
    const name = catalog.facilities.find((f) => f.id === socket.facility)?.name;
    const open = socketView(catalog, account, socket.id);
    const select = button(
      `SOCKET ${socket.id.toUpperCase()} / ${name ?? open.state}`,
      `overview:${socket.id}`,
      () => {
        on.select(socket.id);
      },
    );
    select.className = "socket-card";
    select.dataset.state = open.state;
    for (const fact of socketFacts(catalog, account, socket.id)) select.append(el("small", fact));
    overview?.append(select);
  }
  overview?.append(
    el(
      "p",
      "Select a socket to inspect its capabilities and configure the next expedition.",
      "facts",
    ),
  );
  const ready = affordableFacilities(catalog, account);
  if (ready.length) {
    overview?.append(el("h3", "Ready to develop"));
    for (const choice of ready) {
      const inspect = button(`Inspect ${choice.name}`, `develop:${choice.name}`, () => {
        on.select(choice.socket, choice.facility);
      });
      inspect.className = "development-card";
      inspect.append(el("small", `Enables: ${choice.opens.join(", ")}`));
      overview?.append(inspect);
    }
  }
  const navigation = el("nav", undefined, "service-nav");
  navigation.setAttribute("aria-label", "Station services");
  ["overview", "facilities", "market", "processor", "hangar"].forEach((name) => {
    const tab = name as HomeTab;
    const control = button(name, `view:${name}`, () => {
      on.tab(tab);
    });
    control.setAttribute("aria-current", name === active ? "page" : "false");
    control.setAttribute("aria-controls", `service-${name}`);
    navigation.append(control);
  });
  panel.replaceChildren(navigation, ...panes.values());
  panel.dataset.view = active;
  if (focusAction) {
    const replacement = [...panel.querySelectorAll<HTMLButtonElement>("button[data-action]")].find(
      (control) => control.dataset.action === focusAction,
    );
    if (replacement && !replacement.disabled && !replacement.closest("[hidden]")) {
      replacement.focus({ preventScroll: true });
    }
  }
}
