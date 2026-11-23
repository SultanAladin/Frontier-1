//============================================================================================================================================
//                                                   PHYSICALSURFACEBINDING.H
//============================================================================================================================================
// 🧩 Renderer-facing packed constants for one already-compiled physical surface.
//    This is a data-only bridge: device descriptor ownership stays in the renderer/device layer.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/MaterialSpecification/Api/PhysicalSurfaceSpecification.h"

#include <cstdint>

namespace Slate
{

/// 🧩 GPU-compatible constant payload. Every colour is linear working-space RGB with an explicit padding lane.
/// The scalar selections are packed as unsigned values so shader code never depends on host enum layout.
struct alignas(16) PhysicalSurfacePacket
{
    float Albedo[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
    float NormalIncidenceReflectance[4] = { 0.04f, 0.04f, 0.04f, 1.0f };
    float Scalars[4] = { 0.0f, 0.5f, 1.0f, 0.0f }; // metallic, roughness, opacity, transmission
    float RefractionRatio = 1.5f;
    std::uint32_t ActiveChannelMask = 0u;
    std::uint32_t Closure = 0u;
    std::uint32_t Features = 0u;
    std::uint32_t Coverage = 0u;
    std::uint32_t Wall = 0u;
    std::uint32_t Interface = 0u;
    std::uint32_t TwoSided = 0u;
    std::uint32_t Reserved = 0u;
    // Explicit tail lanes complete the final constant-buffer register. This avoids compiler-inserted
    // alignment padding while keeping the packet a stable 96-byte, sixteen-byte-aligned GPU payload.
    float RegisterPadding[3] = {};
};

static_assert(sizeof(PhysicalSurfacePacket) % 16u == 0u,
              "physical surface constants must remain sixteen-byte aligned for uniform-buffer upload");

/// 🧩 Packs a validated physical closure for a render-time uniform or storage binding.
Deliver<PhysicalSurfacePacket> BindPhysicalSurface(const CompiledPhysicalSurface& Compiled);

} // namespace Slate
