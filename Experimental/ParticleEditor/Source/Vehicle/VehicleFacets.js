// VEHICLE FACETS
// 🧩 Builds the ControlVehicle (body, four tyres, four rims, optional rear wing) as one three.js group for the particle
//    editor, from the three FCT1 facets in ../../Models. Standalone copy of Experimental/CabinHmi/Shared/VehicleAssembly.js
//    with its three outside dependencies brought in: the facet reader (GeometryCompile ImportFormats readFacet), the
//    angle-split normals (GeometryCompile MeshAnalysis cornerNormals) and the axle mounts (ComponentViewer VEHICLE_SHEET).
//
// 📝 Authored facets are Z-up, +X forward, +Y left, metres. Everything here is converted to Y-up on read:
//    (x, y, z) → (x, z, −y). Ground sits at y = 0.

import * as THREE from '../../Vendor/three.esm.min.js';

export { THREE };

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const ModelRoot   = new URL('../../Models/', import.meta.url);
const AxleHeight  = 0.0914;                                       // [m] - authored axle z
const TyreRadius  = 0.509;                                        // [m] - from the tyre facet bounds
const GroundLift  = TyreRadius - AxleHeight;                      // [m] - authored z of the ground is −GroundLift
const AxleMounts  = [[1.7274, 1.0475, 0.0914], [1.7274, -1.0475, 0.0914], [-1.6686, 1.0475, 0.0914], [-1.6686, -1.0475, 0.0914]]; // [m] - authored, Z-up
const WingMount   = [-2.9822, 0.8367, 0];                         // [m] - RearWingSocket location (Y-up, body local)

// CourseMaterial families present in the facets.
export const Family = { BodyCoat: 6, Glass: 7, Plastic: 8, Tyre: 9, Hub: 10, Brake: 11, Trim: 12, Headlight: 13, Taillight: 14, Default: 15 };

//------------------------------------------------------------------------------------------------------------------------
//                                                FACET STREAM AND NORMALS
//------------------------------------------------------------------------------------------------------------------------

// FCT1 stream: 'FCT1', vertex count, facet count, flags, 32 reserved bytes, then positions, indices, families (+ optional extras).
function readFacet(bytes) {
  const view = new DataView(bytes);
  const magic = String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
  if (magic !== 'FCT1') throw new Error(`Not a facet stream (magic '${magic}')`);
  const vertexCount = view.getUint32(4, true), facetCount = view.getUint32(8, true), flags = view.getUint32(12, true);
  let at = 48;
  const positions = new Float32Array(bytes.slice(at, at + vertexCount * 12)); at += vertexCount * 12;
  const indices = new Uint32Array(bytes.slice(at, at + facetCount * 12)); at += facetCount * 12;
  const families = new Uint32Array(bytes.slice(at, at + facetCount * 4)); at += facetCount * 4;
  if (flags & 1) at += facetCount * 36;
  if (at !== bytes.byteLength) throw new Error(`Facet stream length ${bytes.byteLength} disagrees with its header (${at})`);
  return { positions, indices, families, format: 'FCT1 facet', notes: ['Authored Z-up, +X forward, metres', 'CourseMaterial families kept as material slots'] };
}

// Per-corner normals, nine floats per triangle.
//   'flat'   facet normal on every corner
//   'smooth' area-weighted vertex normal
//   'angle'  area-weighted, but only across neighbours within `angle` degrees of the facet (hard edges stay hard)
function cornerNormals(positions, indices, mode = 'angle', angle = 40) {
  const triangles = indices.length / 3;
  const face = new Float32Array(triangles * 3); // area-weighted (unnormalised) facet normals
  for (let t = 0; t < triangles; t++) {
    const a = indices[t * 3] * 3, b = indices[t * 3 + 1] * 3, c = indices[t * 3 + 2] * 3;
    const ux = positions[b] - positions[a], uy = positions[b + 1] - positions[a + 1], uz = positions[b + 2] - positions[a + 2];
    const vx = positions[c] - positions[a], vy = positions[c + 1] - positions[a + 1], vz = positions[c + 2] - positions[a + 2];
    face[t * 3] = uy * vz - uz * vy; face[t * 3 + 1] = uz * vx - ux * vz; face[t * 3 + 2] = ux * vy - uy * vx;
  }
  const out = new Float32Array(triangles * 9);
  const unit = (x, y, z, o) => { const l = Math.hypot(x, y, z) || 1; out[o] = x / l; out[o + 1] = y / l; out[o + 2] = z / l; };
  if (mode === 'flat') {
    for (let t = 0; t < triangles; t++) for (let k = 0; k < 3; k++) unit(face[t * 3], face[t * 3 + 1], face[t * 3 + 2], t * 9 + k * 3);
    return out;
  }
  const vertexCount = positions.length / 3;
  if (mode === 'smooth') {
    const acc = new Float32Array(vertexCount * 3);
    for (let t = 0; t < triangles; t++) for (let k = 0; k < 3; k++) {
      const v = indices[t * 3 + k] * 3;
      acc[v] += face[t * 3]; acc[v + 1] += face[t * 3 + 1]; acc[v + 2] += face[t * 3 + 2];
    }
    for (let t = 0; t < triangles; t++) for (let k = 0; k < 3; k++) {
      const v = indices[t * 3 + k] * 3;
      unit(acc[v], acc[v + 1], acc[v + 2], t * 9 + k * 3);
    }
    return out;
  }
  // Angle split: vertex → incident triangles (CSR).
  const start = new Uint32Array(vertexCount + 1);
  for (let i = 0; i < indices.length; i++) start[indices[i] + 1]++;
  for (let v = 0; v < vertexCount; v++) start[v + 1] += start[v];
  const fill = start.slice(0, vertexCount);
  const incident = new Uint32Array(indices.length);
  for (let i = 0; i < indices.length; i++) incident[fill[indices[i]]++] = (i / 3) | 0;
  const limit = Math.cos((angle * Math.PI) / 180);
  const len = new Float32Array(triangles);
  for (let t = 0; t < triangles; t++) len[t] = Math.hypot(face[t * 3], face[t * 3 + 1], face[t * 3 + 2]) || 1;
  for (let t = 0; t < triangles; t++) {
    const fx = face[t * 3] / len[t], fy = face[t * 3 + 1] / len[t], fz = face[t * 3 + 2] / len[t];
    for (let k = 0; k < 3; k++) {
      const v = indices[t * 3 + k];
      let x = 0, y = 0, z = 0;
      for (let i = start[v]; i < start[v + 1]; i++) {
        const u = incident[i];
        const dot = (face[u * 3] * fx + face[u * 3 + 1] * fy + face[u * 3 + 2] * fz) / len[u];
        if (u === t || dot >= limit) { x += face[u * 3]; y += face[u * 3 + 1]; z += face[u * 3 + 2]; }
      }
      unit(x, y, z, t * 9 + k * 3);
    }
  }
  return out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FACET DECODING
//------------------------------------------------------------------------------------------------------------------------

const FacetCache = new Map();

async function DecodeFacet(File)
{
    if (!FacetCache.has(File))
    {
        FacetCache.set(File, fetch(new URL(File, ModelRoot)).then((Reply) => Reply.arrayBuffer()).then(readFacet));
    }
    return FacetCache.get(File);
}

// 📝 Non-indexed Y-up geometry with angle-split normals and one draw group per family (sorted by family).
function SurfaceFromFacet(Facet, NormalMode = 'angle', SplitAngle = 38)
{
    const Normals   = cornerNormals(Facet.positions, Facet.indices, NormalMode, SplitAngle);
    const Count     = Facet.indices.length / 3;
    const Order     = Array.from({ length: Count }, (_, Index) => Index).sort((A, B) => Facet.families[A] - Facet.families[B] || A - B);
    const Position  = new Float32Array(Count * 9);
    const Normal    = new Float32Array(Count * 9);
    const Surface   = new THREE.BufferGeometry();
    const Families  = [];
    let GroupStart  = 0;
    let Current     = Facet.families[Order[0]];

    for (let Slot = 0; Slot < Count; Slot++)
    {
        const Triangle = Order[Slot];
        const FamilyId = Facet.families[Triangle];
        if (FamilyId !== Current)
        {
            Surface.addGroup(GroupStart * 3, (Slot - GroupStart) * 3, Families.length);
            Families.push(Current);
            GroupStart = Slot;
            Current    = FamilyId;
        }
        for (let Corner = 0; Corner < 3; Corner++)
        {
            const Vertex = Facet.indices[Triangle * 3 + Corner] * 3;
            const Out    = Slot * 9 + Corner * 3;
            const In     = Triangle * 9 + Corner * 3;
            Position[Out]     = Facet.positions[Vertex];
            Position[Out + 1] = Facet.positions[Vertex + 2];
            Position[Out + 2] = -Facet.positions[Vertex + 1];
            Normal[Out]       = Normals[In];
            Normal[Out + 1]   = Normals[In + 2];
            Normal[Out + 2]   = -Normals[In + 1];
        }
    }
    Surface.addGroup(GroupStart * 3, (Count - GroupStart) * 3, Families.length);
    Families.push(Current);
    Surface.setAttribute('position', new THREE.BufferAttribute(Position, 3));
    Surface.setAttribute('normal', new THREE.BufferAttribute(Normal, 3));
    Surface.computeBoundingSphere();
    return { Surface, Families };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    REAR WING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Copied from Experimental/ComponentViewer/Main.js buildWing(): boxes in Y-up, origin at the mount, 8° angle of
//    attack, endplates flush with the body sides. Returns the same { positions, indices, families } layout as a facet
//    but already Y-up, so it is converted back to Z-up for SurfaceFromFacet.
function ConstructWingFacet()
{
    const Positions = [], Indices = [], Families = [];
    const Box = (Centre, Half, FamilyId, Tilt = 0) =>
    {
        const First = Positions.length / 3, Cos = Math.cos(Tilt), Sin = Math.sin(Tilt);
        for (let Corner = 0; Corner < 8; Corner++)
        {
            const X = (Corner & 1 ? 1 : -1) * Half[0], Y = (Corner & 2 ? 1 : -1) * Half[1], Z = (Corner & 4 ? 1 : -1) * Half[2];
            const Px = Centre[0] + X * Cos - Y * Sin, Py = Centre[1] + X * Sin + Y * Cos, Pz = Centre[2] + Z;
            Positions.push(Px, -Pz, Py);                          // Y-up → authored Z-up
        }
        for (const [A, B, C, D] of [[0, 2, 3, 1], [4, 5, 7, 6], [0, 1, 5, 4], [2, 6, 7, 3], [0, 4, 6, 2], [1, 3, 7, 5]])
        {
            Indices.push(First + A, First + B, First + C, First + A, First + C, First + D);
            Families.push(FamilyId, FamilyId);
        }
    };
    const Tilt    = -8 * Math.PI / 180;
    const PlateZ  = 1.2498 - 0.008;
    const Span    = PlateZ - 0.008;
    Box([0, 0.25, 0], [0.17, 0.016, Span], Family.Plastic, Tilt);
    const Trailing = [-0.17 * Math.cos(Tilt) - 0.016 * Math.sin(Tilt), 0.25 - 0.17 * Math.sin(Tilt) + 0.016 * Math.cos(Tilt)];
    Box([Trailing[0] + 0.008 * Math.cos(Tilt) - 0.022 * Math.sin(Tilt), Trailing[1] + 0.008 * Math.sin(Tilt) + 0.022 * Math.cos(Tilt), 0], [0.008, 0.022, Span], Family.BodyCoat, Tilt);
    for (const Z of [-PlateZ, PlateZ]) Box([-0.01, 0.23, Z], [0.2, 0.1, 0.008], Family.BodyCoat);
    for (const Z of [-0.56, 0.56]) Box([0.02, 0.11, Z], [0.05, 0.11, 0.01], Family.Hub);
    return { positions: new Float32Array(Positions), indices: new Uint32Array(Indices), families: new Uint32Array(Families) };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATERIALS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One material per family, shared by every part so a palette change reaches body, wing and rims at once.
export function DefaultFinish(Overrides = {})
{
    const Finish = {
        [Family.BodyCoat]:  new THREE.MeshPhysicalMaterial({ color: '#c9d63a', roughness: 0.28, metalness: 0.35, clearcoat: 1, clearcoatRoughness: 0.06 }),
        [Family.Glass]:     new THREE.MeshPhysicalMaterial({ color: '#06080a', roughness: 0.04, metalness: 0.2, clearcoat: 1 }),
        [Family.Plastic]:   new THREE.MeshStandardMaterial({ color: '#0c0d10', roughness: 0.55, metalness: 0.1 }),
        [Family.Tyre]:      new THREE.MeshStandardMaterial({ color: '#141414', roughness: 0.86, metalness: 0 }),
        [Family.Hub]:       new THREE.MeshStandardMaterial({ color: '#8d939b', roughness: 0.22, metalness: 1, emissive: '#000000' }),
        [Family.Brake]:     new THREE.MeshStandardMaterial({ color: '#7a1f0b', roughness: 0.3, metalness: 0.8 }),
        [Family.Trim]:      new THREE.MeshStandardMaterial({ color: '#d4d6da', roughness: 0.35, metalness: 0.6 }),
        [Family.Headlight]: new THREE.MeshStandardMaterial({ color: '#ffffff', roughness: 0.1, emissive: '#ffffff', emissiveIntensity: 2.2 }),
        [Family.Taillight]: new THREE.MeshStandardMaterial({ color: '#ff2a2a', roughness: 0.1, emissive: '#ff1010', emissiveIntensity: 2.4 }),
        [Family.Default]:   new THREE.MeshStandardMaterial({ color: '#2a2c30', roughness: 0.5, metalness: 0.2 }),
    };
    for (const [Key, Patch] of Object.entries(Overrides))
    {
        if (Patch instanceof THREE.Material) Finish[Key] = Patch;
        else Finish[Key].setValues(Patch);
    }
    return Finish;
}

function FinishedMesh(Built, Finish)
{
    const Mesh = new THREE.Mesh(Built.Surface, Built.Families.map((FamilyId) => Finish[FamilyId] || Finish[Family.Default]));
    Mesh.castShadow    = true;
    Mesh.receiveShadow = true;
    return Mesh;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

/**
 * @param {{ Finish?: object, Wing?: boolean }} Options
 * @returns {Promise<{ Root: THREE.Group, BodyMount: THREE.Group, Body: THREE.Mesh, Wing: THREE.Group|null,
 *                     Wheels: { Steer: THREE.Group, Spin: THREE.Group, Mount: THREE.Vector3, Front: boolean, Left: boolean }[],
 *                     Finish: object, Length: number, Width: number }>}
 */
export async function AssembleVehicle({ Finish = DefaultFinish(), Wing = false } = {})
{
    const [BodyFacet, TyreFacet, RimFacet] = await Promise.all(['ControlVehicleBody.facet', 'ControlVehicleTyre.facet', 'ControlVehicleRim.facet'].map(DecodeFacet));
    const BodySurface = SurfaceFromFacet(BodyFacet, 'angle', 34);
    const TyreSurface = SurfaceFromFacet(TyreFacet, 'smooth');
    const RimSurface  = SurfaceFromFacet(RimFacet, 'angle', 40);

    const Root      = new THREE.Group();
    const BodyMount = new THREE.Group();                          // carries body + wing; ride height and pitch/roll act here
    BodyMount.position.y = GroundLift;
    Root.add(BodyMount);
    const Body = FinishedMesh(BodySurface, Finish);
    BodyMount.add(Body);

    let WingGroup = null;
    if (Wing)
    {
        WingGroup = new THREE.Group();
        WingGroup.position.set(...WingMount);
        WingGroup.add(FinishedMesh(SurfaceFromFacet(ConstructWingFacet(), 'flat'), Finish));
        BodyMount.add(WingGroup);
    }

    const Wheels = AxleMounts.map(([X, Y, Z]) =>
    {
        const Steer = new THREE.Group();
        Steer.position.set(X, Z + GroundLift, -Y);
        const Spin  = new THREE.Group();
        const Turn  = new THREE.Group();
        Turn.rotation.y = Y < 0 ? Math.PI : 0;                    // right-hand wheels face outward (BP_ControlVehicle)
        Turn.add(FinishedMesh(TyreSurface, Finish), FinishedMesh(RimSurface, Finish));
        Spin.add(Turn);
        Steer.add(Spin);
        Root.add(Steer);
        return { Steer, Spin, Mount: Steer.position.clone(), Front: X > 0, Left: Y > 0 };
    });

    return { Root, BodyMount, Body, Wing: WingGroup, Wheels, Finish, Length: 6.22, Width: 2.5 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT LIGHTING
//------------------------------------------------------------------------------------------------------------------------

/**
 * 📝 Image-based lighting from a tiny scene of emissive panels, filtered by PMREM. Each HMI passes its own panels so
 *    paint reflections carry that display's mood (light wall, dusk sky, dim workshop).
 * @param {THREE.WebGLRenderer} Renderer
 * @param {{ Colour: string, Intensity: number, Position: number[], Size: number[], Facing?: number[] }[]} Panels
 * @param {string} Ambient
 */
export function PanelEnvironment(Renderer, Panels, Ambient = '#101114')
{
    const Room = new THREE.Scene();
    Room.background = new THREE.Color(Ambient);
    for (const Panel of Panels)
    {
        const Plane = new THREE.Mesh(new THREE.PlaneGeometry(Panel.Size[0], Panel.Size[1]), new THREE.MeshBasicMaterial({ color: new THREE.Color(Panel.Colour).multiplyScalar(Panel.Intensity), side: THREE.DoubleSide }));
        Plane.position.set(...Panel.Position);
        Plane.lookAt(...(Panel.Facing || [0, 0, 0]));
        Room.add(Plane);
    }
    const Filter = new THREE.PMREMGenerator(Renderer);
    const Target = Filter.fromScene(Room, 0.02);
    Filter.dispose();
    return Target.texture;
}
