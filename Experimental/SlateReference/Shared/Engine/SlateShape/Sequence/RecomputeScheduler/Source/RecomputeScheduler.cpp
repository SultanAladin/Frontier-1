//============================================================================================================================================
//                                                   RECOMPUTESCHEDULER.CPP
//============================================================================================================================================

#include "SlateShape/Sequence/RecomputeScheduler/Api/RecomputeScheduler.h"

namespace Slate
{

void RecomputeScheduler::MarkDirty(FeatureName Name)
{
    if (!Name.Assigned())
        return;
    for (FeatureName Held : HeldDirty)
        if (Held.IssuedIndex == Name.IssuedIndex)
            return;
    HeldDirty.push_back(Name);
}

bool RecomputeScheduler::ScheduleDeclared(const FeatureStructure& Features) const
{
    for (FeatureName Name : HeldDirty)
        if (Features.Resolve(Name) == nullptr)
            return false;
    return true;
}

void RecomputeScheduler::Reclaim()
{
    HeldDirty.clear();
}

} // namespace Slate
