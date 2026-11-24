/*============================================================================================================================================
                                                      WORLDINSTANCEEDITORBRIDGE.H
============================================================================================================================================*/
// 🧩 Editor-only bridge binding WorldInstanceRegister <-> OutlinerPanel, .Instance spawning & WorldScene synchronization

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"

#if FRONTIER_PROFILE_EDITOR

#include "Engine/Codecs/InstanceCodec.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/WorldInstanceRegister.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier::Editor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 WORLD INSTANCE EDITOR BRIDGE
//------------------------------------------------------------------------------------------------------------------------

class WorldInstanceEditorBridge
{
public:
    WorldInstanceEditorBridge() noexcept = default;
    ~WorldInstanceEditorBridge() noexcept;

    WorldInstanceEditorBridge(const WorldInstanceEditorBridge&)            = delete;
    WorldInstanceEditorBridge& operator=(const WorldInstanceEditorBridge&) = delete;

    void BindOutliner(
        World::WorldInstanceRegister& TargetRegister,
        OutlinerPanel&                TargetOutliner);

    void Unbind() noexcept;
    void ReleaseManagedInstances() noexcept;

    // Adopts active atmosphere, solar, lunar & cloud participants from a loaded WorldScene into SoA registers
    void AdoptWorldSceneIntoRegister(
        World::WorldInstanceRegister&                    TargetRegister,
        const GeometricRaster::WorldSceneSpecification&  Scene);

    // Spawns a live Editor category instance from a decoded .Instance document (auto-registers in Outliner)
    [[nodiscard]] World::InstanceToken SpawnInstanceFromDocument(
        World::WorldInstanceRegister&        TargetRegister,
        const Codecs::InstanceDocumentModel& DocumentModel,
        World::InstanceToken                 EnclosingToken = {});

    // Decodes an .Instance or .InstanceSpace file on disk and spawns it at WorldPosition (selecting its Outliner row)
    [[nodiscard]] World::InstanceToken SpawnInstanceFromFile(
        World::WorldInstanceRegister& TargetRegister,
        const std::filesystem::path&  SourcePath,
        const std::array<float, 3>&   WorldPosition,
        World::InstanceToken          EnclosingToken = {},
        std::string*                  OutDiagnostic  = nullptr);

    bool RemoveManagedInstance(World::InstanceToken Token) noexcept;

    // Reclaims any managed instances bound to the provided Outliner slots (returns count reclaimed)
    uint32_t RemoveOutlinerBoundSlots(std::span<const OutlinerSlotToken> Tokens) noexcept;

    // Clones a bound .Instance Outliner row into a new live WorldRegister instance & Outliner row
    [[nodiscard]] OutlinerSlotToken DuplicateOutlinerBoundSlot(
        World::WorldInstanceRegister& TargetRegister,
        OutlinerSlotToken             SourceSlot);

    // Pushes Outliner/Gizmo transform & visibility edits back into WorldInstanceRegister (returns true if mutated)
    [[nodiscard]] bool SynchronizeOutlinerMutationsToRegister(
        World::WorldInstanceRegister& TargetRegister) noexcept;

    // Exports a live instance with its latest transform & directives to an Editor or Shipping .Instance file
    [[nodiscard]] bool ExportInstanceToFile(
        const World::WorldInstanceRegister& SourceRegister,
        World::InstanceToken                Token,
        const std::filesystem::path&        TargetPath,
        bool                                StripEditorDirectives,
        std::string&                        OutDiagnostic) const;

    // Pushes evaluated SoA register outputs back into WorldSceneSpecification (returns true if Scene mutated)
    [[nodiscard]] bool SynchronizeRegisterToWorldScene(
        const World::WorldInstanceRegister&         SourceRegister,
        const World::WorldLoopStepMetrics&          Metrics,
        GeometricRaster::WorldSceneSpecification&   Scene,
        float                                       Δt) const noexcept;

    [[nodiscard]] OutlinerSlotToken    QueryOutlinerSlot(World::InstanceToken Token) const noexcept;
    [[nodiscard]] World::InstanceToken QueryInstanceToken(OutlinerSlotToken SlotToken) const noexcept;
    [[nodiscard]] uint32_t             QueryBoundRowCount() const noexcept
    {
        return static_cast<uint32_t>(InstanceToOutlinerMap.size());
    }

private:
    [[nodiscard]] static ConstructSlotCategory ResolveOutlinerCategory(World::InstanceCategory Category) noexcept;

    World::WorldInstanceRegister*                   BoundRegister         = nullptr; // [-] - Bound world instance register
    OutlinerPanel*                                  BoundOutliner         = nullptr; // [-] - Bound Editor Outliner panel
    std::unordered_map<uint32_t, OutlinerSlotToken> InstanceToOutlinerMap {};        // [-] - Instance SlotIndex -> OutlinerSlotToken
    std::unordered_map<uint32_t, World::InstanceToken> OutlinerToInstanceMap {};     // [-] - Outliner Identifier -> InstanceToken

    // RAII handles for adopted scene participants & spawned .Instance entries (16B value-typed, zero heap per handle)
    std::vector<World::CategoryInstanceHandle>                  ManagedHandles       {}; // [-] - Owned RAII category instances
    std::unordered_map<uint32_t, Codecs::InstanceDocumentModel> DocumentModelsBySlot {}; // [-] - Source .Instance models by SlotIndex
};

} // namespace Frontier::Editor

#endif // FRONTIER_PROFILE_EDITOR
