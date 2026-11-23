//============================================================================================================================================
//                                                    DIMENSIONSPECIFICATION.H
//============================================================================================================================================
// 🧩 One driving dimension declaration for the two-dimensional sketch editor. A dimension names the exact sketch
//    content it measures and can later drive that same content through the solver layer.

#pragma once

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"

#include <cstdint>

namespace Slate
{

struct DimensionName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class DimensionSubject : std::uint32_t
{
    Horizontal = 0u,
    Vertical = 1u,
    Aligned = 2u,
    Radius = 3u,
    Diameter = 4u,
    Angle = 5u,
    SubjectCount = 6u
};

struct DimensionSpecification
{
    DimensionSubject Subject = DimensionSubject::Aligned;
    ReferenceSpecification Primary = {};
    ReferenceSpecification Secondary = {};
    double Target = 0.0;

    bool Declared() const;
};

} // namespace Slate
