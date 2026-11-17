/*============================================================================================================================================
                                                         FLUIDINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidInspectorPanel: 4-tab Fluid parameters, blackbody palette preview strip & World/GI toggles

#include "Engine/Workspaces/Fluid/FluidInspectorPanel.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

bool FluidInspectorPanel::DrawSliderRow(
    PixelSpace&           Surface,
    Rectangle2D           RowRect,
    const char*           Label,
    float&                InOutValue,
    float                 MinVal,
    float                 MaxVal,
    const ControlPointer& Pointer,
    uint32_t              SliderSlot) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.Text({ RowRect.X, RowRect.Y }, 10.5f, C.TextSoft, Label);

    char ValBuf[32] {};
    std::snprintf(ValBuf, sizeof(ValBuf), "%.2f", InOutValue);
    Surface.TextRightAligned({ RowRect.X, RowRect.Y, RowRect.Width, 14.0f }, 10.5f, C.TextPrimary, ValBuf);

    const Rectangle2D Track { RowRect.X, RowRect.Y + 18.0f, RowRect.Width, 8.0f };
    Surface.FillRectangle(Track, C.WellBg, 4.0f);

    const float T = std::clamp((InOutValue - MinVal) / std::max(1.0e-4f, MaxVal - MinVal), 0.0f, 1.0f);
    Surface.FillRectangle({ Track.X, Track.Y, Track.Width * T, Track.Height }, C.WarmAccent, 4.0f);
    Surface.FillCircle({ Track.X + Track.Width * T, Track.Center().Y }, 5.5f, C.TextPrimary);

    if (Pointer.Pressed && Track.InsetXY(-4.0f, -6.0f).Contains({ Pointer.X, Pointer.Y }))
    {
        ActiveDragSlider = static_cast<int32_t>(SliderSlot);
    }
    if (!Pointer.Down)
    {
        ActiveDragSlider = -1;
    }
    if (ActiveDragSlider == static_cast<int32_t>(SliderSlot) && Pointer.Down)
    {
        const float NewT = std::clamp((Pointer.X - Track.X) / std::max(1.0f, Track.Width), 0.0f, 1.0f);
        InOutValue = MinVal + NewT * (MaxVal - MinVal);
        return true;
    }
    return false;
}

bool FluidInspectorPanel::DrawToggleRow(
    PixelSpace&           Surface,
    Rectangle2D           RowRect,
    const char*           Label,
    bool&                 InOutChecked,
    const ControlPointer& Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.Text({ RowRect.X, RowRect.Y + 3.0f }, 10.8f, C.TextSoft, Label);
    const Rectangle2D Pill { RowRect.Right() - 42.0f, RowRect.Y + 1.0f, 42.0f, 18.0f };
    Surface.FillRectangle(Pill, InOutChecked ? C.WarmAccent : C.WellBg, 9.0f);
    Surface.FillCircle({ InOutChecked ? (Pill.Right() - 9.0f) : (Pill.X + 9.0f), Pill.Center().Y }, 6.5f, C.TextPrimary);
    if (Pointer.Pressed && Pill.Contains({ Pointer.X, Pointer.Y }))
    {
        InOutChecked = !InOutChecked;
        return true;
    }
    return false;
}

void FluidInspectorPanel::Render(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer,
    float                  Opacity) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg.ScaledAlpha(Opacity), 22.0f);
    Surface.StrokeRectangle(Bounds, C.HairlineStrong.ScaledAlpha(Opacity), 22.0f, 1.0f);

    const float X = Bounds.X + 14.0f;
    const float W = Bounds.Width - 28.0f;
    float Y = Bounds.Y + 12.0f;

    struct TabDef { FluidInspectorTab Tab; const char* Label; };
    constexpr std::array<TabDef, 4> Tabs {{
        { FluidInspectorTab::EmitterBlast,   "Emit" },
        { FluidInspectorTab::SolverBounds,   "Grid" },
        { FluidInspectorTab::ShadingPalette, "Look" },
        { FluidInspectorTab::ForcesGi,       "GI/FX" },
    }};
    const float TabW = (W - 12.0f) * 0.25f;
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        const ButtonTone Tone = (Session.ActiveTab == Tabs[I].Tab) ? ButtonTone::Primary : ButtonTone::Secondary;
        if (CapsuleButton::Construct(
                Surface,
                { X + static_cast<float>(I) * (TabW + 4.0f), Y, TabW, 24.0f },
                ButtonDescriptor{ Tabs[I].Label, Tone, 10.2f, 10.0f },
                Pointer).Pressed)
        {
            Session.ActiveTab = Tabs[I].Tab;
        }
    }
    Y += 34.0f;
    Surface.FillRectangle({ X, Y, W, 1.0f }, C.Hairline, 0.0f);
    Y += 12.0f;

    auto& Doc = Session.AccessSimulation().AccessDocument();
    bool NeedsReconfigure = false;

    if (Session.ActiveTab == FluidInspectorTab::EmitterBlast)
    {
        DrawToggleRow(Surface, { X, Y, W, 22.0f }, "Continuous Emitter (+Z)", Doc.Emitter.Enabled, Pointer); Y += 28.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Emitter Rate", Doc.Emitter.Rate, 0.0f, 3.0f, Pointer, 0u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Emitter Temperature", Doc.Emitter.Temperature, 0.2f, 10.0f, Pointer, 1u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Upward Jet Velocity (m/s)", Doc.Emitter.UpwardVelocityMps, 0.0f, 8.0f, Pointer, 2u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Swirl Vorticity", Doc.Emitter.Swirl, 0.0f, 6.0f, Pointer, 3u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Blast Strength (m/s)", Doc.Blast.StrengthMps, 1.0f, 18.0f, Pointer, 4u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Blast Lobes", Doc.Blast.Lobes, 1.0f, 10.0f, Pointer, 5u); Y += 38.0f;
    }
    else if (Session.ActiveTab == FluidInspectorTab::SolverBounds)
    {
        NeedsReconfigure |= DrawToggleRow(Surface, { X, Y, W, 22.0f }, "Adaptive Uncapped Ceiling", Doc.Domain.AdaptiveCeilingEnabled, Pointer); Y += 28.0f;
        NeedsReconfigure |= DrawToggleRow(Surface, { X, Y, W, 22.0f }, "World Smoke Drift Grid", Doc.Domain.WorldGridEnabled, Pointer); Y += 28.0f;
        NeedsReconfigure |= DrawToggleRow(Surface, { X, Y, W, 22.0f }, "MacCormack BFECC Advect", Doc.Domain.MacCormackAdvection, Pointer); Y += 28.0f;
        NeedsReconfigure |= DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Max Ceiling (m)", Doc.Domain.MaxExplosionCeilingMeters, 4.0f, 60.0f, Pointer, 10u); Y += 38.0f;
        NeedsReconfigure |= DrawSliderRow(Surface, { X, Y, W, 32.0f }, "World Drift Span (m)", Doc.Domain.WorldGridSpanMeters, 8.0f, 64.0f, Pointer, 11u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Buoyancy (+Z)", Doc.Combustion.Buoyancy, 0.0f, 12.0f, Pointer, 12u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Vorticity Confinement", Doc.Combustion.VorticityConfinement, 0.0f, 8.0f, Pointer, 13u); Y += 38.0f;
    }
    else if (Session.ActiveTab == FluidInspectorTab::ShadingPalette)
    {
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Density Extinction", Doc.Shading.DensityExtinction, 0.2f, 16.0f, Pointer, 20u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Fire Intensity", Doc.Shading.FireIntensity, 0.0f, 6.0f, Pointer, 21u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Temperature Scale", Doc.Shading.TemperatureScale, 0.2f, 3.0f, Pointer, 22u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Smoke Albedo", Doc.Shading.SmokeAlbedo, 0.05f, 1.0f, Pointer, 23u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "HG Anisotropy (g)", Doc.Shading.PhaseAnisotropy, -0.5f, 0.9f, Pointer, 24u); Y += 42.0f;

        // Live 6-palette Blackbody ramp strip
        Surface.Text({ X, Y }, 10.5f, C.TextSoft, "Blackbody Palette (Click to cycle)");
        Y += 16.0f;
        const Rectangle2D Strip { X, Y, W, 18.0f };
        constexpr uint32_t Slices = 24u;
        const float SW = Strip.Width / static_cast<float>(Slices);
        for (uint32_t S = 0u; S < Slices; ++S)
        {
            const float Temp = (static_cast<float>(S) / static_cast<float>(Slices - 1u)) * 4.5f;
            const auto Col = SimulationSpace::FluidSpace::FluidPresets::EvaluateBlackbodyPalette(
                Temp, Doc.Shading.TemperatureScale, Doc.Shading.ColorPalette);
            const auto R = static_cast<uint8_t>(std::clamp(Col.R / (1.0f + Col.R) * 255.0f, 0.0f, 255.0f));
            const auto G = static_cast<uint8_t>(std::clamp(Col.G / (1.0f + Col.G) * 255.0f, 0.0f, 255.0f));
            const auto B = static_cast<uint8_t>(std::clamp(Col.B / (1.0f + Col.B) * 255.0f, 0.0f, 255.0f));
            Surface.FillRectangle({ Strip.X + static_cast<float>(S) * SW, Strip.Y, SW + 0.6f, Strip.Height }, { R, G, B, 255u }, 0.0f);
        }
        Surface.StrokeRectangle(Strip, C.HairlineStrong, 4.0f, 1.0f);
        if (Pointer.Pressed && Strip.Contains({ Pointer.X, Pointer.Y }))
        {
            const uint32_t Next = (static_cast<uint32_t>(Doc.Shading.ColorPalette) + 1u) % 6u;
            Doc.Shading.ColorPalette = static_cast<SimulationSpace::FluidSpace::FluidColorPalette>(Next);
        }
    }
    else
    {
        DrawToggleRow(Surface, { X, Y, W, 22.0f }, "Couple ForceFields", Doc.Combustion.CoupleForceFields, Pointer); Y += 28.0f;
        DrawToggleRow(Surface, { X, Y, W, 22.0f }, "Emit Dynamic GI Fire Lights", Doc.GiCoupling.EmitGiLights, Pointer); Y += 28.0f;
        DrawToggleRow(Surface, { X, Y, W, 22.0f }, "Cast Volumetric GI Shadows", Doc.GiCoupling.CastGiShadows, Pointer); Y += 28.0f;
        DrawToggleRow(Surface, { X, Y, W, 22.0f }, "Fluid-Advected Embers", Doc.Particles.ShowEmbers, Pointer); Y += 28.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "Wind X (m/s)", Doc.Combustion.WindVelocityXY.X, -8.0f, 8.0f, Pointer, 30u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "GI Fire Light Gain", Doc.GiCoupling.EmissiveLightIntensity, 0.0f, 4.0f, Pointer, 31u); Y += 38.0f;
        DrawSliderRow(Surface, { X, Y, W, 32.0f }, "GI Shadow Strength", Doc.GiCoupling.GiShadowStrength, 0.0f, 2.0f, Pointer, 32u); Y += 38.0f;
    }

    if (NeedsReconfigure)
    {
        Session.AccessSimulation().Configure(Doc);
    }
}

} // namespace Frontier::Workspaces
