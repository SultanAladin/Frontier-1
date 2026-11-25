/*============================================================================================================================================
                                                          VEHICLESPECIFICATIONS.H
============================================================================================================================================*/
// 🧩 Immutable research-grade vehicle specifications, Pacejka MF6.1 compiled coefficients, forced induction, fuel & suspension architectures

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t MaxVehicleWheelCorners      = 4u;        // [count]      - Standard 4-corner wheel array (FL, FR, RL, RR)
inline constexpr uint32_t MaxVehicleGearRatios        = 8u;        // [count]      - Up to 8 forward ratios
inline constexpr uint32_t MaxVehicleCurveSamples      = 24u;       // [count]      - Stack-allocated lookup curve capacity
inline constexpr float    StandardAtmospherePa        = 101325.0f; // [Pa]         - Sea-level standard barometric pressure
inline constexpr float    StandardAtmosphereKPa       = 101.325f;  // [kPa]        - Sea-level standard pressure in kilopascals
inline constexpr float    StandardAmbientKelvin       = 293.15f;   // [K]          - 20 degC standard ambient temperature
inline constexpr float    VehicleRpmToRadPerSec       = static_cast<float>(Constants::Tau / 60.0); // [rad/(s*rpm)]
inline constexpr float    VehicleRadPerSecToRpm       = static_cast<float>(60.0 / Constants::Tau); // [rpm/(rad/s)]

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ForcedInductionCategory : uint8_t
{
    NaturallyAspirated = 0u,
    Turbocharged       = 1u,
    Supercharged       = 2u,
    Twincharged        = 3u,
};

enum class SuperchargerDriveCategory : uint8_t
{
    Roots       = 0u, // Positive displacement, flat low-end boost, lower adiabatic efficiency
    TwinScrew   = 1u, // Internal compression positive displacement, high mid-range torque
    Centrifugal = 2u, // Speed-squared compressor, linear-to-quadratic top-end boost
};

enum class SuspensionArchitectureCategory : uint8_t
{
    Telescopic      = 0u, // Vertical coilover with geometric roll-center scaling
    DoubleWishbone  = 1u, // Unequal-length A-arms: strong negative camber gain in bump, low roll center
    MacPhersonStrut = 2u, // Strut axis + lower control arm: low roll center, diminishing camber gain at deep bump
    MultiLink       = 3u, // 5-link virtual instant center: progressive negative camber + bump toe-in stability
    SolidAxle       = 4u, // Rigid live beam axle: coupled opposite-wheel camber & high roll center
    TrailingArm     = 5u, // Longitudinal arm: strong anti-squat, neutral-to-positive roll camber
    Manual          = 6u, // Direct user-authored roll center and camber gain
};

enum class SpringCurveCategory : uint8_t
{
    Linear      = 0u, // Constant rate F = k1 * x
    Progressive = 1u, // Rising cubic rate F = k1 * x + k3 * x^3
    Digressive  = 2u, // High initial rate softening at deep compression
    DualRate    = 3u, // Two-stage helper + main spring transition
};

enum class TransmissionArchitectureCategory : uint8_t
{
    Manual     = 0u,
    Sequential = 1u,
    DualClutch = 2u,
    Automatic  = 3u,
};

enum class ClutchActuationCategory : uint8_t
{
    ManualPedal       = 0u,
    AutomaticSlip     = 1u,
    DualClutchOverlap = 2u,
};

enum class DifferentialCategory : uint8_t
{
    Open            = 0u, // Equal torque split (50/50)
    LimitedSlip     = 1u, // Power/coast ramp locking clutch-pack LSD
    Locked          = 2u, // Rigid spool coupling
    TorqueVectoring = 3u, // Yaw-rate / steering-assisted active torque bias
};

enum class DrivetrainLayoutCategory : uint8_t
{
    RearWheelDrive  = 0u,
    FrontWheelDrive = 1u,
    AllWheelDrive   = 2u,
};

enum class TractionAssistCategory : uint8_t
{
    Off     = 0u,
    Comfort = 1u,
    Sport   = 2u,
    Track   = 3u,
};

enum class FrictionRegimeCategory : uint8_t
{
    Kinetic       = 0u, // Active Pacejka MF6.1 combined-slip traction
    StaticLateral = 1u, // Low-speed lateral stiction hold with kinetic longitudinal slip
    StaticFull    = 2u, // Full Karnopp slope-compensated static hold
};

enum class TirePresetCategory : uint8_t
{
    Gt3SlickDry         = 0u,
    StreetSuperSticky   = 1u,
    StreetSemiSlick     = 2u,
    OffroadRallyDirt    = 3u,
};

enum class WheelContactQueryMode : uint8_t
{
    SingleRay         = 0u, // Single vertical raycast from suspension hardpoint (fastest baseline)
    MultiRayArc       = 1u, // 5-ray circumferential lower tire arc (-28..+28 deg): smooth curb climb & ~2-3x faster than 3D sweep
    CylinderShapeCast = 2u, // Full Jolt 3D CylinderShape sweep via NarrowPhaseQuery::CastShape
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INLINE FAST MATH
//------------------------------------------------------------------------------------------------------------------------

struct CurveSamplePoint
{
    float X = 0.0f; // [unit] - Independent axis coordinate
    float Y = 0.0f; // [unit] - Dependent axis output
};

template <uint32_t Capacity = MaxVehicleCurveSamples>
struct FixedCurveBuffer
{
    std::array<CurveSamplePoint, Capacity> Samples {};
    uint32_t                               Count   = 0u;

    void Clear() noexcept { Count = 0u; }

    void Append(float X, float Y) noexcept
    {
        if (Count < Capacity)
        {
            Samples[Count++] = { X, Y };
        }
    }

    [[nodiscard]] float EvaluateLinear(float QueryX) const noexcept
    {
        if (Count == 0u)
        {
            return 0.0f;
        }
        if (Count == 1u || QueryX <= Samples[0u].X)
        {
            return Samples[0u].Y;
        }
        if (QueryX >= Samples[Count - 1u].X)
        {
            return Samples[Count - 1u].Y;
        }

        uint32_t Low  = 0u;
        uint32_t High = Count - 1u;
        while (High - Low > 1u)
        {
            const uint32_t Mid = (Low + High) >> 1u;
            if (QueryX < Samples[Mid].X)
            {
                High = Mid;
            }
            else
            {
                Low = Mid;
            }
        }
        const float Span = Samples[High].X - Samples[Low].X;
        const float T    = (Span > 1.0e-6f) ? ((QueryX - Samples[Low].X) / Span) : 0.0f;
        return Samples[Low].Y + (Samples[High].Y - Samples[Low].Y) * T;
    }
};

struct VehicleFastMath
{
    // Minimax rational arctangent approximation (max error < 0.00025 rad, branch-reduced for physics thread)
    [[nodiscard]] static constexpr float FastAtan(float X) noexcept
    {
        const float AbsX = (X < 0.0f) ? -X : X;
        if (AbsX <= 1.0f)
        {
            const float X2 = X * X;
            return X * (0.9998660f + X2 * (-0.3302995f + X2 * (0.1801410f + X2 * (-0.0851330f + X2 * 0.0208351f))));
        }
        const float Inv  = 1.0f / X;
        const float Inv2 = Inv * Inv;
        const float Core = Inv * (0.9998660f + Inv2 * (-0.3302995f + Inv2 * (0.1801410f + Inv2 * (-0.0851330f + Inv2 * 0.0208351f))));
        const float HalfPi = static_cast<float>(Constants::HalfPi);
        return (X > 0.0f) ? (HalfPi - Core) : (-HalfPi - Core);
    }

    // 7th-order Bhaskara/minimax sine on [-pi, pi]
    [[nodiscard]] static inline float FastSin(float AngleRad) noexcept
    {
        constexpr float TwoPi  = static_cast<float>(Constants::Tau);
        constexpr float Pi     = static_cast<float>(Constants::Pi);
        constexpr float HalfPi = static_cast<float>(Constants::HalfPi);

        float X = std::fmod(AngleRad + Pi, TwoPi);
        if (X < 0.0f)
        {
            X += TwoPi;
        }
        X -= Pi;
        if (X > HalfPi)
        {
            X = Pi - X;
        }
        else if (X < -HalfPi)
        {
            X = -Pi - X;
        }
        const float X2 = X * X;
        return X * (1.0f + X2 * (-0.16666667f + X2 * (0.00833333f - 0.00019841f * X2)));
    }

    [[nodiscard]] static inline float FastCos(float AngleRad) noexcept
    {
        return FastSin(AngleRad + static_cast<float>(Constants::HalfPi));
    }

    [[nodiscard]] static constexpr float SmoothUnitStep(float X) noexcept
    {
        const float T = std::clamp(X, 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                             ENGINE, OIL & FUEL SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

struct EngineOilSpecification
{
    float SumpCapacityLiters  = 5.5f;   // [L]          - Oil sump volume
    float DensityKgPerM3      = 870.0f; // [kg/m^3]     - Synthetic 5W-40 oil density
    float SpecificHeatJPerKgK = 2000.0f;// [J/(kg*K)]   - Oil heat capacity
    float OptimalTempK        = 368.15f;// [K]          - 95 degC optimal operating temperature
    float DamageTempK         = 413.15f;// [K]          - 140 degC breakdown threshold
    float ColdViscosityMult   = 1.45f;  // [-]          - Cold friction multiplier at 20 degC
};

struct EngineSpecification
{
    float IdleRpm                   = 700.0f;   // [rpm]        - Idle governor target
    float RedlineRpm                = 7200.0f;  // [rpm]        - Fuel-cut rev limiter
    float SoftCutBandRpm            = 200.0f;   // [rpm]        - Soft torque ramp-down below redline
    float EngineInertiaKgM2         = 0.34f;    // [kg*m^2]     - Crankshaft + flywheel inertia
    float EngineBrakingLinearNmS    = 0.08f;    // [N*m*s/rad]  - FMEP mechanical bearing/ring friction
    float EngineBrakingQuadNmS2     = 0.00012f; // [N*m*s^2/rad^2] - Pumping + windage losses
    float IdleFuelFlowKgPerSec      = 0.00035f; // [kg/s]       - Minimum combustion flow keeping idle alive
    float BaseThermalEfficiency     = 0.36f;    // [-]          - Stoichiometric thermal efficiency
    float BoostedThermalEfficiency  = 0.28f;    // [-]          - Enriched AFR efficiency under boost
    float EngineThermalMassJPerK    = 140000.0f;// [J/K]        - Block + coolant lumped thermal capacity
    float PassiveCoolingWPerK       = 55.0f;    // [W/K]        - Idle radiator convection
    float SpeedCoolingWPerKPerMps   = 18.0f;    // [W/(K*m/s)]  - Airflow-assisted radiator heat rejection
    float ThermostatOpenK           = 358.15f;  // [K]          - 85 degC thermostat opening
    float OverheatTempK             = 393.15f;  // [K]          - 120 degC thermal derating onset
    float MaxSafeTempK              = 413.15f;  // [K]          - 140 degC clamp

    FixedCurveBuffer<> TorqueCurveNm         {}; // [rpm -> N*m]   - Naturally-aspirated base torque curve
    FixedCurveBuffer<> ExhaustFlowKgPerSec   {}; // [rpm -> kg/s]  - Full-load exhaust mass flow
    FixedCurveBuffer<> ExhaustTempKelvin     {}; // [rpm -> K]     - Full-load turbine inlet gas temperature
};

struct FuelSpecification
{
    float DensityKgPerLiter    = 0.745f;     // [kg/L]  - Unleaded gasoline density
    float EnergyDensityJPerKg  = 43.5e6f;    // [J/kg]  - Lower heating value (43.5 MJ/kg)
    float OctaneRating         = 95.0f;      // [RON]   - Knock-resistance octane rating
    float StoichiometricAfr    = 14.7f;      // [-]     - Stoichiometric air-fuel mass ratio
    float FullBoostTargetAfr   = 11.8f;      // [-]     - Enriched air-fuel ratio under full boost
};

struct FuelDeliverySpecification
{
    float             TankCapacityLiters        = 68.0f; // [L]   - Full fuel tank capacity
    float             InitialFuelLiters         = 68.0f; // [L]   - Starting fuel volume
    float             WarningFraction           = 0.15f; // [-]   - Low-fuel warning threshold
    float             CriticalFraction          = 0.05f; // [-]   - Critical pickup threshold
    bool              CoupleFuelMassToChassis   = true;  // [-]   - Dynamically scale Jolt body mass as fuel burns
    bool              EnableDecelFuelCut        = true;  // [-]   - Cut injection during overrun coast above idle
    FuelSpecification Fuel                      {};      // [-]   - Fuel thermodynamic properties
};

//------------------------------------------------------------------------------------------------------------------------
//                                           FORCED INDUCTION SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

struct TurbochargerSpecification
{
    float TurboInertiaKgM2        = 0.00004f;   // [kg*m^2]         - Realistic twin-scroll rotor inertia
    float FrictionLinearNmS       = 1.5e-6f;    // [N*m*s/rad]      - Ball-bearing viscous drag (~240 W @ 120k rpm)
    float FrictionQuadNmS2        = 1.2e-10f;   // [N*m*s^2/rad^2]  - Windage drag (~240 W @ 120k rpm)
    float TurbineWheelRadiusM     = 0.027f;     // [m]              - Effective turbine rotor radius
    float TurbineEfficiency       = 0.72f;      // [-]              - Peak isentropic turbine efficiency
    float CompressorEfficiency    = 0.74f;      // [-]              - Peak isentropic compressor efficiency
    float MechanicalEfficiency    = 0.97f;      // [-]              - Bearing shaft mechanical efficiency
    float IntercoolerEfficiency   = 0.78f;      // [-]              - Charge-air heat rejection effectiveness
    float InternalHeatConductWK   = 35.0f;      // [W/K]            - Serrano turbine-to-compressor heat transfer
    float WastegateMaxBoostBaseBar= 1.20f;      // [bar]            - Max gauge boost on BaseTuneOctane
    float WastegateMaxBoostRaceBar= 1.65f;      // [bar]            - Max gauge boost on RaceTuneOctane
    float BaseTuneOctane          = 91.0f;      // [RON]            - Baseline fuel octane
    float RaceTuneOctane          = 100.0f;     // [RON]            - High-octane race tune
    float BovReleaseRateBarPerSec = 12.0f;      // [bar/s]          - Blow-off valve pressure bleed rate
    float MaxShaftRpm             = 185000.0f;  // [rpm]            - Overspeed ceiling

    FixedCurveBuffer<> BoostPressureCurveBar {}; // [turbo rpm -> gauge bar]
    FixedCurveBuffer<> TorqueMultiplierCurve {}; // [gauge bar -> multiplier]
};

struct SuperchargerSpecification
{
    SuperchargerDriveCategory DriveCategory      = SuperchargerDriveCategory::TwinScrew;
    float                     DriveRatio         = 2.8f;   // [ratio] - Crank-to-rotor pulley ratio
    float                     MaxBoostBaseBar    = 0.80f;  // [bar]   - Max gauge boost at base octane
    float                     MaxBoostRaceBar    = 1.15f;  // [bar]   - Max gauge boost at race octane
    float                     BaseTuneOctane     = 91.0f;  // [RON]
    float                     RaceTuneOctane     = 100.0f; // [RON]
    float                     CompressorEff      = 0.72f;  // [-]     - Adiabatic efficiency (Roots ~0.62, TwinScrew ~0.72, Centrifugal ~0.78)
    float                     IntercoolerEff     = 0.72f;  // [-]     - Aftercooler thermal effectiveness
    float                     BypassValveRate    = 10.0f;  // [1/s]   - Electronic bypass valve actuation speed
    float                     BypassDragFraction = 0.18f;  // [-]     - Remaining parasitic load when bypass is fully open
    float                     MaxChargerRpm      = 24000.0f;// [rpm]  - Maximum safe rotor speed

    FixedCurveBuffer<> BoostPressureCurveBar {}; // [charger rpm -> gauge bar]
    FixedCurveBuffer<> TorqueMultiplierCurve {}; // [gauge bar -> multiplier]
    FixedCurveBuffer<> ParasiticDragCurveNm  {}; // [charger rpm -> crank N*m]
};

//------------------------------------------------------------------------------------------------------------------------
//                                       CLUTCH, TRANSMISSION, DIFFERENTIAL & BRAKES
//------------------------------------------------------------------------------------------------------------------------

struct ClutchSpecification
{
    ClutchActuationCategory Actuation             = ClutchActuationCategory::DualClutchOverlap;
    float                   MaxTorqueCapacityNm   = 1500.0f; // [N*m]
    float                   EngagementSeconds     = 0.035f;  // [s]
    float                   DisengagementSeconds  = 0.025f;  // [s]
    float                   BreakawaySlipRpm      = 280.0f;  // [rpm]
    float                   LockSlipRpm           = 45.0f;   // [rpm]
    float                   LaunchBiteEngagement  = 0.24f;   // [-]
    float                   LaunchLockRpmDelta    = 1400.0f; // [rpm above idle]
    float                   LaunchSpeedThresholdMps = 5.5f;  // [m/s]
    float                   LaunchEngagementRate  = 18.0f;   // [1/s]
    float                   CruiseEngagementRate  = 26.0f;   // [1/s]
};

struct TransmissionSpecification
{
    TransmissionArchitectureCategory Architecture     = TransmissionArchitectureCategory::DualClutch;
    bool                             AutomaticShifting= true;
    uint32_t                         ForwardGearCount = 6u;
    std::array<float, MaxVehicleGearRatios> ForwardRatios { 4.20f, 3.10f, 2.50f, 1.90f, 1.50f, 1.20f, 0.95f, 0.80f };
    float                            ReverseRatio     = -3.40f; // [-]  - Signed negative ratio
    float                            FinalDriveRatio  = 3.55f;  // [-]
    float                            DrivetrainEfficiency = 0.91f; // [-]
    float                            ShiftDurationSec = 0.09f;  // [s]  - DCT shift cut duration
    float                            ShiftCooldownSec = 0.30f;  // [s]  - Hysteresis between auto shifts
    float                            UpshiftRpmFrac   = 0.92f;  // [-]  - Upshift threshold fraction of redline
    float                            DownshiftRpmFrac = 0.42f;  // [-]  - Downshift threshold fraction of redline
    float                            CornerHoldLatG   = 0.72f;  // [g]  - Suppress upshift during high-G cornering
};

struct DifferentialSpecification
{
    DifferentialCategory Category            = DifferentialCategory::LimitedSlip;
    float                BiasSplit           = 0.50f;  // [-]     - Front/Left nominal split (Center: front bias)
    float                PreloadNm           = 45.0f;  // [N*m]   - Static clutch-pack preload
    float                PowerLockFactor     = 0.45f;  // [-]     - Acceleration ramp locking strength (0..1)
    float                CoastLockFactor     = 0.25f;  // [-]     - Deceleration ramp locking strength (0..1)
    float                ViscousDampingNmS   = 25.0f;  // [N*m*s/rad] - Speed-difference viscous locking
    float                TorqueVectorGain    = 0.35f;  // [-]     - Active yaw-steering torque vectoring authority
    float                SlipDeadbandRadS    = 0.15f;  // [rad/s] - Prevents asymmetric straight-line pull
};

struct BrakingSpecification
{
    float DiskOuterRadiusM      = 0.185f;   // [m]
    float DiskInnerRadiusM      = 0.105f;   // [m]
    uint32_t PistonCount        = 6u;       // [count]
    float PistonDiameterM       = 0.042f;   // [m]
    float FrictionCoeffCold     = 0.44f;    // [-]
    float FrictionCoeffHot      = 0.36f;    // [-]
    float FadeStartTempK        = 573.15f;  // [K]  - 300 degC
    float FadeEndTempK          = 923.15f;  // [K]  - 650 degC
    float MaxFrontPressurePa    = 12.0e6f;  // [Pa] - 120 bar
    float MaxRearPressurePa     = 8.5e6f;   // [Pa] - 85 bar
    float MaxHandbrakePressurePa= 10.5e6f;  // [Pa] - 105 bar (rear axle)
    float PressureRiseRatePaS   = 65.0e6f;  // [Pa/s]
    float PressureFallRatePaS   = 90.0e6f;  // [Pa/s]
    float RotorMassKg           = 7.2f;     // [kg]
    float SpecificHeatJPerKgK   = 460.0f;   // [J/(kg*K)]
    float ConvectionWPerM2K     = 90.0f;    // [W/(m^2*K)]
    float RotorSurfaceAreaM2    = 0.11f;    // [m^2]
};

//------------------------------------------------------------------------------------------------------------------------
//                                     SUSPENSION ARCHITECTURES, ARB & STEERING RACK
//------------------------------------------------------------------------------------------------------------------------

struct ProgressiveSpringCoefficients
{
    float K1 = 35000.0f; // [N/m]
    float K2 = 0.0f;     // [N/m^2]
    float K3 = 0.0f;     // [N/m^3]
};

struct SuspensionCornerSpecification
{
    SuspensionArchitectureCategory Architecture       = SuspensionArchitectureCategory::DoubleWishbone;
    SpringCurveCategory            CurveCategory      = SpringCurveCategory::Progressive;
    float                          RestLengthMeters   = 0.42f;   // [m]      - Unloaded strut/spring length
    float                          MaxBumpMeters      = 0.11f;   // [m]      - Upward jounce travel from ride height
    float                          MaxDroopMeters     = 0.10f;   // [m]      - Downward rebound travel from ride height
    float                          NaturalFrequencyHz = 2.35f;   // [Hz]     - Target sprung natural frequency
    float                          DampingRatioBump   = 0.55f;   // [-]      - Compression damping ratio zeta
    float                          DampingRatioRebound= 0.72f;   // [-]      - Rebound damping ratio zeta (stiffer rebound)
    float                          FastBumpMultiplier = 0.65f;   // [-]      - High-speed digressive blow-off slope (>0.3 m/s)
    float                          HardeningFactor    = 1.35f;   // [-]      - Progressive rate multiplier near full bump
    float                          BumpStopStiffness  = 180000.0f;// [N/m]   - Progressive rubber bump stop stiffness
    float                          BumpStopEngagement = 0.82f;   // [-]      - Compression fraction where bump stop engages
    float                          MotionRatio        = 0.88f;   // [-]      - Wheel-to-spring leverage ratio (effective k = k_spring * MR^2)
    float                          StaticCamberRad    = -0.022f; // [rad]    - ~ -1.25 deg static negative camber
    float                          StaticToeRad       = 0.002f;  // [rad]    - Slight stability toe
    float                          RollCenterHeightM  = 0.09f;   // [m]      - Geometric roll center height above ground
    float                          AntiDiveSquatFrac  = 0.22f;   // [-]      - Geometric pitch-resistance fraction (0..0.6)
    float                          CamberGainRadPerM  = -0.048f; // [rad/m]  - Camber change per meter of wheel jounce
    float                          BumpToeGainRadPerM = 0.010f;  // [rad/m]  - Bump-steer toe-in stabilization (MultiLink)
    float                          TelescopicScale    = 0.20f;   // [-]      - Roll-center scale for Telescopic mode

    // Derived during Rig initialization (zero per-step cost)
    float                          DerivedSpringRateNPerM     = 35000.0f; // [N/m]
    float                          DerivedBumpDamperNsPerM    = 3200.0f;  // [N*s/m]
    float                          DerivedReboundDamperNsPerM = 4200.0f;  // [N*s/m]
    ProgressiveSpringCoefficients  CubicCoeffs                {};
};

struct AntiRollbarSpecification
{
    bool  Enabled       = true;
    float StiffnessNPerM= 38000.0f; // [N/m]   - Differential wheel-travel force rate F = K * (xL - xR)
    float DampingNsPerM = 900.0f;   // [N*s/m] - Roll velocity damping
};

struct SteeringRackSpecification
{
    float MaxSteerAngleRad          = 0.56f;   // [rad]  - ~32 deg maximum inner road-wheel steer
    float AckermannBlend            = 0.85f;   // [-]    - 0 = parallel, 1 = pure kinematic Ackermann
    float RackRateRadPerSec         = 4.8f;    // [rad/s]- Column actuation slew rate
    bool  EnableVariableRatio       = true;
    float VariableRatioStartMps     = 10.0f;   // [m/s]  - ~36 km/h
    float VariableRatioMaxMps       = 55.0f;   // [m/s]  - ~198 km/h
    float VariableRatioMinScale     = 0.36f;   // [-]    - High-speed steering authority scale
    bool  EnableStabilityLimiter    = true;
    float CriticalBodySlipRad       = 0.19f;   // [rad]  - ~11 deg body slip threshold
    float StabilityMinAuthority     = 0.40f;   // [-]    - Minimum counter-steer authority floor
};

//------------------------------------------------------------------------------------------------------------------------
//                                           PACEJKA MF6.1 TIRE SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct PacejkaCompiledCoefficients
{
    float InvFz0kN    = 1.0f / 6.8f;
    float CxScaled    = 1.78f;
    float CyScaled    = 1.72f;
    float PDx1Scaled  = 1.95f;
    float PDy1Scaled  = 1.90f;
    float InvInertia  = 1.0f / 0.55f;
    float InvRadius   = 1.0f / 0.35f;
};

struct PacejkaTireSpecification
{
    // Geometry & inertia
    float OuterRadiusMeters         = 0.50855f; // [m]
    float WidthMeters               = 0.315f;   // [m]
    float WheelMassKg               = 21.0f;    // [kg]
    float WheelInertiaKgM2          = 1.45f;    // [kg*m^2]

    // Rolling resistance & transient relaxation
    float RollingResistBase         = 0.011f;   // [-]
    float RollingResistSpeedFactor  = 0.00012f; // [s/m]
    float RelaxationLongMeters      = 0.085f;   // [m]
    float RelaxationLatMeters       = 0.078f;   // [m]
    float RelaxationLoadExponent    = 0.35f;    // [-]

    // Global Pacejka MF6.1 scaling
    float NominalLoadFz0kN          = 6.8f;     // [kN]
    float ScaleLx                   = 1.00f;
    float ScaleLy                   = 1.00f;
    float ScaleLtr                  = 1.00f;
    float LoadSensitivityLong       = 0.12f;    // [1/kN]
    float LoadSensitivityLat        = 0.15f;    // [1/kN]
    float LoadSensitivityMinFactor  = 0.58f;    // [-]

    // Longitudinal (Fx) MF6.1
    float PBx1 = 19.5f;  float PBx2 = 20.0f; float PBx3 = 0.40f;
    float PCx1 = 1.74f;
    float PDx1 = 1.72f;  float PDx2 = -0.06f; float PDx3 = 0.028f;
    float PEx1 = -0.14f; float PEx2 = -0.18f; float PEx3 = 0.008f; float PEx4 = 0.032f;

    // Lateral (Fy) MF6.1
    float PBy1 = 17.5f;  float PBy2 = 14.0f; float PBy3 = 0.09f;
    float PCy1 = 1.66f;
    float PDy1 = 1.78f;  float PDy2 = -0.10f; float PDy3 = 0.16f;
    float PEy1 = -1.25f; float PEy2 = -0.45f; float PEy3 = 0.18f;

    // Camber thrust & combined slip weighting (MF6.1 Gx_alpha, Gy_kappa)
    float CamberThrustGain          = 0.14f;    // [1/rad]
    float RBx1                      = 12.5f;    // Combined Fx weighting slope
    float RBx2                      = 10.5f;    // Combined Fx kappa shift
    float RCx1                      = 1.06f;    // Combined Fx shape
    float RBy1                      = 10.2f;    // Combined Fy weighting slope
    float RBy2                      = 8.5f;     // Combined Fy alpha shift
    float RCy1                      = 1.05f;    // Combined Fy shape
    float PneumaticTrailMeters      = 0.038f;   // [m] - Nominal self-aligning lever arm

    // Thermal & wear dynamics
    float TreadThermalMassJPerK     = 410.0f;   // [J/K]
    float CarcassThermalMassJPerK   = 760.0f;   // [J/K]
    float TreadToCarcassWPerK       = 18.0f;    // [W/K]
    float SurfaceConvectionWPerM2K  = 46.0f;    // [W/(m^2*K)]
    float OptimalTempK              = 358.15f;  // [K]  - 85 degC peak compound grip
    float OverheatTempK             = 403.15f;  // [K]  - 130 degC thermal degradation
    float ColdGripFloor             = 0.84f;    // [-]  - Minimum grip when cold (20 degC)
    float InitialTreadDepthMm       = 5.5f;     // [mm]
    float WearRateMmPerMJ           = 0.0014f;  // [mm/MJ]
    float GripLossPerMmWear         = 0.045f;   // [-/mm]

    // Precomputed coefficients (populated once by CompileCoefficients)
    PacejkaCompiledCoefficients Compiled {};

    void CompileCoefficients() noexcept
    {
        Compiled.InvFz0kN   = 1.0f / std::max(NominalLoadFz0kN, 0.25f);
        Compiled.CxScaled   = std::max(PCx1, 0.5f);
        Compiled.CyScaled   = std::max(PCy1, 0.5f);
        Compiled.PDx1Scaled = PDx1 * ScaleLx;
        Compiled.PDy1Scaled = PDy1 * ScaleLy;
        Compiled.InvInertia = 1.0f / std::max(WheelInertiaKgM2, 0.05f);
        Compiled.InvRadius  = 1.0f / std::max(OuterRadiusMeters, 0.05f);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                      AERODYNAMICS, ASSISTS & FULL RIG SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct AerodynamicPackageSpecification
{
    float AirDensityKgPerM3         = 1.225f; // [kg/m^3] - Sea-level standard air
    float FloorOffsetMeters         = 0.14f;  // [m]      - Underbody floor clearance offset below chassis origin

    // Body aero (resolved strictly in vehicle body axes)
    float BodyFrontalAreaM2         = 2.15f;  // [m^2]
    float BodySideAreaM2            = 4.20f;  // [m^2]
    float BodyDragCoeff             = 0.34f;  // [-]
    float BodyLiftCoeff             = 0.05f;  // [-] (positive = lift)
    float BodySideForceCoeff        = 0.65f;  // [-]

    // Rear wing (adaptive + brake deploy; detaches cleanly if RearWing Dock breaks)
    bool  RearWingAttached          = true;
    float WingAreaM2                = 0.92f;  // [m^2]
    float WingBaseDownforceCoeff    = 0.95f;  // [-]
    float WingBaseDragCoeff         = 0.14f;  // [-]
    float WingBrakeDeployBoost      = 0.35f;  // [-] - Additional downforce/drag fraction under heavy braking

    // Front splitter & underbody diffuser (true floor clearance ground effect)
    float SplitterAreaM2            = 0.55f;  // [m^2]
    float SplitterDownforceCoeff    = 0.52f;  // [-]
    float UnderbodyFloorAreaM2      = 2.40f;  // [m^2]
    float UnderbodySuctionCoeff     = 0.42f;  // [-]
    float RideHeightOptimumMeters   = 0.08f;  // [m] - Peak ground-effect floor clearance
    float RideHeightCriticalMeters  = 0.03f;  // [m] - Porpoising stall threshold below this clearance
    float SideSkirtSealingFactor    = 0.85f;  // [-]
    float VortexGenDiffuserBonus    = 1.12f;  // [-]
    float FrontDownforceShare       = 0.38f;  // [-] - Nominal front-axle aero balance (rest on rear)
};

struct DriverAssistSpecification
{
    bool                   AbsEnabled             = true;
    float                  AbsTargetSlipRatio     = -0.16f; // [-]
    float                  AbsReleaseSlipRatio    = -0.22f; // [-]
    float                  AbsMinSpeedMps         = 3.0f;   // [m/s]

    TractionAssistCategory TcsCategory            = TractionAssistCategory::Sport;
    float                  TcsTargetSlipRatio     = 0.15f;  // [-]
    float                  TcsMaxTorqueCut        = 0.80f;  // [-]

    bool                   EscEnabled             = true;
    float                  EscYawErrorDeadbandRadS= 0.14f;  // [rad/s]
    float                  EscBrakeGainNmPerRadS  = 2400.0f;// [N*m/(rad/s)]
};

struct WheelCornerSpecification
{
    Mathematics::Axis3D         LocalMountMeters { 0.0f, 0.0f, 0.0f }; // [m] - Chassis-local hardpoint (+X fwd, +Y up, +Z right)
    bool                        Steerable        = false;
    bool                        Driven           = false;
    bool                        HandbrakeWheel   = false;
    bool                        LeftSide         = false;
    bool                        FrontAxle        = false;
    SuspensionCornerSpecification Suspension     {};
    PacejkaTireSpecification      Tire           {};
};

struct VehicleRigSpecification
{
    float                                        DryChassisMassKg     = 1250.0f; // [kg] - Mass excluding fuel
    Mathematics::Axis3D                          ChassisHalfExtentsM  { 2.45f, 0.26f, 0.96f }; // [m] - Keeps ~0.16 m underbody ground clearance
    Mathematics::Axis3D                          CenterOfMassOffsetM  { 0.05f, -0.14f, 0.0f }; // [m]
    float                                        WheelbaseMeters      = 3.396f;  // [m]
    float                                        TrackWidthFrontM     = 2.095f;  // [m]
    float                                        TrackWidthRearM      = 2.095f;  // [m]

    DrivetrainLayoutCategory                     DrivetrainLayout     = DrivetrainLayoutCategory::AllWheelDrive;
    ForcedInductionCategory                      ForcedInduction      = ForcedInductionCategory::Turbocharged;
    WheelContactQueryMode                        WheelContactMode     = WheelContactQueryMode::MultiRayArc; // [-] - Default 5-ray tire arc
    uint16_t                                     ChassisMaterialId    = 0u;                                 // [id] - PhysicsMaterial for hull contacts
    bool                                         EnableDebugLogging   = true;                               // [-] - Emit structured events to PhysicsDebugLog

    EngineSpecification                          Engine               {};
    EngineOilSpecification                       EngineOil            {};
    FuelDeliverySpecification                    FuelDelivery         {};
    TurbochargerSpecification                    Turbocharger         {};
    SuperchargerSpecification                    Supercharger         {};
    ClutchSpecification                          Clutch               {};
    TransmissionSpecification                    Transmission         {};
    DifferentialSpecification                    CenterDifferential   {};
    DifferentialSpecification                    FrontDifferential    {};
    DifferentialSpecification                    RearDifferential     {};
    BrakingSpecification                         Braking              {};
    SteeringRackSpecification                    SteeringRack         {};
    AntiRollbarSpecification                     FrontAntiRollbar     {};
    AntiRollbarSpecification                     RearAntiRollbar      {};
    AerodynamicPackageSpecification              Aerodynamics         {};
    DriverAssistSpecification                    Assists              {};

    // Corner order: 0 = FL, 1 = FR, 2 = RL, 3 = RR
    std::array<WheelCornerSpecification, MaxVehicleWheelCorners> Corners {};

    // Populate complete factory presets (zero heap allocations)
    static void PopulateDefaultCurves(VehicleRigSpecification& Spec) noexcept;
    static void ApplyTirePreset(PacejkaTireSpecification& Tire, TirePresetCategory Preset) noexcept;
    static void ApplySuspensionArchitectureDefaults(
        SuspensionCornerSpecification& Corner,
        SuspensionArchitectureCategory Architecture) noexcept;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
