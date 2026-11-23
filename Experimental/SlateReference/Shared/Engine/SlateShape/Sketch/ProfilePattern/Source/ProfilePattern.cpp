//============================================================================================================================================
//                                                        PROFILEPATTERN.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/ProfilePattern/Api/ProfilePattern.h"

#include <cmath>

namespace Slate
{

namespace
{
    SpatialPoint MirrorPoint(const SpatialPoint& Subject,
                             const SpatialPoint& AxisStart,
                             const SpatialPoint& AxisEnd)
    {
        const SpatialDirection AxisDirection = Normalize(Difference(AxisStart, AxisEnd));
        const SpatialDirection Offset = Difference(AxisStart, Subject);
        const double Projection = Dot(Offset, AxisDirection);
        const SpatialPoint Closest = Added(AxisStart, Scaled(AxisDirection, Projection));
        const SpatialDirection Delta = Difference(Closest, Subject);
        return Added(Subject, Scaled(Delta, 2.0));
    }

    CurveSpecification OffsetCurve(const CurveSpecification& Geometry,
                                   const SpatialDirection& Offset)
    {
        CurveSpecification Shifted = Geometry;
        switch (Shifted.Subject())
        {
            case CurveSubject::Line:
                Shifted.HeldLine().Origin = Added(Shifted.HeldLine().Origin, Offset);
                Shifted.HeldLine().Terminus = Added(Shifted.HeldLine().Terminus, Offset);
                break;
            case CurveSubject::CircularArc:
                Shifted.HeldCircularArc().Centre = Added(Shifted.HeldCircularArc().Centre, Offset);
                if (Shifted.HeldCircularArc().ThroughDeclared)
                    Shifted.HeldCircularArc().ThroughPoint = Added(Shifted.HeldCircularArc().ThroughPoint, Offset);
                break;
            case CurveSubject::Circle:
                Shifted.HeldCircle().Centre = Added(Shifted.HeldCircle().Centre, Offset);
                break;
            case CurveSubject::EllipticalArc:
                Shifted.HeldEllipticalArc().Centre = Added(Shifted.HeldEllipticalArc().Centre, Offset);
                break;
            case CurveSubject::Ellipse:
                Shifted.HeldEllipse().Centre = Added(Shifted.HeldEllipse().Centre, Offset);
                break;
            case CurveSubject::Bezier:
                for (SpatialPoint& Point : Shifted.HeldBezier().ControlPoints) Point = Added(Point, Offset);
                break;
            case CurveSubject::BasisSpline:
                for (SpatialPoint& Point : Shifted.HeldBasisSpline().ControlPoints) Point = Added(Point, Offset);
                break;
            case CurveSubject::RationalSpline:
                for (SpatialPoint& Point : Shifted.HeldRationalSpline().ControlPoints) Point = Added(Point, Offset);
                break;
            case CurveSubject::Hermite:
                for (SpatialPoint& Point : Shifted.HeldHermite().ControlPoints) Point = Added(Point, Offset);
                Shifted.HeldHermite().StartPoint = Added(Shifted.HeldHermite().StartPoint, Offset);
                Shifted.HeldHermite().EndPoint = Added(Shifted.HeldHermite().EndPoint, Offset);
                break;
            case CurveSubject::SubjectCount:
                break;
        }
        return Shifted;
    }

    CurveSpecification MirrorCurve(const CurveSpecification& Geometry,
                                   const SpatialPoint& AxisStart,
                                   const SpatialPoint& AxisEnd)
    {
        CurveSpecification Mirrored = Geometry;
        switch (Mirrored.Subject())
        {
            case CurveSubject::Line:
                Mirrored.HeldLine().Origin = MirrorPoint(Mirrored.HeldLine().Origin, AxisStart, AxisEnd);
                Mirrored.HeldLine().Terminus = MirrorPoint(Mirrored.HeldLine().Terminus, AxisStart, AxisEnd);
                break;
            case CurveSubject::CircularArc:
                Mirrored.HeldCircularArc().Centre = MirrorPoint(Mirrored.HeldCircularArc().Centre, AxisStart, AxisEnd);
                if (Mirrored.HeldCircularArc().ThroughDeclared)
                    Mirrored.HeldCircularArc().ThroughPoint = MirrorPoint(Mirrored.HeldCircularArc().ThroughPoint, AxisStart, AxisEnd);
                break;
            case CurveSubject::Circle:
                Mirrored.HeldCircle().Centre = MirrorPoint(Mirrored.HeldCircle().Centre, AxisStart, AxisEnd);
                break;
            case CurveSubject::EllipticalArc:
                Mirrored.HeldEllipticalArc().Centre = MirrorPoint(Mirrored.HeldEllipticalArc().Centre, AxisStart, AxisEnd);
                break;
            case CurveSubject::Ellipse:
                Mirrored.HeldEllipse().Centre = MirrorPoint(Mirrored.HeldEllipse().Centre, AxisStart, AxisEnd);
                break;
            case CurveSubject::Bezier:
                for (SpatialPoint& Point : Mirrored.HeldBezier().ControlPoints) Point = MirrorPoint(Point, AxisStart, AxisEnd);
                break;
            case CurveSubject::BasisSpline:
                for (SpatialPoint& Point : Mirrored.HeldBasisSpline().ControlPoints) Point = MirrorPoint(Point, AxisStart, AxisEnd);
                break;
            case CurveSubject::RationalSpline:
                for (SpatialPoint& Point : Mirrored.HeldRationalSpline().ControlPoints) Point = MirrorPoint(Point, AxisStart, AxisEnd);
                break;
            case CurveSubject::Hermite:
                for (SpatialPoint& Point : Mirrored.HeldHermite().ControlPoints) Point = MirrorPoint(Point, AxisStart, AxisEnd);
                Mirrored.HeldHermite().StartPoint = MirrorPoint(Mirrored.HeldHermite().StartPoint, AxisStart, AxisEnd);
                Mirrored.HeldHermite().EndPoint = MirrorPoint(Mirrored.HeldHermite().EndPoint, AxisStart, AxisEnd);
                break;
            case CurveSubject::SubjectCount:
                break;
        }
        return Mirrored;
    }

    Deliver<PatternResult> DuplicateCurveSet(SketchStructure& Declared,
                                             const std::vector<SketchCurveName>& CurveSet,
                                             const SpatialDirection& Offset)
    {
        PatternResult Result;
        for (SketchCurveName Subject : CurveSet)
        {
            if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
                return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "one duplicated curve is absent" });
            Result.CurveSet.push_back(Declared.DeclareCurve(OffsetCurve(Declared.Curves()[Subject.IssuedIndex - 1u].Geometry, Offset)));
        }
        return Deliver<PatternResult>::Result(Result);
    }

    Deliver<PatternResult> DuplicateProfileSet(SketchStructure& Declared,
                                               const std::vector<ProfileNameInFeature>& ProfileSet,
                                               const SpatialDirection& Offset)
    {
        PatternResult Result;
        for (ProfileNameInFeature Subject : ProfileSet)
        {
            if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Profiles().size())
                return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "one duplicated profile is absent" });
            const ProfileSpecification& Source = Declared.Profiles()[Subject.IssuedIndex - 1u];
            ProfileSpecification Copy;
            Copy.DeclarePlane({ Added(Source.HeldPlane().Origin, Offset), Source.HeldPlane().Normal, Source.HeldPlane().AlongDirection });
            for (const ProfileLoop& Loop : Source.HeldLoops())
            {
                ProfileLoop CopyLoop;
                CopyLoop.Orientation = Loop.Orientation;
                for (const ProfileCurveUse& Use : Loop.Traversal)
                {
                    if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > Declared.Curves().size())
                        return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "one profile traversal curve is absent" });
                    const CurveSpecification Shifted = OffsetCurve(Declared.Curves()[Use.TraversedCurve.IssuedIndex - 1u].Geometry, Offset);
                    const SketchCurveName Curve = Declared.DeclareCurve(Shifted);
                    CopyLoop.Traversal.push_back({ { Curve.IssuedIndex }, Use.SameSense });
                }
                Copy.DeclareLoop(CopyLoop);
            }
            Result.ProfileSet.push_back(Declared.DeclareProfile(Copy));
        }
        return Deliver<PatternResult>::Result(Result);
    }
}

Deliver<PatternResult> DuplicateCurves(SketchStructure& Declared,
                                       const std::vector<SketchCurveName>& CurveSet,
                                       const SpatialDirection& Offset)
{
    return DuplicateCurveSet(Declared, CurveSet, Offset);
}

Deliver<PatternResult> DuplicateProfiles(SketchStructure& Declared,
                                         const std::vector<ProfileNameInFeature>& ProfileSet,
                                         const SpatialDirection& Offset)
{
    return DuplicateProfileSet(Declared, ProfileSet, Offset);
}

Deliver<PatternResult> DuplicateBetween(SketchStructure& Declared,
                                        const std::vector<SketchCurveName>& CurveSet,
                                        const SpatialPoint& StartPoint,
                                        const SpatialPoint& EndPoint)
{
    return DuplicateCurveSet(Declared, CurveSet, Difference(StartPoint, EndPoint));
}

Deliver<PatternResult> MirrorCurves(SketchStructure& Declared,
                                    const std::vector<SketchCurveName>& CurveSet,
                                    const SpatialPoint& AxisStart,
                                    const SpatialPoint& AxisEnd)
{
    PatternResult Result;
    for (SketchCurveName Subject : CurveSet)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
            return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "one mirrored curve is absent" });
        Result.CurveSet.push_back(Declared.DeclareCurve(MirrorCurve(Declared.Curves()[Subject.IssuedIndex - 1u].Geometry, AxisStart, AxisEnd)));
    }
    return Deliver<PatternResult>::Result(Result);
}

Deliver<PatternResult> MirrorProfiles(SketchStructure& Declared,
                                      const std::vector<ProfileNameInFeature>& ProfileSet,
                                      const SpatialPoint& AxisStart,
                                      const SpatialPoint& AxisEnd)
{
    PatternResult Result;
    for (ProfileNameInFeature Subject : ProfileSet)
    {
        if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Profiles().size())
            return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "one mirrored profile is absent" });
        const ProfileSpecification& Source = Declared.Profiles()[Subject.IssuedIndex - 1u];
        ProfileSpecification Copy;
        Copy.DeclarePlane(Source.HeldPlane());
        for (const ProfileLoop& Loop : Source.HeldLoops())
        {
            ProfileLoop CopyLoop;
            CopyLoop.Orientation = Loop.Orientation;
            for (const ProfileCurveUse& Use : Loop.Traversal)
            {
                if (!Use.TraversedCurve.Assigned() || Use.TraversedCurve.IssuedIndex > Declared.Curves().size())
                    return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "one profile traversal curve is absent" });
                const CurveSpecification Reflected = MirrorCurve(Declared.Curves()[Use.TraversedCurve.IssuedIndex - 1u].Geometry, AxisStart, AxisEnd);
                const SketchCurveName Curve = Declared.DeclareCurve(Reflected);
                CopyLoop.Traversal.push_back({ { Curve.IssuedIndex }, Use.SameSense });
            }
            Copy.DeclareLoop(CopyLoop);
        }
        Result.ProfileSet.push_back(Declared.DeclareProfile(Copy));
    }
    return Deliver<PatternResult>::Result(Result);
}

Deliver<PatternResult> DeclareLinearPattern(SketchStructure& Declared,
                                            const std::vector<SketchCurveName>& CurveSet,
                                            const SpatialDirection& Step,
                                            std::uint32_t Count)
{
    if (Count < 2u)
        return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "a linear repeat requires at least two copies" });

    PatternResult Result;
    for (std::uint32_t CopyIndex = 1u; CopyIndex < Count; ++CopyIndex)
    {
        const Deliver<PatternResult> Produced = DuplicateCurveSet(Declared, CurveSet, Scaled(Step, static_cast<double>(CopyIndex)));
        if (!Produced)
            return Produced;
        Result.CurveSet.insert(Result.CurveSet.end(), Produced.Resolve().CurveSet.begin(), Produced.Resolve().CurveSet.end());
    }
    return Deliver<PatternResult>::Result(Result);
}

Deliver<PatternResult> DeclareRadialPattern(SketchStructure& Declared,
                                            const std::vector<SketchCurveName>& CurveSet,
                                            const SpatialPoint& Centre,
                                            const SpatialDirection& Axis,
                                            double SweepRadians,
                                            std::uint32_t Count)
{
    if (Count < 2u)
        return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "a radial repeat requires at least two copies" });

    PatternResult Result;
    const double StepRadians = SweepRadians / static_cast<double>(Count - 1u);

    for (std::uint32_t CopyIndex = 1u; CopyIndex < Count; ++CopyIndex)
    {
        for (SketchCurveName Subject : CurveSet)
        {
            if (!Subject.Assigned() || Subject.IssuedIndex > Declared.Curves().size())
                return Deliver<PatternResult>::Refuse({ RefusalReason::ContentUnsupported, "one repeated curve is absent" });
            CurveSpecification Rotated = Declared.Curves()[Subject.IssuedIndex - 1u].Geometry;
            auto RotatePoint = [&](SpatialPoint& Position)
            {
                const SpatialDirection Offset = Difference(Centre, Position);
                const SpatialDirection RotatedOffset = RotateAroundAxis(Offset, Axis, StepRadians * static_cast<double>(CopyIndex));
                Position = Added(Centre, RotatedOffset);
            };

            switch (Rotated.Subject())
            {
                case CurveSubject::Line:
                    RotatePoint(Rotated.HeldLine().Origin);
                    RotatePoint(Rotated.HeldLine().Terminus);
                    break;
                case CurveSubject::CircularArc:
                    RotatePoint(Rotated.HeldCircularArc().Centre);
                    break;
                case CurveSubject::Circle:
                    RotatePoint(Rotated.HeldCircle().Centre);
                    break;
                case CurveSubject::EllipticalArc:
                    RotatePoint(Rotated.HeldEllipticalArc().Centre);
                    break;
                case CurveSubject::Ellipse:
                    RotatePoint(Rotated.HeldEllipse().Centre);
                    break;
                case CurveSubject::Bezier:
                    for (SpatialPoint& Point : Rotated.HeldBezier().ControlPoints) RotatePoint(Point);
                    break;
                case CurveSubject::BasisSpline:
                    for (SpatialPoint& Point : Rotated.HeldBasisSpline().ControlPoints) RotatePoint(Point);
                    break;
                case CurveSubject::RationalSpline:
                    for (SpatialPoint& Point : Rotated.HeldRationalSpline().ControlPoints) RotatePoint(Point);
                    break;
                case CurveSubject::Hermite:
                    for (SpatialPoint& Point : Rotated.HeldHermite().ControlPoints) RotatePoint(Point);
                    RotatePoint(Rotated.HeldHermite().StartPoint);
                    RotatePoint(Rotated.HeldHermite().EndPoint);
                    break;
                case CurveSubject::SubjectCount:
                    break;
            }

            Result.CurveSet.push_back(Declared.DeclareCurve(Rotated));
        }
    }

    return Deliver<PatternResult>::Result(Result);
}

} // namespace Slate
