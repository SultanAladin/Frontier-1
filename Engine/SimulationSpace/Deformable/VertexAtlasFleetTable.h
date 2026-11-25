/*============================================================================================================================================
                                                   VERTEXATLASFLEETTABLE.H
============================================================================================================================================*/
// 🧩 Phase M02: Zero-allocation .VertexAtlas multi-clip fleet instancing table managing up to 16 baked .VertexAtlas
//    clips and 256 world instances with distance-based LOD culling, contiguous 64B GPU upload span, and CPU vertex probing.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                          GPU INSTANCE RECORD & FLEET REPORT
//------------------------------------------------------------------------------------------------------------------------

/// Exact 64-byte std430-compatible GPU instance record for single-draw VAT fleet rendering.
struct alignas(16) VertexAtlasInstanceGpuRecord
{
    std::array<float, 4>    WorldPosAndScale   { 0.0f, 0.0f, 0.0f, 1.0f }; // xyz = world origin [m], w = uniform scale
    std::array<float, 4>    RotationAndPhase   { 0.0f, 0.0f, 0.0f, 0.0f }; // x = yaw, y = pitch, z = spinRad, w = playbackRate
    std::array<uint32_t, 4> ClipSliceAndLod    { 0u, 0u, 1u, 0u };         // x = clipSlot, y = slice0, z = slice1, w = lodTier(0=Full,1=Half,2=Culled)
    std::array<float, 4>    BlendAndStrain     { 0.0f, 1.0f, 0.0f, 0.0f }; // x = sliceFrac, y = deformWeight, z = distMeters, w = pad
};
static_assert(sizeof(VertexAtlasInstanceGpuRecord) == 64u, "VertexAtlasInstanceGpuRecord must remain 64 bytes");

struct VertexAtlasFleetStepSummary
{
    uint32_t ActiveInstances = 0u;
    uint32_t FullRateCount   = 0u;
    uint32_t HalfRateCount   = 0u;
    uint32_t CulledCount     = 0u;
};

struct VertexAtlasSamplePoint
{
    std::array<float, 3> WorldPosMeters { 0.0f, 0.0f, 0.0f };
    float                Strain         = 0.0f;
    bool                 Valid          = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               VERTEX ATLAS FLEET TABLE
//------------------------------------------------------------------------------------------------------------------------

class VertexAtlasFleetTable
{
public:
    static constexpr uint32_t kMaxAtlasClips     = 16u;
    static constexpr uint32_t kMaxFleetInstances = 256u;

    /// Loads a shipped .VertexAtlas binary clip into the next free clip slot (setup-time allocation only).
    /// Returns clipSlot [0..15] or UINT32_MAX on error.
    [[nodiscard]] uint32_t RegisterClipFromFile(
        const std::filesystem::path& atlasPath,
        std::string* outError = nullptr);

    /// Registers an already-baked VertexMotionAtlas clip directly.
    [[nodiscard]] uint32_t RegisterClip(GeometrySpace::Deformation::VertexMotionAtlas clip);

    /// Spawns a world instance referencing `clipSlot` (0 heap allocations; returns instanceSlot or UINT32_MAX if full).
    [[nodiscard]] uint32_t SpawnInstance(
        uint32_t clipSlot,
        const std::array<float, 3>& worldPosMeters,
        float uniformScale = 1.0f,
        float playbackRate = 1.0f,
        float cullDistanceMeters = 80.0f) noexcept;

    /// Advances all fleet instance playback cursors, evaluates distance LOD tiers, and updates the contiguous GPU
    /// instance table with 0 heap allocations.
    [[nodiscard]] VertexAtlasFleetStepSummary StepFleet(
        float deltaTimeSeconds,
        const std::array<float, 3>& viewerPosMeters) noexcept;

    /// Samples an instance's linearly interpolated deformed world vertex position & strain at its live slice cursor (0 allocs).
    [[nodiscard]] VertexAtlasSamplePoint SampleInstanceVertexMeters(
        uint32_t instanceSlot,
        uint32_t vertexOrdinal) const noexcept;

    /// Contiguous 64B-aligned GPU upload view covering [0..ActiveInstanceCount).
    [[nodiscard]] std::span<const VertexAtlasInstanceGpuRecord> InspectGpuUploadSpan() const noexcept
    {
        return { GpuRecords_.data(), InstanceCount_ };
    }

    [[nodiscard]] uint32_t QueryClipCount() const noexcept { return ClipCount_; }
    [[nodiscard]] uint32_t QueryInstanceCount() const noexcept { return InstanceCount_; }
    void ClearAll() noexcept;

private:
    std::array<GeometrySpace::Deformation::VertexMotionAtlas, kMaxAtlasClips> Clips_         {};
    std::array<VertexAtlasInstanceGpuRecord, kMaxFleetInstances>              GpuRecords_    {};
    std::array<float, kMaxFleetInstances>                                     SliceCursors_  {};
    std::array<float, kMaxFleetInstances>                                     CullDistances_ {};
    uint32_t                                                                  ClipCount_     = 0u;
    uint32_t                                                                  InstanceCount_ = 0u;
    uint64_t                                                                  TickCounter_   = 0ull;
};

} // namespace Frontier::SimulationSpace::Deformable
