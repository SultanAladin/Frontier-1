/*============================================================================================================================================
                                                       EFFECTLAYERSPECIFICATION.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 implementation of TomlSubset.js, LayerSpecificationValidation.js, and LayerSpecifications.toml decoding
// Reference: Experimental/FluidEditor/Engine/TomlSubset.js L1-L202, LayerSpecificationValidation.js L1-L109, LayerStack.js L59-L76

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"

#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Frontier::SimulationSpace::EffectLayers
{

namespace
{

[[nodiscard]] std::string_view TrimAscii(const std::string_view s) noexcept
{
    size_t start = 0u;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
    {
        ++start;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1u] == ' ' || s[end - 1u] == '\t' || s[end - 1u] == '\r' || s[end - 1u] == '\n'))
    {
        --end;
    }
    return s.substr(start, end - start);
}

[[nodiscard]] std::string_view StripTomlComment(const std::string_view line) noexcept
{
    bool quoted = false;
    for (size_t i = 0u; i < line.size(); ++i)
    {
        const char c = line[i];
        if (quoted)
        {
            if (c == '\\')
            {
                ++i;
            }
            else if (c == '"')
            {
                quoted = false;
            }
        }
        else if (c == '"')
        {
            quoted = true;
        }
        else if (c == '#')
        {
            return line.substr(0u, i);
        }
    }
    return line;
}

[[nodiscard]] bool SplitTopLevel(
    const std::string_view text,
    const char separator,
    const size_t lineNumber,
    std::vector<std::string_view>& outParts,
    std::string* outError)
{
    outParts.clear();
    int depth   = 0;
    bool quoted = false;
    size_t start = 0u;
    for (size_t i = 0u; i < text.size(); ++i)
    {
        const char c = text[i];
        if (quoted)
        {
            if (c == '\\')
            {
                ++i;
            }
            else if (c == '"')
            {
                quoted = false;
            }
            continue;
        }
        if (c == '"')
        {
            quoted = true;
        }
        else if (c == '[' || c == '{')
        {
            ++depth;
        }
        else if (c == ']' || c == '}')
        {
            --depth;
            if (depth < 0)
            {
                if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unbalanced bracket";
                return false;
            }
        }
        else if (c == separator && depth == 0)
        {
            outParts.push_back(text.substr(start, i - start));
            start = i + 1u;
        }
    }
    if (quoted || depth != 0)
    {
        if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unterminated string or bracket";
        return false;
    }
    outParts.push_back(text.substr(start));
    return true;
}

void AppendUtf8CodePoint(std::string& out, const uint32_t cp)
{
    if (cp <= 0x7Fu)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FFu)
    {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    else
    {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

[[nodiscard]] bool ParseTomlValueRecursive(std::string_view raw, size_t lineNumber, TomlValue& outValue, std::string* outError);

[[nodiscard]] bool AssignKeyValuePair(TomlTable& table, const std::string_view pairText, const size_t lineNumber, std::string* outError)
{
    std::vector<std::string_view> parts;
    if (!SplitTopLevel(pairText, '=', lineNumber, parts, outError))
    {
        return false;
    }
    if (parts.size() < 2u)
    {
        if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": expected key = value";
        return false;
    }
    const std::string_view key = TrimAscii(parts[0u]);
    if (key.empty())
    {
        if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": empty key";
        return false;
    }
    for (const char c : key)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
        {
            if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unsupported key";
            return false;
        }
    }
    const size_t eqOffset = static_cast<size_t>(parts[0u].data() - pairText.data()) + parts[0u].size() + 1u;
    const std::string_view valueSlice = pairText.substr(eqOffset);
    TomlValue parsed {};
    if (!ParseTomlValueRecursive(valueSlice, lineNumber, parsed, outError))
    {
        return false;
    }
    table.Set(key, std::move(parsed));
    return true;
}

[[nodiscard]] bool ParseTomlValueRecursive(const std::string_view raw, const size_t lineNumber, TomlValue& outValue, std::string* outError)
{
    const std::string_view text = TrimAscii(raw);
    if (text.empty())
    {
        if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": missing value";
        return false;
    }
    if (text.front() == '"')
    {
        if (text.size() < 2u || text.back() != '"')
        {
            if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unterminated string";
            return false;
        }
        std::string decoded;
        decoded.reserve(text.size() - 2u);
        for (size_t i = 1u; i + 1u < text.size(); ++i)
        {
            const char c = text[i];
            if (c != '\\')
            {
                decoded.push_back(c);
                continue;
            }
            if (i + 2u >= text.size())
            {
                if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": dangling escape";
                return false;
            }
            const char esc = text[++i];
            switch (esc)
            {
                case 'b': decoded.push_back('\b'); break;
                case 't': decoded.push_back('\t'); break;
                case 'n': decoded.push_back('\n'); break;
                case 'f': decoded.push_back('\f'); break;
                case 'r': decoded.push_back('\r'); break;
                case '"': decoded.push_back('"'); break;
                case '\\': decoded.push_back('\\'); break;
                case 'u':
                {
                    if (i + 4u >= text.size() - 1u)
                    {
                        if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": short \\u escape";
                        return false;
                    }
                    uint32_t cp = 0u;
                    for (size_t k = 0u; k < 4u; ++k)
                    {
                        const char h = text[++i];
                        cp <<= 4u;
                        if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(10 + (h - 'a'));
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(10 + (h - 'A'));
                        else
                        {
                            if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": bad \\u hex";
                            return false;
                        }
                    }
                    AppendUtf8CodePoint(decoded, cp);
                    break;
                }
                default:
                    if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unknown escape";
                    return false;
            }
        }
        outValue.Storage = std::move(decoded);
        return true;
    }
    if (text == "true")
    {
        outValue.Storage = true;
        return true;
    }
    if (text == "false")
    {
        outValue.Storage = false;
        return true;
    }
    if (text.front() == '[')
    {
        if (text.back() != ']')
        {
            if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unterminated array";
            return false;
        }
        TomlArray arr {};
        const std::string_view inner = TrimAscii(text.substr(1u, text.size() - 2u));
        if (!inner.empty())
        {
            std::vector<std::string_view> parts;
            if (!SplitTopLevel(inner, ',', lineNumber, parts, outError))
            {
                return false;
            }
            for (size_t i = 0u; i < parts.size(); ++i)
            {
                const std::string_view item = TrimAscii(parts[i]);
                if (item.empty() && i + 1u == parts.size())
                {
                    continue;
                }
                auto elem = std::make_shared<TomlValue>();
                if (!ParseTomlValueRecursive(item, lineNumber, *elem, outError))
                {
                    return false;
                }
                arr.Entries.push_back(std::move(elem));
            }
        }
        outValue.Storage = std::move(arr);
        return true;
    }
    if (text.front() == '{')
    {
        if (text.back() != '}')
        {
            if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unterminated inline table";
            return false;
        }
        TomlTable tbl {};
        const std::string_view inner = TrimAscii(text.substr(1u, text.size() - 2u));
        if (!inner.empty())
        {
            std::vector<std::string_view> pairs;
            if (!SplitTopLevel(inner, ',', lineNumber, pairs, outError))
            {
                return false;
            }
            for (const std::string_view pair : pairs)
            {
                if (!AssignKeyValuePair(tbl, pair, lineNumber, outError))
                {
                    return false;
                }
            }
        }
        outValue.Storage = std::move(tbl);
        return true;
    }

    // Numeric scalar (integer or floating-point, stripping underscores)
    std::string cleaned;
    cleaned.reserve(text.size());
    bool hasDotOrExp = false;
    bool hasDigit    = false;
    for (const char c : text)
    {
        if (c == '_') continue;
        if (c == '.' || c == 'e' || c == 'E') hasDotOrExp = true;
        if (c >= '0' && c <= '9') hasDigit = true;
        cleaned.push_back(c);
    }
    if (!hasDigit)
    {
        if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": unsupported value";
        return false;
    }
    char* endPtr = nullptr;
    if (!hasDotOrExp)
    {
        const long long iv = std::strtoll(cleaned.c_str(), &endPtr, 10);
        if (endPtr == cleaned.c_str() + cleaned.size())
        {
            outValue.Storage = static_cast<int64_t>(iv);
            return true;
        }
    }
    const double dv = std::strtod(cleaned.c_str(), &endPtr);
    if (endPtr == cleaned.c_str() + cleaned.size() && std::isfinite(dv))
    {
        outValue.Storage = dv;
        return true;
    }
    if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": bad number";
    return false;
}

void EscapeJsonString(std::string& out, const std::string_view s)
{
    out.push_back('"');
    for (const unsigned char c : s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20u)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

void AppendCanonicalJson(std::string& out, const TomlValue& value)
{
    if (const auto* b = std::get_if<bool>(&value.Storage))
    {
        out += (*b ? "true" : "false");
        return;
    }
    if (const auto* iv = std::get_if<int64_t>(&value.Storage))
    {
        out += std::to_string(*iv);
        return;
    }
    if (const auto* dv = std::get_if<double>(&value.Storage))
    {
        if (std::floor(*dv) == *dv && std::fabs(*dv) < 1.0e15)
        {
            out += std::to_string(static_cast<int64_t>(*dv));
        }
        else
        {
            char buf[64];
            const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), *dv);
            if (ec == std::errc())
            {
                out.append(buf, ptr);
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%.17g", *dv);
                out += buf;
            }
        }
        return;
    }
    if (const auto* sv = std::get_if<std::string>(&value.Storage))
    {
        EscapeJsonString(out, *sv);
        return;
    }
    if (const auto* arr = std::get_if<TomlArray>(&value.Storage))
    {
        out.push_back('[');
        for (size_t i = 0u; i < arr->Entries.size(); ++i)
        {
            if (i > 0u) out.push_back(',');
            AppendCanonicalJson(out, *arr->Entries[i]);
        }
        out.push_back(']');
        return;
    }
    if (const auto* tbl = std::get_if<TomlTable>(&value.Storage))
    {
        std::vector<const TomlField*> sorted;
        sorted.reserve(tbl->Fields.size());
        for (const TomlField& field : tbl->Fields)
        {
            sorted.push_back(&field);
        }
        std::sort(sorted.begin(), sorted.end(), [](const TomlField* a, const TomlField* b) noexcept
        {
            return a->Key < b->Key;
        });
        out.push_back('{');
        for (size_t i = 0u; i < sorted.size(); ++i)
        {
            if (i > 0u) out.push_back(',');
            EscapeJsonString(out, sorted[i]->Key);
            out.push_back(':');
            AppendCanonicalJson(out, *sorted[i]->Value);
        }
        out.push_back('}');
        return;
    }
    out += "null";
}

[[nodiscard]] std::string ToLowerAscii(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
    {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

[[nodiscard]] std::optional<ParameterTypeOrdinal> ParseParameterType(const std::string_view s) noexcept
{
    if (s == "Float32") return ParameterTypeOrdinal::Float32;
    if (s == "Int32") return ParameterTypeOrdinal::Int32;
    if (s == "UInt32") return ParameterTypeOrdinal::UInt32;
    if (s == "Bool") return ParameterTypeOrdinal::Bool;
    if (s == "Vector3") return ParameterTypeOrdinal::Vector3;
    if (s == "ColourLinear") return ParameterTypeOrdinal::ColourLinear;
    if (s == "Choice") return ParameterTypeOrdinal::Choice;
    if (s == "Ramp8") return ParameterTypeOrdinal::Ramp8;
    if (s == "Token") return ParameterTypeOrdinal::Token;
    return std::nullopt;
}

[[nodiscard]] std::optional<LayerPhaseOrdinal> ParseLayerPhase(const std::string_view s) noexcept
{
    if (s == "Inject") return LayerPhaseOrdinal::Inject;
    if (s == "Advance") return LayerPhaseOrdinal::Advance;
    if (s == "Shade") return LayerPhaseOrdinal::Shade;
    if (s == "Overlay") return LayerPhaseOrdinal::Overlay;
    return std::nullopt;
}

[[nodiscard]] std::optional<LayerStackOrdinal> ParseLayerStackKind(const std::string_view s) noexcept
{
    if (s == "GasVolume") return LayerStackOrdinal::GasVolume;
    if (s == "ParticleCloud") return LayerStackOrdinal::ParticleCloud;
    return std::nullopt;
}

[[nodiscard]] std::optional<ParameterTargetOrdinal> ParseParameterTarget(const std::string_view s) noexcept
{
    if (s == "Domain") return ParameterTargetOrdinal::Region;
    if (s == "Volume") return ParameterTargetOrdinal::Volume;
    if (s == "Link") return ParameterTargetOrdinal::Link;
    if (s == "Particles") return ParameterTargetOrdinal::Particles;
    return std::nullopt;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   TOML TABLE / VALUE API
//------------------------------------------------------------------------------------------------------------------------

const TomlValue* TomlTable::Find(const std::string_view key) const noexcept
{
    for (const TomlField& f : Fields)
    {
        if (f.Key == key)
        {
            return f.Value.get();
        }
    }
    return nullptr;
}

TomlValue* TomlTable::FindMutable(const std::string_view key) noexcept
{
    for (TomlField& f : Fields)
    {
        if (f.Key == key)
        {
            return f.Value.get();
        }
    }
    return nullptr;
}

TomlValue& TomlTable::Set(const std::string_view key, TomlValue value)
{
    if (TomlValue* existing = FindMutable(key))
    {
        *existing = std::move(value);
        return *existing;
    }
    auto ptr = std::make_shared<TomlValue>(std::move(value));
    Fields.push_back(TomlField { std::string(key), ptr });
    return *ptr;
}

double TomlValue::AsDouble(const double fallback) const noexcept
{
    if (const auto* d = std::get_if<double>(&Storage)) return *d;
    if (const auto* i = std::get_if<int64_t>(&Storage)) return static_cast<double>(*i);
    return fallback;
}

int64_t TomlValue::AsInt64(const int64_t fallback) const noexcept
{
    if (const auto* i = std::get_if<int64_t>(&Storage)) return *i;
    if (const auto* d = std::get_if<double>(&Storage)) return static_cast<int64_t>(std::llround(*d));
    return fallback;
}

bool TomlValue::AsBool(const bool fallback) const noexcept
{
    if (const auto* b = std::get_if<bool>(&Storage)) return *b;
    return fallback;
}

std::string_view TomlValue::AsString(const std::string_view fallback) const noexcept
{
    if (const auto* s = std::get_if<std::string>(&Storage)) return *s;
    return fallback;
}

std::optional<TomlTable> ParseTomlSubset(const std::string_view text, std::string* outError)
{
    // Reference: Experimental/FluidEditor/Engine/TomlSubset.js L96-L143
    TomlTable root {};
    TomlTable* current = &root;
    size_t pos         = 0u;
    size_t lineNumber  = 0u;

    while (pos <= text.size())
    {
        ++lineNumber;
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos)
        {
            nl = text.size();
        }
        std::string_view rawLine = text.substr(pos, nl - pos);
        if (!rawLine.empty() && rawLine.back() == '\r')
        {
            rawLine.remove_suffix(1u);
        }
        const std::string_view content = TrimAscii(StripTomlComment(rawLine));
        pos = nl + 1u;
        if (content.empty())
        {
            continue;
        }

        const bool isArrayHeader = content.size() >= 4u && content.substr(0u, 2u) == "[[" && content.substr(content.size() - 2u) == "]]";
        const bool isTableHeader = !isArrayHeader && content.size() >= 2u && content.front() == '[' && content.back() == ']';
        if (isArrayHeader || isTableHeader)
        {
            const std::string_view headerPath = TrimAscii(
                isArrayHeader ? content.substr(2u, content.size() - 4u) : content.substr(1u, content.size() - 2u));
            std::vector<std::string_view> names;
            size_t s = 0u;
            while (s <= headerPath.size())
            {
                size_t dot = headerPath.find('.', s);
                if (dot == std::string_view::npos) dot = headerPath.size();
                names.push_back(headerPath.substr(s, dot - s));
                s = dot + 1u;
            }
            TomlTable* cursor = &root;
            for (size_t i = 0u; i + 1u < names.size(); ++i)
            {
                TomlValue* next = cursor->FindMutable(names[i]);
                if (next == nullptr)
                {
                    next = &cursor->Set(names[i], TomlValue { TomlTable {} });
                }
                if (auto* arr = std::get_if<TomlArray>(&next->Storage))
                {
                    if (arr->Entries.empty())
                    {
                        if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": empty parent array";
                        return std::nullopt;
                    }
                    next = arr->Entries.back().get();
                }
                auto* subTable = std::get_if<TomlTable>(&next->Storage);
                if (subTable == nullptr)
                {
                    if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": path segment is not a table";
                    return std::nullopt;
                }
                cursor = subTable;
            }
            const std::string_view leaf = names.back();
            if (isArrayHeader)
            {
                TomlValue* existing = cursor->FindMutable(leaf);
                if (existing == nullptr)
                {
                    existing = &cursor->Set(leaf, TomlValue { TomlArray {} });
                }
                auto* arr = std::get_if<TomlArray>(&existing->Storage);
                if (arr == nullptr)
                {
                    if (outError) *outError = "TOML line " + std::to_string(lineNumber) + ": leaf is not an array of tables";
                    return std::nullopt;
                }
                auto elem = std::make_shared<TomlValue>(TomlValue { TomlTable {} });
                current = std::get_if<TomlTable>(&elem->Storage);
                arr->Entries.push_back(std::move(elem));
            }
            else
            {
                TomlValue& created = cursor->Set(leaf, TomlValue { TomlTable {} });
                current = std::get_if<TomlTable>(&created.Storage);
            }
            continue;
        }

        if (!AssignKeyValuePair(*current, content, lineNumber, outError))
        {
            return std::nullopt;
        }
    }
    return root;
}

std::string FormatCanonicalJson(const TomlValue& value)
{
    std::string out;
    AppendCanonicalJson(out, value);
    return out;
}

std::string ComputeFnv1a64Hex(const std::string_view utf8Text)
{
    // Reference: Experimental/FluidEditor/Engine/LayerSpecificationValidation.js L101-L108
    uint64_t hash = 0xcbf29ce484222325ULL;
    constexpr uint64_t kPrime = 0x100000001b3ULL;
    for (const unsigned char b : utf8Text)
    {
        hash ^= static_cast<uint64_t>(b);
        hash *= kPrime;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buffer);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SPECIFICATION LOOKUP & LOADING
//------------------------------------------------------------------------------------------------------------------------

const LayerCategorySpecification* EffectLayerSpecificationSet::FindByCode(const uint32_t categoryCode) const noexcept
{
    for (const LayerCategorySpecification& layer : Layers)
    {
        if (layer.Code == categoryCode)
        {
            return &layer;
        }
    }
    return nullptr;
}

const LayerCategorySpecification* EffectLayerSpecificationSet::FindByToken(const std::string_view token) const noexcept
{
    for (const LayerCategorySpecification& layer : Layers)
    {
        if (layer.Token == token)
        {
            return &layer;
        }
    }
    return nullptr;
}

const ParameterSpecification* EffectLayerSpecificationSet::FindParameter(
    const LayerCategorySpecification& layer,
    const std::string_view name) const noexcept
{
    for (const ParameterSpecification& p : layer.Parameters)
    {
        if (p.Name == name)
        {
            return &p;
        }
    }
    return nullptr;
}

std::string_view ResolveChoiceToken(const ParameterSpecification& parameter, const int64_t code) noexcept
{
    for (const ChoiceOptionDescriptor& option : parameter.Options)
    {
        if (option.Code == code)
        {
            return option.Token;
        }
    }
    return parameter.Options.empty() ? std::string_view {} : std::string_view(parameter.Options.front().Token);
}

int64_t ResolveChoiceCode(const ParameterSpecification& parameter, const std::string_view token) noexcept
{
    for (const ChoiceOptionDescriptor& option : parameter.Options)
    {
        if (option.Token == token)
        {
            return option.Code;
        }
    }
    if (const auto* defInt = std::get_if<int64_t>(&parameter.DefaultVal))
    {
        return *defInt;
    }
    return 0;
}

LayerParameterValue DecodeParameterValueFromToml(const ParameterSpecification& parameter, const TomlValue& rawValue)
{
    switch (parameter.Type)
    {
        case ParameterTypeOrdinal::Float32:
            if (rawValue.IsNumber())
            {
                return std::clamp(rawValue.AsDouble(), parameter.Minimum, parameter.Maximum);
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Int32:
        case ParameterTypeOrdinal::UInt32:
            if (rawValue.IsNumber())
            {
                const double clamped = std::clamp(rawValue.AsDouble(), parameter.Minimum, parameter.Maximum);
                return static_cast<int64_t>(std::llround(clamped));
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Bool:
            if (rawValue.IsBool())
            {
                return rawValue.AsBool();
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::ColourLinear:
            if (rawValue.IsString() && ForceFields::IsHexColourString(rawValue.AsString()))
            {
                return ToLowerAscii(rawValue.AsString());
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Choice:
            if (rawValue.IsNumber())
            {
                const int64_t code = rawValue.AsInt64();
                for (const ChoiceOptionDescriptor& opt : parameter.Options)
                {
                    if (opt.Code == code)
                    {
                        return code;
                    }
                }
                return parameter.DefaultVal;
            }
            if (rawValue.IsString())
            {
                return ResolveChoiceCode(parameter, rawValue.AsString());
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Token:
            if (rawValue.IsString())
            {
                return std::string(rawValue.AsString());
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Vector3:
            if (const TomlArray* arr = rawValue.AsArray(); arr != nullptr && arr->Entries.size() == 3u)
            {
                return std::array<double, 3> {
                    arr->Entries[0u]->AsDouble(),
                    arr->Entries[1u]->AsDouble(),
                    arr->Entries[2u]->AsDouble()
                };
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Ramp8:
            if (const TomlArray* arr = rawValue.AsArray())
            {
                std::vector<std::string> stops;
                const size_t cap = std::min<size_t>(arr->Entries.size(), 8u);
                stops.reserve(cap);
                for (size_t i = 0u; i < cap; ++i)
                {
                    stops.emplace_back(arr->Entries[i]->AsString());
                }
                return stops;
            }
            return parameter.DefaultVal;
    }
    return parameter.DefaultVal;
}

LayerParameterValue CoerceParameterValue(const ParameterSpecification& parameter, const LayerParameterValue& value)
{
    switch (parameter.Type)
    {
        case ParameterTypeOrdinal::Float32:
            if (const auto* d = std::get_if<double>(&value); d != nullptr && std::isfinite(*d))
            {
                return std::clamp(*d, parameter.Minimum, parameter.Maximum);
            }
            if (const auto* i = std::get_if<int64_t>(&value))
            {
                return std::clamp(static_cast<double>(*i), parameter.Minimum, parameter.Maximum);
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Int32:
        case ParameterTypeOrdinal::UInt32:
            if (const auto* i = std::get_if<int64_t>(&value))
            {
                return static_cast<int64_t>(std::llround(std::clamp(static_cast<double>(*i), parameter.Minimum, parameter.Maximum)));
            }
            if (const auto* d = std::get_if<double>(&value); d != nullptr && std::isfinite(*d))
            {
                return static_cast<int64_t>(std::llround(std::clamp(*d, parameter.Minimum, parameter.Maximum)));
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Bool:
            return std::holds_alternative<bool>(value) ? value : parameter.DefaultVal;
        case ParameterTypeOrdinal::ColourLinear:
            if (const auto* s = std::get_if<std::string>(&value); s != nullptr && ForceFields::IsHexColourString(*s))
            {
                return ToLowerAscii(*s);
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Choice:
            if (const auto* i = std::get_if<int64_t>(&value))
            {
                for (const ChoiceOptionDescriptor& opt : parameter.Options)
                {
                    if (opt.Code == *i)
                    {
                        return *i;
                    }
                }
                return parameter.DefaultVal;
            }
            if (const auto* s = std::get_if<std::string>(&value))
            {
                return ResolveChoiceCode(parameter, *s);
            }
            return parameter.DefaultVal;
        case ParameterTypeOrdinal::Token:
            return std::holds_alternative<std::string>(value) ? value : parameter.DefaultVal;
        case ParameterTypeOrdinal::Vector3:
            return std::holds_alternative<std::array<double, 3>>(value) ? value : parameter.DefaultVal;
        case ParameterTypeOrdinal::Ramp8:
            return std::holds_alternative<std::vector<std::string>>(value) ? value : parameter.DefaultVal;
    }
    return parameter.DefaultVal;
}

std::optional<EffectLayerSpecificationSet> LoadLayerSpecificationsFromToml(const std::string_view tomlText, std::string* outError)
{
    auto parsed = ParseTomlSubset(tomlText, outError);
    if (!parsed.has_value())
    {
        return std::nullopt;
    }

    EffectLayerSpecificationSet specSet {};
    specSet.SpecificationHash = ComputeFnv1a64Hex(FormatCanonicalJson(TomlValue { *parsed }));

    // Compute CoreSpecificationHash strictly over canonical 1:1 HTML layers (Code < 220) without mutating parsed->Find("Layer")
    {
        TomlTable coreTable {};
        for (const auto& field : parsed->Fields)
        {
            if (field.Key == "Layer" && field.Value != nullptr && field.Value->IsArray())
            {
                TomlArray filtered {};
                for (const auto& entry : field.Value->AsArray()->Entries)
                {
                    const TomlTable* tbl = entry ? entry->AsTable() : nullptr;
                    const int64_t code = (tbl != nullptr && tbl->Find("Code") != nullptr) ? tbl->Find("Code")->AsInt64(0) : 0;
                    if (code < static_cast<int64_t>(kParticleXpbdLayerCode))
                    {
                        filtered.Entries.push_back(entry);
                    }
                }
                coreTable.Set("Layer", TomlValue { std::move(filtered) });
            }
            else
            {
                coreTable.Fields.push_back(field);
            }
        }
        specSet.CoreSpecificationHash = ComputeFnv1a64Hex(FormatCanonicalJson(TomlValue { coreTable }));
    }

    if (const TomlValue* docVal = parsed->Find("Document"); docVal != nullptr && docVal->IsTable())
    {
        const TomlTable& doc = *docVal->AsTable();
        if (const TomlValue* fmt = doc.Find("Format"); fmt == nullptr || fmt->AsString() != "frontier-layer-specifications")
        {
            if (outError) *outError = "Document.Format must be frontier-layer-specifications";
            return std::nullopt;
        }
        if (const TomlValue* rev = doc.Find("Revision")) specSet.Revision = static_cast<uint32_t>(rev->AsInt64(1));
        if (const TomlValue* maxL = doc.Find("MaximumLayers")) specSet.MaximumLayers = static_cast<uint32_t>(maxL->AsInt64(32));
    }

    const TomlValue* layersVal = parsed->Find("Layer");
    if (layersVal == nullptr || !layersVal->IsArray())
    {
        if (outError) *outError = "No [[Layer]] entries found";
        return std::nullopt;
    }

    for (const auto& layerEntry : layersVal->AsArray()->Entries)
    {
        const TomlTable* tbl = layerEntry->AsTable();
        if (tbl == nullptr) continue;

        LayerCategorySpecification layer {};
        layer.Code     = static_cast<uint32_t>(tbl->Find("Code") ? tbl->Find("Code")->AsInt64(0) : 0);
        layer.Token    = std::string(tbl->Find("Token") ? tbl->Find("Token")->AsString() : "");
        layer.Name     = std::string(tbl->Find("Name") ? tbl->Find("Name")->AsString() : "");
        layer.Required = tbl->Find("Required") ? tbl->Find("Required")->AsBool(false) : false;
        layer.Capacity = static_cast<uint32_t>(tbl->Find("Capacity") ? tbl->Find("Capacity")->AsInt64(1) : 1);
        if (const TomlValue* lnk = tbl->Find("Linked")) layer.Linked = std::string(lnk->AsString());

        if (const TomlValue* st = tbl->Find("Stack"))
        {
            if (auto kind = ParseLayerStackKind(st->AsString())) layer.Stack = *kind;
        }
        if (const TomlValue* ph = tbl->Find("Phase"))
        {
            if (auto phase = ParseLayerPhase(ph->AsString())) layer.Phase = *phase;
        }

        if (const TomlValue* paramsVal = tbl->Find("Parameter"); paramsVal != nullptr && paramsVal->IsArray())
        {
            for (const auto& pEntry : paramsVal->AsArray()->Entries)
            {
                const TomlTable* pTbl = pEntry->AsTable();
                if (pTbl == nullptr) continue;

                ParameterSpecification param {};
                param.Code    = static_cast<uint32_t>(pTbl->Find("Code") ? pTbl->Find("Code")->AsInt64(0) : 0);
                param.Name    = std::string(pTbl->Find("Name") ? pTbl->Find("Name")->AsString() : "");
                param.Label   = std::string(pTbl->Find("Label") ? pTbl->Find("Label")->AsString() : "");
                param.Keyable = pTbl->Find("Keyable") ? pTbl->Find("Keyable")->AsBool(false) : false;
                if (const TomlValue* mn = pTbl->Find("Minimum")) param.Minimum = mn->AsDouble(0.0);
                if (const TomlValue* mx = pTbl->Find("Maximum")) param.Maximum = mx->AsDouble(1.0);
                if (const TomlValue* st = pTbl->Find("Step")) param.Step = st->AsDouble(0.0);

                if (const TomlValue* ty = pTbl->Find("Type"))
                {
                    if (auto parsedType = ParseParameterType(ty->AsString())) param.Type = *parsedType;
                }
                if (const TomlValue* tg = pTbl->Find("Target"))
                {
                    if (auto parsedTarget = ParseParameterTarget(tg->AsString())) param.Target = *parsedTarget;
                }
                if (const TomlValue* optsVal = pTbl->Find("Options"); optsVal != nullptr && optsVal->IsArray())
                {
                    for (const auto& optEntry : optsVal->AsArray()->Entries)
                    {
                        if (const TomlTable* optTbl = optEntry->AsTable())
                        {
                            ChoiceOptionDescriptor opt {};
                            opt.Code  = optTbl->Find("Code") ? optTbl->Find("Code")->AsInt64(0) : 0;
                            opt.Token = std::string(optTbl->Find("Token") ? optTbl->Find("Token")->AsString() : "");
                            opt.Label = std::string(optTbl->Find("Label") ? optTbl->Find("Label")->AsString() : "");
                            param.Options.push_back(std::move(opt));
                        }
                    }
                }
                if (const TomlValue* defVal = pTbl->Find("Default"))
                {
                    param.DefaultVal = DecodeParameterValueFromToml(param, *defVal);
                }
                if (const TomlValue* neuVal = pTbl->Find("Neutral"))
                {
                    param.NeutralVal = DecodeParameterValueFromToml(param, *neuVal);
                }
                layer.Parameters.push_back(std::move(param));
            }
        }
        specSet.Layers.push_back(std::move(layer));
    }

    return specSet;
}

std::optional<EffectLayerSpecificationSet> LoadLayerSpecificationsFromFile(const std::string_view filePath, std::string* outError)
{
    std::ifstream in { std::string(filePath), std::ios::binary };
    if (!in.is_open())
    {
        if (outError) *outError = "Could not open file: " + std::string(filePath);
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return LoadLayerSpecificationsFromToml(ss.str(), outError);
}

} // namespace Frontier::SimulationSpace::EffectLayers
