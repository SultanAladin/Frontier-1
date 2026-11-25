/*============================================================================================================================================
                                                           FLUIDGRIDCODEC.CPP
============================================================================================================================================*/
// 🧩 Stage F (S28) .FluidGrid & .VolumetricSpace two-tier codec implementation
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.1

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/FluidGridCodec.h"

#include <algorithm>
#include <cstring>

namespace Frontier::Codecs::Simulation
{

namespace
{

namespace EL = Frontier::SimulationSpace::EffectLayers;
namespace FL = Frontier::SimulationSpace::Fluid;

[[nodiscard]] std::string_view FormatBlendToken(const EL::LayerBlendOrdinal b) noexcept
{
    switch (b)
    {
        case EL::LayerBlendOrdinal::Replace:  return "Replace";
        case EL::LayerBlendOrdinal::Add:      return "Add";
        case EL::LayerBlendOrdinal::Multiply: return "Multiply";
        case EL::LayerBlendOrdinal::Maximum:  return "Maximum";
    }
    return "Replace";
}

[[nodiscard]] EL::LayerBlendOrdinal ParseBlendToken(const std::string_view s) noexcept
{
    if (s == "Add")      return EL::LayerBlendOrdinal::Add;
    if (s == "Multiply") return EL::LayerBlendOrdinal::Multiply;
    if (s == "Maximum")  return EL::LayerBlendOrdinal::Maximum;
    return EL::LayerBlendOrdinal::Replace;
}

[[nodiscard]] std::string_view FormatTopologyToken(const FL::FluidGridTopologyOrdinal t) noexcept
{
    switch (t)
    {
        case FL::FluidGridTopologyOrdinal::Fixed:    return "Fixed";
        case FL::FluidGridTopologyOrdinal::Infinite: return "Infinite";
    }
    return "Fixed";
}

[[nodiscard]] FL::FluidGridTopologyOrdinal ParseTopologyToken(const std::string_view s) noexcept
{
    if (s == "Infinite") return FL::FluidGridTopologyOrdinal::Infinite;
    return FL::FluidGridTopologyOrdinal::Fixed;
}

[[nodiscard]] std::string_view FormatSourceShapeToken(const FL::ReleaseSourceShapeOrdinal s) noexcept
{
    switch (s)
    {
        case FL::ReleaseSourceShapeOrdinal::Sphere: return "Sphere";
        case FL::ReleaseSourceShapeOrdinal::Disc:   return "Disc";
        case FL::ReleaseSourceShapeOrdinal::Box:    return "Box";
    }
    return "Sphere";
}

[[nodiscard]] FL::ReleaseSourceShapeOrdinal ParseSourceShapeToken(const std::string_view s) noexcept
{
    if (s == "Disc") return FL::ReleaseSourceShapeOrdinal::Disc;
    if (s == "Box")  return FL::ReleaseSourceShapeOrdinal::Box;
    return FL::ReleaseSourceShapeOrdinal::Sphere;
}

[[nodiscard]] std::string_view FormatDebrisKindToken(const FL::FluidDebrisKindOrdinal k) noexcept
{
    switch (k)
    {
        case FL::FluidDebrisKindOrdinal::Rubble: return "Rubble";
        case FL::FluidDebrisKindOrdinal::Ember:  return "Ember";
        case FL::FluidDebrisKindOrdinal::Paper:  return "Paper";
    }
    return "Rubble";
}

[[nodiscard]] FL::FluidDebrisKindOrdinal ParseDebrisKindToken(const std::string_view s) noexcept
{
    if (s == "Ember") return FL::FluidDebrisKindOrdinal::Ember;
    if (s == "Paper") return FL::FluidDebrisKindOrdinal::Paper;
    return FL::FluidDebrisKindOrdinal::Rubble;
}

void AppendLayerParamValueToml(std::string& out, const EL::LayerParameterValue& val)
{
    if (const auto* d = std::get_if<double>(&val))
    {
        out += FormatCanonicalFloat(*d);
    }
    else if (const auto* i = std::get_if<int64_t>(&val))
    {
        out += std::to_string(*i);
    }
    else if (const auto* b = std::get_if<bool>(&val))
    {
        out += (*b ? "true" : "false");
    }
    else if (const auto* v3 = std::get_if<std::array<double, 3>>(&val))
    {
        out += "[ " + FormatCanonicalFloat((*v3)[0]) + ", "
             + FormatCanonicalFloat((*v3)[1]) + ", "
             + FormatCanonicalFloat((*v3)[2]) + " ]";
    }
    else if (const auto* s = std::get_if<std::string>(&val))
    {
        out += EscapeTomlQuotedString(*s);
    }
    else if (const auto* arr = std::get_if<std::vector<std::string>>(&val))
    {
        out += "[ ";
        for (std::size_t idx = 0u; idx < arr->size(); ++idx)
        {
            if (idx > 0u) out += ", ";
            out += EscapeTomlQuotedString((*arr)[idx]);
        }
        out += " ]";
    }
}

void AppendRawBytes(std::vector<std::byte>& dst, const void* ptr, const std::size_t n)
{
    const auto* p = reinterpret_cast<const std::byte*>(ptr);
    dst.insert(dst.end(), p, p + n);
}

[[nodiscard]] bool ReadRawBytes(const std::span<const std::byte> src, std::size_t& cur, void* dst, const std::size_t n) noexcept
{
    if (cur + n > src.size()) return false;
    std::memcpy(dst, src.data() + cur, n);
    cur += n;
    return true;
}

void PackWarmStartBinary(const FluidWarmStartBinary& ws, std::vector<std::byte>& out)
{
    out.clear();
    const uint32_t vCount = static_cast<uint32_t>(ws.VelocityRgba.size());
    const uint32_t sCount = static_cast<uint32_t>(ws.StateRgba.size());
    const uint32_t pCount = static_cast<uint32_t>(ws.PressureLightRgba.size());
    out.reserve(40u + (static_cast<std::size_t>(vCount) + sCount + pCount) * sizeof(float));

    AppendRawBytes(out, &ws.SimulationSeconds, sizeof(double));
    AppendRawBytes(out, &ws.StepIndex, sizeof(uint32_t));
    AppendRawBytes(out, ws.GridResolution.data(), 3u * sizeof(uint32_t));
    AppendRawBytes(out, &vCount, sizeof(uint32_t));
    AppendRawBytes(out, &sCount, sizeof(uint32_t));
    AppendRawBytes(out, &pCount, sizeof(uint32_t));
    if (vCount > 0u) AppendRawBytes(out, ws.VelocityRgba.data(), static_cast<std::size_t>(vCount) * sizeof(float));
    if (sCount > 0u) AppendRawBytes(out, ws.StateRgba.data(), static_cast<std::size_t>(sCount) * sizeof(float));
    if (pCount > 0u) AppendRawBytes(out, ws.PressureLightRgba.data(), static_cast<std::size_t>(pCount) * sizeof(float));
}

[[nodiscard]] bool UnpackWarmStartBinary(const std::span<const std::byte> src, FluidWarmStartBinary& out) noexcept
{
    std::size_t cur = 0u;
    uint32_t vCount = 0u, sCount = 0u, pCount = 0u;
    if (!ReadRawBytes(src, cur, &out.SimulationSeconds, sizeof(double))) return false;
    if (!ReadRawBytes(src, cur, &out.StepIndex, sizeof(uint32_t))) return false;
    if (!ReadRawBytes(src, cur, out.GridResolution.data(), 3u * sizeof(uint32_t))) return false;
    if (!ReadRawBytes(src, cur, &vCount, sizeof(uint32_t))) return false;
    if (!ReadRawBytes(src, cur, &sCount, sizeof(uint32_t))) return false;
    if (!ReadRawBytes(src, cur, &pCount, sizeof(uint32_t))) return false;

    out.VelocityRgba.resize(vCount);
    out.StateRgba.resize(sCount);
    out.PressureLightRgba.resize(pCount);
    if (vCount > 0u && !ReadRawBytes(src, cur, out.VelocityRgba.data(), static_cast<std::size_t>(vCount) * sizeof(float))) return false;
    if (sCount > 0u && !ReadRawBytes(src, cur, out.StateRgba.data(), static_cast<std::size_t>(sCount) * sizeof(float))) return false;
    if (pCount > 0u && !ReadRawBytes(src, cur, out.PressureLightRgba.data(), static_cast<std::size_t>(pCount) * sizeof(float))) return false;
    return cur == src.size();
}

[[nodiscard]] std::array<float, 3> ReadArrayFloat3(const EL::TomlTable& tbl, const std::string_view key, const std::array<float, 3>& fb) noexcept
{
    const EL::TomlValue* v = tbl.Find(key);
    if (v == nullptr || !v->IsArray()) return fb;
    const auto& arr = v->AsArray()->Entries;
    if (arr.size() < 3u) return fb;
    return { static_cast<float>(arr[0]->AsDouble(fb[0])), static_cast<float>(arr[1]->AsDouble(fb[1])), static_cast<float>(arr[2]->AsDouble(fb[2])) };
}

[[nodiscard]] std::array<uint32_t, 3> ReadArrayUInt3(const EL::TomlTable& tbl, const std::string_view key, const std::array<uint32_t, 3>& fb) noexcept
{
    const EL::TomlValue* v = tbl.Find(key);
    if (v == nullptr || !v->IsArray()) return fb;
    const auto& arr = v->AsArray()->Entries;
    if (arr.size() < 3u) return fb;
    return {
        static_cast<uint32_t>(std::max<int64_t>(1, arr[0]->AsInt64(fb[0]))),
        static_cast<uint32_t>(std::max<int64_t>(1, arr[1]->AsInt64(fb[1]))),
        static_cast<uint32_t>(std::max<int64_t>(1, arr[2]->AsInt64(fb[2])))
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      SHARED EFFECTLAYERSTACK TOML CODEC
//------------------------------------------------------------------------------------------------------------------------

void AppendEffectLayerStackToToml(
    std::string& outToml,
    const EL::EffectLayerSpecificationSet& specs,
    const EL::EffectLayerStack& stack)
{
    outToml += "\n[Stack]\n";
    outToml += "Token = " + EscapeTomlQuotedString(stack.Token) + "\n";
    outToml += "Category = " + std::to_string(static_cast<uint32_t>(stack.Category)) + "\n";

    for (const auto& layer : stack.Layers)
    {
        outToml += "\n[[Layer]]\n";
        outToml += "Token = " + EscapeTomlQuotedString(layer.Token) + "\n";
        outToml += "CategoryCode = " + std::to_string(layer.CategoryCode) + "\n";
        outToml += "Enabled = " + std::string(layer.Enabled ? "true" : "false") + "\n";
        outToml += "Solo = " + std::string(layer.Solo ? "true" : "false") + "\n";
        outToml += "Weight = " + FormatCanonicalFloat(layer.Weight) + "\n";
        outToml += "Blend = " + EscapeTomlQuotedString(FormatBlendToken(layer.Blend)) + "\n";

        if (!layer.Parameters.empty())
        {
            const auto* catSpec = specs.FindByCode(layer.CategoryCode);
            outToml += "\n  [Layer.Parameters]\n";
            for (const auto& param : layer.Parameters)
            {
                outToml += "  " + param.Name + " = ";
                if (catSpec != nullptr)
                {
                    if (const auto* pSpec = specs.FindParameter(*catSpec, param.Name);
                        pSpec != nullptr && pSpec->Type == EL::ParameterTypeOrdinal::Choice)
                    {
                        if (const auto* code = std::get_if<int64_t>(&param.Value))
                        {
                            outToml += EscapeTomlQuotedString(EL::ResolveChoiceToken(*pSpec, *code)) + "\n";
                            continue;
                        }
                    }
                }
                AppendLayerParamValueToml(outToml, param.Value);
                outToml += "\n";
            }
        }
    }
}

bool ParseEffectLayerStackFromToml(
    const EL::EffectLayerSpecificationSet& specs,
    const EL::TomlTable& rootTable,
    const EL::LayerStackOrdinal expectedCategory,
    EL::EffectLayerStack& outStack,
    std::string* const outError)
{
    outStack = EL::EffectLayerStack {};
    outStack.Category = expectedCategory;
    outStack.Token    = (expectedCategory == EL::LayerStackOrdinal::GasVolume) ? "gas-volume" : "particle-cloud";

    if (const auto* stVal = rootTable.Find("Stack"); stVal != nullptr && stVal->IsTable())
    {
        const auto& st = *stVal->AsTable();
        if (const auto* tk = st.Find("Token")) outStack.Token = std::string(tk->AsString());
        if (const auto* cat = st.Find("Category"))
        {
            outStack.Category = (cat->AsInt64(1) == 2) ? EL::LayerStackOrdinal::ParticleCloud : EL::LayerStackOrdinal::GasVolume;
        }
    }

    const auto* layersVal = rootTable.Find("Layer");
    if (layersVal == nullptr || !layersVal->IsArray())
    {
        if (outError) *outError = "Missing [[Layer]] array in simulation stack document";
        return false;
    }

    for (const auto& lEntry : layersVal->AsArray()->Entries)
    {
        const EL::TomlTable* tbl = lEntry ? lEntry->AsTable() : nullptr;
        if (tbl == nullptr) continue;

        EL::EffectLayerEntry entry {};
        if (const auto* tk = tbl->Find("Token"))        entry.Token = std::string(tk->AsString());
        if (const auto* cc = tbl->Find("CategoryCode")) entry.CategoryCode = static_cast<uint32_t>(cc->AsInt64(0));
        if (const auto* en = tbl->Find("Enabled"))      entry.Enabled = en->AsBool(true);
        if (const auto* so = tbl->Find("Solo"))         entry.Solo = so->AsBool(false);
        if (const auto* wt = tbl->Find("Weight"))       entry.Weight = wt->AsDouble(1.0);
        if (const auto* bl = tbl->Find("Blend"))        entry.Blend = ParseBlendToken(bl->AsString());

        const auto* catSpec = specs.FindByCode(entry.CategoryCode);
        if (const auto* paramsVal = tbl->Find("Parameters"); paramsVal != nullptr && paramsVal->IsTable())
        {
            for (const auto& field : paramsVal->AsTable()->Fields)
            {
                if (!field.Value) continue;
                if (catSpec != nullptr)
                {
                    if (const auto* pSpec = specs.FindParameter(*catSpec, field.Key))
                    {
                        entry.SetParameter(field.Key, EL::DecodeParameterValueFromToml(*pSpec, *field.Value));
                        continue;
                    }
                }
                if (field.Value->IsBool())        entry.SetParameter(field.Key, field.Value->AsBool());
                else if (field.Value->IsInt())    entry.SetParameter(field.Key, field.Value->AsInt64());
                else if (field.Value->IsNumber()) entry.SetParameter(field.Key, field.Value->AsDouble());
                else if (field.Value->IsString()) entry.SetParameter(field.Key, std::string(field.Value->AsString()));
            }
        }
        outStack.Layers.push_back(std::move(entry));
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLUIDGRIDCODEC
//------------------------------------------------------------------------------------------------------------------------

bool FluidGridCodec::EncodeToBytes(
    const EL::EffectLayerSpecificationSet& specs,
    const FluidGridDocumentModel& model,
    std::vector<std::byte>& outBytes,
    std::string* const outError)
{
    std::string toml;
    toml.reserve(4096u);
    toml += "[Document]\n";
    toml += "Format = \"frontier-fluid-grid\"\n";
    toml += "Suffix = " + EscapeTomlQuotedString(model.ContainerSuffix) + "\n";
    toml += "SchemaVersion = " + std::to_string(model.SchemaVersion) + "\n";
    toml += "Title = " + EscapeTomlQuotedString(model.DocumentTitle) + "\n";

    toml += "\n[Grid]\n";
    toml += "Topology = " + EscapeTomlQuotedString(FormatTopologyToken(model.Grid.Topology)) + "\n";
    toml += "Resolution = [ " + std::to_string(model.Grid.Resolution[0]) + ", "
          + std::to_string(model.Grid.Resolution[1]) + ", " + std::to_string(model.Grid.Resolution[2]) + " ]\n";
    toml += "SizeMeters = [ " + FormatCanonicalFloat(model.Grid.SizeMeters[0]) + ", "
          + FormatCanonicalFloat(model.Grid.SizeMeters[1]) + ", " + FormatCanonicalFloat(model.Grid.SizeMeters[2]) + " ]\n";
    toml += "MaxCells = " + std::to_string(model.Grid.MaxCells) + "\n";
    toml += "TileSizeMeters = " + FormatCanonicalFloat(model.Grid.TileSizeMeters) + "\n";
    toml += "TileResolution = " + std::to_string(model.Grid.TileResolution) + "\n";
    toml += "TileSlotBudget = " + std::to_string(model.Grid.TileSlotBudget) + "\n";
    toml += "WorldTiles = " + std::to_string(model.Grid.WorldTiles) + "\n";

    AppendEffectLayerStackToToml(toml, specs, model.GasStack);

    for (const auto& s : model.Sources)
    {
        toml += "\n[[Source]]\n";
        toml += "Token = " + EscapeTomlQuotedString(s.Token) + "\n";
        toml += "Enabled = " + std::string(s.Source.Enabled ? "true" : "false") + "\n";
        toml += "Shape = " + EscapeTomlQuotedString(FormatSourceShapeToken(s.Source.Shape)) + "\n";
        toml += "Position = [ " + FormatCanonicalFloat(s.Source.PositionMeters[0]) + ", "
              + FormatCanonicalFloat(s.Source.PositionMeters[1]) + ", " + FormatCanonicalFloat(s.Source.PositionMeters[2]) + " ]\n";
        toml += "Radius = " + FormatCanonicalFloat(s.Source.RadiusMeters) + "\n";
        toml += "Velocity = [ " + FormatCanonicalFloat(s.Source.VelocityMps[0]) + ", "
              + FormatCanonicalFloat(s.Source.VelocityMps[1]) + ", " + FormatCanonicalFloat(s.Source.VelocityMps[2]) + " ]\n";
        toml += "Density = " + FormatCanonicalFloat(s.Source.Density) + "\n";
        toml += "Temperature = " + FormatCanonicalFloat(s.Source.Temperature) + "\n";
        toml += "Fuel = " + FormatCanonicalFloat(s.Source.Fuel) + "\n";
        toml += "Jitter = " + FormatCanonicalFloat(s.Source.Jitter) + "\n";
        toml += "PulsePeriod = " + FormatCanonicalFloat(s.Source.PulseSeconds) + "\n";
        toml += "DebrisAmount = " + FormatCanonicalFloat(s.DebrisAmount) + "\n";
        toml += "DebrisSpeed = " + FormatCanonicalFloat(s.DebrisSpeed) + "\n";
        toml += "DebrisKind = " + EscapeTomlQuotedString(FormatDebrisKindToken(s.DebrisKind)) + "\n";
    }

    AppendForceNodesToToml(toml, model.Forces);
    AppendKeyTracksToToml(toml, model.Tracks);

    std::vector<SimulationBinarySection> sections;
    if (model.HasWarmStart)
    {
        SimulationBinarySection sec {};
        sec.Identifier   = "FluidWarmStart";
        sec.SubFormatTag = model.ContainerSuffix;
        PackWarmStartBinary(model.WarmStart, sec.PayloadBytes);
        sections.push_back(std::move(sec));
    }
    return PackTwoTierSimulationBytes(toml, sections, outBytes, outError);
}

bool FluidGridCodec::DecodeFromBytes(
    const EL::EffectLayerSpecificationSet& specs,
    const std::span<const std::byte> bytes,
    FluidGridDocumentModel& outModel,
    std::string* const outError)
{
    std::string tomlText;
    std::vector<SimulationBinarySection> sections;
    if (!UnpackTwoTierSimulationBytes(bytes, tomlText, sections, outError)) return false;

    const auto parsed = EL::ParseTomlSubset(tomlText, outError);
    if (!parsed.has_value()) return false;

    outModel = FluidGridDocumentModel {};
    if (const auto* docVal = parsed->Find("Document"); docVal != nullptr && docVal->IsTable())
    {
        const auto& doc = *docVal->AsTable();
        if (const auto* fmt = doc.Find("Format"); fmt == nullptr || fmt->AsString() != "frontier-fluid-grid")
        {
            if (outError) *outError = "Document.Format must be frontier-fluid-grid";
            return false;
        }
        if (const auto* sf = doc.Find("Suffix"))        outModel.ContainerSuffix = std::string(sf->AsString());
        if (const auto* sv = doc.Find("SchemaVersion")) outModel.SchemaVersion = static_cast<uint32_t>(sv->AsInt64(1));
        if (const auto* tt = doc.Find("Title"))         outModel.DocumentTitle = std::string(tt->AsString());
    }
    else
    {
        if (outError) *outError = "Missing [Document] table in .FluidGrid";
        return false;
    }

    if (const auto* gVal = parsed->Find("Grid"); gVal != nullptr && gVal->IsTable())
    {
        const auto& g = *gVal->AsTable();
        if (const auto* tp = g.Find("Topology"))       outModel.Grid.Topology = ParseTopologyToken(tp->AsString());
        outModel.Grid.Resolution = ReadArrayUInt3(g, "Resolution", outModel.Grid.Resolution);
        outModel.Grid.SizeMeters = ReadArrayFloat3(g, "SizeMeters", outModel.Grid.SizeMeters);
        if (const auto* mc = g.Find("MaxCells"))       outModel.Grid.MaxCells = static_cast<uint32_t>(mc->AsInt64(96));
        if (const auto* ts = g.Find("TileSizeMeters")) outModel.Grid.TileSizeMeters = static_cast<float>(ts->AsDouble(2.0));
        if (const auto* tr = g.Find("TileResolution")) outModel.Grid.TileResolution = static_cast<uint32_t>(tr->AsInt64(24));
        if (const auto* tb = g.Find("TileSlotBudget")) outModel.Grid.TileSlotBudget = static_cast<uint32_t>(tb->AsInt64(48));
        if (const auto* wt = g.Find("WorldTiles"))     outModel.Grid.WorldTiles = static_cast<uint32_t>(wt->AsInt64(0));
    }

    if (!ParseEffectLayerStackFromToml(specs, *parsed, EL::LayerStackOrdinal::GasVolume, outModel.GasStack, outError))
    {
        return false;
    }

    if (const auto* srcVal = parsed->Find("Source"); srcVal != nullptr && srcVal->IsArray())
    {
        for (const auto& entry : srcVal->AsArray()->Entries)
        {
            const EL::TomlTable* tbl = entry ? entry->AsTable() : nullptr;
            if (tbl == nullptr) continue;
            FL::FluidPresetSourceEntry s {};
            if (const auto* tk = tbl->Find("Token"))        s.Token = std::string(tk->AsString());
            if (const auto* en = tbl->Find("Enabled"))      s.Source.Enabled = en->AsBool(true);
            if (const auto* sh = tbl->Find("Shape"))        s.Source.Shape = ParseSourceShapeToken(sh->AsString());
            s.Source.PositionMeters = ReadArrayFloat3(*tbl, "Position", s.Source.PositionMeters);
            if (const auto* ra = tbl->Find("Radius"))       s.Source.RadiusMeters = static_cast<float>(ra->AsDouble(0.25));
            s.Source.VelocityMps    = ReadArrayFloat3(*tbl, "Velocity", s.Source.VelocityMps);
            if (const auto* de = tbl->Find("Density"))      s.Source.Density = static_cast<float>(de->AsDouble(1.0));
            if (const auto* te = tbl->Find("Temperature"))  s.Source.Temperature = static_cast<float>(te->AsDouble(1.0));
            if (const auto* fu = tbl->Find("Fuel"))         s.Source.Fuel = static_cast<float>(fu->AsDouble(0.0));
            if (const auto* ji = tbl->Find("Jitter"))       s.Source.Jitter = static_cast<float>(ji->AsDouble(0.0));
            if (const auto* pp = tbl->Find("PulsePeriod"))  s.Source.PulseSeconds = static_cast<float>(pp->AsDouble(0.0));
            if (const auto* da = tbl->Find("DebrisAmount")) s.DebrisAmount = static_cast<float>(da->AsDouble(0.0));
            if (const auto* ds = tbl->Find("DebrisSpeed"))  s.DebrisSpeed  = static_cast<float>(ds->AsDouble(2.0));
            if (const auto* dk = tbl->Find("DebrisKind"))   s.DebrisKind   = ParseDebrisKindToken(dk->AsString());
            outModel.Sources.push_back(std::move(s));
        }
    }

    outModel.Forces = ParseForceNodesFromToml(*parsed);
    outModel.Tracks = ParseKeyTracksFromToml(*parsed);

    for (const auto& sec : sections)
    {
        if (sec.Identifier == "FluidWarmStart")
        {
            if (!UnpackWarmStartBinary(sec.PayloadBytes, outModel.WarmStart))
            {
                if (outError) *outError = "Corrupt FluidWarmStart binary payload";
                return false;
            }
            outModel.HasWarmStart = true;
        }
    }
    return true;
}

bool FluidGridCodec::EncodeToFile(
    const std::filesystem::path& targetPath,
    const EL::EffectLayerSpecificationSet& specs,
    const FluidGridDocumentModel& model,
    std::string* const outError)
{
    std::vector<std::byte> bytes;
    if (!EncodeToBytes(specs, model, bytes, outError)) return false;
    return WriteSimulationBytesToFile(targetPath, bytes, outError);
}

bool FluidGridCodec::DecodeFromFile(
    const std::filesystem::path& sourcePath,
    const EL::EffectLayerSpecificationSet& specs,
    FluidGridDocumentModel& outModel,
    std::string* const outError)
{
    std::vector<std::byte> bytes;
    if (!ReadSimulationBytesFromFile(sourcePath, bytes, outError)) return false;
    return DecodeFromBytes(specs, bytes, outModel, outError);
}

} // namespace Frontier::Codecs::Simulation
