//============================================================================================================================================
//                                                           WORKSPACECADVERTEX.SLANG
//============================================================================================================================================
// 🧩 The parametric workspace CAD pass's vertex stage: expands the bounded CPU CAD packet into triangles on the
//    GPU and fits the exact sketch plane into the current viewport leaf.

#include "Shared/ToolchainInterchange.slang.h"
#include "Shared/OverlayTransform.slang.h"
#include "Shared/WorkspaceCadNearClip.slang.h"

namespace Slate
{

static const Unsigned32 WorkspaceCadSegmentDraw = 0u;
static const Unsigned32 WorkspaceCadFillDraw    = 1u;
static const Unsigned32 WorkspaceCadMarkerDraw  = 2u;

struct SegmentRecord
{
    Real32x2 Start;
    Real32x2 End;
    Real32 Thickness;
    Real32 Padding[3];
    Real32x4 Colour;
};

struct FillRecord
{
    Real32x2 First;
    Real32x2 Second;
    Real32x2 Third;
    Real32x2 Padding;
    Real32x4 Colour;
};

struct MarkerRecord
{
    Real32x2 Centre;
    Real32 Radius;
    Unsigned32 Subject;
    Real32x4 Colour;
};

struct CadPushConstant
{
    Unsigned32 Draw;
    Real32 DisplayWidth;
    Real32 DisplayHeight;
    Real32 Padding;
    Real32x4 Projection0;
    Real32x4 Projection1;
    Real32x4 Projection2;
};

[[vk::push_constant]]
ConstantBuffer<CadPushConstant> Push;

StructuredBuffer<SegmentRecord> SegmentPool;
StructuredBuffer<FillRecord> FillPool;
StructuredBuffer<MarkerRecord> MarkerPool;

struct WorkspaceCadVertexOutput
{
    Real32x4 Position : SV_Position;
    Real32x4 Colour   : COLOR0;
    Real32x2 Local    : TEXCOORD0;
    Unsigned32 Draw    : TEXCOORD1;
    Unsigned32 Subject : TEXCOORD2;
};

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

Unsigned32 FillCorner(Unsigned32 VertexIndex)
{
    const Unsigned32 Within = VertexIndex % 6u;
    if (Within == 0u) return 0u;
    if (Within == 1u) return 1u;
    if (Within == 2u) return 2u;
    if (Within == 3u) return 0u;
    if (Within == 4u) return 2u;
    return 3u;
}

WorkspaceCadProjectedPoint ProjectPoint(Real32 Along, Real32 Across)
{
    const Real32x4 Projected = Push.Projection0 + Push.Projection1 * Along + Push.Projection2 * Across;
    WorkspaceCadProjectedPoint Point;
    Point.X = Projected.x;
    Point.Y = Projected.y;
    Point.W = Projected.w;
    return Point;
}

Real32x2 ProjectScreen(WorkspaceCadProjectedPoint Point)
{
    const WorkspaceCadScreenPoint Screen = ResolveWorkspaceCadScreenPoint(Point);
    return Real32x2(Screen.X, Screen.Y);
}

[shader("vertex")]
WorkspaceCadVertexOutput main(Unsigned32 VertexIndex : SV_VertexID)
{
    WorkspaceCadVertexOutput Output;
    Output.Position = Real32x4(0.0, 0.0, 0.0, 1.0);
    Output.Colour = Real32x4(1.0, 1.0, 1.0, 1.0);
    Output.Local = Real32x2(0.0, 0.0);
    Output.Draw = Push.Draw;
    Output.Subject = 0u;

    Real32x2 Screen = Real32x2(0.0, 0.0);
    bool Valid = true;

    if (Push.Draw == WorkspaceCadFillDraw)
    {
        const Unsigned32 Record = VertexIndex / 6u;
        const Unsigned32 Within = VertexIndex % 6u;
        const Unsigned32 Corner = FillCorner(VertexIndex);
        const FillRecord Triangle = FillPool[Record];
        WorkspaceCadProjectedPoint Clipped[4];
        const Unsigned32 Count = ClipWorkspaceCadFillTriangleNear(
            ProjectPoint(Triangle.First.x,  Triangle.First.y),
            ProjectPoint(Triangle.Second.x, Triangle.Second.y),
            ProjectPoint(Triangle.Third.x,  Triangle.Third.y),
            Clipped);

        Valid = false;
        if (Count == 3u)
        {
            if (Within < 3u)
            {
                Screen = ProjectScreen(Clipped[Within]);
                Valid = true;
            }
        }
        else if (Count == 4u)
        {
            Screen = ProjectScreen(Clipped[Corner]);
            Valid = true;
        }
        Output.Colour = Triangle.Colour;
    }
    else if (Push.Draw == WorkspaceCadSegmentDraw)
    {
        const Unsigned32 Record = VertexIndex / 6u;
        const Unsigned32 Corner = QuadCorner(VertexIndex);
        const SegmentRecord Segment = SegmentPool[Record];
        WorkspaceCadProjectedPoint StartPoint = ProjectPoint(Segment.Start.x, Segment.Start.y);
        WorkspaceCadProjectedPoint EndPoint = ProjectPoint(Segment.End.x, Segment.End.y);
        Valid = ClipWorkspaceCadSegmentNear(StartPoint, EndPoint);
        if (Valid)
        {
            const Real32x2 Start = ProjectScreen(StartPoint);
            const Real32x2 End = ProjectScreen(EndPoint);
            const Real32x2 Direction = End - Start;
            const Real32 Length = sqrt(dot(Direction, Direction));
            const Real32x2 Normal = Length > 0.0001 ? Real32x2(-Direction.y / Length, Direction.x / Length)
                                                    : Real32x2(0.0, 1.0);
            const Real32 Side = ((Corner == 1u) || (Corner == 3u)) ? 1.0 : -1.0;
            Screen = (Corner < 2u) ? Start : End;
            Screen += Normal * (Segment.Thickness * 0.5 * Side);
        }
        Output.Colour = Segment.Colour;
    }
    else
    {
        const Unsigned32 Record = VertexIndex / 6u;
        const Unsigned32 Corner = QuadCorner(VertexIndex);
        const MarkerRecord Marker = MarkerPool[Record];
        const Real32 LocalX = ((Corner == 1u) || (Corner == 3u)) ? 1.0 : -1.0;
        const Real32 LocalY = Corner >= 2u ? 1.0 : -1.0;
        Output.Local = Real32x2(LocalX, LocalY);
        Output.Subject = Marker.Subject;
        const WorkspaceCadProjectedPoint CentrePoint = ProjectPoint(Marker.Centre.x, Marker.Centre.y);
        Valid = WorkspaceCadProjectedFront(CentrePoint);
        if (Valid)
        {
            const Real32x2 Centre = ProjectScreen(CentrePoint);
            Screen = Centre + Output.Local * Marker.Radius;
        }
        Output.Colour = Marker.Colour;
    }

    Output.Position = Valid ? Real32x4(OverlayNdcX(Screen.x, Push.DisplayWidth),
                                       OverlayNdcY(Screen.y, Push.DisplayHeight),
                                       0.0, 1.0)
                            : Real32x4(-2.0, -2.0, 0.0, 1.0);
    return Output;
}

} // namespace Slate
