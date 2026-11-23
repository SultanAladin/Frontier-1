/*==========================================================================================================================================
                                                         SURFACE CARDS
                                     ------------------------------------------------------------------
                 Planar surface cards with per-texel radiosity: form factors, iterative bounce,
                      distance-field visibility — the card half of the indirect GI (§8.1/§8.2).
==========================================================================================================================================*/

// 🧩 Surface cards: the portable form of the demo's Card* units (Capture/Direct/Radiosity/Final).
//
// A card is a planar quad of scene surface, discretised into texels. The solver builds the classic
// radiosity system over texels — B = E + ρ·F·B — with differential-to-finite form factors
// F_ij = cosθ_i·cosθ_j·A_j / (π·r_eff²), r_eff² = r² + (A_i+A_j)/π (centre-sampled with symmetric
// distance regularisation, so reciprocity A_iF_ij = A_jF_ji holds exactly and near-field overshoot
// cannot create energy across bounces)
// and visibility decided by sphere-tracing the distance field between texel centres: the two
// indirect systems compose exactly as the chain intends. Iteration is Jacobi with a residual
// tolerance; power bookkeeping (emitted vs absorbed) is exposed so validation can audit energy.
// Scalar channel, double precision — the SDK phase runs the same system on GPU-resident card atlases.

#pragma once

#include "DistanceField.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

struct SurfaceCard
{
    SpatialVector Origin{};              // [m] - one corner
    SpatialVector AxisU{1, 0, 0};        // [m] - full edge vector U
    SpatialVector AxisV{0, 1, 0};        // [m] - full edge vector V (normal = normalise(U × V))
    uint32_t      TexelsU = 4;
    uint32_t      TexelsV = 4;
    double        Albedo = 0.5;          // [-] in [0, 1)
    double        EmittedExitance = 0.0; // [exitance] - uniform over the card (E term)
};

class CardRadiosity
{
  public:
    // Builds texels + the form-factor matrix. Occluders may be null (everything visible).
    // Returns false for a degenerate card (zero area / zero texels) or an out-of-range albedo.
    bool Build(const std::vector<SurfaceCard>& Cards, const DistanceFieldScene* Occluders);

    // Jacobi iteration of B = E + ρ·F·B until the max texel delta drops to Tolerance
    // or MaxIterations is reached. Returns the final residual.
    double Solve(uint32_t MaxIterations = 200, double Tolerance = 1e-9);

    [[nodiscard]] uint32_t TexelCount() const noexcept { return static_cast<uint32_t>(Radiosity_.size()); }
    [[nodiscard]] double   Radiosity(uint32_t Texel) const noexcept { return Radiosity_[Texel]; }
    [[nodiscard]] double   FormFactor(uint32_t From, uint32_t To) const noexcept
    {
        return FormFactors_[static_cast<size_t>(From) * Radiosity_.size() + To];
    }
    // Σ_j F_ij — 1 in a perfectly closed, finely-tessellated enclosure.
    [[nodiscard]] double        FormFactorSum(uint32_t From) const noexcept;
    [[nodiscard]] SpatialVector TexelCentre(uint32_t Texel) const noexcept { return Centres_[Texel]; }
    [[nodiscard]] SpatialVector TexelNormal(uint32_t Texel) const noexcept { return Normals_[Texel]; }
    [[nodiscard]] double        TexelArea(uint32_t Texel) const noexcept { return Areas_[Texel]; }
    [[nodiscard]] uint32_t      TexelCard(uint32_t Texel) const noexcept { return CardOf_[Texel]; }

    [[nodiscard]] double EmittedPower() const noexcept;   // Σ E_i·A_i
    [[nodiscard]] double AbsorbedPower() const noexcept;  // Σ (1−ρ_i)·H_i·A_i at the current solution

  private:
    std::vector<double>        FormFactors_;   // [From × To]
    std::vector<double>        Radiosity_;
    std::vector<double>        Emitted_;
    std::vector<double>        Albedos_;
    std::vector<double>        Areas_;
    std::vector<SpatialVector> Centres_;
    std::vector<SpatialVector> Normals_;
    std::vector<uint32_t>      CardOf_;
};

} // namespace Frontier
