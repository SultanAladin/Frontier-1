/*============================================================================================================================================
                                                                FONTCODEC.H
============================================================================================================================================*/
// 🧩 .FontSpace encoder & decoder persisting vector typography metrics, kerning pairs, and 64B-aligned contour streams

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct KerningAdjustmentPair
{
    uint32_t FirstCodepoint   = 0u;  // [ucs4] - Leading Unicode scalar codepoint
    uint32_t SecondCodepoint  = 0u;  // [ucs4] - Following Unicode scalar codepoint
    double   HorizontalEmStep = 0.0; // [em]   - Kerning advance delta in normalized em units
};

struct FontSpaceDocumentModel
{
    std::string                        TypefaceTitle        { "InterVariable" }; // [-]  - Typeface display title
    uint32_t                           UnitsPerEm           = 2048u;             // [u]  - Design grid units per em square
    double                             AscenderEm           = 0.968;             // [em] - Ascender height above baseline
    double                             DescenderEm          = -0.242;            // [em] - Descender depth below baseline
    std::vector<KerningAdjustmentPair> KerningPairs         {};                  // [-]  - Explicit kerning pair adjustments
    std::vector<float>                 SignedDistanceField  {};                  // [-]  - 64B-aligned .CharacterSet contour/SDF samples
};

class FontEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const FontSpaceDocumentModel&      Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const FontSpaceDocumentModel&      Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class FontDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        FontSpaceDocumentModel&      OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        FontSpaceDocumentModel&      OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct FontCodec
{
    using Encoder = FontEncoder;
    using Decoder = FontDecoder;
};

} // namespace Frontier::Codecs
