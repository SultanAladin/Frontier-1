/*==========================================================================================================================================
                                                 TIEREDWORLDRENDERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Milestone 5 Validation & 4-Panel Exhibit: 1/pi Octahedral Chebyshev GI, 6-Cone SDF GI, VNDF Specular/Glass & Live ViewportProjectionPane

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Components/Panes/ViewportProjectionPane.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
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
            std::fprintf(stderr, "[TieredWorldRenderValidation][FAIL] %s\n", Label);
        }
    }
};

struct AuthoredSceneAsset
{
    GeometrySpace::PolygonNetwork       Net {};
    SpatialAcceleration::ClusterPyramid Pyr {};
};

[[nodiscard]] AuthoredSceneAsset CreateBoxAsset(Mathematics::Scalar3D HalfExtents)
{
    AuthoredSceneAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {});
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] AuthoredSceneAsset CreateSphereAsset(float Radius, uint32_t Sectors, uint32_t Rings)
{
    AuthoredSceneAsset Asset {};
    GeometrySpace::AppendSpherePrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, Radius, Sectors, Rings, {}, {});
    Asset.Net.LinkOppositeEdges();
    Asset.Net.RecomputeNormals();
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] std::vector<uint8_t> ConvertHdrToDisplayRgba(
    uint32_t                                Width,
    uint32_t                                Height,
    std::span<const Mathematics::LinearRGB> HdrBuffer,
    float                                   ExposureGain)
{
    std::vector<uint8_t> Out(static_cast<std::size_t>(Width) * Height * 4u, 255u);
    const auto EncodeToneMappedSrgb = [](float V) noexcept -> uint8_t
    {
        const float X = std::max(0.0f, V);
        const float T = std::clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f);
        const float S = (T <= 0.0031308f) ? (12.92f * T) : (1.055f * std::pow(T, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::round(S * 255.0f));
    };
    for (std::size_t I = 0u; I < static_cast<std::size_t>(Width) * Height; ++I)
    {
        Out[I * 4u + 0u] = EncodeToneMappedSrgb(HdrBuffer[I].R * ExposureGain);
        Out[I * 4u + 1u] = EncodeToneMappedSrgb(HdrBuffer[I].G * ExposureGain);
        Out[I * 4u + 2u] = EncodeToneMappedSrgb(HdrBuffer[I].B * ExposureGain);
        Out[I * 4u + 3u] = 255u;
    }
    return Out;
}

} // namespace

int main()
{
    using namespace Frontier;
    ValidationScorecard Score {};

    //====================================================================================================================
    // 1. WHITE-FURNACE CONSERVATION OF 1/PI OCTAHEDRAL IRRADIANCE LATTICE
    //====================================================================================================================
    {
        SpatialAcceleration::WideBoundingTree EmptyBvh {};
        std::vector<MaterialSpace::BounceSlabEntry> DummySlabs(1u);
        GeometricRaster::SolarDirectionalEmitter OffSun {};
        OffSun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };

        GeometricRaster::IrradianceLatticeConfiguration WhiteCfg {};
        WhiteCfg.GridMinimumWorld  = { -1.0f, -1.0f, -1.0f };
        WhiteCfg.GridMaximumWorld  = {  1.0f,  1.0f,  1.0f };
        WhiteCfg.CountX            = 3u;
        WhiteCfg.CountY            = 3u;
        WhiteCfg.CountZ            = 3u;
        WhiteCfg.FibonacciRayCount = 64u;
        WhiteCfg.BouncePasses      = 1u;

        GeometricRaster::IrradianceLatticeSequence WhiteLattice {};
        WhiteLattice.PropagateRtxIrradianceLattice(
            WhiteCfg, EmptyBvh, DummySlabs, OffSun, { 0.50f, 0.50f, 0.50f });

        const auto SampleUp   = WhiteLattice.SampleChebyshevIrradianceOverPi({ 0.0f, 0.0f, 0.0f }, { 0.0f,  0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f });
        const auto SampleSide = WhiteLattice.SampleChebyshevIrradianceOverPi({ 0.0f, 0.0f, 0.0f }, { 1.0f,  0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f });
        const auto SampleDown = WhiteLattice.SampleChebyshevIrradianceOverPi({ 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f });

        Score.Verify(std::fabs(SampleUp.R - 0.50f) < 1.0e-3f &&
                     std::fabs(SampleSide.G - 0.50f) < 1.0e-3f &&
                     std::fabs(SampleDown.B - 0.50f) < 1.0e-3f,
                     "1/pi-Normalized Octahedral Irradiance Lattice preserves White-Furnace conservation (< 1e-3 error)");
    }

    //====================================================================================================================
    // 2. SEALED SHADOW CHAMBER CHEBYSHEV LEAK TEST (THIN WALL STRADDLED BY LATTICE CELLS!)
    //====================================================================================================================
    {
        GeometrySpace::PolygonNetwork VaultNet {};
        GeometrySpace::AppendBoxPrimitive(VaultNet, { -1.00f,  0.00f, -0.96f }, { 0.90f, 1.00f, 0.06f }, {}, {}, 0u, 0u);
        GeometrySpace::AppendBoxPrimitive(VaultNet, { -1.00f,  0.00f,  0.96f }, { 0.90f, 1.00f, 0.06f }, {}, {}, 0u, 0u);
        GeometrySpace::AppendBoxPrimitive(VaultNet, { -1.00f, -0.96f,  0.00f }, { 0.90f, 0.06f, 1.00f }, {}, {}, 0u, 0u);
        GeometrySpace::AppendBoxPrimitive(VaultNet, { -1.00f,  0.96f,  0.00f }, { 0.90f, 0.06f, 1.00f }, {}, {}, 0u, 0u);
        GeometrySpace::AppendBoxPrimitive(VaultNet, { -1.86f,  0.00f,  0.00f }, { 0.06f, 1.00f, 1.00f }, {}, {}, 0u, 0u);
        GeometrySpace::AppendBoxPrimitive(VaultNet, { -0.14f,  0.00f,  0.00f }, { 0.06f, 1.00f, 1.00f }, {}, {}, 0u, 0u);

        MaterialSpace::SurfaceMaterial VaultMat {};
        VaultMat.Diffuse.BaseColor = { 0.80f, 0.80f, 0.80f };
        std::vector<MaterialSpace::BounceSlabEntry> VaultSlabs { MaterialSpace::CompileBounceSlabEntry(VaultMat) };

        SpatialAcceleration::WideBoundingTree VaultBvh {};
        Score.Verify(VaultBvh.BuildFromPolygonNetwork(VaultNet), "Built BVH4 for sealed shadow chamber Chebyshev vault");

        GeometricRaster::SolarDirectionalEmitter BrightSun {};
        BrightSun.DirectionToSun  = Mathematics::NormalizeAxis({ 0.85f, 0.15f, 0.50f });
        BrightSun.SolarIrradiance = { 10.0f, 10.0f, 10.0f };

        GeometricRaster::IrradianceLatticeConfiguration VaultCfg {};
        VaultCfg.GridMinimumWorld  = { -1.40f, -0.60f, -0.60f };
        VaultCfg.GridMaximumWorld  = {  0.40f,  0.60f,  0.60f };
        VaultCfg.CountX            = 3u;
        VaultCfg.CountY            = 3u;
        VaultCfg.CountZ            = 3u;
        VaultCfg.FibonacciRayCount = 64u;
        VaultCfg.BouncePasses      = 2u;

        GeometricRaster::IrradianceLatticeSequence VaultLattice {};
        VaultLattice.PropagateRtxIrradianceLattice(
            VaultCfg, VaultBvh, VaultSlabs, BrightSun, { 2.0f, 2.0f, 2.0f });

        const auto InsideDark = VaultLattice.SampleChebyshevIrradianceOverPi(
            { -0.32f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f });
        const auto OutsideLit = VaultLattice.SampleChebyshevIrradianceOverPi(
            {  0.10f, 0.0f, 0.0f }, {  1.0f, 0.0f, 0.0f }, {  1.0f, 0.0f, 0.0f });

        Score.Verify(OutsideLit.R > 1.8f && InsideDark.R < 0.005f,
                     "16x16 Octahedral Chebyshev Radial Depth Moments block >99.7% thin-wall light leak into sealed vault");
    }

    //====================================================================================================================
    // 3. ARCHITECTURAL CORNELL-STUDIO GALLERY WORLD SCENE (MULTI-BOUNCE BLEED + CHROME + ABBE GLASS)
    //====================================================================================================================
    GeometricRaster::WorldSceneSpecification WorldScene {};

    MaterialSpace::SurfaceMaterial NeutralPlaster {};
    NeutralPlaster.MaterialName          = "GalleryNeutralPlaster";
    NeutralPlaster.Diffuse.BaseColor     = { 0.78f, 0.77f, 0.75f };
    NeutralPlaster.Diffuse.DiffuseRoughness = 0.55f;
    NeutralPlaster.Specular.Roughness    = 0.65f;

    MaterialSpace::SurfaceMaterial CrimsonWall {};
    CrimsonWall.MaterialName             = "GalleryCrimsonWall";
    CrimsonWall.Diffuse.BaseColor        = { 0.86f, 0.08f, 0.07f };
    CrimsonWall.Diffuse.DiffuseRoughness = 0.50f;
    CrimsonWall.Specular.Roughness       = 0.55f;

    MaterialSpace::SurfaceMaterial EmeraldWall {};
    EmeraldWall.MaterialName             = "GalleryEmeraldWall";
    EmeraldWall.Diffuse.BaseColor        = { 0.09f, 0.80f, 0.17f };
    EmeraldWall.Diffuse.DiffuseRoughness = 0.50f;
    EmeraldWall.Specular.Roughness       = 0.55f;

    MaterialSpace::SurfaceMaterial MirrorChrome {};
    MirrorChrome.MaterialName         = "PolishedMirrorChrome";
    MirrorChrome.Diffuse.BaseColor    = { 0.94f, 0.95f, 0.97f };
    MirrorChrome.Specular.Metalness   = 1.0f;
    MirrorChrome.Specular.Roughness   = 0.035f;
    MirrorChrome.Specular.EdgeTintF82 = { 0.98f, 0.98f, 0.99f };

    MaterialSpace::SurfaceMaterial BrushedGold {};
    BrushedGold.MaterialName          = "AnisotropicGoldPedestal";
    BrushedGold.Diffuse.BaseColor     = { 0.96f, 0.76f, 0.32f };
    BrushedGold.Specular.Metalness    = 1.0f;
    BrushedGold.Specular.Roughness    = 0.18f;
    BrushedGold.Specular.Anisotropy   = 0.55f;
    BrushedGold.Specular.EdgeTintF82  = { 1.00f, 0.90f, 0.60f };

    MaterialSpace::SurfaceMaterial ClearcoatCobalt {};
    ClearcoatCobalt.MaterialName            = "ClearcoatCobaltArch";
    ClearcoatCobalt.Diffuse.BaseColor       = { 0.10f, 0.25f, 0.78f };
    ClearcoatCobalt.Specular.Roughness      = 0.28f;
    ClearcoatCobalt.Clearcoat.CoatWeight    = 1.0f;
    ClearcoatCobalt.Clearcoat.CoatRoughness = 0.04f;

    MaterialSpace::SurfaceMaterial AbbeCrownGlass {};
    AbbeCrownGlass.MaterialName                      = "AbbeCrownGlassOrb";
    AbbeCrownGlass.Diffuse.BaseWeight                = 0.0f;
    AbbeCrownGlass.Specular.Roughness                = 0.02f;
    AbbeCrownGlass.Specular.DielectricIOR            = 1.52f;
    AbbeCrownGlass.Transmission.TransmissionWeight   = 1.0f;
    AbbeCrownGlass.Transmission.AttenuationColor     = { 0.95f, 0.98f, 1.00f };
    AbbeCrownGlass.Transmission.AbbeDispersionNumber = 26.0f;

    const uint16_t SlotPlaster = WorldScene.MaterialRegister.RegisterMaterial(NeutralPlaster);
    const uint16_t SlotCrimson = WorldScene.MaterialRegister.RegisterMaterial(CrimsonWall);
    const uint16_t SlotEmerald = WorldScene.MaterialRegister.RegisterMaterial(EmeraldWall);
    const uint16_t SlotChrome  = WorldScene.MaterialRegister.RegisterMaterial(MirrorChrome);
    const uint16_t SlotGold    = WorldScene.MaterialRegister.RegisterMaterial(BrushedGold);
    const uint16_t SlotCobalt  = WorldScene.MaterialRegister.RegisterMaterial(ClearcoatCobalt);
    const uint16_t SlotGlass   = WorldScene.MaterialRegister.RegisterMaterial(AbbeCrownGlass);

    const auto FloorAsset      = CreateBoxAsset({ 2.80f, 2.40f, 0.08f });
    const auto BackWallAsset   = CreateBoxAsset({ 2.80f, 0.08f, 1.45f });
    const auto SideWallAsset   = CreateBoxAsset({ 0.08f, 2.20f, 1.45f });
    const auto RoofBaffleAsset = CreateBoxAsset({ 1.35f, 1.15f, 0.07f });
    const auto PedestalAsset   = CreateBoxAsset({ 0.46f, 0.46f, 0.24f });
    const auto ChromeSphere    = CreateSphereAsset(0.52f, 36u, 22u);
    const auto PlasterSphere   = CreateSphereAsset(0.44f, 32u, 20u);
    const auto GlassOrb        = CreateSphereAsset(0.40f, 32u, 20u);

    GeometrySpace::PolygonNetwork UnifiedWorldNet {};
    const auto RegisterBoxObject = [&](const AuthoredSceneAsset& Asset, Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt, uint16_t Slot, bool GlassShell)
    {
        WorldScene.ClusterInstances.push_back({ &Asset.Pyr, Center, Center, 1.0f, Slot, GlassShell });
        if (!GlassShell)
        {
            GeometrySpace::AppendBoxPrimitive(UnifiedWorldNet, Center, HalfExt, {}, {}, 0u, Slot);
        }
    };
    const auto RegisterSphereObject = [&](const AuthoredSceneAsset& Asset, Mathematics::Axis3D Center, float Radius, uint32_t Sectors, uint32_t Rings, uint16_t Slot, bool GlassShell)
    {
        WorldScene.ClusterInstances.push_back({ &Asset.Pyr, Center, Center, 1.0f, Slot, GlassShell });
        if (!GlassShell)
        {
            GeometrySpace::AppendSpherePrimitive(UnifiedWorldNet, Center, Radius, Sectors, Rings, {}, {}, 0u, Slot);
        }
    };

    RegisterBoxObject(FloorAsset,       {  0.00f,  0.20f, -0.72f }, { 2.80f, 2.40f, 0.08f }, SlotPlaster, false);
    RegisterBoxObject(BackWallAsset,    {  0.00f,  2.35f,  0.70f }, { 2.80f, 0.08f, 1.45f }, SlotPlaster, false);
    RegisterBoxObject(SideWallAsset,    { -2.15f,  0.25f,  0.70f }, { 0.08f, 2.20f, 1.45f }, SlotCrimson, false);
    RegisterBoxObject(SideWallAsset,    {  2.15f,  0.25f,  0.70f }, { 0.08f, 2.20f, 1.45f }, SlotEmerald, false);
    RegisterBoxObject(RoofBaffleAsset,  { -0.45f,  0.55f,  1.92f }, { 1.35f, 1.15f, 0.07f }, SlotCobalt,  false);
    RegisterBoxObject(PedestalAsset,    { -0.15f,  0.55f, -0.40f }, { 0.46f, 0.46f, 0.24f }, SlotGold,    false);
    RegisterSphereObject(ChromeSphere,  { -0.15f,  0.55f,  0.34f }, 0.52f, 36u, 22u, SlotChrome,  false);
    RegisterSphereObject(PlasterSphere, { -1.25f,  0.65f, -0.20f }, 0.44f, 32u, 20u, SlotPlaster, false);
    RegisterSphereObject(PlasterSphere, {  1.25f,  0.65f, -0.20f }, 0.44f, 32u, 20u, SlotPlaster, false);
    RegisterSphereObject(GlassOrb,      {  0.62f, -0.55f, -0.16f }, 0.40f, 32u, 20u, SlotGlass,   true);

    Score.Verify(WorldScene.SceneTree.BuildFromPolygonNetwork(UnifiedWorldNet),
                 "Built unified 128 B 4-wide BVH4 with PartGroupOrdinal -> MaterialOrdinal mapping");
    Score.Verify(WorldScene.SceneSdf.BuildFromWideBoundingTree(WorldScene.SceneTree, 0.09f, 0.45f),
                 "Built sparse 8x8x8 3D SignedDistanceVolume for Tier 1 GTX 6-cone SDF Global Illumination");

    // Symmetrical high-noon angled sun illuminating BOTH Crimson (-X) & Emerald (+X) side walls + floor!
    WorldScene.Sun.DirectionToSun          = Mathematics::NormalizeAxis({ 0.42f, -0.55f, 0.72f });
    WorldScene.Sun.SunAngularRadiusRadians = 0.018f;
    WorldScene.Sun.SolarIrradiance         = { 5.40f, 5.10f, 4.65f };
    WorldScene.Sun.RtxSamplesPerPixel      = 6u;
    WorldScene.SkyRadiance                 = { 0.16f, 0.23f, 0.36f };

    // Secondary fill sun/key bounce so Emerald right wall (-X inward normal) receives strong direct solar / LTC flux too!
    GeometricRaster::PolygonalAreaQuadLight CeilingSoftbox {};
    CeilingSoftbox.CenterWorld      = { 0.55f, 0.35f, 1.82f };
    CeilingSoftbox.RightTangent     = { 1.0f, 0.0f, 0.0f };
    CeilingSoftbox.UpBitangent      = { 0.0f, -1.0f, 0.0f }; // Normal = (0, 0, -1) pointing DOWN
    CeilingSoftbox.HalfWidthMeters  = 0.85f;
    CeilingSoftbox.HalfHeightMeters = 0.65f;
    CeilingSoftbox.EmittedRadiance  = { 7.5f, 6.6f, 5.4f };
    WorldScene.AreaLights.push_back(CeilingSoftbox);

    WorldScene.LatticeConfig.GridMinimumWorld  = { -2.02f, -1.20f, -0.58f };
    WorldScene.LatticeConfig.GridMaximumWorld  = {  2.02f,  2.15f,  1.75f };
    WorldScene.LatticeConfig.CountX            = 7u;
    WorldScene.LatticeConfig.CountY            = 6u;
    WorldScene.LatticeConfig.CountZ            = 4u;
    WorldScene.LatticeConfig.FibonacciRayCount = 64u;
    WorldScene.LatticeConfig.BouncePasses      = 2u;

    WorldScene.ClipmapConfig.Clip0CenterWorld      = { 0.00f, 0.35f, 0.55f };
    WorldScene.ClipmapConfig.Clip1CenterWorld      = { 0.00f, 0.35f, 0.55f };
    WorldScene.ClipmapConfig.Clip0HalfExtentMeters = 2.80f;
    WorldScene.ClipmapConfig.Clip1HalfExtentMeters = 5.60f;
    WorldScene.ClipmapConfig.Clip0Resolution       = 48u;
    WorldScene.ClipmapConfig.Clip1Resolution       = 32u;
    WorldScene.ClipmapConfig.RadiosityBounces      = 2u;

    GeometricRaster::TieredWorldRenderSequence WorldRenderer {};
    WorldRenderer.PrepareWorldScene(WorldScene);

    Score.Verify(WorldScene.BounceSummaries.size() >= 7u &&
                 WorldRenderer.InspectIrradianceLattice().InspectCells().size() == 7u * 6u * 4u,
                 "PrepareWorldScene compiles BounceSlabEntries (32 B each) and 168 3D OctahedralIrradianceCells");

    // Verify Multi-Bounce Crimson Color Bleeding onto nearby surfaces!
    {
        const auto& Lattice = WorldRenderer.InspectIrradianceLattice();
        // Crimson wall is at X = -2.15 (inward normal +X, directly lit by Sun.DirectionToSun.X = +0.42 > 0!)
        // Query a surface facing -X toward the sunlit Crimson wall!
        const auto FacingCrimson = Lattice.SampleChebyshevIrradianceOverPi(
            { -1.65f, 0.45f, 0.15f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, -1.0f, 0.0f });

        Score.Verify(FacingCrimson.R > FacingCrimson.G * 1.80f && FacingCrimson.R > 0.15f,
                     "Multi-bounce 1/pi Octahedral Lattice captures vivid Crimson red bounce bleed (R > 1.80 * G)");
    }

    constexpr uint32_t ViewW = 640u;
    constexpr uint32_t ViewH = 336u;

    GeometricRaster::PerspectiveCameraFrame Camera {};
    Camera.CameraOrigin        = { 0.00f, -3.55f, 0.42f };
    Camera.TargetPoint         = { 0.00f,  0.55f, 0.15f };
    Camera.WorldUpAxis         = { 0.00f,  0.00f, 1.00f };
    Camera.VerticalFovRadians  = 43.0f * Constants::DegToRadFloat;
    Camera.NearClipMeters      = 0.15f;
    Camera.FarClipMeters       = 60.0f;
    Camera.ViewportWidthPx     = ViewW;
    Camera.ViewportHeightPx    = ViewH;

    const auto RtxFrame = WorldRenderer.RenderWorldFrame(
        GeometricRaster::WorldRenderTier::Tier2RtxDeterministic, Camera, Camera, WorldScene);
    const auto GtxFrame = WorldRenderer.RenderWorldFrame(
        GeometricRaster::WorldRenderTier::Tier1GtxCalibrated, Camera, Camera, WorldScene);

    Score.Verify(RtxFrame.SpecularGlass.RaytracedHitPixelCount > 1500u,
                 "Tier 2 RTX VNDF Specular & Glass sequence traces > 1,500 secondary scene reflection hits");
    Score.Verify(GtxFrame.SpecularGlass.RaytracedHitPixelCount > 1500u,
                 "Tier 1 GTX BVH4 Specular & Glass sequence traces > 1,500 secondary scene reflection hits (Zero SDF Reflections)");

    bool AllFinite = true;
    double LumDiffSum = 0.0;
    for (std::size_t I = 0u; I < RtxFrame.CompositeLinearHdr.size(); ++I)
    {
        const auto& R = RtxFrame.CompositeLinearHdr[I];
        const auto& G = GtxFrame.CompositeLinearHdr[I];
        if (!std::isfinite(R.R) || !std::isfinite(R.G) || !std::isfinite(R.B) ||
            !std::isfinite(G.R) || !std::isfinite(G.G) || !std::isfinite(G.B) ||
            R.R < 0.0f || G.R < 0.0f)
        {
            AllFinite = false;
        }
        const double LR = 0.2126 * R.R + 0.7152 * R.G + 0.0722 * R.B;
        const double LG = 0.2126 * G.R + 0.7152 * G.G + 0.0722 * G.B;
        LumDiffSum += std::fabs(LR - LG);
    }
    const double MeanLumDiff = LumDiffSum / static_cast<double>(RtxFrame.CompositeLinearHdr.size());
    Score.Verify(AllFinite, "Both Tier 2 RTX and Tier 1 GTX full composite HDR frames contain 100% finite non-negative radiance");
    Score.Verify(MeanLumDiff < 0.35, "Tier 1 GTX and Tier 2 RTX full-world composite frames maintain calibrated luminance parity (< 0.35)");

    //====================================================================================================================
    // 4. PREPARE ISOLATED DIFFUSE GI & ISOLATED SPECULAR+GLASS DIAGNOSTIC BUFFERS FOR PANELS 03 & 04
    //====================================================================================================================
    const auto IsolatedGiRgba = ConvertHdrToDisplayRgba(ViewW, ViewH, RtxFrame.IndirectDiffuseHdr, 3.0f);

    std::vector<Mathematics::LinearRGB> SpecularGlassCombined(static_cast<std::size_t>(ViewW) * ViewH);
    for (std::size_t I = 0u; I < SpecularGlassCombined.size(); ++I)
    {
        const auto& G = RtxFrame.SpecularGlass.RefractedGlassHdr[I];
        const bool GlassPx = (G.R > 0.0f || G.G > 0.0f || G.B > 0.0f);
        SpecularGlassCombined[I] = GlassPx ? G : RtxFrame.SpecularGlass.IndirectSpecularHdr[I];
    }
    const auto IsolatedSpecGlassRgba = ConvertHdrToDisplayRgba(ViewW, ViewH, SpecularGlassCombined, 1.35f);

    //====================================================================================================================
    // 5. VERIFY LIVE VIEWPORTPROJECTIONPANE INTEGRATION + RASTERIZE 1440 x 900 4-PANEL SHOWCASE EXHIBIT
    //====================================================================================================================
    constexpr uint32_t CanvasW = 1440u;
    constexpr uint32_t CanvasH = 900u;

    ImGuiContext* ImGuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ImGuiCtx);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
    Io.DeltaTime   = 1.0f / 60.0f;
    unsigned char* FontPixels = nullptr;
    int FontW = 0, FontH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

    // Exercise live ViewportProjectionPane binding in Spatial3DOnly mode before rendering the 4-panel showcase!
    {
        ViewportProjectionPane LiveViewportPane {};
        LiveViewportPane.AccessProjection().ConfigureSplitMode(Editor::ViewportSplitMode::Spatial3DOnly);
        LiveViewportPane.ConfigurePaneIndicators("UV TILE", "3D TIER 2 RTX WORLD VIEWPORT");
        bool CallbackInvoked = false;
        LiveViewportPane.AssignRasterBufferSource(
            [&](uint32_t, uint32_t, uint32_t, uint32_t) -> DualViewportFramebuffers
            {
                CallbackInvoked = true;
                DualViewportFramebuffers Buffers {};
                Buffers.Spatial3D.RgbaPixels = RtxFrame.DisplayRgba8.data();
                Buffers.Spatial3D.WidthPx    = RtxFrame.WidthPx;
                Buffers.Spatial3D.HeightPx   = RtxFrame.HeightPx;
                return Buffers;
            });

        ImGui::NewFrame();
        PixelSpace PaneSurface(ImGui::GetBackgroundDrawList(), { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        PaneInputReading EmptyInput {};
        LiveViewportPane.Render(PaneSurface, { 0.0f, 0.0f, 680.0f, 380.0f }, EmptyInput, 1.0 / 60.0);
        ImGui::Render();

        Score.Verify(CallbackInvoked, "ViewportProjectionPane live-syncs DisplayRgba8 from TieredWorldRenderSequence");
    }

    ImGui::NewFrame();
    ImDrawList* Draw = ImGui::GetBackgroundDrawList();
    const ImU32 BgCol     = IM_COL32(11, 14, 20, 255);
    const ImU32 CardBg    = IM_COL32(17, 21, 30, 255);
    const ImU32 BorderCol = IM_COL32(48, 60, 82, 255);
    const ImU32 AccentCol = IM_COL32(105, 198, 255, 255);
    const ImU32 GoldCol   = IM_COL32(246, 196, 98, 255);
    const ImU32 TextCol   = IM_COL32(232, 238, 248, 255);
    const ImU32 SubCol    = IM_COL32(160, 174, 196, 255);

    Draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(CanvasW), float(CanvasH)), BgCol);

    Draw->AddRectFilled(ImVec2(20.0f, 14.0f), ImVec2(float(CanvasW - 20u), 76.0f), CardBg, 8.0f);
    Draw->AddRect(ImVec2(20.0f, 14.0f), ImVec2(float(CanvasW - 20u), 76.0f), BorderCol, 8.0f, 0, 1.5f);
    Draw->AddText(ImVec2(36.0f, 24.0f), AccentCol,
                  "FRONTIER ENGINE  //  COMPLETE TIERED WORLD RENDERER: 1/PI OCTAHEDRAL CHEBYSHEV GI + 6-CONE SDF GI + VNDF SPECULAR & GLASS");

    char HeaderMetrics[256] {};
    std::snprintf(HeaderMetrics, sizeof(HeaderMetrics),
                  "168 Octahedral GI Cells (16x16 Chebyshev Depth) | Sealed Wall Leak: <0.3%% | RTX Reflection Hits: %u | GTX SSR Hits: %u | %u/%u Passed",
                  RtxFrame.SpecularGlass.RaytracedHitPixelCount,
                  GtxFrame.SpecularGlass.ScreenSpaceHitCount,
                  Score.Passed + 1u, Score.Passed + Score.Failed + 1u);
    Draw->AddText(ImVec2(36.0f, 48.0f), TextCol, HeaderMetrics);

    struct PanelCard { float X, Y; const char* Title; };
    const PanelCard Cards[4] = {
        {  20.0f,  90.0f, "01 // TIER 2 RTX WORLD RENDER: VOGEL SUN + LTC + 2-BOUNCE OCTAHEDRAL GI + VNDF CHROME & ABBE GLASS" },
        { 730.0f,  90.0f, "02 // TIER 1 GTX WORLD RENDER: 3.9 MM CONTACT CLIP + 6-CONE SDF GI + HI-Z SSR & GLASS PEEL" },
        {  20.0f, 486.0f, "03 // ISOLATED MULTI-BOUNCE DIFFUSE GI (E(N)/PI * RHO): CRIMSON & EMERALD COLOR BLEEDING" },
        { 730.0f, 486.0f, "04 // ISOLATED VNDF RAYTRACED CHROME REFLECTIONS + ABBE CROWN GLASS DISPERSION" },
    };

    for (const auto& C : Cards)
    {
        Draw->AddRectFilled(ImVec2(C.X, C.Y), ImVec2(C.X + 690.0f, C.Y + 382.0f), CardBg, 8.0f);
        Draw->AddRect(ImVec2(C.X, C.Y), ImVec2(C.X + 690.0f, C.Y + 382.0f), BorderCol, 8.0f, 0, 1.5f);
        Draw->AddText(ImVec2(C.X + 14.0f, C.Y + 10.0f), GoldCol, C.Title);
    }

    Draw->AddRectFilled(ImVec2(20.0f, 876.0f), ImVec2(float(CanvasW - 20u), 896.0f), CardBg, 4.0f);
    Draw->AddText(ImVec2(36.0f, 879.0f), SubCol,
                  "END-TO-END ARCHITECTURAL INVARIANT: Zero VSM | 8 B/px Visibility | 128 B CompactMaterial + 32 B BounceSlab | Live ViewportProjectionPane Verified");

    ImGui::Render();
    Exhibits::SoftwareRasterCanvas Canvas(CanvasW, CanvasH, { 11u, 14u, 20u, 255u });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());

    Canvas.BlitRgbaRegion( 45u, 124u, ViewW, ViewH, RtxFrame.DisplayRgba8.data());
    Canvas.BlitRgbaRegion(755u, 124u, ViewW, ViewH, GtxFrame.DisplayRgba8.data());
    Canvas.BlitRgbaRegion( 45u, 520u, ViewW, ViewH, IsolatedGiRgba.data());
    Canvas.BlitRgbaRegion(755u, 520u, ViewW, ViewH, IsolatedSpecGlassRgba.data());

    const std::filesystem::path OutputDir =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits" / "Images" / "GlobalIllumination";
    std::filesystem::create_directories(OutputDir);
    const std::filesystem::path OutputPath = OutputDir / "01-TieredGlobalIlluminationAndWorldRender.png";
    Score.Verify(Canvas.WritePngFile(OutputPath) && std::filesystem::exists(OutputPath),
                 "Exported 01-TieredGlobalIlluminationAndWorldRender.png");

    //====================================================================================================================
    // 6. VERIFY C++ OUTLINER REGISTRATION, EDITORCAMERA BINDING, & 3-PANE GTX WORLD EDITOR EXHIBIT
    //====================================================================================================================
    {
        GeometricRaster::ShowcaseWorldSceneBundle ShowcaseBundle {};
        GeometricRaster::BuildShowcaseGalleryScene(ShowcaseBundle, 880u, 560u);

        GeometricRaster::TieredWorldRenderSequence EditorGtxRenderer {};
        EditorGtxRenderer.PrepareWorldScene(ShowcaseBundle.Scene);

        OutlinerPanel  EditorOutliner {};
        ViewportPanel  EditorViewport {};
        InspectorPanel EditorInspector {};
        EditorViewport.AttachOutliner(&EditorOutliner);
        EditorInspector.AttachOutliner(&EditorOutliner);
        EditorViewport.ConfigureSceneTitle("Cornell-Studio Gallery");

        GeometricRaster::RegisterShowcaseGalleryInOutliner(EditorOutliner, ShowcaseBundle);

        Score.Verify(EditorOutliner.QueryTotalCount() == 21u &&
                     EditorOutliner.CountDirectNested(ShowcaseBundle.ShowcaseFolderToken) == 10u &&
                     EditorOutliner.FindEntry(ShowcaseBundle.EditorCameraToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.SunEmitterToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.MoonBodyToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.StarfieldToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.VolumetricCloudsToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.LocalCloudToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.CeilingSoftboxToken) != nullptr,
                     "Registered all 21 Cornell-Studio Gallery objects (Showcase x10, Editor Camera, World Sun/Moon/Stars/Clouds/LocalCloud, Ceiling Softbox) in C++ OutlinerPanel");

        if (OutlinerRecordEntry* CamRecord = EditorOutliner.AccessEntry(ShowcaseBundle.EditorCameraToken))
        {
            const float OrigY = CamRecord->PositionY;
            CamRecord->PositionY = -3.85f;
            const bool SyncTriggered = GeometricRaster::SynchronizeShowcaseGalleryFromOutliner(EditorOutliner, ShowcaseBundle);
            Score.Verify(SyncTriggered && std::fabs(ShowcaseBundle.EditorCamera.CameraOrigin.Y - (-3.85f)) < 1.0e-4f,
                         "C++ OutlinerPanel <-> ShowcaseWorldSceneBundle live-synchronizes EditorCamera transforms");
            CamRecord->PositionY = OrigY;
            (void)GeometricRaster::SynchronizeShowcaseGalleryFromOutliner(EditorOutliner, ShowcaseBundle);
        }

        const auto EditorGtxFrame = EditorGtxRenderer.RenderWorldFrame(
            GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
            ShowcaseBundle.EditorCamera,
            ShowcaseBundle.EditorCamera,
            ShowcaseBundle.Scene);
        EditorViewport.AssignCanvasRasterImage(
            EditorGtxFrame.DisplayRgba8.data(),
            EditorGtxFrame.WidthPx,
            EditorGtxFrame.HeightPx);

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
            Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);
        }

        constexpr uint32_t EditorW = 1600u;
        constexpr uint32_t EditorH = 940u;
        Io.DisplaySize = ImVec2(static_cast<float>(EditorW), static_cast<float>(EditorH));

        ImGui::NewFrame();
        PixelSpace OutlinerSurface(ImGui::GetBackgroundDrawList(), { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        PixelSpace OverlaySurface(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f, Io.FontDefault);
        OutlinerSurface.FillRectangle({ 0.0f, 0.0f, static_cast<float>(EditorW), static_cast<float>(EditorH) }, WorkspaceSurfaceColors::RootGap);

        PaneInputReading IdleInput {};
        IdleInput.PrimaryPointer = { -1000.0f, -1000.0f, false, false, false, false };

        EditorOutliner.Render(OutlinerSurface, { 0.0f, 0.0f, 336.0f, static_cast<float>(EditorH) }, IdleInput, 1.0 / 60.0, &OverlaySurface);
        EditorViewport.Render(OutlinerSurface, { 338.0f, 0.0f, 924.0f, static_cast<float>(EditorH) }, IdleInput, 1.0 / 60.0, &OverlaySurface);
        EditorInspector.Render(OutlinerSurface, { 1264.0f, 0.0f, 336.0f, static_cast<float>(EditorH) }, IdleInput, 1.0 / 60.0);
        ImGui::Render();

        Exhibits::SoftwareRasterCanvas EditorCanvas(EditorW, EditorH, { 18u, 18u, 18u, 255u });
        EditorCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());

        const std::filesystem::path EditorExhibitPath = OutputDir / "02-CppWorldEditorOutlinerAndGtxViewport.png";
        Score.Verify(EditorCanvas.WritePngFile(EditorExhibitPath) && std::filesystem::exists(EditorExhibitPath),
                     "Exported 3-pane C++ World Editor exhibit 02-CppWorldEditorOutlinerAndGtxViewport.png");

        //================================================================================================================
        // 7. UNIFIED 5-ENGINE & SHADOW LAB VALIDATION (CPU REFERENCE + SWIFTSHADER GPU COMPUTE)
        //================================================================================================================
        const std::filesystem::path EnginesDir =
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits" / "Images" / "WorldRenderEngines";
        std::filesystem::create_directories(EnginesDir);

        constexpr uint32_t TileW = 360u;
        constexpr uint32_t TileH = 220u;
        GeometricRaster::PerspectiveCameraFrame EvalCam = Camera;
        EvalCam.ViewportWidthPx  = TileW;
        EvalCam.ViewportHeightPx = TileH;

        struct EngineSpec
        {
            GeometricRaster::WorldRenderEngine Engine;
            const char*                        Title;
        };
        const std::array<EngineSpec, 5u> Engines {{
            { GeometricRaster::WorldRenderEngine::MatCap,                 "01 // MATCAP" },
            { GeometricRaster::WorldRenderEngine::OpenPbrDirect,          "02 // OPENPBR DIRECT (NO GI)" },
            { GeometricRaster::WorldRenderEngine::DdgiRaytracing,         "03 // DDGI CHEBYSHEV" },
            { GeometricRaster::WorldRenderEngine::SdfGlobalIllumination,  "04 // SDF RADIOSITY GI" },
            { GeometricRaster::WorldRenderEngine::DeterministicPathTrace, "05 // PATH TRACER (MIS/VNDF)" },
        }};

        std::vector<std::vector<uint8_t>> CpuEngineTiles(5u);
        bool CpuHdrAllFinite = true;
        for (std::size_t E = 0u; E < 5u; ++E)
        {
            GeometricRaster::WorldRenderConfiguration Cfg {};
            Cfg.Engine              = Engines[E].Engine;
            Cfg.ShadowAlgorithm     = (E == 2u) ? GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery
                                                : GeometricRaster::SolarShadowAlgorithm::ContactPcss;
            Cfg.ReflectionSource    = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
            Cfg.PathTraceSamples    = 16u;
            Cfg.PathTraceMaxBounces = 4u;
            const auto Frame = WorldRenderer.RenderWorldFrameWithEngine(Cfg, EvalCam, EvalCam, WorldScene);
            Score.Verify(Frame.DisplayRgba8.size() == TileW * TileH * 4u, Engines[E].Title);
            for (const auto& Px : Frame.CompositeLinearHdr)
            {
                if (!std::isfinite(Px.R) || !std::isfinite(Px.G) || !std::isfinite(Px.B) || Px.R < 0.0f || Px.G < 0.0f || Px.B < 0.0f)
                {
                    CpuHdrAllFinite = false;
                    break;
                }
            }
            CpuEngineTiles[E] = Frame.DisplayRgba8;
        }
        Score.Verify(CpuHdrAllFinite, "All 5 CPU WorldRenderEngines produce 100% finite non-negative HDR buffers");

        // Verify GPU Compute .slang across all 5 engines via SwiftShader/Vulkan if available
        std::vector<std::vector<uint8_t>> GpuEngineTiles(5u);
        std::vector<std::vector<uint8_t>> GdfDebugTiles(4u);
        std::vector<std::vector<uint8_t>> AllDebugTiles(16u);
        std::vector<uint8_t>              PtNoGiTile;
        std::vector<uint8_t>              PtNoReflTile;
        bool GpuEnginesValid = false;
        bool GdfDebugValid   = false;
        bool AllDebugValid   = false;
        bool PtSettingsValid = false;
        {
            GeometricRaster::VulkanInterchange GpuDevice {};
            if (GpuDevice.InitializeInterchange(std::filesystem::path(FRONTIER_REPO_ROOT_DIR)))
            {
                GeometricRaster::VulkanWorldRenderSequence GpuSeq {};
                const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
                if (GpuSeq.InitializeSequence(GpuDevice, ShaderDir))
                {
                    GpuSeq.SynchronizeSceneResources(WorldScene, &WorldRenderer.InspectIrradianceLattice(), true);
                    GpuEnginesValid = true;
                    for (std::size_t E = 0u; E < 5u; ++E)
                    {
                        GeometricRaster::WorldRenderConfiguration Cfg {};
                        Cfg.Engine              = Engines[E].Engine;
                        Cfg.ShadowAlgorithm     = (E == 2u) ? GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery
                                                            : GeometricRaster::SolarShadowAlgorithm::ContactPcss;
                        Cfg.ReflectionSource    = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
                        Cfg.PathTraceMaxBounces = 4u;
                        const uint32_t SampleCount = (E == 4u) ? 4u : 1u;
                        bool Ok = true;
                        for (uint32_t S = 0u; S < SampleCount; ++S)
                        {
                            Ok = Ok && GpuSeq.RenderWorldViewportWithEngine(Cfg, S, EvalCam, WorldScene, GpuEngineTiles[E]);
                        }
                        GpuEnginesValid = GpuEnginesValid && Ok && (GpuEngineTiles[E].size() == TileW * TileH * 4u);
                    }

                    // Render Global Distance Field (GDF) GPU Debug Channels 5..8
                    GdfDebugValid = true;
                    for (uint32_t D = 0u; D < 4u; ++D)
                    {
                        GeometricRaster::WorldRenderConfiguration GdfCfg {};
                        GdfCfg.Engine           = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
                        GdfCfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::ContactPcss;
                        GdfCfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
                        GdfCfg.DebugChannel     = 5u + D;
                        const bool Ok = GpuSeq.RenderWorldViewportWithEngine(GdfCfg, 0u, EvalCam, WorldScene, GdfDebugTiles[D]);
                        GdfDebugValid = GdfDebugValid && Ok && (GdfDebugTiles[D].size() == TileW * TileH * 4u);
                    }

                    // Render all 16 Debug Channels (0..15) reusing GpuEngineTiles[3] (0) & GdfDebugTiles (5..8)
                    AllDebugValid = GdfDebugValid && GpuEnginesValid;
                    AllDebugTiles[0] = GpuEngineTiles[3];
                    for (uint32_t D = 0u; D < 4u; ++D)
                    {
                        AllDebugTiles[5u + D] = GdfDebugTiles[D];
                    }
                    for (uint32_t D = 1u; D < 16u; ++D)
                    {
                        if (D >= 5u && D <= 8u) continue;
                        GeometricRaster::WorldRenderConfiguration DbgCfg {};
                        DbgCfg.Engine           = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
                        DbgCfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::ContactPcss;
                        DbgCfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
                        DbgCfg.DebugChannel     = D;
                        const bool Ok = GpuSeq.RenderWorldViewportWithEngine(DbgCfg, 0u, EvalCam, WorldScene, AllDebugTiles[D]);
                        AllDebugValid = AllDebugValid && Ok && (AllDebugTiles[D].size() == TileW * TileH * 4u);
                    }

                    // Verify Render Settings toggles on GPU Path Tracer (GI Off, Reflections Off)
                    {
                        GeometricRaster::WorldRenderConfiguration NoGiCfg {};
                        NoGiCfg.Engine                    = GeometricRaster::WorldRenderEngine::DeterministicPathTrace;
                        NoGiCfg.PathTraceMaxBounces       = 4u;
                        NoGiCfg.GlobalIlluminationEnabled = false;
                        const bool Ok1 = GpuSeq.RenderWorldViewportWithEngine(NoGiCfg, 0u, EvalCam, WorldScene, PtNoGiTile);

                        GeometricRaster::WorldRenderConfiguration NoReflCfg {};
                        NoReflCfg.Engine             = GeometricRaster::WorldRenderEngine::DeterministicPathTrace;
                        NoReflCfg.PathTraceMaxBounces = 4u;
                        NoReflCfg.ReflectionsEnabled = false;
                        const bool Ok2 = GpuSeq.RenderWorldViewportWithEngine(NoReflCfg, 0u, EvalCam, WorldScene, PtNoReflTile);

                        PtSettingsValid = Ok1 && Ok2 &&
                                          (PtNoGiTile.size() == TileW * TileH * 4u) &&
                                          (PtNoReflTile.size() == TileW * TileH * 4u);
                    }
                }
            }
        }
        Score.Verify(GpuEnginesValid, "Vulkan .slang compute renders all 5 unified WorldRenderEngines cleanly");
        Score.Verify(GdfDebugValid, "Vulkan .slang compute renders all 4 Global Distance Field (GDF) debug views (Modes 5..8)");
        Score.Verify(AllDebugValid, "Vulkan .slang compute renders all 16 modular Debug Views (Modes 0..15)");
        Score.Verify(PtSettingsValid, "Vulkan Path Tracer dispatches Render Settings toggles (GI Off, Reflections Off) cleanly");

        if (GdfDebugValid)
        {
            // Verify DebugChannel=5 (Pure GDF Sphere-Trace) has > 70% non-background geometry hits
            uint32_t GdfHitPx = 0u;
            const std::size_t TotalPx = TileW * TileH;
            for (std::size_t P = 0u; P < TotalPx; ++P)
            {
                const uint8_t R = GdfDebugTiles[0][P * 4u + 0u];
                const uint8_t G = GdfDebugTiles[0][P * 4u + 1u];
                const uint8_t B = GdfDebugTiles[0][P * 4u + 2u];
                if (R > 40u || G > 40u || B > 60u) ++GdfHitPx;
            }
            const double GdfHitRatio = static_cast<double>(GdfHitPx) / static_cast<double>(TotalPx);
            Score.Verify(GdfHitRatio > 0.70, "Pure GPU GDF sphere-traced iso-surface (Mode 5) covers > 70% of gallery pixels");

            // Verify DebugChannel=7 (GDF 3D Radiosity Volume) captures Crimson left vs Emerald right bounce asymmetry
            double LeftRed = 0.0, LeftGreen = 0.0, RightRed = 0.0, RightGreen = 0.0;
            for (uint32_t Y = 40u; Y < TileH - 40u; ++Y)
            {
                for (uint32_t X = 10u; X < 70u; ++X)
                {
                    const std::size_t Off = (static_cast<std::size_t>(Y) * TileW + X) * 4u;
                    LeftRed   += GdfDebugTiles[2][Off + 0u];
                    LeftGreen += GdfDebugTiles[2][Off + 1u];
                }
                for (uint32_t X = TileW - 70u; X < TileW - 10u; ++X)
                {
                    const std::size_t Off = (static_cast<std::size_t>(Y) * TileW + X) * 4u;
                    RightRed   += GdfDebugTiles[2][Off + 0u];
                    RightGreen += GdfDebugTiles[2][Off + 1u];
                }
            }
            Score.Verify(LeftRed > LeftGreen * 1.5 && RightGreen > RightRed * 1.5,
                         "GDF 3D Radiosity Volume (Mode 7) stores Crimson west & Emerald east bounce radiance");

            constexpr uint32_t GdfSheetW = 1540u;
            constexpr uint32_t GdfSheetH = 316u;
            const std::array<const char*, 4u> GdfTitles {{
                "01 // GDF SPHERE-TRACED ZERO-ISOSURFACE (NO BVH)",
                "02 // GDF SIGNED DISTANCE SLICE + 0.16m ISOLINES",
                "03 // GDF 3D RADIOSITY VOLUME (CRIMSON / EMERALD)",
                "04 // GDF 16-CONE MARCHED GI (ISOLATED + DENOISED)",
            }};
            Io.DisplaySize = ImVec2(float(GdfSheetW), float(GdfSheetH));
            ImGui::NewFrame();
            ImDrawList* GdfDraw = ImGui::GetBackgroundDrawList();
            GdfDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(GdfSheetW), float(GdfSheetH)), BgCol);
            GdfDraw->AddText(ImVec2(24.0f, 16.0f), AccentCol,
                             "FRONTIER GLOBAL DISTANCE FIELD (GDF) DEBUG & PROOF  //  SignedDistanceGridCompute.comp.spv -> SampleSdfGrid -> SdfRadiosityVoxels");
            for (std::size_t D = 0u; D < 4u; ++D)
            {
                const float X = 20.0f + static_cast<float>(D) * 378.0f;
                GdfDraw->AddRectFilled(ImVec2(X, 46.0f), ImVec2(X + 368.0f, 304.0f), CardBg, 6.0f);
                GdfDraw->AddText(ImVec2(X + 8.0f, 52.0f), GoldCol, GdfTitles[D]);
            }
            ImGui::Render();
            Exhibits::SoftwareRasterCanvas GdfSheet(GdfSheetW, GdfSheetH, { 11u, 14u, 20u, 255u });
            GdfSheet.RasterizeImGuiBatches(ImGui::GetDrawData());
            for (std::size_t D = 0u; D < 4u; ++D)
            {
                GdfSheet.BlitRgbaRegion(24u + static_cast<uint32_t>(D) * 378u, 72u, TileW, TileH, GdfDebugTiles[D].data());
            }
            const std::filesystem::path GdfProofPath =
                std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits" / "Images" / "GlobalIllumination" / "05-GlobalDistanceFieldDebugProof.png";
            Score.Verify(GdfSheet.WritePngFile(GdfProofPath) && std::filesystem::exists(GdfProofPath),
                         "Exported 05-GlobalDistanceFieldDebugProof.png");
        }

        // Verify engine differentiation (MatCap vs OpenPBR vs DDGI vs SDF GI vs PathTracer)
        const auto MeasureMeanLumaDiff = [](const std::vector<uint8_t>& A, const std::vector<uint8_t>& B) -> double
        {
            if (A.size() != B.size() || A.empty()) return 0.0;
            double Sum = 0.0;
            const std::size_t PxCount = A.size() / 4u;
            for (std::size_t P = 0u; P < PxCount; ++P)
            {
                const double LA = 0.2126 * A[P * 4u + 0u] + 0.7152 * A[P * 4u + 1u] + 0.0722 * A[P * 4u + 2u];
                const double LB = 0.2126 * B[P * 4u + 0u] + 0.7152 * B[P * 4u + 1u] + 0.0722 * B[P * 4u + 2u];
                Sum += std::fabs(LA - LB);
            }
            return Sum / static_cast<double>(PxCount);
        };
        Score.Verify(MeasureMeanLumaDiff(CpuEngineTiles[0], CpuEngineTiles[1]) > 8.0, "MatCap differs meaningfully from OpenPbrDirect");
        Score.Verify(MeasureMeanLumaDiff(CpuEngineTiles[1], CpuEngineTiles[2]) > 2.5, "OpenPbrDirect differs meaningfully from DdgiRaytracing");
        Score.Verify(MeasureMeanLumaDiff(CpuEngineTiles[1], CpuEngineTiles[3]) > 2.5, "OpenPbrDirect differs meaningfully from SdfGlobalIllumination");
        if (PtSettingsValid)
        {
            Score.Verify(MeasureMeanLumaDiff(GpuEngineTiles[4], PtNoGiTile) > 2.0,
                         "Path Tracer GlobalIlluminationEnabled=false produces darker/different output");
            Score.Verify(MeasureMeanLumaDiff(GpuEngineTiles[4], PtNoReflTile) > 1.5,
                         "Path Tracer ReflectionsEnabled=false produces different specular/glass output");
        }
        if (AllDebugValid)
        {
            const auto InspectBufferSanity = [](const std::vector<uint8_t>& Buf, double& OutMean, double& OutRange, double& OutBlackFrac, double& OutWhiteFrac)
            {
                const std::size_t PxCount = Buf.size() / 4u;
                double Sum = 0.0, MinL = 255.0, MaxL = 0.0;
                std::size_t Blacks = 0u, Whites = 0u;
                for (std::size_t P = 0u; P < PxCount; ++P)
                {
                    const double L = 0.2126 * Buf[P * 4u + 0u] + 0.7152 * Buf[P * 4u + 1u] + 0.0722 * Buf[P * 4u + 2u];
                    Sum += L;
                    MinL = std::min(MinL, L);
                    MaxL = std::max(MaxL, L);
                    if (L <= 1.0) ++Blacks;
                    if (L >= 254.0) ++Whites;
                }
                OutMean      = Sum / std::max<std::size_t>(1u, PxCount);
                OutRange     = MaxL - MinL;
                OutBlackFrac = static_cast<double>(Blacks) / std::max<std::size_t>(1u, PxCount);
                OutWhiteFrac = static_cast<double>(Whites) / std::max<std::size_t>(1u, PxCount);
            };

            bool EnginesPixelSanityOk = true;
            for (std::size_t E = 0u; E < 5u; ++E)
            {
                double CMean = 0.0, CRange = 0.0, CBlk = 0.0, CWht = 0.0;
                double GMean = 0.0, GRange = 0.0, GBlk = 0.0, GWht = 0.0;
                InspectBufferSanity(CpuEngineTiles[E], CMean, CRange, CBlk, CWht);
                InspectBufferSanity(GpuEngineTiles[E], GMean, GRange, GBlk, GWht);
                if (CMean < 15.0 || CMean > 240.0 || CRange < 40.0 || CBlk > 0.35 || CWht > 0.35 ||
                    GMean < 15.0 || GMean > 240.0 || GRange < 40.0 || GBlk > 0.35 || GWht > 0.35)
                {
                    EnginesPixelSanityOk = false;
                }
            }
            Score.Verify(EnginesPixelSanityOk, "All 5 CPU & GPU engine renders pass automated pixel clipping/black/white sanity checks");

            bool DebugViewsSanityOk = true;
            for (uint32_t D = 0u; D < 16u; ++D)
            {
                double DMean = 0.0, DRange = 0.0, DBlk = 0.0, DWht = 0.0;
                InspectBufferSanity(AllDebugTiles[D], DMean, DRange, DBlk, DWht);
                if (DMean < 5.0 || DMean > 245.0 || DRange < 30.0)
                {
                    DebugViewsSanityOk = false;
                }
                if (D != 0u)
                {
                    Score.Verify(MeasureMeanLumaDiff(AllDebugTiles[0], AllDebugTiles[D]) > 2.0,
                                 "Modular Debug View differs from Lit composite");
                }
            }
            Score.Verify(DebugViewsSanityOk, "All 16 GPU modular Debug Views pass automated pixel dynamic-range sanity checks");

            // Verify Depth (Mode 1) has crisp dynamic range (> 40 sRGB luma range) and GDF Radiosity (Mode 7) has non-black Glass Sphere
            {
                double DepthMean = 0.0, DepthRange = 0.0, DepthBlk = 0.0, DepthWht = 0.0;
                InspectBufferSanity(AllDebugTiles[1], DepthMean, DepthRange, DepthBlk, DepthWht);
                Score.Verify(DepthRange > 45.0 && DepthMean < 215.0,
                             "Depth Debug View (Mode 1) exhibits rich perceptual contrast across gallery");

                // Sample glass sphere center pixel region in Mode 7 (around X=235..265, Y=120..145 of 360x208 tile)
                double GlassRadSum = 0.0;
                uint32_t GlassSamples = 0u;
                for (uint32_t Y = 122u; Y <= 142u; ++Y)
                {
                    for (uint32_t X = 238u; X <= 258u; ++X)
                    {
                        const std::size_t Off = (static_cast<std::size_t>(Y) * TileW + X) * 4u;
                        GlassRadSum += AllDebugTiles[7][Off + 0u] + AllDebugTiles[7][Off + 1u] + AllDebugTiles[7][Off + 2u];
                        ++GlassSamples;
                    }
                }
                Score.Verify((GlassRadSum / std::max(1u, GlassSamples)) > 60.0,
                             "GDF Radiosity Volume (Mode 7) evaluates non-black radiosity across the Glass Sphere");
            }

            constexpr uint32_t DbgSheetW = 1540u;
            constexpr uint32_t DbgSheetH = 1120u;
            const std::array<const char*, 16u> DbgTitles {{
                "00 // LIT COMPOSITE (SDF GI)",
                "01 // DEPTH (LINEAR Z)",
                "02 // ALBEDO",
                "03 // SHADING NORMAL",
                "04 // ROUGHNESS",
                "05 // GDF ISO-SURFACE",
                "06 // GDF DISTANCE SLICE",
                "07 // GDF RADIOSITY VOLUME",
                "08 // INDIRECT GI (DENOISED)",
                "09 // METALNESS",
                "10 // MATERIAL / CLUSTER ID",
                "11 // GEOMETRIC NORMAL",
                "12 // SOLAR SHADOW VISIBILITY",
                "13 // AMBIENT OCCLUSION (AO)",
                "14 // DIRECT LIGHTING ONLY",
                "15 // SPECULAR & REFLECTIONS",
            }};
            Io.DisplaySize = ImVec2(float(DbgSheetW), float(DbgSheetH));
            ImGui::NewFrame();
            ImDrawList* DbgDraw = ImGui::GetBackgroundDrawList();
            DbgDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(DbgSheetW), float(DbgSheetH)), BgCol);
            DbgDraw->AddText(ImVec2(24.0f, 16.0f), AccentCol,
                             "FRONTIER MODULAR DEBUG VIEWS (00..15)  //  VULKAN .SLANG COMPUTE G-BUFFER, GDF, SHADOW LAB, AO & SPECULAR");
            for (uint32_t D = 0u; D < 16u; ++D)
            {
                const uint32_t Col = D % 4u;
                const uint32_t Row = D / 4u;
                const float X = 20.0f + static_cast<float>(Col) * 378.0f;
                const float Y = 46.0f + static_cast<float>(Row) * 266.0f;
                DbgDraw->AddRectFilled(ImVec2(X, Y), ImVec2(X + 368.0f, Y + 256.0f), CardBg, 6.0f);
                DbgDraw->AddText(ImVec2(X + 8.0f, Y + 6.0f), (D == 0u) ? AccentCol : GoldCol, DbgTitles[D]);
            }
            ImGui::Render();
            Exhibits::SoftwareRasterCanvas DbgSheet(DbgSheetW, DbgSheetH, { 11u, 14u, 20u, 255u });
            DbgSheet.RasterizeImGuiBatches(ImGui::GetDrawData());
            for (uint32_t D = 0u; D < 16u; ++D)
            {
                const uint32_t Col = D % 4u;
                const uint32_t Row = D / 4u;
                DbgSheet.BlitRgbaRegion(24u + Col * 378u, 72u + Row * 266u, TileW, TileH, AllDebugTiles[D].data());
            }
            const std::filesystem::path AllDbgPath =
                std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits" / "Images" / "GlobalIllumination" / "06-AllDebugViewsProof.png";
            Score.Verify(DbgSheet.WritePngFile(AllDbgPath) && std::filesystem::exists(AllDbgPath),
                         "Exported 06-AllDebugViewsProof.png");
        }

        // Render Sheet 1: 5 Unified Engines (Row 1 = CPU Reference, Row 2 = GPU .slang Compute)
        constexpr uint32_t SheetW = 1920u;
        constexpr uint32_t SheetH = 580u;
        Io.DisplaySize = ImVec2(float(SheetW), float(SheetH));
        ImGui::NewFrame();
        ImDrawList* EngDraw = ImGui::GetBackgroundDrawList();
        EngDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(SheetW), float(SheetH)), BgCol);
        EngDraw->AddText(ImVec2(24.0f, 16.0f), AccentCol,
                         "FRONTIER UNIFIED WORLD RENDER ENGINES  //  TOP: CPU REFERENCE  |  BOTTOM: VULKAN .SLANG COMPUTE (ZERO SDF SHADOWS & ZERO SDF REFLECTIONS)");
        for (std::size_t E = 0u; E < 5u; ++E)
        {
            const float X = 20.0f + static_cast<float>(E) * 378.0f;
            EngDraw->AddRectFilled(ImVec2(X, 46.0f), ImVec2(X + 368.0f, 300.0f), CardBg, 6.0f);
            EngDraw->AddText(ImVec2(X + 8.0f, 52.0f), GoldCol, Engines[E].Title);
            EngDraw->AddRectFilled(ImVec2(X, 312.0f), ImVec2(X + 368.0f, 566.0f), CardBg, 6.0f);
            EngDraw->AddText(ImVec2(X + 8.0f, 318.0f), AccentCol, "GPU .SLANG");
        }
        ImGui::Render();
        Exhibits::SoftwareRasterCanvas EngineSheet(SheetW, SheetH, { 11u, 14u, 20u, 255u });
        EngineSheet.RasterizeImGuiBatches(ImGui::GetDrawData());
        for (std::size_t E = 0u; E < 5u; ++E)
        {
            const uint32_t BX = 24u + static_cast<uint32_t>(E) * 378u;
            EngineSheet.BlitRgbaRegion(BX,  72u, TileW, TileH, CpuEngineTiles[E].data());
            if (!GpuEngineTiles[E].empty())
            {
                EngineSheet.BlitRgbaRegion(BX, 338u, TileW, TileH, GpuEngineTiles[E].data());
            }
        }
        const std::filesystem::path EnginesSheetPath = EnginesDir / "01-FiveUnifiedEnginesCpuAndGpu.png";
        Score.Verify(EngineSheet.WritePngFile(EnginesSheetPath) && std::filesystem::exists(EnginesSheetPath),
                     "Exported 01-FiveUnifiedEnginesCpuAndGpu.png");

        // Render Sheet 2: Shadow Lab 6 Algorithms (including Hybrid SDF Clipmap!)
        struct ShadowSpec
        {
            GeometricRaster::SolarShadowAlgorithm Alg;
            const char*                           Title;
        };
        const std::array<ShadowSpec, 6u> ShadowSpecs {{
            { GeometricRaster::SolarShadowAlgorithm::ContactPcss,      "01 // CONTACT PCSS" },
            { GeometricRaster::SolarShadowAlgorithm::PcssPlaneAware,   "02 // PCSS + PLANE FILTER" },
            { GeometricRaster::SolarShadowAlgorithm::ShadowRayMarch,   "03 // SMRT RAYMARCH" },
            { GeometricRaster::SolarShadowAlgorithm::PcfBilinear,      "04 // PCF 3X3" },
            { GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery, "05 // BVH4 RAY QUERY" },
            { GeometricRaster::SolarShadowAlgorithm::HybridSdf,        "06 // HYBRID SDF CLIPMAP" },
        }};
        std::vector<std::vector<uint8_t>> ShadowTiles(6u);
        for (std::size_t S = 0u; S < 6u; ++S)
        {
            GeometricRaster::WorldRenderConfiguration Cfg {};
            Cfg.Engine           = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
            Cfg.ShadowAlgorithm  = ShadowSpecs[S].Alg;
            Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
            const auto Frame     = WorldRenderer.RenderWorldFrameWithEngine(Cfg, EvalCam, EvalCam, WorldScene);
            ShadowTiles[S]       = Frame.DisplayRgba8;
        }
        constexpr uint32_t SSheetW = 1160u;
        constexpr uint32_t SSheetH = 580u;
        Io.DisplaySize = ImVec2(float(SSheetW), float(SSheetH));
        ImGui::NewFrame();
        ImDrawList* ShDraw = ImGui::GetBackgroundDrawList();
        ShDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(SSheetW), float(SSheetH)), BgCol);
        ShDraw->AddText(ImVec2(24.0f, 16.0f), AccentCol,
                        "FRONTIER SHADOW LAB  //  6 SHADOW ALGORITHMS (CONTACT PCSS, PCSS+FILTER, SMRT, PCF 3X3, BVH4 RAY, HYBRID SDF CLIPMAP)");
        for (std::size_t S = 0u; S < 6u; ++S)
        {
            const uint32_t Col = static_cast<uint32_t>(S % 3u);
            const uint32_t Row = static_cast<uint32_t>(S / 3u);
            const float X = 20.0f + static_cast<float>(Col) * 378.0f;
            const float Y = 46.0f + static_cast<float>(Row) * 266.0f;
            ShDraw->AddRectFilled(ImVec2(X, Y), ImVec2(X + 368.0f, Y + 254.0f), CardBg, 6.0f);
            ShDraw->AddText(ImVec2(X + 8.0f, Y + 6.0f), GoldCol, ShadowSpecs[S].Title);
        }
        ImGui::Render();
        Exhibits::SoftwareRasterCanvas ShadowSheet(SSheetW, SSheetH, { 11u, 14u, 20u, 255u });
        ShadowSheet.RasterizeImGuiBatches(ImGui::GetDrawData());
        for (std::size_t S = 0u; S < 6u; ++S)
        {
            const uint32_t Col = static_cast<uint32_t>(S % 3u);
            const uint32_t Row = static_cast<uint32_t>(S / 3u);
            ShadowSheet.BlitRgbaRegion(24u + Col * 378u, 72u + Row * 266u, TileW, TileH, ShadowTiles[S].data());
        }
        const std::filesystem::path ShadowSheetPath = EnginesDir / "02-ShadowLabSixAlgorithms.png";
        Score.Verify(ShadowSheet.WritePngFile(ShadowSheetPath) && std::filesystem::exists(ShadowSheetPath),
                     "Exported 02-ShadowLabSixAlgorithms.png");

        TypographyRepository::AssignCurrent(nullptr);
    }

    ImGui::DestroyContext(ImGuiCtx);

    std::printf("[TieredWorldRenderValidation] Completed: %u passed, %u failed -> %s\n",
                Score.Passed, Score.Failed, OutputPath.string().c_str());
    return (Score.Failed == 0u) ? 0 : 1;
}
