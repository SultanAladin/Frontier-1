//============================================================================================================================================
//                                                          OVERLAYTRANSFORM.SLANG.H
//============================================================================================================================================
// 🧩 The overlay pass's screen → NDC transform, shared by the vertex shader
//    (`WorkspaceOverlayVertex.slang`) and the harness that proofs the pass.
//
//    🔴 WHY THIS EXISTS. The overlay pass draws AFTER the interface, so its
//    vertex transform must agree with the interface's own vertex shader to the
//    pixel: NDC.x = 2x/w − 1, NDC.y = 1 − 2y/h (screen y grows downward, NDC +1
//    is the TOP of the framebuffer — the same mapping ImGui's shader applies).
//    The previous spelling computed `−2y/h − 1`, which maps every positive
//    screen y BELOW NDC −1: the whole overlay was clipped off-screen and the
//    grid, the axes and the gizmo silently drew NOTHING (the recurring "the
//    grid is not showing" — the CPU twin in the harness rasterized in screen
//    space and never exercised this math, so the bug shipped three times).
//    The transform lives here, in `Shared/`, so the shader and the harness
//    compile the SAME functions and the harness can assert the convention
//    (y = 0 must land on NDC +1) on real numbers.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"

namespace Slate
{

/// 🧩 Projects one screen abscissa into NDC, matching the interface's own vertex shader.
/// in    ScreenX      [px] the display ordinate, 0 at the left edge
/// in    DisplayWidth [px] the display the pass's viewport is set against
/// out   [-] the NDC ordinate, −1 at the left edge, +1 at the right
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED float OverlayNdcX(float ScreenX, float DisplayWidth)
{
    return 2.0f * ScreenX / DisplayWidth - 1.0f;
}

/// 🧩 Projects one screen ordinate into NDC, matching Vulkan's viewport convention.
/// in    ScreenY       [px] the display ordinate, 0 at the TOP edge (the interface's convention)
/// in    DisplayHeight [px] the display the pass's viewport is set against
/// out   [-] the NDC ordinate, −1 at the top, +1 at the bottom
/// note  🔴 Screen y grows downward, and in Vulkan NDC −1 is the top row and +1 is the bottom row:
///        the correct spelling is `2y/h − 1`, matching ImGui's Vulkan backend `aPos.y * (2/H) − 1`.
///        A `1 − 2y/h` spelling inverted the entire Y axis, flipping the grid and gizmo upside down.
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED float OverlayNdcY(float ScreenY, float DisplayHeight)
{
    return 2.0f * ScreenY / DisplayHeight - 1.0f;
}

}   // namespace Slate
