import {
  Color,
  DirectionalLight,
  HemisphereLight,
  IcosahedronGeometry,
  LinearSRGBColorSpace,
  Mesh,
  MeshStandardMaterial,
  Scene,
  WebGLRenderer,
} from "three";
import { createTacticalCamera } from "./camera";
import { connect, fetchConnectInfo } from "./net/connection";

// Placeholder scene: a flat-shaded asteroid under the style guide's lighting, viewed from the
// shared tactical camera. Colours are the Blender linear baselines from Visual Style Guide §5–6.
const linear = (r: number, g: number, b: number) =>
  new Color().setRGB(r, g, b, LinearSRGBColorSpace);

const renderer = new WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

const scene = new Scene();
scene.background = linear(0.035, 0.06, 0.11);
scene.add(new HemisphereLight(linear(0.3, 0.48, 1), linear(0.025, 0.038, 0.055), 0.6));
const starlight = new DirectionalLight(linear(0.67, 0.77, 1), 2.4);
starlight.position.set(-4, 10, 6);
scene.add(starlight);

const asteroid = new Mesh(
  new IcosahedronGeometry(3, 1),
  new MeshStandardMaterial({ color: linear(0.1, 0.12, 0.15), roughness: 0.95, flatShading: true }),
);
scene.add(asteroid);

let camera = createTacticalCamera(window.innerWidth / window.innerHeight);
window.addEventListener("resize", () => {
  renderer.setSize(window.innerWidth, window.innerHeight);
  camera = createTacticalCamera(window.innerWidth / window.innerHeight);
});

renderer.setAnimationLoop((time: number) => {
  asteroid.rotation.y = time / 8000;
  renderer.render(scene, camera);
});

// Connection status, until there is a game to show. `data-state` is for tests.
const status = document.createElement("p");
status.id = "status";
document.body.append(status);
function showStatus(state: "connecting" | "connected" | "closed" | "offline", text: string) {
  status.dataset.state = state;
  status.textContent = text;
}
showStatus("connecting", "Connecting…");
void (async () => {
  try {
    const connection = await connect(await fetchConnectInfo());
    showStatus("connected", `Connected · protocol ${String(connection.protocolVersion)}`);
    const reason = await connection.closed;
    showStatus("closed", reason ? `Disconnected: ${reason}` : "Disconnected");
  } catch (error) {
    showStatus("offline", `Offline: ${error instanceof Error ? error.message : String(error)}`);
  }
})();
