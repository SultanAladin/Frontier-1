//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/Drivetrain.h — engine / turbo / AMT clutch / transmission / differential
//============================================================================================================================================
//
//    Phase-1 port of GRIT's powertrain (VehicleSolver.cpp SolvePowertrain / differential lambda, plus the component spec
//    headers Engine/Turbocharger/Transmission/Clutch Specifications.h). Pure C++/scalar — no Unreal, no Jolt, no threading.
//    Defaults are the GRIT "preset-1 GTR" values extracted from the source (torque curve, turbo maps, ratios, inertias).
//
//    Pipeline per step:  engine (RK4 on ω, torque-curve × turbo boost − engine braking) → AMT single clutch (slip/lock)
//                        → transmission (gear × final drive) → differential (Open / Locked / LSD / TorqueVectoring).
//
//    References (as cited in GRIT source): Heywood (1988) "Internal Combustion Engine Fundamentals"; Dixon (2013)
//    "The Shock Absorber Handbook" / driveline; Serrano et al. (2007) turbocharger modelling; Van Berkel (2014) & Walker
//    (2011) automated-manual-transmission clutch dynamics.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Frontier::Vehicle {

//------------------------------------------------------------------------------------------------------------------------
// Monotonic-x lookup curve with binary-search + linear interpolation (mirrors GRIT SampleTorque / Eval* pattern).
//------------------------------------------------------------------------------------------------------------------------
struct Curve
{
    std::vector<float> X, Y;

    void Add(float x, float y) { X.push_back(x); Y.push_back(y); }

    [[nodiscard]] float Sample(float x) const noexcept
    {
        const std::size_t n = X.size();
        if (n == 0) return 0.0f;
        if (x <= X.front()) return Y.front();
        if (x >= X.back())  return Y.back();
        std::size_t lo = 0, hi = n - 1;
        while (hi - lo > 1) { const std::size_t mid = (lo + hi) >> 1; (X[mid] <= x ? lo : hi) = mid; }
        const float t = (X[hi] == X[lo]) ? 0.0f : (x - X[lo]) / (X[hi] - X[lo]);
        return Y[lo] + t * (Y[hi] - Y[lo]);   // FMath::Lerp
    }
};

//------------------------------------------------------------------------------------------------------------------------
struct EngineParameters
{
    Curve TorqueCurve;        // rpm → Nm (wide-open throttle)
    Curve ExhaustFlowCurve;   // rpm → kg/s
    Curve ExhaustTempCurve;   // rpm → K

    float IdleRPM = 700.0f;
    float RedlineRPM = 7200.0f;
    float MaxRPMIncrease = 1800.0f;   // [rpm/s]
    float MaxRPMDecrease = 2600.0f;   // [rpm/s]
    float EngineBrakingCoeff = 0.08f;      // [N·m·s/rad]
    float EngineBrakingQuadratic = 0.00012f;
    float EngineInertia = 0.38f;           // [kg·m²]
    float InvEngineInertia = 1.0f / 0.38f;

    static EngineParameters DefaultGTR();
};

struct TurbochargerParameters
{
    Curve BoostPressureCurve;      // turbo rpm → Bar
    Curve TorqueMultiplierCurve;   // Bar → multiplier

    float TurboInertia = 0.02f;            // [kg·m²]
    float InvTurboInertia = 50.0f;
    float TurboFrictionCoeff = 0.002f;
    float TurboFrictionQuadratic = 0.00005f;
    float WastegateMaxBoost = 1.2f;        // [Bar] (race: 1.6)
    float BovPressureReleaseRate = 10.0f;  // [Bar/s]
    float TurbineEfficiency = 0.65f;
    float CompressorEfficiency = 0.70f;
    float MechanicalEfficiency = 0.98f;

    static TurbochargerParameters DefaultGTR();
};

// Forced-induction selection. A supercharger is BELT-driven off the crank, so its boost tracks engine rpm with no lag
//   (unlike the turbo's exhaust-spool), and it robs crank torque as parasitic drag. A blown engine trades the turbo's
//   lag for an instant, RPM-proportional boost and a permanent parasitic tax.
enum class InductionType
{
    NaturallyAspirated,   // no forced induction (boostMult = 1)
    Turbocharged,         // exhaust-driven turbo (default, spool dynamics)
    Supercharged,         // belt-driven supercharger (instant boost, parasitic drag)
};

// Port of GRIT's FSuperchargerSpecifications (source-only Components/SuperchargerSpecifications.h). Positive-displacement
//   or centrifugal blower geared to the crank at DriveRatio; boost from charger-rpm curve, torque gain from a boost
//   curve, and a parasitic-drag curve that loads the crank. A bypass (recirculation) valve regulates boost to the tune
//   ceiling and vents off-throttle so the blower is not compressing against a closed throttle.
enum class SuperchargerDriveType { Roots, TwinScrew, Centrifugal };

struct SuperchargerParameters
{
    SuperchargerDriveType DriveType = SuperchargerDriveType::TwinScrew;

    Curve BoostPressureCurve;      // charger rpm → Bar
    Curve TorqueMultiplierCurve;   // Bar → multiplier
    Curve ParasiticDragCurve;      // charger rpm → N·m (crank load)

    float ChargerInertia          = 0.015f;   // [kg·m²]
    float ChargerFrictionCoeff    = 0.003f;   // [N·m·s/rad]
    float ChargerFrictionQuadratic= 0.00008f; // [N·m·s²/rad²]
    float DriveRatio              = 3.2f;     // charger rpm = engine rpm × DriveRatio
    float MaxBoost_Base           = 0.8f;     // [Bar] pump-fuel tune ceiling
    float MaxBoost_Race           = 1.2f;     // [Bar] race-fuel tune ceiling
    float BypassValveRate         = 8.0f;     // [Bar/s] recirculation valve slew
    float CompressorEfficiency    = 0.72f;    // [-]
    float ParasiticLossFactor     = 0.08f;    // [-] residual drag fraction when fully bypassed (off-throttle)

    [[nodiscard]] float ChargerRPM(float engineRPM) const noexcept { return engineRPM * DriveRatio; }

    static SuperchargerParameters DefaultTwinScrew();
};

struct TransmissionParameters
{
    // Index layout: [0]=R2, [1]=R1, [2]=N(0), [3..]=1st..6th
    std::vector<float> GearRatios = {-3.8f, -2.0f, 0.0f, 4.2f, 3.1f, 2.5f, 1.9f, 1.5f, 1.2f};
    float FinalDriveRatio = 3.55f;
    float DriveEfficiency = 0.92f;
    int   NeutralIndex = 2;

    [[nodiscard]] float RatioAt(int gearIndex) const noexcept
    {
        if (gearIndex < 0 || gearIndex >= static_cast<int>(GearRatios.size())) return 0.0f;
        return GearRatios[gearIndex];
    }
};

struct ClutchParameters
{
    float MaxTorqueCapacity = 1200.0f;   // [N·m] (preset: 1500)
    float BreakawaySlipRPM  = 300.0f;    // slip at which capacity is reached
    float LockupEngagement  = 0.98f;
    float LockupSlipRPM     = 50.0f;
};

enum class DifferentialMode
{
    Open,            // equal torque split
    Locked,          // spool: equal speed, torque follows
    LimitedSlip,     // clutch-pack LSD with locking bias
    TorqueVectoring, // active bias toward the outer/faster or commanded wheel
};

struct DifferentialParameters
{
    DifferentialMode Mode = DifferentialMode::LimitedSlip;
    float LockingFactor = 0.5f;   // [0..1] LSD bias strength
    float Preload_Nm = 20.0f;     // LSD preload torque
    float VectoringBias = 0.0f;   // [-1..1] TV command (+ = bias right)
};

//------------------------------------------------------------------------------------------------------------------------
struct DrivetrainInputs
{
    float Throttle = 0.0f;    // [0..1]
    int   GearIndex = 3;      // index into GearRatios (3 = 1st gear)
    float dt = 1.0f / 240.0f; // [s]
    // Feedback from the wheels (average driven-wheel spin surface speed → gearbox output rpm reference).
    float DrivenWheelRPM = 0.0f;   // average driven wheel rpm (post-final-drive reference for clutch slip)
    float LeftWheelRPM = 0.0f;
    float RightWheelRPM = 0.0f;
};

struct DrivetrainOutputs
{
    float EngineRPM = 0.0f;
    float EngineTorque_Nm = 0.0f;     // net engine torque after braking, before clutch
    float TurboRPM = 0.0f;            // turbo shaft rpm (0 when supercharged/NA) — also carries charger rpm when blown
    float BoostPressure_Bar = 0.0f;
    float BoostMultiplier = 1.0f;
    float ParasiticDrag_Nm = 0.0f;    // supercharger crank load (0 when NA/turbo)
    float ClutchTorque_Nm = 0.0f;     // torque transmitted through clutch
    bool  ClutchLocked = false;
    float GearboxOutputTorque_Nm = 0.0f;   // after gear × final drive × efficiency
    float LeftDriveTorque_Nm = 0.0f;       // differential outputs
    float RightDriveTorque_Nm = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
class Drivetrain
{
public:
    Drivetrain();

    void AssignEngine(const EngineParameters& P) noexcept { Engine = P; }
    void AssignTurbo(const TurbochargerParameters& P) noexcept { Turbo = P; }
    void AssignTransmission(const TransmissionParameters& P) noexcept { Trans = P; }
    void AssignClutch(const ClutchParameters& P) noexcept { Clutch = P; }
    void AssignDifferential(const DifferentialParameters& P) noexcept { Diff = P; }
    void AssignInduction(InductionType t) noexcept { Induction = t; }
    void AssignSupercharger(const SuperchargerParameters& P) noexcept { Super = P; }
    void AssignRaceTune(bool race) noexcept { RaceTune = race; }

    void Reset(float engineRPM) noexcept;

    DrivetrainOutputs Step(const DrivetrainInputs& In) noexcept;

    [[nodiscard]] float EngineRPM() const noexcept { return EngineOmega * kRadToRpm; }
    [[nodiscard]] float TurboRPM()  const noexcept { return TurboOmegaRad * kRadToRpm; }

    [[nodiscard]] const EngineParameters&        QueryEngine() const noexcept { return Engine; }
    [[nodiscard]] const TurbochargerParameters&  QueryTurbo()  const noexcept { return Turbo; }
    [[nodiscard]] const TransmissionParameters&  QueryTransmission() const noexcept { return Trans; }
    [[nodiscard]] const ClutchParameters&        QueryClutch() const noexcept { return Clutch; }

    static constexpr float kRpmToRad = 0.104719755f;   // 2π/60
    static constexpr float kRadToRpm = 9.54929658f;    // 60/2π

private:
    float NetEngineTorque(float omega, float throttle, float boostMult, float clutchLoad) const noexcept;
    void  SpoolTurbo(float engineRPM, float throttle, float dt) noexcept;
    // Belt-driven supercharger: returns the torque multiplier for this step and writes the parasitic crank drag.
    float StepSupercharger(float engineRPM, float throttle, float dt, float& parasiticDrag_Nm) noexcept;
    void  Differentiate(float driveTorque, float leftRPM, float rightRPM, float& outL, float& outR) const noexcept;

    EngineParameters       Engine;
    TurbochargerParameters Turbo;
    SuperchargerParameters Super;
    TransmissionParameters Trans;
    ClutchParameters       Clutch;
    DifferentialParameters Diff;
    InductionType          Induction = InductionType::Turbocharged;
    bool                   RaceTune  = false;

    float EngineOmega   = 73.3f;   // rad/s (~700 rpm)
    float TurboOmegaRad = 0.0f;    // rad/s (turbo shaft speed)
    float BoostBar      = 0.0f;    // bar
    float BovPosition   = 0.0f;    // [0..1]
    float WastegatePosition = 0.0f;// [0..1]
    float SuperBoostBar = 0.0f;    // bar (supercharger, bypass-regulated)
};

} // namespace Frontier::Vehicle
