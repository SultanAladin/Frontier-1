//============================================================================================================================================
//                                                  DIRECTIONALLIGHTCOMPONENT.CPP
//============================================================================================================================================

#include "SlateWorld/World/DirectionalLightComponent/Api/DirectionalLightComponent.h"

#include <cmath>

namespace Slate
{
void DirectionalLightComponent::SetSolarPosition(double AzimuthDegrees, double ElevationDegrees)
{
    constexpr double Pi = 3.14159265358979323846;
    const double Azimuth = AzimuthDegrees * Pi / 180.0;
    const double Elevation = ElevationDegrees * Pi / 180.0;
    const double Horizontal = std::cos(Elevation);
    Direction[0] = std::sin(Azimuth) * Horizontal;
    Direction[1] = std::sin(Elevation);
    Direction[2] = std::cos(Azimuth) * Horizontal;
}
} // namespace Slate
