/*==========================================================================================================================================
                                                    DETERMINISTIC RANDOM STREAMS
                                     ------------------------------------------------------------------
                 One integer-only generator (PCG32) so every stochastic proof in this engine replays
                      bit-exactly on any platform: same seed, same stream, same verdict.
==========================================================================================================================================*/

// 🧩 Deterministic random: PCG32 (O'Neill 2014) — 64-bit state, 32-bit output, one stream per instance.
//
// Why this lives in Foundation: reservoirs, discrepancy rotations, validation scenes and (at the SDK
// phase) host-side shader seed tables all need the *same* reproducible stream. Integer arithmetic only,
// so results never depend on the host FPU. This is a sampling utility, not a cryptographic source.

#pragma once

#include <cstdint>

namespace Frontier
{

class DeterministicRandom
{
  public:
    DeterministicRandom() noexcept { Seed(0x853c49e6748fea9bull, 0xda3e39cb94b95bdbull); }

    DeterministicRandom(uint64_t StateSeed, uint64_t StreamSeed) noexcept { Seed(StateSeed, StreamSeed); }

    // Standard PCG seeding: warm up once, fold in the state seed, warm up again.
    void Seed(uint64_t StateSeed, uint64_t StreamSeed) noexcept
    {
        State_     = 0;
        Increment_ = (StreamSeed << 1u) | 1u;
        (void)NextUInt32();
        State_ += StateSeed;
        (void)NextUInt32();
    }

    [[nodiscard]] uint32_t NextUInt32() noexcept
    {
        const uint64_t OldState = State_;
        State_                  = OldState * 6364136223846793005ull + Increment_;
        const uint32_t XorShifted = static_cast<uint32_t>(((OldState >> 18u) ^ OldState) >> 27u);
        const uint32_t Rotate     = static_cast<uint32_t>(OldState >> 59u);
        return (XorShifted >> Rotate) | (XorShifted << ((0u - Rotate) & 31u));
    }

    // [0, 1) with 32-bit granularity. Double so reservoir comparisons never quantise at float epsilon.
    [[nodiscard]] double NextUnit() noexcept
    {
        return static_cast<double>(NextUInt32()) * (1.0 / 4294967296.0);
    }

    // Uniform integer in [0, Bound). Bound == 0 yields 0 (refused range, defined result).
    [[nodiscard]] uint32_t NextBelow(uint32_t Bound) noexcept
    {
        if (Bound == 0)
        {
            return 0;
        }
        // Lemire multiply-shift: unbiased without a modulo loop for the bounds this engine uses.
        uint32_t Product = NextUInt32();
        uint64_t Wide    = static_cast<uint64_t>(Product) * Bound;
        uint32_t Low     = static_cast<uint32_t>(Wide);
        if (Low < Bound)
        {
            const uint32_t Threshold = (0u - Bound) % Bound;
            while (Low < Threshold)
            {
                Product = NextUInt32();
                Wide    = static_cast<uint64_t>(Product) * Bound;
                Low     = static_cast<uint32_t>(Wide);
            }
        }
        return static_cast<uint32_t>(Wide >> 32u);
    }

    [[nodiscard]] uint64_t StateForInspection() const noexcept { return State_; }

  private:
    uint64_t State_     = 0;
    uint64_t Increment_ = 0;
};

} // namespace Frontier
