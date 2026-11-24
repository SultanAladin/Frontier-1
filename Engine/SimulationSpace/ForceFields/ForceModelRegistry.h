/*============================================================================================================================================
                                                          FORCEMODELREGISTRY.H
============================================================================================================================================*/
// 🧩 Authoritative 20-model analytic force registry, falloff lookup tables, node factory, and GPU record packer
// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L1-L458

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  REGISTRY QUERY FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

/// Returns all 20 force model descriptors in exact reference ForceGeneratorCatalog.js declaration order.
[[nodiscard]] std::span<const ForceModelDescriptor> QueryAllForceModelDescriptors() noexcept;

/// O(1) lookup by numeric GLSL / Slang ForceModelOrdinal code (0..19). Returns nullptr if out of range.
[[nodiscard]] const ForceModelDescriptor* FindForceModelByOrdinal(ForceModelOrdinal code) noexcept;

/// Exact lookup by reference TOML / JS Id (e.g., "DirectionalWind", "VortexColumn", "FlowStream").
[[nodiscard]] const ForceModelDescriptor* FindForceModelByTomlId(std::string_view tomlId) noexcept;

/// Returns whether a force model uses continuous-force velocity saturation in FluidForcesCompute (FluidShaders.js L709).
[[nodiscard]] bool IsForceModelGasVelocitySaturated(ForceModelOrdinal code) noexcept;

/// Returns exact reference TOML/JS falloff token ("SmoothStep".."Cosine") for codes 0..6.
[[nodiscard]] std::string_view QueryFalloffTomlToken(ForceFalloffOrdinal falloff) noexcept;

/// Returns exact reference UI label ("Smooth", "Linear", "Inverse²", "Infinite", "Gaussian", "Plateau", "Interpolated").
[[nodiscard]] std::string_view QueryFalloffDisplayLabel(ForceFalloffOrdinal falloff) noexcept;

/// Parses a reference TOML/JS falloff string into ForceFalloffOrdinal.
[[nodiscard]] std::optional<ForceFalloffOrdinal> ParseFalloffTomlToken(std::string_view token) noexcept;

/// Returns all 7 reference force category names in exact declaration order.
[[nodiscard]] std::span<const std::string_view> QueryForceCategoryNames() noexcept;

/// Returns all 3 reference release-source shape tokens ("Sphere", "Disc", "Box").
[[nodiscard]] std::span<const std::string_view> QueryReleaseSourceShapeTokens() noexcept;

/// Returns all 3 reference fluid boundary mode tokens ("ClosedBox", "OpenTop", "OpenAll").
[[nodiscard]] std::span<const std::string_view> QueryFluidBoundaryTokens() noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                FACTORY & GPU RECORD PACKING
//------------------------------------------------------------------------------------------------------------------------

/// Constructs a ForceModelNode populated with the exact reference defaults for the given ordinal.
[[nodiscard]] ForceModelNode ConstructDefaultForceNode(ForceModelOrdinal code, std::string_view nodeId = {}) noexcept;

/// Packs a ForceModelNode + evaluated timing weight into a 64-byte ForceModelGpuRecord (FluidSolver.js L419-L438).
[[nodiscard]] ForceModelGpuRecord PackForceModelGpuRecord(const ForceModelNode& node, float timingWeight = 1.0f) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields
