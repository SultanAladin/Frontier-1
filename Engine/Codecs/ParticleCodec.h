/*============================================================================================================================================
                                                               PARTICLECODEC.H
============================================================================================================================================*/
// 🧩 Two-Tier Hybrid .ParticleSpace encoder & decoder delegating colours to ColourCodec and forces to VectorFieldCodec

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/ParticleSpace/ParticleDocument.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier::Workspaces { class RevisionTracker; }

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ParticleEncoder
{
public:
    static constexpr std::string_view CanonicalExtension = ".ParticleSpace";

    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const ParticleSpace::ParticleDocument& Document,
        const Workspaces::RevisionTracker*     OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&           TargetPath,
        const ParticleSpace::ParticleDocument& Document,
        std::string&                           OutDiagnostic,
        const Workspaces::RevisionTracker*     OptionalRevision = nullptr);
};

class ParticleDecoder
{
public:
    [[nodiscard]] static bool ExtractDocument(
        const SpaceContainerPayload&     Payload,
        ParticleSpace::ParticleDocument& OutDocument,
        std::string&                     OutDiagnostic,
        Workspaces::RevisionTracker*     OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&     SourcePath,
        ParticleSpace::ParticleDocument& OutDocument,
        std::string&                     OutDiagnostic,
        Workspaces::RevisionTracker*     OptionalOutRevision = nullptr);
};

struct ParticleCodec
{
    using Encoder = ParticleEncoder;
    using Decoder = ParticleDecoder;
};

} // namespace Frontier::Codecs
