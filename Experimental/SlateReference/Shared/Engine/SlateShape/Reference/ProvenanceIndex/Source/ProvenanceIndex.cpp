//============================================================================================================================================
//                                                      PROVENANCEINDEX.CPP
//============================================================================================================================================

#include "SlateShape/Reference/ProvenanceIndex/Api/ProvenanceIndex.h"

namespace Slate
{

std::uint32_t ProvenanceIndex::Declare(const ProvenanceKey& Incoming)
{
    HeldKeys.push_back(Incoming);
    return static_cast<std::uint32_t>(HeldKeys.size());
}

const ProvenanceKey* ProvenanceIndex::Resolve(std::uint32_t IssuedIndex) const
{
    if (IssuedIndex == 0u || IssuedIndex > HeldKeys.size())
        return nullptr;
    return &HeldKeys[IssuedIndex - 1u];
}

void ProvenanceIndex::Reclaim()
{
    HeldKeys.clear();
}

} // namespace Slate
