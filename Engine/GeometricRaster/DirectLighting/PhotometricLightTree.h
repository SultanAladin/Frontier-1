/*============================================================================================================================================
                                                        PHOTOMETRICLIGHTTREE.H
============================================================================================================================================*/
// 🧩 Adaptive Conty Estevez & Kulla (2018) binary photometric (.ies) importance light tree and unified GPU light register packer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/PhotometricProfile.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t LightRegisterVectorStride  = 4u;          // [vec4f] - 4 float4 vectors per area light or practical lamp
inline constexpr uint32_t LightClusterVectorStride = 4u;          // [vec4f] - 4 float4 vectors per binary light-tree cluster entry
inline constexpr uint32_t LightClusterLeafBit      = 0x80000000u; // [bit]   - Marks a leaf cluster entry holding one lamp index
inline constexpr uint32_t AdaptiveLampCutCeiling   = 8u;          // [cnt]   - Maximum subtree cuts evaluated per shading point

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PracticalLampMode : uint8_t
{
    Disabled     = 0u, // Softbox rig / environment only
    VehicleLamps = 1u, // 2 low-beam headlamps + 2 tail lamps per parked vehicle (4..200 lamps)
    CarParkLamps = 2u, // Vehicle lamps + 28 overhead 3500 K aisle downlights (up to 228 lamps)
};

enum class LampSamplingRule : uint8_t
{
    SingleTree = 0u, // 1 importance-drawn lamp per shading vertex
    SplitFour  = 1u, // Adaptive light-tree cut <= 4 subtrees per shading vertex
    SplitEight = 2u, // Adaptive light-tree cut <= 8 subtrees per shading vertex
    AllLamps   = 3u, // Exact brute-force sum across every practical lamp (reference)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VehicleBayPlacement
{
    float    BayX          = 0.0f;    // [m]   - Studio X coordinate
    float    BayZ          = 0.0f;    // [m]   - Studio Z coordinate
    float    YawRadians    = 0.0f;    // [rad] - Rotation about studio +Y axis (0 or pi)
    uint16_t PaintSlotSwap = 0xFFFFu; // [idx] - Per-instance body finish slot (0xFFFF = hero BodyCoat)
    uint16_t RimSlotSwap   = 0xFFFFu; // [idx] - Per-instance rim finish slot (0xFFFF = hero RimAlloy)
};

struct PracticalLampPlacement
{
    std::array<float, 3> Position      { 0.0f, 0.0f, 0.0f }; // [m]   - Studio world position
    std::array<float, 3> Axis          { 0.0f, -1.0f, 0.0f }; // [-]  - Unit primary photometric beam axis (theta = 0)
    std::array<float, 3> Reference     { 1.0f, 0.0f, 0.0f }; // [-]   - Unit horizontal reference axis (phi = 0)
    std::array<float, 3> PeakIntensity { 0.0f, 0.0f, 0.0f }; // [lin] - RGB scene intensity at normalized profile peak
    uint32_t             ProfileIndex  = 0u;                 // [idx] - Index into resampled 128x32 .ies profile array
    float                FluxPower     = 0.0f;               // [lin] - Luminance-weighted total power (Luminance * NormalizedFlux)
    float                CutoffHalfAngle = 3.14159265f;        // [rad] - Maximum polar angle of non-zero candela (theta_e)
};

struct LightClusterEntry
{
    std::array<float, 3> LowerBounds   { 0.0f, 0.0f, 0.0f }; // [m]   - Cluster AABB minimum
    std::array<float, 3> UpperBounds   { 0.0f, 0.0f, 0.0f }; // [m]   - Cluster AABB maximum
    float                TotalPower    = 0.0f;               // [lin] - Summed FluxPower of nested sub-tree lamps
    std::array<float, 3> ConeAxis      { 0.0f, -1.0f, 0.0f }; // [-]  - Bounding beam axis cone unit vector
    float                ConeSpread    = 0.0f;               // [rad] - Half-angle of bounding axis cone (theta_o)
    float                CutoffHalfAngle = 0.0f;               // [rad] - Max polar cutoff half-angle (theta_e)
    int32_t              LeafLampIndex = -1;                 // [idx] - >= 0 for leaf cluster, -1 for internal cluster
    uint32_t             FirstNestedSlot = 0u;                 // [idx] - Breadth-first index of first nested branch (second = FirstNestedSlot + 1)
    uint32_t             TreeLevel     = 0u;                 // [-]   - Tree depth level from root (0)
};

struct PackedLightRegisterLayout
{
    uint32_t LampCount        = 0u; // [cnt]   - Practical .ies lamp count
    uint32_t ClusterSlotStart = 0u; // [vec4f] - Offset to first LightClusterEntry in LightRegister
    uint32_t LampSlotStart    = 0u; // [vec4f] - Offset to first PracticalLampPlacement in LightRegister
    uint32_t ProfileSlotStart = 0u; // [vec4f] - Offset to first 128x32 resampled .ies grid in LightRegister
    uint32_t ClusterCount     = 0u; // [cnt]   - Flattened binary cluster entry count
    uint32_t MaximumTreeDepth = 0u; // [-]     - Maximum depth of binary light tree
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolves deterministic 5x10 studio parking bays (up to 50 ControlVehicles, hero car at origin bay 0)
[[nodiscard]] std::vector<VehicleBayPlacement> ResolveStudioParkingBays(
    uint32_t VehicleCount,
    uint16_t FirstFleetPaintSlot,
    uint16_t FirstFleetRimSlot);

// Places practical .ies lamps across parked vehicles (headlamps + tail lamps) and optional 28 car-park ceiling downlights
[[nodiscard]] std::vector<PracticalLampPlacement> ConstructFleetPracticalLamps(
    PracticalLampMode                                    Mode,
    std::span<const VehicleBayPlacement>                 Bays,
    std::span<const Codecs::ResampledPhotometricProfile> Profiles,
    float                                                IntensityScale = 1.0f);

// Builds breadth-first flattened Conty Estevez & Kulla (2018) binary importance light tree
[[nodiscard]] std::vector<LightClusterEntry> ConstructPhotometricLightTree(
    std::span<const PracticalLampPlacement> Lamps,
    uint32_t*                               OutMaximumDepth = nullptr);

// Packs [AreaLights 4xA][PracticalLamps 4xL][LightClusterEntries 4xN][IesProfiles 1024xP] into one GPU float buffer
[[nodiscard]] std::vector<float> PackUnifiedLightRegister(
    std::span<const float>                               AreaPackedFloats,
    uint32_t                                             AreaLightCount,
    std::span<const PracticalLampPlacement>              Lamps,
    std::span<const LightClusterEntry>                   LightTree,
    std::span<const Codecs::ResampledPhotometricProfile> Profiles,
    PackedLightRegisterLayout*                             OutLayout = nullptr);

// Host replica of GPU adaptive light-tree cut (power * radius^2 / distance^4) for validation
[[nodiscard]] uint32_t ResolveAdaptiveLampCutHost(
    std::span<const LightClusterEntry>                   LightTree,
    const std::array<float, 3>&                          ShadingPosition,
    uint32_t                                             CutLimit,
    std::array<uint32_t, AdaptiveLampCutCeiling>&        OutClusterCut);

} // namespace Frontier::GeometricRaster
