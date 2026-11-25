/*============================================================================================================================================
                                                    DEFORMABLESURFACEBUILDER.H
============================================================================================================================================*/
// 🧩 Phase D13 Unified C++20 procedural 3D triangle surface generator for ALL 9 deformable presets:
//    swept parallel-transport tubes, interlocking chain links, arched foliage blades, beads, tear-aware cloth, rims & props
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D13)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                             UNIFIED PROCEDURAL RENDER SURFACE
//------------------------------------------------------------------------------------------------------------------------

struct ProceduralRenderSurface
{
    std::vector<float>    Positions   {}; // 3 * VertexCount [m]
    std::vector<float>    Normals     {}; // 3 * VertexCount [-] (unit length)
    std::vector<float>    AlbedoRgb   {}; // 3 * VertexCount [-]
    std::vector<float>    MaterialPbr {}; // 3 * VertexCount (roughness, sss/emissive, metallic)
    std::vector<float>    Strain      {}; // VertexCount [-]
    std::vector<uint32_t> Indices     {}; // 3 * TriangleCount

    [[nodiscard]] uint32_t VertexCount() const noexcept { return static_cast<uint32_t>(Positions.size() / 3u); }
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return static_cast<uint32_t>(Indices.size() / 3u); }

    void Clear() noexcept
    {
        Positions.clear();
        Normals.clear();
        AlbedoRgb.clear();
        MaterialPbr.clear();
        Strain.clear();
        Indices.clear();
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  DEFORMABLE SURFACE BUILDER
//------------------------------------------------------------------------------------------------------------------------

class DeformableSurfaceBuilder
{
public:
    /// Builds all non-tire procedural attachments (tubes, chain links, foliage blades, beads, tear-aware cloth,
    /// forged alloy wheel rim for tire presets, static props, and optional collider sphere) into `outSurface`.
    static void AppendBodyAttachments(
        const SimulationSpace::Deformable::DeformableBody& body,
        ProceduralRenderSurface& outSurface,
        bool includeStaticProps = true,
        bool includeColliderSphere = true);

    /// Appends an already-skinned DeformedSurfaceSample + its HighPolySurfaceRest topology (e.g. sculpted sport tire)
    /// into a ProceduralRenderSurface so tires and rims render together in a unified draw list.
    static void AppendSkinnedSurface(
        const HighPolySurfaceRest& restSurface,
        const DeformedSurfaceSample& deformedSample,
        ProceduralRenderSurface& outSurface);

    /// Builds a forged 5-spoke metallic alloy wheel rim centered at (axleX, axleY, axleZ) rotated by spinAngleRad.
    static void AppendForgedAlloyRim(
        ProceduralRenderSurface& outSurface,
        float axleX,
        float axleY,
        float axleZ,
        float spinAngleRad,
        float rimRadius,
        float beadHalfWidthZ);
};

} // namespace Frontier::GeometrySpace::Deformation
