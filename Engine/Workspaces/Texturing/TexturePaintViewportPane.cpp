/*==========================================================================================================================================
                                                    TEXTUREPAINTVIEWPORTPANE.CPP
==========================================================================================================================================*/
// 🖼️ Implementation of TexturePaintViewportPane: 2-band CardDock header, real 3D/2D PBR viewport & floating Brush Studio modal.

#include "Engine/Workspaces/Texturing/TexturePaintViewportPane.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace Frontier::Workspaces
{

namespace
{

using namespace TexturingAppearance;

struct ToolButtonSpec
{
    PaintToolCategory Tool;
    ControlIcon       Icon;
    const char*       Title;
};

constexpr std::array<ToolButtonSpec, 9u> ViewportToolCatalog {{
    { PaintToolCategory::Brush,      ControlIcon::BrushTool,      "Paint Brush"    },
    { PaintToolCategory::Eraser,     ControlIcon::EraserTool,     "Eraser"         },
    { PaintToolCategory::Fill,       ControlIcon::FillBucket,     "Fill"           },
    { PaintToolCategory::Gradient3D, ControlIcon::PointGradient,  "3D Gradient"    },
    { PaintToolCategory::Curve3D,    ControlIcon::VectorCurve,    "3D Curve"       },
    { PaintToolCategory::Stamp,      ControlIcon::StampTool,      "Stamp"          },
    { PaintToolCategory::Text,       ControlIcon::TextType,       "Text"           },
    { PaintToolCategory::Sample,     ControlIcon::PipetteDropper, "Eyedropper"     },
    { PaintToolCategory::Move,       ControlIcon::MoveArrows,     "Transform"      },
}};

struct BrushStudioSectionSpec
{
    BrushDetailSubTab SubTab;
    ControlIcon       Icon;
    const char*       Label;
    const char*       CountBadge;
};

constexpr std::array<BrushStudioSectionSpec, 14u> BrushSectionsCatalog {{
    { BrushDetailSubTab::LibraryGrid,    ControlIcon::LayersSlabs,          "Library",         "12" },
    { BrushDetailSubTab::StrokePath,     ControlIcon::NoiseWave,            "Stroke path",     ""   },
    { BrushDetailSubTab::Stabilization,  ControlIcon::MotionActivity,       "Stabilization",   ""   },
    { BrushDetailSubTab::Taper,          ControlIcon::VectorPen,            "Taper",           ""   },
    { BrushDetailSubTab::Shape,          ControlIcon::ShapeCircle,          "Shape",           ""   },
    { BrushDetailSubTab::Grain,          ControlIcon::PatternTile,          "Grain",           ""   },
    { BrushDetailSubTab::Rendering,      ControlIcon::SparklesAntiAliasing, "Rendering",       ""   },
    { BrushDetailSubTab::WetMix,         ControlIcon::FillBucket,           "Wet mix",         ""   },
    { BrushDetailSubTab::ColourDynamics, ControlIcon::AppearanceSwatches,   "Colour dynamics", ""   },
    { BrushDetailSubTab::Curves,         ControlIcon::GaugeRate,            "Dynamics",        ""   },
    { BrushDetailSubTab::Stylus,         ControlIcon::VectorPointer,        "Stylus",          ""   },
    { BrushDetailSubTab::Properties,     ControlIcon::SlidersQuality,       "Properties",      ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::MaterialSphere,       "Materials",       ""   },
    { BrushDetailSubTab::About,          ControlIcon::CircleNotice,         "About",           ""   },
}};

constexpr std::array<BrushStudioSectionSpec, 5u> TextSectionsCatalog {{
    { BrushDetailSubTab::LibraryGrid,    ControlIcon::LayersSlabs,          "Library",         "6"  },
    { BrushDetailSubTab::StrokePath,     ControlIcon::TextType,             "Text",            ""   },
    { BrushDetailSubTab::Properties,     ControlIcon::SlidersQuality,       "Type & layout",   ""   },
    { BrushDetailSubTab::Rendering,      ControlIcon::FillBucket,           "Look",            ""   },
    { BrushDetailSubTab::Channels,       ControlIcon::LayersSlabs,          "Channels",        ""   },
}};

void UploadViewportTexturePixels(
    ImTextureData&              TargetTex,
    uint32_t&                   InOutW,
    uint32_t&                   InOutH,
    uint32_t                    TargetW,
    uint32_t                    TargetH,
    const std::vector<uint8_t>& RgbaPixels) noexcept
{
    if (RgbaPixels.size() < static_cast<std::size_t>(TargetW) * TargetH * 4u || TargetW == 0u || TargetH == 0u)
    {
        return;
    }
    const bool NeedsCreate =
        TargetTex.Width != static_cast<int>(TargetW) ||
        TargetTex.Height != static_cast<int>(TargetH) ||
        TargetTex.Pixels == nullptr;
    if (NeedsCreate)
    {
        TargetTex.DestroyPixels();
        TargetTex.SetStatus(ImTextureStatus_Destroyed);
        TargetTex.Create(ImTextureFormat_RGBA32, static_cast<int>(TargetW), static_cast<int>(TargetH));
        InOutW = TargetW;
        InOutH = TargetH;
    }
    std::memcpy(TargetTex.Pixels, RgbaPixels.data(), static_cast<std::size_t>(TargetW) * TargetH * 4u);
    if (!NeedsCreate && TargetTex.Status == ImTextureStatus_OK)
    {
        TargetTex.UpdateRect = { 0u, 0u, static_cast<unsigned short>(TargetW), static_cast<unsigned short>(TargetH) };
        TargetTex.SetStatus(ImTextureStatus_WantUpdates);
    }
}

void ConstructBrushStrokePreviewWave(
    PixelSpace&                          Surface,
    Rectangle2D                          TileBounds,
    const BrushLibraryPresetDescriptor&  Preset,
    bool                                 ActiveCard,
    const TexturingThemeColors&          Theme) noexcept
{
    const float StartX = TileBounds.X + 16.0f;
    const float EndX   = TileBounds.Right() - 16.0f;
    const float MidY   = TileBounds.Y + 18.0f;
    const float SpanX  = std::max(24.0f, EndX - StartX);
    const ColorRgba Ink = ActiveCard
        ? ColorRgba{ 245u, 245u, 242u, 245u }
        : ColorRgba{ 218u, 220u, 224u, 215u };

    const float BaseRadius = std::clamp(static_cast<float>(Preset.PixelRadius) * 0.24f, 1.8f, 6.8f);
    constexpr int Samples  = 34;
    for (int I = 0; I <= Samples; ++I)
    {
        const float T     = static_cast<float>(I) / static_cast<float>(Samples);
        const float Taper = std::sin(T * 3.14159f);
        const float X     = StartX + T * SpanX;
        const float Y     = MidY - std::sin(T * 6.28318f) * 7.2f;
        const float R     = std::max(1.2f, BaseRadius * (0.28f + 0.72f * Taper));

        if (Preset.TipShape == BrushTipShape::Splatter)
        {
            Surface.FillCircle({ X, Y }, R * 0.65f, Ink);
            if ((I % 3) == 0)
            {
                Surface.FillCircle({ X + 3.0f, Y - 4.5f }, 1.5f, Ink);
                Surface.FillCircle({ X - 2.5f, Y + 4.0f }, 1.2f, Ink);
            }
        }
        else if (Preset.TipShape == BrushTipShape::Chisel)
        {
            Surface.StrokeLine({ X - 2.0f, Y - R * 0.85f }, { X + 2.0f, Y + R * 0.85f }, Ink, std::max(2.0f, R * 0.9f));
        }
        else
        {
            Surface.FillCircle({ X, Y }, R, Ink);
        }
    }
    (void)Theme;
}

} // namespace

TexturePaintViewportPane::~TexturePaintViewportPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
    SpatialTexture.DestroyPixels();
    UvCanvasTexture.DestroyPixels();
}

void TexturePaintViewportPane::AttachSession(TexturePaintSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void TexturePaintViewportPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##TexturePaintViewportCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

void TexturePaintViewportPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  /*DeltaSeconds*/)
{
    if (Session == nullptr || Bounds.Width < 80.0f || Bounds.Height < 80.0f)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingTheme(Surface);
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground);

    const Point2D Cursor       { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed      = Input.PrimaryPointer.Pressed;
    const bool    RightPressed = Input.SecondaryPressed;
    const bool    PopupOpen    = BrushStudioModalOpen || ResolutionSheetOpen || ViewModeSheetOpen || MoreToolsSheetOpen || SaveSheetOpen;

    //======================================================================================================================
    // 1. TOP HEADER BAND 1 (Document Name | RESOLUTION [512 PX v] | 9 Circular Tool Buttons + More tools v)
    //======================================================================================================================
    const Rectangle2D Band1R{ Bounds.X, Bounds.Y, Bounds.Width, 42.0f };
    Surface.FillRectangle(Band1R, Theme.SectionSurface);
    Surface.StrokeLine({ Band1R.X, Band1R.Bottom() - 0.5f }, { Band1R.Right(), Band1R.Bottom() - 0.5f }, Theme.BorderSubtle, 1.0f);

    float LeftX = Band1R.X + 12.0f;
    const float Band1MidY = Band1R.Y + 7.0f;

    const Rectangle2D DocNameR{ LeftX, Band1MidY, 132.0f, 28.0f };
    if (!DocNameBuffer.Active && std::string_view(DocNameBuffer.Text, DocNameBuffer.Length) != Session->DocumentName)
    {
        DocNameBuffer.Begin(Session->DocumentName.c_str());
        DocNameBuffer.Active = false;
    }
    Surface.FillRectangle(DocNameR, Theme.WellSurface, 8.0f);
    Surface.StrokeRectangle(DocNameR, DocNameBuffer.Active ? Theme.BorderStrong : Theme.BorderSubtle, 8.0f, 1.0f);
    const ControlContact DocContact = TextInputField::Construct(
        Surface,
        { DocNameR.X + 6.0f, DocNameR.Y + 1.0f, DocNameR.Width - 12.0f, DocNameR.Height - 2.0f },
        DocNameBuffer,
        Input.PrimaryPointer,
        10.2f);
    if ((DocContact.Clicked || DocNameBuffer.Finalized) && DocNameBuffer.Length > 0u)
    {
        Session->DocumentName.assign(DocNameBuffer.Text, DocNameBuffer.Length);
    }
    LeftX = DocNameR.Right() + 12.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { LeftX, Band1MidY + 8.0f }, 8.2f, Theme.TextFaint, "RESOLUTION");
    LeftX += 64.0f;

    char ResBtnText[24];
    std::snprintf(ResBtnText, sizeof(ResBtnText), "%u PX", Session->ResolutionPx);
    const Rectangle2D ResBtnR{ LeftX, Band1MidY + 1.0f, 76.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, ResBtnR, ControlIcon::Count, ResBtnText, true, ResolutionSheetOpen, Theme, Cursor, Pressed))
    {
        ResolutionSheetOpen = !ResolutionSheetOpen;
        ViewModeSheetOpen   = false;
        MoreToolsSheetOpen  = false;
        SaveSheetOpen       = false;
    }

    const Rectangle2D MoreToolsBtnR{ Band1R.Right() - 12.0f - 86.0f, Band1MidY + 1.0f, 86.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, MoreToolsBtnR, ControlIcon::Count, "More tools", true, MoreToolsSheetOpen, Theme, Cursor, Pressed))
    {
        MoreToolsSheetOpen  = !MoreToolsSheetOpen;
        ResolutionSheetOpen = false;
        ViewModeSheetOpen   = false;
        SaveSheetOpen       = false;
    }

    constexpr float ToolBtnSz   = 28.0f;
    constexpr float ToolBtnStep = 31.0f;
    const float ToolsStartX = MoreToolsBtnR.X - 8.0f - static_cast<float>(ViewportToolCatalog.size()) * ToolBtnStep;
    for (std::size_t I = 0u; I < ViewportToolCatalog.size(); ++I)
    {
        const auto& Spec = ViewportToolCatalog[I];
        const Rectangle2D BtnR{ ToolsStartX + static_cast<float>(I) * ToolBtnStep, Band1MidY, ToolBtnSz, ToolBtnSz };
        if (BtnR.X < ResBtnR.Right() + 8.0f)
        {
            continue;
        }
        const bool IsActiveTool = (Session->ActiveTool == Spec.Tool);
        const bool Hover        = BtnR.Contains(Cursor);

        const ColorRgba BtnFill = IsActiveTool
            ? (Theme.LightSurface ? Theme.AccentFill : ColorRgba{ 242u, 242u, 240u, 255u })
            : (Hover ? Theme.HoverSurface : Theme.WellSurface);
        const ColorRgba BtnInk  = IsActiveTool
            ? (Theme.LightSurface ? Theme.HighlightInk : ColorRgba{ 18u, 19u, 22u, 255u })
            : (Hover ? Theme.TextPrimary : Theme.TextMuted);

        Surface.FillRectangle(BtnR, BtnFill, 14.0f);
        Surface.StrokeRectangle(BtnR, IsActiveTool ? BtnFill : Theme.BorderSubtle, 14.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            Spec.Icon,
            VectorIconPlacement{ BtnR.X + 6.5f, BtnR.Y + 6.5f, 15.0f, BtnInk, 1.75f });

        if (Hover && Pressed)
        {
            Session->SelectTool(Spec.Tool);
        }
        else if (Hover && RightPressed)
        {
            Session->SelectTool(Spec.Tool);
            BrushStudioModalOpen = true;
            ResolutionSheetOpen  = false;
            ViewModeSheetOpen    = false;
            MoreToolsSheetOpen   = false;
        }
    }

    //======================================================================================================================
    // 2. TOP HEADER BAND 2 (VIEW [3D Mesh v] | Undo / Redo | Save v | Status Pill "Vulkan GPU")
    //======================================================================================================================
    const Rectangle2D Band2R{ Bounds.X, Band1R.Bottom(), Bounds.Width, 38.0f };
    Surface.FillRectangle(Band2R, Theme.SectionSurface);
    Surface.StrokeLine({ Band2R.X, Band2R.Bottom() - 0.5f }, { Band2R.Right(), Band2R.Bottom() - 0.5f }, Theme.BorderSubtle, 1.0f);

    float B2X = Band2R.X + 12.0f;
    const float B2Y = Band2R.Y + 6.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { B2X, B2Y + 8.0f }, 8.2f, Theme.TextFaint, "VIEW");
    B2X += 36.0f;

    const char* ViewBtnLabel = "3D Mesh";
    if (Session->ViewportLayout == PaintViewportLayout::SplitView)     ViewBtnLabel = "3D + 2D Split";
    else if (Session->ViewportLayout == PaintViewportLayout::TwoDOnly) ViewBtnLabel = "2D UV Only";
    else if (Session->ViewChannelIndex > 0u && Session->ViewChannelIndex < TexturePaintSession::QueryViewChannelLabels().size())
    {
        ViewBtnLabel = TexturePaintSession::QueryViewChannelLabels()[Session->ViewChannelIndex];
    }

    const Rectangle2D ViewBtnR{ B2X, B2Y, 108.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, ViewBtnR, ControlIcon::Count, ViewBtnLabel, true, ViewModeSheetOpen, Theme, Cursor, Pressed))
    {
        ViewModeSheetOpen   = !ViewModeSheetOpen;
        ResolutionSheetOpen = false;
        MoreToolsSheetOpen  = false;
        SaveSheetOpen       = false;
    }

    const float StatusW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.2f, Session->StatusBanner) + 28.0f;
    const Rectangle2D StatusPillR{ Band2R.Right() - 12.0f - StatusW, B2Y + 1.0f, StatusW, 24.0f };
    Surface.FillRectangle(StatusPillR, Theme.WellSurface, 12.0f);
    Surface.StrokeRectangle(StatusPillR, Theme.BorderSubtle, 12.0f, 1.0f);
    Surface.FillCircle({ StatusPillR.X + 11.0f, StatusPillR.Y + 12.0f }, 3.5f, ColorRgba{ 118u, 212u, 134u, 255u });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { StatusPillR.X + 19.0f, StatusPillR.Y + 5.0f }, 9.2f, Theme.TextMuted, Session->StatusBanner);

    const Rectangle2D SaveBtnR{ StatusPillR.X - 8.0f - 62.0f, B2Y, 62.0f, 26.0f };
    if (ConstructHeaderActionCapsule(Surface, SaveBtnR, ControlIcon::Count, "Save", true, SaveSheetOpen, Theme, Cursor, Pressed))
    {
        SaveSheetOpen         = !SaveSheetOpen;
        Session->StatusBanner = "Saved project";
    }

    const Rectangle2D RedoBtnR{ SaveBtnR.X - 6.0f - 26.0f, B2Y, 26.0f, 26.0f };
    if (ConstructHeaderIconButton(Surface, RedoBtnR, ControlIcon::RedoArrow, false, Session->QueryRedoAvailable(), Theme, Cursor, Pressed))
    {
        Session->ApplyRedo();
    }

    const Rectangle2D UndoBtnR{ RedoBtnR.X - 5.0f - 26.0f, B2Y, 26.0f, 26.0f };
    if (ConstructHeaderIconButton(Surface, UndoBtnR, ControlIcon::UndoArrow, false, Session->QueryUndoAvailable(), Theme, Cursor, Pressed))
    {
        Session->ApplyUndo();
    }

    //======================================================================================================================
    // 3. VIEWPORT STAGE (3D Mesh Only | 3D + 2D UV Split | 2D UV Only)
    //======================================================================================================================
    const Rectangle2D StageOuterR{
        Bounds.X + 10.0f,
        Band2R.Bottom() + 10.0f,
        std::max(64.0f, Bounds.Width - 20.0f),
        std::max(64.0f, Bounds.Bottom() - Band2R.Bottom() - 20.0f)
    };

    LastSpatialCanvasRect = {};
    LastUvCanvasRect      = {};

    if (Session->ViewportLayout == PaintViewportLayout::ThreeDOnly)
    {
        LastSpatialCanvasRect = StageOuterR;
    }
    else if (Session->ViewportLayout == PaintViewportLayout::TwoDOnly)
    {
        LastUvCanvasRect = StageOuterR;
    }
    else
    {
        const float HalfW     = (StageOuterR.Width - 10.0f) * 0.5f;
        LastSpatialCanvasRect = { StageOuterR.X, StageOuterR.Y, HalfW, StageOuterR.Height };
        LastUvCanvasRect      = { StageOuterR.X + HalfW + 10.0f, StageOuterR.Y, HalfW, StageOuterR.Height };
    }

    if (LastSpatialCanvasRect.Width > 24.0f && LastSpatialCanvasRect.Height > 24.0f)
    {
        const uint32_t TargetW = std::clamp(static_cast<uint32_t>(LastSpatialCanvasRect.Width * 0.72f), 192u, 520u);
        const uint32_t TargetH = std::clamp(static_cast<uint32_t>(LastSpatialCanvasRect.Height * 0.72f), 192u, 460u);
        const bool NeedsReRender =
            (SpatialTexture.Pixels == nullptr) ||
            (SpatialTexWidth != TargetW) ||
            (SpatialTexHeight != TargetH) ||
            (SpatialTexRevision != Session->ContentRevision) ||
            (std::abs(CachedYaw - Session->CameraYawRadians) > 1.0e-4f) ||
            (std::abs(CachedPitch - Session->CameraPitchRadians) > 1.0e-4f) ||
            (std::abs(CachedDistance - Session->CameraDistanceMeters) > 1.0e-4f) ||
            (CachedViewChannel != Session->ViewChannelIndex) ||
            (CachedWireframe != Session->WireframeOverlay);

        if (NeedsReRender)
        {
            if (Session->RenderSpatialViewportRgba8(TargetW, TargetH, SpatialRgbaBuffer))
            {
                UploadViewportTexturePixels(SpatialTexture, SpatialTexWidth, SpatialTexHeight, TargetW, TargetH, SpatialRgbaBuffer);
                SpatialTexRevision = Session->ContentRevision;
                CachedYaw          = Session->CameraYawRadians;
                CachedPitch        = Session->CameraPitchRadians;
                CachedDistance     = Session->CameraDistanceMeters;
                CachedViewChannel  = Session->ViewChannelIndex;
                CachedWireframe    = Session->WireframeOverlay;
            }
        }
        if (SpatialTexture.Pixels != nullptr)
        {
            Surface.FillTexturedRectangle(LastSpatialCanvasRect, &SpatialTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
        }
        Surface.StrokeRectangle(LastSpatialCanvasRect, Theme.BorderSubtle, 10.0f, 1.0f);

        // 3D Gradient Point Gizmos ("1", "2", ...)
        const PaintLayerEntry* SelLayer = Session->InspectSelectedLayer();
        if (SelLayer != nullptr && (SelLayer->Category == PaintLayerCategory::Gradient || Session->ActiveTool == PaintToolCategory::Gradient3D))
        {
            for (std::size_t PtIdx = 0u; PtIdx < SelLayer->GradientPoints.size(); ++PtIdx)
            {
                const auto& Pt = SelLayer->GradientPoints[PtIdx];
                Point2D LocalPt {};
                if (Session->ProjectWorldPointToViewport(
                        Pt.PositionX,
                        Pt.PositionY,
                        Pt.PositionZ,
                        LastSpatialCanvasRect.Width,
                        LastSpatialCanvasRect.Height,
                        LocalPt))
                {
                    const Point2D ScreenPt{ LastSpatialCanvasRect.X + LocalPt.X, LastSpatialCanvasRect.Y + LocalPt.Y };
                    if (LastSpatialCanvasRect.Contains(ScreenPt))
                    {
                        const bool PtSelected = (SelLayer->SelectedGradientPointIndex == static_cast<int32_t>(PtIdx));
                        if (PtSelected)
                        {
                            Surface.StrokeCircle(ScreenPt, 13.5f, ColorRgba{ 255u, 255u, 255u, 230u }, 1.8f);
                        }
                        Surface.FillCircle(ScreenPt, 9.5f, Pt.TintColor);
                        Surface.StrokeCircle(ScreenPt, 9.5f, ColorRgba{ 18u, 19u, 22u, 240u }, 1.6f);

                        char NumBuf[24];
                        std::snprintf(NumBuf, sizeof(NumBuf), "%zu", PtIdx + 1u);
                        const uint32_t Lum = (static_cast<uint32_t>(Pt.TintColor.Red) * 299u +
                                              static_cast<uint32_t>(Pt.TintColor.Green) * 587u +
                                              static_cast<uint32_t>(Pt.TintColor.Blue) * 114u) / 1000u;
                        const ColorRgba NumInk = (Lum > 145u)
                            ? ColorRgba{ 16u, 17u, 20u, 255u }
                            : ColorRgba{ 248u, 248u, 250u, 255u };
                        WorkspaceAppearance::ConstructWeightedText(
                            Surface, FontWeightClass::Bold, { ScreenPt.X - 3.2f, ScreenPt.Y - 6.0f }, 9.2f, NumInk, NumBuf);

                        const float HitDx = Cursor.X - ScreenPt.X;
                        const float HitDy = Cursor.Y - ScreenPt.Y;
                        if (!PopupOpen && Pressed && (HitDx * HitDx + HitDy * HitDy <= 196.0f))
                        {
                            if (auto* MutLayer = Session->AccessSelectedLayer())
                            {
                                MutLayer->SelectedGradientPointIndex = static_cast<int32_t>(PtIdx);
                            }
                        }
                    }
                }
            }
        }

        if (!PopupOpen && LastSpatialCanvasRect.Contains(Cursor))
        {
            if (std::abs(Input.WheelDeltaY) > 1.0e-4f)
            {
                Session->ZoomCameraByFactor(Input.WheelDeltaY > 0.0f ? 0.91f : 1.10f);
            }
            if (Input.SecondaryDown)
            {
                Session->OrbitCameraByDelta(-Input.PointerDeltaX * 0.011f, Input.PointerDeltaY * 0.011f);
            }
            else if (Input.PrimaryPointer.Pressed || Input.PrimaryPointer.Down)
            {
                const float NormX = (Cursor.X - LastSpatialCanvasRect.X) / std::max(1.0f, LastSpatialCanvasRect.Width);
                const float NormY = (Cursor.Y - LastSpatialCanvasRect.Y) / std::max(1.0f, LastSpatialCanvasRect.Height);
                if (!Session->ApplyViewportPointerStroke(NormX, NormY, Input.PrimaryPointer.Pressed, false) &&
                    Input.PrimaryPointer.Down &&
                    (std::abs(Input.PointerDeltaX) > 0.1f || std::abs(Input.PointerDeltaY) > 0.1f))
                {
                    Session->OrbitCameraByDelta(-Input.PointerDeltaX * 0.010f, Input.PointerDeltaY * 0.010f);
                }
            }
        }
    }

    if (LastUvCanvasRect.Width > 24.0f && LastUvCanvasRect.Height > 24.0f)
    {
        constexpr uint32_t UvDim = 256u;
        if (UvCanvasTexture.Pixels == nullptr || UvTexRevision != Session->ContentRevision)
        {
            if (Session->RenderUvCanvasRgba8(UvDim, UvDim, UvRgbaBuffer))
            {
                UploadViewportTexturePixels(UvCanvasTexture, UvTexWidth, UvTexHeight, UvDim, UvDim, UvRgbaBuffer);
                UvTexRevision = Session->ContentRevision;
            }
        }
        if (UvCanvasTexture.Pixels != nullptr)
        {
            Surface.FillTexturedRectangle(LastUvCanvasRect, &UvCanvasTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
        }
        Surface.StrokeRectangle(LastUvCanvasRect, Theme.BorderSubtle, 10.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { LastUvCanvasRect.X + 10.0f, LastUvCanvasRect.Y + 8.0f },
            8.5f,
            Theme.TextMuted,
            "2D UV ATLAS");

        if (!PopupOpen && LastUvCanvasRect.Contains(Cursor) && (Input.PrimaryPointer.Pressed || Input.PrimaryPointer.Down))
        {
            const float NormX = (Cursor.X - LastUvCanvasRect.X) / std::max(1.0f, LastUvCanvasRect.Width);
            const float NormY = (Cursor.Y - LastUvCanvasRect.Y) / std::max(1.0f, LastUvCanvasRect.Height);
            (void)Session->ApplyViewportPointerStroke(NormX, NormY, Input.PrimaryPointer.Pressed, true);
        }
    }

    //======================================================================================================================
    // 4. HEADER DROPDOWN SHEETS (Resolution, View Mode, More Tools)
    //======================================================================================================================
    if (ResolutionSheetOpen)
    {
        constexpr std::array<uint32_t, 4u> ResOptions {{ 512u, 1024u, 2048u, 4096u }};
        const Rectangle2D SheetR{ ResBtnR.X, ResBtnR.Bottom() + 4.0f, 104.0f, 112.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (std::size_t I = 0u; I < ResOptions.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 25.0f, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (Session->ResolutionPx == ResOptions[I]);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            char Label[24];
            std::snprintf(Label, sizeof(Label), "%u px", ResOptions[I]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, 9.5f, Theme.TextPrimary, Label);
            if (Pressed && Hover)
            {
                Session->AssignResolution(ResOptions[I]);
                ResolutionSheetOpen = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !ResBtnR.Contains(Cursor))
        {
            ResolutionSheetOpen = false;
        }
    }

    if (ViewModeSheetOpen)
    {
        struct ViewModeOptionSpec
        {
            const char*         Label;
            PaintViewportLayout Layout;
            uint32_t            ChannelIdx;
        };
        constexpr std::array<ViewModeOptionSpec, 8u> ViewItems {{
            { "3D Mesh (Lit PBR)", PaintViewportLayout::ThreeDOnly, 0u },
            { "3D + 2D Split",     PaintViewportLayout::SplitView,  0u },
            { "2D UV Only",        PaintViewportLayout::TwoDOnly,   0u },
            { "Channel: Albedo",   PaintViewportLayout::ThreeDOnly, 1u },
            { "Channel: Roughness",PaintViewportLayout::ThreeDOnly, 2u },
            { "Channel: Metalness",PaintViewportLayout::ThreeDOnly, 3u },
            { "Channel: Height",   PaintViewportLayout::ThreeDOnly, 4u },
            { "Channel: Normal",   PaintViewportLayout::ThreeDOnly, 5u },
        }};
        const Rectangle2D SheetR{ ViewBtnR.X, ViewBtnR.Bottom() + 4.0f, 154.0f, static_cast<float>(ViewItems.size()) * 25.0f + 10.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (std::size_t I = 0u; I < ViewItems.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 25.0f, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (Session->ViewportLayout == ViewItems[I].Layout && Session->ViewChannelIndex == ViewItems[I].ChannelIdx);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, 9.4f, Theme.TextPrimary, ViewItems[I].Label);
            if (Pressed && Hover)
            {
                Session->ViewportLayout   = ViewItems[I].Layout;
                Session->ViewChannelIndex = ViewItems[I].ChannelIdx;
                ViewModeSheetOpen         = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !ViewBtnR.Contains(Cursor))
        {
            ViewModeSheetOpen = false;
        }
    }

    if (MoreToolsSheetOpen)
    {
        const auto& Studies = TexturePaintSession::QueryStudyRegistry();
        const Rectangle2D SheetR{ MoreToolsBtnR.Right() - 172.0f, MoreToolsBtnR.Bottom() + 4.0f, 172.0f, 210.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);

        float Sy = SheetR.Y + 6.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { SheetR.X + 10.0f, Sy }, 8.0f, Theme.TextFaint, "STUDY MESH");
        Sy += 16.0f;
        for (std::size_t I = 0u; I < Studies.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, Sy, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (Session->Study == Studies[I].Asset);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, 9.4f, Theme.TextPrimary, Studies[I].Label);
            if (Pressed && Hover)
            {
                Session->SelectStudy(Studies[I].Asset);
                MoreToolsSheetOpen = false;
                break;
            }
            Sy += 24.0f;
        }
        Sy += 4.0f;
        const Rectangle2D WireRowR{ SheetR.X + 5.0f, Sy, SheetR.Width - 10.0f, 24.0f };
        if (WireRowR.Contains(Cursor))
        {
            Surface.FillRectangle(WireRowR, Theme.HoverSurface, 6.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { WireRowR.X + 8.0f, WireRowR.Y + 4.5f }, 9.4f, Theme.TextPrimary,
            Session->WireframeOverlay ? "Hide Wireframe" : "Show Wireframe");
        if (Pressed && WireRowR.Contains(Cursor))
        {
            Session->WireframeOverlay = !Session->WireframeOverlay;
            MoreToolsSheetOpen        = false;
        }
        Sy += 26.0f;
        const Rectangle2D StudioRowR{ SheetR.X + 5.0f, Sy, SheetR.Width - 10.0f, 24.0f };
        if (StudioRowR.Contains(Cursor))
        {
            Surface.FillRectangle(StudioRowR, Theme.HoverSurface, 6.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { StudioRowR.X + 8.0f, StudioRowR.Y + 4.5f }, 9.4f, Theme.TextPrimary, "Open Brush Studio...");
        if (Pressed && StudioRowR.Contains(Cursor))
        {
            BrushStudioModalOpen = true;
            MoreToolsSheetOpen   = false;
        }

        if (Pressed && !SheetR.Contains(Cursor) && !MoreToolsBtnR.Contains(Cursor))
        {
            MoreToolsSheetOpen = false;
        }
    }

    //======================================================================================================================
    // 5. FLOATING BRUSH STUDIO MODAL (brush-menu-paint.png & brush-menu-text.png)
    //======================================================================================================================
    if (BrushStudioModalOpen)
    {
        Surface.FillRectangle(Bounds, ColorRgba{ 0u, 0u, 0u, 128u });

        const float ModalW = std::min(692.0f, Bounds.Width - 28.0f);
        const float ModalH = std::min(538.0f, Bounds.Height - 28.0f);
        const Rectangle2D ModalR{
            Bounds.X + (Bounds.Width - ModalW) * 0.5f,
            Bounds.Y + (Bounds.Height - ModalH) * 0.5f,
            ModalW,
            ModalH
        };

        Surface.FillRectangle(ModalR, Theme.PaneBackground, 16.0f);
        Surface.StrokeRectangle(ModalR, Theme.BorderStrong, 16.0f, 1.2f);

        const Rectangle2D HeadR{ ModalR.X, ModalR.Y, ModalR.Width, 54.0f };
        Surface.StrokeLine({ HeadR.X, HeadR.Bottom() }, { HeadR.Right(), HeadR.Bottom() }, Theme.BorderSubtle, 1.0f);

        const Rectangle2D HeadIconBox{ HeadR.X + 14.0f, HeadR.Y + 11.0f, 32.0f, 32.0f };
        Surface.FillRectangle(HeadIconBox, Theme.WellSurface, 8.0f);
        Surface.StrokeRectangle(HeadIconBox, Theme.BorderSubtle, 8.0f, 1.0f);

        ControlIcon StudioIcon  = ControlIcon::BrushTool;
        std::string StudioTitle = "Brush Studio";
        std::string StudioSub   = "BRUSH · RIGHT-CLICK ANY TOOL TO SWITCH";
        const auto& Presets     = TexturePaintSession::QueryBrushLibraryRegistry();
        const auto& ActivePreset = Presets[std::min<std::size_t>(Session->Brush.ActivePresetIndex, Presets.size() - 1u)];

        if (Session->InspectorTab == BrushInspectorTab::Text || Session->ActiveTool == PaintToolCategory::Text)
        {
            StudioIcon  = ControlIcon::TextType;
            StudioTitle = "Text Studio";
            StudioSub   = "STAMP LETTERING · RIGHT-CLICK ANY TOOL TO SWITCH";
        }
        else if (Session->InspectorTab == BrushInspectorTab::Erase || Session->ActiveTool == PaintToolCategory::Eraser)
        {
            StudioIcon  = ControlIcon::EraserTool;
            StudioTitle = "Eraser Studio";
            StudioSub   = "ERASE CHANNELS · RIGHT-CLICK ANY TOOL TO SWITCH";
        }
        else if (Session->InspectorTab == BrushInspectorTab::Fill || Session->ActiveTool == PaintToolCategory::Fill)
        {
            StudioIcon  = ControlIcon::FillBucket;
            StudioTitle = "Fill Studio";
            StudioSub   = "FLOOD FILL · RIGHT-CLICK ANY TOOL TO SWITCH";
        }
        else if (Session->InspectorTab == BrushInspectorTab::Gradient || Session->ActiveTool == PaintToolCategory::Gradient3D)
        {
            StudioIcon  = ControlIcon::PointGradient;
            StudioTitle = "Gradient Studio";
            StudioSub   = "3D POINT GRADIENT · RIGHT-CLICK ANY TOOL TO SWITCH";
        }
        else
        {
            StudioIcon  = ControlIcon::BrushTool;
            StudioTitle = std::string("Brush Studio · ") + ActivePreset.Label;
            StudioSub   = "BRUSH · HOG BRISTLE FLAT · RIGHT-CLICK ANY TOOL TO SWITCH";
        }

        VectorIconRepository::ConstructControl(
            Surface, StudioIcon, VectorIconPlacement{ HeadIconBox.X + 8.0f, HeadIconBox.Y + 8.0f, 16.0f, Theme.TextPrimary, 1.75f });
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Bold, { HeadIconBox.Right() + 10.0f, HeadR.Y + 11.0f }, HeadR.Width - 280.0f, 13.0f, Theme.TextPrimary, StudioTitle);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { HeadIconBox.Right() + 10.0f, HeadR.Y + 30.0f }, HeadR.Width - 280.0f, 8.0f, Theme.TextFaint, StudioSub);

        float HBtnX = HeadR.Right() - 12.0f - 28.0f;
        const float HBtnY = HeadR.Y + 13.0f;
        const Rectangle2D CloseBtnR{ HBtnX, HBtnY, 28.0f, 28.0f };
        if (ConstructHeaderIconButton(Surface, CloseBtnR, ControlIcon::CloseCross, false, true, Theme, Cursor, Pressed))
        {
            BrushStudioModalOpen = false;
        }
        HBtnX -= 32.0f;
        (void)ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 28.0f, 28.0f }, ControlIcon::DownloadExport, false, true, Theme, Cursor, Pressed);
        HBtnX -= 32.0f;
        (void)ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 28.0f, 28.0f }, ControlIcon::UploadImport, false, true, Theme, Cursor, Pressed);
        HBtnX -= 32.0f;
        if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 28.0f, 28.0f }, ControlIcon::RefreshCycle, false, true, Theme, Cursor, Pressed))
        {
            Session->SelectBrushPreset(6u);
        }
        HBtnX -= 32.0f;
        (void)ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 28.0f, 28.0f }, ControlIcon::CopyDuplicate, false, true, Theme, Cursor, Pressed);
        HBtnX -= 32.0f;
        if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 28.0f, 28.0f }, ControlIcon::RedoArrow, false, Session->QueryRedoAvailable(), Theme, Cursor, Pressed))
        {
            Session->ApplyRedo();
        }
        HBtnX -= 32.0f;
        if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 28.0f, 28.0f }, ControlIcon::UndoArrow, false, Session->QueryUndoAvailable(), Theme, Cursor, Pressed))
        {
            Session->ApplyUndo();
        }

        const float NavW = 182.0f;
        const Rectangle2D NavColR{ ModalR.X, HeadR.Bottom() + 1.0f, NavW, ModalR.Height - HeadR.Height - 1.0f };
        const Rectangle2D RightPaneR{ NavColR.Right() + 1.0f, NavColR.Y, ModalR.Width - NavW - 1.0f, NavColR.Height };
        Surface.StrokeLine({ NavColR.Right(), NavColR.Y }, { NavColR.Right(), NavColR.Bottom() }, Theme.BorderSubtle, 1.0f);

        const bool IsTextStudio = (Session->InspectorTab == BrushInspectorTab::Text || Session->ActiveTool == PaintToolCategory::Text);
        const std::span<const BrushStudioSectionSpec> NavItems = IsTextStudio
            ? std::span<const BrushStudioSectionSpec>(TextSectionsCatalog)
            : std::span<const BrushStudioSectionSpec>(BrushSectionsCatalog);

        float NavY = NavColR.Y + 8.0f;
        for (const auto& Sec : NavItems)
        {
            const Rectangle2D SecRowR{ NavColR.X + 8.0f, NavY, NavColR.Width - 16.0f, 31.0f };
            const bool ActiveSec = (Session->InspectorSubTab == Sec.SubTab);
            const bool HoverSec  = SecRowR.Contains(Cursor);

            if (ActiveSec || HoverSec)
            {
                Surface.FillRectangle(SecRowR, ActiveSec ? StudioRowSelected : StudioRowHover, 7.0f);
            }
            if (ActiveSec)
            {
                Surface.FillRectangle({ SecRowR.X, SecRowR.Y + 6.0f, 3.0f, SecRowR.Height - 12.0f }, ColorRgba{ 255u, 255u, 255u, 255u }, 1.5f);
            }
            VectorIconRepository::ConstructControl(
                Surface,
                Sec.Icon,
                VectorIconPlacement{ SecRowR.X + 9.0f, SecRowR.Y + 8.0f, 14.5f, ActiveSec ? Theme.TextPrimary : Theme.TextMuted, 1.7f });
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                ActiveSec ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                { SecRowR.X + 30.0f, SecRowR.Y + 7.5f },
                SecRowR.Width - 54.0f,
                9.8f,
                ActiveSec ? Theme.TextPrimary : Theme.TextMuted,
                Sec.Label);

            if (Sec.CountBadge != nullptr && Sec.CountBadge[0] != '\0')
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { SecRowR.Right() - 22.0f, SecRowR.Y + 8.5f }, 8.6f, Theme.TextFaint, Sec.CountBadge);
            }
            if (Pressed && HoverSec)
            {
                Session->InspectorSubTab = Sec.SubTab;
                ModalScrollY             = 0.0f;
            }
            NavY += 33.0f;
        }

        Surface.PushClip(RightPaneR);
        float ContentY = RightPaneR.Y + 12.0f;
        const float CPadX = RightPaneR.X + 16.0f;
        const float CInnerW = RightPaneR.Width - 32.0f;

        if (Session->InspectorSubTab == BrushDetailSubTab::LibraryGrid)
        {
            if (IsTextStudio)
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Bold, { CPadX, ContentY }, 9.2f, Theme.TextFaint, "FONTS");
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { RightPaneR.Right() - 26.0f, ContentY }, 9.2f, Theme.TextFaint, "6");
                ContentY += 22.0f;

                const auto& Fonts = TexturePaintSession::QueryFontLabels();
                const float CardGap = 10.0f;
                const float CardW   = (CInnerW - CardGap) * 0.5f;
                const float CardH   = 82.0f;

                for (std::size_t F = 0u; F < Fonts.size(); ++F)
                {
                    const std::size_t Col = F % 2u;
                    const std::size_t Row = F / 2u;
                    const Rectangle2D CardR{
                        CPadX + static_cast<float>(Col) * (CardW + CardGap),
                        ContentY + static_cast<float>(Row) * (CardH + CardGap),
                        CardW,
                        CardH
                    };
                    const bool ActiveFont = (Session->Brush.TextFontIndex == static_cast<uint32_t>(F));
                    const bool HoverFont  = CardR.Contains(Cursor);

                    Surface.FillRectangle(CardR, ActiveFont ? StudioRowSelected : Theme.SectionSurface, 10.0f);
                    Surface.StrokeRectangle(
                        CardR,
                        ActiveFont ? ColorRgba{ 238u, 238u, 236u, 230u } : (HoverFont ? Theme.BorderStrong : Theme.BorderSubtle),
                        10.0f,
                        ActiveFont ? 1.5f : 1.0f);

                    const Rectangle2D WellR{ CardR.X + 6.0f, CardR.Y + 6.0f, CardR.Width - 12.0f, 48.0f };
                    Surface.FillRectangle(WellR, Theme.WellSurface, 7.0f);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Bold, { WellR.X + 12.0f, WellR.Y + 12.0f }, 19.0f, Theme.TextPrimary, "Aa");
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Medium, { WellR.X + 46.0f, WellR.Y + 17.0f }, 12.5f, Theme.TextMuted, "Frontier");

                    WorkspaceAppearance::ConstructWeightedText(
                        Surface,
                        ActiveFont ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                        { CardR.X + 10.0f, CardR.Bottom() - 21.0f },
                        9.8f,
                        Theme.TextPrimary,
                        Fonts[F]);

                    if (Pressed && HoverFont)
                    {
                        Session->SelectTextFont(static_cast<uint32_t>(F));
                    }
                }
            }
            else
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Bold, { CPadX, ContentY }, 9.0f, Theme.TextFaint, "BRUSH LIBRARY");
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { RightPaneR.Right() - 28.0f, ContentY }, 9.0f, Theme.TextFaint, "12");
                ContentY += 20.0f;

                const Rectangle2D SearchR{ CPadX, ContentY, CInnerW, 28.0f };
                Surface.FillRectangle(SearchR, Theme.WellSurface, 8.0f);
                Surface.StrokeRectangle(SearchR, Theme.BorderSubtle, 8.0f, 1.0f);
                VectorIconRepository::ConstructControl(
                    Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchR.X + 9.0f, SearchR.Y + 7.5f, 13.0f, Theme.TextFaint, 1.65f });
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular, { SearchR.X + 28.0f, SearchR.Y + 6.5f }, 9.6f, Theme.TextFaint, "Filter brushes...");
                ContentY += 36.0f;

                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::SemiBold, { CPadX, ContentY }, 9.4f, Theme.TextMuted, "Brushes");
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { RightPaneR.Right() - 28.0f, ContentY }, 9.0f, Theme.TextFaint, "12");
                ContentY += 20.0f;

                const float CardGap = 8.0f;
                const float CardW   = (CInnerW - CardGap) * 0.5f;
                const float CardH   = 62.0f;

                for (std::size_t P = 0u; P < Presets.size(); ++P)
                {
                    const std::size_t Col = P % 2u;
                    const std::size_t Row = P / 2u;
                    const Rectangle2D CardR{
                        CPadX + static_cast<float>(Col) * (CardW + CardGap),
                        ContentY + static_cast<float>(Row) * (CardH + CardGap),
                        CardW,
                        CardH
                    };
                    if (CardR.Bottom() > RightPaneR.Bottom() + 20.0f)
                    {
                        continue;
                    }
                    const bool ActiveCard = (Session->Brush.ActivePresetIndex == static_cast<uint32_t>(P));
                    const bool HoverCard  = CardR.Contains(Cursor);

                    Surface.FillRectangle(CardR, ActiveCard ? StudioRowSelected : Theme.SectionSurface, 9.0f);
                    Surface.StrokeRectangle(
                        CardR,
                        ActiveCard ? ColorRgba{ 240u, 240u, 238u, 240u } : (HoverCard ? Theme.BorderStrong : Theme.BorderSubtle),
                        9.0f,
                        ActiveCard ? 1.5f : 1.0f);

                    const Rectangle2D PreviewWellR{ CardR.X + 5.0f, CardR.Y + 5.0f, CardR.Width - 10.0f, 36.0f };
                    Surface.FillRectangle(PreviewWellR, Theme.WellSurface, 6.0f);
                    ConstructBrushStrokePreviewWave(Surface, PreviewWellR, Presets[P], ActiveCard, Theme);

                    WorkspaceAppearance::ConstructTruncatedText(
                        Surface,
                        ActiveCard ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                        { CardR.X + 9.0f, CardR.Bottom() - 18.0f },
                        CardR.Width - 18.0f,
                        9.2f,
                        ActiveCard ? Theme.TextPrimary : Theme.TextMuted,
                        Presets[P].Label);

                    if (Pressed && HoverCard)
                    {
                        Session->SelectBrushPreset(static_cast<uint32_t>(P));
                    }
                }
            }
        }
        else if (Session->InspectorSubTab == BrushDetailSubTab::Curves)
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 10.5f, Theme.TextPrimary, "Stroke Dynamics & Response Curves");
            ContentY += 24.0f;

            constexpr std::array<const char*, 4u> CurveTabs {{ "Tip Falloff", "Size", "Opacity", "Hue Shift" }};
            uint32_t CurIdx = static_cast<uint32_t>(Session->Brush.ActiveCurve);
            if (ConstructSegmentedCapsuleRow(Surface, { CPadX, ContentY, CInnerW, 28.0f }, CurveTabs.data(), 4u, CurIdx, Theme, Cursor, Pressed))
            {
                Session->Brush.ActiveCurve = static_cast<BrushResponseCurve>(CurIdx);
            }
            ContentY += 38.0f;

            const Rectangle2D GraphR{ CPadX, ContentY, CInnerW, 148.0f };
            Surface.FillRectangle(GraphR, Theme.WellSurface, 10.0f);
            Surface.StrokeRectangle(GraphR, Theme.BorderSubtle, 10.0f, 1.0f);

            const auto& Pts = Session->Brush.Curves[static_cast<std::size_t>(Session->Brush.ActiveCurve)];
            for (std::size_t K = 0u; K + 1u < Pts.size(); ++K)
            {
                const Point2D P0{ GraphR.X + 14.0f + Pts[K].X * (GraphR.Width - 28.0f), GraphR.Bottom() - 14.0f - Pts[K].Y * (GraphR.Height - 28.0f) };
                const Point2D P1{ GraphR.X + 14.0f + Pts[K + 1u].X * (GraphR.Width - 28.0f), GraphR.Bottom() - 14.0f - Pts[K + 1u].Y * (GraphR.Height - 28.0f) };
                Surface.StrokeLine(P0, P1, ColorRgba{ 238u, 238u, 236u, 255u }, 2.0f);
                Surface.FillCircle(P0, 4.5f, ColorRgba{ 255u, 255u, 255u, 255u });
                if (K + 2u == Pts.size())
                {
                    Surface.FillCircle(P1, 4.5f, ColorRgba{ 255u, 255u, 255u, 255u });
                }
            }
            ContentY += 162.0f;

            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Size Jitter", Session->Brush.SizeJitter, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 50.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Opacity Jitter", Session->Brush.OpacityJitter, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
        }
        else
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Bold, { CPadX, ContentY }, 10.5f, Theme.TextPrimary, "Brush Parameters");
            ContentY += 24.0f;

            float RadiusFloat = static_cast<float>(Session->Brush.PixelRadius);
            if (ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Radius", RadiusFloat, 2.0f, 128.0f, "%.0f px", 1.0f, Input, Theme))
            {
                Session->Brush.PixelRadius = static_cast<int32_t>(std::lround(RadiusFloat));
            }
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Opacity", Session->Brush.Opacity, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Flow", Session->Brush.DeliveryRate, 0.02f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Hardness", Session->Brush.Hardness, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Spacing", Session->Brush.Spacing, 0.02f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Stabilization", Session->Brush.Stabilizer, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Roughness", Session->Brush.Roughness, 0.02f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
            ContentY += 48.0f;
            (void)ConstructSliderReadoutRow(Surface, { CPadX, ContentY, CInnerW, 44.0f }, "Metalness", Session->Brush.Metalness, 0.0f, 1.0f, "%.0f%%", 100.0f, Input, Theme);
        }

        Surface.PopClip();

        if (Pressed && !ModalR.Contains(Cursor))
        {
            BrushStudioModalOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
