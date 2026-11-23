//============================================================================================================================================
//                                                          SKETCHBASIS.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"

namespace Slate
{

SpatialBasis ResolveSketchBasis(const SketchStructure& Sketch)
{
    // 🔴 SHAPES DREW IN MID-AIR BECAUSE THIS ASKED THE WRONG QUESTION. `Declared()` is all-or-nothing
    //    over the plane AND every curve, profile and constraint, so a sketch that HAS been given a
    //    workplane but does not yet hold a single curve answers FALSE. Every point of the first shape
    //    an artist draws was therefore mapped through the fallback Ground basis at the world origin
    //    instead of through the active workplane. On Ground that is invisible; on Front, on Right, or
    //    on any offset plane, the shape lands off the grid entirely -- floating in mid-air.
    //
    // 🔴 Only the plane defines the basis, so only the plane is asked about. `PlaneDeclared()` is
    //    `PlaneStanding && Plane.Declared()`, which is exactly the precondition the four lines below
    //    need and nothing more.
    if (!Sketch.PlaneDeclared())
        return { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };

    const SketchPlane& Plane = Sketch.HeldPlane();
    const SpatialDirection Along  = Normalize(Plane.AlongDirection);
    const SpatialDirection Normal = Normalize(Plane.Normal);
    const SpatialDirection Across = Normalize(Cross(Normal, Along));
    return { Plane.Origin, Along, Across, Normal };
}

}   // namespace Slate
