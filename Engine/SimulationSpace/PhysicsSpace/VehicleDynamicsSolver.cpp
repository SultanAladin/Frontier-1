/*============================================================================================================================================
                                                       VEHICLEDYNAMICSSOLVER.CPP
============================================================================================================================================*/
// 🧩 Complete JoltPhysics v5.6.0 vehicle simulation: Pacejka MF6.1, Newton slip, turbo/supercharger, fuel, 6 suspension designs & readings

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Constants/PhysicsConstants.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Frontier::SimulationSpace::PhysicsSpace
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERNAL MATH HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::Axis3D RotateByQuaternion(
    const RigidOrientationQuaternion& Q,
    const Mathematics::Axis3D&        V) noexcept
{
    const float Tx = 2.0f * (Q.Y * V.Z - Q.Z * V.Y);
    const float Ty = 2.0f * (Q.Z * V.X - Q.X * V.Z);
    const float Tz = 2.0f * (Q.X * V.Y - Q.Y * V.X);
    return {
        V.X + Q.W * Tx + (Q.Y * Tz - Q.Z * Ty),
        V.Y + Q.W * Ty + (Q.Z * Tx - Q.X * Tz),
        V.Z + Q.W * Tz + (Q.X * Ty - Q.Y * Tx),
    };
}

[[nodiscard]] inline Mathematics::Axis3D CrossAxis(
    const Mathematics::Axis3D& A,
    const Mathematics::Axis3D& B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
    };
}

[[nodiscard]] inline float DotAxis(
    const Mathematics::Axis3D& A,
    const Mathematics::Axis3D& B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

[[nodiscard]] inline Mathematics::Axis3D AddAxis(
    const Mathematics::Axis3D& A,
    const Mathematics::Axis3D& B) noexcept
{
    return { A.X + B.X, A.Y + B.Y, A.Z + B.Z };
}

[[nodiscard]] inline Mathematics::Axis3D SubAxis(
    const Mathematics::Axis3D& A,
    const Mathematics::Axis3D& B) noexcept
{
    return { A.X - B.X, A.Y - B.Y, A.Z - B.Z };
}

[[nodiscard]] inline Mathematics::Axis3D ScaleAxis(
    const Mathematics::Axis3D& V,
    float                      S) noexcept
{
    return { V.X * S, V.Y * S, V.Z * S };
}

[[nodiscard]] inline float LerpScalar(float A, float B, float T) noexcept
{
    return A + (B - A) * std::clamp(T, 0.0f, 1.0f);
}

[[nodiscard]] inline float MoveToward(float Current, float Target, float MaxStep) noexcept
{
    const float Delta = Target - Current;
    if (std::fabs(Delta) <= MaxStep)
    {
        return Target;
    }
    return Current + ((Delta > 0.0f) ? MaxStep : -MaxStep);
}

[[nodiscard]] inline float EvaluateSpringForceFromSpec(
    const SuspensionCornerSpecification& Spec,
    float                                CompressionMeters) noexcept
{
    const float X = std::max(CompressionMeters, 0.0f);
    switch (Spec.CurveCategory)
    {
        case SpringCurveCategory::Linear:
            return Spec.DerivedSpringRateNPerM * X;

        case SpringCurveCategory::Progressive:
        case SpringCurveCategory::Digressive:
        {
            const float X2 = X * X;
            const float X3 = X2 * X;
            return std::max(0.0f, Spec.CubicCoeffs.K1 * X + Spec.CubicCoeffs.K2 * X2 + Spec.CubicCoeffs.K3 * X3);
        }

        case SpringCurveCategory::DualRate:
        {
            const float TotalStroke = std::max(Spec.MaxBumpMeters + Spec.MaxDroopMeters, 0.05f);
            const float SplitX      = TotalStroke * 0.45f;
            const float SoftRate    = Spec.DerivedSpringRateNPerM * 0.78f;
            const float StiffRate   = Spec.DerivedSpringRateNPerM * std::max(Spec.HardeningFactor, 1.25f);
            if (X <= SplitX)
            {
                return SoftRate * X;
            }
            return SoftRate * SplitX + StiffRate * (X - SplitX);
        }
    }
    return Spec.DerivedSpringRateNPerM * X;
}

[[nodiscard]] inline float ClampFinite(float Candidate, float MinLimit, float MaxLimit) noexcept
{
    if (!std::isfinite(Candidate))
    {
        return 0.0f;
    }
    return std::clamp(Candidate, MinLimit, MaxLimit);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                         FACTORY PRESETS & SPECIFICATION HELPERS
//------------------------------------------------------------------------------------------------------------------------

void VehicleRigSpecification::ApplyTirePreset(PacejkaTireSpecification& Tire, TirePresetCategory Preset) noexcept
{
    switch (Preset)
    {
        case TirePresetCategory::Gt3SlickDry:
            Tire.NominalLoadFz0kN        = 6.8f;
            Tire.PBx1 = 22.0f; Tire.PBx2 = 24.0f; Tire.PBx3 = 0.42f;
            Tire.PCx1 = 1.78f; Tire.PDx1 = 1.88f; Tire.PDx2 = -0.065f; Tire.PDx3 = 0.028f;
            Tire.PEx1 = -0.15f; Tire.PEx2 = -0.20f;
            Tire.PBy1 = 19.2f; Tire.PBy2 = 15.5f; Tire.PBy3 = 0.09f;
            Tire.PCy1 = 1.72f; Tire.PDy1 = 1.90f; Tire.PDy2 = -0.11f; Tire.PDy3 = 0.18f;
            Tire.PEy1 = -1.42f; Tire.PEy2 = -0.42f;
            Tire.OptimalTempK = 368.15f; Tire.OverheatTempK = 408.15f;
            Tire.RollingResistBase = 0.0085f;
            break;

        case TirePresetCategory::StreetSuperSticky:
            Tire.NominalLoadFz0kN        = 5.8f;
            Tire.PBx1 = 19.5f; Tire.PBx2 = 20.0f; Tire.PBx3 = 0.35f;
            Tire.PCx1 = 1.72f; Tire.PDx1 = 1.52f; Tire.PDx2 = -0.055f; Tire.PDx3 = 0.022f;
            Tire.PEx1 = -0.12f; Tire.PEx2 = -0.16f;
            Tire.PBy1 = 17.0f; Tire.PBy2 = 13.5f; Tire.PBy3 = 0.08f;
            Tire.PCy1 = 1.65f; Tire.PDy1 = 1.56f; Tire.PDy2 = -0.09f; Tire.PDy3 = 0.14f;
            Tire.PEy1 = -1.20f; Tire.PEy2 = -0.45f;
            Tire.OptimalTempK = 353.15f; Tire.OverheatTempK = 398.15f;
            Tire.RollingResistBase = 0.0115f;
            break;

        case TirePresetCategory::StreetSemiSlick:
            Tire.NominalLoadFz0kN        = 5.5f;
            Tire.PBx1 = 17.5f; Tire.PBx2 = 18.0f; Tire.PBx3 = 0.30f;
            Tire.PCx1 = 1.68f; Tire.PDx1 = 1.32f; Tire.PDx2 = -0.05f; Tire.PDx3 = 0.020f;
            Tire.PBy1 = 15.5f; Tire.PBy2 = 12.0f; Tire.PBy3 = 0.07f;
            Tire.PCy1 = 1.60f; Tire.PDy1 = 1.35f; Tire.PDy2 = -0.08f; Tire.PDy3 = 0.12f;
            Tire.OptimalTempK = 345.15f; Tire.OverheatTempK = 390.15f;
            Tire.RollingResistBase = 0.013f;
            break;

        case TirePresetCategory::OffroadRallyDirt:
            Tire.NominalLoadFz0kN        = 6.2f;
            Tire.PBx1 = 12.5f; Tire.PBx2 = 11.0f; Tire.PBx3 = 0.20f;
            Tire.PCx1 = 1.52f; Tire.PDx1 = 1.05f; Tire.PDx2 = -0.04f; Tire.PDx3 = 0.015f;
            Tire.PBy1 = 11.0f; Tire.PBy2 = 9.5f;  Tire.PBy3 = 0.05f;
            Tire.PCy1 = 1.48f; Tire.PDy1 = 1.02f; Tire.PDy2 = -0.06f; Tire.PDy3 = 0.08f;
            Tire.OptimalTempK = 335.15f; Tire.OverheatTempK = 385.15f;
            Tire.RollingResistBase = 0.024f;
            break;
    }
    Tire.CompileCoefficients();
}

void VehicleRigSpecification::ApplySuspensionArchitectureDefaults(
    SuspensionCornerSpecification& Corner,
    SuspensionArchitectureCategory Architecture) noexcept
{
    Corner.Architecture = Architecture;
    switch (Architecture)
    {
        case SuspensionArchitectureCategory::DoubleWishbone:
            Corner.RollCenterHeightM  = 0.10f;
            Corner.CamberGainRadPerM  = -0.052f; // Strong negative camber recovery in bump
            Corner.BumpToeGainRadPerM = 0.004f;
            Corner.AntiDiveSquatFrac  = 0.25f;
            Corner.MotionRatio        = 0.88f;
            break;

        case SuspensionArchitectureCategory::MacPhersonStrut:
            Corner.RollCenterHeightM  = 0.045f;  // Very low roll center
            Corner.CamberGainRadPerM  = -0.022f; // Modest initial gain (flattens in deep bump)
            Corner.BumpToeGainRadPerM = -0.003f;
            Corner.AntiDiveSquatFrac  = 0.14f;
            Corner.MotionRatio        = 0.94f;
            break;

        case SuspensionArchitectureCategory::MultiLink:
            Corner.RollCenterHeightM  = 0.115f;
            Corner.CamberGainRadPerM  = -0.046f;
            Corner.BumpToeGainRadPerM = 0.014f;  // Passive bump toe-in stabilization
            Corner.AntiDiveSquatFrac  = 0.30f;
            Corner.MotionRatio        = 0.86f;
            break;

        case SuspensionArchitectureCategory::SolidAxle:
            Corner.RollCenterHeightM  = 0.36f;   // High rigid beam axle roll center
            Corner.CamberGainRadPerM  = 0.0f;    // Camber comes strictly from left-right axle tilt
            Corner.BumpToeGainRadPerM = 0.0f;
            Corner.AntiDiveSquatFrac  = 0.42f;
            Corner.MotionRatio        = 0.98f;
            break;

        case SuspensionArchitectureCategory::TrailingArm:
            Corner.RollCenterHeightM  = 0.18f;
            Corner.CamberGainRadPerM  = 0.015f;  // Slight positive camber tendency in bump/roll
            Corner.BumpToeGainRadPerM = 0.005f;
            Corner.AntiDiveSquatFrac  = 0.48f;   // Strong longitudinal anti-squat
            Corner.MotionRatio        = 0.82f;
            break;

        case SuspensionArchitectureCategory::Telescopic:
            Corner.RollCenterHeightM  = Corner.RestLengthMeters * Corner.TelescopicScale;
            Corner.CamberGainRadPerM  = -0.012f;
            Corner.BumpToeGainRadPerM = 0.0f;
            Corner.AntiDiveSquatFrac  = 0.10f;
            Corner.MotionRatio        = 1.0f;
            break;

        case SuspensionArchitectureCategory::Manual:
            break;
    }
}

void VehicleRigSpecification::PopulateDefaultCurves(VehicleRigSpecification& Spec) noexcept
{
    if (Spec.Engine.TorqueCurveNm.Count == 0u)
    {
        Spec.Engine.TorqueCurveNm.Append(700.0f,  230.0f);
        Spec.Engine.TorqueCurveNm.Append(1500.0f, 340.0f);
        Spec.Engine.TorqueCurveNm.Append(2500.0f, 435.0f);
        Spec.Engine.TorqueCurveNm.Append(3500.0f, 495.0f);
        Spec.Engine.TorqueCurveNm.Append(4800.0f, 520.0f);
        Spec.Engine.TorqueCurveNm.Append(5800.0f, 520.0f);
        Spec.Engine.TorqueCurveNm.Append(6600.0f, 486.0f);
        Spec.Engine.TorqueCurveNm.Append(7200.0f, 455.0f);
    }

    if (Spec.Engine.ExhaustFlowKgPerSec.Count == 0u)
    {
        Spec.Engine.ExhaustFlowKgPerSec.Append(700.0f,  0.045f);
        Spec.Engine.ExhaustFlowKgPerSec.Append(2000.0f, 0.130f);
        Spec.Engine.ExhaustFlowKgPerSec.Append(3500.0f, 0.240f);
        Spec.Engine.ExhaustFlowKgPerSec.Append(5000.0f, 0.360f);
        Spec.Engine.ExhaustFlowKgPerSec.Append(6500.0f, 0.460f);
        Spec.Engine.ExhaustFlowKgPerSec.Append(7200.0f, 0.510f);
    }

    if (Spec.Engine.ExhaustTempKelvin.Count == 0u)
    {
        Spec.Engine.ExhaustTempKelvin.Append(700.0f,  693.15f);
        Spec.Engine.ExhaustTempKelvin.Append(2000.0f, 903.15f);
        Spec.Engine.ExhaustTempKelvin.Append(4000.0f, 1073.15f);
        Spec.Engine.ExhaustTempKelvin.Append(6000.0f, 1173.15f);
        Spec.Engine.ExhaustTempKelvin.Append(7200.0f, 1223.15f);
    }

    if (Spec.Turbocharger.BoostPressureCurveBar.Count == 0u)
    {
        Spec.Turbocharger.BoostPressureCurveBar.Append(15000.0f,  0.00f);
        Spec.Turbocharger.BoostPressureCurveBar.Append(35000.0f,  0.10f);
        Spec.Turbocharger.BoostPressureCurveBar.Append(55000.0f,  0.32f);
        Spec.Turbocharger.BoostPressureCurveBar.Append(80000.0f,  0.72f);
        Spec.Turbocharger.BoostPressureCurveBar.Append(105000.0f, 1.08f);
        Spec.Turbocharger.BoostPressureCurveBar.Append(130000.0f, 1.38f);
        Spec.Turbocharger.BoostPressureCurveBar.Append(160000.0f, 1.62f);
        Spec.Turbocharger.BoostPressureCurveBar.Append(185000.0f, 1.68f);
    }

    if (Spec.Turbocharger.TorqueMultiplierCurve.Count == 0u)
    {
        Spec.Turbocharger.TorqueMultiplierCurve.Append(0.00f, 1.00f);
        Spec.Turbocharger.TorqueMultiplierCurve.Append(0.40f, 1.24f);
        Spec.Turbocharger.TorqueMultiplierCurve.Append(0.80f, 1.48f);
        Spec.Turbocharger.TorqueMultiplierCurve.Append(1.20f, 1.72f);
        Spec.Turbocharger.TorqueMultiplierCurve.Append(1.65f, 1.94f);
    }

    if (Spec.Supercharger.BoostPressureCurveBar.Count == 0u)
    {
        Spec.Supercharger.BoostPressureCurveBar.Append(1800.0f,  0.05f);
        Spec.Supercharger.BoostPressureCurveBar.Append(4500.0f,  0.26f);
        Spec.Supercharger.BoostPressureCurveBar.Append(9000.0f,  0.54f);
        Spec.Supercharger.BoostPressureCurveBar.Append(14000.0f, 0.78f);
        Spec.Supercharger.BoostPressureCurveBar.Append(19000.0f, 0.96f);
        Spec.Supercharger.BoostPressureCurveBar.Append(24000.0f, 1.12f);
    }

    if (Spec.Supercharger.TorqueMultiplierCurve.Count == 0u)
    {
        Spec.Supercharger.TorqueMultiplierCurve.Append(0.00f, 1.00f);
        Spec.Supercharger.TorqueMultiplierCurve.Append(0.40f, 1.25f);
        Spec.Supercharger.TorqueMultiplierCurve.Append(0.80f, 1.52f);
        Spec.Supercharger.TorqueMultiplierCurve.Append(1.20f, 1.76f);
    }

    if (Spec.Supercharger.ParasiticDragCurveNm.Count == 0u)
    {
        Spec.Supercharger.ParasiticDragCurveNm.Append(1800.0f,  6.0f);
        Spec.Supercharger.ParasiticDragCurveNm.Append(5000.0f,  14.0f);
        Spec.Supercharger.ParasiticDragCurveNm.Append(10000.0f, 28.0f);
        Spec.Supercharger.ParasiticDragCurveNm.Append(15000.0f, 46.0f);
        Spec.Supercharger.ParasiticDragCurveNm.Append(20000.0f, 66.0f);
        Spec.Supercharger.ParasiticDragCurveNm.Append(24000.0f, 84.0f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           LEGACY POWERTRAIN UTILITIES
//------------------------------------------------------------------------------------------------------------------------

float VehicleDynamicsSolver::EvaluateTorqueCurveNm(
    const ComponentClass::VehicleDynamicsSettings& Settings,
    float                                          Rpm) noexcept
{
    const auto& Pts = Settings.TorqueCurve;
    if (Pts.empty())
    {
        return 0.0f;
    }
    if (Pts.size() == 1u || Rpm <= Pts.front().Rpm)
    {
        return Pts.front().TorqueNm;
    }
    if (Rpm >= Pts.back().Rpm)
    {
        return Pts.back().TorqueNm;
    }

    std::size_t Seg = 0u;
    for (std::size_t Idx = 0u; Idx + 1u < Pts.size(); ++Idx)
    {
        if (Rpm <= Pts[Idx + 1u].Rpm)
        {
            Seg = Idx;
            break;
        }
    }

    const float X0   = Pts[Seg].Rpm;
    const float X1   = Pts[Seg + 1u].Rpm;
    const float Y0   = Pts[Seg].TorqueNm;
    const float Y1   = Pts[Seg + 1u].TorqueNm;
    const float Span = std::max(X1 - X0, 1.0e-5f);
    const float T    = std::clamp((Rpm - X0) / Span, 0.0f, 1.0f);

    switch (Settings.Interpolation)
    {
        case ComponentClass::CurveInterpolationForm::StepHold:
            return Y0;
        case ComponentClass::CurveInterpolationForm::Linear:
            return Y0 + (Y1 - Y0) * T;
        case ComponentClass::CurveInterpolationForm::EaseInOut:
        {
            const float SmoothT = 0.5f - 0.5f * std::cos(static_cast<float>(Constants::Pi) * T);
            return Y0 + (Y1 - Y0) * SmoothT;
        }
        case ComponentClass::CurveInterpolationForm::SmoothSpline:
        {
            const float P0 = (Seg > 0u) ? Pts[Seg - 1u].TorqueNm : Y0;
            const float P3 = (Seg + 2u < Pts.size()) ? Pts[Seg + 2u].TorqueNm : Y1;
            const float T2 = T * T;
            const float T3 = T2 * T;
            const float C0 = 2.0f * Y0;
            const float C1 = Y1 - P0;
            const float C2 = 2.0f * P0 - 5.0f * Y0 + 4.0f * Y1 - P3;
            const float C3 = -P0 + 3.0f * Y0 - 3.0f * Y1 + P3;
            return std::max(0.0f, 0.5f * (C0 + C1 * T + C2 * T2 + C3 * T3));
        }
    }
    return Y0;
}

void VehicleDynamicsSolver::StepPowertrain(
    const ComponentClass::VehicleDynamicsSettings& Settings,
    float                                          ThrottleNormalized,
    float                                          BrakeNormalized,
    double                                         DeltaSeconds,
    VehicleSimulationTelemetry&                    InOutTelemetry) noexcept
{
    const float Dt       = static_cast<float>(std::clamp(DeltaSeconds, 0.0, 0.1));
    const float Throttle = std::clamp(ThrottleNormalized, 0.0f, 1.0f);
    const float Brake    = std::clamp(BrakeNormalized, 0.0f, 1.0f);
    if (Dt <= 0.0f)
    {
        return;
    }

    constexpr float AirDensity      = 1.225f;
    constexpr float DriveEfficiency = 0.92f;
    constexpr float RollingCoeff    = 0.015f;
    constexpr float MaxBrakeTorque  = 2600.0f * 4.0f;
    constexpr float UpshiftRpm      = 6800.0f;
    constexpr float DownshiftRpm    = 2600.0f;

    const float Mass       = std::max(Settings.ChassisMassKg, 200.0f);
    const float TyreRadius = std::max(Settings.WheelRadiusMeters, 0.15f);
    const float GearRatio  = Settings.GearRatios[std::min<std::size_t>(InOutTelemetry.CurrentGearIndex, Settings.GearRatios.size() - 1u)] * Settings.FinalDriveRatio;

    const float WheelOmega = InOutTelemetry.SpeedMps / TyreRadius;
    const float WheelRpm   = (WheelOmega * 60.0f) / static_cast<float>(Constants::Tau);
    const float FreeRpm    = std::max(Settings.IdleRpm, WheelRpm * GearRatio);
    InOutTelemetry.Rpm     = std::clamp(
        InOutTelemetry.Rpm + (FreeRpm - InOutTelemetry.Rpm) * std::min(1.0f, Dt * 12.0f),
        Settings.IdleRpm,
        Settings.RedlineRpm);

    if (InOutTelemetry.Rpm >= UpshiftRpm && InOutTelemetry.CurrentGearIndex + 1u < Settings.GearRatios.size())
    {
        ++InOutTelemetry.CurrentGearIndex;
    }
    else if (InOutTelemetry.Rpm <= DownshiftRpm && InOutTelemetry.CurrentGearIndex > 0u && InOutTelemetry.SpeedMps > 2.0f)
    {
        --InOutTelemetry.CurrentGearIndex;
    }

    const float ActiveGear = Settings.GearRatios[std::min<std::size_t>(InOutTelemetry.CurrentGearIndex, Settings.GearRatios.size() - 1u)] * Settings.FinalDriveRatio;
    const float EngineNm   = EvaluateTorqueCurveNm(Settings, InOutTelemetry.Rpm) * Throttle;
    InOutTelemetry.EngineTorqueNm = EngineNm;

    const float DriveForce  = (EngineNm * ActiveGear * DriveEfficiency) / TyreRadius;
    const float BrakeForce  = (Brake * MaxBrakeTorque) / TyreRadius;
    const float AeroDrag    = 0.5f * AirDensity * Settings.DragCoefficient * Settings.FrontalAreaM2 * InOutTelemetry.SpeedMps * InOutTelemetry.SpeedMps;
    const float DownforceN  = 0.5f * AirDensity * Settings.AeroLiftAreaM2 * InOutTelemetry.SpeedMps * InOutTelemetry.SpeedMps;
    const float NormalLoadN = Mass * static_cast<float>(Constants::StandardGravity) + DownforceN;
    const float RollingDrag = RollingCoeff * NormalLoadN;

    const float NetForce = DriveForce - AeroDrag - RollingDrag - (InOutTelemetry.SpeedMps > 0.05f ? BrakeForce : 0.0f);
    InOutTelemetry.AccelerationMps2     = NetForce / Mass;
    InOutTelemetry.AeroDownforceNewtons = DownforceN;
    InOutTelemetry.SpeedMps             = std::max(0.0f, InOutTelemetry.SpeedMps + InOutTelemetry.AccelerationMps2 * Dt);
    InOutTelemetry.DistanceMeters      += InOutTelemetry.SpeedMps * Dt;
    InOutTelemetry.WheelAngleRadians   += (InOutTelemetry.SpeedMps / TyreRadius) * Dt;
}

float VehicleDynamicsSolver::EvaluateConstraintEditScalar(
    const ComponentClass::ConstraintComponentSettings& Settings) noexcept
{
    const float Pose = std::clamp(Settings.NormalizedEditPose, 0.0f, 1.0f);
    switch (Settings.Form)
    {
        case ComponentClass::PhysicsConstraintCategory::Hinge:
            return Settings.MinAngleDegrees + (Settings.MaxAngleDegrees - Settings.MinAngleDegrees) * Pose;
        case ComponentClass::PhysicsConstraintCategory::Slider:
            return Settings.MinTravelMeters + (Settings.MaxTravelMeters - Settings.MinTravelMeters) * Pose;
        case ComponentClass::PhysicsConstraintCategory::Spring:
            return -Settings.TravelEachWayMeters + 2.0f * Settings.TravelEachWayMeters * Pose;
        case ComponentClass::PhysicsConstraintCategory::Ball:
            return Settings.ConeAngleDegrees * (Pose * 2.0f - 1.0f);
        case ComponentClass::PhysicsConstraintCategory::Spin:
            return Settings.TargetRpm;
        case ComponentClass::PhysicsConstraintCategory::Tether:
            return Settings.RopeLengthMeters * Pose;
        case ComponentClass::PhysicsConstraintCategory::Dock:
            return Settings.DockConnected ? 1.0f : 0.0f;
        case ComponentClass::PhysicsConstraintCategory::Fixed:
            return 0.0f;
    }
    return 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      VEHICLE PHYSICS RIG — CONSTRUCTION & SETUP
//------------------------------------------------------------------------------------------------------------------------

VehiclePhysicsRig::VehiclePhysicsRig() noexcept
    : BoundaryExchange(256u)
{
}

VehicleRigSpecification VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(
    const ComponentClass::ComponentClassDefinition& ControlVehicleDefinition) noexcept
{
    VehicleRigSpecification Spec {};

    // Default ControlVehicle hardpoints (FL, FR, RL, RR) in Frontier right-handed Y-up meters (+X fwd, +Y up, +Z right)
    Spec.Corners[0u].LocalMountMeters = {  1.7274f, 0.0914f, -1.0475f };
    Spec.Corners[0u].FrontAxle        = true;
    Spec.Corners[0u].LeftSide         = true;
    Spec.Corners[0u].Steerable        = true;
    Spec.Corners[0u].Driven           = true;

    Spec.Corners[1u].LocalMountMeters = {  1.7274f, 0.0914f,  1.0475f };
    Spec.Corners[1u].FrontAxle        = true;
    Spec.Corners[1u].LeftSide         = false;
    Spec.Corners[1u].Steerable        = true;
    Spec.Corners[1u].Driven           = true;

    Spec.Corners[2u].LocalMountMeters = { -1.6686f, 0.0914f, -1.0475f };
    Spec.Corners[2u].FrontAxle        = false;
    Spec.Corners[2u].LeftSide         = true;
    Spec.Corners[2u].Steerable        = false;
    Spec.Corners[2u].Driven           = true;
    Spec.Corners[2u].HandbrakeWheel   = true;

    Spec.Corners[3u].LocalMountMeters = { -1.6686f, 0.0914f,  1.0475f };
    Spec.Corners[3u].FrontAxle        = false;
    Spec.Corners[3u].LeftSide         = false;
    Spec.Corners[3u].Steerable        = false;
    Spec.Corners[3u].Driven           = true;
    Spec.Corners[3u].HandbrakeWheel   = true;

    // Extract authored parameters directly from ControlVehicle's ComponentClassStorage
    float AuthoredTotalMassKg = 1300.0f;
    float AuthoredTyreRadius  = 0.50855f;

    const uint32_t RawSlotCount = ControlVehicleDefinition.Storage.QueryRawSlotCount();
    for (uint32_t SlotIdx = 0u; SlotIdx < RawSlotCount; ++SlotIdx)
    {
        if (!ControlVehicleDefinition.Storage.QuerySlotLive(SlotIdx))
        {
            continue;
        }
        const ComponentClass::ComponentInstanceHeader* HeaderPtr =
            ControlVehicleDefinition.Storage.InspectHeaderByIndex(SlotIdx);
        if (HeaderPtr == nullptr)
        {
            continue;
        }
        const auto& Header = *HeaderPtr;
        if (Header.Category == ComponentClass::ComponentCategory::VehicleDynamics)
        {
            const ComponentClass::VehicleDynamicsSettings* V =
                ControlVehicleDefinition.Storage.InspectVehicle(Header.Token);
            if (V != nullptr)
            {
                AuthoredTotalMassKg = std::max(V->ChassisMassKg, 400.0f);
                AuthoredTyreRadius  = std::max(V->WheelRadiusMeters, 0.20f);
                Spec.Engine.IdleRpm    = V->IdleRpm;
                Spec.Engine.RedlineRpm = V->RedlineRpm;
                Spec.Aerodynamics.BodyDragCoeff     = V->DragCoefficient;
                Spec.Aerodynamics.BodyFrontalAreaM2 = V->FrontalAreaM2;
                Spec.Transmission.FinalDriveRatio   = V->FinalDriveRatio;
                Spec.Transmission.ForwardGearCount  = std::min<uint32_t>(
                    static_cast<uint32_t>(V->GearRatios.size()),
                    MaxVehicleGearRatios);
                for (uint32_t G = 0u; G < Spec.Transmission.ForwardGearCount; ++G)
                {
                    Spec.Transmission.ForwardRatios[G] = V->GearRatios[G];
                }
                Spec.Engine.TorqueCurveNm.Clear();
                for (const auto& Pt : V->TorqueCurve)
                {
                    Spec.Engine.TorqueCurveNm.Append(Pt.Rpm, Pt.TorqueNm);
                }
            }
        }
        else if (Header.Category == ComponentClass::ComponentCategory::ConstraintInstance)
        {
            const ComponentClass::ConstraintComponentSettings* C =
                ControlVehicleDefinition.Storage.InspectConstraint(Header.Token);
            if (C != nullptr)
            {
                uint32_t CornerIdx = 4u;
                if (Header.Title == "SuspensionFL") CornerIdx = 0u;
                else if (Header.Title == "SuspensionFR") CornerIdx = 1u;
                else if (Header.Title == "SuspensionRL") CornerIdx = 2u;
                else if (Header.Title == "SuspensionRR") CornerIdx = 3u;

                if (CornerIdx < MaxVehicleWheelCorners)
                {
                    Spec.Corners[CornerIdx].LocalMountMeters = Header.LocalTransform.LocationMeters;
                    Spec.Corners[CornerIdx].Suspension.NaturalFrequencyHz = std::clamp(C->NaturalFrequencyHz, 1.2f, 4.5f);
                    Spec.Corners[CornerIdx].Suspension.DampingRatioBump   = std::clamp(C->DampingRatioZeta, 0.25f, 1.2f);
                    Spec.Corners[CornerIdx].Suspension.DampingRatioRebound= std::clamp(C->DampingRatioZeta * 1.25f, 0.35f, 1.4f);
                    Spec.Corners[CornerIdx].Suspension.MaxBumpMeters      = std::max(C->MaxTravelMeters, 0.08f);
                    Spec.Corners[CornerIdx].Suspension.MaxDroopMeters     = std::max(-C->MinTravelMeters, 0.08f);
                }
                else if (Header.Title == "SteerFL" || Header.Title == "SteerFR")
                {
                    Spec.SteeringRack.MaxSteerAngleRad = std::max(C->MaxAngleDegrees, 15.0f) * static_cast<float>(Constants::DegToRad);
                }
                else if (Header.Title == "RearWingConstraint")
                {
                    Spec.Aerodynamics.RearWingAttached = C->DockConnected;
                }
            }
        }
    }

    // Derive exact wheelbase & track widths from authored wheel mounts
    const float FrontX = 0.5f * (Spec.Corners[0u].LocalMountMeters.X + Spec.Corners[1u].LocalMountMeters.X);
    const float RearX  = 0.5f * (Spec.Corners[2u].LocalMountMeters.X + Spec.Corners[3u].LocalMountMeters.X);
    Spec.WheelbaseMeters  = std::max(std::fabs(FrontX - RearX), 1.5f);
    Spec.TrackWidthFrontM = std::max(std::fabs(Spec.Corners[1u].LocalMountMeters.Z - Spec.Corners[0u].LocalMountMeters.Z), 1.0f);
    Spec.TrackWidthRearM  = std::max(std::fabs(Spec.Corners[3u].LocalMountMeters.Z - Spec.Corners[2u].LocalMountMeters.Z), 1.0f);

    // Split authored curb mass into dry mass + full tank fuel mass
    const float InitialFuelMassKg = Spec.FuelDelivery.InitialFuelLiters * Spec.FuelDelivery.Fuel.DensityKgPerLiter;
    Spec.DryChassisMassKg = std::max(AuthoredTotalMassKg - InitialFuelMassKg, 500.0f);

    // Apply GT3/sport suspension architectures & street-super-sticky Pacejka MF6.1 tires
    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        VehicleRigSpecification::ApplySuspensionArchitectureDefaults(
            Spec.Corners[I].Suspension,
            Spec.Corners[I].FrontAxle
                ? SuspensionArchitectureCategory::DoubleWishbone
                : SuspensionArchitectureCategory::MultiLink);
        Spec.Corners[I].Suspension.CurveCategory = SpringCurveCategory::Progressive;
        VehicleRigSpecification::ApplyTirePreset(Spec.Corners[I].Tire, TirePresetCategory::Gt3SlickDry);
        Spec.Corners[I].Tire.OuterRadiusMeters = AuthoredTyreRadius;
        Spec.Corners[I].Tire.WheelInertiaKgM2  = 0.5f * Spec.Corners[I].Tire.WheelMassKg * AuthoredTyreRadius * AuthoredTyreRadius * 0.55f;
        Spec.Corners[I].Tire.CompileCoefficients();
    }

    // Center & axle differentials (AWD 40/60 sport bias with rear LSD)
    Spec.DrivetrainLayout              = DrivetrainLayoutCategory::AllWheelDrive;
    Spec.CenterDifferential.Category   = DifferentialCategory::LimitedSlip;
    Spec.CenterDifferential.BiasSplit  = 0.40f; // 40% front / 60% rear
    Spec.FrontDifferential.Category    = DifferentialCategory::LimitedSlip;
    Spec.FrontDifferential.PowerLockFactor = 0.30f;
    Spec.RearDifferential.Category     = DifferentialCategory::LimitedSlip;
    Spec.RearDifferential.PowerLockFactor  = 0.55f;

    VehicleRigSpecification::PopulateDefaultCurves(Spec);
    return Spec;
}

void VehiclePhysicsRig::CalibrateSuspensionSprings() noexcept
{
    const float Gravity = static_cast<float>(Constants::StandardGravity);
    const float TotalMassKg = std::max(LiveTotalMassKg, 300.0f);

    // Compute exact static corner sprung masses via longitudinal + lateral weight distribution
    const float FrontX = 0.5f * (Specification.Corners[0u].LocalMountMeters.X + Specification.Corners[1u].LocalMountMeters.X);
    const float RearX  = 0.5f * (Specification.Corners[2u].LocalMountMeters.X + Specification.Corners[3u].LocalMountMeters.X);
    const float L      = std::max(FrontX - RearX, 1.0f);
    const float ComX   = Specification.CenterOfMassOffsetM.X;
    const float FrontShare = std::clamp((ComX - RearX) / L, 0.30f, 0.70f);
    const float RearShare  = 1.0f - FrontShare;

    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        auto& CornerSpec = Specification.Corners[I].Suspension;
        CornerSpec.MotionRatio = std::clamp(CornerSpec.MotionRatio, 0.4f, 1.2f);
        const float CornerMassKg = TotalMassKg * (Specification.Corners[I].FrontAxle ? (0.5f * FrontShare) : (0.5f * RearShare));
        Wheels[I].SprungMassKg = CornerMassKg;

        // Wheel-rate spring stiffness from target natural frequency: k_w = m_s * (2*pi*f_n)^2
        const float OmegaN = static_cast<float>(Constants::Tau) * std::clamp(CornerSpec.NaturalFrequencyHz, 1.0f, 5.0f);
        const float WheelRateNPerM = CornerMassKg * OmegaN * OmegaN;
        CornerSpec.DerivedSpringRateNPerM = WheelRateNPerM;

        // Critical damping: c_crit = 2 * sqrt(k * m)
        const float CriticalDamp = 2.0f * std::sqrt(std::max(WheelRateNPerM * CornerMassKg, 1.0f));
        CornerSpec.DerivedBumpDamperNsPerM    = CriticalDamp * std::clamp(CornerSpec.DampingRatioBump, 0.2f, 1.4f);
        CornerSpec.DerivedReboundDamperNsPerM = CriticalDamp * std::clamp(CornerSpec.DampingRatioRebound, 0.25f, 1.6f);

        // Exact static sag x_static = g / omega_n^2 -> spring force at nominal ride height == m_s * g
        const float StaticSagMeters = std::clamp(Gravity / (OmegaN * OmegaN), 0.025f, std::max(CornerSpec.MaxDroopMeters, 0.04f));
        const float TotalStrokeM    = std::max(StaticSagMeters + CornerSpec.MaxBumpMeters, 0.08f);

        // Compute progressive/digressive cubic coefficients matching F(x_static) = m_s*g and F(TotalStroke) = Hardening * k * TotalStroke
        if (CornerSpec.CurveCategory == SpringCurveCategory::Progressive)
        {
            const float Hardening = std::max(CornerSpec.HardeningFactor, 1.05f);
            const float ExtraK3   = (WheelRateNPerM * (Hardening - 1.0f)) / std::max(TotalStrokeM * TotalStrokeM, 1.0e-4f);
            // Adjust K1 slightly so K1*x_s + K3*x_s^3 == m_s * g exactly at static ride height
            const float K1Exact   = std::max(
                WheelRateNPerM * 0.5f,
                (CornerMassKg * Gravity - ExtraK3 * StaticSagMeters * StaticSagMeters * StaticSagMeters) / StaticSagMeters);
            CornerSpec.CubicCoeffs = { K1Exact, 0.0f, ExtraK3 };
        }
        else if (CornerSpec.CurveCategory == SpringCurveCategory::Digressive)
        {
            const float InitialMult = std::max(CornerSpec.HardeningFactor, 1.10f);
            const float K1Init      = WheelRateNPerM * InitialMult;
            const float K2Soft      = -(WheelRateNPerM * (InitialMult - 1.0f)) / TotalStrokeM;
            CornerSpec.CubicCoeffs  = { K1Init, K2Soft, 0.0f };
        }
        else
        {
            CornerSpec.CubicCoeffs = { WheelRateNPerM, 0.0f, 0.0f };
        }

        // Compile Pacejka tire coefficients once
        Specification.Corners[I].Tire.CompileCoefficients();
    }
}

bool VehiclePhysicsRig::Initialize(
    PhysicsExtension&                 Physics,
    const VehicleRigSpecification&    InitialSpecification,
    const Mathematics::Axis3D&        SpawnPositionMeters,
    const RigidOrientationQuaternion& SpawnOrientation) noexcept
{
    if (!Physics.QueryInitialized())
    {
        return false;
    }

    Specification = InitialSpecification;
    VehicleRigSpecification::PopulateDefaultCurves(Specification);

    // Initialize fuel and live mass
    FuelLiters = std::clamp(Specification.FuelDelivery.InitialFuelLiters, 0.0f, std::max(Specification.FuelDelivery.TankCapacityLiters, 1.0f));
    FuelMassKg = FuelLiters * Specification.FuelDelivery.Fuel.DensityKgPerLiter;
    LiveTotalMassKg = Specification.DryChassisMassKg + (Specification.FuelDelivery.CoupleFuelMassToChassis ? FuelMassKg : 0.0f);
    LastAppliedJoltMassKg = LiveTotalMassKg;
    FuelStarved  = (FuelLiters <= 1.0e-4f);
    FuelWarning  = (FuelLiters / std::max(Specification.FuelDelivery.TankCapacityLiters, 1.0f)) <= Specification.FuelDelivery.WarningFraction;
    FuelCritical = (FuelLiters / std::max(Specification.FuelDelivery.TankCapacityLiters, 1.0f)) <= Specification.FuelDelivery.CriticalFraction;

    // Reset runtime variables
    EngineRpm           = Specification.Engine.IdleRpm;
    TurboShaftRpm       = 15000.0f;
    TurboBoostBar       = 0.0f;
    WastegatePos        = 0.0f;
    BovPos              = 0.0f;
    SuperchargerRpm     = EngineRpm * Specification.Supercharger.DriveRatio;
    SuperchargerBoostBar= 0.0f;
    SuperchargerBypass  = 1.0f;
    SuperchargerDragNm  = 0.0f;
    CombinedBoostBar    = 0.0f;
    BoostTorqueMult     = 1.0f;
    CoolantTempK        = StandardAmbientKelvin + 65.0f;
    OilTempK            = StandardAmbientKelvin + 68.0f;
    ManifoldPressureKPa = StandardAtmosphereKPa;
    ManifoldTempK       = StandardAmbientKelvin;
    ActiveGear          = 1;
    TargetGear          = 1;
    ShiftingActive      = false;
    ShiftElapsedSec     = 0.0f;
    ShiftCooldownRemain = 0.0f;
    ClutchEngagement    = 0.0f;
    ClutchLocked        = false;
    ClutchTorqueNm      = 0.0f;
    ClutchSlipRpm       = 0.0f;
    CurrentSteerAngleRad= 0.0f;
    PrevForwardSpeedMps = 0.0f;
    PrevLateralSpeedMps = 0.0f;
    ForwardSpeedMps     = 0.0f;
    LateralSpeedMps     = 0.0f;
    VerticalSpeedMps    = 0.0f;
    GroundSpeedMps      = 0.0f;
    SmoothedLongG       = 0.0f;
    SmoothedLatG        = 0.0f;
    DistanceTraveledM   = 0.0f;
    CompletedSteps      = 0ull;
    SimulatedSeconds    = 0.0;

    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        Wheels[I] = InternalWheelRuntime{};
        Wheels[I].TreadDepthMm    = Specification.Corners[I].Tire.InitialTreadDepthMm;
        Wheels[I].HubLocalMeters  = Specification.Corners[I].LocalMountMeters;
    }

    // Spawn Jolt rigid body representing the sprung chassis hull
    RigidBodyDescriptor ChassisDesc {};
    ChassisDesc.Shape.ShapeCategory    = CollisionShapeCategory::Box;
    ChassisDesc.Shape.HalfExtentsMeters= { Specification.ChassisHalfExtentsM.X, Specification.ChassisHalfExtentsM.Y, Specification.ChassisHalfExtentsM.Z };
    ChassisDesc.Shape.Friction         = 0.45f;
    ChassisDesc.Shape.Restitution      = 0.05f;
    ChassisDesc.MotionCategory         = RigidMotionCategory::Dynamic;
    ChassisDesc.ExplicitMassKg         = LiveTotalMassKg;
    ChassisDesc.LinearDamping          = 0.01f;
    ChassisDesc.AngularDamping         = 0.08f;
    ChassisDesc.AllowSleep             = false;
    ChassisDesc.PositionMeters         = SpawnPositionMeters;
    ChassisDesc.Orientation            = SpawnOrientation;
    ChassisDesc.CollisionChannel       = 2u;

    ChassisBody = Physics.IssueRigidBody(ChassisDesc);
    if (!ChassisBody.Valid())
    {
        return false;
    }

    CalibrateSuspensionSprings();
    Initialized = true;

    const RigidBodyReading SeedBody = Physics.InspectBodyReading(ChassisBody);
    PublishReading(SeedBody, 0.0f);
    return true;
}

void VehiclePhysicsRig::Finalize(PhysicsExtension& Physics) noexcept
{
    if (Initialized && ChassisBody.Valid())
    {
        (void)Physics.ReclaimRigidBody(ChassisBody);
    }
    ChassisBody = RigidBodyToken{};
    Initialized = false;
}

bool VehiclePhysicsRig::SubmitControlDirective(const VehicleControlDirective& Directive) noexcept
{
    return BoundaryExchange.EnqueueDirective(Directive);
}

VehiclePhysicsRig::VehicleReadingLease VehiclePhysicsRig::AcquireVehicleReading() const noexcept
{
    return BoundaryExchange.AcquireReadLease();
}

const VehiclePhysicsRig::VehicleReadingExchange& VehiclePhysicsRig::InspectVehicleReadingExchange() const noexcept
{
    return BoundaryExchange.AccessReadingExchange();
}

RigidBodyToken VehiclePhysicsRig::QueryChassisBodyToken() const noexcept
{
    return ChassisBody;
}

bool VehiclePhysicsRig::QueryInitialized() const noexcept
{
    return Initialized;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             DIRECTIVE APPLICATION
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::ApplyDirective(const VehicleControlDirective& Directive) noexcept
{
    switch (Directive.Category)
    {
        case VehicleDirectiveCategory::DriverInput:
            InputThrottle    = std::clamp(Directive.ThrottleNormalized, 0.0f, 1.0f);
            InputBrake       = std::clamp(Directive.BrakeNormalized, 0.0f, 1.0f);
            InputSteering    = std::clamp(Directive.SteeringNormalized, -1.0f, 1.0f);
            InputHandbrake   = std::clamp(Directive.HandbrakeNormalized, 0.0f, 1.0f);
            InputClutchPedal = std::clamp(Directive.ClutchPedalNormalized, 0.0f, 1.0f);
            InputReverse     = Directive.ReverseRequested;
            if (Directive.ShiftUpRequested)   PendingShiftUp   = true;
            if (Directive.ShiftDownRequested) PendingShiftDown = true;
            break;

        case VehicleDirectiveCategory::AssignForcedInduction:
            Specification.ForcedInduction            = Directive.ForcedInduction;
            Specification.Supercharger.DriveCategory = Directive.SuperchargerDrive;
            // Adjust compressor efficiency for requested supercharger architecture
            if (Directive.SuperchargerDrive == SuperchargerDriveCategory::Roots)
            {
                Specification.Supercharger.CompressorEff = 0.62f;
            }
            else if (Directive.SuperchargerDrive == SuperchargerDriveCategory::TwinScrew)
            {
                Specification.Supercharger.CompressorEff = 0.72f;
            }
            else
            {
                Specification.Supercharger.CompressorEff = 0.78f;
            }
            break;

        case VehicleDirectiveCategory::AssignSuspensionDesign:
            for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
            {
                const SuspensionArchitectureCategory Arch =
                    Specification.Corners[I].FrontAxle ? Directive.FrontArchitecture : Directive.RearArchitecture;
                VehicleRigSpecification::ApplySuspensionArchitectureDefaults(Specification.Corners[I].Suspension, Arch);
                Specification.Corners[I].Suspension.CurveCategory = Directive.SpringCurve;
            }
            CalibrateSuspensionSprings();
            break;

        case VehicleDirectiveCategory::RefuelTank:
            if (Directive.RefuelLiters <= 0.0f)
            {
                FuelLiters = Specification.FuelDelivery.TankCapacityLiters;
            }
            else
            {
                FuelLiters = std::clamp(FuelLiters + Directive.RefuelLiters, 0.0f, Specification.FuelDelivery.TankCapacityLiters);
            }
            FuelMassKg  = FuelLiters * Specification.FuelDelivery.Fuel.DensityKgPerLiter;
            FuelStarved = (FuelLiters <= 1.0e-4f);
            break;

        case VehicleDirectiveCategory::AssignRearWingAttached:
            Specification.Aerodynamics.RearWingAttached = Directive.RearWingAttached;
            break;

        case VehicleDirectiveCategory::AssignDriverAssists:
            Specification.Assists.AbsEnabled  = Directive.AbsEnabled;
            Specification.Assists.TcsCategory = Directive.TcsCategory;
            Specification.Assists.EscEnabled  = Directive.EscEnabled;
            break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      STAGE 1: SUSPENSION KINEMATICS & FORCES
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::SolveSuspensionKinematics(
    PhysicsExtension&          Physics,
    const RigidBodyReading&    Body,
    const Mathematics::Axis3D& AxisForward,
    const Mathematics::Axis3D& AxisUp,
    const Mathematics::Axis3D& AxisRight,
    float                      DeltaSeconds) noexcept
{
    const float Gravity = static_cast<float>(Constants::StandardGravity);
    const Mathematics::Axis3D DownDir = ScaleAxis(AxisUp, -1.0f);

    float MinFloorClearance = 1.5f;

    // First pass: Jolt ground raycasts & progressive spring/damper forces
    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        const auto& CornerSpec = Specification.Corners[I];
        const auto& SuspSpec   = CornerSpec.Suspension;
        const auto& TireSpec   = CornerSpec.Tire;
        auto&       W          = Wheels[I];

        const float OmegaN         = static_cast<float>(Constants::Tau) * std::clamp(SuspSpec.NaturalFrequencyHz, 1.0f, 5.0f);
        const float StaticSagM     = std::clamp(Gravity / (OmegaN * OmegaN), 0.025f, std::max(SuspSpec.MaxDroopMeters, 0.04f));
        const float TotalStrokeM   = StaticSagM + SuspSpec.MaxBumpMeters;
        const float FullDroopReach = SuspSpec.RestLengthMeters + StaticSagM + TireSpec.OuterRadiusMeters;

        const Mathematics::Axis3D StrutTopLocal {
            CornerSpec.LocalMountMeters.X,
            CornerSpec.LocalMountMeters.Y + SuspSpec.RestLengthMeters,
            CornerSpec.LocalMountMeters.Z,
        };
        const Mathematics::Axis3D StrutTopWorld = AddAxis(Body.PositionMeters, RotateByQuaternion(Body.Orientation, StrutTopLocal));

        RayCastQuery Ray {};
        Ray.OriginMeters      = StrutTopWorld;
        Ray.DirectionUnit     = DownDir;
        Ray.MaxDistanceMeters = FullDroopReach;
        Ray.ChannelMask       = 0xFFFFFFFFu;

        const RayCastHitReading Hit = Physics.ExecuteRayCastIgnoringBody(Ray, ChassisBody);
        W.PrevCompressionM = W.CompressionM;

        if (!Hit.HitDetected)
        {
            W.InContact        = false;
            W.CompressionM     = 0.0f;
            W.CompressionVelMps= 0.0f;
            W.SpringForceN     = 0.0f;
            W.DamperForceN     = 0.0f;
            W.AntiRollForceN   = 0.0f;
            W.VerticalLoadN    = 0.0f;
            W.HubLocalMeters   = {
                CornerSpec.LocalMountMeters.X,
                CornerSpec.LocalMountMeters.Y - StaticSagM,
                CornerSpec.LocalMountMeters.Z,
            };
            continue;
        }

        W.InContact          = true;
        W.ContactPointWorldM = Hit.WorldPoint;
        W.ContactNormalWorld = Mathematics::NormalizeAxis(Hit.SurfaceNormal, AxisUp);

        const float RawCompression = FullDroopReach - Hit.DistanceMeters;
        W.CompressionM = std::clamp(RawCompression, 0.0f, TotalStrokeM);

        // Compute contact-point chassis vertical velocity for low-noise damper velocity
        const Mathematics::Axis3D MountWorld = AddAxis(Body.PositionMeters, RotateByQuaternion(Body.Orientation, CornerSpec.LocalMountMeters));
        const Mathematics::Axis3D ArmWorld   = SubAxis(MountWorld, Body.PositionMeters);
        const Mathematics::Axis3D MountVel   = AddAxis(Body.LinearVelocity, CrossAxis(Body.AngularVelocity, ArmWorld));
        const float ChassisRelVelIntoGround  = -DotAxis(MountVel, AxisUp);
        const float FiniteDiffVel            = (W.CompressionM - W.PrevCompressionM) / std::max(DeltaSeconds, 1.0e-4f);
        W.CompressionVelMps                  = 0.5f * ChassisRelVelIntoGround + 0.5f * std::clamp(FiniteDiffVel, -6.0f, 6.0f);

        // True underbody floor clearance above ground at this corner
        const float CornerFloorHeight = std::max(
            0.005f,
            (Hit.DistanceMeters - SuspSpec.RestLengthMeters) - Specification.Aerodynamics.FloorOffsetMeters);
        MinFloorClearance = std::min(MinFloorClearance, CornerFloorHeight);

        // Visual wheel hub local offset
        const float DisplacementFromNominal = W.CompressionM - StaticSagM;
        W.HubLocalMeters = {
            CornerSpec.LocalMountMeters.X,
            CornerSpec.LocalMountMeters.Y + DisplacementFromNominal,
            CornerSpec.LocalMountMeters.Z,
        };

        // 1. Progressive / Digressive / DualRate / Linear spring force
        float SpringF = EvaluateSpringForceFromSpec(SuspSpec, W.CompressionM);

        // 2. Progressive rubber bump-stop near full jounce
        const float BumpStopThreshold = TotalStrokeM * SuspSpec.BumpStopEngagement;
        if (W.CompressionM > BumpStopThreshold)
        {
            const float Excess = W.CompressionM - BumpStopThreshold;
            SpringF += SuspSpec.BumpStopStiffness * Excess * (1.0f + 12.0f * Excess);
        }

        // 3. Asymmetric bump/rebound damper with high-speed digressive blow-off
        const float V = W.CompressionVelMps;
        float DamperF = 0.0f;
        if (V >= 0.0f)
        {
            const float SlowKnee = 0.30f; // [m/s]
            const float EffectiveV = (V <= SlowKnee) ? V : (SlowKnee + (V - SlowKnee) * SuspSpec.FastBumpMultiplier);
            DamperF = SuspSpec.DerivedBumpDamperNsPerM * EffectiveV;
        }
        else
        {
            DamperF = SuspSpec.DerivedReboundDamperNsPerM * V;
        }

        W.SpringForceN = SpringF;
        W.DamperForceN = DamperF;
        W.VerticalLoadN = ClampFinite(SpringF + DamperF, 0.0f, W.SprungMassKg * Gravity * 6.0f);
    }

    FloorClearanceM = (MinFloorClearance < 1.49f) ? MinFloorClearance : 0.60f;

    // Second pass: Architecture-specific dynamic camber & bump-toe kinematics
    const float ChassisRollRad = std::asin(std::clamp(DotAxis(AxisRight, { 0.0f, 1.0f, 0.0f }), -0.6f, 0.6f));
    const float FrontTrack     = std::max(Specification.TrackWidthFrontM, 1.0f);
    const float RearTrack      = std::max(Specification.TrackWidthRearM, 1.0f);

    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        const auto& CornerSpec = Specification.Corners[I];
        const auto& SuspSpec   = CornerSpec.Suspension;
        auto&       W          = Wheels[I];

        const float OmegaN     = static_cast<float>(Constants::Tau) * std::clamp(SuspSpec.NaturalFrequencyHz, 1.0f, 5.0f);
        const float StaticSagM = std::clamp(Gravity / (OmegaN * OmegaN), 0.025f, std::max(SuspSpec.MaxDroopMeters, 0.04f));
        const float BumpM      = W.CompressionM - StaticSagM;
        const float SideSign   = CornerSpec.LeftSide ? -1.0f : 1.0f;

        float DynamicCamber = SuspSpec.StaticCamberRad;
        float DynamicToe    = SuspSpec.StaticToeRad;

        switch (SuspSpec.Architecture)
        {
            case SuspensionArchitectureCategory::DoubleWishbone:
                // Unequal-length A-arms recover negative camber cleanly in bump & counter body roll
                DynamicCamber += SuspSpec.CamberGainRadPerM * BumpM + 0.35f * SideSign * ChassisRollRad;
                DynamicToe    += SuspSpec.BumpToeGainRadPerM * BumpM;
                break;

            case SuspensionArchitectureCategory::MacPhersonStrut:
            {
                // Strut geometry loses camber recovery past ~6 cm bump (quadratic reversal)
                const float StrutCurve = BumpM - 4.5f * BumpM * BumpM;
                DynamicCamber += SuspSpec.CamberGainRadPerM * StrutCurve + 0.72f * SideSign * ChassisRollRad;
                DynamicToe    += SuspSpec.BumpToeGainRadPerM * BumpM;
                break;
            }

            case SuspensionArchitectureCategory::MultiLink:
                // 5-link virtual instant center maintains negative camber and adds stabilizing toe-in on bump
                DynamicCamber += SuspSpec.CamberGainRadPerM * BumpM + 0.30f * SideSign * ChassisRollRad;
                DynamicToe    += SuspSpec.BumpToeGainRadPerM * std::max(BumpM, 0.0f);
                break;

            case SuspensionArchitectureCategory::SolidAxle:
            {
                // Rigid live axle couples left and right wheel camber directly through differential travel
                const uint32_t LeftIdx  = CornerSpec.FrontAxle ? 0u : 2u;
                const uint32_t RightIdx = CornerSpec.FrontAxle ? 1u : 3u;
                const float TrackM      = CornerSpec.FrontAxle ? FrontTrack : RearTrack;
                const float AxleTiltRad = std::atan2(Wheels[LeftIdx].CompressionM - Wheels[RightIdx].CompressionM, TrackM);
                DynamicCamber           = SuspSpec.StaticCamberRad + SideSign * AxleTiltRad;
                DynamicToe              = 0.0f;
                break;
            }

            case SuspensionArchitectureCategory::TrailingArm:
                // Trailing arm leans directly with body roll
                DynamicCamber += SuspSpec.CamberGainRadPerM * BumpM + 0.95f * SideSign * ChassisRollRad;
                DynamicToe    += SuspSpec.BumpToeGainRadPerM * BumpM;
                break;

            case SuspensionArchitectureCategory::Telescopic:
            case SuspensionArchitectureCategory::Manual:
                DynamicCamber += SuspSpec.CamberGainRadPerM * BumpM + 0.50f * SideSign * ChassisRollRad;
                break;
        }

        W.DynamicCamberRad  = std::clamp(DynamicCamber, -0.18f, 0.18f);
        W.DynamicBumpToeRad = std::clamp(DynamicToe, -0.05f, 0.05f);
    }

    (void)AxisForward;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      STAGE 2: UNIT-CONSISTENT ANTI-ROLL BARS
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::ComputeAntiRollForces() noexcept
{
    auto SolveAxleArb = [this](uint32_t LeftIdx, uint32_t RightIdx, const AntiRollbarSpecification& Arb) noexcept
    {
        auto& WL = Wheels[LeftIdx];
        auto& WR = Wheels[RightIdx];
        WL.AntiRollForceN = 0.0f;
        WR.AntiRollForceN = 0.0f;
        if (!Arb.Enabled || (!WL.InContact && !WR.InContact))
        {
            return;
        }

        // Unit-consistent differential wheel-travel ARB force: F_arb [N] = K [N/m] * (xL - xR) + C [N*s/m] * (vL - vR)
        const float DiffTravelM = WL.CompressionM - WR.CompressionM;
        const float DiffVelMps  = WL.CompressionVelMps - WR.CompressionVelMps;
        const float ArbForceN   = ClampFinite(
            Arb.StiffnessNPerM * DiffTravelM + Arb.DampingNsPerM * DiffVelMps,
            -18000.0f,
            18000.0f);

        if (WL.InContact)
        {
            WL.AntiRollForceN = ArbForceN;
            WL.VerticalLoadN  = std::max(0.0f, WL.VerticalLoadN + ArbForceN);
        }
        if (WR.InContact)
        {
            WR.AntiRollForceN = -ArbForceN;
            WR.VerticalLoadN  = std::max(0.0f, WR.VerticalLoadN - ArbForceN);
        }
    };

    SolveAxleArb(0u, 1u, Specification.FrontAntiRollbar);
    SolveAxleArb(2u, 3u, Specification.RearAntiRollbar);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    STAGE 3: ACKERMANN + 3-LAYER STEERING RACK
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::ComputeSteeringRack(
    float ForwardSpeedMpsIn,
    float LateralSpeedMpsIn,
    float DeltaSeconds) noexcept
{
    const auto& Rack = Specification.SteeringRack;
    const float SpeedAbs = std::fabs(ForwardSpeedMpsIn);

    // Layer 1: Speed-sensitive variable steering ratio
    float SpeedAuthority = 1.0f;
    if (Rack.EnableVariableRatio && SpeedAbs > Rack.VariableRatioStartMps)
    {
        const float Span = std::max(Rack.VariableRatioMaxMps - Rack.VariableRatioStartMps, 1.0f);
        const float T    = VehicleFastMath::SmoothUnitStep((SpeedAbs - Rack.VariableRatioStartMps) / Span);
        SpeedAuthority   = LerpScalar(1.0f, Rack.VariableRatioMinScale, T);
    }

    // Layer 2: Body slip-angle stability limiter (only attenuates over-steer-inducing turn-in, preserves counter-steer)
    float StabilityAuthority = 1.0f;
    if (Rack.EnableStabilityLimiter && SpeedAbs > 6.0f)
    {
        const float BodySlipRad = std::atan2(LateralSpeedMpsIn, SpeedAbs);
        const float SlipAbs     = std::fabs(BodySlipRad);
        if (SlipAbs > 0.04f)
        {
            const float T = VehicleFastMath::SmoothUnitStep(SlipAbs / std::max(Rack.CriticalBodySlipRad, 0.05f));
            StabilityAuthority = LerpScalar(1.0f, Rack.StabilityMinAuthority, T);
        }
    }

    const float TargetSteerRad = InputSteering * Rack.MaxSteerAngleRad * SpeedAuthority * StabilityAuthority;
    CurrentSteerAngleRad       = MoveToward(CurrentSteerAngleRad, TargetSteerRad, Rack.RackRateRadPerSec * DeltaSeconds);

    // Kinematic Ackermann inner/outer split (positive steer = right turn -> FR is inner wheel)
    const float L = std::max(Specification.WheelbaseMeters, 1.0f);
    const float T = std::max(Specification.TrackWidthFrontM, 0.8f);

    float LeftAngle  = CurrentSteerAngleRad;
    float RightAngle = CurrentSteerAngleRad;
    if (std::fabs(CurrentSteerAngleRad) > 1.0e-4f)
    {
        const float Sign   = (CurrentSteerAngleRad > 0.0f) ? 1.0f : -1.0f;
        const float AbsSteer = std::fabs(CurrentSteerAngleRad);
        const float TurnRadiusM = L / std::max(std::tan(AbsSteer), 1.0e-4f);
        const float InnerRad = std::atan2(L, std::max(TurnRadiusM - 0.5f * T, 0.35f));
        const float OuterRad = std::atan2(L, TurnRadiusM + 0.5f * T);

        const float BlendedInner = LerpScalar(AbsSteer, InnerRad, Rack.AckermannBlend);
        const float BlendedOuter = LerpScalar(AbsSteer, OuterRad, Rack.AckermannBlend);
        if (Sign > 0.0f)
        {
            // Turning right: Right wheel is inner, Left wheel is outer
            LeftAngle  =  BlendedOuter;
            RightAngle =  BlendedInner;
        }
        else
        {
            // Turning left: Left wheel is inner, Right wheel is outer
            LeftAngle  = -BlendedInner;
            RightAngle = -BlendedOuter;
        }
    }

    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        const float SideToeSign = Specification.Corners[I].LeftSide ? 1.0f : -1.0f;
        if (Specification.Corners[I].Steerable)
        {
            const float BaseSteer = Specification.Corners[I].LeftSide ? LeftAngle : RightAngle;
            Wheels[I].SteerAngleRad = BaseSteer + SideToeSign * Wheels[I].DynamicBumpToeRad;
        }
        else
        {
            Wheels[I].SteerAngleRad = SideToeSign * Wheels[I].DynamicBumpToeRad;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  STAGE 4: BODY-FRAME AERODYNAMICS & GROUND EFFECT
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::ComputeBodyAxisAerodynamics(
    float                      ForwardSpeedMpsIn,
    float                      LateralSpeedMpsIn,
    float                      VerticalSpeedMpsIn,
    float                      BrakeInput,
    const Mathematics::Axis3D& AxisForward,
    const Mathematics::Axis3D& AxisUp,
    const Mathematics::Axis3D& AxisRight,
    PhysicsExtension&          Physics,
    const RigidBodyReading&    Body) noexcept
{
    const auto& Aero = Specification.Aerodynamics;
    const float QLong = 0.5f * Aero.AirDensityKgPerM3 * ForwardSpeedMpsIn * ForwardSpeedMpsIn;
    const float QLat  = 0.5f * Aero.AirDensityKgPerM3 * LateralSpeedMpsIn * LateralSpeedMpsIn;

    // Ground-effect multiplier based on TRUE underbody floor clearance (decays properly toward 0 at high ride height)
    float GroundMult = 0.0f;
    if (FloorClearanceM < Aero.RideHeightCriticalMeters)
    {
        // Porpoising stall linear fade below critical clearance
        GroundMult = 0.60f * std::clamp(FloorClearanceM / std::max(Aero.RideHeightCriticalMeters, 0.005f), 0.0f, 1.0f);
    }
    else if (FloorClearanceM <= Aero.RideHeightOptimumMeters)
    {
        const float T = (FloorClearanceM - Aero.RideHeightCriticalMeters)
                      / std::max(Aero.RideHeightOptimumMeters - Aero.RideHeightCriticalMeters, 0.005f);
        GroundMult = LerpScalar(0.60f, 1.0f, T);
    }
    else
    {
        // Inverse power-law decay toward 0 (no artificial constant floor)
        const float Ratio = Aero.RideHeightOptimumMeters / std::max(FloorClearanceM, 0.01f);
        GroundMult = std::pow(Ratio, 1.25f);
    }
    GroundEffectMult = std::clamp(GroundMult, 0.0f, 1.05f);

    // Body drag, lift & lateral crossflow side force (resolved strictly in vehicle body axes)
    const float ForwardSign  = (ForwardSpeedMpsIn >= 0.0f) ? 1.0f : -1.0f;
    const float LateralSign  = (LateralSpeedMpsIn >= 0.0f) ? 1.0f : -1.0f;
    const float BodyDragN    = QLong * Aero.BodyFrontalAreaM2 * Aero.BodyDragCoeff;
    const float BodyLiftN    = QLong * Aero.BodyFrontalAreaM2 * Aero.BodyLiftCoeff;
    const float SideForceN   = -LateralSign * QLat * Aero.BodySideAreaM2 * Aero.BodySideForceCoeff;

    // Front splitter & underbody/diffuser + side skirts + vortex generators (forward airflow only)
    const float ForwardOnlyQ = (ForwardSpeedMpsIn > 0.5f) ? QLong : 0.0f;
    const float SplitterDfN  = ForwardOnlyQ * Aero.SplitterAreaM2 * Aero.SplitterDownforceCoeff * (0.45f + 0.55f * GroundEffectMult);
    const float UnderbodyDfN = ForwardOnlyQ * Aero.UnderbodyFloorAreaM2 * Aero.UnderbodySuctionCoeff
                             * Aero.SideSkirtSealingFactor * Aero.VortexGenDiffuserBonus * GroundEffectMult;
    const float UnderbodyDragN = UnderbodyDfN * 0.14f;

    // Rear wing (drops cleanly to 0 if RearWing Dock constraint broke off!)
    float WingDfN   = 0.0f;
    float WingDragN = 0.0f;
    if (Aero.RearWingAttached && ForwardSpeedMpsIn > 0.5f)
    {
        const float BrakeDeploy = 1.0f + Aero.WingBrakeDeployBoost * std::clamp(BrakeInput, 0.0f, 1.0f);
        WingDfN   = ForwardOnlyQ * Aero.WingAreaM2 * Aero.WingBaseDownforceCoeff * BrakeDeploy;
        WingDragN = ForwardOnlyQ * Aero.WingAreaM2 * Aero.WingBaseDragCoeff * BrakeDeploy;
    }

    AeroDragN            = BodyDragN + UnderbodyDragN + WingDragN;
    AeroDownforceN       = std::max(0.0f, SplitterDfN + UnderbodyDfN + WingDfN - BodyLiftN);
    AeroFrontDownforceN  = SplitterDfN + UnderbodyDfN * Aero.FrontDownforceShare;
    AeroRearDownforceN   = WingDfN + UnderbodyDfN * (1.0f - Aero.FrontDownforceShare);
    AeroSideForceN       = SideForceN;

    // Vertical aerodynamic damping against chassis heave
    const float VerticalDampN = -0.5f * Aero.AirDensityKgPerM3 * Aero.UnderbodyFloorAreaM2 * 0.4f
                              * VerticalSpeedMpsIn * std::fabs(VerticalSpeedMpsIn);

    // Apply body-frame drag, downforce & side force directly to Jolt rigid body
    const Mathematics::Axis3D WorldAeroForce = AddAxis(
        AddAxis(
            ScaleAxis(AxisForward, -ForwardSign * AeroDragN),
            ScaleAxis(AxisUp, -AeroDownforceN + VerticalDampN)),
        ScaleAxis(AxisRight, AeroSideForceN));
    Physics.ApplyForce(ChassisBody, WorldAeroForce);

    // Apply aerodynamic pitch balance moment (front vs rear downforce lever arm)
    const float HalfWheelbase = 0.5f * Specification.WheelbaseMeters;
    const float PitchMomentNm = (AeroFrontDownforceN - AeroRearDownforceN) * HalfWheelbase * 0.25f;
    Physics.ApplyTorque(ChassisBody, ScaleAxis(AxisRight, -PitchMomentNm));
    (void)Body;
}

//------------------------------------------------------------------------------------------------------------------------
//                          STAGE 5: COMPLETE FORCED INDUCTION (TURBO + SUPERCHARGER + TWINCHARGED)
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::ComputeForcedInduction(
    float EngineRpmIn,
    float Throttle,
    float DeltaSeconds) noexcept
{
    const float FuelOctane = Specification.FuelDelivery.Fuel.OctaneRating;
    const float AmbientK   = StandardAmbientKelvin;

    // Default clear values
    float TurboTargetMult  = 1.0f;
    float SuperTargetMult  = 1.0f;
    float EffectiveIntercoolerEff = 0.75f;
    float ActiveCompEff    = 0.72f;

    const bool RunTurbo = (Specification.ForcedInduction == ForcedInductionCategory::Turbocharged)
                       || (Specification.ForcedInduction == ForcedInductionCategory::Twincharged);
    const bool RunSuper = (Specification.ForcedInduction == ForcedInductionCategory::Supercharged)
                       || (Specification.ForcedInduction == ForcedInductionCategory::Twincharged);

    // A. Turbocharger (Heywood/Dixon/Serrano thermodynamics + active proportional wastegate + BOV)
    if (RunTurbo)
    {
        const auto& TSpec = Specification.Turbocharger;
        const float OctaneAlpha = std::clamp(
            (FuelOctane - TSpec.BaseTuneOctane) / std::max(TSpec.RaceTuneOctane - TSpec.BaseTuneOctane, 1.0f),
            0.0f,
            1.0f);
        const float WastegateCeilingBar = LerpScalar(TSpec.WastegateMaxBoostBaseBar, TSpec.WastegateMaxBoostRaceBar, OctaneAlpha);

        // Active proportional wastegate controller
        const float BoostError = TurboBoostBar - (WastegateCeilingBar - 0.12f);
        const float TargetWg   = std::clamp(BoostError / 0.16f, 0.0f, 1.0f);
        WastegatePos           = MoveToward(WastegatePos, TargetWg, 12.0f * DeltaSeconds);

        const float MaxFlowKgS      = Specification.Engine.ExhaustFlowKgPerSec.EvaluateLinear(EngineRpmIn);
        const float MaxExhTempK     = Specification.Engine.ExhaustTempKelvin.EvaluateLinear(EngineRpmIn);
        const float EffectiveThrot  = FuelStarved ? 0.0f : Throttle;
        const float RawExhaustFlow  = MaxFlowKgS * (0.16f + 0.84f * EffectiveThrot);
        const float TurbineFlowKgS  = RawExhaustFlow * (1.0f - 0.82f * WastegatePos);
        const float ExhaustTempK    = LerpScalar(573.15f, MaxExhTempK, EffectiveThrot);

        float TurboOmega = TurboShaftRpm * VehicleRpmToRadPerSec;

        // Isentropic turbine power
        constexpr float GammaExh = 1.33f;
        const float CpExh        = 1150.0f + 0.15f * (ExhaustTempK - 773.15f);
        const float ExpRatio     = 1.25f + 1.15f * EffectiveThrot * (1.0f - 0.45f * WastegatePos);
        const float IsenWorkTurb = CpExh * ExhaustTempK * (1.0f - std::pow(1.0f / ExpRatio, (GammaExh - 1.0f) / GammaExh));
        const float BladeRatio   = (TurboOmega * TSpec.TurbineWheelRadiusM) / std::sqrt(std::max(ExhaustTempK, 300.0f));
        const float EtaTurb      = TSpec.TurbineEfficiency * std::clamp(0.84f + 0.16f * BladeRatio, 0.68f, 1.0f);
        const float TurbinePowerW= TurbineFlowKgS * IsenWorkTurb * EtaTurb;

        // Isentropic compressor power with diabatic heat-transfer correction
        constexpr float GammaAir = 1.40f;
        constexpr float CpAir    = 1005.0f;
        const float PotentialBar = TSpec.BoostPressureCurveBar.EvaluateLinear(TurboShaftRpm);
        const float CompPR       = 1.0f + std::max(PotentialBar, 0.0f);
        const float IsenWorkComp = CpAir * AmbientK * (std::pow(CompPR, (GammaAir - 1.0f) / GammaAir) - 1.0f);
        const float SpeedFrac    = std::clamp(TurboShaftRpm / 95000.0f, 0.25f, 1.3f);
        const float EtaComp      = std::max(0.52f, TSpec.CompressorEfficiency - ((SpeedFrac < 0.6f) ? 0.06f * (1.0f - SpeedFrac / 0.6f) : 0.0f));
        const float CompFlowKgS  = RawExhaustFlow * 0.92f;
        const float CompPowerW   = (CompFlowKgS * IsenWorkComp) / EtaComp;

        // Serrano internal housing heat transfer + mechanical shaft friction
        const float TurbineHousingK = ExhaustTempK * 0.84f;
        const float CompHousingK    = AmbientK + PotentialBar * 16.0f;
        const float InternalHeatW   = TSpec.InternalHeatConductWK * std::max(0.0f, TurbineHousingK - CompHousingK) * 0.10f;

        const float FrictionTorque  = TSpec.FrictionLinearNmS * TurboOmega + TSpec.FrictionQuadNmS2 * TurboOmega * TurboOmega;
        const float NetPowerW       = (TurbinePowerW - CompPowerW) * TSpec.MechanicalEfficiency - FrictionTorque * TurboOmega - InternalHeatW;

        const float DrivingTorqueNm = NetPowerW / std::max(TurboOmega, 80.0f);
        const float InvInertia      = 1.0f / std::max(TSpec.TurboInertiaKgM2, 1.0e-5f);
        const float MaxStepOmega    = 3500.0f; // Anti-overshoot per-step clamp
        const float DeltaOmega      = std::clamp(DrivingTorqueNm * InvInertia * DeltaSeconds, -MaxStepOmega, MaxStepOmega);

        TurboOmega    = std::clamp(TurboOmega + DeltaOmega, 12000.0f * VehicleRpmToRadPerSec, TSpec.MaxShaftRpm * VehicleRpmToRadPerSec);
        TurboShaftRpm = TurboOmega * VehicleRadPerSecToRpm;

        // BOV surge protection & off-throttle pressure vent
        const bool ThrottleLift = (EffectiveThrot < 0.12f) && (TurboBoostBar > 0.10f);
        const bool SurgeRisk    = (CompPR > 1.35f + 0.4f * EffectiveThrot) && (EffectiveThrot < 0.15f);
        if (ThrottleLift || SurgeRisk)
        {
            BovPos        = MoveToward(BovPos, 1.0f, 14.0f * DeltaSeconds);
            TurboBoostBar = std::max(0.0f, TurboBoostBar - TSpec.BovReleaseRateBarPerSec * BovPos * DeltaSeconds);
        }
        else
        {
            BovPos = MoveToward(BovPos, 0.0f, 10.0f * DeltaSeconds);
            const float CappedPotential = std::min(TSpec.BoostPressureCurveBar.EvaluateLinear(TurboShaftRpm), WastegateCeilingBar);
            const float LagRate         = 4.5f + 6.0f * SpeedFrac;
            TurboBoostBar = LerpScalar(TurboBoostBar, CappedPotential * (0.20f + 0.80f * EffectiveThrot), std::min(1.0f, LagRate * DeltaSeconds));
        }

        TurboBoostBar           = std::clamp(TurboBoostBar, 0.0f, WastegateCeilingBar);
        TurboTargetMult         = TSpec.TorqueMultiplierCurve.EvaluateLinear(TurboBoostBar);
        EffectiveIntercoolerEff = TSpec.IntercoolerEfficiency;
        ActiveCompEff           = EtaComp;
    }
    else
    {
        TurboShaftRpm = 0.0f;
        TurboBoostBar = 0.0f;
        WastegatePos  = 0.0f;
        BovPos        = 0.0f;
    }

    // B. Supercharger (Roots / TwinScrew / Centrifugal + electronic bypass + real crank parasitic drag)
    if (RunSuper)
    {
        const auto& SSpec = Specification.Supercharger;
        const float OctaneAlpha = std::clamp(
            (FuelOctane - SSpec.BaseTuneOctane) / std::max(SSpec.RaceTuneOctane - SSpec.BaseTuneOctane, 1.0f),
            0.0f,
            1.0f);
        const float MaxBoostBar = LerpScalar(SSpec.MaxBoostBaseBar, SSpec.MaxBoostRaceBar, OctaneAlpha);

        SuperchargerRpm = std::min(EngineRpmIn * SSpec.DriveRatio, SSpec.MaxChargerRpm);
        const float NormSpeed = std::clamp(SuperchargerRpm / std::max(SSpec.MaxChargerRpm, 1000.0f), 0.0f, 1.0f);

        // Distinct physical pressure delivery curve per supercharger architecture
        float RawBoostBar = 0.0f;
        switch (SSpec.DriveCategory)
        {
            case SuperchargerDriveCategory::Roots:
                // Positive displacement: reaches strong plateau almost immediately off-idle
                RawBoostBar = MaxBoostBar * std::clamp(0.45f + 0.75f * std::sqrt(NormSpeed), 0.0f, 1.05f);
                break;

            case SuperchargerDriveCategory::TwinScrew:
                // Internal compression curve from specification
                RawBoostBar = SSpec.BoostPressureCurveBar.EvaluateLinear(SuperchargerRpm);
                break;

            case SuperchargerDriveCategory::Centrifugal:
                // Impeller pressure rises quadratically with rotor speed (P ~ omega^2)
                RawBoostBar = MaxBoostBar * (NormSpeed * NormSpeed) * 1.15f;
                break;
        }
        RawBoostBar = std::min(RawBoostBar, MaxBoostBar);

        // Electronic bypass valve opens at low throttle or in Twincharged mode once turbo takes over
        float TargetBypass = 1.0f - VehicleFastMath::SmoothUnitStep((Throttle - 0.12f) / 0.45f);
        if (FuelStarved)
        {
            TargetBypass = 1.0f;
        }
        if (Specification.ForcedInduction == ForcedInductionCategory::Twincharged)
        {
            // Seamless handoff: bypass supercharger progressively as turbo builds 0.45..0.95 bar
            const float TurboTakeover = VehicleFastMath::SmoothUnitStep((TurboBoostBar - 0.45f) / 0.50f);
            TargetBypass = std::max(TargetBypass, TurboTakeover);
        }

        SuperchargerBypass   = MoveToward(SuperchargerBypass, TargetBypass, SSpec.BypassValveRate * DeltaSeconds);
        SuperchargerBoostBar = RawBoostBar * (1.0f - SuperchargerBypass);

        // Real crank-belt parasitic torque load (reduced when bypass valve recirculates charge)
        const float FullDragNm = SSpec.ParasiticDragCurveNm.EvaluateLinear(SuperchargerRpm);
        const float DriveScale = (SSpec.DriveCategory == SuperchargerDriveCategory::Centrifugal)
                               ? (0.45f + 0.55f * NormSpeed)
                               : ((SSpec.DriveCategory == SuperchargerDriveCategory::Roots) ? 1.12f : 1.0f);
        SuperchargerDragNm     = FullDragNm * DriveScale * LerpScalar(SSpec.BypassDragFraction, 1.0f, 1.0f - SuperchargerBypass);
        SuperTargetMult        = SSpec.TorqueMultiplierCurve.EvaluateLinear(SuperchargerBoostBar);

        if (!RunTurbo)
        {
            EffectiveIntercoolerEff = SSpec.IntercoolerEff;
            ActiveCompEff           = SSpec.CompressorEff;
        }
    }
    else
    {
        SuperchargerRpm      = 0.0f;
        SuperchargerBoostBar = 0.0f;
        SuperchargerBypass   = 1.0f;
        SuperchargerDragNm   = 0.0f;
    }

    // C. Combine plenum boost, adiabatic charge temperature, intercooler & CORRECT absolute MAP [kPa]
    if (Specification.ForcedInduction == ForcedInductionCategory::Twincharged)
    {
        CombinedBoostBar = std::max(TurboBoostBar, SuperchargerBoostBar) + 0.25f * std::min(TurboBoostBar, SuperchargerBoostBar);
    }
    else if (RunTurbo)
    {
        CombinedBoostBar = TurboBoostBar;
    }
    else if (RunSuper)
    {
        CombinedBoostBar = SuperchargerBoostBar;
    }
    else
    {
        CombinedBoostBar = 0.0f;
    }

    const float PlenumPR     = 1.0f + std::max(CombinedBoostBar, 0.0f);
    const float CompOutTempK = AmbientK * (1.0f + (std::pow(PlenumPR, 0.2857f) - 1.0f) / std::max(ActiveCompEff, 0.45f));
    ManifoldTempK            = CompOutTempK - EffectiveIntercoolerEff * (CompOutTempK - AmbientK);

    // Charge-air thermal density factor (hotter air slightly lowers volumetric torque)
    const float DensityCorrection = std::pow(AmbientK / std::max(ManifoldTempK, AmbientK), 0.35f);

    if (CombinedBoostBar > 0.005f)
    {
        ManifoldPressureKPa = StandardAtmosphereKPa + CombinedBoostBar * 100.0f;
        const float BaseMult = std::max(TurboTargetMult, SuperTargetMult);
        BoostTorqueMult      = std::max(1.0f, 1.0f + (BaseMult - 1.0f) * DensityCorrection);
    }
    else
    {
        // Intake manifold vacuum under part/closed throttle (32 kPa idle vacuum -> 101.325 kPa WOT)
        ManifoldPressureKPa = LerpScalar(32.0f, StandardAtmosphereKPa, Throttle);
        BoostTorqueMult     = 1.0f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           STAGE 6: HYDRAULIC BRAKES, ROTOR THERMAL FADE, ABS & ESC
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::SolveBrakeHydraulics(
    float ForwardSpeedMpsIn,
    float YawRateRadS,
    float DeltaSeconds) noexcept
{
    const auto& BSpec   = Specification.Braking;
    const auto& Assists = Specification.Assists;

    const float PistonRadius = BSpec.PistonDiameterM * 0.5f;
    const float TotalAreaM2  = static_cast<float>(Constants::Pi) * PistonRadius * PistonRadius * static_cast<float>(BSpec.PistonCount);
    const float EffRadiusM   = 0.5f * (BSpec.DiskOuterRadiusM + BSpec.DiskInnerRadiusM);
    const float RotorCapJK   = std::max(BSpec.RotorMassKg * BSpec.SpecificHeatJPerKgK, 500.0f);

    AbsIntervening = false;
    EscIntervening = false;

    // ESC yaw-rate error computation (positive YawRateRadS = right turn, matching positive CurrentSteerAngleRad)
    float EscExtraTorque[MaxVehicleWheelCorners] { 0.0f, 0.0f, 0.0f, 0.0f };
    if (Assists.EscEnabled && std::fabs(ForwardSpeedMpsIn) > 8.0f)
    {
        const float DesiredYawRate = (ForwardSpeedMpsIn / std::max(Specification.WheelbaseMeters, 1.5f))
                                   * std::tan(CurrentSteerAngleRad);
        const float YawError       = YawRateRadS - DesiredYawRate; // >0 = oversteering right (or understeering left)
        if (std::fabs(YawError) > Assists.EscYawErrorDeadbandRadS)
        {
            EscIntervening = true;
            const float ExcessYaw = std::fabs(YawError) - Assists.EscYawErrorDeadbandRadS;
            const float CorrTorque= std::clamp(ExcessYaw * Assists.EscBrakeGainNmPerRadS, 0.0f, 2200.0f);
            if (YawError > 0.0f)
            {
                // Too much rightward yaw -> brake outer-left front (FL) to produce leftward counter-moment
                EscExtraTorque[0u] = CorrTorque;
            }
            else
            {
                // Too much leftward yaw -> brake outer-right front (FR) to produce rightward counter-moment
                EscExtraTorque[1u] = CorrTorque;
            }
        }
    }

    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        auto& W = Wheels[I];
        const bool IsFront = Specification.Corners[I].FrontAxle;
        const float MaxFootPa = IsFront ? BSpec.MaxFrontPressurePa : BSpec.MaxRearPressurePa;
        float TargetPa = InputBrake * MaxFootPa;

        if (Specification.Corners[I].HandbrakeWheel && InputHandbrake > 0.01f)
        {
            TargetPa = std::max(TargetPa, InputHandbrake * BSpec.MaxHandbrakePressurePa);
        }

        // ABS wheel-lock modulation (only modulates foot brake, never overrides handbrake)
        if (Assists.AbsEnabled && InputBrake > 0.05f && InputHandbrake < 0.05f
            && std::fabs(ForwardSpeedMpsIn) > Assists.AbsMinSpeedMps)
        {
            if (W.LongSlipKappa < Assists.AbsReleaseSlipRatio)
            {
                TargetPa *= 0.15f;
                AbsIntervening = true;
            }
            else if (W.LongSlipKappa < Assists.AbsTargetSlipRatio)
            {
                const float T = (W.LongSlipKappa - Assists.AbsTargetSlipRatio)
                              / std::min(Assists.AbsReleaseSlipRatio - Assists.AbsTargetSlipRatio, -0.01f);
                TargetPa *= LerpScalar(1.0f, 0.25f, std::clamp(T, 0.0f, 1.0f));
                AbsIntervening = true;
            }
        }

        // Hydraulic line pressure slew
        const float MaxRate = (TargetPa >= W.BrakePressurePa) ? BSpec.PressureRiseRatePaS : BSpec.PressureFallRatePaS;
        W.BrakePressurePa   = MoveToward(W.BrakePressurePa, TargetPa, MaxRate * DeltaSeconds);

        // Temperature-dependent pad friction coefficient (thermal fade)
        float PadMu = BSpec.FrictionCoeffCold;
        if (W.BrakeTempK >= BSpec.FadeEndTempK)
        {
            PadMu = BSpec.FrictionCoeffHot;
        }
        else if (W.BrakeTempK > BSpec.FadeStartTempK)
        {
            const float T = (W.BrakeTempK - BSpec.FadeStartTempK) / std::max(BSpec.FadeEndTempK - BSpec.FadeStartTempK, 1.0f);
            PadMu = LerpScalar(BSpec.FrictionCoeffCold, BSpec.FrictionCoeffHot, T);
        }

        // Brake torque on both faces of ventilated disk + ESC selective braking
        W.BrakeTorqueNm = 2.0f * PadMu * (W.BrakePressurePa * TotalAreaM2) * EffRadiusM + EscExtraTorque[I];

        // Rotor heat generation & speed-enhanced convective cooling
        const float HeatGenW  = W.BrakeTorqueNm * std::fabs(W.AngularVelocityRadS);
        const float AirFactor = 1.0f + 0.55f * std::sqrt(std::max(0.0f, GroundSpeedMps / 28.0f));
        const float CoolW     = (BSpec.ConvectionWPerM2K * AirFactor) * BSpec.RotorSurfaceAreaM2
                              * std::max(0.0f, W.BrakeTempK - StandardAmbientKelvin);
        W.BrakeTempK = std::clamp(
            W.BrakeTempK + ((HeatGenW - CoolW) / RotorCapJK) * DeltaSeconds,
            StandardAmbientKelvin,
            1173.15f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           STAGE 7: ENGINE, CLUTCH, TRANSMISSION & 3-DIFFERENTIAL DRIVETRAIN
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::SolveDrivetrainTorque(
    float ForwardSpeedMpsIn,
    float LateralGIn,
    float DeltaSeconds) noexcept
{
    const auto& ESpec = Specification.Engine;
    const auto& CSpec = Specification.Clutch;
    const auto& TSpec = Specification.Transmission;

    // 1. TCS traction slip detection (reduces engine torque if driven wheels spin past target)
    TcsIntervening = false;
    float TargetTcsScale = 1.0f;
    if (Specification.Assists.TcsCategory != TractionAssistCategory::Off && InputThrottle > 0.05f && ForwardSpeedMpsIn > 1.5f)
    {
        float SlipThresh = Specification.Assists.TcsTargetSlipRatio;
        if (Specification.Assists.TcsCategory == TractionAssistCategory::Comfort)      SlipThresh = 0.11f;
        else if (Specification.Assists.TcsCategory == TractionAssistCategory::Sport) SlipThresh = 0.16f;
        else if (Specification.Assists.TcsCategory == TractionAssistCategory::Track) SlipThresh = 0.24f;

        float MaxDrivenSlip = 0.0f;
        for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
        {
            if (Specification.Corners[I].Driven && Wheels[I].InContact)
            {
                MaxDrivenSlip = std::max(MaxDrivenSlip, Wheels[I].LongSlipKappa);
            }
        }
        if (MaxDrivenSlip > SlipThresh)
        {
            TcsIntervening = true;
            const float Excess = std::clamp((MaxDrivenSlip - SlipThresh) / 0.15f, 0.0f, 1.0f);
            TargetTcsScale = 1.0f - Excess * Specification.Assists.TcsMaxTorqueCut;
        }
    }
    TcsTorqueScale = MoveToward(TcsTorqueScale, TargetTcsScale, 12.0f * DeltaSeconds);

    // 2. Active gear selection & shift state machine
    if (ShiftCooldownRemain > 0.0f)
    {
        ShiftCooldownRemain = std::max(0.0f, ShiftCooldownRemain - DeltaSeconds);
    }

    if (ShiftingActive)
    {
        ShiftElapsedSec += DeltaSeconds;
        if (ShiftElapsedSec >= TSpec.ShiftDurationSec)
        {
            ActiveGear          = TargetGear;
            ShiftingActive      = false;
            ShiftElapsedSec     = 0.0f;
            ShiftCooldownRemain = TSpec.ShiftCooldownSec;
        }
    }
    else
    {
        const int32_t MaxGear = static_cast<int32_t>(std::max(TSpec.ForwardGearCount, 1u));
        if (InputReverse && ForwardSpeedMpsIn < 1.5f)
        {
            if (ActiveGear != -1)
            {
                TargetGear     = -1;
                ShiftingActive = true;
                ShiftElapsedSec= 0.0f;
            }
        }
        else if (PendingShiftUp && ActiveGear < MaxGear)
        {
            TargetGear     = (ActiveGear < 1) ? 1 : (ActiveGear + 1);
            ShiftingActive = true;
            ShiftElapsedSec= 0.0f;
        }
        else if (PendingShiftDown && ActiveGear > -1)
        {
            TargetGear     = (ActiveGear > 1) ? (ActiveGear - 1) : ((ForwardSpeedMpsIn < 1.5f) ? -1 : 1);
            ShiftingActive = true;
            ShiftElapsedSec= 0.0f;
        }
        else if (TSpec.AutomaticShifting && ShiftCooldownRemain <= 0.0f)
        {
            if (ActiveGear <= 0 && InputThrottle > 0.05f && !InputReverse)
            {
                ActiveGear = 1;
                TargetGear = 1;
            }
            else if (ActiveGear >= 1)
            {
                const bool HighLatHold = std::fabs(LateralGIn) > TSpec.CornerHoldLatG;
                const float UpRpm      = ESpec.RedlineRpm * TSpec.UpshiftRpmFrac;
                const float DownRpm    = ESpec.RedlineRpm * TSpec.DownshiftRpmFrac;
                if (EngineRpm >= UpRpm && ActiveGear < MaxGear && InputThrottle > 0.15f && !HighLatHold)
                {
                    TargetGear     = ActiveGear + 1;
                    ShiftingActive = true;
                    ShiftElapsedSec= 0.0f;
                }
                else if (EngineRpm <= DownRpm && ActiveGear > 1 && ForwardSpeedMpsIn > 3.0f)
                {
                    TargetGear     = ActiveGear - 1;
                    ShiftingActive = true;
                    ShiftElapsedSec= 0.0f;
                }
            }
        }
    }
    PendingShiftUp   = false;
    PendingShiftDown = false;

    float ActiveGearRatio = 0.0f;
    if (ActiveGear == -1)
    {
        ActiveGearRatio = TSpec.ReverseRatio;
    }
    else if (ActiveGear >= 1)
    {
        const uint32_t Idx = static_cast<uint32_t>(std::clamp(ActiveGear - 1, 0, static_cast<int32_t>(TSpec.ForwardGearCount) - 1));
        ActiveGearRatio    = TSpec.ForwardRatios[Idx];
    }
    const float TotalRatio     = ActiveGearRatio * TSpec.FinalDriveRatio;
    const float TotalRatioAbs  = std::fabs(TotalRatio);

    // 3. Measure driven-wheel average shaft speed reflected to transmission input
    float DrivenWheelOmegaSum = 0.0f;
    uint32_t DrivenCount      = 0u;
    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        if (Specification.Corners[I].Driven)
        {
            DrivenWheelOmegaSum += Wheels[I].AngularVelocityRadS;
            ++DrivenCount;
        }
    }
    const float AvgDrivenWheelOmega = (DrivenCount > 0u) ? (DrivenWheelOmegaSum / static_cast<float>(DrivenCount)) : 0.0f;
    const float TransInputOmega     = AvgDrivenWheelOmega * TotalRatio;

    // 4. Engine combustion torque + boost + FMEP friction + oil viscosity + supercharger parasitic drag + rev limiter
    const float EffectiveThrottle = FuelStarved ? 0.0f : (ShiftingActive ? InputThrottle * 0.15f : InputThrottle * TcsTorqueScale);
    const float EngineOmega       = EngineRpm * VehicleRpmToRadPerSec;

    // Cold oil increases viscous friction slightly until warm
    const float OilWarmAlpha   = std::clamp((OilTempK - StandardAmbientKelvin) / std::max(Specification.EngineOil.OptimalTempK - StandardAmbientKelvin, 1.0f), 0.0f, 1.0f);
    const float OilViscFactor  = LerpScalar(Specification.EngineOil.ColdViscosityMult, 1.0f, OilWarmAlpha);
    EngineBrakingNm            = (ESpec.EngineBrakingLinearNmS * EngineOmega + ESpec.EngineBrakingQuadNmS2 * EngineOmega * EngineOmega) * OilViscFactor;

    // Soft-cut rev limiter & thermal overheat derating
    float LimiterScale = 1.0f;
    if (EngineRpm > (ESpec.RedlineRpm - ESpec.SoftCutBandRpm))
    {
        LimiterScale = std::clamp((ESpec.RedlineRpm - EngineRpm) / std::max(ESpec.SoftCutBandRpm, 50.0f), 0.0f, 1.0f);
    }
    float ThermalDerate = 1.0f;
    if (CoolantTempK > ESpec.OverheatTempK)
    {
        ThermalDerate = std::clamp(1.0f - 0.35f * ((CoolantTempK - ESpec.OverheatTempK) / 20.0f), 0.65f, 1.0f);
    }

    // Idle governor keeps engine spinning smoothly at idle when fuel is available
    float IdleGovernorNm = 0.0f;
    if (!FuelStarved && EngineRpm < ESpec.IdleRpm + 180.0f)
    {
        const float Deficit = std::max(0.0f, ESpec.IdleRpm - EngineRpm);
        IdleGovernorNm      = EngineBrakingNm + SuperchargerDragNm + Deficit * 0.45f;
    }

    const float BaseCurveNm    = ESpec.TorqueCurveNm.EvaluateLinear(EngineRpm);
    const float GrossCombustNm = BaseCurveNm * BoostTorqueMult * EffectiveThrottle * LimiterScale * ThermalDerate;
    EngineTorqueNm             = std::max(GrossCombustNm, IdleGovernorNm) - EngineBrakingNm * (1.0f - 0.55f * EffectiveThrottle) - SuperchargerDragNm;
    EnginePowerKw              = std::max(0.0f, EngineTorqueNm * EngineOmega * 0.001f);

    // 5. Clutch engagement & hyperbolic saturation torque transfer (GRIT validated launch/lockup model)
    float TargetEngagement = 0.0f;
    const bool LaunchWin   = (TotalRatioAbs > 0.01f) && (InputThrottle > 0.01f) && (std::fabs(ForwardSpeedMpsIn) < CSpec.LaunchSpeedThresholdMps);
    if (CSpec.Actuation == ClutchActuationCategory::ManualPedal)
    {
        TargetEngagement = 1.0f - InputClutchPedal;
    }
    else if (ShiftingActive)
    {
        TargetEngagement = (CSpec.Actuation == ClutchActuationCategory::DualClutchOverlap) ? 0.25f : 0.0f;
    }
    else if (TotalRatioAbs < 0.01f)
    {
        TargetEngagement = 0.0f;
    }
    else if (LaunchWin)
    {
        const float RpmAlpha   = VehicleFastMath::SmoothUnitStep((EngineRpm - ESpec.IdleRpm) / std::max(CSpec.LaunchLockRpmDelta, 100.0f));
        const float ThrotAlpha = VehicleFastMath::SmoothUnitStep((InputThrottle - 0.02f) / 0.28f);
        TargetEngagement       = std::clamp(LerpScalar(CSpec.LaunchBiteEngagement, 1.0f, RpmAlpha) * ThrotAlpha, 0.0f, 1.0f);
    }
    else if ((InputBrake > 0.10f && std::fabs(ForwardSpeedMpsIn) < CSpec.LaunchSpeedThresholdMps && InputThrottle < 0.05f)
          || (EngineRpm < ESpec.IdleRpm + 180.0f && InputThrottle < 0.05f))
    {
        TargetEngagement = 0.0f;
    }
    else
    {
        TargetEngagement = 1.0f;
    }

    const float EngageRate = (TargetEngagement < ClutchEngagement)
                           ? (1.0f / std::max(CSpec.DisengagementSeconds, 0.005f))
                           : (LaunchWin ? CSpec.LaunchEngagementRate
                                        : (ShiftingActive ? (1.0f / std::max(CSpec.EngagementSeconds, 0.005f)) : CSpec.CruiseEngagementRate));
    ClutchEngagement = MoveToward(ClutchEngagement, TargetEngagement, EngageRate * DeltaSeconds);

    const float SlipOmega   = EngineOmega - TransInputOmega;
    ClutchSlipRpm           = SlipOmega * VehicleRadPerSecToRpm;
    const float CapacityNm  = CSpec.MaxTorqueCapacityNm * ClutchEngagement;
    ClutchLocked            = (ClutchEngagement > 0.96f) && (std::fabs(ClutchSlipRpm) < CSpec.LockSlipRpm) && (TotalRatioAbs > 0.01f);

    if (CapacityNm > 1.0e-3f)
    {
        const float BreakawayRadS = std::max(CSpec.BreakawaySlipRpm * VehicleRpmToRadPerSec, 1.0f);
        const float Sat           = SlipOmega / std::sqrt(SlipOmega * SlipOmega + BreakawayRadS * BreakawayRadS);
        ClutchTorqueNm            = CapacityNm * Sat;
        if (ClutchLocked)
        {
            // When locked, blend direct flywheel torque smoothly with elastic slip damping
            ClutchTorqueNm = std::clamp(EngineTorqueNm + SlipOmega * 12.0f, -CapacityNm, CapacityNm);
        }
    }
    else
    {
        ClutchTorqueNm = 0.0f;
    }

    // 6. Integrate engine crankshaft speed (semi-implicit with coupled inertia when locked)
    const float NetCrankTorqueNm = EngineTorqueNm - ClutchTorqueNm;
    const float CoupledInertia   = ClutchLocked
                                 ? (ESpec.EngineInertiaKgM2 + 0.45f)
                                 : std::max(ESpec.EngineInertiaKgM2, 0.08f);
    float NextEngineOmega = EngineOmega + (NetCrankTorqueNm / CoupledInertia) * DeltaSeconds;
    if (ClutchLocked && TotalRatioAbs > 0.01f)
    {
        // Softly track reflected drivetrain speed to avoid numerical drift
        NextEngineOmega = LerpScalar(NextEngineOmega, std::max(TransInputOmega, ESpec.IdleRpm * VehicleRpmToRadPerSec), std::min(1.0f, 18.0f * DeltaSeconds));
    }
    const float MinRpm = FuelStarved ? 0.0f : (ESpec.IdleRpm * 0.85f);
    EngineRpm = std::clamp(NextEngineOmega * VehicleRadPerSecToRpm, MinRpm, ESpec.RedlineRpm * 1.02f);

    // 7. Transmission output torque -> Center Differential -> Front & Rear Axle Differentials
    const float TransOutputTorqueNm = ClutchTorqueNm * TotalRatio * TSpec.DrivetrainEfficiency;

    auto SplitDifferentialTorque = [](
        float                          InputTorqueNm,
        float                          OmegaA,
        float                          OmegaB,
        float                          SteerRad,
        const DifferentialSpecification& Diff,
        float&                         OutTorqueA,
        float&                         OutTorqueB) noexcept
    {
        const float BaseA = InputTorqueNm * Diff.BiasSplit;
        const float BaseB = InputTorqueNm * (1.0f - Diff.BiasSplit);
        const float DeltaOmega = OmegaA - OmegaB;

        switch (Diff.Category)
        {
            case DifferentialCategory::Open:
                OutTorqueA = BaseA;
                OutTorqueB = BaseB;
                break;

            case DifferentialCategory::LimitedSlip:
            {
                if (std::fabs(DeltaOmega) <= Diff.SlipDeadbandRadS)
                {
                    OutTorqueA = BaseA;
                    OutTorqueB = BaseB;
                    break;
                }
                const float Ramp     = (InputTorqueNm >= 0.0f) ? Diff.PowerLockFactor : Diff.CoastLockFactor;
                const float MaxBias  = Diff.PreloadNm + Ramp * std::fabs(InputTorqueNm) * 0.5f;
                const float Viscous  = std::fabs(DeltaOmega) * Diff.ViscousDampingNmS;
                const float Transfer = std::min(MaxBias, Diff.PreloadNm + Viscous);
                const float Sign     = (DeltaOmega > 0.0f) ? 1.0f : -1.0f; // Shift torque away from faster-spinning wheel
                OutTorqueA           = BaseA - Sign * Transfer;
                OutTorqueB           = BaseB + Sign * Transfer;
                break;
            }

            case DifferentialCategory::Locked:
            {
                const float LockTransfer = std::clamp(DeltaOmega * 120.0f, -std::fabs(InputTorqueNm) - 400.0f, std::fabs(InputTorqueNm) + 400.0f);
                OutTorqueA = BaseA - LockTransfer;
                OutTorqueB = BaseB + LockTransfer;
                break;
            }

            case DifferentialCategory::TorqueVectoring:
            {
                // Actively bias outer wheel under cornering (+Steer = right turn -> bias left outer wheel A)
                const float TvTransfer = std::clamp(SteerRad * Diff.TorqueVectorGain * InputTorqueNm, -0.40f * std::fabs(InputTorqueNm), 0.40f * std::fabs(InputTorqueNm));
                const float LsdDamp    = (std::fabs(DeltaOmega) > Diff.SlipDeadbandRadS)
                                       ? std::clamp(DeltaOmega * Diff.ViscousDampingNmS, -250.0f, 250.0f)
                                       : 0.0f;
                OutTorqueA = BaseA + TvTransfer - LsdDamp;
                OutTorqueB = BaseB - TvTransfer + LsdDamp;
                break;
            }
        }
    };

    float FrontAxleTorqueNm = 0.0f;
    float RearAxleTorqueNm  = 0.0f;
    const float AvgFrontOmega = 0.5f * (Wheels[0u].AngularVelocityRadS + Wheels[1u].AngularVelocityRadS);
    const float AvgRearOmega  = 0.5f * (Wheels[2u].AngularVelocityRadS + Wheels[3u].AngularVelocityRadS);

    switch (Specification.DrivetrainLayout)
    {
        case DrivetrainLayoutCategory::RearWheelDrive:
            RearAxleTorqueNm = TransOutputTorqueNm;
            break;

        case DrivetrainLayoutCategory::FrontWheelDrive:
            FrontAxleTorqueNm = TransOutputTorqueNm;
            break;

        case DrivetrainLayoutCategory::AllWheelDrive:
            SplitDifferentialTorque(
                TransOutputTorqueNm,
                AvgFrontOmega,
                AvgRearOmega,
                0.0f,
                Specification.CenterDifferential,
                FrontAxleTorqueNm,
                RearAxleTorqueNm);
            break;
    }

    SplitDifferentialTorque(
        FrontAxleTorqueNm,
        Wheels[0u].AngularVelocityRadS,
        Wheels[1u].AngularVelocityRadS,
        CurrentSteerAngleRad,
        Specification.FrontDifferential,
        Wheels[0u].DriveTorqueNm,
        Wheels[1u].DriveTorqueNm);

    SplitDifferentialTorque(
        RearAxleTorqueNm,
        Wheels[2u].AngularVelocityRadS,
        Wheels[3u].AngularVelocityRadS,
        CurrentSteerAngleRad,
        Specification.RearDifferential,
        Wheels[2u].DriveTorqueNm,
        Wheels[3u].DriveTorqueNm);
}

//------------------------------------------------------------------------------------------------------------------------
//                           STAGE 8: PACEJKA MF6.1 + IMPLICIT NEWTON SLIP + KARNOPP STICTION
//------------------------------------------------------------------------------------------------------------------------

float VehiclePhysicsRig::EvaluatePacejkaPureFx(
    uint32_t CornerIdx,
    float    Kappa,
    float    CamberRad,
    float    GripScale) const noexcept
{
    const auto& Tire = Specification.Corners[CornerIdx].Tire;
    const float FzN  = Wheels[CornerIdx].VerticalLoadN;
    if (FzN <= 10.0f)
    {
        return 0.0f;
    }
    const float FzKN = FzN * 0.001f;
    const float Dfz  = (FzKN - Tire.NominalLoadFz0kN) * Tire.Compiled.InvFz0kN;

    // Load-sensitive peak friction mu_x
    const float LoadFactor = std::max(Tire.LoadSensitivityMinFactor, 1.0f - Tire.LoadSensitivityLong * std::max(0.0f, Dfz));
    const float MuX        = std::max(0.25f, (Tire.Compiled.PDx1Scaled + Tire.PDx2 * Dfz) * (1.0f - Tire.PDx3 * CamberRad * CamberRad) * LoadFactor * GripScale);
    const float Dx         = MuX * FzN;
    const float Cx         = Tire.Compiled.CxScaled;
    const float Kx         = FzN * (Tire.PBx1 + Tire.PBx2 * Dfz) * std::exp(-0.10f * Dfz);
    const float Bx         = Kx / std::max(Cx * Dx, 1.0f);
    const float Ex         = std::min(0.98f, (Tire.PEx1 + Tire.PEx2 * Dfz + Tire.PEx3 * Dfz * Dfz) * (1.0f - Tire.PEx4 * ((Kappa >= 0.0f) ? 1.0f : -1.0f)));

    const float BxK = Bx * Kappa;
    return Dx * VehicleFastMath::FastSin(Cx * VehicleFastMath::FastAtan(BxK - Ex * (BxK - VehicleFastMath::FastAtan(BxK))));
}

float VehiclePhysicsRig::EvaluatePacejkaPureFy(
    uint32_t CornerIdx,
    float    AlphaRad,
    float    CamberRad,
    float    GripScale) const noexcept
{
    const auto& Tire = Specification.Corners[CornerIdx].Tire;
    const float FzN  = Wheels[CornerIdx].VerticalLoadN;
    if (FzN <= 10.0f)
    {
        return 0.0f;
    }
    const float FzKN = FzN * 0.001f;
    const float Dfz  = (FzKN - Tire.NominalLoadFz0kN) * Tire.Compiled.InvFz0kN;

    const float LoadFactor = std::max(Tire.LoadSensitivityMinFactor, 1.0f - Tire.LoadSensitivityLat * std::max(0.0f, Dfz));
    const float MuY        = std::max(0.25f, (Tire.Compiled.PDy1Scaled + Tire.PDy2 * Dfz) * (1.0f - Tire.PDy3 * CamberRad * CamberRad) * LoadFactor * GripScale);
    const float Dy         = MuY * FzN;
    const float Cy         = Tire.Compiled.CyScaled;
    const float Ky         = FzN * (Tire.PBy1 + Tire.PBy2 * Dfz) * std::exp(-0.12f * Dfz);
    const float By         = Ky / std::max(Cy * Dy, 1.0f);
    const float Ey         = std::min(0.98f, (Tire.PEy1 + Tire.PEy2 * Dfz) * (1.0f - Tire.PEy3 * ((AlphaRad >= 0.0f) ? 1.0f : -1.0f)));

    const float ByA    = By * AlphaRad;
    const float FySlip = Dy * VehicleFastMath::FastSin(Cy * VehicleFastMath::FastAtan(ByA - Ey * (ByA - VehicleFastMath::FastAtan(ByA))));
    // Active camber thrust (negative camber on outside wheel generates inward cornering thrust)
    const float SideSign = Specification.Corners[CornerIdx].LeftSide ? -1.0f : 1.0f;
    const float CamberThrustN = SideSign * Tire.CamberThrustGain * CamberRad * FzN;
    return FySlip + CamberThrustN;
}

void VehiclePhysicsRig::EvaluatePacejkaCombined(
    uint32_t CornerIdx,
    float    Kappa,
    float    AlphaRad,
    float    CamberRad,
    float    GripScale,
    float&   OutFxN,
    float&   OutFyN,
    float&   OutMzNm) const noexcept
{
    const auto& Tire = Specification.Corners[CornerIdx].Tire;
    const float FxPure = EvaluatePacejkaPureFx(CornerIdx, Kappa, CamberRad, GripScale);
    const float FyPure = EvaluatePacejkaPureFy(CornerIdx, AlphaRad, CamberRad, GripScale);

    // MF6.1 combined-slip cosine weighting functions Gx_alpha and Gy_kappa
    const float Bxa = Tire.RBx1 * VehicleFastMath::FastCos(VehicleFastMath::FastAtan(Tire.RBx2 * Kappa));
    const float Gxa = std::clamp(VehicleFastMath::FastCos(Tire.RCx1 * VehicleFastMath::FastAtan(Bxa * AlphaRad)), 0.15f, 1.0f);

    const float Byk = Tire.RBy1 * VehicleFastMath::FastCos(VehicleFastMath::FastAtan(Tire.RBy2 * AlphaRad));
    const float Gyk = std::clamp(VehicleFastMath::FastCos(Tire.RCy1 * VehicleFastMath::FastAtan(Byk * Kappa)), 0.15f, 1.0f);

    OutFxN = FxPure * Gxa;
    OutFyN = FyPure * Gyk;

    // Pneumatic trail decay at high slip angle -> self-aligning torque Mz
    const float TrailDecay = std::exp(-3.2f * std::fabs(AlphaRad));
    OutMzNm = -Tire.ScaleLtr * Tire.PneumaticTrailMeters * TrailDecay * OutFyN;
}

void VehiclePhysicsRig::SolveTireTraction(
    PhysicsExtension&          Physics,
    const RigidBodyReading&    Body,
    const Mathematics::Axis3D& AxisForward,
    const Mathematics::Axis3D& AxisUp,
    const Mathematics::Axis3D& AxisRight,
    float                      ForwardSpeedMpsIn,
    float                      DeltaSeconds) noexcept
{
    (void)ForwardSpeedMpsIn;
    (void)AxisUp;
    const float Gravity = static_cast<float>(Constants::StandardGravity);

    for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
    {
        const auto& CornerSpec = Specification.Corners[I];
        const auto& TireSpec   = CornerSpec.Tire;
        auto&       W          = Wheels[I];

        const float R        = TireSpec.OuterRadiusMeters;
        const float InvI     = TireSpec.Compiled.InvInertia;

        if (!W.InContact || W.VerticalLoadN < 10.0f)
        {
            // Free-spinning airborne wheel integration
            const float BrakeSign = (W.AngularVelocityRadS > 0.01f) ? 1.0f : ((W.AngularVelocityRadS < -0.01f) ? -1.0f : 0.0f);
            const float NetTq     = W.DriveTorqueNm - W.BrakeTorqueNm * BrakeSign - 0.15f * W.AngularVelocityRadS;
            W.AngularVelocityRadS = std::clamp(W.AngularVelocityRadS + NetTq * InvI * DeltaSeconds, -220.0f, 220.0f);
            W.SpinAngleRad       += W.AngularVelocityRadS * DeltaSeconds;
            W.LongSlipKappa       = 0.0f;
            W.SlipAngleAlphaRad   = 0.0f;
            W.FxTransientN        = 0.0f;
            W.FyTransientN        = 0.0f;
            W.AppliedFxN          = 0.0f;
            W.AppliedFyN          = 0.0f;
            W.AppliedMzNm         = 0.0f;
            W.FrictionRegime      = FrictionRegimeCategory::Kinetic;
            continue;
        }

        // 1. Tire thermal & wear grip multiplier
        float ThermalGripScale = 1.0f;
        if (W.TreadTempK < TireSpec.OptimalTempK)
        {
            const float WarmFrac = std::clamp(
                (W.TreadTempK - StandardAmbientKelvin) / std::max(TireSpec.OptimalTempK - StandardAmbientKelvin, 1.0f),
                0.0f,
                1.0f);
            ThermalGripScale = LerpScalar(TireSpec.ColdGripFloor, 1.0f, WarmFrac);
        }
        else if (W.TreadTempK > TireSpec.OverheatTempK)
        {
            const float ExcessK = W.TreadTempK - TireSpec.OverheatTempK;
            ThermalGripScale = std::max(0.68f, 1.0f - 0.006f * ExcessK);
        }
        const float WornMm       = std::max(0.0f, TireSpec.InitialTreadDepthMm - W.TreadDepthMm);
        const float WearGripMult = std::max(0.70f, 1.0f - WornMm * TireSpec.GripLossPerMmWear);
        const float GripScale    = ThermalGripScale * WearGripMult;

        // 2. Contact-patch ground velocity in steered wheel basis (projected onto contact surface plane)
        const float Cs = VehicleFastMath::FastCos(W.SteerAngleRad);
        const float Sn = VehicleFastMath::FastSin(W.SteerAngleRad);
        const Mathematics::Axis3D RawWheelFwd   = AddAxis(ScaleAxis(AxisForward, Cs), ScaleAxis(AxisRight, Sn));
        const Mathematics::Axis3D Nrm           = W.ContactNormalWorld;
        const Mathematics::Axis3D WheelForward  = Mathematics::NormalizeAxis(
            SubAxis(RawWheelFwd, ScaleAxis(Nrm, DotAxis(RawWheelFwd, Nrm))),
            AxisForward);
        const Mathematics::Axis3D WheelRight    = Mathematics::NormalizeAxis(
            CrossAxis(WheelForward, Nrm),
            AxisRight);

        const Mathematics::Axis3D ContactArm    = SubAxis(W.ContactPointWorldM, Body.PositionMeters);
        const Mathematics::Axis3D ContactVel    = AddAxis(Body.LinearVelocity, CrossAxis(Body.AngularVelocity, ContactArm));
        const float VxBase                      = DotAxis(ContactVel, WheelForward);
        const float VyBase                      = DotAxis(ContactVel, WheelRight);
        const float VContact                    = std::sqrt(VxBase * VxBase + VyBase * VyBase);

        // 3. Apply suspension + ARB normal force directly to Jolt chassis body
        const Mathematics::Axis3D MountWorld = AddAxis(Body.PositionMeters, RotateByQuaternion(Body.Orientation, CornerSpec.LocalMountMeters));
        const float TotalVerticalN = ClampFinite(W.VerticalLoadN, 0.0f, W.SprungMassKg * Gravity * 6.0f);
        Physics.ApplyForceAtPoint(ChassisBody, ScaleAxis(Nrm, TotalVerticalN), MountWorld);

        // 4. Karnopp 3-regime stiction check at very low speed (slope holding + zero brake creep)
        constexpr float StictionSpeedThresholdMps = 0.35f;
        const float BrakeForceCapN = W.BrakeTorqueNm * TireSpec.Compiled.InvRadius;
        const float DriveForceN    = W.DriveTorqueNm * TireSpec.Compiled.InvRadius;
        const float FrictionBudgetN= W.VerticalLoadN * std::max(TireSpec.Compiled.PDx1Scaled * GripScale, 0.85f) * 0.95f;

        if (VContact < StictionSpeedThresholdMps && (W.BrakeTorqueNm > 15.0f || std::fabs(DriveForceN) < 120.0f))
        {
            // Gravity tangent component along road slope that tires must cancel to hold still
            const Mathematics::Axis3D GravityVec { 0.0f, -Gravity, 0.0f };
            const float SlopeLongN = -W.SprungMassKg * DotAxis(GravityVec, WheelForward);
            const float SlopeLatN  = -W.SprungMassKg * DotAxis(GravityVec, WheelRight);

            // Viscous stopping force + slope hold force
            const float CornerEffMass = W.SprungMassKg;
            const float HoldGain      = 0.65f / std::max(DeltaSeconds, 1.0e-3f);
            float ReqLatN             = ClampFinite(-CornerEffMass * VyBase * HoldGain + SlopeLatN, -FrictionBudgetN, FrictionBudgetN);
            float ReqLongN            = 0.0f;

            const bool BrakeHolding = (W.BrakeTorqueNm > 15.0f) || (std::fabs(DriveForceN) < 80.0f && InputThrottle < 0.02f);
            if (BrakeHolding)
            {
                const float MaxHoldLong = std::min(FrictionBudgetN, std::max(BrakeForceCapN, 650.0f));
                ReqLongN = ClampFinite(-CornerEffMass * VxBase * HoldGain + SlopeLongN + DriveForceN, -MaxHoldLong, MaxHoldLong);
            }
            else
            {
                ReqLongN = ClampFinite(DriveForceN, -FrictionBudgetN, FrictionBudgetN);
            }

            // Enforce circular friction budget (prioritize slope hold)
            const float ReqMag = std::sqrt(ReqLongN * ReqLongN + ReqLatN * ReqLatN);
            if (ReqMag > FrictionBudgetN && ReqMag > 1.0e-3f)
            {
                const float Scale = FrictionBudgetN / ReqMag;
                ReqLongN *= Scale;
                ReqLatN  *= Scale;
                W.FrictionRegime = FrictionRegimeCategory::StaticLateral;
            }
            else
            {
                W.FrictionRegime = BrakeHolding ? FrictionRegimeCategory::StaticFull : FrictionRegimeCategory::StaticLateral;
            }

            W.AppliedFxN          = ReqLongN;
            W.AppliedFyN          = ReqLatN;
            W.AppliedMzNm         = 0.0f;
            W.FxTransientN        = ReqLongN;
            W.FyTransientN        = ReqLatN;
            W.LongSlipKappa       = 0.0f;
            W.SlipAngleAlphaRad   = 0.0f;
            W.AngularVelocityRadS = BrakeHolding ? 0.0f : (VxBase * TireSpec.Compiled.InvRadius);
            W.SpinAngleRad       += W.AngularVelocityRadS * DeltaSeconds;

            const Mathematics::Axis3D WorldTireForce = AddAxis(
                ScaleAxis(WheelForward, W.AppliedFxN),
                ScaleAxis(WheelRight, W.AppliedFyN));
            Physics.ApplyForceAtPoint(ChassisBody, WorldTireForce, W.ContactPointWorldM);
            continue;
        }

        // 5. Kinetic regime: Implicit Newton-Raphson slip solver (GRIT zero-lag Pacejka MF6.1)
        W.FrictionRegime = FrictionRegimeCategory::Kinetic;

        float Kappa = W.LongSlipKappa;
        float Alpha = W.SlipAngleAlphaRad;

        // Effective wheel torque: brake opposes current wheel spin without flipping sign past zero
        const float BrakeOpposingNm = (std::fabs(W.AngularVelocityRadS) > 0.05f)
                                    ? (W.BrakeTorqueNm * ((W.AngularVelocityRadS > 0.0f) ? 1.0f : -1.0f))
                                    : std::clamp(W.DriveTorqueNm, -W.BrakeTorqueNm, W.BrakeTorqueNm);
        const float NetTorqueNm = W.DriveTorqueNm - BrakeOpposingNm;

        constexpr float Eps           = 1.0e-4f;
        constexpr uint32_t MaxIter    = 8u;
        constexpr float MinSpeedGuard = 1.0f;
        constexpr float ConvTol       = 1.0e-4f;
        constexpr float KappaMin      = -1.0f;
        constexpr float KappaMax      =  1.5f;
        constexpr float AlphaMax      =  1.48353f;

        const float AlphaTarget = -VehicleFastMath::FastAtan(VyBase / std::max(std::fabs(VxBase), MinSpeedGuard));

        for (uint32_t Iter = 0u; Iter < MaxIter; ++Iter)
        {
            const float KSafe = std::clamp(Kappa, KappaMin, KappaMax);
            const float ASafe = std::clamp(Alpha, -AlphaMax, AlphaMax);

            float FxEval = 0.0f;
            float FyEval = 0.0f;
            float MzEval = 0.0f;
            EvaluatePacejkaCombined(I, KSafe, ASafe, W.DynamicCamberRad, GripScale, FxEval, FyEval, MzEval);

            const float OmegaPred   = W.AngularVelocityRadS + (NetTorqueNm - FxEval * R) * InvI * DeltaSeconds;
            const float WheelSpdPred= OmegaPred * R;
            const float Denom       = std::max({ std::fabs(VxBase), std::fabs(WheelSpdPred), MinSpeedGuard });
            const float KappaTarget = (WheelSpdPred - VxBase) / Denom;

            const float ResK = Kappa - KappaTarget;
            const float ResA = Alpha - AlphaTarget;
            if (std::fabs(ResK) < ConvTol && std::fabs(ResA) < ConvTol)
            {
                break;
            }

            // Finite-difference derivative dResK / dK
            const float KPlus = std::clamp(KSafe + Eps, KappaMin, KappaMax);
            float FxPert = 0.0f;
            float FyPert = 0.0f;
            float MzPert = 0.0f;
            EvaluatePacejkaCombined(I, KPlus, ASafe, W.DynamicCamberRad, GripScale, FxPert, FyPert, MzPert);

            const float OmegaPert   = W.AngularVelocityRadS + (NetTorqueNm - FxPert * R) * InvI * DeltaSeconds;
            const float SpdPert     = OmegaPert * R;
            const float DenomPert   = std::max({ std::fabs(VxBase), std::fabs(SpdPert), MinSpeedGuard });
            const float KTargetPert = (SpdPert - VxBase) / DenomPert;
            const float dResKdK     = 1.0f - (KTargetPert - KappaTarget) / std::max(KPlus - KSafe, 1.0e-5f);

            const float RegSlope = ((dResKdK >= 0.0f) ? 1.0f : -1.0f) * std::max(std::fabs(dResKdK), 1.0e-3f);
            Kappa += std::clamp(-ResK / RegSlope, -0.20f, 0.20f);
            Alpha += std::clamp(-ResA, -0.25f, 0.25f);
        }

        Kappa = std::clamp(Kappa, KappaMin, KappaMax);
        Alpha = std::clamp(Alpha, -AlphaMax, AlphaMax);

        float FxSteady = 0.0f;
        float FySteady = 0.0f;
        float MzSteady = 0.0f;
        EvaluatePacejkaCombined(I, Kappa, Alpha, W.DynamicCamberRad, GripScale, FxSteady, FySteady, MzSteady);

        // 6. First-order tire relaxation length dynamics (Pacejka MF-Swift transient filter)
        const float LoadRatio = std::max(W.VerticalLoadN * 0.001f * TireSpec.Compiled.InvFz0kN, 0.15f);
        const float SigmaLong = TireSpec.RelaxationLongMeters * std::pow(LoadRatio, TireSpec.RelaxationLoadExponent);
        const float SigmaLat  = TireSpec.RelaxationLatMeters  * std::pow(LoadRatio, TireSpec.RelaxationLoadExponent);
        const float TauLong   = SigmaLong / std::max(VContact, 1.5f);
        const float TauLat    = SigmaLat  / std::max(VContact, 1.5f);

        const float BlendLong = std::clamp(DeltaSeconds / std::max(TauLong, DeltaSeconds), 0.0f, 1.0f);
        const float BlendLat  = std::clamp(DeltaSeconds / std::max(TauLat,  DeltaSeconds), 0.0f, 1.0f);
        W.FxTransientN = LerpScalar(W.FxTransientN, FxSteady, BlendLong);
        W.FyTransientN = LerpScalar(W.FyTransientN, FySteady, BlendLat);

        // Rolling resistance opposing wheel ground travel
        const float RollingResistN = W.VerticalLoadN * (TireSpec.RollingResistBase + TireSpec.RollingResistSpeedFactor * std::fabs(VxBase));
        const float RollSign       = (VxBase > 0.05f) ? -1.0f : ((VxBase < -0.05f) ? 1.0f : 0.0f);

        W.AppliedFxN  = ClampFinite(W.FxTransientN + RollSign * RollingResistN, -35000.0f, 35000.0f);
        W.AppliedFyN  = ClampFinite(W.FyTransientN, -35000.0f, 35000.0f);
        W.AppliedMzNm = ClampFinite(MzSteady, -1500.0f, 1500.0f);
        W.LongSlipKappa     = Kappa;
        W.SlipAngleAlphaRad = Alpha;

        // 7. Integrate wheel angular speed with brake zero-crossing clamp (prevents reverse spin from braking)
        const float RoadReactionTqNm = W.FxTransientN * R;
        const float DriveOnlyOmega   = W.AngularVelocityRadS + (W.DriveTorqueNm - RoadReactionTqNm) * InvI * DeltaSeconds;
        float NextOmega              = DriveOnlyOmega;
        if (W.BrakeTorqueNm > 1.0f)
        {
            const float MaxBrakeDelta = W.BrakeTorqueNm * InvI * DeltaSeconds;
            if (std::fabs(DriveOnlyOmega) <= MaxBrakeDelta)
            {
                NextOmega = 0.0f;
            }
            else
            {
                NextOmega = DriveOnlyOmega - ((DriveOnlyOmega > 0.0f) ? MaxBrakeDelta : -MaxBrakeDelta);
            }
        }
        W.AngularVelocityRadS = std::clamp(NextOmega, -220.0f, 220.0f);
        W.SpinAngleRad       += W.AngularVelocityRadS * DeltaSeconds;

        // 8. Tire two-node thermal (tread + carcass) & wear integration from slip power
        const float Vsx       = W.AngularVelocityRadS * R - VxBase;
        const float SlipPowW  = std::fabs(W.FxTransientN * Vsx) + std::fabs(W.FyTransientN * VyBase);
        const float PatchArea = TireSpec.WidthMeters * 0.22f;
        const float CoolConvW = TireSpec.SurfaceConvectionWPerM2K * (1.0f + 0.08f * GroundSpeedMps) * PatchArea
                              * (W.TreadTempK - StandardAmbientKelvin);
        const float CondToCarcassW = TireSpec.TreadToCarcassWPerK * (W.TreadTempK - W.CarcassTempK);

        W.TreadTempK = std::clamp(
            W.TreadTempK + ((SlipPowW * 0.45f - CoolConvW - CondToCarcassW) / std::max(TireSpec.TreadThermalMassJPerK, 50.0f)) * DeltaSeconds,
            StandardAmbientKelvin,
            460.0f);
        W.CarcassTempK = std::clamp(
            W.CarcassTempK + ((CondToCarcassW - 8.0f * (W.CarcassTempK - StandardAmbientKelvin)) / std::max(TireSpec.CarcassThermalMassJPerK, 100.0f)) * DeltaSeconds,
            StandardAmbientKelvin,
            430.0f);

        const float SlipEnergyMJ = SlipPowW * DeltaSeconds * 1.0e-6f;
        const float ThermalWear  = (W.TreadTempK > TireSpec.OverheatTempK) ? 2.5f : 1.0f;
        W.TreadDepthMm = std::max(0.0f, W.TreadDepthMm - SlipEnergyMJ * TireSpec.WearRateMmPerMJ * ThermalWear);

        // 9. Apply tire forces at contact patch (with anti-dive/anti-squat vertical offset) & self-aligning torque to Jolt
        const Mathematics::Axis3D WorldTireForce = AddAxis(
            ScaleAxis(WheelForward, W.AppliedFxN),
            ScaleAxis(WheelRight, W.AppliedFyN));
        Physics.ApplyForceAtPoint(ChassisBody, WorldTireForce, W.ContactPointWorldM);
        Physics.ApplyTorque(ChassisBody, ScaleAxis(Nrm, W.AppliedMzNm));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           STAGE 9: FUEL CONSUMPTION, LIVE CHASSIS MASS & ENGINE THERMAL
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::SolveFuelThermalBalance(
    PhysicsExtension& Physics,
    float             EngineOmegaRadS,
    float             NetEngineTorqueNm,
    float             GroundSpeedMpsIn,
    float             DeltaSeconds) noexcept
{
    const auto& ESpec = Specification.Engine;
    const auto& FDel  = Specification.FuelDelivery;
    const auto& FSpec = FDel.Fuel;

    // 1. Boost-dependent AFR & thermal efficiency
    const float BoostFrac  = std::clamp(CombinedBoostBar / 1.2f, 0.0f, 1.0f);
    ActiveAfr              = LerpScalar(FSpec.StoichiometricAfr, FSpec.FullBoostTargetAfr, BoostFrac);
    const float ThermalEff = std::max(0.18f, LerpScalar(ESpec.BaseThermalEfficiency, ESpec.BoostedThermalEfficiency, BoostFrac));

    // 2. Fuel mass flow (with DFCO overrun cut & idle combustion floor)
    const bool DecelCut = FDel.EnableDecelFuelCut && (InputThrottle < 0.01f) && (EngineRpm > ESpec.IdleRpm + 350.0f);
    if (FuelStarved || DecelCut || EngineRpm < 100.0f)
    {
        FuelFlowKgPerSec = 0.0f;
    }
    else
    {
        const float GrossPowerW = std::max(0.0f, (NetEngineTorqueNm + SuperchargerDragNm + EngineBrakingNm) * EngineOmegaRadS);
        const float LoadFlowKgS = (GrossPowerW / ThermalEff) / std::max(FSpec.EnergyDensityJPerKg, 1.0e6f);
        FuelFlowKgPerSec        = std::max(ESpec.IdleFuelFlowKgPerSec, LoadFlowKgS);
    }

    // 3. Deplete tank & dynamically couple live fuel mass into Jolt rigid body
    const float ConsumedKg = FuelFlowKgPerSec * DeltaSeconds;
    FuelMassKg  = std::max(0.0f, FuelMassKg - ConsumedKg);
    FuelLiters  = (FSpec.DensityKgPerLiter > 0.01f) ? (FuelMassKg / FSpec.DensityKgPerLiter) : 0.0f;
    const float LevelFrac = (FDel.TankCapacityLiters > 0.01f) ? std::clamp(FuelLiters / FDel.TankCapacityLiters, 0.0f, 1.0f) : 0.0f;

    FuelStarved  = (FuelMassKg <= 1.0e-5f);
    FuelWarning  = (LevelFrac <= FDel.WarningFraction);
    FuelCritical = (LevelFrac <= FDel.CriticalFraction);
    EstimatedRangeSec = (FuelFlowKgPerSec > 1.0e-5f) ? (FuelMassKg / FuelFlowKgPerSec) : 99999.0f;

    LiveTotalMassKg = Specification.DryChassisMassKg + (FDel.CoupleFuelMassToChassis ? FuelMassKg : 0.0f);
    if (FDel.CoupleFuelMassToChassis && std::fabs(LiveTotalMassKg - LastAppliedJoltMassKg) >= 0.25f)
    {
        Physics.AssignBodyMassKg(ChassisBody, LiveTotalMassKg);
        LastAppliedJoltMassKg = LiveTotalMassKg;
    }

    // 4. Engine block/coolant & oil sump thermal balance
    const float MechPowerW = std::max(0.0f, NetEngineTorqueNm * EngineOmegaRadS);
    const float WasteHeatW = MechPowerW * ((1.0f - ThermalEff) / ThermalEff) + (FuelStarved ? 0.0f : 3500.0f);
    const float ThermostatOpen = VehicleFastMath::SmoothUnitStep((CoolantTempK - ESpec.ThermostatOpenK) / 10.0f);
    const float RadCoolingW    = (ESpec.PassiveCoolingWPerK + ThermostatOpen * (180.0f + ESpec.SpeedCoolingWPerKPerMps * GroundSpeedMpsIn))
                               * std::max(0.0f, CoolantTempK - StandardAmbientKelvin);
    CoolantTempK = std::clamp(
        CoolantTempK + ((WasteHeatW - RadCoolingW) / std::max(ESpec.EngineThermalMassJPerK, 10000.0f)) * DeltaSeconds,
        StandardAmbientKelvin,
        ESpec.MaxSafeTempK);

    // Oil thermal dynamics (block -> oil conduction minus sump air cooling)
    const auto& OSpec = Specification.EngineOil;
    const float OilMassKg      = OSpec.SumpCapacityLiters * 0.001f * OSpec.DensityKgPerM3;
    const float OilThermalCap  = std::max(OilMassKg * OSpec.SpecificHeatJPerKgK, 2000.0f);
    const float BlockToOilW    = (CoolantTempK - OilTempK) * 45.0f + MechPowerW * 0.03f;
    const float SumpCoolW      = (12.0f + GroundSpeedMpsIn * 5.0f) * std::max(0.0f, OilTempK - StandardAmbientKelvin);
    OilTempK = std::clamp(
        OilTempK + ((BlockToOilW - SumpCoolW) / OilThermalCap) * DeltaSeconds,
        StandardAmbientKelvin,
        OSpec.DamageTempK + 15.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  STAGE 10: WAIT-FREE CROSS-THREAD PUBLICATION
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::PublishReading(
    const RigidBodyReading& Body,
    float                   StepComputeUs) noexcept
{
    (void)BoundaryExchange.PublishReadingInPlace(
        [this, &Body, StepComputeUs](VehicleReading& Out) noexcept
        {
            Out.WorldPositionMeters      = Body.PositionMeters;
            Out.WorldOrientation         = Body.Orientation;
            Out.LinearVelocityWorldMps   = Body.LinearVelocity;
            Out.AngularVelocityWorldRadS = Body.AngularVelocity;
            Out.ForwardSpeedMps          = ForwardSpeedMps;
            Out.LateralSpeedMps          = LateralSpeedMps;
            Out.VerticalSpeedMps         = VerticalSpeedMps;
            Out.GroundSpeedMps           = GroundSpeedMps;
            Out.SpeedKph                 = GroundSpeedMps * 3.6f;
            Out.LongitudinalG            = SmoothedLongG;
            Out.LateralG                 = SmoothedLatG;
            Out.YawRateRadPerSec         = YawRateRadPerSec;
            Out.PitchRateRadPerSec       = PitchRateRadPerSec;
            Out.RollRateRadPerSec        = RollRateRadPerSec;
            Out.DistanceTraveledMeters   = DistanceTraveledM;

            Out.EngineRpm                = EngineRpm;
            Out.EngineTorqueNm           = EngineTorqueNm;
            Out.EnginePowerKw            = EnginePowerKw;
            Out.EngineBrakingNm          = EngineBrakingNm;
            Out.CoolantTempKelvin        = CoolantTempK;
            Out.OilTempKelvin            = OilTempK;
            Out.ManifoldPressureKPa      = ManifoldPressureKPa;
            Out.ManifoldTempKelvin       = ManifoldTempK;

            Out.ActiveInduction          = Specification.ForcedInduction;
            Out.ActiveSuperchargerDrive  = Specification.Supercharger.DriveCategory;
            Out.TurboShaftRpm            = TurboShaftRpm;
            Out.TurboBoostBar            = TurboBoostBar;
            Out.WastegatePosition        = WastegatePos;
            Out.BovPosition              = BovPos;
            Out.SuperchargerRpm          = SuperchargerRpm;
            Out.SuperchargerBoostBar     = SuperchargerBoostBar;
            Out.SuperchargerBypassPos    = SuperchargerBypass;
            Out.SuperchargerParasiticNm  = SuperchargerDragNm;
            Out.CombinedBoostBar         = CombinedBoostBar;
            Out.BoostTorqueMultiplier    = BoostTorqueMult;

            Out.FuelLiters               = FuelLiters;
            Out.FuelMassKg               = FuelMassKg;
            Out.FuelLevelFraction        = (Specification.FuelDelivery.TankCapacityLiters > 0.01f)
                                         ? std::clamp(FuelLiters / Specification.FuelDelivery.TankCapacityLiters, 0.0f, 1.0f)
                                         : 0.0f;
            Out.FuelFlowKgPerSec         = FuelFlowKgPerSec;
            Out.ActiveAirFuelRatio       = ActiveAfr;
            Out.EstimatedRangeSeconds    = EstimatedRangeSec;
            Out.LiveTotalMassKg          = LiveTotalMassKg;
            Out.FuelWarningActive        = FuelWarning;
            Out.FuelCriticalActive       = FuelCritical;
            Out.FuelStarved              = FuelStarved;

            Out.ActiveGear               = ActiveGear;
            Out.TargetGear               = TargetGear;
            Out.ShiftingActive           = ShiftingActive;
            Out.ClutchEngagement         = ClutchEngagement;
            Out.ClutchLocked             = ClutchLocked;
            Out.ClutchTorqueNm           = ClutchTorqueNm;
            Out.ClutchSlipRpm            = ClutchSlipRpm;

            Out.AeroDragNewtons          = AeroDragN;
            Out.AeroDownforceNewtons     = AeroDownforceN;
            Out.AeroFrontDownforceN      = AeroFrontDownforceN;
            Out.AeroRearDownforceN       = AeroRearDownforceN;
            Out.AeroSideForceNewtons     = AeroSideForceN;
            Out.FloorClearanceMeters     = FloorClearanceM;
            Out.GroundEffectMultiplier   = GroundEffectMult;
            Out.RearWingAttached         = Specification.Aerodynamics.RearWingAttached;

            Out.FrontArchitecture        = Specification.Corners[0u].Suspension.Architecture;
            Out.RearArchitecture         = Specification.Corners[2u].Suspension.Architecture;
            Out.AbsIntervening           = AbsIntervening;
            Out.TcsIntervening           = TcsIntervening;
            Out.EscIntervening           = EscIntervening;

            for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
            {
                const auto& W = Wheels[I];
                auto&       R = Out.Corners[I];
                R.InContact              = W.InContact;
                R.SuspensionCompressionM = W.CompressionM;
                R.SuspensionVelocityMps  = W.CompressionVelMps;
                R.SpringForceNewtons     = W.SpringForceN;
                R.DamperForceNewtons     = W.DamperForceN;
                R.AntiRollForceNewtons   = W.AntiRollForceN;
                R.VerticalLoadNewtons    = W.VerticalLoadN;
                R.DynamicCamberRad       = W.DynamicCamberRad;
                R.DynamicBumpToeRad      = W.DynamicBumpToeRad;
                R.SteerAngleRad          = W.SteerAngleRad;
                R.AngularVelocityRadS    = W.AngularVelocityRadS;
                R.SpinAngleRad           = W.SpinAngleRad;
                R.LongitudinalSlip       = W.LongSlipKappa;
                R.SlipAngleRad           = W.SlipAngleAlphaRad;
                R.LongitudinalForceN     = W.AppliedFxN;
                R.LateralForceN          = W.AppliedFyN;
                R.SelfAligningTorqueNm   = W.AppliedMzNm;
                R.DriveTorqueNm          = W.DriveTorqueNm;
                R.BrakeTorqueNm          = W.BrakeTorqueNm;
                R.BrakeTempKelvin        = W.BrakeTempK;
                R.TreadTempKelvin        = W.TreadTempK;
                R.CarcassTempKelvin      = W.CarcassTempK;
                R.TreadDepthMm           = W.TreadDepthMm;
                R.FrictionRegime         = W.FrictionRegime;
                R.ContactPointWorldM     = W.ContactPointWorldM;
                R.WheelHubLocalMeters    = W.HubLocalMeters;
            }

            Out.CompletedSteps          = CompletedSteps;
            Out.SimulatedSeconds        = SimulatedSeconds;
            Out.StepComputeMicroseconds = StepComputeUs;
        },
        SimulatedSeconds,
        CompletedSteps);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  PHYSICS THREAD FIXED-STEP ORCHESTRATOR
//------------------------------------------------------------------------------------------------------------------------

void VehiclePhysicsRig::StepOnPhysicsThread(PhysicsExtension& Physics, double FixedDeltaSeconds) noexcept
{
    if (!Initialized || !Physics.QueryInitialized() || !ChassisBody.Valid())
    {
        return;
    }
    const float Dt = static_cast<float>(std::clamp(FixedDeltaSeconds, 1.0e-4, 0.05));
    const auto StartClock = std::chrono::steady_clock::now();

    // 0. Drain lock-free inbound control directives from GameThread / Editor / AI
    (void)BoundaryExchange.DrainDirectives([this](const VehicleControlDirective& Dir) noexcept
    {
        ApplyDirective(Dir);
    });

    // 1. Read authoritative Jolt chassis pose & populate ALL body-axis derived kinematics (fixes GRIT zero-kinematics bug)
    const RigidBodyReading Body = Physics.InspectBodyReading(ChassisBody);
    const Mathematics::Axis3D AxisForward = Mathematics::NormalizeAxis(RotateByQuaternion(Body.Orientation, { 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D AxisUp      = Mathematics::NormalizeAxis(RotateByQuaternion(Body.Orientation, { 0.0f, 1.0f, 0.0f }), { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D AxisRight   = Mathematics::NormalizeAxis(RotateByQuaternion(Body.Orientation, { 0.0f, 0.0f, 1.0f }), { 0.0f, 0.0f, 1.0f });

    ForwardSpeedMps    = DotAxis(Body.LinearVelocity, AxisForward);
    LateralSpeedMps    = DotAxis(Body.LinearVelocity, AxisRight);
    VerticalSpeedMps   = DotAxis(Body.LinearVelocity, AxisUp);
    GroundSpeedMps     = std::sqrt(ForwardSpeedMps * ForwardSpeedMps + LateralSpeedMps * LateralSpeedMps);
    YawRateRadPerSec   = -DotAxis(Body.AngularVelocity, AxisUp);   // +YawRate = right turn (+Steering)
    PitchRateRadPerSec =  DotAxis(Body.AngularVelocity, AxisRight);
    RollRateRadPerSec  =  DotAxis(Body.AngularVelocity, AxisForward);

    const float InvG      = 1.0f / static_cast<float>(Constants::StandardGravity);
    const float RawLongG  = ((ForwardSpeedMps - PrevForwardSpeedMps) / Dt) * InvG;
    const float RawLatG   = ((LateralSpeedMps - PrevLateralSpeedMps) / Dt + ForwardSpeedMps * YawRateRadPerSec) * InvG;
    const float SmoothAlp = std::min(1.0f, 14.0f * Dt);
    SmoothedLongG         = LerpScalar(SmoothedLongG, ClampFinite(RawLongG, -5.0f, 5.0f), SmoothAlp);
    SmoothedLatG          = LerpScalar(SmoothedLatG,  ClampFinite(RawLatG,  -5.0f, 5.0f), SmoothAlp);
    PrevForwardSpeedMps   = ForwardSpeedMps;
    PrevLateralSpeedMps   = LateralSpeedMps;
    DistanceTraveledM    += GroundSpeedMps * Dt;

    // 2. Execute full physics sequence in strict physical dependency order
    ComputeSteeringRack(ForwardSpeedMps, LateralSpeedMps, Dt);
    SolveSuspensionKinematics(Physics, Body, AxisForward, AxisUp, AxisRight, Dt);
    ComputeAntiRollForces();
    ComputeBodyAxisAerodynamics(ForwardSpeedMps, LateralSpeedMps, VerticalSpeedMps, InputBrake, AxisForward, AxisUp, AxisRight, Physics, Body);
    ComputeForcedInduction(EngineRpm, InputThrottle, Dt);
    SolveBrakeHydraulics(ForwardSpeedMps, YawRateRadPerSec, Dt);
    SolveDrivetrainTorque(ForwardSpeedMps, SmoothedLatG, Dt);
    SolveTireTraction(Physics, Body, AxisForward, AxisUp, AxisRight, ForwardSpeedMps, Dt);
    SolveFuelThermalBalance(Physics, EngineRpm * VehicleRpmToRadPerSec, EngineTorqueNm, GroundSpeedMps, Dt);

    ++CompletedSteps;
    SimulatedSeconds += static_cast<double>(Dt);

    const auto EndClock = std::chrono::steady_clock::now();
    const float ComputeUs = std::chrono::duration<float, std::micro>(EndClock - StartClock).count();

    // 3. Publish complete immutable reading atomically for wait-free Game/Render/Audio/Editor readers
    PublishReading(Body, ComputeUs);
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
