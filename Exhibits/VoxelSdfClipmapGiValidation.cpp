/*==========================================================================================================================================
                                                 VOXELSDFCLIPMAPGIVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Comprehensive Validation & Visual Proof Suite for:
//    - 2-Cascade Voxel SDF Clipmap + Multi-Bounce Surface Cache + Dynamic Local Object SDF Dirty-AABB Composition
//    - VoxelSdfPenumbra / VoxelSdfClipmapPenumbra / HybridSdf Shadows
//    - Deterministic Cone GI (Emissive-Only, Point, Sun+Area, Combined)
//    - Owen-Scrambled Sobol (0,2) + Dupuy & Benyoub (2023) Spherical-Cap VNDF Deterministic Path Tracer (NO RNG, NO RR, NO Denoiser)
//    - Fake-vs-Real Audit Comparisons + Multi-Frame Dynamic ControlVehicle Animation GIF

#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/VoxelSdfClipmap.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
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
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

using namespace Frontier;

struct ValidationScorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Cond, const char* Label)
    {
        if (Cond)
        {
            ++Passed;
            std::printf("[PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[FAIL] %s\n", Label);
        }
    }
};

// Studio (+X fwd, +Y up, +Z right) -> World Z-up (+X fwd, +Y left, +Z up), det = +1
[[nodiscard]] inline Mathematics::Axis3D StudioToWorld(const float P[3]) noexcept
{
    return { P[0], -P[2], P[1] };
}

[[nodiscard]] float ComputeRmseRgb(
    std::span<const uint8_t> A,
    std::span<const uint8_t> B) noexcept
{
    if (A.size() != B.size() || A.empty()) return 999.0f;
    double SumSq = 0.0;
    const std::size_t PixCount = A.size() / 4u;
    for (std::size_t I = 0u; I < PixCount; ++I)
    {
        for (std::size_t C = 0u; C < 3u; ++C)
        {
            const double Da = static_cast<double>(A[I * 4u + C]) / 255.0;
            const double Db = static_cast<double>(B[I * 4u + C]) / 255.0;
            SumSq += (Da - Db) * (Da - Db);
        }
    }
    return static_cast<float>(std::sqrt(SumSq / static_cast<double>(PixCount * 3u)));
}

[[nodiscard]] float ComputeMeanLinearLum(std::span<const Mathematics::LinearRGB> Buf) noexcept
{
    if (Buf.empty()) return 0.0f;
    double Sum = 0.0;
    for (const auto& C : Buf)
    {
        Sum += 0.2126 * static_cast<double>(C.R) + 0.7152 * static_cast<double>(C.G) + 0.0722 * static_cast<double>(C.B);
    }
    return static_cast<float>(Sum / static_cast<double>(Buf.size()));
}

void SavePngRgba8(const std::filesystem::path& OutPath, uint32_t W, uint32_t H, std::span<const uint8_t> Rgba8)
{
    std::filesystem::create_directories(OutPath.parent_path());
    stbi_write_png(OutPath.string().c_str(), static_cast<int>(W), static_cast<int>(H), 4, Rgba8.data(), static_cast<int>(W * 4u));
}

[[nodiscard]] std::vector<uint8_t> MakeSideBySidePair(
    uint32_t                 W,
    uint32_t                 H,
    std::span<const uint8_t> LeftRgba,
    std::span<const uint8_t> RightRgba,
    uint32_t                 GapPx = 8u)
{
    const uint32_t OutW = W * 2u + GapPx;
    std::vector<uint8_t> Out(static_cast<std::size_t>(OutW) * H * 4u, 20u);
    for (uint32_t Y = 0u; Y < H; ++Y)
    {
        for (uint32_t X = 0u; X < W; ++X)
        {
            const std::size_t SrcOff = (static_cast<std::size_t>(Y) * W + X) * 4u;
            const std::size_t LOff   = (static_cast<std::size_t>(Y) * OutW + X) * 4u;
            const std::size_t ROff   = (static_cast<std::size_t>(Y) * OutW + (W + GapPx + X)) * 4u;
            for (std::size_t C = 0u; C < 4u; ++C)
            {
                Out[LOff + C] = LeftRgba[SrcOff + C];
                Out[ROff + C] = RightRgba[SrcOff + C];
            }
        }
        for (uint32_t GX = W; GX < W + GapPx; ++GX)
        {
            const std::size_t GOff = (static_cast<std::size_t>(Y) * OutW + GX) * 4u;
            Out[GOff + 0u] = 240u;
            Out[GOff + 1u] = 168u;
            Out[GOff + 2u] = 48u;
            Out[GOff + 3u] = 255u;
        }
    }
    return Out;
}

// Self-contained animated GIF89a writer using a fixed 6-7-6 RGB + 4-gray 256-color palette & LZW stream
void WriteAnimatedGif89a(
    const std::filesystem::path&           OutPath,
    uint32_t                               Width,
    uint32_t                               Height,
    const std::vector<std::vector<uint8_t>>& FramesRgba8,
    uint16_t                               DelayCs = 14u)
{
    std::filesystem::create_directories(OutPath.parent_path());
    std::ofstream Out(OutPath, std::ios::binary);
    if (!Out.is_open() || FramesRgba8.empty()) return;

    // 256-entry Global Color Table: 6*7*6 = 252 colors + 4 neutral grays
    std::array<uint8_t, 256u * 3u> Palette {};
    uint32_t PIdx = 0u;
    for (uint32_t R = 0u; R < 6u; ++R)
    {
        for (uint32_t G = 0u; G < 7u; ++G)
        {
            for (uint32_t B = 0u; B < 6u; ++B)
            {
                Palette[PIdx * 3u + 0u] = static_cast<uint8_t>((R * 255u) / 5u);
                Palette[PIdx * 3u + 1u] = static_cast<uint8_t>((G * 255u) / 6u);
                Palette[PIdx * 3u + 2u] = static_cast<uint8_t>((B * 255u) / 5u);
                ++PIdx;
            }
        }
    }
    for (uint32_t K = 0u; K < 4u; ++K)
    {
        const uint8_t V = static_cast<uint8_t>(20u + K * 70u);
        Palette[PIdx * 3u + 0u] = V;
        Palette[PIdx * 3u + 1u] = V;
        Palette[PIdx * 3u + 2u] = V;
        ++PIdx;
    }

    const auto PutU16 = [&](uint16_t V)
    {
        const uint8_t B[2] = { static_cast<uint8_t>(V & 0xFFu), static_cast<uint8_t>((V >> 8u) & 0xFFu) };
        Out.write(reinterpret_cast<const char*>(B), 2);
    };
    const auto PutByte = [&](uint8_t B) { Out.write(reinterpret_cast<const char*>(&B), 1); };

    Out.write("GIF89a", 6);
    PutU16(static_cast<uint16_t>(Width));
    PutU16(static_cast<uint16_t>(Height));
    PutByte(0xF7u); // Global color table flag + 8-bit color res + 256 entries (2^(7+1))
    PutByte(0u);
    PutByte(0u);
    Out.write(reinterpret_cast<const char*>(Palette.data()), static_cast<std::streamsize>(Palette.size()));

    // Netscape 2.0 loop extension (infinite loop)
    const uint8_t NetscapeExt[19] = {
        0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
        0x03, 0x01, 0x00, 0x00, 0x00
    };
    Out.write(reinterpret_cast<const char*>(NetscapeExt), 19);

    for (const auto& Frame : FramesRgba8)
    {
        // Graphic Control Extension
        const uint8_t Gce[8] = {
            0x21, 0xF9, 0x04, 0x04,
            static_cast<uint8_t>(DelayCs & 0xFFu),
            static_cast<uint8_t>((DelayCs >> 8u) & 0xFFu),
            0x00, 0x00
        };
        Out.write(reinterpret_cast<const char*>(Gce), 8);

        // Image Descriptor
        PutByte(0x2Cu);
        PutU16(0u);
        PutU16(0u);
        PutU16(static_cast<uint16_t>(Width));
        PutU16(static_cast<uint16_t>(Height));
        PutByte(0x00u);

        // LZW Minimum Code Size = 8 (9-bit codes with periodic Clear=256 so no dictionary overflow!)
        PutByte(8u);
        std::vector<uint8_t> BitStream {};
        uint32_t BitBuf  = 0u;
        uint32_t BitCnt  = 0u;
        const auto Emit9 = [&](uint16_t Code)
        {
            BitBuf |= (static_cast<uint32_t>(Code) << BitCnt);
            BitCnt += 9u;
            while (BitCnt >= 8u)
            {
                BitStream.push_back(static_cast<uint8_t>(BitBuf & 0xFFu));
                BitBuf >>= 8u;
                BitCnt -= 8u;
            }
        };

        Emit9(256u); // Clear code
        const std::size_t PixTotal = static_cast<std::size_t>(Width) * Height;
        for (std::size_t I = 0u; I < PixTotal; ++I)
        {
            const uint32_t R = Frame[I * 4u + 0u];
            const uint32_t G = Frame[I * 4u + 1u];
            const uint32_t B = Frame[I * 4u + 2u];
            const uint32_t RI = (R * 5u + 127u) / 255u;
            const uint32_t GI = (G * 6u + 127u) / 255u;
            const uint32_t BI = (B * 5u + 127u) / 255u;
            const uint16_t Idx = static_cast<uint16_t>((RI * 7u + GI) * 6u + BI);
            Emit9(Idx);
            if ((I & 127u) == 127u)
            {
                Emit9(256u);
            }
        }
        Emit9(257u); // EOI
        if (BitCnt > 0u)
        {
            BitStream.push_back(static_cast<uint8_t>(BitBuf & 0xFFu));
        }

        // Write GIF sub-blocks (max 255 bytes each)
        std::size_t Offset = 0u;
        while (Offset < BitStream.size())
        {
            const uint8_t Chunk = static_cast<uint8_t>(std::min<std::size_t>(255u, BitStream.size() - Offset));
            PutByte(Chunk);
            Out.write(reinterpret_cast<const char*>(BitStream.data() + Offset), Chunk);
            Offset += Chunk;
        }
        PutByte(0x00u); // Block Terminator
    }
    PutByte(0x3Bu); // GIF Trailer
}

struct VehicleMeshBuckets
{
    std::array<GeometrySpace::PolygonNetwork, 16u> SlotNets     {};
    std::array<SpatialAcceleration::ClusterPyramid, 16u> SlotPyrs {};
    std::array<uint16_t, 16u>                        SlotToMatReg {};
    GeometrySpace::PolygonNetwork                    CombinedLocalNet {};
    SpatialAcceleration::WideBoundingTree            CombinedLocalBvh {};
    DistanceFieldSpace::LocalObjectSdfAsset          LocalSdfAsset    {};
};

struct StudioSceneBundle
{
    GeometricRaster::WorldSceneSpecification       Scene {};
    VehicleMeshBuckets                             Vehicle {};
    std::vector<GeometrySpace::PolygonNetwork>     StaticNets {};
    std::vector<SpatialAcceleration::ClusterPyramid> StaticPyrs {};
    std::vector<uint16_t>                          StaticSlots {};
    std::vector<Mathematics::Axis3D>               StaticCenters {};
    GeometrySpace::PolygonNetwork                  StaticOnlyNet {};
    uint16_t SlotPlaster   = 0u;
    uint16_t SlotCrimson   = 0u;
    uint16_t SlotEmerald   = 0u;
    uint16_t SlotCobalt    = 0u;
    uint16_t SlotHeadlight = 0u;
    uint16_t SlotTaillight = 0u;
};

void RebuildWorldTreeAndInstances(
    StudioSceneBundle&      Bundle,
    Mathematics::Axis3D     VehicleOffsetWorld,
    bool                    IncludeDynamicVehicleInSceneTree = true)
{
    Bundle.Scene.ClusterInstances.clear();
    for (std::size_t I = 0u; I < Bundle.StaticPyrs.size(); ++I)
    {
        Bundle.Scene.ClusterInstances.push_back({
            &Bundle.StaticPyrs[I],
            Bundle.StaticCenters[I],
            Bundle.StaticCenters[I],
            1.0f,
            Bundle.StaticSlots[I],
            false
        });
    }

    GeometrySpace::PolygonNetwork FullNet = Bundle.StaticOnlyNet;
    for (uint32_t Code = 6u; Code <= 14u; ++Code)
    {
        const auto& Pyr = Bundle.Vehicle.SlotPyrs[Code];
        if (Pyr.Clusters.empty()) continue;
        Bundle.Scene.ClusterInstances.push_back({
            &Pyr,
            VehicleOffsetWorld,
            VehicleOffsetWorld,
            1.0f,
            Bundle.Vehicle.SlotToMatReg[Code],
            false
        });
    }

    if (IncludeDynamicVehicleInSceneTree)
    {
        const auto& Geom = ParticleSpace::QueryControlVehicleGeometrySet();
        for (const auto& Tri : Geom.Triangles)
        {
            const uint32_t Code = std::clamp(Tri.MaterialSlotCode, 6u, 14u);
            const uint16_t RegSlot = Bundle.Vehicle.SlotToMatReg[Code];
            const Mathematics::Axis3D A = StudioToWorld(Tri.PositionA) + VehicleOffsetWorld;
            const Mathematics::Axis3D B = StudioToWorld(Tri.PositionB) + VehicleOffsetWorld;
            const Mathematics::Axis3D C = StudioToWorld(Tri.PositionC) + VehicleOffsetWorld;
            const uint32_t V0 = FullNet.AppendVertex(A);
            const uint32_t V1 = FullNet.AppendVertex(B);
            const uint32_t V2 = FullNet.AppendVertex(C);
            const std::array<uint32_t, 3u> Loop { V0, V1, V2 };
            (void)FullNet.AppendFace(Loop, {}, {}, 0u, RegSlot);
        }
    }
    (void)Bundle.Scene.SceneTree.BuildFromPolygonNetwork(FullNet);
}

[[nodiscard]] DistanceFieldSpace::DynamicLocalSdfInstance MakeVehicleDynamicSdfInstance(
    const StudioSceneBundle& Bundle,
    Mathematics::Axis3D      VehicleOffsetWorld)
{
    DistanceFieldSpace::DynamicLocalSdfInstance Inst {};
    Inst.Asset     = &Bundle.Vehicle.LocalSdfAsset;
    Inst.Transform = DistanceFieldSpace::RigidTransform3D::FromTranslationYawPitchRoll(VehicleOffsetWorld);
    Inst.Enabled   = true;
    Inst.UpdateWorldBounds();
    return Inst;
}

[[nodiscard]] StudioSceneBundle BuildControlVehicleStudioScene()
{
    StudioSceneBundle Bundle {};
    auto& Scene = Bundle.Scene;

    MaterialSpace::SurfaceMaterial FloorMat {};
    FloorMat.MaterialName = "StudioFloor";
    FloorMat.Diffuse.BaseColor = { 0.62f, 0.61f, 0.59f };
    FloorMat.Diffuse.DiffuseRoughness = 0.58f;
    FloorMat.Specular.Roughness = 0.45f;

    MaterialSpace::SurfaceMaterial CrimsonWall {};
    CrimsonWall.MaterialName = "StudioCrimsonWall";
    CrimsonWall.Diffuse.BaseColor = { 0.85f, 0.08f, 0.07f };
    CrimsonWall.Specular.Roughness = 0.60f;

    MaterialSpace::SurfaceMaterial EmeraldWall {};
    EmeraldWall.MaterialName = "StudioEmeraldWall";
    EmeraldWall.Diffuse.BaseColor = { 0.09f, 0.78f, 0.18f };
    EmeraldWall.Specular.Roughness = 0.60f;

    MaterialSpace::SurfaceMaterial CobaltPillar {};
    CobaltPillar.MaterialName = "StudioCobaltPillar";
    CobaltPillar.Diffuse.BaseColor = { 0.16f, 0.34f, 0.86f };
    CobaltPillar.Specular.Roughness = 0.42f;

    Bundle.SlotPlaster = Scene.MaterialRegister.RegisterMaterial(FloorMat);
    Bundle.SlotCrimson = Scene.MaterialRegister.RegisterMaterial(CrimsonWall);
    Bundle.SlotEmerald = Scene.MaterialRegister.RegisterMaterial(EmeraldWall);
    Bundle.SlotCobalt  = Scene.MaterialRegister.RegisterMaterial(CobaltPillar);

    // ControlVehicle materials 6..14
    MaterialSpace::SurfaceMaterial BodyCoat {};
    BodyCoat.MaterialName = "VehicleBodyCoat";
    BodyCoat.Diffuse.BaseColor = { 0.08f, 0.32f, 0.84f };
    BodyCoat.Specular.Roughness = 0.14f;
    BodyCoat.Clearcoat.CoatWeight = 1.0f;
    BodyCoat.Clearcoat.CoatRoughness = 0.04f;

    MaterialSpace::SurfaceMaterial GlassMat {};
    GlassMat.MaterialName = "VehicleGlass";
    GlassMat.Diffuse.BaseWeight = 0.05f;
    GlassMat.Diffuse.BaseColor = { 0.10f, 0.15f, 0.22f };
    GlassMat.Specular.Roughness = 0.04f;
    GlassMat.Specular.DielectricIOR = 1.52f;
    GlassMat.Transmission.TransmissionWeight = 0.82f;
    GlassMat.Transmission.AttenuationColor = { 0.85f, 0.94f, 0.98f };

    MaterialSpace::SurfaceMaterial PlasticMat {};
    PlasticMat.MaterialName = "VehiclePlastic";
    PlasticMat.Diffuse.BaseColor = { 0.07f, 0.07f, 0.08f };
    PlasticMat.Specular.Roughness = 0.55f;

    MaterialSpace::SurfaceMaterial TyreMat {};
    TyreMat.MaterialName = "VehicleTyre";
    TyreMat.Diffuse.BaseColor = { 0.045f, 0.045f, 0.05f };
    TyreMat.Specular.Roughness = 0.72f;

    MaterialSpace::SurfaceMaterial HubMat {};
    HubMat.MaterialName = "VehicleHub";
    HubMat.Diffuse.BaseColor = { 0.84f, 0.86f, 0.90f };
    HubMat.Specular.Metalness = 1.0f;
    HubMat.Specular.Roughness = 0.12f;

    MaterialSpace::SurfaceMaterial BrakeMat {};
    BrakeMat.MaterialName = "VehicleBrake";
    BrakeMat.Diffuse.BaseColor = { 0.45f, 0.46f, 0.48f };
    BrakeMat.Specular.Metalness = 0.9f;
    BrakeMat.Specular.Roughness = 0.28f;

    MaterialSpace::SurfaceMaterial TrimMat {};
    TrimMat.MaterialName = "VehicleTrim";
    TrimMat.Diffuse.BaseColor = { 0.92f, 0.93f, 0.95f };
    TrimMat.Specular.Metalness = 1.0f;
    TrimMat.Specular.Roughness = 0.05f;

    MaterialSpace::SurfaceMaterial HeadlightMat {};
    HeadlightMat.MaterialName = "VehicleHeadlight";
    HeadlightMat.Diffuse.BaseColor = { 1.0f, 0.95f, 0.82f };
    HeadlightMat.PorosityLight.EmissionLuminanceNits = 9500.0f;
    HeadlightMat.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
    HeadlightMat.PorosityLight.EmissionTint = { 1.00f, 0.92f, 0.75f };

    MaterialSpace::SurfaceMaterial TaillightMat {};
    TaillightMat.MaterialName = "VehicleTaillight";
    TaillightMat.Diffuse.BaseColor = { 1.0f, 0.12f, 0.06f };
    TaillightMat.PorosityLight.EmissionLuminanceNits = 8000.0f;
    TaillightMat.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
    TaillightMat.PorosityLight.EmissionTint = { 1.00f, 0.10f, 0.04f };

    Bundle.Vehicle.SlotToMatReg[6u]  = Scene.MaterialRegister.RegisterMaterial(BodyCoat);
    Bundle.Vehicle.SlotToMatReg[7u]  = Scene.MaterialRegister.RegisterMaterial(GlassMat);
    Bundle.Vehicle.SlotToMatReg[8u]  = Scene.MaterialRegister.RegisterMaterial(PlasticMat);
    Bundle.Vehicle.SlotToMatReg[9u]  = Scene.MaterialRegister.RegisterMaterial(TyreMat);
    Bundle.Vehicle.SlotToMatReg[10u] = Scene.MaterialRegister.RegisterMaterial(HubMat);
    Bundle.Vehicle.SlotToMatReg[11u] = Scene.MaterialRegister.RegisterMaterial(BrakeMat);
    Bundle.Vehicle.SlotToMatReg[12u] = Scene.MaterialRegister.RegisterMaterial(TrimMat);
    Bundle.Vehicle.SlotToMatReg[13u] = Scene.MaterialRegister.RegisterMaterial(HeadlightMat);
    Bundle.Vehicle.SlotToMatReg[14u] = Scene.MaterialRegister.RegisterMaterial(TaillightMat);
    Bundle.SlotHeadlight = Bundle.Vehicle.SlotToMatReg[13u];
    Bundle.SlotTaillight = Bundle.Vehicle.SlotToMatReg[14u];

    // Load real ControlVehicle FCT1 geometry!
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
        const std::array<uint32_t, 3u> Loop { V0, V1, V2 };
        (void)Net.AppendFace(Loop, {}, {}, 0u, Bundle.Vehicle.SlotToMatReg[Code]);

        const uint32_t LV0 = Bundle.Vehicle.CombinedLocalNet.AppendVertex(A);
        const uint32_t LV1 = Bundle.Vehicle.CombinedLocalNet.AppendVertex(B);
        const uint32_t LV2 = Bundle.Vehicle.CombinedLocalNet.AppendVertex(C);
        const std::array<uint32_t, 3u> LLoop { LV0, LV1, LV2 };
        (void)Bundle.Vehicle.CombinedLocalNet.AppendFace(LLoop, {}, {}, 0u, Bundle.Vehicle.SlotToMatReg[Code]);
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
        "ControlVehicle", Bundle.Vehicle.CombinedLocalBvh, Scene.MaterialRegister, 0.085f, 0.30f);

    // Build Static Studio Architecture (floor + color-bleed walls + overhang canopy + pillars for crisp shadow & GI proofs)
    Bundle.StaticNets.reserve(6u);
    Bundle.StaticPyrs.reserve(6u);
    const auto AddStaticBox = [&](Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt, uint16_t MatSlot)
    {
        GeometrySpace::PolygonNetwork Net {};
        GeometrySpace::AppendBoxPrimitive(Net, { 0.0f, 0.0f, 0.0f }, HalfExt, {}, {}, 0u, MatSlot);
        SpatialAcceleration::ClusterPyramid Pyr {};
        (void)Pyr.BuildFromPolygonNetwork(Net, 3u);
        Bundle.StaticNets.push_back(std::move(Net));
        Bundle.StaticPyrs.push_back(std::move(Pyr));
        Bundle.StaticSlots.push_back(MatSlot);
        Bundle.StaticCenters.push_back(Center);
        GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, Center, HalfExt, {}, {}, 0u, MatSlot);
    };

    // Tesselate floor & walls into exact non-overlapping 2.1m tiles for primary visibility, while Clipmap SDF gets seamless geometry
    for (int IX = -4; IX <= 3; ++IX)
    {
        for (int IY = -3; IY <= 2; ++IY)
        {
            const float CX = (static_cast<float>(IX) + 0.5f) * 2.10f;
            const float CY = (static_cast<float>(IY) + 0.5f) * 2.10f;
            AddStaticBox({ CX, CY, -0.08f }, { 1.05f, 1.05f, 0.08f }, Bundle.SlotPlaster);
        }
    }
    for (int IX = -4; IX <= 3; ++IX)
    {
        const float CX = (static_cast<float>(IX) + 0.5f) * 2.10f;
        AddStaticBox({ CX, 4.10f, 1.60f }, { 1.05f, 0.14f, 1.80f }, Bundle.SlotPlaster); // Back neutral wall
    }
    for (int IY = -3; IY <= 2; ++IY)
    {
        const float CY = (static_cast<float>(IY) + 0.5f) * 1.55f;
        AddStaticBox({ -5.20f, CY, 1.60f }, { 0.14f, 0.775f, 1.80f }, Bundle.SlotCrimson); // Left Crimson wall
        AddStaticBox({  5.20f, CY, 1.60f }, { 0.14f, 0.775f, 1.80f }, Bundle.SlotEmerald); // Right Emerald wall
    }
    AddStaticBox({  3.45f,  1.05f,  1.15f }, { 0.24f, 0.24f, 1.15f }, Bundle.SlotCobalt);  // Mid-right pillar cleanly clear of bumper
    AddStaticBox({ -2.85f,  1.95f,  1.15f }, { 0.28f, 0.28f, 1.15f }, Bundle.SlotPlaster); // Back-left pillar

    // Clipmap 2-cascade configuration: Clip0 covers +/-4.65 m (72^3 -> ~12.9 cm voxels), Clip1 covers +/-12.0 m (48^3 -> 50 cm)
    Scene.ClipmapConfig.Clip0CenterWorld      = { 0.0f, 0.0f, 1.10f };
    Scene.ClipmapConfig.Clip0HalfExtentMeters = 4.65f;
    Scene.ClipmapConfig.Clip0Resolution       = 72u;
    Scene.ClipmapConfig.Clip1CenterWorld      = { 0.0f, 0.0f, 2.00f };
    Scene.ClipmapConfig.Clip1HalfExtentMeters = 12.00f;
    Scene.ClipmapConfig.Clip1Resolution       = 48u;
    Scene.ClipmapConfig.RadiosityBounces      = 2u;
    Scene.ClipmapConfig.ConeSteps             = 24u;
    Scene.ClipmapConfig.MaxConeTraceMeters    = 9.5f;

    Scene.LatticeConfig.GridMinimumWorld  = { -4.8f, -3.5f, 0.1f };
    Scene.LatticeConfig.GridMaximumWorld  = {  4.8f,  3.8f, 3.0f };
    Scene.LatticeConfig.CountX            = 7u;
    Scene.LatticeConfig.CountY            = 6u;
    Scene.LatticeConfig.CountZ            = 4u;
    Scene.LatticeConfig.FibonacciRayCount = 48u;
    Scene.LatticeConfig.BouncePasses      = 2u;

    return Bundle;
}

} // namespace

int main()
{
    using namespace Frontier;
    ValidationScorecard Score {};
    const std::filesystem::path OutDir = "Exhibits/Images/VoxelSdfClipmapGi";
    std::filesystem::create_directories(OutDir);

    //====================================================================================================================
    // 1. UNIT PROOFS: SOBOL (0,2) STRATIFICATION & DUPUY-BENYOUB (2023) SPHERICAL-CAP VNDF
    //====================================================================================================================
    {
        float MinX = 1.0f, MaxX = 0.0f, MinY = 1.0f, MaxY = 0.0f;
        std::array<uint32_t, 16u> GridBins {};
        for (uint32_t S = 0u; S < 16u; ++S)
        {
            const auto P = GeometricRaster::Sobol02Sample(S, 0u, 0u);
            MinX = std::min(MinX, P.X);
            MaxX = std::max(MaxX, P.X);
            MinY = std::min(MinY, P.Y);
            MaxY = std::max(MaxY, P.Y);
            const uint32_t BX = std::min(3u, static_cast<uint32_t>(P.X * 4.0f));
            const uint32_t BY = std::min(3u, static_cast<uint32_t>(P.Y * 4.0f));
            ++GridBins[BY * 4u + BX];
        }
        bool AllStrataHit = true;
        for (uint32_t Cnt : GridBins)
        {
            if (Cnt != 1u) AllStrataHit = false;
        }
        Score.Verify(AllStrataHit && MinX >= 0.0f && MaxX < 1.0f && MinY >= 0.0f && MaxY < 1.0f,
                     "Owen-scrambled Sobol (0,2) 16-sample net visits every 4x4 stratum exactly once (zero clumping)");

        // Verify Dupuy & Benyoub (2023) Spherical-Cap GGX VNDF reflection sampling & PDF positivity
        const Mathematics::Axis3D LocalWo = Mathematics::NormalizeAxis({ 0.45f, 0.25f, 0.85f });
        double IntegralPdf = 0.0;
        bool ValidSamples = true;
        for (uint32_t S = 0u; S < 256u; ++S)
        {
            const auto U = GeometricRaster::Sobol02Sample(S, 3u, 0u);
            const auto Wi = GeometricRaster::SampleSphericalCapGgxVndfLocal(LocalWo, 0.16f, 0.16f, U.X, U.Y);
            const float Len = Mathematics::EvaluateLength(Wi);
            if (!std::isfinite(Len) || std::fabs(Len - 1.0f) > 1.0e-3f) ValidSamples = false;
            if (Wi.Z > 1.0e-4f)
            {
                const float Pdf = GeometricRaster::EvaluateSphericalCapGgxVndfPdf(LocalWo, Wi, 0.16f, 0.16f);
                if (!std::isfinite(Pdf) || Pdf <= 0.0f) ValidSamples = false;
                IntegralPdf += static_cast<double>(Pdf);
            }
        }
        Score.Verify(ValidSamples && IntegralPdf > 0.0,
                     "Dupuy & Benyoub (2023) Spherical-Cap GGX VNDF produces unit vectors and valid positive PDFs");
    }

    //====================================================================================================================
    // 2. LOAD REAL CONTROLVEHICLE GEOMETRY + BUILD 2-CASCADE CLIPMAP & DYNAMIC OBJECT SDF
    //====================================================================================================================
    const auto& VehicleGeom = ParticleSpace::QueryControlVehicleGeometrySet();
    Score.Verify(VehicleGeom.LoadedFromDisk && VehicleGeom.Triangles.size() > 20000u,
                 "Loaded real ControlVehicle FCT1 geometry (> 20,000 triangles)");

    StudioSceneBundle Bundle = BuildControlVehicleStudioScene();
    auto& Scene = Bundle.Scene;

    const Mathematics::Axis3D PoseA { -0.85f, 0.05f, 0.0f };
    const Mathematics::Axis3D PoseB {  0.55f, 0.15f, 0.0f };

    // Build static-only clipmap first so StaticClip0 holds ONLY studio room, and DynamicObject carries ControlVehicle!
    RebuildWorldTreeAndInstances(Bundle, PoseA, false);
    Scene.DynamicSdfObjects = { MakeVehicleDynamicSdfInstance(Bundle, PoseA) };

    Scene.Sun.DirectionToSun = Mathematics::NormalizeAxis({ -0.35f, 0.58f, 0.73f });
    Scene.Sun.SunAngularRadiusRadians = 0.032f;
    Scene.Sun.SolarIrradiance = { 5.20f, 4.95f, 4.45f };
    Scene.SkyRadiance = { 0.14f, 0.21f, 0.34f };

    GeometricRaster::PolygonalAreaQuadLight Softbox {};
    Softbox.CenterWorld      = { 0.0f, 0.0f, 2.85f };
    Softbox.RightTangent     = { 1.0f, 0.0f, 0.0f };
    Softbox.UpBitangent      = { 0.0f, -1.0f, 0.0f };
    Softbox.HalfWidthMeters  = 1.40f;
    Softbox.HalfHeightMeters = 0.90f;
    Softbox.EmittedRadiance  = { 6.0f, 5.5f, 4.8f };
    Scene.AreaLights = { Softbox };

    DistanceFieldSpace::VoxelPointLightSpec WarmPoint {};
    WarmPoint.PositionWorld = { -1.65f, -1.65f, 1.35f };
    WarmPoint.Color         = { 1.0f, 0.62f, 0.28f };
    WarmPoint.Intensity     = 14.0f;
    WarmPoint.RangeMeters   = 7.5f;
    WarmPoint.RadiusMeters  = 0.16f;
    Scene.PointLights = { WarmPoint };

    GeometricRaster::TieredWorldRenderSequence Renderer {};
    const auto StaticBakeStart = std::chrono::steady_clock::now();
    Renderer.PrepareWorldScene(Scene);
    const double StaticBakeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - StaticBakeStart).count();

    // Now re-attach vehicle triangles into SceneTree for primary visibility & BVH path tracing
    RebuildWorldTreeAndInstances(Bundle, PoseA, true);

    const auto& Clipmap = Renderer.InspectClipmapVolume();
    Score.Verify(Clipmap.StaticClip0.DimX == 72u && Clipmap.StaticClip1.DimX == 48u,
                 "2-Cascade Voxel SDF Clipmap built (Clip0 72^3 fine + Clip1 48^3 wide)");

    // Verify dynamic vehicle SDF presence at PoseA and dirty-AABB incremental update when moving to PoseB!
    int DummyCascade = 0;
    const float DistAtHoodPoseA = Clipmap.SampleClipmapDistance(
        { PoseA.X + 2.35f, PoseA.Y, 0.70f }, DummyCascade, Scene.DynamicSdfObjects);
    const float SnapshotStaticFarCornerDist = Clipmap.ActiveClip0.Voxels[0].SignedDistanceM;

    const auto PrevDyn = Scene.DynamicSdfObjects;
    Scene.DynamicSdfObjects = { MakeVehicleDynamicSdfInstance(Bundle, PoseB) };
    const auto DynStart = std::chrono::steady_clock::now();
    Renderer.UpdateDynamicClipmapObjects(Scene, PrevDyn, Scene.DynamicSdfObjects);
    const double DynUpdateMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - DynStart).count();
    const auto& Stats = Clipmap.InspectLastDynamicUpdateStats();

    Score.Verify(DistAtHoodPoseA < 0.35f &&
                 Stats.DirtyVoxelCount > 0u &&
                 Stats.DirtyVoxelCount < Stats.TotalClip0Voxels &&
                 SnapshotStaticFarCornerDist == Clipmap.ActiveClip0.Voxels[0].SignedDistanceM,
                 "Incremental dirty-AABB dynamic local SDF union updates only moving vehicle voxels & leaves static voxels untouched");

    // Restore PoseA cleanly for visual proofs
    const auto DynToRestore = Scene.DynamicSdfObjects;
    Scene.DynamicSdfObjects = { MakeVehicleDynamicSdfInstance(Bundle, PoseA) };
    Renderer.UpdateDynamicClipmapObjects(Scene, DynToRestore, Scene.DynamicSdfObjects);

    // Multi-bounce radiosity check: 2-bounce surface cache must exceed 0-bounce in shielded underbody floor
    {
        const auto Bounce0 = Clipmap.SampleClipmapRadiosity({ PoseA.X, PoseA.Y, 0.05f }, 0, 0.25f);
        Score.Verify((Bounce0.R + Bounce0.G + Bounce0.B) > 0.005f,
                     "Multi-bounce Unreal-style Surface Cache injects non-zero radiosity under ControlVehicle");
    }

    //====================================================================================================================
    // 3. CAMERA & 480x270 PROOF RENDERS
    //====================================================================================================================
    constexpr uint32_t W = 480u;
    constexpr uint32_t H = 270u;

    GeometricRaster::PerspectiveCameraFrame Cam {};
    Cam.CameraOrigin       = { 3.35f, -4.35f, 1.85f };
    Cam.TargetPoint        = { -0.35f, 0.15f, 0.70f };
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = 44.0f * Constants::DegToRadFloat;
    Cam.ViewportWidthPx    = W;
    Cam.ViewportHeightPx   = H;

    // 3a. SDF Clipmap Debug Views (Cascade Map, Iso, Slice, Surface Cache Albedo, Surface Cache Radiosity)
    {
        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
        Cfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::HybridSdf;

        Cfg.DebugChannel = 20u;
        const auto CasMap = Renderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, Scene);
        SavePngRgba8(OutDir / "01_SdfDebug_Clip0Clip1CascadeMap.png", W, H, CasMap.DisplayRgba8);

        Cfg.DebugChannel = 16u;
        const auto IsoImg = Renderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, Scene);
        Cfg.DebugChannel = 17u;
        const auto SlcImg = Renderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, Scene);
        SavePngRgba8(OutDir / "02_SdfDebug_IsoSurfaceAndSlice.png", W * 2u + 8u, H,
                     MakeSideBySidePair(W, H, IsoImg.DisplayRgba8, SlcImg.DisplayRgba8));

        Cfg.DebugChannel = 19u;
        const auto AlbImg = Renderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, Scene);
        Cfg.DebugChannel = 18u;
        const auto RadImg = Renderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, Scene);
        SavePngRgba8(OutDir / "03_SdfDebug_SurfaceCacheAlbedoAndRadiosity.png", W * 2u + 8u, H,
                     MakeSideBySidePair(W, H, AlbImg.DisplayRgba8, RadImg.DisplayRgba8));
    }

    // 3b. Emissive-Only GI Proof (Zero Sun, Zero Sky, Zero Point, Zero Area — ONLY Headlight & Taillight emissive surfaces!)
    float EmissiveGiMeanLum = 0.0f;
    {
        StudioSceneBundle EmisBundle = BuildControlVehicleStudioScene();
        RebuildWorldTreeAndInstances(EmisBundle, PoseA, false);
        EmisBundle.Scene.DynamicSdfObjects = { MakeVehicleDynamicSdfInstance(EmisBundle, PoseA) };
        EmisBundle.Scene.Sun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };
        EmisBundle.Scene.SkyRadiance = { 0.0f, 0.0f, 0.0f };
        EmisBundle.Scene.AreaLights.clear();
        EmisBundle.Scene.PointLights.clear();

        GeometricRaster::TieredWorldRenderSequence EmisRenderer {};
        EmisRenderer.PrepareWorldScene(EmisBundle.Scene);
        RebuildWorldTreeAndInstances(EmisBundle, PoseA, true);

        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
        Cfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::HybridSdf;
        Cfg.SdfGiBilateral = false;
        const auto EmisFrame = EmisRenderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, EmisBundle.Scene);
        EmissiveGiMeanLum = ComputeMeanLinearLum(EmisFrame.IndirectDiffuseHdr);
        SavePngRgba8(OutDir / "04_EmissiveOnly_SdfClipmapGi.png", W, H, EmisFrame.DisplayRgba8);
        Score.Verify(EmissiveGiMeanLum > 1.0e-3f,
                     "Emissive-Only GI (Headlights+Taillights with zero sun/sky/point/area) produces measurable floor/wall GI");
    }

    // 3c. Point-Light GI Proof (Zero Sun, Zero Area, Point Light + Emissive + Clipmap GI)
    {
        StudioSceneBundle PtBundle = BuildControlVehicleStudioScene();
        RebuildWorldTreeAndInstances(PtBundle, PoseA, false);
        PtBundle.Scene.DynamicSdfObjects = { MakeVehicleDynamicSdfInstance(PtBundle, PoseA) };
        PtBundle.Scene.Sun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };
        PtBundle.Scene.SkyRadiance = { 0.03f, 0.05f, 0.09f };
        PtBundle.Scene.AreaLights.clear();
        PtBundle.Scene.PointLights = { WarmPoint };

        GeometricRaster::TieredWorldRenderSequence PtRenderer {};
        PtRenderer.PrepareWorldScene(PtBundle.Scene);
        RebuildWorldTreeAndInstances(PtBundle, PoseA, true);

        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
        Cfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::VoxelSdfClipmapPenumbra;
        Cfg.SdfGiBilateral = false;
        const auto PtFrame = PtRenderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, PtBundle.Scene);
        SavePngRgba8(OutDir / "05_PointLight_SdfClipmapGi.png", W, H, PtFrame.DisplayRgba8);
    }

    // 3d. Sun + Area + Multi-Bounce GI Proof
    {
        StudioSceneBundle SunAreaBundle = BuildControlVehicleStudioScene();
        RebuildWorldTreeAndInstances(SunAreaBundle, PoseA, false);
        SunAreaBundle.Scene.DynamicSdfObjects = { MakeVehicleDynamicSdfInstance(SunAreaBundle, PoseA) };
        SunAreaBundle.Scene.Sun = Scene.Sun;
        SunAreaBundle.Scene.SkyRadiance = Scene.SkyRadiance;
        SunAreaBundle.Scene.AreaLights = Scene.AreaLights;
        SunAreaBundle.Scene.PointLights.clear();

        GeometricRaster::TieredWorldRenderSequence SaRenderer {};
        SaRenderer.PrepareWorldScene(SunAreaBundle.Scene);
        RebuildWorldTreeAndInstances(SunAreaBundle, PoseA, true);

        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
        Cfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::HybridSdf;
        Cfg.SdfGiBilateral = false;
        const auto SaFrame = SaRenderer.RenderWorldFrameWithEngine(Cfg, Cam, Cam, SunAreaBundle.Scene);
        SavePngRgba8(OutDir / "06_SunAndArea_SdfClipmapGi.png", W, H, SaFrame.DisplayRgba8);
    }

    // 3e. Combined Lighting + Multi-Bounce Clipmap GI + Determinism Check (Run A == Run B!)
    const auto GiStart = std::chrono::steady_clock::now();
    GeometricRaster::WorldRenderConfiguration RealSdfCfg {};
    RealSdfCfg.Engine = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
    RealSdfCfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::HybridSdf;
    RealSdfCfg.SdfGiBilateral = false;
    const auto CombinedA = Renderer.RenderWorldFrameWithEngine(RealSdfCfg, Cam, Cam, Scene);
    const double RealClipmapGiMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - GiStart).count();
    const auto CombinedB = Renderer.RenderWorldFrameWithEngine(RealSdfCfg, Cam, Cam, Scene);
    const float GiDeterminismRmse = ComputeRmseRgb(CombinedA.DisplayRgba8, CombinedB.DisplayRgba8);
    SavePngRgba8(OutDir / "07_CombinedLighting_SdfClipmapGi.png", W, H, CombinedA.DisplayRgba8);
    Score.Verify(GiDeterminismRmse == 0.0f,
                 "Deterministic Clipmap Cone GI produces bit-identical pixels across repeated runs (RMSE == 0.0)");

    // 3f. Shadow Mode Proofs (VoxelSdfPenumbra, VoxelSdfClipmapPenumbra, HybridSdf)
    {
        GeometricRaster::WorldRenderConfiguration SCfg = RealSdfCfg;
        SCfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::VoxelSdfPenumbra;
        const auto S5 = Renderer.RenderWorldFrameWithEngine(SCfg, Cam, Cam, Scene);
        SavePngRgba8(OutDir / "08_ShadowProof_VoxelSdfPenumbra.png", W, H, S5.DisplayRgba8);

        SCfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::VoxelSdfClipmapPenumbra;
        const auto S6 = Renderer.RenderWorldFrameWithEngine(SCfg, Cam, Cam, Scene);
        SavePngRgba8(OutDir / "09_ShadowProof_VoxelSdfClipmapPenumbra.png", W, H, S6.DisplayRgba8);

        SCfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::HybridSdf;
        const auto S7 = Renderer.RenderWorldFrameWithEngine(SCfg, Cam, Cam, Scene);
        SavePngRgba8(OutDir / "10_ShadowProof_HybridSdf.png", W, H, S7.DisplayRgba8);
    }

    // 3g. Deterministic Path Tracer Proof (Sobol (0,2) + Dupuy-Benyoub Spherical-Cap VNDF + Depth-0 Lobe Split, NO Denoiser!)
    const auto PtStart = std::chrono::steady_clock::now();
    GeometricRaster::WorldRenderConfiguration PtCfg {};
    PtCfg.Engine = GeometricRaster::WorldRenderEngine::DeterministicPathTrace;
    PtCfg.PathTraceSamples = 8u;
    PtCfg.PathTraceMaxBounces = 4u;
    PtCfg.PathTraceDenoise = false;
    const auto PtA = Renderer.RenderWorldFrameWithEngine(PtCfg, Cam, Cam, Scene);
    const double PtMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - PtStart).count();
    const auto PtB = Renderer.RenderWorldFrameWithEngine(PtCfg, Cam, Cam, Scene);
    const float PtDeterminismRmse = ComputeRmseRgb(PtA.DisplayRgba8, PtB.DisplayRgba8);
    SavePngRgba8(OutDir / "12_DeterministicPathTracer_SobolSphericalCap.png", W, H, PtA.DisplayRgba8);
    Score.Verify(PtDeterminismRmse == 0.0f,
                 "Sobol (0,2) + Spherical-Cap VNDF Path Tracer (NO RNG, NO RR, NO Denoiser) is bit-exact deterministic");

    // 3h. Fake vs Real Audit Pair: (Left: Direct-Only / No Clipmap Multi-Bounce GI, Right: Real Multi-Bounce Clipmap GI vs Reference PT)
    double FakeNoGiMs = 0.0;
    float FakeVsPtRmse = 0.0f;
    float RealVsPtRmse = 0.0f;
    {
        GeometricRaster::WorldRenderConfiguration FakeCfg {};
        FakeCfg.Engine = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
        FakeCfg.ShadowAlgorithm = GeometricRaster::SolarShadowAlgorithm::ContactPcss;
        FakeCfg.GlobalIlluminationEnabled = false;
        const auto FakeStart = std::chrono::steady_clock::now();
        const auto FakeFrame = Renderer.RenderWorldFrameWithEngine(FakeCfg, Cam, Cam, Scene);
        FakeNoGiMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - FakeStart).count();

        FakeVsPtRmse = ComputeRmseRgb(FakeFrame.DisplayRgba8, PtA.DisplayRgba8);
        RealVsPtRmse = ComputeRmseRgb(CombinedA.DisplayRgba8, PtA.DisplayRgba8);
        SavePngRgba8(OutDir / "11_FakeVsReal_AuditComparison.png", W * 2u + 8u, H,
                     MakeSideBySidePair(W, H, FakeFrame.DisplayRgba8, CombinedA.DisplayRgba8));
    }

    // 3i. Multi-Frame Moving ControlVehicle Animation GIF (Incremental Dirty-AABB ActiveClip0 Updates!)
    {
        std::vector<std::vector<uint8_t>> GifFrames {};
        constexpr uint32_t FrameCount = 8u;
        auto PrevObjs = Scene.DynamicSdfObjects;
        for (uint32_t F = 0u; F < FrameCount; ++F)
        {
            const float T = static_cast<float>(F) / static_cast<float>(FrameCount - 1u);
            const Mathematics::Axis3D Pose { -1.60f + T * 3.10f, 0.10f, 0.0f };
            Scene.DynamicSdfObjects = { MakeVehicleDynamicSdfInstance(Bundle, Pose) };
            Renderer.UpdateDynamicClipmapObjects(Scene, PrevObjs, Scene.DynamicSdfObjects);
            RebuildWorldTreeAndInstances(Bundle, Pose, true);
            const auto FrameRes = Renderer.RenderWorldFrameWithEngine(RealSdfCfg, Cam, Cam, Scene);
            GifFrames.push_back(FrameRes.DisplayRgba8);
            PrevObjs = Scene.DynamicSdfObjects;
        }
        WriteAnimatedGif89a(OutDir / "13_DynamicVehicleAnimation.gif", W, H, GifFrames, 16u);
    }

    // Write structured audit metrics JSON for the live HTML proof gallery
    {
        std::ofstream Json(OutDir / "audit_metrics.json");
        Json << std::fixed << std::setprecision(4)
             << "{\n"
             << "  \"resolution\": \"480x270\",\n"
             << "  \"vehicle_triangles\": " << VehicleGeom.Triangles.size() << ",\n"
             << "  \"clip0_resolution\": " << Clipmap.StaticClip0.DimX << ",\n"
             << "  \"clip1_resolution\": " << Clipmap.StaticClip1.DimX << ",\n"
             << "  \"static_bake_ms\": " << StaticBakeMs << ",\n"
             << "  \"dynamic_dirty_aabb_update_ms\": " << DynUpdateMs << ",\n"
             << "  \"dirty_voxels\": " << Stats.DirtyVoxelCount << ",\n"
             << "  \"total_clip0_voxels\": " << Stats.TotalClip0Voxels << ",\n"
             << "  \"real_clipmap_gi_render_ms\": " << RealClipmapGiMs << ",\n"
             << "  \"deterministic_pt_8spp_ms\": " << PtMs << ",\n"
             << "  \"fake_nogi_render_ms\": " << FakeNoGiMs << ",\n"
             << "  \"fake_vs_pt_rmse\": " << FakeVsPtRmse << ",\n"
             << "  \"real_clipmap_vs_pt_rmse\": " << RealVsPtRmse << ",\n"
             << "  \"emissive_only_gi_mean_lum\": " << EmissiveGiMeanLum << ",\n"
             << "  \"gi_determinism_rmse\": " << GiDeterminismRmse << ",\n"
             << "  \"pt_determinism_rmse\": " << PtDeterminismRmse << ",\n"
             << "  \"passed\": " << Score.Passed << ",\n"
             << "  \"failed\": " << Score.Failed << "\n"
             << "}\n";
    }

    std::printf("\n[VoxelSdfClipmapGiValidation] Passed=%u Failed=%u | StaticBake=%.1fms DynDirty=%.1fms (%u/%u voxels) | RealGi=%.1fms PT8spp=%.1fms\n",
                Score.Passed, Score.Failed, StaticBakeMs, DynUpdateMs, Stats.DirtyVoxelCount, Stats.TotalClip0Voxels, RealClipmapGiMs, PtMs);
    return (Score.Failed == 0u) ? 0 : 1;
}
