//============================================================================================================================================
//                                                              TILESPACE.CPP
//============================================================================================================================================
// 🧩 Reserve, release into quarantine, and the reclamation deferred by the recording slot count.

#include "SlateCompute/Compute/TileSpace/Api/TileSpace.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TileSpace::ReserveTileSpace(std::uint32_t SlotLimit_, std::uint32_t BytesPerTexel)
{
    if (SlotLimit_ == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a index of no slot backs nothing" });

    if (BytesPerTexel == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a texel of no width stores nothing" });

    Limit = SlotLimit_;

    // 📐 The apron is counted into the stored extent rather than added at the transfer site, because a tile
    //    whose apron is budgeted separately is one whose byte offsets overlap its neighbour's border by four
    //    texels on each side — and the overlap reads as a fringe rather than as an arithmetic error.
    TileBytes = static_cast<std::uint64_t>(StoredTexelsPerEdge)
              * static_cast<std::uint64_t>(StoredTexelsPerEdge)
              * static_cast<std::uint64_t>(BytesPerTexel);

    Conditions.assign(Limit, SlotCondition::Free);
    ReleasedAt.assign(Limit, 0u);

    FreeIndexs.clear();
    FreeIndexs.reserve(Limit);

    // 📝 Pushed in descending order so the first claim takes slot zero. A index that handed out its highest
    //    slot first would be correct and would make every measurement of it read backwards.
    for (std::uint32_t Index = Limit; Index-- > 0u;)
        FreeIndexs.push_back(Index);

    HeldSlots     = 0u;
    QuarantinedSlots = 0u;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLAIM AND RELEASE
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> TileSpace::Reserve()
{
    if (FreeIndexs.empty())
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "every slot is claimed or quarantined" });
    }

    const std::uint32_t Held = FreeIndexs.back();
    FreeIndexs.pop_back();

    Conditions[Held] = SlotCondition::Held;
    ++HeldSlots;

    return Deliver<std::uint32_t>::Result(Held);
}

Deliver<bool> TileSpace::Release(std::uint32_t SlotIndex, std::uint64_t RecordingIndex)
{
    if (SlotIndex >= Limit)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such slot" });

    if (Conditions[SlotIndex] != SlotCondition::Held)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the slot is not claimed" });

    Conditions[SlotIndex]   = SlotCondition::Quarantined;
    ReleasedAt[SlotIndex] = RecordingIndex;

    --HeldSlots;
    ++QuarantinedSlots;

    return Deliver<bool>::Result(true);
}

std::uint32_t TileSpace::Reclaim(std::uint64_t RecordingIndex)
{
    if (QuarantinedSlots == 0u)
        return 0u;

    std::uint32_t Reclaimed = 0u;

    for (std::uint32_t SlotIndex = 0u; SlotIndex < Limit; ++SlotIndex)
    {
        if (Conditions[SlotIndex] != SlotCondition::Quarantined)
            continue;

        // 🔴 `20` §5: reclamation is deferred by the recording slot count. The comparison is written as a subtraction
        //    from the current rotation rather than as an addition to the release, because the release ordinal
        //    plus the depth overflows at the end of the representable range and the current ordinal does not.
        if (RecordingIndex < ReleasedAt[SlotIndex]
         || RecordingIndex - ReleasedAt[SlotIndex] < RecordingSlotCount)
        {
            continue;
        }

        Conditions[SlotIndex] = SlotCondition::Free;
        FreeIndexs.push_back(SlotIndex);

        --QuarantinedSlots;
        ++Reclaimed;
    }

    return Reclaimed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint64_t> TileSpace::ByteOffsetOf(std::uint32_t SlotIndex) const
{
    if (SlotIndex >= Limit)
        return Deliver<std::uint64_t>::Refuse({ RefusalReason::ContentUnsupported, "no such slot" });

    return Deliver<std::uint64_t>::Result(static_cast<std::uint64_t>(SlotIndex) * TileBytes);
}

std::uint64_t TileSpace::StoredBytesPerTile() const { return TileBytes; }

std::uint64_t TileSpace::BackingBytes() const
{
    return static_cast<std::uint64_t>(Limit) * TileBytes;
}

std::uint32_t TileSpace::SlotLimit() const       { return Limit;          }
std::uint32_t TileSpace::HeldCount() const      { return HeldSlots;     }
std::uint32_t TileSpace::QuarantinedCount() const  { return QuarantinedSlots; }

std::uint32_t TileSpace::FreeCount() const
{
    return static_cast<std::uint32_t>(FreeIndexs.size());
}

bool TileSpace::InteractionConsistent() const
{
    std::uint32_t Free = 0u;
    std::uint32_t Held = 0u;
    std::uint32_t Kept = 0u;

    for (const SlotCondition Condition_ : Conditions)
    {
        if (Condition_ == SlotCondition::Free)             ++Free;
        else if (Condition_ == SlotCondition::Held)     ++Held;
        else                                             ++Kept;
    }

    return Free == FreeIndexs.size()
        && Held == HeldSlots
        && Kept == QuarantinedSlots
        && Free + Held + Kept == Limit;
}

}   // namespace Slate
