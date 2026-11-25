/*============================================================================================================================================
                                                     DEFORMABLEPRESETREGISTRY.H
============================================================================================================================================*/
// 🧩 Phase D03 Deterministic C++20 preset registry replicating all 9 XPBD + 3D FEM studio presets (tires, plants, webs, cables, cloth)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.3

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRESET METADATA
//------------------------------------------------------------------------------------------------------------------------

struct DeformablePresetDescriptor
{
    DeformablePresetKind              Kind          = DeformablePresetKind::SportTireFem;
    const char*                       PresetId      = "fem_tyre";
    const char*                       FileStem      = "SportTireFem";
    const char*                       DisplayTitle  = "3D FEM Soft-Body Tire";
    const char*                       TagLabel      = "Neo-Hookean";
    DeformableSimulationConfiguration DefaultConfig {};
    float                             GroundY       = -1.78f;
    std::array<float, 3>              ColliderPos   { 1.55f, -1.62f, 0.0f };
    float                             ColliderRadius = 0.42f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE PRESET REGISTRY
//------------------------------------------------------------------------------------------------------------------------

class DeformablePresetRegistry
{
public:
    /// Enumerates all 9 canonical presets (3D FEM Tire, Pneumatic Tire, Ferns, Spiderweb, Cables/Chains, Vines, Reeds, Kelp, Cloth).
    [[nodiscard]] static std::span<const DeformablePresetDescriptor> EnumeratePresets() noexcept;

    /// Looks up a preset descriptor by its PresetId ("fem_tyre", "tyre", "botanical", ...) or FileStem ("SportTireFem", ...).
    [[nodiscard]] static const DeformablePresetDescriptor* FindById(std::string_view presetIdOrStem) noexcept;

    /// Constructs the full DeformableBody geometry, constraints, tetrahedra, and conflict-colored batches for `kind`.
    static bool BuildPreset(
        DeformablePresetKind kind,
        DeformableBody& outBody,
        DeformableSimulationConfiguration& outConfiguration);

    /// Individual modular builders (usable both by presets and custom .SoftBody documents).
    static void AppendFemSoftBodyTire(
        DeformableBody& body,
        float axleX = 0.0f, float axleY = -0.62f, float axleZ = 0.0f,
        float rimRadius = 0.66f, float outerRadius = 1.15f, float halfWidth = 0.43f, uint32_t sectors = 36u);

    static void AppendPneumaticXpbdTire(
        DeformableBody& body,
        float axleX = 0.0f, float axleY = -0.62f, float axleZ = 0.0f,
        float rimRadius = 0.66f, float outerRadius = 1.14f, float halfWidth = 0.42f, uint32_t sectors = 48u);

    static void AppendFernCluster(DeformableBody& body, float ox, float oy, float oz, uint32_t frondCount = 9u);
    static void AppendOrbSpiderweb(DeformableBody& body, float ox, float oy, float oz, float webRadius, uint32_t spokes = 14u, uint32_t rings = 7u);
    static void AppendHangingVine(DeformableBody& body, float x, float yTop, float z, uint32_t segs, float length, uint32_t leafStride = 1u);
    static void AppendReedCluster(DeformableBody& body, float ox, float oy, float oz, uint32_t reedCount = 18u);
    static void AppendKelpForest(DeformableBody& body, float ox, float oy, float oz, uint32_t kelpCount = 10u);
    static void AppendWovenCloth(DeformableBody& body, uint32_t cols = 19u, uint32_t rows = 15u, float width = 2.7f, float height = 2.1f);
};

} // namespace Frontier::SimulationSpace::Deformable
