//============================================================================================================================================
//                                                        SPATIALBASIS.H
//============================================================================================================================================
// 🧩 An orthonormal frame for a surface in world space. The basis is geometry vocabulary, not viewport
//    vocabulary: drawing, snapping, projection, and transforms can all consume it without depending on
//    the workspace's camera or workplane catalogue.

#pragma once

#include "SlateShape/Geometry/SpatialMeasure/Api/SpatialMeasure.h"

namespace Slate
{

/// 🧩 A point on a surface and the three directions that orient it.
/// note ⚠️ `Across` is derived by the owner from `Normal` and `Along`; callers should provide an
///    orthonormal frame so every planar operation measures the same surface.
struct SpatialBasis
{
    SpatialPoint     Origin = {};
    SpatialDirection Along  = { 1.0, 0.0, 0.0 };
    SpatialDirection Across = { 0.0, 0.0, 1.0 };
    SpatialDirection Normal  = { 0.0, 1.0, 0.0 };
};

} // namespace Slate
