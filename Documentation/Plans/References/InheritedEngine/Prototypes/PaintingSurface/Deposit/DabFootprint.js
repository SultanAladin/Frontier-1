/*====================================================================================================================================
                                                     DABFOOTPRINT.JS
====================================================================================================================================*/
// 🧩 Decompose a pointer path into evenly spaced dabs and evaluate the brush falloff those dabs carry

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Spacing is a fraction of the dab RADIUS, matching Blender and Krita, so a spacing of 0.1 lays
//    ten dabs across one radius regardless of brush size. Storing it as a fraction rather than a
//    distance is what lets one recorded stroke replay at any brush size.
export const DefaultSpacing = 0.10;   // [-]  - fraction of radius between dab centres

// A stroke that emits more dabs than this in a single move is clamped. 📝 Dab count scales with pointer
// DISTANCE, not time, so a fast flick across the viewport would otherwise demand thousands of dabs in
// one frame and collapse the frame rate — the positive-feedback trap called out in the research.
export const MaximumDabsPerMove = 512;

// Below this radius in object units a dab cannot resolve in the atlas and is dropped.
const MinimumRadius = 1e-5;

//------------------------------------------------------------------------------------------------------------------------
//                                                        FALLOFF
//------------------------------------------------------------------------------------------------------------------------

// Brush falloff evaluated at a normalized distance from the dab centre.
//
// 📝 Hardness splits the disc into a flat core and a smoothstep shoulder. At hardness 1 the shoulder
//    has zero width, so the result is a hard-edged disc; at 0 the falloff starts at the centre. The
//    same curve is evaluated on the GPU in PaintPass — keep the two in step or the probe and the
//    render will disagree.
export function EvaluateFalloff(NormalizedDistance, Hardness)
{
    if (NormalizedDistance >= 1.0) { return 0.0; }
    if (NormalizedDistance <= 0.0) { return 1.0; }

    const CoreEdge = Math.min(Math.max(Hardness, 0.0), 0.999);
    if (NormalizedDistance <= CoreEdge) { return 1.0; }

    const Shoulder = (NormalizedDistance - CoreEdge) / (1.0 - CoreEdge);

    // Smoothstep on the shoulder: 3t² - 2t³, inverted so the edge reaches zero.
    return 1.0 - (Shoulder * Shoulder * (3.0 - 2.0 * Shoulder));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DAB DECOMPOSITION
//------------------------------------------------------------------------------------------------------------------------

// Distance between two object-space points.
function Separation(From, To)
{
    return Math.hypot(To[0] - From[0], To[1] - From[1], To[2] - From[2]);
}

// Linear blend of two dab anchors. Normal and pressure ride along with position.
//
// 🔴 Coordinate is NOT mixed the way the other fields are. Position, normal and pressure are
//    continuous over the surface, so a linear blend of two nearby samples is a good approximation of
//    the value between them. A UV coordinate is not continuous: it is piecewise per island and jumps
//    at every seam. Mixing across a seam yields a coordinate that lies on NEITHER island — for a
//    stroke crossing Suzanne's ear onto her face, the recorded v marched smoothly 0.87 -> 0.76 while
//    the true v at those positions was 0.21, so 96 of 230 dabs named empty atlas.
//
//    The blend therefore SNAPS to the nearer endpoint's island rather than interpolating between two
//    of them. Within one island the two endpoints' coordinates are close, so snapping costs at most
//    half a dab step of drift; across a seam it is the difference between a real texel and a fiction.
//    📝 This does not move any paint: PaintPass rasterizes UV from the MESH and tests purely in object
//    space, so Coordinate is metadata (diagnostics, and atlas-tile culling later). It must still be
//    true, because anything that trusts it is otherwise reading a coordinate the surface never had.
function BlendAnchor(From, To, Fraction)
{
    const Mix = (A, B) => A + (B - A) * Fraction;

    const Normal = [
        Mix(From.Normal[0], To.Normal[0]),
        Mix(From.Normal[1], To.Normal[1]),
        Mix(From.Normal[2], To.Normal[2])
    ];

    const Length = Math.hypot(Normal[0], Normal[1], Normal[2]);
    if (Length > 1e-12) { Normal[0] /= Length; Normal[1] /= Length; Normal[2] /= Length; }

    // Same triangle at both ends means no seam can lie between them, so the ordinary mix is exact.
    // Otherwise the mix is left in place as a provisional value and ResolveCoordinate replaces it
    // with the surface's own UV at the blended position.
    const Continuous = From.Triangle !== undefined && From.Triangle === To.Triangle;

    return {
        Position: [
            Mix(From.Position[0], To.Position[0]),
            Mix(From.Position[1], To.Position[1]),
            Mix(From.Position[2], To.Position[2])
        ],
        Coordinate: [Mix(From.Coordinate[0], To.Coordinate[0]), Mix(From.Coordinate[1], To.Coordinate[1])],
        Normal,
        Pressure: Mix(From.Pressure ?? 1.0, To.Pressure ?? 1.0),
        // Flags whether the mix above can be trusted, so the caller knows which dabs need resolving.
        CoordinateExact: Continuous
    };
}

// Walk the segment between two anchors, emitting a dab every `Spacing * Radius` object units.
//
// `Carry` is the leftover distance from the previous segment, so spacing is continuous across a whole
// stroke rather than restarting at each pointer sample. Returns { Dabs, Carry }.
//
// 📝 The interpolation is straight-line in object space, not along the surface. Over one pointer
//    sample on a mesh of Suzanne's density the chord and the surface differ by far less than a dab
//    radius; on coarse geometry with large moves this would cut corners, which is recorded as a
//    known limit rather than silently accepted.
export function DecomposeSegment(From, To, Radius, Spacing, Carry)
{
    const Dabs = [];

    if (!(Radius > MinimumRadius)) { return { Dabs, Carry: 0 }; }

    const Step = Math.max(Spacing, 0.01) * Radius;
    const Span = Separation(From.Position, To.Position);

    if (Span < 1e-9) { return { Dabs, Carry }; }

    // Distance along this segment at which the next dab falls.
    let Travelled = Step - Carry;

    while (Travelled <= Span && Dabs.length < MaximumDabsPerMove)
    {
        Dabs.push(BlendAnchor(From, To, Travelled / Span));
        Travelled += Step;
    }

    // 📝 What is left over is measured back from the last dab that was actually placed, not from the
    //    ideal grid — otherwise the clamp above would silently shift every later dab in the stroke.
    const Placed        = Dabs.length;
    const LastDabAt     = Step * Placed - Carry;
    const RemainingSpan = Span - LastDabAt;

    return { Dabs, Carry: Math.max(0, RemainingSpan) };
}

// Convert a brush radius expressed in screen pixels into object units at a given hit.
//
// 📝 The record stores object-space radius so a stroke replays identically at any viewport size or
//    zoom — device pixels are explicitly not a valid unit for anything persisted.
export function ResolveObjectRadius(PixelRadius, Distance, VerticalFieldOfView, SurfaceHeight)
{
    const HalfSpanAtDistance = Math.tan(VerticalFieldOfView * 0.5) * Distance;
    return (PixelRadius / (SurfaceHeight * 0.5)) * HalfSpanAtDistance;
}
