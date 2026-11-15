/*============================================================================================================================================
                                                        PATTERNSTAMPTOOLHEADERPANE.CPP
============================================================================================================================================*/
// 🧩 Central Pattern Studio & Stamp Studio UI chrome renderer (Band 1 doc/res bar, Band 2 circular tool dock + More tools overlay sheet,
//    View/Undo/Redo/Save/Bridge, crisp ruler chrome, deferred canvas backdrop, bottom shortcut hint & zoom controls).

#include "Engine/Workspaces/Texturing/PatternStampToolHeaderPane.h"

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

ControlIcon QueryIconForPatternStampTool(PatternStampToolCategory Tool) noexcept
{
    switch (Tool)
    {
        case PatternStampToolCategory::Select:        return ControlIcon::VectorPointer;
        case PatternStampToolCategory::Curve:         return ControlIcon::VectorCurve;
        case PatternStampToolCategory::Pen:           return ControlIcon::VectorPen;
        case PatternStampToolCategory::EditPoints:    return ControlIcon::EditFocus;
        case PatternStampToolCategory::PointGradient: return ControlIcon::PointGradient;
        case PatternStampToolCategory::Square:        return ControlIcon::ShapeSquare;
        case PatternStampToolCategory::Circle:        return ControlIcon::ShapeCircle;
        case PatternStampToolCategory::Diamond:       return ControlIcon::ShapeDiamond;
        case PatternStampToolCategory::Triangle:      return ControlIcon::ShapeTriangle;
        case PatternStampToolCategory::Star:          return ControlIcon::ShapeStar;
        case PatternStampToolCategory::Hexagon:       return ControlIcon::ShapeHexagon;
        case PatternStampToolCategory::Stripe:        return ControlIcon::ShapeStripe;
        case PatternStampToolCategory::TextBlock:     return ControlIcon::TextType;
        case PatternStampToolCategory::HandPan:       return ControlIcon::HandPan;
        default:                             return ControlIcon::VectorPointer;
    }
}

bool RenderCircularToolButton(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    ControlIcon                 Icon,
    bool                        Active,
    const TexturingThemeColors& Theme,
    Point2D                     Cursor,
    bool                        Pressed) noexcept
{
    const bool Hovered = Bounds.Contains(Cursor);
    const ColorRgba Fill = Active
        ? ColorRgba{ 46u, 50u, 38u, 255u }
        : (Hovered ? Theme.HoverSurface : Theme.RaisedSurface);
    const ColorRgba Stroke = Active
        ? Theme.HighlightFill
        : (Hovered ? Theme.BorderStrong : Theme.BorderSubtle);
    const ColorRgba Ink = Active
        ? Theme.HighlightFill
        : (Hovered ? Theme.TextPrimary : Theme.TextMuted);

    Surface.FillRectangle(Bounds, Fill, Bounds.Height * 0.5f);
    Surface.StrokeRectangle(Bounds, Stroke, Bounds.Height * 0.5f, 1.0f);

    const float IconSz = 13.0f;
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

PatternStampToolHeaderPane::PatternStampToolHeaderPane()
{
    ProjectionPane.ConfigureLockedSplitMode(Editor::ViewportSplitMode::Planar2DOnly);
    ProjectionPane.ConfigurePaneIndicators("2D VECTOR + GIZMO", "");
}

PatternStampToolHeaderPane::~PatternStampToolHeaderPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void PatternStampToolHeaderPane::AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
    CloseAllSheets();
    NameEditBuffer.Active = false;
    Interactor.AttachSession(TargetSession);
    if (Session != nullptr)
    {
        AssignEditText(NameEditBuffer, Session->Document.DocumentName);
    }
}

void PatternStampToolHeaderPane::AttachVulkanRasterizer(GeometricRaster::VectorCanvasRasterizer* TargetRasterizer) noexcept
{
    Rasterizer = TargetRasterizer;
    Interactor.AttachRasterizer(TargetRasterizer);

    if (TargetRasterizer == nullptr)
    {
        ProjectionPane.AssignInteractionCallback({});
        ProjectionPane.AssignRasterBufferSource({});
        return;
    }

    ProjectionPane.ConfigureLockedSplitMode(Editor::ViewportSplitMode::Planar2DOnly);
    ProjectionPane.AssignInteractionCallback(
        [this](const ViewportInteractionReading& Reading)
        {
            if (NameEditBuffer.Active)
            {
                return;
            }
            Interactor.ProcessViewportInput(Reading);
        });

    ProjectionPane.AssignRasterBufferSource(
        [this](uint32_t, uint32_t, uint32_t PlanarW, uint32_t PlanarH) -> DualViewportFramebuffers
        {
            DualViewportFramebuffers Result {};
            if (Session == nullptr || Rasterizer == nullptr || PlanarW < 16u || PlanarH < 16u)
            {
                return Result;
            }
            if (Rasterizer->RasterizeVectorCanvas(PlanarW, PlanarH, *Session, &Interactor.InspectGizmo2D(), &LatestCanvasRGBA))
            {
                LatestCanvasW = PlanarW;
                LatestCanvasH = PlanarH;
                const auto Metrics = Rasterizer->QueryLastMetrics();
                Session->InstancesEvaluated = static_cast<int32_t>(Metrics.TotalInstancesEvaluated);
                Session->MinScaleReading    = Metrics.MinNoiseScale;
                Session->MaxScaleReading    = Metrics.MaxNoiseScale;
                Result.Planar2D = ViewportRasterBuffer{ LatestCanvasRGBA.data(), LatestCanvasW, LatestCanvasH };
            }
            return Result;
        });
}

void PatternStampToolHeaderPane::CloseAllSheets() noexcept
{
    MoreToolsSheetOpen  = false;
    PresetsSheetOpen    = false;
    ResolutionSheetOpen = false;
    ViewSheetOpen       = false;
    SaveSheetOpen       = false;
    SnapSheetOpen       = false;
}

void PatternStampToolHeaderPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##PatternStampToolHeaderCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);

    LatestCanvasWindowLogicalRect = {
        ScreenBounds.X / SafeScale + LatestCanvasLogicalRect.X,
        ScreenBounds.Y / SafeScale + LatestCanvasLogicalRect.Y,
        LatestCanvasLogicalRect.Width,
        LatestCanvasLogicalRect.Height,
    };
    if (CanvasBoundsCallback)
    {
        CanvasBoundsCallback(LatestCanvasWindowLogicalRect);
    }

    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, Interactor.QueryDragCaptureActive());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANE RENDERING
//------------------------------------------------------------------------------------------------------------------------

void PatternStampToolHeaderPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    if (Session == nullptr || Bounds.Width < 260.0f || Bounds.Height < 200.0f)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingThemeColors();

    Surface.PushClip(Bounds);

    const ColorRgba CanvasBg = Theme.LightSurface
        ? ColorRgba{ 233u, 236u, 240u, 255u }
        : ColorRgba{ 16u, 17u, 20u, 255u };
    Surface.FillRectangle(Bounds, CanvasBg, 0.0f);

    const Point2D Cursor    { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed   = Input.PrimaryPointer.Pressed;
    const bool    SheetLock = MoreToolsSheetOpen || PresetsSheetOpen || ResolutionSheetOpen || ViewSheetOpen || SaveSheetOpen || SnapSheetOpen;

    // 1. Top Band 1 (Document Title + Starter Presets Selector + Editable Resolution Combo)
    const float Band1H = 54.0f;
    const float Band2H = 44.0f;
    const float PadX   = 18.0f;
    const Rectangle2D Band1Rect{ Bounds.X, Bounds.Y, Bounds.Width, Band1H };
    const Rectangle2D Band2Rect{ Bounds.X, Bounds.Y + Band1H, Bounds.Width, Band2H };

    Surface.FillRectangle(Band1Rect, Theme.PaneBackground);
    Surface.FillRectangle(Band2Rect, Theme.PaneBackground);
    Surface.StrokeLine({ Band2Rect.X, Band2Rect.Bottom() }, { Band2Rect.Right(), Band2Rect.Bottom() }, Theme.BorderSubtle, 1.0f);

    // Editable document name on left of Band 1 + Presets selector + Resolution combo on right
    const Rectangle2D ResComboRect{ Bounds.Right() - PadX - 186.0f, Band1Rect.Y + 11.0f, 186.0f, 32.0f };
    const Rectangle2D PresetsBtnRect{ ResComboRect.X - 116.0f, Band1Rect.Y + 11.0f, 106.0f, 32.0f };
    const float NameMaxW = std::max(140.0f, PresetsBtnRect.X - (Bounds.X + PadX) - 18.0f);
    const Rectangle2D NameRect{ Bounds.X + PadX - 4.0f, Band1Rect.Y + 9.0f, NameMaxW, 36.0f };

    if (Pressed && !SheetLock)
    {
        if (NameRect.Contains(Cursor))
        {
            if (!NameEditBuffer.Active)
            {
                AssignEditText(NameEditBuffer, Session->Document.DocumentName);
            }
            NameEditBuffer.Active = true;
        }
        else if (NameEditBuffer.Active)
        {
            if (NameEditBuffer.Length > 0u)
            {
                Session->Document.DocumentName = std::string(NameEditBuffer.Text);
            }
            NameEditBuffer.Active = false;
        }
    }
    if (NameEditBuffer.Active)
    {
        WorkspaceAppearance::ForwardInputToTextBuffer(NameEditBuffer, Input, DeltaSeconds);
        Session->Document.DocumentName = (NameEditBuffer.Length == 0u) ? std::string("Untitled") : std::string(NameEditBuffer.Text);
        Surface.FillRectangle(NameRect, Theme.WellSurface, 8.0f);
        Surface.StrokeRectangle(NameRect, Theme.HighlightFill, 8.0f, 1.0f);
    }
    else if (std::string_view(NameEditBuffer.Text) != Session->Document.DocumentName)
    {
        AssignEditText(NameEditBuffer, Session->Document.DocumentName);
    }

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + PadX, Band1Rect.Y + 13.0f },
        NameMaxW - 8.0f,
        24.0f,
        Theme.TextPrimary,
        Session->Document.DocumentName);

    // Starter Presets selector pill on right of Band 1
    Surface.FillRectangle(PresetsBtnRect, PresetsSheetOpen ? Theme.HoverSurface : Theme.SectionSurface, 16.0f);
    Surface.StrokeRectangle(PresetsBtnRect, PresetsSheetOpen ? Theme.HighlightFill : Theme.BorderSubtle, 16.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ModifierStar, VectorIconPlacement{ PresetsBtnRect.X + 12.0f, PresetsBtnRect.Y + 10.0f, 12.0f, Theme.HighlightFill, 1.6f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { PresetsBtnRect.X + 30.0f, PresetsBtnRect.Y + 10.0f }, 10.0f, Theme.TextPrimary, "Presets");
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ PresetsBtnRect.Right() - 20.0f, PresetsBtnRect.Y + 11.0f, 11.0f, Theme.TextMuted, 1.6f });
    if (Pressed && PresetsBtnRect.Contains(Cursor))
    {
        const bool NextOpen = !PresetsSheetOpen;
        CloseAllSheets();
        PresetsSheetOpen = NextOpen;
    }

    // Resolution combo on right of Band 1
    Surface.FillRectangle(ResComboRect, Theme.SectionSurface, 16.0f);
    Surface.StrokeRectangle(ResComboRect, ResolutionSheetOpen ? Theme.HighlightFill : Theme.BorderSubtle, 16.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { ResComboRect.X + 12.0f, ResComboRect.Y + 11.0f }, 8.8f, Theme.TextFaint, "RESOLUTION");
    const std::string ResValStr = std::to_string(Session->ResolutionPx);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { ResComboRect.X + 104.0f, ResComboRect.Y + 10.0f }, 10.8f, Theme.TextPrimary, ResValStr);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { ResComboRect.X + 138.0f, ResComboRect.Y + 11.0f }, 8.6f, Theme.TextFaint, "PX");
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ ResComboRect.Right() - 20.0f, ResComboRect.Y + 11.0f, 11.0f, Theme.TextMuted, 1.6f });
    if (Pressed && ResComboRect.Contains(Cursor))
    {
        const bool NextOpen = !ResolutionSheetOpen;
        CloseAllSheets();
        ResolutionSheetOpen = NextOpen;
    }

    // 2. Band 2: Circular Vector Tool Dock + "More tools" + "| VIEW [...] | Undo | Redo | Save v | Tile on Material | Use in Paint / Use Stamp"
    float ToolX = Bounds.X + PadX;
    const float ToolY = Band2Rect.Y + 7.0f;
    const auto& Tools = PatternStampWorkspaceSession::InspectToolDescriptors();
    for (const auto& Tool : Tools)
    {
        if (!Tool.PrimaryBar)
        {
            continue;
        }
        const Rectangle2D CircleR{ ToolX, ToolY, 30.0f, 30.0f };
        if (RenderCircularToolButton(
                Surface,
                CircleR,
                QueryIconForPatternStampTool(Tool.Category),
                Session->ActiveTool == Tool.Category,
                Theme,
                Cursor,
                Pressed && !SheetLock))
        {
            Session->ActiveTool = Tool.Category;
        }
        ToolX += 34.0f;
    }

    const Rectangle2D MoreBtnRect{ ToolX + 2.0f, ToolY, 76.0f, 30.0f };
    if (ConstructCapsuleButtonControl(
            Surface, MoreBtnRect, "More tools", Theme, Cursor, Pressed, MoreToolsSheetOpen, false, ControlIcon::Count, 9.6f))
    {
        const bool NextOpen = !MoreToolsSheetOpen;
        CloseAllSheets();
        MoreToolsSheetOpen = NextOpen;
    }

    // Right-aligned toolbar tail
    const bool  PatternModeActive    = (Session->Mode == PatternStampWorkspaceMode::Pattern);
    const char* PrimaryLabel = PatternModeActive ? "Use in Paint" : "Use Stamp";
    const float PrimaryW     = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.8f, PrimaryLabel) + 34.0f;
    const float MatBridgeW   = PatternModeActive ? (WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.8f, "Tile on Material") + 34.0f) : 0.0f;
    const float SaveW        = 76.0f;
    const float UndoRedoW    = 28.0f;
    const float ViewW        = 134.0f;

    float TailRightX = Bounds.Right() - PadX;
    const Rectangle2D PrimaryRect{ TailRightX - PrimaryW, ToolY, PrimaryW, 30.0f };
    TailRightX = PrimaryRect.X - 6.0f;

    Rectangle2D MatBridgeRect {};
    if (PatternModeActive)
    {
        MatBridgeRect = { TailRightX - MatBridgeW, ToolY, MatBridgeW, 30.0f };
        TailRightX    = MatBridgeRect.X - 6.0f;
    }

    const Rectangle2D SaveRect{ TailRightX - SaveW, ToolY, SaveW, 30.0f };
    TailRightX = SaveRect.X - 6.0f;
    const Rectangle2D RedoRect{ TailRightX - UndoRedoW, ToolY, UndoRedoW, 30.0f };
    TailRightX = RedoRect.X - 4.0f;
    const Rectangle2D UndoRect{ TailRightX - UndoRedoW, ToolY, UndoRedoW, 30.0f };
    TailRightX = UndoRect.X - 6.0f;
    const Rectangle2D ViewRect{ TailRightX - ViewW, ToolY, ViewW, 30.0f };

    if (ViewRect.X > MoreBtnRect.Right() + 8.0f)
    {
        Surface.StrokeLine({ ViewRect.X - 6.0f, ToolY + 4.0f }, { ViewRect.X - 6.0f, ToolY + 26.0f }, Theme.BorderSubtle, 1.0f);

        Surface.FillRectangle(ViewRect, Theme.RaisedSurface, 15.0f);
        Surface.StrokeRectangle(ViewRect, ViewSheetOpen ? Theme.HighlightFill : Theme.BorderSubtle, 15.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { ViewRect.X + 10.0f, ViewRect.Y + 9.0f }, 8.6f, Theme.TextFaint, "VIEW");
        const auto& ViewLabels = PatternStampWorkspaceSession::InspectViewChannelLabels();
        const std::size_t VIdx = static_cast<std::size_t>(Session->ViewChannel) % ViewLabels.size();
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { ViewRect.X + 40.0f, ViewRect.Y + 9.0f }, ViewRect.Width - 60.0f, 9.2f, Theme.TextPrimary, ViewLabels[VIdx]);
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::ChevronDown, VectorIconPlacement{ ViewRect.Right() - 18.0f, ViewRect.Y + 10.0f, 10.0f, Theme.TextMuted, 1.6f });
        if (Pressed && ViewRect.Contains(Cursor))
        {
            const bool NextOpen = !ViewSheetOpen;
            CloseAllSheets();
            ViewSheetOpen = NextOpen;
        }
    }

    auto RenderUndoRedo = [&](Rectangle2D R, ControlIcon Icon, bool Enabled) -> bool
    {
        const bool Hovered = Enabled && !SheetLock && R.Contains(Cursor);
        ColorRgba  Fill    = Hovered ? Theme.HoverSurface : Theme.RaisedSurface;
        if (!Enabled) Fill.Alpha = 110u;
        Surface.FillRectangle(R, Fill, 14.0f);
        Surface.StrokeRectangle(R, Theme.BorderSubtle, 14.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface, Icon, VectorIconPlacement{ R.X + 8.0f, R.Y + 8.0f, 13.0f, Enabled ? Theme.TextPrimary : Theme.TextFaint, 1.6f });
        return Hovered && Pressed;
    };
    if (RenderUndoRedo(UndoRect, ControlIcon::UndoArrow, Session->QueryCanUndo()))
    {
        Session->ApplyUndo();
    }
    if (RenderUndoRedo(RedoRect, ControlIcon::RedoArrow, Session->QueryCanRedo()))
    {
        Session->ApplyRedo();
    }

    if (ConstructCapsuleButtonControl(
            Surface, SaveRect, "Save", Theme, Cursor, Pressed, SaveSheetOpen, false, ControlIcon::SaveDisk, 9.6f))
    {
        const bool NextOpen = !SaveSheetOpen;
        CloseAllSheets();
        SaveSheetOpen = NextOpen;
    }

    if (PatternModeActive && MatBridgeRect.Width > 0.0f)
    {
        if (ConstructCapsuleButtonControl(
                Surface, MatBridgeRect, "Tile on Material", Theme, Cursor, Pressed && !SheetLock, true, false, ControlIcon::MaterialSphere, 9.6f))
        {
            Session->StatusNotice = "Applied " + Session->Document.DocumentName + " to active Material";
            if (BridgeCallback)
            {
                BridgeCallback("bridge.material");
            }
        }
    }

    if (ConstructCapsuleButtonControl(
            Surface, PrimaryRect, PrimaryLabel, Theme, Cursor, Pressed && !SheetLock, false, true, ControlIcon::BrushTool, 9.6f))
    {
        Session->StatusNotice = PatternModeActive
            ? ("Sent " + Session->Document.DocumentName + " to Texture Paint")
            : ("Selected stamp: " + Session->Document.DocumentName);
        if (BridgeCallback)
        {
            BridgeCallback(PatternModeActive ? "bridge.paint_pattern" : "bridge.paint_stamp");
        }
    }

    // 3. Pixel Ruler Chrome (zoom/pan-synchronized with VectorCanvasRasterizer::ComputeTileLayoutMetrics) + GPU Canvas Region
    const float TopRulerY = Band2Rect.Bottom();
    const float RulerSize = 24.0f;
    const Rectangle2D TopRulerR{ Bounds.X + RulerSize, TopRulerY, Bounds.Width - RulerSize, RulerSize };
    const Rectangle2D LeftRulerR{ Bounds.X, TopRulerY + RulerSize, RulerSize, std::max(0.0f, Bounds.Bottom() - (TopRulerY + RulerSize) - 34.0f) };

    const Rectangle2D CanvasRegion{ LeftRulerR.Right(), TopRulerR.Bottom(), Bounds.Width - RulerSize, std::max(60.0f, Bounds.Bottom() - TopRulerR.Bottom() - 34.0f) };
    LatestCanvasLogicalRect = CanvasRegion;

    const ColorRgba RulerBg = Theme.LightSurface ? Theme.NestedSurface : ColorRgba{ 14u, 15u, 18u, 255u };
    Surface.FillRectangle({ Bounds.X, TopRulerY, Bounds.Width, RulerSize }, RulerBg);
    Surface.FillRectangle(LeftRulerR, RulerBg);
    Surface.StrokeLine({ Bounds.X, TopRulerY + RulerSize }, { Bounds.Right(), TopRulerY + RulerSize }, Theme.BorderSubtle, 1.0f);
    Surface.StrokeLine({ Bounds.X + RulerSize, TopRulerY }, { Bounds.X + RulerSize, Bounds.Bottom() - 34.0f }, Theme.BorderSubtle, 1.0f);

    const uint32_t RulerViewW = static_cast<uint32_t>(std::max(1.0f, std::round(CanvasRegion.Width)));
    const uint32_t RulerViewH = static_cast<uint32_t>(std::max(1.0f, std::round(CanvasRegion.Height)));
    const auto RulerMetrics   = GeometricRaster::VectorCanvasRasterizer::ComputeTileLayoutMetrics(RulerViewW, RulerViewH, *Session);
    const float RulerScale    = RulerMetrics.TileSpanPx / std::max(1.0f, RulerMetrics.DocExtentPx);
    const int32_t TickStepDoc = (RulerScale >= 1.35f) ? 64 : ((RulerScale >= 0.65f) ? 128 : 256);

    Surface.PushClip(TopRulerR);
    for (int32_t DocCoord = -1024; DocCoord <= 1536; DocCoord += TickStepDoc)
    {
        const float ScreenX = TopRulerR.X + RulerMetrics.TileMinX + static_cast<float>(DocCoord) * RulerScale;
        if (ScreenX < TopRulerR.X + 4.0f || ScreenX > TopRulerR.Right() - 18.0f)
        {
            continue;
        }
        Surface.StrokeLine({ ScreenX, TopRulerR.Bottom() - 5.0f }, { ScreenX, TopRulerR.Bottom() }, Theme.BorderSubtle, 1.0f);
        char TickBuf[16];
        std::snprintf(TickBuf, sizeof(TickBuf), "%d", DocCoord);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { ScreenX + 2.0f, TopRulerR.Y + 5.0f }, 8.2f, Theme.TextFaint, TickBuf);
    }
    Surface.PopClip();

    Surface.PushClip(LeftRulerR);
    for (int32_t DocCoord = -1024; DocCoord <= 1536; DocCoord += TickStepDoc)
    {
        const float ScreenY = LeftRulerR.Y + RulerMetrics.TileMinY + static_cast<float>(DocCoord) * RulerScale;
        if (ScreenY < LeftRulerR.Y + 4.0f || ScreenY > LeftRulerR.Bottom() - 12.0f)
        {
            continue;
        }
        Surface.StrokeLine({ LeftRulerR.Right() - 5.0f, ScreenY }, { LeftRulerR.Right(), ScreenY }, Theme.BorderSubtle, 1.0f);
        char TickBuf[16];
        std::snprintf(TickBuf, sizeof(TickBuf), "%d", DocCoord);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftRulerR.X + 2.0f, ScreenY + 1.0f }, 7.8f, Theme.TextFaint, TickBuf);
    }
    Surface.PopClip();

    const float BottomBarY = Bounds.Bottom() - 34.0f;
    const Rectangle2D ZoomBox{ Bounds.Right() - PadX - 134.0f, BottomBarY - 38.0f, 134.0f, 28.0f };

    if (Rasterizer != nullptr)
    {
        const bool AllowCanvasInput = !SheetLock && !NameEditBuffer.Active && !ZoomBox.Contains(Cursor);
        ProjectionPane.RenderViewportCanvas(Surface, CanvasRegion, Input, AllowCanvasInput, &LatestCanvasLogicalRect);
    }
    else
    {
        const float TileSide = std::min(CanvasRegion.Width * 0.72f, CanvasRegion.Height * 0.78f);
        const Rectangle2D TileBoundsR{
            CanvasRegion.X + (CanvasRegion.Width - TileSide) * 0.5f,
            CanvasRegion.Y + (CanvasRegion.Height - TileSide) * 0.5f,
            TileSide,
            TileSide };
        Surface.FillRectangle(TileBoundsR, Theme.LightSurface ? ColorRgba{ 242u, 244u, 247u, 255u } : ColorRgba{ 20u, 22u, 26u, 255u }, 0.0f);
        Surface.StrokeRectangle(TileBoundsR, ColorRgba{ 239u, 244u, 207u, static_cast<uint8_t>(Theme.LightSurface ? 90u : 70u) }, 0.0f, 1.0f);

        const char* CanvasNote = PatternModeActive
            ? "Pattern 2D Vector & PBR Canvas Chrome (Viewport Deferred)"
            : "Stamp Alpha Cutout Vector Canvas Chrome (Viewport Deferred)";
        const float NoteW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, CanvasNote);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { TileBoundsR.X + (TileBoundsR.Width - NoteW) * 0.5f, TileBoundsR.Y + TileBoundsR.Height * 0.5f - 6.0f },
            10.5f,
            Theme.TextFaint,
            CanvasNote);
    }

    // 4. Bottom Strip
    Surface.FillRectangle({ Bounds.X, BottomBarY, Bounds.Width, 34.0f }, Theme.PaneBackground);
    Surface.StrokeLine({ Bounds.X, BottomBarY }, { Bounds.Right(), BottomBarY }, Theme.BorderSubtle, 1.0f);

    const std::string ModalHint = Interactor.QueryModalStatusSummary();
    const char* FooterHint = !ModalHint.empty()
        ? ModalHint.c_str()
        : (!Session->StatusNotice.empty()
               ? Session->StatusNotice.c_str()
               : "Marquee to select · Shift constrain · Alt centred resize · Space pan");
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + PadX, BottomBarY + 10.0f },
        std::max(120.0f, Bounds.Width - 260.0f),
        9.4f,
        !ModalHint.empty() ? Theme.HighlightFill : Theme.TextFaint,
        FooterHint);

    char SelBuf[64];
    if (Rasterizer != nullptr)
    {
        std::snprintf(
            SelBuf,
            sizeof(SelBuf),
            "%s · %d inst",
            Session->InspectSelectedLayer() != nullptr ? "1 selected" : "0 selected",
            Session->InstancesEvaluated);
    }
    else
    {
        std::snprintf(
            SelBuf,
            sizeof(SelBuf),
            "%s",
            Session->InspectSelectedLayer() != nullptr ? "1 selected" : "0 selected");
    }
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface, FontWeightClass::Regular, { Bounds.Right() - PadX, BottomBarY + 10.0f }, 9.4f, Theme.TextFaint, SelBuf);

    Surface.FillRectangle(ZoomBox, Theme.SectionSurface, 14.0f);
    Surface.StrokeRectangle(ZoomBox, Theme.BorderSubtle, 14.0f, 1.0f);

    const Rectangle2D ZoomOutR{ ZoomBox.X + 4.0f, ZoomBox.Y + 2.0f, 24.0f, 24.0f };
    const Rectangle2D ZoomInR{ ZoomBox.X + 76.0f, ZoomBox.Y + 2.0f, 24.0f, 24.0f };
    const Rectangle2D ZoomFitR{ ZoomBox.X + 102.0f, ZoomBox.Y + 2.0f, 28.0f, 24.0f };

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { ZoomOutR.X + 9.0f, ZoomOutR.Y + 5.0f }, 11.0f, ZoomOutR.Contains(Cursor) ? Theme.TextPrimary : Theme.TextMuted, "-");
    char ZoomPct[16];
    std::snprintf(ZoomPct, sizeof(ZoomPct), "%d%%", static_cast<int>(std::lround(Session->ZoomFraction * 100.0f)));
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { ZoomBox.X + 34.0f, ZoomBox.Y + 8.0f }, 10.0f, Theme.TextPrimary, ZoomPct);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { ZoomInR.X + 8.0f, ZoomInR.Y + 5.0f }, 11.0f, ZoomInR.Contains(Cursor) ? Theme.TextPrimary : Theme.TextMuted, "+");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { ZoomFitR.X + 4.0f, ZoomFitR.Y + 7.0f }, 9.6f, ZoomFitR.Contains(Cursor) ? Theme.TextPrimary : Theme.TextMuted, "Fit");

    if (Pressed && !SheetLock)
    {
        if (ZoomOutR.Contains(Cursor)) Session->ZoomFraction = std::clamp(Session->ZoomFraction - 0.12f, 0.25f, 4.0f);
        else if (ZoomInR.Contains(Cursor)) Session->ZoomFraction = std::clamp(Session->ZoomFraction + 0.12f, 0.25f, 4.0f);
        else if (ZoomFitR.Contains(Cursor))
        {
            Session->ZoomFraction = 1.0f;
            Session->PanOffsetU   = 0.0f;
            Session->PanOffsetV   = 0.0f;
        }
    }

    // 5. Overlay Sheets (More tools, Resolution, View, Save)
    if (MoreToolsSheetOpen)
    {
        const float SheetW = 286.0f;
        const float SheetX = std::clamp(MoreBtnRect.X - 60.0f, Bounds.X + 8.0f, Bounds.Right() - SheetW - 8.0f);
        const Rectangle2D SheetR{ SheetX, Band2Rect.Bottom() + 6.0f, SheetW, 186.0f };
        Surface.FillRectangle({ SheetR.X + 2.0f, SheetR.Y + 4.0f, SheetR.Width, SheetR.Height }, ColorRgba{ 0u, 0u, 0u, 110u }, 12.0f);
        Surface.FillRectangle(SheetR, Theme.SectionSurface, 12.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 12.0f, 1.0f);

        std::vector<PatternStampToolDescriptor> ExtraTools;
        for (const auto& T : Tools)
        {
            if (!T.PrimaryBar) ExtraTools.push_back(T);
        }

        bool        Consumed = false;
        const float CellGap  = 8.0f;
        const float CellW    = (SheetR.Width - 24.0f - CellGap) * 0.5f;
        const float CellH    = 32.0f;
        for (std::size_t I = 0u; I < ExtraTools.size(); ++I)
        {
            const std::size_t Col = I % 2u;
            const std::size_t Row = I / 2u;
            const Rectangle2D BtnR{ SheetR.X + 12.0f + static_cast<float>(Col) * (CellW + CellGap), SheetR.Y + 12.0f + static_cast<float>(Row) * (CellH + 6.0f), CellW, CellH };
            const bool Active  = (Session->ActiveTool == ExtraTools[I].Category);
            const bool Hovered = BtnR.Contains(Cursor);
            Surface.FillRectangle(BtnR, Active ? Theme.HighlightFill : (Hovered ? Theme.HoverSurface : Theme.RaisedSurface), 16.0f);
            Surface.StrokeRectangle(BtnR, Active ? Theme.HighlightFill : (Hovered ? Theme.BorderStrong : Theme.BorderSubtle), 16.0f, 1.0f);

            const ColorRgba Ink = Active ? Theme.HighlightInk : Theme.TextPrimary;
            VectorIconRepository::ConstructControl(
                Surface, QueryIconForPatternStampTool(ExtraTools[I].Category), VectorIconPlacement{ BtnR.X + 10.0f, BtnR.Y + 9.0f, 13.0f, Ink, 1.6f });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { BtnR.X + 28.0f, BtnR.Y + 10.0f }, 9.8f, Ink, ExtraTools[I].Label);

            const Rectangle2D KeyR{ BtnR.Right() - 24.0f, BtnR.Y + 7.0f, 16.0f, 18.0f };
            Surface.StrokeRectangle(KeyR, Theme.BorderSubtle, 4.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { KeyR.X + 5.0f, KeyR.Y + 3.0f }, 8.6f, Active ? Theme.HighlightInk : Theme.TextFaint, ExtraTools[I].KeyCap);

            if (Pressed && Hovered)
            {
                Consumed            = true;
                Session->ActiveTool = ExtraTools[I].Category;
                MoreToolsSheetOpen  = false;
            }
        }

        const float StripY = SheetR.Bottom() - 44.0f;
        Surface.StrokeLine({ SheetR.X + 10.0f, StripY - 6.0f }, { SheetR.Right() - 10.0f, StripY - 6.0f }, Theme.BorderSubtle, 1.0f);

        const Rectangle2D GridR{ SheetR.X + 12.0f, StripY, 64.0f, 30.0f };
        const Rectangle2D AxesR{ GridR.Right() + 6.0f, StripY, 66.0f, 30.0f };
        const Rectangle2D SnapR{ AxesR.Right() + 6.0f, StripY, SheetR.Right() - 12.0f - (AxesR.Right() + 6.0f), 30.0f };

        if (ConstructCapsuleButtonControl(Surface, GridR, "Grid", Theme, Cursor, Pressed, Session->ShowGrid, false, ControlIcon::LayoutGrid, 9.4f))
        {
            Consumed          = true;
            Session->ShowGrid = !Session->ShowGrid;
        }
        if (ConstructCapsuleButtonControl(Surface, AxesR, "Axes", Theme, Cursor, Pressed, Session->ShowAxes, false, ControlIcon::EditFocus, 9.4f))
        {
            Consumed          = true;
            Session->ShowAxes = !Session->ShowAxes;
        }

        static constexpr const char* SnapLabels[5] { "Snap: Off", "Snap: 8px", "Snap: 16px", "Snap: 32px", "Snap: 64px" };
        static constexpr int32_t     SnapSteps[5]  { 0, 8, 16, 32, 64 };
        uint32_t SnapIdx = 2u;
        for (uint32_t S = 0u; S < 5u; ++S)
        {
            if (SnapSteps[S] == Session->SnapStepPx) SnapIdx = S;
        }
        Surface.FillRectangle(SnapR, Theme.RaisedSurface, 15.0f);
        Surface.StrokeRectangle(SnapR, SnapSheetOpen ? Theme.HighlightFill : Theme.BorderSubtle, 15.0f, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { SnapR.X + 10.0f, SnapR.Y + 9.0f }, SnapR.Width - 26.0f, 9.4f, Theme.TextPrimary, SnapLabels[SnapIdx]);
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::ChevronDown, VectorIconPlacement{ SnapR.Right() - 18.0f, SnapR.Y + 10.0f, 10.0f, Theme.TextMuted, 1.6f });
        if (Pressed && SnapR.Contains(Cursor))
        {
            Consumed      = true;
            SnapSheetOpen = !SnapSheetOpen;
        }
        if (SnapSheetOpen)
        {
            RenderFittedDropdownSheet(
                Surface,
                SnapR,
                Bounds,
                SnapLabels,
                5u,
                SnapIdx,
                SnapSheetOpen,
                Input,
                [this](uint32_t Picked)
                {
                    static constexpr int32_t Steps[5] { 0, 8, 16, 32, 64 };
                    if (Picked < 5u) Session->SnapStepPx = Steps[Picked];
                },
                &ViewSheetScrollY);
        }
        if (Pressed && !Consumed && !SheetR.Contains(Cursor) && !MoreBtnRect.Contains(Cursor) && !SnapSheetOpen)
        {
            MoreToolsSheetOpen = false;
        }
    }

    if (PresetsSheetOpen)
    {
        const auto& Presets = Session->QueryPresetDirectory();
        std::vector<const char*> PresetLabels {};
        PresetLabels.reserve(Presets.size());
        for (const auto& P : Presets)
        {
            PresetLabels.push_back(P.Title.c_str());
        }
        const uint32_t CurPresetIdx = static_cast<uint32_t>(Session->QueryActivePresetIndex());
        if (!PresetLabels.empty())
        {
            RenderFittedDropdownSheet(
                Surface,
                PresetsBtnRect,
                Bounds,
                PresetLabels.data(),
                static_cast<uint32_t>(PresetLabels.size()),
                CurPresetIdx,
                PresetsSheetOpen,
                Input,
                [this](uint32_t Picked)
                {
                    Session->LoadPresetByIndex(Picked);
                    AssignEditText(NameEditBuffer, Session->Document.DocumentName);
                    Interactor.SynchronizeGizmoAnchor();
                },
                &ViewSheetScrollY);
        }
    }

    if (ResolutionSheetOpen)
    {
        static constexpr const char* ResOpts[4] { "512 x 512", "1024 x 1024", "2048 x 2048", "4096 x 4096" };
        static constexpr uint32_t    ResVals[4] { 512u, 1024u, 2048u, 4096u };
        uint32_t SelRes = 1u;
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            if (ResVals[I] == Session->ResolutionPx) SelRes = I;
        }
        RenderFittedDropdownSheet(
            Surface,
            ResComboRect,
            Bounds,
            ResOpts,
            4u,
            SelRes,
            ResolutionSheetOpen,
            Input,
            [this](uint32_t Picked)
            {
                static constexpr uint32_t Vals[4] { 512u, 1024u, 2048u, 4096u };
                if (Picked < 4u) Session->AssignResolution(Vals[Picked]);
            },
            &ViewSheetScrollY);
    }

    if (ViewSheetOpen)
    {
        const auto& ViewLabels = PatternStampWorkspaceSession::InspectViewChannelLabels();
        const uint32_t CurIdx  = static_cast<uint32_t>(Session->ViewChannel);
        RenderFittedDropdownSheet(
            Surface,
            ViewRect,
            Bounds,
            ViewLabels.data(),
            static_cast<uint32_t>(ViewLabels.size()),
            CurIdx,
            ViewSheetOpen,
            Input,
            [this](uint32_t Picked)
            {
                const auto& Labels = PatternStampWorkspaceSession::InspectViewChannelLabels();
                if (Picked < Labels.size()) Session->ViewChannel = static_cast<PatternStampViewChannel>(Picked);
            },
            &ViewSheetScrollY);
    }

    if (SaveSheetOpen)
    {
        static constexpr const char* SaveLabels[5] {
            "Save Vector Space (.VectorSpace)",
            "Export PNG (Current Res)",
            "Export Vector SVG (.svg)",
            "Export PBR Channels",
            "Open Vector Space / SVG...",
        };
        RenderFittedDropdownSheet(
            Surface,
            SaveRect,
            Bounds,
            SaveLabels,
            5u,
            99u,
            SaveSheetOpen,
            Input,
            [this](uint32_t Picked)
            {
                static constexpr const char* Labels[5] {
                    "Save Vector Space (.VectorSpace)",
                    "Export PNG (Current Res)",
                    "Export Vector SVG (.svg)",
                    "Export PBR Channels",
                    "Open Vector Space / SVG...",
                };
                static constexpr const char* Tags[5] { "save.vectorspace", "save.png", "save.svg", "save.pbr", "save.open" };
                if (Picked < 5u)
                {
                    Session->StatusNotice = Labels[Picked];
                    if (BridgeCallback) BridgeCallback(Tags[Picked]);
                }
            },
            &ViewSheetScrollY);
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
