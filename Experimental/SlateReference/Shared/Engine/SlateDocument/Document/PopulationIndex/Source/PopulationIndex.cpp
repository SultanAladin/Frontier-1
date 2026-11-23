//============================================================================================================================================
//                                                           POPULATIONINDEX.CPP
//============================================================================================================================================
// 🧩 Slot issuance, withdrawal and generational resolution.

#include "SlateDocument/Document/PopulationIndex/Api/PopulationIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      OCCUPANCY
//------------------------------------------------------------------------------------------------------------------------

void OccupancyIndex::Occupy(std::uint32_t SlotIndex)
{
    const std::size_t WordIndex = SlotIndex / 64u;
    const std::uint64_t BitMask   = 1ull << (SlotIndex % 64u);

    if (WordIndex >= OccupancyWords.size())
        OccupancyWords.resize(WordIndex + 1u, 0ull);

    OccupancyWords[WordIndex] |= BitMask;

    if (SlotIndex + 1u > SpannedSlots)
        SpannedSlots = SlotIndex + 1u;
}

void OccupancyIndex::Release(std::uint32_t SlotIndex)
{
    const std::size_t WordIndex = SlotIndex / 64u;

    if (WordIndex >= OccupancyWords.size())
        return;

    OccupancyWords[WordIndex] &= ~(1ull << (SlotIndex % 64u));
}

bool OccupancyIndex::Occupied(std::uint32_t SlotIndex) const
{
    const std::size_t WordIndex = SlotIndex / 64u;

    if (WordIndex >= OccupancyWords.size())
        return false;

    return (OccupancyWords[WordIndex] & (1ull << (SlotIndex % 64u))) != 0ull;
}

std::uint32_t OccupancyIndex::SpannedCount() const
{
    return SpannedSlots;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<OwnerIdentity> PopulationIndex::Register()
{
    std::uint32_t SlotIndex = 0u;

    if (!ReleasedIndexs.empty())
    {
        // 📝 A released slot is reused with its generation already advanced by Withdraw, so the identity
        //    registered here can never equal one registered for the slot's previous owner.
        SlotIndex = ReleasedIndexs.back();
        ReleasedIndexs.pop_back();
    }
    else
    {
        if (SlotGenerations.size() >= PopulationLimit)
        {
            return Deliver<OwnerIdentity>::Refuse(
                { RefusalReason::ExtentExhausted, "the population reached its declared ceiling" });
        }

        SlotIndex = static_cast<std::uint32_t>(SlotGenerations.size());
        SlotGenerations.push_back(1u);
    }

    Occupancy.Occupy(SlotIndex);
    ++OccupiedCount;

    OwnerIdentity Registered;
    Registered.SlotIndex    = SlotIndex;
    Registered.SlotGeneration = SlotGenerations[SlotIndex];

    return Deliver<OwnerIdentity>::Result(Registered);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      WITHDRAWAL
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PopulationIndex::Withdraw(OwnerIdentity Subject)
{
    if (!Resolve(Subject))
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the identity no longer resolves" });

    Occupancy.Release(Subject.SlotIndex);

    // 📝 The generation advances on withdrawal, not on reuse. Every reference carrying the prior generation
    //    resolves to absent from this point, whether or not the slot is ever occupied again.
    ++SlotGenerations[Subject.SlotIndex];

    ReleasedIndexs.push_back(Subject.SlotIndex);
    --OccupiedCount;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

bool PopulationIndex::Resolve(OwnerIdentity Subject) const
{
    if (!Subject.IdentityDeclared())
        return false;

    if (Subject.SlotIndex >= SlotGenerations.size())
        return false;

    if (!Occupancy.Occupied(Subject.SlotIndex))
        return false;

    return SlotGenerations[Subject.SlotIndex] == Subject.SlotGeneration;
}

std::uint32_t PopulationIndex::RegisteredCount() const
{
    return OccupiedCount;
}

}   // namespace Slate
