//============================================================================================================================================
//                                                      WORKPLANESTANDING.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorkplaneStanding/Api/WorkplaneStanding.h"

#include <cmath>

namespace Slate
{

namespace
{
    // 📝 `LengthSquared` comes from CurveSpecification.h — the one definition step 10b folded 119 copies
    //    into. Writing a local one here is how that duplication started; the compiler now refuses it.
    constexpr double DirectionFloor = 1.0e-12;
}

bool Workplane::Declared() const
{
    if (LengthSquared(Normal) <= DirectionFloor || LengthSquared(Along) <= DirectionFloor)
        return false;

    // 🔴 The two must not be parallel. `Cross` of parallel directions is zero-length, and every projection
    //    that uses this plane would then divide by it.
    return LengthSquared(Cross(Normalize(Normal), Normalize(Along))) > DirectionFloor;
}

SpatialDirection Workplane::Across() const
{
    return Normalize(Cross(Normalize(Normal), Normalize(Along)));
}

Workplane ResolveStandingWorkplane(StandingWorkplane Subject)
{
    Workplane Resolved;
    Resolved.Origin = { 0.0, 0.0, 0.0 };
    Resolved.Source = WorkplaneOrigin::Standing;

    switch (Subject)
    {
        // 📝 The ground plane: normal points up, "along" runs to world X. This is the one the grid draws.
        case StandingWorkplane::Ground:
            Resolved.Normal = { 0.0, 1.0, 0.0 };
            Resolved.Along  = { 1.0, 0.0, 0.0 };
            break;

        case StandingWorkplane::Front:
            Resolved.Normal = { 0.0, 0.0, 1.0 };
            Resolved.Along  = { 1.0, 0.0, 0.0 };
            break;

        case StandingWorkplane::Side:
            Resolved.Normal = { 1.0, 0.0, 0.0 };
            Resolved.Along  = { 0.0, 0.0, 1.0 };
            break;

        // ⚠️ No default arm: adding a standing plane must not silently fall through to the ground.
        case StandingWorkplane::SubjectCount:
            break;
    }
    return Resolved;
}

bool ResolveViewedWorkplane(ViewportOrientation Orientation, StandingWorkplane& Viewed)
{
    switch (Orientation)
    {
        // 📝 Down the Y axis: the XZ plane, which is the ground the grid draws and the default.
        case ViewportOrientation::Top:
        case ViewportOrientation::Bottom:
            Viewed = StandingWorkplane::Ground;
            return true;

        // 📝 Down the Z axis, so Z is held at zero and the artist works on XY.
        case ViewportOrientation::Front:
        case ViewportOrientation::Back:
            Viewed = StandingWorkplane::Front;
            return true;

        // 📝 Down the X axis, so X is held at zero and the artist works on YZ.
        case ViewportOrientation::Left:
        case ViewportOrientation::Right:
            Viewed = StandingWorkplane::Side;
            return true;

        // 🔴 Square to no plane. Answering anything here would drag the active plane back to a
        //    standing one the moment the artist orbited off an axis.
        case ViewportOrientation::Isometric:
            break;
    }
    return false;
}

const char* ResolveWorkplaneZeroedAxis(StandingWorkplane Subject)
{
    switch (Subject)
    {
        case StandingWorkplane::Ground: return "Y";   // the XZ plane
        case StandingWorkplane::Front:  return "Z";   // the XY plane
        case StandingWorkplane::Side:   return "X";   // the YZ plane
        case StandingWorkplane::SubjectCount: break;
    }
    return "";
}

Workplane ResolveDefaultWorkplane()
{
    return ResolveStandingWorkplane(StandingWorkplane::Ground);
}

Workplane ResolveOffsetFrom(const Workplane& Subject, double Distance)
{
    Workplane Resolved = Subject;
    if (!Subject.Declared())
        return Resolved;

    Resolved.Origin = Added(Subject.Origin, Scaled(Normalize(Subject.Normal), Distance));

    // 📝 An offset of an offset is still an offset, and a moved standing plane stops being standing —
    //    otherwise the artist could not remove it.
    Resolved.Source = WorkplaneOrigin::Offset;
    return Resolved;
}

Workplane ResolveOffsetWorkplane(StandingWorkplane Subject, double Distance)
{
    return ResolveOffsetFrom(ResolveStandingWorkplane(Subject), Distance);
}

Workplane ResolvePlacedWorkplane(const SpatialPoint& Position, const SpatialDirection& ViewNormal)
{
    Workplane Resolved;
    Resolved.Origin = Position;
    Resolved.Source = WorkplaneOrigin::Placed;

    // ⚠️ A viewer with no direction cannot orient a plane; fall back to the ground rather than producing
    //    one that fails Declared() and refuses every projection downstream.
    if (LengthSquared(ViewNormal) <= DirectionFloor)
    {
        const Workplane Ground = ResolveDefaultWorkplane();
        Resolved.Normal = Ground.Normal;
        Resolved.Along  = Ground.Along;
        return Resolved;
    }

    Resolved.Normal = Normalize(ViewNormal);

    // 🔴 CHOOSING "ALONG" IS WHAT KEEPS THE GRID FROM ARRIVING ROLLED. Project each world axis onto the
    //    plane and take whichever survives best; a candidate nearly parallel to the normal projects to
    //    almost nothing and would give an unstable direction that swings wildly as the view turns.
    const SpatialDirection Candidates[3] = { { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };

    SpatialDirection Best = {};
    double BestLength = 0.0;
    for (const SpatialDirection& Candidate : Candidates)
    {
        // The candidate with its normal-facing part removed: what is left lies in the plane.
        // ⚠️ `Difference` takes two POINTS. Removing the normal-facing part of a direction is a direction
        //    subtraction, which is spelled by adding the negation.
        const SpatialDirection Flattened =
            Added(Candidate, Scaled(Resolved.Normal, -Dot(Candidate, Resolved.Normal)));
        const double Length = LengthSquared(Flattened);
        if (Length > BestLength)
        {
            BestLength = Length;
            Best = Flattened;
        }
    }

    Resolved.Along = BestLength > DirectionFloor ? Normalize(Best) : SpatialDirection{ 1.0, 0.0, 0.0 };
    return Resolved;
}

void ResolveWorkplaneCoordinates(const Workplane& Plane,
                                 const SpatialPoint& Position,
                                 double& Along,
                                 double& Across)
{
    const SpatialDirection OriginToPosition = Difference(Plane.Origin, Position);
    Along  = Dot(OriginToPosition, Normalize(Plane.Along));
    Across = Dot(OriginToPosition, Plane.Across());
}

SpatialPoint ResolveWorkplanePosition(const Workplane& Plane, double Along, double Across)
{
    return Added(Plane.Origin, Added(Scaled(Normalize(Plane.Along), Along),
                                     Scaled(Plane.Across(), Across)));
}

double ResolveWorkplaneOffset(const Workplane& Plane, const SpatialPoint& Position)
{
    return Dot(Difference(Plane.Origin, Position), Normalize(Plane.Normal));
}

SpatialPoint ResolveWorkplaneProjection(const Workplane& Plane, const SpatialPoint& Position)
{
    const double Offset = ResolveWorkplaneOffset(Plane, Position);
    return Added(Position, Scaled(Normalize(Plane.Normal), -Offset));
}

}   // namespace Slate
