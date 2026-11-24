/*============================================================================================================================================
                                                           INSTANCETOKEN.H
============================================================================================================================================*/
// 🧩 Lean 8-byte generation-checked InstanceToken & 48-byte trivially-copyable InstanceRecordEntry with non-kinship containment links

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace Frontier::World
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t InvalidInstanceSlot = std::numeric_limits<uint32_t>::max(); // [idx] - Sentinel for absent slot link

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InstanceCategory : uint16_t
{
    Unassigned        = 0u,  // [-] - Uninitialized category sentinel
    SurfaceGeometry   = 1u,  // [-] - Placed .GeometrySpace / procedural surface participant
    SolarBody         = 2u,  // [-] - Physical solar ephemeris & directional light body
    LunarBody         = 3u,  // [-] - Celestial lunar body slot (up to 6 active bodies)
    AtmosphereMedium  = 4u,  // [-] - Planetary Rayleigh / Mie / Ozone atmospheric medium
    CloudDeck         = 5u,  // [-] - Volumetric cloud deck or localized cloud volume
    StellarField      = 6u,  // [-] - Nocturnal starfield & Milky Way catalog
    Luminaire         = 7u,  // [-] - Point, spot, area, tube, LED, or measured IES light source
    ParticleCloud     = 8u,  // [-] - GPU-driven particulate cloud (.ParticleSpace)
    ForceFieldVolume  = 9u,  // [-] - Analytical or vector-grid force field (.ForceField)
    TriggerZone       = 10u, // [-] - Spatial overlap sensor volume (.TriggerZone)
    AcousticSource    = 11u, // [-] - Spatial acoustic propagation source (.Acoustic)
    CameraOptics      = 12u, // [-] - Optical camera lens, sensor & projection basis
    ContainmentFolder = 13u  // [-] - Organizational containment group (Editor-authored)
};

enum class InstanceDirectiveMask : uint32_t
{
    None                            = 0u,        // [mask] - No directives active
    EnableVisibility                = 1u << 0u,  // [mask] - Render participant in active viewport
    EnableSimulationTick            = 1u << 1u,  // [mask] - Include participant in tight simulation loops
    EnableTransformRebuild          = 1u << 2u,  // [mask] - Transform changed; RequireRebuild pending
    EnableShadowProjection          = 1u << 3u,  // [mask] - Project onto shadow cascades & penumbra
    EnableStaticPlacement           = 1u << 4u,  // [mask] - Immovable placement (skip transform upload)
    EnableEditLock                  = 1u << 5u,  // [mask] - Prevent accidental transform edits in Editor
    EnableDistanceFieldContribution = 1u << 6u,  // [mask] - Include surface in global 3D distance field
    EnableEditorRegistration        = 1u << 7u   // [mask] - Registered with Editor Outliner & Inspector
};

[[nodiscard]] constexpr InstanceDirectiveMask operator|(
    InstanceDirectiveMask LeftMask,
    InstanceDirectiveMask RightMask) noexcept
{
    return static_cast<InstanceDirectiveMask>(
        static_cast<uint32_t>(LeftMask) | static_cast<uint32_t>(RightMask));
}

[[nodiscard]] constexpr InstanceDirectiveMask operator&(
    InstanceDirectiveMask LeftMask,
    InstanceDirectiveMask RightMask) noexcept
{
    return static_cast<InstanceDirectiveMask>(
        static_cast<uint32_t>(LeftMask) & static_cast<uint32_t>(RightMask));
}

[[nodiscard]] constexpr bool QueryDirectiveEnabled(
    uint32_t              RawMask,
    InstanceDirectiveMask Directive) noexcept
{
    return (RawMask & static_cast<uint32_t>(Directive)) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Compact 8-byte generation-checked handle (zero virtual dispatch, immune to stale slot reuse)
struct InstanceToken
{
    uint32_t SlotIndex       = InvalidInstanceSlot; // [idx] - Slot index inside WorldInstanceRegister
    uint16_t Generation      = 0u;                  // [ver] - Non-zero monotonic version for slot authentication
    uint16_t CategoryOrdinal = 0u;                  // [idx] - InstanceCategory enumerator ordinal

    [[nodiscard]] constexpr bool QueryValid() const noexcept
    {
        return (SlotIndex != InvalidInstanceSlot) && (Generation != 0u);
    }

    [[nodiscard]] constexpr InstanceCategory QueryCategory() const noexcept
    {
        return static_cast<InstanceCategory>(CategoryOrdinal);
    }

    [[nodiscard]] constexpr bool operator==(const InstanceToken& Other) const noexcept = default;
};

static_assert(sizeof(InstanceToken) == 8u, "InstanceToken must remain strictly 8 bytes.");
static_assert(std::is_trivially_copyable_v<InstanceToken>, "InstanceToken must remain trivially copyable.");

// 48-byte non-virtual core instance record (fits cleanly in < 1 cache line, zero vtable)
struct alignas(16) InstanceRecordEntry
{
    InstanceToken Token            {};                  // [8 B]  - Generation-checked self identity
    uint32_t      CategorySlot     = InvalidInstanceSlot; // [idx] - Dense index inside category SoA register
    uint32_t      TransformSlot    = InvalidInstanceSlot; // [idx] - Dense index inside WorldTransformStorage
    uint32_t      EnclosingIndex   = InvalidInstanceSlot; // [idx] - Non-kinship enclosing slot (~0u = root)
    uint32_t      FirstNestedIndex = InvalidInstanceSlot; // [idx] - First nested slot (~0u = empty)
    uint32_t      NextLateralIndex = InvalidInstanceSlot; // [idx] - Next lateral slot at same depth (~0u = end)
    uint32_t      DirectiveMask    = 0u;                // [mask] - InstanceDirectiveMask bits
    uint64_t      DocumentHash64   = 0ull;              // [hash] - FNV-1a 64-bit hash of source .Instance document
    uint64_t      SpatialCellId    = 0ull;              // [idx]  - Broad spatial partition cell identifier
};

static_assert(sizeof(InstanceRecordEntry) == 48u, "InstanceRecordEntry must remain strictly 48 bytes.");
static_assert(std::is_trivially_copyable_v<InstanceRecordEntry>, "InstanceRecordEntry must remain trivially copyable.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr std::string_view QueryInstanceCategoryTag(InstanceCategory Category) noexcept
{
    switch (Category)
    {
        case InstanceCategory::SurfaceGeometry:   return "geom:native";
        case InstanceCategory::SolarBody:         return "env:sun";
        case InstanceCategory::LunarBody:         return "env:moon";
        case InstanceCategory::AtmosphereMedium:  return "env:atmosphere";
        case InstanceCategory::CloudDeck:         return "env:clouds";
        case InstanceCategory::StellarField:      return "env:stars";
        case InstanceCategory::Luminaire:         return "light:point";
        case InstanceCategory::ParticleCloud:     return "fx:particle";
        case InstanceCategory::ForceFieldVolume:  return "sim:forcefield";
        case InstanceCategory::TriggerZone:       return "logic:trigger";
        case InstanceCategory::AcousticSource:    return "audio:source";
        case InstanceCategory::CameraOptics:      return "camera:perspective";
        case InstanceCategory::ContainmentFolder: return "env:folder";
        default:                                  return "geom:native";
    }
}

[[nodiscard]] constexpr std::string_view QueryInstanceCategoryName(InstanceCategory Category) noexcept
{
    switch (Category)
    {
        case InstanceCategory::SurfaceGeometry:   return "SurfaceGeometry";
        case InstanceCategory::SolarBody:         return "SolarBody";
        case InstanceCategory::LunarBody:         return "LunarBody";
        case InstanceCategory::AtmosphereMedium:  return "AtmosphereMedium";
        case InstanceCategory::CloudDeck:         return "CloudDeck";
        case InstanceCategory::StellarField:      return "StellarField";
        case InstanceCategory::Luminaire:         return "Luminaire";
        case InstanceCategory::ParticleCloud:     return "ParticleCloud";
        case InstanceCategory::ForceFieldVolume:  return "ForceFieldVolume";
        case InstanceCategory::TriggerZone:       return "TriggerZone";
        case InstanceCategory::AcousticSource:    return "AcousticSource";
        case InstanceCategory::CameraOptics:      return "CameraOptics";
        case InstanceCategory::ContainmentFolder: return "ContainmentFolder";
        default:                                  return "Unassigned";
    }
}

[[nodiscard]] constexpr std::string_view QueryInstanceCategoryIconPath(InstanceCategory Category) noexcept
{
    switch (Category)
    {
        case InstanceCategory::SurfaceGeometry:   return "EngineContent/Icons/Editor/editor-mesh.svg";
        case InstanceCategory::SolarBody:         return "EngineContent/Icons/Editor/sun.svg";
        case InstanceCategory::LunarBody:         return "EngineContent/Icons/Editor/moon.svg";
        case InstanceCategory::AtmosphereMedium:  return "EngineContent/Icons/Editor/sky-scattering.svg";
        case InstanceCategory::CloudDeck:         return "EngineContent/Icons/Editor/clouds.svg";
        case InstanceCategory::StellarField:      return "EngineContent/Icons/Editor/stars.svg";
        case InstanceCategory::Luminaire:         return "EngineContent/Icons/Editor/editor-point-light.svg";
        case InstanceCategory::ParticleCloud:     return "EngineContent/Icons/Editor/editor-particle-emitter.svg";
        case InstanceCategory::ForceFieldVolume:  return "EngineContent/Icons/Editor/wind.svg";
        case InstanceCategory::TriggerZone:       return "EngineContent/Icons/Editor/editor-physics.svg";
        case InstanceCategory::AcousticSource:    return "EngineContent/Icons/Editor/slate-lantern.svg";
        case InstanceCategory::CameraOptics:      return "EngineContent/Icons/Editor/camera.svg";
        case InstanceCategory::ContainmentFolder: return "EngineContent/Icons/Editor/folder-generic.svg";
        default:                                  return "EngineContent/Icons/Editor/editor-mesh.svg";
    }
}

[[nodiscard]] constexpr InstanceCategory ResolveInstanceCategoryName(std::string_view NameText) noexcept
{
    if (NameText == "SurfaceGeometry")   return InstanceCategory::SurfaceGeometry;
    if (NameText == "SolarBody")         return InstanceCategory::SolarBody;
    if (NameText == "LunarBody")         return InstanceCategory::LunarBody;
    if (NameText == "AtmosphereMedium")  return InstanceCategory::AtmosphereMedium;
    if (NameText == "CloudDeck")         return InstanceCategory::CloudDeck;
    if (NameText == "StellarField")      return InstanceCategory::StellarField;
    if (NameText == "Luminaire")         return InstanceCategory::Luminaire;
    if (NameText == "ParticleCloud")     return InstanceCategory::ParticleCloud;
    if (NameText == "ForceFieldVolume")  return InstanceCategory::ForceFieldVolume;
    if (NameText == "TriggerZone")       return InstanceCategory::TriggerZone;
    if (NameText == "AcousticSource")    return InstanceCategory::AcousticSource;
    if (NameText == "CameraOptics")      return InstanceCategory::CameraOptics;
    if (NameText == "ContainmentFolder") return InstanceCategory::ContainmentFolder;
    return InstanceCategory::Unassigned;
}

} // namespace Frontier::World
