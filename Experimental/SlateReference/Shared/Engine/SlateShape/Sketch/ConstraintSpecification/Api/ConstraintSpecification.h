//============================================================================================================================================
//                                                   CONSTRAINTSPECIFICATION.H
//============================================================================================================================================
// 🧩 One sketch constraint declaration. The solver later consumes these declarations without depending on UI or
//    document session state.

#pragma once

#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"

#include <cstdint>

namespace Slate
{

struct ConstraintName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class ConstraintSubject : std::uint32_t
{
    Coincident = 0u,
    Horizontal = 1u,
    Vertical = 2u,
    Parallel = 3u,
    Perpendicular = 4u,
    Tangent = 5u,
    Equal = 6u,
    Fixed = 7u,
    SubjectCount = 8u
};

struct ConstraintSpecification
{
    ConstraintSubject Subject = ConstraintSubject::Fixed;
    ReferenceSpecification Primary = {};
    ReferenceSpecification Secondary = {};

    bool Declared() const;
};

} // namespace Slate
