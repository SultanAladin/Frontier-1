/*============================================================================================================================================
                                                         STAMPVECTORIMPORT.CPP
============================================================================================================================================*/
// 🧩 Convert one sanitized SVG document into real cubic-path geometry via ThorVG's software SVG parser.

#include "StampVectorImport.h"
#include <thorvg.h>
#include <limits>
#include <vector>

namespace Frontier
{
namespace
{
using Coordinate = std::array<float, 2>;

//------------------------------------------------------------------------------------------------------------------------
//                                                   AFFINE COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

tvg::Matrix Multiply(const tvg::Matrix &Outer, const tvg::Matrix &Inner)
{
    // 📝 Composes "apply Inner (a shape's own local transform), then Outer (its ancestor chain)",
    // matching SVG's own nested <g transform="..."> semantics instead of ThorVG's per-node storage
    // (each loaded node only carries its OWN transform attribute; the ancestor chain is composed
    // here by walking the Scene tree ourselves instead of relying on the flat Accessor utility).
    tvg::Matrix Result{};
    Result.e11 = Outer.e11 * Inner.e11 + Outer.e12 * Inner.e21;
    Result.e12 = Outer.e11 * Inner.e12 + Outer.e12 * Inner.e22;
    Result.e13 = Outer.e11 * Inner.e13 + Outer.e12 * Inner.e23 + Outer.e13;
    Result.e21 = Outer.e21 * Inner.e11 + Outer.e22 * Inner.e21;
    Result.e22 = Outer.e21 * Inner.e12 + Outer.e22 * Inner.e22;
    Result.e23 = Outer.e21 * Inner.e13 + Outer.e22 * Inner.e23 + Outer.e23;
    Result.e31 = 0; Result.e32 = 0; Result.e33 = 1;
    return Result;
}
Coordinate Apply(const tvg::Matrix &M, tvg::Point P)
{
    return {M.e11 * P.x + M.e12 * P.y + M.e13, M.e21 * P.x + M.e22 * P.y + M.e23};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHAPE COLLECTION
//------------------------------------------------------------------------------------------------------------------------

struct ImportedShape
{
    std::vector<VectorCubicSegment> Segments;
    Coordinate Minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Coordinate Maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    std::array<float, 4> Colour{.6f, .6f, .6f, 1}; // Fallback for gradient/unset fills (v1 scope)
    bool EvenOdd = false;
};

void TrackPoint(ImportedShape &Shape, Coordinate Point)
{
    Shape.Minimum[0] = std::min(Shape.Minimum[0], Point[0]); Shape.Minimum[1] = std::min(Shape.Minimum[1], Point[1]);
    Shape.Maximum[0] = std::max(Shape.Maximum[0], Point[0]); Shape.Maximum[1] = std::max(Shape.Maximum[1], Point[1]);
}

void CollectShape(const tvg::Shape &Source, const tvg::Matrix &Effective, std::vector<ImportedShape> &Shapes)
{
    const tvg::PathCommand *Commands = nullptr;
    const tvg::Point *Points = nullptr;
    uint32_t CommandCount = 0, PointCount = 0;
    if (Source.path(&Commands, &CommandCount, &Points, &PointCount) != tvg::Result::Success || !CommandCount || !PointCount) return;

    // 📝 Source.fill(&r,&g,&b,&a) still returns Success (with meaningless zeroed-out channels) for a
    // shape painted by a gradient Fill*, so that solid-colour readback can only be trusted once the
    // gradient/no-fill case is ruled out via the separate Fill* getter below; otherwise a legitimate
    // gradient shape would misread as fully transparent (Alpha==0) and get silently dropped.
    uint8_t Red = 0, Green = 0, Blue = 0, Alpha = 255;
    const bool Solid = !Source.fill() && Source.fill(&Red, &Green, &Blue, &Alpha) == tvg::Result::Success;
    if (Solid && Alpha == 0) return; // A fully transparent solid shape contributes no visible ink.

    ImportedShape Shape;
    if (Solid) Shape.Colour = {Red / 255.f, Green / 255.f, Blue / 255.f, Alpha / 255.f};
    Shape.EvenOdd = Source.fillRule() == tvg::FillRule::EvenOdd;

    Coordinate Cursor{0, 0}, ContourStart{0, 0};
    bool Open = false;
    uint32_t PointIndex = 0;
    for (uint32_t Index = 0; Index < CommandCount; ++Index)
    {
        switch (Commands[Index])
        {
        case tvg::PathCommand::MoveTo:
        {
            Cursor = ContourStart = Apply(Effective, Points[PointIndex++]);
            TrackPoint(Shape, Cursor);
            Open = true;
            break;
        }
        case tvg::PathCommand::LineTo:
        {
            if (!Open) { ++PointIndex; break; }
            const Coordinate Finish = Apply(Effective, Points[PointIndex++]);
            Shape.Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {Finish[0], Finish[1], Finish[0], Finish[1]}});
            TrackPoint(Shape, Finish);
            Cursor = Finish;
            break;
        }
        case tvg::PathCommand::CubicTo:
        {
            if (!Open) { PointIndex += 3; break; }
            const Coordinate ControlA = Apply(Effective, Points[PointIndex++]);
            const Coordinate ControlB = Apply(Effective, Points[PointIndex++]);
            const Coordinate Finish = Apply(Effective, Points[PointIndex++]);
            Shape.Segments.push_back({{Cursor[0], Cursor[1], ControlA[0], ControlA[1]}, {ControlB[0], ControlB[1], Finish[0], Finish[1]}});
            TrackPoint(Shape, ControlA); TrackPoint(Shape, ControlB); TrackPoint(Shape, Finish);
            Cursor = Finish;
            break;
        }
        case tvg::PathCommand::Close:
        {
            if (Open && (Cursor[0] != ContourStart[0] || Cursor[1] != ContourStart[1]))
                Shape.Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {ContourStart[0], ContourStart[1], ContourStart[0], ContourStart[1]}});
            Cursor = ContourStart;
            break;
        }
        }
    }
    // 📝 An SVG sub-path need not end with an explicit Z; close it anyway so the nonzero/even-odd
    // winding walk (which assumes every contour is a closed loop) sees a valid edge ring.
    if (Open && (Cursor[0] != ContourStart[0] || Cursor[1] != ContourStart[1]))
        Shape.Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {ContourStart[0], ContourStart[1], ContourStart[0], ContourStart[1]}});

    if (!Shape.Segments.empty() && Shape.Minimum[0] <= Shape.Maximum[0] && Shape.Minimum[1] <= Shape.Maximum[1])
        Shapes.push_back(std::move(Shape));
}

void WalkNode(const tvg::Paint &Node, const tvg::Matrix &Parent, std::vector<ImportedShape> &Shapes, uint32_t &Budget)
{
    if (!Budget) return;
    const auto &Local = const_cast<tvg::Paint &>(Node).transform();
    const auto Effective = Multiply(Parent, Local);
    if (Node.type() == tvg::Type::Shape) { CollectShape(static_cast<const tvg::Shape &>(Node), Effective, Shapes); --Budget; }
    else if (Node.type() == tvg::Type::Scene)
        for (const auto *Child : static_cast<const tvg::Scene &>(Node).paints())
        {
            if (!Budget) break;
            WalkNode(*Child, Effective, Shapes, Budget);
        }
}
} // namespace

uint32_t AppendStampVectorImport(VectorAuthoringDocument &Document, std::string_view SvgMarkup)
{
    if (SvgMarkup.empty() || SvgMarkup.size() > StampVectorMarkupLimit) throw std::runtime_error("Stamp vector markup is out of its authored bound.");

    if (tvg::Initializer::init(1) != tvg::Result::Success) throw std::runtime_error("The ThorVG software renderer could not start.");
    struct TerminateGuard { ~TerminateGuard() { tvg::Initializer::term(); } } Terminate;

    auto *Picture = tvg::Picture::gen();
    if (!Picture) throw std::runtime_error("Could not allocate the Stamp vector import picture.");
    // 📝 Paint::~Paint() is protected; a standalone (never canvas-pushed) Paint is torn down with
    // unref(true) instead of delete, matching ThorVG's own C API bindings (tvg_paint_unref).
    struct PictureGuard { tvg::Picture *P; ~PictureGuard() { P->unref(true); } } PictureOwner{Picture};
    if (Picture->load(SvgMarkup.data(), uint32_t(SvgMarkup.size()), "svg") != tvg::Result::Success)
        throw std::runtime_error("The Stamp vector import could not parse that SVG markup.");

    // 📝 Capture the picture's internal scene root via one Accessor step, then walk it ourselves so
    // ancestor <g transform="..."> chains compose correctly (see Multiply's note above); the flat
    // Accessor callback alone has no parent-chain context to do that composition itself.
    auto *Walker = tvg::Accessor::gen();
    if (!Walker) throw std::runtime_error("Could not allocate the Stamp vector import accessor.");
    struct AccessorGuard { tvg::Accessor *A; ~AccessorGuard() { delete A; } } AccessorOwner{Walker};
    const tvg::Paint *Root = nullptr;
    bool Seen = false;
    (void)Walker->set(Picture, [&Root, &Seen](const tvg::Paint *Node, void *) -> bool {
        if (!Seen) { Seen = true; return true; } // Step past the Picture wrapper itself.
        Root = Node;
        return false; // Stop the flat walk; WalkNode() recurses the rest with proper transforms.
    }, nullptr);

    std::vector<ImportedShape> Shapes;
    uint32_t Budget = 256; // A decal's imported artwork is bounded like every other Stamp component.
    if (Root) WalkNode(*Root, tvg::Matrix{1, 0, 0, 0, 1, 0, 0, 0, 1}, Shapes, Budget);
    if (Shapes.empty()) throw std::runtime_error("That SVG resolved to no renderable shape.");

    uint32_t PreviousIndex = UINT32_MAX;
    for (const auto &Shape : Shapes)
    {
        const float Width = std::max(.01f, Shape.Maximum[0] - Shape.Minimum[0]), Height = std::max(.01f, Shape.Maximum[1] - Shape.Minimum[1]);
        const float CentreX = (Shape.Minimum[0] + Shape.Maximum[0]) * .5f, CentreY = (Shape.Minimum[1] + Shape.Maximum[1]) * .5f;
        const auto SegmentOffset = uint32_t(Document.Segments.size());
        for (auto Segment : Shape.Segments)
        {
            Segment.StartControl[0] = (Segment.StartControl[0] - CentreX) / Width; Segment.StartControl[1] = (Segment.StartControl[1] - CentreY) / Height;
            Segment.StartControl[2] = (Segment.StartControl[2] - CentreX) / Width; Segment.StartControl[3] = (Segment.StartControl[3] - CentreY) / Height;
            Segment.ControlEnd[0] = (Segment.ControlEnd[0] - CentreX) / Width; Segment.ControlEnd[1] = (Segment.ControlEnd[1] - CentreY) / Height;
            Segment.ControlEnd[2] = (Segment.ControlEnd[2] - CentreX) / Width; Segment.ControlEnd[3] = (Segment.ControlEnd[3] - CentreY) / Height;
            Document.Segments.push_back(Segment);
        }
        VectorCoverageInstruction Instruction;
        Instruction.Topology = {6, SegmentOffset, uint32_t(Shape.Segments.size()), Shape.EvenOdd ? 1u : 0u};
        Instruction.Bounds = {CentreX, CentreY, Width, Height};
        Instruction.Transform = {0, 0, 1, 0};
        Instruction.Colour = Instruction.ColourEnd = Instruction.StrokeColour = Shape.Colour;
        Instruction.Paint = {0, .45f, 6, 1};
        const auto ShapeIndex = uint32_t(Document.Instructions.size());
        Document.Instructions.push_back(Instruction);

        if (PreviousIndex == UINT32_MAX) PreviousIndex = ShapeIndex;
        else
        {
            VectorCoverageInstruction Over;
            Over.Topology = {28, PreviousIndex, ShapeIndex, 0}; // 28 = Over: keeps each shape's own colour
            PreviousIndex = uint32_t(Document.Instructions.size());
            Document.Instructions.push_back(Over);
        }
    }
    return PreviousIndex;
}
}
