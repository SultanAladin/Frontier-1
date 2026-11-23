/*==========================================================================================================================================
                                                STATIC INDIRECT RESERVOIR
==========================================================================================================================================*/
// 🧩 Temporal reservoir sampling for static diffuse-emitter distance fields in the CPU reference renderer.

#pragma once

#include "DiscrepancySequence.h"
#include "DistanceField.h"
#include "ReservoirSampling.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier
{

// 📝 Narrow subset of ReSTIR GI: cosine-hemisphere paths to constant-radiance SDF emitters, reused only at the
//    identical surface and scene during one reference Render call. No spatial shifts, changing illumination,
//    moving objects, RGB card lookup, sky paths or multibounce transport are represented here.
class StaticIndirectReservoir
{
  public:
    void Stream(const DistanceFieldScene& Scene,
                SpatialVector             Position,
                SpatialVector             Normal,
                uint32_t                  SampleCount,
                DeterministicRandom&      Random) noexcept
    {
        if (SampleCount == 0 || Scene.Primitives().empty() || !Finite(Position) || !Finite(Normal)) return;
        const SpatialVector N = Normalize(Normal);
        if (N.X == 0.0 && N.Y == 0.0 && N.Z == 0.0) return;
        if (Bound_ && !SameContext(Scene, Position, Normal)) return;
        Scene_ = &Scene; Position_ = Position; Normal_ = Normal; Bound_ = true;
        const SpatialVector Tangent = Normalize(Cross(std::abs(N.Y) < 0.99 ? SpatialVector{0,1,0} : SpatialVector{1,0,0}, N));
        const SpatialVector Bitangent = Cross(N, Tangent);
        const double ShiftX = Random.NextUnit(), ShiftY = Random.NextUnit();
        for (uint32_t Index = 0; Index < SampleCount; ++Index)
        {
            const SequencePoint Q = CranleyPatterson(HammersleyPoint(Index, SampleCount), ShiftX, ShiftY);
            const double Radius = std::sqrt(Q.X), Angle = 2.0*Pi*Q.Y, Cosine = std::sqrt(std::max(1.0-Q.X,0.0));
            const double AlongTangent = Radius*std::cos(Angle), AlongBitangent = Radius*std::sin(Angle);
            const SpatialVector Direction = Normalize({Tangent.X*AlongTangent + Bitangent.X*AlongBitangent + N.X*Cosine,
                                                       Tangent.Y*AlongTangent + Bitangent.Y*AlongBitangent + N.Y*Cosine,
                                                       Tangent.Z*AlongTangent + Bitangent.Z*AlongBitangent + N.Z*Cosine});
            const SpatialVector Origin{Position.X+N.X*0.001, Position.Y+N.Y*0.001, Position.Z+N.Z*0.001};
            const DistanceTraceResult Hit = Scene.Trace(Origin, Direction, 1e3);
            const uint32_t Primitive = Hit.Hit ? Hit.Primitive : UINT32_MAX;
            const ReservoirSample Candidate{Primitive, Cosine, 0.0};
            const double Radiance = PrimitiveRadiance(Scene, Primitive);
            // Source density is cosine/pi, so target/pdf reduces to pi*L. Zero radiance still counts in M.
            const double Weight = Cosine > 0.0 ? Pi*Radiance : 0.0;
            Reservoir_.Update(Candidate, Weight, Random.NextUnit());
        }
    }

    // Reuse is valid only for the same receiver and unchanged distance-field radiance/visibility.
    void Reuse(const StaticIndirectReservoir& Previous,
               const DistanceFieldScene&      Scene,
               uint32_t                       MaximumHistory,
               DeterministicRandom&           Random) noexcept
    {
        if (!Bound_ || Scene_ != &Scene || !Previous.SameContext(Scene, Position_, Normal_)) return;
        Reservoir Prior = Previous.Reservoir_;
        Prior.ClampSampleCount(MaximumHistory);
        Reservoir_.Combine(Prior, Target(Scene, Prior.Sample()), 1.0, Random.NextUnit());
    }

    void Finalize(const DistanceFieldScene& Scene) noexcept
    {
        Reservoir_.Finalize(Bound_ && Scene_ == &Scene ? Target(Scene, Reservoir_.Sample()) : 0.0);
    }

    [[nodiscard]] double Irradiance(const DistanceFieldScene& Scene) const noexcept
    {
        return Bound_ && Scene_ == &Scene && Reservoir_.HasSample()
            ? Reservoir_.Estimate(Target(Scene, Reservoir_.Sample())) : 0.0;
    }

    [[nodiscard]] uint32_t SampleCount() const noexcept { return Reservoir_.SampleCount(); }

  private:
    static constexpr double Pi = 3.14159265358979323846;
    Reservoir Reservoir_;
    const DistanceFieldScene* Scene_ = nullptr;
    SpatialVector Position_{}, Normal_{};
    bool Bound_ = false;

    static bool Finite(SpatialVector A) noexcept
    {
        return std::isfinite(A.X) && std::isfinite(A.Y) && std::isfinite(A.Z);
    }
    bool SameContext(const DistanceFieldScene& Scene, SpatialVector Position, SpatialVector Normal) const noexcept
    {
        return Bound_ && Scene_ == &Scene && Position_.X == Position.X && Position_.Y == Position.Y &&
               Position_.Z == Position.Z && Normal_.X == Normal.X && Normal_.Y == Normal.Y && Normal_.Z == Normal.Z;
    }
    static SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
    {
        return {A.Y*B.Z-A.Z*B.Y, A.Z*B.X-A.X*B.Z, A.X*B.Y-A.Y*B.X};
    }
    static SpatialVector Normalize(SpatialVector A) noexcept
    {
        const double Length = std::sqrt(A.X*A.X+A.Y*A.Y+A.Z*A.Z);
        return Length > 0.0 ? SpatialVector{A.X/Length,A.Y/Length,A.Z/Length} : SpatialVector{};
    }
    static double PrimitiveRadiance(const DistanceFieldScene& Scene, uint32_t Primitive) noexcept
    {
        if (Primitive >= Scene.Primitives().size()) return 0.0;
        const double Radiance = Scene.Primitives()[Primitive].EmissiveRadiance;
        return std::isfinite(Radiance) && Radiance > 0.0 ? Radiance : 0.0;
    }
    static double Target(const DistanceFieldScene& Scene, const ReservoirSample& Sample) noexcept
    {
        return PrimitiveRadiance(Scene, Sample.Candidate) * std::max(0.0, Sample.U);
    }
};

} // namespace Frontier
