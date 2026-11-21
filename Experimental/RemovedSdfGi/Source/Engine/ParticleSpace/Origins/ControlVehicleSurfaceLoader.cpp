/*============================================================================================================================================
                                                       CONTROLVEHICLESURFACELOADER.CPP
============================================================================================================================================*/
// 🧩 Decodes Frontier's Controlled Integrator (ControlVehicle) FCT1 binary surfaces (Body + 4 Rims + 4 Tyres) and samples blue-noise anchors

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace Frontier::ParticleSpace
{

namespace
{

// Placement constants matching Experimental/DeterministicPathTracer/Source/VehicleAssembly.js:
// Authored axes (+X forward, +Y left, +Z up) -> Studio axes (+X forward, +Y up, +Z right), det = +1.
constexpr float    VehicleHeightRebase  = 0.11285f;               // [m]
constexpr float    VehicleGroundLift    = 0.53000f;               // [m]
constexpr float    RimToHubScale        = 0.34460f / 0.344818f;   // [-]
constexpr float    BodyCreaseCosine     = 0.78801075f;            // cos(38 deg)
constexpr uint32_t CornerNormalsBit     = 1u;
constexpr uint32_t Fct1HeaderBytes      = 48u;

struct AxleMountSpec
{
    float                  MountAuthored[3];
    bool                   MirrorLateral;
    ControlVehiclePartCategory RimCategory;
    ControlVehiclePartCategory TyreCategory;
};

constexpr std::array<AxleMountSpec, 4u> VehicleAxleMounts = {{
    { {  1.7274f,  1.0475f, 0.0914f }, false, ControlVehiclePartCategory::RimFrontLeft,  ControlVehiclePartCategory::TyreFrontLeft  },
    { {  1.7274f, -1.0475f, 0.0914f }, true,  ControlVehiclePartCategory::RimFrontRight, ControlVehiclePartCategory::TyreFrontRight },
    { { -1.6686f,  1.0475f, 0.0914f }, false, ControlVehiclePartCategory::RimRearLeft,   ControlVehiclePartCategory::TyreRearLeft   },
    { { -1.6686f, -1.0475f, 0.0914f }, true,  ControlVehiclePartCategory::RimRearRight,  ControlVehiclePartCategory::TyreRearRight  },
}};

struct DecodedFct1Stream
{
    uint32_t              VertexCount   = 0u;
    uint32_t              FaceCount     = 0u;
    std::vector<float>    Positions     {};
    std::vector<uint32_t> Indices       {};
    std::vector<uint32_t> SlotCodes     {};
    std::vector<float>    CornerNormals {};
    bool                  Valid         = false;
};

uint32_t ReadUint32LE(const uint8_t* Ptr) noexcept
{
    uint32_t Out = 0u;
    std::memcpy(&Out, Ptr, sizeof(Out));
    return Out;
}

float ReadFloat32LE(const uint8_t* Ptr) noexcept
{
    float Out = 0.0f;
    std::memcpy(&Out, Ptr, sizeof(Out));
    return Out;
}

[[nodiscard]] std::filesystem::path LocateVehicleBinaryFile(const char* FileName)
{
    const std::vector<std::filesystem::path> SearchRoots = {
        std::filesystem::path("EngineContent/EnvironmentArchives/ControlVehicle"),
        std::filesystem::path("../EngineContent/EnvironmentArchives/ControlVehicle"),
        std::filesystem::path("../../EngineContent/EnvironmentArchives/ControlVehicle"),
        std::filesystem::path("../../../EngineContent/EnvironmentArchives/ControlVehicle"),
#ifdef FRONTIER_REPO_ROOT_DIR
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent/EnvironmentArchives/ControlVehicle",
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Experimental/DeterministicPathTracer/Assets",
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Experimental/VehicleProvingGrounds/Archive",
#endif
        std::filesystem::path("Experimental/DeterministicPathTracer/Assets"),
        std::filesystem::path("Experimental/VehicleProvingGrounds/Archive"),
        std::filesystem::path("../Experimental/DeterministicPathTracer/Assets"),
    };
    for (const auto& Root : SearchRoots)
    {
        const auto Candidate = Root / FileName;
        std::error_code Err {};
        if (std::filesystem::exists(Candidate, Err) && !Err)
        {
            return Candidate;
        }
    }
    return {};
}

[[nodiscard]] DecodedFct1Stream DecodeFct1BinaryFile(const std::filesystem::path& FilePath)
{
    DecodedFct1Stream Stream {};
    if (FilePath.empty()) return Stream;

    std::ifstream Input(FilePath, std::ios::binary | std::ios::ate);
    if (!Input.is_open()) return Stream;

    const std::streamsize ByteSize = Input.tellg();
    if (ByteSize < static_cast<std::streamsize>(Fct1HeaderBytes)) return Stream;

    std::vector<uint8_t> RawBytes(static_cast<std::size_t>(ByteSize));
    Input.seekg(0, std::ios::beg);
    if (!Input.read(reinterpret_cast<char*>(RawBytes.data()), ByteSize)) return Stream;

    if (RawBytes[0] != 'F' || RawBytes[1] != 'C' || RawBytes[2] != 'T' || RawBytes[3] != '1')
    {
        return Stream;
    }

    Stream.VertexCount        = ReadUint32LE(RawBytes.data() + 4u);
    Stream.FaceCount          = ReadUint32LE(RawBytes.data() + 8u);
    const uint32_t HeaderBits = ReadUint32LE(RawBytes.data() + 12u);

    const std::size_t PosBytes   = static_cast<std::size_t>(Stream.VertexCount) * 12u;
    const std::size_t IdxBytes   = static_cast<std::size_t>(Stream.FaceCount) * 12u;
    const std::size_t SlotBytes  = static_cast<std::size_t>(Stream.FaceCount) * 4u;
    const bool        HasNormals = (HeaderBits & CornerNormalsBit) != 0u;
    const std::size_t NormBytes  = HasNormals ? (static_cast<std::size_t>(Stream.FaceCount) * 36u) : 0u;
    const std::size_t Expected   = static_cast<std::size_t>(Fct1HeaderBytes) + PosBytes + IdxBytes + SlotBytes + NormBytes;

    if (RawBytes.size() != Expected) return Stream;

    std::size_t Cursor = Fct1HeaderBytes;
    Stream.Positions.resize(static_cast<std::size_t>(Stream.VertexCount) * 3u);
    for (std::size_t I = 0u; I < Stream.Positions.size(); ++I)
    {
        Stream.Positions[I] = ReadFloat32LE(RawBytes.data() + Cursor + I * 4u);
    }
    Cursor += PosBytes;

    Stream.Indices.resize(static_cast<std::size_t>(Stream.FaceCount) * 3u);
    for (std::size_t I = 0u; I < Stream.Indices.size(); ++I)
    {
        Stream.Indices[I] = ReadUint32LE(RawBytes.data() + Cursor + I * 4u);
    }
    Cursor += IdxBytes;

    Stream.SlotCodes.resize(Stream.FaceCount);
    for (std::size_t I = 0u; I < Stream.SlotCodes.size(); ++I)
    {
        Stream.SlotCodes[I] = ReadUint32LE(RawBytes.data() + Cursor + I * 4u);
    }
    Cursor += SlotBytes;

    if (HasNormals)
    {
        Stream.CornerNormals.resize(static_cast<std::size_t>(Stream.FaceCount) * 9u);
        for (std::size_t I = 0u; I < Stream.CornerNormals.size(); ++I)
        {
            Stream.CornerNormals[I] = ReadFloat32LE(RawBytes.data() + Cursor + I * 4u);
        }
    }
    else
    {
        // Derive 38-degree creased area-weighted corner normals (crisp body creases, smooth panels)
        std::vector<float> FaceNormals(static_cast<std::size_t>(Stream.FaceCount) * 3u, 0.0f);
        std::vector<std::vector<uint32_t>> VertexFaces(Stream.VertexCount);

        for (uint32_t F = 0u; F < Stream.FaceCount; ++F)
        {
            const uint32_t Ia = Stream.Indices[F * 3u + 0u];
            const uint32_t Ib = Stream.Indices[F * 3u + 1u];
            const uint32_t Ic = Stream.Indices[F * 3u + 2u];
            if (Ia >= Stream.VertexCount || Ib >= Stream.VertexCount || Ic >= Stream.VertexCount) continue;

            const float Ax = Stream.Positions[Ia * 3u + 0u], Ay = Stream.Positions[Ia * 3u + 1u], Az = Stream.Positions[Ia * 3u + 2u];
            const float Bx = Stream.Positions[Ib * 3u + 0u], By = Stream.Positions[Ib * 3u + 1u], Bz = Stream.Positions[Ib * 3u + 2u];
            const float Cx = Stream.Positions[Ic * 3u + 0u], Cy = Stream.Positions[Ic * 3u + 1u], Cz = Stream.Positions[Ic * 3u + 2u];
            const float E1x = Bx - Ax, E1y = By - Ay, E1z = Bz - Az;
            const float E2x = Cx - Ax, E2y = Cy - Ay, E2z = Cz - Az;
            FaceNormals[F * 3u + 0u] = E1y * E2z - E1z * E2y;
            FaceNormals[F * 3u + 1u] = E1z * E2x - E1x * E2z;
            FaceNormals[F * 3u + 2u] = E1x * E2y - E1y * E2x;
            VertexFaces[Ia].push_back(F);
            VertexFaces[Ib].push_back(F);
            VertexFaces[Ic].push_back(F);
        }

        Stream.CornerNormals.resize(static_cast<std::size_t>(Stream.FaceCount) * 9u, 0.0f);
        for (uint32_t F = 0u; F < Stream.FaceCount; ++F)
        {
            const float Fx = FaceNormals[F * 3u + 0u];
            const float Fy = FaceNormals[F * 3u + 1u];
            const float Fz = FaceNormals[F * 3u + 2u];
            const float Fl = std::sqrt(Fx * Fx + Fy * Fy + Fz * Fz);
            const float InvFl = (Fl > 1e-10f) ? (1.0f / Fl) : 1.0f;

            for (uint32_t C = 0u; C < 3u; ++C)
            {
                const uint32_t Vidx = Stream.Indices[F * 3u + C];
                float Sx = 0.0f, Sy = 0.0f, Sz = 0.0f;
                if (Vidx < Stream.VertexCount)
                {
                    for (const uint32_t Adj : VertexFaces[Vidx])
                    {
                        const float Ax = FaceNormals[Adj * 3u + 0u];
                        const float Ay = FaceNormals[Adj * 3u + 1u];
                        const float Az = FaceNormals[Adj * 3u + 2u];
                        const float Al = std::sqrt(Ax * Ax + Ay * Ay + Az * Az);
                        if (Al <= 1e-10f) continue;
                        const float CosTheta = (Ax * Fx + Ay * Fy + Az * Fz) * InvFl / Al;
                        if (CosTheta < BodyCreaseCosine) continue;
                        Sx += Ax;
                        Sy += Ay;
                        Sz += Az;
                    }
                }
                const float Sl = std::sqrt(Sx * Sx + Sy * Sy + Sz * Sz);
                if (Sl > 1e-10f)
                {
                    Stream.CornerNormals[F * 9u + C * 3u + 0u] = Sx / Sl;
                    Stream.CornerNormals[F * 9u + C * 3u + 1u] = Sy / Sl;
                    Stream.CornerNormals[F * 9u + C * 3u + 2u] = Sz / Sl;
                }
                else
                {
                    Stream.CornerNormals[F * 9u + C * 3u + 0u] = 0.0f;
                    Stream.CornerNormals[F * 9u + C * 3u + 1u] = 0.0f;
                    Stream.CornerNormals[F * 9u + C * 3u + 2u] = 1.0f;
                }
            }
        }
    }

    Stream.Valid = true;
    return Stream;
}

void AuthoredToStudioPosition(float X, float YLeft, float ZUp, float Out[3]) noexcept
{
    Out[0] = X;
    Out[1] = ZUp - VehicleHeightRebase + VehicleGroundLift;
    Out[2] = -YLeft;
}

void AuthoredToStudioNormal(float Nx, float NyLeft, float NzUp, float Out[3]) noexcept
{
    const float L = std::sqrt(Nx * Nx + NyLeft * NyLeft + NzUp * NzUp);
    const float Inv = (L > 1e-8f) ? (1.0f / L) : 1.0f;
    Out[0] = Nx * Inv;
    Out[1] = NzUp * Inv;
    Out[2] = -NyLeft * Inv;
}

void AppendStreamInstance(
    std::vector<ControlVehicleTriangle>& OutTriangles,
    const DecodedFct1Stream&             Stream,
    const float                          MountAuthored[3],
    bool                                 MirrorLateral,
    float                                Scale,
    ControlVehiclePartCategory               PartCategory)
{
    if (!Stream.Valid) return;

    const std::array<uint32_t, 3u> CornerOrder = MirrorLateral ? std::array<uint32_t, 3u>{{ 0u, 2u, 1u }}
                                                               : std::array<uint32_t, 3u>{{ 0u, 1u, 2u }};
    const float LateralSign = MirrorLateral ? -1.0f : 1.0f;

    OutTriangles.reserve(OutTriangles.size() + Stream.FaceCount);
    for (uint32_t F = 0u; F < Stream.FaceCount; ++F)
    {
        ControlVehicleTriangle Tri {};
        Tri.MaterialSlotCode = Stream.SlotCodes[F];
        Tri.PartCategory         = PartCategory;

        float* OutPos[3]  = { Tri.PositionA, Tri.PositionB, Tri.PositionC };
        float* OutNorm[3] = { Tri.NormalA,   Tri.NormalB,   Tri.NormalC   };

        bool ValidTri = true;
        for (uint32_t OutC = 0u; OutC < 3u; ++OutC)
        {
            const uint32_t SrcC = CornerOrder[OutC];
            const uint32_t Vidx = Stream.Indices[F * 3u + SrcC];
            if (Vidx >= Stream.VertexCount)
            {
                ValidTri = false;
                break;
            }
            const float Px = MountAuthored[0] + Stream.Positions[Vidx * 3u + 0u] * Scale;
            const float Py = MountAuthored[1] + Stream.Positions[Vidx * 3u + 1u] * Scale * LateralSign;
            const float Pz = MountAuthored[2] + Stream.Positions[Vidx * 3u + 2u] * Scale;
            AuthoredToStudioPosition(Px, Py, Pz, OutPos[OutC]);

            const float Nx = Stream.CornerNormals[F * 9u + SrcC * 3u + 0u];
            const float Ny = Stream.CornerNormals[F * 9u + SrcC * 3u + 1u] * LateralSign;
            const float Nz = Stream.CornerNormals[F * 9u + SrcC * 3u + 2u];
            AuthoredToStudioNormal(Nx, Ny, Nz, OutNorm[OutC]);
        }
        if (!ValidTri) continue;

        const float E1x = Tri.PositionB[0] - Tri.PositionA[0];
        const float E1y = Tri.PositionB[1] - Tri.PositionA[1];
        const float E1z = Tri.PositionB[2] - Tri.PositionA[2];
        const float E2x = Tri.PositionC[0] - Tri.PositionA[0];
        const float E2y = Tri.PositionC[1] - Tri.PositionA[1];
        const float E2z = Tri.PositionC[2] - Tri.PositionA[2];
        const float Cx  = E1y * E2z - E1z * E2y;
        const float Cy  = E1z * E2x - E1x * E2z;
        const float Cz  = E1x * E2y - E1y * E2x;
        Tri.TriangleAreaM2 = 0.5f * std::sqrt(Cx * Cx + Cy * Cy + Cz * Cz);
        if (Tri.TriangleAreaM2 > 1e-8f)
        {
            OutTriangles.push_back(Tri);
        }
    }
}

uint32_t HashNextU32(uint32_t& SeedVar) noexcept
{
    SeedVar = SeedVar * 747796405u + 2891336453u;
    uint32_t Word = ((SeedVar >> ((SeedVar >> 28u) + 4u)) ^ SeedVar) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float NextUnitFloat(uint32_t& SeedVar) noexcept
{
    return static_cast<float>(HashNextU32(SeedVar) & 0x00FFFFFFu) / 16777216.0f;
}

uint64_t QuantizeSpatialCellKey(int32_t Ix, int32_t Iy, int32_t Iz) noexcept
{
    const uint64_t Ux = static_cast<uint64_t>(static_cast<uint32_t>(Ix + 32768));
    const uint64_t Uy = static_cast<uint64_t>(static_cast<uint32_t>(Iy + 32768));
    const uint64_t Uz = static_cast<uint64_t>(static_cast<uint32_t>(Iz + 32768));
    return (Ux << 40u) | (Uy << 20u) | Uz;
}

ControlVehicleGeometrySet ConstructControlVehicleGeometrySet()
{
    ControlVehicleGeometrySet Result {};

    const auto BodyStream = DecodeFct1BinaryFile(LocateVehicleBinaryFile("ControlVehicleBody.face"));
    const auto RimStream  = DecodeFct1BinaryFile(LocateVehicleBinaryFile("ControlVehicleRim.face"));
    const auto TyreStream = DecodeFct1BinaryFile(LocateVehicleBinaryFile("ControlVehicleTyre.face"));

    if (BodyStream.Valid)
    {
        constexpr float ZeroMount[3] = { 0.0f, 0.0f, 0.0f };
        AppendStreamInstance(Result.Triangles, BodyStream, ZeroMount, false, 1.0f, ControlVehiclePartCategory::BodyChassis);
        Result.BodyTriCount = static_cast<uint32_t>(Result.Triangles.size());

        for (const auto& Axle : VehicleAxleMounts)
        {
            if (RimStream.Valid)
            {
                AppendStreamInstance(Result.Triangles, RimStream, Axle.MountAuthored, Axle.MirrorLateral, RimToHubScale, Axle.RimCategory);
            }
            if (TyreStream.Valid)
            {
                AppendStreamInstance(Result.Triangles, TyreStream, Axle.MountAuthored, Axle.MirrorLateral, 1.0f, Axle.TyreCategory);
            }
        }
        Result.LoadedFromDisk = true;
    }

    if (!Result.Triangles.empty())
    {
        Result.BoundsMin[0] = Result.BoundsMin[1] = Result.BoundsMin[2] = std::numeric_limits<float>::max();
        Result.BoundsMax[0] = Result.BoundsMax[1] = Result.BoundsMax[2] = std::numeric_limits<float>::lowest();
        for (const auto& T : Result.Triangles)
        {
            const float* Corners[3] = { T.PositionA, T.PositionB, T.PositionC };
            for (const float* P : Corners)
            {
                for (int K = 0; K < 3; ++K)
                {
                    Result.BoundsMin[K] = std::min(Result.BoundsMin[K], P[K]);
                    Result.BoundsMax[K] = std::max(Result.BoundsMax[K], P[K]);
                }
            }
        }
        // Rest tyres cleanly on Studio floor Y = 0
        const float FloorDeltaY = -Result.BoundsMin[1];
        if (std::fabs(FloorDeltaY) > 1e-4f)
        {
            for (auto& T : Result.Triangles)
            {
                T.PositionA[1] += FloorDeltaY;
                T.PositionB[1] += FloorDeltaY;
                T.PositionC[1] += FloorDeltaY;
            }
            Result.BoundsMin[1] += FloorDeltaY;
            Result.BoundsMax[1] += FloorDeltaY;
        }
        for (int K = 0; K < 3; ++K)
        {
            Result.CenterMeters[K] = 0.5f * (Result.BoundsMin[K] + Result.BoundsMax[K]);
        }
        Result.HalfLengthM = 0.5f * (Result.BoundsMax[0] - Result.BoundsMin[0]);
    }

    return Result;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const ControlVehicleGeometrySet& QueryControlVehicleGeometrySet()
{
    static ControlVehicleGeometrySet CachedGeometry = ConstructControlVehicleGeometrySet();
    return CachedGeometry;
}

std::vector<ParticleAnchorSample> SampleControlVehicleAnchors(
    uint32_t SampleCount,
    uint32_t Seed,
    bool     IncludeWheels,
    bool     NormalizeToUnitSpan)
{
    std::vector<ParticleAnchorSample> Out;
    if (SampleCount == 0u) return Out;

    const auto& Vehicle = QueryControlVehicleGeometrySet();
    if (Vehicle.Triangles.empty())
    {
        // Graceful analytical fallback if binary assets are absent in an isolated working directory
        Out.resize(SampleCount);
        for (uint32_t I = 0u; I < SampleCount; ++I)
        {
            const float T = (static_cast<float>(I) + 0.5f) / static_cast<float>(SampleCount);
            const float Phi = static_cast<float>(I) * 2.3999632f;
            const float Y = 1.0f - 2.0f * T;
            const float R = std::sqrt(std::max(0.0f, 1.0f - Y * Y));
            Out[I].PositionMeters[0] = std::cos(Phi) * R * (NormalizeToUnitSpan ? 1.0f : 2.6f);
            Out[I].PositionMeters[1] = (Y * 0.45f + 0.5f) * (NormalizeToUnitSpan ? 0.8f : 1.5f);
            Out[I].PositionMeters[2] = std::sin(Phi) * R * (NormalizeToUnitSpan ? 0.42f : 1.1f);
            Out[I].NormalUnit[0]     = std::cos(Phi) * R;
            Out[I].NormalUnit[1]     = Y;
            Out[I].NormalUnit[2]     = std::sin(Phi) * R;
            Out[I].CurvatureWeight   = Y;
            Out[I].GroupIndex        = 6u;
        }
        return Out;
    }

    // Filter visible exterior-facing triangles (exclude deep hidden interior wheel backsides for cleaner kinetic tiling)
    std::vector<uint32_t> EligibleIndices;
    std::vector<float>    AreaCdf;
    EligibleIndices.reserve(Vehicle.Triangles.size());
    AreaCdf.reserve(Vehicle.Triangles.size());

    float TotalArea = 0.0f;
    for (uint32_t I = 0u; I < Vehicle.Triangles.size(); ++I)
    {
        const auto& T = Vehicle.Triangles[I];
        if (!IncludeWheels && T.PartCategory != ControlVehiclePartCategory::BodyChassis) continue;
        // Skip front grille/fascia, taillight bar, and license plates so those automotive details gleam cleanly!
        if (T.PartCategory == ControlVehiclePartCategory::BodyChassis &&
            (T.MaterialSlotCode == 12u || T.MaterialSlotCode == 13u || T.MaterialSlotCode == 14u || T.MaterialSlotCode == 15u))
        {
            continue;
        }
        float Weight = T.TriangleAreaM2;
        const float AvgNy = (T.NormalA[1] + T.NormalB[1] + T.NormalC[1]) / 3.0f;
        const float AvgY  = (T.PositionA[1] + T.PositionB[1] + T.PositionC[1]) / 3.0f;
        if (AvgNy < -0.22f && AvgY < 0.48f)
        {
            continue; // Exclude flat underside floor pan so 100% of anchors sculpt visible exterior panels
        }
        if (T.PartCategory != ControlVehiclePartCategory::BodyChassis)
        {
            Weight *= 0.32f;
        }
        if (Weight <= 1e-7f) continue;

        TotalArea += Weight;
        EligibleIndices.push_back(I);
        AreaCdf.push_back(TotalArea);
    }

    if (EligibleIndices.empty() || TotalArea <= 1e-7f) return Out;

    // Estimate blue-noise target spacing from total exterior surface area
    const float IdealSpacingM = std::sqrt(std::max(0.01f, TotalArea) / static_cast<float>(SampleCount)) * 0.86f;
    const float CellSizeM     = std::max(0.04f, IdealSpacingM);
    std::unordered_map<uint64_t, std::vector<uint32_t>> SpatialCells;
    SpatialCells.reserve(SampleCount * 2u);

    Out.reserve(SampleCount);
    uint32_t Rng = Seed ^ 0x9E3779B9u;

    const float MinY    = Vehicle.BoundsMin[1];
    const float SpanY   = std::max(0.25f, Vehicle.BoundsMax[1] - Vehicle.BoundsMin[1]);
    const float InvSpan = NormalizeToUnitSpan ? (1.0f / std::max(0.5f, Vehicle.HalfLengthM)) : 1.0f;

    auto PickSurfaceCandidate = [&]() -> ParticleAnchorSample {
        const float Pick = NextUnitFloat(Rng) * TotalArea;
        const auto It = std::lower_bound(AreaCdf.begin(), AreaCdf.end(), Pick);
        const std::size_t PosIdx = (It == AreaCdf.end()) ? (AreaCdf.size() - 1u) : static_cast<std::size_t>(It - AreaCdf.begin());
        const auto& Tri = Vehicle.Triangles[EligibleIndices[PosIdx]];

        const float R1 = std::sqrt(NextUnitFloat(Rng));
        const float R2 = NextUnitFloat(Rng);
        const float W0 = 1.0f - R1;
        const float W1 = R1 * (1.0f - R2);
        const float W2 = R1 * R2;

        ParticleAnchorSample Sample {};
        for (int K = 0; K < 3; ++K)
        {
            Sample.PositionMeters[K] = Tri.PositionA[K] * W0 + Tri.PositionB[K] * W1 + Tri.PositionC[K] * W2;
            Sample.NormalUnit[K]     = Tri.NormalA[K]   * W0 + Tri.NormalB[K]   * W1 + Tri.NormalC[K]   * W2;
        }
        const float NL = std::sqrt(Sample.NormalUnit[0] * Sample.NormalUnit[0] +
                                   Sample.NormalUnit[1] * Sample.NormalUnit[1] +
                                   Sample.NormalUnit[2] * Sample.NormalUnit[2]);
        if (NL > 1e-6f)
        {
            Sample.NormalUnit[0] /= NL;
            Sample.NormalUnit[1] /= NL;
            Sample.NormalUnit[2] /= NL;
        }
        else
        {
            Sample.NormalUnit[0] = 0.0f;
            Sample.NormalUnit[1] = 1.0f;
            Sample.NormalUnit[2] = 0.0f;
        }

        // Normalized elevation in [-1, 1] for auto halftone-to-raised-tile split across body panels
        const float Elev01 = std::clamp((Sample.PositionMeters[1] - MinY) / SpanY, 0.0f, 1.0f);
        Sample.CurvatureWeight = Elev01 * 2.0f - 1.0f;
        Sample.GroupIndex      = Tri.MaterialSlotCode;
        return Sample;
    };

    // Mitchell Best-Candidate blue-noise selection using 3x3x3 spatial cell neighborhood (fast O(N))
    constexpr uint32_t CandidateTries = 18u;
    for (uint32_t I = 0u; I < SampleCount; ++I)
    {
        ParticleAnchorSample BestCand = PickSurfaceCandidate();
        float BestMinDistSq = -1.0f;

        const uint32_t Tries = (I == 0u) ? 1u : CandidateTries;
        for (uint32_t TryIdx = 0u; TryIdx < Tries; ++TryIdx)
        {
            const ParticleAnchorSample Cand = (TryIdx == 0u) ? BestCand : PickSurfaceCandidate();
            const int32_t Cx = static_cast<int32_t>(std::floor(Cand.PositionMeters[0] / CellSizeM));
            const int32_t Cy = static_cast<int32_t>(std::floor(Cand.PositionMeters[1] / CellSizeM));
            const int32_t Cz = static_cast<int32_t>(std::floor(Cand.PositionMeters[2] / CellSizeM));

            float NearestSq = 1e9f;
            for (int32_t Dz = -1; Dz <= 1; ++Dz)
            {
                for (int32_t Dy = -1; Dy <= 1; ++Dy)
                {
                    for (int32_t Dx = -1; Dx <= 1; ++Dx)
                    {
                        const auto CellIt = SpatialCells.find(QuantizeSpatialCellKey(Cx + Dx, Cy + Dy, Cz + Dz));
                        if (CellIt == SpatialCells.end()) continue;
                        for (const uint32_t ExistingIdx : CellIt->second)
                        {
                            const auto& Ex = Out[ExistingIdx];
                            const float Ddx = Cand.PositionMeters[0] - Ex.PositionMeters[0];
                            const float Ddy = Cand.PositionMeters[1] - Ex.PositionMeters[1];
                            const float Ddz = Cand.PositionMeters[2] - Ex.PositionMeters[2];
                            const float Dsq = Ddx * Ddx + Ddy * Ddy + Ddz * Ddz;
                            if (Dsq < NearestSq) NearestSq = Dsq;
                        }
                    }
                }
            }
            if (NearestSq > BestMinDistSq)
            {
                BestMinDistSq = NearestSq;
                BestCand      = Cand;
                if (NearestSq >= IdealSpacingM * IdealSpacingM) break;
            }
        }

        const int32_t Bx = static_cast<int32_t>(std::floor(BestCand.PositionMeters[0] / CellSizeM));
        const int32_t By = static_cast<int32_t>(std::floor(BestCand.PositionMeters[1] / CellSizeM));
        const int32_t Bz = static_cast<int32_t>(std::floor(BestCand.PositionMeters[2] / CellSizeM));
        SpatialCells[QuantizeSpatialCellKey(Bx, By, Bz)].push_back(static_cast<uint32_t>(Out.size()));
        Out.push_back(BestCand);
    }

    if (NormalizeToUnitSpan)
    {
        for (auto& Sample : Out)
        {
            Sample.PositionMeters[0] = (Sample.PositionMeters[0] - Vehicle.CenterMeters[0]) * InvSpan;
            Sample.PositionMeters[1] = (Sample.PositionMeters[1] - Vehicle.CenterMeters[1]) * InvSpan;
            Sample.PositionMeters[2] = (Sample.PositionMeters[2] - Vehicle.CenterMeters[2]) * InvSpan;
        }
    }

    return Out;
}

} // namespace Frontier::ParticleSpace
