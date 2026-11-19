/*==========================================================================================================================================
                                                     CAMERAEFFECTSVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validation & Visual Proofs for World/Particle-Linked Heat Shimmer, Black Hole Lensing, Glitch, and 3-Mode Camera Motion Blur

#include "Engine/Components/Camera/CameraComponent.h"
#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/VisualEffects/CameraOpticsEncoder.h"
#include "Engine/Components/Camera/VisualEffects/MotionBlurEffect.h"
#include "Engine/Components/Camera/VisualEffects/WorldDistortionEffect.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetRepository.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

struct ValidationScorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[CameraEffectsValidation][FAIL] %s\n", Label);
        }
    }
};

struct SceneGeoAsset
{
    GeometrySpace::PolygonNetwork       Net {};
    SpatialAcceleration::ClusterPyramid Pyr {};
};

[[nodiscard]] SceneGeoAsset CreateBoxAsset(Mathematics::Scalar3D HalfExtents)
{
    SceneGeoAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] SceneGeoAsset CreateSphereAsset(float Radius, uint32_t Sectors, uint32_t Rings)
{
    SceneGeoAsset Asset {};
    GeometrySpace::AppendSpherePrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, Radius, Sectors, Rings, {}, {});
    Asset.Net.LinkOppositeEdges();
    Asset.Net.RecomputeNormals();
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] bool WritePngRgba8(
    const std::filesystem::path& Path,
    uint32_t                     W,
    uint32_t                     H,
    const uint8_t*               Rgba)
{
    Exhibits::SoftwareRasterCanvas Canvas(W, H, { 0u, 0u, 0u, 255u });
    Canvas.BlitRgbaRegion(0u, 0u, W, H, Rgba);
    return Canvas.WritePngFile(Path);
}

void BlitTile(
    std::vector<uint8_t>& Sheet,
    uint32_t              SheetW,
    uint32_t              DestX,
    uint32_t              DestY,
    const uint8_t*        Src,
    uint32_t              SrcW,
    uint32_t              SrcH)
{
    for (uint32_t Y = 0u; Y < SrcH; ++Y)
    {
        for (uint32_t X = 0u; X < SrcW; ++X)
        {
            const std::size_t SIdx = (static_cast<std::size_t>(Y) * SrcW + X) * 4u;
            const std::size_t DIdx = (static_cast<std::size_t>(DestY + Y) * SheetW + (DestX + X)) * 4u;
            Sheet[DIdx + 0u] = Src[SIdx + 0u];
            Sheet[DIdx + 1u] = Src[SIdx + 1u];
            Sheet[DIdx + 2u] = Src[SIdx + 2u];
            Sheet[DIdx + 3u] = 255u;
        }
    }
}

[[nodiscard]] double ComputeMeanAbsDiff(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B)
{
    const std::size_t N = std::min(A.size(), B.size());
    if (N == 0u) return 0.0;
    double Sum = 0.0;
    for (std::size_t I = 0u; I < N; ++I)
    {
        Sum += std::abs(static_cast<double>(A[I]) - static_cast<double>(B[I]));
    }
    return Sum / static_cast<double>(N);
}

[[nodiscard]] GeometricRaster::WorldSceneSpecification BuildCheckeredShowcaseScene(
    SceneGeoAsset& FloorBox,
    SceneGeoAsset& LeftPillar,
    SceneGeoAsset& RightPillar,
    SceneGeoAsset& ChromeOrb)
{
    using namespace Frontier::GeometricRaster;

    FloorBox    = CreateBoxAsset({ 10.0f, 10.0f, 0.25f });
    LeftPillar  = CreateBoxAsset({ 0.42f, 0.42f, 1.45f });
    RightPillar = CreateBoxAsset({ 0.36f, 0.36f, 1.15f });
    ChromeOrb   = CreateSphereAsset(0.46f, 28u, 16u);

    WorldSceneSpecification Scene {};

    {
        MaterialSpace::SurfaceMaterial Stone {};
        Stone.Diffuse.BaseColor  = { 0.46f, 0.43f, 0.39f };
        Stone.Specular.Roughness = 0.58f;
        (void)Scene.MaterialRegister.RegisterMaterial(Stone);
    }
    {
        MaterialSpace::SurfaceMaterial Copper {};
        Copper.Diffuse.BaseColor   = { 0.88f, 0.50f, 0.24f };
        Copper.Specular.Metalness  = 0.85f;
        Copper.Specular.Roughness  = 0.22f;
        (void)Scene.MaterialRegister.RegisterMaterial(Copper);
    }
    {
        MaterialSpace::SurfaceMaterial Slate {};
        Slate.Diffuse.BaseColor   = { 0.18f, 0.24f, 0.32f };
        Slate.Specular.Roughness  = 0.36f;
        (void)Scene.MaterialRegister.RegisterMaterial(Slate);
    }
    {
        MaterialSpace::SurfaceMaterial Chrome {};
        Chrome.Diffuse.BaseColor  = { 0.94f, 0.95f, 0.98f };
        Chrome.Specular.Metalness = 1.0f;
        Chrome.Specular.Roughness = 0.08f;
        (void)Scene.MaterialRegister.RegisterMaterial(Chrome);
    }

    GeometrySpace::PolygonNetwork UnifiedNet {};
    auto AddBox = [&](const SceneGeoAsset& Asset, uint16_t MatSlot, Mathematics::Axis3D Pos, Mathematics::Scalar3D HalfExt)
    {
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &Asset.Pyr;
        Inst.MaterialSlot        = MatSlot;
        Inst.WorldTranslation    = Pos;
        Inst.PreviousTranslation = Pos;
        Inst.UniformScale        = 1.0f;
        Scene.ClusterInstances.push_back(Inst);
        GeometrySpace::AppendBoxPrimitive(UnifiedNet, Pos, HalfExt, {}, {}, 0u, MatSlot);
    };
    auto AddSphere = [&](const SceneGeoAsset& Asset, uint16_t MatSlot, Mathematics::Axis3D Pos, float Radius)
    {
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &Asset.Pyr;
        Inst.MaterialSlot        = MatSlot;
        Inst.WorldTranslation    = Pos;
        Inst.PreviousTranslation = Pos;
        Inst.UniformScale        = 1.0f;
        Scene.ClusterInstances.push_back(Inst);
        GeometrySpace::AppendSpherePrimitive(UnifiedNet, Pos, Radius, 28u, 16u, {}, {}, 0u, MatSlot);
    };

    AddBox(FloorBox,    0u, {  0.0f, 4.0f, -0.25f }, { 10.0f, 10.0f, 0.25f });
    AddBox(LeftPillar,  1u, { -1.9f, 5.2f,  1.45f }, { 0.42f, 0.42f, 1.45f });
    AddBox(RightPillar, 2u, {  1.6f, 4.8f,  1.15f }, { 0.36f, 0.36f, 1.15f });
    AddSphere(ChromeOrb, 3u, { -0.55f, 2.2f, 0.46f }, 0.46f);

    // Architectural grid ribs behind the distortion zone so refractive bending is unmistakably legible
    for (int I = -3; I <= 3; ++I)
    {
        AddBox(RightPillar, (I % 2 == 0) ? 1u : 2u,
               { static_cast<float>(I) * 0.95f, 7.6f, 1.15f },
               { 0.14f, 0.14f, 1.15f });
    }

    (void)Scene.SceneTree.BuildFromPolygonNetwork(UnifiedNet);
    (void)Scene.SceneSdf.BuildFromWideBoundingTree(Scene.SceneTree, 0.24f, 0.45f);

    Scene.Atmosphere.Medium.AtmosphereEnabled       = true;
    Scene.Atmosphere.Medium.SkyBrightnessMultiplier = 1.0f;
    Scene.Atmosphere.Solar.SolarEnabled             = true;
    Scene.Atmosphere.Solar.ManualDirectionOverride  = true;
    Scene.Atmosphere.Solar.ManualAzimuthDegrees     = 230.0f;
    Scene.Atmosphere.Solar.ManualElevationDegrees   = 28.0f;
    Scene.Atmosphere.Clouds.CloudsEnabled           = true;
    GeometricRaster::VolumetricCloudSequence::ApplyCloudCategoryPreset(
        Scene.Atmosphere.Clouds, GeometricRaster::CloudCategoryType::Cumulus);
    Scene.Atmosphere.Clouds.CloudFraction           = 0.40f;
    Scene.Atmosphere.Moons[0].Visible               = false;
    Scene.Atmosphere.Stars.StarfieldEnabled         = false;

    Scene.AreaLights.push_back({ { 0.0f, 1.0f, 4.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 1.0f, 1.0f, { 1.4f, 1.35f, 1.3f }, true });
    return Scene;
}

[[nodiscard]] std::vector<float> ComputeSceneLinearDepth(
    const GeometricRaster::WorldSceneSpecification& Scene,
    const Components::CameraPosture&                Posture,
    float                                           TanHalfX,
    float                                           TanHalfY,
    uint32_t                                        W,
    uint32_t                                        H)
{
    std::vector<float> Depth(static_cast<std::size_t>(W) * H, 120.0f);
    for (uint32_t Y = 0u; Y < H; ++Y)
    {
        const float NdcY = 1.0f - ((static_cast<float>(Y) + 0.5f) / static_cast<float>(H)) * 2.0f;
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float NdcX = ((static_cast<float>(X) + 0.5f) / static_cast<float>(W)) * 2.0f - 1.0f;
            const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis(
                Posture.ForwardAxis +
                Posture.RightAxis * (NdcX * TanHalfX) +
                Posture.UpAxis * (NdcY * TanHalfY));

            SpatialAcceleration::RaySegmentQuery Q {};
            Q.Origin          = Posture.CurrentEyePosition;
            Q.UnitDirection   = Dir;
            Q.MinimumDistance = 0.05f;
            Q.MaximumDistance = 120.0f;
            const auto Hit    = Scene.SceneTree.TraceClosestRay(Q, false);
            if (Hit.HitConfirmed)
            {
                const float ForwardCos = std::max(0.05f, Mathematics::EvaluateDot(Dir, Posture.ForwardAxis));
                Depth[static_cast<std::size_t>(Y) * W + X] = Hit.HitDistanceMeters * ForwardCos;
            }
        }
    }
    return Depth;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::Components;
    using namespace Frontier::Components::VisualEffects;
    ValidationScorecard Score {};

    const std::filesystem::path RepoRoot { FRONTIER_REPO_ROOT_DIR };
    const std::filesystem::path ShaderDir { FRONTIER_COMPILED_SHADER_DIR };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits" / "Images" / "CameraEffects";
    std::filesystem::create_directories(OutDir);

    // 1. Verify Particle -> Camera Distortion Anchor Linking for HeatShimmer, BlackHole, and Glitch
    {
        ParticleSpace::ParticleDocument Doc {};
        {
            auto Rig = ParticleSpace::ConstructDefaultStudioRig();
            Rig.Origin  = ParticleSpace::AnalyticSphereOrigin{ { 0.4f, 3.8f, 1.1f }, 0.65f, false };
            Rig.Carrier = ParticleSpace::RefractionLensCarrier{ false, 1.35f }; // Heat shimmer
            Doc.Rigs.push_back(Rig);
        }
        {
            auto Rig = ParticleSpace::ConstructDefaultStudioRig();
            Rig.Origin  = ParticleSpace::AnalyticSphereOrigin{ { 0.15f, 4.2f, 1.35f }, 0.72f, false };
            Rig.Carrier = ParticleSpace::RefractionLensCarrier{ true, 1.50f }; // Black hole
            Doc.Rigs.push_back(Rig);
        }
        {
            auto Rig = ParticleSpace::ConstructDefaultStudioRig();
            Rig.Origin = ParticleSpace::AnalyticBoxOrigin{ { -0.2f, 3.6f, 1.2f }, { 0.9f, 0.4f, 0.9f } };
            ParticleSpace::SpriteBillboardCarrier Spr {};
            Spr.Cutout = ParticleSpace::SpriteCutoutCategory::HologramGlitch;
            Rig.Carrier = Spr;
            Doc.Rigs.push_back(Rig);
        }

        std::vector<CameraWorldDistortionAnchor> Anchors {};
        LinkParticleDocumentDistortionAnchors(Doc, Anchors);
        Score.Verify(Anchors.size() == 3u, "Linked 3 world distortion anchors directly from ParticleDocument rigs");
        Score.Verify(Anchors.size() >= 3u &&
                     Anchors[0].Kind == WorldDistortionCategory::HeatShimmer &&
                     Anchors[1].Kind == WorldDistortionCategory::BlackHole &&
                     Anchors[2].Kind == WorldDistortionCategory::Glitch,
                     "Particle carriers mapped accurately to HeatShimmer, BlackHole, and Glitch camera anchors");
    }

    // 2. Initialize Vulkan & Render Base 3D Showcase Frame + Linear Depth
    GeometricRaster::VulkanInterchange VkDevice {};
    const bool VkOk = VkDevice.InitializeInterchange(RepoRoot);
    Score.Verify(VkOk, "VulkanInterchange initialized for CameraEffectsValidation");

    GeometricRaster::VulkanWorldRenderSequence WorldGpu {};
    const bool WorldInit = WorldGpu.InitializeSequence(VkDevice, ShaderDir);
    Score.Verify(WorldInit, "VulkanWorldRenderSequence initialized");

    CameraOpticsEncoder OpticsEncoder {};
    Score.Verify(OpticsEncoder.InitializeOpticsEncoder(&VkDevice, ShaderDir),
                 "CameraOpticsEncoder initialized with upgraded MotionBlur + Distortion shader");

    SceneGeoAsset FloorBox {}, LeftPillar {}, RightPillar {}, ChromeOrb {};
    auto Scene = BuildCheckeredShowcaseScene(FloorBox, LeftPillar, RightPillar, ChromeOrb);

    const uint32_t FrameW = 480u;
    const uint32_t FrameH = 270u;
    const float    Aspect = static_cast<float>(FrameW) / static_cast<float>(FrameH);

    // Setup CameraComponent at clean horizon angle
    CameraComponent CamComp {};
    CameraConfiguration BaseCamCfg {};
    BaseCamCfg.OperatingMode                 = CameraOperatingMode::Orbit;
    BaseCamCfg.Optics.FocalLengthMillimeters = 28.0f;
    BaseCamCfg.Optics.FocusDistanceMeters    = 4.5f;
    CamComp.Configure(BaseCamCfg);
    CamComp.AccessPosture().TargetPivot          = { 0.0f, 4.5f, 1.05f };
    CamComp.AccessPosture().TargetYawRadians     = 1.5707963f; // Looking toward +Y
    CamComp.AccessPosture().TargetPitchRadians   = 0.09f;
    CamComp.AccessPosture().TargetDistanceMeters = 7.4f;
    CamComp.SnapToTargets(Aspect);

    const auto& Posture = CamComp.InspectPosture();
    GeometricRaster::PerspectiveCameraBasis WorldCam {};
    WorldCam.CameraOrigin       = Posture.CurrentEyePosition;
    WorldCam.TargetPoint        = Posture.CurrentEyePosition + Posture.ForwardAxis * 8.0f;
    WorldCam.WorldUpAxis        = Posture.UpAxis;
    WorldCam.VerticalFovRadians = Posture.VerticalFovDegrees * Constants::DegToRadFloat;
    WorldCam.NearClipMeters     = BaseCamCfg.Optics.NearClipMeters;
    WorldCam.FarClipMeters      = BaseCamCfg.Optics.FarClipMeters;
    WorldCam.ViewportWidthPx    = FrameW;
    WorldCam.ViewportHeightPx   = FrameH;

    GeometricRaster::WorldRenderConfiguration RenderCfg {};
    RenderCfg.Engine          = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
    RenderCfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery;

    WorldGpu.SynchronizeSceneResources(Scene, nullptr, true);
    std::vector<uint8_t> BasePixels {};
    const bool BaseOk = WorldGpu.RenderWorldViewportWithEngine(RenderCfg, 0u, WorldCam, Scene, BasePixels);
    Score.Verify(BaseOk && BasePixels.size() == FrameW * FrameH * 4u, "Rendered crisp base 3D world frame");

    const float TanY = std::tan(0.5f * Posture.VerticalFovDegrees * Constants::DegToRadFloat);
    const float TanX = TanY * Aspect;
    const std::vector<float> DepthMeters = ComputeSceneLinearDepth(Scene, Posture, TanX, TanY, FrameW, FrameH);

    // 3. World-Linked Heat Shimmer & Black Hole Gravitational Lensing (Exhibit 01)
    std::vector<uint8_t> HeatPixels = BasePixels;
    {
        ParticleSpace::ParticleDocument HeatDoc {};
        auto HeatRig = ParticleSpace::ConstructDefaultStudioRig();
        HeatRig.Origin  = ParticleSpace::AnalyticSphereOrigin{ { -1.35f, 6.0f, 0.95f }, 0.92f, false };
        HeatRig.Carrier = ParticleSpace::RefractionLensCarrier{ false, 1.45f };
        HeatDoc.Rigs.push_back(HeatRig);

        CameraConfiguration Cfg = BaseCamCfg;
        Cfg.EffectClockSeconds            = 1.75f;
        Cfg.HeatShimmer.Enabled           = true;
        Cfg.HeatShimmer.Intensity         = 1.55f;
        Cfg.HeatShimmer.Frequency         = 16.0f;
        Cfg.HeatShimmer.RiseSpeed         = 2.6f;
        Cfg.HeatShimmer.ChromaticDispersion = 0.45f;
        Cfg.HeatShimmer.UseWorldAnchors   = true;
        LinkParticleDocumentDistortionAnchors(HeatDoc, Cfg.WorldDistortionAnchors);

        Score.Verify(
            OpticsEncoder.ExecuteGPUOpticsWithPosture(FrameW, FrameH, Cfg, &Posture, DepthMeters, HeatPixels),
            "Executed GPU world-linked Heat Shimmer");
        Score.Verify(ComputeMeanAbsDiff(BasePixels, HeatPixels) > 0.30,
                     "World-linked Heat Shimmer visibly refracts pixels behind plume");
    }

    std::vector<uint8_t> BlackHolePixels = BasePixels;
    {
        ParticleSpace::ParticleDocument BhDoc {};
        auto BhRig = ParticleSpace::ConstructDefaultStudioRig();
        BhRig.Origin  = ParticleSpace::AnalyticSphereOrigin{ { -1.35f, 6.2f, 1.40f }, 0.76f, false };
        BhRig.Carrier = ParticleSpace::RefractionLensCarrier{ true, 1.25f };
        BhDoc.Rigs.push_back(BhRig);

        CameraConfiguration Cfg = BaseCamCfg;
        Cfg.EffectClockSeconds                  = 2.10f;
        Cfg.BlackHoleLens.Enabled               = true;
        Cfg.BlackHoleLens.LensingStrength       = 1.15f;
        Cfg.BlackHoleLens.EventHorizonScale     = 0.36f;
        Cfg.BlackHoleLens.AccretionGlowIntensity = 1.25f;
        Cfg.BlackHoleLens.ChromaticSplit        = 0.42f;
        Cfg.BlackHoleLens.SwirlTwist            = 0.55f;
        LinkParticleDocumentDistortionAnchors(BhDoc, Cfg.WorldDistortionAnchors);

        Score.Verify(
            OpticsEncoder.ExecuteGPUOpticsWithPosture(FrameW, FrameH, Cfg, &Posture, DepthMeters, BlackHolePixels),
            "Executed GPU world-linked Black Hole Gravitational Lensing");
        Score.Verify(ComputeMeanAbsDiff(BasePixels, BlackHolePixels) > 3.5,
                     "World-linked Black Hole visibly bends & illuminates background");

        // Depth occlusion test: foreground pixels closer than the Black Hole anchor (DepthMeters < AnchorDepth - 0.5)
        // inside the Black Hole's screen influence must suffer zero lensing distortion or accretion bleed.
        const auto BhProj = ProjectWorldDistortionAnchor(Cfg, Posture, Cfg.WorldDistortionAnchors[0], Aspect);
        uint32_t OccludedForegroundSamples = 0u;
        uint32_t CorruptedForegroundSamples = 0u;
        for (uint32_t Y = 0u; Y < FrameH; ++Y)
        {
            const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(FrameH);
            for (uint32_t X = 0u; X < FrameW; ++X)
            {
                const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(FrameW);
                const float Dx = (U - BhProj.ScreenU) * Aspect;
                const float Dy = (V - BhProj.ScreenV);
                const float Dist = std::sqrt(Dx * Dx + Dy * Dy);
                const std::size_t PixIdx = static_cast<std::size_t>(Y) * FrameW + X;
                if (Dist < BhProj.ScreenRadiusUv * 2.2f &&
                    DepthMeters[PixIdx] + 0.55f < BhProj.LinearDepthMeters)
                {
                    ++OccludedForegroundSamples;
                    const std::size_t C = PixIdx * 4u;
                    const int MaxChDiff = std::max({
                        std::abs(static_cast<int>(BasePixels[C + 0u]) - static_cast<int>(BlackHolePixels[C + 0u])),
                        std::abs(static_cast<int>(BasePixels[C + 1u]) - static_cast<int>(BlackHolePixels[C + 1u])),
                        std::abs(static_cast<int>(BasePixels[C + 2u]) - static_cast<int>(BlackHolePixels[C + 2u]))
                    });
                    if (MaxChDiff > 3)
                    {
                        ++CorruptedForegroundSamples;
                    }
                }
            }
        }
        std::printf("[Diag] BhProj UV=(%.3f,%.3f) Rad=%.4f Depth=%.2f OcclSamples=%u Corrupted=%u\n",
                    BhProj.ScreenU, BhProj.ScreenV, BhProj.ScreenRadiusUv, BhProj.LinearDepthMeters,
                    OccludedForegroundSamples, CorruptedForegroundSamples);
        Score.Verify(OccludedForegroundSamples > 100u && CorruptedForegroundSamples == 0u,
                     "Depth occlusion prevents far Black Hole from distorting closer foreground pillar");
    }

    {
        const uint32_t SheetW = FrameW * 3u;
        const uint32_t SheetH = FrameH;
        std::vector<uint8_t> Sheet(SheetW * SheetH * 4u, 255u);
        BlitTile(Sheet, SheetW, 0u,          0u, BasePixels.data(),      FrameW, FrameH);
        BlitTile(Sheet, SheetW, FrameW,      0u, HeatPixels.data(),      FrameW, FrameH);
        BlitTile(Sheet, SheetW, FrameW * 2u, 0u, BlackHolePixels.data(), FrameW, FrameH);
        Score.Verify(WritePngRgba8(OutDir / "01-WorldLinkedHeatShimmerAndBlackHole.png", SheetW, SheetH, Sheet.data()),
                     "Wrote 01-WorldLinkedHeatShimmerAndBlackHole.png");
    }

    // 4. Glitch Effect (World-Localized Hologram Glitch vs Fullscreen Camera Glitch) (Exhibit 02)
    std::vector<uint8_t> GlitchWorldPixels = BasePixels;
    std::vector<uint8_t> GlitchFullPixels  = BasePixels;
    {
        ParticleSpace::ParticleDocument GlitchDoc {};
        auto GRig = ParticleSpace::ConstructDefaultStudioRig();
        GRig.Origin = ParticleSpace::AnalyticBoxOrigin{ { -1.35f, 6.1f, 1.25f }, { 1.05f, 0.5f, 1.15f } };
        ParticleSpace::SpriteBillboardCarrier Spr {};
        Spr.Cutout = ParticleSpace::SpriteCutoutCategory::HologramGlitch;
        GRig.Carrier = Spr;
        GlitchDoc.Rigs.push_back(GRig);

        CameraConfiguration CfgWorld = BaseCamCfg;
        CfgWorld.EffectClockSeconds            = 0.85f;
        CfgWorld.Glitch.Enabled                = true;
        CfgWorld.Glitch.Intensity              = 1.20f;
        CfgWorld.Glitch.BlockJitter            = 0.85f;
        CfgWorld.Glitch.ScanlineTear           = 0.90f;
        CfgWorld.Glitch.RgbSplit               = 1.00f;
        CfgWorld.Glitch.RateHz                 = 16.0f;
        CfgWorld.Glitch.LocalizeToWorldAnchors = true;
        LinkParticleDocumentDistortionAnchors(GlitchDoc, CfgWorld.WorldDistortionAnchors);

        Score.Verify(
            OpticsEncoder.ExecuteGPUOpticsWithPosture(FrameW, FrameH, CfgWorld, &Posture, DepthMeters, GlitchWorldPixels),
            "Executed GPU world-localized Glitch");

        CameraConfiguration CfgFull = CfgWorld;
        CfgFull.Glitch.LocalizeToWorldAnchors = false;
        Score.Verify(
            OpticsEncoder.ExecuteGPUOpticsWithPosture(FrameW, FrameH, CfgFull, &Posture, DepthMeters, GlitchFullPixels),
            "Executed GPU fullscreen Glitch");

        Score.Verify(ComputeMeanAbsDiff(BasePixels, GlitchWorldPixels) > 0.25, "World-localized Glitch modulates emitter region");
        Score.Verify(ComputeMeanAbsDiff(BasePixels, GlitchFullPixels) > ComputeMeanAbsDiff(BasePixels, GlitchWorldPixels) * 2.5,
                     "Fullscreen Glitch modulates broader screen area than localized emitter Glitch");

        const uint32_t SheetW = FrameW * 2u;
        std::vector<uint8_t> Sheet(SheetW * FrameH * 4u, 255u);
        BlitTile(Sheet, SheetW, 0u,     0u, GlitchWorldPixels.data(), FrameW, FrameH);
        BlitTile(Sheet, SheetW, FrameW, 0u, GlitchFullPixels.data(),  FrameW, FrameH);
        Score.Verify(WritePngRgba8(OutDir / "02-GlitchWorldAndCameraModes.png", SheetW, FrameH, Sheet.data()),
                     "Wrote 02-GlitchWorldAndCameraModes.png");
    }

    // 5. Three Motion Blur Modes (CameraVelocity Reprojection, DirectionalShutter, RadialZoom) (Exhibit 03)
    {
        // Simulate a crisp camera pan + dolly between previous and current frame for CameraVelocity
        CamComp.ApplyOrbitAnglesDelta(0.14f, 0.014f);
        CamComp.ApplyLogarithmicDolly(0.16f);
        CamComp.AdvanceKinematics(0.040f, Aspect);
        const auto& MovedPosture = CamComp.InspectPosture();

        auto RunBlurMode = [&](MotionBlurStyle Style) -> std::vector<uint8_t>
        {
            std::vector<uint8_t> Out = BasePixels;
            CameraConfiguration Cfg = BaseCamCfg;
            Cfg.MotionBlur.Enabled             = true;
            Cfg.MotionBlur.Style               = Style;
            Cfg.MotionBlur.Intensity           = 1.25f;
            Cfg.MotionBlur.ShutterAngleDegrees = 220.0f;
            Cfg.MotionBlur.SampleCount         = 14u;
            Cfg.MotionBlur.MaxBlurRadiusPx     = 28.0f;
            Cfg.MotionBlur.ZoomCenterU         = 0.50f;
            Cfg.MotionBlur.ZoomCenterV         = 0.48f;
            Cfg.MotionBlur.RadialSpinAmount    = 0.28f;
            Cfg.MotionBlur.DirectionalAngleRad = 0.12f;
            const bool Ok = OpticsEncoder.ExecuteGPUOpticsWithPosture(
                FrameW, FrameH, Cfg, &MovedPosture, DepthMeters, Out);
            Score.Verify(Ok, "Executed MotionBlur GPU pass");
            return Out;
        };

        const auto BlurCamVel = RunBlurMode(MotionBlurStyle::CameraVelocity);
        const auto BlurDir    = RunBlurMode(MotionBlurStyle::DirectionalShutter);
        const auto BlurRadial = RunBlurMode(MotionBlurStyle::RadialZoom);

        Score.Verify(ComputeMeanAbsDiff(BasePixels, BlurCamVel) > 2.0, "CameraVelocity motion blur visibly blurs moving camera frame");
        Score.Verify(ComputeMeanAbsDiff(BasePixels, BlurDir)    > 2.0, "DirectionalShutter motion blur visibly blurs frame");
        Score.Verify(ComputeMeanAbsDiff(BasePixels, BlurRadial) > 2.0, "RadialZoom motion blur visibly blurs frame");
        Score.Verify(ComputeMeanAbsDiff(BlurCamVel, BlurRadial) > 1.5 && ComputeMeanAbsDiff(BlurDir, BlurRadial) > 1.5 && ComputeMeanAbsDiff(BlurCamVel, BlurDir) > 1.2,
                     "Three motion blur modes produce distinct blur signatures");

        const uint32_t SheetW = FrameW * 2u;
        const uint32_t SheetH = FrameH * 2u;
        std::vector<uint8_t> Sheet(SheetW * SheetH * 4u, 255u);
        BlitTile(Sheet, SheetW, 0u,     0u,      BasePixels.data(), FrameW, FrameH);
        BlitTile(Sheet, SheetW, FrameW, 0u,      BlurCamVel.data(), FrameW, FrameH);
        BlitTile(Sheet, SheetW, 0u,     FrameH,  BlurDir.data(),    FrameW, FrameH);
        BlitTile(Sheet, SheetW, FrameW, FrameH,  BlurRadial.data(), FrameW, FrameH);
        Score.Verify(WritePngRgba8(OutDir / "03-ThreeMotionBlurModes.png", SheetW, SheetH, Sheet.data()),
                     "Wrote 03-ThreeMotionBlurModes.png");
    }

    OpticsEncoder.TeardownOpticsEncoder();
    WorldGpu.TeardownSequence();
    VkDevice.TeardownInterchange();

    std::printf("[CameraEffectsValidation] Passed: %u, Failed: %u\n", Score.Passed, Score.Failed);
    return Score.Failed == 0u ? 0 : 1;
}
