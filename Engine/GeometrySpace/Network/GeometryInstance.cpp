/*============================================================================================================================================
                                                           GEOMETRYINSTANCE.CPP
============================================================================================================================================*/
// 🧩 Implementation of GeometryInstanceRegistry recycling indexed slots and maintaining non-kinship Enclosing/FirstNested/NextPeer links

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/GeometryInstance.h"

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  ALLOCATION & RECYCLING
//------------------------------------------------------------------------------------------------------------------------

GeometryInstanceToken GeometryInstanceRegistry::IssueInstance(
    std::string_view                     Title,
    uint32_t                             NetworkSlotIndex,
    const Mathematics::SpatialTransform& InitialTransform)
{
    uint32_t ZeroBasedSlot = 0u;
    uint32_t NextGen       = 1u;

    if (!RecycledSlots.empty())
    {
        ZeroBasedSlot = RecycledSlots.back();
        RecycledSlots.pop_back();
        NextGen = Slots[ZeroBasedSlot].Identity.Token.Generation + 1u;
        if (NextGen == 0u)
        {
            NextGen = 1u;
        }
    }
    else
    {
        ZeroBasedSlot = static_cast<uint32_t>(Slots.size());
        Slots.emplace_back();
        SlotOccupiedMask.push_back(0u);
    }

    GeometryInstance& Slot            = Slots[ZeroBasedSlot];
    Slot.Identity                     = GeometryInstanceIdentity{};
    Slot.Identity.Token               = GeometryInstanceToken{ ZeroBasedSlot + 1u, NextGen };
    Slot.Identity.NetworkSlotIndex    = NetworkSlotIndex;
    Slot.Transform                    = InitialTransform;
    Slot.InstanceTitle                = Title.empty() ? "GeometryInstance" : std::string(Title);
    SlotOccupiedMask[ZeroBasedSlot]   = 1u;
    ++LiveCount;

    return Slot.Identity.Token;
}

bool GeometryInstanceRegistry::ReclaimInstance(GeometryInstanceToken Token) noexcept
{
    if (!AuthenticateToken(Token))
    {
        return false;
    }

    const uint32_t ZeroBasedSlot = Token.SlotIndex - 1u;
    DetachFromEnclosingLinks(ZeroBasedSlot);

    // 📝 Promote any nested instances under this slot back to root level before reclaiming
    uint32_t NestedWalk = Slots[ZeroBasedSlot].Identity.FirstNestedIndex;
    while (NestedWalk != InvalidNetworkIndex && NestedWalk < Slots.size())
    {
        const uint32_t NextPeer = Slots[NestedWalk].Identity.NextPeerIndex;
        Slots[NestedWalk].Identity.EnclosingIndex = InvalidNetworkIndex;
        Slots[NestedWalk].Identity.NextPeerIndex  = InvalidNetworkIndex;
        Slots[NestedWalk].Identity.DepthLevel     = 0u;
        NestedWalk = NextPeer;
    }

    Slots[ZeroBasedSlot].Identity.FirstNestedIndex = InvalidNetworkIndex;
    SlotOccupiedMask[ZeroBasedSlot]                = 0u;
    RecycledSlots.push_back(ZeroBasedSlot);
    --LiveCount;
    return true;
}

bool GeometryInstanceRegistry::AttachNestedInstance(
    GeometryInstanceToken EnclosingToken,
    GeometryInstanceToken NestedToken) noexcept
{
    if (!AuthenticateToken(EnclosingToken) || !AuthenticateToken(NestedToken) || EnclosingToken == NestedToken)
    {
        return false;
    }

    const uint32_t EnclosingSlot = EnclosingToken.SlotIndex - 1u;
    const uint32_t NestedSlot    = NestedToken.SlotIndex - 1u;

    // 📝 Guard against cycles along the enclosing chain
    uint32_t Cursor = EnclosingSlot;
    while (Cursor != InvalidNetworkIndex && Cursor < Slots.size())
    {
        if (Cursor == NestedSlot)
        {
            return false;
        }
        Cursor = Slots[Cursor].Identity.EnclosingIndex;
    }

    DetachFromEnclosingLinks(NestedSlot);

    Slots[NestedSlot].Identity.EnclosingIndex = EnclosingSlot;
    Slots[NestedSlot].Identity.DepthLevel     = static_cast<uint16_t>(Slots[EnclosingSlot].Identity.DepthLevel + 1u);
    Slots[NestedSlot].Identity.NextPeerIndex  = Slots[EnclosingSlot].Identity.FirstNestedIndex;
    Slots[EnclosingSlot].Identity.FirstNestedIndex = NestedSlot;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool GeometryInstanceRegistry::AuthenticateToken(GeometryInstanceToken Token) const noexcept
{
    if (!Token.Valid())
    {
        return false;
    }
    const uint32_t ZeroBasedSlot = Token.SlotIndex - 1u;
    if (ZeroBasedSlot >= Slots.size() || SlotOccupiedMask[ZeroBasedSlot] == 0u)
    {
        return false;
    }
    return Slots[ZeroBasedSlot].Identity.Token.Generation == Token.Generation;
}

GeometryInstance* GeometryInstanceRegistry::ResolveInstance(GeometryInstanceToken Token) noexcept
{
    if (!AuthenticateToken(Token))
    {
        return nullptr;
    }
    return &Slots[Token.SlotIndex - 1u];
}

const GeometryInstance* GeometryInstanceRegistry::InspectInstance(GeometryInstanceToken Token) const noexcept
{
    if (!AuthenticateToken(Token))
    {
        return nullptr;
    }
    return &Slots[Token.SlotIndex - 1u];
}

uint32_t GeometryInstanceRegistry::QueryLiveInstanceCount() const noexcept
{
    return LiveCount;
}

std::vector<GeometryInstanceToken> GeometryInstanceRegistry::EnumerateLiveInstances() const
{
    std::vector<GeometryInstanceToken> Output;
    Output.reserve(LiveCount);
    for (std::size_t Idx = 0u; Idx < Slots.size(); ++Idx)
    {
        if (SlotOccupiedMask[Idx] != 0u)
        {
            Output.push_back(Slots[Idx].Identity.Token);
        }
    }
    return Output;
}

void GeometryInstanceRegistry::DetachFromEnclosingLinks(uint32_t ZeroBasedSlot) noexcept
{
    const uint32_t EnclosingSlot = Slots[ZeroBasedSlot].Identity.EnclosingIndex;
    if (EnclosingSlot == InvalidNetworkIndex || EnclosingSlot >= Slots.size())
    {
        return;
    }

    uint32_t* LinkPointer = &Slots[EnclosingSlot].Identity.FirstNestedIndex;
    while (*LinkPointer != InvalidNetworkIndex && *LinkPointer < Slots.size())
    {
        if (*LinkPointer == ZeroBasedSlot)
        {
            *LinkPointer = Slots[ZeroBasedSlot].Identity.NextPeerIndex;
            break;
        }
        LinkPointer = &Slots[*LinkPointer].Identity.NextPeerIndex;
    }

    Slots[ZeroBasedSlot].Identity.EnclosingIndex = InvalidNetworkIndex;
    Slots[ZeroBasedSlot].Identity.NextPeerIndex  = InvalidNetworkIndex;
    Slots[ZeroBasedSlot].Identity.DepthLevel     = 0u;
}

} // namespace Frontier::GeometrySpace
