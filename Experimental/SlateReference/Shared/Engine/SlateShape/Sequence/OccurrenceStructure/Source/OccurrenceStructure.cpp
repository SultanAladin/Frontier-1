//============================================================================================================================================
//                                                    OCCURRENCESTRUCTURE.CPP
//============================================================================================================================================

#include "SlateShape/Sequence/OccurrenceStructure/Api/OccurrenceStructure.h"

namespace Slate
{

bool DeclaredOccurrence::Declared() const
{
    switch (Subject)
    {
        case OccurrenceSubject::Sketch:      return Sketch.Assigned();
        case OccurrenceSubject::Profile:     return Profile.Assigned();
        case OccurrenceSubject::Solid:       return Solid.Assigned();
        case OccurrenceSubject::SubjectCount:return false;
    }
    return false;
}

OccurrenceNameInFeature OccurrenceStructure::Declare(const DeclaredOccurrence& Incoming)
{
    HeldOccurrences.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldOccurrences.size()) };
}

const DeclaredOccurrence* OccurrenceStructure::Resolve(OccurrenceNameInFeature Name) const
{
    if (!Name.Assigned() || Name.IssuedIndex > HeldOccurrences.size())
        return nullptr;
    return &HeldOccurrences[Name.IssuedIndex - 1u];
}

bool OccurrenceStructure::Declared() const
{
    for (const DeclaredOccurrence& Occurrence : HeldOccurrences)
        if (!Occurrence.Declared())
            return false;
    return true;
}

void OccurrenceStructure::Reclaim()
{
    HeldOccurrences.clear();
}

} // namespace Slate
