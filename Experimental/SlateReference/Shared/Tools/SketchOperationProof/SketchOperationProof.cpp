//============================================================================================================================================
//                                                       SKETCHOPERATIONPROOF.CPP
//============================================================================================================================================
// 🧩 Executes Cut, Trim, Extend, Offset and Fill -- the five operations that act on curves already drawn --
//    and proves each against what the operation MEANS rather than against a recording of what it did.
//
// 🔴 CUT DIVIDES AND KEEPS EVERYTHING; TRIM DIVIDES AND THROWS ONE PIECE AWAY. That distinction is the
//    whole reason both exist, and section 1 measures it by TOTAL LENGTH: after a cut the pieces still sum
//    to the original, and after a trim they do not. Length is the property neither operation can fake.
//
// 🔴 A TRIM THROUGH THE MIDDLE LEAVES TWO CURVES, NOT ONE. The tempting implementation shortens the
//    subject and returns it, which silently loses everything beyond the far crossing. Section 2 trims the
//    middle out of a line crossed twice and insists both surviving pieces are there.
//
// 🔴 AN OFFSET'S CORNERS ARE WHERE THE PUSHED LEGS MEET. Pushing each corner sideways along its own normal
//    looks right on a straight run and is wrong at every bend -- the distance stops being constant, which
//    is the one property an offset has. Section 4 measures the perpendicular distance from the offset back
//    to the original at many points along it, including across the corner, and demands they all agree.
//
// 🔴 A CIRCLE INSIDE A CIRCLE IS A TUBE. Both rings are closed and planar, so judged on their own merits
//    both fill and the inner disc is painted over the hole it is meant to be. Section 5 proves the nesting
//    depth decides it, that a third ring inside the hole is material again, and that rings on different
//    planes do not nest however they line up when flattened.
//
// 📝 Negative-tested. Cutting forwards instead of backwards, clamping the offset's sign rather than its
//    magnitude, dropping the far piece of an interior trim, and filling by geometry alone each refute a
//    section below. A gate never seen to fail proves nothing.

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateWorkspace/Discipline/SketchOperationSession/Api/SketchOperationSession.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Slate;

namespace {

unsigned Claims = 0u;
unsigned Failures = 0u;

void Claim(bool Held, const char* Stated)
{
    ++Claims;
    if (!Held)
    {
        std::printf("    FAIL  %s\n", Stated);
        ++Failures;
    }
}

bool Near(double Left, double Right, double Tolerance = 1.0e-6)
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right, double Tolerance = 1.0e-6)
{
    return std::sqrt(LengthSquared(Difference(Left, Right))) <= Tolerance;
}

double LengthOf(const WorldSketchStructure& Sketch, WorldCurveName Name)
{
    const DeclaredWorldCurve* Held = Sketch.Resolve(Name);
    if (Held == nullptr || !Held->Geometry.Declared())
        return 0.0;

    std::vector<SpatialPoint> Polyline;
    AppendCurvePolyline(Held->Geometry, Polyline, 256u);
    double Total = 0.0;
    for (std::size_t Index = 1u; Index < Polyline.size(); ++Index)
        Total += std::sqrt(LengthSquared(Difference(Polyline[Index], Polyline[Index - 1u])));
    return Total;
}

/// 🧩 The perpendicular distance from a point to the INFINITE line a curve lies on.
/// 🔴 THE INFINITE LINE, DELIBERATELY. An offset leg is parallel to its original, and parallelism is a
///    property of the LINE. Measuring to the segment would report the distance to an endpoint wherever
///    the foot fell outside it -- which at a mitred corner it always does, and the true invariant would
///    be hidden behind an artefact of where the original happens to stop.
double DistanceToLineOf(const WorldSketchStructure& Sketch, WorldCurveName Name, const SpatialPoint& Probe)
{
    const DeclaredWorldCurve* Held = Sketch.Resolve(Name);
    if (Held == nullptr || Held->Geometry.Subject() != CurveSubject::Line)
        return 1.0e30;

    const LineCurve& Line = Held->Geometry.HeldLine();
    const SpatialDirection Span = Difference(Line.Origin, Line.Terminus);
    const double Length = std::sqrt(LengthSquared(Span));
    if (!(Length > 1.0e-12))
        return std::sqrt(LengthSquared(Difference(Line.Origin, Probe)));

    const SpatialDirection Along = Normalize(Span);
    const double Projected = Dot(Difference(Line.Origin, Probe), Along);
    const SpatialPoint Foot = Added(Line.Origin, Scaled(Along, Projected));
    return std::sqrt(LengthSquared(Difference(Foot, Probe)));
}

/// 🧩 The shortest distance from a point to a curve, through its tessellation.
double DistanceToCurve(const WorldSketchStructure& Sketch, WorldCurveName Name, const SpatialPoint& Probe)
{
    const DeclaredWorldCurve* Held = Sketch.Resolve(Name);
    if (Held == nullptr)
        return 1.0e30;

    std::vector<SpatialPoint> Polyline;
    AppendCurvePolyline(Held->Geometry, Polyline, 256u);

    double Nearest = 1.0e30;
    for (std::size_t Index = 1u; Index < Polyline.size(); ++Index)
    {
        const SpatialPoint& Start = Polyline[Index - 1u];
        const SpatialPoint& End = Polyline[Index];
        // 📝 `Difference(From, To)` is To minus From. Both spans here run FROM the segment's start.
        const SpatialDirection Span = Difference(Start, End);
        const double SpanSquared = LengthSquared(Span);
        double Parameter = 0.0;
        if (SpanSquared > 1.0e-18)
            Parameter = Dot(Difference(Start, Probe), Span) / SpanSquared;
        Parameter = Parameter < 0.0 ? 0.0 : (Parameter > 1.0 ? 1.0 : Parameter);
        const SpatialPoint Foot = { Start.Left    + Span.Left    * Parameter,
                                    Start.Up      + Span.Up      * Parameter,
                                    Start.Forward + Span.Forward * Parameter };
        const double Distance = std::sqrt(LengthSquared(Difference(Probe, Foot)));
        if (Distance < Nearest)
            Nearest = Distance;
    }
    return Nearest;
}

const WorldPlacementFrame Ground = {{ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};

//------------------------------------------------------------------------------------------------------------------------
//                                          1. CUT DIVIDES AND KEEPS EVERYTHING
//------------------------------------------------------------------------------------------------------------------------

void ProveCutKeepsEverything()
{
    std::printf("\n1. Cut divides a curve and loses nothing\n");

    WorldSketchStructure Sketch;
    const WorldCurveName Line = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    const double Original = LengthOf(Sketch, Line);
    Claim(Near(Original, 100.0), "the line is a hundred units long");

    WorldCurveName Leading = {};
    WorldCurveName Trailing = {};
    Claim(CutWorldCurve(Sketch, Line, { 30.0, 0.0, 0.0 }, Leading, Trailing)
              == OperationVerdict::Produced,
          "cutting thirty units along succeeds");

    // 🔴 THE SUBJECT SURVIVES AS THE LEADING PIECE. Declaring two new curves and retiring the original
    //    would invalidate every loop, constraint and selection that names it -- so the cut shortens the
    //    subject in place and declares only the piece that did not exist before.
    Claim(Leading.IssuedIndex == Line.IssuedIndex, "the subject itself is the leading piece");
    Claim(Trailing.Assigned() && Trailing.IssuedIndex != Line.IssuedIndex,
          "and only the trailing piece is newly declared");

    const double Front = LengthOf(Sketch, Leading);
    const double Back = LengthOf(Sketch, Trailing);
    Claim(Near(Front, 30.0), "the leading piece is thirty units");
    Claim(Near(Back, 70.0), "the trailing piece is seventy units");

    // 📐 The claim that separates Cut from Trim, stated as arithmetic: nothing was removed.
    Claim(Near(Front + Back, Original), "and together they are exactly the original length");

    const DeclaredWorldCurve* HeldFront = Sketch.Resolve(Leading);
    const DeclaredWorldCurve* HeldBack = Sketch.Resolve(Trailing);
    Claim(HeldFront != nullptr && SamePoint(HeldFront->Geometry.HeldLine().Terminus, { 30.0, 0.0, 0.0 }),
          "the pieces meet exactly at the cut point");
    Claim(HeldBack != nullptr && SamePoint(HeldBack->Geometry.HeldLine().Origin, { 30.0, 0.0, 0.0 }),
          "with no gap and no overlap");

    // ⚠️ A cut at an endpoint would declare a zero-length curve for every later measure to defend
    //    itself against. It refuses, and says which reason.
    WorldSketchStructure Edge;
    const WorldCurveName Short = Edge.DeclareLine({ 0.0, 0.0, 0.0 }, { 50.0, 0.0, 0.0 }, Ground);
    WorldCurveName A = {}, B = {};
    Claim(CutWorldCurve(Edge, Short, { 0.0, 0.0, 0.0 }, A, B) == OperationVerdict::PointAtEnd,
          "cutting at the origin refuses as a point-at-end");
    Claim(CutWorldCurve(Edge, Short, { 50.0, 0.0, 0.0 }, A, B) == OperationVerdict::PointAtEnd,
          "and so does cutting at the terminus");
    Claim(CutWorldCurve(Edge, Short, { 25.0, 0.0, 40.0 }, A, B) == OperationVerdict::PointNotOnCurve,
          "a point off the curve refuses as not-on-curve");
    Claim(Edge.CurveCount() == 1u, "and every refusal left the sketch exactly as it was");
}

//------------------------------------------------------------------------------------------------------------------------
//                                       2. A CUT AT EVERY CROSSING, BACK TO FRONT
//------------------------------------------------------------------------------------------------------------------------

void ProveCutAtCrossings()
{
    std::printf("\n2. Cutting at every crossing divides in the right places\n");

    WorldSketchStructure Sketch;
    const WorldCurveName Spine = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Sketch.DeclareLine({ 25.0, 0.0, -20.0 }, { 25.0, 0.0, 20.0 }, Ground);
    Sketch.DeclareLine({ 60.0, 0.0, -20.0 }, { 60.0, 0.0, 20.0 }, Ground);

    std::vector<WorldCurveName> Produced;
    Claim(CutWorldCurveAtCrossings(Sketch, Spine, Produced) == OperationVerdict::Produced,
          "a line crossed twice cuts at both crossings");
    Claim(Produced.size() == 3u, "leaving three pieces");

    // 🔴 THE PIECES MUST BE 25, 35 AND 40 -- IN THAT ORDER. Cutting front to back is the bug this claim
    //    exists to catch: the first cut shortens the subject, so the second crossing's distance, measured
    //    against the ORIGINAL span, then falls beyond the shortened curve and lands in the wrong place.
    //    Cutting back to front leaves every not-yet-used parameter measured against untouched geometry.
    double Total = 0.0;
    if (Produced.size() == 3u)
    {
        Claim(Near(LengthOf(Sketch, Produced[0u]), 25.0), "the first piece runs to the first crossing");
        Claim(Near(LengthOf(Sketch, Produced[1u]), 35.0), "the second spans the two crossings");
        Claim(Near(LengthOf(Sketch, Produced[2u]), 40.0), "the third runs from the last crossing to the end");
        for (const WorldCurveName& Piece : Produced)
            Total += LengthOf(Sketch, Piece);
    }
    Claim(Near(Total, 100.0), "and the three pieces still sum to the original hundred");

    WorldSketchStructure Lonely;
    const WorldCurveName Alone = Lonely.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    std::vector<WorldCurveName> None;
    Claim(CutWorldCurveAtCrossings(Lonely, Alone, None) == OperationVerdict::NoIntersection,
          "a line nothing crosses refuses as no-intersection");
    Claim(Lonely.CurveCount() == 1u, "and is left whole");
}

//------------------------------------------------------------------------------------------------------------------------
//                                        3. TRIM REMOVES WHAT WAS POINTED AT
//------------------------------------------------------------------------------------------------------------------------

void ProveTrimRemovesThePiece()
{
    std::printf("\n3. Trim removes the piece under the pointer, and keeps the rest\n");

    // ① An interior trim: a line crossed twice, pointed at between the crossings.
    WorldSketchStructure Sketch;
    const WorldCurveName Spine = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Sketch.DeclareLine({ 25.0, 0.0, -20.0 }, { 25.0, 0.0, 20.0 }, Ground);
    Sketch.DeclareLine({ 60.0, 0.0, -20.0 }, { 60.0, 0.0, 20.0 }, Ground);

    std::vector<WorldCurveName> Remaining;
    Claim(TrimWorldCurve(Sketch, Spine, { 40.0, 0.0, 0.0 }, Remaining) == OperationVerdict::Produced,
          "pointing between the crossings trims that span");

    // 🔴 TWO PIECES SURVIVE, NOT ONE. Shortening the subject and returning it loses everything past the
    //    far crossing -- forty units of line that simply vanish. This claim is the whole reason
    //    `TrimWorldCurve` reports a vector rather than shortening in place.
    Claim(Remaining.size() == 2u, "and TWO pieces survive, one either side of the removed span");

    double Total = 0.0;
    for (const WorldCurveName& Piece : Remaining)
        Total += LengthOf(Sketch, Piece);
    Claim(Near(Total, 65.0), "the survivors are the 25 before and the 40 after -- 65 units");
    Claim(Total < 100.0, "which is LESS than the original: a trim removes, where a cut does not");

    // 📝 The removed span is genuinely gone: nothing in the sketch passes through its middle any more.
    bool AnythingAtForty = false;
    for (const WorldCurveName& Piece : Remaining)
        if (DistanceToCurve(Sketch, Piece, { 40.0, 0.0, 0.0 }) < 1.0e-6)
            AnythingAtForty = true;
    Claim(!AnythingAtForty, "and no surviving piece still passes through where the artist pointed");

    // ② An overhang: crossed once, pointed at beyond the crossing.
    WorldSketchStructure Overhang;
    const WorldCurveName Long = Overhang.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Overhang.DeclareLine({ 70.0, 0.0, -20.0 }, { 70.0, 0.0, 20.0 }, Ground);

    std::vector<WorldCurveName> Kept;
    Claim(TrimWorldCurve(Overhang, Long, { 90.0, 0.0, 0.0 }, Kept) == OperationVerdict::Produced,
          "pointing past a single crossing trims the overhang");
    Claim(Kept.size() == 1u, "leaving one piece");
    Claim(Kept.size() == 1u && Near(LengthOf(Overhang, Kept[0u]), 70.0),
          "which runs from the free end to the crossing");

    // 🔴 NOTHING CROSSES IT, SO THE WHOLE EDGE IS THE PIECE UNDER THE POINTER, AND TRIM REMOVES IT. This
    //    is the case the tool is used on most -- a side of a drawn shape, an isolated line -- and Trim
    //    once refused it, on the argument that removing a whole edge is a deletion. That refusal is what
    //    made Trim appear to do nothing on ordinary geometry: an edge meets its neighbours only at its
    //    own ends, which are junctions rather than interior crossings, so a rectangle side never had a
    //    bounded piece to take and the click fell on the floor. The piece the artist points at is the
    //    whole edge, and clicking it takes the whole edge.
    WorldSketchStructure Bare;
    const WorldCurveName Only = Bare.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    std::vector<WorldCurveName> Nothing;
    Claim(TrimWorldCurve(Bare, Only, { 50.0, 0.0, 0.0 }, Nothing) == OperationVerdict::Produced,
          "a line nothing crosses is trimmed away whole -- the piece under the pointer is all of it");
    // 🔴 RETIRED IN PLACE, NOT ERASED. The curve keeps its 1-based index -- which every loop, constraint,
    //    dimension and the cross-frame mapping stores by value -- and its geometry is cleared, so it is
    //    gone from the drawing without renumbering anything after it.
    Claim(Bare.CurveCount() == 1u, "its index is kept, so nothing that names a later curve is disturbed");
    Claim(Bare.Resolve(Only) != nullptr && Bare.Resolve(Only)->Retired,
          "and the edge itself is retired -- cleared, and marked so every consumer skips it");
    Claim(!Bare.Resolve(Only)->Geometry.Declared(),
          "its geometry is emptied, which is what the renderer, picker, snapper and analysis already skip");

    // 🔴 A MISS IS STILL A MISS. Removing a whole edge on a bare click must not become "delete whatever
    //    edge was nearest": a probe off the line is refused exactly as the partial trim refuses one.
    WorldSketchStructure Missed;
    const WorldCurveName Aimed = Missed.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    std::vector<WorldCurveName> Untouched;
    Claim(TrimWorldCurve(Missed, Aimed, { 50.0, 0.0, 40.0 }, Untouched) == OperationVerdict::PointNotOnCurve,
          "a click well off the line trims nothing, whole edge or not");
    Claim(!Missed.Resolve(Aimed)->Retired, "and the edge it missed is left standing");

    // 🔴 A CURVED EDGE IS TRIMMED WHOLE, NOT REFUSED AS UNSUPPORTED. The crossing-bounded partial trim is
    //    line geometry, but removing an entire edge is the same act whatever the edge is -- so an arc,
    //    which Trim once turned away outright, is taken out in one piece like any other whole edge. This
    //    is what lets Trim clear a fillet's rounded corner or a slot's end.
    WorldSketchStructure Curved;
    const WorldCurveName Arc = Curved.DeclareThreePointArc({ 0.0, 0.0, 0.0 },
                                                           { 50.0, 0.0, 50.0 },
                                                           { 100.0, 0.0, 0.0 });
    std::vector<WorldCurveName> ArcGone;
    Claim(TrimWorldCurve(Curved, Arc, { 50.0, 0.0, 50.0 }, ArcGone) == OperationVerdict::Produced,
          "an arc is trimmed away whole, where before it was refused as unsupported geometry");
    Claim(Curved.Resolve(Arc)->Retired, "the arc is retired in place, its index kept like any other edge");

    // 🔴 TRIMMING A SIDE OFF A CLOSED SHAPE OPENS IT, AND THE OPEN SHAPE HAS NO FACE. This is the case
    //    the whole fix is for: a drawn rectangle, one side clicked, that side gone -- and the sketch that
    //    remains is still valid, its other three sides intact, its loop reporting itself open so its fill
    //    stops being drawn. That the sketch stays `Declared()` is what proves the removal was a retire in
    //    place and not a corruption: an erased curve would have renumbered the loop's edges onto the
    //    wrong geometry.
    WorldSketchStructure Shape;
    const WorldCurveName Bottom = Shape.DeclareLine({ 0.0, 0.0, 0.0 },   { 100.0, 0.0, 0.0 },   Ground);
    const WorldCurveName Right  = Shape.DeclareLine({ 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }, Ground);
    const WorldCurveName Top    = Shape.DeclareLine({ 100.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 }, Ground);
    const WorldCurveName Left   = Shape.DeclareLine({ 0.0, 0.0, 100.0 }, { 0.0, 0.0, 0.0 },     Ground);
    const WorldLoopName Face = Shape.DeclareLoop({ { { Bottom, true }, { Right, true },
                                                    { Top, true }, { Left, true } } });

    const auto FaceRecord = [](const WorldSketchAnalysis& Analysis, WorldLoopName Name)
        -> const WorldLoopAnalysisRecord*
    {
        for (const WorldLoopAnalysisRecord& Record : Analysis.Loops)
            if (Record.Loop.IssuedIndex == Name.IssuedIndex)
                return &Record;
        return nullptr;
    };

    WorldSketchAnalysis Whole = AnalyzeWorldSketch(Shape, 48u, 0.05, 0.05);
    const WorldLoopAnalysisRecord* WholeFace = FaceRecord(Whole, Face);
    Claim(WholeFace != nullptr && WholeFace->FillEligible, "the closed rectangle fills before the trim");

    std::vector<WorldCurveName> Left3;
    Claim(TrimWorldCurve(Shape, Bottom, { 50.0, 0.0, 0.0 }, Left3) == OperationVerdict::Produced,
          "clicking a side of the rectangle trims that whole side away");
    Claim(Shape.Declared(),
          "and the sketch that remains is still valid -- the retire renumbered nothing");
    Claim(Shape.Resolve(Bottom)->Retired && !Shape.Resolve(Right)->Retired
          && !Shape.Resolve(Top)->Retired && !Shape.Resolve(Left)->Retired,
          "only the clicked side is gone; the other three stand untouched");

    WorldSketchAnalysis Opened = AnalyzeWorldSketch(Shape, 48u, 0.05, 0.05);
    const WorldLoopAnalysisRecord* OpenedFace = FaceRecord(Opened, Face);
    Claim(OpenedFace != nullptr && !OpenedFace->FillEligible,
          "the shape is open now, so its face no longer fills -- exactly as removing an edge should read");

    // ③ 🔴 A T-JUNCTION IS A DIVISION, AND TRIM COULD NOT SEE ONE. Every case above is built from
    //    segments that cross THROUGH the subject, and the crossing test accepted only that transversal
    //    case -- it discarded a `Touching`, which is precisely one segment's endpoint landing on the
    //    other. But a T-junction is how divisions are actually drawn: a line stopping ON an edge, and a
    //    rectangle's own corners. So on ordinary geometry Trim found no bounds, refused, highlighted
    //    nothing and removed nothing -- which is the report. The claims above all passed throughout,
    //    because every one of them was built from the one shape the defect did not touch.
    WorldSketchStructure Junction;
    const WorldCurveName Edge = Junction.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Junction.DeclareLine({ 30.0, 0.0, 0.0 }, { 30.0, 0.0, 40.0 }, Ground);   // stops ON the edge
    Junction.DeclareLine({ 70.0, 0.0, 0.0 }, { 70.0, 0.0, 40.0 }, Ground);   // and so does this

    SpatialPoint JunctionFrom = {};
    SpatialPoint JunctionTo   = {};
    Claim(EvaluateWorldTrim(Junction, Edge, { 50.0, 0.0, 0.0 }, JunctionFrom, JunctionTo)
              == OperationVerdict::Produced,
          "a piece bounded by two T-junctions previews, so the artist SEES what will go");

    std::vector<WorldCurveName> JunctionKept;
    Claim(TrimWorldCurve(Junction, Edge, { 50.0, 0.0, 0.0 }, JunctionKept) == OperationVerdict::Produced,
          "and it trims, where before it refused because neither bound was a crossing");
    Claim(JunctionKept.size() == 2u, "leaving the two stubs either side of the junctions");

    double JunctionTotal = 0.0;
    for (const WorldCurveName& Piece : JunctionKept)
        JunctionTotal += LengthOf(Junction, Piece);
    Claim(Near(JunctionTotal, 60.0), "which measure 30 and 30 -- the middle 40 is gone");

    // 📝 The preview and the commit agree about WHICH span goes, which is what makes the highlight
    //    trustworthy rather than decorative.
    Claim(Near(JunctionFrom.Left, 30.0) && Near(JunctionTo.Left, 70.0),
          "and the highlight spanned exactly the piece the commit removed");
}

//------------------------------------------------------------------------------------------------------------------------
//                                          4. EXTEND MEETS THE NEAREST CURVE
//------------------------------------------------------------------------------------------------------------------------

void ProveExtendMeetsTheNearest()
{
    std::printf("\n4. Extend grows the nearer end to the FIRST thing it meets\n");

    WorldSketchStructure Sketch;
    const WorldCurveName Stub = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 40.0, 0.0, 0.0 }, Ground);
    Sketch.DeclareLine({ 70.0, 0.0, -30.0 }, { 70.0, 0.0, 30.0 }, Ground);   // [-] - the near wall
    Sketch.DeclareLine({ 95.0, 0.0, -30.0 }, { 95.0, 0.0, 30.0 }, Ground);   // [-] - a further wall

    SpatialPoint Landing = {};
    Claim(EvaluateWorldExtend(Sketch, Stub, { 38.0, 0.0, 0.0 }, Landing) == OperationVerdict::Produced,
          "the dry run reports the extend would succeed");

    // 🔴 THE NEAR WALL, AT 70 -- NOT THE FAR ONE AT 95. "Extend to meet" means the first crossing; a
    //    version taking the furthest looks identical on any fixture with only one wall, which is why
    //    this fixture has two.
    Claim(SamePoint(Landing, { 70.0, 0.0, 0.0 }), "and would land on the NEARER wall, not the further");

    Claim(ExtendWorldCurve(Sketch, Stub, { 38.0, 0.0, 0.0 }) == OperationVerdict::Produced,
          "performing it succeeds");
    const DeclaredWorldCurve* Held = Sketch.Resolve(Stub);
    Claim(Held != nullptr && SamePoint(Held->Geometry.HeldLine().Terminus, { 70.0, 0.0, 0.0 }),
          "the terminus travelled to the wall");
    Claim(Held != nullptr && SamePoint(Held->Geometry.HeldLine().Origin, { 0.0, 0.0, 0.0 }),
          "and the far end did NOT move");
    Claim(Near(LengthOf(Sketch, Stub), 70.0), "so the line is now seventy units");

    // ② The other end. Pointing near the origin must grow the origin, whatever the curve's direction.
    WorldSketchStructure Backward;
    const WorldCurveName Middle = Backward.DeclareLine({ 50.0, 0.0, 0.0 }, { 90.0, 0.0, 0.0 }, Ground);
    Backward.DeclareLine({ 10.0, 0.0, -30.0 }, { 10.0, 0.0, 30.0 }, Ground);

    Claim(ExtendWorldCurve(Backward, Middle, { 52.0, 0.0, 0.0 }) == OperationVerdict::Produced,
          "pointing near the origin extends the origin");
    const DeclaredWorldCurve* Grown = Backward.Resolve(Middle);
    Claim(Grown != nullptr && SamePoint(Grown->Geometry.HeldLine().Origin, { 10.0, 0.0, 0.0 }),
          "which travels backwards to the wall behind it");
    Claim(Grown != nullptr && SamePoint(Grown->Geometry.HeldLine().Terminus, { 90.0, 0.0, 0.0 }),
          "while the terminus stays put");

    // ⚠️ Nothing to meet. Extending by some invented default would produce geometry the artist neither
    //    asked for nor could predict, so it refuses and the preview shows nothing.
    WorldSketchStructure Empty;
    const WorldCurveName Free = Empty.DeclareLine({ 0.0, 0.0, 0.0 }, { 40.0, 0.0, 0.0 }, Ground);
    Claim(ExtendWorldCurve(Empty, Free, { 38.0, 0.0, 0.0 }) == OperationVerdict::NoIntersection,
          "with nothing ahead it refuses rather than inventing a length");
    Claim(Near(LengthOf(Empty, Free), 40.0), "and the line keeps its original length");
}

//------------------------------------------------------------------------------------------------------------------------
//                                       5. AN OFFSET IS EVERYWHERE THE SAME DISTANCE
//------------------------------------------------------------------------------------------------------------------------

void ProveOffsetHoldsItsDistance()
{
    std::printf("\n5. An offset holds its distance, corners included\n");

    // 📝 An L, deliberately: a straight run alone cannot tell a correct offset from one that pushes each
    //    endpoint sideways, because on a straight run the two agree exactly. The bend is the whole test.
    WorldSketchStructure Sketch;
    const WorldCurveName AB = Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 100.0, 0.0, 0.0 },   Ground);
    const WorldCurveName BC = Sketch.DeclareLine({ 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }, Ground);
    const std::vector<WorldCurveName> Chain = { AB, BC };

    std::vector<WorldCurveName> Produced;
    Claim(OffsetWorldChain(Sketch, Chain, Ground, 10.0, Produced) == OperationVerdict::Produced,
          "a ten-unit offset of a bent chain succeeds");
    Claim(Produced.size() == 2u, "producing one curve per input curve");

    Claim(Near(LengthOf(Sketch, AB), 100.0) && Near(LengthOf(Sketch, BC), 100.0),
          "the original chain is untouched -- an offset is always a new curve");

    // 🔴 EACH OFFSET LEG IS PARALLEL TO ITS OWN ORIGINAL, AT EXACTLY TEN, ALONG ITS WHOLE LENGTH. This
    //    is the claim a corner-pushing offset fails: pushing the shared corner B along the AVERAGE of the
    //    two normals lands it at about (107.07, -7.07), so the first offset leg runs from ten units out
    //    to seven and is not parallel to anything. Sampling only the ends would let that through, since
    //    the far end of each leg is right; sampling the whole length is what catches it.
    unsigned Sampled = 0u;
    unsigned Wrong = 0u;
    for (std::size_t Leg = 0u; Leg < Produced.size() && Leg < Chain.size(); ++Leg)
    {
        const DeclaredWorldCurve* Held = Sketch.Resolve(Produced[Leg]);
        if (Held == nullptr)
            continue;

        std::vector<SpatialPoint> Polyline;
        AppendCurvePolyline(Held->Geometry, Polyline, 64u);
        for (const SpatialPoint& Point : Polyline)
        {
            ++Sampled;
            if (!Near(DistanceToLineOf(Sketch, Chain[Leg], Point), 10.0, 1.0e-6))
                ++Wrong;
        }
    }
    Claim(Sampled >= 4u, "the offset was sampled along its whole length");
    Claim(Wrong == 0u, "and EVERY sample sits exactly ten units from the leg it offsets");

    // 📝 The mitred corner is FURTHER than ten from the original corner, and that is correct rather
    //    than a defect: the intersection of two lines each ten units out is ten-root-two out at a right
    //    angle. A corner held at ten would leave a gap on the outside of the bend.
    const DeclaredWorldCurve* Mitre = Produced.size() == 2u ? Sketch.Resolve(Produced[0u]) : nullptr;
    Claim(Mitre != nullptr &&
              Near(std::sqrt(LengthSquared(Difference(SpatialPoint{ 100.0, 0.0, 0.0 },
                                                      Mitre->Geometry.HeldLine().Terminus))),
                   10.0 * std::sqrt(2.0)),
          "and the mitred corner stands ten-root-two out, where the two offset lines actually meet");

    // ② The corner of the offset is where the two pushed legs MEET, which for a right angle offset
    //    outward by ten is a point ten units out along both axes.
    const DeclaredWorldCurve* FirstOffset = Produced.size() == 2u ? Sketch.Resolve(Produced[0u]) : nullptr;
    const DeclaredWorldCurve* SecondOffset = Produced.size() == 2u ? Sketch.Resolve(Produced[1u]) : nullptr;
    Claim(FirstOffset != nullptr && SecondOffset != nullptr &&
              SamePoint(FirstOffset->Geometry.HeldLine().Terminus,
                        SecondOffset->Geometry.HeldLine().Origin),
          "the offset legs meet at a shared corner, leaving no gap");

    // ③ The sign chooses the side, and the two sides are mirror images about the original.
    std::vector<WorldCurveName> Other;
    Claim(OffsetWorldChain(Sketch, Chain, Ground, -10.0, Other) == OperationVerdict::Produced,
          "a negative distance offsets the other way");
    const DeclaredWorldCurve* Left = Produced.empty() ? nullptr : Sketch.Resolve(Produced[0u]);
    const DeclaredWorldCurve* Right = Other.empty() ? nullptr : Sketch.Resolve(Other[0u]);
    Claim(Left != nullptr && Right != nullptr &&
              !SamePoint(Left->Geometry.HeldLine().Origin, Right->Geometry.HeldLine().Origin),
          "and lands somewhere else entirely");

    // ④ Zero is not an offset.
    std::vector<WorldCurveName> Nothing;
    Claim(OffsetWorldChain(Sketch, Chain, Ground, 0.0, Nothing) == OperationVerdict::DistanceNotPositive,
          "a zero distance refuses -- that is a copy, not an offset");
}

//------------------------------------------------------------------------------------------------------------------------
//                                          6. THE OFFSET STATES ITS OWN LIMIT
//------------------------------------------------------------------------------------------------------------------------

void ProveOffsetLimit()
{
    std::printf("\n6. An offset states the distance at which it would collapse\n");

    // 📝 A narrow rectangle: offsetting inward by half the short side collapses it to a line, and
    //    anything beyond that turns it inside out.
    WorldSketchStructure Sketch;
    const WorldCurveName AB = Sketch.DeclareLine({ 0.0, 0.0, 0.0 },    { 200.0, 0.0, 0.0 },  Ground);
    const WorldCurveName BC = Sketch.DeclareLine({ 200.0, 0.0, 0.0 },  { 200.0, 0.0, 40.0 }, Ground);
    const WorldCurveName CD = Sketch.DeclareLine({ 200.0, 0.0, 40.0 }, { 0.0, 0.0, 40.0 },   Ground);
    const WorldCurveName DA = Sketch.DeclareLine({ 0.0, 0.0, 40.0 },   { 0.0, 0.0, 0.0 },    Ground);
    const std::vector<WorldCurveName> Ring = { AB, BC, CD, DA };

    const double Limit = ResolveOffsetLimit(Sketch, Ring, Ground);
    Claim(Limit > 0.0, "the chain reports a positive limit");

    // 🔴 THE LIMIT IS THE EDGE OF WHAT WORKS, from both sides. A limit that is merely SAFE could be
    //    reported as one and pass; this insists that just inside it succeeds and just outside it fails.
    std::vector<WorldCurveName> Inside;
    Claim(OffsetWorldChain(Sketch, Ring, Ground, -(Limit * 0.99), Inside) == OperationVerdict::Produced,
          "just inside the limit still offsets");

    std::vector<WorldCurveName> Beyond;
    Claim(OffsetWorldChain(Sketch, Ring, Ground, -(Limit * 1.1), Beyond) == OperationVerdict::WouldCollapse,
          "and just beyond it refuses as a collapse rather than folding inside out");

    // 📝 A forty-unit-wide rectangle collapses at twenty. The limit is geometry, not a guess.
    Claim(Limit < 21.0 && Limit > 19.0, "and the limit of a forty-wide rectangle is about twenty");
}

//------------------------------------------------------------------------------------------------------------------------
//                                         7. FILL IS A WISH, NESTING IS A RULE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A ring of `Sides` straight curves approximating a circle, declared as a closed loop.
WorldLoopName DeclareRing(WorldSketchStructure& Sketch,
                          const SpatialPoint& Centre,
                          double Radius,
                          const WorldPlacementFrame& Frame,
                          unsigned Sides = 24u)
{
    std::vector<SpatialPoint> Points;
    for (unsigned Step = 0u; Step < Sides; ++Step)
    {
        const double Angle = 6.283185307179586 * static_cast<double>(Step) / static_cast<double>(Sides);
        const double Along = std::cos(Angle) * Radius;
        const double Across = std::sin(Angle) * Radius;
        Points.push_back({ Centre.Left + Frame.AlongDirection.Left * Along
                               + (Frame.Normal.Up * Frame.AlongDirection.Forward
                                  - Frame.Normal.Forward * Frame.AlongDirection.Up) * Across,
                           Centre.Up + Frame.AlongDirection.Up * Along
                               + (Frame.Normal.Forward * Frame.AlongDirection.Left
                                  - Frame.Normal.Left * Frame.AlongDirection.Forward) * Across,
                           Centre.Forward + Frame.AlongDirection.Forward * Along
                               + (Frame.Normal.Left * Frame.AlongDirection.Up
                                  - Frame.Normal.Up * Frame.AlongDirection.Left) * Across });
    }

    std::vector<WorldCurveUse> Traversal;
    for (unsigned Step = 0u; Step < Sides; ++Step)
    {
        const WorldCurveName Edge =
            Sketch.DeclareLine(Points[Step], Points[(Step + 1u) % Sides], Frame);
        Traversal.push_back({ Edge, true });
    }
    return Sketch.DeclareLoop({ Traversal });
}

const WorldLoopAnalysisRecord* RecordFor(const WorldSketchAnalysis& Analysis, WorldLoopName Name)
{
    for (const WorldLoopAnalysisRecord& Record : Analysis.Loops)
        if (Record.Loop.IssuedIndex == Name.IssuedIndex)
            return &Record;
    return nullptr;
}

void ProveFillAndNesting()
{
    std::printf("\n7. Fill is the artist's wish; nesting is geometry's rule\n");

    // ① A circle inside a circle is a TUBE.
    WorldSketchStructure Sketch;
    const WorldLoopName Outer = DeclareRing(Sketch, { 0.0, 0.0, 0.0 }, 100.0, Ground);
    const WorldLoopName Inner = DeclareRing(Sketch, { 0.0, 0.0, 0.0 }, 40.0, Ground);

    WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch, 48u, 0.05, 0.05);
    const WorldLoopAnalysisRecord* OuterRecord = RecordFor(Analysis, Outer);
    const WorldLoopAnalysisRecord* InnerRecord = RecordFor(Analysis, Inner);

    Claim(OuterRecord != nullptr && InnerRecord != nullptr, "both rings are analysed");
    Claim(OuterRecord != nullptr && OuterRecord->Closed && OuterRecord->Coplanar,
          "the outer ring is closed and planar");
    Claim(InnerRecord != nullptr && InnerRecord->Closed && InnerRecord->Coplanar,
          "and so, on its own merits, is the inner one");

    // 🔴 THIS IS THE TUBE. Both rings pass every geometric test for filling, and filling both is exactly
    //    the bug: the inner disc is painted over the hole. Depth decides it instead.
    Claim(OuterRecord != nullptr && OuterRecord->Nesting == 0u, "the outer ring is enclosed by nothing");
    Claim(InnerRecord != nullptr && InnerRecord->Nesting == 1u, "the inner ring is enclosed by one loop");
    Claim(InnerRecord != nullptr && InnerRecord->Hole, "so the inner ring is a HOLE");
    Claim(OuterRecord != nullptr && !OuterRecord->Hole, "and the outer ring is not");
    Claim(OuterRecord != nullptr && OuterRecord->FillEligible, "the outer ring fills");
    Claim(InnerRecord != nullptr && !InnerRecord->FillEligible,
          "the inner ring does NOT -- so a circle in a circle draws as a tube, not a disc");
    Claim(InnerRecord != nullptr && InnerRecord->Container.IssuedIndex == Outer.IssuedIndex,
          "and the hole names the face it is a hole in");

    // ② An island inside the hole is material again. Depth two, even, filled.
    const WorldLoopName Island = DeclareRing(Sketch, { 0.0, 0.0, 0.0 }, 15.0, Ground);
    Analysis = AnalyzeWorldSketch(Sketch, 48u, 0.05, 0.05);
    const WorldLoopAnalysisRecord* IslandRecord = RecordFor(Analysis, Island);
    Claim(IslandRecord != nullptr && IslandRecord->Nesting == 2u, "a third ring inside the hole is depth two");
    Claim(IslandRecord != nullptr && !IslandRecord->Hole, "which is EVEN, so it is material again");
    Claim(IslandRecord != nullptr && IslandRecord->FillEligible, "and it fills");
    Claim(IslandRecord != nullptr && IslandRecord->Container.IssuedIndex == Inner.IssuedIndex,
          "sitting directly inside the hole rather than inside the outermost ring");

    // ③ Rings on different planes do not nest, however they line up when flattened.
    WorldSketchStructure Perpendicular;
    const WorldPlacementFrame Wall = {{ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }};
    const WorldLoopName Floor = DeclareRing(Perpendicular, { 0.0, 0.0, 0.0 }, 100.0, Ground);
    const WorldLoopName Upright = DeclareRing(Perpendicular, { 0.0, 0.0, 0.0 }, 40.0, Wall);
    const WorldSketchAnalysis Split = AnalyzeWorldSketch(Perpendicular, 48u, 0.05, 0.05);

    const WorldLoopAnalysisRecord* FloorRecord = RecordFor(Split, Floor);
    const WorldLoopAnalysisRecord* UprightRecord = RecordFor(Split, Upright);
    Claim(FloorRecord != nullptr && FloorRecord->Nesting == 0u, "a ring on the floor nests in nothing");
    Claim(UprightRecord != nullptr && UprightRecord->Nesting == 0u,
          "and a ring on a WALL is not inside it, however the two overlap when flattened");
    Claim(UprightRecord != nullptr && !UprightRecord->Hole, "so neither becomes a hole in the other");

    // ④ Fill is a WISH, and the wish is what the toggle writes.
    Claim(WorldLoopFillWanted(Sketch, Outer), "a loop wants its fill by default");
    Claim(DeclareWorldLoopFill(Sketch, Outer, false), "the Fill tool turns it off");
    Claim(!WorldLoopFillWanted(Sketch, Outer), "and the wish is remembered");

    Analysis = AnalyzeWorldSketch(Sketch, 48u, 0.05, 0.05);
    OuterRecord = RecordFor(Analysis, Outer);
    Claim(OuterRecord != nullptr && OuterRecord->Closed && OuterRecord->Coplanar,
          "the ring is still perfectly fillable geometry");
    Claim(OuterRecord != nullptr && !OuterRecord->FillEligible,
          "but is not filled, because the artist said not to -- the wish overrides the geometry");

    Claim(DeclareWorldLoopFill(Sketch, Outer, true), "toggling it back on succeeds");
    Analysis = AnalyzeWorldSketch(Sketch, 48u, 0.05, 0.05);
    OuterRecord = RecordFor(Analysis, Outer);
    Claim(OuterRecord != nullptr && OuterRecord->FillEligible, "and the face returns");

    // ⑤ Wishing an open loop filled records the wish and fills nothing.
    WorldSketchStructure Open;
    const WorldCurveName One = Open.DeclareLine({ 0.0, 0.0, 0.0 },   { 100.0, 0.0, 0.0 },   Ground);
    const WorldCurveName Two = Open.DeclareLine({ 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }, Ground);
    const WorldLoopName Gap = Open.DeclareLoop({ { { One, true }, { Two, true } } });

    Claim(DeclareWorldLoopFill(Open, Gap, true), "an open loop accepts the wish");
    const WorldSketchAnalysis Unclosed = AnalyzeWorldSketch(Open, 48u, 0.05, 0.05);
    const WorldLoopAnalysisRecord* GapRecord = RecordFor(Unclosed, Gap);
    Claim(GapRecord != nullptr && !GapRecord->Closed, "the loop is not closed");
    Claim(GapRecord != nullptr && !GapRecord->FillEligible,
          "and does not fill -- Fill cannot make an open loop fillable, and does not pretend to");
}

//------------------------------------------------------------------------------------------------------------------------
//                                          8. THE GESTURES, START TO FINISH
//------------------------------------------------------------------------------------------------------------------------

void ProveTheGestures()
{
    std::printf("\n8. The gestures: a click for the click operations, a drag for the one with a figure\n");

    // ① Trim: hover, then release. No readout, because there is no figure to set.
    WorldSketchStructure Sketch;
    const WorldCurveName Spine = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Sketch.DeclareLine({ 70.0, 0.0, -20.0 }, { 70.0, 0.0, 20.0 }, Ground);

    SketchOperationSession Session;
    Session.Manner = OperationManner::Trim;
    const std::vector<WorldCurveName> NoChain;

    AdvanceSketchOperationSession(Sketch, NoChain, Ground, { { 400.0, 0.0, 400.0 }, false, false, false },
                                  Session);
    Claim(Session.Phase == OperationPhase::Idle, "with the pointer nowhere near a curve, nothing is armed");
    Claim(!Session.Target.Assigned(), "and no target is named");

    AdvanceSketchOperationSession(Sketch, NoChain, Ground, { { 90.0, 0.0, 0.0 }, false, false, false },
                                  Session);
    Claim(Session.Phase == OperationPhase::Ready, "hovering the overhang arms the trim");
    Claim(Session.Target.IssuedIndex == Spine.IssuedIndex, "on the curve under the pointer");

    // 🔴 NO READOUT FOR A CLICK OPERATION. A popup asking Apply for a click already made is a second
    //    confirmation of a decision already taken.
    Claim(!Session.ReadoutStanding(), "and raises NO readout, because a trim has no figure to set");

    AdvanceSketchOperationSession(Sketch, NoChain, Ground, { { 90.0, 0.0, 0.0 }, false, false, true },
                                  Session);
    Claim(Session.Phase == OperationPhase::Applied, "releasing performs it");

    std::vector<WorldCurveName> Produced;
    Claim(PerformSketchOperation(Sketch, NoChain, Ground, Session, Produced) == OperationVerdict::Produced,
          "and the operation runs");
    Claim(Produced.size() == 1u && Near(LengthOf(Sketch, Produced[0u]), 70.0),
          "leaving the seventy units up to the crossing");
    Claim(Session.Phase == OperationPhase::Idle, "the session returns to hunting for the next one");

    // ② Offset: hover, press, drag, clamp, release to the readout, type, apply.
    WorldSketchStructure Ring;
    const WorldCurveName AB = Ring.DeclareLine({ 0.0, 0.0, 0.0 },    { 200.0, 0.0, 0.0 },  Ground);
    const WorldCurveName BC = Ring.DeclareLine({ 200.0, 0.0, 0.0 },  { 200.0, 0.0, 40.0 }, Ground);
    const WorldCurveName CD = Ring.DeclareLine({ 200.0, 0.0, 40.0 }, { 0.0, 0.0, 40.0 },   Ground);
    const WorldCurveName DA = Ring.DeclareLine({ 0.0, 0.0, 40.0 },   { 0.0, 0.0, 0.0 },    Ground);
    const std::vector<WorldCurveName> Chain = { AB, BC, CD, DA };

    SketchOperationSession Drag;
    Drag.Manner = OperationManner::Offset;

    AdvanceSketchOperationSession(Ring, Chain, Ground, { { 0.0, 0.0, -5.0 }, false, false, false }, Drag);
    Claim(Drag.Phase == OperationPhase::Ready, "with a chain chosen, the offset is armed");
    Claim(Drag.Limit > 0.0, "and it knows the distance at which it would collapse");
    Claim(!Drag.ReadoutStanding(), "no readout yet -- nothing has been dragged");

    AdvanceSketchOperationSession(Ring, Chain, Ground, { { 0.0, 0.0, -5.0 }, true, true, false }, Drag);
    Claim(Drag.Phase == OperationPhase::Dragging, "pressing begins the drag");
    Claim(Drag.ReadoutStanding(), "and NOW the readout stands, because there is a figure to show");

    AdvanceSketchOperationSession(Ring, Chain, Ground, { { 100.0, 0.0, -12.0 }, false, true, false }, Drag);
    Claim(Near(std::fabs(Drag.Distance), 12.0, 1.0e-6), "the figure follows the pointer");
    Claim(!Drag.Clamped, "within the limit, nothing is clamped");

    // 🔴 THE CLAMP BINDS THE MAGNITUDE AND KEEPS THE SIGN. Clamping the signed value would flip an
    //    inward drag outward at the limit, which is a very confusing thing to watch happen.
    const double Sign = Drag.Distance < 0.0 ? -1.0 : 1.0;
    AdvanceSketchOperationSession(Ring, Chain, Ground, { { 100.0, 0.0, -900.0 }, false, true, false }, Drag);
    Claim(Drag.Clamped, "dragging far past the limit clamps");
    Claim(Near(std::fabs(Drag.Distance), Drag.Limit, 1.0e-9), "at exactly the limit");
    Claim((Drag.Distance < 0.0 ? -1.0 : 1.0) == Sign, "and on the SAME SIDE it was being dragged");

    const std::uint32_t Before = Ring.CurveCount();
    AdvanceSketchOperationSession(Ring, Chain, Ground, { { 100.0, 0.0, -12.0 }, false, false, true }, Drag);
    Claim(Drag.Phase == OperationPhase::Pending, "releasing hands the figure to the readout");
    Claim(Drag.ReadoutStanding(), "which is still standing");
    Claim(Ring.CurveCount() == Before, "and writes NOTHING -- the release is not the commit");

    // ③ A typed figure is the same number, through the same clamp.
    DeclareOperationDistance(Drag, -8.0);
    Claim(Near(Drag.Distance, -8.0), "a typed figure replaces the dragged one");
    DeclareOperationDistance(Drag, -900.0);
    Claim(Near(std::fabs(Drag.Distance), Drag.Limit, 1.0e-9),
          "and passes the same clamp -- the readout is a way to be precise, not a way around the limit");

    DeclareOperationDistance(Drag, -8.0);
    std::vector<WorldCurveName> Offset;
    Claim(PerformSketchOperation(Ring, Chain, Ground, Drag, Offset) == OperationVerdict::Produced,
          "Apply performs the offset");
    Claim(Offset.size() == 4u, "producing four curves for the four sides");
    Claim(Ring.CurveCount() == Before + 4u, "and only now does the sketch grow");

    // ④ Cancelling writes nothing, because nothing was ever written.
    SketchOperationSession Abandoned;
    Abandoned.Manner = OperationManner::Offset;
    AdvanceSketchOperationSession(Ring, Chain, Ground, { { 0.0, 0.0, -5.0 }, true, true, false }, Abandoned);
    AdvanceSketchOperationSession(Ring, Chain, Ground, { { 0.0, 0.0, -15.0 }, false, false, true },
                                  Abandoned);
    const std::uint32_t Standing = Ring.CurveCount();
    CancelSketchOperationSession(Abandoned);
    Claim(Abandoned.Phase == OperationPhase::Idle, "cancelling returns the session to idle");
    Claim(!Abandoned.ReadoutStanding(), "the readout goes away");
    Claim(Ring.CurveCount() == Standing, "and the sketch is untouched");
}

//----------------------------------------------------------------------------------------------------
// 🔴 THE TWO REASONS THE OPERATIONS "DID NOTHING".
//----------------------------------------------------------------------------------------------------
// Every claim above passes and yet Cut, Trim, Extend and Fill were all dead in the editor. Both causes
// are in the seam between the pointer and the session, which is precisely what the sections above never
// exercise: they hand the session a probe already sitting on the geometry, at a zoom nobody chose.
//
// ① THE REACH WAS A FIXED WORLD DISTANCE. Reaching a curve is the ENTIRE precondition of the four click
//    operations. `OperationProbeReach` is 8 world units, so at metre scale it is a fraction of a pixel:
//    the probe never lands inside it, the session reports `SubjectMissing`, and nothing the artist
//    clicks does anything at all.
//
// ② FILL NEVER NAMED ITS LOOP. `Session.Loop` was cleared on cancel and read on apply, and nothing in
//    between ever wrote it -- so the apply resolved a default name and was refused every time. Section 7
//    above missed it entirely because it calls `DeclareWorldLoopFill` directly, never through a gesture.
void ProveTheReachAndTheFillSubject()
{
    std::printf("\n9. The reach follows the zoom, and Fill names the loop it is pointing at\n");

    // ① The defect as arithmetic. `OrthoScale` is pixels per world unit.
    const double ZoomedIn  = 10.0;    // [px/unit] - a small part filling the leaf
    const double ZoomedOut = 0.05;    // [px/unit] - ten metres across the leaf

    Claim(OperationProbeReach * ZoomedOut < 1.0,
          "the fixed world reach is SUB-PIXEL at metre scale, which is why the tools looked dead");

    const double ReachIn  = OperationProbeReachPixels / ZoomedIn;
    const double ReachOut = OperationProbeReachPixels / ZoomedOut;
    Claim(Near(ReachIn * ZoomedIn, OperationProbeReachPixels, 1.0e-9) &&
          Near(ReachOut * ZoomedOut, OperationProbeReachPixels, 1.0e-9),
          "a pixel-derived reach is worth the same pixels at both zooms");

    // ② A trim aimed 30 units off the line: far outside the 8-unit default, well inside the zoomed-out
    //    reach. This is exactly the press the editor used to ignore.
    WorldSketchStructure Sketch;
    const WorldCurveName Spine = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Sketch.DeclareLine({ 70.0, 0.0, -40.0 }, { 70.0, 0.0, 40.0 }, Ground);

    const std::vector<WorldCurveName> NoChain;
    // 📝 Placed beyond the crossing's own extent as well as off the spine, so the nearest curve is
    //    unambiguously the spine: at x=95 the crossing line is 25 away and the spine is 20.
    const SpatialPoint Off = { 95.0, 0.0, 20.0 };   // 20 units clear of the spine

    SketchOperationSession Missed;
    Missed.Manner = OperationManner::Trim;
    AdvanceSketchOperationSession(Sketch, NoChain, Ground, { Off, false, false, false, 0.0 }, Missed);
    Claim(Missed.Phase == OperationPhase::Idle,
          "with no reach stated, the default is too small and the trim never arms");

    SketchOperationSession Reached;
    Reached.Manner = OperationManner::Trim;
    AdvanceSketchOperationSession(Sketch, NoChain, Ground, { Off, false, false, false, ReachOut }, Reached);
    Claim(Reached.Phase == OperationPhase::Ready,
          "the SAME probe arms the trim once the zoomed-out reach is stated");
    Claim(Reached.Target.IssuedIndex == Spine.IssuedIndex, "on the curve actually nearest it");

    // ③ And it must still narrow: zoomed in, that probe is correctly out of reach.
    SketchOperationSession Tight;
    Tight.Manner = OperationManner::Trim;
    AdvanceSketchOperationSession(Sketch, NoChain, Ground, { Off, false, false, false, ReachIn }, Tight);
    Claim(Tight.Phase == OperationPhase::Idle,
          "and zoomed in the same probe is out of reach -- the reach narrows as well as widens");

    // ④ FILL, DRIVEN AS A GESTURE RATHER THAN CALLED DIRECTLY.
    WorldSketchStructure Shape;
    const WorldCurveName AB = Shape.DeclareLine({ 0.0, 0.0, 0.0 },     { 100.0, 0.0, 0.0 },   Ground);
    const WorldCurveName BC = Shape.DeclareLine({ 100.0, 0.0, 0.0 },   { 100.0, 0.0, 100.0 }, Ground);
    const WorldCurveName CD = Shape.DeclareLine({ 100.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 },   Ground);
    const WorldCurveName DA = Shape.DeclareLine({ 0.0, 0.0, 100.0 },   { 0.0, 0.0, 0.0 },     Ground);
    const WorldLoopName Face = Shape.DeclareLoop({ { { AB, true }, { BC, true },
                                                    { CD, true }, { DA, true } } });

    SketchOperationSession Filling;
    Filling.Manner = OperationManner::Fill;

    AdvanceSketchOperationSession(Shape, NoChain, Ground, { { 50.0, 0.0, 2.0 }, false, false, false, 0.0 },
                                  Filling);
    Claim(Filling.Phase == OperationPhase::Ready, "hovering an edge of the square arms the fill");
    Claim(Filling.Loop.IssuedIndex == Face.IssuedIndex,
          "and NAMES THE LOOP that edge belongs to -- nothing ever wrote this, so Fill could not work");

    Claim(WorldLoopFillWanted(Shape, Face), "the face is wanted to begin with");

    AdvanceSketchOperationSession(Shape, NoChain, Ground, { { 50.0, 0.0, 2.0 }, false, false, true, 0.0 },
                                  Filling);
    Claim(Filling.Phase == OperationPhase::Applied, "releasing performs it");

    std::vector<WorldCurveName> Nothing;
    Claim(PerformSketchOperation(Shape, NoChain, Ground, Filling, Nothing) == OperationVerdict::Produced,
          "and the fill toggle actually runs, instead of refusing a loop it was never given");
    Claim(!WorldLoopFillWanted(Shape, Face), "the face is switched off, which is what the artist asked");

    // ⑤ Pointing at a curve no loop uses has no face to toggle, and says so rather than pretending.
    WorldSketchStructure Loose;
    Loose.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);

    SketchOperationSession Stray;
    Stray.Manner = OperationManner::Fill;
    AdvanceSketchOperationSession(Loose, NoChain, Ground, { { 50.0, 0.0, 2.0 }, false, false, false, 0.0 },
                                  Stray);
    Claim(!Stray.Loop.Assigned(), "a lone line belongs to no loop");
    Claim(Stray.Preview == OperationVerdict::SubjectMissing,
          "and the preview reports there is nothing to fill, rather than arming a refusal");
}

//----------------------------------------------------------------------------------------------------
// 🔴 THE PREVIEWS. WHAT THE ARTIST IS SHOWN BEFORE THEY COMMIT.
//----------------------------------------------------------------------------------------------------
// The operations all worked and none of them said so. Trim resolved the piece it would delete and drew
// nothing; Cut snapped a division point and drew nothing; both reported `Produced` merely because a
// curve was in reach, so they promised success at places the commit then refused. What is proven here is
// that the preview and the commit are the SAME answer -- not two opinions that happen to agree today.
void ProveThePreviews()
{
    std::printf("\n10. Trim and Cut say what they are about to do\n");

    const std::vector<WorldCurveName> NoChain;

    // A spine crossed twice, so it has three pieces: [0,30], [30,70], [70,100].
    WorldSketchStructure Sketch;
    const WorldCurveName Spine = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Sketch.DeclareLine({ 30.0, 0.0, -20.0 }, { 30.0, 0.0, 20.0 }, Ground);
    Sketch.DeclareLine({ 70.0, 0.0, -20.0 }, { 70.0, 0.0, 20.0 }, Ground);

    // ① THE MIDDLE PIECE. Pointing between the crossings must name exactly that span.
    SpatialPoint From = {};
    SpatialPoint To   = {};
    Claim(EvaluateWorldTrim(Sketch, Spine, { 50.0, 0.0, 0.0 }, From, To) == OperationVerdict::Produced,
          "a trim between two crossings is offered");
    Claim(Near(From.Left, 30.0, 1.0e-9) && Near(To.Left, 70.0, 1.0e-9),
          "and the span it names is the piece BETWEEN them -- what the click will delete");

    // ② THE OVERHANG. Beyond the last crossing the piece runs to the curve's own end.
    Claim(EvaluateWorldTrim(Sketch, Spine, { 85.0, 0.0, 0.0 }, From, To) == OperationVerdict::Produced,
          "an overhang past the last crossing is offered");
    Claim(Near(From.Left, 70.0, 1.0e-9) && Near(To.Left, 100.0, 1.0e-9),
          "and it runs from that crossing to the free end");

    // ③ THE PREVIEW IS THE COMMIT. Whatever span was promised is the span that actually goes.
    Claim(EvaluateWorldTrim(Sketch, Spine, { 50.0, 0.0, 0.0 }, From, To) == OperationVerdict::Produced,
          "the middle piece is promised again");
    {
        WorldSketchStructure Copy = Sketch;
        std::vector<WorldCurveName> Remaining;
        Claim(TrimWorldCurve(Copy, Spine, { 50.0, 0.0, 0.0 }, Remaining) == OperationVerdict::Produced,
              "and performing it succeeds");
        const LineCurve& Kept = Copy.Resolve(Spine)->Geometry.HeldLine();
        Claim(Near(Kept.Terminus.Left, From.Left, 1.0e-9),
              "the piece that survives ends exactly where the promise began -- one answer, not two");
    }

    // ④ A CURVE WITH NOTHING CROSSING IT IS THE WHOLE PIECE, and the preview highlights it end to end --
    //    exactly the edge the click will remove, so the highlight and the commit are one answer.
    WorldSketchStructure Lone;
    const WorldCurveName Free = Lone.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    Claim(EvaluateWorldTrim(Lone, Free, { 50.0, 0.0, 0.0 }, From, To) == OperationVerdict::Produced,
          "an uncrossed curve offers its whole self as the piece that would go");
    Claim(Near(From.Left, 0.0, 1.0e-9) && Near(To.Left, 100.0, 1.0e-9),
          "and the span it names runs end to end of the edge");
    {
        // 🔴 THE PREVIEW IS THE COMMIT, for the whole-edge case too. What the highlight promised is what
        //    the click takes: the edge is retired, and it kept its index doing so.
        WorldSketchStructure Copy = Lone;
        std::vector<WorldCurveName> Gone;
        Claim(TrimWorldCurve(Copy, Free, { 50.0, 0.0, 0.0 }, Gone) == OperationVerdict::Produced,
              "and performing it removes the whole edge that was promised");
        Claim(Copy.CurveCount() == 1u && Copy.Resolve(Free)->Retired,
              "the edge is retired in place -- gone from the drawing, its index undisturbed");
    }

    // ⑤ CUT SNAPS ONTO THE CURVE. The marker must sit on the line, not beside it.
    // 📝 `OnCurveTolerance` is 1e-3, so the probe must genuinely be ON the curve -- the driver hands
    //    over a point already intersected with the workplane, not a raw pointer position. A probe a
    //    few units clear is correctly refused, which claim ⑨ below states outright.
    SpatialPoint Division = {};
    Claim(EvaluateWorldCut(Sketch, Spine, { 42.0, 0.0, 1.0e-4 }, Division) == OperationVerdict::Produced,
          "a cut on the line is offered");
    Claim(Near(Division.Forward, 0.0, 1.0e-9) && Near(Division.Left, 42.0, 1.0e-9),
          "and the point is SNAPPED onto it, so the marker cannot float beside the curve it cuts");

    Claim(EvaluateWorldCut(Sketch, Spine, { 42.0, 0.0, 3.0 }, Division) == OperationVerdict::PointNotOnCurve,
          "and a probe genuinely off the curve is refused rather than cut at the nearest guess");

    // ⑥ AND IT REFUSES AT THE ENDS, where a cut would make a zero-length piece.
    Claim(EvaluateWorldCut(Sketch, Spine, { 0.0, 0.0, 0.0 }, Division) == OperationVerdict::PointAtEnd,
          "cutting at the very start is refused, so no marker promises it");

    // ⑦ THE SESSION CARRIES ALL OF IT, which is what the renderer reads.
    SketchOperationSession Trimming;
    Trimming.Manner = OperationManner::Trim;
    AdvanceSketchOperationSession(Sketch, NoChain, Ground,
                                  { { 50.0, 0.0, 0.0 }, false, false, false, 12.0 }, Trimming);
    Claim(Trimming.Preview == OperationVerdict::Produced, "the session previews the trim");
    Claim(Near(Trimming.DepartingFrom.Left, 30.0, 1.0e-9) &&
          Near(Trimming.DepartingTo.Left, 70.0, 1.0e-9),
          "and publishes the doomed span for the renderer to draw in red");

    // ⑧ HOVERING AN UNCROSSED CURVE ARMS THE WHOLE-EDGE TRIM. The piece under the pointer is the whole
    //    edge, so the session previews it end to end and publishes that span for the renderer to draw --
    //    the artist sees the entire edge lit before the click takes it.
    SketchOperationSession WholeEdge;
    WholeEdge.Manner = OperationManner::Trim;
    AdvanceSketchOperationSession(Lone, NoChain, Ground,
                                  { { 50.0, 0.0, 0.0 }, false, false, false, 12.0 }, WholeEdge);
    Claim(WholeEdge.Preview == OperationVerdict::Produced,
          "hovering an uncrossed edge arms the trim -- the whole edge is the piece that would go");
    Claim(Near(WholeEdge.DepartingFrom.Left, 0.0, 1.0e-9) &&
          Near(WholeEdge.DepartingTo.Left, 100.0, 1.0e-9),
          "and the span it publishes runs the full length of the edge");

    // ⑨ 🔴 THE PROBE IS SNAPPED ONTO THE CURVE, AND THIS IS WHAT ACTUALLY KILLED TRIM AND CUT IN THE APP.
    //    Reaching a curve is deliberately generous -- twelve PIXELS, which at metre scale is a long way
    //    in world units -- but both operations refuse a probe more than `OnCurveTolerance` (1e-3) off the
    //    line. So the gesture named a curve, armed on it, and was then refused by the very operation it
    //    had armed. The artist clicks a line, the tool clearly sees it, and nothing happens.
    SketchOperationSession Offline;
    Offline.Manner = OperationManner::Trim;
    AdvanceSketchOperationSession(Sketch, NoChain, Ground,
                                  { { 50.0, 0.0, 0.5 }, false, false, false, 12.0 }, Offline);
    Claim(Offline.Target.IssuedIndex == Spine.IssuedIndex,
          "a probe half a unit off the line still reaches the curve, as it should");
    Claim(Near(Offline.Probe.Forward, 0.0, 1.0e-9),
          "and the probe is SNAPPED onto it, closing the gap between what may be reached and what is accepted");
    Claim(Offline.Preview == OperationVerdict::Produced,
          "so the trim previews instead of being refused for being off the line");

    AdvanceSketchOperationSession(Sketch, NoChain, Ground,
                                  { { 50.0, 0.0, 0.5 }, false, false, true, 12.0 }, Offline);
    {
        WorldSketchStructure Copy = Sketch;
        std::vector<WorldCurveName> Produced;
        Claim(PerformSketchOperation(Copy, NoChain, Ground, Offline, Produced)
                  == OperationVerdict::Produced,
              "and performing it succeeds, where the raw pointer position was refused outright");
    }

    SketchOperationSession Cutting;
    Cutting.Manner = OperationManner::Cut;
    AdvanceSketchOperationSession(Sketch, NoChain, Ground,
                                  { { 42.0, 0.0, 1.0e-4 }, false, false, false, 12.0 }, Cutting);
    Claim(Near(Cutting.Division.Left, 42.0, 1.0e-9) && Near(Cutting.Division.Forward, 0.0, 1.0e-9),
          "and Cut publishes the division point, snapped onto the curve");
}

} // namespace

int main()
{
    std::printf("SketchOperationProof -- cut, trim, extend, offset and fill, executed\n");

    ProveCutKeepsEverything();
    ProveCutAtCrossings();
    ProveTrimRemovesThePiece();
    ProveExtendMeetsTheNearest();
    ProveOffsetHoldsItsDistance();
    ProveOffsetLimit();
    ProveFillAndNesting();
    ProveTheGestures();
    ProveTheReachAndTheFillSubject();
    ProveThePreviews();

    std::printf("\n%u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
