//============================================================================================================================================
//                                             WORLDSKETCHRENDERINGPROJECTION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorldSketchRenderingProjection/Api/WorldSketchRenderingProjection.h"

#include "Shared/WorkspaceCadNearClip.slang.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Slate
{

namespace
{

struct PlanarVertex
{
    double Along = 0.0;
    double Across = 0.0;
    SpatialPoint World = {};
};

WorkspaceCadProjectedPoint ResolveProjectedPoint(const ResolvedCamera& Camera,
                                                 const PlaneExtent& Extent,
                                                 const SpatialPoint& Position)
{
    WorkspaceCadProjectedPoint Point = {};
    if (!Camera.Perspective)
    {
        float ScreenX = 0.0f;
        float ScreenY = 0.0f;
        static_cast<void>(ProjectFromCamera(Camera, Extent, Position, ScreenX, ScreenY));
        Point.X = ScreenX;
        Point.Y = ScreenY;
        Point.W = 1.0f;
        return Point;
    }

    const SpatialDirection EyeToPoint = Difference(Camera.Frame.Eye, Position);
    const double CameraX = Dot(EyeToPoint, Camera.Frame.Right);
    const double CameraY = Dot(EyeToPoint, Camera.Frame.Up);
    const double CameraZ = Dot(EyeToPoint, Camera.Frame.Forward);
    const double TanHalf = std::tan(Camera.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
    const double Focal = (Extent.Height() * 0.5) / std::max(TanHalf, 1.0e-6);
    const double CentreX = Extent.MinimumX + Extent.Width() * 0.5;
    const double CentreY = Extent.MinimumY + Extent.Height() * 0.5;

    Point.X = static_cast<Real32>(CentreX * CameraZ + Focal * CameraX);
    Point.Y = static_cast<Real32>(CentreY * CameraZ - Focal * CameraY);
    Point.W = static_cast<Real32>(CameraZ);
    return Point;
}

void AppendClippedSegment(const ResolvedCamera& Camera,
                          const PlaneExtent& Extent,
                          const SpatialPoint& Start,
                          const SpatialPoint& End,
                          Unsigned32 Packed,
                          Real32 Thickness,
                          WorkspaceCadPacket& Delivered)
{
    WorkspaceCadProjectedPoint First = ResolveProjectedPoint(Camera, Extent, Start);
    WorkspaceCadProjectedPoint Second = ResolveProjectedPoint(Camera, Extent, End);
    if (Camera.Perspective && !ClipWorkspaceCadSegmentNear(First, Second))
        return;

    const WorkspaceCadScreenPoint A = ResolveWorkspaceCadScreenPoint(First);
    const WorkspaceCadScreenPoint B = ResolveWorkspaceCadScreenPoint(Second);
    Delivered.AddSegment(A.X, A.Y, B.X, B.Y, Packed, Thickness);
}

void AppendClippedTriangle(const ResolvedCamera& Camera,
                           const PlaneExtent& Extent,
                           const SpatialPoint& A,
                           const SpatialPoint& B,
                           const SpatialPoint& C,
                           Unsigned32 Packed,
                           WorkspaceCadPacket& Delivered)
{
    WorkspaceCadProjectedPoint First = ResolveProjectedPoint(Camera, Extent, A);
    WorkspaceCadProjectedPoint Second = ResolveProjectedPoint(Camera, Extent, B);
    WorkspaceCadProjectedPoint Third = ResolveProjectedPoint(Camera, Extent, C);

    if (!Camera.Perspective)
    {
        const WorkspaceCadScreenPoint ScreenA = ResolveWorkspaceCadScreenPoint(First);
        const WorkspaceCadScreenPoint ScreenB = ResolveWorkspaceCadScreenPoint(Second);
        const WorkspaceCadScreenPoint ScreenC = ResolveWorkspaceCadScreenPoint(Third);
        Delivered.AddFill(ScreenA.X, ScreenA.Y, ScreenB.X, ScreenB.Y, ScreenC.X, ScreenC.Y, Packed);
        return;
    }

    WorkspaceCadProjectedPoint Clipped[4] = {};
    const Unsigned32 Count = ClipWorkspaceCadFillTriangleNear(First, Second, Third, Clipped);
    if (Count == 3u)
    {
        const WorkspaceCadScreenPoint ScreenA = ResolveWorkspaceCadScreenPoint(Clipped[0u]);
        const WorkspaceCadScreenPoint ScreenB = ResolveWorkspaceCadScreenPoint(Clipped[1u]);
        const WorkspaceCadScreenPoint ScreenC = ResolveWorkspaceCadScreenPoint(Clipped[2u]);
        Delivered.AddFill(ScreenA.X, ScreenA.Y, ScreenB.X, ScreenB.Y, ScreenC.X, ScreenC.Y, Packed);
        return;
    }
    if (Count == 4u)
    {
        const WorkspaceCadScreenPoint ScreenA = ResolveWorkspaceCadScreenPoint(Clipped[0u]);
        const WorkspaceCadScreenPoint ScreenB = ResolveWorkspaceCadScreenPoint(Clipped[1u]);
        const WorkspaceCadScreenPoint ScreenC = ResolveWorkspaceCadScreenPoint(Clipped[2u]);
        const WorkspaceCadScreenPoint ScreenD = ResolveWorkspaceCadScreenPoint(Clipped[3u]);
        Delivered.AddFill(ScreenA.X, ScreenA.Y, ScreenB.X, ScreenB.Y, ScreenC.X, ScreenC.Y, Packed);
        Delivered.AddFill(ScreenA.X, ScreenA.Y, ScreenC.X, ScreenC.Y, ScreenD.X, ScreenD.Y, Packed);
    }
}

Unsigned32 ResolveViewportCurveStepFloor(const ResolvedCamera& Camera,
                                          const CurveSpecification& Geometry,
                                          Unsigned32 Floor)
{
    // The geometry resolver supplies a curvature-aware floor. This second, view-aware floor prevents
    // close orbit shots from exposing the underlying parameter chords: orthographic scale is pixels per
    // world unit, while perspective detail falls with eye distance. The multiplier is bounded so a tiny
    // zoom cannot exhaust the packet budget.
    double Detail = 1.0;
    if (!Camera.Perspective)
    {
        Detail = std::sqrt(std::max(Camera.OrthoScale, 1.0) / 48.0);
    }
    else
    {
        const SpatialPoint& Eye = Camera.Frame.Eye;
        const double Distance = std::sqrt(Eye.Left * Eye.Left + Eye.Up * Eye.Up + Eye.Forward * Eye.Forward);
        Detail = std::sqrt(12.0 / std::max(Distance, 0.25));
    }

    return ResolveCurveStepCountForDetail(Geometry, Floor, Detail);
}

bool ResolveCurvePolyline(const WorldSketchStructure& Declared,
                          WorldCurveName Subject,
                          const ResolvedCamera& Camera,
                          Unsigned32 StepFloor,
                          std::vector<SpatialPoint>& Delivered)
{
    Delivered.clear();
    const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    AppendCurvePolyline(Held->Geometry, Delivered,
                        ResolveViewportCurveStepFloor(Camera, Held->Geometry, StepFloor));
    return Delivered.size() >= 2u;
}

double SignedArea(const std::vector<PlanarVertex>& Outline)
{
    if (Outline.size() < 3u)
        return 0.0;

    double Sum = 0.0;
    for (std::size_t Index = 0u; Index < Outline.size(); ++Index)
    {
        const std::size_t Next = (Index + 1u) % Outline.size();
        Sum += Outline[Index].Along * Outline[Next].Across
             - Outline[Next].Along * Outline[Index].Across;
    }
    return Sum * 0.5;
}

double TurnOf(const PlanarVertex& A, const PlanarVertex& B, const PlanarVertex& C)
{
    return (B.Along - A.Along) * (C.Across - A.Across)
         - (B.Across - A.Across) * (C.Along - A.Along);
}

bool WithinTriangle(const PlanarVertex& A,
                    const PlanarVertex& B,
                    const PlanarVertex& C,
                    const PlanarVertex& Point)
{
    const double First = TurnOf(A, B, Point);
    const double Second = TurnOf(B, C, Point);
    const double Third = TurnOf(C, A, Point);
    const bool AnyNegative = First < 0.0 || Second < 0.0 || Third < 0.0;
    const bool AnyPositive = First > 0.0 || Second > 0.0 || Third > 0.0;
    return !(AnyNegative && AnyPositive);
}

bool ClipEars(const std::vector<PlanarVertex>& Outline,
              std::vector<Unsigned32>& Delivered)
{
    Delivered.clear();
    if (Outline.size() < 3u)
        return false;

    std::vector<Unsigned32> Remaining;
    Remaining.reserve(Outline.size());
    for (Unsigned32 Index = 0u; Index < static_cast<Unsigned32>(Outline.size()); ++Index)
        Remaining.push_back(Index);

    std::size_t Attempts = Remaining.size() * Remaining.size() + 4u;
    while (Remaining.size() > 3u && Attempts-- > 0u)
    {
        bool Clipped = false;
        for (std::size_t Position = 0u; Position < Remaining.size(); ++Position)
        {
            const Unsigned32 Previous = Remaining[(Position + Remaining.size() - 1u) % Remaining.size()];
            const Unsigned32 Current = Remaining[Position];
            const Unsigned32 Next = Remaining[(Position + 1u) % Remaining.size()];
            if (TurnOf(Outline[Previous], Outline[Current], Outline[Next]) <= 0.0)
                continue;

            bool Swallows = false;
            for (const Unsigned32 Other : Remaining)
            {
                if (Other == Previous || Other == Current || Other == Next)
                    continue;
                if (WithinTriangle(Outline[Previous], Outline[Current], Outline[Next], Outline[Other]))
                {
                    Swallows = true;
                    break;
                }
            }
            if (Swallows)
                continue;

            Delivered.push_back(Previous);
            Delivered.push_back(Current);
            Delivered.push_back(Next);
            Remaining.erase(Remaining.begin() + static_cast<std::ptrdiff_t>(Position));
            Clipped = true;
            break;
        }

        if (!Clipped)
            return false;
    }

    if (Remaining.size() != 3u)
        return false;

    Delivered.push_back(Remaining[0u]);
    Delivered.push_back(Remaining[1u]);
    Delivered.push_back(Remaining[2u]);
    return true;
}

/// 🧩 Flattens a loop's outline into its own plane, wound anticlockwise.
std::vector<PlanarVertex> FlattenOutline(const WorldPlacementFrame& Frame,
                                         const std::vector<SpatialPoint>& Points)
{
    std::vector<PlanarVertex> Outline;
    Outline.reserve(Points.size());
    for (const SpatialPoint& Point : Points)
    {
        double Along = 0.0;
        double Across = 0.0;
        ResolveWorldPlacementCoordinates(Frame, Point, Along, Across);
        Outline.push_back({ Along, Across, Point });
    }

    if (SignedArea(Outline) < 0.0)
        std::reverse(Outline.begin(), Outline.end());
    return Outline;
}

/// 🧩 The vertex of a hole nearest a vertex of the outline it sits in, as a pair of indices.
void FindBridge(const std::vector<PlanarVertex>& Outline,
                const std::vector<PlanarVertex>& Hole,
                std::size_t& OuterAt,
                std::size_t& InnerAt)
{
    OuterAt = 0u;
    InnerAt = 0u;
    double Nearest = std::numeric_limits<double>::max();

    for (std::size_t Outer = 0u; Outer < Outline.size(); ++Outer)
        for (std::size_t Inner = 0u; Inner < Hole.size(); ++Inner)
        {
            const double Across = Outline[Outer].Across - Hole[Inner].Across;
            const double Along = Outline[Outer].Along - Hole[Inner].Along;
            const double Distance = Along * Along + Across * Across;
            if (Distance < Nearest)
            {
                Nearest = Distance;
                OuterAt = Outer;
                InnerAt = Inner;
            }
        }
}

/// 🧩 Cuts a hole into an outline, giving one outline that walks in and back out again.
/// 🔴 THIS IS HOW A CIRCLE INSIDE A CIRCLE BECOMES A TUBE. Ear clipping fills a single closed outline
///    and knows nothing of holes, so the hole is stitched INTO the outline: walk the outer ring to the
///    nearest vertex, cross the bridge, walk the hole the OTHER WAY round, and cross back. The result is
///    one degenerate-but-simple outline whose interior is the material between the rings, which ear
///    clipping then handles with no idea that a hole was ever involved.
/// 📝 The hole is reversed because a hole must wind against its container. Two rings wound the same way
///    would leave the bridge crossing itself and the ears would clip the hole shut.
void StitchHole(std::vector<PlanarVertex>& Outline, const std::vector<PlanarVertex>& Hole)
{
    if (Hole.size() < 3u || Outline.size() < 3u)
        return;

    std::vector<PlanarVertex> Reversed(Hole.rbegin(), Hole.rend());

    std::size_t OuterAt = 0u;
    std::size_t InnerAt = 0u;
    FindBridge(Outline, Reversed, OuterAt, InnerAt);

    std::vector<PlanarVertex> Stitched;
    Stitched.reserve(Outline.size() + Reversed.size() + 2u);

    for (std::size_t Step = 0u; Step <= OuterAt; ++Step)
        Stitched.push_back(Outline[Step]);
    for (std::size_t Step = 0u; Step < Reversed.size(); ++Step)
        Stitched.push_back(Reversed[(InnerAt + Step) % Reversed.size()]);
    Stitched.push_back(Reversed[InnerAt]);
    for (std::size_t Step = OuterAt; Step < Outline.size(); ++Step)
        Stitched.push_back(Outline[Step]);

    Outline.swap(Stitched);
}

void AppendFillForLoop(const WorldLoopAnalysisRecord& Loop,
                       const std::vector<WorldLoopAnalysisRecord>& Everything,
                       const ResolvedCamera& Camera,
                       const PlaneExtent& Extent,
                       Unsigned32 Packed,
                       WorkspaceCadPacket& Delivered)
{
    if (!Loop.FillEligible || !Loop.SupportFrame.Declared() || Loop.Outline.size() < 3u)
        return;

    std::vector<PlanarVertex> Outline = FlattenOutline(Loop.SupportFrame, Loop.Outline);

    // 🔴 THE HOLES ARE CUT BEFORE THE EARS ARE CLIPPED, never painted over afterwards. The fill is
    //    transparent, so a hole drawn on top in the background colour would be a visible disc rather
    //    than a gap, and would still be there when the background changed.
    for (const WorldLoopAnalysisRecord& Other : Everything)
    {
        if (!Other.Hole || Other.Outline.size() < 3u)
            continue;
        if (Other.Container.IssuedIndex != Loop.Loop.IssuedIndex)
            continue;

        StitchHole(Outline, FlattenOutline(Loop.SupportFrame, Other.Outline));
    }

    std::vector<Unsigned32> Triangles;
    if (!ClipEars(Outline, Triangles))
        return;

    for (std::size_t Index = 0u; Index + 2u < Triangles.size(); Index += 3u)
        AppendClippedTriangle(Camera, Extent,
                              Outline[Triangles[Index]].World,
                              Outline[Triangles[Index + 1u]].World,
                              Outline[Triangles[Index + 2u]].World,
                              Packed, Delivered);
}

} // namespace

WorkspaceCadProjection ResolveWorldSketchScreenProjection(std::uint32_t DisplayWidth,
                                                         std::uint32_t DisplayHeight)
{
    WorkspaceCadProjection Projection = {};
    Projection.DisplayWidth = static_cast<float>(DisplayWidth);
    Projection.DisplayHeight = static_cast<float>(DisplayHeight);
    Projection.Projection0[0] = 0.0f;
    Projection.Projection0[1] = 0.0f;
    Projection.Projection0[2] = 0.0f;
    Projection.Projection0[3] = 1.0f;
    Projection.Projection1[0] = 1.0f;
    Projection.Projection1[1] = 0.0f;
    Projection.Projection1[2] = 0.0f;
    Projection.Projection1[3] = 0.0f;
    Projection.Projection2[0] = 0.0f;
    Projection.Projection2[1] = 1.0f;
    Projection.Projection2[2] = 0.0f;
    Projection.Projection2[3] = 0.0f;
    return Projection;
}

Deliver<bool> ProjectWorldSketchRendering(const WorldSketchStructure& Declared,
                                         const ResolvedCamera& Camera,
                                         const PlaneExtent& PhysicalExtent,
                                         WorkspaceCadPacket& Delivered,
                                         const WorldSelectionSet& Selection,
                                         const WorldSketchRenderingStyle& Style,
                                         double ClosureTolerance,
                                         double CoplanarTolerance)
{
    Delivered.Reset();

    std::vector<SpatialPoint> Polyline;
    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        if (!ResolveCurvePolyline(Declared, { CurveIndex }, Camera, Style.CurveSteps, Polyline))
            continue;

        bool IsSelectedCurve = false;
        for (const WorldPick& Pick : Selection.Items)
        {
            if (Pick.Subject == WorldPickSubject::Curve && Pick.Curve.IssuedIndex == CurveIndex)
            {
                IsSelectedCurve = true;
                break;
            }
            if (Pick.Subject == WorldPickSubject::Point && Pick.Curve.IssuedIndex == CurveIndex)
            {
                IsSelectedCurve = true;
                break;
            }
            if (Pick.Subject == WorldPickSubject::Control && Pick.Curve.IssuedIndex == CurveIndex)
            {
                IsSelectedCurve = true;
                break;
            }
            if (Pick.Subject == WorldPickSubject::Loop && Pick.Loop.Assigned())
            {
                const DeclaredWorldLoop* Loop = Declared.Resolve(Pick.Loop);
                if (Loop != nullptr)
                {
                    for (const WorldCurveUse& Use : Loop->Traversal)
                    {
                        if (Use.TraversedCurve.IssuedIndex == CurveIndex)
                        {
                            IsSelectedCurve = true;
                            break;
                        }
                    }
                }
                if (IsSelectedCurve)
                    break;
            }
        }

        const Unsigned32 LineColour = IsSelectedCurve ? Style.SelectedCurveColour : Style.CurveColour;
        const Real32 LineThickness = IsSelectedCurve ? Style.SelectedCurveThickness : Style.CurveThickness;

        for (std::size_t PointIndex = 0u; PointIndex + 1u < Polyline.size(); ++PointIndex)
            AppendClippedSegment(Camera, PhysicalExtent,
                                 Polyline[PointIndex], Polyline[PointIndex + 1u],
                                 LineColour, LineThickness, Delivered);
    }

    const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Declared, Style.CurveSteps,
                                                          ClosureTolerance, CoplanarTolerance);
    for (const WorldLoopAnalysisRecord& Loop : Analysis.Loops)
    {
        bool IsSelectedLoop = false;
        for (const WorldPick& Pick : Selection.Items)
        {
            if (Pick.Subject == WorldPickSubject::Loop && Pick.Loop.IssuedIndex == Loop.Loop.IssuedIndex)
            {
                IsSelectedLoop = true;
                break;
            }
        }
        AppendFillForLoop(Loop, Analysis.Loops, Camera, PhysicalExtent,
                          IsSelectedLoop ? Style.SelectedFillColour : Style.FillColour,
                          Delivered);
    }

    return Deliver<bool>::Result(true);
}

} // namespace Slate
