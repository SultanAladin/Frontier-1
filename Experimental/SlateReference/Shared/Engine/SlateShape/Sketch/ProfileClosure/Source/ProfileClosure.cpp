//============================================================================================================================================
//                                                        PROFILECLOSURE.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/ProfileClosure/Api/ProfileClosure.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    bool SamePoint(const SpatialPoint& LeftPoint, const SpatialPoint& RightPoint)
    {
        return LengthSquared(Difference(LeftPoint, RightPoint)) <= 1.0e-18;
    }

    bool ResolveLine(const SketchStructure& Declared,
                     SketchCurveName Subject,
                     SpatialPoint& StartPoint,
                     SpatialPoint& EndPoint)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
            return false;
        const CurveSpecification& Geometry = Declared.Curves()[Subject.IssuedIndex - 1u].Geometry;
        if (Geometry.Subject() != CurveSubject::Line || !Geometry.Declared())
            return false;
        StartPoint = Geometry.HeldLine().Origin;
        EndPoint = Geometry.HeldLine().Terminus;
        return true;
    }

    bool OrderLineChain(const SketchStructure& Declared,
                        const std::vector<SketchCurveName>& CurveSet,
                        std::vector<SpatialPoint>& Points,
                        bool& Closed)
    {
        Points.clear();
        Closed = false;
        if (CurveSet.empty())
            return false;

        struct PendingEdge
        {
            SpatialPoint First = {};
            SpatialPoint Second = {};
            bool Taken = false;
        };

        std::vector<PendingEdge> Pending;
        Pending.reserve(CurveSet.size());
        for (SketchCurveName Curve : CurveSet)
        {
            SpatialPoint StartPoint = {};
            SpatialPoint EndPoint = {};
            if (!ResolveLine(Declared, Curve, StartPoint, EndPoint))
                return false;
            Pending.push_back({ StartPoint, EndPoint, false });
        }

        Pending[0].Taken = true;
        Points.push_back(Pending[0].First);
        Points.push_back(Pending[0].Second);

        while (true)
        {
            bool Advanced = false;
            for (PendingEdge& Edge : Pending)
            {
                if (Edge.Taken)
                    continue;
                if (SamePoint(Points.back(), Edge.First))
                {
                    Points.push_back(Edge.Second);
                    Edge.Taken = true;
                    Advanced = true;
                    break;
                }
                if (SamePoint(Points.back(), Edge.Second))
                {
                    Points.push_back(Edge.First);
                    Edge.Taken = true;
                    Advanced = true;
                    break;
                }
            }
            if (!Advanced)
                break;
        }

        for (const PendingEdge& Edge : Pending)
            if (!Edge.Taken)
                return false;

        Closed = SamePoint(Points.front(), Points.back());
        if (Closed)
            Points.pop_back();
        return Points.size() >= 2u;
    }

    bool ResolveLoopPoints(const SketchStructure& Declared,
                           const ProfileLoop& Loop,
                           std::vector<SpatialPoint>& Points)
    {
        Points.clear();
        if (Loop.Traversal.size() < 3u)
            return false;

        SpatialPoint PreviousEnd = {};
        SpatialPoint FirstStart = {};
        bool First = true;
        for (const ProfileCurveUse& Use : Loop.Traversal)
        {
            if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > Declared.Curves().size())
                return false;
            const CurveSpecification& Geometry = Declared.Curves()[Use.TraversedCurve.IssuedIndex - 1u].Geometry;
            if (Geometry.Subject() != CurveSubject::Line || !Geometry.Declared())
                return false;

            SpatialPoint StartPoint = Use.SameSense ? Geometry.HeldLine().Origin : Geometry.HeldLine().Terminus;
            SpatialPoint EndPoint = Use.SameSense ? Geometry.HeldLine().Terminus : Geometry.HeldLine().Origin;
            if (First)
            {
                FirstStart = StartPoint;
                First = false;
            }
            else if (!SamePoint(PreviousEnd, StartPoint))
                return false;
            Points.push_back(StartPoint);
            PreviousEnd = EndPoint;
        }

        return SamePoint(PreviousEnd, FirstStart);
    }
}

Deliver<ClosureResult> CloseCurveChain(SketchStructure& Declared,
                                       const std::vector<SketchCurveName>& CurveSet)
{
    std::vector<SpatialPoint> Points;
    bool Closed = false;
    if (!OrderLineChain(Declared, CurveSet, Points, Closed) || Closed)
        return Deliver<ClosureResult>::Refuse({ RefusalReason::ContentUnsupported, "the curve set is not one open connected line chain" });

    ProfileSpecification Profile;
    Profile.DeclarePlane({ Declared.HeldPlane().Origin, Declared.HeldPlane().Normal, Declared.HeldPlane().AlongDirection });
    ProfileLoop Loop;
    Loop.Orientation = ProfileLoopOrientation::Outer;
    for (std::size_t PointIndex = 0u; PointIndex < Points.size(); ++PointIndex)
    {
        const std::size_t NextIndex = (PointIndex + 1u) % Points.size();
        const SketchCurveName Edge = Declared.DeclareLine(Points[PointIndex], Points[NextIndex]);
        Loop.Traversal.push_back({ { Edge.IssuedIndex }, true });
    }
    Profile.DeclareLoop(Loop);

    ClosureResult Result;
    Result.Profile = Declared.DeclareProfile(Profile);
    Result.Closed = true;
    return Deliver<ClosureResult>::Result(Result);
}

Deliver<ClosureResult> JoinCurveChain(SketchStructure& Declared,
                                      const std::vector<SketchCurveName>& CurveSet)
{
    std::vector<SpatialPoint> Points;
    bool Closed = false;
    if (!OrderLineChain(Declared, CurveSet, Points, Closed))
        return Deliver<ClosureResult>::Refuse({ RefusalReason::ContentUnsupported, "the curve set is not one connected line chain" });

    if (Closed)
        return CloseCurveChain(Declared, CurveSet);

    ClosureResult Result;
    for (std::size_t PointIndex = 0u; PointIndex + 1u < Points.size(); ++PointIndex)
        Result.CurveSet.push_back(Declared.DeclareLine(Points[PointIndex], Points[PointIndex + 1u]));
    Result.Closed = false;
    return Deliver<ClosureResult>::Result(Result);
}

Deliver<std::vector<SketchCurveName>> OpenProfileLoop(SketchStructure& Declared,
                                                      ProfileNameInFeature Subject,
                                                      std::uint32_t LoopIndex,
                                                      std::uint32_t BreakEdgeIndex)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Profiles().size())
        return Deliver<std::vector<SketchCurveName>>::Refuse({ RefusalReason::ContentUnsupported, "no such profile is declared" });

    const ProfileSpecification& Profile = Declared.Profiles()[Subject.IssuedIndex - 1u];
    if (LoopIndex >= Profile.HeldLoops().size())
        return Deliver<std::vector<SketchCurveName>>::Refuse({ RefusalReason::ContentUnsupported, "no such loop is declared" });

    std::vector<SpatialPoint> Points;
    if (!ResolveLoopPoints(Declared, Profile.HeldLoops()[LoopIndex], Points))
        return Deliver<std::vector<SketchCurveName>>::Refuse({ RefusalReason::ContentUnsupported, "opening currently accepts line-only loops" });
    if (BreakEdgeIndex >= Points.size())
        return Deliver<std::vector<SketchCurveName>>::Refuse({ RefusalReason::ContentUnsupported, "no such break edge is declared" });

    std::vector<SketchCurveName> Result;
    Result.reserve(Points.size() - 1u);
    for (std::size_t Step = 1u; Step < Points.size(); ++Step)
    {
        const std::size_t Index = (BreakEdgeIndex + Step) % Points.size();
        const std::size_t NextIndex = (Index + 1u) % Points.size();
        if (Step == Points.size() - 1u)
            break;
        Result.push_back(Declared.DeclareLine(Points[Index], Points[NextIndex]));
    }

    return Deliver<std::vector<SketchCurveName>>::Result(Result);
}

} // namespace Slate
