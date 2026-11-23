//============================================================================================================================================
//                                                        PROFILERESHAPE.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/ProfileReshape/Api/ProfileReshape.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

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

    double Clamp01(double Value)
    {
        if (Value < 0.0) return 0.0;
        if (Value > 1.0) return 1.0;
        return Value;
    }

    struct CurveCutPosition
    {
        double Parameter = 0.0;
        SpatialPoint Position = {};
    };

    bool ResolveCurveEndpoints(const SketchStructure& Declared,
                               SketchCurveName Subject,
                               const CurveSpecification*& Geometry,
                               SpatialPoint& StartPoint,
                               SpatialPoint& EndPoint)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
            return false;
        Geometry = &Declared.Curves()[Subject.IssuedIndex - 1u].Geometry;
        if (!Geometry->Declared())
            return false;

        switch (Geometry->Subject())
        {
            case CurveSubject::Line:
                StartPoint = Geometry->HeldLine().Origin;
                EndPoint = Geometry->HeldLine().Terminus;
                return true;
            case CurveSubject::CircularArc:
            {
                const CircularArcCurve& Arc = Geometry->HeldCircularArc();
                const SpatialDirection StartDirection = Normalize(Arc.StartDirection);
                const SpatialDirection EndDirection = Normalize(RotateAroundAxis(StartDirection, Arc.Normal, Arc.SweepRadians));
                StartPoint = Added(Arc.Centre, Scaled(StartDirection, Arc.Radius));
                EndPoint = Added(Arc.Centre, Scaled(EndDirection, Arc.Radius));
                return true;
            }
            case CurveSubject::Circle:
            {
                const CircleCurve& Circle = Geometry->HeldCircle();
                const SpatialDirection StartDirection = Normalize(Circle.StartDirection);
                StartPoint = Added(Circle.Centre, Scaled(StartDirection, Circle.Radius));
                EndPoint = StartPoint;
                return true;
            }
            case CurveSubject::EllipticalArc:
            {
                const EllipticalArcCurve& Arc = Geometry->HeldEllipticalArc();
                const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
                const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
                StartPoint = { Arc.Centre.Left + MajorDirection.Left * Arc.MajorRadius * std::cos(Arc.StartRadians) + MinorDirection.Left * Arc.MinorRadius * std::sin(Arc.StartRadians),
                               Arc.Centre.Up + MajorDirection.Up * Arc.MajorRadius * std::cos(Arc.StartRadians) + MinorDirection.Up * Arc.MinorRadius * std::sin(Arc.StartRadians),
                               Arc.Centre.Forward + MajorDirection.Forward * Arc.MajorRadius * std::cos(Arc.StartRadians) + MinorDirection.Forward * Arc.MinorRadius * std::sin(Arc.StartRadians) };
                const double EndRadians = Arc.StartRadians + Arc.SweepRadians;
                EndPoint = { Arc.Centre.Left + MajorDirection.Left * Arc.MajorRadius * std::cos(EndRadians) + MinorDirection.Left * Arc.MinorRadius * std::sin(EndRadians),
                             Arc.Centre.Up + MajorDirection.Up * Arc.MajorRadius * std::cos(EndRadians) + MinorDirection.Up * Arc.MinorRadius * std::sin(EndRadians),
                             Arc.Centre.Forward + MajorDirection.Forward * Arc.MajorRadius * std::cos(EndRadians) + MinorDirection.Forward * Arc.MinorRadius * std::sin(EndRadians) };
                return true;
            }
            case CurveSubject::Ellipse:
            {
                const EllipseCurve& Ellipse = Geometry->HeldEllipse();
                const SpatialDirection MajorDirection = Normalize(Ellipse.MajorDirection);
                StartPoint = Added(Ellipse.Centre, Scaled(MajorDirection, Ellipse.MajorRadius));
                EndPoint = StartPoint;
                return true;
            }
            case CurveSubject::Bezier:
                if (Geometry->HeldBezier().ControlPoints.size() < 2u) return false;
                StartPoint = Geometry->HeldBezier().ControlPoints.front();
                EndPoint = Geometry->HeldBezier().ControlPoints.back();
                return true;
            case CurveSubject::BasisSpline:
                if (Geometry->HeldBasisSpline().ControlPoints.size() < 2u) return false;
                StartPoint = Geometry->HeldBasisSpline().ControlPoints.front();
                EndPoint = Geometry->HeldBasisSpline().ControlPoints.back();
                return true;
            case CurveSubject::RationalSpline:
                if (Geometry->HeldRationalSpline().ControlPoints.size() < 2u) return false;
                StartPoint = Geometry->HeldRationalSpline().ControlPoints.front();
                EndPoint = Geometry->HeldRationalSpline().ControlPoints.back();
                return true;
            case CurveSubject::Hermite:
                StartPoint = Geometry->HeldHermite().StartPoint;
                EndPoint = Geometry->HeldHermite().EndPoint;
                return true;
            case CurveSubject::SubjectCount:
                return false;
        }
        return false;
    }

    bool ResolveCurveCutPosition(const CurveSpecification& Geometry,
                                 const SpatialPoint& Target,
                                 CurveCutPosition& Resolved)
    {
        std::vector<SpatialPoint> Polyline;
        AppendCurvePolyline(Geometry, Polyline, 96u);
        if (Polyline.size() < 2u)
            return false;

        double BestDistance = 1.0e300;
        double BestParameter = 0.0;
        SpatialPoint BestPosition = Polyline.front();

        for (std::size_t Index = 0u; Index + 1u < Polyline.size(); ++Index)
        {
            const SpatialPoint& StartPoint = Polyline[Index];
            const SpatialPoint& EndPoint = Polyline[Index + 1u];
            const SpatialDirection Span = Difference(StartPoint, EndPoint);
            const SpatialDirection Offset = Difference(StartPoint, Target);
            const double SpanLengthSquared = LengthSquared(Span);
            const double Local = SpanLengthSquared > 1.0e-18
                ? Clamp01(Dot(Offset, Span) / SpanLengthSquared)
                : 0.0;
            const SpatialPoint Position = Added(StartPoint, Scaled(Span, Local));
            const double Distance = LengthSquared(Difference(Position, Target));
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestParameter = (static_cast<double>(Index) + Local) / static_cast<double>(Polyline.size() - 1u);
                BestPosition = Position;
            }
        }

        Resolved = { BestParameter, BestPosition };
        return true;
    }

    CurveSpecification TrimPolynomialCurve(const CurveSpecification& Geometry,
                                           double FirstParameter,
                                           double LastParameter)
    {
        const ParameterInterval Interval = { FirstParameter, LastParameter };
        switch (Geometry.Subject())
        {
            case CurveSubject::Bezier:
                return CurveSpecification::DeclareBezier(Geometry.HeldBezier().ControlPoints, Interval);
            case CurveSubject::BasisSpline:
                return CurveSpecification::DeclareBasisSpline(Geometry.HeldBasisSpline(), Interval);
            case CurveSubject::RationalSpline:
                return CurveSpecification::DeclareRationalSpline(Geometry.HeldRationalSpline(), Interval);
            case CurveSubject::Hermite:
                return CurveSpecification::DeclareHermite(Geometry.HeldHermite(), Interval);
            default:
                return CurveSpecification{};
        }
    }

    bool ResolveLoopCutPositions(const SketchStructure& Declared,
                                 const ProfileLoop& Loop,
                                 std::vector<CurveCutPosition>& Cuts,
                                 std::vector<CurveSpecification>& Curves)
    {
        Cuts.clear();
        Curves.clear();
        if (Loop.Traversal.size() < 3u)
            return false;
        Cuts.reserve(Loop.Traversal.size());
        Curves.reserve(Loop.Traversal.size());

        SpatialPoint PreviousEnd = {};
        SpatialPoint FirstStart = {};
        bool First = true;
        for (const ProfileCurveUse& Use : Loop.Traversal)
        {
            if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > Declared.Curves().size())
                return false;
            const CurveSpecification& Geometry = Declared.Curves()[Use.TraversedCurve.IssuedIndex - 1u].Geometry;
            SpatialPoint StartPoint = {};
            SpatialPoint EndPoint = {};
            const CurveSpecification* GeometryPointer = nullptr;
            if (!ResolveCurveEndpoints(Declared, { Use.TraversedCurve.IssuedIndex }, GeometryPointer, StartPoint, EndPoint))
                return false;
            if (!Use.SameSense)
                std::swap(StartPoint, EndPoint);
            if (First)
            {
                FirstStart = StartPoint;
                First = false;
            }
            else if (!SamePoint(PreviousEnd, StartPoint))
                return false;
            Cuts.push_back({ Use.SameSense ? Geometry.Interval().Minimum : Geometry.Interval().Maximum, StartPoint });
            Curves.push_back(Geometry);
            PreviousEnd = EndPoint;
        }

        return SamePoint(PreviousEnd, FirstStart);
    }

    bool ResolveLoopPoints(const SketchStructure& Declared,
                           const ProfileLoop& Loop,
                           std::vector<SpatialPoint>& Points)
    {
        Points.clear();
        std::vector<CurveCutPosition> Cuts;
        std::vector<CurveSpecification> Curves;
        if (!ResolveLoopCutPositions(Declared, Loop, Cuts, Curves))
            return false;
        Points.reserve(Cuts.size());
        for (const CurveCutPosition& Cut : Cuts)
            Points.push_back(Cut.Position);
        return Points.size() >= 3u;
    }

    double SignedArea(const std::vector<SpatialPoint>& Points, const SpatialDirection& PlaneNormal)
    {
        SpatialDirection Sum = {};
        for (std::size_t Index = 0u; Index < Points.size(); ++Index)
        {
            const SpatialPoint& Current = Points[Index];
            const SpatialPoint& Next = Points[(Index + 1u) % Points.size()];
            Sum.Left += (Current.Up - Next.Up) * (Current.Forward + Next.Forward);
            Sum.Up += (Current.Forward - Next.Forward) * (Current.Left + Next.Left);
            Sum.Forward += (Current.Left - Next.Left) * (Current.Up + Next.Up);
        }
        return 0.5 * Dot(Sum, Normalize(PlaneNormal));
    }

    bool ConvexLoop(const std::vector<SpatialPoint>& Points,
                    const SpatialDirection& PlaneNormal)
    {
        double Sign = 0.0;
        for (std::size_t Index = 0u; Index < Points.size(); ++Index)
        {
            const SpatialDirection First = Difference(Points[Index], Points[(Index + 1u) % Points.size()]);
            const SpatialDirection Second = Difference(Points[(Index + 1u) % Points.size()], Points[(Index + 2u) % Points.size()]);
            const double Current = Dot({ First.Up * Second.Forward - First.Forward * Second.Up,
                                         First.Forward * Second.Left - First.Left * Second.Forward,
                                         First.Left * Second.Up - First.Up * Second.Left },
                                       Normalize(PlaneNormal));
            if (std::fabs(Current) <= 1.0e-9)
                continue;
            if (Sign == 0.0)
                Sign = Current;
            else if ((Sign > 0.0) != (Current > 0.0))
                return false;
        }
        return true;
    }
}

Deliver<SketchCurveName> TrimCurve(SketchStructure& Declared,
                                   SketchCurveName Subject,
                                   const SpatialPoint& Position,
                                   bool KeepStart)
{
    const CurveSpecification* Geometry = nullptr;
    SpatialPoint StartPoint = {};
    SpatialPoint EndPoint = {};
    if (!ResolveCurveEndpoints(Declared, Subject, Geometry, StartPoint, EndPoint) || Geometry == nullptr)
        return Deliver<SketchCurveName>::Refuse({ RefusalReason::ContentUnsupported, "trim could not resolve the curve" });

    switch (Geometry->Subject())
    {
        case CurveSubject::Line:
            return Deliver<SketchCurveName>::Result(KeepStart ? Declared.DeclareLine(StartPoint, Position)
                                                              : Declared.DeclareLine(Position, EndPoint));

        case CurveSubject::CircularArc:
        {
            const CircularArcCurve& Arc = Geometry->HeldCircularArc();
            const SpatialDirection StartDirection = Normalize(Arc.StartDirection);
            const SpatialDirection TrimDirection = Normalize(Difference(Arc.Centre, Position));
            const double Cosine = std::clamp(Dot(StartDirection, TrimDirection), -1.0, 1.0);
            double Sweep = std::acos(Cosine);
            const double Signed = Dot(Normalize(Arc.Normal), Cross(StartDirection, TrimDirection));
            if (Signed < 0.0)
                Sweep = -Sweep;
            if (KeepStart)
            {
                return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(
                    CurveSpecification::DeclareCircularArc({ Arc.Centre, Arc.Normal, Arc.StartDirection, Arc.ThroughPoint, false, Arc.Radius, Sweep }, Geometry->Interval())));
            }
            // 📝 The arc that survives starts where the trim landed, so the direction it originally
            //    ended at plays no part -- computing it was dead work the compiler rightly flagged.
            const double EndSweep = Arc.SweepRadians - Sweep;
            return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(
                CurveSpecification::DeclareCircularArc({ Arc.Centre, Arc.Normal, TrimDirection, Arc.ThroughPoint, false, Arc.Radius, EndSweep }, Geometry->Interval())));
        }

        case CurveSubject::Circle:
        {
            const CircleCurve& Circle = Geometry->HeldCircle();
            const SpatialDirection StartDirection = Normalize(Circle.StartDirection);
            const SpatialDirection TrimDirection = Normalize(Difference(Circle.Centre, Position));
            const double Cosine = std::clamp(Dot(StartDirection, TrimDirection), -1.0, 1.0);
            double Sweep = std::acos(Cosine);
            const double Signed = Dot(Normalize(Circle.Normal), Cross(StartDirection, TrimDirection));
            if (Signed < 0.0)
                Sweep = 6.283185307179586 - Sweep;
            if (!KeepStart)
            {
                const double Remaining = 6.283185307179586 - Sweep;
                return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(
                    CurveSpecification::DeclareCircularArc({ Circle.Centre, Circle.Normal, TrimDirection, {}, false, Circle.Radius, Remaining }, { 0.0, 1.0 })));
            }
            return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(
                CurveSpecification::DeclareCircularArc({ Circle.Centre, Circle.Normal, Circle.StartDirection, {}, false, Circle.Radius, Sweep }, { 0.0, 1.0 })));
        }

        case CurveSubject::EllipticalArc:
        {
            const EllipticalArcCurve& Arc = Geometry->HeldEllipticalArc();
            const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
            const SpatialDirection Offset = Difference(Arc.Centre, Position);
            const double Phase = std::atan2(Dot(Offset, MinorDirection) / Arc.MinorRadius,
                                            Dot(Offset, MajorDirection) / Arc.MajorRadius);
            if (KeepStart)
            {
                return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(
                    CurveSpecification::DeclareEllipticalArc({ Arc.Centre, Arc.Normal, Arc.MajorDirection, Arc.MajorRadius, Arc.MinorRadius, Arc.StartRadians, Phase - Arc.StartRadians }, Geometry->Interval())));
            }
            return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(
                CurveSpecification::DeclareEllipticalArc({ Arc.Centre, Arc.Normal, Arc.MajorDirection, Arc.MajorRadius, Arc.MinorRadius, Phase, Arc.StartRadians + Arc.SweepRadians - Phase }, Geometry->Interval())));
        }

        case CurveSubject::Ellipse:
        {
            const EllipseCurve& Ellipse = Geometry->HeldEllipse();
            const SpatialDirection MajorDirection = Normalize(Ellipse.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Ellipse.Normal, MajorDirection));
            const SpatialDirection Offset = Difference(Ellipse.Centre, Position);
            const double Phase = std::atan2(Dot(Offset, MinorDirection) / Ellipse.MinorRadius,
                                            Dot(Offset, MajorDirection) / Ellipse.MajorRadius);
            const double Sweep = KeepStart ? Phase : (6.283185307179586 - Phase);
            const double StartRadians = KeepStart ? 0.0 : Phase;
            return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(
                CurveSpecification::DeclareEllipticalArc({ Ellipse.Centre, Ellipse.Normal, Ellipse.MajorDirection, Ellipse.MajorRadius, Ellipse.MinorRadius, StartRadians, Sweep }, { 0.0, 1.0 })));
        }

        case CurveSubject::Bezier:
        case CurveSubject::BasisSpline:
        case CurveSubject::RationalSpline:
        case CurveSubject::Hermite:
        {
            CurveCutPosition Cut = {};
            if (!ResolveCurveCutPosition(*Geometry, Position, Cut))
                return Deliver<SketchCurveName>::Refuse({ RefusalReason::ContentUnsupported, "the curve could not be parameterised for trim" });
            const double Minimum = Geometry->Interval().Minimum;
            const double Maximum = Geometry->Interval().Maximum;
            const double Split = Minimum + (Maximum - Minimum) * Cut.Parameter;
            const CurveSpecification Trimmed = KeepStart
                ? TrimPolynomialCurve(*Geometry, Minimum, Split)
                : TrimPolynomialCurve(*Geometry, Split, Maximum);
            return Deliver<SketchCurveName>::Result(Declared.DeclareCurve(Trimmed));
        }

        case CurveSubject::SubjectCount:
            return Deliver<SketchCurveName>::Refuse({ RefusalReason::ContentUnsupported, "the curve cannot be trimmed" });
    }

    return Deliver<SketchCurveName>::Refuse({ RefusalReason::ContentUnsupported, "the curve cannot be trimmed" });
}

Deliver<std::vector<SketchCurveName>> CutCurve(SketchStructure& Declared,
                                               SketchCurveName Subject,
                                               const SpatialPoint& Position)
{
    std::vector<SketchCurveName> Result;
    const Deliver<SketchCurveName> First = TrimCurve(Declared, Subject, Position, true);
    const Deliver<SketchCurveName> Second = TrimCurve(Declared, Subject, Position, false);
    if (!First || !Second)
        return Deliver<std::vector<SketchCurveName>>::Refuse({ RefusalReason::ContentUnsupported, "the curve could not be cut" });
    Result.push_back(First.Resolve());
    Result.push_back(Second.Resolve());
    return Deliver<std::vector<SketchCurveName>>::Result(Result);
}

Deliver<bool> CutProfile(SketchStructure& Declared,
                         ProfileNameInFeature Subject,
                         std::uint32_t LoopIndex,
                         std::uint32_t EdgeIndex,
                         const SpatialPoint& Position,
                         std::vector<SketchCurveName>& Produced)
{
    Produced.clear();
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Profiles().size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such profile is declared" });
    const ProfileSpecification& Profile = Declared.Profiles()[Subject.IssuedIndex - 1u];
    if (LoopIndex >= Profile.HeldLoops().size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such loop is declared" });
    const ProfileLoop& Loop = Profile.HeldLoops()[LoopIndex];
    if (EdgeIndex >= Loop.Traversal.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such edge is declared" });

    const Deliver<std::vector<SketchCurveName>> Split = CutCurve(Declared, { Loop.Traversal[EdgeIndex].TraversedCurve.IssuedIndex }, Position);
    if (!Split)
        return Deliver<bool>::Refuse(Split.Error);
    Produced = Split.Resolve();

    for (std::size_t UseIndex = 1u; UseIndex < Loop.Traversal.size(); ++UseIndex)
    {
        const std::size_t CurrentIndex = (EdgeIndex + UseIndex) % Loop.Traversal.size();
        Produced.push_back({ Loop.Traversal[CurrentIndex].TraversedCurve.IssuedIndex });
    }

    return Deliver<bool>::Result(true);
}

ReshapeDisposition EvaluateProfileInset(const SketchStructure& Declared,
                                        ProfileNameInFeature Subject,
                                        double Distance)
{
    if (!Subject.Assigned() && Distance == 0.0)
        return ReshapeDisposition::NotRequested;
    if (!Declared.Declared() || !Subject.Assigned() || Distance <= 0.0)
        return ReshapeDisposition::InvalidSpecification;
    if (Subject.IssuedIndex > Declared.Profiles().size())
        return ReshapeDisposition::InvalidSpecification;

    const ProfileSpecification& Profile = Declared.Profiles()[Subject.IssuedIndex - 1u];
    if (Profile.HeldLoops().size() != 1u || Profile.HeldLoops().front().Orientation != ProfileLoopOrientation::Outer)
        return ReshapeDisposition::UnsupportedGeometry;

    std::vector<SpatialPoint> Points;
    if (!ResolveLoopPoints(Declared, Profile.HeldLoops().front(), Points))
        return ReshapeDisposition::UnsupportedGeometry;
    if (!ConvexLoop(Points, Profile.HeldPlane().Normal))
        return ReshapeDisposition::UnsupportedGeometry;
    return ReshapeDisposition::Produced;
}

Deliver<ProfileNameInFeature> ApplyProfileInset(SketchStructure& Declared,
                                                ProfileNameInFeature Subject,
                                                double Distance)
{
    if (EvaluateProfileInset(Declared, Subject, Distance) != ReshapeDisposition::Produced)
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the requested inset is unsupported" });

    const ProfileSpecification& Source = Declared.Profiles()[Subject.IssuedIndex - 1u];
    std::vector<SpatialPoint> Points;
    ResolveLoopPoints(Declared, Source.HeldLoops().front(), Points);

    const double Orientation = SignedArea(Points, Source.HeldPlane().Normal) >= 0.0 ? 1.0 : -1.0;
    std::vector<SpatialPoint> InsetPoints;
    InsetPoints.reserve(Points.size());

    for (std::size_t PointIndex = 0u; PointIndex < Points.size(); ++PointIndex)
    {
        const SpatialPoint& Prior = Points[(PointIndex + Points.size() - 1u) % Points.size()];
        const SpatialPoint& Current = Points[PointIndex];
        const SpatialPoint& Next = Points[(PointIndex + 1u) % Points.size()];

        const SpatialDirection Incoming = Normalize(Difference(Current, Prior));
        const SpatialDirection Outgoing = Normalize(Difference(Current, Next));
        const SpatialDirection LeftNormal = Normalize(Cross(Source.HeldPlane().Normal, Incoming));
        const SpatialDirection RightNormal = Normalize(Cross(Outgoing, Source.HeldPlane().Normal));
        const SpatialDirection Bisector = Normalize(Added(Scaled(LeftNormal, Orientation), Scaled(RightNormal, Orientation)));
        const double Cosine = std::clamp(Dot(Scaled(LeftNormal, Orientation), Bisector), 1.0e-6, 1.0);
        InsetPoints.push_back(Added(Current, Scaled(Bisector, Distance / Cosine)));
    }

    ProfileSpecification InsetProfile;
    InsetProfile.DeclarePlane(Source.HeldPlane());
    ProfileLoop Loop;
    Loop.Orientation = ProfileLoopOrientation::Outer;
    for (std::size_t PointIndex = 0u; PointIndex < InsetPoints.size(); ++PointIndex)
    {
        const std::size_t NextIndex = (PointIndex + 1u) % InsetPoints.size();
        const SketchCurveName Edge = Declared.DeclareLine(InsetPoints[PointIndex], InsetPoints[NextIndex]);
        Loop.Traversal.push_back({ { Edge.IssuedIndex }, true });
    }
    InsetProfile.DeclareLoop(Loop);

    return Deliver<ProfileNameInFeature>::Result(Declared.DeclareProfile(InsetProfile));
}

} // namespace Slate
