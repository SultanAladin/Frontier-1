/*============================================================================================================================================
                                                          DIFFERENTIALWORLDENCODER.H
============================================================================================================================================*/
// 🧩 Sector-relative 24-bit sub-millimeter quantization, smallest-three 32-bit quaternions, baseline delta encoding, and slerp windowing

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr float    WorldSectorSpanMeters         = 256.0f;  // [m]     - Width of one 16-bit sector cell
inline constexpr float    MaxQuantizedLinearSpeedMps    = 250.0f;  // [m/s]   - Symmetric clamp for 16-bit linear velocity
inline constexpr float    MaxQuantizedAngularSpeedRadPs = 32.0f;   // [rad/s] - Symmetric clamp for 16-bit angular velocity
inline constexpr uint32_t MaxSnapshotHistoryDepth       = 16u;     // [count] - Bounded interpolation snapshot history

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct UnitQuaternion
{
    float X = 0.0f; // [-] - Imaginary i component
    float Y = 0.0f; // [-] - Imaginary j component
    float Z = 0.0f; // [-] - Imaginary k component
    float W = 1.0f; // [-] - Real scalar component
};

struct ReplicatedEntityKinematics
{
    uint32_t            EntityIdentifier           = 0u;   // [-]     - Non-zero replicated entity id
    Mathematics::Axis3D PositionMeters             {};     // [m]     - Right-handed Z-up world coordinates
    UnitQuaternion      Orientation                {};     // [-]     - Unit orientation quaternion
    Mathematics::Axis3D LinearVelocityMetersPerSec {};     // [m/s]   - World-space linear velocity
    Mathematics::Axis3D AngularVelocityRadPerSec   {};     // [rad/s] - Body/world angular velocity
};

struct QuantizedSpatialRecord
{
    uint32_t               EntityIdentifier        = 0u;       // [-] - Entity id
    std::array<int16_t, 3> SectorIndices           { 0, 0, 0 };// [-] - 256 m sector coordinates (X, Y, Z)
    std::array<int32_t, 3> LocalSigned24           { 0, 0, 0 };// [-] - 24-bit local coordinates in [-8388607, +8388607]
    uint32_t               SmallestThreeQuaternion = 0u;       // [-] - 2-bit index + 3x10-bit quaternion components
    std::array<int16_t, 3> LinearVelocitySigned16  { 0, 0, 0 };// [-] - Quantized linear velocity
    std::array<int16_t, 3> AngularVelocitySigned16 { 0, 0, 0 };// [-] - Quantized angular velocity

    [[nodiscard]] bool operator==(const QuantizedSpatialRecord&) const noexcept = default;
};

struct WorldSnapshotView
{
    uint32_t                              SimulationTick    = 0u;  // [-] - Authoritative tick index
    double                                TimestampSeconds  = 0.0; // [s] - Authoritative simulation timestamp
    std::vector<ReplicatedEntityKinematics> Entities        {};    // [-] - Sorted or unsorted entity kinematics
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t                   PackSmallestThreeQuaternion(const UnitQuaternion& Orientation) noexcept;
[[nodiscard]] UnitQuaternion             UnpackSmallestThreeQuaternion(uint32_t PackedQuaternion) noexcept;
[[nodiscard]] UnitQuaternion             SlerpUnitQuaternion(const UnitQuaternion& Start, const UnitQuaternion& Target, float Alpha) noexcept;

[[nodiscard]] QuantizedSpatialRecord     QuantizeEntityKinematics(const ReplicatedEntityKinematics& Entity) noexcept;
[[nodiscard]] ReplicatedEntityKinematics DequantizeEntityKinematics(const QuantizedSpatialRecord& Quantized) noexcept;

[[nodiscard]] bool                       EncodeWorldSnapshotDelta(
    uint32_t                                    BaselineTick,
    std::span<const ReplicatedEntityKinematics> BaselineEntities,
    uint32_t                                    TargetTick,
    std::span<const ReplicatedEntityKinematics> TargetEntities,
    std::vector<uint8_t>&                       OutDeltaBytes,
    uint32_t&                                   OutChangedEntityCount);

[[nodiscard]] bool                       DecodeWorldSnapshotDelta(
    std::span<const ReplicatedEntityKinematics> BaselineEntities,
    std::span<const uint8_t>                    DeltaBytes,
    uint32_t&                                   OutBaselineTick,
    uint32_t&                                   OutTargetTick,
    std::vector<ReplicatedEntityKinematics>&    OutReconstructedEntities);

[[nodiscard]] uint64_t                   ComputeWorldAlignmentDigest(std::span<const ReplicatedEntityKinematics> Entities) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class SpatialInterpolationWindow final
{
public:
    SpatialInterpolationWindow() = default;

    void               PushAuthoritativeSnapshot(const WorldSnapshotView& Snapshot);
    void               ClearHistory() noexcept;

    [[nodiscard]] bool SampleInterpolatedEntities(
        double                                   RenderTimestampSeconds,
        std::vector<ReplicatedEntityKinematics>& OutInterpolatedEntities) const;

    [[nodiscard]] uint32_t QueryBufferedSnapshotCount() const noexcept { return static_cast<uint32_t>(History.size()); }

private:
    std::vector<WorldSnapshotView> History {};
};

} // namespace Frontier::NetworkingSpace
