/*============================================================================================================================================
                                                            CLOUDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance CloudInspector (KPI metrics, Coverage, CloudDeck cross-section, Body, Wind, Quality)

#include "Engine/Editor/Inspectors/CloudInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

inline void PutPixel(std::vector<uint8_t>& Buf, uint32_t W, uint32_t X, uint32_t Y, uint8_t R, uint8_t G, uint8_t B) noexcept
{
    const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
    Buf[Off + 0u] = R; Buf[Off + 1u] = G; Buf[Off + 2u] = B; Buf[Off + 3u] = 255u;
}
} // namespace

void CloudInspector::UpdateCloudDeckSurface(const GeometricRaster::CloudDeckConfiguration& Clouds)
{
    uint64_t Sig = 0xC1007007ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.CloudFloorMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.LayerThicknessMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.ExtinctionMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.CloudFraction);

    const uint32_t W = 224u, H = 176u;
    CloudDeckPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float SpanAlt = std::max(400.0f, std::ceil(((Clouds.CloudFloorMeters + Clouds.LayerThicknessMeters) * 1.25f) / 500.0f) * 500.0f);
        auto AltToY = [&](float Alt) noexcept -> float
        {
            return static_cast<float>(H) - 18.0f - (Alt / SpanAlt) * (static_cast<float>(H) - 44.0f);
        };
        const float DatumY  = AltToY(Clouds.CloudFloorMeters);
        const float DepthPx = (Clouds.LayerThicknessMeters / SpanAlt) * (static_cast<float>(H) - 44.0f);
        const float Dens01  = std::clamp(Clouds.ExtinctionMultiplier / 4.0f, 0.12f, 1.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX   = static_cast<float>(X);
                const float TopY = DatumY - DepthPx * (0.65f + 0.28f * std::sin(FX * 0.08f) + 0.12f * std::sin(FX * 0.31f));
                float R = 6.0f / 255.0f, G = 7.0f / 255.0f, B = 9.0f / 255.0f;
                if (FY >= TopY && FY <= DatumY + 4.0f)
                {
                    const float T = std::clamp((FY - TopY) / std::max(1.0f, (DatumY + 4.0f) - TopY), 0.0f, 1.0f);
                    const float A = (T < 0.35f) ? (0.08f + (T / 0.35f) * 0.68f * Dens01) : (0.74f * Dens01);
                    R = R * (1.0f - A) + ((1.0f - T) * 0.93f + T * 0.36f) * A;
                    G = G * (1.0f - A) + ((1.0f - T) * 0.95f + T * 0.42f) * A;
                    B = B * (1.0f - A) + ((1.0f - T) * 0.97f + T * 0.50f) * A;
                }
                for (int32_t Line = 0; Line <= 4; ++Line)
                {
                    if (std::fabs(FY - AltToY(SpanAlt * static_cast<float>(Line) * 0.25f)) < 0.6f)
                    {
                        R += 0.06f; G += 0.06f; B += 0.07f;
                    }
                }
                if (std::fabs(FY - DatumY) < 0.8f && ((X / 4u) % 2u == 0u)) { R = 0.82f; G = 0.84f; B = 0.88f; }
                PutPixel(Pixels, W, X, Y,
                    static_cast<uint8_t>(std::clamp(std::lround(R * 255.0f), 0l, 255l)),
                    static_cast<uint8_t>(std::clamp(std::lround(G * 255.0f), 0l, 255l)),
                    static_cast<uint8_t>(std::clamp(std::lround(B * 255.0f), 0l, 255l)));
            }
        }
    });
}

float CloudInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    CloudInstanceData&      Instance,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 6u> Pages = {{
        "Overview", "Coverage", "Layer", "Body", "Wind", "Quality",
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Clouds = Instance.Clouds;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // Cloud deck altitude cross-section hero (1:1 HTML parity above KPI grid)
        UpdateCloudDeckSurface(Clouds);
        {
            const Rectangle2D Hero { LeftX, CursorY, InnerWidth, 176.0f };
            Surface.FillRectangle(Hero, ColorRgba{ 22u, 22u, 24u, 255u }, InspectorMetrics::SectionRadius);
            Surface.StrokeRectangle(Hero, InspectorMetrics::SectionBorder, InspectorMetrics::SectionRadius, 1.0f);
            const Rectangle2D Inner { Hero.X + 8.0f, Hero.Y + 8.0f, Hero.Width - 16.0f, Hero.Height - 16.0f };
            CloudDeckPreview.Present(Surface, Inner, 18.0f);
            CursorY = Hero.Bottom() + InspectorMetrics::SectionGap;
        }

        char Cov[16]{}, Opt[16]{}, Flr[16]{}, Drf[16]{};
        std::snprintf(Cov, sizeof(Cov), "%.0f", Clouds.CloudFraction * 100.0f);
        std::snprintf(Opt, sizeof(Opt), "%.1f", Clouds.ExtinctionMultiplier * 3.0f);
        std::snprintf(Flr, sizeof(Flr), "%.0f", Clouds.CloudFloorMeters);
        std::snprintf(Drf, sizeof(Drf), "%.1f", std::max(1.0f, Clouds.WindSpeedMetersPerSecond * 0.14f));
        const std::array<InspectorKpiMetricSpec, 4u> Kpis = {{
            { Cov, "%", "COVERAGE" },
            { Opt, "",  "OPTICAL" },
            { Flr, "m", "BASE" },
            { Drf, "x", "DRIFT" },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Kpis);

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Cloud, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "clouds", "weather" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 210.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud coverage");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Pct[16]{}; std::snprintf(Pct, sizeof(Pct), "%.0f", Clouds.CloudFraction * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Pct, "%", "Horizontal sky cloud fraction");
        float PctVal = Clouds.CloudFraction * 100.0f;
        bool Changed = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 3101u, "Fraction", "%", "%.0f", 0.0f, 100.0f, PctVal, ActiveDragId, Input, Changed);
        if (Changed) { Clouds.CloudFraction = PctVal * 0.01f; OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        UpdateCloudDeckSurface(Clouds);
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 390.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud altitude");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Km[16]{}; std::snprintf(Km, sizeof(Km), "%.2f", Clouds.CloudFloorMeters * 0.001f);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Km, "km", "World Z altitude  ·  vertical section");
            const Rectangle2D DeckBox { LeftX + PadX, Y + 4.0f, BodyW, 176.0f };
            CloudDeckPreview.Present(Surface, DeckBox, 8.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DeckBox.X + 8.0f, DeckBox.Y + 6.0f }, 8.5f, InspectorMetrics::MutedInk, "CLOUD DECK  ·  VERTICAL SECTION");
            Y = DeckBox.Bottom() + 14.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3201u, "Floor altitude", "m", "%.0f", 100.0f, 12000.0f, Clouds.CloudFloorMeters, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 276.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Layer thickness");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Km[16]{}; std::snprintf(Km, sizeof(Km), "%.2f", Clouds.LayerThicknessMeters * 0.001f);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Km, "km", "Vertical development  ·  density profile");
            std::array<float, 24u> Profile{};
            for (std::size_t I = 0u; I < Profile.size(); ++I)
            {
                const float H = static_cast<float>(I) / 23.0f;
                Profile[I] = std::clamp(std::sin(H * PiF) * (1.0f - 0.35f * H) + Clouds.AnvilTopSpread * std::pow(H, 4.0f) * 0.55f, 0.0f, 1.0f);
            }
            InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Profile, ColorRgba{ 222u, 232u, 246u, 255u }, ColorRgba{ 196u, 212u, 232u, 42u }, "Floor", "Top");
            Y += 86.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3202u, "Thickness", "m", "%.0f", 100.0f, 6500.0f, Clouds.LayerThicknessMeters, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 3u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 444.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud body");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3301u, "Density", "x", "%.2f", 0.0f, 4.0f, Clouds.ExtinctionMultiplier, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3302u, "Feature Scale", "x", "%.2f", 0.2f, 3.0f, Clouds.FeatureScaleMultiplier, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3303u, "Anisotropy", "g", "%.2f", -0.9f, 0.9f, Clouds.ForwardScatterAnisotropy, ActiveDragId, Input, OutMutated);
        static constexpr std::array<const char*, 6u> Types = {{ "Cumulus", "Stratus", "Stratocumulus", "Cumulonimbus", "Cirrus", "Altostratus" }};
        static constexpr std::array<GeometricRaster::CloudCategoryType, 6u> TypeEnums = {{
            GeometricRaster::CloudCategoryType::Cumulus,
            GeometricRaster::CloudCategoryType::Stratus,
            GeometricRaster::CloudCategoryType::Stratocumulus,
            GeometricRaster::CloudCategoryType::Cumulonimbus,
            GeometricRaster::CloudCategoryType::Cirrus,
            GeometricRaster::CloudCategoryType::Altostratus,
        }};
        uint32_t ActiveType = 0u;
        for (uint32_t I = 0u; I < TypeEnums.size(); ++I) if (Clouds.Category == TypeEnums[I]) { ActiveType = I; break; }
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Preset", Types, ActiveType, 2u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 6) { GeometricRaster::VolumetricCloudSequence::ApplyCloudCategoryPreset(Clouds, TypeEnums[static_cast<std::size_t>(Chosen)]); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3304u, "Anvil", "", "%.2f", 0.0f, 1.0f, Clouds.AnvilTopSpread, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3305u, "Ceiling", "m", "%.0f", 4000.0f, 22000.0f, Clouds.DeckCeilingMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 4u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 306.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud wind");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Follow wind", Clouds.FollowWindEnabled, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3401u, "Wind speed", "m/s", "%.1f", 0.0f, 40.0f, Clouds.WindSpeedMetersPerSecond, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3402u, "Wind bearing", "deg", "%.0f", 0.0f, 360.0f, Clouds.WindBearingDegrees, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3403u, "Wind shear", "/km", "%.2f", 0.0f, 2.0f, Clouds.WindShearPerKilometer, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3404u, "Turbulence", "", "%.2f", 0.0f, 1.0f, Clouds.WindTurbulence, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 272.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Quality & cloud shadows");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        float Steps = static_cast<float>(Clouds.CloudMarchSteps);
        bool StepsChanged = false;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3501u, "March steps", "", "%.0f", 16.0f, 128.0f, Steps, ActiveDragId, Input, StepsChanged);
        if (StepsChanged) { Clouds.CloudMarchSteps = static_cast<uint32_t>(std::lround(Steps)); OutMutated = true; }
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cloud shadows", Clouds.CloudShadow.ShadowEnabled, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3502u, "Shadow density", "x", "%.2f", 0.0f, 3.0f, Clouds.CloudShadow.ShadowDensityMultiplier, ActiveDragId, Input, OutMutated, !Clouds.CloudShadow.ShadowEnabled);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3503u, "Shadow fraction", "", "%.2f", 0.0f, 1.0f, Clouds.CloudShadow.ShadowFraction, ActiveDragId, Input, OutMutated, !Clouds.CloudShadow.ShadowEnabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
