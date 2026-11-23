//============================================================================================================================================
//                                                     TOPOLOGYSELECTION.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Reference/TopologySelection/Api/TopologySelection.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    double DistanceSquared(const SpatialPoint& LeftPoint,
                           const SpatialPoint& RightPoint)
    {
        return LengthSquared(Difference(LeftPoint, RightPoint));
    }

    double DistancePointSegmentSquared(const SpatialPoint& Probe,
                                       const SpatialPoint& StartPoint,
                                       const SpatialPoint& EndPoint)
    {
        const SpatialDirection Span = Difference(StartPoint, EndPoint);
        const SpatialDirection Offset = Difference(StartPoint, Probe);
        const double SpanLengthSquared = LengthSquared(Span);
        if (SpanLengthSquared <= 1.0e-18)
            return DistanceSquared(Probe, StartPoint);

        const double Parameter = std::clamp((Offset.Left * Span.Left + Offset.Up * Span.Up + Offset.Forward * Span.Forward) / SpanLengthSquared,
                                            0.0, 1.0);
        const SpatialPoint Closest = Added(StartPoint, Scaled(Span, Parameter));
        return DistanceSquared(Probe, Closest);
    }

    EdgeSpanNameInFeature EncodeSpanName(LoopName Loop, std::uint32_t LocalIndex)
    {
        return { (Loop.IssuedIndex << 12u) | ((LocalIndex + 1u) & 0xFFFu) };
    }

    bool ResolveLoopPolygon(const SolidStructure& Declared,
                            LoopName Subject,
                            std::vector<SpatialPoint>& Traversal)
    {
        Traversal.clear();

        std::vector<VertexName> Vertices;
        if (!Declared.ResolveLoopVertices(Subject, Vertices))
            return false;

        const SolidView View = Declared.Resolve();
        if (View.Vertices == nullptr)
            return false;

        Traversal.reserve(Vertices.size());
        for (VertexName Vertex : Vertices)
        {
            if (!Vertex.Assigned() || Vertex.IssuedIndex > View.Vertices->size())
                return false;
            Traversal.push_back((*View.Vertices)[Vertex.IssuedIndex - 1u].Position);
        }

        return Traversal.size() >= 3u;
    }

    int DominantPlaneAxis(const SpatialDirection& Normal)
    {
        const double AbsoluteX = std::fabs(Normal.Left);
        const double AbsoluteY = std::fabs(Normal.Up);
        const double AbsoluteZ = std::fabs(Normal.Forward);
        if (AbsoluteX >= AbsoluteY && AbsoluteX >= AbsoluteZ) return 0;
        if (AbsoluteY >= AbsoluteX && AbsoluteY >= AbsoluteZ) return 1;
        return 2;
    }

    PlanarPoint ProjectToPlane(const SpatialPoint& Position,
                               int Axis)
    {
        switch (Axis)
        {
            case 0: return { Position.Up, Position.Forward };
            case 1: return { Position.Left, Position.Forward };
            default:return { Position.Left, Position.Up };
        }
    }

    bool PointInsidePolygon(const PlanarPoint& Probe,
                            const std::vector<PlanarPoint>& Polygon)
    {
        bool Inside = false;
        for (std::size_t Index = 0u, Prior = Polygon.size() - 1u; Index < Polygon.size(); Prior = Index++)
        {
            const PlanarPoint& Current = Polygon[Index];
            const PlanarPoint& Previous = Polygon[Prior];
            if (((Current.Across > Probe.Across) != (Previous.Across > Probe.Across))
             && (Probe.Along < (Previous.Along - Current.Along) * (Probe.Across - Current.Across)
                                      / (Previous.Across - Current.Across) + Current.Along))
            {
                Inside = !Inside;
            }
        }
        return Inside;
    }

    bool FaceContainsPoint(const SolidStructure& Declared,
                           const DeclaredFace& Face,
                           const SpatialPoint& Probe)
    {
        const SolidView View = Declared.Resolve();
        if (View.Surfaces == nullptr)
            return false;
        if (!Face.SupportingSurface.Assigned() || Face.SupportingSurface.IssuedIndex > View.Surfaces->size())
            return false;

        const SurfaceSpecification& Surface = (*View.Surfaces)[Face.SupportingSurface.IssuedIndex - 1u].Geometry;
        if (Surface.Subject() != SurfaceForm::Plane)
            return false;

        const SpatialDirection Normal = Normalize(Surface.HeldPlane().Normal);
        const SpatialDirection Offset = Difference(Surface.HeldPlane().Origin, Probe);
        if (std::fabs(Dot(Normal, Offset)) > 1.0e-6)
            return false;

        const int Axis = DominantPlaneAxis(Normal);
        const PlanarPoint FlatProbe = ProjectToPlane(Probe, Axis);

        bool OuterHit = false;
        for (const DeclaredFaceLoop& FaceLoop : Face.LoopSet)
        {
            std::vector<SpatialPoint> LoopWorld;
            if (!ResolveLoopPolygon(Declared, FaceLoop.TraversedLoop, LoopWorld))
                return false;
            std::vector<PlanarPoint> FlatLoop;
            FlatLoop.reserve(LoopWorld.size());
            for (const SpatialPoint& Position : LoopWorld)
                FlatLoop.push_back(ProjectToPlane(Position, Axis));

            const DeclaredLoop* HeldLoop = View.Loops != nullptr && FaceLoop.TraversedLoop.Assigned() && FaceLoop.TraversedLoop.IssuedIndex <= View.Loops->size()
                                         ? &(*View.Loops)[FaceLoop.TraversedLoop.IssuedIndex - 1u]
                                         : nullptr;
            if (HeldLoop == nullptr)
                return false;

            if (HeldLoop->Standing == LoopStanding::Outer)
                OuterHit = PointInsidePolygon(FlatProbe, FlatLoop);
            else if (PointInsidePolygon(FlatProbe, FlatLoop))
                return false;
        }

        return OuterHit;
    }
}

bool ResolveEdgeSpans(const SolidStructure& Declared,
                      std::vector<EdgeSpanPlacement>& Resolved)
{
    Resolved.clear();

    const SolidView View = Declared.Resolve();
    if (View.Loops == nullptr || View.Edges == nullptr)
        return false;

    for (std::uint32_t LoopIndex = 1u; LoopIndex <= View.Loops->size(); ++LoopIndex)
    {
        std::vector<EdgeName> LoopEdges;
        if (!Declared.ResolveLoopEdges({ LoopIndex }, LoopEdges) || LoopEdges.empty())
            continue;

        std::uint32_t SpanIndex = 0u;
        EdgeSpanPlacement Current = {};
        Current.Loop = { LoopIndex };

        for (std::size_t EdgeIndex = 0u; EdgeIndex < LoopEdges.size(); ++EdgeIndex)
        {
            const DeclaredEdge& Edge = (*View.Edges)[LoopEdges[EdgeIndex].IssuedIndex - 1u];
            if (!Current.SupportingCurve.Assigned())
                Current.SupportingCurve = Edge.SupportingCurve;

            if (Current.SupportingCurve.IssuedIndex != Edge.SupportingCurve.IssuedIndex)
            {
                Current.Name = EncodeSpanName({ LoopIndex }, SpanIndex++);
                Resolved.push_back(Current);
                Current = {};
                Current.Loop = { LoopIndex };
                Current.SupportingCurve = Edge.SupportingCurve;
            }

            Current.EdgeSet.push_back(LoopEdges[EdgeIndex]);
        }

        if (!Current.EdgeSet.empty())
        {
            Current.Name = EncodeSpanName({ LoopIndex }, SpanIndex++);
            Resolved.push_back(Current);
        }
    }

    return !Resolved.empty();
}

bool ResolveNearestVertex(const SolidStructure& Declared,
                          const SpatialPoint& Probe,
                          double MaximumDistance,
                          VertexName& Resolved,
                          double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;

    const SolidView View = Declared.Resolve();
    if (View.Vertices == nullptr)
        return false;

    for (std::uint32_t VertexIndex = 1u; VertexIndex <= View.Vertices->size(); ++VertexIndex)
    {
        const double Candidate = std::sqrt(DistanceSquared(Probe, (*View.Vertices)[VertexIndex - 1u].Position));
        if (Candidate <= Distance)
        {
            Distance = Candidate;
            Resolved = { VertexIndex };
            ResolvedAny = true;
        }
    }

    return ResolvedAny;
}

bool ResolveNearestEdgeSpan(const SolidStructure& Declared,
                            const SpatialPoint& Probe,
                            double MaximumDistance,
                            EdgeSpanPlacement& Resolved,
                            double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;
    std::vector<EdgeSpanPlacement> SpanSet;

    const SolidView View = Declared.Resolve();
    if (View.Edges == nullptr || View.Vertices == nullptr)
        return false;
    if (!ResolveEdgeSpans(Declared, SpanSet))
        return false;

    for (const EdgeSpanPlacement& Span : SpanSet)
    {
        for (EdgeName EdgeNameValue : Span.EdgeSet)
        {
            const DeclaredEdge& Edge = (*View.Edges)[EdgeNameValue.IssuedIndex - 1u];
            const SpatialPoint& StartPoint = (*View.Vertices)[Edge.StartVertex.IssuedIndex - 1u].Position;
            const SpatialPoint& EndPoint = (*View.Vertices)[Edge.EndVertex.IssuedIndex - 1u].Position;
            const double Candidate = std::sqrt(DistancePointSegmentSquared(Probe, StartPoint, EndPoint));
            if (Candidate <= Distance)
            {
                Distance = Candidate;
                Resolved = Span;
                ResolvedAny = true;
            }
        }
    }

    return ResolvedAny;
}

bool ResolveNearestLoop(const SolidStructure& Declared,
                        const SpatialPoint& Probe,
                        double MaximumDistance,
                        LoopName& Resolved,
                        double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;
    std::vector<EdgeSpanPlacement> SpanSet;

    if (!ResolveEdgeSpans(Declared, SpanSet))
        return false;

    const SolidView View = Declared.Resolve();
    if (View.Edges == nullptr || View.Vertices == nullptr)
        return false;

    for (const EdgeSpanPlacement& Span : SpanSet)
    {
        for (EdgeName EdgeNameValue : Span.EdgeSet)
        {
            const DeclaredEdge& Edge = (*View.Edges)[EdgeNameValue.IssuedIndex - 1u];
            const SpatialPoint& StartPoint = (*View.Vertices)[Edge.StartVertex.IssuedIndex - 1u].Position;
            const SpatialPoint& EndPoint = (*View.Vertices)[Edge.EndVertex.IssuedIndex - 1u].Position;
            const double Candidate = std::sqrt(DistancePointSegmentSquared(Probe, StartPoint, EndPoint));
            if (Candidate <= Distance)
            {
                Distance = Candidate;
                Resolved = Span.Loop;
                ResolvedAny = true;
            }
        }
    }

    return ResolvedAny;
}

bool ResolveNearestFace(const SolidStructure& Declared,
                        const SpatialPoint& Probe,
                        double MaximumDistance,
                        FaceName& Resolved,
                        double& Distance)
{
    Distance = MaximumDistance;
    bool ResolvedAny = false;

    const SolidView View = Declared.Resolve();
    if (View.Faces == nullptr || View.Surfaces == nullptr)
        return false;

    for (std::uint32_t FaceIndex = 1u; FaceIndex <= View.Faces->size(); ++FaceIndex)
    {
        const DeclaredFace& Face = (*View.Faces)[FaceIndex - 1u];
        if (!FaceContainsPoint(Declared, Face, Probe))
            continue;

        const SurfaceSpecification& Surface = (*View.Surfaces)[Face.SupportingSurface.IssuedIndex - 1u].Geometry;
        if (Surface.Subject() != SurfaceForm::Plane)
            continue;

        const SpatialDirection Normal = Normalize(Surface.HeldPlane().Normal);
        const double Candidate = std::fabs(Dot(Normal, Difference(Surface.HeldPlane().Origin, Probe)));
        if (Candidate <= Distance)
        {
            Distance = Candidate;
            Resolved = { FaceIndex };
            ResolvedAny = true;
        }
    }

    return ResolvedAny;
}

bool ResolveNearestSolid(const SolidStructure& Declared,
                         const SpatialPoint& Probe,
                         double MaximumDistance,
                         SolidName& Resolved,
                         double& Distance)
{
    FaceName Face = {};
    if (!ResolveNearestFace(Declared, Probe, MaximumDistance, Face, Distance))
        return false;
    Resolved = { 1u };
    return true;
}

} // namespace Slate
