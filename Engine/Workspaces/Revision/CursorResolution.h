/*============================================================================================================================================
                                                             CURSORRESOLUTION.H
============================================================================================================================================*/
// 🧩 Active root-to-cursor revision replay reconstructing parametric documents at arbitrary target resolutions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/NonLinearHistory.h"
#include "Engine/Workspaces/Revision/OperationRecorder.h"

#include <cstdint>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ReconstructionTargetSpec
{
    uint32_t TargetWidthPx         = 2048u; // [px] - Requested horizontal pixel resolution
    uint32_t TargetHeightPx        = 2048u; // [px] - Requested vertical pixel resolution
    double   DotsPerInch           = 300.0; // [dpi] - Physical output density for vector stroke scaling
    double   LinearToleranceMeters = 1e-4;  // [m]  - Spatial curve subdivision tolerance
};

struct ResolvedBrushSegment
{
    double StartPixelX  = 0.0; // [px]  - Reconstructed start X at TargetWidthPx
    double StartPixelY  = 0.0; // [px]  - Reconstructed start Y at TargetHeightPx
    double EndPixelX    = 0.0; // [px]  - Reconstructed end X at TargetWidthPx
    double EndPixelY    = 0.0; // [px]  - Reconstructed end Y at TargetHeightPx
    double RadiusPixels = 1.0; // [px]  - Reconstructed brush radius scaled to target resolution
    double LinearRed    = 0.0; // [0-1] - Linear-light red channel intensity
    double LinearGreen  = 0.0; // [0-1] - Linear-light green channel intensity
    double LinearBlue   = 0.0; // [0-1] - Linear-light blue channel intensity
};

struct ResolvedUVTilePlacement
{
    int32_t  TileU                  = 0;     // [tile] - Integer UDIM horizontal UVTile index
    int32_t  TileV                  = 0;     // [tile] - Integer UDIM vertical UVTile index
    uint32_t UDIMNumber             = 1001u; // [code] - Canonical UDIM number (1001 + TileU + 10 * TileV)
    double   CumulativeTranslateU   = 0.0;   // [uv]   - Reconstructed horizontal UV offset
    double   CumulativeTranslateV   = 0.0;   // [uv]   - Reconstructed vertical UV offset
    double   CumulativeScale        = 1.0;   // [-]    - Reconstructed uniform UV scale
    double   CumulativeRotationRad  = 0.0;   // [rad]  - Reconstructed UV island rotation
    double   EffectiveTexelDensity  = 512.0; // [px/m] - Reconstructed texel density scaled by target resolution
};

struct ResolvedVectorControlPoint
{
    uint32_t ContourIndex   = 0u;  // [idx] - Owning vector contour index
    double   PositionPixelX = 0.0; // [px]  - Reconstructed X position at TargetWidthPx
    double   PositionPixelY = 0.0; // [px]  - Reconstructed Y position at TargetHeightPx
    double   StrokeWidthPx  = 1.0; // [px]  - Physical stroke width converted from mm via DotsPerInch
};

struct ReconstructedDocumentProjection
{
    ReconstructionTargetSpec                TargetSpec           {};   // [-]     - Resolution parameters used for replay
    uint32_t                                ReplayedStepCount    = 0u; // [count] - Total baseline + active steps evaluated
    double                                  NetChannelExposureEV = 0.0;// [EV]    - Accumulated scalar channel exposure delta
    std::vector<ResolvedBrushSegment>       BrushSegments        {};   // [-]     - Resolution-scaled brush strokes
    std::vector<ResolvedUVTilePlacement>    UVTiles              {};   // [-]     - Reconstructed UDIM UVTile placements
    std::vector<ResolvedVectorControlPoint> VectorControlPoints  {};   // [-]     - Physical-DPI-scaled vector control points
};

class CursorResolution
{
public:
    [[nodiscard]] static ReconstructedDocumentProjection ReconstructAtCursor(
        const NonLinearHistory&         History,
        const OperationRecorder&        Recorder,
        const ReconstructionTargetSpec& TargetSpec);

private:
    static void ApplyParametricStep(
        const ParametricOperationParameters& Step,
        const ReconstructionTargetSpec&      TargetSpec,
        ReconstructedDocumentProjection&     OutProjection);
};

} // namespace Frontier::Workspaces
