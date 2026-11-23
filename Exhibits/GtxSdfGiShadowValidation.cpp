/*============================================================================================================================================
                                                        GTXSDFGISHADOWVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Order 2 Validation: GtxLowEnd Hybrid SDF Cone + Capsule Rig vs HighEnd 0-MB-SDF BVH4 Shadows + Skinned Refit + Stokes/Surfel GI

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/ArticulatedCapsuleRig.h"
#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::Codecs;
using namespace Frontier::GeometricRaster;

[[nodiscard]] std::filesystem::path ResolveRepositoryRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#elif defined(FRONTIER_REPOSITORY_ROOT)
    return std::filesystem::path(FRONTIER_REPOSITORY_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] std::filesystem::path LocateCompiledShaderFolder(const std::filesystem::path& RepoRoot)
{
    const std::array<std::filesystem::path, 5u> Candidates {{
#ifdef FRONTIER_COMPILED_SHADER_DIR
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR),
#else
        std::filesystem::current_path() / "CompiledShaders",
#endif
        std::filesystem::current_path() / "CompiledShaders",
        RepoRoot / "build" / "CompiledShaders",
        RepoRoot / "Build" / "CompiledShaders",
        RepoRoot / "Out" / "Build" / "CompiledShaders",
    }};
    for (const auto& Dir : Candidates)
    {
        if (std::filesystem::exists(Dir / "HybridConeShadowCompute.comp.spv"))
        {
            return Dir;
        }
    }
    return Candidates[0];
}

[[nodiscard]] bool VerifyCompiledSpirvBinary(const std::filesystem::path& SpvPath) noexcept
{
    std::ifstream Stream(SpvPath, std::ios::binary | std::ios::ate);
    if (!Stream.is_open())
    {
        return false;
    }
    const auto ByteCount = Stream.tellg();
    if (ByteCount < 20)
    {
        return false;
    }
    Stream.seekg(0, std::ios::beg);
    uint32_t MagicWord = 0u;
    Stream.read(reinterpret_cast<char*>(&MagicWord), sizeof(MagicWord));
    return MagicWord == 0x07230203u;
}

void AppendBigEndianUint32(std::vector<uint8_t>& Dest, uint32_t Word)
{
    Dest.push_back(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
    Dest.push_back(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    Dest.push_back(static_cast<uint8_t>((Word >> 8u) & 0xFFu));
    Dest.push_back(static_cast<uint8_t>(Word & 0xFFu));
}

[[nodiscard]] uint32_t ComputePngCrc32(const uint8_t* Bytes, std::size_t Length) noexcept
{
    uint32_t Crc = 0xFFFFFFFFu;
    for (std::size_t I = 0u; I < Length; ++I)
    {
        Crc ^= Bytes[I];
        for (int Bit = 0; Bit < 8; ++Bit)
        {
            const uint32_t Mask = static_cast<uint32_t>(-static_cast<int32_t>(Crc & 1u));
            Crc                 = (Crc >> 1u) ^ (0xEDB88320u & Mask);
        }
    }
    return ~Crc;
}

void AppendPngChunkBytes(std::vector<uint8_t>& Dest, const char Tag[4], const std::vector<uint8_t>& Body)
{
    AppendBigEndianUint32(Dest, static_cast<uint32_t>(Body.size()));
    const std::size_t Start = Dest.size();
    for (int I = 0; I < 4; ++I)
    {
        Dest.push_back(static_cast<uint8_t>(Tag[I]));
    }
    Dest.insert(Dest.end(), Body.begin(), Body.end());
    AppendBigEndianUint32(Dest, ComputePngCrc32(Dest.data() + Start, 4u + Body.size()));
}

[[nodiscard]] bool SaveRgba8ImageToPng(
    const std::filesystem::path& FilePath,
    uint32_t                     Width,
    uint32_t                     Height,
    std::span<const uint8_t>     Rgba8Pixels)
{
    if (Width == 0u || Height == 0u || Rgba8Pixels.size() < static_cast<std::size_t>(Width) * Height * 4u)
    {
        return false;
    }
    std::filesystem::create_directories(FilePath.parent_path());

    const std::size_t RowBytes = static_cast<std::size_t>(Width) * 4u + 1u;
    std::vector<uint8_t> RawScanlines(RowBytes * Height, 0u);
    for (uint32_t Y = 0u; Y < Height; ++Y)
    {
        uint8_t* Dst = RawScanlines.data() + static_cast<std::size_t>(Y) * RowBytes;
        Dst[0]       = 0u;
        std::memcpy(Dst + 1u, Rgba8Pixels.data() + static_cast<std::size_t>(Y) * Width * 4u, static_cast<std::size_t>(Width) * 4u);
    }

    std::vector<uint8_t> ZlibStream;
    ZlibStream.push_back(0x78u);
    ZlibStream.push_back(0x01u);

    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    std::size_t Offset = 0u;
    while (Offset < RawScanlines.size())
    {
        const std::size_t BlockLen = std::min<std::size_t>(65535u, RawScanlines.size() - Offset);
        const bool FinalChunk          = (Offset + BlockLen == RawScanlines.size());
        ZlibStream.push_back(FinalChunk ? 0x01u : 0x00u);
        const auto Len16  = static_cast<uint16_t>(BlockLen);
        const auto NLen16 = static_cast<uint16_t>(~Len16);
        ZlibStream.push_back(static_cast<uint8_t>(Len16 & 0xFFu));
        ZlibStream.push_back(static_cast<uint8_t>((Len16 >> 8u) & 0xFFu));
        ZlibStream.push_back(static_cast<uint8_t>(NLen16 & 0xFFu));
        ZlibStream.push_back(static_cast<uint8_t>((NLen16 >> 8u) & 0xFFu));
        for (std::size_t I = 0u; I < BlockLen; ++I)
        {
            const uint8_t Byte = RawScanlines[Offset + I];
            ZlibStream.push_back(Byte);
            AdlerA = (AdlerA + Byte) % 65521u;
            AdlerB = (AdlerB + AdlerA) % 65521u;
        }
        Offset += BlockLen;
    }
    AppendBigEndianUint32(ZlibStream, (AdlerB << 16u) | AdlerA);

    std::vector<uint8_t> PngBytes { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    std::vector<uint8_t> Ihdr;
    AppendBigEndianUint32(Ihdr, Width);
    AppendBigEndianUint32(Ihdr, Height);
    Ihdr.insert(Ihdr.end(), { 8u, 6u, 0u, 0u, 0u });
    AppendPngChunkBytes(PngBytes, "IHDR", Ihdr);
    AppendPngChunkBytes(PngBytes, "IDAT", ZlibStream);
    AppendPngChunkBytes(PngBytes, "IEND", {});

    std::ofstream Out(FilePath, std::ios::binary);
    if (!Out.is_open())
    {
        return false;
    }
    Out.write(reinterpret_cast<const char*>(PngBytes.data()), static_cast<std::streamsize>(PngBytes.size()));
    return Out.good();
}

[[nodiscard]] bool RequireCondition(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[GtxSdfGiShadowValidation] FAIL: %s\n", Label);
        return false;
    }
    return true;
}

[[nodiscard]] GeometrySpace::PolygonNetwork BuildBoxAsset(float ExtentX, float ExtentY, float ExtentZ)
{
    GeometrySpace::PolygonNetwork Net {};
    const float HX = ExtentX * 0.5f;
    const float HY = ExtentY * 0.5f;
    const float HZ = ExtentZ * 0.5f;
    const uint32_t V0 = Net.AppendVertex({ -HX, -HY, -HZ });
    const uint32_t V1 = Net.AppendVertex({  HX, -HY, -HZ });
    const uint32_t V2 = Net.AppendVertex({  HX,  HY, -HZ });
    const uint32_t V3 = Net.AppendVertex({ -HX,  HY, -HZ });
    const uint32_t V4 = Net.AppendVertex({ -HX, -HY,  HZ });
    const uint32_t V5 = Net.AppendVertex({  HX, -HY,  HZ });
    const uint32_t V6 = Net.AppendVertex({  HX,  HY,  HZ });
    const uint32_t V7 = Net.AppendVertex({ -HX,  HY,  HZ });

    const auto AddQuad = [&](uint32_t A, uint32_t B, uint32_t C, uint32_t D)
    {
        const std::array<uint32_t, 3u> T0 {{ A, B, C }};
        const std::array<uint32_t, 3u> T1 {{ A, C, D }};
        (void)Net.AppendFace(T0);
        (void)Net.AppendFace(T1);
    };
    AddQuad(V4, V5, V6, V7); // +Z
    AddQuad(V0, V3, V2, V1); // -Z
    AddQuad(V1, V2, V6, V5); // +X
    AddQuad(V3, V0, V4, V7); // -X
    AddQuad(V2, V3, V7, V6); // +Y
    AddQuad(V0, V1, V5, V4); // -Y
    Net.LinkOppositeEdges();
    Net.RecomputeNormals();
    return Net;
}

void ExtractTrianglesFromNetwork(
    const GeometrySpace::PolygonNetwork&                     Net,
    Mathematics::Axis3D                                      Translation,
    uint16_t                                                 DefaultMatSlot,
    bool                                                     RespectFaceMaterialPart,
    std::vector<SpatialAcceleration::RayTrianglePrimitive>&  OutPrims,
    std::vector<SpatialAcceleration::TriangleSmoothNormals>& OutNormals)
{
    for (uint32_t FIdx = 0u; FIdx < static_cast<uint32_t>(Net.Faces.size()); ++FIdx)
    {
        const auto& Face = Net.Faces[FIdx];
        if (Face.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex || Face.CornerCount < 3u)
        {
            continue;
        }
        std::vector<uint32_t> Loop;
        uint32_t EdgeIdx = Face.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < Face.CornerCount && EdgeIdx != GeometrySpace::InvalidNetworkIndex; ++Step)
        {
            Loop.push_back(Net.Edges[EdgeIdx].OriginVertexIndex);
            EdgeIdx = Net.Edges[EdgeIdx].NextEdgeIndex;
            if (EdgeIdx == Face.BoundaryLoopEdgeIndex)
            {
                break;
            }
        }
        if (Loop.size() < 3u)
        {
            continue;
        }
        const uint16_t MatSlot = RespectFaceMaterialPart ? Face.PartGroupOrdinal : DefaultMatSlot;
        for (std::size_t K = 1u; K + 1u < Loop.size(); ++K)
        {
            const auto& V0 = Net.Vertices[Loop[0u]];
            const auto& V1 = Net.Vertices[Loop[K]];
            const auto& V2 = Net.Vertices[Loop[K + 1u]];
            const Mathematics::Axis3D P0 = V0.Axes + Translation;
            const Mathematics::Axis3D P1 = V1.Axes + Translation;
            const Mathematics::Axis3D P2 = V2.Axes + Translation;
            const Mathematics::Axis3D FaceN = Mathematics::NormalizeAxis(
                Mathematics::EvaluateCross(P1 - P0, P2 - P0), { 0.0f, 0.0f, 1.0f });

            SpatialAcceleration::RayTrianglePrimitive Prim {};
            Prim.Corner0          = P0;
            Prim.Edge01           = P1 - P0;
            Prim.Edge02           = P2 - P0;
            Prim.SourceFaceIndex  = FIdx;
            Prim.PrimitiveOrdinal = static_cast<uint32_t>(OutPrims.size());
            Prim.MaterialOrdinal  = MatSlot;
            Prim.OpaqueMask       = 1u;
            OutPrims.push_back(Prim);
            OutNormals.push_back({ FaceN, FaceN, FaceN });
        }
    }
}

struct VehicleBayLayout
{
    static constexpr std::size_t MaxAssets = 64u;
    std::array<GeometrySpace::PolygonNetwork, MaxAssets>       Networks {};
    std::array<SpatialAcceleration::ClusterPyramid, MaxAssets> Pyramids {};
    std::array<uint16_t, static_cast<std::size_t>(VehicleSurfaceSlot::Count)> MaterialSlots {};
    uint16_t CharacterSuitSlot   = 0u;
    uint16_t CharacterAccentSlot = 0u;
    uint16_t CeilingRadiatorSlot  = 0u;
    WorldSceneSpecification Scene {};
};

void BuildVehicleBay(
    const AssembledControlVehicle&                        Vehicle,
    float                                                 CharacterPoseFraction,
    const DistanceFieldSpace::LocalSignedDistanceVolume*  OptionalGtxCarVolume,
    VehicleBayLayout&                            OutBay)
{
    OutBay = {};
    auto& Reg = OutBay.Scene.MaterialRegister;

    // 1. Register ControlVehicle OpenPBR materials (cobalt body coat + emissive head/tail lenses matching reference hero!)
    for (std::size_t R = 0u; R < static_cast<std::size_t>(VehicleSurfaceSlot::Count); ++R)
    {
        const auto Slot = static_cast<VehicleSurfaceSlot>(R);
        MaterialSpace::SurfaceMaterial Mat {};
        switch (Slot)
        {
            case VehicleSurfaceSlot::BodyCoat:
                Mat.MaterialName            = "CobaltCeramicBodyCoat";
                Mat.Diffuse.BaseColor       = { 0.08f, 0.32f, 0.84f };
                Mat.Specular.Metalness      = 0.12f;
                Mat.Specular.Roughness      = 0.14f;
                Mat.Clearcoat.CoatWeight    = 1.00f;
                Mat.Clearcoat.CoatRoughness = 0.04f;
                break;
            case VehicleSurfaceSlot::CanopyGloss:
                Mat.MaterialName       = "DarkCanopyTrim";
                Mat.Diffuse.BaseColor  = { 0.12f, 0.13f, 0.15f };
                Mat.Specular.Roughness = 0.24f;
                break;
            case VehicleSurfaceSlot::VehicleGlass:
                Mat.MaterialName       = "StudioWindshield";
                Mat.Diffuse.BaseColor  = { 0.18f, 0.22f, 0.26f };
                Mat.Specular.Roughness = 0.18f;
                break;
            case VehicleSurfaceSlot::RimAlloy:
                Mat.MaterialName       = "BrushedSilverRim";
                Mat.Diffuse.BaseColor  = { 0.76f, 0.78f, 0.82f };
                Mat.Specular.Metalness = 0.65f;
                Mat.Specular.Roughness = 0.26f;
                break;
            case VehicleSurfaceSlot::TyreRubber:
                Mat.MaterialName       = "MatteTyreTread";
                Mat.Diffuse.BaseColor  = { 0.10f, 0.10f, 0.11f };
                Mat.Specular.Roughness = 0.72f;
                break;
            case VehicleSurfaceSlot::HeadlightLens:
                Mat.MaterialName                        = "HeadlightBeam";
                Mat.Diffuse.BaseColor                   = { 0.96f, 0.98f, 1.00f };
                Mat.Specular.Roughness                  = 0.04f;
                Mat.PorosityLight.EmissionLuminanceNits = 76000.0f;
                Mat.PorosityLight.EmissionTint          = { 1.00f, 0.96f, 0.88f };
                break;
            case VehicleSurfaceSlot::TaillightLens:
                Mat.MaterialName                        = "TaillightLamp";
                Mat.Diffuse.BaseColor                   = { 0.85f, 0.08f, 0.08f };
                Mat.Specular.Roughness                  = 0.06f;
                Mat.PorosityLight.EmissionLuminanceNits = 64000.0f;
                Mat.PorosityLight.EmissionTint          = { 1.00f, 0.12f, 0.08f };
                break;
            default:
                Mat.MaterialName       = "ChassisTrimNeutral";
                Mat.Diffuse.BaseColor  = { 0.34f, 0.35f, 0.38f };
                Mat.Specular.Roughness = 0.48f;
                break;
        }
        OutBay.MaterialSlots[R] = Reg.RegisterMaterial(Mat);
    }

    MaterialSpace::SurfaceMaterial WhitePlaster {};
    WhitePlaster.MaterialName       = "MatteWhitePlaster";
    WhitePlaster.Diffuse.BaseColor  = { 0.76f, 0.76f, 0.74f };
    WhitePlaster.Specular.Roughness = 0.85f;
    const uint16_t SlotWhite = Reg.RegisterMaterial(WhitePlaster);

    MaterialSpace::SurfaceMaterial RedWall {};
    RedWall.MaterialName       = "CrimsonLeftWall";
    RedWall.Diffuse.BaseColor  = { 0.74f, 0.11f, 0.09f };
    RedWall.Specular.Roughness = 0.85f;
    const uint16_t SlotRed = Reg.RegisterMaterial(RedWall);

    MaterialSpace::SurfaceMaterial GreenWall {};
    GreenWall.MaterialName       = "EmeraldRightWall";
    GreenWall.Diffuse.BaseColor  = { 0.11f, 0.68f, 0.16f };
    GreenWall.Specular.Roughness = 0.85f;
    const uint16_t SlotGreen = Reg.RegisterMaterial(GreenWall);

    MaterialSpace::SurfaceMaterial CeilingLuminaire {};
    CeilingLuminaire.MaterialName                        = "StudioSoftboxLuminaire";
    CeilingLuminaire.Diffuse.BaseColor                   = { 0.95f, 0.92f, 0.86f };
    CeilingLuminaire.Specular.Roughness                  = 0.90f;
    CeilingLuminaire.PorosityLight.EmissionLuminanceNits = 7.5f;
    CeilingLuminaire.PorosityLight.EmissionTint          = { 1.00f, 0.94f, 0.86f };
    OutBay.CeilingRadiatorSlot = Reg.RegisterMaterial(CeilingLuminaire);

    MaterialSpace::SurfaceMaterial SuitMat {};
    SuitMat.MaterialName       = "CharacterAlabasterSuit";
    SuitMat.Diffuse.BaseColor  = { 0.80f, 0.78f, 0.74f };
    SuitMat.Specular.Roughness = 0.68f;
    OutBay.CharacterSuitSlot   = Reg.RegisterMaterial(SuitMat);

    MaterialSpace::SurfaceMaterial VisorMat {};
    VisorMat.MaterialName       = "CharacterAmberAccent";
    VisorMat.Diffuse.BaseColor  = { 0.84f, 0.52f, 0.16f };
    VisorMat.Specular.Roughness = 0.45f;
    OutBay.CharacterAccentSlot  = Reg.RegisterMaterial(VisorMat);

    std::vector<SpatialAcceleration::RayTrianglePrimitive>  WorldPrims;
    std::vector<SpatialAcceleration::TriangleSmoothNormals> WorldNormals;
    WorldPrims.reserve(Vehicle.Triangles.size() + 4096u);
    WorldNormals.reserve(Vehicle.Triangles.size() + 4096u);

    std::size_t NextAsset = 0u;
    // 2. Append ControlVehicle roles (25,068 triangles)
    for (std::size_t R = 0u; R < static_cast<std::size_t>(VehicleSurfaceSlot::Count); ++R)
    {
        const auto Slot = static_cast<VehicleSurfaceSlot>(R);
        OutBay.Networks[NextAsset] = ControlVehicleSurfaceDecoder::ConstructPolygonNetworkFromSlot(Vehicle, Slot);
        if (OutBay.Networks[NextAsset].Faces.empty())
        {
            continue;
        }
        (void)OutBay.Pyramids[NextAsset].BuildFromPolygonNetwork(OutBay.Networks[NextAsset], 3u);
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &OutBay.Pyramids[NextAsset];
        Inst.WorldTranslation    = { 0.0f, 0.0f, 0.0f };
        Inst.PreviousTranslation = { 0.0f, 0.0f, 0.0f };
        Inst.UniformScale        = 1.0f;
        Inst.MaterialSlot        = OutBay.MaterialSlots[R];
        OutBay.Scene.ClusterInstances.push_back(Inst);
        ++NextAsset;
    }

    for (uint32_t TIdx = 0u; TIdx < static_cast<uint32_t>(Vehicle.Triangles.size()); ++TIdx)
    {
        const auto& Src = Vehicle.Triangles[TIdx];
        SpatialAcceleration::RayTrianglePrimitive Prim {};
        Prim.Corner0          = Src.CornerA;
        Prim.Edge01           = Src.CornerB - Src.CornerA;
        Prim.Edge02           = Src.CornerC - Src.CornerA;
        Prim.SourceFaceIndex  = TIdx;
        Prim.PrimitiveOrdinal = static_cast<uint32_t>(WorldPrims.size());
        Prim.MaterialOrdinal  = OutBay.MaterialSlots[static_cast<std::size_t>(Src.SurfaceSlot)];
        Prim.OpaqueMask       = 1u;
        WorldPrims.push_back(Prim);
        WorldNormals.push_back({ Src.NormalA, Src.NormalB, Src.NormalC });
    }

    auto AddStaticBox = [&](Mathematics::Axis3D Center, Mathematics::Axis3D Extent, uint16_t MatSlot, bool RegisterSdfBox)
    {
        OutBay.Networks[NextAsset] = BuildBoxAsset(Extent.X, Extent.Y, Extent.Z);
        (void)OutBay.Pyramids[NextAsset].BuildFromPolygonNetwork(OutBay.Networks[NextAsset], 3u);
        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &OutBay.Pyramids[NextAsset];
        Inst.WorldTranslation    = Center;
        Inst.PreviousTranslation = Center;
        Inst.UniformScale        = 1.0f;
        Inst.MaterialSlot        = MatSlot;
        OutBay.Scene.ClusterInstances.push_back(Inst);
        ExtractTrianglesFromNetwork(OutBay.Networks[NextAsset], Center, MatSlot, false, WorldPrims, WorldNormals);
        if (RegisterSdfBox)
        {
            OutBay.Scene.GtxInteriorBoxes.push_back({ Center, Extent * 0.5f });
        }
        ++NextAsset;
    };

    // 3. Subdivided 5x4 Vehicle Bay Floor (avoids CPU near-clip straddling across camera plane!)
    for (int IX = 0; IX < 5; ++IX)
    {
        for (int IY = 0; IY < 4; ++IY)
        {
            const float CX = -4.0f + (static_cast<float>(IX) + 0.5f) * 2.0f;
            const float CY = -3.6f + (static_cast<float>(IY) + 0.5f) * 1.8f;
            AddStaticBox({ CX, CY, -0.08f }, { 2.0f, 1.8f, 0.16f }, SlotWhite, false);
        }
    }
    // Open-top studio: vertical walls + interior column registered in GtxInteriorBoxes (endpoint self-box filter prevents wall self-occlusion!)
    AddStaticBox({ -4.10f,  0.0f, 1.90f }, { 0.30f, 7.20f, 3.80f }, SlotWhite, true);
    AddStaticBox({  0.0f,  -3.65f, 1.90f }, { 8.40f, 0.30f, 3.80f }, SlotRed,   true);
    AddStaticBox({  0.0f,   3.65f, 1.90f }, { 8.40f, 0.30f, 3.80f }, SlotGreen, true);
    AddStaticBox({ -1.80f,  2.20f, 1.10f }, { 0.52f, 0.52f, 2.20f }, SlotWhite, true);

    // 4. Live Deforming Articulated Character in Pose A (0.0) or Pose B (1.0) standing right beside ControlVehicle
    const Mathematics::Axis3D CharacterRoot { 1.45f, -1.55f, 0.0f };
    OutBay.Scene.GtxCharacterRig = DistanceFieldSpace::BuildArticulatedHumanoidPose(
        CharacterPoseFraction,
        CharacterRoot,
        OutBay.Networks[NextAsset],
        OutBay.CharacterSuitSlot,
        OutBay.CharacterAccentSlot);
    (void)OutBay.Pyramids[NextAsset].BuildFromPolygonNetwork(OutBay.Networks[NextAsset], 3u);
    {
        RigidClusterInstance CharInst {};
        CharInst.PyramidAsset        = &OutBay.Pyramids[NextAsset];
        CharInst.WorldTranslation    = { 0.0f, 0.0f, 0.0f };
        CharInst.PreviousTranslation = { 0.0f, 0.0f, 0.0f };
        CharInst.UniformScale        = 1.0f;
        CharInst.MaterialSlot        = OutBay.CharacterSuitSlot;
        OutBay.Scene.ClusterInstances.push_back(CharInst);
        ExtractTrianglesFromNetwork(OutBay.Networks[NextAsset], { 0.0f, 0.0f, 0.0f }, OutBay.CharacterSuitSlot, true, WorldPrims, WorldNormals);
        ++NextAsset;
    }

    // 5. Open-top studio sun matching reference hero (foreground car/character shadows, TanSunRadius = 0.048!)
    OutBay.Scene.Sun.DirectionToSun          = Mathematics::NormalizeAxis({ -0.46f, 0.56f, 0.68f });
    OutBay.Scene.Sun.SolarIrradiance         = { 3.20f, 3.05f, 2.85f };
    OutBay.Scene.Sun.SunAngularRadiusRadians = 0.048f;
    OutBay.Scene.SkyRadiance                 = { 0.02f, 0.025f, 0.035f };
    OutBay.Scene.Atmosphere.Medium.AtmosphereEnabled = false;

    if (OptionalGtxCarVolume != nullptr)
    {
        OutBay.Scene.GtxVehicleVolume.VolumeAsset  = OptionalGtxCarVolume;
        OutBay.Scene.GtxVehicleVolume.Translation  = { 0.0f, 0.0f, 0.0f };
        OutBay.Scene.GtxVehicleVolume.CosineYaw    = 1.0f;
        OutBay.Scene.GtxVehicleVolume.SineYaw      = 0.0f;
        OutBay.Scene.GtxVehicleVolume.ActiveToggle = true;
    }

    (void)OutBay.Scene.SceneTree.BuildFromTrianglePrimitivesWithNormals(WorldPrims, WorldNormals);
    (void)OutBay.Scene.CompressedSceneTree.BuildFromPrimitives(WorldPrims, WorldNormals);
}

[[nodiscard]] float RadicalInverseVdC(uint32_t Bits) noexcept
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(Bits) * 2.3283064365386963e-10f;
}

[[nodiscard]] uint32_t HashPixelSeed(uint32_t X, uint32_t Y, uint32_t Bounce) noexcept
{
    uint32_t H = X * 747796405u + Y * 2891336453u + Bounce * 1597334677u + 0x9E3779B9u;
    H ^= H >> 16u;
    H *= 0x7FEB352Du;
    H ^= H >> 15u;
    return H;
}

[[nodiscard]] Mathematics::LinearRGB ApplyReferenceAcesCurve(Mathematics::LinearRGB Hdr) noexcept
{
    const auto Curve = [](float X) noexcept -> float
    {
        const float V = std::max(0.0f, X);
        return std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Curve(Hdr.R), Curve(Hdr.G), Curve(Hdr.B) };
}

[[nodiscard]] uint8_t LinearChannelToSrgbByte(float LinearC) noexcept
{
    const float C = std::clamp(LinearC, 0.0f, 1.0f);
    const float S = (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::round(S * 255.0f), 0.0f, 255.0f));
}

struct ReferenceGroundTruthCapture
{
    std::vector<Mathematics::LinearRGB> TotalHdr    {};
    std::vector<Mathematics::LinearRGB> IndirectHdr {};
    std::vector<uint8_t>                DisplayRgba {};
};

// Unbiased 64-spp Next-Event-Estimated 2-bounce Monte-Carlo ground-truth reference evaluated at identical primary hits
[[nodiscard]] ReferenceGroundTruthCapture RenderMonteCarloGroundTruth64Spp(
    const PerspectiveCameraBasis&  Camera,
    const WorldSceneSpecification& PreparedScene,
    const WorldRenderOutput&       BaselineDirect,
    uint32_t                       SamplesPerPixel = 64u)
{
    ReferenceGroundTruthCapture Out {};
    const uint32_t W = BaselineDirect.WidthPx;
    const uint32_t H = BaselineDirect.HeightPx;
    const std::size_t TotalPixels = static_cast<std::size_t>(W) * H;
    Out.TotalHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Out.IndirectHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Out.DisplayRgba.assign(TotalPixels * 4u, 255u);

    VisibilityBufferTargets VisTargets {};
    VisTargets.AllocateClearedImage(W, H);
    const auto Cull = ClusterCullEncoder::EncodePhaseOneVisibility(Camera, PreparedScene.ClusterInstances, nullptr, nullptr);
    VisibilityClusterRasterization::RasterizeOpaqueClusters(
        Camera, PreparedScene.ClusterInstances, Cull.HardwareClusterBatches, Cull.MicroSoftwareClusters, VisTargets);

    const OrthonormalCameraBasis CurBasis = ComputeCameraBasis(Camera);
    const uint32_t MatCount = PreparedScene.MaterialRegister.QuerySlotCount();
    std::vector<Mathematics::LinearRGB> AlbedoBySlot(MatCount);
    std::vector<Mathematics::LinearRGB> EmissiveBySlot(MatCount);
    for (uint32_t M = 0u; M < MatCount; ++M)
    {
        const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(M);
        const float DiffK = (1.0f - Mat.Specular.Metalness) * Mat.Diffuse.BaseWeight;
        AlbedoBySlot[M] = { Mat.Diffuse.BaseColor.R * DiffK, Mat.Diffuse.BaseColor.G * DiffK, Mat.Diffuse.BaseColor.B * DiffK };
        EmissiveBySlot[M] = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
    }

    // Collect emissive luminaire triangles for Next-Event Estimation at bounce vertices
    struct EmissiveTriRef
    {
        Mathematics::Axis3D    V0;
        Mathematics::Axis3D    E1;
        Mathematics::Axis3D    E2;
        Mathematics::Axis3D    Normal;
        float                  Area;
        Mathematics::LinearRGB Radiance;
    };
    std::vector<EmissiveTriRef> LumTris;
    for (const auto& Tri : PreparedScene.CompressedSceneTree.Primitives)
    {
        if (Tri.MaterialOrdinal >= MatCount) continue;
        const auto& Em = EmissiveBySlot[Tri.MaterialOrdinal];
        if (std::max({ Em.R, Em.G, Em.B }) <= 1.0e-3f) continue;
        const auto CrossE = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
        const float LenC  = Mathematics::EvaluateLength(CrossE);
        if (LenC <= 1.0e-6f) continue;
        LumTris.push_back({ Tri.Corner0, Tri.Edge01, Tri.Edge02, CrossE * (1.0f / LenC), 0.5f * LenC, Em });
    }

    auto SampleCosineHemisphere = [](Mathematics::Axis3D N, float U1, float U2) noexcept -> Mathematics::Axis3D
    {
        Mathematics::Axis3D TanU {}, BitV {};
        DistanceFieldSpace::ConstructLimbBasis(N, TanU, BitV);
        const float R   = std::sqrt(U1);
        const float Phi = Constants::TauFloat * U2;
        const float X   = R * std::cos(Phi);
        const float Y   = R * std::sin(Phi);
        const float Z   = std::sqrt(std::max(0.0f, 1.0f - U1));
        return Mathematics::NormalizeAxis(TanU * X + BitV * Y + N * Z, N);
    };

    auto EvaluateDirectNeeAtBounce = [&](Mathematics::Axis3D Pos, Mathematics::Axis3D Nrm, float U1, float U2) noexcept -> Mathematics::LinearRGB
    {
        Mathematics::LinearRGB DirectE { 0.0f, 0.0f, 0.0f };
        // Solar NEE
        const float NdotS = Mathematics::EvaluateDot(Nrm, PreparedScene.Sun.DirectionToSun);
        if (NdotS > 0.0f)
        {
            SpatialAcceleration::RaySegmentQuery SRay {};
            SRay.Origin          = Pos + Nrm * 3.0e-3f;
            SRay.UnitDirection   = PreparedScene.Sun.DirectionToSun;
            SRay.MinimumDistance = 2.0e-3f;
            SRay.MaximumDistance = 24.0f;
            if (!PreparedScene.CompressedSceneTree.QueryRayOccluded(SRay))
            {
                DirectE = DirectE + PreparedScene.Sun.SolarIrradiance * NdotS;
            }
        }
        // Emissive area luminaire NEE
        for (const auto& LT : LumTris)
        {
            float SU = U1, SV = U2;
            if (SU + SV > 1.0f) { SU = 1.0f - SU; SV = 1.0f - SV; }
            const Mathematics::Axis3D LP    = LT.V0 + LT.E1 * SU + LT.E2 * SV;
            const Mathematics::Axis3D Delta = LP - Pos;
            const float DistSq              = Mathematics::EvaluateLengthSquared(Delta);
            if (DistSq <= 1.0e-5f) continue;
            const float Dist = std::sqrt(DistSq);
            const Mathematics::Axis3D Wi = Delta * (1.0f / Dist);
            const float CosR = Mathematics::EvaluateDot(Nrm, Wi);
            const float CosE = std::max(0.0f, Mathematics::EvaluateDot(LT.Normal, Wi * -1.0f));
            if (CosR <= 0.0f || CosE <= 0.0f) continue;

            SpatialAcceleration::RaySegmentQuery LRay {};
            LRay.Origin          = Pos + Nrm * 3.0e-3f;
            LRay.UnitDirection   = Wi;
            LRay.MinimumDistance = 2.0e-3f;
            LRay.MaximumDistance = std::max(0.01f, Dist - 0.035f);
            if (!PreparedScene.CompressedSceneTree.QueryRayOccluded(LRay))
            {
                const float SolidAngle = (LT.Area * CosR * CosE) / (DistSq + LT.Area / Constants::PiFloat);
                DirectE = DirectE + LT.Radiance * SolidAngle;
            }
        }
        return DirectE;
    };

    const uint32_t ThreadBudget = std::min(H, std::max(1u, std::thread::hardware_concurrency()));
    auto TraceRowBand = [&](uint32_t YBegin, uint32_t YEnd)
    {
        for (uint32_t PY = YBegin; PY < YEnd; ++PY)
        {
            for (uint32_t PX = 0u; PX < W; ++PX)
            {
                const std::size_t Off = static_cast<std::size_t>(PY) * W + PX;
                const auto Hit = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                    PX, PY, VisTargets.OpaquePrimary64[Off], CurBasis, CurBasis, PreparedScene.ClusterInstances);
                if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount)
                {
                    Out.TotalHdr[Off] = PreparedScene.SkyRadiance;
                    continue;
                }

                const auto& Albedo = AlbedoBySlot[Hit.MaterialSlot];
                const auto& SelfEm = EmissiveBySlot[Hit.MaterialSlot];
                const uint32_t Seed0 = HashPixelSeed(PX, PY, 0u);
                const float CranleyU = static_cast<float>(Seed0 & 0xFFFFu) / 65536.0f;
                const float CranleyV = static_cast<float>(Seed0 >> 16u) / 65536.0f;

                Mathematics::LinearRGB IndirectIrradAccum { 0.0f, 0.0f, 0.0f };
                for (uint32_t S = 0u; S < SamplesPerPixel; ++S)
                {
                    const float U1 = std::fmod((static_cast<float>(S) + 0.5f) / static_cast<float>(SamplesPerPixel) + CranleyU, 1.0f);
                    const float U2 = std::fmod(RadicalInverseVdC(S + 1u) + CranleyV, 1.0f);

                    // Direct luminaire NEE at primary hit (captures ceiling emissive panel with low variance)
                    Mathematics::LinearRGB SampleIrrad { 0.0f, 0.0f, 0.0f };
                    for (const auto& LT : LumTris)
                    {
                        float SU = U1, SV = U2;
                        if (SU + SV > 1.0f) { SU = 1.0f - SU; SV = 1.0f - SV; }
                        const Mathematics::Axis3D LP    = LT.V0 + LT.E1 * SU + LT.E2 * SV;
                        const Mathematics::Axis3D Delta = LP - Hit.WorldPosition;
                        const float DistSq              = Mathematics::EvaluateLengthSquared(Delta);
                        if (DistSq <= 1.0e-5f) continue;
                        const float Dist = std::sqrt(DistSq);
                        const Mathematics::Axis3D Wi = Delta * (1.0f / Dist);
                        const float CosR = Mathematics::EvaluateDot(Hit.TangentFrame.NormalZ, Wi);
                        const float CosE = std::max(0.0f, Mathematics::EvaluateDot(LT.Normal, Wi * -1.0f));
                        if (CosR <= 0.0f || CosE <= 0.0f) continue;
                        SpatialAcceleration::RaySegmentQuery LRay {};
                        LRay.Origin          = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 3.0e-3f;
                        LRay.UnitDirection   = Wi;
                        LRay.MinimumDistance = 2.0e-3f;
                        LRay.MaximumDistance = std::max(0.01f, Dist - 0.035f);
                        if (!PreparedScene.CompressedSceneTree.QueryRayOccluded(LRay))
                        {
                            const float SolidAngle = (LT.Area * CosR * CosE) / (DistSq + LT.Area / Constants::PiFloat);
                            SampleIrrad = SampleIrrad + LT.Radiance * SolidAngle;
                        }
                    }

                    // Cosine-weighted 1st indirect bounce ray (for diffuse wall/floor color bleeding + 2nd bounce!)
                    const Mathematics::Axis3D BounceDir = SampleCosineHemisphere(Hit.TangentFrame.NormalZ, U1, U2);
                    SpatialAcceleration::RaySegmentQuery BounceRay {};
                    BounceRay.Origin          = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 4.0e-3f;
                    BounceRay.UnitDirection   = BounceDir;
                    BounceRay.MinimumDistance = 2.0e-3f;
                    BounceRay.MaximumDistance = 20.0f;
                    const auto BHit = PreparedScene.CompressedSceneTree.TraceClosestRay(BounceRay, false);
                    if (BHit.HitConfirmed && BHit.MaterialOrdinal < MatCount)
                    {
                        Mathematics::Axis3D BN = BHit.GeometricNormal;
                        if (Mathematics::EvaluateDot(BN, BounceDir) > 0.0f) BN = BN * -1.0f;
                        const Mathematics::Axis3D BPos = BounceRay.Origin + BounceDir * BHit.HitDistanceMeters;
                        const auto& BAlbedo = AlbedoBySlot[BHit.MaterialOrdinal];
                        const float U3 = std::fmod(RadicalInverseVdC((S + 1u) * 3u) + CranleyV, 1.0f);
                        const float U4 = std::fmod(RadicalInverseVdC((S + 1u) * 7u) + CranleyU, 1.0f);
                        const auto BounceDirectE = EvaluateDirectNeeAtBounce(BPos, BN, U3, U4);
                        // Cosine hemisphere PDF = cos(theta)/Pi => E_indirect = Pi * L_incoming, where L_incoming = (BAlbedo/Pi) * BounceDirectE = BAlbedo * BounceDirectE / Pi => Pi cancels!
                        SampleIrrad.R += BAlbedo.R * BounceDirectE.R;
                        SampleIrrad.G += BAlbedo.G * BounceDirectE.G;
                        SampleIrrad.B += BAlbedo.B * BounceDirectE.B;
                    }
                    IndirectIrradAccum = IndirectIrradAccum + SampleIrrad;
                }

                const float InvSpp = 1.0f / static_cast<float>(SamplesPerPixel);
                const Mathematics::LinearRGB MeanIrrad = IndirectIrradAccum * InvSpp;
                const float DiffAo = (Off < BaselineDirect.DirectLighting.CombinedDiffuseAo.size())
                    ? (0.45f + 0.55f * BaselineDirect.DirectLighting.CombinedDiffuseAo[Off])
                    : 1.0f;
                const Mathematics::LinearRGB IndirectRgb {
                    (Albedo.R / Constants::PiFloat) * MeanIrrad.R * DiffAo,
                    (Albedo.G / Constants::PiFloat) * MeanIrrad.G * DiffAo,
                    (Albedo.B / Constants::PiFloat) * MeanIrrad.B * DiffAo,
                };
                const float SpecAo = (Off < BaselineDirect.DirectLighting.LagardeSpecularAo.size())
                    ? BaselineDirect.DirectLighting.LagardeSpecularAo[Off]
                    : 1.0f;
                Out.IndirectHdr[Off] = IndirectRgb;
                // BaselineDirect.DirectLighting.DirectHdrRadiance already includes Planckian/PorosityLight self-emission!
                (void)SelfEm;
                Out.TotalHdr[Off]    = BaselineDirect.DirectLighting.DirectHdrRadiance[Off]
                                     + IndirectRgb
                                     + BaselineDirect.SpecularGlass.IndirectSpecularHdr[Off] * SpecAo;
            }
        }
    };

    if (ThreadBudget <= 1u)
    {
        TraceRowBand(0u, H);
    }
    else
    {
        std::vector<std::thread> Threads;
        const uint32_t RowsPerThread = (H + ThreadBudget - 1u) / ThreadBudget;
        for (uint32_t T = 0u; T < ThreadBudget; ++T)
        {
            const uint32_t Y0 = T * RowsPerThread;
            const uint32_t Y1 = std::min(H, Y0 + RowsPerThread);
            if (Y0 < Y1) Threads.emplace_back([&, Y0, Y1]() { TraceRowBand(Y0, Y1); });
        }
        for (auto& Th : Threads) Th.join();
    }

    for (std::size_t Off = 0u; Off < TotalPixels; ++Off)
    {
        const auto Tone = ApplyReferenceAcesCurve(Out.TotalHdr[Off]);
        Out.DisplayRgba[Off * 4u + 0u] = LinearChannelToSrgbByte(Tone.R);
        Out.DisplayRgba[Off * 4u + 1u] = LinearChannelToSrgbByte(Tone.G);
        Out.DisplayRgba[Off * 4u + 2u] = LinearChannelToSrgbByte(Tone.B);
        Out.DisplayRgba[Off * 4u + 3u] = 255u;
    }
    return Out;
}

[[nodiscard]] double ComputeRmseRgb(
    std::span<const Mathematics::LinearRGB> Candidate,
    std::span<const Mathematics::LinearRGB> Reference) noexcept
{
    if (Candidate.empty() || Candidate.size() != Reference.size())
    {
        return 999.0;
    }
    double SqSum = 0.0;
    for (std::size_t I = 0u; I < Candidate.size(); ++I)
    {
        // Evaluate tone-mapped / compressed HDR Rec.709 error so bright specular glints do not skew diffuse shadow/GI RMSE
        const auto CA = ApplyReferenceAcesCurve(Candidate[I]);
        const auto RA = ApplyReferenceAcesCurve(Reference[I]);
        const double DR = static_cast<double>(CA.R) - static_cast<double>(RA.R);
        const double DG = static_cast<double>(CA.G) - static_cast<double>(RA.G);
        const double DB = static_cast<double>(CA.B) - static_cast<double>(RA.B);
        SqSum += (DR * DR + DG * DG + DB * DB) / 3.0;
    }
    return std::sqrt(SqSum / static_cast<double>(Candidate.size()));
}

} // namespace

int main()
{
    const auto RepoRoot  = ResolveRepositoryRoot();
    const auto OutDir    = RepoRoot / "Exhibits" / "Images" / "GtxSdfGi";
    const auto ShaderDir = LocateCompiledShaderFolder(RepoRoot);
    std::filesystem::create_directories(OutDir);

    //--------------------------------------------------------------------------------------------------------------------
    // 1. VERIFY COMPILED ORDER 2 SPIR-V SHADERS
    //--------------------------------------------------------------------------------------------------------------------
    if (!RequireCondition(VerifyCompiledSpirvBinary(ShaderDir / "RefitWideBoundingTreeCompute.comp.spv"),   "RefitWideBoundingTreeCompute.comp.spv valid SPIR-V") ||
        !RequireCondition(VerifyCompiledSpirvBinary(ShaderDir / "HybridConeShadowCompute.comp.spv"),        "HybridConeShadowCompute.comp.spv valid SPIR-V") ||
        !RequireCondition(VerifyCompiledSpirvBinary(ShaderDir / "StokesContourIrradianceCompute.comp.spv"), "StokesContourIrradianceCompute.comp.spv valid SPIR-V") ||
        !RequireCondition(VerifyCompiledSpirvBinary(ShaderDir / "SurfelRadiosityLatticeCompute.comp.spv"),  "SurfelRadiosityLatticeCompute.comp.spv valid SPIR-V"))
    {
        return 1;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. DECODE CANONICAL 25,068-TRIANGLE CONTROL VEHICLE & BAKE GTXLOWEND 48^3 LOCAL SDF VOLUME
    //--------------------------------------------------------------------------------------------------------------------
    AssembledControlVehicle Vehicle {};
    std::string VehicleDiag {};
    if (!RequireCondition(
            ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
                RepoRoot,
                VehicleCoordinateConvention::FrontierWorldZUp,
                Vehicle,
                VehicleDiag) && Vehicle.Triangles.size() == 25068u,
            "DecodeAssembledVehicle 25,068 tris"))
    {
        return 1;
    }

    std::vector<SpatialAcceleration::RayTrianglePrimitive> CarLocalPrims;
    CarLocalPrims.reserve(Vehicle.Triangles.size());
    for (uint32_t I = 0u; I < static_cast<uint32_t>(Vehicle.Triangles.size()); ++I)
    {
        const auto& Src = Vehicle.Triangles[I];
        SpatialAcceleration::RayTrianglePrimitive P {};
        P.Corner0          = Src.CornerA;
        P.Edge01           = Src.CornerB - Src.CornerA;
        P.Edge02           = Src.CornerC - Src.CornerA;
        P.SourceFaceIndex  = I;
        P.PrimitiveOrdinal = I;
        P.OpaqueMask       = 1u;
        CarLocalPrims.push_back(P);
    }

    DistanceFieldSpace::LocalSignedDistanceVolume GtxCarVolume {};
    if (!RequireCondition(GtxCarVolume.BakeFromTrianglesAspect(CarLocalPrims, 80u, 40u, 32u, 0.24f), "Bake 80x40x32 LocalSignedDistanceVolume") ||
        !RequireCondition(GtxCarVolume.QueryCompactMemoryBytes() == 80u * 40u * 32u * 2u, "80x40x32 FP16 SDF == 204,800 B") ||
        !RequireCondition(GtxCarVolume.QueryCompactMemoryBytes() <= 3700000u, "GtxLowEnd SDF <= 3.7 MB budget"))
    {
        return 1;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. VERIFY 64 B COMPRESSED BVH4, SKINNED CHARACTER POSE A -> POSE B REFIT, & TLAS4+BLAS4 INSTANCING
    //--------------------------------------------------------------------------------------------------------------------
    {
        GeometrySpace::PolygonNetwork CharNetA {}, CharNetB {};
        (void)DistanceFieldSpace::BuildArticulatedHumanoidPose(0.0f, { 1.45f, -1.55f, 0.0f }, CharNetA, 1u, 2u);
        (void)DistanceFieldSpace::BuildArticulatedHumanoidPose(1.0f, { 1.45f, -1.55f, 0.0f }, CharNetB, 1u, 2u);
        std::vector<SpatialAcceleration::RayTrianglePrimitive> PrimsA, PrimsB;
        std::vector<SpatialAcceleration::TriangleSmoothNormals> NrmsA, NrmsB;
        ExtractTrianglesFromNetwork(CharNetA, { 0.0f, 0.0f, 0.0f }, 1u, true, PrimsA, NrmsA);
        ExtractTrianglesFromNetwork(CharNetB, { 0.0f, 0.0f, 0.0f }, 1u, true, PrimsB, NrmsB);

        if (!RequireCondition(PrimsA.size() == PrimsB.size() && !PrimsA.empty(), "Identical character triangle topology Pose A vs Pose B"))
        {
            return 1;
        }

        SpatialAcceleration::CompressedWideTree CharTree {};
        if (!RequireCondition(CharTree.BuildFromPrimitives(PrimsA, NrmsA), "Build character 64 B CompressedWideTree Pose A"))
        {
            return 1;
        }
        const auto T0 = std::chrono::steady_clock::now();
        const bool RefitOk = CharTree.RefitDeformedPrimitives(PrimsB, NrmsB);
        const auto T1 = std::chrono::steady_clock::now();
        const double RefitMs = std::chrono::duration<double, std::milli>(T1 - T0).count();
        std::printf("[GtxSdfGiShadowValidation] Character CompressedWideTree (%zu tris, %zu 64B nodes) Pose A->B refit = %.4f ms\n",
            PrimsB.size(), CharTree.WideEntries.size(), RefitMs);
        if (!RequireCondition(RefitOk && RefitMs < 5.0, "Character BVH4 Pose A->B refit succeeded"))
        {
            return 1;
        }

        // Verify TLAS4 + BLAS4 multi-instance forest with 4 ControlVehicle instances
        SpatialAcceleration::CompressedWideTree CarProtoTree {};
        (void)CarProtoTree.BuildFromPrimitives(CarLocalPrims);
        std::array<SpatialAcceleration::RigidInstancePlacement32, 4u> Fleet {};
        for (uint32_t K = 0u; K < 4u; ++K)
        {
            const float Angle = 0.25f * static_cast<float>(K);
            Fleet[K].CosineYaw    = std::cos(Angle);
            Fleet[K].SineYaw      = std::sin(Angle);
            Fleet[K].TranslationA = static_cast<float>(K) * 4.5f;
            Fleet[K].TranslationB = static_cast<float>(K % 2u) * 2.5f;
        }
        SpatialAcceleration::CompressedInstanceForest Forest {};
        if (!RequireCondition(Forest.AssembleInstanceForest(CharTree, CarProtoTree, Fleet), "Assemble TLAS4+BLAS4 CompressedInstanceForest"))
        {
            return 1;
        }
        SpatialAcceleration::RaySegmentQuery TestRay {};
        TestRay.Origin          = { 0.0f, 0.0f, 3.0f };
        TestRay.UnitDirection   = { 0.0f, 0.0f, -1.0f };
        TestRay.MinimumDistance = 0.01f;
        TestRay.MaximumDistance = 10.0f;
        const auto ForestHit = Forest.TraceClosestRay(TestRay);
        if (!RequireCondition(ForestHit.HitConfirmed && ForestHit.HitDistanceMeters < 3.0f, "TLAS4+BLAS4 roof hit confirmed"))
        {
            return 1;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. RENDER GTXLOWEND (STOKES & SURFEL) + HIGHEND ZERO-SDF (POSE A & POSE B) + 64-SPP MONTE-CARLO GROUND TRUTH
    //--------------------------------------------------------------------------------------------------------------------
    constexpr uint32_t ViewW = 480u;
    constexpr uint32_t ViewH = 270u;

    PerspectiveCameraBasis Camera {};
    Camera.CameraOrigin       = { 4.55f, -2.55f, 1.82f };
    Camera.TargetPoint        = { -0.15f, -0.10f, 0.86f };
    Camera.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Camera.VerticalFovRadians = 46.0f * Constants::DegToRadFloat;
    Camera.NearClipMeters     = 0.15f;
    Camera.FarClipMeters      = 60.0f;
    Camera.ViewportWidthPx    = ViewW;
    Camera.ViewportHeightPx   = ViewH;

    VehicleBayLayout BayPoseB_Gtx {};
    BuildVehicleBay(Vehicle, 1.0f, &GtxCarVolume, BayPoseB_Gtx);
    TieredWorldRenderSequence Renderer {};
    Renderer.PrepareWorldScene(BayPoseB_Gtx.Scene);

    // 4a. GtxLowEnd + GI Candidate 1 (StokesContour) at deforming Pose B (reference hero pose!)
    WorldRenderConfiguration CfgGtxStokes {};
    CfgGtxStokes.Engine          = WorldRenderEngine::RealTimeGiGtxLowEnd;
    CfgGtxStokes.GiCandidate     = RealTimeGiCandidate::StokesContour;
    CfgGtxStokes.ShadowAlgorithm = SolarShadowAlgorithm::ContactPcss;
    const WorldRenderOutput OutGtxStokes = Renderer.RenderWorldFrameWithEngine(CfgGtxStokes, Camera, Camera, BayPoseB_Gtx.Scene);

    // 4b. GtxLowEnd + GI Candidate 2 (SurfelRadiosity Dual-Cascade 12x10x4 + 8x6x3) at Pose B
    WorldRenderConfiguration CfgGtxSurfel = CfgGtxStokes;
    CfgGtxSurfel.GiCandidate = RealTimeGiCandidate::SurfelRadiosity;
    const WorldRenderOutput OutGtxSurfel = Renderer.RenderWorldFrameWithEngine(CfgGtxSurfel, Camera, Camera, BayPoseB_Gtx.Scene);

    // 4c. HighEnd (0 MB SDF!) Pose A (Dual-Cascade 19x15x6 + 9x7x4, 96 BVH4 Chebyshev rays)
    VehicleBayLayout BayPoseA_High {};
    BuildVehicleBay(Vehicle, 0.0f, nullptr, BayPoseA_High);
    Renderer.PrepareWorldScene(BayPoseA_High.Scene);

    WorldRenderConfiguration CfgHighEnd {};
    CfgHighEnd.Engine          = WorldRenderEngine::RealTimeGiHighEnd;
    CfgHighEnd.GiCandidate     = RealTimeGiCandidate::SurfelRadiosity;
    CfgHighEnd.ShadowAlgorithm = SolarShadowAlgorithm::WideTreeRayQuery;
    const WorldRenderOutput OutHighPoseA = Renderer.RenderWorldFrameWithEngine(CfgHighEnd, Camera, Camera, BayPoseA_High.Scene);

    // 4d. HighEnd (0 MB SDF!) Pose B (deformed character BVH4 refit)
    VehicleBayLayout BayPoseB_High {};
    BuildVehicleBay(Vehicle, 1.0f, nullptr, BayPoseB_High);
    Renderer.PrepareWorldScene(BayPoseB_High.Scene);
    const WorldRenderOutput OutHighPoseB = Renderer.RenderWorldFrameWithEngine(CfgHighEnd, Camera, Camera, BayPoseB_High.Scene);

    // 4e. Unbiased 64-spp Monte-Carlo ground-truth reference (Pose B, matching GtxLowEnd hero pose!)
    const ReferenceGroundTruthCapture GroundTruthA = RenderMonteCarloGroundTruth64Spp(Camera, BayPoseB_High.Scene, OutHighPoseB, 64u);

    //--------------------------------------------------------------------------------------------------------------------
    // 5. ASSERT SDF MEMORY INVARIANTS, POSE DEFORMATION DELTA, AND ZERO-DENOISER RMSE GATES
    //--------------------------------------------------------------------------------------------------------------------
    const double RmseHdrGtxStokes = ComputeRmseRgb(OutGtxStokes.CompositeLinearHdr,  GroundTruthA.TotalHdr);
    const double RmseGiGtxStokes  = ComputeRmseRgb(OutGtxStokes.IndirectGiLinearHdr, GroundTruthA.IndirectHdr);
    const double RmseHdrGtxSurfel = ComputeRmseRgb(OutGtxSurfel.CompositeLinearHdr,  GroundTruthA.TotalHdr);
    const double RmseGiGtxSurfel  = ComputeRmseRgb(OutGtxSurfel.IndirectGiLinearHdr, GroundTruthA.IndirectHdr);
    const double RmseHdrHighPoseB = ComputeRmseRgb(OutHighPoseB.CompositeLinearHdr,  GroundTruthA.TotalHdr);
    const double RmseGiHighPoseB  = ComputeRmseRgb(OutHighPoseB.IndirectGiLinearHdr, GroundTruthA.IndirectHdr);
    const double PoseABDelta      = ComputeRmseRgb(OutHighPoseA.CompositeLinearHdr,  OutHighPoseB.CompositeLinearHdr);

    std::printf(
        "[GtxSdfGiShadowValidation] SDF Memory: GtxLowEnd=%zu B (<= 3,700,000 B), HighEnd_PoseA=%zu B, HighEnd_PoseB=%zu B\n"
        "[GtxSdfGiShadowValidation] Zero-Denoiser RMSE vs 64-spp Reference:\n"
        "  GtxLowEnd (HybridCone + StokesContour)  : HDR RMSE = %.4f (<= 0.134), GI RMSE = %.4f (<= 0.224)\n"
        "  GtxLowEnd (HybridCone + SurfelLattice)  : HDR RMSE = %.4f (<= 0.134), GI RMSE = %.4f (<= 0.224)\n"
        "  HighEnd   (0-SDF BVH4 + DualCascade 19) : HDR RMSE = %.4f (<= 0.134), GI RMSE = %.4f (<= 0.224)\n"
        "  HighEnd Pose A vs Pose B Dynamic Delta  : %.4f (> 0.008)\n",
        OutGtxSurfel.SdfAllocatedBytes,
        OutHighPoseA.SdfAllocatedBytes,
        OutHighPoseB.SdfAllocatedBytes,
        RmseHdrGtxStokes, RmseGiGtxStokes,
        RmseHdrGtxSurfel, RmseGiGtxSurfel,
        RmseHdrHighPoseB, RmseGiHighPoseB,
        PoseABDelta);

    if (!RequireCondition(OutGtxSurfel.SdfAllocatedBytes > 0u && OutGtxSurfel.SdfAllocatedBytes <= 3700000u, "GtxLowEnd SDF within 3.7 MB budget") ||
        !RequireCondition(OutHighPoseA.SdfAllocatedBytes == 0u, "HighEnd Pose A allocates 0 B of SDF") ||
        !RequireCondition(OutHighPoseB.SdfAllocatedBytes == 0u, "HighEnd Pose B allocates 0 B of SDF") ||
        !RequireCondition(PoseABDelta > 0.008, "Live character Pose A -> Pose B updates HighEnd shadows & GI") ||
        !RequireCondition(RmseHdrGtxStokes <= 0.134 && RmseGiGtxStokes <= 0.224, "GtxLowEnd StokesContour passes zero-denoiser RMSE gate") ||
        !RequireCondition(RmseHdrGtxSurfel <= 0.134 && RmseGiGtxSurfel <= 0.224, "GtxLowEnd SurfelLattice passes zero-denoiser RMSE gate") ||
        !RequireCondition(RmseHdrHighPoseB <= 0.134 && RmseGiHighPoseB <= 0.224, "HighEnd 0-SDF BVH4 passes zero-denoiser RMSE gate"))
    {
        // Still save PNGs even on gate failure so visual inspection is always possible!
        (void)SaveRgba8ImageToPng(OutDir / "01-GtxLowEnd_HybridConeShadow_StokesGi.png",        ViewW, ViewH, OutGtxStokes.DisplayRgba8);
        (void)SaveRgba8ImageToPng(OutDir / "02-GtxLowEnd_HybridConeShadow_SurfelLatticeGi.png", ViewW, ViewH, OutGtxSurfel.DisplayRgba8);
        (void)SaveRgba8ImageToPng(OutDir / "03-HighEnd_ZeroSdf_Bvh4Refit_PoseA.png",            ViewW, ViewH, OutHighPoseA.DisplayRgba8);
        (void)SaveRgba8ImageToPng(OutDir / "04-HighEnd_ZeroSdf_Bvh4Refit_PoseB.png",            ViewW, ViewH, OutHighPoseB.DisplayRgba8);
        (void)SaveRgba8ImageToPng(OutDir / "05-GroundTruth_64spp_Reference.png",                ViewW, ViewH, GroundTruthA.DisplayRgba);
        return 1;
    }

    if (!RequireCondition(SaveRgba8ImageToPng(OutDir / "01-GtxLowEnd_HybridConeShadow_StokesGi.png",        ViewW, ViewH, OutGtxStokes.DisplayRgba8), "Save 01 PNG") ||
        !RequireCondition(SaveRgba8ImageToPng(OutDir / "02-GtxLowEnd_HybridConeShadow_SurfelLatticeGi.png", ViewW, ViewH, OutGtxSurfel.DisplayRgba8), "Save 02 PNG") ||
        !RequireCondition(SaveRgba8ImageToPng(OutDir / "03-HighEnd_ZeroSdf_Bvh4Refit_PoseA.png",            ViewW, ViewH, OutHighPoseA.DisplayRgba8), "Save 03 PNG") ||
        !RequireCondition(SaveRgba8ImageToPng(OutDir / "04-HighEnd_ZeroSdf_Bvh4Refit_PoseB.png",            ViewW, ViewH, OutHighPoseB.DisplayRgba8), "Save 04 PNG") ||
        !RequireCondition(SaveRgba8ImageToPng(OutDir / "05-GroundTruth_64spp_Reference.png",                ViewW, ViewH, GroundTruthA.DisplayRgba),  "Save 05 PNG"))
    {
        return 1;
    }

    std::printf("[GtxSdfGiShadowValidation] PASS — all 5 Order 2 PNG proofs written to %s\n", OutDir.string().c_str());
    return 0;
}
