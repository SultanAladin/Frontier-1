/*============================================================================================================================================
                                                          LEDSTRIPINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of LED Strip Inspector (2 tabs: Overview, Source)

#include "Engine/Editor/Inspectors/LedStripInspector.h"
#include "Engine/Editor/Inspectors/LightInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampByteSt(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}
} // namespace

void LedStripInspector::UpdateRibbonSurface(const LedStripData& Instance)
{
    uint64_t Key = 0x11070001ull ^ (Instance.OpalDiffuser ? 1ull : 0ull);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.LengthMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.DiodesPerMetre);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.KelvinTemperature);

    const uint32_t W = 524u, H = 376u;
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::PhotometricLightPreview, 6u,
        { 1.2f, Instance.DiodesPerMetre * 0.1f, Instance.LengthMeters, Instance.KelvinTemperature },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.78f, 0.44f, 1.0f }
    };
    RibbonPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.5f;
        const float Cy = 186.0f;
        const float HalfL = 216.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                float R = 24.0f, G = 24.0f, B = 25.0f;
                const float DX = std::fabs(static_cast<float>(X) - Cx);
                const float DY = std::fabs(static_cast<float>(Y) - Cy);

                // Warm amber glow around ribbon
                if (DX <= HalfL + 12.0f)
                {
                    const float Glow = std::exp(-DY / 14.0f) * 26.0f;
                    R += Glow * 1.15f; G += Glow * 0.78f; B += Glow * 0.35f;
                }
                // Flexible PCB ribbon channel + warm amber SMD LED packages
                if (DX <= HalfL && DY <= 12.0f)
                {
                    R = 54.0f; G = 46.0f; B = 36.0f;
                    if (DY > 10.5f) { R = 104.0f; G = 94.0f; B = 78.0f; }
                    if (DY <= 4.0f)
                    {
                        const int Cell = static_cast<int>(X) % 14;
                        if (Instance.OpalDiffuser || Cell < 9) { R = 240.0f; G = 166.0f; B = 88.0f; }
                    }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteSt(R);
                Pixels[Off + 1u] = ClampByteSt(G);
                Pixels[Off + 2u] = ClampByteSt(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LedStripInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LedStripData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);

    const float TotalLm   = Instance.LumensPerMetre * Instance.LengthMeters * Instance.Dimmer;
    const float TotalW    = Instance.WattsPerMetre * Instance.LengthMeters;
    const float CurrentA  = TotalW / std::max(1.0f, Instance.SupplyVolts);
    const int   LedCount  = static_cast<int>(std::lround(Instance.LengthMeters * Instance.DiodesPerMetre));

    if (ActivePage == 0u)
    {
        UpdateRibbonSurface(Instance);
        const float ImgH = LightUi::ComputeOverviewPreviewHeight(BodyW);
        const Rectangle2D Card { WideX, CursorY, WideW, LightUi::ComputeOverviewCardHeight(ImgH) };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);

        WorkspaceAppearance::ConstructEnvironmentSymbolIcon(Surface, EnvironmentSymbolCategory::LedStrip, { BodyX, Card.Y + 21.0f }, 20.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 28.0f, Card.Y + 23.0f }, 16.4f, InspectorMetrics::HeadingInk, "Ribbon light");

        float Y = LightUi::ConstructGiantMetric(Surface, BodyX, Card.Y + 64.0f, TotalLm, "lm", "Estimated luminous flux");
        Surface.StrokeLine({ BodyX, Y }, { BodyX + BodyW, Y }, LightUi::DividerInk, 1.0f);
        Y += 16.0f;

        char SumBuf[48]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "Horizontal / %.1f m run", Instance.LengthMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Y }, 11.8f, InspectorMetrics::MutedInk, SumBuf);
        Y += 26.0f;

        const Rectangle2D ImgBox { BodyX, Y + 8.0f, BodyW, ImgH };
        RibbonPreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, LightUi::CardFill);
        char VoltBuf[16]{};
        std::snprintf(VoltBuf, sizeof(VoltBuf), "%.0f V DC", Instance.SupplyVolts);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 4.0f, ImgBox.Y + 6.0f }, 8.2f, InspectorMetrics::DimInk, VoltBuf);
        const char* PkgTxt = Instance.OpalDiffuser ? "OPAL DIFFUSER" : "EXPOSED PACKAGES";
        const float PTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, PkgTxt);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Right() - PTW - 4.0f, ImgBox.Y + 6.0f }, 8.2f, InspectorMetrics::DimInk, PkgTxt);
        char RunCap[40]{};
        std::snprintf(RunCap, sizeof(RunCap), "%.1f m RUN \xC2\xB7 %d EMITTERS", Instance.LengthMeters, LedCount);
        const float RCW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, RunCap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - RCW * 0.5f, ImgBox.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, RunCap);

        char LoadBuf[24]{}, CntBuf[24]{};
        std::snprintf(LoadBuf, sizeof(LoadBuf), "%.1f W", TotalW);
        std::snprintf(CntBuf,  sizeof(CntBuf),  "%d LEDs", LedCount);
        LightUi::ConstructStudyTilesAndFooter(
            Surface, BodyX, ImgBox.Bottom() + 24.0f, BodyW,
            "Source study \xC2\xB7 photometric summary",
            "Connected load", LoadBuf, nullptr,
            "Emitter count", CntBuf, nullptr,
            "Output per metre", "Flux per metre");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 2: SOURCE — Layout & segments (h = 542) + Transform (WORLD SPACE)
        const Rectangle2D Card { WideX, CursorY, WideW, 542.0f };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Layout & segments");
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "OPTICS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - OW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "OPTICS");

        char CurBuf[24]{};
        std::snprintf(CurBuf, sizeof(CurBuf), "%.2f A", CurrentA);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 58.0f }, 36.0f, ColorRgba{ 244u, 244u, 244u, 255u }, CurBuf);
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, Card.Y + 106.0f, BodyW, 11.0f, 15.5f, InspectorMetrics::MutedInk,
            "Connected watts \xC3\xB7 supply volts \xC2\xB7 ideal full-load current");

        const Rectangle2D Plot { BodyX, Card.Y + 148.0f, BodyW, 88.0f };
        char RightLen[16]{};
        std::snprintf(RightLen, sizeof(RightLen), "%.1f m", Instance.LengthMeters);
        LightUi::ConstructMinimalOpticGrid(Surface, Plot, "0", RightLen);
        Surface.StrokeLine({ Plot.X, Plot.Bottom() - 16.0f }, { Plot.Right(), Plot.Y + 4.0f }, ColorRgba{ 226u, 168u, 114u, 255u }, 1.6f);

        float FY = Plot.Bottom() + 18.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8651u, "Strip length",    "m",  "%.2g", 0.2f, 10.0f, Instance.LengthMeters,   ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8652u, "Emitter density", "/m", "%.0f", 30.0f, 144.0f, Instance.DiodesPerMetre, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8653u, "Supply voltage",  "V",  "%.0f", 5.0f, 48.0f, Instance.SupplyVolts,    ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, BodyX, FY + 4.0f, BodyW, "Opal diffuser", Instance.OpalDiffuser, Input, OutMutated);
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
        CursorY = LightUi::ConstructTransformCard(Surface, WideX, CursorY, WideW, Entry);
    }

    return CursorY;
}

} // namespace Frontier
