/*============================================================================================================================================
                                                       COMPONENTWORLDPLACEMENT.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentWorldPlacement spawning, sparse per-instance overrides & effective transform resolution

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentWorldPlacement.h"

namespace Frontier::ComponentClass
{

WorldPlacementToken ComponentWorldPlacementRegister::SpawnPlacement(
    ComponentClassToken ClassToken,
    std::string_view    InstanceTitle,
    ComponentTransform  WorldTransform)
{
    uint32_t ZeroIdx = 0u;
    if (!FreeSlots.empty())
    {
        ZeroIdx = FreeSlots.back();
        FreeSlots.pop_back();
    }
    else
    {
        ZeroIdx = static_cast<uint32_t>(Slots.size());
        Slots.emplace_back();
    }

    PlacementSlot& Slot = Slots[ZeroIdx];
    Slot.Occupied                 = true;
    Slot.Gen                      = (Slot.Gen == 0u) ? 1u : Slot.Gen;
    Slot.Entry                    = WorldPlacementEntry{};
    Slot.Entry.PlacementToken     = { ZeroIdx + 1u, Slot.Gen };
    Slot.Entry.ClassToken         = ClassToken;
    Slot.Entry.InstanceTitle      = InstanceTitle.empty() ? std::string("PlacedComponentClass") : std::string(InstanceTitle);
    Slot.Entry.WorldTransform     = WorldTransform;
    Slot.Entry.ExpandedInOutliner = true;

    return Slot.Entry.PlacementToken;
}

bool ComponentWorldPlacementRegister::RemovePlacement(WorldPlacementToken Token) noexcept
{
    if (!Token.Valid())
    {
        return false;
    }
    const uint32_t ZeroIdx = Token.SlotIndex - 1u;
    if (ZeroIdx >= Slots.size() || !Slots[ZeroIdx].Occupied || Slots[ZeroIdx].Gen != Token.Generation)
    {
        return false;
    }
    Slots[ZeroIdx].Occupied = false;
    ++Slots[ZeroIdx].Gen;
    FreeSlots.push_back(ZeroIdx);
    return true;
}

WorldPlacementEntry* ComponentWorldPlacementRegister::ResolvePlacement(WorldPlacementToken Token) noexcept
{
    if (!Token.Valid())
    {
        return nullptr;
    }
    const uint32_t ZeroIdx = Token.SlotIndex - 1u;
    if (ZeroIdx >= Slots.size() || !Slots[ZeroIdx].Occupied || Slots[ZeroIdx].Gen != Token.Generation)
    {
        return nullptr;
    }
    return &Slots[ZeroIdx].Entry;
}

const WorldPlacementEntry* ComponentWorldPlacementRegister::InspectPlacement(WorldPlacementToken Token) const noexcept
{
    if (!Token.Valid())
    {
        return nullptr;
    }
    const uint32_t ZeroIdx = Token.SlotIndex - 1u;
    if (ZeroIdx >= Slots.size() || !Slots[ZeroIdx].Occupied || Slots[ZeroIdx].Gen != Token.Generation)
    {
        return nullptr;
    }
    return &Slots[ZeroIdx].Entry;
}

InstanceOverrideDelta& ComponentWorldPlacementRegister::AcquireOverride(
    WorldPlacementToken    Token,
    ComponentInstanceToken ComponentToken)
{
    WorldPlacementEntry* Entry = ResolvePlacement(Token);
    for (InstanceOverrideDelta& Delta : Entry->Overrides)
    {
        if (Delta.TargetComponentToken == ComponentToken)
        {
            return Delta;
        }
    }
    Entry->Overrides.emplace_back();
    Entry->Overrides.back().TargetComponentToken = ComponentToken;
    return Entry->Overrides.back();
}

const InstanceOverrideDelta* ComponentWorldPlacementRegister::InspectOverride(
    WorldPlacementToken    Token,
    ComponentInstanceToken ComponentToken) const noexcept
{
    const WorldPlacementEntry* Entry = InspectPlacement(Token);
    if (Entry == nullptr)
    {
        return nullptr;
    }
    for (const InstanceOverrideDelta& Delta : Entry->Overrides)
    {
        if (Delta.TargetComponentToken == ComponentToken)
        {
            return &Delta;
        }
    }
    return nullptr;
}

ComponentTransform ComponentWorldPlacementRegister::EvaluateEffectiveLocalTransform(
    const ComponentClassDefinition& Definition,
    WorldPlacementToken             PlacementToken,
    ComponentInstanceToken          ComponentToken) const noexcept
{
    const InstanceOverrideDelta* Delta = InspectOverride(PlacementToken, ComponentToken);
    if (Delta != nullptr && Delta->OverrideTransform)
    {
        return Delta->LocalTransform;
    }
    const ComponentInstanceHeader* Header = Definition.Storage.InspectHeader(ComponentToken);
    return (Header != nullptr) ? Header->LocalTransform : ComponentTransform{};
}

uint32_t ComponentWorldPlacementRegister::QueryLivePlacementCount() const noexcept
{
    uint32_t Count = 0u;
    for (const PlacementSlot& Slot : Slots)
    {
        if (Slot.Occupied)
        {
            ++Count;
        }
    }
    return Count;
}

std::vector<WorldPlacementToken> ComponentWorldPlacementRegister::CollectLiveTokens() const
{
    std::vector<WorldPlacementToken> Tokens {};
    for (const PlacementSlot& Slot : Slots)
    {
        if (Slot.Occupied)
        {
            Tokens.push_back(Slot.Entry.PlacementToken);
        }
    }
    return Tokens;
}

} // namespace Frontier::ComponentClass
