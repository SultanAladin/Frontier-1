/*==========================================================================================================================================
                                                 EMISSIVEGISHOOTOUTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 3-Algorithm Emissive Lighting Shootout on ControlVehicle (Zero Cone GI, Zero Jitter, Zero Denoiser, Unchanged 72x72x39 SDF):
//    - Algorithm 1: Analytic Emissive-Triangle LTC + Hybrid Clipmap SDF Penumbra + Deterministic Surfel 1-Bounce Radiosity
//    - Algorithm 2: Hierarchical Emissive Lightcuts (Walter et al. 2005) + Virtual Spherical Lights (VSL, Hašan et al. 2009)
//    - Algorithm 3: Exact Arvo/Baum Triangle Form-Factor Direct + Order-2 L2 Spherical Harmonic (SH9) Multi-Bounce Field
//    - Ground Truth: 64-SPP 4-Bounce Path Tracer with Explicit Emissive-Triangle NEE + MIS (No Denoiser)

#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/VoxelSdfClipmap.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/Visibility/ClusterCullEncoder.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/GeometricRaster/Visibility/VisibilityClusterRasterization.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"
#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ExternalPackages/stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <span>
#include <thread>
#include <vector>

namespace
{

using namespace Frontier;

[[nodiscard]] inline Mathematics::Axis3D StudioToWorld(const float P[3]) noexcept
{
    return { P[0], -P[2], P[1] };
}

[[nodiscard]] float EncodeLinearToSrgb(float V) noexcept
{
    const float C = std::clamp(V, 0.0f, 1.0f);
    return (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
}

[[nodiscard]] Mathematics::LinearRGB ApplyAcesFilmic(Mathematics::LinearRGB Hdr) noexcept
{
    const auto Curve = [](float X) noexcept -> float
    {
        const float V = std::max(0.0f, X * 0.85f);
        return std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Curve(Hdr.R), Curve(Hdr.G), Curve(Hdr.B) };
}

void HdrToRgba8(
    uint32_t                               W,
    uint32_t                               H,
    std::span<const Mathematics::LinearRGB> Hdr,
    std::vector<uint8_t>&                  OutRgba8)
{
    OutRgba8.resize(static_cast<std::size_t>(W) * H * 4u);
    for (std::size_t I = 0u; I < Hdr.size(); ++I)
    {
        const auto T = ApplyAcesFilmic(Hdr[I]);
        OutRgba8[I * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(T.R) * 255.0f));
        OutRgba8[I * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(T.G) * 255.0f));
        OutRgba8[I * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(T.B) * 255.0f));
        OutRgba8[I * 4u + 3u] = 255u;
    }
}

void SavePngRgba8(const std::filesystem::path& OutPath, uint32_t W, uint32_t H, std::span<const uint8_t> Rgba8)
{
    std::filesystem::create_directories(OutPath.parent_path());
    stbi_write_png(OutPath.string().c_str(), static_cast<int>(W), static_cast<int>(H), 4, Rgba8.data(), static_cast<int>(W * 4u));
}

[[nodiscard]] std::vector<uint8_t> MakeQuadPanel2x2(
    uint32_t                 W,
    uint32_t                 H,
    std::span<const uint8_t> TL,
    std::span<const uint8_t> TR,
    std::span<const uint8_t> BL,
    std::span<const uint8_t> BR,
    uint32_t                 Gap = 6u)
{
    const uint32_t OutW = W * 2u + Gap;
    const uint32_t OutH = H * 2u + Gap;
    std::vector<uint8_t> Out(static_cast<std::size_t>(OutW) * OutH * 4u, 22u);
    for (uint32_t Y = 0u; Y < OutH; ++Y)
    {
        for (uint32_t X = 0u; X < OutW; ++X)
        {
            const std::size_t DO = (static_cast<std::size_t>(Y) * OutW + X) * 4u;
            Out[DO + 0u] = 240u;
            Out[DO + 1u] = 168u;
            Out[DO + 2u] = 48u;
            Out[DO + 3u] = 255u;
        }
    }
    const auto CopyTile = [&](std::span<const uint8_t> Src, uint32_t OX, uint32_t OY)
    {
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const std::size_t SO = (static_cast<std::size_t>(Y) * W + X) * 4u;
                const std::size_t DO = (static_cast<std::size_t>(OY + Y) * OutW + (OX + X)) * 4u;
                for (std::size_t C = 0u; C < 4u; ++C) Out[DO + C] = Src[SO + C];
            }
        }
    };
    CopyTile(TL, 0u, 0u);
    CopyTile(TR, W + Gap, 0u);
    CopyTile(BL, 0u, H + Gap);
    CopyTile(BR, W + Gap, H + Gap);
    return Out;
}

[[nodiscard]] float ComputeRmseSrgb(std::span<const uint8_t> A, std::span<const uint8_t> B) noexcept
{
    if (A.size() != B.size() || A.empty()) return 999.0f;
    double Sum = 0.0;
    const std::size_t Pix = A.size() / 4u;
    for (std::size_t I = 0u; I < Pix; ++I)
    {
        for (std::size_t C = 0u; C < 3u; ++C)
        {
            const double D = (static_cast<double>(A[I * 4u + C]) - static_cast<double>(B[I * 4u + C])) / 255.0;
            Sum += D * D;
        }
    }
    return static_cast<float>(std::sqrt(Sum / static_cast<double>(Pix * 3u)));
}

[[nodiscard]] float ComputeRmseHdr(
    std::span<const Mathematics::LinearRGB> A,
    std::span<const Mathematics::LinearRGB> B) noexcept
{
    if (A.size() != B.size() || A.empty()) return 999.0f;
    double Sum = 0.0;
    for (std::size_t I = 0u; I < A.size(); ++I)
    {
        const double DR = static_cast<double>(A[I].R) - static_cast<double>(B[I].R);
        const double DG = static_cast<double>(A[I].G) - static_cast<double>(B[I].G);
        const double DB = static_cast<double>(A[I].B) - static_cast<double>(B[I].B);
        Sum += (DR * DR + DG * DG + DB * DB) / 3.0;
    }
    return static_cast<float>(std::sqrt(Sum / static_cast<double>(A.size())));
}

[[nodiscard]] float ComputeMeanLum(std::span<const Mathematics::LinearRGB> Buf) noexcept
{
    if (Buf.empty()) return 0.0f;
    double Sum = 0.0;
    for (const auto& C : Buf)
    {
        Sum += 0.2126 * static_cast<double>(C.R) + 0.7152 * static_cast<double>(C.G) + 0.0722 * static_cast<double>(C.B);
    }
    return static_cast<float>(Sum / static_cast<double>(Buf.size()));
}

// Measures spatial roughness / streak energy on flat regions (mean neighbor gradient magnitude difference vs reference)
[[nodiscard]] float ComputeGradientRmseSrgb(
    uint32_t                 W,
    uint32_t                 H,
    std::span<const uint8_t> A,
    std::span<const uint8_t> Ref) noexcept
{
    if (W < 3u || H < 3u || A.size() != Ref.size()) return 999.0f;
    double Sum = 0.0;
    std::size_t Cnt = 0u;
    for (uint32_t Y = 1u; Y + 1u < H; ++Y)
    {
        for (uint32_t X = 1u; X + 1u < W; ++X)
        {
            const std::size_t C  = (static_cast<std::size_t>(Y) * W + X) * 4u;
            const std::size_t RX = (static_cast<std::size_t>(Y) * W + (X + 1u)) * 4u;
            const std::size_t DY = (static_cast<std::size_t>(Y + 1u) * W + X) * 4u;
            for (std::size_t Ch = 0u; Ch < 3u; ++Ch)
            {
                const double Gax = (static_cast<double>(A[RX + Ch]) - static_cast<double>(A[C + Ch])) / 255.0;
                const double Gay = (static_cast<double>(A[DY + Ch]) - static_cast<double>(A[C + Ch])) / 255.0;
                const double Grx = (static_cast<double>(Ref[RX + Ch]) - static_cast<double>(Ref[C + Ch])) / 255.0;
                const double Gry = (static_cast<double>(Ref[DY + Ch]) - static_cast<double>(Ref[C + Ch])) / 255.0;
                Sum += (Gax - Grx) * (Gax - Grx) + (Gay - Gry) * (Gay - Gry);
                ++Cnt;
            }
        }
    }
    return static_cast<float>(std::sqrt(Sum / static_cast<double>(std::max<std::size_t>(1u, Cnt))));
}

template <typename Fn>
void ParallelForRows(uint32_t H, Fn&& Body)
{
    const uint32_t TC = std::min(H, std::max(1u, std::thread::hardware_concurrency()));
    if (TC <= 1u || H <= 4u)
    {
        Body(0u, H);
        return;
    }
    std::vector<std::thread> Workers;
    Workers.reserve(TC);
    const uint32_t Chunk = (H + TC - 1u) / TC;
    for (uint32_t T = 0u; T < TC; ++T)
    {
        const uint32_t Y0 = T * Chunk;
        const uint32_t Y1 = std::min(H, Y0 + Chunk);
        if (Y0 < Y1) Workers.emplace_back([&, Y0, Y1]() { Body(Y0, Y1); });
    }
    for (auto& W : Workers) W.join();
}

//========================================================================================================================
// SCENE SETUP (IDENTICAL CONTROLVEHICLE + STUDIO ROOM, ZERO SUN/SKY/AREA/POINT, 72x72x39 SDF)
//========================================================================================================================

struct VehicleMeshBuckets
{
    std::array<GeometrySpace::PolygonNetwork, 16u>     SlotNets {};
    std::array<SpatialAcceleration::ClusterPyramid, 16u> SlotPyrs {};
    std::array<uint16_t, 16u>                          SlotToMatReg {};
    GeometrySpace::PolygonNetwork                      CombinedLocalNet {};
    SpatialAcceleration::WideBoundingTree              CombinedLocalBvh {};
    DistanceFieldSpace::LocalGeometrySdfAsset            LocalSdfAsset {};
};

struct StudioSceneBundle
{
    GeometricRaster::WorldSceneSpecification         Scene {};
    VehicleMeshBuckets                               Vehicle {};
    std::vector<GeometrySpace::PolygonNetwork>       StaticNets {};
    std::vector<SpatialAcceleration::ClusterPyramid> StaticPyrs {};
    std::vector<uint16_t>                            StaticSlots {};
    std::vector<Mathematics::Axis3D>                 StaticCenters {};
    GeometrySpace::PolygonNetwork                    StaticOnlyNet {};
    uint16_t SlotPlaster = 0u;
    uint16_t SlotCrimson = 0u;
    uint16_t SlotEmerald = 0u;
    uint16_t SlotCobalt  = 0u;
};

[[nodiscard]] StudioSceneBundle BuildEmissiveOnlyControlVehicleScene(Mathematics::Axis3D PoseA)
{
    StudioSceneBundle Bundle {};
    auto& Scene = Bundle.Scene;

    const auto MakeDiffuse = [](Mathematics::LinearRGB Col, float Rough) noexcept -> MaterialSpace::SurfaceMaterial
    {
        MaterialSpace::SurfaceMaterial M {};
        M.Diffuse.BaseColor        = Col;
        M.Diffuse.BaseWeight       = 1.0f;
        M.Diffuse.DiffuseRoughness = 0.08f;
        M.Specular.Roughness       = Rough;
        M.Specular.SpecularWeight  = 0.35f;
        return M;
    };

    Bundle.SlotPlaster = Scene.MaterialRegister.RegisterMaterial(MakeDiffuse({ 0.85f, 0.85f, 0.87f }, 0.82f));
    Bundle.SlotCrimson = Scene.MaterialRegister.RegisterMaterial(MakeDiffuse({ 0.85f, 0.08f, 0.07f }, 0.78f));
    Bundle.SlotEmerald = Scene.MaterialRegister.RegisterMaterial(MakeDiffuse({ 0.10f, 0.72f, 0.25f }, 0.76f));
    Bundle.SlotCobalt  = Scene.MaterialRegister.RegisterMaterial(MakeDiffuse({ 0.15f, 0.36f, 0.85f }, 0.45f));

    auto MakeBody = [](Mathematics::LinearRGB Col, float Metal, float Rough, float Coat) noexcept
    {
        MaterialSpace::SurfaceMaterial M {};
        M.Diffuse.BaseColor        = Col;
        M.Diffuse.BaseWeight       = 1.0f;
        M.Specular.Metalness       = Metal;
        M.Specular.Roughness       = Rough;
        M.Specular.SpecularWeight  = 0.75f;
        M.Clearcoat.CoatWeight     = Coat;
        M.Clearcoat.CoatRoughness  = 0.06f;
        return M;
    };
    MaterialSpace::SurfaceMaterial BodyMat   = MakeBody({ 0.14f, 0.42f, 0.88f }, 0.05f, 0.22f, 0.80f);
    MaterialSpace::SurfaceMaterial GlassMat  = MakeBody({ 0.05f, 0.09f, 0.14f }, 0.00f, 0.04f, 1.00f);
    MaterialSpace::SurfaceMaterial PlasticMat = MakeBody({ 0.16f, 0.17f, 0.19f }, 0.00f, 0.58f, 0.0f);
    MaterialSpace::SurfaceMaterial TyreMat   = MakeBody({ 0.08f, 0.08f, 0.09f }, 0.00f, 0.75f, 0.0f);
    MaterialSpace::SurfaceMaterial HubMat    = MakeBody({ 0.78f, 0.80f, 0.84f }, 0.92f, 0.18f, 0.0f);
    MaterialSpace::SurfaceMaterial BrakeMat  = MakeBody({ 0.45f, 0.14f, 0.12f }, 0.40f, 0.36f, 0.0f);
    MaterialSpace::SurfaceMaterial TrimMat   = MakeBody({ 0.09f, 0.10f, 0.12f }, 0.25f, 0.34f, 0.0f);

    MaterialSpace::SurfaceMaterial HeadlightMat {};
    HeadlightMat.Diffuse.BaseColor                      = { 1.0f, 0.95f, 0.85f };
    HeadlightMat.Diffuse.BaseWeight                     = 1.0f;
    HeadlightMat.Specular.Roughness                     = 0.10f;
    HeadlightMat.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
    HeadlightMat.PorosityLight.EmissionTint             = { 1.0f, 0.92f, 0.75f };
    HeadlightMat.PorosityLight.EmissionLuminanceNits    = 9500.0f;

    MaterialSpace::SurfaceMaterial TaillightMat {};
    TaillightMat.Diffuse.BaseColor                      = { 0.98f, 0.14f, 0.08f };
    TaillightMat.Diffuse.BaseWeight                     = 1.0f;
    TaillightMat.Specular.Roughness                     = 0.12f;
    TaillightMat.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
    TaillightMat.PorosityLight.EmissionTint             = { 1.0f, 0.10f, 0.04f };
    TaillightMat.PorosityLight.EmissionLuminanceNits    = 8000.0f;

    Bundle.Vehicle.SlotToMatReg[6u]  = Scene.MaterialRegister.RegisterMaterial(BodyMat);
    Bundle.Vehicle.SlotToMatReg[7u]  = Scene.MaterialRegister.RegisterMaterial(GlassMat);
    Bundle.Vehicle.SlotToMatReg[8u]  = Scene.MaterialRegister.RegisterMaterial(PlasticMat);
    Bundle.Vehicle.SlotToMatReg[9u]  = Scene.MaterialRegister.RegisterMaterial(TyreMat);
    Bundle.Vehicle.SlotToMatReg[10u] = Scene.MaterialRegister.RegisterMaterial(HubMat);
    Bundle.Vehicle.SlotToMatReg[11u] = Scene.MaterialRegister.RegisterMaterial(BrakeMat);
    Bundle.Vehicle.SlotToMatReg[12u] = Scene.MaterialRegister.RegisterMaterial(TrimMat);
    Bundle.Vehicle.SlotToMatReg[13u] = Scene.MaterialRegister.RegisterMaterial(HeadlightMat);
    Bundle.Vehicle.SlotToMatReg[14u] = Scene.MaterialRegister.RegisterMaterial(TaillightMat);

    const auto& Geom = ParticleSpace::QueryControlVehicleGeometrySet();
    for (const auto& Tri : Geom.Triangles)
    {
        const uint32_t Code = std::clamp(Tri.MaterialSlotCode, 6u, 14u);
        const Mathematics::Axis3D A = StudioToWorld(Tri.PositionA);
        const Mathematics::Axis3D B = StudioToWorld(Tri.PositionB);
        const Mathematics::Axis3D C = StudioToWorld(Tri.PositionC);

        auto& Net = Bundle.Vehicle.SlotNets[Code];
        const uint32_t V0 = Net.AppendVertex(A);
        const uint32_t V1 = Net.AppendVertex(B);
        const uint32_t V2 = Net.AppendVertex(C);
        (void)Net.AppendFace(std::array<uint32_t, 3u>{ V0, V1, V2 }, {}, {}, 0u, Bundle.Vehicle.SlotToMatReg[Code]);

        const uint32_t LV0 = Bundle.Vehicle.CombinedLocalNet.AppendVertex(A);
        const uint32_t LV1 = Bundle.Vehicle.CombinedLocalNet.AppendVertex(B);
        const uint32_t LV2 = Bundle.Vehicle.CombinedLocalNet.AppendVertex(C);
        (void)Bundle.Vehicle.CombinedLocalNet.AppendFace(
            std::array<uint32_t, 3u>{ LV0, LV1, LV2 }, {}, {}, 0u, Bundle.Vehicle.SlotToMatReg[Code]);
    }
    for (uint32_t Code = 6u; Code <= 14u; ++Code)
    {
        if (!Bundle.Vehicle.SlotNets[Code].Faces.empty())
        {
            (void)Bundle.Vehicle.SlotPyrs[Code].BuildFromPolygonNetwork(Bundle.Vehicle.SlotNets[Code], 3u);
        }
    }
    (void)Bundle.Vehicle.CombinedLocalBvh.BuildFromPolygonNetwork(Bundle.Vehicle.CombinedLocalNet);
    (void)Bundle.Vehicle.LocalSdfAsset.BakeFromLocalTree(
        "ControlVehicle", Bundle.Vehicle.CombinedLocalBvh, Scene.MaterialRegister, 0.048f, 0.30f);

    const auto AddPrimaryTile = [&](Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt, uint16_t MatSlot)
    {
        GeometrySpace::PolygonNetwork Net {};
        GeometrySpace::AppendBoxPrimitive(Net, { 0.0f, 0.0f, 0.0f }, HalfExt, {}, {}, 0u, MatSlot);
        SpatialAcceleration::ClusterPyramid Pyr {};
        (void)Pyr.BuildFromPolygonNetwork(Net, 3u);
        Bundle.StaticNets.push_back(std::move(Net));
        Bundle.StaticPyrs.push_back(std::move(Pyr));
        Bundle.StaticSlots.push_back(MatSlot);
        Bundle.StaticCenters.push_back(Center);
    };

    GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, {  0.0f,  0.0f, -0.08f }, { 8.40f, 6.30f, 0.08f }, {}, {}, 0u, Bundle.SlotPlaster);
    GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, {  0.0f,  4.10f, 1.60f }, { 8.40f, 0.14f, 1.80f }, {}, {}, 0u, Bundle.SlotPlaster);
    GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, { -5.20f, 0.0f,  1.60f }, { 0.14f, 5.20f, 1.80f }, {}, {}, 0u, Bundle.SlotCrimson);
    GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, {  5.20f, 0.0f,  1.60f }, { 0.14f, 5.20f, 1.80f }, {}, {}, 0u, Bundle.SlotEmerald);
    GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, {  3.45f, 1.05f, 1.15f }, { 0.24f, 0.24f, 1.15f }, {}, {}, 0u, Bundle.SlotCobalt);
    GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, { -2.85f, 1.95f, 1.15f }, { 0.28f, 0.28f, 1.15f }, {}, {}, 0u, Bundle.SlotPlaster);

    for (int IX = -4; IX <= 3; ++IX)
    {
        for (int IY = -3; IY <= 2; ++IY)
        {
            const float CX = (static_cast<float>(IX) + 0.5f) * 2.10f;
            const float CY = (static_cast<float>(IY) + 0.5f) * 2.10f;
            AddPrimaryTile({ CX, CY, -0.08f }, { 1.05f, 1.05f, 0.08f }, Bundle.SlotPlaster);
        }
    }
    for (int IX = -4; IX <= 3; ++IX)
    {
        const float CX = (static_cast<float>(IX) + 0.5f) * 2.10f;
        AddPrimaryTile({ CX, 4.10f, 1.60f }, { 1.05f, 0.14f, 1.80f }, Bundle.SlotPlaster);
    }
    for (int IY = -3; IY <= 2; ++IY)
    {
        const float CY = (static_cast<float>(IY) + 0.5f) * 1.55f;
        AddPrimaryTile({ -5.20f, CY, 1.60f }, { 0.14f, 0.775f, 1.80f }, Bundle.SlotCrimson);
        AddPrimaryTile({  5.20f, CY, 1.60f }, { 0.14f, 0.775f, 1.80f }, Bundle.SlotEmerald);
    }
    AddPrimaryTile({  3.45f, 1.05f, 1.15f }, { 0.24f, 0.24f, 1.15f }, Bundle.SlotCobalt);
    AddPrimaryTile({ -2.85f, 1.95f, 1.15f }, { 0.28f, 0.28f, 1.15f }, Bundle.SlotPlaster);

    // Keep exact 72x72x39 / 48^3 voxel resolution (NO voxel increase!)
    Scene.ClipmapConfig.Clip0CenterWorld      = { 0.0f, 0.0f, 1.35f };
    Scene.ClipmapConfig.Clip0HalfExtentMeters = 5.45f;
    Scene.ClipmapConfig.Clip0Resolution       = 72u;
    Scene.ClipmapConfig.Clip1CenterWorld      = { 0.0f, 0.0f, 2.00f };
    Scene.ClipmapConfig.Clip1HalfExtentMeters = 12.00f;
    Scene.ClipmapConfig.Clip1Resolution       = 48u;
    Scene.ClipmapConfig.RadiosityBounces      = 2u;

    Scene.Atmosphere.Medium.AtmosphereEnabled = false;
    Scene.Sun.SolarIrradiance                 = { 0.0f, 0.0f, 0.0f };
    Scene.SkyRadiance                         = { 0.0f, 0.0f, 0.0f };

    // Build StaticOnly Tree first for Clipmap SDF static bake, then attach dynamic vehicle
    (void)Scene.SceneTree.BuildFromPolygonNetwork(Bundle.StaticOnlyNet);
    DistanceFieldSpace::DynamicLocalSdfInstance DynInst {};
    DynInst.Asset     = &Bundle.Vehicle.LocalSdfAsset;
    DynInst.Transform = DistanceFieldSpace::RigidTransform3D::FromTranslationYawPitchRoll(PoseA, 0.0f, 0.0f, 0.0f, 1.0f);
    DynInst.UpdateWorldBounds();
    Scene.DynamicSdfObjects = { DynInst };

    Scene.ClusterInstances.clear();
    for (std::size_t I = 0u; I < Bundle.StaticPyrs.size(); ++I)
    {
        Scene.ClusterInstances.push_back({
            &Bundle.StaticPyrs[I], Bundle.StaticCenters[I], Bundle.StaticCenters[I], 1.0f, Bundle.StaticSlots[I], false
        });
    }
    for (uint32_t Code = 6u; Code <= 14u; ++Code)
    {
        if (!Bundle.Vehicle.SlotPyrs[Code].Clusters.empty())
        {
            Scene.ClusterInstances.push_back({
                &Bundle.Vehicle.SlotPyrs[Code], PoseA, PoseA, 1.0f, Bundle.Vehicle.SlotToMatReg[Code], false
            });
        }
    }

    return Bundle;
}

void AttachVehicleToSceneTree(StudioSceneBundle& Bundle, Mathematics::Axis3D PoseA)
{
    GeometrySpace::PolygonNetwork FullNet = Bundle.StaticOnlyNet;
    const auto& Geom = ParticleSpace::QueryControlVehicleGeometrySet();
    for (const auto& Tri : Geom.Triangles)
    {
        const uint32_t Code    = std::clamp(Tri.MaterialSlotCode, 6u, 14u);
        const uint16_t RegSlot = Bundle.Vehicle.SlotToMatReg[Code];
        const Mathematics::Axis3D A = StudioToWorld(Tri.PositionA) + PoseA;
        const Mathematics::Axis3D B = StudioToWorld(Tri.PositionB) + PoseA;
        const Mathematics::Axis3D C = StudioToWorld(Tri.PositionC) + PoseA;
        const uint32_t V0 = FullNet.AppendVertex(A);
        const uint32_t V1 = FullNet.AppendVertex(B);
        const uint32_t V2 = FullNet.AppendVertex(C);
        (void)FullNet.AppendFace(std::array<uint32_t, 3u>{ V0, V1, V2 }, {}, {}, 0u, RegSlot);
    }
    (void)Bundle.Scene.SceneTree.BuildFromPolygonNetwork(FullNet);
}

//========================================================================================================================
// SHARED EMISSIVE TRIANGLE EXTRACTION + EXACT POLYGONAL CONTOUR INTEGRAL (BAUM 1989 / ARVO 1995 / HEITZ 2016)
//========================================================================================================================

struct EmissiveTri
{
    Mathematics::Axis3D    V0 {};
    Mathematics::Axis3D    V1 {};
    Mathematics::Axis3D    V2 {};
    Mathematics::Axis3D    Centroid {};
    Mathematics::Axis3D    Normal {};
    float                  Area = 0.0f;
    Mathematics::LinearRGB Radiance {};
    float                  TotalFlux = 0.0f; // Area * Pi * Luma
};

[[nodiscard]] std::vector<EmissiveTri> ExtractEmissiveTriangles(
    const SpatialAcceleration::WideBoundingTree&  SceneTree,
    const MaterialSpace::CompactMaterialRegister& MatReg)
{
    std::vector<EmissiveTri> Out;
    const uint32_t MatCount = MatReg.QuerySlotCount();
    for (const auto& Prim : SceneTree.Primitives)
    {
        if (Prim.MaterialOrdinal >= MatCount) continue;
        const auto& Mat = MatReg.InspectAuthoredMaterial(Prim.MaterialOrdinal);
        const auto Emit = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        const float Lum = 0.2126f * Emit.R + 0.7152f * Emit.G + 0.0722f * Emit.B;
        if (Lum <= 1.0e-5f) continue;

        const Mathematics::Axis3D P0 = Prim.Corner0;
        const Mathematics::Axis3D P1 = Prim.Corner0 + Prim.Edge01;
        const Mathematics::Axis3D P2 = Prim.Corner0 + Prim.Edge02;
        const Mathematics::Axis3D CrossN = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
        const float DoubleArea = Mathematics::EvaluateLength(CrossN);
        if (DoubleArea <= 1.0e-7f) continue;

        const float Area = DoubleArea * 0.5f;
        Out.push_back(EmissiveTri{
            P0, P1, P2,
            (P0 + P1 + P2) * (1.0f / 3.0f),
            CrossN * (1.0f / DoubleArea),
            Area,
            Emit,
            Area * Constants::PiFloat * Lum,
        });
    }
    return Out;
}

[[nodiscard]] float IntegrateSphericalEdgeZ(Mathematics::Axis3D A, Mathematics::Axis3D B) noexcept
{
    const float X    = std::clamp(Mathematics::EvaluateDot(A, B), -0.99999f, 0.99999f);
    const float AbsX = std::fabs(X);
    const float Num  = 5.42031f + (3.12829f + 0.0902326f * AbsX) * AbsX;
    const float Den  = 3.45068f + (4.18814f + AbsX) * AbsX;
    float ThetaOverSin = Num / Den;
    if (X < 0.0f)
    {
        const float SinT = std::sqrt(std::max(1.0e-8f, 1.0f - X * X));
        ThetaOverSin = Constants::PiFloat / SinT - ThetaOverSin;
    }
    return (A.X * B.Y - A.Y * B.X) * ThetaOverSin;
}

// Exact Sutherland-Hodgman horizon-clipped 3-vertex polygon cosine irradiance form factor E / (pi * L) in [0..1]
[[nodiscard]] float IntegrateClippedTriangleCosineFormFactor(
    const std::array<Mathematics::Axis3D, 3u>& LocalTri) noexcept
{
    std::array<Mathematics::Axis3D, 6u> Clipped {};
    uint32_t Count = 0u;
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const auto& Cur  = LocalTri[I];
        const auto& Next = LocalTri[(I + 1u) % 3u];
        const bool CurUp  = (Cur.Z >= 0.0f);
        const bool NextUp = (Next.Z >= 0.0f);
        if (CurUp) Clipped[Count++] = Cur;
        if (CurUp != NextUp)
        {
            const float T = Cur.Z / (Cur.Z - Next.Z);
            Mathematics::Axis3D Hit = Cur + (Next - Cur) * T;
            Hit.Z = 0.0f;
            Clipped[Count++] = Hit;
        }
    }
    if (Count < 3u) return 0.0f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        Clipped[I] = Mathematics::NormalizeAxis(Clipped[I], { 0.0f, 0.0f, 1.0f });
    }
    float Sum = 0.0f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        Sum += IntegrateSphericalEdgeZ(Clipped[I], Clipped[(I + 1u) % Count]);
    }
    return std::clamp(std::fabs(Sum) / Constants::TauFloat, 0.0f, 1.0f);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateTriLtcDirect(
    const EmissiveTri&                   Tri,
    Mathematics::Axis3D                  Pos,
    Mathematics::Axis3D                  Nrm,
    Mathematics::Axis3D                  ViewDir,
    const MaterialSpace::SurfaceMaterial& Mat,
    uint32_t                             LobeMask,
    float                                Visibility) noexcept
{
    if (Visibility <= 1.0e-5f) return {};
    // Reject shaded point behind single-sided emitter plane
    if (Mathematics::EvaluateDot(Pos - Tri.Centroid, Tri.Normal) <= 1.0e-4f) return {};

    const float NdotV = std::clamp(Mathematics::EvaluateDot(Nrm, ViewDir), 1.0e-4f, 1.0f);
    Mathematics::Axis3D T1 = ViewDir - Nrm * NdotV;
    if (Mathematics::EvaluateLengthSquared(T1) <= 1.0e-6f)
    {
        const Mathematics::Axis3D Up = (std::fabs(Nrm.Z) < 0.9f) ? Mathematics::Axis3D{ 0, 0, 1 } : Mathematics::Axis3D{ 1, 0, 0 };
        T1 = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Up, Nrm));
    }
    else
    {
        T1 = Mathematics::NormalizeAxis(T1);
    }
    const Mathematics::Axis3D T2 = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Nrm, T1));

    const std::array<Mathematics::Axis3D, 3u> WorldV {{ Tri.V0, Tri.V1, Tri.V2 }};
    std::array<Mathematics::Axis3D, 3u> DiffLocal {};
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const auto R = WorldV[I] - Pos;
        DiffLocal[I] = {
            Mathematics::EvaluateDot(R, T1),
            Mathematics::EvaluateDot(R, T2),
            Mathematics::EvaluateDot(R, Nrm),
        };
    }
    const float DiffFF = IntegrateClippedTriangleCosineFormFactor(DiffLocal);

    const float Alpha = std::clamp(Mat.Specular.Roughness * Mat.Specular.Roughness, 0.004f, 1.0f);
    const float SinV  = std::sqrt(std::max(0.0f, 1.0f - NdotV * NdotV));
    const float InvSX = 1.0f / std::max(0.04f, Alpha * (0.85f + 0.15f * NdotV));
    const float InvSY = 1.0f / std::max(0.04f, Alpha);
    const float Shear = -SinV / std::max(0.08f, NdotV + Alpha);

    std::array<Mathematics::Axis3D, 3u> SpecLocal {};
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const auto& D = DiffLocal[I];
        const float MX = -D.X * NdotV + D.Z * SinV;
        const float MZ =  D.X * SinV  + D.Z * NdotV;
        SpecLocal[I] = { (MX + Shear * MZ * Alpha) * InvSX, D.Y * InvSY, MZ };
    }
    const float SpecFF = IntegrateClippedTriangleCosineFormFactor(SpecLocal);

    const auto& Sheet = MaterialSpace::InspectGlobalDirectionalAlbedoSheet();
    const auto  AlbO  = MaterialSpace::SampleDirectionalAlbedo(Sheet, NdotV, Mat.Specular.Roughness);
    const float DielF0 = ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                         ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                         Mat.Specular.SpecularWeight;
    const Mathematics::LinearRGB SpecF0 {
        DielF0 * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.R * Mat.Specular.Metalness,
        DielF0 * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.G * Mat.Specular.Metalness,
        DielF0 * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.B * Mat.Specular.Metalness,
    };
    const Mathematics::LinearRGB SpecAlb {
        SpecF0.R * AlbO.DirectionalAlbedoE + (1.0f - SpecF0.R) * (1.0f - AlbO.DirectionalAlbedoE) * 0.25f,
        SpecF0.G * AlbO.DirectionalAlbedoE + (1.0f - SpecF0.G) * (1.0f - AlbO.DirectionalAlbedoE) * 0.25f,
        SpecF0.B * AlbO.DirectionalAlbedoE + (1.0f - SpecF0.B) * (1.0f - AlbO.DirectionalAlbedoE) * 0.25f,
    };
    const float DiffScale = ((LobeMask & MaterialSpace::LobeDiffuseEON) != 0u)
        ? ((1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight) * Mat.Diffuse.BaseWeight)
        : 0.0f;

    return {
        Visibility * Tri.Radiance.R * (DiffScale * Mat.Diffuse.BaseColor.R * DiffFF + SpecAlb.R * SpecFF),
        Visibility * Tri.Radiance.G * (DiffScale * Mat.Diffuse.BaseColor.G * DiffFF + SpecAlb.G * SpecFF),
        Visibility * Tri.Radiance.B * (DiffScale * Mat.Diffuse.BaseColor.B * DiffFF + SpecAlb.B * SpecFF),
    };
}

// Deterministic Studio Surfels for 1-bounce indirect radiosity (floor, walls, pillars)
struct StudioSurfel
{
    Mathematics::Axis3D    Position {};
    Mathematics::Axis3D    Normal {};
    Mathematics::LinearRGB DirectIrradOverPi {};
    Mathematics::LinearRGB Albedo {};
    float                  Area = 0.0f;
    float                  Radius = 0.35f;
};

[[nodiscard]] std::vector<StudioSurfel> BuildDeterministicStudioSurfels(
    const StudioSceneBundle&                   Bundle,
    std::span<const EmissiveTri>               Emissives,
    const SpatialAcceleration::WideBoundingTree& SceneTree)
{
    std::vector<StudioSurfel> Surfels;
    const auto AddPatchGrid = [&](
        Mathematics::Axis3D Center,
        Mathematics::Axis3D UAxis,
        Mathematics::Axis3D VAxis,
        Mathematics::Axis3D Nrm,
        float               HalfU,
        float               HalfV,
        uint32_t            NU,
        uint32_t            NV,
        uint16_t            MatSlot)
    {
        const auto& Mat = Bundle.Scene.MaterialRegister.InspectAuthoredMaterial(MatSlot);
        const float CellArea = (2.0f * HalfU / static_cast<float>(NU)) * (2.0f * HalfV / static_cast<float>(NV));
        const float CellRad  = std::sqrt(CellArea / Constants::PiFloat);
        for (uint32_t J = 0u; J < NV; ++J)
        {
            const float FV = ((static_cast<float>(J) + 0.5f) / static_cast<float>(NV)) * 2.0f - 1.0f;
            for (uint32_t I = 0u; I < NU; ++I)
            {
                const float FU = ((static_cast<float>(I) + 0.5f) / static_cast<float>(NU)) * 2.0f - 1.0f;
                const Mathematics::Axis3D P = Center + UAxis * (FU * HalfU) + VAxis * (FV * HalfV);
                // Skip surfels trapped strictly inside the vehicle chassis footprint
                if (std::fabs(Nrm.Z) > 0.9f && P.X > -2.9f && P.X < 1.3f && P.Y > -0.95f && P.Y < 1.05f)
                {
                    continue;
                }
                Mathematics::LinearRGB IrrOverPi {};
                for (const auto& Tri : Emissives)
                {
                    if (Mathematics::EvaluateDot(P - Tri.Centroid, Tri.Normal) <= 1.0e-4f) continue;
                    const Mathematics::Axis3D ToL = Tri.Centroid - P;
                    const float Dist = Mathematics::EvaluateLength(ToL);
                    const Mathematics::Axis3D Dir = ToL * (1.0f / std::max(1.0e-5f, Dist));
                    if (Mathematics::EvaluateDot(Nrm, Dir) <= 1.0e-4f) continue;

                    SpatialAcceleration::RaySegmentQuery Ray {};
                    Ray.Origin          = P + Nrm * 3.0e-3f;
                    Ray.UnitDirection   = Dir;
                    Ray.MinimumDistance = 1.0e-3f;
                    Ray.MaximumDistance = std::max(2.0e-3f, Dist - 8.0e-3f);
                    if (SceneTree.QueryRayOccluded(Ray)) continue;

                    std::array<Mathematics::Axis3D, 3u> LocalT {{
                        { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }
                    }};
                    Mathematics::Axis3D T1 = Mathematics::NormalizeAxis(
                        Mathematics::EvaluateCross((std::fabs(Nrm.Z) < 0.9f) ? Mathematics::Axis3D{0,0,1} : Mathematics::Axis3D{1,0,0}, Nrm));
                    Mathematics::Axis3D T2 = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Nrm, T1));
                    const std::array<Mathematics::Axis3D, 3u> W {{ Tri.V0, Tri.V1, Tri.V2 }};
                    for (uint32_t K = 0u; K < 3u; ++K)
                    {
                        const auto R = W[K] - P;
                        LocalT[K] = { Mathematics::EvaluateDot(R, T1), Mathematics::EvaluateDot(R, T2), Mathematics::EvaluateDot(R, Nrm) };
                    }
                    const float FF = IntegrateClippedTriangleCosineFormFactor(LocalT);
                    IrrOverPi = IrrOverPi + Tri.Radiance * FF;
                }
                if ((IrrOverPi.R + IrrOverPi.G + IrrOverPi.B) > 1.0e-5f)
                {
                    Surfels.push_back(StudioSurfel{
                        P, Nrm, IrrOverPi, Mat.Diffuse.BaseColor * Mat.Diffuse.BaseWeight, CellArea, CellRad
                    });
                }
            }
        }
    };

    // Floor (z=0), Back Wall (y=3.96), Crimson Left Wall (x=-5.06), Emerald Right Wall (x=5.06)
    AddPatchGrid({  0.0f,  0.0f, 0.0f  }, { 1, 0, 0 }, { 0, 1, 0 }, {  0,  0, 1 }, 6.0f, 4.5f, 16u, 12u, Bundle.SlotPlaster);
    AddPatchGrid({  0.0f,  3.96f, 1.7f }, { 1, 0, 0 }, { 0, 0, 1 }, {  0, -1, 0 }, 5.5f, 1.7f, 14u,  6u, Bundle.SlotPlaster);
    AddPatchGrid({ -5.06f, 0.0f, 1.7f  }, { 0, 1, 0 }, { 0, 0, 1 }, {  1,  0, 0 }, 4.5f, 1.7f, 12u,  6u, Bundle.SlotCrimson);
    AddPatchGrid({  5.06f, 0.0f, 1.7f  }, { 0, 1, 0 }, { 0, 0, 1 }, { -1,  0, 0 }, 4.5f, 1.7f, 12u,  6u, Bundle.SlotEmerald);

    return Surfels;
}

//========================================================================================================================
// ALGORITHM 1: ANALYTIC EMISSIVE LTC + CLIPMAP SDF SOFT PENUMBRA + 1-BOUNCE SURFEL RADIOSITY
//========================================================================================================================

void RenderAlgorithm1_AnalyticLtcSdfPenumbra(
    uint32_t                                               W,
    uint32_t                                               H,
    std::span<const GeometricRaster::AnalyticResolvedSurfaceSample> Primary,
    const StudioSceneBundle&                               Bundle,
    const DistanceFieldSpace::VoxelSdfClipmapVolume&       Clipmap,
    std::span<const EmissiveTri>                           Emissives,
    std::span<const StudioSurfel>                          Surfels,
    std::vector<Mathematics::LinearRGB>&                   OutHdr)
{
    OutHdr.assign(static_cast<std::size_t>(W) * H, { 0.0f, 0.0f, 0.0f });
    const uint32_t MatCount = Bundle.Scene.MaterialRegister.QuerySlotCount();
    const auto& SceneTree   = Bundle.Scene.SceneTree;

    ParallelForRows(H, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t PY = Y0; PY < Y1; ++PY)
        {
            for (uint32_t PX = 0u; PX < W; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * W + PX;
                const auto& Hit = Primary[Off];
                if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount) continue;

                const auto& Mat = Bundle.Scene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const uint32_t Lobes = Bundle.Scene.MaterialRegister.InspectCompactEntry(Hit.MaterialSlot).ActiveLobeMask;
                const auto& N = Hit.TangentFrame.NormalZ;

                Mathematics::LinearRGB Hdr = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);

                // 1. Direct Analytic Emissive Triangle LTC gated by Hybrid Clipmap SDF Soft Penumbra (NO jitter, NO denoiser)
                // Offset ray end away from emitter surface so the vehicle's own 0.048m SDF shell never self-occludes emitters!
                for (const auto& Tri : Emissives)
                {
                    if (Mathematics::EvaluateDot(Hit.WorldPosition - Tri.Centroid, Tri.Normal) <= 1.0e-4f) continue;
                    const Mathematics::Axis3D ToL = Tri.Centroid - Hit.WorldPosition;
                    const float Dist = Mathematics::EvaluateLength(ToL);
                    const Mathematics::Axis3D Dir = ToL * (1.0f / std::max(1.0e-5f, Dist));
                    if (Mathematics::EvaluateDot(N, Dir) <= 1.0e-4f) continue;

                    const float EffRadius = std::sqrt(Tri.Area / Constants::PiFloat);
                    float Vis = 1.0f;
                    if (Dist > 0.22f)
                    {
                        Vis = Clipmap.TraceClipmapSdfPenumbra(
                            Hit.WorldPosition + Dir * 0.04f, N, Dir, std::max(0.05f, Dist - 0.16f),
                            std::max(0.05f, EffRadius), false, Bundle.Scene.DynamicSdfObjects, false).Visibility;
                    }

                    // Short-range exact BVH contact shadow (< 0.35 m) preserves crisp tire/bumper contact
                    if (Vis > 0.01f)
                    {
                        SpatialAcceleration::RaySegmentQuery Contact {};
                        Contact.Origin          = Hit.WorldPosition + N * 2.5e-3f;
                        Contact.UnitDirection   = Dir;
                        Contact.MinimumDistance = 1.0e-3f;
                        Contact.MaximumDistance = std::min(0.45f, std::max(2.0e-3f, Dist - 8.0e-3f));
                        if (SceneTree.QueryRayOccluded(Contact)) Vis = 0.0f;
                    }

                    if (Vis > 1.0e-4f)
                    {
                        Hdr = Hdr + EvaluateTriLtcDirect(Tri, Hit.WorldPosition, N, Hit.WorldViewDirection, Mat, Lobes, Vis);
                    }
                }

                // 2. Deterministic 1-Bounce Analytical Disk-Form-Factor Surfel Radiosity (Diffuse + Broad Specular)
                const float DiffW = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight) * Mat.Diffuse.BaseWeight;
                const float SpecW = std::max(Mat.Specular.SpecularWeight * 0.25f, std::max(Mat.Specular.Metalness, Mat.Clearcoat.CoatWeight));
                if (DiffW > 1.0e-4f || SpecW > 1.0e-4f)
                {
                    const auto ReflDir = Mathematics::NormalizeAxis(
                        N * (2.0f * std::max(0.0f, Mathematics::EvaluateDot(N, Hit.WorldViewDirection))) - Hit.WorldViewDirection);
                    Mathematics::LinearRGB BounceDiff {};
                    Mathematics::LinearRGB BounceSpec {};
                    for (const auto& S : Surfels)
                    {
                        const Mathematics::Axis3D Delta = S.Position - Hit.WorldPosition;
                        const float Dist2 = Mathematics::EvaluateLengthSquared(Delta);
                        if (Dist2 < 0.02f) continue;
                        const float Dist = std::sqrt(Dist2);
                        const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);
                        const float CosP = Mathematics::EvaluateDot(N, Dir);
                        const float CosS = Mathematics::EvaluateDot(S.Normal, Dir * -1.0f);
                        if (CosP <= 1.0e-3f || CosS <= 1.0e-3f) continue;

                        const float FF = (S.Area * CosP * CosS) / (Constants::PiFloat * (Dist2 + S.Radius * S.Radius));
                        const Mathematics::LinearRGB Ex {
                            S.DirectIrradOverPi.R * S.Albedo.R,
                            S.DirectIrradOverPi.G * S.Albedo.G,
                            S.DirectIrradOverPi.B * S.Albedo.B,
                        };
                        BounceDiff = BounceDiff + Ex * FF;
                        if (SpecW > 1.0e-4f)
                        {
                            const float RdotL = std::max(0.0f, Mathematics::EvaluateDot(ReflDir, Dir));
                            const float Rough = std::clamp(Mat.Specular.Roughness, 0.12f, 1.0f);
                            const float SpecP = std::pow(RdotL, std::clamp(2.0f / (Rough * Rough) - 2.0f, 2.0f, 32.0f));
                            const float Solid = (S.Area * CosS) / (Constants::PiFloat * (Dist2 + S.Radius * S.Radius));
                            BounceSpec = BounceSpec + Ex * (Solid * SpecP * 0.35f);
                        }
                    }
                    Hdr.R += BounceDiff.R * Mat.Diffuse.BaseColor.R * DiffW + BounceSpec.R * SpecW;
                    Hdr.G += BounceDiff.G * Mat.Diffuse.BaseColor.G * DiffW + BounceSpec.G * SpecW;
                    Hdr.B += BounceDiff.B * Mat.Diffuse.BaseColor.B * DiffW + BounceSpec.B * SpecW;
                }

                OutHdr[Off] = Hdr;
            }
        }
    });
}

//========================================================================================================================
// ALGORITHM 2: HIERARCHICAL LIGHTCUTS (WALTER 2005) + VIRTUAL SPHERICAL LIGHTS (VSL, HAŠAN 2009)
//========================================================================================================================

struct VirtualSphericalLight
{
    Mathematics::Axis3D    Position {};
    Mathematics::Axis3D    Normal {};
    Mathematics::LinearRGB FluxOverPi {}; // Area * Radiance (or Area * BounceExitance)
    float                  Radius = 0.10f;
    bool                   TwoSided = false;
};

struct LightTreeNode
{
    Mathematics::Axis3D    AabbMin {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D    AabbMax { -1.0e9f, -1.0e9f, -1.0e9f };
    Mathematics::Axis3D    RepPos {};
    Mathematics::Axis3D    RepNormal {};
    Mathematics::LinearRGB TotalFluxOverPi {};
    float                  FluxLuma = 0.0f;
    float                  RepRadius = 0.1f;
    int32_t                Left  = -1;
    int32_t                Right = -1;
    int32_t                LeafVslIndex = -1;
};

int32_t BuildLightTreeRecursive(
    std::vector<VirtualSphericalLight>& Lights,
    std::vector<LightTreeNode>&         Nodes,
    uint32_t                            Begin,
    uint32_t                            End)
{
    LightTreeNode Node {};
    Mathematics::LinearRGB SumFlux {};
    Mathematics::Axis3D WeightedPos {};
    Mathematics::Axis3D WeightedNrm {};
    float SumLuma = 0.0f;

    for (uint32_t I = Begin; I < End; ++I)
    {
        const auto& L = Lights[I];
        Node.AabbMin.X = std::min(Node.AabbMin.X, L.Position.X - L.Radius);
        Node.AabbMin.Y = std::min(Node.AabbMin.Y, L.Position.Y - L.Radius);
        Node.AabbMin.Z = std::min(Node.AabbMin.Z, L.Position.Z - L.Radius);
        Node.AabbMax.X = std::max(Node.AabbMax.X, L.Position.X + L.Radius);
        Node.AabbMax.Y = std::max(Node.AabbMax.Y, L.Position.Y + L.Radius);
        Node.AabbMax.Z = std::max(Node.AabbMax.Z, L.Position.Z + L.Radius);

        const float W = std::max(1.0e-5f, 0.2126f * L.FluxOverPi.R + 0.7152f * L.FluxOverPi.G + 0.0722f * L.FluxOverPi.B);
        SumFlux = SumFlux + L.FluxOverPi;
        WeightedPos = WeightedPos + L.Position * W;
        WeightedNrm = WeightedNrm + L.Normal * W;
        SumLuma += W;
    }

    Node.TotalFluxOverPi = SumFlux;
    Node.FluxLuma        = SumLuma;
    Node.RepPos          = WeightedPos * (1.0f / std::max(1.0e-5f, SumLuma));
    Node.RepNormal       = Mathematics::NormalizeAxis(WeightedNrm, { 0, 0, 1 });
    const Mathematics::Axis3D Ext = Node.AabbMax - Node.AabbMin;
    Node.RepRadius       = 0.5f * Mathematics::EvaluateLength(Ext);

    const int32_t MyIdx = static_cast<int32_t>(Nodes.size());
    Nodes.push_back(Node);

    if (End - Begin == 1u)
    {
        Nodes[MyIdx].LeafVslIndex = static_cast<int32_t>(Begin);
        Nodes[MyIdx].RepPos       = Lights[Begin].Position;
        Nodes[MyIdx].RepNormal    = Lights[Begin].Normal;
        Nodes[MyIdx].RepRadius    = Lights[Begin].Radius;
        return MyIdx;
    }

    int Axis = 0;
    if (Ext.Y > Ext.X && Ext.Y >= Ext.Z) Axis = 1;
    else if (Ext.Z > Ext.X && Ext.Z >= Ext.Y) Axis = 2;

    const uint32_t Mid = Begin + (End - Begin) / 2u;
    std::nth_element(Lights.begin() + Begin, Lights.begin() + Mid, Lights.begin() + End,
        [Axis](const VirtualSphericalLight& A, const VirtualSphericalLight& B)
        {
            const float VA = (Axis == 0) ? A.Position.X : ((Axis == 1) ? A.Position.Y : A.Position.Z);
            const float VB = (Axis == 0) ? B.Position.X : ((Axis == 1) ? B.Position.Y : B.Position.Z);
            return VA < VB;
        });

    const int32_t LChild = BuildLightTreeRecursive(Lights, Nodes, Begin, Mid);
    const int32_t RChild = BuildLightTreeRecursive(Lights, Nodes, Mid, End);
    Nodes[MyIdx].Left  = LChild;
    Nodes[MyIdx].Right = RChild;
    return MyIdx;
}

[[nodiscard]] Mathematics::LinearRGB EvaluateSingleVslHaSan2009(
    Mathematics::Axis3D                  VslPos,
    Mathematics::Axis3D                  VslNormal,
    Mathematics::LinearRGB               FluxOverPi,
    float                                VslRadius,
    Mathematics::Axis3D                  Pos,
    Mathematics::Axis3D                  Nrm,
    Mathematics::Axis3D                  ViewDir,
    const MaterialSpace::SurfaceMaterial& Mat,
    uint32_t                             LobeMask,
    const SpatialAcceleration::WideBoundingTree& SceneTree,
    bool                                 CheckOcclusion) noexcept
{
    const Mathematics::Axis3D ToL = VslPos - Pos;
    const float Dist2 = Mathematics::EvaluateLengthSquared(ToL);
    const float Dist  = std::sqrt(std::max(1.0e-6f, Dist2));
    const Mathematics::Axis3D Dir = ToL * (1.0f / Dist);

    const float CosX = Mathematics::EvaluateDot(Nrm, Dir);
    const float CosL = Mathematics::EvaluateDot(VslNormal, Dir * -1.0f);
    if (CosX <= 1.0e-4f || CosL <= 1.0e-4f) return {};

    if (CheckOcclusion)
    {
        SpatialAcceleration::RaySegmentQuery Ray {};
        Ray.Origin          = Pos + Nrm * 3.0e-3f;
        Ray.UnitDirection   = Dir;
        Ray.MinimumDistance = 1.0e-3f;
        Ray.MaximumDistance = std::max(2.0e-3f, Dist - std::max(0.01f, VslRadius * 0.25f));
        if (SceneTree.QueryRayOccluded(Ray)) return {};
    }

    // Hašan et al. (2009) Virtual Spherical Light finite solid-angle form factor:
    // Caps irradiance smoothly at Omega_VSL / pi when d -> R without singular 1/r^2 spikes!
    const float SinAlpha = std::clamp(VslRadius / std::max(Dist, VslRadius + 1.0e-4f), 0.0f, 0.995f);
    const float CosAlpha = std::sqrt(std::max(0.0f, 1.0f - SinAlpha * SinAlpha));
    const float SolidAngleOverPi = 2.0f * (1.0f - CosAlpha);
    const float PointFormFactor  = (CosX * CosL) / std::max(VslRadius * VslRadius, Dist2 + VslRadius * VslRadius * 0.5f);
    const float Area = Constants::PiFloat * VslRadius * VslRadius;
    const float DiffuseGeom = std::min(SolidAngleOverPi * CosX / std::max(1.0e-4f, Area), PointFormFactor);

    const float DiffW = ((LobeMask & MaterialSpace::LobeDiffuseEON) != 0u)
        ? ((1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight) * Mat.Diffuse.BaseWeight)
        : 0.0f;

    // Glossy VSL cone-broadened OpenPBR microfacet reflection (Hašan et al. 2009 Section 4)
    Mathematics::Axis3D TangentU {}, TangentV {};
    const Mathematics::Axis3D Up = (std::fabs(Nrm.Z) < 0.9f) ? Mathematics::Axis3D{0,0,1} : Mathematics::Axis3D{1,0,0};
    TangentU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Up, Nrm));
    TangentV = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Nrm, TangentU));
    const auto ToLocal = [&](Mathematics::Axis3D V) noexcept -> Mathematics::Axis3D
    {
        return { Mathematics::EvaluateDot(V, TangentU), Mathematics::EvaluateDot(V, TangentV), Mathematics::EvaluateDot(V, Nrm) };
    };
    const auto LocalWo = ToLocal(ViewDir);
    const auto LocalWi = ToLocal(Dir);
    // Broaden effective roughness inside the VSL cone solid angle for artifact-free glossy reflection
    MaterialSpace::SurfaceMaterial BroadMat = Mat;
    const float VslBroadR = std::sqrt(SinAlpha);
    BroadMat.Specular.Roughness = std::clamp(std::sqrt(Mat.Specular.Roughness * Mat.Specular.Roughness + VslBroadR * VslBroadR * 0.25f), 0.04f, 1.0f);
    const auto SpecBsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
        BroadMat, LobeMask & ~MaterialSpace::LobeDiffuseEON, LocalWo, LocalWi);

    return {
        FluxOverPi.R * (DiffW * Mat.Diffuse.BaseColor.R * DiffuseGeom + SpecBsdf.R * DiffuseGeom * Constants::PiFloat),
        FluxOverPi.G * (DiffW * Mat.Diffuse.BaseColor.G * DiffuseGeom + SpecBsdf.G * DiffuseGeom * Constants::PiFloat),
        FluxOverPi.B * (DiffW * Mat.Diffuse.BaseColor.B * DiffuseGeom + SpecBsdf.B * DiffuseGeom * Constants::PiFloat),
    };
}

void RenderAlgorithm2_LightcutsVsl(
    uint32_t                                               W,
    uint32_t                                               H,
    std::span<const GeometricRaster::AnalyticResolvedSurfaceSample> Primary,
    const StudioSceneBundle&                               Bundle,
    std::span<const EmissiveTri>                           Emissives,
    std::span<const StudioSurfel>                          Surfels,
    std::vector<Mathematics::LinearRGB>&                   OutHdr)
{
    OutHdr.assign(static_cast<std::size_t>(W) * H, { 0.0f, 0.0f, 0.0f });
    const uint32_t MatCount = Bundle.Scene.MaterialRegister.QuerySlotCount();
    const auto& SceneTree   = Bundle.Scene.SceneTree;

    std::vector<VirtualSphericalLight> Lights;
    Lights.reserve(Emissives.size() + Surfels.size());
    for (const auto& Tri : Emissives)
    {
        Lights.push_back(VirtualSphericalLight{
            Tri.Centroid,
            Tri.Normal,
            Tri.Radiance * Tri.Area,
            std::max(0.04f, std::sqrt(Tri.Area / Constants::PiFloat)),
            false,
        });
    }
    for (const auto& S : Surfels)
    {
        Lights.push_back(VirtualSphericalLight{
            S.Position,
            S.Normal,
            Mathematics::LinearRGB{
                S.DirectIrradOverPi.R * S.Albedo.R * S.Area,
                S.DirectIrradOverPi.G * S.Albedo.G * S.Area,
                S.DirectIrradOverPi.B * S.Albedo.B * S.Area,
            },
            S.Radius,
            false,
        });
    }

    std::vector<LightTreeNode> TreeNodes;
    int32_t RootIdx = -1;
    if (!Lights.empty())
    {
        TreeNodes.reserve(Lights.size() * 2u);
        RootIdx = BuildLightTreeRecursive(Lights, TreeNodes, 0u, static_cast<uint32_t>(Lights.size()));
    }

    ParallelForRows(H, [&](uint32_t Y0, uint32_t Y1)
    {
        std::vector<int32_t> Heap;
        Heap.reserve(128u);
        for (uint32_t PY = Y0; PY < Y1; ++PY)
        {
            for (uint32_t PX = 0u; PX < W; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * W + PX;
                const auto& Hit = Primary[Off];
                if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount) continue;

                const auto& Mat = Bundle.Scene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const uint32_t Lobes = Bundle.Scene.MaterialRegister.InspectCompactEntry(Hit.MaterialSlot).ActiveLobeMask;
                const auto& N = Hit.TangentFrame.NormalZ;
                Mathematics::LinearRGB Hdr = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);

                if (RootIdx >= 0)
                {
                    // Deterministic Lightcut traversal with tight error bound to suppress cluster seams
                    Heap.clear();
                    Heap.push_back(RootIdx);
                    uint32_t CutCount = 0u;
                    constexpr uint32_t MaxCutNodes = 180u;
                    while (!Heap.empty())
                    {
                        const int32_t Idx = Heap.back();
                        Heap.pop_back();
                        const auto& Node = TreeNodes[Idx];

                        if (Node.LeafVslIndex >= 0)
                        {
                            const auto& L = Lights[Node.LeafVslIndex];
                            Hdr = Hdr + EvaluateSingleVslHaSan2009(
                                L.Position, L.Normal, L.FluxOverPi, std::max(0.08f, L.Radius),
                                Hit.WorldPosition, N, Hit.WorldViewDirection, Mat, Lobes, SceneTree, true);
                            continue;
                        }

                        const Mathematics::Axis3D C = (Node.AabbMin + Node.AabbMax) * 0.5f;
                        const float Dist = std::max(0.25f, Mathematics::EvaluateLength(C - Hit.WorldPosition) - Node.RepRadius);
                        const float Bound = Node.FluxLuma / (Dist * Dist);
                        const float CurrentLum = std::max(0.02f, 0.2126f * Hdr.R + 0.7152f * Hdr.G + 0.0722f * Hdr.B);

                        if ((Bound > 0.0025f * CurrentLum) && (CutCount + Heap.size() < MaxCutNodes))
                        {
                            if (Node.Left >= 0)  Heap.push_back(Node.Left);
                            if (Node.Right >= 0) Heap.push_back(Node.Right);
                            ++CutCount;
                        }
                        else
                        {
                            Hdr = Hdr + EvaluateSingleVslHaSan2009(
                                Node.RepPos, Node.RepNormal, Node.TotalFluxOverPi, std::max(0.18f, Node.RepRadius),
                                Hit.WorldPosition, N, Hit.WorldViewDirection, Mat, Lobes, SceneTree, true);
                        }
                    }
                }

                OutHdr[Off] = Hdr;
            }
        }
    });
}

//========================================================================================================================
// ALGORITHM 3: EXACT TRIANGLE FORM-FACTOR DIRECT + ORDER-2 L2 SPHERICAL HARMONIC (SH9) MULTI-BOUNCE RADIANCE FIELD
//========================================================================================================================

struct Sh9Rgb
{
    std::array<Mathematics::LinearRGB, 9u> C {};
};

[[nodiscard]] std::array<float, 9u> EvaluateSh9Basis(Mathematics::Axis3D D) noexcept
{
    return {{
        0.28209479f,
        0.48860251f * D.Y,
        0.48860251f * D.Z,
        0.48860251f * D.X,
        1.09254843f * D.X * D.Y,
        1.09254843f * D.Y * D.Z,
        0.31539157f * (3.0f * D.Z * D.Z - 1.0f),
        1.09254843f * D.X * D.Z,
        0.54627422f * (D.X * D.X - D.Y * D.Y),
    }};
}

// Ramamoorthi & Hanrahan (2001) exact cosine-lobe irradiance / pi from L2 SH radiance coefficients
[[nodiscard]] Mathematics::LinearRGB EvaluateSh9IrradianceOverPi(
    const Sh9Rgb&       Sh,
    Mathematics::Axis3D N) noexcept
{
    // Band factors A_l / pi: l=0 -> 1.0, l=1 -> 2/3, l=2 -> 1/4
    static constexpr std::array<float, 9u> BandW {{
        1.0f,
        0.6666667f, 0.6666667f, 0.6666667f,
        0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
    }};
    const auto Y = EvaluateSh9Basis(N);
    Mathematics::LinearRGB Out {};
    for (uint32_t K = 0u; K < 9u; ++K)
    {
        const float W = BandW[K] * Y[K];
        Out.R += Sh.C[K].R * W;
        Out.G += Sh.C[K].G * W;
        Out.B += Sh.C[K].B * W;
    }
    return { std::max(0.0f, Out.R), std::max(0.0f, Out.G), std::max(0.0f, Out.B) };
}

void RenderAlgorithm3_ExactTriFormFactorPlusSh9Field(
    uint32_t                                               W,
    uint32_t                                               H,
    std::span<const GeometricRaster::AnalyticResolvedSurfaceSample> Primary,
    const StudioSceneBundle&                               Bundle,
    std::span<const EmissiveTri>                           Emissives,
    std::span<const StudioSurfel>                          Surfels,
    std::vector<Mathematics::LinearRGB>&                   OutHdr)
{
    OutHdr.assign(static_cast<std::size_t>(W) * H, { 0.0f, 0.0f, 0.0f });
    const uint32_t MatCount = Bundle.Scene.MaterialRegister.QuerySlotCount();
    const auto& SceneTree   = Bundle.Scene.SceneTree;

    // 1. Build 15 x 12 x 6 Order-2 L2 SH Irradiance Field (Zero 8x8 octahedral seams, zero stochastic probe jitter)
    constexpr uint32_t NX = 15u, NY = 12u, NZ = 6u;
    const Mathematics::Axis3D GMin { -4.35f, -3.15f, 0.42f };
    const Mathematics::Axis3D GMax {  4.35f,  3.35f, 2.95f };
    const Mathematics::Axis3D Step {
        (GMax.X - GMin.X) / static_cast<float>(NX - 1u),
        (GMax.Y - GMin.Y) / static_cast<float>(NY - 1u),
        (GMax.Z - GMin.Z) / static_cast<float>(NZ - 1u),
    };

    struct ShProbe
    {
        Mathematics::Axis3D Pos {};
        Sh9Rgb              Sh {};
        float               BackfaceRatio = 0.0f;
    };
    std::vector<ShProbe> Probes(static_cast<std::size_t>(NX) * NY * NZ);

    for (uint32_t IZ = 0u; IZ < NZ; ++IZ)
    {
        for (uint32_t IY = 0u; IY < NY; ++IY)
        {
            for (uint32_t IX = 0u; IX < NX; ++IX)
            {
                const std::size_t Idx = (static_cast<std::size_t>(IZ) * NY + IY) * NX + IX;
                const Mathematics::Axis3D P {
                    GMin.X + static_cast<float>(IX) * Step.X,
                    GMin.Y + static_cast<float>(IY) * Step.Y,
                    GMin.Z + static_cast<float>(IZ) * Step.Z,
                };
                Probes[Idx].Pos = P;

                // Deterministic backface validity check (12 icosahedral/Fibonacci directions)
                uint32_t BackCnt = 0u;
                for (uint32_t R = 0u; R < 32u; ++R)
                {
                    const float Z = 1.0f - (2.0f * (static_cast<float>(R) + 0.5f)) / 32.0f;
                    const float Rxy = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
                    const float Phi = Constants::TauFloat * std::fmod(static_cast<float>(R) * 0.61803398875f, 1.0f);
                    const Mathematics::Axis3D D { Rxy * std::cos(Phi), Rxy * std::sin(Phi), Z };
                    SpatialAcceleration::RaySegmentQuery Ray {};
                    Ray.Origin = P; Ray.UnitDirection = D; Ray.MinimumDistance = 1e-3f; Ray.MaximumDistance = 20.0f;
                    const auto Hit = SceneTree.TraceClosestRay(Ray, false);
                    if (Hit.HitConfirmed && Mathematics::EvaluateDot(Hit.GeometricNormal, D) >= 0.0f) ++BackCnt;
                }
                Probes[Idx].BackfaceRatio = static_cast<float>(BackCnt) / 32.0f;
                if (Probes[Idx].BackfaceRatio > 0.25f) continue;

                // Analytically project 1-bounce studio surfels into L2 SH coefficients (zero Monte Carlo probe noise!)
                for (const auto& S : Surfels)
                {
                    const Mathematics::Axis3D Delta = S.Position - P;
                    const float Dist2 = Mathematics::EvaluateLengthSquared(Delta);
                    const float Dist  = std::sqrt(std::max(1.0e-5f, Dist2));
                    const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);
                    const float CosS = Mathematics::EvaluateDot(S.Normal, Dir * -1.0f);
                    if (CosS <= 1.0e-3f) continue;

                    SpatialAcceleration::RaySegmentQuery VisRay {};
                    VisRay.Origin = P; VisRay.UnitDirection = Dir;
                    VisRay.MinimumDistance = 1e-3f; VisRay.MaximumDistance = std::max(0.01f, Dist - 0.03f);
                    if (SceneTree.QueryRayOccluded(VisRay)) continue;

                    const float SolidAngle = std::min(1.5f, (S.Area * CosS) / std::max(0.20f, Dist2 + S.Radius * S.Radius));
                    const Mathematics::LinearRGB Rad {
                        S.DirectIrradOverPi.R * S.Albedo.R * SolidAngle,
                        S.DirectIrradOverPi.G * S.Albedo.G * SolidAngle,
                        S.DirectIrradOverPi.B * S.Albedo.B * SolidAngle,
                    };
                    const auto Y = EvaluateSh9Basis(Dir);
                    for (uint32_t K = 0u; K < 9u; ++K)
                    {
                        Probes[Idx].Sh.C[K].R += Rad.R * Y[K];
                        Probes[Idx].Sh.C[K].G += Rad.G * Y[K];
                        Probes[Idx].Sh.C[K].B += Rad.B * Y[K];
                    }
                }
            }
        }
    }

    const auto SampleSh9Field = [&](Mathematics::Axis3D Pos, Mathematics::Axis3D N, Mathematics::Axis3D QueryDir) noexcept -> Mathematics::LinearRGB
    {
        const Mathematics::Axis3D SamplePos = Pos + N * 0.14f;
        const float FX = std::clamp((SamplePos.X - GMin.X) / Step.X, 0.0f, static_cast<float>(NX - 1u));
        const float FY = std::clamp((SamplePos.Y - GMin.Y) / Step.Y, 0.0f, static_cast<float>(NY - 1u));
        const float FZ = std::clamp((SamplePos.Z - GMin.Z) / Step.Z, 0.0f, static_cast<float>(NZ - 1u));
        const uint32_t X0 = static_cast<uint32_t>(std::floor(FX));
        const uint32_t Y0 = static_cast<uint32_t>(std::floor(FY));
        const uint32_t Z0 = static_cast<uint32_t>(std::floor(FZ));
        const uint32_t X1 = std::min(NX - 1u, X0 + 1u);
        const uint32_t Y1 = std::min(NY - 1u, Y0 + 1u);
        const uint32_t Z1 = std::min(NZ - 1u, Z0 + 1u);
        const float LTX = FX - static_cast<float>(X0), LTY = FY - static_cast<float>(Y0), LTZ = FZ - static_cast<float>(Z0);
        // C1 Hermite interpolation eliminates Mach bands at probe planes
        const float TX = LTX * LTX * (3.0f - 2.0f * LTX);
        const float TY = LTY * LTY * (3.0f - 2.0f * LTY);
        const float TZ = LTZ * LTZ * (3.0f - 2.0f * LTZ);

        Mathematics::LinearRGB Sum {};
        float WSum = 0.0f;
        for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
        {
            const uint32_t CZ = DZ ? Z1 : Z0;
            const float WZ = DZ ? TZ : (1.0f - TZ);
            for (uint32_t DY = 0u; DY < 2u; ++DY)
            {
                const uint32_t CY = DY ? Y1 : Y0;
                const float WY = DY ? TY : (1.0f - TY);
                for (uint32_t DX = 0u; DX < 2u; ++DX)
                {
                    const uint32_t CX = DX ? X1 : X0;
                    const float WX = DX ? TX : (1.0f - TX);
                    const auto& Pr = Probes[(static_cast<std::size_t>(CZ) * NY + CY) * NX + CX];
                    if (Pr.BackfaceRatio > 0.25f) continue;

                    const Mathematics::Axis3D ToP = Pr.Pos - Pos;
                    const float Len = Mathematics::EvaluateLength(ToP);
                    const Mathematics::Axis3D Dir = (Len > 1e-4f) ? (ToP * (1.0f / Len)) : N;
                    const float Wrap = 0.5f * (Mathematics::EvaluateDot(Dir, N) + 1.0f);
                    const float W = WX * WY * WZ * std::max(0.02f, Wrap * Wrap);
                    Sum = Sum + EvaluateSh9IrradianceOverPi(Pr.Sh, QueryDir) * W;
                    WSum += W;
                }
            }
        }
        return (WSum > 1.0e-5f) ? (Sum * (1.0f / WSum)) : Mathematics::LinearRGB{};
    };

    // 2. Per-pixel Exact Triangle Form-Factor + 9-Tap Deterministic Stratified Visibility + SH9 Diffuse & Glossy Field
    ParallelForRows(H, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t PY = Y0; PY < Y1; ++PY)
        {
            for (uint32_t PX = 0u; PX < W; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * W + PX;
                const auto& Hit = Primary[Off];
                if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount) continue;

                const auto& Mat = Bundle.Scene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
                const uint32_t Lobes = Bundle.Scene.MaterialRegister.InspectCompactEntry(Hit.MaterialSlot).ActiveLobeMask;
                const auto& N = Hit.TangentFrame.NormalZ;
                Mathematics::LinearRGB Hdr = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);

                // Direct emissive via exact triangle LTC + 9 deterministic stratified barycentric visibility taps (no jitter!)
                static constexpr std::array<std::array<float, 3u>, 9u> Bary9 {{
                    {{ 0.70f, 0.15f, 0.15f }},
                    {{ 0.15f, 0.70f, 0.15f }},
                    {{ 0.15f, 0.15f, 0.70f }},
                    {{ 0.45f, 0.45f, 0.10f }},
                    {{ 0.45f, 0.10f, 0.45f }},
                    {{ 0.10f, 0.45f, 0.45f }},
                    {{ 0.3333f, 0.3333f, 0.3334f }},
                    {{ 0.55f, 0.25f, 0.20f }},
                    {{ 0.20f, 0.55f, 0.25f }},
                }};
                for (const auto& Tri : Emissives)
                {
                    if (Mathematics::EvaluateDot(Hit.WorldPosition - Tri.Centroid, Tri.Normal) <= 1.0e-4f) continue;
                    uint32_t Hits = 0u;
                    for (const auto& B : Bary9)
                    {
                        const Mathematics::Axis3D Tgt = Tri.V0 * B[0] + Tri.V1 * B[1] + Tri.V2 * B[2];
                        const Mathematics::Axis3D ToL = Tgt - Hit.WorldPosition;
                        const float Dist = Mathematics::EvaluateLength(ToL);
                        const Mathematics::Axis3D Dir = ToL * (1.0f / std::max(1e-5f, Dist));
                        if (Mathematics::EvaluateDot(N, Dir) <= 1.0e-4f) continue;
                        SpatialAcceleration::RaySegmentQuery Ray {};
                        Ray.Origin = Hit.WorldPosition + N * 2.5e-3f;
                        Ray.UnitDirection = Dir;
                        Ray.MinimumDistance = 1e-3f;
                        Ray.MaximumDistance = std::max(2e-3f, Dist - 6e-3f);
                        if (!SceneTree.QueryRayOccluded(Ray)) ++Hits;
                    }
                    if (Hits > 0u)
                    {
                        const float Vis = static_cast<float>(Hits) * (1.0f / 9.0f);
                        Hdr = Hdr + EvaluateTriLtcDirect(Tri, Hit.WorldPosition, N, Hit.WorldViewDirection, Mat, Lobes, Vis);
                    }
                }

                // Indirect Diffuse + Clearcoat/Specular reflection via L2 SH9 field
                const float DiffW = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight) * Mat.Diffuse.BaseWeight;
                if (DiffW > 1.0e-4f)
                {
                    const auto IrrOverPi = SampleSh9Field(Hit.WorldPosition, N, N);
                    Hdr.R += IrrOverPi.R * Mat.Diffuse.BaseColor.R * DiffW;
                    Hdr.G += IrrOverPi.G * Mat.Diffuse.BaseColor.G * DiffW;
                    Hdr.B += IrrOverPi.B * Mat.Diffuse.BaseColor.B * DiffW;
                }
                const float NdotV = std::clamp(Mathematics::EvaluateDot(N, Hit.WorldViewDirection), 0.0f, 1.0f);
                const float Fres = 0.04f + 0.96f * std::pow(1.0f - NdotV, 5.0f);
                const float SpecEnvW = Mat.Clearcoat.CoatWeight * Fres + Mat.Specular.Metalness * 0.85f + Mat.Specular.SpecularWeight * Fres * 0.4f;
                if (SpecEnvW > 1.0e-3f)
                {
                    const auto ReflDir = Mathematics::NormalizeAxis(N * (2.0f * NdotV) - Hit.WorldViewDirection);
                    const auto EnvSpec = SampleSh9Field(Hit.WorldPosition, N, ReflDir);
                    Hdr.R += EnvSpec.R * SpecEnvW * (Mat.Specular.Metalness > 0.5f ? Mat.Diffuse.BaseColor.R : 1.0f);
                    Hdr.G += EnvSpec.G * SpecEnvW * (Mat.Specular.Metalness > 0.5f ? Mat.Diffuse.BaseColor.G : 1.0f);
                    Hdr.B += EnvSpec.B * SpecEnvW * (Mat.Specular.Metalness > 0.5f ? Mat.Diffuse.BaseColor.B : 1.0f);
                }

                OutHdr[Off] = Hdr;
            }
        }
    });
}

//========================================================================================================================
// GROUND TRUTH REFERENCE: 64-SPP PATH TRACER WITH EXPLICIT EMISSIVE-TRIANGLE NEE + MIS (NO DENOISER)
//========================================================================================================================

void RenderPathTraceNeeReference64Spp(
    uint32_t                                               W,
    uint32_t                                               H,
    const GeometricRaster::PerspectiveCameraBasis&         Cam,
    const StudioSceneBundle&                               Bundle,
    std::span<const EmissiveTri>                           Emissives,
    std::vector<Mathematics::LinearRGB>&                   OutHdr)
{
    OutHdr.assign(static_cast<std::size_t>(W) * H, { 0.0f, 0.0f, 0.0f });
    const auto Basis = GeometricRaster::ComputeCameraBasis(Cam);
    const auto& SceneTree = Bundle.Scene.SceneTree;
    const uint32_t MatCount = Bundle.Scene.MaterialRegister.QuerySlotCount();

    std::vector<float> Cdf(Emissives.size(), 0.0f);
    double TotalFlux = 0.0;
    for (std::size_t I = 0u; I < Emissives.size(); ++I)
    {
        TotalFlux += static_cast<double>(Emissives[I].TotalFlux);
        Cdf[I] = static_cast<float>(TotalFlux);
    }
    const float InvTotalFlux = (TotalFlux > 1.0e-6) ? static_cast<float>(1.0 / TotalFlux) : 0.0f;
    for (float& V : Cdf) V *= InvTotalFlux;

    constexpr uint32_t Spp = 128u;
    constexpr uint32_t MaxBounces = 4u;

    ParallelForRows(H, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t PY = Y0; PY < Y1; ++PY)
        {
            for (uint32_t PX = 0u; PX < W; ++PX)
            {
                const uint32_t PixelSeed = PY * 747796405u + PX * 2891336453u + 1u;
                Mathematics::LinearRGB Sum {};

                for (uint32_t S = 0u; S < Spp; ++S)
                {
                    const auto Sub = GeometricRaster::Sobol02Sample(S, 0u, PixelSeed);
                    const float FU = (static_cast<float>(PX) + Sub.X) / static_cast<float>(W);
                    const float FV = (static_cast<float>(PY) + Sub.Y) / static_cast<float>(H);
                    const float NdcX = FU * 2.0f - 1.0f;
                    const float NdcY = 1.0f - FV * 2.0f;
                    Mathematics::Axis3D RayO = Basis.CameraOrigin;
                    Mathematics::Axis3D RayD = Mathematics::NormalizeAxis(
                        Basis.ForwardAxis + Basis.RightAxis * (NdcX * Basis.TanHalfFovX) + Basis.UpAxis * (NdcY * Basis.TanHalfFovY));

                    Mathematics::LinearRGB PathRad {};
                    Mathematics::LinearRGB Throughput { 1.0f, 1.0f, 1.0f };
                    bool CountEmissiveOnHit = true;

                    for (uint32_t B = 0u; B < MaxBounces; ++B)
                    {
                        SpatialAcceleration::RaySegmentQuery Q {};
                        Q.Origin = RayO; Q.UnitDirection = RayD; Q.MinimumDistance = 1e-3f; Q.MaximumDistance = 120.0f;
                        const auto Hit = SceneTree.TraceClosestRay(Q, false);
                        if (!Hit.HitConfirmed) break;

                        const Mathematics::Axis3D HitP = RayO + RayD * Hit.HitDistanceMeters;
                        const bool Front = (Mathematics::EvaluateDot(Hit.GeometricNormal, RayD) < 0.0f);
                        const Mathematics::Axis3D N = Front ? Hit.GeometricNormal : (Hit.GeometricNormal * -1.0f);
                        const uint16_t Slot = (Hit.MaterialOrdinal < MatCount) ? Hit.MaterialOrdinal : 0u;
                        const auto& Mat = Bundle.Scene.MaterialRegister.InspectAuthoredMaterial(Slot);
                        const uint32_t Lobes = Bundle.Scene.MaterialRegister.InspectCompactEntry(Slot).ActiveLobeMask;

                        const auto Emit = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
                        if (CountEmissiveOnHit && Front)
                        {
                            PathRad = PathRad + Mathematics::LinearRGB{
                                Throughput.R * Emit.R, Throughput.G * Emit.G, Throughput.B * Emit.B
                            };
                        }
                        if ((Emit.R + Emit.G + Emit.B) > 0.1f) break;

                        Mathematics::Axis3D Tu {}, Tv {};
                        const Mathematics::Axis3D Up = (std::fabs(N.Z) < 0.9f) ? Mathematics::Axis3D{0,0,1} : Mathematics::Axis3D{1,0,0};
                        Tu = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Up, N));
                        Tv = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(N, Tu));
                        const auto ToLocal = [&](Mathematics::Axis3D V) noexcept
                        {
                            return Mathematics::Axis3D{
                                Mathematics::EvaluateDot(V, Tu), Mathematics::EvaluateDot(V, Tv), Mathematics::EvaluateDot(V, N)
                            };
                        };
                        const auto Wo = RayD * -1.0f;
                        const auto LocalWo = ToLocal(Wo);

                        // Explicit NEE on Emissive Triangles (importance-sampled by flux CDF)
                        if (!Emissives.empty() && TotalFlux > 1.0e-6)
                        {
                            const auto ULight = GeometricRaster::Sobol02Sample(S, B * 4u + 1u, PixelSeed ^ 0x9E3779B9u);
                            const auto UBary  = GeometricRaster::Sobol02Sample(S, B * 4u + 2u, PixelSeed ^ 0x85EBCA6Bu);
                            const auto It = std::lower_bound(Cdf.begin(), Cdf.end(), std::clamp(ULight.X, 0.0f, 0.99999f));
                            const std::size_t EIdx = std::min<std::size_t>(
                                Emissives.size() - 1u, static_cast<std::size_t>(std::distance(Cdf.begin(), It)));
                            const auto& ETri = Emissives[EIdx];
                            const float SelectProb = std::max(1.0e-6f, ETri.TotalFlux * InvTotalFlux);

                            const float SqrtU = std::sqrt(UBary.X);
                            const float B0 = 1.0f - SqrtU;
                            const float B1 = SqrtU * (1.0f - UBary.Y);
                            const float B2 = SqrtU * UBary.Y;
                            const Mathematics::Axis3D Target = ETri.V0 * B0 + ETri.V1 * B1 + ETri.V2 * B2;
                            const Mathematics::Axis3D ToL = Target - HitP;
                            const float Dist2 = Mathematics::EvaluateLengthSquared(ToL);
                            const float Dist  = std::sqrt(std::max(1e-6f, Dist2));
                            const Mathematics::Axis3D LDir = ToL * (1.0f / Dist);
                            const float CosS = Mathematics::EvaluateDot(N, LDir);
                            const float CosL = Mathematics::EvaluateDot(ETri.Normal, LDir * -1.0f);
                            if (CosS > 1e-4f && CosL > 1e-4f)
                            {
                                SpatialAcceleration::RaySegmentQuery VisQ {};
                                VisQ.Origin = HitP + N * 2.5e-3f; VisQ.UnitDirection = LDir;
                                VisQ.MinimumDistance = 1e-3f; VisQ.MaximumDistance = std::max(2e-3f, Dist - 4e-3f);
                                if (!SceneTree.QueryRayOccluded(VisQ))
                                {
                                    const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                                        Mat, Lobes, LocalWo, ToLocal(LDir));
                                    const float PdfArea = SelectProb / std::max(1e-6f, ETri.Area);
                                    const float G = (CosS * CosL) / std::max(1e-4f, Dist2);
                                    const float W = G / PdfArea;
                                    PathRad.R += Throughput.R * Bsdf.R * ETri.Radiance.R * W;
                                    PathRad.G += Throughput.G * Bsdf.G * ETri.Radiance.G * W;
                                    PathRad.B += Throughput.B * Bsdf.B * ETri.Radiance.B * W;
                                }
                            }
                        }

                        // Cosine/GGX continuation bounce (skip emissive on indirect hit to avoid double counting with NEE)
                        const auto UScat = GeometricRaster::Sobol02Sample(S, B * 4u + 3u, PixelSeed ^ 0xC2B2AE35u);
                        const float DiffProb = std::clamp(1.0f - Mat.Specular.Metalness, 0.10f, 0.92f);
                        const auto UChoice = GeometricRaster::Sobol02Sample(S, B * 4u + 4u, PixelSeed ^ 0x27D4EB2Fu);
                        Mathematics::Axis3D LocalWi {};
                        if (UChoice.X < DiffProb)
                        {
                            const float Phi = Constants::TauFloat * UScat.X;
                            const float R   = std::sqrt(UScat.Y);
                            LocalWi = { R * std::cos(Phi), R * std::sin(Phi), std::sqrt(std::max(0.0f, 1.0f - UScat.Y)) };
                            const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                                Mat, Lobes & MaterialSpace::LobeDiffuseEON, LocalWo, LocalWi);
                            const float Pdf = (LocalWi.Z / Constants::PiFloat) * DiffProb;
                            if (Pdf <= 1e-5f) break;
                            const float Scale = LocalWi.Z / Pdf;
                            Throughput.R *= Bsdf.R * Scale;
                            Throughput.G *= Bsdf.G * Scale;
                            Throughput.B *= Bsdf.B * Scale;
                        }
                        else
                        {
                            const float A = std::clamp(Mat.Specular.Roughness * Mat.Specular.Roughness, 0.02f, 1.0f);
                            LocalWi = GeometricRaster::SampleSphericalCapGgxVndfLocal(LocalWo, A, A, UScat.X, UScat.Y);
                            if (LocalWi.Z <= 1e-4f) break;
                            const float Pdf = GeometricRaster::EvaluateSphericalCapGgxVndfPdf(LocalWo, LocalWi, A, A) * (1.0f - DiffProb);
                            if (Pdf <= 1e-5f) break;
                            const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                                Mat, Lobes & ~MaterialSpace::LobeDiffuseEON, LocalWo, LocalWi);
                            const float Scale = LocalWi.Z / Pdf;
                            Throughput.R *= Bsdf.R * Scale;
                            Throughput.G *= Bsdf.G * Scale;
                            Throughput.B *= Bsdf.B * Scale;
                        }
                        CountEmissiveOnHit = false;
                        RayO = HitP + N * 2.5e-3f;
                        RayD = Mathematics::NormalizeAxis(Tu * LocalWi.X + Tv * LocalWi.Y + N * LocalWi.Z);
                    }
                    Sum = Sum + PathRad;
                }
                OutHdr[static_cast<std::size_t>(PY) * W + PX] = Sum * (1.0f / static_cast<float>(Spp));
            }
        }
    });
}

} // namespace

int main()
{
    using namespace Frontier;
    const std::filesystem::path OutDir = "Exhibits/Images/EmissiveShootout";
    std::filesystem::create_directories(OutDir);

    const Mathematics::Axis3D PoseA { -0.85f, 0.05f, 0.0f };
    StudioSceneBundle Bundle = BuildEmissiveOnlyControlVehicleScene(PoseA);

    // Bake Clipmap SDF (72x72x39 static + dynamic vehicle) WITHOUT increasing voxel resolution
    DistanceFieldSpace::VoxelSdfClipmapVolume Clipmap {};
    const Mathematics::Axis3D H0 {
        Bundle.Scene.ClipmapConfig.Clip0HalfExtentMeters,
        Bundle.Scene.ClipmapConfig.Clip0HalfExtentMeters,
        Bundle.Scene.ClipmapConfig.Clip0HalfExtentMeters * 0.55f,
    };
    const Mathematics::Axis3D H1 {
        Bundle.Scene.ClipmapConfig.Clip1HalfExtentMeters,
        Bundle.Scene.ClipmapConfig.Clip1HalfExtentMeters,
        Bundle.Scene.ClipmapConfig.Clip1HalfExtentMeters * 0.45f,
    };
    Clipmap.BuildStaticClipmap(
        Bundle.Scene.SceneTree, Bundle.Scene.MaterialRegister,
        Bundle.Scene.ClipmapConfig.Clip0CenterWorld - H0,
        Bundle.Scene.ClipmapConfig.Clip0CenterWorld + H0,
        72u, 72u, 39u,
        Bundle.Scene.ClipmapConfig.Clip1CenterWorld - H1,
        Bundle.Scene.ClipmapConfig.Clip1CenterWorld + H1,
        48u, 48u, 21u);
    Clipmap.ComposeDynamicObjectsIntoActiveClip0(
        Bundle.Scene.DynamicSdfObjects, {}, Bundle.Scene.Sun, {}, {}, {}, true);

    // Attach vehicle triangles into SceneTree for analytic emissive extraction & BVH visibility
    AttachVehicleToSceneTree(Bundle, PoseA);

    constexpr uint32_t W = 480u;
    constexpr uint32_t H = 270u;
    GeometricRaster::PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = { 3.35f, -4.35f, 1.85f };
    Cam.TargetPoint        = { -0.35f, 0.15f, 0.70f };
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = 44.0f * Constants::DegToRadFloat;
    Cam.ViewportWidthPx    = W;
    Cam.ViewportHeightPx   = H;

    // Shared 64-bit primary visibility (zero denoiser, zero jitter)
    const auto Cull = GeometricRaster::ClusterCullEncoder::EncodePhaseOneVisibility(Cam, Bundle.Scene.ClusterInstances);
    GeometricRaster::VisibilityBufferTargets VisTargets {};
    VisTargets.AllocateClearedImage(W, H);
    GeometricRaster::VisibilityClusterRasterization::RasterizeOpaqueClusters(
        Cam, Bundle.Scene.ClusterInstances, Cull.HardwareClusterBatches, Cull.MicroSoftwareClusters, VisTargets);
    const auto Basis = GeometricRaster::ComputeCameraBasis(Cam);
    std::vector<GeometricRaster::AnalyticResolvedSurfaceSample> Primary(static_cast<std::size_t>(W) * H);
    for (uint32_t PY = 0u; PY < H; ++PY)
    {
        for (uint32_t PX = 0u; PX < W; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * W + PX;
            Primary[Off] = GeometricRaster::VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                PX, PY, VisTargets.OpaquePrimary64[Off], Basis, Basis, Bundle.Scene.ClusterInstances);
        }
    }

    const auto Emissives = ExtractEmissiveTriangles(Bundle.Scene.SceneTree, Bundle.Scene.MaterialRegister);
    const auto Surfels   = BuildDeterministicStudioSurfels(Bundle, Emissives, Bundle.Scene.SceneTree);
    std::printf("[EmissiveShootout] Emissive triangles=%zu | Deterministic studio surfels=%zu\n",
                Emissives.size(), Surfels.size());

    // Reference: 64-SPP NEE Path Tracer (No Denoiser)
    std::vector<Mathematics::LinearRGB> RefHdr;
    std::vector<uint8_t> RefRgba;
    const auto T0Ref = std::chrono::steady_clock::now();
    RenderPathTraceNeeReference64Spp(W, H, Cam, Bundle, Emissives, RefHdr);
    const double RefMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0Ref).count();
    HdrToRgba8(W, H, RefHdr, RefRgba);
    SavePngRgba8(OutDir / "00_Reference_PathTrace64SppNee.png", W, H, RefRgba);

    // Run 1: Analytic Emissive-Triangle LTC + Clipmap SDF Penumbra + Surfel Radiosity
    std::vector<Mathematics::LinearRGB> Algo1Hdr;
    std::vector<uint8_t> Algo1Rgba;
    const auto T0A1 = std::chrono::steady_clock::now();
    RenderAlgorithm1_AnalyticLtcSdfPenumbra(W, H, Primary, Bundle, Clipmap, Emissives, Surfels, Algo1Hdr);
    const double A1Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0A1).count();
    HdrToRgba8(W, H, Algo1Hdr, Algo1Rgba);
    SavePngRgba8(OutDir / "01_Algo1_AnalyticLtc_SdfPenumbra_SurfelBounce.png", W, H, Algo1Rgba);

    // Run 2: Hierarchical Lightcuts + Virtual Spherical Lights (VSL, Hašan 2009)
    std::vector<Mathematics::LinearRGB> Algo2Hdr;
    std::vector<uint8_t> Algo2Rgba;
    const auto T0A2 = std::chrono::steady_clock::now();
    RenderAlgorithm2_LightcutsVsl(W, H, Primary, Bundle, Emissives, Surfels, Algo2Hdr);
    const double A2Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0A2).count();
    HdrToRgba8(W, H, Algo2Hdr, Algo2Rgba);
    SavePngRgba8(OutDir / "02_Algo2_Lightcuts_VirtualSphericalLights.png", W, H, Algo2Rgba);

    // Run 3: Exact Arvo/Baum Triangle Form-Factor + L2 Spherical Harmonic (SH9) Multi-Bounce Field
    std::vector<Mathematics::LinearRGB> Algo3Hdr;
    std::vector<uint8_t> Algo3Rgba;
    const auto T0A3 = std::chrono::steady_clock::now();
    RenderAlgorithm3_ExactTriFormFactorPlusSh9Field(W, H, Primary, Bundle, Emissives, Surfels, Algo3Hdr);
    const double A3Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0A3).count();
    HdrToRgba8(W, H, Algo3Hdr, Algo3Rgba);
    SavePngRgba8(OutDir / "03_Algo3_ExactTriFormFactor_Sh9Field.png", W, H, Algo3Rgba);

    // 2x2 Comparison Sheet: [Algo 1 | Algo 2] / [Algo 3 | 64-SPP NEE Reference]
    const auto QuadSheet = MakeQuadPanel2x2(W, H, Algo1Rgba, Algo2Rgba, Algo3Rgba, RefRgba);
    SavePngRgba8(OutDir / "04_Shootout_Comparison_2x2.png", W * 2u + 6u, H * 2u + 6u, QuadSheet);

    struct AlgoScore
    {
        const char* Name;
        double      Ms;
        float       SrgbRmse;
        float       HdrRmse;
        float       GradRmse;
        float       MeanLum;
    };
    const std::array<AlgoScore, 3u> Scores {{
        { "Algo1_AnalyticLtc_SdfPenumbra", A1Ms, ComputeRmseSrgb(Algo1Rgba, RefRgba), ComputeRmseHdr(Algo1Hdr, RefHdr), ComputeGradientRmseSrgb(W, H, Algo1Rgba, RefRgba), ComputeMeanLum(Algo1Hdr) },
        { "Algo2_Lightcuts_VSL",           A2Ms, ComputeRmseSrgb(Algo2Rgba, RefRgba), ComputeRmseHdr(Algo2Hdr, RefHdr), ComputeGradientRmseSrgb(W, H, Algo2Rgba, RefRgba), ComputeMeanLum(Algo2Hdr) },
        { "Algo3_ExactTriFF_SH9Field",     A3Ms, ComputeRmseSrgb(Algo3Rgba, RefRgba), ComputeRmseHdr(Algo3Hdr, RefHdr), ComputeGradientRmseSrgb(W, H, Algo3Rgba, RefRgba), ComputeMeanLum(Algo3Hdr) },
    }};

    const float RefLum = ComputeMeanLum(RefHdr);
    std::printf("\n=== EMISSIVE GI 3-ALGORITHM SHOOTOUT (Zero Cone GI | Zero Jitter | Zero Denoiser | 72x72x39 SDF) ===\n");
    std::printf("Reference_64Spp_NEE : Time=%8.1f ms | MeanLum=%.4f\n", RefMs, RefLum);
    std::size_t BestIdx = 0u;
    for (std::size_t I = 0u; I < Scores.size(); ++I)
    {
        const auto& S = Scores[I];
        std::printf("%-30s : Time=%8.1f ms | sRGB_RMSE=%.4f | HDR_RMSE=%.4f | Grad_RMSE=%.4f | MeanLum=%.4f\n",
                    S.Name, S.Ms, S.SrgbRmse, S.HdrRmse, S.GradRmse, S.MeanLum);
        if (S.SrgbRmse < Scores[BestIdx].SrgbRmse) BestIdx = I;
    }
    std::printf("[WINNER] %s (lowest sRGB RMSE = %.4f vs 64-SPP NEE Path Trace)\n",
                Scores[BestIdx].Name, Scores[BestIdx].SrgbRmse);

    std::ofstream Json(OutDir / "shootout_metrics.json");
    if (Json.is_open())
    {
        Json << std::fixed << std::setprecision(4)
             << "{\n"
             << "  \"emissive_triangles\": " << Emissives.size() << ",\n"
             << "  \"studio_surfels\": " << Surfels.size() << ",\n"
             << "  \"reference_64spp_ms\": " << RefMs << ",\n"
             << "  \"reference_mean_lum\": " << RefLum << ",\n"
             << "  \"winner\": \"" << Scores[BestIdx].Name << "\",\n"
             << "  \"results\": [\n";
        for (std::size_t I = 0u; I < Scores.size(); ++I)
        {
            const auto& S = Scores[I];
            Json << "    {\"name\": \"" << S.Name << "\", \"ms\": " << S.Ms
                 << ", \"srgb_rmse\": " << S.SrgbRmse
                 << ", \"hdr_rmse\": " << S.HdrRmse
                 << ", \"grad_rmse\": " << S.GradRmse
                 << ", \"mean_lum\": " << S.MeanLum << "}"
                 << (I + 1u < Scores.size() ? "," : "") << "\n";
        }
        Json << "  ]\n}\n";
    }

    return 0;
}
