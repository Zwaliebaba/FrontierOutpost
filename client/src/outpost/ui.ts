/**
 * The home panel (design §2 and §7): the selected socket and its choices with their previews,
 * NPC trade at posted prices, the processor, and the hangar. It only builds DOM from
 * `options.ts`, which holds the rules; every action goes to the server, which decides.
 */
import type { AccountInfo, Catalog, ClientMessage, Good } from "../protocol";
import { fittingRows, holding, socketView, toggleFitting } from "./options";

export interface HomeHandlers {
  send(message: ClientMessage): void;
  select(socket: string): void;
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

/** Rebuilds the home panel's contents for `account`, with `selected` socket open. */
export function renderHome(
  panel: HTMLElement,
  catalog: Catalog,
  account: AccountInfo,
  selected: string,
  on: HomeHandlers,
): void {
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
        `${String(outpost.processorBatches)} batches per run · threat tiers up to ` +
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
  const queued = account.processing.reduce((sum, q) => sum + q.batches, 0);
  panel.append(
    el(
      "p",
      `${String(queued)} batches queued · finishes up to ${String(outpost.processorBatches)} ` +
        "when you next dock from a run",
      "facts",
    ),
  );
  for (const recipe of catalog.recipes) {
    const held = holding(account, recipe.input.good);
    const waiting = account.processing.find((q) => q.recipe === recipe.id)?.batches ?? 0;
    const most = Math.floor(held / recipe.input.amount);
    const row = el("div", undefined, "choice");
    row.dataset.recipe = recipe.id;
    row.append(
      el(
        "span",
        `${String(recipe.input.amount)} ${recipe.input.good} → ${String(recipe.output.amount)} ` +
          `${recipe.output.good} · ${String(waiting)} queued `,
      ),
      button(
        "Queue 1",
        `process:${recipe.id}`,
        () => {
          on.send({ type: "process", recipe: recipe.id, batches: 1 });
        },
        most < 1 ? `Needs ${String(recipe.input.amount)} ${recipe.input.good}` : undefined,
      ),
      button(
        `Queue ${String(Math.max(most, 1))}`,
        `process-all:${recipe.id}`,
        () => {
          on.send({ type: "process", recipe: recipe.id, batches: most });
        },
        most < 1 ? `Needs ${String(recipe.input.amount)} ${recipe.input.good}` : undefined,
      ),
      button(
        "Take back 1",
        `unqueue:${recipe.id}`,
        () => {
          on.send({ type: "unqueue", recipe: recipe.id, batches: 1 });
        },
        waiting < 1 ? "Nothing queued" : undefined,
      ),
    );
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
        row.saved ? "Unfit" : "Fit",
        `fit:${row.fitting.id}`,
        () => {
          if (toggle) on.send(toggle);
        },
        toggle ? undefined : "A hull carries two modules",
      ),
    );
    panel.append(item);
  }
}
