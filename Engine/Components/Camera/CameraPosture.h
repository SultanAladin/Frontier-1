/*============================================================================================================================================
                                                             CAMERAPOSTURE.H
============================================================================================================================================*/
// 🧩 Smoothed vs target spatial/planar posture, orthonormal camera basis, thin-lens optical telemetry, and Vulkan clip transform

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CameraPosture
{
    // Target command values (updated immediately on user input or mode directives)
    Mathematics::Axis3D TargetPivot            { 0.0f, 0.0f, 0.42f }; // [m]   - Desired orbit / follow focus anchor
    Mathematics::Axis3D TargetEyePosition      { 0.0f, -4.6f, 1.8f }; // [m]   - Desired free-fly / first-person eye position
    float               TargetYawRadians       = 0.88f;               // [rad] - Desired azimuth angle
    float               TargetPitchRadians     = 0.30f;               // [rad] - Desired elevation angle
    float               TargetRollRadians      = 0.0f;                // [rad] - Desired camera roll angle
    float               TargetDistanceMeters   = 5.80f;               // [m]   - Desired orbit distance from pivot
    Mathematics::UVAxes TargetPlanarCenter     { 0.50f, 0.50f };      // [uv]  - Desired 2D planar center
    float               TargetPlanarZoom       = 1.0f;                // [-]   - Desired 2D planar magnification
    float               TargetSplineParameter  = 0.0f;                // [0,1] - Normalized progress along SplineRail

    // Current smoothed readings (advanced each step via exponential decay / spring kinematics)
    Mathematics::Axis3D CurrentPivot           { 0.0f, 0.0f, 0.42f }; // [m]   - Smoothed orbit / follow focus anchor
    Mathematics::Axis3D CurrentEyePosition     { 0.0f, -4.6f, 1.8f }; // [m]   - Resolved world-space camera eye position
    Mathematics::Axis3D FollowPivotVelocity    { 0.0f, 0.0f, 0.0f };  // [m/s] - Critically-damped spring velocity
    float               CurrentYawRadians      = 0.88f;               // [rad] - Smoothed azimuth angle
    float               CurrentPitchRadians    = 0.30f;               // [rad] - Smoothed elevation angle
    float               CurrentRollRadians     = 0.0f;                // [rad] - Smoothed roll angle
    float               CurrentDistanceMeters  = 5.80f;               // [m]   - Smoothed & collision-clamped eye distance
    Mathematics::UVAxes CurrentPlanarCenter    { 0.50f, 0.50f };      // [uv]  - Smoothed 2D planar center
    float               CurrentPlanarZoom      = 1.0f;                // [-]   - Smoothed 2D planar magnification
    float               CurrentSplineParameter = 0.0f;                // [0,1] - Smoothed spline progress

    // Resolved orthonormal right-handed camera basis
    Mathematics::Axis3D RightAxis              { 1.0f, 0.0f, 0.0f };  // [-]   - Camera local +X right unit vector
    Mathematics::Axis3D UpAxis                 { 0.0f, 0.0f, 1.0f };  // [-]   - Camera local +Y up unit vector
    Mathematics::Axis3D ForwardAxis            { 0.0f, 1.0f, 0.0f };  // [-]   - Camera local forward viewing unit vector

    // Derived physical thin-lens telemetry
    float HorizontalFovDegrees     = 59.0f;   // [deg] - Horizontal field of view from sensor width & focal length
    float VerticalFovDegrees       = 42.0f;   // [deg] - Vertical field of view from sensor height & focal length
    float EntrancePupilMillimeters = 11.14f;  // [mm]  - Physical entrance pupil diameter (FocalLength / FStop)
    float HyperfocalMeters         = 12.0f;   // [m]   - Hyperfocal distance for 0.029mm CoC standard
    float NearSharpLimitMeters     = 3.3f;    // [m]   - Near acceptable sharpness plane
    float FarSharpLimitMeters      = 7.5f;    // [m]   - Far acceptable sharpness plane (capped at 1.0e5f)

    // Isolated Vulkan NDC View-Projection transform
    Mathematics::ClipTransform4x4 ViewClip {};
};

} // namespace Frontier::Components
