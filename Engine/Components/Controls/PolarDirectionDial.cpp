/*============================================================================================================================================
                                                         POLARDIRECTIONDIAL.CPP
============================================================================================================================================*/
// 🧩 Implementation of generic reusable polar azimuth & elevation compass dial

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/PolarDirectionDial.h"

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <cmath>

namespace Frontier::PolarDirectionDial
{

namespace
{
constexpr float PiF = 3.14159265359f;
} // namespace

const char* ResolveCompassLabel(float AzimuthDegrees) noexcept
{
    float Wrapped = std::fmod(AzimuthDegrees, 360.0f);
    if (Wrapped < 0.0f) Wrapped += 360.0f;
    if (Wrapped < 22.5f || Wrapped >= 337.5f) return "north";
    if (Wrapped < 67.5f)  return "northeast";
    if (Wrapped < 112.5f) return "east";
    if (Wrapped < 157.5f) return "southeast";
    if (Wrapped < 202.5f) return "south";
    if (Wrapped < 247.5f) return "southwest";
    if (Wrapped < 292.5f) return "west";
    return "northwest";
}

void Construct(
    PixelSpace&             Surface,
    Point2D                 Center,
    float                   Radius,
    float                   AzimuthDegrees,
    float                   ElevationDegrees,
    bool                    ShowElevation,
    const PaneInputReading& Input,
    float&                  OutAzimuthDegrees,
    float&                  OutElevationDegrees,
    bool&                   OutChanged,
    bool                    Interactive) noexcept
{
    Surface.FillCircle(Center, Radius, ColorRgba{ 14u, 15u, 18u, 255u });
    Surface.StrokeCircle(Center, Radius,         ColorRgba{ 255u, 255u, 255u, 28u }, 1.1f);
    Surface.StrokeCircle(Center, Radius * 0.66f, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
    Surface.StrokeCircle(Center, Radius * 0.33f, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
    Surface.StrokeLine({ Center.X - Radius, Center.Y }, { Center.X + Radius, Center.Y }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
    Surface.StrokeLine({ Center.X, Center.Y - Radius }, { Center.X, Center.Y + Radius }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Center.X - 3.5f,          Center.Y - Radius + 4.0f  }, 8.5f, InspectorMetrics::DimInk, "N");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Center.X - 3.0f,          Center.Y + Radius - 13.0f }, 8.5f, InspectorMetrics::DimInk, "S");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Center.X + Radius - 11.0f, Center.Y - 4.5f           }, 8.5f, InspectorMetrics::DimInk, "E");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Center.X - Radius + 4.0f,  Center.Y - 4.5f           }, 8.5f, InspectorMetrics::DimInk, "W");

    const float AzRad = AzimuthDegrees * (PiF / 180.0f);
    const float Radial = ShowElevation
                             ? (std::clamp(1.0f - std::max(0.0f, ElevationDegrees) / 90.0f, 0.05f, 0.94f) * Radius)
                             : (Radius * 0.78f);
    const Point2D Marker { Center.X + std::sin(AzRad) * Radial, Center.Y - std::cos(AzRad) * Radial };
    Surface.StrokeLine(Center, Marker, ColorRgba{ 246u, 212u, 140u, 128u }, 1.4f);
    Surface.FillCircle(Marker, 6.5f, ColorRgba{ 255u, 214u, 122u, 65u });
    Surface.FillCircle(Marker, 3.8f, ColorRgba{ 248u, 232u, 186u, 255u });

    if (Interactive)
    {
        const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        const float DX   = Cursor.X - Center.X;
        const float DY   = Cursor.Y - Center.Y;
        const float Dist = std::sqrt(DX * DX + DY * DY);
        if (Dist <= Radius && Input.PrimaryPointer.Down)
        {
            float NextAz = std::atan2(DX, -DY) * (180.0f / PiF);
            if (NextAz < 0.0f) NextAz += 360.0f;
            OutAzimuthDegrees = NextAz;
            if (ShowElevation)
            {
                OutElevationDegrees = std::clamp((1.0f - Dist / Radius) * 90.0f, 0.0f, 89.0f);
            }
            OutChanged = true;
        }
    }
}

} // namespace Frontier::PolarDirectionDial
