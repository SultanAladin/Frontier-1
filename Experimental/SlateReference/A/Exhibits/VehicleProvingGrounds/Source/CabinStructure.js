import { CabinLayout as SlateLayout, ConstructCabinTransform, ConstructCabinFacets as SlateCabinFacets } from './SlateCabinLayout.js';
import { ConstructSlateFittings } from './SlateCabinFittings.js';
import { TransformPoint, TransformDirection } from './TransformProjection.js';
// Slate 3c987f3 cockpit (dash, wheel-mounted cluster, tablet and seats), adapted to
// the existing deferred renderer. Mirrors are retained from the host. +X forward,
// +Y driver/left, +Z up. All physical fittings and seated cameras remain fixed.

import { CourseMaterial } from "./MaterialSpecification.js";
import { ResolveCoMAuthoredZ } from "./VehicleSpecification.js";

const Rebase = ResolveCoMAuthoredZ();

// 📝 Model frame → car-body frame.
function Body(X, Y, Z)
{
    return [X, Y, Z - Rebase];
}

function Add(A, B) { return [A[0] + B[0], A[1] + B[1], A[2] + B[2]]; }
function Subtract(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
function Scale(A, S) { return [A[0] * S, A[1] * S, A[2] * S]; }
function Dot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
function Cross(A, B) { return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]]; }
function Normalize(A) { const L = Math.hypot(A[0], A[1], A[2]) || 1.0; return [A[0] / L, A[1] / L, A[2] / L]; }
function Combine(Origin, Axes, Weights) { return Add(Origin, Add(Add(Scale(Axes[0], Weights[0]), Scale(Axes[1], Weights[1])), Scale(Axes[2], Weights[2]))); }

// 📝 A triangle whose geometric normal is turned to agree with Expected (closed solids come out wound outward).
function Triangle(Facets, A, B, C, Family, Expected = null, Normals = null)
{
    let Corners = [A, B, C], Corner = Normals;
    if (Expected)
    {
        const Normal = Cross(Subtract(B, A), Subtract(C, A));
        if (Dot(Normal, Expected) < 0.0)
        {
            Corners = [A, C, B];
            if (Normals)
                Corner = [Normals[0], Normals[2], Normals[1]];
        }
    }
    Facets.push(Corner ? { Corners, Normals: Corner, Family } : { Corners, Family });
}

function Quad(Facets, A, B, C, D, Family, Expected = null)
{
    Triangle(Facets, A, B, C, Family, Expected);
    Triangle(Facets, A, C, D, Family, Expected);
}

// 📝 An oriented box: centre, half extents along three orthonormal axes.
function Box(Facets, Centre, Half, Axes, Family)
{
    for (let Axis = 0; Axis < 3; ++Axis)
    {
        for (const Sign of [-1, 1])
        {
            const U = (Axis + 1) % 3, V = (Axis + 2) % 3;
            const Corner = (Su, Sv) =>
            {
                const Weights = [0, 0, 0];
                Weights[Axis] = Sign * Half[Axis];
                Weights[U] = Su * Half[U];
                Weights[V] = Sv * Half[V];
                return Combine(Centre, Axes, Weights);
            };
            Quad(Facets, Corner(-1, -1), Corner(1, -1), Corner(1, 1), Corner(-1, 1), Family, Scale(Axes[Axis], Sign));
        }
    }
}

// 📝 A capped cylinder between two points (smooth sides, flat caps).
function Cylinder(Facets, From, To, Radius, Family, Sides = 16)
{
    const Axis = Normalize(Subtract(To, From));
    const Helper = Math.abs(Axis[2]) < 0.9 ? [0.0, 0.0, 1.0] : [1.0, 0.0, 0.0];
    const U = Normalize(Cross(Axis, Helper)), V = Cross(Axis, U);
    const Ring = (Index) => { const θ = 2.0 * Math.PI * Index / Sides; return Add(Scale(U, Math.cos(θ)), Scale(V, Math.sin(θ))); };
    for (let Index = 0; Index < Sides; ++Index)
    {
        const R0 = Ring(Index), R1 = Ring(Index + 1);
        const A = Add(From, Scale(R0, Radius)), B = Add(From, Scale(R1, Radius)), C = Add(To, Scale(R1, Radius)), D = Add(To, Scale(R0, Radius));
        const Expected = Add(R0, R1);
        Triangle(Facets, A, B, C, Family, Expected, [R0, R1, R1]);
        Triangle(Facets, A, C, D, Family, Expected, [R0, R1, R0]);
        Triangle(Facets, From, B, A, Family, Scale(Axis, -1.0));
        Triangle(Facets, To, D, C, Family, Axis);
    }
}

// 📝 A screen or mirror frame: centre, the viewer's right, up, and the normal toward the viewer (Right × Up).
function Fixture(Centre, Right, Up, Width, Height, Extra = {})
{
    const Normal = Normalize(Cross(Right, Up));
    return { Centre, Right: Normalize(Right), Up: Normalize(Up), Normal, Width, Height, ...Extra };
}

// 📝 Tilt: the top of the screen leaning away from the driver (+X) by Tilt radians (DriveCabinLayout's CabinScreenBasis).
function ScreenFixture(CentreModel, Tilt, Width, Height, Extra = {})
{
    const Up = [Math.sin(Tilt), 0.0, Math.cos(Tilt)];
    return Fixture(Body(...CentreModel), [0.0, -1.0, 0.0], Up, Width, Height, Extra);
}

// 📐 A planar mirror aimed for an eye: the normal halves the eye's incoming ray and the wanted reflected ray.
function MirrorFixture(Name, CentreModel, Eye, Reflected, Width, Height)
{
    const Centre = Body(...CentreModel);
    const Incoming = Normalize(Subtract(Centre, Eye));
    const Normal = Normalize(Subtract(Normalize(Reflected), Incoming));
    const Right = Normalize(Cross([0.0, 0.0, 1.0], Normal));
    const Up = Cross(Normal, Right);
    return { Name, Centre, Right, Up, Normal, Width, Height };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYOUT
//------------------------------------------------------------------------------------------------------------------------

const DriverEyeModel = [-0.20, 0.42, 1.12];
const ColumnAxis = [-Math.cos(SlateLayout.WheelTilt), 0, Math.sin(SlateLayout.WheelTilt)];   // toward the driver, rising 26.6°

// 📝 Hip (H-) points on the front cushions: where a seated occupant's eye is carried from when the cockpit is sized.
export const SeatHipPoints = Object.freeze({ Driver: Body(-0.30, 0.42, 0.34), Passenger: Body(-0.30, -0.42, 0.34) });

export const CabinLayout = (() =>
{
    const Eye = Body(...DriverEyeModel);
    const WheelCentre = Body(...SlateLayout.Wheel);
    const Layout = {
        DriverEye:    { Position: Eye, PitchDown: 0.14, FieldOfView: 70.0 },
        PassengerEye: { Position: Body(-0.22, -0.42, 1.10), PitchDown: 0.12, FieldOfView: 70.0 },
        Steering:     { Pivot: WheelCentre, Axis: ColumnAxis, Radius: SlateLayout.WheelRadius, Ratio: 3 },
        Tablet:       ScreenFixture(SlateLayout.Tablet, SlateLayout.TabletTilt, SlateLayout.TabletWidth, SlateLayout.TabletHeight, { Luminance: 3.0 }),
        Cluster:      ScreenFixture(SlateLayout.Wheel, SlateLayout.WheelTilt, 0.27, 0.151, { Luminance: 3.5, Outline: "hexagon" }),
        Mirrors: [
            MirrorFixture("Left mirror",  [1.02, 1.33, 0.86], Eye, [-1.0, 0.06, -0.03], 0.19, 0.105),
            MirrorFixture("Right mirror", [1.02, -1.33, 0.86], Eye, [-1.0, -0.06, -0.03], 0.19, 0.105),
            MirrorFixture("Rear-view mirror", [0.62, 0.0, 1.20], Eye, [-1.0, 0.0, -0.035], 0.26, 0.075)
        ]
    };
    Layout.Tablet.Centre = Add(Layout.Tablet.Centre, Scale(Layout.Tablet.Normal, 0.014));
    Layout.Cluster.Centre = Add(Layout.Cluster.Centre, Scale(Layout.Cluster.Normal, 0.023));
    return Layout;
})();

// 📝 The camera rigs: the chase camera, the free camera (F8) and the interior and inspection rigs fixed to the car
//    (C cycles them). Eye and look in the car-body frame; Pitch [rad] down, Fov [°] vertical.
export const CameraRigs = Object.freeze({
    Chase:     null,
    Orbit:     { OrbitVehicle: true, Fov: 60 },
    Cockpit:   { Eye: CabinLayout.DriverEye.Position, Hip: SeatHipPoints.Driver, Pitch: CabinLayout.DriverEye.PitchDown, Fov: 70.0, Interior: true },
    HMI:       { Interior: true, FocusTablet: true, Fov: 40.0 },
    Passenger: { Eye: CabinLayout.PassengerEye.Position, Hip: SeatHipPoints.Passenger, Pitch: CabinLayout.PassengerEye.PitchDown, Fov: 70.0, Interior: true },
    Hood:      { Eye: [1.62, 0.0, 0.70], Pitch: 0.05, Fov: 62.0 },
    "Tyre FL": { Eye: [2.95, 2.05, -0.12], Target: [1.7274, 1.0475, -0.38], Fov: 38.0 },
    "Tyre RL": { Eye: [-0.55, 2.15, -0.15], Target: [-1.6686, 1.0475, -0.38], Fov: 38.0 }
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  COCKPIT SIZE
//------------------------------------------------------------------------------------------------------------------------

// Fixed frame retained as a compatibility value for camera/touch consumers.
const CockpitAnchor = Body(0.30, 0.0, 0.14);

export function ResolveCockpitFrame()
{
    // Cabin hardware and seated cameras are fixed. HUD controls never feed geometry.
    return { Anchor: CockpitAnchor, Scale: 1, Offset: [0,0,0] };
}

// 📐 P′ = Anchor + Scale·(P − Anchor) + Offset — the same placement the vertex stage gives the cockpit's facets.
export function PlaceCockpitPoint(Point, Frame)
{
    return Add(Add(Frame.Anchor, Scale(Subtract(Point, Frame.Anchor), Frame.Scale)), Frame.Offset);
}

// 📝 A seated eye: carried by its seat's hip point (Eye + Place(Hip) − Hip), its height above the cushion unscaled.
export function PlaceSeatedEye(Eye, Hip, Frame)
{
    return Add(Eye, Subtract(PlaceCockpitPoint(Hip, Frame), Hip));
}

// 📝 A screen fixture placed with the cockpit: its centre moved, its size scaled, its axes unchanged.
export function PlaceCockpitFixture(Fixture, Frame)
{
    return { ...Fixture, Centre: PlaceCockpitPoint(Fixture.Centre, Frame), Width: Fixture.Width * Frame.Scale, Height: Fixture.Height * Frame.Scale };
}

// 📝 The cluster's hexagon (ClusterSequence's outline), unit points over [−1, 1]².
export const ClusterHexagon = [[-0.62, -1], [0.62, -1], [1, 0], [0.62, 1], [-0.62, 1], [-1, 0]];

//------------------------------------------------------------------------------------------------------------------------
//                                                    FACETS
//------------------------------------------------------------------------------------------------------------------------

export function ConstructCabinFacets()
{
    const Facets = SlateCabinFacets().map(Facet => ({ ...Facet, Family: CourseMaterial.CabinTrim }));
    const F = CourseMaterial;
    const WheelPose = ConstructCabinTransform(SlateLayout.Wheel, SlateLayout.WheelTilt);
    const TabletPose = ConstructCabinTransform(SlateLayout.Tablet, SlateLayout.TabletTilt);
    const Vertices = ConstructSlateFittings();
    for (let Index = 0; Index < Vertices.length; Index += 3) {
        const Group = Vertices.slice(Index, Index + 3), Material = Group[0].Material;
        const Pose = Material <= 2 ? WheelPose : TabletPose;
        const Family = Material <= 2 ? F.SteeringWheel : Material === 3 ? F.TabletHousing : F.DisplayGlass;
        Facets.push({ Family, Corners: Group.map(V => TransformPoint(Pose, V.P)),
            Normals: Group.map(V => Normalize(TransformDirection(Pose, V.N))) });
    }

    // ⑥ mirrors: door housings on short arms, the header mount of the rear-view mirror, silvered glass
    for (const Mirror of CabinLayout.Mirrors)
    {
        const Header = Mirror.Name.startsWith("Rear");
        const Depth = Header ? 0.018 : 0.05;
        const Housing = Add(Mirror.Centre, Scale(Mirror.Normal, -Depth - 0.0015));
        Box(Facets, Housing, [Mirror.Width * 0.5 + 0.012, Mirror.Height * 0.5 + 0.012, Depth], [Mirror.Right, Mirror.Up, Mirror.Normal],
            Header ? F.MirrorTrim : F.BodyCoat);
        const Corner = (X, Y) => Add(Add(Mirror.Centre, Scale(Mirror.Right, X * Mirror.Width * 0.5)), Scale(Mirror.Up, Y * Mirror.Height * 0.5));
        Quad(Facets, Corner(-1, -1), Corner(1, -1), Corner(1, 1), Corner(-1, 1), F.MirrorGlass, Mirror.Normal);
        if (Header)
            Cylinder(Facets, Add(Mirror.Centre, Scale(Mirror.Normal, -0.03)), Add(Mirror.Centre, [0.03, 0.0, 0.11]), 0.012, F.MirrorTrim, 10);
        else
        {
            const Side = Math.sign(Mirror.Centre[1]);
            Box(Facets, Add(Mirror.Centre, [0.035, -Side * 0.12, -0.05]), [0.06, 0.07, 0.022], [[1, 0, 0], [0, 1, 0], [0, 0, 1]], F.BodyCoat);
        }
    }
    return Facets;
}

// 📝 The glazing of the authored body (VehicleGlass facets), body frame, for the cabin overlay to draw see-through.
export function ExtractGlassFacets(BodyFacets)
{
    return BodyFacets.filter((Facet) => Facet.Family === CourseMaterial.VehicleGlass);
}

export { Body as ProjectModelPoint };

// Tablet-only scaling: keep its centre fixed and preserve aspect ratio. No cluster,
// steering wheel, dashboard, seats or driver-eye motion.
export function ResolveTabletScale(Value = 1) {
    return Number.isFinite(Value) ? Math.max(0.75, Math.min(2.5, Value)) : 1;
}
export function ResolveTabletFixture(Value = 1) {
    const Scale = ResolveTabletScale(Value);
    return { ...CabinLayout.Tablet, Width: CabinLayout.Tablet.Width * Scale, Height: CabinLayout.Tablet.Height * Scale };
}
export function ResolveHmiCamera(Scale = 1, Aspect = 1, Zoom = 1) {
    const Tablet = ResolveTabletFixture(Scale), Fov = 40;
    const Distance = 0.5 * Math.max(Tablet.Height, Tablet.Width / Math.max(0.2, Aspect))
        / Math.tan(Fov * Math.PI / 360) * 1.18 * (Number.isFinite(Zoom) ? Math.max(0.65, Math.min(2, Zoom)) : 1);
    return { Eye: Add(Tablet.Centre, Tablet.Normal.map(V => V * Distance)), Target: Tablet.Centre, Up: Tablet.Up, Fov };
}
