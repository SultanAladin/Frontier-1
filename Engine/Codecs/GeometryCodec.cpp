/*============================================================================================================================================
                                                              GEOMETRYCODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of .GeometrySpace Two-Tier Hybrid encoder & decoder (full n-gon PolygonNetwork + legacy VehiclePolygon support)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/GeometryCodec.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace Frontier::Codecs
{

namespace
{

constexpr uint32_t NativePolygonSectionMagic   = 0x314E4750u; // 'PGN1'
constexpr uint32_t NativePolygonSectionVersion = 1u;

template <typename PodType>
void AppendPod(std::vector<std::byte>& OutBytes, const PodType& SourceEntry)
{
    const std::size_t Offset = OutBytes.size();
    OutBytes.resize(Offset + sizeof(PodType));
    std::memcpy(OutBytes.data() + Offset, &SourceEntry, sizeof(PodType));
}

template <typename PodType>
bool ReadPod(std::span<const std::byte>& Cursor, PodType& OutEntry) noexcept
{
    if (Cursor.size() < sizeof(PodType))
    {
        return false;
    }
    std::memcpy(&OutEntry, Cursor.data(), sizeof(PodType));
    Cursor = Cursor.subspan(sizeof(PodType));
    return true;
}

[[nodiscard]] std::vector<std::byte> EncodePolygonNetworkBinary(const GeometrySpace::PolygonNetwork& Network)
{
    std::vector<std::byte> Bytes;
    Bytes.reserve(32u + Network.Vertices.size() * 12u + Network.Faces.size() * 48u + Network.Edges.size() * 16u);

    AppendPod(Bytes, NativePolygonSectionMagic);
    AppendPod(Bytes, NativePolygonSectionVersion);
    const auto VertCount = static_cast<uint32_t>(Network.Vertices.size());
    const auto FaceCount = static_cast<uint32_t>(Network.Faces.size());
    AppendPod(Bytes, VertCount);
    AppendPod(Bytes, FaceCount);
    AppendPod(Bytes, Network.NetworkRevision);
    AppendPod(Bytes, Network.UVRevision);
    AppendPod(Bytes, Network.ShadingRevision);

    for (const GeometrySpace::Vertex& Vert : Network.Vertices)
    {
        AppendPod(Bytes, Vert.Axes);
    }

    for (const GeometrySpace::Face& CurrentFace : Network.Faces)
    {
        AppendPod(Bytes, CurrentFace.CornerCount);
        AppendPod(Bytes, CurrentFace.PriorityLayerOrdinal);
        AppendPod(Bytes, CurrentFace.PartGroupOrdinal);
        AppendPod(Bytes, CurrentFace.Shading.AlbedoLinearRGB);
        AppendPod(Bytes, CurrentFace.Shading.Metallic);
        AppendPod(Bytes, CurrentFace.Shading.Roughness);
        AppendPod(Bytes, CurrentFace.Shading.Specular);
        AppendPod(Bytes, CurrentFace.Shading.MatCapBlend);
        AppendPod(Bytes, CurrentFace.Shading.MatCapSlot);

        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount; ++Step)
        {
            const GeometrySpace::DirectedEdge& Edge = Network.Edges[Walk];
            AppendPod(Bytes, Edge.OriginVertexIndex);
            AppendPod(Bytes, Edge.ParameterUV);
            const auto MaskByte = static_cast<uint8_t>(Edge.AttributeMask);
            AppendPod(Bytes, MaskByte);
            Walk = Edge.NextEdgeIndex;
        }
    }

    return Bytes;
}

[[nodiscard]] bool DecodePolygonNetworkBinary(
    std::span<const std::byte>     RawBytes,
    const std::string&             Title,
    GeometrySpace::PolygonNetwork& OutNetwork)
{
    std::span<const std::byte> Cursor = RawBytes;
    uint32_t Magic = 0u, Version = 0u, VertCount = 0u, FaceCount = 0u;
    uint64_t NetRev = 1ull, UVRev = 1ull, ShadeRev = 1ull;
    if (!ReadPod(Cursor, Magic) || !ReadPod(Cursor, Version) ||
        Magic != NativePolygonSectionMagic || Version != NativePolygonSectionVersion ||
        !ReadPod(Cursor, VertCount) || !ReadPod(Cursor, FaceCount) ||
        !ReadPod(Cursor, NetRev) || !ReadPod(Cursor, UVRev) || !ReadPod(Cursor, ShadeRev))
    {
        return false;
    }

    OutNetwork.Reset();
    OutNetwork.NetworkTitle = Title;
    OutNetwork.ReserveStorage(VertCount, FaceCount * 4u, FaceCount);

    for (uint32_t VIdx = 0u; VIdx < VertCount; ++VIdx)
    {
        Mathematics::Axis3D Axes {};
        if (!ReadPod(Cursor, Axes))
        {
            return false;
        }
        (void)OutNetwork.AppendVertex(Axes);
    }

    for (uint32_t FIdx = 0u; FIdx < FaceCount; ++FIdx)
    {
        uint16_t CornerCount = 0u, LayerOrd = 0u, PartOrd = 0u;
        GeometrySpace::FaceShadingAssignment Shading {};
        if (!ReadPod(Cursor, CornerCount) || !ReadPod(Cursor, LayerOrd) || !ReadPod(Cursor, PartOrd) ||
            !ReadPod(Cursor, Shading.AlbedoLinearRGB) || !ReadPod(Cursor, Shading.Metallic) ||
            !ReadPod(Cursor, Shading.Roughness) || !ReadPod(Cursor, Shading.Specular) ||
            !ReadPod(Cursor, Shading.MatCapBlend) || !ReadPod(Cursor, Shading.MatCapSlot) ||
            CornerCount < 3u)
        {
            return false;
        }

        std::vector<uint32_t>                        LoopV(CornerCount);
        std::vector<Mathematics::UVAxes>             LoopUV(CornerCount);
        std::vector<GeometrySpace::EdgeAttributeMask> LoopMasks(CornerCount);
        for (uint16_t Step = 0u; Step < CornerCount; ++Step)
        {
            uint8_t MaskByte = 0u;
            if (!ReadPod(Cursor, LoopV[Step]) || !ReadPod(Cursor, LoopUV[Step]) || !ReadPod(Cursor, MaskByte))
            {
                return false;
            }
            LoopMasks[Step] = static_cast<GeometrySpace::EdgeAttributeMask>(MaskByte);
        }

        const uint32_t CreatedFace = OutNetwork.AppendFace(LoopV, LoopUV, Shading, LayerOrd, PartOrd);
        if (CreatedFace == GeometrySpace::InvalidNetworkIndex)
        {
            return false;
        }
        uint32_t Walk = OutNetwork.Faces[CreatedFace].BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CornerCount; ++Step)
        {
            OutNetwork.Edges[Walk].AttributeMask = LoopMasks[Step];
            Walk = OutNetwork.Edges[Walk].NextEdgeIndex;
        }
    }

    OutNetwork.LinkOppositeEdges(false);
    OutNetwork.RecomputeNormals();
    OutNetwork.NetworkRevision = NetRev;
    OutNetwork.UVRevision      = UVRev;
    OutNetwork.ShadingRevision = ShadeRev;
    return true;
}

// 📝 Decodes bundled VehiclePolygon + VehicleUVSheet sections inside ControlledIntegrator.GeometrySpace
[[nodiscard]] bool TryDecodeBundledVehicleSections(
    const SpaceContainerPayload& Payload,
    GeometrySpaceDocument&       OutDocument)
{
    if (Payload.BinarySections.size() < 2u ||
        Payload.BinarySections[0u].SectionIdentifier != "VehiclePolygon" ||
        Payload.BinarySections[1u].SectionIdentifier != "VehicleUVSheet")
    {
        return false;
    }

    const auto& GeometryBytes = Payload.BinarySections[0u].PayloadBytes;
    const auto& UVBytes       = Payload.BinarySections[1u].PayloadBytes;
    if (GeometryBytes.size() < 8u)
    {
        return false;
    }

    uint32_t VertCount = 0u, FaceCount = 0u;
    std::memcpy(&VertCount, GeometryBytes.data(), sizeof(uint32_t));
    std::memcpy(&FaceCount, GeometryBytes.data() + 4u, sizeof(uint32_t));

    const std::size_t ExpectedGeom = 8ull + static_cast<std::size_t>(VertCount) * 12ull + static_cast<std::size_t>(FaceCount) * 16ull;
    const std::size_t ExpectedUV   = static_cast<std::size_t>(FaceCount) * 24ull;
    if (VertCount == 0u || FaceCount == 0u || GeometryBytes.size() < ExpectedGeom || UVBytes.size() < ExpectedUV)
    {
        return false;
    }

    OutDocument = GeometrySpaceDocument{};
    OutDocument.Network.NetworkTitle = Payload.DocumentTitle;
    OutDocument.UDIMTiles            = { 1001u };
    (void)OutDocument.Partitions.ConstructLayer("Body Coat Hero", { 0.937f, 0.957f, 0.812f }, 55.0f, 1001u);
    (void)OutDocument.Partitions.ConstructLayer("Optics & Glass", { 0.620f, 0.812f, 1.000f }, 15.0f, 1001u);

    const std::array<GeometrySpace::FaceShadingAssignment, 6u> PartMaterials {{
        { { 0.27f, 0.37f, 0.56f }, 0.72f, 0.18f, 0.88f, 0.52f, 2u },
        { { 0.05f, 0.08f, 0.12f }, 0.08f, 0.06f, 0.95f, 0.62f, 6u },
        { { 0.88f, 0.78f, 0.62f }, 0.20f, 0.10f, 0.90f, 0.50f, 5u },
        { { 0.10f, 0.11f, 0.13f }, 0.36f, 0.42f, 0.48f, 0.35f, 1u },
        { { 0.06f, 0.065f, 0.072f }, 0.04f, 0.68f, 0.28f, 0.30f, 8u },
        { { 0.70f, 0.73f, 0.78f }, 0.90f, 0.20f, 0.92f, 0.58f, 2u },
    }};

    OutDocument.Network.ReserveStorage(VertCount, FaceCount * 3u, FaceCount);
    const std::byte* VertCursor = GeometryBytes.data() + 8u;
    for (uint32_t VIdx = 0u; VIdx < VertCount; ++VIdx)
    {
        float Coords[3] {};
        std::memcpy(Coords, VertCursor + static_cast<std::size_t>(VIdx) * 12ull, 12ull);
        (void)OutDocument.Network.AppendVertex({ Coords[0], Coords[1], Coords[2] });
    }

    const std::byte* FaceCursor = VertCursor + static_cast<std::size_t>(VertCount) * 12ull;
    const std::byte* UVCursor   = UVBytes.data();
    for (uint32_t FIdx = 0u; FIdx < FaceCount; ++FIdx)
    {
        uint32_t TriIndices[3] {};
        uint16_t LayerOrd  = 0u;
        uint16_t PartGroup = 0u;
        const std::byte* Rec = FaceCursor + static_cast<std::size_t>(FIdx) * 16ull;
        std::memcpy(TriIndices, Rec, 12ull);
        std::memcpy(&LayerOrd,  Rec + 12ull, 2ull);
        std::memcpy(&PartGroup, Rec + 14ull, 2ull);

        float RawUV[6] {};
        std::memcpy(RawUV, UVCursor + static_cast<std::size_t>(FIdx) * 24ull, 24ull);

        const std::array<uint32_t, 3u> Loop {{ TriIndices[0], TriIndices[1], TriIndices[2] }};
        const std::array<Mathematics::UVAxes, 3u> LoopUV {{
            { RawUV[0], RawUV[1] }, { RawUV[2], RawUV[3] }, { RawUV[4], RawUV[5] }
        }};
        const auto& Mat = PartMaterials[std::min<std::size_t>(PartGroup, PartMaterials.size() - 1u)];
        (void)OutDocument.Network.AppendFace(Loop, LoopUV, Mat, LayerOrd, static_cast<uint16_t>(PartGroup + 1u));
    }

    OutDocument.Network.LinkOppositeEdges(true);
    OutDocument.Network.RecomputeNormals();
    OutDocument.Network.MarkDihedralSharpSeams(52.0f, false);
    OutDocument.Partitions.SynchronizePartitions(OutDocument.Network);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY ENCODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload GeometryEncoder::ConstructPayload(
    const GeometrySpaceDocument&       Document,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload {};
    Payload.SuffixCategory  = SpaceSuffixCategory::GeometrySpace;
    Payload.SchemaVersion      = 1u;
    Payload.DocumentTitle      = Document.Network.NetworkTitle.empty() ? "UntitledGeometry" : Document.Network.NetworkTitle;
    Payload.AuthoringWorkspace = "Frontier.Geometry";

    toml::table MetadataSection {};
    MetadataSection.insert_or_assign("VertexCount",          static_cast<int64_t>(Document.Network.Vertices.size()));
    MetadataSection.insert_or_assign("DirectedEdgeCount",    static_cast<int64_t>(Document.Network.Edges.size()));
    MetadataSection.insert_or_assign("FaceCount",            static_cast<int64_t>(Document.Network.Faces.size()));
    MetadataSection.insert_or_assign("CreaseAngleDegrees",   static_cast<double>(Document.CreaseAngleDegrees));
    MetadataSection.insert_or_assign("PartSlotCount",        static_cast<int64_t>(Document.SurfaceSlots.size()));
    if (!Document.SurfaceParameterPath.empty())
    {
        MetadataSection.insert_or_assign("SurfaceParameterPath", Document.SurfaceParameterPath);
    }
    MetadataSection.insert_or_assign("ClustersIncluded",     Document.ClustersIncluded);
    MetadataSection.insert_or_assign("WideTreeIncluded",     Document.WideTreeIncluded);

    toml::array UDIMArray {};
    for (const uint32_t Code : Document.UDIMTiles)
    {
        UDIMArray.push_back(static_cast<int64_t>(Code));
    }
    MetadataSection.insert_or_assign("UDIMTiles", std::move(UDIMArray));

    toml::array LayersArray {};
    for (const GeometrySpace::PriorityAllocationLayer& Layer : Document.Partitions.InspectLayers())
    {
        toml::table LayerEntry {};
        LayerEntry.insert_or_assign("LayerOrdinal",       static_cast<int64_t>(Layer.LayerOrdinal));
        LayerEntry.insert_or_assign("LayerTitle",         Layer.LayerTitle);
        LayerEntry.insert_or_assign("TargetSharePercent", static_cast<double>(Layer.TargetSharePercent));
        LayerEntry.insert_or_assign("TargetUDIMNumber",   static_cast<int64_t>(Layer.TargetUDIMNumber));
        toml::array TintRGB {};
        TintRGB.push_back(static_cast<double>(Layer.LinearTintRGB.X));
        TintRGB.push_back(static_cast<double>(Layer.LinearTintRGB.Y));
        TintRGB.push_back(static_cast<double>(Layer.LinearTintRGB.Z));
        LayerEntry.insert_or_assign("LinearTintRGB", std::move(TintRGB));
        LayersArray.push_back(std::move(LayerEntry));
    }
    MetadataSection.insert_or_assign("PriorityLayers", std::move(LayersArray));
    Payload.DomainManifest.insert_or_assign("GeometryMetadata", std::move(MetadataSection));

    if (!Document.SurfaceSlots.empty())
    {
        toml::array SlotArray {};
        for (const GeometrySurfaceSlotLink& Slot : Document.SurfaceSlots)
        {
            toml::table SlotEntry {};
            SlotEntry.insert_or_assign("PartGroupOrdinal", static_cast<int64_t>(Slot.PartGroupOrdinal));
            SlotEntry.insert_or_assign("SlotTitle", Slot.SlotTitle);
            SlotEntry.insert_or_assign("MaterialPath", Slot.MaterialPath);
            SlotArray.push_back(std::move(SlotEntry));
        }
        Payload.DomainManifest.insert_or_assign("SurfaceSlots", std::move(SlotArray));
    }

    BinarySectionBlock PolygonBlock {};
    PolygonBlock.SectionIdentifier = "PolygonNetwork";
    PolygonBlock.SubFormatTag      = ".PolygonNetwork";
    PolygonBlock.PayloadBytes      = EncodePolygonNetworkBinary(Document.Network);
    Payload.BinarySections.push_back(std::move(PolygonBlock));

    if (!Document.FaceCornerNormals.empty())
    {
        BinarySectionBlock NormalBlock {};
        NormalBlock.SectionIdentifier = "SmoothNormals";
        NormalBlock.SubFormatTag      = ".SmoothNormals";
        const std::size_t ByteLength  = Document.FaceCornerNormals.size() * sizeof(float);
        NormalBlock.PayloadBytes.resize(ByteLength);
        std::memcpy(NormalBlock.PayloadBytes.data(), Document.FaceCornerNormals.data(), ByteLength);
        Payload.BinarySections.push_back(std::move(NormalBlock));
    }

    if (Document.ClustersIncluded && !Document.ClusterHierarchy.Clusters.empty())
    {
        BinarySectionBlock ClusterBlock {};
        ClusterBlock.SectionIdentifier = "ClusterPyramid";
        ClusterBlock.SubFormatTag      = ".ClusterHierarchy";
        ClusterBlock.PayloadBytes      = Document.ClusterHierarchy.SerializeToBinaryBytes();
        Payload.BinarySections.push_back(std::move(ClusterBlock));
    }

    if (Document.WideTreeIncluded && !Document.RayAcceleration.WideEntries.empty())
    {
        BinarySectionBlock WideTreeBlock {};
        WideTreeBlock.SectionIdentifier = "WideBoundingTree";
        WideTreeBlock.SubFormatTag      = ".WideBoundingTree";
        WideTreeBlock.PayloadBytes      = Document.RayAcceleration.SerializeToBinaryBytes();
        Payload.BinarySections.push_back(std::move(WideTreeBlock));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }

    return Payload;
}

bool GeometryEncoder::EncodeToBytes(
    const GeometrySpaceDocument&       Document,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = ConstructPayload(Document, OptionalRevision);
    return SpaceContainerEncoder::EncodeToBytes(Payload, OutContainerBytes, OutDiagnostic);
}

bool GeometryEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const GeometrySpaceDocument&       Document,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = ConstructPayload(Document, OptionalRevision);
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY DECODER
//------------------------------------------------------------------------------------------------------------------------

bool GeometryDecoder::ExtractDocument(
    const SpaceContainerPayload& Payload,
    GeometrySpaceDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::GeometrySpace)
    {
        OutDiagnostic = "Container SuffixCategory is not .GeometrySpace.";
        return false;
    }

    if (TryDecodeBundledVehicleSections(Payload, OutDocument))
    {
        OutDiagnostic.clear();
        return true;
    }

    OutDocument = GeometrySpaceDocument{};
    OutDocument.UDIMTiles.clear();

    if (const auto* MetadataSection = Payload.DomainManifest["GeometryMetadata"].as_table())
    {
        OutDocument.CreaseAngleDegrees   = static_cast<float>((*MetadataSection)["CreaseAngleDegrees"].value_or(38.0));
        OutDocument.SurfaceParameterPath = (*MetadataSection)["SurfaceParameterPath"].value_or(std::string{});
        if (const auto* UDIMArray = (*MetadataSection)["UDIMTiles"].as_array())
        {
            for (const toml::node& Elem : *UDIMArray)
            {
                if (const auto Code = Elem.value<int64_t>(); Code.has_value() && *Code >= 1001)
                {
                    OutDocument.UDIMTiles.push_back(static_cast<uint32_t>(*Code));
                }
            }
        }

        if (const auto* LayersArray = (*MetadataSection)["PriorityLayers"].as_array())
        {
            for (const toml::node& Elem : *LayersArray)
            {
                const auto* LayerTbl = Elem.as_table();
                if (LayerTbl == nullptr)
                {
                    continue;
                }
                const std::string Title = (*LayerTbl)["LayerTitle"].value_or(std::string("Layer"));
                const auto Share        = static_cast<float>((*LayerTbl)["TargetSharePercent"].value_or(25.0));
                const auto TargetUDIM   = static_cast<uint32_t>((*LayerTbl)["TargetUDIMNumber"].value_or(int64_t{ 0 }));
                Mathematics::Scalar3D Tint { 0.937f, 0.957f, 0.812f };
                if (const auto* TintArr = (*LayerTbl)["LinearTintRGB"].as_array(); TintArr != nullptr && TintArr->size() >= 3u)
                {
                    Tint.X = static_cast<float>((*TintArr)[0u].value_or(0.937));
                    Tint.Y = static_cast<float>((*TintArr)[1u].value_or(0.957));
                    Tint.Z = static_cast<float>((*TintArr)[2u].value_or(0.812));
                }
                (void)OutDocument.Partitions.ConstructLayer(Title, Tint, Share, TargetUDIM);
            }
        }
    }

    if (OutDocument.UDIMTiles.empty())
    {
        OutDocument.UDIMTiles.push_back(1001u);
    }

    if (const auto* SlotArray = Payload.DomainManifest["SurfaceSlots"].as_array())
    {
        for (const toml::node& Elem : *SlotArray)
        {
            if (const auto* SlotTbl = Elem.as_table())
            {
                GeometrySurfaceSlotLink Link {};
                Link.PartGroupOrdinal = static_cast<uint32_t>((*SlotTbl)["PartGroupOrdinal"].value_or(int64_t{ 0 }));
                Link.SlotTitle        = (*SlotTbl)["SlotTitle"].value_or(std::string{});
                Link.MaterialPath     = (*SlotTbl)["MaterialPath"].value_or(std::string{});
                OutDocument.SurfaceSlots.push_back(std::move(Link));
            }
        }
    }

    bool PolygonDecoded = false;
    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SectionIdentifier == "PolygonNetwork" &&
            (Section.SubFormatTag == ".PolygonNetwork" || Section.SubFormatTag == ".Baseline"))
        {
            if (!DecodePolygonNetworkBinary(Section.PayloadBytes, Payload.DocumentTitle, OutDocument.Network))
            {
                OutDiagnostic = "Corrupt PolygonNetwork section inside .GeometrySpace.";
                return false;
            }
            PolygonDecoded = true;
        }
        else if (Section.SectionIdentifier == "SmoothNormals" && Section.SubFormatTag == ".SmoothNormals")
        {
            if (Section.PayloadBytes.size() % sizeof(float) != 0u)
            {
                OutDiagnostic = "Corrupt SmoothNormals float32 section inside .GeometrySpace.";
                return false;
            }
            OutDocument.FaceCornerNormals.resize(Section.PayloadBytes.size() / sizeof(float));
            std::memcpy(OutDocument.FaceCornerNormals.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
        }
        else if (Section.SubFormatTag == ".ClusterHierarchy")
        {
            if (!OutDocument.ClusterHierarchy.DeserializeFromBinaryBytes(Section.PayloadBytes))
            {
                OutDiagnostic = "Corrupt .ClusterHierarchy section inside .GeometrySpace.";
                return false;
            }
            OutDocument.ClustersIncluded = true;
        }
        else if (Section.SubFormatTag == ".WideBoundingTree")
        {
            if (!OutDocument.RayAcceleration.DeserializeFromBinaryBytes(Section.PayloadBytes))
            {
                OutDiagnostic = "Corrupt .WideBoundingTree section inside .GeometrySpace.";
                return false;
            }
            OutDocument.WideTreeIncluded = true;
        }
    }

    if (OptionalRevision != nullptr)
    {
        if (!RevisionDecoder::ExtractRevisionSections(Payload, *OptionalRevision, OutDiagnostic))
        {
            return false;
        }
    }

    if (!PolygonDecoded)
    {
        OutDiagnostic = "Missing PolygonNetwork .Baseline section inside .GeometrySpace.";
        return false;
    }

    OutDocument.Partitions.SynchronizePartitions(OutDocument.Network);
    OutDiagnostic.clear();
    return true;
}

bool GeometryDecoder::DecodeFromBytes(
    std::span<const std::byte>   RawContainerBytes,
    GeometrySpaceDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload {};
    if (!SpaceContainerDecoder::DecodeFromBytes(RawContainerBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalRevision);
}

bool GeometryDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    GeometrySpaceDocument&       OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload {};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalRevision);
}

} // namespace Frontier::Codecs
