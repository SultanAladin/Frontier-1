/*============================================================================================================================================
                                                            PATTERNLIBRARYPRESETS.H
============================================================================================================================================*/
// 🧩 Seamless starter patterns for the Pattern library gallery. Each preset is ONE retained motif
// expanded by the array generator (single shape, arrayed and scaled), never hand-stamped duplicates;
// alternating inks recolour the expanded copies in placement order.

#pragma once
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "Engine/Render/VectorCoverage.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace Frontier
{
inline size_t QueryPatternLibraryPresetCount() noexcept { return 8; }

inline const char *QueryPatternLibraryPresetName(size_t Index)
{
    constexpr const char *Names[]{"Polka Dots", "Half Drop", "Stripes", "Checker", "Diamond Lattice", "Starfield",
        "Diamond Weave", "Pebble Path"};
    if (Index >= QueryPatternLibraryPresetCount()) throw std::runtime_error("Unknown pattern library preset.");
    return Names[Index];
}

// 📝 Composable motif modifiers, mirroring the HTML pattern studio's arrayInset/mirrorMotif/
// alignToEdges (Experimental/TextureStudio/src/patternProcedural.js) so both authoring surfaces
// reconstruct library motifs from the same few reusable operations instead of hand-placed copies.
// An "inset array" is just the existing array generator with StepX=StepY=0 and a Linear scale
// profile: identical centred copies shrinking by a fixed amount per ring (see MirrorVectorAuthoring-
// Motif's caller below) -- no bespoke primitive needed for that step.

// 📝 Grid array: a 2D repeat of one base instruction across Columns x Rows, the C++ counterpart of
// the HTML pattern studio's gridArray (Experimental/TextureStudio/src/patternProcedural.js) -- a
// per-row X offset (brick/herringbone courses), and per-cell size/colour/rotation callbacks (a
// distance/gradient size profile, checkerboards, alternating motifs). Each callback may be empty to
// keep the base instruction's value.
inline std::vector<VectorCoverageInstruction> ConstructGridMotif(const VectorCoverageInstruction &Base,
    uint32_t Columns, uint32_t Rows, float StepX, float StepY,
    const std::function<float(uint32_t)> &RowOffsetX = nullptr,
    const std::function<float(uint32_t, uint32_t)> &SizeAt = nullptr,
    const std::function<std::array<float, 4>(uint32_t, uint32_t)> &ColourAt = nullptr,
    const std::function<float(uint32_t, uint32_t)> &RotationAt = nullptr)
{
    if (Columns == 0 || Rows == 0) throw std::runtime_error("ConstructGridMotif needs positive columns/rows.");
    std::vector<VectorCoverageInstruction> Motif;
    Motif.reserve(size_t(Columns) * size_t(Rows));
    for (uint32_t Row = 0; Row < Rows; ++Row)
    {
        const float OffsetX = RowOffsetX ? RowOffsetX(Row) : 0.f;
        for (uint32_t Column = 0; Column < Columns; ++Column)
        {
            VectorCoverageInstruction Cell = Base;
            Cell.Bounds[0] = Base.Bounds[0] + OffsetX + StepX * float(Column);
            Cell.Bounds[1] = Base.Bounds[1] + StepY * float(Row);
            if (SizeAt)
            {
                const float Size = SizeAt(Row, Column);
                Cell.Bounds[2] = Size;
                Cell.Bounds[3] = Size;
            }
            if (ColourAt) { Cell.Colour = ColourAt(Row, Column); Cell.ColourEnd = Cell.Colour; }
            if (RotationAt) Cell.Transform[0] = RotationAt(Row, Column);
            Motif.push_back(Cell);
        }
    }
    return Motif;
}

// 📝 Scatter placement: a deterministic pseudo-random position for motif copy `Index`, the C++
// counterpart of the HTML scatterPlacement tool -- two coprime-ish strides modulo the tile size,
// instead of inlined magic-number arithmetic at each call site.
inline std::array<float, 2> QueryScatterPlacement(uint32_t Index, float StrideX = 197, float StrideY = 113,
    float OffsetX = 0, float OffsetY = 0, float Width = 512, float Height = 512)
{
    const auto Wrap = [](float Value, float Span) { return std::fmod(std::fmod(Value, Span) + Span, Span); };
    return {Wrap(float(Index) * StrideX + OffsetX, Width), Wrap(float(Index) * StrideY + OffsetY, Height)};
}

// 📝 Fibonacci square spiral: the C++ counterpart of the HTML fibonacciSquareSpiral tool -- each
// successive square's side is the next Fibonacci number, attached to the growing bounding rectangle
// rotating through right/up/left/down, computed from the recurrence rather than a transcribed
// coordinate list. Returns `Count` squares in local, un-scaled spiral units (square 0 is [0, 0, 1]).
struct FibonacciSquare { float X, Y, Side; };
inline std::vector<FibonacciSquare> ConstructFibonacciSquareSpiral(uint32_t Count)
{
    if (Count == 0) throw std::runtime_error("ConstructFibonacciSquareSpiral needs a positive count.");
    std::vector<float> Fibonacci{1, 1};
    while (Fibonacci.size() < Count) Fibonacci.push_back(Fibonacci[Fibonacci.size() - 1] + Fibonacci[Fibonacci.size() - 2]);
    struct { float XMin = 0, YMin = 0, XMax = 0, YMax = 0; } Bounds;
    std::vector<FibonacciSquare> Squares;
    Squares.reserve(Count);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const float Side = Fibonacci[Index];
        float X, Y;
        if (Index == 0) { X = 0; Y = 0; Bounds.XMax = Side; Bounds.YMax = Side; }
        else
            switch ((Index - 1) % 4)
            {
            case 0: X = Bounds.XMax; Y = Bounds.YMin; Bounds.XMax += Side; break; // right
            case 1: X = Bounds.XMin; Y = Bounds.YMin - Side; Bounds.YMin -= Side; break; // up
            case 2: X = Bounds.XMin - Side; Y = Bounds.YMin; Bounds.XMin -= Side; break; // left
            default: X = Bounds.XMin; Y = Bounds.YMax; Bounds.YMax += Side; break; // down
            }
        Squares.push_back({X, Y, Side});
    }
    return Squares;
}

// 📝 Array/Inset: runs the existing generic array generator (same engine the Stamp/Grid presets
// above already use) against a throwaway scratch document and harvests the resulting instructions as
// a plain, uncommitted list -- the array/inset step from the user's own worked example ("1 weave
// inner and outer ... an array/inset"), reusing one tested placement engine instead of a bespoke
// shrink loop.
inline std::vector<VectorCoverageInstruction> ConstructInsetMotif(
    const VectorCoverageInstruction &Seed, const std::string &Caption, const VectorAuthoringArraySpecification &Specification)
{
    VectorAuthoringDocument Scratch;
    const uint32_t SeedToken = ConstructVectorAuthoringPrimitive(Scratch, Seed, Caption);
    const auto Result = GenerateVectorAuthoringArray(Scratch, std::span<const uint32_t>{&SeedToken, 1}, Specification);
    std::vector<VectorCoverageInstruction> Motif;
    Motif.reserve(Result.Selection.size());
    for (const uint32_t Token : Result.Selection)
        for (const auto &Layer : Result.Document.Layers)
            if (Layer.Token == Token) { Motif.push_back(Result.Document.Instructions[Layer.Root]); break; }
    return Motif;
}

// 📝 Mirror: returns the motif plus one reflected copy of each instruction, across a horizontal
// ("y") or vertical ("x") line through `Through`. A reflection always flips handedness, so any
// rotation negates.
inline std::vector<VectorCoverageInstruction> MirrorVectorAuthoringMotif(
    std::vector<VectorCoverageInstruction> Motif, char Axis, float Through)
{
    const size_t Count = Motif.size();
    for (size_t Index = 0; Index < Count; ++Index)
    {
        VectorCoverageInstruction Reflected = Motif[Index];
        if (Axis == 'y') Reflected.Bounds[1] = 2 * Through - Reflected.Bounds[1];
        else if (Axis == 'x') Reflected.Bounds[0] = 2 * Through - Reflected.Bounds[0];
        else throw std::runtime_error("MirrorVectorAuthoringMotif axis must be 'x' or 'y'.");
        if (Reflected.Transform[0] != 0) Reflected.Transform[0] = -Reflected.Transform[0];
        Motif.push_back(Reflected);
    }
    return Motif;
}

// 📝 Align-to-edge: returns one translated copy of the motif per anchor (anchor coordinates
// translate the motif's own local bounds, authored as offsets, to a tile-edge-relative position).
// `Recolour` optionally replaces instructions whose colour equals `AccentColour`, so a shared ring
// structure can still carry a different accent per edge.
inline std::vector<VectorCoverageInstruction> AlignVectorAuthoringMotifToEdges(
    const std::vector<VectorCoverageInstruction> &Motif, std::span<const std::array<float, 2>> Anchors,
    const std::array<float, 4> &AccentColour, std::span<const std::array<float, 4>> Recolour)
{
    if (Anchors.size() != Recolour.size()) throw std::runtime_error("Each edge anchor needs its own accent recolour.");
    std::vector<VectorCoverageInstruction> Placed;
    Placed.reserve(Motif.size() * Anchors.size());
    for (size_t Edge = 0; Edge < Anchors.size(); ++Edge)
        for (const auto &Source : Motif)
        {
            VectorCoverageInstruction Copy = Source;
            Copy.Bounds[0] += Anchors[Edge][0];
            Copy.Bounds[1] += Anchors[Edge][1];
            if (Copy.Colour == AccentColour) { Copy.Colour = Recolour[Edge]; Copy.ColourEnd = Recolour[Edge]; }
            Placed.push_back(Copy);
        }
    return Placed;
}

// 📝 Every motif stays inside the 512 tile except deliberate half-drop seam twins,
// so each preset repeats without visible discontinuities.
inline VectorAuthoringDocument ConstructPatternLibraryPreset(size_t Index)
{
    const char *Name = QueryPatternLibraryPresetName(Index);
    VectorAuthoringDocument Document;
    Document.Name = Name;
    const std::array<std::array<float, 4>, 2> Palette{{{.8627f, .4627f, .3529f, 1}, {.35f, .55f, .45f, 1}}};

    // 📝 One motif at the lattice centroid (array placements centre on the motif bounds), expanded
    // by GenerateVectorAuthoringArray; Ink recolours copies in row-major placement order.
    const auto Stamp = [&](VectorAuthoringDocument Base, uint32_t Category, float CentreX, float CentreY,
                           float Width, float Height, const char *Stem,
                           const VectorAuthoringArraySpecification &Specification, uint32_t Columns,
                           const std::function<size_t(uint32_t, uint32_t)> &Ink)
    {
        VectorCoverageInstruction Source;
        Source.Topology[0] = Category;
        Source.Bounds = {CentreX, CentreY, Width, Height};
        Source.Colour = Palette[0];
        Source.ColourEnd = Source.Colour;
        Source.StrokeColour = {.23f, .24f, .28f, 1};
        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
        const uint32_t Token = ConstructVectorAuthoringPrimitive(Base, Source, Stem);
        auto Result = GenerateVectorAuthoringArray(Base, std::span<const uint32_t>{&Token, 1}, Specification);
        for (size_t Copy = 0; Copy < Result.Selection.size(); ++Copy)
        {
            const size_t Pick = Ink(uint32_t(Copy / Columns), uint32_t(Copy % Columns)) % Palette.size();
            for (auto &Layer : Result.Document.Layers)
                if (Layer.Token == Result.Selection[Copy])
                {
                    auto &Instruction = Result.Document.Instructions[Layer.Root];
                    Instruction.Colour = Palette[Pick];
                    Instruction.ColourEnd = Palette[Pick];
                }
        }
        return std::move(Result.Document);
    };
    const auto Grid = [](uint32_t Columns, uint32_t Rows, float StepX, float StepY)
    {
        VectorAuthoringArraySpecification Specification;
        Specification.Category = VectorAuthoringArrayCategory::Grid;
        Specification.Columns = Columns;
        Specification.Rows = Rows;
        Specification.StepX = StepX;
        Specification.StepY = StepY;
        return Specification;
    };

    switch (Index)
    {
    case 0: // Polka Dots: 4x4 dots, checkerboard ink.
        return Stamp(std::move(Document), 1, 256, 256, 88, 88, "Polka dot", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t Column) { return size_t((Row + Column) % 2); });
    case 1: // Half Drop: even rows on the 128 lattice, odd rows shifted with seam twins at x 0/512.
        Document = Stamp(std::move(Document), 1, 256, 192, 72, 72, "Drop dot", Grid(4, 2, 128, 256), 4,
            [](uint32_t, uint32_t) { return size_t(0); });
        return Stamp(std::move(Document), 1, 256, 320, 72, 72, "Drop dot", Grid(5, 2, 128, 256), 5,
            [](uint32_t, uint32_t) { return size_t(1); });
    case 2: // Stripes: one bar in a 4-step linear array.
    {
        VectorAuthoringArraySpecification Specification;
        Specification.Category = VectorAuthoringArrayCategory::Linear;
        Specification.Count = 4;
        Specification.StepX = 128;
        Specification.StepY = 0;
        return Stamp(std::move(Document), 0, 256, 256, 56, 512, "Stripe", Specification, 4,
            [](uint32_t, uint32_t Column) { return size_t(Column % 2); });
    }
    case 3: // Checker: the two interleaved 128 sublattices of the 64 board.
        Document = Stamp(std::move(Document), 0, 224, 224, 64, 64, "Check", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t) { return size_t(Row % 2); });
        return Stamp(std::move(Document), 0, 288, 288, 64, 64, "Check", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t) { return size_t((Row + 1) % 2); });
    case 4: // Diamond Lattice: 4x4 diamonds, checkerboard ink.
        return Stamp(std::move(Document), 2, 256, 256, 104, 104, "Diamond", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t Column) { return size_t((Row + Column) % 2); });
    case 5: // Starfield: 4x4 stars, column ink.
        return Stamp(std::move(Document), 5, 256, 256, 92, 92, "Star", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t Column) { return size_t((Row * 2 + Column) % 2); });
    case 6: // Diamond Weave: one ring array/inset (the user's own worked example), mirrored across
            // the tile's vertical midline, then aligned to the left/right edge quarters with a
            // distinct accent per edge -- the composable procedural stack the whole library is
            // meant to be reconstructed from.
    {
        VectorCoverageInstruction Field;
        Field.Topology[0] = 0;
        Field.Bounds = {256, 256, 512, 512};
        Field.Colour = {.1490f, .2353f, .2824f, 1};
        Field.ColourEnd = Field.Colour;
        Field.StrokeColour = Field.Colour;
        ConstructVectorAuthoringPrimitive(Document, Field, "Weave field");

        const std::array<float, 4> AccentMarker{0, 0, 0, 0}; // Overwritten per edge below; never rendered.
        const std::array<float, 4> Ground{.1490f, .2353f, .2824f, 1};
        VectorCoverageInstruction Seed;
        Seed.Topology[0] = 2;
        Seed.Bounds = {0, 128, 242, 242};
        Seed.Colour = AccentMarker;
        Seed.ColourEnd = AccentMarker;
        Seed.StrokeColour = {.23f, .24f, .28f, 1};
        Seed.Topology[3] |= VectorCoverageStrokeColourExplicit;

        // 📝 Array/Inset: one diamond shrinking into 4 concentric rings (242, 189, 136, 83), the
        // "inner and outer weave" from the user's own example.
        VectorAuthoringArraySpecification Inset;
        Inset.Category = VectorAuthoringArrayCategory::Linear;
        Inset.ScaleProfile = VectorAuthoringScaleProfile::Linear;
        Inset.Count = 4;
        Inset.StepX = 0;
        Inset.StepY = 0;
        Inset.StartScale = 100;
        Inset.EndScale = 83.f / 242.f * 100;
        auto Ring = ConstructInsetMotif(Seed, "Weave ring", Inset);
        for (size_t Copy = 1; Copy < Ring.size(); Copy += 2) { Ring[Copy].Colour = Ground; Ring[Copy].ColourEnd = Ground; }

        // 📝 Mirror: reflect the ring column across the tile's vertical midline for the bottom row.
        const auto Rows = MirrorVectorAuthoringMotif(Ring, 'y', 256);

        // 📝 Align-to-edge: place one accent-recoloured copy of the ring column at each edge quarter.
        const std::array<std::array<float, 2>, 2> Anchors{{{128, 0}, {384, 0}}};
        const std::array<std::array<float, 4>, 2> Accents{{{.8f, .4392f, .3569f, 1}, {.8471f, .6941f, .4196f, 1}}};
        const auto Placed = AlignVectorAuthoringMotifToEdges(Rows, Anchors, AccentMarker, Accents);
        for (const auto &Instruction : Placed) ConstructVectorAuthoringPrimitive(Document, Instruction, "Weave ring");
        return Document;
    }
    default: // Pebble Path: a brick-offset grid of small pebbles (Grid array tool) with a handful of
             // larger accent pebbles scattered over it (Scatter placement tool) -- the C++ side of
             // the two HTML tools that didn't yet have a shipped demonstration (gridArray powers
             // Cube lattice, scatterPlacement powers Painted blossoms).
    {
        VectorCoverageInstruction Field;
        Field.Topology[0] = 0;
        Field.Bounds = {256, 256, 512, 512};
        Field.Colour = {.149f, .204f, .239f, 1};
        Field.ColourEnd = Field.Colour;
        Field.StrokeColour = Field.Colour;
        ConstructVectorAuthoringPrimitive(Document, Field, "Pebble ground");

        // 📝 Grid: one pebble motif repeated on a brick-offset lattice (odd rows shift half a step).
        VectorCoverageInstruction Pebble;
        Pebble.Topology[0] = 1;
        Pebble.Bounds = {64, 64, 56, 56};
        Pebble.Colour = {.74f, .71f, .62f, 1};
        Pebble.ColourEnd = Pebble.Colour;
        Pebble.StrokeColour = {.35f, .32f, .27f, 1};
        Pebble.Topology[3] |= VectorCoverageStrokeColourExplicit;
        const auto Bed = ConstructGridMotif(Pebble, 4, 4, 128, 128, [](uint32_t Row) { return (Row % 2) ? 64.f : 0.f; });
        for (const auto &Instruction : Bed) ConstructVectorAuthoringPrimitive(Document, Instruction, "Pebble");

        // 📝 Scatter: a handful of larger accent pebbles at deterministic pseudo-random positions.
        VectorCoverageInstruction Accent;
        Accent.Topology[0] = 1;
        Accent.Bounds = {0, 0, 72, 72};
        Accent.Colour = {.882f, .616f, .369f, 1};
        Accent.ColourEnd = Accent.Colour;
        Accent.StrokeColour = {.35f, .32f, .27f, 1};
        Accent.Topology[3] |= VectorCoverageStrokeColourExplicit;
        for (uint32_t Index = 0; Index < 5; ++Index)
        {
            const auto Position = QueryScatterPlacement(Index, 173, 149, 48, 72);
            VectorCoverageInstruction Placed = Accent;
            Placed.Bounds[0] = Position[0];
            Placed.Bounds[1] = Position[1];
            ConstructVectorAuthoringPrimitive(Document, Placed, "Pebble accent");
        }
        return Document;
    }
    }
}
} // namespace Frontier
