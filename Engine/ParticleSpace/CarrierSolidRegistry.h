/*============================================================================================================================================
                                                        CARRIERSOLIDREGISTRY.H
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 procedural 3D carrier solids (18 built-in + CustomObject import), OBJ/STL/FCT1 loaders, RGBA32F texel packing & anchor sampling
// Reference: Experimental/FluidEditor/Engine/ObjectMeshCatalog.js L1-L1167

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleCloudTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kMaximumSolidTriangles = 120000u; // [-] - Hard triangle cap per solid (ObjectMeshCatalog.js L36)
inline constexpr uint32_t kMaximumAnchorCount    = 16384u;  // [-] - Hard anchor cap per cloud (ObjectMeshCatalog.js L37)
inline constexpr uint32_t kAnchorTexelWidth      = 256u;    // [-] - Packed RGBA32F anchor texture width (L38)

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SolidCreaseSegment
{
    std::array<float, 3> A { 0.0f, 0.0f, 0.0f }; // [-] - Normalized start endpoint
    std::array<float, 3> B { 0.0f, 0.0f, 0.0f }; // [-] - Normalized end endpoint
};

struct SolidBounds
{
    std::array<double, 3> Minimum { 0.0, 0.0, 0.0 }; // [-] - Raw pre-normalization min corner
    std::array<double, 3> Maximum { 0.0, 0.0, 0.0 }; // [-] - Raw pre-normalization max corner
    std::array<double, 3> Centre  { 0.0, 0.0, 0.0 }; // [-] - Raw centroid
    double                Radius  = 1.0;             // [-] - Raw half-extent before normalization
};

struct CarrierSolidRecord
{
    std::string                     Name          {};   // [-] - Canonical solid name
    std::vector<float>              Corners       {};   // [-] - Normalized corner positions (Triangles * 9)
    std::vector<float>              Normals       {};   // [-] - Per-corner face normals (Triangles * 9)
    std::vector<float>              Areas         {};   // [-] - Per-triangle area (Triangles)
    std::vector<float>              RidgeWeights  {};   // [-] - Area * (1 + creaseCount * 1.6)
    std::vector<float>              CreaseFactors {};   // [-] - min(1, creaseCount * 0.5)
    std::vector<SolidCreaseSegment> Creases       {};   // [-] - Detected sharp crease edges (<= 4096)
    double                          TotalArea     = 0.0;// [-] - Sum of normalized triangle areas
    SolidBounds                     Bounds        {};   // [-] - Raw bounds prior to unit normalization
    float                           Radius        = 1.0f;
    uint32_t                        Triangles     = 0u; // [-] - Triangle count
    uint32_t                        VertexCount   = 0u; // [-] - Triangles * 3
};

struct SolidTexelTable
{
    std::vector<float> Data          {}; // [-] - RGBA32F packed texels (Width * Height * 4)
    uint32_t           Width         = 1u;
    uint32_t           Height        = 1u;
    uint32_t           TriangleCount = 0u;
    uint32_t           VertexCount   = 0u;
};

struct SolidAnchorSet
{
    std::vector<float> Positions {}; // [-] - Count * 3 xyz positions
    std::vector<float> Normals   {}; // [-] - Count * 3 unit normals
    std::vector<float> Ridges    {}; // [-] - Count ridge flags (0 or 1)
    uint32_t           Count     = 0u;
};

struct AnchorTexelTable
{
    std::vector<float> Data   {}; // [-] - RGBA32F packed texels (Width * Height * 4)
    uint32_t           Width  = kAnchorTexelWidth;
    uint32_t           Height = 1u;
    uint32_t           Count  = 0u;
};

struct CustomSolidEntry
{
    std::string        Id            {}; // [-] - Registry handle ("solid-1", ...)
    std::string        Name          {}; // [-] - Display label
    CarrierSolidRecord Solid         {}; // [-] - Finalized normalized solid
    uint32_t           TriangleCount = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               BUILT-IN SOLID QUERIES
//------------------------------------------------------------------------------------------------------------------------

/// Returns all 19 selectable solid names (18 built-in + "CustomObject") in exact reference order.
[[nodiscard]] std::span<const std::string_view> QueryAllSolidNames() noexcept;

/// Returns all 18 procedural built-in solid names ("Cube".."FishBody").
[[nodiscard]] std::span<const std::string_view> QueryBuiltInSolidNames() noexcept;

/// Returns the user-facing display label for any solid token.
[[nodiscard]] std::string_view QuerySolidDisplayLabel(std::string_view solidName) noexcept;

/// Builds and returns a normalized procedural built-in solid (falls back to "UtahTeapot" if unknown).
[[nodiscard]] const CarrierSolidRecord& ResolveBuiltInSolid(std::string_view solidName);

//------------------------------------------------------------------------------------------------------------------------
//                                           IMPORT, PACKING & SAMPLING
//------------------------------------------------------------------------------------------------------------------------

/// Parses Wavefront .obj text into a normalized CarrierSolidRecord (ObjectMeshCatalog.js L825-L858).
[[nodiscard]] std::optional<CarrierSolidRecord> ParseObjSolid(std::string_view objText, std::string_view name = "Imported OBJ");

/// Parses ASCII or binary STL bytes into a normalized CarrierSolidRecord (ObjectMeshCatalog.js L861-L893).
[[nodiscard]] std::optional<CarrierSolidRecord> ParseStlSolid(std::span<const uint8_t> rawBytes, std::string_view name = "Imported STL");

/// Parses Frontier binary FCT1 triangle stream into a normalized CarrierSolidRecord (ObjectMeshCatalog.js L926-L961).
[[nodiscard]] std::optional<CarrierSolidRecord> ParseBinaryFct1Solid(std::span<const uint8_t> rawBytes, std::string_view name = "Imported FCT1");

/// Packs a CarrierSolidRecord into 2 RGBA32F texels per triangle corner (ObjectMeshCatalog.js L986-L1014).
[[nodiscard]] SolidTexelTable PackSolidTexels(const CarrierSolidRecord& solid);

/// Distributes `count` anchors over `solid` with EvenSurface, SurfaceVolume, or RidgeAccent (ObjectMeshCatalog.js L1058-L1118).
[[nodiscard]] SolidAnchorSet SampleSolidAnchors(
    const CarrierSolidRecord& solid,
    uint32_t count,
    SolidSampleModeOrdinal mode = SolidSampleModeOrdinal::EvenSurface,
    double yawDegrees = 0.0);

/// Packs sampled anchors into the 2-texel-per-anchor RGBA32F layout (ObjectMeshCatalog.js L1121-L1139).
[[nodiscard]] AnchorTexelTable PackAnchorTexels(const SolidAnchorSet& sampled);

//------------------------------------------------------------------------------------------------------------------------
//                                                 CARRIERSOLIDREGISTRY
//------------------------------------------------------------------------------------------------------------------------

class CarrierSolidRegistry
{
public:
    CarrierSolidRegistry() noexcept = default;
    ~CarrierSolidRegistry() noexcept = default;

    void ClearCustomSolids() noexcept;

    [[nodiscard]] std::string RegisterCustomSolid(std::string_view name, CarrierSolidRecord solid);
    [[nodiscard]] std::optional<std::string> ImportCustomSolid(std::string_view fileName, std::span<const uint8_t> rawBytes);
    [[nodiscard]] const CustomSolidEntry* FindCustomSolid(std::string_view customId) const noexcept;
    [[nodiscard]] const CarrierSolidRecord& ResolveSolid(std::string_view solidName, std::string_view customId = {}) const;

private:
    std::vector<CustomSolidEntry> CustomEntries {};
    uint32_t                      NextSequence  = 1u;
};

} // namespace Frontier::ParticleSpace
