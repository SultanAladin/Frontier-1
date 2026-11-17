/*==========================================================================================================================================
                                                   CELESTIALCLOUDSVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validation & Visual Proofs for Sun Ephemeris, Bruneton/Hillaire SkyView LUT, 4x Moons, Starfield & Raymarched Volumetric Clouds

#include "Engine/Codecs/ImageCodec.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Editor/Inspectors/CloudsInspectorView.h"
#include "Engine/Editor/Inspectors/MoonInspectorView.h"
#include "Engine/Editor/Inspectors/StarsInspectorView.h"
#include "Engine/Editor/Inspectors/SunInspectorView.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

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
    // 3. Lunar Atlas .ImageSpace Roundtrip & Multi-Moon / Starfield Evaluation
    // -----------------------------------------------------------------------------------------------------------------
    {
        const auto AtlasRgba = LunarStarfieldSequence::BuildProceduralLunarSurfaceAtlasRgba8();
        Score.Verify(AtlasRgba.size() == LunarStarfieldSequence::AtlasTileWidthPx *
                                             LunarStarfieldSequence::AtlasTileHeightPx *
                                             LunarStarfieldSequence::AtlasPresetCount * 4u,
                     "Procedural 6-preset Lunar Atlas dimensions match 256x768 RGBA8");

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
                         Decoded.NominalWidthPx == 256u,
                     "Decoded EngineContent/Skies/LunarSurfaceAtlas.ImageSpace with 6 preset layers");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // 4. Volumetric Clouds: 6 Presets, Local Volume & Solar Cloud Shadow Transmittance
    // -----------------------------------------------------------------------------------------------------------------
    {
        CloudDeckConfiguration Deck {};
        VolumetricCloudSequence::ApplyCloudCategoryPreset(Deck, CloudCategoryType::Cumulonimbus);
        Score.Verify(Deck.LayerThicknessMeters > 4000.0f && Deck.AnvilTopSpread > 0.8f,
                     "Cumulonimbus preset applies tall vertical anvil cloud thickness");

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
    const bool GpuOk = VulkanOk && GpuRenderer.InitializeSequence(VulkanDevice, RepoRoot / "Build/CompiledShaders");
    Score.Verify(GpuOk, "VulkanWorldRenderSequence initialized with SkyViewTransmittanceCompute + WorldViewportRadiometricCompute");

    OutdoorGeometryAsset FloorBox {}, PedestalBox {}, GoldSphere {}, GlassSphere {};
    WorldSceneSpecification Scene = BuildOutdoorShowcaseScene(FloorBox, PedestalBox, GoldSphere, GlassSphere);
    TieredWorldRenderSequence CpuRenderer {};

    const uint32_t TileW = 280u;
    const uint32_t TileH = 180u;
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
            Scene.Atmosphere.Clouds.CloudFraction       = (Idx == 3u) ? 0.15f : 0.34f;
            Scene.Atmosphere.Moons[0].Visible           = true;
            Scene.Atmosphere.Moons[0].AzimuthDegrees    = 280.0f;
            Scene.Atmosphere.Moons[0].ElevationDegrees  = 22.0f;
            Scene.Atmosphere.Moons[0].AngularDiameterDegrees = 6.5f;
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

    // Exhibit 02: 6 Cloud Categorys + Bounded LocalCloudVolume
    {
        const std::array<CloudCategoryType, 6u> Types {{
            CloudCategoryType::Stratus,
            CloudCategoryType::Stratocumulus,
            CloudCategoryType::Cumulus,
            CloudCategoryType::Cumulonimbus,
            CloudCategoryType::Altostratus,
            CloudCategoryType::Cirrus,
        }};
        std::vector<uint8_t> Sheet(static_cast<std::size_t>(TileW * 3u) * (TileH * 2u) * 4u, 255u);

        PerspectiveCameraFrame SkyCam = Cam;
        SkyCam.CameraOrigin = { 0.0f, -4.5f, 1.4f };
        SkyCam.TargetPoint  = { 0.0f,  5.0f, 3.8f };

        for (uint32_t Idx = 0u; Idx < 6u; ++Idx)
        {
            Scene.Atmosphere.Solar.LocalHours     = 10.5f;
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
            BlitTile(Sheet, TileW * 3u, (Idx % 3u) * TileW, (Idx / 3u) * TileH, TileRgba.data(), TileW, TileH);
        }

        const std::filesystem::path P2 = OutDir / "02-SixCloudTypesAndLocalCloud.png";
        Score.Verify(WritePngRgba8(P2, TileW * 3u, TileH * 2u, Sheet.data()),
                     "Exported 02-SixCloudTypesAndLocalCloud.png");
    }

    // Exhibit 03: Multi-Moon (4 Simultaneous Moons) + Starfield + 6-Preset Lunar Atlas Strip
    {
        Scene.Atmosphere.LocalCloud.LocalEnabled = false;
        Scene.Atmosphere.Clouds.CloudsEnabled    = false;
        Scene.Atmosphere.Solar.LocalHours        = 23.5f;
        Scene.Atmosphere.Stars.StarfieldEnabled  = true;
        Scene.Atmosphere.Stars.BrightnessMultiplier = 1.8f;
        Scene.Atmosphere.Stars.PointSizeScale    = 1.7f;

        Scene.Atmosphere.Moons[0] = { true, false, LunarSurfacePreset::Luna,    0.65f, 2.4f, 0.9f, 9.0f, 250.0f, 22.0f, 0.0f, 0.0f };
        Scene.Atmosphere.Moons[1] = { true, false, LunarSurfacePreset::Ember,   0.38f, 2.2f, 1.1f, 7.5f, 263.0f, 26.0f, 18.0f, -10.0f };
        Scene.Atmosphere.Moons[2] = { true, false, LunarSurfacePreset::Glacier, 0.78f, 2.0f, 0.7f, 6.5f, 276.0f, 21.0f, -15.0f, 12.0f };
        Scene.Atmosphere.Moons[3] = { true, false, LunarSurfacePreset::Sulfur,  0.50f, 2.3f, 0.8f, 7.0f, 289.0f, 25.0f, 28.0f, 0.0f };

        PerspectiveCameraFrame NightCam = Cam;
        NightCam.CameraOrigin = { 0.0f, -4.5f, 1.2f };
        NightCam.TargetPoint  = { 0.0f,  5.0f, 3.6f };
        NightCam.ViewportWidthPx  = 640u;
        NightCam.ViewportHeightPx = 360u;

        CpuRenderer.SynchronizeWorldAtmosphere(Scene);
        std::vector<uint8_t> NightRgba;
        WorldRenderConfiguration Cfg {};
        Cfg.Engine = WorldRenderEngine::OpenPbrDirect;
        if (!GpuOk || !GpuRenderer.RenderWorldViewportWithEngine(Cfg, 0u, NightCam, Scene, NightRgba))
        {
            NightRgba = CpuRenderer.RenderWorldFrameWithEngine(Cfg, NightCam, NightCam, Scene).DisplayRgba8;
        }
        const std::filesystem::path P3 = OutDir / "03-MultiMoonAtlasAndPhasePresets.png";
        Score.Verify(WritePngRgba8(P3, 640u, 360u, NightRgba.data()),
                     "Exported 03-MultiMoonAtlasAndPhasePresets.png");
    }

    // Exhibit 04: Outdoor World Architecture Lit by Physical Sky + Sun + Cloud Shadows (CPU vs GPU Parity)
    {
        Scene.Atmosphere.Solar.LocalHours     = 15.6f;
        Scene.Atmosphere.Clouds.CloudsEnabled = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(Scene.Atmosphere.Clouds, CloudCategoryType::Cumulus);
        Scene.Atmosphere.Moons[0] = { true, false, LunarSurfacePreset::Luna, 0.62f, 1.8f, 0.8f, 5.0f, 28.0f, 22.0f, 0.0f, 0.0f };
        Scene.Atmosphere.Moons[1].Visible = false;
        Scene.Atmosphere.Moons[2].Visible = false;
        Scene.Atmosphere.Moons[3].Visible = false;

        PerspectiveCameraFrame HeroCam = Cam;
        HeroCam.ViewportWidthPx  = 480u;
        HeroCam.ViewportHeightPx = 300u;

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

        std::vector<uint8_t> SideBySide(static_cast<std::size_t>(480u * 2u) * 300u * 4u, 255u);
        BlitTile(SideBySide, 960u,   0u, 0u, CpuOut.DisplayRgba8.data(), 480u, 300u);
        BlitTile(SideBySide, 960u, 480u, 0u, GpuOut.data(),              480u, 300u);
        const std::filesystem::path P4 = OutDir / "04-OutdoorWorldWithAtmosphereGpu.png";
        Score.Verify(WritePngRgba8(P4, 960u, 300u, SideBySide.data()),
                     "Exported 04-OutdoorWorldWithAtmosphereGpu.png");
    }

    if (GpuOk)
    {
        GpuRenderer.TeardownSequence();
    }
    if (VulkanOk)
    {
        VulkanDevice.TeardownInterchange();
    }

    //================================================================================================================
    // 5. TURN 3 NATIVE C++20 CELESTIAL & CLOUDS INSPECTOR VIEWS + 3-PANE WORLD EDITOR PROOF
    //================================================================================================================
    {
        ImGui::CreateContext();
        ImGuiIO& Io = ImGui::GetIO();
        Io.IniFilename = nullptr;

        TypographyRepository EditorFonts {};
        const std::filesystem::path FontRoot =
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent" / "FontsArchives";
        if (std::filesystem::exists(FontRoot / "FontRepository.FontSpace") &&
            EditorFonts.LoadRepository(FontRoot) > 0u)
        {
            TypographyRepository::AssignCurrent(&EditorFonts);
            if (ImFont* DefaultUiFont = EditorFonts.QueryTabStripFont())
            {
                Io.FontDefault = DefaultUiFont;
            }
        }
        unsigned char* FontPixels = nullptr;
        int            FontW      = 0;
        int            FontH      = 0;
        Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

        Editor::Inspectors::SunInspectorView    SunView {};
        Editor::Inspectors::MoonInspectorView   MoonView {};
        Editor::Inspectors::StarsInspectorView  StarsView {};
        Editor::Inspectors::CloudsInspectorView CloudsView {};
        Editor::Inspectors::CloudsInspectorView LocalCloudView {};

        WorldAtmosphereConfiguration InspAtmo {};
        InspAtmo.Medium.AtmosphereEnabled = true;
        InspAtmo.Solar.LocalHours         = 15.50f;
        InspAtmo.LocalCloud.LocalEnabled  = true;
        uint32_t ActiveMoonSlot           = 0u;

        constexpr float PanelW = 380.0f;
        constexpr float PanelH = 980.0f;
        constexpr uint32_t SheetW = static_cast<uint32_t>(PanelW * 5.0f + 8.0f);
        constexpr uint32_t SheetH = static_cast<uint32_t>(PanelH);
        Io.DisplaySize = ImVec2(static_cast<float>(SheetW), static_cast<float>(SheetH));

        // Test live interactive click on Sun "Enabled" tile (left half of capability tile row)
        PaneInputReading ClickSunToggle {};
        ClickSunToggle.PrimaryPointer = { 90.0f, 108.0f, true, true, true, false };
        ImGui::NewFrame();
        PixelSpace HitTestSurface(ImGui::GetBackgroundDrawList(), { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        const bool SolarToggled = SunView.Render(
            HitTestSurface,
            { 0.0f, 0.0f, PanelW, PanelH },
            { &InspAtmo.Solar, &InspAtmo.Medium },
            ClickSunToggle,
            1.0 / 60.0);
        ImGui::EndFrame();
        Score.Verify(SolarToggled && !InspAtmo.Solar.SolarEnabled,
                     "SunInspectorView capability tile click toggles SolarEnabled cleanly");
        InspAtmo.Solar.SolarEnabled = true;

        // Render all 5 Native C++ Inspector Views side-by-side
        ImGui::NewFrame();
        PixelSpace SheetSurface(ImGui::GetBackgroundDrawList(), { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        SheetSurface.FillRectangle({ 0.0f, 0.0f, static_cast<float>(SheetW), static_cast<float>(SheetH) }, ColorRgba{ 38, 38, 38, 255 });

        PaneInputReading IdleInput {};
        IdleInput.PrimaryPointer = { -1000.0f, -1000.0f, false, false, false, false };

        (void)SunView.Render(SheetSurface,        { 0.0f * (PanelW + 2.0f), 0.0f, PanelW, PanelH }, { &InspAtmo.Solar, &InspAtmo.Medium }, IdleInput, 1.0 / 60.0);
        (void)MoonView.Render(SheetSurface,       { 1.0f * (PanelW + 2.0f), 0.0f, PanelW, PanelH }, { &InspAtmo.Moons, &ActiveMoonSlot }, IdleInput, 1.0 / 60.0);
        (void)StarsView.Render(SheetSurface,      { 2.0f * (PanelW + 2.0f), 0.0f, PanelW, PanelH }, { &InspAtmo.Stars, 1.25f }, IdleInput, 1.0 / 60.0);
        (void)CloudsView.Render(SheetSurface,     { 3.0f * (PanelW + 2.0f), 0.0f, PanelW, PanelH }, { &InspAtmo.Clouds, &InspAtmo.LocalCloud, false }, IdleInput, 1.0 / 60.0);
        (void)LocalCloudView.Render(SheetSurface, { 4.0f * (PanelW + 2.0f), 0.0f, PanelW, PanelH }, { &InspAtmo.Clouds, &InspAtmo.LocalCloud, true }, IdleInput, 1.0 / 60.0);
        ImGui::Render();

        Exhibits::SoftwareRasterCanvas InspCanvas(SheetW, SheetH, { 18u, 18u, 18u, 255u });
        InspCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        const std::filesystem::path P5 = OutDir / "05-CelestialInspectorViewsProof.png";
        Score.Verify(InspCanvas.WritePngFile(P5) && std::filesystem::exists(P5),
                     "Exported 05-CelestialInspectorViewsProof.png (Sun, Moon, Stars, Volumetric Clouds, Local Cloud)");

        // 3-Pane World Editor Integration Proof (Outliner + Atmospheric Sky Viewport + Sun Inspector)
        ShowcaseWorldSceneBundle EditorBundle {};
        BuildShowcaseGalleryScene(EditorBundle, 640u, 360u, true);
        EditorBundle.Scene.Atmosphere.Medium.AtmosphereEnabled = true;
        EditorBundle.Scene.Atmosphere.Solar.LocalHours         = 15.8f;
        EditorBundle.Scene.Atmosphere.Clouds.CloudsEnabled     = true;
        // Open back wall & roof baffle in outdoor proof so atmospheric sky + cumulus clouds frame the gallery specimens
        if (EditorBundle.Scene.ClusterInstances.size() > 4u)
        {
            EditorBundle.Scene.ClusterInstances[1u].UniformScale = 0.0f;
            EditorBundle.Scene.ClusterInstances[4u].UniformScale = 0.0f;
        }
        RebuildShowcaseSceneAcceleration(EditorBundle, true);
        CpuRenderer.PrepareWorldScene(EditorBundle.Scene);
        CpuRenderer.SynchronizeWorldAtmosphere(EditorBundle.Scene);

        const auto SkyRaster = CpuRenderer.RenderWorldFrameWithEngine(
            { WorldRenderEngine::OpenPbrDirect },
            EditorBundle.EditorCamera,
            EditorBundle.EditorCamera,
            EditorBundle.Scene);

        OutlinerPanel  EditorOutliner {};
        ViewportPanel  EditorViewport {};
        InspectorPanel EditorInspector {};
        EditorViewport.AttachOutliner(&EditorOutliner);
        EditorInspector.AttachOutliner(&EditorOutliner);
        EditorViewport.ConfigureSceneTitle("Outdoor Atmosphere Showcase");
        EditorViewport.AssignCanvasRasterImage(SkyRaster.DisplayRgba8.data(), SkyRaster.WidthPx, SkyRaster.HeightPx);
        RegisterShowcaseGalleryInOutliner(EditorOutliner, EditorBundle);
        EditorOutliner.SelectSingle(EditorBundle.SunEmitterToken);
        EditorInspector.ConfigureFullOverrideCallback(
            [&](PixelSpace& Surf, Rectangle2D Rect, OutlinerPanel*, OutlinerRecordEntry* Primary, const PaneInputReading& Inp, double Dt) -> bool
            {
                if (Primary != nullptr && Primary->Token == EditorBundle.SunEmitterToken)
                {
                    (void)SunView.Render(Surf, Rect, { &EditorBundle.Scene.Atmosphere.Solar, &EditorBundle.Scene.Atmosphere.Medium }, Inp, Dt);
                    return true;
                }
                return false;
            });

        constexpr uint32_t EdW = 1600u;
        constexpr uint32_t EdH = 940u;
        Io.DisplaySize = ImVec2(static_cast<float>(EdW), static_cast<float>(EdH));
        ImGui::NewFrame();
        PixelSpace EdSurface(ImGui::GetBackgroundDrawList(), { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        PixelSpace EdOverlay(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        EdSurface.FillRectangle({ 0.0f, 0.0f, static_cast<float>(EdW), static_cast<float>(EdH) }, WorkspaceSurfaceColors::RootGap);
        EditorOutliner.Render(EdSurface, { 0.0f, 0.0f, 336.0f, static_cast<float>(EdH) }, IdleInput, 1.0 / 60.0, &EdOverlay);
        EditorViewport.Render(EdSurface, { 338.0f, 0.0f, 924.0f, static_cast<float>(EdH) }, IdleInput, 1.0 / 60.0, &EdOverlay);
        EditorInspector.Render(EdSurface, { 1264.0f, 0.0f, 336.0f, static_cast<float>(EdH) }, IdleInput, 1.0 / 60.0);
        ImGui::Render();

        Exhibits::SoftwareRasterCanvas EdCanvas(EdW, EdH, { 18u, 18u, 18u, 255u });
        EdCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        const std::filesystem::path P6 = OutDir / "06-CppWorldEditorWithCelestialInspector.png";
        Score.Verify(EdCanvas.WritePngFile(P6) && std::filesystem::exists(P6),
                     "Exported 06-CppWorldEditorWithCelestialInspector.png");

        TypographyRepository::AssignCurrent(nullptr);
        ImGui::DestroyContext();
    }

    std::printf("[CelestialCloudsValidation] Passed=%u Failed=%u\n", Score.Passed, Score.Failed);
    return (Score.Failed == 0u) ? 0 : 1;
}
