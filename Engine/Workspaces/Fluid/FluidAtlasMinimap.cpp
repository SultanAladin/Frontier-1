/*============================================================================================================================================
                                                           FLUIDATLASMINIMAP.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidAtlasMinimap: 4x6 Z-slice volumetric atlas minimap matching Html_Presets_View.png

#include "Engine/Workspaces/Fluid/FluidAtlasMinimap.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>

namespace Frontier::Workspaces
{

void FluidAtlasMinimap::RenderOverlay(
    PixelSpace&            Surface,
    Rectangle2D            AnchorBounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer) noexcept
{
    if (!Session.AtlasMinimapOpen) return;

    const Rectangle2D Box { AnchorBounds.Right() - 216.0f, AnchorBounds.Center().Y - 58.0f, 198.0f, 134.0f };
    Surface.FillRectangle(Box, { 12u, 14u, 18u, 235u }, 8.0f);
    Surface.StrokeRectangle(Box, { 239u, 165u, 111u, 150u }, 8.0f, 1.0f);

    const auto& Hero = Session.InspectSimulation().InspectHeroGrid();
    const auto& Doc  = Session.InspectSimulation().InspectDocument();
    if (Hero.Resolution > 0u)
    {
        const uint32_t R  = Hero.Resolution;
        const uint32_t Rd = Hero.ReadIdx();
        constexpr uint32_t TileCols = 6u;
        constexpr uint32_t TileRows = 4u;
        const float Pad   = 4.0f;
        const float TileW = (Box.Width - Pad * 2.0f) / static_cast<float>(TileCols);
        const float TileH = (Box.Height - Pad * 2.0f) / static_cast<float>(TileRows);
        constexpr uint32_t SampleN = 10u;

        for (uint32_t TR = 0u; TR < TileRows; ++TR)
        {
            for (uint32_t TC = 0u; TC < TileCols; ++TC)
            {
                const uint32_t SliceIdx = TR * TileCols + TC;
                const uint32_t IZ = std::min(R - 1u, (SliceIdx * (R - 1u)) / (TileCols * TileRows - 1u));
                const float TX0 = Box.X + Pad + static_cast<float>(TC) * TileW;
                const float TY0 = Box.Y + Pad + static_cast<float>(TR) * TileH;
                const float CW  = TileW / static_cast<float>(SampleN);
                const float CH  = TileH / static_cast<float>(SampleN);

                for (uint32_t SY = 0u; SY < SampleN; ++SY)
                {
                    const uint32_t IY = (SY * (R - 1u)) / (SampleN - 1u);
                    for (uint32_t SX = 0u; SX < SampleN; ++SX)
                    {
                        const uint32_t IX = (SX * (R - 1u)) / (SampleN - 1u);
                        const uint32_t Idx = SimulationSpace::FluidSpace::FluidGridVolume::FlatIndex(
                            static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);
                        const auto& Th = Hero.Thermo[Rd][Idx];
                        if (Th.X < 0.02f && Th.Y < 0.02f) continue;

                        const auto Glow = SimulationSpace::FluidSpace::FluidPresets::EvaluateBlackbodyPalette(
                            Th.Y, Doc.Shading.TemperatureScale, Doc.Shading.ColorPalette);
                        const float S = std::clamp(Th.X * 0.25f, 0.0f, 0.85f);
                        const auto R8 = static_cast<uint8_t>(std::clamp((S * 0.55f + Glow.R) / (1.0f + Glow.R) * 255.0f, 0.0f, 255.0f));
                        const auto G8 = static_cast<uint8_t>(std::clamp((S * 0.58f + Glow.G) / (1.0f + Glow.G) * 255.0f, 0.0f, 255.0f));
                        const auto B8 = static_cast<uint8_t>(std::clamp((S * 0.65f + Glow.B) / (1.0f + Glow.B) * 255.0f, 0.0f, 255.0f));
                        Surface.FillRectangle(
                            { TX0 + static_cast<float>(SX) * CW, TY0 + static_cast<float>(SY) * CH, CW + 0.4f, CH + 0.4f },
                            { R8, G8, B8, 255u }, 0.0f);
                    }
                }
                Surface.StrokeRectangle({ TX0, TY0, TileW, TileH }, { 120u, 150u, 195u, 70u }, 0.0f, 0.6f);
            }
        }
    }

    if (ControlAppearance::QueryPointerOver(Box, Pointer) && Pointer.Pressed)
    {
        Session.AtlasMinimapOpen = false;
    }
}

} // namespace Frontier::Workspaces
