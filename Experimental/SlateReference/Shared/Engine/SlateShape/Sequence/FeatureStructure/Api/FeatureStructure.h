//============================================================================================================================================
//                                                       FEATURESTRUCTURE.H
//============================================================================================================================================
// 🧩 The CAD authoring DAG declaration seam — features, their references, and their recompute standing.

#pragma once

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class FeatureSubject : std::uint32_t
{
    Sketch = 0u,
    Extrusion = 1u,
    Revolution = 2u,
    Loft = 3u,
    Sweep = 4u,
    Boolean = 5u,
    Fillet = 6u,
    Chamfer = 7u,
    Offset = 8u,
    Taper = 9u,
    SubjectCount = 10u
};

enum class FeatureStanding : std::uint32_t
{
    Resolved = 0u,
    Recomputing = 1u,
    Refused = 2u,
    Suppressed = 3u,
    ReferenceStale = 4u
};

struct DeclaredFeature
{
    FeatureSubject Subject = FeatureSubject::Sketch;
    FeatureStanding Standing = FeatureStanding::Resolved;
    std::vector<ReferenceSpecification> Inputs = {};
    bool Suppressed = false;
};

class FeatureStructure
{
public:
    FeatureName Declare(const DeclaredFeature& Incoming);
    const DeclaredFeature* Resolve(FeatureName Name) const;
    std::uint32_t DeclaredCount() const { return static_cast<std::uint32_t>(HeldFeatures.size()); }
    bool Declared() const;
    void Reclaim();

private:
    std::vector<DeclaredFeature> HeldFeatures = {};
};

} // namespace Slate
