/*============================================================================================================================================
                                                         STARFIELDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Stars (Starfield) Inspector (4 tabs: Overview, Motion, Renderer, Baking)

#include "Engine/Editor/Inspectors/StarfieldInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] uint8_t ClampByteSt(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

} // namespace

void StarfieldInspector::UpdateStarChartSurface(const GeometricRaster::StarfieldConfiguration& Stars)
{
    uint64_t Key = 0x57A41101ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Stars.LimitingMagnitude);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Stars.BrightnessMultiplier);

    const uint32_t W = 444u, H = 212u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::StarfieldSheet, 0u, { Stars.BrightnessMultiplier, Stars.LimitingMagnitude, 0.0f, 0.0f } };
    StarChart.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = 26u; Pixels[Off + 1u] = 27u; Pixels[Off + 2u] = 32u; Pixels[Off + 3u] = 255u;
            }
        }
        const uint32_t Count = static_cast<uint32_t>(std::clamp(Stars.LimitingMagnitude * 42.0f, 30.0f, 380.0f));
        uint32_t Rng = 0x12345678u;
        auto NextF = [&Rng]() noexcept
        {
            Rng = Rng * 1664525u + 1013904223u;
            return static_cast<float>(Rng >> 8u) / 16777216.0f;
        };
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const float SX = 8.0f + NextF() * (static_cast<float>(W) - 16.0f);
            const float SY = 8.0f + NextF() * (static_cast<float>(H) - 16.0f);
            const float Mag = NextF();
            const float Rad = (Mag > 0.85f) ? 1.8f : 1.1f;
            const float Bri = (0.35f + 0.65f * Mag) * 230.0f;
            const int X0 = std::max(0, static_cast<int>(SX - 2.0f));
            const int X1 = std::min(static_cast<int>(W) - 1, static_cast<int>(SX + 2.0f));
            const int Y0 = std::max(0, static_cast<int>(SY - 2.0f));
            const int Y1 = std::min(static_cast<int>(H) - 1, static_cast<int>(SY + 2.0f));
            for (int PY = Y0; PY <= Y1; ++PY)
            {
                for (int PX = X0; PX <= X1; ++PX)
                {
                    const float D = std::hypot(static_cast<float>(PX) - SX, static_cast<float>(PY) - SY);
                    const float Cov = std::exp(-(D * D) / std::max(0.35f, Rad * Rad * 0.45f));
                    const std::size_t Off = (static_cast<std::size_t>(PY) * W + static_cast<std::size_t>(PX)) * 4u;
                    Pixels[Off + 0u] = ClampByteSt(static_cast<float>(Pixels[Off + 0u]) + Bri * Cov);
                    Pixels[Off + 1u] = ClampByteSt(static_cast<float>(Pixels[Off + 1u]) + Bri * Cov);
                    Pixels[Off + 2u] = ClampByteSt(static_cast<float>(Pixels[Off + 2u]) + (Bri + 18.0f) * Cov);
                }
            }
        }
    });
}

void StarfieldInspector::UpdateStarSheetSurface(const GeometricRaster::StarfieldConfiguration& Stars, bool EntryVisible)
{
    uint64_t Key = 0x57A44404ull ^ (EntryVisible ? 0x1ull : 0x0ull);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Stars.LimitingMagnitude);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Stars.BrightnessMultiplier);
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::StarfieldSheet, 1u, { Stars.BrightnessMultiplier, Stars.LimitingMagnitude, 0.0f, 0.0f } };
    StarSheetBake.EnsureUpdatedGpu(Key, 256u, 128u, GpuSpec, [](std::vector<uint8_t>& Pixels)
    {
        for (std::size_t I = 0u; I < Pixels.size(); I += 4u)
        {
            Pixels[I + 0u] = 12u; Pixels[I + 1u] = 14u; Pixels[I + 2u] = 22u; Pixels[I + 3u] = 255u;
        }
    });
}

float StarfieldInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, StarfieldInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Motion", "Renderer", "Baking" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Stars   = Instance.Stars;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Star field (h = 425.5), Quick controls (5 switches)
        UpdateStarChartSurface(Stars);
        const Rectangle2D FieldCard { LeftX, CursorY, InnerWidth, 425.5f };
        InspectorPrimitives::ConstructSectionSurface(Surface, FieldCard, "Star field");

        char MagBuf[16]{};
        std::snprintf(MagBuf, sizeof(MagBuf), "%.1f", Stars.LimitingMagnitude);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, FieldCard.Y + 62.0f, MagBuf, "mag", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FieldCard.Y + 116.5f }, 11.4f, InspectorMetrics::MutedInk, "Higher values reveal fainter stars");

        const Rectangle2D ChartBox { BodyX, FieldCard.Y + 154.0f, BodyW, 106.0f };
        StarChart.Present(Surface, ChartBox, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FieldCard.Y + 283.5f, BodyW, 7901u, "Limiting magnitude", "mag", "%.1f", 0.0f, 8.0f, Stars.LimitingMagnitude, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, FieldCard.Y + 355.5f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "Equatorial chart study \xC2\xB7 no live star catalogue connection");

        CursorY = FieldCard.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 5u> Quick = {{
            { "Star field",     InspectorSwitchSymbol::Stars,   Entry.VisibilityEnabled,       false },
            { "Twinkle",        InspectorSwitchSymbol::Twinkle, Stars.ScintillationEnabled,    false },
            { "Bright catalog", InspectorSwitchSymbol::Stars,   Stars.EnableBrightStarCatalog, false },
            { "Milky Way",      InspectorSwitchSymbol::Galaxy,  Stars.EnableGalacticBand,      false },
            { "Zodiacal light", InspectorSwitchSymbol::Zodiac,  Stars.ZodiacalLightEnabled,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Stars.StarfieldEnabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Stars.ScintillationEnabled = !Stars.ScintillationEnabled; OutMutated = true; }
        else if (Clicked == 2) { Stars.EnableBrightStarCatalog = !Stars.EnableBrightStarCatalog; OutMutated = true; }
        else if (Clicked == 3) { Stars.EnableGalacticBand = !Stars.EnableGalacticBand; OutMutated = true; }
        else if (Clicked == 4) { Stars.ZodiacalLightEnabled = !Stars.ZodiacalLightEnabled; OutMutated = true; }
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: MOTION — Twinkle (h = 478.9), Celestial rotation (h = 260)
        const Rectangle2D TwCard { LeftX, CursorY, InnerWidth, 478.9f };
        InspectorPrimitives::ConstructSectionSurface(Surface, TwCard, "Twinkle");

        char PctBuf[16]{};
        std::snprintf(PctBuf, sizeof(PctBuf), "%.0f", Stars.ScintillationDepthPercent);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, TwCard.Y + 62.0f, PctBuf, "%", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, TwCard.Y + 116.5f }, 11.4f, InspectorMetrics::MutedInk, "Modulation depth");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, TwCard.Y + 148.0f }, 10.6f, InspectorMetrics::HeadingInk, "RELATIVE BRIGHTNESS");
        const float LsW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, "LIVE SIGNAL");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - LsW, TwCard.Y + 148.0f }, 10.6f, InspectorMetrics::HeadingInk, "LIVE SIGNAL");

        // Star pulse icon + instantaneous readout
        const Point2D StarC { BodyX + 44.0f, TwCard.Y + 190.0f };
        Surface.FillCircle(StarC, 16.0f, ColorRgba{ 165u, 158u, 198u, 22u });
        Surface.FillCircle(StarC, 8.0f, ColorRgba{ 195u, 188u, 225u, 52u });
        Surface.StrokeLine({ StarC.X - 7.0f, StarC.Y }, { StarC.X + 7.0f, StarC.Y }, ColorRgba{ 238u, 232u, 255u, 225u }, 1.2f);
        Surface.StrokeLine({ StarC.X, StarC.Y - 7.0f }, { StarC.X, StarC.Y + 7.0f }, ColorRgba{ 238u, 232u, 255u, 225u }, 1.2f);
        Surface.FillCircle(StarC, 2.2f, ColorRgba{ 255u, 255u, 255u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 94.0f, TwCard.Y + 180.0f }, 9.4f, InspectorMetrics::MutedInk, "93%");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 94.0f, TwCard.Y + 194.0f }, 8.6f, InspectorMetrics::DimInk, "instantaneous brightness");

        // Live modulation trace box
        const Rectangle2D TraceBox { BodyX + 8.0f, TwCard.Y + 220.0f, BodyW - 16.0f, 52.0f };
        Surface.FillRectangle(TraceBox, ColorRgba{ 28u, 26u, 34u, 255u }, 8.0f);
        const float Amp = std::clamp(Stars.ScintillationDepthPercent / 100.0f, 0.0f, 1.0f) * 14.0f;
        const int Steps = 48;
        Point2D Prev {};
        for (int I = 0; I <= Steps; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(Steps);
            const float X = TraceBox.X + 6.0f + T * (TraceBox.Width - 12.0f);
            const float Y = TraceBox.Y + 16.0f + std::sin(T * 18.0f * std::max(0.4f, Stars.ScintillationRateHertz)) * Amp * 0.55f + std::cos(T * 7.5f) * Amp * 0.25f;
            const Point2D Pt { X, Y };
            if (I > 0) Surface.StrokeLine(Prev, Pt, ColorRgba{ 168u, 150u, 198u, 225u }, 1.4f);
            Prev = Pt;
        }
        Surface.FillCircle(Prev, 2.8f, ColorRgba{ 246u, 244u, 252u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TraceBox.X + 6.0f, TraceBox.Bottom() + 6.0f }, 8.4f, InspectorMetrics::DimInk, "-4s");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TraceBox.Center().X - 6.0f, TraceBox.Bottom() + 6.0f }, 8.4f, InspectorMetrics::DimInk, "-2s");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TraceBox.Right() - 18.0f, TraceBox.Bottom() + 6.0f }, 8.4f, InspectorMetrics::DimInk, "now");

        float FY = TwCard.Y + 312.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7910u, "Twinkle depth", "%",  "%.0f", 0.0f, 100.0f, Stars.ScintillationDepthPercent, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7911u, "Twinkle rate",  "Hz", "%.2g", 0.2f,   3.0f, Stars.ScintillationRateHertz,    ActiveDragId, Input, OutMutated);
        CursorY = TwCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Celestial rotation
        const Rectangle2D RotCard { LeftX, CursorY, InnerWidth, 240.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, RotCard, "Celestial rotation");
        char RotBuf[16]{};
        std::snprintf(RotBuf, sizeof(RotBuf), "%.0f\xC2\xB0", Stars.CelestialRotationDegrees);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, RotCard.Y + 62.0f, RotBuf, "", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, RotCard.Y + 116.5f }, 11.4f, InspectorMetrics::MutedInk, "Rotate the field around the celestial pole");
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, RotCard.Y + 150.0f, BodyW, 7912u, "Rotation", "deg", "%.0f", 0.0f, 360.0f, Stars.CelestialRotationDegrees, ActiveDragId, Input, OutMutated);
        CursorY = RotCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        // TAB 3: RENDERER — Card 1 Renderer scale (h = 341), Card 2 Zodiacal light (h = 207)
        const Rectangle2D RenCard { LeftX, CursorY, InnerWidth, 341.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, RenCard, "Renderer scale");
        float FY = RenCard.Y + 68.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7920u, "Brightness", "\xC3\x97", "%.2g", 0.0f, 4.0f, Stars.BrightnessMultiplier, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7921u, "Point Size", "\xC3\x97", "%.2g", 0.4f, 3.0f, Stars.PointSizeScale,       ActiveDragId, Input, OutMutated);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FY + 4.0f }, 11.4f, InspectorMetrics::MutedInk, "Loaded");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 10.0f, FY + 4.0f }, 11.4f, InspectorMetrics::MutedInk, "-");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, FY + 32.0f }, 11.4f, InspectorMetrics::MutedInk, "Sidereal Time");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 10.0f, FY + 32.0f }, 11.4f, InspectorMetrics::MutedInk, "-");

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, RenCard.Y + 271.0f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "The chart omits atmosphere and daylight extinction. The pole dial is a schematic.");
        CursorY = RenCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Zodiacal light (h = 207)
        const Rectangle2D ZodCard { LeftX, CursorY, InnerWidth, 207.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ZodCard, "Zodiacal light");

        const float SwY = ZodCard.Y + 64.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, SwY + 4.0f }, 11.6f, InspectorMetrics::HeadingInk, "Zodiacal light");
        const Rectangle2D Pill { BodyX + BodyW - 44.0f, SwY, 44.0f, 24.0f };
        Surface.FillRectangle(Pill, Stars.ZodiacalLightEnabled ? ColorRgba{ 62u, 64u, 68u, 255u } : ColorRgba{ 44u, 44u, 46u, 255u }, 12.0f);
        const float DotX = Stars.ZodiacalLightEnabled ? (Pill.X + 12.0f) : (Pill.Right() - 12.0f);
        Surface.FillCircle({ DotX, Pill.Center().Y }, 9.5f, ColorRgba{ 244u, 244u, 244u, 255u });
        if (Pill.Contains(Cursor) && Input.PrimaryPointer.Pressed) { Stars.ZodiacalLightEnabled = !Stars.ZodiacalLightEnabled; OutMutated = true; }

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ZodCard.Y + 112.0f, BodyW, 7922u, "Zodiacal radiance", "\xC3\x97", "%.2g", 0.0f, 4.0f, Stars.ZodiacalRadianceScale, ActiveDragId, Input, OutMutated);
        CursorY = ZodCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 4: BAKING — BAKING eyebrow + Star field bake (h = 197)
        UpdateStarSheetSurface(Stars, Entry.VisibilityEnabled);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, "BAKING");
        const char* SubRight = "Browser bake \xC2\xB7 the 3D viewport uses this";
        const float SRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, SubRight);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + InnerWidth - SRW, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, SubRight);
        CursorY += 34.0f;

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 197.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Star field bake");
        const float Gap   = 12.0f;
        const float HalfW = std::floor((BodyW - Gap) * 0.5f);

        const Rectangle2D Tile { BodyX, S.Y + 62.0f, HalfW, 112.0f };
        Surface.FillRectangle(Tile, ColorRgba{ 30u, 30u, 30u, 255u }, 14.0f);
        Surface.StrokeRectangle(Tile, ColorRgba{ 255u, 255u, 255u, 16u }, 14.0f, 1.0f);
        const Rectangle2D Badge { Tile.Center().X - 18.0f, Tile.Y + 12.0f, 36.0f, 36.0f };
        Surface.FillRectangle(Badge, ColorRgba{ 46u, 46u, 46u, 255u }, 9.0f);
        const Point2D Bc = Badge.Center();
        Surface.StrokeLine({ Bc.X, Bc.Y - 7.0f }, { Bc.X, Bc.Y + 3.0f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        Surface.StrokeLine({ Bc.X - 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        Surface.StrokeLine({ Bc.X + 4.0f, Bc.Y - 1.0f }, { Bc.X, Bc.Y + 3.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        Surface.StrokeLine({ Bc.X - 6.0f, Bc.Y + 6.5f }, { Bc.X + 6.0f, Bc.Y + 6.5f }, ColorRgba{ 165u, 165u, 165u, 255u }, 1.8f);
        const float L1W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "Use baked");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - L1W * 0.5f, Tile.Y + 56.0f }, 11.2f, ColorRgba{ 196u, 196u, 194u, 255u }, "Use baked");
        const float L2W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.2f, "image");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - L2W * 0.5f, Tile.Y + 70.0f }, 11.2f, ColorRgba{ 196u, 196u, 194u, 255u }, "image");
        const float StW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, "UNAVAILABLE");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - StW * 0.5f, Tile.Bottom() - 16.0f }, 8.6f, InspectorMetrics::DimInk, "UNAVAILABLE");

        const float RightX = BodyX + HalfW + Gap;
        const Rectangle2D BakeBtn { RightX, S.Y + 62.0f, HalfW, 33.0f };
        Surface.FillRectangle(BakeBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
        Surface.StrokeRectangle(BakeBtn, ColorRgba{ 255u, 255u, 255u, 28u }, 9.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakeBtn.Center().X - 10.0f, BakeBtn.Y + 10.5f }, 11.2f, InspectorMetrics::HeadingInk, "Bake");

        const Rectangle2D LoadBtn { RightX, S.Y + 103.0f, HalfW, 33.0f };
        Surface.FillRectangle(LoadBtn, ColorRgba{ 36u, 36u, 36u, 255u }, 9.0f);
        Surface.StrokeRectangle(LoadBtn, ColorRgba{ 255u, 255u, 255u, 22u }, 9.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LoadBtn.Center().X - 24.0f, LoadBtn.Y + 10.5f }, 11.2f, InspectorMetrics::DimInk, "Load image");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RightX, S.Y + 152.0f }, 9.6f, InspectorMetrics::DimInk, "Not baked \xC2\xB7 1024 \xC3\x97 512");

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
