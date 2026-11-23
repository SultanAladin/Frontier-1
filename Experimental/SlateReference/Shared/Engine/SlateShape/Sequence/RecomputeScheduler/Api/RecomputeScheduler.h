//============================================================================================================================================
//                                                     RECOMPUTESCHEDULER.H
//============================================================================================================================================
// 🧩 Dirty-feature ordering seam. The scheduler is a declaration-only bridge for now, kept outside Application so
//    the eventual feature recompute route stays headless.

#pragma once

#include "SlateShape/Sequence/FeatureStructure/Api/FeatureStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

class RecomputeScheduler
{
public:
    void MarkDirty(FeatureName Name);
    const std::vector<FeatureName>& DirtySet() const { return HeldDirty; }
    bool ScheduleDeclared(const FeatureStructure& Features) const;
    void Reclaim();

private:
    std::vector<FeatureName> HeldDirty = {};
};

} // namespace Slate
