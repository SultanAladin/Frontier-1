//============================================================================================================================================
//                                                     WORKSPACEDECLARATION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorkspaceDeclaration/Api/WorkspaceDeclaration.h"

namespace Slate
{

Deliver<bool> ApplyWorkspace(const WorkspaceDeclaration& Declared, PanelStructure& Partition)
{
    Partition.ConstructPanelPartition(Declared.Initial);

    if (Declared.StepCount > WorkspaceDeclaration::StepLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "the workspace declares more steps than the partition can hold" });

    for (std::uint32_t Index = 0u; Index < Declared.StepCount; ++Index)
    {
        const ArrangementStep& Step = Declared.Steps[Index];

        // 🔴 Each step is checked against the partition's own refusal rather than assumed to hold. A
        //    declaration naming a slot that does not exist is a defect in the declaration, and reporting
        //    the refusal is what turns it into a diagnosable one instead of a partition that quietly ends
        //    up with fewer panels than the artist expected.
        Deliver<bool> Applied = Deliver<bool>::Result(true);

        switch (Step.Applied)
        {
            case ArrangementStep::Action::Seat:
                Applied = Partition.Assign(Step.Slot, Step.Subject);
                break;

            case ArrangementStep::Action::Divide:
                Applied = Partition.Divide(Step.Slot, Step.Axis, Step.Side);
                break;

            case ArrangementStep::Action::Proportion:
                Applied = Partition.Proportion(Step.Slot, Step.Fraction);
                break;

            case ArrangementStep::Action::ActionCount:
                return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                               "the workspace declares a step that names no action" });
        }

        if (!Applied.Resolved)
            return Applied;
    }

    return Deliver<bool>::Result(true);
}

}   // namespace Slate
