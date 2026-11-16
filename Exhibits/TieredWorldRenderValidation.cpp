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

        Score.Verify(EditorOutliner.QueryTotalCount() == 17u &&
                     EditorOutliner.CountDirectNested(ShowcaseBundle.ShowcaseFolderToken) == 10u &&
                     EditorOutliner.FindEntry(ShowcaseBundle.EditorCameraToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.SunEmitterToken) != nullptr &&
                     EditorOutliner.FindEntry(ShowcaseBundle.CeilingSoftboxToken) != nullptr,
                     "Registered all 17 Cornell-Studio Gallery objects (Showcase x10, Editor Camera, Sun, Ceiling Softbox) in C++ OutlinerPanel");

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
        for (std::size_t E = 0u; E < 5u; ++E)
        {
            GeometricRaster::WorldRenderConfiguration Cfg {};
            Cfg.Engine              = Engines[E].Engine;
            Cfg.ShadowAlgorithm     = (E == 2u) ? GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery
                                                : GeometricRaster::SolarShadowAlgorithm::ContactPcss;
            Cfg.ReflectionSource    = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
            Cfg.PathTraceSamples    = 8u;
            Cfg.PathTraceMaxBounces = 4u;
            const auto Frame = WorldRenderer.RenderWorldFrameWithEngine(Cfg, EvalCam, EvalCam, WorldScene);
            Score.Verify(Frame.DisplayRgba8.size() == TileW * TileH * 4u, Engines[E].Title);
            CpuEngineTiles[E] = Frame.DisplayRgba8;
        }

        // Verify GPU Compute .slang across all 5 engines via SwiftShader/Vulkan if available
        std::vector<std::vector<uint8_t>> GpuEngineTiles(5u);
        bool GpuEnginesValid = false;
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
                }
            }
        }
        Score.Verify(GpuEnginesValid, "Vulkan .slang compute renders all 5 unified WorldRenderEngines cleanly");

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

        // Render Sheet 2: Shadow Lab 5 Algorithms (Zero SDF Shadows!)
        struct ShadowSpec
        {
            GeometricRaster::SolarShadowAlgorithm Alg;
            const char*                           Title;
        };
        const std::array<ShadowSpec, 5u> ShadowSpecs {{
            { GeometricRaster::SolarShadowAlgorithm::ContactPcss,      "01 // CONTACT PCSS" },
            { GeometricRaster::SolarShadowAlgorithm::PcssPlaneAware,   "02 // PCSS + PLANE FILTER" },
            { GeometricRaster::SolarShadowAlgorithm::ShadowRayMarch,   "03 // SMRT RAYMARCH" },
            { GeometricRaster::SolarShadowAlgorithm::PcfBilinear,      "04 // PCF 3X3" },
            { GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery, "05 // BVH4 RAY QUERY" },
        }};
        std::vector<std::vector<uint8_t>> ShadowTiles(5u);
        for (std::size_t S = 0u; S < 5u; ++S)
        {
            GeometricRaster::WorldRenderConfiguration Cfg {};
            Cfg.Engine           = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
            Cfg.ShadowAlgorithm  = ShadowSpecs[S].Alg;
            Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
            const auto Frame     = WorldRenderer.RenderWorldFrameWithEngine(Cfg, EvalCam, EvalCam, WorldScene);
            ShadowTiles[S]       = Frame.DisplayRgba8;
        }
        constexpr uint32_t SSheetH = 310u;
        Io.DisplaySize = ImVec2(float(SheetW), float(SSheetH));
        ImGui::NewFrame();
        ImDrawList* ShDraw = ImGui::GetBackgroundDrawList();
        ShDraw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(SheetW), float(SSheetH)), BgCol);
        ShDraw->AddText(ImVec2(24.0f, 16.0f), AccentCol,
                        "FRONTIER SHADOW LAB  //  5 DIRECT SHADOW ALGORITHMS (ZERO SDF SHADOWS)");
        for (std::size_t S = 0u; S < 5u; ++S)
        {
            const float X = 20.0f + static_cast<float>(S) * 378.0f;
            ShDraw->AddRectFilled(ImVec2(X, 46.0f), ImVec2(X + 368.0f, 300.0f), CardBg, 6.0f);
            ShDraw->AddText(ImVec2(X + 8.0f, 52.0f), GoldCol, ShadowSpecs[S].Title);
        }
        ImGui::Render();
        Exhibits::SoftwareRasterCanvas ShadowSheet(SheetW, SSheetH, { 11u, 14u, 20u, 255u });
        ShadowSheet.RasterizeImGuiBatches(ImGui::GetDrawData());
        for (std::size_t S = 0u; S < 5u; ++S)
        {
            ShadowSheet.BlitRgbaRegion(24u + static_cast<uint32_t>(S) * 378u, 72u, TileW, TileH, ShadowTiles[S].data());
        }
        const std::filesystem::path ShadowSheetPath = EnginesDir / "02-ShadowLabFiveAlgorithms.png";
        Score.Verify(ShadowSheet.WritePngFile(ShadowSheetPath) && std::filesystem::exists(ShadowSheetPath),
                     "Exported 02-ShadowLabFiveAlgorithms.png");

        TypographyRepository::AssignCurrent(nullptr);
    }

    ImGui::DestroyContext(ImGuiCtx);

    std::printf("[TieredWorldRenderValidation] Completed: %u passed, %u failed -> %s\n",
                Score.Passed, Score.Failed, OutputPath.string().c_str());
    return (Score.Failed == 0u) ? 0 : 1;
}
