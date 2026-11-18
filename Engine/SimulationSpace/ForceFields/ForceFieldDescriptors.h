/*============================================================================================================================================
                                                           FORCEFIELDDESCRIPTORS.H
============================================================================================================================================*/
// 🧩 Shared C++20 descriptors, 19 ForceModel laws, Right-Hand +Z Up math primitives, motion paths, and 16B-aligned GPU layouts

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t MaxForceMotionWaypoints = 16u;  // [count] - Maximum waypoints per ForceMotionSpecification
inline constexpr uint32_t MaxActiveForceSources   = 64u;  // [count] - Maximum simultaneous force sources per dispatch
inline constexpr uint32_t FormulaTextCapacity     = 256u; // [B]     - Fixed storage length for VectorFieldLang formulas

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ForceModel : uint32_t
{
    LinearWind           = 0u,  // [-] - Uniform directional aerodynamic flow
    Buoyancy             = 1u,  // [-] - Archimedes upward lift against medium density
    RadialBurst          = 2u,  // [-] - Expanding spherical shockwave shell
    Attractor            = 3u,  // [-] - Point radial inward pull with soft core
    Repulsor             = 4u,  // [-] - Point radial outward push
    AxisAttractor        = 5u,  // [-] - Cylindrical axis radial convergence
    Containment          = 6u,  // [-] - Soft restoring inward boundary pressure
    RotationalColumn     = 7u,  // [-] - Tangential circulation + helical swirl around axis
    Torque               = 8u,  // [-] - Pure angular vorticity torque field
    Drag                 = 9u,  // [-] - Velocity-proportional aerodynamic damping
    CurlNoise            = 10u, // [-] - Exact divergence-free curl of 3D potential field
    AtmosphericGust      = 11u, // [-] - Modulated turbulent wind gusts
    VorticityConfinement = 12u, // [-] - Amplified local curl preservation
    BrownianNoise        = 13u, // [-] - Stochastic multi-octave jitter field
    LorentzField         = 14u, // [-] - Charged velocity cross magnetic B vector force
    MagneticDipole       = 15u, // [-] - Magnetic dipole field (3(m·r̂)r̂ - m) / r³
    FlowStream           = 16u, // [-] - Configured directional profile (Current/Thermal/Jet/NavigationField/Wake)
    SplineForce          = 17u, // [-] - Tangential attraction and transport along waypoint path
    BoundaryAdhesion     = 18u, // [-] - Normal/tangential boundary adherence traction
    Count                = 19u, // [count] - Total analytical ForceModel laws
};

enum class FlowStreamProfile : uint32_t
{
    Current         = 0u, // [-] - River / ocean laminar channel current
    ThermalUpdraft  = 1u, // [-] - Rising convective thermal column (+Z Up)
    SlipstreamJet   = 2u, // [-] - High-velocity conical nozzle jet
    NavigationField = 3u, // [-] - Steered flow-channel guidance stream
    VesselWake      = 4u, // [-] - Trailing V-wake behind moving hulls/vehicles
};

enum class ForceDurationMode : uint32_t
{
    Continuous = 0u, // [-] - Steady time-invariant force contribution
    Modulated  = 1u, // [-] - Periodic harmonic modulation in time
    Impulse    = 2u, // [-] - Triggered decaying burst envelope
};

enum class ForceAttenuationMode : uint32_t
{
    SmoothStep    = 0u, // [-] - Cubic Hermite radial taper inside bounds
    Linear        = 1u, // [-] - Linear radial taper from centre to boundary
    InverseSquare = 2u, // [-] - Physical 1 / (1 + (r / r_core)^2) decay
    None          = 3u, // [-] - Constant strength inside reach envelope
};

enum class ForceDomainCategory : uint32_t
{
    VectorField     = 0u, // [-] - Analytical vector laws & electromagnetic fields
    StreamTransport = 1u, // [-] - Directed fluid streams, splines, & surface traction
    TurbulenceField = 2u, // [-] - Divergence-free curl, gusts, vorticity, & stochastic noise
};

enum class ReachEnvelopeCategory : uint32_t
{
    Sphere      = 0u, // [-] - Isotropic radial ball around Centre
    Column      = 1u, // [-] - Oriented finite cylinder along local Z axis
    OrientedBox = 2u, // [-] - Oriented rectangular volume defined by HalfExtents
    PlanarBox   = 3u, // [-] - Thin box slab aligned with local XY plane
};

enum class MotionPathTopology : uint32_t
{
    Static        = 0u, // [-] - Stationary at authored Centre
    LinearSpan    = 1u, // [-] - Straight geodesic travel A -> B
    CatmullSpline = 2u, // [-] - Smooth 3D Catmull-Rom curve across waypoints A..P..B
    HelicalOrbit  = 3u, // [-] - Circular or helical revolution around path centre
};

enum class MotionCycleBehavior : uint32_t
{
    PingPong = 0u, // [-] - Oscillate A -> B -> A continuously
    WrapLoop = 1u, // [-] - Periodic wrap A -> B, A -> B
    PlayOnce = 2u, // [-] - Single traversal A -> B clamping at endpoint
};

enum class StreamlineColorMode : uint32_t
{
    SpeedRamp    = 0u, // [-] - Spectral velocity ramp (teal -> cyan -> amber -> crimson)
    CyanSingle   = 1u, // [-] - Uniform vivid #68DCFF cyan lines
    CustomSingle = 2u, // [-] - User-selected RGBA tint
    Elevation    = 3u, // [-] - Colored by +Z altitude height
};

enum class CrossSectionPlaneAxis : uint32_t
{
    XYHorizontal = 0u, // [-] - Horizontal XY slice at Z = CrossSectionOffsetMeters
    XZLateral    = 1u, // [-] - Vertical XZ slice at Y = CrossSectionOffsetMeters
    YZFrontal    = 2u, // [-] - Vertical YZ slice at X = CrossSectionOffsetMeters
    AllThreeAxes = 3u, // [-] - Simultaneous orthogonal tri-planar cross-sections
};

enum class HeatmapChannelMode : uint32_t
{
    Speed      = 0u, // [-] - |Velocity| magnitude [m/s]
    ForceAccel = 1u, // [-] - |Acceleration| magnitude [m/s^2]
    Curl       = 2u, // [-] - |∇×F| vorticity intensity [1/s]
    Divergence = 3u, // [-] - ∇·F expansion/compression [1/s]
    Pressure   = 4u, // [-] - Dynamic pressure 0.5 ρ |v|^2 [Pa]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) SpatialVector3
{
    float X       = 0.0f; // [m] - Right-Hand +X Forward
    float Y       = 0.0f; // [m] - Right-Hand +Y Left
    float Z       = 0.0f; // [m] - Right-Hand +Z Up
    float Padding = 0.0f; // [-] - Explicit 16-byte SIMD alignment pad

    [[nodiscard]] constexpr SpatialVector3 operator+(const SpatialVector3& Rhs) const noexcept;
    [[nodiscard]] constexpr SpatialVector3 operator-(const SpatialVector3& Rhs) const noexcept;
    [[nodiscard]] constexpr SpatialVector3 operator*(float Scalar) const noexcept;
    [[nodiscard]] constexpr SpatialVector3 operator/(float Scalar) const noexcept;
    constexpr SpatialVector3&              operator+=(const SpatialVector3& Rhs) noexcept;
    constexpr SpatialVector3&              operator-=(const SpatialVector3& Rhs) noexcept;
    constexpr SpatialVector3&              operator*=(float Scalar) noexcept;
};

struct BasisVectors3
{
    SpatialVector3 Forward{ 1.0f, 0.0f, 0.0f }; // [-] - Local rotated +X axis
    SpatialVector3 Left   { 0.0f, 1.0f, 0.0f }; // [-] - Local rotated +Y axis
    SpatialVector3 Up     { 0.0f, 0.0f, 1.0f }; // [-] - Local rotated +Z axis
};

struct ForceMotionSpecification
{
    MotionPathTopology                       Topology                   = MotionPathTopology::Static;     // [-]   - Path geometry topology
    MotionCycleBehavior                      CycleBehavior              = MotionCycleBehavior::PingPong;  // [-]   - Time loop behavior
    float                                    SpeedMetresPerSecond       = 8.0f;                           // [m/s] - Traversal speed along arc
    float                                    PhaseOffset                = 0.0f;                           // [0-1] - Normalized starting phase offset
    float                                    OrbitRadiusMeters          = 18.0f;                          // [m]   - Helical orbit horizontal radius
    float                                    OrbitVerticalAmplitudeMeters = 4.0f;                         // [m]   - Vertical oscillation amplitude (+Z)
    float                                    OrbitTurns                 = 1.0f;                           // [-]   - Helical cycles per period
    bool                                     OrientAlongPathTangent     = true;                           // [-]   - Align Euler rotation with path velocity
    uint32_t                                 WaypointCount              = 0u;                             // [count]- Active waypoints [0..16]
    std::array<SpatialVector3, MaxForceMotionWaypoints> Waypoints       {};                               // [m]   - Path knots A..P..B
    float                                    NormalizedProgress         = 0.0f;                           // [0-1] - Most recently evaluated phase
    float                                    EvaluatedArcLengthMeters   = 0.0f;                           // [m]   - Cached arc length
};

struct WindFieldSubcomponent
{
    bool  DrivesAtmosphereAndClouds = true;    // [-] - Couple this ForceSource to clouds, fog & sky wind advection
    bool  ProjectToCloudDeck        = true;    // [-] - Project horizontal field vertically across cloud layer altitudes
    float CouplingScaleHeightMeters = 2800.0f; // [m] - Vertical atmospheric coupling scale height
    float CloudAdvectionGain        = 1.0f;    // [-] - Multiplier on ForceField -> cloud/sky advection velocity
    float TurbulenceSwirlGain       = 1.0f;    // [-] - Multiplier on vorticity curl injected into cloud/fog turbulence
};

struct ForceSourceRecordEntry
{
    uint32_t                 UniqueToken          = 0u;                                  // [id]    - Monotonic force identifier
    uint32_t                 OutlinerTokenId      = 0u;                                  // [id]    - Linked OutlinerPanel slot identifier
    std::string              Label                { "Linear Wind" };                     // [-]     - Authoring display title
    ForceModel               Model                = ForceModel::LinearWind;              // [-]     - Analytical law identifier
    ForceDomainCategory      Domain               = ForceDomainCategory::VectorField;    // [-]     - Governing subsystem domain
    FlowStreamProfile        StreamProfile        = FlowStreamProfile::Current;          // [-]     - Profile when Model == FlowStream
    ForceAttenuationMode     Attenuation          = ForceAttenuationMode::SmoothStep;    // [-]     - Radial distance decay
    ForceDurationMode        DurationMode         = ForceDurationMode::Continuous;       // [-]     - Temporal modulation envelope
    ReachEnvelopeCategory    Envelope             = ReachEnvelopeCategory::Sphere;       // [-]     - Spatial bounding shell shape

    SpatialVector3           CentreMeters         { 0.0f, 0.0f, 8.0f };                  // [m]     - Static / authored anchor (+Z Up)
    SpatialVector3           EvaluatedCentreMeters{ 0.0f, 0.0f, 8.0f };                  // [m]     - Runtime position after motion path
    SpatialVector3           EulerDegrees         { 0.0f, 0.0f, 0.0f };                  // [°]     - Intrinsic XYZ orientation
    SpatialVector3           HalfExtentsMeters    { 30.0f, 30.0f, 30.0f };               // [m]     - Oriented box half extents
    float                    RadiusMeters         = 30.0f;                               // [m]     - Sphere / column outer influence radius
    float                    Strength             = 18.0f;                               // [m/s]   - Primary velocity or acceleration amplitude
    float                    FalloffExponent      = 1.5f;                                // [-]     - Radial attenuation shaping exponent
    float                    CoreRadiusMeters     = 4.0f;                                // [m]     - Singularity regularization radius
    float                    FrequencyHertz       = 0.45f;                               // [Hz]    - Modulation / gust / pulse frequency
    float                    TurbulenceScaleMeters = 14.0f;                              // [m]     - Spatial noise wavelength
    uint32_t                 OctaveCount          = 3u;                                  // [count] - Fractal noise octaves [1..5]
    float                    SwirlRatio           = 0.55f;                               // [-]     - Helical tangential-to-inward blend
    float                    ImpulseTriggerTimeSec = -100.0f;                            // [s]     - Timestamp of most recent impulse trigger
    float                    ImpulseDurationSec   = 1.25f;                               // [s]     - Impulse envelope decay half-width
    float                    BuoyancyDensityRatio = 1.6f;                                // [-]     - Medium vs particle density multiplier
    float                    DragCoefficient      = 1.2f;                                // [1/s]   - Linear drag damping rate
    uint32_t                 Seed                 = 1337u;                               // [-]     - Deterministic PRNG / phase seed

    bool                     Enabled              = true;                                // [-]     - Runtime solver active toggle
    bool                     Visible              = true;                                // [-]     - Editor overlay visibility toggle
    bool                     Solo                 = false;                               // [-]     - Solo isolate toggle
    bool                     Locked               = false;                               // [-]     - Prevent transform edits
    bool                     DynamicMotion        = false;                               // [-]     - Active ForceMotionPath traversal toggle
    bool                     FormulaEnabled       = false;                               // [-]     - Evaluate FormulaSourceText override
    std::string              FormulaSourceText    {};                                    // [-]     - VectorFieldLang expression
    uint32_t                 CompiledFormulaSlot  = 0u;                                  // [idx]   - Bytecode slot index when compiled
    ForceMotionSpecification Motion               {};                                    // [-]     - Optional waypoint motion path
    WindFieldSubcomponent    WindField            {};                                    // [-]     - Atmosphere / Cloud / Fog wind coupling subcomponent
};

struct alignas(16) ForceSourceShaderEntry
{
    float    CentreX;           // [m]    - Evaluated world X
    float    CentreY;           // [m]    - Evaluated world Y
    float    CentreZ;           // [m]    - Evaluated world Z
    float    RadiusMeters;      // [m]    - Outer reach radius

    float    DirectionX;        // [-]    - Local forward axis X (+X unit vector rotated)
    float    DirectionY;        // [-]    - Local forward axis Y
    float    DirectionZ;        // [-]    - Local forward axis Z
    float    Strength;          // [m/s]  - Scaled & modulated effective magnitude

    float    TangentLeftX;      // [-]    - Local +Y Left axis X
    float    TangentLeftY;      // [-]    - Local +Y Left axis Y
    float    TangentLeftZ;      // [-]    - Local +Y Left axis Z
    float    FalloffExponent;   // [-]    - Attenuation power exponent

    float    UpX;               // [-]    - Local +Z Up axis X
    float    UpY;               // [-]    - Local +Z Up axis Y
    float    UpZ;               // [-]    - Local +Z Up axis Z
    float    CoreRadiusMeters;  // [m]    - Regularization inner radius

    float    HalfExtentX;       // [m]    - Box half-extent X
    float    HalfExtentY;       // [m]    - Box half-extent Y
    float    HalfExtentZ;       // [m]    - Box half-extent Z
    float    TurbulenceScaleM;  // [m]    - Spatial noise wavelength

    float    FrequencyHertz;    // [Hz]   - Temporal modulation rate
    float    SwirlRatio;        // [-]    - Tangential curl ratio
    float    BuoyancyRatio;     // [-]    - Buoyancy / density scalar
    float    DragCoefficient;   // [1/s]  - Linear drag damping rate

    uint32_t ModelId;           // [-]    - ForceModel enum ordinal
    uint32_t StreamProfileId;   // [-]    - FlowStreamProfile enum ordinal
    uint32_t AttenuationId;     // [-]    - ForceAttenuationMode enum ordinal
    uint32_t EnvelopeId;        // [-]    - ReachEnvelopeCategory enum ordinal

    uint32_t DurationId;        // [-]    - ForceDurationMode enum ordinal
    uint32_t OctaveCount;       // [-]    - Fractal noise octave count
    uint32_t Seed;              // [-]    - Deterministic seed
    uint32_t FlagMask;          // [mask] - Bit 0: Enabled, Bit 1: FormulaEnabled, Bit 2: DynamicMotion

    float    ImpulsePhase01;    // [0-1]  - Pre-evaluated burst wave progress
    float    TimeSeconds;       // [s]    - Simulation clock timestamp
    uint32_t WaypointStartIdx;  // [idx]  - Index into spline waypoint SSBO
    uint32_t WaypointCount;     // [count]- Waypoints belonging to this source
};

static_assert(sizeof(ForceSourceShaderEntry) == 144u, "ForceSourceShaderEntry must remain exactly 144 bytes (9x float4) to match .slang StructuredBuffer layout.");
static_assert(alignof(ForceSourceShaderEntry) == 16u, "ForceSourceShaderEntry must preserve 16-byte SIMD alignment.");
static_assert(offsetof(ForceSourceShaderEntry, ModelId) == 96u, "ForceSourceShaderEntry offset parity with .slang buffer.");

struct SpatialForceSample
{
    SpatialVector3 AdvectionVelocity {}; // [m/s]   - Target drift/flow velocity vector
    SpatialVector3 LinearAcceleration{}; // [m/s^2] - Direct Newtonian acceleration
    SpatialVector3 AngularTorque     {}; // [rad/s^2]- Rotational vorticity / torque vector
    float          DragDecayRate     = 0.0f; // [1/s] - Summed exponential linear drag coefficient
};

struct ForceDiagnosticsReading
{
    SpatialForceSample TotalForce         {};     // [-]     - Combined field sample at query coordinate
    SpatialVector3     CurlVector         {};     // [1/s]   - Finite-difference vorticity ∇×F
    float              CurlMagnitude      = 0.0f; // [1/s]   - |∇×F| vorticity intensity
    float              Divergence         = 0.0f; // [1/s]   - Finite-difference ∇·F
    float              DynamicPressurePa  = 0.0f; // [Pa]    - 0.5 * DryAirDensity * |v|^2
    uint32_t           BeaufortScale      = 0u;   // [0-12]  - Meteorological wind Beaufort index
    float              CompassAzimuthDeg  = 0.0f; // [°]     - Horizontal bearing in XY plane
    uint32_t           ActiveForceCount   = 0u;   // [count] - Contributing force sources at point
};

struct ForceOverlaySettings
{
    bool                  Enabled                  = true;                               // [-]   - Master Force Viz header toggle (renders in Lit mode)
    bool                  ShowHeatmap              = true;                               // [-]   - Cross-section color heatmap & isolines
    bool                  ShowVectors              = true;                               // [-]   - Instanced 3D directional arrows
    bool                  ShowStreamlines          = true;                               // [-]   - Volumetric RK4 advection ribbons
    bool                  ShowBounds               = true;                               // [-]   - Semi-transparent shaded influence volumes
    bool                  ShowMotionCurves         = true;                               // [-]   - 3D trajectory curves & A/P/B waypoint spheres
    CrossSectionPlaneAxis CrossSectionAxis         = CrossSectionPlaneAxis::XYHorizontal;// [-]   - Active cross-section orientation
    float                 CrossSectionOffsetMeters = 8.0f;                               // [m]   - Section plane coordinate offset
    HeatmapChannelMode    HeatmapChannel           = HeatmapChannelMode::Speed;          // [-]   - Displayed scalar field quantity
    StreamlineColorMode   StreamColorMode          = StreamlineColorMode::CyanSingle;    // [-]   - Streamline shading mode
    uint32_t              CustomStreamColorRgba    = 0x68DCFFFFu;                        // [-]   - Custom RGBA when CustomSingle selected
    float                 StreamRibbonThickness    = 0.62f;                              // [x]   - Ribbon half-width scale
    float                 StreamEmissiveBoost      = 1.0f;                               // [x]   - HDR boost over Lit surface shading
    float                 StreamOpacity            = 0.92f;                              // [0-1] - Ribbon alpha opacity
    uint32_t              StreamCurveResolution    = 40u;                                // [steps]- Integration subdivisions per streamline
    uint32_t              StreamLineCount          = 180u;                               // [count]- Simultaneous RK4 streamline ribbons
    bool                  CrossSectionAnemometerActive = false;                          // [-]   - PointSampleCursor enabled in viewport
    SpatialVector3        AnemometerWorldMeters    { 0.0f, 0.0f, 8.0f };                 // [m]   - World coordinate of PointSampleCursor
    uint32_t              HeatmapColorSchemeOrdinal = 0u;                                // [0-3] - 0=Engineering, 1=Turbo, 2=Plasma, 3=Diverging
    float                 HeatmapCeilingScale      = 34.0f;                              // [m/s] - Fixed color scale upper clamp
    float                 ArrowSpacingMeters       = 46.0f;                              // [m]   - Volumetric arrow lattice spacing
    float                 HeatmapOpacity           = 0.58f;                              // [0-1] - Cross-section heatmap alpha
    uint32_t              ArrowLayerCount          = 1u;                                 // [1-3] - Volumetric arrow vertical slabs
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename SourceType>
concept SpatialForceSampleSource = requires(
    const SourceType&     Source,
    const SpatialVector3& WorldPoint,
    const SpatialVector3& BodyVelocity,
    double                TimeSeconds)
{
    { Source.QueryForceFieldSample(WorldPoint, BodyVelocity, TimeSeconds) } noexcept -> std::same_as<SpatialForceSample>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr float          DotProduct(const SpatialVector3& A, const SpatialVector3& B) noexcept;
[[nodiscard]] constexpr SpatialVector3 CrossProduct(const SpatialVector3& A, const SpatialVector3& B) noexcept;
[[nodiscard]] inline float             VectorLengthSquared(const SpatialVector3& V) noexcept;
[[nodiscard]] inline float             VectorLength(const SpatialVector3& V) noexcept;
[[nodiscard]] inline SpatialVector3    NormalizeSafe(const SpatialVector3& V, const SpatialVector3& Fallback = { 1.0f, 0.0f, 0.0f }) noexcept;
[[nodiscard]] inline BasisVectors3     ConstructOrthonormalBasisFromEuler(const SpatialVector3& EulerDegrees) noexcept;
[[nodiscard]] inline SpatialVector3    ComputeEulerDegreesFromDirection(const SpatialVector3& Direction) noexcept;

[[nodiscard]] constexpr ForceDomainCategory QueryDomainForModel(ForceModel Model) noexcept;
[[nodiscard]] constexpr const char*         QueryForceModelDisplayName(ForceModel Model) noexcept;
[[nodiscard]] constexpr const char*         QueryFlowStreamDisplayName(FlowStreamProfile Profile) noexcept;
[[nodiscard]] ForceSourceShaderEntry        PackForceSourceForShader(
    const ForceSourceRecordEntry& Entry,
    double                        TimeSeconds,
    uint32_t                      WaypointStartIdx = 0u) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

constexpr SpatialVector3 SpatialVector3::operator+(const SpatialVector3& Rhs) const noexcept
{
    return { X + Rhs.X, Y + Rhs.Y, Z + Rhs.Z, 0.0f };
}

constexpr SpatialVector3 SpatialVector3::operator-(const SpatialVector3& Rhs) const noexcept
{
    return { X - Rhs.X, Y - Rhs.Y, Z - Rhs.Z, 0.0f };
}

constexpr SpatialVector3 SpatialVector3::operator*(float Scalar) const noexcept
{
    return { X * Scalar, Y * Scalar, Z * Scalar, 0.0f };
}

constexpr SpatialVector3 SpatialVector3::operator/(float Scalar) const noexcept
{
    const float Inv = (Scalar != 0.0f) ? (1.0f / Scalar) : 0.0f;
    return { X * Inv, Y * Inv, Z * Inv, 0.0f };
}

constexpr SpatialVector3& SpatialVector3::operator+=(const SpatialVector3& Rhs) noexcept
{
    X += Rhs.X;
    Y += Rhs.Y;
    Z += Rhs.Z;
    return *this;
}

constexpr SpatialVector3& SpatialVector3::operator-=(const SpatialVector3& Rhs) noexcept
{
    X -= Rhs.X;
    Y -= Rhs.Y;
    Z -= Rhs.Z;
    return *this;
}

constexpr SpatialVector3& SpatialVector3::operator*=(float Scalar) noexcept
{
    X *= Scalar;
    Y *= Scalar;
    Z *= Scalar;
    return *this;
}

constexpr float DotProduct(const SpatialVector3& A, const SpatialVector3& B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

constexpr SpatialVector3 CrossProduct(const SpatialVector3& A, const SpatialVector3& B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
        0.0f,
    };
}

inline float VectorLengthSquared(const SpatialVector3& V) noexcept
{
    return V.X * V.X + V.Y * V.Y + V.Z * V.Z;
}

inline float VectorLength(const SpatialVector3& V) noexcept
{
    return std::sqrt(VectorLengthSquared(V));
}

inline SpatialVector3 NormalizeSafe(const SpatialVector3& V, const SpatialVector3& Fallback) noexcept
{
    const float LenSq = VectorLengthSquared(V);
    if (LenSq <= 1.0e-12f)
    {
        return Fallback;
    }
    const float InvLen = 1.0f / std::sqrt(LenSq);
    return { V.X * InvLen, V.Y * InvLen, V.Z * InvLen, 0.0f };
}

inline BasisVectors3 ConstructOrthonormalBasisFromEuler(const SpatialVector3& EulerDegrees) noexcept
{
    const float Rx = EulerDegrees.X * Constants::DegToRadFloat;
    const float Ry = EulerDegrees.Y * Constants::DegToRadFloat;
    const float Rz = EulerDegrees.Z * Constants::DegToRadFloat;

    const float Cx = std::cos(Rx);
    const float Sx = std::sin(Rx);
    const float Cy = std::cos(Ry);
    const float Sy = std::sin(Ry);
    const float Cz = std::cos(Rz);
    const float Sz = std::sin(Rz);

    // Intrinsic Z-Y-X rotation matrix preserving Right-Hand +Z Up (+X Forward, +Y Left)
    BasisVectors3 Basis{};
    Basis.Forward = NormalizeSafe({ Cz * Cy, Sz * Cy, -Sy }, { 1.0f, 0.0f, 0.0f });
    Basis.Left    = NormalizeSafe({ Cz * Sy * Sx - Sz * Cx, Sz * Sy * Sx + Cz * Cx, Cy * Sx }, { 0.0f, 1.0f, 0.0f });
    Basis.Up      = NormalizeSafe(CrossProduct(Basis.Forward, Basis.Left), { 0.0f, 0.0f, 1.0f });
    return Basis;
}

inline SpatialVector3 ComputeEulerDegreesFromDirection(const SpatialVector3& Direction) noexcept
{
    const SpatialVector3 Dir   = NormalizeSafe(Direction, { 1.0f, 0.0f, 0.0f });
    const float          Yaw   = std::atan2(Dir.Y, Dir.X) * Constants::RadToDegFloat;
    const float          Horiz = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y);
    const float          Pitch = std::atan2(-Dir.Z, Horiz) * Constants::RadToDegFloat;
    return { 0.0f, Pitch, Yaw, 0.0f };
}

constexpr ForceDomainCategory QueryDomainForModel(ForceModel Model) noexcept
{
    switch (Model)
    {
        case ForceModel::CurlNoise:
        case ForceModel::AtmosphericGust:
        case ForceModel::VorticityConfinement:
        case ForceModel::BrownianNoise:
            return ForceDomainCategory::TurbulenceField;

        case ForceModel::FlowStream:
        case ForceModel::SplineForce:
        case ForceModel::BoundaryAdhesion:
            return ForceDomainCategory::StreamTransport;

        default:
            return ForceDomainCategory::VectorField;
    }
}

constexpr const char* QueryForceModelDisplayName(ForceModel Model) noexcept
{
    switch (Model)
    {
        case ForceModel::LinearWind:           return "Linear Wind";
        case ForceModel::Buoyancy:             return "Buoyancy";
        case ForceModel::RadialBurst:          return "Radial Burst";
        case ForceModel::Attractor:            return "Attractor";
        case ForceModel::Repulsor:             return "Repulsor";
        case ForceModel::AxisAttractor:        return "Axis Attractor";
        case ForceModel::Containment:          return "Containment";
        case ForceModel::RotationalColumn:     return "Rotational Column";
        case ForceModel::Torque:               return "Torque";
        case ForceModel::Drag:                 return "Drag";
        case ForceModel::CurlNoise:            return "Curl Noise";
        case ForceModel::AtmosphericGust:      return "Atmospheric Gust";
        case ForceModel::VorticityConfinement: return "Vorticity Confinement";
        case ForceModel::BrownianNoise:        return "Brownian Noise";
        case ForceModel::LorentzField:         return "Lorentz Field";
        case ForceModel::MagneticDipole:       return "Magnetic Dipole";
        case ForceModel::FlowStream:           return "FlowStream";
        case ForceModel::SplineForce:          return "Spline Force";
        case ForceModel::BoundaryAdhesion:     return "Boundary Adhesion";
        default:                               return "Linear Wind";
    }
}

constexpr const char* QueryFlowStreamDisplayName(FlowStreamProfile Profile) noexcept
{
    switch (Profile)
    {
        case FlowStreamProfile::Current:         return "Current";
        case FlowStreamProfile::ThermalUpdraft:  return "Thermal Updraft";
        case FlowStreamProfile::SlipstreamJet:   return "Slipstream Jet";
        case FlowStreamProfile::NavigationField: return "NavigationField";
        case FlowStreamProfile::VesselWake:      return "Vessel Wake";
        default:                                 return "Current";
    }
}

inline ForceSourceShaderEntry PackForceSourceForShader(
    const ForceSourceRecordEntry& Entry,
    double                        TimeSeconds,
    uint32_t                      WaypointStartIdx) noexcept
{
    const BasisVectors3 Basis = ConstructOrthonormalBasisFromEuler(Entry.EulerDegrees);
    const SpatialVector3 Pos  = Entry.DynamicMotion ? Entry.EvaluatedCentreMeters : Entry.CentreMeters;

    float EffectiveStrength = Entry.Strength;
    float ImpulsePhase      = 0.0f;

    if (Entry.DurationMode == ForceDurationMode::Modulated)
    {
        const float PhaseRad = Constants::TauFloat * Entry.FrequencyHertz * static_cast<float>(TimeSeconds);
        EffectiveStrength   *= 0.5f + 0.5f * std::sin(PhaseRad);
    }
    else if (Entry.DurationMode == ForceDurationMode::Impulse)
    {
        const float Elapsed = static_cast<float>(TimeSeconds) - Entry.ImpulseTriggerTimeSec;
        if (Elapsed >= 0.0f && Entry.ImpulseDurationSec > 1.0e-4f)
        {
            const float Norm = Elapsed / Entry.ImpulseDurationSec;
            ImpulsePhase     = std::clamp(Norm, 0.0f, 1.5f);
            EffectiveStrength *= std::exp(-2.8f * Norm);
        }
        else
        {
            EffectiveStrength = 0.0f;
            ImpulsePhase      = 0.0f;
        }
    }

    uint32_t BitMask = 0u;
    if (Entry.Enabled)
    {
        BitMask |= 1u;
    }
    if (Entry.FormulaEnabled)
    {
        BitMask |= 2u;
    }
    if (Entry.DynamicMotion)
    {
        BitMask |= 4u;
    }

    ForceSourceShaderEntry Packed{};
    Packed.CentreX           = Pos.X;
    Packed.CentreY           = Pos.Y;
    Packed.CentreZ           = Pos.Z;
    Packed.RadiusMeters      = std::max(0.25f, Entry.RadiusMeters);
    Packed.DirectionX        = Basis.Forward.X;
    Packed.DirectionY        = Basis.Forward.Y;
    Packed.DirectionZ        = Basis.Forward.Z;
    Packed.Strength          = EffectiveStrength;
    Packed.TangentLeftX      = Basis.Left.X;
    Packed.TangentLeftY      = Basis.Left.Y;
    Packed.TangentLeftZ      = Basis.Left.Z;
    Packed.FalloffExponent   = std::max(0.1f, Entry.FalloffExponent);
    Packed.UpX               = Basis.Up.X;
    Packed.UpY               = Basis.Up.Y;
    Packed.UpZ               = Basis.Up.Z;
    Packed.CoreRadiusMeters  = std::max(0.1f, Entry.CoreRadiusMeters);
    Packed.HalfExtentX       = std::max(0.25f, Entry.HalfExtentsMeters.X);
    Packed.HalfExtentY       = std::max(0.25f, Entry.HalfExtentsMeters.Y);
    Packed.HalfExtentZ       = std::max(0.25f, Entry.HalfExtentsMeters.Z);
    Packed.TurbulenceScaleM  = std::max(0.25f, Entry.TurbulenceScaleMeters);
    Packed.FrequencyHertz    = std::max(0.01f, Entry.FrequencyHertz);
    Packed.SwirlRatio        = Entry.SwirlRatio;
    Packed.BuoyancyRatio     = Entry.BuoyancyDensityRatio;
    Packed.DragCoefficient   = std::max(0.0f, Entry.DragCoefficient);
    Packed.ModelId           = static_cast<uint32_t>(Entry.Model);
    Packed.StreamProfileId   = static_cast<uint32_t>(Entry.StreamProfile);
    Packed.AttenuationId     = static_cast<uint32_t>(Entry.Attenuation);
    Packed.EnvelopeId        = static_cast<uint32_t>(Entry.Envelope);
    Packed.DurationId        = static_cast<uint32_t>(Entry.DurationMode);
    Packed.OctaveCount       = std::clamp(Entry.OctaveCount, 1u, 5u);
    Packed.Seed              = Entry.Seed;
    Packed.FlagMask          = BitMask;
    Packed.ImpulsePhase01    = ImpulsePhase;
    Packed.TimeSeconds       = static_cast<float>(TimeSeconds);
    Packed.WaypointStartIdx  = WaypointStartIdx;
    Packed.WaypointCount     = Entry.Motion.WaypointCount;
    return Packed;
}

} // namespace Frontier::SimulationSpace::ForceFields
