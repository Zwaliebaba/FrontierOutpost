/**
 * The jump lab: one pilot's ship jumping back and forth on a loop, drawn by the game's own scene
 * and effects with no server, so the jump's look can be judged and tuned. `?calm=1` shows it as
 * reduced motion draws it; `?t=7.05` holds the loop at that second, for screenshots.
 */
import "../chart.css";
import { Vector3 } from "three";
import { ExpeditionView, RESTING_ELEVATION } from "../game/render";
import { JumpEffects } from "../game/jumpEffects";
import type { EntityPose } from "../net/interpolation";
import { loadOutpostModels } from "../outpost/models";
import { useShipModel } from "../view/readability";

/** Seconds at rest, spooling, and after arriving, for each of the loop's two jumps. */
const REST = 1;
const SPOOL = 6;
const AFTER = 3;
const LEG = REST + SPOOL + AFTER;
/** How far each jump goes, metres: short, so both ends are in view. */
const HOP = 30;
const VIEW_WIDTH = 64;
const STEP = 1 / 60;
const SHIP = 1;

const params = new URLSearchParams(window.location.search);
const calm = params.get("calm") === "1";
const held = params.get("t");

const view = new ExpeditionView(document.body, SHIP, VIEW_WIDTH);
view.world.configureFeedback(!calm, 0);
const wash = document.createElement("div");
wash.id = "jump-wash";
document.body.prepend(wash);
const caption = document.querySelector<HTMLElement>("#caption");
const centre = new Vector3(HOP / 2, 0, 0);

let jumps = new JumpEffects();
let clock = 0;

/** Where the ship is and which way it faces `t` seconds into the loop, and its spool. */
function scripted(t: number): { pose: EntityPose; share: number | null; phase: string } {
  const loop = t % (2 * LEG);
  const outbound = loop < LEG;
  const into = outbound ? loop : loop - LEG;
  const jumped = into >= REST + SPOOL;
  const from = outbound ? 0 : HOP;
  const to = outbound ? HOP : 0;
  const share = into >= REST && !jumped ? (into - REST) / SPOOL : null;
  return {
    pose: {
      id: SHIP,
      kind: "playerShip",
      x: jumped ? to : from,
      z: 0,
      heading: outbound ? 0 : Math.PI,
      hull: 1,
    },
    share,
    phase: jumped
      ? "arrived"
      : share === null
        ? "at rest"
        : `spooling ${(share * 100).toFixed(0)}%`,
  };
}

/** Moves the loop on by `dt` seconds. */
function step(dt: number): EntityPose[] {
  clock += dt;
  const { pose, share } = scripted(clock);
  jumps.setCharge(share === null ? null : SHIP, share ?? 0);
  const leapt = jumps.observe([pose], dt);
  if (leapt.includes(SHIP) && !calm && held === null) {
    wash.animate([{ opacity: 0 }, { opacity: 0.8, offset: 0.12 }, { opacity: 0 }], {
      duration: 420,
      easing: "ease-out",
    });
  }
  jumps.advance(dt);
  return [pose];
}

function draw(poses: EntityPose[], dt: number): void {
  view.setPunch(calm ? 1 : jumps.punch(SHIP));
  view.render(poses, null, centre, RESTING_ELEVATION, 0, undefined, dt, jumps);
  if (caption) caption.textContent = `${clock.toFixed(2)} s · ${scripted(clock).phase}`;
}

function resize(): void {
  view.resize(window.innerWidth, window.innerHeight, VIEW_WIDTH);
}

/** Replays the loop from the start to `t` seconds, so a held frame is the same every time. */
function holdAt(t: number): void {
  jumps = new JumpEffects();
  clock = 0;
  let poses = step(0);
  while (clock + STEP <= t) poses = step(STEP);
  draw(poses, STEP);
}

window.addEventListener("resize", () => {
  resize();
  if (held !== null) holdAt(Number(held));
});
resize();

loadOutpostModels()
  .then((models) => {
    useShipModel(models.frigate);
    view.world.redraw();
  })
  .catch(() => {
    // The greybox hull stands in.
  })
  .finally(() => {
    if (held !== null) {
      holdAt(Number(held));
      document.body.dataset.ready = "true";
      return;
    }
    let last = performance.now();
    const frame = (now: number) => {
      const dt = Math.min(0.1, (now - last) / 1_000);
      last = now;
      draw(step(dt), dt);
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
    document.body.dataset.ready = "true";
  });
