/*============================================================================================================================================
                                                               IMAGECODEC.H
============================================================================================================================================*/
// 🧩 .ImageSpace encoder & decoder persisting linear-light .CanvasLayer metadata, .LayerStackOrder, and 64B-aligned pixels

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

struct ImageCanvasLayerDescriptor
{
    std::string LayerTitle        { "PrimaryLayer" }; // [-]   - Human-readable layer title
    double      OpacityFactor     = 1.0;           // [0-1] - Linear layer opacity
    bool        VisibilityEnabled = true;          // [-]   - Layer visibility toggle
};

struct ImageSpaceDocumentModel
{
    std::string                             DocumentTitle      { "UntitledCanvas" }; // [-]  - .ImageSpace document title
    uint32_t                                NominalWidthPx     = 2048u;              // [px] - Reference horizontal resolution
    uint32_t                                NominalHeightPx    = 2048u;              // [px] - Reference vertical resolution
    std::vector<ImageCanvasLayerDescriptor> Layers             {};                   // [-]  - Ordered .CanvasLayer descriptors
    std::vector<float>                      LinearRgbaPreview  {};                   // [0-1]- Optional 64B-aligned linear RGBA preview
};

class ImageEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const ImageSpaceDocumentModel&     Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const ImageSpaceDocumentModel&     Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class ImageDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        ImageSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        ImageSpaceDocumentModel&     OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct ImageCodec
{
    using Encoder = ImageEncoder;
    using Decoder = ImageDecoder;
};

} // namespace Frontier::Codecs
