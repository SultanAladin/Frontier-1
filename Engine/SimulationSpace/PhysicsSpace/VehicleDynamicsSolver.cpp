/*============================================================================================================================================
                                                       VEHICLEDYNAMICSSOLVER.CPP
============================================================================================================================================*/
// 🧩 Implementation of VehicleDynamicsSolver TorqueCurve spline evaluation, 6-speed shifting & aero downforce telemetry

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Constants/PhysicsConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::PhysicsSpace
{

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

    const float X0 = Pts[Seg].Rpm;
    const float X1 = Pts[Seg + 1u].Rpm;
    const float Y0 = Pts[Seg].TorqueNm;
    const float Y1 = Pts[Seg + 1u].TorqueNm;
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

    constexpr float AirDensity       = 1.225f;
    constexpr float DriveEfficiency  = 0.92f;
    constexpr float RollingCoeff     = 0.015f;
    constexpr float MaxBrakeTorque   = 2600.0f * 4.0f;
    constexpr float UpshiftRpm       = 6800.0f;
    constexpr float DownshiftRpm     = 2600.0f;

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

    const float DriveForce    = (EngineNm * ActiveGear * DriveEfficiency) / TyreRadius;
    const float BrakeForce    = (Brake * MaxBrakeTorque) / TyreRadius;
    const float AeroDrag      = 0.5f * AirDensity * Settings.DragCoefficient * Settings.FrontalAreaM2 * InOutTelemetry.SpeedMps * InOutTelemetry.SpeedMps;
    const float DownforceN    = 0.5f * AirDensity * Settings.AeroLiftAreaM2 * InOutTelemetry.SpeedMps * InOutTelemetry.SpeedMps;
    const float NormalLoadN   = Mass * static_cast<float>(Constants::StandardGravity) + DownforceN;
    const float RollingDrag   = RollingCoeff * NormalLoadN;

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

} // namespace Frontier::SimulationSpace::PhysicsSpace
