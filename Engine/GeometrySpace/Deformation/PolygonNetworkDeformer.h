/*============================================================================================================================================
                                                     POLYGONNETWORKDEFORMER.H
============================================================================================================================================*/
// 🧩 Phase D09 Bidirectional Frontier PolygonNetwork <-> HighPolySurfaceRest / Live Cage / VAT deformation bridge
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D09)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <string>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  POLYGON NETWORK DEFORMER
//------------------------------------------------------------------------------------------------------------------------

class PolygonNetworkDeformer
{
public:
    /// Extracts an arbitrary Frontier PolygonNetwork (triangles, quads, or n-gons via fan triangulation)
    /// into a contiguous HighPolySurfaceRest ready for 3D Tetrahedral FEM or Quad-Cage embedding and VAT baking.
    [[nodiscard]] static HighPolySurfaceRest ExtractSurfaceRest(const PolygonNetwork& network);

    /// Constructs a valid half-edge Frontier PolygonNetwork (with linked opposite edges & smooth normals)
    /// from any HighPolySurfaceRest (such as TireSurfaceBuilder::ConstructSportTireSurface).
    [[nodiscard]] static PolygonNetwork BuildPolygonNetworkFromSurface(
        const HighPolySurfaceRest& surface,
        std::string networkTitle = "DeformableSurface");

    /// Applies a live cage-skinned or VAT-interpolated DeformedSurfaceSample directly onto a matching PolygonNetwork,
    /// updating every Vertex::Axes and Vertex::Normal and advancing NetworkRevision.
    [[nodiscard]] static bool ApplyDeformedSample(
        PolygonNetwork& targetNetwork,
        const DeformedSurfaceSample& deformedSample,
        bool recomputeFaceNormals = false) noexcept;
};

} // namespace Frontier::GeometrySpace::Deformation
