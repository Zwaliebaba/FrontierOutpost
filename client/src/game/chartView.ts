/**
 * The system map as the pilot sees it: a minimap that is always up in flight, and the full map it
 * opens, where a destination is chosen and the jump drive engaged. It only draws what `chart.ts`
 * works out and reports clicks; the jump itself is asked of the server, which decides.
 *
 * The ship keeps flying under the open map, so the map never takes the flight keys: Space does
 * not press its buttons, and nothing in it holds the focus once it closes.
 */
import type { DepositInfo, EntityKind, ObstacleInfo, SiteInfo } from "../protocol";
import { Written } from "../view/written";
import { chartBearing, fitChart, keepInFrame, plot, scaleBar } from "./chart";
import type { ChartFrame, ChartPlace, ContactSide, JumpReadiness, PlaceKind } from "./chart";
import type { JumpReadout } from "./jump";
import { distanceText } from "./navigation";

export interface ChartContact {
  id: number;
  side: ContactSide;
  kind: EntityKind;
  x: number;
  z: number;
  heading: number;
}

/** Everything the map shows that changes during a run. */
export interface ChartState {
  places: readonly ChartPlace[];
  /** The destination chosen for the jump drive. */
  selected: string | null;
  readout: JumpReadout;
  readiness: JumpReadiness;
  own: { x: number; z: number; heading: number } | null;
  contacts: readonly ChartContact[];
  /** Units left in each of the site's deposits. */
  deposits: readonly number[];
}

export interface ChartHandlers {
  /** A destination was clicked or tapped. */
  choose(id: string): void;
  /** The jump button was pressed: engage, or cancel a spool. */
  engage(): void;
  /** A destination was double-clicked: choose it and engage at once. */
  jumpTo(id: string): void;
}

const SVG_NS = "http://www.w3.org/2000/svg";

function svg<K extends keyof SVGElementTagNameMap>(
  tag: K,
  className?: string,
  attributes: Readonly<Record<string, string | number>> = {},
): SVGElementTagNameMap[K] {
  const node = document.createElementNS(SVG_NS, tag);
  if (className !== undefined) node.setAttribute("class", className);
  for (const [name, value] of Object.entries(attributes)) node.setAttribute(name, String(value));
  return node;
}

function html<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className?: string,
  text?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (className !== undefined) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

/** One glyph for each kind of place, drawn about the origin in a 20-unit box. */
const GLYPH: Readonly<Record<PlaceKind, string>> = {
  dock: "M-6.5 -6.5H6.5V6.5H-6.5Z M-2.5 6.5V1H2.5V6.5",
  oreField: "M0 -8L6.5 0L0 8L-6.5 0Z M-2.5 0L0 -3L2.5 0",
  derelict: "M-8 4L-4 -5.5H2.5L0.5 -0.5L4 -0.5L8 -3.5L5.5 5.5H-1L0.5 1.5L-3 1.5Z",
  volatileVeins: "M0 -8C3.5 -3 6 0.2 6 3A6 6 0 0 1 -6 3C-6 0.2 -3.5 -3 0 -8Z",
  nestCore: "M0 -8L7 -4V4L0 8L-7 4V-4Z M0 -2.5L2.2 -1.2V1.2L0 2.5L-2.2 1.2V-1.2Z",
};
const PADLOCK = "M-3.5 -1H3.5V5H-3.5Z M-2 -1V-3A2 2 0 0 1 2 -3V-1";
const SHIP = "M0 -9L6 7L0 3.5L-6 7Z";
const DIAMOND = "M0 -5L5 0L0 5L-5 0Z";
const HEXAGON = "M0 -7L6 -3.5V3.5L0 7L-6 3.5V-3.5Z";

/** The glyph the map draws for a kind of place, as an icon for a list beside it. */
export function glyph(kind: PlaceKind, size: number): SVGSVGElement {
  const icon = svg("svg", "glyph", { viewBox: "-10 -10 20 20", width: size, height: size });
  icon.setAttribute("aria-hidden", "true");
  icon.append(svg("path", undefined, { d: GLYPH[kind] }));
  return icon;
}

function key(text: string): HTMLElement {
  return html("kbd", undefined, text);
}

interface PlaceNodes {
  group: SVGGElement;
  sub: SVGTextElement | null;
  at: { x: number; y: number };
}

/**
 * How much a drawing of the chart shows: the minimap's dots, the full map with its labels and
 * detail, or a still preview of a site not yet flown, with a glyph for each place.
 */
type PlotMode = "mini" | "full" | "preview";

/** One drawing of the chart. */
class Plot {
  readonly root: SVGSVGElement;
  readonly #width: number;
  readonly #height: number;
  readonly #padding: number;
  readonly #mode: PlotMode;
  readonly #detailed: boolean;
  readonly #choose: ((id: string) => void) | null;
  readonly #jumpTo: ((id: string) => void) | null;
  readonly #written = new Written();
  readonly #ground = svg("g");
  readonly #course = svg("line", "chart-course");
  readonly #placeLayer = svg("g");
  readonly #contactLayer = svg("g");
  readonly #ship = svg("g", "chart-ship");
  readonly #places = new Map<string, PlaceNodes>();
  readonly #contacts = new Map<number, SVGGElement>();
  #deposits: SVGCircleElement[] = [];
  #frame: ChartFrame | null = null;

  constructor(
    width: number,
    height: number,
    padding: number,
    mode: PlotMode,
    handlers?: Pick<ChartHandlers, "choose" | "jumpTo">,
  ) {
    const detailed = mode === "full";
    this.#mode = mode;
    this.#width = width;
    this.#height = height;
    this.#padding = padding;
    this.#detailed = detailed;
    this.#choose = handlers?.choose ?? null;
    this.#jumpTo = handlers?.jumpTo ?? null;
    this.root = svg("svg", `chart-svg chart-svg-${mode}`, {
      viewBox: `0 0 ${String(width)} ${String(height)}`,
      preserveAspectRatio: "xMidYMid meet",
    });
    // The list beside the map says the same in words, for a reader the drawing cannot serve.
    this.root.setAttribute("aria-hidden", "true");
    if (detailed) this.#ship.append(svg("circle", "ship-pulse", { r: 9 }));
    this.#ship.append(svg("path", "ship-hull", { d: SHIP }));
    // The ship and the course show from the first update; a preview never has one.
    this.#ship.setAttribute("visibility", "hidden");
    this.#course.setAttribute("visibility", "hidden");
    this.root.append(this.#ground, this.#course, this.#placeLayer, this.#contactLayer, this.#ship);
  }

  /** The length the scale bar shows, for the full map's legend. */
  get bar(): { metres: number; pixels: number } | null {
    return this.#frame ? scaleBar(this.#frame, 110) : null;
  }

  /** Draws what a run's site never changes: its bounds, rocks, deposits and places. */
  setSite(
    site: SiteInfo,
    obstacles: readonly ObstacleInfo[],
    deposits: readonly DepositInfo[],
    places: readonly ChartPlace[],
  ): void {
    // A preview is small: it frames the site, and holds the dock at its edge to show the way.
    const preview = this.#mode === "preview";
    const frame = fitChart(site, this.#width, this.#height, this.#padding, !preview);
    this.#frame = frame;
    this.#written.forget("");
    this.#ground.replaceChildren();
    this.#placeLayer.replaceChildren();
    this.#contactLayer.replaceChildren();
    this.#places.clear();
    this.#contacts.clear();

    const centre = plot(frame, site.centreX, site.centreZ);
    const dock = this.#at(frame, site.dockX, site.dockZ);
    if (this.#detailed) {
      // A metric grid: one square is the scale bar's length.
      const step = scaleBar(frame, 110).pixels;
      const grid = svg("path", "chart-grid");
      const lines: string[] = [];
      for (let x = centre.x % step; x < this.#width; x += step) {
        lines.push(`M${x.toFixed(1)} 0V${String(this.#height)}`);
      }
      for (let y = centre.y % step; y < this.#height; y += step) {
        lines.push(`M0 ${y.toFixed(1)}H${String(this.#width)}`);
      }
      grid.setAttribute("d", lines.join(""));
      this.#ground.append(grid);
      for (const rock of obstacles) {
        const at = plot(frame, rock.x, rock.z);
        this.#ground.append(
          svg("circle", "chart-rock", {
            cx: at.x.toFixed(1),
            cy: at.y.toFixed(1),
            r: Math.max(0.9, rock.radius * frame.scale).toFixed(1),
          }),
        );
      }
    }
    this.#ground.append(
      svg("line", "chart-lane", { x1: dock.x, y1: dock.y, x2: centre.x, y2: centre.y }),
      svg("circle", "chart-boundary", { cx: centre.x, cy: centre.y, r: site.radius * frame.scale }),
    );
    if (this.#detailed) {
      const label = svg("text", "chart-caption", {
        x: centre.x,
        y: centre.y - site.radius * frame.scale - 8,
        "text-anchor": "middle",
      });
      label.textContent = `SITE BOUNDARY · ${distanceText(site.radius * 2)} ACROSS`;
      this.#ground.append(label);
    }

    for (const place of places) this.#addPlace(frame, place);

    this.#deposits = this.#detailed
      ? deposits.map((deposit) => {
          const at = plot(frame, deposit.x, deposit.z);
          const dot = svg("circle", "chart-deposit", { cx: at.x, cy: at.y, r: 2.4 });
          dot.dataset.material = deposit.material;
          this.#placeLayer.append(dot);
          return dot;
        })
      : [];
  }

  /** Where a place is drawn: where it is, or at the edge of a preview that does not reach it. */
  #at(frame: ChartFrame, x: number, z: number): { x: number; y: number } {
    const at = plot(frame, x, z);
    return this.#mode === "preview" ? keepInFrame(frame, at, 16) : at;
  }

  #addPlace(frame: ChartFrame, place: ChartPlace): void {
    const at = this.#at(frame, place.x, place.z);
    const reach = place.radius * frame.scale;
    const group = svg("g", "chart-place", {
      transform: `translate(${at.x.toFixed(1)} ${at.y.toFixed(1)})`,
    });
    group.dataset.target = place.id;
    group.dataset.kind = place.kind;
    group.dataset.locked = String(place.locked);
    let sub: SVGTextElement | null = null;
    if (this.#mode === "preview") {
      const size = Math.max(11, reach);
      group.append(
        svg("circle", "place-area", { r: size }),
        svg("path", "place-glyph", { d: GLYPH[place.kind], transform: "scale(0.85)" }),
      );
      if (place.locked) {
        group.append(svg("path", "place-lock", { d: PADLOCK, transform: "translate(9 -9)" }));
      }
    } else if (!this.#detailed) {
      const size = place.kind === "dock" ? 4 : Math.max(4, reach);
      group.append(
        svg("circle", "place-ring", { r: size + 4 }),
        place.kind === "dock"
          ? svg("rect", "place-area", { x: -size, y: -size, width: size * 2, height: size * 2 })
          : svg("circle", "place-area", { r: size }),
      );
    } else {
      const size = Math.max(13, reach);
      const icon = svg("path", "place-glyph", { d: GLYPH[place.kind] });
      const name = svg("text", "place-name", { y: size + 17, "text-anchor": "middle" });
      name.textContent = place.name.toUpperCase();
      sub = svg("text", "place-sub", { y: size + 31, "text-anchor": "middle" });
      // The touch target is a finger wide whatever the pocket's drawn size (design §5).
      const hit = svg("circle", "place-hit", { r: Math.max(24, size + 4) });
      group.append(
        svg("circle", "place-ring", { r: size + 6 }),
        svg("circle", "place-area", { r: size }),
        icon,
      );
      if (place.locked) {
        group.append(svg("path", "place-lock", { d: PADLOCK, transform: "translate(11 -11)" }));
      }
      group.append(name, sub, hit);
      const id = place.id;
      hit.addEventListener("click", () => this.#choose?.(id));
      hit.addEventListener("dblclick", () => this.#jumpTo?.(id));
    }
    this.#placeLayer.append(group);
    this.#places.set(place.id, { group, sub, at });
  }

  #attribute(place: string, node: Element, name: string, value: string): void {
    if (this.#written.changed(`${place}.${name}`, value)) node.setAttribute(name, value);
  }

  update(state: ChartState): void {
    const frame = this.#frame;
    if (!frame) return;
    for (const place of state.places) {
      const drawn = this.#places.get(place.id);
      if (!drawn) continue;
      const { group, sub } = drawn;
      const id = `place.${place.id}`;
      this.#attribute(id, group, "data-selected", String(state.selected === place.id));
      this.#attribute(
        id,
        group,
        "data-jumping",
        String(state.readout.state === "spooling" && state.readout.target === place.id),
      );
      if (sub) {
        const text = [
          distanceText(place.distance),
          ...(place.locked ? ["LOCKED"] : []),
          ...place.tags.map((tag) => tag.toUpperCase()),
        ].join(" · ");
        if (this.#written.changed(`${id}.sub`, text)) sub.textContent = text;
      }
    }

    state.deposits.forEach((left, index) => {
      const dot = this.#deposits[index];
      if (dot) this.#attribute(`deposit.${String(index)}`, dot, "data-empty", String(left <= 0));
    });

    const margin = this.#detailed ? 12 : 7;
    const ship = state.own
      ? keepInFrame(frame, plot(frame, state.own.x, state.own.z), margin)
      : null;
    this.#attribute("ship", this.#ship, "visibility", ship ? "visible" : "hidden");
    if (ship && state.own) {
      const scale = this.#detailed ? 1 : 0.72;
      this.#attribute(
        "ship",
        this.#ship,
        "transform",
        `translate(${ship.x.toFixed(1)} ${ship.y.toFixed(1)}) ` +
          `rotate(${chartBearing(state.own.heading).toFixed(0)}) scale(${String(scale)})`,
      );
    }

    // The course: a line from the ship to the destination chosen.
    const chosen = state.selected === null ? undefined : this.#places.get(state.selected);
    this.#attribute("course", this.#course, "visibility", ship && chosen ? "visible" : "hidden");
    if (ship && chosen) {
      this.#attribute("course", this.#course, "x1", ship.x.toFixed(1));
      this.#attribute("course", this.#course, "y1", ship.y.toFixed(1));
      this.#attribute("course", this.#course, "x2", chosen.at.x.toFixed(1));
      this.#attribute("course", this.#course, "y2", chosen.at.y.toFixed(1));
      this.#attribute(
        "course",
        this.#course,
        "data-jumping",
        String(state.readout.state === "spooling"),
      );
    }

    const seen = new Set<number>();
    for (const contact of state.contacts) {
      if (contact.side === "own") continue;
      seen.add(contact.id);
      let node = this.#contacts.get(contact.id);
      if (!node) {
        node = svg("g", "chart-contact");
        node.dataset.side = contact.side;
        node.dataset.kind = contact.kind;
        const shape = contact.side === "crew" ? SHIP : contact.kind === "core" ? HEXAGON : DIAMOND;
        node.append(svg("path", undefined, { d: shape }));
        this.#contacts.set(contact.id, node);
        this.#contactLayer.append(node);
      }
      const at = keepInFrame(frame, plot(frame, contact.x, contact.z), margin);
      const turned = contact.side === "crew" ? chartBearing(contact.heading).toFixed(0) : "0";
      const scale = this.#detailed ? (contact.side === "crew" ? 0.8 : 1) : 0.6;
      this.#attribute(
        `contact.${String(contact.id)}`,
        node,
        "transform",
        `translate(${at.x.toFixed(1)} ${at.y.toFixed(1)}) rotate(${turned}) scale(${String(scale)})`,
      );
    }
    for (const [id, node] of this.#contacts) {
      if (seen.has(id)) continue;
      node.remove();
      this.#contacts.delete(id);
      this.#written.forget(`contact.${String(id)}.`);
    }
  }
}

/**
 * A still chart of a site, for the briefing: where the dock and the pockets lie, and which are
 * locked, before the pilot commits to the run.
 */
export function sitePreview(
  site: SiteInfo,
  places: readonly ChartPlace[],
  width: number,
  height: number,
): SVGSVGElement {
  const preview = new Plot(width, height, 14, "preview");
  preview.setSite(site, [], [], places);
  return preview.root;
}

interface Row {
  button: HTMLButtonElement;
  status: HTMLElement;
  tags: HTMLElement;
  distance: HTMLElement;
}

export class ChartView {
  /** The full map, over the scene while it is open. */
  readonly overlay = html("div");
  /** The minimap and the jump readout, in a corner for as long as a run lasts. */
  readonly navigator = html("div");
  readonly #on: ChartHandlers;
  readonly #mini: Plot;
  readonly #full: Plot;
  readonly #written = new Written();
  readonly #title = html("h2");
  readonly #list = html("ol", "chart-list");
  readonly #rows = new Map<string, Row>();
  readonly #scale = html("div", "chart-scale");
  readonly #scaleText = html("span");
  readonly #actionTarget = html("p", "action-target");
  readonly #facts = html("dl", "action-facts");
  readonly #factValues: HTMLElement[] = [];
  readonly #action = html("section", "chart-action");
  readonly #progress = html("div", "action-fill");
  readonly #engage = html("button", "chart-engage");
  readonly #engageLabel = html("span");
  readonly #note = html("p", "action-note");
  readonly #jump = html("div");
  readonly #jumpText = html("span", "jump-text");
  readonly #jumpHint = html("span", "jump-hint");
  readonly #jumpFill = html("div", "jump-fill");
  /** The last digit that chooses a destination, in the footer's hint. */
  readonly #lastKey = key("5");
  #places: readonly ChartPlace[] = [];

  constructor(handlers: ChartHandlers) {
    this.#on = handlers;
    this.#mini = new Plot(232, 156, 16, "mini");
    this.#full = new Plot(820, 600, 64, "full", handlers);
    this.#buildNavigator();
    this.#buildOverlay();
  }

  get isOpen(): boolean {
    return this.overlay.dataset.state === "open";
  }

  open(): void {
    if (this.isOpen || this.#places.length === 0) return;
    this.overlay.dataset.state = "open";
    this.navigator.dataset.map = "open";
  }

  close(): void {
    if (!this.isOpen) return;
    delete this.overlay.dataset.state;
    delete this.navigator.dataset.map;
    // Nothing in the closed map may keep the focus: Enter would press a button nobody can see.
    const focused = document.activeElement;
    if (focused instanceof HTMLElement && this.overlay.contains(focused)) focused.blur();
  }

  toggle(): void {
    if (this.isOpen) this.close();
    else this.open();
  }

  /** The destination a digit key chooses, or null. */
  keyed(digit: string): string | null {
    return this.#places.find((place) => place.key === digit)?.id ?? null;
  }

  /** A run begins: draws its site and lists its destinations. */
  setSite(
    site: SiteInfo,
    obstacles: readonly ObstacleInfo[],
    deposits: readonly DepositInfo[],
    places: readonly ChartPlace[],
  ): void {
    this.#places = places;
    this.#written.forget("");
    this.#title.textContent = site.name;
    this.#lastKey.textContent = places.at(-1)?.key ?? "1";
    this.#mini.setSite(site, obstacles, deposits, places);
    this.#full.setSite(site, obstacles, deposits, places);
    const bar = this.#full.bar;
    if (bar) {
      this.#scale.style.width = `${bar.pixels.toFixed(0)}px`;
      this.#scaleText.textContent = distanceText(bar.metres);
    }
    this.#list.replaceChildren();
    this.#rows.clear();
    for (const place of places) {
      const button = html("button", "chart-row");
      button.type = "button";
      button.dataset.target = place.id;
      button.dataset.locked = String(place.locked);
      const main = html("span", "row-main");
      const status = html("small");
      main.append(html("strong", undefined, place.name), status);
      const tags = html("span", "row-tags");
      const distance = html("span", "row-distance");
      button.append(key(place.key), glyph(place.kind, 22), main, tags, distance);
      const id = place.id;
      button.addEventListener("click", () => {
        this.#on.choose(id);
      });
      button.addEventListener("dblclick", () => {
        this.#on.jumpTo(id);
      });
      const item = html("li");
      item.append(button);
      this.#list.append(item);
      this.#rows.set(place.id, { button, status, tags, distance });
    }
    this.navigator.dataset.active = "true";
  }

  /** The run is over: the map goes with it. */
  clear(): void {
    this.close();
    this.#places = [];
    delete this.navigator.dataset.active;
  }

  update(state: ChartState): void {
    this.#places = state.places;
    this.#mini.update(state);
    this.#updateReadout(state);
    if (!this.isOpen) return;
    this.#full.update(state);
    for (const place of state.places) {
      const row = this.#rows.get(place.id);
      if (!row) continue;
      const id = `row.${place.id}`;
      const selected = String(state.selected === place.id);
      if (this.#written.changed(`${id}.selected`, selected)) {
        row.button.dataset.selected = selected;
        row.button.setAttribute("aria-pressed", selected);
      }
      const jumping = String(
        state.readout.state === "spooling" && state.readout.target === place.id,
      );
      if (this.#written.changed(`${id}.jumping`, jumping)) row.button.dataset.jumping = jumping;
      if (this.#written.changed(`${id}.status`, place.status)) {
        row.status.textContent = place.status;
      }
      const tags = place.tags.join(" ");
      if (this.#written.changed(`${id}.tags`, tags)) {
        row.tags.replaceChildren(
          ...place.tags.map((tag) => {
            const node = html("span", "tag", tag.toUpperCase());
            node.dataset.tag = tag;
            return node;
          }),
        );
      }
      const distance = distanceText(place.distance);
      if (this.#written.changed(`${id}.distance`, distance)) row.distance.textContent = distance;
    }
    this.#updateAction(state);
  }

  #updateReadout(state: ChartState): void {
    const { readout } = state;
    const place = state.places.find((candidate) => candidate.id === readout.target);
    if (this.#written.changed("jump.state", readout.state))
      this.#jump.dataset.state = readout.state;
    if (this.#written.changed("jump.target", readout.target ?? "")) {
      if (readout.target === null) delete this.#jump.dataset.target;
      else this.#jump.dataset.target = readout.target;
    }
    const text =
      readout.state === "chosen" && place
        ? `${readout.text} · ${distanceText(place.distance)}`
        : readout.text;
    if (this.#written.changed("jump.text", text)) this.#jumpText.textContent = text;
    if (this.#written.changed("jump.hint", readout.hint)) {
      const [first, ...rest] = readout.hint.split(" ");
      this.#jumpHint.replaceChildren(key(first ?? ""), ` ${rest.join(" ")}`);
    }
    const width = `${(readout.share * 100).toFixed(1)}%`;
    if (this.#written.changed("jump.fill", width)) this.#jumpFill.style.width = width;
  }

  #updateAction(state: ChartState): void {
    const { readout, readiness } = state;
    const spooling = readout.state === "spooling";
    const place = state.places.find(
      (candidate) => candidate.id === (spooling ? readout.target : state.selected),
    );
    const mode = spooling ? "spooling" : readiness.ready ? "ready" : place ? "blocked" : "idle";
    if (this.#written.changed("action.state", mode)) this.#action.dataset.state = mode;
    const target = place?.name ?? "No destination";
    if (this.#written.changed("action.target", target)) this.#actionTarget.textContent = target;
    const facts = place ? [distanceText(place.distance), place.status, place.arrival] : [];
    if (this.#written.changed("action.facts", String(place !== undefined))) {
      this.#facts.hidden = !place;
    }
    facts.forEach((fact, index) => {
      const node = this.#factValues[index];
      if (node && this.#written.changed(`action.fact.${String(index)}`, fact)) {
        node.textContent = fact;
      }
    });
    const label = spooling ? "Cancel jump" : "Engage jump";
    if (this.#written.changed("action.label", label)) this.#engageLabel.textContent = label;
    const disabled = !spooling && !readiness.ready;
    if (this.#written.changed("action.disabled", String(disabled))) {
      this.#engage.disabled = disabled;
    }
    const note = spooling
      ? readout.text
      : readiness.ready
        ? "The drive spools for a few seconds. A hostile lock cancels it."
        : readiness.reason;
    if (this.#written.changed("action.note", note)) this.#note.textContent = note;
    const width = `${(readout.share * 100).toFixed(1)}%`;
    if (this.#written.changed("action.fill", width)) this.#progress.style.width = width;
  }

  #buildNavigator(): void {
    this.navigator.id = "navigator";
    const mini = html("button");
    mini.id = "minimap";
    mini.type = "button";
    mini.dataset.action = "open-map";
    mini.title = "Open the system map (M)";
    mini.setAttribute("aria-label", "Open the system map");
    mini.setAttribute("aria-keyshortcuts", "M");
    // Reached by pointer or by M, never by focus: Space, the trigger, must not press it.
    mini.tabIndex = -1;
    mini.addEventListener("pointerdown", (event) => {
      event.preventDefault();
    });
    mini.addEventListener("click", () => {
      this.toggle();
    });
    const head = html("span", "mini-head");
    head.append(html("span", undefined, "SYSTEM MAP"), key("M"));
    mini.append(head, this.#mini.root);

    this.#jump.id = "jump";
    this.#jump.dataset.state = "idle";
    const track = html("div", "jump-track");
    track.append(this.#jumpFill);
    this.#jump.append(
      html("span", "jump-label", "JUMP DRIVE"),
      this.#jumpHint,
      this.#jumpText,
      track,
    );
    this.navigator.append(mini, this.#jump);
  }

  #buildOverlay(): void {
    this.overlay.id = "chart";
    this.overlay.setAttribute("role", "dialog");
    this.overlay.setAttribute("aria-label", "System map");
    // A click on the dimmed scene around the map closes it.
    this.overlay.addEventListener("click", (event) => {
      if (event.target === this.overlay) this.close();
    });
    // Space is the trigger. A button here that was clicked keeps the focus, and Space would
    // press it again; Enter still presses it.
    for (const type of ["keydown", "keyup"] as const) {
      this.overlay.addEventListener(type, (event) => {
        if (event.code === "Space") event.preventDefault();
      });
    }

    const frame = html("div", "chart-window");
    const head = html("header", "chart-head");
    const titles = html("div");
    titles.append(html("p", "eyebrow", "NAVIGATION / SYSTEM MAP"), this.#title);
    const close = html("button", "chart-close");
    close.type = "button";
    close.dataset.action = "close-map";
    close.append("Close ", key("M"));
    close.addEventListener("click", () => {
      this.close();
    });
    head.append(titles, close);

    const body = html("div", "chart-body");
    const stage = html("div", "chart-plot");
    const legend = html("ul", "chart-legend");
    for (const [side, path, text] of [
      ["own", SHIP, "You"],
      ["crew", SHIP, "Crew"],
      ["hostile", DIAMOND, "Hostile"],
    ] as const) {
      const item = html("li");
      const icon = svg("svg", "glyph", { viewBox: "-10 -10 20 20", width: 14, height: 14 });
      icon.dataset.side = side;
      icon.append(svg("path", undefined, { d: path }));
      item.append(icon, text);
      legend.append(item);
    }
    const scale = html("div", "chart-scale-box");
    scale.append(this.#scale, this.#scaleText);
    stage.append(this.#full.root, scale, legend);

    const side = html("aside", "chart-side");
    for (const label of ["Distance", "Status", "Arrival"]) {
      const row = html("div");
      const value = html("dd");
      row.append(html("dt", undefined, label), value);
      this.#factValues.push(value);
      this.#facts.append(row);
    }
    const track = html("div", "action-track");
    track.append(this.#progress);
    this.#engage.type = "button";
    this.#engage.dataset.action = "engage-jump";
    this.#engage.append(this.#engageLabel, key("J"));
    this.#engage.addEventListener("click", () => {
      this.#on.engage();
    });
    this.#note.setAttribute("role", "status");
    this.#action.append(
      html("p", "eyebrow", "JUMP DRIVE"),
      this.#actionTarget,
      this.#facts,
      track,
      this.#engage,
      this.#note,
    );
    side.append(html("p", "eyebrow", "DESTINATIONS"), this.#list, this.#action);
    body.append(stage, side);

    const foot = html("footer", "chart-foot");
    const digits = html("span");
    digits.append(key("1"), "–", this.#lastKey, " choose");
    foot.append(digits);
    for (const [letter, text] of [
      ["J", "engage or cancel"],
      ["M", "close"],
    ] as const) {
      const hint = html("span");
      hint.append(key(letter), ` ${text}`);
      foot.append(hint);
    }
    foot.append(html("span", "chart-live", "The ship keeps flying: flight keys stay live"));
    frame.append(head, body, foot);
    this.overlay.append(frame);
  }
}
