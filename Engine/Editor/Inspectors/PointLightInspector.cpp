/*============================================================================================================================================
                                                         POINTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Point Light Inspector (2 tabs: Overview, Source)

#include "Engine/Editor/Inspectors/PointLightInspector.h"
#include "Engine/Editor/Inspectors/LightInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampBytePt(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}
} // namespace

void PointLightInspector::UpdateFalloffSurface(const PointLightData& Instance)
{
    uint64_t Key = 0x11010001ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.IntensityCandela);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.ReachMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.DecayExponent);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.RadiantTint.R);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.RadiantTint.B);

    // Seamless dark card background rgb(24,24,25) with concentric reach rings + indigo core
    const uint32_t W = 524u, H = 376u;
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::PhotometricLightPreview, 0u,
        { Instance.IntensityCandela * 0.0015f, Instance.ReachMeters, Instance.DecayExponent, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { Instance.RadiantTint.R, Instance.RadiantTint.G, Instance.RadiantTint.B, 1.0f }
    };
    FalloffPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.5f;
        const float Cy = static_cast<float>(H) * 0.50f;
        const float MaxR = 148.0f;
        const float TR = std::clamp(Instance.RadiantTint.R, 0.2f, 1.0f);
        const float TG = std::clamp(Instance.RadiantTint.G, 0.2f, 1.0f);
        const float TB = std::clamp(Instance.RadiantTint.B, 0.2f, 1.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - Cx;
                const float DY = static_cast<float>(Y) - Cy;
                const float D  = std::sqrt(DX * DX + DY * DY);

                float R = 24.0f, G = 24.0f, B = 25.0f;
                // Soft radial bluish-indigo glow matching reference-rim-point__0
                const float Halo = std::exp(-D / 52.0f) * 34.0f;
                R += Halo * 0.58f * TR;
                G += Halo * 0.65f * TG;
                B += Halo * 1.35f * TB;

                // 4 concentric reach rings
                for (int K = 1; K <= 4; ++K)
                {
                    const float RR = MaxR * (static_cast<float>(K) * 0.25f);
                    if (std::fabs(D - RR) < 1.05f) { R += 18.0f; G += 19.0f; B += 22.0f; }
                }
                // Emitter core dot
                if (D < 10.5f)
                {
                    const float Core = 1.0f - std::clamp((D - 8.0f) / 2.5f, 0.0f, 1.0f);
                    R = R * (1.0f - Core) + (104.0f * TR) * Core;
                    G = G * (1.0f - Core) + (116.0f * TG) * Core;
                    B = B * (1.0f - Core) + (214.0f * TB) * Core;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampBytePt(R);
                Pixels[Off + 1u] = ClampBytePt(G);
                Pixels[Off + 2u] = ClampBytePt(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float PointLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PointLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const float Lux5m = Instance.IntensityCandela / std::max(0.01f, std::pow(5.0f, std::max(0.2f, Instance.DecayExponent)));

    if (ActivePage == 0u)
    {
        UpdateFalloffSurface(Instance);
        const float ImgH = LightUi::ComputeOverviewPreviewHeight(BodyW);
        const Rectangle2D Card { WideX, CursorY, WideW, LightUi::ComputeOverviewCardHeight(ImgH) };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);

        WorkspaceAppearance::ConstructEnvironmentSymbolIcon(Surface, EnvironmentSymbolCategory::PointLight, { BodyX, Card.Y + 21.0f }, 20.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 28.0f, Card.Y + 23.0f }, 16.4f, InspectorMetrics::HeadingInk, "Radial falloff");

        float Y = LightUi::ConstructGiantMetric(Surface, BodyX, Card.Y + 64.0f, Instance.IntensityCandela, "cd", "Authored luminous intensity");
        Surface.StrokeLine({ BodyX, Y }, { BodyX + BodyW, Y }, LightUi::DividerInk, 1.0f);
        Y += 16.0f;

        char SumBuf[48]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "Omnidirectional / decay %.1f", Instance.DecayExponent);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Y }, 11.8f, InspectorMetrics::MutedInk, SumBuf);
        Y += 26.0f;

        const Rectangle2D ImgBox { BodyX, Y + 8.0f, BodyW, ImgH };
        FalloffPreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, LightUi::CardFill);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 4.0f, ImgBox.Y + 6.0f }, 8.4f, InspectorMetrics::DimInk, "OMNIDIRECTIONAL");
        char DecBuf[24]{};
        std::snprintf(DecBuf, sizeof(DecBuf), "DECAY %.1f", Instance.DecayExponent);
        const float DW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.4f, DecBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Right() - DW - 4.0f, ImgBox.Y + 6.0f }, 8.4f, InspectorMetrics::DimInk, DecBuf);
        char RingBuf[48]{};
        std::snprintf(RingBuf, sizeof(RingBuf), "%.0f m  %.0f m  %.0f m  %.0f m",
            Instance.ReachMeters * 0.25f, Instance.ReachMeters * 0.50f, Instance.ReachMeters * 0.75f, Instance.ReachMeters);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X + 18.0f, ImgBox.Center().Y - 5.0f }, ImgBox.Width * 0.5f - 20.0f, 7.8f, InspectorMetrics::DimInk, RingBuf);
        const char* Cap = "Reach guides \xC2\xB7 free space";
        const float CW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, Cap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - CW * 0.5f, ImgBox.Bottom() - 14.0f }, 8.6f, InspectorMetrics::DimInk, Cap);

        char LuxBuf[24]{}, ReachBuf[24]{};
        std::snprintf(LuxBuf, sizeof(LuxBuf), "%.2f lx", Lux5m);
        std::snprintf(ReachBuf, sizeof(ReachBuf), "%.0f m", Instance.ReachMeters);
        LightUi::ConstructStudyTilesAndFooter(
            Surface, BodyX, ImgBox.Bottom() + 24.0f, BodyW,
            "Source study \xC2\xB7 photometric summary",
            "At 5 m \xC2\xB7 estimate", LuxBuf, nullptr,
            "Reach", ReachBuf, nullptr,
            "Source & colour", "Intensity");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 2: SOURCE — Attenuation (h = 624)
        const Rectangle2D Card { WideX, CursorY, WideW, 624.0f };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Attenuation");
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "OPTICS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - OW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "OPTICS");

        char LuxBuf[24]{};
        std::snprintf(LuxBuf, sizeof(LuxBuf), "%.2f lx", Lux5m);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 58.0f }, 36.0f, ColorRgba{ 244u, 244u, 244u, 255u }, LuxBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 108.0f }, 11.2f, InspectorMetrics::MutedInk, "Authored decay at 5 m \xC2\xB7 free-space estimate");

        // Illuminance decay curve plot (1m..10m)
        const Rectangle2D Plot { BodyX + 42.0f, Card.Y + 158.0f, BodyW - 46.0f, 118.0f };
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.X, Plot.Y - 22.0f }, 9.2f, InspectorMetrics::DimInk, "ILLUMINANCE \xC2\xB7 lx");
        for (int R = 0; R <= 4; ++R)
        {
            const float GY = Plot.Y + Plot.Height * (static_cast<float>(R) / 4.0f);
            Surface.StrokeLine({ Plot.X, GY }, { Plot.Right(), GY }, ColorRgba{ 255u, 255u, 255u, 16u }, 1.0f);
            char YLbl[16]{};
            std::snprintf(YLbl, sizeof(YLbl), "%.3g", Instance.IntensityCandela * (1.0f - static_cast<float>(R) * 0.25f));
            const float YW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, YLbl);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.X - YW - 8.0f, GY - 5.0f }, 9.2f, InspectorMetrics::DimInk, YLbl);
        }
        const int Segs = 40;
        Point2D Prev {};
        for (int I = 0; I <= Segs; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(Segs);
            const float Dist = 1.0f + T * 9.0f;
            const float Rel  = std::pow(1.0f / Dist, std::max(0.2f, Instance.DecayExponent));
            const Point2D P { Plot.X + T * Plot.Width, Plot.Bottom() - Rel * Plot.Height };
            if (I > 0) Surface.StrokeLine(Prev, P, ColorRgba{ 112u, 126u, 224u, 255u }, 1.8f);
            Prev = P;
        }
        const std::array<float, 4u> Dists = {{ 1.0f, 2.0f, 5.0f, 10.0f }};
        for (float D : Dists)
        {
            const float T   = (D - 1.0f) / 9.0f;
            const float Rel = std::pow(1.0f / D, std::max(0.2f, Instance.DecayExponent));
            const Point2D P { Plot.X + T * Plot.Width, Plot.Bottom() - Rel * Plot.Height };
            Surface.FillCircle(P, 3.5f, (D == 5.0f) ? ColorRgba{ 245u, 245u, 245u, 255u } : ColorRgba{ 112u, 126u, 224u, 255u });
            char XLbl[12]{};
            std::snprintf(XLbl, sizeof(XLbl), "%.0f m", D);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { P.X - 8.0f, Plot.Bottom() + 8.0f }, 9.2f, InspectorMetrics::DimInk, XLbl);
        }

        // 4-row Distance / Estimate / Relative table
        float TY = Plot.Bottom() + 38.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, TY }, 10.6f, InspectorMetrics::DimInk, "Distance");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 144.0f, TY }, 10.6f, InspectorMetrics::DimInk, "Estimate");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 44.0f, TY }, 10.6f, InspectorMetrics::DimInk, "Relative");
        TY += 18.0f;
        for (float D : Dists)
        {
            Surface.StrokeLine({ BodyX, TY }, { BodyX + BodyW, TY }, LightUi::DividerInk, 1.0f);
            const float Rel = std::pow(1.0f / D, std::max(0.2f, Instance.DecayExponent));
            char DStr[16]{}, EStr[24]{}, RStr[16]{};
            std::snprintf(DStr, sizeof(DStr), "%.0f m", D);
            std::snprintf(EStr, sizeof(EStr), "%.2f lx", Instance.IntensityCandela * Rel);
            std::snprintf(RStr, sizeof(RStr), "%.1f%%", Rel * 100.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, TY + 8.0f }, 11.4f, InspectorMetrics::HeadingInk, DStr);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 144.0f, TY + 8.0f }, 11.4f, InspectorMetrics::HeadingInk, EStr);
            const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.4f, RStr);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - RW, TY + 8.0f }, 11.4f, InspectorMetrics::HeadingInk, RStr);
            TY += 26.0f;
        }

        TY += 14.0f;
        TY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8601u, "Reach",          "m", "%.0f", 2.0f, 80.0f, Instance.ReachMeters,   ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8602u, "Decay exponent", "",  "%.2g", 0.5f,  4.0f, Instance.DecayExponent, ActiveDragId, Input, OutMutated);
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
        CursorY = LightUi::ConstructTransformCard(Surface, WideX, CursorY, WideW, Entry);
    }

    return CursorY;
}

} // namespace Frontier
