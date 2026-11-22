/*============================================================================================================================================
                                                        LOCALSIGNEDDISTANCEVOLUME.H
============================================================================================================================================*/
// 🧩 Per-asset compact local distance & surface-material volume with C0-continuous Euclidean outer-box clamping and rigid 3D placement

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
    const class LocalSignedDistanceVolume* VolumeAsset          = nullptr;
    Mathematics::Axis3D                    Translation          { 0.0f, 0.0f, 0.0f };
    float                                  CosineYaw            = 1.0f;
    float                                  SineYaw              = 0.0f;
    bool                                   ActiveToggle         = true;
    float                                  UniformScale         = 1.0f;
    uint16_t                               MaterialSlotOverride = 0xFFFFu;
    bool                                   FullMatrixValid      = false;
    Mathematics::Axis3D                    WorldToLocalRow0     { 1.0f, 0.0f, 0.0f };
    Mathematics::Axis3D                    WorldToLocalRow1     { 0.0f, 1.0f, 0.0f };
    Mathematics::Axis3D                    WorldToLocalRow2     { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D                    LocalToWorldRow0     { 1.0f, 0.0f, 0.0f };
    Mathematics::Axis3D                    LocalToWorldRow1     { 0.0f, 1.0f, 0.0f };
    Mathematics::Axis3D                    LocalToWorldRow2     { 0.0f, 0.0f, 1.0f };

    void ConfigureRigidTransform(
        const class LocalSignedDistanceVolume* Asset,
        Mathematics::Axis3D                    WorldPosition,
        Mathematics::Axis3D                    EulerRotationDegrees,
        float                                  Scale           = 1.0f,
        uint16_t                               OverrideMatSlot = 0xFFFFu) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             LOCAL SIGNED DISTANCE VOLUME
//------------------------------------------------------------------------------------------------------------------------

class LocalSignedDistanceVolume
{
public:
    uint32_t              ResolutionX                 = 0u;
    uint32_t              ResolutionY                 = 0u;
    uint32_t              ResolutionZ                 = 0u;
    uint32_t              UniqueAssetSerial           = 0u;
    Mathematics::Axis3D   LocalMin                    {};
    Mathematics::Axis3D   LocalMax                    {};
    Mathematics::Axis3D   VoxelSpacing                {};
    std::vector<uint16_t> HalfDistanceSamples         {}; // Compact IEEE-754 binary16 distance samples
    std::vector<float>    DistanceSamples             {}; // Mirror float32 slice for direct GPU structured buffer upload
    std::vector<uint32_t> PackedNormalMaterialSamples {}; // Packed [9:0]=OctX, [19:10]=OctY, [31:20]=MaterialOrdinal

    void Reset() noexcept;

    bool BakeFromTriangles(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
        uint32_t                                                   CubicResolution = 48u,
        float                                                      MarginMeters    = 0.24f);

    bool BakeFromTrianglesAspect(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
        uint32_t                                                   ResX,
        uint32_t                                                   ResY,
        uint32_t                                                   ResZ,
        float                                                      MarginMeters = 0.24f);

    [[nodiscard]] float SampleTrilinearLocal(Mathematics::Axis3D ClampedLocalPos) const noexcept;

    [[nodiscard]] static float SampleSmoothWorldDistance(
        const RigidLocalVolumePlacement& Placement,
        Mathematics::Axis3D              WorldPos) noexcept;

    [[nodiscard]] std::size_t QueryCompactMemoryBytes() const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline std::size_t LocalSignedDistanceVolume::QueryCompactMemoryBytes() const noexcept
{
    return HalfDistanceSamples.size() * sizeof(uint16_t);
}

} // namespace Frontier::DistanceFieldSpace
