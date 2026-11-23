//============================================================================================================================================
//                                                            ENROLLMENTINDEX.H
//============================================================================================================================================
// 🧩 Which slots are registered in a named subset, compressed by interval rather than stored per owner.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE NAMED SUBSETS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every subset the outliner declares, and where each one's mutations are recorded.
/// note  ⚠️ `MembershipDomain` and `MembershipIndex` are retired spellings. the earlier broad geographic noun is retired and the
///        mechanism is enrollment — `12` §3.
/// note  🔴 `12` §11: every subset mutation is a transaction. What differs between these is where the
///        transaction is recorded, not whether there is one. Selection is recorded in `SelectionSequence`
///        and is session-scoped; the other three are recorded in `RevisionSequence` and scrubbed by undo.
/// tag   guarantee
enum class SubsetSubject : std::uint32_t
{
    Selection           = 0u,   // [-] - recorded in SelectionSequence, session-scoped
    VisibilityExclusion = 1u,   // [-] - recorded in RevisionSequence, scrubbed by undo
    Isolation           = 2u,   // [-] - recorded in RevisionSequence, scrubbed by undo
    Lock                = 3u,   // [-] - recorded in RevisionSequence, scrubbed by undo
    SubsetCount         = 4u    // [-] - the closed count, never a subset
};

/// 🧩 One run of consecutively registered slots, inclusive at both ends.
/// note  💾 A subset over a scene is overwhelmingly contiguous in row order, so a run is the storage that
///        matches the shape of the fact. Storing it densely costs a bit per owner per subset and a linear
///        comparison to answer, and `12` §6 has room for neither at a million owners.
/// tag   nonallocating, nonthrowing
struct RegisteredInterval
{
    std::uint32_t  FirstIndex = 0u;   // [-] - first registered slot ordinal of the run
    std::uint32_t  LastIndex  = 0u;   // [-] - last of it; equal to the first for a single slot
};

//------------------------------------------------------------------------------------------------------------------------
//                                              INTERVAL ENROLMENT, ON ITS OWN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Registers one ordinal into a sorted run of intervals, merging where it abuts.
/// in    Runs     [-]  sorted, never touching; amended in place
/// in    Index  [-]  the ordinal to register
/// out   Sampled  [-]  false when the ordinal was already registered, so a caller may count arrivals
/// note  🔴 Declared apart from `RegistrationIndex` because `38` §3 registers **faces and vertices** by this same
///        mechanism and neither is a slot of the document population. One implementation both read is the whole
///        point: two interval implementations that must agree are one that will not.
/// cost  🚩
/// tag   api, nonthrowing
bool RegisterInterval(std::vector<RegisteredInterval>& Runs, std::uint32_t Index);

/// 🧩 Whether one ordinal is registered in a sorted run of intervals.
/// out   Registered  [-]  answered by a search over the runs, never over the ordinals
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool IntervalRegistered(const std::vector<RegisteredInterval>& Runs, std::uint32_t Index);

//------------------------------------------------------------------------------------------------------------------------
//                                                   MUTUAL EXCLUSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether two subsets may hold one owner at once.
/// in    LeftSubset   [-]  one subset
/// in    RightSubset  [-]  the other
/// out   Exclusive    [-]  true when an owner may not be registered in both
/// note  🔴 Isolation and visibility exclusion are mutually exclusive: an owner isolated to be the only
///        one visible cannot also be excluded from visibility. `12` §10 rules multi-enrollment in mutually
///        exclusive subsets rejected at commit rather than resolved by a precedence nobody declared.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool SubsetsExclusive(SubsetSubject LeftSubset, SubsetSubject RightSubset)
{
    return (LeftSubset == SubsetSubject::Isolation           && RightSubset == SubsetSubject::VisibilityExclusion)
        || (LeftSubset == SubsetSubject::VisibilityExclusion && RightSubset == SubsetSubject::Isolation);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE ENROLLMENTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every named subset over one population, each held as an ordered run of intervals.
/// note  The runs stay sorted and never touch, so an registration is a search and a merge rather than an append.
///        Two runs that abut are one run: leaving them apart grows the storage without adding a fact to it.
/// tag   owning
class RegistrationIndex
{
public:

    /// 🧩 Registers one owner in a subset.
    /// in    Subject         [-]  the owner
    /// in    RegisteredSubset  [-]  which subset
    /// out   Result         [-]  refuses with IdentityStale for an undeclared identity, and with
    ///                            ContentUnsupported when a mutually exclusive subset already holds it
    /// note  🔴 The exclusion refusal is decided before anything is written, so a rejected registration leaves
    ///        no partial state behind. `12` §10 rejects at commit and resolves nothing silently.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Register(OwnerIdentity Subject, SubsetSubject RegisteredSubset);

    /// 🧩 Withdraws one owner from a subset, dividing the run it sat inside.
    /// in    Subject         [-]  the owner
    /// in    RegisteredSubset  [-]  which subset
    /// out   Result         [-]  refuses with IdentityStale when the owner was not registered
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Unenrol(OwnerIdentity Subject, SubsetSubject RegisteredSubset);

    /// 🧩 Withdraws one owner from every subset — the subset half of invariant 8.
    /// in    Subject  [-]  the owner being retired
    /// cost  🚩
    /// tag   api, nonthrowing
    void UnenrolEverywhere(OwnerIdentity Subject);

    /// 🧩 Whether one owner is registered in a subset.
    /// in    Subject         [-]  the owner
    /// in    RegisteredSubset  [-]  which subset
    /// out   Registered        [-]  answered by interval comparison over the runs
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Registered(OwnerIdentity Subject, SubsetSubject RegisteredSubset) const;

    /// 🧩 The runs of one subset, in ascending slot order.
    /// in    RegisteredSubset  [-]  which subset
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<RegisteredInterval>& Intervals(SubsetSubject RegisteredSubset) const;

    /// 🧩 How many owners one subset holds.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t RegisteredCount(SubsetSubject RegisteredSubset) const;

    /// 🧩 Empties one subset entirely.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim(SubsetSubject RegisteredSubset);

    /// 🧩 🔍 Whether every registered slot is occupied at the current generation — invariant 6.
    /// in    Generations  [-]  the current generation per slot, zero where the slot is vacant
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    bool RegistrationsOccupied(const std::vector<std::uint32_t>& Generations) const;

private:

    static constexpr std::size_t SubsetSpan = static_cast<std::size_t>(SubsetSubject::SubsetCount);

    std::vector<RegisteredInterval>  SubsetIntervals[SubsetSpan] = {};   // [-] - sorted, never touching
    std::uint32_t                  SubsetCounts[SubsetSpan]    = {};   // [-] - registered owners per subset
};

}   // namespace Slate
