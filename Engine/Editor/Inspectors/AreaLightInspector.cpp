/*============================================================================================================================================
                                                         AREALIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Area Light Inspector (2 tabs: Overview, Source)

#include "Engine/Editor/Inspectors/AreaLightInspector.h"
#include "Engine/Editor/Inspectors/LightInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampByteAr(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}
} // namespace

void AreaLightInspector::UpdateApertureSurface(const AreaLightData& Instance)
{
    uint64_t Key = 0x11040001ull ^ (static_cast<uint64_t>(Instance.ApertureFormIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.WidthMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.HeightMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.LuminousFlux);

    const uint32_t W = 524u, H = 376u;
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::PhotometricLightPreview, 3u,
        { Instance.LuminousFlux * 0.0008f, Instance.WidthMeters, Instance.HeightMeters, static_cast<float>(Instance.ApertureFormIndex) },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.96f, 0.92f, 0.84f, 1.0f }
    };
    AperturePreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.5f;
        const float Cy = static_cast<float>(H) * 0.48f;
        const float Aspect = std::clamp(Instance.WidthMeters / std::max(0.1f, Instance.HeightMeters), 0.35f, 3.0f);
        const float HalfW = (Aspect >= 1.0f) ? 168.0f : 168.0f * Aspect;
        const float HalfH = (Aspect >= 1.0f) ? (168.0f / Aspect) : 168.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                float R = 24.0f, G = 24.0f, B = 25.0f;
                const float DX = std::fabs(static_cast<float>(X) - Cx);
                const float DY = std::fabs(static_cast<float>(Y) - Cy);
                const bool Inside = (Instance.ApertureFormIndex == 0u)
                    ? (DX <= HalfW && DY <= HalfH)
                    : (std::hypot(DX / std::max(1.0f, HalfW), DY / std::max(1.0f, HalfH)) <= 1.0f);
                const bool OnEdge = (Instance.ApertureFormIndex == 0u)
                    ? ((std::fabs(DX - HalfW) < 1.8f && DY <= HalfH + 1.0f) || (std::fabs(DY - HalfH) < 1.8f && DX <= HalfW + 1.0f))
                    : (std::fabs(std::hypot(DX / std::max(1.0f, HalfW), DY / std::max(1.0f, HalfH)) - 1.0f) < 0.015f);

                if (Inside)
                {
                    R = 48.0f; G = 46.0f; B = 42.0f;
                    // Dotted emitter grid inside softbox matching light__0
                    if ((X % 18u) < 2u && (Y % 18u) < 2u) { R = 132.0f; G = 122.0f; B = 98.0f; }
                }
                if (OnEdge) { R = 224.0f; G = 212.0f; B = 186.0f; }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteAr(R);
                Pixels[Off + 1u] = ClampByteAr(G);
                Pixels[Off + 2u] = ClampByteAr(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float AreaLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AreaLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    const float AreaM2 = (Instance.ApertureFormIndex == 0u)
        ? (Instance.WidthMeters * Instance.HeightMeters)
        : (3.14159265f * 0.25f * Instance.WidthMeters * Instance.HeightMeters);

    if (ActivePage == 0u)
    {
        UpdateApertureSurface(Instance);
        const float ImgH = LightUi::ComputeOverviewPreviewHeight(BodyW);
        const Rectangle2D Card { WideX, CursorY, WideW, LightUi::ComputeOverviewCardHeight(ImgH) };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);

        WorkspaceAppearance::ConstructEnvironmentSymbolIcon(Surface, EnvironmentSymbolCategory::AreaLight, { BodyX, Card.Y + 21.0f }, 20.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 28.0f, Card.Y + 23.0f }, 16.4f, InspectorMetrics::HeadingInk, "Luminous surface");

        float Y = LightUi::ConstructGiantMetric(Surface, BodyX, Card.Y + 64.0f, Instance.LuminousFlux, "lm", "Authored luminous flux");
        Surface.StrokeLine({ BodyX, Y }, { BodyX + BodyW, Y }, LightUi::DividerInk, 1.0f);
        Y += 16.0f;

        char SumBuf[64]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "%s / %.1f \xC3\x97 %.1f m aperture",
            (Instance.ApertureFormIndex == 0u) ? "Rectangle" : "Disk", Instance.WidthMeters, Instance.HeightMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Y }, 11.8f, InspectorMetrics::MutedInk, SumBuf);
        Y += 26.0f;

        const Rectangle2D ImgBox { BodyX, Y + 8.0f, BodyW, ImgH };
        AperturePreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, LightUi::CardFill);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 4.0f, ImgBox.Y + 6.0f }, 8.2f, InspectorMetrics::DimInk, "APERTURE / FRONT ELEVATION");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 4.0f, ImgBox.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, "FRONT FACE");
        char SpBuf[24]{};
        std::snprintf(SpBuf, sizeof(SpBuf), "%.0f\xC2\xB0 SPREAD", Instance.SpreadDegrees);
        const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, SpBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Right() - SW - 4.0f, ImgBox.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, SpBuf);
        char WDim[16]{};
        std::snprintf(WDim, sizeof(WDim), "%.1f m", Instance.WidthMeters);
        const float WDW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, WDim);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - WDW * 0.5f, ImgBox.Bottom() - 28.0f }, 8.2f, InspectorMetrics::DimInk, WDim);

        char AreaBuf[24]{};
        std::snprintf(AreaBuf, sizeof(AreaBuf), "%.2g m\xC2\xB2", AreaM2);
        LightUi::ConstructStudyTilesAndFooter(
            Surface, BodyX, ImgBox.Bottom() + 24.0f, BodyW,
            "Source study \xC2\xB7 photometric summary",
            "Aperture", AreaBuf, nullptr,
            "Emission", Instance.TwoSidedRadiator ? "Two-sided" : "One-sided", nullptr,
            "Source & colour", "Luminous flux");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 2: SOURCE — Emitter dimensions (h = 576) + Transform (WORLD SPACE)
        const Rectangle2D Card { WideX, CursorY, WideW, 576.0f };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Emitter dimensions");
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "OPTICS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - OW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "OPTICS");

        char FluxArea[32]{};
        LightUi::FormatWithCommas(Instance.LuminousFlux / std::max(0.01f, AreaM2), FluxArea, sizeof(FluxArea));
        std::string FaStr = std::string(FluxArea) + " lm/m\xC2\xB2";
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 58.0f }, 34.0f, ColorRgba{ 244u, 244u, 244u, 255u }, FaStr);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 108.0f }, 11.0f, InspectorMetrics::MutedInk, "Flux per aperture area \xC2\xB7 not surface luminance");

        // Lambertian cosine angular distribution (-90°..90°)
        const Rectangle2D Plot { BodyX, Card.Y + 138.0f, BodyW, 88.0f };
        LightUi::ConstructMinimalOpticGrid(Surface, Plot, "\xE2\x88\x92""90\xC2\xB0", "90\xC2\xB0");
        const int Segs = 60;
        Point2D Prev {};
        for (int I = 0; I <= Segs; ++I)
        {
            const float T   = static_cast<float>(I) / static_cast<float>(Segs);
            const float Rad = (-1.570796f) + T * 3.14159265f;
            const float Amp = std::max(0.0f, std::cos(Rad));
            const Point2D P { Plot.X + T * Plot.Width, (Plot.Bottom() - 16.0f) - Amp * (Plot.Height - 20.0f) };
            if (I > 0) Surface.StrokeLine(Prev, P, ColorRgba{ 212u, 206u, 192u, 255u }, 1.5f);
            Prev = P;
        }

        // Rectangle | Disk 2-button toggle
        const float SegY = Plot.Bottom() + 10.0f;
        const float SegW = std::floor((BodyW - 8.0f) * 0.5f);
        static constexpr std::array<const char*, 2u> Forms = {{ "Rectangle", "Disk" }};
        for (uint32_t I = 0u; I < 2u; ++I)
        {
            const Rectangle2D B { BodyX + static_cast<float>(I) * (SegW + 8.0f), SegY, SegW, 34.0f };
            const bool Sel = (Instance.ApertureFormIndex == I);
            Surface.FillRectangle(B, Sel ? ColorRgba{ 62u, 62u, 65u, 255u } : ColorRgba{ 32u, 32u, 34u, 255u }, 10.0f);
            Surface.StrokeRectangle(B, Sel ? ColorRgba{ 165u, 165u, 170u, 150u } : ColorRgba{ 255u, 255u, 255u, 18u }, 10.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.4f, Forms[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { B.Center().X - TW * 0.5f, B.Y + 10.5f }, 11.4f, Sel ? InspectorMetrics::HeadingInk : InspectorMetrics::MutedInk, Forms[I]);
            if (B.Contains(Cursor) && Input.PrimaryPointer.Pressed) { Instance.ApertureFormIndex = I; OutMutated = true; }
        }

        float FY = SegY + 50.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8621u, "Width",       "m",        "%.2g", 0.1f, 10.0f, Instance.WidthMeters,  ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8622u, "Height",      "m",        "%.2g", 0.1f, 10.0f, Instance.HeightMeters, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8623u, "Beam spread", "\xC2\xB0", "%.0f", 15.0f, 180.0f, Instance.SpreadDegrees, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, BodyX, FY + 4.0f, BodyW, "Two-sided emission", Instance.TwoSidedRadiator, Input, OutMutated);
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
        CursorY = LightUi::ConstructTransformCard(Surface, WideX, CursorY, WideW, Entry);
    }

    return CursorY;
}

} // namespace Frontier
