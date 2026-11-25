/*============================================================================================================================================
                                                          VERTEXATLASCODEC.CPP
============================================================================================================================================*/
// 🧩 Phase D07 .VertexAtlas two-tier codec implementation (TOML header + 64B-aligned Tier-2 RGBA32F texels & slice poses)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/VertexAtlasCodec.h"

#include <cstring>

namespace Frontier::Codecs::Simulation
{

namespace
{

namespace DEF = Frontier::GeometrySpace::Deformation;
namespace EL  = Frontier::SimulationSpace::EffectLayers;

void AppendRaw(std::vector<std::byte>& dst, const void* ptr, size_t n)
{
    const auto* p = reinterpret_cast<const std::byte*>(ptr);
    dst.insert(dst.end(), p, p + n);
}

[[nodiscard]] bool ReadRaw(std::span<const std::byte> src, size_t& cur, void* dst, size_t n) noexcept
{
    if (cur + n > src.size()) return false;
    std::memcpy(dst, src.data() + cur, n);
    cur += n;
    return true;
}

[[nodiscard]] const EL::TomlTable* FindSubTable(const EL::TomlTable& root, std::string_view key) noexcept
{
    const EL::TomlValue* v = root.Find(key);
    return (v != nullptr) ? v->AsTable() : nullptr;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      VERTEX ATLAS CODEC
//------------------------------------------------------------------------------------------------------------------------

bool VertexAtlasCodec::EncodeToBytes(
    const VertexAtlasDocumentModel& model,
    std::vector<std::byte>& outBytes,
    std::string* outError)
{
    const DEF::VertexMotionAtlas& atlas = model.Atlas;
    if (!atlas.Ready())
    {
        if (outError) *outError = "VertexMotionAtlas is empty or not ready for encoding.";
        return false;
    }

    std::string toml;
    toml.reserve(1024u);
    toml += "# Frontier Engine — .VertexAtlas Baked Vertex Motion Atlas\n";
    toml += "[Container]\n";
    toml += "FormatSuffix = \".VertexAtlas\"\n";
    toml += "SchemaVersion = " + std::to_string(model.SchemaVersion) + "\n";
    toml += "DocumentTitle = " + EscapeTomlQuotedString(model.DocumentTitle) + "\n";
    toml += "SolverLabel = " + EscapeTomlQuotedString(atlas.SolverLabel) + "\n\n";

    toml += "[AtlasLayout]\n";
    toml += "VertexCount = " + std::to_string(atlas.VertexCount) + "\n";
    toml += "TriangleCount = " + std::to_string(atlas.TriangleCount) + "\n";
    toml += "SliceCount = " + std::to_string(atlas.SliceCount) + "\n";
    toml += "AtlasWidth = " + std::to_string(atlas.AtlasWidth) + "\n";
    toml += "RowsPerSlice = " + std::to_string(atlas.RowsPerSlice) + "\n";
    toml += "AtlasHeight = " + std::to_string(atlas.AtlasHeight) + "\n";
    toml += "SlicesPerSecond = " + FormatCanonicalFloat(atlas.SlicesPerSecond) + "\n";
    toml += "DisplacementMinX = " + FormatCanonicalFloat(atlas.DisplacementMin[0]) + "\n";
    toml += "DisplacementMinY = " + FormatCanonicalFloat(atlas.DisplacementMin[1]) + "\n";
    toml += "DisplacementMinZ = " + FormatCanonicalFloat(atlas.DisplacementMin[2]) + "\n";
    toml += "DisplacementMaxX = " + FormatCanonicalFloat(atlas.DisplacementMax[0]) + "\n";
    toml += "DisplacementMaxY = " + FormatCanonicalFloat(atlas.DisplacementMax[1]) + "\n";
    toml += "DisplacementMaxZ = " + FormatCanonicalFloat(atlas.DisplacementMax[2]) + "\n";

    std::vector<SimulationBinarySection> sections;
    sections.reserve(3u);

    {
        SimulationBinarySection sec {};
        sec.Identifier   = "PositionStrainRgba32f";
        sec.SubFormatTag = ".VertexAtlas";
        AppendRaw(sec.PayloadBytes, atlas.PositionStrainRgba.data(), atlas.PositionStrainRgba.size() * sizeof(float));
        sec.ChecksumFnv64 = ComputeSimulationFnv1a64(sec.PayloadBytes);
        sections.push_back(std::move(sec));
    }
    {
        SimulationBinarySection sec {};
        sec.Identifier   = "NormalRgba32f";
        sec.SubFormatTag = ".VertexAtlas";
        AppendRaw(sec.PayloadBytes, atlas.NormalRgba.data(), atlas.NormalRgba.size() * sizeof(float));
        sec.ChecksumFnv64 = ComputeSimulationFnv1a64(sec.PayloadBytes);
        sections.push_back(std::move(sec));
    }
    {
        SimulationBinarySection sec {};
        sec.Identifier   = "RigidSlicePoses";
        sec.SubFormatTag = ".VertexAtlas";
        AppendRaw(sec.PayloadBytes, atlas.SlicePoses.data(), atlas.SlicePoses.size() * sizeof(DEF::RigidSlicePose));
        sec.ChecksumFnv64 = ComputeSimulationFnv1a64(sec.PayloadBytes);
        sections.push_back(std::move(sec));
    }

    return PackTwoTierSimulationBytes(toml, sections, outBytes, outError);
}

bool VertexAtlasCodec::DecodeFromBytes(
    std::span<const std::byte> bytes,
    VertexAtlasDocumentModel& outModel,
    std::string* outError)
{
    std::string tomlText;
    std::vector<SimulationBinarySection> sections;
    if (!UnpackTwoTierSimulationBytes(bytes, tomlText, sections, outError))
    {
        return false;
    }

    std::string parseErr;
    const auto parsedRoot = EL::ParseTomlSubset(tomlText, &parseErr);
    if (!parsedRoot.has_value())
    {
        if (outError) *outError = "VertexAtlas TOML parse failed: " + parseErr;
        return false;
    }
    const EL::TomlTable& root = *parsedRoot;

    outModel = VertexAtlasDocumentModel {};
    DEF::VertexMotionAtlas& atlas = outModel.Atlas;

    if (const auto* c = FindSubTable(root, "Container"))
    {
        if (const auto* v = c->Find("DocumentTitle"); v && v->IsString()) outModel.DocumentTitle = std::string(v->AsString());
        if (const auto* v = c->Find("SolverLabel"); v && v->IsString()) atlas.SolverLabel = std::string(v->AsString());
        if (const auto* v = c->Find("SchemaVersion"); v && v->IsInt()) outModel.SchemaVersion = static_cast<uint32_t>(v->AsInt64(1));
    }

    const auto readU32 = [](const EL::TomlTable* t, const char* k, uint32_t& dst)
    {
        if (t) { if (const auto* v = t->Find(k); v && v->IsInt()) dst = static_cast<uint32_t>(v->AsInt64(dst)); }
    };
    const auto readF32 = [](const EL::TomlTable* t, const char* k, float& dst)
    {
        if (t) { if (const auto* v = t->Find(k); v && v->IsNumber()) dst = static_cast<float>(v->AsDouble(dst)); }
    };

    if (const auto* l = FindSubTable(root, "AtlasLayout"))
    {
        readU32(l, "VertexCount", atlas.VertexCount);
        readU32(l, "TriangleCount", atlas.TriangleCount);
        readU32(l, "SliceCount", atlas.SliceCount);
        readU32(l, "AtlasWidth", atlas.AtlasWidth);
        readU32(l, "RowsPerSlice", atlas.RowsPerSlice);
        readU32(l, "AtlasHeight", atlas.AtlasHeight);
        readF32(l, "SlicesPerSecond", atlas.SlicesPerSecond);
        readF32(l, "DisplacementMinX", atlas.DisplacementMin[0]);
        readF32(l, "DisplacementMinY", atlas.DisplacementMin[1]);
        readF32(l, "DisplacementMinZ", atlas.DisplacementMin[2]);
        readF32(l, "DisplacementMaxX", atlas.DisplacementMax[0]);
        readF32(l, "DisplacementMaxY", atlas.DisplacementMax[1]);
        readF32(l, "DisplacementMaxZ", atlas.DisplacementMax[2]);
    }

    const size_t expectedTexelFloats = static_cast<size_t>(atlas.AtlasWidth) * atlas.AtlasHeight * 4u;
    for (const auto& sec : sections)
    {
        size_t cur = 0u;
        if (sec.Identifier == "PositionStrainRgba32f")
        {
            atlas.PositionStrainRgba.resize(expectedTexelFloats);
            if (!ReadRaw(sec.PayloadBytes, cur, atlas.PositionStrainRgba.data(), expectedTexelFloats * sizeof(float)))
            {
                if (outError) *outError = "VertexAtlas PositionStrainRgba32f size mismatch.";
                return false;
            }
        }
        else if (sec.Identifier == "NormalRgba32f")
        {
            atlas.NormalRgba.resize(expectedTexelFloats);
            if (!ReadRaw(sec.PayloadBytes, cur, atlas.NormalRgba.data(), expectedTexelFloats * sizeof(float)))
            {
                if (outError) *outError = "VertexAtlas NormalRgba32f size mismatch.";
                return false;
            }
        }
        else if (sec.Identifier == "RigidSlicePoses")
        {
            atlas.SlicePoses.resize(atlas.SliceCount);
            if (!ReadRaw(sec.PayloadBytes, cur, atlas.SlicePoses.data(), static_cast<size_t>(atlas.SliceCount) * sizeof(DEF::RigidSlicePose)))
            {
                if (outError) *outError = "VertexAtlas RigidSlicePoses size mismatch.";
                return false;
            }
        }
    }

    if (!atlas.Ready())
    {
        if (outError) *outError = "VertexAtlas decoded incomplete payload.";
        return false;
    }
    return true;
}

bool VertexAtlasCodec::EncodeToFile(
    const std::filesystem::path& targetPath,
    const VertexAtlasDocumentModel& model,
    std::string* outError)
{
    std::vector<std::byte> bytes;
    if (!EncodeToBytes(model, bytes, outError))
    {
        return false;
    }
    return WriteSimulationBytesToFile(targetPath, bytes, outError);
}

bool VertexAtlasCodec::DecodeFromFile(
    const std::filesystem::path& sourcePath,
    VertexAtlasDocumentModel& outModel,
    std::string* outError)
{
    std::vector<std::byte> bytes;
    if (!ReadSimulationBytesFromFile(sourcePath, bytes, outError))
    {
        return false;
    }
    return DecodeFromBytes(bytes, outModel, outError);
}

} // namespace Frontier::Codecs::Simulation
