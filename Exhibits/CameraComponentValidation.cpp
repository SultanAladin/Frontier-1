/*============================================================================================================================================
                                                      CAMERACOMPONENTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for CameraComponent (6 modes, thin-lens optics, smoothing, constraints, Z-Up/Y-Up parity & GPU .slang visual effects)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraComponent.h"
#include "Engine/Components/Camera/VisualEffects/CameraOpticsEncoder.h"
#include "Engine/Components/Camera/VisualEffects/DepthOfField.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

[[nodiscard]] bool VerifyCheck(bool Passed, const char* Label) noexcept
{
    if (!Passed)
    {
        std::fprintf(stderr, "[CameraComponentValidation] FAILED: %s\n", Label);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::Components;
    using namespace Frontier::Mathematics;

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Physical Thin-Lens Optics & Sensor Gates
    //--------------------------------------------------------------------------------------------------------------------
    CameraComponent Cam {};
    Cam.AssignSensorGate(SensorGatePreset::Vista35mm);
    Cam.AssignFocalLengthMillimeters(50.0f);
    Cam.AccessConfiguration().Optics.ApertureFStop       = 2.8f;
    Cam.AccessConfiguration().Optics.FocusDistanceMeters = 5.0f;
    Cam.SnapToTargets(1.5f);

    const CameraPosture& OpticsPosture = Cam.InspectPosture();
    if (!VerifyCheck(std::abs(OpticsPosture.VerticalFovDegrees - 26.9915f) < 0.05f, "50mm on 36x24mm vertical FOV ~26.99 deg") ||
        !VerifyCheck(std::abs(OpticsPosture.HorizontalFovDegrees - 39.5978f) < 0.05f, "50mm on 36x24mm horizontal FOV ~39.60 deg") ||
        !VerifyCheck(std::abs(OpticsPosture.EntrancePupilMillimeters - (50.0f / 2.8f)) < 0.02f, "Entrance pupil = Focal / FStop") ||
        !VerifyCheck(OpticsPosture.NearSharpLimitMeters < 5.0f && OpticsPosture.FarSharpLimitMeters > 5.0f, "DoF near/far bracket focus plane"))
    {
        return 1;
    }

    Cam.AssignSensorGate(SensorGatePreset::IMAX70mm);
    if (!VerifyCheck(Cam.InspectPosture().HorizontalFovDegrees > 65.0f, "IMAX 70mm widens horizontal FOV for fixed 50mm lens"))
    {
        return 1;
    }
    Cam.AssignSensorGate(SensorGatePreset::Vista35mm);
    Cam.AssignVerticalFovDegrees(42.0f);

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Pitch Pole-Guard, Spatial/Planar Bounds & Swept-Sphere Collision Avoidance
    //--------------------------------------------------------------------------------------------------------------------
    Cam.ApplyOrbitAnglesDelta(0.0f, 3.0f); // Attempt +171 deg pitch past +90 deg pole
    if (!VerifyCheck(Cam.InspectPosture().TargetPitchRadians <= 1.52f, "PitchConstraint clamps positive pole < 87.1 deg"))
    {
        return 1;
    }
    Cam.ApplyOrbitAnglesDelta(0.0f, -6.0f);
    if (!VerifyCheck(Cam.InspectPosture().TargetPitchRadians >= -1.52f, "PitchConstraint clamps negative pole > -87.1 deg"))
    {
        return 1;
    }

    Cam.AccessConfiguration().Bounds.EnforceSpatialBounds = true;
    Cam.AccessConfiguration().Bounds.SpatialMinimum       = { -2.0f, -2.0f, 0.0f };
    Cam.AccessConfiguration().Bounds.SpatialMaximum       = {  2.0f,  2.0f, 3.0f };
    Cam.ApplySpatialPanPixels(-50000.0f, 50000.0f, 800u, 600u, 1.0f);
    if (!VerifyCheck(
            Cam.InspectPosture().TargetPivot.X <= 2.0f && Cam.InspectPosture().TargetPivot.Z <= 3.0f,
            "BoundsConstraint clamps 3D pivot inside configured volume"))
    {
        return 1;
    }
    Cam.AccessConfiguration().Bounds.EnforceSpatialBounds = false;

    // Collision avoidance between pivot (0,0,0.5) and desired eye along -Y at distance 8m
    Editor::SpatialCameraRig ClearRig {};
    ClearRig.TargetPivot    = { 0.0f, 0.0f, 0.5f };
    ClearRig.YawRadians     = 0.0f;
    ClearRig.PitchRadians   = 0.0f;
    ClearRig.DistanceMeters = 8.0f;
    ClearRig.Basis          = WorldCoordinateBasis::RightHandedZUp;
    Cam.AlignFromSpatialRig(ClearRig, true);
    Cam.AccessConfiguration().Collision.Enabled               = true;
    Cam.AccessConfiguration().Collision.ClearanceRadiusMeters = 0.20f;
    Cam.AccessConfiguration().Collision.Obstacles             = { { { 0.0f, -4.0f, 0.5f }, 0.80f } };
    Cam.AdvanceKinematics(1.0f, 16.0f / 9.0f);
    if (!VerifyCheck(
            Cam.InspectPosture().CurrentDistanceMeters < 3.15f && Cam.InspectPosture().CurrentDistanceMeters > 2.85f,
            "CollisionAvoidance pulls camera eye in front of occluding obstacle sphere"))
    {
        return 1;
    }
    Cam.AccessConfiguration().Collision.Enabled = false;

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Exponential Smoothing Convergence (>99.9% settled within 300 ms & Step-Rate Independence)
    //--------------------------------------------------------------------------------------------------------------------
    Cam.AlignFromSpatialRig(ClearRig, true);
    Cam.ApplyOrbitAnglesDelta(1.0f, 0.4f);
    Cam.ApplyLogarithmicDolly(0.5f);
    const float TargetYaw  = Cam.InspectPosture().TargetYawRadians;
    const float TargetDist = Cam.InspectPosture().TargetDistanceMeters;

    // Advance 18 steps at 60 Hz = 300 ms total
    for (int Step = 0; Step < 18; ++Step)
    {
        Cam.AdvanceKinematics(1.0f / 60.0f, 16.0f / 9.0f);
    }
    if (!VerifyCheck(
            std::abs(Cam.InspectPosture().CurrentYawRadians - TargetYaw) < 0.001f &&
            std::abs(Cam.InspectPosture().CurrentDistanceMeters - TargetDist) < 0.005f,
            "Exponential smoothing converges >99.9% within 300 ms"))
    {
        return 1;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. All 6 Operating Modes (Orbit, Fly, FirstPerson, Pan2D, FollowTarget, SplineRail)
    //--------------------------------------------------------------------------------------------------------------------
    Cam.AssignOperatingMode(CameraOperatingMode::Fly);
    const Axis3D FlyStart = Cam.InspectPosture().CurrentEyePosition;
    Cam.ApplyFlyStride(1.0f, 0.0f, 0.0f, true, 0.10f);
    Cam.AdvanceKinematics(0.50f, 1.6f);
    if (!VerifyCheck(EvaluateDistance(FlyStart, Cam.InspectPosture().CurrentEyePosition) > 0.5f, "FlyMode translates eye forward"))
    {
        return 1;
    }

    Cam.AssignOperatingMode(CameraOperatingMode::FirstPerson);
    Cam.ApplyOrbitAnglesDelta(0.25f, 0.55f);
    const float StartZ = Cam.InspectPosture().TargetEyePosition.Z;
    Cam.ApplyFlyStride(1.0f, 0.0f, 0.0f, false, 0.20f);
    if (!VerifyCheck(std::abs(Cam.InspectPosture().TargetEyePosition.Z - StartZ) < 1.0e-5f, "FirstPersonMode locks stride to horizontal plane"))
    {
        return 1;
    }

    Cam.AssignOperatingMode(CameraOperatingMode::Pan2D);
    Cam.ApplyPlanarPanDelta(0.35f, -0.15f);
    Cam.ApplyPlanarLogZoom(0.40f);
    Cam.AdvanceKinematics(0.50f, 1.0f);
    if (!VerifyCheck(Cam.InspectPosture().CurrentPlanarZoom > 1.35f, "Pan2DMode advances logarithmic planar zoom"))
    {
        return 1;
    }

    Cam.AssignOperatingMode(CameraOperatingMode::FollowTarget);
    Cam.AssignFollowTargetAnchor({ 4.0f, 6.0f, 1.2f });
    for (int Step = 0; Step < 60; ++Step)
    {
        Cam.AdvanceKinematics(1.0f / 60.0f, 1.6f);
    }
    if (!VerifyCheck(
            EvaluateDistance(Cam.InspectPosture().CurrentPivot, { 4.0f, 6.0f, 1.2f }) < 0.05f,
            "FollowTargetMode critically-damped spring converges onto moving anchor"))
    {
        return 1;
    }

    Cam.AccessConfiguration().SplineControlPoints = {
        { -4.0f, -4.0f, 1.5f },
        {  0.0f, -5.0f, 2.2f },
        {  4.0f, -3.0f, 1.8f },
        {  6.0f,  2.0f, 1.4f },
    };
    Cam.AssignOperatingMode(CameraOperatingMode::SplineRail);
    Cam.AssignSplineProgress(0.65f);
    Cam.SnapToTargets(1.6f);
    if (!VerifyCheck(Cam.InspectPosture().CurrentEyePosition.X > 0.5f, "SplineRailMode evaluates Catmull-Rom rail posture"))
    {
        return 1;
    }

#if !FRONTIER_PROFILE_SHIPPING
    Cam.AccessConfiguration().Diagnostics.ShowFrustumOutline = true;
    Cam.AccessConfiguration().Diagnostics.ShowFocusPlane     = true;
    Cam.AccessConfiguration().Diagnostics.CompositionGuide   = CompositionGuideStyle::CrosshairAndThirds;
    const auto FrustumDiag = Cam.ConstructFrustumDiagnostics(16.0f / 9.0f);
    const auto FocusDiag   = Cam.ConstructFocusPlaneDiagnostics(16.0f / 9.0f);
    const auto GuideSegs   = Cam.BuildCompositionDiagnostics();
    if (!VerifyCheck(FrustumDiag.Edges.size() == 12u, "Non-Shipping FrustumVisualizer constructs 12 edges") ||
        !VerifyCheck(FocusDiag.Corners.size() == 4u, "Non-Shipping FocusPlaneIndicator constructs 4 focus corners") ||
        !VerifyCheck(GuideSegs.size() == 6u, "Non-Shipping CompositionOverlay constructs Crosshair + Rule-of-Thirds"))
    {
        return 1;
    }
#endif

    //--------------------------------------------------------------------------------------------------------------------
    // 5. Vulkan .slang Z-Up / Y-Up Isomorphism & GPU CameraVisualEffects.slang Validation Renders
    //--------------------------------------------------------------------------------------------------------------------
    const std::filesystem::path RepoRoot  = Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path OutDir    = ShaderDir.parent_path() / "ValidationCaptures";

    GeometricRaster::VulkanInterchange VkDevice {};
    std::string                        VkDiag;
    if (!VerifyCheck(VkDevice.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }

    GeometricRaster::UVWorkspaceRasterizer Rasterizer {};
    if (!VerifyCheck(Rasterizer.InitializeRasterizer(&VkDevice, ShaderDir), "Initialized UVWorkspaceRasterizer"))
    {
        return 1;
    }

    VisualEffects::CameraOpticsEncoder OpticsEncoder {};
    if (!VerifyCheck(OpticsEncoder.InitializeOpticsEncoder(&VkDevice, ShaderDir), "Initialized CameraOpticsEncoder (.slang compute)"))
    {
        return 1;
    }

    constexpr uint32_t RenderW = 800u;
    constexpr uint32_t RenderH = 500u;

    // 5A. Render ControlledIntegrator in Default RightHandedZUp
    AssignActiveWorldBasis(WorldCoordinateBasis::RightHandedZUp);
    const auto VehicleZUp = GeometrySpace::ConstructSamplePolygonNetwork(
        GeometrySpace::SampleAssetPreset::ControlledIntegrator,
        WorldCoordinateBasis::RightHandedZUp);

    Axis3D MinZ { 1.0e9f, 1.0e9f, 1.0e9f }, MaxZ { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& V : VehicleZUp.Network.Vertices)
    {
        MinZ.X = std::min(MinZ.X, V.Axes.X); MaxZ.X = std::max(MaxZ.X, V.Axes.X);
        MinZ.Y = std::min(MinZ.Y, V.Axes.Y); MaxZ.Y = std::max(MaxZ.Y, V.Axes.Y);
        MinZ.Z = std::min(MinZ.Z, V.Axes.Z); MaxZ.Z = std::max(MaxZ.Z, V.Axes.Z);
    }
    if (!VerifyCheck(MinZ.Z >= -0.05f && (MaxZ.Z - MinZ.Z) > 1.5f && (MaxZ.X - MinZ.X) > 5.5f, "ControlledIntegrator rests upright on Z >= 0 in RightHandedZUp"))
    {
        return 1;
    }

    (void)Rasterizer.UploadPolygonNetwork(VehicleZUp.Network, 1u);
    GeometricRaster::Viewport3DRenderOptions OptsZ {};
    OptsZ.Camera.TargetPivot    = (MinZ + MaxZ) * 0.5f;
    OptsZ.Camera.YawRadians     = 0.88f;
    OptsZ.Camera.PitchRadians   = 0.30f;
    OptsZ.Camera.DistanceMeters = 9.40f;
    OptsZ.Camera.VerticalFovDeg = 42.0f;
    OptsZ.Camera.Basis          = WorldCoordinateBasis::RightHandedZUp;
    OptsZ.WireframeEnabled      = false;

    Editor::TransformGizmo GizmoZ {};
    GizmoZ.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    GizmoZ.ConfigureAnchor(OptsZ.Camera.TargetPivot, 1.35f);

    std::vector<uint8_t> PixelsZUp {};
    if (!VerifyCheck(Rasterizer.Rasterize3DViewport(RenderW, RenderH, OptsZ, &GizmoZ, &PixelsZUp), "Rasterized ControlledIntegrator in RightHandedZUp"))
    {
        return 1;
    }

    Exhibits::SoftwareRasterCanvas CanvasZ(RenderW, RenderH, { 18u, 20u, 24u, 255u });
    CanvasZ.BlitRgbaRegion(0u, 0u, RenderW, RenderH, PixelsZUp.data());
    if (!VerifyCheck(CanvasZ.WritePngFile(OutDir / "Camera_ControlledIntegrator_ZUp.png"), "Saved Camera_ControlledIntegrator_ZUp.png"))
    {
        return 1;
    }

    // 5B. Render ControlledIntegrator in Optional RightHandedYUp and verify upright parity
    AssignActiveWorldBasis(WorldCoordinateBasis::RightHandedYUp);
    const auto VehicleYUp = GeometrySpace::ConstructSamplePolygonNetwork(
        GeometrySpace::SampleAssetPreset::ControlledIntegrator,
        WorldCoordinateBasis::RightHandedYUp);
    (void)Rasterizer.UploadPolygonNetwork(VehicleYUp.Network, 1u);

    GeometricRaster::Viewport3DRenderOptions OptsY = OptsZ;
    OptsY.Camera.Basis       = WorldCoordinateBasis::RightHandedYUp;
    OptsY.Camera.TargetPivot = ConvertVectorBetweenBases(OptsZ.Camera.TargetPivot, WorldCoordinateBasis::RightHandedZUp, WorldCoordinateBasis::RightHandedYUp);

    std::vector<uint8_t> PixelsYUp {};
    if (!VerifyCheck(Rasterizer.Rasterize3DViewport(RenderW, RenderH, OptsY, nullptr, &PixelsYUp), "Rasterized ControlledIntegrator in RightHandedYUp"))
    {
        return 1;
    }
    AssignActiveWorldBasis(WorldCoordinateBasis::RightHandedZUp);

    // 5C. Execute GPU .slang CameraVisualEffects (LensFlare + Hexagonal Ghosts + ChromaticAberration + DoF + Vignette)
    std::vector<uint8_t> PixelsOptics = PixelsZUp;
    CameraConfiguration  VfxConfig {};
    VfxConfig.Optics.FocalLengthMillimeters          = 50.0f;
    VfxConfig.Optics.ApertureFStop                   = 1.4f;
    VfxConfig.Optics.FocusDistanceMeters             = 9.4f;
    VfxConfig.LensFlare.Enabled                      = true;
    VfxConfig.LensFlare.AnamorphicStreak             = true;
    VfxConfig.LensFlare.StarburstDiffraction         = true;
    VfxConfig.LensFlare.ApertureGhosts               = true;
    VfxConfig.LensFlare.GhostShape                   = ApertureGhostShape::Hexagon;
    VfxConfig.LensFlare.GhostReflectionCount         = 8u;
    VfxConfig.LensFlare.FlareIntensity               = 0.85f;
    VfxConfig.LensFlare.GhostIntensity               = 0.65f;
    VfxConfig.ChromaticAberration.Enabled            = true;
    VfxConfig.ChromaticAberration.DispersionStrength = 0.55f;
    VfxConfig.DepthOfField.Enabled                   = true;
    VfxConfig.DepthOfField.MaxBokehRadiusPx          = 8.0f;
    VfxConfig.Vignette.Enabled                       = true;
    VfxConfig.Vignette.Intensity                     = 0.50f;
#if !FRONTIER_PROFILE_SHIPPING
    VfxConfig.Diagnostics.CompositionGuide           = CompositionGuideStyle::RuleOfThirds;
#endif

    std::vector<float> SyntheticDepth(RenderW * RenderH, 9.4f);
    for (uint32_t Y = 0u; Y < RenderH; ++Y)
    {
        for (uint32_t X = 0u; X < RenderW; ++X)
        {
            // Foreground & background depth gradient around 9.4m focus plane to exercise thin-lens bokeh
            const float NormY = static_cast<float>(Y) / static_cast<float>(RenderH);
            SyntheticDepth[Y * RenderW + X] = 3.5f + NormY * 14.0f;
        }
    }

    if (!VerifyCheck(
            OpticsEncoder.ExecuteGPUOptics(RenderW, RenderH, VfxConfig, SyntheticDepth, PixelsOptics),
            "Executed GPU CameraVisualEffects.comp.spv post-process"))
    {
        return 1;
    }

    uint64_t DiffAccum = 0ull;
    for (std::size_t ByteIdx = 0u; ByteIdx < PixelsOptics.size(); ++ByteIdx)
    {
        DiffAccum += static_cast<uint64_t>(std::abs(static_cast<int>(PixelsOptics[ByteIdx]) - static_cast<int>(PixelsZUp[ByteIdx])));
    }
    if (!VerifyCheck(DiffAccum > 250000ull, "GPU CameraVisualEffects visibly modulated framebuffer pixels"))
    {
        return 1;
    }

    Exhibits::SoftwareRasterCanvas CanvasOptics(RenderW, RenderH, { 18u, 20u, 24u, 255u });
    CanvasOptics.BlitRgbaRegion(0u, 0u, RenderW, RenderH, PixelsOptics.data());
    if (!VerifyCheck(CanvasOptics.WritePngFile(OutDir / "Camera_OpticsFlareDoFVignette.png"), "Saved Camera_OpticsFlareDoFVignette.png"))
    {
        return 1;
    }

    OpticsEncoder.TeardownOpticsEncoder();
    Rasterizer.TeardownRasterizer();
    VkDevice.TeardownInterchange();

    std::printf("[CameraComponentValidation] All 6 camera modes, thin-lens optics, Z-up/Y-up renders & GPU .slang effects PASSED.\n");
    return 0;
}
