//============================================================================================================================================
//                                                         SKETCHPICKING.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"

#include <cstddef>

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slate
{

namespace
{

double PickingDistanceSquared(const SpatialPoint& Left, const SpatialPoint& Right)
{
    return LengthSquared(Difference(Left, Right));
}

bool PickingPointsNear(const SpatialPoint& Left, const SpatialPoint& Right, double Tolerance = 1.0e-5)
{
    return PickingDistanceSquared(Left, Right) <= Tolerance * Tolerance;
}

double PickingSignedArea(const std::vector<SpatialPoint>& Points)
{
    if (Points.size() < 3u)
        return 0.0;

    double Area = 0.0;
    for (std::size_t Index = 0u; Index < Points.size(); ++Index)
    {
        const SpatialPoint& Current = Points[Index];
        const SpatialPoint& Next = Points[(Index + 1u) % Points.size()];
        Area += Current.Left * Next.Forward - Next.Left * Current.Forward;
    }
    return Area * 0.5;
}

bool PickingPointInsideLoop(const SpatialPoint& Probe, const std::vector<SpatialPoint>& Loop)
{
    if (Loop.size() < 3u)
        return false;

    bool Inside = false;
    for (std::size_t Index = 0u, Prior = Loop.size() - 1u; Index < Loop.size(); Prior = Index++)
    {
        const SpatialPoint& A = Loop[Index];
        const SpatialPoint& B = Loop[Prior];
        const bool Crosses = ((A.Forward > Probe.Forward) != (B.Forward > Probe.Forward))
                          && (Probe.Left < (B.Left - A.Left)
                                           * (Probe.Forward - A.Forward)
                                           / ((B.Forward - A.Forward) + 1.0e-300)
                                           + A.Left);
        if (Crosses)
            Inside = !Inside;
    }
    return Inside;
}

bool AppendProfileLoopPolyline(const SketchStructure& Sketch,
                               const ProfileLoop& Loop,
                               std::vector<SpatialPoint>& Points)
{
    Points.clear();
    for (const ProfileCurveUse& Use : Loop.Traversal)
    {
        if (Use.TraversedCurve.IssuedIndex == 0u || Use.TraversedCurve.IssuedIndex > Sketch.Curves().size())
            return false;

        std::vector<SpatialPoint> Segment;
        AppendCurvePolyline(Sketch.Curves()[Use.TraversedCurve.IssuedIndex - 1u].Geometry, Segment, 48u);
        if (Segment.size() < 2u)
            return false;
        if (!Use.SameSense)
            std::reverse(Segment.begin(), Segment.end());

        if (Points.empty())
        {
            Points = Segment;
            continue;
        }

        if (!PickingPointsNear(Points.back(), Segment.front()))
        {
            if (PickingPointsNear(Points.back(), Segment.back()))
                std::reverse(Segment.begin(), Segment.end());
            else
                return false;
        }

        Points.insert(Points.end(), Segment.begin() + 1, Segment.end());
    }

    if (Points.size() >= 2u && PickingPointsNear(Points.front(), Points.back()))
        Points.pop_back();
    return Points.size() >= 3u;
}

WorkspaceRecordName ResolveProfileRecordForCurve(const SketchStructure& Sketch,
                                                 const WorkspaceRecordStructure& Records,
                                                 SketchCurveName Curve)
{
    if (!Curve.Assigned())
        return {};

    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record == nullptr || !Record->Profile.Assigned() ||
            Record->Profile.IssuedIndex > Sketch.Profiles().size())
            continue;
        if (ProfileContainsCurve(Sketch.Profiles()[Record->Profile.IssuedIndex - 1u], Curve))
            return { Index };
    }

    return {};
}

WorkspaceRecordName ResolveProfileRecordAtPoint(const SketchStructure& Sketch,
                                                const WorkspaceRecordStructure& Records,
                                                const SpatialPoint& Probe)
{
    WorkspaceRecordName Best = {};
    double BestArea = 1.0e300;

    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record == nullptr || !Record->Profile.Assigned() ||
            Record->Profile.IssuedIndex > Sketch.Profiles().size())
            continue;

        const ProfileSpecification& Profile = Sketch.Profiles()[Record->Profile.IssuedIndex - 1u];
        bool InsideOuter = false;
        bool InsideHole = false;
        double Area = 1.0e300;

        for (const ProfileLoop& Loop : Profile.HeldLoops())
        {
            std::vector<SpatialPoint> LoopPoints;
            if (!AppendProfileLoopPolyline(Sketch, Loop, LoopPoints))
                continue;

            if (!PickingPointInsideLoop(Probe, LoopPoints))
                continue;

            const double LoopArea = std::abs(PickingSignedArea(LoopPoints));
            if (Loop.Orientation == ProfileLoopOrientation::Outer)
            {
                InsideOuter = true;
                Area = std::min(Area, LoopArea);
            }
            else
            {
                InsideHole = true;
            }
        }

        if (InsideOuter && !InsideHole && Area < BestArea)
        {
            Best = { Index };
            BestArea = Area;
        }
    }

    return Best;
}

}   // namespace

bool ResolveSketchPointPosition(const SketchStructure& Sketch,
                                SketchPointName Subject,
                                SpatialPoint& Position)
{
    if (!Subject.Assigned())
        return false;

    // ⚠️ A point name PACKS its curve into the high bits. A name with no curve part belongs to no curve
    //    and cannot be located, so it is refused rather than searched for.
    const std::uint32_t CurveIndex = Subject.IssuedIndex >> 8u;
    if (CurveIndex == 0u)
        return false;

    std::vector<SketchPointPlacement> Points;
    if (!ResolveSketchPoints(Sketch, { CurveIndex }, Points))
        return false;

    for (const SketchPointPlacement& Current : Points)
        if (Current.Name.IssuedIndex == Subject.IssuedIndex)
        {
            Position = Current.Position;
            return true;
        }

    return false;
}

bool ProfileContainsCurve(const ProfileSpecification& Profile, SketchCurveName Curve)
{
    for (const ProfileLoop& Loop : Profile.HeldLoops())
        for (const ProfileCurveUse& Use : Loop.Traversal)
            if (Use.TraversedCurve.IssuedIndex == Curve.IssuedIndex)
                return true;
    return false;
}

WorkspaceRecordName ResolveRecordForPoint(const SketchStructure& Sketch,
                                           const WorkspaceRecordStructure& Records,
                                           SketchPointName Point)
{
    // 🔴 REFUSES AN UNASSIGNED NAME. Without this the loop below matches the first record whose
    //    `SketchPoint` is also zero — which is EVERY record that carries no point at all, so a dimension
    //    or a folder would be handed back as the owner of a point that does not exist. The shipped code
    //    had no such guard and relied on every caller having a real point; the proof pins it instead.
    if (!Point.Assigned())
        return {};

    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record != nullptr && Record->SketchPoint.IssuedIndex == Point.IssuedIndex)
            return { Index };
    }

    const std::uint32_t CurveIndex = Point.IssuedIndex >> 8u;
    if (CurveIndex == 0u)
        return {};

    // 📝 A point with no record of its own belongs first to the curve that owns it.
    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record != nullptr && Record->SketchCurve.IssuedIndex == CurveIndex)
            return { Index };
    }

    // 🔴 PROFILE-ONLY SHAPES STILL HAVE VERTICES. A triangle or rectangle often reaches the directory as
    //    one closed-profile record with no child edge rows; falling out here would make its corners
    //    unpickable in Vertex mode and Free mode would degrade them into edge/profile picks.
    if (CurveIndex <= Sketch.Curves().size())
    {
        const SketchCurveName Curve = { CurveIndex };
        for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
        {
            const WorkspaceRecord* Record = Records.Resolve({ Index });
            if (Record == nullptr || !Record->Profile.Assigned() ||
                Record->Profile.IssuedIndex > Sketch.Profiles().size())
                continue;
            if (ProfileContainsCurve(Sketch.Profiles()[Record->Profile.IssuedIndex - 1u], Curve))
                return { Index };
        }
    }

    return {};
}

WorkspaceRecordName ResolveRecordForCurve(const SketchStructure& Sketch,
                                          const WorkspaceRecordStructure& Records,
                                          SketchCurveName Curve)
{
    // 🔴 The same hazard as above: an unassigned curve would match every record that carries no curve.
    if (!Curve.Assigned())
        return {};

    // 🔴 A DIRECT RECORD WINS. Grabbing one edge of a rectangle should select the edge when the edge is a
    //    record in its own right, and the enclosing profile only when it is not.
    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record != nullptr && Record->SketchCurve.IssuedIndex == Curve.IssuedIndex)
            return { Index };
    }

    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record == nullptr || !Record->Profile.Assigned() ||
            Record->Profile.IssuedIndex > Sketch.Profiles().size())
            continue;
        if (ProfileContainsCurve(Sketch.Profiles()[Record->Profile.IssuedIndex - 1u], Curve))
            return { Index };
    }

    return {};
}

bool ResolveCurvePivot(const SketchStructure& Sketch, SketchCurveName Curve, SpatialPoint& Pivot)
{
    if (!Curve.Assigned() || Curve.IssuedIndex > Sketch.Curves().size())
        return false;

    const CurveSpecification& Geometry = Sketch.Curves()[Curve.IssuedIndex - 1u].Geometry;
    switch (Geometry.Subject())
    {
        case CurveSubject::Line:
        {
            const LineCurve& Line = Geometry.HeldLine();
            Pivot = { (Line.Origin.Left + Line.Terminus.Left) * 0.5,
                      (Line.Origin.Up + Line.Terminus.Up) * 0.5,
                      (Line.Origin.Forward + Line.Terminus.Forward) * 0.5 };
            return true;
        }
        case CurveSubject::CircularArc:
            Pivot = Geometry.HeldCircularArc().Centre;
            return true;
        case CurveSubject::Circle:
            Pivot = Geometry.HeldCircle().Centre;
            return true;
        case CurveSubject::EllipticalArc:
            Pivot = Geometry.HeldEllipticalArc().Centre;
            return true;
        case CurveSubject::Ellipse:
            Pivot = Geometry.HeldEllipse().Centre;
            return true;

        // 📝 The MIDDLE control point, not the average of them. Averaging drags the pivot towards wherever
        //    the controls bunch up, so it would shift under the artist as they edit the spline.
        case CurveSubject::Bezier:
            if (!Geometry.HeldBezier().ControlPoints.empty())
            {
                Pivot = Geometry.HeldBezier().ControlPoints[Geometry.HeldBezier().ControlPoints.size() / 2u];
                return true;
            }
            return false;
        case CurveSubject::BasisSpline:
            if (!Geometry.HeldBasisSpline().ControlPoints.empty())
            {
                Pivot = Geometry.HeldBasisSpline().ControlPoints[Geometry.HeldBasisSpline().ControlPoints.size() / 2u];
                return true;
            }
            return false;
        case CurveSubject::RationalSpline:
            if (!Geometry.HeldRationalSpline().ControlPoints.empty())
            {
                Pivot = Geometry.HeldRationalSpline().ControlPoints[Geometry.HeldRationalSpline().ControlPoints.size() / 2u];
                return true;
            }
            return false;
        case CurveSubject::Hermite:
        {
            const HermiteCurve& Hermite = Geometry.HeldHermite();
            if (Hermite.ControlPoints.size() >= 2u)
            {
                Pivot = Hermite.ControlPoints[Hermite.ControlPoints.size() / 2u];
                return true;
            }
            Pivot = { (Hermite.StartPoint.Left + Hermite.EndPoint.Left) * 0.5,
                      (Hermite.StartPoint.Up + Hermite.EndPoint.Up) * 0.5,
                      (Hermite.StartPoint.Forward + Hermite.EndPoint.Forward) * 0.5 };
            return true;
        }

        // ⚠️ No default arm: a new curve subject must be given a pivot deliberately, not silently refused.
        case CurveSubject::SubjectCount:
            return false;
    }
    return false;
}

bool ResolveProfilePivot(const SketchStructure& Sketch, ProfileNameInFeature Profile, SpatialPoint& Pivot)
{
    if (!Profile.Assigned() || Profile.IssuedIndex > Sketch.Profiles().size())
        return false;

    const ProfileSpecification& Source = Sketch.Profiles()[Profile.IssuedIndex - 1u];
    std::uint32_t Count = 0u;
    Pivot = {};

    // ⚠️ The mean of the CURVES' pivots, not of their points. A curve cut into more segments than its
    //    neighbours would otherwise drag the profile's pivot towards itself.
    for (const ProfileLoop& Loop : Source.HeldLoops())
        for (const ProfileCurveUse& Use : Loop.Traversal)
        {
            SpatialPoint CurvePivot = {};
            if (!ResolveCurvePivot(Sketch, { Use.TraversedCurve.IssuedIndex }, CurvePivot))
                continue;
            Pivot = { Pivot.Left + CurvePivot.Left,
                      Pivot.Up + CurvePivot.Up,
                      Pivot.Forward + CurvePivot.Forward };
            ++Count;
        }

    if (Count == 0u)
        return false;

    const double Scale = 1.0 / static_cast<double>(Count);
    Pivot = { Pivot.Left * Scale, Pivot.Up * Scale, Pivot.Forward * Scale };
    return true;
}

void AppendPlacementUnique(std::vector<SketchPlacementSubject>& Placements,
                           const SketchPlacementSubject& Placement)
{
    // 🔴 Two curves meeting at a corner both report that corner. Collecting it twice would move it twice
    //    as far as the cursor travelled.
    for (const SketchPlacementSubject& Existing : Placements)
    {
        if (Placement.ControlPlacement == Existing.ControlPlacement)
        {
            if (!Placement.ControlPlacement && Placement.Point.IssuedIndex == Existing.Point.IssuedIndex)
                return;
            if (Placement.ControlPlacement && Placement.Control.IssuedIndex == Existing.Control.IssuedIndex)
                return;
        }
    }
    Placements.push_back(Placement);
}

void CollectCurvePlacements(const SketchStructure& Sketch,
                            SketchCurveName Curve,
                            std::vector<SketchPlacementSubject>& Placements)
{
    std::vector<SketchPointPlacement> Points;
    if (ResolveSketchPoints(Sketch, Curve, Points))
        for (const SketchPointPlacement& Point : Points)
            AppendPlacementUnique(Placements, { false, Point.Name, {}, Point.Position });

    std::vector<SketchControlPlacement> Controls;
    if (ResolveSketchControls(Sketch, Curve, Controls))
        for (const SketchControlPlacement& Control : Controls)
            AppendPlacementUnique(Placements, { true, {}, Control.Name, Control.Position });
}

void CollectProfilePlacements(const SketchStructure& Sketch,
                              ProfileNameInFeature Profile,
                              std::vector<SketchPlacementSubject>& Placements)
{
    if (!Profile.Assigned() || Profile.IssuedIndex > Sketch.Profiles().size())
        return;

    for (const ProfileLoop& Loop : Sketch.Profiles()[Profile.IssuedIndex - 1u].HeldLoops())
        for (const ProfileCurveUse& Use : Loop.Traversal)
            CollectCurvePlacements(Sketch, { Use.TraversedCurve.IssuedIndex }, Placements);
}

SketchPick ResolveSketchPick(const SketchStructure& Sketch,
                             const WorkspaceRecordStructure& Records,
                             const SpatialPoint& Probe,
                             double MaximumDistance)
{
    // 🔴 POINT FIRST. A curve passes through its own endpoints, so a search that compared distances would
    //    return the curve and the artist could never grab the end they were aiming at.
    SketchPointPlacement Point = {};
    double Distance = MaximumDistance;
    if (ResolveNearestSketchPoint(Sketch, Probe, MaximumDistance, Point, Distance))
    {
        SketchPick Pick = {};
        Pick.Subject  = SketchPickSubject::Point;
        Pick.Point    = Point.Name;
        Pick.Curve    = Point.SourceCurve;
        Pick.Position = Point.Position;
        Pick.Record   = ResolveRecordForPoint(Sketch, Records, Point.Name);

        // ⚠️ Only returned once the directory can name it. A pick with no record is a selection nothing
        //    can act on, so the search falls through to the next kind instead.
        if (Pick.Record.Assigned())
            return Pick;
    }

    SketchControlPlacement Control = {};
    Distance = MaximumDistance;
    if (ResolveNearestSketchControl(Sketch, Probe, MaximumDistance, Control, Distance))
    {
        SketchPick Pick = {};
        Pick.Subject  = SketchPickSubject::Control;
        Pick.Control  = Control.Name;
        Pick.Curve    = Control.SourceCurve;
        Pick.Position = Control.Position;
        Pick.Record   = ResolveRecordForCurve(Sketch, Records, Control.SourceCurve);
        if (Pick.Record.Assigned())
            return Pick;
    }

    SketchCurveName Curve = {};
    Distance = MaximumDistance;
    if (ResolveNearestSketchCurve(Sketch, Probe, MaximumDistance, Curve, Distance))
    {
        SketchPick Pick = {};
        Pick.Subject = SketchPickSubject::Curve;
        Pick.Curve   = Curve;
        Pick.Record  = ResolveRecordForCurve(Sketch, Records, Curve);

        // 📝 A curve pick reports its PIVOT, not the point on it nearest the cursor: that is what a
        //    following rotate or scale turns about.
        ResolveCurvePivot(Sketch, Curve, Pick.Position);
        return Pick;
    }

    return {};
}

SketchPick ResolveSketchPickForElement(const SketchStructure& Sketch,
                                       const WorkspaceRecordStructure& Records,
                                       const SpatialPoint& Probe,
                                       double MaximumDistance,
                                       SelectionElement Element)
{
    double Distance = MaximumDistance;

    // ⚠️ EACH ARM RETURNS OR FALLS OUT — none of them falls through to another KIND. That is the whole
    //    difference from the unrestricted search above, and it is what the artist asked for: with a mode
    //    standing, the wanted kind is the only candidate and a miss is a miss.
    switch (Element)
    {
        case SelectionElement::Vertex:
        {
            // 📝 An endpoint, then a Bezier control handle. Both are grabbed and dragged and neither is
            //    an edge, so both are vertices to the artist; the order between them is the same
            //    smallest-target-first rule the unrestricted search uses.
            SketchPointPlacement Point = {};
            if (ResolveNearestSketchPoint(Sketch, Probe, MaximumDistance, Point, Distance))
            {
                SketchPick Pick = {};
                Pick.Subject  = SketchPickSubject::Point;
                Pick.Point    = Point.Name;
                Pick.Curve    = Point.SourceCurve;
                Pick.Position = Point.Position;
                Pick.Record   = ResolveRecordForPoint(Sketch, Records, Point.Name);
                if (Pick.Record.Assigned())
                    return Pick;
            }

            SketchControlPlacement Control = {};
            Distance = MaximumDistance;
            if (ResolveNearestSketchControl(Sketch, Probe, MaximumDistance, Control, Distance))
            {
                SketchPick Pick = {};
                Pick.Subject  = SketchPickSubject::Control;
                Pick.Control  = Control.Name;
                Pick.Curve    = Control.SourceCurve;
                Pick.Position = Control.Position;
                Pick.Record   = ResolveRecordForCurve(Sketch, Records, Control.SourceCurve);
                if (Pick.Record.Assigned())
                    return Pick;
            }
            return {};
        }

        case SelectionElement::Edge:
        {
            SketchCurveName Curve = {};
            if (ResolveNearestSketchCurve(Sketch, Probe, MaximumDistance, Curve, Distance))
            {
                SketchPick Pick = {};
                Pick.Subject = SketchPickSubject::Curve;
                Pick.Curve   = Curve;
                Pick.Record  = ResolveRecordForCurve(Sketch, Records, Curve);
                ResolveCurvePivot(Sketch, Curve, Pick.Position);
                if (Pick.Record.Assigned())
                    return Pick;
            }
            return {};
        }

        case SelectionElement::Face:
        case SelectionElement::Object:
        {
            // 🔴 A PROFILE IS HIT THROUGH ITS AREA, not only by grazing one of its edges. The previous
            //    search asked the nearest curve for its owner, which meant clicking the middle of a face
            //    selected nothing at all unless the pointer happened to be within edge tolerance.
            const WorkspaceRecordName ProfileRecord = ResolveProfileRecordAtPoint(Sketch, Records, Probe);
            if (ProfileRecord.Assigned())
            {
                SketchPick WholeShape = {};
                if (ResolvePickForRecord(Sketch, Records, ProfileRecord, WholeShape))
                    return WholeShape;
            }

            // 📝 And when the probe is on the boundary rather than inside, Face/Object still mean the
            //    enclosing profile if one exists, not the edge record the unrestricted search would pick.
            SketchCurveName Curve = {};
            if (ResolveNearestSketchCurve(Sketch, Probe, MaximumDistance, Curve, Distance))
            {
                const WorkspaceRecordName ProfileOwner = ResolveProfileRecordForCurve(Sketch, Records, Curve);
                if (ProfileOwner.Assigned())
                {
                    SketchPick WholeShape = {};
                    if (ResolvePickForRecord(Sketch, Records, ProfileOwner, WholeShape))
                        return WholeShape;
                }

                if (Element == SelectionElement::Object)
                {
                    SketchPick WholeShape = {};
                    const WorkspaceRecordName Owner = ResolveRecordForCurve(Sketch, Records, Curve);
                    if (Owner.Assigned() && ResolvePickForRecord(Sketch, Records, Owner, WholeShape))
                        return WholeShape;
                }
            }
            return {};
        }

        // 🔴 WHATEVER IS NEAREST, WHICHEVER KIND. This is the unrestricted search, reached only when the
        //    artist asks for it by name. `ResolveSketchPick` already resolves smallest-target-first —
        //    point, then control, then curve — so Free is that function and not a second copy of it.
        case SelectionElement::Free:
            return ResolveSketchPick(Sketch, Records, Probe, MaximumDistance);

        case SelectionElement::ElementCount:
            break;
    }

    return {};
}

bool ResolvePickForRecord(const SketchStructure& Sketch,
                          const WorkspaceRecordStructure& Records,
                          WorkspaceRecordName Record,
                          SketchPick& Pick)
{
    Pick = {};
    const WorkspaceRecord* Held = Records.Resolve(Record);
    if (Held == nullptr)
        return false;

    Pick.Record = Record;
    switch (Held->Subject)
    {
        case WorkspaceRecordSubject::Point:
            Pick.Subject = SketchPickSubject::Point;
            Pick.Point   = Held->SketchPoint;
            return ResolveSketchPointPosition(Sketch, Held->SketchPoint, Pick.Position);

        case WorkspaceRecordSubject::OpenCurve:
            Pick.Subject = SketchPickSubject::Curve;
            Pick.Curve   = Held->SketchCurve;
            return ResolveCurvePivot(Sketch, Held->SketchCurve, Pick.Position);

        // 📝 A profile, a surface and a solid are all picked AS the record. There is no smaller thing to
        //    name: the artist selected the whole object in the outliner.
        case WorkspaceRecordSubject::ClosedProfile:
        case WorkspaceRecordSubject::ThinSurface:
        case WorkspaceRecordSubject::Solid:
            Pick.Subject = SketchPickSubject::Record;
            return ResolveProfilePivot(Sketch, Held->Profile, Pick.Position);

        // ⚠️ A dimension, a constraint, a folder and a pattern have no geometry to grab. Refusing is
        //    correct: selecting one in the outliner must not arm a transform.
        default:
            return false;
    }
}

bool SameSketchPickIdentity(const SketchPick& Left, const SketchPick& Right)
{
    if (!Left.Standing() || !Right.Standing() || Left.Subject != Right.Subject ||
        Left.Record.IssuedIndex != Right.Record.IssuedIndex)
        return false;

    switch (Left.Subject)
    {
        case SketchPickSubject::Point:
            return Left.Point.IssuedIndex == Right.Point.IssuedIndex;
        case SketchPickSubject::Control:
            return Left.Control.IssuedIndex == Right.Control.IssuedIndex;
        case SketchPickSubject::Curve:
            return Left.Curve.IssuedIndex == Right.Curve.IssuedIndex;
        case SketchPickSubject::Record:
            return true;
        default:
            return false;
    }
}

void SetSketchPick(SketchSelectionSet& Set, const SketchPick& Pick, bool Additive)
{
    if (!Pick.Standing())
    {
        if (!Additive)
            Set.Clear();
        return;
    }

    if (!Additive)
    {
        Set.Items.clear();
        Set.Items.push_back(Pick);
        return;
    }

    for (std::size_t Index = 0u; Index < Set.Items.size(); ++Index)
    {
        if (!SameSketchPickIdentity(Set.Items[Index], Pick))
            continue;
        Set.Items.erase(Set.Items.begin() + static_cast<std::ptrdiff_t>(Index));
        return;
    }
    // The most recently added item is the active item. This keeps the visible marker and
    // directory focus on the thing just Shift-clicked while the complete set remains selected.
    Set.Items.insert(Set.Items.begin(), Pick);
}

void RefreshSketchSelectionPositions(SketchSelectionSet& Set,
                                      const SketchStructure& Sketch,
                                      const WorkspaceRecordStructure& Records)
{
    for (SketchPick& Pick : Set.Items)
    {
        if (Pick.Subject == SketchPickSubject::Point)
            ResolveSketchPointPosition(Sketch, Pick.Point, Pick.Position);
        else if (Pick.Subject == SketchPickSubject::Curve)
            ResolveCurvePivot(Sketch, Pick.Curve, Pick.Position);
        else if (Pick.Subject == SketchPickSubject::Record)
        {
            SketchPick Refreshed = {};
            if (ResolvePickForRecord(Sketch, Records, Pick.Record, Refreshed))
                Pick.Position = Refreshed.Position;
        }
        else if (Pick.Subject == SketchPickSubject::Control)
        {
            for (std::uint32_t CurveIndex = 1u;
                 CurveIndex <= Sketch.Curves().size() && Pick.Subject == SketchPickSubject::Control;
                 ++CurveIndex)
            {
                std::vector<SketchControlPlacement> Controls;
                if (!ResolveSketchControls(Sketch, { CurveIndex }, Controls))
                    continue;
                for (const SketchControlPlacement& Control : Controls)
                    if (Control.Name.IssuedIndex == Pick.Control.IssuedIndex)
                    {
                        Pick.Position = Control.Position;
                        CurveIndex = static_cast<std::uint32_t>(Sketch.Curves().size());
                        break;
                    }
            }
        }
    }
}

}   // namespace Slate
