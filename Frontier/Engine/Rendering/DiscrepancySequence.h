/*==========================================================================================================================================
                                                      DISCREPANCY SEQUENCE
                                     ------------------------------------------------------------------
                 Low-discrepancy candidate points for the illumination chain (plan §8.2 step 1:
                      DiscrepancySequence feeds DirectRadiance's initial reservoir candidates).
==========================================================================================================================================*/

// 🧩 Discrepancy sequence: radical-inverse (Halton/Hammersley) points + Cranley–Patterson rotation.
//
// The chain's first unit exists so reservoir candidates arrive stratified instead of clumped: at the
// 1-spp budgets ReSTIR runs at, pure pseudo-random candidates waste a visible fraction of samples on
// near-duplicates. This portable core generates the same 2D point sets the SDK-phase compute unit will
// bake into its candidate table; validation proves the discrepancy advantage over the PCG stream at
// equal counts, with identical seeds on every platform (double precision, integer index maths).

#pragma once

#include <cstdint>

namespace Frontier
{

struct SequencePoint
{
    double X = 0.0;   // [-] in [0, 1)
    double Y = 0.0;   // [-] in [0, 1)
};

// Radical inverse of Index in the given prime Base (van der Corput sequence). Base < 2 yields 0.
[[nodiscard]] double RadicalInverse(uint32_t Base, uint64_t Index) noexcept;

// Halton(2, 3) point Index (Index starts at 1 by convention; 0 maps to the origin corner).
[[nodiscard]] SequencePoint HaltonPoint(uint64_t Index) noexcept;

// Hammersley point Index of Count: (Index/Count, radicalInverse2(Index)). Count == 0 yields origin.
[[nodiscard]] SequencePoint HammersleyPoint(uint64_t Index, uint64_t Count) noexcept;

// Cranley–Patterson rotation: toroidal shift of the whole set by (ShiftX, ShiftY) — randomises the
// pattern per pixel/frame while preserving its stratification exactly.
[[nodiscard]] SequencePoint CranleyPatterson(SequencePoint Point, double ShiftX, double ShiftY) noexcept;

} // namespace Frontier
