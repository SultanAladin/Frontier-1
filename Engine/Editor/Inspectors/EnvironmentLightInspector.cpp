/*============================================================================================================================================
                                                      ENVIRONMENTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Environment Light Inspector (2 tabs: Overview, Settings)

#include "Engine/Editor/Inspectors/EnvironmentLightInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] uint8_t ClampByteEl(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}
} // namespace

void EnvironmentLightInspector::UpdateHemisphereSurface(const EnvironmentLightInstanceData& Instance)
{
    uint64_t Key = 0xE11B0001ull ^ (static_cast<uint64_t>(Instance.SourcePresetIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.RotationDegrees);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.GainScale);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.SkyRadiance.R);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.SkyRadiance.G);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.SkyRadiance.B);

    // Exact 268 x 144 sky-dome side view + top-view blue compass (2x crisp: 536 x 288)
    const uint32_t W = 536u, H = 288u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::EnvironmentDome, 0u, { Instance.GainScale, Instance.RotationDegrees, 0.0f, 0.0f } };
    HemispherePreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float SideCx = 166.0f, SideCy = 216.0f, SideR = 132.0f;
        const float TopCx  = 416.0f, TopCy  = 144.0f, TopR  = 86.0f;
        const float AzRad  = (135.0f + Instance.RotationDegrees) * 0.01745329f;
        const float SunTx  = TopCx + std::sin(AzRad) * TopR * 0.56f;
        const float SunTy  = TopCy - std::cos(AzRad) * TopR * 0.56f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                float R = 21.0f, G = 21.0f, B = 23.0f;

                // Left: Side-view sky dome + ground slab + central shaded probe sphere
                const float SDX = static_cast<float>(X) - SideCx;
                const float SDY = static_cast<float>(Y) - SideCy;
                const float SD  = std::sqrt(SDX * SDX + SDY * SDY);
                if (SDY <= 0.0f && SD <= SideR)
                {
                    const float Elev = std::clamp(-SDY / SideR, 0.0f, 1.0f);
                    R = 198.0f * (1.0f - Elev) + 84.0f  * Elev;
                    G = 216.0f * (1.0f - Elev) + 148.0f * Elev;
                    B = 234.0f * (1.0f - Elev) + 214.0f * Elev;
                    // Sun glow on right slope
                    const float SunGlow = std::exp(-std::hypot(SDX - 64.0f, SDY + 62.0f) / 36.0f) * 48.0f;
                    R += SunGlow; G += SunGlow; B += SunGlow * 0.85f;
                    if (SD > SideR - 2.0f) { R = 165.0f; G = 198.0f; B = 232.0f; }
                }
                // Ground bar under dome
                if (SDY > 0.0f && SDY <= 22.0f && std::fabs(SDX) <= SideR + 8.0f)
                {
                    R = 96.0f; G = 88.0f; B = 80.0f;
                }
                // Shaded white probe sphere resting on ground
                const float PDX = static_cast<float>(X) - SideCx;
                const float PDY = static_cast<float>(Y) - (SideCy - 24.0f);
                const float PD  = std::sqrt(PDX * PDX + PDY * PDY);
                if (PD <= 26.0f)
                {
                    const float NX = PDX / 26.0f, NY = PDY / 26.0f;
                    const float NZ = std::sqrt(std::max(0.0f, 1.0f - NX * NX - NY * NY));
                    const float L  = std::clamp(NX * 0.42f - NY * 0.58f + NZ * 0.68f, 0.15f, 1.0f);
                    const float Spec = std::pow(std::max(0.0f, NX * 0.25f - NY * 0.45f + NZ * 0.86f), 14.0f) * 0.45f;
                    R = 65.0f + L * 185.0f + Spec * 70.0f;
                    G = 72.0f + L * 185.0f + Spec * 70.0f;
                    B = 82.0f + L * 182.0f + Spec * 70.0f;
                }

                // Right: Top-view sky compass disc
                const float TDX = static_cast<float>(X) - TopCx;
                const float TDY = static_cast<float>(Y) - TopCy;
                const float TD  = std::sqrt(TDX * TDX + TDY * TDY);
                if (TD <= TopR)
                {
                    const float T = TD / TopR;
                    R = 88.0f  * (1.0f - T) + 214.0f * T;
                    G = 152.0f * (1.0f - T) + 226.0f * T;
                    B = 216.0f * (1.0f - T) + 238.0f * T;
                    const float SDot = std::hypot(static_cast<float>(X) - SunTx, static_cast<float>(Y) - SunTy);
                    const float SG = std::exp(-SDot / 18.0f) * 95.0f;
                    R += SG; G += SG; B += SG * 0.9f;
                    if (SDot < 6.0f) { R = 255.0f; G = 255.0f; B = 248.0f; }
                    if (TD < 4.5f)   { R = 18.0f;  G = 22.0f;  B = 28.0f;  }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteEl(R);
                Pixels[Off + 1u] = ClampByteEl(G);
                Pixels[Off + 2u] = ClampByteEl(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float EnvironmentLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, EnvironmentLightInstanceData& Instance,
    ViewportDisplaySettings* /*DisplaySettings*/,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    static constexpr std::array<const char*, 5u> SourceNames = {{
        "Sky atmosphere", "HDRI Studio", "HDRI Overcast", "HDRI Sunset", "Uniform colour"
    }};

    if (ActivePage == 0u)
    {
        UpdateHemisphereSurface(Instance);
        // TAB 1: OVERVIEW — Sky dome (h = 400) + Quick controls (5 switches)
        const Rectangle2D DomeCard { WideX, CursorY, WideW, 400.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, DomeCard, "Sky dome", SourceNames[std::min(Instance.SourcePresetIndex, 4u)], InspectorMetrics::DimInk);

        const float LuxK = 12.0f * std::max(0.05f, Instance.GainScale);
        char LuxBuf[24]{};
        std::snprintf(LuxBuf, sizeof(LuxBuf), "%.0fk", LuxK);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, DomeCard.Y + 56.0f, LuxBuf, "lx on a level surface", nullptr, 38.0f);

        // Dark sky-dome + compass well
        const Rectangle2D Well { BodyX - 3.0f, DomeCard.Y + 110.0f, BodyW + 6.0f, 160.0f };
        HemispherePreview.Present(Surface, Well, InspectorMetrics::PreviewRadius, ColorRgba{ 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, ColorRgba{ 21u, 21u, 23u, 255u });
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, InspectorMetrics::PreviewRadius, 1.0f);
        // Compass letters & captions matching environment-light__0
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 12.0f,  Well.Bottom() - 15.0f }, 8.0f, InspectorMetrics::DimInk, "W");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 44.0f,  Well.Bottom() - 15.0f }, 8.0f, InspectorMetrics::DimInk, "side view \xC2\xB7 looking north");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 144.0f, Well.Bottom() - 15.0f }, 8.0f, InspectorMetrics::DimInk, "E");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 172.0f, Well.Bottom() - 15.0f }, 8.0f, InspectorMetrics::DimInk, "top view \xC2\xB7 rings 30\xC2\xB0 / 60\xC2\xB0");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 206.0f, Well.Y + 10.0f }, 8.4f, InspectorMetrics::MutedInk, "N");

        // 3 KPI tiles: KEY LIGHT SE · 38°, DIRECT 55%, BOUNCE 30%
        const float KpiY = Well.Bottom() + 12.0f;
        const float KpiGap = 8.0f;
        const float KpiW = std::floor((BodyW - KpiGap * 2.0f) / 3.0f);
        char BounceBuf[16]{};
        std::snprintf(BounceBuf, sizeof(BounceBuf), "%.0f%%", Instance.GroundBounce * 100.0f);
        const std::array<std::pair<const char*, const char*>, 3u> Kpis = {{
            { "KEY LIGHT", "SE \xC2\xB7 38\xC2\xB0" },
            { "DIRECT",    "55%" },
            { "BOUNCE",    BounceBuf },
        }};
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D T { BodyX + static_cast<float>(I) * (KpiW + KpiGap), KpiY, KpiW, 54.0f };
            Surface.FillRectangle(T, ColorRgba{ 26u, 26u, 27u, 255u }, 12.0f);
            Surface.StrokeRectangle(T, ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T.X + 10.0f, T.Y + 9.0f }, 8.4f, InspectorMetrics::DimInk, Kpis[I].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T.X + 10.0f, T.Y + 27.0f }, 14.0f, InspectorMetrics::HeadingInk, Kpis[I].second);
        }

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, KpiY + 64.0f, BodyW, 10.2f, 14.8f, InspectorMetrics::DimInk,
            "Light arriving from the whole sky \xC2\xB7 drag to rotate the environment. Diagram of the authored values; not the native renderer.");
        CursorY = DomeCard.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls (4 / 5 on: Enabled, Diffuse, Specular, Shadows, Use sun)
        const std::array<InspectorQuickSwitchSpec, 5u> Quick = {{
            { "Enabled",  InspectorSwitchSymbol::Eye,       Entry.VisibilityEnabled, false },
            { "Diffuse",  InspectorSwitchSymbol::Orbit,     Instance.IndirectGi,     false },
            { "Specular", InspectorSwitchSymbol::Stars,     true,                    false },
            { "Shadows",  InspectorSwitchSymbol::Shadow,    true,                    false },
            { "Use sun",  InspectorSwitchSymbol::Sun,       false,                   false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, WideX, CursorY, WideW, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.IndirectGi = !Instance.IndirectGi; OutMutated = true; }
    }
    else
    {
        // TAB 2: SETTINGS — Card 1 Source (h = 283), Card 2 Response (h = 379)
        const Rectangle2D SrcCard { WideX, CursorY, WideW, 283.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SrcCard, "Source");
        float SY = SrcCard.Y + 68.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, SY }, 11.4f, InspectorMetrics::MutedInk, "Source");
        const Rectangle2D Drop { BodyX, SY + 18.0f, BodyW, 34.0f };
        Surface.FillRectangle(Drop, ColorRgba{ 12u, 12u, 12u, 255u }, InspectorMetrics::ControlRadius);
        Surface.StrokeRectangle(Drop, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::ControlRadius, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Drop.X + 16.0f, Drop.Y + 10.5f }, 12.2f, InspectorMetrics::HeadingInk, SourceNames[std::min(Instance.SourcePresetIndex, 4u)]);
        const Point2D Ch { Drop.Right() - 16.0f, Drop.Center().Y };
        Surface.StrokeLine({ Ch.X - 4.0f, Ch.Y - 2.0f }, { Ch.X, Ch.Y + 2.5f }, InspectorMetrics::HeadingInk, 1.5f);
        Surface.StrokeLine({ Ch.X, Ch.Y + 2.5f }, { Ch.X + 4.0f, Ch.Y - 2.0f }, InspectorMetrics::HeadingInk, 1.5f);
        if (Drop.Contains(Cursor) && Input.PrimaryPointer.Pressed) { Instance.SourcePresetIndex = (Instance.SourcePresetIndex + 1u) % 5u; OutMutated = true; }

        SY = Drop.Bottom() + 20.0f;
        SY = InspectorPrimitives::ConstructColourFieldRow(Surface, BodyX, SY, BodyW, 8500u, "Colour", Instance.SkyRadiance, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY + 6.0f, BodyW, 8501u, "Rotation", "\xC2\xB0", "%.0f", 0.0f, 360.0f, Instance.RotationDegrees, ActiveDragId, Input, OutMutated);
        CursorY = SrcCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D RespCard { WideX, CursorY, WideW, 379.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, RespCard, "Response");
        float RY = RespCard.Y + 68.0f;
        RY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, RY, BodyW, 8502u, "Intensity",     "\xC3\x97", "%.2g", 0.0f, 4.0f, Instance.GainScale,    ActiveDragId, Input, OutMutated);
        RY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, RY, BodyW, 8503u, "Exposure",      "EV",       "%.2g", -4.0f, 4.0f, Instance.ExposureEv,  ActiveDragId, Input, OutMutated);
        RY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, RY, BodyW, 8504u, "Saturation",    "",         "%.2g", 0.0f, 2.0f, Instance.Saturation,   ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, RY, BodyW, 8505u, "Ground bounce", "",        "%.2g", 0.0f, 1.0f, Instance.GroundBounce, ActiveDragId, Input, OutMutated);
        CursorY = RespCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
