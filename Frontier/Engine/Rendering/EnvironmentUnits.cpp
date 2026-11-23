/*==========================================================================================================================================
                                                  ENVIRONMENT FRAME DEFINITION
==========================================================================================================================================*/

#include "EnvironmentUnits.h"

namespace Frontier
{

EnvironmentFrameDeclaration DeclareEnvironmentFrame(uint32_t Width, uint32_t Height)
{
    EnvironmentFrameDeclaration Frame;

    const TargetTouch ReadTransmittance{"TransmittanceLut", TargetUsage::Sampled};
    const TargetTouch ReadMultiple{"MultipleScatteringLut", TargetUsage::Sampled};
    const TargetTouch ReadDepth{"SurfaceDepth", TargetUsage::DepthRead};

    Frame.Units = {
        SubmissionUnitDeclaration{
            "AtmosphereTransmittanceSubmission",
            {TargetTouch{"TransmittanceLut", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "AtmosphereMultipleSubmission",
            {ReadTransmittance, TargetTouch{"MultipleScatteringLut", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CelestialSkyRasterization",
            {ReadTransmittance, ReadMultiple, TargetTouch{"SkyRadiance", TargetUsage::ColourWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "NightStarsRasterization",
            {TargetTouch{"SkyRadiance", TargetUsage::StorageReadWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CloudRecordsSubmission",
            {TargetTouch{"CloudDensityRecords", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CloudShadowProjectSubmission",
            {TargetTouch{"CloudDensityRecords", TargetUsage::StorageRead},
             TargetTouch{"CloudShadowRecords", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CloudTraceSubmission",
            {TargetTouch{"CloudDensityRecords", TargetUsage::StorageRead},
             TargetTouch{"CloudShadowRecords", TargetUsage::StorageRead},
             TargetTouch{"SkyRadiance", TargetUsage::Sampled}, ReadTransmittance,
             TargetTouch{"CloudRadiance", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "CloudPresentRasterization",
            {TargetTouch{"CloudRadiance", TargetUsage::Sampled},
             TargetTouch{"EnvironmentRadiance", TargetUsage::ColourWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "FogMediumSubmission",
            {ReadDepth, TargetTouch{"EnvironmentRadiance", TargetUsage::StorageReadWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "PrecipitationComputeSubmission",
            {TargetTouch{"PrecipitationRecords", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "PrecipitationRasterization",
            {TargetTouch{"PrecipitationRecords", TargetUsage::StorageRead}, ReadDepth,
             TargetTouch{"EnvironmentRadiance", TargetUsage::StorageReadWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "FlareVisibilitySubmission",
            {TargetTouch{"SkyRadiance", TargetUsage::Sampled}, ReadDepth,
             TargetTouch{"FlareVisibility", TargetUsage::StorageWrite}},
            {},
        },
        SubmissionUnitDeclaration{
            "FlareImageRasterization",
            {TargetTouch{"FlareVisibility", TargetUsage::Sampled},
             TargetTouch{"EnvironmentRadiance", TargetUsage::StorageReadWrite}},
            {},
        },
    };

    Frame.Targets = {
        RenderTargetDeclaration{"TransmittanceLut", TargetFormat::Rgba16Float, 256, 64, true, TargetUsage::Sampled},
        RenderTargetDeclaration{"MultipleScatteringLut", TargetFormat::Rgba16Float, 32, 32, true, TargetUsage::Sampled},
        RenderTargetDeclaration{"SurfaceDepth", TargetFormat::Depth32Float, Width, Height, true, TargetUsage::DepthRead},
        RenderTargetDeclaration{"SkyRadiance", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"CloudDensityRecords", TargetFormat::R32Float, 512, 512, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"CloudShadowRecords", TargetFormat::R32Float, 256, 256, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"CloudRadiance", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"EnvironmentRadiance", TargetFormat::Rgba16Float, Width, Height, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"PrecipitationRecords", TargetFormat::Rg32Uint, 256, 256, false, TargetUsage::Undefined},
        RenderTargetDeclaration{"FlareVisibility", TargetFormat::R32Float, 16, 16, false, TargetUsage::Undefined},
    };

    return Frame;
}

} // namespace Frontier
