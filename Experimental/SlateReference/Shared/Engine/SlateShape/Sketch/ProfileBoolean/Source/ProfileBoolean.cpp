//============================================================================================================================================
//                                                         PROFILEBOOLEAN.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/ProfileBoolean/Api/ProfileBoolean.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    struct FlatLoop
    {
        std::vector<PlanarPoint> Points = {};
        ProfileLoopOrientation Orientation = ProfileLoopOrientation::Outer;
    };

    struct FlatProfile
    {
        std::vector<FlatLoop> Loops = {};
    };

    PlanarPoint FlattenPoint(const ProfilePlane& Plane, const SpatialPoint& Position)
    {
        const SpatialDirection AlongDirection = Normalize(Plane.AlongDirection);
        const SpatialDirection AcrossDirection = Normalize(Cross(Plane.Normal, AlongDirection));
        const SpatialDirection Offset = { Position.Left - Plane.Origin.Left,
                                          Position.Up - Plane.Origin.Up,
                                          Position.Forward - Plane.Origin.Forward };
        return { Dot(Offset, AlongDirection), Dot(Offset, AcrossDirection) };
    }

    SpatialPoint LiftPoint(const ProfilePlane& Plane, const PlanarPoint& Position)
    {
        const SpatialDirection AlongDirection = Normalize(Plane.AlongDirection);
        const SpatialDirection AcrossDirection = Normalize(Cross(Plane.Normal, AlongDirection));
        return {
            Plane.Origin.Left + AlongDirection.Left * Position.Along + AcrossDirection.Left * Position.Across,
            Plane.Origin.Up + AlongDirection.Up * Position.Along + AcrossDirection.Up * Position.Across,
            Plane.Origin.Forward + AlongDirection.Forward * Position.Along + AcrossDirection.Forward * Position.Across
        };
    }

    void AppendCurvePolylineLocal(const CurveSpecification& Geometry,
                                  std::vector<SpatialPoint>& Polyline)
    {
        Slate::AppendCurvePolyline(Geometry, Polyline, 48u);
    }

    bool ResolveFlatProfile(const SketchStructure& Declared,
                            ProfileNameInFeature Subject,
                            FlatProfile& Resolved,
                            ProfilePlane& Plane)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Profiles().size())
            return false;
        const ProfileSpecification& Profile = Declared.Profiles()[Subject.IssuedIndex - 1u];
        if (!Profile.Declared())
            return false;
        Plane = Profile.HeldPlane();
        Resolved = {};
        for (const ProfileLoop& Loop : Profile.HeldLoops())
        {
            FlatLoop Flat;
            Flat.Orientation = Loop.Orientation;
            std::vector<SpatialPoint> Segment;
            for (std::size_t UseIndex = 0u; UseIndex < Loop.Traversal.size(); ++UseIndex)
            {
                const ProfileCurveUse& Use = Loop.Traversal[UseIndex];
                if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > Declared.Curves().size())
                    return false;
                const CurveSpecification& Curve = Declared.Curves()[Use.TraversedCurve.IssuedIndex - 1u].Geometry;
                AppendCurvePolylineLocal(Curve, Segment);
                if (Segment.size() < 2u)
                    return false;
                if (!Use.SameSense)
                    std::reverse(Segment.begin(), Segment.end());
                for (std::size_t PointIndex = 0u; PointIndex < Segment.size(); ++PointIndex)
                {
                    if (!Flat.Points.empty() && PointIndex == 0u)
                        continue;
                    Flat.Points.push_back(FlattenPoint(Plane, Segment[PointIndex]));
                }
            }
            if (Flat.Points.size() < 3u)
                return false;
            if (Flat.Points.front().Along == Flat.Points.back().Along && Flat.Points.front().Across == Flat.Points.back().Across)
                Flat.Points.pop_back();
            Resolved.Loops.push_back(Flat);
        }
        return !Resolved.Loops.empty();
    }

    double SignedArea(const std::vector<PlanarPoint>& Polygon)
    {
        double Sum = 0.0;
        for (std::size_t Index = 0u; Index < Polygon.size(); ++Index)
        {
            const PlanarPoint& Current = Polygon[Index];
            const PlanarPoint& Next = Polygon[(Index + 1u) % Polygon.size()];
            Sum += Current.Along * Next.Across - Next.Along * Current.Across;
        }
        return Sum * 0.5;
    }

    bool ConvexPolygon(const std::vector<PlanarPoint>& Polygon)
    {
        if (Polygon.size() < 3u)
            return false;
        double Sign = 0.0;
        for (std::size_t Index = 0u; Index < Polygon.size(); ++Index)
        {
            const PlanarPoint& A = Polygon[Index];
            const PlanarPoint& B = Polygon[(Index + 1u) % Polygon.size()];
            const PlanarPoint& C = Polygon[(Index + 2u) % Polygon.size()];
            const double CrossValue = (B.Along - A.Along) * (C.Across - B.Across) - (B.Across - A.Across) * (C.Along - B.Along);
            if (std::fabs(CrossValue) <= 1.0e-9)
                continue;
            if (Sign == 0.0)
                Sign = CrossValue;
            else if ((Sign > 0.0) != (CrossValue > 0.0))
                return false;
        }
        return true;
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

    double Orientation(const PlanarPoint& A,
                       const PlanarPoint& B,
                       const PlanarPoint& C)
    {
        return (B.Along - A.Along) * (C.Across - A.Across)
             - (B.Across - A.Across) * (C.Along - A.Along);
    }

    bool SegmentsIntersect(const PlanarPoint& A0, const PlanarPoint& A1,
                           const PlanarPoint& B0, const PlanarPoint& B1)
    {
        const double O1 = Orientation(A0, A1, B0);
        const double O2 = Orientation(A0, A1, B1);
        const double O3 = Orientation(B0, B1, A0);
        const double O4 = Orientation(B0, B1, A1);
        return ((O1 > 0.0) != (O2 > 0.0)) && ((O3 > 0.0) != (O4 > 0.0));
    }

    bool PolygonsIntersect(const std::vector<PlanarPoint>& LeftPolygon,
                           const std::vector<PlanarPoint>& RightPolygon)
    {
        for (std::size_t LeftIndex = 0u; LeftIndex < LeftPolygon.size(); ++LeftIndex)
        {
            const PlanarPoint& LeftA = LeftPolygon[LeftIndex];
            const PlanarPoint& LeftB = LeftPolygon[(LeftIndex + 1u) % LeftPolygon.size()];
            for (std::size_t RightIndex = 0u; RightIndex < RightPolygon.size(); ++RightIndex)
            {
                const PlanarPoint& RightA = RightPolygon[RightIndex];
                const PlanarPoint& RightB = RightPolygon[(RightIndex + 1u) % RightPolygon.size()];
                if (SegmentsIntersect(LeftA, LeftB, RightA, RightB))
                    return true;
            }
        }
        return false;
    }

    bool SamePlane(const ProfilePlane& LeftPlane,
                   const ProfilePlane& RightPlane)
    {
        const SpatialDirection LeftNormal = Normalize(LeftPlane.Normal);
        const SpatialDirection RightNormal = Normalize(RightPlane.Normal);
        const double Parallel = Dot(LeftNormal, RightNormal);
        if (std::fabs(std::fabs(Parallel) - 1.0) > 1.0e-6)
            return false;

        const SpatialDirection Offset = { RightPlane.Origin.Left - LeftPlane.Origin.Left,
                                          RightPlane.Origin.Up - LeftPlane.Origin.Up,
                                          RightPlane.Origin.Forward - LeftPlane.Origin.Forward };
        return std::fabs(Dot(LeftNormal, Offset)) <= 1.0e-6;
    }

    std::vector<PlanarPoint> IntersectConvex(const std::vector<PlanarPoint>& Subject,
                                             const std::vector<PlanarPoint>& Clip)
    {
        std::vector<PlanarPoint> Output = Subject;
        const double ClipSign = SignedArea(Clip) >= 0.0 ? 1.0 : -1.0;

        for (std::size_t EdgeIndex = 0u; EdgeIndex < Clip.size() && !Output.empty(); ++EdgeIndex)
        {
            const PlanarPoint A = Clip[EdgeIndex];
            const PlanarPoint B = Clip[(EdgeIndex + 1u) % Clip.size()];
            std::vector<PlanarPoint> Input = Output;
            Output.clear();

            for (std::size_t PointIndex = 0u; PointIndex < Input.size(); ++PointIndex)
            {
                const PlanarPoint Current = Input[PointIndex];
                const PlanarPoint Previous = Input[(PointIndex + Input.size() - 1u) % Input.size()];
                const double CurrentSide = Orientation(A, B, Current) * ClipSign;
                const double PreviousSide = Orientation(A, B, Previous) * ClipSign;
                const bool CurrentInside = CurrentSide >= -1.0e-9;
                const bool PreviousInside = PreviousSide >= -1.0e-9;

                if (CurrentInside != PreviousInside)
                {
                    const double Denominator = (Current.Along - Previous.Along) * (B.Across - A.Across)
                                             - (Current.Across - Previous.Across) * (B.Along - A.Along);
                    if (std::fabs(Denominator) > 1.0e-12)
                    {
                        const double Numerator = (A.Along - Previous.Along) * (B.Across - A.Across)
                                               - (A.Across - Previous.Across) * (B.Along - A.Along);
                        const double Parameter = Numerator / Denominator;
                        Output.push_back({ Previous.Along + (Current.Along - Previous.Along) * Parameter,
                                           Previous.Across + (Current.Across - Previous.Across) * Parameter });
                    }
                }
                if (CurrentInside)
                    Output.push_back(Current);
            }
        }

        return Output;
    }

    ProfileNameInFeature DeclareProfileFromLoops(SketchStructure& Declared,
                                                 const ProfilePlane& Plane,
                                                 const std::vector<FlatLoop>& Loops)
    {
        ProfileSpecification Profile;
        Profile.DeclarePlane(Plane);

        for (const FlatLoop& Flat : Loops)
        {
            ProfileLoop Loop;
            Loop.Orientation = Flat.Orientation;
            for (std::size_t PointIndex = 0u; PointIndex < Flat.Points.size(); ++PointIndex)
            {
                const std::size_t NextIndex = (PointIndex + 1u) % Flat.Points.size();
                const SpatialPoint StartPoint = LiftPoint(Plane, Flat.Points[PointIndex]);
                const SpatialPoint EndPoint = LiftPoint(Plane, Flat.Points[NextIndex]);
                const SketchCurveName Edge = Declared.DeclareLine(StartPoint, EndPoint);
                Loop.Traversal.push_back({ { Edge.IssuedIndex }, true });
            }
            Profile.DeclareLoop(Loop);
        }

        return Declared.DeclareProfile(Profile);
    }
}

ProfileBooleanDisposition EvaluateProfileBoolean(const SketchStructure& Declared,
                                                 const std::vector<ProfileNameInFeature>& OperandSet,
                                                 BooleanSubject Subject)
{
    if (OperandSet.empty())
        return ProfileBooleanDisposition::NotRequested;
    if (OperandSet.size() < 2u || !Declared.Declared())
        return ProfileBooleanDisposition::InvalidSpecification;

    ProfilePlane SharedPlane = {};
    bool FirstPlane = true;
    for (ProfileNameInFeature Operand : OperandSet)
    {
        FlatProfile Flat;
        ProfilePlane Plane = {};
        if (!ResolveFlatProfile(Declared, Operand, Flat, Plane))
            return ProfileBooleanDisposition::InvalidSpecification;
        if (FirstPlane)
        {
            SharedPlane = Plane;
            FirstPlane = false;
        }
        else if (!SamePlane(SharedPlane, Plane))
            return ProfileBooleanDisposition::UnsupportedGeometry;
    }

    (void)Subject;
    return ProfileBooleanDisposition::Produced;
}

Deliver<std::vector<ProfileNameInFeature>> ApplyProfileBoolean(SketchStructure& Declared,
                                                               const std::vector<ProfileNameInFeature>& OperandSet,
                                                               BooleanSubject Subject)
{
    if (!Declared.Declared())
        return Deliver<std::vector<ProfileNameInFeature>>::Refuse({ RefusalReason::ContentUnsupported, "the sketch is not declared" });
    if (OperandSet.size() < 2u)
        return Deliver<std::vector<ProfileNameInFeature>>::Refuse({ RefusalReason::ContentUnsupported, "a boolean requires at least two profiles" });

    std::vector<FlatProfile> OperandProfiles;
    OperandProfiles.reserve(OperandSet.size());
    ProfilePlane SharedPlane = {};
    bool FirstPlane = true;

    for (ProfileNameInFeature Operand : OperandSet)
    {
        FlatProfile Flat;
        ProfilePlane Plane = {};
        if (!ResolveFlatProfile(Declared, Operand, Flat, Plane))
            return Deliver<std::vector<ProfileNameInFeature>>::Refuse({ RefusalReason::ContentUnsupported, "a boolean operand is not a resolvable profile" });
        if (Flat.Loops.empty() || Flat.Loops.front().Orientation != ProfileLoopOrientation::Outer)
            return Deliver<std::vector<ProfileNameInFeature>>::Refuse({ RefusalReason::ContentUnsupported, "each boolean operand requires one outer loop" });
        if (FirstPlane)
        {
            SharedPlane = Plane;
            FirstPlane = false;
        }
        else if (!SamePlane(SharedPlane, Plane))
        {
            return Deliver<std::vector<ProfileNameInFeature>>::Refuse({ RefusalReason::ContentUnsupported, "boolean operands must share one plane" });
        }
        OperandProfiles.push_back(Flat);
    }

    std::vector<ProfileNameInFeature> Produced;

    switch (Subject)
    {
        case BooleanSubject::Intersect:
        {
            FlatProfile Current = OperandProfiles.front();
            for (std::size_t OperandIndex = 1u; OperandIndex < OperandProfiles.size(); ++OperandIndex)
            {
                const std::vector<PlanarPoint>& CurrentOuter = Current.Loops.front().Points;
                const std::vector<PlanarPoint>& ClipOuter = OperandProfiles[OperandIndex].Loops.front().Points;

                if (PolygonsIntersect(CurrentOuter, ClipOuter))
                {
                    if (!ConvexPolygon(CurrentOuter) || !ConvexPolygon(ClipOuter))
                    {
                        return Deliver<std::vector<ProfileNameInFeature>>::Refuse(
                            { RefusalReason::ContentUnsupported, "intersecting profiles currently require convex outer loops" });
                    }
                    Current.Loops = { { IntersectConvex(CurrentOuter, ClipOuter), ProfileLoopOrientation::Outer } };
                    if (Current.Loops.front().Points.size() < 3u)
                        return Deliver<std::vector<ProfileNameInFeature>>::Result({});
                }
                else if (PointInsidePolygon(CurrentOuter.front(), ClipOuter))
                {
                    // Current already lies wholly inside the clip.
                }
                else if (PointInsidePolygon(ClipOuter.front(), CurrentOuter))
                {
                    Current = OperandProfiles[OperandIndex];
                }
                else
                {
                    return Deliver<std::vector<ProfileNameInFeature>>::Result({});
                }
            }
            Produced.push_back(DeclareProfileFromLoops(Declared, SharedPlane, Current.Loops));
            return Deliver<std::vector<ProfileNameInFeature>>::Result(Produced);
        }

        case BooleanSubject::Subtract:
        {
            const FlatProfile& SubjectProfile = OperandProfiles.front();
            const std::vector<PlanarPoint>& SubjectOuter = SubjectProfile.Loops.front().Points;
            std::vector<FlatLoop> ResultLoops = SubjectProfile.Loops;

            for (std::size_t OperandIndex = 1u; OperandIndex < OperandProfiles.size(); ++OperandIndex)
            {
                const FlatProfile& CutterProfile = OperandProfiles[OperandIndex];
                const std::vector<PlanarPoint>& Cutter = CutterProfile.Loops.front().Points;
                if (PolygonsIntersect(SubjectOuter, Cutter))
                {
                    return Deliver<std::vector<ProfileNameInFeature>>::Refuse(
                        { RefusalReason::ContentUnsupported, "subtract currently accepts disjoint or contained cutters only" });
                }
                if (PointInsidePolygon(SubjectOuter.front(), Cutter))
                    return Deliver<std::vector<ProfileNameInFeature>>::Result({});
                if (!PointInsidePolygon(Cutter.front(), SubjectOuter))
                    continue;

                ResultLoops.push_back({ Cutter, ProfileLoopOrientation::Inner });
            }

            Produced.push_back(DeclareProfileFromLoops(Declared, SharedPlane, ResultLoops));
            return Deliver<std::vector<ProfileNameInFeature>>::Result(Produced);
        }

        case BooleanSubject::Unite:
        {
            std::vector<bool> Consumed(OperandProfiles.size(), false);
            for (std::size_t LeftIndex = 0u; LeftIndex < OperandProfiles.size(); ++LeftIndex)
            {
                if (Consumed[LeftIndex])
                    continue;
                const FlatProfile& LeftProfile = OperandProfiles[LeftIndex];
                const std::vector<PlanarPoint>& LeftOuter = LeftProfile.Loops.front().Points;
                for (std::size_t RightIndex = 0u; RightIndex < OperandProfiles.size(); ++RightIndex)
                {
                    if (RightIndex == LeftIndex)
                        continue;
                    const std::vector<PlanarPoint>& RightOuter = OperandProfiles[RightIndex].Loops.front().Points;
                    if (PolygonsIntersect(LeftOuter, RightOuter))
                    {
                        return Deliver<std::vector<ProfileNameInFeature>>::Refuse(
                            { RefusalReason::ContentUnsupported, "union currently supports disjoint or contained outer loops only" });
                    }
                    if (PointInsidePolygon(RightOuter.front(), LeftOuter))
                        Consumed[RightIndex] = true;
                }

                Produced.push_back(DeclareProfileFromLoops(Declared, SharedPlane, LeftProfile.Loops));
            }
            return Deliver<std::vector<ProfileNameInFeature>>::Result(Produced);
        }
    }

    return Deliver<std::vector<ProfileNameInFeature>>::Refuse({ RefusalReason::ContentUnsupported, "no such boolean subject" });
}

} // namespace Slate
