/*============================================================================================================================================
                                                            RENDERSPACECODEC.H
============================================================================================================================================*/
// 🧩 .RenderSpace Two-Tier Hybrid encoder & decoder for Viewport Render Engine, Shadow Cascades, Distance Field Spans & Tone Transform

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RenderSpaceDocumentModel
{
    std::string                               DocumentTitle                 { "DefaultEditorViewport" };
    GeometricRaster::WorldRenderConfiguration RenderConfiguration           {};
    bool                                      CheapMatCapTwoByTwoShadow     = true;   // [-]  - Engine 0 cheap 2x2 bilinear shadow (no SDF, no PCSS)
    bool                                      AntiAliasingEnabled           = true;
    float                                     RenderScalePercent            = 100.0f;
    float                                     FineCascadeHalfSpanMeters     = 2.85f;
    float                                     CoarseCascadeHalfSpanMeters   = 8.50f;
    uint32_t                                  CascadeResolutionTexels       = 1024u;
    float                                     FineDistanceFieldHalfMeters   = 4.20f;
    float                                     CoarseDistanceFieldHalfMeters = 14.0f;
    float                                     StaticCloudRenderScale        = 1.00f;
    float                                     ExposureStopsEV               = 0.0f;
    std::string                               ToneTransformOperator         { "ACESFilmicSrgb" };
};

class RenderSpaceEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const RenderSpaceDocumentModel&    Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const RenderSpaceDocumentModel&    Model,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const RenderSpaceDocumentModel&    Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class RenderSpaceDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        RenderSpaceDocumentModel&    OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   ContainerBytes,
        RenderSpaceDocumentModel&    OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        RenderSpaceDocumentModel&    OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct RenderSpaceCodec
{
    using Encoder = RenderSpaceEncoder;
    using Decoder = RenderSpaceDecoder;
};

} // namespace Frontier::Codecs
