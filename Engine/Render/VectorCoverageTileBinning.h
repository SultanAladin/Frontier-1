/*============================================================================================================================================
                                                      VECTORCOVERAGETILEBINNING.H
============================================================================================================================================*/
// 🧩 CPU-computed per-tile shape culling for the Vulkan vector coverage compute shader: the same analytic
// signed-distance evaluation stays exact (and therefore crisp at any zoom/resolution), but a pixel only
// walks the shapes whose padded footprint actually reaches its tile instead of every shape in the document.
// Sized for "hundreds of shapes" bound CPU-side once per upload (microseconds), not the millions-of-paths,
// GPU-driven binning used by Vello/Spinel -- this is the lighter-weight technique those renderers are built
// on, adapted to this engine's existing immediate-mode Vulkan/Slang pipeline rather than ported wholesale.

#pragma once
#include "VectorCoverage.h"
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{
// 📝 Keep mirrored exactly in Engine/Render/Shader/VectorCoverage.slang (TileSize constant there).
inline constexpr uint32_t VectorCoverageTileSize = 32;

struct VectorCoverageTileDirectoryEntry
{
    uint32_t Offset = 0;
    uint32_t Count = 0;
    bool operator==(const VectorCoverageTileDirectoryEntry &) const = default;
};

struct VectorCoverageTileBinning
{
    uint32_t TilesPerRow = 0;
    uint32_t TilesPerColumn = 0;
    // 📝 Row-major, one entry per tile; Directory[Y * TilesPerRow + X].
    std::vector<VectorCoverageTileDirectoryEntry> Directory;
    // 📝 Flat compacted leaf-instruction indices, CSR-addressed through Directory; never longer than
    // (TilesPerRow * TilesPerColumn * VectorCoverageInstructionCapacity) even in the fully-overlapping case.
    std::vector<uint32_t> Indices;
};

// 📝 Exact (never under-inclusive) program-space bounding box of one leaf instruction's painted footprint,
// already padded by its own stroke half-width and the supplied antialias half-band. Boolean/mask instructions
// (Category >= 16) and fully-inert unpainted construction contours (Category 6, Paint.w == 0, Transform.y <= 0 --
// see VectorCoverage.slang's early "no fill/stroke response" return) have no footprint: callers skip binning
// them entirely, which is output-identical because the shader always default-initializes their evaluation
// slot to zero and every consumer in this file's compute shader only ever reads an operand through its
// saturated fill/stroke alpha (or that alpha's complement), so a shape that cannot move any pixel past zero
// alpha is safe to omit from every tile. Returns false (no footprint) rather than an empty box so callers
// never have to special-case a box with Minimum > Maximum.
[[nodiscard]] bool ComputeVectorCoverageFootprint(const VectorCoverageInstruction &Instruction,
    std::span<const VectorCubicSegment> Segments, float Antialias, std::array<float, 4> &Bounds) noexcept;

// 📝 Built once per ConfigureRetiredProgram upload (CPU, no GPU dependency): buckets every leaf instruction's
// padded footprint into the Width x Height output image's pixel-space tile grid. Antialias must match the
// value the paired dispatch will use (512 / min(Width, Height), the same formula VectorCoverageCompute uses)
// so a shape's fade-to-zero distance and this culling boundary agree exactly.
[[nodiscard]] VectorCoverageTileBinning BuildVectorCoverageTileBinning(
    const VectorCoverageProgram &Program, uint32_t Width, uint32_t Height, float Antialias,
    uint32_t TileSize = VectorCoverageTileSize);
}
