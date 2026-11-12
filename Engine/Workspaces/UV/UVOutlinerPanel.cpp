/*============================================================================================================================================
                                                           UVOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVOutlinerPanel matching Experimental/UVEditor (KPI trio, stat row, 0px layer boxes & UDIM Space Budget spectrum)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVOutlinerPanel.h"

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

void UVOutlinerPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session               = TargetSession;
    CachedNetworkRevision = UINT64_MAX;
    CachedUVRevision      = UINT64_MAX;
}

void UVOutlinerPanel::RefreshCachedAtlasTelemetry()
{
    if (Session == nullptr)
    {
        return;
    }

    const auto& Net = Session->Asset.Network;
    if (CachedNetworkRevision != Net.NetworkRevision || CachedUVRevision != Net.UVRevision)
    {
        CachedNetworkRevision = Net.NetworkRevision;
        CachedUVRevision      = Net.UVRevision;

        const auto Islands = SeamCutGenerator::PartitionConnectedUVIslands(Net);
        CachedIslandCount  = static_cast<uint32_t>(Islands.size());

        double TotalUVArea = 0.0;
        for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Net.Faces.size()); ++FaceIdx)
        {
            TotalUVArea += std::max(0.0f, Net.EvaluateFaceSignedAreaUV(FaceIdx));
        }
        const double TileCount = static_cast<double>(std::max<std::size_t>(1u, Session->Asset.UDIMTiles.size()));
        CachedUtilizationPct   = static_cast<float>(std::clamp((TotalUVArea / TileCount) * 100.0, 0.0, 100.0));
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

    RefreshCachedAtlasTelemetry();

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;

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
        if (Pressed)
        {
            if (SearchRect.Contains(Cursor))
            {
                SearchEditBuffer.Active          = true;
                SearchEditBuffer.Caret           = SearchEditBuffer.Length;
                SearchEditBuffer.SelectionAnchor = SearchEditBuffer.Caret;
            }
            else if (SearchEditBuffer.Active)
            {
                SearchEditBuffer.Finalize();
            }
        }
        if (SearchEditBuffer.Active)
        {
            SearchEditBuffer.BlinkPhase = std::fmod(SearchEditBuffer.BlinkPhase + static_cast<float>(Δt), 1.06f);
            if (!Input.TypedUtf8.empty())
            {
                SearchEditBuffer.Insert(Input.TypedUtf8.c_str());
            }
            if (Input.BackspacePressed)
            {
                SearchEditBuffer.Backspace();
            }
            if (Input.EscapePressed || Input.EnterPressed)
            {
                SearchEditBuffer.Finalize();
            }
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

    // 3. UDIM Filter Chip Row ("All UDIMs" + per-tile chips)
    {
        float ChipX = LeftX;
        auto ConstructFilterChip = [&](const char* Label, uint32_t Code)
        {
            const float ChipW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, Label) + 18.0f;
            if (ChipX + ChipW > LeftX + InnerW && ChipX > LeftX + 2.0f)
            {
                return;
            }
            const Rectangle2D ChipRect{ ChipX, CurY, ChipW, 24.0f };
            const bool        Active  = (UDIMFilterCode == Code);
            const bool        Hovered = ChipRect.Contains(Cursor);
            Surface.FillRectangle(ChipRect, Active ? Theme.AccentSoft : (Hovered ? Theme.HoverBg : Theme.WellBg), 12.0f);
            Surface.StrokeRectangle(ChipRect, Active ? Theme.AccentBorder : Theme.BorderSubtle, 12.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { ChipRect.X + 9.0f, ChipRect.Y + 5.5f },
                9.5f,
                Active ? Theme.TextPrimary : Theme.TextMuted,
                Label);
            if (Hovered && Pressed)
            {
                UDIMFilterCode = Code;
                if (Code >= 1001u)
                {
                    Session->ActiveUDIMNumber  = Code;
                    Session->FramebuffersDirty = true;
                }
            }
            ChipX += ChipW + 5.0f;
        };

        ConstructFilterChip("All UDIMs", 0u);
        for (const uint32_t TileCode : Session->Asset.UDIMTiles)
        {
            char TileLbl[24]{};
            std::snprintf(TileLbl, sizeof(TileLbl), "UDIM %u", TileCode);
            ConstructFilterChip(TileLbl, TileCode);
        }
    }
    CurY += 32.0f;

    // Reserve sticky bottom footer (.pz-outliner-budget-footer: 54px)
    constexpr float   FooterH = 54.0f;
    const Rectangle2D ScrollArea{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - FooterH - CurY)
    };

    if (ScrollArea.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY = std::max(0.0f, ScrollOffsetY - Input.WheelDeltaY * 28.0f);
    }

    Surface.PushClip(ScrollArea);
    float BodyY = ScrollArea.Y - ScrollOffsetY + 2.0f;

    // 4a. 3-Column KPI Trio (.pz-left-kpi-trio: Utilization | Selected | Remaining)
    {
        const Rectangle2D TrioBox{ LeftX, BodyY, InnerW, 60.0f };
        Surface.FillRectangle(TrioBox, Theme.SectionBg, 16.0f);
        Surface.StrokeRectangle(TrioBox, Theme.BorderSubtle, 16.0f, 1.0f);

        const float ColW = InnerW / 3.0f;
        // Highlight first cell softly like Experimental/UVEditor .pz-left-kpi.highlight
        Surface.FillRectangle({ TrioBox.X + 2.0f, TrioBox.Y + 2.0f, ColW - 2.0f, TrioBox.Height - 4.0f }, Theme.AccentSoft, 14.0f);
        Surface.StrokeLine({ TrioBox.X + ColW, TrioBox.Y + 10.0f }, { TrioBox.X + ColW, TrioBox.Bottom() - 10.0f }, Theme.BorderSubtle, 1.0f);
        Surface.StrokeLine({ TrioBox.X + ColW * 2.0f, TrioBox.Y + 10.0f }, { TrioBox.X + ColW * 2.0f, TrioBox.Bottom() - 10.0f }, Theme.BorderSubtle, 1.0f);

        char UtilBuf[24]{}, SelBuf[32]{}, RemBuf[24]{};
        std::snprintf(UtilBuf, sizeof(UtilBuf), "%.1f%%", CachedUtilizationPct);
        std::snprintf(SelBuf, sizeof(SelBuf), "%uf", Session->SelectionMask.CountSelectedFaces());
        std::snprintf(RemBuf, sizeof(RemBuf), "%.0f%%", Session->Asset.Layers.DeriveRemainderSharePercent());

        const ColorRgba UtilInk = Theme.LightSurface ? Theme.AccentFill : ColorRgba{ 239, 244, 207, 255 };
        auto ConstructKpiColumn = [&](uint32_t Idx, const char* Val, const char* Cap, ColorRgba ValInk)
        {
            const float CellX = TrioBox.X + ColW * static_cast<float>(Idx);
            const float ValW  = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 14.0f, Val);
            const float CapW  = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, Cap);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Medium,
                { CellX + (ColW - ValW) * 0.5f, TrioBox.Y + 11.0f },
                14.0f,
                ValInk,
                Val);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { CellX + (ColW - CapW) * 0.5f, TrioBox.Y + 35.0f },
                9.0f,
                Theme.TextMuted,
                Cap);
        };
        ConstructKpiColumn(0u, UtilBuf, "Utilization", UtilInk);
        ConstructKpiColumn(1u, SelBuf,  "Selected",    Theme.TextPrimary);
        ConstructKpiColumn(2u, RemBuf,  "Remaining",   Theme.TextPrimary);
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
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { StretchBox.X + 18.0f, StretchBox.Y + 19.5f }, 10.5f, Theme.CautionInk, "a");

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
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { OverlapBox.X + 19.5f, OverlapBox.Y + 19.5f }, 10.5f, OvRing, "!");

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
    const auto LayerSpan = Session->Asset.Layers.InspectLayers();
    {
        char LblBuf[48]{};
        std::snprintf(LblBuf, sizeof(LblBuf), "LAYERS (%02zu)", LayerSpan.size());
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { LeftX, BodyY + 4.0f }, 9.0f, Theme.TextMuted, LblBuf);

        const std::array<std::pair<const char*, UVCommandDirective>, 3u> MiniOps {{
            { "Stk", UVCommandDirective::StackSimilarIslands  },
            { "Uns", UVCommandDirective::UnstackOverlapping   },
            { "Fit", UVCommandDirective::FitActiveSelection },
        }};
        float OpX = LeftX + InnerW - (28.0f * 3.0f + 8.0f);
        for (const auto& [Tag, Dir] : MiniOps)
        {
            const Rectangle2D OpBtn{ OpX, BodyY, 28.0f, 20.0f };
            if (ConstructCapsuleButton(Surface, OpBtn, Tag, Theme, Cursor, Pressed, false, false, 8.2f, 5.0f))
            {
                (void)Session->ExecuteDirective(Dir, nullptr);
            }
            OpX += 32.0f;
        }
        BodyY += 26.0f;
    }

    // 4d. Priority Allocation Layer Boxes (.pz-frontier-layer-box: STRICTLY 0px SHARP CORNERS!)
    const std::string_view FilterQuery(SearchEditBuffer.Text, SearchEditBuffer.Length);
    uint16_t PendingDeleteLayer = 0u;

    for (const auto& Layer : LayerSpan)
    {
        if (UDIMFilterCode != 0u && Layer.TargetUDIMNumber != UDIMFilterCode)
        {
            continue;
        }
        if (!ContainsCaseInsensitive(Layer.LayerTitle, FilterQuery))
        {
            continue;
        }

        const bool        IsActive  = (Session->SelectedLayerOrdinal == Layer.LayerOrdinal);
        const ColorRgba   LayerCol  = LinearTintToRgba(Layer.LinearTintRGB);
        const auto&       PartRef   = Session->Asset.Layers.InspectPartitionSpan(Session->Asset.Network, Layer.LayerOrdinal);
        const Rectangle2D BoxRect   { LeftX, BodyY, InnerW, 42.0f };
        const bool        RowHover  = ScrollArea.Contains(Cursor) && BoxRect.Contains(Cursor);

        // Strictly 0.0f corner radius per Experimental/UVEditor .pz-frontier-layer-box!
        Surface.FillRectangle(BoxRect, IsActive ? Theme.AccentSoft : (RowHover ? Theme.HoverBg : Theme.SectionBg), 0.0f);
        Surface.StrokeRectangle(BoxRect, IsActive ? LayerCol : Theme.BorderSubtle, 0.0f, 1.0f);
        Surface.FillRectangle({ BoxRect.X, BoxRect.Y, 3.0f, BoxRect.Height }, LayerCol, 0.0f);

        // 12x12 sharp 0px colour swatch
        const Rectangle2D SwatchRect{ BoxRect.X + 11.0f, BoxRect.Y + 15.0f, 12.0f, 12.0f };
        Surface.FillRectangle(SwatchRect, LayerCol, 0.0f);
        Surface.StrokeRectangle(SwatchRect, ColorRgba{ 0, 0, 0, 90 }, 0.0f, 1.0f);

        // Right-side controls: [55%] 0px box + [+] bind + [x] delete
        const Rectangle2D DelBtn  { BoxRect.Right() - 24.0f, BoxRect.Y + 11.0f, 20.0f, 20.0f };
        const Rectangle2D BindBtn { DelBtn.X - 22.0f, BoxRect.Y + 11.0f, 20.0f, 20.0f };
        const Rectangle2D PctBox  { BindBtn.X - 40.0f, BoxRect.Y + 11.0f, 36.0f, 20.0f };

        Surface.FillRectangle(PctBox, Theme.WellBg, 0.0f);
        Surface.StrokeRectangle(PctBox, Theme.BorderStrong, 0.0f, 1.0f);
        char PctStr[16]{};
        std::snprintf(PctStr, sizeof(PctStr), "%.0f%%", Layer.TargetSharePercent);
        const float PctW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, PctStr);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { PctBox.X + (PctBox.Width - PctW) * 0.5f, PctBox.Y + 3.5f },
            9.5f,
            LayerCol,
            PctStr);

        // Bind '+' button
        const bool BindHover = BindBtn.Contains(Cursor);
        Surface.FillRectangle(BindBtn, BindHover ? Theme.HoverBg : Theme.WellBg, 0.0f);
        Surface.StrokeRectangle(BindBtn, Theme.BorderSubtle, 0.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BindBtn.X + 6.0f, BindBtn.Y + 2.5f }, 11.0f, Theme.TextMuted, "+");

        // Delete 'x' button
        const bool DelHover = DelBtn.Contains(Cursor);
        Surface.FillRectangle(DelBtn, DelHover ? Theme.DangerSoft : Theme.WellBg, 0.0f);
        Surface.StrokeRectangle(DelBtn, Theme.BorderSubtle, 0.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::CloseCross,
            VectorIconPlacement{ DelBtn.X + 4.5f, DelBtn.Y + 4.5f, 11.0f, DelHover ? Theme.DangerInk : Theme.TextMuted, 1.6f });

        // Title & Subtitle
        const float TextAvailW = std::max(40.0f, PctBox.X - (BoxRect.X + 30.0f) - 6.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { BoxRect.X + 30.0f, BoxRect.Y + 6.0f },
            TextAvailW,
            11.0f,
            Theme.TextPrimary,
            Layer.LayerTitle);

        char SubMeta[96]{};
        std::snprintf(
            SubMeta,
            sizeof(SubMeta),
            "UDIM %u · %s faces",
            Layer.TargetUDIMNumber,
            FormatCountWithCommas(PartRef.FaceIndices.size()).c_str());
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { BoxRect.X + 30.0f, BoxRect.Y + 22.5f },
            TextAvailW,
            8.8f,
            Theme.TextMuted,
            SubMeta);

        if (RowHover && Pressed)
        {
            if (DelHover && LayerSpan.size() > 1u)
            {
                PendingDeleteLayer = Layer.LayerOrdinal;
            }
            else if (BindHover)
            {
                Session->SelectedLayerOrdinal = Layer.LayerOrdinal;
                Session->BindSelectionToPriorityLayer(Layer.LayerOrdinal);
            }
            else
            {
                Session->SelectPriorityLayer(Layer.LayerOrdinal);
            }
        }

        BodyY += 46.0f;
    }

    if (PendingDeleteLayer != 0u)
    {
        Session->SelectedLayerOrdinal = PendingDeleteLayer;
        (void)Session->DeleteSelectedLayer();
    }

    // 4e. Unassigned Remainder Box (.pz-frontier-layer-box.remainder, 0px corners, #6e6e6e bar)
    {
        const bool        RemActive = (Session->SelectedLayerOrdinal == 0u);
        const ColorRgba   RemCol    { 110, 110, 110, 255 };
        const auto&       RemRef    = Session->Asset.Layers.InspectPartitionSpan(Session->Asset.Network, 0u);
        const Rectangle2D RemBox    { LeftX, BodyY, InnerW, 42.0f };
        const bool        RemHover  = ScrollArea.Contains(Cursor) && RemBox.Contains(Cursor);

        Surface.FillRectangle(RemBox, RemActive ? Theme.AccentSoft : (RemHover ? Theme.HoverBg : Theme.WellBg), 0.0f);
        Surface.StrokeRectangle(RemBox, RemActive ? Theme.AccentBorder : Theme.BorderSubtle, 0.0f, 1.0f);
        Surface.FillRectangle({ RemBox.X, RemBox.Y, 3.0f, RemBox.Height }, RemCol, 0.0f);
        Surface.FillRectangle({ RemBox.X + 11.0f, RemBox.Y + 15.0f, 12.0f, 12.0f }, RemCol, 0.0f);

        const Rectangle2D PctBox{ RemBox.Right() - 44.0f, RemBox.Y + 11.0f, 36.0f, 20.0f };
        Surface.FillRectangle(PctBox, Theme.SectionBg, 0.0f);
        Surface.StrokeRectangle(PctBox, Theme.BorderSubtle, 0.0f, 1.0f);
        char RemPct[16]{};
        std::snprintf(RemPct, sizeof(RemPct), "%.0f%%", Session->Asset.Layers.DeriveRemainderSharePercent());
        const float PctW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, RemPct);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { PctBox.X + (PctBox.Width - PctW) * 0.5f, PctBox.Y + 3.5f }, 9.5f, Theme.TextMuted, RemPct);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { RemBox.X + 30.0f, RemBox.Y + 6.0f },
            PctBox.X - (RemBox.X + 34.0f),
            11.0f,
            Theme.TextPrimary,
            "Unassigned Remainder");

        char RemMeta[96]{};
        std::snprintf(RemMeta, sizeof(RemMeta), "Auto-fills remaining · %s faces", FormatCountWithCommas(RemRef.FaceIndices.size()).c_str());
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { RemBox.X + 30.0f, RemBox.Y + 22.5f },
            PctBox.X - (RemBox.X + 34.0f),
            8.8f,
            Theme.TextMuted,
            RemMeta);

        if (RemHover && Pressed)
        {
            Session->SelectPriorityLayer(0u);
        }
        BodyY += 48.0f;
    }

    Surface.PopClip();

    // 5. Sticky Bottom Footer (.pz-outliner-budget-footer: UDIM 1001 Space Budget + 6px 0px-radius spectrum bar)
    const Rectangle2D FooterRect{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FooterRect, Theme.PanelBg);
    Surface.StrokeLine({ FooterRect.X, FooterRect.Y }, { FooterRect.Right(), FooterRect.Y }, Theme.BorderSubtle, 1.0f);

    ConstructPackGridIcon(Surface, { LeftX, FooterRect.Y + 11.0f }, 12.0f, Theme.TextMuted);
    char FootLeft[48]{};
    std::snprintf(FootLeft, sizeof(FootLeft), "UDIM %u Space Budget", Session->ActiveUDIMNumber);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + 17.0f, FooterRect.Y + 10.0f }, 9.8f, Theme.TextPrimary, FootLeft);

    char FootRight[48]{};
    std::snprintf(FootRight, sizeof(FootRight), "%.1f%% filled · STRICT", CachedUtilizationPct);
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

    PixelSpace             Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(Surface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces
