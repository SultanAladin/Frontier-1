//============================================================================================================================================
//                                                              TRIGRAMINDEX.H
//============================================================================================================================================
// 🧩 Name search that narrows by trigram and then confirms exactly — approximate index, exact answer.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TRIGRAMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A trigram is folded to a fixed ordinal over a 40-symbol alphabet — the twenty-six letters, the ten
//    digits, and four spellings for everything else. Folding rather than hashing means two distinct trigrams
//    never share an ordinal, so a candidate run is narrowed by a fact rather than by a probability.
inline constexpr std::uint32_t TrigramAlphabet = 40u;                                                 // [-]
inline constexpr std::uint32_t TrigramSpan     = TrigramAlphabet * TrigramAlphabet * TrigramAlphabet; // [-] - 64000

/// 🧩 Folds one character to its alphabet ordinal, case-insensitively.
/// in    Incoming  [-]  one character of a name
/// out   Index   [-]  below TrigramAlphabet; everything unrecognised folds to the last four
/// note  Case folding is what makes a search for "arm" find "Arm". The artist typed a name, not a spelling.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr std::uint32_t FoldedIndex(char Incoming)
{
    if (Incoming >= 'a' && Incoming <= 'z')
        return static_cast<std::uint32_t>(Incoming - 'a');

    if (Incoming >= 'A' && Incoming <= 'Z')
        return static_cast<std::uint32_t>(Incoming - 'A');

    if (Incoming >= '0' && Incoming <= '9')
        return 26u + static_cast<std::uint32_t>(Incoming - '0');

    if (Incoming == ' ' || Incoming == '_')
        return 36u;

    if (Incoming == '.' || Incoming == '-')
        return 37u;

    if (Incoming == '/' || Incoming == '\\')
        return 38u;

    return 39u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE NAME SEARCH
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Trigram narrowing over the population's names, with the exact confirmation that follows it.
/// note  🔴 `12` §3: approximate index, exact confirmation. An index that answers alone will eventually
///        answer wrongly — a trigram set matches names that do not contain the sought text at all, and
///        presenting those as results is a search the artist stops trusting.
/// note  🔴 A rename re-derives its entries within the same tick — `12` §4 step ⑦. Search that answers with
///        a name the artist has already changed is worse than search that finds nothing.
/// tag   owning
class TrigramIndex
{
public:

    /// 🧩 Declares one owner's name, replacing whatever it held before.
    /// in    Subject   [-]  the owner
    /// in    Declared  [-]  the name the artist gave it
    /// out   Result   [-]  refuses with IdentityStale for an undeclared identity
    /// post  every trigram run mentioning this owner describes the new name only
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Declare(OwnerIdentity Subject, const std::string& Declared);

    /// 🧩 Withdraws one owner's name and every trigram entry that reached it.
    /// in    Subject  [-]  the owner being retired or renamed
    /// cost  🚩
    /// tag   api, nonthrowing
    void Withdraw(OwnerIdentity Subject);

    /// 🧩 Narrows to the owners whose names contain the sought text, then confirms each exactly.
    /// in    Sought      [-]  what the artist typed; shorter than a trigram falls back to confirmation alone
    /// out   Confirmed   [-]  owners whose names genuinely contain it, in ascending slot order
    /// note  The narrowing is the rarest trigram's run rather than the intersection of all of them: one run
    ///        is already small, and intersecting costs more than confirming the difference.
    /// note  🔴 Every returned identity is the one Declare was given, generation included. Reconstructing an
    ///        identity from a slot ordinal alone would hand back a reference that resolves to whatever later
    ///        took the slot, which is the defect the generation exists to prevent.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::vector<OwnerIdentity> Narrow(const std::string& Sought) const;

    /// 🧩 One owner's declared name, empty when it has none.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::string& DeclaredName(OwnerIdentity Subject) const;

    /// 🧩 How many owners carry a declared name.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t NamedCount() const;

private:

    void Enter(std::uint32_t SlotIndex, const std::string& Declared);

    std::vector<std::string>                  DeclaredNames;           // [-] - by slot ordinal; the exact text
    std::vector<OwnerIdentity>             NamedIdentities;         // [-] - as declared; never reconstructed
    std::vector<std::vector<std::uint32_t>>   TrigramRuns;             // [-] - slot ordinals per trigram ordinal
    std::string                               AbsentName     = {};    // [-] - returned for an unnamed owner
    std::uint32_t                             NamedOwners = 0u;    // [-] - owners carrying a name
};

}   // namespace Slate
