/*============================================================================================================================================
                                                            SIGNEDDISTANCEVOLUME.H
============================================================================================================================================*/
// 🧩 Sparse 8x8x8 narrow-band brick 3D Signed Distance Volume for Tier 1 GTX solar cone penumbra and 6-cone diffuse GI

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t BrickEdgeVoxelCount  = 8u;   // [voxels] - 8x8x8 = 512 narrow-band samples per allocated brick
inline constexpr uint32_t BrickTotalVoxelCount = 512u; // [voxels] - BrickEdgeVoxelCount^3
inline constexpr uint32_t UnallocatedBrickSlot = 0xFFFFFFFFu; // [idx] - Far-field brick using coarse distance

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SparseDistanceBrick
{
    std::array<int16_t, BrickTotalVoxelCount> QuantizedSamples {}; // [snorm16] - Signed distance normalized by NarrowBandMeters
};

class SignedDistanceVolume
{
public:
    Mathematics::Axis3D              VolumeBoundsMin    {};     // [m]     - World-space padded AABB lower corner
    Mathematics::Axis3D              VolumeBoundsMax    {};     // [m]     - World-space padded AABB upper corner
    float                            VoxelSpacingMeters = 0.1f; // [m]     - Fine narrow-band voxel spacing
    float                            NarrowBandMeters   = 0.8f; // [m]     - Quantization half-range for int16_t brick samples
    uint32_t                         BrickCountX        = 0u;   // [count] - Coarse macro-brick grid resolution along X
    uint32_t                         BrickCountY        = 0u;   // [count] - Coarse macro-brick grid resolution along Y
    uint32_t                         BrickCountZ        = 0u;   // [count] - Coarse macro-brick grid resolution along Z
    std::vector<uint32_t>            BrickIndirection   {};     // [idx]   - Allocated brick slot or UnallocatedBrickSlot
    std::vector<float>               CoarseBrickCenterDistance {}; // [m]  - Signed center distance for unallocated far bricks
    std::vector<SparseDistanceBrick> AllocatedBricks    {};     // [-]     - Dense 8x8x8 narrow-band bricks near surface shells

    void Reset() noexcept;

    // Builds sparse narrow-band 3D SDF from a WideBoundingTree with automatic padding and brick pruning.
    bool BuildFromWideBoundingTree(
        const SpatialAcceleration::WideBoundingTree& Tree,
        float                                        TargetVoxelSpacingMeters,
        float                                        PaddingMarginMeters = 0.5f);

    // Evaluates continuous trilinear world-space signed distance (exact exterior Box-SDF + interior brick interpolation).
    [[nodiscard]] float SampleSignedDistance(Mathematics::Axis3D WorldPosition) const noexcept;

    // Evaluates unit central-difference SDF surface gradient (outward normal estimator).
    [[nodiscard]] Mathematics::Axis3D EvaluateSurfaceGradient(Mathematics::Axis3D WorldPosition) const noexcept;

    // Sphere-traces a soft shadow or ambient-occlusion cone through the SDF, returning [0..1] unoccluded visibility.
    [[nodiscard]] float TraceConeVisibility(
        Mathematics::Axis3D RayOrigin,
        Mathematics::Axis3D UnitDirection,
        float               MaximumTraceDistanceMeters,
        float               TanHalfConeAngle = 0.04f,
        uint32_t            MaxSphereSteps   = 48u) const noexcept;

    [[nodiscard]] std::size_t QueryOccupiedMemoryBytes() const noexcept;
};

} // namespace Frontier::DistanceFieldSpace
