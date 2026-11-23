/*============================================================================================================================================
                                                        COMPONENTWORLDPLACEMENT.H
============================================================================================================================================*/
// 🧩 World placement record binding a compiled ComponentClass into World Outliner rows with non-destructive per-instance overrides

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::ComponentClass
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct InstanceOverrideDelta
{
    ComponentInstanceToken           TargetComponentToken   {};    // [id]  - Component inside referenced ComponentClass
    bool                             OverrideTransform      = false; // [-] - Override LocalTransform
    ComponentTransform               LocalTransform         {};    // [-]   - Overridden local transform
    bool                             OverrideVisibility     = false; // [-] - Override VisibilityEnabled
    bool                             VisibilityEnabled      = true;  // [-]   - Overridden visibility
    bool                             OverrideLight          = false; // [-] - Override Light intensity/colour
    Mathematics::Scalar3D            LightLinearColourRGB   { 1.0f, 1.0f, 1.0f }; // [0-1]
    float                            LightIntensityCandela  = 20.0f; // [cd]
    std::vector<MaterialSlotOverride> SlotOverrides         {};    // [-]   - Overridden geometry material slots
};

struct WorldPlacementEntry
{
    WorldPlacementToken                PlacementToken    {};     // [id]  - World placement handle
    ComponentClassToken                ClassToken        {};     // [id]  - Source ComponentClass handle
    std::string                        InstanceTitle     {};     // [-]   - World Outliner title (e.g. "ControlVehicle_01")
    ComponentTransform                 WorldTransform    {};     // [-]   - Root placement in world coordinates
    bool                               ExpandedInOutliner= true; // [-]   - Outliner disclosure state
    std::vector<InstanceOverrideDelta> Overrides         {};     // [-]   - Sparse per-instance deltas
};

class ComponentWorldPlacementRegister
{
public:
    [[nodiscard]] WorldPlacementToken SpawnPlacement(
        ComponentClassToken ClassToken,
        std::string_view    InstanceTitle,
        ComponentTransform  WorldTransform);

    bool RemovePlacement(WorldPlacementToken Token) noexcept;

    [[nodiscard]] WorldPlacementEntry*       ResolvePlacement(WorldPlacementToken Token) noexcept;
    [[nodiscard]] const WorldPlacementEntry* InspectPlacement(WorldPlacementToken Token) const noexcept;

    [[nodiscard]] InstanceOverrideDelta& AcquireOverride(WorldPlacementToken Token, ComponentInstanceToken ComponentToken);
    [[nodiscard]] const InstanceOverrideDelta* InspectOverride(
        WorldPlacementToken    Token,
        ComponentInstanceToken ComponentToken) const noexcept;

    [[nodiscard]] ComponentTransform EvaluateEffectiveLocalTransform(
        const ComponentClassDefinition& Definition,
        WorldPlacementToken             PlacementToken,
        ComponentInstanceToken          ComponentToken) const noexcept;

    [[nodiscard]] uint32_t QueryLivePlacementCount() const noexcept;
    [[nodiscard]] std::vector<WorldPlacementToken> CollectLiveTokens() const;

private:
    struct PlacementSlot
    {
        WorldPlacementEntry Entry    {};
        uint32_t            Gen      = 1u;
        bool                Occupied = false;
    };

    std::vector<PlacementSlot> Slots     {};
    std::vector<uint32_t>      FreeSlots {};
};

} // namespace Frontier::ComponentClass
