/*============================================================================================================================================
                                                        PARTICLEPRESETCATALOGUE.H
============================================================================================================================================*/
// 🧩 Authoritative C++ catalogue of all 102 Frontier Particle Studio presets (93 active + 9 quarantined across 15 VFX categories)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleRig.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticlePresetMetadata
{
    std::string_view Identifier      {}; // [-] - Unique preset key (e.g. "object-surface-pour", "firework-grand-finale")
    std::string_view DisplayName     {}; // [-] - Human-readable studio title
    std::string_view CategoryLabel   {}; // [-] - Studio category ("Silk & Curl", "Pyro & Water", "Shields & Tech", ...)
    std::string_view SubLabel        {}; // [-] - Compact badge sublabel
    std::string_view SummaryText     {}; // [-] - Concise preset description
    bool             Quarantined     = false; // [-] - True for the 9 quarantined experimental presets
    uint32_t         SlotCapacity    = 1200u;
    float            ReleaseRate     = 120.0f;
    uint8_t          OriginCode      = 0u;
    uint8_t          CarrierCode     = 0u;
    uint8_t          SpriteCutout    = 0u;
    uint8_t          SurfacePreset   = 0u;
    uint8_t          FireworkShape   = 0u;
    uint8_t          CurlMode        = 0u;
    uint8_t          DynamicsFamily  = 0u; // 0=Ballistic,1=Curl,2=Swarm,3=Species,4=Orbital,5=Electro,6=Harmonic,7=Attractor,8=FluidSph,9=Granular,10=Elastic,11=Thermal,12=Arc,13=Matrix,14=Botanical,15=Morpho
    bool             AdditiveBlend   = true;
    float            OriginCenter[3] = { 0.0f, 1.5f, 0.0f };
    float            OriginRadius    = 0.5f;
    float            OriginHalf[3]   = { 1.0f, 1.0f, 1.0f };
    float            LaunchDir[3]    = { 0.0f, 1.0f, 0.0f };
    float            SpreadRad       = 0.5f;
    float            SpeedMin        = 1.0f;
    float            SpeedMax        = 3.0f;
    float            LifeMin         = 1.5f;
    float            LifeMax         = 3.0f;
    float            SizeBirth       = 0.06f;
    float            SizeDeath       = 0.03f;
    float            SizeScale       = 1.0f;
    float            GravityScale    = 0.5f;
    float            Drag            = 0.2f;
    float            WindCoupling    = 0.3f;
    float            Bounce          = 0.0f;
    float            Buoyancy        = 0.0f;
    float            Flutter         = 0.0f;
    float            CurlFreq        = 0.85f;
    float            CurlAmp         = 2.2f;
    float            CurlRate        = 0.24f;
    float            ShellRadius     = 1.75f;
    float            ColA[4]         = { 1.0f, 1.0f, 1.0f, 1.0f };
    float            ColMid[4]       = { 0.5f, 0.8f, 1.0f, 1.0f };
    float            ColB[4]         = { 0.1f, 0.4f, 0.9f, 1.0f };
    std::string_view ColorByKey      { "modeDefault" };
    std::string_view FireworkText    { "2026" };
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Returns read-only span of all 102 authoritative Particle Studio presets (93 active + 9 quarantined).
[[nodiscard]] std::span<const ParticlePresetMetadata> QueryAllParticlePresets() noexcept;

/// Returns all unique category labels in canonical studio order.
[[nodiscard]] std::span<const std::string_view> QueryPresetCategories() noexcept;

/// Finds a preset by exact Identifier (returns nullptr if missing).
[[nodiscard]] const ParticlePresetMetadata* FindParticlePresetById(std::string_view Identifier) noexcept;

/// Constructs a fully configured ParticleRig from any catalogue preset entry.
[[nodiscard]] ParticleRig InstantiateRigFromPreset(const ParticlePresetMetadata& Preset);

/// Constructs a complete ParticleDocument (rig + 6 calibrated Studio ForceSources) for the requested preset ID.
[[nodiscard]] ParticleDocument InstantiateDocumentFromPresetId(std::string_view Identifier);

} // namespace Frontier::ParticleSpace
