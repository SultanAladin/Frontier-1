/*============================================================================================================================================
                                                               COLOURCODEC.H
============================================================================================================================================*/
// 🧩 .ColourSpace encoder & decoder persisting radiometric swatches, ramps, and 64B-aligned spectral power samples

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RadiometricSwatchEntry
{
    std::string SwatchName  {};     // [-]   - Semantic swatch label
    double      LinearRed   = 0.0;  // [0-1] - Scene-referred linear red intensity
    double      LinearGreen = 0.0;  // [0-1] - Scene-referred linear green intensity
    double      LinearBlue  = 0.0;  // [0-1] - Scene-referred linear blue intensity
    double      Opacity     = 1.0;  // [0-1] - Alpha opacity factor
};

struct ColourSpaceDocumentModel
{
    std::string                         DocumentTitle          { "RadiometricColours" }; // [-]  - Container title
    std::string                         ReferencePrimaries     { "ACEScg" };             // [-]  - Scene-linear colourimetric primaries
    double                              WhitePointKelvin       = 6504.0;                 // [K]  - Illuminant correlated colour temperature
    std::vector<RadiometricSwatchEntry> Swatches               {};                       // [-]  - Named linear-light colour swatches
    std::vector<float>                  SpectralSamplesNanometer {};                     // [W]  - 64B-aligned .Spectrum binary samples
};

class ColourEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const ColourSpaceDocumentModel&    Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const ColourSpaceDocumentModel&    Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class ColourDecoder
{
public:
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
};

struct ColourCodec
{
    using Encoder = ColourEncoder;
    using Decoder = ColourDecoder;
};

} // namespace Frontier::Codecs
