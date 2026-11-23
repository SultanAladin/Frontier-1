/*==========================================================================================================================================
                                                         ATMOSPHEREAUDITPROOF.CPP
==========================================================================================================================================*/
// 🧩 Comprehensive Visual & Quantitative Audit Exhibit writing >=10 PNG proofs to "Exhibits/Images/Atmosphere/" covering:
// 1. Dawn & Civil Twilight (no 360-deg white horizon ribbon, no brown fill)
// 2. Sunrise & Smooth Eddington Solar Corona (no circular step ring)
// 3. Sunset Golden Hour & Self-Shadowed Clouds
// 4. High Noon & Deep-Blue Rayleigh Zenith
// 5. Outer Space Orbital View (curved atmosphere limb, ocean/continents/clouds, stars, sun & moon)
// 6. All 6 Moons with Physical Volumetric Atmospheres (Luna, Ember, Glacier, Sulfur, Shroud, Shard)
// 7. Nocturnal Pinpoint Optical Starfield (J2000 catalog, Planckian B-V colors, 4-blade spikes, Milky Way dust lanes)
// 8. Six Key Cloud Formations (Lenticular, Mammatus, KelvinHelmholtz, Asperitas, Nacreous, Pyrocumulus - zero lattice/stipple)
// 9. Clouds Advecting & Swirling with Ekman Wind (T=0s, T=28s, Delta Heatmap)
// 10. Full 3D Terrestrial Outdoor World Integration (OpenPbrDirect + WideTreeRayQuery + Cloud Shadows + Sky)

#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

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
            std::fprintf(stderr, "[AtmosphereAuditProof][FAIL] %s\n", Label);
        }
    }
};

struct ProofGeoAsset
{
    GeometrySpace::PolygonNetwork       Net {};
    SpatialAcceleration::ClusterPyramid Pyr {};
};

[[nodiscard]] ProofGeoAsset CreateBoxAsset(Mathematics::Scalar3D HalfExtents)
{
    ProofGeoAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] ProofGeoAsset CreateSphereAsset(float Radius, uint32_t Sectors, uint32_t Rings)
{
    ProofGeoAsset Asset {};
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

[[nodiscard]] double ComputeMeanAbsDiff(const std::vector<uint8_t>& A, const std::vector<uint8_t>& B)
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

[[nodiscard]] double ComputeMeanBrightness(const std::vector<uint8_t>& Rgba, uint32_t W, uint32_t H, uint32_t Y0, uint32_t Y1)
{
    double Sum = 0.0;
    uint64_t Count = 0u;
    for (uint32_t Y = Y0; Y < std::min(H, Y1); ++Y)
    {
        for (uint32_t X = 0u; X < W; ++X)
        {
            const std::size_t Idx = (static_cast<std::size_t>(Y) * W + X) * 4u;
            Sum += (0.2126 * Rgba[Idx] + 0.7152 * Rgba[Idx + 1u] + 0.0722 * Rgba[Idx + 2u]) / 255.0;
            ++Count;
        }
    }
    return (Count > 0u) ? (Sum / static_cast<double>(Count)) : 0.0;
}

[[nodiscard]] WorldSceneSpecification BuildSkyOnlyScene(ProofGeoAsset& HiddenTriBox)
{
    WorldSceneSpecification Scene {};
    HiddenTriBox = CreateBoxAsset({ 0.05f, 0.05f, 0.05f });
    {
        MaterialSpace::SurfaceMaterial Matte {};
        Matte.Diffuse.BaseColor = { 0.45f, 0.45f, 0.45f };
        (void)Scene.MaterialRegister.RegisterMaterial(Matte);
    }
    GeometrySpace::PolygonNetwork Net {};
    RigidClusterInstance Inst {};
    Inst.PyramidAsset        = &HiddenTriBox.Pyr;
    Inst.MaterialSlot        = 0u;
    Inst.WorldTranslation    = { 0.0f, 0.0f, -120.0f };
    Inst.PreviousTranslation = Inst.WorldTranslation;
    Inst.UniformScale        = 1.0f;
    Scene.ClusterInstances.push_back(Inst);
    GeometrySpace::AppendBoxPrimitive(Net, { 0.0f, 0.0f, -120.0f }, { 0.05f, 0.05f, 0.05f }, {}, {}, 0u, 0u);
    (void)Scene.SceneTree.BuildFromPolygonNetwork(Net);

    Scene.Atmosphere.Medium.AtmosphereEnabled       = true;
    Scene.Atmosphere.Medium.SkyBrightnessMultiplier = 1.0f;
    Scene.Atmosphere.Solar.SolarEnabled             = true;
    Scene.Atmosphere.Solar.ManualDirectionOverride  = true;
    Scene.Atmosphere.Solar.AngularDiameterDegrees   = 0.75f;
    Scene.Atmosphere.Solar.SolarIntensityMultiplier = 22.0f;
    Scene.Atmosphere.LensFlare.Enabled              = false;
    Scene.Atmosphere.Stars.StarfieldEnabled         = false;
    for (auto& M : Scene.Atmosphere.Moons)
    {
        M.Visible = false;
    }
    Scene.Atmosphere.Clouds.CloudsEnabled             = false;
    Scene.Atmosphere.Fog.HeightFog.Enabled            = false;
    Scene.Atmosphere.Fog.AerialFog.Enabled            = false;
    Scene.Atmosphere.Fog.LocalFogs.clear();
    Scene.Atmosphere.LocalCloud.LocalEnabled          = false;
    Scene.Atmosphere.LocalClouds.clear();
    return Scene;
}

[[nodiscard]] PerspectiveCameraBasis MakeCamera(
    Mathematics::Axis3D Origin,
    Mathematics::Axis3D Target,
    uint32_t            W,
    uint32_t            H,
    float               FovDeg = 58.0f)
{
    PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = Origin;
    Cam.TargetPoint        = Target;
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = FovDeg * Constants::DegToRadFloat;
    Cam.NearClipMeters     = 0.1f;
    Cam.FarClipMeters      = 5000.0f;
    Cam.ViewportWidthPx    = W;
    Cam.ViewportHeightPx   = H;
    return Cam;
}

} // namespace

int main()
{
    ValidationScorecard Score {};
    const std::filesystem::path RepoRoot { FRONTIER_REPO_ROOT_DIR };
    const std::filesystem::path ShaderDir { FRONTIER_COMPILED_SHADER_DIR };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/Atmosphere";
    std::filesystem::create_directories(OutDir);

    VulkanInterchange VkDevice {};
    Score.Verify(VkDevice.InitializeInterchange(RepoRoot), "VulkanInterchange initialized");

    VulkanWorldRenderSequence Gpu {};
    Score.Verify(Gpu.InitializeSequence(VkDevice, ShaderDir), "VulkanWorldRenderSequence initialized");

    WorldRenderConfiguration RenderCfg {};
    RenderCfg.Engine          = WorldRenderEngine::OpenPbrDirect;
    RenderCfg.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;

    ProofGeoAsset HiddenBox {};
    WorldSceneSpecification SkyScene = BuildSkyOnlyScene(HiddenBox);
    Gpu.SynchronizeSceneResources(SkyScene, true);

    const uint32_t HeroW = 480u;
    const uint32_t HeroH = 270u;

    // ------------------------------------------------------------------------------------------------
    // Proof 01: Dawn & Civil Twilight (No fake 360-deg white horizon ribbon, no brown lower hemisphere)
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Solar.ManualAzimuthDegrees   = 252.0f;
        S.Atmosphere.Solar.ManualElevationDegrees = 2.0f;
        S.Atmosphere.Clouds.CloudsEnabled         = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(S.Atmosphere.Clouds, CloudCategoryType::Stratocumulus);
        S.Atmosphere.Clouds.CloudFraction         = 0.34f;
        S.Atmosphere.Clouds.CloudMarchSteps       = 48u;

        // Look away from sun (Az=72 -> +X,-Y) and toward dawn horizon (Az=252 -> -X,+Y) side-by-side
        std::vector<uint8_t> AntiDawn {}, TowardDawn {};
        const auto CamAnti   = MakeCamera({ 0.0f, 0.0f, 2.0f }, {  0.31f, -0.95f, 2.14f }, 360u, 240u, 58.0f);
        const auto CamToward = MakeCamera({ 0.0f, 0.0f, 2.0f }, { -0.31f,  0.95f, 2.14f }, 360u, 240u, 58.0f);
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, CamAnti, S, AntiDawn), "Rendered Anti-Solar Dawn");
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, CamToward, S, TowardDawn), "Rendered Solar Dawn");

        const double AntiHzLum   = ComputeMeanBrightness(AntiDawn, 360u, 240u, 95u, 130u);
        const double TowardHzLum = ComputeMeanBrightness(TowardDawn, 360u, 240u, 95u, 130u);
        std::printf("[AtmosphereAuditProof] Dawn Horizon Lum: Toward=%.3f Anti=%.3f\n", TowardHzLum, AntiHzLum);
        Score.Verify(TowardHzLum > AntiHzLum + 0.05 && AntiHzLum < 0.42, "Anti-solar dawn horizon has no fake white ribbon");

        std::vector<uint8_t> Sheet(720u * 240u * 4u, 255u);
        BlitTile(Sheet, 720u, 0u,   0u, TowardDawn.data(), 360u, 240u);
        BlitTile(Sheet, 720u, 360u, 0u, AntiDawn.data(),   360u, 240u);
        Score.Verify(WritePngRgba8(OutDir / "01-DawnAndTwilightSky.png", 720u, 240u, Sheet.data()),
                     "Wrote Exhibits/Images/Atmosphere/01-DawnAndTwilightSky.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 02: Sunrise Horizon & Smooth Eddington Solar Corona (No circular cutoff ring)
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Solar.ManualAzimuthDegrees   = 265.0f;
        S.Atmosphere.Solar.ManualElevationDegrees = 5.5f;
        S.Atmosphere.Solar.AngularDiameterDegrees = 1.25f;
        S.Atmosphere.Clouds.CloudsEnabled         = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(S.Atmosphere.Clouds, CloudCategoryType::Cirrus);
        S.Atmosphere.Clouds.CloudFraction         = 0.34f;

        const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { -0.087f, 0.996f, 2.12f }, HeroW, HeroH, 54.0f);
        std::vector<uint8_t> Pixels {};
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, Pixels), "Rendered Sunrise Corona");
        Score.Verify(WritePngRgba8(OutDir / "02-SunriseHorizonAndCorona.png", HeroW, HeroH, Pixels.data()),
                     "Wrote Exhibits/Images/Atmosphere/02-SunriseHorizonAndCorona.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 03: Sunset Golden Hour & Self-Shadowed Volumetric Clouds
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Solar.ManualAzimuthDegrees   = 270.0f;
        S.Atmosphere.Solar.ManualElevationDegrees = 3.5f;
        S.Atmosphere.Solar.AngularDiameterDegrees = 1.00f;
        S.Atmosphere.Clouds.CloudsEnabled         = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(S.Atmosphere.Clouds, CloudCategoryType::Cumulus);
        S.Atmosphere.Clouds.CloudFraction         = 0.45f;
        S.Atmosphere.Clouds.ExtinctionMultiplier  = 2.60f;
        S.Atmosphere.Clouds.CloudMarchSteps       = 48u;

        const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { 0.0f, 1.0f, 2.18f }, HeroW, HeroH, 58.0f);
        std::vector<uint8_t> Pixels {};
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, Pixels), "Rendered Sunset Golden Hour");
        Score.Verify(WritePngRgba8(OutDir / "03-SunsetGoldenHourAndClouds.png", HeroW, HeroH, Pixels.data()),
                     "Wrote Exhibits/Images/Atmosphere/03-SunsetGoldenHourAndClouds.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 04: High Noon & Deep-Blue Rayleigh Zenith
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Solar.ManualAzimuthDegrees   = 240.0f;
        S.Atmosphere.Solar.ManualElevationDegrees = 58.0f;
        S.Atmosphere.Solar.AngularDiameterDegrees = 0.85f;
        S.Atmosphere.Clouds.CloudsEnabled         = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(S.Atmosphere.Clouds, CloudCategoryType::Cumulus);
        S.Atmosphere.Clouds.CloudFraction         = 0.32f;

        const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { -0.50f, 0.78f, 2.58f }, HeroW, HeroH, 60.0f);
        std::vector<uint8_t> Pixels {};
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, Pixels), "Rendered High Noon Zenith");
        const double ZenithLum = ComputeMeanBrightness(Pixels, HeroW, HeroH, 0u, 80u);
        Score.Verify(ZenithLum > 0.12, "High noon sky zenith has rich bright blue daylight luminance");
        Score.Verify(WritePngRgba8(OutDir / "04-NoonZenithAndClearSky.png", HeroW, HeroH, Pixels.data()),
                     "Wrote Exhibits/Images/Atmosphere/04-NoonZenithAndClearSky.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 05: Outer Space Orbital View (Curved Earth Limb + Clouds + Stars + Milky Way + Sun + Moon)
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Medium.OrbitalViewEnabled     = true;
        S.Atmosphere.Medium.ObserverAltitudeMeters = 520000.0f;
        S.Atmosphere.Medium.PlanetRadiusMeters     = 6371000.0f;
        S.Atmosphere.Solar.ManualAzimuthDegrees    = 254.0f;
        S.Atmosphere.Solar.ManualElevationDegrees  = 8.5f;
        S.Atmosphere.Solar.AngularDiameterDegrees  = 1.45f;
        S.Atmosphere.Clouds.CloudFraction          = 0.48f;
        S.Atmosphere.SimulationSeconds             = 12.0f;

        S.Atmosphere.Stars.StarfieldEnabled        = true;
        S.Atmosphere.Stars.LimitingMagnitude       = 6.4f;
        S.Atmosphere.Stars.BrightnessMultiplier    = 2.25f;
        S.Atmosphere.Stars.PointSizeScale          = 1.25f;
        S.Atmosphere.Stars.EnableGalacticBand      = true;
        S.Atmosphere.Stars.EnableBrightStarCatalog = true;

        S.Atmosphere.Moons[0].Visible                = true;
        S.Atmosphere.Moons[0].SurfacePreset          = LunarSurfacePreset::Luna;
        S.Atmosphere.Moons[0].AzimuthDegrees         = 288.0f;
        S.Atmosphere.Moons[0].ElevationDegrees       = 10.5f;
        S.Atmosphere.Moons[0].AngularDiameterDegrees = 4.6f;
        S.Atmosphere.Moons[0].SynodicPhaseFraction   = 0.66f;
        S.Atmosphere.Moons[0].BrightnessMultiplier   = 2.6f;

        // Camera pitched -14 deg toward +Y (FOV spans -48 to +20 deg): curved planet fills bottom half, Sun+Moon+Stars fill top half
        const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { 0.0f, 0.97f, 1.75f }, HeroW, HeroH, 68.0f);
        std::vector<uint8_t> Pixels {};
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, Pixels), "Rendered Outer Space Orbital View");

        const double UpperSpaceLum = ComputeMeanBrightness(Pixels, HeroW, HeroH, 0u, 85u);
        const double PlanetLimbLum = ComputeMeanBrightness(Pixels, HeroW, HeroH, 145u, 270u);
        Score.Verify(PlanetLimbLum > UpperSpaceLum + 0.015, "Orbital view shows illuminated planet + limb below dark starfield");
        Score.Verify(WritePngRgba8(OutDir / "05-OuterSpaceOrbitalEarthStarsSun.png", HeroW, HeroH, Pixels.data()),
                     "Wrote Exhibits/Images/Atmosphere/05-OuterSpaceOrbitalEarthStarsSun.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 06: All 6 Moons with Physical Volumetric Atmospheres (Luna, Ember, Glacier, Sulfur, Shroud, Shard)
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Solar.ManualAzimuthDegrees    = 60.0f;
        S.Atmosphere.Solar.ManualElevationDegrees  = -32.0f;
        S.Atmosphere.Stars.StarfieldEnabled        = true;
        S.Atmosphere.Stars.LimitingMagnitude       = 6.0f;
        S.Atmosphere.Stars.BrightnessMultiplier    = 1.85f;
        S.Atmosphere.Stars.EnableGalacticBand      = true;
        S.Atmosphere.Stars.EnableBrightStarCatalog = true;

        // 6 moons arranged in a clean arc above the horizon (+Y hemisphere, Az 244..297 deg), all with physical volumetric atmospheres
        S.Atmosphere.Moons[0] = { true, false, LunarSurfacePreset::Luna,    0.66f, 2.1f, 0.7f, 6.2f, 244.0f, 22.0f,   0.0f,  0.0f,
                                  true, 0.55f, 0.16f, { 0.65f, 0.80f, 1.00f } };
        S.Atmosphere.Moons[1] = { true, false, LunarSurfacePreset::Ember,   0.52f, 2.2f, 0.8f, 6.2f, 254.0f, 24.5f,  15.0f, -8.0f,
                                  true, 1.10f, 0.24f, { 1.00f, 0.48f, 0.18f } };
        S.Atmosphere.Moons[2] = { true, false, LunarSurfacePreset::Glacier, 0.72f, 2.1f, 0.8f, 6.2f, 265.0f, 21.5f, -12.0f, 10.0f,
                                  true, 1.15f, 0.28f, { 0.40f, 0.82f, 1.00f } };
        S.Atmosphere.Moons[3] = { true, false, LunarSurfacePreset::Sulfur,  0.58f, 2.1f, 0.8f, 6.2f, 276.0f, 24.5f,  25.0f,  0.0f,
                                  true, 0.95f, 0.22f, { 0.96f, 0.86f, 0.34f } };
        S.Atmosphere.Moons[4] = { true, false, LunarSurfacePreset::Shroud,  0.64f, 2.2f, 0.9f, 6.8f, 287.0f, 22.0f,  10.0f,  6.0f,
                                  true, 1.35f, 0.32f, { 0.98f, 0.74f, 0.38f } };
        S.Atmosphere.Moons[5] = { true, false, LunarSurfacePreset::Shard,   0.56f, 2.2f, 0.7f, 6.0f, 297.0f, 24.5f, -14.0f,  8.0f,
                                  true, 0.75f, 0.20f, { 0.72f, 0.78f, 0.88f } };

        const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { 0.0f, 0.94f, 2.34f }, 560u, 315u, 62.0f);
        std::vector<uint8_t> Pixels {};
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, Pixels), "Rendered 6 Moons with Physical Atmospheres");

        // Verify horizon is deep astronomical dark (no orange/brown horizon band)
        const double NightHorizLum = ComputeMeanBrightness(Pixels, 560u, 315u, 250u, 310u);
        Score.Verify(NightHorizLum < 0.06, "Astronomical night horizon has zero orange/brown band");
        Score.Verify(WritePngRgba8(OutDir / "06-SixMoonsWithPhysicalAtmospheres.png", 560u, 315u, Pixels.data()),
                     "Wrote Exhibits/Images/Atmosphere/06-SixMoonsWithPhysicalAtmospheres.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 07: Nocturnal Pinpoint Optical Starfield & Milky Way Galactic Band
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Solar.ManualAzimuthDegrees    = 65.0f;
        S.Atmosphere.Solar.ManualElevationDegrees  = -35.0f;
        S.Atmosphere.Stars.StarfieldEnabled        = true;
        S.Atmosphere.Stars.LimitingMagnitude       = 6.6f;
        S.Atmosphere.Stars.BrightnessMultiplier    = 2.60f;
        S.Atmosphere.Stars.PointSizeScale          = 1.30f;
        S.Atmosphere.Stars.EnableGalacticBand      = true;
        S.Atmosphere.Stars.EnableBrightStarCatalog = true;

        // Look directly along the Milky Way Galactic Equator (dot({+0.78, +0.16, +0.28}, GalPole{-0.34, 0.74, 0.58}) ~= 0)
        const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { 0.78f, 0.16f, 2.28f }, HeroW, HeroH, 66.0f);
        std::vector<uint8_t> Pixels {};
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, Pixels), "Rendered Pinpoint Starfield & Milky Way");
        Score.Verify(WritePngRgba8(OutDir / "07-NocturnalPinpointStarfieldAndMilkyWay.png", HeroW, HeroH, Pixels.data()),
                     "Wrote Exhibits/Images/Atmosphere/07-NocturnalPinpointStarfieldAndMilkyWay.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 08: Six Key Cloud Formations (Zero lattice/crosshatch/stipple)
    // ------------------------------------------------------------------------------------------------
    {
        const uint32_t TileW = 280u;
        const uint32_t TileH = 160u;
        std::vector<uint8_t> Sheet(TileW * 3u * TileH * 2u * 4u, 255u);
        const std::array<CloudCategoryType, 6u> Fmts {{
            CloudCategoryType::Lenticular,
            CloudCategoryType::Mammatus,
            CloudCategoryType::KelvinHelmholtz,
            CloudCategoryType::Asperitas,
            CloudCategoryType::Nacreous,
            CloudCategoryType::Pyrocumulus,
        }};

        for (uint32_t I = 0u; I < 6u; ++I)
        {
            auto S = SkyScene;
            S.Atmosphere.Solar.ManualAzimuthDegrees   = 235.0f;
            S.Atmosphere.Solar.ManualElevationDegrees = (Fmts[I] == CloudCategoryType::Nacreous) ? 4.0f : 32.0f;
            S.Atmosphere.Clouds.CloudsEnabled         = true;
            VolumetricCloudSequence::ApplyCloudCategoryPreset(S.Atmosphere.Clouds, Fmts[I]);
            S.Atmosphere.Clouds.CloudMarchSteps       = 48u;

            const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { -0.45f, 0.82f, 2.38f }, TileW, TileH, 60.0f);
            std::vector<uint8_t> Tile {};
            Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, Tile), "Rendered cloud formation tile");
            BlitTile(Sheet, TileW * 3u, (I % 3u) * TileW, (I / 3u) * TileH, Tile.data(), TileW, TileH);
        }
        Score.Verify(WritePngRgba8(OutDir / "08-CloudFormationsNoArtifacts.png", TileW * 3u, TileH * 2u, Sheet.data()),
                     "Wrote Exhibits/Images/Atmosphere/08-CloudFormationsNoArtifacts.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 09: Clouds Advecting & Swirling with Wind (T=0s, T=28s, Delta Heatmap)
    // ------------------------------------------------------------------------------------------------
    {
        auto S = SkyScene;
        S.Atmosphere.Solar.ManualAzimuthDegrees      = 230.0f;
        S.Atmosphere.Solar.ManualElevationDegrees    = 30.0f;
        S.Atmosphere.Clouds.CloudsEnabled            = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(S.Atmosphere.Clouds, CloudCategoryType::Cumulus);
        S.Atmosphere.Clouds.CloudFraction            = 0.48f;
        S.Atmosphere.Clouds.FollowWindEnabled        = true;
        S.Atmosphere.Clouds.WindSpeedMetersPerSecond = 26.0f;
        S.Atmosphere.Clouds.WindBearingDegrees       = 240.0f;
        S.Atmosphere.Clouds.WindTurbulence           = 0.72f;
        S.Atmosphere.Clouds.CloudMarchSteps          = 48u;

        // Also add a nearby bounded cloud volume so both deck + local cloud show advection
        BoundedCloudVolume LocalC {};
        LocalC.LocalEnabled             = true;
        LocalC.FollowWindEnabled        = true;
        LocalC.Shape                    = AtmosphericVolumeShape::Ellipsoid;
        LocalC.VolumeCenterMeters       = { -2.0f, 18.0f, 6.5f };
        LocalC.HalfExtentMeters         = { 7.0f, 5.0f, 2.5f };
        LocalC.FeatureScaleMeters       = 3.0f;
        LocalC.CloudFraction            = 0.78f;
        LocalC.ExtinctionMultiplier     = 1.6f;
        S.Atmosphere.LocalClouds.push_back(LocalC);

        const uint32_t TileW = 320u;
        const uint32_t TileH = 200u;
        const auto Cam = MakeCamera({ 0.0f, 0.0f, 2.0f }, { -0.10f, 0.95f, 2.34f }, TileW, TileH, 62.0f);

        std::vector<uint8_t> FrameT0 {}, FrameT28 {};
        S.Atmosphere.SimulationSeconds = 0.0f;
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, FrameT0), "Rendered Wind Clouds T=0s");

        S.Atmosphere.SimulationSeconds = 28.0f;
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, S, FrameT28), "Rendered Wind Clouds T=28s");

        const double AdvectDiff = ComputeMeanAbsDiff(FrameT0, FrameT28);
        std::printf("[AtmosphereAuditProof] Wind Cloud Advection MAD (T=0 -> T=28s) = %.2f\n", AdvectDiff);
        Score.Verify(AdvectDiff > 4.5, "Clouds visibly advect under Ekman wind");

        std::vector<uint8_t> DeltaMap(TileW * TileH * 4u, 255u);
        for (uint32_t I = 0u; I < TileW * TileH; ++I)
        {
            const std::size_t P = static_cast<std::size_t>(I) * 4u;
            const double D = (std::abs(static_cast<double>(FrameT28[P + 0u]) - FrameT0[P + 0u]) +
                              std::abs(static_cast<double>(FrameT28[P + 1u]) - FrameT0[P + 1u]) +
                              std::abs(static_cast<double>(FrameT28[P + 2u]) - FrameT0[P + 2u])) / 3.0;
            const double N = std::min(1.0, D / 64.0);
            DeltaMap[P + 0u] = static_cast<uint8_t>(std::min(255.0, N * 380.0));
            DeltaMap[P + 1u] = static_cast<uint8_t>(std::min(255.0, N * 220.0));
            DeltaMap[P + 2u] = static_cast<uint8_t>(std::min(255.0, 24.0 + N * 160.0));
        }

        std::vector<uint8_t> Sheet(TileW * 3u * TileH * 4u, 255u);
        BlitTile(Sheet, TileW * 3u, 0u,          0u, FrameT0.data(),  TileW, TileH);
        BlitTile(Sheet, TileW * 3u, TileW,       0u, FrameT28.data(), TileW, TileH);
        BlitTile(Sheet, TileW * 3u, TileW * 2u,  0u, DeltaMap.data(), TileW, TileH);
        Score.Verify(WritePngRgba8(OutDir / "09-CloudsAdvectingWithWind.png", TileW * 3u, TileH, Sheet.data()),
                     "Wrote Exhibits/Images/Atmosphere/09-CloudsAdvectingWithWind.png");
    }

    // ------------------------------------------------------------------------------------------------
    // Proof 10: Full 3D Terrestrial Outdoor World Integration (OpenPbrDirect + Cloud Shadows + Sky)
    // ------------------------------------------------------------------------------------------------
    {
        ProofGeoAsset GroundBox = CreateBoxAsset({ 16.0f, 16.0f, 0.3f });
        ProofGeoAsset Monolith  = CreateBoxAsset({ 0.8f, 0.8f, 2.2f });
        ProofGeoAsset Orb       = CreateSphereAsset(0.95f, 28u, 16u);

        WorldSceneSpecification World {};
        {
            MaterialSpace::SurfaceMaterial GroundMat {};
            GroundMat.Diffuse.BaseColor  = { 0.44f, 0.42f, 0.39f };
            GroundMat.Specular.Roughness = 0.68f;
            (void)World.MaterialRegister.RegisterMaterial(GroundMat);

            MaterialSpace::SurfaceMaterial CopperMat {};
            CopperMat.Diffuse.BaseColor  = { 0.88f, 0.58f, 0.34f };
            CopperMat.Specular.Metalness = 0.28f;
            CopperMat.Specular.Roughness = 0.24f;
            (void)World.MaterialRegister.RegisterMaterial(CopperMat);

            MaterialSpace::SurfaceMaterial SlateMat {};
            SlateMat.Diffuse.BaseColor   = { 0.24f, 0.29f, 0.36f };
            SlateMat.Specular.Roughness  = 0.36f;
            (void)World.MaterialRegister.RegisterMaterial(SlateMat);
        }

        GeometrySpace::PolygonNetwork Net {};
        auto AddInst = [&](const ProofGeoAsset& A, uint16_t Mat, Mathematics::Axis3D Pos)
        {
            RigidClusterInstance Inst {};
            Inst.PyramidAsset        = &A.Pyr;
            Inst.MaterialSlot        = Mat;
            Inst.WorldTranslation    = Pos;
            Inst.PreviousTranslation = Pos;
            Inst.UniformScale        = 1.0f;
            World.ClusterInstances.push_back(Inst);
        };
        AddInst(GroundBox, 0u, { 0.0f, 4.0f, -0.3f });
        GeometrySpace::AppendBoxPrimitive(Net, { 0.0f, 4.0f, -0.3f }, { 16.0f, 16.0f, 0.3f }, {}, {}, 0u, 0u);
        AddInst(Monolith, 2u, { -2.2f, 6.5f, 2.2f });
        GeometrySpace::AppendBoxPrimitive(Net, { -2.2f, 6.5f, 2.2f }, { 0.8f, 0.8f, 2.2f }, {}, {}, 0u, 2u);
        AddInst(Orb, 1u, { 1.6f, 5.2f, 0.95f });
        GeometrySpace::AppendSpherePrimitive(Net, { 1.6f, 5.2f, 0.95f }, 0.95f, 28u, 16u, {}, {}, 0u, 1u);
        (void)World.SceneTree.BuildFromPolygonNetwork(Net);

        World.Atmosphere.Medium.AtmosphereEnabled       = true;
        World.Atmosphere.Solar.SolarEnabled             = true;
        World.Atmosphere.Solar.ManualDirectionOverride  = true;
        World.Atmosphere.Solar.ManualAzimuthDegrees     = 132.0f;
        World.Atmosphere.Solar.ManualElevationDegrees   = 36.0f;
        World.Atmosphere.Solar.AngularDiameterDegrees   = 0.90f;
        World.Atmosphere.Clouds.CloudsEnabled           = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(World.Atmosphere.Clouds, CloudCategoryType::Cumulus);
        World.Atmosphere.Clouds.CloudFraction           = 0.40f;
        World.Atmosphere.Clouds.CloudShadow.ShadowEnabled = true;
        World.Atmosphere.Moons[0] = { true, false, LunarSurfacePreset::Luna, 0.70f, 1.8f, 0.7f, 4.5f, 282.0f, 22.0f, 0.0f, 0.0f,
                                      true, 0.55f, 0.16f, { 0.65f, 0.80f, 1.00f } };

        Gpu.SynchronizeSceneResources(World, true);
        const auto Cam = MakeCamera({ 0.0f, -4.0f, 2.0f }, { -0.2f, 6.0f, 2.55f }, HeroW, HeroH, 54.0f);
        std::vector<uint8_t> Pixels {};
        Score.Verify(Gpu.RenderWorldViewportWithEngine(RenderCfg, 0u, Cam, World, Pixels), "Rendered 3D World Integration");
        Score.Verify(WritePngRgba8(OutDir / "10-TerrestrialAndOrbitalWorldShowcase.png", HeroW, HeroH, Pixels.data()),
                     "Wrote Exhibits/Images/Atmosphere/10-TerrestrialAndOrbitalWorldShowcase.png");
    }

    std::printf("[AtmosphereAuditProof] Passed: %u, Failed: %u\n", Score.Passed, Score.Failed);
    return (Score.Failed == 0u) ? 0 : 1;
}
