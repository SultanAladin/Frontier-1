/*============================================================================================================================================
                                                COMPONENTCLASSSIMULATIONASSEMBLER.H
============================================================================================================================================*/
// 🧩 Phase D28 Prefab-to-Simulation bridge assembling ComponentClassDefinition + ComponentWorldPlacementRegister
//    (including hierarchical transforms & non-destructive InstanceOverrideDelta overrides) into SimulationSceneDocument
//    and live SimulationCoordinatorExtension instances across all 5 coupled simulation domains.
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D27-D29)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentWorldPlacement.h"
#include "Engine/SimulationSpace/SimulationSceneDocument.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  ASSEMBLY TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct PrefabAssemblyReport
{
    uint32_t PlacementsProcessed   = 0u;
    uint32_t ForcesAssembled       = 0u;
    uint32_t FluidSourcesAssembled = 0u;
    uint32_t CloudsAssembled       = 0u;
    uint32_t SoftBodiesAssembled   = 0u;
    uint32_t CollidersAssembled    = 0u;
    uint32_t SkippedHiddenCount    = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPONENTCLASS SIMULATION ASSEMBLER
//------------------------------------------------------------------------------------------------------------------------

class ComponentClassSimulationAssembler
{
public:
    /// Builds a ready-to-stamp DynamicRig ComponentClassDefinition containing all 4 simulation component categories
    /// (ForceFieldInstance, FluidRegionInstance, ParticleCloudInstance, SoftBodyInstance) + CollisionVolume.
    [[nodiscard]] static ComponentClass::ComponentClassDefinition BuildTracksideRigComponentClass();

    /// Evaluates the effective world-space translation for a component inside a placement (composing parent nesting,
    /// per-instance transform overrides, and the placement's root WorldTransform with YZX rotation).
    [[nodiscard]] static std::array<float, 3> ComputeEffectiveWorldPosition(
        const ComponentClass::ComponentClassDefinition&        definition,
        const ComponentClass::ComponentWorldPlacementRegister& placementRegister,
        ComponentClass::WorldPlacementToken                    placementToken,
        ComponentClass::ComponentInstanceToken                 componentToken) noexcept;

    /// Converts all live world placements referencing `classCatalog` into a unified SimulationSceneDocument.
    [[nodiscard]] static SimulationSceneDocument BuildSceneFromPlacements(
        const std::unordered_map<uint64_t, const ComponentClass::ComponentClassDefinition*>& classCatalog,
        const ComponentClass::ComponentWorldPlacementRegister&                               placementRegister,
        std::string_view                                                                     sceneTitle = "AssembledComponentWorld",
        PrefabAssemblyReport*                                                                outReport = nullptr);

    /// Convenience entry assembling a single ComponentClassDefinition across all matching placements and mounting
    /// directly into an initialized SimulationCoordinatorExtension.
    [[nodiscard]] static bool AssembleIntoCoordinator(
        ComponentClass::ComponentClassToken                    classToken,
        const ComponentClass::ComponentClassDefinition&        definition,
        const ComponentClass::ComponentWorldPlacementRegister& placementRegister,
        SimulationCoordinatorExtension&                        coordinator,
        PrefabAssemblyReport*                                  outReport = nullptr,
        std::string*                                           outError = nullptr);
};

} // namespace Frontier::SimulationSpace
