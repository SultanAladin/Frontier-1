//============================================================================================================================================
//                                                             TRIGRAMINDEX.CPP
//============================================================================================================================================
// 🧩 Trigram folding and entry, the rarest-run narrowing, and the exact confirmation over it.

#include "SlateDocument/Document/TrigramIndex/Api/TrigramIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     FOLDED NAMES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The trigram ordinals one name occupies, in ascending order and without repetition. A repeated trigram
//    would enter the same run twice and withdraw from it once.
std::vector<std::uint32_t> FoldedTrigrams(const std::string& Declared)
{
    std::vector<std::uint32_t> Folded;

    if (Declared.size() < 3u)
        return Folded;

    Folded.reserve(Declared.size() - 2u);

    for (std::size_t Index = 0u; Index + 3u <= Declared.size(); ++Index)
    {
        const std::uint32_t Trigram = FoldedIndex(Declared[Index])      * TrigramAlphabet * TrigramAlphabet
                                    + FoldedIndex(Declared[Index + 1u]) * TrigramAlphabet
                                    + FoldedIndex(Declared[Index + 2u]);

        bool Repeated = false;

        for (const std::uint32_t Held : Folded)
        {
            if (Held == Trigram)
            {
                Repeated = true;
                break;
            }
        }

        if (!Repeated)
            Folded.push_back(Trigram);
    }

    return Folded;
}

// 📝 The exact confirmation. Case-folded so it agrees with the folding the trigrams used — a narrowing that
//    matched case-insensitively and confirmed case-sensitively drops results the index promised.
bool NameContains(const std::string& Declared, const std::string& Sought)
{
    if (Sought.empty())
        return true;

    if (Sought.size() > Declared.size())
        return false;

    for (std::size_t Start = 0u; Start + Sought.size() <= Declared.size(); ++Start)
    {
        std::size_t Matched = 0u;

        while (Matched < Sought.size()
            && FoldedIndex(Declared[Start + Matched]) == FoldedIndex(Sought[Matched]))
        {
            ++Matched;
        }

        if (Matched == Sought.size())
            return true;
    }

    return false;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     DECLARATION
//------------------------------------------------------------------------------------------------------------------------

void TrigramIndex::Enter(std::uint32_t SlotIndex, const std::string& Declared)
{
    if (TrigramRuns.empty())
        TrigramRuns.resize(TrigramSpan);

    for (const std::uint32_t Trigram : FoldedTrigrams(Declared))
    {
        std::vector<std::uint32_t>& Run = TrigramRuns[Trigram];

        std::size_t Lower = 0u;
        std::size_t Upper = Run.size();

        while (Lower < Upper)
        {
            const std::size_t Middle = Lower + (Upper - Lower) / 2u;

            if (Run[Middle] < SlotIndex)
                Lower = Middle + 1u;
            else
                Upper = Middle;
        }

        if (Lower < Run.size() && Run[Lower] == SlotIndex)
            continue;

        Run.insert(Run.begin() + static_cast<std::ptrdiff_t>(Lower), SlotIndex);
    }
}

Deliver<bool> TrigramIndex::Declare(OwnerIdentity Subject, const std::string& Declared)
{
    if (!Subject.IdentityDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "an undeclared identity carries no name" });

    // 🔴 The former name's entries are withdrawn before the new ones are entered. Skipping the withdrawal is
    //    exactly the defect step ⑦ exists to prevent: the owner stays findable under a name it lost.
    Withdraw(Subject);

    const std::size_t Required = static_cast<std::size_t>(Subject.SlotIndex) + 1u;

    if (Required > DeclaredNames.size())
    {
        DeclaredNames.resize(Required);
        NamedIdentities.resize(Required);
    }

    DeclaredNames[Subject.SlotIndex]   = Declared;
    NamedIdentities[Subject.SlotIndex] = Subject;

    if (!Declared.empty())
        ++NamedOwners;

    Enter(Subject.SlotIndex, Declared);

    return Deliver<bool>::Result(true);
}

void TrigramIndex::Withdraw(OwnerIdentity Subject)
{
    if (!Subject.IdentityDeclared() || Subject.SlotIndex >= DeclaredNames.size())
        return;

    const std::string Departing = DeclaredNames[Subject.SlotIndex];

    if (Departing.empty())
        return;

    for (const std::uint32_t Trigram : FoldedTrigrams(Departing))
    {
        std::vector<std::uint32_t>& Run = TrigramRuns[Trigram];

        for (std::size_t Index = 0u; Index < Run.size(); ++Index)
        {
            if (Run[Index] == Subject.SlotIndex)
            {
                Run.erase(Run.begin() + static_cast<std::ptrdiff_t>(Index));
                break;
            }
        }
    }

    DeclaredNames[Subject.SlotIndex].clear();
    NamedIdentities[Subject.SlotIndex] = OwnerIdentity{};
    --NamedOwners;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE NARROWING
//------------------------------------------------------------------------------------------------------------------------

std::vector<OwnerIdentity> TrigramIndex::Narrow(const std::string& Sought) const
{
    std::vector<OwnerIdentity> Confirmed;

    if (Sought.empty())
        return Confirmed;

    const std::vector<std::uint32_t> Folded = FoldedTrigrams(Sought);

    // 📝 Text shorter than one trigram narrows to nothing, so it is confirmed against every declared name
    //    rather than answered as absent. A one-character search returning nothing looks like a broken index.
    if (Folded.empty() || TrigramRuns.empty())
    {
        for (std::uint32_t SlotIndex = 0u; SlotIndex < DeclaredNames.size(); ++SlotIndex)
        {
            if (!DeclaredNames[SlotIndex].empty() && NameContains(DeclaredNames[SlotIndex], Sought))
                Confirmed.push_back(NamedIdentities[SlotIndex]);
        }

        return Confirmed;
    }

    // 📝 Narrowed by the rarest run rather than by intersecting every run. One run is already small, and the
    //    intersection costs more than confirming the difference between it and the answer.
    const std::vector<std::uint32_t>* Narrowest = &TrigramRuns[Folded[0]];

    for (const std::uint32_t Trigram : Folded)
    {
        if (TrigramRuns[Trigram].size() < Narrowest->size())
            Narrowest = &TrigramRuns[Trigram];
    }

    // 🔴 The exact confirmation. A trigram set matches names that never contain the sought text — "arm" and
    //    "ram" share no trigram, but longer text easily produces candidates that do not match at all.
    for (const std::uint32_t SlotIndex : *Narrowest)
    {
        if (SlotIndex >= DeclaredNames.size())
            continue;

        if (!NameContains(DeclaredNames[SlotIndex], Sought))
            continue;

        Confirmed.push_back(NamedIdentities[SlotIndex]);
    }

    return Confirmed;
}

const std::string& TrigramIndex::DeclaredName(OwnerIdentity Subject) const
{
    if (!Subject.IdentityDeclared() || Subject.SlotIndex >= DeclaredNames.size())
        return AbsentName;

    return DeclaredNames[Subject.SlotIndex];
}

std::uint32_t TrigramIndex::NamedCount() const
{
    return NamedOwners;
}

}   // namespace Slate
