/*============================================================================================================================================
                                                         SPOTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Spot Light Inspector (2 tabs: Overview, Source)

#include "Engine/Editor/Inspectors/SpotLightInspector.h"
#include "Engine/Editor/Inspectors/LightInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampByteSp(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}
} // namespace

void SpotLightInspector::UpdateEnvelopeSurface(const SpotLightData& Instance)
{
    uint64_t Key = 0x11020001ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.FullConeDegrees);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.Penumbra);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.IntensityCandela);

    const uint32_t W = 524u, H = 376u;
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::PhotometricLightPreview, 1u,
        { Instance.IntensityCandela * 0.001f, Instance.FullConeDegrees * (1.0f - Instance.Penumbra), Instance.FullConeDegrees, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.96f, 0.88f, 0.72f, 1.0f }
    };
    EnvelopePreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.5f;
        const float ApexY = 82.0f, BaseY = 304.0f;
        const float HalfSpan = std::clamp(std::tan(Instance.FullConeDegrees * 0.5f * 0.01745329f) * 290.0f, 34.0f, 160.0f);
        const float InnerFrac = std::clamp(1.0f - Instance.Penumbra, 0.15f, 0.92f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                float R = 24.0f, G = 24.0f, B = 25.0f;
                const float DX = std::fabs(static_cast<float>(X) - Cx);
                if (Y >= ApexY && Y <= BaseY)
                {
                    const float T = (static_cast<float>(Y) - ApexY) / (BaseY - ApexY);
                    const float WOuter = HalfSpan * T;
                    const float WInner = WOuter * InnerFrac;
                    if (DX <= WOuter)
                    {
                        const float Soft = (DX <= WInner) ? 1.0f : std::clamp(1.0f - (DX - WInner) / std::max(1.0f, WOuter - WInner), 0.0f, 1.0f);
                        const float Fill = (24.0f + 16.0f * Soft) * (0.65f + 0.35f * (1.0f - T));
                        R += Fill; G += Fill; B += Fill * 0.96f;
                    }
                    if (std::fabs(DX - WOuter) < 1.3f) { R = 148.0f; G = 148.0f; B = 146.0f; }
                    if (std::fabs(DX - WInner) < 1.1f && ((Y / 8u) % 2u == 0u)) { R = 136.0f; G = 136.0f; B = 134.0f; }
                }
                if (std::fabs(static_cast<float>(Y) - BaseY) < 1.3f && DX <= HalfSpan)
                {
                    R = 148.0f; G = 148.0f; B = 146.0f;
                }
                // Lamp head capsule at top
                if (Y >= 46u && Y <= 80u && DX <= 26.0f)
                {
                    R = 72.0f; G = 75.0f; B = 70.0f;
                    if (Y <= 48u || Y >= 78u || DX >= 24.0f) { R = 152.0f; G = 154.0f; B = 146.0f; }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteSp(R);
                Pixels[Off + 1u] = ClampByteSp(G);
                Pixels[Off + 2u] = ClampByteSp(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float SpotLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, SpotLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);

    if (ActivePage == 0u)
    {
        UpdateEnvelopeSurface(Instance);
        const float ImgH = LightUi::ComputeOverviewPreviewHeight(BodyW);
        const Rectangle2D Card { WideX, CursorY, WideW, LightUi::ComputeOverviewCardHeight(ImgH) };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);

        WorkspaceAppearance::ConstructEnvironmentSymbolIcon(Surface, EnvironmentSymbolCategory::SpotLight, { BodyX, Card.Y + 21.0f }, 20.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 28.0f, Card.Y + 23.0f }, 16.4f, InspectorMetrics::HeadingInk, "Beam envelope");

        float Y = LightUi::ConstructGiantMetric(Surface, BodyX, Card.Y + 64.0f, Instance.IntensityCandela, "cd", "Authored luminous intensity");
        Surface.StrokeLine({ BodyX, Y }, { BodyX + BodyW, Y }, LightUi::DividerInk, 1.0f);
        Y += 16.0f;

        char SumBuf[48]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "%.0f\xC2\xB0 cone / %.0f%% soft edge", Instance.FullConeDegrees, Instance.Penumbra * 100.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Y }, 11.8f, InspectorMetrics::MutedInk, SumBuf);
        Y += 26.0f;

        const Rectangle2D ImgBox { BodyX, Y + 8.0f, BodyW, ImgH };
        EnvelopePreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, LightUi::CardFill);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 4.0f, ImgBox.Y + 6.0f }, 8.4f, InspectorMetrics::DimInk, "PENUMBRA");
        char PenBuf[16]{};
        std::snprintf(PenBuf, sizeof(PenBuf), "%.0f %%", Instance.Penumbra * 100.0f);
        const float PW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.4f, PenBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Right() - PW - 4.0f, ImgBox.Y + 6.0f }, 8.4f, InspectorMetrics::DimInk, PenBuf);
        char CapBuf[32]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%.0f\xC2\xB0 FULL CONE", Instance.FullConeDegrees);
        const float CW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.4f, CapBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - CW * 0.5f, ImgBox.Bottom() - 14.0f }, 8.4f, InspectorMetrics::DimInk, CapBuf);

        char ConeVal[16]{}, SoftVal[16]{};
        std::snprintf(ConeVal, sizeof(ConeVal), "%.0f\xC2\xB0", Instance.FullConeDegrees);
        std::snprintf(SoftVal, sizeof(SoftVal), "%.0f %%", Instance.Penumbra * 100.0f);
        LightUi::ConstructStudyTilesAndFooter(
            Surface, BodyX, ImgBox.Bottom() + 24.0f, BodyW,
            "Source study \xC2\xB7 photometric summary",
            "Full cone", ConeVal, nullptr,
            "Soft edge", SoftVal, nullptr,
            "Source & colour", "Intensity");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 2: SOURCE — Beam shaping (h = 416) + Transform (WORLD SPACE)
        const Rectangle2D Card { WideX, CursorY, WideW, 416.0f };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Beam shaping");
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "OPTICS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - OW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "OPTICS");

        const float Diam5m = 2.0f * 5.0f * std::tan(Instance.FullConeDegrees * 0.5f * 0.01745329f);
        char DiamBuf[24]{};
        std::snprintf(DiamBuf, sizeof(DiamBuf), "%.2f m", Diam5m);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 58.0f }, 36.0f, ColorRgba{ 244u, 244u, 244u, 255u }, DiamBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 108.0f }, 11.0f, InspectorMetrics::MutedInk, "Beam diameter on a perpendicular plane at 5 m");

        // Top-hat angular beam profile (-90°..90°)
        const Rectangle2D Plot { BodyX, Card.Y + 138.0f, BodyW, 88.0f };
        LightUi::ConstructMinimalOpticGrid(Surface, Plot, "\xE2\x88\x92""90\xC2\xB0", "90\xC2\xB0");
        const float HalfDeg  = std::max(2.0f, Instance.FullConeDegrees * 0.5f);
        const float InnerDeg = HalfDeg * std::clamp(1.0f - Instance.Penumbra, 0.1f, 0.95f);
        const int Segs = 80;
        Point2D Prev {};
        for (int I = 0; I <= Segs; ++I)
        {
            const float T   = static_cast<float>(I) / static_cast<float>(Segs);
            const float Deg = std::fabs(-90.0f + T * 180.0f);
            float Amp = 0.0f;
            if (Deg <= InnerDeg) Amp = 1.0f;
            else if (Deg < HalfDeg)
            {
                const float U = (Deg - InnerDeg) / std::max(0.1f, HalfDeg - InnerDeg);
                Amp = 0.5f * (1.0f + std::cos(U * 3.14159265f));
            }
            const Point2D P { Plot.X + T * Plot.Width, (Plot.Bottom() - 16.0f) - Amp * (Plot.Height - 20.0f) };
            if (I > 0) Surface.StrokeLine(Prev, P, ColorRgba{ 212u, 206u, 192u, 255u }, 1.5f);
            Prev = P;
        }

        float FY = Plot.Bottom() + 18.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8611u, "Full cone angle", "\xC2\xB0", "%.0f", 5.0f, 120.0f, Instance.FullConeDegrees, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8612u, "Penumbra", "", "%.2g", 0.0f, 1.0f, Instance.Penumbra, ActiveDragId, Input, OutMutated);
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
        CursorY = LightUi::ConstructTransformCard(Surface, WideX, CursorY, WideW, Entry);
    }

    return CursorY;
}

} // namespace Frontier
