/*============================================================================================================================================
                                                          ATMOSPHERICWINDSOLVER.H
============================================================================================================================================*/
// 🧩 Unified atmospheric wind solver computing procedural wind advection, Ekman veer/gusts, and eddy curl for Clouds & Fog

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WindCouplingSource : uint8_t
{
    GlobalWind     = 0u, // [-] - Master AtmosphericWindParameters
    CloudWind      = 1u, // [-] - Cloud deck Speed / Bearing / Shear
    LocalCloudWind = 2u, // [-] - Local cloud domain wind
    LocalFogWind   = 3u, // [-] - Local fog domain wind
    HeightFogWind  = 4u, // [-] - Exponential height fog boundary-layer drift
    AerialFogWind  = 5u, // [-] - Aerial perspective haze drift
};

enum class AtmosphericVolumeShape : uint8_t
{
    Box        = 0u, // [-] - Axis/yaw-oriented rectangular bounding prism
    Sphere     = 1u, // [-] - Spherical volume inscribed inside HalfExtent min radius
    Ellipsoid  = 2u, // [-] - Anisotropic 3D ellipsoid scaled by HalfExtentMeters
    Cylinder   = 3u, // [-] - Vertical finite cylinder along +Z
    Cone       = 4u, // [-] - Tapered vertical cone widening at base
    Diamond    = 5u, // [-] - Octahedral diamond volume (|x|/hx + |y|/hy + |z|/hz <= 1)
    Prism      = 6u, // [-] - Regular N-sided extruded prism (PrismSides = 3..16)
    HalfSphere = 7u, // [-] - Grounded hemispherical dome (+Z up) with smooth radial & base falloff
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphericWindParameters
{
    bool  Enabled               = true;   // [-]     - Master atmospheric wind toggle
    float SpeedMetersPerSecond  = 7.0f;   // [m/s]   - Prevailing horizontal wind speed [0..40]
    float BearingDegrees        = 250.0f; // [deg]   - Prevailing wind direction bearing [0..360)
    float ShearPerKilometer     = 0.35f;  // [/km]   - Vertical wind speed shear gradient [0..2]
    float VeerDegreesPerKm      = 12.0f;  // [d/km]  - Ekman spiral directional veer with altitude [-60..60]
    bool  AirShearAdvection     = false;  // [-]     - Enable high-altitude dynamic air shear boost
    float GustStrength          = 0.25f;  // [0..1]  - Peak periodic/turbulent gust amplitude
    float GustFrequencyHz       = 0.30f;  // [Hz]    - Gust oscillation cadence
    float Turbulence            = 0.30f;  // [0..1]  - Small-scale eddy domain warp intensity
    float Steadiness            = 1.00f;  // [0..1]  - 1.0 = steady laminar flow, 0.0 = highly gusty
};

struct ResolvedWindSample
{
    Mathematics::Axis3D VelocityMetersPerSec { 0.0f, 0.0f, 0.0f }; // [m/s] - Total 3D advection velocity
    float               CurlScalar           = 0.0f;               // [1/s] - Local vorticity magnitude |∇×F|
    float               TurbulenceScalar     = 0.0f;               // [0..2]- Effective turbulence intensity
};

//------------------------------------------------------------------------------------------------------------------------
//                                                ATMOSPHERIC WIND SOLVER
//------------------------------------------------------------------------------------------------------------------------

class AtmosphericWindSolver
{
public:
    // Evaluates analytical normalized distance mask [0..1] inside any of the 7 AtmosphericVolumeShapes
    [[nodiscard]] static float EvaluateVolumeShapeMask(
        const Mathematics::Axis3D&   SampleWorld,
        const Mathematics::Axis3D&   CenterWorld,
        const Mathematics::Scalar3D& HalfExtentMeters,
        AtmosphericVolumeShape       Shape,
        float                        YawDegrees,
        uint32_t                     PrismSides,
        float                        EdgeSoftness) noexcept;

    // Resolves wind velocity + curl at WorldPoint from authored AtmosphericWindParameters & altitude Ekman veer
    [[nodiscard]] static ResolvedWindSample ResolveWindSample(
        const AtmosphericWindParameters& GlobalWind,
        float                            DeckWindSpeedMps,
        float                            DeckWindBearingDeg,
        WindCouplingSource               Source,
        const Mathematics::Axis3D&       WorldPoint,
        float                            AltitudeMeters,
        float                            TimeSeconds) noexcept;

    // 4-step RK2 Semi-Lagrangian backward streamline advection through Wind + Ekman veer
    [[nodiscard]] static Mathematics::Axis3D AdvectWorldPointBackward(
        const AtmosphericWindParameters& GlobalWind,
        float                            DeckWindSpeedMps,
        float                            DeckWindBearingDeg,
        WindCouplingSource               Source,
        const Mathematics::Axis3D&       WorldPoint,
        float                            AltitudeMeters,
        float                            TotalSeconds,
        float*                           OutCurlScalar = nullptr) noexcept;
};

} // namespace Frontier::GeometricRaster
