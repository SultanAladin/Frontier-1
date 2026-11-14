/*============================================================================================================================================
                                                               VECTORCODEC.H
============================================================================================================================================*/
// 🧩 .VectorSpace encoder & decoder persisting .StrokeAppearance, .PathCurve metadata, and 64B-aligned Bezier control points

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

struct VectorPathCurveDescriptor
{
    std::string CurveTitle        {};    // [-]     - Semantic contour identifier
    bool        ClosedLoopEnabled = true;// [-]     - Closed contour indicator
    uint32_t    ControlPointCount = 0u;  // [count] - Number of Bezier knots in contour
};

struct VectorSpaceDocumentModel
{
    std::string                            DocumentTitle          { "TechnicalVector" }; // [-]  - .VectorSpace title
    double                                 DefaultStrokeWidthMm   = 0.35;                // [mm] - Physical ISO technical pen width
    double                                 ViewBoxAspectRatio     = 1.0;                 // [-]  - Normalized ViewBox aspect ratio
    std::vector<VectorPathCurveDescriptor> Curves                 {};                    // [-]  - .PathCurve contour descriptors
    std::vector<double>                    ControlPointKnots      {};                    // [-]  - 64B-aligned [u, v, tu, tv] knots
};

class VectorEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const VectorSpaceDocumentModel&    Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const VectorSpaceDocumentModel&    Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class VectorDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        VectorSpaceDocumentModel&    OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        VectorSpaceDocumentModel&    OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct VectorCodec
{
    using Encoder = VectorEncoder;
    using Decoder = VectorDecoder;
};

} // namespace Frontier::Codecs
