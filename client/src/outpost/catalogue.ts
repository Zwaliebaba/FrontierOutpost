/** Generated asset metadata and URLs; looking up an entry never downloads its GLB. */
import generated from "../../../Design/Art/catalogue.json";

interface Asset {
  kind: string;
  id: string;
  glb: string;
  pending: boolean;
  roots: Record<string, unknown>;
}
interface Catalogue {
  assets: Record<string, Asset>;
  legacy: Record<string, { glb: string; root: string | null }>;
}
const catalogue: Catalogue = generated;
const urls = import.meta.glob<string>("../../../Design/**/*.glb", {
  eager: true,
  query: "?url",
  import: "default",
});

function urlFor(path: string): string {
  const normalise = (value: string): string => {
    const parts: string[] = [];
    for (const part of value.split("/")) {
      if (part === "..") parts.pop();
      else if (part !== "." && part !== "") parts.push(part);
    }
    return parts.join("/");
  };
  const resolved = normalise(`../../../Design/Art/${path}`);
  const key = Object.keys(urls).find((candidate) => normalise(candidate) === resolved);
  const url = key === undefined ? undefined : urls[key];
  if (url === undefined) throw new Error(`Catalogue asset has no bundled URL: ${path}`);
  return url;
}

export function legacyUrl(id: string): string {
  const entry = catalogue.legacy[id];
  if (!entry) throw new Error(`Unknown frozen asset: ${id}`);
  return urlFor(entry.glb);
}

export function facilityEntry(id: string): { url: string; roots: string[] } {
  const entry = catalogue.assets[`facility:${id}`];
  if (!entry || entry.pending) throw new Error(`Facility art is not ready: ${id}`);
  return { url: urlFor(entry.glb), roots: Object.keys(entry.roots) };
}

/** A hull's model, by the game's identifier (`frigate`): its GLB and its one root. */
export function hullEntry(id: string): { url: string; root: string } {
  const entry = catalogue.assets[`hull:${id}`];
  const root = entry && Object.keys(entry.roots)[0];
  if (!entry || !root) throw new Error(`Hull art is not in the catalogue: ${id}`);
  return { url: urlFor(entry.glb), root };
}

/** An NPC ship's model, by the game's identifier (`raider`): its GLB and its one root. */
export function npcEntry(id: string): { url: string; root: string } | null {
  const entry = catalogue.assets[`npc:${id}`];
  const root = entry && Object.keys(entry.roots)[0];
  return entry && root ? { url: urlFor(entry.glb), root } : null;
}
