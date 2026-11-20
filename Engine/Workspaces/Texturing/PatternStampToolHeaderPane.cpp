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

void DrawMotifPreviewInWell(PixelSpace& Surface, Rectangle2D WellR, std::size_t ShapeIdx, ColorRgba Ink) noexcept
{
    const Point2D C{ WellR.X + WellR.Width * 0.5f, WellR.Y + WellR.Height * 0.5f };
    const float R = std::min(WellR.Width, WellR.Height) * 0.34f;
    if (ShapeIdx == 16u)
    {
        Surface.StrokeLine({ C.X - R, C.Y + R * 0.72f }, { C.X + R, C.Y - R * 0.72f }, Ink, 2.4f);
        return;
    }
    switch (ShapeIdx % 16u)
    {
        case 0u: Surface.FillCircle(C, R, Ink); break;
        case 1u: Surface.StrokeCircle(C, R, Ink, 3.2f); break;
        case 2u: Surface.FillRectangle({ C.X - R, C.Y - R, R * 2.0f, R * 2.0f }, Ink, 1.5f); break;
        case 3u: Surface.FillRectangle({ C.X - R, C.Y - R, R * 2.0f, R * 2.0f }, Ink, 6.0f); break;
        case 4u:
            Surface.StrokeLine({ C.X, C.Y - R }, { C.X + R, C.Y }, Ink, 2.5f);
            Surface.StrokeLine({ C.X + R, C.Y }, { C.X, C.Y + R }, Ink, 2.5f);
            Surface.StrokeLine({ C.X, C.Y + R }, { C.X - R, C.Y }, Ink, 2.5f);
            Surface.StrokeLine({ C.X - R, C.Y }, { C.X, C.Y - R }, Ink, 2.5f);
            Surface.FillCircle(C, R * 0.45f, Ink);
            break;
        case 5u:
            for (int K = 0; K < 6; ++K)
            {
                const float A0 = static_cast<float>(K) * 1.04719755f;
                const float A1 = static_cast<float>(K + 1) * 1.04719755f;
                Surface.StrokeLine({ C.X + std::cos(A0) * R, C.Y + std::sin(A0) * R }, { C.X + std::cos(A1) * R, C.Y + std::sin(A1) * R }, Ink, 2.4f);
            }
            Surface.FillCircle(C, R * 0.55f, Ink);
            break;
        case 6u:
            Surface.StrokeLine({ C.X, C.Y - R }, { C.X + R, C.Y + R * 0.85f }, Ink, 2.5f);
            Surface.StrokeLine({ C.X + R, C.Y + R * 0.85f }, { C.X - R, C.Y + R * 0.85f }, Ink, 2.5f);
            Surface.StrokeLine({ C.X - R, C.Y + R * 0.85f }, { C.X, C.Y - R }, Ink, 2.5f);
            break;
        case 7u:
            for (int K = 0; K < 5; ++K)
            {
                const float A = -1.570796f + static_cast<float>(K) * 1.256637f;
                Surface.StrokeLine(C, { C.X + std::cos(A) * R, C.Y + std::sin(A) * R }, Ink, 2.5f);
            }
            Surface.FillCircle(C, R * 0.36f, Ink);
            break;
        case 8u:
            Surface.FillRectangle({ C.X - R * 0.30f, C.Y - R, R * 0.60f, R * 2.0f }, Ink, 2.0f);
            Surface.FillRectangle({ C.X - R, C.Y - R * 0.30f, R * 2.0f, R * 0.60f }, Ink, 2.0f);
            break;
        case 9u:
            Surface.StrokeLine({ C.X - R, C.Y + R * 0.35f }, { C.X, C.Y - R * 0.55f }, Ink, 3.2f);
            Surface.StrokeLine({ C.X, C.Y - R * 0.55f }, { C.X + R, C.Y + R * 0.35f }, Ink, 3.2f);
            break;
        case 10u:
            Surface.FillRectangle({ C.X - R * 1.25f, C.Y - R * 0.36f, R * 2.5f, R * 0.72f }, Ink, 3.0f);
            break;
        case 11u:
            Surface.StrokeCircle(C, R, Ink, 3.0f);
            Surface.FillCircle({ C.X + R * 0.36f, C.Y - R * 0.20f }, R * 0.75f, ColorRgba{ 14u, 15u, 18u, 255u });
            break;
        case 12u:
            Surface.FillCircle({ C.X, C.Y + R * 0.22f }, R * 0.68f, Ink);
            Surface.StrokeLine({ C.X - R * 0.55f, C.Y + R * 0.05f }, { C.X, C.Y - R }, Ink, 2.8f);
            Surface.StrokeLine({ C.X + R * 0.55f, C.Y + R * 0.05f }, { C.X, C.Y - R }, Ink, 2.8f);
            break;
        case 13u:
            Surface.FillCircle(C, R * 0.65f, Ink);
            Surface.StrokeLine({ C.X - R * 0.85f, C.Y + R * 0.65f }, { C.X + R * 0.85f, C.Y - R * 0.65f }, ColorRgba{ 14u, 15u, 18u, 255u }, 1.8f);
            break;
        case 14u:
            for (int K = 0; K < 5; ++K)
            {
                const float A = static_cast<float>(K) * 1.256637f;
                Surface.FillCircle({ C.X + std::cos(A) * R * 0.52f, C.Y + std::sin(A) * R * 0.52f }, R * 0.44f, Ink);
            }
            break;
        default:
            Surface.StrokeCircle(C, R * 0.75f, Ink, 3.0f);
            for (int K = 0; K < 8; ++K)
            {
                const float A = static_cast<float>(K) * 0.785398f;
                Surface.StrokeLine({ C.X + std::cos(A) * R * 0.55f, C.Y + std::sin(A) * R * 0.55f }, { C.X + std::cos(A) * R, C.Y + std::sin(A) * R }, Ink, 2.5f);
            }
            break;
    }
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
    ToolStudioModalOpen = false;
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
        if (Input.SecondaryPressed && CircleR.Contains(Cursor))
        {
            Session->ActiveTool = Tool.Category;
            CloseAllSheets();
            ToolStudioModalOpen = true;
            ToolStudioSubTab    = 0u;
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

        // Viewport-placed Linear Gradient handle line & Point Field numbered pins (fill-linear-scene.png & fill-point-field.png parity)
        if (const PatternStampLayerSpec* SelLayer = Session->InspectSelectedLayer())
        {
            const float TileX0 = CanvasRegion.X + RulerMetrics.TileMinX;
            const float TileY0 = CanvasRegion.Y + RulerMetrics.TileMinY;
            const float TileS  = RulerMetrics.TileSpanPx;

            Surface.PushClip(CanvasRegion);
            if (SelLayer->FillSource == PatternStampFillSourceMode::LinearGradient)
            {
                const float AngRad = SelLayer->GradientAngleDeg * 0.0174532925f;
                const float Cx     = TileX0 + TileS * 0.50f;
                const float Cy     = TileY0 + TileS * 0.50f;
                const float HalfL  = TileS * 0.35f;
                const Point2D P0{ Cx - std::cos(AngRad) * HalfL, Cy - std::sin(AngRad) * HalfL };
                const Point2D P1{ Cx + std::cos(AngRad) * HalfL, Cy + std::sin(AngRad) * HalfL };
                const Point2D Pm{ (P0.X + P1.X) * 0.5f, (P0.Y + P1.Y) * 0.5f };

                Surface.StrokeLine(P0, P1, ColorRgba{ 16u, 17u, 20u, 200u }, 3.5f);
                Surface.StrokeLine(P0, P1, ColorRgba{ 248u, 248u, 250u, 235u }, 1.8f);

                Surface.FillCircle(P0, 9.0f, SelLayer->FillTint);
                Surface.StrokeCircle(P0, 9.0f, ColorRgba{ 255u, 255u, 255u, 245u }, 2.0f);
                Surface.FillCircle(Pm, 6.5f, ColorRgba{
                    static_cast<uint8_t>((SelLayer->FillTint.Red   + SelLayer->FillTintB.Red)   / 2u),
                    static_cast<uint8_t>((SelLayer->FillTint.Green + SelLayer->FillTintB.Green) / 2u),
                    static_cast<uint8_t>((SelLayer->FillTint.Blue  + SelLayer->FillTintB.Blue)  / 2u),
                    255u });
                Surface.StrokeCircle(Pm, 6.5f, ColorRgba{ 255u, 255u, 255u, 230u }, 1.5f);
                Surface.FillCircle(P1, 9.0f, SelLayer->FillTintB);
                Surface.StrokeCircle(P1, 9.0f, ColorRgba{ 255u, 255u, 255u, 245u }, 2.0f);
            }
            else if (SelLayer->FillSource == PatternStampFillSourceMode::PointField || Session->ActiveTool == PatternStampToolCategory::PointGradient)
            {
                for (std::size_t PIdx = 0u; PIdx < 4u; ++PIdx)
                {
                    const auto& Pin = SelLayer->PointFieldPins[PIdx];
                    const Point2D PinPt{ TileX0 + Pin.NormX * TileS, TileY0 + Pin.NormY * TileS };
                    Surface.StrokeCircle(PinPt, 18.0f, ColorRgba{ Pin.Tint.Red, Pin.Tint.Green, Pin.Tint.Blue, 130u }, 1.5f);
                    Surface.FillCircle(PinPt, 10.0f, Pin.Tint);
                    Surface.StrokeCircle(PinPt, 10.0f, ColorRgba{ 255u, 255u, 255u, 240u }, 1.8f);
                    char NumBuf[16];
                    std::snprintf(NumBuf, sizeof(NumBuf), "%zu", PIdx + 1u);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Bold, { PinPt.X - 3.2f, PinPt.Y - 6.0f }, 9.0f, ColorRgba{ 16u, 17u, 20u, 255u }, NumBuf);
                }
            }
            Surface.PopClip();
        }
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

    // 6. Floating Tool Context Menu (.atm-menu from ArtworkToolMenu.js — menu-shapes.png & menu-gradient.png 1:1 parity)
    if (ToolStudioModalOpen)
    {
        const bool IsGradStudio  = (Session->ActiveTool == PatternStampToolCategory::PointGradient);
        const bool IsCurveStudio = (Session->ActiveTool == PatternStampToolCategory::Curve ||
                                    Session->ActiveTool == PatternStampToolCategory::Pen ||
                                    Session->ActiveTool == PatternStampToolCategory::EditPoints);

        const float MenuW = IsGradStudio || IsCurveStudio ? 368.0f : 412.0f;
        const float MenuH = IsGradStudio ? 426.0f : (IsCurveStudio ? 310.0f : 564.0f);
        const float MenuX = std::clamp(Bounds.X + 118.0f, Bounds.X + 12.0f, Bounds.Right() - MenuW - 12.0f);
        const float MenuY = std::min(Band2Rect.Bottom() + 6.0f, Bounds.Bottom() - MenuH - 12.0f);
        const Rectangle2D ModalR{ MenuX, MenuY, MenuW, MenuH };

        // Outer .atm-menu frame (#121212 panel fill, 22px radius, --line-strong border, no canvas dim backdrop!)
        Surface.FillRectangle(ModalR, ColorRgba{ 18u, 18u, 20u, 250u }, 22.0f);
        Surface.StrokeRectangle(ModalR, ColorRgba{ 255u, 255u, 255u, 42u }, 22.0f, 1.0f);

        const char* AtmTitle = IsGradStudio ? "Gradient" : (IsCurveStudio ? "Path" : "Shapes");
        const char* AtmTool  = IsGradStudio ? "GRADIENT" : (IsCurveStudio ? "CURVE" : "CIRCLE");

        // .atm-bar header
        const Rectangle2D HeadR{ ModalR.X, ModalR.Y, ModalR.Width, 40.0f };
        Surface.StrokeLine({ HeadR.X, HeadR.Bottom() }, { HeadR.Right(), HeadR.Bottom() }, ColorRgba{ 255u, 255u, 255u, 20u }, 1.0f);
        const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.8f, AtmTitle);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { HeadR.X + 16.0f, HeadR.Y + 12.0f }, 10.8f, Theme.TextPrimary, AtmTitle);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { HeadR.X + 24.0f + TitleW, HeadR.Y + 14.5f }, 8.2f,
            ColorRgba{ 232u, 194u, 122u, 255u }, AtmTool);

        const Rectangle2D CloseBtnR{ HeadR.Right() - 36.0f, HeadR.Y + 8.0f, 24.0f, 24.0f };
        Surface.FillRectangle(CloseBtnR, CloseBtnR.Contains(Cursor) ? ColorRgba{ 48u, 50u, 56u, 255u } : ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::CloseCross, VectorIconPlacement{ CloseBtnR.X + 7.0f, CloseBtnR.Y + 7.0f, 10.0f, Theme.TextMuted, 1.65f });
        if (Pressed && CloseBtnR.Contains(Cursor))
        {
            ToolStudioModalOpen = false;
        }

        const float Bx = ModalR.X + 12.0f;
        const float Bw = ModalR.Width - 24.0f;
        float       By = HeadR.Bottom() + 8.0f;
        PatternStampLayerSpec* SelLayer = Session->InspectSelectedLayer();

        auto DrawAtmSeg = [&](float Y, std::span<const char* const> Labels, std::size_t ActiveIdx) -> std::size_t
        {
            const Rectangle2D SegR{ Bx + 10.0f, Y, Bw - 20.0f, 28.0f };
            Surface.FillRectangle(SegR, ColorRgba{ 5u, 5u, 6u, 255u }, 14.0f);
            const float BtnW = (SegR.Width - 6.0f) / static_cast<float>(Labels.size());
            std::size_t Picked = ActiveIdx;
            for (std::size_t I = 0u; I < Labels.size(); ++I)
            {
                const Rectangle2D Br{ SegR.X + 3.0f + static_cast<float>(I) * BtnW, SegR.Y + 3.0f, BtnW, 22.0f };
                const bool Act = (I == ActiveIdx);
                if (Act)
                {
                    Surface.FillRectangle(Br, ColorRgba{ 239u, 244u, 207u, 255u }, 11.0f);
                }
                const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, Act ? FontWeightClass::SemiBold : FontWeightClass::Regular, 8.8f, Labels[I]);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, Act ? FontWeightClass::SemiBold : FontWeightClass::Regular,
                    { Br.X + (Br.Width - Tw) * 0.5f, Br.Y + 4.5f }, 8.8f,
                    Act ? ColorRgba{ 23u, 25u, 14u, 255u } : Theme.TextMuted, Labels[I]);
                if (Pressed && Br.Contains(Cursor))
                {
                    Picked = I;
                }
            }
            return Picked;
        };

        auto DrawAtmColourPill = [&](float X, float Y, float W, ColorRgba Col, const char* HexText)
        {
            const Rectangle2D PillR{ X, Y, W, 28.0f };
            Surface.FillRectangle(PillR, ColorRgba{ 5u, 5u, 6u, 255u }, 14.0f);
            Surface.FillCircle({ PillR.X + 14.0f, PillR.Y + 14.0f }, 10.0f, Col);
            Surface.StrokeCircle({ PillR.X + 14.0f, PillR.Y + 14.0f }, 10.0f, ColorRgba{ 255u, 255u, 255u, 36u }, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { PillR.X + 30.0f, PillR.Y + 7.5f }, 8.6f, Theme.TextMuted, HexText);
        };

        if (IsGradStudio)
        {
            // Section 1: TARGET (Fill | Outline | Ground + Editing Solid Disc / Fine Accent Rings)
            const Rectangle2D Sec1{ Bx, By, Bw, 80.0f };
            Surface.FillRectangle(Sec1, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "TARGET");
            static constexpr const char* TargetTabs[3] { "Fill", "Outline", "Ground" };
            (void)DrawAtmSeg(By + 24.0f, TargetTabs, 0u);
            char EditNote[64];
            std::snprintf(EditNote, sizeof(EditNote), "Editing %s", SelLayer != nullptr ? SelLayer->Name.c_str() : "Solid Disc");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bx + 10.0f, By + 58.0f }, 8.4f, Theme.TextMuted, EditNote);
            By += 88.0f;

            // Section 2: SOURCE (Linear | Radial | Point field + Interpolation dropdown)
            const Rectangle2D Sec2{ Bx, By, Bw, 94.0f };
            Surface.FillRectangle(Sec2, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "SOURCE");
            static constexpr const char* SourceTabs[3] { "Linear", "Radial", "Point field" };
            const std::size_t CurSrc = (SelLayer != nullptr && SelLayer->FillSource == PatternStampFillSourceMode::PointField) ? 2u : 1u;
            (void)DrawAtmSeg(By + 24.0f, SourceTabs, CurSrc);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bx + 10.0f, By + 64.0f }, 8.8f, Theme.TextMuted, "Interpolation");
            const Rectangle2D InterpR{ Bx + 96.0f, By + 58.0f, Bw - 106.0f, 26.0f };
            Surface.FillRectangle(InterpR, ColorRgba{ 5u, 5u, 6u, 255u }, 13.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { InterpR.X + 12.0f, InterpR.Y + 6.0f }, 8.8f, Theme.TextPrimary, "Step");
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::ChevronDown, VectorIconPlacement{ InterpR.Right() - 16.0f, InterpR.Y + 8.5f, 9.0f, Theme.TextMuted, 1.6f });
            By += 102.0f;

            // Section 3: STOPS (ramp bar + 3 colour stop pills + help note)
            const Rectangle2D Sec3{ Bx, By, Bw, 134.0f };
            Surface.FillRectangle(Sec3, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "STOPS");
            const Rectangle2D RampR{ Bx + 10.0f, By + 24.0f, Bw - 20.0f, 12.0f };
            const std::array<ColorRgba, 3u> Stops {{
                { 234u, 236u, 239u, 255u },
                { 162u, 177u, 192u, 255u },
                {  90u, 118u, 146u, 255u },
            }};
            const float SegW = RampR.Width / 3.0f;
            for (std::size_t S = 0u; S < 3u; ++S)
            {
                Surface.FillRectangle({ RampR.X + static_cast<float>(S) * SegW, RampR.Y, SegW + 1.0f, RampR.Height }, Stops[S], 6.0f);
            }
            DrawAtmColourPill(Bx + 10.0f,   By + 44.0f, 88.0f, Stops[0], "#EAECEF");
            DrawAtmColourPill(Bx + 104.0f,  By + 44.0f, 88.0f, Stops[1], "#A2B1C0");
            DrawAtmColourPill(Bx + 198.0f,  By + 44.0f, 88.0f, Stops[2], "#5A7692");
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { Bx + 10.0f, By + 82.0f }, Bw - 20.0f, 8.0f, Theme.TextFaint,
                "Drag the centre to move the ramp, the rim handle to size it. Click the");
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { Bx + 10.0f, By + 96.0f }, Bw - 20.0f, 8.0f, Theme.TextFaint,
                "line to add a stop, drag stops along it, double-click one to remove it.");
            By += 142.0f;

            // Reset placement button on bottom-right
            const Rectangle2D ResetBtnR{ ModalR.Right() - 118.0f, By, 106.0f, 26.0f };
            Surface.FillRectangle(ResetBtnR, ColorRgba{ 34u, 35u, 39u, 255u }, 13.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { ResetBtnR.X + 14.0f, ResetBtnR.Y + 6.0f }, 8.4f, Theme.TextPrimary, "Reset placement");
        }
        else if (IsCurveStudio)
        {
            // Path menu (ArtworkToolMenu.js pathView 1:1): CURVE (Smoothing) + LOOK (Outline width, Colour, Fill closed paths)
            const Rectangle2D Sec1{ Bx, By, Bw, 54.0f };
            Surface.FillRectangle(Sec1, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "CURVE");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,  { Bx + 10.0f, By + 28.0f }, 8.8f, Theme.TextMuted, "Smoothing");
            const Rectangle2D STrack1{ Bx + 96.0f, By + 32.0f, Bw - 146.0f, 6.0f };
            Surface.FillRectangle(STrack1, ColorRgba{ 48u, 50u, 56u, 255u }, 3.0f);
            Surface.FillCircle({ STrack1.X + STrack1.Width * 0.5f, STrack1.Y + 3.0f }, 6.5f, ColorRgba{ 235u, 235u, 235u, 255u });
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { Bx + Bw - 10.0f, By + 28.0f }, 8.4f, Theme.TextPrimary, "0.5");
            By += 62.0f;

            const Rectangle2D Sec2{ Bx, By, Bw, 128.0f };
            Surface.FillRectangle(Sec2, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "LOOK");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,  { Bx + 10.0f, By + 28.0f }, 8.8f, Theme.TextMuted, "Outline width");
            const Rectangle2D STrack2{ Bx + 96.0f, By + 32.0f, Bw - 146.0f, 6.0f };
            Surface.FillRectangle(STrack2, ColorRgba{ 48u, 50u, 56u, 255u }, 3.0f);
            Surface.FillCircle({ STrack2.X + STrack2.Width * 0.25f, STrack2.Y + 3.0f }, 6.5f, ColorRgba{ 235u, 235u, 235u, 255u });
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { Bx + Bw - 10.0f, By + 28.0f }, 8.4f, Theme.TextPrimary, "6 px");
            DrawAtmColourPill(Bx + 10.0f, By + 50.0f, Bw - 20.0f, ColorRgba{ 239u, 244u, 207u, 255u }, "#EFF4CF");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bx + 10.0f, By + 86.0f }, 8.8f, Theme.TextPrimary, "Fill closed paths");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bx + 10.0f, By + 100.0f }, 7.6f, Theme.TextFaint, "Closing on the first point makes a filled shape");
            Surface.FillRectangle({ Bx + Bw - 44.0f, By + 90.0f, 34.0f, 18.0f }, ColorRgba{ 255u, 255u, 255u, 255u }, 9.0f);
            Surface.FillCircle({ Bx + Bw - 19.0f, By + 99.0f }, 6.5f, ColorRgba{ 17u, 17u, 17u, 255u });
            By += 138.0f;

            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { Bx + 4.0f, By }, Bw - 8.0f, 8.0f, Theme.TextFaint,
                "Click to add points · click the first point or press Enter to finish.");
        }
        else
        {
            // Shapes menu (menu-shapes.png 1:1): SHAPE (4x5 grid of 17 tiles) + FILL + OUTLINE
            static constexpr std::array<const char*, 17u> MotifNames {{
                "Circle", "Ring", "Square", "Rounded Box",
                "Diamond", "Hexagon", "Triangle", "Star",
                "Cross", "Chevron", "Stripe", "Arc",
                "Teardrop", "Botanical Leaf", "5-Petal Flower", "Cog",
                "Line"
            }};
            const Rectangle2D Sec1{ Bx, By, Bw, 322.0f };
            Surface.FillRectangle(Sec1, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "SHAPE");

            const float GridX = Bx + 10.0f;
            const float GridY = By + 24.0f;
            const float Gap   = 6.0f;
            const float TileW = (Bw - 20.0f - Gap * 3.0f) / 4.0f;
            const float TileH = 52.0f;
            for (std::size_t S = 0u; S < 17u; ++S)
            {
                const std::size_t Col = S % 4u;
                const std::size_t Row = S / 4u;
                const Rectangle2D TileR{
                    GridX + static_cast<float>(Col) * (TileW + Gap),
                    GridY + static_cast<float>(Row) * (TileH + Gap),
                    TileW,
                    TileH
                };
                const bool Act = (S == 0u);
                Surface.FillRectangle(TileR, Act ? ColorRgba{ 36u, 37u, 42u, 255u } : ColorRgba{ 5u, 5u, 6u, 255u }, 10.0f);
                if (Act)
                {
                    Surface.StrokeRectangle(TileR, ColorRgba{ 255u, 255u, 255u, 44u }, 10.0f, 1.0f);
                }

                const Rectangle2D IconWell{ TileR.X + 8.0f, TileR.Y + 4.0f, TileR.Width - 16.0f, 26.0f };
                DrawMotifPreviewInWell(Surface, IconWell, S, Act ? ColorRgba{ 239u, 244u, 207u, 255u } : ColorRgba{ 240u, 240u, 242u, 255u });

                const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 7.8f, MotifNames[S]);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular,
                    { TileR.X + std::max(4.0f, (TileR.Width - Tw) * 0.5f), TileR.Bottom() - 15.0f },
                    TileR.Width - 8.0f, 7.8f, Act ? Theme.TextPrimary : Theme.TextMuted, MotifNames[S]);

                if (Pressed && TileR.Contains(Cursor) && SelLayer != nullptr && S < 16u)
                {
                    SelLayer->MotifShape = static_cast<PatternStampMotifShape>(S);
                    Session->BumpRevision();
                }
            }
            By += 330.0f;

            // Section 2: FILL (Solid | Linear | Radial | Point + #E8C27A pill)
            const Rectangle2D Sec2{ Bx, By, Bw, 92.0f };
            Surface.FillRectangle(Sec2, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "FILL");
            static constexpr const char* FillTabs[4] { "Solid", "Linear", "Radial", "Point" };
            (void)DrawAtmSeg(By + 24.0f, FillTabs, 0u);
            DrawAtmColourPill(Bx + 10.0f, By + 58.0f, 92.0f, ColorRgba{ 232u, 194u, 122u, 255u }, "#E8C27A");
            By += 100.0f;

            // Section 3: OUTLINE (Width 0 px + #16181C pill)
            const Rectangle2D Sec3{ Bx, By, Bw, 82.0f };
            Surface.FillRectangle(Sec3, ColorRgba{ 28u, 29u, 33u, 255u }, 14.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Bx + 10.0f, By + 9.0f }, 7.6f, Theme.TextFaint, "OUTLINE");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,  { Bx + 10.0f, By + 26.0f }, 8.8f, Theme.TextMuted, "Width");
            const Rectangle2D STrack{ Bx + 86.0f, By + 30.0f, Bw - 136.0f, 6.0f };
            Surface.FillRectangle(STrack, ColorRgba{ 48u, 50u, 56u, 255u }, 3.0f);
            Surface.FillCircle({ STrack.X + 4.0f, STrack.Y + 3.0f }, 6.5f, ColorRgba{ 235u, 235u, 235u, 255u });
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { Bx + Bw - 10.0f, By + 26.0f }, 8.4f, Theme.TextPrimary, "0 px");
            DrawAtmColourPill(Bx + 10.0f, By + 46.0f, Bw - 20.0f, ColorRgba{ 22u, 24u, 28u, 255u }, "#16181C");
        }

        if (Pressed && !ModalR.Contains(Cursor))
        {
            ToolStudioModalOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
