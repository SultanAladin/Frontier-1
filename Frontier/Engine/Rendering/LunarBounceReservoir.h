/*==========================================================================================================================================
                                                  LUNAR BOUNCE RESERVOIR
==========================================================================================================================================*/
// 🧩 Same-receiver temporal reuse for one diffuse SDF bounce illuminated by a directional Moon.
// Visibility is tested against the SDF scene only. This is not spatial, dynamic or multibounce ReSTIR GI.

#pragma once

#include "DiscrepancySequence.h"
#include "DistanceField.h"
#include "ReservoirSampling.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier
{

class LunarBounceReservoir
{
  public:
    void Stream(const DistanceFieldScene& Scene, SpatialVector Position, SpatialVector Normal,
                SpatialVector MoonDirection, double MoonIrradiance, uint32_t Count,
                DeterministicRandom& Random) noexcept
    {
        if (Count == 0 || Scene.Primitives().empty() || !Finite(Position) || !Finite(Normal) ||
            !Finite(MoonDirection) || !std::isfinite(MoonIrradiance) || MoonIrradiance <= 0.0) return;
        const SpatialVector N = Normalize(Normal), Moon = Normalize(MoonDirection);
        if (Dot(N, N) == 0.0 || Dot(Moon, Moon) == 0.0) return;
        if (Bound_ && !SameContext(Scene, Position, Normal, Moon, MoonIrradiance)) return;
        Scene_ = &Scene; Position_ = Position; Normal_ = Normal;
        MoonDirection_ = Moon; MoonIrradiance_ = MoonIrradiance; Bound_ = true;
        const SpatialVector Tangent = Normalize(Cross(std::abs(N.Y) < 0.99 ? SpatialVector{0, 1, 0}
                                                                             : SpatialVector{1, 0, 0}, N));
        const SpatialVector Bitangent = Cross(N, Tangent);
        const double ShiftX = Random.NextUnit(), ShiftY = Random.NextUnit();
        for (uint32_t Index = 0; Index < Count; ++Index)
        {
            const auto Q = CranleyPatterson(HammersleyPoint(Index, Count), ShiftX, ShiftY);
            const double Radius = std::sqrt(Q.X), Angle = 2.0 * Pi * Q.Y;
            const double Cosine = std::sqrt(std::max(0.0, 1.0 - Q.X));
            const SpatialVector Direction = Normalize(Add(Add(Mul(Tangent, Radius * std::cos(Angle)),
                                                               Mul(Bitangent, Radius * std::sin(Angle))),
                                                           Mul(N, Cosine)));
            const SpatialVector Origin = Add(Position, Mul(N, 0.001));
            const auto Hit = Scene.IntersectAnalytic(Origin, Direction, 1e3);
            double Radiance = 0.0;
            if (Hit.Hit && Hit.Primitive < Scene.Primitives().size())
            {
                const double Albedo = Scene.Primitives()[Hit.Primitive].DiffuseReflectance;
                if (std::isfinite(Albedo) && Albedo > 0.0)
                {
                    const SpatialVector Surface = Add(Origin, Mul(Direction, Hit.Distance));
                    const SpatialVector HitNormal = Scene.NormalAt(Surface);
                    const double CosMoon = std::max(0.0, Dot(HitNormal, Moon));
                    if (CosMoon > 0.0 && !Scene.IntersectAnalytic(Add(Surface, Mul(HitNormal, 0.002)), Moon, 1e3).Hit)
                        Radiance = std::clamp(Albedo, 0.0, 1.0) * MoonIrradiance * CosMoon / Pi;
                }
            }
            // The cosine-hemisphere PDF is cos/pi. Zero-contribution candidates still count.
            Reservoir_.Update({Hit.Hit ? Hit.Primitive : UINT32_MAX, Cosine, Radiance},
                              Cosine > 0.0 ? Pi * Radiance : 0.0, Random.NextUnit());
        }
    }

    void Reuse(const LunarBounceReservoir& Previous, const DistanceFieldScene& Scene,
               uint32_t MaximumHistory, DeterministicRandom& Random) noexcept
    {
        if (!Bound_ || Scene_ != &Scene ||
            !Previous.SameContext(Scene, Position_, Normal_, MoonDirection_, MoonIrradiance_)) return;
        Reservoir Prior = Previous.Reservoir_;
        Prior.ClampSampleCount(MaximumHistory);
        Reservoir_.Combine(Prior, Target(Prior.Sample()), 1.0, Random.NextUnit());
    }

    void Finalize() noexcept { Reservoir_.Finalize(Bound_ ? Target(Reservoir_.Sample()) : 0.0); }
    [[nodiscard]] double Irradiance() const noexcept
    { return Bound_ && Reservoir_.HasSample() ? Reservoir_.Estimate(Target(Reservoir_.Sample())) : 0.0; }
    [[nodiscard]] uint32_t SampleCount() const noexcept { return Reservoir_.SampleCount(); }

  private:
    static constexpr double Pi = 3.14159265358979323846;
    Reservoir Reservoir_;
    const DistanceFieldScene* Scene_ = nullptr;
    SpatialVector Position_{}, Normal_{}, MoonDirection_{};
    double MoonIrradiance_ = 0.0;
    bool Bound_ = false;
    static bool Finite(SpatialVector A) noexcept
    { return std::isfinite(A.X) && std::isfinite(A.Y) && std::isfinite(A.Z); }
    static double Dot(SpatialVector A, SpatialVector B) noexcept { return A.X*B.X + A.Y*B.Y + A.Z*B.Z; }
    static SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X+B.X, A.Y+B.Y, A.Z+B.Z}; }
    static SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X*S, A.Y*S, A.Z*S}; }
    static SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
    { return {A.Y*B.Z-A.Z*B.Y, A.Z*B.X-A.X*B.Z, A.X*B.Y-A.Y*B.X}; }
    static SpatialVector Normalize(SpatialVector A) noexcept
    {
        const double Length = std::sqrt(Dot(A, A));
        return Length > 0.0 ? Mul(A, 1.0 / Length) : SpatialVector{};
    }
    static double Target(const ReservoirSample& Sample) noexcept
    { return std::max(0.0, Sample.U) * std::max(0.0, Sample.V); }
    bool SameContext(const DistanceFieldScene& Scene, SpatialVector Position, SpatialVector Normal,
                     SpatialVector Moon, double Irradiance) const noexcept
    {
        return Bound_ && Scene_ == &Scene && Position_.X == Position.X && Position_.Y == Position.Y &&
               Position_.Z == Position.Z && Normal_.X == Normal.X && Normal_.Y == Normal.Y && Normal_.Z == Normal.Z &&
               MoonDirection_.X == Moon.X && MoonDirection_.Y == Moon.Y && MoonDirection_.Z == Moon.Z &&
               MoonIrradiance_ == Irradiance;
    }
};

} // namespace Frontier
