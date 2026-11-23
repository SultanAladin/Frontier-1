/*============================================================================================================================================
                                                        VEHICLEDYNAMICSSOLVER.H
============================================================================================================================================*/
// 🧩 JoltPhysics v5.6.0 vehicle solver: Pacejka MF6.1, implicit Newton slip, turbo/supercharger, fuel, 6 suspension designs & wait-free readings

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/ComponentClass/ComponentInstanceCategories.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleSpecifications.h"
#include "Engine/Threading/Sync/ThreadBoundaryExchange.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   LEGACY TELEMETRY STRUCT
//------------------------------------------------------------------------------------------------------------------------

struct VehicleSimulationTelemetry
{
    float    SpeedMps             = 0.0f;   // [m/s]   - Longitudinal chassis ground speed
    float    Rpm                  = 700.0f; // [rpm]   - Engine crankshaft speed
    float    AccelerationMps2     = 0.0f;   // [m/s^2] - Net longitudinal acceleration
    float    EngineTorqueNm       = 0.0f;   // [N*m]   - Instantaneous crankshaft torque
    float    AeroDownforceNewtons = 0.0f;   // [N]     - Aerodynamic downforce (0.5 * rho * ClA * v^2)
    float    WheelAngleRadians    = 0.0f;   // [rad]   - Accumulated tyre rotation angle
    float    DistanceMeters       = 0.0f;   // [m]     - Longitudinal distance travelled
    uint32_t CurrentGearIndex     = 0u;     // [idx]   - 0-based active gear (0..5 -> GEAR 1..6)
};

//------------------------------------------------------------------------------------------------------------------------
//                                        CROSS-THREAD VEHICLE CONTROL DIRECTIVE
//------------------------------------------------------------------------------------------------------------------------

enum class VehicleDirectiveCategory : uint8_t
{
    DriverInput             = 0u, // Continuous throttle/brake/steer/handbrake/clutch & shift triggers
    AssignForcedInduction   = 1u, // Switch NA / Turbo / Supercharger / Twincharged (plus supercharger drive type)
    AssignSuspensionDesign  = 2u, // Switch Front & Rear suspension architectures + spring curve type
    RefuelTank              = 3u, // Add fuel liters (or reset to full tank when <= 0)
    AssignRearWingAttached  = 4u, // Couple RearWing Dock rupture/relatch into aero package
    AssignDriverAssists     = 5u, // Toggle ABS / TCS / ESC at runtime
};

struct VehicleControlDirective
{
    VehicleDirectiveCategory       Category                = VehicleDirectiveCategory::DriverInput;

    // DriverInput payload
    float                          ThrottleNormalized      = 0.0f; // [0..1]
    float                          BrakeNormalized         = 0.0f; // [0..1]
    float                          SteeringNormalized      = 0.0f; // [-1..+1] (+1 = right turn)
    float                          HandbrakeNormalized     = 0.0f; // [0..1]
    float                          ClutchPedalNormalized   = 0.0f; // [0..1] (manual clutch only)
    bool                           ShiftUpRequested        = false;
    bool                           ShiftDownRequested      = false;
    bool                           ReverseRequested        = false;

    // Forced induction override payload
    ForcedInductionCategory        ForcedInduction         = ForcedInductionCategory::Turbocharged;
    SuperchargerDriveCategory      SuperchargerDrive       = SuperchargerDriveCategory::TwinScrew;

    // Suspension architecture override payload
    SuspensionArchitectureCategory FrontArchitecture       = SuspensionArchitectureCategory::DoubleWishbone;
    SuspensionArchitectureCategory RearArchitecture        = SuspensionArchitectureCategory::MultiLink;
    SpringCurveCategory            SpringCurve             = SpringCurveCategory::Progressive;

    // Fuel / Aero / Assist payload
    float                          RefuelLiters            = 0.0f; // [L]
    bool                           RearWingAttached        = true;
    bool                           AbsEnabled              = true;
    TractionAssistCategory         TcsCategory             = TractionAssistCategory::Sport;
    bool                           EscEnabled              = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                       WAIT-FREE PUBLISHED VEHICLE READING
//------------------------------------------------------------------------------------------------------------------------

struct WheelCornerReading
{
    bool                   InContact              = false;
    float                  SuspensionCompressionM = 0.0f;   // [m]     - Positive upward compression from full droop
    float                  SuspensionVelocityMps  = 0.0f;   // [m/s]   - Positive = compressing (jounce)
    float                  SpringForceNewtons     = 0.0f;   // [N]
    float                  DamperForceNewtons     = 0.0f;   // [N]
    float                  AntiRollForceNewtons   = 0.0f;   // [N]
    float                  VerticalLoadNewtons    = 0.0f;   // [N]     - Effective contact normal load Fz
    float                  DynamicCamberRad       = 0.0f;   // [rad]   - Suspension-kinematic camber angle
    float                  DynamicBumpToeRad      = 0.0f;   // [rad]   - Kinematic bump-toe angle
    float                  SteerAngleRad          = 0.0f;   // [rad]   - Ackermann steered wheel angle
    float                  AngularVelocityRadS    = 0.0f;   // [rad/s] - Wheel spin rate
    float                  SpinAngleRad           = 0.0f;   // [rad]   - Accumulated visual wheel rotation
    float                  LongitudinalSlip       = 0.0f;   // [-]     - Pacejka slip ratio kappa
    float                  SlipAngleRad           = 0.0f;   // [rad]   - Pacejka lateral slip angle alpha
    float                  LongitudinalForceN     = 0.0f;   // [N]     - Applied tire Fx
    float                  LateralForceN          = 0.0f;   // [N]     - Applied tire Fy
    float                  SelfAligningTorqueNm   = 0.0f;   // [N*m]   - Pacejka Mz
    float                  DriveTorqueNm          = 0.0f;   // [N*m]
    float                  BrakeTorqueNm          = 0.0f;   // [N*m]
    float                  BrakeTempKelvin        = StandardAmbientKelvin; // [K]
    float                  TreadTempKelvin        = StandardAmbientKelvin; // [K]
    float                  CarcassTempKelvin      = StandardAmbientKelvin; // [K]
    float                  TreadDepthMm           = 5.5f;   // [mm]
    FrictionRegimeCategory FrictionRegime         = FrictionRegimeCategory::Kinetic;
    Mathematics::Axis3D    ContactPointWorldM     { 0.0f, 0.0f, 0.0f }; // [m]
    Mathematics::Axis3D    WheelHubLocalMeters    { 0.0f, 0.0f, 0.0f }; // [m] - Visual wheel center offset in chassis frame
};

struct VehicleReading
{
    // Body-axis kinematics (always populated every fixed step)
    Mathematics::Axis3D        WorldPositionMeters     { 0.0f, 0.0f, 0.0f };
    RigidOrientationQuaternion WorldOrientation        {};
    Mathematics::Axis3D        LinearVelocityWorldMps  { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D        AngularVelocityWorldRadS{ 0.0f, 0.0f, 0.0f };
    float                      ForwardSpeedMps         = 0.0f;   // [m/s]
    float                      LateralSpeedMps         = 0.0f;   // [m/s]
    float                      VerticalSpeedMps        = 0.0f;   // [m/s]
    float                      GroundSpeedMps          = 0.0f;   // [m/s]
    float                      SpeedKph                = 0.0f;   // [km/h]
    float                      LongitudinalG           = 0.0f;   // [g]
    float                      LateralG                = 0.0f;   // [g]
    float                      YawRateRadPerSec        = 0.0f;   // [rad/s]
    float                      PitchRateRadPerSec      = 0.0f;   // [rad/s]
    float                      RollRateRadPerSec       = 0.0f;   // [rad/s]
    float                      DistanceTraveledMeters  = 0.0f;   // [m]

    // Engine, thermal & manifold
    float                      EngineRpm               = 700.0f; // [rpm]
    float                      EngineTorqueNm          = 0.0f;   // [N*m] - Net flywheel torque after induction & drag
    float                      EnginePowerKw           = 0.0f;   // [kW]
    float                      EngineBrakingNm         = 0.0f;   // [N*m]
    float                      CoolantTempKelvin       = StandardAmbientKelvin; // [K]
    float                      OilTempKelvin           = StandardAmbientKelvin; // [K]
    float                      ManifoldPressureKPa     = StandardAtmosphereKPa; // [kPa] - Absolute MAP (101.325 + boost*100)
    float                      ManifoldTempKelvin      = StandardAmbientKelvin; // [K]

    // Forced induction (Turbocharger + Supercharger + Twincharged)
    ForcedInductionCategory    ActiveInduction         = ForcedInductionCategory::Turbocharged;
    SuperchargerDriveCategory  ActiveSuperchargerDrive = SuperchargerDriveCategory::TwinScrew;
    float                      TurboShaftRpm           = 0.0f;   // [rpm]
    float                      TurboBoostBar           = 0.0f;   // [bar] - Gauge pressure
    float                      WastegatePosition       = 0.0f;   // [0..1]
    float                      BovPosition             = 0.0f;   // [0..1]
    float                      SuperchargerRpm         = 0.0f;   // [rpm]
    float                      SuperchargerBoostBar    = 0.0f;   // [bar] - Gauge pressure
    float                      SuperchargerBypassPos   = 0.0f;   // [0..1]
    float                      SuperchargerParasiticNm = 0.0f;   // [N*m] - Crank belt parasitic drag
    float                      CombinedBoostBar        = 0.0f;   // [bar] - Net gauge boost entering plenum
    float                      BoostTorqueMultiplier   = 1.0f;   // [-]

    // Fuel delivery & live mass
    float                      FuelLiters              = 68.0f;  // [L]
    float                      FuelMassKg              = 50.66f; // [kg]
    float                      FuelLevelFraction       = 1.0f;   // [0..1]
    float                      FuelFlowKgPerSec        = 0.0f;   // [kg/s]
    float                      ActiveAirFuelRatio      = 14.7f;  // [-]
    float                      EstimatedRangeSeconds   = 0.0f;   // [s]
    float                      LiveTotalMassKg         = 1300.0f;// [kg]
    bool                       FuelWarningActive       = false;
    bool                       FuelCriticalActive      = false;
    bool                       FuelStarved             = false;

    // Clutch & Transmission
    int32_t                    ActiveGear              = 1;      // [-1=R, 0=N, 1..N=Forward]
    int32_t                    TargetGear              = 1;
    bool                       ShiftingActive          = false;
    float                      ClutchEngagement        = 0.0f;   // [0..1]
    bool                       ClutchLocked            = false;
    float                      ClutchTorqueNm          = 0.0f;   // [N*m]
    float                      ClutchSlipRpm           = 0.0f;   // [rpm]

    // Aerodynamics (resolved strictly in vehicle body axes + true floor clearance)
    float                      AeroDragNewtons         = 0.0f;   // [N]
    float                      AeroDownforceNewtons    = 0.0f;   // [N]
    float                      AeroFrontDownforceN     = 0.0f;   // [N]
    float                      AeroRearDownforceN      = 0.0f;   // [N]
    float                      AeroSideForceNewtons    = 0.0f;   // [N]
    float                      FloorClearanceMeters    = 0.14f;  // [m]
    float                      GroundEffectMultiplier  = 1.0f;   // [-]
    bool                       RearWingAttached        = true;

    // Driver assists & suspension architecture
    SuspensionArchitectureCategory FrontArchitecture   = SuspensionArchitectureCategory::DoubleWishbone;
    SuspensionArchitectureCategory RearArchitecture    = SuspensionArchitectureCategory::MultiLink;
    bool                           AbsIntervening      = false;
    bool                           TcsIntervening      = false;
    bool                           EscIntervening      = false;

    // Per-corner telemetry (0=FL, 1=FR, 2=RL, 3=RR)
    std::array<WheelCornerReading, MaxVehicleWheelCorners> Corners {};

    // Simulation step telemetry
    uint64_t                   CompletedSteps          = 0ull;
    double                     SimulatedSeconds        = 0.0;
    float                      StepComputeMicroseconds = 0.0f;   // [us]
};

//------------------------------------------------------------------------------------------------------------------------
//                                      LEGACY POWERTRAIN UTILITY FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

class VehicleDynamicsSolver
{
public:
    [[nodiscard]] static float EvaluateTorqueCurveNm(
        const ComponentClass::VehicleDynamicsSettings& Settings,
        float                                          Rpm) noexcept;

    static void StepPowertrain(
        const ComponentClass::VehicleDynamicsSettings& Settings,
        float                                          ThrottleNormalized,
        float                                          BrakeNormalized,
        double                                         DeltaSeconds,
        VehicleSimulationTelemetry&                    InOutTelemetry) noexcept;

    [[nodiscard]] static float EvaluateConstraintEditScalar(
        const ComponentClass::ConstraintComponentSettings& Settings) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                     JOLT PHYSICS VEHICLE RIG (PHYSICS THREAD)
//------------------------------------------------------------------------------------------------------------------------

class VehiclePhysicsRig
{
public:
    using VehicleReadingLease    = Threading::ReadingLease<VehicleReading>;
    using VehicleReadingExchange = Threading::PublishedReadingExchange<VehicleReading, 8u>;

    VehiclePhysicsRig() noexcept;
    ~VehiclePhysicsRig() noexcept = default;

    VehiclePhysicsRig(const VehiclePhysicsRig&)            = delete;
    VehiclePhysicsRig& operator=(const VehiclePhysicsRig&) = delete;

    // Translate authored ControlVehicle ComponentClassDefinition into full VehicleRigSpecification
    [[nodiscard]] static VehicleRigSpecification ConstructSpecificationFromControlVehicle(
        const ComponentClass::ComponentClassDefinition& ControlVehicleDefinition) noexcept;

    // Lifecycle (invoke Initialize/StepOnPhysicsThread/Finalize from PhysicsThread / simulation owner)
    [[nodiscard]] bool Initialize(
        PhysicsExtension&                 Physics,
        const VehicleRigSpecification&    InitialSpecification,
        const Mathematics::Axis3D&        SpawnPositionMeters,
        const RigidOrientationQuaternion& SpawnOrientation = {}) noexcept;

    void StepOnPhysicsThread(PhysicsExtension& Physics, double FixedDeltaSeconds) noexcept;
    void Finalize(PhysicsExtension& Physics) noexcept;

    // Thread-safe cross-thread API (callable from GameThread, RenderThread, AudioThread, EditorThread)
    [[nodiscard]] bool                          SubmitControlDirective(const VehicleControlDirective& Directive) noexcept;
    [[nodiscard]] VehicleReadingLease           AcquireVehicleReading() const noexcept;
    [[nodiscard]] const VehicleReadingExchange& InspectVehicleReadingExchange() const noexcept;
    [[nodiscard]] RigidBodyToken                QueryChassisBodyToken() const noexcept;
    [[nodiscard]] bool                          QueryInitialized() const noexcept;

private:
    struct InternalWheelRuntime
    {
        float                  PrevCompressionM    = 0.0f;
        float                  CompressionM        = 0.0f;
        float                  CompressionVelMps   = 0.0f;
        float                  SpringForceN        = 0.0f;
        float                  DamperForceN        = 0.0f;
        float                  AntiRollForceN      = 0.0f;
        float                  VerticalLoadN       = 0.0f;
        float                  SprungMassKg        = 325.0f;
        float                  DynamicCamberRad    = 0.0f;
        float                  DynamicBumpToeRad   = 0.0f;
        float                  SteerAngleRad       = 0.0f;
        float                  AngularVelocityRadS = 0.0f;
        float                  SpinAngleRad        = 0.0f;
        float                  LongSlipKappa       = 0.0f;
        float                  SlipAngleAlphaRad   = 0.0f;
        float                  FxTransientN        = 0.0f;
        float                  FyTransientN        = 0.0f;
        float                  AppliedFxN          = 0.0f;
        float                  AppliedFyN          = 0.0f;
        float                  AppliedMzNm         = 0.0f;
        float                  DriveTorqueNm       = 0.0f;
        float                  BrakePressurePa     = 0.0f;
        float                  BrakeTorqueNm       = 0.0f;
        float                  BrakeTempK          = StandardAmbientKelvin;
        float                  TreadTempK          = StandardAmbientKelvin + 35.0f; // Warm street start (~55 C)
        float                  CarcassTempK        = StandardAmbientKelvin + 30.0f;
        float                  TreadDepthMm        = 5.5f;
        bool                   InContact           = false;
        FrictionRegimeCategory FrictionRegime      = FrictionRegimeCategory::Kinetic;
        Mathematics::Axis3D    ContactPointWorldM  { 0.0f, 0.0f, 0.0f };
        Mathematics::Axis3D    ContactNormalWorld  { 0.0f, 1.0f, 0.0f };
        Mathematics::Axis3D    HubLocalMeters      { 0.0f, 0.0f, 0.0f };
    };

    void CalibrateSuspensionSprings() noexcept;
    void ApplyDirective(const VehicleControlDirective& Directive) noexcept;

    void SolveSuspensionKinematics(
        PhysicsExtension&          Physics,
        const RigidBodyReading&    Body,
        const Mathematics::Axis3D& AxisForward,
        const Mathematics::Axis3D& AxisUp,
        const Mathematics::Axis3D& AxisRight,
        float                      DeltaSeconds) noexcept;

    void ComputeAntiRollForces() noexcept;

    void ComputeSteeringRack(
        float ForwardSpeedMps,
        float LateralSpeedMps,
        float DeltaSeconds) noexcept;

    void ComputeBodyAxisAerodynamics(
        float                      ForwardSpeedMps,
        float                      LateralSpeedMps,
        float                      VerticalSpeedMps,
        float                      BrakeInput,
        const Mathematics::Axis3D& AxisForward,
        const Mathematics::Axis3D& AxisUp,
        const Mathematics::Axis3D& AxisRight,
        PhysicsExtension&          Physics,
        const RigidBodyReading&    Body) noexcept;

    void ComputeForcedInduction(
        float EngineRpm,
        float Throttle,
        float DeltaSeconds) noexcept;

    void SolveBrakeHydraulics(
        float ForwardSpeedMps,
        float YawRateRadS,
        float DeltaSeconds) noexcept;

    void SolveDrivetrainTorque(
        float ForwardSpeedMps,
        float LateralG,
        float DeltaSeconds) noexcept;

    void SolveTireTraction(
        PhysicsExtension&          Physics,
        const RigidBodyReading&    Body,
        const Mathematics::Axis3D& AxisForward,
        const Mathematics::Axis3D& AxisUp,
        const Mathematics::Axis3D& AxisRight,
        float                      ForwardSpeedMps,
        float                      DeltaSeconds) noexcept;

    void SolveFuelThermalBalance(
        PhysicsExtension& Physics,
        float             EngineOmegaRadS,
        float             NetEngineTorqueNm,
        float             GroundSpeedMps,
        float             DeltaSeconds) noexcept;

    void PublishReading(
        const RigidBodyReading& Body,
        float                   StepComputeUs) noexcept;

    [[nodiscard]] float EvaluatePacejkaPureFx(uint32_t CornerIdx, float Kappa, float CamberRad, float GripScale) const noexcept;
    [[nodiscard]] float EvaluatePacejkaPureFy(uint32_t CornerIdx, float AlphaRad, float CamberRad, float GripScale) const noexcept;
    void                EvaluatePacejkaCombined(
        uint32_t CornerIdx,
        float    Kappa,
        float    AlphaRad,
        float    CamberRad,
        float    GripScale,
        float&   OutFxN,
        float&   OutFyN,
        float&   OutMzNm) const noexcept;

    VehicleRigSpecification Specification {};
    RigidBodyToken          ChassisBody   {};
    bool                    Initialized   = false;

    // Live driver control inputs (PhysicsThread-owned, updated from drained directives)
    float                   InputThrottle       = 0.0f;
    float                   InputBrake          = 0.0f;
    float                   InputSteering       = 0.0f;
    float                   InputHandbrake      = 0.0f;
    float                   InputClutchPedal    = 0.0f;
    bool                    InputReverse        = false;
    bool                    PendingShiftUp      = false;
    bool                    PendingShiftDown    = false;

    // Kinematics runtime
    float                   PrevForwardSpeedMps = 0.0f;
    float                   PrevLateralSpeedMps = 0.0f;
    float                   ForwardSpeedMps     = 0.0f;
    float                   LateralSpeedMps     = 0.0f;
    float                   VerticalSpeedMps    = 0.0f;
    float                   GroundSpeedMps      = 0.0f;
    float                   SmoothedLongG       = 0.0f;
    float                   SmoothedLatG        = 0.0f;
    float                   YawRateRadPerSec    = 0.0f;
    float                   PitchRateRadPerSec  = 0.0f;
    float                   RollRateRadPerSec   = 0.0f;
    float                   DistanceTraveledM   = 0.0f;

    // Steering runtime
    float                   CurrentSteerAngleRad = 0.0f;

    // Engine, forced induction & thermal runtime
    float                   EngineRpm            = 700.0f;
    float                   EngineTorqueNm       = 0.0f;
    float                   EnginePowerKw        = 0.0f;
    float                   EngineBrakingNm      = 0.0f;
    float                   CoolantTempK         = StandardAmbientKelvin + 62.0f; // Warmed up (~82 C)
    float                   OilTempK             = StandardAmbientKelvin + 65.0f; // (~85 C)
    float                   ManifoldPressureKPa  = StandardAtmosphereKPa;
    float                   ManifoldTempK        = StandardAmbientKelvin;

    float                   TurboShaftRpm        = 12000.0f; // Warm idle spin
    float                   TurboBoostBar        = 0.0f;
    float                   WastegatePos         = 0.0f;
    float                   BovPos               = 0.0f;

    float                   SuperchargerRpm      = 0.0f;
    float                   SuperchargerBoostBar = 0.0f;
    float                   SuperchargerBypass   = 1.0f;
    float                   SuperchargerDragNm   = 0.0f;

    float                   CombinedBoostBar     = 0.0f;
    float                   BoostTorqueMult      = 1.0f;

    // Fuel runtime
    float                   FuelLiters           = 68.0f;
    float                   FuelMassKg           = 50.66f;
    float                   FuelFlowKgPerSec     = 0.0f;
    float                   ActiveAfr            = 14.7f;
    float                   EstimatedRangeSec    = 99999.0f;
    float                   LiveTotalMassKg      = 1300.0f;
    float                   LastAppliedJoltMassKg= 1300.0f;
    bool                    FuelWarning          = false;
    bool                    FuelCritical         = false;
    bool                    FuelStarved          = false;

    // Clutch & Transmission runtime
    int32_t                 ActiveGear           = 1;
    int32_t                 TargetGear           = 1;
    bool                    ShiftingActive       = false;
    float                   ShiftElapsedSec      = 0.0f;
    float                   ShiftCooldownRemain  = 0.0f;
    float                   ClutchEngagement     = 0.0f;
    bool                    ClutchLocked         = false;
    float                   ClutchTorqueNm       = 0.0f;
    float                   ClutchSlipRpm        = 0.0f;

    // Aero runtime
    float                   AeroDragN            = 0.0f;
    float                   AeroDownforceN       = 0.0f;
    float                   AeroFrontDownforceN  = 0.0f;
    float                   AeroRearDownforceN   = 0.0f;
    float                   AeroSideForceN       = 0.0f;
    float                   FloorClearanceM      = 0.14f;
    float                   GroundEffectMult     = 1.0f;

    // Assist intervention runtime
    bool                    AbsIntervening       = false;
    bool                    TcsIntervening       = false;
    bool                    EscIntervening       = false;
    float                   TcsTorqueScale       = 1.0f;

    // Per-corner runtime
    std::array<InternalWheelRuntime, MaxVehicleWheelCorners> Wheels {};

    // Step accounting
    uint64_t                CompletedSteps       = 0ull;
    double                  SimulatedSeconds     = 0.0;

    // Generic wait-free cross-thread reading bank + lock-free directive queue
    Threading::ThreadBoundaryExchange<VehicleReading, VehicleControlDirective, 8u> BoundaryExchange;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
