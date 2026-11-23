//============================================================================================================================================
//                                                       SKETCHCREATION.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/SketchCreation/Api/SketchCreation.h"

#include <cmath>

namespace Slate
{

namespace
{
    std::uint32_t RequiredAnchors(SketchCreationSubject Subject)
    {
        switch (Subject)
        {
            case SketchCreationSubject::Line:          return 2u;
            case SketchCreationSubject::ThreePointArc: return 3u;
            case SketchCreationSubject::Circle:        return 2u;
            case SketchCreationSubject::Ellipse:       return 3u;
            case SketchCreationSubject::Oval:          return 3u;
            case SketchCreationSubject::Polygon:       return 2u;
            case SketchCreationSubject::Slot:          return 3u;
            case SketchCreationSubject::Hermite:       return 4u;
            case SketchCreationSubject::Polyline:
            case SketchCreationSubject::Bezier:
            case SketchCreationSubject::BasisSpline:
            case SketchCreationSubject::RationalSpline:
            case SketchCreationSubject::SubjectCount:  return 0u;
        }
        return 0u;
    }

    CircleCurve ResolveCircle(const SketchPlane& Plane,
                              const SpatialPoint& Centre,
                              const SpatialPoint& RadiusPoint)
    {
        return { Centre,
                 Plane.Normal,
                 Normalize(Difference(Centre, RadiusPoint)),
                 std::sqrt(LengthSquared(Difference(Centre, RadiusPoint))) };
    }

    EllipseCurve ResolveEllipse(const SketchPlane& Plane,
                                const SpatialPoint& Centre,
                                const SpatialPoint& MajorPoint,
                                const SpatialPoint& MinorPoint)
    {
        return { Centre,
                 Plane.Normal,
                 Normalize(Difference(Centre, MajorPoint)),
                 std::sqrt(LengthSquared(Difference(Centre, MajorPoint))),
                 std::sqrt(LengthSquared(Difference(Centre, MinorPoint))) };
    }
}

void BeginSketchCreation(SketchCreationContext& Context,
                         const SketchCreationSpecification& Requested)
{
    Context = {};
    Context.Engaged = true;
    Context.Requested = Requested;
}

void PreviewSketchCreation(SketchCreationContext& Context,
                           const SpatialPoint& Position)
{
    Context.PreviewStanding = true;
    Context.Preview = Position;
}

void AppendSketchCreation(SketchCreationContext& Context,
                          const SpatialPoint& Position)
{
    if (!Context.Engaged)
        return;
    Context.Anchors.push_back(Position);
    Context.PreviewStanding = false;
}

void CancelSketchCreation(SketchCreationContext& Context)
{
    Context = {};
}

bool CreationReady(const SketchCreationContext& Context)
{
    if (!Context.Engaged)
        return false;

    const std::uint32_t Fixed = RequiredAnchors(Context.Requested.Subject);
    if (Fixed != 0u)
        return Context.Anchors.size() >= Fixed;

    switch (Context.Requested.Subject)
    {
        case SketchCreationSubject::Polyline:
            return Context.Anchors.size() >= 2u;
        case SketchCreationSubject::Bezier:
        case SketchCreationSubject::BasisSpline:
        case SketchCreationSubject::RationalSpline:
            return Context.Anchors.size() >= 2u;
        default:
            return false;
    }
}

Deliver<SketchCreationResult> FinishSketchCreation(SketchStructure& Declared,
                                                   SketchCreationContext& Context)
{
    if (!Context.Engaged)
        return Deliver<SketchCreationResult>::Refuse({ RefusalReason::ContentUnsupported, "no sketch creation stands" });
    if (!CreationReady(Context))
        return Deliver<SketchCreationResult>::Refuse({ RefusalReason::ContentUnsupported, "the standing primitive has too few anchors" });

    SketchCreationResult Result = {};

    switch (Context.Requested.Subject)
    {
        case SketchCreationSubject::Line:
            Result.CurveSet.push_back(Declared.DeclareLine(Context.Anchors[0], Context.Anchors[1]));
            break;

        case SketchCreationSubject::Polyline:
        {
            if (Context.Requested.CloseLoop)
            {
                ProfileSpecification Profile;
                Profile.DeclarePlane({ Declared.HeldPlane().Origin, Declared.HeldPlane().Normal, Declared.HeldPlane().AlongDirection });
                ProfileLoop Loop;
                Loop.Orientation = ProfileLoopOrientation::Outer;
                for (std::size_t PointIndex = 0u; PointIndex < Context.Anchors.size(); ++PointIndex)
                {
                    const std::size_t NextIndex = (PointIndex + 1u) % Context.Anchors.size();
                    const SketchCurveName Edge = Declared.DeclareLine(Context.Anchors[PointIndex], Context.Anchors[NextIndex]);
                    Loop.Traversal.push_back({ { Edge.IssuedIndex }, true });
                }
                Profile.DeclareLoop(Loop);
                Result.Profile = Declared.DeclareProfile(Profile);
            }
            else
            {
                const Deliver<bool> DeclaredPolyline = Declared.DeclarePolyline(Context.Anchors, Result.CurveSet);
                if (!DeclaredPolyline)
                    return Deliver<SketchCreationResult>::Refuse(DeclaredPolyline.Error);
            }
            break;
        }

        case SketchCreationSubject::ThreePointArc:
            Result.CurveSet.push_back(Declared.DeclareThreePointArc(Context.Anchors[0], Context.Anchors[1], Context.Anchors[2]));
            break;

        case SketchCreationSubject::Circle:
        {
            const Deliver<ProfileNameInFeature> Profile = Declared.DeclareCircleProfile(
                ResolveCircle(Declared.HeldPlane(), Context.Anchors[0], Context.Anchors[1]));
            if (!Profile)
                return Deliver<SketchCreationResult>::Refuse(Profile.Error);
            Result.Profile = Profile.Resolve();
            break;
        }

        case SketchCreationSubject::Ellipse:
        {
            const Deliver<ProfileNameInFeature> Profile = Declared.DeclareEllipseProfile(
                ResolveEllipse(Declared.HeldPlane(), Context.Anchors[0], Context.Anchors[1], Context.Anchors[2]));
            if (!Profile)
                return Deliver<SketchCreationResult>::Refuse(Profile.Error);
            Result.Profile = Profile.Resolve();
            break;
        }

        case SketchCreationSubject::Oval:
        {
            const Deliver<ProfileNameInFeature> Profile = Declared.DeclareOvalProfile(
                ResolveEllipse(Declared.HeldPlane(), Context.Anchors[0], Context.Anchors[1], Context.Anchors[2]));
            if (!Profile)
                return Deliver<SketchCreationResult>::Refuse(Profile.Error);
            Result.Profile = Profile.Resolve();
            break;
        }

        case SketchCreationSubject::Polygon:
        {
            const SpatialDirection RadiusDirection = Difference(Context.Anchors[0], Context.Anchors[1]);
            const double Radius = std::sqrt(LengthSquared(RadiusDirection));
            const Deliver<ProfileNameInFeature> Profile = Declared.DeclareRegularPolygon(Context.Anchors[0], Radius,
                                                                                         Context.Requested.PolygonSideCount);
            if (!Profile)
                return Deliver<SketchCreationResult>::Refuse(Profile.Error);
            Result.Profile = Profile.Resolve();
            break;
        }

        case SketchCreationSubject::Slot:
        {
            // 📝 The same perpendicular measure the tool previews and the commit uses, so a slot
            //    created through this path is the one that was dragged out.
            const std::vector<SpatialPoint> Spine = { Context.Anchors[0], Context.Anchors[1] };
            const double Radius = ResolveSpineDistance(Spine, Context.Anchors[2]);
            const Deliver<ProfileNameInFeature> Profile = Declared.DeclareSlot(Context.Anchors[0], Context.Anchors[1], Radius);
            if (!Profile)
                return Deliver<SketchCreationResult>::Refuse(Profile.Error);
            Result.Profile = Profile.Resolve();
            break;
        }

        case SketchCreationSubject::Bezier:
            Result.CurveSet.push_back(Declared.DeclareBezier(Context.Anchors));
            break;

        case SketchCreationSubject::BasisSpline:
        {
            BasisSplineCurve Spline;
            Spline.ControlPoints = Context.Anchors;
            Spline.Degree = Context.Requested.CurveDegree;
            Result.CurveSet.push_back(Declared.DeclareBasisSpline(Spline));
            break;
        }

        case SketchCreationSubject::RationalSpline:
        {
            RationalSplineCurve Spline;
            Spline.ControlPoints = Context.Anchors;
            Spline.Degree = Context.Requested.CurveDegree;
            Spline.Weights.assign(Context.Anchors.size(), 1.0);
            Result.CurveSet.push_back(Declared.DeclareRationalSpline(Spline));
            break;
        }

        case SketchCreationSubject::Hermite:
        {
            HermiteCurve Curve;
            Curve.StartPoint = Context.Anchors[0];
            Curve.EndPoint = Context.Anchors[1];
            Curve.StartTangent = Difference(Context.Anchors[0], Context.Anchors[2]);
            Curve.EndTangent = Difference(Context.Anchors[1], Context.Anchors[3]);
            Result.CurveSet.push_back(Declared.DeclareHermite(Curve));
            break;
        }

        case SketchCreationSubject::SubjectCount:
            return Deliver<SketchCreationResult>::Refuse({ RefusalReason::ContentUnsupported, "no such primitive subject" });
    }

    Result.Produced = true;
    Context = {};
    return Deliver<SketchCreationResult>::Result(Result);
}

} // namespace Slate
