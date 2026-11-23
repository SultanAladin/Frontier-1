/*============================================================================================================================================
                                                              WORKSPACEZOOM.H
============================================================================================================================================*/
// 🧩 One shared, pure home for the Pattern/Stamp 2D canvases' zoom-step math, so every input path that
// can change Zoom (the +/- buttons, the keyboard, and the mouse-scroll-wheel handler) clamps to the exact
// same {min, max} ceiling. This exists because the scroll-wheel handler in both panels kept its own
// separately-hardcoded 8.f (800%) ceiling after the +/- buttons were raised to 10.f (1000%): the on-screen
// zoom readout and the buttons both promised 1000% was reachable, but scrolling to zoom in silently
// stalled 200% short of it. Routing every zoom input through these two functions makes that exact class of
// drift impossible to reintroduce.

#pragma once
#include <algorithm>
#include <cmath>

namespace Frontier
{
inline constexpr float WorkspaceMinimumZoom = .125f; // 12.5% -- the documented minimum for every Pattern/Stamp 2D canvas
inline constexpr float WorkspaceMaximumZoom = 10.f;  // 1000% -- the documented maximum for every Pattern/Stamp 2D canvas

// 📝 Applies one discrete +/- zoom-button (or keyboard) step; Multiplier is >1 to zoom in, <1 to zoom out.
inline float ApplyWorkspaceZoomStep(float Zoom, float Multiplier)
{
    return std::clamp(Zoom * Multiplier, WorkspaceMinimumZoom, WorkspaceMaximumZoom);
}

// 📝 Applies a continuous scroll-wheel zoom delta (NotchCount may be fractional and/or negative).
inline float ApplyWorkspaceWheelZoom(float Zoom, float NotchCount)
{
    return std::clamp(Zoom * std::pow(1.1f, NotchCount), WorkspaceMinimumZoom, WorkspaceMaximumZoom);
}
}
