//============================================================================================================================
// 🧩 Frontier — TyreDeformation.h
//============================================================================================================================
// 📝 Vehicle-wheel telemetry adapter for the standalone spring and continuum vertex projection.
//    Provenance, stated plainly: in the demo this is a GPU vertex deformation driven by the dynamics (contact angle,
//    carcass deflection, gauge pressure, lateral-force shear) — it shapes the rubber, it does not feed forces back.
//    The adapter converts existing VehicleSolver wheel telemetry into the standalone CPU projection input.
//    It does not port vehicle dynamics or establish identical GPU arithmetic.
//============================================================================================================================

#pragma once

#include <algorithm>
#include <cmath>

#include "Engine/Physics/TyreSurfaceProjection.h"
#include "Engine/Physics/VehicleSolver.h"

namespace Frontier
{

// 📝 Encodes one wheel's record from its dynamic state (VehicleVisualState.js EncodeRecord, offsets 36..51):
//    contact angle from the ground normal carried into the steered, spun wheel frame; lateral distortion from the
//    cornering force at 150 kN per metre, clamped to ±40 mm. A shredded wheel encodes an empty record.
inline TyreDeformationRecord ResolveTyreDeformationRecord(const VehicleWheelState& Wheel, const double VehicleOrientation[4])
{
    TyreDeformationRecord Record;
    Record.Shredded = Wheel.Shredded;
    if (Wheel.Shredded)
        return Record;
    Record.Deflection = (float)std::fmax(0.0, Wheel.TyreDeflection);
    Record.GaugePressure = (float)std::fmax(0.0, Wheel.Pressure);
    // Steered = Orientation · Rz(Steer); Spun = Steered · Ry(SpinAngle); LocalDown = Spun⁻¹ · (−GroundNormal).
    const auto AxisQuat = [](double Ax, double Ay, double Az, double Angle, double Out[4])
    {
        const double Sine = std::sin(0.5 * Angle);
        Out[0] = Ax * Sine; Out[1] = Ay * Sine; Out[2] = Az * Sine; Out[3] = std::cos(0.5 * Angle);
    };
    const auto Multiply = [](const double L[4], const double R[4], double Out[4])
    {
        Out[0] = L[3] * R[0] + L[0] * R[3] + L[1] * R[2] - L[2] * R[1];
        Out[1] = L[3] * R[1] - L[0] * R[2] + L[1] * R[3] + L[2] * R[0];
        Out[2] = L[3] * R[2] + L[0] * R[1] - L[1] * R[0] + L[2] * R[3];
        Out[3] = L[3] * R[3] - L[0] * R[0] - L[1] * R[1] - L[2] * R[2];
    };
    const auto Rotate = [](const double Q[4], const double V[3], double Out[3])
    {
        const double Crossed[3] = { Q[1] * V[2] - Q[2] * V[1], Q[2] * V[0] - Q[0] * V[2], Q[0] * V[1] - Q[1] * V[0] };
        const double Twice[3] = { 2.0 * Crossed[0], 2.0 * Crossed[1], 2.0 * Crossed[2] };
        Out[0] = V[0] + Q[3] * Twice[0] + (Q[1] * Twice[2] - Q[2] * Twice[1]);
        Out[1] = V[1] + Q[3] * Twice[1] + (Q[2] * Twice[0] - Q[0] * Twice[2]);
        Out[2] = V[2] + Q[3] * Twice[2] + (Q[0] * Twice[1] - Q[1] * Twice[0]);
    };
    double SteerQuat[4]; AxisQuat(0.0, 0.0, 1.0, Wheel.Steer, SteerQuat);
    double Steered[4]; Multiply(VehicleOrientation, SteerQuat, Steered);
    double SpinQuat[4]; AxisQuat(0.0, 1.0, 0.0, Wheel.SpinAngle, SpinQuat);
    double Spun[4]; Multiply(Steered, SpinQuat, Spun);
    const double Conjugate[4] = { -Spun[0], -Spun[1], -Spun[2], Spun[3] };
    const double DownWorld[3] = { -Wheel.GroundNormal[0], -Wheel.GroundNormal[1], -Wheel.GroundNormal[2] };
    double LocalDown[3]; Rotate(Conjugate, DownWorld, LocalDown);
    Record.ContactAngle = (float)std::atan2(LocalDown[2], LocalDown[0]);
    Record.LateralDistortion = (float)std::fmax(-0.04, std::fmin(0.04, Wheel.LateralForce / 150000.0));
    return Record;
}

} // namespace Frontier
