/*============================================================================================================================================
                                                         GEOMETRYINTERCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of Wavefront .obj & glTF 2.0 .gltf multi-UDIM PolygonNetwork + FacePartitionRegister import and export

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Interchange/GeometryInterchange.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Frontier::Interchange
{

namespace
{

[[nodiscard]] std::string LowercaseExtension(const std::filesystem::path& FilePath)
{
    std::string Ext = FilePath.extension().string();
    for (char& Ch : Ext)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Ext;
}

[[nodiscard]] std::string_view TrimAscii(std::string_view View) noexcept
{
    while (!View.empty() && std::isspace(static_cast<unsigned char>(View.front())))
    {
        View.remove_prefix(1u);
    }
    while (!View.empty() && std::isspace(static_cast<unsigned char>(View.back())))
    {
        View.remove_suffix(1u);
    }
    return View;
}

[[nodiscard]] Mathematics::Scalar3D LayerSwatchTint(uint32_t Index) noexcept
{
    constexpr std::array<Mathematics::Scalar3D, 6u> Swatches {{
        { 0.95f, 0.88f, 0.62f },
        { 0.42f, 0.72f, 0.96f },
        { 0.56f, 0.86f, 0.68f },
        { 0.94f, 0.58f, 0.46f },
        { 0.78f, 0.62f, 0.94f },
        { 0.54f, 0.86f, 0.88f },
    }};
    return Swatches[Index % Swatches.size()];
}

void FinalizeConstructedAssetUDIMs(GeometrySpace::ConstructedSampleAsset& Asset)
{
    std::set<uint32_t> UniqueUDIMs {};
    for (uint32_t FaceIdx = 0u; FaceIdx < Asset.Network.Faces.size(); ++FaceIdx)
    {
        UniqueUDIMs.insert(Asset.Network.DeriveFaceUDIMNumber(FaceIdx));
    }
    if (UniqueUDIMs.empty())
    {
        UniqueUDIMs.insert(1001u);
    }
    Asset.UDIMTiles.assign(UniqueUDIMs.begin(), UniqueUDIMs.end());
    Asset.Layers.SynchronizePartitions(Asset.Network, GeometrySpace::GeometryInstanceToken{ 0u, 1u });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BASE64 CODEC
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string EncodeRadix64(const uint8_t* Bytes, std::size_t Length)
{
    static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string Out;
    Out.reserve(((Length + 2u) / 3u) * 4u);

    for (std::size_t Idx = 0u; Idx < Length; Idx += 3u)
    {
        const uint32_t B0 = Bytes[Idx];
        const uint32_t B1 = (Idx + 1u < Length) ? Bytes[Idx + 1u] : 0u;
        const uint32_t B2 = (Idx + 2u < Length) ? Bytes[Idx + 2u] : 0u;
        const uint32_t Triple = (B0 << 16u) | (B1 << 8u) | B2;

        Out.push_back(Alphabet[(Triple >> 18u) & 0x3Fu]);
        Out.push_back(Alphabet[(Triple >> 12u) & 0x3Fu]);
        Out.push_back((Idx + 1u < Length) ? Alphabet[(Triple >> 6u) & 0x3Fu] : '=');
        Out.push_back((Idx + 2u < Length) ? Alphabet[Triple & 0x3Fu] : '=');
    }
    return Out;
}

[[nodiscard]] std::vector<uint8_t> DecodeRadix64(std::string_view Encoded)
{
    std::array<int8_t, 256u> DigitLookup {};
    DigitLookup.fill(-1);
    static constexpr std::string_view Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t Idx = 0u; Idx < Alphabet.size(); ++Idx)
    {
        DigitLookup[static_cast<uint8_t>(Alphabet[Idx])] = static_cast<int8_t>(Idx);
    }

    std::vector<uint8_t> Out;
    Out.reserve((Encoded.size() * 3u) / 4u);

    uint32_t Accum = 0u;
    int      Bits  = -8;
    for (const char Ch : Encoded)
    {
        const unsigned char UCh = static_cast<unsigned char>(Ch);
        if (std::isspace(UCh))
        {
            continue;
        }
        if (Ch == '=')
        {
            break;
        }
        const int8_t Val = DigitLookup[UCh];
        if (Val < 0)
        {
            continue;
        }
        Accum = (Accum << 6u) | static_cast<uint32_t>(Val);
        Bits += 6;
        if (Bits >= 0)
        {
            Out.push_back(static_cast<uint8_t>((Accum >> Bits) & 0xFFu));
            Bits -= 8;
        }
    }
    return Out;
}

[[nodiscard]] bool ParseObjIndex(std::string_view Token, int& OutIndex) noexcept
{
    if (Token.empty())
    {
        OutIndex = 0;
        return true;
    }
    const auto Parsed = std::from_chars(Token.data(), Token.data() + Token.size(), OutIndex);
    return Parsed.ec == std::errc{} && Parsed.ptr == Token.data() + Token.size();
}

[[nodiscard]] int ResolveObjIndex(int RawOneBased, std::size_t TotalCount) noexcept
{
    if (TotalCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return -1;
    }
    if (RawOneBased > 0)
    {
        return RawOneBased - 1;
    }
    if (RawOneBased < 0)
    {
        return static_cast<int>(TotalCount) + RawOneBased;
    }
    return -1;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  FORMAT DISPATCH
//------------------------------------------------------------------------------------------------------------------------

ExternalGeometryFormat GeometryInterchange::DetectFormatFromPath(const std::filesystem::path& FilePath) noexcept
{
    const std::string Ext = LowercaseExtension(FilePath);
    if (Ext == ".obj")
    {
        return ExternalGeometryFormat::WavefrontObj;
    }
    if (Ext == ".gltf")
    {
        return ExternalGeometryFormat::Gltf2Json;
    }
    return ExternalGeometryFormat::Unknown;
}

bool GeometryInterchange::ImportAssetFromFile(
    const std::filesystem::path&           SourcePath,
    GeometrySpace::ConstructedSampleAsset& OutAsset,
    std::string&                           OutDiagnostic)
{
    std::ifstream Stream(SourcePath, std::ios::binary);
    if (!Stream)
    {
        OutDiagnostic = "Unable to open external geometry file: " + SourcePath.string();
        return false;
    }
    std::ostringstream Buffer;
    Buffer << Stream.rdbuf();
    const std::string Content = Buffer.str();
    const std::string Stem    = SourcePath.stem().string().empty() ? "ImportedGeometry" : SourcePath.stem().string();

    switch (DetectFormatFromPath(SourcePath))
    {
        case ExternalGeometryFormat::WavefrontObj:
            return ImportWavefrontObjText(Content, Stem, OutAsset, OutDiagnostic);
        case ExternalGeometryFormat::Gltf2Json:
            return ImportGltf2JsonText(Content, SourcePath.parent_path(), Stem, OutAsset, OutDiagnostic);
        case ExternalGeometryFormat::Unknown:
            break;
    }

    OutDiagnostic = "Unsupported external geometry extension: " + SourcePath.extension().string();
    return false;
}

bool GeometryInterchange::ExportAssetToFile(
    const std::filesystem::path&                TargetPath,
    const GeometrySpace::PolygonNetwork&        Network,
    const GeometrySpace::FacePartitionRegister* OptionalPartitions,
    std::string&                                OutDiagnostic)
{
    if (TargetPath.has_parent_path())
    {
        std::error_code Ec {};
        std::filesystem::create_directories(TargetPath.parent_path(), Ec);
    }

    std::string Payload;
    switch (DetectFormatFromPath(TargetPath))
    {
        case ExternalGeometryFormat::WavefrontObj:
            Payload = ExportWavefrontObjText(Network, OptionalPartitions);
            break;
        case ExternalGeometryFormat::Gltf2Json:
            Payload = ExportGltf2JsonText(Network);
            break;
        case ExternalGeometryFormat::Unknown:
            OutDiagnostic = "Unsupported export extension: " + TargetPath.extension().string();
            return false;
    }

    std::ofstream Out(TargetPath, std::ios::binary | std::ios::trunc);
    if (!Out)
    {
        OutDiagnostic = "Unable to write geometry file: " + TargetPath.string();
        return false;
    }
    Out.write(Payload.data(), static_cast<std::streamsize>(Payload.size()));
    return Out.good();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WAVEFRONT .OBJ
//------------------------------------------------------------------------------------------------------------------------

bool GeometryInterchange::ImportWavefrontObjText(
    std::string_view                       ObjText,
    std::string_view                       DefaultTitle,
    GeometrySpace::ConstructedSampleAsset& OutAsset,
    std::string&                           OutDiagnostic)
{
    OutAsset = GeometrySpace::ConstructedSampleAsset{};
    OutAsset.Network.NetworkTitle = std::string(DefaultTitle);

    std::vector<Mathematics::Axis3D> TempPositions {};
    std::vector<Mathematics::UVAxes> TempUVs       {};
    std::unordered_map<std::string, uint16_t> GroupToLayer {};

    uint16_t ActiveLayerOrdinal = 0u;
    uint16_t ActivePartOrdinal  = 0u;
    uint32_t NextColorIdx       = 0u;

    std::size_t LineStart = 0u;
    while (LineStart < ObjText.size())
    {
        std::size_t LineEnd = ObjText.find('\n', LineStart);
        if (LineEnd == std::string_view::npos)
        {
            LineEnd = ObjText.size();
        }
        const std::string_view RawLine = TrimAscii(ObjText.substr(LineStart, LineEnd - LineStart));
        LineStart = LineEnd + 1u;

        if (RawLine.empty() || RawLine.front() == '#')
        {
            continue;
        }

        if (RawLine.starts_with("v "))
        {
            float X = 0.0f;
            float Y = 0.0f;
            float Z = 0.0f;
            if (std::sscanf(std::string(RawLine.substr(2u)).c_str(), "%f %f %f", &X, &Y, &Z) == 3 &&
                std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z) &&
                std::abs(X) <= 20000.0f && std::abs(Y) <= 20000.0f && std::abs(Z) <= 20000.0f)
            {
                const Mathematics::Axis3D Converted = Mathematics::ConvertVectorBetweenBases(
                    { X, Y, Z },
                    Mathematics::WorldCoordinateBasis::RightHandedYUp,
                    Mathematics::QueryActiveWorldBasis());
                TempPositions.push_back(Converted);
                (void)OutAsset.Network.AppendVertex(Converted);
            }
        }
        else if (RawLine.starts_with("vt "))
        {
            float U = 0.0f;
            float V = 0.0f;
            if (std::sscanf(std::string(RawLine.substr(3u)).c_str(), "%f %f", &U, &V) >= 2 &&
                std::isfinite(U) && std::isfinite(V) && std::abs(U) <= 100000.0f && std::abs(V) <= 100000.0f)
            {
                TempUVs.push_back({ U, V });
            }
        }
        else if (RawLine.starts_with("o ") || RawLine.starts_with("g ") || RawLine.starts_with("usemtl "))
        {
            const std::size_t SpacePos = RawLine.find(' ');
            const std::string GroupName(TrimAscii(RawLine.substr(SpacePos + 1u)));
            if (!GroupName.empty())
            {
                if (RawLine.starts_with("o ") && OutAsset.Network.Faces.empty())
                {
                    OutAsset.Network.NetworkTitle = GroupName;
                }
                auto It = GroupToLayer.find(GroupName);
                if (It == GroupToLayer.end())
                {
                    // Reserve up to 4 named priority layers; route additional groups to Remainder (0)
                    if (GroupToLayer.size() < 4u)
                    {
                        const uint16_t NewOrd = OutAsset.Layers.ConstructLayer(
                            GroupName,
                            LayerSwatchTint(NextColorIdx++),
                            20.0f);
                        GroupToLayer.emplace(GroupName, NewOrd);
                        ActiveLayerOrdinal = NewOrd;
                    }
                    else
                    {
                        ActiveLayerOrdinal = 0u;
                    }
                    ++ActivePartOrdinal;
                }
                else
                {
                    ActiveLayerOrdinal = It->second;
                }
            }
        }
        else if (RawLine.starts_with("f "))
        {
            std::vector<uint32_t>            FaceVerts {};
            std::vector<Mathematics::UVAxes> FaceUVs   {};

            std::string_view Rest = TrimAscii(RawLine.substr(2u));
            while (!Rest.empty())
            {
                std::size_t NextWs = 0u;
                while (NextWs < Rest.size() && !std::isspace(static_cast<unsigned char>(Rest[NextWs])))
                {
                    ++NextWs;
                }
                const std::string Token(Rest.substr(0u, NextWs));
                Rest = TrimAscii(Rest.substr(NextWs));

                int VIdx  = 0;
                int VtIdx = 0;
                const std::size_t Slash1 = Token.find('/');
                bool ValidIndices = true;
                if (Slash1 == std::string::npos)
                {
                    ValidIndices = ParseObjIndex(Token, VIdx);
                }
                else
                {
                    ValidIndices = ParseObjIndex(std::string_view(Token).substr(0u, Slash1), VIdx);
                    const std::size_t Slash2 = Token.find('/', Slash1 + 1u);
                    const std::string VtSub  = (Slash2 == std::string::npos)
                        ? Token.substr(Slash1 + 1u)
                        : Token.substr(Slash1 + 1u, Slash2 - Slash1 - 1u);
                    if (!VtSub.empty())
                    {
                        ValidIndices = ValidIndices && ParseObjIndex(VtSub, VtIdx);
                    }
                }
                if (!ValidIndices)
                {
                    FaceVerts.clear();
                    FaceUVs.clear();
                    break;
                }

                const int ResolvedV  = ResolveObjIndex(VIdx, TempPositions.size());
                const int ResolvedVt = ResolveObjIndex(VtIdx, TempUVs.size());
                if (ResolvedV < 0 || static_cast<std::size_t>(ResolvedV) >= TempPositions.size())
                {
                    continue;
                }
                FaceVerts.push_back(static_cast<uint32_t>(ResolvedV));
                if (ResolvedVt >= 0 && static_cast<std::size_t>(ResolvedVt) < TempUVs.size())
                {
                    FaceUVs.push_back(TempUVs[static_cast<std::size_t>(ResolvedVt)]);
                }
                else
                {
                    const auto& Pos = TempPositions[static_cast<std::size_t>(ResolvedV)];
                    FaceUVs.push_back({ Pos.X * 0.5f + 0.5f, Pos.Z * 0.5f + 0.5f });
                }
            }

            if (FaceVerts.size() >= 3u)
            {
                const std::set<uint32_t> UniqueFaceVertices(FaceVerts.begin(), FaceVerts.end());
                if (UniqueFaceVertices.size() != FaceVerts.size())
                {
                    continue;
                }
                GeometrySpace::FaceShadingAssignment Shading {};
                Shading.MatCapSlot = static_cast<uint8_t>((ActiveLayerOrdinal * 2u + 3u) % 16u);
                (void)OutAsset.Network.AppendFace(FaceVerts, FaceUVs, Shading, ActiveLayerOrdinal, ActivePartOrdinal);
            }
        }
    }

    if (OutAsset.Network.Faces.empty() || OutAsset.Network.Vertices.empty())
    {
        OutDiagnostic = "Wavefront .obj stream contains zero valid polygonal faces.";
        return false;
    }

    OutAsset.Network.LinkOppositeEdges(true);
    OutAsset.Network.RecomputeNormals();
    FinalizeConstructedAssetUDIMs(OutAsset);
    return true;
}

std::string GeometryInterchange::ExportWavefrontObjText(
    const GeometrySpace::PolygonNetwork&        Network,
    const GeometrySpace::FacePartitionRegister* OptionalPartitions)
{
    std::ostringstream Out;
    Out.setf(std::ios::fixed);
    Out.precision(6);

    Out << "# Frontier GeometryInterchange Wavefront OBJ\n";
    Out << "o " << (Network.NetworkTitle.empty() ? "FrontierNetwork" : Network.NetworkTitle) << "\n";

    const auto ActiveBasis = Mathematics::QueryActiveWorldBasis();
    for (const auto& Vert : Network.Vertices)
    {
        const Mathematics::Axis3D P = Mathematics::ConvertVectorBetweenBases(
            Vert.Axes,
            ActiveBasis,
            Mathematics::WorldCoordinateBasis::RightHandedYUp);
        Out << "v " << P.X << " " << P.Y << " " << P.Z << "\n";
    }
    for (const auto& Vert : Network.Vertices)
    {
        const Mathematics::Axis3D N = Mathematics::ConvertVectorBetweenBases(
            Vert.Normal,
            ActiveBasis,
            Mathematics::WorldCoordinateBasis::RightHandedYUp);
        Out << "vn " << N.X << " " << N.Y << " " << N.Z << "\n";
    }
    for (const auto& Edge : Network.Edges)
    {
        Out << "vt " << Edge.ParameterUV.U << " " << Edge.ParameterUV.V << "\n";
    }

    uint16_t LastLayer = 0xFFFFu;
    for (uint32_t FaceIdx = 0u; FaceIdx < Network.Faces.size(); ++FaceIdx)
    {
        const auto& Face = Network.Faces[FaceIdx];
        if (Face.PriorityLayerOrdinal != LastLayer)
        {
            LastLayer = Face.PriorityLayerOrdinal;
            std::string GroupTitle = "Remainder";
            if (OptionalPartitions != nullptr && LastLayer != 0u)
            {
                if (const auto* Layer = OptionalPartitions->InspectLayer(LastLayer))
                {
                    GroupTitle = Layer->LayerTitle;
                    std::replace(GroupTitle.begin(), GroupTitle.end(), ' ', '_');
                }
            }
            else if (LastLayer != 0u)
            {
                GroupTitle = "PriorityLayer_" + std::to_string(LastLayer);
            }
            Out << "g " << GroupTitle << "\n";
        }

        Out << "f";
        uint32_t EdgeIdx = Face.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < Face.CornerCount && EdgeIdx != GeometrySpace::InvalidNetworkIndex; ++Corner)
        {
            const auto&    Edge = Network.Edges[EdgeIdx];
            const uint32_t V1   = Edge.OriginVertexIndex + 1u;
            const uint32_t Vt1  = EdgeIdx + 1u;
            Out << " " << V1 << "/" << Vt1 << "/" << V1;
            EdgeIdx = Edge.NextEdgeIndex;
        }
        Out << "\n";
    }
    return Out.str();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLTF 2.0 (.GLTF)
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] std::size_t FindMatchingBracket(std::string_view Text, std::size_t OpenPos, char OpenCh, char CloseCh) noexcept
{
    int  Depth    = 0;
    bool InString = false;
    for (std::size_t Idx = OpenPos; Idx < Text.size(); ++Idx)
    {
        const char Ch = Text[Idx];
        if (Ch == '"' && (Idx == 0u || Text[Idx - 1u] != '\\'))
        {
            InString = !InString;
        }
        if (InString)
        {
            continue;
        }
        if (Ch == OpenCh)
        {
            ++Depth;
        }
        else if (Ch == CloseCh)
        {
            --Depth;
            if (Depth == 0)
            {
                return Idx;
            }
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] std::string QuoteJsonFieldKey(std::string_view FieldName)
{
    std::string Needle {};
    Needle.reserve(FieldName.size() + 2u);
    Needle.push_back('"');
    Needle.append(FieldName.data(), FieldName.size());
    Needle.push_back('"');
    return Needle;
}

[[nodiscard]] int ExtractJsonIntField(std::string_view ObjSlice, std::string_view FieldName, int DefaultVal = -1) noexcept
{
    const std::string Needle = QuoteJsonFieldKey(FieldName);
    const std::size_t Pos    = ObjSlice.find(Needle);
    if (Pos == std::string_view::npos)
    {
        return DefaultVal;
    }
    const std::size_t Colon = ObjSlice.find(':', Pos + Needle.size());
    if (Colon == std::string_view::npos)
    {
        return DefaultVal;
    }
    const std::string_view Tail = TrimAscii(ObjSlice.substr(Colon + 1u));
    int Value = DefaultVal;
    (void)std::from_chars(Tail.data(), Tail.data() + Tail.size(), Value);
    return Value;
}

[[nodiscard]] std::vector<std::string_view> SplitJsonObjectArray(std::string_view Root, std::string_view ArrayKey)
{
    std::vector<std::string_view> Elements {};
    const std::string Needle = QuoteJsonFieldKey(ArrayKey);
    const std::size_t KeyPos = Root.find(Needle);
    if (KeyPos == std::string_view::npos)
    {
        return Elements;
    }
    const std::size_t ArrOpen = Root.find('[', KeyPos + Needle.size());
    if (ArrOpen == std::string_view::npos)
    {
        return Elements;
    }
    const std::size_t ArrClose = FindMatchingBracket(Root, ArrOpen, '[', ']');
    if (ArrClose == std::string_view::npos)
    {
        return Elements;
    }

    std::size_t Cursor = ArrOpen + 1u;
    while (Cursor < ArrClose)
    {
        const std::size_t ObjOpen = Root.find('{', Cursor);
        if (ObjOpen == std::string_view::npos || ObjOpen >= ArrClose)
        {
            break;
        }
        const std::size_t ObjClose = FindMatchingBracket(Root, ObjOpen, '{', '}');
        if (ObjClose == std::string_view::npos || ObjClose > ArrClose)
        {
            break;
        }
        Elements.push_back(Root.substr(ObjOpen, ObjClose - ObjOpen + 1u));
        Cursor = ObjClose + 1u;
    }
    return Elements;
}

} // namespace

bool GeometryInterchange::ImportGltf2JsonText(
    std::string_view                       GltfJsonText,
    const std::filesystem::path&           RootDirectory,
    std::string_view                       DefaultTitle,
    GeometrySpace::ConstructedSampleAsset& OutAsset,
    std::string&                           OutDiagnostic)
{
    OutAsset = GeometrySpace::ConstructedSampleAsset{};
    OutAsset.Network.NetworkTitle = std::string(DefaultTitle);

    const auto Buffers     = SplitJsonObjectArray(GltfJsonText, "buffers");
    const auto BufferViews = SplitJsonObjectArray(GltfJsonText, "bufferViews");
    const auto Accessors   = SplitJsonObjectArray(GltfJsonText, "accessors");

    if (Buffers.empty() || BufferViews.empty() || Accessors.empty())
    {
        OutDiagnostic = "glTF 2.0 document missing buffers, bufferViews, or accessors.";
        return false;
    }

    // Load buffer 0 (embedded base64 data URI or external .bin file)
    std::vector<uint8_t> BinaryBlob {};
    {
        const std::string_view Buf0 = Buffers.front();
        const std::size_t UriKey    = Buf0.find("\"uri\"");
        if (UriKey != std::string_view::npos)
        {
            const std::size_t Q1 = Buf0.find('"', UriKey + 5u);
            const std::size_t Q2 = (Q1 != std::string_view::npos) ? Buf0.find('"', Q1 + 1u) : std::string_view::npos;
            if (Q1 != std::string_view::npos && Q2 != std::string_view::npos)
            {
                const std::string_view Uri = Buf0.substr(Q1 + 1u, Q2 - Q1 - 1u);
                const std::size_t Radix64Marker = Uri.find("base64,");
                if (Radix64Marker != std::string_view::npos)
                {
                    BinaryBlob = DecodeRadix64(Uri.substr(Radix64Marker + 7u));
                }
                else
                {
                    std::ifstream BinIn(RootDirectory / std::string(Uri), std::ios::binary);
                    if (BinIn)
                    {
                        BinaryBlob.assign(std::istreambuf_iterator<char>(BinIn), std::istreambuf_iterator<char>());
                    }
                }
            }
        }
    }

    if (BinaryBlob.empty())
    {
        OutDiagnostic = "Unable to decode glTF 2.0 binary buffer payload.";
        return false;
    }

    auto ResolveAccessorBytes = [&](int AccessorIdx, int& OutCount, int& OutCompType, uint64_t AccessorValueByteCount) -> const uint8_t*
    {
        if (AccessorIdx < 0 || static_cast<std::size_t>(AccessorIdx) >= Accessors.size())
        {
            return nullptr;
        }
        const auto Acc       = Accessors[static_cast<std::size_t>(AccessorIdx)];
        const int  ViewIdx   = ExtractJsonIntField(Acc, "bufferView", -1);
        const int  AccOffset = ExtractJsonIntField(Acc, "byteOffset", 0);
        OutCount             = ExtractJsonIntField(Acc, "count", 0);
        OutCompType          = ExtractJsonIntField(Acc, "componentType", 0);
        if (ViewIdx < 0 || static_cast<std::size_t>(ViewIdx) >= BufferViews.size() || OutCount <= 0)
        {
            return nullptr;
        }
        const auto View = BufferViews[static_cast<std::size_t>(ViewIdx)];
        const int ViewOffset = ExtractJsonIntField(View, "byteOffset", 0);
        const int ViewLength = ExtractJsonIntField(View, "byteLength", -1);
        const int ViewBuffer = ExtractJsonIntField(View, "buffer", 0);
        const int ByteStride = ExtractJsonIntField(View, "byteStride", 0);
        if (ViewBuffer != 0 || ViewOffset < 0 || AccOffset < 0 || ViewLength < 0 ||
            (ByteStride != 0 && static_cast<uint64_t>(ByteStride) != AccessorValueByteCount))
        {
            return nullptr;
        }
        const uint64_t BaseOffset = static_cast<uint64_t>(ViewOffset) + static_cast<uint64_t>(AccOffset);
        const uint64_t SpanBytes = static_cast<uint64_t>(OutCount) * AccessorValueByteCount;
        const uint64_t ViewEnd = static_cast<uint64_t>(ViewOffset) + static_cast<uint64_t>(ViewLength);
        const uint64_t SpanEnd = BaseOffset + SpanBytes;
        if (BaseOffset % std::max<uint64_t>(1u, AccessorValueByteCount == 12u || AccessorValueByteCount == 8u ? sizeof(float) : AccessorValueByteCount) != 0u ||
            BaseOffset > BinaryBlob.size() || SpanEnd < BaseOffset || SpanEnd > ViewEnd || ViewEnd > BinaryBlob.size())
        {
            return nullptr;
        }
        return BinaryBlob.data() + static_cast<std::size_t>(BaseOffset);
    };

    const int PosAccIdx = ExtractJsonIntField(GltfJsonText, "POSITION", -1);
    const int UVAccIdx  = ExtractJsonIntField(GltfJsonText, "TEXCOORD_0", -1);
    const int IndAccIdx = ExtractJsonIntField(GltfJsonText, "indices", -1);

    int PosCount = 0;
    int PosType  = 0;
    const uint8_t* PosBytes = ResolveAccessorBytes(PosAccIdx, PosCount, PosType, sizeof(float) * 3u);
    if (PosBytes == nullptr || PosCount < 3 || PosType != 5126)
    {
        OutDiagnostic = "glTF 2.0 primitive missing FLOAT POSITION accessor.";
        return false;
    }

    int UVCount = 0;
    int UVType  = 0;
    const uint8_t* UVBytes = ResolveAccessorBytes(UVAccIdx, UVCount, UVType, sizeof(float) * 2u);

    // Weld identical 3D positions so UV-split glTF vertices reconstruct a connected half-edge PolygonNetwork
    struct QuantizedPos
    {
        int32_t X;
        int32_t Y;
        int32_t Z;
        bool operator==(const QuantizedPos&) const noexcept = default;
    };
    struct QuantizedHash
    {
        std::size_t operator()(const QuantizedPos& Key) const noexcept
        {
            return (static_cast<std::size_t>(Key.X) * 73856093u) ^
                   (static_cast<std::size_t>(Key.Y) * 19349663u) ^
                   (static_cast<std::size_t>(Key.Z) * 83492791u);
        }
    };

    std::unordered_map<QuantizedPos, uint32_t, QuantizedHash> WeldedLookup {};
    std::vector<uint32_t>      RemappedVerts(static_cast<std::size_t>(PosCount));
    std::vector<Mathematics::UVAxes> RawUVs(static_cast<std::size_t>(PosCount), { 0.0f, 0.0f });

    const float* PosFloats = reinterpret_cast<const float*>(PosBytes);
    const float* UVFloats  = (UVBytes != nullptr && UVType == 5126) ? reinterpret_cast<const float*>(UVBytes) : nullptr;

    for (int Idx = 0; Idx < PosCount; ++Idx)
    {
        const Mathematics::Axis3D P { PosFloats[Idx * 3 + 0], PosFloats[Idx * 3 + 1], PosFloats[Idx * 3 + 2] };
        if (!std::isfinite(P.X) || !std::isfinite(P.Y) || !std::isfinite(P.Z) ||
            std::abs(P.X) > 20000.0f || std::abs(P.Y) > 20000.0f || std::abs(P.Z) > 20000.0f)
        {
            OutDiagnostic = "glTF 2.0 POSITION contains non-finite or out-of-range coordinates.";
            return false;
        }
        const QuantizedPos Key {
            static_cast<int32_t>(std::llround(P.X * 100000.0)),
            static_cast<int32_t>(std::llround(P.Y * 100000.0)),
            static_cast<int32_t>(std::llround(P.Z * 100000.0)),
        };
        auto [It, Inserted] = WeldedLookup.emplace(Key, 0u);
        if (Inserted)
        {
            const Mathematics::Axis3D Converted = Mathematics::ConvertVectorBetweenBases(
                P,
                Mathematics::WorldCoordinateBasis::RightHandedYUp,
                Mathematics::QueryActiveWorldBasis());
            It->second = OutAsset.Network.AppendVertex(Converted);
        }
        RemappedVerts[static_cast<std::size_t>(Idx)] = It->second;

        if (UVFloats != nullptr && Idx < UVCount)
        {
            // glTF 2.0 stores V top-down (1.0 - V relative to OpenGL/UDIM bottom-left convention)
            const float U = UVFloats[Idx * 2 + 0];
            const float V = UVFloats[Idx * 2 + 1];
            if (!std::isfinite(U) || !std::isfinite(V) || std::abs(U) > 100000.0f || std::abs(V) > 100000.0f)
            {
                OutDiagnostic = "glTF 2.0 TEXCOORD_0 contains non-finite or out-of-range coordinates.";
                return false;
            }
            RawUVs[static_cast<std::size_t>(Idx)] = { U, 1.0f - V };
        }
        else
        {
            RawUVs[static_cast<std::size_t>(Idx)] = { P.X * 0.5f + 0.5f, P.Z * 0.5f + 0.5f };
        }
    }

    const uint16_t ImportedLayer = OutAsset.Layers.ConstructLayer("Imported Primary", { 0.94f, 0.86f, 0.62f }, 50.0f);

    int IndCount = 0;
    int IndType  = 0;
    uint64_t IndexByteSize = 1u;
    if (IndAccIdx >= 0 && static_cast<std::size_t>(IndAccIdx) < Accessors.size())
    {
        const int IndexComponentType = ExtractJsonIntField(Accessors[static_cast<std::size_t>(IndAccIdx)], "componentType", 0);
        IndexByteSize = (IndexComponentType == 5123) ? 2u : ((IndexComponentType == 5125) ? 4u : 1u);
    }
    const uint8_t* IndBytes = ResolveAccessorBytes(IndAccIdx, IndCount, IndType, IndexByteSize);
    if (IndAccIdx >= 0 && IndBytes == nullptr)
    {
        OutDiagnostic = "glTF 2.0 indices accessor exceeds its buffer view or binary buffer.";
        return false;
    }
    if (IndBytes != nullptr && IndType != 5121 && IndType != 5123 && IndType != 5125)
    {
        OutDiagnostic = "glTF 2.0 indices accessor uses an unsupported component type.";
        return false;
    }
    if (IndBytes != nullptr && (IndCount % 3) != 0)
    {
        OutDiagnostic = "glTF 2.0 triangle indices count is not divisible by three.";
        return false;
    }
    const int TriCount = (IndAccIdx >= 0) ? (IndCount / 3) : (PosCount / 3);

    for (int Tri = 0; Tri < TriCount; ++Tri)
    {
        uint32_t RawI[3]{};
        for (int C = 0; C < 3; ++C)
        {
            const int Elem = Tri * 3 + C;
            if (IndBytes == nullptr)
            {
                RawI[C] = static_cast<uint32_t>(Elem);
            }
            else if (IndType == 5123) // UNSIGNED_SHORT
            {
                RawI[C] = reinterpret_cast<const uint16_t*>(IndBytes)[Elem];
            }
            else if (IndType == 5125) // UNSIGNED_INT
            {
                RawI[C] = reinterpret_cast<const uint32_t*>(IndBytes)[Elem];
            }
            else
            {
                RawI[C] = IndBytes[Elem];
            }
        }
        if (RawI[0] >= static_cast<uint32_t>(PosCount) ||
            RawI[1] >= static_cast<uint32_t>(PosCount) ||
            RawI[2] >= static_cast<uint32_t>(PosCount))
        {
            continue;
        }
        const std::array<uint32_t, 3u> LoopV {{ RemappedVerts[RawI[0]], RemappedVerts[RawI[1]], RemappedVerts[RawI[2]] }};
        if (LoopV[0] == LoopV[1] || LoopV[1] == LoopV[2] || LoopV[2] == LoopV[0])
        {
            continue;
        }
        const std::array<Mathematics::UVAxes, 3u> LoopUV {{ RawUVs[RawI[0]], RawUVs[RawI[1]], RawUVs[RawI[2]] }};
        (void)OutAsset.Network.AppendFace(LoopV, LoopUV, {}, ImportedLayer, 0u);
    }

    if (OutAsset.Network.Faces.empty())
    {
        OutDiagnostic = "glTF 2.0 primitive yielded zero non-degenerate triangles.";
        return false;
    }

    OutAsset.Network.LinkOppositeEdges(true);
    OutAsset.Network.RecomputeNormals();
    FinalizeConstructedAssetUDIMs(OutAsset);
    return true;
}

std::string GeometryInterchange::ExportGltf2JsonText(const GeometrySpace::PolygonNetwork& Network)
{
    std::vector<float>    Positions {};
    std::vector<float>    Normals   {};
    std::vector<float>    Texcoords {};
    std::vector<uint32_t> Indices   {};

    Mathematics::Axis3D MinPos {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D MaxPos { -1.0e9f, -1.0e9f, -1.0e9f };

    for (const auto& Face : Network.Faces)
    {
        if (Face.CornerCount < 3u || Face.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            continue;
        }
        std::vector<uint32_t> FaceEdges {};
        FaceEdges.reserve(Face.CornerCount);
        uint32_t Cur = Face.BoundaryLoopEdgeIndex;
        bool FaceValid = true;
        for (uint16_t C = 0u; C < Face.CornerCount; ++C)
        {
            if (Cur >= Network.Edges.size())
            {
                FaceValid = false;
                break;
            }
            const auto& Edge = Network.Edges[Cur];
            if (Edge.OriginVertexIndex >= Network.Vertices.size())
            {
                FaceValid = false;
                break;
            }
            FaceEdges.push_back(Cur);
            Cur = Edge.NextEdgeIndex;
        }
        if (!FaceValid || FaceEdges.size() < 3u)
        {
            continue;
        }
        const uint32_t RootOut = static_cast<uint32_t>(Positions.size() / 3u);
        const auto ActiveBasis = Mathematics::QueryActiveWorldBasis();
        for (const uint32_t EIdx : FaceEdges)
        {
            const auto& Edge = Network.Edges[EIdx];
            const auto& Vert = Network.Vertices[Edge.OriginVertexIndex];
            const Mathematics::Axis3D P = Mathematics::ConvertVectorBetweenBases(
                Vert.Axes,
                ActiveBasis,
                Mathematics::WorldCoordinateBasis::RightHandedYUp);
            const Mathematics::Axis3D N = Mathematics::ConvertVectorBetweenBases(
                Vert.Normal,
                ActiveBasis,
                Mathematics::WorldCoordinateBasis::RightHandedYUp);
            Positions.insert(Positions.end(), { P.X, P.Y, P.Z });
            Normals.insert(Normals.end(), { N.X, N.Y, N.Z });
            Texcoords.insert(Texcoords.end(), { Edge.ParameterUV.U, 1.0f - Edge.ParameterUV.V });

            MinPos.X = std::min(MinPos.X, P.X);
            MinPos.Y = std::min(MinPos.Y, P.Y);
            MinPos.Z = std::min(MinPos.Z, P.Z);
            MaxPos.X = std::max(MaxPos.X, P.X);
            MaxPos.Y = std::max(MaxPos.Y, P.Y);
            MaxPos.Z = std::max(MaxPos.Z, P.Z);
        }
        for (std::size_t Tri = 1u; Tri + 1u < FaceEdges.size(); ++Tri)
        {
            Indices.push_back(RootOut);
            Indices.push_back(RootOut + static_cast<uint32_t>(Tri));
            Indices.push_back(RootOut + static_cast<uint32_t>(Tri + 1u));
        }
    }

    if (Positions.empty() || Indices.empty())
    {
        return {};
    }

    const std::size_t PosBytes = Positions.size() * sizeof(float);
    const std::size_t NrmBytes = Normals.size() * sizeof(float);
    const std::size_t TexBytes = Texcoords.size() * sizeof(float);
    const std::size_t IndBytes = Indices.size() * sizeof(uint32_t);
    const std::size_t TotalLen = PosBytes + NrmBytes + TexBytes + IndBytes;

    std::vector<uint8_t> Combined(TotalLen);
    std::size_t Offset = 0u;
    std::memcpy(Combined.data() + Offset, Positions.data(), PosBytes); Offset += PosBytes;
    std::memcpy(Combined.data() + Offset, Normals.data(), NrmBytes);   Offset += NrmBytes;
    std::memcpy(Combined.data() + Offset, Texcoords.data(), TexBytes); Offset += TexBytes;
    std::memcpy(Combined.data() + Offset, Indices.data(), IndBytes);

    const std::string Radix64Uri = "data:application/octet-stream;base64," + EncodeRadix64(Combined.data(), Combined.size());
    const std::size_t VertCount = Positions.size() / 3u;

    std::ostringstream Json;
    Json.setf(std::ios::fixed);
    Json.precision(6);
    Json << "{\n"
         << "  \"asset\": { \"version\": \"2.0\", \"generator\": \"Frontier GeometryInterchange\" },\n"
         << "  \"scene\": 0,\n"
         << "  \"scenes\": [{ \"nodes\": [0] }],\n"
         << "  \"nodes\": [{ \"name\": \"" << Network.NetworkTitle << "\", \"mesh\": 0 }],\n"
         << "  \"meshes\": [{\n"
         << "    \"name\": \"" << Network.NetworkTitle << "\",\n"
         << "    \"primitives\": [{\n"
         << "      \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 },\n"
         << "      \"indices\": 3\n"
         << "    }]\n"
         << "  }],\n"
         << "  \"buffers\": [{ \"byteLength\": " << TotalLen << ", \"uri\": \"" << Radix64Uri << "\" }],\n"
         << "  \"bufferViews\": [\n"
         << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": " << PosBytes << " },\n"
         << "    { \"buffer\": 0, \"byteOffset\": " << PosBytes << ", \"byteLength\": " << NrmBytes << " },\n"
         << "    { \"buffer\": 0, \"byteOffset\": " << (PosBytes + NrmBytes) << ", \"byteLength\": " << TexBytes << " },\n"
         << "    { \"buffer\": 0, \"byteOffset\": " << (PosBytes + NrmBytes + TexBytes) << ", \"byteLength\": " << IndBytes << " }\n"
         << "  ],\n"
         << "  \"accessors\": [\n"
         << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": " << VertCount << ", \"type\": \"VEC3\", "
         << "\"min\": [" << MinPos.X << ", " << MinPos.Y << ", " << MinPos.Z << "], "
         << "\"max\": [" << MaxPos.X << ", " << MaxPos.Y << ", " << MaxPos.Z << "] },\n"
         << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": " << VertCount << ", \"type\": \"VEC3\" },\n"
         << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": " << VertCount << ", \"type\": \"VEC2\" },\n"
         << "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": " << Indices.size() << ", \"type\": \"SCALAR\" }\n"
         << "  ]\n"
         << "}\n";
    return Json.str();
}

} // namespace Frontier::Interchange
