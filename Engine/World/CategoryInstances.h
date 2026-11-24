/*============================================================================================================================================
                                                          CATEGORYINSTANCES.H
============================================================================================================================================*/
// 🧩 Dual-profile RAII category instance types (Editor vs Production) & tight SoA main-loop execution functions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/World/WorldInstanceRegister.h"

#include <cstdint>
#include <string_view>

namespace Frontier::World
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Creation parameters for SolarInstance
struct SolarInstanceParameters
{
    float AzimuthDegrees         = 145.0f; // [deg]   - Compass azimuth
    float ElevationDegrees       = 35.0f;  // [deg]   - Solar altitude above horizon
    float AngularDiameterDegrees = 0.533f; // [deg]   - Apparent disc diameter
    float IntensityMultiplier    = 1.0f;   // [x]     - Physical irradiance gain
    float DayCycleSpeedDegreesS  = 0.0f;   // [deg/s] - Continuous day-cycle rate
    bool  CycleAnimationEnabled  = false;  // [-]     - Step day cycle during ExecuteSolarLoop
    bool  VisibilityEnabled      = true;   // [-]     - Active solar render contribution
};

// Creation parameters for LunarInstance
struct LunarInstanceParameters
{
    uint32_t MoonSlotOrdinal        = 0u;     // [idx]   - Target atmosphere moon slot [0..5]
    float    RightAscensionDegrees  = 210.0f; // [deg]   - Orbital azimuth
    float    DeclinationDegrees     = 28.0f;  // [deg]   - Orbital elevation
    float    AngularDiameterDegrees = 0.518f; // [deg]   - Apparent lunar disc diameter
    float    PhaseAngleDegrees      = 45.0f;  // [deg]   - Illuminated phase angle
    float    OrbitalRateDegreesPerS = 0.0f;   // [deg/s] - Continuous orbital rate
    float    AlbedoReflectance      = 0.136f; // [0-1]   - Bond/geometric albedo
    bool     VisibilityEnabled      = true;   // [-]     - Active render visibility
};

// Creation parameters for AtmosphereInstance
struct AtmosphereInstanceParameters
{
    float RayleighScaleMeters    = 8000.0f; // [m] - Rayleigh scale height
    float MieScaleMeters         = 1200.0f; // [m] - Mie aerosol scale height
    float OzoneDensityMultiplier = 1.0f;    // [x] - Stratospheric ozone gain
    float ObserverAltitudeMeters = 120.0f;  // [m] - Observer elevation above sea level
    bool  VisibilityEnabled      = true;    // [-] - Active sky medium
};

// Creation parameters for CloudInstance
struct CloudInstanceParameters
{
    float BaseAltitudeMeters  = 1800.0f; // [m]   - Cloud base altitude
    float ThicknessMeters     = 1400.0f; // [m]   - Cloud vertical thickness
    float DensityFraction     = 0.45f;   // [0-1] - Cloud coverage fraction
    float WindSpeedMetersPerS = 8.0f;    // [m/s] - Horizontal wind speed
    bool  VisibilityEnabled   = true;    // [-]   - Active cloud deck
};

// Creation parameters for LuminaireInstance
struct LuminaireInstanceParameters
{
    uint8_t LuminaireFormOrdinal = 0u;     // [idx] - 0=Point, 1=Spot, 2=Area, 3=IES
    float   PeakLuminanceR       = 800.0f; // [cd]  - Linear red intensity
    float   PeakLuminanceG       = 740.0f; // [cd]  - Linear green intensity
    float   PeakLuminanceB       = 620.0f; // [cd]  - Linear blue intensity
    float   RangeMeters          = 18.0f;  // [m]   - Attenuation radius
    float   OuterConeDegrees     = 45.0f;  // [deg] - Outer spot cone angle
    bool    VisibilityEnabled    = true;   // [-]   - Active luminaire
};

// Creation parameters for SurfaceInstance
struct SurfaceInstanceParameters
{
    uint64_t GeometryDocumentHash64 = 0ull; // [hash] - FNV-1a hash of .GeometrySpace document
    uint64_t MaterialDocumentHash64 = 0ull; // [hash] - FNV-1a hash of .MaterialSpace document
    uint32_t PrimitiveShapeOrdinal  = 0u;   // [idx]  - Analytic primitive shape ordinal
    bool     CastShadows            = true; // [-]    - Project onto solar shadow cascades
    bool     VisibilityEnabled      = true; // [-]    - Active viewport render contribution
};

// Creation parameters for ParticleInstance
struct ParticleInstanceParameters
{
    uint64_t ParticleDocumentHash64 = 0ull;  // [hash]  - FNV-1a hash of .ParticleSpace document
    uint32_t MaximumParticleCount   = 4096u; // [count] - GPU particle capacity
    float    ReleaseRatePerSecond   = 120.0f;// [1/s]   - Continuous particle release rate
    bool     SimulationActive       = true;  // [-]     - Step during ExecuteParticleLoop
};

// Creation parameters for ForceFieldInstance
struct ForceFieldInstanceParameters
{
    uint8_t ForceModelOrdinal     = 0u;     // [idx]   - Analytical force model ordinal
    float   StrengthMetersPerS2   = 9.81f;  // [m/s^2] - Acceleration magnitude
    float   InfluenceRadiusMeters = 25.0f;  // [m]     - Spherical falloff radius
    float   DirectionAxisX        = 0.0f;   // [-]     - Force axis X
    float   DirectionAxisY        = 0.0f;   // [-]     - Force axis Y
    float   DirectionAxisZ        = -1.0f;  // [-]     - Force axis Z
    bool    FieldActive           = true;   // [-]     - Active field evaluation
};

// Creation parameters for TriggerInstance
struct TriggerInstanceParameters
{
    float HalfExtentMetersX = 2.0f; // [m] - Half-extent X
    float HalfExtentMetersY = 2.0f; // [m] - Half-extent Y
    float HalfExtentMetersZ = 2.0f; // [m] - Half-extent Z
    bool  SensorActive      = true; // [-] - Active overlap sensing
};

// Creation parameters for AcousticInstance
struct AcousticInstanceParameters
{
    uint64_t AcousticDocumentHash64 = 0ull;  // [hash] - FNV-1a hash of .Acoustic document
    float    GainScalar             = 1.0f;  // [x]    - Linear amplitude gain
    float    MaxDistanceMeters      = 60.0f; // [m]    - Maximum audible range
    bool     PlaybackActive         = true;  // [-]    - Active acoustic playback
};

// Creation parameters for CameraInstance
struct CameraInstanceParameters
{
    float FocalLengthMillimeters = 35.0f;    // [mm] - Lens focal length
    float SensorWidthMillimeters = 36.0f;    // [mm] - Full-frame sensor width
    float NearPlaneMeters        = 0.05f;    // [m]  - Near clip plane
    float FarPlaneMeters         = 25000.0f; // [m]  - Far clip plane
    bool  PrimaryViewActive      = true;     // [-]  - Primary render camera
};

// Per-frame telemetry returned by ExecuteWorldSimulationSequence
struct WorldLoopStepMetrics
{
    uint32_t EvaluatedSolarCount       = 0u; // [count] - Solar entries processed
    uint32_t EvaluatedLunarCount       = 0u; // [count] - Lunar entries processed
    uint32_t EvaluatedAtmosphereCount  = 0u; // [count] - Atmosphere entries processed
    uint32_t EvaluatedCloudCount       = 0u; // [count] - Cloud decks processed
    uint32_t EvaluatedLuminaireCount   = 0u; // [count] - Luminaires processed
    uint32_t EvaluatedForceFieldCount  = 0u; // [count] - Force fields processed
    uint32_t EvaluatedTriggerCount     = 0u; // [count] - Trigger zones evaluated
    uint32_t EvaluatedParticleCount    = 0u; // [count] - Particle clouds stepped
    uint32_t EvaluatedAcousticCount    = 0u; // [count] - Acoustic sources stepped
    uint32_t UploadedTransformCount    = 0u; // [count] - Rebuilt transforms flushed
};

//------------------------------------------------------------------------------------------------------------------------
//                                             DUAL-PROFILE CATEGORY INSTANCES
//------------------------------------------------------------------------------------------------------------------------

// Non-virtual RAII handle base (16 bytes total, zero vtable, zero heap allocation)
class CategoryInstanceHandle
{
public:
    CategoryInstanceHandle() = default;
    ~CategoryInstanceHandle() noexcept;

    CategoryInstanceHandle(const CategoryInstanceHandle&)            = delete;
    CategoryInstanceHandle& operator=(const CategoryInstanceHandle&) = delete;

    CategoryInstanceHandle(CategoryInstanceHandle&& Other) noexcept;
    CategoryInstanceHandle& operator=(CategoryInstanceHandle&& Other) noexcept;

    [[nodiscard]] InstanceToken QueryToken() const noexcept { return Token; }
    [[nodiscard]] bool          QueryValid() const noexcept
    {
        return (Register != nullptr) && Register->AuthenticateToken(Token);
    }

    void Release() noexcept;

protected:
    WorldInstanceRegister* Register = nullptr; // [-] - Owning register (non-owning pointer)
    InstanceToken          Token    {};        // [-] - Generation-checked slot identity
};

static_assert(sizeof(CategoryInstanceHandle) == 16u, "CategoryInstanceHandle must remain strictly 16 bytes.");

class SolarInstance final : public CategoryInstanceHandle
{
public:
    SolarInstance() = default;

    // Production constructor (zero Editor Outliner/Inspector overhead)
    SolarInstance(
        WorldInstanceRegister&         TargetRegister,
        const SolarInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    // Editor constructor (registers Outliner row + Inspector selection tag + removal protection)
    SolarInstance(
        WorldInstanceRegister&         TargetRegister,
        std::string_view               DisplayTitle,
        const SolarInstanceParameters& Parameters      = {},
        InstanceToken                  EnclosingToken  = {});
#endif
};

class LunarInstance final : public CategoryInstanceHandle
{
public:
    LunarInstance() = default;

    // Production constructor (zero Editor Outliner/Inspector overhead)
    LunarInstance(
        WorldInstanceRegister&         TargetRegister,
        const LunarInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    // Editor constructor (registers Outliner row + Inspector selection tag)
    LunarInstance(
        WorldInstanceRegister&         TargetRegister,
        std::string_view               DisplayTitle,
        const LunarInstanceParameters& Parameters      = {},
        InstanceToken                  EnclosingToken  = {});
#endif
};

class AtmosphereInstance final : public CategoryInstanceHandle
{
public:
    AtmosphereInstance() = default;

    AtmosphereInstance(
        WorldInstanceRegister&              TargetRegister,
        const AtmosphereInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    AtmosphereInstance(
        WorldInstanceRegister&              TargetRegister,
        std::string_view                    DisplayTitle,
        const AtmosphereInstanceParameters& Parameters     = {},
        InstanceToken                       EnclosingToken = {});
#endif
};

class CloudInstance final : public CategoryInstanceHandle
{
public:
    CloudInstance() = default;

    CloudInstance(
        WorldInstanceRegister&         TargetRegister,
        const CloudInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    CloudInstance(
        WorldInstanceRegister&         TargetRegister,
        std::string_view               DisplayTitle,
        const CloudInstanceParameters& Parameters     = {},
        InstanceToken                  EnclosingToken = {});
#endif
};

class LuminaireInstance final : public CategoryInstanceHandle
{
public:
    LuminaireInstance() = default;

    LuminaireInstance(
        WorldInstanceRegister&             TargetRegister,
        const LuminaireInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    LuminaireInstance(
        WorldInstanceRegister&             TargetRegister,
        std::string_view                   DisplayTitle,
        const LuminaireInstanceParameters& Parameters     = {},
        InstanceToken                      EnclosingToken = {});
#endif
};

class SurfaceInstance final : public CategoryInstanceHandle
{
public:
    SurfaceInstance() = default;

    SurfaceInstance(
        WorldInstanceRegister&           TargetRegister,
        const SurfaceInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    SurfaceInstance(
        WorldInstanceRegister&           TargetRegister,
        std::string_view                 DisplayTitle,
        const SurfaceInstanceParameters& Parameters     = {},
        InstanceToken                    EnclosingToken = {});
#endif
};

class ParticleInstance final : public CategoryInstanceHandle
{
public:
    ParticleInstance() = default;

    ParticleInstance(
        WorldInstanceRegister&            TargetRegister,
        const ParticleInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    ParticleInstance(
        WorldInstanceRegister&            TargetRegister,
        std::string_view                  DisplayTitle,
        const ParticleInstanceParameters& Parameters     = {},
        InstanceToken                     EnclosingToken = {});
#endif
};

class ForceFieldInstance final : public CategoryInstanceHandle
{
public:
    ForceFieldInstance() = default;

    ForceFieldInstance(
        WorldInstanceRegister&              TargetRegister,
        const ForceFieldInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    ForceFieldInstance(
        WorldInstanceRegister&              TargetRegister,
        std::string_view                    DisplayTitle,
        const ForceFieldInstanceParameters& Parameters     = {},
        InstanceToken                       EnclosingToken = {});
#endif
};

class TriggerInstance final : public CategoryInstanceHandle
{
public:
    TriggerInstance() = default;

    TriggerInstance(
        WorldInstanceRegister&           TargetRegister,
        const TriggerInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    TriggerInstance(
        WorldInstanceRegister&           TargetRegister,
        std::string_view                 DisplayTitle,
        const TriggerInstanceParameters& Parameters     = {},
        InstanceToken                    EnclosingToken = {});
#endif
};

class AcousticInstance final : public CategoryInstanceHandle
{
public:
    AcousticInstance() = default;

    AcousticInstance(
        WorldInstanceRegister&            TargetRegister,
        const AcousticInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    AcousticInstance(
        WorldInstanceRegister&            TargetRegister,
        std::string_view                  DisplayTitle,
        const AcousticInstanceParameters& Parameters     = {},
        InstanceToken                     EnclosingToken = {});
#endif
};

class CameraInstance final : public CategoryInstanceHandle
{
public:
    CameraInstance() = default;

    CameraInstance(
        WorldInstanceRegister&          TargetRegister,
        const CameraInstanceParameters& Parameters = {});

#if FRONTIER_PROFILE_EDITOR
    CameraInstance(
        WorldInstanceRegister&          TargetRegister,
        std::string_view                DisplayTitle,
        const CameraInstanceParameters& Parameters     = {},
        InstanceToken                   EnclosingToken = {});
#endif
};

//------------------------------------------------------------------------------------------------------------------------
//                                           TIGHT MAIN-LOOP EXECUTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

uint32_t ExecuteSolarLoop(SolarInstanceRegister& SolarRegister, float Δt) noexcept;
uint32_t ExecuteLunarLoop(LunarInstanceRegister& LunarRegister, float Δt) noexcept;
uint32_t ExecuteAtmosphereLoop(AtmosphereInstanceRegister& AtmosphereRegister, float Δt) noexcept;
uint32_t ExecuteCloudLoop(CloudInstanceRegister& CloudRegister, float Δt) noexcept;
uint32_t ExecuteLuminaireLoop(LuminaireInstanceRegister& LuminaireRegister, float Δt) noexcept;
uint32_t ExecuteForceFieldLoop(ForceFieldInstanceRegister& ForceFieldRegister, float Δt) noexcept;
uint32_t ExecuteTriggerLoop(WorldInstanceRegister& WorldRegister, float Δt) noexcept;
uint32_t ExecuteParticleLoop(ParticleInstanceRegister& ParticleRegister, float Δt) noexcept;
uint32_t ExecuteAcousticLoop(AcousticInstanceRegister& AcousticRegister, float Δt) noexcept;
uint32_t ExecuteTransformLoop(WorldInstanceRegister& WorldRegister) noexcept;

WorldLoopStepMetrics ExecuteWorldSimulationSequence(
    WorldInstanceRegister& WorldRegister,
    float                  Δt,
    bool                   ExecutionEnabled) noexcept;

} // namespace Frontier::World
