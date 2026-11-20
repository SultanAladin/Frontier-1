/*============================================================================================================================================
                                                         REVOLUTIONSWEEPPRIMITIVES.H
============================================================================================================================================*/
// 🧩 Parametric lathe revolution, linear planar extrusion, and twist-minimized Bishop parallel-transport profile sweeps

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LatheProfileWaypoint
{
    float RadialOffsetMeters = 0.5f;  // [m]  - Perpendicular radial distance from revolution axis (>= 0)
    float AxialHeightMeters  = 0.0f;  // [m]  - Signed offset along revolution axis
    bool  SharpRingCrease    = false; // [-]  - Marks generated circumferential ring edges with EnableSharpCrease
};

struct SweepSpineWaypoint
{
    Mathematics::Axis3D Coordinates      {};    // [m]   - 3D spine waypoint coordinate
    float               CrossSectionScale = 1.0f; // [-]   - Isotropic cross-section scale multiplier at this station
    float               RollAngleRadians  = 0.0f; // [rad] - Explicit axial roll offset relative to Bishop transport
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void AppendLatheRevolutionPrimitive(
    PolygonNetwork&                       TargetNetwork,
    Mathematics::Axis3D                   AxisOrigin,
    Mathematics::Axis3D                   AxisDirection,
    std::span<const LatheProfileWaypoint> ProfileWaypoints,
    uint32_t                              RadialSegments,
    float                                 SweepAngleRadians,
    bool                                  ClosePartialCaps,
    UVPatchBounds                         UVRegion,
    const FaceShadingAssignment&          Shading,
    uint16_t                              PriorityLayerOrdinal = 0u,
    uint16_t                              PartGroupOrdinal     = 0u);

void AppendLinearExtrusionPrimitive(
    PolygonNetwork&                      TargetNetwork,
    Mathematics::Axis3D                  SectionOrigin,
    Mathematics::Axis3D                  SectionRightAxis,
    Mathematics::Axis3D                  SectionUpAxis,
    std::span<const Mathematics::Axis2D> ClosedContourCCW,
    Mathematics::Axis3D                  ExtrusionOffset,
    uint32_t                             AxialDivisions,
    float                                TotalTwistRadians,
    float                                EndTaperScale,
    UVPatchBounds                        UVRegion,
    const FaceShadingAssignment&         Shading,
    uint16_t                             PriorityLayerOrdinal = 0u,
    uint16_t                             PartGroupOrdinal     = 0u);

void AppendProfileSweepPrimitive(
    PolygonNetwork&                      TargetNetwork,
    std::span<const Mathematics::Axis2D> ClosedCrossSectionCCW,
    std::span<const SweepSpineWaypoint>  SpineWaypoints,
    bool                                 ClosedSpineLoop,
    bool                                 CapOpenEnds,
    UVPatchBounds                        UVRegion,
    const FaceShadingAssignment&         Shading,
    uint16_t                             PriorityLayerOrdinal = 0u,
    uint16_t                             PartGroupOrdinal     = 0u);

} // namespace Frontier::GeometrySpace
