//============================================================================================================================================
//                                                           OVERLAYVERTEX.SLANG
//============================================================================================================================================
// 🧩 The overlay pass's vertex stage: expands every CPU-side primitive into its
//    screen-space triangles ON THE GPU, so the CPU never tessellates.
//
//    One vertex buffer holds every record, in three regions:
//      - lines:      { x0, y0, x1, y1, thickness, a, r, g, b }   (two points per line)
//      - dots:       { x, y, radius, padding, a, r, g, b }       (one point per dot)
//      - triangles:  { x0, y0, x1, y1, x2, y2, a, r, g, b }      (three points)
//    The push constant's `Role` selects the region: 0 lines, 1 dots, 2 triangles.
//    A line or dot draws 6 × count vertices — the expanded quad's two TRIANGLE_LIST
//    triangles, whose logical corner is `QuadCorner(SV_VertexID)` — and a triangle
//    draws 3 × count (`SV_VertexID % 3`). The screen-space expansion is the whole point: the CPU
//    record is two points per line and one per dot, so a dense lattice or a
//    high-poly wireframe never reaches the CPU's tessellator.

#include "Shared/ToolchainInterchange.slang.h"
#include "Shared/OverlayTransform.slang.h"

namespace Slate
{

static const Unsigned32 WorkspaceOverlayLineDraw       = 0u;
static const Unsigned32 WorkspaceOverlayDotDraw        = 1u;
static const Unsigned32 WorkspaceOverlayTriangleDraw   = 2u;
static const Unsigned32 WorkspaceOverlayGroundGridDraw = 3u;

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECORD SHAPES
//------------------------------------------------------------------------------------------------------------------------

// 📐 One CPU record per line: the two endpoints, the thickness, and the straight-alpha colour.
struct LineRecord
{
    Real32x2   Start;       // [px]
    Real32x2   End;         // [px]
    Real32     Thickness;   // [px]
    Real32     Padding[3];  // [-] - alignment to 48 bytes
    Real32x4   Colour;      // [-] - straight alpha, 0..1
};

// 📐 One CPU record per dot: the centre, the radius, and the colour.
struct DotRecord
{
    Real32x2   Centre;      // [px]
    Real32     Radius;      // [px]
    Real32     Padding;     // [-] - alignment to 24 bytes
    Real32x4   Colour;      // [-] - straight alpha, 0..1
};

// 📐 One CPU record per triangle: the three vertices and the colour.
struct TriangleRecord
{
    Real32x2   First;       // [px]
    Real32x2   Second;      // [px]
    Real32x2   Third;       // [px]
    Real32x2   Padding;     // [-] - alignment to 32 bytes
    Real32x4   Colour;      // [-] - straight alpha, 0..1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PUSH CONSTANT
//------------------------------------------------------------------------------------------------------------------------

// 📐 What one draw reads: which region, and the display size for the screen -> NDC transform.
//    🔴 Mode 0, 1 and 3 draw 6 vertices per record (corner = QuadCorner(VertexIndex)), triangle kind
//       draws 3 (corner = VertexIndex % 3). The record ordinal is VertexIndex / 6 (or / 3).
struct OverlayPushConstant
{
    Unsigned32 Draw;       // [-] - 0 lines, 1 dots, 2 triangles, 3 the analytic ground
    Real32     DisplayWidth;   // [px]
    Real32     DisplayHeight;  // [px]
    Real32     FadeRadiusMetres; // [m] - ground visibility radius around the camera

    // 📐 GROUND-GRID KIND — THE ANALYTIC GROUND. Everything the fragment stage needs to
    //    solve the ground plane per pixel: the eye, the camera basis, the
    //    frustum's tangents, and the lattice's own cell and presentation.
    //
    // 🔴 The previous grid marched line SEGMENTS on the CPU and handed them here
    //    as screen-space records. That is why it carried a near-plane split, a
    //    polyline buffer, an off-screen cull, a 1024-segment ceiling and a finite
    //    extent whose edge moved with the eye — nineteen RED markers over 1828
    //    lines, every one of them a consequence of tessellating a plane on a CPU.
    //    Solved per pixel there is no tessellation or segment budget. Its authored
    //    world extent and camera fade are analytic coverage, never generated geometry.
    Real32x4   EyeAndCell;       // [-] - xyz the eye in metres, w the base cell
    Real32x4   ForwardAndTanH;   // [-] - xyz the view direction, w tan(halfH)
    Real32x4   RightAndTanV;     // [-] - xyz the right axis,     w tan(halfV)
    Real32x4   UpAndPresent;     // [-] - xyz the up axis,        w presentation
    Real32x4   LatticeColour;    // [-] - rgb fine-line tone, w finite world extent in metres
    Real32x4   WeightsAndDot;    // [-] - x line weight, y dot radius, z major step, w axis mask
    Real32x4   LeafRect;         // [px] - x minX, y minY, z maxX, w maxY
};

[[vk::push_constant]]
ConstantBuffer<OverlayPushConstant> Push;

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE QUAD'S SIX VERTICES
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 A QUAD IS SIX VERTICES UNDER `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`, NOT FOUR.
//    The pipeline declares TRIANGLE_LIST and the list topology consumes vertices in
//    consecutive, non-overlapping triples — it has no notion of a shared corner. Every
//    quad mode here emitted FOUR vertices per record and the draw issued `4 × count`,
//    so the assembler took {0,1,2} as one triangle and then {3,4,5} — three of whose
//    corners belong to the NEXT record. Measured on a faithful CPU transcription of
//    this shader under that assembly:
//
//      ground-grid kind, the ground   4 vertices -> 1 triangle, 1 vertex dropped, leaf 50.0% covered
//      dot kind, three dots  12 vertices -> 4 triangles, 2 of them spanning two dots,
//                                         136 px covered where three whole dots are 192
//      line kind, three lines 12 vertices -> the middle line ABSENT and rows 82…217 holding
//                                         a diagonal smear built from two lines' corners
//
//    That is exactly the reported "lines render, dots don't" and the half-lit ground.
//    A strip topology would also fix it but would break the triangle mode, which is a
//    genuine list; the vertex count is what changes instead.
//
// 📝 The two triangles are {0,1,2} and {2,1,3} over the same four logical corners, so
//    every mode below keeps its own corner arithmetic untouched and merely reaches it
//    through this map. Corner 2 and corner 1 are repeated, which is what a list means.
Unsigned32 QuadCorner(Unsigned32 VertexIndex)
{
    const Unsigned32 Within = VertexIndex % 6u;

    if (Within == 0u) return 0u;
    if (Within == 1u) return 1u;
    if (Within == 2u) return 2u;
    if (Within == 3u) return 2u;
    if (Within == 4u) return 1u;

    return 3u;
}

// 📝 The three record regions of the one vertex buffer, bound as structured pools.
StructuredBuffer<LineRecord>     LinePool;      // [-] - region 0
StructuredBuffer<DotRecord>      DotPool;       // [-] - region 1
StructuredBuffer<TriangleRecord> TrianglePool;  // [-] - region 2

struct WorkspaceOverlayVertexOutput
{
    Real32x4   Position : SV_Position;
    Real32x4   Colour   : COLOR0;
    Real32x2   Local    : TEXCOORD0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

// 📐 The entry is named `main`: slangc emits the name the [shader(...)] attribute carries, and the
//    engine's ShaderCodec::Stage always binds the "main" entry of a lowered module.
// 🔴 Every corner is computed from `SV_VertexID` alone — no per-vertex attributes exist. The draw
//    counts ARE the record counts multiplied by the corner count, so the CPU record stays minimal.
[shader("vertex")]
WorkspaceOverlayVertexOutput main(Unsigned32 VertexIndex : SV_VertexID)
{
    WorkspaceOverlayVertexOutput Output;

    // 📐 The viewport transform, SHARED with the harness: `OverlayNdcX/Y` live in
    //    `Shared/OverlayTransform.slang.h`, compiled by both the shader toolchain and the host.
    Real32x2 Position = Real32x2(0.0, 0.0);
    Real32x4 Colour   = Real32x4(1.0, 1.0, 1.0, 1.0);
    Real32x2 Local    = Real32x2(0.0, 0.0);

    if (Push.Draw == WorkspaceOverlayLineDraw)
    {
        // 📐 The line's four corners are the endpoints widened by half the thickness along the
        //    segment's normal. The normal is computed here, keeping the CPU record at four floats.
        const Unsigned32 Record = VertexIndex / 6u;
        const Unsigned32 Corner = QuadCorner(VertexIndex);

        const LineRecord Line = LinePool[Record];
        const Real32x2 Direction = Line.End - Line.Start;
        const Real32    Length    = sqrt(dot(Direction, Direction));
        const Real32x2 Normal    = (Length > 0.0001)
                                 ? Real32x2(-Direction.y / Length, Direction.x / Length)
                                 : Real32x2(0.0, 1.0);

        const Real32 Half = Line.Thickness * 0.5;
        Position = (Corner < 2u) ? Line.Start : Line.End;
        const Real32 Side = ((Corner == 1u) || (Corner == 3u)) ? 1.0 : -1.0;
        Position += Normal * (Half * Side);
        Colour    = Line.Colour;
    }
    else if (Push.Draw == WorkspaceOverlayDotDraw)
    {
        // 📐 The dot's four corners are the centre widened by the radius along both axes.
        const Unsigned32 Record = VertexIndex / 6u;
        const Unsigned32 Corner = QuadCorner(VertexIndex);

        const DotRecord Dot = DotPool[Record];
        const Real32 X = ((Corner == 1u) || (Corner == 3u)) ?  1.0 : -1.0;
        const Real32 Y = (Corner >= 2u)                       ?  1.0 : -1.0;

        Local    = Real32x2(X, Y);
        Position = Dot.Centre + Local * Dot.Radius;
        Colour   = Dot.Colour;
    }
    else if (Push.Draw == WorkspaceOverlayGroundGridDraw)
    {
        // 📐 The analytic ground is ONE quad over the viewport leaf; the fragment
        //    stage decides what each pixel of it is. Four corners from the
        //    vertex ordinal alone, spanning LeafRect.
        const Unsigned32 Corner = QuadCorner(VertexIndex);
        const Real32 X = ((Corner == 1u) || (Corner == 3u)) ? 1.0 : 0.0;
        const Real32 Y = (Corner >= 2u)                     ? 1.0 : 0.0;

        const Real32 ScreenX = lerp(Push.LeafRect.x, Push.LeafRect.z, X);
        const Real32 ScreenY = lerp(Push.LeafRect.y, Push.LeafRect.w, Y);

        Position = Real32x2(ScreenX, ScreenY);
        Colour   = Real32x4(X, Y, 0.0, 1.0);   // the fragment stage reads the leaf corner (0..1)
    }
    else
    {
        // 📐 The triangle's three vertices pass through untouched.
        const Unsigned32 Record = VertexIndex / 3u;
        const Unsigned32 Corner = VertexIndex % 3u;

        const TriangleRecord Triangle = TrianglePool[Record];
        Position = (Corner == 0u) ? Triangle.First
                 : (Corner == 1u) ? Triangle.Second : Triangle.Third;
        Colour   = Triangle.Colour;
    }

    Output.Position = Real32x4(OverlayNdcX(Position.x, Push.DisplayWidth),
                               OverlayNdcY(Position.y, Push.DisplayHeight),
                               0.0, 1.0);
    Output.Colour   = Colour;
    Output.Local    = Local;

    return Output;
}

} // namespace Slate
