/*============================================================================================================================================
                                                        SURFACESHADING.H
============================================================================================================================================*/
// 🧩 Portable matcap layer, normalized Blinn–Phong EON and directional PCF reference calculations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LinearColor
{
    float Red   = 0.0f; // [0-1] - Linear red radiance
    float Green = 0.0f; // [0-1] - Linear green radiance
    float Blue  = 0.0f; // [0-1] - Linear blue radiance
};

struct DirectionVector
{
    float X = 0.0f; // [-] - Horizontal direction component
    float Y = 0.0f; // [-] - Vertical direction component
    float Z = 0.0f; // [-] - Depth direction component
};

struct MatcapLayer
{
    uint32_t      TextureToken = 0u;                  // [idx] - Stable sampled-image resource token
    LinearColor   Tint         {1.0f, 1.0f, 1.0f};    // [0-1] - Linear layer radiance multiplier
    float         Weight       = 1.0f;                // [0-1] - Contribution before range normalization
    float         NormalScale  = 0.5f;                // [-] - View-normal matcap coordinate scale
    float         NormalBias   = 0.5f;                // [-] - View-normal matcap coordinate offset
};

struct DirectionalLightDescription
{
    DirectionVector DirectionFromSurface {0.0f, 0.0f, 1.0f}; // [-] - Normalized direction toward light
    LinearColor     Radiance             {1.0f, 1.0f, 1.0f}; // [0-1] - Linear light radiance
    float           AmbientIntensity     = 0.08f;              // [0-1] - Unshadowed diffuse fill multiplier
};

struct EonSpecularDescription
{
    LinearColor Reflectance {0.04f, 0.04f, 0.04f}; // [0-1] - Normal-incidence specular reflectance
    float       Shininess   = 48.0f;                // [-] - Blinn–Phong lobe exponent
    float       Intensity   = 1.0f;                 // [0-1] - Specular energy multiplier
};

struct DirectionalShadowDescription
{
    std::span<const float> DepthTexels       {};   // [0-1] - Row-major light-space nearest depths
    uint32_t               WidthPx           = 0u; // [px] - Depth texture width
    uint32_t               HeightPx          = 0u; // [px] - Depth texture height
    float                  DepthBias         = 0.0015f; // [0-1] - Receiver depth comparison bias
    uint32_t               FilterRadiusPx    = 1u; // [px] - Symmetric PCF sample radius
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr LinearColor   ModulateRadiance(LinearColor Left, LinearColor Right) noexcept;
[[nodiscard]] constexpr LinearColor   ScaleRadiance(LinearColor Radiance, float Scale) noexcept;
[[nodiscard]] constexpr LinearColor   AddRadiance(LinearColor Left, LinearColor Right) noexcept;
[[nodiscard]] constexpr DirectionVector NormalizeDirection(DirectionVector Direction) noexcept;
[[nodiscard]] constexpr float           DotDirection(DirectionVector Left, DirectionVector Right) noexcept;
[[nodiscard]] constexpr float           EvaluateEonSpecular(
    DirectionVector SurfaceNormal,
    DirectionVector ViewDirection,
    DirectionVector LightDirection,
    float           Shininess) noexcept;
[[nodiscard]] float                     SampleDirectionalShadowVisibility(
    float                              LightU,
    float                              LightV,
    float                              ReceiverDepth,
    const DirectionalShadowDescription& Shadow) noexcept;
[[nodiscard]] LinearColor               EvaluateFastSurfaceLighting(
    DirectionVector                    SurfaceNormal,
    DirectionVector                    ViewDirection,
    const DirectionalLightDescription&  Light,
    const EonSpecularDescription&       Specular,
    LinearColor                        MatcapRadiance,
    float                              ShadowVisibility) noexcept;

} // namespace Frontier::GeometricRaster

//------------------------------------------------------------------------------------------------------------------------
//                                                 TEMPLATE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

namespace Frontier::GeometricRaster
{

constexpr LinearColor ModulateRadiance(LinearColor Left, LinearColor Right) noexcept
{
    return LinearColor{Left.Red * Right.Red, Left.Green * Right.Green, Left.Blue * Right.Blue};
}

constexpr LinearColor ScaleRadiance(LinearColor Radiance, float Scale) noexcept
{
    return LinearColor{Radiance.Red * Scale, Radiance.Green * Scale, Radiance.Blue * Scale};
}

constexpr LinearColor AddRadiance(LinearColor Left, LinearColor Right) noexcept
{
    return LinearColor{Left.Red + Right.Red, Left.Green + Right.Green, Left.Blue + Right.Blue};
}

constexpr DirectionVector NormalizeDirection(DirectionVector Direction) noexcept
{
    const float SquaredLength = DotDirection(Direction, Direction);
    if (SquaredLength <= 1.0e-20f)
    {
        return DirectionVector{0.0f, 0.0f, 1.0f};
    }
    const float InverseLength = 1.0f / std::sqrt(SquaredLength);
    return DirectionVector{Direction.X * InverseLength, Direction.Y * InverseLength, Direction.Z * InverseLength};
}

constexpr float DotDirection(DirectionVector Left, DirectionVector Right) noexcept
{
    return Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z;
}

constexpr float EvaluateEonSpecular(
    DirectionVector SurfaceNormal,
    DirectionVector ViewDirection,
    DirectionVector LightDirection,
    float           Shininess) noexcept
{
    const DirectionVector Normal = NormalizeDirection(SurfaceNormal);
    const DirectionVector View   = NormalizeDirection(ViewDirection);
    const DirectionVector TowardLight = NormalizeDirection(LightDirection);
    const DirectionVector Halfway = NormalizeDirection(DirectionVector{
        View.X + TowardLight.X,
        View.Y + TowardLight.Y,
        View.Z + TowardLight.Z});
    const float NormalDotHalfway = std::max(0.0f, DotDirection(Normal, Halfway));
    const float Exponent = std::max(0.0f, Shininess);
    const float Normalization = (Exponent + 8.0f) / (8.0f * std::numbers::pi_v<float>);
    return Normalization * std::pow(NormalDotHalfway, Exponent);
}

} // namespace Frontier::GeometricRaster
