/*============================================================================================================================================
                                                     VECTORCOVERAGETILEBINNING.CPP
============================================================================================================================================*/
// 🧩 Pure-CPU geometry: no Vulkan dependency, exercised directly by VectorCoverageTileBinningValidation.

#include "VectorCoverageTileBinning.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
// 📝 The program-space (not pixel-space) raw box, centred on the instruction's own local origin, before
// rotation/translation/padding. Categories 0-5 are all inscribed within their declared half-extent (every
// vertex formula in VectorCoverage.slang's ShapeDistance keeps |radius| <= 1 of HalfExtent); category 6's
// cubic segments are guaranteed to lie within the convex hull of their control points (standard Bezier
// property), so the control-point extrema are an exact, never-under-inclusive raw box.
bool ComputeLocalBox(const VectorCoverageInstruction &Instruction, std::span<const VectorCubicSegment> Segments,
    std::array<float, 4> &LocalBox) noexcept
{
    const auto Category = Instruction.Topology[0];
    if (Category <= 5)
    {
        const float HalfX = Instruction.Bounds[2] * .5f, HalfY = Instruction.Bounds[3] * .5f;
        LocalBox = {-HalfX, -HalfY, HalfX, HalfY};
        return true;
    }
    if (Category != 6) return false;
    // 📝 Fully inert: never contributes nonzero fill or stroke alpha anywhere (see VectorCoverage.slang's
    // "Unpainted construction contours evaluate no fill/stroke response" early return) -- no footprint.
    if (Instruction.Paint[3] == 0 && Instruction.Transform[1] <= 0) return false;
    const auto First = Instruction.Topology[1];
    const auto Count = Instruction.Topology[2];
    if (!Count || uint64_t(First) + Count > Segments.size()) return false;
    float MinX = 1e30f, MinY = 1e30f, MaxX = -1e30f, MaxY = -1e30f;
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const auto &Segment = Segments[First + Index];
        for (float X : {Segment.StartControl[0], Segment.StartControl[2], Segment.ControlEnd[0], Segment.ControlEnd[2]})
        {
            MinX = std::min(MinX, X * Instruction.Bounds[2]);
            MaxX = std::max(MaxX, X * Instruction.Bounds[2]);
        }
        for (float Y : {Segment.StartControl[1], Segment.StartControl[3], Segment.ControlEnd[1], Segment.ControlEnd[3]})
        {
            MinY = std::min(MinY, Y * Instruction.Bounds[3]);
            MaxY = std::max(MaxY, Y * Instruction.Bounds[3]);
        }
    }
    if (MinX > MaxX || MinY > MaxY) return false;
    LocalBox = {MinX, MinY, MaxX, MaxY};
    return true;
}
}

bool ComputeVectorCoverageFootprint(const VectorCoverageInstruction &Instruction,
    std::span<const VectorCubicSegment> Segments, float Antialias, std::array<float, 4> &Bounds) noexcept
{
    std::array<float, 4> LocalBox{};
    if (!ComputeLocalBox(Instruction, Segments, LocalBox)) return false;
    // 📝 Forward-rotate the box's four corners by the shape's own angle (the inverse of the shader's
    // query-point-into-local-space rotation; see EvaluateVectorPoint's Local computation) and translate by
    // its centre to recover the exact program-space footprint, matching the GPU evaluation bit-for-bit.
    const float Angle = Instruction.Transform[0];
    const float Cosine = std::cos(Angle), Sine = std::sin(Angle);
    float MinX = 1e30f, MinY = 1e30f, MaxX = -1e30f, MaxY = -1e30f;
    for (float LocalX : {LocalBox[0], LocalBox[2]})
        for (float LocalY : {LocalBox[1], LocalBox[3]})
        {
            const float WorldX = Instruction.Bounds[0] + Cosine * LocalX - Sine * LocalY;
            const float WorldY = Instruction.Bounds[1] + Sine * LocalX + Cosine * LocalY;
            MinX = std::min(MinX, WorldX); MaxX = std::max(MaxX, WorldX);
            MinY = std::min(MinY, WorldY); MaxY = std::max(MaxY, WorldY);
        }
    // 📝 Pad by whichever of fill or stroke reaches further: fill fades out at Antialias/2 past the raw
    // outline, stroke straddles the outline and fades out at half its width plus another Antialias/2.
    const float StrokeHalfWidth = std::max(Instruction.Transform[1], 0.f) * .5f;
    const float Padding = std::max(Antialias * .5f, StrokeHalfWidth + Antialias * .5f);
    Bounds = {MinX - Padding, MinY - Padding, MaxX + Padding, MaxY + Padding};
    return true;
}

VectorCoverageTileBinning BuildVectorCoverageTileBinning(const VectorCoverageProgram &Program, uint32_t Width,
    uint32_t Height, float Antialias, uint32_t TileSize)
{
    VectorCoverageTileBinning Binning;
    Binning.TilesPerRow = (Width + TileSize - 1) / TileSize;
    Binning.TilesPerColumn = (Height + TileSize - 1) / TileSize;
    const uint32_t TileCount = Binning.TilesPerRow * Binning.TilesPerColumn;
    Binning.Directory.assign(TileCount, {});
    if (!TileCount || Program.Instructions.empty()) return Binning;
    // 📝 Program space is always the fixed 512-unit logical canvas VectorCoverageCompute maps pixels into
    // (Point = (Pixel + .5) / Extent * 512), regardless of which pooled bake extent this dispatch targets.
    constexpr float ProgramExtent = 512.f;
    std::vector<std::array<uint32_t, 4>> TileRanges(Program.Instructions.size());
    std::vector<bool> HasFootprint(Program.Instructions.size(), false);
    std::vector<uint32_t> PerTileCount(TileCount, 0);
    for (size_t Index = 0; Index < Program.Instructions.size(); ++Index)
    {
        std::array<float, 4> Bounds{};
        if (!ComputeVectorCoverageFootprint(Program.Instructions[Index], Program.Segments, Antialias, Bounds)) continue;
        // 📝 Clip against the visible program-space window before mapping to pixels; a shape entirely
        // outside [0, 512) in either axis contributes to no tile (it would never reach a dispatched pixel).
        if (Bounds[2] <= 0 || Bounds[3] <= 0 || Bounds[0] >= ProgramExtent || Bounds[1] >= ProgramExtent) continue;
        const float PixelMinX = std::max(Bounds[0], 0.f) / ProgramExtent * Width;
        const float PixelMinY = std::max(Bounds[1], 0.f) / ProgramExtent * Height;
        const float PixelMaxX = std::min(Bounds[2], ProgramExtent) / ProgramExtent * Width;
        const float PixelMaxY = std::min(Bounds[3], ProgramExtent) / ProgramExtent * Height;
        if (PixelMaxX <= PixelMinX || PixelMaxY <= PixelMinY) continue;
        const uint32_t TileMinX = std::min(uint32_t(PixelMinX) / TileSize, Binning.TilesPerRow - 1);
        const uint32_t TileMinY = std::min(uint32_t(PixelMinY) / TileSize, Binning.TilesPerColumn - 1);
        const uint32_t TileMaxX = std::min(uint32_t(std::max(PixelMaxX - 1e-4f, PixelMinX)) / TileSize, Binning.TilesPerRow - 1);
        const uint32_t TileMaxY = std::min(uint32_t(std::max(PixelMaxY - 1e-4f, PixelMinY)) / TileSize, Binning.TilesPerColumn - 1);
        TileRanges[Index] = {TileMinX, TileMinY, TileMaxX, TileMaxY};
        HasFootprint[Index] = true;
        for (uint32_t Y = TileMinY; Y <= TileMaxY; ++Y)
            for (uint32_t X = TileMinX; X <= TileMaxX; ++X)
                ++PerTileCount[Y * Binning.TilesPerRow + X];
    }
    uint32_t Running = 0;
    for (uint32_t Tile = 0; Tile < TileCount; ++Tile)
    {
        Binning.Directory[Tile] = {Running, PerTileCount[Tile]};
        Running += PerTileCount[Tile];
    }
    Binning.Indices.resize(Running);
    std::vector<uint32_t> Cursor(TileCount, 0);
    for (size_t Index = 0; Index < Program.Instructions.size(); ++Index)
    {
        if (!HasFootprint[Index]) continue;
        const auto &[TileMinX, TileMinY, TileMaxX, TileMaxY] = TileRanges[Index];
        for (uint32_t Y = TileMinY; Y <= TileMaxY; ++Y)
            for (uint32_t X = TileMinX; X <= TileMaxX; ++X)
            {
                const uint32_t Tile = Y * Binning.TilesPerRow + X;
                Binning.Indices[Binning.Directory[Tile].Offset + Cursor[Tile]++] = uint32_t(Index);
            }
    }
    return Binning;
}
}
