/*============================================================================================================================================
                                                           EFFECTLAYERSTACK.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 implementation of Experimental/FluidEditor/Engine/LayerStack.js L1-L441
// Reference: Experimental/FluidEditor/Engine/LayerStack.js L1-L441

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerStack.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Frontier::SimulationSpace::EffectLayers
{

namespace
{

[[nodiscard]] std::string MixHexColoursLinear(const std::string_view fromHex, const std::string_view toHex, const double weight)
{
    // Reference: Experimental/FluidEditor/Engine/LayerStack.js L143-L152
    if (weight >= 1.0) return std::string(toHex);
    if (weight <= 0.0) return std::string(fromHex);
    const auto a = ForceFields::HexColourToLinearRgb(fromHex);
    const auto b = ForceFields::HexColourToLinearRgb(toHex);
    const std::array<double, 3> mixed = {
        a[0] + (b[0] - a[0]) * weight,
        a[1] + (b[1] - a[1]) * weight,
        a[2] + (b[2] - a[2]) * weight
    };
    return ForceFields::LinearRgbToHexColour(mixed);
}

[[nodiscard]] double AsNumeric(const LayerParameterValue& v) noexcept
{
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* i = std::get_if<int64_t>(&v)) return static_cast<double>(*i);
    return 0.0;
}

// Reference: Experimental/FluidEditor/Engine/LayerStack.js L154-L176
[[nodiscard]] LayerParameterValue BlendParameterValue(
    const ParameterSpecification& parameter,
    const LayerParameterValue& current,
    const LayerParameterValue& amount,
    const double weight,
    const LayerBlendOrdinal blend)
{
    if (parameter.Type == ParameterTypeOrdinal::ColourLinear)
    {
        const auto* fromS = std::get_if<std::string>(&current);
        const auto* toS   = std::get_if<std::string>(&amount);
        if (fromS != nullptr && toS != nullptr)
        {
            return MixHexColoursLinear(*fromS, *toS, weight);
        }
        return amount;
    }
    if (parameter.Type == ParameterTypeOrdinal::Float32
        || parameter.Type == ParameterTypeOrdinal::Int32
        || parameter.Type == ParameterTypeOrdinal::UInt32)
    {
        const double cur = AsNumeric(current);
        const double amt = AsNumeric(amount);
        const double neu = AsNumeric(parameter.NeutralVal);
        double next = amt;
        if (blend == LayerBlendOrdinal::Add)
        {
            next = cur + (amt - neu) * weight;
        }
        else if (blend == LayerBlendOrdinal::Multiply)
        {
            next = cur * (1.0 + (amt - 1.0) * weight);
        }
        else if (blend == LayerBlendOrdinal::Maximum)
        {
            next = std::max(cur, (weight >= 1.0) ? amt : (cur + (amt - cur) * weight));
        }
        else
        {
            next = (weight >= 1.0) ? amt : (cur + (amt - cur) * weight);
        }
        if (parameter.Type == ParameterTypeOrdinal::Float32)
        {
            return next;
        }
        return static_cast<int64_t>(std::llround(next));
    }
    if (parameter.Type == ParameterTypeOrdinal::Vector3)
    {
        const auto* curV = std::get_if<std::array<double, 3>>(&current);
        const auto* amtV = std::get_if<std::array<double, 3>>(&amount);
        const auto* neuV = std::get_if<std::array<double, 3>>(&parameter.NeutralVal);
        if (curV != nullptr && amtV != nullptr && neuV != nullptr)
        {
            if (blend == LayerBlendOrdinal::Add)
            {
                return std::array<double, 3> {
                    (*curV)[0] + ((*amtV)[0] - (*neuV)[0]) * weight,
                    (*curV)[1] + ((*amtV)[1] - (*neuV)[1]) * weight,
                    (*curV)[2] + ((*amtV)[2] - (*neuV)[2]) * weight
                };
            }
            if (blend == LayerBlendOrdinal::Replace && weight < 1.0)
            {
                return std::array<double, 3> {
                    (*curV)[0] + ((*amtV)[0] - (*curV)[0]) * weight,
                    (*curV)[1] + ((*amtV)[1] - (*curV)[1]) * weight,
                    (*curV)[2] + ((*amtV)[2] - (*curV)[2]) * weight
                };
            }
        }
        return amount;
    }
    return amount;
}

[[nodiscard]] EvaluatedParameterBlock* SelectTargetBlock(StackEvaluationOutput& out, const ParameterTargetOrdinal target) noexcept
{
    switch (target)
    {
        case ParameterTargetOrdinal::Region:    return &out.RegionFields;
        case ParameterTargetOrdinal::Volume:    return &out.VolumeFields;
        case ParameterTargetOrdinal::Particles: return &out.ParticleFields;
        case ParameterTargetOrdinal::Link:
        default:                                return nullptr;
    }
}

[[nodiscard]] bool TargetBelongsToStack(const LayerStackOrdinal stackKind, const ParameterTargetOrdinal target) noexcept
{
    if (stackKind == LayerStackOrdinal::GasVolume)
    {
        return target == ParameterTargetOrdinal::Region || target == ParameterTargetOrdinal::Volume;
    }
    return target == ParameterTargetOrdinal::Particles;
}

[[nodiscard]] std::vector<ForceFields::KeyTrack> ParseTracksFromTomlArray(const TomlArray* trackArray)
{
    std::vector<ForceFields::KeyTrack> tracks;
    if (trackArray == nullptr)
    {
        return tracks;
    }
    for (const auto& entry : trackArray->Entries)
    {
        const TomlTable* tbl = entry->AsTable();
        if (tbl == nullptr) continue;
        const TomlValue* pathVal = tbl->Find("Path");
        const TomlValue* keysVal = tbl->Find("Keys");
        if (keysVal == nullptr) keysVal = tbl->Find("Key");
        if (pathVal == nullptr || !pathVal->IsString() || keysVal == nullptr || !keysVal->IsArray()) continue;

        ForceFields::KeyTrack track {};
        track.Path = std::string(pathVal->AsString());
        if (const TomlValue* ext = tbl->Find("Extrapolation"))
        {
            const int64_t code = ext->AsInt64(0);
            if (code >= 0 && code <= 2) track.Extrapolation = static_cast<ForceFields::KeyExtrapolationOrdinal>(code);
        }
        if (const TomlValue* per = tbl->Find("Period"))
        {
            track.PeriodSeconds = per->AsDouble(0.0);
        }
        for (const auto& kEntry : keysVal->AsArray()->Entries)
        {
            const TomlTable* kTbl = kEntry->AsTable();
            if (kTbl == nullptr) continue;
            const TomlValue* tVal = kTbl->Find("Time");
            const TomlValue* aVal = kTbl->Find("Amount");
            if (tVal == nullptr || !tVal->IsNumber() || aVal == nullptr) continue;

            ForceFields::TrackKey key {};
            key.TimeSeconds = tVal->AsDouble();
            if (aVal->IsNumber()) key.Amount = aVal->AsDouble();
            else if (aVal->IsString()) key.Amount = std::string(aVal->AsString());
            else if (aVal->IsBool()) key.Amount = std::string(aVal->AsBool() ? "true" : "false");
            else if (const TomlArray* arr = aVal->AsArray(); arr != nullptr && arr->Entries.size() == 3u)
            {
                key.Amount = std::array<double, 3> { arr->Entries[0u]->AsDouble(), arr->Entries[1u]->AsDouble(), arr->Entries[2u]->AsDouble() };
            }
            if (const TomlValue* easeVal = kTbl->Find("Ease"))
            {
                const int64_t ec = easeVal->AsInt64(1);
                if (ec >= 0 && ec <= 3) key.Ease = static_cast<ForceFields::KeyEaseOrdinal>(ec);
            }
            auto readTangent = [](const TomlValue* tv) -> std::array<double, 3>
            {
                if (tv == nullptr) return { 0.0, 0.0, 0.0 };
                if (tv->IsNumber()) { const double v = tv->AsDouble(); return { v, v, v }; }
                if (const TomlArray* a = tv->AsArray(); a != nullptr && a->Entries.size() == 3u)
                {
                    return { a->Entries[0u]->AsDouble(), a->Entries[1u]->AsDouble(), a->Entries[2u]->AsDouble() };
                }
                return { 0.0, 0.0, 0.0 };
            };
            key.InTangent  = readTangent(kTbl->Find("In"));
            key.OutTangent = readTangent(kTbl->Find("Out"));
            track.Keys.push_back(std::move(key));
            if (track.Keys.size() >= ForceFields::kMaximumKeysPerTrack) break;
        }
        if (!track.Keys.empty())
        {
            std::sort(track.Keys.begin(), track.Keys.end(), [](const ForceFields::TrackKey& l, const ForceFields::TrackKey& r) noexcept
            {
                return l.TimeSeconds < r.TimeSeconds;
            });
            tracks.push_back(std::move(track));
        }
    }
    return tracks;
}

[[nodiscard]] bool IsSteppedParameterType(const ParameterTypeOrdinal type) noexcept
{
    return type == ParameterTypeOrdinal::Bool
        || type == ParameterTypeOrdinal::Choice
        || type == ParameterTypeOrdinal::Ramp8
        || type == ParameterTypeOrdinal::Token;
}

[[nodiscard]] LayerParameterValue KeyValueToLayerValue(const ParameterSpecification& parameter, const ForceFields::KeyValueVariant& sampled)
{
    if (const auto* d = std::get_if<double>(&sampled))
    {
        if (parameter.Type == ParameterTypeOrdinal::Float32)
        {
            return CoerceParameterValue(parameter, *d);
        }
        return CoerceParameterValue(parameter, static_cast<int64_t>(std::llround(*d)));
    }
    if (const auto* v = std::get_if<std::array<double, 3>>(&sampled))
    {
        return CoerceParameterValue(parameter, *v);
    }
    if (const auto* s = std::get_if<std::string>(&sampled))
    {
        if (parameter.Type == ParameterTypeOrdinal::Bool)
        {
            return (*s == "true");
        }
        return CoerceParameterValue(parameter, *s);
    }
    return parameter.DefaultVal;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY & BLOCK METHODS
//------------------------------------------------------------------------------------------------------------------------

const LayerParameterValue* EffectLayerEntry::FindParameter(const std::string_view name) const noexcept
{
    for (const ParameterAssignment& a : Parameters)
    {
        if (a.Name == name)
        {
            return &a.Value;
        }
    }
    return nullptr;
}

void EffectLayerEntry::SetParameter(const std::string_view name, LayerParameterValue value)
{
    for (ParameterAssignment& a : Parameters)
    {
        if (a.Name == name)
        {
            a.Value = std::move(value);
            return;
        }
    }
    Parameters.push_back(ParameterAssignment { std::string(name), std::move(value) });
}

const LayerParameterValue* EvaluatedParameterBlock::Find(const std::string_view name) const noexcept
{
    for (const ParameterAssignment& a : Fields)
    {
        if (a.Name == name)
        {
            return &a.Value;
        }
    }
    return nullptr;
}

void EvaluatedParameterBlock::Set(const std::string_view name, LayerParameterValue value)
{
    for (ParameterAssignment& a : Fields)
    {
        if (a.Name == name)
        {
            a.Value = std::move(value);
            return;
        }
    }
    Fields.push_back(ParameterAssignment { std::string(name), std::move(value) });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                LAYER CONSTRUCTION & READING
//------------------------------------------------------------------------------------------------------------------------

std::optional<EffectLayerEntry> ConstructEffectLayer(
    const EffectLayerSpecificationSet& specs,
    const uint32_t categoryCode,
    const std::string_view token)
{
    const LayerCategorySpecification* spec = specs.FindByCode(categoryCode);
    if (spec == nullptr)
    {
        return std::nullopt;
    }
    EffectLayerEntry entry {};
    entry.Token        = token.empty() ? ("layer-" + spec->Token) : std::string(token);
    entry.CategoryCode = categoryCode;
    entry.Enabled      = true;
    entry.Solo         = false;
    entry.Weight       = 1.0;
    entry.Blend        = LayerBlendOrdinal::Replace;
    entry.Parameters.reserve(spec->Parameters.size());
    for (const ParameterSpecification& p : spec->Parameters)
    {
        entry.Parameters.push_back(ParameterAssignment { p.Name, p.DefaultVal });
    }
    return entry;
}

EffectLayerStack ParseEffectLayerStackFromToml(
    const EffectLayerSpecificationSet& specs,
    const TomlTable& sourceTable,
    const LayerStackOrdinal defaultCategory)
{
    // Reference: Experimental/FluidEditor/Engine/LayerStack.js L327-L352
    EffectLayerStack stack {};
    stack.Category = defaultCategory;
    if (const TomlValue* catVal = sourceTable.Find("Category"))
    {
        const int64_t c = catVal->AsInt64(static_cast<int64_t>(defaultCategory));
        if (c == 1 || c == 2)
        {
            stack.Category = static_cast<LayerStackOrdinal>(c);
        }
    }
    if (const TomlValue* tokVal = sourceTable.Find("Token"); tokVal != nullptr && tokVal->IsString())
    {
        stack.Token = std::string(tokVal->AsString());
    }
    else
    {
        stack.Token = (stack.Category == LayerStackOrdinal::GasVolume) ? "gas-volume" : "particle-cloud";
    }

    const TomlValue* layerList = sourceTable.Find("Layer");
    if (layerList == nullptr) layerList = sourceTable.Find("Layers");
    if (layerList != nullptr && layerList->IsArray())
    {
        uint32_t seq = 0u;
        for (const auto& entryPtr : layerList->AsArray()->Entries)
        {
            const TomlTable* raw = entryPtr->AsTable();
            if (raw == nullptr) continue;
            ++seq;

            const uint32_t catCode = static_cast<uint32_t>(raw->Find("Category") ? raw->Find("Category")->AsInt64(0) : 0);
            const std::string rawToken = raw->Find("Token") ? std::string(raw->Find("Token")->AsString()) : ("layer-" + std::to_string(seq));
            const LayerCategorySpecification* spec = specs.FindByCode(catCode);

            EffectLayerEntry layer {};
            if (spec != nullptr)
            {
                layer = *ConstructEffectLayer(specs, catCode, rawToken);
            }
            else
            {
                layer.Token        = rawToken;
                layer.CategoryCode = catCode;
            }
            if (const TomlValue* en = raw->Find("Enabled")) layer.Enabled = en->AsBool(true);
            if (const TomlValue* so = raw->Find("Solo")) layer.Solo = so->AsBool(false);
            if (const TomlValue* wt = raw->Find("Weight")) layer.Weight = std::clamp(wt->AsDouble(1.0), 0.0, 1.0);
            if (const TomlValue* bl = raw->Find("Blend"))
            {
                const int64_t b = bl->AsInt64(0);
                if (b >= 0 && b <= 3) layer.Blend = static_cast<LayerBlendOrdinal>(b);
            }
            if (const TomlValue* paramsVal = raw->Find("Parameters"); paramsVal != nullptr && paramsVal->IsTable())
            {
                const TomlTable& pTbl = *paramsVal->AsTable();
                for (const TomlField& field : pTbl.Fields)
                {
                    if (spec != nullptr)
                    {
                        if (const ParameterSpecification* pSpec = specs.FindParameter(*spec, field.Key))
                        {
                            layer.SetParameter(field.Key, DecodeParameterValueFromToml(*pSpec, *field.Value));
                            continue;
                        }
                    }
                    // Preserve unknown parameters verbatim (LayerStack.js L340)
                    if (field.Value->IsBool()) layer.SetParameter(field.Key, field.Value->AsBool());
                    else if (field.Value->IsInt()) layer.SetParameter(field.Key, field.Value->AsInt64());
                    else if (field.Value->IsNumber()) layer.SetParameter(field.Key, field.Value->AsDouble());
                    else if (field.Value->IsString()) layer.SetParameter(field.Key, std::string(field.Value->AsString()));
                }
            }
            stack.Layers.push_back(std::move(layer));
        }
    }

    const TomlValue* trackVal = sourceTable.Find("Track");
    if (trackVal == nullptr) trackVal = sourceTable.Find("Tracks");
    if (trackVal != nullptr && trackVal->IsArray())
    {
        stack.Tracks = ParseTracksFromTomlArray(trackVal->AsArray());
    }
    return stack;
}

EffectLayerEntry* InsertLayerInPhaseOrder(
    const EffectLayerSpecificationSet& specs,
    EffectLayerStack& stack,
    const uint32_t categoryCode,
    const std::string_view token)
{
    // Reference: Experimental/FluidEditor/Engine/LayerStack.js L355-L370
    const LayerCategorySpecification* spec = specs.FindByCode(categoryCode);
    if (spec == nullptr || stack.Layers.size() >= specs.MaximumLayers)
    {
        return nullptr;
    }
    uint32_t existingCount = 0u;
    for (const EffectLayerEntry& existing : stack.Layers)
    {
        if (existing.CategoryCode == categoryCode) ++existingCount;
    }
    if (existingCount >= spec->Capacity)
    {
        return nullptr;
    }
    const uint8_t targetRank = static_cast<uint8_t>(spec->Phase);
    size_t insertPos = stack.Layers.size();
    for (size_t i = stack.Layers.size(); i > 0u; --i)
    {
        const LayerCategorySpecification* other = specs.FindByCode(stack.Layers[i - 1u].CategoryCode);
        if (other != nullptr && static_cast<uint8_t>(other->Phase) <= targetRank)
        {
            insertPos = i;
            break;
        }
        insertPos = i - 1u;
    }
    auto created = ConstructEffectLayer(specs, categoryCode, token);
    if (!created.has_value())
    {
        return nullptr;
    }
    auto it = stack.Layers.insert(stack.Layers.begin() + static_cast<std::ptrdiff_t>(insertPos), std::move(*created));
    return &(*it);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STACK EVALUATION
//------------------------------------------------------------------------------------------------------------------------

StackEvaluationOutput EvaluateEffectLayerStack(
    const EffectLayerSpecificationSet& specs,
    const EffectLayerStack& stack,
    const std::span<const std::string> activeSourceIds,
    const std::optional<double> evaluationTimeSeconds)
{
    // Reference: Experimental/FluidEditor/Engine/LayerStack.js L182-L245
    StackEvaluationOutput out {};

    // 1. Seed all target blocks with Neutral values for every owned parameter
    for (const LayerCategorySpecification& spec : specs.Layers)
    {
        if (spec.Stack != stack.Category) continue;
        for (const ParameterSpecification& param : spec.Parameters)
        {
            if (!TargetBelongsToStack(stack.Category, param.Target)) continue;
            if (EvaluatedParameterBlock* block = SelectTargetBlock(out, param.Target))
            {
                block->Set(param.Name, param.NeutralVal);
            }
        }
    }

    // 2. Validate layer count, required index-0 layer, stack membership, and per-category capacity
    if (stack.Layers.size() > specs.MaximumLayers)
    {
        out.StoppedReason = "More than " + std::to_string(specs.MaximumLayers) + " layers";
        out.Problems.push_back(StackProblemEntry { StackProblemSeverity::Stop, {}, out.StoppedReason });
    }
    for (const LayerCategorySpecification& spec : specs.Layers)
    {
        if (spec.Stack != stack.Category || !spec.Required) continue;
        int32_t foundIndex = -1;
        for (size_t i = 0u; i < stack.Layers.size(); ++i)
        {
            if (stack.Layers[i].CategoryCode == spec.Code)
            {
                foundIndex = static_cast<int32_t>(i);
                break;
            }
        }
        if (foundIndex < 0)
        {
            if (out.StoppedReason.empty()) out.StoppedReason = spec.Name + " layer missing";
            out.Problems.push_back(StackProblemEntry { StackProblemSeverity::Stop, {}, spec.Name + " layer is required" });
        }
        else if (foundIndex != 0)
        {
            if (out.StoppedReason.empty()) out.StoppedReason = spec.Name + " must be the first layer";
            out.Problems.push_back(StackProblemEntry { StackProblemSeverity::Stop, stack.Layers[static_cast<size_t>(foundIndex)].Token, spec.Name + " must be the first layer" });
        }
    }

    std::vector<std::pair<uint32_t, uint32_t>> categoryCounts;
    for (const EffectLayerEntry& layer : stack.Layers)
    {
        const LayerCategorySpecification* spec = specs.FindByCode(layer.CategoryCode);
        if (spec == nullptr)
        {
            out.Problems.push_back(StackProblemEntry {
                StackProblemSeverity::Info, layer.Token,
                "Unknown category " + std::to_string(layer.CategoryCode) + ": kept, not evaluated" });
            continue;
        }
        if (spec->Stack != stack.Category)
        {
            out.Problems.push_back(StackProblemEntry { StackProblemSeverity::Stop, layer.Token, spec->Name + " belongs to another stack" });
            if (out.StoppedReason.empty()) out.StoppedReason = spec->Name + " does not belong in this stack";
            continue;
        }
        uint32_t* countPtr = nullptr;
        for (auto& pair : categoryCounts)
        {
            if (pair.first == layer.CategoryCode) { countPtr = &pair.second; break; }
        }
        if (countPtr == nullptr)
        {
            categoryCounts.emplace_back(layer.CategoryCode, 1u);
            countPtr = &categoryCounts.back().second;
        }
        else
        {
            ++(*countPtr);
        }
        if (*countPtr > spec->Capacity)
        {
            out.Problems.push_back(StackProblemEntry { StackProblemSeverity::Stop, layer.Token, spec->Name + ": at most " + std::to_string(spec->Capacity) });
            if (out.StoppedReason.empty()) out.StoppedReason = "Too many " + spec->Name + " layers";
        }
    }

    // 3. Determine solo isolation
    bool soloing = false;
    for (const EffectLayerEntry& layer : stack.Layers)
    {
        if (layer.Solo && layer.Enabled)
        {
            soloing = true;
            break;
        }
    }

    // 4. Fold layers in order (sampling KeyTracks at evaluationTimeSeconds when provided)
    for (const EffectLayerEntry& staticLayer : stack.Layers)
    {
        const LayerCategorySpecification* spec = specs.FindByCode(staticLayer.CategoryCode);
        if (spec == nullptr || spec->Stack != stack.Category) continue;

        EffectLayerEntry evaluatedLayer = staticLayer;
        if (evaluationTimeSeconds.has_value() && std::isfinite(*evaluationTimeSeconds) && !stack.Tracks.empty())
        {
            const double t = *evaluationTimeSeconds;
            for (const ForceFields::KeyTrack& track : stack.Tracks)
            {
                const auto parts = ForceFields::SplitLayerTrackPath(track.Path);
                if (!parts.has_value() || parts->LayerToken != evaluatedLayer.Token) continue;
                if (parts->ParameterName == "Weight")
                {
                    if (!spec->Required && spec->Linked.empty())
                    {
                        evaluatedLayer.Weight = std::clamp(ForceFields::SampleScalarTrack(track, t, evaluatedLayer.Weight), 0.0, 1.0);
                    }
                    continue;
                }
                if (const ParameterSpecification* pSpec = specs.FindParameter(*spec, parts->ParameterName))
                {
                    const auto sampled = ForceFields::SampleKeyTrack(track, t, IsSteppedParameterType(pSpec->Type));
                    if (sampled.has_value())
                    {
                        evaluatedLayer.SetParameter(pSpec->Name, KeyValueToLayerValue(*pSpec, *sampled));
                    }
                }
            }
        }

        const bool live = evaluatedLayer.Enabled
            && (!soloing || evaluatedLayer.Solo || spec->Required)
            && (spec->Required || evaluatedLayer.Weight > 0.0);

        if (spec->Linked == "Emitter")
        {
            std::string linkToken;
            if (const LayerParameterValue* linkVal = evaluatedLayer.FindParameter("Emitter"))
            {
                if (const auto* s = std::get_if<std::string>(linkVal)) linkToken = *s;
            }
            bool found = false;
            for (const std::string& id : activeSourceIds)
            {
                if (id == linkToken) { found = true; break; }
            }
            if (!found)
            {
                out.Problems.push_back(StackProblemEntry { StackProblemSeverity::Warning, evaluatedLayer.Token, "Linked emitter not found" });
            }
            bool updated = false;
            for (SourceLinkState& link : out.SourceLinks)
            {
                if (link.SourceId == linkToken) { link.Enabled = live; updated = true; break; }
            }
            if (!updated)
            {
                out.SourceLinks.push_back(SourceLinkState { linkToken, live });
            }
            if (live) out.ContributingTokens.push_back(evaluatedLayer.Token);
            continue;
        }

        if (!live) continue;
        out.ContributingTokens.push_back(evaluatedLayer.Token);

        const double weight = spec->Required ? 1.0 : evaluatedLayer.Weight;
        for (const ParameterSpecification& param : spec->Parameters)
        {
            if (!TargetBelongsToStack(stack.Category, param.Target)) continue;
            EvaluatedParameterBlock* block = SelectTargetBlock(out, param.Target);
            const LayerParameterValue* current = (block != nullptr) ? block->Find(param.Name) : nullptr;
            const LayerParameterValue* amount  = evaluatedLayer.FindParameter(param.Name);
            if (block == nullptr || current == nullptr || amount == nullptr) continue;
            block->Set(param.Name, BlendParameterValue(param, *current, *amount, weight, evaluatedLayer.Blend));
        }
    }

    // 5. Convert Choice integer codes to canonical Token strings (LayerStack.js L242-L243)
    for (const LayerCategorySpecification& spec : specs.Layers)
    {
        if (spec.Stack != stack.Category) continue;
        for (const ParameterSpecification& param : spec.Parameters)
        {
            if (param.Type != ParameterTypeOrdinal::Choice || !TargetBelongsToStack(stack.Category, param.Target)) continue;
            EvaluatedParameterBlock* block = SelectTargetBlock(out, param.Target);
            if (block == nullptr) continue;
            if (const LayerParameterValue* val = block->Find(param.Name))
            {
                if (const auto* code = std::get_if<int64_t>(val))
                {
                    block->Set(param.Name, std::string(ResolveChoiceToken(param, *code)));
                }
            }
        }
    }

    return out;
}

} // namespace Frontier::SimulationSpace::EffectLayers
