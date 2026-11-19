/*==========================================================================================================================================
                                                   CELESTIALCLOUDSVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validation & Visual Proofs for Sun Ephemeris, Bruneton/Hillaire SkyView LUT, 4x Moons, Starfield & Raymarched Volumetric Clouds

#include "Engine/Codecs/ImageCodec.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
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
            std::fprintf(stderr, "[CelestialCloudsValidation][FAIL] %s\n", Label);
        }
    }
};

struct OutdoorGeometryAsset
{
    GeometrySpace::PolygonNetwork       Net {};
    SpatialAcceleration::ClusterPyramid Pyr {};
};

[[nodiscard]] OutdoorGeometryAsset CreateBoxAsset(Mathematics::Scalar3D HalfExtents)
{
    OutdoorGeometryAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] OutdoorGeometryAsset CreateSphereAsset(float Radius, uint32_t Sectors, uint32_t Rings)
{
    OutdoorGeometryAsset Asset {};
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

[[nodiscard]] float MeanRgbaBrightness(const std::vector<uint8_t>& Rgba) noexcept
{
    if (Rgba.empty()) return 0.0f;
    double Sum = 0.0;
    const std::size_t PxCount = Rgba.size() / 4u;
    for (std::size_t I = 0u; I < PxCount; ++I)
    {
        Sum += (0.2126 * Rgba[I * 4u + 0u] + 0.7152 * Rgba[I * 4u + 1u] + 0.0722 * Rgba[I * 4u + 2u]) / 255.0;
    }
    return static_cast<float>(Sum / static_cast<double>(PxCount));
}

[[nodiscard]] GeometricRaster::WorldSceneSpecification BuildOutdoorShowcaseScene(
    OutdoorGeometryAsset& FloorBox,
    OutdoorGeometryAsset& PedestalBox,
    OutdoorGeometryAsset& GoldSphere,
    OutdoorGeometryAsset& GlassSphere)
{
    using namespace Frontier::GeometricRaster;

    FloorBox    = CreateBoxAsset({ 4.2f, 4.2f, 0.12f });
    PedestalBox = CreateBoxAsset({ 0.55f, 0.55f, 0.72f });
    GoldSphere  = CreateSphereAsset(0.58f, 32u, 20u);
    GlassSphere = CreateSphereAsset(0.42f, 28u, 18u);

    WorldSceneSpecification Scene {};

    // Slot 0: Stone Ground Slab
    {
        MaterialSpace::SurfaceMaterial Stone {};
        Stone.Diffuse.BaseColor  = { 0.42f, 0.41f, 0.39f };
        Stone.Specular.Roughness = 0.68f;
        (void)Scene.MaterialRegister.RegisterMaterial(Stone);
    }
    // Slot 1: Brushed Gold Sphere
    {
        MaterialSpace::SurfaceMaterial Gold {};
        Gold.Diffuse.BaseColor   = { 0.96f, 0.76f, 0.34f };
        Gold.Specular.EdgeTintF82 = { 1.00f, 0.90f, 0.68f };
        Gold.Specular.Metalness  = 1.0f;
        Gold.Specular.Roughness  = 0.16f;
        (void)Scene.MaterialRegister.RegisterMaterial(Gold);
    }
    // Slot 2: Crown Glass Sphere
    {
        MaterialSpace::SurfaceMaterial Glass {};
        Glass.Diffuse.BaseColor                = { 0.94f, 0.98f, 1.00f };
        Glass.Specular.Roughness               = 0.04f;
        Glass.Specular.DielectricIOR           = 1.52f;
        Glass.Transmission.TransmissionWeight  = 0.94f;
        Glass.Transmission.AbbeDispersionNumber = 58.0f;
        Glass.Transmission.AttenuationColor    = { 0.95f, 0.98f, 1.00f };
        (void)Scene.MaterialRegister.RegisterMaterial(Glass);
    }
    // Slot 3: Architectural Concrete Plinth
    {
        MaterialSpace::SurfaceMaterial Plinth {};
        Plinth.Diffuse.BaseColor  = { 0.78f, 0.76f, 0.73f };
        Plinth.Specular.Roughness = 0.45f;
        (void)Scene.MaterialRegister.RegisterMaterial(Plinth);
    }

    GeometrySpace::PolygonNetwork UnifiedWorldNet {};
    auto AddBoxInstance = [&](const OutdoorGeometryAsset& Asset, uint16_t MatSlot, Mathematics::Axis3D Pos, Mathematics::Scalar3D HalfExt, bool TransparentGlass = false)
    {
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &Asset.Pyr;
        Inst.MaterialSlot        = MatSlot;
        Inst.WorldTranslation    = Pos;
        Inst.PreviousTranslation = Pos;
        Inst.UniformScale        = 1.0f;
        Inst.TransparentGlass    = TransparentGlass;
        Scene.ClusterInstances.push_back(Inst);
        if (!TransparentGlass)
        {
            GeometrySpace::AppendBoxPrimitive(UnifiedWorldNet, Pos, HalfExt, {}, {}, 0u, MatSlot);
        }
    };
    auto AddSphereInstance = [&](const OutdoorGeometryAsset& Asset, uint16_t MatSlot, Mathematics::Axis3D Pos, float Radius, uint32_t Sectors, uint32_t Rings, bool TransparentGlass = false)
    {
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &Asset.Pyr;
        Inst.MaterialSlot        = MatSlot;
        Inst.WorldTranslation    = Pos;
        Inst.PreviousTranslation = Pos;
        Inst.UniformScale        = 1.0f;
        Inst.TransparentGlass    = TransparentGlass;
        Scene.ClusterInstances.push_back(Inst);
        if (!TransparentGlass)
        {
            GeometrySpace::AppendSpherePrimitive(UnifiedWorldNet, Pos, Radius, Sectors, Rings, {}, {}, 0u, MatSlot);
        }
    };

    AddBoxInstance(FloorBox,       0u, {  0.0f, 1.4f, -0.12f }, { 4.2f, 4.2f, 0.12f });
    AddBoxInstance(PedestalBox,    3u, { -1.1f, 2.2f,  0.72f }, { 0.55f, 0.55f, 0.72f });
    AddSphereInstance(GoldSphere,  1u, {  0.55f, 1.6f, 0.58f }, 0.58f, 32u, 20u, false);
    AddSphereInstance(GlassSphere, 2u, { -1.1f, 2.2f,  1.86f }, 0.42f, 28u, 18u, true);

    (void)Scene.SceneTree.BuildFromPolygonNetwork(UnifiedWorldNet);
    (void)Scene.SceneSdf.BuildFromWideBoundingTree(Scene.SceneTree, 0.20f, 0.45f);
    Scene.LatticeConfig.GridMinimumWorld  = { -3.5f, -2.0f, 0.1f };
    Scene.LatticeConfig.GridMaximumWorld  = {  3.5f,  4.0f, 3.0f };
    Scene.LatticeConfig.CountX            = 5u;
    Scene.LatticeConfig.CountY            = 5u;
    Scene.LatticeConfig.CountZ            = 3u;
    Scene.LatticeConfig.FibonacciRayCount = 48u;
    Scene.LatticeConfig.BouncePasses      = 1u;

    Scene.Atmosphere.Medium.AtmosphereEnabled = true;
    Scene.Atmosphere.Solar.SolarEnabled       = true;
    Scene.Atmosphere.Clouds.CloudsEnabled     = true;
    Scene.Atmosphere.Stars.StarfieldEnabled   = true;
    return Scene;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;

    ValidationScorecard Score {};
    const std::filesystem::path RepoRoot { FRONTIER_REPO_ROOT_DIR };
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/Atmosphere";
    std::filesystem::create_directories(OutDir);

    // -----------------------------------------------------------------------------------------------------------------
    // 1. Solar Ephemeris, Kelvin Tint, & Day Cycle Progression
    // -----------------------------------------------------------------------------------------------------------------
    {
        SolarCelestialConfiguration SolarNoon {};
        SolarNoon.LocalHours = 12.2f;
        const auto SolvedNoon = CelestialEphemerisSolver::SolveSolarEphemeris(SolarNoon);
        Score.Verify(SolvedNoon.ElevationDegrees > 45.0f && SolvedNoon.WorldDirectionToSun.Z > 0.70f,
                     "Solar ephemeris produces high noon elevation (+Z Up)");

        SolarCelestialConfiguration SolarSunset {};
        SolarSunset.LocalHours = 18.0f;
        SolarSunset.TintSource = SolarTintSource::KelvinTemperature;
        SolarSunset.ColorTemperatureKelvin = 2600.0f;
        const auto SolvedSunset = CelestialEphemerisSolver::SolveSolarEphemeris(SolarSunset);
        Score.Verify(SolvedSunset.ElevationDegrees < SolvedNoon.ElevationDegrees,
                     "Sunset solar elevation is lower than solar noon");
        Score.Verify(SolvedSunset.EffectiveSolarTint.R > SolvedSunset.EffectiveSolarTint.B,
                     "Sunset Planckian Kelvin tint shifts warm (R > B)");

        SolarCelestialConfiguration Cycle = SolarNoon;
        Cycle.CycleAnimationEnabled = true;
        Cycle.CycleSpeedPreset      = DayCycleSpeedPreset::Custom;
        Cycle.DayDurationHours      = 0.24f;
        CelestialEphemerisSolver::AdvanceDayCycleHours(Cycle, 12.0);
        Score.Verify(Cycle.LocalHours > SolarNoon.LocalHours,
                     "Day cycle advances LocalHours deterministically");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // 2. Bruneton/Hillaire Optical Transmittance & 96x48 SkyView LUT
    // -----------------------------------------------------------------------------------------------------------------
    {
        AtmosphericSkySequence SkySeq {};
        SolarCelestialConfiguration NoonCfg {};
        NoonCfg.LocalHours = 12.0f;
        const auto NoonSun = CelestialEphemerisSolver::SolveSolarEphemeris(NoonCfg);
        AtmosphericMediumConfiguration Medium {};
        SkySeq.BuildSkyViewLut(NoonSun.WorldDirectionToSun, Medium, NoonCfg.SolarIntensityMultiplier);

        const auto OverheadCol  = SkySeq.SampleSkyViewDirection({ 0.0f, 0.0f, 1.0f });
        const auto LowAngleCol = SkySeq.SampleSkyViewDirection({ 1.0f, 0.0f, 0.02f });
        Score.Verify(OverheadCol.B > OverheadCol.R * 2.5f && OverheadCol.B > 0.02f,
                     "Noon Rayleigh SkyView zenith is deep blue (B > 2.5*R)");
        Score.Verify(LowAngleCol.R + LowAngleCol.G + LowAngleCol.B > OverheadCol.R + OverheadCol.G + OverheadCol.B,
                     "SkyView horizon exhibits Mie forward/limb scattering brightening");

        SolarCelestialConfiguration NightCfg {};
        NightCfg.LocalHours = 0.0f;
        const auto NightSun = CelestialEphemerisSolver::SolveSolarEphemeris(NightCfg);
        SkySeq.BuildSkyViewLut(NightSun.WorldDirectionToSun, Medium, NightCfg.SolarIntensityMultiplier);
        const auto NightOverhead = SkySeq.SampleSkyViewDirection({ 0.0f, 0.0f, 1.0f });
        Score.Verify(NightOverhead.B < OverheadCol.B * 0.1f,
                     "Midnight sky extinguishes daytime scattering down to nocturnal airglow");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // 3. Real 2K Lunar Atlas .ImageSpace Roundtrip & Multi-Moon / Starfield Evaluation
    // -----------------------------------------------------------------------------------------------------------------
    {
        const auto& AtlasRgba = LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8();
        Score.Verify(AtlasRgba.size() == LunarStarfieldSequence::AtlasTileWidthPx *
                                             LunarStarfieldSequence::AtlasTileHeightPx *
                                             LunarStarfieldSequence::AtlasPresetCount * 4u,
                     "Real 2K 6-preset Lunar Atlas dimensions match 512x1536 RGBA8");

        const std::filesystem::path AtlasPath = RepoRoot / "EngineContent/Skies/LunarSurfaceAtlas.ImageSpace";
        std::filesystem::create_directories(AtlasPath.parent_path());

        Codecs::ImageSpaceDocumentModel ImgDoc {};
        ImgDoc.DocumentTitle   = "LunarSurfaceAtlas";
        ImgDoc.NominalWidthPx  = LunarStarfieldSequence::AtlasTileWidthPx;
        ImgDoc.NominalHeightPx = LunarStarfieldSequence::AtlasTileHeightPx * LunarStarfieldSequence::AtlasPresetCount;
        ImgDoc.Layers          = {
            { "Luna", 1.0, true }, { "Ember", 1.0, true }, { "Glacier", 1.0, true },
            { "Sulfur", 1.0, true }, { "Shroud", 1.0, true }, { "Shard", 1.0, true },
        };
        ImgDoc.LinearRgbaPreview.resize(AtlasRgba.size());
        for (std::size_t I = 0u; I < AtlasRgba.size(); ++I)
        {
            ImgDoc.LinearRgbaPreview[I] = static_cast<float>(AtlasRgba[I]) / 255.0f;
        }
        std::string Diag {};
        Score.Verify(Codecs::ImageEncoder::EncodeToFile(AtlasPath, ImgDoc, Diag),
                     "Encoded EngineContent/Skies/LunarSurfaceAtlas.ImageSpace");

        Codecs::ImageSpaceDocumentModel Decoded {};
        Score.Verify(Codecs::ImageDecoder::DecodeFromFile(AtlasPath, Decoded, Diag) &&
                         Decoded.Layers.size() == 6u &&
                         Decoded.NominalWidthPx == 512u,
                     "Decoded EngineContent/Skies/LunarSurfaceAtlas.ImageSpace with 6 real 2K preset layers");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // 4. Volumetric Clouds: 12 Formations (6 Classic + 6 Unique), Local Volume & Solar Cloud Shadow Transmittance
    // -----------------------------------------------------------------------------------------------------------------
    {
        CloudDeckConfiguration Deck {};
        VolumetricCloudSequence::ApplyCloudCategoryPreset(Deck, CloudCategoryType::Cumulonimbus);
        Score.Verify(Deck.LayerThicknessMeters > 4000.0f && Deck.AnvilTopSpread > 0.8f,
                     "Cumulonimbus preset applies tall vertical anvil cloud thickness");

        VolumetricCloudSequence::ApplyCloudCategoryPreset(Deck, CloudCategoryType::Nacreous);
        Score.Verify(Deck.CloudFloorMeters > 10000.0f,
                     "Nacreous stratospheric preset places clouds above 10 km altitude");

        const float ClearShadow = VolumetricCloudSequence::EvaluateSolarCloudTransmittance(
            { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, Deck, 6371000.0f, 0.0f);
        Score.Verify(ClearShadow >= 0.0f && ClearShadow <= 1.0f,
                     "Solar cloud shadow transmittance remains in valid [0..1] optical range");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // 5. Initialize Vulkan + Render CPU/GPU Visual Proof Exhibits
    // -----------------------------------------------------------------------------------------------------------------
    VulkanInterchange VulkanDevice {};
    const bool VulkanOk = VulkanDevice.InitializeInterchange(RepoRoot);
    VulkanWorldRenderSequence GpuRenderer {};
#ifdef FRONTIER_COMPILED_SHADER_DIR
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
#else
    const std::filesystem::path ShaderDir = RepoRoot / "Build/CompiledShaders";
#endif
    const bool GpuOk = VulkanOk && GpuRenderer.InitializeSequence(VulkanDevice, ShaderDir);
    Score.Verify(GpuOk, "VulkanWorldRenderSequence initialized with SkyViewTransmittanceCompute + WorldViewportRadiometricCompute");

    OutdoorGeometryAsset FloorBox {}, PedestalBox {}, GoldSphere {}, GlassSphere {};
    WorldSceneSpecification Scene = BuildOutdoorShowcaseScene(FloorBox, PedestalBox, GoldSphere, GlassSphere);
    TieredWorldRenderSequence CpuRenderer {};

    const uint32_t TileW = 340u;
    const uint32_t TileH = 210u;
    PerspectiveCameraFrame Cam {};
    Cam.CameraOrigin     = { 0.0f, -3.6f, 1.25f };
    Cam.TargetPoint      = { 0.0f,  2.2f, 2.25f };
    Cam.WorldUpAxis      = { 0.0f,  0.0f, 1.0f };
    Cam.VerticalFovRadians = 0.92f;
    Cam.ViewportWidthPx  = TileW;
    Cam.ViewportHeightPx = TileH;

    // Exhibit 01: 4-Time-of-Day Cycle Montage (Dawn, Noon, Golden Hour, Midnight Stars+Moon) on GPU
    {
        const std::array<float, 4u> Hours {{ 6.35f, 12.20f, 17.65f, 23.25f }};
        std::vector<uint8_t> Sheet(static_cast<std::size_t>(TileW * 2u) * (TileH * 2u) * 4u, 255u);
        std::array<float, 4u> Means {};

        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            Scene.Atmosphere.Solar.LocalHours              = Hours[Idx];
            Scene.Atmosphere.Solar.ManualDirectionOverride = false;
            Scene.Atmosphere.Clouds.CloudsEnabled          = true;
            VolumetricCloudSequence::ApplyCloudCategoryPreset(Scene.Atmosphere.Clouds, CloudCategoryType::Cumulus);
            Scene.Atmosphere.Clouds.CloudFraction       = (Idx == 3u) ? 0.15f : 0.38f;
            Scene.Atmosphere.Moons[0].Visible           = true;
            Scene.Atmosphere.Moons[0].SurfacePreset     = LunarSurfacePreset::Luna;
            Scene.Atmosphere.Moons[0].AzimuthDegrees    = 280.0f;
            Scene.Atmosphere.Moons[0].ElevationDegrees  = 22.0f;
            Scene.Atmosphere.Moons[0].AngularDiameterDegrees = 7.5f;
            Scene.Atmosphere.Moons[0].BrightnessMultiplier   = 2.6f;
            Scene.Atmosphere.Stars.StarfieldEnabled     = true;
            Scene.Atmosphere.Stars.PointSizeScale       = 1.6f;

            CpuRenderer.PrepareWorldScene(Scene);
            if (GpuOk)
            {
                GpuRenderer.SynchronizeSceneResources(Scene, &CpuRenderer.InspectIrradianceLattice(), Idx == 0u);
            }

            std::vector<uint8_t> TileRgba;
            WorldRenderConfiguration Cfg {};
            Cfg.Engine           = WorldRenderEngine::DdgiRaytracing;
            Cfg.ShadowAlgorithm  = SolarShadowAlgorithm::WideTreeRayQuery;
            Cfg.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;

            const bool Ok = GpuOk
                ? GpuRenderer.RenderWorldViewportWithEngine(Cfg, 0u, Cam, Scene, TileRgba)
                : false;
            if (!Ok)
            {
                TileRgba = CpuRenderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, Scene).DisplayRgba8;
            }
            Means[Idx] = MeanRgbaBrightness(TileRgba);
            BlitTile(Sheet, TileW * 2u, (Idx % 2u) * TileW, (Idx / 2u) * TileH, TileRgba.data(), TileW, TileH);
        }

        Score.Verify(Means[1] > Means[3] * 2.0f,
                     "Daytime Noon sky+scene render is dramatically brighter than Midnight stars+moon render");
        const std::filesystem::path P1 = OutDir / "01-SkySunDayCycleCpuAndGpu.png";
        Score.Verify(WritePngRgba8(P1, TileW * 2u, TileH * 2u, Sheet.data()),
                     "Exported 01-SkySunDayCycleCpuAndGpu.png");
    }

    // Exhibit 02: All 12 Cloud Formations (6 Classic + 6 Unique) + Bounded LocalCloudVolume
    {
        const std::array<CloudCategoryType, 12u> Types {{
            CloudCategoryType::Stratus,
            CloudCategoryType::Stratocumulus,
            CloudCategoryType::Cumulus,
            CloudCategoryType::Cumulonimbus,
            CloudCategoryType::Altostratus,
            CloudCategoryType::Cirrus,
            CloudCategoryType::Lenticular,
            CloudCategoryType::Mammatus,
            CloudCategoryType::KelvinHelmholtz,
            CloudCategoryType::Asperitas,
            CloudCategoryType::Nacreous,
            CloudCategoryType::Pyrocumulus,
        }};
        std::vector<uint8_t> Sheet(static_cast<std::size_t>(TileW * 4u) * (TileH * 3u) * 4u, 255u);

        PerspectiveCameraFrame SkyCam = Cam;
        SkyCam.CameraOrigin = { 0.0f, -4.5f, 1.4f };
        SkyCam.TargetPoint  = { 0.0f,  5.0f, 3.8f };

        for (uint32_t Idx = 0u; Idx < 12u; ++Idx)
        {
            Scene.Atmosphere.Solar.LocalHours     = (Idx == 10u) ? 17.55f : 10.5f; // Golden-indigo twilight boosts Nacreous iridescence
            Scene.Atmosphere.Moons[0].Visible     = false;
            VolumetricCloudSequence::ApplyCloudCategoryPreset(Scene.Atmosphere.Clouds, Types[Idx]);
            Scene.Atmosphere.LocalCloud.LocalEnabled = (Idx == 2u);
            Scene.Atmosphere.LocalCloud.VolumeCenterMeters = { 0.0f, 6.0f, 3.2f };
            Scene.Atmosphere.LocalCloud.HalfExtentMeters   = { 2.5f, 2.5f, 1.2f };
            Scene.Atmosphere.LocalCloud.FeatureScaleMeters = 1.8f;

            CpuRenderer.SynchronizeWorldAtmosphere(Scene);
            std::vector<uint8_t> TileRgba;
            WorldRenderConfiguration Cfg {};
            Cfg.Engine = WorldRenderEngine::OpenPbrDirect;
            if (!GpuOk || !GpuRenderer.RenderWorldViewportWithEngine(Cfg, 0u, SkyCam, Scene, TileRgba))
            {
                TileRgba = CpuRenderer.RenderWorldFrameWithEngine(Cfg, SkyCam, SkyCam, Scene).DisplayRgba8;
            }
            BlitTile(Sheet, TileW * 4u, (Idx % 4u) * TileW, (Idx / 4u) * TileH, TileRgba.data(), TileW, TileH);
        }

        const std::filesystem::path P2 = OutDir / "02-TwelveCloudFormationsAndLocalCloud.png";
        Score.Verify(WritePngRgba8(P2, TileW * 4u, TileH * 3u, Sheet.data()),
                     "Exported 02-TwelveCloudFormationsAndLocalCloud.png");
    }

    // Exhibit 03: All 6 Real 2K Textured Moons (Luna, Ember, Glacier, Sulfur, Shroud, Shard) + Starfield
    {
        Scene.Atmosphere.LocalCloud.LocalEnabled = false;
        Scene.Atmosphere.Clouds.CloudsEnabled    = false;
        Scene.Atmosphere.Solar.LocalHours        = 23.5f;
        Scene.Atmosphere.Stars.StarfieldEnabled  = true;
        Scene.Atmosphere.Stars.BrightnessMultiplier = 1.8f;
        Scene.Atmosphere.Stars.PointSizeScale    = 1.7f;

        PerspectiveCameraFrame NightCam = Cam;
        NightCam.CameraOrigin = { 0.0f, -4.5f, 1.2f };
        NightCam.TargetPoint  = { 0.0f,  5.0f, 3.6f };
        NightCam.ViewportWidthPx  = 800u;
        NightCam.ViewportHeightPx = 300u;

        // Top panel: 4 simultaneous moons (Luna, Ember, Glacier, Sulfur)
        Scene.Atmosphere.Moons[0] = { true, false, LunarSurfacePreset::Luna,    0.58f, 2.1f, 0.7f, 9.5f, 250.0f, 23.0f, 0.0f, 0.0f };
        Scene.Atmosphere.Moons[1] = { true, false, LunarSurfacePreset::Ember,   0.46f, 2.2f, 0.8f, 9.5f, 263.0f, 25.0f, 18.0f, -10.0f };
        Scene.Atmosphere.Moons[2] = { true, false, LunarSurfacePreset::Glacier, 0.68f, 2.0f, 0.7f, 9.5f, 276.0f, 22.0f, -15.0f, 12.0f };
        Scene.Atmosphere.Moons[3] = { true, false, LunarSurfacePreset::Sulfur,  0.52f, 2.2f, 0.8f, 9.5f, 289.0f, 25.0f, 28.0f, 0.0f };

        CpuRenderer.SynchronizeWorldAtmosphere(Scene);
        std::vector<uint8_t> TopRgba;
        WorldRenderConfiguration Cfg {};
        Cfg.Engine = WorldRenderEngine::OpenPbrDirect;
        if (!GpuOk || !GpuRenderer.RenderWorldViewportWithEngine(Cfg, 0u, NightCam, Scene, TopRgba))
        {
            TopRgba = CpuRenderer.RenderWorldFrameWithEngine(Cfg, NightCam, NightCam, Scene).DisplayRgba8;
        }

        // Bottom panel: Shroud & Shard + full Luna showcase
        Scene.Atmosphere.Moons[0] = { true, false, LunarSurfacePreset::Shroud, 0.56f, 2.3f, 0.7f, 11.0f, 258.0f, 23.0f, 10.0f, 5.0f };
        Scene.Atmosphere.Moons[1] = { true, false, LunarSurfacePreset::Shard,  0.52f, 2.3f, 0.7f, 11.0f, 280.0f, 24.0f, -12.0f, 8.0f };
        Scene.Atmosphere.Moons[2].Visible = false;
        Scene.Atmosphere.Moons[3].Visible = false;
        CpuRenderer.SynchronizeWorldAtmosphere(Scene);
        std::vector<uint8_t> BotRgba;
        if (!GpuOk || !GpuRenderer.RenderWorldViewportWithEngine(Cfg, 0u, NightCam, Scene, BotRgba))
        {
            BotRgba = CpuRenderer.RenderWorldFrameWithEngine(Cfg, NightCam, NightCam, Scene).DisplayRgba8;
        }

        std::vector<uint8_t> Combined(static_cast<std::size_t>(800u) * 600u * 4u, 255u);
        BlitTile(Combined, 800u, 0u,   0u, TopRgba.data(), 800u, 300u);
        BlitTile(Combined, 800u, 0u, 300u, BotRgba.data(), 800u, 300u);

        const std::filesystem::path P3 = OutDir / "03-MultiMoonAtlasAndPhasePresets.png";
        Score.Verify(WritePngRgba8(P3, 800u, 600u, Combined.data()),
                     "Exported 03-MultiMoonAtlasAndPhasePresets.png");
    }

    // Exhibit 04: Outdoor World Architecture Lit by Physical Sky + Sun + Anti-Banded Multi-Scatter Clouds (CPU vs GPU Parity)
    {
        Scene.Atmosphere.Solar.LocalHours     = 15.6f;
        Scene.Atmosphere.Clouds.CloudsEnabled = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(Scene.Atmosphere.Clouds, CloudCategoryType::Cumulus);
        Scene.Atmosphere.Moons[0] = { true, false, LunarSurfacePreset::Luna, 0.62f, 1.8f, 0.8f, 6.0f, 28.0f, 22.0f, 0.0f, 0.0f };
        Scene.Atmosphere.Moons[1].Visible = false;
        Scene.Atmosphere.Moons[2].Visible = false;
        Scene.Atmosphere.Moons[3].Visible = false;

        PerspectiveCameraFrame HeroCam = Cam;
        HeroCam.ViewportWidthPx  = 640u;
        HeroCam.ViewportHeightPx = 400u;

        CpuRenderer.PrepareWorldScene(Scene);
        WorldRenderConfiguration Cfg {};
        Cfg.Engine           = WorldRenderEngine::DdgiRaytracing;
        Cfg.ShadowAlgorithm  = SolarShadowAlgorithm::WideTreeRayQuery;
        Cfg.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;

        const auto CpuOut = CpuRenderer.RenderWorldFrameWithEngine(Cfg, HeroCam, HeroCam, Scene);
        std::vector<uint8_t> GpuOut;
        if (GpuOk)
        {
            GpuRenderer.SynchronizeSceneResources(Scene, &CpuRenderer.InspectIrradianceLattice(), true);
        }
        const bool GpuDone = GpuOk && GpuRenderer.RenderWorldViewportWithEngine(Cfg, 0u, HeroCam, Scene, GpuOut);
        Score.Verify(GpuDone && GpuOut.size() == CpuOut.DisplayRgba8.size(),
                     "Outdoor scene rendered on GPU Vulkan matching CPU buffer resolution");

        const float CpuMean = MeanRgbaBrightness(CpuOut.DisplayRgba8);
        const float GpuMean = MeanRgbaBrightness(GpuOut);
        Score.Verify(std::fabs(CpuMean - GpuMean) < 0.14f,
                     "CPU vs GPU outdoor physical sky + cloud render mean brightness parity (< 0.14)");

        std::vector<uint8_t> SideBySide(static_cast<std::size_t>(640u * 2u) * 400u * 4u, 255u);
        BlitTile(SideBySide, 1280u,   0u, 0u, CpuOut.DisplayRgba8.data(), 640u, 400u);
        BlitTile(SideBySide, 1280u, 640u, 0u, GpuOut.data(),              640u, 400u);
        const std::filesystem::path P4 = OutDir / "04-OutdoorWorldWithAtmosphereGpu.png";
        Score.Verify(WritePngRgba8(P4, 1280u, 400u, SideBySide.data()),
                     "Exported 04-OutdoorWorldWithAtmosphereGpu.png");
    }

    // Exhibit 05: Sky-Alone 24-Hour Day-to-Night Cycle Animation (Zero Foreground Geometry, Zero Clouds)
    {
        WorldSceneSpecification SkyOnlyScene {};
        {
            MaterialSpace::SurfaceMaterial DummyMat {};
            (void)SkyOnlyScene.MaterialRegister.RegisterMaterial(DummyMat);
        }
        SkyOnlyScene.Atmosphere.Medium.AtmosphereEnabled  = true;
        SkyOnlyScene.Atmosphere.Solar.SolarEnabled        = true;
        SkyOnlyScene.Atmosphere.Clouds.CloudsEnabled      = false;
        SkyOnlyScene.Atmosphere.LocalCloud.LocalEnabled   = false;
        SkyOnlyScene.Atmosphere.Moons[0]                  = { true, false, LunarSurfacePreset::Luna, 0.72f, 2.2f, 0.75f, 5.5f, 225.0f, 28.0f, 0.0f, 0.0f };
        SkyOnlyScene.Atmosphere.Stars.StarfieldEnabled    = true;
        SkyOnlyScene.Atmosphere.Stars.PointSizeScale      = 1.5f;
        SkyOnlyScene.Atmosphere.Stars.BrightnessMultiplier = 1.6f;

        const uint32_t FrameW = 360u;
        const uint32_t FrameH = 200u;
        PerspectiveCameraFrame SkyOnlyCam {};
        SkyOnlyCam.CameraOrigin       = { 0.0f, 0.0f, 1.5f };
        SkyOnlyCam.TargetPoint        = { 0.0f, 12.0f, 4.8f };
        SkyOnlyCam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
        SkyOnlyCam.VerticalFovRadians = 0.96f;
        SkyOnlyCam.ViewportWidthPx    = FrameW;
        SkyOnlyCam.ViewportHeightPx   = FrameH;

        constexpr uint32_t FrameCount = 24u;
        const std::filesystem::path FramesDir = "/tmp/frontier_sky_cycle_frames";
        std::filesystem::create_directories(FramesDir);
        std::vector<uint8_t> CycleSheet(static_cast<std::size_t>(FrameW * 6u) * (FrameH * 4u) * 4u, 255u);
        std::array<float, FrameCount> FrameMeans {};

        WorldRenderConfiguration SkyCfg {};
        SkyCfg.Engine = WorldRenderEngine::OpenPbrDirect;

        CpuRenderer.PrepareWorldScene(SkyOnlyScene);
        if (GpuOk)
        {
            GpuRenderer.SynchronizeSceneResources(SkyOnlyScene, &CpuRenderer.InspectIrradianceLattice(), true);
        }

        double MaxSkyColumnJump = 0.0;
        for (uint32_t F = 0u; F < FrameCount; ++F)
        {
            // Sweep smoothly from 05:00 dawn across noon (12:00), sunset (18:00), midnight (24:00) to 04:00 pre-dawn
            const float LocalHr = std::fmod(5.0f + static_cast<float>(F), 24.0f);
            SkyOnlyScene.Atmosphere.Solar.LocalHours = LocalHr;
            SkyOnlyScene.Atmosphere.Solar.ManualDirectionOverride = false;
            SkyOnlyScene.Atmosphere.Moons[0].AzimuthDegrees   = 205.0f + static_cast<float>(F) * 2.5f;
            SkyOnlyScene.Atmosphere.Moons[0].ElevationDegrees = 18.0f + 16.0f * std::sin(static_cast<float>(F) * 0.24f);

            CpuRenderer.SynchronizeWorldAtmosphere(SkyOnlyScene);
            if (GpuOk)
            {
                GpuRenderer.SynchronizeSceneResources(SkyOnlyScene, nullptr, false);
            }

            std::vector<uint8_t> FrameRgba;
            if (!GpuOk || !GpuRenderer.RenderWorldViewportWithEngine(SkyCfg, 0u, SkyOnlyCam, SkyOnlyScene, FrameRgba))
            {
                FrameRgba = CpuRenderer.RenderWorldFrameWithEngine(SkyCfg, SkyOnlyCam, SkyOnlyCam, SkyOnlyScene).DisplayRgba8;
            }
            FrameMeans[F] = MeanRgbaBrightness(FrameRgba);

            // Check horizontal-band-free vertical gradient smoothness across 3 sky columns (minimum jump ignores pinpoint stars/sun)
            const auto ColLuma = [&](uint32_t CX, uint32_t Y) -> double
            {
                const std::size_t P = (static_cast<std::size_t>(Y) * FrameW + CX) * 4u;
                return 0.2126 * FrameRgba[P] + 0.7152 * FrameRgba[P + 1u] + 0.0722 * FrameRgba[P + 2u];
            };
            for (uint32_t Y = 5u; Y + 5u < FrameH / 2u; ++Y)
            {
                const double J0 = std::abs(ColLuma(30u, Y + 1u) - ColLuma(30u, Y));
                const double J1 = std::abs(ColLuma(90u, Y + 1u) - ColLuma(90u, Y));
                const double J2 = std::abs(ColLuma(150u, Y + 1u) - ColLuma(150u, Y));
                const double RowBandJump = std::min({ J0, J1, J2 });
                MaxSkyColumnJump = std::max(MaxSkyColumnJump, RowBandJump);
            }

            char NameBuf[64];
            std::snprintf(NameBuf, sizeof(NameBuf), "frame_%02u.png", F);
            (void)WritePngRgba8(FramesDir / NameBuf, FrameW, FrameH, FrameRgba.data());
            BlitTile(CycleSheet, FrameW * 6u, (F % 6u) * FrameW, (F / 6u) * FrameH, FrameRgba.data(), FrameW, FrameH);
        }

        // Frame 7 is 12:00 Noon, Frame 19 is 00:00 Midnight
        Score.Verify(FrameMeans[7] > FrameMeans[19] * 3.0f,
                     "Sky-alone day-to-night cycle transitions cleanly from bright noon to dark starry night");
        Score.Verify(MaxSkyColumnJump < 12.0,
                     "Sky-alone vertical gradient is smooth without Mach-band contour steps (< 12 LSB/px)");

        const std::filesystem::path P5 = OutDir / "05-SkyAloneDayToNightCycle.png";
        Score.Verify(WritePngRgba8(P5, FrameW * 6u, FrameH * 4u, CycleSheet.data()),
                     "Exported 05-SkyAloneDayToNightCycle.png");
    }

    // Exhibit 06: 4 Named Astronomical Starfield Presets (ZodiacalLightDusk, SouthernCrossCoalsack, WinterHunterBetelgeuse, MagellanicClouds)
    {
        const std::array<StarfieldPresetIdentifier, 4u> Presets {{
            StarfieldPresetIdentifier::ZodiacalLightDusk,
            StarfieldPresetIdentifier::SouthernCrossCoalsack,
            StarfieldPresetIdentifier::WinterHunterBetelgeuse,
            StarfieldPresetIdentifier::MagellanicClouds,
        }};
        const std::array<const char*, 4u> FileNames {{
            "10-ZodiacalLightDusk.png",
            "11-SouthernCrossCoalsack.png",
            "12-WinterHunterBetelgeuse.png",
            "13-MagellanicClouds.png",
        }};

        for (std::size_t I = 0u; I < Presets.size(); ++I)
        {
            const StarfieldPresetSpecification Spec = LunarStarfieldSequence::BuildStarfieldPreset(Presets[I]);
            WorldSceneSpecification StarScene {};
            {
                MaterialSpace::SurfaceMaterial DummyMat {};
                (void)StarScene.MaterialRegister.RegisterMaterial(DummyMat);
            }
            StarScene.Atmosphere.Medium.AtmosphereEnabled  = true;
            StarScene.Atmosphere.Solar.SolarEnabled        = true;
            StarScene.Atmosphere.Solar.ManualDirectionOverride = true;
            StarScene.Atmosphere.Solar.ManualAzimuthDegrees   = Spec.SunAzimuthDegrees;
            StarScene.Atmosphere.Solar.ManualElevationDegrees = Spec.SunElevationDegrees;
            StarScene.Atmosphere.Clouds.CloudsEnabled      = false;
            StarScene.Atmosphere.LocalCloud.LocalEnabled   = false;
            StarScene.Atmosphere.Stars                     = Spec.Stars;
            for (auto& M : StarScene.Atmosphere.Moons)
            {
                M.Visible = false;
            }

            const uint32_t PresetW = 960u;
            const uint32_t PresetH = 540u;
            const auto Fwd = CelestialEphemerisSolver::ConvertAzimuthElevationToWorldAxis(
                Spec.CameraAzimuthDegrees, Spec.CameraElevationDegrees);
            const float TanHalfHoriz = std::tan(0.5f * Spec.HorizontalFovDegrees * Constants::DegToRadFloat);
            const float Aspect       = static_cast<float>(PresetW) / static_cast<float>(PresetH);
            const float VerticalFov  = 2.0f * std::atan(TanHalfHoriz / Aspect);

            PerspectiveCameraFrame StarCam {};
            StarCam.CameraOrigin       = { 0.0f, 0.0f, 1.5f };
            StarCam.TargetPoint        = { Fwd.X * 12.0f, Fwd.Y * 12.0f, 1.5f + Fwd.Z * 12.0f };
            StarCam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
            StarCam.VerticalFovRadians = VerticalFov;
            StarCam.ViewportWidthPx    = PresetW;
            StarCam.ViewportHeightPx   = PresetH;

            WorldRenderConfiguration StarCfg {};
            StarCfg.Engine = WorldRenderEngine::OpenPbrDirect;

            CpuRenderer.PrepareWorldScene(StarScene);
            CpuRenderer.SynchronizeWorldAtmosphere(StarScene);
            if (GpuOk)
            {
                GpuRenderer.SynchronizeSceneResources(StarScene, &CpuRenderer.InspectIrradianceLattice(), true);
            }

            std::vector<uint8_t> StarRgba;
            if (!GpuOk || !GpuRenderer.RenderWorldViewportWithEngine(StarCfg, 0u, StarCam, StarScene, StarRgba))
            {
                StarRgba = CpuRenderer.RenderWorldFrameWithEngine(StarCfg, StarCam, StarCam, StarScene).DisplayRgba8;
            }

            const std::filesystem::path PresetOut = OutDir / FileNames[I];
            Score.Verify(WritePngRgba8(PresetOut, PresetW, PresetH, StarRgba.data()),
                         FileNames[I]);
        }
    }

    if (GpuOk)
    {
        GpuRenderer.TeardownSequence();
    }
    if (VulkanOk)
    {
        VulkanDevice.TeardownInterchange();
    }

    std::printf("[CelestialCloudsValidation] Passed=%u Failed=%u\n", Score.Passed, Score.Failed);
    return (Score.Failed == 0u) ? 0 : 1;
}
