//============================================================================================================================================
//                                                       LOFTSPECIFICATION.H
//============================================================================================================================================

#pragma once

#include "SlateShape/Geometry/ProfileSpecification/Api/ProfileSpecification.h"

#include <vector>

namespace Slate
{

struct LoftSpecification
{
    std::vector<ProfileName> Sections = {};
    bool Closed = false;

    bool Declared() const { return Sections.size() >= 2u; }
};

} // namespace Slate
