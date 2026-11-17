/*============================================================================================================================================
                                                      PARAMETRICPOLYGONBRIDGE.H
============================================================================================================================================*/
// 🧩 Bridge converting Parametric B-rep tessellations (PolygonComplex / ModelPartition) into Frontier::GeometrySpace::PolygonNetwork

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <string_view>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Converts a watertight parametric PolygonComplex into a half-edge PolygonNetwork preserving B-rep face partitions and chart UVs.
[[nodiscard]] bool ConvertPolygonComplexToNetwork(
    const PolygonComplex& Complex,
    std::string_view      NetworkTitle,
    PolygonNetwork&       OutNetwork);

// Tessellates Body within Partition and converts it into a Frontier PolygonNetwork (returns false with OutRefusal on refusal).
[[nodiscard]] bool TessellateBodyToPolygonNetwork(
    const ModelPartition&       Partition,
    BodyToken                   Body,
    const TessellationSettings& Settings,
    std::string_view            NetworkTitle,
    PolygonNetwork&             OutNetwork,
    std::string*                OutRefusal = nullptr);

} // namespace Frontier::GeometrySpace::Parametric
