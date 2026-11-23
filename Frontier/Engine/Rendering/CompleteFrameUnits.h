/*==========================================================================================================================================
                                                    COMPLETE FRAME DECLARATION
                                     ------------------------------------------------------------------
                 One whole frame: surface raster -> indirect prelude -> ReSTIR chain -> SVGF ->
                      environment -> history write-back -> display present, in a single
                           declaration the ordering/atlas/barrier machinery resolves.
==========================================================================================================================================*/

// 🧩 Complete frame declaration: names surface, illumination, environment, history and present units.
// 📝 These declarations describe intended target touches and ordering; they do not allocate a frame,
//    submit Vulkan commands, ping-pong reservoirs, composite an editor viewport or establish that
//    any ReSTIR estimator is unbiased. The CPU reference renderer implements only separate, scoped models.

#pragma once

#include "EnvironmentUnits.h"
#include "IlluminationUnits.h"
#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct CompleteFrameDeclaration
{
    std::vector<SubmissionUnitDeclaration> Units;
    std::vector<RenderTargetDeclaration>   Targets;
};

struct CompleteFrameOptions
{
    bool IncludeInspection = false;   // appends the editor-only inspection unit when enabled
};

[[nodiscard]] CompleteFrameDeclaration DeclareCompleteFrame(uint32_t Width, uint32_t Height,
                                                            CompleteFrameOptions Options = {});

} // namespace Frontier
