/*====================================================================================================================================
                                                           SOLIDARCCAMERA.H
====================================================================================================================================*/
// 🧩 Sketcher viewport camera: the shared Engine/Render/OrbitCamera plus scene framing and box selection of edges

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Box selection of B-rep edges (Plasticity viewport-selector): the scene edges whose every polyline point projects
//    inside the pixel box Low … High (viewport pixels, y down). Without x-ray only visible edges qualify: at least one
//    segment midpoint is not behind the nearest face along its pixel ray (2 px + 10⁻⁴ of the bounds diagonal of slack,
//    so creases and silhouettes count as visible). XRayEnabled keeps hidden edges too. Indices in scene order.
//    Section (Alt+S, may be null): edges wholly on the removed side never qualify, and visibility honours the clip and cap.
[[nodiscard]] std::vector<uint32_t> EncloseSketcherEdges(const SketcherScene& Scene, const OrbitCameraBasis& Frame, std::array<double, 2> Low,
                                                         std::array<double, 2> High, bool XRayEnabled, const SketcherSectionClip* Section = nullptr);

// Fits the orbit camera to the scene bounds (FitOrbitCamera on Scene.BoundsLow … BoundsHigh).
void FitSketcherCamera(OrbitCamera& Camera, const SketcherScene& Scene);

} // namespace Frontier::Workspaces::Sketcher
