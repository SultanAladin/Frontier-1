/*==========================================================================================================================================
                                                    TEXTUREPAINTLAYERSTACKPANE.CPP
==========================================================================================================================================*/
// 🗂️ Implementation of TexturePaintLayerStackPane: Project Zero PaintOutliner parity with thumbnails, gradient points & footer action bar.

#include "Engine/Workspaces/Texturing/TexturePaintLayerStackPane.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

namespace Frontier::Workspaces
{

namespace
{

using namespace TexturingAppearance;

struct AddLayerOptionSpec
{
    PaintLayerCategory Category;
    ControlIcon        Symbol;
    const char*        TitleText;
    const char*        HintText;
    const char*        FooterLabel;
};

constexpr std::array<AddLayerOptionSpec, 8u> AddLayerCatalog {{
    { PaintLayerCategory::Paint,    ControlIcon::BrushTool,      "Paint",     "Hand-painted texels",       "Paint layer"     },
    { PaintLayerCategory::Fill,     ControlIcon::FillBucket,     "Fill",      "Flat colour / pattern",     "Fill layer"      },
    { PaintLayerCategory::Material, ControlIcon::MaterialSphere, "Material",  "Frontier preset material",  "Material layer"  },
    { PaintLayerCategory::Stamp,    ControlIcon::StampTool,      "Stamp",     "Decal stamps on UV",        "Stamp layer"     },
    { PaintLayerCategory::Text,     ControlIcon::TextType,       "Text",      "Lettering stamped in",      "Text layer"      },
    { PaintLayerCategory::WearGen,  ControlIcon::SparklesAntiAliasing, "Generator", "Edge wear · dust · grunge", "Generator layer" },
    { PaintLayerCategory::Gradient, ControlIcon::PointGradient,  "Gradient",  "Placed points · blended",   "Gradient layer"  },
    { PaintLayerCategory::Folder,   ControlIcon::FolderClosed,   "Folder",    "Group layers together",     "Folder layer"    },
}};

[[nodiscard]] bool MatchesSearchAndFilter(
    const PaintLayerEntry& Layer,
    PaintFilterTab         ActiveFilter,
    std::string_view       Query) noexcept
{
    if (ActiveFilter == PaintFilterTab::VisibleOnly && !Layer.Visible) return false;
    if (ActiveFilter == PaintFilterTab::HiddenOnly  &&  Layer.Visible) return false;
    if (ActiveFilter == PaintFilterTab::MaskedOnly  && !Layer.Mask.Attached) return false;
    if (ActiveFilter == PaintFilterTab::LockedOnly  && !Layer.Locked) return false;

    if (Query.empty())
    {
        return true;
    }
    auto ContainsCaseInsensitive = [](std::string_view Haystack, std::string_view Needle) noexcept -> bool
    {
        if (Needle.size() > Haystack.size()) return false;
        for (std::size_t I = 0u; I + Needle.size() <= Haystack.size(); ++I)
        {
            bool Match = true;
            for (std::size_t J = 0u; J < Needle.size(); ++J)
            {
                if (std::tolower(static_cast<unsigned char>(Haystack[I + J])) !=
                    std::tolower(static_cast<unsigned char>(Needle[J])))
                {
                    Match = false;
                    break;
                }
            }
            if (Match) return true;
        }
        return false;
    };
    return ContainsCaseInsensitive(Layer.Name, Query);
}

void ConstructLayerThumbnailTile(
    PixelSpace&                 Surface,
    const TexturePaintSession&  Session,
    const PaintLayerEntry&      Layer,
    Rectangle2D                 ThumbBounds,
    const TexturingThemeColors& Theme) noexcept
{
    if (Layer.Category == PaintLayerCategory::Folder)
    {
        Surface.FillRectangle(ThumbBounds, Theme.WellSurface, 6.0f);
        Surface.StrokeRectangle(ThumbBounds, Theme.BorderSubtle, 6.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            Layer.FolderExpanded ? ControlIcon::FolderOpen : ControlIcon::FolderClosed,
            VectorIconPlacement{ ThumbBounds.X + 4.0f, ThumbBounds.Y + 4.0f, 16.0f, Theme.TextPrimary, 1.65f });
        return;
    }

    constexpr uint32_t GridN = 8u;
    std::vector<uint8_t> ThumbRgba;
    Session.RenderLayerThumbnailRgba8(Layer, GridN, ThumbRgba);

    Surface.FillRectangle(ThumbBounds, Theme.WellSurface, 6.0f);
    const float CellW = (ThumbBounds.Width - 2.0f) / static_cast<float>(GridN);
    const float CellH = (ThumbBounds.Height - 2.0f) / static_cast<float>(GridN);
    for (uint32_t Gy = 0u; Gy < GridN; ++Gy)
    {
        for (uint32_t Gx = 0u; Gx < GridN; ++Gx)
        {
            const std::size_t Idx = (static_cast<std::size_t>(Gy) * GridN + Gx) * 4u;
            const ColorRgba CellColor{
                ThumbRgba[Idx + 0u],
                ThumbRgba[Idx + 1u],
                ThumbRgba[Idx + 2u],
                255u
            };
            Surface.FillRectangle(
                { ThumbBounds.X + 1.0f + static_cast<float>(Gx) * CellW,
                  ThumbBounds.Y + 1.0f + static_cast<float>(Gy) * CellH,
                  CellW + 0.4f,
                  CellH + 0.4f },
                CellColor,
                (Gx == 0u || Gx + 1u == GridN || Gy == 0u || Gy + 1u == GridN) ? 1.5f : 0.0f);
        }
    }
    Surface.StrokeRectangle(ThumbBounds, Theme.BorderStrong, 6.0f, 1.0f);
}

} // namespace

TexturePaintLayerStackPane::~TexturePaintLayerStackPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void TexturePaintLayerStackPane::AttachSession(TexturePaintSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void TexturePaintLayerStackPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##TexturePaintLayerStackCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

void TexturePaintLayerStackPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  /*DeltaSeconds*/)
{
    if (Session == nullptr || Bounds.Width < 40.0f || Bounds.Height < 40.0f)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingTheme(Surface);
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground);
    Surface.StrokeLine({ Bounds.Right() - 0.5f, Bounds.Y }, { Bounds.Right() - 0.5f, Bounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    const float   PadX    = 12.0f;
    const float   InnerW  = std::max(120.0f, Bounds.Width - PadX * 2.0f);
    float         CurY    = Bounds.Y + 12.0f;

    // 1. Header ("Layers", "Texture Paint · Utah Teapot", "+ Add v" button)
    const auto& ActiveStudy = Session->QueryActiveStudyDescriptor();
    const std::string SubStudy = std::string("Texture Paint · ") + ActiveStudy.Label;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { Bounds.X + PadX, CurY - 2.0f }, 15.0f, Theme.TextPrimary, "Layers");
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { Bounds.X + PadX + 52.0f, CurY + 3.0f }, InnerW - 122.0f, 8.6f, Theme.TextFaint, SubStudy);

    const Rectangle2D AddBtnR{ Bounds.Right() - PadX - 66.0f, CurY - 2.0f, 66.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, AddBtnR, ControlIcon::PlusAdd, "Add", true, AddLayerSheetOpen, Theme, Cursor, Pressed))
    {
        AddLayerSheetOpen  = !AddLayerSheetOpen;
        FooterAddSheetOpen = false;
        FilterSheetOpen    = false;
    }
    CurY += 30.0f;

    // 2. Stat Tiles ("LAYERS 2/2", "CHANNELS 4/22")
    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const Rectangle2D StatLayersR{ Bounds.X + PadX, CurY, StatW, 42.0f };
    const Rectangle2D StatChansR { StatLayersR.Right() + StatGap, CurY, StatW, 42.0f };

    char LayersValBuf[32];
    std::snprintf(LayersValBuf, sizeof(LayersValBuf), "%zu/%zu", Session->CountVisibleLayers(), Session->CountNonFolderLayers());
    char ChansValBuf[32];
    std::snprintf(ChansValBuf, sizeof(ChansValBuf), "%zu/%zu", Session->CountPaintedChannels(), TexturePaintSession::ChannelCount);

    if (ConstructOutlinerStatTile(
            Surface,
            StatLayersR,
            ControlIcon::LayersSlabs,
            "LAYERS",
            LayersValBuf,
            Session->CountVisibleLayers() > 0u,
            Session->LayerFilter == PaintFilterTab::HiddenOnly,
            Theme,
            Cursor,
            Pressed))
    {
        Session->LayerFilter = (Session->LayerFilter == PaintFilterTab::HiddenOnly)
            ? PaintFilterTab::AllLayers
            : PaintFilterTab::HiddenOnly;
    }
    if (ConstructOutlinerStatTile(
            Surface,
            StatChansR,
            ControlIcon::BrushTool,
            "CHANNELS",
            ChansValBuf,
            Session->CountPaintedChannels() > 0u,
            Session->LayerFilter == PaintFilterTab::MaskedOnly,
            Theme,
            Cursor,
            Pressed))
    {
        Session->LayerFilter = (Session->LayerFilter == PaintFilterTab::MaskedOnly)
            ? PaintFilterTab::AllLayers
            : PaintFilterTab::MaskedOnly;
    }
    CurY += 50.0f;

    // 3. Search Bar + Filter Dropdown Button
    const float FilterBtnW = 64.0f;
    const Rectangle2D SearchBoxR{ Bounds.X + PadX, CurY, InnerW - FilterBtnW - 6.0f, 28.0f };
    const Rectangle2D FilterBtnR{ SearchBoxR.Right() + 6.0f, CurY, FilterBtnW, 28.0f };

    Surface.FillRectangle(SearchBoxR, ColorRgba{ 19u, 20u, 23u, 255u }, 14.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::SearchGlass,
        VectorIconPlacement{ SearchBoxR.X + 10.0f, SearchBoxR.Y + 8.0f, 12.0f, Theme.TextFaint, 1.6f });

    if (SearchBuffer.Length == 0u && !SearchBuffer.Active)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SearchBoxR.X + 27.0f, SearchBoxR.Y + 6.5f }, 9.0f, Theme.TextFaint, "Search layers...");
    }
    const ControlContact SearchContact = TextInputField::Construct(
        Surface,
        { SearchBoxR.X + 24.0f, SearchBoxR.Y + 1.0f, SearchBoxR.Width - 28.0f, SearchBoxR.Height - 2.0f },
        SearchBuffer,
        Input.PrimaryPointer,
        9.4f);
    if (SearchContact.Clicked || SearchBuffer.Active || SearchBuffer.Finalized)
    {
        Session->SearchQuery.assign(SearchBuffer.Text, SearchBuffer.Length);
    }

    const char* FilterBtnLabel = "Filter";
    if (Session->LayerFilter == PaintFilterTab::HiddenOnly)      FilterBtnLabel = "Hidden";
    else if (Session->LayerFilter == PaintFilterTab::MaskedOnly) FilterBtnLabel = "Masked";
    else if (Session->LayerFilter == PaintFilterTab::LockedOnly) FilterBtnLabel = "Locked";

    if (ConstructHeaderActionCapsule(Surface, FilterBtnR, ControlIcon::Count, FilterBtnLabel, true, FilterSheetOpen, Theme, Cursor, Pressed))
    {
        FilterSheetOpen    = !FilterSheetOpen;
        AddLayerSheetOpen  = false;
        FooterAddSheetOpen = false;
    }
    CurY += 36.0f;

    // 4. Scrollable Outliner Tree Area (reserving 34px for action bar + 28px for telemetry strip)
    constexpr float ActionBarH  = 34.0f;
    constexpr float TelemetryH  = 28.0f;
    const float     TreeBottomY = Bounds.Bottom() - ActionBarH - TelemetryH;
    const Rectangle2D TreeViewportR{ Bounds.X, CurY, Bounds.Width, std::max(48.0f, TreeBottomY - CurY) };

    const bool PopupCapturing = AddLayerSheetOpen || FooterAddSheetOpen || FilterSheetOpen;
    if (!PopupCapturing && TreeViewportR.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY = ControlAppearance::AdvanceScrollOffset(
            ScrollOffsetY,
            Input.WheelDeltaY * 32.0f,
            TreeViewportR.Height + MaxScrollY,
            TreeViewportR.Height);
    }
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScrollY);

    Surface.PushClip(TreeViewportR);
    float RowY = TreeViewportR.Y + 2.0f - ScrollOffsetY;
    constexpr float RowH = 38.0f;
    const bool AllowTreeClicks = !PopupCapturing && TreeViewportR.Contains(Cursor);

    // 4A. Root Mesh Row ("Utah Teapot", "4 parts · 512 px document", badge "512")
    char ResTagBuf[16];
    if (Session->ResolutionPx >= 1024u)
    {
        std::snprintf(ResTagBuf, sizeof(ResTagBuf), "%uK", Session->ResolutionPx / 1024u);
    }
    else
    {
        std::snprintf(ResTagBuf, sizeof(ResTagBuf), "%u", Session->ResolutionPx);
    }
    char MeshMetaBuf[64];
    std::snprintf(MeshMetaBuf, sizeof(MeshMetaBuf), "%u parts · %u px document", ActiveStudy.PartCount, Session->ResolutionPx);

    const OutlinerRowInteraction MeshRowAct = ConstructOutlinerTreeRow(
        Surface,
        { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, RowH },
        0u,
        true,
        Session->MeshPartsExpanded,
        false,
        false,
        ControlIcon::CubeShape,
        Theme.TextMuted,
        ActiveStudy.Label,
        MeshMetaBuf,
        {},
        ResTagBuf,
        -1,
        -1,
        -1,
        Theme,
        Cursor,
        AllowTreeClicks && Pressed,
        false);
    if (MeshRowAct.CaretToggled || MeshRowAct.RowSelected)
    {
        Session->MeshPartsExpanded = !Session->MeshPartsExpanded;
    }
    RowY += RowH + 2.0f;

    if (Session->MeshPartsExpanded)
    {
        const auto& PartNames = TexturePaintSession::QueryTeapotPartLabels();
        const uint32_t ShowParts = std::min<uint32_t>(ActiveStudy.PartCount, static_cast<uint32_t>(PartNames.size()));
        for (uint32_t P = 0u; P < ShowParts; ++P)
        {
            std::size_t FilteredCount = 0u;
            for (const auto& L : Session->Layers)
            {
                if (L.PartFilter == static_cast<int32_t>(P)) ++FilteredCount;
            }
            char PartMeta[48];
            std::snprintf(PartMeta, sizeof(PartMeta), "%zu layers filtered", FilteredCount);
            (void)ConstructOutlinerTreeRow(
                Surface,
                { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, 34.0f },
                1u,
                false,
                false,
                false,
                false,
                ControlIcon::ShapeCircle,
                Theme.TextFaint,
                PartNames[P],
                PartMeta,
                {},
                {},
                -1,
                -1,
                -1,
                Theme,
                Cursor,
                false,
                false);
            RowY += 35.0f;
        }
    }

    // 4B. Group Row ("Layer stack", "top of the stack first")
    (void)ConstructOutlinerTreeRow(
        Surface,
        { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, RowH },
        0u,
        true,
        true,
        false,
        false,
        ControlIcon::LayersSlabs,
        Theme.TextMuted,
        "Layer stack",
        "top of the stack first",
        {},
        {},
        -1,
        -1,
        -1,
        Theme,
        Cursor,
        false,
        false);
    RowY += RowH + 2.0f;

    // 4C. Layer Rows (Top of the stack first: rbegin -> rend)
    const auto& ChannelReg = TexturePaintSession::QueryChannelRegistry();
    for (auto It = Session->Layers.rbegin(); It != Session->Layers.rend(); ++It)
    {
        PaintLayerEntry& Layer = *It;
        if (!MatchesSearchAndFilter(Layer, Session->LayerFilter, Session->SearchQuery))
        {
            continue;
        }
        if (Layer.FolderOwnerId != 0u)
        {
            bool FolderCollapsed = false;
            for (const auto& F : Session->Layers)
            {
                if (F.Id == Layer.FolderOwnerId && !F.FolderExpanded)
                {
                    FolderCollapsed = true;
                    break;
                }
            }
            if (FolderCollapsed)
            {
                continue;
            }
        }

        const uint32_t DepthLevel = (Layer.FolderOwnerId != 0u) ? 2u : 1u;
        const bool     Selected   = (Layer.Id == Session->SelectedLayerId);
        const bool     HasNested  = (Layer.Category == PaintLayerCategory::Folder) ||
                                    Layer.Mask.Attached ||
                                    (Layer.Category == PaintLayerCategory::Gradient && !Layer.GradientPoints.empty()) ||
                                    (!Layer.Stamps.empty());
        const bool     IsExpanded = (Layer.Category == PaintLayerCategory::Folder)
            ? Layer.FolderExpanded
            : Layer.ExpandedInOutliner;

        std::vector<ColorRgba> PeekTints;
        PeekTints.reserve(5u);
        for (std::size_t C = 0u; C < TexturePaintSession::ChannelCount && PeekTints.size() < 5u; ++C)
        {
            if (Layer.ActiveChannels[C])
            {
                PeekTints.push_back(
                    C == static_cast<std::size_t>(PaintSurfaceChannel::Albedo)
                        ? Layer.PrimerTint
                        : ChannelReg[C].SwatchTint);
            }
        }

        Rectangle2D IconSlotR {};
        const std::string SubtitleStr = Layer.FormatSubtitle();
        const OutlinerRowInteraction RowAct = ConstructOutlinerTreeRow(
            Surface,
            { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, RowH },
            DepthLevel,
            HasNested,
            IsExpanded,
            Selected,
            !Layer.Visible,
            ControlIcon::Count,
            Theme.TextPrimary,
            Layer.Name,
            SubtitleStr,
            PeekTints,
            Layer.Mask.Attached ? "MASK" : "",
            Layer.Locked ? 1 : 0,
            -1,
            Layer.Visible ? 1 : 0,
            Theme,
            Cursor,
            AllowTreeClicks && Pressed,
            AllowTreeClicks && Input.DoubleClicked,
            &IconSlotR);

        ConstructLayerThumbnailTile(Surface, *Session, Layer, IconSlotR, Theme);

        if (RenamingLayerId == Layer.Id)
        {
            const Rectangle2D EditR{ IconSlotR.Right() + 6.0f, RowY + 6.0f, Bounds.Width - IconSlotR.Right() - 68.0f, 24.0f };
            Surface.FillRectangle(EditR, Theme.WellSurface, 6.0f);
            Surface.StrokeRectangle(EditR, Theme.BorderStrong, 6.0f, 1.0f);
            (void)TextInputField::Construct(Surface, EditR, RenameBuffer, Input.PrimaryPointer, 10.0f);
            if (RenameBuffer.Length > 0u)
            {
                Layer.Name.assign(RenameBuffer.Text, RenameBuffer.Length);
            }
            if ((Pressed && !EditR.Contains(Cursor)) || RenameBuffer.Finalized || !RenameBuffer.Active)
            {
                RenamingLayerId = 0u;
            }
        }

        if (RowAct.CaretToggled)
        {
            if (Layer.Category == PaintLayerCategory::Folder)
            {
                Layer.FolderExpanded = !Layer.FolderExpanded;
            }
            else
            {
                Layer.ExpandedInOutliner = !Layer.ExpandedInOutliner;
            }
        }
        if (RowAct.EyeToggled)
        {
            Layer.Visible = !Layer.Visible;
            Session->MarkContentModified();
        }
        if (RowAct.LockToggled)
        {
            Layer.Locked = !Layer.Locked;
        }
        if (RowAct.RowSelected)
        {
            Session->SelectedLayerId = Layer.Id;
            if (RowAct.DoubleClicked && !Layer.Locked)
            {
                RenamingLayerId = Layer.Id;
                RenameBuffer.Begin(Layer.Name.c_str());
            }
        }
        RowY += RowH + 2.0f;

        // 4D. Nested child rows (Mask, Gradient points + Background, Stamps)
        if (HasNested && IsExpanded)
        {
            if (Layer.Mask.Attached)
            {
                const char* MaskGenName = "Manual paint · 100%";
                if (Layer.Mask.Generator == PaintMaskGenerator::EdgeWear)         MaskGenName = "Edge wear · 100%";
                else if (Layer.Mask.Generator == PaintMaskGenerator::CreviceDirt) MaskGenName = "Crevice dirt · 100%";
                else if (Layer.Mask.Generator == PaintMaskGenerator::TopDust)     MaskGenName = "Top dust · 100%";

                const OutlinerRowInteraction MaskRowAct = ConstructOutlinerTreeRow(
                    Surface,
                    { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, 34.0f },
                    DepthLevel + 1u,
                    false,
                    false,
                    Selected && Layer.Mask.EditingMask,
                    !Layer.Mask.Enabled,
                    ControlIcon::MaskCutout,
                    Theme.TextPrimary,
                    "Mask",
                    MaskGenName,
                    {},
                    Layer.Mask.Invert ? "INV" : "",
                    -1,
                    Layer.Mask.Enabled ? 1 : 0,
                    -1,
                    Theme,
                    Cursor,
                    AllowTreeClicks && Pressed,
                    false);
                if (MaskRowAct.StatusToggled)
                {
                    Layer.Mask.Enabled = !Layer.Mask.Enabled;
                    Session->MarkContentModified();
                }
                if (MaskRowAct.RowSelected)
                {
                    Session->SelectedLayerId = Layer.Id;
                    Layer.Mask.EditingMask   = true;
                }
                RowY += 35.0f;
            }

            if (Layer.Category == PaintLayerCategory::Gradient)
            {
                for (std::size_t PtIdx = 0u; PtIdx < Layer.GradientPoints.size(); ++PtIdx)
                {
                    const auto& Pt = Layer.GradientPoints[PtIdx];
                    char PtMeta[64];
                    std::snprintf(
                        PtMeta,
                        sizeof(PtMeta),
                        "rough %d · metal %d · height %d",
                        static_cast<int>(std::lround(Pt.Roughness * 100.0f)),
                        static_cast<int>(std::lround(Pt.Metalness * 100.0f)),
                        static_cast<int>(std::lround(Pt.Height * 100.0f)));
                    Rectangle2D PtIconSlot {};
                    const bool PtSelected = Selected && (Layer.SelectedGradientPointIndex == static_cast<int32_t>(PtIdx));
                    const OutlinerRowInteraction PtAct = ConstructOutlinerTreeRow(
                        Surface,
                        { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, 34.0f },
                        DepthLevel + 1u,
                        false,
                        false,
                        PtSelected,
                        false,
                        ControlIcon::Count,
                        Theme.TextPrimary,
                        Pt.Label,
                        PtMeta,
                        {},
                        {},
                        -1,
                        -1,
                        -1,
                        Theme,
                        Cursor,
                        AllowTreeClicks && Pressed,
                        false,
                        &PtIconSlot);
                    Surface.FillRectangle({ PtIconSlot.X + 1.0f, PtIconSlot.Y + 1.0f, 22.0f, 22.0f }, Pt.TintColor, 6.0f);
                    Surface.StrokeRectangle({ PtIconSlot.X + 1.0f, PtIconSlot.Y + 1.0f, 22.0f, 22.0f }, ColorRgba{ 255u, 255u, 255u, 36u }, 6.0f, 1.0f);
                    if (PtAct.RowSelected)
                    {
                        Session->SelectedLayerId         = Layer.Id;
                        Layer.SelectedGradientPointIndex = static_cast<int32_t>(PtIdx);
                    }
                    RowY += 35.0f;
                }

                char BgMeta[64];
                std::snprintf(
                    BgMeta,
                    sizeof(BgMeta),
                    "rough %d · metal %d · height %d",
                    static_cast<int>(std::lround(Layer.GradientBackgroundRoughness * 100.0f)),
                    static_cast<int>(std::lround(Layer.GradientBackgroundMetalness * 100.0f)),
                    static_cast<int>(std::lround(Layer.GradientBackgroundHeight * 100.0f)));
                Rectangle2D BgIconSlot {};
                const bool BgSelected = Selected && (Layer.SelectedGradientPointIndex == -1);
                const OutlinerRowInteraction BgAct = ConstructOutlinerTreeRow(
                    Surface,
                    { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, 34.0f },
                    DepthLevel + 1u,
                    false,
                    false,
                    BgSelected,
                    false,
                    ControlIcon::Count,
                    Theme.TextPrimary,
                    "Background",
                    BgMeta,
                    {},
                    {},
                    -1,
                    -1,
                    -1,
                    Theme,
                    Cursor,
                    AllowTreeClicks && Pressed,
                    false,
                    &BgIconSlot);
                Surface.FillRectangle({ BgIconSlot.X + 1.0f, BgIconSlot.Y + 1.0f, 22.0f, 22.0f }, Layer.GradientBackgroundTint, 6.0f);
                Surface.StrokeRectangle({ BgIconSlot.X + 1.0f, BgIconSlot.Y + 1.0f, 22.0f, 22.0f }, ColorRgba{ 255u, 255u, 255u, 36u }, 6.0f, 1.0f);
                if (BgAct.RowSelected)
                {
                    Session->SelectedLayerId         = Layer.Id;
                    Layer.SelectedGradientPointIndex = -1;
                }
                RowY += 35.0f;
            }

            for (auto& St : Layer.Stamps)
            {
                char StMeta[64];
                std::snprintf(
                    StMeta,
                    sizeof(StMeta),
                    "%s · %d%% · %.0f deg",
                    St.ThreeDProj ? "3D" : "UV",
                    static_cast<int>(std::lround(St.Scale * 100.0f)),
                    St.RotationDeg);
                Rectangle2D StIconSlot {};
                const OutlinerRowInteraction StAct = ConstructOutlinerTreeRow(
                    Surface,
                    { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, 34.0f },
                    DepthLevel + 1u,
                    false,
                    false,
                    Selected && Layer.SelectedStampId == St.Id,
                    !St.Visible,
                    ControlIcon::Count,
                    St.TintColor,
                    St.Name,
                    StMeta,
                    {},
                    {},
                    -1,
                    -1,
                    St.Visible ? 1 : 0,
                    Theme,
                    Cursor,
                    AllowTreeClicks && Pressed,
                    false,
                    &StIconSlot);
                Surface.FillCircle({ StIconSlot.X + 12.0f, StIconSlot.Y + 12.0f }, 5.5f, St.TintColor);
                if (StAct.EyeToggled)
                {
                    St.Visible = !St.Visible;
                    Session->MarkContentModified();
                }
                if (StAct.RowSelected)
                {
                    Session->SelectedLayerId = Layer.Id;
                    Layer.SelectedStampId    = St.Id;
                }
                RowY += 35.0f;
            }
        }
    }

    const float ContentH = (RowY + ScrollOffsetY) - TreeViewportR.Y + 8.0f;
    MaxScrollY = std::max(0.0f, ContentH - TreeViewportR.Height);
    Surface.PopClip();

    // 5. Outliner Footer Action Bar (8 icon buttons: +, ↑, ↓, duplicate, eye, lock, mask ... trash)
    const Rectangle2D ActionBarR{ Bounds.X + 4.0f, TreeBottomY, Bounds.Width - 8.0f, ActionBarH };

    PaintLayerEntry* SelLayer = Session->AccessSelectedLayer();
    const bool HasSel         = (SelLayer != nullptr);
    const bool SelVis         = HasSel && SelLayer->Visible;
    const bool SelLocked      = HasSel && SelLayer->Locked;
    const bool SelMasked      = HasSel && SelLayer->Mask.Attached;

    float ActX = ActionBarR.X + 8.0f;
    const float ActY = ActionBarR.Y + 4.0f;
    constexpr float BtnSz = 26.0f;
    constexpr float BtnStep = 29.0f;

    const Rectangle2D FooterAddBtnR{ ActX, ActY, BtnSz, BtnSz };
    if (ConstructHeaderIconButton(Surface, FooterAddBtnR, ControlIcon::PlusAdd, FooterAddSheetOpen, true, Theme, Cursor, Pressed))
    {
        FooterAddSheetOpen = !FooterAddSheetOpen;
        AddLayerSheetOpen  = false;
        FilterSheetOpen    = false;
    }
    ActX += BtnStep;

    if (ConstructHeaderIconButton(Surface, { ActX, ActY, BtnSz, BtnSz }, ControlIcon::ChevronUp, false, Session->QueryMovableUp(), Theme, Cursor, Pressed))
    {
        Session->MoveSelectedLayerUp();
    }
    ActX += BtnStep;

    if (ConstructHeaderIconButton(Surface, { ActX, ActY, BtnSz, BtnSz }, ControlIcon::ChevronDown, false, Session->QueryMovableDown(), Theme, Cursor, Pressed))
    {
        Session->MoveSelectedLayerDown();
    }
    ActX += BtnStep;

    if (ConstructHeaderIconButton(Surface, { ActX, ActY, BtnSz, BtnSz }, ControlIcon::CopyDuplicate, false, HasSel, Theme, Cursor, Pressed))
    {
        Session->DuplicateSelectedLayer();
    }
    ActX += BtnStep;

    if (ConstructHeaderIconButton(Surface, { ActX, ActY, BtnSz, BtnSz }, SelVis ? ControlIcon::EyeVisible : ControlIcon::EyeHidden, false, HasSel, Theme, Cursor, Pressed))
    {
        if (SelLayer != nullptr)
        {
            SelLayer->Visible = !SelLayer->Visible;
            Session->MarkContentModified();
        }
    }
    ActX += BtnStep;

    if (ConstructHeaderIconButton(Surface, { ActX, ActY, BtnSz, BtnSz }, SelLocked ? ControlIcon::LockClosed : ControlIcon::LockOpen, SelLocked, HasSel, Theme, Cursor, Pressed))
    {
        if (SelLayer != nullptr)
        {
            SelLayer->Locked = !SelLayer->Locked;
        }
    }
    ActX += BtnStep;

    if (ConstructHeaderIconButton(Surface, { ActX, ActY, BtnSz, BtnSz }, ControlIcon::MaskCutout, SelMasked, HasSel && !SelLocked, Theme, Cursor, Pressed))
    {
        Session->ToggleOrCreateMaskOnSelectedLayer();
    }

    const Rectangle2D TrashBtnR{ ActionBarR.Right() - 8.0f - BtnSz, ActY, BtnSz, BtnSz };
    if (ConstructHeaderIconButton(Surface, TrashBtnR, ControlIcon::TrashDelete, false, HasSel && !SelLocked && Session->Layers.size() > 1u, Theme, Cursor, Pressed))
    {
        Session->DeleteSelectedLayer();
    }

    // 6. Telemetry Strip ("RES 512 | LAYERS 2 | MASKS 0 | PAINTED 4")
    const Rectangle2D TelemetryR{ Bounds.X, Bounds.Bottom() - TelemetryH, Bounds.Width, TelemetryH };
    char LayersCountStr[16];
    std::snprintf(LayersCountStr, sizeof(LayersCountStr), "%zu", Session->CountNonFolderLayers());
    char MasksCountStr[16];
    std::snprintf(MasksCountStr, sizeof(MasksCountStr), "%zu", Session->CountAttachedMasks());
    char PaintedCountStr[16];
    std::snprintf(PaintedCountStr, sizeof(PaintedCountStr), "%zu", Session->CountPaintedChannels());

    const std::array<std::pair<std::string_view, std::string_view>, 4u> TelemetryCols {{
        { "RES",     ResTagBuf       },
        { "LAYERS",  LayersCountStr  },
        { "MASKS",   MasksCountStr   },
        { "PAINTED", PaintedCountStr },
    }};
    ConstructOutlinerTelemetryStrip(Surface, TelemetryR, TelemetryCols, Theme);

    // 7. Overlay Popups (Top "+ Add v" sheet, Footer "[+]" upward sheet, or "Filter v" sheet)
    if (AddLayerSheetOpen)
    {
        const float SheetW = std::min(228.0f, InnerW);
        const float SheetH = static_cast<float>(AddLayerCatalog.size()) * 36.0f + 12.0f;
        const Rectangle2D SheetR{ Bounds.Right() - PadX - SheetW, AddBtnR.Bottom() + 4.0f, SheetW, SheetH };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 12.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 12.0f, 1.0f);

        for (std::size_t I = 0u; I < AddLayerCatalog.size(); ++I)
        {
            const auto& Spec = AddLayerCatalog[I];
            const Rectangle2D ItemR{ SheetR.X + 6.0f, SheetR.Y + 6.0f + static_cast<float>(I) * 36.0f, SheetR.Width - 12.0f, 34.0f };
            const bool ItemHover = ItemR.Contains(Cursor);
            if (ItemHover)
            {
                Surface.FillRectangle(ItemR, Theme.HoverSurface, 8.0f);
            }
            VectorIconRepository::ConstructControl(
                Surface,
                Spec.Symbol,
                VectorIconPlacement{ ItemR.X + 8.0f, ItemR.Y + 9.0f, 15.0f, Theme.TextPrimary, 1.7f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::SemiBold, { ItemR.X + 30.0f, ItemR.Y + 4.0f }, 10.0f, Theme.TextPrimary, Spec.TitleText);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { ItemR.X + 30.0f, ItemR.Y + 18.0f }, ItemR.Width - 36.0f, 8.2f, Theme.TextFaint, Spec.HintText);

            if (Pressed && ItemHover)
            {
                Session->AddLayer(Spec.Category);
                AddLayerSheetOpen = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !AddBtnR.Contains(Cursor))
        {
            AddLayerSheetOpen = false;
        }
    }

    if (FooterAddSheetOpen)
    {
        const float SheetW = 168.0f;
        const float SheetH = static_cast<float>(AddLayerCatalog.size()) * 27.0f + 10.0f;
        const Rectangle2D SheetR{ ActionBarR.X + 8.0f, ActionBarR.Y - SheetH - 4.0f, SheetW, SheetH };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);

        for (std::size_t I = 0u; I < AddLayerCatalog.size(); ++I)
        {
            const auto& Spec = AddLayerCatalog[I];
            const Rectangle2D ItemR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 27.0f, SheetR.Width - 10.0f, 25.0f };
            const bool ItemHover = ItemR.Contains(Cursor);
            if (ItemHover)
            {
                Surface.FillRectangle(ItemR, Theme.HoverSurface, 6.0f);
            }
            VectorIconRepository::ConstructControl(
                Surface,
                Spec.Symbol,
                VectorIconPlacement{ ItemR.X + 7.0f, ItemR.Y + 5.5f, 13.5f, Theme.TextPrimary, 1.7f });
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Medium, { ItemR.X + 26.0f, ItemR.Y + 5.0f }, ItemR.Width - 32.0f, 9.6f, Theme.TextPrimary, Spec.FooterLabel);

            if (Pressed && ItemHover)
            {
                Session->AddLayer(Spec.Category);
                FooterAddSheetOpen = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !FooterAddBtnR.Contains(Cursor))
        {
            FooterAddSheetOpen = false;
        }
    }

    if (FilterSheetOpen)
    {
        constexpr std::array<const char*, 4u> FilterNames {{
            "All layers",
            "Hidden only",
            "With mask",
            "Locked only",
        }};
        constexpr std::array<PaintFilterTab, 4u> FilterModes {{
            PaintFilterTab::AllLayers,
            PaintFilterTab::HiddenOnly,
            PaintFilterTab::MaskedOnly,
            PaintFilterTab::LockedOnly,
        }};
        const Rectangle2D SheetR{ FilterBtnR.Right() - 132.0f, FilterBtnR.Bottom() + 4.0f, 132.0f, 114.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (std::size_t I = 0u; I < FilterNames.size(); ++I)
        {
            const Rectangle2D ItemR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 26.0f, SheetR.Width - 10.0f, 24.0f };
            const bool Active = (Session->LayerFilter == FilterModes[I]);
            const bool Hover  = ItemR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(ItemR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                Active ? FontWeightClass::SemiBold : FontWeightClass::Regular,
                { ItemR.X + 8.0f, ItemR.Y + 4.5f },
                9.5f,
                Theme.TextPrimary,
                FilterNames[I]);
            if (Pressed && Hover)
            {
                Session->LayerFilter = FilterModes[I];
                FilterSheetOpen      = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !FilterBtnR.Contains(Cursor))
        {
            FilterSheetOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
