/**
 * The game client: joins the expedition, flies the player's ship with the keyboard and draws
 * every ship from the server's snapshots with the tactical camera (design §5).
 */
import { Vector3 } from "three";
import { connect, fetchConnectInfo } from "./net/connection";
import type { Connection } from "./net/connection";
import type { SnapshotEvent } from "./protocol";
import type { EntityPose } from "./net/interpolation";
import { Predictor } from "./game/prediction";
import { Selection } from "./game/selection";
import { Shots } from "./game/shots";
import { ExpeditionFullError, Session, waitForJoin } from "./game/session";
import type { Controls } from "./game/session";
import { ExpeditionView, RESTING_ELEVATION } from "./game/render";
import { loadSim } from "./sim";
import type { Sim } from "./sim";
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
const lock = line("lock");

// Shown while the pilot is home after being destroyed.
const destroyed = document.createElement("div");
destroyed.id = "destroyed";
const destroyedText = document.createElement("p");
const launchButton = document.createElement("button");
launchButton.textContent = "Launch";
destroyed.append(destroyedText, launchButton);
document.body.append(destroyed);
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
  // Hold to fire at the locked target (the owner's choice for M2).
  fire: held.has("Space"),
});

// --- Game --------------------------------------------------------------------------------------

async function start(): Promise<void> {
  showStatus("connecting", "Connecting…");
  const connection = await connect(await fetchConnectInfo());
  showStatus("connected", `Connected · protocol ${String(connection.protocolVersion)}`);
  // Browsers do not always close a WebTransport session with its page; without this, the
  // server would keep a ghost ship until the session timed out.
  window.addEventListener("pagehide", () => {
    connection.close();
  });
  void connection.closed.then((reason) => {
    showStatus("closed", reason ? `Disconnected: ${reason}` : "Disconnected");
  });

  const [joined, loaded] = await Promise.all([waitForJoin(connection), rules]);
  loaded.setObstacles(joined.obstacles);
  const view = new ExpeditionView(document.body, joined.shipId, START_WIDTH);
  view.setObstacles(joined.obstacles);
  fly(connection, view, joined.weaponRange, () => beginLife(connection, joined, loaded));
}

/** One ship's flight, from launch until it is destroyed or the session ends. */
interface Life {
  session: Session;
  predictor: Predictor;
  /** Snapshot events not yet drawn: they are drawn where each ship is drawn. */
  events: SnapshotEvent[];
  /** The player's own hull, 0 to 255, from the latest snapshot. */
  hull: { own: number };
}

function beginLife(
  connection: Connection,
  { shipId, tickRate }: { shipId: number; tickRate: number },
  loaded: Sim,
): Life {
  // Your own ship is predicted with the server's rules; others are drawn from snapshots.
  const predictor = new Predictor((state, controls) => loaded.stepShip(state, controls));
  const events: SnapshotEvent[] = [];
  const hull = { own: 255 };
  const session = new Session(
    connection,
    shipId,
    tickRate,
    () => performance.now(),
    (snapshot) => {
      events.push(...snapshot.events);
      const own = snapshot.entities.find((entity) => entity.id === shipId);
      if (!own) return;
      hull.own = own.hull;
      const { x, z } = positionMetres(own);
      const velocity = velocityMetresPerSecond(own);
      predictor.reconcile(
        { x, z, heading: headingRadians(own), speed: Math.hypot(velocity.x, velocity.z) },
        snapshot.inputAck,
      );
    },
  );
  hud.dataset.ship = String(shipId);
  return { session, predictor, events, hull };
}

const KIND_NAME = { playerShip: "Pilot", drone: "Drone", raider: "Raider", warden: "Warden" };

function fly(
  connection: Connection,
  view: ExpeditionView,
  weaponRange: number,
  firstLife: () => Life,
): void {
  let life = firstLife();
  /** False while the pilot is home after being destroyed. */
  let flying = true;
  const shots = new Shots();

  // Destroyed: the pilot is home (design §6) until they launch again in a reissued hull.
  connection.onMessage((message) => {
    if (message.type !== "destroyed") return;
    flying = false;
    life.session.retire();
    selection.release();
    destroyed.dataset.state = "home";
    destroyedText.textContent =
      `Destroyed, returned home. Hull reissued; lost ${String(message.modulesLost)} ` +
      `upgraded modules and ${String(message.cargoLost)} cargo.`;
  });
  launchButton.addEventListener("click", () => {
    launchButton.disabled = true;
    void (async () => {
      const rejoin = waitForJoin(connection);
      await connection.send({ type: "launch" });
      const [joined, loaded] = await Promise.all([rejoin, rules]);
      life = beginLife(connection, joined, loaded);
      view.setOwnShip(joined.shipId);
      flying = true;
      delete destroyed.dataset.state;
    })()
      .catch((error: unknown) => {
        destroyedText.textContent = `Could not launch: ${String(error)}`;
      })
      .finally(() => {
        launchButton.disabled = false;
      });
  });
  const home: LookState = { yaw: 0, pitch: RESTING_ELEVATION };
  let look: LookState = { ...home };
  let viewWidth = START_WIDTH;
  let dragFrom: ScreenPoint | null = null;
  let poses: EntityPose[] = [];
  const selection = new Selection((target) => {
    void connection.send({ type: "lock", target }).catch(() => undefined);
  });
  window.addEventListener("keydown", (event) => {
    if (event.code === "Tab") {
      event.preventDefault();
      const own = poses.find((pose) => pose.id === life.session.shipId);
      if (own)
        selection.cycle(
          own,
          poses.filter((pose) => pose.id !== life.session.shipId),
        );
    } else if (event.code === "Escape") {
      selection.release();
    }
  });
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
    // Clicking a hostile locks it and centres the view; an ally is only centred on; empty space
    // returns the view to your own ship.
    const others = poses.filter((pose) => pose.id !== life.session.shipId);
    const candidates = others.map((pose) => ({
      id: String(pose.id),
      screen: worldToScreen(new Vector3(pose.x, 0, pose.z), view.camera, view.viewport),
    }));
    const picked = pickNearest({ x: event.clientX, y: event.clientY }, candidates, PICK_RADIUS_PX);
    selection.click(others.find((pose) => String(pose.id) === picked) ?? null);
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

  const tickMs = 1_000 / life.session.tickRate;
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
      if (flying) {
        const controls = readControls();
        const { sequence, sent } = life.session.sendControls(controls);
        life.predictor.advance(sequence, sent);
        // Steering takes the view back to your own ship, so it is never flown off screen.
        if (controls.thrust !== 0 || controls.turn !== 0) selection.steer();
      }
    }

    // In flight the view springs back once the look control is released (design §5).
    if (held.has("KeyR")) {
      look = { ...home };
      viewWidth = START_WIDTH;
      selection.steer();
      resize();
    }
    const keyTurn = axis(["KeyE"], ["KeyQ"]);
    if (keyTurn !== 0) look = turnLook(look, keyTurn * KEY_TURN_RATE * dt);
    else if (!dragFrom && !isHome(look, home)) look = settleLook(look, home, dt);

    const { session, predictor, events, hull } = life;
    const others = session.snapshots.sample(now).filter((pose) => pose.id !== session.shipId);
    const predicted = flying ? predictor.visual(1 - Math.max(0, untilTick) / tickMs, dt) : null;
    const own: EntityPose | undefined = predicted
      ? { id: session.shipId, kind: "playerShip", hull: hull.own, ...predicted }
      : undefined;
    poses = own ? [own, ...others] : others;

    const drawnAt = (id: number) => poses.find((pose) => pose.id === id);
    for (const event of events.splice(0)) {
      if (event.type === "fired") {
        const from = drawnAt(event.shooter);
        if (from) shots.fire(event.shooter, event.target, from, event.speed);
      } else {
        const at = drawnAt(event.ship);
        if (at) shots.blast(at);
      }
    }
    shots.advance(dt, drawnAt);
    selection.prune(new Set(others.map((pose) => pose.id)));
    const focus = poses.find((pose) => pose.id === selection.focus);
    const centre = focus ?? own;
    if (centre) target.set(centre.x, 0, centre.z);
    view.render(poses, selection.lock, target, look.pitch, look.yaw, shots);

    const locked = poses.find((pose) => pose.id === selection.lock);
    if (locked && own) {
      lock.dataset.target = String(locked.id);
      lock.textContent =
        `Locked: ${KIND_NAME[locked.kind]} ${String(locked.id)} · ` +
        `${Math.hypot(locked.x - own.x, locked.z - own.z).toFixed(0)} m ` +
        `${Math.hypot(locked.x - own.x, locked.z - own.z) <= weaponRange ? "in range" : "out of range"} · ` +
        `hull ${String(Math.round((locked.hull / 255) * 100))}% · Tab next, Esc release`;
    } else {
      delete lock.dataset.target;
      lock.textContent = "";
    }

    if (own) {
      hud.dataset.ships = poses.map((pose) => pose.id).join(" ");
      if (session.roundTripMs !== null) hud.dataset.roundTrip = session.roundTripMs.toFixed(0);
      hud.dataset.x = own.x.toFixed(2);
      hud.dataset.z = own.z.toFixed(2);
      hud.textContent =
        `Ship ${String(session.shipId)} · ${String(poses.length)} in flight · ` +
        `x ${own.x.toFixed(1)} z ${own.z.toFixed(1)} · ` +
        `hull ${String(Math.round((hull.own / 255) * 100))}% · ` +
        `round trip ${session.roundTripMs === null ? "–" : `${session.roundTripMs.toFixed(0)} ms`} · ` +
        `correction ${(predictor.lastCorrection * 100).toFixed(0)} cm · ` +
        `W A S D fly, Tab lock, Space fire`;
    }
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}

start().catch((error: unknown) => {
  const text = error instanceof ExpeditionFullError ? error.message : String(error);
  showStatus("offline", `Offline: ${error instanceof Error ? error.message : text}`);
});
