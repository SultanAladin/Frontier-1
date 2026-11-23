/*==========================================================================================================================================
                                                           AUTOMOTIVEPAINT.H
==========================================================================================================================================*/
// 🧩 Aligned, bounded automotive binder, flake and clearcoat microstructure parameters shared with Slang.

#pragma once
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>

namespace Frontier
{
struct alignas(16) AutomotivePaint
{
    std::array<uint32_t, 4> Configuration{0, 3, 0, 0}; // Enabled, buried layers, palette/ramp/single, palette count.
    std::array<float, 4> Distribution{.64f, 450, 1, .65f}; // Coverage, particle size, frequency multiplier, binder depth.
    std::array<float, 4> Finish{.12f, .3f, .85f, 1}; // Roughness and metallic endpoints.
    std::array<float, 4> Relief{.2f, .55f, .18f, 24}; // Particle tilt, burial, orange peel and peel frequency.
    std::array<std::array<float, 4>, 12> Palette{}; // Linear RGB, ramp position.
    std::array<float, 4> AverageTint{}; // Derived at compilation using the reference 48 linear-space samples.
};
static_assert(sizeof(AutomotivePaint) == 272);
static_assert(offsetof(AutomotivePaint, Palette) == 64);

inline bool ValidateAutomotivePaint(const AutomotivePaint &Paint)
{
    if (Paint.Configuration[0] > 1 || Paint.Configuration[1] < 1 || Paint.Configuration[1] > 4 ||
        Paint.Configuration[2] > 2 || Paint.Configuration[3] > 12 || (Paint.Configuration[0] && !Paint.Configuration[3])) return false;
    const auto Within = [](float Coefficient, float Minimum, float Maximum)
    {
        return std::isfinite(Coefficient) && Coefficient >= Minimum && Coefficient <= Maximum;
    };
    if (!Within(Paint.Distribution[0], 0, 1) || !Within(Paint.Distribution[1], .001f, 1000000) ||
        !Within(Paint.Distribution[2], .001f, 1000) || !Within(Paint.Distribution[3], 0, 4)) return false;
    for (float Coefficient : Paint.Finish) if (!Within(Coefficient, 0, 1)) return false;
    if (Paint.Finish[0] > Paint.Finish[1] || Paint.Finish[2] > Paint.Finish[3]) return false;
    for (size_t Index = 0; Index < 4; ++Index)
        if (!Within(Paint.Relief[Index], 0, Index == 3 ? 10000.f : 1.f)) return false;
    for (float Coefficient : Paint.AverageTint) if (!Within(Coefficient, 0, 1)) return false;
    if (Paint.AverageTint[3]) return false;
    for (size_t Index = 0; Index < Paint.Palette.size(); ++Index)
    {
        for (float Coefficient : Paint.Palette[Index]) if (!Within(Coefficient, 0, 1)) return false;
        if (Paint.Configuration[2] == 1 && Index && Index < Paint.Configuration[3] &&
            Paint.Palette[Index][3] < Paint.Palette[Index - 1][3]) return false;
    }
    return true;
}
} // namespace Frontier
