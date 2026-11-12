/*============================================================================================================================================
                                                            CURSORRESOLUTION.CPP
============================================================================================================================================*/
// 🧩 Resolution-independent parametric document reconstruction implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/CursorResolution.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                               RECONSTRUCTION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

ReconstructedDocumentProjection CursorResolution::ReconstructAtCursor(
    const NonLinearHistory&         History,
    const OperationRecorder&        Recorder,
    const ReconstructionTargetSpec& TargetSpec)
{
    ReconstructedDocumentProjection Projection{};
    Projection.TargetSpec = TargetSpec;
    Projection.TargetSpec.TargetWidthPx  = std::max(1u, TargetSpec.TargetWidthPx);
    Projection.TargetSpec.TargetHeightPx = std::max(1u, TargetSpec.TargetHeightPx);

    // 1. Replay folded parametric baseline operations first so Prefix Fold remains 100% lossless.
    const ParametricBaselineSummary& Baseline = Recorder.InspectBaseline();
    for (const ParametricOperationParameters& FoldedOp : Baseline.FoldedOperations)
    {
        ApplyParametricStep(FoldedOp, Projection.TargetSpec, Projection);
        ++Projection.ReplayedStepCount;
    }

    // 2. Replay active root-to-cursor revision chain in forward chronological order.
    std::vector<uint32_t> ActiveChain;
    History.ExtractActiveRootToCursorChain(ActiveChain);
    const auto Entries = History.InspectEntries();
    const auto Records = Recorder.InspectRecordEntries();

    for (uint32_t StepIndex : ActiveChain)
    {
        if (StepIndex >= Entries.size())
        {
            continue;
        }
        const uint32_t RecordIndex = Entries[StepIndex].RecordEntryIndex;
        if (RecordIndex == 0u || RecordIndex >= Records.size())
        {
            continue;
        }
        const ParametricRecordEntry& Entry = Records[RecordIndex];
        if (Entry.AbsorbedOrPurged)
        {
            continue;
        }
        ApplyParametricStep(Entry.Parameters, Projection.TargetSpec, Projection);
        ++Projection.ReplayedStepCount;
    }

    return Projection;
}

void CursorResolution::ApplyParametricStep(
    const ParametricOperationParameters& Step,
    const ReconstructionTargetSpec&      TargetSpec,
    ReconstructedDocumentProjection&     OutProjection)
{
    const double WidthF    = static_cast<double>(TargetSpec.TargetWidthPx);
    const double HeightF   = static_cast<double>(TargetSpec.TargetHeightPx);
    const double MinDimF   = std::min(WidthF, HeightF);
    const double Scale2048 = MinDimF / 2048.0;

    switch (Step.Category)
    {
        case ParametricOperationCategory::BaselineAnchor:
            break;

        case ParametricOperationCategory::NormalizedBrushSegment:
        {
            ResolvedBrushSegment Segment{};
            Segment.StartPixelX  = Step.Scalars[0] * WidthF;
            Segment.StartPixelY  = Step.Scalars[1] * HeightF;
            Segment.EndPixelX    = Step.Scalars[2] * WidthF;
            Segment.EndPixelY    = Step.Scalars[3] * HeightF;
            Segment.RadiusPixels = std::max(0.5, Step.Scalars[4] * MinDimF);
            Segment.LinearRed    = Step.Scalars[5];
            Segment.LinearGreen  = Step.Scalars[6];
            Segment.LinearBlue   = Step.Scalars[7];
            OutProjection.BrushSegments.push_back(Segment);
            break;
        }

        case ParametricOperationCategory::UVTileTransform:
        {
            const int32_t ClampedU = std::max(0, Step.TileU);
            const int32_t ClampedV = std::max(0, Step.TileV);
            const uint32_t UDIM    = static_cast<uint32_t>(1001 + (ClampedU % 10) + 10 * ClampedV);

            ResolvedUVTilePlacement* Existing = nullptr;
            for (ResolvedUVTilePlacement& Candidate : OutProjection.UVTiles)
            {
                if (Candidate.TileU == Step.TileU && Candidate.TileV == Step.TileV)
                {
                    Existing = &Candidate;
                    break;
                }
            }
            if (Existing == nullptr)
            {
                ResolvedUVTilePlacement Fresh{};
                Fresh.TileU  = Step.TileU;
                Fresh.TileV  = Step.TileV;
                Fresh.UDIMNumber = UDIM;
                OutProjection.UVTiles.push_back(Fresh);
                Existing = &OutProjection.UVTiles.back();
            }

            Existing->CumulativeTranslateU  += Step.Scalars[0];
            Existing->CumulativeTranslateV  += Step.Scalars[1];
            Existing->CumulativeScale       *= (Step.Scalars[2] <= 0.0 ? 1.0 : Step.Scalars[2]);
            Existing->CumulativeRotationRad += Step.Scalars[3];
            Existing->EffectiveTexelDensity  = (Step.Scalars[4] <= 0.0 ? 512.0 : Step.Scalars[4]) * Scale2048;
            break;
        }

        case ParametricOperationCategory::ScalarChannelAdjustment:
        {
            OutProjection.NetChannelExposureEV += Step.Scalars[1];
            break;
        }

        case ParametricOperationCategory::VectorContourControlPoint:
        {
            constexpr double MillimetersPerInch = 25.4;
            ResolvedVectorControlPoint Control{};
            Control.ContourIndex   = Step.TargetChannel;
            Control.PositionPixelX = Step.Scalars[0] * WidthF;
            Control.PositionPixelY = Step.Scalars[1] * HeightF;
            Control.StrokeWidthPx  = std::max(0.25, (Step.Scalars[4] / MillimetersPerInch) * TargetSpec.DotsPerInch);
            OutProjection.VectorControlPoints.push_back(Control);
            break;
        }

        case ParametricOperationCategory::StructuredPayloadDelta:
        {
            OutProjection.NetChannelExposureEV += Step.Scalars[0];
            break;
        }
    }
}

} // namespace Frontier::Workspaces
