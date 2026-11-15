/*==========================================================================================================================================
                                                     MATERIALVIEWPORTHOST.CPP
==========================================================================================================================================*/
// 🧩 Implementation of MaterialViewportHost: Frontier TexturingAppearance toolbar + Direct OpenPBR 3D viewport + orbit/impact interaction

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialViewportHost.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

namespace
{

[[nodiscard]] bool PointInsideRect(Rectangle2D Rect, Point2D Cursor) noexcept
{
    return Cursor.X >= Rect.X && Cursor.X <= Rect.Right() && Cursor.Y >= Rect.Y && Cursor.Y <= Rect.Bottom();
}

[[nodiscard]] const char* QuerySpecimenCaption(PreviewSpecimenGeometry Specimen) noexcept
{
    switch (Specimen)
    {
        case PreviewSpecimenGeometry::ShaderballPedestal: return "Shaderball";
        case PreviewSpecimenGeometry::DrapedClothSheet:    return "Cloth";
        case PreviewSpecimenGeometry::RoundedStudioBox:    return "Rounded Box";
        case PreviewSpecimenGeometry::TurntablePlane:      return "Ground Plane";
        default:                                           return "Shaderball";
    }
}

[[nodiscard]] const char* QueryRenderModeCaption(PreviewRenderMode Mode) noexcept
{
    switch (Mode)
    {
        case PreviewRenderMode::DirectStudioOpenPbr:     return "Direct OpenPBR (No GI)";
        case PreviewRenderMode::RealTimeGiSequence:      return "Real-Time GI";
        case PreviewRenderMode::DeterministicPathTracer: return "Path Tracer";
        default:                                         return "Direct OpenPBR (No GI)";
    }
}

} // namespace

MaterialViewportHost::~MaterialViewportHost() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
    PreviewTexture.DestroyPixels();
}

void MaterialViewportHost::AttachSession(MaterialWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
    InvalidatePreviewCache();
}

void MaterialViewportHost::AttachRenderer(MaterialPreviewRenderer* TargetRenderer) noexcept
{
    PreviewRenderer = TargetRenderer;
    InvalidatePreviewCache();
}

void MaterialViewportHost::AssignBottomOcclusionHeight(float OcclusionLogicalPx) noexcept
{
    BottomOcclusionPx = std::max(0.0f, OcclusionLogicalPx);
}

void MaterialViewportHost::InvalidatePreviewCache() noexcept
{
    LastSignature   = 0u;
    LastRenderClock = -1.0;
}

bool MaterialViewportHost::QueryAnyDropdownOpen() const noexcept
{
    return PresetDropdownOpen || SpecimenDropdownOpen || LightingDropdownOpen;
}

void MaterialViewportHost::CloseDropdowns() noexcept
{
    PresetDropdownOpen   = false;
    SpecimenDropdownOpen = false;
    LightingDropdownOpen = false;
}

void MaterialViewportHost::UploadPreviewTexture(
    const std::vector<uint8_t>& RgbaPixels,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaPixels.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return;
    }

    const bool NeedsCreate =
        PreviewTexture.Width != static_cast<int>(WidthPx) ||
        PreviewTexture.Height != static_cast<int>(HeightPx) ||
        PreviewTexture.Pixels == nullptr;

    if (NeedsCreate)
    {
        PreviewTexture.DestroyPixels();
        PreviewTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }

    std::memcpy(PreviewTexture.Pixels, RgbaPixels.data(), static_cast<std::size_t>(WidthPx) * HeightPx * 4u);
    if (!NeedsCreate && PreviewTexture.Status == ImTextureStatus_OK)
    {
        PreviewTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(WidthPx), static_cast<unsigned short>(HeightPx) };
        PreviewTexture.SetStatus(ImTextureStatus_WantUpdates);
    }

    CachedWidthPx  = WidthPx;
    CachedHeightPx = HeightPx;
}

void MaterialViewportHost::PresentImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##MaterialViewportCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const Rectangle2D ScreenBounds  { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds { 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };
    LastScreenBounds = ScreenBounds;

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, OrbitDragging);
}

void MaterialViewportHost::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    if (Session == nullptr || Bounds.Width < 32.0f || Bounds.Height < 32.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;
    const bool                 Overlay = QueryAnyDropdownOpen();
    ControlPointer             Ptr     = Input.PrimaryPointer;

    // 1. Clean monochrome studio backdrop matching Pattern & Baking Workspaces
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);

    // 2. Top Viewport Toolbar (exact match to 09-BakingWorkspaceMain.png & 05-PatternWorkspaceMain.png)
    constexpr float ToolbarH = 44.0f;
    const Rectangle2D ToolbarRect { Bounds.X, Bounds.Y, Bounds.Width, ToolbarH };
    Surface.FillRectangle(ToolbarRect, Theme.SectionSurface, 0.0f);
    Surface.StrokeLine({ ToolbarRect.X, ToolbarRect.Bottom() }, { ToolbarRect.Right(), ToolbarRect.Bottom() }, Theme.BorderSubtle, 1.0f);

    const float BtnH = 26.0f;
    const float BtnY = ToolbarRect.Y + (ToolbarH - BtnH) * 0.5f;
    float CurX = ToolbarRect.X + 12.0f;

    auto& ActiveRecipe = Session->AccessActiveRecipe();

    // Dropdown 1: Active Dynamic Material Preset
    const std::string PresetCaption = ActiveRecipe.DisplayTitle;
    const float PresetW = std::clamp(
        WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, PresetCaption) + 46.0f,
        164.0f,
        262.0f);
    if (ConstructDropdownBarRow(
            Surface,
            CurX,
            BtnY,
            PresetW,
            {},
            PresetCaption.c_str(),
            PresetDropdownOpen,
            Theme,
            Ptr,
            PresetAnchor,
            ControlIcon::MaterialSphere,
            OptionalTooltip::FromTitled("Dynamic Material", "Switch modular dynamic material preset or pick from ContentBrowser.")))
    {
        PresetDropdownOpen   = !PresetDropdownOpen;
        SpecimenDropdownOpen = false;
        LightingDropdownOpen = false;
    }
    CurX += PresetW + 8.0f;

    // Dropdown 2: Preview Specimen Geometry
    const char* SpecimenLabel = QuerySpecimenCaption(Session->ActiveSpecimen);
    const float SpecimenW = std::clamp(
        WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, SpecimenLabel) + 46.0f,
        112.0f,
        154.0f);
    if (ConstructDropdownBarRow(
            Surface,
            CurX,
            BtnY,
            SpecimenW,
            {},
            SpecimenLabel,
            SpecimenDropdownOpen,
            Theme,
            Ptr,
            SpecimenAnchor,
            ControlIcon::CubeShape,
            OptionalTooltip::FromTitled("Preview Specimen", "Inspect asset-agnostic material on Shaderball, Cloth, Rounded Box, or Plane.")))
    {
        SpecimenDropdownOpen = !SpecimenDropdownOpen;
        PresetDropdownOpen   = false;
        LightingDropdownOpen = false;
    }
    CurX += SpecimenW + 8.0f;

    // Dropdown 3: Lighting Mode (GI-Free Direct OpenPBR default + Real-Time GI / Path Tracer switches)
    const char* ModeLabel = QueryRenderModeCaption(Session->ActiveRenderMode);
    const float ModeW = std::clamp(
        WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, ModeLabel) + 46.0f,
        170.0f,
        220.0f);
    if (ConstructDropdownBarRow(
            Surface,
            CurX,
            BtnY,
            ModeW,
            {},
            ModeLabel,
            LightingDropdownOpen,
            Theme,
            Ptr,
            LightingAnchor,
            ControlIcon::SunIllumination,
            OptionalTooltip::FromTitled("Viewport Lighting", "Direct OpenPBR (No GI) runs full 10-lobe shading without GI for instant preview.")))
    {
        LightingDropdownOpen = !LightingDropdownOpen;
        PresetDropdownOpen   = false;
        SpecimenDropdownOpen = false;
    }
    CurX += ModeW + 10.0f;

    // Right-side primary buttons (Pause/Play cream primary pill + Impact Pulse secondary pill)
    const float PlayW = 108.0f;
    const Rectangle2D PlayRect { ToolbarRect.Right() - 12.0f - PlayW, BtnY, PlayW, BtnH };
    if (ConstructCapsuleButtonControl(
            Surface,
            PlayRect,
            Session->PlaybackRunning ? "Pause Clock" : "Play Clock",
            Theme,
            Cursor,
            !Overlay && Pressed,
            false,
            true,
            ControlIcon::MotionActivity,
            10.0f,
            13.0f,
            OptionalTooltip::FromTitled("Simulation Clock", "Toggle real-time playback of procedural and transition drivers.")))
    {
        Session->PlaybackRunning = !Session->PlaybackRunning;
    }

    const float ImpactW = 114.0f;
    const Rectangle2D ImpactRect { PlayRect.X - 8.0f - ImpactW, BtnY, ImpactW, BtnH };
    if (ImpactRect.X > CurX + 12.0f)
    {
        if (ConstructCapsuleButtonControl(
                Surface,
                ImpactRect,
                "Impact Pulse",
                Theme,
                Cursor,
                !Overlay && Pressed,
                false,
                false,
                ControlIcon::SparklesAntiAliasing,
                10.0f,
                13.0f,
                OptionalTooltip::FromTitled("Trigger Impact Pulse", "Fire a live 3D geodesic shockwave pulse with zero shader recompile.")))
        {
            Session->TriggerViewportImpactPulse({ 0.28f, 0.24f, 0.92f });
        }
    }

    // Hardware Backend Status Badge (matching 09-BakingWorkspaceMain.png green-dot badge)
    const bool UsingVulkan = (PreviewRenderer != nullptr && PreviewRenderer->QueryVulkanReady()) || Session->LastRenderUsedGpu;
    const char* BackendLabel = UsingVulkan ? "Vulkan 1.3 Compute · Direct OpenPBR" : "CPU OpenPBR 1.1 Reference";
    const float BadgeW = std::min(
        248.0f,
        WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, BackendLabel) + 34.0f);
    if (CurX + BadgeW + 10.0f < ImpactRect.X)
    {
        const Rectangle2D BadgeRect { CurX, BtnY, BadgeW, BtnH };
        Surface.FillRectangle(BadgeRect, Theme.WellSurface, 13.0f);
        Surface.StrokeRectangle(BadgeRect, Theme.BorderSubtle, 13.0f, 1.0f);
        const ColorRgba DotCol = UsingVulkan ? Theme.ReadyGreen : Theme.TextMuted;
        Surface.FillCircle({ BadgeRect.X + 12.0f, BadgeRect.Y + BadgeRect.Height * 0.5f }, 3.5f, DotCol);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { BadgeRect.X + 22.0f, BadgeRect.Y + 6.5f },
            BadgeRect.Width - 30.0f,
            9.8f,
            Theme.TextMuted,
            BackendLabel);
    }

    // 3. Visible 3D Viewport Canvas (automatically lifts above open ContentBrowser bottom shelf!)
    const float CanvasTop    = ToolbarRect.Bottom();
    const float EffectiveOcc = std::clamp(BottomOcclusionPx, 0.0f, std::max(0.0f, Bounds.Height - ToolbarH - 180.0f));
    const float CanvasHeight = std::max(140.0f, Bounds.Bottom() - CanvasTop - EffectiveOcc);
    const Rectangle2D CanvasRect { Bounds.X, CanvasTop, Bounds.Width, CanvasHeight };
    LastCanvasBounds = CanvasRect;

    // 4. Interactive Orbit / Zoom / Click-to-Impact
    if (!Overlay && PointInsideRect(CanvasRect, Cursor))
    {
        if (Pressed)
        {
            OrbitDragging  = true;
            DragAnchorX    = Cursor.X;
            DragAnchorY    = Cursor.Y;
            DragStartYaw   = Session->Camera.OrbitYawRadians;
            DragStartPitch = Session->Camera.OrbitPitchRadians;

            // Surface click triggers a live 3D geodesic impact ripple
            const float NormX = ((Cursor.X - CanvasRect.X) / std::max(1.0f, CanvasRect.Width) - 0.5f) * 2.0f;
            const float NormY = (0.5f - (Cursor.Y - CanvasRect.Y) / std::max(1.0f, CanvasRect.Height)) * 2.0f;
            if (NormX * NormX + NormY * NormY < 0.72f)
            {
                const float Z = std::sqrt(std::max(0.05f, 1.0f - NormX * NormX - NormY * NormY));
                Session->TriggerViewportImpactPulse({ NormX, NormY, Z });
            }
        }
        if (std::fabs(Input.WheelDeltaY) > 0.01f)
        {
            Session->Camera.OrbitDistanceM = std::clamp(
                Session->Camera.OrbitDistanceM * std::pow(0.90f, Input.WheelDeltaY),
                1.65f,
                4.80f);
            Session->DirtyPreviewRequested = true;
        }
        if (ImGui::GetCurrentContext() != nullptr && ImGui::IsKeyPressed(ImGuiKey_Space, false))
        {
            Session->PlaybackRunning = !Session->PlaybackRunning;
        }
    }

    if (OrbitDragging)
    {
        if (Input.PrimaryPointer.Down)
        {
            const float Dx = Cursor.X - DragAnchorX;
            const float Dy = Cursor.Y - DragAnchorY;
            Session->Camera.OrbitYawRadians   = DragStartYaw + Dx * 0.008f;
            Session->Camera.OrbitPitchRadians = std::clamp(DragStartPitch + Dy * 0.006f, -1.15f, 1.15f);
            Session->DirtyPreviewRequested = true;
        }
        else
        {
            OrbitDragging = false;
        }
    }

    if (Session->PlaybackRunning && DeltaSeconds > 0.0)
    {
        Session->AdvanceSimulationClock(DeltaSeconds);
    }

    // 5. Render exact aspect-matched Direct OpenPBR preview buffer (zero horizontal stretch even in ultra-wide SplitHalf!)
    const float ActualAspect = std::max(0.25f, CanvasRect.Width / std::max(1.0f, CanvasRect.Height));
    uint32_t TargetH = std::clamp(static_cast<uint32_t>(std::round(CanvasRect.Height * 0.90f)), 180u, 520u);
    uint32_t TargetW = static_cast<uint32_t>(std::round(static_cast<float>(TargetH) * ActualAspect));
    if (TargetW > 1280u)
    {
        TargetW = 1280u;
        TargetH = std::max(140u, static_cast<uint32_t>(std::round(static_cast<float>(TargetW) / ActualAspect)));
    }

    const uint64_t CurSig = ActiveRecipe.LayerStack.ComputeStructuralSignatureHash();
    const bool NeedsRender =
        Session->DirtyPreviewRequested ||
        CachedWidthPx != TargetW ||
        CachedHeightPx != TargetH ||
        LastSignature != CurSig ||
        std::fabs(LastRenderClock - Session->PlaybackClockSeconds) > 1.0e-4 ||
        std::fabs(LastYawRad - Session->Camera.OrbitYawRadians) > 1.0e-4f ||
        std::fabs(LastPitchRad - Session->Camera.OrbitPitchRadians) > 1.0e-4f ||
        std::fabs(LastDistMeters - Session->Camera.OrbitDistanceM) > 1.0e-4f ||
        LastSpecimenCode != static_cast<uint8_t>(Session->ActiveSpecimen) ||
        LastModeCode != static_cast<uint8_t>(Session->ActiveRenderMode);

    if (NeedsRender)
    {
        MaterialSpace::Dynamic::SynchronizeRecipeParameters(ActiveRecipe);
        ActiveRecipe.Parameters.ClockSeconds = static_cast<float>(Session->PlaybackClockSeconds);

        PreviewRenderResult Result {};
        if (PreviewRenderer != nullptr)
        {
            Result = PreviewRenderer->RenderPreview(
                ActiveRecipe,
                ActiveRecipe.Parameters,
                Session->ActiveSpecimen,
                Session->ActiveRenderMode,
                Session->Camera,
                TargetW,
                TargetH);
        }
        else
        {
            Result = MaterialPreviewRenderer::RenderPreviewCpuReference(
                ActiveRecipe,
                ActiveRecipe.Parameters,
                Session->ActiveSpecimen,
                Session->Camera,
                TargetW,
                TargetH);
        }

        if (!Result.DisplayRgba8.empty() && Result.WidthPx > 0u && Result.HeightPx > 0u)
        {
            UploadPreviewTexture(Result.DisplayRgba8, Result.WidthPx, Result.HeightPx);
            Session->LastRenderWidthPx     = Result.WidthPx;
            Session->LastRenderHeightPx    = Result.HeightPx;
            Session->LastRenderMillisec    = Result.RenderMillisec;
            Session->LastActiveLobeMask    = Result.ActiveLobeMask;
            Session->LastRenderUsedGpu     = Result.UsedVulkanCompute;
            Session->DirtyPreviewRequested = false;
            LastSignature                  = CurSig;
            LastRenderClock                = Session->PlaybackClockSeconds;
            LastYawRad                     = Session->Camera.OrbitYawRadians;
            LastPitchRad                   = Session->Camera.OrbitPitchRadians;
            LastDistMeters                 = Session->Camera.OrbitDistanceM;
            LastSpecimenCode               = static_cast<uint8_t>(Session->ActiveSpecimen);
            LastModeCode                   = static_cast<uint8_t>(Session->ActiveRenderMode);
        }
    }

    // 6. Blit crisp studio render filling the visible viewport canvas with zero aspect distortion
    if (PreviewTexture.Pixels != nullptr && CachedWidthPx > 0u && CachedHeightPx > 0u)
    {
        Surface.FillTexturedRectangle(CanvasRect, &PreviewTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f, { 255u, 255u, 255u, 255u });
    }

    // 7. Subtle top-left material status chip (matching 09-BakingWorkspaceMain.png's top-left viewport pill)
    {
        char ModeChip[128] {};
        std::snprintf(
            ModeChip,
            sizeof(ModeChip),
            "%s  ·  %u×%u  ·  %.1fms",
            ActiveRecipe.CategoryLabel.c_str(),
            Session->LastRenderWidthPx,
            Session->LastRenderHeightPx,
            Session->LastRenderMillisec);
        const float ChipW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, ModeChip) + 22.0f;
        const Rectangle2D ChipRect { CanvasRect.X + 14.0f, CanvasRect.Y + 12.0f, ChipW, 22.0f };
        Surface.FillRectangle(ChipRect, { 16u, 17u, 20u, 212u }, 11.0f);
        Surface.StrokeRectangle(ChipRect, Theme.BorderSubtle, 11.0f, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { ChipRect.X + 11.0f, ChipRect.Y + 5.0f },
            ChipRect.Width - 18.0f,
            9.5f,
            Theme.TextMuted,
            ModeChip);
    }

    // 8. Clean bottom-left navigation hint pill (zero overlap with centered ContentBrowser trapezoidal notch!)
    if (CanvasRect.Height >= 220.0f)
    {
        const char* Hint = "LMB Drag Orbit  ·  Click Surface for Impact Pulse  ·  Scroll Zoom  ·  Space Play/Pause";
        const float MaxHintW = std::max(180.0f, CanvasRect.Width * 0.44f);
        const float HintW = std::min(
            MaxHintW,
            WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, Hint) + 24.0f);
        const Rectangle2D HintRect { CanvasRect.X + 14.0f, CanvasRect.Bottom() - 32.0f, HintW, 22.0f };
        Surface.FillRectangle(HintRect, { 16u, 17u, 20u, 218u }, 11.0f);
        Surface.StrokeRectangle(HintRect, Theme.BorderSubtle, 11.0f, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { HintRect.X + 12.0f, HintRect.Y + 5.0f },
            HintRect.Width - 20.0f,
            9.5f,
            Theme.TextMuted,
            Hint);
    }

    // 9. Dropdown overlays
    if (PresetDropdownOpen)
    {
        std::vector<const char*> Labels {};
        Labels.reserve(Session->PresetRepository.size());
        uint32_t ActiveIdx = 0u;
        for (std::size_t Idx = 0u; Idx < Session->PresetRepository.size(); ++Idx)
        {
            Labels.push_back(Session->PresetRepository[Idx].DisplayTitle.c_str());
            if (Session->PresetRepository[Idx].RecipeId == ActiveRecipe.RecipeId)
            {
                ActiveIdx = static_cast<uint32_t>(Idx);
            }
        }
        RenderFittedDropdownSheet(
            Surface,
            PresetAnchor,
            Bounds,
            Labels.data(),
            static_cast<uint32_t>(Labels.size()),
            ActiveIdx,
            PresetDropdownOpen,
            Input,
            [this](uint32_t Picked)
            {
                if (Picked < Session->PresetRepository.size())
                {
                    Session->SelectPresetIdentity(Session->PresetRepository[Picked].PresetIdentity);
                    InvalidatePreviewCache();
                }
            },
            &SheetScrollY);
    }

    if (SpecimenDropdownOpen)
    {
        static constexpr const char* SpecimenLabels[4] { "Shaderball", "Cloth", "Rounded Box", "Ground Plane" };
        RenderFittedDropdownSheet(
            Surface,
            SpecimenAnchor,
            Bounds,
            SpecimenLabels,
            4u,
            static_cast<uint32_t>(Session->ActiveSpecimen),
            SpecimenDropdownOpen,
            Input,
            [this](uint32_t Picked)
            {
                if (Picked < 4u)
                {
                    Session->ActiveSpecimen = static_cast<PreviewSpecimenGeometry>(Picked);
                    Session->DirtyPreviewRequested = true;
                }
            },
            &SheetScrollY);
    }

    if (LightingDropdownOpen)
    {
        static constexpr const char* ModeLabels[3] { "Direct OpenPBR (No GI)", "Real-Time GI", "Path Tracer" };
        RenderFittedDropdownSheet(
            Surface,
            LightingAnchor,
            Bounds,
            ModeLabels,
            3u,
            static_cast<uint32_t>(Session->ActiveRenderMode),
            LightingDropdownOpen,
            Input,
            [this](uint32_t Picked)
            {
                if (Picked < 3u)
                {
                    Session->ActiveRenderMode = static_cast<PreviewRenderMode>(Picked);
                    Session->DirtyPreviewRequested = true;
                }
            },
            &SheetScrollY);
    }
}

} // namespace Frontier::Workspaces
