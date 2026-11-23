//============================================================================================================================================
//                                                            REDRAWSCHEDULER.CPP
//============================================================================================================================================
// 🧩 A flat registration of marks, and the three-operand wake rule read from it.

#include "SlateUI/Interface/RedrawScheduler/Api/RedrawScheduler.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> RedrawScheduler::Register(const char* Naming)
{
    if (Registered >= PanelCapacity)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "no panel slot remains" });

    Marks[Registered]   = RedrawMark::Rearrange;
    Namings[Registered] = (Naming != nullptr) ? Naming : "";

    return Deliver<std::uint32_t>::Result(Registered++);
}

void RedrawScheduler::Mark(std::uint32_t PanelIndex, RedrawMark Declared)
{
    if (PanelIndex >= Registered)
        return;

    Marks[PanelIndex] = Dearer(Marks[PanelIndex], Declared);
}

void RedrawScheduler::MarkEvery(RedrawMark Declared)
{
    for (std::uint32_t PanelIndex = 0u; PanelIndex < Registered; ++PanelIndex)
        Marks[PanelIndex] = Dearer(Marks[PanelIndex], Declared);
}

RedrawMark RedrawScheduler::Current(std::uint32_t PanelIndex) const
{
    return (PanelIndex < Registered) ? Marks[PanelIndex] : RedrawMark::Quiet;
}

bool RedrawScheduler::Marked() const
{
    for (std::uint32_t PanelIndex = 0u; PanelIndex < Registered; ++PanelIndex)
    {
        if (Marks[PanelIndex] != RedrawMark::Quiet)
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE WAKE RULE
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 Written as one disjunction rather than as three early returns, because the three operands are one
//    rule and a reader must be able to see that none of them is missing. The three-line form is where a
//    fourth condition eventually gets added to only two of the branches.
bool RedrawScheduler::Waking(bool AnythingMoving, bool ArrivalHeld) const
{
    return Marked() || AnythingMoving || ArrivalHeld;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RETIREMENT
//------------------------------------------------------------------------------------------------------------------------

void RedrawScheduler::Retire()
{
    if (Marked())
        ++RecordedCount;
    else
        ++QuietCount;

    for (std::uint32_t PanelIndex = 0u; PanelIndex < Registered; ++PanelIndex)
        Marks[PanelIndex] = RedrawMark::Quiet;
}

void RedrawScheduler::Retire(std::uint32_t PanelIndex)
{
    if (PanelIndex < Registered)
        Marks[PanelIndex] = RedrawMark::Quiet;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE READS
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t RedrawScheduler::RegisteredCount() const
{
    return Registered;
}

const char* RedrawScheduler::Naming(std::uint32_t PanelIndex) const
{
    return (PanelIndex < Registered) ? Namings[PanelIndex] : "";
}

std::uint64_t RedrawScheduler::QuietTicks() const
{
    return QuietCount;
}

std::uint64_t RedrawScheduler::RecordedTicks() const
{
    return RecordedCount;
}

}   // namespace Slate
