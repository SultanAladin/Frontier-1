/*============================================================================================================================================
                                                           KEYTRACKSAMPLER.H
============================================================================================================================================*/
// 🧩 Keyed parameter track sampling (Constant, Linear, Fritsch-Carlson Smooth, Bezier), extrapolation, linear-light colour easing, and timing envelopes
// Reference: Experimental/FluidEditor/Engine/KeyTracks.js L1-L360

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kMaximumKeysPerTrack   = 256u;           // [-] - Hard cap per track (KeyTracks.js L31)
inline constexpr double   kKeyTimeEpsilonSeconds = 1.0 / 480.0;    // [s] - Half of a 240 Hz step (KeyTracks.js L33)

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class KeyEaseOrdinal : uint8_t
{
    Constant = 0u, // [-] - Hold segment start amount
    Linear   = 1u, // [-] - Linear interpolation across segment
    Smooth   = 2u, // [-] - Fritsch-Carlson monotone cubic Hermite (auto tangents, zero overshoot)
    Bezier   = 3u, // [-] - Cubic Hermite with explicit Out/In tangents (amount / second)
};

enum class KeyExtrapolationOrdinal : uint8_t
{
    Hold     = 0u, // [-] - Clamp to last key amount after end
    Cycle    = 1u, // [-] - Periodic wrap modulo Period
    PingPong = 2u, // [-] - Periodic triangular reflection modulo 2 * Period
};

enum class KeyValueCategory : uint8_t
{
    Scalar    = 0u, // [-] - Numeric double (Float32 / Int32 / UInt32)
    Vector3   = 1u, // [-] - 3-component numeric vector
    ColourHex = 2u, // [-] - "#rrggbb" hex colour eased in linear sRGB light
    StepToken = 3u, // [-] - Stepped Bool / Choice / Token string (always Constant)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

using KeyValueVariant = std::variant<double, std::array<double, 3>, std::string>;

struct TrackKey
{
    double                  TimeSeconds = 0.0;                      // [s]        - Key timestamp (sorted ascending)
    KeyValueVariant         Amount      = 0.0;                      // [-]        - Scalar, Vector3, "#rrggbb", or stepped token
    KeyEaseOrdinal          Ease        = KeyEaseOrdinal::Linear;   // [-]        - Segment interpolation mode starting at this key
    std::array<double, 3>   InTangent   = { 0.0, 0.0, 0.0 };        // [unit/s]   - Incoming Bezier slope (x used for Scalar)
    std::array<double, 3>   OutTangent  = { 0.0, 0.0, 0.0 };        // [unit/s]   - Outgoing Bezier slope (x used for Scalar)
};

struct KeyTrack
{
    std::string             Path          {};                               // [-] - "<Field>" or "<layer-token>/<Parameter>"
    KeyExtrapolationOrdinal Extrapolation = KeyExtrapolationOrdinal::Hold;  // [-] - Post-last-key behaviour
    double                  PeriodSeconds = 0.0;                            // [s] - Cycle / PingPong period (0 = last - first)
    bool                    FromEnvelope  = false;                          // [-] - True when generated from TimingEnvelope
    std::vector<TrackKey>   Keys          {};                               // [-] - Sorted key sequence (max 256)
};

struct TimingEnvelopeParameters
{
    double StartTimeSeconds = 0.0;  // [s] - Activation start time
    double DurationSeconds  = 0.0;  // [s] - Active window duration (0 = infinite)
    double FadeSeconds      = 0.15; // [s] - Linear fade-in / fade-out duration
    double RepeatSeconds    = 0.0;  // [s] - Periodic repeat interval (0 = one-shot)
};

struct LayerTrackPathParts
{
    std::string_view LayerToken    {}; // [-] - Segment before '/'
    std::string_view ParameterName {}; // [-] - Segment after '/'
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOUR & CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool IsHexColourString(std::string_view text) noexcept;
[[nodiscard]] KeyValueCategory ClassifyKeyValue(const KeyValueVariant& amount) noexcept;
[[nodiscard]] std::array<double, 3> HexColourToLinearRgb(std::string_view hex) noexcept;
[[nodiscard]] std::string LinearRgbToHexColour(const std::array<double, 3>& linearRgb);

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLING & ENVELOPES
//------------------------------------------------------------------------------------------------------------------------

/// Rounds a timestamp to the microsecond grid (KeyTracks.js L203).
[[nodiscard]] double RoundKeyTimeSeconds(double timeSeconds) noexcept;

/// Computes local track time after Hold / Cycle / PingPong extrapolation (KeyTracks.js L103-L114).
[[nodiscard]] double ComputeTrackLocalTime(const KeyTrack& track, double timeSeconds) noexcept;

/// Samples `track` at `timeSeconds` with exact KeyTracks.js L118-L139 semantics.
[[nodiscard]] std::optional<KeyValueVariant> SampleKeyTrack(const KeyTrack& track, double timeSeconds, bool stepped = false);

/// Convenience scalar sampler returning fallback when the track is empty or non-scalar.
[[nodiscard]] double SampleScalarTrack(const KeyTrack& track, double timeSeconds, double fallbackValue = 0.0) noexcept;

/// Generates the exact piecewise-linear Weight track from StartTime / Duration / Fade / Repeat (KeyTracks.js L247-L270).
[[nodiscard]] std::optional<KeyTrack> ConstructEnvelopeWeightTrack(const TimingEnvelopeParameters& envelope);

/// Evaluates node weight in [0, 1] using explicit "Weight" track if present, otherwise TimingEnvelopeParameters (KeyTracks.js L280-L298).
[[nodiscard]] double EvaluateNodeTimingWeight(
    std::span<const KeyTrack> nodeTracks,
    const TimingEnvelopeParameters& envelope,
    double timeSeconds) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                TRACK EDITING & INVALIDATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] const KeyTrack* FindKeyTrack(std::span<const KeyTrack> tracks, std::string_view path) noexcept;
[[nodiscard]] KeyTrack* FindKeyTrackMutable(std::vector<KeyTrack>& tracks, std::string_view path) noexcept;
[[nodiscard]] int32_t FindKeyIndexAt(const KeyTrack& track, double timeSeconds) noexcept;

/// Inserts or replaces a key within kKeyTimeEpsilonSeconds (KeyTracks.js L160-L172).
TrackKey* SetTrackKey(
    std::vector<KeyTrack>& tracks,
    std::string_view path,
    double timeSeconds,
    const KeyValueVariant& amount,
    KeyEaseOrdinal ease = KeyEaseOrdinal::Smooth,
    bool steppedType = false);

/// Removes the key at `timeSeconds`, dropping the track if empty (KeyTracks.js L175-L183).
bool RemoveTrackKey(std::vector<KeyTrack>& tracks, std::string_view path, double timeSeconds);

/// Removes an entire track by path (KeyTracks.js L185-L190).
bool RemoveKeyTrack(std::vector<KeyTrack>& tracks, std::string_view path);

/// Offsets and scales selected key indices about `pivotSeconds`, sorting and collapsing epsilon overlaps (KeyTracks.js L193-L200).
void TransformTrackKeys(
    KeyTrack& track,
    std::span<const size_t> selectedIndices,
    double offsetSeconds,
    double scaleFactor = 1.0,
    double pivotSeconds = 0.0);

/// Splits "<layer-token>/<Parameter>" into its two halves (KeyTracks.js L344-L348).
[[nodiscard]] std::optional<LayerTrackPathParts> SplitLayerTrackPath(std::string_view path) noexcept;

/// Earliest time a key edit at `editTimeSeconds` can affect (Smooth reaches two keys back) (KeyTracks.js L351-L360).
[[nodiscard]] double ComputeAffectedTimeFrom(const KeyTrack& track, double editTimeSeconds) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields
