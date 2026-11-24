/*============================================================================================================================================
                                                         TUBELIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Tube Light Inspector (2 tabs: Overview, Source)

#include "Engine/Editor/Inspectors/TubeLightInspector.h"
#include "Engine/Editor/Inspectors/LightInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampByteTb(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}
} // namespace

void TubeLightInspector::UpdateTubeSurface(const TubeLightData& Instance)
{
    uint64_t Key = 0x11050001ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.LengthMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.TubeRadiusMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.LuminousFlux);

    const uint32_t W = 524u, H = 376u;
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::PhotometricLightPreview, 4u,
        { Instance.LuminousFlux * 0.0008f, Instance.LengthMeters, Instance.TubeRadiusMeters, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.88f, 0.94f, 1.0f, 1.0f }
    };
    TubePreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.5f;
        const float Cy = 178.0f;
        const float HalfL = 142.0f;
        const float Rad   = 15.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                float R = 24.0f, G = 24.0f, B = 25.0f;
                const float DX = std::max(0.0f, std::fabs(static_cast<float>(X) - Cx) - HalfL);
                const float DY = static_cast<float>(Y) - Cy;
                const float D  = std::sqrt(DX * DX + DY * DY);

                // Soft glowing halo around glass tube
                const float Glow = std::exp(-D / 22.0f) * 28.0f;
                R += Glow; G += Glow; B += Glow;

                // Cylindrical glass fluorescent tube with specular gradient
                if (D <= Rad)
                {
                    const float N = std::clamp(1.0f - std::fabs(DY) / Rad, 0.0f, 1.0f);
                    const float Tube = 96.0f + N * 116.0f;
                    R = Tube; G = Tube; B = Tube;
                    if (D > Rad - 1.5f) { R = 178.0f; G = 178.0f; B = 180.0f; }
                }

                // Vertical dotted downward emission rays below tube
                if (Y > 205u && Y < 285u && std::fabs(static_cast<float>(X) - Cx) <= HalfL + 4.0f)
                {
                    const int RelX = static_cast<int>(std::lround(static_cast<float>(X) - (Cx - HalfL)));
                    if ((RelX % 36) == 0 && ((Y / 5u) % 2u == 0u)) { R += 32.0f; G += 32.0f; B += 34.0f; }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteTb(R);
                Pixels[Off + 1u] = ClampByteTb(G);
                Pixels[Off + 2u] = ClampByteTb(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float TubeLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, TubeLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const float LmPerM = Instance.LuminousFlux / std::max(0.1f, Instance.LengthMeters);

    if (ActivePage == 0u)
    {
        UpdateTubeSurface(Instance);
        const float ImgH = LightUi::ComputeOverviewPreviewHeight(BodyW);
        const Rectangle2D Card { WideX, CursorY, WideW, LightUi::ComputeOverviewCardHeight(ImgH) };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);

        WorkspaceAppearance::ConstructEnvironmentSymbolIcon(Surface, EnvironmentSymbolCategory::TubeLight, { BodyX, Card.Y + 21.0f }, 20.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 28.0f, Card.Y + 23.0f }, 16.4f, InspectorMetrics::HeadingInk, "Linear radiance");

        float Y = LightUi::ConstructGiantMetric(Surface, BodyX, Card.Y + 64.0f, Instance.LuminousFlux, "lm", "Authored luminous flux");
        Surface.StrokeLine({ BodyX, Y }, { BodyX + BodyW, Y }, LightUi::DividerInk, 1.0f);
        Y += 16.0f;

        char SumBuf[48]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "%.1f m linear emitter", Instance.LengthMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Y }, 11.8f, InspectorMetrics::MutedInk, SumBuf);
        Y += 26.0f;

        const Rectangle2D ImgBox { BodyX, Y + 8.0f, BodyW, ImgH };
        TubePreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, LightUi::CardFill);
        char RadTop[24]{};
        std::snprintf(RadTop, sizeof(RadTop), "%.0f mm RADIUS", Instance.TubeRadiusMeters * 1000.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 4.0f, ImgBox.Y + 6.0f }, 8.2f, InspectorMetrics::DimInk, RadTop);
        const char* RadEm = "RADIAL EMISSION";
        const float REW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, RadEm);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Right() - REW - 4.0f, ImgBox.Y + 6.0f }, 8.2f, InspectorMetrics::DimInk, RadEm);
        char LenCap[28]{};
        std::snprintf(LenCap, sizeof(LenCap), "%.1f m LENGTH", Instance.LengthMeters);
        const float LCW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, LenCap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - LCW * 0.5f, ImgBox.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, LenCap);

        char LinBuf[24]{}, RadVal[24]{};
        LightUi::FormatWithCommas(LmPerM, LinBuf, sizeof(LinBuf));
        std::string LinStr = std::string(LinBuf) + " lm/m";
        std::snprintf(RadVal, sizeof(RadVal), "%.0f mm", Instance.TubeRadiusMeters * 1000.0f);
        LightUi::ConstructStudyTilesAndFooter(
            Surface, BodyX, ImgBox.Bottom() + 24.0f, BodyW,
            "Source study \xC2\xB7 photometric summary",
            "Linear output", LinStr.c_str(), nullptr,
            "Tube radius", RadVal, nullptr,
            "Source & colour", "Luminous flux");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 2: SOURCE — Emitter dimensions (h = 488) + Transform (WORLD SPACE)
        const Rectangle2D Card { WideX, CursorY, WideW, 488.0f };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Emitter dimensions");
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "OPTICS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - OW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "OPTICS");

        char LinBuf[24]{};
        LightUi::FormatWithCommas(LmPerM, LinBuf, sizeof(LinBuf));
        std::string LinStr = std::string(LinBuf) + " lm/m";
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 58.0f }, 34.0f, ColorRgba{ 244u, 244u, 244u, 255u }, LinStr);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 108.0f }, 11.0f, InspectorMetrics::MutedInk, "Authored source flux per metre");

        const Rectangle2D Plot { BodyX, Card.Y + 138.0f, BodyW, 88.0f };
        LightUi::ConstructMinimalOpticGrid(Surface, Plot, "0", "100%");
        Surface.StrokeLine({ Plot.X, Plot.Y + 24.0f }, { Plot.Right(), Plot.Y + 24.0f }, ColorRgba{ 204u, 204u, 206u, 255u }, 1.6f);

        float FY = Plot.Bottom() + 18.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8631u, "Length",      "m", "%.2g", 0.2f,  6.0f, Instance.LengthMeters,     ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8632u, "Tube radius", "m", "%.3g", 0.01f, 0.2f, Instance.TubeRadiusMeters, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8633u, "Reach",      "m", "%.0f", 2.0f, 80.0f, Instance.ReachMeters,      ActiveDragId, Input, OutMutated);
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
        CursorY = LightUi::ConstructTransformCard(Surface, WideX, CursorY, WideW, Entry);
    }

    return CursorY;
}

} // namespace Frontier
