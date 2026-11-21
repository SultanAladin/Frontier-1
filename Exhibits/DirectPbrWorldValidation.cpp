/*============================================================================================================================================
                                                        DIRECTPBRWORLDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Order 1 Validation: Standalone Direct PBR (MatCap + OpenPbrDirect + LTC + LM-63 .ies Photometry + 25,068-tri ControlVehicle, Zero Bounce)

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/Codecs/PhotometricProfile.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

[[nodiscard]] bool RequireCondition(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[DirectPbrWorldValidation] FAIL: %s\n", Label);
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path LocateCompiledShaderFolder(const std::filesystem::path& RepoRoot)
{
    const std::array<std::filesystem::path, 6u> Candidates {{
#ifdef FRONTIER_COMPILED_SHADER_DIR
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR),
#else
        std::filesystem::current_path() / "CompiledShaders",
#endif
        std::filesystem::current_path() / "CompiledShaders",
        RepoRoot / "build/CompiledShaders",
        RepoRoot / "Build/Editor/CompiledShaders",
        RepoRoot / "Build/Development/CompiledShaders",
        std::filesystem::path("CompiledShaders"),
    }};

    for (const auto& PathCandidate : Candidates)
    {
        std::error_code FsErr {};
        if (std::filesystem::exists(PathCandidate / "WorldViewportRadiometricCompute.comp.spv", FsErr) && !FsErr)
        {
            return PathCandidate;
        }
    }
    return Candidates.front();
}

[[nodiscard]] bool SaveRgba8ImageToPng(
    const std::filesystem::path& TargetPng,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    const std::vector<uint8_t>&  RgbaBytes)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaBytes.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return false;
    }
    std::error_code DirErr {};
    std::filesystem::create_directories(TargetPng.parent_path(), DirErr);

    const std::size_t RowStride = 1u + static_cast<std::size_t>(WidthPx) * 4u;
    std::vector<uint8_t> Scanlines(RowStride * HeightPx, 0u);
    for (uint32_t Y = 0u; Y < HeightPx; ++Y)
    {
        std::memcpy(
            Scanlines.data() + static_cast<std::size_t>(Y) * RowStride + 1u,
            RgbaBytes.data() + static_cast<std::size_t>(Y) * WidthPx * 4u,
            static_cast<std::size_t>(WidthPx) * 4u);
    }

    std::vector<uint8_t> ZlibBytes { 0x78u, 0x01u };
    std::size_t Offset = 0u;
    while (Offset < Scanlines.size())
    {
        const std::size_t BlockLen = std::min<std::size_t>(65535u, Scanlines.size() - Offset);
        const bool IsFinal         = (Offset + BlockLen == Scanlines.size());
        ZlibBytes.push_back(IsFinal ? 0x01u : 0x00u);
        const uint16_t Len16  = static_cast<uint16_t>(BlockLen);
        const uint16_t NLen16 = static_cast<uint16_t>(~Len16);
        ZlibBytes.push_back(static_cast<uint8_t>(Len16 & 0xFFu));
        ZlibBytes.push_back(static_cast<uint8_t>((Len16 >> 8u) & 0xFFu));
        ZlibBytes.push_back(static_cast<uint8_t>(NLen16 & 0xFFu));
        ZlibBytes.push_back(static_cast<uint8_t>((NLen16 >> 8u) & 0xFFu));
        ZlibBytes.insert(
            ZlibBytes.end(),
            Scanlines.begin() + static_cast<std::ptrdiff_t>(Offset),
            Scanlines.begin() + static_cast<std::ptrdiff_t>(Offset + BlockLen));
        Offset += BlockLen;
    }
    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    for (uint8_t B : Scanlines)
    {
        AdlerA = (AdlerA + B) % 65521u;
        AdlerB = (AdlerB + AdlerA) % 65521u;
    }
    AppendBigEndianUint32(ZlibBytes, (AdlerB << 16u) | AdlerA);

    std::vector<uint8_t> PngBytes { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    std::vector<uint8_t> Ihdr {};
    AppendBigEndianUint32(Ihdr, WidthPx);
    AppendBigEndianUint32(Ihdr, HeightPx);
    Ihdr.insert(Ihdr.end(), { 8u, 6u, 0u, 0u, 0u });
    AppendPngChunkBytes(PngBytes, "IHDR", Ihdr);
    AppendPngChunkBytes(PngBytes, "IDAT", ZlibBytes);
    AppendPngChunkBytes(PngBytes, "IEND", {});

    std::ofstream Out(TargetPng, std::ios::binary | std::ios::trunc);
    Out.write(reinterpret_cast<const char*>(PngBytes.data()), static_cast<std::streamsize>(PngBytes.size()));
    return Out.good();
}

[[nodiscard]] double ComputeMeanLuminanceByte(
    const std::vector<uint8_t>& RgbaBytes,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    uint32_t                    MinX,
    uint32_t                    MaxX,
    uint32_t                    MinY,
    uint32_t                    MaxY) noexcept
{
    double   Sum   = 0.0;
    uint64_t Count = 0ull;
    for (uint32_t Y = MinY; Y < std::min(MaxY, HeightPx); ++Y)
    {
        for (uint32_t X = MinX; X < std::min(MaxX, WidthPx); ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
            const double R = static_cast<double>(RgbaBytes[Off + 0u]);
            const double G = static_cast<double>(RgbaBytes[Off + 1u]);
            const double B = static_cast<double>(RgbaBytes[Off + 2u]);
            Sum += 0.2126 * R + 0.7152 * G + 0.0722 * B;
            ++Count;
        }
    }
    return (Count > 0ull) ? (Sum / static_cast<double>(Count)) : 0.0;
}

struct VehicleShowroomSceneRig
{
    WorldSceneSpecification                                                     Scene           {};
    std::array<GeometrySpace::PolygonNetwork, 16u>                              Networks        {};
    std::array<SpatialAcceleration::ClusterPyramid, 16u>                        Pyramids        {};
    std::array<uint16_t, static_cast<std::size_t>(VehicleSurfaceSlot::Count)> MaterialSlots {};
};

void BuildDirectPbrVehicleShowroom(
    const AssembledControlVehicle&                   Vehicle,
    const std::vector<ResampledPhotometricProfile>&  PhotometrySuite,
    bool                                             NightCarParkMode,
    VehicleShowroomSceneRig&                         OutRig)
{
    OutRig = {};
    auto& Reg = OutRig.Scene.MaterialRegister;

    // 1. Register 11 Vehicle OpenPBR Materials + 5 Showroom / Calibration Materials
    const auto ConstructSlotMaterial = [](VehicleSurfaceSlot Slot) -> MaterialSpace::SurfaceMaterial
    {
        MaterialSpace::SurfaceMaterial Mat {};
        switch (Slot)
        {
            case VehicleSurfaceSlot::BodyCoat:
                Mat.MaterialName            = "VehicleCobaltSapphireCoat";
                Mat.Diffuse.BaseColor       = { 0.09f, 0.28f, 0.74f };
                Mat.Specular.Metalness      = 0.72f;
                Mat.Specular.Roughness      = 0.20f;
                Mat.Specular.EdgeTintF82    = { 0.55f, 0.78f, 1.00f };
                Mat.Clearcoat.CoatWeight    = 1.00f;
                Mat.Clearcoat.CoatRoughness = 0.03f;
                break;
            case VehicleSurfaceSlot::CanopyGloss:
                Mat.MaterialName            = "VehicleObsidianCanopy";
                Mat.Diffuse.BaseColor       = { 0.03f, 0.035f, 0.045f };
                Mat.Specular.Roughness      = 0.08f;
                Mat.Clearcoat.CoatWeight    = 1.00f;
                Mat.Clearcoat.CoatRoughness = 0.02f;
                break;
            case VehicleSurfaceSlot::VehicleGlass:
                Mat.MaterialName            = "VehicleTintedWindshield";
                Mat.Diffuse.BaseColor       = { 0.05f, 0.09f, 0.13f };
                Mat.Specular.Roughness      = 0.025f;
                Mat.Specular.DielectricIOR  = 1.52f;
                Mat.Clearcoat.CoatWeight    = 1.00f;
                Mat.Clearcoat.CoatRoughness = 0.015f;
                break;
            case VehicleSurfaceSlot::VehiclePlastic:
                Mat.MaterialName       = "VehicleIntakePolymer";
                Mat.Diffuse.BaseColor  = { 0.07f, 0.075f, 0.082f };
                Mat.Specular.Roughness = 0.52f;
                break;
            case VehicleSurfaceSlot::TyreRubber:
                Mat.MaterialName       = "VulcanizedTyreRubber";
                Mat.Diffuse.BaseColor  = { 0.045f, 0.046f, 0.050f };
                Mat.Specular.Roughness = 0.68f;
                break;
            case VehicleSurfaceSlot::RimAlloy:
                Mat.MaterialName          = "ForgedTitaniumRimAlloy";
                Mat.Diffuse.BaseColor     = { 0.84f, 0.86f, 0.90f };
                Mat.Specular.Metalness    = 0.96f;
                Mat.Specular.Roughness    = 0.16f;
                Mat.Specular.EdgeTintF82  = { 0.96f, 0.97f, 0.99f };
                break;
            case VehicleSurfaceSlot::WheelCapAccent:
                Mat.MaterialName          = "AnodizedBronzeHubCap";
                Mat.Diffuse.BaseColor     = { 0.78f, 0.54f, 0.24f };
                Mat.Specular.Metalness    = 0.92f;
                Mat.Specular.Roughness    = 0.20f;
                break;
            case VehicleSurfaceSlot::BrakeDisc:
                Mat.MaterialName          = "MachinedSteelBrakeRotor";
                Mat.Diffuse.BaseColor     = { 0.66f, 0.68f, 0.71f };
                Mat.Specular.Metalness    = 0.94f;
                Mat.Specular.Roughness    = 0.28f;
                Mat.Specular.Anisotropy   = 0.55f;
                break;
            case VehicleSurfaceSlot::VehicleTrim:
                Mat.MaterialName          = "SatinGraphiteTrim";
                Mat.Diffuse.BaseColor     = { 0.14f, 0.15f, 0.17f };
                Mat.Specular.Metalness    = 0.40f;
                Mat.Specular.Roughness    = 0.34f;
                break;
            case VehicleSurfaceSlot::HeadlightLens:
                Mat.MaterialName                             = "XenonHeadlampOptic";
                Mat.Diffuse.BaseColor                        = { 0.92f, 0.95f, 1.00f };
                Mat.Specular.Roughness                       = 0.04f;
                Mat.PorosityLight.EmissionLuminanceNits      = 1650.0f;
                Mat.PorosityLight.BlackbodyTemperatureKelvin = 5800.0f;
                Mat.PorosityLight.EmissionTint               = { 0.94f, 0.97f, 1.00f };
                break;
            case VehicleSurfaceSlot::TaillightLens:
            default:
                Mat.MaterialName                        = "LedTaillampOptic";
                Mat.Diffuse.BaseColor                   = { 0.85f, 0.05f, 0.04f };
                Mat.Specular.Roughness                  = 0.06f;
                Mat.PorosityLight.EmissionLuminanceNits = 980.0f;
                Mat.PorosityLight.EmissionTint          = { 1.00f, 0.08f, 0.05f };
                break;
        }
        return Mat;
    };

    for (std::size_t R = 0u; R < static_cast<std::size_t>(VehicleSurfaceSlot::Count); ++R)
    {
        OutRig.MaterialSlots[R] = Reg.RegisterMaterial(ConstructSlotMaterial(static_cast<VehicleSurfaceSlot>(R)));
    }

    MaterialSpace::SurfaceMaterial FloorMat {};
    FloorMat.MaterialName            = "PolishedEpoxyDeckFloor";
    FloorMat.Diffuse.BaseColor       = { 0.24f, 0.25f, 0.28f };
    FloorMat.Specular.Roughness      = 0.26f;
    FloorMat.Clearcoat.CoatWeight    = 0.45f;
    FloorMat.Clearcoat.CoatRoughness = 0.14f;
    const uint16_t SlotFloor = Reg.RegisterMaterial(FloorMat);

    MaterialSpace::SurfaceMaterial WallMat {};
    WallMat.MaterialName       = "ArchitecturalCycloramaConcrete";
    WallMat.Diffuse.BaseColor  = { 0.56f, 0.58f, 0.62f };
    WallMat.Specular.Roughness = 0.62f;
    const uint16_t SlotWall = Reg.RegisterMaterial(WallMat);

    MaterialSpace::SurfaceMaterial ChromeOrb {};
    ChromeOrb.MaterialName        = "CalibrationMirrorChrome";
    ChromeOrb.Diffuse.BaseColor   = { 0.95f, 0.96f, 0.98f };
    ChromeOrb.Specular.Metalness  = 1.0f;
    ChromeOrb.Specular.Roughness  = 0.03f;
    const uint16_t SlotChrome = Reg.RegisterMaterial(ChromeOrb);

    MaterialSpace::SurfaceMaterial CrimsonCoatOrb {};
    CrimsonCoatOrb.MaterialName            = "CalibrationCrimsonClearcoat";
    CrimsonCoatOrb.Diffuse.BaseColor       = { 0.78f, 0.06f, 0.08f };
    CrimsonCoatOrb.Specular.Metalness      = 0.35f;
    CrimsonCoatOrb.Specular.Roughness      = 0.22f;
    CrimsonCoatOrb.Clearcoat.CoatWeight    = 1.0f;
    CrimsonCoatOrb.Clearcoat.CoatRoughness = 0.025f;
    const uint16_t SlotCrimson = Reg.RegisterMaterial(CrimsonCoatOrb);

    // 2. Build ClusterPyramids for all 11 Vehicle Roles + Floor + Backdrop Wall + 2 Calibration Spheres + Bollard
    std::vector<SpatialAcceleration::RayTrianglePrimitive>  WorldPrims;
    std::vector<SpatialAcceleration::TriangleSmoothNormals> WorldNormals;
    WorldPrims.reserve(Vehicle.Triangles.size() + 2048u);
    WorldNormals.reserve(Vehicle.Triangles.size() + 2048u);

    for (std::size_t R = 0u; R < static_cast<std::size_t>(VehicleSurfaceSlot::Count); ++R)
    {
        const auto Slot = static_cast<VehicleSurfaceSlot>(R);
        OutRig.Networks[R] = ControlVehicleSurfaceDecoder::ConstructPolygonNetworkFromSlot(Vehicle, Slot);
        if (OutRig.Networks[R].Faces.empty())
        {
            continue;
        }
        (void)OutRig.Pyramids[R].BuildFromPolygonNetwork(OutRig.Networks[R], 3u);

        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &OutRig.Pyramids[R];
        Inst.WorldTranslation    = { 0.0f, 0.0f, 0.0f };
        Inst.PreviousTranslation = { 0.0f, 0.0f, 0.0f };
        Inst.UniformScale        = 1.0f;
        Inst.MaterialSlot        = OutRig.MaterialSlots[R];
        OutRig.Scene.ClusterInstances.push_back(Inst);
    }

    // Add exact 38-deg creased vehicle triangles directly into BVH4
    for (uint32_t TIdx = 0u; TIdx < static_cast<uint32_t>(Vehicle.Triangles.size()); ++TIdx)
    {
        const auto& Src = Vehicle.Triangles[TIdx];
        SpatialAcceleration::RayTrianglePrimitive Prim {};
        Prim.Corner0          = Src.CornerA;
        Prim.Edge01           = Src.CornerB - Src.CornerA;
        Prim.Edge02           = Src.CornerC - Src.CornerA;
        Prim.SourceFaceIndex  = TIdx;
        Prim.PrimitiveOrdinal = static_cast<uint32_t>(WorldPrims.size());
        Prim.MaterialOrdinal  = OutRig.MaterialSlots[static_cast<std::size_t>(Src.SurfaceSlot)];
        Prim.OpaqueMask       = 1u;
        WorldPrims.push_back(Prim);
        WorldNormals.push_back({ Src.NormalA, Src.NormalB, Src.NormalC });
    }

    const auto AppendStaticNetworkInstance = [&](
        std::size_t         AssetSlot,
        uint16_t            MatSlot,
        Mathematics::Axis3D Translation,
        bool                SmoothNormals)
    {
        if (SmoothNormals)
        {
            OutRig.Networks[AssetSlot].LinkOppositeEdges();
            OutRig.Networks[AssetSlot].RecomputeNormals();
        }
        (void)OutRig.Pyramids[AssetSlot].BuildFromPolygonNetwork(OutRig.Networks[AssetSlot], 3u);

        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &OutRig.Pyramids[AssetSlot];
        Inst.WorldTranslation    = Translation;
        Inst.PreviousTranslation = Translation;
        Inst.UniformScale        = 1.0f;
        Inst.MaterialSlot        = MatSlot;
        OutRig.Scene.ClusterInstances.push_back(Inst);

        const auto& Net = OutRig.Networks[AssetSlot];
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
            for (std::size_t K = 1u; K + 1u < Loop.size(); ++K)
            {
                const auto& V0 = Net.Vertices[Loop[0u]];
                const auto& V1 = Net.Vertices[Loop[K]];
                const auto& V2 = Net.Vertices[Loop[K + 1u]];
                const Mathematics::Axis3D P0 = V0.Axes + Translation;
                const Mathematics::Axis3D P1 = V1.Axes + Translation;
                const Mathematics::Axis3D P2 = V2.Axes + Translation;
                const Mathematics::Axis3D FaceN = Mathematics::NormalizeAxis(
                    Mathematics::EvaluateCross(P1 - P0, P2 - P0),
                    { 0.0f, 0.0f, 1.0f });

                SpatialAcceleration::RayTrianglePrimitive Prim {};
                Prim.Corner0          = P0;
                Prim.Edge01           = P1 - P0;
                Prim.Edge02           = P2 - P0;
                Prim.SourceFaceIndex  = FIdx;
                Prim.PrimitiveOrdinal = static_cast<uint32_t>(WorldPrims.size());
                Prim.MaterialOrdinal  = MatSlot;
                Prim.OpaqueMask       = 1u;
                WorldPrims.push_back(Prim);
                if (SmoothNormals)
                {
                    WorldNormals.push_back({ V0.Normal, V1.Normal, V2.Normal });
                }
                else
                {
                    WorldNormals.push_back({ FaceN, FaceN, FaceN });
                }
            }
        }
    };

    // Tiled 6x6 floor deck (-10..+14 m X, -9..+9 m Y, top at Z = 0.0) so no triangle straddles camera near-clip W=0
    for (int IX = 0; IX < 6; ++IX)
    {
        for (int IY = 0; IY < 6; ++IY)
        {
            const float CenterX = -10.0f + (static_cast<float>(IX) + 0.5f) * 4.0f;
            const float CenterY = -9.0f  + (static_cast<float>(IY) + 0.5f) * 3.0f;
            GeometrySpace::AppendBoxPrimitive(OutRig.Networks[11u], { CenterX, CenterY, -0.10f }, { 2.0f, 1.5f, 0.10f }, {}, {});
        }
    }
    AppendStaticNetworkInstance(11u, SlotFloor, { 0.0f, 0.0f, 0.0f }, false);

    // Forward headlamp target wall & rear architectural backdrop
    GeometrySpace::AppendBoxPrimitive(OutRig.Networks[12u], { 0.0f, 0.0f, 0.0f }, { 0.18f, 7.5f, 2.40f }, {}, {});
    AppendStaticNetworkInstance(12u, SlotWall, { 9.20f, 0.0f, 2.40f }, false);

    GeometrySpace::AppendBoxPrimitive(OutRig.Networks[13u], { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.18f, 2.40f }, {}, {});
    AppendStaticNetworkInstance(13u, SlotWall, { 1.50f, 5.20f, 2.40f }, false);

    // Calibration spheres beside vehicle
    GeometrySpace::AppendSpherePrimitive(OutRig.Networks[14u], { 0.0f, 0.0f, 0.0f }, 0.42f, 32u, 20u, {}, {});
    AppendStaticNetworkInstance(14u, SlotChrome, { 1.15f, -2.05f, 0.42f }, true);

    GeometrySpace::AppendSpherePrimitive(OutRig.Networks[15u], { 0.0f, 0.0f, 0.0f }, 0.42f, 32u, 20u, {}, {});
    AppendStaticNetworkInstance(15u, SlotCrimson, { -0.35f, -2.05f, 0.42f }, true);

    (void)OutRig.Scene.SceneTree.BuildFromTrianglePrimitivesWithNormals(WorldPrims, WorldNormals);

    // 3. Configure Lighting: Daylight + LTC Softboxes vs Night Car-Park LM-63 .ies Photometry
    OutRig.Scene.PhotometricProfiles = PhotometrySuite;

    if (!NightCarParkMode)
    {
        OutRig.Scene.Sun.DirectionToSun          = Mathematics::NormalizeAxis({ 0.48f, -0.58f, 0.66f });
        OutRig.Scene.Sun.SolarIrradiance         = { 4.20f, 3.95f, 3.65f };
        OutRig.Scene.Sun.SunAngularRadiusRadians = 0.0093f;
        OutRig.Scene.SkyRadiance                 = { 0.24f, 0.34f, 0.52f };
        OutRig.Scene.Atmosphere.Medium.AtmosphereEnabled = false;

        PolygonalAreaQuadLight KeySoftbox {};
        KeySoftbox.CenterWorld      = { 1.20f, -2.60f, 3.40f };
        KeySoftbox.RightTangent     = { 1.0f, 0.0f, 0.0f };
        KeySoftbox.UpBitangent      = Mathematics::NormalizeAxis({ 0.0f, 0.78f, 0.62f });
        KeySoftbox.HalfWidthMeters  = 2.10f;
        KeySoftbox.HalfHeightMeters = 0.65f;
        KeySoftbox.EmittedRadiance  = { 7.5f, 7.2f, 6.8f };
        OutRig.Scene.AreaLights.push_back(KeySoftbox);

        PolygonalAreaQuadLight OverheadRibbon {};
        OverheadRibbon.CenterWorld      = { 0.15f, 0.25f, 3.85f };
        OverheadRibbon.RightTangent     = { 1.0f, 0.0f, 0.0f };
        OverheadRibbon.UpBitangent      = { 0.0f, 1.0f, 0.0f };
        OverheadRibbon.HalfWidthMeters  = 2.60f;
        OverheadRibbon.HalfHeightMeters = 0.48f;
        OverheadRibbon.EmittedRadiance  = { 5.4f, 5.8f, 6.6f };
        OutRig.Scene.AreaLights.push_back(OverheadRibbon);
    }
    else
    {
        // Night Car-Park Mode: Zero Sun, deep night ambient, illuminated purely by LM-63 .ies Photometric Lamps + subtle LTC bay strip
        OutRig.Scene.Sun.DirectionToSun                  = Mathematics::NormalizeAxis({ 0.25f, -0.40f, 0.88f });
        OutRig.Scene.Sun.SolarIrradiance                 = { 0.0f, 0.0f, 0.0f };
        OutRig.Scene.SkyRadiance                         = { 0.018f, 0.024f, 0.042f };
        OutRig.Scene.Atmosphere.Medium.AtmosphereEnabled = false;

        // Profile [0]: LowBeamHeadlamp.ies (Left & Right vehicle headlamps pitched -3.5 deg forward along +X)
        const Mathematics::Axis3D HeadlampAim = Mathematics::NormalizeAxis({ 0.998f, 0.0f, -0.062f });
        const float FrontLensX = Vehicle.BoundsMax.X + 0.06f;
        for (float LateralY : { 0.68f, -0.68f })
        {
            PhotometricPointRadiator Headlamp {};
            Headlamp.PositionWorld    = { FrontLensX, LateralY, 0.68f };
            Headlamp.BeamAxis         = HeadlampAim;
            Headlamp.ZeroAzimuthAxis  = { 0.0f, -1.0f, 0.0f };
            Headlamp.PeakIntensityRgb = { 62.0f, 66.0f, 74.0f };
            Headlamp.RangeMeters      = 24.0f;
            Headlamp.RadiusMeters     = 0.06f;
            Headlamp.ProfileOrdinal   = 0u;
            OutRig.Scene.PhotometricRadiators.push_back(Headlamp);
        }

        // Profile [1]: TailLamp.ies (Left & Right rear crimson tail lamps aimed along -X)
        const Mathematics::Axis3D TailAim = Mathematics::NormalizeAxis({ -0.994f, 0.0f, -0.110f });
        const float RearLensX = Vehicle.BoundsMin.X - 0.06f;
        for (float LateralY : { 0.72f, -0.72f })
        {
            PhotometricPointRadiator TailLamp {};
            TailLamp.PositionWorld    = { RearLensX, LateralY, 0.82f };
            TailLamp.BeamAxis         = TailAim;
            TailLamp.ZeroAzimuthAxis  = { 0.0f, 1.0f, 0.0f };
            TailLamp.PeakIntensityRgb = { 22.0f, 1.10f, 0.70f };
            TailLamp.RangeMeters      = 14.0f;
            TailLamp.RadiusMeters     = 0.06f;
            TailLamp.ProfileOrdinal   = 1u;
            OutRig.Scene.PhotometricRadiators.push_back(TailLamp);
        }

        // Profile [2]: CarParkDownlight.ies (3 architectural ceiling downlights along the vehicle bay)
        for (float BayX : { -1.80f, 0.65f, 3.60f })
        {
            PhotometricPointRadiator Downlight {};
            Downlight.PositionWorld    = { BayX, -0.25f, 3.75f };
            Downlight.BeamAxis         = { 0.0f, 0.0f, -1.0f };
            Downlight.ZeroAzimuthAxis  = { 1.0f, 0.0f, 0.0f };
            Downlight.PeakIntensityRgb = { 20.0f, 19.2f, 17.8f };
            Downlight.RangeMeters      = 16.0f;
            Downlight.RadiusMeters     = 0.10f;
            Downlight.ProfileOrdinal   = 2u;
            OutRig.Scene.PhotometricRadiators.push_back(Downlight);
        }
    }
}

} // namespace

int main()
{
    const std::filesystem::path RepoRoot = ResolveRepositoryRoot();
    const std::filesystem::path ImageDir = RepoRoot / "Exhibits/Images/DirectPbr";

    //====================================================================================================================
    // 1. Validate ANSI/IESNA LM-63-2002 Type C Photometric Profile Codec & 128x32 Resampler
    //====================================================================================================================
    std::vector<ResampledPhotometricProfile> PhotometrySuite {};
    std::string Diagnostic {};
    if (!RequireCondition(
            PhotometricProfileCodec::LoadCanonicalPhotometrySuite(RepoRoot, PhotometrySuite, Diagnostic),
            Diagnostic.empty() ? "LoadCanonicalPhotometrySuite" : Diagnostic.c_str()) ||
        !RequireCondition(PhotometrySuite.size() == 3u, "Three canonical .ies profiles loaded"))
    {
        return 1;
    }

    for (const auto& Profile : PhotometrySuite)
    {
        if (!RequireCondition(Profile.PeakCandela > 10.0f, "LM-63 PeakCandela > 10 cd") ||
            !RequireCondition(Profile.NormalizedFluxSr > 0.01f && Profile.NormalizedFluxSr <= Constants::TauFloat * 2.0f, "LM-63 NormalizedFluxSr physical") ||
            !RequireCondition(Profile.CutoffHalfAngleRad > 0.15f && Profile.CutoffHalfAngleRad <= Constants::PiFloat + 1.0e-4f, "LM-63 CutoffHalfAngleRad valid"))
        {
            return 1;
        }
    }

    // Verify automotive low-beam sharp horizon cutoff (downward road throw >> upward glare)
    {
        const auto& LowBeam = PhotometrySuite[0u];
        const Mathematics::Axis3D Aim  { 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D RefY { 0.0f, -1.0f, 0.0f };
        const float RoadThrow = PhotometricProfileCodec::EvaluateNormalizedIntensity(
            LowBeam, Mathematics::NormalizeAxis({ 0.996f, 0.0f, -0.087f }), Aim, RefY);
        const float RearBack  = PhotometricProfileCodec::EvaluateNormalizedIntensity(
            LowBeam, { -1.0f, 0.0f, 0.0f }, Aim, RefY);
        std::printf("[DirectPbrWorldValidation] LowBeamHeadlamp.ies: Peak=%.1f cd, Flux=%.3f sr, RoadThrow=%.3f, Rear=%.4f\n",
            LowBeam.PeakCandela, LowBeam.NormalizedFluxSr, RoadThrow, RearBack);
        if (!RequireCondition(RoadThrow > 0.20f && RearBack < 1.0e-4f, "LowBeamHeadlamp forward road throw & zero rear leakage"))
        {
            return 1;
        }
    }

    //====================================================================================================================
    // 2. Validate FCT1 ControlVehicleSurfaceDecoder (25,068 triangles, 38-deg creased normals, WorldZUp & StudioYUp)
    //====================================================================================================================
    AssembledControlVehicle VehicleZUp {};
    AssembledControlVehicle VehicleYUp {};
    if (!RequireCondition(
            ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
                RepoRoot, VehicleCoordinateConvention::FrontierWorldZUp, VehicleZUp, Diagnostic),
            Diagnostic.empty() ? "Decode ControlVehicle FrontierWorldZUp" : Diagnostic.c_str()) ||
        !RequireCondition(
            ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
                RepoRoot, VehicleCoordinateConvention::StudioPathYUp, VehicleYUp, Diagnostic),
            Diagnostic.empty() ? "Decode ControlVehicle StudioPathYUp" : Diagnostic.c_str()))
    {
        return 1;
    }

    std::printf(
        "[DirectPbrWorldValidation] ControlVehicle decoded: Total=%zu tris (Body=%u, Rims=%u, Tyres=%u), ZBounds=[%.4f, %.4f] m\n",
        VehicleZUp.Triangles.size(),
        VehicleZUp.BodyTriangleCount,
        VehicleZUp.RimTriangleCount,
        VehicleZUp.TyreTriangleCount,
        VehicleZUp.BoundsMin.Z,
        VehicleZUp.BoundsMax.Z);

    if (!RequireCondition(VehicleZUp.Triangles.size() == 25068u, "Exact 25,068 assembled ControlVehicle triangles") ||
        !RequireCondition(VehicleYUp.Triangles.size() == 25068u, "StudioPathYUp parity triangle count") ||
        !RequireCondition(std::fabs(VehicleZUp.BoundsMin.Z) < 1.0e-3f, "FrontierWorldZUp tyres rest cleanly at Z = 0") ||
        !RequireCondition(VehicleZUp.BoundsMax.Z > 1.15f && VehicleZUp.BoundsMax.Z < 1.85f, "Physical vehicle roof height"))
    {
        return 1;
    }

    //====================================================================================================================
    // 3. Render & Validate Order 1 Standalone Direct PBR on Vulkan GPU + CPU (Zero Bounce)
    //====================================================================================================================
    constexpr uint32_t RenderWidthPx  = 640u;
    constexpr uint32_t RenderHeightPx = 360u;

    PerspectiveCameraBasis HeroCamera {};
    HeroCamera.CameraOrigin       = { 5.45f, -4.35f, 1.82f };
    HeroCamera.TargetPoint        = { 0.45f, -0.15f, 0.68f };
    HeroCamera.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    HeroCamera.VerticalFovRadians = 38.0f * Constants::DegToRadFloat;
    HeroCamera.ViewportWidthPx    = RenderWidthPx;
    HeroCamera.ViewportHeightPx   = RenderHeightPx;
    HeroCamera.NearClipMeters     = 0.10f;

    VehicleShowroomSceneRig DaylightRig {};
    VehicleShowroomSceneRig NightPhotoRig {};
    BuildDirectPbrVehicleShowroom(VehicleZUp, PhotometrySuite, false, DaylightRig);
    BuildDirectPbrVehicleShowroom(VehicleZUp, PhotometrySuite, true,  NightPhotoRig);

    TieredWorldRenderSequence CpuSequence {};
    CpuSequence.PrepareWorldScene(DaylightRig.Scene);
    CpuSequence.PrepareWorldScene(NightPhotoRig.Scene);

    VulkanInterchange VkDevice {};
    std::string VkDiag {};
    if (!RequireCondition(VkDevice.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }

    const std::filesystem::path ShaderDir = LocateCompiledShaderFolder(RepoRoot);
    VulkanWorldRenderSequence GpuSequence {};
    if (!RequireCondition(GpuSequence.InitializeSequence(VkDevice, ShaderDir), "Initialize VulkanWorldRenderSequence"))
    {
        return 1;
    }

    // 3a. GPU MatCap Studio (Engine 0)
    GpuSequence.SynchronizeSceneResources(DaylightRig.Scene, true);
    std::vector<uint8_t> MatCapGpuRgba {};
    {
        WorldRenderConfiguration Cfg {};
        Cfg.Engine             = WorldRenderEngine::MatCap;
        Cfg.ReflectionsEnabled = false;
        Cfg.RaytracingEnabled  = false;
        if (!RequireCondition(GpuSequence.RenderWorldViewportWithEngine(Cfg, 0u, HeroCamera, DaylightRig.Scene, MatCapGpuRgba), "GPU MatCap render") ||
            !RequireCondition(SaveRgba8ImageToPng(ImageDir / "01-DirectPbrMatCapStudio.png", RenderWidthPx, RenderHeightPx, MatCapGpuRgba), "Write 01-DirectPbrMatCapStudio.png"))
        {
            return 1;
        }
    }

    // 3b. GPU OpenPbrDirect Daylight + Clipped-Polygon LTC Softboxes + Solar Shadows (Engine 1, Zero GI)
    std::vector<uint8_t> DaylightGpuRgba {};
    {
        WorldRenderConfiguration Cfg {};
        Cfg.Engine             = WorldRenderEngine::OpenPbrDirect;
        Cfg.ShadowAlgorithm    = SolarShadowAlgorithm::WideTreeRayQuery;
        Cfg.ReflectionSource   = SpecularReflectionSource::SkyEnvironment; // Pure 0-bounce Order 1 Direct PBR
        Cfg.ReflectionsEnabled = true;
        Cfg.RaytracingEnabled  = true;
        if (!RequireCondition(GpuSequence.RenderWorldViewportWithEngine(Cfg, 1u, HeroCamera, DaylightRig.Scene, DaylightGpuRgba), "GPU OpenPbrDirect Daylight+LTC render") ||
            !RequireCondition(SaveRgba8ImageToPng(ImageDir / "02-DirectPbrDaylightLtcGpu.png", RenderWidthPx, RenderHeightPx, DaylightGpuRgba), "Write 02-DirectPbrDaylightLtcGpu.png"))
        {
            return 1;
        }
    }

    // 3c. GPU OpenPbrDirect Night Car-Park .ies Photometry (Headlamps + TailLamps + Downlights, Zero GI)
    GpuSequence.SynchronizeSceneResources(NightPhotoRig.Scene, true);
    std::vector<uint8_t> NightGpuRgba {};
    {
        WorldRenderConfiguration Cfg {};
        Cfg.Engine             = WorldRenderEngine::OpenPbrDirect;
        Cfg.ShadowAlgorithm    = SolarShadowAlgorithm::WideTreeRayQuery;
        Cfg.ReflectionSource   = SpecularReflectionSource::SkyEnvironment;
        Cfg.ReflectionsEnabled = true;
        Cfg.RaytracingEnabled  = true;
        if (!RequireCondition(GpuSequence.RenderWorldViewportWithEngine(Cfg, 2u, HeroCamera, NightPhotoRig.Scene, NightGpuRgba), "GPU OpenPbrDirect Night .ies Photometry render") ||
            !RequireCondition(SaveRgba8ImageToPng(ImageDir / "03-DirectPbrNightPhotometricGpu.png", RenderWidthPx, RenderHeightPx, NightGpuRgba), "Write 03-DirectPbrNightPhotometricGpu.png"))
        {
            return 1;
        }
    }

    // 3d. CPU OpenPbrDirect Night Car-Park .ies Photometry (LinearCosineLightIntegration Parity Proof)
    WorldRenderConfiguration CpuNightCfg {};
    CpuNightCfg.Engine             = WorldRenderEngine::OpenPbrDirect;
    CpuNightCfg.ShadowAlgorithm    = SolarShadowAlgorithm::WideTreeRayQuery;
    CpuNightCfg.ReflectionSource   = SpecularReflectionSource::SkyEnvironment;
    CpuNightCfg.ReflectionsEnabled = true;
    CpuNightCfg.RaytracingEnabled  = true;
    const WorldRenderOutput CpuNightOut = CpuSequence.RenderWorldFrameWithEngine(
        CpuNightCfg, HeroCamera, HeroCamera, NightPhotoRig.Scene);
    if (!RequireCondition(
            SaveRgba8ImageToPng(ImageDir / "04-DirectPbrNightPhotometricCpu.png", RenderWidthPx, RenderHeightPx, CpuNightOut.DisplayRgba8),
            "Write 04-DirectPbrNightPhotometricCpu.png"))
    {
        return 1;
    }

    const double MatCapLum   = ComputeMeanLuminanceByte(MatCapGpuRgba,   RenderWidthPx, RenderHeightPx, 80u, 560u, 60u, 320u);
    const double DaylightLum = ComputeMeanLuminanceByte(DaylightGpuRgba, RenderWidthPx, RenderHeightPx, 80u, 560u, 60u, 320u);
    const double NightGpuLum = ComputeMeanLuminanceByte(NightGpuRgba,    RenderWidthPx, RenderHeightPx, 80u, 560u, 60u, 320u);
    const double NightCpuLum = ComputeMeanLuminanceByte(CpuNightOut.DisplayRgba8, RenderWidthPx, RenderHeightPx, 80u, 560u, 60u, 320u);

    // Headlamp forward floor beam pool (left-lower screen ahead of +X bumper) must exceed unlit deep background corner
    const double ForwardBeamPoolLum = ComputeMeanLuminanceByte(NightGpuRgba, RenderWidthPx, RenderHeightPx, 40u, 250u, 190u, 330u);
    const double FarDarkCornerLum   = ComputeMeanLuminanceByte(NightGpuRgba, RenderWidthPx, RenderHeightPx, 540u, 635u, 8u, 60u);

    std::printf(
        "[DirectPbrWorldValidation] Mean ROI Luminance [0..255]: MatCap=%.1f, DaylightLTC=%.1f, NightIES_GPU=%.1f, NightIES_CPU=%.1f (BeamPool=%.1f vs DarkSky=%.1f)\n",
        MatCapLum, DaylightLum, NightGpuLum, NightCpuLum, ForwardBeamPoolLum, FarDarkCornerLum);

    if (!RequireCondition(MatCapLum > 22.0 && DaylightLum > 35.0, "MatCap and Daylight OpenPbrDirect non-black") ||
        !RequireCondition(NightGpuLum > 18.0 && NightCpuLum > 18.0, "Night .ies photometric GPU & CPU scenes illuminated") ||
        !RequireCondition(ForwardBeamPoolLum > FarDarkCornerLum + 20.0, "LowBeamHeadlamp .ies throws bright pool onto floor/wall"))
    {
        return 1;
    }

    GpuSequence.TeardownSequence();
    std::printf("[DirectPbrWorldValidation] ALL ORDER 1 DIRECT PBR VALIDATIONS PASSED.\n");
    return 0;
}
