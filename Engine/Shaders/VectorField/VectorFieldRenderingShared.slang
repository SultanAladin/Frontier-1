/*============================================================================================================================================
                                                       VECTORFIELDRENDERINGSHARED.SLANG
============================================================================================================================================*/
// 🧩 Shared Editor-only uniforms, Reverse-Z +Z Up projections, and PickID constants for VectorField .slang rasterization

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Compute/ForceFieldEvaluationShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const uint PickCategoryNone          = 0u;
static const uint PickCategoryForceCircle   = 1u;
static const uint PickCategoryMotionWaypoint= 2u;
static const uint PickCategoryAnemometer    = 3u;
static const uint PickCategoryLitObject     = 4u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportOverlayUniforms
{
    float4   ViewProjRow0;               // [-]   - Row 0 of World -> Clip (+X Fwd, +Y Left, +Z Up, Reverse-Z [1->0])
    float4   ViewProjRow1;               // [-]   - Row 1 of World -> Clip
    float4   ViewProjRow2;               // [-]   - Row 2 of World -> Clip
    float4   ViewProjRow3;               // [-]   - Row 3 of World -> Clip
    float4   CameraPositionTime;      // xyz = CameraWorld [m], w = TimeSeconds [s]
    float4   CameraRightThickness;    // xyz = CameraRight, w = StreamRibbonThickness
    float4   CameraUpEmissive;        // xyz = CameraUp, w = StreamEmissiveBoost
    float4   CustomStreamColor;          // rgba = Custom stream tint [0..1]
    float4   CrossSectionPlaneParams;    // x = Axis(0=XY,1=XZ,2=YZ), y = OffsetM, z = HalfSpanM, w = HeatmapChannel
    float4   StreamVisualParams;         // x = ColorMode, y = Opacity, z = Resolution, w = LineCount
    uint4    ActiveSourceCounts;         // x = SourceCount, y = SelectedForceToken, z = SelectedWaypointIdx, w = LitShadingEnabled
    float4   AnemometerWorldActive;   // xyz = Anemometer [m], w = Active(0/1)
};

struct RenderInstancePushConstants
{
    uint ObjectIndex;     // [idx] - Source index, instance offset, or shape slot
    uint SubMode;         // [-]   - Plane axis / shape kind / path mode
    uint PickCategory;    // [-]   - PickID category tag
    uint PickPrimaryId;   // [id]  - Primary Entry/Force token
    uint PickSecondaryId; // [id]  - Waypoint / gizmo sub-index
    uint ReservedPad;
};

struct OverlayFragmentOutput
{
    float4 Color  : SV_Target0; // RGBA8 color target
    uint4  PickId : SV_Target1; // RGBA32UI PickIDs target
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 SHARED HELPER FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float4 VfProjectWorldToClip(ViewportOverlayUniforms U, float3 WorldPos)
{
    float4 Wp = float4(WorldPos, 1.0);
    return float4(
        dot(U.ViewProjRow0, Wp),
        dot(U.ViewProjRow1, Wp),
        dot(U.ViewProjRow2, Wp),
        dot(U.ViewProjRow3, Wp));
}

float3 VfElevationColor(float AltitudeZ)
{
    float T = saturate((AltitudeZ + 10.0) / 45.0);
    return lerp(float3(0.18, 0.44, 0.72), float3(0.95, 0.80, 0.32), T);
}
