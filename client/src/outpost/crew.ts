/**
 * The crew panel (design §2): form a crew or join one by its invite code, see who is in it and
 * the split, and, as host, change the split. The owner's launch rule: the host launches and the
 * others follow within a minute, so a member's launch button follows the host.
 */
import type { ClientMessage, CrewInfo } from "../protocol";

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  text?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (text !== undefined) node.textContent = text;
  return node;
}

function button(text: string, action: string, onClick: () => void): HTMLButtonElement {
  const node = el("button", text);
  node.dataset.action = action;
  node.addEventListener("click", onClick);
  return node;
}

/** What the host may launch, and what a member may do about the host's launch. */
export interface LaunchRole {
  /** Whether this pilot's own launch buttons start the run. */
  launches: boolean;
  /** A member may follow the host into the run they launched at this tier. */
  follow: number | null;
  note: string | null;
}

export function launchRole(crew: CrewInfo | null): LaunchRole {
  if (!crew || crew.members.length < 2) return { launches: true, follow: null, note: null };
  if (crew.hosting) {
    return {
      launches: true,
      follow: null,
      note: "You host: your launch takes the crew's contract, and the crew follows you.",
    };
  }
  if (crew.launch) {
    return {
      launches: false,
      follow: crew.launch.tier,
      note: `The host has launched: follow within ${String(crew.launch.secondsLeft)} s.`,
    };
  }
  return { launches: false, follow: null, note: "The host launches; you follow within a minute." };
}

/** Whole percentages read from the split inputs, or null unless they add up to 100. */
export function readSplit(values: readonly string[]): number[] | null {
  const shares = values.map((value) => Number(value));
  if (shares.some((share) => !Number.isInteger(share) || share < 0)) return null;
  return shares.reduce((sum, share) => sum + share, 0) === 100 ? shares : null;
}

/** Rebuilds the crew section in `container`. */
export function renderCrew(
  container: HTMLElement,
  crew: CrewInfo | null,
  send: (message: ClientMessage) => void,
): void {
  container.replaceChildren();
  container.className = "crew";
  container.append(el("h3", "Crew"));
  if (!crew) {
    delete container.dataset.code;
    container.append(el("p", "Fly with up to three others: form a crew, or join one by its code."));
    const code = el("input");
    code.id = "crew-code";
    code.placeholder = "Code";
    code.maxLength = 6;
    code.autocomplete = "off";
    container.append(
      button("Form a crew", "create-crew", () => {
        send({ type: "createCrew" });
      }),
      code,
      button("Join", "join-crew", () => {
        if (code.value.trim()) send({ type: "joinCrew", code: code.value.trim() });
      }),
    );
    return;
  }
  container.dataset.code = crew.code;
  container.dataset.members = String(crew.members.length);
  container.append(el("p", `Invite code ${crew.code}`));
  const list = el("ul");
  const inputs: HTMLInputElement[] = [];
  crew.members.forEach((member, index) => {
    const item = el("li");
    item.dataset.share = String(member.share);
    const name = `${member.callsign}${index === 0 ? " (host)" : ""}${member.you ? " · you" : ""}`;
    if (crew.hosting && crew.members.length > 1) {
      const share = el("input");
      share.type = "number";
      share.min = "0";
      share.max = "100";
      share.value = String(member.share);
      share.dataset.split = String(index);
      inputs.push(share);
      item.append(`${name} `, share, " %");
    } else {
      item.textContent = `${name} · ${String(member.share)}%`;
    }
    list.append(item);
  });
  container.append(list);
  if (inputs.length > 0) {
    container.append(
      button("Set split", "set-split", () => {
        const shares = readSplit(inputs.map((input) => input.value));
        if (shares) send({ type: "setSplit", shares });
      }),
    );
  }
  container.append(
    button("Leave crew", "leave-crew", () => {
      send({ type: "leaveCrew" });
    }),
  );
}
