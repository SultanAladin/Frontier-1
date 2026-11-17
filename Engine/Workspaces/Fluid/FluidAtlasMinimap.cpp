/*============================================================================================================================================
                                                           FLUIDATLASMINIMAP.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidAtlasMinimap: renders live XZ cross-section slice of Smoke & Temperature

#include "Engine/Workspaces/Fluid/FluidAtlasMinimap.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <cstdio>

namespace Frontier::Workspaces
{

void FluidAtlasMinimap::RenderOverlay(
    PixelSpace&            Surface,
    Rectangle2D            AnchorBounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer) noexcept
{
    if (!Session.AtlasMinimapOpen) return;

    const auto C = ParticleAppearance::QueryStudioColors();
    const Rectangle2D Box { AnchorBounds.Right() - 214.0f, AnchorBounds.Bottom() - 236.0f, 200.0f, 222.0f };
    Surface.FillRectangle(Box, C.PanelBg, 14.0f);
    Surface.StrokeRectangle(Box, C.HairlineStrong, 14.0f, 1.0f);

    Surface.Text({ Box.X + 10.0f, Box.Y + 8.0f }, 10.5f, C.TextPrimary, "XZ SLICE MINIMAP");
    if (CapsuleButton::Construct(
            Surface, { Box.Right() - 30.0f, Box.Y + 5.0f, 22.0f, 18.0f },
            ButtonDescriptor{ "X", ButtonTone::Secondary, 9.5f, 6.0f }, Pointer).Pressed)
    {
        Session.AtlasMinimapOpen = false;
        return;
    }

    const Rectangle2D GridRect { Box.X + 12.0f, Box.Y + 30.0f, 176.0f, 176.0f };
    Surface.FillRectangle(GridRect, { 10u, 12u, 16u, 255u }, 6.0f);

    const auto& Hero = Session.InspectSimulation().InspectHeroGrid();
    const auto& Doc  = Session.InspectSimulation().InspectDocument();
    if (Hero.Resolution > 0u)
    {
        const uint32_t R  = Hero.Resolution;
        const uint32_t Rd = Hero.ReadIdx();
        const uint32_t IY = std::clamp(static_cast<uint32_t>(SliceOffset01 * static_cast<float>(R - 1u)), 0u, R - 1u);
        const uint32_t SampleStep = std::max(1u, R / 32u);
        const uint32_t Cells = (R + SampleStep - 1u) / SampleStep;
        const float CW = GridRect.Width / static_cast<float>(Cells);
        const float CH = GridRect.Height / static_cast<float>(Cells);

        for (uint32_t IZ = 0u; IZ < R; IZ += SampleStep)
        {
            const uint32_t Row = Cells - 1u - (IZ / SampleStep);
            for (uint32_t IX = 0u; IX < R; IX += SampleStep)
            {
                const uint32_t Col = IX / SampleStep;
                const uint32_t Idx = SimulationSpace::FluidSpace::FluidGridVolume::FlatIndex(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);
                const auto& Th = Hero.Thermo[Rd][Idx];
                const auto Glow = SimulationSpace::FluidSpace::FluidPresets::EvaluateBlackbodyPalette(
                    Th.Y, Doc.Shading.TemperatureScale, Doc.Shading.ColorPalette);
                const float S = std::clamp(Th.X * 0.25f, 0.0f, 0.85f);
                const auto R8 = static_cast<uint8_t>(std::clamp((S * 0.55f + Glow.R) / (1.0f + Glow.R) * 255.0f, 8.0f, 255.0f));
                const auto G8 = static_cast<uint8_t>(std::clamp((S * 0.58f + Glow.G) / (1.0f + Glow.G) * 255.0f, 10.0f, 255.0f));
                const auto B8 = static_cast<uint8_t>(std::clamp((S * 0.65f + Glow.B) / (1.0f + Glow.B) * 255.0f, 14.0f, 255.0f));
                Surface.FillRectangle(
                    { GridRect.X + static_cast<float>(Col) * CW, GridRect.Y + static_cast<float>(Row) * CH, CW + 0.5f, CH + 0.5f },
                    { R8, G8, B8, 255u }, 0.0f);
            }
        }
    }
    Surface.StrokeRectangle(GridRect, C.HairlineStrong, 6.0f, 1.0f);
}

} // namespace Frontier::Workspaces
