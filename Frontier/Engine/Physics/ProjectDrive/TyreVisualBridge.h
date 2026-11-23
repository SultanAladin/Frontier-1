/*==========================================================================================================================================
                                                PROJECT DRIVE TYRE VISUAL BRIDGE
                                     ------------------------------------------------------------------
            Feed the VehicleHMI demonstration's shape solvers from Project Drive wheel telemetry.
==========================================================================================================================================*/

#pragma once

#include "VehicleSolver.h"
#include "../TyreContinuum.h"

namespace Frontier
{

// 📝 Optional visual bridge, not another contact solver. Project Drive's contact fan, struts and Pacejka
//    forces remain authoritative. Pressure is explicitly supplied by the host (e.g. from its thermal system).
enum class DemoTyreVisualMode { Spring, Projection, Solid };

class ProjectDriveTyreVisualBridge
{
public:
    void Reset(const Vehicle::VehicleTelemetry& Telemetry);
    void Advance(const Vehicle::VehicleTelemetry& Telemetry, const double Pressures[4],
                 DemoTyreVisualMode Mode, double Seconds);
    const TyreCarcassProjection& Projection(int Wheel) const { return m_Projection[Wheel]; }
    const TyreCarcassSolid& Solid(int Wheel) const { return m_Solid[Wheel]; }

private:
    static TyreContinuumInput Input(const Vehicle::WheelTelemetry& Wheel, double Pressure);
    TyreCarcassProjection m_Projection[4];
    TyreCarcassSolid m_Solid[4];
};

} // namespace Frontier
