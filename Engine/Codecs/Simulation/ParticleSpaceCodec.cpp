/*============================================================================================================================================
                                                         PARTICLESPACECODEC.CPP
============================================================================================================================================*/
// 🧩 Stage F (S28) .ParticleSpace two-tier codec implementation
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.1

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/ParticleSpaceCodec.h"

#include <cstring>

namespace Frontier::Codecs::Simulation
{

namespace
{

namespace EL = Frontier::SimulationSpace::EffectLayers;
namespace PS = Frontier::ParticleSpace;

void AppendBytes(std::vector<std::byte>& dst, const void* ptr, const std::size_t n)
{
    const auto* p = reinterpret_cast<const std::byte*>(ptr);
    dst.insert(dst.end(), p, p + n);
}

[[nodiscard]] bool ReadBytes(const std::span<const std::byte> src, std::size_t& cur, void* dst, const std::size_t n) noexcept
{
    if (cur + n > src.size()) return false;
    std::memcpy(dst, src.data() + cur, n);
    cur += n;
    return true;
}

void PackXpbdTablesBinary(const PS::XpbdPackedGpuTables& tbl, std::vector<std::byte>& out)
{
    out.clear();
    const uint32_t uCount = static_cast<uint32_t>(tbl.U32Topology.size());
    const uint32_t fCount = static_cast<uint32_t>(tbl.F32Rest.size());
    out.reserve(24u + static_cast<std::size_t>(uCount) * sizeof(uint32_t) + static_cast<std::size_t>(fCount) * sizeof(float));

    AppendBytes(out, &tbl.TotalDuals, sizeof(uint32_t));
    AppendBytes(out, &tbl.HingeDualOffset, sizeof(uint32_t));
    AppendBytes(out, &tbl.SeamDualOffset, sizeof(uint32_t));
    AppendBytes(out, &tbl.DistanceDualOffset, sizeof(uint32_t));
    AppendBytes(out, &uCount, sizeof(uint32_t));
    AppendBytes(out, &fCount, sizeof(uint32_t));
    if (uCount > 0u) AppendBytes(out, tbl.U32Topology.data(), static_cast<std::size_t>(uCount) * sizeof(uint32_t));
    if (fCount > 0u) AppendBytes(out, tbl.F32Rest.data(), static_cast<std::size_t>(fCount) * sizeof(float));
}

[[nodiscard]] bool UnpackXpbdTablesBinary(const std::span<const std::byte> src, PS::XpbdPackedGpuTables& out) noexcept
{
    std::size_t cur = 0u;
    uint32_t uCount = 0u, fCount = 0u;
    if (!ReadBytes(src, cur, &out.TotalDuals, sizeof(uint32_t))) return false;
    if (!ReadBytes(src, cur, &out.HingeDualOffset, sizeof(uint32_t))) return false;
    if (!ReadBytes(src, cur, &out.SeamDualOffset, sizeof(uint32_t))) return false;
    if (!ReadBytes(src, cur, &out.DistanceDualOffset, sizeof(uint32_t))) return false;
    if (!ReadBytes(src, cur, &uCount, sizeof(uint32_t))) return false;
    if (!ReadBytes(src, cur, &fCount, sizeof(uint32_t))) return false;

    out.U32Topology.resize(uCount);
    out.F32Rest.resize(fCount);
    if (uCount > 0u && !ReadBytes(src, cur, out.U32Topology.data(), static_cast<std::size_t>(uCount) * sizeof(uint32_t))) return false;
    if (fCount > 0u && !ReadBytes(src, cur, out.F32Rest.data(), static_cast<std::size_t>(fCount) * sizeof(float))) return false;
    return cur == src.size();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTICLESPACECODEC
//------------------------------------------------------------------------------------------------------------------------

bool ParticleSpaceCodec::EncodeToBytes(
    const EL::EffectLayerSpecificationSet& specs,
    const ParticleSpaceDocumentModel& model,
    std::vector<std::byte>& outBytes,
    std::string* const outError)
{
    std::string toml;
    toml.reserve(4096u);
    toml += "[Document]\n";
    toml += "Format = \"frontier-particle-space\"\n";
    toml += "SchemaVersion = " + std::to_string(model.SchemaVersion) + "\n";
    toml += "Title = " + EscapeTomlQuotedString(model.DocumentTitle) + "\n";
    toml += "NodeId = " + EscapeTomlQuotedString(model.NodeId) + "\n";
    toml += "TemplateId = " + EscapeTomlQuotedString(model.TemplateId) + "\n";
    toml += "Enabled = " + std::string(model.Enabled ? "true" : "false") + "\n";
    toml += "Visible = " + std::string(model.Visible ? "true" : "false") + "\n";

    AppendEffectLayerStackToToml(toml, specs, model.Stack);
    AppendKeyTracksToToml(toml, model.Tracks);

    std::vector<SimulationBinarySection> sections;
    if (model.HasXpbdTables)
    {
        SimulationBinarySection sec {};
        sec.Identifier   = "XpbdPackedTables";
        sec.SubFormatTag = ".ConstraintSet";
        PackXpbdTablesBinary(model.XpbdTables, sec.PayloadBytes);
        sections.push_back(std::move(sec));
    }
    if (!model.CustomSolidBytes.empty())
    {
        SimulationBinarySection sec {};
        sec.Identifier   = "CustomCarrierSolid";
        sec.SubFormatTag = ".GeometrySpace";
        sec.PayloadBytes = model.CustomSolidBytes;
        sections.push_back(std::move(sec));
    }
    return PackTwoTierSimulationBytes(toml, sections, outBytes, outError);
}

bool ParticleSpaceCodec::DecodeFromBytes(
    const EL::EffectLayerSpecificationSet& specs,
    const std::span<const std::byte> bytes,
    ParticleSpaceDocumentModel& outModel,
    std::string* const outError)
{
    std::string tomlText;
    std::vector<SimulationBinarySection> sections;
    if (!UnpackTwoTierSimulationBytes(bytes, tomlText, sections, outError)) return false;

    const auto parsed = EL::ParseTomlSubset(tomlText, outError);
    if (!parsed.has_value()) return false;

    outModel = ParticleSpaceDocumentModel {};
    if (const auto* docVal = parsed->Find("Document"); docVal != nullptr && docVal->IsTable())
    {
        const auto& doc = *docVal->AsTable();
        if (const auto* fmt = doc.Find("Format"); fmt == nullptr || fmt->AsString() != "frontier-particle-space")
        {
            if (outError) *outError = "Document.Format must be frontier-particle-space";
            return false;
        }
        if (const auto* sv = doc.Find("SchemaVersion")) outModel.SchemaVersion = static_cast<uint32_t>(sv->AsInt64(1));
        if (const auto* tt = doc.Find("Title"))         outModel.DocumentTitle = std::string(tt->AsString());
        if (const auto* id = doc.Find("NodeId"))        outModel.NodeId        = std::string(id->AsString());
        if (const auto* ti = doc.Find("TemplateId"))    outModel.TemplateId    = std::string(ti->AsString());
        if (const auto* en = doc.Find("Enabled"))       outModel.Enabled       = en->AsBool(true);
        if (const auto* vi = doc.Find("Visible"))       outModel.Visible       = vi->AsBool(true);
    }
    else
    {
        if (outError) *outError = "Missing [Document] table in .ParticleSpace";
        return false;
    }

    if (!ParseEffectLayerStackFromToml(specs, *parsed, EL::LayerStackOrdinal::ParticleCloud, outModel.Stack, outError))
    {
        return false;
    }
    outModel.Tracks = ParseKeyTracksFromToml(*parsed);

    for (const auto& sec : sections)
    {
        if (sec.Identifier == "XpbdPackedTables")
        {
            if (!UnpackXpbdTablesBinary(sec.PayloadBytes, outModel.XpbdTables))
            {
                if (outError) *outError = "Corrupt XpbdPackedTables binary payload";
                return false;
            }
            outModel.HasXpbdTables = true;
        }
        else if (sec.Identifier == "CustomCarrierSolid")
        {
            outModel.CustomSolidBytes = sec.PayloadBytes;
        }
    }
    return true;
}

bool ParticleSpaceCodec::EncodeToFile(
    const std::filesystem::path& targetPath,
    const EL::EffectLayerSpecificationSet& specs,
    const ParticleSpaceDocumentModel& model,
    std::string* const outError)
{
    std::vector<std::byte> bytes;
    if (!EncodeToBytes(specs, model, bytes, outError)) return false;
    return WriteSimulationBytesToFile(targetPath, bytes, outError);
}

bool ParticleSpaceCodec::DecodeFromFile(
    const std::filesystem::path& sourcePath,
    const EL::EffectLayerSpecificationSet& specs,
    ParticleSpaceDocumentModel& outModel,
    std::string* const outError)
{
    std::vector<std::byte> bytes;
    if (!ReadSimulationBytesFromFile(sourcePath, bytes, outError)) return false;
    return DecodeFromBytes(specs, bytes, outModel, outError);
}

} // namespace Frontier::Codecs::Simulation
