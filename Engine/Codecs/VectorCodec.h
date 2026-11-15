/*============================================================================================================================================
                                                                VECTORCODEC.H
============================================================================================================================================*/
// 🧩 .VectorSpace encoder & decoder persisting .StrokeAppearance, .PathCurve, [VectorWorkspace], [[VectorLayers]], and .SvgXml sections

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"

#include <array>
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

struct VectorWorkspaceGradientStopEntry
{
    std::array<uint8_t, 4u> TintRgba { 255u, 255u, 255u, 255u };
    float                   Position = 0.0f;
};

struct VectorWorkspacePinEntry
{
    bool                    Active   = false;
    float                   NormX    = 0.5f;
    float                   NormY    = 0.5f;
    std::array<uint8_t, 4u> TintRgba { 255u, 255u, 255u, 255u };
};

struct VectorWorkspaceChannelEntry
{
    uint32_t                ChannelKey    = 0u;
    float                   ScalarWeight  = 0.5f;
    std::array<uint8_t, 4u> TintRgba      { 232u, 194u, 122u, 255u };
};

struct VectorWorkspaceMaskEntry
{
    bool    Enabled   = false;
    bool    Invert    = false;
    uint8_t Mode      = 0u;
    float   Frequency = 4.0f;
    float   Threshold = 0.45f;
    float   Softness  = 0.25f;
    int32_t Seed      = 211;
};

struct VectorWorkspaceModifierEntry
{
    uint8_t                 Category       = 0u;
    bool                    Enabled        = true;
    uint8_t                 NoiseAlgorithm = 0u;
    uint8_t                 ModulateTarget = 0u;
    float                   MinScale       = 0.40f;
    float                   MaxScale       = 1.60f;
    float                   Frequency      = 3.5f;
    std::array<uint8_t, 4u> TargetTintBRgba { 118u, 178u, 216u, 255u };
    uint8_t                 GridLayout     = 2u;
    int32_t                 Columns        = 8;
    int32_t                 Rows           = 8;
    int32_t                 RadialCount    = 8;
    float                   RadiusPx       = 110.0f;
    int32_t                 Rings          = 1;
    uint8_t                 MirrorAxis     = 2u;
    int32_t                 EchoCopies     = 3;
    float                   InsetStepPx    = 24.0f;
    std::array<uint8_t, 4u> AltTintRgba    { 24u, 27u, 31u, 255u };
    float                   PosJitterPx    = 16.0f;
    float                   RotJitterDeg   = 25.0f;
    float                   Dropout        = 0.15f;
    uint8_t                 WarpMode       = 0u;
    float                   WarpAmplitude  = 24.0f;
    float                   ChipWearAmount = 0.25f;
};

struct VectorWorkspaceKnotEntry
{
    float AnchorX    = 0.0f;
    float AnchorY    = 0.0f;
    float InHandleX  = 0.0f;
    float InHandleY  = 0.0f;
    float OutHandleX = 0.0f;
    float OutHandleY = 0.0f;
    bool  Smooth     = true;
};

struct VectorWorkspaceSubpathEntry
{
    bool                                ClosedLoop = true;
    std::vector<VectorWorkspaceKnotEntry> Knots    {};
};

struct VectorWorkspaceLayerEntry
{
    uint32_t                                Identity          = 1u;
    uint32_t                                EnclosingFolderId = 0u;
    std::string                             Title             {};
    uint8_t                                 Category          = 0u;
    bool                                    Visible           = true;
    bool                                    Locked            = false;
    bool                                    ExpandedDrawer    = false;
    bool                                    CollapsedFolder   = false;
    uint8_t                                 BlendMode         = 0u;
    float                                   Opacity           = 1.0f;
    uint8_t                                 MotifShape        = 0u;
    bool                                    ClosedCurveLoop   = true;
    float                                   CurveSmoothness   = 0.5f;
    std::string                             TextContent       {};
    float                                   FontSizePx        = 54.0f;
    std::string                             FontFilePath     {};
    std::string                             TypefaceTitle   {};
    float                                   LetterSpacingEm   = 0.0f;
    std::string                             SvgPathSnippet    {};
    bool                                    FillEnabled       = true;
    uint8_t                                 FillSource        = 0u;
    std::array<uint8_t, 4u>                 FillTintRgba      { 239u, 244u, 207u, 255u };
    std::array<uint8_t, 4u>                 FillTintBRgba     { 90u, 118u, 146u, 255u };
    float                                   GradientAngleDeg  = 45.0f;
    std::vector<VectorWorkspaceGradientStopEntry> FillStops   {};
    float                                   StrokeWidthPx     = 0.0f;
    uint8_t                                 StrokeSource      = 0u;
    std::array<uint8_t, 4u>                 StrokeTintRgba    { 22u, 24u, 28u, 255u };
    uint8_t                                 StrokeCap         = 1u;
    uint8_t                                 StrokeJoin        = 1u;
    bool                                    EvenOddFillRule   = false;
    float                                   CenterX           = 256.0f;
    float                                   CenterY           = 256.0f;
    float                                   WidthPx           = 128.0f;
    float                                   HeightPx          = 128.0f;
    float                                   RotationDeg       = 0.0f;
    uint8_t                                 FinishPreset      = 0u;
    std::array<VectorWorkspacePinEntry, 12u> PointPins        {};
    std::vector<VectorWorkspaceSubpathEntry> Subpaths         {};
    std::vector<VectorWorkspaceChannelEntry> Channels         {};
    VectorWorkspaceMaskEntry                 Mask             {};
    std::vector<VectorWorkspaceModifierEntry> Modifiers       {};
};

struct VectorSpaceDocumentModel
{
    std::string                            DocumentTitle           { "TechnicalVector" }; // [-]  - .VectorSpace title
    double                                 DefaultStrokeWidthMm    = 0.35;                // [mm] - Physical ISO technical pen width
    double                                 ViewBoxAspectRatio      = 1.0;                 // [-]  - Normalized ViewBox aspect ratio
    std::vector<VectorPathCurveDescriptor> Curves                  {};                    // [-]  - .PathCurve contour descriptors
    std::vector<double>                    ControlPointKnots       {};                    // [-]  - 64B-aligned [u, v, tu, tv] knots

    // Pattern / Stamp Studio workspace persistence ([VectorWorkspace] + [[VectorLayers]] + .SvgXml)
    bool                                   WorkspaceDocumentPresent = false;
    std::string                            WorkspaceProfile        { "Pattern" };
    uint32_t                               ResolutionPx            = 1024u;
    bool                                   GroundEnabled           = true;
    uint8_t                                GroundFillSource        = 0u;
    std::array<uint8_t, 4u>                GroundTintARgba         { 23u, 26u, 31u, 255u };
    std::array<uint8_t, 4u>                GroundTintBRgba         { 11u, 11u, 11u, 255u };
    float                                  GroundAngleDeg          = 45.0f;
    bool                                   GhostTileBordersVisible = true;
    std::string                            EmbeddedSvgXml          {};
    std::vector<VectorWorkspaceLayerEntry> WorkspaceLayers         {};
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
