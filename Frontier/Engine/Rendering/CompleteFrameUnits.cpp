/*==========================================================================================================================================
                                                   COMPLETE FRAME DEFINITION
==========================================================================================================================================*/

#include "CompleteFrameUnits.h"

#include "InspectionUnits.h"

#include <string>
#include <unordered_map>

namespace Frontier
{

namespace
{

void MergeTargets(std::vector<RenderTargetDeclaration>& Into, const std::vector<RenderTargetDeclaration>& From)
{
    std::unordered_map<std::string, bool> Seen;
    for (const RenderTargetDeclaration& Existing : Into)
    {
        Seen[Existing.Name] = true;
    }
    for (const RenderTargetDeclaration& Candidate : From)
    {
        if (Seen.find(Candidate.Name) == Seen.end())
        {
            Into.push_back(Candidate);
            Seen[Candidate.Name] = true;
        }
    }
}

} // namespace

CompleteFrameDeclaration DeclareCompleteFrame(uint32_t Width, uint32_t Height, CompleteFrameOptions Options)
{
    const IlluminationFrameDeclaration Illumination = DeclareIlluminationFrame(Width, Height);
    const EnvironmentFrameDeclaration  Environment  = DeclareEnvironmentFrame(Width, Height);

    CompleteFrameDeclaration Frame;

    // Surface first: everything downstream samples what it writes.
    Frame.Units.push_back(SubmissionUnitDeclaration{
        "SurfaceRaster",
        {TargetTouch{"SurfaceAttributes", TargetUsage::ColourWrite},
         TargetTouch{"SurfaceDepth", TargetUsage::DepthWrite}},
        {},
    });
    Frame.Units.insert(Frame.Units.end(), Illumination.Units.begin(), Illumination.Units.end());
    Frame.Units.insert(Frame.Units.end(), Environment.Units.begin(), Environment.Units.end());

    Frame.Units.push_back(SubmissionUnitDeclaration{
        "LightingHistoryUpdateSubmission",
        {TargetTouch{"FilteredLighting", TargetUsage::Sampled},
         TargetTouch{"LightingHistory", TargetUsage::StorageWrite}},
        {},
    });
    Frame.Units.push_back(SubmissionUnitDeclaration{
        "DisplayPresent",
        {TargetTouch{"FilteredLighting", TargetUsage::Sampled},
         TargetTouch{"EnvironmentRadiance", TargetUsage::Sampled},
         TargetTouch{"SurfaceDepth", TargetUsage::DepthRead},
         TargetTouch{"Swapchain", TargetUsage::ColourWrite},
         TargetTouch{"Swapchain", TargetUsage::Present}},
        {},
    });

    MergeTargets(Frame.Targets, Illumination.Targets);
    MergeTargets(Frame.Targets, Environment.Targets);
    Frame.Targets.push_back(
        RenderTargetDeclaration{"Swapchain", TargetFormat::Bgra8Unorm, Width, Height, true, TargetUsage::Present});

    if (Options.IncludeInspection && InspectionViewsEnabled())
    {
        Frame.Units.push_back(DeclareInspectionUnit(InspectionView::Resolved, "FilteredLighting"));
        Frame.Targets.push_back(DeclareInspectionTarget(Width, Height));
    }

    return Frame;
}

} // namespace Frontier
