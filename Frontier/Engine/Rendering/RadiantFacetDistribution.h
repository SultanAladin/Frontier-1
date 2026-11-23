/*==========================================================================================================================================
                                                RADIANTFACETDISTRIBUTION.H
==========================================================================================================================================*/
// 🧩 Compile and validate a float32 area/luminance distribution for emissive triangle sampling.

#pragma once

#include "PathReplayFootprint.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   DISTRIBUTION CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 LightInfluence.slang reads a count followed by (facet index, probability, area, cumulative probability).
//    The 24-bit probability lattice reserves one tick per nondegenerate, luminous facet. This keeps arbitrarily
//    dim sources selectable, but changes their probability from the ideal area/luminance ratio by up to the
//    reserved-tick allocation. The compiler emits ascending facet indices; the generic validator below does not
//    enforce ordering or uniqueness. Geometric area is two-sided; this is not clustered source selection.
inline constexpr uint32_t RadiantFacetProbabilityTicks = 1u << 24;

[[nodiscard]] std::vector<float> CompileRadiantFacetDistribution(const std::vector<PathReplayFacet>& Facets);

// 📝 Validates a distribution embedded at Offset in a larger float atlas. A zero-count distribution is valid.
//    This validates the encoded shape, not that geometry or radiance still match the originating facets.
void ValidateRadiantFacetDistribution(const std::vector<float>& Atlas, size_t Offset, size_t FacetCount);

} // namespace Frontier
