//============================================================================================================================================
//                                                   REVOLUTIONSPECIFICATION.H
//============================================================================================================================================
// 🧩 Exact revolution request declaration. The implementation route is intentionally separated from the current
//    polygon/document pipeline so the CAD kernel can grow additively.

#pragma once

#include "SlateShape/Geometry/ProfileSpecification/Api/ProfileSpecification.h"

#include <cstdint>

namespace Slate
{

enum class RevolutionDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSpecification = 1u,
    ImplementationAbsent = 2u,
    Produced = 3u
};

struct RevolutionSpecification
{
    ProfileName SourceProfile = {};
    SpatialPoint AxisOrigin = {};
    SpatialDirection AxisDirection = {};
    double SweepRadians = 0.0;

    bool Declared() const;
};

RevolutionDisposition EvaluateRevolution(const RevolutionSpecification& Declared);

} // namespace Slate
