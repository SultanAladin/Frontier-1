/*==========================================================================================================================================
                                                    LUNAR DISC PROJECTION
==========================================================================================================================================*/
// 🧩 Reference-only textured Moon: projected-area quadrature for flux and an antialiased disc.
// The texture is normalized to a configurable full-Moon/Sun visual flux ratio. This is a
// broadband approximation, not a measured lunar BRDF or spectral calibration.

#pragma once

#include "ReferenceRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

struct LunarPixel
{
    SpectrumRgb Radiance{};
    double Coverage = 0.0;
};

class LunarDiscProjection
{
  public:
    LunarDiscProjection(const ReferenceLunarProfile& Profile, SpatialVector SunDirection,
                        SpectrumRgb SunIrradiance) noexcept
        : Profile_(Profile), SunIrradiance_(SunIrradiance)
    {
        constexpr double Pi = 3.14159265358979323846;
        const double DirectionLength = Length(Profile.Direction);
        const double SunLength = Length(SunDirection);
        if (!Profile.Albedo || !Profile.Albedo->Loaded() || !std::isfinite(DirectionLength) || DirectionLength < 1e-12 ||
            !std::isfinite(SunLength) || SunLength < 1e-12 || !std::isfinite(Profile.AngularRadiusRad) ||
            Profile.AngularRadiusRad <= 0.0 || Profile.AngularRadiusRad >= 0.2 ||
            !std::isfinite(Profile.FullMoonFluxRatio) || Profile.FullMoonFluxRatio <= 0.0 ||
            !std::isfinite(Profile.EarthshineStrength) || Profile.EarthshineStrength < 0.0 ||
            !std::isfinite(Profile.SubObserverLongitudeRad) || !std::isfinite(Profile.SubObserverLatitudeRad) ||
            !std::isfinite(SunIrradiance.R) || !std::isfinite(SunIrradiance.G) || !std::isfinite(SunIrradiance.B) ||
            SunIrradiance.R < 0.0 || SunIrradiance.G < 0.0 || SunIrradiance.B < 0.0)
            return;
        Direction_ = Mul(Profile.Direction, 1.0 / DirectionLength);
        SunDirection_ = Mul(SunDirection, 1.0 / SunLength);
        Observer_ = Mul(Direction_, -1.0);
        SpatialVector North = Sub(Profile.North, Mul(Direction_, Dot(Profile.North, Direction_)));
        if (!std::isfinite(Length(North)) || Length(North) < 1e-12)
        {
            const SpatialVector Fallback = std::abs(Direction_.Z) < 0.9 ? SpatialVector{0, 0, 1}
                                                                         : SpatialVector{0, 1, 0};
            North = Sub(Fallback, Mul(Direction_, Dot(Fallback, Direction_)));
        }
        North_ = Mul(North, 1.0 / Length(North));
        East_ = Cross(Direction_, North_);
        SinRadius_ = std::sin(Profile.AngularRadiusRad);
        ProjectedArea_ = Pi * SinRadius_ * SinRadius_;
        PhaseAngle_ = std::acos(std::clamp(Dot(Observer_, SunDirection_), -1.0, 1.0));

        double FullMean = 0.0;
        size_t Index = 0;
        for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        {
            const double Radius = std::sqrt((Ring + 0.5) / Rings);
            const double Mu = std::sqrt(1.0 - Radius * Radius);
            for (uint32_t Sector = 0; Sector < Sectors; ++Sector)
            {
                const double Angle = 2.0 * Pi * (Sector + 0.5 * (Ring & 1)) / Sectors;
                Source_[Index].A = Radius * std::cos(Angle);
                Source_[Index].B = Radius * std::sin(Angle);
                const auto Full = EvaluateLunarDiscRadiance(*Profile.Albedo, Source_[Index].A, Source_[Index].B,
                    Profile.SubObserverLongitudeRad, Profile.SubObserverLatitudeRad, Mu, Mu, 0.0,
                    {1, 1, 1}, 0.0);
                FullMean += Luminance(Full);
                ++Index;
            }
        }
        FullMean /= SourceCount;
        if (!std::isfinite(FullMean) || FullMean <= 1e-15)
            return;
        // The reference ratio is at the mean apparent radius; varying distance changes disc flux by area.
        constexpr double MeanRadius = 0.004596;
        const double ReferenceArea = Pi * std::sin(MeanRadius) * std::sin(MeanRadius);
        Gain_ = Profile.FullMoonFluxRatio / (ReferenceArea * FullMean);
        Valid_ = true;
        for (auto& Sample : Source_)
        {
            Sample.Radiance = RadianceAt(Sample.A, Sample.B);
            Irradiance_.R += Sample.Radiance.R * ProjectedArea_ / SourceCount;
            Irradiance_.G += Sample.Radiance.G * ProjectedArea_ / SourceCount;
            Irradiance_.B += Sample.Radiance.B * ProjectedArea_ / SourceCount;
        }
    }

    [[nodiscard]] bool Valid() const noexcept { return Valid_; }
    [[nodiscard]] SpatialVector Direction() const noexcept { return Direction_; }
    // Top-of-atmosphere beam integrated over the lunar disc, in the Sun irradiance's units.
    [[nodiscard]] SpectrumRgb Irradiance() const noexcept { return Irradiance_; }

    // Dx/Dy and PixelWidth/Height are angular coordinates in the tangent plane at the Moon centre.
    [[nodiscard]] LunarPixel Evaluate(double Dx, double Dy, double PixelWidth, double PixelHeight,
                                      SpatialVector TangentRight, SpatialVector TangentUp) const noexcept
    {
        LunarPixel Result;
        if (!Valid_ || PixelWidth <= 0.0 || PixelHeight <= 0.0) return Result;
        const double EastX = Dot(East_, TangentRight), EastY = Dot(East_, TangentUp);
        const double NorthX = Dot(North_, TangentRight), NorthY = Dot(North_, TangentUp);
        const double RadiusPixels = SinRadius_ / std::min(PixelWidth, PixelHeight);
        const double Blend = std::clamp((RadiusPixels - 3.0) / 3.0, 0.0, 1.0);
        const double ResolvedWeight = Blend * Blend * (3.0 - 2.0 * Blend);
        if (ResolvedWeight < 1.0)
        {
            const double SigmaX = 0.55 * PixelWidth, SigmaY = 0.55 * PixelHeight;
            const double Root2 = std::sqrt(2.0);
            const double AreaPerSample = ProjectedArea_ / SourceCount;
            for (const auto& Sample : Source_)
            {
                const double X = SinRadius_ * (Sample.A * EastX + Sample.B * NorthX);
                const double Y = SinRadius_ * (Sample.A * EastY + Sample.B * NorthY);
                const double Wx = 0.5 * (std::erf((Dx + 0.5 * PixelWidth - X) / (Root2 * SigmaX)) -
                                         std::erf((Dx - 0.5 * PixelWidth - X) / (Root2 * SigmaX)));
                const double Wy = 0.5 * (std::erf((Dy + 0.5 * PixelHeight - Y) / (Root2 * SigmaY)) -
                                         std::erf((Dy - 0.5 * PixelHeight - Y) / (Root2 * SigmaY)));
                const double Weight = (1.0 - ResolvedWeight) * AreaPerSample * Wx * Wy / (PixelWidth * PixelHeight);
                Result.Radiance.R += Sample.Radiance.R * Weight;
                Result.Radiance.G += Sample.Radiance.G * Weight;
                Result.Radiance.B += Sample.Radiance.B * Weight;
                Result.Coverage += Weight;
            }
        }
        if (ResolvedWeight > 0.0)
        {
            constexpr int Grid = 4;
            for (int Y = 0; Y < Grid; ++Y)
                for (int X = 0; X < Grid; ++X)
                {
                    const double Qx = Dx + ((X + 0.5) / Grid - 0.5) * PixelWidth;
                    const double Qy = Dy + ((Y + 0.5) / Grid - 0.5) * PixelHeight;
                    const double A = (Qx * EastX + Qy * EastY) / SinRadius_;
                    const double B = (Qx * NorthX + Qy * NorthY) / SinRadius_;
                    if (A * A + B * B > 1.0) continue;
                    const auto C = RadianceAt(A, B);
                    const double Weight = ResolvedWeight / (Grid * Grid);
                    Result.Radiance.R += C.R * Weight;
                    Result.Radiance.G += C.G * Weight;
                    Result.Radiance.B += C.B * Weight;
                    Result.Coverage += Weight;
                }
        }
        Result.Coverage = std::clamp(Result.Coverage, 0.0, 1.0);
        return Result;
    }

  private:
    static constexpr uint32_t Rings = 32, Sectors = 48, SourceCount = Rings * Sectors;
    struct SourceSample { double A = 0, B = 0; SpectrumRgb Radiance{}; };
    static double Dot(SpatialVector A, SpatialVector B) noexcept { return A.X*B.X + A.Y*B.Y + A.Z*B.Z; }
    static double Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }
    static SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X*S, A.Y*S, A.Z*S}; }
    static SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X+B.X, A.Y+B.Y, A.Z+B.Z}; }
    static SpatialVector Sub(SpatialVector A, SpatialVector B) noexcept { return {A.X-B.X, A.Y-B.Y, A.Z-B.Z}; }
    static SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
    { return {A.Y*B.Z-A.Z*B.Y, A.Z*B.X-A.X*B.Z, A.X*B.Y-A.Y*B.X}; }
    static double Luminance(SpectrumRgb C) noexcept { return 0.2126*C.R + 0.7152*C.G + 0.0722*C.B; }
    [[nodiscard]] SpectrumRgb RadianceAt(double A, double B) const noexcept
    {
        const double Mu = std::sqrt(std::max(0.0, 1.0 - A*A - B*B));
        const SpatialVector Normal = Add(Add(Mul(East_, A), Mul(North_, B)), Mul(Observer_, Mu));
        const auto C = EvaluateLunarDiscRadiance(*Profile_.Albedo, A, B,
            Profile_.SubObserverLongitudeRad, Profile_.SubObserverLatitudeRad,
            std::max(0.0, Dot(Normal, SunDirection_)), Mu, PhaseAngle_, {1, 1, 1}, Profile_.EarthshineStrength);
        return {C.R * Gain_ * SunIrradiance_.R, C.G * Gain_ * SunIrradiance_.G, C.B * Gain_ * SunIrradiance_.B};
    }
    ReferenceLunarProfile Profile_;
    SpectrumRgb SunIrradiance_{}, Irradiance_{};
    SpatialVector Direction_{}, SunDirection_{}, Observer_{}, East_{}, North_{};
    double SinRadius_ = 0, ProjectedArea_ = 0, PhaseAngle_ = 0, Gain_ = 0;
    bool Valid_ = false;
    std::array<SourceSample, SourceCount> Source_{};
};

} // namespace Frontier
