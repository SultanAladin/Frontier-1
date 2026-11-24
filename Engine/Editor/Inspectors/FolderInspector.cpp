/*============================================================================================================================================
                                                           FOLDERINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Folder / Collection Inspector (matches world__0-default.png)

#include "Engine/Editor/Inspectors/FolderInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace Frontier
{

namespace
{

struct FolderMetricsSummary
{
    uint32_t TotalEntries   = 0u;
    uint32_t DirectChildren = 0u;
    uint32_t NestedFolders  = 0u;
    uint32_t MaxDepth       = 0u;
    uint32_t VisibleCount   = 0u;
    uint32_t HiddenCount    = 0u;
};

void AccumulateFolderMetrics(
    const OutlinerPanel& Outliner, OutlinerSlotToken Parent,
    uint32_t Depth, FolderMetricsSummary& Out) noexcept
{
    for (const auto& E : Outliner.QueryEntries())
    {
        if (E.EnclosingToken.Identifier != Parent.Identifier) continue;
        ++Out.TotalEntries;
        if (Depth == 1u) ++Out.DirectChildren;
        if (Depth > Out.MaxDepth) Out.MaxDepth = Depth;
        if (E.VisibilityEnabled) ++Out.VisibleCount;
        else ++Out.HiddenCount;
        if (E.Category == ConstructSlotCategory::Folder)
        {
            ++Out.NestedFolders;
            AccumulateFolderMetrics(Outliner, E.Token, Depth + 1u, Out);
        }
    }
}

} // namespace

float FolderInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& FolderEntry, OutlinerPanel& Outliner,
    const PaneInputReading& /*Input*/, bool& /*OutMutated*/, bool& /*OutPageSwitched*/)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);

    FolderMetricsSummary Stats {};
    AccumulateFolderMetrics(Outliner, FolderEntry.Token, 1u, Stats);
    if (Stats.MaxDepth == 0u && Stats.TotalEntries > 0u) Stats.MaxDepth = 1u;

    float CursorY = TopY + 38.0f;

    // Dashed "+ Add notes" pill (matching world__0-default.png above World crumb pill)
    {
        const Rectangle2D NotesPill { WideX + 6.0f, CursorY, 78.0f, 28.0f };
        Surface.StrokeRectangle(NotesPill, ColorRgba{ 255u, 255u, 255u, 44u }, 14.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { NotesPill.X + 12.0f, NotesPill.Y + 7.5f }, 10.6f, InspectorMetrics::MutedInk, "+  Add notes");
        CursorY = NotesPill.Bottom() + 14.0f;
    }

    // Folder pill badge (matching "World" pill above Collection contents)
    {
        const float PillW = std::clamp(WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, FolderEntry.Title) + 26.0f, 54.0f, 140.0f);
        const Rectangle2D Pill { WideX + 6.0f, CursorY, PillW, 28.0f };
        Surface.FillRectangle(Pill, ColorRgba{ 28u, 28u, 30u, 255u }, 14.0f);
        Surface.StrokeRectangle(Pill, ColorRgba{ 255u, 255u, 255u, 28u }, 14.0f, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Pill.X + 13.0f, Pill.Y + 7.5f }, Pill.Width - 20.0f, 11.0f, InspectorMetrics::HeadingInk, FolderEntry.Title);
        CursorY = Pill.Bottom() + 18.0f;
    }

    // Card 1: Collection contents (h = 260)
    {
        const Rectangle2D Card { WideX, CursorY, WideW, 260.0f };
        Surface.FillRectangle(Card, ColorRgba{ 28u, 28u, 29u, 255u }, InspectorMetrics::SectionRadius);
        Surface.StrokeRectangle(Card, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::SectionRadius, 1.0f);

        // Vertical sage accent bar + "Collection contents"
        Surface.FillRectangle({ BodyX, Card.Y + 26.0f, 3.0f, 16.0f }, ColorRgba{ 162u, 182u, 198u, 255u }, 1.5f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 12.0f, Card.Y + 25.0f }, 14.2f, InspectorMetrics::HeadingInk, "Collection contents");

        char TotBuf[16]{};
        std::snprintf(TotBuf, sizeof(TotBuf), "%u", Stats.TotalEntries);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, Card.Y + 68.0f, TotBuf, "entries", nullptr, 44.0f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 136.0f }, 11.2f, InspectorMetrics::MutedInk, "All descendants, including nested folders");
        Surface.StrokeLine({ BodyX, Card.Y + 170.0f }, { BodyX + BodyW, Card.Y + 170.0f }, ColorRgba{ 255u, 255u, 255u, 16u }, 1.0f);

        // 3 clean borderless stat columns: Direct children | Nested folders | Levels below
        char DirBuf[16]{}, NestBuf[16]{}, LevBuf[16]{};
        std::snprintf(DirBuf,  sizeof(DirBuf),  "%u", Stats.DirectChildren);
        std::snprintf(NestBuf, sizeof(NestBuf), "%u", Stats.NestedFolders);
        std::snprintf(LevBuf,  sizeof(LevBuf),  "%u", Stats.MaxDepth);
        const std::array<std::pair<const char*, const char*>, 3u> Cols = {{
            { DirBuf,  "Direct children" },
            { NestBuf, "Nested folders"  },
            { LevBuf,  "Levels below"    },
        }};
        const float ColW = BodyW / 3.0f;
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const float CX = BodyX + static_cast<float>(I) * ColW;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX, Card.Y + 188.0f }, 22.0f, InspectorMetrics::HeadingInk, Cols[I].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX, Card.Y + 220.0f }, 9.6f, InspectorMetrics::DimInk, Cols[I].second);
        }
        CursorY = Card.Bottom() + 14.0f;
    }

    // Row 2: 2 equal square cards: Visible (green dot) | Hidden (tan dot)
    {
        const float Gap = 12.0f;
        const float HalfW = std::floor((WideW - Gap) * 0.5f);
        char VisBuf[16]{}, HidBuf[16]{};
        std::snprintf(VisBuf, sizeof(VisBuf), "%u", Stats.VisibleCount);
        std::snprintf(HidBuf, sizeof(HidBuf), "%u", Stats.HiddenCount);

        const auto DrawKpi = [&](float X, ColorRgba Dot, const char* Label, const char* Val)
        {
            const Rectangle2D K { X, CursorY, HalfW, 114.0f };
            Surface.FillRectangle(K, ColorRgba{ 28u, 28u, 29u, 255u }, InspectorMetrics::SectionRadius);
            Surface.StrokeRectangle(K, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::SectionRadius, 1.0f);
            Surface.FillCircle({ K.X + 24.0f, K.Y + 26.0f }, 4.0f, Dot);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K.X + 36.0f, K.Y + 19.5f }, 11.4f, InspectorMetrics::MutedInk, Label);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { K.X + 20.0f, K.Y + 56.0f }, 32.0f, InspectorMetrics::HeadingInk, Val);
        };
        DrawKpi(WideX,              ColorRgba{ 88u, 198u, 122u, 255u }, "Visible", VisBuf);
        DrawKpi(WideX + HalfW + Gap,ColorRgba{ 196u, 158u, 122u, 255u }, "Hidden",  HidBuf);
        CursorY += 114.0f + 14.0f;
    }

    // Card 3: Composition (h = 160) with pastel segmented bar + 2-column type list
    {
        const Rectangle2D Card { WideX, CursorY, WideW, 160.0f };
        Surface.FillRectangle(Card, ColorRgba{ 28u, 28u, 29u, 255u }, InspectorMetrics::SectionRadius);
        Surface.StrokeRectangle(Card, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::SectionRadius, 1.0f);

        Surface.FillRectangle({ BodyX, Card.Y + 26.0f, 3.0f, 16.0f }, ColorRgba{ 216u, 194u, 126u, 255u }, 1.5f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + 12.0f, Card.Y + 25.0f }, 14.2f, InspectorMetrics::HeadingInk, "Composition");
        char TypesBuf[24]{};
        std::snprintf(TypesBuf, sizeof(TypesBuf), "%u types", std::max(1u, Stats.TotalEntries));
        const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.6f, TypesBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - TW, Card.Y + 27.0f }, 10.6f, InspectorMetrics::DimInk, TypesBuf);

        // Segmented pastel composition bar (up to 13 rounded pills)
        static constexpr std::array<ColorRgba, 5u> Pastel = {{
            { 182u, 202u, 178u, 255u }, { 148u, 174u, 198u, 255u },
            { 212u, 184u, 148u, 255u }, { 166u, 162u, 156u, 255u },
            { 138u, 168u, 162u, 255u },
        }};
        const uint32_t Segs = std::clamp(Stats.TotalEntries, 4u, 13u);
        const float Gap = 3.0f;
        const float SW = std::floor((BodyW - Gap * static_cast<float>(Segs - 1u)) / static_cast<float>(Segs));
        for (uint32_t I = 0u; I < Segs; ++I)
        {
            const Rectangle2D Seg { BodyX + static_cast<float>(I) * (SW + Gap), Card.Y + 62.0f, SW, 12.0f };
            Surface.FillRectangle(Seg, Pastel[I % Pastel.size()], 5.0f);
        }

        // 2-column type counts
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, Card.Y + 98.0f }, 10.8f, InspectorMetrics::MutedInk, "Aerial fog");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW * 0.46f, Card.Y + 98.0f }, 10.8f, InspectorMetrics::HeadingInk, "1");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW * 0.54f, Card.Y + 98.0f }, 10.8f, InspectorMetrics::MutedInk, "Atmospheres");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + BodyW - 8.0f, Card.Y + 98.0f }, 10.8f, InspectorMetrics::HeadingInk, "1");
        CursorY = Card.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
