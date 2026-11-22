/*====================================================================================================================================
                                                           REPLAYDIGEST.H
====================================================================================================================================*/
// 🧩 Canonical step digests for incremental replay: equal digests, equal replay inputs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Feature/FeatureTree.h"

#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Every field of Step that the replay reads, in a fixed byte order (doubles bit-exact), followed by the current value of
//    each parameter its values and sketch constraints bind. A SpaceCurve also carries every Mirror plane of the tree (an
//    end on one snaps onto it), the only input a step reads from later steps. Two steps with equal digests replay
//    identically after equal earlier steps. Captured reference signatures are included (they steer geometric rebinding).
[[nodiscard]] std::string DigestFeatureStep(const FeatureTree& Tree, const FeatureStep& Step);
[[nodiscard]] std::string DigestModifiers(const FeatureTree& Tree);

} // namespace Frontier::GeometrySpace::Parametric
