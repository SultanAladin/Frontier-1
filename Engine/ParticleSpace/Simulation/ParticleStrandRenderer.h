/*============================================================================================================================================
                                                          PARTICLESTRANDRENDERER.H
============================================================================================================================================*/
// 🧩 Perspective Z-buffered 3D software rasterizer for dynamic strands (cables/chains, leafy vines, woven flags/sails, glossy beaded curtains & props)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleRig.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Perspective orbit camera matching Experimental/ParticleEditor Final ResolveCamera().
struct StrandCameraSpec
{
    float FocusMeters[3] = { 0.0f, 1.5f, 0.0f };
    float YawRadians      = 0.62f;
    float PitchRadians    = 0.34f;
    float DistanceMeters  = 12.2f;
    float FocalMm         = 29.0f;
    float SensorWidthMm   = 36.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Single-rig bundle for multi-rig Z-buffered strand scene rendering.
struct DynamicStrandRigView
{
    const ParticleRig*            Rig            = nullptr;
    std::span<const ParticleSlot> Slots          {};
    std::span<const StrandVertex> StrandVertices {};
};

/// Renders a live dynamic-strand ParticleRig (20-node StrandVertices + leaves + hardware props + floor grid) into RGBA8 sRGB.
void RenderDynamicStrandRigRgba8(
    const ParticleRig&            Rig,
    std::span<const ParticleSlot> Slots,
    std::span<const StrandVertex> StrandVertices,
    const StrandCameraSpec&       Camera,
    int                           Width,
    int                           Height,
    std::vector<uint8_t>&         OutRgba8);

/// Renders multiple dynamic-strand ParticleRigs together into a shared Z-buffered RGBA8 sRGB frame.
void RenderDynamicStrandSceneRgba8(
    std::span<const DynamicStrandRigView> Rigs,
    const StrandCameraSpec&               Camera,
    int                                   Width,
    int                                   Height,
    std::vector<uint8_t>&                 OutRgba8);

} // namespace Frontier::ParticleSpace
