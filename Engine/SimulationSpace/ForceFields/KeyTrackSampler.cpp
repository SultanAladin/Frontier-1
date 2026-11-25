/*============================================================================================================================================
                                                          KEYTRACKSAMPLER.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 implementation of Experimental/FluidEditor/Engine/KeyTracks.js L1-L360
// Reference: Experimental/FluidEditor/Engine/KeyTracks.js L1-L360

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

[[nodiscard]] inline double SrgbChannelToLinear(const double channel) noexcept
{
    return (channel <= 0.04045) ? (channel / 12.92) : std::pow((channel + 0.055) / 1.055, 2.4);
}

[[nodiscard]] inline double LinearChannelToSrgb(const double channel) noexcept
{
    return (channel <= 0.0031308) ? (channel * 12.92) : (1.055 * std::pow(channel, 1.0 / 2.4) - 0.055);
}

[[nodiscard]] inline int HexNibble(const char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

[[nodiscard]] inline double ExtractScalarChannel(const KeyValueVariant& value, const size_t axis) noexcept
{
    if (const auto* s = std::get_if<double>(&value))
    {
        return *s;
    }
    if (const auto* v = std::get_if<std::array<double, 3>>(&value))
    {
        return (*v)[axis];
    }
    return 0.0;
}

[[nodiscard]] inline double ExtractTangentChannel(const std::array<double, 3>& tangent, const size_t axis) noexcept
{
    return tangent[axis];
}

// Reference: Experimental/FluidEditor/Engine/KeyTracks.js L65-L76
// Monotone Fritsch-Carlson tangent at key `index` for one scalar channel: zero overshoot between keys.
template <typename ChannelGetter>
[[nodiscard]] double ComputeAutoTangent(std::span<const TrackKey> keys, const size_t index, ChannelGetter&& getValue) noexcept
{
    if (index == 0u || index + 1u >= keys.size())
    {
        return 0.0;
    }
    const TrackKey& previous = keys[index - 1u];
    const TrackKey& current  = keys[index];
    const TrackKey& next     = keys[index + 1u];

    const double leftSpan  = current.TimeSeconds - previous.TimeSeconds;
    const double rightSpan = next.TimeSeconds - current.TimeSeconds;
    const double left      = (getValue(current) - getValue(previous)) / std::max(1.0e-9, leftSpan);
    const double right     = (getValue(next) - getValue(current)) / std::max(1.0e-9, rightSpan);
    if (left * right <= 0.0)
    {
        return 0.0;
    }
    const double leftWeight  = 2.0 * rightSpan + leftSpan;
    const double rightWeight = rightSpan + 2.0 * leftSpan;
    return (leftWeight + rightWeight) / (leftWeight / left + rightWeight / right);
}

// Reference: Experimental/FluidEditor/Engine/KeyTracks.js L78-L82
[[nodiscard]] inline double EvaluateHermite(const double p0, const double p1, const double m0, const double m1, const double span, const double u) noexcept
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    return (2.0 * u3 - 3.0 * u2 + 1.0) * p0
         + (u3 - 2.0 * u2 + u) * span * m0
         + (-2.0 * u3 + 3.0 * u2) * p1
         + (u3 - u2) * span * m1;
}

// Reference: Experimental/FluidEditor/Engine/KeyTracks.js L85-L96
template <typename ChannelGetter>
[[nodiscard]] double EaseSegmentChannel(
    std::span<const TrackKey> keys,
    const size_t index,
    const double u,
    const size_t tangentAxis,
    ChannelGetter&& getValue) noexcept
{
    const TrackKey& a = keys[index];
    const TrackKey& b = keys[index + 1u];
    const double p0   = getValue(a);
    const double p1   = getValue(b);
    const double span = b.TimeSeconds - a.TimeSeconds;

    switch (a.Ease)
    {
        case KeyEaseOrdinal::Constant:
            return p0;
        case KeyEaseOrdinal::Smooth:
            return EvaluateHermite(
                p0,
                p1,
                ComputeAutoTangent(keys, index, getValue),
                ComputeAutoTangent(keys, index + 1u, getValue),
                span,
                u);
        case KeyEaseOrdinal::Bezier:
            return EvaluateHermite(
                p0,
                p1,
                ExtractTangentChannel(a.OutTangent, tangentAxis),
                ExtractTangentChannel(b.InTangent, tangentAxis),
                span,
                u);
        case KeyEaseOrdinal::Linear:
        default:
            return p0 + (p1 - p0) * u;
    }
}

// Reference: Experimental/FluidEditor/Engine/KeyTracks.js L232-L239
[[nodiscard]] inline double EvaluateLegacyEnvelopeAt(const double localTime, const double duration, const double fade) noexcept
{
    if (localTime < 0.0)
    {
        return 0.0;
    }
    const double rise = std::min(1.0, localTime / fade);
    if (duration <= 0.0)
    {
        return rise;
    }
    if (localTime >= duration)
    {
        return 0.0;
    }
    return std::min(rise, std::min(1.0, (duration - localTime) / fade));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOUR & CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

bool IsHexColourString(const std::string_view text) noexcept
{
    if (text.size() != 7u || text[0] != '#')
    {
        return false;
    }
    for (size_t i = 1u; i < 7u; ++i)
    {
        if (HexNibble(text[i]) < 0)
        {
            return false;
        }
    }
    return true;
}

KeyValueCategory ClassifyKeyValue(const KeyValueVariant& amount) noexcept
{
    if (std::holds_alternative<double>(amount))
    {
        return KeyValueCategory::Scalar;
    }
    if (std::holds_alternative<std::array<double, 3>>(amount))
    {
        return KeyValueCategory::Vector3;
    }
    if (const auto* s = std::get_if<std::string>(&amount))
    {
        if (IsHexColourString(*s))
        {
            return KeyValueCategory::ColourHex;
        }
    }
    return KeyValueCategory::StepToken;
}

std::array<double, 3> HexColourToLinearRgb(const std::string_view hex) noexcept
{
    if (!IsHexColourString(hex))
    {
        return { 0.0, 0.0, 0.0 };
    }
    const int r = (HexNibble(hex[1]) << 4) | HexNibble(hex[2]);
    const int g = (HexNibble(hex[3]) << 4) | HexNibble(hex[4]);
    const int b = (HexNibble(hex[5]) << 4) | HexNibble(hex[6]);
    return {
        SrgbChannelToLinear(static_cast<double>(r) / 255.0),
        SrgbChannelToLinear(static_cast<double>(g) / 255.0),
        SrgbChannelToLinear(static_cast<double>(b) / 255.0)
    };
}

std::string LinearRgbToHexColour(const std::array<double, 3>& linearRgb)
{
    auto quantize = [](const double lin) noexcept -> uint32_t
    {
        const double clampedLin  = std::clamp(lin, 0.0, 1.0);
        const double clampedSrgb = std::clamp(LinearChannelToSrgb(clampedLin), 0.0, 1.0);
        return static_cast<uint32_t>(std::floor(clampedSrgb * 255.0 + 0.5));
    };
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", quantize(linearRgb[0]), quantize(linearRgb[1]), quantize(linearRgb[2]));
    return std::string(buffer);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLING & ENVELOPES
//------------------------------------------------------------------------------------------------------------------------

double RoundKeyTimeSeconds(const double timeSeconds) noexcept
{
    return std::floor(timeSeconds * 1.0e6 + 0.5) / 1.0e6;
}

double ComputeTrackLocalTime(const KeyTrack& track, const double timeSeconds) noexcept
{
    // Reference: Experimental/FluidEditor/Engine/KeyTracks.js L103-L114
    if (track.Keys.empty())
    {
        return 0.0;
    }
    const double first = track.Keys.front().TimeSeconds;
    const double last  = track.Keys.back().TimeSeconds;
    if (timeSeconds <= first)
    {
        return first;
    }
    const double period = (track.PeriodSeconds > 0.0) ? track.PeriodSeconds : (last - first);
    if (track.Extrapolation == KeyExtrapolationOrdinal::Hold || !(period > 0.0))
    {
        return std::min(timeSeconds, last);
    }
    const double offset = timeSeconds - first;
    if (track.Extrapolation == KeyExtrapolationOrdinal::Cycle)
    {
        return first + std::fmod(offset, period);
    }
    const double cycle = std::fmod(offset, 2.0 * period);
    return first + ((cycle <= period) ? cycle : (2.0 * period - cycle));
}

std::optional<KeyValueVariant> SampleKeyTrack(const KeyTrack& track, const double timeSeconds, const bool stepped)
{
    // Reference: Experimental/FluidEditor/Engine/KeyTracks.js L118-L139
    const auto& keys = track.Keys;
    if (keys.empty())
    {
        return std::nullopt;
    }
    if (keys.size() == 1u)
    {
        return keys[0u].Amount;
    }
    const double localTime = ComputeTrackLocalTime(track, timeSeconds);
    size_t low  = 0u;
    size_t high = keys.size() - 1u;
    if (localTime >= keys[high].TimeSeconds)
    {
        return keys[high].Amount;
    }
    while (high - low > 1u)
    {
        const size_t middle = (low + high) >> 1u;
        if (keys[middle].TimeSeconds <= localTime)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }

    const TrackKey& a = keys[low];
    const TrackKey& b = keys[low + 1u];
    const double u    = (localTime - a.TimeSeconds) / std::max(1.0e-12, b.TimeSeconds - a.TimeSeconds);

    const KeyValueCategory categoryA = ClassifyKeyValue(a.Amount);
    const KeyValueCategory categoryB = ClassifyKeyValue(b.Amount);
    if (stepped || categoryA == KeyValueCategory::StepToken || categoryA != categoryB)
    {
        return a.Amount;
    }
    if (categoryA == KeyValueCategory::Scalar)
    {
        return EaseSegmentChannel(keys, low, u, 0u, [](const TrackKey& k) noexcept { return ExtractScalarChannel(k.Amount, 0u); });
    }
    if (categoryA == KeyValueCategory::Vector3)
    {
        std::array<double, 3> out {};
        for (size_t ax = 0u; ax < 3u; ++ax)
        {
            out[ax] = EaseSegmentChannel(keys, low, u, ax, [ax](const TrackKey& k) noexcept { return ExtractScalarChannel(k.Amount, ax); });
        }
        return out;
    }

    // ColourHex: ease in linear light (KeyTracks.js L137-L138)
    std::vector<TrackKey> linearKeys = keys;
    for (TrackKey& k : linearKeys)
    {
        if (const auto* hex = std::get_if<std::string>(&k.Amount))
        {
            k.Amount = HexColourToLinearRgb(*hex);
        }
    }
    std::array<double, 3> linearRgb {};
    for (size_t ax = 0u; ax < 3u; ++ax)
    {
        linearRgb[ax] = EaseSegmentChannel(linearKeys, low, u, ax, [ax](const TrackKey& k) noexcept { return ExtractScalarChannel(k.Amount, ax); });
    }
    return LinearRgbToHexColour(linearRgb);
}

double SampleScalarTrack(const KeyTrack& track, const double timeSeconds, const double fallbackValue) noexcept
{
    const auto sampled = SampleKeyTrack(track, timeSeconds, false);
    if (!sampled.has_value())
    {
        return fallbackValue;
    }
    if (const auto* s = std::get_if<double>(&(*sampled)))
    {
        return *s;
    }
    return fallbackValue;
}

std::optional<KeyTrack> ConstructEnvelopeWeightTrack(const TimingEnvelopeParameters& envelope)
{
    // Reference: Experimental/FluidEditor/Engine/KeyTracks.js L247-L270
    const double start    = envelope.StartTimeSeconds;
    const double duration = envelope.DurationSeconds;
    const double fade     = std::max(0.001, envelope.FadeSeconds);
    const double repeat   = envelope.RepeatSeconds;
    if (start <= 0.0 && duration <= 0.0)
    {
        return std::nullopt;
    }

    const double end = (repeat > 0.0) ? repeat : std::numeric_limits<double>::infinity();
    std::vector<double> breakpoints;
    breakpoints.reserve(6u);
    auto pushIfInside = [&](const double local)
    {
        if (std::isfinite(local) && local >= 0.0 && local < end)
        {
            breakpoints.push_back(local);
        }
    };
    pushIfInside(0.0);
    pushIfInside(fade);
    if (duration > 0.0)
    {
        pushIfInside(duration - fade);
        pushIfInside(duration * 0.5);
        pushIfInside(duration);
    }
    if (std::isfinite(end))
    {
        breakpoints.push_back(end);
    }
    std::sort(breakpoints.begin(), breakpoints.end());
    breakpoints.erase(std::unique(breakpoints.begin(), breakpoints.end()), breakpoints.end());

    std::vector<TrackKey> allKeys;
    allKeys.reserve(breakpoints.size());
    for (const double local : breakpoints)
    {
        TrackKey key {};
        key.TimeSeconds = start + local;
        key.Amount      = EvaluateLegacyEnvelopeAt(local, duration, fade);
        key.Ease        = KeyEaseOrdinal::Linear;
        allKeys.push_back(key);
    }

    // Drop keys that sit on the straight line between their neighbours (KeyTracks.js L262-L268)
    KeyTrack track {};
    track.Path          = "Weight";
    track.Extrapolation = (repeat > 0.0) ? KeyExtrapolationOrdinal::Cycle : KeyExtrapolationOrdinal::Hold;
    track.PeriodSeconds = (repeat > 0.0) ? repeat : 0.0;
    track.FromEnvelope  = true;
    for (size_t i = 0u; i < allKeys.size(); ++i)
    {
        if (i == 0u || i + 1u == allKeys.size())
        {
            track.Keys.push_back(allKeys[i]);
            continue;
        }
        const TrackKey& prev = allKeys[i - 1u];
        const TrackKey& curr = allKeys[i];
        const TrackKey& next = allKeys[i + 1u];
        const double p0 = std::get<double>(prev.Amount);
        const double p1 = std::get<double>(next.Amount);
        const double pc = std::get<double>(curr.Amount);
        const double line = p0 + (p1 - p0) * (curr.TimeSeconds - prev.TimeSeconds) / (next.TimeSeconds - prev.TimeSeconds);
        if (std::fabs(line - pc) > 1.0e-12)
        {
            track.Keys.push_back(curr);
        }
    }
    return track;
}

double EvaluateNodeTimingWeight(
    const std::span<const KeyTrack> nodeTracks,
    const TimingEnvelopeParameters& envelope,
    const double timeSeconds) noexcept
{
    if (const KeyTrack* explicitWeight = FindKeyTrack(nodeTracks, "Weight"))
    {
        return std::clamp(SampleScalarTrack(*explicitWeight, timeSeconds, 1.0), 0.0, 1.0);
    }
    const auto envelopeTrack = ConstructEnvelopeWeightTrack(envelope);
    if (!envelopeTrack.has_value())
    {
        return 1.0;
    }
    return std::clamp(SampleScalarTrack(*envelopeTrack, timeSeconds, 1.0), 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                TRACK EDITING & INVALIDATION
//------------------------------------------------------------------------------------------------------------------------

const KeyTrack* FindKeyTrack(const std::span<const KeyTrack> tracks, const std::string_view path) noexcept
{
    for (const KeyTrack& track : tracks)
    {
        if (track.Path == path)
        {
            return &track;
        }
    }
    return nullptr;
}

KeyTrack* FindKeyTrackMutable(std::vector<KeyTrack>& tracks, const std::string_view path) noexcept
{
    for (KeyTrack& track : tracks)
    {
        if (track.Path == path)
        {
            return &track;
        }
    }
    return nullptr;
}

int32_t FindKeyIndexAt(const KeyTrack& track, const double timeSeconds) noexcept
{
    for (size_t i = 0u; i < track.Keys.size(); ++i)
    {
        if (std::fabs(track.Keys[i].TimeSeconds - timeSeconds) <= kKeyTimeEpsilonSeconds)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

TrackKey* SetTrackKey(
    std::vector<KeyTrack>& tracks,
    const std::string_view path,
    const double timeSeconds,
    const KeyValueVariant& amount,
    const KeyEaseOrdinal ease,
    const bool steppedType)
{
    KeyTrack* track = FindKeyTrackMutable(tracks, path);
    if (track == nullptr)
    {
        KeyTrack created {};
        created.Path          = std::string(path);
        created.Extrapolation = KeyExtrapolationOrdinal::Hold;
        tracks.push_back(std::move(created));
        track = &tracks.back();
    }
    const bool stepped = steppedType || ClassifyKeyValue(amount) == KeyValueCategory::StepToken;
    const int32_t existingIndex = FindKeyIndexAt(*track, timeSeconds);
    if (existingIndex >= 0)
    {
        TrackKey& existing = track->Keys[static_cast<size_t>(existingIndex)];
        existing.Amount = amount;
        if (stepped)
        {
            existing.Ease = KeyEaseOrdinal::Constant;
        }
        return &existing;
    }
    if (track->Keys.size() >= kMaximumKeysPerTrack)
    {
        return nullptr;
    }
    TrackKey inserted {};
    inserted.TimeSeconds = RoundKeyTimeSeconds(timeSeconds);
    inserted.Amount      = amount;
    inserted.Ease        = stepped ? KeyEaseOrdinal::Constant : ease;
    track->Keys.push_back(std::move(inserted));
    std::sort(track->Keys.begin(), track->Keys.end(), [](const TrackKey& l, const TrackKey& r) noexcept
    {
        return l.TimeSeconds < r.TimeSeconds;
    });
    const int32_t finalIndex = FindKeyIndexAt(*track, RoundKeyTimeSeconds(timeSeconds));
    return (finalIndex >= 0) ? &track->Keys[static_cast<size_t>(finalIndex)] : nullptr;
}

bool RemoveTrackKey(std::vector<KeyTrack>& tracks, const std::string_view path, const double timeSeconds)
{
    KeyTrack* track = FindKeyTrackMutable(tracks, path);
    if (track == nullptr)
    {
        return false;
    }
    const int32_t index = FindKeyIndexAt(*track, timeSeconds);
    if (index < 0)
    {
        return false;
    }
    track->Keys.erase(track->Keys.begin() + index);
    if (track->Keys.empty())
    {
        RemoveKeyTrack(tracks, path);
    }
    return true;
}

bool RemoveKeyTrack(std::vector<KeyTrack>& tracks, const std::string_view path)
{
    for (auto it = tracks.begin(); it != tracks.end(); ++it)
    {
        if (it->Path == path)
        {
            tracks.erase(it);
            return true;
        }
    }
    return false;
}

void TransformTrackKeys(
    KeyTrack& track,
    const std::span<const size_t> selectedIndices,
    const double offsetSeconds,
    const double scaleFactor,
    const double pivotSeconds)
{
    for (const size_t idx : selectedIndices)
    {
        if (idx < track.Keys.size())
        {
            const double shifted = pivotSeconds + (track.Keys[idx].TimeSeconds - pivotSeconds) * scaleFactor + offsetSeconds;
            track.Keys[idx].TimeSeconds = RoundKeyTimeSeconds(std::max(0.0, shifted));
        }
    }
    std::sort(track.Keys.begin(), track.Keys.end(), [](const TrackKey& l, const TrackKey& r) noexcept
    {
        return l.TimeSeconds < r.TimeSeconds;
    });
    for (size_t i = track.Keys.size(); i > 1u; --i)
    {
        if (std::fabs(track.Keys[i - 1u].TimeSeconds - track.Keys[i - 2u].TimeSeconds) <= kKeyTimeEpsilonSeconds)
        {
            track.Keys.erase(track.Keys.begin() + static_cast<std::ptrdiff_t>(i - 2u));
        }
    }
}

std::optional<LayerTrackPathParts> SplitLayerTrackPath(const std::string_view path) noexcept
{
    const size_t slash = path.find('/');
    if (slash == std::string_view::npos)
    {
        return std::nullopt;
    }
    return LayerTrackPathParts { path.substr(0u, slash), path.substr(slash + 1u) };
}

double ComputeAffectedTimeFrom(const KeyTrack& track, const double editTimeSeconds) noexcept
{
    // Reference: Experimental/FluidEditor/Engine/KeyTracks.js L351-L360
    if (track.Keys.empty())
    {
        return 0.0;
    }
    size_t index = track.Keys.size();
    for (size_t i = 0u; i < track.Keys.size(); ++i)
    {
        if (track.Keys[i].TimeSeconds >= editTimeSeconds - kKeyTimeEpsilonSeconds)
        {
            index = i;
            break;
        }
    }
    if (index == 0u)
    {
        return 0.0;
    }
    if (track.Extrapolation != KeyExtrapolationOrdinal::Hold)
    {
        return std::max(0.0, std::min(track.Keys.front().TimeSeconds, editTimeSeconds));
    }
    const size_t anchorIndex = (index >= 2u) ? (index - 2u) : 0u;
    return std::max(0.0, std::min(track.Keys[anchorIndex].TimeSeconds, editTimeSeconds));
}

} // namespace Frontier::SimulationSpace::ForceFields
