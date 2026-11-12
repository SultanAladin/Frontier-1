/*============================================================================================================================================
                                                       TEXELDENSITYEQUALIZER.H
============================================================================================================================================*/
// 🧩 Physical texel density equalizer (px/m) and multi-UDIM (1001..100N) tile distributor for PolygonNetwork

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Workspaces/UV/Solvers/ConcaveSkylinePacker.h"

#include <cstdint>

namespace Frontier::Workspaces
{

struct TexelDensityReport
{
    uint32_t IslandsEqualized        = 0u;    // [count] - Islands rescaled to physical texel density
    float    AchievedMeanTexelPerM   = 0.0f;  // [px/m]  - Achieved average texel density across islands
};

class TexelDensityEqualizer
{
public:
    // Rescales every seam-bounded UV island about its UV centroid so physical texel density matches TargetTexelsPerMeter
    // (clamped cleanly inside its current UDIM [TileU, TileU+1] x [0, 1] tile bounds).
    static TexelDensityReport EqualizePhysicalTexelDensity(
        GeometrySpace::PolygonNetwork& Network,
        float                          TargetTexelsPerMeter = 512.0f,
        uint32_t                       ReferenceTilePixels  = 2048u);
};

} // namespace Frontier::Workspaces
