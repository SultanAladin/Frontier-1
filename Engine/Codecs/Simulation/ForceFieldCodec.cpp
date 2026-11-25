/*============================================================================================================================================
                                                           FORCEFIELDCODEC.CPP
============================================================================================================================================*/
// 🧩 Stage F (S28) .ForceField two-tier codec + shared 64B-aligned binary framing and KeyTrack/Force TOML serialization
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.1

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/ForceFieldCodec.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Frontier::Codecs::Simulation
{

namespace
{

namespace FF = Frontier::SimulationSpace::ForceFields;
namespace EL = Frontier::SimulationSpace::EffectLayers;

constexpr char kBinarySentinel[] = "\n# --- FRONTIER_TIER2_BINARY_BEGIN ---\n";
constexpr char kBinaryMagic[8]   = { 'F', 'R', 'N', 'T', 'S', 'I', 'M', '2' };

void AppendU32LE(std::vector<std::byte>& dst, const uint32_t v)
{
    dst.push_back(static_cast<std::byte>(v & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >> 8u) & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >> 16u) & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >> 24u) & 0xFFu));
}

void AppendU64LE(std::vector<std::byte>& dst, const uint64_t v)
{
    for (uint32_t i = 0u; i < 8u; ++i)
    {
        dst.push_back(static_cast<std::byte>((v >> (i * 8u)) & 0xFFu));
    }
}

[[nodiscard]] bool ReadU32LE(const std::span<const std::byte> src, std::size_t& cursor, uint32_t& outVal) noexcept
{
    if (cursor + 4u > src.size()) return false;
    outVal = static_cast<uint32_t>(src[cursor])
           | (static_cast<uint32_t>(src[cursor + 1u]) << 8u)
           | (static_cast<uint32_t>(src[cursor + 2u]) << 16u)
           | (static_cast<uint32_t>(src[cursor + 3u]) << 24u);
    cursor += 4u;
    return true;
}

[[nodiscard]] bool ReadU64LE(const std::span<const std::byte> src, std::size_t& cursor, uint64_t& outVal) noexcept
{
    if (cursor + 8u > src.size()) return false;
    uint64_t v = 0ull;
    for (uint32_t i = 0u; i < 8u; ++i)
    {
        v |= static_cast<uint64_t>(src[cursor + i]) << (i * 8u);
    }
    cursor += 8u;
    outVal = v;
    return true;
}

void PadToAlignment64(std::vector<std::byte>& dst)
{
    const std::size_t rem = dst.size() % kSimulationBinaryAlignmentBytes;
    if (rem != 0u)
    {
        dst.insert(dst.end(), kSimulationBinaryAlignmentBytes - rem, std::byte{ 0 });
    }
}

[[nodiscard]] std::string_view FormatEaseToken(const FF::KeyEaseOrdinal e) noexcept
{
    switch (e)
    {
        case FF::KeyEaseOrdinal::Constant: return "Constant";
        case FF::KeyEaseOrdinal::Linear:   return "Linear";
        case FF::KeyEaseOrdinal::Smooth:   return "Smooth";
        case FF::KeyEaseOrdinal::Bezier:   return "Bezier";
    }
    return "Linear";
}

[[nodiscard]] FF::KeyEaseOrdinal ParseEaseToken(const std::string_view s) noexcept
{
    if (s == "Constant") return FF::KeyEaseOrdinal::Constant;
    if (s == "Smooth")   return FF::KeyEaseOrdinal::Smooth;
    if (s == "Bezier")   return FF::KeyEaseOrdinal::Bezier;
    return FF::KeyEaseOrdinal::Linear;
}

[[nodiscard]] std::string_view FormatExtrapToken(const FF::KeyExtrapolationOrdinal e) noexcept
{
    switch (e)
    {
        case FF::KeyExtrapolationOrdinal::Hold:     return "Hold";
        case FF::KeyExtrapolationOrdinal::Cycle:    return "Cycle";
        case FF::KeyExtrapolationOrdinal::PingPong: return "PingPong";
    }
    return "Hold";
}

[[nodiscard]] FF::KeyExtrapolationOrdinal ParseExtrapToken(const std::string_view s) noexcept
{
    if (s == "Cycle")    return FF::KeyExtrapolationOrdinal::Cycle;
    if (s == "PingPong") return FF::KeyExtrapolationOrdinal::PingPong;
    return FF::KeyExtrapolationOrdinal::Hold;
}

[[nodiscard]] std::array<float, 3> ReadVec3Float(const EL::TomlTable& tbl, const std::string_view key, const std::array<float, 3>& fallback) noexcept
{
    const EL::TomlValue* v = tbl.Find(key);
    if (v == nullptr || !v->IsArray()) return fallback;
    const auto& arr = v->AsArray()->Entries;
    if (arr.size() < 3u) return fallback;
    return {
        static_cast<float>(arr[0]->AsDouble(fallback[0])),
        static_cast<float>(arr[1]->AsDouble(fallback[1])),
        static_cast<float>(arr[2]->AsDouble(fallback[2]))
    };
}

[[nodiscard]] std::array<double, 3> ReadVec3Double(const EL::TomlTable& tbl, const std::string_view key, const std::array<double, 3>& fallback) noexcept
{
    const EL::TomlValue* v = tbl.Find(key);
    if (v == nullptr || !v->IsArray()) return fallback;
    const auto& arr = v->AsArray()->Entries;
    if (arr.size() < 3u) return fallback;
    return {
        arr[0]->AsDouble(fallback[0]),
        arr[1]->AsDouble(fallback[1]),
        arr[2]->AsDouble(fallback[2])
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              SHARED TIER-2 BINARY FRAMING
//------------------------------------------------------------------------------------------------------------------------

uint64_t ComputeSimulationFnv1a64(const std::span<const std::byte> bytes) noexcept
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (const std::byte b : bytes)
    {
        h ^= static_cast<uint64_t>(b);
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string EscapeTomlQuotedString(const std::string_view raw)
{
    std::string out;
    out.reserve(raw.size() + 2u);
    out.push_back('"');
    for (const char ch : raw)
    {
        if (ch == '\\')      out += "\\\\";
        else if (ch == '"')  out += "\\\"";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else                 out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::string FormatCanonicalFloat(const double value)
{
    const double clean = (std::fabs(value) < 1.0e-12) ? 0.0 : value;
    char buf[64] {};
    std::snprintf(buf, sizeof(buf), "%.9g", clean);
    std::string s(buf);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos)
    {
        s += ".0";
    }
    return s;
}

bool PackTwoTierSimulationBytes(
    const std::string_view tomlHeaderUtf8,
    const std::span<const SimulationBinarySection> binarySections,
    std::vector<std::byte>& outBytes,
    std::string* const outError)
{
    outBytes.clear();
    outBytes.reserve(tomlHeaderUtf8.size() + 256u);
    for (const char ch : tomlHeaderUtf8)
    {
        outBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    if (binarySections.empty())
    {
        return true;
    }

    const std::string_view sentinel(kBinarySentinel);
    for (const char ch : sentinel)
    {
        outBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    PadToAlignment64(outBytes);

    // Binary directory header (starts on 64B boundary)
    for (const char m : kBinaryMagic) outBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(m)));
    AppendU32LE(outBytes, kSimulationCodecSchemaVersion);
    AppendU32LE(outBytes, static_cast<uint32_t>(binarySections.size()));

    for (const auto& sec : binarySections)
    {
        const uint64_t csum = ComputeSimulationFnv1a64(sec.PayloadBytes);
        AppendU32LE(outBytes, static_cast<uint32_t>(sec.Identifier.size()));
        for (const char ch : sec.Identifier) outBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        AppendU32LE(outBytes, static_cast<uint32_t>(sec.SubFormatTag.size()));
        for (const char ch : sec.SubFormatTag) outBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        AppendU64LE(outBytes, static_cast<uint64_t>(sec.PayloadBytes.size()));
        AppendU64LE(outBytes, csum);
        PadToAlignment64(outBytes);
        outBytes.insert(outBytes.end(), sec.PayloadBytes.begin(), sec.PayloadBytes.end());
        PadToAlignment64(outBytes);
    }

    if (outError != nullptr) outError->clear();
    return true;
}

bool UnpackTwoTierSimulationBytes(
    const std::span<const std::byte> streamBytes,
    std::string& outTomlHeaderUtf8,
    std::vector<SimulationBinarySection>& outBinarySections,
    std::string* const outError)
{
    outTomlHeaderUtf8.clear();
    outBinarySections.clear();

    const std::string_view rawView(reinterpret_cast<const char*>(streamBytes.data()), streamBytes.size());
    const std::string_view sentinel(kBinarySentinel);
    const std::size_t pos = rawView.find(sentinel);
    if (pos == std::string_view::npos)
    {
        outTomlHeaderUtf8.assign(rawView.data(), rawView.size());
        return true;
    }

    outTomlHeaderUtf8.assign(rawView.data(), pos);
    std::size_t cursor = pos + sentinel.size();
    const std::size_t rem = cursor % kSimulationBinaryAlignmentBytes;
    if (rem != 0u) cursor += (kSimulationBinaryAlignmentBytes - rem);

    if (cursor + 16u > streamBytes.size())
    {
        if (outError) *outError = "Truncated Tier-2 binary header";
        return false;
    }
    if (std::memcmp(streamBytes.data() + cursor, kBinaryMagic, 8u) != 0)
    {
        if (outError) *outError = "Invalid Tier-2 binary magic";
        return false;
    }
    cursor += 8u;

    uint32_t version = 0u, count = 0u;
    if (!ReadU32LE(streamBytes, cursor, version) || !ReadU32LE(streamBytes, cursor, count))
    {
        if (outError) *outError = "Truncated Tier-2 binary counts";
        return false;
    }
    if (version != kSimulationCodecSchemaVersion)
    {
        if (outError) *outError = "Unsupported Tier-2 binary schema version";
        return false;
    }

    outBinarySections.reserve(count);
    for (uint32_t i = 0u; i < count; ++i)
    {
        uint32_t idLen = 0u, tagLen = 0u;
        uint64_t byteLen = 0ull, expectedCsum = 0ull;
        if (!ReadU32LE(streamBytes, cursor, idLen) || cursor + idLen > streamBytes.size())
        {
            if (outError) *outError = "Truncated section identifier";
            return false;
        }
        std::string id(reinterpret_cast<const char*>(streamBytes.data() + cursor), idLen);
        cursor += idLen;

        if (!ReadU32LE(streamBytes, cursor, tagLen) || cursor + tagLen > streamBytes.size())
        {
            if (outError) *outError = "Truncated section tag";
            return false;
        }
        std::string tag(reinterpret_cast<const char*>(streamBytes.data() + cursor), tagLen);
        cursor += tagLen;

        if (!ReadU64LE(streamBytes, cursor, byteLen) || !ReadU64LE(streamBytes, cursor, expectedCsum))
        {
            if (outError) *outError = "Truncated section length/checksum";
            return false;
        }
        const std::size_t padRem = cursor % kSimulationBinaryAlignmentBytes;
        if (padRem != 0u) cursor += (kSimulationBinaryAlignmentBytes - padRem);

        if (cursor + byteLen > streamBytes.size())
        {
            if (outError) *outError = "Truncated section payload bytes";
            return false;
        }
        const std::span<const std::byte> payload = streamBytes.subspan(cursor, static_cast<std::size_t>(byteLen));
        const uint64_t actualCsum = ComputeSimulationFnv1a64(payload);
        if (actualCsum != expectedCsum)
        {
            if (outError) *outError = "Tier-2 binary section FNV-1a checksum mismatch";
            return false;
        }

        SimulationBinarySection sec {};
        sec.Identifier    = std::move(id);
        sec.SubFormatTag  = std::move(tag);
        sec.PayloadBytes.assign(payload.begin(), payload.end());
        sec.ChecksumFnv64 = actualCsum;
        outBinarySections.push_back(std::move(sec));

        cursor += static_cast<std::size_t>(byteLen);
        const std::size_t tailRem = cursor % kSimulationBinaryAlignmentBytes;
        if (tailRem != 0u) cursor += (kSimulationBinaryAlignmentBytes - tailRem);
    }
    return true;
}

bool WriteSimulationBytesToFile(
    const std::filesystem::path& targetPath,
    const std::span<const std::byte> bytes,
    std::string* const outError)
{
    std::error_code ec;
    if (targetPath.has_parent_path())
    {
        std::filesystem::create_directories(targetPath.parent_path(), ec);
    }
    std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (outError) *outError = "Failed to open output file: " + targetPath.string();
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out.good())
    {
        if (outError) *outError = "Failed while writing file: " + targetPath.string();
        return false;
    }
    return true;
}

bool ReadSimulationBytesFromFile(
    const std::filesystem::path& sourcePath,
    std::vector<std::byte>& outBytes,
    std::string* const outError)
{
    std::ifstream in(sourcePath, std::ios::binary | std::ios::ate);
    if (!in)
    {
        if (outError) *outError = "Failed to open input file: " + sourcePath.string();
        return false;
    }
    const std::streamsize sz = in.tellg();
    if (sz < 0)
    {
        if (outError) *outError = "Failed to query file size: " + sourcePath.string();
        return false;
    }
    outBytes.resize(static_cast<std::size_t>(sz));
    in.seekg(0, std::ios::beg);
    if (sz > 0)
    {
        in.read(reinterpret_cast<char*>(outBytes.data()), sz);
        if (!in.good())
        {
            if (outError) *outError = "Failed while reading file: " + sourcePath.string();
            return false;
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       KEYTRACK & FORCE TOML SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

void AppendKeyTracksToToml(
    std::string& outToml,
    const std::span<const FF::KeyTrack> tracks)
{
    for (const auto& tr : tracks)
    {
        outToml += "\n[[Track]]\n";
        outToml += "Path = " + EscapeTomlQuotedString(tr.Path) + "\n";
        outToml += "Extrapolation = " + EscapeTomlQuotedString(FormatExtrapToken(tr.Extrapolation)) + "\n";
        outToml += "PeriodSeconds = " + FormatCanonicalFloat(tr.PeriodSeconds) + "\n";
        outToml += "FromEnvelope = " + std::string(tr.FromEnvelope ? "true" : "false") + "\n";

        for (const auto& key : tr.Keys)
        {
            outToml += "\n  [[Track.Key]]\n";
            outToml += "  Time = " + FormatCanonicalFloat(key.TimeSeconds) + "\n";
            outToml += "  Ease = " + EscapeTomlQuotedString(FormatEaseToken(key.Ease)) + "\n";
            if (const auto* d = std::get_if<double>(&key.Amount))
            {
                outToml += "  Amount = " + FormatCanonicalFloat(*d) + "\n";
            }
            else if (const auto* v3 = std::get_if<std::array<double, 3>>(&key.Amount))
            {
                outToml += "  Amount = [ " + FormatCanonicalFloat((*v3)[0]) + ", "
                         + FormatCanonicalFloat((*v3)[1]) + ", " + FormatCanonicalFloat((*v3)[2]) + " ]\n";
            }
            else if (const auto* s = std::get_if<std::string>(&key.Amount))
            {
                outToml += "  Amount = " + EscapeTomlQuotedString(*s) + "\n";
            }
            outToml += "  InTangent = [ " + FormatCanonicalFloat(key.InTangent[0]) + ", "
                     + FormatCanonicalFloat(key.InTangent[1]) + ", " + FormatCanonicalFloat(key.InTangent[2]) + " ]\n";
            outToml += "  OutTangent = [ " + FormatCanonicalFloat(key.OutTangent[0]) + ", "
                     + FormatCanonicalFloat(key.OutTangent[1]) + ", " + FormatCanonicalFloat(key.OutTangent[2]) + " ]\n";
        }
    }
}

std::vector<FF::KeyTrack> ParseKeyTracksFromToml(const EL::TomlTable& rootTable)
{
    std::vector<FF::KeyTrack> tracks;
    const EL::TomlValue* trVal = rootTable.Find("Track");
    if (trVal == nullptr || !trVal->IsArray()) return tracks;

    for (const auto& entry : trVal->AsArray()->Entries)
    {
        const EL::TomlTable* tbl = entry ? entry->AsTable() : nullptr;
        if (tbl == nullptr) continue;

        FF::KeyTrack tr {};
        if (const auto* p = tbl->Find("Path"))          tr.Path = std::string(p->AsString());
        if (const auto* e = tbl->Find("Extrapolation")) tr.Extrapolation = ParseExtrapToken(e->AsString());
        if (const auto* per = tbl->Find("PeriodSeconds")) tr.PeriodSeconds = per->AsDouble(0.0);
        if (const auto* env = tbl->Find("FromEnvelope"))  tr.FromEnvelope = env->AsBool(false);

        if (const EL::TomlValue* keysVal = tbl->Find("Key"); keysVal != nullptr && keysVal->IsArray())
        {
            for (const auto& kEntry : keysVal->AsArray()->Entries)
            {
                const EL::TomlTable* kTbl = kEntry ? kEntry->AsTable() : nullptr;
                if (kTbl == nullptr) continue;

                FF::TrackKey key {};
                if (const auto* t = kTbl->Find("Time")) key.TimeSeconds = t->AsDouble(0.0);
                if (const auto* e = kTbl->Find("Ease")) key.Ease = ParseEaseToken(e->AsString());
                if (const auto* a = kTbl->Find("Amount"))
                {
                    if (a->IsString())     key.Amount = std::string(a->AsString());
                    else if (a->IsArray()) key.Amount = ReadVec3Double(*kTbl, "Amount", { 0.0, 0.0, 0.0 });
                    else                   key.Amount = a->AsDouble(0.0);
                }
                key.InTangent  = ReadVec3Double(*kTbl, "InTangent",  { 0.0, 0.0, 0.0 });
                key.OutTangent = ReadVec3Double(*kTbl, "OutTangent", { 0.0, 0.0, 0.0 });
                tr.Keys.push_back(std::move(key));
            }
        }
        tracks.push_back(std::move(tr));
    }
    return tracks;
}

void AppendForceNodesToToml(
    std::string& outToml,
    const std::span<const FF::ForceModelNode> forces)
{
    for (const auto& f : forces)
    {
        const auto* desc = FF::FindForceModelByOrdinal(f.ModelCode);
        const std::string_view tomlId = (desc != nullptr) ? desc->TomlId : "Wind";
        outToml += "\n[[Force]]\n";
        outToml += "NodeId = " + EscapeTomlQuotedString(f.NodeId) + "\n";
        outToml += "Model = " + EscapeTomlQuotedString(tomlId) + "\n";
        outToml += "Name = " + EscapeTomlQuotedString(f.Name) + "\n";
        outToml += "Enabled = " + std::string(f.Enabled ? "true" : "false") + "\n";
        outToml += "Visible = " + std::string(f.Visible ? "true" : "false") + "\n";
        outToml += "Position = [ " + FormatCanonicalFloat(f.PositionMeters[0]) + ", "
                 + FormatCanonicalFloat(f.PositionMeters[1]) + ", " + FormatCanonicalFloat(f.PositionMeters[2]) + " ]\n";
        outToml += "Direction = [ " + FormatCanonicalFloat(f.Direction[0]) + ", "
                 + FormatCanonicalFloat(f.Direction[1]) + ", " + FormatCanonicalFloat(f.Direction[2]) + " ]\n";
        outToml += "Strength = " + FormatCanonicalFloat(f.Strength) + "\n";
        outToml += "Radius = " + FormatCanonicalFloat(f.RadiusMeters) + "\n";
        outToml += "Falloff = " + EscapeTomlQuotedString(FF::QueryFalloffTomlToken(f.Falloff)) + "\n";
        outToml += "Frequency = " + FormatCanonicalFloat(f.Frequency) + "\n";
        outToml += "Secondary = " + FormatCanonicalFloat(f.Secondary) + "\n";
        outToml += "Tertiary = " + FormatCanonicalFloat(f.Tertiary) + "\n";
        outToml += "StartTime = " + FormatCanonicalFloat(f.StartTimeSec) + "\n";
        outToml += "Duration = " + FormatCanonicalFloat(f.DurationSec) + "\n";
        outToml += "Fade = " + FormatCanonicalFloat(f.FadeSec) + "\n";
        outToml += "Repeat = " + FormatCanonicalFloat(f.RepeatSec) + "\n";
        outToml += "TargetChannels = " + std::to_string(f.TargetChannels) + "\n";
    }
}

std::vector<FF::ForceModelNode> ParseForceNodesFromToml(const EL::TomlTable& rootTable)
{
    std::vector<FF::ForceModelNode> forces;
    const EL::TomlValue* fVal = rootTable.Find("Force");
    if (fVal == nullptr || !fVal->IsArray()) return forces;

    uint32_t index = 0u;
    for (const auto& entry : fVal->AsArray()->Entries)
    {
        const EL::TomlTable* tbl = entry ? entry->AsTable() : nullptr;
        if (tbl == nullptr) continue;

        FF::ForceModelOrdinal model = FF::ForceModelOrdinal::DirectionalWind;
        if (const auto* m = tbl->Find("Model"))
        {
            if (const auto* desc = FF::FindForceModelByTomlId(m->AsString())) model = desc->Code;
        }
        else if (const auto* t = tbl->Find("Type"))
        {
            if (const auto* desc = FF::FindForceModelByTomlId(t->AsString())) model = desc->Code;
        }

        const std::string fallbackId = "force-" + std::to_string(index++);
        FF::ForceModelNode node = FF::ConstructDefaultForceNode(model, fallbackId);
        if (const auto* id = tbl->Find("NodeId"))   node.NodeId = std::string(id->AsString());
        if (const auto* nm = tbl->Find("Name"))     node.Name = std::string(nm->AsString());
        if (const auto* en = tbl->Find("Enabled"))  node.Enabled = en->AsBool(true);
        if (const auto* vi = tbl->Find("Visible"))  node.Visible = vi->AsBool(true);
        node.PositionMeters = ReadVec3Float(*tbl, "Position", node.PositionMeters);
        node.Direction      = ReadVec3Float(*tbl, "Direction", node.Direction);
        if (const auto* st = tbl->Find("Strength"))  node.Strength = static_cast<float>(st->AsDouble(node.Strength));
        if (const auto* ra = tbl->Find("Radius"))    node.RadiusMeters = static_cast<float>(ra->AsDouble(node.RadiusMeters));
        if (const auto* fo = tbl->Find("Falloff"))
        {
            if (auto parsedFo = FF::ParseFalloffTomlToken(fo->AsString())) node.Falloff = *parsedFo;
        }
        if (const auto* fr = tbl->Find("Frequency")) node.Frequency = static_cast<float>(fr->AsDouble(node.Frequency));
        if (const auto* sc = tbl->Find("Secondary")) node.Secondary = static_cast<float>(sc->AsDouble(node.Secondary));
        if (const auto* te = tbl->Find("Tertiary"))  node.Tertiary  = static_cast<float>(te->AsDouble(node.Tertiary));
        if (const auto* st = tbl->Find("StartTime")) node.StartTimeSec = static_cast<float>(st->AsDouble(node.StartTimeSec));
        if (const auto* du = tbl->Find("Duration"))  node.DurationSec  = static_cast<float>(du->AsDouble(node.DurationSec));
        if (const auto* fa = tbl->Find("Fade"))      node.FadeSec      = static_cast<float>(fa->AsDouble(node.FadeSec));
        if (const auto* rp = tbl->Find("Repeat"))    node.RepeatSec    = static_cast<float>(rp->AsDouble(node.RepeatSec));
        if (const auto* ch = tbl->Find("TargetChannels")) node.TargetChannels = static_cast<uint32_t>(ch->AsInt64(node.TargetChannels));

        forces.push_back(std::move(node));
    }
    return forces;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FORCEFIELDCODEC
//------------------------------------------------------------------------------------------------------------------------

bool ForceFieldCodec::EncodeToBytes(
    const ForceFieldDocumentModel& model,
    std::vector<std::byte>& outBytes,
    std::string* const outError)
{
    std::string toml;
    toml.reserve(2048u);
    toml += "[Document]\n";
    toml += "Format = \"frontier-force-field\"\n";
    toml += "SchemaVersion = " + std::to_string(model.SchemaVersion) + "\n";
    toml += "Title = " + EscapeTomlQuotedString(model.DocumentTitle) + "\n";

    AppendForceNodesToToml(toml, model.Forces);
    AppendKeyTracksToToml(toml, model.Tracks);

    std::vector<SimulationBinarySection> sections;
    if (!model.VectorGridBytes.empty())
    {
        SimulationBinarySection sec {};
        sec.Identifier   = "VectorGrid";
        sec.SubFormatTag = ".VectorField";
        sec.PayloadBytes = model.VectorGridBytes;
        sections.push_back(std::move(sec));
    }
    return PackTwoTierSimulationBytes(toml, sections, outBytes, outError);
}

bool ForceFieldCodec::DecodeFromBytes(
    const std::span<const std::byte> bytes,
    ForceFieldDocumentModel& outModel,
    std::string* const outError)
{
    std::string tomlText;
    std::vector<SimulationBinarySection> sections;
    if (!UnpackTwoTierSimulationBytes(bytes, tomlText, sections, outError))
    {
        return false;
    }

    const auto parsed = EL::ParseTomlSubset(tomlText, outError);
    if (!parsed.has_value()) return false;

    outModel = ForceFieldDocumentModel {};
    if (const auto* docVal = parsed->Find("Document"); docVal != nullptr && docVal->IsTable())
    {
        const auto& doc = *docVal->AsTable();
        if (const auto* fmt = doc.Find("Format"); fmt == nullptr || fmt->AsString() != "frontier-force-field")
        {
            if (outError) *outError = "Document.Format must be frontier-force-field";
            return false;
        }
        if (const auto* sv = doc.Find("SchemaVersion")) outModel.SchemaVersion = static_cast<uint32_t>(sv->AsInt64(1));
        if (const auto* tt = doc.Find("Title"))         outModel.DocumentTitle = std::string(tt->AsString());
    }
    else
    {
        if (outError) *outError = "Missing [Document] table in .ForceField";
        return false;
    }

    outModel.Forces = ParseForceNodesFromToml(*parsed);
    outModel.Tracks = ParseKeyTracksFromToml(*parsed);
    for (const auto& sec : sections)
    {
        if (sec.Identifier == "VectorGrid")
        {
            outModel.VectorGridBytes = sec.PayloadBytes;
        }
    }
    return true;
}

bool ForceFieldCodec::EncodeToFile(
    const std::filesystem::path& targetPath,
    const ForceFieldDocumentModel& model,
    std::string* const outError)
{
    std::vector<std::byte> bytes;
    if (!EncodeToBytes(model, bytes, outError)) return false;
    return WriteSimulationBytesToFile(targetPath, bytes, outError);
}

bool ForceFieldCodec::DecodeFromFile(
    const std::filesystem::path& sourcePath,
    ForceFieldDocumentModel& outModel,
    std::string* const outError)
{
    std::vector<std::byte> bytes;
    if (!ReadSimulationBytesFromFile(sourcePath, bytes, outError)) return false;
    return DecodeFromBytes(bytes, outModel, outError);
}

} // namespace Frontier::Codecs::Simulation
