/*============================================================================================================================================
                                                          CAMERACONFIGURATION.H
============================================================================================================================================*/
// 🧩 Physical sensor gate, thin-lens optics, kinematics, constraints, GPU visual effects, and non-Shipping diagnostics parameters

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <cstdint>
#include <vector>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SensorGatePreset : uint8_t
{
    Vista35mm   = 0u, // [mm] - 36.00 x 24.00 mm (3:2 VistaVision / 35mm Still Gate)
    Super35mm   = 1u, // [mm] - 24.89 x 18.66 mm (4:3 Academy Super-35)
    IMAX70mm    = 2u, // [mm] - 70.41 x 52.63 mm (15-perf 65/70mm)
    Cinema16mm  = 3u, // [mm] - 10.26 x  7.49 mm (Standard 16mm)
    CustomGate  = 4u, // [mm] - Explicit user-configured SensorWidthMillimeters x SensorHeightMillimeters
};

enum class CameraOperatingMode : uint8_t
{
    Orbit        = 0u, // [-] - Pivot-centered spherical turntable navigation
    Fly          = 1u, // [-] - Free 6-DOF spatial look + planar translation
    FirstPerson  = 2u, // [-] - Eye-anchored yaw/pitch navigation aligned to world up
    Pan2D        = 3u, // [-] - Aspect-locked 2D orthographic planar canvas pan & zoom
    FollowTarget = 4u, // [-] - Critically-damped spring pursuit behind a moving anchor
    SplineRail   = 5u, // [-] - Catmull-Rom spline rail path with look-ahead tangent
};

enum class CameraProjectionMode : uint8_t
{
    Perspective  = 0u, // [-] - Thin-lens pinhole / physical aperture perspective frustum
    Orthographic = 1u, // [-] - Parallel orthographic projection volume
};

enum class ApertureGhostShape : uint8_t
{
    Round   = 0u, // [-] - Circular open-iris reflection disc
    Hexagon = 6u, // [-] - 6-blade iris hexagonal internal reflection
    Octagon = 8u, // [-] - 8-blade iris octagonal internal reflection
};

enum class CompositionGuideStyle : uint8_t
{
    None               = 0u, // [-] - No composition overlay
    RuleOfThirds       = 1u, // [-] - 3x3 Rule-of-Thirds grid
    BroadcastSafeGate  = 2u, // [-] - 90% Title Safe & 95% Outer Safe broadcast gates
    CrosshairAndThirds = 3u, // [-] - Optical center reticle + Rule-of-Thirds grid
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OpticalLensParameters
{
    SensorGatePreset SensorGate              = SensorGatePreset::Vista35mm;    // [-]  - Physical film/sensor gate preset
    float            SensorWidthMillimeters  = 36.0f;                          // [mm] - Horizontal active photosite gate width
    float            SensorHeightMillimeters = 24.0f;                          // [mm] - Vertical active photosite gate height
    float            FocalLengthMillimeters  = 31.2f;                          // [mm] - Optical focal length (~42 deg vertical FOV on 24mm gate)
    float            ApertureFStop           = 2.8f;                           // [f/] - Relative aperture f-number (f/0.95 .. f/32)
    float            FocusDistanceMeters     = 4.6f;                           // [m]  - Subject plane in sharp focus
    float            NearClipMeters          = 0.08f;                          // [m]  - Near frustum clip plane
    float            FarClipMeters           = 500.0f;                         // [m]  - Far frustum clip plane
    float            OrthographicSpanMeters  = 3.6f;                           // [m]  - Vertical world span in Orthographic projection
};

struct CameraKinematicsParameters
{
    float OrbitSmoothingRate     = 26.0f;  // [1/s]   - Exponential convergence lambda for yaw/pitch (>99.9% settled < 300ms)
    float PanSmoothingRate       = 26.0f;  // [1/s]   - Exponential convergence lambda for pivot/eye translation
    float ZoomSmoothingRate      = 24.0f;  // [1/s]   - Exponential convergence lambda for log-space distance & 2D zoom
    float FollowSpringFrequency  = 9.5f;   // [rad/s] - Critically-damped spring natural frequency for FollowTarget
    float FlySpeedMetersPerSec   = 6.5f;   // [m/s]   - Standard free-fly translation speed
    float FlyBoostMultiplier     = 3.0f;   // [-]     - Sprint multiplier in Fly / FirstPerson modes
    float PitchLimitRadians      = 1.5184f;// [rad]   - Symmetric pole guard (+/- 87.0 deg)
    float MinDistanceMeters      = 0.20f;  // [m]     - Closest orbit / dolly distance
    float MaxDistanceMeters      = 250.0f; // [m]     - Farthest orbit / dolly distance
};

struct BoundsConstraintParameters
{
    bool                EnforceSpatialBounds = false;                       // [-]  - Clamp pivot/eye inside SpatialMinimum..SpatialMaximum
    Mathematics::Axis3D SpatialMinimum       { -250.0f, -250.0f, -50.0f };  // [m]  - Lower corner of allowed world volume
    Mathematics::Axis3D SpatialMaximum       {  250.0f,  250.0f, 250.0f };  // [m]  - Upper corner of allowed world volume
    bool                EnforcePlanarBounds  = true;                        // [-]  - Clamp 2D UV center inside PlanarMinimum..PlanarMaximum
    Mathematics::UVAxes PlanarMinimum        { -2.0f, -2.0f };              // [uv] - Lower 2D planar bound
    Mathematics::UVAxes PlanarMaximum        { 12.0f,  6.0f };              // [uv] - Upper 2D planar bound
};

struct CollisionObstacleSphere
{
    Mathematics::Axis3D Center { 0.0f, 0.0f, 0.0f }; // [m] - World-space obstacle sphere center
    float               Radius = 0.5f;               // [m] - World-space obstacle sphere radius
};

struct CollisionAvoidanceParameters
{
    bool                                 Enabled               = false; // [-] - Pull camera eye forward when occluded between pivot and desired eye
    float                                ClearanceRadiusMeters = 0.18f; // [m] - Swept camera clearance radius
    std::vector<CollisionObstacleSphere> Obstacles             {};      // [-] - Registered spherical occluders
};

struct LensFlareParameters
{
    bool               Enabled                = false;                        // [-]   - Master GPU lens-flare toggle
    bool               AnamorphicStreak       = true;                         // [-]   - Cool horizontal cinema streak
    bool               StarburstDiffraction   = true;                         // [-]   - Polygonal iris diffraction spikes
    bool               ApertureGhosts         = true;                         // [-]   - Multi-coated internal reflections
    ApertureGhostShape GhostShape             = ApertureGhostShape::Hexagon;  // [-]   - Iris blade geometry
    uint32_t           GhostReflectionCount   = 7u;                           // [cnt] - Number of internal ghost reflections (0..24)
    uint32_t           StarburstRayPairs      = 6u;                           // [cnt] - Diffraction spike pairs (2..12)
    float              SourceNormalizedU      = 0.28f;                        // [uv]  - Normalized horizontal light-source coordinate
    float              SourceNormalizedV      = 0.34f;                        // [uv]  - Normalized vertical light-source coordinate
    float              FlareIntensity         = 0.65f;                        // [-]   - Primary flare radiometric gain (0..2)
    float              FlareSpread            = 0.55f;                        // [-]   - Corona & streak spread factor (0..1.5)
    float              GhostIntensity         = 0.45f;                        // [-]   - Coated ghost reflection gain (0..2)
    float              GhostSpacing           = 0.65f;                        // [-]   - Axial spacing along optical center line
    float              StarburstRotationRad   = 0.22f;                        // [rad] - Diffraction spike rotation angle
};

struct ChromaticAberrationParameters
{
    bool  Enabled            = false; // [-] - Master radial dispersion toggle
    float DispersionStrength = 0.42f; // [-] - Edge R/B lateral chromatic shift
};

struct DepthOfFieldParameters
{
    bool  Enabled          = false; // [-]  - Master thin-lens Circle-of-Confusion blur toggle
    float MaxBokehRadiusPx = 10.0f; // [px] - Upper clamp on screen-space bokeh disc radius
};

struct VignetteParameters
{
    bool  Enabled   = false; // [-] - Master natural cos^4 optical falloff toggle
    float Intensity = 0.48f; // [-] - Edge darkening strength (0..1)
    float Roundness = 0.75f; // [-] - Aspect-ratio circularity blend (0..1)
};

#if !FRONTIER_PROFILE_SHIPPING
struct CameraDiagnosticsParameters
{
    bool                  ShowFrustumOutline = false;                       // [-] - Construct 8-corner / 12-edge frustum outline
    bool                  ShowFocusPlane     = false;                       // [-] - Highlight thin-lens focus plane in viewport
    CompositionGuideStyle CompositionGuide   = CompositionGuideStyle::None; // [-] - Rule-of-Thirds / Safe-Gate / Crosshair guide
};
#endif

struct CameraConfiguration
{
    CameraOperatingMode               OperatingMode       = CameraOperatingMode::Orbit;
    CameraProjectionMode              ProjectionMode      = CameraProjectionMode::Perspective;
    Mathematics::WorldCoordinateBasis WorldBasis          = Mathematics::WorldCoordinateBasis::RightHandedZUp;
    OpticalLensParameters             Optics              {};
    CameraKinematicsParameters        Kinematics          {};
    BoundsConstraintParameters        Bounds              {};
    CollisionAvoidanceParameters      Collision           {};
    LensFlareParameters               LensFlare           {};
    ChromaticAberrationParameters     ChromaticAberration {};
    DepthOfFieldParameters            DepthOfField        {};
    VignetteParameters                Vignette            {};
    std::vector<Mathematics::Axis3D>  SplineControlPoints {};
    Mathematics::Axis3D               FollowOffset        { 0.0f, -4.2f, 1.6f };
#if !FRONTIER_PROFILE_SHIPPING
    CameraDiagnosticsParameters       Diagnostics         {};
#endif
};

inline void ApplySensorGateDimensions(OpticalLensParameters& Lens) noexcept
{
    switch (Lens.SensorGate)
    {
        case SensorGatePreset::Vista35mm:
            Lens.SensorWidthMillimeters  = 36.00f;
            Lens.SensorHeightMillimeters = 24.00f;
            break;
        case SensorGatePreset::Super35mm:
            Lens.SensorWidthMillimeters  = 24.89f;
            Lens.SensorHeightMillimeters = 18.66f;
            break;
        case SensorGatePreset::IMAX70mm:
            Lens.SensorWidthMillimeters  = 70.41f;
            Lens.SensorHeightMillimeters = 52.63f;
            break;
        case SensorGatePreset::Cinema16mm:
            Lens.SensorWidthMillimeters  = 10.26f;
            Lens.SensorHeightMillimeters = 7.49f;
            break;
        case SensorGatePreset::CustomGate:
            break;
    }
}

} // namespace Frontier::Components
