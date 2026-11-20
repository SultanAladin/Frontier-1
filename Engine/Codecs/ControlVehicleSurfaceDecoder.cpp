/*============================================================================================================================================
                                                     CONTROLVEHICLESURFACEDECODER.CPP
============================================================================================================================================*/
// 🧩 FCT1 binary face binary decoder assembling the 25,068-triangle Controlled Integrator vehicle (Body + 4 Rims + 4 Tyres)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace Frontier::Codecs
{

namespace
{

constexpr float    VehicleHeightRebase = 0.11285f;             // [m] - Authored Z of body origin above axle plane
constexpr float    VehicleGroundLift   = 0.53000f;             // [m] - Places 0.509 m tyre contact on ground plane
constexpr float    RimWheelScale       = 0.34460f / 0.344818f; // [-] - Rim binary radius -> wheel socket radius
constexpr uint32_t CornerNormalsBit    = 1u;                   // [-] - FCT1 header bit 0
constexpr uint32_t Fct1HeaderByteCount = 48u;                  // [B] - FCT1 binary header length

struct WheelAxleMount
{
    float                  AuthoredMount[3];
    bool                   MirrorLateral;
    VehicleSubassemblyPart RimPart;
    VehicleSubassemblyPart TyrePart;
};

constexpr std::array<WheelAxleMount, 4u> CanonicalAxleMounts {{
    { {  1.7274f,  1.0475f, 0.0914f }, false, VehicleSubassemblyPart::RimFrontLeft,  VehicleSubassemblyPart::TyreFrontLeft  },
    { {  1.7274f, -1.0475f, 0.0914f }, true,  VehicleSubassemblyPart::RimFrontRight, VehicleSubassemblyPart::TyreFrontRight },
    { { -1.6686f,  1.0475f, 0.0914f }, false, VehicleSubassemblyPart::RimRearLeft,   VehicleSubassemblyPart::TyreRearLeft   },
    { { -1.6686f, -1.0475f, 0.0914f }, true,  VehicleSubassemblyPart::RimRearRight,  VehicleSubassemblyPart::TyreRearRight  },
}};

[[nodiscard]] uint32_t ReadLeUint32(const uint8_t* Ptr) noexcept
{
    uint32_t Word = 0u;
    std::memcpy(&Word, Ptr, sizeof(Word));
    return Word;
}

[[nodiscard]] float ReadLeFloat32(const uint8_t* Ptr) noexcept
{
    float Scalar = 0.0f;
    std::memcpy(&Scalar, Ptr, sizeof(Scalar));
    return Scalar;
}

[[nodiscard]] VehicleSurfaceSlot ResolveBodyPartSlot(uint32_t PartGroupCode) noexcept
{
    switch (PartGroupCode)
    {
        case 6u:  return VehicleSurfaceSlot::BodyCoat;
        case 7u:  return VehicleSurfaceSlot::VehicleGlass;
        case 8u:  return VehicleSurfaceSlot::VehiclePlastic;
        case 9u:  return VehicleSurfaceSlot::CanopyGloss; // Body binary part group 9 = gloss roof/pillar canopy
        case 10u: return VehicleSurfaceSlot::WheelCapAccent;
        case 11u: return VehicleSurfaceSlot::BrakeDisc;
        case 12u: return VehicleSurfaceSlot::VehicleTrim;
        case 13u: return VehicleSurfaceSlot::HeadlightLens;
        case 14u: return VehicleSurfaceSlot::TaillightLens;
        default:  return VehicleSurfaceSlot::VehicleTrim;
    }
}

[[nodiscard]] Mathematics::Axis3D ConvertAuthoredPosition(
    float                       AuthX,
    float                       AuthYLeft,
    float                       AuthZUp,
    VehicleCoordinateConvention Convention) noexcept
{
    const float LiftedZ = AuthZUp - VehicleHeightRebase + VehicleGroundLift;
    if (Convention == VehicleCoordinateConvention::StudioPathYUp)
    {
        return { AuthX, LiftedZ, -AuthYLeft };
    }
    return { AuthX, AuthYLeft, LiftedZ };
}

[[nodiscard]] Mathematics::Axis3D ConvertAuthoredNormal(
    float                       Nx,
    float                       NyLeft,
    float                       NzUp,
    VehicleCoordinateConvention Convention) noexcept
{
    if (Convention == VehicleCoordinateConvention::StudioPathYUp)
    {
        return Mathematics::NormalizeAxis({ Nx, NzUp, -NyLeft }, { 0.0f, 1.0f, 0.0f });
    }
    return Mathematics::NormalizeAxis({ Nx, NyLeft, NzUp }, { 0.0f, 0.0f, 1.0f });
}

[[nodiscard]] std::filesystem::path LocateFaceBinaryOnDisk(
    const std::filesystem::path& RepositoryRoot,
    const char*                  StemName)
{
    const std::array<std::string, 2u> FileCandidates {{
        std::string(StemName) + ".face",
        std::string(StemName) + ".face",
    }};
    const std::array<std::filesystem::path, 6u> Directories {{
        RepositoryRoot / "EngineContent/EnvironmentArchives/ControlVehicle",
        RepositoryRoot / "Experimental/DeterministicPathTracer/Assets",
        RepositoryRoot / "Experimental/Shared/Shapes",
        std::filesystem::path("EngineContent/EnvironmentArchives/ControlVehicle"),
        std::filesystem::path("Experimental/DeterministicPathTracer/Assets"),
        std::filesystem::path("../EngineContent/EnvironmentArchives/ControlVehicle"),
    }};

    for (const auto& Dir : Directories)
    {
        for (const auto& FileName : FileCandidates)
        {
            const std::filesystem::path Candidate = Dir / FileName;
            std::error_code FsErr {};
            if (std::filesystem::exists(Candidate, FsErr) && !FsErr)
            {
                return Candidate;
            }
        }
    }
    return {};
}

void AppendSubassemblyInstance(
    std::vector<DecodedVehicleTriangle>& OutTriangles,
    const DecodedFaceStream&              Stream,
    const float                            Mount[3],
    bool                                   MirrorLateral,
    float                                  UniformScale,
    VehicleSubassemblyPart                 Part,
    VehicleCoordinateConvention            Convention)
{
    const std::array<uint32_t, 3u> CornerOrder = MirrorLateral
        ? std::array<uint32_t, 3u>{{ 0u, 2u, 1u }}
        : std::array<uint32_t, 3u>{{ 0u, 1u, 2u }};
    const float LateralSign = MirrorLateral ? -1.0f : 1.0f;

    OutTriangles.reserve(OutTriangles.size() + Stream.FaceCount);
    for (uint32_t FaceIdx = 0u; FaceIdx < Stream.FaceCount; ++FaceIdx)
    {
        const uint32_t I0 = Stream.Indices[FaceIdx * 3u + CornerOrder[0]];
        const uint32_t I1 = Stream.Indices[FaceIdx * 3u + CornerOrder[1]];
        const uint32_t I2 = Stream.Indices[FaceIdx * 3u + CornerOrder[2]];
        if (I0 >= Stream.VertexCount || I1 >= Stream.VertexCount || I2 >= Stream.VertexCount)
        {
            continue;
        }

        const auto ReadPos = [&](uint32_t VertexIdx) noexcept -> Mathematics::Axis3D
        {
            const float Px = Mount[0] + Stream.Positions[VertexIdx * 3u + 0u] * UniformScale;
            const float Py = Mount[1] + Stream.Positions[VertexIdx * 3u + 1u] * UniformScale * LateralSign;
            const float Pz = Mount[2] + Stream.Positions[VertexIdx * 3u + 2u] * UniformScale;
            return ConvertAuthoredPosition(Px, Py, Pz, Convention);
        };

        const auto ReadNrm = [&](uint32_t SrcCorner) noexcept -> Mathematics::Axis3D
        {
            const std::size_t Origin = static_cast<std::size_t>(FaceIdx) * 9u + static_cast<std::size_t>(SrcCorner) * 3u;
            const float Nx = Stream.CornerNormals[Origin + 0u];
            const float Ny = Stream.CornerNormals[Origin + 1u] * LateralSign;
            const float Nz = Stream.CornerNormals[Origin + 2u];
            return ConvertAuthoredNormal(Nx, Ny, Nz, Convention);
        };

        DecodedVehicleTriangle Tri {};
        Tri.CornerA    = ReadPos(I0);
        Tri.CornerB    = ReadPos(I1);
        Tri.CornerC    = ReadPos(I2);
        Tri.NormalA    = ReadNrm(CornerOrder[0]);
        Tri.NormalB    = ReadNrm(CornerOrder[1]);
        Tri.NormalC    = ReadNrm(CornerOrder[2]);
        Tri.PartGroupCode = Stream.PartGroupCodes[FaceIdx];
        Tri.Part       = Part;

        if (Part == VehicleSubassemblyPart::BodyChassis)
        {
            Tri.SurfaceSlot = ResolveBodyPartSlot(Tri.PartGroupCode);
        }
        else if (Part == VehicleSubassemblyPart::RimFrontLeft  ||
                 Part == VehicleSubassemblyPart::RimFrontRight ||
                 Part == VehicleSubassemblyPart::RimRearLeft   ||
                 Part == VehicleSubassemblyPart::RimRearRight)
        {
            Tri.SurfaceSlot = (Tri.PartGroupCode == 10u)
                ? VehicleSurfaceSlot::WheelCapAccent
                : VehicleSurfaceSlot::RimAlloy;
        }
        else
        {
            Tri.SurfaceSlot = VehicleSurfaceSlot::TyreRubber;
        }

        const Mathematics::Axis3D EdgeAB = Tri.CornerB - Tri.CornerA;
        const Mathematics::Axis3D EdgeAC = Tri.CornerC - Tri.CornerA;
        const Mathematics::Axis3D CrossN = Mathematics::EvaluateCross(EdgeAB, EdgeAC);
        Tri.AreaMeters2 = 0.5f * std::sqrt(Mathematics::EvaluateLengthSquared(CrossN));
        OutTriangles.push_back(Tri);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

bool ControlVehicleSurfaceDecoder::DecodeFaceBytes(
    std::span<const uint8_t> PayloadBytes,
    DecodedFaceStream&      OutStream,
    std::string&             OutDiagnostic,
    float                    CreaseAngleDegrees)
{
    OutStream = {};
    OutDiagnostic.clear();

    if (PayloadBytes.size() < Fct1HeaderByteCount)
    {
        OutDiagnostic = "FCT1 binary shorter than 48-byte header";
        return false;
    }
    if (PayloadBytes[0] != 'F' || PayloadBytes[1] != 'C' || PayloadBytes[2] != 'T' || PayloadBytes[3] != '1')
    {
        OutDiagnostic = "Payload magic is not FCT1";
        return false;
    }

    OutStream.VertexCount     = ReadLeUint32(PayloadBytes.data() + 4u);
    OutStream.FaceCount      = ReadLeUint32(PayloadBytes.data() + 8u);
    const uint32_t HeaderMask = ReadLeUint32(PayloadBytes.data() + 12u);

    const std::size_t PosBytes  = static_cast<std::size_t>(OutStream.VertexCount) * 12u;
    const std::size_t IdxBytes  = static_cast<std::size_t>(OutStream.FaceCount) * 12u;
    const std::size_t FamBytes  = static_cast<std::size_t>(OutStream.FaceCount) * 4u;
    const bool        CornerAssigned = (HeaderMask & CornerNormalsBit) != 0u;
    const std::size_t NrmBytes  = CornerAssigned ? (static_cast<std::size_t>(OutStream.FaceCount) * 36u) : 0u;
    const std::size_t Expected  = Fct1HeaderByteCount + PosBytes + IdxBytes + FamBytes + NrmBytes;

    if (PayloadBytes.size() != Expected)
    {
        OutDiagnostic = "FCT1 binary byte length mismatch";
        return false;
    }

    std::size_t Offset = Fct1HeaderByteCount;
    OutStream.Positions.resize(static_cast<std::size_t>(OutStream.VertexCount) * 3u);
    for (std::size_t I = 0u; I < OutStream.Positions.size(); ++I)
    {
        OutStream.Positions[I] = ReadLeFloat32(PayloadBytes.data() + Offset + I * 4u);
    }
    Offset += PosBytes;

    OutStream.Indices.resize(static_cast<std::size_t>(OutStream.FaceCount) * 3u);
    for (std::size_t I = 0u; I < OutStream.Indices.size(); ++I)
    {
        OutStream.Indices[I] = ReadLeUint32(PayloadBytes.data() + Offset + I * 4u);
    }
    Offset += IdxBytes;

    OutStream.PartGroupCodes.resize(OutStream.FaceCount);
    for (std::size_t I = 0u; I < OutStream.PartGroupCodes.size(); ++I)
    {
        OutStream.PartGroupCodes[I] = ReadLeUint32(PayloadBytes.data() + Offset + I * 4u);
    }
    Offset += FamBytes;

    if (CornerAssigned)
    {
        OutStream.CornerNormals.resize(static_cast<std::size_t>(OutStream.FaceCount) * 9u);
        for (std::size_t I = 0u; I < OutStream.CornerNormals.size(); ++I)
        {
            OutStream.CornerNormals[I] = ReadLeFloat32(PayloadBytes.data() + Offset + I * 4u);
        }
    }
    else
    {
        const float CreaseCosine = std::cos(CreaseAngleDegrees * Constants::DegToRadFloat);
        std::vector<Mathematics::Axis3D>   AreaWeightedNormals(OutStream.FaceCount);
        std::vector<std::vector<uint32_t>> VertexIncidence(OutStream.VertexCount);

        for (uint32_t F = 0u; F < OutStream.FaceCount; ++F)
        {
            const uint32_t Ia = OutStream.Indices[F * 3u + 0u];
            const uint32_t Ib = OutStream.Indices[F * 3u + 1u];
            const uint32_t Ic = OutStream.Indices[F * 3u + 2u];
            if (Ia >= OutStream.VertexCount || Ib >= OutStream.VertexCount || Ic >= OutStream.VertexCount)
            {
                continue;
            }
            const Mathematics::Axis3D Pa { OutStream.Positions[Ia * 3u + 0u], OutStream.Positions[Ia * 3u + 1u], OutStream.Positions[Ia * 3u + 2u] };
            const Mathematics::Axis3D Pb { OutStream.Positions[Ib * 3u + 0u], OutStream.Positions[Ib * 3u + 1u], OutStream.Positions[Ib * 3u + 2u] };
            const Mathematics::Axis3D Pc { OutStream.Positions[Ic * 3u + 0u], OutStream.Positions[Ic * 3u + 1u], OutStream.Positions[Ic * 3u + 2u] };
            AreaWeightedNormals[F] = Mathematics::EvaluateCross(Pb - Pa, Pc - Pa);
            VertexIncidence[Ia].push_back(F);
            VertexIncidence[Ib].push_back(F);
            VertexIncidence[Ic].push_back(F);
        }

        OutStream.CornerNormals.assign(static_cast<std::size_t>(OutStream.FaceCount) * 9u, 0.0f);
        for (uint32_t F = 0u; F < OutStream.FaceCount; ++F)
        {
            const Mathematics::Axis3D Fn  = AreaWeightedNormals[F];
            const float               FLen = std::sqrt(Mathematics::EvaluateLengthSquared(Fn));
            const float               InvF = (FLen > 1.0e-10f) ? (1.0f / FLen) : 1.0f;

            for (uint32_t C = 0u; C < 3u; ++C)
            {
                const uint32_t Vidx = OutStream.Indices[F * 3u + C];
                Mathematics::Axis3D Sum { 0.0f, 0.0f, 0.0f };
                if (Vidx < OutStream.VertexCount)
                {
                    for (uint32_t Adj : VertexIncidence[Vidx])
                    {
                        const Mathematics::Axis3D An   = AreaWeightedNormals[Adj];
                        const float               ALen = std::sqrt(Mathematics::EvaluateLengthSquared(An));
                        if (ALen <= 1.0e-10f)
                        {
                            continue;
                        }
                        const float CosAngle = Mathematics::EvaluateDot(Fn, An) * InvF / ALen;
                        if (CosAngle >= CreaseCosine)
                        {
                            Sum = Sum + An;
                        }
                    }
                }
                const Mathematics::Axis3D Unit = Mathematics::NormalizeAxis(Sum, { 0.0f, 0.0f, 1.0f });
                const std::size_t OutOrigin = static_cast<std::size_t>(F) * 9u + static_cast<std::size_t>(C) * 3u;
                OutStream.CornerNormals[OutOrigin + 0u] = Unit.X;
                OutStream.CornerNormals[OutOrigin + 1u] = Unit.Y;
                OutStream.CornerNormals[OutOrigin + 2u] = Unit.Z;
            }
        }
    }

    return true;
}

bool ControlVehicleSurfaceDecoder::DecodeFaceFile(
    const std::filesystem::path& FilePath,
    DecodedFaceStream&          OutStream,
    std::string&                 OutDiagnostic,
    float                        CreaseAngleDegrees)
{
    std::ifstream Input(FilePath, std::ios::binary | std::ios::ate);
    if (!Input.is_open())
    {
        OutDiagnostic = "Unable to open FCT1 binary: " + FilePath.generic_string();
        return false;
    }
    const std::streamsize Size = Input.tellg();
    if (Size <= 0)
    {
        OutDiagnostic = "Empty FCT1 binary: " + FilePath.generic_string();
        return false;
    }
    std::vector<uint8_t> Bytes(static_cast<std::size_t>(Size));
    Input.seekg(0, std::ios::beg);
    if (!Input.read(reinterpret_cast<char*>(Bytes.data()), Size))
    {
        OutDiagnostic = "Failed reading FCT1 binary: " + FilePath.generic_string();
        return false;
    }
    return DecodeFaceBytes(Bytes, OutStream, OutDiagnostic, CreaseAngleDegrees);
}

bool ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
    const std::filesystem::path& RepositoryRoot,
    VehicleCoordinateConvention  Convention,
    AssembledControlVehicle&     OutVehicle,
    std::string&                 OutDiagnostic)
{
    OutVehicle            = {};
    OutVehicle.Convention = Convention;
    OutDiagnostic.clear();

    const std::filesystem::path BodyPath = LocateFaceBinaryOnDisk(RepositoryRoot, "ControlVehicleBody");
    const std::filesystem::path RimPath  = LocateFaceBinaryOnDisk(RepositoryRoot, "ControlVehicleRim");
    const std::filesystem::path TyrePath = LocateFaceBinaryOnDisk(RepositoryRoot, "ControlVehicleTyre");

    if (BodyPath.empty() || RimPath.empty() || TyrePath.empty())
    {
        OutDiagnostic = "ControlVehicle FCT1 binaries not found under EngineContent or Experimental";
        return false;
    }

    DecodedFaceStream BodyStream {};
    DecodedFaceStream RimStream  {};
    DecodedFaceStream TyreStream {};
    if (!DecodeFaceFile(BodyPath, BodyStream, OutDiagnostic, 38.0f) ||
        !DecodeFaceFile(RimPath,  RimStream,  OutDiagnostic, 38.0f) ||
        !DecodeFaceFile(TyrePath, TyreStream, OutDiagnostic, 38.0f))
    {
        return false;
    }

    constexpr float ZeroMount[3] { 0.0f, 0.0f, 0.0f };
    AppendSubassemblyInstance(
        OutVehicle.Triangles,
        BodyStream,
        ZeroMount,
        false,
        1.0f,
        VehicleSubassemblyPart::BodyChassis,
        Convention);
    OutVehicle.BodyTriangleCount = static_cast<uint32_t>(OutVehicle.Triangles.size());

    for (const WheelAxleMount& Mount : CanonicalAxleMounts)
    {
        const std::size_t BeforeTyre = OutVehicle.Triangles.size();
        AppendSubassemblyInstance(
            OutVehicle.Triangles,
            TyreStream,
            Mount.AuthoredMount,
            Mount.MirrorLateral,
            1.0f,
            Mount.TyrePart,
            Convention);
        OutVehicle.TyreTriangleCount += static_cast<uint32_t>(OutVehicle.Triangles.size() - BeforeTyre);

        const std::size_t BeforeRim = OutVehicle.Triangles.size();
        AppendSubassemblyInstance(
            OutVehicle.Triangles,
            RimStream,
            Mount.AuthoredMount,
            Mount.MirrorLateral,
            RimWheelScale,
            Mount.RimPart,
            Convention);
        OutVehicle.RimTriangleCount += static_cast<uint32_t>(OutVehicle.Triangles.size() - BeforeRim);
    }

    if (OutVehicle.Triangles.empty())
    {
        OutDiagnostic = "Assembled ControlVehicle produced zero triangles";
        return false;
    }

    Mathematics::Axis3D MinB {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    Mathematics::Axis3D MaxB {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };

    const auto AccumulateCorner = [&](Mathematics::Axis3D P) noexcept
    {
        MinB.X = std::min(MinB.X, P.X);
        MinB.Y = std::min(MinB.Y, P.Y);
        MinB.Z = std::min(MinB.Z, P.Z);
        MaxB.X = std::max(MaxB.X, P.X);
        MaxB.Y = std::max(MaxB.Y, P.Y);
        MaxB.Z = std::max(MaxB.Z, P.Z);
    };

    for (const auto& Tri : OutVehicle.Triangles)
    {
        AccumulateCorner(Tri.CornerA);
        AccumulateCorner(Tri.CornerB);
        AccumulateCorner(Tri.CornerC);
    }

    if (Convention == VehicleCoordinateConvention::FrontierWorldZUp)
    {
        const float LiftZ = -MinB.Z;
        if (std::fabs(LiftZ) > 1.0e-5f)
        {
            for (auto& Tri : OutVehicle.Triangles)
            {
                Tri.CornerA.Z += LiftZ;
                Tri.CornerB.Z += LiftZ;
                Tri.CornerC.Z += LiftZ;
            }
            MinB.Z += LiftZ;
            MaxB.Z += LiftZ;
        }
    }

    OutVehicle.BoundsMin    = MinB;
    OutVehicle.BoundsMax    = MaxB;
    OutVehicle.BoundsCenter = (MinB + MaxB) * 0.5f;
    return true;
}

GeometrySpace::PolygonNetwork ControlVehicleSurfaceDecoder::ConstructPolygonNetworkFromSlot(
    const AssembledControlVehicle& Vehicle,
    VehicleSurfaceSlot     TargetSlot)
{
    GeometrySpace::PolygonNetwork Network {};
    uint32_t MatchingFaces = 0u;
    for (const auto& Tri : Vehicle.Triangles)
    {
        if (Tri.SurfaceSlot == TargetSlot)
        {
            ++MatchingFaces;
        }
    }
    if (MatchingFaces == 0u)
    {
        return Network;
    }

    Network.ReserveStorage(MatchingFaces * 3u, MatchingFaces * 3u, MatchingFaces);
    for (const auto& Tri : Vehicle.Triangles)
    {
        if (Tri.SurfaceSlot != TargetSlot)
        {
            continue;
        }
        const uint32_t Va = Network.AppendVertex(Tri.CornerA);
        const uint32_t Vb = Network.AppendVertex(Tri.CornerB);
        const uint32_t Vc = Network.AppendVertex(Tri.CornerC);
        Network.Vertices[Va].Normal = Tri.NormalA;
        Network.Vertices[Vb].Normal = Tri.NormalB;
        Network.Vertices[Vc].Normal = Tri.NormalC;

        const std::array<uint32_t, 3u> Loop {{ Va, Vb, Vc }};
        (void)Network.AppendFace(Loop);
    }
    return Network;
}

} // namespace Frontier::Codecs
