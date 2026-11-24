/*============================================================================================================================================
                                                       PRECIPITATIONINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Precipitation Inspector (3 tabs: Overview, Particles, Simulation)

#include "Engine/Editor/Inspectors/PrecipitationInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

void ConstructPrecipTypeGlyph(PixelSpace& Surface, Point2D Center, uint32_t TypeIndex) noexcept
{
    if (TypeIndex == 0u || TypeIndex == 1u) // Rain / Drizzle streaks
    {
        const ColorRgba Ink = (TypeIndex == 0u) ? ColorRgba{ 138u, 152u, 162u, 92u } : ColorRgba{ 122u, 134u, 144u, 78u };
        const float Len = (TypeIndex == 0u) ? 4.5f : 3.0f;
        for (int Row = -1; Row <= 1; ++Row)
        {
            for (int Col = -1; Col <= 1; ++Col)
            {
                const float X = Center.X + static_cast<float>(Col) * 6.0f + static_cast<float>(Row) * 1.5f;
                const float Y = Center.Y + static_cast<float>(Row) * 6.5f;
                Surface.StrokeLine({ X, Y - Len * 0.5f }, { X - 1.0f, Y + Len * 0.5f }, Ink, 1.0f);
            }
        }
    }
    else if (TypeIndex == 2u) // Hail pellets
    {
        const std::array<Point2D, 6u> Dots = {{
            { -6.0f, -5.0f }, { 2.0f, -7.0f }, { 7.0f, -1.0f },
            { -2.0f,  1.0f }, { 4.0f,  5.0f }, { -5.0f, 6.0f }
        }};
        for (std::size_t I = 0u; I < Dots.size(); ++I)
        {
            const float R = (I % 2u == 0u) ? 2.4f : 1.7f;
            Surface.FillCircle({ Center.X + Dots[I].X, Center.Y + Dots[I].Y }, R, ColorRgba{ 212u, 224u, 236u, 235u });
        }
    }
    else // Snow / Sleet snowflakes
    {
        const ColorRgba Ink { 166u, 202u, 230u, 235u };
        for (int Arm = 0; Arm < 3; ++Arm)
        {
            const float A = static_cast<float>(Arm) * 1.0471975f;
            const float DX = std::cos(A) * 9.0f, DY = std::sin(A) * 9.0f;
            Surface.StrokeLine({ Center.X - DX, Center.Y - DY }, { Center.X + DX, Center.Y + DY }, Ink, 1.4f);
        }
        Surface.FillCircle(Center, 2.2f, ColorRgba{ 225u, 240u, 250u, 255u });
    }
}

} // namespace

float PrecipitationInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PrecipitationInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Particles", "Simulation" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        // Card 1: Precipitation type (h = 267, 3 + 2 grid of 5 type buttons)
        const Rectangle2D TypeCard { LeftX, CursorY, InnerWidth, 267.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, TypeCard, "Precipitation type");

        static constexpr std::array<const char*, 5u> TypeLabels = {{ "Rain", "Drizzle", "Hail", "Snow", "Sleet" }};
        const float Gap   = 8.0f;
        const float BtnW  = std::floor((BodyW - Gap * 2.0f) / 3.0f);
        const float BtnH  = 84.0f;
        for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
        {
            const uint32_t Col = Idx % 3u;
            const uint32_t Row = Idx / 3u;
            const Rectangle2D Btn {
                BodyX + static_cast<float>(Col) * (BtnW + Gap),
                TypeCard.Y + 64.0f + static_cast<float>(Row) * (BtnH + Gap),
                BtnW, BtnH
            };
            const bool Sel = (Instance.ModeIndex == Idx);
            Surface.FillRectangle(Btn, Sel ? ColorRgba{ 30u, 36u, 34u, 255u } : ColorRgba{ 28u, 30u, 30u, 255u }, 12.0f);
            Surface.StrokeRectangle(Btn, Sel ? ColorRgba{ 168u, 214u, 194u, 215u } : ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, Sel ? 1.2f : 1.0f);

            ConstructPrecipTypeGlyph(Surface, { Btn.Center().X, Btn.Y + 31.0f }, Idx);
            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.8f, TypeLabels[Idx]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { Btn.Center().X - LW * 0.5f, Btn.Bottom() - 22.0f }, 10.8f,
                Sel ? ColorRgba{ 234u, 236u, 234u, 255u } : ColorRgba{ 196u, 198u, 196u, 255u }, TypeLabels[Idx]);

            if (Btn.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                Instance.ModeIndex = Idx;
                OutMutated         = true;
            }
        }
        CursorY = TypeCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Quick controls (4 switches: Enabled, Follow Wind, Spawn from Clouds, Ground Collision)
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",           InspectorSwitchSymbol::Cloud, Entry.VisibilityEnabled, false },
            { "Follow Wind",       InspectorSwitchSymbol::Wind,  FollowWind,              false },
            { "Spawn from Clouds", InspectorSwitchSymbol::Cloud, SpawnFromClouds,         false },
            { "Ground Collision",  InspectorSwitchSymbol::Cube,  GroundCollision,         false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { FollowWind = !FollowWind; OutMutated = true; }
        else if (Clicked == 2) { SpawnFromClouds = !SpawnFromClouds; OutMutated = true; }
        else if (Clicked == 3) { GroundCollision = !GroundCollision; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "weather" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: PARTICLES — Card 1 Fall + density (h = 379), Card 2 Particle scale (h = 260)
        const Rectangle2D FallCard { LeftX, CursorY, InnerWidth, 379.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, FallCard, "Fall + density");
        float FY = FallCard.Y + 70.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6801u, "Intensity",     "mm/h",     "%.0f", 0.0f, 100.0f, IntensityMmPerHour, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6802u, "Density",       "\xC3\x97", "%.2g", 0.0f,   5.0f, DensityScale,       ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6803u, "Particle Size", "\xC3\x97", "%.2g", 0.2f,   3.0f, ParticleSizeScale,  ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6804u, "Wind Drift",   "",         "%.2g", 0.0f,   1.0f, WindDrift,          ActiveDragId, Input, OutMutated);
        Instance.RateMultiplier = DensityScale;
        Instance.DropSizeMm     = ParticleSizeScale * 3.0f;
        Instance.WindSlantScale = WindDrift;
        CursorY = FallCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D ScaleCard { LeftX, CursorY, InnerWidth, 260.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ScaleCard, "Particle scale");
        char DiamBuf[24]{};
        std::snprintf(DiamBuf, sizeof(DiamBuf), "%.2f", ParticleSizeScale * 3.0f);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, ScaleCard.Y + 65.0f, DiamBuf, "mm", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, ScaleCard.Y + 118.0f }, 11.4f, InspectorMetrics::MutedInk, "Nominal diameter \xC2\xB7 type radius \xC3\x97 size scale");
        CursorY = ScaleCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 3: SIMULATION — Simulation + settling (h = 269)
        const Rectangle2D SimCard { LeftX, CursorY, InnerWidth, 269.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SimCard, "Simulation + settling");
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SimCard.Y + 70.0f, BodyW, 6810u, "Accumulation", "", "%.2g", 0.0f, 1.0f, Accumulation, ActiveDragId, Input, OutMutated);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, SimCard.Y + 142.0f }, 11.5f, InspectorMetrics::MutedInk, "Particles");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 10.0f, SimCard.Y + 142.0f }, 11.5f, InspectorMetrics::MutedInk, "-");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, SimCard.Y + 170.0f }, 11.5f, InspectorMetrics::MutedInk, "Snow Depth");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 10.0f, SimCard.Y + 170.0f }, 11.5f, InspectorMetrics::MutedInk, "-");

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, SimCard.Y + 200.0f, BodyW, 11.8f, 16.5f, InspectorMetrics::MutedInk,
            "Native simulation disconnected in HTML. No live particle count is fabricated.");

        CursorY = SimCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
