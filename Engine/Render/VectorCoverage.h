/*============================================================================================================================================
                                                             VECTORCOVERAGE.H
============================================================================================================================================*/
// 🧩 Rebuildable CPU descriptors for GPU vector coverage, non-destructive Boolean expressions and masking.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Frontier
{
inline constexpr uint32_t VectorCoverageStrokeColourExplicit = 0x80000000u;
// 📝 Keep these two mirrored exactly in Engine/Render/Shader/VectorCoverage.slang and VectorSurface.slang
// (fixed-size Evaluations[]/Instructions[] array bounds there cannot include this header). Raising the
// instruction capacity lets one composited document hold hundreds of shapes instead of the former 128-node
// wall; the extension region packs the same 64-slot budget already shared by selection candidates and
// surface contributions, now addressed starting right after the (larger) instruction region.
inline constexpr uint32_t VectorCoverageInstructionCapacity = 256;
inline constexpr uint32_t VectorCoverageExtensionCapacity = 64;
// 📝 Wang's formula (Wang 1984): the number of uniform chords needed to flatten a cubic Bezier so no point
// on the chord polyline deviates from the true analytic curve by more than Tolerance. Mirror this exactly
// in VectorCoverage.slang and VectorSurface.slang's ShapeDistance (Slang/HLSL cannot include this header,
// so GPU adaptive curve flattening is derived/tested here and hand-copied there). This CPU copy exists to
// validate the formula and its clamping in isolation with real unit tests -- it is not itself called by
// the GPU rasterizer and cannot by itself prove the hand-copied Slang text matches bit-for-bit; shader
// execution still cannot be verified in this environment (see VectorCoverageCurveFlatteningValidation.cpp).
// Small/flat ornamental curves (the common case across hundreds of authored shapes, previously forced
// through a fixed 32-chord flatten regardless of size) collapse to as few as 2 chords; sharp or large
// curves still subdivide enough to stay within Tolerance of the analytic curve.
[[nodiscard]] inline uint32_t EstimateCubicFlattenSegments(std::array<float, 2> First, std::array<float, 2> Second,
    std::array<float, 2> Third, std::array<float, 2> Fourth, float Tolerance) noexcept
{
    const auto Deviation = [](std::array<float, 2> A, std::array<float, 2> B, std::array<float, 2> C)
    {
        const float X = A[0] - 2 * B[0] + C[0], Y = A[1] - 2 * B[1] + C[1];
        return std::sqrt(X * X + Y * Y);
    };
    const float ControlDeviation = std::max(Deviation(First, Second, Third), Deviation(Second, Third, Fourth));
    const float Bounded = std::max(Tolerance, 1e-4f);
    const float Count = std::ceil(std::sqrt(ControlDeviation * 3.f / (4.f * Bounded)));
    return uint32_t(std::clamp(Count, 2.f, 32.f));
}
enum class VectorCoverageCategory : uint32_t
{
    Rectangle, Ellipse, Diamond, Triangle, Polygon, Star, CubicContour,
    Union = 16, Difference, Intersection, Exclude,
    AlphaMask = 24, LuminanceMask, Over = 28
};
struct alignas(16) VectorCoverageInstruction
{
    std::array<uint32_t, 4> Topology{};
    std::array<float, 4> Bounds{256, 256, 100, 100};
    std::array<float, 4> Transform{0, 0, 1, 0};
    std::array<float, 4> Colour{1, 1, 1, 1};
    std::array<float, 4> ColourEnd{1, 1, 1, 1};
    std::array<float, 4> StrokeColour{1, 1, 1, 1};
    std::array<float, 4> Paint{0, .45f, 6, 1};
    bool operator==(const VectorCoverageInstruction &) const = default;
};
struct alignas(16) VectorCubicSegment
{
    std::array<float, 4> StartControl{};
    std::array<float, 4> ControlEnd{};
    bool operator==(const VectorCubicSegment &) const = default;
};
static_assert(sizeof(VectorCoverageInstruction) == 112 && sizeof(VectorCubicSegment) == 32);
struct VectorCoverageProgram
{
    std::vector<VectorCoverageInstruction> Instructions;
    std::vector<VectorCubicSegment> Segments;
    uint32_t Root = 0;
    bool operator==(const VectorCoverageProgram &) const = default;
};
struct VectorSelectionProgram
{
    VectorCoverageProgram Coverage;
    // [idx] - Ordered coverage roots and authenticated source tokens, lowest to highest.
    std::vector<std::array<uint32_t, 2>> Candidates;
};
inline void ValidateVectorCoverage(const VectorCoverageProgram &Program)
{
    if (Program.Instructions.empty() || Program.Instructions.size() > VectorCoverageInstructionCapacity || Program.Root >= Program.Instructions.size() || Program.Segments.size() > 32768)
        throw std::runtime_error("Vector coverage exceeds bounded GPU program limits.");
    for (size_t Index = 0; Index < Program.Instructions.size(); ++Index)
    {
        const auto &Instruction = Program.Instructions[Index];
        for (const auto &Coordinates : {Instruction.Bounds, Instruction.Transform, Instruction.Colour, Instruction.ColourEnd, Instruction.StrokeColour, Instruction.Paint})
            for (float Coordinate : Coordinates) if (!std::isfinite(Coordinate)) throw std::runtime_error("Non-finite vector coverage descriptor.");
        const auto Category = Instruction.Topology[0];
        if (Category <= 6)
        {
            if (std::abs(Instruction.Bounds[0]) > 16384 || std::abs(Instruction.Bounds[1]) > 16384 || Instruction.Bounds[2] < .01f || Instruction.Bounds[3] < .01f || Instruction.Bounds[2] > 8192 || Instruction.Bounds[3] > 8192)
                throw std::runtime_error("Vector extent exceeds the finite GPU authoring domain.");
            if (Category == 6 && (Instruction.Topology[2] == 0 || uint64_t(Instruction.Topology[1]) + Instruction.Topology[2] > Program.Segments.size())) throw std::runtime_error("Cubic segment range is invalid.");
            if (Category == 6 && Instruction.Paint[3] != 0)
            {
                std::array<float, 2> ContourStart{}, PreviousEnd{};
                const auto Coincident = [](const std::array<float, 2> &First, const std::array<float, 2> &Second)
                {
                    return std::abs(First[0] - Second[0]) <= .000001f && std::abs(First[1] - Second[1]) <= .000001f;
                };
                for (uint32_t Curve = 0; Curve < Instruction.Topology[2]; ++Curve)
                {
                    const auto &Segment = Program.Segments[Instruction.Topology[1] + Curve];
                    const std::array<float, 2> Start{Segment.StartControl[0], Segment.StartControl[1]};
                    if (!Curve) ContourStart = Start;
                    else if (!Coincident(Start, PreviousEnd))
                    {
                        if (!Coincident(PreviousEnd, ContourStart)) throw std::runtime_error("GPU fill contours must be explicitly closed.");
                        ContourStart = Start;
                    }
                    PreviousEnd = {Segment.ControlEnd[2], Segment.ControlEnd[3]};
                }
                if (!Coincident(PreviousEnd, ContourStart)) throw std::runtime_error("GPU fill contour is open.");
            }
            if ((Category == 4 || Category == 5) && (Instruction.Paint[2] < 3 || Instruction.Paint[2] > 32 || std::floor(Instruction.Paint[2]) != Instruction.Paint[2])) throw std::runtime_error("Polygon corner count must be 3–32.");
            if (Category == 5 && (Instruction.Paint[1] < .05f || Instruction.Paint[1] > .95f)) throw std::runtime_error("Star inner radius is invalid.");
        }
        else if ((Category >= 16 && Category <= 19) || Category == 24 || Category == 25 || Category == 28)
        {
            if (Instruction.Topology[1] >= Index || Instruction.Topology[2] >= Index) throw std::runtime_error("Vector coverage operands must precede their operation; cycles are forbidden.");
            if ((Category == 24 || Category == 25) && Instruction.Topology[3] > 1) throw std::runtime_error("Mask inversion must be zero or one.");
        }
        else throw std::runtime_error("Unsupported GPU vector coverage operation.");
        if (Instruction.Transform[1] < 0 || Instruction.Transform[1] > 100 || Instruction.Transform[2] < 0 || Instruction.Transform[2] > 1 || Instruction.Transform[3] < 0 || Instruction.Transform[3] > 2 || std::floor(Instruction.Transform[3]) != Instruction.Transform[3] || std::abs(Instruction.Transform[0]) > 1e6f || std::abs(Instruction.Paint[0]) > 1e6f || (Instruction.Paint[3] != 0 && Instruction.Paint[3] != 1))
            throw std::runtime_error("Vector stroke, opacity or paint category is invalid.");
        for (float Channel : Instruction.Colour) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector colour must be normalized.");
        for (float Channel : Instruction.ColourEnd) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector gradient colour must be normalized.");
        for (float Channel : Instruction.StrokeColour) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector stroke colour must be normalized.");
    }
    for (const auto &Segment : Program.Segments)
        for (const auto &Coordinates : {Segment.StartControl, Segment.ControlEnd})
            for (float Coordinate : Coordinates) if (!std::isfinite(Coordinate) || std::abs(Coordinate) > 16) throw std::runtime_error("Cubic control coordinate exceeds its normalized GPU domain.");
}
inline void ValidateVectorSelection(const VectorSelectionProgram &Program)
{
    ValidateVectorCoverage(Program.Coverage);
    if (Program.Candidates.size() > VectorCoverageExtensionCapacity) throw std::runtime_error("Vector selection exceeds candidate capacity.");
    for (size_t Index = 0; Index < Program.Candidates.size(); ++Index)
    {
        const auto &Candidate = Program.Candidates[Index];
        if (Candidate[0] >= Program.Coverage.Instructions.size() || !Candidate[1])
            throw std::runtime_error("Invalid vector selection root or source token.");
        for (size_t Earlier = 0; Earlier < Index; ++Earlier)
            if (Program.Candidates[Earlier][1] == Candidate[1]) throw std::runtime_error("Duplicate vector selection token.");
    }
}

}
