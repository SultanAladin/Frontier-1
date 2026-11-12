/*============================================================================================================================================
                                                           ANALYTICPRIMITIVES.H
============================================================================================================================================*/
// 🧩 Parametric solid builders and 5 multi-part studio sample PolygonNetwork constructors with clean UDIM UVs and FacePartitionRegister

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SampleAssetPreset : uint8_t
{
    ControlledIntegrator = 0u, // [-] - Multi-part mechanical housing & rotor PolygonNetwork (UDIM 1001)
    AeroCoupeGT          = 1u, // [-] - Bodycoat hero, aero trim, canopy & 4 wheels (UDIM 1001 & 1002)
    TacticalCrate        = 2u, // [-] - Bevelled hard-case shell, latches & corner bumpers (UDIM 1001)
    ArmoredBust          = 3u, // [-] - Sculpted helm, visor & pauldron plates (UDIM 1001)
    UDIMArchKit          = 4u, // [-] - Plinth, fluted column, architrave & vault across UDIM 1001..1004
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct UVPatchBounds
{
    Mathematics::UVAxes Minimum { 0.02f, 0.02f }; // [uv] - Lower-left corner inside target UDIM tile
    Mathematics::UVAxes Maximum { 0.98f, 0.98f }; // [uv] - Upper-right corner inside target UDIM tile
};

struct ConstructedSampleAsset
{
    PolygonNetwork        Network    {};               // [-]    - Assembled half-edge PolygonNetwork
    FacePartitionRegister Layers     {};               // [-]    - Pre-configured PriorityAllocationLayer registry
    std::vector<uint32_t> UDIMTiles  { 1001u };        // [code] - Active UDIM tile codes
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void AppendBoxPrimitive(
    PolygonNetwork&              TargetNetwork,
    Mathematics::Axis3D          Center,
    Mathematics::Scalar3D        HalfExtents,
    UVPatchBounds                UVRegion,
    const FaceShadingAssignment& Shading,
    uint16_t                     PriorityLayerOrdinal = 0u,
    uint16_t                     PartGroupOrdinal     = 0u);

void AppendCylinderFrustumPrimitive(
    PolygonNetwork&              TargetNetwork,
    Mathematics::Axis3D          OriginCenter,
    Mathematics::Axis3D          TopCenter,
    float                        FootRadius,
    float                        TopRadius,
    uint32_t                     RadialSegments,
    UVPatchBounds                UVRegion,
    const FaceShadingAssignment& Shading,
    uint16_t                     PriorityLayerOrdinal = 0u,
    uint16_t                     PartGroupOrdinal     = 0u);

void AppendTorusRingPrimitive(
    PolygonNetwork&              TargetNetwork,
    Mathematics::Axis3D          Center,
    float                        MajorRadius,
    float                        MinorRadius,
    uint32_t                     MajorSegments,
    uint32_t                     MinorSegments,
    UVPatchBounds                UVRegion,
    const FaceShadingAssignment& Shading,
    uint16_t                     PriorityLayerOrdinal = 0u,
    uint16_t                     PartGroupOrdinal     = 0u);

[[nodiscard]] ConstructedSampleAsset ConstructSamplePolygonNetwork(SampleAssetPreset Preset);

} // namespace Frontier::GeometrySpace
