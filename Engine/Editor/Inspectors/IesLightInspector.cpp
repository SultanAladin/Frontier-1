/*============================================================================================================================================
                                                          IESLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of IES Photometric Light Inspector (2 tabs: Overview, Source)

#include "Engine/Editor/Inspectors/IesLightInspector.h"
#include "Engine/Editor/Inspectors/LightInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampByteIes(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

[[nodiscard]] float EvaluateIesCurve(uint32_t Preset, float Deg) noexcept
{
    if (Deg >= 90.0f) return 0.0f;
    const float U = Deg / 90.0f;
    if (Preset == 3u) // ECE Low Beam
    {
        if (Deg < 12.0f) return 1.0f - (Deg / 12.0f) * 0.36f;
        return std::max(0.0f, 0.64f * std::pow(1.0f - (Deg - 12.0f) / 78.0f, 1.6f));
    }
    return std::pow(std::max(0.0f, std::cos(U * 1.570796f)), (Preset == 0u) ? 2.2f : 1.3f);
}
} // namespace

void IesLightInspector::UpdatePolarSurface(const IesLightData& Instance)
{
    uint64_t Key = 0x11030001ull ^ (static_cast<uint64_t>(Instance.ProfilePresetIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.LuminousFlux);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.FieldAngleDegrees);

    const uint32_t W = 524u, H = 376u;
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::PhotometricLightPreview, 2u,
        { Instance.LuminousFlux * 0.0008f, static_cast<float>(Instance.ProfilePresetIndex), Instance.FieldAngleDegrees, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.72f, 0.84f, 0.98f, 1.0f }
    };
    PolarPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Cx = static_cast<float>(W) * 0.5f;
        const float Cy = static_cast<float>(H) * 0.49f;
        const float MaxR = 148.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - Cx;
                const float DY = static_cast<float>(Y) - Cy;
                const float D  = std::sqrt(DX * DX + DY * DY);

                float R = 24.0f, G = 24.0f, B = 25.0f;
                for (int K = 1; K <= 4; ++K)
                {
                    const float RR = MaxR * (static_cast<float>(K) * 0.25f);
                    if (std::fabs(D - RR) < 1.0f) { R += 16.0f; G += 17.0f; B += 19.0f; }
                }

                // Polar candela lobe (downward = 0 deg -> +Y)
                const float DegFromDown = std::atan2(std::fabs(DX), DY) * 57.29578f;
                if (DY >= 0.0f)
                {
                    const bool RightSide = (DX >= 0.0f);
                    float LobeVal = EvaluateIesCurve(Instance.ProfilePresetIndex, DegFromDown);
                    if (Instance.ProfilePresetIndex == 3u && !RightSide) LobeVal = (DegFromDown < 3.5f) ? LobeVal : 0.0f;
                    const float LobeR = LobeVal * MaxR;
                    if (D <= LobeR)
                    {
                        R += 28.0f; G += 24.0f; B += 16.0f;
                    }
                    if (std::fabs(D - LobeR) < 1.6f)
                    {
                        R = 228.0f; G = 186.0f; B = 132.0f;
                    }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteIes(R);
                Pixels[Off + 1u] = ClampByteIes(G);
                Pixels[Off + 2u] = ClampByteIes(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float IesLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, IesLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    // Auto-detect ECE Low Beam preset when Entry title mentions ECE
    if (Entry.Title.find("ECE") != std::string::npos && Instance.ProfilePresetIndex == 0u)
    {
        Instance.ProfilePresetIndex = 3u;
    }

    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    static constexpr std::array<const char*, 8u> PresetNames = {{
        "Downlight", "Wall wash", "Batwing", "ECE Low Beam", "SAE Low Beam", "High Beam", "Fog Lamp", "Parking Lamp"
    }};
    const uint32_t PresetIdx = std::min(Instance.ProfilePresetIndex, 7u);

    if (ActivePage == 0u)
    {
        UpdatePolarSurface(Instance);
        const float ImgH = LightUi::ComputeOverviewPreviewHeight(BodyW);
        const Rectangle2D Card { WideX, CursorY, WideW, LightUi::ComputeOverviewCardHeight(ImgH) };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);

        WorkspaceAppearance::ConstructEnvironmentSymbolIcon(Surface, EnvironmentSymbolCategory::IesLight, { BodyX, Card.Y + 21.0f }, 20.0f);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { BodyX + 28.0f, Card.Y + 23.0f }, BodyW - 28.0f, 16.4f, InspectorMetrics::HeadingInk, "Photometric distribution");

        float Y = LightUi::ConstructGiantMetric(Surface, BodyX, Card.Y + 64.0f, Instance.LuminousFlux, "lm", "Profile source flux");
        Surface.StrokeLine({ BodyX, Y }, { BodyX + BodyW, Y }, LightUi::DividerInk, 1.0f);
        Y += 16.0f;

        char SumBuf[64]{};
        std::snprintf(SumBuf, sizeof(SumBuf), "%s / polar study", PresetNames[PresetIdx]);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Y }, 11.8f, InspectorMetrics::MutedInk, SumBuf);
        Y += 26.0f;

        const Rectangle2D ImgBox { BodyX, Y + 8.0f, BodyW, ImgH };
        PolarPreview.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, LightUi::CardFill);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 8.0f, ImgBox.Y + 6.0f }, 8.4f, ColorRgba{ 222u, 168u, 114u, 255u }, "C0");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Right() - 28.0f, ImgBox.Y + 6.0f }, 8.4f, ColorRgba{ 126u, 186u, 186u, 255u }, "C90");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.X + 28.0f, ImgBox.Center().Y - 5.0f }, 8.4f, InspectorMetrics::DimInk, "90\xC2\xB0");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Right() - 46.0f, ImgBox.Center().Y - 5.0f }, 8.4f, InspectorMetrics::DimInk, "90\xC2\xB0");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - 5.0f, ImgBox.Bottom() - 28.0f }, 8.4f, InspectorMetrics::DimInk, "0\xC2\xB0");
        const char* Cap = "NORMALIZED PRESET";
        const float CW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.4f, Cap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ImgBox.Center().X - CW * 0.5f, ImgBox.Bottom() - 14.0f }, 8.4f, InspectorMetrics::DimInk, Cap);

        char ScaledBuf[32]{}, FieldBuf[16]{};
        LightUi::FormatWithCommas(Instance.LuminousFlux * Instance.ProfileMultiplier, ScaledBuf, sizeof(ScaledBuf));
        std::string ScaledStr = std::string(ScaledBuf) + " lm";
        std::snprintf(FieldBuf, sizeof(FieldBuf), "%.0f\xC2\xB0", Instance.FieldAngleDegrees);
        LightUi::ConstructStudyTilesAndFooter(
            Surface, BodyX, ImgBox.Bottom() + 24.0f, BodyW,
            "Illustrative preset \xC2\xB7 polar photometry",
            "Scaled output", ScaledStr.c_str(), nullptr,
            "Field angle", FieldBuf, nullptr,
            "Source & colour", "Luminous flux");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 2: SOURCE — Distribution profile curve editor + 4 pills + 4 KPIs + Name/Actions + Preset grid
        const Rectangle2D Card { WideX, CursorY, WideW, 602.0f };
        LightUi::ConstructDarkLuminaireCard(Surface, Card);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 24.0f }, 16.4f, InspectorMetrics::HeadingInk, "Distribution profile");
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, "OPTICS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - OW, Card.Y + 28.0f }, 9.6f, InspectorMetrics::DimInk, "OPTICS");

        // Curve editor well
        const Rectangle2D Well { BodyX, Card.Y + 56.0f, BodyW, 152.0f };
        Surface.FillRectangle(Well, ColorRgba{ 16u, 16u, 17u, 255u }, 12.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 16u }, 12.0f, 1.0f);
        const Rectangle2D PBox { Well.X + 28.0f, Well.Y + 14.0f, Well.Width - 36.0f, Well.Height - 36.0f };
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 4.0f, PBox.Y - 4.0f }, 8.0f, InspectorMetrics::DimInk, "100%");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 8.0f, PBox.Center().Y - 4.0f }, 8.0f, InspectorMetrics::DimInk, "50%");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 18.0f, PBox.Bottom() - 4.0f }, 8.0f, InspectorMetrics::DimInk, "0");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PBox.X + PBox.Width * 0.17f, PBox.Y + 4.0f }, 8.0f, InspectorMetrics::DimInk, "beam");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PBox.X + PBox.Width * 0.37f, PBox.Y + 4.0f }, 8.0f, InspectorMetrics::DimInk, "field");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PBox.X + PBox.Width * 0.68f, PBox.Y + 4.0f }, 8.0f, InspectorMetrics::DimInk, "UPWARD");

        const std::array<float, 11u> KnotsDeg = {{ 0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 75.0f, 90.0f, 120.0f, 180.0f }};
        Point2D Prev {};
        for (std::size_t I = 0u; I < KnotsDeg.size(); ++I)
        {
            const float T = KnotsDeg[I] / 180.0f;
            const float V = EvaluateIesCurve(PresetIdx, KnotsDeg[I]);
            const Point2D P { PBox.X + T * PBox.Width, PBox.Bottom() - V * PBox.Height };
            if (I > 0u) Surface.StrokeLine(Prev, P, ColorRgba{ 232u, 178u, 118u, 255u }, 1.8f);
            Surface.FillCircle(P, 3.6f, ColorRgba{ 20u, 20u, 22u, 255u });
            Surface.StrokeCircle(P, 3.6f, ColorRgba{ 246u, 246u, 246u, 255u }, 1.4f);
            Prev = P;
        }

        // 4 mode pills: C0 plane | C90 plane | Linked | Smooth
        const float PillY = Well.Bottom() + 12.0f;
        const std::array<std::pair<const char*, bool>, 4u> Pills = {{
            { "C0 plane", true }, { "C90 plane", false }, { "Linked", false }, { "Smooth", true }
        }};
        const float PGap = 6.0f;
        const float PW = std::floor((BodyW - PGap * 3.0f) / 4.0f);
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D P { BodyX + static_cast<float>(I) * (PW + PGap), PillY, PW, 28.0f };
            Surface.FillRectangle(P, Pills[I].second ? ColorRgba{ 54u, 54u, 58u, 255u } : ColorRgba{ 28u, 28u, 30u, 255u }, 14.0f);
            Surface.StrokeRectangle(P, Pills[I].second ? ColorRgba{ 165u, 165u, 172u, 150u } : ColorRgba{ 255u, 255u, 255u, 20u }, 14.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, Pills[I].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { P.Center().X - TW * 0.5f, P.Y + 8.5f }, 9.6f, Pills[I].second ? InspectorMetrics::HeadingInk : InspectorMetrics::MutedInk, Pills[I].first);
        }

        // 4 mini KPI tiles: Peak, Beam, Field, Upward
        const float KpiY = PillY + 40.0f;
        const std::array<std::pair<const char*, const char*>, 4u> Kpis = {{
            { "1,16...", "Peak" }, { "54\xC2\xB0", "Beam" }, { "127\xC2\xB0", "Field" }, { "0.0 %", "Upward" }
        }};
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D K { BodyX + static_cast<float>(I) * (PW + PGap), KpiY, PW, 54.0f };
            Surface.FillRectangle(K, LightUi::TileFill, 10.0f);
            Surface.StrokeRectangle(K, LightUi::TileBorder, 10.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K.X + 7.0f, K.Y + 12.0f }, 13.0f, InspectorMetrics::HeadingInk, Kpis[I].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K.X + 7.0f, K.Y + 34.0f }, 9.2f, InspectorMetrics::DimInk, Kpis[I].second);
        }

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, KpiY + 68.0f, BodyW, 10.6f, 15.0f, InspectorMetrics::MutedInk,
            "ECE Low Beam preset \xC2\xB7 drag a point to start a custom profile from it.");

        // Name + Profile name input
        const float NameY = KpiY + 100.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, NameY + 8.0f }, 11.2f, InspectorMetrics::MutedInk, "Name");
        const Rectangle2D NameBox { BodyX + 46.0f, NameY, BodyW - 46.0f, 30.0f };
        Surface.FillRectangle(NameBox, ColorRgba{ 14u, 14u, 15u, 255u }, 15.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { NameBox.X + 12.0f, NameBox.Y + 8.5f }, 11.2f, InspectorMetrics::DimInk, "Profile name");

        // 3 action buttons + Reset to preset
        const float ActY = NameY + 38.0f;
        const std::array<const char*, 3u> Acts = {{ "Save to library", "Export .ies", "Import .ies" }};
        const float AW = std::floor((BodyW - 12.0f) / 3.0f);
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D B { BodyX + static_cast<float>(I) * (AW + 6.0f), ActY, AW, 28.0f };
            Surface.FillRectangle(B, ColorRgba{ 34u, 34u, 36u, 255u }, 14.0f);
            Surface.StrokeRectangle(B, ColorRgba{ 255u, 255u, 255u, 20u }, 14.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, Acts[I]);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { B.X + std::max(4.0f, (B.Width - TW) * 0.5f), B.Y + 8.5f }, B.Width - 8.0f, 9.6f, InspectorMetrics::MutedInk, Acts[I]);
        }
        const Rectangle2D ResetBtn { BodyX, ActY + 34.0f, BodyW, 28.0f };
        Surface.FillRectangle(ResetBtn, ColorRgba{ 34u, 34u, 36u, 255u }, 14.0f);
        Surface.StrokeRectangle(ResetBtn, ColorRgba{ 255u, 255u, 255u, 20u }, 14.0f, 1.0f);
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, "Reset to preset");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ResetBtn.Center().X - RW * 0.5f, ResetBtn.Y + 8.0f }, 10.6f, InspectorMetrics::MutedInk, "Reset to preset");

        // Start from a preset 2x2 buttons
        const float PreY = ResetBtn.Bottom() + 18.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PreY }, 11.0f, InspectorMetrics::MutedInk, "Start from a preset");
        const float PreW = std::floor((BodyW - 8.0f) * 0.5f);
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D PB { BodyX + static_cast<float>(I % 2u) * (PreW + 8.0f), PreY + 18.0f + static_cast<float>(I / 2u) * 38.0f, PreW, 30.0f };
            const bool Sel = (PresetIdx == I);
            Surface.FillRectangle(PB, Sel ? ColorRgba{ 54u, 54u, 58u, 255u } : ColorRgba{ 30u, 30u, 32u, 255u }, 10.0f);
            Surface.StrokeRectangle(PB, Sel ? ColorRgba{ 160u, 160u, 168u, 150u } : ColorRgba{ 255u, 255u, 255u, 18u }, 10.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.8f, PresetNames[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PB.Center().X - TW * 0.5f, PB.Y + 9.0f }, 10.8f, Sel ? InspectorMetrics::HeadingInk : InspectorMetrics::MutedInk, PresetNames[I]);
            if (PB.Contains(Cursor) && Input.PrimaryPointer.Pressed) { Instance.ProfilePresetIndex = I; OutMutated = true; }
        }
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
