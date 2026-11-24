/*============================================================================================================================================
                                                         TIRESURFACEBUILDER.H
============================================================================================================================================*/
// 🧩 Phase D04 Shared 7,552-vertex / 12,544-triangle sculpted automotive sport tire surface generator (used by FEM, XPBD & VAT)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TIRE SURFACE BUILDER
//------------------------------------------------------------------------------------------------------------------------

class TireSurfaceBuilder
{
public:
    /// Constructs the canonical 7,552-vertex / 12,544-triangle sculpted sport tire surface:
    /// 128 sectors x 35 cross-section profile stations (rim guard, sidewall ribs, 4 rain grooves, 5 tread ribs)
    /// + 320 beveled directional chevron tread lugs + 64 sidewall radial grip pads.
    [[nodiscard]] static HighPolySurfaceRest ConstructSportTireSurface(
        float axleX,
        float axleY,
        float axleZ,
        float rimRadius,
        float outerRadius,
        float halfWidth);
};

} // namespace Frontier::GeometrySpace::Deformation
