//============================================================================================================================================
//                                                           OFFSETSOLVER.H
//============================================================================================================================================

#pragma once

#include <cstdint>

namespace Slate
{

struct OffsetSpecification
{
    std::uint32_t SourceFace = 0u;
    double Distance = 0.0;

    bool Declared() const { return SourceFace != 0u && Distance != 0.0; }
};

} // namespace Slate
