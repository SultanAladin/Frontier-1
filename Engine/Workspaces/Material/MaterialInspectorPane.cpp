/*==========================================================================================================================================
                                                   MATERIALINSPECTORPANE.CPP
==========================================================================================================================================*/
// 🧩 Implementation of MaterialInspectorPane (zero-recompile DynamicParameterBlock sliders, driver selectors & 10-lobe OpenPBR readouts)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialInspectorPane.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] bool RenderParameterSlider(
    PixelSpace&           Surface,
    float                 X,
    float                 Y,
    float                 Width,
    const char*           Label,
    float&                TargetScalar,
    float                 MinBound,
    float                 MaxBound,
    const ControlPointer& Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.Text({ X, Y }, 9.2f, C.TextSoft, Label);

    char NumBuf[32];
    std::snprintf(NumBuf, sizeof(NumBuf), "%.2f", TargetScalar);
    Surface.TextRightAligned({ X, Y, Width, 12.0f }, 9.2f, C.TextPrimary, NumBuf);

    const Rectangle2D Track { X, Y + 17.0f, Width, 6.0f };
    const float Norm = std::clamp((TargetScalar - MinBound) / std::max(1.0e-5f, MaxBound - MinBound), 0.0f, 1.0f);
    Surface.FillRectangle(Track, C.SliderTrack, 3.0f);
    Surface.FillRectangle({ Track.X, Track.Y, Track.Width * Norm, Track.Height }, C.WarmAccent, 3.0f);
    Surface.FillCircle({ Track.X + Track.Width * Norm, Track.Center().Y }, 5.0f, C.SliderThumb);

    const Rectangle2D HitRect { X, Y + 10.0f, Width, 18.0f };
    if (ControlAppearance::QueryPointerOver(HitRect, Pointer) && Pointer.Down)
    {
        const float T = std::clamp((Pointer.X - Track.X) / std::max(1.0f, Track.Width), 0.0f, 1.0f);
        TargetScalar = MinBound + (MaxBound - MinBound) * T;
        return true;
    }
    return false;
}

[[nodiscard]] ColorRgba ConvertLinearColor(const std::array<float, 4u>& Linear) noexcept
{
    const auto ChannelByte = [](float V) noexcept -> uint8_t {
        return static_cast<uint8_t>(std::lround(std::pow(std::clamp(V, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f));
    };
    return { ChannelByte(Linear[0]), ChannelByte(Linear[1]), ChannelByte(Linear[2]), 255u };
}

} // namespace

void MaterialInspectorPane::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    MaterialWorkspaceSession& Session,
    const ControlPointer&     Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 20.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 20.0f, 1.0f);
    Surface.PushClip(Bounds);

    float Y = Bounds.Y + 16.0f;
    const float X = Bounds.X + 16.0f;
    const float W = Bounds.Width - 32.0f;

    auto& Recipe = Session.AccessActiveRecipe();
    auto& Params = Recipe.Parameters;

    // Highlighted KPI Header Summary (Project Zero / Sun Inspector signature #eff4cf accent banner)
    const Rectangle2D HeroBox { X, Y, W, 62.0f };
    Surface.FillRectangle(HeroBox, C.HighlightBg, 14.0f);
    Surface.Text({ HeroBox.X + 14.0f, HeroBox.Y + 10.0f }, 13.0f, C.HighlightInk, Recipe.DisplayTitle.c_str());

    char MetaBuf[96];
    std::snprintf(MetaBuf, sizeof(MetaBuf), "%s  |  256B GPU Block  |  Sig %04llX",
        Recipe.CategoryLabel.c_str(),
        static_cast<unsigned long long>(Session.LastCompiledSignatureHash & 0xFFFFULL));
    Surface.Text({ HeroBox.X + 14.0f, HeroBox.Y + 34.0f }, 9.0f, C.HighlightMuted, MetaBuf);
    Y += 74.0f;

    // Color Swatches Strip (Primary / Secondary / Tertiary / Accent)
    Surface.Text({ X, Y }, 9.8f, C.TextMuted, "LINEAR REC.709 CHROMATIC SLOTS");
    Y += 18.0f;
    const std::array<const char*, 4u> SwatchLabels {{ "Primary", "Secondary", "Seam", "Glow" }};
    const std::array<std::array<float, 4u>*, 4u> SwatchPtrs {{
        &Params.PrimaryColorRgba, &Params.SecondaryColorRgba, &Params.TertiaryColorRgba, &Params.AccentRadianceRgba
    }};
    const float SwatchW = (W - 18.0f) / 4.0f;
    for (std::size_t I = 0u; I < 4u; ++I)
    {
        const Rectangle2D Box { X + static_cast<float>(I) * (SwatchW + 6.0f), Y, SwatchW, 38.0f };
        Surface.FillRectangle(Box, C.CardBg, 10.0f);
        Surface.StrokeRectangle(Box, C.Hairline, 10.0f, 1.0f);
        Surface.FillRectangle({ Box.X + 6.0f, Box.Y + 6.0f, Box.Width - 12.0f, 14.0f }, ConvertLinearColor(*SwatchPtrs[I]), 5.0f);
        Surface.TextCentered({ Box.X, Box.Y + 22.0f, Box.Width, 14.0f }, 8.0f, C.TextMuted, SwatchLabels[I]);
    }
    Y += 50.0f;

    // Live 120 Hz DynamicParameterBlock Sliders (Zero Shader Recompilation)
    Surface.Text({ X, Y }, 9.8f, C.TextMuted, "LIVE DYNAMIC PARAMETERS (ZERO RECOMPILE)");
    Y += 18.0f;

    bool AnyChanged = false;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Transition / Dissolve Progress", Params.TransitionProgress, 0.0f, 1.0f, Pointer); Y += 32.0f;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Procedural Feature Scale",       Params.DetailScale,        0.5f, 14.0f, Pointer); Y += 32.0f;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Relief & Wave Amplitude",        Params.ReliefStrength,     0.0f, 2.0f, Pointer); Y += 32.0f;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Animation Clock Speed",          Params.AnimationSpeed,     0.0f, 3.0f, Pointer); Y += 32.0f;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Seam & Thermal Glow Gain",       Params.SeamGlowIntensity,  0.0f, 4.5f, Pointer); Y += 32.0f;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Climate Rain / Wetness",         Params.WeatherRainIntensity, 0.0f, 1.0f, Pointer); Y += 32.0f;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Roughness Offset Bias",          Params.RoughnessBias,     -0.45f, 0.45f, Pointer); Y += 32.0f;
    AnyChanged |= RenderParameterSlider(Surface, X, Y, W, "Thin-Film Thickness (nm)",       Params.ThinFilmNanometers, 140.0f, 960.0f, Pointer); Y += 36.0f;

    if (AnyChanged)
    {
        MaterialSpace::Dynamic::SanitizeDynamicParameterBlock(Params);
        Session.DirtyPreviewRequested = true;
    }

    // Parameter Driver Mode Selector
    Surface.Text({ X, Y }, 9.8f, C.TextMuted, "PARAMETER DRIVER");
    Y += 18.0f;
    const std::array<MaterialSpace::Dynamic::ParameterDriverMode, 4u> QuickDrivers {{
        MaterialSpace::Dynamic::ParameterDriverMode::PlaybackClock,
        MaterialSpace::Dynamic::ParameterDriverMode::PingPongOscillation,
        MaterialSpace::Dynamic::ParameterDriverMode::ExternalTimeline,
        MaterialSpace::Dynamic::ParameterDriverMode::WorldImpactPulse,
    }};
    const std::array<const char*, 4u> DriverShort {{ "Clock", "PingPong", "Timeline", "Impact" }};
    const float DrvW = (W - 12.0f) / 4.0f;
    for (std::size_t I = 0u; I < 4u; ++I)
    {
        const Rectangle2D Btn { X + static_cast<float>(I) * (DrvW + 4.0f), Y, DrvW, 24.0f };
        const bool Active  = (Params.PrimaryDriverOrdinal == static_cast<uint32_t>(QuickDrivers[I]));
        const bool Hovered = ControlAppearance::QueryPointerOver(Btn, Pointer);
        Surface.FillRectangle(Btn, Active ? C.WarmSoft : C.CardBg, 8.0f);
        Surface.StrokeRectangle(Btn, Active ? C.WarmAccent : C.Hairline, 8.0f, 1.0f);
        Surface.TextCentered(Btn, 8.3f, Active ? C.WarmAccent : (Hovered ? C.TextPrimary : C.TextSoft), DriverShort[I]);
        if (Hovered && Pointer.Pressed)
        {
            Params.PrimaryDriverOrdinal = static_cast<uint32_t>(QuickDrivers[I]);
            Session.DirtyPreviewRequested = true;
        }
    }
    Y += 34.0f;

    // Active OpenPBR 1.1 Optical Lobes Readout
    Surface.Text({ X, Y }, 9.8f, C.TextMuted, "OPENPBR 1.1 OPTICAL LOBES");
    Y += 18.0f;
    const uint32_t Mask = Session.LastActiveLobeMask | MaterialSpace::DetermineActiveLobeMask(Recipe.PrimaryFinish);
    struct LobeChip { const char* Title; uint32_t Bit; };
    const std::array<LobeChip, 6u> Chips {{
        { "EON Diff",  MaterialSpace::LobeDiffuseEON },
        { "GGX Metal", MaterialSpace::LobeSpecularMetalGGX },
        { "Clearcoat", MaterialSpace::LobeClearcoat },
        { "ThinFilm",  MaterialSpace::LobeThinFilmAiry },
        { "Glint",     MaterialSpace::LobeGlintMicroflakes },
        { "Transmit",  MaterialSpace::LobeDielectricTransmission },
    }};
    const float ChipW = (W - 10.0f) / 3.0f;
    for (std::size_t I = 0u; I < Chips.size(); ++I)
    {
        const float CX = X + static_cast<float>(I % 3u) * (ChipW + 5.0f);
        const float CY = Y + static_cast<float>(I / 3u) * 24.0f;
        const bool Active = ((Mask & Chips[I].Bit) != 0u);
        const Rectangle2D Rect { CX, CY, ChipW, 20.0f };
        Surface.FillRectangle(Rect, Active ? C.EmeraldSoft : C.WellBg, 7.0f);
        Surface.StrokeRectangle(Rect, Active ? C.LiveGreen : C.Hairline, 7.0f, 1.0f);
        Surface.TextCentered(Rect, 8.2f, Active ? C.LiveGreen : C.TextFaint, Chips[I].Title);
    }
    Y += 56.0f;

    // 1-Click Static Channel Snapshot Baker Button
    LastBakeButtonRect = { X, std::min(Y, Bounds.Bottom() - 42.0f), W, 30.0f };
    const bool BakeHover = ControlAppearance::QueryPointerOver(LastBakeButtonRect, Pointer);
    Surface.FillRectangle(LastBakeButtonRect, Session.ChannelBakeCompleted ? C.EmeraldSoft : (BakeHover ? C.CardElevated : C.CardBg), 10.0f);
    Surface.StrokeRectangle(LastBakeButtonRect, Session.ChannelBakeCompleted ? C.LiveGreen : C.WarmAccent, 10.0f, 1.0f);
    Surface.TextCentered(LastBakeButtonRect, 9.5f, Session.ChannelBakeCompleted ? C.LiveGreen : C.WarmAccent,
        Session.ChannelBakeCompleted ? "STATIC CHANNELS SNAPSHOTTED" : "SNAPSHOT STATIC PBR CHANNELS");
    if (BakeHover && Pointer.Pressed)
    {
        Session.RequestChannelSnapshotBake();
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
