/*============================================================================================================================================
                                                           COLOURALIASREPOSITORY.H
============================================================================================================================================*/
// 🧩 Engine-wide ColourSpace alias codex resolving named colours, gradients, blackbody illuminants & modular ColourSourceProfiles into any colour space

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourSourceTypes.h"
#include "Engine/ColourSpace/ColourTypes.h"
#include "Engine/ColourSpace/SpectralDistribution.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourAliasEntry
{
    std::string              PrimaryAlias         {};     // [-]   - Canonical human-readable alias ("Royal Blue", "Aurora", "Daylight D65", ...)
    std::string              SeriesTitle          {};     // [-]   - Collection or series caption ("Blue", "Botanical", "Studio", "Illuminant", "Project")
    std::vector<std::string> AlternateAliases     {};     // [-]   - Short synonyms & identifiers ("Blue", "rainbow", "fire", "D65", ...)
    ColourSourceProfile      SourceProfile        {};     // [-]   - Canonical modular payload (Solid, Fill, Multi-Stop Ramp, Quantized, Blackbody, Discrete, Noise)

    // Precomputed representative colorimetric projections (exact pigment for solids; t=0.5 sample for gradients/fills/blackbody)
    LinearRgba               RepresentativeLinear { 1.0f, 1.0f, 1.0f, 1.0f }; // [0-1+] - Scene-referred Linear sRGB / Rec.709
    SrgbRgba8                RepresentativeSrgb8  { 255u, 255u, 255u, 255u }; // [0-255]- Display 8-bit sRGB
    OklabTriplet             RepresentativeOklab  {};                         // [-]    - Perceptual Oklab triplet
    HslTriplet               RepresentativeHsl    {};                         // [deg,%]- HSL triplet
    CmykQuadruplet           RepresentativeCmyk   {};                         // [0-1]  - CMYK separation
    std::string              HexUpper             { "#FFFFFF" };              // [-]    - Uppercase "#RRGGBB"
    float                    RelativeLuminance    = 1.0f;                     // [0-1]  - WCAG 2.1 relative luminance
    bool                     ReadOnlyEngineAlias  = true;                     // [-]    - True for shipped read-only built-in aliases

    [[nodiscard]] ColourSourceCategory QuerySourceCategory() const noexcept
    {
        return QueryColourSourceCategory(SourceProfile.Source);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ColourAliasRepository
{
public:
    ColourAliasRepository();

    void ResetToBuiltInAliases();

    // Register or update ANY modular ColourSourceProfile (Solid, Fill, Gradient, Quantized, Blackbody, Discrete, SpatialNoise) under a named alias
    std::size_t RegisterSourceAlias(
        std::string_view         PrimaryAlias,
        ColourSourceProfile      Profile,
        std::string_view         SeriesTitle         = "Project",
        std::vector<std::string> AlternateAliases    = {},
        bool                     ReadOnlyEngineAlias = false);

    // Convenience registration for a solid pigment alias from hex notation (e.g. "Frontier Cobalt", "#2454FF")
    std::size_t RegisterSolidHexAlias(
        std::string_view         PrimaryAlias,
        std::string_view         HexNotation,
        std::string_view         SeriesTitle         = "Project",
        std::vector<std::string> AlternateAliases    = {},
        bool                     ReadOnlyEngineAlias = false);

    // Convenience registration for a solid linear pigment alias
    std::size_t RegisterSolidLinearAlias(
        std::string_view         PrimaryAlias,
        LinearRgba               LinearTint,
        std::string_view         SeriesTitle         = "Project",
        std::vector<std::string> AlternateAliases    = {},
        bool                     ReadOnlyEngineAlias = false);

    // Convenience registration for a named blackbody illuminant alias (e.g. "Tungsten", 3200K)
    std::size_t RegisterBlackbodyAlias(
        std::string_view         PrimaryAlias,
        float                    KelvinTemperature,
        std::string_view         SeriesTitle         = "Illuminant",
        std::vector<std::string> AlternateAliases    = {},
        bool                     ReadOnlyEngineAlias = false);

    // Synchronize a built-in or project multi-stop colour ramp preset into the unified alias codex
    std::size_t SyncRampPreset(const ColourRampPresetEntry& Preset);

    bool RemoveCustomAlias(std::string_view AliasToken);

    // Queries & lookup (case-insensitive, whitespace/underscore/hyphen-tolerant across PrimaryAlias & AlternateAliases)
    [[nodiscard]] std::span<const ColourAliasEntry> QueryAliases() const noexcept;
    [[nodiscard]] const ColourAliasEntry*           QueryAliasAt(std::size_t Index) const noexcept;
    [[nodiscard]] const ColourAliasEntry*           FindAlias(std::string_view AliasToken) const noexcept;
    [[nodiscard]] std::size_t                        FindAliasIndex(std::string_view AliasToken) const noexcept;
    [[nodiscard]] std::vector<std::size_t>           QueryIndicesByCategory(ColourSourceCategory Category) const;

    // Universal multi-format resolvers — accept ANY registered alias (solid, gradient, ramp, blackbody, synonym), "#RRGGBB" hex, or "<N>K" Kelvin token
    [[nodiscard]] bool ResolveAliasToProfile(std::string_view Token, ColourSourceProfile& OutProfile) const;

    [[nodiscard]] bool ResolveAliasToLinear(
        std::string_view Token,
        LinearRgba&      OutLinear,
        float            SampleT = 0.5f) const;

    [[nodiscard]] bool ResolveAliasInPrimaries(
        std::string_view      Token,
        ChromaticityPrimaries TargetPrimaries,
        LinearRgba&           OutTint,
        float                 SampleT = 0.5f) const;

    [[nodiscard]] bool ResolveAliasToSrgb8(
        std::string_view      Token,
        SrgbRgba8&            OutSrgb8,
        float                 SampleT = 0.5f,
        TransferCurveCategory Curve   = TransferCurveCategory::PiecewiseSrgb) const;

    [[nodiscard]] bool ResolveAliasToOklab(
        std::string_view Token,
        OklabTriplet&    OutOklab,
        float            SampleT = 0.5f) const;

    [[nodiscard]] bool ResolveAliasToOklch(
        std::string_view Token,
        OklchTriplet&    OutOklch,
        float            SampleT = 0.5f) const;

    [[nodiscard]] bool ResolveAliasToHsl(
        std::string_view Token,
        HslTriplet&      OutHsl,
        float            SampleT = 0.5f) const;

    [[nodiscard]] bool ResolveAliasToCmyk(
        std::string_view Token,
        CmykQuadruplet&  OutCmyk,
        float            SampleT = 0.5f) const;

    [[nodiscard]] bool ResolveAliasToSpectrum(
        std::string_view      Token,
        SpectralCurveProfile& OutSpectrum,
        float                 SampleT = 0.5f) const;

private:
    [[nodiscard]] static std::string       NormalizeAliasKey(std::string_view Text);
    [[nodiscard]] static bool              TryParseKelvinToken(std::string_view Token, float& OutKelvin) noexcept;
    [[nodiscard]] static ColourAliasEntry ConstructAliasEntry(
        std::string_view         PrimaryAlias,
        ColourSourceProfile      Profile,
        std::string_view         SeriesTitle,
        std::vector<std::string> AlternateAliases,
        bool                     ReadOnlyEngineAlias);

    std::vector<ColourAliasEntry> Aliases {}; // [-] - Unified ordered collection of solid, gradient, thermal & procedural aliases
};

} // namespace Frontier::ColourSpace
