//============================================================================================================================================
//                                                        PROVENANCEINDEX.H
//============================================================================================================================================
// 🧩 Feature-produced topology naming declarations. This is the seam by which faces, edges and vertices can later
//    be resolved across recomputes without coupling the feature graph to the current document pipeline.

#pragma once

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class GeneratorSubject : std::uint32_t
{
    ProfileLateral = 0u,
    ProfileStartCap = 1u,
    ProfileEndCap = 2u,
    RevolutionLateral = 3u,
    LoftLateral = 4u,
    SweepLateral = 5u,
    BooleanSplit = 6u,
    FilletRolling = 7u,
    OffsetShifted = 8u,
    SubjectCount = 9u
};

struct ProvenanceKey
{
    FeatureName Producer = {};
    GeneratorSubject Subject = GeneratorSubject::ProfileLateral;
    std::uint32_t PrimaryIssuedIndex = 0u;
    std::uint32_t SecondaryIssuedIndex = 0u;
    std::uint32_t Discriminator = 0u;

    bool Declared() const { return Producer.Assigned(); }
};

class ProvenanceIndex
{
public:
    std::uint32_t Declare(const ProvenanceKey& Incoming);
    const ProvenanceKey* Resolve(std::uint32_t IssuedIndex) const;
    void Reclaim();

private:
    std::vector<ProvenanceKey> HeldKeys = {};
};

} // namespace Slate
