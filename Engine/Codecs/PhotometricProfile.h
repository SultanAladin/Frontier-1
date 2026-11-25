/*============================================================================================================================================
                                                            PHOTOMETRICPROFILE.H
============================================================================================================================================*/
// 🧩 ANSI/IESNA LM-63-1995/2002 Type C (.ies) photometric profile parser, symmetry folder, and 128x32 angular candela resampler

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t PhotometricVerticalCount   = 128u; // [samples] - Polar steps across [0, pi] inclusive
inline constexpr uint32_t PhotometricHorizontalCount = 32u;  // [samples] - Azimuthal steps across [0, 2*pi) wrapping
inline constexpr uint32_t PhotometricGridStride     = PhotometricVerticalCount * PhotometricHorizontalCount; // [4096 floats]

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RawPhotometricDistribution
{
    std::string                                      ProfileLabel     {};      // [-]  - Diagnostic profile identifier
    std::vector<std::pair<std::string, std::string>> KeywordEntries   {};      // [-]  - Bracketed LM-63 [KEYWORD] metadata
    std::vector<float>                               VerticalAngles   {};      // [deg]- Monotonic polar angles (0..90 or 0..180)
    std::vector<float>                               HorizontalAngles {};      // [deg]- Monotonic azimuthal angles
    std::vector<float>                               CandelaSamples    {};      // [cd] - Row-major [HorizontalIndex * VerticalCount + VerticalIndex]
    float                                            PeakCandela      = 0.0f;  // [cd] - Maximum scaled candela reading
};

struct ResampledPhotometricProfile
{
    std::string                                  ProfileLabel         {};      // [-]  - Originating profile title
    std::array<float, PhotometricGridStride>    NormalizedCandela    {};      // [0-1]- [Horizontal * 128 + Vertical] normalized grid
    float                                        PeakCandela          = 0.0f;  // [cd] - Peak luminous intensity prior to normalization
    float                                        NormalizedFluxSr     = 0.0f;  // [sr] - Solid-angle integral of NormalizedCandela over sphere
    float                                        CutoffHalfAngleRad = 3.14159265f; // [rad]- Conservative polar cutoff beyond which grid is zero
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  PHOTOMETRIC PROFILE CODEC
//------------------------------------------------------------------------------------------------------------------------

class PhotometricProfileCodec
{
public:
    // Parses an ANSI/IESNA LM-63-1995/2002 Type C text buffer into a scaled candela grid
    [[nodiscard]] static bool ParseLm63Text(
        std::string_view            SourceText,
        std::string_view            ProfileLabel,
        RawPhotometricDistribution& OutDistribution,
        std::string&                OutDiagnostic);

    // Reads and parses an LM-63 .ies file from disk
    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        RawPhotometricDistribution&  OutDistribution,
        std::string&                 OutDiagnostic);

    // Evaluates scaled candela [cd] at (VerticalDegrees in [0,180], HorizontalDegrees in [0,360)) using LM-63 symmetry rules
    [[nodiscard]] static float EvaluateCandelaAtDegrees(
        const RawPhotometricDistribution& Distribution,
        float                             VerticalDegrees,
        float                             HorizontalDegrees) noexcept;

    // Resamples a parsed Type C distribution onto the canonical 128x32 GPU/CPU angular grid
    [[nodiscard]] static bool ResampleToCanonicalGrid(
        const RawPhotometricDistribution& Distribution,
        ResampledPhotometricProfile&      OutProfile,
        std::string&                      OutDiagnostic);

    // Convenience routine decoding and resampling an .ies file in one step
    [[nodiscard]] static bool LoadResampledProfileFromFile(
        const std::filesystem::path& SourcePath,
        ResampledPhotometricProfile& OutProfile,
        std::string&                 OutDiagnostic);

    // Evaluates normalized [0..1] intensity toward UnitLightToSurface given unit BeamAxis and unit ZeroAzimuthAxis
    [[nodiscard]] static float EvaluateNormalizedIntensity(
        const ResampledPhotometricProfile& Profile,
        Mathematics::Axis3D                UnitLightToSurface,
        Mathematics::Axis3D                UnitBeamAxis,
        Mathematics::Axis3D                UnitZeroAzimuthAxis) noexcept;

    // Loads the 3 canonical studio .ies profiles: [0] LowBeamHeadlamp, [1] TailLamp, [2] CarParkDownlight
    [[nodiscard]] static bool LoadCanonicalPhotometrySuite(
        const std::filesystem::path&              RepositoryRoot,
        std::vector<ResampledPhotometricProfile>& OutSuite,
        std::string&                              OutDiagnostic);
};

} // namespace Frontier::Codecs
