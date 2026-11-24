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

#include <cstdint>
#include <memory>
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

    bool RemoveManagedInstance(World::InstanceToken Token) noexcept;

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
    std::vector<World::CategoryInstanceHandle> ManagedHandles {};   // [-] - Owned RAII category instances
};

} // namespace Frontier::Editor

#endif // FRONTIER_PROFILE_EDITOR
