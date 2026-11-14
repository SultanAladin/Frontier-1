/*============================================================================================================================================
                                                          FIREWORKPATTERNSAMPLER.H
============================================================================================================================================*/
// 🧩 All 10 pyrotechnic shell & New Year's Sky-Text/Object pattern samplers (1:1 with FireworkPatterns.js)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string_view QueryFireworkShapeLabel(FireworkShapeCategory Shape) noexcept;

/// Generates deterministic 3D burst anchor targets for any of the 10 FireworkShapeCategory patterns (0..9).
[[nodiscard]] std::vector<ParticleAnchorSample> SampleFireworkPatternAnchors(
    const FireworkOrigin& Origin,
    uint32_t              SampleCount,
    uint32_t              FinaleCycleIndex = 0u,
    uint32_t              Seed             = 2026u);

} // namespace Frontier::ParticleSpace
