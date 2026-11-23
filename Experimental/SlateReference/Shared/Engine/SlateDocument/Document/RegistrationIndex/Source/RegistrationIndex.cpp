//============================================================================================================================================
//                                                           ENROLLMENTINDEX.CPP
//============================================================================================================================================
// 🧩 Interval merging, division, and the exclusion refusal that precedes every write.

#include "SlateDocument/Document/RegistrationIndex/Api/RegistrationIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERVAL SEARCH
//------------------------------------------------------------------------------------------------------------------------

// 📝 The first run whose last ordinal is not below the subject. Every registration, withdrawal and test starts
//    here, so it is one routine rather than three loops that must agree.
static std::size_t LocateInterval(const std::vector<RegisteredInterval>& Runs, std::uint32_t SlotIndex)
{
    std::size_t Lower = 0u;
    std::size_t Upper = Runs.size();

    while (Lower < Upper)
    {
        const std::size_t Middle = Lower + (Upper - Lower) / 2u;

        if (Runs[Middle].LastIndex < SlotIndex)
            Lower = Middle + 1u;
        else
            Upper = Middle;
    }

    return Lower;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERVAL ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The merging body Register used to hold inline, lifted out so that `38`'s degeneracy registration reads the same
//    code rather than a second copy of the same reasoning.
bool RegisterInterval(std::vector<RegisteredInterval>& Runs, std::uint32_t Index)
{
    const std::size_t Located = LocateInterval(Runs, Index);

    if (Located < Runs.size() && Runs[Located].FirstIndex <= Index)
        return false;

    const bool AbutsBelow = Located != 0u && Runs[Located - 1u].LastIndex + 1u == Index;
    const bool AbutsAbove = Located < Runs.size() && Runs[Located].FirstIndex == Index + 1u;

    if (AbutsBelow && AbutsAbove)
    {
        Runs[Located - 1u].LastIndex = Runs[Located].LastIndex;
        Runs.erase(Runs.begin() + static_cast<std::ptrdiff_t>(Located));
    }
    else if (AbutsBelow)
    {
        Runs[Located - 1u].LastIndex = Index;
    }
    else if (AbutsAbove)
    {
        Runs[Located].FirstIndex = Index;
    }
    else
    {
        RegisteredInterval Incoming;
        Incoming.FirstIndex = Index;
        Incoming.LastIndex  = Index;

        Runs.insert(Runs.begin() + static_cast<std::ptrdiff_t>(Located), Incoming);
    }

    return true;
}

bool IntervalRegistered(const std::vector<RegisteredInterval>& Runs, std::uint32_t Index)
{
    const std::size_t Located = LocateInterval(Runs, Index);

    return Located < Runs.size() && Runs[Located].FirstIndex <= Index;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> RegistrationIndex::Register(OwnerIdentity Subject, SubsetSubject RegisteredSubset)
{
    if (!Subject.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "an undeclared identity registers in nothing" });

    // 🔴 Decided before anything is written. A rejected registration leaves no partial state, which is what
    //    lets the caller abandon its transaction rather than repair it.
    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(SubsetSubject::SubsetCount); ++Index)
    {
        const SubsetSubject Current = static_cast<SubsetSubject>(Index);

        if (SubsetsExclusive(RegisteredSubset, Current) && Registered(Subject, Current))
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "a mutually exclusive subset already holds the owner" });
        }
    }

    std::vector<RegisteredInterval>& Runs        = SubsetIntervals[static_cast<std::size_t>(RegisteredSubset)];

    // 📝 Extending an abutting run keeps the storage at one run per contiguous span. Two runs that touch
    //    carry no fact the merged run does not, and every later comparison pays for the extra entry.
    if (RegisterInterval(Runs, Subject.SlotIndex))
        ++SubsetCounts[static_cast<std::size_t>(RegisteredSubset)];

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      WITHDRAWAL
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> RegistrationIndex::Unenrol(OwnerIdentity Subject, SubsetSubject RegisteredSubset)
{
    if (!Subject.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "an undeclared identity registers in nothing" });

    std::vector<RegisteredInterval>& Runs        = SubsetIntervals[static_cast<std::size_t>(RegisteredSubset)];
    const std::uint32_t            SlotIndex = Subject.SlotIndex;
    const std::size_t              Located     = LocateInterval(Runs, SlotIndex);

    if (Located >= Runs.size() || Runs[Located].FirstIndex > SlotIndex)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the owner was not registered here" });

    const RegisteredInterval Held = Runs[Located];

    if (Held.FirstIndex == SlotIndex && Held.LastIndex == SlotIndex)
    {
        Runs.erase(Runs.begin() + static_cast<std::ptrdiff_t>(Located));
    }
    else if (Held.FirstIndex == SlotIndex)
    {
        Runs[Located].FirstIndex = SlotIndex + 1u;
    }
    else if (Held.LastIndex == SlotIndex)
    {
        Runs[Located].LastIndex = SlotIndex - 1u;
    }
    else
    {
        // 📝 A withdrawal from the interior divides one run into two. This is the only operation that grows
        //    the run count, and it grows it by exactly one.
        Runs[Located].LastIndex = SlotIndex - 1u;

        RegisteredInterval Upper;
        Upper.FirstIndex = SlotIndex + 1u;
        Upper.LastIndex  = Held.LastIndex;

        Runs.insert(Runs.begin() + static_cast<std::ptrdiff_t>(Located) + 1, Upper);
    }

    --SubsetCounts[static_cast<std::size_t>(RegisteredSubset)];

    return Deliver<bool>::Result(true);
}

void RegistrationIndex::UnenrolEverywhere(OwnerIdentity Subject)
{
    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(SubsetSubject::SubsetCount); ++Index)
        Discard(Unenrol(Subject, static_cast<SubsetSubject>(Index)));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

bool RegistrationIndex::Registered(OwnerIdentity Subject, SubsetSubject RegisteredSubset) const
{
    if (!Subject.IdentityDeclared())
        return false;

    return IntervalRegistered(SubsetIntervals[static_cast<std::size_t>(RegisteredSubset)], Subject.SlotIndex);
}

const std::vector<RegisteredInterval>& RegistrationIndex::Intervals(SubsetSubject RegisteredSubset) const
{
    return SubsetIntervals[static_cast<std::size_t>(RegisteredSubset)];
}

std::uint32_t RegistrationIndex::RegisteredCount(SubsetSubject RegisteredSubset) const
{
    return SubsetCounts[static_cast<std::size_t>(RegisteredSubset)];
}

void RegistrationIndex::Reclaim(SubsetSubject RegisteredSubset)
{
    SubsetIntervals[static_cast<std::size_t>(RegisteredSubset)].clear();
    SubsetCounts[static_cast<std::size_t>(RegisteredSubset)] = 0u;
}

bool RegistrationIndex::RegistrationsOccupied(const std::vector<std::uint32_t>& Generations) const
{
    for (std::size_t Subset = 0u; Subset < SubsetSpan; ++Subset)
    {
        for (const RegisteredInterval& Run : SubsetIntervals[Subset])
        {
            for (std::uint32_t Index = Run.FirstIndex; Index <= Run.LastIndex; ++Index)
            {
                if (Index >= Generations.size() || Generations[Index] == 0u)
                    return false;
            }
        }
    }

    return true;
}

}   // namespace Slate
