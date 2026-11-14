/*============================================================================================================================================
                                                          CONFORMALUNWRAPSOLVER.H
============================================================================================================================================*/
// 🧩 Parallel multi-lane Harmonic / Cotangent Conformal UV Flattener executing across Threading::ExecutionChannels

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <cstdint>

namespace Frontier::Workspaces
{

struct ConformalUnwrapReport
{
    uint32_t IslandsFlattened     = 0u;   // [count] - Number of seam-bounded islands unwrapped
    uint32_t FlippedFacesRepaired = 0u;   // [count] - Faces rescued to positive CCW winding
    double   ElapsedMilliseconds  = 0.0;  // [ms]    - Wall-clock unwrap time across parallel lanes
};

class ConformalUnwrapSolver
{
public:
    // Flattens all seam-bounded UV islands in parallel across ExecutionChannels (or single-lane if Channels == nullptr),
    // guaranteeing positive CCW UV signed area (0 flipped faces) and preserving 3D surface metric proportions.
    static ConformalUnwrapReport FlattenIslandsConformally(
        GeometrySpace::PolygonNetwork& Network,
        Threading::ExecutionChannels*  OptionalChannels    = nullptr,
        uint32_t                       HarmonicIterations  = 28u);
};

} // namespace Frontier::Workspaces
