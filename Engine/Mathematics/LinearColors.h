/*============================================================================================================================================
                                                                LINEARCOLORS.H
============================================================================================================================================*/
// 🧩 Dedicated scene-referred Linear Rec.709 / sRGB tristimulus struct preserving explicit .R, .G, .B radiometric channels

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cmath>

namespace Frontier::Mathematics
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LinearRGB
{
    float R = 0.0f; // [cd/m^2 | -] - Scene-referred linear red tristimulus or reflectance
    float G = 0.0f; // [cd/m^2 | -] - Scene-referred linear green tristimulus or reflectance
    float B = 0.0f; // [cd/m^2 | -] - Scene-referred linear blue tristimulus or reflectance

    [[nodiscard]] constexpr bool operator==(const LinearRGB&) const noexcept = default;
};

[[nodiscard]] constexpr LinearRGB operator+(LinearRGB Left, LinearRGB Right) noexcept
{
    return { Left.R + Right.R, Left.G + Right.G, Left.B + Right.B };
}

[[nodiscard]] constexpr LinearRGB operator*(LinearRGB Left, float Scale) noexcept
{
    return { Left.R * Scale, Left.G * Scale, Left.B * Scale };
}

[[nodiscard]] constexpr LinearRGB operator*(float Scale, LinearRGB Right) noexcept
{
    return { Scale * Right.R, Scale * Right.G, Scale * Right.B };
}

[[nodiscard]] constexpr float EvaluateRec709Luminance(LinearRGB Color) noexcept
{
    return 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
}

} // namespace Frontier::Mathematics
