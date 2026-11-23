/*============================================================================================================================================
                                                           FOLDERINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone generic FolderInspector for any Outliner container (never hardcoded to a single folder name)

#include "Engine/Editor/Inspectors/FolderInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <array>
#include <cstdio>

namespace Frontier
{

float FolderInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& FolderEntry, OutlinerPanel& Outliner,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Contents" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const uint32_t DirectCount = Outliner.CountDirectNested(FolderEntry.Token);
    const uint32_t TotalCount  = Outliner.CountAllNested(FolderEntry.Token);
    const uint32_t FolderCount = Outliner.CountNestedFolders(FolderEntry.Token);

    if (ActivePage == 0u)
    {
        char DirBuf[16]{}, TotBuf[16]{}, SubBuf[16]{}, VisBuf[16]{};
        std::snprintf(DirBuf, sizeof(DirBuf), "%u", DirectCount);
        std::snprintf(TotBuf, sizeof(TotBuf), "%u", TotalCount);
        std::snprintf(SubBuf, sizeof(SubBuf), "%u", FolderCount);
        std::snprintf(VisBuf, sizeof(VisBuf), "%s", FolderEntry.VisibilityEnabled ? "ON" : "OFF");
        const std::array<InspectorKpiMetricSpec, 4u> Kpis = {{
            { DirBuf, "", "DIRECT" },
            { TotBuf, "", "TOTAL" },
            { SubBuf, "", "FOLDERS" },
            { VisBuf, "", "VISIBILITY" },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Kpis);

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Visible",  InspectorSwitchSymbol::Eye,  FolderEntry.VisibilityEnabled, false },
            { "Expanded", InspectorSwitchSymbol::Cube, FolderEntry.Expanded,          false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { FolderEntry.VisibilityEnabled = !FolderEntry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { FolderEntry.Expanded = !FolderEntry.Expanded; OutMutated = true; }

        // Folder accent colour strip
        const Rectangle2D SwatchSec { LeftX, CursorY, InnerWidth, 94.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SwatchSec, "Folder accent");
        static constexpr std::array<ColorRgba, 6u> Swatches = {{
            WorkspaceSurfaceColors::ShowcaseSage,
            ColorRgba{ 126u, 174u, 232u, 255u },
            ColorRgba{ 232u, 178u, 108u, 255u },
            ColorRgba{ 196u, 148u, 232u, 255u },
            ColorRgba{ 226u, 128u, 134u, 255u },
            ColorRgba{ 168u, 172u, 178u, 255u },
        }};
        const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        for (std::size_t I = 0u; I < Swatches.size(); ++I)
        {
            const Point2D Ctr { LeftX + PadX + 12.0f + static_cast<float>(I) * 36.0f, CursorY + 62.0f };
            Surface.FillCircle(Ctr, 11.0f, Swatches[I]);
            if (FolderEntry.FolderTint.Red == Swatches[I].Red && FolderEntry.FolderTint.Green == Swatches[I].Green && FolderEntry.FolderTint.Blue == Swatches[I].Blue)
            {
                Surface.StrokeCircle(Ctr, 13.5f, ColorRgba{ 244u, 244u, 246u, 255u }, 1.5f);
            }
            const float DX = Cursor.X - Ctr.X, DY = Cursor.Y - Ctr.Y;
            if (DX * DX + DY * DY <= 144.0f && Input.PrimaryPointer.Pressed)
            {
                FolderEntry.FolderTint = Swatches[I];
                OutMutated = true;
            }
        }
        CursorY = SwatchSec.Bottom() + InspectorMetrics::SectionGap;

        static constexpr std::array<const char*, 2u> Tags = {{ "folder", "container" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, FolderEntry.AuthorNotes);
    }
    else
    {
        uint32_t Listed = 0u;
        for (const auto& Entry : Outliner.QueryEntries())
        {
            if (Entry.EnclosingToken == FolderEntry.Token) ++Listed;
        }
        const float RowH = 28.0f;
        const float SecH = InspectorMetrics::SectionPadTop + 34.0f + std::max(1u, Listed) * RowH + InspectorMetrics::SectionPadBottom;
        const Rectangle2D S { LeftX, CursorY, InnerWidth, SecH };
        char CountBuf[20]{}; std::snprintf(CountBuf, sizeof(CountBuf), "%u", Listed);
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Contained entries", CountBuf);

        float Y = CursorY + InspectorMetrics::SectionPadTop + 30.0f;
        if (Listed == 0u)
        {
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y + 4.0f }, BodyW, 11.0f, InspectorMetrics::MutedInk, "Folder is empty.");
        }
        else
        {
            const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
            for (const auto& Entry : Outliner.QueryEntries())
            {
                if (Entry.EnclosingToken != FolderEntry.Token) continue;
                const Rectangle2D RowRect { LeftX + PadX, Y, BodyW, 24.0f };
                Surface.FillRectangle(RowRect, ColorRgba{ 24u, 24u, 27u, 255u }, 8.0f);
                WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { RowRect.X + 10.0f, RowRect.Y + 5.5f }, BodyW - 44.0f, 11.0f, ColorRgba{ 230u, 230u, 234u, 255u }, Entry.Title.c_str());
                const char* VisTxt = Entry.VisibilityEnabled ? "ON" : "OFF";
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { RowRect.Right() - 28.0f, RowRect.Y + 6.5f }, 9.5f, Entry.VisibilityEnabled ? InspectorMetrics::QuickActiveGreen : InspectorMetrics::DimInk, VisTxt);
                if (RowRect.Contains(Cursor) && Input.PrimaryPointer.Pressed)
                {
                    Outliner.SelectSingle(Entry.Token);
                    break;
                }
                Y += RowH;
            }
        }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
