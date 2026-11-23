//============================================================================================================================================
//                                                      OCCURRENCESTRUCTURE.H
//============================================================================================================================================
// 🧩 Sketch, profile and solid occurrences with independent placement. Exact geometry remains local to its own
//    kernel declarations; occurrences carry the transform that places one declared item into authoring space.

#pragma once

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class OccurrenceSubject : std::uint32_t
{
    Sketch = 0u,
    Profile = 1u,
    Solid = 2u,
    SubjectCount = 3u
};

struct DeclaredOccurrence
{
    OccurrenceSubject Subject = OccurrenceSubject::Sketch;
    SketchName Sketch = {};
    ProfileNameInFeature Profile = {};
    SolidNameInFeature Solid = {};
    DecomposedTransform Placement = {};

    bool Declared() const;
};

class OccurrenceStructure
{
public:
    OccurrenceNameInFeature Declare(const DeclaredOccurrence& Incoming);
    const DeclaredOccurrence* Resolve(OccurrenceNameInFeature Name) const;
    std::uint32_t DeclaredCount() const { return static_cast<std::uint32_t>(HeldOccurrences.size()); }
    bool Declared() const;
    void Reclaim();

private:
    std::vector<DeclaredOccurrence> HeldOccurrences = {};
};

} // namespace Slate
