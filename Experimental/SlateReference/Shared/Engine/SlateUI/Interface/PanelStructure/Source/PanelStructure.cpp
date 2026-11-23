//============================================================================================================================================
//                                                         PANELSTRUCTURE.CPP
//============================================================================================================================================
// 🧩 Division, withdrawal, assignment and proportional resizing of a bounded workspace partition.

#include "SlateUI/Interface/PanelStructure/Api/PanelStructure.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

void PanelStructure::ConstructPanelPartition(PanelSubject InitialSubject)
{
    Reset();
    Records[RootIndex].Occupied = true;
    Records[RootIndex].Subject  = InitialSubject;
}

void PanelStructure::Reset()
{
    for (std::uint32_t Index = 0u; Index < RecordLimit; ++Index)
        Records[Index] = PanelRecord{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DIVISION
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t PanelStructure::TakeVacant()
{
    for (std::uint32_t Index = 1u; Index < RecordLimit; ++Index)
    {
        if (!Records[Index].Occupied)
            return Index;
    }

    return RecordLimit;
}

Deliver<bool> PanelStructure::Divide(std::uint32_t LeafIndex,
                                     PanelDivisionAxis Axis,
                                     PanelDivisionSide VacantSide)
{
    if (LeafIndex >= RecordLimit || !Records[LeafIndex].Occupied || Records[LeafIndex].Divided)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "that ordinal names no leaf panel" });

    const std::uint32_t FirstSlot = TakeVacant();
    if (FirstSlot >= RecordLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no two panel slots remain" });

    Records[FirstSlot].Occupied = true;
    const std::uint32_t SecondSlot = TakeVacant();
    Records[FirstSlot].Occupied = false;

    if (SecondSlot >= RecordLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no two panel slots remain" });

    const PanelSubject DepartingSubject = Records[LeafIndex].Subject;
    const bool VacantTop = VacantSide == PanelDivisionSide::Minimum;

    Records[FirstSlot] = PanelRecord{ true, false,
                                      VacantTop ? PanelSubject::Vacant : DepartingSubject };
    Records[SecondSlot] = PanelRecord{ true, false,
                                       VacantTop ? DepartingSubject : PanelSubject::Vacant };

    PanelRecord& Divided = Records[LeafIndex];
    Divided.Divided       = true;
    Divided.Subject       = PanelSubject::Vacant;
    Divided.Axis          = Axis;
    Divided.MinimumFraction = 0.5f;
    Divided.Minimum  = FirstSlot;
    Divided.Maximum   = SecondSlot;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       WITHDRAWAL
//------------------------------------------------------------------------------------------------------------------------

bool PanelStructure::Encloses(std::uint32_t BranchIndex,
                              std::uint32_t SeekingIndex,
                              std::uint32_t& EnclosingIndex,
                              bool& MinimumSide) const
{
    if (BranchIndex >= RecordLimit || !Records[BranchIndex].Occupied || !Records[BranchIndex].Divided)
        return false;

    const PanelRecord& Branch = Records[BranchIndex];
    if (Branch.Minimum == SeekingIndex || Branch.Maximum == SeekingIndex)
    {
        EnclosingIndex = BranchIndex;
        MinimumSide        = Branch.Minimum == SeekingIndex;
        return true;
    }

    return Encloses(Branch.Minimum, SeekingIndex, EnclosingIndex, MinimumSide) ||
           Encloses(Branch.Maximum, SeekingIndex, EnclosingIndex, MinimumSide);
}

Deliver<bool> PanelStructure::Withdraw(std::uint32_t LeafIndex)
{
    if (LeafIndex >= RecordLimit || !Records[LeafIndex].Occupied || Records[LeafIndex].Divided)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "that ordinal names no leaf panel" });

    if (LeafIndex == RootIndex)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the sole panel cannot be withdrawn" });

    std::uint32_t EnclosingIndex = RecordLimit;
    bool          MinimumSide        = false;
    if (!Encloses(RootIndex, LeafIndex, EnclosingIndex, MinimumSide))
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "the leaf has no enclosing division" });

    const PanelRecord Enclosing = Records[EnclosingIndex];
    const std::uint32_t PromotedIndex = MinimumSide ? Enclosing.Maximum : Enclosing.Minimum;
    const PanelRecord Promoted = Records[PromotedIndex];

    Records[EnclosingIndex] = Promoted;
    Records[LeafIndex]      = PanelRecord{};
    Records[PromotedIndex]  = PanelRecord{};

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        EDITING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PanelStructure::Assign(std::uint32_t LeafIndex, PanelSubject Subject)
{
    if (LeafIndex >= RecordLimit || !Records[LeafIndex].Occupied || Records[LeafIndex].Divided)
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "that ordinal names no leaf panel" });

    if (Subject >= PanelSubject::SubjectCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "that panel subject is unsupported" });

    Records[LeafIndex].Subject = Subject;
    return Deliver<bool>::Result(true);
}

Deliver<bool> PanelStructure::Proportion(std::uint32_t DivisionIndex, float MinimumFraction)
{
    if (DivisionIndex >= RecordLimit || !Records[DivisionIndex].Occupied ||
        !Records[DivisionIndex].Divided)
    {
        return Deliver<bool>::Refuse({ RefusalReason::IdentityStale, "that ordinal names no panel division" });
    }

    Records[DivisionIndex].MinimumFraction = (MinimumFraction < 0.05f) ? 0.05f
                                                   : (MinimumFraction > 0.95f) ? 0.95f
                                                                                 : MinimumFraction;
    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        READINGS
//------------------------------------------------------------------------------------------------------------------------

Deliver<PanelRecord> PanelStructure::Current(std::uint32_t Index) const
{
    if (Index >= RecordLimit || !Records[Index].Occupied)
        return Deliver<PanelRecord>::Refuse({ RefusalReason::IdentityStale, "that panel ordinal is unoccupied" });

    return Deliver<PanelRecord>::Result(Records[Index]);
}

bool PanelStructure::RemovalAccepted() const
{
    const PanelRecord& Root = Records[RootIndex];
    return Root.Occupied && Root.Divided;
}

}   // namespace Slate
