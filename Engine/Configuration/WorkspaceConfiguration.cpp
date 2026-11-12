/*============================================================================================================================================
                                                         WORKSPACECONFIGURATION.CPP
============================================================================================================================================*/
// 🧩 Implementation of WorkspaceConfiguration clamping UV workspace layout ratios, tile resolutions, and TransformGizmo snap increments

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/WorkspaceConfiguration.h"

#include <algorithm>

namespace Frontier::Configuration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONFIGURATION & QUERIES
//------------------------------------------------------------------------------------------------------------------------

void WorkspaceConfiguration::ConfigureUVPreferences(const UVWorkspacePreferences& Preferences) noexcept
{
    UVPreferences = Preferences;
    UVPreferences.DefaultTileResolutionPx = std::clamp(UVPreferences.DefaultTileResolutionPx, 256u, 16384u);
    UVPreferences.IslandMarginPixels      = std::min(UVPreferences.IslandMarginPixels, 256u);
    UVPreferences.SplitRatioNormalized    = std::clamp(UVPreferences.SplitRatioNormalized, 0.15f, 0.85f);
    UVPreferences.LeftOutlinerWidthPx     = std::clamp(UVPreferences.LeftOutlinerWidthPx, 220.0f, 560.0f);
    UVPreferences.RightInspectorWidthPx   = std::clamp(UVPreferences.RightInspectorWidthPx, 240.0f, 620.0f);
    UVPreferences.TargetTexelDensity      = std::clamp(UVPreferences.TargetTexelDensity, 1.0f, 16384.0f);
}

void WorkspaceConfiguration::ConfigureGizmoSnap(const GizmoSnapSettings& SnapSettings) noexcept
{
    GizmoSnap = SnapSettings;
    GizmoSnap.SpatialMoveStepMeters = std::max(0.001f, GizmoSnap.SpatialMoveStepMeters);
    GizmoSnap.UVMoveStepUnits       = std::max(0.0005f, GizmoSnap.UVMoveStepUnits);
    GizmoSnap.ScaleFactorStep       = std::max(0.01f, GizmoSnap.ScaleFactorStep);
    GizmoSnap.RotationStepDegrees   = std::clamp(GizmoSnap.RotationStepDegrees, 0.25f, 90.0f);
}

void WorkspaceConfiguration::ResetDefaults() noexcept
{
    UVPreferences = UVWorkspacePreferences{};
    GizmoSnap     = GizmoSnapSettings{};
}

const UVWorkspacePreferences& WorkspaceConfiguration::InspectUVPreferences() const noexcept
{
    return UVPreferences;
}

UVWorkspacePreferences& WorkspaceConfiguration::AccessUVPreferences() noexcept
{
    return UVPreferences;
}

const GizmoSnapSettings& WorkspaceConfiguration::InspectGizmoSnap() const noexcept
{
    return GizmoSnap;
}

GizmoSnapSettings& WorkspaceConfiguration::AccessGizmoSnap() noexcept
{
    return GizmoSnap;
}

} // namespace Frontier::Configuration
