//============================================================================================================================================
//                                                       WORLDSKETCHBOOLEAN.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchBoolean/Api/WorldSketchBoolean.h"

#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

// 🔴 CLIPPER2 IS THE ROBUST BACKEND, AND ITS ABSENCE IS AN HONEST REFUSAL, NOT A GUESS. Every actually
//    overlapping boolean -- two rectangles that cross, a bite out of a side, a shape split down the middle
//    -- needs a real planar-area engine; the hand-rolled convex clip the compatibility unit carried could
//    not do them and pretended the cases did not exist. When the header is not vendored the unit still
//    builds, and each boolean returns `BackendAbsent` rather than a result no one should trust.
#if __has_include("clipper2/clipper.h")
    #include "clipper2/clipper.h"
    #define SLATE_WORLD_BOOLEAN_HAS_CLIPPER2 1
#else
    #define SLATE_WORLD_BOOLEAN_HAS_CLIPPER2 0
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Slate
{

namespace
{

// 📐 How finely a curved edge is sampled before the area work. Matches the fill preview and the area
//    analysis, so a boolean sees the same shape the artist sees shaded.
constexpr std::uint32_t BooleanStepFloor = 64u;

// 📐 Two planar points are the same within this, in world units. Clipper works in scaled integers, so a
//    result vertex that lands within this of an operand vertex is that vertex.
constexpr double BooleanWeldTolerance = 1.0e-4;

// 📐 How far an open cutter may miss the shape's plane and still count as being on it.
constexpr double BooleanPlaneTolerance = 1.0e-3;

// 📐 The half-width of the sliver an open cutter is thickened to before it is subtracted. It must be wide
//    enough to survive Clipper2's coordinate rounding (a hundredth of a unit at the default precision) yet
//    fine enough that the gap it opens between the two pieces reads as a cut line rather than a channel.
constexpr double BooleanKnifeHalfWidth = 0.05;

//------------------------------------------------------------------------------------------------------------------------
//                                              A SHAPE, FLATTENED INTO ITS PLANE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One closed ring of a shape, in the plane's own along/across coordinates.
struct FlatRing
{
    std::vector<PlanarPoint> Points = {};
    bool Hole = false;
};

/// 🧩 A whole shape -- an outer loop and any loops that are holes in it -- flattened into one plane.
struct FlatShape
{
    WorldPlacementFrame Frame = {};
    std::vector<FlatRing> Rings = {};
    bool Standing = false;
};

double SignedArea(const std::vector<PlanarPoint>& Ring)
{
    if (Ring.size() < 3u)
        return 0.0;
    double Sum = 0.0;
    for (std::size_t Index = 0u; Index < Ring.size(); ++Index)
    {
        const PlanarPoint& Current = Ring[Index];
        const PlanarPoint& Next = Ring[(Index + 1u) % Ring.size()];
        Sum += Current.Along * Next.Across - Next.Along * Current.Across;
    }
    return Sum * 0.5;
}

PlanarPoint Flatten(const WorldPlacementFrame& Frame, const SpatialPoint& Position)
{
    double Along = 0.0;
    double Across = 0.0;
    ResolveWorldPlacementCoordinates(Frame, Position, Along, Across);
    return { Along, Across };
}

SpatialPoint Lift(const WorldPlacementFrame& Frame, const PlanarPoint& Position)
{
    return ResolveWorldPlacementPosition(Frame, Position.Along, Position.Across);
}

bool SamePlane(const WorldPlacementFrame& Left, const WorldPlacementFrame& Right)
{
    const SpatialDirection LeftNormal  = Normalize(Left.Normal);
    const SpatialDirection RightNormal = Normalize(Right.Normal);
    if (std::fabs(std::fabs(Dot(LeftNormal, RightNormal)) - 1.0) > 1.0e-6)
        return false;
    const SpatialDirection Offset = Difference(Left.Origin, Right.Origin);
    return std::fabs(Dot(LeftNormal, Offset)) <= BooleanPlaneTolerance;
}

/// 🧩 Resolves a single closed loop into one outer ring in its own plane.
/// 🔴 ONE OPERAND IS ONE LOOP, AND ITS HOLES ARE THE BOOLEAN'S OWN OUTPUT, NOT A GLOBAL SEARCH. An
///    earlier attempt gathered "the holes inside this loop" from the sketch-wide nesting analysis, and
///    that analysis is even-odd across EVERY loop in the sketch: when the two operands overlap -- which
///    is the whole point of a boolean -- a vertex of the other operand lands inside this one, the nesting
///    pass reads the other shape as a hole in this one, and the boolean is handed a shape it never had.
///    The artist selects one loop; that loop's outline is its outer ring, full stop. A hole in the RESULT
///    (a circle cut from a rectangle) is produced by the difference itself and read back by winding, so
///    nothing here needs a fragile containment guess. A loop's own `Outline` and `SupportFrame` are
///    computed before the nesting pass and so are safe to take from the analysis record.
/// 🔴 BOTH OPERANDS MUST BE FLATTENED IN ONE SHARED FRAME. Every loop's analysis record carries its own
///    `SupportFrame`, whose origin sits at that loop's own first vertex. Flattening each operand in its
///    OWN frame silently translates both to the same local origin -- two rectangles a hundred units apart
///    both come out as (0,0)..(100,100), perfectly coincident -- so a union collapses to a single square
///    and an intersect returns the whole thing. The first operand fixes the common frame; the second is
///    flattened in that same frame so the two shapes keep their real relative position on the plane.
bool ResolveFlatShape(const WorldSketchStructure& Declared,
                      const WorldSketchAnalysis& Analysis,
                      WorldLoopName Subject,
                      FlatShape& Resolved,
                      const WorldPlacementFrame* SharedFrame = nullptr)
{
    const WorldLoopAnalysisRecord* Outer = nullptr;
    for (const WorldLoopAnalysisRecord& Record : Analysis.Loops)
        if (Record.Loop.IssuedIndex == Subject.IssuedIndex)
        {
            Outer = &Record;
            break;
        }

    if (Outer == nullptr || !Outer->Closed || Outer->Outline.size() < 3u
        || !Outer->SupportFrame.Declared())
        return false;

    Resolved = {};
    Resolved.Frame = (SharedFrame != nullptr) ? *SharedFrame : Outer->SupportFrame;
    Resolved.Standing = true;

    FlatRing OuterRing;
    OuterRing.Hole = false;
    for (const SpatialPoint& Point : Outer->Outline)
        OuterRing.Points.push_back(Flatten(Resolved.Frame, Point));
    Resolved.Rings.push_back(OuterRing);

    static_cast<void>(Declared);
    return true;
}

/// 🧩 The world curve behind an operand that names a single open curve, if it resolves to one.
const DeclaredWorldCurve* ResolveOpenCurve(const WorldSketchStructure& Declared, WorldCurveName Subject)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || Held->Retired || !Held->Geometry.Declared())
        return nullptr;
    return Held;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THE CLIPPER2 SEAM
//------------------------------------------------------------------------------------------------------------------------

#if SLATE_WORLD_BOOLEAN_HAS_CLIPPER2

Clipper2Lib::PathsD ShapeToPaths(const FlatShape& Shape)
{
    // 🔴 THE OUTER RING WOUND ONE WAY, THE HOLES THE OTHER. Clipper's NonZero rule reads a ring's winding
    //    to decide material from void, so a hole must run opposite its outer loop or it fills the shape
    //    back in. The analysis does not promise a consistent winding, so it is enforced here: the outer
    //    ring is made positive and every hole negative before the area work begins.
    Clipper2Lib::PathsD Paths;
    for (const FlatRing& Ring : Shape.Rings)
    {
        if (Ring.Points.size() < 3u)
            continue;
        std::vector<PlanarPoint> Points = Ring.Points;
        const double Area = SignedArea(Points);
        const bool WantPositive = !Ring.Hole;
        if ((Area >= 0.0) != WantPositive)
            std::reverse(Points.begin(), Points.end());

        Clipper2Lib::PathD Path;
        Path.reserve(Points.size());
        for (const PlanarPoint& Point : Points)
            Path.push_back({ Point.Along, Point.Across });
        Paths.push_back(Path);
    }
    return Paths;
}

/// 🧩 Turns Clipper's output paths back into flat rings, holes marked by their winding.
std::vector<FlatRing> PathsToRings(const Clipper2Lib::PathsD& Paths)
{
    std::vector<FlatRing> Rings;
    for (const Clipper2Lib::PathD& Path : Paths)
    {
        if (Path.size() < 3u)
            continue;
        FlatRing Ring;
        for (const Clipper2Lib::PointD& Point : Path)
            Ring.Points.push_back({ Point.x, Point.y });
        // 🔴 A NEGATIVE RING IS A HOLE. Clipper writes an outer contour positive and a hole negative
        //    under the NonZero/Positive rules, so the sign of the area is the answer to "is this a hole".
        Ring.Hole = SignedArea(Ring.Points) < 0.0;
        Rings.push_back(Ring);
    }
    return Rings;
}

#endif

//------------------------------------------------------------------------------------------------------------------------
//                                              DECLARING THE RESULT AS GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Removes a run of points that repeat, and drops a closing duplicate, so an edge is not zero length.
void WeldRing(std::vector<PlanarPoint>& Points)
{
    std::vector<PlanarPoint> Welded;
    for (const PlanarPoint& Point : Points)
    {
        if (!Welded.empty())
        {
            const double DAlong  = Point.Along  - Welded.back().Along;
            const double DAcross = Point.Across - Welded.back().Across;
            if (DAlong * DAlong + DAcross * DAcross <= BooleanWeldTolerance * BooleanWeldTolerance)
                continue;
        }
        Welded.push_back(Point);
    }
    if (Welded.size() >= 2u)
    {
        const double DAlong  = Welded.front().Along  - Welded.back().Along;
        const double DAcross = Welded.front().Across - Welded.back().Across;
        if (DAlong * DAlong + DAcross * DAcross <= BooleanWeldTolerance * BooleanWeldTolerance)
            Welded.pop_back();
    }
    Points.swap(Welded);
}

/// 🧩 Declares one closed ring as a run of world lines and a loop that traverses them.
/// 📝 A boolean result is a polygon, so its edges are straight; a curved operand contributes the polygon
///    that approximated it, which is the same geometry the fill and area already worked from.
WorldLoopName DeclareRingLoop(WorldSketchStructure& Declared,
                              const WorldPlacementFrame& Frame,
                              const std::vector<PlanarPoint>& RingPoints)
{
    std::vector<PlanarPoint> Points = RingPoints;
    WeldRing(Points);
    if (Points.size() < 3u)
        return {};

    std::vector<WorldCurveUse> Traversal;
    Traversal.reserve(Points.size());
    for (std::size_t Index = 0u; Index < Points.size(); ++Index)
    {
        const PlanarPoint& Start = Points[Index];
        const PlanarPoint& End   = Points[(Index + 1u) % Points.size()];
        const WorldCurveName Edge = Declared.DeclareLine(Lift(Frame, Start), Lift(Frame, End), Frame);
        if (!Edge.Assigned())
            return {};
        Traversal.push_back({ Edge, true });
    }

    DeclaredWorldLoop Loop;
    Loop.Traversal = Traversal;
    Loop.FillWanted = true;
    return Declared.DeclareLoop(Loop);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              OPEN-CURVE CUT: SPLIT A SHAPE IN TWO
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether an open curve genuinely crosses a shape -- enters it and leaves -- so it splits it in two.
/// 🔴 A CUTTER THAT DOES NOT CROSS SPLITS NOTHING, and must be refused rather than declared to have done
///    nothing. The spec is explicit that the curve "must be incomplete": an open curve whose two ends lie
///    on opposite sides of the shape's boundary, passing through the interior, is the one that divides it.
bool OpenCutterCrosses(const std::vector<PlanarPoint>& Cutter,
                       const std::vector<PlanarPoint>& Outer)
{
    if (Cutter.size() < 2u || Outer.size() < 3u)
        return false;

    std::uint32_t Crossings = 0u;
    for (std::size_t Index = 0u; Index + 1u < Cutter.size(); ++Index)
    {
        const PlanarPoint& P0 = Cutter[Index];
        const PlanarPoint& P1 = Cutter[Index + 1u];
        for (std::size_t Edge = 0u; Edge < Outer.size(); ++Edge)
        {
            const PlanarPoint& Q0 = Outer[Edge];
            const PlanarPoint& Q1 = Outer[(Edge + 1u) % Outer.size()];

            const double D1 = (P1.Along - P0.Along) * (Q0.Across - P0.Across)
                            - (P1.Across - P0.Across) * (Q0.Along - P0.Along);
            const double D2 = (P1.Along - P0.Along) * (Q1.Across - P0.Across)
                            - (P1.Across - P0.Across) * (Q1.Along - P0.Along);
            const double D3 = (Q1.Along - Q0.Along) * (P0.Across - Q0.Across)
                            - (Q1.Across - Q0.Across) * (P0.Along - Q0.Along);
            const double D4 = (Q1.Along - Q0.Along) * (P1.Across - Q0.Across)
                            - (Q1.Across - Q0.Across) * (P1.Along - Q0.Along);

            if (((D1 > 0.0) != (D2 > 0.0)) && ((D3 > 0.0) != (D4 > 0.0)))
                ++Crossings;
        }
    }

    // 📝 An even number of boundary crossings, two or more, is a curve that goes in and comes back out --
    //    the divide. One crossing is a curve that starts inside and ends outside, which trims rather than
    //    splits, and zero is a miss.
    return Crossings >= 2u;
}

/// 🧩 The cutter sampled and flattened into the shape's plane, if it lies in that plane.
bool ResolveFlatCutter(const WorldSketchStructure& Declared,
                       WorldCurveName Subject,
                       const WorldPlacementFrame& Frame,
                       std::vector<PlanarPoint>& Resolved)
{
    const DeclaredWorldCurve* Held = ResolveOpenCurve(Declared, Subject);
    if (Held == nullptr)
        return false;

    std::vector<SpatialPoint> Sampled;
    AppendCurvePolyline(Held->Geometry, Sampled, BooleanStepFloor);
    if (Sampled.size() < 2u)
        return false;

    Resolved.clear();
    for (const SpatialPoint& Point : Sampled)
    {
        if (std::fabs(ResolveWorldPlacementOffset(Frame, Point)) > BooleanPlaneTolerance)
            return false;
        Resolved.push_back(Flatten(Frame, Point));
    }
    return Resolved.size() >= 2u;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE WORK
//------------------------------------------------------------------------------------------------------------------------

bool WorldBooleanBackendAvailable()
{
    return SLATE_WORLD_BOOLEAN_HAS_CLIPPER2 != 0;
}

namespace
{

/// 🧩 Classifies a pair of operands for a manner without changing anything. Shared by evaluate and perform.
WorldBooleanVerdict ClassifyBoolean(const WorldSketchStructure& Declared,
                                    const WorldSketchAnalysis& Analysis,
                                    const WorldBooleanOperand& First,
                                    const WorldBooleanOperand& Second,
                                    WorldBooleanManner Manner,
                                    FlatShape& FirstShape,
                                    FlatShape& SecondShape,
                                    std::vector<PlanarPoint>& Cutter,
                                    bool& OpenCut)
{
    OpenCut = false;

    if (!First.Declared() || !Second.Declared())
        return WorldBooleanVerdict::OperandMissing;

    // 🔴 THE FIRST OPERAND IS ALWAYS A SHAPE. Union and Intersect are symmetric, but Cut keeps the first
    //    and removes the second, so the first must be a closed region whichever manner runs. An open curve
    //    can only ever be the second operand of a Cut.
    if (!First.NamesLoop())
        return WorldBooleanVerdict::SubjectNotClosed;
    if (!ResolveFlatShape(Declared, Analysis, First.Loop, FirstShape))
        return WorldBooleanVerdict::SubjectNotClosed;

    // 📝 CUT BY AN OPEN CURVE IS ITS OWN BRANCH. When the second operand is a lone curve the manner must
    //    be Cut -- a curve has no area to union or intersect -- and the curve must cross the shape.
    if (Second.NamesCurve())
    {
        if (Manner != WorldBooleanManner::Cut)
            return WorldBooleanVerdict::SubjectNotClosed;
        if (!ResolveFlatCutter(Declared, Second.Curve, FirstShape.Frame, Cutter))
            return WorldBooleanVerdict::DifferentPlane;
        if (!OpenCutterCrosses(Cutter, FirstShape.Rings.front().Points))
            return WorldBooleanVerdict::CutterNotCrossing;
        OpenCut = true;
        return WorldBooleanVerdict::Produced;
    }

    // 🔴 Flatten the second operand in the FIRST operand's frame, so both share one coordinate system and
    //    keep their true relative position (see ResolveFlatShape). Verify coplanarity against the second
    //    operand's own natural support frame BEFORE it is re-expressed in the shared frame.
    FlatShape SecondNatural;
    if (!ResolveFlatShape(Declared, Analysis, Second.Loop, SecondNatural))
        return WorldBooleanVerdict::SubjectNotClosed;
    if (!SamePlane(FirstShape.Frame, SecondNatural.Frame))
        return WorldBooleanVerdict::DifferentPlane;
    if (!ResolveFlatShape(Declared, Analysis, Second.Loop, SecondShape, &FirstShape.Frame))
        return WorldBooleanVerdict::SubjectNotClosed;

    static_cast<void>(Manner);
    return WorldBooleanVerdict::Produced;
}

} // namespace

WorldBooleanVerdict EvaluateWorldBoolean(const WorldSketchStructure& Declared,
                                         const WorldBooleanOperand& First,
                                         const WorldBooleanOperand& Second,
                                         WorldBooleanManner Manner)
{
    if (!WorldBooleanBackendAvailable())
    {
        // 📝 Still classify the obviously invalid pairs, so the refusal the artist sees names the real
        //    problem when there is one, and only falls back to "no backend" for a pair that would work.
        if (!First.Declared() || !Second.Declared())
            return WorldBooleanVerdict::OperandMissing;
        return WorldBooleanVerdict::BackendAbsent;
    }

    const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Declared, BooleanStepFloor);
    FlatShape FirstShape;
    FlatShape SecondShape;
    std::vector<PlanarPoint> Cutter;
    bool OpenCut = false;
    return ClassifyBoolean(Declared, Analysis, First, Second, Manner,
                           FirstShape, SecondShape, Cutter, OpenCut);
}

Deliver<std::vector<WorldLoopName>> PerformWorldBoolean(WorldSketchStructure& Declared,
                                                        const WorldBooleanOperand& First,
                                                        const WorldBooleanOperand& Second,
                                                        WorldBooleanManner Manner)
{
    using Refusal = Deliver<std::vector<WorldLoopName>>;

    if (!WorldBooleanBackendAvailable())
        return Refusal::Refuse({ RefusalReason::ContentUnsupported,
                                 "the robust planar-area backend is not built in" });

    const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Declared, BooleanStepFloor);
    FlatShape FirstShape;
    FlatShape SecondShape;
    std::vector<PlanarPoint> Cutter;
    bool OpenCut = false;
    const WorldBooleanVerdict Classified =
        ClassifyBoolean(Declared, Analysis, First, Second, Manner,
                        FirstShape, SecondShape, Cutter, OpenCut);
    if (Classified != WorldBooleanVerdict::Produced)
        return Refusal::Refuse({ RefusalReason::ContentUnsupported,
                                 "the boolean operands do not describe a valid operation" });

#if SLATE_WORLD_BOOLEAN_HAS_CLIPPER2
    using namespace Clipper2Lib;

    std::vector<WorldLoopName> Produced;

    // ① CUT BY AN OPEN CURVE: split the shape into the pieces on either side of the curve.
    // 🔴 THE CURVE IS THICKENED TO A KNIFE, then subtracted. A zero-width line removes no area, so the two
    //    halves would still touch along it and read as one region. Inflating the open cutter to a sliver
    //    and differencing it opens a hairline gap exactly on the cut -- the two pieces the artist wanted,
    //    each a closed region in its own right.
    if (OpenCut)
    {
        PathsD Subject = ShapeToPaths(FirstShape);
        PathD KnifePath;
        for (const PlanarPoint& Point : Cutter)
            KnifePath.push_back({ Point.Along, Point.Across });
        PathsD Knife;
        Knife.push_back(KnifePath);
        const PathsD Sliver = InflatePaths(Knife, BooleanKnifeHalfWidth,
                                           JoinType::Miter, EndType::Square);
        const PathsD Halves = Difference(Subject, Sliver, FillRule::NonZero);

        for (const FlatRing& Ring : PathsToRings(Halves))
        {
            if (Ring.Hole)
                continue;
            const WorldLoopName Loop = DeclareRingLoop(Declared, FirstShape.Frame, Ring.Points);
            if (Loop.Assigned())
                Produced.push_back(Loop);
        }

        if (Produced.empty())
            return Refusal::Refuse({ RefusalReason::ContentUnsupported,
                                     "the cut left no region" });
        return Refusal::Result(Produced);
    }

    // ② AREA BOOLEANS between two closed shapes.
    const PathsD SubjectPaths = ShapeToPaths(FirstShape);
    const PathsD ClipPaths    = ShapeToPaths(SecondShape);

    PathsD Result;
    switch (Manner)
    {
        case WorldBooleanManner::Union:
        {
            PathsD Both = SubjectPaths;
            Both.insert(Both.end(), ClipPaths.begin(), ClipPaths.end());
            Result = Union(Both, FillRule::NonZero);
            break;
        }
        case WorldBooleanManner::Cut:
            Result = Difference(SubjectPaths, ClipPaths, FillRule::NonZero);
            break;
        case WorldBooleanManner::Intersect:
            Result = Intersect(SubjectPaths, ClipPaths, FillRule::NonZero);
            break;
    }

    // 🔴 THE OUTERS FIRST, THEN THEIR HOLES. A hole must be declared as its own loop and the analysis will
    //    read it back as a hole by nesting -- exactly how a hand-drawn circle inside a rectangle already
    //    behaves -- so both are declared here and the fill machinery does the rest.
    for (const FlatRing& Ring : PathsToRings(Result))
    {
        const WorldLoopName Loop = DeclareRingLoop(Declared, FirstShape.Frame, Ring.Points);
        if (Loop.Assigned())
            Produced.push_back(Loop);
    }

    if (Produced.empty())
        return Refusal::Refuse({ RefusalReason::ContentUnsupported,
                                 "the boolean left no region" });
    return Refusal::Result(Produced);
#else
    return Refusal::Refuse({ RefusalReason::ContentUnsupported,
                             "the robust planar-area backend is not built in" });
#endif
}

} // namespace Slate
