/*============================================================================================================================================
                                                           SOFTBODYCODEC.CPP
============================================================================================================================================*/
// 🧩 Phase D07 .SoftBody two-tier codec implementation (TOML Tier-1 header + optional 64B-aligned Tier-2 rest-cage binary)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/SoftBodyCodec.h"
#include "Engine/SimulationSpace/Deformable/ConflictColorPartitioner.h"

#include <cstring>

namespace Frontier::Codecs::Simulation
{

namespace
{

namespace DF = Frontier::SimulationSpace::Deformable;
namespace EL = Frontier::SimulationSpace::EffectLayers;

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

[[nodiscard]] std::string_view FormatPresetKindToken(DF::DeformablePresetKind kind) noexcept
{
    switch (kind)
    {
        case DF::DeformablePresetKind::SportTireFem:       return "SportTireFem";
        case DF::DeformablePresetKind::SportTirePneumatic: return "SportTirePneumatic";
        case DF::DeformablePresetKind::BotanicalFerns:     return "BotanicalFerns";
        case DF::DeformablePresetKind::OrbSpiderweb:       return "OrbSpiderweb";
        case DF::DeformablePresetKind::CablesChains:       return "CablesChains";
        case DF::DeformablePresetKind::HangingVines:       return "HangingVines";
        case DF::DeformablePresetKind::MarshReeds:         return "MarshReeds";
        case DF::DeformablePresetKind::UnderwaterKelp:     return "UnderwaterKelp";
        case DF::DeformablePresetKind::WovenCloth:         return "WovenCloth";
    }
    return "SportTireFem";
}

[[nodiscard]] DF::DeformablePresetKind ParsePresetKindToken(std::string_view token) noexcept
{
    if (token == "SportTirePneumatic") return DF::DeformablePresetKind::SportTirePneumatic;
    if (token == "BotanicalFerns")     return DF::DeformablePresetKind::BotanicalFerns;
    if (token == "OrbSpiderweb")       return DF::DeformablePresetKind::OrbSpiderweb;
    if (token == "CablesChains")       return DF::DeformablePresetKind::CablesChains;
    if (token == "HangingVines")       return DF::DeformablePresetKind::HangingVines;
    if (token == "MarshReeds")         return DF::DeformablePresetKind::MarshReeds;
    if (token == "UnderwaterKelp")     return DF::DeformablePresetKind::UnderwaterKelp;
    if (token == "WovenCloth")         return DF::DeformablePresetKind::WovenCloth;
    return DF::DeformablePresetKind::SportTireFem;
}

[[nodiscard]] std::string_view InferSolverCategory(DF::DeformablePresetKind kind) noexcept
{
    switch (kind)
    {
        case DF::DeformablePresetKind::SportTireFem:       return "FemVolumetric3D";
        case DF::DeformablePresetKind::SportTirePneumatic: return "PneumaticXpbd";
        case DF::DeformablePresetKind::WovenCloth:         return "XpbdCloth";
        default:                                           return "XpbdStrand";
    }
}

void PackRestCageBinary(const DF::DeformableBody& body, std::vector<std::byte>& out)
{
    out.clear();
    const uint32_t pCount = body.PointCount;
    const uint32_t dCount = static_cast<uint32_t>(body.DistanceConstraints.size());
    const uint32_t bCount = static_cast<uint32_t>(body.BendConstraints.size());
    const uint32_t tCount = body.TetCount;

    AppendRaw(out, &pCount, sizeof(uint32_t));
    AppendRaw(out, &dCount, sizeof(uint32_t));
    AppendRaw(out, &bCount, sizeof(uint32_t));
    AppendRaw(out, &tCount, sizeof(uint32_t));
    AppendRaw(out, &body.GroundY, sizeof(float));

    if (pCount > 0u)
    {
        AppendRaw(out, body.Positions.data(), static_cast<size_t>(pCount) * 3u * sizeof(float));
        AppendRaw(out, body.InverseMass.data(), static_cast<size_t>(pCount) * sizeof(float));
    }
    if (dCount > 0u)
    {
        AppendRaw(out, body.DistanceConstraints.data(), static_cast<size_t>(dCount) * sizeof(DF::DistanceConstraintEntry));
    }
    if (bCount > 0u)
    {
        AppendRaw(out, body.BendConstraints.data(), static_cast<size_t>(bCount) * sizeof(DF::BendConstraintEntry));
    }
    if (tCount > 0u)
    {
        AppendRaw(out, body.TetIndices.data(), static_cast<size_t>(tCount) * 4u * sizeof(uint32_t));
        AppendRaw(out, body.TetInverseRestDm.data(), static_cast<size_t>(tCount) * 9u * sizeof(float));
        AppendRaw(out, body.TetRestVolume.data(), static_cast<size_t>(tCount) * sizeof(float));
    }
}

[[nodiscard]] bool UnpackRestCageBinary(std::span<const std::byte> src, DF::DeformableBody& out)
{
    size_t cur = 0u;
    uint32_t pCount = 0u, dCount = 0u, bCount = 0u, tCount = 0u;
    if (!ReadRaw(src, cur, &pCount, sizeof(uint32_t))) return false;
    if (!ReadRaw(src, cur, &dCount, sizeof(uint32_t))) return false;
    if (!ReadRaw(src, cur, &bCount, sizeof(uint32_t))) return false;
    if (!ReadRaw(src, cur, &tCount, sizeof(uint32_t))) return false;
    if (!ReadRaw(src, cur, &out.GroundY, sizeof(float))) return false;

    out.ResetAll();
    out.PointCount = pCount;
    out.Positions.resize(static_cast<size_t>(pCount) * 3u, 0.0f);
    out.InverseMass.resize(pCount, 1.0f);
    if (pCount > 0u)
    {
        if (!ReadRaw(src, cur, out.Positions.data(), out.Positions.size() * sizeof(float))) return false;
        if (!ReadRaw(src, cur, out.InverseMass.data(), out.InverseMass.size() * sizeof(float))) return false;
    }
    out.PrevPositions = out.Positions;
    out.RestPositions = out.Positions;
    out.Velocities.assign(out.Positions.size(), 0.0f);
    out.ContactRadius.assign(pCount, 0.025f);
    out.AeroDragScale.assign(pCount, 1.0f);
    out.PointStrain.assign(pCount, 0.0f);

    out.DistanceConstraints.resize(dCount);
    out.DistanceDuals.assign(dCount, 0.0f);
    if (dCount > 0u && !ReadRaw(src, cur, out.DistanceConstraints.data(), static_cast<size_t>(dCount) * sizeof(DF::DistanceConstraintEntry)))
    {
        return false;
    }
    out.BendConstraints.resize(bCount);
    out.BendDuals.assign(bCount, 0.0f);
    if (bCount > 0u && !ReadRaw(src, cur, out.BendConstraints.data(), static_cast<size_t>(bCount) * sizeof(DF::BendConstraintEntry)))
    {
        return false;
    }

    out.TetCount = tCount;
    out.TetIndices.resize(static_cast<size_t>(tCount) * 4u, 0u);
    out.TetInverseRestDm.resize(static_cast<size_t>(tCount) * 9u, 0.0f);
    out.TetRestVolume.resize(tCount, 0.0f);
    out.TetStrain.assign(tCount, 0.0f);
    if (tCount > 0u)
    {
        if (!ReadRaw(src, cur, out.TetIndices.data(), out.TetIndices.size() * sizeof(uint32_t))) return false;
        if (!ReadRaw(src, cur, out.TetInverseRestDm.data(), out.TetInverseRestDm.size() * sizeof(float))) return false;
        if (!ReadRaw(src, cur, out.TetRestVolume.data(), out.TetRestVolume.size() * sizeof(float))) return false;
    }
    out.TetCofactorNormals.assign(static_cast<size_t>(tCount) * 9u, 0.0f);
    for (uint32_t t = 0u; t < tCount; ++t)
    {
        out.TetCofactorNormals[static_cast<size_t>(t) * 9u + 0u] = 1.0f;
        out.TetCofactorNormals[static_cast<size_t>(t) * 9u + 4u] = 1.0f;
        out.TetCofactorNormals[static_cast<size_t>(t) * 9u + 8u] = 1.0f;
    }

    DF::ConflictColorPartitioner::PartitionAllConstraints(out);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        SOFT BODY CODEC
//------------------------------------------------------------------------------------------------------------------------

SoftBodyDocumentModel SoftBodyCodec::BuildFromPreset(
    DF::DeformablePresetKind kind,
    bool embedRestCageBinary)
{
    SoftBodyDocumentModel model {};
    const auto presets = DF::DeformablePresetRegistry::EnumeratePresets();
    const DF::DeformablePresetDescriptor* found = nullptr;
    for (const auto& p : presets)
    {
        if (p.Kind == kind)
        {
            found = &p;
            break;
        }
    }
    if (found != nullptr)
    {
        model.DocumentTitle = std::string(found->DisplayTitle);
        model.PresetId      = std::string(found->FileStem);
        model.Configuration = found->DefaultConfig;
        model.Description   = std::string(found->TagLabel) + " deformable preset (" + std::string(found->PresetId) + ")";
    }
    else
    {
        model.PresetId = std::string(FormatPresetKindToken(kind));
    }

    model.SolverCategory      = std::string(InferSolverCategory(kind));
    model.SchemaVersion       = kSimulationCodecSchemaVersion;
    model.PresetKind          = kind;
    model.EmbedRestCageBinary = embedRestCageBinary;

    if (embedRestCageBinary)
    {
        DF::DeformablePresetRegistry::BuildPreset(kind, model.EmbeddedRestBody, model.Configuration);
    }
    return model;
}

bool SoftBodyCodec::EncodeToBytes(
    const SoftBodyDocumentModel& model,
    std::vector<std::byte>& outBytes,
    std::string* outError)
{
    const auto& cfg = model.Configuration;
    std::string toml;
    toml.reserve(2048u);

    toml += "# Frontier Engine — .SoftBody Deformable Simulation Preset\n";
    toml += "[Container]\n";
    toml += "FormatSuffix = \".SoftBody\"\n";
    toml += "SchemaVersion = " + std::to_string(model.SchemaVersion) + "\n";
    toml += "DocumentTitle = " + EscapeTomlQuotedString(model.DocumentTitle) + "\n";
    toml += "PresetId = " + EscapeTomlQuotedString(model.PresetId) + "\n";
    toml += "PresetKind = " + EscapeTomlQuotedString(FormatPresetKindToken(model.PresetKind)) + "\n";
    toml += "SolverCategory = " + EscapeTomlQuotedString(model.SolverCategory) + "\n";
    toml += "Description = " + EscapeTomlQuotedString(model.Description) + "\n\n";

    toml += "[Solver]\n";
    toml += "Substeps = " + std::to_string(cfg.Substeps) + "\n";
    toml += "GravityY = " + FormatCanonicalFloat(cfg.GravityY) + "\n";
    toml += "Damping = " + FormatCanonicalFloat(cfg.Damping) + "\n";
    toml += "UnderwaterEnabled = " + std::string(cfg.UnderwaterEnabled ? "true" : "false") + "\n\n";

    toml += "[Material]\n";
    toml += "StretchStiffness = " + FormatCanonicalFloat(cfg.StretchStiffness) + "\n";
    toml += "BendStiffness = " + FormatCanonicalFloat(cfg.BendStiffness) + "\n";
    toml += "FemYoungMpa = " + FormatCanonicalFloat(cfg.FemYoungMpa) + "\n";
    toml += "FemPoissonNu = " + FormatCanonicalFloat(cfg.FemPoissonNu) + "\n";
    toml += "TearRatio = " + FormatCanonicalFloat(cfg.TearRatio) + "\n\n";

    toml += "[Environment]\n";
    toml += "WindStrength = " + FormatCanonicalFloat(cfg.WindStrength) + "\n";
    toml += "Turbulence = " + FormatCanonicalFloat(cfg.Turbulence) + "\n";
    toml += "ColliderEnabled = " + std::string(cfg.ColliderEnabled ? "true" : "false") + "\n\n";

    toml += "[Tire]\n";
    toml += "PressurePsi = " + FormatCanonicalFloat(cfg.TirePressurePsi) + "\n";
    toml += "AxleLoad = " + FormatCanonicalFloat(cfg.TireAxleLoad) + "\n";
    toml += "SpinRadPerSec = " + FormatCanonicalFloat(cfg.TireSpinRadPerSec) + "\n";

    std::vector<SimulationBinarySection> sections;
    if (model.EmbedRestCageBinary && model.EmbeddedRestBody.PointCount > 0u)
    {
        SimulationBinarySection sec {};
        sec.Identifier   = "RestCageBinary";
        sec.SubFormatTag = ".SoftBody";
        PackRestCageBinary(model.EmbeddedRestBody, sec.PayloadBytes);
        sec.ChecksumFnv64 = ComputeSimulationFnv1a64(sec.PayloadBytes);
        sections.push_back(std::move(sec));
    }

    return PackTwoTierSimulationBytes(toml, sections, outBytes, outError);
}

bool SoftBodyCodec::DecodeFromBytes(
    std::span<const std::byte> bytes,
    SoftBodyDocumentModel& outModel,
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
        if (outError) *outError = "SoftBody TOML parse failed: " + parseErr;
        return false;
    }
    const EL::TomlTable& root = *parsedRoot;

    outModel = SoftBodyDocumentModel {};
    if (const auto* container = FindSubTable(root, "Container"))
    {
        if (const auto* v = container->Find("DocumentTitle"); v && v->IsString()) outModel.DocumentTitle = std::string(v->AsString());
        if (const auto* v = container->Find("PresetId"); v && v->IsString()) outModel.PresetId = std::string(v->AsString());
        if (const auto* v = container->Find("PresetKind"); v && v->IsString()) outModel.PresetKind = ParsePresetKindToken(v->AsString());
        if (const auto* v = container->Find("SolverCategory"); v && v->IsString()) outModel.SolverCategory = std::string(v->AsString());
        if (const auto* v = container->Find("Description"); v && v->IsString()) outModel.Description = std::string(v->AsString());
        if (const auto* v = container->Find("SchemaVersion"); v && v->IsInt()) outModel.SchemaVersion = static_cast<uint32_t>(v->AsInt64(1));
    }

    auto& cfg = outModel.Configuration;
    const auto readFloat = [](const EL::TomlTable* tbl, const char* key, float& dst)
    {
        if (!tbl) return;
        if (const auto* v = tbl->Find(key); v && v->IsNumber()) dst = static_cast<float>(v->AsDouble(dst));
    };
    const auto readBool = [](const EL::TomlTable* tbl, const char* key, bool& dst)
    {
        if (!tbl) return;
        if (const auto* v = tbl->Find(key); v && v->IsBool()) dst = v->AsBool(dst);
    };

    if (const auto* solver = FindSubTable(root, "Solver"))
    {
        if (const auto* v = solver->Find("Substeps"); v && v->IsInt()) cfg.Substeps = static_cast<uint32_t>(v->AsInt64(6));
        readFloat(solver, "GravityY", cfg.GravityY);
        readFloat(solver, "Damping", cfg.Damping);
        readBool(solver, "UnderwaterEnabled", cfg.UnderwaterEnabled);
    }

    if (const auto* mat = FindSubTable(root, "Material"))
    {
        readFloat(mat, "StretchStiffness", cfg.StretchStiffness);
        readFloat(mat, "BendStiffness", cfg.BendStiffness);
        readFloat(mat, "FemYoungMpa", cfg.FemYoungMpa);
        readFloat(mat, "FemPoissonNu", cfg.FemPoissonNu);
        readFloat(mat, "TearRatio", cfg.TearRatio);
    }

    if (const auto* env = FindSubTable(root, "Environment"))
    {
        readFloat(env, "WindStrength", cfg.WindStrength);
        readFloat(env, "Turbulence", cfg.Turbulence);
        readBool(env, "ColliderEnabled", cfg.ColliderEnabled);
    }

    if (const auto* tire = FindSubTable(root, "Tire"))
    {
        readFloat(tire, "PressurePsi", cfg.TirePressurePsi);
        readFloat(tire, "AxleLoad", cfg.TireAxleLoad);
        readFloat(tire, "SpinRadPerSec", cfg.TireSpinRadPerSec);
    }

    for (const auto& sec : sections)
    {
        if (sec.Identifier == "RestCageBinary")
        {
            if (!UnpackRestCageBinary(sec.PayloadBytes, outModel.EmbeddedRestBody))
            {
                if (outError) *outError = "SoftBody RestCageBinary payload truncated or corrupt.";
                return false;
            }
            outModel.EmbedRestCageBinary = true;
        }
    }
    return true;
}

bool SoftBodyCodec::EncodeToFile(
    const std::filesystem::path& targetPath,
    const SoftBodyDocumentModel& model,
    std::string* outError)
{
    std::vector<std::byte> bytes;
    if (!EncodeToBytes(model, bytes, outError))
    {
        return false;
    }
    return WriteSimulationBytesToFile(targetPath, bytes, outError);
}

bool SoftBodyCodec::DecodeFromFile(
    const std::filesystem::path& sourcePath,
    SoftBodyDocumentModel& outModel,
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
