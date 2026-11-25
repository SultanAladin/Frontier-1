/*============================================================================================================================================
                                                      SURFACEQUADCAGEEMBEDDER.H
============================================================================================================================================*/
// 🧩 Phase D04 Bilinear quad-cage + signed normal extrusion embedder (Pneumatic XPBD shell -> shared high-poly surface)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                SURFACE QUAD CAGE EMBEDDER
//------------------------------------------------------------------------------------------------------------------------

class SurfaceQuadCageEmbedder
{
public:
    /// Bakes bilinear quad weights, tangent-basis rest normals (nT, nB, nN), and signed height offset hOff
    /// onto `body.PneumaticTires[0]`.
    [[nodiscard]] static CageEmbeddedSurface EmbedIntoPneumaticCarcass(
        const SimulationSpace::Deformable::DeformableBody& body,
        const HighPolySurfaceRest& restSurface);

    /// Evaluates live deformed world positions, tangent-basis normals, and radial deflection strain on CPU
    /// (bit-identical to DeformableCageSkinning.slang Mode 1).
    static void EvaluateLiveSurface(
        const SimulationSpace::Deformable::DeformableBody& body,
        const CageEmbeddedSurface& embedded,
        DeformedSurfaceSample& outSample);
};

} // namespace Frontier::GeometrySpace::Deformation
