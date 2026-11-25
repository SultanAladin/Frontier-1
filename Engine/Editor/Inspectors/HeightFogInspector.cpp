/*============================================================================================================================================
                                                         HEIGHTFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Height Fog Inspector (Overview valley cut-away, Density live curve, Light towards/away study)

#include "Engine/Editor/Inspectors/HeightFogInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr float FogCeilingMeters = 3000.0f;

[[nodiscard]] float ComputeSigmaAtAltitude(float Density, float FalloffMeters, float AltitudeMeters) noexcept
{
    const float SafeFalloff = std::max(10.0f, FalloffMeters);
    return std::max(0.0f, Density) * std::exp(-std::max(0.0f, AltitudeMeters) / SafeFalloff);
}

[[nodiscard]] float ComputeVisibilityMeters(float Sigma) noexcept
{
    return (Sigma > 1.0e-9f) ? (3.912f / Sigma) : 1.0e9f;
}

void FormatDistanceText(float Meters, char* Buffer, std::size_t Capacity) noexcept
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

[[nodiscard]] float SvgAltitudeToY(float AltitudeMeters) noexcept
{
    // Exact FogCards.jsx formula: Bottom=168, Top=14, sqrt altitude scale
    const float T = std::sqrt(std::clamp(AltitudeMeters, 0.0f, FogCeilingMeters) / FogCeilingMeters);
    return 168.0f - T * (168.0f - 14.0f);
}

[[nodiscard]] float SvgYToAltitude(float Py) noexcept
{
    const float Frac = (168.0f - std::clamp(Py, 14.0f, 168.0f)) / (168.0f - 14.0f);
    return FogCeilingMeters * Frac * Frac;
}

[[nodiscard]] float EvalCubicBezier1D(float P0, float P1, float P2, float P3, float T) noexcept
{
    const float U = 1.0f - T;
    return U * U * U * P0 + 3.0f * U * U * T * P1 + 3.0f * U * T * T * P2 + T * T * T * P3;
}

// Evaluates exact FogCards.jsx Hills curve height (in SVG Y coords) for SVG X in [46..300]
[[nodiscard]] float SampleValleyHillY(float SvgX) noexcept
{
    constexpr float L = 46.0f;
    // Segment 1: (L, Y(60)) C (L+40, Y(240)), (L+70, Y(330)), (L+96, Y(300))
    // Segment 2: S (L+140, Y(40)), (L+170, Y(20))  -> reflected C1 = (L+122, 2*Y(300)-Y(330))
    // Segment 3: S (L+220, Y(520)), (300, Y(900))  -> reflected C1 = (L+200, 2*Y(20)-Y(40))
    const float Y60  = SvgAltitudeToY(60.0f);
    const float Y240 = SvgAltitudeToY(240.0f);
    const float Y330 = SvgAltitudeToY(330.0f);
    const float Y300 = SvgAltitudeToY(300.0f);
    const float Y40  = SvgAltitudeToY(40.0f);
    const float Y20  = SvgAltitudeToY(20.0f);
    const float Y520 = SvgAltitudeToY(520.0f);
    const float Y900 = SvgAltitudeToY(900.0f);

    struct CubicSeg { float X0, Y0, X1, Y1, X2, Y2, X3, Y3; };
    const std::array<CubicSeg, 3u> Segs = {{
        { L,        Y60,  L + 40.0f,  Y240,              L + 70.0f,  Y330, L + 96.0f,  Y300 },
        { L + 96.0f,Y300, L + 122.0f, 2.0f * Y300 - Y330, L + 140.0f, Y40,  L + 170.0f, Y20  },
        { L + 170.0f,Y20, L + 200.0f, 2.0f * Y20  - Y40,  L + 220.0f, Y520, 300.0f,     Y900 },
    }};

    const CubicSeg* Chosen = &Segs[2];
    if (SvgX <= Segs[0].X3)      Chosen = &Segs[0];
    else if (SvgX <= Segs[1].X3) Chosen = &Segs[1];

    // Binary search T in [0..1] for monotonic X
    float Lo = 0.0f, Hi = 1.0f;
    for (int It = 0; It < 12; ++It)
    {
        const float Mid = 0.5f * (Lo + Hi);
        if (EvalCubicBezier1D(Chosen->X0, Chosen->X1, Chosen->X2, Chosen->X3, Mid) < SvgX) Lo = Mid;
        else Hi = Mid;
    }
    return EvalCubicBezier1D(Chosen->Y0, Chosen->Y1, Chosen->Y2, Chosen->Y3, 0.5f * (Lo + Hi));
}

// Evaluates exact FogLightStudy hill curve Y in [0..96] for local X in [0..146]:
// M0 74 Q 30 66 60 72 T 146 70
[[nodiscard]] float SampleLightStudyHillY(float LocalX) noexcept
{
    if (LocalX <= 60.0f)
    {
        const float T = std::clamp(LocalX / 60.0f, 0.0f, 1.0f);
        const float U = 1.0f - T;
        return U * U * 74.0f + 2.0f * U * T * 66.0f + T * T * 72.0f;
    }
    // Reflected control point: (90, 2*72 - 66) = (90, 78)
    const float T = std::clamp((LocalX - 60.0f) / 86.0f, 0.0f, 1.0f);
    const float U = 1.0f - T;
    return U * U * 72.0f + 2.0f * U * T * 78.0f + T * T * 70.0f;
}

[[nodiscard]] uint8_t ClampChannel8(float Val) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(Val), 0l, 255l));
}

void ConstructDashedLine(PixelSpace& Surface, Point2D A, Point2D B, ColorRgba Ink, float DashLen, float GapLen, float Width = 1.0f) noexcept
{
    const float DX = B.X - A.X;
    const float DY = B.Y - A.Y;
    const float Len = std::sqrt(DX * DX + DY * DY);
    if (Len < 0.5f) return;
    const float UX = DX / Len, UY = DY / Len;
    float Pos = 0.0f;
    while (Pos < Len)
    {
        const float End = std::min(Len, Pos + DashLen);
        Surface.StrokeLine({ A.X + UX * Pos, A.Y + UY * Pos }, { A.X + UX * End, A.Y + UY * End }, Ink, Width);
        Pos += DashLen + GapLen;
    }
}

void ConstructSuperscriptMinusOne(PixelSpace& Surface, Point2D Origin, float FontSize, ColorRgba Ink, const char* Prefix, const char* Suffix = "") noexcept
{
    float X = Origin.X;
    if (Prefix != nullptr && Prefix[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Origin.Y }, FontSize, Ink, Prefix);
        X += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, FontSize, Prefix);
    }
    const float SupSize = FontSize * 0.72f;
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Origin.Y - FontSize * 0.14f }, SupSize, Ink, "-1");
    X += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, SupSize, "-1");
    if (Suffix != nullptr && Suffix[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Origin.Y }, FontSize, Ink, Suffix);
    }
}

} // namespace

void HeightFogInspector::UpdateLayerSurface(const HeightFogInstanceData& Instance, bool Enabled)
{
    const auto& Fog = Instance.HeightFog;
    const ColorRgba Tint = InspectorPrimitives::EncodeLinearRgbToSrgb8(Fog.HeightColour);
    uint64_t Key = 0x4F061A73ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.Density);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.FalloffHeightMeters);
    Key = InspectorPrimitives::MixRevisionU64(Key, Enabled ? 1ull : 0ull);
    Key = InspectorPrimitives::MixRevisionU64(Key, (static_cast<uint64_t>(Tint.Red) << 16u) | (static_cast<uint64_t>(Tint.Green) << 8u) | Tint.Blue);

    // Exact FogCards.jsx clip rect aspect: 254 x 154 -> render 2x crisp 508 x 308
    const uint32_t W = 508u, H = 308u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::FogLayer, Enabled ? 1u : 0u, { Fog.Density, Fog.FalloffHeightMeters * 0.0025f, 0.0f, 0.0f } };
    GpuSpec.ColourA[0] = static_cast<float>(Tint.Red) / 255.0f;
    GpuSpec.ColourA[1] = static_cast<float>(Tint.Green) / 255.0f;
    GpuSpec.ColourA[2] = static_cast<float>(Tint.Blue) / 255.0f;
    LayerPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        // Precompute valley silhouette height per column
        std::vector<float> HillY(W);
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float SvgX = 46.0f + (static_cast<float>(X) + 0.5f) / static_cast<float>(W) * 254.0f;
            HillY[X] = SampleValleyHillY(SvgX);
        }

        const float TowerY = SvgAltitudeToY(120.0f);
        const float EnabledGain = Enabled ? 0.85f : 0.50f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V    = (static_cast<float>(Y) + 0.5f) / static_cast<float>(H);
            const float SvgY = 14.0f + V * 154.0f;
            const float Alt  = SvgYToAltitude(SvgY);
            const float Sig  = ComputeSigmaAtAltitude(Fog.Density, Fog.FalloffHeightMeters, Alt);
            const float FogA = (1.0f - std::exp(-Sig * 1000.0f)) * EnabledGain;

            // Exact sky vertical gradient: #1b2534 (27,37,52) -> #3b4656 (59,70,86)
            const float SkyR = 27.0f + (59.0f - 27.0f) * V;
            const float SkyG = 37.0f + (70.0f - 37.0f) * V;
            const float SkyB = 52.0f + (86.0f - 52.0f) * V;

            for (uint32_t X = 0u; X < W; ++X)
            {
                const float SvgX = 46.0f + (static_cast<float>(X) + 0.5f) / static_cast<float>(W) * 254.0f;
                float R = SkyR, G = SkyG, B = SkyB;

                // Valley hill silhouette (#11151a -> 17, 21, 26)
                if (SvgY >= HillY[X])
                {
                    R = 17.0f; G = 21.0f; B = 26.0f;
                }

                // 120 m tower (x in [164..167]) and 5 town silhouettes (#0b0e12 -> 11, 14, 18)
                bool InStruct = (SvgX >= 164.0f && SvgX <= 167.0f && SvgY >= TowerY);
                if (!InStruct && SvgX >= 172.0f && SvgX <= 205.0f)
                {
                    for (int Bld = 0; Bld < 5; ++Bld)
                    {
                        const float BX = 172.0f + static_cast<float>(Bld) * 7.0f;
                        const float BY = SvgAltitudeToY(18.0f + static_cast<float>((Bld * 13) % 22));
                        if (SvgX >= BX && SvgX <= BX + 5.0f && SvgY >= BY) { InStruct = true; break; }
                    }
                }
                if (InStruct)
                {
                    R = 11.0f; G = 14.0f; B = 18.0f;
                }

                // Exact alpha composite of fog layer colour over valley
                R = R * (1.0f - FogA) + static_cast<float>(Tint.Red)   * FogA;
                G = G * (1.0f - FogA) + static_cast<float>(Tint.Green) * FogA;
                B = B * (1.0f - FogA) + static_cast<float>(Tint.Blue)  * FogA;

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampChannel8(R);
                Pixels[Off + 1u] = ClampChannel8(G);
                Pixels[Off + 2u] = ClampChannel8(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void HeightFogInspector::UpdateLightStudySurfaces(const HeightFogInstanceData& Instance)
{
    const auto& Fog = Instance.HeightFog;
    const ColorRgba Tint = InspectorPrimitives::EncodeLinearRgbToSrgb8(Fog.HeightColour);
    uint64_t Key = 0x4F061167ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.SunScatterIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.Density);
    Key = InspectorPrimitives::MixRevisionU64(Key, (static_cast<uint64_t>(Tint.Red) << 16u) | (static_cast<uint64_t>(Tint.Green) << 8u) | Tint.Blue);

    const float Scatter = std::max(0.0f, Fog.SunScatterIntensity);
    const float Haze    = std::clamp(Fog.Density / 0.05f, 0.15f, 1.0f);
    const float Glow    = std::clamp(Scatter / 2.0f, 0.0f, 1.0f);

    auto MixCol = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float SkyR = MixCol(38.0f, static_cast<float>(Tint.Red),   0.55f * Haze);
    const float SkyG = MixCol(48.0f, static_cast<float>(Tint.Green), 0.55f * Haze);
    const float SkyB = MixCol(62.0f, static_cast<float>(Tint.Blue),  0.55f * Haze);
    const float LowR = MixCol(70.0f, static_cast<float>(Tint.Red),   0.85f * Haze);
    const float LowG = MixCol(80.0f, static_cast<float>(Tint.Green), 0.85f * Haze);
    const float LowB = MixCol(92.0f, static_cast<float>(Tint.Blue),  0.85f * Haze);
    const float HilR = MixCol(18.0f, static_cast<float>(Tint.Red),   0.30f * Haze);
    const float HilG = MixCol(20.0f, static_cast<float>(Tint.Green), 0.30f * Haze);
    const float HilB = MixCol(24.0f, static_cast<float>(Tint.Blue),  0.30f * Haze);
    const float MidR = MixCol(255.0f, static_cast<float>(Tint.Red),   0.40f);
    const float MidG = MixCol(236.0f, static_cast<float>(Tint.Green), 0.40f);
    const float MidB = MixCol(200.0f, static_cast<float>(Tint.Blue),  0.40f);

    // Exact FogLightStudy panel dimensions: 146 x 96 -> render 2x crisp 292 x 192
    const uint32_t W = 292u, H = 192u;
    auto RasterPanel = [&](bool TowardsSun, std::vector<uint8_t>& Pixels)
    {
        const float GlowRad   = 18.0f + 46.0f * Glow;
        const float DiscAlpha = std::clamp(1.0f - 0.6f * Haze, 0.0f, 1.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V      = (static_cast<float>(Y) + 0.5f) / static_cast<float>(H);
            const float LocalY = V * 96.0f;
            const float BgR    = MixCol(SkyR, LowR, V);
            const float BgG    = MixCol(SkyG, LowG, V);
            const float BgB    = MixCol(SkyB, LowB, V);

            for (uint32_t X = 0u; X < W; ++X)
            {
                const float LocalX = (static_cast<float>(X) + 0.5f) / static_cast<float>(W) * 146.0f;
                float R = BgR, G = BgG, B = BgB;

                if (TowardsSun)
                {
                    const float Dist = std::hypot(LocalX - 73.0f, LocalY - 58.0f);
                    const float U    = Dist / std::max(1.0f, GlowRad);
                    if (U < 1.0f)
                    {
                        // Exact 3-stop SVG radialGradient: 0 (#fff6dc, 0.15+0.85*Glow) -> 0.4 (Mid, 0.45*Glow) -> 1.0 (0)
                        float GR, GG, GB, GA;
                        if (U <= 0.4f)
                        {
                            const float T = U / 0.4f;
                            GR = MixCol(255.0f, MidR, T);
                            GG = MixCol(246.0f, MidG, T);
                            GB = MixCol(220.0f, MidB, T);
                            GA = MixCol(0.15f + 0.85f * Glow, 0.45f * Glow, T);
                        }
                        else
                        {
                            const float T = (U - 0.4f) / 0.6f;
                            GR = MixCol(MidR, 255.0f, T);
                            GG = MixCol(MidG, 255.0f, T);
                            GB = MixCol(MidB, 255.0f, T);
                            GA = MixCol(0.45f * Glow, 0.0f, T);
                        }
                        R = R * (1.0f - GA) + GR * GA;
                        G = G * (1.0f - GA) + GG * GA;
                        B = B * (1.0f - GA) + GB * GA;
                    }
                    // Anti-aliased solar disc r=5 at (73, 58)
                    const float Edge = std::clamp(5.2f - Dist, 0.0f, 1.0f) * DiscAlpha;
                    if (Edge > 0.0f)
                    {
                        R = R * (1.0f - Edge) + 255.0f * Edge;
                        G = G * (1.0f - Edge) + 251.0f * Edge;
                        B = B * (1.0f - Edge) + 239.0f * Edge;
                    }
                }

                // Exact dark rolling hill silhouette foreground
                const float HillEdge = SampleLightStudyHillY(LocalX);
                const float HillCov  = std::clamp((LocalY - HillEdge) * 1.6f + 0.5f, 0.0f, 1.0f);
                if (HillCov > 0.0f)
                {
                    R = R * (1.0f - HillCov) + HilR * HillCov;
                    G = G * (1.0f - HillCov) + HilG * HillCov;
                    B = B * (1.0f - HillCov) + HilB * HillCov;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampChannel8(R);
                Pixels[Off + 1u] = ClampChannel8(G);
                Pixels[Off + 2u] = ClampChannel8(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    };

    InspectorGpuBakeSpec SunSpec { InspectorGpuBakeMode::FogLayer, 2u, { Fog.Density, Fog.FalloffHeightMeters * 0.0025f, Fog.SunScatterIntensity, 0.0f } };
    TowardsSunPreview.EnsureUpdatedGpu(Key ^ 0x1111ull, W, H, SunSpec, [&](std::vector<uint8_t>& Px) { RasterPanel(true, Px); });
    SunSpec.Flags = 3u;
    AwayFromSunPreview.EnsureUpdatedGpu(Key ^ 0x2222ull, W, H, SunSpec, [&](std::vector<uint8_t>& Px) { RasterPanel(false, Px); });
}

float HeightFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, HeightFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Density", "Light" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Fog = Instance.HeightFog;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f; // 222px at 340px pane width
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    const float GroundSigma = ComputeSigmaAtAltitude(Fog.Density, Fog.FalloffHeightMeters, 0.0f);
    const float FalloffM    = std::max(10.0f, Fog.FalloffHeightMeters);
    const float GroundVisM  = ComputeVisibilityMeters(GroundSigma);

    if (ActivePage == 0u)
    {
        // ============================================================================================================
        // TAB 1: OVERVIEW — Fog layer (exact 474.6px height), Quick controls (1 switch: Enabled), Tags, Notes
        // ============================================================================================================
        UpdateLayerSurface(Instance, Entry.VisibilityEnabled);

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 475.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Fog layer");

        // Big metric ("196m" / "3.9 km") at exact HTML y = S.Y + 66 (254)
        char VisText[32]{};
        FormatDistanceText(GroundVisM, VisText, sizeof(VisText));
        std::string_view VisView(VisText);
        const std::size_t SpacePos = VisView.find(' ');
        if (SpacePos != std::string_view::npos)
        {
            const std::string NumStr(VisView.substr(0u, SpacePos));
            const std::string UnitStr(VisView.substr(SpacePos + 1u));
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, S.Y + 65.0f }, 35.0f, ColorRgba{ 233u, 233u, 233u, 255u }, NumStr);
            const float NumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 35.0f, NumStr);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX + NumW + 2.0f, S.Y + 65.0f }, 35.0f, ColorRgba{ 233u, 233u, 233u, 255u }, UnitStr);
        }
        else
        {
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, S.Y + 65.0f }, 35.0f, ColorRgba{ 233u, 233u, 233u, 255u }, VisText);
        }

        // Caption at exact HTML y = S.Y + 116.5 (304.5)
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, S.Y + 117.0f }, 12.2f, InspectorMetrics::MutedInk, "How far you can see at ground level");

        // HeightFogLayer SVG box at exact HTML y = S.Y + 147 (335), w = 222, h = 140.6 (viewBox 0 0 300 190)
        const Rectangle2D PlotBox { BodyX, S.Y + 147.0f, BodyW, 140.6f };
        const float SX = PlotBox.Width  / 300.0f;
        const float SY = PlotBox.Height / 190.0f;
        auto ToPx = [&](float Vx, float Vy) noexcept -> Point2D { return { PlotBox.X + Vx * SX, PlotBox.Y + Vy * SY }; };

        const Rectangle2D SceneRect {
            PlotBox.X + 46.0f * SX,
            PlotBox.Y + 14.0f * SY,
            254.0f * SX,
            154.0f * SY,
        };
        LayerPreview.Present(Surface, SceneRect, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        // Y-axis ticks (0 m, 100 m, 500 m, 1 km, 3 km)
        static constexpr std::array<float, 5u> Ticks = {{ 0.0f, 100.0f, 500.0f, 1000.0f, 3000.0f }};
        for (float Tick : Ticks)
        {
            const float TY = SvgAltitudeToY(Tick);
            Surface.StrokeLine(ToPx(42.0f, TY), ToPx(46.0f, TY), ColorRgba{ 106u, 106u, 106u, 255u }, 1.0f);
            char TLabel[16]{};
            if (Tick >= 1000.0f) std::snprintf(TLabel, sizeof(TLabel), "%.0f km", Tick / 1000.0f);
            else                 std::snprintf(TLabel, sizeof(TLabel), "%.0f m", Tick);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.5f, TLabel);
            const Point2D TP = ToPx(39.0f, TY);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TP.X - TW, TP.Y - 4.0f }, 7.5f, InspectorMetrics::MutedInk, TLabel);
        }

        // Interactive draggable dashed falloff height line + right-side handle dot
        const float FySvg = SvgAltitudeToY(FalloffM);
        const Point2D LineL = ToPx(46.0f, FySvg);
        const Point2D LineR = ToPx(300.0f, FySvg);
        const Rectangle2D HandleHit { LineL.X, LineL.Y - 9.0f, LineR.X - LineL.X, 18.0f };
        if (HandleHit.Contains(Cursor) && Input.PrimaryPointer.Pressed) ActiveDragId = 6109u;
        if (ActiveDragId == 6109u && Input.PrimaryPointer.Down)
        {
            const float LocalSvgY = (Cursor.Y - PlotBox.Y) / SY;
            const float NextFalloff = std::round(std::clamp(SvgYToAltitude(LocalSvgY), 10.0f, 3000.0f));
            if (std::fabs(NextFalloff - Fog.FalloffHeightMeters) > 0.5f)
            {
                Fog.FalloffHeightMeters = NextFalloff;
                OutMutated = true;
            }
        }
        else if (ActiveDragId == 6109u && !Input.PrimaryPointer.Down)
        {
            ActiveDragId = 0u;
        }

        ConstructDashedLine(Surface, LineL, LineR, ColorRgba{ 255u, 255u, 255u, 204u }, 3.0f, 2.2f, 1.0f);
        Surface.FillCircle(ToPx(292.0f, FySvg), 4.5f * SX, ColorRgba{ 242u, 242u, 242u, 255u });

        // Exact 3 in-scene labels + bottom drag prompt
        char FallDist[24]{}, HighAltDist[24]{}, HighVisDist[24]{};
        FormatDistanceText(FalloffM, FallDist, sizeof(FallDist));
        const float HighAlt = std::min(FogCeilingMeters, FalloffM * 3.0f);
        FormatDistanceText(HighAlt, HighAltDist, sizeof(HighAltDist));
        FormatDistanceText(ComputeVisibilityMeters(ComputeSigmaAtAltitude(Fog.Density, FalloffM, HighAlt)), HighVisDist, sizeof(HighVisDist));

        char FallLine[64]{}, GroundLine[64]{}, HighLine[64]{};
        std::snprintf(FallLine,   sizeof(FallLine),   "falloff %s \xC2\xB7 density \xC3\xB7 e", FallDist);
        std::snprintf(GroundLine, sizeof(GroundLine), "ground: see %s", VisText);
        std::snprintf(HighLine,   sizeof(HighLine),   "at %s: see %s", HighAltDist, HighVisDist);

        const float HighLabelSvgY = std::max(24.0f, SvgAltitudeToY(HighAlt) - 5.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, ToPx(52.0f, HighLabelSvgY - 8.0f), 7.2f, ColorRgba{ 210u, 216u, 224u, 180u }, HighLine);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, ToPx(52.0f, FySvg - 13.0f),         7.2f, ColorRgba{ 244u, 246u, 248u, 242u }, FallLine);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, ToPx(52.0f, 154.0f),                 7.2f, ColorRgba{ 244u, 246u, 248u, 242u }, GroundLine);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, ToPx(46.0f, 177.0f),                 7.2f, InspectorMetrics::MutedInk, "Drag the dashed line to set the falloff height");

        // Exact HTML field positions: Density slider at S.Y + 331.6 (y=519.6), Falloff Height slider at S.Y + 403.6 (y=591.6)
        float FY = S.Y + 311.6f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6101u, "Density", "1/m", "%.2f", 0.0f, 0.2f, Fog.Density, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6102u, "Falloff Height", "m", "%.0f", 10.0f, 3000.0f, Fog.FalloffHeightMeters, ActiveDragId, Input, OutMutated);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls: EXACTLY 1 switch ("Enabled", fog symbol) matching HTML
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
    else if (ActivePage == 1u)
    {
        // ============================================================================================================
        // TAB 2: DENSITY — Density by altitude (exact 599px height, HeightFogVisual.jsx + 3 readouts)
        // ============================================================================================================
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 599.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Density by altitude");

        // Status row at S.Y + 69 (257)
        const float StatusY = S.Y + 69.0f;
        const ColorRgba DotCol = Entry.VisibilityEnabled ? ColorRgba{ 81u, 212u, 135u, 255u } : ColorRgba{ 211u, 113u, 113u, 255u };
        Surface.FillCircle({ BodyX + 3.0f, StatusY + 4.5f }, 3.0f, DotCol);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { BodyX + 13.0f, StatusY }, 9.0f, ColorRgba{ 143u, 150u, 145u, 255u },
            Entry.VisibilityEnabled ? "LIVE DENSITY PROFILE" : "AUTHORED PREVIEW");

        char FallRight[32]{};
        std::snprintf(FallRight, sizeof(FallRight), "%.0f m falloff", FalloffM);
        const float FRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, FallRight);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { BodyX + BodyW - FRW, StatusY }, 9.5f, ColorRgba{ 201u, 204u, 199u, 255u }, FallRight);

        // Large metric at S.Y + 91 (279) with crisp raised m^-1 superscript
        char DenBuf[24]{};
        std::snprintf(DenBuf, sizeof(DenBuf), "%.4f", Fog.Density);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, S.Y + 92.0f }, 35.0f, ColorRgba{ 226u, 228u, 223u, 255u }, DenBuf);
        const float DW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 35.0f, DenBuf);
        ConstructSuperscriptMinusOne(Surface, { BodyX + DW + 8.0f, S.Y + 110.0f }, 11.0f, ColorRgba{ 119u, 125u, 120u, 255u }, "m", " datum density");

        // Interactive SVG plot at exact HTML S.Y + 141 (329), w = 222, h = 220 (viewBox 0 0 300 210)
        const Rectangle2D Plot { BodyX, S.Y + 141.0f, BodyW, 220.0f };
        Surface.FillRectangle(Plot, ColorRgba{ 25u, 25u, 25u, 255u }, 12.0f);
        Surface.StrokeRectangle(Plot, ColorRgba{ 255u, 255u, 255u, 11u }, 12.0f, 1.0f);

        const float SX = Plot.Width  / 300.0f;
        const float SY = Plot.Height / 210.0f;
        auto ToP = [&](float Vx, float Vy) noexcept -> Point2D { return { Plot.X + Vx * SX, Plot.Y + Vy * SY }; };
        auto AltToVx = [](float Alt) noexcept -> float { return 34.0f + (Alt / 3000.0f) * (286.0f - 34.0f); };
        auto DenToVy = [](float Den) noexcept -> float { return 174.0f - (std::clamp(Den, 0.0f, 0.2f) / 0.2f) * (174.0f - 20.0f); };

        // Pointer drag edits both Falloff Height (X) and Density (Y) identically to HeightFogVisual.jsx
        if (Plot.Contains(Cursor) && Input.PrimaryPointer.Pressed) ActiveDragId = 6120u;
        if (ActiveDragId == 6120u && Input.PrimaryPointer.Down)
        {
            const float Vx = std::clamp((Cursor.X - Plot.X) / SX, 34.0f, 286.0f);
            const float Vy = std::clamp((Cursor.Y - Plot.Y) / SY, 20.0f, 174.0f);
            const float NextFalloff = std::round(10.0f + ((Vx - 34.0f) / (286.0f - 34.0f)) * 2990.0f);
            const float NextDensity = ((174.0f - Vy) / (174.0f - 20.0f)) * 0.2f;
            if (std::fabs(NextFalloff - Fog.FalloffHeightMeters) > 0.5f || std::fabs(NextDensity - Fog.Density) > 1.0e-4f)
            {
                Fog.FalloffHeightMeters = NextFalloff;
                Fog.Density       = NextDensity;
                OutMutated = true;
            }
        }
        else if (ActiveDragId == 6120u && !Input.PrimaryPointer.Down)
        {
            ActiveDragId = 0u;
        }

        // Horizontal grid lines & Y labels (0, 0.05, 0.10, 0.15, 0.20)
        static constexpr std::array<float, 5u> YVals = {{ 0.0f, 0.05f, 0.10f, 0.15f, 0.20f }};
        for (float Yv : YVals)
        {
            const float Vy = DenToVy(Yv);
            if (Yv > 0.0f) ConstructDashedLine(Surface, ToP(34.0f, Vy), ToP(286.0f, Vy), ColorRgba{ 255u, 255u, 255u, 18u }, 1.5f, 3.7f, 1.0f);
            else           Surface.StrokeLine(ToP(34.0f, Vy), ToP(286.0f, Vy), ColorRgba{ 255u, 255u, 255u, 24u }, 1.0f);

            char YLbl[12]{};
            if (Yv > 0.0f) std::snprintf(YLbl, sizeof(YLbl), "%.2f", Yv);
            else           std::snprintf(YLbl, sizeof(YLbl), "0");
            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.5f, YLbl);
            const Point2D LP = ToP(28.0f, Vy);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LP.X - LW, LP.Y - 4.0f }, 7.5f, ColorRgba{ 150u, 157u, 153u, 255u }, YLbl);
        }

        // Vertical grid lines & X labels (0 m, 1 km, 2 km, 3 km)
        static constexpr std::array<float, 4u> XVals = {{ 0.0f, 1000.0f, 2000.0f, 3000.0f }};
        for (float Xv : XVals)
        {
            const float Vx = AltToVx(Xv);
            Surface.StrokeLine(ToP(Vx, 20.0f), ToP(Vx, 174.0f), ColorRgba{ 255u, 255u, 255u, 11u }, 1.0f);
            char XLbl[12]{};
            if (Xv > 0.0f) std::snprintf(XLbl, sizeof(XLbl), "%.0f km", Xv / 1000.0f);
            else           std::snprintf(XLbl, sizeof(XLbl), "0 m");
            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.5f, XLbl);
            const Point2D LP = ToP(Vx, 186.0f);
            float TX = LP.X - LW * 0.5f;
            if (Xv <= 0.0f)      TX = LP.X;
            else if (Xv >= 3000.0f) TX = LP.X - LW;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TX, LP.Y }, 7.5f, ColorRgba{ 150u, 157u, 153u, 255u }, XLbl);
        }

        // Curve fill & stroke
        const ColorRgba CurveCol = InspectorPrimitives::EncodeLinearRgbToSrgb8(Fog.HeightColour);
        std::array<Point2D, 61u> CurvePts{};
        const float BasePxY = ToP(34.0f, 174.0f).Y;
        for (std::size_t Idx = 0u; Idx < 61u; ++Idx)
        {
            const float Alt = static_cast<float>(Idx) * 50.0f;
            CurvePts[Idx] = ToP(AltToVx(Alt), DenToVy(ComputeSigmaAtAltitude(Fog.Density, FalloffM, Alt)));
        }
        const uint8_t FillA = Entry.VisibilityEnabled ? 72u : 46u;
        for (std::size_t Idx = 0u; Idx + 1u < 61u; ++Idx)
        {
            const std::array<Point2D, 4u> Quad = {{
                CurvePts[Idx],
                CurvePts[Idx + 1u],
                { CurvePts[Idx + 1u].X, BasePxY },
                { CurvePts[Idx].X,      BasePxY },
            }};
            Surface.FillPolygon(Quad, CurveCol.WithAlpha(FillA));
        }
        Surface.StrokePolyline(CurvePts, CurveCol, 1.5f, false);

        // Vertical dashed falloff marker + draggable dot + "37% · 400 m" label
        const float MarkVx = AltToVx(FalloffM);
        const float MarkVy = DenToVy(ComputeSigmaAtAltitude(Fog.Density, FalloffM, FalloffM));
        ConstructDashedLine(Surface, ToP(MarkVx, 20.0f), ToP(MarkVx, 174.0f), ColorRgba{ 255u, 255u, 255u, 107u }, 3.2f, 3.2f, 1.0f);
        const Point2D DotP = ToP(MarkVx, MarkVy);
        Surface.FillCircle(DotP, 4.4f, ColorRgba{ 17u, 19u, 21u, 255u });
        Surface.FillCircle(DotP, 3.2f, CurveCol);

        char MarkTxt[32]{};
        std::snprintf(MarkTxt, sizeof(MarkTxt), "37%% \xC2\xB7 %.0f m", FalloffM);
        const float MTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.5f, MarkTxt);
        const Point2D MTP = ToP(std::min(282.0f, MarkVx + 8.0f), std::max(14.0f, MarkVy - 14.0f));
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular,
            { (MarkVx > 230.0f) ? (DotP.X - MTW - 6.0f) : MTP.X, MTP.Y },
            7.5f, ColorRgba{ 150u, 157u, 153u, 255u }, MarkTxt);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, ToP(34.0f, 199.0f), 7.2f, ColorRgba{ 150u, 157u, 153u, 255u }, "DRAG POINT \xC2\xB7 DENSITY / FALLOFF");

        // 2-column sub-cards ("AT FALLOFF HEIGHT" | "AT 2× FALLOFF")
        const float SubY = Plot.Bottom() + 12.0f;
        const float SubGap = 12.0f;
        const float SubW = (BodyW - SubGap) * 0.5f;
        const float SubH = 51.0f;
        const Rectangle2D BoxL { BodyX, SubY, SubW, SubH };
        const Rectangle2D BoxR { BodyX + SubW + SubGap, SubY, SubW, SubH };
        Surface.FillRectangle(BoxL, ColorRgba{ 24u, 25u, 24u, 255u }, 10.0f);
        Surface.FillRectangle(BoxR, ColorRgba{ 24u, 25u, 24u, 255u }, 10.0f);

        char At1Buf[24]{}, At2Buf[24]{};
        std::snprintf(At1Buf, sizeof(At1Buf), "%.4f m", ComputeSigmaAtAltitude(Fog.Density, FalloffM, FalloffM));
        std::snprintf(At2Buf, sizeof(At2Buf), "%.4f m", ComputeSigmaAtAltitude(Fog.Density, FalloffM, FalloffM * 2.0f));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BoxL.X + 11.0f, BoxL.Y + 10.0f },  8.5f, ColorRgba{ 120u, 126u, 121u, 255u }, "AT FALLOFF HEIGHT");
        ConstructSuperscriptMinusOne(Surface, { BoxL.X + 11.0f, BoxL.Y + 26.5f }, 14.5f, ColorRgba{ 215u, 218u, 213u, 255u }, At1Buf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BoxR.X + 11.0f, BoxR.Y + 10.0f },  8.5f, ColorRgba{ 120u, 126u, 121u, 255u }, "AT 2\xC3\x97 FALLOFF");
        ConstructSuperscriptMinusOne(Surface, { BoxR.X + 11.0f, BoxR.Y + 26.5f }, 14.5f, ColorRgba{ 215u, 218u, 213u, 255u }, At2Buf);

        // 3-line explanation paragraph matching HTML
        float PY = BoxL.Bottom() + 14.0f;
        const ColorRgba ParaInk { 119u, 125u, 120u, 255u };
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PY },         10.2f, ParaInk, "Drag the profile point to author the same Density and");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PY + 14.5f }, 10.2f, ParaInk, "Falloff Height values used by Medium, Visibility and");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PY + 29.0f }, 10.2f, ParaInk, "Beam Chamber.");

        // 3 readout rows matching exact HTML vertical alignment
        float RY = PY + 54.0f;
        char Vis0[24]{}, VisF[24]{}, FallLbl[40]{}, UpBuf[32]{};
        FormatDistanceText(GroundVisM, Vis0, sizeof(Vis0));
        FormatDistanceText(ComputeVisibilityMeters(ComputeSigmaAtAltitude(Fog.Density, FalloffM, FalloffM)), VisF, sizeof(VisF));
        char FallTitleDist[24]{};
        FormatDistanceText(FalloffM, FallTitleDist, sizeof(FallTitleDist));
        std::snprintf(FallLbl, sizeof(FallLbl), "Visibility at %s", FallTitleDist);
        const float ZenithPct = std::exp(-Fog.Density * FalloffM) * 100.0f;
        std::snprintf(UpBuf, sizeof(UpBuf), "%.2g %% of the light", ZenithPct);

        auto ConstructReadoutRow = [&](const char* Lbl, const char* Val)
        {
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RY }, 12.2f, ColorRgba{ 153u, 153u, 153u, 255u }, Lbl);
            const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 12.2f, Val);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - VW, RY }, 12.2f, ColorRgba{ 193u, 193u, 193u, 255u }, Val);
            RY += 28.5f;
        };
        ConstructReadoutRow("Visibility at 0 m", Vis0);
        ConstructReadoutRow(FallLbl, VisF);
        ConstructReadoutRow("Looking straight up", UpBuf);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // ============================================================================================================
        // TAB 3: LIGHT — Light in the fog (exact 391px height, FogLightStudy 2 side-by-side panels + Sun Scatter + Colour)
        // ============================================================================================================
        UpdateLightStudySurfaces(Instance);

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 391.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Light in the fog");

        // Exact HTML Metric: white integer + grey fractional + muted "×" at S.Y + 65 (253)
        char ScatNum[16]{};
        std::snprintf(ScatNum, sizeof(ScatNum), "%.2f", Fog.SunScatterIntensity);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, S.Y + 65.0f, ScatNum, "\xC3\x97", nullptr, 35.0f);

        // Caption at exact HTML y = S.Y + 117 (305)
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, S.Y + 117.0f }, 12.2f, InspectorMetrics::MutedInk, "Forward glow around the sun");

        // FogLightStudy SVG at exact HTML y = S.Y + 147 (335), w = 222, h = 82.9 (viewBox 0 0 300 112)
        const Rectangle2D StudyBox { BodyX, S.Y + 147.0f, BodyW, 82.9f };
        const float SX = StudyBox.Width  / 300.0f;
        const float SY = StudyBox.Height / 112.0f;

        const Rectangle2D LeftPanel  { StudyBox.X + 0.0f   * SX, StudyBox.Y, 146.0f * SX, 96.0f * SY };
        const Rectangle2D RightPanel { StudyBox.X + 154.0f * SX, StudyBox.Y, 146.0f * SX, 96.0f * SY };
        const float PanelR = 12.0f * SX;

        TowardsSunPreview.Present(Surface, LeftPanel, PanelR);
        AwayFromSunPreview.Present(Surface, RightPanel, PanelR);

        // Top-left panel headers
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftPanel.X  + 8.0f * SX, LeftPanel.Y  + 6.0f * SY }, 7.0f, ColorRgba{ 244u, 246u, 248u, 240u }, "TOWARDS THE SUN");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RightPanel.X + 8.0f * SX, RightPanel.Y + 6.0f * SY }, 7.0f, ColorRgba{ 244u, 246u, 248u, 240u }, "AWAY FROM THE SUN");

        // Bottom captions ("forward glow 0.60 ×" left, "tinted by the fog colour" right)
        char GlowCap[32]{};
        std::snprintf(GlowCap, sizeof(GlowCap), "forward glow %.2f \xC3\x97", Fog.SunScatterIntensity);
        const float CapY = StudyBox.Y + 101.0f * SY;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { StudyBox.X, CapY }, 7.4f, InspectorMetrics::MutedInk, GlowCap);
        const char* RightCap = "tinted by the fog colour";
        const float RCW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.4f, RightCap);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { StudyBox.Right() - RCW, CapY }, 7.4f, InspectorMetrics::MutedInk, RightCap);

        // Exact HTML Sun Scatter field (slider at S.Y + 273.9 -> y=461.9) + Colour row directly below
        float FY = S.Y + 253.9f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6104u, "Sun Scatter", "\xC3\x97", "%.1f", 0.0f, 2.0f, Fog.SunScatterIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, BodyX, FY - 3.0f, BodyW, 6110u, "Colour", Fog.HeightColour, ActiveDragId, Input, OutMutated);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
