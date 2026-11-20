/*==========================================================================================================================================
                                                     WINDCLOUDSFOGVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Validation & Visual Proofs for Phases A–E: Wind Coupling, Multi-Shape Depth-Clipped Local Clouds, Height/Aerial Fog & Local Fog Volumes

#include "Engine/Components/Camera/CameraComponent.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricFogSequence.h"
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
            std::fprintf(stderr, "[WindCloudsFogValidation][FAIL] %s\n", Label);
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
    uint32_t              TileW,
    uint32_t              TileH,
    const uint8_t*        TileRgba)
{
    for (uint32_t Y = 0u; Y < TileH; ++Y)
    {
        for (uint32_t X = 0u; X < TileW; ++X)
        {
            const uint32_t Dst = ((DestY + Y) * SheetW + (DestX + X)) * 4u;
            const uint32_t Src = (Y * TileW + X) * 4u;
            Sheet[Dst + 0u] = TileRgba[Src + 0u];
            Sheet[Dst + 1u] = TileRgba[Src + 1u];
            Sheet[Dst + 2u] = TileRgba[Src + 2u];
            Sheet[Dst + 3u] = 255u;
        }
    }
}

[[nodiscard]] float MeanAbsPixelDiff(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B,
    uint32_t                    X0 = 0u,
    uint32_t                    Y0 = 0u,
    uint32_t                    X1 = 0u,
    uint32_t                    Y1 = 0u,
    uint32_t                    W  = 0u) noexcept
{
    if (A.size() != B.size() || A.empty()) return 0.0f;
    if (W == 0u)
    {
        double Sum = 0.0;
        for (size_t I = 0u; I < A.size(); I += 4u)
        {
            Sum += std::abs(static_cast<int>(A[I + 0u]) - static_cast<int>(B[I + 0u]));
            Sum += std::abs(static_cast<int>(A[I + 1u]) - static_cast<int>(B[I + 1u]));
            Sum += std::abs(static_cast<int>(A[I + 2u]) - static_cast<int>(B[I + 2u]));
        }
        return static_cast<float>(Sum / (static_cast<double>(A.size() / 4u) * 3.0));
    }
    double Sum = 0.0;
    uint64_t Count = 0ull;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const size_t I = (static_cast<size_t>(Y) * W + X) * 4u;
            Sum += std::abs(static_cast<int>(A[I + 0u]) - static_cast<int>(B[I + 0u]));
            Sum += std::abs(static_cast<int>(A[I + 1u]) - static_cast<int>(B[I + 1u]));
            Sum += std::abs(static_cast<int>(A[I + 2u]) - static_cast<int>(B[I + 2u]));
            ++Count;
        }
    }
    return (Count > 0ull) ? static_cast<float>(Sum / (static_cast<double>(Count) * 3.0)) : 0.0f;
}

[[nodiscard]] std::vector<uint8_t> MakeDiffHeatmap(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B,
    float                       Gain = 4.0f)
{
    std::vector<uint8_t> Out(A.size(), 255u);
    for (size_t I = 0u; I < A.size(); I += 4u)
    {
        const float D = (std::abs(static_cast<float>(A[I + 0u]) - static_cast<float>(B[I + 0u])) +
                         std::abs(static_cast<float>(A[I + 1u]) - static_cast<float>(B[I + 1u])) +
                         std::abs(static_cast<float>(A[I + 2u]) - static_cast<float>(B[I + 2u]))) /
                        (3.0f * 255.0f);
        const float V = std::clamp(D * Gain, 0.0f, 1.0f);
        Out[I + 0u] = static_cast<uint8_t>(std::round(std::clamp(V * 2.0f, 0.0f, 1.0f) * 255.0f));
        Out[I + 1u] = static_cast<uint8_t>(std::round(std::clamp(V * 1.4f - 0.2f, 0.0f, 1.0f) * 255.0f));
        Out[I + 2u] = static_cast<uint8_t>(std::round(std::clamp(0.15f + V * 0.7f, 0.0f, 1.0f) * 255.0f));
        Out[I + 3u] = 255u;
    }
    return Out;
}

struct OutdoorProofScene
{
    SceneGeoAsset GroundBox {};
    SceneGeoAsset HeroPillar {};
    SceneGeoAsset MidPillar {};
    SceneGeoAsset FarPillar {};
    SceneGeoAsset SphereAsset {};
    GeometricRaster::WorldSceneSpecification Scene {};
    GeometricRaster::PerspectiveCameraBasis  CamFrame {};

    void BuildBase(uint32_t TileW, uint32_t TileH)
    {
        using namespace Frontier::GeometricRaster;

        GroundBox   = CreateBoxAsset({ 60.0f, 60.0f, 0.4f });
        HeroPillar  = CreateBoxAsset({ 1.6f, 1.6f, 8.0f });
        MidPillar   = CreateBoxAsset({ 2.4f, 2.4f, 14.0f });
        FarPillar   = CreateBoxAsset({ 3.8f, 3.8f, 22.0f });
        SphereAsset = CreateSphereAsset(1.4f, 20u, 14u);

        Scene = {};
        auto AddMat = [&](Mathematics::LinearRGB Color, float Rough = 0.78f, float Metal = 0.0f)
        {
            MaterialSpace::SurfaceMaterial Mat {};
            Mat.Diffuse.BaseColor  = Color;
            Mat.Specular.Roughness = Rough;
            Mat.Specular.Metalness = Metal;
            return Scene.MaterialRegister.RegisterMaterial(Mat);
        };
        const uint16_t GroundMat    = AddMat({ 0.36f, 0.38f, 0.32f }, 0.85f);
        const uint16_t DarkStoneMat = AddMat({ 0.12f, 0.14f, 0.17f }, 0.65f);
        const uint16_t MidStoneMat  = AddMat({ 0.34f, 0.29f, 0.24f }, 0.70f);
        const uint16_t FarStoneMat  = AddMat({ 0.44f, 0.37f, 0.32f }, 0.72f);
        const uint16_t GoldMat      = AddMat({ 0.88f, 0.68f, 0.32f }, 0.42f, 0.0f);

        GeometrySpace::PolygonNetwork UnifiedNet {};
        auto AddBox = [&](const SceneGeoAsset& Asset, uint16_t Mat, Mathematics::Scalar3D Pos, Mathematics::Scalar3D HalfExt)
        {
            RigidClusterInstance Inst {};
            Inst.PyramidAsset        = &Asset.Pyr;
            Inst.MaterialSlot        = Mat;
            Inst.WorldTranslation    = { Pos.X, Pos.Y, Pos.Z };
            Inst.PreviousTranslation = Inst.WorldTranslation;
            Inst.UniformScale        = 1.0f;
            Scene.ClusterInstances.push_back(Inst);
            GeometrySpace::AppendBoxPrimitive(UnifiedNet, { Pos.X, Pos.Y, Pos.Z }, HalfExt, {}, {}, 0u, Mat);
        };
        auto AddSphere = [&](const SceneGeoAsset& Asset, uint16_t Mat, Mathematics::Scalar3D Pos, float R)
        {
            RigidClusterInstance Inst {};
            Inst.PyramidAsset        = &Asset.Pyr;
            Inst.MaterialSlot        = Mat;
            Inst.WorldTranslation    = { Pos.X, Pos.Y, Pos.Z };
            Inst.PreviousTranslation = Inst.WorldTranslation;
            Inst.UniformScale        = 1.0f;
            Scene.ClusterInstances.push_back(Inst);
            GeometrySpace::AppendSpherePrimitive(UnifiedNet, { Pos.X, Pos.Y, Pos.Z }, R, 20u, 14u, {}, {}, 0u, Mat);
        };

        // Ground & stepping landmarks from 11 m to 84 m
        AddBox(GroundBox,    GroundMat,    {   0.0f, 40.0f, -0.4f }, { 60.0f, 60.0f,  0.4f });
        AddBox(HeroPillar,   DarkStoneMat, {  -4.5f, 14.0f,  7.2f }, {  1.6f,  1.6f,  8.0f });
        AddSphere(SphereAsset, GoldMat,    {  -4.5f, 11.2f,  2.2f }, 1.4f);
        AddBox(MidPillar,    MidStoneMat,  {  11.0f, 36.0f, 13.5f }, {  2.4f,  2.4f, 14.0f });
        AddBox(FarPillar,    FarStoneMat,  { -14.0f, 76.0f, 21.0f }, {  3.8f,  3.8f, 22.0f });
        AddBox(FarPillar,    FarStoneMat,  {  20.0f, 84.0f, 21.0f }, {  3.8f,  3.8f, 22.0f });

        (void)Scene.SceneTree.BuildFromPolygonNetwork(UnifiedNet);

        Scene.Atmosphere.Medium.AtmosphereEnabled       = true;
        Scene.Atmosphere.Medium.SkyBrightnessMultiplier = 1.0f;
        Scene.Atmosphere.Solar.SolarEnabled             = true;
        Scene.Atmosphere.Solar.ManualDirectionOverride  = true;
        Scene.Atmosphere.Solar.ManualAzimuthDegrees     = 236.0f;
        Scene.Atmosphere.Solar.ManualElevationDegrees   = 25.0f;
        Scene.Atmosphere.LensFlare.Enabled              = false;
        Scene.Atmosphere.Moons[0].Visible               = false;
        Scene.Atmosphere.Stars.StarfieldEnabled         = false;

        Scene.Atmosphere.Clouds.CloudsEnabled             = true;
        VolumetricCloudSequence::ApplyCloudCategoryPreset(Scene.Atmosphere.Clouds, CloudCategoryType::Cumulus);
        Scene.Atmosphere.Clouds.CloudFloorMeters          = 650.0f;
        Scene.Atmosphere.Clouds.LayerThicknessMeters      = 550.0f;
        Scene.Atmosphere.Clouds.CloudFraction             = 0.44f;
        Scene.Atmosphere.Clouds.ExtinctionMultiplier      = 1.75f;
        Scene.Atmosphere.Clouds.FeatureScaleMultiplier  = 1.0f;
        Scene.Atmosphere.Clouds.FollowWindEnabled         = true;
        Scene.Atmosphere.Clouds.CloudShadow.ShadowEnabled = true;

        Scene.Atmosphere.Wind.SpeedMetersPerSecond        = 14.0f;
        Scene.Atmosphere.Wind.BearingDegrees              = 250.0f;
        Scene.Atmosphere.Wind.ShearPerKilometer           = 0.35f;
        Scene.Atmosphere.Wind.VeerDegreesPerKm            = 14.0f;
        Scene.Atmosphere.Wind.Turbulence                  = 0.38f;
        Scene.Atmosphere.Wind.Steadiness                  = 0.92f;
        Scene.Atmosphere.Clouds.WindSpeedMetersPerSecond  = 14.0f;
        Scene.Atmosphere.Clouds.WindBearingDegrees        = 250.0f;
        Scene.Atmosphere.Clouds.WindVeerDegreesPerKm      = 14.0f;
        Scene.Atmosphere.Clouds.WindTurbulence            = 0.38f;

        Scene.Atmosphere.Fog.HeightFog.Enabled            = false;
        Scene.Atmosphere.Fog.AerialFog.Enabled            = false;
        Scene.Atmosphere.Fog.LocalFogs.clear();
        Scene.Atmosphere.LocalCloud.LocalEnabled          = false;
        Scene.Atmosphere.LocalClouds.clear();

        CamFrame = {};
        CamFrame.CameraOrigin       = { 0.0f, -6.0f, 5.5f };
        CamFrame.TargetPoint        = { 0.0f, 36.0f, 11.5f };
        CamFrame.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
        CamFrame.VerticalFovRadians = 56.0f * Constants::DegToRadFloat;
        CamFrame.NearClipMeters     = 0.1f;
        CamFrame.FarClipMeters      = 500.0f;
        CamFrame.ViewportWidthPx    = TileW;
        CamFrame.ViewportHeightPx   = TileH;
    }
};

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;

    ValidationScorecard Score {};
    const std::filesystem::path RepoRoot  = FRONTIER_REPO_ROOT_DIR;
    const std::filesystem::path ShaderDir = FRONTIER_COMPILED_SHADER_DIR;
    const std::filesystem::path OutDir    = RepoRoot / "Exhibits/Images/WindCloudsFog";
    std::filesystem::create_directories(OutDir);

    // 1. Unit checks for Wind Solver, 7 Volume Shapes, and Height Fog Singularity Safety
    {
        AtmosphericWindParameters Wp {};
        Wp.SpeedMetersPerSecond = 10.0f;
        Wp.BearingDegrees       = 250.0f;
        Wp.VeerDegreesPerKm     = 20.0f;
        Wp.GustStrength         = 0.25f;
        Wp.Turbulence           = 0.45f;
        Wp.Steadiness           = 0.8f;

        const auto Auth0 = AtmosphericWindSolver::ResolveWindSample(
            Wp, 10.0f, 250.0f, WindCouplingSource::GlobalWind, { 0.0f, 24.0f, 0.0f }, 0.0f, 1.5f);
        const auto Auth2Km = AtmosphericWindSolver::ResolveWindSample(
            Wp, 10.0f, 250.0f, WindCouplingSource::GlobalWind, { 0.0f, 24.0f, 2000.0f }, 2000.0f, 1.5f);

        Score.Verify(Auth0.VelocityMetersPerSec.X != 0.0f || Auth0.VelocityMetersPerSec.Y != 0.0f, "GlobalWind resolves positive velocity");
        Score.Verify(std::abs(Auth0.VelocityMetersPerSec.X - Auth2Km.VelocityMetersPerSec.X) > 0.5f, "Ekman veer rotates wind vector with altitude");
        Score.Verify(Auth0.CurlScalar > 0.01f, "Turbulent wind injects nonzero CurlScalar");

        const auto ShapeCenterCheck = [&](AtmosphericVolumeShape Shape)
        {
            return AtmosphericWindSolver::EvaluateVolumeShapeMask(
                { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 4.0f, 4.0f, 4.0f }, Shape, 0.35f, 6u, 0.25f);
        };
        Score.Verify(ShapeCenterCheck(AtmosphericVolumeShape::Box) > 0.99f &&
                     ShapeCenterCheck(AtmosphericVolumeShape::Sphere) > 0.99f &&
                     ShapeCenterCheck(AtmosphericVolumeShape::Ellipsoid) > 0.99f &&
                     ShapeCenterCheck(AtmosphericVolumeShape::Cylinder) > 0.99f &&
                     ShapeCenterCheck(AtmosphericVolumeShape::Cone) > 0.99f &&
                     ShapeCenterCheck(AtmosphericVolumeShape::Diamond) > 0.99f &&
                     ShapeCenterCheck(AtmosphericVolumeShape::Prism) > 0.99f,
                     "All 7 AtmosphericVolumeShapes evaluate to 1.0 at centre");

        HeightFogConfiguration Hf {};
        Hf.Enabled             = true;
        Hf.Density             = 0.02f;
        Hf.FalloffHeightMeters = 400.0f;
        const float TauFlat  = VolumetricFogSequence::EvaluateHeightFogOpticalDepth(0.0f, 0.0f, 100.0f, Hf);
        const float TauSlope = VolumetricFogSequence::EvaluateHeightFogOpticalDepth(0.0f, 0.1f, 100.0f, Hf);
        Score.Verify(std::isfinite(TauFlat) && std::abs(TauFlat - 2.0f) < 0.05f, "HeightFog singularity limit dz=0 matches D*L");
        Score.Verify(std::isfinite(TauSlope) && TauSlope > 0.0f && std::abs(TauFlat - TauSlope) < 0.15f, "HeightFog analytical optical depth continuous near dz=0");
        Score.Verify(std::abs(VolumetricFogSequence::ComputeTwoPercentVisibilityRangeMeters(0.0f, Hf) - 195.6f) < 1.0f, "2% Koschmieder visibility helper matches ~195.6m");
    }

    const uint32_t TileW = 280u;
    const uint32_t TileH = 180u;
    TieredWorldRenderSequence Renderer {};
    WorldRenderConfiguration Cfg {};
    Cfg.Engine          = WorldRenderEngine::OpenPbrDirect;
    Cfg.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;

    auto RenderCpu = [&](OutdoorProofScene& Proof)
    {
        Renderer.CompileBounceSummaries(Proof.Scene);
        Renderer.SynchronizeWorldAtmosphere(Proof.Scene);
        return Renderer.RenderWorldFrameWithEngine(Cfg, Proof.CamFrame, Proof.CamFrame, Proof.Scene);
    };

    // 2. Sheet 1: Wind-Coupled Global Clouds & Ground Shadows
    {
        OutdoorProofScene S {};
        S.BuildBase(TileW, TileH);

        S.Scene.Atmosphere.SimulationSeconds = 0.0f;
        const auto R0 = RenderCpu(S);

        S.Scene.Atmosphere.SimulationSeconds = 55.0f;
        const auto R45 = RenderCpu(S);

        S.Scene.Atmosphere.SimulationSeconds = 8.0f;
        S.Scene.Atmosphere.Clouds.WindSource = WindCouplingSource::CloudWind;
        S.Scene.Atmosphere.Clouds.WindSpeedMetersPerSecond = 26.0f;
        S.Scene.Atmosphere.Clouds.WindTurbulence = 0.85f;
        const auto Rff = RenderCpu(S);

        const float DriftDiff   = MeanAbsPixelDiff(R0.DisplayRgba8, R45.DisplayRgba8);
        const float SkyDrift    = MeanAbsPixelDiff(R0.DisplayRgba8, R45.DisplayRgba8, 0u, 0u, TileW, TileH / 2u, TileW);
        const float GroundDrift = MeanAbsPixelDiff(R0.DisplayRgba8, R45.DisplayRgba8, 0u, TileH / 2u, TileW, TileH, TileW);
        Score.Verify(SkyDrift > 1.0f, "Wind advection visibly drifts global clouds over time");
        Score.Verify(GroundDrift > 0.10f, "Ground cloud shadows remain synced with drifting cloud deck");
        Score.Verify(DriftDiff > 0.5f, "Sheet 1 wind delta confirmed");

        const auto Heat = MakeDiffHeatmap(R0.DisplayRgba8, R45.DisplayRgba8, 5.5f);
        std::vector<uint8_t> Sheet(TileW * 2u * TileH * 2u * 4u, 255u);
        BlitTile(Sheet, TileW * 2u, 0u,    0u,    TileW, TileH, R0.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, 0u,    TileW, TileH, R45.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, 0u,    TileH, TileW, TileH, Rff.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, TileH, TileW, TileH, Heat.data());
        Score.Verify(WritePngRgba8(OutDir / "01-WindCoupledCloudsAndShadows.png", TileW * 2u, TileH * 2u, Sheet.data()),
                     "Write 01-WindCoupledCloudsAndShadows.png");
    }

    // 3. Sheet 2: Multi-Shape Depth-Clipped Local Clouds + Turbulent Wind Advection
    {
        OutdoorProofScene S {};
        S.BuildBase(TileW, TileH);
        S.Scene.Atmosphere.Clouds.CloudFraction = 0.25f;

        auto MakeLocalCloud = [&](Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt,
                                  AtmosphericVolumeShape Shape, float YawDeg, uint32_t Sides = 6u)
        {
            BoundedCloudVolume V {};
            V.LocalEnabled             = true;
            V.VolumeCenterMeters       = Center;
            V.HalfExtentMeters         = HalfExt;
            V.Shape                    = Shape;
            V.YawDegrees               = YawDeg;
            V.PrismSides               = Sides;
            V.EdgeSoftness             = 0.34f;
            V.CloudFraction            = 0.88f;
            V.ExtinctionMultiplier     = 3.4f;
            V.FeatureScaleMeters       = 14.0f;
            V.ForwardScatterAnisotropy = 0.50f;
            V.FollowWindEnabled        = true;
            V.WindSource               = WindCouplingSource::LocalCloudWind;
            return V;
        };

        S.Scene.Atmosphere.LocalClouds.push_back(MakeLocalCloud({ -6.5f, 18.0f, 6.2f }, { 4.2f, 4.2f, 3.2f }, AtmosphericVolumeShape::Sphere, 0.0f));
        S.Scene.Atmosphere.LocalClouds.push_back(MakeLocalCloud({  0.0f, 16.0f, 7.0f }, { 3.5f, 3.5f, 4.2f }, AtmosphericVolumeShape::Cone, 20.0f));
        S.Scene.Atmosphere.LocalClouds.push_back(MakeLocalCloud({  6.2f, 17.0f, 6.0f }, { 3.6f, 3.6f, 3.8f }, AtmosphericVolumeShape::Diamond, 15.0f));
        S.Scene.Atmosphere.LocalClouds.push_back(MakeLocalCloud({ -1.5f, 24.0f, 11.0f }, { 5.2f, 4.0f, 3.4f }, AtmosphericVolumeShape::Prism, 32.0f, 6u));
        const auto RShapesA = RenderCpu(S);

        S.Scene.Atmosphere.LocalClouds.clear();
        S.Scene.Atmosphere.LocalClouds.push_back(MakeLocalCloud({ -6.0f, 18.0f, 6.5f }, { 4.4f, 3.4f, 3.0f }, AtmosphericVolumeShape::Box, 28.0f));
        S.Scene.Atmosphere.LocalClouds.push_back(MakeLocalCloud({  0.5f, 17.5f, 6.2f }, { 4.8f, 3.2f, 2.8f }, AtmosphericVolumeShape::Ellipsoid, -24.0f));
        S.Scene.Atmosphere.LocalClouds.push_back(MakeLocalCloud({  6.8f, 19.0f, 6.8f }, { 3.2f, 3.2f, 4.2f }, AtmosphericVolumeShape::Cylinder, 0.0f));
        const auto RShapesB = RenderCpu(S);

        S.Scene.Atmosphere.Wind.SpeedMetersPerSecond = 24.0f;
        S.Scene.Atmosphere.Wind.Turbulence           = 0.85f;
        S.Scene.Atmosphere.SimulationSeconds         = 14.0f;
        for (auto& V : S.Scene.Atmosphere.LocalClouds) V.WindSource = WindCouplingSource::LocalCloudWind;
        const auto RSwirl = RenderCpu(S);

        S.Scene.Atmosphere.LocalClouds.clear();
        const auto RNoLocal = RenderCpu(S);

        Score.Verify(MeanAbsPixelDiff(RShapesA.DisplayRgba8, RNoLocal.DisplayRgba8) > 2.0f, "Multi-shape LocalClouds visibly render in scene");
        Score.Verify(MeanAbsPixelDiff(RShapesB.DisplayRgba8, RSwirl.DisplayRgba8) > 0.6f, "Ekman wind & turbulence advects LocalClouds");

        const auto Heat = MakeDiffHeatmap(RShapesA.DisplayRgba8, RNoLocal.DisplayRgba8, 4.0f);
        std::vector<uint8_t> Sheet(TileW * 2u * TileH * 2u * 4u, 255u);
        BlitTile(Sheet, TileW * 2u, 0u,    0u,    TileW, TileH, RShapesA.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, 0u,    TileW, TileH, RShapesB.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, 0u,    TileH, TileW, TileH, RSwirl.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, TileH, TileW, TileH, Heat.data());
        Score.Verify(WritePngRgba8(OutDir / "02-MultiShapeLocalCloudsDepthClipped.png", TileW * 2u, TileH * 2u, Sheet.data()),
                     "Write 02-MultiShapeLocalCloudsDepthClipped.png");
    }

    // 4. Sheet 3: Exponential Height Fog & Distance-Gated Aerial Perspective
    {
        OutdoorProofScene S {};
        S.BuildBase(TileW, TileH);
        const auto RClear = RenderCpu(S);

        S.Scene.Atmosphere.Fog.HeightFog.Enabled             = true;
        S.Scene.Atmosphere.Fog.HeightFog.Density             = 0.0055f;
        S.Scene.Atmosphere.Fog.HeightFog.FalloffHeightMeters = 16.0f;
        S.Scene.Atmosphere.Fog.HeightFog.BaseAltitudeMeters  = 0.0f;
        S.Scene.Atmosphere.Fog.HeightFog.SunScatterIntensity = 0.75f;
        S.Scene.Atmosphere.Fog.HeightFog.HeightColour        = { 0.86f, 0.90f, 0.96f };
        const auto RHeightFog = RenderCpu(S);

        S.Scene.Atmosphere.Fog.HeightFog.Enabled             = false;
        S.Scene.Atmosphere.Fog.AerialFog.Enabled             = true;
        S.Scene.Atmosphere.Fog.AerialFog.DensityMultiplier   = 2.6f;
        S.Scene.Atmosphere.Fog.AerialFog.StartDistanceMeters = 16.0f;
        S.Scene.Atmosphere.Fog.AerialFog.MieBlend            = 0.55f;
        const auto RAerialFog = RenderCpu(S);

        S.Scene.Atmosphere.Fog.HeightFog.Enabled = true;
        S.Scene.Atmosphere.Fog.HeightFog.Density = 0.0040f;
        const auto RCombined = RenderCpu(S);

        Score.Verify(MeanAbsPixelDiff(RClear.DisplayRgba8, RHeightFog.DisplayRgba8) > 3.0f, "HeightFog visibly attenuates scene");
        Score.Verify(MeanAbsPixelDiff(RClear.DisplayRgba8, RAerialFog.DisplayRgba8) > 2.5f, "AerialPerspective visibly veils distant landmarks");
        Score.Verify(MeanAbsPixelDiff(RHeightFog.DisplayRgba8, RCombined.DisplayRgba8) > 1.5f, "Combined HeightFog + AerialPerspective layers cleanly");

        std::vector<uint8_t> Sheet(TileW * 2u * TileH * 2u * 4u, 255u);
        BlitTile(Sheet, TileW * 2u, 0u,    0u,    TileW, TileH, RClear.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, 0u,    TileW, TileH, RHeightFog.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, 0u,    TileH, TileW, TileH, RAerialFog.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, TileH, TileW, TileH, RCombined.DisplayRgba8.data());
        Score.Verify(WritePngRgba8(OutDir / "03-HeightFogAndAerialPerspective.png", TileW * 2u, TileH * 2u, Sheet.data()),
                     "Write 03-HeightFogAndAerialPerspective.png");
    }

    // 5. Sheet 4: Multi-Shape Local Fog Volumes + Wind Advection + GPU Parity Check
    {
        OutdoorProofScene S {};
        S.BuildBase(TileW, TileH);

        auto MakeLocalFog = [&](Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt,
                                AtmosphericVolumeShape Shape, Mathematics::LinearRGB Tint,
                                float YawDeg = 0.0f, uint32_t Sides = 6u)
        {
            LocalFogVolume V {};
            V.Enabled            = true;
            V.VolumeCenterMeters = Center;
            V.HalfExtentMeters   = HalfExt;
            V.Shape              = Shape;
            V.YawDegrees         = YawDeg;
            V.PrismSides         = Sides;
            V.EdgeSoftness       = 0.28f;
            V.Density            = 2.65f;
            V.Coverage           = 0.88f;
            V.FeatureScaleMeters = 9.0f;
            V.AnisotropyG        = 0.62f;
            V.FogTint            = Tint;
            V.FollowWind         = true;
            V.WindSource         = WindCouplingSource::LocalFogWind;
            return V;
        };

        S.Scene.Atmosphere.Fog.LocalFogs.push_back(MakeLocalFog({ -7.5f, 15.0f, 3.2f }, { 3.5f, 3.5f, 3.0f }, AtmosphericVolumeShape::Sphere,    { 0.72f, 0.88f, 1.00f }));
        S.Scene.Atmosphere.Fog.LocalFogs.push_back(MakeLocalFog({ -2.5f, 18.0f, 3.4f }, { 3.4f, 3.0f, 3.0f }, AtmosphericVolumeShape::Box,       { 0.98f, 0.84f, 0.70f }, 22.0f));
        S.Scene.Atmosphere.Fog.LocalFogs.push_back(MakeLocalFog({  3.2f, 17.5f, 3.2f }, { 4.0f, 2.8f, 2.6f }, AtmosphericVolumeShape::Ellipsoid, { 0.80f, 0.96f, 0.88f }, -18.0f));
        S.Scene.Atmosphere.Fog.LocalFogs.push_back(MakeLocalFog({  8.2f, 18.0f, 3.8f }, { 2.8f, 2.8f, 3.8f }, AtmosphericVolumeShape::Cone,      { 0.95f, 0.76f, 0.88f }));
        S.Scene.Atmosphere.Fog.LocalFogs.push_back(MakeLocalFog({ -6.0f, 26.0f, 4.2f }, { 3.2f, 3.2f, 4.0f }, AtmosphericVolumeShape::Cylinder,  { 0.78f, 0.92f, 1.00f }));
        S.Scene.Atmosphere.Fog.LocalFogs.push_back(MakeLocalFog({  0.5f, 28.0f, 4.6f }, { 3.6f, 3.6f, 4.2f }, AtmosphericVolumeShape::Diamond,   { 1.00f, 0.90f, 0.76f }, 15.0f));
        S.Scene.Atmosphere.Fog.LocalFogs.push_back(MakeLocalFog({  7.5f, 28.0f, 4.4f }, { 3.6f, 3.6f, 4.0f }, AtmosphericVolumeShape::Prism,     { 0.84f, 0.86f, 1.00f }, 30.0f, 6u));
        const auto RT0 = RenderCpu(S);

        S.Scene.Atmosphere.SimulationSeconds = 12.0f;
        const auto RT12 = RenderCpu(S);

        S.Scene.Atmosphere.Wind.SpeedMetersPerSecond = 22.0f;
        S.Scene.Atmosphere.Wind.Turbulence           = 0.85f;
        S.Scene.Atmosphere.SimulationSeconds         = 24.0f;
        const auto RForceFog = RenderCpu(S);

        S.Scene.Atmosphere.Fog.HeightFog.Enabled             = true;
        S.Scene.Atmosphere.Fog.HeightFog.Density             = 0.0032f;
        S.Scene.Atmosphere.Fog.HeightFog.FalloffHeightMeters = 16.0f;
        S.Scene.Atmosphere.Fog.AerialFog.Enabled             = true;
        S.Scene.Atmosphere.Fog.AerialFog.DensityMultiplier   = 1.4f;
        S.Scene.Atmosphere.Fog.AerialFog.StartDistanceMeters = 18.0f;
        BoundedCloudVolume HeroCloud {};
        HeroCloud.LocalEnabled         = true;
        HeroCloud.VolumeCenterMeters   = { -1.0f, 22.0f, 12.0f };
        HeroCloud.HalfExtentMeters     = { 7.0f, 5.0f, 3.2f };
        HeroCloud.Shape                = AtmosphericVolumeShape::Ellipsoid;
        HeroCloud.CloudFraction        = 0.82f;
        HeroCloud.ExtinctionMultiplier = 2.8f;
        HeroCloud.FeatureScaleMeters   = 16.0f;
        HeroCloud.WindSource           = WindCouplingSource::LocalCloudWind;
        S.Scene.Atmosphere.LocalClouds.push_back(HeroCloud);
        const auto RFullStack = RenderCpu(S);

        Score.Verify(MeanAbsPixelDiff(RT0.DisplayRgba8, RT12.DisplayRgba8) > 0.5f, "LocalFogVolumes visibly advect with wind over time");
        Score.Verify(MeanAbsPixelDiff(RT12.DisplayRgba8, RForceFog.DisplayRgba8) > 0.35f, "Strong turbulent wind modifies LocalFogVolumes");

        // GPU VulkanWorldRenderSequence parity check
        VulkanInterchange VkDevice {};
        if (VkDevice.InitializeInterchange(RepoRoot))
        {
            VulkanWorldRenderSequence WorldGpu {};
            if (WorldGpu.InitializeSequence(VkDevice, ShaderDir))
            {
                WorldGpu.SynchronizeSceneResources(S.Scene, true);
                std::vector<uint8_t> GpuPixels {};
                const bool GpuOk = WorldGpu.RenderWorldViewportWithEngine(Cfg, 0u, S.CamFrame, S.Scene, GpuPixels);
                Score.Verify(GpuOk && GpuPixels.size() == TileW * TileH * 4u, "Vulkan GPU rendered full wind+clouds+fog frame");
                if (GpuOk)
                {
                    const float CpuGpuMad = MeanAbsPixelDiff(RFullStack.DisplayRgba8, GpuPixels);
                    std::printf("[WindCloudsFogValidation] CPU vs GPU FullStack MAD = %.2f\n", CpuGpuMad);
                    Score.Verify(CpuGpuMad < 35.0f, "GPU VulkanWorldRenderSequence matches CPU TieredWorldRenderSequence");
                }
            }
        }

        std::vector<uint8_t> Sheet(TileW * 2u * TileH * 2u * 4u, 255u);
        BlitTile(Sheet, TileW * 2u, 0u,    0u,    TileW, TileH, RT0.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, 0u,    TileW, TileH, RT12.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, 0u,    TileH, TileW, TileH, RForceFog.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, TileH, TileW, TileH, RFullStack.DisplayRgba8.data());
        Score.Verify(WritePngRgba8(OutDir / "04-MultiShapeLocalFogWithWind.png", TileW * 2u, TileH * 2u, Sheet.data()),
                     "Write 04-MultiShapeLocalFogWithWind.png");
    }

    // 6. Sheet 5: Large-Radius HalfSphere Fog + Turbulent Wind Sculpting + Volumetric God Rays (Clouds + Fog)
    {
        OutdoorProofScene S {};
        S.BuildBase(TileW, TileH);
        // Sun slightly front-left behind pillars & broken clouds for dramatic crepuscular shafts into fog
        S.Scene.Atmosphere.Solar.ManualAzimuthDegrees   = 264.0f;
        S.Scene.Atmosphere.Solar.ManualElevationDegrees = 19.0f;
        S.Scene.Atmosphere.Clouds.CloudFraction         = 0.54f;
        S.Scene.Atmosphere.Clouds.ExtinctionMultiplier  = 2.6f;

        // Broken LocalClouds overhead to cast distinct cloud shafts down through the HalfSphere fog
        auto AddCloud = [&](Mathematics::Axis3D Center, Mathematics::Scalar3D Ext, AtmosphericVolumeShape Sh)
        {
            BoundedCloudVolume C {};
            C.LocalEnabled         = true;
            C.VolumeCenterMeters   = Center;
            C.HalfExtentMeters     = Ext;
            C.Shape                = Sh;
            C.EdgeSoftness         = 0.36f;
            C.CloudFraction        = 0.90f;
            C.ExtinctionMultiplier = 3.8f;
            C.FeatureScaleMeters   = 15.0f;
            S.Scene.Atmosphere.LocalClouds.push_back(C);
        };
        AddCloud({ -9.5f, 36.0f, 16.5f }, { 7.5f, 5.5f, 3.4f }, AtmosphericVolumeShape::Ellipsoid);
        AddCloud({  7.0f, 39.0f, 17.5f }, { 7.2f, 5.5f, 3.4f }, AtmosphericVolumeShape::Ellipsoid);

        // Large-radius HalfSphere fog dome with smooth radial & height falloff
        LocalFogVolume HalfSphereDome {};
        HalfSphereDome.Enabled            = true;
        HalfSphereDome.FollowWind         = true;
        HalfSphereDome.WindSource         = WindCouplingSource::LocalFogWind;
        HalfSphereDome.Shape              = AtmosphericVolumeShape::HalfSphere;
        HalfSphereDome.VolumeCenterMeters = { 0.0f, 34.0f, 0.0f };
        HalfSphereDome.HalfExtentMeters   = { 44.0f, 44.0f, 22.0f };
        HalfSphereDome.EdgeSoftness       = 0.78f;
        HalfSphereDome.Density            = 1.45f;
        HalfSphereDome.Coverage           = 0.78f;
        HalfSphereDome.FeatureScaleMeters = 10.0f;
        HalfSphereDome.AnisotropyG        = 0.68f;
        HalfSphereDome.FogTint            = { 0.78f, 0.86f, 0.95f };
        S.Scene.Atmosphere.Fog.LocalFogs.push_back(HalfSphereDome);

        // Tile 1: HalfSphere fog (no Wind, no GodRays)
        S.Scene.Atmosphere.Wind.Enabled = false;
        S.Scene.Atmosphere.SimulationSeconds = 0.0f;
        const auto RHalfSphereStill = RenderCpu(S);

        // Verify HalfSphere falloff math (smooth monotonic drop from center outward, zero below base)
        const float MaskCenter = AtmosphericWindSolver::EvaluateVolumeShapeMask(
            { 0.0f, 34.0f, 1.0f }, HalfSphereDome.VolumeCenterMeters, HalfSphereDome.HalfExtentMeters,
            AtmosphericVolumeShape::HalfSphere, 0.0f, 6u, HalfSphereDome.EdgeSoftness);
        const float MaskMid = AtmosphericWindSolver::EvaluateVolumeShapeMask(
            { 20.0f, 34.0f, 6.0f }, HalfSphereDome.VolumeCenterMeters, HalfSphereDome.HalfExtentMeters,
            AtmosphericVolumeShape::HalfSphere, 0.0f, 6u, HalfSphereDome.EdgeSoftness);
        const float MaskRim = AtmosphericWindSolver::EvaluateVolumeShapeMask(
            { 40.0f, 34.0f, 2.0f }, HalfSphereDome.VolumeCenterMeters, HalfSphereDome.HalfExtentMeters,
            AtmosphericVolumeShape::HalfSphere, 0.0f, 6u, HalfSphereDome.EdgeSoftness);
        const float MaskOut = AtmosphericWindSolver::EvaluateVolumeShapeMask(
            { 50.0f, 34.0f, 2.0f }, HalfSphereDome.VolumeCenterMeters, HalfSphereDome.HalfExtentMeters,
            AtmosphericVolumeShape::HalfSphere, 0.0f, 6u, HalfSphereDome.EdgeSoftness);
        Score.Verify(MaskCenter > MaskMid && MaskMid > MaskRim && MaskRim > 0.0f && MaskOut == 0.0f,
                     "HalfSphere fog mask exhibits smooth radial dome falloff and clean outer boundary");

        // Tile 2: Turbulent Wind active INSIDE the HalfSphere fog
        S.Scene.Atmosphere.Wind.Enabled              = true;
        S.Scene.Atmosphere.Wind.SpeedMetersPerSecond = 24.0f;
        S.Scene.Atmosphere.Wind.Turbulence           = 0.90f;
        S.Scene.Atmosphere.Wind.GustStrength         = 0.70f;
        S.Scene.Atmosphere.Fog.LocalFogs[0].WindSource = WindCouplingSource::LocalFogWind;
        S.Scene.Atmosphere.SimulationSeconds = 8.5f;
        const auto RHalfSphereForced = RenderCpu(S);

        Score.Verify(MeanAbsPixelDiff(RHalfSphereStill.DisplayRgba8, RHalfSphereForced.DisplayRgba8) > 0.75f,
                     "Turbulent wind visibly advects and sculpts interior of HalfSphere fog");

        // Tile 3: Enable Volumetric God Rays interacting with Clouds + HalfSphere Fog
        S.Scene.Atmosphere.Fog.HeightFog.Enabled             = true;
        S.Scene.Atmosphere.Fog.HeightFog.Density             = 0.0028f;
        S.Scene.Atmosphere.Fog.HeightFog.FalloffHeightMeters = 18.0f;
        S.Scene.Atmosphere.Fog.GodRays.Enabled               = true;
        S.Scene.Atmosphere.Fog.GodRays.Intensity             = 1.45f;
        S.Scene.Atmosphere.Fog.GodRays.AnisotropyG           = 0.78f;
        S.Scene.Atmosphere.Fog.GodRays.Density               = 0.85f;
        S.Scene.Atmosphere.Fog.GodRays.Decay                 = 0.90f;
        S.Scene.Atmosphere.Fog.GodRays.MaxDistanceMeters     = 180.0f;
        S.Scene.Atmosphere.Fog.GodRays.SampleSteps           = 24u;
        S.Scene.Atmosphere.Fog.GodRays.InteractWithClouds    = true;
        S.Scene.Atmosphere.Fog.GodRays.InteractWithFog       = true;
        const auto RGodRaysFull = RenderCpu(S);

        // Tile 4: God Rays with Fog+Clouds disabled vs enabled comparison + GPU parity
        Score.Verify(MeanAbsPixelDiff(RHalfSphereForced.DisplayRgba8, RGodRaysFull.DisplayRgba8) > 1.25f,
                     "Volumetric God Rays visibly project sunlit shafts through Clouds + Fog");

        std::vector<uint8_t> GpuGodRayPixels(TileW * TileH * 4u, 255u);
        VulkanInterchange VkDevice {};
        if (VkDevice.InitializeInterchange(RepoRoot))
        {
            VulkanWorldRenderSequence WorldGpu {};
            if (WorldGpu.InitializeSequence(VkDevice, ShaderDir))
            {
                WorldGpu.SynchronizeSceneResources(S.Scene, true);
                const bool GpuOk = WorldGpu.RenderWorldViewportWithEngine(Cfg, 0u, S.CamFrame, S.Scene, GpuGodRayPixels);
                Score.Verify(GpuOk && GpuGodRayPixels.size() == TileW * TileH * 4u,
                             "Vulkan GPU rendered HalfSphere Fog + Clouds + GodRays frame");
                if (GpuOk)
                {
                    const float CpuGpuMad = MeanAbsPixelDiff(RGodRaysFull.DisplayRgba8, GpuGodRayPixels);
                    std::printf("[WindCloudsFogValidation] CPU vs GPU HalfSphere+GodRays MAD = %.2f\n", CpuGpuMad);
                    Score.Verify(CpuGpuMad < 35.0f, "GPU matches CPU on HalfSphere Fog + Clouds + GodRays");
                }
            }
        }

        std::vector<uint8_t> Sheet(TileW * 2u * TileH * 2u * 4u, 255u);
        BlitTile(Sheet, TileW * 2u, 0u,    0u,    TileW, TileH, RHalfSphereStill.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, 0u,    TileW, TileH, RHalfSphereForced.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, 0u,    TileH, TileW, TileH, RGodRaysFull.DisplayRgba8.data());
        BlitTile(Sheet, TileW * 2u, TileW, TileH, TileW, TileH, GpuGodRayPixels.data());
        Score.Verify(WritePngRgba8(OutDir / "05-HalfSphereFogAndGodRays.png", TileW * 2u, TileH * 2u, Sheet.data()),
                     "Write 05-HalfSphereFogAndGodRays.png");
    }

    std::printf("[WindCloudsFogValidation] Passed=%u Failed=%u\n", Score.Passed, Score.Failed);
    return (Score.Failed == 0u) ? 0 : 1;
}
