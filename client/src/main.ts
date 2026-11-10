/**
 * The game client: joins the expedition, flies the player's ship with the keyboard and draws
 * every ship from the server's snapshots with the tactical camera (design §5).
 */
import { Vector3 } from "three";
import { connect, fetchConnectInfo } from "./net/connection";
import type { EntityPose } from "./net/interpolation";
import { Predictor } from "./game/prediction";
import { ExpeditionFullError, Session, waitForJoin } from "./game/session";
import type { Controls } from "./game/session";
import { ExpeditionView, RESTING_ELEVATION } from "./game/render";
import { loadSim } from "./sim";
import { headingRadians, positionMetres, velocityMetresPerSecond } from "./net/snapshot";
import { dragLook, isHome, settleLook, turnLook, zoomWidth } from "./view/look";
import type { LookState } from "./view/look";
import { pickNearest, worldToScreen } from "./view/picking";
import type { ScreenPoint } from "./view/picking";

const START_WIDTH = 40;
const PICK_RADIUS_PX = 24;
const KEY_TURN_RATE = 1.6;

function line(id: string): HTMLParagraphElement {
  const element = document.createElement("p");
  element.id = id;
  element.className = "hud-line";
  document.body.append(element);
  return element;
}

// Status lines. `data-state` is for tests.
const status = line("status");
const sim = line("sim");
const hud = line("hud");
function showStatus(state: "connecting" | "connected" | "closed" | "offline", text: string) {
  status.dataset.state = state;
  status.textContent = text;
}

const rules = loadSim().then(
  (loaded) => {
    sim.dataset.state = "loaded";
    sim.textContent = `Simulation · ${String(loaded.ticksPerSecond())} ticks per second`;
    return loaded;
  },
  (error: unknown) => {
    sim.dataset.state = "failed";
    sim.textContent = `Simulation failed to load: ${String(error)}`;
    throw error;
  },
);

// --- Input -------------------------------------------------------------------------------------

const held = new Set<string>();
window.addEventListener("keydown", (event) => held.add(event.code));
window.addEventListener("keyup", (event) => held.delete(event.code));
window.addEventListener("blur", () => {
  held.clear();
});
const axis = (positive: string[], negative: string[]) =>
  Number(positive.some((key) => held.has(key))) - Number(negative.some((key) => held.has(key)));
const readControls = (): Controls => ({
  thrust: axis(["KeyW", "ArrowUp"], ["KeyS", "ArrowDown"]),
  turn: axis(["KeyD", "ArrowRight"], ["KeyA", "ArrowLeft"]),
});

// --- Game --------------------------------------------------------------------------------------

async function start(): Promise<void> {
  showStatus("connecting", "Connecting…");
  const connection = await connect(await fetchConnectInfo());
  showStatus("connected", `Connected · protocol ${String(connection.protocolVersion)}`);
  void connection.closed.then((reason) => {
    showStatus("closed", reason ? `Disconnected: ${reason}` : "Disconnected");
  });

  const [{ shipId, tickRate }, loaded] = await Promise.all([waitForJoin(connection), rules]);
  // Your own ship is predicted with the server's rules; others are drawn from snapshots.
  const predictor = new Predictor((state, controls) => loaded.stepShip(state, controls));
  const session = new Session(
    connection,
    shipId,
    tickRate,
    () => performance.now(),
    (snapshot) => {
      const own = snapshot.entities.find((entity) => entity.id === shipId);
      if (!own) return;
      const { x, z } = positionMetres(own);
      const velocity = velocityMetresPerSecond(own);
      predictor.reconcile(
        { x, z, heading: headingRadians(own), speed: Math.hypot(velocity.x, velocity.z) },
        snapshot.inputAck,
      );
    },
  );
  const view = new ExpeditionView(document.body, shipId, START_WIDTH);
  hud.dataset.ship = String(shipId);
  fly(session, view, predictor);
}

function fly(session: Session, view: ExpeditionView, predictor: Predictor): void {
  const home: LookState = { yaw: 0, pitch: RESTING_ELEVATION };
  let look: LookState = { ...home };
  let viewWidth = START_WIDTH;
  let dragFrom: ScreenPoint | null = null;
  let focusId: number | null = null;
  let poses: EntityPose[] = [];
  const target = new Vector3();

  const resize = () => {
    view.resize(window.innerWidth, window.innerHeight, viewWidth);
  };
  window.addEventListener("resize", resize);
  resize();

  const canvas = view.canvas;
  canvas.addEventListener("contextmenu", (event) => {
    event.preventDefault();
  });
  canvas.addEventListener("pointerdown", (event) => {
    if (event.button === 2) {
      dragFrom = { x: event.clientX, y: event.clientY };
      canvas.setPointerCapture(event.pointerId);
      return;
    }
    if (event.button !== 0) return;
    // Clicking a ship centres the view on it; clicking empty space returns to your own ship.
    const candidates = poses
      .filter((pose) => pose.id !== session.shipId)
      .map((pose) => ({
        id: String(pose.id),
        screen: worldToScreen(new Vector3(pose.x, 0, pose.z), view.camera, view.viewport),
      }));
    const picked = pickNearest({ x: event.clientX, y: event.clientY }, candidates, PICK_RADIUS_PX);
    focusId = picked === null ? null : Number(picked);
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!dragFrom) return;
    look = dragLook(look, event.clientX - dragFrom.x, event.clientY - dragFrom.y);
    dragFrom = { x: event.clientX, y: event.clientY };
  });
  const endDrag = () => {
    dragFrom = null;
  };
  canvas.addEventListener("pointerup", endDrag);
  canvas.addEventListener("pointercancel", endDrag);
  canvas.addEventListener(
    "wheel",
    (event) => {
      event.preventDefault();
      viewWidth = zoomWidth(viewWidth, event.deltaY, event.deltaMode);
      resize();
    },
    { passive: false },
  );

  const tickMs = 1_000 / session.tickRate;
  let last = performance.now();
  let untilTick = 0;
  const frame = (now: number) => {
    const dt = Math.min(0.1, (now - last) / 1_000);
    last = now;

    // Controls go out once per simulation tick, as the server steps.
    untilTick -= dt * 1_000;
    if (untilTick <= 0) {
      untilTick += tickMs;
      if (untilTick < 0) untilTick = tickMs;
      const controls = readControls();
      const { sequence, sent } = session.sendControls(controls);
      predictor.advance(sequence, sent);
      // Steering takes the view back to your own ship, so it is never flown off screen.
      if (controls.thrust !== 0 || controls.turn !== 0) focusId = null;
    }

    // In flight the view springs back once the look control is released (design §5).
    if (held.has("KeyR")) {
      look = { ...home };
      viewWidth = START_WIDTH;
      focusId = null;
      resize();
    }
    const keyTurn = axis(["KeyE"], ["KeyQ"]);
    if (keyTurn !== 0) look = turnLook(look, keyTurn * KEY_TURN_RATE * dt);
    else if (!dragFrom && !isHome(look, home)) look = settleLook(look, home, dt);

    const others = session.snapshots.sample(now).filter((pose) => pose.id !== session.shipId);
    const predicted = predictor.visual(1 - Math.max(0, untilTick) / tickMs, dt);
    const own: EntityPose | undefined = predicted
      ? { id: session.shipId, kind: "playerShip", hull: 255, ...predicted }
      : undefined;
    poses = own ? [own, ...others] : others;
    const focus = poses.find((pose) => pose.id === focusId);
    if (focusId !== null && !focus) focusId = null;
    const centre = focus ?? own;
    if (centre) target.set(centre.x, 0, centre.z);
    view.render(poses, target, look.pitch, look.yaw);

    if (own) {
      hud.dataset.ships = poses.map((pose) => pose.id).join(" ");
      hud.dataset.x = own.x.toFixed(2);
      hud.dataset.z = own.z.toFixed(2);
      hud.textContent =
        `Ship ${String(session.shipId)} · ${String(poses.length)} in flight · ` +
        `x ${own.x.toFixed(1)} z ${own.z.toFixed(1)} · ` +
        `correction ${(predictor.lastCorrection * 100).toFixed(0)} cm · W A S D to fly`;
    }
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}

start().catch((error: unknown) => {
  const text = error instanceof ExpeditionFullError ? error.message : String(error);
  showStatus("offline", `Offline: ${error instanceof Error ? error.message : text}`);
});
