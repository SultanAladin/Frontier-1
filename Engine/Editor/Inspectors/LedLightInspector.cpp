/*============================================================================================================================================
                                                          LEDLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of LED Emitter Inspector (2 tabs: Overview, Source)

#include "Engine/Editor/Inspectors/LedLightInspector.h"
#include "Engine/Editor/Inspectors/LightInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampByteLd(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}
} // namespace

void LedLightInspector::UpdatePackageSurface(const LedLightData& Instance)
{
    uint64_t Key = 0x11060001ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.DriverWatts);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.PackageDiameterMm);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.OpticAngleDeg);

    const uint32_t W = 524u, H = 376u;
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::PhotometricLightPreview, 5u,
        { Instance.DriverWatts * 0.04f, Instance.OpticAngleDeg, Instance.PackageDiameterMm, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.92f, 0.72f, 1.0f }
    };
    PackagePreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.5f;
        const float Cy = 186.0f;
        const float PkgHalf = 104.0f;
        const float InnerHalf = 84.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                float R = 24.0f, G = 24.0f, B = 25.0f;
                const float DX = std::fabs(static_cast<float>(X) - Cx);
                const float DY = std::fabs(static_cast<float>(Y) - Cy);

                // Ceramic COB square package with rounded bezel
                if (DX <= PkgHalf && DY <= PkgHalf)
                {
                    R = 46.0f; G = 48.0f; B = 45.0f;
                    if (DX > PkgHalf - 2.0f || DY > PkgHalf - 2.0f) { R = 112.0f; G = 114.0f; B = 108.0f; }
                }
                // Warm phosphor interior well with 4x4 bright LED dies
                if (DX <= InnerHalf && DY <= InnerHalf)
                {
                    R = 114.0f; G = 96.0f; B = 78.0f;
                    const float LX = (static_cast<float>(X) - (Cx - InnerHalf)) / (InnerHalf * 2.0f) * 4.0f;
                    const float LY = (static_cast<float>(Y) - (Cy - InnerHalf)) / (InnerHalf * 2.0f) * 4.0f;
                    const float Fx = LX - std::floor(LX);
                    const float Fy = LY - std::floor(LY);
                    if (Fx > 0.22f && Fx < 0.78f && Fy > 0.22f && Fy < 0.78f)
                    {
                        R = 236.0f; G = 198.0f; B = 158.0f;
                    }
                }
                // Side gold SMD solder pins (5 per side)
                if (DX > PkgHalf + 4.0f && DX < PkgHalf + 18.0f && DY < 84.0f)
                {
                    const int PinMod = static_cast<int>(std::lround(static_cast<float>(Y) - (Cy - 72.0f))) % 36;
                    if (PinMod >= 0 && PinMod < 5) { R = 158.0f; G = 144.0f; B = 112.0f; }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteLd(R);
                Pixels[Off + 1u] = ClampByteLd(G);
                Pixels[Off + 2u] = ClampByteLd(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LedLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LedLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const float EstLm = Instance.DriverWatts * Instance.EfficacyLmPerW * Instance.Dimmer;

    if (ActivePage == 0u)
    {
        UpdatePackageSurface(Instance);
        const float ImgH = LightUi::ComputeOverviewPreviewHeight(BodyW);
        const Rectangle2D Card { WideX, CursorY, WideW, LightUi::ComputeOverviewCardHeight(ImgH) };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);

        WorkspaceAppearance::ConstructEnvironmentSymbolIcon(Surface, EnvironmentSymbolCategory::LedLight, { BodyX, Card.Y + 21.0f }, 20.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 28.0f, Card.Y + 23.0f }, 16.4f, InspectorMetrics::HeadingInk, "LED emitter");

        float Y = LightUi::ConstructGiantMetric(Surface, BodyX, Card.Y + 64.0f, EstLm, "lm", "Estimated luminous flux");
        Surface.StrokeLine({ BodyX, Y }, { BodyX + BodyW, Y }, LightUi::DividerInk, 1.0f);
        Y += 16.0f;

        char SumBuf[48]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "%.0f mm package / %.0f K", Instance.PackageDiameterMm, Instance.KelvinTemperature);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Y }, 11.8f, InspectorMetrics::MutedInk, SumBuf);
        Y += 26.0f;

        const Rectangle2D ImgBox { BodyX, Y + 8.0f, BodyW, ImgH };
        PackagePreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, LightUi::CardFill);
        char OptCap[24]{};
        std::snprintf(OptCap, sizeof(OptCap), "%.0f\xC2\xB0 OPTIC", Instance.OpticAngleDeg);
        const float OCW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, OptCap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - OCW * 0.5f, ImgBox.Y + 6.0f }, 8.2f, InspectorMetrics::DimInk, OptCap);
        char PkgCap[28]{};
        std::snprintf(PkgCap, sizeof(PkgCap), "%.0f mm PACKAGE", Instance.PackageDiameterMm);
        const float PCW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, PkgCap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - PCW * 0.5f, ImgBox.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, PkgCap);

        char PwrBuf[16]{}, EffBuf[24]{};
        std::snprintf(PwrBuf, sizeof(PwrBuf), "%.0f W", Instance.DriverWatts);
        std::snprintf(EffBuf, sizeof(EffBuf), "%.0f lm/W", Instance.EfficacyLmPerW);
        LightUi::ConstructStudyTilesAndFooter(
            Surface, BodyX, ImgBox.Bottom() + 24.0f, BodyW,
            "Source study \xC2\xB7 photometric summary",
            "Driver power", PwrBuf, nullptr,
            "Efficacy target", EffBuf, nullptr,
            "Driver & colour", "Driver power");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 2: SOURCE — Package & optic (h = 426) + Transform (WORLD SPACE)
        const Rectangle2D Card { WideX, CursorY, WideW, 426.0f };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Package & optic");
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "OPTICS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - OW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "OPTICS");

        char DimBuf[16]{};
        std::snprintf(DimBuf, sizeof(DimBuf), "%.0f %%", Instance.Dimmer * 100.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 58.0f }, 36.0f, ColorRgba{ 244u, 244u, 244u, 255u }, DimBuf);
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, Card.Y + 106.0f, BodyW, 11.0f, 15.5f, InspectorMetrics::MutedInk,
            "Authored dimmer \xC2\xB7 estimated output ignores thermal losses");

        const Rectangle2D Plot { BodyX, Card.Y + 148.0f, BodyW, 88.0f };
        LightUi::ConstructMinimalOpticGrid(Surface, Plot, "0", "100%");
        Surface.StrokeLine({ Plot.X, Plot.Bottom() - 16.0f }, { Plot.Right(), Plot.Y + 4.0f }, ColorRgba{ 226u, 182u, 144u, 255u }, 1.6f);

        float FY = Plot.Bottom() + 18.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8641u, "Package diameter", "mm",        "%.0f", 5.0f, 120.0f, Instance.PackageDiameterMm, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8642u, "Emission angle",   "\xC2\xB0", "%.0f", 15.0f, 160.0f, Instance.OpticAngleDeg,    ActiveDragId, Input, OutMutated);
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
        CursorY = LightUi::ConstructTransformCard(Surface, WideX, CursorY, WideW, Entry);
    }

    return CursorY;
}

} // namespace Frontier
