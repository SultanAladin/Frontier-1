/*============================================================================================================================================
                                                        LOCALSIGNEDDISTANCEVOLUME.H
============================================================================================================================================*/
// 🧩 Order 2 GtxLowEnd-ONLY per-asset compact 48^3 distance volume with C0-continuous Euclidean outer-box distance clamping

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ClosedOccluderBox
{
    Mathematics::Axis3D CenterWorld {};
    Mathematics::Axis3D HalfExtent  {};

    [[nodiscard]] float EvaluateSignedDistance(Mathematics::Axis3D WorldPos) const noexcept;
};

struct RigidLocalVolumePlacement
{
    const class LocalSignedDistanceVolume* VolumeAsset  = nullptr;
    Mathematics::Axis3D                    Translation  { 0.0f, 0.0f, 0.0f };
    float                                  CosineYaw    = 1.0f;
    float                                  SineYaw      = 0.0f;
    bool                                   ActiveToggle = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             LOCAL SIGNED DISTANCE VOLUME
//------------------------------------------------------------------------------------------------------------------------

class LocalSignedDistanceVolume
{
public:
    uint32_t            ResolutionX     = 0u;
    uint32_t            ResolutionY     = 0u;
    uint32_t            ResolutionZ     = 0u;
    Mathematics::Axis3D LocalMin        {};
    Mathematics::Axis3D LocalMax        {};
    Mathematics::Axis3D VoxelSpacing    {};
    std::vector<uint16_t> HalfDistanceSamples {}; // Compact IEEE-754 binary16 distance samples (80x40x32 = 204,800 bytes << 3.7 MB budget)
    std::vector<float>    DistanceSamples     {}; // Mirror float32 slice for direct GPU structured buffer upload when active

    void Reset() noexcept;

    // Bakes a conservative unsigned/C0-clamped local distance volume around InputPrimitives (e.g. 48^3 ControlVehicle)
    bool BakeFromTriangles(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
        uint32_t                                                   CubicResolution = 48u,
        float                                                      MarginMeters    = 0.24f);

    // Aspect-matched bake (e.g. 80x40x32 ControlVehicle at ~6.0 cm isotropic pitch) for near-isotropic voxel spacing
    bool BakeFromTrianglesAspect(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
        uint32_t                                                   ResX,
        uint32_t                                                   ResY,
        uint32_t                                                   ResZ,
        float                                                      MarginMeters = 0.24f);

    // Trilinear lookup inside [LocalMin, LocalMax]
    [[nodiscard]] float SampleTrilinearLocal(Mathematics::Axis3D ClampedLocalPos) const noexcept;

    // C0-continuous world-space distance via Euclidean outer-box clamp hypot(BoundaryD, BoxD) (eliminates AABB seam!)
    [[nodiscard]] static float SampleSmoothWorldDistance(
        const RigidLocalVolumePlacement& Placement,
        Mathematics::Axis3D              WorldPos) noexcept;

    [[nodiscard]] std::size_t QueryCompactMemoryBytes() const noexcept
    {
        return HalfDistanceSamples.size() * sizeof(uint16_t);
    }
};

} // namespace Frontier::DistanceFieldSpace
