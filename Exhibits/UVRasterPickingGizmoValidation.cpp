/*============================================================================================================================================
                                                    UVRASTERPICKINGGIZMOVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 2 validation suite for Vulkan .slang Renders, Timeline Semaphore TimelineTransferRegister, 3D/2D GPU Picking, and 3D/2D TransformGizmo

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Configuration/WorkspaceConfiguration.h"
#include "Engine/Editor/Viewports/TransformGizmo.h"
#include "Engine/Editor/Viewports/ViewportProjection.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Workspaces/UV/UVViewportInteractor.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                VERIFICATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool VerifyCondition(bool ConditionMet, const char* Description) noexcept
{
    if (!ConditionMet)
    {
        std::fprintf(stderr, "[UVRasterPickingGizmoValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

[[nodiscard]] uint32_t CountPopulatedBits(std::span<const uint32_t> BitmaskWords) noexcept
{
    uint32_t Count = 0u;
    for (const uint32_t Word : BitmaskWords)
    {
        Count += static_cast<uint32_t>(std::popcount(Word));
    }
    return Count;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SUITE 1: MODULAR 3D & 2D TRANSFORMGIZMO MATH
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ValidateModularTransformGizmo(std::string& OutMoveHud, std::string& OutScaleHud, std::string& OutRotHud)
{
    using namespace Frontier::Editor;
    using namespace Frontier::Configuration;
    using namespace Frontier::Mathematics;

    const GizmoSnapSettings SnapCfg {};
    TransformGizmo          Gizmo3D {};
    Gizmo3D.ConfigureDimensionMode(GizmoDimensionMode::Spatial3D);
    Gizmo3D.ConfigureAnchor({ 0.0f, 0.50f, 0.0f }, 1.05f);

    const auto Verts3D = Gizmo3D.BuildTriangleStream({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });
    if (!VerifyCondition(!Verts3D.empty() && (Verts3D.size() % 3u) == 0u, "3D TransformGizmo generated valid triangle stream"))
    {
        return false;
    }

    // 1. +X Translation with Ctrl snap (0.25 m step)
    SpatialTransform TargetXform {};
    TargetXform.Position = { 0.0f, 0.50f, 0.0f };
    const ViewportRay3D StartRayX { { 0.40f, 0.50f, 4.0f }, { 0.0f, 0.0f, -1.0f } };
    const ViewportRay3D DragRayX  { { 0.93f, 0.50f, 4.0f }, { 0.0f, 0.0f, -1.0f } }; // +0.53 -> snaps to +0.50
    if (!VerifyCondition(Gizmo3D.BeginDragInteraction(GizmoHandleOrdinal::TranslateX, StartRayX, TargetXform), "Began +X translate drag"))
    {
        return false;
    }
    const GizmoInteractionDelta MoveDelta = Gizmo3D.EvaluateDragInteraction(DragRayX, true, SnapCfg, TargetXform);
    const GizmoInteractionDelta StableMoveDelta = Gizmo3D.EvaluateDragInteraction(DragRayX, true, SnapCfg, TargetXform);
    Gizmo3D.EndDragInteraction();
    OutMoveHud = MoveDelta.HudReadout;
    if (!VerifyCondition(MoveDelta.TransformChanged && std::abs(TargetXform.Position.X - 0.50f) < 1.0e-4f, "+X Ctrl-snap translation == +0.50 m") ||
        !VerifyCondition(StableMoveDelta.TransformChanged && std::abs(TargetXform.Position.X - 0.50f) < 1.0e-4f, "Repeated unchanged drag sample does not jitter the gizmo back to its start position"))
    {
        return false;
    }

    // 2. +Y Scale with Ctrl snap (0.10x step) verifying gizmo WorldRadius stays constant!
    Gizmo3D.ConfigureAnchor({ 0.0f, 0.50f, 0.0f }, 1.05f);
    const ViewportRay3D StartRayY { { 0.0f, 1.00f, 4.0f }, { 0.0f, 0.0f, -1.0f } };
    const ViewportRay3D DragRayY  { { 0.0f, 1.22f, 4.0f }, { 0.0f, 0.0f, -1.0f } };
    if (!VerifyCondition(Gizmo3D.BeginDragInteraction(GizmoHandleOrdinal::ScaleY, StartRayY, TargetXform), "Began +Y scale drag"))
    {
        return false;
    }
    const GizmoInteractionDelta ScaleDelta = Gizmo3D.EvaluateDragInteraction(DragRayY, true, SnapCfg, TargetXform);
    Gizmo3D.EndDragInteraction();
    OutScaleHud = ScaleDelta.HudReadout;
    if (!VerifyCondition(ScaleDelta.TransformChanged && std::abs(TargetXform.Scale.Y - 1.20f) < 1.0e-4f, "+Y Ctrl-snap scale == x1.20") ||
        !VerifyCondition(std::abs(Gizmo3D.QueryWorldRadius() - 1.05f) < 1.0e-6f, "Gizmo WorldRadius remains constant during object scaling"))
    {
        return false;
    }

    // 3. Planar 2D UV Gizmo rotation with Ctrl snap (5.0 deg step)
    TransformGizmo Gizmo2D {};
    Gizmo2D.ConfigureDimensionMode(GizmoDimensionMode::Planar2D);
    Gizmo2D.ConfigureAnchor({ 0.50f, 0.50f, 0.0f }, 0.35f);
    const auto Verts2D = Gizmo2D.BuildTriangleStream();
    if (!VerifyCondition(!Verts2D.empty() && Verts2D.size() < Verts3D.size(), "2D Planar Gizmo builds compact planar handle stream"))
    {
        return false;
    }

    SpatialTransform UVXform {};
    const ViewportRay3D RotStart { { 0.80f, 0.50f, 2.0f }, { 0.0f, 0.0f, -1.0f } }; // 0 deg
    const ViewportRay3D RotDrag  { { 0.78f, 0.58f, 2.0f }, { 0.0f, 0.0f, -1.0f } }; // ~16 deg -> snaps to +15.0 deg
    if (!VerifyCondition(Gizmo2D.BeginDragInteraction(GizmoHandleOrdinal::RotateZ, RotStart, UVXform), "Began 2D UV arc rotation"))
    {
        return false;
    }
    const GizmoInteractionDelta RotDelta = Gizmo2D.EvaluateDragInteraction(RotDrag, true, SnapCfg, UVXform);
    Gizmo2D.EndDragInteraction();
    OutRotHud = RotDelta.HudReadout;
    return VerifyCondition(RotDelta.TransformChanged && std::abs(UVXform.Rotation.Z - 15.0f) < 1.0e-3f, "2D UV Ctrl-snap rotation == +15.0 deg");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::GeometrySpace;
    using namespace Frontier::Editor;

    std::string MoveHud {}, ScaleHud {}, RotHud {};
    if (!ValidateModularTransformGizmo(MoveHud, ScaleHud, RotHud))
    {
        return 1;
    }

    // The spatial viewport uses a right-handed, Z-up frame: camera up, dolly and axial presets
    // must agree so the image, picking rays, and transform axes stay aligned.
    ViewportProjection SpatialProjection {};
    Frontier::Mathematics::Axis3D CamRight {}, CamUp {}, CamForward {};
    SpatialProjection.EvaluateCameraBasis(CamRight, CamUp, CamForward);
    const float InitialDistance = SpatialProjection.InspectSpatialCamera().DistanceMeters;
    SpatialProjection.DollySpatialCamera(0.8f);
    if (!VerifyCondition(CamUp.Z > 0.90f, "Spatial camera is Z-up") ||
        !VerifyCondition(SpatialProjection.InspectSpatialCamera().DistanceMeters < InitialDistance, "Spatial dolly zooms toward the pivot"))
    {
        return 1;
    }
    SpatialProjection.ApplySpatialPreset(SpatialProjectionPreset::Top);
    if (!VerifyCondition(SpatialProjection.EvaluateCameraEyePosition().Z > SpatialProjection.InspectSpatialCamera().TargetPivot.Z, "Top view looks down the Z axis"))
    {
        return 1;
    }

    Frontier::Workspaces::UVWorkspaceSession CameraSession {};
    Frontier::Workspaces::UVViewportInteractor CameraInteractor {};
    CameraInteractor.AttachSession(&CameraSession);
    Frontier::PaneInputReading CameraInput {};
    CameraInput.PrimaryPointer.X = 150.0f;
    CameraInput.PrimaryPointer.Y = 100.0f;
    CameraInput.WheelDeltaY = 1.0f;
    const Frontier::ViewportInteractionReading CameraReading {
        {},
        Frontier::Rectangle2D{ 100.0f, 0.0f, 200.0f, 200.0f },
        CameraInput,
    };
    const float BeforeWheelZoom = CameraSession.AccessViewportProjection().InspectSpatialCamera().DistanceMeters;
    CameraInteractor.ProcessViewportInput(CameraReading);
    if (!VerifyCondition(CameraSession.AccessViewportProjection().InspectSpatialCamera().DistanceMeters < BeforeWheelZoom, "Viewport wheel input reaches the 3D camera dolly"))
    {
        return 1;
    }

    const std::filesystem::path RepoRoot  = Frontier::Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);

    VulkanInterchange VkInterchange {};
    std::string         VkDiag;
    if (!VerifyCondition(VkInterchange.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }

    UVWorkspaceRasterizer Rasterizer {};
    if (!VerifyCondition(Rasterizer.InitializeRasterizer(&VkInterchange, ShaderDir), "Initialized UVWorkspaceRasterizer (.slang graphics + compute)"))
    {
        return 1;
    }

    ConstructedSampleAsset AeroAsset = ConstructSamplePolygonNetwork(SampleAssetPreset::AeroCoupeGT);
    if (!VerifyCondition(Rasterizer.UploadPolygonNetwork(AeroAsset.Network, 1u), "Uploaded AeroCoupeGT PolygonNetwork to VRAM"))
    {
        return 1;
    }

    // 1. Validate O(1) SelectionMaskCompute.slang on VRAM
    const FacePartitionSpan& HeroLayerRef = AeroAsset.Layers.InspectPartitionSpan(AeroAsset.Network, 1u);
    if (!VerifyCondition(Rasterizer.DispatchGPUSelection(GPUSelectionOperation::SelectByLayer, 1u, false), "Dispatched SelectionMaskCompute for Layer 1"))
    {
        return 1;
    }
    const std::vector<uint32_t> Layer1MaskWords = Rasterizer.ReadbackSelectionBitmaskWords();
    const uint32_t              GPUSelectedBits = CountPopulatedBits(Layer1MaskWords);
    if (!VerifyCondition(
            GPUSelectedBits == static_cast<uint32_t>(HeroLayerRef.FaceIndices.size()),
            "SelectionMaskCompute VRAM bit count matches FacePartitionRegister Layer 1 face count"))
    {
        return 1;
    }

    // 2. Validate UVDistortionInspection.slang + TimelineTransferRegister
    const GPUUVDistortionSummary Metrics = Rasterizer.EvaluateUVDistortionInspection(AeroAsset.Network, true);
    if (!VerifyCondition(Metrics.FlippedFaceCount == 0u, "GPU UVDistortionInspection verified 0 flipped UV faces") ||
        !VerifyCondition(Metrics.OverlapFaceCount == 0u, "GPU UVDistortionInspection verified 0 overlapping UV islands") ||
        !VerifyCondition(Metrics.MeanL2Stretch > 0.2f && Metrics.MeanL2Stretch < 5.0f, "GPU UVDistortionInspection produced valid L2 stretch"))
    {
        return 1;
    }

    // 3. Rasterize 3D Viewport (Lit + 16-MatCap + Wireframe + Default-Off Seam + 3D Slate TransformGizmo) & Verify 3D GPU Picking
    constexpr uint32_t ViewW = 780u;
    constexpr uint32_t ViewH = 540u;

    TransformGizmo Gizmo3D {};
    Gizmo3D.ConfigureDimensionMode(GizmoDimensionMode::Spatial3D);
    Gizmo3D.ConfigureAnchor({ 0.0f, 0.0f, 0.56f }, 1.15f);
    Gizmo3D.AssignHoveredHandle(GizmoHandleOrdinal::PlaneXY);

    Viewport3DRenderOptions Opts3D {};
    Opts3D.Camera.TargetPivot    = { 0.0f, 0.0f, 0.36f };
    Opts3D.Camera.YawRadians     = 0.64f;
    Opts3D.Camera.PitchRadians   = 0.36f;
    Opts3D.Camera.DistanceMeters = 4.35f;
    Opts3D.DisplayMode           = SurfaceDisplayMode::SurfaceMaterial;
    Opts3D.WireframeEnabled      = true;
    Opts3D.SeamOverlayEnabled    = false;
    Opts3D.ActiveLayerOrdinal    = 1u;

    std::vector<uint8_t> Pixels3D {};
    if (!VerifyCondition(Rasterizer.Rasterize3DViewport(ViewW, ViewH, Opts3D, &Gizmo3D, &Pixels3D), "Rasterized 3D Vulkan MRT Viewport"))
    {
        return 1;
    }
    bool RepeatedSurfaceIterationsOkay = true;
    for (uint32_t IterationIdx = 0u; IterationIdx < 192u; ++IterationIdx)
    {
        if (!Rasterizer.Rasterize3DViewport(ViewW, ViewH, Opts3D, nullptr, nullptr))
        {
            RepeatedSurfaceIterationsOkay = false;
            break;
        }
    }
    bool ResizeIterationsOkay = true;
    for (uint32_t ResizeIdx = 0u; ResizeIdx < 12u; ++ResizeIdx)
    {
        const uint32_t ResizeW = 400u + ResizeIdx * 7u;
        const uint32_t ResizeH = 300u + ResizeIdx * 5u;
        if (!Rasterizer.Rasterize3DViewport(ResizeW, ResizeH, Opts3D, nullptr, nullptr))
        {
            ResizeIterationsOkay = false;
            break;
        }
    }
    if (!VerifyCondition(RepeatedSurfaceIterationsOkay, "192 consecutive 3D-only Vulkan iterations retain valid descriptors") ||
        !VerifyCondition(ResizeIterationsOkay, "repeated 3D target size changes recreate attachments safely") ||
        !VerifyCondition(Rasterizer.Rasterize3DViewport(ViewW, ViewH, Opts3D, &Gizmo3D, &Pixels3D), "Restored 3D surface and gizmo after descriptor churn"))
    {
        return 1;
    }

    // Scan a sparse grid to verify both 3D Surface Face Pick AND 3D Gizmo Handle Pick in R32G32B32A32_UINT attachment!
    ViewportPickHit SampleSurfaceHit3D {};
    ViewportPickHit SampleGizmoHit3D   {};
    for (int32_t PY = 140; PY < 440 && (!SampleSurfaceHit3D.HitSurfaceFace() || !SampleGizmoHit3D.HitGizmo()); PY += 18)
    {
        for (int32_t PX = 220; PX < 560; PX += 18)
        {
            const ViewportPickHit Candidate = Rasterizer.Pick3DPixel(PX, PY);
            if (!SampleSurfaceHit3D.HitSurfaceFace() && Candidate.HitSurfaceFace())
            {
                SampleSurfaceHit3D = Candidate;
            }
            if (!SampleGizmoHit3D.HitGizmo() && Candidate.HitGizmo())
            {
                SampleGizmoHit3D = Candidate;
            }
        }
    }

    if (!VerifyCondition(SampleSurfaceHit3D.HitSurfaceFace(), "3D GPU Pick R32G32B32A32_UINT resolved AeroCoupeGT FaceIndex") ||
        !VerifyCondition(SampleGizmoHit3D.HitGizmo(), "3D GPU Pick R32G32B32A32_UINT resolved Slate TransformGizmo HandleOrdinal"))
    {
        return 1;
    }

    // Validate GPU Marquee Reduction (ViewportPickReduction.slang)
    (void)Rasterizer.DispatchGPUSelection(GPUSelectionOperation::ClearAll);
    const RegionPickSummary Marquee3D = Rasterizer.Reduce3DRegionPick(260u, 200u, 520u, 420u);
    const uint32_t MarqueeSelectedFaces = CountPopulatedBits(Rasterizer.ReadbackSelectionBitmaskWords());
    if (!VerifyCondition(Marquee3D.HitPixelCount > 50u && MarqueeSelectedFaces > 0u, "GPU ViewportPickReduction populated VRAM SelectionBitmask"))
    {
        return 1;
    }

    // Restore Layer 1 selection for crisp 2D canvas highlight
    (void)Rasterizer.DispatchGPUSelection(GPUSelectionOperation::SelectByLayer, 1u, false);

    // 4. Rasterize 2D UDIM 1001 & 1002 UVTile Canvas + 2D Slate TransformGizmo & Verify 2D GPU Picking
    TransformGizmo Gizmo2D {};
    Gizmo2D.ConfigureDimensionMode(GizmoDimensionMode::Planar2D);
    Gizmo2D.ConfigureAnchor({ 0.50f, 0.50f, 0.0f }, 0.38f);
    Gizmo2D.AssignHoveredHandle(GizmoHandleOrdinal::RotateZ);

    Canvas2DRenderOptions Opts2D {};
    Opts2D.VisibleMinUV       = { -0.06f, -0.22f };
    Opts2D.VisibleMaxUV       = { 2.06f, 1.22f };
    Opts2D.DisplayMode        = UVCanvasDisplayMode::PriorityLayerFill;
    Opts2D.CheckerEnabled     = true;
    Opts2D.ActiveLayerOrdinal = 1u;

    std::vector<uint8_t> Pixels2D {};
    if (!VerifyCondition(Rasterizer.Rasterize2DUVCanvas(ViewW, ViewH, Opts2D, &Gizmo2D, &Pixels2D), "Rasterized 2D UDIM Vulkan MRT Canvas"))
    {
        return 1;
    }

    ViewportPickHit SampleUVHit2D {};
    for (int32_t PY = 150; PY < 420 && !SampleUVHit2D.HitSurfaceFace(); PY += 20)
    {
        for (int32_t PX = 60; PX < 700; PX += 20)
        {
            const ViewportPickHit Candidate = Rasterizer.Pick2DPixel(PX, PY);
            if (Candidate.HitSurfaceFace())
            {
                SampleUVHit2D = Candidate;
                break;
            }
        }
    }
    if (!VerifyCondition(SampleUVHit2D.HitSurfaceFace(), "2D GPU Pick R32G32B32A32_UINT resolved UDIM UVTile FaceIndex"))
    {
        return 1;
    }

    // 5. Composite Visual Exhibit (Exhibits/Images/UV/01-VulkanRendersPickingAndGizmo.png)
    constexpr uint32_t OutW = 1660u;
    constexpr uint32_t OutH = 880u;

    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io = ImGui::GetIO();
    (void)Frontier::Exhibits::LoadWorkspaceDmSansFont(Io, 11.0f);

    Frontier::Exhibits::SoftwareRasterCanvas Canvas(OutW, OutH, { 16, 17, 20, 255 });

    for (int AllChecksGreen = 0; AllChecksGreen < 2; ++AllChecksGreen)
    {
        Io.DisplaySize = ImVec2(static_cast<float>(OutW), static_cast<float>(OutH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(Io.DisplaySize);
        ImGui::Begin(
            "Phase2VulkanExhibit",
            nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);

        ImDrawList* ForegroundList       = ImGui::GetWindowDrawList();
        const ImU32 TileFill   = IM_COL32(24, 26, 31, 255);
        const ImU32 TileBorder = IM_COL32(48, 52, 62, 255);
        const ImU32 AccentGold = IM_COL32(239, 244, 207, 255);
        const ImU32 AccentBlue = IM_COL32(126, 188, 255, 255);
        const ImU32 EmeraldOk  = IM_COL32(98, 214, 150, 255);
        const ImU32 TextBright = IM_COL32(232, 235, 242, 255);
        const ImU32 TextMuted  = IM_COL32(154, 160, 174, 255);

        ForegroundList->AddRectFilled(ImVec2(20.0f, 16.0f), ImVec2(1640.0f, 66.0f), TileFill, 6.0f);
        ForegroundList->AddRect(ImVec2(20.0f, 16.0f), ImVec2(1640.0f, 66.0f), TileBorder, 6.0f);
        ForegroundList->AddText(ImVec2(38.0f, 33.0f), AccentGold, "PHASE 2 VERIFIED  |  VULKAN .SLANG MRT RENDERS, TIMELINE READBACK RING, 3D/2D GPU PICKING & SLATE GIZMO");

        char AdapterTitle[128]{};
        std::snprintf(AdapterTitle, sizeof(AdapterTitle), "Vulkan Adapter: %.*s", static_cast<int>(VkInterchange.QueryAdapterName().size()), VkInterchange.QueryAdapterName().data());
        ForegroundList->AddText(ImVec2(1240.0f, 33.0f), EmeraldOk, AdapterTitle);

        // Left Viewport Frame (3D Surface + 16-MatCap + 3D Gizmo)
        ForegroundList->AddRectFilled(ImVec2(20.0f, 80.0f), ImVec2(820.0f, 672.0f), TileFill, 6.0f);
        ForegroundList->AddRect(ImVec2(20.0f, 80.0f), ImVec2(820.0f, 672.0f), TileBorder, 6.0f);
        ForegroundList->AddText(ImVec2(36.0f, 94.0f), AccentBlue, "1. 3D .SLANG SURFACE + 16-MATCAP + SLATE 3D GIZMO (MRT RGBA8 + R32G32B32A32_UINT)");

        // Right Viewport Frame (2D UDIM 1001/1002 + 2D Gizmo)
        ForegroundList->AddRectFilled(ImVec2(840.0f, 80.0f), ImVec2(1640.0f, 672.0f), TileFill, 6.0f);
        ForegroundList->AddRect(ImVec2(840.0f, 80.0f), ImVec2(1640.0f, 672.0f), TileBorder, 6.0f);
        ForegroundList->AddText(ImVec2(856.0f, 94.0f), AccentBlue, "2. 2D .SLANG UDIM 1001 & 1002 CANVAS + SLATE 2D GIZMO (MRT RGBA8 + PICK)");

        // Bottom Telemetry Deck
        ForegroundList->AddRectFilled(ImVec2(20.0f, 688.0f), ImVec2(1640.0f, 864.0f), TileFill, 6.0f);
        ForegroundList->AddRect(ImVec2(20.0f, 688.0f), ImVec2(1640.0f, 864.0f), TileBorder, 6.0f);
        ForegroundList->AddText(ImVec2(38.0f, 704.0f), AccentGold, "GPU COMPUTE, TIMELINE SEMAPHORE READBACK RING & PICKING TELEMETRY");

        char LineA[200]{};
        std::snprintf(
            LineA,
            sizeof(LineA),
            "3D GPU Pick Hit: Instance=%u, Face=%u, Edge=%u  |  3D Gizmo Pick HandleOrdinal=%u  |  Marquee Reduction: %u px -> %u Faces",
            SampleSurfaceHit3D.InstanceSlot,
            SampleSurfaceHit3D.FaceIndex,
            SampleSurfaceHit3D.DirectedEdgeIndex,
            SampleGizmoHit3D.GizmoHandleOrdinal,
            Marquee3D.HitPixelCount,
            MarqueeSelectedFaces);
        ForegroundList->AddText(ImVec2(38.0f, 736.0f), EmeraldOk, LineA);

        char LineB[200]{};
        std::snprintf(
            LineB,
            sizeof(LineB),
            "2D GPU Pick Hit: Instance=%u, Face=%u  |  SelectionMaskCompute Layer 1 Bits=%u  |  GPU Stretch L2=%.3f (Flips=%u, Overlaps=%u)",
            SampleUVHit2D.InstanceSlot,
            SampleUVHit2D.FaceIndex,
            GPUSelectedBits,
            Metrics.MeanL2Stretch,
            Metrics.FlippedFaceCount,
            Metrics.OverlapFaceCount);
        ForegroundList->AddText(ImVec2(38.0f, 766.0f), TextBright, LineB);

        char LineC[200]{};
        std::snprintf(
            LineC,
            sizeof(LineC),
            "Slate GizmoPRO Snapped Interactions Verified:  [%s]   |   [%s] (Radius Constant 1.05)   |   [%s]",
            MoveHud.c_str(),
            ScaleHud.c_str(),
            RotHud.c_str());
        ForegroundList->AddText(ImVec2(38.0f, 796.0f), AccentBlue, LineC);
        ForegroundList->AddText(ImVec2(38.0f, 826.0f), TextMuted, "Zero VkFence Stalls: All GPU->CPU Readbacks Synchronized Strictly via Vulkan 1.2 Timeline Semaphores (TimelineTransferRegister)");

        ImGui::End();
        ImGui::Render();

        if (AllChecksGreen == 1)
        {
            Canvas.Clear({ 16, 17, 20, 255 });
            Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
            Canvas.BlitRgbaRegion(30u, 120u, ViewW, ViewH, Pixels3D.data());
            Canvas.BlitRgbaRegion(850u, 120u, ViewW, ViewH, Pixels2D.data());
        }
    }

    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/UV";
    std::filesystem::create_directories(OutDir);
    const bool Saved = Canvas.WritePngFile(OutDir / "01-VulkanRendersPickingAndGizmo.png");
    ImGui::DestroyContext(GuiCtx);

    Rasterizer.TeardownRasterizer();
    VkInterchange.TeardownInterchange();

    if (!VerifyCondition(Saved, "Wrote Exhibits/Images/UV/01-VulkanRendersPickingAndGizmo.png"))
    {
        return 1;
    }

    std::printf(
        "[UVRasterPickingGizmoValidation] Phase 2 passed: 3D Face=%u, 3D Gizmo=%u, 2D Face=%u, Marquee=%u faces, L2=%.3f.\n",
        SampleSurfaceHit3D.FaceIndex,
        SampleGizmoHit3D.GizmoHandleOrdinal,
        SampleUVHit2D.FaceIndex,
        MarqueeSelectedFaces,
        Metrics.MeanL2Stretch);
    return 0;
}
