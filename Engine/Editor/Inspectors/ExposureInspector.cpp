/*============================================================================================================================================
                                                           EXPOSUREINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 ProjectZeroEditor-parity Exposure component inspector (2 tabs: Overview, Settings)

#include "Engine/Editor/Inspectors/ExposureInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float ExposureInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, ExposureInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    const float LinearGain = std::clamp(std::pow(2.0f, Instance.ExposureEv) * Instance.BiasMultiplier, 0.03125f, 16.0f);

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Hero EV exposure meter card + Quick presets + Quick controls
        const Rectangle2D MeterCard { WideX, CursorY, WideW, 316.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, MeterCard, "Exposure meter", "ACES Filmic", InspectorMetrics::DimInk);

        char EvBuf[28]{};
        std::snprintf(EvBuf, sizeof(EvBuf), "%+.2f", Instance.ExposureEv);
        char GainCaption[64]{};
        std::snprintf(GainCaption, sizeof(GainCaption), "%.2f\xC3\x97 linear gain \xC2\xB7 %s",
            LinearGain, Instance.Enabled && Entry.VisibilityEnabled ? "Active on GPU" : "Bypassed");
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, MeterCard.Y + 62.0f, EvBuf, "EV", GainCaption, 36.0f);

        // Dark photographic EV stop scale well (-4 .. 0 .. +4 EV)
        const Rectangle2D Well { BodyX, MeterCard.Y + 136.0f, BodyW, 74.0f };
        Surface.FillRectangle(Well, ColorRgba{ 22u, 22u, 24u, 255u }, 12.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);

        const float TrackX = Well.X + 14.0f;
        const float TrackW = std::max(40.0f, Well.Width - 28.0f);
        const float TrackY = Well.Y + 34.0f;
        Surface.FillRectangle({ TrackX, TrackY, TrackW, 4.0f }, ColorRgba{ 52u, 52u, 58u, 255u }, 2.0f);

        const float NormEv = std::clamp((Instance.ExposureEv + 4.0f) / 8.0f, 0.0f, 1.0f);
        const float ZeroX  = TrackX + TrackW * 0.5f;
        const float CurX   = TrackX + TrackW * NormEv;
        const float FillX0 = std::min(ZeroX, CurX);
        const float FillW  = std::max(2.0f, std::fabs(CurX - ZeroX));
        Surface.FillRectangle({ FillX0, TrackY, FillW, 4.0f }, ColorRgba{ 224u, 166u, 76u, 240u }, 2.0f);
        Surface.FillCircle({ CurX, TrackY + 2.0f }, 6.0f, ColorRgba{ 246u, 246u, 246u, 255u });
        Surface.StrokeCircle({ CurX, TrackY + 2.0f }, 6.0f, ColorRgba{ 224u, 166u, 76u, 255u }, 1.5f);

        for (int Stop = -4; Stop <= 4; ++Stop)
        {
            const float SX = TrackX + TrackW * (static_cast<float>(Stop + 4) / 8.0f);
            const float TickH = (Stop == 0) ? 12.0f : 7.0f;
            Surface.StrokeLine({ SX, TrackY - TickH - 2.0f }, { SX, TrackY - 2.0f },
                Stop == 0 ? ColorRgba{ 235u, 235u, 240u, 180u } : ColorRgba{ 255u, 255u, 255u, 45u }, 1.0f);
            if ((Stop % 2) == 0)
            {
                char StopLbl[8]{};
                std::snprintf(StopLbl, sizeof(StopLbl), "%+d", Stop);
                if (Stop == 0) std::snprintf(StopLbl, sizeof(StopLbl), "0");
                const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, StopLbl);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
                    { SX - LW * 0.5f, TrackY + 12.0f }, 8.8f, InspectorMetrics::DimInk, StopLbl);
            }
        }

        // Click / drag directly inside the EV meter well
        const Rectangle2D TrackHit { TrackX - 6.0f, Well.Y + 12.0f, TrackW + 12.0f, Well.Height - 20.0f };
        if (TrackHit.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            ActiveDragId = 8500u;
        }
        if (ActiveDragId == 8500u)
        {
            if (Input.PrimaryPointer.Down)
            {
                const float T = std::clamp((Cursor.X - TrackX) / std::max(1.0f, TrackW), 0.0f, 1.0f);
                const float NextEv = std::round((T * 8.0f - 4.0f) * 20.0f) / 20.0f;
                if (std::fabs(NextEv - Instance.ExposureEv) > 1.0e-4f)
                {
                    Instance.ExposureEv = NextEv;
                    OutMutated = true;
                }
            }
            else
            {
                ActiveDragId = 0u;
            }
        }

        // Quick EV presets (-2, -1, 0, +1, +2)
        static constexpr std::array<float, 5u>       PresetEvs    = {{ -2.0f, -1.0f, 0.0f, 1.0f, 2.0f }};
        static constexpr std::array<const char*, 5u> PresetLabels = {{ "-2 EV", "-1 EV", "0 EV", "+1 EV", "+2 EV" }};
        const float PresY = Well.Bottom() + 14.0f;
        const float Gap   = 6.0f;
        const float PW    = std::floor((BodyW - Gap * 4.0f) / 5.0f);
        for (uint32_t I = 0u; I < 5u; ++I)
        {
            const Rectangle2D Pill { BodyX + static_cast<float>(I) * (PW + Gap), PresY, PW, 30.0f };
            const bool Active = std::fabs(Instance.ExposureEv - PresetEvs[I]) < 0.05f;
            Surface.FillRectangle(Pill, Active ? ColorRgba{ 224u, 166u, 76u, 42u } : ColorRgba{ 28u, 28u, 30u, 255u }, 8.0f);
            Surface.StrokeRectangle(Pill, Active ? ColorRgba{ 224u, 166u, 76u, 210u } : ColorRgba{ 255u, 255u, 255u, 18u }, 8.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.2f, PresetLabels[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium,
                { Pill.Center().X - TW * 0.5f, Pill.Y + 9.0f }, 10.2f,
                Active ? ColorRgba{ 246u, 212u, 146u, 255u } : InspectorMetrics::MutedInk, PresetLabels[I]);
            if (Pill.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                Instance.ExposureEv = PresetEvs[I];
                OutMutated = true;
            }
        }

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, PresY + 42.0f, BodyW, 10.6f, 15.0f, InspectorMetrics::DimInk,
            "Photographic EV stops scale linear HDR radiance before ACES filmic tone mapping.");
        CursorY = MeterCard.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls (Enabled + Reset to 0 EV)
        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",    InspectorSwitchSymbol::Eye,  Entry.VisibilityEnabled && Instance.Enabled, false },
            { "Reset 0 EV", InspectorSwitchSymbol::Sun,  std::fabs(Instance.ExposureEv) < 0.01f && std::fabs(Instance.BiasMultiplier - 1.0f) < 0.01f, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, WideX, CursorY, WideW, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            const bool Next = !(Entry.VisibilityEnabled && Instance.Enabled);
            Entry.VisibilityEnabled = Next;
            Instance.Enabled        = Next;
            OutMutated              = true;
        }
        else if (Clicked == 1)
        {
            Instance.ExposureEv     = 0.0f;
            Instance.BiasMultiplier = 1.0f;
            OutMutated              = true;
        }
    }
    else
    {
        // TAB 2: SETTINGS — Exposure card + Tone & Vignette card
        const Rectangle2D ExpCard { WideX, CursorY, WideW, 235.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ExpCard, "Exposure");
        float EY = ExpCard.Y + 68.0f;
        EY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, EY, BodyW, 8501u, "EV Compensation", "EV", "%+.2f", -4.0f, 4.0f, Instance.ExposureEv, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, EY, BodyW, 8502u, "Exposure Bias", "\xC3\x97", "%.2f", 0.25f, 4.0f, Instance.BiasMultiplier, ActiveDragId, Input, OutMutated);
        CursorY = ExpCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D ToneCard { WideX, CursorY, WideW, 307.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ToneCard, "Tone & Lens");
        float TY = ToneCard.Y + 68.0f;
        TY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8503u, "Contrast", "", "%.2f", 0.5f, 2.0f, Instance.ContrastGain, ActiveDragId, Input, OutMutated);
        TY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8504u, "Saturation", "", "%.2f", 0.0f, 2.0f, Instance.SaturationGain, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8505u, "Vignette", "", "%.2f", 0.0f, 1.0f, Instance.VignetteStrength, ActiveDragId, Input, OutMutated);
        CursorY = ToneCard.Bottom() + InspectorMetrics::SectionGap;
    }

    if (OutMutated && DisplaySettings != nullptr && Entry.VisibilityEnabled && Instance.Enabled)
    {
        DisplaySettings->ExposureGain          = std::clamp(std::pow(2.0f, Instance.ExposureEv) * Instance.BiasMultiplier, 0.0625f, 16.0f);
        DisplaySettings->PostProcessContrast   = Instance.ContrastGain;
        DisplaySettings->PostProcessSaturation = Instance.SaturationGain;
        DisplaySettings->PostProcessVignette   = Instance.VignetteStrength;
    }

    return CursorY;
}

} // namespace Frontier
