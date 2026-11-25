/*============================================================================================================================================
                                                          FORCEFIELDREGISTER.CPP
============================================================================================================================================*/
// 🧩 Authoritative force-node register with KeyTrack/TimingEnvelope evaluation, TOML preset loading, and wait-free cross-thread publication
// Reference: Experimental/FluidEditor/Engine/ForceGeneratorCatalog.js L1-L458, KeyTracks.js L294-L341, FluidSolver.js L419-L438

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldRegister.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

[[nodiscard]] double ReadScalarFromKeyValue(const KeyValueVariant& value, const double fallback) noexcept
{
    if (const auto* d = std::get_if<double>(&value))
    {
        return std::isfinite(*d) ? *d : fallback;
    }
    return fallback;
}

[[nodiscard]] std::array<float, 3> ReadVec3FromKeyValue(
    const KeyValueVariant& value,
    const std::array<float, 3>& fallback) noexcept
{
    if (const auto* v = std::get_if<std::array<double, 3>>(&value))
    {
        return {
            std::isfinite((*v)[0]) ? static_cast<float>((*v)[0]) : fallback[0],
            std::isfinite((*v)[1]) ? static_cast<float>((*v)[1]) : fallback[1],
            std::isfinite((*v)[2]) ? static_cast<float>((*v)[2]) : fallback[2]
        };
    }
    return fallback;
}

[[nodiscard]] bool ReadBoolFromKeyValue(const KeyValueVariant& value, const bool fallback) noexcept
{
    if (const auto* s = std::get_if<std::string>(&value))
    {
        if (*s == "true" || *s == "1") return true;
        if (*s == "false" || *s == "0") return false;
    }
    if (const auto* d = std::get_if<double>(&value))
    {
        return *d >= 0.5;
    }
    return fallback;
}

[[nodiscard]] std::array<float, 3> ReadVec3FromToml(
    const EffectLayers::TomlTable& table,
    const std::string_view key,
    const std::array<float, 3>& fallback) noexcept
{
    const EffectLayers::TomlValue* v = table.Find(key);
    if (v == nullptr || !v->IsArray()) return fallback;
    const auto* arr = v->AsArray();
    if (arr->Entries.size() < 3u) return fallback;
    return {
        static_cast<float>(arr->Entries[0]->AsDouble(fallback[0])),
        static_cast<float>(arr->Entries[1]->AsDouble(fallback[1])),
        static_cast<float>(arr->Entries[2]->AsDouble(fallback[2]))
    };
}

[[nodiscard]] KeyEaseOrdinal ParseEaseOrdinal(const EffectLayers::TomlValue* val) noexcept
{
    if (val == nullptr) return KeyEaseOrdinal::Linear;
    if (val->IsString())
    {
        const std::string_view s = val->AsString();
        if (s == "Constant") return KeyEaseOrdinal::Constant;
        if (s == "Linear")   return KeyEaseOrdinal::Linear;
        if (s == "Smooth")   return KeyEaseOrdinal::Smooth;
        if (s == "Bezier")   return KeyEaseOrdinal::Bezier;
    }
    const int64_t code = val->AsInt64(1);
    if (code >= 0 && code <= 3) return static_cast<KeyEaseOrdinal>(code);
    return KeyEaseOrdinal::Linear;
}

[[nodiscard]] KeyExtrapolationOrdinal ParseExtrapolationOrdinal(const EffectLayers::TomlValue* val) noexcept
{
    if (val == nullptr) return KeyExtrapolationOrdinal::Hold;
    if (val->IsString())
    {
        const std::string_view s = val->AsString();
        if (s == "Cycle")    return KeyExtrapolationOrdinal::Cycle;
        if (s == "PingPong") return KeyExtrapolationOrdinal::PingPong;
        return KeyExtrapolationOrdinal::Hold;
    }
    const int64_t code = val->AsInt64(0);
    if (code >= 0 && code <= 2) return static_cast<KeyExtrapolationOrdinal>(code);
    return KeyExtrapolationOrdinal::Hold;
}

[[nodiscard]] std::vector<KeyTrack> ParseNodeTracksFromToml(const EffectLayers::TomlTable& table)
{
    std::vector<KeyTrack> tracks;
    const EffectLayers::TomlValue* trackVal = table.Find("Track");
    if (trackVal == nullptr) trackVal = table.Find("Tracks");
    if (trackVal == nullptr || !trackVal->IsArray()) return tracks;

    for (const auto& entryPtr : trackVal->AsArray()->Entries)
    {
        const EffectLayers::TomlTable* t = entryPtr->AsTable();
        if (t == nullptr) continue;
        const EffectLayers::TomlValue* pathVal = t->Find("Path");
        if (pathVal == nullptr || !pathVal->IsString()) continue;

        KeyTrack track {};
        track.Path          = std::string(pathVal->AsString());
        track.Extrapolation = ParseExtrapolationOrdinal(t->Find("Extrapolation"));
        track.PeriodSeconds = t->Find("Period") ? t->Find("Period")->AsDouble(0.0) : 0.0;
        track.FromEnvelope  = t->Find("FromEnvelope") ? t->Find("FromEnvelope")->AsBool(false) : false;

        const EffectLayers::TomlValue* keysVal = t->Find("Keys");
        if (keysVal == nullptr) keysVal = t->Find("Key");
        if (keysVal != nullptr && keysVal->IsArray())
        {
            for (const auto& kPtr : keysVal->AsArray()->Entries)
            {
                const EffectLayers::TomlTable* kt = kPtr->AsTable();
                if (kt == nullptr) continue;
                TrackKey key {};
                key.TimeSeconds = kt->Find("Time") ? kt->Find("Time")->AsDouble(0.0) : 0.0;
                key.Ease        = ParseEaseOrdinal(kt->Find("Ease"));
                if (const EffectLayers::TomlValue* amt = kt->Find("Amount"))
                {
                    if (amt->IsArray() && amt->AsArray()->Entries.size() >= 3u)
                    {
                        const auto& e = amt->AsArray()->Entries;
                        key.Amount = std::array<double, 3> { e[0]->AsDouble(), e[1]->AsDouble(), e[2]->AsDouble() };
                    }
                    else if (amt->IsBool())
                    {
                        key.Amount = amt->AsBool() ? std::string("true") : std::string("false");
                    }
                    else if (amt->IsString())
                    {
                        key.Amount = std::string(amt->AsString());
                    }
                    else
                    {
                        key.Amount = amt->AsDouble(0.0);
                    }
                }
                if (const EffectLayers::TomlValue* inT = kt->Find("In"))
                {
                    if (inT->IsArray() && inT->AsArray()->Entries.size() >= 3u)
                    {
                        const auto& e = inT->AsArray()->Entries;
                        key.InTangent = { e[0]->AsDouble(), e[1]->AsDouble(), e[2]->AsDouble() };
                    }
                    else
                    {
                        key.InTangent = { inT->AsDouble(), 0.0, 0.0 };
                    }
                }
                if (const EffectLayers::TomlValue* outT = kt->Find("Out"))
                {
                    if (outT->IsArray() && outT->AsArray()->Entries.size() >= 3u)
                    {
                        const auto& e = outT->AsArray()->Entries;
                        key.OutTangent = { e[0]->AsDouble(), e[1]->AsDouble(), e[2]->AsDouble() };
                    }
                    else
                    {
                        key.OutTangent = { outT->AsDouble(), 0.0, 0.0 };
                    }
                }
                track.Keys.push_back(std::move(key));
            }
        }
        if (!track.Keys.empty())
        {
            std::sort(track.Keys.begin(), track.Keys.end(), [](const TrackKey& a, const TrackKey& b) noexcept
            {
                return a.TimeSeconds < b.TimeSeconds;
            });
            tracks.push_back(std::move(track));
        }
    }
    return tracks;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               KEYED NODE EVALUATION HELPERS
//------------------------------------------------------------------------------------------------------------------------

ForceModelNode EvaluateKeyedForceNode(
    const ManagedForceEntry& entry,
    const double simulationSeconds) noexcept
{
    // Reference: Experimental/FluidEditor/Engine/KeyTracks.js L329-L341
    ForceModelNode result = entry.Node;
    if (entry.Tracks.empty())
    {
        return result;
    }

    for (const KeyTrack& track : entry.Tracks)
    {
        if (track.Path == "Weight" || track.Keys.empty())
        {
            continue;
        }
        const bool isStepped = (track.Path == "Enabled" || track.Path == "Falloff");
        const auto sampled = SampleKeyTrack(track, simulationSeconds, isStepped);
        if (!sampled.has_value())
        {
            continue;
        }

        if (track.Path == "Position")
        {
            result.PositionMeters = ReadVec3FromKeyValue(*sampled, result.PositionMeters);
        }
        else if (track.Path == "Direction")
        {
            result.Direction = ReadVec3FromKeyValue(*sampled, result.Direction);
        }
        else if (track.Path == "Strength")
        {
            result.Strength = static_cast<float>(ReadScalarFromKeyValue(*sampled, result.Strength));
        }
        else if (track.Path == "Radius")
        {
            result.RadiusMeters = std::max(0.05f, static_cast<float>(ReadScalarFromKeyValue(*sampled, result.RadiusMeters)));
        }
        else if (track.Path == "Frequency")
        {
            result.Frequency = static_cast<float>(ReadScalarFromKeyValue(*sampled, result.Frequency));
        }
        else if (track.Path == "Secondary")
        {
            result.Secondary = static_cast<float>(ReadScalarFromKeyValue(*sampled, result.Secondary));
        }
        else if (track.Path == "Tertiary")
        {
            result.Tertiary = static_cast<float>(ReadScalarFromKeyValue(*sampled, result.Tertiary));
        }
        else if (track.Path == "Enabled")
        {
            result.Enabled = ReadBoolFromKeyValue(*sampled, result.Enabled);
        }
        else if (track.Path == "Falloff")
        {
            if (const auto* s = std::get_if<std::string>(&(*sampled)))
            {
                if (const auto parsed = ParseFalloffTomlToken(*s))
                {
                    result.Falloff = *parsed;
                }
            }
        }
    }
    return result;
}

float EvaluateForceNodeWeight(
    const ManagedForceEntry& entry,
    const double simulationSeconds) noexcept
{
    // Reference: Experimental/FluidEditor/Engine/KeyTracks.js L294-L298
    const TimingEnvelopeParameters env {
        static_cast<double>(entry.Node.StartTimeSec),
        static_cast<double>(entry.Node.DurationSec),
        static_cast<double>(entry.Node.FadeSec),
        static_cast<double>(entry.Node.RepeatSec)
    };
    const double w = EvaluateNodeTimingWeight(entry.Tracks, env, simulationSeconds);
    return static_cast<float>(std::clamp(w, 0.0, 1.0));
}

std::vector<ManagedForceEntry> ParseForceEntriesFromToml(
    const EffectLayers::TomlTable& documentRoot)
{
    std::vector<ManagedForceEntry> result;
    const EffectLayers::TomlValue* forceArr = documentRoot.Find("Force");
    if (forceArr == nullptr) forceArr = documentRoot.Find("Forces");
    if (forceArr == nullptr || !forceArr->IsArray())
    {
        return result;
    }

    uint32_t seq = 0u;
    for (const auto& entryPtr : forceArr->AsArray()->Entries)
    {
        const EffectLayers::TomlTable* tbl = entryPtr->AsTable();
        if (tbl == nullptr) continue;
        ++seq;

        ForceModelOrdinal ordinal = ForceModelOrdinal::DirectionalWind;
        if (const EffectLayers::TomlValue* typeVal = tbl->Find("Type"))
        {
            if (typeVal->IsString())
            {
                if (const ForceModelDescriptor* desc = FindForceModelByTomlId(typeVal->AsString()))
                {
                    ordinal = desc->Code;
                }
            }
            else
            {
                const int64_t code = typeVal->AsInt64(0);
                if (code >= 0 && code < static_cast<int64_t>(kForceModelCount))
                {
                    ordinal = static_cast<ForceModelOrdinal>(code);
                }
            }
        }

        std::string nodeId = tbl->Find("Id") ? std::string(tbl->Find("Id")->AsString())
                           : (tbl->Find("Token") ? std::string(tbl->Find("Token")->AsString())
                           : ("force-" + std::to_string(seq)));

        ForceModelNode node = ConstructDefaultForceNode(ordinal, nodeId);
        if (const EffectLayers::TomlValue* v = tbl->Find("Name"); v && v->IsString()) node.Name = std::string(v->AsString());
        if (const EffectLayers::TomlValue* v = tbl->Find("Enabled")) node.Enabled = v->AsBool(node.Enabled);
        if (const EffectLayers::TomlValue* v = tbl->Find("Visible")) node.Visible = v->AsBool(node.Visible);
        node.PositionMeters = ReadVec3FromToml(*tbl, "Position", node.PositionMeters);
        node.Direction      = ReadVec3FromToml(*tbl, "Direction", node.Direction);
        if (const EffectLayers::TomlValue* v = tbl->Find("Strength"))  node.Strength     = static_cast<float>(v->AsDouble(node.Strength));
        if (const EffectLayers::TomlValue* v = tbl->Find("Radius"))    node.RadiusMeters = static_cast<float>(v->AsDouble(node.RadiusMeters));
        if (const EffectLayers::TomlValue* v = tbl->Find("Frequency")) node.Frequency    = static_cast<float>(v->AsDouble(node.Frequency));
        if (const EffectLayers::TomlValue* v = tbl->Find("Secondary")) node.Secondary    = static_cast<float>(v->AsDouble(node.Secondary));
        if (const EffectLayers::TomlValue* v = tbl->Find("Tertiary"))  node.Tertiary     = static_cast<float>(v->AsDouble(node.Tertiary));
        if (const EffectLayers::TomlValue* v = tbl->Find("StartTime")) node.StartTimeSec = static_cast<float>(v->AsDouble(node.StartTimeSec));
        if (const EffectLayers::TomlValue* v = tbl->Find("Duration"))  node.DurationSec  = static_cast<float>(v->AsDouble(node.DurationSec));
        if (const EffectLayers::TomlValue* v = tbl->Find("Fade"))      node.FadeSec      = static_cast<float>(v->AsDouble(node.FadeSec));
        if (const EffectLayers::TomlValue* v = tbl->Find("Repeat"))    node.RepeatSec    = static_cast<float>(v->AsDouble(node.RepeatSec));
        if (const EffectLayers::TomlValue* v = tbl->Find("Falloff"))
        {
            if (v->IsString())
            {
                if (const auto parsed = ParseFalloffTomlToken(v->AsString()))
                {
                    node.Falloff = *parsed;
                }
            }
            else
            {
                const int64_t code = v->AsInt64(0);
                if (code >= 0 && code < static_cast<int64_t>(kForceFalloffCount))
                {
                    node.Falloff = static_cast<ForceFalloffOrdinal>(code);
                }
            }
        }

        ManagedForceEntry managed {};
        managed.Node   = std::move(node);
        managed.Tracks = ParseNodeTracksFromToml(*tbl);
        result.push_back(std::move(managed));
    }
    return result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FORCEFIELDREGISTER
//------------------------------------------------------------------------------------------------------------------------

ForceFieldRegister::ForceFieldRegister() noexcept
{
    PublishedForceFieldReading initial {};
    Exchange.InitializeSeed(initial, 0.0, 0ull);
}

void ForceFieldRegister::Clear() noexcept
{
    Entries.clear();
    NextAutoSequence = 1u;
}

ManagedForceEntry& ForceFieldRegister::AddForceNode(
    const ForceModelOrdinal modelCode,
    const std::string_view nodeId)
{
    std::string id = nodeId.empty()
        ? ("force-" + std::to_string(NextAutoSequence++))
        : std::string(nodeId);
    ManagedForceEntry entry {};
    entry.Node = ConstructDefaultForceNode(modelCode, id);
    Entries.push_back(std::move(entry));
    return Entries.back();
}

ManagedForceEntry& ForceFieldRegister::AddForceEntry(ManagedForceEntry entry)
{
    if (entry.Node.NodeId.empty())
    {
        entry.Node.NodeId = "force-" + std::to_string(NextAutoSequence++);
    }
    Entries.push_back(std::move(entry));
    return Entries.back();
}

bool ForceFieldRegister::RemoveForceNode(const std::string_view nodeId) noexcept
{
    for (auto it = Entries.begin(); it != Entries.end(); ++it)
    {
        if (it->Node.NodeId == nodeId)
        {
            Entries.erase(it);
            return true;
        }
    }
    return false;
}

ManagedForceEntry* ForceFieldRegister::FindForceEntry(const std::string_view nodeId) noexcept
{
    for (ManagedForceEntry& entry : Entries)
    {
        if (entry.Node.NodeId == nodeId) return &entry;
    }
    return nullptr;
}

const ManagedForceEntry* ForceFieldRegister::FindForceEntry(const std::string_view nodeId) const noexcept
{
    for (const ManagedForceEntry& entry : Entries)
    {
        if (entry.Node.NodeId == nodeId) return &entry;
    }
    return nullptr;
}

uint32_t ForceFieldRegister::LoadFromTomlTable(const EffectLayers::TomlTable& documentRoot)
{
    Clear();
    Entries = ParseForceEntriesFromToml(documentRoot);
    NextAutoSequence = static_cast<uint32_t>(Entries.size()) + 1u;
    return static_cast<uint32_t>(Entries.size());
}

uint32_t ForceFieldRegister::PublishEvaluatedReading(
    const double simulationSeconds,
    const uint64_t stepOrdinal,
    const uint32_t maxRecordCap) noexcept
{
    // Reference: Experimental/FluidEditor/Engine/FluidSolver.js L419-L438
    const uint32_t effectiveCap = std::min(maxRecordCap, kMaximumGpuForceModels);
    uint32_t packedCount = 0u;

    (void)Exchange.PublishInPlace(
        [&](PublishedForceFieldReading& reading) noexcept
        {
            reading.SimulationSeconds = simulationSeconds;
            uint32_t count = 0u;
            for (const ManagedForceEntry& entry : Entries)
            {
                if (count >= effectiveCap) break;
                const ForceModelNode* evalPtr = &entry.Node;
                ForceModelNode scratchNode {};
                if (!entry.Tracks.empty())
                {
                    // Copy POD parameters only (omit std::string NodeId/Name -> zero heap allocation per tick)
                    scratchNode.ModelCode      = entry.Node.ModelCode;
                    scratchNode.Falloff        = entry.Node.Falloff;
                    scratchNode.Enabled        = entry.Node.Enabled;
                    scratchNode.PositionMeters = entry.Node.PositionMeters;
                    scratchNode.Direction      = entry.Node.Direction;
                    scratchNode.Strength       = entry.Node.Strength;
                    scratchNode.RadiusMeters   = entry.Node.RadiusMeters;
                    scratchNode.Frequency      = entry.Node.Frequency;
                    scratchNode.Secondary      = entry.Node.Secondary;
                    scratchNode.Tertiary       = entry.Node.Tertiary;
                    scratchNode.StartTimeSec   = entry.Node.StartTimeSec;
                    scratchNode.DurationSec    = entry.Node.DurationSec;
                    scratchNode.FadeSec        = entry.Node.FadeSec;
                    scratchNode.RepeatSec      = entry.Node.RepeatSec;
                    scratchNode.TargetChannels = entry.Node.TargetChannels;
                    for (const KeyTrack& track : entry.Tracks)
                    {
                        if (track.Path == "Weight" || track.Keys.empty()) continue;
                        const bool isStepped = (track.Path == "Enabled" || track.Path == "Falloff");
                        const auto sampled = SampleKeyTrack(track, simulationSeconds, isStepped);
                        if (!sampled.has_value()) continue;
                        if (track.Path == "Position")       scratchNode.PositionMeters = ReadVec3FromKeyValue(*sampled, scratchNode.PositionMeters);
                        else if (track.Path == "Direction") scratchNode.Direction      = ReadVec3FromKeyValue(*sampled, scratchNode.Direction);
                        else if (track.Path == "Strength")  scratchNode.Strength       = static_cast<float>(ReadScalarFromKeyValue(*sampled, scratchNode.Strength));
                        else if (track.Path == "Radius")    scratchNode.RadiusMeters   = std::max(0.05f, static_cast<float>(ReadScalarFromKeyValue(*sampled, scratchNode.RadiusMeters)));
                        else if (track.Path == "Frequency") scratchNode.Frequency      = static_cast<float>(ReadScalarFromKeyValue(*sampled, scratchNode.Frequency));
                        else if (track.Path == "Secondary") scratchNode.Secondary      = static_cast<float>(ReadScalarFromKeyValue(*sampled, scratchNode.Secondary));
                        else if (track.Path == "Tertiary")  scratchNode.Tertiary       = static_cast<float>(ReadScalarFromKeyValue(*sampled, scratchNode.Tertiary));
                        else if (track.Path == "Enabled")   scratchNode.Enabled        = ReadBoolFromKeyValue(*sampled, scratchNode.Enabled);
                    }
                    evalPtr = &scratchNode;
                }
                const ForceModelNode& keyed = *evalPtr;
                if (!keyed.Enabled) continue;
                const float envelope = EvaluateForceNodeWeight(entry, simulationSeconds);
                reading.Records[count] = PackForceModelGpuRecord(keyed, envelope);
                ++count;
            }
            reading.ActiveCount = count;
            packedCount = count;
        },
        simulationSeconds,
        stepOrdinal);

    return packedCount;
}

ForceFieldRegister::ReadingLeaseType ForceFieldRegister::AcquirePublishedReading() const noexcept
{
    return Exchange.AcquireReadLease();
}

uint64_t ForceFieldRegister::QueryPublishedGeneration() const noexcept
{
    return Exchange.QueryPublishedGeneration();
}

} // namespace Frontier::SimulationSpace::ForceFields
