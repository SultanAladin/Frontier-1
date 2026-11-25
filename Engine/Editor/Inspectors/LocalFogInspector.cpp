/*============================================================================================================================================
                                                          LOCALFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Local Fog Inspector (LiveGraph.jsx FogGraph + FogPanel.jsx FogBeamChamber + FogShapePanel.jsx)

#include "Engine/Editor/Inspectors/LocalFogInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

struct Point3F { float X, Y, Z; };

[[nodiscard]] uint8_t ClampByteLF(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

[[nodiscard]] const char* ShapeOptionLabel(GeometricRaster::AtmosphericVolumeShape Shape) noexcept
{
    switch (Shape)
    {
        case GeometricRaster::AtmosphericVolumeShape::Box:      return "Box / rectangular volume";
        case GeometricRaster::AtmosphericVolumeShape::Sphere:   return "Sphere";
        case GeometricRaster::AtmosphericVolumeShape::Cylinder: return "Cylinder";
        case GeometricRaster::AtmosphericVolumeShape::Cone:     return "Cone";
        case GeometricRaster::AtmosphericVolumeShape::Prism:    return "Regular polygon prism";
        default:                                                return "Box / rectangular volume";
    }
}

[[nodiscard]] const char* ShapeShortTag(GeometricRaster::AtmosphericVolumeShape Shape) noexcept
{
    switch (Shape)
    {
        case GeometricRaster::AtmosphericVolumeShape::Box:      return "BOX";
        case GeometricRaster::AtmosphericVolumeShape::Sphere:   return "SPHERE";
        case GeometricRaster::AtmosphericVolumeShape::Cylinder: return "CYLINDER";
        case GeometricRaster::AtmosphericVolumeShape::Cone:     return "CONE";
        case GeometricRaster::AtmosphericVolumeShape::Prism:    return "PRISM";
        default:                                                return "BOX";
    }
}

void BuildVolumeWireframe(
    GeometricRaster::AtmosphericVolumeShape Shape,
    float HalfX, float HalfY, float HalfZ, uint32_t PrismSides,
    std::vector<Point3F>& OutVerts,
    std::vector<std::pair<std::size_t, std::size_t>>& OutEdges)
{
    OutVerts.clear();
    OutEdges.clear();
    const float HX = std::max(0.5f, HalfX);
    const float HY = std::max(0.5f, HalfY);
    const float HZ = std::max(0.5f, HalfZ);

    if (Shape == GeometricRaster::AtmosphericVolumeShape::Box)
    {
        OutVerts = {
            { -HX, -HY, -HZ }, {  HX, -HY, -HZ }, {  HX,  HY, -HZ }, { -HX,  HY, -HZ },
            { -HX, -HY,  HZ }, {  HX, -HY,  HZ }, {  HX,  HY,  HZ }, { -HX,  HY,  HZ },
        };
        OutEdges = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7},
        };
    }
    else if (Shape == GeometricRaster::AtmosphericVolumeShape::Cylinder || Shape == GeometricRaster::AtmosphericVolumeShape::Prism)
    {
        const uint32_t N = (Shape == GeometricRaster::AtmosphericVolumeShape::Prism) ? std::clamp(PrismSides, 3u, 16u) : 16u;
        const float R = std::max(HX, HY);
        for (uint32_t I = 0u; I < N; ++I)
        {
            const float A = (static_cast<float>(I) / static_cast<float>(N)) * 6.2831853f;
            OutVerts.push_back({ R * std::cos(A), R * std::sin(A), -HZ });
        }
        for (uint32_t I = 0u; I < N; ++I)
        {
            const float A = (static_cast<float>(I) / static_cast<float>(N)) * 6.2831853f;
            OutVerts.push_back({ R * std::cos(A), R * std::sin(A),  HZ });
        }
        for (uint32_t I = 0u; I < N; ++I)
        {
            const uint32_t Next = (I + 1u) % N;
            OutEdges.push_back({ I, Next });
            OutEdges.push_back({ N + I, N + Next });
            OutEdges.push_back({ I, N + I });
        }
    }
    else if (Shape == GeometricRaster::AtmosphericVolumeShape::Cone)
    {
        const uint32_t N = 16u;
        const float R = std::max(HX, HY);
        for (uint32_t I = 0u; I < N; ++I)
        {
            const float A = (static_cast<float>(I) / static_cast<float>(N)) * 6.2831853f;
            OutVerts.push_back({ R * std::cos(A), R * std::sin(A), -HZ });
        }
        OutVerts.push_back({ 0.0f, 0.0f, HZ });
        for (uint32_t I = 0u; I < N; ++I)
        {
            OutEdges.push_back({ I, (I + 1u) % N });
            OutEdges.push_back({ I, N });
        }
    }
    else // Sphere
    {
        const uint32_t Segs = 16u;
        const float R = std::max({ HX, HY, HZ });
        const std::array<float, 3u> Lats = {{ -0.55f, 0.0f, 0.55f }};
        for (std::size_t Ring = 0u; Ring < 3u; ++Ring)
        {
            const float Z  = R * std::sin(Lats[Ring]);
            const float RR = R * std::cos(Lats[Ring]);
            const std::size_t Base = OutVerts.size();
            for (uint32_t I = 0u; I < Segs; ++I)
            {
                const float A = (static_cast<float>(I) / static_cast<float>(Segs)) * 6.2831853f;
                OutVerts.push_back({ RR * std::cos(A), RR * std::sin(A), Z });
            }
            for (uint32_t I = 0u; I < Segs; ++I) OutEdges.push_back({ Base + I, Base + ((I + 1u) % Segs) });
        }
    }
}

void ConstructDimensionInputBox(
    PixelSpace& Surface, float X, float Y, float W,
    const char* Label, float Value) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { X, Y }, 11.5f, InspectorMetrics::MutedInk, Label);
    const Rectangle2D Box { X, Y + 22.0f, W, 34.0f };
    Surface.FillRectangle(Box, ColorRgba{ 22u, 24u, 25u, 255u }, 9.0f);
    Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 16u }, 9.0f, 1.0f);
    char ValBuf[32]{};
    std::snprintf(ValBuf, sizeof(ValBuf), "%.0f", std::round(Value));
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 12.0f, Box.Y + 10.5f }, 12.5f, ColorRgba{ 230u, 232u, 234u, 255u }, ValBuf);
}

} // namespace

void LocalFogInspector::UpdateShapeGlowSurface(const LocalFogInstanceData& Instance)
{
    uint64_t Key = 0x54A9E10Cu;
    Key ^= static_cast<uint64_t>(Instance.LocalFog.Shape) * 0x9E3779B97F4A7C15ull;

    // Exact FogShapePanel.jsx SVG radial glow: #151b1d base + centered radial #adcbd6 (0.16 -> 0.0)
    const uint32_t W = 444u, H = 348u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::FogLayer, 5u, { 0.35f, 1.0f, 0.0f, 0.0f } };
    ShapeGlowPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float Ny = ((static_cast<float>(Y) + 0.5f) / static_cast<float>(H) - 0.5f) / (112.0f / 250.0f);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float Nx = ((static_cast<float>(X) + 0.5f) / static_cast<float>(W) - 0.5f) / (155.0f / 320.0f);
                const float D  = std::sqrt(Nx * Nx + Ny * Ny);
                const float G  = std::max(0.0f, 1.0f - D) * 0.16f;
                const float R  = 21.0f + (173.0f - 21.0f) * G;
                const float Gr = 27.0f + (203.0f - 27.0f) * G;
                const float B  = 29.0f + (214.0f - 29.0f) * G;
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteLF(R);
                Pixels[Off + 1u] = ClampByteLF(Gr);
                Pixels[Off + 2u] = ClampByteLF(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LocalFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LocalFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Medium", "Shape" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Vol = Instance.LocalFog;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const float Sigma = 0.01f * std::max(0.0f, Vol.Density) * std::clamp(Vol.Coverage, 0.0f, 1.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        // ============================================================================================================
        // TAB 1: OVERVIEW — Visibility through fog (629px card), Quick controls (Enabled), Tags, Notes
        // ============================================================================================================
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 629.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Visibility through fog");

        // Status indicator dot + uppercase status label at S.Y + 62 (y=250)
        const bool Live = Entry.VisibilityEnabled;
        const ColorRgba DotInk = Live ? ColorRgba{ 67u, 201u, 102u, 255u } : ColorRgba{ 217u, 103u, 103u, 255u };
        Surface.FillCircle({ BodyX + 4.5f, S.Y + 66.0f }, 3.8f, DotInk);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { BodyX + 14.0f, S.Y + 61.5f }, 9.8f, ColorRgba{ 148u, 152u, 146u, 255u },
            Live ? "LIVE AUTHORING PROBE" : "AUTHORED PREVIEW \xC2\xB7 MEDIUM DISABLED");

        // Big split metric ("33.29 %") at S.Y + 95 (y=283)
        const float ProbeClamped = std::clamp(ProbeDistanceMeters, 0.0f, 2000.0f);
        const float TransPct     = std::exp(-Sigma * ProbeClamped) * 100.0f;
        char PctBuf[24]{};
        std::snprintf(PctBuf, sizeof(PctBuf), "%.2f", TransPct);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, S.Y + 95.0f, PctBuf, "%", nullptr, 40.0f);

        // 2-line caption at S.Y + 152 (y=340)
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, S.Y + 152.0f, BodyW, 12.2f, 16.5f, InspectorMetrics::MutedInk,
            Live ? "Light transmitted after the selected in-medium distance"
                 : "Preview light transmitted after the selected in-medium distance");

        // LiveGraph FogGraph Plot at S.Y + 204 -> SVG top at S.Y + 232 (y=420)
        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 1u> Series = {{
            {
                "Transmission",
                ColorRgba{ 216u, 203u, 176u, 255u },
                [Sigma](float DistM) noexcept -> float { return std::exp(-Sigma * std::max(0.0f, DistM)) * 100.0f; },
            },
        }};
        (void)InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, S.Y + 204.0f, BodyW, 6301u,
            0.0f, 2000.0f, 0.0f, 100.0f, Series, ProbeDistanceMeters,
            "m", "%", "", ActiveDragId, Input, OutMutated);

        // "Drag to sample · arrow keys for precision" at S.Y + 461 (y=649)
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { BodyX, S.Y + 461.0f }, 11.2f, InspectorMetrics::MutedInk,
            "Drag to sample \xC2\xB7 arrow keys for precision");

        // Distance · m input box at S.Y + 486 (y=674)
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, S.Y + 486.0f }, 10.8f, InspectorMetrics::MutedInk, "Distance \xC2\xB7 m");
        const Rectangle2D DistBox { BodyX, S.Y + 506.0f, BodyW, 30.0f };
        Surface.FillRectangle(DistBox, ColorRgba{ 22u, 22u, 22u, 255u }, 7.0f);
        Surface.StrokeRectangle(DistBox, ColorRgba{ 255u, 255u, 255u, 16u }, 7.0f, 1.0f);
        char DistVal[24]{};
        std::snprintf(DistVal, sizeof(DistVal), "%.0f", std::round(ProbeDistanceMeters));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DistBox.X + 10.0f, DistBox.Y + 8.5f }, 11.8f, ColorRgba{ 230u, 230u, 230u, 255u }, DistVal);

        // Wrapped bottom note at S.Y + 548 (y=736)
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, S.Y + 548.0f, BodyW, 12.0f, 16.2f, InspectorMetrics::MutedInk,
            "Homogeneous authoring probe: sigma = 0.01 \xC3\x97 density \xC3\x97 coverage / m. Does not integrate the volume shape.");

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls (1 switch: Enabled)
        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Fog, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            Vol.Enabled             = Entry.VisibilityEnabled;
            OutMutated              = true;
        }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "fog" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        // ============================================================================================================
        // TAB 2: MEDIUM — Density, Coverage, Feature Scale, Anisotropy + FogBeamChamber (530px card)
        // ============================================================================================================
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 530.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Medium");

        float FY = S.Y + 72.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6310u, "Density",       "\xC3\x97", "%.2g",  0.0f,   4.0f, Vol.Density,            ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6311u, "Coverage",      "",         "%.2g",  0.0f,   1.0f, Vol.Coverage,           ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6312u, "Feature Scale", "m",        "%.0f", 10.0f, 500.0f, Vol.FeatureScaleMeters, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6313u, "Anisotropy",    "",         "%.2g", -0.8f,   0.9f, Vol.AnisotropyG,        ActiveDragId, Input, OutMutated);

        // Exact FogPanel.jsx FogBeamChamber box (x=59, y=546, w=222, h=150; aspect-preserving 300x102 SVG -> 198x67.3 at y=602)
        const Rectangle2D Box { BodyX, S.Y + 358.0f, BodyW, 150.0f };
        Surface.FillRectangle(Box, ColorRgba{ 23u, 23u, 23u, 255u }, 16.0f);
        Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 14u }, 16.0f, 1.0f);

        const bool  Live    = Entry.VisibilityEnabled;
        const float Preview = Live ? 1.0f : 0.82f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 12.0f, Box.Y + 14.0f }, 9.8f, ColorRgba{ 146u, 148u, 144u, 255u }, "BEAM CHAMBER");
        const char* LiveStr = Live ? "LIVE" : "PREVIEW";
        const float LW      = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.8f, LiveStr);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Box.Right() - 12.0f - LW, Box.Y + 14.0f }, 9.8f, ColorRgba{ 232u, 234u, 232u, 255u }, LiveStr);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 12.0f, Box.Y + 29.0f }, 9.8f, ColorRgba{ 118u, 120u, 118u, 255u }, "Interior 2 m layer");

        // SVG viewBox 0 0 300 102 with aspect ratio preserved (198 x 67.32 centered in the 198 x 102 SVG viewport)
        const float SvgW = Box.Width - 24.0f;
        const float SvgH = SvgW * (102.0f / 300.0f);
        const Rectangle2D Svg { Box.X + 12.0f, S.Y + 397.0f + (102.0f - SvgH) * 0.5f, SvgW, SvgH };
        const float SX = Svg.Width  / 300.0f;
        const float SY = Svg.Height / 102.0f;
        auto VX = [&](float X) noexcept -> float { return Svg.X + X * SX; };
        auto VY = [&](float Y) noexcept -> float { return Svg.Y + Y * SY; };

        for (float YTick : { 26.0f, 51.0f, 76.0f })
        {
            InspectorPrimitives::ConstructDashedSegment(Surface, { VX(12.0f), VY(YTick) }, { VX(288.0f), VY(YTick) }, ColorRgba{ 255u, 255u, 255u, 18u }, 2.0f, 5.0f, 1.0f);
        }

        const float Spread      = std::max(0.0f, Vol.AnisotropyG);
        const float DistanceMax = 400.0f;
        const ColorRgba Tint { 212u, 227u, 236u, 255u };
        for (int Idx = 0; Idx < 70; ++Idx)
        {
            const float Frac     = static_cast<float>(Idx) / 69.0f;
            const float Dist     = Frac * DistanceMax;
            const float Physical = std::exp(-Sigma * Dist);
            const float Exposure = 0.32f + 0.68f * std::pow(Physical, 0.15f);
            const float HalfH    = 3.0f + Frac * Frac * (11.0f + Spread * 24.0f);
            const float Alpha    = Preview * (0.10f + Spread * 0.20f) * Exposure;
            Surface.FillRectangle(
                { VX(12.0f + Frac * 272.0f), VY(51.0f - HalfH), 4.2f * SX, (HalfH * 2.0f) * SY },
                Tint.WithAlpha(static_cast<uint8_t>(std::clamp(Alpha * 255.0f, 2.0f, 255.0f))), 0.0f);
        }

        const uint8_t EnvAlpha = static_cast<uint8_t>(std::clamp(Preview * 0.42f * 255.0f, 10.0f, 255.0f));
        const uint8_t CenAlpha = static_cast<uint8_t>(std::clamp(Preview * 0.82f * 255.0f, 10.0f, 255.0f));
        Surface.StrokeLine({ VX(12.0f), VY(51.0f) }, { VX(288.0f), VY(40.0f - Spread * 10.0f) }, Tint.WithAlpha(EnvAlpha), 1.0f);
        Surface.StrokeLine({ VX(12.0f), VY(51.0f) }, { VX(288.0f), VY(62.0f + Spread * 10.0f) }, Tint.WithAlpha(EnvAlpha), 1.0f);
        Surface.StrokeLine({ VX(12.0f), VY(51.0f) }, { VX(288.0f), VY(51.0f) }, Tint.WithAlpha(CenAlpha), 1.5f);

        const float RangeM = (Sigma > 1.0e-9f) ? (-std::log(0.02f) / Sigma) : 1.0e9f;
        if (RangeM < 1.0e8f)
        {
            const float MarkerX = 12.0f + std::clamp(RangeM / DistanceMax, 0.0f, 1.0f) * 276.0f;
            InspectorPrimitives::ConstructDashedSegment(Surface, { VX(MarkerX), VY(18.0f) }, { VX(MarkerX), VY(84.0f) }, ColorRgba{ 237u, 143u, 143u, 200u }, 3.0f, 3.0f, 1.0f);
        }
        Surface.FillCircle({ VX(12.0f), VY(51.0f) }, 3.2f, ColorRgba{ 255u, 241u, 202u, static_cast<uint8_t>(Preview * 255.0f) });

        char LeftLbl[48]{}, RightLbl[48]{}, BotLbl[48]{};
        std::snprintf(LeftLbl, sizeof(LeftLbl), "ANISOTROPY \xC2\xB7 %.2f", Spread);
        if (RangeM >= 1.0e8f)      std::snprintf(RightLbl, sizeof(RightLbl), "2%% \xC2\xB7 CLEAR");
        else if (RangeM >= 1000.0f) std::snprintf(RightLbl, sizeof(RightLbl), "2%% \xC2\xB7 %.1f km", RangeM / 1000.0f);
        else                        std::snprintf(RightLbl, sizeof(RightLbl), "2%% \xC2\xB7 %.0f m", std::round(RangeM));
        const int Phys400 = static_cast<int>(std::lround(std::exp(-Sigma * DistanceMax) * 100.0f));
        std::snprintf(BotLbl, sizeof(BotLbl), "%d%% PHYSICAL AT %.0f m", Phys400, DistanceMax);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { VX(10.0f), VY(6.0f) }, 7.8f, ColorRgba{ 158u, 162u, 158u, 255u }, LeftLbl);
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.8f, RightLbl);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { VX(290.0f) - RW, VY(6.0f) }, 7.8f, ColorRgba{ 158u, 162u, 158u, 255u }, RightLbl);
        const float BW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.8f, BotLbl);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { VX(290.0f) - BW, VY(88.0f) }, 7.8f, ColorRgba{ 148u, 152u, 148u, 255u }, BotLbl);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // ============================================================================================================
        // TAB 3: SHAPE — Fog volume shape (dropdown, isometric wireframe, Width/Depth/Height, Transform)
        // ============================================================================================================
        UpdateShapeGlowSurface(Instance);

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 840.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Fog volume shape");

        // Shape dropdown pill at S.Y + 62 (label) / S.Y + 84 (black pill h=34)
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, S.Y + 62.0f }, 12.0f, InspectorMetrics::MutedInk, "Shape");
        const Rectangle2D DropRect { BodyX, S.Y + 84.0f, BodyW, 34.0f };
        Surface.FillRectangle(DropRect, ColorRgba{ 2u, 2u, 2u, 255u }, InspectorMetrics::ControlRadius);
        Surface.StrokeRectangle(DropRect, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::ControlRadius, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DropRect.X + 16.0f, DropRect.Y + 10.5f }, 12.2f, ColorRgba{ 230u, 232u, 232u, 255u }, ShapeOptionLabel(Vol.Shape));
        // Down chevron on right
        const float Cx = DropRect.Right() - 16.0f, Cy = DropRect.Center().Y;
        Surface.StrokeLine({ Cx - 4.0f, Cy - 2.0f }, { Cx, Cy + 2.5f }, ColorRgba{ 216u, 218u, 218u, 255u }, 1.6f);
        Surface.StrokeLine({ Cx, Cy + 2.5f }, { Cx + 4.0f, Cy - 2.0f }, ColorRgba{ 216u, 218u, 218u, 255u }, 1.6f);
        if (DropRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            const uint32_t Next = (static_cast<uint32_t>(Vol.Shape) + 1u) % 5u;
            Vol.Shape  = static_cast<GeometricRaster::AtmosphericVolumeShape>(Next);
            OutMutated = true;
        }

        // Isometric wireframe preview box at exact HTML S.Y + 156 (y=344), w=222, h=173.9
        const Rectangle2D PrevRect { BodyX, S.Y + 156.0f, BodyW, 173.9f };
        ShapeGlowPreview.Present(Surface, PrevRect, 12.0f);
        Surface.StrokeRectangle(PrevRect, ColorRgba{ 255u, 255u, 255u, 16u }, 12.0f, 1.0f);

        std::vector<Point3F> Verts;
        std::vector<std::pair<std::size_t, std::size_t>> Edges;
        BuildVolumeWireframe(Vol.Shape, Vol.HalfExtentMeters.X, Vol.HalfExtentMeters.Y, Vol.HalfExtentMeters.Z, Vol.PrismSides, Verts, Edges);

        if (!Verts.empty())
        {
            const float SqrtHalf = 0.70710678f;
            const float InvSqrt6 = 0.40824829f;
            const float Sqrt23   = 0.81649658f;
            std::vector<Point2D> Raw;
            Raw.reserve(Verts.size());
            float MinX = 1e9f, MaxX = -1e9f, MinY = 1e9f, MaxY = -1e9f;
            for (const auto& P : Verts)
            {
                const float Px = (P.X - P.Y) * SqrtHalf;
                const float Py = (P.X + P.Y) * InvSqrt6 - P.Z * Sqrt23;
                Raw.push_back({ Px, Py });
                MinX = std::min(MinX, Px); MaxX = std::max(MaxX, Px);
                MinY = std::min(MinY, Py); MaxY = std::max(MaxY, Py);
            }
            const float Scale = std::min(270.0f / std::max(0.001f, MaxX - MinX), 190.0f / std::max(0.001f, MaxY - MinY));
            const float SX    = PrevRect.Width  / 320.0f;
            const float SY    = PrevRect.Height / 250.0f;
            std::vector<Point2D> ScreenPts;
            ScreenPts.reserve(Raw.size());
            for (const auto& R : Raw)
            {
                const float SvgX = 160.0f + (R.X - 0.5f * (MinX + MaxX)) * Scale;
                const float SvgY = 120.0f + (R.Y - 0.5f * (MinY + MaxY)) * Scale;
                ScreenPts.push_back({ PrevRect.X + SvgX * SX, PrevRect.Y + SvgY * SY });
            }
            const ColorRgba WireInk = ColorRgba{ 175u, 206u, 218u, static_cast<uint8_t>(Vol.Shape == GeometricRaster::AtmosphericVolumeShape::Sphere ? 105u : 155u) };
            for (const auto& [A, B] : Edges)
            {
                Surface.StrokeLine(ScreenPts[A], ScreenPts[B], WireInk, 0.85f);
            }
        }

        char CapBuf[64]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%s \xC2\xB7 Z UP \xC2\xB7 FIT TO VOLUME", ShapeShortTag(Vol.Shape));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PrevRect.X + 10.0f, PrevRect.Bottom() - 16.0f }, 7.5f, ColorRgba{ 129u, 150u, 159u, 255u }, CapBuf);

        // 2-column Dimensions: Width (m), Depth (m), Height (m)
        const float HalfCol = std::floor((BodyW - 14.0f) * 0.5f);
        ConstructDimensionInputBox(Surface, BodyX,                  S.Y + 350.0f, HalfCol, "Width (m)",  Vol.HalfExtentMeters.X * 2.0f);
        ConstructDimensionInputBox(Surface, BodyX + HalfCol + 14.0f, S.Y + 350.0f, HalfCol, "Depth (m)",  Vol.HalfExtentMeters.Y * 2.0f);
        ConstructDimensionInputBox(Surface, BodyX,                  S.Y + 422.0f, HalfCol, "Height (m)", Vol.HalfExtentMeters.Z * 2.0f);

        // Transform header (matching TransformPanel.jsx inside FogShapePanel)
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { BodyX, S.Y + 502.0f }, 14.5f, ColorRgba{ 209u, 209u, 206u, 255u }, "Transform");
        const float WSW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, "WORLD SPACE");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - WSW, S.Y + 518.0f }, 9.2f, InspectorMetrics::DimInk, "WORLD SPACE");

        // Component | X | Y | Z column headers at S.Y + 568 (y=756)
        const float ColY = S.Y + 568.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX,          ColY }, 9.8f, InspectorMetrics::DimInk, "Component");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 86.0f,  ColY }, 10.2f, ColorRgba{ 216u, 134u, 126u, 255u }, "X");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 140.0f, ColY }, 10.2f, ColorRgba{ 148u, 186u, 138u, 255u }, "Y");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 194.0f, ColY }, 10.2f, ColorRgba{ 126u, 168u, 218u, 255u }, "Z");

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
