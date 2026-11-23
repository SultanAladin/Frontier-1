/*==========================================================================================================================================
                                                              INSPECTION VIEWS
                                     ------------------------------------------------------------------
                             The demo's debug view set as editor-only units in the same submission declaration.
==========================================================================================================================================*/

// 🧩 Inspection views: the demo's debug view set as editor-only units in the same submission declaration.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// The demo's debug/inspection entrypoints (Phase 0 ShaderContract: MaterialResolve
// Debug* x5, ClusterDebugFallback Debug* x3, InspectionMarch MarchInspection), plus the
// plain resolved view. MatCap / CAD-edge previews are viewport shading modes, not
// inspection views — see ViewportShadingMode in ViewportUnits.h.
enum class InspectionView : uint8_t
{
    Resolved,         // DebugCopyResolved — the resolved frame, verbatim
    VisibilityIds,    // DebugVisibilityIds — stable hash colour per cluster-triangle
    GlassLayers,      // DebugGlassLayers — front/back refraction layer tells
    HiZ,              // DebugHiZView — reversed-Z pyramid
    LodLevels,        // DebugLodLevels — cluster DAG cut level per pixel
    ClusterFallback,  // ClusterDebugFallback — unavailable / failed / no-geometry states
    InspectionMarch   // MarchInspection — marched inspection volume
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Inspection exists only where editor machinery exists (plan §5): Shipping binaries
// contain no inspection units, targets or shaders. Callers append the unit only when
// this returns true; the check is compile-time constant per build profile.
[[nodiscard]] constexpr bool InspectionViewsEnabled() noexcept
{
    return BuildConfiguration::EditorMachineryEnabled;
}

[[nodiscard]] const char* InspectionViewText(InspectionView View) noexcept;

// One inspection unit: reads the view's source target, writes the shared Inspection
// target. Appended after DisplayPresent in the frame declaration; barriers and
// lifetimes derive like any other unit (the Inspection target is transient, so the
// atlas may alias it over anything already dead — debug views cost no steady memory).
[[nodiscard]] SubmissionUnitDeclaration DeclareInspectionUnit(InspectionView View, std::string SourceTarget);

[[nodiscard]] RenderTargetDeclaration DeclareInspectionTarget(uint32_t Width, uint32_t Height);

} // namespace Frontier
