//============================================================================================================================================
//                                                   WORLDSKETCHSKETCHBRIDGE.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorldSketchBridge/Api/WorldSketchBridge.h"

#include "Shared/WorkspaceCadNearClip.slang.h"
#include "SlateShape/Sketch/SketchSelection/Api/SketchSelection.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"
#include "SlateShape/World/WorldSketchDimensionSolver/Api/WorldSketchDimensionSolver.h"
#include "SlateWorkspace/Discipline/WorldSketchDimensionAuthoring/Api/WorldSketchDimensionAuthoring.h"
#include "SlateWorkspace/Discipline/PlacementCommit/Api/PlacementCommit.h"
#include "SlateWorkspace/Discipline/RecordDeclaration/Api/RecordDeclaration.h"
#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Slate
{

namespace
{

WorkspaceShapeFamily FamilyOfCurve(const CurveSpecification& Curve)
{
    switch (Curve.Subject())
    {
        case CurveSubject::Line:          return WorkspaceShapeFamily::Line;
        case CurveSubject::CircularArc:   return WorkspaceShapeFamily::CircularArc;
        case CurveSubject::Circle:        return WorkspaceShapeFamily::Circle;
        case CurveSubject::EllipticalArc:
        case CurveSubject::Ellipse:       return WorkspaceShapeFamily::Ellipse;
        case CurveSubject::Bezier:        return WorkspaceShapeFamily::Bezier;
        case CurveSubject::BasisSpline:   return WorkspaceShapeFamily::BasisSpline;
        case CurveSubject::RationalSpline:return WorkspaceShapeFamily::Nurbs;
        case CurveSubject::Hermite:       return WorkspaceShapeFamily::Hermite;
        case CurveSubject::SubjectCount:  return WorkspaceShapeFamily::Unknown;
    }
    return WorkspaceShapeFamily::Unknown;
}

WorldPlacementFrame ResolveSketchSupportFrame(const SketchStructure& Sketch)
{
    if (!Sketch.PlaneDeclared())
        return {};

    const SketchPlane& Plane = Sketch.HeldPlane();
    return { Plane.Origin, Plane.Normal, Plane.AlongDirection };
}

bool WorkplaneDeclared(const Workplane& ActiveWorkplane)
{
    return LengthSquared(ActiveWorkplane.Along) > 1.0e-18
        && LengthSquared(ActiveWorkplane.Normal) > 1.0e-18
        && LengthSquared(Cross(Normalize(ActiveWorkplane.Normal), Normalize(ActiveWorkplane.Along))) > 1.0e-18;
}

WorldPlacementFrame ResolveWorkplaneSupportFrame(const Workplane& ActiveWorkplane)
{
    if (!WorkplaneDeclared(ActiveWorkplane))
        return {};

    const SpatialBasis Basis = ResolveWorkplaneBasis(ActiveWorkplane);
    return { Basis.Origin, Basis.Normal, Basis.Along };
}

WorldPlacementFrame ResolveProfileSupportFrame(const ProfileSpecification& Profile)
{
    const ProfilePlane& Plane = Profile.HeldPlane();
    return { Plane.Origin, Plane.Normal, Plane.AlongDirection };
}

bool SketchHasCommittedGeometry(const SketchStructure& Sketch)
{
    return !Sketch.Curves().empty() || !Sketch.Profiles().empty();
}

bool ResolveSketchControlPosition(const SketchStructure& Sketch,
                                  SketchControlName Subject,
                                  SpatialPoint& Position)
{
    if (!Subject.Assigned())
        return false;

    const std::uint32_t CurveIndex = Subject.IssuedIndex >> 12u;
    if (CurveIndex == 0u)
        return false;

    std::vector<SketchControlPlacement> Controls;
    if (!ResolveSketchControls(Sketch, { CurveIndex }, Controls))
        return false;

    for (const SketchControlPlacement& Control : Controls)
        if (Control.Name.IssuedIndex == Subject.IssuedIndex)
        {
            Position = Control.Position;
            return true;
        }

    return false;
}

bool ResolveWorldControlPosition(const WorldSketchStructure& Declared,
                                 WorldControlName Subject,
                                 SpatialPoint& Position)
{
    if (!Subject.Assigned())
        return false;

    const std::uint32_t CurveIndex = Subject.IssuedIndex >> 12u;
    if (CurveIndex == 0u)
        return false;

    std::vector<WorldControlPlacement> Controls;
    if (!ResolveWorldSketchControls(Declared, { CurveIndex }, Controls))
        return false;

    for (const WorldControlPlacement& Control : Controls)
        if (Control.Name.IssuedIndex == Subject.IssuedIndex)
        {
            Position = Control.Position;
            return true;
        }

    return false;
}

WorkspaceCadProjectedPoint ResolveProjectedPoint(const ResolvedCamera& Camera,
                                                 const PlaneExtent& Extent,
                                                 const SpatialPoint& Position)
{
    WorkspaceCadProjectedPoint Point = {};

    if (!Camera.Perspective)
    {
        const SpatialDirection Offset = Difference(Camera.Frame.Eye, Position);
        const double CentreX = Extent.MinimumX + Extent.Width() * 0.5;
        const double CentreY = Extent.MinimumY + Extent.Height() * 0.5;
        Point.X = static_cast<Real32>(CentreX + Dot(Offset, Camera.Frame.Right) * Camera.OrthoScale);
        Point.Y = static_cast<Real32>(CentreY - Dot(Offset, Camera.Frame.Up) * Camera.OrthoScale);
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

bool PlacementClosesOnItself(const std::vector<SpatialPoint>& Anchors)
{
    if (Anchors.size() < 3u)
        return false;

    double Longest = 0.0;
    for (std::size_t Index = 0u; Index + 1u < Anchors.size(); ++Index)
        Longest = std::max(Longest, LengthSquared(Difference(Anchors[Index], Anchors[Index + 1u])));

    if (Longest <= 0.0)
        return false;

    const double Tolerance = std::sqrt(Longest) * 0.01;
    return LengthSquared(Difference(Anchors.front(), Anchors.back())) <= Tolerance * Tolerance;
}

bool PlacementFormsProfile(const SealedPlacement& Placed)
{
    if (Placed.Construction || !Placed.ClosedProfile)
        return false;

    const PlacementDeclaration Declared = DeclaredPlacement(Placed.Subject, Placed.Method);
    return Declared.ClosedProfile || (Placed.Subject == SketchSubject::Polyline && PlacementClosesOnItself(Placed.Anchors));
}

bool ResolveWorldBackedPlacementCurves(const SealedPlacement& Placed,
                                       const SpatialBasis& Basis,
                                       std::vector<CurveSpecification>& Delivered)
{
    Delivered.clear();
    if (Placed.Subject == SketchSubject::None || Placed.Subject == SketchSubject::Point ||
        Placed.Subject == SketchSubject::Dimension || Placed.Anchors.size() < 2u)
        return false;

    std::vector<SpatialPoint> Anchors = Placed.Anchors;
    const SpatialPoint Final = Anchors.back();
    Anchors.pop_back();
    ResolvePlacementCurves(Basis, Placed.Subject, Anchors, Final, Delivered,
                           std::clamp(Placed.Resolution, PolygonSideMinimum, PolygonSideMaximum));
    return !Delivered.empty();
}

std::string PlacementCreateOperation(const SealedPlacement& Placed,
                                     bool Profile)
{
    const char* Naming = DeclaredPlacement(Placed.Subject, Placed.Method).Naming;
    const std::string Base = (Naming != nullptr && Naming[0] != '\0') ? Naming : "Shape";
    if (Placed.Construction)
        return std::string("Create Construction ") + Base;
    if (Profile)
        return std::string("Create ") + Base + " Profile";
    return std::string("Create ") + Base;
}

WorldPointName ResolveWorldPointForSketchPoint(const WorldSketchMapping& Mapping,
                                               SketchPointName Point)
{
    if (!Point.Assigned())
        return {};
    const WorldCurveName WorldCurve =
        ResolveWorldCurveForSketchCurve(Mapping, { Point.IssuedIndex >> 8u });
    return { (WorldCurve.IssuedIndex << 8u) | (Point.IssuedIndex & 0xFFu) };
}

WorldControlName ResolveWorldControlForSketchControl(const WorldSketchMapping& Mapping,
                                                     SketchControlName Control)
{
    if (!Control.Assigned())
        return {};
    const WorldCurveName WorldCurve =
        ResolveWorldCurveForSketchCurve(Mapping, { Control.IssuedIndex >> 12u });
    return { (WorldCurve.IssuedIndex << 12u) | (Control.IssuedIndex & 0xFFFu) };
}

SketchPointName ResolveSketchPointForWorldPoint(const WorldSketchMapping& Mapping,
                                               WorldPointName Point)
{
    if (!Point.Assigned())
        return {};
    const SketchCurveName SketchCurve =
        ResolveSketchCurveForWorldCurve(Mapping, { Point.IssuedIndex >> 8u });
    return { (SketchCurve.IssuedIndex << 8u) | (Point.IssuedIndex & 0xFFu) };
}

SketchControlName ResolveSketchControlForWorldControl(const WorldSketchMapping& Mapping,
                                                      WorldControlName Control)
{
    if (!Control.Assigned())
        return {};
    const SketchCurveName SketchCurve =
        ResolveSketchCurveForWorldCurve(Mapping, { Control.IssuedIndex >> 12u });
    return { (SketchCurve.IssuedIndex << 12u) | (Control.IssuedIndex & 0xFFFu) };
}

WorldConstraintReference ResolveWorldConstraintReference(const ReferenceSpecification& Reference,
                                                         const WorldSketchMapping& Mapping)
{
    WorldConstraintReference Delivered = {};
    if (Reference.Subject == ReferenceSubject::SketchCurve && Reference.SketchCurve.Assigned())
    {
        Delivered.Subject = WorldConstraintReferenceSubject::Curve;
        Delivered.Curve = ResolveWorldCurveForSketchCurve(Mapping, Reference.SketchCurve);
    }
    else if (Reference.Subject == ReferenceSubject::SketchPoint && Reference.SketchPoint.Assigned())
    {
        Delivered.Subject = WorldConstraintReferenceSubject::Point;
        Delivered.Point = ResolveWorldPointForSketchPoint(Mapping, Reference.SketchPoint).IssuedIndex;
    }
    return Delivered;
}

ReferenceSpecification ResolveSketchConstraintReference(const WorldConstraintReference& Reference,
                                                        const WorldSketchMapping& Mapping)
{
    ReferenceSpecification Delivered = {};
    if (Reference.Subject == WorldConstraintReferenceSubject::Curve && Reference.Curve.Assigned())
    {
        Delivered.Subject = ReferenceSubject::SketchCurve;
        Delivered.SketchCurve = ResolveSketchCurveForWorldCurve(Mapping, Reference.Curve);
    }
    else if (Reference.Subject == WorldConstraintReferenceSubject::Point && Reference.Point != 0u)
    {
        Delivered.Subject = ReferenceSubject::SketchPoint;
        Delivered.SketchPoint = ResolveSketchPointForWorldPoint(Mapping, { Reference.Point });
    }
    return Delivered;
}

WorldDimensionReference ResolveWorldDimensionReference(const ReferenceSpecification& Reference,
                                                        const WorldSketchMapping& Mapping)
{
    WorldDimensionReference Delivered = {};
    if (Reference.Subject == ReferenceSubject::SketchCurve && Reference.SketchCurve.Assigned())
    {
        Delivered.Subject = WorldDimensionReferenceSubject::Curve;
        Delivered.Curve = ResolveWorldCurveForSketchCurve(Mapping, Reference.SketchCurve);
    }
    else if (Reference.Subject == ReferenceSubject::SketchPoint && Reference.SketchPoint.Assigned())
    {
        Delivered.Subject = WorldDimensionReferenceSubject::Point;
        Delivered.Point = ResolveWorldPointForSketchPoint(Mapping, Reference.SketchPoint).IssuedIndex;
    }
    else if (Reference.Subject == ReferenceSubject::SketchControl && Reference.SketchControl.Assigned())
    {
        Delivered.Subject = WorldDimensionReferenceSubject::Control;
        const WorldControlName Control = ResolveWorldControlForSketchControl(Mapping, Reference.SketchControl);
        Delivered.Control = Control.IssuedIndex;
    }
    return Delivered;
}

ReferenceSpecification ResolveSketchDimensionReference(const WorldDimensionReference& Reference,
                                                       const WorldSketchMapping& Mapping)
{
    ReferenceSpecification Delivered = {};
    if (Reference.Subject == WorldDimensionReferenceSubject::Curve && Reference.Curve.Assigned())
    {
        Delivered.Subject = ReferenceSubject::SketchCurve;
        Delivered.SketchCurve = ResolveSketchCurveForWorldCurve(Mapping, Reference.Curve);
    }
    else if (Reference.Subject == WorldDimensionReferenceSubject::Point && Reference.Point != 0u)
    {
        Delivered.Subject = ReferenceSubject::SketchPoint;
        Delivered.SketchPoint = ResolveSketchPointForWorldPoint(Mapping, { Reference.Point });
    }
    else if (Reference.Subject == WorldDimensionReferenceSubject::Control && Reference.Control != 0u)
    {
        Delivered.Subject = ReferenceSubject::SketchControl;
        Delivered.SketchControl = ResolveSketchControlForWorldControl(Mapping, { Reference.Control });
    }
    return Delivered;
}

} // namespace

WorldCurveName ResolveWorldCurveForSketchCurve(const WorldSketchMapping& Mapping,
                                               SketchCurveName Curve)
{
    if (!Curve.Assigned())
        return {};
    for (const WorldSketchCurveReference& Reference : Mapping.Curves)
        if (Reference.Sketch.IssuedIndex == Curve.IssuedIndex)
            return Reference.World;
    return { Curve.IssuedIndex };
}

SketchCurveName ResolveSketchCurveForWorldCurve(const WorldSketchMapping& Mapping,
                                                WorldCurveName Curve)
{
    if (!Curve.Assigned())
        return {};
    for (const WorldSketchCurveReference& Reference : Mapping.Curves)
        if (Reference.World.IssuedIndex == Curve.IssuedIndex)
            return Reference.Sketch;
    return { Curve.IssuedIndex };
}

WorldConstraintName ResolveWorldConstraintForSketchConstraint(const WorldSketchMapping& Mapping,
                                                              ConstraintName Constraint)
{
    if (!Constraint.Assigned())
        return {};
    for (const WorldSketchConstraintReference& Reference : Mapping.Constraints)
        if (Reference.Sketch.IssuedIndex == Constraint.IssuedIndex)
            return Reference.World;
    return { Constraint.IssuedIndex };
}

ConstraintName ResolveSketchConstraintForWorldConstraint(const WorldSketchMapping& Mapping,
                                                         WorldConstraintName Constraint)
{
    if (!Constraint.Assigned())
        return {};
    for (const WorldSketchConstraintReference& Reference : Mapping.Constraints)
        if (Reference.World.IssuedIndex == Constraint.IssuedIndex)
            return Reference.Sketch;
    return { Constraint.IssuedIndex };
}

WorldDimensionName ResolveWorldDimensionForSketchDimension(const WorldSketchMapping& Mapping,
                                                           DimensionName Dimension)
{
    if (!Dimension.Assigned())
        return {};
    for (const WorldSketchDimensionReference& Reference : Mapping.Dimensions)
        if (Reference.Sketch.IssuedIndex == Dimension.IssuedIndex)
            return Reference.World;
    return { Dimension.IssuedIndex };
}

DimensionName ResolveSketchDimensionForWorldDimension(const WorldSketchMapping& Mapping,
                                                      WorldDimensionName Dimension)
{
    if (!Dimension.Assigned())
        return {};
    for (const WorldSketchDimensionReference& Reference : Mapping.Dimensions)
        if (Reference.World.IssuedIndex == Dimension.IssuedIndex)
            return Reference.Sketch;
    return { Dimension.IssuedIndex };
}

bool ResolveWorldDimensionReferenceForSketchSnap(const WorldSketchMapping& Mapping,
                                                 const SketchSnapPlacement& Snap,
                                                 WorldDimensionReference& Reference)
{
    Reference = {};
    if (Snap.SketchPoint.Assigned())
    {
        Reference.Subject = WorldDimensionReferenceSubject::Point;
        Reference.Point = ResolveWorldPointForSketchPoint(Mapping, Snap.SketchPoint).IssuedIndex;
    }
    else if (Snap.SketchControl.Assigned())
    {
        Reference.Subject = WorldDimensionReferenceSubject::Control;
        Reference.Control = ResolveWorldControlForSketchControl(Mapping, Snap.SketchControl).IssuedIndex;
    }
    else if (Snap.SourceCurve.Assigned())
    {
        Reference.Subject = WorldDimensionReferenceSubject::Curve;
        Reference.Curve = ResolveWorldCurveForSketchCurve(Mapping, Snap.SourceCurve);
    }
    return Reference.Declared();
}

SketchSnapPlacement ResolveCompatibilitySnap(const WorldSnapPlacement& Snapped,
                                             const WorldSketchMapping& Mapping)
{
    if (!Snapped.Resolved())
        return {};

    const SketchCurveName SketchCurve = ResolveSketchCurveForWorldCurve(Mapping, Snapped.SourceCurve);
    SketchPointName SketchPoint = {};
    if (Snapped.WorldPoint.Assigned())
        SketchPoint = { (SketchCurve.IssuedIndex << 8u) | (Snapped.WorldPoint.IssuedIndex & 0xFFu) };

    SketchControlName SketchControl = {};
    if (Snapped.WorldControl.Assigned())
        SketchControl = { (SketchCurve.IssuedIndex << 12u) | (Snapped.WorldControl.IssuedIndex & 0xFFFu) };

    return { static_cast<SketchSnapSubject>(static_cast<std::uint32_t>(Snapped.Subject)),
             SketchCurve, SketchPoint, SketchControl, Snapped.Position, Snapped.Distance };
}

bool MirrorSketchIntoWorldSketch(const SketchStructure& Sketch,
                                WorldSketchStructure& Declared,
                                WorldSketchMapping& Mapping)
{
    Declared.Reclaim();
    Mapping.Curves.clear();
    Mapping.Loops.clear();
    Mapping.Constraints.clear();
    Mapping.Dimensions.clear();

    const WorldPlacementFrame SketchSupport = ResolveSketchSupportFrame(Sketch);
    std::vector<WorldPlacementFrame> CurveSupports(Sketch.Curves().size(), SketchSupport);

    for (std::uint32_t ProfileIndex = 0u; ProfileIndex < Sketch.Profiles().size(); ++ProfileIndex)
    {
        const ProfileSpecification& Profile = Sketch.Profiles()[ProfileIndex];
        const WorldPlacementFrame ProfileSupport = ResolveProfileSupportFrame(Profile);
        if (!ProfileSupport.Declared())
            continue;

        for (const ProfileLoop& Loop : Profile.HeldLoops())
            for (const ProfileCurveUse& Use : Loop.Traversal)
                if (Use.TraversedCurve.IssuedIndex > 0u
                 && Use.TraversedCurve.IssuedIndex <= CurveSupports.size())
                    CurveSupports[Use.TraversedCurve.IssuedIndex - 1u] = ProfileSupport;
    }

    for (std::size_t CurveIndex = 0u; CurveIndex < Sketch.Curves().size(); ++CurveIndex)
    {
        const DeclaredSketchCurve& Curve = Sketch.Curves()[CurveIndex];
        const WorldPlacementFrame& Support = CurveSupports[CurveIndex];
        const WorldCurveName WorldCurve = Support.Declared()
                                        ? Declared.DeclareCurve(Curve.Geometry, Support)
                                        : Declared.DeclareCurve(Curve.Geometry);
        Mapping.Curves.push_back({ WorldCurve, { static_cast<std::uint32_t>(CurveIndex + 1u) } });
    }

    for (std::uint32_t ConstraintIndex = 1u;
         ConstraintIndex <= Sketch.Constraints().size(); ++ConstraintIndex)
    {
        const ConstraintSpecification& Source = Sketch.Constraints()[ConstraintIndex - 1u];
        WorldConstraintSpecification Mirrored = {};
        Mirrored.Subject = static_cast<WorldConstraintSubject>(static_cast<std::uint32_t>(Source.Subject));
        Mirrored.Primary = ResolveWorldConstraintReference(Source.Primary, Mapping);
        Mirrored.Secondary = ResolveWorldConstraintReference(Source.Secondary, Mapping);
        const WorldConstraintName WorldConstraint = Declared.DeclareConstraint(Mirrored);
        if (!WorldConstraint.Assigned())
            return false;
        Mapping.Constraints.push_back({ WorldConstraint, { ConstraintIndex } });
    }

    for (std::uint32_t ProfileIndex = 0u; ProfileIndex < Sketch.Profiles().size(); ++ProfileIndex)
    {
        const ProfileSpecification& Profile = Sketch.Profiles()[ProfileIndex];
        for (std::uint32_t LoopIndex = 0u; LoopIndex < Profile.HeldLoops().size(); ++LoopIndex)
        {
            const ProfileLoop& Loop = Profile.HeldLoops()[LoopIndex];
            DeclaredWorldLoop Mirrored = {};
            Mirrored.Traversal.reserve(Loop.Traversal.size());
            for (const ProfileCurveUse& Use : Loop.Traversal)
                Mirrored.Traversal.push_back({ { Use.TraversedCurve.IssuedIndex }, Use.SameSense });
            const WorldLoopName WorldLoop = Declared.DeclareLoop(Mirrored);
            Mapping.Loops.push_back({ WorldLoop, { ProfileIndex + 1u }, LoopIndex });
        }
    }

    for (std::uint32_t DimensionIndex = 1u;
         DimensionIndex <= Sketch.Dimensions().size(); ++DimensionIndex)
    {
        const DimensionSpecification& Source = Sketch.Dimensions()[DimensionIndex - 1u];
        WorldDimensionSpecification Mirrored = {};
        Mirrored.Subject = static_cast<WorldDimensionSubject>(static_cast<std::uint32_t>(Source.Subject));
        Mirrored.Target = Source.Target;
        Mirrored.Primary = ResolveWorldDimensionReference(Source.Primary, Mapping);
        Mirrored.Secondary = ResolveWorldDimensionReference(Source.Secondary, Mapping);

        // A legacy profile radius dimension names the profile rather than its curve. The world model
        // has no profile-owned dimension reference, so resolve the first curve in that mapped loop.
        if (Source.Primary.Subject == ReferenceSubject::Profile && Source.Primary.Profile.Assigned())
        {
            for (const WorldSketchLoopReference& Loop : Mapping.Loops)
                if (Loop.Profile.IssuedIndex == Source.Primary.Profile.IssuedIndex)
                {
                    const ProfileSpecification& Profile = Sketch.Profiles()[Loop.Profile.IssuedIndex - 1u];
                    const ProfileLoop& ProfileLoop = Profile.HeldLoops()[Loop.ProfileLoopIndex];
                    if (!ProfileLoop.Traversal.empty())
                    {
                        Mirrored.Primary.Subject = WorldDimensionReferenceSubject::Curve;
                        Mirrored.Primary.Curve = ResolveWorldCurveForSketchCurve(
                            Mapping, SketchCurveName{ ProfileLoop.Traversal.front().TraversedCurve.IssuedIndex });
                    }
                    break;
                }
        }

        const WorldDimensionName WorldDimension = Declared.DeclareDimension(Mirrored);
        if (!WorldDimension.Assigned())
            return false;
        Mapping.Dimensions.push_back({ WorldDimension, { DimensionIndex } });
    }

    return true;
}

bool MirrorWorldConstraintIntoSketch(const WorldSketchStructure& Declared,
                                     const WorldSketchMapping& Mapping,
                                     WorldConstraintName WorldConstraint,
                                     SketchStructure& Sketch,
                                     ConstraintName& SketchConstraint)
{
    SketchConstraint = {};
    const WorldConstraintSpecification* Source = Declared.Resolve(WorldConstraint);
    if (Source == nullptr || !Source->Declared())
        return false;

    ConstraintSpecification Mirrored = {};
    Mirrored.Subject = static_cast<ConstraintSubject>(static_cast<std::uint32_t>(Source->Subject));
    Mirrored.Primary = ResolveSketchConstraintReference(Source->Primary, Mapping);
    Mirrored.Secondary = ResolveSketchConstraintReference(Source->Secondary, Mapping);
    if (!Mirrored.Declared())
        return false;

    SketchConstraint = Sketch.DeclareConstraint(Mirrored);
    return SketchConstraint.Assigned();
}

bool MirrorWorldDimensionIntoSketch(const WorldSketchStructure& Declared,
                                    const WorldSketchMapping& Mapping,
                                    WorldDimensionName WorldDimension,
                                    SketchStructure& Sketch,
                                    DimensionName& SketchDimension)
{
    SketchDimension = {};
    const WorldDimensionSpecification* Source = Declared.Resolve(WorldDimension);
    if (Source == nullptr || !Source->Declared())
        return false;

    DimensionSpecification Mirrored = {};
    Mirrored.Subject = static_cast<DimensionSubject>(static_cast<std::uint32_t>(Source->Subject));
    Mirrored.Primary = ResolveSketchDimensionReference(Source->Primary, Mapping);
    Mirrored.Secondary = ResolveSketchDimensionReference(Source->Secondary, Mapping);
    Mirrored.Target = Source->Target;
    if (!Mirrored.Declared())
        return false;

    SketchDimension = Sketch.DeclareDimension(Mirrored);
    return SketchDimension.Assigned();
}

bool ApplyWorldSketchToSketch(const WorldSketchStructure& Declared,
                             SketchStructure& Sketch)
{
    if (Declared.CurveCount() != static_cast<std::uint32_t>(Sketch.Curves().size()))
        return false;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        const DeclaredWorldCurve* Source = Declared.Resolve(WorldCurveName{ CurveIndex });
        if (Source == nullptr)
            return false;
        Sketch.Curves()[CurveIndex - 1u].Geometry = Source->Geometry;
        // 🔴 A RETIRED EDGE TRAVELS AS RETIRED. Copying only the geometry would hand the compatibility
        //    sketch an empty curve with no word that its emptiness is intended, and `Declared()` would
        //    fail the whole sketch over it. The flag rides along so both models agree the edge is gone.
        Sketch.Curves()[CurveIndex - 1u].Retired = Source->Retired;
    }

    return true;
}

bool ApplyWorldSketchToSketch(const WorldSketchStructure& Declared,
                             const WorldSketchMapping& Mapping,
                             SketchStructure& Sketch)
{
    if (Mapping.Curves.empty())
        return ApplyWorldSketchToSketch(Declared, Sketch);

    for (const WorldSketchCurveReference& Reference : Mapping.Curves)
    {
        const DeclaredWorldCurve* Source = Declared.Resolve(Reference.World);
        if (Source == nullptr || !Reference.Sketch.Assigned()
         || Reference.Sketch.IssuedIndex > Sketch.Curves().size())
            return false;
        Sketch.Curves()[Reference.Sketch.IssuedIndex - 1u].Geometry = Source->Geometry;
        // 🔴 A RETIRED EDGE TRAVELS AS RETIRED, so the compatibility sketch reads its empty geometry as a
        //    removed curve rather than a broken one -- see the mapping-free writeback above.
        Sketch.Curves()[Reference.Sketch.IssuedIndex - 1u].Retired = Source->Retired;
    }

    return true;
}

bool AdoptWorldSketchCurvesIntoSketch(const WorldSketchStructure& Declared,
                                     WorldSketchMapping& Mapping,
                                     SketchStructure& Sketch)
{
    // 🔴 AN OPERATION DECLARES INTO THE WORLD MODEL ALONE, AND THE MIRROR NEVER HEARS ABOUT IT.
    //    `ApplyWorldSketchToSketch` walks the MAPPING, so it can only ever refresh curves that were
    //    already paired. A fillet arc, a chamfer chord or a cut's second half exists solely in the world
    //    model, has no mapping entry, and is therefore invisible to the compatibility sketch -- which is
    //    what picking, the outliner and the selection gizmo all read. That is why a new fillet could be
    //    seen but never selected: it was drawn from the world model and picked from the sketch, and the
    //    two disagreed about how many curves existed.
    // 📝 Pairing only. Geometry is copied by the writeback above; this closes the gap in the mapping so
    //    that writeback has something to walk.
    bool Adopted = false;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        const WorldCurveName World = { CurveIndex };

        bool Paired = false;
        for (const WorldSketchCurveReference& Reference : Mapping.Curves)
            if (Reference.World.IssuedIndex == CurveIndex)
            {
                Paired = true;
                break;
            }
        if (Paired)
            continue;

        const DeclaredWorldCurve* Source = Declared.Resolve(World);
        if (Source == nullptr)
            continue;

        const SketchCurveName Mirrored = Sketch.DeclareCurve(Source->Geometry);
        if (!Mirrored.Assigned())
            continue;

        Mapping.Curves.push_back({ World, Mirrored });
        Adopted = true;
    }

    return Adopted;
}

WorkspaceRecordName ResolveRecordForWorldLoop(const WorkspaceRecordStructure& Records,
                                              const WorldSketchMapping& Mapping,
                                              WorldLoopName Loop)
{
    if (!Loop.Assigned())
        return {};

    const WorldSketchLoopReference* Matched = nullptr;
    for (const WorldSketchLoopReference& Reference : Mapping.Loops)
        if (Reference.World.IssuedIndex == Loop.IssuedIndex)
        {
            Matched = &Reference;
            break;
        }
    if (Matched == nullptr && Loop.IssuedIndex <= Mapping.Loops.size())
        Matched = &Mapping.Loops[Loop.IssuedIndex - 1u];
    if (Matched == nullptr)
        return {};

    const ProfileNameInFeature Profile = Matched->Profile;
    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record != nullptr && Record->Profile.IssuedIndex == Profile.IssuedIndex)
            return { Index };
    }

    return {};
}

bool ResolveWorldPickForSketchPick(const SketchStructure& Sketch,
                                   const WorkspaceRecordStructure& Records,
                                   const WorldSketchStructure& Declared,
                                   const WorldSketchMapping& Mapping,
                                   const SketchPick& Selection,
                                   WorldPick& Resolved)
{
    static_cast<void>(Sketch);
    Resolved = {};
    if (!Selection.Standing())
        return false;

    switch (Selection.Subject)
    {
        case SketchPickSubject::Point:
            Resolved.Subject = WorldPickSubject::Point;
            Resolved.Point = ResolveWorldPointForSketchPoint(Mapping, Selection.Point);
            Resolved.Curve = ResolveWorldCurveForSketchCurve(Mapping,
                                                              { Selection.Point.IssuedIndex >> 8u });
            return ResolveWorldSketchPointPosition(Declared, Resolved.Point, Resolved.Position);

        case SketchPickSubject::Control:
            Resolved.Subject = WorldPickSubject::Control;
            Resolved.Control = ResolveWorldControlForSketchControl(Mapping, Selection.Control);
            Resolved.Curve = ResolveWorldCurveForSketchCurve(Mapping,
                                                              { Selection.Control.IssuedIndex >> 12u });
            return ResolveWorldControlPosition(Declared, Resolved.Control, Resolved.Position);

        case SketchPickSubject::Curve:
            Resolved.Subject = WorldPickSubject::Curve;
            Resolved.Curve = ResolveWorldCurveForSketchCurve(Mapping, Selection.Curve);
            return ResolveWorldCurvePivot(Declared, Resolved.Curve, Resolved.Position);

        case SketchPickSubject::Record:
        {
            const WorkspaceRecord* Record = Selection.Record.Assigned()
                                          ? Records.Resolve(Selection.Record)
                                          : nullptr;
            if (Record == nullptr)
                return false;

            if (Record->SketchPoint.Assigned())
            {
                Resolved.Subject = WorldPickSubject::Point;
                Resolved.Point = ResolveWorldPointForSketchPoint(Mapping, Record->SketchPoint);
                Resolved.Curve = ResolveWorldCurveForSketchCurve(Mapping,
                                                                  { Record->SketchPoint.IssuedIndex >> 8u });
                return ResolveWorldSketchPointPosition(Declared, Resolved.Point, Resolved.Position);
            }

            if (Record->SketchCurve.Assigned())
            {
                Resolved.Subject = WorldPickSubject::Curve;
                Resolved.Curve = ResolveWorldCurveForSketchCurve(Mapping, Record->SketchCurve);
                return ResolveWorldCurvePivot(Declared, Resolved.Curve, Resolved.Position);
            }

            if (Record->Profile.Assigned())
            {
                for (std::uint32_t LoopIndex = 1u; LoopIndex <= Mapping.Loops.size(); ++LoopIndex)
                    if (Mapping.Loops[LoopIndex - 1u].Profile.IssuedIndex == Record->Profile.IssuedIndex)
                    {
                        Resolved.Subject = WorldPickSubject::Loop;
                        Resolved.Loop = Mapping.Loops[LoopIndex - 1u].World.Assigned()
                                      ? Mapping.Loops[LoopIndex - 1u].World
                                      : WorldLoopName{ LoopIndex };
                        return ResolveWorldLoopPivot(Declared, Resolved.Loop, Resolved.Position);
                    }
            }
            return false;
        }

        case SketchPickSubject::None:
            return false;
    }

    return false;
}

bool ResolveSketchPickForWorldPick(const SketchStructure& Sketch,
                                   const WorkspaceRecordStructure& Records,
                                   const WorldSketchMapping& Mapping,
                                   const WorldPick& Selection,
                                   SketchPick& Resolved)
{
    Resolved = {};
    if (!Selection.Standing())
        return false;

    switch (Selection.Subject)
    {
        case WorldPickSubject::Point:
            Resolved.Subject = SketchPickSubject::Point;
            Resolved.Point = ResolveSketchPointForWorldPoint(Mapping, Selection.Point);
            Resolved.Curve = ResolveSketchCurveForWorldCurve(Mapping,
                                                              { Selection.Point.IssuedIndex >> 8u });
            Resolved.Record = ResolveRecordForPoint(Sketch, Records, Resolved.Point);
            if (!Resolved.Record.Assigned() && Resolved.Curve.Assigned())
                Resolved.Record = ResolveRecordForCurve(Sketch, Records, Resolved.Curve);
            return ResolveSketchPointPosition(Sketch, Resolved.Point, Resolved.Position);

        case WorldPickSubject::Control:
            Resolved.Subject = SketchPickSubject::Control;
            Resolved.Control = ResolveSketchControlForWorldControl(Mapping, Selection.Control);
            Resolved.Curve = ResolveSketchCurveForWorldCurve(Mapping,
                                                              { Selection.Control.IssuedIndex >> 12u });
            Resolved.Record = ResolveRecordForCurve(Sketch, Records, Resolved.Curve);
            return ResolveSketchControlPosition(Sketch, Resolved.Control, Resolved.Position);

        case WorldPickSubject::Curve:
            Resolved.Subject = SketchPickSubject::Curve;
            Resolved.Curve = ResolveSketchCurveForWorldCurve(Mapping, Selection.Curve);
            Resolved.Record = ResolveRecordForCurve(Sketch, Records, Resolved.Curve);
            return ResolveCurvePivot(Sketch, Resolved.Curve, Resolved.Position);

        case WorldPickSubject::Loop:
            Resolved.Subject = SketchPickSubject::Record;
            Resolved.Record = ResolveRecordForWorldLoop(Records, Mapping, Selection.Loop);
            if (!Resolved.Record.Assigned())
                return false;
            for (const WorldSketchLoopReference& Reference : Mapping.Loops)
                if (Reference.World.IssuedIndex == Selection.Loop.IssuedIndex)
                    return ResolveProfilePivot(Sketch, Reference.Profile, Resolved.Position);
            if (Selection.Loop.IssuedIndex <= Mapping.Loops.size())
                return ResolveProfilePivot(Sketch, Mapping.Loops[Selection.Loop.IssuedIndex - 1u].Profile,
                                           Resolved.Position);
            return false;

        case WorldPickSubject::None:
            return false;
    }

    return false;
}

Deliver<bool> ProjectWorldBackedSketchRendering(const SketchStructure& Sketch,
                                                const ResolvedCamera& Camera,
                                                const PlaneExtent& LogicalExtent,
                                                const DrawableScale& Drawable,
                                                WorkspaceCadPacket& Delivered,
                                                const WorldSelectionSet& Selection,
                                                const WorldSketchRenderingStyle& Style,
                                                double ClosureTolerance,
                                                double CoplanarTolerance)
{
    WorldSketchStructure World;
    WorldSketchMapping Mapping;
    MirrorSketchIntoWorldSketch(Sketch, World, Mapping);
    return ProjectWorldBackedSketchRendering(World, Camera, LogicalExtent, Drawable,
                                             Delivered, Selection, Style, ClosureTolerance, CoplanarTolerance);
}

Deliver<bool> ProjectWorldBackedSketchRendering(const WorldSketchStructure& Declared,
                                                const ResolvedCamera& Camera,
                                                const PlaneExtent& LogicalExtent,
                                                const DrawableScale& Drawable,
                                                WorkspaceCadPacket& Delivered,
                                                const WorldSelectionSet& Selection,
                                                const WorldSketchRenderingStyle& Style,
                                                double ClosureTolerance,
                                                double CoplanarTolerance)
{
    return ProjectWorldSketchRendering(Declared, Camera, Drawable.ToPhysical(LogicalExtent),
                                       Delivered, Selection, Style, ClosureTolerance, CoplanarTolerance);
}

bool ProjectWorldPlacementPreview(const ResolvedCamera& Camera,
                                  const PlaneExtent& LogicalExtent,
                                  const DrawableScale& Drawable,
                                  const std::vector<CurveSpecification>& Geometry,
                                  const std::vector<SpatialPoint>& Anchors,
                                  const SpatialPoint& Hover,
                                  WorkspaceCadPacket& Delivered,
                                  const SketchRenderingStyle& Style)
{
    const PlaneExtent PhysicalExtent = Drawable.ToPhysical(LogicalExtent);
    const double Distance = std::sqrt(Camera.Frame.Eye.Left * Camera.Frame.Eye.Left
                                    + Camera.Frame.Eye.Up * Camera.Frame.Eye.Up
                                    + Camera.Frame.Eye.Forward * Camera.Frame.Eye.Forward);
    const double DetailScale = Camera.Perspective
                             ? std::sqrt(12.0 / std::max(Distance, 0.25))
                             : std::sqrt(std::max(Camera.OrthoScale, 1.0) / 48.0);
    bool Appended = false;

    for (const CurveSpecification& Span : Geometry)
    {
        if (!Span.Declared())
            continue;

        std::vector<SpatialPoint> Polyline;
        AppendCurvePolyline(Span, Polyline,
                             ResolveCurveStepCountForDetail(Span, Style.CurveSteps, DetailScale));
        for (std::size_t Index = 0u; Index + 1u < Polyline.size(); ++Index)
        {
            AppendClippedSegment(Camera, PhysicalExtent,
                                 Polyline[Index], Polyline[Index + 1u],
                                 Style.PreviewCurveColour, Style.CurveThickness, Delivered);
            Appended = true;
        }
    }

    for (const SpatialPoint& Anchor : Anchors)
    {
        float X = 0.0f;
        float Y = 0.0f;
        if (ProjectFromCamera(Camera, PhysicalExtent, Anchor, X, Y))
        {
            Delivered.AddMarker(X, Y, Style.ControlColour, Style.ControlRadius,
                                WorkspaceCadMarkerSubject::SketchControl);
            Appended = true;
        }
    }

    float HoverX = 0.0f;
    float HoverY = 0.0f;
    if (ProjectFromCamera(Camera, PhysicalExtent, Hover, HoverX, HoverY))
    {
        Delivered.AddMarker(HoverX, HoverY, Style.PreviewCurveColour, Style.ControlRadius,
                            WorkspaceCadMarkerSubject::SketchControl);
        Appended = true;
    }

    return Appended;
}

bool CommitPlacementWorldBacked(const Workplane& ActiveWorkplane,
                                WorldSketchStructure& Declared,
                                WorldSketchMapping& Mapping,
                                WorkspaceNameIndex& Naming,
                                SketchStructure& Sketch,
                                WorkspaceRecordStructure& Records,
                                WorkspaceRevisionSequence& Revisions,
                                const SealedPlacement& Placed,
                                WorkspaceRecordName& SelectedRecord)
{
    // World placement is a single transaction across the live model and every compatibility projection.
    // This snapshot is deliberately taken before the legacy bootstrap import: even an import followed by
    // an invalid placement must leave world curves, mappings, records, names, revisions, and selection as
    // they were when the gesture began.
    const WorldSketchStructure WorldBefore = Declared;
    const WorldSketchMapping MappingBefore = Mapping;
    const WorkspaceNameIndex NamingBefore = Naming;
    const SketchStructure SketchBefore = Sketch;
    const WorkspaceRecordStructure RecordsBefore = Records;
    const WorkspaceRevisionSequence RevisionsBefore = Revisions;
    const auto Rollback = [&]()
    {
        Declared = WorldBefore;
        Mapping = MappingBefore;
        Naming = NamingBefore;
        Sketch = SketchBefore;
        Records = RecordsBefore;
        Revisions = RevisionsBefore;
        SelectedRecord = {};
        return false;
    };

    SelectedRecord = {};

    if (Placed.Subject == SketchSubject::None)
        return Rollback();

    if (Placed.Subject == SketchSubject::Dimension)
    {
        // A dimension is a semantic relationship, not a curve. Import a legacy document only at the
        // bootstrap boundary, then declare and solve the relationship in the live world structure.
        if (Declared.CurveCount() == 0u && SketchHasCommittedGeometry(Sketch)
         && !MirrorSketchIntoWorldSketch(Sketch, Declared, Mapping))
            return Rollback();

        if (Placed.Placements.size() < 2u || Placed.Anchors.size() < 2u)
            return Rollback();
        WorldDimensionReference Primary = {};
        WorldDimensionReference Secondary = {};
        if (!ResolveWorldDimensionReferenceForSketchSnap(Mapping, Placed.Placements[0], Primary)
         || !ResolveWorldDimensionReferenceForSketchSnap(Mapping, Placed.Placements[1], Secondary))
            return Rollback();

        const double Target = std::sqrt(LengthSquared(Difference(Placed.Anchors[0], Placed.Anchors[1])));
        const Deliver<WorldDimensionSpecification> Specification =
            DeclareWorldDimensionFrom(WorldDimensionSubject::Aligned, Primary, Secondary, Target);
        if (!Specification)
            return Rollback();
        const WorldDimensionName WorldDimension = Declared.DeclareDimension(Specification.Resolve());
        if (!WorldDimension.Assigned() || !ApplyWorldDimension(Declared, WorldDimension)
         || !ApplyWorldSketchToSketch(Declared, Mapping, Sketch))
            return Rollback();

        DimensionName SketchDimension = {};
        if (!MirrorWorldDimensionIntoSketch(Declared, Mapping, WorldDimension, Sketch, SketchDimension))
            return Rollback();
        Mapping.Dimensions.push_back({ WorldDimension, SketchDimension });
        const WorkspaceRecordName Record = DeclareWorkspaceDimension(Naming, Records, SketchDimension);
        if (!Record.Assigned() || Records.Resolve(Record) == nullptr)
            return Rollback();
        Revisions.Seal("Declared " + std::string(Records.Resolve(Record)->Naming),
                       "Create Dimension", { Record }, Revisions.DeclaredCount() + 1u);
        SelectedRecord = Record;
        return true;
    }

    // 🧩 World geometry is the live authoring authority. Import the compatibility sketch only when the
    //    world model has not been seeded yet; an already-live world model must never be rebuilt from a
    //    stale or partial sketch mirror before a new placement.
    if (Declared.CurveCount() == 0u && SketchHasCommittedGeometry(Sketch)
     && !MirrorSketchIntoWorldSketch(Sketch, Declared, Mapping))
        return Rollback();

    WorldPlacementFrame Support = ResolveWorkplaneSupportFrame(ActiveWorkplane);
    if (!Support.Declared())
        Support = ResolveSketchSupportFrame(Sketch);
    const bool SupportStanding = Support.Declared();

    std::vector<WorldCurveName> WorldCurves;
    if (Placed.Subject == SketchSubject::Point)
    {
        if (Placed.Anchors.empty())
            return Rollback();

        SpatialDirection Along = SupportStanding ? Normalize(Support.AlongDirection)
                                                 : SpatialDirection{ 1.0, 0.0, 0.0 };
        if (LengthSquared(Along) <= 1.0e-12)
            Along = { 1.0, 0.0, 0.0 };

        const SpatialPoint Tip = Added(Placed.Anchors.back(), Scaled(Along, 0.001));
        WorldCurves.push_back(SupportStanding
                            ? Declared.DeclareLine(Placed.Anchors.back(), Tip, Support)
                            : Declared.DeclareLine(Placed.Anchors.back(), Tip));
    }
    else
    {
        std::vector<CurveSpecification> Geometry;
        const SpatialBasis PlacementBasis = SupportStanding
            ? SpatialBasis{ Support.Origin,
                            Normalize(Support.AlongDirection),
                            Normalize(Cross(Normalize(Support.Normal), Normalize(Support.AlongDirection))),
                            Normalize(Support.Normal) }
            : SpatialBasis{};
        if (!ResolveWorldBackedPlacementCurves(Placed, PlacementBasis, Geometry))
            return Rollback();

        WorldCurves.reserve(Geometry.size());
        for (const CurveSpecification& Curve : Geometry)
            WorldCurves.push_back(SupportStanding
                                ? Declared.DeclareCurve(Curve, Support)
                                : Declared.DeclareCurve(Curve));
    }

    if (WorldCurves.empty())
        return Rollback();

    WorldLoopName WorldLoop = {};
    if (PlacementFormsProfile(Placed))
    {
        DeclaredWorldLoop Loop = {};
        for (const WorldCurveName& Curve : WorldCurves)
            Loop.Traversal.push_back({ Curve, true });
        WorldLoop = Declared.DeclareLoop(Loop);
        if (!WorldLoop.Assigned())
            return Rollback();
    }

    std::vector<SketchCurveName> SketchCurves;
    SketchCurves.reserve(WorldCurves.size());
    for (const WorldCurveName& Curve : WorldCurves)
    {
        const DeclaredWorldCurve* Resolved = Declared.Resolve(Curve);
        if (Resolved == nullptr || !Resolved->Geometry.Declared())
            return Rollback();
        const SketchCurveName SketchCurve = Sketch.DeclareCurve(Resolved->Geometry);
        SketchCurves.push_back(SketchCurve);
        Mapping.Curves.push_back({ Curve, SketchCurve });
    }

    const bool Profile = PlacementFormsProfile(Placed);
    std::vector<WorkspaceRecordName> Written;
    if (Placed.Subject == SketchSubject::Point)
    {
        std::vector<SketchPointPlacement> Points;
        if (!ResolveSketchPoints(Sketch, SketchCurves.front(), Points) || Points.empty())
            return Rollback();
        const WorkspaceRecordName Record = DeclareWorkspacePoint(Naming, Records, Points.front().Name);
        Written.push_back(Record);
        SelectedRecord = Record;
    }
    else if (Profile)
    {
        ProfileSpecification Shape = {};
        if (SupportStanding)
            Shape.DeclarePlane({ Support.Origin, Support.Normal, Support.AlongDirection });
        else if (Sketch.PlaneDeclared())
            Shape.DeclarePlane({ Sketch.HeldPlane().Origin, Sketch.HeldPlane().Normal, Sketch.HeldPlane().AlongDirection });

        ProfileLoop Loop = {};
        Loop.Orientation = ProfileLoopOrientation::Outer;
        for (const SketchCurveName& Curve : SketchCurves)
            Loop.Traversal.push_back({ { Curve.IssuedIndex }, true });
        Shape.DeclareLoop(Loop);

        const ProfileNameInFeature DeclaredProfile = Sketch.DeclareProfile(Shape);
        Mapping.Loops.push_back({ WorldLoop, DeclaredProfile, 0u });
        WorkspaceShapeFamily Family = WorkspaceShapeFamily::Profile;
        if (Placed.Subject == SketchSubject::Circle)
            Family = WorkspaceShapeFamily::Circle;
        else if (Placed.Subject == SketchSubject::Ellipse)
            Family = WorkspaceShapeFamily::Ellipse;
        else if (Placed.Subject == SketchSubject::Rectangle)
            Family = WorkspaceShapeFamily::Rectangle;
        else if (Placed.Subject == SketchSubject::Polygon)
            Family = WorkspaceShapeFamily::Polygon;
        else if (Placed.Subject == SketchSubject::Slot)
            Family = WorkspaceShapeFamily::Slot;
        const WorkspaceRecordName Record = DeclareWorkspaceProfile(Naming, Records, DeclaredProfile, Family);
        Written.push_back(Record);
        SelectedRecord = Record;
    }
    else
    {
        if (Placed.Subject == SketchSubject::Hermite && !SketchCurves.empty())
        {
            Written.push_back(DeclareWorkspaceCurve(Naming, Records, SketchCurves.front(), Placed.Construction, WorkspaceShapeFamily::Hermite));
        }
        else
        {
            for (const SketchCurveName& Curve : SketchCurves)
            {
                WorkspaceShapeFamily Family = WorkspaceShapeFamily::Unknown;
                if (Curve.Assigned() && Curve.IssuedIndex <= Sketch.Curves().size())
                    Family = FamilyOfCurve(Sketch.Curves()[Curve.IssuedIndex - 1u].Geometry);
                Written.push_back(DeclareWorkspaceCurve(Naming, Records, Curve, Placed.Construction, Family));
            }
        }
        SelectedRecord = Written.empty() ? WorkspaceRecordName{} : Written.front();
    }

    if (!SelectedRecord.Assigned() || Written.empty())
        return Rollback();

    const WorkspaceRecord* Primary = Records.Resolve(SelectedRecord);
    const char* NamingText = DeclaredPlacement(Placed.Subject, Placed.Method).Naming;
    const std::string Description = Primary != nullptr
                                  ? std::string("Declared ") + Primary->Naming
                                  : std::string("Declared ") + (NamingText != nullptr ? NamingText : "shape");
    Revisions.Seal(Description,
                   PlacementCreateOperation(Placed, Profile),
                   Written,
                   Revisions.DeclaredCount() + 1u);
    return true;
}

} // namespace Slate
