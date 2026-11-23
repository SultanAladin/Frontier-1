//============================================================================================================================================
//                                                   DIRECTIONALLIGHTCOMPONENT.H
//============================================================================================================================================
// 🧩 Runtime sunlight shared by sky presentation, direct lighting and shadow scheduling.

#pragma once

namespace Slate
{

class DirectionalLightComponent
{
public:
    void SetSolarPosition(double AzimuthDegrees, double ElevationDegrees);

    double Direction[3] = { 0.0, 1.0, 0.0 }; // unit vector toward the sun, Y-up
    double Illuminance = 4.8;
    double TemperatureKelvin = 5500.0;
    double AngularRadiusDegrees = 0.266;
    double ShadowStrength = 1.0;
    bool CastShadows = true;
};

} // namespace Slate
