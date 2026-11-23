/*==========================================================================================================================================
                                                 ILLUMINATION FRAME DEFINITION
==========================================================================================================================================*/

#include "IlluminationUnits.h"

#include <algorithm>

namespace Frontier
{

IlluminationFrameDeclaration DeclareIlluminationFrame(uint32_t Width, uint32_t Height)
{
    IlluminationFrameDeclaration Frame;
    const uint32_t ClusterWidth  = std::max(Width / 16, 1u);    // [tiles] - 16×16 px light-cluster grid
    const uint32_t ClusterHeight = std::max(Height / 16, 1u);   // [tiles]

    const TargetTouch ReadSeq{"CandidateSequence", TargetUsage::StorageRead};
    const TargetTouch ReadClusters{"LightClusters", TargetUsage::StorageRead};
    const TargetTouch ReadSurface{"SurfaceAttributes", TargetUsage::Sampled};

    Frame.Units = {
        // Indirect prelude (port order 2): distance-field trace + card radiosity merge into
        // IndirectRadiance, which DirectRadiance's candidates then sample from.
        SubmissionUnitDeclaration{
            "DistanceRecordsSubmission",
            {TargetTouch{"DistanceFieldRecords", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "DistanceTraversalSubmission",
            {TargetTouch{"DistanceFieldRecords", TargetUsage::StorageRead}, ReadSurface,
             TargetTouch{"DistanceIndirect", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CardCaptureSubmission",
            {TargetTouch{"CardAtlas", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CardDirectSubmission",
            {TargetTouch{"CardAtlas", TargetUsage::StorageRead},
             TargetTouch{"CardLighting", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CardRadiositySubmission",
            {TargetTouch{"CardLighting", TargetUsage::StorageReadWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CardFinalSubmission",
            {TargetTouch{"CardLighting", TargetUsage::StorageRead}, ReadSurface,
             TargetTouch{"CardIndirect", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "DistanceTransferSubmission",
            {TargetTouch{"DistanceIndirect", TargetUsage::Sampled},
             TargetTouch{"CardIndirect", TargetUsage::Sampled},
             TargetTouch{"IndirectRadiance", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "DiscrepancySequenceSubmission",
            {TargetTouch{"CandidateSequence", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "ClusteredLightsSubmission",
            {TargetTouch{"LightClusters", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "DirectRadianceSubmission",
            {ReadSeq, ReadClusters, ReadSurface,
             TargetTouch{"IndirectRadiance", TargetUsage::Sampled},
             TargetTouch{"ReservoirsCurrent", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "ReservoirTemporalSubmission",
            {TargetTouch{"ReservoirsCurrent", TargetUsage::StorageRead},
             TargetTouch{"ReservoirsPrevious", TargetUsage::StorageRead},
             TargetTouch{"ReservoirsTemporal", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "ReservoirSpatialSubmission",
            {TargetTouch{"ReservoirsTemporal", TargetUsage::StorageRead}, ReadSurface,
             TargetTouch{"ReservoirsSpatial", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "MaterialResolveSubmission",
            {ReadSurface, TargetTouch{"ResolvedMaterial", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "SurfaceReflectanceSubmission",
            {TargetTouch{"ReservoirsSpatial", TargetUsage::StorageRead},
             TargetTouch{"ResolvedMaterial", TargetUsage::StorageRead},
             TargetTouch{"DirectLighting", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "VarianceTemporalSubmission",
            {TargetTouch{"DirectLighting", TargetUsage::StorageRead},
             TargetTouch{"LightingHistory", TargetUsage::Sampled},
             TargetTouch{"LightingMoments", TargetUsage::StorageReadWrite},
             TargetTouch{"FilterPing", TargetUsage::StorageWrite},
             TargetTouch{"VarianceEstimate", TargetUsage::StorageWrite}},
            {},
        },
    };

    // Wavelet cascade: ping-pong between FilterPing/FilterPong, variance updated in place per level.
    // After an odd level count the result rests in FilterPong (see VarianceUpsample's read below).
    for (uint32_t Level = 0; Level < IlluminationWaveletLevels; ++Level)
    {
        const bool FromPing = (Level % 2) == 0;
        Frame.Units.push_back(SubmissionUnitDeclaration{
            "VarianceWaveletSubmission" + std::to_string(Level),
            {TargetTouch{FromPing ? "FilterPing" : "FilterPong", TargetUsage::StorageRead},
             TargetTouch{"VarianceEstimate", TargetUsage::StorageReadWrite},
             ReadSurface,
             TargetTouch{FromPing ? "FilterPong" : "FilterPing", TargetUsage::StorageWrite}},
            {},
        });
    }

    Frame.Units.push_back(SubmissionUnitDeclaration{
        "VarianceUpsampleSubmission",
        {TargetTouch{"FilterPong", TargetUsage::Sampled}, ReadSurface,
         TargetTouch{"FilteredLighting", TargetUsage::StorageWrite}},
        {},
    });

    Frame.Targets = {
        RenderTargetDeclaration{"DistanceFieldRecords", TargetFormat::R32Float, 256, 256, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"DistanceIndirect", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"CardAtlas", TargetFormat::Rgba16Float, 1024, 1024, true, TargetUsage::StorageRead},
        RenderTargetDeclaration{"CardLighting", TargetFormat::Rgba16Float, 1024, 1024, true, TargetUsage::StorageReadWrite},
        RenderTargetDeclaration{"CardIndirect", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"IndirectRadiance", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"CandidateSequence", TargetFormat::Rg32Uint, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"LightClusters", TargetFormat::Rg32Uint, ClusterWidth, ClusterHeight, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"ReservoirsCurrent", TargetFormat::Rg32Uint, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"ReservoirsPrevious", TargetFormat::Rg32Uint, Width, Height, true, TargetUsage::StorageRead},
        RenderTargetDeclaration{"ReservoirsTemporal", TargetFormat::Rg32Uint, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"ReservoirsSpatial", TargetFormat::Rg32Uint, Width, Height, true, TargetUsage::StorageWrite},
        RenderTargetDeclaration{"SurfaceAttributes", TargetFormat::Rgba16Float, Width, Height, true, TargetUsage::Sampled},
        RenderTargetDeclaration{"ResolvedMaterial", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"DirectLighting", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"LightingHistory", TargetFormat::Rgba16Float, Width, Height, true, TargetUsage::Sampled},
        RenderTargetDeclaration{"LightingMoments", TargetFormat::Rg16Float, Width, Height, true, TargetUsage::StorageReadWrite},
        RenderTargetDeclaration{"FilterPing", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"FilterPong", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"VarianceEstimate", TargetFormat::R32Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"FilteredLighting", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
    };

    return Frame;
}

} // namespace Frontier
