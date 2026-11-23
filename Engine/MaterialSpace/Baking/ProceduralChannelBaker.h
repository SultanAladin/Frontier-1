/*============================================================================================================================================
                                                           PROCEDURALCHANNELBAKER.H
============================================================================================================================================*/
// 🧩 Deterministic tileable procedural normal, Toksvig-filtered roughness, & albedo mip-pyramid baker

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <cstdint>
#include <vector>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ProceduralPatternCategory : uint8_t
{
    BrushedAnisotropicGrooves = 0, // [-] - Directional machined metal micro-scratches
    PorousMineralAggregate    = 1, // [-] - Multi-octave cellular mineral pores & aggregate
    WovenMicrofiberTwill      = 2, // [-] - Interlaced warp/weft cloth fiber relief
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BakedChannelTexel
{
    Mathematics::LinearRGB AlbedoModulation { 1.0f, 1.0f, 1.0f }; // [-] - Multiplicative linear albedo factor
    Mathematics::Axis3D    TangentNormal    { 0.0f, 0.0f, 1.0f }; // [-] - Unit tangent-space normal (+Z up)
    float                  EffectiveRoughness = 0.25f;            // [-] - Toksvig normal-variance filtered roughness
    float                  ReliefHeight       = 0.50f;            // [-] - Normalized relief height [0, 1]
};

struct BakedChannelMipLevel
{
    uint32_t                       Width  = 0u; // [px] - Mip width
    uint32_t                       Height = 0u; // [px] - Mip height
    std::vector<BakedChannelTexel> Texels {};   // [-]  - Row-major baked texels
};

struct BakedChannelPyramid
{
    std::vector<BakedChannelMipLevel> MipLevels {}; // [-] - Mip 0 (finest) .. Mip N-1 (1x1), with Toksvig roughness widening
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] BakedChannelPyramid BakeProceduralChannelPyramid(
    ProceduralPatternCategory Pattern,
    uint32_t              BaseResolution,
    float                 BaseRoughness,
    float                 ReliefAmplitude = 1.0f,
    uint32_t              DeterministicSeed = 1337u);

} // namespace Frontier::MaterialSpace
