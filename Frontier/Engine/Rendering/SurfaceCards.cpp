/*==========================================================================================================================================
                                                    SURFACE CARDS DEFINITION
==========================================================================================================================================*/

#include "SurfaceCards.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Sub(SpatialVector A, SpatialVector B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }
SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

constexpr double Pi = 3.14159265358979323846;

} // namespace

bool CardRadiosity::Build(const std::vector<SurfaceCard>& Cards, const DistanceFieldScene* Occluders)
{
    Centres_.clear(); Normals_.clear(); Areas_.clear(); Albedos_.clear(); Emitted_.clear(); CardOf_.clear();
    for (uint32_t CardIndex = 0; CardIndex < Cards.size(); ++CardIndex)
    {
        const SurfaceCard& Card = Cards[CardIndex];
        if (Card.TexelsU == 0 || Card.TexelsV == 0 || Card.Albedo < 0.0 || Card.Albedo >= 1.0)
        {
            return false;
        }
        const SpatialVector NormalRaw = Cross(Card.AxisU, Card.AxisV);
        const double        CardArea  = Length(NormalRaw);
        if (CardArea <= 0.0)
        {
            return false;
        }
        const SpatialVector Normal    = Mul(NormalRaw, 1.0 / CardArea);
        const double        TexelArea = CardArea / (static_cast<double>(Card.TexelsU) * Card.TexelsV);
        for (uint32_t V = 0; V < Card.TexelsV; ++V)
        {
            for (uint32_t U = 0; U < Card.TexelsU; ++U)
            {
                const double FU = (static_cast<double>(U) + 0.5) / Card.TexelsU;
                const double FV = (static_cast<double>(V) + 0.5) / Card.TexelsV;
                Centres_.push_back(Add(Card.Origin, Add(Mul(Card.AxisU, FU), Mul(Card.AxisV, FV))));
                Normals_.push_back(Normal);
                Areas_.push_back(TexelArea);
                Albedos_.push_back(Card.Albedo);
                Emitted_.push_back(Card.EmittedExitance);
                CardOf_.push_back(CardIndex);
            }
        }
    }

    const uint32_t Count = static_cast<uint32_t>(Centres_.size());
    FormFactors_.assign(static_cast<size_t>(Count) * Count, 0.0);
    for (uint32_t I = 0; I < Count; ++I)
    {
        for (uint32_t J = 0; J < Count; ++J)
        {
            if (I == J)
            {
                continue;
            }
            const SpatialVector Delta = Sub(Centres_[J], Centres_[I]);
            const double        R2    = Dot(Delta, Delta);
            if (R2 <= 0.0)
            {
                continue;
            }
            const double        R       = std::sqrt(R2);
            const SpatialVector Towards = Mul(Delta, 1.0 / R);
            const double        CosI    = Dot(Normals_[I], Towards);
            const double        CosJ    = Dot(Normals_[J], Mul(Towards, -1.0));
            if (CosI <= 0.0 || CosJ <= 0.0)
            {
                continue;   // back-facing pair exchanges nothing
            }
            if (Occluders != nullptr && !Occluders->SegmentVisible(Centres_[I], Centres_[J]))
            {
                continue;
            }
            // Distance regularisation r^2 + (A_i + A_j)/pi: treats each texel as a disk of its own
            // area for distance purposes. The raw centre-sampled kernel systematically overshoots in
            // the near field (texel size ~ distance), which multi-bounce amplifies into created
            // energy; the additive term is symmetric in (I, J), so reciprocity stays exact while
            // closed-room sums approach 1 from below and tighten with texel refinement.
            const double EffectiveR2 = R2 + (Areas_[I] + Areas_[J]) / Pi;
            FormFactors_[static_cast<size_t>(I) * Count + J] =
                std::clamp(CosI * CosJ * Areas_[J] / (Pi * EffectiveR2), 0.0, 1.0);
        }
    }
    Radiosity_ = Emitted_;
    return Count > 0;
}

double CardRadiosity::Solve(uint32_t MaxIterations, double Tolerance)
{
    const uint32_t Count = TexelCount();
    if (Count == 0)
    {
        return 0.0;
    }
    std::vector<double> Next(Count, 0.0);
    double Residual = 0.0;
    for (uint32_t Iteration = 0; Iteration < MaxIterations; ++Iteration)
    {
        Residual = 0.0;
        for (uint32_t I = 0; I < Count; ++I)
        {
            double Incoming = 0.0;
            const double* Row = &FormFactors_[static_cast<size_t>(I) * Count];
            for (uint32_t J = 0; J < Count; ++J)
            {
                Incoming += Row[J] * Radiosity_[J];
            }
            Next[I]  = Emitted_[I] + Albedos_[I] * Incoming;
            Residual = std::max(Residual, std::abs(Next[I] - Radiosity_[I]));
        }
        Radiosity_.swap(Next);
        if (Residual <= Tolerance)
        {
            break;
        }
    }
    return Residual;
}

double CardRadiosity::FormFactorSum(uint32_t From) const noexcept
{
    const uint32_t Count = TexelCount();
    double Sum = 0.0;
    for (uint32_t J = 0; J < Count; ++J)
    {
        Sum += FormFactors_[static_cast<size_t>(From) * Count + J];
    }
    return Sum;
}

double CardRadiosity::EmittedPower() const noexcept
{
    double Sum = 0.0;
    for (uint32_t I = 0; I < TexelCount(); ++I)
    {
        Sum += Emitted_[I] * Areas_[I];
    }
    return Sum;
}

double CardRadiosity::AbsorbedPower() const noexcept
{
    const uint32_t Count = TexelCount();
    double Sum = 0.0;
    for (uint32_t I = 0; I < Count; ++I)
    {
        double Incoming = 0.0;
        const double* Row = &FormFactors_[static_cast<size_t>(I) * Count];
        for (uint32_t J = 0; J < Count; ++J)
        {
            Incoming += Row[J] * Radiosity_[J];
        }
        Sum += (1.0 - Albedos_[I]) * Incoming * Areas_[I];
    }
    return Sum;
}

} // namespace Frontier
