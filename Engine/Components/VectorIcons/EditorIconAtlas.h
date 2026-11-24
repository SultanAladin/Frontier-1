/*============================================================================================================================================
                                                            EDITORICONATLAS.H
============================================================================================================================================*/
// 🧩 Runtime ThorVG SVG-to-RGBA texture atlas for official Frontier editor icons (EngineContent/Icons/Editor/*.svg)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"

namespace Frontier::EditorIconAtlas
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// Draws the official SVG icon corresponding to Symbol into Surface at [TopLeft, TopLeft + Extent].
// Returns true if the official SVG icon was rasterized and drawn; returns false to allow procedural fallback.
[[nodiscard]] bool DrawSymbolIcon(
    PixelSpace&               Surface,
    EnvironmentSymbolCategory Symbol,
    Point2D                   TopLeft,
    float                     Extent,
    float                     Opacity = 1.0f) noexcept;

// Queries the relative SVG filename under EngineContent/Icons/Editor/ for Symbol (nullptr if unmapped).
[[nodiscard]] const char* QuerySymbolSvgFilename(EnvironmentSymbolCategory Symbol) noexcept;

// Releases cached atlas resources (safe to call at shutdown or after ImGui context destruction).
void Shutdown() noexcept;

} // namespace Frontier::EditorIconAtlas
