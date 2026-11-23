//============================================================================================================================================
//                                                            COURSESTRUCTURE.JS
//============================================================================================================================================
// 🧩 The proving grounds' terrain — the height query the tyres, fragments and smoke read — and the facet emitters every shape uses.

import { CourseMaterial } from "./MaterialSpecification.js";

// 📝 Metres, +X forward along the main straight / +Y left / +Z up. The asphalt pad runs 216 m east–west (the proving
//    grounds' original 120 m square plus the training arena's 96 m east wing) by 120 m; past it a grass plane 2 cm
//    lower runs out to 480 m. Relief is analytic and small — the ramp wedge and the kerbs — so the tyres, the debris
//    and the renderer all read one exact surface.
export const CourseConstants = Object.freeze({
    PadMinimum:     Object.freeze([-60.0, -60.0]),
    PadMaximum:     Object.freeze([156.0, 60.0]),
    GroundCell:     4.0,
    RampNearX:      22.0,
    RampRunX:       6.0,
    RampRise:       1.35,
    RampHalfWidth:  3.2,
    RampCentreY:    0.0,
    SurroundHeight: -0.02
});

// 📝 The training cone: a 70 cm slalom cone after the common EN 13422 pattern — a chamfered square rubber base, a
//    fluorescent PVC body with a rounded tip, two retroreflective collars — weighted low the way a real one is (the
//    base carries most of its 3.2 kg), so it rocks and settles rather than toppling at a brush.
export const ConeConstants = Object.freeze({
    BaseHalf:     0.19,    // [m]  half side of the square base
    BaseHeight:   0.03,    // [m]
    BaseChamfer:  0.05,    // [m]  corner chamfer of the base
    BottomRadius: 0.150,   // [m]  body radius on the base
    TopRadius:    0.032,   // [m]  body radius under the tip
    BodyTop:      0.70,    // [m]
    TipTop:       0.716,   // [m]
    Segments:     24,      // [-]
    Bands:        Object.freeze([Object.freeze([0.30, 0.40]), Object.freeze([0.47, 0.53])]),   // [m] retroreflective collars
    Mass:         3.2,     // [kg]
    CentreHeight: 0.15     // [m]  centre of mass above the ground
});

// 📝 Raised kerb strips, [x₀, x₁, y₀, y₁] at KerbHeight: red-and-white along both edges of the main straight.
export const KerbConstants = Object.freeze({
    Height: 0.120,
    Depth:  0.300,
    SegmentLength: 4.0,
    Strips: Object.freeze([
        Object.freeze([-52.0, 20.0, 6.00, 6.30]),
        Object.freeze([-52.0, 20.0, -6.30, -6.00])
    ])
});

// 📝 Where the car starts and where R puts it back: the grid box behind the start line, facing down the straight.
export const SpawnPoint = Object.freeze({ Position: [-40.0, 0.0], Yaw: 0.0 });

//------------------------------------------------------------------------------------------------------------------------
//                                                   TERRAIN QUERIES
//------------------------------------------------------------------------------------------------------------------------

export function WithinKerb(PositionX, PositionY)
{
    for (const [X0, X1, Y0, Y1] of KerbConstants.Strips)
        if (PositionX >= X0 && PositionX <= X1 && PositionY >= Y0 && PositionY <= Y1)
            return true;
    return false;
}

// 📝 CourseHeight(): kerbs, then the ramp wedge (dropping to zero past the crest so the car launches); flat elsewhere.
export function ResolveCourseHeight(PositionX, PositionY)
{
    const C = CourseConstants;
    if (WithinKerb(PositionX, PositionY))
        return KerbConstants.Height;
    if (PositionX >= C.RampNearX && PositionX <= C.RampNearX + C.RampRunX && Math.abs(PositionY - C.RampCentreY) <= C.RampHalfWidth)
        return C.RampRise * (PositionX - C.RampNearX) / C.RampRunX;
    return 0.0;
}

// 📝 CourseNormal(): central differences over ±0.15 m.
export function ResolveCourseNormal(PositionX, PositionY)
{
    const Step = 0.15;
    const SlopeX = (ResolveCourseHeight(PositionX + Step, PositionY) - ResolveCourseHeight(PositionX - Step, PositionY)) / (2.0 * Step);
    const SlopeY = (ResolveCourseHeight(PositionX, PositionY + Step) - ResolveCourseHeight(PositionX, PositionY - Step)) / (2.0 * Step);
    const Length = Math.hypot(SlopeX, SlopeY, 1.0);
    return [-SlopeX / Length, -SlopeY / Length, 1.0 / Length];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FACET EMISSION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each emitter appends world- or local-space facets as { Corners: [[x,y,z]×3], Family }, counter-clockwise outward.
export function EmitQuad(Facets, A, B, C, D, Family)
{
    Facets.push({ Corners: [A, B, C], Family });
    Facets.push({ Corners: [A, C, D], Family });
}

// 📝 Ramp wedge, world space: sloped surface, far vertical face, the two side walls. The authored corners wind inward
//    (DriveSceneAuthor), so each facet's winding is reversed: the distance transfer classifies inside by back faces.
export function EmitRampFacets()
{
    const C = CourseConstants;
    const Near = C.RampNearX, Far = C.RampNearX + C.RampRunX, Rise = C.RampRise;
    const Low = C.RampCentreY - C.RampHalfWidth, High = C.RampCentreY + C.RampHalfWidth;
    const Facets = [];
    EmitQuad(Facets, [Near, Low, 0.0], [Near, High, 0.0], [Far, High, Rise], [Far, Low, Rise], CourseMaterial.Ramp);
    EmitQuad(Facets, [Far, Low, Rise], [Far, High, Rise], [Far, High, 0.0], [Far, Low, 0.0], CourseMaterial.Ramp);
    Facets.push({ Corners: [[Near, Low, 0.0], [Far, Low, Rise], [Far, Low, 0.0]], Family: CourseMaterial.Ramp });
    Facets.push({ Corners: [[Near, High, 0.0], [Far, High, 0.0], [Far, High, Rise]], Family: CourseMaterial.Ramp });
    for (const Facet of Facets)
        Facet.Corners = [Facet.Corners[0], Facet.Corners[2], Facet.Corners[1]];
    return Facets;
}

// 📝 The chamfered square base as an octagon, counter-clockwise from +X.
function ResolveConeBaseOutline()
{
    const H = ConeConstants.BaseHalf, C = ConeConstants.BaseChamfer;
    return [[H, -(H - C)], [H, H - C], [H - C, H], [-(H - C), H], [-H, H - C], [-H, -(H - C)], [-(H - C), -H], [H - C, -H]];
}

// 📝 One training cone, local to its base centre: the octagonal base (flat), the body split at the collars (smooth
//    normals, so 24 segments read as round), and a two-ring dome for the tip.
export function EmitConeFacets()
{
    const K = ConeConstants;
    const Facets = [];
    const Outline = ResolveConeBaseOutline();
    for (let Side = 0; Side < Outline.length; ++Side)
    {
        const [X0, Y0] = Outline[Side], [X1, Y1] = Outline[(Side + 1) % Outline.length];
        EmitQuad(Facets, [X0, Y0, 0.0], [X1, Y1, 0.0], [X1, Y1, K.BaseHeight], [X0, Y0, K.BaseHeight], CourseMaterial.ConeBase);
        Facets.push({ Corners: [[0.0, 0.0, K.BaseHeight], [X0, Y0, K.BaseHeight], [X1, Y1, K.BaseHeight]], Family: CourseMaterial.ConeBase });
        Facets.push({ Corners: [[0.0, 0.0, 0.0], [X1, Y1, 0.0], [X0, Y0, 0.0]], Family: CourseMaterial.ConeBase });
    }
    const Radius = (Z) => K.BottomRadius + (K.TopRadius - K.BottomRadius) * (Z - K.BaseHeight) / (K.BodyTop - K.BaseHeight);
    const Slope = (K.BottomRadius - K.TopRadius) / (K.BodyTop - K.BaseHeight);
    const Splits = [K.BaseHeight, K.Bands[0][0], K.Bands[0][1], K.Bands[1][0], K.Bands[1][1], K.BodyTop];
    const Families = [CourseMaterial.ConeOrange, CourseMaterial.ConeBand, CourseMaterial.ConeOrange, CourseMaterial.ConeBand, CourseMaterial.ConeOrange];
    const Ring = (Angle, Z, R) => [R * Math.cos(Angle), R * Math.sin(Angle), Z];
    const Normal = (Angle, Lift) => { const L = Math.hypot(1.0, Lift); return [Math.cos(Angle) / L, Math.sin(Angle) / L, Lift / L]; };
    for (let Segment = 0; Segment < K.Segments; ++Segment)
    {
        const A0 = 2.0 * Math.PI * Segment / K.Segments, A1 = 2.0 * Math.PI * (Segment + 1) / K.Segments;
        for (let Band = 0; Band + 1 < Splits.length; ++Band)
        {
            const Z0 = Splits[Band], Z1 = Splits[Band + 1];
            const P00 = Ring(A0, Z0, Radius(Z0)), P10 = Ring(A1, Z0, Radius(Z0)), P11 = Ring(A1, Z1, Radius(Z1)), P01 = Ring(A0, Z1, Radius(Z1));
            const N0 = Normal(A0, Slope), N1 = Normal(A1, Slope);
            Facets.push({ Corners: [P00, P10, P11], Normals: [N0, N1, N1], Family: Families[Band] });
            Facets.push({ Corners: [P00, P11, P01], Normals: [N0, N1, N0], Family: Families[Band] });
        }
        // 📝 The tip: the body's last ring, a ring two thirds of the way round the dome, the apex.
        const Mid = K.BodyTop + 0.6 * (K.TipTop - K.BodyTop), MidRadius = 0.62 * K.TopRadius;
        const Q00 = Ring(A0, K.BodyTop, K.TopRadius), Q10 = Ring(A1, K.BodyTop, K.TopRadius);
        const Q01 = Ring(A0, Mid, MidRadius), Q11 = Ring(A1, Mid, MidRadius);
        const M0 = Normal(A0, 1.1), M1 = Normal(A1, 1.1);
        Facets.push({ Corners: [Q00, Q10, Q11], Normals: [Normal(A0, Slope), Normal(A1, Slope), M1], Family: CourseMaterial.ConeOrange });
        Facets.push({ Corners: [Q00, Q11, Q01], Normals: [Normal(A0, Slope), M1, M0], Family: CourseMaterial.ConeOrange });
        Facets.push({ Corners: [Q01, Q11, [0.0, 0.0, K.TipTop]], Normals: [M0, M1, [0.0, 0.0, 1.0]], Family: CourseMaterial.ConeOrange });
    }
    return Facets;
}

// 📝 The cone's collision hull points, local to its base centre: a regular octagon a touch wider than the base's
//    flats (at the ground and on top of the base) and a small octagon under the tip, every ring at the same angles
//    so each face of the hull is a plane — 18 planes, 24 points. A fair hull for something that rolls.
export function ResolveConeHullPoints()
{
    const K = ConeConstants;
    const Points = [];
    const Base = K.BaseHalf * 1.04;
    for (const [Z, Radius] of [[0.0, Base], [K.BaseHeight, Base], [K.BodyTop, K.TopRadius]])
        for (let Index = 0; Index < 8; ++Index)
        {
            const Angle = 2.0 * Math.PI * (Index + 0.5) / 8;
            const R = Radius / Math.cos(Math.PI / 8);   // the octagon's flats at Radius
            Points.push([R * Math.cos(Angle), R * Math.sin(Angle), Z]);
        }
    return Points;
}

// 📝 A row of tyre stacks along x (a tyre barrier section), local to its bottom centre: each stack four tyres high,
//    the top tyre painted white so the wall reads at speed.
export function EmitTyreBarrierFacets(Count, Spacing, Radius, Height, Sides = 12)
{
    const Facets = [];
    for (let Stack = 0; Stack < Count; ++Stack)
    {
        const Offset = (Stack - 0.5 * (Count - 1)) * Spacing;
        const Split = 0.75 * Height;
        for (const [Z0, Z1, Family] of [[0.0, Split, CourseMaterial.Tyre], [Split, Height, CourseMaterial.PaintWhite]])
        {
            for (let Side = 0; Side < Sides; ++Side)
            {
                const A0 = 2.0 * Math.PI * Side / Sides, A1 = 2.0 * Math.PI * (Side + 1) / Sides;
                const P0 = [Offset + Radius * Math.cos(A0), Radius * Math.sin(A0)], P1 = [Offset + Radius * Math.cos(A1), Radius * Math.sin(A1)];
                EmitQuad(Facets, [P0[0], P0[1], Z0], [P1[0], P1[1], Z0], [P1[0], P1[1], Z1], [P0[0], P0[1], Z1], Family);
                if (Z1 === Height)
                    Facets.push({ Corners: [[Offset, 0.0, Height], [P0[0], P0[1], Height], [P1[0], P1[1], Height]], Family: CourseMaterial.Tyre });
                if (Z0 === 0.0)
                    Facets.push({ Corners: [[Offset, 0.0, 0.0], [P1[0], P1[1], 0.0], [P0[0], P0[1], 0.0]], Family });
            }
        }
    }
    return Facets;
}

// 📝 A closed box between two corners (any placement), outward counter-clockwise winding.
export function EmitCornerBoxFacets(Minimum, Maximum, Family)
{
    const [X0, Y0, Z0] = Minimum, [X1, Y1, Z1] = Maximum;
    const Facets = [];
    EmitQuad(Facets, [X0, Y0, Z1], [X1, Y0, Z1], [X1, Y1, Z1], [X0, Y1, Z1], Family);
    EmitQuad(Facets, [X0, Y1, Z0], [X1, Y1, Z0], [X1, Y0, Z0], [X0, Y0, Z0], Family);
    EmitQuad(Facets, [X1, Y0, Z0], [X1, Y1, Z0], [X1, Y1, Z1], [X1, Y0, Z1], Family);
    EmitQuad(Facets, [X0, Y1, Z0], [X0, Y0, Z0], [X0, Y0, Z1], [X0, Y1, Z1], Family);
    EmitQuad(Facets, [X1, Y1, Z0], [X0, Y1, Z0], [X0, Y1, Z1], [X1, Y1, Z1], Family);
    EmitQuad(Facets, [X0, Y0, Z0], [X1, Y0, Z0], [X1, Y0, Z1], [X0, Y0, Z1], Family);
    return Facets;
}

// 📝 A closed axis-aligned box, local to its bottom centre, outward counter-clockwise winding. FaceFamilies, when
//    given, colours the six faces individually: [top, bottom, +X, −X, +Y, −Y].
export function EmitBoxFacets(ExtentX, ExtentY, ExtentZ, Family, FaceFamilies = null)
{
    const X = 0.5 * ExtentX, Y = 0.5 * ExtentY, Z = ExtentZ;
    const F = FaceFamilies || [Family, Family, Family, Family, Family, Family];
    const Facets = [];
    EmitQuad(Facets, [-X, -Y, Z], [X, -Y, Z], [X, Y, Z], [-X, Y, Z], F[0]);       // top    (+Z)
    EmitQuad(Facets, [-X, Y, 0.0], [X, Y, 0.0], [X, -Y, 0.0], [-X, -Y, 0.0], F[1]); // bottom (−Z)
    EmitQuad(Facets, [X, -Y, 0.0], [X, Y, 0.0], [X, Y, Z], [X, -Y, Z], F[2]);       // front  (+X)
    EmitQuad(Facets, [-X, Y, 0.0], [-X, -Y, 0.0], [-X, -Y, Z], [-X, Y, Z], F[3]);   // back   (−X)
    EmitQuad(Facets, [X, Y, 0.0], [-X, Y, 0.0], [-X, Y, Z], [X, Y, Z], F[4]);       // left   (+Y)
    EmitQuad(Facets, [-X, -Y, 0.0], [X, -Y, 0.0], [X, -Y, Z], [-X, -Y, Z], F[5]);   // right  (−Y)
    return Facets;
}

// 📝 A wall panel: Outer on every face but the −Y one, which is the painted Inner face a building shows inside.
export function EmitPanelFacets(ExtentX, ExtentY, ExtentZ, Outer, Inner)
{
    return EmitBoxFacets(ExtentX, ExtentY, ExtentZ, Outer, [Outer, Outer, Outer, Outer, Outer, Inner]);
}

// 📝 A vertical prism of Sides facets (tyre stacks), local to its bottom centre.
export function EmitPrismFacets(Radius, Height, Sides, Family)
{
    const Facets = [];
    for (let Side = 0; Side < Sides; ++Side)
    {
        const A0 = 2.0 * Math.PI * Side / Sides, A1 = 2.0 * Math.PI * (Side + 1) / Sides;
        const P0 = [Radius * Math.cos(A0), Radius * Math.sin(A0)], P1 = [Radius * Math.cos(A1), Radius * Math.sin(A1)];
        EmitQuad(Facets, [P0[0], P0[1], 0.0], [P1[0], P1[1], 0.0], [P1[0], P1[1], Height], [P0[0], P0[1], Height], Family);
        Facets.push({ Corners: [[0.0, 0.0, Height], [P0[0], P0[1], Height], [P1[0], P1[1], Height]], Family });
        Facets.push({ Corners: [[0.0, 0.0, 0.0], [P1[0], P1[1], 0.0], [P0[0], P0[1], 0.0]], Family });
    }
    return Facets;
}

// 📝 A convex (y, z) profile, counter-clockwise seen from +X, extruded along x over Length and centred on x = 0.
export function EmitExtrusionFacets(Profile, Length, Family)
{
    const X = 0.5 * Length;
    const Facets = [];
    const Count = Profile.length;
    for (let Index = 0; Index < Count; ++Index)
    {
        const [Y0, Z0] = Profile[Index], [Y1, Z1] = Profile[(Index + 1) % Count];
        EmitQuad(Facets, [-X, Y0, Z0], [-X, Y1, Z1], [X, Y1, Z1], [X, Y0, Z0], Family);
    }
    for (let Index = 1; Index + 1 < Count; ++Index)
    {
        Facets.push({ Corners: [[X, ...Profile[0]], [X, ...Profile[Index]], [X, ...Profile[Index + 1]]], Family });
        Facets.push({ Corners: [[-X, ...Profile[0]], [-X, ...Profile[Index + 1]], [-X, ...Profile[Index]]], Family });
    }
    return Facets;
}
