/*============================================================================================================================================
                                                       ATMOSPHERICFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Atmospheric Fog Inspector (FogCards.jsx AerialRidges + LiveGraph.jsx FogSpectrum)

#include "Engine/Editor/Inspectors/AtmosphericFogInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

struct RidgeLayerSpec
{
    float DistanceMeters;
    float TopOffset;
    float Lift;
    float Seed;
};

constexpr std::array<RidgeLayerSpec, 5u> RidgeSpecs = {{
    { 2000.0f,  70.0f, 26.0f, 1.0f },
    { 1400.0f,  84.0f, 24.0f, 2.0f },
    {  900.0f,  98.0f, 22.0f, 3.0f },
    {  500.0f, 112.0f, 20.0f, 4.0f },
    {  180.0f, 128.0f, 18.0f, 5.0f },
}};

[[nodiscard]] float SampleRidgeY(const RidgeLayerSpec& Spec, float T) noexcept
{
    const float H = std::sin(T * 7.0f + Spec.Seed * 1.7f) * 0.5f
                  + std::sin(T * 17.0f + Spec.Seed * 3.1f) * 0.25f
                  + std::sin(T * 3.0f + Spec.Seed) * 0.6f;
    return Spec.TopOffset - Spec.Lift * (0.5f + 0.4f * H);
}

void FormatDistanceShort(float Meters, char* Buffer, std::size_t Capacity) noexcept
{
    if (!std::isfinite(Meters) || Meters > 1.0e8f)
    {
        std::snprintf(Buffer, Capacity, "clear");
    }
    else if (Meters >= 1000.0f)
    {
        if (Meters >= 10000.0f) std::snprintf(Buffer, Capacity, "%.0f km", Meters / 1000.0f);
        else                    std::snprintf(Buffer, Capacity, "%.1f km", Meters / 1000.0f);
    }
    else
    {
        std::snprintf(Buffer, Capacity, "%.0f m", std::round(Meters));
    }
}

[[nodiscard]] uint8_t ClampByte8(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

} // namespace

void AtmosphericFogInspector::UpdateRidgesSurface(const AtmosphericFogInstanceData& Instance)
{
    const auto& Fog = Instance.AerialFog;
    uint64_t Key = 0xAE41A109ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.DensityMultiplier);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.MieBlend);

    // Exact FogCards.jsx AerialRidges scene box: 300 x 150 -> render 2x crisp 600 x 300
    const uint32_t W = 600u, H = 300u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::FogLayer, 4u, { Fog.DensityMultiplier * 0.4f, 1.0f, Fog.MieBlend, 0.0f } };
    RidgePreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Sigma = std::max(0.0f, Fog.DensityMultiplier) * 0.001f;
        const float Blend = std::clamp(Fog.MieBlend, 0.0f, 1.0f);
        // Exact FogCards.jsx: Haze = mix(#8fb0d8 [143,176,216], #d2d4d8 [210,212,216], Blend)
        const float HazeR = 143.0f + (210.0f - 143.0f) * Blend;
        const float HazeG = 176.0f + (212.0f - 176.0f) * Blend;
        const float HazeB = 216.0f + (216.0f - 216.0f) * Blend;

        auto MixC = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
        const float SkyTopR = MixC(40.0f, HazeR, 0.25f), SkyTopG = MixC(70.0f, HazeG, 0.25f), SkyTopB = MixC(120.0f, HazeB, 0.25f);
        const float SkyBotR = MixC(40.0f, HazeR, 0.85f), SkyBotG = MixC(70.0f, HazeG, 0.85f), SkyBotB = MixC(120.0f, HazeB, 0.85f);

        // Precompute 5 ridge heights per X column and per-ridge colors
        std::array<std::vector<float>, 5u> RidgeYs{};
        std::array<float, 5u> RidgeR{}, RidgeG{}, RidgeB{};
        for (std::size_t L = 0u; L < 5u; ++L)
        {
            const float TVis = std::exp(-Sigma * RidgeSpecs[L].DistanceMeters);
            const float FogT = 1.0f - TVis;
            RidgeR[L] = MixC(24.0f, HazeR, FogT);
            RidgeG[L] = MixC(30.0f, HazeG, FogT);
            RidgeB[L] = MixC(26.0f, HazeB, FogT);
            RidgeYs[L].resize(W);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float T = (static_cast<float>(X) + 0.5f) / static_cast<float>(W);
                RidgeYs[L][X] = SampleRidgeY(RidgeSpecs[L], T);
            }
        }

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V    = (static_cast<float>(Y) + 0.5f) / static_cast<float>(H);
            const float SvgY = V * 150.0f;
            const float SkyR = MixC(SkyTopR, SkyBotR, V);
            const float SkyG = MixC(SkyTopG, SkyBotG, V);
            const float SkyB = MixC(SkyTopB, SkyBotB, V);

            for (uint32_t X = 0u; X < W; ++X)
            {
                float R = SkyR, G = SkyG, B = SkyB;
                for (std::size_t L = 0u; L < 5u; ++L)
                {
                    const float Cov = std::clamp((SvgY - RidgeYs[L][X]) * 1.8f + 0.5f, 0.0f, 1.0f);
                    if (Cov > 0.0f)
                    {
                        R = R * (1.0f - Cov) + RidgeR[L] * Cov;
                        G = G * (1.0f - Cov) + RidgeG[L] * Cov;
                        B = B * (1.0f - Cov) + RidgeB[L] * Cov;
                    }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByte8(R);
                Pixels[Off + 1u] = ClampByte8(G);
                Pixels[Off + 2u] = ClampByte8(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float AtmosphericFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AtmosphericFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Spectrum" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Fog = Instance.AerialFog;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const float Sigma = std::max(0.0f, Fog.DensityMultiplier) * 0.001f;
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        // ============================================================================================================
        // TAB 1: OVERVIEW — Aerial perspective (535px card), Quick controls (1 switch: Enabled), Tags, Notes
        // ============================================================================================================
        UpdateRidgesSurface(Instance);

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 535.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Aerial perspective");

        // Metric ("4.0km" -> white "4" + muted ".0km", exact HTML split metric)
        const float VisM = (Sigma > 1.0e-9f) ? (3.912f / Sigma + std::max(0.0f, Fog.StartDistanceMeters)) : 1.0e9f;
        char VisStr[32]{};
        FormatDistanceShort(VisM, VisStr, sizeof(VisStr));
        std::string_view VisV(VisStr);
        const std::size_t SpPos = VisV.find(' ');
        const std::string NumOnly = (SpPos != std::string_view::npos) ? std::string(VisV.substr(0u, SpPos)) : std::string(VisV);
        const std::string UnitOnly = (SpPos != std::string_view::npos) ? std::string(VisV.substr(SpPos + 1u)) : std::string{};
        const std::size_t DotP = NumOnly.find('.');
        if (DotP != std::string::npos)
        {
            const std::string IntP = NumOnly.substr(0u, DotP);
            const std::string FracAndUnit = NumOnly.substr(DotP) + UnitOnly;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, S.Y + 65.0f }, 35.0f, ColorRgba{ 233u, 233u, 233u, 255u }, IntP);
            const float IW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 35.0f, IntP);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX + IW, S.Y + 65.0f }, 35.0f, ColorRgba{ 105u, 107u, 105u, 255u }, FracAndUnit);
        }
        else
        {
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, S.Y + 65.0f }, 35.0f, ColorRgba{ 233u, 233u, 233u, 255u }, NumOnly + UnitOnly);
        }

        // Caption at exact HTML y = S.Y + 117
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, S.Y + 117.0f }, 12.2f, InspectorMetrics::MutedInk, "Visibility, counting the clear air before Start");

        // AerialRidges SVG at exact HTML S.Y + 147 (y=335), w = 222, h = 137.6 (viewBox 0 0 300 186)
        const Rectangle2D PlotBox { BodyX, S.Y + 147.0f, BodyW, 137.6f };
        const float SX = PlotBox.Width  / 300.0f;
        const float SY = PlotBox.Height / 186.0f;
        const Rectangle2D SceneRect { PlotBox.X, PlotBox.Y, 300.0f * SX, 150.0f * SY };
        RidgePreview.Present(Surface, SceneRect, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        // 5 right-aligned ridge transmission labels
        for (const auto& Spec : RidgeSpecs)
        {
            char DistBuf[24]{}, LblBuf[48]{};
            FormatDistanceShort(std::max(0.0f, Fog.StartDistanceMeters) + Spec.DistanceMeters, DistBuf, sizeof(DistBuf));
            const int Pct = static_cast<int>(std::lround(std::exp(-Sigma * Spec.DistanceMeters) * 100.0f));
            std::snprintf(LblBuf, sizeof(LblBuf), "%s \xC2\xB7 %d%%", DistBuf, Pct);
            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.2f, LblBuf);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular,
                { SceneRect.Right() - 6.0f * SX - LW, PlotBox.Y + (Spec.TopOffset - 9.0f) * SY },
                7.2f, ColorRgba{ 226u, 232u, 240u, 196u }, LblBuf);
        }

        // Interactive probe track at SVG y = 158 + vertical dashed line through scene
        const float ProbeClamped = std::clamp(ProbeDistanceMeters, 0.0f, 2000.0f);
        const float ProbePxX     = PlotBox.X + (ProbeClamped / 2000.0f) * PlotBox.Width;
        const float TrackPxY     = PlotBox.Y + 158.0f * SY;
        const Rectangle2D ProbeHit { PlotBox.X, SceneRect.Bottom(), PlotBox.Width, 20.0f };
        if (ProbeHit.Contains(Cursor) && Input.PrimaryPointer.Pressed) ActiveDragId = 6209u;
        if (ActiveDragId == 6209u && Input.PrimaryPointer.Down)
        {
            const float NextProbe = std::round(std::clamp((Cursor.X - PlotBox.X) / PlotBox.Width, 0.0f, 1.0f) * 2000.0f);
            if (std::fabs(NextProbe - ProbeDistanceMeters) > 0.5f) { ProbeDistanceMeters = NextProbe; OutMutated = true; }
        }
        else if (ActiveDragId == 6209u && !Input.PrimaryPointer.Down)
        {
            ActiveDragId = 0u;
        }

        InspectorPrimitives::ConstructDashedSegment(Surface, { ProbePxX, SceneRect.Y }, { ProbePxX, SceneRect.Bottom() }, ColorRgba{ 255u, 255u, 255u, 140u }, 2.5f, 2.5f, 1.0f);
        Surface.StrokeLine({ PlotBox.X, TrackPxY }, { PlotBox.Right(), TrackPxY }, ColorRgba{ 58u, 58u, 58u, 255u }, 2.4f);
        Surface.StrokeLine({ PlotBox.X, TrackPxY }, { ProbePxX, TrackPxY }, ColorRgba{ 207u, 207u, 207u, 255u }, 2.4f);
        Surface.FillCircle({ ProbePxX, TrackPxY }, 4.0f, ColorRgba{ 242u, 242u, 242u, 255u });

        // Bottom captions at SVG y = 182
        char StartDist[24]{}, ProbeDist[24]{}, LeftCap[64]{}, RightCap[32]{};
        FormatDistanceShort(std::max(0.0f, Fog.StartDistanceMeters), StartDist, sizeof(StartDist));
        FormatDistanceShort(std::max(0.0f, Fog.StartDistanceMeters) + ProbeClamped, ProbeDist, sizeof(ProbeDist));
        std::snprintf(LeftCap,  sizeof(LeftCap),  "Start %s: clear air before the haze begins", StartDist);
        std::snprintf(RightCap, sizeof(RightCap), "probe %s", ProbeDist);
        const float CapY = PlotBox.Y + 174.0f * SY;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PlotBox.X, CapY }, 7.2f, InspectorMetrics::MutedInk, LeftCap);
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.2f, RightCap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PlotBox.Right() - RW, CapY }, 7.2f, InspectorMetrics::MutedInk, RightCap);

        // Exact HTML Density & Start fields (sliders at S.Y + 328.6 -> y=516.6 and S.Y + 400.6 -> y=588.6)
        float FY = S.Y + 308.6f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6201u, "Density", "\xC3\x97", "%.2g", 0.0f, 4.0f, Fog.DensityMultiplier, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6202u, "Start", "m", "%.0f", 0.0f, 2000.0f, Fog.StartDistanceMeters, ActiveDragId, Input, OutMutated);

        // Bottom wrapped paragraph at S.Y + 448.6 (y=636.6)
        char ProbeOnly[24]{}, NoteBuf[200]{};
        FormatDistanceShort(ProbeClamped, ProbeOnly, sizeof(ProbeOnly));
        const float SurvPct = std::exp(-Sigma * ProbeClamped) * 100.0f;
        std::snprintf(NoteBuf, sizeof(NoteBuf), "%.1f %% of the light survives %s of haze. Illustrative sigma = 0.001 \xC3\x97 density / m, not a native atmosphere march.", SurvPct, ProbeOnly);
        (void)InspectorPrimitives::ConstructWrappedParagraph(Surface, BodyX, S.Y + 448.6f, BodyW, 12.0f, 16.5f, InspectorMetrics::MutedInk, NoteBuf);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls: EXACTLY 1 tile ("Enabled")
        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Fog, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            Fog.Enabled             = Entry.VisibilityEnabled;
            OutMutated              = true;
        }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "fog" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        // ============================================================================================================
        // TAB 2: SPECTRUM — Spectral transmission (600px card, FogSpectrum <Plot> + Mie Blend slider)
        // ============================================================================================================
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 600.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Spectral transmission");

        char AeroBuf[16]{};
        std::snprintf(AeroBuf, sizeof(AeroBuf), "%d", static_cast<int>(std::lround(Fog.MieBlend * 100.0f)));
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, S.Y + 65.0f, AeroBuf, "% aerosol", nullptr, 35.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { BodyX, S.Y + 117.0f }, 12.2f, InspectorMetrics::MutedInk,
            (Fog.MieBlend < 0.5f) ? "Blue, clean-air haze" : "Grey-white, dusty haze");

        const float PathM = std::max(0.0f, ProbeDistanceMeters);
        const float Mix   = std::clamp(Fog.MieBlend, 0.0f, 1.0f);
        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 1u> Series = {{
            {
                "Transmission",
                ColorRgba{ 158u, 184u, 208u, 255u },
                [Sigma, PathM, Mix](float LambdaNm) noexcept -> float
                {
                    const float Ratio = 550.0f / std::max(380.0f, LambdaNm);
                    const float Ex    = (1.0f - Mix) * std::pow(Ratio, 4.0f) + Mix * std::pow(Ratio, 1.3f);
                    return 100.0f * std::exp(-Sigma * PathM * Ex);
                },
            },
        }};

        char NoteBuf[256]{};
        std::snprintf(
            NoteBuf, sizeof(NoteBuf),
            "Drag to sample wavelength. Uses Density, Mie Blend and %.0f m of travel through the medium after Start; the disabled state remains an authored preview. At 550 nm this matches Visibility. Not native spectral rendering.",
            PathM);

        (void)InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, S.Y + 152.0f, BodyW, 6210u,
            380.0f, 780.0f, 0.0f, 100.0f, Series, SampleWavelengthNm,
            "nm", "%", NoteBuf, ActiveDragId, Input, OutMutated);

        // Mie Blend slider at exact HTML S.Y + 509 (slider at y=717)
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, S.Y + 509.0f, BodyW, 6203u, "Mie Blend", "", "%.1f", 0.0f, 1.0f, Fog.MieBlend, ActiveDragId, Input, OutMutated);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
