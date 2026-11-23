//============================================================================================================================================
//                                                    TESSELLATIONSPECIFICATION.H
//============================================================================================================================================
// 🧩 Exact-to-discrete derivation declaration — the seam by which exact CAD geometry can later feed the current
//    polygon/document/render pipeline without making that pipeline authoritative.

#pragma once

#include <cstdint>

namespace Slate
{

enum class TessellationSubject : std::uint32_t
{
    Preview = 0u,
    Working = 1u,
    Export = 2u
};

struct TessellationSpecification
{
    TessellationSubject Subject = TessellationSubject::Preview;
    double ChordDeviation = 0.1;
    double AngularDeviationRadians = 0.08726646259971647; // [rad] - five degrees
    bool EdgeReuse = true;

    bool Declared() const
    {
        return ChordDeviation > 0.0 && AngularDeviationRadians > 0.0;
    }
};

} // namespace Slate
