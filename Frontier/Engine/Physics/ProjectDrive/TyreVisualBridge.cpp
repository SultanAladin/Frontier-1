/*==========================================================================================================================================
                                                PROJECT DRIVE TYRE VISUAL BRIDGE
==========================================================================================================================================*/

#include "TyreVisualBridge.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

TyreContinuumInput ProjectDriveTyreVisualBridge::Input(const Vehicle::WheelTelemetry& Wheel, double Pressure)
{
    TyreContinuumInput Result;
    Result.Centre[0] = Wheel.HubPosition.x; Result.Centre[1] = Wheel.HubPosition.y; Result.Centre[2] = Wheel.HubPosition.z;
    const double S = std::sin(0.5 * Wheel.SpinAngle), C = std::cos(0.5 * Wheel.SpinAngle);
    const auto& Q = Wheel.HubRotation; // Hub rotation includes steer; the material spin is on its local +Y.
    Result.Orientation[0] = Q.x*C - Q.z*S;
    Result.Orientation[1] = Q.y*C + Q.w*S;
    Result.Orientation[2] = Q.z*C + Q.x*S;
    Result.Orientation[3] = Q.w*C - Q.y*S;
    Result.GroundNormal[0] = Wheel.ContactNormal.x;
    Result.GroundNormal[1] = Wheel.ContactNormal.y;
    Result.GroundNormal[2] = Wheel.ContactNormal.z;
    Result.Deflection = Wheel.InContact ? std::max(0.0, static_cast<double>(Wheel.TyreDeflection)) : 0.0;
    Result.Pressure = Pressure;
    Result.LateralForce = Wheel.LateralForce;
    return Result;
}

void ProjectDriveTyreVisualBridge::Reset(const Vehicle::VehicleTelemetry& Telemetry)
{
    for (int Index = 0; Index < 4; ++Index)
    {
        const auto Wheel = static_cast<unsigned>(Index) < Telemetry.WheelCount ? Telemetry.Wheels[Index]
                                                                                : Vehicle::WheelTelemetry{};
        const TyreContinuumInput State = Input(Wheel, 220000.0);
        m_Projection[Index].Reset(State);
        m_Solid[Index].Reset(State);
    }
}

void ProjectDriveTyreVisualBridge::Advance(const Vehicle::VehicleTelemetry& Telemetry, const double Pressures[4],
                                            DemoTyreVisualMode Mode, double Seconds)
{
    if (Mode == DemoTyreVisualMode::Spring) return;
    const double Dt = std::max(1.0/480.0, std::min(Seconds != 0.0 ? Seconds : 1.0/60.0, 1.0/30.0));
    for (unsigned Index = 0; Index < std::min(Telemetry.WheelCount, 4u); ++Index)
    {
        const TyreContinuumInput State = Input(Telemetry.Wheels[Index], Pressures[Index]);
        if (Mode == DemoTyreVisualMode::Projection) m_Projection[Index].Advance(State, Dt, 2);
        else if (Mode == DemoTyreVisualMode::Solid) m_Solid[Index].Advance(State, Dt, 2);
    }
}

} // namespace Frontier
