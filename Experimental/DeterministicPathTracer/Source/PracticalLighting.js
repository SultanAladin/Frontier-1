/*====================================================================================================================================
                                                      PRACTICALLIGHTING.JS
====================================================================================================================================*/
// 🧩 Many practical lamps (IES point emitters on every car and over the car park) and the light tree that picks one per vertex

import { ProfileFloatStride } from './PhotometricProfile.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Profile files (Assets/Photometry) in profile-index order.
export const PhotometryFiles = Object.freeze(['LowBeamHeadlamp.ies', 'TailLamp.ies', 'CarParkDownlight.ies']);
const ProfileHeadlamp  = 0;
const ProfileTailLamp  = 1;
const ProfileDownlight = 2;

export const PracticalModes = Object.freeze({
    off:      { Label: 'Off' },
    vehicles: { Label: 'Car lamps (2 head + 2 tail per car)' },
    carpark:  { Label: 'Car lamps + 28 car-park downlights' }
});

// 📝 Candela → scene radiance units. The softbox rig is in arbitrary linear units; 1/40 makes a 1,700 cd downlight at
//    5 m give roughly the floor irradiance of the key softbox. The inspector slider multiplies this.
const CandelaToScene = 1 / 40;

// 📝 Lamp placement in the vehicle's object frame (+X forward, +Y up, +Z right). The emissive lens strips span
//    x 2.89–3.11 (head) and −3.11…−2.98 (tail); each point emitter sits 5 cm outside the lens so its shadow rays
//    leave the body unobstructed. Beams: headlamps 1.5° nose-down, tail lamps 3° down.
const VehicleLamps = [
    { Profile: ProfileHeadlamp, Position: [3.16, 0.95, -0.75], Pitch: -1.5, Rearward: false, Colour: [1.00, 0.95, 0.88] },
    { Profile: ProfileHeadlamp, Position: [3.16, 0.95,  0.75], Pitch: -1.5, Rearward: false, Colour: [1.00, 0.95, 0.88] },
    { Profile: ProfileTailLamp, Position: [-3.16, 1.09, -0.85], Pitch: -3.0, Rearward: true, Colour: [1.00, 0.035, 0.012] },
    { Profile: ProfileTailLamp, Position: [-3.16, 1.09,  0.85], Pitch: -3.0, Rearward: true, Colour: [1.00, 0.035, 0.012] }
];

// 📝 Downlights over the four aisles between the five parking rows (X = ±4.85, ±14.55), every 6 m along the bays,
//    5 m up. Neutral-warm 3500 K.
const DownlightAisles  = [-14.55, -4.85, 4.85, 14.55];
const DownlightStations = [2, -4, -10, -16, -22, -28, -34];
const DownlightHeight  = 5.0;
const DownlightColour  = [1.00, 0.90, 0.78];

const LightRecordVectors = 4;    // [vec4f] - position + profile, axis, reference, intensity
const TreeNodeVectors    = 4;    // [vec4f] - lower + power, upper + link, cone axis + unused, θo θe
const LeafFlag           = 0x80000000;


//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR HELPERS
//------------------------------------------------------------------------------------------------------------------------

const Dot       = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Cross     = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Normalize = V => { const L = Math.hypot(V[0], V[1], V[2]) || 1; return V.map(C => C / L); };
const AngleBetween = (A, B) => Math.acos(Math.min(1, Math.max(-1, Dot(A, B))));

// 📝 Rodrigues rotation of V about unit Axis by Angle.
function Rotate(V, Axis, Angle)
{
    const C = Math.cos(Angle), S = Math.sin(Angle), K = Cross(Axis, V), D = Dot(Axis, V) * (1 - C);
    return [0, 1, 2].map(I => V[I] * C + K[I] * S + Axis[I] * D);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    LAMP ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Same rigid bay transform as TracingDivision.ConstructInstanceTree (rotation about +Y, then translation).
function PlaceInBay(Bay, Point, IsDirection)
{
    const C = Bay.Yaw === 0 ? 1 : (Bay.Yaw === Math.PI ? -1 : Math.cos(Bay.Yaw));
    const S = (Bay.Yaw === 0 || Bay.Yaw === Math.PI) ? 0 : Math.sin(Bay.Yaw);
    const X = C * Point[0] + S * Point[2], Z = -S * Point[0] + C * Point[2];
    return IsDirection ? [X, Point[1], Z] : [X + Bay.X, Point[1], Z + Bay.Z];
}

// 📝 Returns lamps { Position, Axis, Reference, Intensity (rgb scene units at the profile peak), Profile, Power }.
//    Reference is φ = 0 of the profile: "down" perpendicular to the beam for car lamps, world +X for downlights.
export function ConstructPracticalLamps(Mode, Bays, Profiles, IntensityScale = 1)
{
    if (!Mode || Mode === 'off') return [];
    const Lamps = [];
    const Emit = (Profile, Position, Axis, Reference, Colour) =>
    {
        const Peak = Profiles[Profile].PeakCandela * CandelaToScene * IntensityScale;
        const Intensity = Colour.map(C => C * Peak);
        const Luminance = 0.2126 * Intensity[0] + 0.7152 * Intensity[1] + 0.0722 * Intensity[2];
        Lamps.push({ Position, Axis, Reference, Intensity, Profile, Power: Luminance * Profiles[Profile].NormalizedFlux,
                     EmissionAngle: Profiles[Profile].EmissionAngle });
    };
    for (const Bay of Bays)
    {
        for (const Lamp of VehicleLamps)
        {
            const Pitch = Lamp.Pitch * Math.PI / 180;
            const Local = Normalize([(Lamp.Rearward ? -1 : 1) * Math.cos(Pitch), Math.sin(Pitch), 0]);
            const Down  = Normalize([0, -1, 0].map((C, I) => C - Local[I] * Dot([0, -1, 0], Local)));
            Emit(Lamp.Profile, PlaceInBay(Bay, Lamp.Position, false), Normalize(PlaceInBay(Bay, Local, true)),
                 Normalize(PlaceInBay(Bay, Down, true)), Lamp.Colour);
        }
    }
    if (Mode === 'carpark')
    {
        for (const X of DownlightAisles) for (const Z of DownlightStations) Emit(ProfileDownlight, [X, DownlightHeight, Z], [0, -1, 0], [1, 0, 0], DownlightColour);
    }
    return Lamps;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHT TREE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Bounding cone of emission axes (PBRT-v4 DirectionCone::Union): smallest cone around both input cones.
function UniteCones(A, B)
{
    if (A.Spread >= Math.PI || B.Spread >= Math.PI) return { Axis: A.Axis, Spread: Math.PI };
    const Between = AngleBetween(A.Axis, B.Axis);
    if (Math.min(Between + B.Spread, Math.PI) <= A.Spread) return A;
    if (Math.min(Between + A.Spread, Math.PI) <= B.Spread) return B;
    const Spread = (A.Spread + Between + B.Spread) / 2;
    if (Spread >= Math.PI) return { Axis: A.Axis, Spread: Math.PI };
    const Pivot = Cross(A.Axis, B.Axis);
    if (Math.hypot(...Pivot) < 1e-9) return { Axis: A.Axis, Spread: Math.PI };
    return { Axis: Normalize(Rotate(A.Axis, Normalize(Pivot), Spread - A.Spread)), Spread };
}

// 📝 Binary light tree (Conty Estevez & Kulla 2018): each node bounds its lamps' positions (box), emission axes (cone
//    half-angle θo) and emission extent (θe), and carries their total power. Splits sort the lamps along the axis of
//    largest centroid extent and cut where the summed-power balance is closest to even — a fixed rule, so the tree is
//    a pure function of the lamp list. Leaves hold exactly one lamp.
export function ConstructLightTree(Lamps)
{
    const Build = Indices =>
    {
        const Node = { Lower: [Infinity, Infinity, Infinity], Upper: [-Infinity, -Infinity, -Infinity], Power: 0,
                       Cone: null, EmissionAngle: 0, Children: null, Lamp: -1, Left: -1 };
        for (const Index of Indices)
        {
            const Lamp = Lamps[Index];
            for (let Axis = 0; Axis < 3; Axis++) { Node.Lower[Axis] = Math.min(Node.Lower[Axis], Lamp.Position[Axis]); Node.Upper[Axis] = Math.max(Node.Upper[Axis], Lamp.Position[Axis]); }
            Node.Power += Lamp.Power;
            Node.Cone = Node.Cone ? UniteCones(Node.Cone, { Axis: Lamp.Axis, Spread: 0 }) : { Axis: Lamp.Axis, Spread: 0 };
            Node.EmissionAngle = Math.max(Node.EmissionAngle, Lamp.EmissionAngle);
        }
        if (Indices.length === 1) { Node.Lamp = Indices[0]; return Node; }
        const Extent = [0, 1, 2].map(Axis => Node.Upper[Axis] - Node.Lower[Axis]);
        const SplitAxis = Extent.indexOf(Math.max(...Extent));
        const Sorted = [...Indices].sort((A, B) => (Lamps[A].Position[SplitAxis] - Lamps[B].Position[SplitAxis]) || (A - B));
        let Running = 0, Cut = 1, Best = Infinity;
        for (let I = 0; I + 1 < Sorted.length; I++)
        {
            Running += Lamps[Sorted[I]].Power;
            const Imbalance = Math.abs(2 * Running - Node.Power) + Math.abs(2 * (I + 1) - Sorted.length) * Node.Power / Sorted.length;
            if (Imbalance < Best) { Best = Imbalance; Cut = I + 1; }
        }
        Node.Children = [Build(Sorted.slice(0, Cut)), Build(Sorted.slice(Cut))];
        return Node;
    };
    if (!Lamps.length) return { Nodes: [], Depth: 0 };
    // 📝 Breadth-first flattening: each inner node's two children get adjacent slots (left, left + 1), so a node keeps
    //    a single link.
    const Root = Build(Lamps.map((_, Index) => Index));
    Root.Level = 0;
    const Nodes = [Root];
    let Depth = 0;
    for (let Cursor = 0; Cursor < Nodes.length; Cursor++)
    {
        const Node = Nodes[Cursor];
        Depth = Math.max(Depth, Node.Level);
        if (!Node.Children) continue;
        Node.Left = Nodes.length;
        for (const Child of Node.Children) { Child.Level = Node.Level + 1; Nodes.push(Child); }
    }
    return { Nodes, Depth };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU PACKING
//------------------------------------------------------------------------------------------------------------------------

// 📝 One vec4f storage array for every light (RadianceIntegration.wgsl LightRecords):
//    [area lights 4 × A][lamps 4 × L][tree nodes 4 × N][profiles ProfileFloatStride / 4 × P]. Returns the packed
//    floats and the vec4 offsets for Tracing.Practical.
export function PackLightRecords(AreaPacked, AreaCount, Lamps, Tree, Profiles)
{
    const AreaVectors    = AreaCount * 4;
    const LampBase       = AreaVectors;
    const NodeBase       = LampBase + Lamps.length * LightRecordVectors;
    const ProfileBase    = NodeBase + Tree.Nodes.length * TreeNodeVectors;
    const ProfileVectors = Lamps.length ? Profiles.length * ProfileFloatStride / 4 : 0;
    const Floats = new Float32Array(Math.max(4, (ProfileBase + ProfileVectors) * 4));
    const Words  = new Uint32Array(Floats.buffer);
    Floats.set(AreaPacked.subarray(0, AreaVectors * 4), 0);
    Lamps.forEach((Lamp, Index) =>
    {
        const O = (LampBase + Index * LightRecordVectors) * 4;
        Floats.set([...Lamp.Position, Lamp.Profile, ...Lamp.Axis, 0, ...Lamp.Reference, 0, ...Lamp.Intensity, 0], O);
    });
    Tree.Nodes.forEach((Node, Index) =>
    {
        const O = (NodeBase + Index * TreeNodeVectors) * 4;
        // 📝 Precompute cos/sin of θo and cos(θe) so LightNodeImportance runs with zero asin/acos/cos SFU calls.
        Floats.set([
            ...Node.Lower, Node.Power,
            ...Node.Upper, 0,
            ...Node.Cone.Axis, 0,
            Math.cos(Node.Cone.Spread), Math.sin(Node.Cone.Spread), Math.cos(Node.EmissionAngle), Node.EmissionAngle
        ], O);
        Words[O + 7] = Node.Lamp >= 0 ? (LeafFlag | Node.Lamp) >>> 0 : Node.Left;
    });
    if (ProfileVectors) Profiles.forEach((Profile, Index) => Floats.set(Profile.Table, (ProfileBase + Index * ProfileFloatStride / 4) * 4));
    return { Floats, Layout: { LampCount: Lamps.length, NodeBase, LampBase, ProfileBase } };
}
