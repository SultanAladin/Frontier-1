/*============================================================================================================================================
                                                      VERTEXMOTIONATLASTYPES.H
============================================================================================================================================*/
// 🧩 Phase D05 256-column tiled RGBA32F/RGBA16F Vertex Motion Atlas (VAT) storage, bounds, per-slice axle poses & GPU playback UBO
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU ABI UNIFORM BLOCK
//------------------------------------------------------------------------------------------------------------------------

/// Exact 64-byte uniform block matching Engine/Shaders/Deformable/DeformableLayouts.slang::VertexMotionAtlasUniforms.
struct alignas(16) VertexMotionAtlasUniforms
{
    std::array<uint32_t, 4> PackingSlices   { 256u, 30u, 0u, 1u };          // x=texWidth, y=rowsPerSlice, z=slice0, w=slice1
    std::array<float, 4>    BlendFlags      { 0.0f, 0.0f, 0.0f, 0.0f };     // x=sliceFrac, y=deformMode(0/1/2), z=showHeatmap, w=pad
    std::array<float, 4>    BoundsMinPad    { 0.0f, 0.0f, 0.0f, 0.0f };     // xyz=displacement min bounds
    std::array<float, 4>    BoundsMaxPad    { 0.0f, 0.0f, 0.0f, 0.0f };     // xyz=displacement max bounds
};
static_assert(sizeof(VertexMotionAtlasUniforms) == 64u, "VertexMotionAtlasUniforms must remain 64 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  BAKED ATLAS CONTAINERS
//------------------------------------------------------------------------------------------------------------------------

struct RigidSlicePose
{
    float AxleX        = 0.0f;
    float AxleY        = -0.62f;
    float SpinAngleRad = 0.0f;
    float ColliderX    = 1.55f;
    float ColliderY    = -1.62f;
    float ColliderZ    = 0.0f;
};

struct VertexMotionAtlas
{
    static constexpr uint32_t kDefaultAtlasWidth = 256u;

    std::string                 SolverLabel     { "3D Neo-Hookean FEM" };
    uint32_t                    VertexCount     = 0u;
    uint32_t                    TriangleCount   = 0u;
    uint32_t                    SliceCount      = 64u;
    uint32_t                    AtlasWidth      = kDefaultAtlasWidth;
    uint32_t                    RowsPerSlice    = 0u;
    uint32_t                    AtlasHeight     = 0u;
    float                       SlicesPerSecond = 30.0f;
    std::array<float, 3>        DisplacementMin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>        DisplacementMax { 0.0f, 0.0f, 0.0f };

    // Interleaved RGBA32F texels (size = AtlasWidth * AtlasHeight * 4)
    // PositionStrainRgba: rgb = world position [m], a = 3D strain [-]
    // NormalRgba:         rgb = world unit normal,  a = 0
    std::vector<float>          PositionStrainRgba {};
    std::vector<float>          NormalRgba         {};
    std::vector<RigidSlicePose> SlicePoses         {};

    [[nodiscard]] bool Ready() const noexcept
    {
        return VertexCount > 0u && SliceCount > 0u && !PositionStrainRgba.empty() && SlicePoses.size() == SliceCount;
    }

    [[nodiscard]] size_t TexelOffset(uint32_t vertexOrdinal, uint32_t sliceOrdinal) const noexcept
    {
        const uint32_t col = vertexOrdinal & (AtlasWidth - 1u);
        const uint32_t row = (vertexOrdinal / AtlasWidth) + sliceOrdinal * RowsPerSlice;
        return (static_cast<size_t>(row) * AtlasWidth + col) * 4u;
    }
};

struct VertexMotionAtlasBakeSettings
{
    uint32_t SliceCount          = 64u;
    uint32_t SimStepsPerSlice    = 2u;
    float    StepDeltaSeconds    = 1.0f / 60.0f;
    float    MinimumSpinRadSec   = 5.5f;
    float    CurbSweepHalfSpanX  = 1.85f;
    float    CurbHeightAboveFloor = 0.17f;
    bool     EnableCurbSweep     = true;
};

} // namespace Frontier::GeometrySpace::Deformation
