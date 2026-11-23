//============================================================================================================================================
//                                                     FEATURESTRUCTURE.CPP
//============================================================================================================================================

#include "SlateShape/Sequence/FeatureStructure/Api/FeatureStructure.h"

namespace Slate
{

FeatureName FeatureStructure::Declare(const DeclaredFeature& Incoming)
{
    HeldFeatures.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldFeatures.size()) };
}

const DeclaredFeature* FeatureStructure::Resolve(FeatureName Name) const
{
    if (!Name.Assigned() || Name.IssuedIndex > HeldFeatures.size())
        return nullptr;
    return &HeldFeatures[Name.IssuedIndex - 1u];
}

bool FeatureStructure::Declared() const
{
    for (const DeclaredFeature& Feature : HeldFeatures)
        for (const ReferenceSpecification& Input : Feature.Inputs)
            if (!Input.Declared())
                return false;
    return true;
}

void FeatureStructure::Reclaim()
{
    HeldFeatures.clear();
}

} // namespace Slate
