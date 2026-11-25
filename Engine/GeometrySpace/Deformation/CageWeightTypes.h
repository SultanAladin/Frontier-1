/*============================================================================================================================================
                                                          CAGEWEIGHTTYPES.H
============================================================================================================================================*/
// 🧩 Phase D04 Solver-agnostic high-poly surface & 84-byte GPU cage-embedded vertex ABI (matches DeformableCageSkinning.slang)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §2 & §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class CageEmbedMode : uint8_t
{
    Tetrahedral3D = 0u, // [-] - 4-point 3D tetrahedral barycentric weights + Nanson cof(F) tet ordinal in Extra[0]
    SurfaceQuad2D = 1u  // [-] - 4-corner bilinear quad weights + local (nT, nB, nN) & signed height offset in Extra[0..3]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU VERTEX LAYOUT
//------------------------------------------------------------------------------------------------------------------------

/// Exact 84-byte (21 float) GPU vertex layout matching DeformableCageSkinning.slang and XpbdStrandStudio.
struct CageBoundVertex
{
    std::array<float, 3> RestNormal  { 0.0f, 1.0f, 0.0f };       // loc 0: rest sculpted unit normal
    std::array<float, 3> Albedo      { 0.12f, 0.12f, 0.13f };    // loc 1: base surface colour
    std::array<float, 3> MaterialPbr { 0.72f, 1.0f, 1.0f };      // loc 2: x=roughness, y=isTread, z=ambientOcclusion
    std::array<float, 4> CageIndices { 0.0f, 0.0f, 0.0f, 0.0f }; // loc 3: 4 cage point indices (encoded as float)
    std::array<float, 4> CageWeights { 1.0f, 0.0f, 0.0f, 0.0f }; // loc 4: 4 barycentric or bilinear weights
    std::array<float, 4> ExtraParams { 0.0f, 0.0f, 0.0f, 0.0f }; // loc 5: FEM=(tetIdx,0,0,0); XPBD=(nT,nB,nN,hOff)
};
static_assert(sizeof(CageBoundVertex) == 84u, "CageBoundVertex must remain exactly 84 bytes (21 floats).");

//------------------------------------------------------------------------------------------------------------------------
//                                              HIGH-POLY SURFACE CONTAINERS
//------------------------------------------------------------------------------------------------------------------------

struct HighPolySurfaceRest
{
    std::vector<float>    RestPositions {}; // 3 * VertexCount [m]
    std::vector<float>    RestNormals   {}; // 3 * VertexCount [-]
    std::vector<float>    AlbedoRgb     {}; // 3 * VertexCount [-]
    std::vector<float>    MaterialPbr   {}; // 3 * VertexCount (rough, isTread, ao)
    std::vector<uint32_t> Indices       {}; // 3 * TriangleCount

    [[nodiscard]] uint32_t VertexCount() const noexcept { return static_cast<uint32_t>(RestPositions.size() / 3u); }
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return static_cast<uint32_t>(Indices.size() / 3u); }
};

struct CageEmbeddedSurface
{
    CageEmbedMode                Mode         = CageEmbedMode::Tetrahedral3D;
    HighPolySurfaceRest          RestSurface  {};
    std::vector<CageBoundVertex> GpuVertices  {};
    std::vector<uint32_t>        CageIndices  {}; // 4 * VertexCount (integer copy for fast CPU evaluation)
    std::vector<float>           CageWeights  {}; // 4 * VertexCount
    std::vector<float>           ExtraParams  {}; // 4 * VertexCount
    float                        MaxRestError = 0.0f; // [m] diagnostic reconstruction accuracy
    float                        MaxAbsWeight = 0.0f; // [-] stability bound check

    [[nodiscard]] uint32_t VertexCount() const noexcept { return RestSurface.VertexCount(); }
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return RestSurface.TriangleCount(); }
};

struct DeformedSurfaceSample
{
    std::vector<float> Positions {}; // 3 * VertexCount [m]
    std::vector<float> Normals   {}; // 3 * VertexCount [-]
    std::vector<float> Strain    {}; // VertexCount [-]
};

} // namespace Frontier::GeometrySpace::Deformation
