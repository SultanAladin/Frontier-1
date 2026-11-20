/*====================================================================================================================================
                                                       BLENDCATALOGUE.H
====================================================================================================================================*/
// 🧩 Blend specimens: bodies, edge selections, closed-form volume changes and membership oracles (validation + viewer)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Blend/RollingBallGeometry.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Families: Harness (the old kernel's 15-row RollingBallBlend harness, same bodies and sizes), BevelLab (the old
//    editor proof scene, 6 bevels), Extra (angles ≠ 90°, elliptical caps) and Refusal (the only correct answer is a
//    refusal). Expected volume changes are derived by hand — prisms, Pappus in the meridian half-plane — never from
//    the kernel.
struct BlendSpecimen
{
    std::string                  Name            = {};                                       // [-]    - Specimen label
    std::string                  Family          = {};                                       // [-]    - Harness / BevelLab / Extra / Refusal
    std::string                  Description     = {};                                       // [-]    - What it exercises
    BodyToken                    Body            = {};                                       // [-]    - Body to blend
    std::vector<EdgeToken>       Edges           = {};                                       // [-]    - Selection
    BlendSettings                Settings        = {};                                       // [-]    - Profile and size
    double                       ExpectedDelta   = std::nan("");                             // [len³] - V(after) − V(before)
    int                          ExpectedFaces   = -1;                                       // [-]    - Faces after (−1: not asserted)
    bool                         RefusalExpected = false;                                    // [-]    - Must refuse
    std::function<double(Vector3)> Clearance     = {};                                       // [len]  - Signed clearance oracle (> 0 inside)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> EnumerateBlendSpecimens();

// Builds the named specimen into the partition (empty Name on an unknown specimen).
[[nodiscard]] BlendSpecimen ConstructBlendSpecimen(ModelPartition& Partition, const std::string& Name);

// 📝 Volume change of a rim blend by Pappus in the meridian half-plane (ρ, z): corner E, unit directions along both
//    faces away from E, ball radius (fillet) or setback (chamfer); negative when material is removed (convex).
[[nodiscard]] double MeasureRimBlendVolume(Vector2 Corner, Vector2 AlongFirst, Vector2 AlongSecond, double Size, BlendProfile Profile, bool ConvexCondition);

// 📝 Material removed by a variable fillet on a 90° edge of length Length between two planes, radius Low at one end
//    and High at the other (exact cone, divergence theorem about its apex; constant radius: (1 − π/4)·r²·Length).
[[nodiscard]] double MeasureVariableFilletLoss(double Length, double Low, double High);

} // namespace Frontier::GeometrySpace::Parametric
