//============================================================================================================================================
//                                                  EXTRUSIONSPECIFICATION.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Operation/ExtrusionSpecification/Api/ExtrusionSpecification.h"

#include <cmath>

namespace Slate
{

namespace
{
    struct ResolvedLoopRing
    {
        ProfileLoopOrientation Orientation = ProfileLoopOrientation::Outer;
        std::vector<CurveSpecification> Curves = {};
        std::vector<SpatialPoint> StartPoints = {};
        std::vector<SpatialPoint> EndPoints = {};
    };

    struct BuiltLoopRing
    {
        ProfileLoopOrientation Orientation = ProfileLoopOrientation::Outer;
        std::vector<SpatialPoint> StartRing = {};
        std::vector<SpatialPoint> EndRing = {};
        std::vector<VertexName> StartVertices = {};
        std::vector<VertexName> EndVertices = {};
        std::vector<CurveNameInSolid> StartCurves = {};
        std::vector<CurveNameInSolid> EndCurves = {};
        std::vector<CurveNameInSolid> SpineCurves = {};
        std::vector<EdgeName> StartEdges = {};
        std::vector<EdgeName> EndEdges = {};
        std::vector<EdgeName> SpineEdges = {};
        std::vector<SurfaceNameInSolid> WallSurfaces = {};
    };

    SpatialPoint Shifted(const SpatialPoint& Position,
                         const SpatialDirection& Direction,
                         double Distance)
    {
        return { Position.Left + Direction.Left * Distance,
                 Position.Up + Direction.Up * Distance,
                 Position.Forward + Direction.Forward * Distance };
    }

    bool SamePoint(const SpatialPoint& LeftPoint,
                   const SpatialPoint& RightPoint)
    {
        const SpatialDirection Offset = Difference(LeftPoint, RightPoint);
        return LengthSquared(Offset) <= 1.0e-18;
    }

    SpatialPoint PointAlong(const SpatialPoint& Origin,
                            const SpatialDirection& Direction,
                            double Distance)
    {
        return Shifted(Origin, Normalize(Direction), Distance);
    }

    bool ResolveCurveEndpoints(const CurveSpecification& Declared,
                               SpatialPoint& StartPoint,
                               SpatialPoint& EndPoint)
    {
        if (!Declared.Declared())
            return false;

        switch (Declared.Subject())
        {
            case CurveSubject::Line:
                StartPoint = Declared.HeldLine().Origin;
                EndPoint = Declared.HeldLine().Terminus;
                return true;

            case CurveSubject::CircularArc:
            {
                const CircularArcCurve& Arc = Declared.HeldCircularArc();
                const SpatialDirection StartDirection = Normalize(Arc.StartDirection);
                const SpatialDirection EndDirection = RotateAroundAxis(StartDirection, Arc.Normal, Arc.SweepRadians);
                StartPoint = PointAlong(Arc.Centre, StartDirection, Arc.Radius);
                EndPoint = PointAlong(Arc.Centre, EndDirection, Arc.Radius);
                return true;
            }

            case CurveSubject::EllipticalArc:
            {
                const EllipticalArcCurve& Arc = Declared.HeldEllipticalArc();
                const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
                const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
                const double StartCosine = std::cos(Arc.StartRadians);
                const double StartSine = std::sin(Arc.StartRadians);
                const double EndRadians = Arc.StartRadians + Arc.SweepRadians;
                const double EndCosine = std::cos(EndRadians);
                const double EndSine = std::sin(EndRadians);
                StartPoint = {
                    Arc.Centre.Left + MajorDirection.Left * Arc.MajorRadius * StartCosine + MinorDirection.Left * Arc.MinorRadius * StartSine,
                    Arc.Centre.Up + MajorDirection.Up * Arc.MajorRadius * StartCosine + MinorDirection.Up * Arc.MinorRadius * StartSine,
                    Arc.Centre.Forward + MajorDirection.Forward * Arc.MajorRadius * StartCosine + MinorDirection.Forward * Arc.MinorRadius * StartSine
                };
                EndPoint = {
                    Arc.Centre.Left + MajorDirection.Left * Arc.MajorRadius * EndCosine + MinorDirection.Left * Arc.MinorRadius * EndSine,
                    Arc.Centre.Up + MajorDirection.Up * Arc.MajorRadius * EndCosine + MinorDirection.Up * Arc.MinorRadius * EndSine,
                    Arc.Centre.Forward + MajorDirection.Forward * Arc.MajorRadius * EndCosine + MinorDirection.Forward * Arc.MinorRadius * EndSine
                };
                return true;
            }

            case CurveSubject::Bezier:
                if (Declared.HeldBezier().ControlPoints.size() < 2u)
                    return false;
                StartPoint = Declared.HeldBezier().ControlPoints.front();
                EndPoint = Declared.HeldBezier().ControlPoints.back();
                return true;

            case CurveSubject::RationalSpline:
                if (Declared.HeldRationalSpline().ControlPoints.size() < 2u)
                    return false;
                StartPoint = Declared.HeldRationalSpline().ControlPoints.front();
                EndPoint = Declared.HeldRationalSpline().ControlPoints.back();
                return true;

            case CurveSubject::SubjectCount:
                return false;
        }

        return false;
    }

    CurveSpecification ShiftedCurve(const CurveSpecification& Declared,
                                    const SpatialDirection& Direction,
                                    double Distance)
    {
        switch (Declared.Subject())
        {
            case CurveSubject::Line:
                return CurveSpecification::DeclareLine(Shifted(Declared.HeldLine().Origin, Direction, Distance),
                                                       Shifted(Declared.HeldLine().Terminus, Direction, Distance));

            case CurveSubject::CircularArc:
            {
                CircularArcCurve ShiftedArc = Declared.HeldCircularArc();
                ShiftedArc.Centre = Shifted(ShiftedArc.Centre, Direction, Distance);
                return CurveSpecification::DeclareCircularArc(ShiftedArc, Declared.Interval());
            }

            case CurveSubject::EllipticalArc:
            {
                EllipticalArcCurve ShiftedArc = Declared.HeldEllipticalArc();
                ShiftedArc.Centre = Shifted(ShiftedArc.Centre, Direction, Distance);
                return CurveSpecification::DeclareEllipticalArc(ShiftedArc, Declared.Interval());
            }

            case CurveSubject::Bezier:
            {
                std::vector<SpatialPoint> ControlPoints = Declared.HeldBezier().ControlPoints;
                for (SpatialPoint& Point : ControlPoints)
                    Point = Shifted(Point, Direction, Distance);
                return CurveSpecification::DeclareBezier(ControlPoints, Declared.Interval());
            }

            case CurveSubject::RationalSpline:
            {
                RationalSplineCurve ShiftedSpline = Declared.HeldRationalSpline();
                for (SpatialPoint& Point : ShiftedSpline.ControlPoints)
                    Point = Shifted(Point, Direction, Distance);
                return CurveSpecification::DeclareRationalSpline(ShiftedSpline, Declared.Interval());
            }

            case CurveSubject::SubjectCount:
                break;
        }

        return CurveSpecification{};
    }

    bool ResolveProfileCurve(const ProfileCurveUse& Use,
                             const std::vector<CurveSpecification>& SourceCurves,
                             CurveSpecification& TraversedCurve,
                             SpatialPoint& StartPoint,
                             SpatialPoint& EndPoint)
    {
        if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > SourceCurves.size())
            return false;

        const CurveSpecification& Declared = SourceCurves[Use.TraversedCurve.IssuedIndex - 1u];
        if (!ResolveCurveEndpoints(Declared, StartPoint, EndPoint))
            return false;

        TraversedCurve = Declared;
        if (!Use.SameSense)
        {
            const SpatialPoint PriorStart = StartPoint;
            StartPoint = EndPoint;
            EndPoint = PriorStart;
        }

        return true;
    }

    bool ResolveLoopRing(const ProfileLoop& Loop,
                         const std::vector<CurveSpecification>& SourceCurves,
                         ResolvedLoopRing& Resolved)
    {
        if (Loop.Traversal.size() < 3u)
            return false;

        Resolved = {};
        Resolved.Orientation = Loop.Orientation;
        Resolved.Curves.reserve(Loop.Traversal.size());
        Resolved.StartPoints.reserve(Loop.Traversal.size());
        Resolved.EndPoints.reserve(Loop.Traversal.size());

        SpatialPoint FirstStart = {};
        SpatialPoint PreviousEnd = {};
        for (std::size_t UseIndex = 0; UseIndex < Loop.Traversal.size(); ++UseIndex)
        {
            CurveSpecification TraversedCurve = {};
            SpatialPoint StartPoint = {};
            SpatialPoint EndPoint = {};
            if (!ResolveProfileCurve(Loop.Traversal[UseIndex], SourceCurves, TraversedCurve, StartPoint, EndPoint))
                return false;

            if (UseIndex == 0u)
                FirstStart = StartPoint;
            else if (!SamePoint(PreviousEnd, StartPoint))
                return false;

            Resolved.Curves.push_back(TraversedCurve);
            Resolved.StartPoints.push_back(StartPoint);
            Resolved.EndPoints.push_back(EndPoint);
            PreviousEnd = EndPoint;
        }

        return SamePoint(PreviousEnd, FirstStart);
    }

    Deliver<LoopName> DeclareCapLoop(SolidStructure& Constructed,
                                     const BuiltLoopRing& Built,
                                     bool StartCap)
    {
        std::vector<CoedgeName> Coedges;
        Coedges.reserve(Built.StartEdges.size());

        if (StartCap)
        {
            for (std::size_t EdgeIndex = 0u; EdgeIndex < Built.StartEdges.size(); ++EdgeIndex)
            {
                const std::size_t ReverseIndex = Built.StartEdges.size() - 1u - EdgeIndex;
                const Deliver<CoedgeName> Coedge = Constructed.DeclareCoedge(Built.StartEdges[ReverseIndex], EdgeOrientation::Reversed);
                if (!Coedge)
                    return Deliver<LoopName>::Refuse(Coedge.Error);
                Coedges.push_back(Coedge.Resolve());
            }
        }
        else
        {
            for (EdgeName Traversed : Built.EndEdges)
            {
                const Deliver<CoedgeName> Coedge = Constructed.DeclareCoedge(Traversed, EdgeOrientation::Forward);
                if (!Coedge)
                    return Deliver<LoopName>::Refuse(Coedge.Error);
                Coedges.push_back(Coedge.Resolve());
            }
        }

        return Constructed.DeclareLoop({ Built.Orientation == ProfileLoopOrientation::Outer ? LoopStanding::Outer
                                                                                             : LoopStanding::Inner,
                                         Coedges });
    }

    DeclaredFaceLoop DeclareCapFaceLoop(const BuiltLoopRing& Built,
                                        LoopName TraversedLoop,
                                        bool StartCap)
    {
        DeclaredFaceLoop Declared;
        Declared.TraversedLoop = TraversedLoop;
        Declared.TrimSet.reserve(Built.StartCurves.size());

        if (StartCap)
        {
            for (std::size_t EdgeIndex = 0u; EdgeIndex < Built.StartCurves.size(); ++EdgeIndex)
            {
                const std::size_t ReverseIndex = Built.StartCurves.size() - 1u - EdgeIndex;
                Declared.TrimSet.push_back({ Built.StartCurves[ReverseIndex], false });
            }
        }
        else
        {
            for (CurveNameInSolid Traversed : Built.EndCurves)
                Declared.TrimSet.push_back({ Traversed, true });
        }

        return Declared;
    }

    SurfaceSpecification DeclareWallSurface(const CurveSpecification& StartCurve,
                                            const SpatialPoint& StartPoint,
                                            const SpatialPoint& EndPoint,
                                            const SpatialDirection& Axis)
    {
        if (StartCurve.Subject() == CurveSubject::Line)
        {
            const SpatialDirection EdgeDirection = Difference(StartPoint, EndPoint);
            const SpatialDirection WallNormal = Normalize(Cross(EdgeDirection, Axis));
            const SpatialDirection AlongDirection = Normalize(EdgeDirection);
            return SurfaceSpecification::DeclarePlane({ StartPoint, WallNormal, AlongDirection },
                                                      { { 0.0, 1.0 }, { 0.0, 1.0 } });
        }

        return SurfaceSpecification::DeclareLinearExtrusion({ StartCurve, Axis },
                                                            { { 0.0, 1.0 }, { 0.0, 1.0 } });
    }
}

bool ExtrusionSpecification::Declared() const
{
    return SourceProfile.Assigned()
        && LengthSquared(Direction) > 0.0
        && Distance > 0.0;
}

ExtrusionDisposition EvaluateExtrusion(const ExtrusionSpecification& Declared)
{
    if (!Declared.SourceProfile.Assigned() && Declared.Distance == 0.0)
        return ExtrusionDisposition::NotRequested;
    if (!Declared.Declared())
        return ExtrusionDisposition::InvalidSpecification;
    return ExtrusionDisposition::ImplementationAbsent;
}

Deliver<SolidStructure> ConstructExtrusion(const ProfileSpecification& SourceProfile,
                                           const std::vector<CurveSpecification>& SourceCurves,
                                           const ExtrusionSpecification& Declared)
{
    if (!Declared.Declared())
        return Deliver<SolidStructure>::Refuse({ RefusalReason::ContentUnsupported, "the extrusion request is not declared" });
    if (!SourceProfile.Declared())
        return Deliver<SolidStructure>::Refuse({ RefusalReason::ContentUnsupported, "the source profile is not declared" });

    std::vector<ResolvedLoopRing> ResolvedLoops;
    ResolvedLoops.reserve(SourceProfile.HeldLoops().size());

    std::uint32_t OuterCount = 0u;
    for (const ProfileLoop& Loop : SourceProfile.HeldLoops())
    {
        ResolvedLoopRing Resolved = {};
        if (!ResolveLoopRing(Loop, SourceCurves, Resolved))
        {
            return Deliver<SolidStructure>::Refuse(
                { RefusalReason::ContentUnsupported, "the extrusion requires connected declared profile curves" });
        }
        if (Resolved.Orientation == ProfileLoopOrientation::Outer)
            ++OuterCount;
        ResolvedLoops.push_back(Resolved);
    }

    if (OuterCount != 1u)
        return Deliver<SolidStructure>::Refuse({ RefusalReason::ContentUnsupported, "the extrusion requires exactly one outer loop" });

    const SpatialDirection Axis = Normalize(Declared.Direction);
    const double StartShift = Declared.Symmetric ? -Declared.Distance * 0.5 : 0.0;
    const double EndShift   = Declared.Symmetric ?  Declared.Distance * 0.5 : Declared.Distance;

    SolidStructure Constructed;
    const SurfaceNameInSolid StartSurface = Constructed.DeclareSurface(
        SurfaceSpecification::DeclarePlane({ Shifted(SourceProfile.HeldPlane().Origin, Axis, StartShift), Negated(Axis), SourceProfile.HeldPlane().AlongDirection },
                                           { { 0.0, 1.0 }, { 0.0, 1.0 } }));
    const SurfaceNameInSolid EndSurface = Constructed.DeclareSurface(
        SurfaceSpecification::DeclarePlane({ Shifted(SourceProfile.HeldPlane().Origin, Axis, EndShift), Axis, SourceProfile.HeldPlane().AlongDirection },
                                           { { 0.0, 1.0 }, { 0.0, 1.0 } }));

    std::vector<BuiltLoopRing> BuiltLoops;
    BuiltLoops.reserve(ResolvedLoops.size());

    for (const ResolvedLoopRing& Resolved : ResolvedLoops)
    {
        BuiltLoopRing Built = {};
        Built.Orientation = Resolved.Orientation;
        Built.StartRing.reserve(Resolved.StartPoints.size());
        Built.EndRing.reserve(Resolved.StartPoints.size());
        Built.StartVertices.reserve(Resolved.StartPoints.size());
        Built.EndVertices.reserve(Resolved.StartPoints.size());
        Built.StartCurves.reserve(Resolved.Curves.size());
        Built.EndCurves.reserve(Resolved.Curves.size());
        Built.SpineCurves.reserve(Resolved.StartPoints.size());
        Built.StartEdges.reserve(Resolved.Curves.size());
        Built.EndEdges.reserve(Resolved.Curves.size());
        Built.SpineEdges.reserve(Resolved.StartPoints.size());
        Built.WallSurfaces.reserve(Resolved.Curves.size());

        for (std::size_t PointIndex = 0u; PointIndex < Resolved.StartPoints.size(); ++PointIndex)
        {
            Built.StartRing.push_back(Shifted(Resolved.StartPoints[PointIndex], Axis, StartShift));
            Built.EndRing.push_back(Shifted(Resolved.StartPoints[PointIndex], Axis, EndShift));
        }

        for (std::size_t VertexIndex = 0u; VertexIndex < Built.StartRing.size(); ++VertexIndex)
        {
            Built.StartVertices.push_back(Constructed.DeclareVertex(Built.StartRing[VertexIndex]));
            Built.EndVertices.push_back(Constructed.DeclareVertex(Built.EndRing[VertexIndex]));
        }

        for (std::size_t EdgeIndex = 0u; EdgeIndex < Resolved.Curves.size(); ++EdgeIndex)
        {
            const std::size_t NextIndex = (EdgeIndex + 1u) % Built.StartRing.size();
            const CurveSpecification StartCurve = ShiftedCurve(Resolved.Curves[EdgeIndex], Axis, StartShift);
            const CurveSpecification EndCurve = ShiftedCurve(Resolved.Curves[EdgeIndex], Axis, EndShift);
            const CurveNameInSolid StartCurveName = Constructed.DeclareCurve(StartCurve);
            const CurveNameInSolid EndCurveName = Constructed.DeclareCurve(EndCurve);
            const CurveNameInSolid SpineCurveName = Constructed.DeclareCurve(
                CurveSpecification::DeclareLine(Built.StartRing[EdgeIndex], Built.EndRing[EdgeIndex]));

            const Deliver<EdgeName> StartEdge = Constructed.DeclareEdge(Built.StartVertices[EdgeIndex], Built.StartVertices[NextIndex], StartCurveName);
            const Deliver<EdgeName> EndEdge = Constructed.DeclareEdge(Built.EndVertices[EdgeIndex], Built.EndVertices[NextIndex], EndCurveName);
            const Deliver<EdgeName> SpineEdge = Constructed.DeclareEdge(Built.StartVertices[EdgeIndex], Built.EndVertices[EdgeIndex], SpineCurveName);
            if (!StartEdge || !EndEdge || !SpineEdge)
                return Deliver<SolidStructure>::Refuse({ RefusalReason::ContentUnsupported, "the extrusion edge set could not be declared" });

            Built.StartCurves.push_back(StartCurveName);
            Built.EndCurves.push_back(EndCurveName);
            Built.SpineCurves.push_back(SpineCurveName);
            Built.StartEdges.push_back(StartEdge.Resolve());
            Built.EndEdges.push_back(EndEdge.Resolve());
            Built.SpineEdges.push_back(SpineEdge.Resolve());
            Built.WallSurfaces.push_back(Constructed.DeclareSurface(
                DeclareWallSurface(StartCurve, Built.StartRing[EdgeIndex], Built.StartRing[NextIndex], Axis)));
        }

        BuiltLoops.push_back(Built);
    }

    std::vector<DeclaredFaceLoop> StartLoopSet;
    std::vector<DeclaredFaceLoop> EndLoopSet;
    StartLoopSet.reserve(BuiltLoops.size());
    EndLoopSet.reserve(BuiltLoops.size());

    for (const BuiltLoopRing& Built : BuiltLoops)
    {
        const Deliver<LoopName> StartLoop = DeclareCapLoop(Constructed, Built, true);
        const Deliver<LoopName> EndLoop = DeclareCapLoop(Constructed, Built, false);
        if (!StartLoop || !EndLoop)
            return Deliver<SolidStructure>::Refuse({ RefusalReason::ContentUnsupported, "the cap loops could not be declared" });

        StartLoopSet.push_back(DeclareCapFaceLoop(Built, StartLoop.Resolve(), true));
        EndLoopSet.push_back(DeclareCapFaceLoop(Built, EndLoop.Resolve(), false));
    }

    if (Declared.StartCap)
    {
        const Deliver<FaceName> StartFace = Constructed.DeclareFace({ StartSurface, true, StartLoopSet });
        if (!StartFace)
            return Deliver<SolidStructure>::Refuse(StartFace.Error);
    }

    if (Declared.EndCap)
    {
        const Deliver<FaceName> EndFace = Constructed.DeclareFace({ EndSurface, true, EndLoopSet });
        if (!EndFace)
            return Deliver<SolidStructure>::Refuse(EndFace.Error);
    }

    for (const BuiltLoopRing& Built : BuiltLoops)
    {
        for (std::size_t EdgeIndex = 0u; EdgeIndex < Built.StartEdges.size(); ++EdgeIndex)
        {
            const std::size_t NextIndex = (EdgeIndex + 1u) % Built.StartEdges.size();

            const Deliver<CoedgeName> AlongStart = Constructed.DeclareCoedge(Built.StartEdges[EdgeIndex], EdgeOrientation::Forward);
            const Deliver<CoedgeName> RiseEnd = Constructed.DeclareCoedge(Built.SpineEdges[NextIndex], EdgeOrientation::Forward);
            const Deliver<CoedgeName> AlongEnd = Constructed.DeclareCoedge(Built.EndEdges[EdgeIndex], EdgeOrientation::Reversed);
            const Deliver<CoedgeName> FallStart = Constructed.DeclareCoedge(Built.SpineEdges[EdgeIndex], EdgeOrientation::Reversed);
            if (!AlongStart || !RiseEnd || !AlongEnd || !FallStart)
                return Deliver<SolidStructure>::Refuse({ RefusalReason::ContentUnsupported, "the wall traversal could not be declared" });

            const Deliver<LoopName> WallLoop = Constructed.DeclareLoop(
                { LoopStanding::Outer, { AlongStart.Resolve(), RiseEnd.Resolve(), AlongEnd.Resolve(), FallStart.Resolve() } });
            if (!WallLoop)
                return Deliver<SolidStructure>::Refuse(WallLoop.Error);

            DeclaredFaceLoop WallFaceLoop;
            WallFaceLoop.TraversedLoop = WallLoop.Resolve();
            WallFaceLoop.TrimSet = {
                { Built.StartCurves[EdgeIndex], true },
                { Built.SpineCurves[NextIndex], true },
                { Built.EndCurves[EdgeIndex], false },
                { Built.SpineCurves[EdgeIndex], false }
            };

            const Deliver<FaceName> WallFace = Constructed.DeclareFace({ Built.WallSurfaces[EdgeIndex], true, { WallFaceLoop } });
            if (!WallFace)
                return Deliver<SolidStructure>::Refuse(WallFace.Error);
        }
    }

    if (!Constructed.Declared())
        return Deliver<SolidStructure>::Refuse({ RefusalReason::ContentUnsupported, "the extrusion result is structurally incomplete" });

    return Deliver<SolidStructure>::Result(Constructed);
}

} // namespace Slate
