/*==========================================================================================================================================
                                                          TWO UNITS, THREE TARGETS
                                     ------------------------------------------------------------------
                                             The smallest honest frame the viewport can submit.
==========================================================================================================================================*/

// 🧩 Two units, three targets: the smallest honest frame the viewport can submit.

#include "ViewportUnits.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ViewportFrameDeclaration DeclareViewportFrame(uint32_t Width, uint32_t Height)
{
    ViewportFrameDeclaration Frame;

    Frame.Units = {
        SubmissionUnitDeclaration{
            "SurfaceRaster",
            {
                TargetTouch{"Surface", TargetUsage::ColourWrite},
                TargetTouch{"Depth", TargetUsage::DepthWrite},
            },
            {},
        },
        SubmissionUnitDeclaration{
            "DisplayPresent",
            {
                TargetTouch{"Surface", TargetUsage::Sampled},
                TargetTouch{"Swapchain", TargetUsage::ColourWrite},
                TargetTouch{"Swapchain", TargetUsage::Present},
            },
            {},
        },
    };

    Frame.Targets = {
        RenderTargetDeclaration{"Surface", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"Depth", TargetFormat::Depth32Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"Swapchain", TargetFormat::Bgra8Unorm, Width, Height, true, TargetUsage::Present},
    };

    return Frame;
}

} // namespace Frontier
