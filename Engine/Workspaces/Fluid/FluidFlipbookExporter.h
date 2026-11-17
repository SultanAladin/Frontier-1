/*============================================================================================================================================
                                                           FLUIDFLIPBOOKEXPORTER.H
============================================================================================================================================*/
// 🧩 Offline sprite-sheet / flipbook exporter capturing multi-frame Fluid raymarches into RGBA8 atlas + TOML metadata

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

#include <cstdint>
#include <string_view>

namespace Frontier::Workspaces
{

struct FluidFlipbookExportConfig
{
    uint32_t FrameCols     = 4u;
    uint32_t FrameRows     = 4u;
    uint32_t CellWidthPx   = 96u;
    uint32_t CellHeightPx  = 96u;
    float    StepSeconds   = 1.0f / 24.0f;
};

class FluidFlipbookExporter
{
public:
    [[nodiscard]] static bool ExportAtlasPngAndManifest(
        FluidWorkspaceSession&           Session,
        const FluidFlipbookExportConfig& Config,
        std::string_view                 OutputPngPath,
        std::string_view                 OutputTomlPath);
};

} // namespace Frontier::Workspaces
