/*==========================================================================================================================================
                                                   THE VIEWPORT'S FIRST FRAME DECLARATION
                                     ------------------------------------------------------------------
                                             Surface raster into display present, nothing more.
==========================================================================================================================================*/

// 🧩 The viewport's first frame declaration: surface raster into display present, nothing more.

#pragma once

#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// One whole frame as data: units + the targets they touch. The SDK phase records exactly
// this declaration (SurfaceRaster body: Shaders/SurfaceRaster.slang, DisplayPresent body:
// Shaders/DisplayPresent.slang); the portable layer orders it, aliases it and derives its
// barriers today.
struct ViewportFrameDeclaration
{
    std::vector<SubmissionUnitDeclaration> Units;
    std::vector<RenderTargetDeclaration>   Targets;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHADING MODES
//------------------------------------------------------------------------------------------------------------------------

// Viewport shading modes beyond the lit frame. MatCap and CAD-edge are the upstream
// TextureWorkspace preview modes — their shader sets already exist upstream
// (Engine/Render/SurfaceRasterization.h: Surface/Edge/MatCap/PreviewPbr) and are
// reused at the SDK phase, not reinvented here. The mode selects which surface
// shader set the SurfaceRaster unit records; ordering/barriers are unchanged.
enum class ViewportShadingMode : uint8_t
{
    Shaded,    // Lit PBR preview (SurfaceRaster.slang today)
    MatCap,    // Material-capture sphere lookup (upstream MatCap shader set)
    CadEdges   // Flat faces + edge overlay (upstream Edge shader set)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// SurfaceRaster writes the HDR surface + depth; DisplayPresent samples the surface,
// writes the swapchain and hands it to Present (two touches, one unit — the SDK unit
// records its own transition to PRESENT_SRC after the blit).
[[nodiscard]] ViewportFrameDeclaration DeclareViewportFrame(uint32_t Width, uint32_t Height);

} // namespace Frontier
