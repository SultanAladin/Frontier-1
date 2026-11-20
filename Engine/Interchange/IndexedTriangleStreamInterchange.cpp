/*============================================================================================================================================
                                                 INDEXEDTRIANGLESTREAMINTERCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of FCT1 binary stream decoding, Z-up -> Y-up axis conversion, and CourseMaterial slot mapping

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Interchange/IndexedTriangleStreamInterchange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <unordered_map>
#include <vector>

namespace Frontier::Interchange
{

namespace
{

constexpr std::array<CourseMaterialDescriptor, 10u> ControlVehiclePalette = {{
    {  6u, "BodyCoat",       { 0.680f, 0.800f, 0.000f }, 0.20f, 0.00f, { 0.000f, 0.000f, 0.000f } },
    {  7u, "VehicleGlass",   { 0.026f, 0.028f, 0.030f }, 0.05f, 0.00f, { 0.000f, 0.000f, 0.000f } },
    {  8u, "VehiclePlastic", { 0.012f, 0.014f, 0.018f }, 0.50f, 0.00f, { 0.000f, 0.000f, 0.000f } },
    {  9u, "Tyre",           { 0.037f, 0.037f, 0.037f }, 0.76f, 0.00f, { 0.000f, 0.000f, 0.000f } },
    { 10u, "Hub",            { 0.060f, 0.220f, 0.620f }, 0.22f, 1.00f, { 0.000f, 0.033f, 0.120f } },
    { 11u, "Brake",          { 0.300f, 0.075f, 0.025f }, 0.29f, 0.82f, { 0.000f, 0.000f, 0.000f } },
    { 12u, "VehicleTrim",    { 0.800f, 0.800f, 0.800f }, 0.50f, 0.00f, { 0.000f, 0.000f, 0.000f } },
    { 13u, "Headlight",      { 0.800f, 0.800f, 0.800f }, 0.10f, 0.00f, { 1.000f, 1.000f, 1.000f } },
    { 14u, "Taillight",      { 0.800f, 0.800f, 0.800f }, 0.10f, 0.00f, { 1.000f, 0.000f, 0.016f } },
    { 15u, "VehicleDefault", { 0.800f, 0.800f, 0.800f }, 0.50f, 0.00f, { 0.000f, 0.000f, 0.000f } },
}};

[[nodiscard]] uint32_t ReadUint32LE(const std::byte* Bytes) noexcept
{
    uint32_t Out = 0u;
    std::memcpy(&Out, Bytes, sizeof(Out));
    return Out;
}

[[nodiscard]] float ReadFloat32LE(const std::byte* Bytes) noexcept
{
    float Out = 0.0f;
    std::memcpy(&Out, Bytes, sizeof(Out));
    return Out;
}

} // namespace

const CourseMaterialDescriptor* IndexedTriangleStreamInterchange::InspectCourseMaterial(uint32_t SlotIdentifier) noexcept
{
    for (const CourseMaterialDescriptor& Desc : ControlVehiclePalette)
    {
        if (Desc.SlotIdentifier == SlotIdentifier)
        {
            return &Desc;
        }
    }
    return nullptr;
}

bool IndexedTriangleStreamInterchange::ImportFct1Bytes(
    std::span<const std::byte>             RawBytes,
    std::string_view                       DefaultTitle,
    bool                                   ConvertZUpToYUp,
    GeometrySpace::ConstructedSampleAsset& OutAsset,
    std::string&                           OutDiagnostic)
{
    OutDiagnostic.clear();
    OutAsset.Network.Reset();
    OutAsset.Layers = GeometrySpace::FacePartitionRegister{};
    OutAsset.UDIMTiles.clear();

    if (RawBytes.size() < 48u)
    {
        OutDiagnostic = "FCT1 stream shorter than 48-byte header.";
        return false;
    }

    if (static_cast<char>(RawBytes[0]) != 'F' ||
        static_cast<char>(RawBytes[1]) != 'C' ||
        static_cast<char>(RawBytes[2]) != 'T' ||
        static_cast<char>(RawBytes[3]) != '1')
    {
        OutDiagnostic = "Invalid stream signature; expected FCT1.";
        return false;
    }

    const uint32_t VertexCount   = ReadUint32LE(RawBytes.data() + 4u);
    const uint32_t TriangleCount = ReadUint32LE(RawBytes.data() + 8u);
    const uint32_t StreamMask    = ReadUint32LE(RawBytes.data() + 12u);

    const std::size_t PositionsBytes = static_cast<std::size_t>(VertexCount) * 12u;
    const std::size_t IndicesBytes   = static_cast<std::size_t>(TriangleCount) * 12u;
    const std::size_t SlotsBytes     = static_cast<std::size_t>(TriangleCount) * 4u;
    const std::size_t ExtraBytes     = ((StreamMask & 1u) != 0u) ? static_cast<std::size_t>(TriangleCount) * 36u : 0u;
    const std::size_t ExpectedSize   = 48u + PositionsBytes + IndicesBytes + SlotsBytes + ExtraBytes;

    if (RawBytes.size() != ExpectedSize)
    {
        OutDiagnostic = "FCT1 byte length disagrees with header counts.";
        return false;
    }

    if (VertexCount == 0u || TriangleCount == 0u)
    {
        OutDiagnostic = "FCT1 stream contains zero geometry.";
        return false;
    }

    OutAsset.Network.NetworkTitle = DefaultTitle.empty() ? std::string("FCT1Asset") : std::string(DefaultTitle);
    OutAsset.Network.ReserveStorage(VertexCount, TriangleCount * 3u, TriangleCount);

    const std::byte* PosPtr   = RawBytes.data() + 48u;
    const std::byte* IndexPtr = PosPtr + PositionsBytes;
    const std::byte* SlotPtr  = IndexPtr + IndicesBytes;

    for (uint32_t V = 0u; V < VertexCount; ++V)
    {
        const float X = ReadFloat32LE(PosPtr + static_cast<std::size_t>(V) * 12u + 0u);
        const float Y = ReadFloat32LE(PosPtr + static_cast<std::size_t>(V) * 12u + 4u);
        const float Z = ReadFloat32LE(PosPtr + static_cast<std::size_t>(V) * 12u + 8u);
        if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z))
        {
            OutDiagnostic = "FCT1 stream contains non-finite vertex coordinates.";
            return false;
        }
        const Mathematics::Axis3D Converted = ConvertZUpToYUp
            ? Mathematics::Axis3D{ X, Z, -Y }
            : Mathematics::Axis3D{ X, Y, Z };
        (void)OutAsset.Network.AppendVertex(Converted);
    }

    std::unordered_map<uint32_t, uint16_t> SlotLayerOrdinal {};
    const std::array<Mathematics::UVAxes, 3u> DefaultTriangleUVs {{
        { 0.10f, 0.10f },
        { 0.90f, 0.10f },
        { 0.50f, 0.90f },
    }};

    for (uint32_t T = 0u; T < TriangleCount; ++T)
    {
        const uint32_t A = ReadUint32LE(IndexPtr + static_cast<std::size_t>(T) * 12u + 0u);
        const uint32_t B = ReadUint32LE(IndexPtr + static_cast<std::size_t>(T) * 12u + 4u);
        const uint32_t C = ReadUint32LE(IndexPtr + static_cast<std::size_t>(T) * 12u + 8u);
        if (A >= VertexCount || B >= VertexCount || C >= VertexCount || A == B || B == C || A == C)
        {
            continue;
        }

        const uint32_t SlotId = ReadUint32LE(SlotPtr + static_cast<std::size_t>(T) * 4u);
        const CourseMaterialDescriptor* Mat = InspectCourseMaterial(SlotId);

        GeometrySpace::FaceShadingAssignment Shading {};
        if (Mat != nullptr)
        {
            Shading.AlbedoLinearRGB     = Mat->AlbedoLinearRGB;
            Shading.MatCapTintLinearRGB = Mat->AlbedoLinearRGB;
            Shading.Metallic            = Mat->Metallic;
            Shading.Roughness           = Mat->Roughness;
            Shading.Specular            = 0.5f;
            Shading.MatCapBlend         = 0.0f;
            Shading.MatCapSlot          = static_cast<uint8_t>(SlotId & 0x0Fu);
        }

        auto LayerIt = SlotLayerOrdinal.find(SlotId);
        if (LayerIt == SlotLayerOrdinal.end())
        {
            const std::string_view LayerTitle = (Mat != nullptr) ? Mat->Title : std::string_view("CourseSlot");
            const uint16_t CreatedOrdinal = OutAsset.Layers.ConstructLayer(
                LayerTitle,
                Shading.AlbedoLinearRGB,
                10.0f,
                1001u);
            LayerIt = SlotLayerOrdinal.emplace(SlotId, CreatedOrdinal).first;
        }

        const std::array<uint32_t, 3u> TriLoop { A, B, C };
        (void)OutAsset.Network.AppendFace(
            TriLoop,
            DefaultTriangleUVs,
            Shading,
            LayerIt->second,
            static_cast<uint16_t>(SlotId));
    }

    if (OutAsset.Network.Faces.empty())
    {
        OutDiagnostic = "FCT1 stream produced zero valid triangles.";
        return false;
    }

    OutAsset.Network.LinkOppositeEdges(false);
    OutAsset.Network.MarkDihedralSharpSeams(40.0f, false);
    OutAsset.Network.RecomputeNormals();
    OutAsset.UDIMTiles = { 1001u };
    OutAsset.Layers.SynchronizePartitions(OutAsset.Network, GeometrySpace::GeometryInstanceToken{ 0u, 1u });
    return true;
}

bool IndexedTriangleStreamInterchange::ImportFct1File(
    const std::filesystem::path&           SourcePath,
    bool                                   ConvertZUpToYUp,
    GeometrySpace::ConstructedSampleAsset& OutAsset,
    std::string&                           OutDiagnostic)
{
    std::ifstream Stream(SourcePath, std::ios::binary | std::ios::ate);
    if (!Stream)
    {
        OutDiagnostic = "Unable to open FCT1 file: " + SourcePath.string();
        return false;
    }

    const std::streamsize FileSize = Stream.tellg();
    if (FileSize < 48)
    {
        OutDiagnostic = "FCT1 file is empty or truncated: " + SourcePath.string();
        return false;
    }

    std::vector<std::byte> Bytes(static_cast<std::size_t>(FileSize));
    Stream.seekg(0, std::ios::beg);
    if (!Stream.read(reinterpret_cast<char*>(Bytes.data()), FileSize))
    {
        OutDiagnostic = "Failed reading FCT1 file bytes: " + SourcePath.string();
        return false;
    }

    return ImportFct1Bytes(Bytes, SourcePath.stem().string(), ConvertZUpToYUp, OutAsset, OutDiagnostic);
}

} // namespace Frontier::Interchange
