/*============================================================================================================================================
                                                               COLOURCODEC.H
============================================================================================================================================*/
// 🧩 .ColourSpace, .ColourRamp & .ColourSwatch encoder/decoder persisting radiometric swatches, modular ramps, spectra & 64B-aligned LUTs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/ColourSpace/ColourDocument.h"
#include "Engine/ColourSpace/ColourSourceTypes.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RadiometricSwatchEntry
{
    std::string SwatchName  {};           // [-]   - Semantic swatch label
    double      LinearRed   = 0.0;        // [0-1] - Scene-referred linear red intensity
    double      LinearGreen = 0.0;        // [0-1] - Scene-referred linear green intensity
    double      LinearBlue  = 0.0;        // [0-1] - Scene-referred linear blue intensity
    double      Opacity     = 1.0;        // [0-1] - Alpha opacity factor
    std::string SeriesTitle { "Custom" }; // [-]   - Optional collection caption
};

struct ColourSpaceDocumentModel
{
    std::string                                     DocumentTitle             { "RadiometricColours" }; // [-] - Container title
    std::string                                     ReferencePrimaries        { "ACEScg" };             // [-] - Scene-linear colourimetric primaries
    std::string                                     TransferCurve             { "Linear" };             // [-] - Authoring transfer curve token
    std::string                                     DefaultInterpolationSpace { "LinearSrgb" };         // [-] - Ramp interpolation colour space
    double                                          WhitePointKelvin          = 6504.0;                 // [K] - Illuminant correlated colour temperature
    std::vector<RadiometricSwatchEntry>             Swatches                  {};                       // [-] - Named linear-light colour swatches
    std::vector<ColourSpace::AuthoredColourRamp>    Ramps                     {};                       // [-] - Modular authored colour ramps
    std::vector<float>                              SpectralSamplesNanometer  {};                       // [W] - 64B-aligned .Spectrum binary samples
    std::vector<ColourSpace::ColourRampLut64>       BakedLuts64               {};                       // [-] - 64B-aligned .ColourLut binary blocks

    [[nodiscard]] static ColourSpaceDocumentModel FromColourDocument(const ColourSpace::ColourDocument& Document);
    [[nodiscard]] ColourSpace::ColourDocument     ToColourDocument() const;
};

class ColourEncoder
{
public:
    static constexpr std::string_view CanonicalRampExtension   = ".ColourRamp";
    static constexpr std::string_view CanonicalSwatchExtension = ".ColourSwatch";

    [[nodiscard]] static toml::table SerializeSourceToManifest(const ColourSpace::ColourSourceProfile& Profile);

    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const ColourSpaceDocumentModel&    Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const ColourSpaceDocumentModel&    Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeDocumentToFile(
        const std::filesystem::path&       TargetPath,
        const ColourSpace::ColourDocument& Document,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    // 📝 Standalone singular .ColourRamp preset container (canonical PascalCase write)
    [[nodiscard]] static std::string EncodeStandaloneRampToToml(const ColourSpace::AuthoredColourRamp& Ramp);
    [[nodiscard]] static bool        EncodeStandaloneRampToFile(
        const std::filesystem::path&           TargetPath,
        const ColourSpace::AuthoredColourRamp& Ramp,
        std::string&                           OutDiagnostic);
};

class ColourDecoder
{
public:
    [[nodiscard]] static bool MatchesRampExtension(const std::filesystem::path& FilePath) noexcept;
    [[nodiscard]] static bool MatchesSwatchExtension(const std::filesystem::path& FilePath) noexcept;

    [[nodiscard]] static bool ExtractSourceFromManifest(
        const toml::table&                 SourceManifest,
        ColourSpace::ColourSourceProfile&  OutProfile,
        std::string&                       OutDiagnostic);

    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        ColourSpaceDocumentModel&    OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        ColourSpaceDocumentModel&    OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeDocumentFromFile(
        const std::filesystem::path& SourcePath,
        ColourSpace::ColourDocument& OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    // 📝 Standalone singular .ColourRamp preset container (case-insensitive load)
    [[nodiscard]] static bool DecodeStandaloneRampFromToml(
        std::string_view                 TomlText,
        ColourSpace::AuthoredColourRamp& OutRamp,
        std::string&                     OutDiagnostic);

    [[nodiscard]] static bool DecodeStandaloneRampFromFile(
        const std::filesystem::path&     SourcePath,
        ColourSpace::AuthoredColourRamp& OutRamp,
        std::string&                     OutDiagnostic);
};

struct ColourCodec
{
    using Encoder = ColourEncoder;
    using Decoder = ColourDecoder;
};

} // namespace Frontier::Codecs
