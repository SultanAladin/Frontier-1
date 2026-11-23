//============================================================================================================================================
//                                                              VEHICLEASSETS.JS
//============================================================================================================================================
// 🚗 Level vehicles, parked in the 3D viewport, previewed in their inspector and captured as the outliner's icons
// (CaptureEditorAssetIcons.mjs).
//   • Controlled Vehicle: the vehicle the player drives, the Component Viewer's ControlVehicle from its own facets (ControlVehicleModel.js;
//     Follow-up 61 — it was a stand-in racer built here).
//   • Off-road Vehicle: a boxy 4×4 station wagon: upright glass, snorkel, roof rack with cargo, bull bar, fender flares, spare wheel on
//     the rear door, knobbly tyres.
//   • Pickup Truck: a double-cab pickup: open load bed with tailgate, sports bar, chrome grille, tow bar.
// The other two are built in code with three.js (no files), low-poly but true to life. How each is built: the body is a side profile (wheel arches cut in) extruded across the car's width with rounded edges; the cabin is
// a second profile in tinted glass, narrowing towards the roof, under a painted roof and pillars; wheels are turned (rounded tyre
// shoulders, knobbly tread where needed) around their own rims. Every vehicle looks down −z with +y up, in metres at real size, with
// its origin on the ground midway between the axles.

import { THREE } from "../Shared/Studio/Render.js";
import { ModelIcon } from "./EditorAssets.js";
import { ControlVehicleParts, ControlVehicleSheet } from "./ControlVehicleModel.js";

export const VehicleModels = [
  { Id: "controlled", Name: "Controlled Vehicle", Icon: "asset-vehicle-controlled", Caption: "ControlVehicle wedge coupé · the player's vehicle", Paint: "#c9d63a" },
  { Id: "offroad", Name: "Off-road Vehicle", Icon: "asset-vehicle-offroad", Caption: "Boxy 4×4 wagon · snorkel, roof rack, bull bar", Paint: "#bfa77a" },
  { Id: "pickup", Name: "Pickup Truck", Icon: "asset-vehicle-pickup", Caption: "Double-cab pickup · load bed, sports bar", Paint: "#e9eaec" },
];
export const VehiclePaints = [
  ["Acid lime", "#c9d63a"],
  ["Sunset orange", "#ef7a1f"],
  ["Sand", "#bfa77a"],
  ["Glacier white", "#e9eaec"],
  ["Rally red", "#b8262e"],
  ["Olive", "#5f6b47"],
  ["Night blue", "#22324f"],
  ["Graphite", "#3a3d42"],
];
export const VehicleModelOf = (Id) => VehicleModels.find((Each) => Each.Id === Id) || VehicleModels[0];

// ── Materials ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────
const Finish = (Colour, Roughness, Metalness, Extra = {}) => new THREE.MeshStandardMaterial({ color: new THREE.Color(Colour), roughness: Roughness, metalness: Metalness, ...Extra });
function Finishes(Paint) {
  return {
    Paint: Finish(Paint, 0.34, 0.2),
    Trim: Finish("#1d1f22", 0.7, 0.1),
    Plastic: Finish("#2a2c30", 0.82, 0),
    Rubber: Finish("#141516", 0.92, 0),
    Chrome: Finish("#c9cdd3", 0.18, 1),
    Steel: Finish("#7d828a", 0.42, 0.85),
    Alloy: Finish("#a7abb2", 0.3, 0.9),
    Glass: Finish("#1a2532", 0.08, 0.6, { emissive: new THREE.Color("#0b121b") }),
    Lamp: Finish("#fff6dc", 0.2, 0, { emissive: new THREE.Color("#fff1c8"), emissiveIntensity: 0.9 }),
    Tail: Finish("#b3121a", 0.3, 0, { emissive: new THREE.Color("#c4141c"), emissiveIntensity: 0.6 }),
    Amber: Finish("#e08a1a", 0.3, 0, { emissive: new THREE.Color("#e08a1a"), emissiveIntensity: 0.4 }),
    Cargo: Finish("#4d4a3f", 0.85, 0),
  };
}

// ── Shapes ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
function Part(Group, Geometry, Material, [X, Y, Z] = [0, 0, 0], [Rx, Ry, Rz] = [0, 0, 0]) {
  const Piece = new THREE.Mesh(Geometry, Material);
  Piece.position.set(X, Y, Z);
  Piece.rotation.set(Rx, Ry, Rz);
  Piece.castShadow = Piece.receiveShadow = true;
  Group.add(Piece);
  return Piece;
}
// A box with softly rounded edges, centred.
function Block(Width, Height, Depth, R = 0.02) {
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
  const Geometry = new THREE.ExtrudeGeometry(Outline, { depth: Math.max(1e-4, Depth - 2 * R), bevelEnabled: true, bevelThickness: R, bevelSize: R * 0.98, bevelSegments: 2, curveSegments: 2 });
  Geometry.translate(0, 0, -(Depth - 2 * R) / 2);
  Geometry.computeVertexNormals();
  return Geometry;
}
// A tube from A to B (points [x, y, z]).
function Rod(Group, Material, A, B, Radius = 0.025, Sides = 10) {
  const From = new THREE.Vector3(...A), To = new THREE.Vector3(...B);
  const Piece = Part(Group, new THREE.CylinderGeometry(Radius, Radius, From.distanceTo(To), Sides), Material);
  Piece.position.copy(From).add(To).multiplyScalar(0.5);
  Piece.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), To.clone().sub(From).normalize());
  return Piece;
}
// A side profile ([along, up] points, front = +along) extruded across Width with rounded edges (Round), turned so the front looks down
// −z and centred across x. Arches: [[along, up, radius] …] wheel arches cut into the bottom edge at Sill.
function Profiled(Points, Width, Round, { Arches = [], Sill = null } = {}) {
  const Outline = new THREE.Shape();
  Outline.moveTo(...Points[0]);
  for (const Point of Points.slice(1)) Outline.lineTo(...Point);
  if (Sill !== null) {
    // Points run rear-bottom → over the top → front-bottom; the bottom edge runs back to the start, over each arch (front first).
    for (const [Along, Up, Radius] of [...Arches].sort((A, B) => B[0] - A[0])) {
      const Start = Math.asin(Math.max(-1, Math.min(1, (Sill - Up) / Radius)));
      Outline.lineTo(Along + Radius * Math.cos(Start), Sill);
      Outline.absarc(Along, Up, Radius, Start, Math.PI - Start, false);
    }
  }
  Outline.closePath();
  const Depth = Math.max(0.01, Width - 2 * Round);
  const Geometry = new THREE.ExtrudeGeometry(Outline, { depth: Depth, bevelEnabled: Round > 0, bevelThickness: Round, bevelSize: Round * 0.9, bevelSegments: 3, curveSegments: 10 });
  Geometry.rotateY(Math.PI / 2);
  Geometry.translate(-Depth / 2, 0, 0);
  Geometry.computeVertexNormals();
  return Geometry;
}
// Narrows a part towards its top (tumblehome): x scales from 1 at From to Scale at To.
function Narrowed(Geometry, From, To, Scale) {
  const Points = Geometry.attributes.position;
  for (let Index = 0; Index < Points.count; Index++) {
    const T = Math.max(0, Math.min(1, (Points.getY(Index) - From) / (To - From)));
    Points.setX(Index, Points.getX(Index) * (1 + (Scale - 1) * T));
  }
  Geometry.computeVertexNormals();
  return Geometry;
}
// One wheel, axle along x: a turned tyre with rounded shoulders (Knobbly: a blocky tread), its rim and hub. Spokes: the rim's look.
function Wheel(M, Radius, Width, { Rim = 0.6, Knobbly = false, Spokes = 6, RimFinish = null } = {}) {
  const G = new THREE.Group();
  const Inner = Radius * Rim, H = Width / 2, S = Math.min(0.06, Width * 0.22);
  const Profile = [[Inner, -H * 0.92], [Radius - S, -H], [Radius - S * 0.25, -H + S * 0.6], [Radius, -H + S], [Radius, H - S], [Radius - S * 0.25, H - S * 0.6], [Radius - S, H], [Inner, H * 0.92]];
  const Tyre = new THREE.LatheGeometry(Profile.map(([R, Y]) => new THREE.Vector2(R, Y)), Knobbly ? 40 : 30);
  if (Knobbly) {
    // Tread blocks: every other segment of the tread stands proud.
    const Points = Tyre.attributes.position;
    for (let Index = 0; Index < Points.count; Index++) {
      const X = Points.getX(Index), Z = Points.getZ(Index), Out = Math.hypot(X, Z);
      if (Out < Radius - S * 0.3) continue;
      const Slot = Math.round(((Math.atan2(Z, X) + Math.PI) / (Math.PI * 2)) * 40);
      const Lift = Slot % 2 ? 1.035 : 1;
      Points.setXYZ(Index, X * Lift, Points.getY(Index), Z * Lift);
    }
    Tyre.computeVertexNormals();
  }
  Tyre.rotateZ(Math.PI / 2);
  Part(G, Tyre, M.Rubber);
  const Metal = RimFinish || M.Alloy;
  const Barrel = new THREE.CylinderGeometry(Inner, Inner, Width * 0.86, 28, 1, true);
  Barrel.rotateZ(Math.PI / 2);
  Part(G, Barrel, M.Plastic);
  // The face: a dished disc with spokes and a hub, on the outer side (+x; the wheel is mirrored for the left side).
  const Face = new THREE.CylinderGeometry(Inner * 0.98, Inner * 0.9, 0.03, 28);
  Face.rotateZ(Math.PI / 2);
  Part(G, Face, M.Plastic, [H * 0.62, 0, 0]);
  for (let Spoke = 0; Spoke < Spokes; Spoke++) {
    const Turn = (Spoke / Spokes) * Math.PI * 2;
    const Arm = Part(G, Block(0.035, Inner * 0.9, Inner * 0.2, 0.01), Metal, [H * 0.72, Math.sin(Turn) * Inner * 0.47, Math.cos(Turn) * Inner * 0.47]);
    Arm.rotation.x = -Turn + Math.PI / 2;
  }
  const Lip = new THREE.TorusGeometry(Inner * 0.97, 0.018, 6, 30);
  Lip.rotateY(Math.PI / 2);
  Part(G, Lip, Metal, [H * 0.74, 0, 0]);
  const Cap = new THREE.CylinderGeometry(Inner * 0.22, Inner * 0.26, 0.05, 16);
  Cap.rotateZ(Math.PI / 2);
  Part(G, Cap, Metal, [H * 0.76, 0, 0]);
  return G;
}
function Wheels(G, M, Spec, Options) {
  for (const Along of Spec.Axles)
    for (const Side of [1, -1]) {
      const Each = Wheel(M, Spec.WheelRadius, Spec.TyreWidth, Options);
      Each.position.set((Side * Spec.Track) / 2, Spec.WheelRadius, -Along);
      if (Side < 0) Each.rotation.y = Math.PI;
      G.add(Each);
    }
}
// The cabin: a glass profile narrowing to the roof, a painted roof skin over it and painted pillars (Pillars: along positions of the
// upright pillars, B and C; the A pillars follow the windscreen's edges).
function Cabin(G, M, { Points, Width, Narrow, Belt, Roof, Pillars = [], Round = 0.05, RoofMaterial = null }) {
  Part(G, Narrowed(Profiled(Points, Width, Round), Belt, Roof, Narrow), M.Glass);
  const [RearLow, RearHigh, FrontHigh, FrontLow] = Points;
  // The glass's rounded edges stand Round proud of its profile; the roof skin covers them.
  const Skin = 0.07, Over = Round + 0.012;
  const RoofShape = [
    [RearHigh[0] - 0.012, RearHigh[1] - Skin],
    [RearHigh[0] - 0.012, RearHigh[1] + Over],
    [FrontHigh[0] + 0.012, FrontHigh[1] + Over],
    [FrontHigh[0] + 0.012 + Skin * ((FrontLow[0] - FrontHigh[0]) / (FrontHigh[1] - FrontLow[1])), FrontHigh[1] - Skin],
  ];
  Part(G, Profiled(RoofShape, Width * Narrow + 0.04, 0.02), RoofMaterial || M.Paint);
  // Upright pillars: thin painted bands across the cabin (only their sides show through the glass).
  for (const Along of Pillars) {
    const Band = Narrowed(Profiled([[Along - 0.05, Belt], [Along - 0.05, Roof], [Along + 0.05, Roof], [Along + 0.05, Belt]], Width + 0.012, Round), Belt, Roof, Narrow);
    Part(G, Band, M.Paint);
  }
  // A pillars along the windscreen's two edges.
  const Half = (Width / 2) * 1.004;
  for (const Side of [1, -1])
    Rod(G, M.Paint, [Side * Half, FrontLow[1], -FrontLow[0]], [Side * Half * Narrow, FrontHigh[1], -FrontHigh[0]], 0.035, 8);
}
function Mirrors(G, M, Along, Up, Half) {
  for (const Side of [1, -1]) {
    Rod(G, M.Trim, [Side * (Half - 0.02), Up, -Along], [Side * (Half + 0.12), Up + 0.04, -Along - 0.04], 0.015, 6);
    Part(G, Block(0.06, 0.13, 0.19, 0.025), M.Trim, [Side * (Half + 0.16), Up + 0.06, -Along - 0.05]);
  }
}

// ── Vehicles ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Controlled Vehicle: the ControlVehicle from its own facets (ControlVehicleModel.js), turned to look down −z with its origin midway
// between the axles like the others.
function ControlVehicle(Paint) {
  const G = new THREE.Group();
  const { Root } = ControlVehicleParts(Paint);
  Root.position.x = -ControlVehicleSheet.Middle;
  const Turned = new THREE.Group();
  Turned.rotation.y = Math.PI / 2;
  Turned.add(Root);
  G.add(Turned);
  return G;
}

function OffRoader(M) {
  const G = new THREE.Group();
  const Spec = { Axles: [1.36, -1.37], Track: 1.6, WheelRadius: 0.4, TyreWidth: 0.27 };
  const Sill = 0.55;
  const Body = [[-2.28, Sill], [-2.3, 1.22], [-2.24, 1.25], [1.08, 1.24], [1.18, 1.21], [2.2, 1.14], [2.3, 1.06], [2.33, 0.62], [2.29, Sill]];
  Part(G, Profiled(Body, 1.88, 0.05, { Sill, Arches: [[1.36, 0.4, 0.47], [-1.37, 0.4, 0.47]] }), M.Paint);
  Cabin(G, M, { Points: [[-2.27, 1.24], [-2.23, 2.0], [0.82, 2.02], [1.12, 1.24]], Width: 1.84, Narrow: 0.95, Belt: 1.24, Roof: 2.02, Pillars: [-0.12, -1.25] });
  // Fender flares over the arches.
  for (const Along of Spec.Axles)
    for (const Side of [1, -1]) {
      const Flare = new THREE.TorusGeometry(0.5, 0.05, 4, 16, Math.PI);
      Flare.rotateY(Math.PI / 2);
      Part(G, Flare, M.Plastic, [Side * 0.95, 0.4, -Along]);
    }
  // Roof rack: a frame of rails with cargo cases and a jerry can.
  const RackY = 2.12;
  for (const Side of [1, -1]) {
    Rod(G, M.Trim, [Side * 0.8, RackY, -0.75], [Side * 0.8, RackY, 2.1], 0.022);
    for (const Along of [-0.7, 0.6, 2.05]) Rod(G, M.Trim, [Side * 0.8, 2.03, Along], [Side * 0.8, RackY, Along], 0.02);
  }
  for (const Along of [-0.7, -0.05, 0.6, 1.3, 2.05]) Rod(G, M.Trim, [-0.8, RackY, Along], [0.8, RackY, Along], 0.02);
  Part(G, Block(0.9, 0.26, 0.6, 0.03), M.Cargo, [-0.2, RackY + 0.15, 1.25]);
  Part(G, Block(0.5, 0.2, 0.42, 0.03), M.Plastic, [0.35, RackY + 0.12, 0.35]);
  Part(G, Block(0.18, 0.34, 0.3, 0.03), Finish("#5a6b3a", 0.6, 0.1), [0.55, RackY + 0.18, 1.35]);
  // Snorkel up the right A pillar.
  Rod(G, M.Plastic, [0.97, 1.0, -1.15], [0.97, 1.3, -1.15], 0.05);
  Rod(G, M.Plastic, [0.97, 1.3, -1.15], [0.93, 1.95, -0.86], 0.05);
  Part(G, Block(0.13, 0.13, 0.2, 0.04), M.Plastic, [0.93, 2.0, -0.92]);
  // Steel bumper with a bull bar; grille between the headlamps.
  Part(G, Block(1.92, 0.22, 0.2, 0.04), M.Steel, [0, 0.6, -2.38]);
  for (const Side of [1, -1]) Rod(G, M.Steel, [Side * 0.45, 0.68, -2.44], [Side * 0.38, 1.12, -2.38], 0.035);
  Rod(G, M.Steel, [-0.38, 1.12, -2.38], [0.38, 1.12, -2.38], 0.035);
  Part(G, Block(1.0, 0.3, 0.04, 0.02), M.Trim, [0, 0.92, -2.34]);
  for (let Slat = 0; Slat < 4; Slat++) Part(G, Block(0.95, 0.025, 0.02, 0.008), M.Steel, [0, 0.82 + Slat * 0.07, -2.36]);
  for (const Side of [1, -1]) Part(G, new THREE.CylinderGeometry(0.09, 0.09, 0.05, 18).rotateX(Math.PI / 2), M.Lamp, [Side * 0.68, 0.94, -2.34]);
  Part(G, Block(1.92, 0.2, 0.16, 0.04), M.Steel, [0, 0.6, 2.33]);
  for (const Side of [1, -1]) Part(G, Block(0.1, 0.24, 0.05, 0.015), M.Tail, [Side * 0.84, 0.98, 2.31]);
  // Spare wheel on the rear door.
  const Spare = Wheel(M, 0.4, 0.26, { Knobbly: true, Spokes: 5, RimFinish: M.Steel, Rim: 0.55 });
  Spare.rotation.y = -Math.PI / 2;
  Spare.position.set(0.15, 1.2, 2.47);
  G.add(Spare);
  // Side steps.
  for (const Side of [1, -1]) Part(G, Block(0.12, 0.05, 1.2, 0.02), M.Plastic, [Side * 0.98, 0.48, 0]);
  Mirrors(G, M, 1.0, 1.35, 0.92);
  Wheels(G, M, Spec, { Knobbly: true, Spokes: 5, Rim: 0.55, RimFinish: M.Steel });
  return G;
}
// Pickup Truck: a double cab with an open load bed.
function Pickup(M) {
  const G = new THREE.Group();
  const Spec = { Axles: [1.76, -1.32], Track: 1.56, WheelRadius: 0.38, TyreWidth: 0.26 };
  const Sill = 0.52, Bed = 0.9, Rail = 1.16, Back = -2.66, Cab = -0.96;
  const Body = [[Back, Sill], [Back - 0.02, Bed], [Cab, Bed], [Cab, Rail], [0.98, 1.16], [1.12, 1.13], [2.44, 1.04], [2.64, 0.92], [2.68, 0.6], [2.64, Sill]];
  Part(G, Profiled(Body, 1.85, 0.05, { Sill, Arches: [[1.76, 0.38, 0.45], [-1.32, 0.38, 0.45]] }), M.Paint);
  // Load bed: side walls with capped rails, the front wall and a tailgate.
  const Length = Cab - Back;
  for (const Side of [1, -1]) {
    Part(G, Block(0.07, Rail - Bed + 0.02, Length, 0.025), M.Paint, [Side * 0.89, (Rail + Bed) / 2, -(Back + Cab) / 2]);
    Part(G, Block(0.1, 0.03, Length, 0.012), M.Trim, [Side * 0.89, Rail + 0.012, -(Back + Cab) / 2]);
  }
  Part(G, Block(1.85, Rail - Bed + 0.02, 0.07, 0.025), M.Paint, [0, (Rail + Bed) / 2, -Back - 0.01]);
  Part(G, Block(1.72, 0.012, Length - 0.06, 0.004), M.Plastic, [0, Bed + 0.006, -(Back + Cab) / 2]);
  Part(G, Block(0.5, 0.06, 0.02, 0.01), M.Trim, [0, Rail - 0.05, -Back + 0.03]);
  // Sports bar behind the cab.
  for (const Side of [1, -1]) {
    Rod(G, M.Chrome, [Side * 0.82, Rail, -(Cab - 0.25)], [Side * 0.72, 1.68, -(Cab - 0.1)], 0.035);
    Rod(G, M.Chrome, [Side * 0.82, Rail, -(Cab - 0.25)], [Side * 0.82, Rail + 0.25, -(Cab - 0.6)], 0.03);
  }
  Rod(G, M.Chrome, [-0.72, 1.68, -(Cab - 0.1)], [0.72, 1.68, -(Cab - 0.1)], 0.035);
  Cabin(G, M, { Points: [[-0.94, 1.15], [-0.9, 1.82], [0.42, 1.85], [1.06, 1.15]], Width: 1.78, Narrow: 0.88, Belt: 1.15, Roof: 1.85, Pillars: [-0.2] });
  // Chrome grille and bumper, fog lamps, tow bar.
  Part(G, Block(1.1, 0.34, 0.05, 0.04), M.Chrome, [0, 0.86, -2.66]);
  Part(G, Block(1.0, 0.26, 0.03, 0.03), M.Trim, [0, 0.86, -2.685]);
  for (let Slat = 0; Slat < 3; Slat++) Part(G, Block(0.98, 0.02, 0.02, 0.006), M.Chrome, [0, 0.79 + Slat * 0.075, -2.7]);
  Part(G, Block(1.86, 0.2, 0.16, 0.05), M.Paint, [0, 0.56, -2.62]);
  Part(G, Block(1.4, 0.08, 0.06, 0.02), M.Plastic, [0, 0.47, -2.67]);
  for (const Side of [1, -1]) Part(G, new THREE.CylinderGeometry(0.045, 0.045, 0.03, 14).rotateX(Math.PI / 2), M.Lamp, [Side * 0.7, 0.56, -2.71]);
  Part(G, Block(1.86, 0.14, 0.14, 0.04), M.Steel, [0, 0.58, 2.69]);
  Rod(G, M.Steel, [0, 0.5, 2.7], [0, 0.5, 2.95], 0.03);
  Part(G, new THREE.SphereGeometry(0.035, 10, 8), M.Chrome, [0, 0.55, 2.95]);
  Mirrors(G, M, 0.92, 1.25, 0.88);
  Wheels(G, M, Spec, { Spokes: 6, Rim: 0.62, RimFinish: M.Alloy });
  return G;
}

// One vehicle as a group (looking down −z, resting on the ground) in its paint.
export function VehicleAsset(Id, Paint = null) {
  const Model = VehicleModelOf(Id);
  const Colour = /^#[\da-f]{6}$/i.test(Paint || "") ? Paint : Model.Paint;
  const G = Model.Id === "offroad" ? OffRoader(Finishes(Colour)) : Model.Id === "pickup" ? Pickup(Finishes(Colour)) : ControlVehicle(Colour);
  G.name = Model.Name;
  return G;
}
// The headline numbers each inspector shows (metres, kilograms).
export const VehicleFacts = {
  controlled: { Length: 6.22, Width: 2.5, Height: 1.77, Wheelbase: 3.4, Track: 2.1, Clearance: 0.31, Mass: 1300, Seats: 2, Drive: "6-speed · 520 N·m peak" },
  offroad: { Length: 4.8, Width: 1.96, Height: 2.3, Wheelbase: 2.73, Track: 1.6, Clearance: 0.27, Mass: 2400, Seats: 5, Drive: "4×4 · low range" },
  pickup: { Length: 5.68, Width: 1.86, Height: 1.85, Wheelbase: 3.08, Track: 1.56, Clearance: 0.29, Mass: 2100, Seats: 5, Drive: "4×4 · 6-speed auto" },
};

// For CaptureEditorAssetIcons.mjs: each vehicle's outliner icon (three-quarter front view, studio lit).
export const VehicleIcon = (Id, Size = 96) => ModelIcon(VehicleAsset(Id), Size, { Yaw: -128, Pitch: 16 });
if (typeof window !== "undefined") window.FrontierEditorAssets = { ...(window.FrontierEditorAssets || {}), VehicleIcon, VehicleModels };
