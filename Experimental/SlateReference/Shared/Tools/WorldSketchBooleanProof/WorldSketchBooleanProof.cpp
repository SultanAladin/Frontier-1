// 🧩 Proof for two-dimensional world-sketch booleans: union, cut, intersect, and an open-curve split.
//
// 🔴 THE THINGS THE ARTIST ASKED FOR, EACH MADE A CLAIM. Two overlapping shapes union into one region;
//    a bite taken out of a side; a circle inside a rectangle becoming a hole; an intersection keeping only
//    the shared area; an open curve cutting a shape into two closed pieces; and a curve that misses being
//    refused rather than silently doing nothing.

#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateShape/World/WorldSketchBoolean/Api/WorldSketchBoolean.h"

#include <cmath>
#include <cstdio>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Require(bool Held, const char* Claim)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Claim);
    }
}

const WorldPlacementFrame Ground = { { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } };

// 🧩 Declares an axis-aligned rectangle on the ground plane and returns its loop.
WorldLoopName DeclareRectangle(WorldSketchStructure& Sketch,
                               double MinX, double MinZ, double MaxX, double MaxZ)
{
    const WorldCurveName AB = Sketch.DeclareLine({ MinX, 0.0, MinZ }, { MaxX, 0.0, MinZ }, Ground);
    const WorldCurveName BC = Sketch.DeclareLine({ MaxX, 0.0, MinZ }, { MaxX, 0.0, MaxZ }, Ground);
    const WorldCurveName CD = Sketch.DeclareLine({ MaxX, 0.0, MaxZ }, { MinX, 0.0, MaxZ }, Ground);
    const WorldCurveName DA = Sketch.DeclareLine({ MinX, 0.0, MaxZ }, { MinX, 0.0, MinZ }, Ground);
    return Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });
}

// 🧩 Declares a circle as a many-sided polygon loop on the ground plane.
WorldLoopName DeclareCirclePolygon(WorldSketchStructure& Sketch,
                                   double CentreX, double CentreZ, double Radius,
                                   std::uint32_t Sides = 48u)
{
    std::vector<WorldCurveName> Edges;
    std::vector<SpatialPoint> Points;
    for (std::uint32_t Index = 0u; Index < Sides; ++Index)
    {
        const double Angle = 2.0 * 3.14159265358979323846 * static_cast<double>(Index) / static_cast<double>(Sides);
        Points.push_back({ CentreX + Radius * std::cos(Angle), 0.0, CentreZ + Radius * std::sin(Angle) });
    }
    std::vector<WorldCurveUse> Traversal;
    for (std::uint32_t Index = 0u; Index < Sides; ++Index)
    {
        const SpatialPoint& Start = Points[Index];
        const SpatialPoint& End   = Points[(Index + 1u) % Sides];
        Traversal.push_back({ Sketch.DeclareLine(Start, End, Ground), true });
    }
    return Sketch.DeclareLoop({ Traversal });
}

const WorldLoopAnalysisRecord* ResolveLoop(const WorldSketchAnalysis& Analysis, WorldLoopName Name)
{
    for (const WorldLoopAnalysisRecord& Loop : Analysis.Loops)
        if (Loop.Loop.IssuedIndex == Name.IssuedIndex)
            return &Loop;
    return nullptr;
}

double SignedOutlineArea(const WorldPlacementFrame& Frame, const std::vector<SpatialPoint>& Outline)
{
    if (Outline.size() < 3u)
        return 0.0;
    double Sum = 0.0;
    for (std::size_t Index = 0u; Index < Outline.size(); ++Index)
    {
        const std::size_t Next = (Index + 1u) % Outline.size();
        double AlongA = 0.0, AcrossA = 0.0, AlongB = 0.0, AcrossB = 0.0;
        ResolveWorldPlacementCoordinates(Frame, Outline[Index], AlongA, AcrossA);
        ResolveWorldPlacementCoordinates(Frame, Outline[Next],  AlongB, AcrossB);
        Sum += AlongA * AcrossB - AlongB * AcrossA;
    }
    return Sum * 0.5;
}

// 🧩 The net material area of the loops a boolean produced, measured by each loop's OWN winding.
// 🔴 NOT VIA GLOBAL NESTING. The operands are kept, so the result overlaps them on the same plane and the
//    global hole/nesting analysis -- which is even-odd across every loop in the sketch -- no longer reads
//    the produced loops in isolation. What a boolean produced is judged by the geometry it produced: a
//    hole ring is declared wound opposite its outer, so summing SIGNED areas subtracts holes for free and
//    ignores whatever the unrelated originals do.
double ProducedMaterialArea(const WorldSketchStructure& Sketch,
                            const std::vector<WorldLoopName>& Produced)
{
    const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch, 64u);
    double Total = 0.0;
    for (const WorldLoopName& Name : Produced)
    {
        const WorldLoopAnalysisRecord* Record = ResolveLoop(Analysis, Name);
        if (Record == nullptr)
            continue;
        Total += std::fabs(SignedOutlineArea(Record->SupportFrame, Record->Outline));
    }
    return Total;
}

// 🧩 The net material area (outer areas minus hole areas) of a produced set, by signed winding.
double ProducedNetArea(const WorldSketchStructure& Sketch,
                       const std::vector<WorldLoopName>& Produced)
{
    // 🔴 SIGN BY THE NESTING FLAG, NOT BY RAW WINDING. Each produced loop is re-declared with its own
    //    support frame, and a loop's signed area in its own frame is always the same sign regardless of
    //    whether it is an outer or a hole -- the frames differ, so the raw signs are not comparable across
    //    loops. The analysis already reads holes back by even-odd nesting (the washer-structure claim
    //    proves it), so trust that: an outer adds its area, a hole subtracts it.
    const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch, 64u);
    double Net = 0.0;
    for (const WorldLoopName& Name : Produced)
    {
        const WorldLoopAnalysisRecord* Record = ResolveLoop(Analysis, Name);
        if (Record == nullptr)
            continue;
        const double Magnitude = std::fabs(SignedOutlineArea(Record->SupportFrame, Record->Outline));
        Net += Record->Hole ? -Magnitude : Magnitude;
    }
    return std::fabs(Net);
}

bool Near(double Value, double Target, double Tolerance)
{
    return std::fabs(Value - Target) <= Tolerance;
}

} // namespace

int main()
{
    std::printf("[WorldSketchBooleanProof] two-dimensional booleans over world-sketch regions\n");

    if (!WorldBooleanBackendAvailable())
    {
        std::printf("  the robust area backend is not built in; booleans refuse rather than guess\n");
        WorldSketchStructure Sketch;
        const WorldLoopName A = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);
        const WorldLoopName B = DeclareRectangle(Sketch, 50.0, 50.0, 150.0, 150.0);
        const Deliver<std::vector<WorldLoopName>> Result =
            PerformWorldBoolean(Sketch, { A, {} }, { B, {} }, WorldBooleanManner::Union);
        Require(!Result.Resolved, "with no backend a boolean refuses rather than returning a bad result");
        std::printf("[WorldSketchBooleanProof] %u claims, %u failures\n", Claims, Failures);
        return Failures == 0u ? 0 : 1;
    }

    //----------------------------------------------------------------------------------------------------
    // 1. UNION of two overlapping rectangles is one welded region, its area the inclusion-exclusion sum.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName A = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);   // 10000
        const WorldLoopName B = DeclareRectangle(Sketch, 50.0, 50.0, 150.0, 150.0); // 10000, overlap 50x50=2500

        Require(EvaluateWorldBoolean(Sketch, { A, {} }, { B, {} }, WorldBooleanManner::Union)
                    == WorldBooleanVerdict::Produced,
                "a union of two overlapping shapes previews as producible");

        const Deliver<std::vector<WorldLoopName>> Result =
            PerformWorldBoolean(Sketch, { A, {} }, { B, {} }, WorldBooleanManner::Union);
        Require(Result.Resolved, "and it performs");
        if (Result.Resolved)
        {
            Require(Result.Delivered.size() == 1u, "welding two overlapping rectangles leaves one region");
            const double Area = ProducedMaterialArea(Sketch, Result.Delivered);
            Require(Near(Area, 10000.0 + 10000.0 - 2500.0, 5.0),
                    "whose area is A + B - overlap = 17500");
        }
    }

    //----------------------------------------------------------------------------------------------------
    // 2. CUT: a bite taken out of a rectangle's side by an overlapping rectangle.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName A = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);    // 10000
        const WorldLoopName B = DeclareRectangle(Sketch, 80.0, 40.0, 140.0, 60.0);   // overlap 20x20=400

        const Deliver<std::vector<WorldLoopName>> Result =
            PerformWorldBoolean(Sketch, { A, {} }, { B, {} }, WorldBooleanManner::Cut);
        Require(Result.Resolved, "a cut of one shape by an overlapping shape performs");
        if (Result.Resolved)
        {
            const double Area = ProducedMaterialArea(Sketch, Result.Delivered);
            Require(Near(Area, 10000.0 - 400.0, 5.0), "and removes exactly the overlapping area (9600)");
        }
    }

    //----------------------------------------------------------------------------------------------------
    // 3. CUT with a fully contained circle becomes a HOLE -- the tube case, winding handled.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName Rect = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);
        const WorldLoopName Hole = DeclareCirclePolygon(Sketch, 50.0, 50.0, 20.0);

        const Deliver<std::vector<WorldLoopName>> Result =
            PerformWorldBoolean(Sketch, { Rect, {} }, { Hole, {} }, WorldBooleanManner::Cut);
        Require(Result.Resolved, "cutting a rectangle by a circle inside it performs");
        if (Result.Resolved)
        {
            const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch, 64u);
            std::uint32_t Outers = 0u;
            std::uint32_t Holes = 0u;
            for (const WorldLoopName& Name : Result.Delivered)
            {
                const WorldLoopAnalysisRecord* Record = ResolveLoop(Analysis, Name);
                if (Record == nullptr)
                    continue;
                if (Record->Hole)
                    ++Holes;
                else
                    ++Outers;
            }
            Require(Outers == 1u && Holes == 1u,
                    "the result is one outer region with one hole -- a washer, not a filled disc");

            const double Area = ProducedNetArea(Sketch, Result.Delivered);
            Require(Near(Area, 10000.0 - 3.14159265 * 400.0, 40.0),
                    "and its material area is the rectangle less the circle");
        }
    }

    //----------------------------------------------------------------------------------------------------
    // 4. INTERSECT keeps only the shared area of two overlapping rectangles.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName A = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);
        const WorldLoopName B = DeclareRectangle(Sketch, 50.0, 50.0, 150.0, 150.0);

        const Deliver<std::vector<WorldLoopName>> Result =
            PerformWorldBoolean(Sketch, { A, {} }, { B, {} }, WorldBooleanManner::Intersect);
        Require(Result.Resolved, "an intersection of two overlapping shapes performs");
        if (Result.Resolved)
        {
            Require(Result.Delivered.size() == 1u, "leaving one region");
            const double Area = ProducedMaterialArea(Sketch, Result.Delivered);
            Require(Near(Area, 2500.0, 5.0), "whose area is the 50x50 overlap");
        }
    }

    //----------------------------------------------------------------------------------------------------
    // 5. AN OPEN CURVE THAT CROSSES A SHAPE SPLITS IT INTO TWO CLOSED PIECES.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName Rect = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);
        // 🔴 THE CUTTER MUST BE INCOMPLETE, and cross the shape end to end: a line running from left of
        //    the rectangle to right of it, through the middle.
        const WorldCurveName Knife = Sketch.DeclareLine({ -20.0, 0.0, 50.0 }, { 120.0, 0.0, 50.0 }, Ground);

        Require(EvaluateWorldBoolean(Sketch, { Rect, {} }, { {}, Knife }, WorldBooleanManner::Cut)
                    == WorldBooleanVerdict::Produced,
                "an open curve crossing the shape previews as a producible cut");

        const Deliver<std::vector<WorldLoopName>> Result =
            PerformWorldBoolean(Sketch, { Rect, {} }, { {}, Knife }, WorldBooleanManner::Cut);
        Require(Result.Resolved, "and it performs");
        if (Result.Resolved)
        {
            Require(Result.Delivered.size() == 2u, "splitting the shape into TWO closed pieces");
            const double Area = ProducedMaterialArea(Sketch, Result.Delivered);
            Require(Near(Area, 10000.0, 20.0),
                    "which together still cover the original area (only a hairline is lost to the cut)");
        }
    }

    //----------------------------------------------------------------------------------------------------
    // 6. A CURVE THAT MISSES THE SHAPE SPLITS NOTHING, and is refused rather than doing nothing.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName Rect = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);
        const WorldCurveName Miss = Sketch.DeclareLine({ 0.0, 0.0, 200.0 }, { 100.0, 0.0, 200.0 }, Ground);

        Require(EvaluateWorldBoolean(Sketch, { Rect, {} }, { {}, Miss }, WorldBooleanManner::Cut)
                    == WorldBooleanVerdict::CutterNotCrossing,
                "a cutter that never touches the shape is refused, not silently ignored");
    }

    //----------------------------------------------------------------------------------------------------
    // 7. FEWER THAN TWO OPERANDS IS REFUSED.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName A = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);
        Require(EvaluateWorldBoolean(Sketch, { A, {} }, { {}, {} }, WorldBooleanManner::Union)
                    == WorldBooleanVerdict::OperandMissing,
                "a boolean with only one operand is refused");
    }

    //----------------------------------------------------------------------------------------------------
    // 8. THE ORIGINALS ARE KEPT. A boolean is non-destructive; its inputs still stand afterwards.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldLoopName A = DeclareRectangle(Sketch, 0.0, 0.0, 100.0, 100.0);
        const WorldLoopName B = DeclareRectangle(Sketch, 50.0, 50.0, 150.0, 150.0);
        const std::uint32_t CurvesBefore = Sketch.CurveCount();
        const std::uint32_t LoopsBefore  = Sketch.LoopCount();

        const Deliver<std::vector<WorldLoopName>> Result =
            PerformWorldBoolean(Sketch, { A, {} }, { B, {} }, WorldBooleanManner::Union);
        Require(Result.Resolved, "the union performs");
        Require(Sketch.CurveCount() > CurvesBefore && Sketch.LoopCount() > LoopsBefore,
                "the result adds geometry");
        // The original loops keep their indices and remain resolvable.
        Require(Sketch.Resolve(A) != nullptr && Sketch.Resolve(B) != nullptr,
                "and the two original shapes are kept, undisturbed");
    }

    std::printf("[WorldSketchBooleanProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
