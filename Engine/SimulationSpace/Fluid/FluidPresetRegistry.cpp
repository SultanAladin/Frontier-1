/*============================================================================================================================================
                                                       FLUIDPRESETREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Stage D (Slice S18) 1:1 54-preset .FluidGrid loader, 8,192 tracer/debris CPU mirrors, and RGBA8 PNG flipbook writer
// Reference: Experimental/FluidEditor/Engine/PresetScenes.js L1-L171 & FluidShaders.js L951-L999, L1589-L1740

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidPresetRegistry.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Frontier::SimulationSpace::Fluid
{

namespace
{

namespace EL = Frontier::SimulationSpace::EffectLayers;
namespace FF = Frontier::SimulationSpace::ForceFields;

[[nodiscard]] float FractF(const float x) noexcept
{
    return x - std::floor(x);
}

[[nodiscard]] std::array<float, 3> ParseHexRgb(const std::string_view hex, const std::array<float, 3>& fallback) noexcept
{
    std::string_view s = hex;
    if (!s.empty() && s.front() == '#') s.remove_prefix(1u);
    if (s.size() != 6u) return fallback;
    auto hexNib = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    int vals[6];
    for (size_t i = 0u; i < 6u; ++i)
    {
        vals[i] = hexNib(s[i]);
        if (vals[i] < 0) return fallback;
    }
    return {
        static_cast<float>((vals[0] << 4) | vals[1]) / 255.0f,
        static_cast<float>((vals[2] << 4) | vals[3]) / 255.0f,
        static_cast<float>((vals[4] << 4) | vals[5]) / 255.0f
    };
}

[[nodiscard]] std::array<float, 3> ReadVec3Toml(const EL::TomlTable& tbl, const std::string_view key, const std::array<float, 3>& fallback) noexcept
{
    const auto* v = tbl.Find(key);
    if (v == nullptr || !v->IsArray()) return fallback;
    const auto* arr = v->AsArray();
    if (arr->Entries.size() < 3u) return fallback;
    return {
        static_cast<float>(arr->Entries[0]->AsDouble(fallback[0])),
        static_cast<float>(arr->Entries[1]->AsDouble(fallback[1])),
        static_cast<float>(arr->Entries[2]->AsDouble(fallback[2]))
    };
}

[[nodiscard]] std::array<uint32_t, 3> ReadUVec3Toml(const EL::TomlTable& tbl, const std::string_view key, const std::array<uint32_t, 3>& fallback) noexcept
{
    const auto* v = tbl.Find(key);
    if (v == nullptr || !v->IsArray()) return fallback;
    const auto* arr = v->AsArray();
    if (arr->Entries.size() < 3u) return fallback;
    return {
        static_cast<uint32_t>(std::max<int64_t>(1, arr->Entries[0]->AsInt64(fallback[0]))),
        static_cast<uint32_t>(std::max<int64_t>(1, arr->Entries[1]->AsInt64(fallback[1]))),
        static_cast<uint32_t>(std::max<int64_t>(1, arr->Entries[2]->AsInt64(fallback[2])))
    };
}

[[nodiscard]] FluidBoundaryOrdinal ParseBoundaryOrdinal(const std::string_view token) noexcept
{
    if (token == "ClosedBox")     return FluidBoundaryOrdinal::ClosedBox;
    if (token == "OpenTop")       return FluidBoundaryOrdinal::OpenTop;
    if (token == "OpenAll")       return FluidBoundaryOrdinal::OpenAll;
    if (token == "InfiniteFloor") return FluidBoundaryOrdinal::InfiniteFloor;
    return FluidBoundaryOrdinal::OpenTop;
}

[[nodiscard]] ReleaseSourceShapeOrdinal ParseSourceShapeOrdinal(const std::string_view token) noexcept
{
    if (token == "Disc") return ReleaseSourceShapeOrdinal::Disc;
    if (token == "Box")  return ReleaseSourceShapeOrdinal::Box;
    return ReleaseSourceShapeOrdinal::Sphere;
}

[[nodiscard]] FluidDebrisKindOrdinal ParseDebrisKindOrdinal(const std::string_view token) noexcept
{
    if (token == "Ember") return FluidDebrisKindOrdinal::Ember;
    if (token == "Paper") return FluidDebrisKindOrdinal::Paper;
    return FluidDebrisKindOrdinal::Rubble;
}

[[nodiscard]] float ReadEvalFloat(const EL::EvaluatedParameterBlock& block, const std::string_view name, const float fallback) noexcept
{
    const auto* val = block.Find(name);
    if (val == nullptr) return fallback;
    if (const auto* d = std::get_if<double>(val)) return static_cast<float>(*d);
    if (const auto* i = std::get_if<int64_t>(val)) return static_cast<float>(*i);
    return fallback;
}

[[nodiscard]] int32_t ReadEvalInt(const EL::EvaluatedParameterBlock& block, const std::string_view name, const int32_t fallback) noexcept
{
    const auto* val = block.Find(name);
    if (val == nullptr) return fallback;
    if (const auto* i = std::get_if<int64_t>(val)) return static_cast<int32_t>(*i);
    if (const auto* d = std::get_if<double>(val)) return static_cast<int32_t>(std::lround(*d));
    return fallback;
}

[[nodiscard]] std::string_view ReadEvalString(const EL::EvaluatedParameterBlock& block, const std::string_view name, const std::string_view fallback) noexcept
{
    const auto* val = block.Find(name);
    if (val == nullptr) return fallback;
    if (const auto* s = std::get_if<std::string>(val)) return *s;
    return fallback;
}

[[nodiscard]] FF::ForceModelOrdinal ParseForceModelToken(const std::string_view token) noexcept
{
    if (const auto* desc = FF::FindForceModelByTomlId(token); desc != nullptr)
    {
        return desc->Code;
    }
    return FF::ForceModelOrdinal::DirectionalWind;
}

[[nodiscard]] FF::ForceFalloffOrdinal ParseForceFalloffToken(const std::string_view token) noexcept
{
    if (const auto parsed = FF::ParseFalloffTomlToken(token); parsed.has_value())
    {
        return *parsed;
    }
    return FF::ForceFalloffOrdinal::SmoothStep;
}

[[nodiscard]] std::array<float, 3> SpawnTracerPositionCpu(
    const FluidVolumeUniformBlock& vol,
    const float px,
    const float py,
    const float salt) noexcept
{
    const float h = vol.GridCentreCellSize[3];
    const float ex = static_cast<float>(vol.GridBoundary[0]) * h;
    const float ey = static_cast<float>(vol.GridBoundary[1]) * h;
    const float ez = static_cast<float>(vol.GridBoundary[2]) * h;
    const float ux = FF::Hash31({ px, py, salt });
    const float uy = FF::Hash31({ py, px, salt + 7.3f });
    const float uz = FF::Hash31({ px + 3.1f, py + 3.1f, salt + 19.7f });
    return {
        vol.GridCentreCellSize[0] + (ux - 0.5f) * ex * 0.96f,
        vol.GridCentreCellSize[1] + (uy - 0.5f) * ey * 0.96f,
        vol.GridCentreCellSize[2] + (uz - 0.5f) * ez * 0.96f
    };
}

[[nodiscard]] std::array<float, 3> RandomUnitDirectionCpu(const float sx, const float sy, const float sz) noexcept
{
    const float z = FF::Hash31({ sx, sy, sz }) * 2.0f - 1.0f;
    const float a = FF::Hash31({ sz + 5.1f, sx + 5.1f, sy + 5.1f }) * 6.2831853f;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return { r * std::cos(a), z, r * std::sin(a) };
}

[[nodiscard]] uint32_t ComputeCrc32(const uint8_t* data, const size_t len) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < len; ++i)
    {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b)
        {
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

[[nodiscard]] uint32_t ComputeAdler32(const uint8_t* data, const size_t len) noexcept
{
    uint32_t s1 = 1u, s2 = 0u;
    for (size_t i = 0u; i < len; ++i)
    {
        s1 = (s1 + static_cast<uint32_t>(data[i])) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16u) | s1;
}

void AppendBe32(std::vector<uint8_t>& out, const uint32_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 24u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >>  8u) & 0xFFu));
    out.push_back(static_cast<uint8_t>( v         & 0xFFu));
}

void AppendPngChunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& payload)
{
    AppendBe32(out, static_cast<uint32_t>(payload.size()));
    const size_t crcStart = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const uint32_t crc = ComputeCrc32(out.data() + crcStart, out.size() - crcStart);
    AppendBe32(out, crc);
}

} // namespace

bool FluidPresetRegistry::LoadFromDirectory(
    const std::string_view directoryPath,
    const EL::EffectLayerSpecificationSet& specs,
    std::string* const outError)
{
    Presets.clear();
    std::error_code ec;
    if (!std::filesystem::exists(directoryPath, ec))
    {
        if (outError != nullptr) *outError = "Fluid preset directory not found: " + std::string(directoryPath);
        return false;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directoryPath, ec))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".FluidGrid")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& path : files)
    {
        std::ifstream in(path);
        if (!in.is_open())
        {
            if (outError != nullptr) *outError = "Failed to open preset file: " + path.string();
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string parseErr;
        const auto rootOpt = EL::ParseTomlSubset(ss.str(), &parseErr);
        if (!rootOpt.has_value())
        {
            if (outError != nullptr) *outError = "Parse error in " + path.string() + ": " + parseErr;
            return false;
        }
        const auto& root = *rootOpt;

        FluidPresetRecord rec {};
        rec.FilePath = path.string();

        if (const auto* sceneVal = root.Find("Scene"); sceneVal != nullptr && sceneVal->IsTable())
        {
            const auto& sc = *sceneVal->AsTable();
            if (const auto* v = sc.Find("Order"))    rec.Order    = static_cast<int32_t>(v->AsInt64(0));
            if (const auto* v = sc.Find("Id"))       rec.Id       = std::string(v->AsString());
            if (const auto* v = sc.Find("Name"))     rec.Name     = std::string(v->AsString());
            if (const auto* v = sc.Find("Family"))   rec.Family   = std::string(v->AsString());
            if (const auto* v = sc.Find("Icon"))     rec.Icon     = std::string(v->AsString());
            if (const auto* v = sc.Find("Summary"))  rec.Summary  = std::string(v->AsString());
            if (const auto* v = sc.Find("Boundary")) rec.Boundary = ParseBoundaryOrdinal(v->AsString());
        }

        if (const auto* gridVal = root.Find("Grid"); gridVal != nullptr && gridVal->IsTable())
        {
            const auto& g = *gridVal->AsTable();
            rec.Grid.Resolution = ReadUVec3Toml(g, "Resolution", rec.Grid.Resolution);
            rec.Grid.SizeMeters = ReadVec3Toml(g, "Size", rec.Grid.SizeMeters);
            if (const auto* v = g.Find("MaxCells"))       rec.Grid.MaxCells       = static_cast<uint32_t>(std::max<int64_t>(8, v->AsInt64(96)));
            if (const auto* v = g.Find("Grid"))           rec.Grid.Topology       = (v->AsString() == "Infinite") ? FluidGridTopologyOrdinal::Infinite : FluidGridTopologyOrdinal::Fixed;
            if (const auto* v = g.Find("TileSize"))       rec.Grid.TileSizeMeters = static_cast<float>(v->AsDouble(2.0));
            if (const auto* v = g.Find("TileResolution")) rec.Grid.TileResolution = static_cast<uint32_t>(std::max<int64_t>(8, v->AsInt64(24)));
            if (const auto* v = g.Find("TilePool"))       rec.Grid.TileSlotBudget = static_cast<uint32_t>(std::max<int64_t>(1, v->AsInt64(48)));
            if (const auto* v = g.Find("Tiles"))          rec.Grid.WorldTiles     = static_cast<uint32_t>(std::max<int64_t>(0, v->AsInt64(0)));
        }

        if (const auto* renderVal = root.Find("Render"); renderVal != nullptr && renderVal->IsTable())
        {
            const auto& r = *renderVal->AsTable();
            if (const auto* v = r.Find("Visible"))  rec.RenderVisible  = v->AsBool(true);
            if (const auto* v = r.Find("Exposure")) rec.RenderExposure = static_cast<float>(v->AsDouble(1.05));
            if (const auto* v = r.Find("Steps"))    rec.RenderSteps    = static_cast<int32_t>(v->AsInt64(72));
            if (const auto* v = r.Find("Filter"))   rec.VoxelPreview   = (v->AsString() == "Voxel");
        }

        if (const auto* debrisVal = root.Find("Debris"); debrisVal != nullptr && debrisVal->IsTable())
        {
            const auto& d = *debrisVal->AsTable();
            if (const auto* v = d.Find("Visible"))  rec.Debris.Visible  = v->AsBool(false);
            if (const auto* v = d.Find("Count"))    rec.Debris.Count    = static_cast<uint32_t>(std::max<int64_t>(64, v->AsInt64(1024)));
            if (const auto* v = d.Find("Lifetime")) rec.Debris.Lifetime = static_cast<float>(v->AsDouble(3.2));
            if (const auto* v = d.Find("Size"))     rec.Debris.Size     = static_cast<float>(v->AsDouble(2.0));
            if (const auto* v = d.Find("Gravity"))  rec.Debris.Gravity  = static_cast<float>(v->AsDouble(9.8));
            if (const auto* v = d.Find("Drag"))     rec.Debris.Drag     = static_cast<float>(v->AsDouble(1.2));
            if (const auto* v = d.Find("Bounce"))   rec.Debris.Bounce   = static_cast<float>(v->AsDouble(0.35));
            if (const auto* v = d.Find("Glow"))     rec.Debris.Glow     = static_cast<float>(v->AsDouble(1.5));
        }

        if (const auto* volArrVal = root.Find("Volume"); volArrVal != nullptr && volArrVal->IsArray())
        {
            const auto* volArr = volArrVal->AsArray();
            if (!volArr->Entries.empty() && volArr->Entries[0]->IsTable())
            {
                rec.GasStack = EL::ParseEffectLayerStackFromToml(specs, *volArr->Entries[0]->AsTable(), EL::LayerStackOrdinal::GasVolume);
            }
        }

        if (const auto* srcArrVal = root.Find("Emitter"); srcArrVal != nullptr && srcArrVal->IsArray())
        {
            for (const auto& entryPtr : srcArrVal->AsArray()->Entries)
            {
                if (!entryPtr->IsTable()) continue;
                const auto& e = *entryPtr->AsTable();
                FluidPresetSourceEntry item {};
                if (const auto* v = e.Find("Token"))       item.Token                  = std::string(v->AsString());
                if (const auto* v = e.Find("Enabled"))     item.Source.Enabled         = v->AsBool(true);
                if (const auto* v = e.Find("Shape"))       item.Source.Shape           = ParseSourceShapeOrdinal(v->AsString());
                item.Source.SourceId       = item.Token;
                item.Source.PositionMeters = ReadVec3Toml(e, "Position", item.Source.PositionMeters);
                item.Source.VelocityMps    = ReadVec3Toml(e, "Velocity", item.Source.VelocityMps);
                item.Source.Orientation    = ReadVec3Toml(e, "Orientation", item.Source.Orientation);
                if (const auto* v = e.Find("Radius"))      item.Source.RadiusMeters    = static_cast<float>(v->AsDouble(0.35));
                if (const auto* v = e.Find("Density"))     item.Source.Density         = static_cast<float>(v->AsDouble(1.0));
                if (const auto* v = e.Find("Temperature")) item.Source.Temperature     = static_cast<float>(v->AsDouble(2.0));
                if (const auto* v = e.Find("Fuel"))        item.Source.Fuel            = static_cast<float>(v->AsDouble(1.0));
                if (const auto* v = e.Find("Jitter"))      item.Source.Jitter          = static_cast<float>(v->AsDouble(0.2));
                if (const auto* v = e.Find("Pulse"))       item.Source.PulseSeconds    = static_cast<float>(v->AsDouble(0.0));
                if (const auto* v = e.Find("Debris"))      item.DebrisAmount           = static_cast<float>(v->AsDouble(0.0));
                if (const auto* v = e.Find("DebrisSpeed")) item.DebrisSpeed            = static_cast<float>(v->AsDouble(2.0));
                if (const auto* v = e.Find("DebrisKind"))  item.DebrisKind             = ParseDebrisKindOrdinal(v->AsString());
                rec.ReleaseSources.push_back(std::move(item));
            }
        }

        if (const auto* forceArrVal = root.Find("Force"); forceArrVal != nullptr && forceArrVal->IsArray())
        {
            for (const auto& entryPtr : forceArrVal->AsArray()->Entries)
            {
                if (!entryPtr->IsTable()) continue;
                const auto& f = *entryPtr->AsTable();
                if (const auto* en = f.Find("Enabled"); en != nullptr && !en->AsBool(true)) continue;

                const auto model   = ParseForceModelToken(f.Find("Type") ? f.Find("Type")->AsString() : std::string_view{});
                const auto falloff = ParseForceFalloffToken(f.Find("Falloff") ? f.Find("Falloff")->AsString() : std::string_view{"Smooth"});
                const auto pos     = ReadVec3Toml(f, "Position",  { 0.0f, 0.0f, 0.0f });
                const auto dir     = ReadVec3Toml(f, "Direction", { 1.0f, 0.0f, 0.0f });
                const float radius    = f.Find("Radius")    ? static_cast<float>(f.Find("Radius")->AsDouble(2.0))    : 2.0f;
                const float strength  = f.Find("Strength")  ? static_cast<float>(f.Find("Strength")->AsDouble(1.0))  : 1.0f;
                const float frequency = f.Find("Frequency") ? static_cast<float>(f.Find("Frequency")->AsDouble(1.0)) : 1.0f;
                const float secondary = f.Find("Secondary") ? static_cast<float>(f.Find("Secondary")->AsDouble(0.0)) : 0.0f;
                const float pulse     = f.Find("Pulse")     ? static_cast<float>(f.Find("Pulse")->AsDouble(0.0))     : 0.0f;
                const float pWidth    = f.Find("PulseWidth") ? static_cast<float>(f.Find("PulseWidth")->AsDouble(0.25)) : 0.25f;

                FF::ForceModelGpuRecord gpuRec {
                    { pos[0], pos[1], pos[2], radius },
                    { dir[0], dir[1], dir[2], strength },
                    { static_cast<float>(model), static_cast<float>(falloff), frequency, secondary },
                    { pulse, pWidth, static_cast<float>(FF::ForceTargetChannelBits::All), 1.0f }
                };
                rec.ForceRecords.push_back(gpuRec);
            }
        }

        if (const auto* colArrVal = root.Find("Collider"); colArrVal != nullptr && colArrVal->IsArray())
        {
            for (const auto& entryPtr : colArrVal->AsArray()->Entries)
            {
                if (!entryPtr->IsTable()) continue;
                const auto& c = *entryPtr->AsTable();
                if (const auto* en = c.Find("Enabled"); en != nullptr && !en->AsBool(true)) continue;
                FluidColliderSphere sphere {};
                sphere.CentreMeters = ReadVec3Toml(c, "Position", { 0.0f, 0.0f, 0.0f });
                sphere.RadiusMeters = c.Find("Radius") ? static_cast<float>(c.Find("Radius")->AsDouble(0.5)) : 0.5f;
                rec.Colliders.push_back(sphere);
            }
        }

        if (const auto* pArrVal = root.Find("Particle"); pArrVal != nullptr && pArrVal->IsArray())
        {
            for (const auto& entryPtr : pArrVal->AsArray()->Entries)
            {
                if (!entryPtr->IsTable()) continue;
                const auto& p = *entryPtr->AsTable();
                FluidPresetParticleRef pref {};
                if (const auto* v = p.Find("Template")) pref.TemplateId = std::string(v->AsString());
                if (const auto* v = p.Find("Name"))     pref.Name       = std::string(v->AsString());
                if (const auto* v = p.Find("Count"))    pref.Count      = static_cast<uint32_t>(std::max<int64_t>(0, v->AsInt64(0)));
                rec.ParticleRefs.push_back(std::move(pref));
            }
        }

        Presets.push_back(std::move(rec));
    }

    std::stable_sort(Presets.begin(), Presets.end(), [](const FluidPresetRecord& a, const FluidPresetRecord& b) {
        if (a.Order != b.Order) return a.Order < b.Order;
        return a.Id < b.Id;
    });
    return !Presets.empty();
}

const FluidPresetRecord* FluidPresetRegistry::FindById(const std::string_view presetId) const noexcept
{
    for (const auto& p : Presets)
    {
        if (p.Id == presetId) return &p;
    }
    return nullptr;
}

FluidPresetRuntimeBundle EvaluateFluidPresetRuntime(
    const EL::EffectLayerSpecificationSet& specs,
    const FluidPresetRecord& preset,
    const double simulationTimeSeconds)
{
    FluidPresetRuntimeBundle out {};
    std::vector<std::string> srcIds;
    srcIds.reserve(preset.ReleaseSources.size());
    for (const auto& s : preset.ReleaseSources) srcIds.push_back(s.Token);

    const auto eval = EL::EvaluateEffectLayerStack(specs, preset.GasStack, srcIds, simulationTimeSeconds);
    out.StackStopped = eval.IsStopped();

    out.StepSettings.Boundary            = preset.Boundary;
    out.StepSettings.Gravity             = ReadEvalFloat(eval.RegionFields, "Gravity", 0.25f);
    out.StepSettings.ForceResponse       = ReadEvalFloat(eval.RegionFields, "ForceResponse", 1.0f);
    out.StepSettings.PressureIterations  = static_cast<uint32_t>(std::max(1, ReadEvalInt(eval.RegionFields, "PressureIterations", 24)));
    out.StepSettings.Buoyancy            = ReadEvalFloat(eval.RegionFields, "Buoyancy", 1.2f);
    out.StepSettings.AmbientTemperature  = ReadEvalFloat(eval.RegionFields, "AmbientTemperature", 0.0f);
    out.StepSettings.Cooling             = ReadEvalFloat(eval.RegionFields, "Cooling", 0.65f);
    out.StepSettings.Combustion          = ReadEvalFloat(eval.RegionFields, "Combustion", 0.0f);
    out.StepSettings.Expansion           = ReadEvalFloat(eval.RegionFields, "Expansion", 0.0f);
    out.StepSettings.Vorticity           = ReadEvalFloat(eval.RegionFields, "Vorticity", 0.0f);
    out.StepSettings.Dissipation         = ReadEvalFloat(eval.RegionFields, "Dissipation", 0.992f);
    out.StepSettings.VelocityDissipation = ReadEvalFloat(eval.RegionFields, "VelocityDissipation", 0.996f);
    out.StepSettings.Advection           = (ReadEvalString(eval.RegionFields, "Advection", "MacCormack") == "SemiLagrangian")
                                         ? FluidAdvectionOrdinal::SemiLagrangian
                                         : FluidAdvectionOrdinal::MacCormack;

    out.RenderSettings.Visible       = preset.RenderVisible;
    out.RenderSettings.Exposure      = preset.RenderExposure;
    out.RenderSettings.Steps         = preset.RenderSteps;
    out.RenderSettings.VoxelPreview  = preset.VoxelPreview;
    out.RenderSettings.DensityScale  = ReadEvalFloat(eval.VolumeFields, "Density", 1.2f);
    out.RenderSettings.Absorption    = ReadEvalFloat(eval.VolumeFields, "Absorption", 2.6f);
    out.RenderSettings.Anisotropy    = ReadEvalFloat(eval.VolumeFields, "Anisotropy", 0.55f);
    out.RenderSettings.LightIntensity = ReadEvalFloat(eval.VolumeFields, "LightIntensity", 1.6f);
    out.RenderSettings.Albedo        = ParseHexRgb(ReadEvalString(eval.VolumeFields, "Albedo",  ""), out.RenderSettings.Albedo);
    out.RenderSettings.AmbientColour = ParseHexRgb(ReadEvalString(eval.VolumeFields, "Ambient", ""), out.RenderSettings.AmbientColour);
    out.RenderSettings.FireIntensity = ReadEvalFloat(eval.VolumeFields, "FireIntensity", 0.0f);
    out.RenderSettings.FireThreshold = ReadEvalFloat(eval.VolumeFields, "FireThreshold", 1.0f);
    out.RenderSettings.FireSoftness  = ReadEvalFloat(eval.VolumeFields, "FireSoftness", 3.5f);
    out.RenderSettings.FireOpacity   = ReadEvalFloat(eval.VolumeFields, "FireOpacity", 0.35f);
    out.RenderSettings.FireLight     = ReadEvalFloat(eval.VolumeFields, "FireLight", 1.0f);
    out.RenderSettings.FireEmber     = ParseHexRgb(ReadEvalString(eval.VolumeFields, "FireEmber", ""), out.RenderSettings.FireEmber);
    out.RenderSettings.FireFlame     = ParseHexRgb(ReadEvalString(eval.VolumeFields, "FireFlame", ""), out.RenderSettings.FireFlame);
    out.RenderSettings.FireCore      = ParseHexRgb(ReadEvalString(eval.VolumeFields, "FireCore",  ""), out.RenderSettings.FireCore);

    out.StepSettings.FireThreshold = out.RenderSettings.FireThreshold;
    out.StepSettings.FireSoftness  = out.RenderSettings.FireSoftness;

    out.Shimmer.Strength  = ReadEvalFloat(eval.VolumeFields, "ShimmerStrength",  0.0f);
    out.Shimmer.Frequency = ReadEvalFloat(eval.VolumeFields, "ShimmerFrequency", 14.0f);
    out.Shimmer.Speed     = ReadEvalFloat(eval.VolumeFields, "ShimmerSpeed",     2.2f);
    out.Shimmer.Chromatic = ReadEvalFloat(eval.VolumeFields, "ShimmerChromatic", 0.0f);

    for (const auto& s : preset.ReleaseSources)
    {
        bool linkedActive = s.Source.Enabled;
        for (const auto& link : eval.SourceLinks)
        {
            if (link.SourceId == s.Token)
            {
                linkedActive = link.Enabled;
                break;
            }
        }
        if (linkedActive) out.ActiveSources.push_back(s.Source);
    }
    return out;
}

void StepFluidTracersCpu(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeGridCpu& velocity,
    const uint32_t width,
    const uint32_t height,
    const float deltaTimeSeconds,
    const float simulationTimeSeconds,
    const float lifetimeSeconds,
    EL::FluidTracerDebrisBufferSet& buffers)
{
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (buffers.Tracers.size() != count)
    {
        buffers.Tracers.assign(count, { 0.0f, 0.0f, 0.0f, 0.0f });
        buffers.TracerPrevious.assign(count, { 0.0f, 0.0f, 0.0f, 0.0f });
        buffers.TracersNeedReset = true;
    }

    buffers.TracerPrevious = buffers.Tracers;
    const float h = volume.GridCentreCellSize[3];
    const float halfX = static_cast<float>(volume.GridBoundary[0]) * h * 0.5f;
    const float halfY = static_cast<float>(volume.GridBoundary[1]) * h * 0.5f;
    const float halfZ = static_cast<float>(volume.GridBoundary[2]) * h * 0.5f;

    for (uint32_t y = 0u; y < height; ++y)
    {
        for (uint32_t x = 0u; x < width; ++x)
        {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float px = static_cast<float>(x);
            const float py = static_cast<float>(y);

            if (buffers.TracersNeedReset)
            {
                const auto p = SpawnTracerPositionCpu(volume, px, py, 0.0f);
                const float age = FF::Hash31({ px, py, 3.3f }) * lifetimeSeconds;
                buffers.Tracers[idx] = { p[0], p[1], p[2], age };
                continue;
            }

            auto cur = buffers.Tracers[idx];
            float posX = cur[0], posY = cur[1], posZ = cur[2];
            float age  = cur[3] + deltaTimeSeconds;

            const auto c1 = FluidCellFromWorld(volume, { posX, posY, posZ });
            const auto v1 = velocity.SampleTrilinear(c1[0], c1[1], c1[2]);
            const auto c2 = FluidCellFromWorld(volume, {
                posX + 0.5f * deltaTimeSeconds * v1[0],
                posY + 0.5f * deltaTimeSeconds * v1[1],
                posZ + 0.5f * deltaTimeSeconds * v1[2]
            });
            const auto v2 = velocity.SampleTrilinear(c2[0], c2[1], c2[2]);
            posX += deltaTimeSeconds * v2[0];
            posY += deltaTimeSeconds * v2[1];
            posZ += deltaTimeSeconds * v2[2];

            const bool outside = std::fabs(posX - volume.GridCentreCellSize[0]) > halfX
                              || std::fabs(posY - volume.GridCentreCellSize[1]) > halfY
                              || std::fabs(posZ - volume.GridCentreCellSize[2]) > halfZ;
            if (age > lifetimeSeconds || outside)
            {
                const auto p = SpawnTracerPositionCpu(volume, px, py, simulationTimeSeconds);
                posX = p[0]; posY = p[1]; posZ = p[2];
                age  = 0.0f;
            }
            buffers.Tracers[idx] = { posX, posY, posZ, age };
        }
    }

    if (buffers.TracersNeedReset)
    {
        buffers.TracersNeedReset = false;
        buffers.TracerPrevious   = buffers.Tracers;
    }
}

void StepFluidDebrisCpu(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeGridCpu& velocity,
    const std::span<const FluidPresetSourceEntry> sources,
    const FluidDebrisSettings& settings,
    const uint32_t width,
    const uint32_t height,
    const float deltaTimeSeconds,
    const float simulationTimeSeconds,
    EL::FluidTracerDebrisBufferSet& buffers)
{
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (buffers.DebrisPositions.size() != count)
    {
        buffers.DebrisPositions.assign(count, { 0.0f, -1.0e4f, 0.0f, settings.Lifetime + 1.0f });
        buffers.DebrisVelocities.assign(count, { 0.0f, 0.0f, 0.0f, 0.0f });
        buffers.DebrisPrevious.assign(count, { 0.0f, -1.0e4f, 0.0f, settings.Lifetime + 1.0f });
        buffers.DebrisNeedReset = true;
    }

    buffers.DebrisPrevious = buffers.DebrisPositions;
    const float h = volume.GridCentreCellSize[3];
    const float halfX = static_cast<float>(volume.GridBoundary[0]) * h * 0.5f;
    const float halfY = static_cast<float>(volume.GridBoundary[1]) * h * 0.5f;
    const float halfZ = static_cast<float>(volume.GridBoundary[2]) * h * 0.5f;
    const float floorY = volume.GridCentreCellSize[1] - halfY + h * 0.5f;
    const size_t srcCount = std::min(sources.size(), static_cast<size_t>(kMaximumReleaseSources));

    for (uint32_t y = 0u; y < height; ++y)
    {
        for (uint32_t x = 0u; x < width; ++x)
        {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float px = static_cast<float>(x);
            const float py = static_cast<float>(y);

            if (buffers.DebrisNeedReset)
            {
                const float initSeed = FF::Hash31({ px, py, 11.7f });
                buffers.DebrisPositions[idx]  = { 0.0f, -1.0e4f, 0.0f, settings.Lifetime + 1.0f };
                buffers.DebrisVelocities[idx] = { 0.0f, 0.0f, 0.0f, initSeed };
                continue;
            }

            auto p = buffers.DebrisPositions[idx];
            auto v = buffers.DebrisVelocities[idx];
            float posX = p[0], posY = p[1], posZ = p[2], age = p[3];
            float velX = v[0], velY = v[1], velZ = v[2], vw  = v[3];

            bool dead = (age >= settings.Lifetime);
            if (dead && srcCount > 0u)
            {
                const size_t srcIdx = static_cast<size_t>(std::fmod(px + py * 7.0f, static_cast<float>(srcCount)));
                const auto& src = sources[srcIdx];
                const float chance = src.DebrisAmount * src.Source.EnvelopeWeight * deltaTimeSeconds * 9.0f;
                if (FF::Hash31({ px, py, simulationTimeSeconds }) < chance)
                {
                    const auto dir = RandomUnitDirectionCpu(px, py, simulationTimeSeconds + 3.3f);
                    const float rScale = src.Source.RadiusMeters * FF::Hash31({ py, px, simulationTimeSeconds });
                    posX = src.Source.PositionMeters[0] + dir[0] * rScale;
                    posY = src.Source.PositionMeters[1] + dir[1] * rScale;
                    posZ = src.Source.PositionMeters[2] + dir[2] * rScale;
                    const float spd = src.DebrisSpeed * (0.35f + 0.65f * FF::Hash31({ px, py, simulationTimeSeconds + 9.1f }));
                    velX = src.Source.VelocityMps[0] + dir[0] * spd;
                    velY = src.Source.VelocityMps[1] + dir[1] * spd + 0.45f * src.DebrisSpeed;
                    velZ = src.Source.VelocityMps[2] + dir[2] * spd;
                    vw   = static_cast<float>(src.DebrisKind) + FractF(vw);
                    age  = 0.0f;
                    dead = false;
                }
            }

            if (!dead)
            {
                age += deltaTimeSeconds;
                const int32_t kind = static_cast<int32_t>(std::floor(vw));
                const float seedValue = FractF(vw);
                const auto cell = FluidCellFromWorld(volume, { posX, posY, posZ });
                const auto gas  = velocity.SampleTrilinear(cell[0], cell[1], cell[2]);

                const float gravScale = (kind == 1) ? 0.12f : ((kind == 2) ? 0.30f : 1.0f);
                const float dragScale = (kind == 1) ? 3.00f : ((kind == 2) ? 4.00f : 1.0f);
                velY -= settings.Gravity * gravScale * deltaTimeSeconds;
                if (kind == 1)
                {
                    velY += (0.9f + 0.6f * seedValue) * deltaTimeSeconds * std::exp(-age * 0.6f);
                }
                if (kind != 0)
                {
                    const auto cn = FF::CurlNoise({
                        posX * 1.7f + simulationTimeSeconds * 0.8f,
                        posY * 1.7f + seedValue * 9.0f,
                        posZ * 1.7f
                    });
                    const float cnScale = ((kind == 2) ? 1.6f : 0.5f) * deltaTimeSeconds;
                    velX += cn[0] * cnScale;
                    velY += cn[1] * cnScale;
                    velZ += cn[2] * cnScale;
                }
                const float blend = std::min(1.0f, settings.Drag * dragScale * deltaTimeSeconds * (0.6f + 0.8f * seedValue));
                velX += (gas[0] - velX) * blend;
                velY += (gas[1] - velY) * blend;
                velZ += (gas[2] - velZ) * blend;

                posX += velX * deltaTimeSeconds;
                posY += velY * deltaTimeSeconds;
                posZ += velZ * deltaTimeSeconds;

                if (posY < floorY)
                {
                    posY = floorY;
                    velY = std::fabs(velY) * settings.Bounce;
                    velX *= 0.55f;
                    velZ *= 0.55f;
                    if (velY < 0.25f) { velX = 0.0f; velY = 0.0f; velZ = 0.0f; }
                    if (kind == 1) age = settings.Lifetime;
                }

                const float lx = posX - volume.GridCentreCellSize[0];
                const float ly = posY - volume.GridCentreCellSize[1];
                const float lz = posZ - volume.GridCentreCellSize[2];
                if (std::fabs(lx) > halfX || std::fabs(lz) > halfZ || ly > halfY)
                {
                    age = settings.Lifetime;
                }
            }

            buffers.DebrisPositions[idx]  = { posX, posY, posZ, age };
            buffers.DebrisVelocities[idx] = { velX, velY, velZ, vw };
        }
    }

    if (buffers.DebrisNeedReset)
    {
        buffers.DebrisNeedReset = false;
        buffers.DebrisPrevious  = buffers.DebrisPositions;
    }
}

bool WriteRgba8PngFile(
    const std::string_view filePath,
    const uint32_t width,
    const uint32_t height,
    const std::span<const uint8_t> rgba8Bytes)
{
    const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (width == 0u || height == 0u || rgba8Bytes.size() < expected) return false;

    // Build raw scanlines (1 filter byte 0x00 per row + 4*width RGBA bytes)
    const size_t rowStride = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (rowStride + 1u));
    for (uint32_t y = 0u; y < height; ++y)
    {
        raw.push_back(0u);
        const uint8_t* rowPtr = rgba8Bytes.data() + static_cast<size_t>(y) * rowStride;
        raw.insert(raw.end(), rowPtr, rowPtr + rowStride);
    }

    // Wrap raw scanlines in valid RFC 1950 zlib stream using stored deflate blocks (max 65535 bytes/block)
    std::vector<uint8_t> zlibData;
    zlibData.reserve(raw.size() + 64u);
    zlibData.push_back(0x78u);
    zlibData.push_back(0x01u);
    size_t offset = 0u;
    while (offset < raw.size())
    {
        const size_t chunkLen = std::min<size_t>(65535u, raw.size() - offset);
        const bool isFinal = (offset + chunkLen >= raw.size());
        zlibData.push_back(isFinal ? 0x01u : 0x00u);
        const uint16_t len16  = static_cast<uint16_t>(chunkLen);
        const uint16_t nlen16 = static_cast<uint16_t>(~len16);
        zlibData.push_back(static_cast<uint8_t>(len16 & 0xFFu));
        zlibData.push_back(static_cast<uint8_t>((len16 >> 8u) & 0xFFu));
        zlibData.push_back(static_cast<uint8_t>(nlen16 & 0xFFu));
        zlibData.push_back(static_cast<uint8_t>((nlen16 >> 8u) & 0xFFu));
        zlibData.insert(zlibData.end(), raw.begin() + static_cast<ptrdiff_t>(offset), raw.begin() + static_cast<ptrdiff_t>(offset + chunkLen));
        offset += chunkLen;
    }
    AppendBe32(zlibData, ComputeAdler32(raw.data(), raw.size()));

    std::vector<uint8_t> png;
    static constexpr uint8_t kSig[8] { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    png.insert(png.end(), kSig, kSig + 8);

    std::vector<uint8_t> ihdr;
    AppendBe32(ihdr, width);
    AppendBe32(ihdr, height);
    ihdr.push_back(8u); // bit depth
    ihdr.push_back(6u); // RGBA colour type
    ihdr.push_back(0u); // compression
    ihdr.push_back(0u); // filter
    ihdr.push_back(0u); // interlace
    AppendPngChunk(png, "IHDR", ihdr);
    AppendPngChunk(png, "IDAT", zlibData);
    AppendPngChunk(png, "IEND", {});

    std::ofstream out(std::string(filePath), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

} // namespace Frontier::SimulationSpace::Fluid
