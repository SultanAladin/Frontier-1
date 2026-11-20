/*==========================================================================================================================================
                                                       LENSFLAREVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validation & Visual Proofs for Baked 3-Archetype Lens Flare (Anamorphic, Streaks, Halos + Coated Ghosts) on CPU & Vulkan GPU

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/VisualEffects/CameraOpticsEncoder.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"
#include "Engine/GeometricRaster/WorldRendering/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/WorldRendering/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
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
            std::fprintf(stderr, "[LensFlareValidation][FAIL] %s\n", Label);
        }
    }
};

struct FlareGeometryAsset
{
    GeometrySpace::PolygonNetwork       Net {};
    SpatialAcceleration::ClusterPyramid Pyr {};
};

[[nodiscard]] FlareGeometryAsset CreateBoxAsset(Mathematics::Scalar3D HalfExtents)
{
    FlareGeometryAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] FlareGeometryAsset CreateSphereAsset(float Radius, uint32_t Sectors, uint32_t Rings)
{
    FlareGeometryAsset Asset {};
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

void BlitScaledRegion(
    uint32_t              SrcW,
    uint32_t              SrcH,
    const uint8_t*        SrcRgba,
    uint32_t              DstX,
    uint32_t              DstY,
    uint32_t              DstW,
    uint32_t              DstH,
    uint32_t              CanvasW,
    std::vector<uint8_t>& CanvasRgba)
{
    for (uint32_t Y = 0u; Y < DstH; ++Y)
    {
        const uint32_t SY = std::min(SrcH - 1u, (Y * SrcH) / std::max(1u, DstH));
        for (uint32_t X = 0u; X < DstW; ++X)
        {
            const uint32_t SX      = std::min(SrcW - 1u, (X * SrcW) / std::max(1u, DstW));
            const std::size_t SOff = (static_cast<std::size_t>(SY) * SrcW + SX) * 4u;
            const std::size_t DOff = (static_cast<std::size_t>(DstY + Y) * CanvasW + (DstX + X)) * 4u;
            CanvasRgba[DOff + 0u] = SrcRgba[SOff + 0u];
            CanvasRgba[DOff + 1u] = SrcRgba[SOff + 1u];
            CanvasRgba[DOff + 2u] = SrcRgba[SOff + 2u];
            CanvasRgba[DOff + 3u] = 255u;
        }
    }
}

[[nodiscard]] double ComputeMeanLuminance(const std::vector<uint8_t>& Rgba, uint32_t W, uint32_t H)
{
    double Sum = 0.0;
    const std::size_t Total = static_cast<std::size_t>(W) * H;
    for (std::size_t I = 0u; I < Total; ++I)
    {
        const double R = static_cast<double>(Rgba[I * 4u + 0u]) / 255.0;
        const double G = static_cast<double>(Rgba[I * 4u + 1u]) / 255.0;
        const double B = static_cast<double>(Rgba[I * 4u + 2u]) / 255.0;
        Sum += 0.2126 * R + 0.7152 * G + 0.0722 * B;
    }
    return Sum / static_cast<double>(std::max<std::size_t>(1u, Total));
}

[[nodiscard]] double ComputeMeanAbsDiff(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B)
{
    const std::size_t N = std::min(A.size(), B.size());
    if (N == 0u)
    {
        return 0.0;
    }
    double Sum = 0.0;
    for (std::size_t I = 0u; I < N; ++I)
    {
        Sum += std::abs(static_cast<double>(A[I]) - static_cast<double>(B[I]));
    }
    return Sum / static_cast<double>(N);
}

[[nodiscard]] GeometricRaster::WorldSceneSpecification BuildBacklitLandscapeScene(
    FlareGeometryAsset& GroundBox,
    FlareGeometryAsset& PillarBox,
    FlareGeometryAsset& ChromeSphere,
    bool                IncludeSolarBlocker)
{
    using namespace Frontier::GeometricRaster;

    GroundBox    = CreateBoxAsset({ 14.0f, 14.0f, 0.25f });
    PillarBox    = CreateBoxAsset({ 0.45f, 0.45f, 1.25f });
    ChromeSphere = CreateSphereAsset(0.42f, 28u, 16u);

    WorldSceneSpecification Scene {};

    {
        MaterialSpace::SurfaceMaterial GroundMat {};
        GroundMat.Diffuse.BaseColor  = { 0.16f, 0.17f, 0.19f };
        GroundMat.Specular.Roughness = 0.65f;
        (void)Scene.MaterialRegister.RegisterMaterial(GroundMat);
    }
    {
        MaterialSpace::SurfaceMaterial PillarMat {};
        PillarMat.Diffuse.BaseColor  = { 0.12f, 0.13f, 0.15f };
        PillarMat.Specular.Roughness = 0.42f;
        PillarMat.Specular.Metalness = 0.35f;
        (void)Scene.MaterialRegister.RegisterMaterial(PillarMat);
    }
    {
        MaterialSpace::SurfaceMaterial ChromeMat {};
        ChromeMat.Diffuse.BaseColor  = { 0.90f, 0.91f, 0.94f };
        ChromeMat.Specular.Roughness = 0.08f;
        ChromeMat.Specular.Metalness = 1.0f;
        (void)Scene.MaterialRegister.RegisterMaterial(ChromeMat);
    }

    GeometrySpace::PolygonNetwork UnifiedWorldNet {};
    auto AddBox = [&](const FlareGeometryAsset& Asset, uint16_t MatSlot, Mathematics::Axis3D Pos, Mathematics::Scalar3D HalfExt, float Scale = 1.0f)
    {
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &Asset.Pyr;
        Inst.MaterialSlot        = MatSlot;
        Inst.WorldTranslation    = Pos;
        Inst.PreviousTranslation = Pos;
        Inst.UniformScale        = Scale;
        Scene.ClusterInstances.push_back(Inst);
        GeometrySpace::AppendBoxPrimitive(UnifiedWorldNet, Pos, { HalfExt.X * Scale, HalfExt.Y * Scale, HalfExt.Z * Scale }, {}, {}, 0u, MatSlot);
    };
    auto AddSphere = [&](const FlareGeometryAsset& Asset, uint16_t MatSlot, Mathematics::Axis3D Pos, float Radius)
    {
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &Asset.Pyr;
        Inst.MaterialSlot        = MatSlot;
        Inst.WorldTranslation    = Pos;
        Inst.PreviousTranslation = Pos;
        Inst.UniformScale        = 1.0f;
        Scene.ClusterInstances.push_back(Inst);
        GeometrySpace::AppendSpherePrimitive(UnifiedWorldNet, Pos, Radius, 28u, 16u, {}, {}, 0u, MatSlot);
    };

    AddBox(GroundBox, 0u, { 0.0f, 4.0f, -0.25f }, { 14.0f, 14.0f, 0.25f });
    AddBox(PillarBox, 1u, { -3.1f, 4.5f, 1.25f }, { 0.45f, 0.45f, 1.25f });
    AddSphere(ChromeSphere, 2u, { 1.85f, 2.6f, 0.42f }, 0.42f);

    if (IncludeSolarBlocker)
    {
        AddBox(PillarBox, 1u, { -0.61f, 2.30f, 1.65f }, { 0.45f, 0.45f, 1.25f }, 1.8f);
    }

    (void)Scene.SceneTree.BuildFromPolygonNetwork(UnifiedWorldNet);

    Scene.Atmosphere.Medium.AtmosphereEnabled       = true;
    Scene.Atmosphere.Medium.SkyBrightnessMultiplier = 1.0f;
    Scene.Atmosphere.Solar.SolarEnabled             = true;
    Scene.Atmosphere.Solar.ManualDirectionOverride  = true;
    Scene.Atmosphere.Solar.ManualAzimuthDegrees     = 264.0f;
    Scene.Atmosphere.Solar.ManualElevationDegrees   = 13.5f;
    Scene.Atmosphere.Solar.AngularDiameterDegrees   = 1.45f;
    Scene.Atmosphere.Solar.SolarIntensityMultiplier = 22.0f;
    Scene.Atmosphere.Clouds.CloudsEnabled           = true;
    VolumetricCloudSequence::ApplyCloudCategoryPreset(Scene.Atmosphere.Clouds, CloudCategoryType::Cirrus);
    Scene.Atmosphere.Clouds.CloudFraction           = 0.34f;
    Scene.Atmosphere.Clouds.ExtinctionMultiplier    = 0.75f;
    Scene.Atmosphere.Moons[0].Visible               = false;
    Scene.Atmosphere.Stars.StarfieldEnabled         = false;

    Scene.Atmosphere.LensFlare.Enabled               = true;
    Scene.Atmosphere.LensFlare.Style                 = LensFlareStyle::Anamorphic;
    Scene.Atmosphere.LensFlare.FlareIntensity        = 1.15f;
    Scene.Atmosphere.LensFlare.FlareSpread           = 0.95f;
    Scene.Atmosphere.LensFlare.HaloIntensity         = 0.85f;
    Scene.Atmosphere.LensFlare.GhostIntensity        = 0.72f;
    Scene.Atmosphere.LensFlare.GhostSpacing          = 0.80f;
    Scene.Atmosphere.LensFlare.GhostCount            = 8u;
    Scene.Atmosphere.LensFlare.GhostSides            = 6u;
    Scene.Atmosphere.LensFlare.StreakRotationDegrees = 14.0f;

    Scene.AreaLights.push_back({ { 0.0f, 1.5f, 3.2f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.8f, 0.8f, { 1.4f, 1.3f, 1.2f }, true });
    return Scene;
}

[[nodiscard]] GeometricRaster::PerspectiveCameraBasis BuildHorizonCamera(uint32_t WidthPx, uint32_t HeightPx)
{
    GeometricRaster::PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = { 0.0f, -3.5f, 0.72f };
    Cam.TargetPoint        = { -0.4f, 8.0f, 2.35f };
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = 46.0f * Constants::DegToRadFloat;
    Cam.NearClipMeters     = 0.08f;
    Cam.FarClipMeters      = 250.0f;
    Cam.ViewportWidthPx    = WidthPx;
    Cam.ViewportHeightPx   = HeightPx;
    return Cam;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    ValidationScorecard Score {};

    const std::filesystem::path RepoRoot { FRONTIER_REPO_ROOT_DIR };
    const std::filesystem::path ShaderDir { FRONTIER_COMPILED_SHADER_DIR };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits" / "Images" / "LensFlare";
    std::filesystem::create_directories(OutDir);

    VulkanInterchange VkDevice {};
    const bool VkOk = VkDevice.InitializeInterchange(RepoRoot);
    Score.Verify(VkOk, "VulkanInterchange initialized for LensFlareValidation");

    // 1. Bake & Persist 512x768 3-Archetype Optical Kernel Atlas (GPU + CPU deterministic verification)
    const std::vector<uint8_t> CpuAtlas = LensFlareSequence::BakeOpticalKernelAtlasRgba8();
    const std::size_t ExpectedAtlasBytes = static_cast<std::size_t>(LensFlareSequence::AtlasTileWidthPx) *
                                           (LensFlareSequence::AtlasTileHeightPx *
                                            LensFlareSequence::AtlasStyleCount) * 4u;
    Score.Verify(CpuAtlas.size() == ExpectedAtlasBytes, "CPU optical kernel baker generated 512x768 RGBA8 atlas");

    const std::filesystem::path AtlasAssetPath = RepoRoot / "EngineContent" / "Optics" / "LensFlareBakedAtlas.ImageSpace";
    std::error_code Ec {};
    std::filesystem::remove(AtlasAssetPath, Ec);

    std::vector<uint8_t> GpuAtlas {};
    const bool GpuBakedOk = LensFlareSequence::BakeOpticalAtlasOnGpu(VkDevice, ShaderDir, GpuAtlas);
    Score.Verify(GpuBakedOk && GpuAtlas.size() == ExpectedAtlasBytes, "LensFlareBakeCompute.comp.spv baked 512x768 RGBA8 atlas on GPU");

    const bool SavedOk = LensFlareSequence::SaveBakedAtlasToImageSpace(AtlasAssetPath, GpuAtlas);
    Score.Verify(SavedOk && std::filesystem::exists(AtlasAssetPath), "Persisted EngineContent/Optics/LensFlareBakedAtlas.ImageSpace");

    std::vector<uint8_t> ActiveAtlas {};
    const bool LoadedOk = LensFlareSequence::LoadBakedAtlasFromImageSpace(AtlasAssetPath, ActiveAtlas);
    Score.Verify(LoadedOk && ActiveAtlas.size() == ExpectedAtlasBytes, "Loaded baked atlas back from EngineContent/Optics/LensFlareBakedAtlas.ImageSpace");

    const auto& CachedAtlas = LensFlareSequence::QueryBakedLensFlareAtlasRgba8();
    Score.Verify(CachedAtlas.size() == ExpectedAtlasBytes, "QueryBakedLensFlareAtlasRgba8 returns 512x768 RGBA8 atlas");

    const uint32_t TileBytes = LensFlareSequence::AtlasTileWidthPx * LensFlareSequence::AtlasTileHeightPx * 4u;
    std::vector<uint8_t> Tile0(ActiveAtlas.begin(), ActiveAtlas.begin() + TileBytes);
    std::vector<uint8_t> Tile1(ActiveAtlas.begin() + TileBytes, ActiveAtlas.begin() + TileBytes * 2u);
    std::vector<uint8_t> Tile2(ActiveAtlas.begin() + TileBytes * 2u, ActiveAtlas.end());

    const double CpuGpuBakeDiff = ComputeMeanAbsDiff(CpuAtlas, ActiveAtlas);
    const double Diff01         = ComputeMeanAbsDiff(Tile0, Tile1);
    const double Diff12         = ComputeMeanAbsDiff(Tile1, Tile2);
    const double Diff02         = ComputeMeanAbsDiff(Tile0, Tile2);
    Score.Verify(CpuGpuBakeDiff < 4.0, "GPU LensFlareBakeCompute matches deterministic CPU baker (< 4 LSB MAD)");
    Score.Verify(Diff01 > 3.0 && Diff12 > 3.0 && Diff02 > 3.0, "All 3 baked optical kernel tiles (Anamorphic/Streaks/Halos) are distinct");

    // 2. Quantitative CPU evaluation checks (Halos + Archetype separation + Occlusion)
    {
        LensFlareConfiguration Cfg {};
        Cfg.Enabled        = true;
        Cfg.FlareIntensity = 1.1f;
        Cfg.HaloIntensity  = 1.1f;
        Cfg.GhostIntensity = 0.7f;

        ProjectedLensFlareSlice Proj {};
        Proj.ScreenU       = 0.35f;
        Proj.ScreenV       = 0.32f;
        Proj.EffectiveGain = 1.0f;
        Proj.SourceTint    = { 1.0f, 0.92f, 0.78f };

        auto SampleOffAxisRing = [&](LensFlareStyle Style, float HaloGain) -> float
        {
            Cfg.Style         = Style;
            Cfg.HaloIntensity = HaloGain;
            const float RingR = 0.21f * Cfg.FlareSpread;
            float Accum = 0.0f;
            for (uint32_t I = 0u; I < 16u; ++I)
            {
                const float Ang = static_cast<float>(I) * (6.2831853f / 16.0f);
                const float U   = Proj.ScreenU + std::cos(Ang) * RingR / (16.0f / 9.0f);
                const float V   = Proj.ScreenV + std::sin(Ang) * RingR;
                const auto  C   = LensFlareSequence::EvaluateBakedLensFlare(Cfg, Proj, U, V, 16.0f / 9.0f);
                Accum += C.R + C.G + C.B;
            }
            return Accum / 16.0f;
        };

        const float AnamWithHalo   = SampleOffAxisRing(LensFlareStyle::Anamorphic, 1.2f);
        const float AnamNoHalo     = SampleOffAxisRing(LensFlareStyle::Anamorphic, 0.0f);
        const float StreakWithHalo = SampleOffAxisRing(LensFlareStyle::Streaks, 1.2f);
        const float StreakNoHalo   = SampleOffAxisRing(LensFlareStyle::Streaks, 0.0f);
        const float HaloWithHalo   = SampleOffAxisRing(LensFlareStyle::Halos, 1.2f);
        const float HaloNoHalo     = SampleOffAxisRing(LensFlareStyle::Halos, 0.0f);

        Score.Verify(AnamWithHalo > AnamNoHalo + 0.01f, "Anamorphic archetype visibly supports HaloIntensity");
        Score.Verify(StreakWithHalo > StreakNoHalo + 0.01f, "Streaks archetype visibly supports HaloIntensity");
        Score.Verify(HaloWithHalo > HaloNoHalo + 0.02f, "Halos archetype strongly modulates concentric spectral rings");
    }

    // 3. Render 3 Hero Exhibits on GPU + CPU World Renderers (Anamorphic, Streaks, Halos)
    FlareGeometryAsset GroundBox {}, PillarBox {}, ChromeSphere {};
    FlareGeometryAsset BGroundBox {}, BPillarBox {}, BChromeSphere {};
    auto OpenScene    = BuildBacklitLandscapeScene(GroundBox, PillarBox, ChromeSphere, false);
    auto BlockedScene = BuildBacklitLandscapeScene(BGroundBox, BPillarBox, BChromeSphere, true);

    const uint32_t FrameW = 720u;
    const uint32_t FrameH = 405u;
    const auto Cam    = BuildHorizonCamera(FrameW, FrameH);
    const auto CpuCam = BuildHorizonCamera(360u, 202u);

    VulkanWorldRenderSequence GpuRenderer {};
    const bool GpuInit = GpuRenderer.InitializeSequence(VkDevice, ShaderDir);
    Score.Verify(GpuInit, "VulkanWorldRenderSequence initialized for lens flare validation");

    TieredWorldRenderSequence CpuRenderer {};
    WorldRenderConfiguration CpuCfg {};
    CpuCfg.Engine          = WorldRenderEngine::OpenPbrDirect;
    CpuCfg.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;

    WorldRenderConfiguration GpuCfg {};
    GpuCfg.Engine          = WorldRenderEngine::OpenPbrDirect;
    GpuCfg.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;

    CpuRenderer.PrepareWorldScene(OpenScene);
    GpuRenderer.SynchronizeSceneResources(OpenScene, true);

    // 3A. Anamorphic Cinema Flare (GPU + CPU parity + PNG)
    std::vector<uint8_t> GpuAnamorphic {};
    OpenScene.Atmosphere.LensFlare.Style                 = LensFlareStyle::Anamorphic;
    OpenScene.Atmosphere.LensFlare.FlareIntensity        = 1.05f;
    OpenScene.Atmosphere.LensFlare.HaloIntensity         = 0.55f;
    OpenScene.Atmosphere.LensFlare.GhostIntensity        = 0.72f;
    OpenScene.Atmosphere.LensFlare.GhostSides            = 6u;
    OpenScene.Atmosphere.LensFlare.StreakRotationDegrees = 0.0f;
    const bool OkGpuAnam = GpuRenderer.RenderWorldViewportWithEngine(GpuCfg, 0u, Cam, OpenScene, GpuAnamorphic);
    Score.Verify(OkGpuAnam && GpuAnamorphic.size() == FrameW * FrameH * 4u, "GPU rendered 01-AnamorphicCinemaFlare");

    const auto CpuAnamFrame = CpuRenderer.RenderWorldFrameWithEngine(CpuCfg, CpuCam, CpuCam, OpenScene);
    Score.Verify(CpuAnamFrame.DisplayRgba8.size() == 360u * 202u * 4u, "CPU rendered Anamorphic frame");

    std::vector<uint8_t> GpuFlareOff {};
    OpenScene.Atmosphere.LensFlare.Enabled = false;
    (void)GpuRenderer.RenderWorldViewportWithEngine(GpuCfg, 0u, Cam, OpenScene, GpuFlareOff);
    OpenScene.Atmosphere.LensFlare.Enabled = true;

    const double LumOff  = ComputeMeanLuminance(GpuFlareOff, FrameW, FrameH);
    const double LumAnam = ComputeMeanLuminance(GpuAnamorphic, FrameW, FrameH);
    Score.Verify(LumAnam > LumOff + 0.003 && ComputeMeanAbsDiff(GpuFlareOff, GpuAnamorphic) > 0.8,
                 "Anamorphic lens flare visibly illuminates GPU framebuffer");
    Score.Verify(WritePngRgba8(OutDir / "01-AnamorphicCinemaFlare.png", FrameW, FrameH, GpuAnamorphic.data()),
                 "Wrote 01-AnamorphicCinemaFlare.png");

    // 3B. Streaks Diffraction Flare (GPU + PNG)
    std::vector<uint8_t> GpuStreaks {};
    OpenScene.Atmosphere.Solar.ManualAzimuthDegrees      = 267.0f;
    OpenScene.Atmosphere.Solar.ManualElevationDegrees    = 15.0f;
    OpenScene.Atmosphere.LensFlare.Style                 = LensFlareStyle::Streaks;
    OpenScene.Atmosphere.LensFlare.FlareIntensity        = 1.25f;
    OpenScene.Atmosphere.LensFlare.HaloIntensity         = 0.75f;
    OpenScene.Atmosphere.LensFlare.GhostIntensity        = 0.68f;
    OpenScene.Atmosphere.LensFlare.GhostSides            = 8u;
    OpenScene.Atmosphere.LensFlare.StreakRotationDegrees = 18.0f;
    const bool OkGpuStreaks = GpuRenderer.RenderWorldViewportWithEngine(GpuCfg, 0u, Cam, OpenScene, GpuStreaks);
    Score.Verify(OkGpuStreaks && GpuStreaks.size() == FrameW * FrameH * 4u, "GPU rendered 02-StreaksDiffractionFlare");
    Score.Verify(ComputeMeanAbsDiff(GpuAnamorphic, GpuStreaks) > 2.5, "Streaks exhibit visually differs from Anamorphic exhibit");
    Score.Verify(WritePngRgba8(OutDir / "02-StreaksDiffractionFlare.png", FrameW, FrameH, GpuStreaks.data()),
                 "Wrote 02-StreaksDiffractionFlare.png");

    // 3C. Halos Spectral Flare (GPU + PNG)
    std::vector<uint8_t> GpuHalos {};
    OpenScene.Atmosphere.Solar.ManualAzimuthDegrees      = 265.0f;
    OpenScene.Atmosphere.Solar.ManualElevationDegrees    = 14.0f;
    OpenScene.Atmosphere.LensFlare.Style                 = LensFlareStyle::Halos;
    OpenScene.Atmosphere.LensFlare.FlareIntensity        = 1.20f;
    OpenScene.Atmosphere.LensFlare.HaloIntensity         = 1.45f;
    OpenScene.Atmosphere.LensFlare.GhostIntensity        = 0.82f;
    OpenScene.Atmosphere.LensFlare.GhostSides            = 0u;
    const bool OkGpuHalos = GpuRenderer.RenderWorldViewportWithEngine(GpuCfg, 0u, Cam, OpenScene, GpuHalos);
    Score.Verify(OkGpuHalos && GpuHalos.size() == FrameW * FrameH * 4u, "GPU rendered 03-HalosSpectralFlare");
    Score.Verify(ComputeMeanAbsDiff(GpuStreaks, GpuHalos) > 2.5, "Halos exhibit visually differs from Streaks exhibit");
    Score.Verify(WritePngRgba8(OutDir / "03-HalosSpectralFlare.png", FrameW, FrameH, GpuHalos.data()),
                 "Wrote 03-HalosSpectralFlare.png");

    // 3D. Solar Occlusion Check: monolith blocking solar line-of-sight attenuates flare
    const Mathematics::Axis3D Fwd  = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin);
    const Mathematics::Axis3D Rgt  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis));
    const Mathematics::Axis3D Up   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Rgt, Fwd));
    const float               TanY = std::tan(Cam.VerticalFovRadians * 0.5f);
    const float               TanX = TanY * (static_cast<float>(FrameW) / static_cast<float>(FrameH));

    const auto OpenProj = LensFlareSequence::ResolveSolarFlareProjection(
        Cam.CameraOrigin, Fwd, Rgt, Up, TanX, TanY, OpenScene.Atmosphere, &OpenScene.SceneTree);
    const auto BlockedProj = LensFlareSequence::ResolveSolarFlareProjection(
        Cam.CameraOrigin, Fwd, Rgt, Up, TanX, TanY, BlockedScene.Atmosphere, &BlockedScene.SceneTree);
    Score.Verify(OpenProj.EffectiveGain > 0.25f && BlockedProj.EffectiveGain < OpenProj.EffectiveGain * 0.2f,
                 "BVH solar line-of-sight occlusion attenuates flare gain");

    // 4. CameraOpticsEncoder GPU Post-Process across all 3 Baked Archetypes + Atlas Composite Sheet
    Components::VisualEffects::CameraOpticsEncoder OpticsEncoder {};
    Score.Verify(OpticsEncoder.InitializeOpticsEncoder(&VkDevice, ShaderDir), "CameraOpticsEncoder initialized with baked flare atlas");

    const uint32_t CamW = 320u;
    const uint32_t CamH = 180u;
    std::vector<uint8_t> DarkBackdrop(CamW * CamH * 4u, 0u);
    for (uint32_t Y = 0u; Y < CamH; ++Y)
    {
        const float T = static_cast<float>(Y) / static_cast<float>(CamH);
        for (uint32_t X = 0u; X < CamW; ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * CamW + X) * 4u;
            DarkBackdrop[Off + 0u] = static_cast<uint8_t>(18.0f + 12.0f * T);
            DarkBackdrop[Off + 1u] = static_cast<uint8_t>(24.0f + 16.0f * T);
            DarkBackdrop[Off + 2u] = static_cast<uint8_t>(38.0f + 22.0f * T);
            DarkBackdrop[Off + 3u] = 255u;
        }
    }

    auto RunCameraArchetype = [&](Components::LensFlareArchetype Arch) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> Pixels = DarkBackdrop;
        Components::CameraConfiguration CamCfg {};
        CamCfg.LensFlare.Enabled              = true;
        CamCfg.LensFlare.Archetype            = Arch;
        CamCfg.LensFlare.SourceNormalizedU    = 0.34f;
        CamCfg.LensFlare.SourceNormalizedV    = 0.36f;
        CamCfg.LensFlare.FlareIntensity       = 1.15f;
        CamCfg.LensFlare.FlareSpread          = 0.90f;
        CamCfg.LensFlare.HaloIntensity        = 1.05f;
        CamCfg.LensFlare.GhostIntensity       = 0.72f;
        CamCfg.LensFlare.GhostSpacing         = 0.75f;
        CamCfg.LensFlare.GhostReflectionCount = 7u;
        CamCfg.LensFlare.GhostShape           = Components::ApertureGhostShape::Hexagon;
        (void)OpticsEncoder.ExecuteGPUOptics(CamW, CamH, CamCfg, {}, Pixels);
        return Pixels;
    };

    const auto CamAnam   = RunCameraArchetype(Components::LensFlareArchetype::Anamorphic);
    const auto CamStreak = RunCameraArchetype(Components::LensFlareArchetype::Streaks);
    const auto CamHalo   = RunCameraArchetype(Components::LensFlareArchetype::Halos);
    Score.Verify(ComputeMeanAbsDiff(CamAnam, CamStreak) > 2.5, "CameraOpticsEncoder Anamorphic vs Streaks differ");
    Score.Verify(ComputeMeanAbsDiff(CamStreak, CamHalo) > 2.5, "CameraOpticsEncoder Streaks vs Halos differ");

    const uint32_t SheetW = 960u;
    const uint32_t SheetH = 380u;
    std::vector<uint8_t> Sheet(SheetW * SheetH * 4u, 14u);
    for (std::size_t I = 3u; I < Sheet.size(); I += 4u)
    {
        Sheet[I] = 255u;
    }
    BlitScaledRegion(512u, 256u, Tile0.data(), 12u,  12u,  304u, 152u, SheetW, Sheet);
    BlitScaledRegion(512u, 256u, Tile1.data(), 328u, 12u,  304u, 152u, SheetW, Sheet);
    BlitScaledRegion(512u, 256u, Tile2.data(), 644u, 12u,  304u, 152u, SheetW, Sheet);
    BlitScaledRegion(CamW, CamH, CamAnam.data(),   12u,  188u, 304u, 171u, SheetW, Sheet);
    BlitScaledRegion(CamW, CamH, CamStreak.data(), 328u, 188u, 304u, 171u, SheetW, Sheet);
    BlitScaledRegion(CamW, CamH, CamHalo.data(),   644u, 188u, 304u, 171u, SheetW, Sheet);
    Score.Verify(WritePngRgba8(OutDir / "04-BakedAtlasAndCameraOptics.png", SheetW, SheetH, Sheet.data()),
                 "Wrote 04-BakedAtlasAndCameraOptics.png");

    OpticsEncoder.TeardownOpticsEncoder();
    GpuRenderer.TeardownSequence();
    VkDevice.TeardownInterchange();

    std::printf("[LensFlareValidation] Passed: %u, Failed: %u\n", Score.Passed, Score.Failed);
    return Score.Failed == 0u ? 0 : 1;
}
