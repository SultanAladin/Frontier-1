#include "SlateShape/World/WorldSketchSnap/Api/WorldSketchSnap.h"

#include <cmath>
#include <cstdio>

using namespace Slate;

namespace
{
std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Sentence)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Sentence);
    }
}

bool Near(double Left, double Right, double Tolerance = 1.0e-4)
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right, double Tolerance = 1.0e-4)
{
    return Near(Left.Left, Right.Left, Tolerance)
        && Near(Left.Up, Right.Up, Tolerance)
        && Near(Left.Forward, Right.Forward, Tolerance);
}

WorldSnapMask Only(std::uint32_t Subject)
{
    WorldSnapMask Accepted = {};
    Accepted.EndpointAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Endpoint);
    Accepted.MidpointAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Midpoint);
    Accepted.CentreAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Centre);
    Accepted.ControlAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Control);
    Accepted.AlongCurveAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::AlongCurve);
    Accepted.IntersectionAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Intersection);
    Accepted.GridAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Grid);
    Accepted.PerpendicularAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Perpendicular);
    Accepted.TangentAccepted = Subject == static_cast<std::uint32_t>(WorldSnapSubject::Tangent);
    return Accepted;
}

void ProveWorldSnapHasNoSketchDependency()
{
    std::printf("\n1. World snapping answers from exact 3D curves and an explicit support frame\n");

    const WorldPlacementFrame Active = { { 0.0, 40.0, 0.0 },
                                         { 0.0, 1.0, 0.0 },
                                         { 1.0, 0.0, 0.0 } };
    WorldSketchStructure World = {};
    const WorldCurveName Diagonal = World.DeclareLine({ 0.0, 40.0, 0.0 }, { 100.0, 40.0, 100.0 }, Active);
    World.DeclareLine({ 0.0, 40.0, 100.0 }, { 100.0, 40.0, 0.0 }, Active);
    World.DeclareCircle({ { 200.0, 40.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }, 20.0 }, Active);

    WorldSnapMask EndpointAndAlong = {};
    EndpointAndAlong.AlongCurveAccepted = true;
    const WorldSnapPlacement Endpoint = ResolveNearestWorldSnap(
        World, Active, { -0.25, 40.0, 0.2 }, 10.0, EndpointAndAlong, 10.0);
    Claim(Endpoint.Resolved() && Endpoint.Subject == WorldSnapSubject::Endpoint
       && Endpoint.SourceCurve.IssuedIndex == Diagonal.IssuedIndex
       && SamePoint(Endpoint.Position, { 0.0, 40.0, 0.0 }),
          "semantic endpoint precedence wins over a nearer along-curve projection");

    const WorldSnapPlacement Intersection = ResolveNearestWorldSnap(
        World, Active, { 50.0, 40.0, 50.0 }, 2.0,
        Only(static_cast<std::uint32_t>(WorldSnapSubject::Intersection)), 10.0);
    Claim(Intersection.Resolved() && Intersection.Subject == WorldSnapSubject::Intersection
       && SamePoint(Intersection.Position, { 50.0, 40.0, 50.0 }),
          "intersection snapping resolves in the supplied plane rather than fixed world axes");

    const WorldSnapPlacement Centre = ResolveNearestWorldSnap(
        World, Active, { 200.5, 40.0, 0.5 }, 2.0,
        Only(static_cast<std::uint32_t>(WorldSnapSubject::Centre)), 10.0);
    Claim(Centre.Resolved() && Centre.Subject == WorldSnapSubject::Centre
       && SamePoint(Centre.Position, { 200.0, 40.0, 0.0 }),
          "circle centres are offered by world semantic controls");

    const WorldSnapPlacement Grid = ResolveNearestWorldSnap(
        World, Active, { 13.0, 40.0, 26.0 }, 20.0,
        Only(static_cast<std::uint32_t>(WorldSnapSubject::Grid)), 10.0);
    Claim(Grid.Resolved() && Grid.Subject == WorldSnapSubject::Grid
       && SamePoint(Grid.Position, { 10.0, 40.0, 30.0 }),
          "grid snapping uses the active frame origin and axes");
}

void ProvePendingAnchorsStayWorldSemantic()
{
    std::printf("\n2. Pending placement anchors snap before any compatibility sketch exists\n");

    const WorldPlacementFrame Active = { { 0.0, 40.0, 0.0 },
                                         { 0.0, 1.0, 0.0 },
                                         { 1.0, 0.0, 0.0 } };
    const std::vector<SpatialPoint> Pending = { { 80.0, 40.0, 20.0 } };
    const WorldSnapPlacement Snapped = ResolveNearestWorldSnap(
        {}, Active, { 80.5, 40.0, 20.5 }, 2.0,
        Only(static_cast<std::uint32_t>(WorldSnapSubject::Endpoint)), 10.0, Pending);

    Claim(Snapped.Resolved() && Snapped.Subject == WorldSnapSubject::Endpoint
       && !Snapped.SourceCurve.Assigned()
       && SamePoint(Snapped.Position, Pending.front()),
          "the first loop can close on its pending world anchor without sketch declarations");
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("WORLD SKETCH SNAP PROOF\n");
    std::printf("=========================================================================\n");

    ProveWorldSnapHasNoSketchDependency();
    ProvePendingAnchorsStayWorldSemantic();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
