/*============================================================================================================================================
                                                          FLUIDFLIPBOOKEXPORTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidFlipbookExporter: steps simulation over N frames and composites RGBA8 atlas PNG + TOML manifest

#include "Engine/Workspaces/Fluid/FluidFlipbookExporter.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace Frontier::Workspaces
{

bool FluidFlipbookExporter::ExportAtlasPngAndManifest(
    FluidWorkspaceSession&           Session,
    const FluidFlipbookExportConfig& Config,
    std::string_view                 OutputPngPath,
    std::string_view                 OutputTomlPath)
{
    const uint32_t Cols = std::clamp(Config.FrameCols, 1u, 16u);
    const uint32_t Rows = std::clamp(Config.FrameRows, 1u, 16u);
    const uint32_t CW   = std::clamp(Config.CellWidthPx, 16u, 512u);
    const uint32_t CH   = std::clamp(Config.CellHeightPx, 16u, 512u);
    const uint32_t AtlasW = Cols * CW;
    const uint32_t AtlasH = Rows * CH;

    std::vector<uint8_t> Atlas(static_cast<std::size_t>(AtlasW) * AtlasH * 4u, 0u);
    const uint32_t TotalFrames = Cols * Rows;

    for (uint32_t F = 0u; F < TotalFrames; ++F)
    {
        Session.StepSimulation(Config.StepSeconds);
        Session.RenderPreviewFramebuffer(CW, CH);

        const uint32_t Col = F % Cols;
        const uint32_t Row = F / Cols;
        for (uint32_t Y = 0u; Y < CH; ++Y)
        {
            const uint32_t DstY = Row * CH + Y;
            for (uint32_t X = 0u; X < CW; ++X)
            {
                const uint32_t DstX = Col * CW + X;
                const std::size_t SrcOff = (static_cast<std::size_t>(Y) * CW + X) * 4u;
                const std::size_t DstOff = (static_cast<std::size_t>(DstY) * AtlasW + DstX) * 4u;
                Atlas[DstOff + 0u] = Session.PreviewRgba8[SrcOff + 0u];
                Atlas[DstOff + 1u] = Session.PreviewRgba8[SrcOff + 1u];
                Atlas[DstOff + 2u] = Session.PreviewRgba8[SrcOff + 2u];
                Atlas[DstOff + 3u] = Session.PreviewRgba8[SrcOff + 3u];
            }
        }
    }

    const std::string PngPath(OutputPngPath);
    if (stbi_write_png(PngPath.c_str(), static_cast<int>(AtlasW), static_cast<int>(AtlasH), 4, Atlas.data(), static_cast<int>(AtlasW * 4u)) == 0)
    {
        return false;
    }

    std::ostringstream Toml;
    Toml << "[flipbook]\n"
         << "preset_id = \"" << Session.ActivePresetId << "\"\n"
         << "columns = " << Cols << "\n"
         << "rows = " << Rows << "\n"
         << "cell_width_px = " << CW << "\n"
         << "cell_height_px = " << CH << "\n"
         << "step_seconds = " << Config.StepSeconds << "\n";

    std::ofstream Out(std::string(OutputTomlPath), std::ios::binary);
    if (!Out.is_open()) return false;
    Out << Toml.str();
    return Out.good();
}

} // namespace Frontier::Workspaces
