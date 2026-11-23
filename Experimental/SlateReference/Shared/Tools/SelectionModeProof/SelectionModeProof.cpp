//============================================================================================================================================
//                                                        SELECTIONMODEPROOF.CPP
//============================================================================================================================================
// ⭐ THE SELECT TOOL MUST PICK THE KIND OF ELEMENT THE ARTIST ASKED FOR.
//
// 🔴 It picked whatever won a fixed priority — point, then control, then curve — so reaching for an EDGE
//    with a vertex sitting anywhere near it returned the vertex instead, and whether the artist got what
//    they meant depended on how far the view happened to be zoomed. Measured on a line with a probe three
//    units from an endpoint and one unit from the edge through it, the unrestricted search returns the
//    Point every time.
//
// 🔴 A MODE THAT MERELY PREFERS ITS ELEMENT IS THE SAME DEFECT WEARING A HAT. If `Vertex` fell back to a
//    curve whenever no vertex was in range, the artist would still be guessing which kind they were about
//    to get. Claim ③ is the one that matters: a miss must be a miss.

#include "SlateWorkspace/Discipline/RecordDeclaration/Api/RecordDeclaration.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <sstream>
#include <fstream>

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

// 📝 The catalogue claims read the source text: "is this call ordered before that one" is a question
//    about the wiring, and the wiring is what was broken while every unit passed.
std::string ReadWhole(const char* Path)
{
    std::ifstream Stream(Path);
    if (!Stream)
        return std::string();

    std::ostringstream Gathered;
    Gathered << Stream.rdbuf();
    return Gathered.str();
}

void Require(bool Held, const char* Naming)
{
    ++Claims;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAILED  %s\n", Naming);
}

}   // namespace

int main()
{
    using namespace Slate;

    std::printf("[SelectionModeProof]\n");

    SketchStructure Sketch;
    WorkspaceRecordStructure Records;
    WorkspaceNameIndex Naming;

    Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
    const SketchCurveName Line = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 200.0, 0.0, 0.0 });
    static_cast<void>(DeclareWorkspaceCurve(Naming, Records, Line));

    // ① THE UNRESTRICTED SEARCH IS BIASED, AND THAT IS WHY THE MODE EXISTS. Three units from an
    //    endpoint and one unit from the edge — nearer the EDGE — it still returns the point.
    {
        const SpatialPoint Probe = { 3.0, 0.0, 1.0 };
        const SketchPick Free = ResolveSketchPick(Sketch, Records, Probe, 20.0);
        Require(Free.Subject == SketchPickSubject::Point,
                "the unrestricted search returns a vertex even when the edge is nearer");
    }

    // ② EACH MODE RETURNS ITS OWN KIND, from one probe that is in range of all three.
    {
        const SpatialPoint Probe = { 3.0, 0.0, 1.0 };

        const SketchPick Vertex =
            ResolveSketchPickForElement(Sketch, Records, Probe, 20.0, SelectionElement::Vertex);
        Require(Vertex.Subject == SketchPickSubject::Point, "Vertex mode returns a vertex");

        const SketchPick Edge =
            ResolveSketchPickForElement(Sketch, Records, Probe, 20.0, SelectionElement::Edge);
        Require(Edge.Subject == SketchPickSubject::Curve,
                "Edge mode returns the EDGE, though a vertex is nearer and would have won");
        Require(Edge.Curve.Assigned(), "and names the curve it picked");

        const SketchPick Face =
            ResolveSketchPickForElement(Sketch, Records, Probe, 20.0, SelectionElement::Face);
        Require(!Face.Standing(), "Face mode refuses an open wire that encloses no region");
    }

    // ③ 🔴 A MISS IS A MISS. At the edge's midpoint, a hundred units from either endpoint, Vertex mode
    //    must return NOTHING rather than quietly handing back the edge.
    {
        const SpatialPoint Middle = { 100.0, 0.0, 2.0 };

        const SketchPick Vertex =
            ResolveSketchPickForElement(Sketch, Records, Middle, 20.0, SelectionElement::Vertex);
        Require(!Vertex.Standing(),
                "Vertex mode picks NOTHING where there is no vertex, rather than falling back");

        const SketchPick Edge =
            ResolveSketchPickForElement(Sketch, Records, Middle, 20.0, SelectionElement::Edge);
        Require(Edge.Subject == SketchPickSubject::Curve,
                "while Edge mode picks the edge from the same probe");
    }

    // ④ TOLERANCE STILL BOUNDS THE SEARCH. A mode does not make a distant element reachable.
    {
        const SpatialPoint Distant = { 3.0, 0.0, 500.0 };
        for (const SelectionElement Element : { SelectionElement::Vertex,
                                                SelectionElement::Edge,
                                                SelectionElement::Face })
        {
            const SketchPick Missed =
                ResolveSketchPickForElement(Sketch, Records, Distant, 20.0, Element);
            Require(!Missed.Standing(), "nothing within tolerance picks nothing, in every mode");
        }
    }

    // ⑤ FACE AND OBJECT MUST PICK THE WHOLE CLOSED PROFILE THROUGH ITS AREA, not only by grazing one
    //    of its edges. A probe in the middle of a closed region is the ordinary way an artist reaches it.
    {
        SketchStructure ProfileSketch;
        WorkspaceRecordStructure ProfileRecords;
        WorkspaceNameIndex ProfileNaming;

        ProfileSketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
        const Deliver<ProfileNameInFeature> Square =
            ProfileSketch.DeclareRegularPolygon({ 0.0, 0.0, 0.0 }, 40.0, 4u, SpatialDirection{ 1.0, 0.0, 0.0 });
        Require(Square.Resolved, "a closed profile can be declared for face picking");
        if (Square.Resolved)
        {
            const WorkspaceRecordName Shape =
                DeclareWorkspaceProfile(ProfileNaming, ProfileRecords, Square.Resolve());
            const SpatialPoint Middle = { 0.0, 0.0, 0.0 };

            const SketchPick Face =
                ResolveSketchPickForElement(ProfileSketch, ProfileRecords, Middle, 100.0, SelectionElement::Face);
            Require(Face.Subject == SketchPickSubject::Record,
                    "Face mode reaches the whole closed profile from its interior");
            Require(Face.Record.IssuedIndex == Shape.IssuedIndex,
                    "and names the profile record rather than one edge of it");

            const SketchPick Object =
                ResolveSketchPickForElement(ProfileSketch, ProfileRecords, Middle, 100.0, SelectionElement::Object);
            Require(Object.Subject == SketchPickSubject::Record,
                    "Object mode also reaches the whole profile from its interior");
            Require(Object.Record.IssuedIndex == Shape.IssuedIndex,
                    "and keeps the same profile record identity");
        }
    }

    // ⑤b PROFILE-ONLY SHAPES STILL OFFER VERTEX AND EDGE PICKS. A triangle drawn as one closed profile
    //     with no child edge rows must still let its corner highlight as a vertex and its side as an edge.
    {
        SketchStructure TriangleSketch;
        WorkspaceRecordStructure TriangleRecords;
        WorkspaceNameIndex TriangleNaming;

        TriangleSketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
        const Deliver<ProfileNameInFeature> Triangle =
            TriangleSketch.DeclareRegularPolygon({ 0.0, 0.0, 0.0 }, 40.0, 3u, SpatialDirection{ 1.0, 0.0, 0.0 });
        Require(Triangle.Resolved, "a closed triangle can be declared for element picking");
        if (Triangle.Resolved)
        {
            const WorkspaceRecordName Shape =
                DeclareWorkspaceProfile(TriangleNaming, TriangleRecords, Triangle.Resolve());
            const ProfileSpecification& Held = TriangleSketch.Profiles()[Triangle.Resolve().IssuedIndex - 1u];
            const SketchCurveName FirstEdge = { Held.HeldLoops()[0].Traversal[0].TraversedCurve.IssuedIndex };

            std::vector<SketchPointPlacement> Corners;
            Require(ResolveSketchPoints(TriangleSketch, FirstEdge, Corners) && Corners.size() == 2u,
                    "the triangle exposes the two corners of one edge");
            if (Corners.size() == 2u)
            {
                const SpatialPoint Corner = Corners[0].Position;
                const SpatialPoint Midpoint = { (Corners[0].Position.Left + Corners[1].Position.Left) * 0.5,
                                                0.0,
                                                (Corners[0].Position.Forward + Corners[1].Position.Forward) * 0.5 };

                const SketchPick Vertex = ResolveSketchPickForElement(
                    TriangleSketch, TriangleRecords, Corner, 5.0, SelectionElement::Vertex);
                Require(Vertex.Subject == SketchPickSubject::Point,
                        "Vertex mode reaches a triangle corner even when only the profile has a record");
                Require(Vertex.Record.IssuedIndex == Shape.IssuedIndex,
                        "and that corner carries the owning profile record");

                const SketchPick Edge = ResolveSketchPickForElement(
                    TriangleSketch, TriangleRecords, Midpoint, 5.0, SelectionElement::Edge);
                Require(Edge.Subject == SketchPickSubject::Curve,
                        "Edge mode reaches the existing triangle side rather than missing it");
                Require(Edge.Record.IssuedIndex == Shape.IssuedIndex,
                        "and the side also resolves through the profile record");
            }
        }
    }

    // ⑤ THE OPTIONS ADMIT ONE KIND AND REFUSE THE REST — the declaration the widget writes into.
    {
        SelectionOptions Options;
        Options.Element = SelectionElement::Edge;
        Require(Options.Admits(SelectionElement::Edge), "the standing element is admitted");
        Require(!Options.Admits(SelectionElement::Vertex), "and every other kind is refused");
        Require(!Options.Admits(SelectionElement::Face), "including a face");

        Options.Tolerance = 5000.0f;
        Require(Options.ResolvedTolerance() == SelectionOptions::ToleranceMaximum,
                "a tolerance beyond the range is held at the bound");
        Options.Tolerance = -1.0f;
        Require(Options.ResolvedTolerance() == SelectionOptions::ToleranceMinimum,
                "and a negative one cannot disable picking");
    }

    // ⑥ 🔴 THE TOLERANCE IS PIXELS, AND THE CONVERSION IS THE PROJECTION'S OWN, INVERTED. A world-unit
    //    tolerance is wrong at both ends of the zoom range: the same number that picks one vertex when
    //    zoomed in swallows a dozen when zoomed out.
    {
        ViewportStanding View;
        View.OrthoScale = 4.0;   // [px/unit] - what the projection multiplies by

        // 8 px at 4 px per unit is 2 units. Exactly, with no fitted constant.
        Require(std::fabs(ResolvePickTolerance(View, false, 8.0, 900.0) - 2.0) < 1.0e-9,
                "an orthographic reach is the pixel radius divided by pixels-per-unit");

        // 🔴 THE POINT OF THE WHOLE EXERCISE: zoom in tenfold and the same 8 px reaches a tenth as far
        //    into the world, so it stays 8 px on the screen the artist is looking at.
        View.OrthoScale = 40.0;
        Require(std::fabs(ResolvePickTolerance(View, false, 8.0, 900.0) - 0.2) < 1.0e-9,
                "and shrinks in world units exactly as the view zooms in");

        // The perspective arm inverts `ProjectThroughFrame`'s focal length, so a point ONE tolerance
        // away from the pointer projects to exactly the stated pixel radius. Verified by projecting.
        View.OrthoScale = 1.0;
        View.Distance = 240.0;
        View.FieldOfViewDegrees = 60.0;
        const double Reach = ResolvePickTolerance(View, true, 8.0, 900.0);
        const double TanHalf = std::tan(60.0 * 0.5 * 3.14159265358979323846 / 180.0);
        const double Pixels = Reach * ((900.0 * 0.5) / TanHalf) / 240.0;
        Require(std::fabs(Pixels - 8.0) < 1.0e-9,
                "a perspective reach projects back to the stated pixel radius at the focus");

        // ⚠️ And a nonsense reach cannot disable picking or divide by nothing.
        Require(ResolvePickTolerance(View, false, 0.0, 900.0) > 0.0,
                "a reach of nothing still reaches something");
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    //  ⑦ THE FIVE MODES, AND WHAT EACH ONE REFUSES.
    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    {
        Require(static_cast<std::uint32_t>(SelectionElement::ElementCount) == 5u,
                "five modes stand: Vertex, Edge, Face, Object, Free");

        // 🔴 EVERY MODE MUST BE NAMED. A mode with an empty caption draws a blank segment the artist
        //    cannot tell from a disabled one.
        for (std::uint32_t Index = 0u; Index < 5u; ++Index)
        {
            const char* const Named = SelectionElementText(static_cast<SelectionElement>(Index));
            Require(Named != nullptr && Named[0] != '\0', "each mode carries a caption");
        }

        // 🔴 EXACTNESS IS THE WHOLE POINT of the four named modes: each admits its own kind and refuses
        //    every other, which is what the artist asked for when reaching for an edge kept giving them
        //    a vertex.
        SelectionOptions Options;
        Options.Element = SelectionElement::Edge;
        Require(Options.Admits(SelectionElement::Edge), "Edge admits an edge");
        Require(!Options.Admits(SelectionElement::Vertex), "and refuses a vertex, however near");
        Require(!Options.Admits(SelectionElement::Face), "and refuses a face");

        Options.Element = SelectionElement::Object;
        Require(Options.Admits(SelectionElement::Object), "Object admits an object");
        Require(!Options.Admits(SelectionElement::Edge), "and refuses the edge it was reached through");

        // 🔴 FREE IS THE ONE THAT ADMITS EVERYTHING, and it is a mode the artist CHOOSES rather than the
        //    behaviour they are stuck with. That distinction is the entire reason it is allowed back.
        Options.Element = SelectionElement::Free;
        Require(Options.Admits(SelectionElement::Vertex)
             && Options.Admits(SelectionElement::Edge)
             && Options.Admits(SelectionElement::Face)
             && Options.Admits(SelectionElement::Object),
                "Free admits every kind, which is what makes it Free");
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    //  ⑧ THE CATALOGUE RESET. 🔴 ONLY THE TWO REQUESTED BANDS SURVIVE, AND NEITHER IS GATED.
    //
    //  The current reset keeps the draw band and the parked operation band, renames them to
    //  `2DPrimitives` and `Operations`, and removes the old dimension/selection gating from the
    //  presentation layer. Operations stay visible as a visual-only shell while their algorithms are
    //  retired; drawing tools stay available exactly as before.
    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    {
        const std::string Host = ReadWhole("Engine/Application/EditorHost/Source/EditorHost.cpp");
        Require(!Host.empty(), "the host source is readable");

        Require(Host.find("ParametricToolsApplied.ActiveDimension =") != std::string::npos,
                "the host states the active dimension, or every Edge tool stays hidden forever");

        const std::size_t StatedAt = Host.find("ParametricToolsApplied.ActiveDimension =");
        const std::size_t RecordAt = Host.find("ParametricTools.Record(");
        Require(StatedAt != std::string::npos && RecordAt != std::string::npos && StatedAt < RecordAt,
                "and states it BEFORE the panel records, or the panel filters on last frame's answer");

        Require(Host.find("ParametricToolsApplied.WorkplaneActivation = Sketch.Declared()") != std::string::npos,
                "the workplane standing is the sketch's, not a preset button's");

        // 🔴 Q MUST ACTUALLY REACH SELECT, AND SOURCE ORDER IS THE WHOLE DEFECT. The catalogue's tiles
        //    are read through the SHARED `ParametricInteraction`, whose release latch is retired by its
        //    `Advance` -- and that `Advance` sits near the end of the frame, hundreds of lines BELOW the
        //    Q handler. A tile still under the pointer therefore carries a release into the next frame,
        //    where the panel re-reads it and re-assigns `ActiveSubject` from the tile, putting the
        //    operation straight back after the key had already cleared it. The reported symptom was that
        //    operations would not return to Select. Only dropping the grab in the handler settles it, and
        //    only source order can say so -- no headless run of the sessions can see this.
        const std::size_t ChooseAt   = Host.find("KeySubject::ChooseSelect");
        const std::size_t AbandonAt  = Host.find("ParametricInteraction.Abandon()");
        const std::size_t AdvanceAt  = Host.find("ParametricInteraction.Advance(");
        Require(ChooseAt != std::string::npos, "the host answers Q at all");
        Require(AbandonAt != std::string::npos && ChooseAt < AbandonAt,
                "and Q drops the catalogue's standing grab, or a hovered tile re-asserts the tool");
        Require(AdvanceAt != std::string::npos && AbandonAt < AdvanceAt,
                "and drops it while the release still stands, before the index retires it");

        // 🔴 THE DRIVER'S MEMORY IS RETIRED WITH THE SESSIONS. `Prepared` is the only record of which
        //    tool the state belongs to, and the driver never runs while Select stands -- so cancelling
        //    the sessions without clearing it leaves it naming the abandoned operation. Re-entering that
        //    same tool then finds `ActiveTool == Prepared`, skips the tool-change cancel, and resumes a
        //    gesture the artist had already dismissed with Q.
        const std::size_t PreparedAt = Host.find("SketchOperations.Prepared = ParametricToolSubject::Select");
        Require(PreparedAt != std::string::npos && ChooseAt < PreparedAt,
                "and Q retires the driver's prepared tool, so re-entering it starts clean");

        // 🔴 THE SOURCE OF THE DIMENSION IS THE STANDING PICK. Read from the widget's mode instead and
        //    the catalogue would offer edge operations because the artist was LOOKING at Edge mode,
        //    rather than because an edge is actually held.
        Require(Host.find("SketchPick& Held = SketchSemanticSelection") != std::string::npos,
                "and the dimension is read from what is selected, not from what could be");

        // 🔴 THE OPERATIONS MUST BE DRIVEN AFTER THE FRAME'S CAMERA IS SETTLED, and this is a fact about
        //    SOURCE ORDER that no headless proof of the sessions can reach. `SceneCamera` is assigned
        //    near the top of the leaf and then REASSIGNED further down; on an orthographic leaf the
        //    second value is an ORBIT camera resolved from the sketch basis, which is a genuinely
        //    different camera from the free one above it -- forward vectors disagreeing by a dot of
        //    -0.42, eyes 100 units apart. Driving the operations against the earlier value un-projected
        //    the pointer through a camera the artist was not looking through, so the probe landed
        //    somewhere else on the workplane, reached no curve and no corner, and every operation
        //    silently did nothing. In perspective the two agree exactly, which is what made it look
        //    intermittent. The sessions were correct throughout; only the order was wrong.
        const std::size_t OrbitAt     = Host.find(": ResolveOrbitCamera(SketchBasis, SketchView, false);");
        const std::size_t OperationAt = Host.find("DriveSketchOperations(");
        const std::size_t AnnotateAt  = Host.find("DriveAnnotations(");

        Require(OrbitAt != std::string::npos, "the host settles the leaf camera before drawing");
        Require(OperationAt != std::string::npos && OrbitAt < OperationAt,
                "and drives the operations AFTER it, or they aim through the wrong camera");
        Require(AnnotateAt != std::string::npos && OrbitAt < AnnotateAt,
                "and the annotations likewise, since a dimension is placed through the same projection");

        // 🔴 CUT HAD NO TILE. The subject, the geometry and the apply arm all existed; no band listed it,
        //    so none of it was reachable.
        const std::string Panel =
            ReadWhole("Engine/SlateUI/Interface/ParametricTools/Source/ParametricToolsPanel.cpp");
        Require(!Panel.empty(), "the catalogue source is readable");
        Require(Panel.find("{ \"Cut\"") != std::string::npos,
                "Cut has a tile, not just an enumeration member");
        Require(Panel.find("ParametricToolSubject::Cut") != std::string::npos,
                "and the band maps its index onto the Cut subject");

        // 🔴 THE PRESENTATION FILTERS ARE NOW NEUTRALISED. The helpers still exist, but both answer
        //    `false` so the reduced catalogue cannot hide or dim the two surviving bands while the
        //    operation workflow is intentionally parked.
        const std::size_t GateAt = Panel.find("bool Gated(");
        const std::size_t HideAt = Panel.find("bool Hidden(");
        Require(GateAt != std::string::npos && HideAt != std::string::npos,
                "the catalogue states both filters");

        const std::string GateBody = Panel.substr(GateAt, HideAt > GateAt ? HideAt - GateAt : 0u);
        const std::size_t HideEnds = Panel.find("\n}", HideAt);
        const std::string HideBody = Panel.substr(HideAt, HideEnds > HideAt ? HideEnds - HideAt : 0u);

        // 🔴 FOUR BANDS NOW, AND THIS CLAIM WAS CHANGED DELIBERATELY -- TWICE. It first pinned
        //    `BandCount = 2u` while the catalogue was intentionally a drawing band and an operations
        //    band; the annotation tiles were then asked for and built, making three. They have now been
        //    split into dimensions and constraints, because a dimension states what a size IS and may
        //    drive the geometry, while a constraint states a relation that must HOLD and drives nothing.
        //    A proof demanding the old count would be enforcing a decision that has been superseded.
        //    What the claim protects is unchanged: no band has been dropped to make room.
        Require(Panel.find("BandCount = 4u") != std::string::npos,
                "the catalogue carries the drawing, operations, dimension and constraint bands");
        Require(Panel.find("2DPrimitives") != std::string::npos
             && Panel.find("Operations") != std::string::npos
             && Panel.find("\"Dimensions\"") != std::string::npos
             && Panel.find("\"Constraints\"") != std::string::npos,
                "named 2DPrimitives, Operations, Dimensions and Constraints");

        // 🔴 A BAND THAT NOTHING INDEXES INTO IS UNREACHABLE, AND COMPILES PERFECTLY. The annotation
        //    tiles were defined in the catalogue for exactly that reason and could not be chosen,
        //    because the bands array listed two entries. Listing them is only half the fix:
        //    `ToolSubjectOf` also had no case for the band, so every tile would have reported `Select`.
        Require(Panel.find("DimensionTools") != std::string::npos
             && Panel.find("ConstraintTools") != std::string::npos,
                "the annotation tiles are listed by bands rather than defined and orphaned");

        // 🔴 THE TWO KINDS ARE SEPARATELY REACHABLE. Splitting the table but leaving one band pointing at
        //    it would put the heading in the interface and still hand the artist a single list.
        Require(Panel.find("case 3u:") != std::string::npos
             && Panel.find("ParametricToolSubject::HorizontalConstraint") != std::string::npos,
                "and the constraint band maps its own indices rather than sharing the dimensions'");
        Require(Panel.find("ParametricToolSubject::LinearDimension") != std::string::npos
             && Panel.find("ParametricToolSubject::RadialDimension") != std::string::npos,
                "and the band maps its indices onto dimension subjects rather than falling back to Select");
        Require(GateBody.find("return false;") != std::string::npos,
                "operations are no longer gated while the menu is being reduced to a visual-only shell");
        Require(HideBody.find("return false;") != std::string::npos,
                "and no band is hidden out from under the artist while the workflow is reset");
    }

    std::printf("[SelectionModeProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
