//============================================================================================================================================
//                                                       PLACEMENTCOMMIT.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/PlacementCommit/Api/PlacementCommit.h"

#include "SlateShape/Sketch/ConstraintSolver/Api/ConstraintSolver.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Slate
{

namespace
{

constexpr double CommitPi = 3.14159265358979323846;

SpatialBasis ResolvePlacementBasis(const SketchPlane& Plane)
{
    const SpatialDirection Along = Normalize(Plane.AlongDirection);
    const SpatialDirection Normal = Normalize(Plane.Normal);
    return { Plane.Origin, Along, Normalize(Cross(Normal, Along)), Normal };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHARED BY THE ARMS
//------------------------------------------------------------------------------------------------------------------------

ReferenceSpecification ReferenceFromPoint(SketchPointName Point)
{
    ReferenceSpecification Reference = {};
    Reference.Subject = ReferenceSubject::SketchPoint;
    Reference.SketchPoint = Point;
    return Reference;
}

ReferenceSpecification ReferenceFromCurve(SketchCurveName Curve)
{
    ReferenceSpecification Reference = {};
    Reference.Subject = ReferenceSubject::SketchCurve;
    Reference.SketchCurve = Curve;
    return Reference;
}


SketchPointName EncodePlacedPointName(SketchCurveName Curve, std::uint32_t LocalIndex)
{
    return { (Curve.IssuedIndex << 8u) | ((LocalIndex + 1u) & 0xFFu) };
}

ReferenceSpecification ReferenceFromSnap(const SketchSnapPlacement& Snap)
{
    ReferenceSpecification Reference = {};
    if (Snap.SketchPoint.Assigned())
    {
        Reference.Subject = ReferenceSubject::SketchPoint;
        Reference.SketchPoint = Snap.SketchPoint;
    }
    else if (Snap.SketchControl.Assigned())
    {
        Reference.Subject = ReferenceSubject::SketchControl;
        Reference.SketchControl = Snap.SketchControl;
    }
    else if (Snap.SourceCurve.Assigned())
    {
        Reference.Subject = ReferenceSubject::SketchCurve;
        Reference.SketchCurve = Snap.SourceCurve;
    }
    return Reference;
}


bool ResolveThreePointCircle(const SpatialPoint& A,
                             const SpatialPoint& B,
                             const SpatialPoint& C,
                             SpatialPoint& Centre,
                             double& Radius)
{
    const double D = 2.0 * (A.Left * (B.Forward - C.Forward) + B.Left * (C.Forward - A.Forward) + C.Left * (A.Forward - B.Forward));
    if (std::abs(D) <= 1.0e-9)
        return false;
    const double A2 = A.Left * A.Left + A.Forward * A.Forward;
    const double B2 = B.Left * B.Left + B.Forward * B.Forward;
    const double C2 = C.Left * C.Left + C.Forward * C.Forward;
    Centre.Left = (A2 * (B.Forward - C.Forward) + B2 * (C.Forward - A.Forward) + C2 * (A.Forward - B.Forward)) / D;
    Centre.Forward = (A2 * (C.Left - B.Left) + B2 * (A.Left - C.Left) + C2 * (B.Left - A.Left)) / D;
    Centre.Up = A.Up;
    Radius = std::sqrt(LengthSquared(Difference(Centre, A)));
    return Radius > 1.0e-6;
}

void AddLineAutoConstraints(WorkspaceNameIndex& Naming,
                            SketchStructure& Sketch,
                            const SketchPlane& Plane,
                            WorkspaceRecordStructure& Records,
                            PlacementJournal& Revisions,
                            SketchCurveName Curve,
                            const SpatialPoint& StartPoint,
                            const SpatialPoint& EndPoint,
                            const SketchSnapPlacement* StartSnap,
                            const SketchSnapPlacement* EndSnap,
                            std::vector<WorkspaceRecordName>& Written)
{
    const SpatialBasis Basis = ResolvePlacementBasis(Plane);
    double StartAlong = 0.0, StartAcross = 0.0, EndAlong = 0.0, EndAcross = 0.0;
    ResolvePlaneCoordinates(Basis, StartPoint, StartAlong, StartAcross);
    ResolvePlaneCoordinates(Basis, EndPoint, EndAlong, EndAcross);
    const double Span = std::sqrt((EndAlong - StartAlong) * (EndAlong - StartAlong) +
                                  (EndAcross - StartAcross) * (EndAcross - StartAcross));
    if (Span > 1.0e-6)
    {
        ConstraintSpecification Axis = {};
        Axis.Primary = ReferenceFromCurve(Curve);
        if (std::fabs(EndAcross - StartAcross) <= std::max(Span * 0.015, 0.05))
        {
            Axis.Subject = ConstraintSubject::Horizontal;
            SealConstraintRecord(Naming, Records, Revisions, Sketch, Axis, Written);
        }
        else if (std::fabs(EndAlong - StartAlong) <= std::max(Span * 0.015, 0.05))
        {
            Axis.Subject = ConstraintSubject::Vertical;
            SealConstraintRecord(Naming, Records, Revisions, Sketch, Axis, Written);
        }
    }

    const SketchPointName StartNamed = EncodePlacedPointName(Curve, 0u);
    const SketchPointName EndNamed = EncodePlacedPointName(Curve, 1u);
    const SketchSnapPlacement* Snaps[2] = { StartSnap, EndSnap };
    const SketchPointName NewPoints[2] = { StartNamed, EndNamed };
    for (std::uint32_t Index = 0u; Index < 2u; ++Index)
    {
        if (Snaps[Index] == nullptr || !Snaps[Index]->SketchPoint.Assigned())
            continue;
        ConstraintSpecification Coincident = {};
        Coincident.Subject = ConstraintSubject::Coincident;
        Coincident.Primary = ReferenceFromPoint(NewPoints[Index]);
        Coincident.Secondary = ReferenceFromPoint(Snaps[Index]->SketchPoint);
        SealConstraintRecord(Naming, Records, Revisions, Sketch, Coincident, Written);
    }
}

Deliver<WorkspaceRecordName> DeclareLine(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SketchCurveName Curve = Sketch.DeclareLine(Placed.Anchors[0], Placed.Anchors[1]);
        const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::Line);
        std::vector<WorkspaceRecordName> Written = { Record };
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming),
                       Placed.Construction ? "Create Construction Curve" : "Create Curve", { Record },
                       Revisions.DeclaredCount() + 1u);
        AddLineAutoConstraints(Naming, Sketch, Plane, Records, Revisions, Curve,
                               Placed.Anchors[0], Placed.Anchors[1],
                               Placed.Placements.size() > 0u ? &Placed.Placements[0] : nullptr,
                               Placed.Placements.size() > 1u ? &Placed.Placements[1] : nullptr,
                               Written);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclarePoint(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SpatialPoint Tip = Added(Placed.Anchors.back(), Scaled(Normalize(Plane.AlongDirection), 0.001));
        const SketchCurveName Curve = Sketch.DeclareLine(Placed.Anchors.back(), Tip);
        const WorkspaceRecordName Record = DeclareWorkspacePoint(Naming, Records, EncodePlacedPointName(Curve, 0u));
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Point", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

// 🔴 WHETHER A RUN OF POINTS COMES BACK TO WHERE IT STARTED. The snap that lets an artist land the
//    last point exactly on the first is what makes this exact rather than approximate, but a hand-
//    placed point a hair away is still plainly meant to close -- so the test is scaled to the shape's
//    own size rather than being an absolute distance. A tolerance in world units would close a
//    millimetre-wide shape that was never meant to and refuse a kilometre-wide one that was.
bool ClosesOnItself(const std::vector<SpatialPoint>& Anchors)
{
    if (Anchors.size() < 3u)
        return false;

    double Longest = 0.0;
    for (std::size_t Index = 0u; Index + 1u < Anchors.size(); ++Index)
        Longest = std::max(Longest, LengthSquared(Difference(Anchors[Index], Anchors[Index + 1u])));

    if (Longest <= 0.0)
        return false;

    // 📝 A hundredth of the longest leg: comfortably inside snapping distance, far too small to close
    //    a shape the artist deliberately left open.
    const double Tolerance = std::sqrt(Longest) * 0.01;
    return LengthSquared(Difference(Anchors.front(), Anchors.back())) <= Tolerance * Tolerance;
}

Deliver<WorkspaceRecordName> DeclarePolyline(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        std::vector<SketchCurveName> Curves;
        const Deliver<bool> Declared = Sketch.DeclarePolyline(Placed.Anchors, Curves);
        if (!Declared.Resolved)
            return Deliver<WorkspaceRecordName>::Refuse(Declared.Error);

        // 🔴 A POLYLINE THAT COMES BACK TO ITS START ENCLOSES SOMETHING. It committed as a run of
        //    separate lines that happened to meet, so nothing downstream knew there was an inside:
        //    it could not be shaded and it could not be extruded or lofted. Sealed as a profile it
        //    is a region, and the semi-transparent fill every other closed shape already gets falls
        //    out of the render path with no special case.
        //
        // ⚠️ Construction geometry is never a region -- it exists to be measured from, not filled --
        //    and the artist can decline the region with the closed-profile toggle.
        if (!Placed.Construction && Placed.ClosedProfile && Curves.size() >= 3u
            && ClosesOnItself(Placed.Anchors))
        {
            ProfileSpecification Profile;
            Profile.DeclarePlane({ Plane.Origin, Plane.Normal,
                                   Plane.AlongDirection });
            ProfileLoop Loop;
            Loop.Orientation = ProfileLoopOrientation::Outer;
            for (const SketchCurveName& Curve : Curves)
                Loop.Traversal.push_back({ { Curve.IssuedIndex }, true });
            Profile.DeclareLoop(Loop);

            const WorkspaceRecordName Record =
                DeclareWorkspaceProfile(Naming, Records, Sketch.DeclareProfile(Profile), WorkspaceShapeFamily::Polygon);
            Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming),
                           "Create Closed Polyline", { Record }, Revisions.DeclaredCount() + 1u);
            return Deliver<WorkspaceRecordName>::Result(Record);
        }

        std::vector<WorkspaceRecordName> RecordsWritten;
        RecordsWritten.reserve(Curves.size());
        for (std::uint32_t Index = 0u; Index < Curves.size(); ++Index)
        {
            SketchCurveName Curve = Curves[Index];
            RecordsWritten.push_back(DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::Polygon));
            AddLineAutoConstraints(Naming, Sketch, Plane, Records, Revisions, Curve,
                                   Placed.Anchors[Index], Placed.Anchors[Index + 1u],
                                   Placed.Placements.size() > Index ? &Placed.Placements[Index] : nullptr,
                                   Placed.Placements.size() > Index + 1u ? &Placed.Placements[Index + 1u] : nullptr,
                                   RecordsWritten);
        }
        Revisions.Seal("Declared polyline", Placed.Construction ? "Create Construction Polyline" : "Create Polyline",
                       RecordsWritten, Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(RecordsWritten.empty() ? WorkspaceRecordName{} : RecordsWritten.front());
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareThreePointArc(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& /*Plane*/,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        if (!ArcReady(Placed.Anchors[0], Placed.Anchors[1], Placed.Anchors[2]))
            return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                          "the arc points are collinear" });
        const SketchCurveName Curve = Sketch.DeclareThreePointArc(Placed.Anchors[0], Placed.Anchors[1], Placed.Anchors[2]);
        const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::CircularArc);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming),
                       Placed.Construction ? "Create Construction Arc" : "Create Arc", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareCentredArc(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SpatialPoint Centre = Placed.Anchors[0];
        const SpatialPoint Start = Placed.Anchors[1];
        const SpatialPoint End = Placed.Anchors[2];
        const double Radius = std::sqrt(LengthSquared(Difference(Centre, Start)));
        if (Radius <= 1.0e-6)
            return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported, "the arc radius is too small" });
        const double A0 = std::atan2(Start.Forward - Centre.Forward, Start.Left - Centre.Left);
        const double A1 = std::atan2(End.Forward - Centre.Forward, End.Left - Centre.Left);
        double Sweep = A1 - A0;
        if (Sweep <= 0.0)
            Sweep += 2.0 * CommitPi;
        const CircularArcCurve Arc = { Centre, Plane.Normal, Normalize(Difference(Centre, Start)), {}, false, Radius, Sweep };
        const SketchCurveName Curve = Sketch.DeclareCurve(CurveSpecification::DeclareCircularArc(Arc, { 0.0, 1.0 }));
        const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::CircularArc);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming),
                       Placed.Method == PlacementMethod::Tangent ? "Create Tangent Arc" : "Create Centred Arc", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}



Deliver<WorkspaceRecordName> DeclareBasisSpline(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& /*Plane*/,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        BasisSplineCurve Spline;
        Spline.ControlPoints = Placed.Anchors;
        Spline.Degree = std::min<std::uint32_t>(3u, static_cast<std::uint32_t>(Spline.ControlPoints.size() - 1u));
        Spline.Periodic = false;
        const SketchCurveName Curve = Sketch.DeclareBasisSpline(Spline);
        const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::BasisSpline);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Basis Spline", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareRationalSpline(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& /*Plane*/,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        RationalSplineCurve Spline;
        Spline.ControlPoints = Placed.Anchors;
        Spline.Weights.assign(Placed.Anchors.size(), 1.0);
        Spline.Degree = std::min<std::uint32_t>(3u, static_cast<std::uint32_t>(Spline.ControlPoints.size() - 1u));
        Spline.Periodic = false;
        const SketchCurveName Curve = Sketch.DeclareRationalSpline(Spline);
        const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::Nurbs);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create NURBS Curve", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareHermite(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& /*Plane*/,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
    if (Placed.Anchors.size() < 2u)
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                      "a Hermite curve requires at least two distinct positions" });

    HermiteCurve Hermite;
    Hermite.ControlPoints = Placed.Anchors;
    Hermite.StartPoint = Placed.Anchors.front();
    Hermite.EndPoint = Placed.Anchors.back();
    Hermite.Tangents.resize(Placed.Anchors.size());
    for (std::size_t i = 0u; i < Placed.Anchors.size(); ++i)
    {
        const SpatialPoint& Before = (i == 0u) ? Placed.Anchors[0] : Placed.Anchors[i - 1u];
        const SpatialPoint& After = (i + 1u == Placed.Anchors.size()) ? Placed.Anchors[i] : Placed.Anchors[i + 1u];
        Hermite.Tangents[i] = Scaled(Difference(Before, After), 0.5);
    }
    Hermite.StartTangent = Hermite.Tangents.front();
    Hermite.EndTangent = Hermite.Tangents.back();

    const SketchCurveName Curve = Sketch.DeclareCurve(CurveSpecification::DeclareHermite(Hermite, { 0.0, 1.0 }));
    const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::Hermite);

    Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Hermite Curve", { Record },
                   Revisions.DeclaredCount() + 1u);
    return Deliver<WorkspaceRecordName>::Result(Record);
}

Deliver<WorkspaceRecordName> DeclareDiameterCircle(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SpatialPoint A = Placed.Anchors[0];
        const SpatialPoint B = Placed.Anchors[1];
        const SpatialPoint Centre = { (A.Left + B.Left) * 0.5, (A.Up + B.Up) * 0.5, (A.Forward + B.Forward) * 0.5 };
        const double Radius = std::sqrt(LengthSquared(Difference(Centre, A)));
        if (Radius <= 1.0e-6)
            return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported, "the circle radius is too small" });
        const CircleCurve Circle = { Centre, Plane.Normal, Normalize(Difference(Centre, A)), Radius };
        const Deliver<ProfileNameInFeature> Profile = Placed.Construction ? Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "construction circle" })
                                                                         : Sketch.DeclareCircleProfile(Circle, Plane);
        if (Profile.Resolved)
        {
            const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Profile.Resolve(), WorkspaceShapeFamily::Circle);
            Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Diameter Circle", { Record },
                           Revisions.DeclaredCount() + 1u);
            return Deliver<WorkspaceRecordName>::Result(Record);
        }
        const SketchCurveName Curve = Sketch.DeclareCircle(Circle);
        const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, true, WorkspaceShapeFamily::Circle);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Construction Diameter Circle", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareThreePointCircle(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        SpatialPoint Centre = {};
        double Radius = 0.0;
        if (!ResolveThreePointCircle(Placed.Anchors[0], Placed.Anchors[1], Placed.Anchors[2], Centre, Radius))
            return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported, "the circle points are collinear" });
        const CircleCurve Circle = { Centre, Plane.Normal, Normalize(Difference(Centre, Placed.Anchors[0])), Radius };
        const Deliver<ProfileNameInFeature> Profile = Sketch.DeclareCircleProfile(Circle, Plane);
        if (!Profile.Resolved)
            return Deliver<WorkspaceRecordName>::Refuse(Profile.Error);
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Profile.Resolve(), WorkspaceShapeFamily::Circle);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Three Point Circle", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclarePolygon(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const double Radius = std::sqrt(LengthSquared(Difference(Placed.Anchors[0], Placed.Anchors[1])));

        // 🔴 THE SIDE COUNT COMES FROM THE PLACEMENT, NOT FROM THIS LINE. It was hardcoded to six, so
        //    the polygon tool drew a hexagon and only a hexagon however the artist set it up. The
        //    wheel drives `SealedPlacement::Resolution` while the shape is being placed; it is clamped
        //    here as well because a sealed placement arriving from anywhere else must still be sane.
        const std::uint32_t Sides = std::clamp(Placed.Resolution, PolygonSideMinimum, PolygonSideMaximum);
        // 📝 The drag direction is handed on, so the committed polygon is the previewed one.
        const Deliver<ProfileNameInFeature> Profile = Sketch.DeclareRegularPolygon(
            Placed.Anchors[0], Radius, Sides, Plane, Difference(Placed.Anchors[0], Placed.Anchors.back()));
        if (!Profile.Resolved)
            return Deliver<WorkspaceRecordName>::Refuse(Profile.Error);
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Profile.Resolve(), WorkspaceShapeFamily::Polygon);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Polygon", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareSlot(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
    if (Placed.Anchors.size() < 3u)
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported, "a slot requires spine points and a radius" });

    // 🔴 THE THICKNESS IS MEASURED TO THE SPINE, NOT TO ITS LAST POINT, and the preview measures it the
    //    same way. Taking it from the last point made the committed slot disagree with the previewed
    //    one by however far along the run the pointer had been: a pointer 20 out from the middle of a
    //    spine 100 long committed a radius of 53.852.
    std::vector<SpatialPoint> Spine(Placed.Anchors.begin(), Placed.Anchors.end() - 1);
    const double Radius = ResolveSpineDistance(Spine, Placed.Anchors.back());
    if (Radius <= 1.0e-6)
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported, "the slot radius is too small" });

    const Deliver<ProfileNameInFeature> Profile = Sketch.DeclarePolylineSlot(Spine, Radius, Plane);
    if (!Profile.Resolved)
        return Deliver<WorkspaceRecordName>::Refuse(Profile.Error);
    const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Profile.Resolve(), WorkspaceShapeFamily::Slot);
    Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Slot", { Record },
                   Revisions.DeclaredCount() + 1u);
    return Deliver<WorkspaceRecordName>::Result(Record);
}

Deliver<WorkspaceRecordName> DeclareThreePointRectangle(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SpatialPoint A = Placed.Anchors[0];
        const SpatialPoint B = Placed.Anchors[1];
        const SpatialPoint C = Placed.Anchors[2];
        const SpatialPoint D = Added(A, Difference(B, C));
        ProfileSpecification Profile;
        Profile.DeclarePlane({ Plane.Origin, Plane.Normal, Plane.AlongDirection });
        ProfileLoop Loop;
        Loop.Orientation = ProfileLoopOrientation::Outer;
        const SketchCurveName AB = Sketch.DeclareLine(A, B);
        const SketchCurveName BC = Sketch.DeclareLine(B, C);
        const SketchCurveName CD = Sketch.DeclareLine(C, D);
        const SketchCurveName DA = Sketch.DeclareLine(D, A);
        Loop.Traversal = { { { AB.IssuedIndex }, true }, { { BC.IssuedIndex }, true }, { { CD.IssuedIndex }, true }, { { DA.IssuedIndex }, true } };
        Profile.DeclareLoop(Loop);
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Sketch.DeclareProfile(Profile), WorkspaceShapeFamily::Rectangle);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Three Point Rectangle", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareDimension(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& /*Plane*/,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const ReferenceSpecification Primary = ReferenceFromSnap(Placed.Placements[0]);
        const ReferenceSpecification Secondary = ReferenceFromSnap(Placed.Placements[1]);
        if (!Primary.Declared() || !Secondary.Declared())
            return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                          "linear dimensions require two snapped sketch references" });
        DimensionSpecification Dimension = {};
        Dimension.Subject = DimensionSubject::Aligned;
        Dimension.Primary = Primary;
        Dimension.Secondary = Secondary;
        Dimension.Target = std::sqrt(LengthSquared(Difference(Placed.Anchors[0], Placed.Anchors[1])));
        const DimensionName DimensionNamed = Sketch.DeclareDimension(Dimension);
        const WorkspaceRecordName Record = DeclareWorkspaceDimension(Naming, Records, DimensionNamed);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Dimension", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareEllipse(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        // 🔴 THE COMMITTED ELLIPSE WAS NOT THE ONE PREVIEWED. This measured the drag along the
        //    PLANE'S OWN AXES -- |Δalong| by |Δacross| -- and then declared the major axis along the
        //    plane's `AlongDirection` regardless of where the artist had actually dragged. Dragging
        //    to (150, 60) previewed a 161.6-long ellipse tilted 22° and committed a 150 by 60 one
        const SpatialPoint MajorAnchor = Placed.Anchors.size() >= 2u ? Placed.Anchors[1] : Placed.Anchors.back();
        const SpatialDirection Span = Difference(Placed.Anchors[0], MajorAnchor);
        const double Major = std::sqrt(LengthSquared(Span));
        if (Major <= 1.0e-6)
            return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                          "the ellipse major radius is too small" });

        // 📝 A second anchor states the minor axis when the method asks for one; with only a centre
        //    and a rim point there is nothing to measure it from, so it is half the major -- the
        //    same figure the preview draws.
        double Minor = Major * 0.5;
        if (Placed.Anchors.size() >= 3u)
        {
            const double Stated = std::sqrt(LengthSquared(
                Difference(Placed.Anchors[0], Placed.Anchors[2])));
            if (Stated > 1.0e-6)
                Minor = Stated;
        }

        const EllipseCurve Ellipse = { Placed.Anchors[0], Plane.Normal,
                                       Normalize(Span), Major, Minor };
        if (Placed.Construction)
        {
            const SketchCurveName Curve = Sketch.DeclareEllipse(Ellipse);
            const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, true, WorkspaceShapeFamily::Ellipse);
            Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Construction Ellipse", { Record },
                           Revisions.DeclaredCount() + 1u);
            return Deliver<WorkspaceRecordName>::Result(Record);
        }
        const Deliver<ProfileNameInFeature> Profile = Sketch.DeclareEllipseProfile(Ellipse, Plane);
        if (!Profile.Resolved)
            return Deliver<WorkspaceRecordName>::Refuse(Profile.Error);
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Profile.Resolve(), WorkspaceShapeFamily::Ellipse);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Ellipse Profile", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareBezier(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& /*Plane*/,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SketchCurveName Curve = Sketch.DeclareBezier(Placed.Anchors);
        const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, WorkspaceShapeFamily::Bezier);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming),
                       Placed.Construction ? "Create Construction Bezier" : "Create Bezier", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareCentreRadiusCircle(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SpatialDirection Radius = Difference(Placed.Anchors[0], Placed.Anchors.back());
        const double RadiusLength = std::sqrt(LengthSquared(Radius));
        if (RadiusLength <= 1.0e-6)
            return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                          "the circle radius is too small" });

        const CircleCurve Circle = { Placed.Anchors[0], Plane.Normal, Normalize(Radius), RadiusLength };
        if (Placed.Construction)
        {
            const SketchCurveName Curve = Sketch.DeclareCircle(Circle);
            const WorkspaceRecordName Record = DeclareWorkspaceCurve(Naming, Records, Curve, true, WorkspaceShapeFamily::Circle);
            Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming),
                           "Create Construction Circle", { Record },
                           Revisions.DeclaredCount() + 1u);
            return Deliver<WorkspaceRecordName>::Result(Record);
        }

        const Deliver<ProfileNameInFeature> Profile = Sketch.DeclareCircleProfile(Circle, Plane);
        if (!Profile.Resolved)
            return Deliver<WorkspaceRecordName>::Refuse(Profile.Error);
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Profile.Resolve(), WorkspaceShapeFamily::Circle);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Profile", { Record },
                       Revisions.DeclaredCount() + 1u);
        DimensionSpecification RadiusDimension = {};
        RadiusDimension.Subject = DimensionSubject::Radius;
        RadiusDimension.Primary.Subject = ReferenceSubject::Profile;
        RadiusDimension.Primary.Profile = Profile.Resolve();
        RadiusDimension.Target = RadiusLength;
        if (RadiusDimension.Declared())
        {
            const DimensionName DimensionNamed = Sketch.DeclareDimension(RadiusDimension);
            const WorkspaceRecordName DimensionRecord = DeclareWorkspaceDimension(Naming, Records, DimensionNamed);
            Revisions.Seal("Declared " + std::string(Records.Resolve(DimensionRecord)->Naming), "Create Radius Dimension", { DimensionRecord },
                           Revisions.DeclaredCount() + 1u);
        }
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareCentredRectangle(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SpatialBasis Basis = ResolvePlacementBasis(Plane);
        const SpatialPoint Centre = Placed.Anchors[0];
        double CentreAlong = 0.0, CentreAcross = 0.0;
        double EndAlong = 0.0, EndAcross = 0.0;
        ResolvePlaneCoordinates(Basis, Centre, CentreAlong, CentreAcross);
        ResolvePlaneCoordinates(Basis, Placed.Anchors.back(), EndAlong, EndAcross);
        const SpatialPoint A = ResolvePlanarPoint(Basis,
                                                  CentreAlong + (CentreAlong - EndAlong),
                                                  CentreAcross + (CentreAcross - EndAcross));
        const SpatialPoint C = Placed.Anchors.back();
        const SpatialPoint B = ResolvePlanarPoint(Basis,
                                                  EndAlong,
                                                  CentreAcross + (CentreAcross - EndAcross));
        const SpatialPoint D = ResolvePlanarPoint(Basis,
                                                  CentreAlong + (CentreAlong - EndAlong),
                                                  EndAcross);
        ProfileSpecification Profile;
        Profile.DeclarePlane({ Plane.Origin, Plane.Normal, Plane.AlongDirection });
        ProfileLoop Loop;
        Loop.Orientation = ProfileLoopOrientation::Outer;
        const SketchCurveName AB = Sketch.DeclareLine(A, B);
        const SketchCurveName BC = Sketch.DeclareLine(B, C);
        const SketchCurveName CD = Sketch.DeclareLine(C, D);
        const SketchCurveName DA = Sketch.DeclareLine(D, A);
        Loop.Traversal = { { { AB.IssuedIndex }, true }, { { BC.IssuedIndex }, true }, { { CD.IssuedIndex }, true }, { { DA.IssuedIndex }, true } };
        Profile.DeclareLoop(Loop);
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, Sketch.DeclareProfile(Profile), WorkspaceShapeFamily::Rectangle);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Center Rectangle", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

Deliver<WorkspaceRecordName> DeclareExtentRectangle(WorkspaceNameIndex& Naming,
                                    SketchStructure& Sketch,
                                    const SketchPlane& Plane,
                                    WorkspaceRecordStructure& Records,
                                    PlacementJournal& Revisions,
                                    const SealedPlacement& Placed)
{
        const SpatialBasis Basis = ResolvePlacementBasis(Plane);
        const SpatialPoint A = Placed.Anchors[0];
        const SpatialPoint C = Placed.Anchors.back();
        double AAlong = 0.0, AAcross = 0.0, CAlong = 0.0, CAcross = 0.0;
        ResolvePlaneCoordinates(Basis, A, AAlong, AAcross);
        ResolvePlaneCoordinates(Basis, C, CAlong, CAcross);
        const SpatialPoint B = ResolvePlanarPoint(Basis, CAlong, AAcross);
        const SpatialPoint D = ResolvePlanarPoint(Basis, AAlong, CAcross);

        ProfileSpecification Profile;
        Profile.DeclarePlane({ Plane.Origin, Plane.Normal, Plane.AlongDirection });
        ProfileLoop Loop;
        Loop.Orientation = ProfileLoopOrientation::Outer;
        const SketchCurveName AB = Sketch.DeclareLine(A, B);
        const SketchCurveName BC = Sketch.DeclareLine(B, C);
        const SketchCurveName CD = Sketch.DeclareLine(C, D);
        const SketchCurveName DA = Sketch.DeclareLine(D, A);
        if (Placed.Construction)
        {
            const WorkspaceRecordName First = DeclareWorkspaceCurve(Naming, Records, AB, true);
            const WorkspaceRecordName Second = DeclareWorkspaceCurve(Naming, Records, BC, true);
            const WorkspaceRecordName Third = DeclareWorkspaceCurve(Naming, Records, CD, true);
            const WorkspaceRecordName Fourth = DeclareWorkspaceCurve(Naming, Records, DA, true);
            Revisions.Seal("Declared construction rectangle", "Create Construction Rectangle",
                           { First, Second, Third, Fourth }, Revisions.DeclaredCount() + 1u);
            return Deliver<WorkspaceRecordName>::Result(First);
        }
        Loop.Traversal = { { { AB.IssuedIndex }, true }, { { BC.IssuedIndex }, true },
                           { { CD.IssuedIndex }, true }, { { DA.IssuedIndex }, true } };
        Profile.DeclareLoop(Loop);
        const ProfileNameInFeature ProfileName = Sketch.DeclareProfile(Profile);
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, ProfileName, WorkspaceShapeFamily::Rectangle);
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Profile", { Record },
                       Revisions.DeclaredCount() + 1u);
        return Deliver<WorkspaceRecordName>::Result(Record);
    return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                  "the placement does not describe a shape" });
}

/// 🧩 The dispatch table: which declarer answers which pair.
/// note 🔴 BOTH AXES, ALWAYS. A row matching only the subject is what let a centred circle be built as a
///       centre-and-radius circle and left the centred-arc code unreachable.
struct CommitRow
{
    SketchSubject   Subject;
    PlacementMethod Method;
    std::uint32_t   Required;
    Deliver<WorkspaceRecordName> (*Declare)(WorkspaceNameIndex&,
                                            SketchStructure&,
                                            const SketchPlane&,
                                            WorkspaceRecordStructure&,
                                            PlacementJournal&,
                                            const SealedPlacement&);
};

constexpr CommitRow CommitTable[] =
{
    { SketchSubject::Point,          PlacementMethod::Extent,     1u, DeclarePoint },
    { SketchSubject::Line,           PlacementMethod::Extent,     2u, DeclareLine },
    { SketchSubject::Polyline,       PlacementMethod::Extent,     2u, DeclarePolyline },
    { SketchSubject::Bezier,         PlacementMethod::Extent,     2u, DeclareBezier },
    { SketchSubject::BasisSpline,    PlacementMethod::Extent,     3u, DeclareBasisSpline },
    { SketchSubject::RationalSpline, PlacementMethod::Extent,     3u, DeclareRationalSpline },
    { SketchSubject::Hermite,        PlacementMethod::Extent,     2u, DeclareHermite },
    { SketchSubject::Dimension,      PlacementMethod::Extent,     2u, DeclareDimension },
    { SketchSubject::Slot,           PlacementMethod::Extent,     2u, DeclareSlot },

    // 🔴 The arc arms the host could never reach. `Centred` and `Tangent` both take a centre, a start
    //    and an end; `ThreePoint` and the bare method take three points ON the arc. Those are different
    //    shapes from the same three anchors, and the host built the second for all four.
    { SketchSubject::Arc,            PlacementMethod::Extent,     3u, DeclareThreePointArc },
    { SketchSubject::Arc,            PlacementMethod::ThreePoint, 3u, DeclareThreePointArc },
    { SketchSubject::Arc,            PlacementMethod::Centred,    3u, DeclareCentredArc },
    { SketchSubject::Arc,            PlacementMethod::Tangent,    3u, DeclareCentredArc },

    { SketchSubject::Circle,         PlacementMethod::Extent,     2u, DeclareCentreRadiusCircle },
    { SketchSubject::Circle,         PlacementMethod::Centred,    2u, DeclareCentreRadiusCircle },
    { SketchSubject::Circle,         PlacementMethod::Diameter,   2u, DeclareDiameterCircle },
    { SketchSubject::Circle,         PlacementMethod::ThreePoint, 3u, DeclareThreePointCircle },

    { SketchSubject::Ellipse,        PlacementMethod::Extent,     3u, DeclareEllipse },
    { SketchSubject::Ellipse,        PlacementMethod::Centred,    3u, DeclareEllipse },
    { SketchSubject::Ellipse,        PlacementMethod::Diameter,   3u, DeclareEllipse },

    { SketchSubject::Rectangle,      PlacementMethod::Extent,     2u, DeclareExtentRectangle },
    { SketchSubject::Rectangle,      PlacementMethod::Centred,    2u, DeclareCentredRectangle },
    { SketchSubject::Rectangle,      PlacementMethod::ThreePoint, 3u, DeclareThreePointRectangle },

    { SketchSubject::Polygon,        PlacementMethod::Centred,    2u, DeclarePolygon },
};

const CommitRow* ResolveCommitRow(SketchSubject Subject, PlacementMethod Method)
{
    for (const CommitRow& Row : CommitTable)
        if (Row.Subject == Subject && Row.Method == Method)
            return &Row;
    return nullptr;
}

}   // namespace

void SealConstraintRecord(WorkspaceNameIndex& Naming,
                          WorkspaceRecordStructure& Records,
                          PlacementJournal& Revisions,
                          SketchStructure& Sketch,
                          const ConstraintSpecification& Constraint,
                          std::vector<WorkspaceRecordName>& Written)
{
    if (!Constraint.Declared())
        return;
    const ConstraintName Named = Sketch.DeclareConstraint(Constraint);
    Discard(ApplyConstraint(Sketch, Named));
    const WorkspaceRecordName Record = DeclareWorkspaceConstraint(Naming, Records, Named);
    Written.push_back(Record);
    Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming), "Create Constraint", { Record },
                   Revisions.DeclaredCount() + 1u);
}

bool ArcReady(const SpatialPoint& StartPoint,
                   const SpatialPoint& ThroughPoint,
                   const SpatialPoint& EndPoint)
{
    const SpatialDirection First = Difference(StartPoint, ThroughPoint);
    const SpatialDirection Second = Difference(StartPoint, EndPoint);
    return LengthSquared(First) > 1.0e-8
        && LengthSquared(Second) > 1.0e-8
        && LengthSquared(Cross(First, Second)) > 1.0e-8;
}

bool CommitSupported(SketchSubject Subject, PlacementMethod Method)
{
    return ResolveCommitRow(Subject, Method) != nullptr;
}

namespace
{

Deliver<WorkspaceRecordName> CommitPlacementAtPlane(WorkspaceNameIndex& Naming,
                                                    SketchStructure& Sketch,
                                                    const SketchPlane& Plane,
                                                    WorkspaceRecordStructure& Records,
                                                    WorkspaceRevisionSequence& Revisions,
                                                    const SealedPlacement& Placed)
{
    const CommitRow* Row = ResolveCommitRow(Placed.Subject, Placed.Method);
    if (Row == nullptr)
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                      "that shape cannot be placed that way" });

    const std::size_t Standing = Placed.Subject == SketchSubject::Dimension ? Placed.Placements.size()
                                                                            : Placed.Anchors.size();
    if (Standing < Row->Required)
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                      "the placement has too few anchors" });

    PlacementJournal Journal(Revisions);
    const Deliver<WorkspaceRecordName> Made = Row->Declare(Naming, Sketch, Plane, Records, Journal, Placed);
    if (!Made.Resolved)
        return Made;

    Journal.Close();
    return Made;
}

} // namespace

Deliver<WorkspaceRecordName> CommitPlacement(WorkspaceNameIndex& Naming,
                                             SketchStructure& Sketch,
                                             WorkspaceRecordStructure& Records,
                                             WorkspaceRevisionSequence& Revisions,
                                             const SealedPlacement& Placed)
{
    if (!Sketch.PlaneDeclared())
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                      "the compatibility sketch has no authoring plane" });
    return CommitPlacementAtPlane(Naming, Sketch, Sketch.HeldPlane(), Records, Revisions, Placed);
}

Deliver<WorkspaceRecordName> CommitPlacement(WorkspaceNameIndex& Naming,
                                             SketchStructure& Sketch,
                                             const SketchPlane& ActivePlane,
                                             WorkspaceRecordStructure& Records,
                                             WorkspaceRevisionSequence& Revisions,
                                             const SealedPlacement& Placed)
{
    if (!ActivePlane.Declared())
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                      "the active authoring plane is not declared" });
    return CommitPlacementAtPlane(Naming, Sketch, ActivePlane, Records, Revisions, Placed);
}

Deliver<WorkspaceRecordName> CommitConstraint(WorkspaceNameIndex& Naming,
                                              SketchStructure& Sketch,
                                              WorkspaceRecordStructure& Records,
                                              WorkspaceRevisionSequence& Revisions,
                                              const ConstraintSpecification& Constraint)
{
    if (!Constraint.Declared())
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                      "the constraint names nothing to relate" });

    std::vector<WorkspaceRecordName> Written;
    {
        PlacementJournal Journal(Revisions);
        SealConstraintRecord(Naming, Records, Journal, Sketch, Constraint, Written);
        Journal.Close();
    }

    if (Written.empty())
        return Deliver<WorkspaceRecordName>::Refuse({ RefusalReason::ContentUnsupported,
                                                      "the constraint wrote no record" });

    return Deliver<WorkspaceRecordName>::Result(Written.front());
}

}   // namespace Slate
