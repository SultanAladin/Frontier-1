/*====================================================================================================================================
                                                         RECORDSTORE.H
====================================================================================================================================*/
// 🧩 Dense slot store with generation-checked tokens (TokenIssuer / TokenAuthentication) and a vacancy table

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOKENS
//------------------------------------------------------------------------------------------------------------------------

// 💡 A token is (slot, generation). Reclaiming a slot advances its generation, so a token held across a deletion is
//    detected as stale instead of silently aliasing the slot's next occupant — the failure mode of bare indices
//    (gap G7: a subtract renumbered 8 of 12 edges).
template <typename Tag>
struct RecordToken
{
    uint32_t Slot       = UINT32_MAX;                                                        // [idx] - Slot in the store
    uint32_t Generation = 0;                                                                 // [-]   - Occupancy generation

    [[nodiscard]] constexpr bool PresentCondition() const noexcept { return Slot != UINT32_MAX; }
    constexpr bool operator==(const RecordToken&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORE
//------------------------------------------------------------------------------------------------------------------------

template <typename Tag, typename Profile>
class RecordStore
{
public:
    using Token = RecordToken<Tag>;

    // TokenIssuer: place a profile in a vacant slot (or append) and issue its token.
    Token Issue(Profile Payload)
    {
        if (!VacancyTable.empty())
        {
            const uint32_t Slot = VacancyTable.back();                                       // [idx] - Recycled slot
            VacancyTable.pop_back();
            SlotTable[Slot].Payload       = std::move(Payload);
            SlotTable[Slot].OccupiedState = true;
            return { Slot, SlotTable[Slot].Generation };
        }
        SlotTable.push_back({ std::move(Payload), 0u, true });
        return { static_cast<uint32_t>(SlotTable.size() - 1), 0u };
    }

    // TokenAuthentication: the token names a live occupant of the generation it was issued for.
    [[nodiscard]] bool Authenticate(Token Candidate) const noexcept
    {
        return Candidate.Slot < SlotTable.size() && SlotTable[Candidate.Slot].OccupiedState &&
               SlotTable[Candidate.Slot].Generation == Candidate.Generation;
    }

    // ⚠️ Callers must authenticate first when the token's provenance is external; internal links are audited instead.
    [[nodiscard]] Profile&       Resolve(Token Target)       noexcept { return SlotTable[Target.Slot].Payload; }
    [[nodiscard]] const Profile& Resolve(Token Target) const noexcept { return SlotTable[Target.Slot].Payload; }

    // GarbageReclamation of one slot: advance the generation and record the vacancy.
    void Reclaim(Token Target)
    {
        if (!Authenticate(Target)) return;
        SlotTable[Target.Slot].OccupiedState = false;
        SlotTable[Target.Slot].Generation   += 1;
        SlotTable[Target.Slot].Payload       = Profile{};
        VacancyTable.push_back(Target.Slot);
    }

    // Enumerate live tokens in slot order (deterministic).
    void Enumerate(const std::function<void(Token)>& Visitor) const
    {
        for (uint32_t Slot = 0; Slot < SlotTable.size(); ++Slot)
        {
            if (SlotTable[Slot].OccupiedState) Visitor({ Slot, SlotTable[Slot].Generation });
        }
    }

    // Token of the current occupant of a slot (persistent names are keyed by slot; the generation is re-attached here).
    [[nodiscard]] Token TokenAt(uint32_t Slot) const noexcept { return { Slot, Slot < SlotTable.size() ? SlotTable[Slot].Generation : 0u }; }

    [[nodiscard]] size_t OccupiedCount() const noexcept { return SlotTable.size() - VacancyTable.size(); }

private:
    struct SlotEntry
    {
        Profile  Payload       = {};                                                         // [-] - Occupant profile
        uint32_t Generation    = 0;                                                          // [-] - Occupancy generation
        bool     OccupiedState = false;                                                      // [-] - Slot holds a live occupant
    };

    std::vector<SlotEntry> SlotTable;                                                        // [-] - Dense slots
    std::vector<uint32_t>  VacancyTable;                                                     // [idx] - Reclaimed slots
};

} // namespace Frontier::GeometrySpace::Parametric
