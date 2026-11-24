/*============================================================================================================================================
                                                      VERTEXMOTIONATLASBAKER.H
============================================================================================================================================*/
// 🧩 Phase D05 Offscreen deterministic VAT baker running 3D FEM or Pneumatic XPBD into 256-col tiled RGBA32F textures
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  VERTEX MOTION ATLAS BAKER
//------------------------------------------------------------------------------------------------------------------------

class VertexMotionAtlasBaker
{
public:
    /// Bakes a complete `RGBA32F` Vertex Motion Atlas (world positions + 3D strain + Nanson/quad normals + rigid axle poses)
    /// from `body` and `embeddedSurface`. Preserves `body` state cleanly via snapshot-restore.
    [[nodiscard]] static VertexMotionAtlas BakeFromDeformable(
        SimulationSpace::Deformable::DeformableBody& body,
        const SimulationSpace::Deformable::DeformableSimulationConfiguration& configuration,
        const CageEmbeddedSurface& embeddedSurface,
        const VertexMotionAtlasBakeSettings& settings = {});
};

} // namespace Frontier::GeometrySpace::Deformation
