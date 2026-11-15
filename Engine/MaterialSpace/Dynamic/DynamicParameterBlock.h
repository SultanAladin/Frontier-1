/*==========================================================================================================================================
                                                     DYNAMICPARAMETERBLOCK.H
==========================================================================================================================================*/
// 🧩 Exact 256-byte GPU-aligned live uniform block updating dynamic material sliders, drivers, weather & 3D impact pulses at 120 Hz

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialDefinitions.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <array>
#include <cstdint>

namespace Frontier::MaterialSpace::Dynamic
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Exact 256-byte (16 x float4) GPU uniform block matching Slang GpuDynamicParameterBlock byte-for-byte
struct alignas(16) DynamicParameterBlock
{
    // Float4 [0]: Clock & progression controls
    float    ClockSeconds          = 0.0f;  // [s]   - Active simulation clock
    float    AnimationSpeed        = 1.0f;  // [-]   - Time speed multiplier
    float    TransitionProgress    = 0.50f; // [0-1] - Normalized dissolve / transition / scan position
    float    DetailScale           = 4.0f;  // [-]   - Spatial procedural feature frequency

    // Float4 [1]: Relief, opacity & climate inputs
    float    ReliefStrength        = 0.65f; // [-]   - Normal perturbation & micro-relief amplitude
    float    OpacityScale          = 1.0f;  // [0-1] - Global opacity multiplier for translucent barriers/water
    float    WeatherRainIntensity  = 0.55f; // [0-1] - Global rain wetness / puddle flood / streak drive
    float    WeatherSnowDepth      = 0.0f;  // [0-1] - Global frost & snow accumulation drive

    // Float4 [2..5]: Linear Rec.709 material colors + radiometric accent multiplier in .w
    std::array<float, 4u> PrimaryColorRgba    { 0.04f, 0.05f, 0.08f, 1.0f }; // Primary substrate/finish color
    std::array<float, 4u> SecondaryColorRgba  { 0.68f, 0.72f, 0.78f, 1.0f }; // Secondary transition/vein color
    std::array<float, 4u> TertiaryColorRgba   { 0.13f, 0.72f, 1.00f, 1.0f }; // Accent / edge seam / wireframe tint
    std::array<float, 4u> AccentRadianceRgba  { 0.45f, 0.90f, 1.00f, 1.6f }; // Luminous glow tint + intensity (.w)

    // Float4 [6..9]: Up to 4 live 3D impact origins in normalized local space + AgeSeconds in .w (< 0 = inactive)
    std::array<std::array<float, 4u>, 4u> ImpactOriginsAge {{
        {  0.55f,  0.26f,  0.79f, 0.25f },
        { -0.48f, -0.34f,  0.81f, 0.72f },
        {  0.00f,  0.00f,  1.00f, -1.0f },
        {  0.00f,  0.00f,  1.00f, -1.0f },
    }};

    // Float4 [10..11]: Asset-agnostic local bounding box for normalizing coordinates on ANY 3D asset
    std::array<float, 4u> AssetBoundsMinPad   { -1.0f, -1.0f, -1.0f, 0.0f };
    std::array<float, 4u> AssetBoundsMaxPad   {  1.0f,  1.0f,  1.0f, 0.0f };

    // Float4 [12]: Configurable sweep axis & edge width for transitions, lidar scans, and voxel dissolves
    std::array<float, 4u> SweepAxisWidth   {  0.92f, 0.34f, 0.00f, 0.08f };

    // Float4 [13]: Auxiliary physical parameters (IOR, thin-film nm, roughness bias, metalness bias)
    float    OpticalIor            = 1.50f;  // [-]  - Dielectric / water refractive index
    float    ThinFilmNanometers    = 460.0f; // [nm] - Center optical film thickness
    float    RoughnessBias         = 0.0f;   // [-]  - Live artist roughness offset [-0.5, +0.5]
    float    MetalnessBias         = 0.0f;   // [-]  - Live artist metalness offset [-0.5, +0.5]

    // Float4 [14]: Wave, caustic, and seam tuning
    float    SeamGlowIntensity     = 1.85f; // [-]  - Leading-edge plasma / thermal seam gain
    float    WaveAmplitude         = 0.55f; // [-]  - Ocean / capillary height amplitude
    float    TurbulenceAmount      = 0.50f; // [-]  - Organic domain-warp disturbance
    float    ThinFilmSpread        = 120.0f;// [nm] - Interference thickness modulation span

    // Uint4 [15]: Active dispatch indices & driver mode selector
    uint32_t PresetOrdinal         = 0u;    // [idx] - DynamicPresetIdentity ordinal
    uint32_t ActiveLayerCount      = 1u;    // [cnt] - Active visible layer entries
    uint32_t ActiveChannelMask     = 0xFFu; // [bit] - Enabled BlendChannelBitmask union
    uint32_t PrimaryDriverOrdinal  = 1u;    // [idx] - ParameterDriverMode ordinal
};

static_assert(sizeof(DynamicParameterBlock) == 256u, "DynamicParameterBlock must stay strictly 256 bytes for GPU alignment.");

struct DynamicDriverInputs
{
    double              ElapsedSeconds       = 0.0;   // [s]   - Monotonic playback clock
    float               ExternalProgress     = 0.50f; // [0-1] - External sequencer / controller override
    float               GlobalRainIntensity  = 0.55f; // [0-1] - Scene climate wetness
    float               GlobalSnowDepth      = 0.0f;  // [0-1] - Scene snow accumulation
    bool                UseExternalProgress  = false; // [-]   - True when ExternalTimeline drives TransitionProgress
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve all live driver modes into an updated 256-byte GPU uniform block with zero shader recompilation
void ResolveDynamicParameters(
    DynamicParameterBlock&     TargetBlock,
    const DynamicDriverInputs& DriverInputs) noexcept;

// 📝 Record a live 3D surface impact pulse (normalized local direction) into the next ring slot for shockwave ripples
void TriggerImpactPulse(
    DynamicParameterBlock& TargetBlock,
    Mathematics::Axis3D    NormalizedLocalHitDirection) noexcept;

// 📝 Configure asset-agnostic local bounds so sweep/dissolve/scan shaders scale cleanly to any 3D asset
void AssignAssetLocalBounds(
    DynamicParameterBlock& TargetBlock,
    Mathematics::Axis3D    BoundsMin,
    Mathematics::Axis3D    BoundsMax) noexcept;

// 📝 Normalize any local-space 3D coordinate into [-1, +1]^3 using the bound asset's local bounding box
[[nodiscard]] Mathematics::Axis3D NormalizeAssetCoordinate(
    const DynamicParameterBlock& Block,
    Mathematics::Axis3D          LocalPosition) noexcept;

// 📝 Clamp scalar parameters into finite, physically safe domains
void SanitizeDynamicParameterBlock(DynamicParameterBlock& TargetBlock) noexcept;

} // namespace Frontier::MaterialSpace::Dynamic
