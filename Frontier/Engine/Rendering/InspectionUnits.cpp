/*==========================================================================================================================================
                                              ONE SHARED INSPECTION TARGET, ONE UNIT PER VIEW
                                     ------------------------------------------------------------------
                                          Debug rendering rides the normal declaration machinery.
==========================================================================================================================================*/

// 🧩 One shared Inspection target, one unit per view: debug rendering rides the normal declaration machinery.

#include "InspectionUnits.h"

#include <utility>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const char* InspectionViewText(InspectionView View) noexcept
{
    switch (View)
    {
        case InspectionView::Resolved:        return "Resolved";
        case InspectionView::VisibilityIds:   return "VisibilityIds";
        case InspectionView::GlassLayers:     return "GlassLayers";
        case InspectionView::HiZ:             return "HiZ";
        case InspectionView::LodLevels:       return "LodLevels";
        case InspectionView::ClusterFallback: return "ClusterFallback";
        case InspectionView::InspectionMarch: return "InspectionMarch";
    }
    return "Resolved";
}

SubmissionUnitDeclaration DeclareInspectionUnit(InspectionView View, std::string SourceTarget)
{
    SubmissionUnitDeclaration Unit;
    Unit.Name = "Inspect";
    Unit.Name += InspectionViewText(View);
    Unit.Touches = {
        TargetTouch{std::move(SourceTarget), TargetUsage::StorageRead},
        TargetTouch{"Inspection", TargetUsage::StorageWrite},
    };
    return Unit;
}

RenderTargetDeclaration DeclareInspectionTarget(uint32_t Width, uint32_t Height)
{
    return RenderTargetDeclaration{"Inspection", TargetFormat::Rgba8Unorm, Width, Height,
                                   false, TargetUsage::Undefined};
}

} // namespace Frontier
