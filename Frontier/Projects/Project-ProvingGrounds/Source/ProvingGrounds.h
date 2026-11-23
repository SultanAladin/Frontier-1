/*==========================================================================================================================================
                                                              PROVING GROUNDS
                                     ------------------------------------------------------------------
                              The procedural first scene — a spinning cube over a ground plane, engine-proved.
==========================================================================================================================================*/

// 🧩 Proving Grounds: the procedural first scene — a spinning cube over a ground plane, engine-proved.

#pragma once

#include "Engine/Foundation/Math.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ProvingGroundsVertex
{
    Vec3 Position;
    Vec3 Normal;
    Vec3 Colour;   // [linear] - Per-face tint so rotation is visible without textures
};

struct ProvingGroundsGeometry
{
    std::vector<ProvingGroundsVertex> Vertices;
    std::vector<uint32_t>             Indices;   // [indices] - Triangle list
};

// What the game division publishes and the render division consumes (SnapshotExchange payload):
// everything one frame of viewport rendering needs, nothing it doesn't.
struct ProvingGroundsSnapshot
{
    Mat4     ModelToWorld;
    Mat4     ViewProjection;
    uint64_t FrameIndex        = 0;   // [frames]
    double   SimulatedSeconds  = 0;   // [s]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Cube (24 vertices, 36 indices, one tint per face) + ground plane (4 vertices, 6 indices).
[[nodiscard]] ProvingGroundsGeometry BuildProvingGroundsGeometry();

// Deterministic spin simulation: fixed angular rate about +Y through the cube centre,
// camera locked at a three-quarter view. Advance on the game side, snapshot for render.
class ProvingGroundsSimulation
{
  public:
    static constexpr double AngularRate = 0.6;   // [rad/s]

    void Advance(double Δτ) noexcept;

    [[nodiscard]] ProvingGroundsSnapshot TakeSnapshot(uint32_t ViewportWidth, uint32_t ViewportHeight) const noexcept;
    [[nodiscard]] double QueryAngle() const noexcept { return Angle; }              // [rad]
    [[nodiscard]] uint64_t QueryFrameIndex() const noexcept { return FrameIndex; }  // [frames]

  private:
    double   Angle            = 0;   // [rad]
    double   SimulatedSeconds = 0;   // [s]
    uint64_t FrameIndex       = 0;   // [frames]
};

} // namespace Frontier
