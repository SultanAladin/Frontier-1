//============================================================================================================================================
//                                                          SELECTIONSEQUENCE.CPP
//============================================================================================================================================
// 🧩 Sealing, traversal, and the restoration that pairs a document scrub with the selection it served.

#include "SlateDocument/Document/SelectionSequence/Api/SelectionSequence.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       SEALING
//------------------------------------------------------------------------------------------------------------------------

void SelectionSequence::Seal(const std::vector<OwnerIdentity>& Selected, std::uint64_t RevisionIndex)
{
    // 📝 Sealing after a backward traversal truncates what stood ahead, exactly as the document sequence
    //    does. Leaving it would let a forward traversal reach a selection the artist has since replaced.
    if (TraversalIndex < CommittedOrder.size())
        CommittedOrder.resize(static_cast<std::size_t>(TraversalIndex));

    CommittedSelection Incoming;
    Incoming.SelectedOwners = Selected;
    Incoming.RevisionIndex   = RevisionIndex;

    CommittedOrder.push_back(Incoming);

    CurrentSelection = Selected;
    TraversalIndex  = CommittedOrder.size();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SelectionSequence::Retreat()
{
    if (TraversalIndex == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the traversal is at the beginning" });

    --TraversalIndex;

    if (TraversalIndex == 0u)
        CurrentSelection.clear();
    else
        CurrentSelection = CommittedOrder[static_cast<std::size_t>(TraversalIndex) - 1u].SelectedOwners;

    return Deliver<bool>::Result(true);
}

Deliver<bool> SelectionSequence::Advance()
{
    if (TraversalIndex >= CommittedOrder.size())
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the traversal is at the end" });

    CurrentSelection = CommittedOrder[static_cast<std::size_t>(TraversalIndex)].SelectedOwners;
    ++TraversalIndex;

    return Deliver<bool>::Result(true);
}

Deliver<bool> SelectionSequence::RestoreAt(std::uint64_t RevisionIndex)
{
    // 📝 The most recent selection sealed at or before the arrived-at revision. Searched backwards because a
    //    scrub arrives at a revision the artist selected against several times, and the last one is theirs.
    for (std::size_t Index = CommittedOrder.size(); Index-- > 0u;)
    {
        if (CommittedOrder[Index].RevisionIndex > RevisionIndex)
            continue;

        CurrentSelection = CommittedOrder[Index].SelectedOwners;
        TraversalIndex  = Index + 1u;

        return Deliver<bool>::Result(true);
    }

    return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "no selection was sealed at that revision" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const std::vector<OwnerIdentity>& SelectionSequence::Current() const
{
    return CurrentSelection;
}

const std::vector<CommittedSelection>& SelectionSequence::Committed() const
{
    return CommittedOrder;
}

std::uint64_t SelectionSequence::TraversalPosition() const
{
    return TraversalIndex;
}

}   // namespace Slate
