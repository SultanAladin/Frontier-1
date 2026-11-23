//============================================================================================================================================
//                                                              EDITORASSETS.JS
//============================================================================================================================================
// 🧩 Editor assets: low-poly but true-to-life models for scene objects that have no geometry of their own, built in code with three.js
// (so they need no files), drawn in the 3D viewport, previewed in the inspector and captured as the outliner's icons.
//   • Cameras, three bodies (CameraBodies): A · Film (a studio film camera: twin-reel magazine, matte box with a top flag, eyepiece,
//     follow focus on 15 mm rods), B · Digital (a compact cinema box: cooling fins, top handle, side monitor, V-mount battery, geared
//     prime) and C · Mirrorless (a stills body: grip, viewfinder hump, dials, a ribbed zoom lens).
//   • A tripod (fluid head, three legs with a spreader) for cameras standing in the scene.
// Every model looks down −z with +y up, in metres at real size, with its origin under the body (on the tripod's head plate).

import { THREE } from "../Shared/Studio/Render.js";

export const CameraBodies = [
  { Id: "film", Letter: "A", Name: "Film", Icon: "asset-camera-film", Caption: "Studio film camera · magazine, matte box" },
  { Id: "digital", Letter: "B", Name: "Digital", Icon: "asset-camera-digital", Caption: "Digital cinema · monitor, V-mount" },
  { Id: "mirrorless", Letter: "C", Name: "Mirrorless", Icon: "asset-camera-mirrorless", Caption: "Stills body · zoom lens" },
];
// The editor camera is a stills body; scene cameras start as film cameras.
export const CameraBodyOf = (Id, Chosen) => CameraBodies.find((Each) => Each.Id === Chosen)?.Id || (Id === "camera" ? "mirrorless" : "film");

// ── Materials ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────
const Finish = (Colour, Roughness, Metalness, Extra = {}) => new THREE.MeshStandardMaterial({ color: new THREE.Color(Colour), roughness: Roughness, metalness: Metalness, ...Extra });
function Finishes() {
  return {
    Body: Finish("#32353b", 0.4, 0.5),
    Satin: Finish("#464a51", 0.45, 0.5),
    Rubber: Finish("#141517", 0.88, 0),
    Chrome: Finish("#c4c8ce", 0.22, 1),
    Steel: Finish("#8d9198", 0.35, 0.9),
    Glass: Finish("#16263a", 0.04, 0.9, { emissive: new THREE.Color("#0b1626"), envMapIntensity: 2 }),
    Accent: Finish("#c8402b", 0.4, 0.3),
    Gold: Finish("#c79a45", 0.3, 0.9),
    Light: Finish("#d6d7d9", 0.5, 0.1),
    Screen: Finish("#0d1b2c", 0.15, 0.2, { emissive: new THREE.Color("#2a5d8c"), emissiveIntensity: 0.55 }),
    Tally: Finish("#ff2b1c", 0.3, 0, { emissive: new THREE.Color("#ff2b1c"), emissiveIntensity: 1.4 }),
    Battery: Finish("#4b4f57", 0.6, 0.2),
  };
}

// ── Shapes ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// A box with rounded edges (radius R, Steps segments on each round), centred.
function RoundedBox(Width, Height, Depth, R, Steps = 2) {
  R = Math.min(R, Width / 2 - 1e-4, Height / 2 - 1e-4, Depth / 2 - 1e-4);
  const W = Width / 2 - R, H = Height / 2 - R;
  const Outline = new THREE.Shape();
  Outline.moveTo(-W, -H - R);
  Outline.lineTo(W, -H - R);
  Outline.quadraticCurveTo(W + R, -H - R, W + R, -H);
  Outline.lineTo(W + R, H);
  Outline.quadraticCurveTo(W + R, H + R, W, H + R);
  Outline.lineTo(-W, H + R);
  Outline.quadraticCurveTo(-W - R, H + R, -W - R, H);
  Outline.lineTo(-W - R, -H);
  Outline.quadraticCurveTo(-W - R, -H - R, -W, -H - R);
  const Geometry = new THREE.ExtrudeGeometry(Outline, { depth: Depth - 2 * R, bevelEnabled: true, bevelThickness: R, bevelSize: R * 0.98, bevelSegments: Steps, curveSegments: Steps });
  Geometry.translate(0, 0, -(Depth - 2 * R) / 2);
  Geometry.computeVertexNormals();
  return Geometry;
}
// A cylinder along z (the lens axis).
function Tube(Radius, Length, Sides = 24, RadiusBack = Radius) {
  const Geometry = new THREE.CylinderGeometry(Radius, RadiusBack, Length, Sides);
  Geometry.rotateX(Math.PI / 2);
  return Geometry;
}
// A ring with ribs (a grip ring on a lens), along z.
function Ribbed(Radius, Length, Ribs = 28, Depth = 0.06) {
  const Geometry = new THREE.CylinderGeometry(Radius, Radius, Length, Ribs * 2, 1);
  const Points = Geometry.attributes.position;
  for (let Index = 0; Index < Points.count; Index++) {
    const X = Points.getX(Index), Z = Points.getZ(Index), Out = Math.hypot(X, Z);
    if (Out < Radius * 0.5) continue;
    const Slot = Math.round(((Math.atan2(Z, X) + Math.PI) / (Math.PI * 2)) * Ribs * 2);
    const Scale = Slot % 2 ? 1 - Depth : 1;
    Points.setXYZ(Index, X * Scale, Points.getY(Index), Z * Scale);
  }
  Geometry.rotateX(Math.PI / 2);
  Geometry.computeVertexNormals();
  return Geometry;
}
// A lens barrel turned from a profile of [radius, z] points (front at −z).
function Turned(Profile, Sides = 28) {
  const Geometry = new THREE.LatheGeometry(Profile.map(([R, Z]) => new THREE.Vector2(R, Z)), Sides);
  Geometry.rotateX(-Math.PI / 2);
  return Geometry;
}
function Part(Group, Geometry, Material, [X, Y, Z] = [0, 0, 0], [Rx, Ry, Rz] = [0, 0, 0]) {
  const Piece = new THREE.Mesh(Geometry, Material);
  Piece.position.set(X, Y, Z);
  Piece.rotation.set(Rx, Ry, Rz);
  Piece.castShadow = Piece.receiveShadow = true;
  Group.add(Piece);
  return Piece;
}
// The front glass: a slightly domed disc with a coloured coating ring.
function FrontGlass(Group, M, Radius, Z, Y = 0) {
  const Dome = new THREE.SphereGeometry(Radius * 2.2, 24, 6, 0, Math.PI * 2, 0, Math.asin(1 / 2.2));
  Dome.rotateX(-Math.PI / 2);
  Dome.translate(0, 0, Radius * 2.2 * Math.cos(Math.asin(1 / 2.2)));
  Part(Group, Dome, M.Glass, [0, Y, Z + 0.002]);
  Part(Group, new THREE.TorusGeometry(Radius * 1.02, Radius * 0.05, 6, 28), M.Rubber, [0, Y, Z]);
}

// ── Cameras ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
function FilmCamera(M) {
  const G = new THREE.Group();
  // Body and baseplate.
  Part(G, RoundedBox(0.16, 0.19, 0.3, 0.014), M.Body, [0, 0.125, 0]);
  Part(G, RoundedBox(0.15, 0.03, 0.34, 0.006), M.Satin, [0, 0.015, 0.0]);
  Part(G, RoundedBox(0.012, 0.15, 0.24, 0.004), M.Satin, [0.086, 0.125, 0.01]);
  // Twin-reel magazine on top: two drums joined by a throat.
  for (const Z of [-0.075, 0.095]) {
    Part(G, new THREE.CylinderGeometry(0.095, 0.095, 0.07, 32), M.Body, [0, 0.31, Z], [0, 0, Math.PI / 2]);
    Part(G, new THREE.CylinderGeometry(0.06, 0.06, 0.074, 20), M.Satin, [0, 0.31, Z], [0, 0, Math.PI / 2]);
    Part(G, new THREE.CylinderGeometry(0.012, 0.012, 0.08, 10), M.Chrome, [0, 0.31, Z], [0, 0, Math.PI / 2]);
  }
  Part(G, RoundedBox(0.06, 0.07, 0.12, 0.01), M.Body, [0, 0.235, 0.01]);
  // Lens: mount, a stepped barrel with focus and iris rings, front glass.
  const LensY = 0.13;
  Part(G, Tube(0.055, 0.02), M.Chrome, [0, LensY, -0.16]);
  Part(G, Turned([[0.001, 0], [0.046, 0], [0.046, 0.03], [0.05, 0.035], [0.05, 0.11], [0.058, 0.12], [0.058, 0.16], [0.001, 0.16]]), M.Body, [0, LensY, -0.17]);
  Part(G, Ribbed(0.054, 0.03, 30), M.Rubber, [0, LensY, -0.235]);
  Part(G, Ribbed(0.051, 0.018, 30, 0.08), M.Satin, [0, LensY, -0.27]);
  Part(G, new THREE.TorusGeometry(0.05, 0.0025, 4, 28), M.Accent, [0, LensY, -0.3]);
  FrontGlass(G, M, 0.05, -0.331, LensY);
  // Matte box: a square funnel of four plates and a top flag.
  const Box = new THREE.Group();
  Box.position.set(0, LensY, -0.37);
  const Plate = (W, H, D) => RoundedBox(W, H, D, 0.002, 1);
  Part(Box, Plate(0.2, 0.006, 0.09), M.Rubber, [0, 0.075, 0], [-0.12, 0, 0]);
  Part(Box, Plate(0.2, 0.006, 0.09), M.Rubber, [0, -0.075, 0], [0.12, 0, 0]);
  Part(Box, Plate(0.006, 0.15, 0.09), M.Rubber, [0.1, 0, 0], [0, 0.12, 0]);
  Part(Box, Plate(0.006, 0.15, 0.09), M.Rubber, [-0.1, 0, 0], [0, -0.12, 0]);
  Part(Box, Plate(0.21, 0.004, 0.11), M.Body, [0, 0.085, -0.095], [-0.18, 0, 0]);
  Part(Box, Plate(0.2, 0.15, 0.012), M.Satin, [0, 0, 0.05]);
  G.add(Box);
  // 15 mm rods under the lens, a follow-focus unit and its gear on the focus ring.
  for (const X of [-0.03, 0.03]) Part(G, Tube(0.0075, 0.4, 10), M.Steel, [X, 0.045, -0.15]);
  Part(G, RoundedBox(0.04, 0.05, 0.035, 0.005), M.Satin, [0.075, 0.07, -0.235]);
  Part(G, new THREE.CylinderGeometry(0.018, 0.018, 0.012, 16), M.Chrome, [0.06, 0.09, -0.235], [0, 0, Math.PI / 2]);
  Part(G, new THREE.CylinderGeometry(0.016, 0.016, 0.03, 14), M.Rubber, [0.1, 0.07, -0.235], [0, 0, Math.PI / 2]);
  // Eyepiece on the operator's side, angled back, with a rubber cup.
  const Eye = new THREE.Group();
  Eye.position.set(-0.11, 0.18, 0.05);
  Eye.rotation.set(0, -0.25, 0);
  Part(Eye, Tube(0.02, 0.2, 14), M.Body, [0, 0, 0.08]);
  Part(Eye, Tube(0.032, 0.05, 16, 0.026), M.Rubber, [0, 0, 0.2]);
  Part(Eye, RoundedBox(0.05, 0.06, 0.05, 0.008), M.Body, [0.035, 0, -0.03]);
  G.add(Eye);
  // Carry handle and a red tally light.
  Part(G, RoundedBox(0.024, 0.02, 0.2, 0.008), M.Rubber, [0, 0.43, 0.01]);
  for (const Z of [-0.08, 0.1]) Part(G, RoundedBox(0.018, 0.05, 0.018, 0.004), M.Satin, [0, 0.4, Z]);
  Part(G, new THREE.SphereGeometry(0.006, 10, 6), M.Tally, [0.06, 0.2, -0.15]);
  return G;
}

function DigitalCamera(M) {
  const G = new THREE.Group();
  // Body: a cube-ish box with cooling fins on the left.
  Part(G, RoundedBox(0.13, 0.13, 0.18, 0.012), M.Body, [0, 0.08, 0]);
  for (let Fin = 0; Fin < 7; Fin++) Part(G, RoundedBox(0.008, 0.1, 0.012, 0.002, 1), M.Satin, [-0.069, 0.08, -0.06 + Fin * 0.02]);
  Part(G, RoundedBox(0.12, 0.016, 0.2, 0.004), M.Satin, [0, 0.008, 0]);
  // Top handle on two rosettes.
  for (const Z of [-0.06, 0.07]) Part(G, new THREE.CylinderGeometry(0.012, 0.012, 0.04, 12), M.Satin, [0, 0.165, Z]);
  Part(G, RoundedBox(0.03, 0.026, 0.2, 0.01), M.Rubber, [0, 0.195, 0.005]);
  // Side monitor on an arm, screen facing the operator.
  Part(G, Tube(0.006, 0.06, 8), M.Steel, [0.07, 0.17, -0.02], [0, Math.PI / 2, 0]);
  const Monitor = new THREE.Group();
  Monitor.position.set(0.13, 0.2, -0.02);
  Monitor.rotation.set(-0.1, 0.5, 0);
  Part(Monitor, RoundedBox(0.15, 0.095, 0.022, 0.006), M.Body);
  Part(Monitor, new THREE.PlaneGeometry(0.135, 0.078), M.Screen, [0, 0, 0.0115]);
  G.add(Monitor);
  // V-mount battery at the back.
  Part(G, RoundedBox(0.1, 0.13, 0.05, 0.01), M.Battery, [0, 0.085, 0.12]);
  Part(G, RoundedBox(0.06, 0.012, 0.004, 0.002, 1), M.Light, [0, 0.1, 0.146]);
  // Geared cinema prime: housing, two gear rings, focus scale, front glass.
  const LensY = 0.08;
  Part(G, Tube(0.05, 0.02), M.Chrome, [0, LensY, -0.1]);
  Part(G, Turned([[0.001, 0], [0.042, 0], [0.042, 0.02], [0.047, 0.025], [0.047, 0.14], [0.055, 0.15], [0.055, 0.19], [0.001, 0.19]]), M.Body, [0, LensY, -0.11]);
  Part(G, Ribbed(0.054, 0.016, 40, 0.1), M.Satin, [0, LensY, -0.17]);
  Part(G, Ribbed(0.054, 0.016, 40, 0.1), M.Satin, [0, LensY, -0.225]);
  Part(G, new THREE.TorusGeometry(0.0475, 0.0018, 4, 30), M.Light, [0, LensY, -0.2]);
  FrontGlass(G, M, 0.05, -0.3, LensY);
  Part(G, new THREE.SphereGeometry(0.005, 10, 6), M.Tally, [0.05, 0.135, -0.09]);
  Part(G, RoundedBox(0.03, 0.012, 0.004, 0.002, 1), M.Accent, [-0.035, 0.125, -0.091]);
  return G;
}

function MirrorlessCamera(M) {
  const G = new THREE.Group();
  // Body, grip and viewfinder hump.
  Part(G, RoundedBox(0.128, 0.078, 0.042, 0.009), M.Body, [0, 0.045, 0]);
  Part(G, RoundedBox(0.036, 0.074, 0.03, 0.012), M.Rubber, [0.048, 0.043, -0.026]);
  const Hump = new THREE.Shape();
  Hump.moveTo(-0.026, 0);
  Hump.lineTo(0.026, 0);
  Hump.lineTo(0.017, 0.024);
  Hump.lineTo(-0.017, 0.024);
  Hump.closePath();
  const HumpGeometry = new THREE.ExtrudeGeometry(Hump, { depth: 0.036, bevelEnabled: true, bevelThickness: 0.004, bevelSize: 0.003, bevelSegments: 2 });
  HumpGeometry.translate(0, 0, -0.018);
  Part(G, HumpGeometry, M.Body, [0, 0.082, 0.004]);
  Part(G, RoundedBox(0.03, 0.02, 0.014, 0.004), M.Rubber, [0, 0.094, 0.028]);
  // Dials, shutter button and hot shoe.
  Part(G, new THREE.CylinderGeometry(0.011, 0.011, 0.008, 18), M.Satin, [-0.042, 0.088, 0.004]);
  Part(G, new THREE.CylinderGeometry(0.01, 0.01, 0.007, 18), M.Satin, [0.036, 0.088, 0.008]);
  Part(G, new THREE.CylinderGeometry(0.006, 0.0065, 0.006, 14), M.Chrome, [0.048, 0.086, -0.024]);
  Part(G, RoundedBox(0.02, 0.003, 0.018, 0.001, 1), M.Steel, [0, 0.108, 0.004]);
  // Back screen.
  Part(G, new THREE.PlaneGeometry(0.075, 0.05), M.Screen, [-0.01, 0.045, 0.0215]);
  // Mount and a ribbed zoom lens with a gold ring.
  const LensY = 0.043;
  Part(G, Tube(0.03, 0.006), M.Chrome, [0, LensY, -0.024]);
  Part(G, Turned([[0.001, 0], [0.029, 0], [0.031, 0.01], [0.031, 0.075], [0.035, 0.085], [0.035, 0.105], [0.001, 0.105]]), M.Body, [0, LensY, -0.027]);
  Part(G, Ribbed(0.0335, 0.028, 36, 0.07), M.Rubber, [0, LensY, -0.06]);
  Part(G, Ribbed(0.0345, 0.012, 36, 0.07), M.Rubber, [0, LensY, -0.105]);
  Part(G, new THREE.TorusGeometry(0.0312, 0.0012, 4, 30), M.Gold, [0, LensY, -0.08]);
  FrontGlass(G, M, 0.03, -0.132, LensY);
  return G;
}

// A fluid head on three legs, its plate at height Height, legs spread on the ground.
function Tripod(M, Height) {
  const G = new THREE.Group();
  Part(G, RoundedBox(0.09, 0.05, 0.1, 0.01), M.Body, [0, Height - 0.03, 0]);
  Part(G, Tube(0.008, 0.38, 10), M.Rubber, [0.03, Height - 0.06, 0.24], [0.42, 0, 0]);
  Part(G, new THREE.CylinderGeometry(0.055, 0.045, 0.05, 20), M.Satin, [0, Height - 0.08, 0]);
  const Spread = Math.max(0.3, Height * 0.38);
  for (let Leg = 0; Leg < 3; Leg++) {
    const Angle = (Leg / 3) * Math.PI * 2 + Math.PI / 6;
    const Foot = new THREE.Vector3(Math.cos(Angle) * Spread, 0, Math.sin(Angle) * Spread);
    const Top = new THREE.Vector3(Math.cos(Angle) * 0.04, Height - 0.1, Math.sin(Angle) * 0.04);
    const Length = Foot.distanceTo(Top);
    for (const [Radius, From, To, Material] of [[0.014, 0, 0.55, M.Steel], [0.01, 0.5, 1, M.Satin]]) {
      const Piece = Part(G, new THREE.CylinderGeometry(Radius, Radius, Length * (To - From), 10), Material);
      Piece.position.copy(Top.clone().lerp(Foot, (From + To) / 2));
      Piece.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), Top.clone().sub(Foot).normalize());
    }
    Part(G, new THREE.SphereGeometry(0.016, 10, 6), M.Rubber, [Foot.x, 0.012, Foot.z]);
    const Brace = Part(G, new THREE.CylinderGeometry(0.005, 0.005, Spread * 0.42, 6), M.Steel);
    const Inner = Top.clone().lerp(Foot, 0.62), Middle = new THREE.Vector3(0, Inner.y, 0);
    Brace.position.copy(Inner.clone().lerp(Middle, 0.5));
    Brace.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), Inner.clone().sub(Middle).normalize());
    Brace.scale.y = Inner.distanceTo(Middle) / (Spread * 0.42);
  }
  return G;
}

// One camera body as a group (looking down −z). With Stand, it sits on a tripod at Height metres.
// Stand: on a tripod at Height. Raised: no tripod, the body alone held at Height (a camera that moves, on a crane, rig or by hand).
export function CameraAsset(Body, { Stand = false, Raised = false, Height = 1.35 } = {}) {
  const M = Finishes();
  const Camera = Body === "digital" ? DigitalCamera(M) : Body === "mirrorless" ? MirrorlessCamera(M) : FilmCamera(M);
  Camera.name = "Camera body";
  if (!Stand && !Raised) return Camera;
  const G = new THREE.Group();
  Camera.position.y = Height;
  G.add(Camera);
  if (Stand) G.add(Tripod(M, Height));
  G.userData.body = Camera;
  return G;
}

// ── Icons and previews ──────────────────────────────────────────────────────────────────────────────────────────────────────────
// A soft studio to reflect in: two big softboxes and a floor bounce, prefiltered for the metal finishes.
export function StudioReflections(Renderer) {
  const Room = new THREE.Scene();
  Room.background = new THREE.Color("#2a2d33");
  const Panel = (Colour, Strength, [X, Y, Z], [W, H]) => {
    const Softbox = new THREE.Mesh(new THREE.PlaneGeometry(W, H), new THREE.MeshBasicMaterial({ color: new THREE.Color(Colour).multiplyScalar(Strength), side: THREE.DoubleSide }));
    Softbox.position.set(X, Y, Z);
    Softbox.lookAt(0, 0, 0);
    Room.add(Softbox);
  };
  Panel("#ffffff", 6, [4, 5, 3], [4, 3]);
  Panel("#cfe0ff", 3, [-5, 2, -2], [3, 4]);
  Panel("#ffe6cc", 1.5, [0, -4, 1], [8, 8]);
  Panel("#ffffff", 2, [0, 6, -4], [6, 2]);
  const Prefilter = new THREE.PMREMGenerator(Renderer);
  const Reflections = Prefilter.fromScene(Room, 0.03).texture;
  Prefilter.dispose();
  return Reflections;
}

let IconStudio = null;
// A transparent PNG of one camera body (three-quarter view from the front left, studio lit), as a data URL.
export const CameraIcon = (Body, Size = 96) => ModelIcon(CameraAsset(Body), Size);
// A transparent PNG of any model looking down −z (three-quarter view from the front left, studio lit), fitted to Width × Height, as a
// data URL. The model is disposed afterwards.
export function ModelIcon(Model, Size = 96, { Height = Size, Yaw = -140, Pitch = 18 } = {}) {
  if (!IconStudio) {
    const Canvas = document.createElement("canvas");
    const Renderer = new THREE.WebGLRenderer({ canvas: Canvas, antialias: true, alpha: true, preserveDrawingBuffer: true });
    Renderer.outputColorSpace = THREE.SRGBColorSpace;
    Renderer.toneMapping = THREE.ACESFilmicToneMapping;
    Renderer.toneMappingExposure = 1.45;
    const Scene = new THREE.Scene();
    Scene.environment = StudioReflections(Renderer);
    Scene.add(new THREE.HemisphereLight("#eef2ff", "#2a2a30", 1.1));
    const Key = new THREE.DirectionalLight("#ffffff", 2.4);
    Key.position.set(-3, 5, -4);
    const Rim = new THREE.DirectionalLight("#bcd4ff", 2.6);
    Rim.position.set(5, 3, 4);
    Scene.add(Key, Rim);
    IconStudio = { Canvas, Renderer, Scene, Camera: new THREE.PerspectiveCamera(24, 1, 0.01, 50) };
  }
  const { Canvas, Renderer, Scene, Camera } = IconStudio;
  Renderer.setPixelRatio(1);
  Renderer.setSize(Size, Height, false);
  Renderer.setClearColor(0x000000, 0);
  Camera.aspect = Size / Height;
  Scene.add(Model);
  Model.updateMatrixWorld(true);
  const Sphere = new THREE.Box3().setFromObject(Model).getBoundingSphere(new THREE.Sphere());
  const Distance = (Sphere.radius / Math.sin(THREE.MathUtils.degToRad(Camera.fov / 2))) * 0.98;
  Yaw = THREE.MathUtils.degToRad(Yaw);
  Pitch = THREE.MathUtils.degToRad(Pitch);
  Camera.position.copy(Sphere.center).add(new THREE.Vector3(Math.sin(Yaw) * Math.cos(Pitch), Math.sin(Pitch), Math.cos(Yaw) * Math.cos(Pitch)).multiplyScalar(Distance));
  Camera.near = Distance / 50;
  Camera.far = Distance * 4;
  Camera.lookAt(Sphere.center);
  Camera.clearViewOffset();
  Camera.updateProjectionMatrix();
  Camera.updateMatrixWorld(true);
  // Fit the model's projected outline to the square (6 % margin) rather than its bounding sphere, so small icons stay legible.
  const Low = new THREE.Vector2(Infinity, Infinity), High = new THREE.Vector2(-Infinity, -Infinity), Point = new THREE.Vector3();
  Model.traverse((Part) => {
    if (!Part.isMesh) return;
    const Positions = Part.geometry.attributes.position;
    for (let Index = 0; Index < Positions.count; Index++) {
      Point.fromBufferAttribute(Positions, Index).applyMatrix4(Part.matrixWorld).project(Camera);
      Low.min(Point);
      High.max(Point);
    }
  });
  // Projected extents in pixels of the full view; the window keeps the picture's aspect.
  const Wide = ((High.x - Low.x) / 2) * Size * 1.06, Tall = ((High.y - Low.y) / 2) * Height * 1.06;
  const Scale = Math.max(Wide / Size, Tall / Height);
  const Middle = [((Low.x + High.x) / 2 + 1) / 2 * Size, (1 - (Low.y + High.y) / 2) / 2 * Height];
  Camera.setViewOffset(Size, Height, Middle[0] - (Size * Scale) / 2, Middle[1] - (Height * Scale) / 2, Size * Scale, Height * Scale);
  Renderer.render(Scene, Camera);
  Camera.clearViewOffset();
  const Picture = Canvas.toDataURL("image/png");
  Scene.remove(Model);
  Model.traverse((Each) => {
    Each.geometry?.dispose();
    Each.material?.dispose?.();
  });
  return Picture;
}

// ── Studio look in the viewport ─────────────────────────────────────────────────────────────────────────────────────────────────
// A studio matcap (a lit sphere: a big soft key top left, a cool rim right, a warm bounce below), painted once on a 2D canvas. Assets
// drawn with it read as studio-lit metal and glass in any scene light, for one small shader (no environment bake, which costs seconds
// in a software renderer).
let Matcap = null;
function StudioMatcap() {
  if (Matcap) return Matcap;
  const Canvas = document.createElement("canvas");
  Canvas.width = Canvas.height = 128;
  const Brush = Canvas.getContext("2d");
  const Glow = (X, Y, Radius, Colour) => {
    const Paint = Brush.createRadialGradient(X, Y, 0, X, Y, Radius);
    Paint.addColorStop(0, Colour);
    Paint.addColorStop(1, "rgba(0,0,0,0)");
    Brush.fillStyle = Paint;
    Brush.fillRect(0, 0, 128, 128);
  };
  Brush.fillStyle = "#2b2e33";
  Brush.fillRect(0, 0, 128, 128);
  Glow(64, 64, 64, "rgba(70,74,82,1)");
  Glow(42, 34, 46, "rgba(235,238,245,0.95)");
  Glow(38, 30, 12, "rgba(255,255,255,1)");
  Glow(112, 70, 30, "rgba(150,185,255,0.7)");
  Glow(64, 124, 38, "rgba(255,214,170,0.35)");
  Brush.globalCompositeOperation = "destination-in";
  Brush.fillStyle = "#000";
  Brush.beginPath();
  Brush.arc(64, 64, 63, 0, Math.PI * 2);
  Brush.fill();
  Matcap = new THREE.CanvasTexture(Canvas);
  Matcap.colorSpace = THREE.SRGBColorSpace;
  return Matcap;
}
// Swaps an asset's materials for the studio matcap, keeping each part's colour (lifted so black finishes still show their form),
// opacity and emissive accents (lamps).
export function StudioLit(Asset) {
  const Swapped = new Map();
  Asset.traverse((Part) => {
    if (!Part.isMesh || !Part.material) return;
    const Before = Part.material;
    if (!Swapped.has(Before)) {
      const Glowing = Before.emissive && Before.emissive.getHex() !== 0 && Before.emissiveIntensity > 0;
      const Colour = Glowing ? Before.emissive.clone() : Before.color.clone().lerp(new THREE.Color("#ffffff"), 0.18 + 0.25 * (Before.metalness || 0)).multiplyScalar(1.6);
      Swapped.set(Before, new THREE.MeshMatcapMaterial({ color: Colour, matcap: StudioMatcap(), transparent: Before.transparent, opacity: Before.opacity }));
    }
    Part.material = Swapped.get(Before);
  });
  return Asset;
}

// For CaptureEditorAssetIcons.mjs, which saves the icons as PNG files for the build to ship.
if (typeof window !== "undefined") window.FrontierEditorAssets = { CameraIcon, CameraBodies, ModelIcon };
