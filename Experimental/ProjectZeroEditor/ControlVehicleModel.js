//============================================================================================================================================
//                                                           CONTROLVEHICLEMODEL.JS
//============================================================================================================================================
// 🚗 The Controlled Vehicle's model: the ControlVehicle from its own facets (Experimental/Shared/Models: body, tyre, rim; the Component
// Viewer's BP_ControlVehicle and the cabin HMI lobby use the same files), shipped inside the editor (Build.mjs, NativeAssets.Facets) and
// put together here without waiting on a download.
// Axes: +x forward, +y up, +z to the right (authored facets are z-up, +x forward, +y left, converted (x, y, z) → (x, z, −y) as in
// CabinHmi/Shared/VehicleAssembly.js); metres; the ground at y = 0; x = 0 at the middle of the body. Places measured from the facets
// (ControlVehicleSheet) are what the Component Viewer puts each component on.

import { THREE } from "../Shared/Studio/Render.js";
import { readFacet } from "../Shared/GeometryCompile/Engine/ImportFormats.js";
import { cornerNormals } from "../Shared/GeometryCompile/Engine/MeshAnalysis.js";
import { VEHICLE_SHEET } from "../ComponentViewer/Blueprint.js";

// CourseMaterial families in the facets.
export const Family = { BodyCoat: 6, Glass: 7, Plastic: 8, Tyre: 9, Cap: 10, Brake: 11, Trim: 12, Headlight: 13, Taillight: 14, Default: 15 };

const TyreRadius = 0.509;
const GroundLift = TyreRadius - VEHICLE_SHEET.axleMount[0][2]; // authored z of the ground is −GroundLift
// Measured from the facets: body 6.22 × 2.50 m, roof 1.77 m, belly 0.31 m off the ground; the headlamp band across the nose and the
// tail-lamp band across the tail; the axles from the Vehicle Lab sheet.
export const ControlVehicleSheet = {
  Length: 6.22,
  Width: 2.5,
  Height: 1.77,
  Clearance: 0.31,
  TyreRadius,
  AxleHeight: TyreRadius,
  FrontAxle: VEHICLE_SHEET.axleMount[0][0],
  RearAxle: VEHICLE_SHEET.axleMount[2][0],
  Track: VEHICLE_SHEET.axleMount[0][1] * 2,
  Middle: (VEHICLE_SHEET.axleMount[0][0] + VEHICLE_SHEET.axleMount[2][0]) / 2, // the wheelbase centre, where socket mounts count from
  Headlamps: [[3.05, 0.96, -0.78], [3.05, 0.96, 0.78]],
  Taillamps: [[-3.06, 1.09, -0.82], [-3.06, 1.09, 0.82]],
};

// Non-indexed y-up surface with angle-split normals and one draw group per family.
function Surface(Facet, Mode, Angle) {
  const Normals = cornerNormals(Facet.positions, Facet.indices, Mode, Angle);
  const Count = Facet.indices.length / 3;
  const Order = Array.from({ length: Count }, (_, Index) => Index).sort((A, B) => Facet.families[A] - Facet.families[B] || A - B);
  const Position = new Float32Array(Count * 9), Normal = new Float32Array(Count * 9);
  const Shape = new THREE.BufferGeometry();
  const Families = [];
  let Start = 0, Current = Facet.families[Order[0]];
  for (let Slot = 0; Slot < Count; Slot++) {
    const Triangle = Order[Slot], Id = Facet.families[Triangle];
    if (Id !== Current) {
      Shape.addGroup(Start * 3, (Slot - Start) * 3, Families.length);
      Families.push(Current);
      Start = Slot;
      Current = Id;
    }
    for (let Corner = 0; Corner < 3; Corner++) {
      const Vertex = Facet.indices[Triangle * 3 + Corner] * 3, Out = Slot * 9 + Corner * 3, In = Triangle * 9 + Corner * 3;
      Position[Out] = Facet.positions[Vertex];
      Position[Out + 1] = Facet.positions[Vertex + 2];
      Position[Out + 2] = -Facet.positions[Vertex + 1];
      Normal[Out] = Normals[In];
      Normal[Out + 1] = Normals[In + 2];
      Normal[Out + 2] = -Normals[In + 1];
    }
  }
  Shape.addGroup(Start * 3, (Count - Start) * 3, Families.length);
  Families.push(Current);
  Shape.setAttribute("position", new THREE.BufferAttribute(Position, 3));
  Shape.setAttribute("normal", new THREE.BufferAttribute(Normal, 3));
  Shape.computeBoundingSphere();
  Shape.computeBoundingBox();
  return { Shape, Families };
}

// Decoded once; three.js uploads a geometry again after a dispose, so sharing these between models is safe.
let Surfaces = null;
export function ControlVehicleReady() {
  return !!window.NativeAssets?.Facets?.Body;
}
function Decoded() {
  if (Surfaces) return Surfaces;
  const Bytes = (Name) => Uint8Array.from(atob(window.NativeAssets.Facets[Name]), (Character) => Character.charCodeAt(0)).buffer;
  Surfaces = {
    Body: Surface(readFacet(Bytes("Body")), "angle", 34),
    Tyre: Surface(readFacet(Bytes("Tyre")), "smooth", 40),
    Rim: Surface(readFacet(Bytes("Rim")), "angle", 40),
  };
  return Surfaces;
}

// One material per family (fresh per model, so a paint or a highlight on one never reaches another).
export function ControlVehicleFinish(Paint = "#c9d63a") {
  const Standard = (Colour, Roughness, Metalness, Extra = {}) => new THREE.MeshStandardMaterial({ color: new THREE.Color(Colour), roughness: Roughness, metalness: Metalness, ...Extra });
  return {
    [Family.BodyCoat]: Standard(Paint, 0.3, 0.32),
    [Family.Glass]: Standard("#07090c", 0.06, 0.4),
    [Family.Plastic]: Standard("#121317", 0.55, 0.1),
    [Family.Tyre]: Standard("#161616", 0.86, 0),
    [Family.Cap]: Standard("#8d939b", 0.24, 0.9),
    [Family.Brake]: Standard("#7a1f0b", 0.3, 0.8),
    [Family.Trim]: Standard("#d4d6da", 0.35, 0.6),
    [Family.Headlight]: Standard("#ffffff", 0.1, 0, { emissive: new THREE.Color("#ffffff"), emissiveIntensity: 1.2 }),
    [Family.Taillight]: Standard("#ff2a2a", 0.1, 0, { emissive: new THREE.Color("#ff1010"), emissiveIntensity: 1.4 }),
    [Family.Default]: Standard("#2a2c30", 0.5, 0.2),
  };
}

const Finished = ({ Shape, Families }, Finish) => {
  const Part = new THREE.Mesh(Shape, Families.map((Id) => Finish[Id] || Finish[Family.Default]));
  Part.castShadow = true;
  Part.receiveShadow = true;
  return Part;
};

// The ControlVehicle in its paint: { Root, Body, Wheels: [{ Steer, Lift, Spin, Size, Front, Left, Mount }], Finish }. Each wheel hangs
// Steer (turns about y) → Lift (rides up and down) → Size (scales with the tyre radius) → Spin (rolls about z) so the Component Viewer
// can move them; the level leaves them as built.
export function ControlVehicleParts(Paint) {
  const { Body: BodySurface, Tyre, Rim } = Decoded();
  const Finish = ControlVehicleFinish(Paint);
  const Root = new THREE.Group();
  const Body = Finished(BodySurface, Finish);
  Body.position.y = GroundLift;
  Root.add(Body);
  const Wheels = VEHICLE_SHEET.axleMount.map(([X, Y, Z]) => {
    const Steer = new THREE.Group();
    Steer.position.set(X, Z + GroundLift, -Y);
    const Lift = new THREE.Group(), Size = new THREE.Group(), Spin = new THREE.Group(), Face = new THREE.Group();
    Face.rotation.y = Y < 0 ? Math.PI : 0; // right-hand wheels face outward (BP_ControlVehicle)
    Face.add(Finished(Tyre, Finish), Finished(Rim, Finish));
    Spin.add(Face);
    Size.add(Spin);
    Lift.add(Size);
    Steer.add(Lift);
    Root.add(Steer);
    return { Steer, Lift, Size, Spin, Front: X > 0, Left: Y > 0, Mount: Steer.position.clone() };
  });
  return { Root, Body, Wheels, Finish };
}
