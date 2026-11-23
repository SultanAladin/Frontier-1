// VEHICLE ASSEMBLY
// 🧩 Builds the Component Viewer's ControlVehicle (body, four tyres, four rims, optional rear wing) as one three.js
//    group for the cabin HMI prototypes. Same FCT1 facets, axle mounts and ground offset as BP_ControlVehicle.
//
// 📝 Authored facets are Z-up, +X forward, +Y left, metres. Everything here is converted to Y-up on read:
//    (x, y, z) → (x, z, −y), exactly as Experimental/ComponentViewer/Main.js does. Ground sits at y = 0.

import * as THREE from '../../Shared/Studio/Vendor/three.esm.min.js';
import { readFacet } from '../../Shared/GeometryCompile/Engine/ImportFormats.js';
import { cornerNormals } from '../../Shared/GeometryCompile/Engine/MeshAnalysis.js';
import { VEHICLE_SHEET } from '../../ComponentViewer/Blueprint.js';

export { THREE };

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const ModelRoot   = new URL('../../Shared/Models/', import.meta.url);
const AxleHeight  = 0.0914;                                       // [m] - authored axle z
const TyreRadius  = 0.509;                                        // [m] - from the tyre facet bounds
const GroundLift  = TyreRadius - AxleHeight;                      // [m] - authored z of the ground is −GroundLift
const WingMount   = [-2.9822, 0.8367, 0];                         // [m] - RearWingSocket location (Y-up, body local)

// CourseMaterial families present in the facets.
export const Family = { BodyCoat: 6, Glass: 7, Plastic: 8, Tyre: 9, Hub: 10, Brake: 11, Trim: 12, Headlight: 13, Taillight: 14, Default: 15 };

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

    const Wheels = VEHICLE_SHEET.axleMount.map(([X, Y, Z]) =>
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

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLOUR EASING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Eases a material colour towards a target over a duration; used for paint, light and accent micro animations.
export function EaseColour(Colour, Target, Duration = 600)
{
    const Start = Colour.clone();
    const End   = new THREE.Color(Target);
    const Begin = performance.now();
    return new Promise((Resolve) =>
    {
        const Advance = (Now) =>
        {
            const Ratio = Math.min(1, (Now - Begin) / Duration);
            Colour.copy(Start).lerp(End, 1 - Math.pow(1 - Ratio, 3));
            if (Ratio < 1) requestAnimationFrame(Advance); else Resolve();
        };
        requestAnimationFrame(Advance);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MIRROR TWIN
//------------------------------------------------------------------------------------------------------------------------

/**
 * 📝 Reflective floors without a Reflector: a y-flipped twin of the vehicle sits under a semi-transparent floor.
 *    Geometry and materials are shared; Sync copies every local transform (wheel spin, steer, body lift) each frame.
 *    three.js flips the face winding for a negative-determinant matrix, so the twin shades correctly.
 * @param {THREE.Object3D} Source
 * @returns {{ Group: THREE.Group, Sync: () => void }}
 */
export function MirrorTwin(Source)
{
    const Group = new THREE.Group();
    Group.scale.y = -1;
    const Copy = Source.clone(true);
    Group.add(Copy);
    const From = [], To = [];
    Source.traverse((Each) => From.push(Each));
    Copy.traverse((Each) => To.push(Each));
    const Sync = () =>
    {
        for (let Index = 0; Index < From.length; Index++)
        {
            To[Index].position.copy(From[Index].position);
            To[Index].quaternion.copy(From[Index].quaternion);
            To[Index].scale.copy(From[Index].scale);
            To[Index].visible = From[Index].visible;
        }
    };
    return { Group, Sync };
}
