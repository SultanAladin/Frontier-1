/*============================================================================================================================================
                                                           UVOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVOutlinerPanel matching Experimental/UVEditor (KPI trio, stat row, 0px layer boxes & UDIM Space Budget spectrum)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVOutlinerPanel.h"

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlTooltip.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"
#include "Engine/Workspaces/UV/UVPaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty())
    {
        return true;
    }
    if (Haystack.size() < Needle.size())
    {
        return false;
    }
    for (std::size_t Start = 0u; Start + Needle.size() <= Haystack.size(); ++Start)
    {
        bool Match = true;
        for (std::size_t Idx = 0u; Idx < Needle.size(); ++Idx)
        {
            const unsigned char A = static_cast<unsigned char>(Haystack[Start + Idx]);
            const unsigned char B = static_cast<unsigned char>(Needle[Idx]);
            if (std::tolower(A) != std::tolower(B))
            {
                Match = false;
                break;
            }
        }
        if (Match)
        {
            return true;
        }
    }
    return false;
}

} // namespace

UVOutlinerPanel::~UVOutlinerPanel() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void UVOutlinerPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session               = TargetSession;
    UDIMFilterCode        = 0u;
    UDIMFilterDropdownOpen = false;
    ScrollOffsetY         = 0.0f;
    SearchEditBuffer      = {};
    RenameEditBuffer      = {};
    RenamingLayerOrd      = 0u;
    ColorPickerLayerOrdinal = 0u;
    CachedNetworkRevision = UINT64_MAX;
    CachedUVRevision      = UINT64_MAX;
    CachedLayerAssignmentSignature = UINT64_MAX;
}

void UVOutlinerPanel::RefreshCachedAtlasTelemetry()
{
    if (Session == nullptr)
    {
        return;
    }

    const auto& Net = Session->Asset.Network;
    uint64_t AssignmentSignature = 1469598103934665603ull;
    for (const auto& Face : Net.Faces)
    {
        AssignmentSignature ^= Face.PriorityLayerOrdinal;
        AssignmentSignature *= 1099511628211ull;
    }
    const bool AssignmentChanged = CachedLayerAssignmentSignature != AssignmentSignature;
    if (CachedNetworkRevision != Net.NetworkRevision || CachedUVRevision != Net.UVRevision || AssignmentChanged)
    {
        CachedNetworkRevision = Net.NetworkRevision;
        CachedUVRevision      = Net.UVRevision;
        CachedLayerAssignmentSignature = AssignmentSignature;

        const auto Islands = SeamCutGenerator::PartitionConnectedUVIslands(Net);
        CachedIslandCount = static_cast<uint32_t>(Islands.size());
        CachedFaceIslandOrdinal.assign(Net.Faces.size(), UINT32_MAX);
        CachedIslandUDIMNumber.clear();
        CachedIslandLayerOrdinal.clear();
        CachedIslandUVArea.clear();
        CachedIslandUDIMNumber.reserve(Islands.size());
        CachedIslandLayerOrdinal.reserve(Islands.size());
        CachedIslandUVArea.reserve(Islands.size());
        for (const UVIslandPartition& Island : Islands)
        {
            CachedIslandUDIMNumber.push_back(Island.AssignedUDIMNumber);
            CachedIslandLayerOrdinal.push_back(Island.PriorityLayerOrdinal);
            CachedIslandUVArea.push_back(Island.TotalAreaUV);
            for (const uint32_t FaceIndex : Island.FaceIndices)
            {
                if (FaceIndex < CachedFaceIslandOrdinal.size())
                {
                    CachedFaceIslandOrdinal[FaceIndex] = Island.IslandOrdinal;
                }
            }
        }
    }

    uint32_t CautionFaces = 0u;
    const auto& StretchVec = Session->LatestMetrics.PerFaceStretch;
    if (!StretchVec.empty())
    {
        for (const float S : StretchVec)
        {
            if (S > 1.38f || S < 0.68f)
            {
                ++CautionFaces;
            }
        }
        CachedStretchCautionPct = (100.0f * static_cast<float>(CautionFaces)) / static_cast<float>(StretchVec.size());
    }
    else
    {
        CachedStretchCautionPct = 0.0f;
    }
}

void UVOutlinerPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PanelBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    RenderedLayerSlotBounds.clear();
    const uint64_t TickSequence = (ImGui::GetCurrentContext() != nullptr)
        ? (static_cast<uint64_t>(ImGui::GetFrameCount()) + 1ull)
        : ++HeadlessTickSequence;
    {
        bool WantsUndo = false;
        bool WantsRedo = false;
        ResolveHistoryShortcuts(Input, WantsUndo, WantsRedo);
        (void)Session->TryConsumeGlobalHistoryShortcut(
            WantsUndo, WantsRedo, SearchEditBuffer.Active, TickSequence);
    }

    RefreshCachedAtlasTelemetry();

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    if (Input.EscapePressed)
    {
        ColorPickerLayerOrdinal = 0u;
        UDIMFilterDropdownOpen = false;
    }

    const uint32_t DisplayTile = UDIMFilterCode >= 1001u ? UDIMFilterCode : Session->ActiveUDIMNumber;
    double TileAreaUV = 0.0;
    for (std::size_t IslandIndex = 0u; IslandIndex < CachedIslandUDIMNumber.size(); ++IslandIndex)
    {
        if (CachedIslandUDIMNumber[IslandIndex] == DisplayTile && IslandIndex < CachedIslandUVArea.size())
        {
            TileAreaUV += CachedIslandUVArea[IslandIndex];
        }
    }
    CachedUtilizationPct = static_cast<float>(std::clamp(TileAreaUV * 100.0, 0.0, 100.0));

    const auto SelectedFaces = Session->SelectionMask.CollectSelectedFaces();
    std::vector<uint8_t> SelectedIslandMask(CachedIslandCount, 0u);
    for (const uint32_t FaceIndex : SelectedFaces)
    {
        if (FaceIndex < CachedFaceIslandOrdinal.size())
        {
            const uint32_t IslandOrdinal = CachedFaceIslandOrdinal[FaceIndex];
            if (IslandOrdinal < SelectedIslandMask.size())
            {
                SelectedIslandMask[IslandOrdinal] = 1u;
            }
        }
    }
    const uint32_t SelectedIslandCount = static_cast<uint32_t>(
        std::count(SelectedIslandMask.begin(), SelectedIslandMask.end(), static_cast<uint8_t>(1u)));

    const float PadX   = 14.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = std::max(140.0f, Bounds.Width - PadX * 2.0f);
    float       CurY   = Bounds.Y + 14.0f;

    // 1. Pane Header (.pz-pane-head): "UV Space" + "<I> total islands · <U> UDIM" + 26x26 sharp 0px '+' button
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY },
        15.5f,
        Theme.TextPrimary,
        "UV Space");

    char SubHeadBuf[96]{};
    std::snprintf(
        SubHeadBuf,
        sizeof(SubHeadBuf),
        "%u total islands · %zu UDIM",
        CachedIslandCount,
        std::max<std::size_t>(1u, Session->Asset.UDIMTiles.size()));
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY + 20.0f },
        9.5f,
        Theme.TextMuted,
        SubHeadBuf);

    const Rectangle2D PlusBtn{ LeftX + InnerW - 26.0f, CurY + 2.0f, 26.0f, 26.0f };
    {
        const bool PlusHover = PlusBtn.Contains(Cursor);
        if (PlusHover)
        {
            ControlAppearance::RecordHoveredTooltip(
                PlusBtn, OptionalTooltip::FromText("Create a priority layer and bind the current selection when the remainder is active."));
        }
        // Strictly 0px sharp rectangular button (.pz-outliner-plus-btn)
        Surface.FillRectangle(PlusBtn, PlusHover ? Theme.HoverBg : Theme.SectionBg, 0.0f);
        Surface.StrokeRectangle(PlusBtn, PlusHover ? Theme.AccentBorder : Theme.BorderStrong, 0.0f, 1.0f);
        const float CX = PlusBtn.X + 13.0f;
        const float CY = PlusBtn.Y + 13.0f;
        Surface.StrokeLine({ CX - 5.0f, CY }, { CX + 5.0f, CY }, Theme.TextPrimary, 1.4f);
        Surface.StrokeLine({ CX, CY - 5.0f }, { CX, CY + 5.0f }, Theme.TextPrimary, 1.4f);

        if (PlusHover && Pressed)
        {
            static constexpr std::array<Mathematics::Scalar3D, 5u> Swatches {{
                { 0.608f, 0.890f, 0.769f },
                { 0.949f, 0.757f, 0.490f },
                { 0.898f, 0.596f, 0.608f },
                { 0.769f, 0.655f, 0.906f },
                { 0.620f, 0.812f, 1.000f },
            }};
            char NewTitle[48]{};
            std::snprintf(NewTitle, sizeof(NewTitle), "Priority Layer %u", CreatedLayerCount);
            const auto& Tint = Swatches[(CreatedLayerCount - 1u) % Swatches.size()];
            ++CreatedLayerCount;
            (void)Session->CreatePriorityLayer(NewTitle, Tint, 10.0f);
        }
    }
    CurY += 40.0f;

    // 2. Search Bar (.pz-search-row: 38px height, 19px capsules radius)
    const Rectangle2D SearchRect{ LeftX, CurY, InnerW, 38.0f };
    {
        if (Input.ShortcutFind)
        {
            SearchEditBuffer.Active = true;
            SearchEditBuffer.Caret = SearchEditBuffer.Length;
            SearchEditBuffer.SelectionAnchor = 0u;
        }
        if (Pressed)
        {
            if (SearchRect.Contains(Cursor))
            {
                SearchEditBuffer.Active = true;
                SearchEditBuffer.Caret = SearchEditBuffer.Length;
                SearchEditBuffer.SelectionAnchor = SearchEditBuffer.Caret;
            }
            else if (SearchEditBuffer.Active)
            {
                SearchEditBuffer.Finalize();
            }
        }
        WorkspaceAppearance::ForwardInputToTextBuffer(SearchEditBuffer, Input, Δt);
        if (Input.EscapePressed && SearchEditBuffer.Active)
        {
            SearchEditBuffer.Cancel();
        }
        else if (Input.EnterPressed && SearchEditBuffer.Active)
        {
            SearchEditBuffer.Finalize();
        }

        if (SearchRect.Contains(Cursor) && !SearchEditBuffer.Active)
        {
            ControlAppearance::RecordHoveredTooltip(
                SearchRect, OptionalTooltip::FromText("Search layers by name. Press Ctrl+F to focus."));
        }
        Surface.FillRectangle(SearchRect, Theme.WellBg, 19.0f);
        Surface.StrokeRectangle(SearchRect, SearchEditBuffer.Active ? Theme.AccentBorder : Theme.BorderSubtle, 19.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::SearchGlass,
            VectorIconPlacement{ SearchRect.X + 12.0f, SearchRect.Y + 11.5f, 14.5f, Theme.TextMuted, 1.8f });

        if (SearchEditBuffer.Length == 0u && !SearchEditBuffer.Active)
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { SearchRect.X + 33.0f, SearchRect.Y + 11.5f },
                10.5f,
                Theme.TextMuted,
                "Search layers");
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { SearchRect.Right() - 42.0f, SearchRect.Y + 12.5f },
                9.0f,
                Theme.TextFaint,
                "Ctrl+F");
        }
        else
        {
            const std::string_view QueryView(SearchEditBuffer.Text, SearchEditBuffer.Length);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { SearchRect.X + 33.0f, SearchRect.Y + 11.5f },
                SearchRect.Width - 58.0f,
                10.5f,
                Theme.TextPrimary,
                QueryView);

            if (SearchEditBuffer.Length > 0u)
            {
                const Rectangle2D ClearHit{ SearchRect.Right() - 25.0f, SearchRect.Y + 9.0f, 18.0f, 18.0f };
                VectorIconRepository::ConstructControl(
                    Surface,
                    ControlIcon::CloseCross,
                    VectorIconPlacement{ ClearHit.X + 3.0f, ClearHit.Y + 3.0f, 11.5f, Theme.TextMuted, 1.7f });
                if (ClearHit.Contains(Cursor) && Pressed)
                {
                    SearchEditBuffer.Length          = 0u;
                    SearchEditBuffer.Caret           = 0u;
                    SearchEditBuffer.SelectionAnchor = 0u;
                    SearchEditBuffer.Text[0]         = '\0';
                }
            }
        }
    }
    CurY += 46.0f;

    // 3. UDIM filter uses the shared DropdownSelector control used throughout the workspace shell.
    Rectangle2D UDIMFilterDropdownBounds{};
    std::vector<uint32_t> UDIMFilterOptions{ 0u };
    for (const uint32_t TileCode : Session->Asset.UDIMTiles)
    {
        if (TileCode >= 1001u && std::find(UDIMFilterOptions.begin(), UDIMFilterOptions.end(), TileCode) == UDIMFilterOptions.end())
        {
            UDIMFilterOptions.push_back(TileCode);
        }
    }
    if (Session->ActiveUDIMNumber >= 1001u &&
        std::find(UDIMFilterOptions.begin(), UDIMFilterOptions.end(), Session->ActiveUDIMNumber) == UDIMFilterOptions.end())
    {
        UDIMFilterOptions.push_back(Session->ActiveUDIMNumber);
    }
    std::sort(UDIMFilterOptions.begin() + 1, UDIMFilterOptions.end());

    std::vector<std::string> UDIMFilterLabelStorage{};
    std::vector<const char*> UDIMFilterLabels{};
    UDIMFilterLabelStorage.reserve(UDIMFilterOptions.size());
    UDIMFilterLabels.reserve(UDIMFilterOptions.size());
    for (const uint32_t TileCode : UDIMFilterOptions)
    {
        UDIMFilterLabelStorage.emplace_back(TileCode == 0u ? "All UDIMs" : "UDIM " + std::to_string(TileCode));
    }
    for (const std::string& Label : UDIMFilterLabelStorage)
    {
        UDIMFilterLabels.push_back(Label.c_str());
    }
    const auto CurrentFilter = std::find(UDIMFilterOptions.begin(), UDIMFilterOptions.end(), UDIMFilterCode);
    const uint32_t CurrentFilterIndex = CurrentFilter == UDIMFilterOptions.end()
        ? 0u
        : static_cast<uint32_t>(std::distance(UDIMFilterOptions.begin(), CurrentFilter));
    if (CurrentFilter == UDIMFilterOptions.end())
    {
        UDIMFilterCode = 0u;
    }
    UDIMFilterDropdownBounds = { LeftX, CurY, InnerW, 31.0f };
    const ControlContact FilterContact = DropdownSelector::ConstructButton(
        Surface,
        UDIMFilterDropdownBounds,
        UDIMFilterLabels[CurrentFilterIndex],
        UDIMFilterDropdownOpen,
        Input.PrimaryPointer,
        1.0f,
        ControlIcon::LayoutGrid,
        OptionalTooltip::FromTitled("UDIM Filter", "Filter layer rows, face counts, and allocation metrics by UDIM tile."));
    if (FilterContact.Clicked)
    {
        UDIMFilterDropdownOpen = !UDIMFilterDropdownOpen;
    }
    CurY += 39.0f;

    const auto LayerSpan = Session->Asset.Layers.InspectLayers();
    const std::string_view FilterQuery(SearchEditBuffer.Text, SearchEditBuffer.Length);
    std::vector<uint16_t> VisibleLayerOrdinals{};
    VisibleLayerOrdinals.reserve(LayerSpan.size());
    for (const auto& Layer : LayerSpan)
    {
        const auto& PartRef = Session->Asset.Layers.InspectPartitionSpan(
            Session->Asset.Network, Layer.LayerOrdinal);
        const bool TileMatches = UDIMFilterCode == 0u || Layer.TargetUDIMNumber == UDIMFilterCode ||
            std::find(PartRef.UDIMNumbers.begin(), PartRef.UDIMNumbers.end(), UDIMFilterCode) != PartRef.UDIMNumbers.end();
        if (TileMatches && ContainsCaseInsensitive(Layer.LayerTitle, FilterQuery))
        {
            VisibleLayerOrdinals.push_back(Layer.LayerOrdinal);
        }
    }

    const auto& RemainderPartition = Session->Asset.Layers.InspectPartitionSpan(Session->Asset.Network, 0u);
    std::vector<uint32_t> RemainderFaces{};
    RemainderFaces.reserve(RemainderPartition.FaceIndices.size());
    for (const uint32_t FaceIndex : RemainderPartition.FaceIndices)
    {
        if (UDIMFilterCode == 0u || Session->Asset.Network.DeriveFaceUDIMNumber(FaceIndex) == UDIMFilterCode)
        {
            RemainderFaces.push_back(FaceIndex);
        }
    }
    const bool ShowRemainderBox = !RemainderFaces.empty() || LayerSpan.empty() ||
        Session->Asset.Layers.DeriveExplicitShareSum() < 99.999f;

    // Reserve sticky bottom footer (.pz-outliner-budget-footer: 54px)
    constexpr float   FooterH = 54.0f;
    const Rectangle2D ScrollArea{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - FooterH - CurY)
    };

    const float BodyContentHeight = 158.0f + static_cast<float>(VisibleLayerOrdinals.size()) * 42.0f +
        (ShowRemainderBox ? 44.0f : 0.0f);
    const float MaxScroll = std::max(0.0f, BodyContentHeight - ScrollArea.Height);
    if (ScrollArea.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 28.0f;
    }
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScroll);

    Surface.PushClip(ScrollArea);
    float BodyY = ScrollArea.Y - ScrollOffsetY + 2.0f;

    constexpr std::array<Mathematics::Scalar3D, 6u> LayerColorPalette {{
        { 0.937f, 0.957f, 0.812f }, { 0.620f, 0.812f, 1.000f }, { 0.608f, 0.890f, 0.769f },
        { 0.949f, 0.757f, 0.490f }, { 0.898f, 0.596f, 0.608f }, { 0.769f, 0.655f, 0.906f },
    }};
    constexpr std::array<const char*, 6u> LayerColorNames {{ "Studio Ivory", "Sky", "Mint", "Amber", "Coral", "Lavender" }};
    Rectangle2D ColorPaletteBounds{};
    int32_t ColorPaletteLayerRow = -1;
    bool ColorPaletteConsumedPress = false;
    if (ColorPickerLayerOrdinal != 0u)
    {
        const auto Found = std::find(VisibleLayerOrdinals.begin(), VisibleLayerOrdinals.end(), ColorPickerLayerOrdinal);
        if (Found == VisibleLayerOrdinals.end())
        {
            ColorPickerLayerOrdinal = 0u;
        }
        else
        {
            ColorPaletteLayerRow = static_cast<int32_t>(std::distance(VisibleLayerOrdinals.begin(), Found));
            const float LayerY = ScrollArea.Y - ScrollOffsetY + 160.0f + static_cast<float>(ColorPaletteLayerRow) * 42.0f;
            const float PaletteW = std::min(152.0f, InnerW);
            const float PaletteX = LeftX + std::min(28.0f, std::max(0.0f, InnerW - PaletteW));
            ColorPaletteBounds = { PaletteX, LayerY + 42.0f, PaletteW, 30.0f };
            if (ColorPaletteBounds.Bottom() > ScrollArea.Bottom())
            {
                ColorPaletteBounds.Y = LayerY - ColorPaletteBounds.Height - 4.0f;
            }
            ColorPaletteConsumedPress = Pressed && ColorPaletteBounds.Contains(Cursor);
            if (Pressed && !ColorPaletteBounds.Contains(Cursor))
            {
                const Rectangle2D SwatchButton{ LeftX + 10.0f, LayerY + 13.5f, 12.0f, 12.0f };
                if (!SwatchButton.Contains(Cursor))
                {
                    ColorPickerLayerOrdinal = 0u;
                    ColorPaletteLayerRow = -1;
                }
            }
            else if (Pressed)
            {
                for (uint32_t Slot = 0u; Slot < LayerColorPalette.size(); ++Slot)
                {
                    const Rectangle2D SwatchHit{
                        ColorPaletteBounds.X + 7.0f + static_cast<float>(Slot) * 23.0f,
                        ColorPaletteBounds.Y + 7.0f, 18.0f, 16.0f };
                    if (SwatchHit.Contains(Cursor))
                    {
                        ColorPaletteConsumedPress = true;
                        Session->AssignLayerTintByOrdinal(ColorPickerLayerOrdinal, LayerColorPalette[Slot], true);
                        ColorPickerLayerOrdinal = 0u;
                        ColorPaletteLayerRow = -1;
                        break;
                    }
                }
            }
        }
    }

    // 4a. 3-Column KPI Trio (.pz-left-kpi-trio: Utilization | Selected | Remaining)
    {
        const float KpiGap   = 7.0f;
        const float KpiCellW = (InnerW - KpiGap * 2.0f) / 3.0f;
        const float KpiCellH = 60.0f;

        char UtilBuf[24]{}, UtilSubBuf[32]{}, SelBuf[32]{}, SelSubBuf[32]{}, RemBuf[32]{};
        const uint32_t RemainderIslandCount = static_cast<uint32_t>(
            std::count(CachedIslandLayerOrdinal.begin(), CachedIslandLayerOrdinal.end(), static_cast<uint16_t>(0u)));
        std::snprintf(UtilBuf, sizeof(UtilBuf), "%.1f%%", CachedUtilizationPct);
        std::snprintf(UtilSubBuf, sizeof(UtilSubBuf), "UDIM %u", DisplayTile);
        std::snprintf(SelBuf, sizeof(SelBuf), "%u / %u", SelectedIslandCount, CachedIslandCount);
        std::snprintf(SelSubBuf, sizeof(SelSubBuf), "%s", SelectedIslandCount > 0u ? "Selected" : "Click islands");
        std::snprintf(RemBuf, sizeof(RemBuf), "%u / %u", RemainderIslandCount, CachedIslandCount);

        const ColorRgba UtilInk = Theme.LightSurface ? Theme.AccentFill : ColorRgba{ 239, 244, 207, 255 };
        auto ConstructKpiCell = [&](uint32_t Idx, const char* TopLabel, const char* MainReading, const char* SubLabel, ColorRgba ValInk, bool Highlighted)
        {
            const Rectangle2D CellRect{
                LeftX + (KpiCellW + KpiGap) * static_cast<float>(Idx),
                BodyY,
                KpiCellW,
                KpiCellH
            };
            Surface.FillRectangle(CellRect, Highlighted ? Theme.AccentSoft : Theme.SectionBg, 14.0f);
            Surface.StrokeRectangle(CellRect, Highlighted ? Theme.AccentBorder : Theme.BorderSubtle, 14.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Medium,
                { CellRect.X + 9.0f, CellRect.Y + 7.5f }, KpiCellW - 14.0f, 7.8f, Theme.TextMuted, TopLabel);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Medium,
                { CellRect.X + 9.0f, CellRect.Y + 21.5f }, KpiCellW - 14.0f, 13.0f, ValInk, MainReading);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular,
                { CellRect.X + 9.0f, CellRect.Y + 41.5f }, KpiCellW - 14.0f, 8.2f, Theme.TextMuted, SubLabel);
        };
        ConstructKpiCell(0u, "UTILIZATION", UtilBuf, UtilSubBuf, UtilInk, true);
        ConstructKpiCell(1u, "SELECTED",    SelBuf,  SelSubBuf,  Theme.TextPrimary, false);
        ConstructKpiCell(2u, "REMAINING",   RemBuf,  "Auto-fill", Theme.TextPrimary, false);
        BodyY += 68.0f;
    }

    // 4b. 2-Column Stat Row (.pz-stat-row: Stretch Caution & Overlaps)
    {
        const float       Gap  = 8.0f;
        const float       BoxW = (InnerW - Gap) * 0.5f;
        const Rectangle2D StretchBox{ LeftX, BodyY, BoxW, 54.0f };
        const Rectangle2D OverlapBox{ StretchBox.Right() + Gap, BodyY, BoxW, 54.0f };

        Surface.FillRectangle(StretchBox, Theme.SectionBg, 16.0f);
        Surface.StrokeRectangle(StretchBox, Theme.BorderSubtle, 16.0f, 1.0f);
        Surface.FillCircle({ StretchBox.X + 22.0f, StretchBox.Y + 27.0f }, 12.0f, Theme.CautionSoft);
        Surface.StrokeCircle({ StretchBox.X + 22.0f, StretchBox.Y + 27.0f }, 12.0f, Theme.CautionInk, 1.1f);
        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::TriangleAlert,
            VectorIconPlacement{ StretchBox.X + 15.5f, StretchBox.Y + 20.5f, 13.0f, Theme.CautionInk, 1.7f });

        char StretchVal[24]{};
        std::snprintf(StretchVal, sizeof(StretchVal), "%.1f%%", CachedStretchCautionPct);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { StretchBox.X + 41.0f, StretchBox.Y + 11.0f }, 13.0f, Theme.TextPrimary, StretchVal);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { StretchBox.X + 41.0f, StretchBox.Y + 30.0f }, BoxW - 46.0f, 8.8f, Theme.TextMuted, "Stretch Caution");

        Surface.FillRectangle(OverlapBox, Theme.SectionBg, 16.0f);
        Surface.StrokeRectangle(OverlapBox, Theme.BorderSubtle, 16.0f, 1.0f);
        const bool      ZeroOverlaps = (Session->LatestMetrics.OverlapFaceCount == 0u);
        const ColorRgba OvRing       = ZeroOverlaps ? Theme.ReadyGreen : Theme.DangerInk;
        const ColorRgba OvSoft       = ZeroOverlaps ? ColorRgba{ 121, 189, 143, 34 } : Theme.DangerSoft;
        Surface.FillCircle({ OverlapBox.X + 22.0f, OverlapBox.Y + 27.0f }, 12.0f, OvSoft);
        Surface.StrokeCircle({ OverlapBox.X + 22.0f, OverlapBox.Y + 27.0f }, 12.0f, OvRing, 1.1f);
        VectorIconRepository::ConstructControl(
            Surface,
            ZeroOverlaps ? ControlIcon::CircleCheck : ControlIcon::OctagonAlert,
            VectorIconPlacement{ OverlapBox.X + 15.5f, OverlapBox.Y + 20.5f, 13.0f, OvRing, 1.7f });

        char OvVal[24]{};
        std::snprintf(OvVal, sizeof(OvVal), "%u", Session->LatestMetrics.OverlapFaceCount);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { OverlapBox.X + 41.0f, OverlapBox.Y + 11.0f }, 13.0f, Theme.TextPrimary, OvVal);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { OverlapBox.X + 41.0f, OverlapBox.Y + 30.0f }, BoxW - 46.0f, 8.8f, Theme.TextMuted, "Overlaps");
        if (OverlapBox.Contains(Cursor) && Pressed)
        {
            (void)Session->ExecuteDirective(UVCommandDirective::SelectOverlappingFaces, nullptr);
        }
        BodyY += 64.0f;
    }

    // 4c. Layers Subheader ("LAYERS (02)" + right [Stack | Unstack | Reset] icon buttons)
    {
        char LblBuf[48]{};
        std::snprintf(LblBuf, sizeof(LblBuf), "LAYERS (%02zu)", LayerSpan.size());
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { LeftX, BodyY + 4.0f }, 9.0f, Theme.TextMuted, LblBuf);

        struct OutlinerMiniOperation
        {
            ControlIcon        OperationIcon;
            const char*        Tooltip;
            UVCommandDirective Directive;
            bool               UnassignSelection;
        };
        constexpr std::array<OutlinerMiniOperation, 3u> MiniOps {{
            { ControlIcon::LayersSlabs,  "Stack similar UV islands.",                        UVCommandDirective::StackSimilarIslands, false },
            { ControlIcon::LayoutGrid,   "Unstack overlapping UV islands.",                  UVCommandDirective::UnstackOverlapping,  false },
            { ControlIcon::RefreshCycle, "Unassign selected faces from their priority layer.", UVCommandDirective::FitActiveSelection,  true  },
        }};
        float OpX = LeftX + InnerW - (24.0f * 3.0f + 8.0f);
        for (const auto& Op : MiniOps)
        {
            const Rectangle2D OpBtn{ OpX, BodyY, 24.0f, 20.0f };
            const bool        OpHovered = ScrollArea.Contains(Cursor) && OpBtn.Contains(Cursor);
            if (OpHovered)
            {
                ControlAppearance::RecordHoveredTooltip(OpBtn, OptionalTooltip::FromText(Op.Tooltip));
            }
            Surface.FillRectangle(OpBtn, OpHovered ? Theme.HoverBg : Theme.SectionBg, 5.0f);
            Surface.StrokeRectangle(OpBtn, OpHovered ? Theme.BorderStrong : Theme.BorderSubtle, 5.0f, 1.0f);
            VectorIconRepository::ConstructControl(
                Surface,
                Op.OperationIcon,
                VectorIconPlacement{ OpBtn.X + 6.0f, OpBtn.Y + 4.0f, 12.0f, OpHovered ? Theme.TextPrimary : Theme.TextMuted, 1.6f });
            if (OpHovered && Pressed)
            {
                if (Op.UnassignSelection)
                {
                    Session->BindSelectionToPriorityLayer(0u);
                }
                else
                {
                    (void)Session->ExecuteDirective(Op.Directive, nullptr);
                }
            }
            OpX += 28.0f;
        }
        BodyY += 26.0f;
    }

    // 4d. Priority Allocation Layer Boxes (.pz-frontier-layer-box: STRICTLY 0px SHARP CORNERS!)
    uint16_t PendingDeleteLayer = 0u;
    const bool AnySelectionActive = Session->SpatialSelectionActive || (Session->SelectionMask.CountSelectedFaces() > 0u);
    auto SetActiveLayerWithoutChangingSelection = [&](uint16_t LayerOrdinal)
    {
        Session->SpatialSelectionActive = true;
        Session->SelectedLayerOrdinal = LayerOrdinal;
        Session->RenderOptions3D.ActiveLayerOrdinal = LayerOrdinal;
        Session->RenderOptions2D.ActiveLayerOrdinal = LayerOrdinal;
        Session->FramebuffersDirty = true;
    };
    auto SelectLayerFacesInCurrentFilter = [&](uint16_t LayerOrdinal, bool FrameAfterSelection)
    {
        SetActiveLayerWithoutChangingSelection(LayerOrdinal);
        const auto& Ref = Session->Asset.Layers.InspectPartitionSpan(Session->Asset.Network, LayerOrdinal);
        std::vector<uint32_t> FilteredFaces{};
        FilteredFaces.reserve(Ref.FaceIndices.size());
        for (const uint32_t FaceIndex : Ref.FaceIndices)
        {
            if (UDIMFilterCode == 0u || Session->Asset.Network.DeriveFaceUDIMNumber(FaceIndex) == UDIMFilterCode)
            {
                FilteredFaces.push_back(FaceIndex);
            }
        }
        Session->SelectionMask.SynchronizeCapacity(Session->Asset.Network);
        if (Input.ControlHeld)
        {
            for (const uint32_t FaceIndex : FilteredFaces)
            {
                Session->SelectionMask.ToggleFaceSelected(FaceIndex);
            }
        }
        else
        {
            Session->SelectionMask.SelectFacesFromList(FilteredFaces, Input.ShiftHeld);
        }
        Session->RefreshGizmoAnchorsFromSelection();
        Session->SynchronizeSelectionToGPU();
        Session->FramebuffersDirty = true;
        if (FrameAfterSelection)
        {
            Session->FitSelectionOrNetwork();
        }
    };

    for (const uint16_t LayerOrdinal : VisibleLayerOrdinals)
    {
        const auto* LayerPtr = Session->Asset.Layers.InspectLayer(LayerOrdinal);
        if (LayerPtr == nullptr)
        {
            continue;
        }
        const auto& Layer = *LayerPtr;
        const bool RowSelected = AnySelectionActive && (Session->SelectedLayerOrdinal == Layer.LayerOrdinal);
        const ColorRgba LayerCol = LinearTintToRgba(Layer.LinearTintRGB);
        const auto& PartRef = Session->Asset.Layers.InspectPartitionSpan(Session->Asset.Network, Layer.LayerOrdinal);
        const Rectangle2D BoxRect   { LeftX, BodyY, InnerW, 39.0f };
        const bool        RowHover  = ScrollArea.Contains(Cursor) && BoxRect.Contains(Cursor);
        if (RowHover)
        {
            ControlAppearance::RecordHoveredTooltip(
                BoxRect, OptionalTooltip::FromText("Select this layer's faces. Double-click to frame its UV bounds."));
        }

        // Strictly 0.0f corner radius per Experimental/UVEditor .pz-frontier-layer-box (#171717 / #1e1e1e / #262626)!
        const ColorRgba BoxFill   = RowSelected ? ColorRgba{ 38, 38, 38, 255 } : (RowHover ? ColorRgba{ 30, 30, 30, 255 } : ColorRgba{ 23, 23, 23, 255 });
        const ColorRgba BoxBorder = RowSelected ? ColorRgba{ 68, 68, 68, 255 } : (RowHover ? ColorRgba{ 52, 52, 52, 255 } : ColorRgba{ 39, 39, 39, 255 });
        Surface.FillRectangle(BoxRect, BoxFill, 0.0f);
        Surface.StrokeRectangle(BoxRect, BoxBorder, 0.0f, 1.0f);
        Surface.FillRectangle({ BoxRect.X, BoxRect.Y, 3.0f, BoxRect.Height }, LayerCol, 0.0f);

        // 12x12 sharp 0px colour swatch (.pz-layer-sq-swatch)
        const Rectangle2D SwatchRect{ BoxRect.X + 10.0f, BoxRect.Y + 13.5f, 12.0f, 12.0f };
        Surface.FillRectangle(SwatchRect, LayerCol, 0.0f);
        Surface.StrokeRectangle(SwatchRect, ColorRgba{ 255, 255, 255, 72 }, 0.0f, 1.0f);
        const bool SwatchHover = ScrollArea.Contains(Cursor) && SwatchRect.Contains(Cursor);
        if (SwatchHover)
        {
            ControlAppearance::RecordHoveredTooltip(
                SwatchRect, OptionalTooltip::FromText("Pick a priority-layer colour."));
        }
        if (SwatchHover && Pressed && !ColorPaletteConsumedPress)
        {
            ColorPickerLayerOrdinal = ColorPickerLayerOrdinal == Layer.LayerOrdinal ? 0u : Layer.LayerOrdinal;
        }

        // Right-side controls: [55%] 0px box (.pz-layer-pct-box) + [+] bind + [x] delete
        const Rectangle2D DelBtn  { BoxRect.Right() - 24.0f, BoxRect.Y + 9.5f, 20.0f, 20.0f };
        const Rectangle2D BindBtn { DelBtn.X - 22.0f, BoxRect.Y + 9.5f, 20.0f, 20.0f };
        const Rectangle2D PctBox  { BindBtn.X - 40.0f, BoxRect.Y + 9.5f, 36.0f, 20.0f };

        Surface.FillRectangle(PctBox, ColorRgba{ 32, 32, 32, 255 }, 0.0f);
        Surface.StrokeRectangle(PctBox, ColorRgba{ 50, 50, 50, 255 }, 0.0f, 1.0f);
        char PctStr[16]{};
        std::snprintf(PctStr, sizeof(PctStr), "%.0f%%", Layer.TargetSharePercent);
        const float PctW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, PctStr);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { PctBox.X + (PctBox.Width - PctW) * 0.5f, PctBox.Y + 3.5f },
            9.5f,
            ColorRgba{ 239, 244, 207, 255 },
            PctStr);

        // Bind '+' button
        const bool BindHover = BindBtn.Contains(Cursor);
        if (BindHover && ScrollArea.Contains(Cursor))
        {
            ControlAppearance::RecordHoveredTooltip(
                BindBtn, OptionalTooltip::FromText("Bind the currently selected faces to this priority layer."));
        }
        Surface.FillRectangle(BindBtn, BindHover ? Theme.HoverBg : ColorRgba{ 32, 32, 32, 255 }, 0.0f);
        Surface.StrokeRectangle(BindBtn, Theme.BorderSubtle, 0.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::PlusAdd,
            VectorIconPlacement{ BindBtn.X + 4.5f, BindBtn.Y + 4.5f, 11.0f, BindHover ? Theme.TextPrimary : Theme.TextMuted, 1.7f });

        // Delete 'x' button
        const bool DelHover = DelBtn.Contains(Cursor);
        if (DelHover && ScrollArea.Contains(Cursor))
        {
            ControlAppearance::RecordHoveredTooltip(
                DelBtn, OptionalTooltip::FromText("Delete this priority layer and return its faces to the remainder."));
        }
        Surface.FillRectangle(DelBtn, DelHover ? Theme.DangerSoft : ColorRgba{ 32, 32, 32, 255 }, 0.0f);
        Surface.StrokeRectangle(DelBtn, Theme.BorderSubtle, 0.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::CloseCross,
            VectorIconPlacement{ DelBtn.X + 4.5f, DelBtn.Y + 4.5f, 11.0f, DelHover ? Theme.DangerInk : Theme.TextMuted, 1.6f });

        // Title & Subtitle (.pz-layer-box-title & .pz-layer-box-sub)
        const float TextAvailW = std::max(40.0f, PctBox.X - (BoxRect.X + 31.0f) - 6.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { BoxRect.X + 31.0f, BoxRect.Y + 5.0f },
            TextAvailW,
            11.0f,
            RowSelected ? ColorRgba{ 255, 255, 255, 255 } : Theme.TextPrimary,
            Layer.LayerTitle);

        char SubMeta[96]{};
        char UdimLabel[40]{};
        if (Layer.TargetUDIMNumber >= 1001u)
        {
            std::snprintf(UdimLabel, sizeof(UdimLabel), "UDIM %u", Layer.TargetUDIMNumber);
        }
        else if (PartRef.UDIMNumbers.size() == 1u)
        {
            std::snprintf(UdimLabel, sizeof(UdimLabel), "UDIM %u", PartRef.UDIMNumbers.front());
        }
        else if (PartRef.UDIMNumbers.size() > 1u)
        {
            std::snprintf(UdimLabel, sizeof(UdimLabel), "Multi UDIM");
        }
        else
        {
            std::snprintf(UdimLabel, sizeof(UdimLabel), "Auto UDIM");
        }
        std::size_t VisibleFaceCount = PartRef.FaceIndices.size();
        if (UDIMFilterCode >= 1001u)
        {
            VisibleFaceCount = 0u;
            for (const uint32_t FaceIndex : PartRef.FaceIndices)
            {
                VisibleFaceCount += Session->Asset.Network.DeriveFaceUDIMNumber(FaceIndex) == UDIMFilterCode ? 1u : 0u;
            }
        }
        std::snprintf(
            SubMeta,
            sizeof(SubMeta),
            "%s · %s faces",
            UdimLabel,
            FormatCountWithCommas(VisibleFaceCount).c_str());
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { BoxRect.X + 31.0f, BoxRect.Y + 21.0f },
            TextAvailW,
            8.8f,
            Theme.TextMuted,
            SubMeta);

        if (BoxRect.Y < ScrollArea.Bottom() && BoxRect.Bottom() > ScrollArea.Y)
        {
            const float ClipTop = std::max(BoxRect.Y, ScrollArea.Y);
            const float ClipBtm = std::min(BoxRect.Bottom(), ScrollArea.Bottom());
            if (ClipBtm - ClipTop >= 12.0f)
            {
                RenderedLayerSlotBounds.emplace_back(
                    Layer.LayerOrdinal,
                    Rectangle2D{ BoxRect.X, ClipTop, BoxRect.Width, ClipBtm - ClipTop });
            }
        }

        if (RowHover && Pressed && !SwatchHover && !ColorPaletteConsumedPress)
        {
            if (DelHover)
            {
                PendingDeleteLayer = Layer.LayerOrdinal;
            }
            else if (BindHover)
            {
                SetActiveLayerWithoutChangingSelection(Layer.LayerOrdinal);
                Session->BindSelectionToPriorityLayer(Layer.LayerOrdinal);
                if (ContentBrowserPanel* Browser = Session->QueryBrowserPanel())
                {
                    Browser->FocusColourSlot("uv.priority_layer.tint");
                }
            }
            else
            {
                SelectLayerFacesInCurrentFilter(Layer.LayerOrdinal, false);
                if (ContentBrowserPanel* Browser = Session->QueryBrowserPanel())
                {
                    Browser->FocusColourSlot("uv.priority_layer.tint");
                }
                if (Input.DoubleClicked)
                {
                    Session->FitSelectionOrNetwork();
                }
            }
        }

        BodyY += 42.0f;
    }

    if (PendingDeleteLayer != 0u)
    {
        Session->SelectedLayerOrdinal = PendingDeleteLayer;
        (void)Session->DeleteSelectedLayer();
    }

    // 4e. Unassigned Remainder Box (.pz-frontier-layer-box.remainder, 0px corners, #6e6e6e bar)
    if (ShowRemainderBox)
    {
        const bool RemActive = AnySelectionActive && (Session->SelectedLayerOrdinal == 0u);
        const ColorRgba RemCol{ 110, 110, 110, 255 };
        const Rectangle2D RemBox{ LeftX, BodyY, InnerW, 39.0f };
        const bool        RemHover  = ScrollArea.Contains(Cursor) && RemBox.Contains(Cursor);

        const ColorRgba RemFill   = RemActive ? ColorRgba{ 38, 38, 38, 255 } : (RemHover ? ColorRgba{ 30, 30, 30, 255 } : ColorRgba{ 20, 20, 20, 255 });
        const ColorRgba RemBorder = RemActive ? ColorRgba{ 68, 68, 68, 255 } : ColorRgba{ 39, 39, 39, 255 };
        Surface.FillRectangle(RemBox, RemFill, 0.0f);
        Surface.StrokeRectangle(RemBox, RemBorder, 0.0f, 1.0f);
        Surface.FillRectangle({ RemBox.X, RemBox.Y, 3.0f, RemBox.Height }, RemCol, 0.0f);
        Surface.FillRectangle({ RemBox.X + 10.0f, RemBox.Y + 13.5f, 12.0f, 12.0f }, RemCol, 0.0f);

        const Rectangle2D PctBox{ RemBox.Right() - 44.0f, RemBox.Y + 9.5f, 36.0f, 20.0f };
        Surface.FillRectangle(PctBox, ColorRgba{ 32, 32, 32, 255 }, 0.0f);
        Surface.StrokeRectangle(PctBox, ColorRgba{ 50, 50, 50, 255 }, 0.0f, 1.0f);
        char RemPct[16]{};
        std::snprintf(RemPct, sizeof(RemPct), "%.0f%%", Session->Asset.Layers.DeriveRemainderSharePercent());
        const float PctW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, RemPct);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { PctBox.X + (PctBox.Width - PctW) * 0.5f, PctBox.Y + 3.5f }, 9.5f, Theme.TextMuted, RemPct);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { RemBox.X + 31.0f, RemBox.Y + 5.0f },
            PctBox.X - (RemBox.X + 35.0f),
            11.0f,
            Theme.TextPrimary,
            "Unassigned Remainder");

        char RemMeta[96]{};
        std::snprintf(RemMeta, sizeof(RemMeta), "Auto-fills remaining · %s faces", FormatCountWithCommas(RemainderFaces.size()).c_str());
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { RemBox.X + 31.0f, RemBox.Y + 21.0f },
            PctBox.X - (RemBox.X + 35.0f),
            8.8f,
            Theme.TextMuted,
            RemMeta);

        if (RemHover)
        {
            ControlAppearance::RecordHoveredTooltip(
                RemBox, OptionalTooltip::FromText("Select the unassigned faces in the current UDIM filter."));
        }
        if (RemHover && Pressed && !RemainderFaces.empty())
        {
            SelectLayerFacesInCurrentFilter(0u, false);
        }
        BodyY += 42.0f;
    }

    if (ColorPickerLayerOrdinal != 0u && ColorPaletteLayerRow >= 0 && ColorPaletteBounds.Height > 0.0f)
    {
        const auto* PickerLayer = Session->Asset.Layers.InspectLayer(ColorPickerLayerOrdinal);
        Surface.FillRectangle(ColorPaletteBounds, Theme.SectionBg, 7.0f);
        Surface.StrokeRectangle(ColorPaletteBounds, Theme.BorderStrong, 7.0f, 1.0f);
        for (uint32_t Slot = 0u; Slot < LayerColorPalette.size(); ++Slot)
        {
            const Rectangle2D SwatchHit{
                ColorPaletteBounds.X + 7.0f + static_cast<float>(Slot) * 23.0f,
                ColorPaletteBounds.Y + 7.0f, 18.0f, 16.0f };
            const bool Hovered = SwatchHit.Contains(Cursor);
            Surface.FillRectangle(SwatchHit, LinearTintToRgba(LayerColorPalette[Slot]), 3.0f);
            const bool SelectedColor = PickerLayer != nullptr &&
                std::abs(PickerLayer->LinearTintRGB.X - LayerColorPalette[Slot].X) < 0.015f &&
                std::abs(PickerLayer->LinearTintRGB.Y - LayerColorPalette[Slot].Y) < 0.015f &&
                std::abs(PickerLayer->LinearTintRGB.Z - LayerColorPalette[Slot].Z) < 0.015f;
            Surface.StrokeRectangle(
                SwatchHit, SelectedColor ? Theme.TextPrimary : (Hovered ? Theme.AccentBorder : Theme.BorderSubtle),
                3.0f, SelectedColor || Hovered ? 1.5f : 1.0f);
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    SwatchHit, OptionalTooltip::FromTitled(LayerColorNames[Slot], "Apply this colour to the priority layer."));
            }
        }
    }

    Surface.PopClip();

    if (UDIMFilterDropdownOpen && !UDIMFilterLabels.empty())
    {
        Rectangle2D SheetAnchor = UDIMFilterDropdownBounds;
        Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(
            SheetAnchor, static_cast<uint32_t>(UDIMFilterLabels.size()));
        if (SheetBounds.Bottom() > Bounds.Bottom())
        {
            SheetAnchor.Y -= SheetBounds.Height + 2.0f * ControlMetrics::DropdownPaddingY + SheetAnchor.Height;
            const Rectangle2D AboveSheet = DropdownSelector::QuerySheetBounds(
                SheetAnchor, static_cast<uint32_t>(UDIMFilterLabels.size()));
            if (AboveSheet.Y >= Bounds.Y)
            {
                SheetBounds = AboveSheet;
            }
            else
            {
                SheetAnchor = UDIMFilterDropdownBounds;
            }
        }

        uint32_t ChosenIndex = CurrentFilterIndex;
        const ControlContact Choice = DropdownSelector::ConstructOptionSheet(
            Surface,
            SheetAnchor,
            UDIMFilterLabels.data(),
            static_cast<uint32_t>(UDIMFilterLabels.size()),
            CurrentFilterIndex,
            Input.PrimaryPointer,
            ChosenIndex);
        if (Choice.Clicked && ChosenIndex < UDIMFilterOptions.size())
        {
            UDIMFilterCode = UDIMFilterOptions[ChosenIndex];
            if (UDIMFilterCode >= 1001u)
            {
                Session->ActiveUDIMNumber = UDIMFilterCode;
                Session->FramebuffersDirty = true;
            }
            UDIMFilterDropdownOpen = false;
        }
        else if (Input.PrimaryPointer.Released &&
                 !UDIMFilterDropdownBounds.Contains(Cursor) && !SheetBounds.Contains(Cursor))
        {
            UDIMFilterDropdownOpen = false;
        }
    }

    // 5. Sticky Bottom Footer (.pz-outliner-budget-footer: UDIM 1001 Space Budget + 6px 0px-radius spectrum bar)
    const Rectangle2D FooterRect{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FooterRect, Theme.PanelBg);
    Surface.StrokeLine({ FooterRect.X, FooterRect.Y }, { FooterRect.Right(), FooterRect.Y }, Theme.BorderSubtle, 1.0f);

    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::LayoutGrid,
        VectorIconPlacement{ LeftX, FooterRect.Y + 11.0f, 12.0f, Theme.TextMuted, 1.6f });
    char FootLeft[48]{};
    std::snprintf(FootLeft, sizeof(FootLeft), "UDIM %u Space Budget", Session->ActiveUDIMNumber);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + 17.0f, FooterRect.Y + 10.0f }, 9.8f, Theme.TextPrimary, FootLeft);

    char FootRight[48]{};
    const char* BudgetMode = Session->WorkspaceConfig.InspectUVPreferences().BudgetMode ==
        Configuration::UVBudgetEnforcementMode::StrictRatio ? "STRICT" : "FILL POCKETS";
    std::snprintf(FootRight, sizeof(FootRight), "%.1f%% filled · %s", CachedUtilizationPct, BudgetMode);
    const float FootRightW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, FootRight);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + InnerW - FootRightW, FooterRect.Y + 10.5f },
        9.0f,
        Theme.TextMuted,
        FootRight);

    // 6px sharp 0px-radius segmented allocation spectrum bar
    const Rectangle2D SpecBar{ LeftX, FooterRect.Y + 32.0f, InnerW, 6.0f };
    Surface.FillRectangle(SpecBar, ColorRgba{ 110, 110, 110, 255 }, 0.0f);
    float SegX = SpecBar.X;
    for (const auto& Layer : Session->Asset.Layers.InspectLayers())
    {
        const float SegW = std::clamp(Layer.TargetSharePercent * 0.01f, 0.0f, 1.0f) * SpecBar.Width;
        if (SegW > 0.5f)
        {
            Surface.FillRectangle({ SegX, SpecBar.Y, SegW, SpecBar.Height }, LinearTintToRgba(Layer.LinearTintRGB), 0.0f);
            SegX += SegW;
        }
    }
    Surface.StrokeRectangle(SpecBar, Theme.BorderSubtle, 0.0f, 1.0f);

    Surface.PopClip();
    (void)ControlTooltip::PresentHoveredControlTooltip(
        Surface, { 0.0f, 0.0f, Bounds.Width, Bounds.Height }, Input.PrimaryPointer);
}

void UVOutlinerPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##UVOutlinerPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    if (Session != nullptr)
    {
        const Point2D PhysicalCursor {
            ScreenBounds.X + Input.PrimaryPointer.X * SafeScale,
            ScreenBounds.Y + Input.PrimaryPointer.Y * SafeScale,
        };
        if (ContentBrowserPanel* Browser = Session->QueryBrowserPanel())
        {
            if (Browser->QueryBlocksPointerAt(PhysicalCursor))
            {
                WorkspaceAppearance::SuppressPanePointerInput(Input);
            }
        }
    }

    Render(Surface, LogicalBounds, Input, Δt);

    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);

    if (Session != nullptr)
    {
        if (ContentBrowserPanel* Browser = Session->QueryBrowserPanel())
        {
            const float DrawerTopY = Browser->QueryDrawerTopPhysicalY();

            // Zero out any previously registered Outliner row slots that scrolled off or were deleted
            for (const uint16_t PrevOrd : PreviouslyRegisteredLayerOrdinals)
            {
                const std::string SlotId = "uv.outliner.layer." + std::to_string(PrevOrd);
                Browser->RegisterColourSlot(ColourSlotRegistration{
                    SlotId,
                    "Priority Layer Row",
                    Rectangle2D{},
                    nullptr,
                    [Sess = Session, PrevOrd](ColorRgba AssignedRgba, const ColourCodexEntry&)
                    {
                        if (Sess != nullptr)
                        {
                            Sess->AssignLayerTintByOrdinal(
                                PrevOrd,
                                {
                                    static_cast<float>(AssignedRgba.Red) / 255.0f,
                                    static_cast<float>(AssignedRgba.Green) / 255.0f,
                                    static_cast<float>(AssignedRgba.Blue) / 255.0f,
                                },
                                true);
                        }
                    },
                });
            }
            PreviouslyRegisteredLayerOrdinals.clear();

            for (const auto& [Ord, LogicalBox] : RenderedLayerSlotBounds)
            {
                const Rectangle2D PhysicalBox {
                    ScreenBounds.X + LogicalBox.X * SafeScale,
                    ScreenBounds.Y + LogicalBox.Y * SafeScale,
                    LogicalBox.Width * SafeScale,
                    LogicalBox.Height * SafeScale,
                };
                Rectangle2D MappedBounds {};
                if (PhysicalBox.Y < DrawerTopY)
                {
                    Rectangle2D VisiblePhys = PhysicalBox;
                    if (VisiblePhys.Bottom() > DrawerTopY)
                    {
                        VisiblePhys.Height = std::max(0.0f, DrawerTopY - VisiblePhys.Y);
                    }
                    if (VisiblePhys.Height >= 12.0f)
                    {
                        MappedBounds = Browser->MapPhysicalToLogicalBounds(VisiblePhys);
                    }
                }
                const auto* LayerPtr = Session->Asset.Layers.InspectLayer(Ord);
                const std::string Title = LayerPtr != nullptr ? LayerPtr->LayerTitle : ("Priority Layer " + std::to_string(Ord));
                Browser->RegisterColourSlot(ColourSlotRegistration{
                    "uv.outliner.layer." + std::to_string(Ord),
                    Title,
                    MappedBounds,
                    nullptr,
                    [Sess = Session, Ord](ColorRgba AssignedRgba, const ColourCodexEntry&)
                    {
                        if (Sess != nullptr)
                        {
                            Sess->AssignLayerTintByOrdinal(
                                Ord,
                                {
                                    static_cast<float>(AssignedRgba.Red) / 255.0f,
                                    static_cast<float>(AssignedRgba.Green) / 255.0f,
                                    static_cast<float>(AssignedRgba.Blue) / 255.0f,
                                },
                                true);
                        }
                    },
                });
                PreviouslyRegisteredLayerOrdinals.push_back(Ord);
            }

            // Keep uv.priority_layer.tint active for toolbar "Apply" without double-ringing selected row on drag
            Browser->RegisterColourSlot(ColourSlotRegistration{
                "uv.priority_layer.tint",
                "Active UV Priority Layer Tint",
                Rectangle2D{},
                nullptr,
                [Sess = Session](ColorRgba AssignedRgba, const ColourCodexEntry&)
                {
                    if (Sess != nullptr)
                    {
                        Sess->AssignSelectedLayerTint(
                            {
                                static_cast<float>(AssignedRgba.Red) / 255.0f,
                                static_cast<float>(AssignedRgba.Green) / 255.0f,
                                static_cast<float>(AssignedRgba.Blue) / 255.0f,
                            },
                            true);
                    }
                },
            });
        }
    }
}

} // namespace Frontier::Workspaces
