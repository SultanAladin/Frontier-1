/*============================================================================================================================================
                                                        PATTERNSTAMPLAYERSTACKPANE.CPP
============================================================================================================================================*/
// 🧩 Left-docked Pattern/Stamp Layer Stack renderer: Add Layer overlay sheet, stat filters, search, tree rows, masks, toolbar & footer.

#include "Engine/Workspaces/Texturing/PatternStampLayerStackPane.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

namespace
{

std::string FormatTwoDigits(std::size_t N) noexcept
{
    char Buf[16];
    std::snprintf(Buf, sizeof(Buf), "%02zu", N);
    return std::string(Buf);
}

std::string FormatHexColor(ColorRgba Rgba) noexcept
{
    char Buf[16];
    FormatHexLower(Rgba, Buf, sizeof(Buf));
    return std::string(Buf);
}

std::string ToLowerAscii(std::string_view S) noexcept
{
    std::string Out(S);
    for (char& C : Out)
    {
        if (C >= 'A' && C <= 'Z')
        {
            C = static_cast<char>(C + ('a' - 'A'));
        }
    }
    return Out;
}

void AssignEditText(TextEditBuffer& Buf, std::string_view TextContent) noexcept
{
    const std::size_t CopyLen = std::min(TextContent.size(), static_cast<std::size_t>(TextEditBuffer::Capacity - 1u));
    for (std::size_t I = 0u; I < CopyLen; ++I)
    {
        Buf.Text[I] = TextContent[I];
    }
    Buf.Text[CopyLen]   = '\0';
    Buf.Length          = static_cast<uint32_t>(CopyLen);
    Buf.Caret           = Buf.Length;
    Buf.SelectionAnchor = Buf.Length;
}

void ClearEditText(TextEditBuffer& Buf) noexcept
{
    Buf.Text[0]         = '\0';
    Buf.Length          = 0u;
    Buf.Caret           = 0u;
    Buf.SelectionAnchor = 0u;
}

ControlIcon QueryMotifPreviewIcon(const PatternStampLayerSpec& Layer) noexcept
{
    if (Layer.Category == PatternStampLayerCategory::Folder)       return ControlIcon::FolderOpen;
    if (Layer.Category == PatternStampLayerCategory::Typographic)  return ControlIcon::TextType;
    if (Layer.Category == PatternStampLayerCategory::GradientWash) return ControlIcon::PointGradient;
    switch (Layer.MotifShape)
    {
        case PatternStampMotifShape::Ring:
        case PatternStampMotifShape::Circle:    return ControlIcon::ShapeCircle;
        case PatternStampMotifShape::Square:
        case PatternStampMotifShape::RoundedBox:return ControlIcon::ShapeSquare;
        case PatternStampMotifShape::Diamond:   return ControlIcon::ShapeDiamond;
        case PatternStampMotifShape::Hexagon:   return ControlIcon::ShapeHexagon;
        case PatternStampMotifShape::Triangle:  return ControlIcon::ShapeTriangle;
        case PatternStampMotifShape::Star:      return ControlIcon::ShapeStar;
        case PatternStampMotifShape::StripeBar:
        case PatternStampMotifShape::LineStroke:return ControlIcon::ShapeStripe;
        default:                           return ControlIcon::VectorCurve;
    }
}

void RenderSchematicLayerThumb(
    PixelSpace&                 Surface,
    Rectangle2D                 ThumbRect,
    const PatternStampLayerSpec&     Layer,
    const TexturingThemeColors& Theme) noexcept
{
    Surface.FillRectangle(ThumbRect, Theme.LightSurface ? Theme.NestedSurface : ColorRgba{ 16u, 18u, 22u, 255u }, 6.0f);
    Surface.StrokeRectangle(ThumbRect, Theme.BorderSubtle, 6.0f, 1.0f);

    if (Layer.Category == PatternStampLayerCategory::Folder)
    {
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::FolderOpen, VectorIconPlacement{ ThumbRect.X + 9.0f, ThumbRect.Y + 9.0f, 16.0f, Theme.HighlightFill, 1.6f });
        return;
    }

    const ColorRgba Ink = Layer.FillTint.Alpha > 20u ? Layer.FillTint : Theme.TextPrimary;
    const float     Cx  = ThumbRect.X + ThumbRect.Width * 0.5f;
    const float     Cy  = ThumbRect.Y + ThumbRect.Height * 0.5f;

    const bool ArrayModifierActive = std::any_of(
        Layer.Modifiers.begin(),
        Layer.Modifiers.end(),
        [](const PatternStampModifierSpec& M) { return M.Enabled && (M.Category == PatternStampModifierCategory::GridArray || M.Category == PatternStampModifierCategory::RadialArray); });

    if (ArrayModifierActive)
    {
        for (int Row = -1; Row <= 1; ++Row)
        {
            for (int Col = -1; Col <= 1; ++Col)
            {
                const float DotX = Cx + static_cast<float>(Col) * 8.0f + ((Row & 1) ? 2.0f : 0.0f);
                const float DotY = Cy + static_cast<float>(Row) * 8.0f;
                const float R    = (Row == 0 && Col == 0) ? 2.8f : 2.1f;
                if (Layer.MotifShape == PatternStampMotifShape::Ring)
                {
                    Surface.StrokeCircle({ DotX, DotY }, R, Ink, 1.2f);
                }
                else
                {
                    Surface.FillCircle({ DotX, DotY }, R, (Row == 0 && Col == 1) ? Layer.FillTintB : Ink);
                }
            }
        }
        return;
    }

    if (Layer.MotifShape == PatternStampMotifShape::Circle)
    {
        Surface.FillCircle({ Cx, Cy }, 10.5f, Ink);
        return;
    }
    if (Layer.MotifShape == PatternStampMotifShape::Ring)
    {
        Surface.StrokeCircle({ Cx, Cy }, 10.0f, Ink, 2.4f);
        return;
    }

    VectorIconRepository::ConstructControl(
        Surface,
        QueryMotifPreviewIcon(Layer),
        VectorIconPlacement{ ThumbRect.X + 8.0f, ThumbRect.Y + 8.0f, 18.0f, Ink, 1.8f });
}

void RenderSchematicMaskThumb(PixelSpace& Surface, Rectangle2D ThumbRect, const PatternStampLayerMaskSpec& Mask) noexcept
{
    const ColorRgba Bg = Mask.Invert ? ColorRgba{ 239u, 244u, 207u, 255u } : ColorRgba{ 20u, 22u, 26u, 255u };
    const ColorRgba Fg = Mask.Invert ? ColorRgba{ 20u, 22u, 26u, 255u } : ColorRgba{ 239u, 244u, 207u, 225u };
    Surface.FillRectangle(ThumbRect, Bg, 4.0f);

    if (Mask.Mode == PatternStampMaskMode::Radial)
    {
        Surface.FillCircle({ ThumbRect.X + ThumbRect.Width * 0.5f, ThumbRect.Y + ThumbRect.Height * 0.5f }, ThumbRect.Width * 0.30f, Fg);
    }
    else if (Mask.Mode == PatternStampMaskMode::Stripe)
    {
        Surface.FillRectangle({ ThumbRect.X + 4.0f, ThumbRect.Y + 2.0f, 4.0f, ThumbRect.Height - 4.0f }, Fg, 0.0f);
        Surface.FillRectangle({ ThumbRect.X + 12.0f, ThumbRect.Y + 2.0f, 4.0f, ThumbRect.Height - 4.0f }, Fg, 0.0f);
    }
    else
    {
        Surface.FillCircle({ ThumbRect.X + 7.5f, ThumbRect.Y + 8.5f }, 4.2f, Fg);
        Surface.FillCircle({ ThumbRect.X + 14.5f, ThumbRect.Y + 14.0f }, 3.8f, Fg);
        Surface.FillCircle({ ThumbRect.X + 14.5f, ThumbRect.Y + 6.5f }, 2.4f, Fg);
    }
    Surface.StrokeRectangle(ThumbRect, ColorRgba{ 255u, 255u, 255u, 45u }, 4.0f, 1.0f);
}

bool RenderToolIconButton(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    ControlIcon                 Icon,
    bool                        Enabled,
    bool                        Highlighted,
    bool                        DangerTint,
    const TexturingThemeColors& Theme,
    Point2D                     Cursor,
    bool                        Pressed) noexcept
{
    const bool Hovered = Enabled && Bounds.Contains(Cursor);
    ColorRgba Fill = Highlighted
        ? Theme.HighlightFill
        : (Hovered ? Theme.HoverSurface : Theme.RaisedSurface);
    if (!Enabled)
    {
        Fill.Alpha = 115u;
    }
    ColorRgba Stroke = Highlighted
        ? Theme.HighlightFill
        : (Hovered ? (DangerTint ? ColorRgba{ 232u, 110u, 110u, 190u } : Theme.BorderStrong) : Theme.BorderSubtle);
    ColorRgba Ink = Highlighted
        ? Theme.HighlightInk
        : (DangerTint && Hovered ? ColorRgba{ 244u, 130u, 130u, 255u } : (Enabled ? Theme.TextPrimary : Theme.TextFaint));

    Surface.FillRectangle(Bounds, Fill, Bounds.Height * 0.5f);
    Surface.StrokeRectangle(Bounds, Stroke, Bounds.Height * 0.5f, 1.0f);
    const float IconSz = std::min(14.0f, Bounds.Height * 0.52f);
    VectorIconRepository::ConstructControl(
        Surface,
        Icon,
        VectorIconPlacement{
            Bounds.X + (Bounds.Width - IconSz) * 0.5f,
            Bounds.Y + (Bounds.Height - IconSz) * 0.5f,
            IconSz,
            Ink,
            1.7f });
    return Hovered && Pressed;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

PatternStampLayerStackPane::~PatternStampLayerStackPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void PatternStampLayerStackPane::AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept
{
    Session            = TargetSession;
    AddLayerSheetOpen  = false;
    RenamingLayerId    = 0u;
    LastClickLayerId   = 0u;
    TimeSinceLastClick = 10.0;
    ClearEditText(SearchBuffer);
    SearchBuffer.Active = false;
    if (Session != nullptr)
    {
        Session->LayerSearch.clear();
    }
}

void PatternStampLayerStackPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##PatternStampLayerStackCanvas",
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

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANE RENDERING
//------------------------------------------------------------------------------------------------------------------------

void PatternStampLayerStackPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    if (Session == nullptr || Bounds.Width < 120.0f || Bounds.Height < 160.0f)
    {
        return;
    }

    TimeSinceLastClick += std::max(0.0, DeltaSeconds);
    const TexturingThemeColors Theme = QueryTexturingThemeColors();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);
    Surface.StrokeLine({ Bounds.Right(), Bounds.Y }, { Bounds.Right(), Bounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    const float   PadX    = 12.0f;
    const float   InnerW  = Bounds.Width - PadX * 2.0f;
    const float   LeftX   = Bounds.X + PadX;
    float         CursorY = Bounds.Y + 12.0f;

    // 1. Heading row ("Layers  2 / 32" + New Folder circle + Add Layer '+' circle)
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { LeftX, CursorY + 4.0f }, 15.0f, Theme.TextPrimary, "Layers");

    const std::string CountLabel = std::to_string(Session->Document.Layers.size()) + " / " + std::to_string(PatternStampWorkspaceSession::MaxLayerLimit);
    const float CountW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, CountLabel) + 14.0f;
    const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 15.0f, "Layers");
    const Rectangle2D CountRect{ LeftX + TitleW + 8.0f, CursorY + 5.0f, CountW, 20.0f };
    Surface.FillRectangle(CountRect, Theme.RaisedSurface, 10.0f);
    Surface.StrokeRectangle(CountRect, Theme.BorderSubtle, 10.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { CountRect.X + 7.0f, CountRect.Y + 4.0f }, 9.8f, Theme.TextMuted, CountLabel);

    const Rectangle2D AddBtnRect{ Bounds.Right() - PadX - 28.0f, CursorY, 28.0f, 28.0f };
    const Rectangle2D FolderBtnRect{ AddBtnRect.X - 34.0f, CursorY, 28.0f, 28.0f };
    if (RenderToolIconButton(Surface, FolderBtnRect, ControlIcon::FolderPlus, true, false, false, Theme, Cursor, Pressed))
    {
        AddLayerSheetOpen = false;
        Session->AddLayer(PatternStampLayerCategory::Folder);
    }
    if (RenderToolIconButton(
            Surface, AddBtnRect, AddLayerSheetOpen ? ControlIcon::CloseCross : ControlIcon::PlusAdd, true, AddLayerSheetOpen, false, Theme, Cursor, Pressed))
    {
        AddLayerSheetOpen = !AddLayerSheetOpen;
    }

    const float AddSheetAnchorBottomY = CursorY + 34.0f;
    CursorY += 38.0f;

    // 2. Visible / Hidden Stat Filter Boxes (2-column grid matching 01-HTML-pattern.png)
    const std::size_t TotalCount   = Session->Document.Layers.size();
    const std::size_t VisibleCount = Session->CountVisibleLayers();
    const std::size_t HiddenCount  = TotalCount >= VisibleCount ? (TotalCount - VisibleCount) : 0u;

    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const float StatH   = 56.0f;

    auto RenderStatBox = [&](Rectangle2D BoxRect, ControlIcon Icon, ColorRgba DiscFill, std::string_view SubLabel, std::size_t CountReading, bool ActiveFilter) -> bool
    {
        const bool Hovered = !AddLayerSheetOpen && BoxRect.Contains(Cursor);
        Surface.FillRectangle(BoxRect, ActiveFilter ? Theme.AccentSoft : (Hovered ? Theme.HoverSurface : Theme.SectionSurface), 10.0f);
        Surface.StrokeRectangle(BoxRect, ActiveFilter ? Theme.AccentBorder : (Hovered ? Theme.BorderStrong : Theme.BorderSubtle), 10.0f, 1.0f);

        const Rectangle2D DiscRect{ BoxRect.X + 10.0f, BoxRect.Y + 10.0f, 16.0f, 16.0f };
        Surface.FillCircle({ DiscRect.X + 8.0f, DiscRect.Y + 8.0f }, 8.0f, DiscFill);
        VectorIconRepository::ConstructControl(
            Surface, Icon, VectorIconPlacement{ DiscRect.X + 3.0f, DiscRect.Y + 3.0f, 10.0f, ColorRgba{ 18u, 22u, 18u, 255u }, 1.8f });

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { BoxRect.X + 10.0f, BoxRect.Y + 34.0f }, 10.0f, Theme.TextMuted, SubLabel);

        const std::string NumStr = FormatTwoDigits(CountReading);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface, FontWeightClass::Medium, { BoxRect.Right() - 12.0f, BoxRect.Y + 20.0f }, 18.0f, Theme.TextPrimary, NumStr);

        return Hovered && Pressed;
    };

    if (RenderStatBox(
            { LeftX, CursorY, StatW, StatH },
            ControlIcon::CircleCheck,
            ColorRgba{ 118u, 198u, 126u, 230u },
            "Visible",
            VisibleCount,
            Session->LayerFilter == PatternStampLayerFilterMode::VisibleOnly))
    {
        Session->LayerFilter = (Session->LayerFilter == PatternStampLayerFilterMode::VisibleOnly)
            ? PatternStampLayerFilterMode::AllLayers
            : PatternStampLayerFilterMode::VisibleOnly;
    }

    if (RenderStatBox(
            { LeftX + StatW + StatGap, CursorY, StatW, StatH },
            ControlIcon::EyeHidden,
            ColorRgba{ 115u, 120u, 130u, 140u },
            "Hidden",
            HiddenCount,
            Session->LayerFilter == PatternStampLayerFilterMode::HiddenOnly))
    {
        Session->LayerFilter = (Session->LayerFilter == PatternStampLayerFilterMode::HiddenOnly)
            ? PatternStampLayerFilterMode::AllLayers
            : PatternStampLayerFilterMode::HiddenOnly;
    }
    CursorY += StatH + 8.0f;

    // Optional filter status bar
    const bool FilterActive = (Session->LayerFilter != PatternStampLayerFilterMode::AllLayers) || !Session->LayerSearch.empty();
    if (FilterActive)
    {
        const Rectangle2D NoteR{ LeftX, CursorY, InnerW, 24.0f };
        Surface.FillRectangle(NoteR, Theme.AccentSoft, 6.0f);
        std::string NoteMsg = "Filtered:";
        if (Session->LayerFilter == PatternStampLayerFilterMode::VisibleOnly) NoteMsg += " Visible layers";
        else if (Session->LayerFilter == PatternStampLayerFilterMode::HiddenOnly) NoteMsg += " Hidden layers";
        if (!Session->LayerSearch.empty()) NoteMsg += " \"" + Session->LayerSearch + "\"";
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { NoteR.X + 8.0f, NoteR.Y + 5.0f }, InnerW - 64.0f, 9.2f, Theme.TextPrimary, NoteMsg);

        const Rectangle2D ResetR{ NoteR.Right() - 56.0f, NoteR.Y + 3.0f, 50.0f, 18.0f };
        if (ConstructCapsuleButtonControl(Surface, ResetR, "Show all", Theme, Cursor, Pressed && !AddLayerSheetOpen, false, false, ControlIcon::Count, 8.6f))
        {
            Session->LayerFilter = PatternStampLayerFilterMode::AllLayers;
            Session->LayerSearch.clear();
            ClearEditText(SearchBuffer);
            SearchBuffer.Active = false;
        }
        CursorY += 28.0f;
    }

    // 3. Search Bar ("Find a layer...")
    const Rectangle2D SearchRect{ LeftX, CursorY, InnerW, 30.0f };
    Surface.FillRectangle(SearchRect, SearchBuffer.Active ? Theme.HoverSurface : Theme.WellSurface, 15.0f);
    Surface.StrokeRectangle(SearchRect, SearchBuffer.Active ? Theme.BorderStrong : Theme.BorderSubtle, 15.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchRect.X + 10.0f, SearchRect.Y + 8.0f, 13.0f, Theme.TextFaint, 1.7f });

    if (Pressed && !AddLayerSheetOpen)
    {
        const Rectangle2D ClearBtnR{ SearchRect.Right() - 24.0f, SearchRect.Y + 4.0f, 20.0f, 22.0f };
        if (SearchBuffer.Length > 0u && ClearBtnR.Contains(Cursor))
        {
            ClearEditText(SearchBuffer);
            Session->LayerSearch.clear();
            SearchBuffer.Active = false;
        }
        else if (SearchRect.Contains(Cursor))
        {
            SearchBuffer.Active       = true;
            RenamingLayerId           = 0u;
            InlineRenameBuffer.Active = false;
        }
        else if (SearchBuffer.Active)
        {
            SearchBuffer.Active = false;
        }
    }
    if (SearchBuffer.Active)
    {
        WorkspaceAppearance::ForwardInputToTextBuffer(SearchBuffer, Input, DeltaSeconds);
        Session->LayerSearch = std::string(SearchBuffer.Text);
    }
    else if (std::string_view(SearchBuffer.Text) != Session->LayerSearch)
    {
        AssignEditText(SearchBuffer, Session->LayerSearch);
    }

    const std::string_view SearchDisplay = (SearchBuffer.Length == 0u) ? std::string_view("Find a layer...") : std::string_view(SearchBuffer.Text);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { SearchRect.X + 29.0f, SearchRect.Y + 8.0f },
        SearchRect.Width - 52.0f,
        10.2f,
        (SearchBuffer.Length == 0u) ? Theme.TextFaint : Theme.TextPrimary,
        SearchDisplay);
    if (SearchBuffer.Length > 0u)
    {
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::CloseCross, VectorIconPlacement{ SearchRect.Right() - 20.0f, SearchRect.Y + 9.0f, 11.0f, Theme.TextMuted, 1.6f });
    }
    CursorY += 38.0f;

    // Collect top -> bottom filtered entries
    struct VisibleEntry
    {
        uint32_t Identity = 0u;
        uint32_t Depth    = 0u;
        bool     EffVis   = true;
    };
    std::vector<VisibleEntry> Displayed;
    const std::string QueryLower = ToLowerAscii(Session->LayerSearch);
    const bool SearchOrFilterOn  = (Session->LayerFilter != PatternStampLayerFilterMode::AllLayers) || !QueryLower.empty();

    for (auto It = Session->Document.Layers.rbegin(); It != Session->Document.Layers.rend(); ++It)
    {
        const auto& L = *It;
        if (!SearchOrFilterOn && Session->QueryUnderCollapsedFolder(L.Identity))
        {
            continue;
        }
        const bool EffVis = Session->QueryEffectiveVisible(L.Identity);
        if (Session->LayerFilter == PatternStampLayerFilterMode::VisibleOnly && !EffVis) continue;
        if (Session->LayerFilter == PatternStampLayerFilterMode::HiddenOnly && EffVis) continue;
        if (!QueryLower.empty())
        {
            const std::string Hay = ToLowerAscii(L.Name + " " + PatternStampWorkspaceSession::DescribeLayerSubtitle(L));
            if (Hay.find(QueryLower) == std::string::npos)
            {
                continue;
            }
        }
        Displayed.push_back({ L.Identity, SearchOrFilterOn ? 0u : Session->QueryLayerDepth(L.Identity), EffVis });
    }

    // 4. "TOP -> BOTTOM" + "N SHOWN"
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { LeftX, CursorY }, 9.0f, Theme.TextFaint, "TOP -> BOTTOM");
    const std::string ShownLabel = std::to_string(Displayed.size()) + " SHOWN";
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface, FontWeightClass::Medium, { LeftX + InnerW, CursorY }, 9.0f, Theme.TextFaint, ShownLabel);
    CursorY += 18.0f;

    // 5. Reserve bottom blocks cleanly (Toolbar + Canvas Tile ground + 3-metric Pane Footer)
    const float FooterH     = 26.0f;
    const float GroundTileH = 44.0f;
    const float ToolbarH    = 34.0f;
    const float BottomBandY = Bounds.Bottom() - FooterH - GroundTileH - ToolbarH - 12.0f;
    const Rectangle2D ScrollRect{ Bounds.X, CursorY, Bounds.Width, std::max(40.0f, BottomBandY - CursorY) };

    if (!AddLayerSheetOpen && ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 0.001f)
    {
        const float MaxScroll = std::max(0.0f, CachedContentHeight - ScrollRect.Height);
        ScrollOffsetY         = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScroll);
    }

    const Point2D ListCursor = (!AddLayerSheetOpen && ScrollRect.Contains(Cursor)) ? Cursor : Point2D{ -9999.0f, -9999.0f };
    const bool    ListPress  = (!AddLayerSheetOpen && ScrollRect.Contains(Cursor)) && Pressed;

    if (!Input.PrimaryPointer.Down)
    {
        DraggingReorder  = false;
        ScrubbingOpacity = false;
    }

    // 6. Scrollable Layer Tree
    Surface.PushClip(ScrollRect);
    float RowCursorY          = ScrollRect.Y + 2.0f - ScrollOffsetY;
    const float RowTopAnchorY = RowCursorY;

    if (Displayed.empty())
    {
        const Rectangle2D EmptyR{ LeftX, RowCursorY + 8.0f, InnerW, 56.0f };
        Surface.FillRectangle(EmptyR, Theme.SectionSurface, 10.0f);
        Surface.StrokeRectangle(EmptyR, Theme.BorderSubtle, 10.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { EmptyR.X + 12.0f, EmptyR.Y + 11.0f }, 10.5f, Theme.TextPrimary, SearchOrFilterOn ? "No matching layers" : "No layers yet");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { EmptyR.X + 12.0f, EmptyR.Y + 30.0f }, 9.2f, Theme.TextFaint, SearchOrFilterOn ? "Clear filter or search query to view all." : "Use + above to add a layer.");
        RowCursorY += 70.0f;
    }
    else
    {
        struct PendingMoveOp
        {
            bool     Ready          = false;
            uint32_t Identity       = 0u;
            int32_t  StepDelta      = 0;
            bool     DragDrop       = false;
            uint32_t TargetIdentity = 0u;
            bool     DropAbove      = true;
        } PendingMove {};

        for (std::size_t VisIdx = 0u; VisIdx < Displayed.size(); ++VisIdx)
        {
            const VisibleEntry Entry = Displayed[VisIdx];
            PatternStampLayerSpec*  Layer = Session->FindLayerById(Entry.Identity);
            if (Layer == nullptr)
            {
                continue;
            }
            const bool SelectedRow = (Session->Document.SelectedLayerId == Layer->Identity);
            const bool FolderEntry   = (Layer->Category == PatternStampLayerCategory::Folder);
            const float IndentPx  = static_cast<float>(Entry.Depth) * 14.0f;
            const Rectangle2D RowRect{ LeftX + IndentPx, RowCursorY, std::max(120.0f, InnerW - IndentPx), 48.0f };
            const bool RowHover   = RowRect.Contains(ListCursor);

            ColorRgba RowFill   = SelectedRow ? Theme.HoverSurface : (RowHover ? Theme.RaisedSurface : Theme.SectionSurface);
            ColorRgba RowStroke = SelectedRow ? Theme.HighlightFill : (RowHover ? Theme.BorderStrong : Theme.BorderSubtle);
            if (!Entry.EffVis)
            {
                RowFill.Alpha = 145u;
            }
            Surface.FillRectangle(RowRect, RowFill, 8.0f);
            Surface.StrokeRectangle(RowRect, RowStroke, 8.0f, SelectedRow ? 1.2f : 1.0f);

            if (SelectedRow)
            {
                Surface.FillRectangle({ RowRect.X, RowRect.Y + 4.0f, 3.0f, RowRect.Height - 8.0f }, Theme.HighlightFill, 1.5f);
            }

            // Drag grip
            const Rectangle2D GripR{ RowRect.X + 6.0f, RowRect.Y + 8.0f, 14.0f, 32.0f };
            VectorIconRepository::ConstructControl(
                Surface,
                ControlIcon::DragGrip,
                VectorIconPlacement{ GripR.X + 1.0f, GripR.Y + 9.0f, 12.0f, GripR.Contains(ListCursor) ? Theme.TextPrimary : Theme.TextFaint, 1.6f });
            if (ListPress && GripR.Contains(ListCursor))
            {
                DraggingLayerId  = Layer->Identity;
                DragStartCursorY = ListCursor.Y;
                DraggingReorder  = true;
            }
            if (DraggingReorder && DraggingLayerId == Layer->Identity && Input.PrimaryPointer.Down)
            {
                if (ListCursor.Y < RowRect.Y - 12.0f && VisIdx > 0u)
                {
                    PendingMove = { true, Layer->Identity, 0, true, Displayed[VisIdx - 1u].Identity, true };
                }
                else if (ListCursor.Y > RowRect.Bottom() + 12.0f && VisIdx + 1u < Displayed.size())
                {
                    PendingMove = { true, Layer->Identity, 0, true, Displayed[VisIdx + 1u].Identity, false };
                }
            }

            // Disclosure chevron
            const Rectangle2D ChevR{ GripR.Right() + 2.0f, RowRect.Y + 14.0f, 16.0f, 20.0f };
            const bool ChevronExpanded = FolderEntry ? !Layer->CollapsedFolder : Layer->ExpandedInStack;
            VectorIconRepository::ConstructControl(
                Surface,
                ChevronExpanded ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
                VectorIconPlacement{ ChevR.X + 3.0f, ChevR.Y + 5.0f, 10.0f, ChevR.Contains(ListCursor) ? Theme.TextPrimary : Theme.TextFaint, 1.7f });
            bool ConsumedSubClick = false;
            if (ListPress && ChevR.Contains(ListCursor))
            {
                ConsumedSubClick = true;
                if (FolderEntry) Session->ToggleFolderCollapsed(Layer->Identity);
                else Session->ToggleLayerExpandedDrawer(Layer->Identity);
            }

            // Schematic 36x36 thumbnail
            const Rectangle2D ThumbR{ ChevR.Right() + 4.0f, RowRect.Y + 6.0f, 36.0f, 36.0f };
            RenderSchematicLayerThumb(Surface, ThumbR, *Layer, Theme);

            // Right control cluster
            const Rectangle2D LockR{ RowRect.Right() - 26.0f, RowRect.Y + 13.0f, 20.0f, 22.0f };
            const Rectangle2D EyeR{ LockR.X - 22.0f, RowRect.Y + 13.0f, 20.0f, 22.0f };
            const Rectangle2D UpArrowR{ EyeR.X - 18.0f, RowRect.Y + 11.0f, 14.0f, 13.0f };
            const Rectangle2D DnArrowR{ EyeR.X - 18.0f, RowRect.Y + 24.0f, 14.0f, 13.0f };
            const Rectangle2D OpacityChipR{ UpArrowR.X - 38.0f, RowRect.Y + 14.0f, 34.0f, 20.0f };

            // Name & Subtitle
            const float TextLeftX = ThumbR.Right() + 8.0f;
            const float TextMaxW  = std::max(36.0f, OpacityChipR.X - TextLeftX - 6.0f);
            if (RenamingLayerId == Layer->Identity)
            {
                const Rectangle2D RenameR{ TextLeftX, RowRect.Y + 7.0f, TextMaxW, 20.0f };
                Surface.FillRectangle(RenameR, Theme.WellSurface, 4.0f);
                Surface.StrokeRectangle(RenameR, Theme.HighlightFill, 4.0f, 1.0f);
                WorkspaceAppearance::ForwardInputToTextBuffer(InlineRenameBuffer, Input, DeltaSeconds);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Medium, { RenameR.X + 4.0f, RenameR.Y + 3.0f }, TextMaxW - 8.0f, 10.0f, Theme.TextPrimary, InlineRenameBuffer.Text);
                if (!InlineRenameBuffer.Active || (ListPress && !RenameR.Contains(ListCursor)))
                {
                    if (InlineRenameBuffer.Length > 0u)
                    {
                        Layer->Name = std::string(InlineRenameBuffer.Text);
                    }
                    RenamingLayerId           = 0u;
                    InlineRenameBuffer.Active = false;
                }
            }
            else
            {
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    FontWeightClass::SemiBold,
                    { TextLeftX, RowRect.Y + 8.0f },
                    TextMaxW,
                    10.5f,
                    Entry.EffVis ? Theme.TextPrimary : Theme.TextMuted,
                    Layer->Name);
                const std::string Sub = FolderEntry
                    ? ("Folder · " + std::to_string(Session->CountFolderImmediateMembers(Layer->Identity)) + " entries")
                    : PatternStampWorkspaceSession::DescribeLayerSubtitle(*Layer);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular, { TextLeftX, RowRect.Y + 25.0f }, TextMaxW, 8.8f, Theme.TextFaint, Sub);
            }

            if (!FolderEntry)
            {
                Surface.FillRectangle(OpacityChipR, OpacityChipR.Contains(ListCursor) ? Theme.HoverSurface : Theme.WellSurface, 6.0f);
                Surface.StrokeRectangle(OpacityChipR, Theme.BorderSubtle, 6.0f, 1.0f);
                const std::string OpStr = std::to_string(static_cast<int>(std::lround(Layer->Opacity * 100.0f))) + "%";
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular, { OpacityChipR.X + 5.0f, OpacityChipR.Y + 4.0f }, OpacityChipR.Width - 6.0f, 8.8f, Theme.TextPrimary, OpStr);
                if (ListPress && OpacityChipR.Contains(ListCursor) && !Layer->Locked)
                {
                    ConsumedSubClick   = true;
                    ScrubbingOpacityId = Layer->Identity;
                    ScrubbingOpacity   = true;
                    ScrubStartCursorX  = ListCursor.X;
                    ScrubStartOpacity  = Layer->Opacity;
                }
                if (ScrubbingOpacity && ScrubbingOpacityId == Layer->Identity && Input.PrimaryPointer.Down)
                {
                    Layer->Opacity = std::clamp(ScrubStartOpacity + (Input.PrimaryPointer.X - ScrubStartCursorX) / 110.0f, 0.0f, 1.0f);
                }
            }

            VectorIconRepository::ConstructControl(
                Surface,
                ControlIcon::ChevronUp,
                VectorIconPlacement{ UpArrowR.X + 2.0f, UpArrowR.Y + 1.0f, 10.0f, (VisIdx > 0u && UpArrowR.Contains(ListCursor)) ? Theme.TextPrimary : Theme.TextFaint, 1.6f });
            VectorIconRepository::ConstructControl(
                Surface,
                ControlIcon::ChevronDown,
                VectorIconPlacement{ DnArrowR.X + 2.0f, DnArrowR.Y + 1.0f, 10.0f, (VisIdx + 1u < Displayed.size() && DnArrowR.Contains(ListCursor)) ? Theme.TextPrimary : Theme.TextFaint, 1.6f });
            if (ListPress && UpArrowR.Contains(ListCursor) && VisIdx > 0u)
            {
                ConsumedSubClick = true;
                PendingMove      = { true, Layer->Identity, +1, false, 0u, false };
            }
            if (ListPress && DnArrowR.Contains(ListCursor) && VisIdx + 1u < Displayed.size())
            {
                ConsumedSubClick = true;
                PendingMove      = { true, Layer->Identity, -1, false, 0u, false };
            }

            Surface.FillRectangle(EyeR, EyeR.Contains(ListCursor) ? Theme.HoverSurface : Theme.WellSurface, 6.0f);
            VectorIconRepository::ConstructControl(
                Surface,
                Layer->Visible ? ControlIcon::EyeVisible : ControlIcon::EyeHidden,
                VectorIconPlacement{ EyeR.X + 4.0f, EyeR.Y + 5.0f, 12.0f, Layer->Visible ? Theme.HighlightFill : Theme.TextFaint, 1.6f });
            if (ListPress && EyeR.Contains(ListCursor))
            {
                ConsumedSubClick = true;
                Session->ToggleLayerVisible(Layer->Identity);
            }

            VectorIconRepository::ConstructControl(
                Surface,
                Layer->Locked ? ControlIcon::LockClosed : ControlIcon::LockOpen,
                VectorIconPlacement{ LockR.X + 4.0f, LockR.Y + 5.0f, 12.0f, Layer->Locked ? Theme.HighlightFill : Theme.TextFaint, 1.6f });
            if (ListPress && LockR.Contains(ListCursor))
            {
                ConsumedSubClick = true;
                Session->ToggleLayerLocked(Layer->Identity);
            }

            if (ListPress && !ConsumedSubClick && RowHover)
            {
                if (LastClickLayerId == Layer->Identity && TimeSinceLastClick <= 0.35)
                {
                    RenamingLayerId           = Layer->Identity;
                    AssignEditText(InlineRenameBuffer, Layer->Name);
                    InlineRenameBuffer.Active = true;
                }
                LastClickLayerId   = Layer->Identity;
                TimeSinceLastClick = 0.0;
                Session->SelectLayer(Layer->Identity);
            }

            RowCursorY += 52.0f;

            // Inline Layer Mask sub-row
            if (!FolderEntry)
            {
                const float MaskIndent = IndentPx + 14.0f;
                const Rectangle2D MaskR{ LeftX + MaskIndent, RowCursorY, std::max(100.0f, InnerW - MaskIndent), 34.0f };
                Surface.FillRectangle(MaskR, Layer->Mask.Enabled ? Theme.AccentSoft : Theme.SectionSurface, 6.0f);
                Surface.StrokeRectangle(MaskR, Theme.BorderSubtle, 6.0f, 1.0f);

                const Rectangle2D MThumbR{ MaskR.X + 8.0f, MaskR.Y + 6.0f, 22.0f, 22.0f };
                RenderSchematicMaskThumb(Surface, MThumbR, Layer->Mask);

                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Medium, { MThumbR.Right() + 8.0f, MaskR.Y + 4.0f }, MaskR.Width - 64.0f, 9.5f, Layer->Mask.Enabled ? Theme.TextPrimary : Theme.TextMuted, "Layer mask");

                static constexpr const char* MaskModeNames[3] { "noise", "radial", "stripe" };
                char MetaBuf[64];
                std::snprintf(MetaBuf, sizeof(MetaBuf), "%s · T:%.2f", MaskModeNames[static_cast<std::size_t>(Layer->Mask.Mode) % 3u], Layer->Mask.Threshold);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular, { MThumbR.Right() + 8.0f, MaskR.Y + 18.0f }, MaskR.Width - 64.0f, 8.2f, Theme.TextFaint, MetaBuf);

                const Rectangle2D MEyeR{ MaskR.Right() - 26.0f, MaskR.Y + 6.0f, 20.0f, 22.0f };
                VectorIconRepository::ConstructControl(
                    Surface,
                    Layer->Mask.Enabled ? ControlIcon::EyeVisible : ControlIcon::EyeHidden,
                    VectorIconPlacement{ MEyeR.X + 4.0f, MEyeR.Y + 5.0f, 12.0f, Layer->Mask.Enabled ? Theme.HighlightFill : Theme.TextFaint, 1.6f });

                if (ListPress && MEyeR.Contains(ListCursor))
                {
                    Session->PushUndoSnapshot();
                    Layer->Mask.Enabled = !Layer->Mask.Enabled;
                    Session->RecomputeSummaryMetrics();
                }
                else if (ListPress && MaskR.Contains(ListCursor))
                {
                    Session->SelectLayer(Layer->Identity);
                }

                RowCursorY += 40.0f;
            }

            if (!FolderEntry && Layer->ExpandedInStack)
            {
                const Rectangle2D ExpR{ LeftX + IndentPx + 14.0f, RowCursorY, std::max(100.0f, InnerW - IndentPx - 14.0f), 46.0f };
                Surface.FillRectangle(ExpR, Theme.WellSurface, 6.0f);
                Surface.StrokeRectangle(ExpR, Theme.BorderSubtle, 6.0f, 1.0f);

                float Cx = ExpR.X + 8.0f;
                for (const auto& Ch : Layer->Channels)
                {
                    const auto& Desc = PatternStampWorkspaceSession::QueryChannelDescriptor(Ch.Key);
                    const float CW   = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, Desc.ShortLabel) + 12.0f;
                    if (Cx + CW > ExpR.Right() - 8.0f)
                    {
                        break;
                    }
                    Surface.FillRectangle({ Cx, ExpR.Y + 6.0f, CW, 16.0f }, Theme.RaisedSurface, 8.0f);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Regular, { Cx + 6.0f, ExpR.Y + 9.0f }, 8.2f, Theme.TextPrimary, Desc.ShortLabel);
                    Cx += CW + 4.0f;
                }
                char GeoBuf[64];
                std::snprintf(GeoBuf, sizeof(GeoBuf), "Pos (%.0f, %.0f) · %.0fx%.0f", Layer->CenterX, Layer->CenterY, Layer->WidthPx, Layer->HeightPx);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular, { ExpR.X + 8.0f, ExpR.Y + 26.0f }, ExpR.Width - 16.0f, 8.5f, Theme.TextFaint, GeoBuf);

                RowCursorY += 52.0f;
            }
        }

        if (PendingMove.Ready)
        {
            if (PendingMove.DragDrop)
            {
                Session->ReorderLayerRelative(PendingMove.Identity, PendingMove.TargetIdentity, PendingMove.DropAbove);
            }
            else
            {
                Session->MoveLayerStep(PendingMove.Identity, PendingMove.StepDelta);
            }
        }
    }

    CachedContentHeight = std::max(ScrollRect.Height, RowCursorY - RowTopAnchorY + 8.0f);
    ScrollOffsetY       = std::clamp(ScrollOffsetY, 0.0f, std::max(0.0f, CachedContentHeight - ScrollRect.Height));
    WorkspaceAppearance::ConstructVerticalScrollIndicator(Surface, ScrollRect, CachedContentHeight, ScrollOffsetY, ListCursor);
    Surface.PopClip();

    // 7. Bottom Toolbar (Up / Down | Duplicate / Mask / Delete)
    float ToolBarY = Bounds.Bottom() - FooterH - GroundTileH - ToolbarH - 6.0f;
    Surface.StrokeLine({ Bounds.X, ToolBarY - 3.0f }, { Bounds.Right(), ToolBarY - 3.0f }, Theme.BorderSubtle, 1.0f);

    const bool ModifiableLayer = (Session->InspectSelectedLayer() != nullptr);
    const Rectangle2D ActUpR{ LeftX, ToolBarY + 3.0f, 28.0f, 26.0f };
    const Rectangle2D ActDnR{ LeftX + 32.0f, ToolBarY + 3.0f, 28.0f, 26.0f };
    const Rectangle2D ActDelR{ Bounds.Right() - PadX - 28.0f, ToolBarY + 3.0f, 28.0f, 26.0f };
    const Rectangle2D ActMaskR{ ActDelR.X - 32.0f, ToolBarY + 3.0f, 28.0f, 26.0f };
    const Rectangle2D ActDupR{ ActMaskR.X - 32.0f, ToolBarY + 3.0f, 28.0f, 26.0f };

    if (RenderToolIconButton(Surface, ActUpR, ControlIcon::ChevronUp, ModifiableLayer, false, false, Theme, Cursor, Pressed && !AddLayerSheetOpen))
    {
        Session->MoveLayerStep(Session->Document.SelectedLayerId, +1);
    }
    if (RenderToolIconButton(Surface, ActDnR, ControlIcon::ChevronDown, ModifiableLayer, false, false, Theme, Cursor, Pressed && !AddLayerSheetOpen))
    {
        Session->MoveLayerStep(Session->Document.SelectedLayerId, -1);
    }
    if (RenderToolIconButton(Surface, ActDupR, ControlIcon::CopyDuplicate, ModifiableLayer, false, false, Theme, Cursor, Pressed && !AddLayerSheetOpen))
    {
        Session->DuplicateSelectedLayer();
    }
    if (RenderToolIconButton(Surface, ActMaskR, ControlIcon::MaskCutout, ModifiableLayer, false, false, Theme, Cursor, Pressed && !AddLayerSheetOpen))
    {
        Session->ToggleSelectedLayerMask();
    }
    if (RenderToolIconButton(Surface, ActDelR, ControlIcon::TrashDelete, Session->Document.Layers.size() > 1u, false, true, Theme, Cursor, Pressed && !AddLayerSheetOpen))
    {
        Session->DeleteSelectedLayer();
    }

    // 8. Canvas Tile Ground Bar ("Canvas Tile · 1024x1024" + "Seamless"/"Cutout")
    const float GroundY = ToolBarY + ToolbarH + 2.0f;
    const Rectangle2D GroundR{ LeftX, GroundY, InnerW, GroundTileH - 4.0f };
    Surface.FillRectangle(GroundR, Theme.SectionSurface, 8.0f);
    Surface.StrokeRectangle(GroundR, Theme.BorderSubtle, 8.0f, 1.0f);

    const Rectangle2D GroundIconBox{ GroundR.X + 8.0f, GroundR.Y + 8.0f, 24.0f, 24.0f };
    Surface.FillRectangle(GroundIconBox, Theme.RaisedSurface, 6.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::LayersSlabs, VectorIconPlacement{ GroundIconBox.X + 5.0f, GroundIconBox.Y + 5.0f, 14.0f, Theme.TextMuted, 1.6f });

    const std::string TileTitle = "Canvas Tile · " + std::to_string(Session->ResolutionPx) + "x" + std::to_string(Session->ResolutionPx);
    const std::string TileSub   = Session->Document.GroundEnabled
        ? ("Background " + FormatHexColor(Session->Document.GroundTintA))
        : "Transparent alpha cutout";
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::SemiBold, { GroundIconBox.Right() + 8.0f, GroundR.Y + 7.0f }, InnerW - 110.0f, 9.8f, Theme.TextPrimary, TileTitle);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { GroundIconBox.Right() + 8.0f, GroundR.Y + 21.0f }, InnerW - 110.0f, 8.8f, Theme.TextFaint, TileSub);

    const std::string_view ModeTag = (Session->Mode == PatternStampWorkspaceMode::Pattern) ? "Seamless" : "Cutout";
    const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.8f, ModeTag) + 14.0f;
    const Rectangle2D TagR{ GroundR.Right() - TagW - 8.0f, GroundR.Y + 10.0f, TagW, 20.0f };
    Surface.FillRectangle(TagR, Theme.RaisedSurface, 6.0f);
    Surface.StrokeRectangle(TagR, Theme.BorderSubtle, 6.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { TagR.X + 7.0f, TagR.Y + 4.0f }, 8.8f, Theme.TextMuted, ModeTag);

    // 9. 3-Metric Pane Footer (LAYERS 2 | VISIBLE 2 | MODS 4)
    const Rectangle2D FooterR{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FooterR, Theme.LightSurface ? Theme.NestedSurface : WorkspaceSurfaceColors::FooterBackground);
    Surface.StrokeLine({ FooterR.X, FooterR.Y }, { FooterR.Right(), FooterR.Y }, Theme.BorderSubtle, 1.0f);

    const std::size_t ModsCount = Session->CountEnabledModifiers();
    struct FooterMetric { const char* Label; std::string ValStr; float XFrac; };
    const std::array<FooterMetric, 3> Metrics {{
        { "LAYERS",  std::to_string(TotalCount),   0.04f },
        { "VISIBLE", std::to_string(VisibleCount), 0.36f },
        { "MODS",    std::to_string(ModsCount),    0.68f },
    }};
    for (const auto& M : Metrics)
    {
        const float Mx = Bounds.X + Bounds.Width * M.XFrac;
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Mx, FooterR.Y + 7.0f }, 8.6f, Theme.TextFaint, M.Label);
        const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, M.Label);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { Mx + LW + 6.0f, FooterR.Y + 7.0f }, 9.2f, Theme.TextPrimary, M.ValStr);
    }

    // 10. Overlay Add Layer OverlaySheet Sheet
    if (AddLayerSheetOpen)
    {
        const Rectangle2D SheetR{ LeftX, AddSheetAnchorBottomY, InnerW, 214.0f };
        Surface.FillRectangle({ SheetR.X + 2.0f, SheetR.Y + 4.0f, SheetR.Width, SheetR.Height }, ColorRgba{ 0u, 0u, 0u, 110u }, 12.0f);
        Surface.FillRectangle(SheetR, Theme.SectionSurface, 12.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 12.0f, 1.0f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { SheetR.X + 14.0f, SheetR.Y + 12.0f }, 9.2f, Theme.TextFaint, "ADD LAYER");
        const Rectangle2D CloseR{ SheetR.Right() - 28.0f, SheetR.Y + 8.0f, 20.0f, 20.0f };
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::CloseCross, VectorIconPlacement{ CloseR.X + 4.0f, CloseR.Y + 4.0f, 12.0f, CloseR.Contains(Cursor) ? Theme.TextPrimary : Theme.TextFaint, 1.7f });

        struct AddLayerOption
        {
            PatternStampLayerCategory Category;
            ControlIcon      Icon;
            const char*      Label;
            const char*      Hint;
        };
        static constexpr std::array<AddLayerOption, 4> AddOpts {{
            { PatternStampLayerCategory::Motif,        ControlIcon::ShapeStar,      "+ Motif",  "Vector shape / procedural motif" },
            { PatternStampLayerCategory::Typographic,  ControlIcon::TextType,       "+ Text",   "Typography & engraved label" },
            { PatternStampLayerCategory::GradientWash, ControlIcon::SlidersQuality, "+ Grad",   "Linear / radial surface gradient" },
            { PatternStampLayerCategory::Folder,       ControlIcon::FolderClosed,   "+ Folder", "Group & nest layers" },
        }};

        bool ConsumedSheetPress = false;
        float EntryTopY         = SheetR.Y + 34.0f;
        for (const auto& Opt : AddOpts)
        {
            const Rectangle2D OptR{ SheetR.X + 10.0f, EntryTopY, SheetR.Width - 20.0f, 40.0f };
            const bool Hovered = OptR.Contains(Cursor);
            Surface.FillRectangle(OptR, Hovered ? Theme.HoverSurface : Theme.RaisedSurface, 8.0f);
            Surface.StrokeRectangle(OptR, Hovered ? Theme.BorderStrong : Theme.BorderSubtle, 8.0f, 1.0f);

            const Rectangle2D IconBox{ OptR.X + 8.0f, OptR.Y + 6.0f, 28.0f, 28.0f };
            Surface.FillRectangle(IconBox, Theme.WellSurface, 6.0f);
            Surface.StrokeRectangle(IconBox, Theme.BorderSubtle, 6.0f, 1.0f);
            VectorIconRepository::ConstructControl(
                Surface, Opt.Icon, VectorIconPlacement{ IconBox.X + 7.0f, IconBox.Y + 7.0f, 14.0f, Theme.HighlightFill, 1.6f });

            const float ChipW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.8f, Opt.Label) + 16.0f;
            const Rectangle2D LabelCap{ IconBox.Right() + 8.0f, OptR.Y + 4.0f, ChipW, 18.0f };
            Surface.FillRectangle(LabelCap, Theme.WellSurface, 9.0f);
            Surface.StrokeRectangle(LabelCap, Theme.BorderSubtle, 9.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { LabelCap.X + 8.0f, LabelCap.Y + 3.0f }, 9.8f, Theme.TextPrimary, Opt.Label);

            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { IconBox.Right() + 8.0f, OptR.Y + 24.0f }, OptR.Width - 52.0f, 8.5f, Theme.TextFaint, Opt.Hint);

            if (Pressed && Hovered)
            {
                ConsumedSheetPress = true;
                AddLayerSheetOpen  = false;
                Session->AddLayer(Opt.Category);
            }
            EntryTopY += 44.0f;
        }

        if (Pressed && CloseR.Contains(Cursor))
        {
            AddLayerSheetOpen = false;
        }
        else if (Pressed && !ConsumedSheetPress && !SheetR.Contains(Cursor) && !AddBtnRect.Contains(Cursor))
        {
            AddLayerSheetOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
