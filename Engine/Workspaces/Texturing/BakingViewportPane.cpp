/*============================================================================================================================================
                                                            BAKINGVIEWPORTPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of BakingViewportPane: live Vulkan 3D PBR + High-wire + Cage view, GPU 2D maps & SDF sections, Quick Bake Menu.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Texturing/BakingViewportPane.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

namespace
{

[[nodiscard]] bool PointInsideRect(Rectangle2D Rect, Point2D Cursor) noexcept
{
    return Cursor.X >= Rect.X && Cursor.X <= Rect.Right() && Cursor.Y >= Rect.Y && Cursor.Y <= Rect.Bottom();
}

[[nodiscard]] const char* QueryLayoutLabel(BakerViewportLayout Layout) noexcept
{
    switch (Layout)
    {
        case BakerViewportLayout::ThreeDOnly: return "3D Mesh Only";
        case BakerViewportLayout::TwoDOnly:   return "2D Map Only";
        case BakerViewportLayout::SplitView:  return "3D + 2D Split";
        default:                              return "3D Mesh Only";
    }
}

[[nodiscard]] const char* QueryAxisLabel(VolumeSectionAxis Axis) noexcept
{
    switch (Axis)
    {
        case VolumeSectionAxis::AxisZ: return "Z";
        case VolumeSectionAxis::AxisY: return "Y";
        case VolumeSectionAxis::AxisX: return "X";
        default:                       return "Z";
    }
}

[[nodiscard]] std::filesystem::path FindSampleMeshIfAvailable(bool HighPoly)
{
#ifdef FRONTIER_REPO_ROOT_DIR
    const std::filesystem::path Root(FRONTIER_REPO_ROOT_DIR);
#else
    const std::filesystem::path Root = std::filesystem::current_path();
#endif
    const std::array<const char*, 4u> Candidates = HighPoly
        ? std::array<const char*, 4u>{{
              "EngineContent/Meshes/Sample_High.obj",
              "EngineExports/Baking/Sample_High.obj",
              "EngineContent/Meshes/Sample_High.stl",
              "EngineExports/Baking/Sample_High.stl",
          }}
        : std::array<const char*, 4u>{{
              "EngineContent/Meshes/Sample_Low.obj",
              "EngineExports/Baking/Sample_Low.obj",
              "EngineContent/Meshes/Sample_Low.stl",
              "EngineExports/Baking/Sample_Low.stl",
          }};

    std::error_code Ec;
    for (const char* Rel : Candidates)
    {
        const std::filesystem::path P = Root / Rel;
        if (std::filesystem::exists(P, Ec))
        {
            return P;
        }
    }
    return {};
}

} // namespace

BakingViewportPane::~BakingViewportPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
    SpatialTexture.DestroyPixels();
    PlanarTexture.DestroyPixels();
}

void BakingViewportPane::AttachSession(BakingWorkspaceSession* TargetSession) noexcept
{
    Session            = TargetSession;
    StudyDropdownOpen  = false;
    LayoutDropdownOpen = false;
    QuickOverlayOpen   = false;
    OrbitDragActive    = false;
    PanDragActive      = false;
    SheetScrollY       = 0.0f;
    LastSpatialKey     = {};
}

void BakingViewportPane::InvalidateCachedFrames() noexcept
{
    ++LocalAssetRevision;
    LastSpatialKey = {};
}

void BakingViewportPane::UploadTexturePixels(
    ImTextureData& TargetTex,
    const uint8_t* RgbaPixels,
    uint32_t       WidthPx,
    uint32_t       HeightPx)
{
    if (RgbaPixels == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return;
    }
    const bool NeedsCreate =
        TargetTex.Width != static_cast<int>(WidthPx) ||
        TargetTex.Height != static_cast<int>(HeightPx) ||
        TargetTex.Pixels == nullptr;
    if (NeedsCreate)
    {
        TargetTex.DestroyPixels();
        TargetTex.SetStatus(ImTextureStatus_Destroyed);
        TargetTex.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }
    std::memcpy(TargetTex.Pixels, RgbaPixels, static_cast<std::size_t>(WidthPx) * HeightPx * 4u);

    if (!NeedsCreate && TargetTex.Status == ImTextureStatus_OK)
    {
        TargetTex.UpdateRect = { 0u, 0u, static_cast<unsigned short>(WidthPx), static_cast<unsigned short>(HeightPx) };
        TargetTex.SetStatus(ImTextureStatus_WantUpdates);
    }
}

void BakingViewportPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##BakingViewportCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, OrbitDragActive || PanDragActive);
}

void BakingViewportPane::ConfigureQuickOverlayOpen(bool Open) noexcept
{
    QuickOverlayOpen = Open;
    if (Open)
    {
        StudyDropdownOpen  = false;
        LayoutDropdownOpen = false;
        if (QuickOverlayAnchor.X <= 1.0f && QuickOverlayAnchor.Y <= 1.0f && LastCanvasBounds.Width > 20.0f)
        {
            QuickOverlayAnchor = {
                LastCanvasBounds.X + LastCanvasBounds.Width * 0.5f,
                LastCanvasBounds.Y + LastCanvasBounds.Height * 0.42f,
            };
        }
    }
}

bool BakingViewportPane::QueryQuickOverlayOpen() const noexcept
{
    return QuickOverlayOpen;
}

bool BakingViewportPane::QueryAnyOverlayOpen() const noexcept
{
    return StudyDropdownOpen || LayoutDropdownOpen || QuickOverlayOpen;
}

void BakingViewportPane::CloseAllOverlays() noexcept
{
    StudyDropdownOpen  = false;
    LayoutDropdownOpen = false;
    QuickOverlayOpen   = false;
}

void BakingViewportPane::RenderThreeDViewport(
    PixelSpace&             Surface,
    Rectangle2D             ViewportRect,
    const PaneInputReading& Input,
    bool                    OverlayUp)
{
    if (Session == nullptr || ViewportRect.Width < 32.0f || ViewportRect.Height < 32.0f)
    {
        return;
    }

    const TexturingThemeColors Theme  = QueryTexturingThemeColors();
    const Point2D              Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Inside = PointInsideRect(ViewportRect, Cursor);

    // Interactive orbit / pan / zoom / reset camera controls
    if (!OverlayUp && Inside)
    {
        if (Input.PrimaryPointer.Pressed)
        {
            const bool ShiftHeld = (ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().KeyShift;
            OrbitDragActive = !ShiftHeld;
            PanDragActive   = ShiftHeld;
            LastPointerPos  = Cursor;
        }
        else if (Input.MiddlePressed)
        {
            PanDragActive  = true;
            OrbitDragActive = false;
            LastPointerPos = Cursor;
        }

        if (std::abs(Input.WheelDeltaY) > 0.01f)
        {
            Session->ZoomCameraByFactor(std::pow(0.89f, Input.WheelDeltaY));
        }
        if (ImGui::GetCurrentContext() != nullptr && ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            Session->ResetOrbitCamera();
        }
    }

    if (OrbitDragActive && Input.PrimaryPointer.Down)
    {
        const float Dx = Cursor.X - LastPointerPos.X;
        const float Dy = Cursor.Y - LastPointerPos.Y;
        Session->OrbitCameraByDelta(-Dx * 0.0085f, Dy * 0.0075f);
        LastPointerPos = Cursor;
    }
    else if (PanDragActive && (Input.PrimaryPointer.Down || Input.MiddleDown))
    {
        const float Dx = Cursor.X - LastPointerPos.X;
        const float Dy = Cursor.Y - LastPointerPos.Y;
        const float Scale = Session->CameraDistanceMeters * 0.0022f;
        Session->PanCameraByDelta(-Dx * Scale, Dy * Scale);
        LastPointerPos = Cursor;
    }
    else
    {
        OrbitDragActive = false;
        PanDragActive   = false;
    }

    const uint32_t RenderW = std::clamp(static_cast<uint32_t>(std::round(ViewportRect.Width)), 128u, 1280u);
    const uint32_t RenderH = std::clamp(static_cast<uint32_t>(std::round(ViewportRect.Height)), 128u, 960u);

    const SpatialRenderCacheKey DesiredKey {
        RenderW,
        RenderH,
        Session->Study,
        Session->InspectedTarget,
        Session->SurfaceBaked,
        Session->ShowHighWireOverlay,
        Session->ShowCageOverlay,
        Session->FrontCageOffsetMeters,
        Session->CameraYawRadians,
        Session->CameraPitchRadians,
        Session->CameraDistanceMeters,
        Session->CameraPivotX,
        Session->CameraPivotY,
        Session->CameraPivotZ,
        LocalAssetRevision,
    };

    if (!(DesiredKey == LastSpatialKey) || CachedSpatialRgba.empty())
    {
        if (Session->RenderSpatialViewportRgba8(RenderW, RenderH, CachedSpatialRgba))
        {
            LastSpatialKey = DesiredKey;
        }
    }

    if (!CachedSpatialRgba.empty())
    {
        UploadTexturePixels(SpatialTexture, CachedSpatialRgba.data(), DesiredKey.WidthPx, DesiredKey.HeightPx);
        Surface.FillTexturedRectangle(ViewportRect, &SpatialTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
    }

    // Viewport Header Tag + Quick High-Wire / Cage Toggles
    const Rectangle2D Tag3D{ ViewportRect.X + 14.0f, ViewportRect.Y + 14.0f, 184.0f, 22.0f };
    Surface.FillRectangle(Tag3D, ColorRgba{ 22u, 24u, 28u, 222u }, 11.0f);
    Surface.StrokeRectangle(Tag3D, Theme.BorderSubtle, 11.0f, 1.0f);
    Surface.TextCentered(Tag3D, 9.0f, Theme.TextPrimary, "3D LOW-POLY + BAKED MAP");

    // Inline toggle pills in top-right of 3D view
    if (ViewportRect.Width > 380.0f)
    {
        const bool Pressed = !OverlayUp && Input.PrimaryPointer.Pressed;
        const Rectangle2D CageBtn{ ViewportRect.Right() - 78.0f, ViewportRect.Y + 14.0f, 64.0f, 22.0f };
        if (ConstructCapsuleButtonControl(
                Surface,
                CageBtn,
                "Cage",
                Theme,
                Cursor,
                Pressed,
                Session->ShowCageOverlay,
                false,
                ControlIcon::Count,
                9.0f,
                11.0f,
                OptionalTooltip::FromTitled("Translucent Cage", "Preview ray projection cage envelope in 3D.")))
        {
            Session->ShowCageOverlay = !Session->ShowCageOverlay;
        }

        const Rectangle2D WireBtn{ CageBtn.X - 88.0f, ViewportRect.Y + 14.0f, 82.0f, 22.0f };
        if (ConstructCapsuleButtonControl(
                Surface,
                WireBtn,
                "High Wire",
                Theme,
                Cursor,
                Pressed,
                Session->ShowHighWireOverlay,
                false,
                ControlIcon::Count,
                9.0f,
                11.0f,
                OptionalTooltip::FromTitled("High-Poly Wireframe", "Overlay source high-resolution silhouette wireframe.")))
        {
            Session->ShowHighWireOverlay = !Session->ShowHighWireOverlay;
        }
    }
}

void BakingViewportPane::RenderInspectedTwoDPreview(PixelSpace& Surface, Rectangle2D PreviewArea)
{
    if (Session == nullptr)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingThemeColors();

    const float Side = std::max(80.0f, std::min(PreviewArea.Width, PreviewArea.Height) - 36.0f);
    const Rectangle2D CanvasBox{
        PreviewArea.X + (PreviewArea.Width - Side) * 0.5f,
        PreviewArea.Y + (PreviewArea.Height - Side) * 0.5f,
        Side,
        Side,
    };

    Surface.FillRectangle(CanvasBox, ColorRgba{ 19u, 20u, 24u, 255u }, 14.0f);
    Surface.StrokeRectangle(CanvasBox, Theme.BorderStrong, 14.0f, 1.0f);

    const BakerTargetDescriptor& TargetDesc = BakingWorkspaceSession::QueryTargetDescriptor(Session->InspectedTarget);
    const uint8_t*               PlanarPtr  = nullptr;
    uint32_t                     PlanarW    = 0u;
    uint32_t                     PlanarH    = 0u;

    const bool IsVolume = (Session->InspectedTarget == BakerTargetId::VolumeSdf);
    const bool HasGpuPixels =
        ((IsVolume && Session->VolumeSdfBaked) || (!IsVolume && Session->QueryTargetReady(Session->InspectedTarget))) &&
        Session->QueryInspectedPlanarRgba8(PlanarPtr, PlanarW, PlanarH);

    if (HasGpuPixels)
    {
        const float Pad = 14.0f;
        const Rectangle2D InnerTex{ CanvasBox.X + Pad, CanvasBox.Y + Pad, CanvasBox.Width - Pad * 2.0f, CanvasBox.Height - Pad * 2.0f };
        UploadTexturePixels(PlanarTexture, PlanarPtr, PlanarW, PlanarH);
        Surface.FillTexturedRectangle(InnerTex, &PlanarTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 8.0f);
        Surface.StrokeRectangle(InnerTex, Theme.BorderSubtle, 8.0f, 1.0f);

        char ReadyText[96];
        if (IsVolume)
        {
            std::snprintf(
                ReadyText,
                sizeof(ReadyText),
                "%u³ SDF · %s %.2f",
                Session->VolumeGridDim,
                QueryAxisLabel(Session->SectionAxis),
                Session->SectionDepthNormalized);
        }
        else
        {
            std::snprintf(ReadyText, sizeof(ReadyText), "%s (%ux%u)", TargetDesc.Label, PlanarW, PlanarH);
        }
        const Rectangle2D CapBadge{ CanvasBox.X + 20.0f, CanvasBox.Bottom() - 34.0f, CanvasBox.Width - 40.0f, 22.0f };
        Surface.FillRectangle(CapBadge, ColorRgba{ 18u, 20u, 25u, 210u }, 11.0f);
        Surface.TextCentered(CapBadge, 10.5f, Theme.TextPrimary, ReadyText);
    }
    else if (IsVolume)
    {
        Surface.TextCentered(CanvasBox, 12.0f, Theme.TextMuted, "Click 'Bake Selected Maps' to compute GPU 3D Volume SDF");
    }
    else
    {
        Surface.TextCentered(CanvasBox, 12.0f, Theme.TextMuted, "Select channels & click 'Bake Selected Maps'");
    }
}

void BakingViewportPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Session == nullptr || Bounds.Width < 120.0f || Bounds.Height < 120.0f)
    {
        return;
    }

    const TexturingThemeColors Theme     = QueryTexturingThemeColors();
    const Point2D              Cursor    { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed   = Input.PrimaryPointer.Pressed;
    const bool                 OverlayUp = QueryAnyOverlayOpen();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);

    // Top toolbar
    constexpr float   ToolbarH = 46.0f;
    const Rectangle2D ToolbarBounds{ Bounds.X, Bounds.Y, Bounds.Width, ToolbarH };
    Surface.FillRectangle(ToolbarBounds, Theme.PaneBackground, 0.0f);
    Surface.StrokeLine({ ToolbarBounds.X, ToolbarBounds.Bottom() }, { ToolbarBounds.Right(), ToolbarBounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    float                CurX = ToolbarBounds.X + 12.0f;
    const float          BtnY = ToolbarBounds.Y + 8.0f;
    const float          BtnH = 30.0f;
    const ControlPointer Ptr  = OverlayUp ? ControlPointer{} : Input.PrimaryPointer;

    // 1. Study Selector
    const BakerStudyDescriptor& ActiveStudy = Session->QueryActiveStudyDescriptor();
    const std::string StudyCaption = !Session->CustomLowAssetLabel.empty()
        ? (Session->CustomLowAssetLabel + " Study")
        : std::string(ActiveStudy.Label);
    const float StudyW = std::min(220.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, StudyCaption) + 46.0f);
    if (ConstructDropdownBarRow(
            Surface,
            CurX,
            BtnY,
            StudyW,
            {},
            StudyCaption.c_str(),
            StudyDropdownOpen,
            Theme,
            Ptr,
            LastStudyAnchor,
            ControlIcon::Count,
            OptionalTooltip::FromTitled("Study Asset", "Switch built-in High/Low geometry study pair.")))
    {
        StudyDropdownOpen  = !StudyDropdownOpen;
        LayoutDropdownOpen = false;
        QuickOverlayOpen   = false;
    }
    CurX += StudyW + 8.0f;

    // 2. View Layout Selector
    const char* LayoutLabel = QueryLayoutLabel(Session->ViewportLayout);
    const float LayoutW     = std::min(148.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, LayoutLabel) + 42.0f);
    if (ConstructDropdownBarRow(
            Surface,
            CurX,
            BtnY,
            LayoutW,
            {},
            LayoutLabel,
            LayoutDropdownOpen,
            Theme,
            Ptr,
            LastLayoutAnchor,
            ControlIcon::Count,
            OptionalTooltip::FromTitled("Bake Viewport View", "Switch 3D Mesh Only, 2D Map Only, or 3D + 2D Split.")))
    {
        LayoutDropdownOpen = !LayoutDropdownOpen;
        StudyDropdownOpen  = false;
        QuickOverlayOpen   = false;
    }
    CurX += LayoutW + 8.0f;

    // 3. Low Mesh / High Mesh import buttons (.OBJ / .STL asset intake or OS drop)
    const float LowBtnW = 92.0f;
    if (CurX + LowBtnW < ToolbarBounds.Right() - 320.0f)
    {
        if (ConstructCapsuleButtonControl(
                Surface,
                { CurX, BtnY, LowBtnW, BtnH },
                "Low Mesh",
                Theme,
                Cursor,
                !OverlayUp && Pressed,
                !Session->CustomLowAssetLabel.empty(),
                false,
                ControlIcon::UploadImport,
                10.0f,
                15.0f,
                OptionalTooltip::FromTitled("Import Low Mesh", "Drop any .OBJ or .STL onto the window to replace Low target.")))
        {
            const std::filesystem::path Candidate = FindSampleMeshIfAvailable(false);
            if (!Candidate.empty())
            {
                std::string Diag;
                if (Session->LoadCustomStudyAsset(Candidate, false, &Diag))
                {
                    InvalidateCachedFrames();
                }
            }
            else
            {
                Session->ProgressStatus = "Drop .OBJ or .STL onto window to load Low mesh";
            }
        }
        CurX += LowBtnW + 8.0f;
    }

    const float HighBtnW = 94.0f;
    if (CurX + HighBtnW < ToolbarBounds.Right() - 280.0f)
    {
        if (ConstructCapsuleButtonControl(
                Surface,
                { CurX, BtnY, HighBtnW, BtnH },
                "High Mesh",
                Theme,
                Cursor,
                !OverlayUp && Pressed,
                !Session->CustomHighAssetLabel.empty(),
                false,
                ControlIcon::UploadImport,
                10.0f,
                15.0f,
                OptionalTooltip::FromTitled("Import High Mesh", "Drop any .OBJ or .STL onto the window to replace High source.")))
        {
            const std::filesystem::path Candidate = FindSampleMeshIfAvailable(true);
            if (!Candidate.empty())
            {
                std::string Diag;
                if (Session->LoadCustomStudyAsset(Candidate, true, &Diag))
                {
                    InvalidateCachedFrames();
                }
            }
            else
            {
                Session->ProgressStatus = "Drop .OBJ or .STL onto window to load High mesh";
            }
        }
        CurX += HighBtnW + 8.0f;
    }

    // Right-side primary buttons (Bake Selected Maps + Send to Paint)
    const float       BakeW = 148.0f;
    const Rectangle2D BakeRect{ ToolbarBounds.Right() - 12.0f - BakeW, BtnY, BakeW, BtnH };
    if (ConstructCapsuleButtonControl(
            Surface,
            BakeRect,
            "Bake Selected Maps",
            Theme,
            Cursor,
            !OverlayUp && Pressed,
            false,
            true,
            ControlIcon::BakeBox,
            10.0f,
            15.0f,
            OptionalTooltip::FromTitled("Bake Selected Maps", "Execute Vulkan GPU high-to-low surface and volume SDF bake.")))
    {
        Session->ExecuteBakeSimulation();
        InvalidateCachedFrames();
    }

    const float       SendW = 114.0f;
    const Rectangle2D SendRect{ BakeRect.X - 8.0f - SendW, BtnY, SendW, BtnH };
    if (SendRect.X > CurX + 10.0f)
    {
        if (ConstructCapsuleButtonControl(
                Surface,
                SendRect,
                "Send to Paint",
                Theme,
                Cursor,
                !OverlayUp && Pressed,
                Session->BridgedToPaint,
                false,
                ControlIcon::BrushTool,
                10.0f,
                15.0f,
                OptionalTooltip::FromTitled("Send to Paint", "Transfer baked AO, Curvature and Normals into Texture Paint.")))
        {
            Session->SendBakedTargetsToPaint();
        }
    }

    // GPU Ready indicator chip
    const float GpuChipW = std::min(
        std::max(0.0f, SendRect.X - CurX - 8.0f),
        WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, Session->DeviceIndicatorText) + 28.0f);
    if (GpuChipW >= 80.0f)
    {
        const Rectangle2D GpuRect{ CurX, BtnY + 3.0f, GpuChipW, 24.0f };
        Surface.FillRectangle(GpuRect, Theme.WellSurface, 12.0f);
        Surface.StrokeRectangle(GpuRect, Theme.BorderSubtle, 12.0f, 1.0f);
        Surface.FillCircle({ GpuRect.X + 11.0f, GpuRect.Y + 12.0f }, 3.5f, Theme.ReadyGreen);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Medium,
            { GpuRect.X + 19.0f, GpuRect.Y + 6.0f },
            GpuRect.Width - 24.0f,
            9.5f,
            Theme.TextPrimary,
            Session->DeviceIndicatorText);
    }

    // Stage canvas below toolbar
    LastCanvasBounds = {
        Bounds.X,
        ToolbarBounds.Bottom(),
        Bounds.Width,
        std::max(1.0f, Bounds.Bottom() - ToolbarBounds.Bottom()),
    };
    Surface.PushClip(LastCanvasBounds);
    Surface.FillRectangle(LastCanvasBounds, ColorRgba{ 11u, 11u, 11u, 255u }, 0.0f);

    const BakerTargetDescriptor& InspectedDesc = BakingWorkspaceSession::QueryTargetDescriptor(Session->InspectedTarget);

    if (Session->ViewportLayout == BakerViewportLayout::ThreeDOnly)
    {
        RenderThreeDViewport(Surface, LastCanvasBounds, Input, OverlayUp);
    }
    else if (Session->ViewportLayout == BakerViewportLayout::TwoDOnly)
    {
        RenderInspectedTwoDPreview(Surface, LastCanvasBounds);

        char TagBuf[96];
        std::snprintf(TagBuf, sizeof(TagBuf), "INSPECTING: %s", InspectedDesc.Label);
        const float TagW = std::min(
            LastCanvasBounds.Width - 28.0f,
            WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 9.0f, TagBuf) + 22.0f);
        const Rectangle2D Tag2D{ LastCanvasBounds.X + 14.0f, LastCanvasBounds.Y + 14.0f, TagW, 22.0f };
        Surface.FillRectangle(Tag2D, ColorRgba{ 22u, 24u, 28u, 225u }, 11.0f);
        Surface.StrokeRectangle(Tag2D, Theme.BorderSubtle, 11.0f, 1.0f);
        Surface.TextCentered(Tag2D, 9.0f, Theme.TextPrimary, TagBuf);
    }
    else
    {
        const float       HalfW = LastCanvasBounds.Width * 0.5f;
        const Rectangle2D LeftPane { LastCanvasBounds.X, LastCanvasBounds.Y, HalfW, LastCanvasBounds.Height };
        const Rectangle2D RightPane{ LastCanvasBounds.X + HalfW, LastCanvasBounds.Y, LastCanvasBounds.Width - HalfW, LastCanvasBounds.Height };

        RenderThreeDViewport(Surface, LeftPane, Input, OverlayUp);
        Surface.StrokeLine({ LeftPane.Right(), LeftPane.Y }, { LeftPane.Right(), LeftPane.Bottom() }, Theme.BorderSubtle, 1.0f);

        RenderInspectedTwoDPreview(Surface, RightPane);

        char TagBuf[96];
        std::snprintf(TagBuf, sizeof(TagBuf), "INSPECTING: %s", InspectedDesc.Label);
        const float TagW = std::min(
            RightPane.Width - 28.0f,
            WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 9.0f, TagBuf) + 22.0f);
        const Rectangle2D Tag2D{ RightPane.X + 14.0f, RightPane.Y + 14.0f, TagW, 22.0f };
        Surface.FillRectangle(Tag2D, ColorRgba{ 22u, 24u, 28u, 225u }, 11.0f);
        Surface.StrokeRectangle(Tag2D, Theme.BorderSubtle, 11.0f, 1.0f);
        Surface.TextCentered(Tag2D, 9.0f, Theme.TextPrimary, TagBuf);
    }

    // Bottom HUD instruction chip
    const char* HudCaption = "LMB Drag Orbit · Shift/MMB Pan · Wheel Zoom · R Reset · Click Map to Inspect";
    const float HudW = std::min(
        LastCanvasBounds.Width - 28.0f,
        WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, HudCaption) + 28.0f);
    const Rectangle2D HudRect{ LastCanvasBounds.X + (LastCanvasBounds.Width - HudW) * 0.5f, LastCanvasBounds.Bottom() - 36.0f, HudW, 24.0f };
    Surface.FillRectangle(HudRect, ColorRgba{ 20u, 22u, 27u, 225u }, 12.0f);
    Surface.StrokeRectangle(HudRect, Theme.BorderSubtle, 12.0f, 1.0f);
    Surface.TextCentered(HudRect, 9.5f, Theme.TextMuted, HudCaption);

    // Quick overlay interactions (Right-click or Q inside canvas)
    if (PointInsideRect(LastCanvasBounds, Cursor))
    {
        if (Input.SecondaryPressed)
        {
            QuickOverlayAnchor = Cursor;
            ConfigureQuickOverlayOpen(true);
        }
        else if (ImGui::GetCurrentContext() != nullptr && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        {
            QuickOverlayAnchor = Cursor;
            ConfigureQuickOverlayOpen(!QuickOverlayOpen);
        }
    }
    if (QuickOverlayOpen && Input.EscapePressed)
    {
        CloseAllOverlays();
    }

    Surface.PopClip();

    // Render top-level overlays (dropdown sheets + Quick Bake Menu) inside this pane's surface
    RenderTopLevelOverlays(Surface, Bounds, Input);

    Surface.PopClip();
}

void BakingViewportPane::RenderTopLevelOverlays(
    PixelSpace&             Surface,
    Rectangle2D             WorkspaceBounds,
    const PaneInputReading& Input) noexcept
{
    if (Session == nullptr || !QueryAnyOverlayOpen())
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;

    if (StudyDropdownOpen)
    {
        const auto& Studies = BakingWorkspaceSession::QueryStudyRegistry();
        std::array<const char*, 5u> Labels{};
        for (std::size_t I = 0u; I < Studies.size(); ++I)
        {
            Labels[I] = Studies[I].Label;
        }
        const uint32_t ActiveIdx = static_cast<uint32_t>(Session->Study);
        RenderFittedDropdownSheet(
            Surface,
            LastStudyAnchor,
            WorkspaceBounds,
            Labels.data(),
            static_cast<uint32_t>(Labels.size()),
            ActiveIdx,
            StudyDropdownOpen,
            Input,
            [this, &Studies](uint32_t Picked)
            {
                if (Picked < Studies.size())
                {
                    Session->SelectStudy(Studies[Picked].Study);
                    InvalidateCachedFrames();
                }
            },
            &SheetScrollY);
    }

    if (LayoutDropdownOpen)
    {
        static constexpr const char* LayoutLabels[3] { "3D Mesh Only", "2D Map Only", "3D + 2D Split" };
        const uint32_t ActiveIdx = static_cast<uint32_t>(Session->ViewportLayout);
        RenderFittedDropdownSheet(
            Surface,
            LastLayoutAnchor,
            WorkspaceBounds,
            LayoutLabels,
            3u,
            ActiveIdx,
            LayoutDropdownOpen,
            Input,
            [this](uint32_t Picked)
            {
                if (Picked < 3u)
                {
                    Session->ViewportLayout = static_cast<BakerViewportLayout>(Picked);
                }
            },
            &SheetScrollY);
    }

    if (QuickOverlayOpen)
    {
        constexpr float BoxW = 318.0f;
        constexpr float BoxH = 114.0f;
        const float X = std::clamp(QuickOverlayAnchor.X - BoxW * 0.5f, WorkspaceBounds.X + 12.0f, WorkspaceBounds.Right() - BoxW - 12.0f);
        const float Y = std::clamp(QuickOverlayAnchor.Y - BoxH * 0.5f, WorkspaceBounds.Y + 12.0f, WorkspaceBounds.Bottom() - BoxH - 12.0f);
        const Rectangle2D BoxRect{ X, Y, BoxW, BoxH };

        Surface.FillRectangle(BoxRect, Theme.NestedSurface, 12.0f);
        Surface.StrokeRectangle(BoxRect, Theme.BorderStrong, 12.0f, 1.0f);

        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::SemiBold, { X + 14.0f, Y + 10.0f }, 160.0f, 10.5f, Theme.TextPrimary, "Baking Quick Menu");
        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { X + BoxW - 12.0f, Y + 11.0f }, 9.0f, Theme.TextMuted, "Right-Click / Q / Esc");
        Surface.StrokeLine({ X + 12.0f, Y + 30.0f }, { X + BoxW - 12.0f, Y + 30.0f }, Theme.BorderSubtle, 1.0f);

        const std::array<uint32_t, 4u>    ResChoices {{ 256u, 512u, 1024u, 2048u }};
        const std::array<const char*, 4u> ResLabels  {{ "256px", "512px", "1024px", "2048px" }};
        const float ChipW = (BoxW - 24.0f - 18.0f) * 0.25f;
        for (std::size_t I = 0u; I < ResChoices.size(); ++I)
        {
            const Rectangle2D ChipRect{ X + 12.0f + static_cast<float>(I) * (ChipW + 6.0f), Y + 38.0f, ChipW, 24.0f };
            if (ConstructCapsuleButtonControl(
                    Surface,
                    ChipRect,
                    ResLabels[I],
                    Theme,
                    Cursor,
                    Pressed,
                    Session->TargetResolutionPx == ResChoices[I],
                    false,
                    ControlIcon::Count,
                    9.5f,
                    12.0f))
            {
                Session->AssignResolution(ResChoices[I]);
            }
        }

        const Rectangle2D QuickBakeRect{ X + 12.0f, Y + 72.0f, 168.0f, 28.0f };
        if (ConstructCapsuleButtonControl(Surface, QuickBakeRect, "Bake Selected Maps", Theme, Cursor, Pressed, false, true, ControlIcon::BakeBox, 10.0f, 14.0f))
        {
            Session->ExecuteBakeSimulation();
            InvalidateCachedFrames();
            QuickOverlayOpen = false;
        }
        const Rectangle2D QuickSendRect{ QuickBakeRect.Right() + 8.0f, Y + 72.0f, BoxW - 24.0f - 176.0f, 28.0f };
        if (ConstructCapsuleButtonControl(Surface, QuickSendRect, "Send to Paint", Theme, Cursor, Pressed, Session->BridgedToPaint, false, ControlIcon::BrushTool, 10.0f, 14.0f))
        {
            Session->SendBakedTargetsToPaint();
            QuickOverlayOpen = false;
        }

        if (Pressed && !PointInsideRect(BoxRect, Cursor))
        {
            QuickOverlayOpen = false;
        }
    }
}

} // namespace Frontier::Workspaces
