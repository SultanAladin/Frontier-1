//============================================================================================================================================
//                                                            POPULATIONINDEX.H
//============================================================================================================================================
// 🧩 Generationally versioned slot index — the population every owner of the document sits inside.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      OCCUPANCY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which slots are occupied. The free set is the complement and is never stored separately.
/// note  Two representations of one fact diverge, and the divergence is discovered by whichever reader
///       trusted the stale one.
/// tag   owning
class OccupancyIndex
{
public:

    /// 🧩 Declares a slot occupied.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Occupy(std::uint32_t SlotIndex);

    /// 🧩 Declares a slot free.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Release(std::uint32_t SlotIndex);

    /// 🧩 Whether a slot is occupied.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Occupied(std::uint32_t SlotIndex) const;

    /// 🧩 How many slots the index spans, occupied or not.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t SpannedCount() const;

private:

    std::vector<std::uint64_t>  OccupancyWords;   // [-] - one bit per slot, least significant first
    std::uint32_t               SpannedSlots = 0u; // [-] - slots the index currently spans
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE POPULATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The slot index with generational identity. Every owner in the document is one slot here.
/// note  🔴 A reference held across a deletion resolves to absent rather than to whatever later took the
///       slot. That is what makes references safe without reference counting.
/// tag   owning
class PopulationIndex
{
public:

    /// 🧩 Registers one owner and issues its identity.
    /// out   OwnerIdentity [-]  slot ordinal paired with the generation now held
    /// err   refuses with ExtentExhausted when the population reaches its declared ceiling
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<OwnerIdentity> Register();

    /// 🧩 Withdraws one owner and advances the slot's generation.
    /// in    Subject  [-]  the identity to withdraw
    /// out   Result  [-]  refuses with IdentityStale when the identity no longer resolves
    /// post  every reference carrying the prior generation resolves to absent
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Withdraw(OwnerIdentity Subject);

    /// 🧩 Whether an identity still names the owner it was registered for.
    /// in    Subject  [-]  the identity to resolve
    /// out   Resolved [-]  false for a stale generation and for an unoccupied slot alike
    /// note  Comparison is an integer test, at Exact. An identity that collides is not an identity.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Resolve(OwnerIdentity Subject) const;

    /// 🧩 How many owners are registered.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t RegisteredCount() const;

private:

    static constexpr std::uint32_t PopulationLimit = 1048576u;   // [-] - slots the population may span

    std::vector<std::uint32_t>  SlotGenerations;      // [-] - current generation per slot; one-based
    std::vector<std::uint32_t>  ReleasedIndexs;     // [-] - slots free for reuse, most recent first
    OccupancyIndex              Occupancy;            // [-] - which of them are occupied
    std::uint32_t               OccupiedCount = 0u;   // [-] - registered owners
};

}   // namespace Slate
