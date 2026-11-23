/*==========================================================================================================================================
                                                    ILLUMINATION FRAME DECLARATION
                                     ------------------------------------------------------------------
                 The ReSTIR + SVGF chain (plan §8.2) declared as ordered submission units over
                      named targets, so ordering, aliasing and barriers derive from it centrally.
==========================================================================================================================================*/

// 🧩 Illumination frame declaration: the §8.2 chain as data — units + the targets they touch.
//
//   [indirect prelude] DistanceRecords → DistanceTraversal ┐
//                        CardCapture → CardDirect → CardRadiosity → CardFinal ┴→ DistanceTransfer
//     → DiscrepancySequence → ClusteredLights → DirectRadiance (initial candidates, +IndirectRadiance)
//     → ReservoirTemporal (prev-frame import) → ReservoirSpatial (intended reuse stage)
//     → MaterialResolve → SurfaceReflectance (shade the winning sample)
//     → VarianceTemporal → VarianceWavelet ×5 → VarianceUpsample
//
// Target classes follow §7.1: the reservoir previous-frame import, lighting history and moments are
// Persistent (never atlas-aliased — aliasing a ping-pong pair would corrupt temporal reuse), the
// surface attributes are an imported read from the surface phase, and everything else is transient.
// Reservoir records use the Rg32Uint class declared for them in RenderTarget.h. These are declarations,
// not proof that the named shaders execute or that their reuse equations are unbiased. The portable layer
// orders target touches and derives barriers; Exhibits/IlluminationChain tests selected CPU mathematics.
// The retained demo's temporal reconnection has a separately documented missing-Jacobian counterexample.

#pragma once

#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct IlluminationFrameDeclaration
{
    std::vector<SubmissionUnitDeclaration> Units;
    std::vector<RenderTargetDeclaration>   Targets;
};

// Number of à-trous levels the wavelet cascade declares (steps 1, 2, 4, 8, 16).
inline constexpr uint32_t IlluminationWaveletLevels = 5;

// Declares the chain for a Width×Height frame. ReservoirsSpatial becomes next frame's
// ReservoirsPrevious by host ping-pong swap; that swap is why both are declared off-atlas.
// ReservoirsSpatial rests at StorageWrite (its last touch in the frame that produced it).
[[nodiscard]] IlluminationFrameDeclaration DeclareIlluminationFrame(uint32_t Width, uint32_t Height);

} // namespace Frontier
