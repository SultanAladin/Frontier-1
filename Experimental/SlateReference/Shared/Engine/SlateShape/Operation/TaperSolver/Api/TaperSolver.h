//============================================================================================================================================
//                                                            TAPERSOLVER.H
//============================================================================================================================================
// 🧩 The taper applied to a face so a moulded or cast part can leave its tool — the angle by which a wall
//    leans away from the direction the tool is drawn in.
//
// 🔴 This unit carried a banned word in its name until this pass — one that reads as "a rough version"
//    far more often than it reads as this angle. The ban exists so no name in the source carries a second
//    meaning. `Taper` names the geometry and nothing else. The rename cost nothing: no unit referenced it.

#pragma once

#include <cstdint>

namespace Slate
{

struct TaperSpecification
{
    std::uint32_t SourceFace = 0u;
    double AngleRadians = 0.0;

    bool Declared() const { return SourceFace != 0u && AngleRadians != 0.0; }
};

} // namespace Slate
