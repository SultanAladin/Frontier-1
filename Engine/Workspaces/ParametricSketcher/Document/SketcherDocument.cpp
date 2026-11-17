/*====================================================================================================================================
                                                         SOLIDARCDOCUMENT.CPP
====================================================================================================================================*/
// 🧩 Sketcher document: construct tiles as sketch + sweep pairs, atomic commands, undo / redo, selection upkeep

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"

#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/SurfaceAlgebra.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/SectionLoft.h"
#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherCurveImport.h"

#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Blend/RollingBallGeometry.h"
#include "Engine/GeometrySpace/Parametric/Exchange/StepExchange.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceChange.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MinimumDistance.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchCatalogue.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSection.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveNetwork.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <limits>
#include <map>
#include <queue>
#include <set>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

using Kind = SketchConstraintKind;
constexpr double Pi = std::numbers::pi;

//------------------------------------------------------------------------------------------------------------------------
//                                                    AUTHORING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] double ElapsedMs(std::chrono::steady_clock::time_point Start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
}

[[nodiscard]] double ParameterNamed(const FeatureTree& Tree, const std::string& Name)
{
    for (const FeatureParameter& Parameter : Tree.Parameters) if (Parameter.Name == Name) return Parameter.Value;
    return 0.0;
}

// 📝 Labels are unique for the life of the document: the next ordinal follows the largest "#n" already used.
[[nodiscard]] int NextStepOrdinal(const FeatureTree& Tree)
{
    int Largest = 0;                                                                         // [-] - Largest ordinal in use
    for (const FeatureStep& Step : Tree.Steps)
    {
        const size_t Mark = Step.Label.rfind('#');
        if (Mark == std::string::npos) continue;
        Largest = std::max(Largest, std::atoi(Step.Label.c_str() + Mark + 1));
    }
    for (const FeatureModifier& Modifier : Tree.Modifiers)
    {
        const size_t Mark = Modifier.Label.rfind('#');
        if (Mark == std::string::npos) continue;
        Largest = std::max(Largest, std::atoi(Modifier.Label.c_str() + Mark + 1));
    }
    return Largest + 1;
}

// Next free caption for a tile ("Block1", "Block2", …) over every caption in use.
[[nodiscard]] std::string NextCaption(const SketcherDocument& Document, const std::string& Stem)
{
    for (int Ordinal = 1;; ++Ordinal)
    {
        const std::string Caption = Stem + std::to_string(Ordinal);
        const bool UsedCondition  = std::any_of(Document.Captions.begin(), Document.Captions.end(),
                                                [&](const auto& Entry) { return Entry.second == Caption; });
        if (!UsedCondition) return Caption;
    }
}

struct TileAuthoring
{
    FeatureTree&       Tree;
    const std::string& Caption;

    [[nodiscard]] std::string Named(const std::string& Suffix) const { return Caption + "." + Suffix; }

    void Parameter(const std::string& Suffix, double Value, double Low, double High, const std::string& Description)
    {
        Tree.Parameters.push_back({ Named(Suffix), Value, std::min(Low, Value), std::max(High, Value), Description });
    }

    // A sketch dimension bound to a tile parameter (literal = the parameter's current value).
    void Bound(SketchModel& Sketch, Kind Constraint, int First, int Second, const std::string& Suffix)
    {
        const int Index = AppendSketchConstraint(Sketch, Constraint, First, Second, -1, ParameterNamed(Tree, Named(Suffix)), Suffix);
        Sketch.Constraints[Index].Parameter = Named(Suffix);
    }

    FeatureStep& Step(FeatureStepKind StepKind, const std::string& Label)
    {
        FeatureStep Step;
        Step.Kind  = StepKind;
        Step.Label = Label;
        Tree.Steps.push_back(std::move(Step));
        return Tree.Steps.back();
    }
};

// 📝 Sketch planes: XY (normal +z, elevation = Z) for extrusions, XZ (x → x, sketch y → z, normal −y, elevation = −Y)
//    for revolutions about a vertical axis.
[[nodiscard]] OrthonormalFrame FloorPlane() { return {}; }

[[nodiscard]] OrthonormalFrame UprightPlane()
{
    OrthonormalFrame Plane;
    Plane.XAxis = { 1.0, 0.0, 0.0 };
    Plane.YAxis = { 0.0, 0.0, 1.0 };
    Plane.ZAxis = { 0.0, -1.0, 0.0 };
    return Plane;
}

// ① parameters ② sketch with a fixed origin and every placement bound ③ sweep step. Returns the caption.
std::string AuthorTile(SketcherDocument& Document, const SketcherConstructRequest& Request, FeatureCombination Combination)
{
    FeatureTree&      Tree    = Document.Tree;
    const char*       Stem    = SketcherConstructLabel(Request.Kind);
    const std::string Caption = NextCaption(Document, Stem);
    const int         Ordinal = NextStepOrdinal(Tree);
    const std::string SketchLabel = "Sketch#" + std::to_string(Ordinal);
    TileAuthoring     Author{ Tree, Caption };
    const Vector3     Spot    = Request.Position;
    const size_t      FirstStep      = Tree.Steps.size();
    const size_t      FirstParameter = Tree.Parameters.size();

    switch (Request.Kind)
    {
    case SketcherConstructKind::Block:
    {
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, "corner x");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, "corner y");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, "corner z (sketch elevation)");
        Author.Parameter("Width", Request.Width, 0.1, 10.0, "extent along x");
        Author.Parameter("Depth", Request.Depth, 0.1, 10.0, "extent along y");
        Author.Parameter("Height", Request.Height, 0.1, 10.0, "extent along z (extrude)");
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = FloorPlane();
        Section.Elevation    = ParameterValue(Author.Named("Z"));
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const SketchRectangle Rectangle = AppendSketchRectangle(Sketch, { Spot.X, Spot.Y }, { Spot.X + Request.Width, Spot.Y + Request.Depth }, 1);
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, Rectangle.FirstPoint, "X");
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, Rectangle.FirstPoint, "Y");
        Author.Bound(Sketch, Kind::HorizontalDistance, Rectangle.FirstPoint, Rectangle.FirstPoint + 1, "Width");
        Author.Bound(Sketch, Kind::VerticalDistance, Rectangle.FirstPoint, Rectangle.FirstPoint + 3, "Depth");
        FeatureStep& Sweep = Author.Step(FeatureStepKind::Extrude, "Extrude#" + std::to_string(Ordinal + 1));
        Sweep.Profile      = SketchLabel;
        Sweep.Extent       = ParameterValue(Author.Named("Height"));
        Sweep.Combination  = Combination;
        Sweep.Target       = Request.Target;
        break;
    }
    case SketcherConstructKind::Cylinder:
    {
        // 📝 Axis z: floor sketch at elevation Z, circle at (X, Y). Axis x: YZ sketch (normal +x) at elevation X, circle at
        //    (Y, Z). Axis y: upright sketch (normal −y) at elevation −Y, circle at (X, Z), extent negated so Height runs +y.
        //    Position is the centre of the base circle in every case (wheel arches: Axis x at the wheel centre).
        const int   Axis      = std::clamp(Request.Axis, 0, 2);
        const char* Along[3]  = { "base x (sketch elevation)", "base y (sketch elevation)", "base z (sketch elevation)" };
        const char* Extents[3] = { "extent along +x (extrude)", "extent along +y (extrude)", "extent along +z (extrude)" };
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, Axis == 0 ? Along[0] : "axis x");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, Axis == 1 ? Along[1] : "axis y");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, Axis == 2 ? Along[2] : "axis z");
        Author.Parameter("Radius", Request.Radius, 0.05, 5.0, "radius");
        Author.Parameter("Height", Request.Height, 0.1, 10.0, Extents[Axis]);
        const char* Elevation = Axis == 0 ? "X" : Axis == 1 ? "Y" : "Z";
        const char* First     = Axis == 0 ? "Y" : "X";
        const char* Second    = Axis == 2 ? "Y" : "Z";
        const GeometrySpace::Parametric::Vector2 Place = Axis == 0 ? GeometrySpace::Parametric::Vector2{ Spot.Y, Spot.Z } : Axis == 1 ? GeometrySpace::Parametric::Vector2{ Spot.X, Spot.Z } : GeometrySpace::Parametric::Vector2{ Spot.X, Spot.Y };
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = Axis == 0 ? SketcherSketchPlane(2) : Axis == 1 ? UprightPlane() : FloorPlane();
        Section.Elevation    = ParameterValue(Author.Named(Elevation), Axis == 1 ? -1.0 : 1.0);
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int Centre     = AppendSketchPoint(Sketch, Place);
        const int Circle     = AppendSketchCircle(Sketch, Centre, Request.Radius, 1);
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, Centre, First);
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, Centre, Second);
        Author.Bound(Sketch, Kind::Radius, Circle, -1, "Radius");
        FeatureStep& Sweep = Author.Step(FeatureStepKind::Extrude, "Extrude#" + std::to_string(Ordinal + 1));
        Sweep.Profile      = SketchLabel;
        Sweep.Extent       = ParameterValue(Author.Named("Height"), Axis == 1 ? -1.0 : 1.0);
        Sweep.Combination  = Combination;
        Sweep.Target       = Request.Target;
        break;
    }
    case SketcherConstructKind::Sphere:
    {
        // 📝 Half disc: closing line on the axis (a profile line, not construction) + arc through +x, revolved 2π.
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, "centre x");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, "centre y (sketch elevation)");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, "centre z");
        Author.Parameter("Radius", Request.Radius, 0.05, 5.0, "radius");
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = UprightPlane();
        Section.Elevation    = ParameterValue(Author.Named("Y"), -1.0);
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int Centre     = AppendSketchPoint(Sketch, { Spot.X, Spot.Z });
        const int South      = AppendSketchPoint(Sketch, { Spot.X, Spot.Z - Request.Radius });
        const int North      = AppendSketchPoint(Sketch, { Spot.X, Spot.Z + Request.Radius });
        AppendSketchLine(Sketch, South, North, 1);
        AppendSketchArc(Sketch, Centre, South, North, 2);
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, Centre, "X");
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, Centre, "Z");
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, Centre, South, -1, 0.0, "SouthOnAxis");
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, Centre, North, -1, 0.0, "NorthOnAxis");
        Author.Bound(Sketch, Kind::VerticalDistance, Centre, North, "Radius");
        FeatureStep& Sweep    = Author.Step(FeatureStepKind::Revolve, "Revolve#" + std::to_string(Ordinal + 1));
        Sweep.Profile         = SketchLabel;
        Sweep.Combination     = Combination;
        Sweep.Target          = Request.Target;
        Sweep.AxisIdentifier  = 1;
        Sweep.Angle           = LiteralValue(2.0 * Pi);
        break;
    }
    case SketcherConstructKind::Torus:
    {
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, "axis x");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, "axis y (sketch elevation)");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, "centre z");
        Author.Parameter("Major", Request.Major, 0.2, 5.0, "axis to tube centre");
        Author.Parameter("Minor", Request.Minor, 0.05, 2.0, "tube radius");
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = UprightPlane();
        Section.Elevation    = ParameterValue(Author.Named("Y"), -1.0);
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int AxisStart  = AppendSketchPoint(Sketch, { Spot.X, Spot.Z });
        const int AxisEnd    = AppendSketchPoint(Sketch, { Spot.X, Spot.Z + 1.0 });
        const int Centre     = AppendSketchPoint(Sketch, { Spot.X + Request.Major, Spot.Z });
        AppendSketchLine(Sketch, AxisStart, AxisEnd, 1, true);
        const int Circle     = AppendSketchCircle(Sketch, Centre, Request.Minor, 2);
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, AxisStart, "X");
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, AxisStart, "Z");
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, AxisStart, AxisEnd, -1, 0.0, "AxisUpright");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, AxisStart, AxisEnd, -1, 1.0, "AxisLength");
        Author.Bound(Sketch, Kind::HorizontalDistance, AxisStart, Centre, "Major");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, AxisStart, Centre, -1, 0.0, "Level");
        Author.Bound(Sketch, Kind::Radius, Circle, -1, "Minor");
        FeatureStep& Sweep    = Author.Step(FeatureStepKind::Revolve, "Revolve#" + std::to_string(Ordinal + 1));
        Sweep.Profile         = SketchLabel;
        Sweep.Combination     = Combination;
        Sweep.Target          = Request.Target;
        Sweep.AxisIdentifier  = 1;
        Sweep.Angle           = LiteralValue(2.0 * Pi);
        break;
    }
    case SketcherConstructKind::Cone:
    {
        // 📝 Right triangle on XZ: the leg on the axis is a profile line (as the sphere's diameter) and the revolution axis;
        //    the apex sits Height above the base centre (negative: the cone points down).
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, "axis x");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, "axis y (sketch elevation)");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, "base z");
        Author.Parameter("Radius", Request.Radius, 0.05, 5.0, "base radius");
        Author.Parameter("Height", Request.Height, 0.1, 10.0, "base → apex along +z");
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = UprightPlane();
        Section.Elevation    = ParameterValue(Author.Named("Y"), -1.0);
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int Base       = AppendSketchPoint(Sketch, { Spot.X, Spot.Z });
        const int Apex       = AppendSketchPoint(Sketch, { Spot.X, Spot.Z + Request.Height });
        const int Rim        = AppendSketchPoint(Sketch, { Spot.X + Request.Radius, Spot.Z });
        AppendSketchLine(Sketch, Apex, Base, 1);
        AppendSketchLine(Sketch, Base, Rim, 2);
        AppendSketchLine(Sketch, Rim, Apex, 3);
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, Base, "X");
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, Base, "Z");
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, Base, Apex, -1, 0.0, "ApexOnAxis");
        Author.Bound(Sketch, Kind::VerticalDistance, Base, Apex, "Height");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, Base, Rim, -1, 0.0, "Level");
        Author.Bound(Sketch, Kind::HorizontalDistance, Base, Rim, "Radius");
        FeatureStep& Sweep    = Author.Step(FeatureStepKind::Revolve, "Revolve#" + std::to_string(Ordinal + 1));
        Sweep.Profile         = SketchLabel;
        Sweep.Combination     = Combination;
        Sweep.Target          = Request.Target;
        Sweep.AxisIdentifier  = 1;
        Sweep.Angle           = LiteralValue(2.0 * Pi);
        break;
    }
    case SketcherConstructKind::Wedge:
    {
        // 📝 Right triangle on XZ (upright sketch, normal −y, elevation −Y) extruded by −Depth, so Depth runs along +y as
        //    the y-axis cylinder does. The high side stands at x = X; the slope falls to the floor at x = X + Width.
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, "corner x (high side)");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, "corner y (sketch elevation)");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, "corner z");
        Author.Parameter("Width", Request.Width, 0.1, 10.0, "extent along x (slope run)");
        Author.Parameter("Depth", Request.Depth, 0.1, 10.0, "extent along y (extrude)");
        Author.Parameter("Height", Request.Height, 0.1, 10.0, "height of the high side");
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = UprightPlane();
        Section.Elevation    = ParameterValue(Author.Named("Y"), -1.0);
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int Heel       = AppendSketchPoint(Sketch, { Spot.X, Spot.Z });
        const int Toe        = AppendSketchPoint(Sketch, { Spot.X + Request.Width, Spot.Z });
        const int Crest      = AppendSketchPoint(Sketch, { Spot.X, Spot.Z + Request.Height });
        AppendSketchLine(Sketch, Heel, Toe, 1);
        AppendSketchLine(Sketch, Toe, Crest, 2);
        AppendSketchLine(Sketch, Crest, Heel, 3);
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, Heel, "X");
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, Heel, "Z");
        Author.Bound(Sketch, Kind::HorizontalDistance, Heel, Toe, "Width");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, Heel, Toe, -1, 0.0, "Level");
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, Heel, Crest, -1, 0.0, "Upright");
        Author.Bound(Sketch, Kind::VerticalDistance, Heel, Crest, "Height");
        FeatureStep& Sweep = Author.Step(FeatureStepKind::Extrude, "Extrude#" + std::to_string(Ordinal + 1));
        Sweep.Profile      = SketchLabel;
        Sweep.Extent       = ParameterValue(Author.Named("Depth"), -1.0);
        Sweep.Combination  = Combination;
        Sweep.Target       = Request.Target;
        break;
    }
    case SketcherConstructKind::Prism:
    {
        // 📝 Regular n-gon on XY: every vertex on a construction circle of the bound Radius, every side equal to the first,
        //    the first vertex on a construction ray from the centre at Angle. 2n + 5 unknowns, 2n + 5 equations.
        const int    Sides  = std::clamp(Request.Sides, 3, 64);
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, "centre x");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, "centre y");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, "base z (sketch elevation)");
        Author.Parameter("Radius", Request.Radius, 0.05, 5.0, "circumradius (centre → vertex)");
        Author.Parameter("Height", Request.Height, 0.1, 10.0, "extent along +z (extrude)");
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = FloorPlane();
        Section.Elevation    = ParameterValue(Author.Named("Z"));
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int Centre     = AppendSketchPoint(Sketch, { Spot.X, Spot.Y });
        const int Ring       = AppendSketchCircle(Sketch, Centre, Request.Radius, 1, true);
        const GeometrySpace::Parametric::Vector2 Heading{ std::cos(Request.Angle), std::sin(Request.Angle) };
        const int Aim        = AppendSketchPoint(Sketch, { Spot.X + Heading.X, Spot.Y + Heading.Y });
        const int Ray        = AppendSketchLine(Sketch, Centre, Aim, 2, true);
        std::vector<int> Corners;
        for (int Index = 0; Index < Sides; ++Index)
        {
            const double θ = Request.Angle + 2.0 * Pi * Index / Sides;
            Corners.push_back(AppendSketchPoint(Sketch, { Spot.X + Request.Radius * std::cos(θ), Spot.Y + Request.Radius * std::sin(θ) }));
        }
        std::vector<int> Edges;
        for (int Index = 0; Index < Sides; ++Index) Edges.push_back(AppendSketchLine(Sketch, Corners[Index], Corners[(Index + 1) % Sides], 3 + Index));
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, Centre, "X");
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, Centre, "Y");
        Author.Bound(Sketch, Kind::Radius, Ring, -1, "Radius");
        AppendSketchConstraint(Sketch, Kind::HorizontalDistance, Centre, Aim, -1, Heading.X, "AimX");
        AppendSketchConstraint(Sketch, Kind::VerticalDistance, Centre, Aim, -1, Heading.Y, "AimY");
        AppendSketchConstraint(Sketch, Kind::PointOnLine, Corners[0], Ray, -1, 0.0, "FirstOnRay");
        for (int Index = 0; Index < Sides; ++Index)
            AppendSketchConstraint(Sketch, Kind::PointOnCircle, Corners[Index], Ring, -1, 0.0, "OnRing" + std::to_string(Index + 1));
        for (int Index = 1; Index < Sides; ++Index)
            AppendSketchConstraint(Sketch, Kind::EqualLength, Edges[0], Edges[Index], -1, 0.0, "Equal" + std::to_string(Index + 1));
        FeatureStep& Sweep = Author.Step(FeatureStepKind::Extrude, "Extrude#" + std::to_string(Ordinal + 1));
        Sweep.Profile      = SketchLabel;
        Sweep.Extent       = ParameterValue(Author.Named("Height"));
        Sweep.Combination  = Combination;
        Sweep.Target       = Request.Target;
        break;
    }
    case SketcherConstructKind::Tube:
    {
        // 📝 Annulus on XY: two circles on one centre point (concentric by construction), the inner one a hole of the profile.
        Author.Parameter("X", Spot.X, Spot.X - 5.0, Spot.X + 5.0, "axis x");
        Author.Parameter("Y", Spot.Y, Spot.Y - 5.0, Spot.Y + 5.0, "axis y");
        Author.Parameter("Z", Spot.Z, Spot.Z - 5.0, Spot.Z + 5.0, "base z (sketch elevation)");
        Author.Parameter("Radius", Request.Radius, 0.05, 5.0, "outer radius");
        Author.Parameter("Inner", Request.Inner, 0.02, 5.0, "bore radius");
        Author.Parameter("Height", Request.Height, 0.1, 10.0, "extent along +z (extrude)");
        FeatureStep& Section = Author.Step(FeatureStepKind::Sketch, SketchLabel);
        Section.Plane        = FloorPlane();
        Section.Elevation    = ParameterValue(Author.Named("Z"));
        SketchModel& Sketch  = Section.Sketch;
        const int Origin     = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int Centre     = AppendSketchPoint(Sketch, { Spot.X, Spot.Y });
        const int Outer      = AppendSketchCircle(Sketch, Centre, Request.Radius, 1);
        const int Bore       = AppendSketchCircle(Sketch, Centre, Request.Inner, 2);
        AppendSketchConstraint(Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        Author.Bound(Sketch, Kind::HorizontalDistance, Origin, Centre, "X");
        Author.Bound(Sketch, Kind::VerticalDistance, Origin, Centre, "Y");
        Author.Bound(Sketch, Kind::Radius, Outer, -1, "Radius");
        Author.Bound(Sketch, Kind::Radius, Bore, -1, "Inner");
        FeatureStep& Sweep = Author.Step(FeatureStepKind::Extrude, "Extrude#" + std::to_string(Ordinal + 1));
        Sweep.Profile      = SketchLabel;
        Sweep.Extent       = ParameterValue(Author.Named("Height"));
        Sweep.Combination  = Combination;
        Sweep.Target       = Request.Target;
        break;
    }
    }
    if (Request.PlaneCondition)
    {
        // 📝 Oriented to a body face: the tile is authored in its own floor frame, then every sketch plane it made is carried
        //    rigidly into the face frame F (o ↦ F.o + F.x̂·oₓ + F.ŷ·o_y + F.n̂·o_z, axes rotated alike). Elevations run along
        //    the carried normals and extrusions / revolutions follow their sketches, so the whole solid turns with the face.
        const OrthonormalFrame& Face = Request.Plane;
        const auto Carry  = [&Face](Vector3 Point) { return Face.XAxis * Point.X + Face.YAxis * Point.Y + Face.ZAxis * Point.Z; };
        for (size_t Index = FirstStep; Index < Tree.Steps.size(); ++Index)
        {
            FeatureStep& Step = Tree.Steps[Index];
            if (Step.Kind != FeatureStepKind::Sketch) continue;
            Step.Plane = { Face.Origin + Carry(Step.Plane.Origin), Carry(Step.Plane.XAxis), Carry(Step.Plane.YAxis), Carry(Step.Plane.ZAxis) };
        }
        for (size_t Index = FirstParameter; Index < Tree.Parameters.size(); ++Index) Tree.Parameters[Index].Description += " · face frame";
    }
    Document.Captions[SketchLabel]            = Caption;
    Document.Captions[Tree.Steps.back().Label] = Caption;
    return Caption;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMMAND FRAME
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SketcherHistoryEntry Snapshot(const SketcherDocument& Document, const std::string& Label)
{
    return { Document.Tree, Document.Captions, Label, Document.Appearance, false, std::make_shared<const RegenerationCache>(Document.Replay),
             Document.SummaryCheckpoint, Document.Summary, Document.Dimensions };
}

// 📝 Only the newest entries of each stack keep their checkpoints (memory bound); older ones replay when reached.
constexpr size_t HistoryReplayDepth = 8;                                                     // [-] - Entries per stack with checkpoints

void TrimHistoryReplay(SketcherDocument& Document)
{
    for (std::vector<SketcherHistoryEntry>* Entries : { &Document.UndoEntries, &Document.RedoEntries })
    {
        for (size_t Index = 0; Index + HistoryReplayDepth < Entries->size(); ++Index) (*Entries)[Index].Replay.reset();
    }
}

// 📝 A matcap-only entry swaps the appearance back and keeps the body (and its Revision): no regeneration.
void Restore(SketcherDocument& Document, SketcherHistoryEntry Entry)
{
    Document.Appearance = std::move(Entry.Appearance);
    Document.Dimensions = std::move(Entry.Dimensions);
    if (Entry.AppearanceCondition) return;
    Document.Tree     = std::move(Entry.Tree);
    Document.Captions = std::move(Entry.Captions);
    if (Entry.Replay)
    {
        Document.Replay            = *Entry.Replay;
        Document.Summary           = std::move(Entry.Summary);
        Document.SummaryCheckpoint = std::move(Entry.SummaryCheckpoint);
    }
    RegenerateSketcherDocument(Document);
}

// ① snapshot (flagged matcap-only) ② change the appearance (the callback returns a refusal or "") ③ keep or roll back.
template <typename Change>
SketcherCommandOutcome RunAppearanceCommand(SketcherDocument& Document, const std::string& Label, Change&& Apply)
{
    FRONTIER_TRACE_SCOPE("Appearance", Label);
    const auto           Start  = std::chrono::steady_clock::now();
    SketcherHistoryEntry Before = Snapshot(Document, Label);
    Before.AppearanceCondition  = true;
    SketcherCommandOutcome Outcome;
    Outcome.Label   = Label;
    Outcome.Refusal = Apply();
    if (!Outcome.Refusal.empty())
    {
        Document.Appearance  = std::move(Before.Appearance);
        Document.Dimensions  = std::move(Before.Dimensions);
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

// 📝 First refusal among the steps at or after First that did not already refuse before the command (RefusedBefore:
//    labels; the steps a command appends are never in it).
[[nodiscard]] std::string RefusalFrom(const SketcherDocument& Document, size_t First, const std::set<std::string>& RefusedBefore)
{
    for (size_t Index = First; Index < Document.Regeneration.Steps.size(); ++Index)
    {
        const FeatureStepReport& Report = Document.Regeneration.Steps[Index];
        if (Report.Status == FeatureStepStatus::Refused && !RefusedBefore.contains(Report.Label)) return Report.Label + ": " + Report.Refusal;
    }
    return {};
}

[[nodiscard]] std::set<std::string> RefusedLabels(const SketcherDocument& Document)
{
    std::set<std::string> Labels;
    for (const FeatureStepReport& Report : Document.Regeneration.Steps) if (Report.Status == FeatureStepStatus::Refused) Labels.insert(Report.Label);
    return Labels;
}

// ① snapshot ② change (the callback returns a refusal or "") ③ regenerate ④ roll back when a new step refused, or (edits
//    of existing steps: WatchFrom) a step at or after WatchFrom that regenerated before now refuses.
template <typename Change>
SketcherCommandOutcome RunCommand(SketcherDocument& Document, const std::string& Label, bool CaptureCondition, Change&& Apply,
                                  size_t WatchFrom = std::numeric_limits<size_t>::max())
{
    FRONTIER_TRACE_SCOPE("Command", Label);
    const auto                  Start         = std::chrono::steady_clock::now();
    SketcherHistoryEntry        Before        = Snapshot(Document, Label);
    const size_t                FirstNew      = std::min(Document.Tree.Steps.size(), WatchFrom);
    const std::set<std::string> RefusedBefore = RefusedLabels(Document);
    SketcherCommandOutcome Outcome;
    Outcome.Label = Label;
    Outcome.Refusal = Apply();
    if (Outcome.Refusal.empty())
    {
        // 📝 The figures (audit, volume) are measured only once the command stands: a refused command rolls back to
        //    the snapshot, whose figures are reused, so measuring the refused body was wasted work.
        RegenerateSketcherDocumentDeferred(Document, CaptureCondition);
        Outcome.Refusal = RefusalFrom(Document, FirstNew, RefusedBefore);
        if (Outcome.Refusal.empty()) SummarizeSketcherDocument(Document);
    }
    if (!Outcome.Refusal.empty())
    {
        FRONTIER_TRACE("Command", "refused, rolled back: " + Outcome.Refusal);
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    FRONTIER_TRACE("Command", "accepted · " + std::to_string(Document.Tree.Steps.size()) + " step(s) · " + std::to_string(Document.Regeneration.Bodies.size()) + " body(ies)");
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

[[nodiscard]] std::string FormatAmount(double Value)
{
    char Text[32];
    std::snprintf(Text, sizeof(Text), "%.4g", Value);
    return Text;
}

// Keeps the names some body still carries (Faces or Edges member of every body's naming).
void KeepResolvedNames(std::vector<std::string>& Names, const std::vector<RegeneratedBody>& Bodies, bool EdgeCondition)
{
    std::set<std::string> Present;
    for (const RegeneratedBody& Body : Bodies)
        for (const auto& Entry : EdgeCondition ? Body.Naming.Edges : Body.Naming.Faces) Present.insert(Entry.second);
    std::erase_if(Names, [&](const std::string& Name) { return !Present.contains(Name); });
}

[[nodiscard]] const RegeneratedBody* FindRegeneratedBody(const SketcherDocument& Document, const std::string& Key)
{
    for (const RegeneratedBody& Body : Document.Regeneration.Bodies) if (Body.Key == Key) return &Body;
    return nullptr;
}

// 📝 The one body carrying every name (faces or edges); refusal text when they span bodies or none carries one.
[[nodiscard]] std::string CommonBody(const SketcherDocument& Document, SketcherSelectionKind Kind, const std::vector<std::string>& Names, std::string& Key)
{
    Key.clear();
    // ① The bodies carrying every name (a cut's pieces share names): the picked body, the active one, else the first.
    std::vector<std::string> Carriers;
    for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
    {
        const std::map<uint32_t, std::string>& Table = Kind == SketcherSelectionKind::Edge ? Body.Naming.Edges : Kind == SketcherSelectionKind::Vertex ? Body.Naming.Vertices
                                                     : Body.Naming.Faces;
        std::set<std::string> Present;
        for (const auto& Entry : Table) Present.insert(Entry.second);
        if (!Names.empty() && std::all_of(Names.begin(), Names.end(), [&](const std::string& Name) { return Present.contains(Name); })) Carriers.push_back(Body.Key);
    }
    for (const std::string& Preferred : { Document.Selection.PickedBody, Document.Regeneration.Active })
        if (!Preferred.empty() && std::find(Carriers.begin(), Carriers.end(), Preferred) != Carriers.end()) { Key = Preferred; return {}; }
    if (!Carriers.empty()) { Key = Carriers.front(); return {}; }
    // ② None carries them all: say why.
    for (const std::string& Name : Names)
    {
        const std::string Owner = SketcherBodyOfName(Document, Kind, Name);
        if (Owner.empty()) return (Kind == SketcherSelectionKind::Edge ? "edge " : Kind == SketcherSelectionKind::Vertex ? "vertex " : "face ") + Name + " is on no body";
        if (!Key.empty() && Owner != Key)
            return "the selection spans " + SketcherBodyCaption(Document, Key) + " and " + SketcherBodyCaption(Document, Owner) + " (one body at a time)";
        Key = Owner;
    }
    return {};
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherDocument CreateSketcherDocument(const std::string& Title)
{
    SketcherDocument Document;
    Document.Title       = Title;
    Document.Tree.Name   = Title;
    Document.Partition   = std::make_unique<ModelPartition>();
    return Document;
}

void RegenerateSketcherDocument(SketcherDocument& Document, bool CaptureCondition)
{
    RegenerateSketcherDocumentDeferred(Document, CaptureCondition);
    SummarizeSketcherDocument(Document);
}

void RegenerateSketcherDocumentDeferred(SketcherDocument& Document, bool CaptureCondition)
{
    FRONTIER_TRACE_SCOPE("Document", std::string("RegenerateSketcherDocument") + (CaptureCondition ? " · capture" : ""));
    Document.Partition    = std::make_unique<ModelPartition>();
    Document.Regeneration = RegenerateFeatureTree(*Document.Partition, Document.Tree, CaptureCondition, &Document.Replay);
    // 📝 One counter for every document in the process, so a staged preview never shares a scene key with its source.
    static std::atomic<uint64_t> RevisionCounter{ 0 };
    Document.Revision     = ++RevisionCounter;
    KeepResolvedNames(Document.Selection.Faces, Document.Regeneration.Bodies, false);
    KeepResolvedNames(Document.Selection.Edges, Document.Regeneration.Bodies, true);
    std::erase_if(Document.Selection.Vertices, [&](const std::string& Name)
    {
        return std::none_of(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(), [&](const RegeneratedBody& Body)
        {
            return std::any_of(Body.Naming.Vertices.begin(), Body.Naming.Vertices.end(), [&](const auto& Entry) { return Entry.second == Name; });
        });
    });
    std::erase_if(Document.Selection.Bodies, [&](const std::string& Key) { return !FindRegeneratedBody(Document, Key); });
    // Picked curves are SpaceCurves or sketches (loft sections, gizmo); either kind survives while its label still solves.
    std::erase_if(Document.Selection.Curves, [&](const std::string& Label) { return !Document.Regeneration.Curves.contains(Label) && !Document.Regeneration.Sketches.contains(Label); });
    RefreshSketcherDimensions(Document);
}

void SummarizeSketcherDocument(SketcherDocument& Document)
{
    FRONTIER_TRACE_SCOPE("Document", "SummarizeSketcherDocument");
    // Every step (and modifier stack when non-empty) restored from the checkpoint the summary was measured on:
    // the bodies are those bodies (same records), so the audit / volume figures still hold; only captions refresh.
    const std::shared_ptr<const ReplayCheckpoint> Final = !Document.Tree.Modifiers.empty()
                                                        ? Document.Replay.ModifierCheckpoint
                                                        : (Document.Replay.Checkpoints.empty() ? nullptr : Document.Replay.Checkpoints.back());
    const bool ReusedCondition = Final && Document.SummaryCheckpoint == Final && Document.Regeneration.Resumed == static_cast<int>(Document.Tree.Steps.size());
    Document.SummaryCheckpoint = Final;
    SketcherBodySummary& Summary = Document.Summary;
    if (ReusedCondition)
    {
        Summary.RegenerationMs = Document.Regeneration.Milliseconds;
        for (SketcherBodyFigures& Figures : Summary.Bodies) Figures.Caption = SketcherBodyCaption(Document, Figures.Key);
    }
    else
    {
        Summary                      = {};
        Summary.RegenerationMs       = Document.Regeneration.Milliseconds;
        Summary.SoundCondition       = true;
        for (const RegeneratedBody& Regenerated : Document.Regeneration.Bodies)
        {
            const BodyToken Body = Regenerated.Body;
            if (!Body.PresentCondition() || !Document.Partition->Bodies.Authenticate(Body)) continue;
            const BodyInventory Inventory = Document.Partition->CollectInventory(Body);
            if (Inventory.Faces.empty()) continue;
            const auto KnownAudit  = std::find_if(Document.Regeneration.Audits.rbegin(), Document.Regeneration.Audits.rend(),
                                                  [&](const auto& Entry) { return Entry.first == Body; });
            const AuditReport   Audit     = KnownAudit != Document.Regeneration.Audits.rend() ? KnownAudit->second : AuditBody(*Document.Partition, Body);
            SketcherBodyFigures Figures;
            Figures.Key            = Regenerated.Key;
            Figures.Caption        = SketcherBodyCaption(Document, Regenerated.Key);
            Figures.SoundCondition = Audit.SoundCondition();
            Figures.SheetCondition = Document.Partition->Bodies.Resolve(Body).Category == BodyCategory::Sheet;
            Figures.Faces          = static_cast<int>(Inventory.Faces.size());
            Figures.Edges          = static_cast<int>(Inventory.Edges.size());
            Figures.Vertices       = static_cast<int>(Inventory.Vertices.size());
            Figures.Genus          = Audit.Genus;
            // A moved body's volume was integrated by the move's own assembly (same shells, same integral): reused.
            // The audit integrated every solid shell already (G6, same shells, same order): its total is the volume.
            const auto Known       = std::find_if(Document.Regeneration.Volumes.begin(), Document.Regeneration.Volumes.end(), [&](const auto& Entry) { return Entry.first == Body; });
            Figures.Volume         = Figures.SheetCondition ? 0.0 : Known != Document.Regeneration.Volumes.end() ? Known->second
                                   : Audit.VolumeCondition ? Audit.Volume : IntegrateBodyVolume(*Document.Partition, Body);
            Summary.PresentCondition = true;
            Summary.SoundCondition   = Summary.SoundCondition && Figures.SoundCondition;
            Summary.SheetCondition   = Summary.SheetCondition || Figures.SheetCondition;
            Summary.Faces           += Figures.Faces;
            Summary.Edges           += Figures.Edges;
            Summary.Vertices        += Figures.Vertices;
            Summary.Genus            = (Summary.Genus < 0 ? 0 : Summary.Genus) + Figures.Genus;
            if (!Document.Appearance.LiveCutters.contains(Figures.Key)) Summary.Volume += Figures.Volume;
            Summary.Bodies.push_back(std::move(Figures));
        }
        if (!Summary.PresentCondition) Summary.SoundCondition = false;
    }
}

SketcherCommandOutcome AdoptSketcherPreview(SketcherDocument& Document, SketcherDocument&& Preview, const SketcherCommandOutcome& Staged)
{
    FRONTIER_TRACE_SCOPE("Command", Staged.Label + " · adopted from the preview");
    const auto           Start  = std::chrono::steady_clock::now();
    SketcherHistoryEntry Before = Snapshot(Document, Staged.Label);
    Document.Tree              = std::move(Preview.Tree);
    Document.Captions          = std::move(Preview.Captions);
    Document.Partition         = std::move(Preview.Partition);
    Document.Regeneration      = std::move(Preview.Regeneration);
    Document.Summary           = std::move(Preview.Summary);
    Document.Selection         = std::move(Preview.Selection);
    Document.Revision          = Preview.Revision;
    Document.Appearance        = std::move(Preview.Appearance);
    if (!Preview.Dimensions.empty()) Document.Dimensions = std::move(Preview.Dimensions);
    RefreshSketcherDimensions(Document);
    Document.Replay            = std::move(Preview.Replay);
    Document.SummaryCheckpoint = std::move(Preview.SummaryCheckpoint);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    SketcherCommandOutcome Outcome = Staged;
    Outcome.Milliseconds          += ElapsedMs(Start);
    return Outcome;
}

SketcherDocument CloneSketcherDocument(const SketcherDocument& Source)
{
    SketcherDocument Copy = CreateSketcherDocument(Source.Title);
    Copy.Tree      = Source.Tree;
    Copy.Captions  = Source.Captions;
    Copy.Selection = Source.Selection;
    Copy.ImagePlanes = Source.ImagePlanes;
    Copy.Appearance = Source.Appearance;
    Copy.DrawingSheet = Source.DrawingSheet;
    Copy.Dimensions = Source.Dimensions;
    Copy.Replay     = Source.Replay;
    Copy.Summary    = Source.Summary;
    Copy.SummaryCheckpoint = Source.SummaryCheckpoint;
    RegenerateSketcherDocument(Copy);
    return Copy;
}

SketcherCommandOutcome ConstructSketcherPrimitive(SketcherDocument& Document, const SketcherConstructRequest& Request)
{
    const bool        FirstBodyCondition = !Document.Summary.PresentCondition;
    const std::string Label = NextCaption(Document, SketcherConstructLabel(Request.Kind)) + " · " +
                              (FirstBodyCondition && Request.Combination != FeatureCombination::Separate ? "New" : FeatureCombinationLabel(Request.Combination));
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        // 📝 Only the dimensions the tile reads are checked (a cone's Height may be negative: it points down).
        const SketcherConstructKind Tile = Request.Kind;
        const bool BoxLike = Tile == SketcherConstructKind::Block || Tile == SketcherConstructKind::Wedge;
        const bool Round   = Tile == SketcherConstructKind::Cylinder || Tile == SketcherConstructKind::Sphere || Tile == SketcherConstructKind::Cone ||
                             Tile == SketcherConstructKind::Prism || Tile == SketcherConstructKind::Tube;
        const bool Tall    = Tile != SketcherConstructKind::Sphere && Tile != SketcherConstructKind::Torus;
        if ((BoxLike && (Request.Width <= 0.0 || Request.Depth <= 0.0)) || (Round && Request.Radius <= 0.0) ||
            (Tall && (Tile == SketcherConstructKind::Cone ? Request.Height == 0.0 : Request.Height <= 0.0)))
            return "every dimension must be positive";
        if (Tile == SketcherConstructKind::Torus && (Request.Minor <= 0.0 || Request.Major <= Request.Minor)) return "torus: Major must exceed Minor > 0";
        if (Tile == SketcherConstructKind::Tube && (Request.Inner <= 0.0 || Request.Inner >= Request.Radius)) return "tube: the bore must lie inside the outer radius";
        if (Tile == SketcherConstructKind::Prism && (Request.Sides < 3 || Request.Sides > 64)) return "prism: 3 … 64 sides";
        if (!FirstBodyCondition && Request.Combination == FeatureCombination::New)
            return "a second body needs Union, Difference, Intersection or New body";
        if (!Request.Target.empty() && !FindRegeneratedBody(Document, Request.Target)) return "no body named " + Request.Target;
        if (Request.PlaneCondition)
        {
            const OrthonormalFrame& Face = Request.Plane;
            const bool Finite = std::isfinite(Face.Origin.X) && std::isfinite(Face.Origin.Y) && std::isfinite(Face.Origin.Z);
            const bool Square = std::abs(Length(Face.XAxis) - 1.0) < 1.0e-9 && std::abs(Length(Face.YAxis) - 1.0) < 1.0e-9 && std::abs(Length(Face.ZAxis) - 1.0) < 1.0e-9
                             && std::abs(Dot(Face.XAxis, Face.YAxis)) < 1.0e-9 && Length(Cross(Face.XAxis, Face.YAxis) - Face.ZAxis) < 1.0e-9;
            if (!Finite || !Square) return "the face plane is not an orthonormal right-handed frame";
        }
        if (Request.Start > 0.0) return "a construct start lies on or below its plane (start ≤ 0)";
        const size_t Authored = Document.Tree.Steps.size();
        AuthorTile(Document, Request, FirstBodyCondition && Request.Combination != FeatureCombination::Separate ? FeatureCombination::New : Request.Combination);
        // A footprint on the tangent plane of a curved face starts sunk into it (MeasureSketcherProfileSink), so the
        // Union closes instead of touching along a line.
        if (Request.Start < 0.0)
            for (size_t Index = Authored; Index < Document.Tree.Steps.size(); ++Index)
            {
                FeatureStep& Step = Document.Tree.Steps[Index];
                if (Step.Kind == FeatureStepKind::Extrude && Step.Combination == FeatureCombination::Union && EvaluateFeatureValue(Document.Tree, Step.Extent) > 0.0)
                    Step.Start = FeatureValue{ Request.Start };
            }
        return {};
    });
}

SketcherCommandOutcome BlendSketcherSelection(SketcherDocument& Document, BlendProfile Shape, double Size)
{
    SketcherBlendRequest Request;
    Request.Shape = Shape;
    Request.Size  = Size;
    return BlendSketcherSelection(Document, Request);
}

// PropagateCondition: the step takes the picked edges' tangent chains too; Grown says whether that added any edge.
static SketcherCommandOutcome BlendSketcherEdges(SketcherDocument& Document, const SketcherBlendRequest& Request, bool PropagateCondition, bool& Grown)
{
    const bool        VariableCondition = Request.EndSize.has_value() && Request.VertexSizes.empty();
    const bool        HandleCondition   = !Request.VertexSizes.empty();
    const std::string Stem  = Request.Shape == BlendProfile::Fillet ? "Fillet" : "Chamfer";
    const std::string Sizes = FormatAmount(Request.Size) + (VariableCondition ? " → " + FormatAmount(*Request.EndSize) : std::string())
                            + (HandleCondition ? " · " + std::to_string(Request.VertexSizes.size()) + " corner size(s)" : std::string());
    const std::string Label = Stem + " " + Sizes + " on " + std::to_string(Document.Selection.Edges.size()) + " edge(s)";
    return RunCommand(Document, Label, true, [&]() -> std::string
    {
        if (Document.Selection.Edges.empty()) return "select one or more edges first";
        if (Request.Size <= 0.0 || (VariableCondition && *Request.EndSize <= 0.0)) return "the blend size must be positive";
        for (const auto& [Vertex, Size] : Request.VertexSizes) if (!(Size > 0.0)) return "the corner size at " + Vertex + " must be positive";
        std::string Owner;
        if (const std::string Problem = CommonBody(Document, SketcherSelectionKind::Edge, Document.Selection.Edges, Owner); !Problem.empty()) return Problem;
        const RegeneratedBody* Target = FindRegeneratedBody(Document, Owner);

        // ① Variable: name each edge's anchor vertex on the current body (start, or end when reversed).
        std::vector<TopologyReference> Anchors;
        if (VariableCondition)
        {
            if (!Document.Partition || !Target) return "there is no body to blend";
            const BodyNaming& Naming = Target->Naming;
            for (const std::string& EdgeName : Document.Selection.Edges)
            {
                const auto Edge = std::find_if(Naming.Edges.begin(), Naming.Edges.end(), [&](const auto& Entry) { return Entry.second == EdgeName; });
                if (Edge == Naming.Edges.end()) return "edge " + EdgeName + " is not on the current body";
                const EdgeProfile& Profile = Document.Partition->Edges.Resolve(Document.Partition->Edges.TokenAt(Edge->first));
                if (Profile.Start == Profile.End) return "variable size on a closed edge (the size would jump at its only vertex)";
                const VertexToken Anchor = Request.ReversedCondition ? Profile.End : Profile.Start;
                const auto Vertex = Naming.Vertices.find(Anchor.Slot);
                if (Vertex == Naming.Vertices.end()) return "the anchor vertex of " + EdgeName + " has no persistent name";
                Anchors.push_back({ ReferenceKind::Vertex, Vertex->second, {} });
            }
        }

        // ①′ Handles: every listed vertex must be an end of a picked edge on the current body.
        std::vector<TopologyReference> Corners;
        if (HandleCondition)
        {
            if (!Document.Partition || !Target) return "there is no body to blend";
            std::set<std::string> Ends;
            for (const std::string& EdgeName : Document.Selection.Edges)
            {
                const auto Edge = std::find_if(Target->Naming.Edges.begin(), Target->Naming.Edges.end(), [&](const auto& Entry) { return Entry.second == EdgeName; });
                if (Edge == Target->Naming.Edges.end()) return "edge " + EdgeName + " is not on the current body";
                const EdgeProfile& Profile = Document.Partition->Edges.Resolve(Document.Partition->Edges.TokenAt(Edge->first));
                for (VertexToken End : { Profile.Start, Profile.End })
                    if (const auto Name = Target->Naming.Vertices.find(End.Slot); Name != Target->Naming.Vertices.end()) Ends.insert(Name->second);
            }
            std::set<std::string> Seen;
            for (const auto& [Vertex, Size] : Request.VertexSizes)
            {
                if (!Ends.contains(Vertex)) return "corner " + Vertex + " is not an end of a picked edge";
                if (!Seen.insert(Vertex).second) return "corner " + Vertex + " has two sizes";
                Corners.push_back({ ReferenceKind::Vertex, Vertex, {} });
            }
        }

        // ② Parameters and the step.
        const std::string Caption   = NextCaption(Document, Stem);
        const std::string StepLabel = "Blend#" + std::to_string(NextStepOrdinal(Document.Tree));
        double            Larger    = std::max(Request.Size, VariableCondition ? *Request.EndSize : Request.Size);
        for (const auto& Entry : Request.VertexSizes) Larger = std::max(Larger, Entry.second);
        const char*       Meaning   = Request.Shape == BlendProfile::Fillet ? "fillet radius" : "chamfer setback";
        Document.Tree.Parameters.push_back({ Caption + ".Size", Request.Size, 0.0, std::max(1.0, 4.0 * Larger),
                                             VariableCondition ? std::string(Meaning) + " at the anchor corners" : std::string(Meaning) });
        FeatureStep Step;
        Step.Kind  = FeatureStepKind::Blend;
        Step.Label = StepLabel;
        Step.Shape  = Request.Shape;
        Step.Size   = ParameterValue(Caption + ".Size");
        Step.Target = Owner;
        // 💡 Tangent propagation (constant size, on refusal): an edge that runs on G1 into another sharp edge (the rim of a
        //    rounded end) takes the whole chain, as CAD fillets do — a blend cannot end where its support turns tangent.
        std::vector<std::string> Chain = Document.Selection.Edges;
        if (PropagateCondition && !VariableCondition && !HandleCondition && Document.Partition && Target)
        {
            std::vector<EdgeToken> Seeds;
            for (const EdgeToken Edge : Document.Partition->CollectInventory(Target->Body).Edges)
                if (const auto Found = Target->Naming.Edges.find(Edge.Slot); Found != Target->Naming.Edges.end() && std::find(Chain.begin(), Chain.end(), Found->second) != Chain.end()) Seeds.push_back(Edge);
            for (const EdgeToken Edge : GrowTangentChain(*Document.Partition, Target->Body, Seeds))
                if (const auto Found = Target->Naming.Edges.find(Edge.Slot); Found != Target->Naming.Edges.end() && std::find(Chain.begin(), Chain.end(), Found->second) == Chain.end()) Chain.push_back(Found->second);
        }
        Grown = Chain.size() > Document.Selection.Edges.size();
        if (PropagateCondition && !Grown) return "no tangent chain to propagate";
        for (const std::string& Name : Chain) Step.References.push_back({ ReferenceKind::Edge, Name, {} });
        if (VariableCondition)
        {
            Document.Tree.Parameters.push_back({ Caption + ".EndSize", *Request.EndSize, 0.0, std::max(1.0, 4.0 * Larger), std::string(Meaning) + " at the far corners" });
            Step.EndSize     = ParameterValue(Caption + ".EndSize");
            Step.SizeAnchors = std::move(Anchors);
        }
        for (size_t Index = 0; Index < Request.VertexSizes.size(); ++Index)
        {
            const std::string Name = Caption + ".Corner" + std::to_string(Index + 1);
            Document.Tree.Parameters.push_back({ Name, Request.VertexSizes[Index].second, 0.0, std::max(1.0, 4.0 * Larger), std::string(Meaning) + " at " + Request.VertexSizes[Index].first });
            Step.AnchorSizes.push_back(ParameterValue(Name));
        }
        if (HandleCondition) Step.SizeAnchors = std::move(Corners);
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Captions[StepLabel] = Caption;
        return {};
    });
}

// The picked edges alone first; refused, their tangent chains (the blend could not end at a G1 joint) when they grow.
SketcherCommandOutcome BlendSketcherSelection(SketcherDocument& Document, const SketcherBlendRequest& Request)
{
    bool Grown = false;
    const SketcherCommandOutcome Alone = BlendSketcherEdges(Document, Request, false, Grown);
    if (Alone.AcceptedCondition || Request.EndSize.has_value() || !Request.VertexSizes.empty()) return Alone;
    const SketcherCommandOutcome Chained = BlendSketcherEdges(Document, Request, true, Grown);
    return Chained.AcceptedCondition ? Chained : Alone;
}

// Edge tokens of the picked edge names on the one body carrying them (empty when they span bodies or none resolves).
[[nodiscard]] std::vector<EdgeToken> ResolvePickedEdges(const SketcherDocument& Document, const std::vector<std::string>& Edges, const RegeneratedBody*& Owner)
{
    Owner = nullptr;
    std::string Key;
    if (!Document.Partition || Edges.empty() || !CommonBody(Document, SketcherSelectionKind::Edge, Edges, Key).empty()) return {};
    Owner = FindRegeneratedBody(Document, Key);
    if (!Owner) return {};
    std::vector<EdgeToken> Tokens;
    for (const std::string& Name : Edges)
    {
        const auto Entry = std::find_if(Owner->Naming.Edges.begin(), Owner->Naming.Edges.end(), [&](const auto& Pair) { return Pair.second == Name; });
        if (Entry == Owner->Naming.Edges.end()) return {};
        Tokens.push_back(Document.Partition->Edges.TokenAt(Entry->first));
    }
    return Tokens;
}

// The two faces of a manifold edge (the second equals the first on a lamina edge).
[[nodiscard]] std::array<FaceToken, 2> EdgeFaces(const ModelPartition& Partition, EdgeToken Edge)
{
    const FinToken Entry = Partition.Edges.Resolve(Edge).RadialFin;
    const FinToken Mate  = Partition.RadialMate(Entry);
    return { Partition.Loops.Resolve(Partition.Fins.Resolve(Entry).Loop).Face, Partition.Loops.Resolve(Partition.Fins.Resolve(Mate).Loop).Face };
}

std::vector<SketcherBlendHandle> MeasureSketcherBlendHandles(const SketcherDocument& Document, const std::vector<std::string>& Edges)
{
    const RegeneratedBody*       Owner  = nullptr;
    const std::vector<EdgeToken> Tokens = ResolvePickedEdges(Document, Edges, Owner);
    if (Tokens.empty()) return {};
    const ModelPartition& Partition = *Document.Partition;
    const auto Bisector = [&](const std::vector<std::pair<FaceToken, Vector3>>& Feet)
    {
        Vector3 Sum{};
        std::set<uint32_t> Faces;
        for (const auto& [Face, Position] : Feet) if (Faces.insert(Face.Slot).second) Sum = Sum + EvaluateOutwardNormal(Partition, Face, Position);
        return Length(Sum) > 1.0e-9 ? Normalize(Sum) : Vector3{};
    };
    // How far the faces turn about the bisector: the mean cosine between it and each face's normal (cos ½φ for two faces
    // turning φ). A handle sits on the bevel from it: a fillet's mid-line lies r (1 / Spread − 1) in from the sharp
    // edge, a chamfer's s √(1 − Spread²).
    const auto SpreadOf = [&](const std::vector<std::pair<FaceToken, Vector3>>& Feet, Vector3 Direction)
    {
        double Sum = 0.0;
        int    Count = 0;
        std::set<uint32_t> Faces;
        for (const auto& [Face, Position] : Feet)
            if (Faces.insert(Face.Slot).second) { Sum += Dot(Direction, EvaluateOutwardNormal(Partition, Face, Position)); ++Count; }
        return Count > 0 ? Sum / Count : 0.0;
    };

    // ① The uniform handle at the middle of the first edge.
    std::vector<SketcherBlendHandle> Handles;
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Tokens.front());
        const Vector3      Middle  = EvaluateCurve(Partition.Curves.Resolve(Profile.Curve), 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
        const auto         Faces   = EdgeFaces(Partition, Tokens.front());
        const std::vector<std::pair<FaceToken, Vector3>> Feet = { { Faces[0], Middle }, { Faces[1], Middle } };
        const Vector3 Direction = Bisector(Feet);
        Handles.push_back({ {}, Middle, Direction, int(Tokens.size()), SpreadOf(Feet, Direction) });
    }

    // ② One per distinct end vertex of the open picked edges (a closed edge's only vertex cannot take its own size).
    std::map<uint32_t, std::vector<std::pair<FaceToken, Vector3>>> Feet;
    std::map<uint32_t, int>  Counts;
    std::set<uint32_t>       Closed;
    std::vector<uint32_t>    Order;
    for (EdgeToken Edge : Tokens)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        const auto         Faces   = EdgeFaces(Partition, Edge);
        if (Profile.Start == Profile.End) { Closed.insert(Profile.Start.Slot); continue; }
        for (VertexToken End : { Profile.Start, Profile.End })
        {
            const Vector3 Position = Partition.Vertices.Resolve(End).Position;
            if (!Counts.contains(End.Slot)) Order.push_back(End.Slot);
            Counts[End.Slot] += 1;
            Feet[End.Slot].push_back({ Faces[0], Position });
            Feet[End.Slot].push_back({ Faces[1], Position });
        }
    }
    for (uint32_t Slot : Order)
    {
        const auto Name = Owner->Naming.Vertices.find(Slot);
        if (Closed.contains(Slot) || Name == Owner->Naming.Vertices.end()) continue;
        const Vector3 Direction = Bisector(Feet[Slot]);
        if (Length(Direction) < 0.5) continue;
        Handles.push_back({ Name->second, Partition.Vertices.Resolve(Partition.Vertices.TokenAt(Slot)).Position, Direction, Counts[Slot], SpreadOf(Feet[Slot], Direction) });
    }
    return Handles;
}

SketcherBlendFit MeasureSketcherBlendFit(const SketcherDocument& Document, const std::vector<std::string>& Edges)
{
    SketcherBlendFit             Fit;
    const RegeneratedBody*       Owner  = nullptr;
    const std::vector<EdgeToken> Tokens = ResolvePickedEdges(Document, Edges, Owner);
    if (Tokens.empty()) { Fit.Refusal = "no picked edges on one body"; return Fit; }
    const ModelPartition& Partition = *Document.Partition;
    double Span = std::numeric_limits<double>::infinity();
    for (EdgeToken Edge : Tokens)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        const double       Along   = MeasureEdgeLength(Partition, Edge);
        Span = std::min(Span, Profile.Start == Profile.End ? Along / (2.0 * std::numbers::pi) : Along);   // Closed: its radius when it is a circle
        // The other edges of both faces that touch this edge's ends bound how far the blend can roll onto the faces.
        for (FaceToken Face : EdgeFaces(Partition, Edge))
        {
            for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
            {
                const FinToken First = Partition.Loops.Resolve(Loop).FirstFin;
                if (!First.PresentCondition()) continue;
                FinToken Fin = First;
                for (int Guard = 0; Guard < 100000; ++Guard)
                {
                    const EdgeToken Other = Partition.Fins.Resolve(Fin).Edge;
                    if (Other != Edge)
                    {
                        const EdgeProfile& Neighbour = Partition.Edges.Resolve(Other);
                        const bool Touching = Neighbour.Start == Profile.Start || Neighbour.Start == Profile.End || Neighbour.End == Profile.Start || Neighbour.End == Profile.End;
                        if (Touching && Neighbour.Start != Neighbour.End) Span = std::min(Span, MeasureEdgeLength(Partition, Other));
                    }
                    Fin = Partition.Fins.Resolve(Fin).Successor;
                    if (Fin == First) break;
                }
            }
        }
    }
    if (!std::isfinite(Span) || !(Span > 0.0)) { Fit.Refusal = "the picked edges have no length"; return Fit; }
    Fit.Span = Span;
    // 📝 Reaches (the report "it suppose to go longer even if i put a value greater it refuses … this is a cube"): the
    //    bevel stopped at Span, the width of the faces beside the edge — wrong twice: past it the blend runs on over the
    //    next face (the modeller builds that overflow exactly), and a fillet's radius is not its setback (an edge between
    //    faces at 135° turns 45°: r · tan 22.5° = 0.41 r). The largest size worth asking for is the one whose setback is
    //    the body's diagonal; beyond, the modeller's refusals (the bound search) say where it really stops.
    Vector3 Low{ std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() };
    Vector3 High{ -Low.X, -Low.Y, -Low.Z };
    for (const auto& [Slot, Name] : Owner->Naming.Vertices)
    {
        const Vector3 Position = Partition.Vertices.Resolve(Partition.Vertices.TokenAt(Slot)).Position;
        Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
        High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) };
    }
    const double Diagonal = std::isfinite(Low.X) ? std::max(Distance(Low, High), Span) : 4.0 * Span;   // [len] - Body extent
    double Turning = 0.0;                                                                    // [rad] - Sharpest turn of the picked edges
    for (EdgeToken Edge : Tokens)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        const Vector3      Middle  = EvaluateCurve(Partition.Curves.Resolve(Profile.Curve), 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
        const auto         Faces   = EdgeFaces(Partition, Edge);
        if (Faces.size() < 2) continue;
        const double Cosine = Dot(EvaluateOutwardNormal(Partition, Faces[0], Middle), EvaluateOutwardNormal(Partition, Faces[1], Middle));
        Turning = std::max(Turning, std::acos(std::clamp(Cosine, -1.0, 1.0)));
    }
    Turning          = std::clamp(Turning, 10.0 * std::numbers::pi / 180.0, 170.0 * std::numbers::pi / 180.0);
    Fit.ChamferReach = Diagonal;
    Fit.FilletReach  = Diagonal / std::tan(0.5 * Turning);
    // 📝 Merge sizes (the report "when we bevel and edges / vertices come close together they must merge or create new
    //    vertex"): on a planar support face, a straight picked edge's rail runs parallel to it, setback s = r · tan ½θ
    //    in (fillet, θ the turn between the faces) or s = d (chamfer). A parallel straight edge w away on the same face
    //    is met when the setbacks add up to w — both rails when it is picked too (they fuse into one ridge, the face
    //    between them vanishes), the rail alone when it is not (the face is used up). The viewport snaps a dragged size
    //    onto these, so the blends meet exactly and the modeller merges them instead of leaving a sliver face.
    {
        const auto Rate = [&](EdgeToken Edge, bool Chamfer)                                // Setback per unit size
        {
            if (Chamfer) return 1.0;
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            const Vector3      Middle  = EvaluateCurve(Partition.Curves.Resolve(Profile.Curve), 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
            const auto         Faces   = EdgeFaces(Partition, Edge);
            const double       Cosine  = Dot(EvaluateOutwardNormal(Partition, Faces[0], Middle), EvaluateOutwardNormal(Partition, Faces[1], Middle));
            return std::tan(0.5 * std::acos(std::clamp(Cosine, -1.0, 1.0)));
        };
        std::set<uint32_t> Picked;
        for (EdgeToken Edge : Tokens) Picked.insert(Edge.Slot);
        for (EdgeToken Edge : Tokens)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            const auto*        Line    = std::get_if<StraightLine>(&Partition.Curves.Resolve(Profile.Curve));
            if (!Line || Profile.Start == Profile.End) continue;
            for (FaceToken Face : EdgeFaces(Partition, Edge))
            {
                const FaceProfile& Record = Partition.Faces.Resolve(Face);
                if (!std::holds_alternative<PlaneSurface>(Partition.Surfaces.Resolve(Record.Surface))) continue;
                for (LoopToken Loop : Record.Loops)
                {
                    const FinToken First = Partition.Loops.Resolve(Loop).FirstFin;
                    if (!First.PresentCondition()) continue;
                    FinToken Fin = First;
                    for (int Guard = 0; Guard < 100000; ++Guard)
                    {
                        const EdgeToken    Other     = Partition.Fins.Resolve(Fin).Edge;
                        const EdgeProfile& Neighbour = Partition.Edges.Resolve(Other);
                        const auto*        Across    = std::get_if<StraightLine>(&Partition.Curves.Resolve(Neighbour.Curve));
                        if (Other != Edge && Across && Neighbour.Start != Neighbour.End && std::abs(std::abs(Dot(Across->Direction, Line->Direction)) - 1.0) <= 1.0e-12)
                        {
                            const Vector3 Offset = Across->Origin - Line->Origin;
                            const double  Width  = Length(Offset - Line->Direction * Dot(Offset, Line->Direction));   // [len] - Line-to-line distance
                            if (Width > 1.0e-9 * Diagonal)
                            {
                                const bool Both = Picked.contains(Other.Slot);
                                for (const bool Chamfer : { false, true })
                                {
                                    const double Sum = Rate(Edge, Chamfer) + (Both ? Rate(Other, Chamfer) : 0.0);
                                    if (Sum > 1.0e-9) (Chamfer ? Fit.ChamferMerges : Fit.FilletMerges).push_back(Width / Sum);
                                }
                            }
                        }
                        Fin = Partition.Fins.Resolve(Fin).Successor;
                        if (Fin == First) break;
                    }
                }
            }
        }
        for (std::vector<double>* Merges : { &Fit.FilletMerges, &Fit.ChamferMerges })
        {
            std::sort(Merges->begin(), Merges->end());
            Merges->erase(std::unique(Merges->begin(), Merges->end(), [&](double A, double B) { return std::abs(A - B) <= 1.0e-9 * Diagonal; }), Merges->end());
        }
    }
    const double Wanted = 0.15 * Span;                                                      // [len] - About 15 % of the bound
    const double Decade = std::pow(10.0, std::floor(std::log10(Wanted)));
    const double Steps[] = { 5.0, 2.0, 1.0 };
    Fit.Fitted = Decade;
    for (double Step : Steps) if (Step * Decade <= Wanted * (1.0 + 1.0e-9)) { Fit.Fitted = Step * Decade; break; }
    return Fit;
}

// 💡 Where a direct edit (face change, reshape) of body Owner goes in the history: before the body's trailing fillets /
//    chamfers, so they re-solve on the edited shape (Parasolid re-blends after a tweak; here the history replays them).
//    Walking back from the end: sketches, curves and other bodies' steps are passed over; each Blend on Owner moves the
//    insertion before it, unless a picked name was made by that blend (its faces / edges do not exist before it); any
//    other step on Owner stops the walk. Returns Steps.size() when nothing moves it (append).
[[nodiscard]] size_t DirectEditInsertion(const SketcherDocument& Document, const std::string& Owner, const std::vector<std::string>& Names)
{
    const std::vector<FeatureStep>& Steps = Document.Tree.Steps;
    size_t Insertion = Steps.size();
    for (size_t Index = Steps.size(); Index-- > 0;)
    {
        const FeatureStep& Step = Steps[Index];
        if (Step.Kind == FeatureStepKind::Sketch || Step.Kind == FeatureStepKind::SpaceCurve) continue;
        const bool CreatesSeparateBody = ((Step.Kind == FeatureStepKind::Extrude || Step.Kind == FeatureStepKind::Revolve ||
                                           Step.Kind == FeatureStepKind::Loft || Step.Kind == FeatureStepKind::Network ||
                                           Step.Kind == FeatureStepKind::Pipe || Step.Kind == FeatureStepKind::Evolution) &&
                                          (Step.Combination == FeatureCombination::New || Step.Combination == FeatureCombination::Separate)) ||
                                         Step.Kind == FeatureStepKind::Import;
        const bool TouchesCondition = CreatesSeparateBody
                                    ? (Step.Label == Owner)
                                    : (Step.Target.empty() || Step.Target == Owner || Step.Tool == Owner || Step.Label == Owner || Step.Source == Owner);
        if (!TouchesCondition) continue;
        if (Step.Kind != FeatureStepKind::Blend) break;
        const std::string Made = Step.Label + ".";
        if (std::any_of(Names.begin(), Names.end(), [&](const std::string& Name) { return Name.find(Made) != std::string::npos; })) break;
        Insertion = Index;
    }
    return Insertion;
}

// One direct-edit step: tried before the trailing fillets first (they follow the edit), then appended when that refuses
// (the fillets could not re-solve on the new shape, or a reference did not exist yet).
template <typename Author>
[[nodiscard]] SketcherCommandOutcome RunDirectEdit(SketcherDocument& Document, const std::string& Label, const std::string& Owner,
                                                   const std::vector<std::string>& Names, Author&& Write)
{
    const size_t Before = Owner.empty() ? Document.Tree.Steps.size() : DirectEditInsertion(Document, Owner, Names);
    const auto   Attempt = [&](size_t At)
    {
        return RunCommand(Document, Label, true, [&]() -> std::string
        {
            FeatureStep Step;
            if (std::string Problem = Write(Step); !Problem.empty()) return Problem;
            Document.Tree.Steps.insert(Document.Tree.Steps.begin() + static_cast<std::ptrdiff_t>(std::min(At, Document.Tree.Steps.size())), std::move(Step));
            return {};
        }, At);
    };
    SketcherCommandOutcome Outcome = Attempt(Before);
    if (Outcome.AcceptedCondition || Before >= Document.Tree.Steps.size()) return Outcome;
    SketcherCommandOutcome Appended = Attempt(Document.Tree.Steps.size());
    if (!Appended.AcceptedCondition) Appended.Refusal += " (before the fillets: " + Outcome.Refusal + ")";
    return Appended;
}

SketcherCommandOutcome ChangeSketcherSelection(SketcherDocument& Document, FaceOperationKind Operation, double Amount, std::optional<Vector3> Direction)
{
    const std::string Stem  = SketcherFaceOperationLabel(Operation);
    const std::string Label = Stem + " " + FormatAmount(Amount) + " on " + std::to_string(Document.Selection.Faces.size()) + " face(s)";
    const bool WholeBodyCondition = Operation == FaceOperationKind::OffsetBody || Operation == FaceOperationKind::Heal;
    std::string Owner;
    std::string OwnerProblem;
    if (!Document.Selection.Faces.empty() && Document.Summary.PresentCondition)
        OwnerProblem = CommonBody(Document, SketcherSelectionKind::Face, Document.Selection.Faces, Owner);
    if (Owner.empty() && !Document.Selection.Bodies.empty()) Owner = Document.Selection.Bodies.front();   // Whole-body verbs on a picked body
    // Whole-body verbs (offset body, heal) stay at the end: they act on the body as it stands.
    const std::string Placed = WholeBodyCondition || !OwnerProblem.empty() ? std::string() : Owner;
    return RunDirectEdit(Document, Label, Placed, Document.Selection.Faces, [&](FeatureStep& Step) -> std::string
    {
        if (Document.Selection.Faces.empty() && !WholeBodyCondition) return "select one or more faces first";
        if (!Document.Summary.PresentCondition) return "there is no body to change";
        if (!OwnerProblem.empty()) return OwnerProblem;
        const std::string Caption = NextCaption(Document, Stem);
        const std::string StepLabel = "Change#" + std::to_string(NextStepOrdinal(Document.Tree));
        const double      Span = std::max(1.0, 4.0 * std::abs(Amount));
        Document.Tree.Parameters.push_back({ Caption + ".Amount", Amount, std::min(-Span, Amount), std::max(Span, Amount),
                                             Operation == FaceOperationKind::Draft ? "draft angle [rad]" : Operation == FaceOperationKind::Rotate ? "angle [rad]"
                                             : Operation == FaceOperationKind::Scale ? "factor" : "distance" });
        Step.Kind      = FeatureStepKind::FaceOperation;
        Step.Label     = StepLabel;
        Step.Operation = Operation;
        Step.Amount    = ParameterValue(Caption + ".Amount");
        Step.Target    = Owner;
        if (Direction && (Operation == FaceOperationKind::Tweak || Operation == FaceOperationKind::Rotate))
        {
            if (!(Length(*Direction) > 1.0e-12)) return "the direction is zero";
            Step.Points = { Normalize(*Direction) };
        }
        for (const std::string& Name : Document.Selection.Faces) Step.References.push_back({ ReferenceKind::Face, Name, {} });
        Document.Captions[StepLabel] = Caption;
        return {};
    });
}

SketcherCommandOutcome ReshapeSketcherSelection(SketcherDocument& Document, const AffineMap& Map, const std::string& Verb)
{
    const SketcherSelection& Selection = Document.Selection;
    const size_t Count = Selection.Vertices.size() + Selection.Edges.size() + Selection.Faces.size();
    const char*  Noun  = (!Selection.Faces.empty() && Selection.Vertices.empty() && Selection.Edges.empty())    ? " face(s)"
                       : (!Selection.Edges.empty() && Selection.Vertices.empty() && Selection.Faces.empty())    ? " edge(s)"
                       : (!Selection.Vertices.empty() && Selection.Edges.empty() && Selection.Faces.empty())    ? " vertex(es)"
                                                                                                                : " sub-element(s)";
    const std::string Label = Verb + " " + std::to_string(Count) + Noun;
    // The owner carries every picked vertex, edge, and face.
    std::string Owner, Problem;
    if (Count == 0) Problem = "pick vertices (1), edges (2), or faces (3) first";
    else if (!Document.Summary.PresentCondition) Problem = "there is no body to reshape";
    else
    {
        const auto MergeOwner = [&](SketcherSelectionKind Kind, const std::vector<std::string>& Items)
        {
            if (!Problem.empty() || Items.empty()) return;
            std::string PartOwner;
            Problem = CommonBody(Document, Kind, Items, PartOwner);
            if (!Problem.empty()) return;
            if (!Owner.empty() && Owner != PartOwner) Problem = "the picked elements are on different bodies (one body at a time)";
            else Owner = PartOwner;
        };
        MergeOwner(SketcherSelectionKind::Face, Selection.Faces);
        MergeOwner(SketcherSelectionKind::Edge, Selection.Edges);
        MergeOwner(SketcherSelectionKind::Vertex, Selection.Vertices);
    }
    std::vector<std::string> Names = Selection.Vertices;
    Names.insert(Names.end(), Selection.Edges.begin(), Selection.Edges.end());
    Names.insert(Names.end(), Selection.Faces.begin(), Selection.Faces.end());
    return RunDirectEdit(Document, Label, Problem.empty() ? Owner : std::string(), Names, [&](FeatureStep& Step) -> std::string
    {
        if (!Problem.empty()) return Problem;
        const std::string StepLabel = "Change#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Kind      = FeatureStepKind::FaceOperation;
        Step.Label     = StepLabel;
        Step.Operation = FaceOperationKind::Reshape;
        Step.Placement = Map;
        Step.Target    = Owner;
        for (const std::string& Name : Document.Selection.Vertices) Step.References.push_back({ ReferenceKind::Vertex, Name, {} });
        for (const std::string& Name : Document.Selection.Edges) Step.References.push_back({ ReferenceKind::Edge, Name, {} });
        for (const std::string& Name : Document.Selection.Faces) Step.References.push_back({ ReferenceKind::Face, Name, {} });
        Document.Captions[StepLabel] = NextCaption(Document, "Reshape");
        return {};
    });
}

SketcherCommandOutcome InsetSketcherSelection(SketcherDocument& Document, double Distance, double Depth)
{
    const std::string Label = "Inset " + FormatAmount(Distance) + (Depth != 0.0 ? " depth " + FormatAmount(Depth) : std::string()) + " on " +
                              std::to_string(Document.Selection.Faces.size()) + " face(s)";
    return RunCommand(Document, Label, true, [&]() -> std::string
    {
        if (Document.Selection.Faces.empty()) return "select one or more planar faces first";
        if (!Document.Summary.PresentCondition) return "there is no body to change";
        std::string Owner;
        if (const std::string Problem = CommonBody(Document, SketcherSelectionKind::Face, Document.Selection.Faces, Owner); !Problem.empty()) return Problem;
        const std::string Caption   = NextCaption(Document, "Inset");
        const std::string StepLabel = "Change#" + std::to_string(NextStepOrdinal(Document.Tree));
        const double      Span      = std::max(1.0, 4.0 * std::max(std::abs(Distance), std::abs(Depth)));
        Document.Tree.Parameters.push_back({ Caption + ".Amount", Distance, 0.0, std::max(Span, Distance), "inset distance" });
        Document.Tree.Parameters.push_back({ Caption + ".Depth", Depth, std::min(-Span, Depth), std::max(Span, Depth), "depth (− pocket)" });
        FeatureStep Step;
        Step.Kind      = FeatureStepKind::FaceOperation;
        Step.Label     = StepLabel;
        Step.Operation = FaceOperationKind::Inset;
        Step.Amount    = ParameterValue(Caption + ".Amount");
        Step.Extent    = ParameterValue(Caption + ".Depth");
        Step.Target    = Owner;
        for (const std::string& Name : Document.Selection.Faces) Step.References.push_back({ ReferenceKind::Face, Name, {} });
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Captions[StepLabel] = Caption;
        return {};
    });
}

SketcherCommandOutcome AssignSketcherParameter(SketcherDocument& Document, const std::string& Name, double Value)
{
    return RunCommand(Document, Name + " = " + FormatAmount(Value), false, [&]() -> std::string
    {
        if (!AssignFeatureParameter(Document.Tree, Name, Value)) return "no parameter named " + Name;
        return {};
    });
}

SketcherCommandOutcome DeleteSketcherStep(SketcherDocument& Document, const std::string& Label)
{
    // 📝 A construct tile is one sketch + one sweep: deleting either removes both (and the tile's parameters).
    const auto Found = Document.Captions.find(Label);
    const std::string Caption = Found != Document.Captions.end() ? Found->second : Label;
    return RunCommand(Document, "Delete " + Caption, false, [&]() -> std::string
    {
        std::set<std::string> Doomed;
        for (const FeatureStep& Step : Document.Tree.Steps)
        {
            const auto Entry = Document.Captions.find(Step.Label);
            if (Step.Label == Label || (Entry != Document.Captions.end() && Entry->second == Caption)) Doomed.insert(Step.Label);
        }
        if (Doomed.empty()) return "no step labelled " + Label;
        for (const FeatureStep& Step : Document.Tree.Steps) if (!Step.Profile.empty() && Doomed.contains(Step.Profile)) Doomed.insert(Step.Label);   // Sweeps of a deleted sketch go too
        std::erase_if(Document.Tree.Steps, [&](const FeatureStep& Step) { return Doomed.contains(Step.Label); });
        std::erase_if(Document.Tree.Parameters, [&](const FeatureParameter& Parameter) { return Parameter.Name.rfind(Caption + ".", 0) == 0; });
        for (const std::string& DoomedLabel : Doomed) Document.Captions.erase(DoomedLabel);
        return {};
    });
}

SketcherCommandOutcome ReplaceSketcherTree(SketcherDocument& Document, FeatureTree Tree, std::map<std::string, std::string> Captions,
                                           const std::string& Label)
{
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        Document.Tree     = std::move(Tree);
        Document.Captions = std::move(Captions);
        Document.Selection = {};
        return {};
    });
}

std::string ComposeSketcherSketch(const SketcherDrawingRequest& Request, SketchModel& Sketch)
{
    Sketch = {};
    const std::vector<Vector2>& Points = Request.Points;
    const char*  Tool    = SketcherDrawingLabel(Request.Kind);
    double       Extent  = 0.0;                                                                // [len] - Click spread
    for (const Vector2& First : Points) for (const Vector2& Second : Points) Extent = std::max(Extent, Distance(First, Second));
    const double Join    = 1.0e-9 * std::max(Extent, 1.0);                                     // [len] - Coincidence distance
    auto Needs = [&](size_t Count) -> std::string
    {
        return Points.size() < Count ? std::string(Tool) + " needs " + std::to_string(Count) + " points" : std::string{};
    };
    switch (Request.Kind)
    {
    case SketcherDrawingKind::Line:
    {
        if (std::string Refusal = Needs(Request.ClosedCondition ? 3 : 2); !Refusal.empty()) return Refusal;
        std::vector<int> Indices;
        for (const Vector2& Point : Points) Indices.push_back(AppendSketchPoint(Sketch, Point));
        const size_t Count = Request.ClosedCondition ? Indices.size() : Indices.size() - 1;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const int Start = Indices[Index], End = Indices[(Index + 1) % Indices.size()];
            if (Distance(Points[Index], Points[(Index + 1) % Points.size()]) <= Join) return "Line points " + std::to_string(Index + 1) + " and " + std::to_string((Index + 1) % Points.size() + 1) + " coincide";
            AppendSketchLine(Sketch, Start, End, static_cast<int>(Index) + 1);
        }
        return {};
    }
    case SketcherDrawingKind::Rectangle:
    {
        if (std::string Refusal = Needs(2); !Refusal.empty()) return Refusal;
        const Vector2 First  = Request.CentreCondition ? Points[0] * 2.0 - Points[1] : Points[0];
        const Vector2 Second = Points[1];
        const Vector2 Low{ std::min(First.X, Second.X), std::min(First.Y, Second.Y) }, High{ std::max(First.X, Second.X), std::max(First.Y, Second.Y) };
        if (High.X - Low.X <= Join || High.Y - Low.Y <= Join) return "Rectangle has no width or no height";
        AppendSketchRectangle(Sketch, Low, High, 1);
        return {};
    }
    case SketcherDrawingKind::Circle:
    {
        if (std::string Refusal = Needs(2); !Refusal.empty()) return Refusal;
        const Vector2 Centre = Request.CentreCondition ? Points[0] : (Points[0] + Points[1]) * 0.5;
        const double  Radius = Request.CentreCondition ? Distance(Points[0], Points[1]) : 0.5 * Distance(Points[0], Points[1]);
        if (Radius <= Join) return "Circle has no radius";
        AppendSketchCircle(Sketch, AppendSketchPoint(Sketch, Centre), Radius, 1);
        return {};
    }
    case SketcherDrawingKind::Arc:
    {
        // Circle through start, end and the third click; counter-clockwise from start to end when start → third → end
        // turns left (orientation > 0), else stored from end to start (the sketch arc is always counter-clockwise).
        if (std::string Refusal = Needs(3); !Refusal.empty()) return Refusal;
        const Vector2 Start = Points[0], End = Points[1], Through = Points[2];
        const double  Turn  = (Through.X - Start.X) * (End.Y - Start.Y) - (Through.Y - Start.Y) * (End.X - Start.X);   // [len²] - orient(Start, Through, End)
        if (std::abs(Turn) <= Join * std::max(Extent, 1.0)) return "Arc points are collinear";
        const double  Scale = 2.0 * (Start.X * (Through.Y - End.Y) + Through.X * (End.Y - Start.Y) + End.X * (Start.Y - Through.Y));
        const double  StartSquare = Start.X * Start.X + Start.Y * Start.Y, ThroughSquare = Through.X * Through.X + Through.Y * Through.Y, EndSquare = End.X * End.X + End.Y * End.Y;
        const Vector2 Centre{ (StartSquare * (Through.Y - End.Y) + ThroughSquare * (End.Y - Start.Y) + EndSquare * (Start.Y - Through.Y)) / Scale,
                              (StartSquare * (End.X - Through.X) + ThroughSquare * (Start.X - End.X) + EndSquare * (Through.X - Start.X)) / Scale };
        const int CentreIndex = AppendSketchPoint(Sketch, Centre);
        const int StartIndex  = AppendSketchPoint(Sketch, Start), EndIndex = AppendSketchPoint(Sketch, End);
        if (Turn > 0.0) AppendSketchArc(Sketch, CentreIndex, StartIndex, EndIndex, 1);
        else AppendSketchArc(Sketch, CentreIndex, EndIndex, StartIndex, 1);
        return {};
    }
    case SketcherDrawingKind::Slot:
    {
        // Stadium: caps centred on the first two clicks, half-width = distance of the third click from their line.
        if (std::string Refusal = Needs(3); !Refusal.empty()) return Refusal;
        const Vector2 First = Points[0], Second = Points[1];
        const double  Length = Distance(First, Second);
        if (Length <= Join) return "Slot cap centres coincide";
        const Vector2 Along  = (Second - First) * (1.0 / Length);
        const Vector2 Left{ -Along.Y, Along.X };
        const double  Radius = std::abs((Points[2].X - First.X) * Left.X + (Points[2].Y - First.Y) * Left.Y);
        if (Radius <= Join) return "Slot has no width";
        const int FirstCentre = AppendSketchPoint(Sketch, First), SecondCentre = AppendSketchPoint(Sketch, Second);
        const int TopFirst    = AppendSketchPoint(Sketch, First + Left * Radius), TopSecond = AppendSketchPoint(Sketch, Second + Left * Radius);
        const int BottomFirst = AppendSketchPoint(Sketch, First - Left * Radius), BottomSecond = AppendSketchPoint(Sketch, Second - Left * Radius);
        const int Bottom = AppendSketchLine(Sketch, BottomFirst, BottomSecond, 1);
        const int Cap    = AppendSketchArc(Sketch, SecondCentre, BottomSecond, TopSecond, 2);
        const int Top    = AppendSketchLine(Sketch, TopSecond, TopFirst, 3);
        const int Back   = AppendSketchArc(Sketch, FirstCentre, TopFirst, BottomFirst, 4);
        AppendSketchConstraint(Sketch, Kind::Tangent, Bottom, Cap, -1, 0.0, "T1");
        AppendSketchConstraint(Sketch, Kind::Tangent, Top, Cap, -1, 0.0, "T2");
        AppendSketchConstraint(Sketch, Kind::Tangent, Top, Back, -1, 0.0, "T3");
        AppendSketchConstraint(Sketch, Kind::Tangent, Bottom, Back, -1, 0.0, "T4");
        return {};
    }
    case SketcherDrawingKind::Polygon:
    {
        // Regular polygon: the first click is the centre, the second a vertex (size and turn); Sides closed lines.
        if (std::string Refusal = Needs(2); !Refusal.empty()) return Refusal;
        if (Request.Sides < 3 || Request.Sides > 64) return "Polygon needs 3 … 64 sides";
        const Vector2 Centre = Points[0];
        const double  Radius = Distance(Points[0], Points[1]);
        if (Radius <= Join) return "Polygon has no radius";
        const double  Turn = std::atan2(Points[1].Y - Centre.Y, Points[1].X - Centre.X);
        std::vector<int> Corners;
        for (int Index = 0; Index < Request.Sides; ++Index)
        {
            const double θ = Turn + 2.0 * Pi * Index / Request.Sides;
            Corners.push_back(AppendSketchPoint(Sketch, { Centre.X + Radius * std::cos(θ), Centre.Y + Radius * std::sin(θ) }));
        }
        for (int Index = 0; Index < Request.Sides; ++Index) AppendSketchLine(Sketch, Corners[Index], Corners[(Index + 1) % Request.Sides], Index + 1);
        return {};
    }
    case SketcherDrawingKind::Curve:
    {
        if (std::string Refusal = Needs(Request.ClosedCondition ? 3 : 2); !Refusal.empty()) return Refusal;
        std::vector<int> Indices;
        for (const Vector2& Point : Points) Indices.push_back(AppendSketchPoint(Sketch, Point));
        if (Request.ClosedCondition) Indices.push_back(Indices.front());
        const int Curve = AppendSketchSpline(Sketch, Indices, Request.Form, 1);
        const SketchSplineOutcome Resolved = ResolveSketchCurveSpline(Sketch, Sketch.Curves[Curve]);
        return Resolved.Refusal;
    }
    }
    return "unknown drawing tool";
}

SketcherCommandOutcome DrawSketcherSketch(SketcherDocument& Document, const SketcherDrawingRequest& Request)
{
    const std::string Stem    = Request.Kind == SketcherDrawingKind::Curve ? std::string("Curve") : SketcherDrawingLabel(Request.Kind);
    const std::string Caption = NextCaption(Document, Stem);
    const char*       FormTag = Request.Kind == SketcherDrawingKind::Curve ? SketchSplineFormLabel(Request.Form) : nullptr;
    return RunCommand(Document, Caption + (FormTag ? std::string(" · ") + FormTag : std::string{}), false, [&]() -> std::string
    {
        if (Request.GridPlane < 0 || Request.GridPlane > 2) return "construction plane must be XY, XZ or YZ";
        SketchModel Sketch;
        if (std::string Refusal = ComposeSketcherSketch(Request, Sketch); !Refusal.empty()) return Refusal;
        FeatureStep Step;
        Step.Kind   = FeatureStepKind::Sketch;
        Step.Label  = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Plane  = SketcherSketchPlane(Request.GridPlane);
        if (Request.PlaneCondition)
        {
            // Drawn on a body face: the face plane (orthonormal, finite) carries the sketch.
            const OrthonormalFrame& Face = Request.Plane;
            const bool Finite = std::isfinite(Face.Origin.X) && std::isfinite(Face.Origin.Y) && std::isfinite(Face.Origin.Z);
            const bool Square = std::abs(Length(Face.XAxis) - 1.0) < 1.0e-9 && std::abs(Length(Face.YAxis) - 1.0) < 1.0e-9 && std::abs(Length(Face.ZAxis) - 1.0) < 1.0e-9
                             && std::abs(Dot(Face.XAxis, Face.YAxis)) < 1.0e-9 && Length(Cross(Face.XAxis, Face.YAxis) - Face.ZAxis) < 1.0e-9;
            if (!Finite || !Square) return "the face plane is not an orthonormal right-handed frame";
            Step.Plane = Face;
        }
        Step.Sketch        = std::move(Sketch);
        Step.FillCondition = Request.FillCondition;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

SketcherCommandOutcome FillSketcherSketch(SketcherDocument& Document, const std::string& Sketch, bool FillCondition)
{
    const auto Source = Document.Captions.find(Sketch);
    const std::string Caption = Source != Document.Captions.end() ? Source->second : Sketch;
    return RunCommand(Document, std::string(FillCondition ? "Fill " : "Unfill ") + Caption, false, [&]() -> std::string
    {
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Sketch; });
        if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::Sketch) return "no sketch step labelled " + Sketch;
        if (Found->FillCondition == FillCondition) return Caption + (FillCondition ? " is already filled" : " is already unfilled");
        if (FillCondition)
        {
            const auto Solved = Document.Regeneration.Sketches.find(Sketch);
            if (Solved == Document.Regeneration.Sketches.end()) return Caption + " did not solve";
            const SketchProfile Profile = ExtractSketchProfile(Solved->second);
            if (!Profile.Refusal.empty()) return Caption + " cannot be filled: " + Profile.Refusal;
        }
        Found->FillCondition = FillCondition;
        return {};
    });
}

SketcherCommandOutcome FlipSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys)
{
    std::vector<std::string> Unique;
    for (const std::string& Key : Keys) if (std::find(Unique.begin(), Unique.end(), Key) == Unique.end()) Unique.push_back(Key);
    std::string Operands;
    for (const std::string& Key : Unique) Operands += (Operands.empty() ? "" : ", ") + SketcherBodyCaption(Document, Key);
    return RunCommand(Document, "Flip normals (" + Operands + ")", false, [&]() -> std::string
    {
        if (Unique.empty()) return "pick the sheets to flip (mode 4)";
        for (const std::string& Key : Unique)
        {
            const RegeneratedBody* Body = FindRegeneratedBody(Document, Key);
            if (!Body) return "no body named " + Key;
            if (Document.Partition && Document.Partition->Bodies.Resolve(Body->Body).Category != BodyCategory::Sheet)
                return SketcherBodyCaption(Document, Key) + " is a solid: only sheets (lofts, walls) turn over";
        }
        for (const std::string& Key : Unique)
        {
            // ① a whole-body Flip that is the body's last step is undone instead of stacking a second one. A face Flip is
            //    not: turning the whole object after some faces must keep those faces' turn (they come back to the
            //    original side while the rest turns), and removing the step undid the face flip instead.
            auto Last = Document.Tree.Steps.end();
            for (auto Step = Document.Tree.Steps.begin(); Step != Document.Tree.Steps.end(); ++Step)
                if (Step->Target == Key || Step->Label == Key) Last = Step;
            if (Last != Document.Tree.Steps.end() && Last->Kind == FeatureStepKind::Flip && Last->Target == Key && Last->References.empty())
            {
                Document.Captions.erase(Last->Label);
                Document.Tree.Steps.erase(Last);
                continue;
            }
            // ② else a new Flip step
            FeatureStep Step;
            Step.Kind   = FeatureStepKind::Flip;
            Step.Label  = "Flip#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Target = Key;
            Document.Captions[Step.Label] = NextCaption(Document, "Flip");
            Document.Tree.Steps.push_back(std::move(Step));
        }
        return {};
    });
}

SketcherCommandOutcome FlipSketcherFaces(SketcherDocument& Document, const std::vector<std::string>& Faces)
{
    // Faces grouped by their body, in pick order.
    std::vector<std::pair<std::string, std::vector<std::string>>> Groups;
    for (const std::string& Face : Faces)
    {
        const std::string Key = SketcherBodyOfName(Document, SketcherSelectionKind::Face, Face);
        auto Group = std::find_if(Groups.begin(), Groups.end(), [&](const auto& Entry) { return Entry.first == Key; });
        if (Group == Groups.end()) { Groups.push_back({ Key, {} }); Group = Groups.end() - 1; }
        if (std::find(Group->second.begin(), Group->second.end(), Face) == Group->second.end()) Group->second.push_back(Face);
    }
    std::string Operands;
    for (const auto& [Key, Named] : Groups) Operands += (Operands.empty() ? "" : ", ") + std::to_string(Named.size()) + " face(s) of " + (Key.empty() ? std::string("?") : SketcherBodyCaption(Document, Key));
    return RunCommand(Document, "Flip normals (" + Operands + ")", false, [&]() -> std::string
    {
        if (Groups.empty()) return "pick the faces to flip (mode 3)";
        for (const auto& [Key, Named] : Groups)
        {
            const RegeneratedBody* Body = Key.empty() ? nullptr : FindRegeneratedBody(Document, Key);
            if (!Body) return "face " + Named.front() + " is on no body";
            if (Document.Partition && Document.Partition->Bodies.Resolve(Body->Body).Category != BodyCategory::Sheet)
                return SketcherBodyCaption(Document, Key) + " is a solid: only sheets (lofts, walls) turn over";
        }
        for (const auto& [Key, Named] : Groups)
        {
            // ① the body's last step flipping exactly these faces is undone instead of stacking a second one
            auto Last = Document.Tree.Steps.end();
            for (auto Step = Document.Tree.Steps.begin(); Step != Document.Tree.Steps.end(); ++Step)
                if (Step->Target == Key || Step->Label == Key) Last = Step;
            if (Last != Document.Tree.Steps.end() && Last->Kind == FeatureStepKind::Flip && Last->Target == Key && Last->References.size() == Named.size()
                && std::all_of(Last->References.begin(), Last->References.end(), [&](const TopologyReference& Reference)
                               { return std::find(Named.begin(), Named.end(), Reference.Name) != Named.end(); }))
            {
                Document.Captions.erase(Last->Label);
                Document.Tree.Steps.erase(Last);
                continue;
            }
            // ② else a new Flip step naming the faces
            FeatureStep Step;
            Step.Kind   = FeatureStepKind::Flip;
            Step.Label  = "Flip#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Target = Key;
            for (const std::string& Name : Named) Step.References.push_back({ ReferenceKind::Face, Name, {} });
            Document.Captions[Step.Label] = NextCaption(Document, "Flip");
            Document.Tree.Steps.push_back(std::move(Step));
        }
        return {};
    });
}

SketcherCommandOutcome ExtrudeSketcherSketch(SketcherDocument& Document, const std::string& SketchLabel, double Distance, FeatureCombination Combination,
                                             const std::string& Target, double Start, bool FillCondition)
{
    const bool        FirstBodyCondition = !Document.Summary.PresentCondition && Combination != FeatureCombination::Separate;
    const std::string Caption = NextCaption(Document, "Extrude");
    const auto        Source  = Document.Captions.find(SketchLabel);
    const std::string Label   = Caption + " · " + (Source != Document.Captions.end() ? Source->second : SketchLabel) + " · " +
                                (FirstBodyCondition ? "New" : FeatureCombinationLabel(Combination));
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == SketchLabel; });
        if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::Sketch) return "no sketch step labelled " + SketchLabel;
        if (!(std::abs(Distance) > 1.0e-9)) return "extrude distance is zero";
        if (FirstBodyCondition && (Combination == FeatureCombination::Difference || Combination == FeatureCombination::Intersection))
            return std::string("nothing to ") + (Combination == FeatureCombination::Difference ? "subtract from" : "intersect with") + " yet (no body)";
        if (!FirstBodyCondition && Combination == FeatureCombination::New) return "a second body needs Union, Difference, Intersection or New body";
        if (!Target.empty() && !FindRegeneratedBody(Document, Target)) return "no body named " + Target;
        if (Start * Distance > 0.0) return "an extrude start lies on or behind the sketch plane (opposite the distance)";
        // A cut of zero-thickness walls means nothing: the region it cuts is filled first (as the knife does).
        if (FillCondition && !FirstBodyCondition && (Combination == FeatureCombination::Difference || Combination == FeatureCombination::Intersection)) Found->FillCondition = true;
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Extrude;
        if (Start != 0.0) Step.Start = FeatureValue{ Start };
        Step.Label       = "Extrude#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Profile     = SketchLabel;
        Step.Extent      = FeatureValue{ Distance };
        Step.Combination = FirstBodyCondition ? FeatureCombination::New : Combination;
        Step.Target      = Target;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

OrthonormalFrame SketcherSketchPlane(int GridPlane) noexcept
{
    OrthonormalFrame Plane;
    if (GridPlane == 1) return UprightPlane();
    if (GridPlane == 2)
    {
        Plane.XAxis = { 0.0, 1.0, 0.0 };
        Plane.YAxis = { 0.0, 0.0, 1.0 };
        Plane.ZAxis = { 1.0, 0.0, 0.0 };
    }
    return Plane;
}

bool SketcherSketchConsumedCondition(const SketcherDocument& Document, const std::string& SketchLabel)
{
    return std::any_of(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Profile == SketchLabel; });
}

bool SketcherSketchReferencedCondition(const SketcherDocument& Document, const std::string& SketchLabel)
{
    const bool ByStep = std::any_of(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step)
    {
        return Step.Label != SketchLabel && (Step.CurveOffsetGuide == SketchLabel || Step.Profile == SketchLabel || Step.Path == SketchLabel ||
               std::find(Step.Sections.begin(), Step.Sections.end(), SketchLabel) != Step.Sections.end());
    });
    if (ByStep) return true;
    return std::any_of(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(), [&](const FeatureModifier& Modifier)
    {
        return Modifier.Curve == SketchLabel;
    });
}

SketcherCommandOutcome ImportSketcherCurves(SketcherDocument& Document, const VehicleCurveFile& File, double Tolerance)
{
    const std::string Label = "Import " + std::to_string(File.Curves.size()) + " curve(s)";
    SketcherCommandOutcome Outcome = RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (!File.Refusal.empty()) return "import: " + File.Refusal;
        if (File.Curves.empty()) return "import: the file holds no curves";
        if (!(Tolerance > 0.0)) return "import: the fit tolerance must be positive";
        for (const VehicleCurve& Curve : File.Curves)
        {
            FeatureStep Step;
            Step.Kind            = FeatureStepKind::SpaceCurve;
            Step.Label           = "Curve#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Points          = Curve.Points;
            Step.ClosedCondition = Curve.ClosedCondition;
            Step.PlanarCondition = Curve.PlanarCondition;
            Step.Tolerance       = FeatureValue{ Tolerance };
            const bool Taken = std::any_of(Document.Captions.begin(), Document.Captions.end(), [&](const auto& Entry) { return Entry.second == Curve.Name; });
            Document.Captions[Step.Label] = Taken ? NextCaption(Document, Curve.Name + ".") : Curve.Name;
            Document.Tree.Steps.push_back(std::move(Step));
        }
        return {};
    });
    // 📝 Image planes ride along with an accepted import (replaced by name); they are references, not history.
    if (Outcome.AcceptedCondition)
    {
        for (const SketcherImagePlane& Plane : File.ImagePlanes)
        {
            const auto Same = std::find_if(Document.ImagePlanes.begin(), Document.ImagePlanes.end(), [&](const SketcherImagePlane& Held) { return Held.Name == Plane.Name; });
            if (Same != Document.ImagePlanes.end()) *Same = Plane;
            else Document.ImagePlanes.push_back(Plane);
        }
    }
    return Outcome;
}

SketcherCommandOutcome ImportSketcherStep(SketcherDocument& Document, const SketcherStepReading& Reading)
{
    const std::string Label = "Import " + Reading.Stem + " · " + std::to_string(Reading.Texts.size()) + " solid(s)";
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (!Reading.Refusal.empty()) return Reading.Refusal;
        if (Reading.Texts.empty()) return "Import " + Reading.Stem + ": the file holds no solid";
        for (size_t Index = 0; Index < Reading.Texts.size(); ++Index)
        {
            FeatureStep Step;
            Step.Kind         = FeatureStepKind::Import;
            Step.Label        = "Import#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Exchange     = Reading.Texts[Index];
            Step.ExchangeUnit = SketcherModelUnitMillimetres;
            const std::string Wanted = Reading.Texts.size() == 1 ? Reading.Stem : Reading.Stem + "." + std::to_string(Index + 1);
            const bool Taken = std::any_of(Document.Captions.begin(), Document.Captions.end(), [&](const auto& Entry) { return Entry.second == Wanted; });
            Document.Captions[Step.Label] = Taken ? NextCaption(Document, Wanted + ".") : Wanted;
            Document.Tree.Steps.push_back(std::move(Step));
        }
        return {};
    });
}

SketcherCommandOutcome LoftSketcherCurves(SketcherDocument& Document, const std::vector<std::string>& Labels, int Continuity, bool SolidCondition)
{
    // Curves that meet each other make a network; otherwise they are sections.
    std::vector<NurbsCurve> Fitted;
    for (const std::string& Name : Labels)
    {
        const auto Found = Document.Regeneration.Curves.find(Name);
        if (Found != Document.Regeneration.Curves.end()) Fitted.push_back(Found->second.Curve);
    }
    std::vector<NurbsCurve> NormalSections;
    for(const auto& Name:Labels)
    {
        if(auto Curve=Document.Regeneration.Curves.find(Name);Curve!=Document.Regeneration.Curves.end()) NormalSections.push_back(Curve->second.Curve);
        else if(auto Model=Document.Regeneration.Sketches.find(Name);Model!=Document.Regeneration.Sketches.end())
        {
            auto Step=std::find_if(Document.Tree.Steps.begin(),Document.Tree.Steps.end(),[&](const auto& S){return S.Label==Name;});
            if(Step!=Document.Tree.Steps.end())
            {
                auto Plane=Step->Plane;Plane.Origin=Plane.Origin+Plane.ZAxis*EvaluateFeatureValue(Document.Tree,Step->Elevation);
                const auto Section=ComposeSketchSection(Model->second,Plane);
                if(Section.Curve) NormalSections.push_back(*Section.Curve);
            }
        }
    }
    const bool        NetworkCondition = Fitted.size() == Labels.size() && Fitted.size() >= 4 && DetectCurveNetwork(Fitted, NetworkSettings{});
    const std::string Caption = NextCaption(Document, NetworkCondition ? "Network" : "Loft");
    const std::string Label   = Caption + " · " + std::to_string(Labels.size()) + (NetworkCondition ? " curves" : " sections")
                              + (Continuity > 0 ? " · G" + std::to_string(Continuity) : std::string()) + (SolidCondition && !NetworkCondition ? " · solid" : "");
    SketcherCommandOutcome Outcome = RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (Labels.size() < 2) return "loft needs at least 2 curves (select them in section order)";
        for (size_t Index = 0; Index < Labels.size(); ++Index)
        {
            const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Labels[Index]; });
            if (Found == Document.Tree.Steps.end() || (Found->Kind != FeatureStepKind::SpaceCurve && Found->Kind != FeatureStepKind::Sketch)) return "no curve or sketch step labelled " + Labels[Index];
            if (std::find(Labels.begin(), Labels.begin() + Index, Labels[Index]) != Labels.begin() + Index) return "curve " + Labels[Index] + " is selected twice";
        }
        if (Continuity < 0 || Continuity > 2) return "continuity must be G0, G1 or G2";
        if (NetworkCondition && Continuity > 0) return "these curves cross (a network): G1 / G2 is available for section lofts only (🚧 networks)";
        if (NetworkCondition && SolidCondition) return "these curves cross (a network): a solid loft needs closed sections in order";
        if (SolidCondition && Continuity > 0) return "a solid loft is G0 (G1 / G2 join a loft to neighbouring panels)";
        FeatureStep Step;
        Step.Kind       = NetworkCondition ? FeatureStepKind::Network : FeatureStepKind::Loft;
        Step.Label      = (NetworkCondition ? "Network#" : "Loft#") + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Sections   = Labels;
        // An open sheet has no intrinsic outside. Match the locked tracing reference,
        // otherwise use the positive dominant normal, independently of pick order.
        if (!SolidCondition && NormalSections.size() == Labels.size())
        {
            std::optional<NurbsSurface> Preview;
            if(NetworkCondition) {const auto N=ConstructNetworkSurface(NormalSections,NetworkSettings{},Labels);if(N.Refusal.empty()) Preview=N.Surface;}
            else {const auto L=LoftSectionSurface(NormalSections,LoftSettings{});if(L.Refusal.empty()) Preview=L.Surface;}
            if (Preview)
            {
                const auto Sample = EvaluateNurbsSurface(*Preview, {0.5,0.5});
                const auto Normal = Cross(Sample.DerivativeU, Sample.DerivativeV);
                Vector3 Expected{};
                double Nearest = INFINITY;
                const auto Scene = BuildSketcherScene(Document);
                for (const auto& Corner : Scene.Corners)
                {
                    const auto& Face = Scene.Faces[Corner.Face];
                    if (!Document.Appearance.LockedFigures.contains(Scene.Bodies[Face.Body].Key)) continue;
                    const Vector3 Position{Corner.Position[0],Corner.Position[1],Corner.Position[2]};
                    const double Gap = Distance(Position, Sample.Position);
                    if (Gap < Nearest) { Nearest = Gap; Expected = {Corner.Normal[0],Corner.Normal[1],Corner.Normal[2]}; }
                }
                if(Length(Expected)<1e-12 && !NetworkCondition)
                {
                    bool Closed=true;Vector3 Centre{};size_t Count=0;
                    for(const auto& Curve:NormalSections)
                    {
                        const auto& P=Curve.Definition->Poles;
                        Closed=Closed && P.size()>2 && Distance(P.front(),P.back())<1e-9;
                        for(const auto& Point:P) {Centre=Centre+Point;++Count;}
                    }
                    if(Closed && Count) Expected=Sample.Position-Centre*(1.0/double(Count)); // Tube: radial outside, not an arbitrary hemisphere.
                }
                if (Length(Expected) < 1e-12)
                {
                    const double X=std::abs(Normal.X), Y=std::abs(Normal.Y), Z=std::abs(Normal.Z);
                    Expected = Z>=X && Z>=Y ? Vector3{0,0,1} : Y>=X ? Vector3{0,1,0} : Vector3{1,0,0};
                }
                Step.ReverseNormals=Dot(Normal,Expected)<0;
            }
        }
        Step.Continuity = Continuity;
        Step.ClosedCondition = SolidCondition;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
    if (Outcome.AcceptedCondition) Document.Selection.Curves.clear();
    return Outcome;
}

SketcherCommandOutcome LoftSketcherSelection(SketcherDocument& Document, int Continuity, bool SolidCondition)
{
    if (!Document.Partition) return LoftSketcherCurves(Document, Document.Selection.Curves, Continuity, SolidCondition);
    std::vector<std::string> SelectedEdges = Document.Selection.Edges;
    if (SelectedEdges.empty() && Document.Selection.Curves.size() == 1
        && (!Document.Selection.Bodies.empty() || !Document.Selection.Faces.empty() || !Document.Selection.PickedBody.empty()))
    {
        const std::string& TargetCurve = Document.Selection.Curves.front();
        const auto         TargetFit   = Document.Regeneration.Curves.find(TargetCurve);
        if (TargetFit != Document.Regeneration.Curves.end() && TargetFit->second.Curve.Definition && TargetFit->second.Curve.Definition->Poles.size() >= 2)
        {
            const Vector3 StartC = TargetFit->second.Curve.Definition->Poles.front();
            const Vector3 EndC   = TargetFit->second.Curve.Definition->Poles.back();
            std::string SheetKey = !Document.Selection.Bodies.empty() ? Document.Selection.Bodies.front() : Document.Selection.PickedBody;
            if (SheetKey.empty() && !Document.Selection.Faces.empty())
                SheetKey = SketcherBodyOfName(Document, SketcherSelectionKind::Face, Document.Selection.Faces.front());
            const RegeneratedBody* Owner = FindRegeneratedBody(Document, SheetKey);
            if (Owner && Document.Partition->Bodies.Authenticate(Owner->Body)
                && Document.Partition->Bodies.Resolve(Owner->Body).Category == BodyCategory::Sheet)
            {
                // Prefer an existing Loft section curve that shares an endpoint (V-hinge) with the new curve.
                std::string HingedSection;
                double      BestHingeGap = 1.0e-3;
                for (const FeatureStep& Step : Document.Tree.Steps)
                {
                    if (Step.Kind != FeatureStepKind::Loft) continue;
                    for (const std::string& Section : Step.Sections)
                    {
                        if (Section == TargetCurve) continue;
                        const auto Fit = Document.Regeneration.Curves.find(Section);
                        if (Fit == Document.Regeneration.Curves.end() || !Fit->second.Curve.Definition || Fit->second.Curve.Definition->Poles.size() < 2) continue;
                        const Vector3 SecStart = Fit->second.Curve.Definition->Poles.front();
                        const Vector3 SecEnd   = Fit->second.Curve.Definition->Poles.back();
                        const double  Gap      = std::fmin(std::fmin(Distance(StartC, SecStart), Distance(StartC, SecEnd)),
                                                           std::fmin(Distance(EndC, SecStart),   Distance(EndC, SecEnd)));
                        if (Gap <= BestHingeGap) { BestHingeGap = Gap; HingedSection = Section; }
                    }
                }
                if (!HingedSection.empty())
                {
                    SketcherCommandOutcome Outcome = LoftSketcherCurves(Document, { HingedSection, TargetCurve }, Continuity, SolidCondition);
                    if (Outcome.AcceptedCondition)
                    {
                        Document.Selection.Bodies.clear();
                        Document.Selection.Faces.clear();
                    }
                    return Outcome;
                }
                // Otherwise pick the closest open laminar boundary edge of the selected sheet body.
                const BodyInventory Inventory = Document.Partition->CollectInventory(Owner->Body);
                std::string BestEdge;
                double      BestDist = 1.0e30;
                for (const EdgeToken Edge : Inventory.Edges)
                {
                    const EdgeProfile& Profile = Document.Partition->Edges.Resolve(Edge);
                    if (!Document.Partition->Fins.Authenticate(Profile.RadialFin)
                        || Document.Partition->RadialMate(Profile.RadialFin) != Profile.RadialFin)
                    {
                        continue;
                    }
                    const auto Named = Owner->Naming.Edges.find(Edge.Slot);
                    if (Named == Owner->Naming.Edges.end()) continue;
                    const CurveGeometry& Carrier = Document.Partition->Curves.Resolve(Profile.Curve);
                    const Vector3 Mid = EvaluateCurve(Carrier, 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
                    const double  Score = std::fmin(Distance(StartC, Mid), Distance(EndC, Mid));
                    if (Score < BestDist) { BestDist = Score; BestEdge = Named->second; }
                }
                if (!BestEdge.empty()) SelectedEdges.push_back(BestEdge);
            }
        }
    }
    if (SelectedEdges.empty()) return LoftSketcherCurves(Document, Document.Selection.Curves, Continuity, SolidCondition);

    struct ExtractedEdge
    {
        std::string         ExistingLabel = {};
        std::vector<Vector3> Points       = {};
        std::vector<double>  CurveKnots   = {};
        bool                 ClosedCondition = false;
    };
    std::vector<ExtractedEdge> Pending;
    const ModelPartition& Partition = *Document.Partition;
    for (const std::string& EdgeName : SelectedEdges)
    {
        uint32_t FoundSlot = UINT32_MAX;
        for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
        {
            for (const auto& [Slot, Candidate] : Body.Naming.Edges)
            {
                if (Candidate == EdgeName) { FoundSlot = Slot; break; }
            }
            if (FoundSlot != UINT32_MAX) break;
        }
        if (FoundSlot == UINT32_MAX) continue;
        const EdgeToken Edge = Partition.Edges.TokenAt(FoundSlot);
        if (!Partition.Edges.Authenticate(Edge)) continue;
        const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Carrier = Partition.Curves.Resolve(Profile.Curve);
        constexpr int Samples = 16;
        std::array<Vector3, Samples + 1> EdgePoints;
        for (int Index = 0; Index <= Samples; ++Index)
        {
            const double Parameter = Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * (static_cast<double>(Index) / Samples);
            EdgePoints[Index] = EvaluateCurve(Carrier, Parameter).Position;
        }
        std::string MatchedLabel;
        for (const FeatureStep& Step : Document.Tree.Steps)
        {
            if (Step.Kind != FeatureStepKind::Sketch && Step.Kind != FeatureStepKind::SpaceCurve) continue;
            const auto Fitted = Document.Regeneration.Curves.find(Step.Label);
            if (Fitted == Document.Regeneration.Curves.end() || !Fitted->second.Curve.Definition) continue;
            const NurbsDomain Domain = MeasureNurbsCurveDomain(*Fitted->second.Curve.Definition);
            double SameWorst = 0.0, SwapWorst = 0.0;
            for (int Index = 0; Index <= Samples; ++Index)
            {
                const double Ratio = static_cast<double>(Index) / Samples;
                const Vector3 Forward = EvaluateNurbsCurve(*Fitted->second.Curve.Definition, Domain.Low + (Domain.High - Domain.Low) * Ratio).Position;
                const Vector3 Reverse = EvaluateNurbsCurve(*Fitted->second.Curve.Definition, Domain.High - (Domain.High - Domain.Low) * Ratio).Position;
                SameWorst = std::fmax(SameWorst, Distance(EdgePoints[Index], Forward));
                SwapWorst = std::fmax(SwapWorst, Distance(EdgePoints[Index], Reverse));
            }
            if (std::fmin(SameWorst, SwapWorst) <= 5.0e-5) { MatchedLabel = Step.Label; break; }
        }
        ExtractedEdge Item;
        Item.ExistingLabel = MatchedLabel;
        if (MatchedLabel.empty())
        {
            Item.ClosedCondition = Profile.Start == Profile.End && Distance(EdgePoints.front(), EdgePoints[Samples / 2]) > 5.0e-5;
            if (std::holds_alternative<StraightLine>(Carrier) && !Item.ClosedCondition)
            {
                Item.Points     = { EdgePoints.front(), EdgePoints.back() };
                Item.CurveKnots = { 0.0, 0.0, 1.0, 1.0 };
            }
            else if (const auto* Nurbs = std::get_if<NurbsCurve>(&Carrier); Nurbs && Nurbs->Definition)
            {
                const NurbsDomain Domain = MeasureNurbsCurveDomain(*Nurbs->Definition);
                const bool UnitWeights = std::all_of(Nurbs->Definition->Weights.begin(), Nurbs->Definition->Weights.end(), [](double Weight) { return std::abs(Weight - 1.0) <= 1.0e-12; });
                if (UnitWeights && std::abs(Profile.IntervalLow - Domain.Low) <= 1.0e-9 && std::abs(Profile.IntervalHigh - Domain.High) <= 1.0e-9)
                {
                    Item.Points     = Nurbs->Definition->Poles;
                    Item.CurveKnots = Nurbs->Definition->Knots;
                }
            }
            if (Item.Points.empty())
            {
                constexpr int Dense = 32;
                Item.Points.reserve(Dense + 1);
                for (int Index = 0; Index <= Dense; ++Index)
                {
                    const double Parameter = Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * (static_cast<double>(Index) / Dense);
                    Item.Points.push_back(EvaluateCurve(Carrier, Parameter).Position);
                }
            }
        }
        Pending.push_back(std::move(Item));
    }

    // When every picked edge matched an existing curve step, loft those curve labels directly without adding extra steps.
    bool AllMatched = true;
    for (const ExtractedEdge& Item : Pending) if (Item.ExistingLabel.empty()) AllMatched = false;
    if (AllMatched)
    {
        std::vector<std::string> Labels = Document.Selection.Curves;
        for (const ExtractedEdge& Item : Pending)
            if (std::find(Labels.begin(), Labels.end(), Item.ExistingLabel) == Labels.end()) Labels.push_back(Item.ExistingLabel);
        SketcherCommandOutcome Outcome = LoftSketcherCurves(Document, Labels, Continuity, SolidCondition);
        if (Outcome.AcceptedCondition) Document.Selection.Edges.clear();
        return Outcome;
    }

    const size_t TotalCount = Document.Selection.Curves.size() + Pending.size();
    const std::string Caption = NextCaption(Document, "Loft");
    const std::string Label   = Caption + " · " + std::to_string(TotalCount) + " sections"
                              + (Continuity > 0 ? " · G" + std::to_string(Continuity) : std::string()) + (SolidCondition ? " · solid" : "");
    SketcherCommandOutcome Outcome = RunCommand(Document, Label, false, [&]() -> std::string
    {
        std::vector<std::string> Labels = Document.Selection.Curves;
        for (const ExtractedEdge& Item : Pending)
        {
            if (!Item.ExistingLabel.empty())
            {
                if (std::find(Labels.begin(), Labels.end(), Item.ExistingLabel) == Labels.end()) Labels.push_back(Item.ExistingLabel);
                continue;
            }
            FeatureStep CurveStep;
            CurveStep.Kind            = FeatureStepKind::SpaceCurve;
            CurveStep.Label           = "Curve#" + std::to_string(NextStepOrdinal(Document.Tree));
            CurveStep.Points          = Item.Points;
            CurveStep.CurveKnots      = Item.CurveKnots;
            CurveStep.ClosedCondition = Item.ClosedCondition;
            CurveStep.Tolerance       = { 1.0e-5 };
            Document.Captions[CurveStep.Label] = NextCaption(Document, "Curve");
            Document.Appearance.HiddenCurves.insert(CurveStep.Label);
            Labels.push_back(CurveStep.Label);
            Document.Tree.Steps.push_back(std::move(CurveStep));
        }
        if (Labels.size() < 2) return "loft needs at least 2 curves (select them in section order)";
        if (Continuity < 0 || Continuity > 2) return "continuity must be G0, G1 or G2";
        if (SolidCondition && Continuity > 0) return "a solid loft is G0 (G1 / G2 join a loft to neighbouring panels)";
        FeatureStep Step;
        Step.Kind            = FeatureStepKind::Loft;
        Step.Label           = "Loft#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Sections        = std::move(Labels);
        Step.Continuity      = Continuity;
        Step.ClosedCondition = SolidCondition;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
    if (Outcome.AcceptedCondition)
    {
        Document.Selection.Curves.clear();
        Document.Selection.Edges.clear();
    }
    return Outcome;
}

std::vector<Vector3> SampleThroughCurve(const std::vector<Vector3>& Points, bool ClosedCondition, SketchSplineForm Form, int PerSpan, const std::vector<double>& Knots)
{
    // 📝 Every sketch form is linear in its defining points, C(τ) = Σ wⱼ(τ)·Qⱼ (WeighSketchSpline), so the 3D curve with
    //    the same weights is exactly the sketch curve where the points lie on a plane, and the same form off it.
    std::vector<Vector3> Slots = Points;
    if (ClosedCondition && Slots.size() >= 2 && Distance(Slots.front(), Slots.back()) > 1.0e-12) Slots.push_back(Slots.front());
    const int Count = static_cast<int>(Slots.size());
    if (Count < 2) return Slots;
    const SketchSplineWeights Domain = WeighSketchSpline(Form, Count, ClosedCondition, 0.0, Knots);
    if (!Domain.Refusal.empty()) return {};
    const int Total = std::max(1, Count - 1) * std::max(2, PerSpan);
    std::vector<Vector3> Samples;
    Samples.reserve(static_cast<size_t>(Total) + 1);
    for (int Step = 0; Step <= Total; ++Step)
    {
        const double              τ       = Domain.Low + (Domain.High - Domain.Low) * double(Step) / double(Total);
        const SketchSplineWeights Weights = WeighSketchSpline(Form, Count, ClosedCondition, τ, Knots);
        Vector3 Point;
        for (int Slot = 0; Slot < Count && Slot < int(Weights.Position.size()); ++Slot) Point = Point + Slots[static_cast<size_t>(Slot)] * Weights.Position[static_cast<size_t>(Slot)];
        Samples.push_back(Point);
    }
    if (ClosedCondition) Samples.back() = Samples.front();                                   // Exactly closed
    return Samples;
}

namespace
{
    // 📝 The exact 3D curve of a sketch form through Points: the form's poles are linear in the points too, so two generic
    //    2D shadows A = (x + 0.37 z, y + 0.61 z) and B = (x − 0.53 z, y + 0.29 z), each resolved as a sketch curve (same
    //    knots), give every pole back: z = (A.x − B.x) / 0.9, x = A.x − 0.37 z, y = A.y − 0.61 z. Empty when refused.
    struct ExactThrough { std::vector<Vector3> Poles; std::vector<double> Knots; };
    std::optional<ExactThrough> ResolveExactThrough(const std::vector<Vector3>& Points, bool ClosedCondition, SketchSplineForm Form, const std::vector<double>& Knots)
    {
        std::vector<Vector3> Slots = Points;
        if (ClosedCondition && Slots.size() >= 2 && Distance(Slots.front(), Slots.back()) > 1.0e-12) Slots.push_back(Slots.front());
        if (Slots.size() < 2) return std::nullopt;
        double Size = 0.0;
        for (const Vector3& Point : Slots) Size = std::max(Size, Distance(Point, Slots.front()));
        std::vector<Vector2> First, Second;
        for (const Vector3& Point : Slots)
        {
            First.push_back({ Point.X + 0.37 * Point.Z, Point.Y + 0.61 * Point.Z });
            Second.push_back({ Point.X - 0.53 * Point.Z, Point.Y + 0.29 * Point.Z });
        }
        const SketchSplineOutcome A = ResolveSketchSpline(Form, First, std::max(Size, 1.0e-3), Knots);
        const SketchSplineOutcome B = ResolveSketchSpline(Form, Second, std::max(Size, 1.0e-3), Knots);
        if (!A.Refusal.empty() || !B.Refusal.empty() || A.Curve.Poles.size() != B.Curve.Poles.size() || A.Curve.Knots != B.Curve.Knots) return std::nullopt;
        ExactThrough Exact;
        Exact.Knots = A.Curve.Knots;
        for (size_t Index = 0; Index < A.Curve.Poles.size(); ++Index)
        {
            const double Z = (A.Curve.Poles[Index].X - B.Curve.Poles[Index].X) / 0.9;
            Exact.Poles.push_back({ A.Curve.Poles[Index].X - 0.37 * Z, A.Curve.Poles[Index].Y - 0.61 * Z, Z });
        }
        if (static_cast<int>(Exact.Knots.size()) - static_cast<int>(Exact.Poles.size()) - 1 < 1) return std::nullopt;
        return Exact;
    }

    std::string CaptionOfStep(const SketcherDocument& Document, const std::string& Label)
    {
        const auto Found = Document.Captions.find(Label);
        return Found != Document.Captions.end() ? Found->second : Label;
    }

    // One SpaceCurve step through Samples (the caller's command), fitted to 10⁻⁶ of its size — or, with Exact, the exact
    //    curve itself (its poles and knots, no fit); returns its label.
    std::string AppendSpaceCurveStep(SketcherDocument& Document, std::vector<Vector3> Samples, bool ClosedCondition, const std::string& Caption,
                                     const std::optional<ExactThrough>& Exact = std::nullopt)
    {
        Vector3 Low = Samples.front(), High = Samples.front();
        for (const Vector3& Point : Samples)
        {
            Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
            High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
        }
        FeatureStep Step;
        Step.Kind            = FeatureStepKind::SpaceCurve;
        Step.Label           = "Curve#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Points          = Exact ? Exact->Poles : std::move(Samples);
        Step.CurveKnots      = Exact ? Exact->Knots : std::vector<double>{};
        Step.ClosedCondition = ClosedCondition;
        Step.PlanarCondition = false;
        Step.Tolerance       = FeatureValue{ 1.0e-6 * std::max(Distance(Low, High), 1.0e-3) };
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return Document.Tree.Steps.back().Label;
    }
}

SketcherCommandOutcome DrawSketcherSpaceCurve(SketcherDocument& Document, const std::vector<Vector3>& Points, bool ClosedCondition, SketchSplineForm Form)
{
    const std::string Caption = NextCaption(Document, "Curve");
    std::string       Label;
    SketcherCommandOutcome Outcome = RunCommand(Document, Caption + " · 3D curve through " + std::to_string(Points.size()) + " points", false, [&]() -> std::string
    {
        if (Points.size() < 2) return "a 3D curve needs at least 2 points";
        for (size_t Index = 1; Index < Points.size(); ++Index)
            if (Distance(Points[Index - 1], Points[Index]) <= 1.0e-12) return "the same point twice in a row";
        const bool           Closed  = ClosedCondition && Points.size() >= 3;
        std::vector<Vector3> Samples = SampleThroughCurve(Points, Closed, Form);
        if (Samples.size() < 2) return std::string("the ") + SketchSplineFormLabel(Form) + " form takes no curve through these points";
        Label = AppendSpaceCurveStep(Document, std::move(Samples), Closed, Caption, ResolveExactThrough(Points, Closed, Form, {}));
        return {};
    });
    if (Outcome.AcceptedCondition) Document.Selection.Curves = { Label };
    return Outcome;
}

namespace { std::string SplitCrossingsInPlace(SketcherDocument&,double,const std::string& Only = {}); }
SketcherCommandOutcome DrawSketcherProjectedCurve(SketcherDocument& Document, const NurbsCurve& Curve, bool Closed, bool AutoSplit, double IntersectionTolerance)
{
    std::string Label;
    SketcherCommandOutcome Outcome = RunCommand(Document, "Draw surface trace", false, [&]() -> std::string
    {
        if (!Curve.Definition || Curve.Definition->Poles.size() < 2) return "surface trace has no fitted curve";
        const auto& Definition = *Curve.Definition;
        if (std::any_of(Definition.Weights.begin(), Definition.Weights.end(), [](double Weight) { return std::abs(Weight - 1.0) > 1.0e-12; }))
            return "surface trace must be a non-rational B-spline";
        if (Closed && Distance(Definition.Poles.front(), Definition.Poles.back()) > 1.0e-8) return "surface trace is not closed";
        ExactThrough Exact{ Definition.Poles, Definition.Knots };
        Label = AppendSpaceCurveStep(Document, Definition.Poles, Closed, NextCaption(Document, "Surface trace"), Exact);
        return AutoSplit ? SplitCrossingsInPlace(Document,IntersectionTolerance,Label) : std::string{};
    });
    if (Outcome.AcceptedCondition) Document.Selection.Curves = { Label };
    return Outcome;
}

std::vector<double> SketcherCurveControlParameters(const NurbsCurveDefinition& D)
{
    std::vector<double> Parameters;
    for (size_t I=0; I<D.Poles.size(); ++I)
    {
        double T=0; for(int J=1; J<=D.Degree; ++J) T+=D.Knots[I+J];
        Parameters.push_back(T/D.Degree);
    }
    return Parameters;
}

namespace {
void StoreControlDefinition(FeatureStep& Step, const NurbsCurveDefinition& D)
{
    Step.Points=D.Poles; Step.CurveKnots=D.Knots; Step.PoleEdits.clear();
    const double Low=D.Knots[D.Degree], High=D.Knots[D.Poles.size()];
    for (auto& K:Step.CurveKnots) K=(K-Low)/(High-Low);
}
}

namespace {
// Exact curve splitting after a genuine 3D proximity root, never a screen-space
// intersection. The tolerance is explicit: near joints share the same endpoint.
std::string SplitCrossingsInPlace(SketcherDocument& D,double Tolerance,const std::string& Only)
{
    if(!(Tolerance>0) || !std::isfinite(Tolerance)) return "Intersection tolerance must be finite and positive";
    struct Curve {std::string Label;NurbsCurveDefinition Definition;std::vector<Vector3> Samples;};
    std::vector<Curve> Curves;
    constexpr int Samples=96;
    for(const auto& S:D.Tree.Steps) {
        if(S.Kind!=FeatureStepKind::SpaceCurve || SketcherLockedCondition(D,S.Label)) continue;
        NurbsCurveDefinition Def;
        if(auto It=D.Regeneration.Curves.find(S.Label);It!=D.Regeneration.Curves.end())Def=*It->second.Curve.Definition;
        else {
            Def.Poles=S.Points;Def.Knots=S.CurveKnots;Def.Degree=int(Def.Knots.size())-int(Def.Poles.size())-1;Def.Weights.assign(Def.Poles.size(),1);
            if(Def.Degree<1 || Def.Poles.size()<2) continue;
        }
        Curve C{S.Label,Def,{}};
        for(int I=0;I<=Samples;++I) C.Samples.push_back(EvaluateNurbsCurve(Def,I/double(Samples)).Position);
        Curves.push_back(std::move(C));
    }
    struct Joint {double T;Vector3 P;};std::map<std::string,std::vector<Joint>> Cuts;
    for(size_t A=0;A<Curves.size();++A)for(size_t B=A;B<Curves.size();++B) {
        const auto& CA=Curves[A];const auto& CB=Curves[B];
        if(!Only.empty() && CA.Label!=Only && CB.Label!=Only)continue;
        for(int I=0;I<Samples;++I)for(int J=0;J<Samples;++J) {
            if(A==B && (J<=I+2 || (I==0 && J==Samples-1)))continue;
            auto PA=CA.Samples[I],QA=CA.Samples[I+1],PB=CB.Samples[J],QB=CB.Samples[J+1];
            auto U=QA-PA,V=QB-PB,W=PA-PB;
            const double UU=Dot(U,U),VV=Dot(V,V),UV=Dot(U,V),UW=Dot(U,W),VW=Dot(V,W),Den=UU*VV-UV*UV;
            if(Den<1e-14*UU*VV || UU<1e-24 || VV<1e-24)continue; // overlaps/tangencies are not transverse cuts
            double S=std::clamp((UV*VW-VV*UW)/Den,0.0,1.0),T=std::clamp((UU*VW-UV*UW)/Den,0.0,1.0);
            if(Distance(PA+U*S,PB+V*T)>Tolerance+Length(QA-PA)*.02+Length(QB-PB)*.02)continue;
            double X=(I+S)/Samples,Y=(J+T)/Samples;
            for(int Iter=0;Iter<24;++Iter) {
                auto E=EvaluateNurbsCurveDerivatives(CA.Definition,X),F=EvaluateNurbsCurveDerivatives(CB.Definition,Y);
                auto R=E.Position-F.Position;double XX=Dot(E.First,E.First),YY=Dot(F.First,F.First),XY=Dot(E.First,F.First);
                double Det=XX*YY-XY*XY;if(Det<1e-16*XX*YY)break;
                double RX=Dot(E.First,R),RY=Dot(F.First,R);
                double DX=(-YY*RX+XY*RY)/Det,DY=(-XY*RX+XX*RY)/Det;
                X=std::clamp(X+DX,0.0,1.0);Y=std::clamp(Y+DY,0.0,1.0);
                if(std::abs(DX)+std::abs(DY)<1e-12)break;
            }
            auto P=EvaluateNurbsCurve(CA.Definition,X).Position,Q=EvaluateNurbsCurve(CB.Definition,Y).Position;
            if(Distance(P,Q)>Tolerance || (A==B && std::abs(X-Y)<1e-5))continue;
            auto JointPoint=(P+Q)*.5;
            for(auto Entry:{std::pair<std::string,double>{CA.Label,X},{CB.Label,Y}}) {
                auto& List=Cuts[Entry.first];if(std::none_of(List.begin(),List.end(),[&](const auto& C){return std::abs(C.T-Entry.second)<1e-6;}))List.push_back({Entry.second,JointPoint});
            }
        }
    }
    for(const auto& C:Curves) {
        auto It=Cuts.find(C.Label);if(It==Cuts.end())continue;
        auto& J=It->second;std::sort(J.begin(),J.end(),[](auto A,auto B){return A.T<B.T;});
        const bool Interior=std::any_of(J.begin(),J.end(),[](auto P){return P.T>1e-7 && P.T<1-1e-7;});
        auto Step=std::find_if(D.Tree.Steps.begin(),D.Tree.Steps.end(),[&](const auto& S){return S.Label==C.Label;});
        if(!Step->CurveOffsetGuide.empty() || (Interior && SketcherSketchReferencedCondition(D,C.Label)))
            return "A crossing curve has live dependents; detach those dependencies before auto-splitting (nothing changed)";
        FeatureStep Original=*Step;size_t Index=Step-D.Tree.Steps.begin();auto Remaining=C.Definition;
        std::vector<NurbsCurveDefinition> Pieces;
        for(auto Joint:J) if(Joint.T>1e-7 && Joint.T<1-1e-7) {
            auto Split=SplitNurbsCurve(Remaining,Joint.T);if(!Split)return "Intersection boundary cannot be split exactly";
            Split->first.Poles.back()=Joint.P;Split->second.Poles.front()=Joint.P;
            Pieces.push_back(Split->first);Remaining=Split->second;
        }
        Pieces.push_back(Remaining);
        if(J.front().T<=1e-7)Pieces.front().Poles.front()=J.front().P;
        if(J.back().T>=1-1e-7)Pieces.back().Poles.back()=J.back().P;
        for(size_t I=0;I<Pieces.size();++I) {
            auto Piece=Original;StoreControlDefinition(Piece,Pieces[I]);if(Pieces.size()>1)Piece.ClosedCondition=false;
            if(I==0)D.Tree.Steps[Index]=std::move(Piece);
            else {Piece.Label="Curve#"+std::to_string(NextStepOrdinal(D.Tree));D.Captions[Piece.Label]=NextCaption(D,"Curve piece");D.Tree.Steps.push_back(std::move(Piece));}
        }
    }
    return {};
}
}
// 📝 Knot insertion divides the evaluated curve, without fitting, resampling or moving its controls.
SketcherCommandOutcome SplitSketcherSpaceCurveAt(SketcherDocument& Document, const std::string& Label, double Parameter)
{
    const std::string OriginalLabel = Label; // Selection may itself supply Label.
    std::string SecondLabel;
    SketcherCommandOutcome Outcome = RunCommand(Document, "Split curve at pointer", false, [&]() -> std::string
    {
        auto Step = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(),
                                 [&](const FeatureStep& Candidate) { return Candidate.Label == OriginalLabel; });
        if (Step == Document.Tree.Steps.end() || Step->Kind != FeatureStepKind::SpaceCurve)
            return "Choose an authored 3D curve";
        if (SketcherLockedCondition(Document, OriginalLabel)) return "Unlock this curve before splitting";
        if (!Step->CurveOffsetGuide.empty()) return "Detach the live distance target before splitting it";
        if (SketcherSketchReferencedCondition(Document, OriginalLabel))
            return "This curve has live dependents; detach them before splitting (nothing changed)";
        const auto Found = Document.Regeneration.Curves.find(OriginalLabel);
        if (Found == Document.Regeneration.Curves.end() || !Found->second.Curve.Definition)
            return "The curve has no regenerated definition";
        const auto& Definition = *Found->second.Curve.Definition;
        if (std::any_of(Definition.Weights.begin(), Definition.Weights.end(),
                        [](double Weight) { return std::abs(Weight - 1.0) > 1.0e-12; }))
            return "This curve stores rational weights; its editable split format is not supported";
        const double Low = Definition.Knots[Definition.Degree], High = Definition.Knots[Definition.Poles.size()];
        if (!std::isfinite(Parameter) || Parameter <= Low + (High - Low) * 1.0e-7 || Parameter >= High - (High - Low) * 1.0e-7)
            return "Split inside the curve, not at an existing endpoint";
        const auto Pieces = SplitNurbsCurve(Definition, Parameter);
        if (!Pieces) return "The curve cannot be divided at this parameter";
        FeatureStep Second = *Step;
        StoreControlDefinition(*Step, Pieces->first);
        Step->ClosedCondition = false;
        StoreControlDefinition(Second, Pieces->second);
        Second.ClosedCondition = false;
        Second.Label = "Curve#" + std::to_string(NextStepOrdinal(Document.Tree));
        SecondLabel = Second.Label;
        Document.Captions[SecondLabel] = NextCaption(Document, "Curve piece");
        Document.Tree.Steps.push_back(std::move(Second));
        return {};
    });
    if (Outcome.AcceptedCondition) Document.Selection.Curves = { OriginalLabel, SecondLabel };
    return Outcome;
}

SketcherCommandOutcome SplitSketcherCurveCrossings(SketcherDocument& D,double Tolerance)
{
    return RunCommand(D,"Split 3D curve crossings",false,[&](){return SplitCrossingsInPlace(D,Tolerance);},0);
}

SketcherCommandOutcome LinkSketcherSurfaceOffset(SketcherDocument& D,const std::string& GuideInput,const std::string& TargetInput,
    const std::string& BodyInput,double Distance,double Tolerance)
{
    const std::string Guide=GuideInput,Target=TargetInput,Body=BodyInput;
    return RunCommand(D,"Live surface-distance offset",false,[&]() -> std::string {
        if(Guide==Target || !(Distance>0) || !(Tolerance>0) || !std::isfinite(Distance+Tolerance)) return "Select two different curves and finite positive distance/tolerance";
        auto G=std::find_if(D.Tree.Steps.begin(),D.Tree.Steps.end(),[&](const auto& S){return S.Label==Guide;});
        auto T=std::find_if(D.Tree.Steps.begin(),D.Tree.Steps.end(),[&](const auto& S){return S.Label==Target;});
        if(G==D.Tree.Steps.end() || T==D.Tree.Steps.end() || T->Kind!=FeatureStepKind::SpaceCurve) return "Target must be an existing native 3D spline";
        if(G>T) {
            for(auto It=T+1;It<=G;++It)
                if(It->CurveOffsetGuide==Target || It->Profile==Target || It->Path==Target || std::find(It->Sections.begin(),It->Sections.end(),Target)!=It->Sections.end())
                    return "Cannot link: guide/history would depend on its own target (cycle)";
            auto Moved=*T;D.Tree.Steps.erase(T);
            G=std::find_if(D.Tree.Steps.begin(),D.Tree.Steps.end(),[&](const auto& S){return S.Label==Guide;});
            T=D.Tree.Steps.insert(G+1,std::move(Moved));
        }
        if(SketcherLockedCondition(D,Target)) return "Target is locked";
        if(!FindRegeneratedBody(D,Body)) return "Choose a valid reference body";
        auto Fit=D.Regeneration.Curves.find(Target);
        if(Fit==D.Regeneration.Curves.end())return "Target does not regenerate";
        StoreControlDefinition(*T,*Fit->second.Curve.Definition); // current target chooses side and extent
        G=std::find_if(D.Tree.Steps.begin(),D.Tree.Steps.end(),[&](const auto& S){return S.Label==Guide;});
        NurbsCurve GuideCurve;
        if(auto C=D.Regeneration.Curves.find(Guide);C!=D.Regeneration.Curves.end())GuideCurve=C->second.Curve;
        else if(auto C=D.Regeneration.Sketches.find(Guide);C!=D.Regeneration.Sketches.end()) {
            auto Plane=G->Plane;Plane.Origin=Plane.Origin+Plane.ZAxis*EvaluateFeatureValue(D.Tree,G->Elevation);
            auto Section=ComposeSketchSection(C->second,Plane);if(Section.Curve)GuideCurve=*Section.Curve;
        }
        if(!GuideCurve.Definition)return "Guide must be a single curve";
        T->CurveOffsetOrigin=EvaluateNurbsCurve(*GuideCurve.Definition,.5).Position;
        T->CurveOffsetGuide=Guide;T->CurveOffsetBody=Body;T->CurveOffsetDistance=Distance;T->CurveOffsetTolerance=Tolerance;
        return {};
    },0);
}
SketcherCommandOutcome DetachSketcherSurfaceOffset(SketcherDocument& D,const std::string& Target)
{
    return RunCommand(D,"Detach surface offset (keep current curve)",false,[&]() -> std::string {
        auto T=std::find_if(D.Tree.Steps.begin(),D.Tree.Steps.end(),[&](const auto& S){return S.Label==Target;});
        auto Fit=D.Regeneration.Curves.find(Target);
        if(T==D.Tree.Steps.end() || Fit==D.Regeneration.Curves.end() || T->CurveOffsetGuide.empty()) return "Select a live offset curve";
        if(SketcherLockedCondition(D,Target))return "Target is locked";
        StoreControlDefinition(*T,*Fit->second.Curve.Definition);T->CurveOffsetGuide.clear();T->CurveOffsetBody.clear();return {};
    },0);
}

SketcherCommandOutcome DeleteSketcherCurvePoles(SketcherDocument& Document, const std::string& Owner, const std::vector<int>& Indices)
{
    const std::string Label=Owner; // May alias Tree.Steps storage.
    return RunCommand(Document,"Delete selected spline controls",false,[&]() -> std::string
    {
        auto Step=std::find_if(Document.Tree.Steps.begin(),Document.Tree.Steps.end(),[&](const auto& S){return S.Label==Label && S.Kind==FeatureStepKind::SpaceCurve;});
        const auto Fit=Document.Regeneration.Curves.find(Label);
        if (Step==Document.Tree.Steps.end() || Fit==Document.Regeneration.Curves.end()) return "Select a spline control first";
        if (SketcherLockedCondition(Document,Label)) return "The spline is locked";
        if (!Step->CurveOffsetGuide.empty()) return "Detach the live surface offset before editing its controls or spans";
        const auto& Before=*Fit->second.Curve.Definition;
        std::set<int> Selected(Indices.begin(),Indices.end());
        if (Selected.empty() || *Selected.begin()<0 || *Selected.rbegin()>=int(Before.Poles.size())) return "Invalid control selection";
        std::vector<Vector3> Kept;
        for(int I=0; I<int(Before.Poles.size()); ++I) if(!Selected.contains(I)) Kept.push_back(Before.Poles[I]);
        if (Kept.size()<2) return "A spline needs at least two controls; no object was deleted. Use object mode to delete the whole spline";
        const bool Closed=Step->ClosedCondition && !Selected.contains(0) && !Selected.contains(int(Before.Poles.size())-1);
        if (Closed && Kept.size()<4) return "A closed spline needs three distinct controls; no object was deleted";
        NurbsCurveDefinition D;
        D.Poles=Kept; D.Degree=std::min(Before.Degree,int(Kept.size())-1);
        D.Knots.assign(D.Degree+1,0.0);
        const int Spans=int(Kept.size())-D.Degree;
        for(int I=1; I<Spans; ++I) D.Knots.push_back(double(I)/Spans);
        D.Knots.insert(D.Knots.end(),D.Degree+1,1.0);
        StoreControlDefinition(*Step,D); Step->ClosedCondition=Closed;
        return {};
    },0);
}

SketcherCommandOutcome SubdivideSketcherCurve(SketcherDocument& Document, const std::string& Owner, const std::vector<int>& Spans)
{
    const std::string Label=Owner;
    return RunCommand(Document,"Subdivide spline (exact knot insertion)",false,[&]() -> std::string
    {
        const auto Fit=Document.Regeneration.Curves.find(Label);
        auto Step=std::find_if(Document.Tree.Steps.begin(),Document.Tree.Steps.end(),[&](const auto& S){return S.Label==Label && S.Kind==FeatureStepKind::SpaceCurve;});
        if (Step==Document.Tree.Steps.end() || Fit==Document.Regeneration.Curves.end()) return "Select a spline or control span first";
        if (SketcherLockedCondition(Document,Label)) return "The spline is locked";
        if (!Step->CurveOffsetGuide.empty()) return "Detach the live surface offset before editing its controls or spans";
        const auto& Before=*Fit->second.Curve.Definition;
        const auto T=SketcherCurveControlParameters(Before);
        std::set<int> Chosen(Spans.begin(),Spans.end());
        if (Chosen.empty()) for(int I=0; I+1<int(T.size()); ++I) Chosen.insert(I);
        if (*Chosen.begin()<0 || *Chosen.rbegin()+1>=int(T.size())) return "Invalid spline span selection";
        std::vector<double> Inserted;
        for(int I:Chosen) if(T[I+1]-T[I]>1e-12) Inserted.push_back((T[I]+T[I+1])*0.5);
        const auto Refined=RefineNurbsCurveKnots(Before,Inserted);
        if (Refined.Poles.size()==Before.Poles.size()) return "Selected spans cannot be subdivided further at this knot multiplicity";
        StoreControlDefinition(*Step,Refined);
        return {};
    },0);
}

SketcherCommandOutcome DeleteSketcherCurveSpans(SketcherDocument& Document, const std::string& Owner, const std::vector<int>& Spans)
{
    const std::string Label=Owner;
    return RunCommand(Document,"Delete selected spline spans",false,[&]() -> std::string
    {
        const auto Fit=Document.Regeneration.Curves.find(Label);
        auto Step=std::find_if(Document.Tree.Steps.begin(),Document.Tree.Steps.end(),[&](const auto& S){return S.Label==Label && S.Kind==FeatureStepKind::SpaceCurve;});
        if (Step==Document.Tree.Steps.end() || Fit==Document.Regeneration.Curves.end()) return "Select a spline span first";
        if (SketcherLockedCondition(Document,Label)) return "The spline is locked";
        if (!Step->CurveOffsetGuide.empty()) return "Detach the live surface offset before editing its controls or spans";
        const auto Before=*Fit->second.Curve.Definition;
        const auto T=SketcherCurveControlParameters(Before);
        std::set<int> Selected(Spans.begin(),Spans.end());
        if (Selected.empty() || *Selected.begin()<0 || *Selected.rbegin()+1>=int(T.size())) return "Invalid spline span selection";
        std::vector<std::pair<double,double>> Kept;
        for(int I=0; I+1<int(T.size());)
        {
            if(Selected.contains(I)) {++I;continue;}
            int First=I; while(I+1<int(T.size()) && !Selected.contains(I)) ++I;
            if(T[I]-T[First]>1e-12) Kept.push_back({T[First],T[I]});
        }
        if (SketcherSketchReferencedCondition(Document,Label)) return "This spline is used by another feature; detach its dependents before deleting spans (nothing deleted)";
        if (Kept.empty()) { Document.Tree.Steps.erase(Step); Document.Captions.erase(Label); return {}; }
        const FeatureStep Original=*Step;
        const size_t Index=size_t(Step-Document.Tree.Steps.begin());
        for(size_t I=0; I<Kept.size(); ++I)
        {
            auto D=Before;
            if(Kept[I].second<T.back()-1e-12)
            {auto Cut=SplitNurbsCurve(D,Kept[I].second); if(!Cut)return "Cannot split the selected span boundary";D=Cut->first;}
            if(Kept[I].first>T.front()+1e-12)
            {auto Cut=SplitNurbsCurve(D,Kept[I].first); if(!Cut)return "Cannot split the selected span boundary";D=Cut->second;}
            FeatureStep Piece=Original; Piece.ClosedCondition=false; StoreControlDefinition(Piece,D);
            if(I==0) Document.Tree.Steps[Index]=std::move(Piece);
            else
            {
                Piece.Label="Curve#"+std::to_string(NextStepOrdinal(Document.Tree));
                Document.Captions[Piece.Label]=NextCaption(Document,"Curve");
                Document.Tree.Steps.push_back(std::move(Piece));
            }
        }
        return {};
    },0);
}

SketcherCommandOutcome LiftSketcherSketchPoints(SketcherDocument& Document, const std::string& SketchName, const OrthonormalFrame& Plane, const std::vector<int>& Points,
                                                const std::vector<Vector3>& Targets)
{
    // The caller may pass a label owned by Tree.Steps; appending curves reallocates that vector.
    const std::string Sketch = SketchName;
    std::vector<std::string> Made;
    SketcherCommandOutcome Outcome = RunCommand(Document, "Lift " + CaptionOfStep(Document, Sketch) + " points off its plane", false, [&]() -> std::string
    {
        auto Step = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Each) { return Each.Label == Sketch && Each.Kind == FeatureStepKind::Sketch; });
        if (Step == Document.Tree.Steps.end()) return "no sketch named " + Sketch;
        if (Points.size() != Targets.size() || Points.empty()) return "no point moved";
        const auto         Solved = Document.Regeneration.Sketches.find(Sketch);
        const SketchModel  Model  = Solved != Document.Regeneration.Sketches.end() ? Solved->second : Step->Sketch;
        std::map<int, Vector3> Moved;
        for (size_t Index = 0; Index < Points.size(); ++Index)
        {
            if (Points[Index] < 0 || Points[Index] >= int(Model.Points.size())) return "a moved point is not in " + Sketch;
            Moved[Points[Index]] = Targets[Index];
        }
        // ① Freeform curves and lines can leave the sketch plane. Arcs / circles retain their analytic plane.
        std::vector<int> Curves;
        for (int Curve = 0; Curve < int(Model.Curves.size()); ++Curve)
        {
            const SketchCurve& Each  = Model.Curves[static_cast<size_t>(Curve)];
            const bool         Touch = Each.Kind == SketchCurveKind::Spline ? std::any_of(Each.Points.begin(), Each.Points.end(), [&](int Point) { return Moved.contains(Point); })
                                                                            : Moved.contains(Each.Start) || Moved.contains(Each.End) || Moved.contains(Each.Centre);
            if (!Touch) continue;
            if (Each.Kind != SketchCurveKind::Spline && Each.Kind != SketchCurveKind::Line) return "move the whole arc / circle in XYZ; its individual points retain the analytic plane";
            Curves.push_back(Curve);
        }
        if (Curves.empty()) return "no curve passes through the moved points";
        for (const FeatureStep& Other : Document.Tree.Steps)
            if (Other.Label != Sketch && (Other.Profile == Sketch || Other.Path == Sketch || std::find(Other.Sections.begin(), Other.Sections.end(), Sketch) != Other.Sections.end()))
                return CaptionOfStep(Document, Sketch) + " is used by " + CaptionOfStep(Document, Other.Label) + ": its curves stay on the plane";
        // ② each becomes a 3D curve of the same form through its (moved) points
        for (int Curve : Curves)
        {
            const SketchCurve&   Each = Model.Curves[static_cast<size_t>(Curve)];
            const std::vector<int> Indices = Each.Kind == SketchCurveKind::Line ? std::vector<int>{ Each.Start, Each.End } : Each.Points;
            const bool           Closed = Indices.size() >= 3 && Indices.front() == Indices.back();
            std::vector<Vector3> Slots;
            for (size_t Index = 0; Index + (Closed ? 1 : 0) < Indices.size(); ++Index)
            {
                const int Point = Indices[Index];
                Slots.push_back(Moved.contains(Point) ? Moved.at(Point) : Plane.Origin + Plane.XAxis * Model.Points[static_cast<size_t>(Point)].Position.X + Plane.YAxis * Model.Points[static_cast<size_t>(Point)].Position.Y);
            }
            std::vector<Vector3> Samples = SampleThroughCurve(Slots, Closed, Each.Form, 48, Each.Knots);
            if (Samples.size() < 2) return std::string("the ") + SketchSplineFormLabel(Each.Form) + " curve cannot leave its plane";
            Made.push_back(AppendSpaceCurveStep(Document, std::move(Samples), Closed, NextCaption(Document, "Curve"), ResolveExactThrough(Slots, Closed, Each.Form, Each.Knots)));
        }
        // ③ they leave the sketch (an emptied sketch goes); the appends above may have moved the steps
        Step = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Each) { return Each.Label == Sketch && Each.Kind == FeatureStepKind::Sketch; });
        for (auto Curve = Curves.rbegin(); Curve != Curves.rend(); ++Curve) EraseSketchCurve(Step->Sketch, *Curve);
        if (Step->Sketch.Curves.empty())
        {
            Document.Captions.erase(Sketch);
            Document.Tree.Steps.erase(Step);
        }
        return {};
    });
    if (Outcome.AcceptedCondition) Document.Selection.Curves = Made;
    return Outcome;
}

SketcherCommandOutcome MirrorSketcherPanels(SketcherDocument& Document, int Axis)
{
    const char*       AxisName = Axis == 0 ? "x" : Axis == 1 ? "y" : "z";
    const std::string Caption  = NextCaption(Document, "Mirror");
    return RunCommand(Document, Caption + " · plane " + AxisName + " = 0", false, [&]() -> std::string
    {
        if (Axis < 0 || Axis > 2) return "mirror axis must be x, y or z";
        FeatureStep Step;
        Step.Kind         = FeatureStepKind::Mirror;
        Step.Label        = "Mirror#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Plane.Origin = { 0.0, 0.0, 0.0 };
        Step.Plane.ZAxis  = { Axis == 0 ? 1.0 : 0.0, Axis == 1 ? 1.0 : 0.0, Axis == 2 ? 1.0 : 0.0 };
        Step.Plane.XAxis  = { Axis == 0 ? 0.0 : 1.0, Axis == 0 ? 1.0 : 0.0, 0.0 };
        Step.Plane.YAxis  = Cross(Step.Plane.ZAxis, Step.Plane.XAxis);
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

SketcherCommandOutcome CloseSketcherPanels(SketcherDocument& Document)
{
    const std::string Caption = NextCaption(Document, "Close");
    return RunCommand(Document, Caption + " · plane caps", false, [&]() -> std::string
    {
        FeatureStep Step;
        Step.Kind  = FeatureStepKind::Close;
        Step.Label = "Close#" + std::to_string(NextStepOrdinal(Document.Tree));
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

SketcherCommandOutcome MoveSketcherPole(SketcherDocument& Document, const std::string& Label, int Pole, Vector3 Position)
{
    return MoveSketcherPoles(Document, Label, { Pole }, { Position });
}

SketcherCommandOutcome MoveSketcherPoles(SketcherDocument& Document, const std::string& Label, const std::vector<int>& Poles, const std::vector<Vector3>& Positions)
{
    const auto        Caption = Document.Captions.find(Label);
    const std::string Name    = Caption != Document.Captions.end() ? Caption->second : Label;
    const std::string Command = Poles.size() == 1 ? ("Move pole " + std::to_string(Poles[0]) + " of " + Name)
                                                  : ("Move " + std::to_string(Poles.size()) + " poles of " + Name);
    return RunCommand(Document, Command, false, [&]() -> std::string
    {
        if (Poles.empty() || Poles.size() != Positions.size()) return "pick at least one pole to move";
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Label; });
        if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::SpaceCurve) return "no curve step labelled " + Label;
        const auto Fitted = Document.Regeneration.Curves.find(Label);
        if (Fitted == Document.Regeneration.Curves.end()) return "curve " + Label + " has no fitted poles (refused step)";
        if (!Found->CurveOffsetGuide.empty()) return "Detach the live surface offset before moving its controls";
        const int Total = static_cast<int>(Fitted->second.Curve.Definition->Poles.size());
        for (size_t Index = 0; Index < Poles.size(); ++Index)
        {
            const int     Pole     = Poles[Index];
            const Vector3 Position = Positions[Index];
            if (Pole < 0 || Pole >= Total) return "pole " + std::to_string(Pole) + " does not exist";
            if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z)) return "pole position is not finite";
            const auto Edit = std::find_if(Found->PoleEdits.begin(), Found->PoleEdits.end(), [&](const FeaturePoleEdit& Entry) { return Entry.Index == Pole; });
            if (Edit != Found->PoleEdits.end()) Edit->Position = Position;
            else Found->PoleEdits.push_back({ Pole, Position });
        }
        return {};
    },0);
}

void SelectSketcherCurve(SketcherDocument& Document, const std::string& Label, bool AdditiveCondition)
{
    std::vector<std::string>& Curves = Document.Selection.Curves;
    if (!AdditiveCondition)
    {
        Document.Selection = {};
        Document.Selection.Curves.push_back(Label);
        return;
    }
    const auto Found = std::find(Curves.begin(), Curves.end(), Label);
    if (Found != Curves.end()) Curves.erase(Found);
    else Curves.push_back(Label);
}

bool UndoSketcherCommand(SketcherDocument& Document)
{
    if (Document.UndoEntries.empty()) return false;
    FRONTIER_TRACE_SCOPE("History", "Undo · " + Document.UndoEntries.back().Label);
    SketcherHistoryEntry Entry = std::move(Document.UndoEntries.back());
    Document.UndoEntries.pop_back();
    Document.RedoEntries.push_back(Snapshot(Document, Entry.Label));
    Document.RedoEntries.back().AppearanceCondition = Entry.AppearanceCondition;
    Restore(Document, std::move(Entry));
    TrimHistoryReplay(Document);
    return true;
}

bool RedoSketcherCommand(SketcherDocument& Document)
{
    if (Document.RedoEntries.empty()) return false;
    FRONTIER_TRACE_SCOPE("History", "Redo · " + Document.RedoEntries.back().Label);
    SketcherHistoryEntry Entry = std::move(Document.RedoEntries.back());
    Document.RedoEntries.pop_back();
    Document.UndoEntries.push_back(Snapshot(Document, Entry.Label));
    Document.UndoEntries.back().AppearanceCondition = Entry.AppearanceCondition;
    Restore(Document, std::move(Entry));
    TrimHistoryReplay(Document);
    return true;
}

SketcherCommandOutcome AssignSketcherMatcap(SketcherDocument& Document, const std::string& Matcap)
{
    const std::vector<std::string> Faces  = Document.Selection.Faces;
    const std::vector<std::string> Bodies = Faces.empty() ? Document.Selection.Bodies : std::vector<std::string>{};
    const std::string Label = "Matcap " + Matcap + " → " + (!Faces.empty() ? std::to_string(Faces.size()) + " face(s)"
                                                         : !Bodies.empty() ? std::to_string(Bodies.size()) + " body(ies)" : std::string("body"));
    return RunAppearanceCommand(Document, Label, [&]() -> std::string
    {
        SketcherAppearance& Appearance = Document.Appearance;
        if (!SketcherMatcapKnownCondition(Appearance, Matcap)) return "no matcap named " + Matcap;
        if (!Bodies.empty())
        {
            bool ChangedCondition = false;
            for (const std::string& Key : Bodies)
            {
                std::string& Assigned = Appearance.BodyMatcaps[Key];
                ChangedCondition = ChangedCondition || Assigned != Matcap;
                Assigned = Matcap;
            }
            return ChangedCondition ? std::string{} : "the selected bodies already show " + Matcap;
        }
        if (Faces.empty())
        {
            if (Appearance.BodyMatcap == Matcap) return "the body already shows " + Matcap;
            Appearance.BodyMatcap = Matcap;
            return {};
        }
        bool ChangedCondition = false;
        for (const std::string& Face : Faces)
        {
            std::string& Assigned = Appearance.FaceMatcaps[Face];
            ChangedCondition = ChangedCondition || Assigned != Matcap;
            Assigned = Matcap;
        }
        return ChangedCondition ? std::string{} : "the selected faces already show " + Matcap;
    });
}

SketcherCommandOutcome RemoveSketcherMatcap(SketcherDocument& Document)
{
    const std::vector<std::string> Faces  = Document.Selection.Faces;
    const std::vector<std::string> Bodies = Faces.empty() ? Document.Selection.Bodies : std::vector<std::string>{};
    const std::string Label = !Faces.empty()  ? "Matcap removed → " + std::to_string(Faces.size()) + " face(s) show the body"
                            : !Bodies.empty() ? "Matcap removed → " + std::to_string(Bodies.size()) + " body(ies) show the default"
                                              : std::string("Matcap removed → body Studio");
    return RunAppearanceCommand(Document, Label, [&]() -> std::string
    {
        SketcherAppearance& Appearance = Document.Appearance;
        if (!Bodies.empty())
        {
            size_t Removed = 0;
            for (const std::string& Key : Bodies) Removed += Appearance.BodyMatcaps.erase(Key);
            return Removed ? std::string{} : "the selected bodies have no matcap of their own";
        }
        if (Faces.empty())
        {
            if (Appearance.BodyMatcap == SketcherStudioMatcap) return "the body already shows Studio";
            Appearance.BodyMatcap = SketcherStudioMatcap;
            return {};
        }
        size_t Removed = 0;
        for (const std::string& Face : Faces) Removed += Appearance.FaceMatcaps.erase(Face);
        return Removed ? std::string{} : "the selected faces have no matcap of their own";
    });
}

SketcherCommandOutcome AddSketcherMatcapPicture(SketcherDocument& Document, const std::string& Name, const std::string& Path)
{
    return RunAppearanceCommand(Document, "Matcap picture " + Name, [&]() -> std::string
    {
        SketcherAppearance& Appearance = Document.Appearance;
        if (Name.empty() || std::any_of(Name.begin(), Name.end(), [](char Character) { return std::isspace(static_cast<unsigned char>(Character)) != 0; }))
            return "a matcap name needs at least one character and no spaces";
        if (SketcherMatcapKnownCondition(Appearance, Name)) return "a matcap named " + Name + " exists already";
        if (Appearance.CustomMatcaps.size() + SketcherBuiltInMatcaps().size() >= SketcherMatcapLayerLimit) return "the matcap library is full";
        const SketcherPicture Picture = DecodeSketcherPicture(Path);
        if (!Picture.Refusal.empty()) return Picture.Refusal;
        Appearance.CustomMatcaps.push_back({ Name, Path });
        return {};
    });
}

void SelectSketcherName(SketcherDocument& Document, SketcherSelectionKind Kind, const std::string& Name, bool AdditiveCondition, const std::string& Body)
{
    std::vector<std::string>& Names = Kind == SketcherSelectionKind::Face ? Document.Selection.Faces
                                    : Kind == SketcherSelectionKind::Edge ? Document.Selection.Edges
                                    : Kind == SketcherSelectionKind::Vertex ? Document.Selection.Vertices : Document.Selection.Bodies;
    if (!AdditiveCondition)
    {
        Document.Selection = {};
        Names.push_back(Name);
        Document.Selection.LastKind = Kind;
        if (Kind != SketcherSelectionKind::Body) Document.Selection.PickedBody = Body;
        return;
    }
    Document.Selection.LastKind = Kind;
    if (Kind != SketcherSelectionKind::Body && !Body.empty()) Document.Selection.PickedBody = Body;
    const auto Found = std::find(Names.begin(), Names.end(), Name);
    if (Found != Names.end()) Names.erase(Found);
    else Names.push_back(Name);
}

std::vector<std::string> SketcherTangentChain(const SketcherDocument& Document, const std::string& Name)
{
    const RegeneratedBody* Owner = FindRegeneratedBody(Document, SketcherBodyOfName(Document, SketcherSelectionKind::Edge, Name));
    if (!Document.Partition || !Owner) return { Name };
    const GeometrySpace::Parametric::BodyNaming&    Naming    = Owner->Naming;
    const GeometrySpace::Parametric::BodyInventory  Inventory = Document.Partition->CollectInventory(Owner->Body);
    std::vector<GeometrySpace::Parametric::EdgeToken> Seeds;
    for (const GeometrySpace::Parametric::EdgeToken Edge : Inventory.Edges)
    {
        const auto Found = Naming.Edges.find(Edge.Slot);
        if (Found != Naming.Edges.end() && Found->second == Name) Seeds.push_back(Edge);
    }
    if (Seeds.empty()) return { Name };
    std::vector<std::string> Names;
    for (const GeometrySpace::Parametric::EdgeToken Edge : GeometrySpace::Parametric::GrowTangentChain(*Document.Partition, Owner->Body, Seeds))
    {
        const auto Found = Naming.Edges.find(Edge.Slot);
        if (Found != Naming.Edges.end()) Names.push_back(Found->second);
    }
    return Names;
}

namespace
{

constexpr int RadialLimit = 16;                                                              // [-] - Fins around one edge (non-manifold guard)
constexpr int CycleLimit  = 4096;                                                            // [-] - Fins round one loop (corrupt-cycle guard)

// One body's topology as the Blender walkers see it: edge names, fins round each edge, edge ends at each vertex.
struct SketcherTopologyWalk
{
    const ModelPartition*                 Partition = nullptr;                               // [-] - Owning partition
    const GeometrySpace::Parametric::BodyNaming* Naming    = nullptr;                               // [-] - Persistent names
    std::vector<EdgeToken>                Edges     = {};                                    // [-] - Body edges
    std::map<uint32_t, int>               Valence   = {};                                    // [-] - Vertex slot → edge ends
    EdgeToken                             Start     = {};                                    // [-] - Edge the walk starts on
    bool                                  StartCondition = false;                            // [-] - The name resolved
};

SketcherTopologyWalk PrepareTopologyWalk(const SketcherDocument& Document, const std::string& Name, const std::string& Body)
{
    SketcherTopologyWalk Walk;
    const RegeneratedBody* Owner = FindRegeneratedBody(Document, Body.empty() ? SketcherBodyOfName(Document, SketcherSelectionKind::Edge, Name) : Body);
    if (!Document.Partition || !Owner) return Walk;
    Walk.Partition = Document.Partition.get();
    Walk.Naming    = &Owner->Naming;
    Walk.Edges     = Walk.Partition->CollectInventory(Owner->Body).Edges;
    for (const EdgeToken Edge : Walk.Edges)
    {
        const EdgeProfile& Profile = Walk.Partition->Edges.Resolve(Edge);
        ++Walk.Valence[Profile.Start.Slot];
        ++Walk.Valence[Profile.End.Slot];
        const auto Found = Walk.Naming->Edges.find(Edge.Slot);
        if (!Walk.StartCondition && Found != Walk.Naming->Edges.end() && Found->second == Name) { Walk.Start = Edge; Walk.StartCondition = true; }
    }
    return Walk;
}

[[nodiscard]] int RadialCount(const ModelPartition& Partition, EdgeToken Edge)
{
    const FinToken First = Partition.Edges.Resolve(Edge).RadialFin;
    if (!First.PresentCondition()) return 0;
    int      Count = 0;
    FinToken Fin   = First;
    do
    {
        if (++Count > RadialLimit) return Count;
        Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
    } while (Fin.PresentCondition() && !(Fin == First));
    return Count;
}

// The other fin of a manifold edge (absent at a boundary or non-manifold edge).
[[nodiscard]] FinToken RadialMate(const ModelPartition& Partition, FinToken Fin)
{
    if (RadialCount(Partition, Partition.Fins.Resolve(Fin).Edge) != 2) return {};
    return Partition.Fins.Resolve(Fin).RadialSuccessor;
}

// The neighbouring fin of the same loop that shares Vertex with Fin (BM_loop_other_edge_loop).
[[nodiscard]] FinToken FanNeighbour(const ModelPartition& Partition, FinToken Fin, VertexToken Vertex)
{
    const FinProfile&  Use     = Partition.Fins.Resolve(Fin);
    const EdgeProfile& Edge    = Partition.Edges.Resolve(Use.Edge);
    const VertexToken  Arrival = Use.Sense == Orientation::Forward ? Edge.End : Edge.Start;
    return Arrival == Vertex ? Use.Successor : Use.Predecessor;
}

[[nodiscard]] int LoopFinCount(const ModelPartition& Partition, FinToken Fin)
{
    int      Count = 1;
    FinToken Cycle = Partition.Fins.Resolve(Fin).Successor;
    while (Cycle.PresentCondition() && !(Cycle == Fin) && Count <= CycleLimit) { ++Count; Cycle = Partition.Fins.Resolve(Cycle).Successor; }
    return Count;
}

[[nodiscard]] int FacesAtVertex(const ModelPartition& Partition, const std::vector<EdgeToken>& Edges, VertexToken Vertex)
{
    std::set<uint32_t> Faces;
    for (const EdgeToken Edge : Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        if (!(Profile.Start == Vertex) && !(Profile.End == Vertex)) continue;
        FinToken Fin = Profile.RadialFin;
        for (int Guard = 0; Fin.PresentCondition() && Guard < RadialLimit; ++Guard)
        {
            Faces.insert(Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face.Slot);
            Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
            if (Fin == Profile.RadialFin) break;
        }
    }
    return int(Faces.size());
}

std::vector<std::string> NameWalkedEdges(const SketcherTopologyWalk& Walk, const std::vector<EdgeToken>& Walked, const std::string& Name)
{
    std::vector<std::string> Names;
    for (const EdgeToken Edge : Walked)
    {
        const auto Found = Walk.Naming->Edges.find(Edge.Slot);
        if (Found != Walk.Naming->Edges.end() && std::find(Names.begin(), Names.end(), Found->second) == Names.end()) Names.push_back(Found->second);
    }
    return Names.empty() ? std::vector<std::string>{ Name } : Names;
}

// BMW_EDGERING: breadth-first across even faces (QuadOnly: four-sided only, the face-loop walker); Crossed collects the
// face slots stepped over.
std::vector<EdgeToken> WalkEdgeRing(const SketcherTopologyWalk& Walk, bool QuadOnly, std::vector<FaceToken>* Crossed)
{
    const ModelPartition&  Partition = *Walk.Partition;
    std::vector<EdgeToken> Ring{ Walk.Start };
    std::set<uint32_t>     Chosen{ Walk.Start.Slot };
    std::set<uint32_t>     Faces;
    if (RadialCount(Partition, Walk.Start) > 2) return Ring;                                 // Non-manifold: that edge alone
    for (size_t Cursor = 0; Cursor < Ring.size(); ++Cursor)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Ring[Cursor]);
        if (!Profile.RadialFin.PresentCondition() || RadialCount(Partition, Ring[Cursor]) > 2) continue;
        FinToken Fin = Profile.RadialFin;
        for (int Guard = 0; Guard < RadialLimit; ++Guard)
        {
            const FinProfile&  Use  = Partition.Fins.Resolve(Fin);
            const LoopProfile& Loop = Partition.Loops.Resolve(Use.Loop);
            const int          Size = LoopFinCount(Partition, Fin);
            if (Partition.Faces.Resolve(Loop.Face).Loops.size() == 1 && Size >= 4 && Size % 2 == 0 && (!QuadOnly || Size == 4))
            {
                FinToken Across = Fin;
                for (int Step = 0; Step < Size / 2; ++Step) Across = Partition.Fins.Resolve(Across).Successor;
                const EdgeToken Opposite = Partition.Fins.Resolve(Across).Edge;
                if (Crossed && Faces.insert(Loop.Face.Slot).second) Crossed->push_back(Loop.Face);
                if (RadialCount(Partition, Opposite) <= 2 && Chosen.insert(Opposite.Slot).second) Ring.push_back(Opposite);
            }
            Fin = Use.RadialSuccessor;
            if (!Fin.PresentCondition() || Fin == Profile.RadialFin) break;
        }
    }
    return Ring;
}

} // namespace

std::vector<std::string> SketcherEdgeRing(const SketcherDocument& Document, const std::string& Name, const std::string& Body)
{
    const SketcherTopologyWalk Walk = PrepareTopologyWalk(Document, Name, Body);
    if (!Walk.StartCondition) return { Name };
    return NameWalkedEdges(Walk, WalkEdgeRing(Walk, false, nullptr), Name);
}

std::vector<std::string> SketcherFaceLoop(const SketcherDocument& Document, const std::string& Edge, const std::string& Body)
{
    const SketcherTopologyWalk Walk = PrepareTopologyWalk(Document, Edge, Body);
    if (!Walk.StartCondition) return {};
    const RegeneratedBody* Owner = FindRegeneratedBody(Document, Body.empty() ? SketcherBodyOfName(Document, SketcherSelectionKind::Edge, Edge) : Body);
    std::vector<FaceToken> Crossed;
    (void)WalkEdgeRing(Walk, true, &Crossed);
    std::vector<std::string> Names;
    for (const FaceToken Face : Crossed)
    {
        const auto Found = Owner->Naming.Faces.find(Face.Slot);
        if (Found != Owner->Naming.Faces.end() && std::find(Names.begin(), Names.end(), Found->second) == Names.end()) Names.push_back(Found->second);
    }
    return Names;
}

std::vector<std::string> SketcherEdgeLoop(const SketcherDocument& Document, const std::string& Name, const std::string& Body)
{
    const SketcherTopologyWalk Walk = PrepareTopologyWalk(Document, Name, Body);
    if (!Walk.StartCondition) return { Name };
    const ModelPartition& Partition = *Walk.Partition;
    const auto ValenceOf = [&](VertexToken Vertex) { const auto Found = Walk.Valence.find(Vertex.Slot); return Found == Walk.Valence.end() ? 0 : Found->second; };
    const EdgeProfile& First = Partition.Edges.Resolve(Walk.Start);
    const int          Fins  = RadialCount(Partition, Walk.Start);
    if (Fins == 0 || Fins > 2 || First.Start == First.End) return { Name };                  // Wire, non-manifold or closed: itself
    const bool BoundaryCondition = Fins == 1;

    // f_hub: a three-edge, three-face end beside a face of more than four sides walks round that face.
    FaceToken Hub = {};
    if (!BoundaryCondition)
    {
        bool Pole = false;
        for (const VertexToken End : { First.Start, First.End })
            if (ValenceOf(End) == 3 && FacesAtVertex(Partition, Walk.Edges, End) == 3) Pole = true;
        if (Pole)
        {
            int      Largest = 0;
            FinToken Fin     = First.RadialFin;
            for (int Guard = 0; Guard < 2; ++Guard)
            {
                const int Size = LoopFinCount(Partition, Fin);
                if (Size > Largest) { Largest = Size; Hub = Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face; }
                Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
            }
            if (Largest <= 4) Hub = {};
        }
    }

    // One step on from Edge through its end Vertex (absent when the walk stops there).
    const auto Advance = [&](EdgeToken Edge, VertexToken Vertex) -> EdgeToken
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        if (Profile.Start == Profile.End) return {};
        const int Valence = ValenceOf(Vertex);
        if (Hub.PresentCondition())
        {
            if (Valence != 3) return {};
            FinToken Fin = Profile.RadialFin;
            for (int Guard = 0; Guard < RadialLimit && Fin.PresentCondition(); ++Guard)
            {
                if (Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face == Hub)
                {
                    const EdgeToken Next = Partition.Fins.Resolve(FanNeighbour(Partition, Fin, Vertex)).Edge;
                    return RadialCount(Partition, Next) == 2 ? Next : EdgeToken{};
                }
                Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
                if (Fin == Profile.RadialFin) break;
            }
            return {};
        }
        if (BoundaryCondition)
        {
            // Round the fan to the next boundary edge.
            FinToken Fin = Profile.RadialFin;
            for (int Guard = 0; Guard < 64; ++Guard)
            {
                Fin = FanNeighbour(Partition, Fin, Vertex);
                const EdgeToken Next  = Partition.Fins.Resolve(Fin).Edge;
                const int       Count = RadialCount(Partition, Next);
                if (Count == 1) return Next == Edge ? EdgeToken{} : Next;
                if (Count != 2) return {};
                Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
            }
            return {};
        }
        if (Valence != 4 && Valence != 2) return {};                                         // A pole ends the loop
        FinToken Fin = Profile.RadialFin;
        for (int Step = 0; Step < Valence / 2; ++Step)
        {
            Fin = FanNeighbour(Partition, Fin, Vertex);
            Fin = RadialMate(Partition, Fin);
            if (!Fin.PresentCondition()) return {};
        }
        const EdgeToken Next = Partition.Fins.Resolve(Fin).Edge;
        return Next == Edge ? EdgeToken{} : Next;
    };

    std::vector<EdgeToken> Loop{ Walk.Start };
    std::set<uint32_t>     Chosen{ Walk.Start.Slot };
    for (const VertexToken Toward : { First.End, First.Start })
    {
        EdgeToken   Edge   = Walk.Start;
        VertexToken Vertex = Toward;
        for (int Guard = 0; Guard < int(Walk.Edges.size()); ++Guard)
        {
            const EdgeToken Next = Advance(Edge, Vertex);
            if (!Next.PresentCondition() || !Chosen.insert(Next.Slot).second) break;
            Loop.push_back(Next);
            const EdgeProfile& Profile = Partition.Edges.Resolve(Next);
            Vertex = Profile.Start == Vertex ? Profile.End : Profile.Start;
            Edge   = Next;
        }
    }
    return NameWalkedEdges(Walk, Loop, Name);
}

const char* SketcherEdgePathModeLabel(SketcherEdgePathMode Mode) noexcept
{
    switch (Mode)
    {
    case SketcherEdgePathMode::AlongLoop: return "Along loop";
    case SketcherEdgePathMode::WholeLoop: return "Whole loop";
    case SketcherEdgePathMode::Shortest:  return "Shortest";
    }
    return "Along loop";
}

SketcherEdgePath SketcherEdgePathBetween(const SketcherDocument& Document, const std::string& From, const std::string& To, SketcherEdgePathMode Mode, bool OtherSideCondition,
                                         const std::string& Body)
{
    SketcherEdgePath Path;
    const SketcherTopologyWalk Walk = PrepareTopologyWalk(Document, From, Body);
    if (!Walk.StartCondition) { Path.Refusal = From + " is not an edge of the picked body"; return Path; }
    const ModelPartition& Partition = *Walk.Partition;
    std::map<std::string, EdgeToken> ByName;
    std::map<uint32_t, double>       Lengths;                                                // [len] - Edge slot → length
    std::map<uint32_t, std::vector<EdgeToken>> AtVertex;                                     // Vertex slot → edges ending there
    for (const EdgeToken Edge : Walk.Edges)
    {
        const auto Found = Walk.Naming->Edges.find(Edge.Slot);
        if (Found != Walk.Naming->Edges.end()) ByName.emplace(Found->second, Edge);
        Lengths[Edge.Slot] = MeasureEdgeLength(Partition, Edge);
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        AtVertex[Profile.Start.Slot].push_back(Edge);
        if (!(Profile.End == Profile.Start)) AtVertex[Profile.End.Slot].push_back(Edge);
    }
    const auto Target = ByName.find(To);
    if (Target == ByName.end()) { Path.Refusal = To + " is on another body than " + From; return Path; }
    const EdgeToken First = Walk.Start, Last = Target->second;
    const auto Name = [&](EdgeToken Edge) { return Walk.Naming->Edges.at(Edge.Slot); };
    const auto Ends = [&](EdgeToken Edge) { const EdgeProfile& Profile = Partition.Edges.Resolve(Edge); return std::array<uint32_t, 2>{ Profile.Start.Slot, Profile.End.Slot }; };

    // Dijkstra over vertices from From's ends (or one of them) to To's ends, through Allowed edges only (From and To
    // themselves never inside). Interior: the edges between, in order; empty optional when To is out of reach.
    struct Route { std::vector<EdgeToken> Interior; uint32_t Leave = 0, Arrive = 0; double Length = 0.0; };
    const auto Connect = [&](const std::set<uint32_t>* Allowed, const std::vector<uint32_t>& Sources, const std::vector<uint32_t>& Targets) -> std::optional<Route>
    {
        std::map<uint32_t, double> Best;
        std::map<uint32_t, std::pair<uint32_t, EdgeToken>> Previous;                         // Vertex → (vertex before, edge taken)
        std::map<uint32_t, uint32_t> Origin;                                                 // Vertex → source it grew from
        using Entry = std::pair<double, uint32_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> Open;
        for (const uint32_t Source : Sources) { Best[Source] = 0.0; Origin[Source] = Source; Open.push({ 0.0, Source }); }
        while (!Open.empty())
        {
            const auto [Cost, Vertex] = Open.top();
            Open.pop();
            if (Cost > Best[Vertex]) continue;
            if (std::find(Targets.begin(), Targets.end(), Vertex) != Targets.end())
            {
                Route Found;
                Found.Arrive = Vertex;
                Found.Length = Cost;
                for (uint32_t At = Vertex; Previous.contains(At); At = Previous[At].first) Found.Interior.insert(Found.Interior.begin(), Previous[At].second);
                Found.Leave = Origin[Vertex];
                return Found;
            }
            for (const EdgeToken Edge : AtVertex[Vertex])
            {
                if (Edge == First || Edge == Last || (Allowed && !Allowed->contains(Edge.Slot))) continue;
                const std::array<uint32_t, 2> Pair = Ends(Edge);
                const uint32_t Other = Pair[0] == Vertex ? Pair[1] : Pair[0];
                const double   Next  = Cost + Lengths[Edge.Slot];
                if (Best.contains(Other) && Best[Other] <= Next) continue;
                Best[Other] = Next;
                Previous[Other] = { Vertex, Edge };
                Origin[Other] = Origin[Vertex];
                Open.push({ Next, Other });
            }
        }
        return std::nullopt;
    };
    const std::array<uint32_t, 2> FromEnds = Ends(First), ToEnds = Ends(Last);
    const auto Assemble = [&](const Route& Taken)
    {
        std::vector<std::string> Names{ Name(First) };
        double Total = Lengths[First.Slot];
        for (const EdgeToken Edge : Taken.Interior) { Names.push_back(Name(Edge)); Total += Lengths[Edge.Slot]; }
        if (!(Last == First)) { Names.push_back(Name(Last)); Total += Lengths[Last.Slot]; }
        return std::pair<std::vector<std::string>, double>{ Names, Total };
    };
    if (First == Last) { Path.Edges = { From }; Path.Length = Lengths[First.Slot]; Path.Carrier = "one edge"; return Path; }

    if (Mode == SketcherEdgePathMode::Shortest)
    {
        const std::optional<Route> Taken = Connect(nullptr, { FromEnds[0], FromEnds[1] }, { ToEnds[0], ToEnds[1] });
        if (!Taken) { Path.Refusal = To + " is not connected to " + From + " by edges"; return Path; }
        std::tie(Path.Edges, Path.Length) = Assemble(*Taken);
        Path.Carrier = "any edges";
        return Path;
    }

    // ① Loops holding both edges: the edge loops through either, the rims of the faces beside From, From's tangent chain.
    struct Carrier { std::set<uint32_t> Edges; std::string Caption; };
    std::vector<Carrier> Carriers;
    const auto FromNames = [&](const std::vector<std::string>& Names, const std::string& Caption)
    {
        Carrier Each{ {}, Caption };
        for (const std::string& Member : Names) if (const auto Found = ByName.find(Member); Found != ByName.end()) Each.Edges.insert(Found->second.Slot);
        if (Each.Edges.contains(First.Slot) && Each.Edges.contains(Last.Slot)) Carriers.push_back(std::move(Each));
    };
    FromNames(SketcherEdgeLoop(Document, From, Body), "edge loop");
    FromNames(SketcherEdgeLoop(Document, To, Body), "edge loop");
    {
        FinToken Fin = Partition.Edges.Resolve(First).RadialFin;
        for (int Guard = 0; Fin.PresentCondition() && Guard < RadialLimit; ++Guard)
        {
            const FinProfile& Use = Partition.Fins.Resolve(Fin);
            Carrier Rim{ {}, "rim of a face" };
            const auto FaceName = Walk.Naming->Faces.find(Partition.Loops.Resolve(Use.Loop).Face.Slot);
            if (FaceName != Walk.Naming->Faces.end()) Rim.Caption = "rim of " + FaceName->second;
            FinToken Cycle = Fin;
            for (int Count = 0; Count <= CycleLimit; ++Count)
            {
                Rim.Edges.insert(Partition.Fins.Resolve(Cycle).Edge.Slot);
                Cycle = Partition.Fins.Resolve(Cycle).Successor;
                if (!Cycle.PresentCondition() || Cycle == Fin) break;
            }
            if (Rim.Edges.contains(Last.Slot)) Carriers.push_back(std::move(Rim));
            Fin = Use.RadialSuccessor;
            if (!Fin.PresentCondition() || Fin == Partition.Edges.Resolve(First).RadialFin) break;
        }
    }
    FromNames(SketcherTangentChain(Document, From), "tangent chain");
    if (Carriers.empty())
    {
        Path.Refusal = From + " and " + To + " share no edge loop, face rim or tangent chain (Inspector › Edge path › Shortest takes any edges)";
        return Path;
    }

    // ② One carrier for every mode: the shortest near side, ties to a closed loop (so Other side / Whole loop apply).
    //    Along loop takes its near side (OtherSide: the far side), Whole loop all of it.
    int    Chosen = -1;
    double Best   = 1.0e300;
    bool   BestClosed = false;
    std::optional<Route> ChosenNear, ChosenFar;
    for (int Index = 0; Index < int(Carriers.size()); ++Index)
    {
        const Carrier& Each = Carriers[Index];
        const std::optional<Route> Near = Connect(&Each.Edges, { FromEnds[0], FromEnds[1] }, { ToEnds[0], ToEnds[1] });
        if (!Near) continue;
        std::set<uint32_t> Rest = Each.Edges;
        for (const EdgeToken Edge : Near->Interior) Rest.erase(Edge.Slot);
        const uint32_t FarFrom = FromEnds[0] == Near->Leave ? FromEnds[1] : FromEnds[0];
        const uint32_t FarTo   = ToEnds[0] == Near->Arrive ? ToEnds[1] : ToEnds[0];
        const std::optional<Route> Far = FarFrom == Near->Leave || FarTo == Near->Arrive ? std::nullopt : Connect(&Rest, { FarFrom }, { FarTo });
        const double Ranked = Assemble(*Near).second;
        const bool   Shorter = Ranked < Best - 1.0e-12;
        const bool   Tied    = std::abs(Ranked - Best) <= 1.0e-12;
        if (!Shorter && !(Tied && Far && !BestClosed)) continue;
        Chosen = Index; Best = Ranked; BestClosed = Far.has_value(); ChosenNear = Near; ChosenFar = Far;
    }
    if (Chosen >= 0)
    {
        const Carrier& Each  = Carriers[Chosen];
        const Route&   Taken = OtherSideCondition && ChosenFar ? *ChosenFar : *ChosenNear;
        std::tie(Path.Edges, Path.Length) = Assemble(Taken);
        if (Mode == SketcherEdgePathMode::WholeLoop)
        {
            for (const uint32_t Slot : Each.Edges)
            {
                const std::string Member = Walk.Naming->Edges.at(Slot);
                if (std::find(Path.Edges.begin(), Path.Edges.end(), Member) == Path.Edges.end()) { Path.Edges.push_back(Member); Path.Length += Lengths[Slot]; }
            }
        }
        Path.Carrier         = Each.Caption;
        Path.ClosedCondition = ChosenFar.has_value();
    }
    if (Path.Edges.empty()) Path.Refusal = From + " and " + To + " lie on one loop but no side of it joins them";
    return Path;
}

static std::string ExecuteCombineBodySteps(
    SketcherDocument&               Document,
    const std::string&              Target,
    const std::vector<std::string>& Tools,
    FeatureCombination              Combination)
{
    if (Combination != FeatureCombination::Union && Combination != FeatureCombination::Difference && Combination != FeatureCombination::Intersection)
        return "choose Union, Difference or Intersection";
    if (Tools.empty()) return "select two or more bodies (the first is the target)";
    if (!FindRegeneratedBody(Document, Target)) return "no body named " + Target;
    std::set<std::string> Seen{ Target };
    for (const std::string& Tool : Tools)
    {
        if (!FindRegeneratedBody(Document, Tool)) return "no body named " + Tool;
        if (!Seen.insert(Tool).second) return SketcherBodyCaption(Document, Tool) + " is picked twice (the target cannot be a tool)";
    }
    const std::string Caption = NextCaption(Document, "Combine");
    for (const std::string& Tool : Tools)
    {
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Combine;
        Step.Label       = "Combine#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Combination = Combination;
        Step.Target      = Target;
        Step.Tool        = Tool;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
    }
    Document.Selection = {};
    return {};
}

SketcherCommandOutcome CombineSketcherBodies(SketcherDocument& Document, const std::string& Target, const std::vector<std::string>& Tools, FeatureCombination Combination)
{
    std::string Operands = SketcherBodyCaption(Document, Target);
    const char* Sign = Combination == FeatureCombination::Difference ? " - " : Combination == FeatureCombination::Intersection ? " & " : " + ";
    for (const std::string& Tool : Tools) Operands += Sign + SketcherBodyCaption(Document, Tool);
    const std::string Label = std::string("Combine · ") + FeatureCombinationLabel(Combination) + " (" + Operands + ")";
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        return ExecuteCombineBodySteps(Document, Target, Tools, Combination);
    });
}

SketcherCommandOutcome CutSketcherBody(SketcherDocument& Document, const SketcherCutRequest& Request)
{
    const std::string Target = Request.Target.empty() ? Document.Regeneration.Active : Request.Target;
    const std::string Label  = "Cut (" + SketcherBodyCaption(Document, Target) + " by " + SketcherBodyCaption(Document, Request.Cutter) + ")";
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (Target.empty()) return "pick the body to cut (mode 4), or build one first";
        if (!FindRegeneratedBody(Document, Target)) return "no body named " + Target;
        const FeatureStep* Cutter = nullptr;
        for (const FeatureStep& Step : Document.Tree.Steps) if (Step.Label == Request.Cutter) Cutter = &Step;
        if (!Cutter || (Cutter->Kind != FeatureStepKind::Sketch && Cutter->Kind != FeatureStepKind::SpaceCurve))
            return "choose the cutter: focus a sketch step or select a curve";
        FeatureStep Step;
        Step.Kind   = FeatureStepKind::Cut;
        Step.Label  = "Cut#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Target = Target;
        if (Cutter->Kind == FeatureStepKind::Sketch) Step.Profile = Cutter->Label;
        else
        {
            Step.Sections    = { Cutter->Label };
            Step.Plane.ZAxis = Request.Direction;
            Step.Plane.XAxis = std::fabs(Request.Direction.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
        }
        Document.Captions[Step.Label] = NextCaption(Document, "Cut");
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Selection = {};
        return {};
    });
}

SketcherCommandOutcome RemoveSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys)
{
    std::string Operands;
    for (const std::string& Key : Keys) Operands += (Operands.empty() ? "" : ", ") + SketcherBodyCaption(Document, Key);
    return RunCommand(Document, "Remove (" + Operands + ")", false, [&]() -> std::string
    {
        if (Keys.empty()) return "pick the bodies to remove (mode 4)";
        std::set<std::string> Seen;
        for (const std::string& Key : Keys)
        {
            if (!FindRegeneratedBody(Document, Key)) return "no body named " + Key;
            if (!Seen.insert(Key).second) continue;
            FeatureStep Step;
            Step.Kind   = FeatureStepKind::Remove;
            Step.Label  = "Remove#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Target = Key;
            Document.Captions[Step.Label] = NextCaption(Document, "Remove");
            Document.Tree.Steps.push_back(std::move(Step));
        }
        Document.Selection = {};
        return {};
    });
}

namespace
{
    [[nodiscard]] OrthonormalFrame ElevatedPlane(const FeatureTree& Tree, const FeatureStep& Step)
    {
        OrthonormalFrame Plane = Step.Plane;
        Plane.Origin = Plane.Origin + Plane.ZAxis * EvaluateFeatureValue(Tree, Step.Elevation);
        return Plane;
    }

    [[nodiscard]] const SketchModel& SolvedSketch(const SketcherDocument& Document, const FeatureStep& Step)
    {
        const auto Found = Document.Regeneration.Sketches.find(Step.Label);
        return Found != Document.Regeneration.Sketches.end() ? Found->second : Step.Sketch;
    }

    // 📝 A segment of a sketch on plane From, re-expressed in the coordinates of the coplanar plane To (rotation, offset,
    //    and a mirror when the normals oppose: arcs then run the other way). Exact for lines, arcs, circles and B-splines.
    [[nodiscard]] ProfileSegment MapSegment(ProfileSegment Segment, const OrthonormalFrame& From, const OrthonormalFrame& To)
    {
        auto Map = [&](Vector2 Point)
        {
            const Vector3 Offset = From.Origin + From.XAxis * Point.X + From.YAxis * Point.Y - To.Origin;
            return Vector2{ Dot(Offset, To.XAxis), Dot(Offset, To.YAxis) };
        };
        const double Turn = Dot(From.XAxis, To.XAxis) * Dot(From.YAxis, To.YAxis) - Dot(From.YAxis, To.XAxis) * Dot(From.XAxis, To.YAxis);
        Segment.Start = Map(Segment.Start);
        Segment.End   = Map(Segment.End);
        if (Segment.Kind == ProfileSegmentKind::Line) return Segment;
        if (Segment.Kind == ProfileSegmentKind::Spline)
        {
            for (Vector2& Pole : Segment.Spline.Poles) Pole = Map(Pole);
            return Segment;
        }
        const Vector2 Origin = Map(Segment.Centre);
        const Vector2 Rim    = Map(Segment.Centre + Vector2{ std::cos(Segment.StartAngle), std::sin(Segment.StartAngle) } * Segment.Radius);
        Segment.Centre     = Origin;
        Segment.StartAngle = std::atan2(Rim.Y - Origin.Y, Rim.X - Origin.X);
        if (Turn < 0.0) Segment.Sweep = -Segment.Sweep;
        return Segment;
    }
}

SketchTrimOutcome PreviewSketcherTrim(const SketcherDocument& Document, const SketcherTrimRequest& Request)
{
    const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Request.Sketch; });
    if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::Sketch)
    {
        SketchTrimOutcome Outcome;
        Outcome.Refusal = "no sketch step labelled " + Request.Sketch;
        return Outcome;
    }
    if (Request.InsertCondition) return InsertSketchCurvePoint(SolvedSketch(Document, *Found), Request.Curve, Request.Pick);
    if (Request.SplitCondition) return SplitSketchCurve(SolvedSketch(Document, *Found), Request.Curve, Request.Pick);
    const OrthonormalFrame      Plane = ElevatedPlane(Document.Tree, *Found);
    std::vector<ProfileSegment> Foreign;
    for (const FeatureStep& Step : Document.Tree.Steps)
    {
        if (Step.Kind != FeatureStepKind::Sketch || Step.Label == Request.Sketch || Document.Appearance.HiddenCurves.contains(Step.Label)) continue;
        const OrthonormalFrame Other = ElevatedPlane(Document.Tree, Step);
        const Vector3          Apart = Other.Origin - Plane.Origin;
        if (std::fabs(Dot(Other.ZAxis, Plane.ZAxis)) < 1.0 - 1.0e-9 || std::fabs(Dot(Apart, Plane.ZAxis)) > 1.0e-9 * std::max(1.0, Length(Apart))) continue;
        const SketchModel& Sketch = SolvedSketch(Document, Step);
        for (int Curve = 0; Curve < static_cast<int>(Sketch.Curves.size()); ++Curve)
        {
            std::string    Refusal;
            ProfileSegment Segment = ResolveSketchCurveSegment(Sketch, Curve, 1.0e-9, Refusal);
            if (Refusal.empty()) Foreign.push_back(MapSegment(std::move(Segment), Other, Plane));
        }
    }
    return TrimSketchCurve(SolvedSketch(Document, *Found), Request.Curve, Request.Pick, Foreign);
}

SketcherRegionPreview PreviewSketcherRegion(const SketcherDocument& Document, const SketcherRegionRequest& Request)
{
    SketcherRegionPreview Preview;
    const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Request.Sketch; });
    if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::Sketch) { Preview.Refusal = "no sketch step labelled " + Request.Sketch; return Preview; }
    Preview.Plane = ElevatedPlane(Document.Tree, *Found);
    // ① every drawn curve of this sketch and of the shown sketches in its plane, in its coordinates
    std::vector<ProfileSegment> Segments;
    std::map<std::string, int>  Drawn;                                                       // Sketch label → curves it gave
    double Scale = 1.0;
    for (const FeatureStep& Step : Document.Tree.Steps)
    {
        if (Step.Kind != FeatureStepKind::Sketch) continue;
        if (Step.Label != Request.Sketch && Document.Appearance.HiddenCurves.contains(Step.Label)) continue;
        const OrthonormalFrame Other = ElevatedPlane(Document.Tree, Step);
        const Vector3          Apart = Other.Origin - Preview.Plane.Origin;
        if (std::fabs(Dot(Other.ZAxis, Preview.Plane.ZAxis)) < 1.0 - 1.0e-9 || std::fabs(Dot(Apart, Preview.Plane.ZAxis)) > 1.0e-9 * std::max(1.0, Length(Apart))) continue;
        const SketchModel& Sketch = SolvedSketch(Document, Step);
        Scale = std::max(Scale, MeasureSketchScale(Sketch));
        for (int Curve = 0; Curve < static_cast<int>(Sketch.Curves.size()); ++Curve)
        {
            if (Sketch.Curves[Curve].Construction) continue;
            std::string    Refusal;
            ProfileSegment Segment = ResolveSketchCurveSegment(Sketch, Curve, 1.0e-9, Refusal);
            if (!Refusal.empty()) continue;
            Segments.push_back(Step.Label == Request.Sketch ? std::move(Segment) : MapSegment(std::move(Segment), Other, Preview.Plane));
            Preview.Sources.push_back(Step.Label);
            ++Drawn[Step.Label];
        }
    }
    // ② the region, and whether it is one sketch's own loops (every piece a whole curve of it, all its curves used)
    Preview.Region = LocateSketchRegion(Segments, Request.Pick, Scale);
    if (!Preview.Region.Refusal.empty()) { Preview.Refusal = Preview.Region.Refusal; return Preview; }
    std::set<int> Used;
    std::string   Owner;
    bool          WholeCondition = true;
    for (const std::vector<RegionPiece>& Loop : Preview.Region.Loops)
        for (const RegionPiece& Piece : Loop)
        {
            const std::string& Source = Preview.Sources[Piece.Source];
            if (Owner.empty()) Owner = Source;
            WholeCondition = WholeCondition && Source == Owner && Piece.FromFraction <= 1.0e-9 && Piece.ToFraction >= 1.0 - 1.0e-9;
            Used.insert(Piece.Source);
        }
    if (WholeCondition && static_cast<int>(Used.size()) == Drawn[Owner])
    {
        const auto Solved = Document.Regeneration.Sketches.find(Owner);
        if (Solved != Document.Regeneration.Sketches.end() && ExtractSketchProfile(Solved->second).Refusal.empty()) Preview.WholeSketch = Owner;
    }
    return Preview;
}

SketcherCommandOutcome FillSketcherRegion(SketcherDocument& Document, const SketcherRegionRequest& Request)
{
    const SketcherRegionPreview Preview = PreviewSketcherRegion(Document, Request);
    if (Preview.Refusal.empty() && !Preview.WholeSketch.empty()) return FillSketcherSketch(Document, Preview.WholeSketch, true);
    const std::string Caption = NextCaption(Document, "Fill");
    return RunCommand(Document, "Fill region into " + Caption, false, [&]() -> std::string
    {
        if (!Preview.Refusal.empty()) return "Fill: " + Preview.Refusal;
        FeatureStep Step;
        Step.Kind          = FeatureStepKind::Sketch;
        Step.Label         = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Plane         = Preview.Plane;
        Step.Sketch        = ComposeRegionSketch(Preview.Region);
        Step.FillCondition = true;
        if (Step.Sketch.Curves.empty()) return "Fill: the region has no boundary";
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

SketcherCommandOutcome TrimSketcherSketch(SketcherDocument& Document, const SketcherTrimRequest& Request)
{
    const auto  Caption = Document.Captions.find(Request.Sketch);
    const std::string Owner = Caption != Document.Captions.end() ? Caption->second : Request.Sketch;
    std::string CurveName = "curve " + std::to_string(Request.Curve);
    for (const FeatureStep& Step : Document.Tree.Steps)
    {
        if (Step.Label != Request.Sketch || Step.Kind != FeatureStepKind::Sketch) continue;
        const SketchModel& Sketch = SolvedSketch(Document, Step);
        if (Request.Curve >= 0 && Request.Curve < static_cast<int>(Sketch.Curves.size())) CurveName = SketchCurveName(Sketch.Curves[Request.Curve]);
    }
    const bool Referenced = SketcherSketchReferencedCondition(Document, Request.Sketch);

    // ① in place: the sketch itself changes (later steps follow when they still regenerate).
    const std::string Verb = Request.InsertCondition ? "Insert point on " : Request.SplitCondition ? "Cut " : "Trim ";
    SketcherCommandOutcome InPlace = RunCommand(Document, Verb + CurveName + " of " + Owner, false, [&]() -> std::string
    {
        const SketchTrimOutcome Trim = PreviewSketcherTrim(Document, Request);
        if (!Trim.Refusal.empty()) return Trim.Refusal;
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Request.Sketch; });
        if (!Trim.Sketch.Curves.empty())
        {
            Found->Sketch = Trim.Sketch;
            return {};
        }
        if (Referenced) return "the trim would leave " + Owner + " without curves, and a later step uses it";
        Document.Tree.Steps.erase(Found);
        Document.Captions.erase(Request.Sketch);
        return {};
    }, 0);
    if (InPlace.AcceptedCondition || !Referenced || !PreviewSketcherTrim(Document, Request).Refusal.empty()) return InPlace;

    // ② detached: the original stays (hidden) for the steps that read it; the trimmed curves become a free sketch.
    std::string Stem = Owner;
    while (!Stem.empty() && std::isdigit(static_cast<unsigned char>(Stem.back()))) Stem.pop_back();
    if (Stem.empty()) Stem = "Sketch";
    const std::string Copy = NextCaption(Document, Stem);
    return RunCommand(Document, Verb + CurveName + " of " + Owner + " into " + Copy + " (" + Owner + " kept hidden for the steps that read it)", false,
                      [&]() -> std::string
    {
        const SketchTrimOutcome Trim = PreviewSketcherTrim(Document, Request);
        if (!Trim.Refusal.empty()) return Trim.Refusal;
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Request.Sketch; });
        if (Found == Document.Tree.Steps.end()) return "no sketch step labelled " + Request.Sketch;
        Document.Appearance.HiddenCurves.insert(Request.Sketch);
        std::erase(Document.Selection.Curves, Request.Sketch);
        if (Trim.Sketch.Curves.empty()) return {};
        FeatureStep Detached = *Found;
        Detached.Label  = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        Detached.Sketch = Trim.Sketch;
        Document.Captions[Detached.Label] = Copy;
        Document.Tree.Steps.push_back(std::move(Detached));
        return {};
    });
}

namespace
{
    // 📝 The cut at Point on every shown sketch: Edited holds each changed sketch (solved model with its curves split),
    //    Outcome the counts and the preview geometry.
    void CutSketchesAtPoint(const SketcherDocument& Document, const Vector3& Point, std::map<std::string, SketchModel>& Edited, SketcherPointCut& Outcome)
    {
        constexpr double EndGuard = 1.0e-3;                                                  // [-]   - As SplitSketchCurve: no break at an open end
        const double     Tolerance = 1.0e-6 * std::max(1.0, Length(Point));                   // [len] - Point on a plane / curve
        const auto Shown = [&](const FeatureStep& Step) { return Step.Kind == FeatureStepKind::Sketch && !Document.Appearance.HiddenCurves.contains(Step.Label); };
        for (const FeatureStep& Step : Document.Tree.Steps)
        {
            if (!Shown(Step)) continue;
            const OrthonormalFrame Plane = ElevatedPlane(Document.Tree, Step);
            if (std::fabs(Dot(Point - Plane.Origin, Plane.ZAxis)) > Tolerance) continue;
            const Vector2 Pick{ Dot(Point - Plane.Origin, Plane.XAxis), Dot(Point - Plane.Origin, Plane.YAxis) };
            // Curves of the other shown sketches in this plane (a circle's second break may come from them).
            std::vector<ProfileSegment> Foreign;
            for (const FeatureStep& Other : Document.Tree.Steps)
            {
                if (!Shown(Other) || Other.Label == Step.Label) continue;
                const OrthonormalFrame Frame = ElevatedPlane(Document.Tree, Other);
                const Vector3          Apart = Frame.Origin - Plane.Origin;
                if (std::fabs(Dot(Frame.ZAxis, Plane.ZAxis)) < 1.0 - 1.0e-9 || std::fabs(Dot(Apart, Plane.ZAxis)) > 1.0e-9 * std::max(1.0, Length(Apart))) continue;
                const SketchModel& Sketch = SolvedSketch(Document, Other);
                for (int Curve = 0; Curve < static_cast<int>(Sketch.Curves.size()); ++Curve)
                {
                    std::string    Refusal;
                    ProfileSegment Segment = ResolveSketchCurveSegment(Sketch, Curve, 1.0e-9, Refusal);
                    if (Refusal.empty()) Foreign.push_back(MapSegment(std::move(Segment), Frame, Plane));
                }
            }
            SketchModel Model   = SolvedSketch(Document, Step);
            int         Changed = 0;
            for (int Round = 0; Round < 64; ++Round)
            {
                bool Split = false;
                for (int Curve = 0; Curve < static_cast<int>(Model.Curves.size()) && !Split; ++Curve)
                {
                    if (Model.Curves[Curve].Construction) continue;
                    std::string          Refusal;
                    const ProfileSegment Target = ResolveSketchCurveSegment(Model, Curve, 1.0e-9, Refusal);
                    if (!Refusal.empty() || Target.Kind == ProfileSegmentKind::Spline) continue;
                    const double Fraction = LocateProfileFraction(Target, Pick);
                    if (Distance(EvaluateProfileSegment(Target, Fraction), Pick) > Tolerance) continue;
                    const bool Closed = Target.Kind == ProfileSegmentKind::Circle;
                    if (!Closed && (Fraction <= EndGuard || Fraction >= 1.0 - EndGuard)) continue;   // Touches Point at an end
                    // A circle's partner: its next crossing counter-clockwise with another curve of the plane.
                    std::optional<Vector2> Partner;
                    if (Closed)
                    {
                        double Nearest = 2.0;                                                 // [-] - Fraction gap past the pick
                        const auto Consider = [&](const ProfileSegment& Cutter)
                        {
                            for (const SegmentCrossing& Crossing : IntersectProfileSegments(Target, Cutter, 1.0e-9 * std::max(1.0, Target.Radius)))
                            {
                                const double Gap = Crossing.TargetFraction - Fraction - std::floor(Crossing.TargetFraction - Fraction);
                                if (Gap > EndGuard && Gap < 1.0 - EndGuard && Gap < Nearest) { Nearest = Gap; Partner = Crossing.Position; }
                            }
                        };
                        for (int Other = 0; Other < static_cast<int>(Model.Curves.size()); ++Other)
                        {
                            if (Other == Curve) continue;
                            std::string          Ignored;
                            const ProfileSegment Cutter = ResolveSketchCurveSegment(Model, Other, 1.0e-9, Ignored);
                            if (Ignored.empty()) Consider(Cutter);
                        }
                        for (const ProfileSegment& Cutter : Foreign) Consider(Cutter);
                    }
                    const SketchTrimOutcome Cut = SplitSketchCurve(Model, Curve, Pick, Partner);
                    if (!Cut.Refusal.empty()) continue;
                    std::vector<Vector3> Samples;
                    for (int Index = 0; Index <= 48; ++Index)
                    {
                        const Vector2 Sample = EvaluateProfileSegment(Target, Index / 48.0);
                        Samples.push_back(Plane.Origin + Plane.XAxis * Sample.X + Plane.YAxis * Sample.Y);
                    }
                    Outcome.Lit.push_back(std::move(Samples));
                    for (const Vector2& Break : Cut.SplitPositions) Outcome.Breaks.push_back(Plane.Origin + Plane.XAxis * Break.X + Plane.YAxis * Break.Y);
                    Model = Cut.Sketch;
                    ++Changed;
                    Split = true;
                }
                if (!Split) break;
            }
            if (Changed == 0) continue;
            Outcome.Curves += Changed;
            Outcome.Sketches.push_back(Step.Label);
            Edited[Step.Label] = std::move(Model);
        }
    }
}

SketcherPointCut PreviewSketcherPointCut(const SketcherDocument& Document, const Vector3& Point)
{
    SketcherPointCut                   Outcome;
    std::map<std::string, SketchModel> Edited;
    CutSketchesAtPoint(Document, Point, Edited, Outcome);
    return Outcome;
}

SketcherCommandOutcome CutSketcherSketchesAt(SketcherDocument& Document, const Vector3& Point)
{
    SketcherPointCut                   Preview;
    std::map<std::string, SketchModel> Edited;
    CutSketchesAtPoint(Document, Point, Edited, Preview);
    std::string Owners;
    for (const std::string& Label : Preview.Sketches)
    {
        const auto Caption = Document.Captions.find(Label);
        Owners += (Owners.empty() ? "" : ", ") + (Caption != Document.Captions.end() ? Caption->second : Label);
    }
    char Where[96];
    std::snprintf(Where, sizeof Where, "(%.4g, %.4g, %.4g)", Point.X, Point.Y, Point.Z);
    return RunCommand(Document, "Cut " + std::to_string(Preview.Curves) + " curve(s) at " + Where + (Owners.empty() ? std::string{} : " · " + Owners), false, [&]() -> std::string
    {
        if (Preview.Curves == 0) return "no curve passes through that point (hold Ctrl to snap onto a crossing, an end or a midpoint)";
        for (auto& [Label, Model] : Edited)
        {
            const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Label; });
            if (Found == Document.Tree.Steps.end()) return "no sketch step labelled " + Label;
            Found->Sketch = Model;
        }
        return {};
    }, 0);
}

SketcherCommandOutcome CutSketcherSketchCorners(SketcherDocument& Document, const SketcherCornerRequest& Request)
{
    const bool        FilletCondition = Request.Shape == SketchCornerShape::Fillet;
    std::string       Owners;
    for (const std::string& Sketch : Request.Sketches)
    {
        const auto Caption = Document.Captions.find(Sketch);
        Owners += (Owners.empty() ? "" : ", ") + (Caption != Document.Captions.end() ? Caption->second : Sketch);
    }
    char Amount[48];
    std::snprintf(Amount, sizeof Amount, "%.4g", Request.Size);
    const std::string Corners = Request.Points.empty() ? std::string("every corner") : std::to_string(Request.Points.size()) + (Request.Points.size() == 1 ? " corner" : " corners");
    const std::string Label   = std::string(FilletCondition ? "Fillet r " : "Chamfer ") + Amount + " · " + Corners + " of " + Owners;
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (Request.Sketches.empty()) return std::string("pick a sketch, or corner points of one (mode 1), to ") + (FilletCondition ? "fillet" : "chamfer");
        if (!Request.Points.empty() && Request.Sketches.size() != 1) return "corner points belong to one sketch";
        for (const std::string& Sketch : Request.Sketches)
        {
            const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Sketch; });
            if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::Sketch) return "no sketch step labelled " + Sketch;
            const SketchCornerOutcome Cut = CutSketchCorners(SolvedSketch(Document, *Found), Request.Points, Request.Size, Request.Shape);
            const auto Caption = Document.Captions.find(Sketch);
            if (!Cut.Refusal.empty()) return (Caption != Document.Captions.end() ? Caption->second : Sketch) + ": " + Cut.Refusal;
            Found->Sketch = Cut.Sketch;
        }
        return {};
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EDITING (G / R / S · SHIFT+D · CTRL+V · H · J · O · P)
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] std::string ListCaptions(const SketcherDocument& Document, const std::vector<std::string>& Keys)
    {
        std::string Operands;
        for (const std::string& Key : Keys) Operands += (Operands.empty() ? "" : ", ") + SketcherBodyCaption(Document, Key);
        return Operands;
    }

    [[nodiscard]] std::vector<std::string> UniqueKeys(const std::vector<std::string>& Keys)
    {
        std::vector<std::string> Unique;
        for (const std::string& Key : Keys) if (std::find(Unique.begin(), Unique.end(), Key) == Unique.end()) Unique.push_back(Key);
        return Unique;
    }

    [[nodiscard]] const FeatureStep* FindStep(const SketcherDocument& Document, const std::string& Label)
    {
        for (const FeatureStep& Step : Document.Tree.Steps) if (Step.Label == Label) return &Step;
        return nullptr;
    }

    // 📝 A sketch step through an affine map (G / R / S, Shift+D copies). Rigid: the plane is carried, the model kept.
    //    Otherwise the solved points go through Map into the image plane (x along the image of the old x-axis), circle radii
    //    scale, dimensions are re-measured (a bound parameter lets go of its value) and fixed points follow. A uniform scale
    //    keeps every relation; a stretch (S then X / Y / Z) refuses arcs and circles (they would become ellipses) and drops
    //    the relations it breaks (perpendicular, equal length, symmetric, G2, end directions; vertical when sheared).
    [[nodiscard]] std::string CarrySketchStep(const SketcherDocument& Document, FeatureStep& Step, const AffineMap& Map, int& Dropped)
    {
        const auto Turn = [&](Vector3 Vector) { return Map.ColumnX * Vector.X + Map.ColumnY * Vector.Y + Map.ColumnZ * Vector.Z; };
        const Vector3 Columns[3] = { Map.ColumnX, Map.ColumnY, Map.ColumnZ };
        bool RigidCondition = true;
        for (int First = 0; First < 3; ++First)
            for (int Second = 0; Second < 3; ++Second)
                RigidCondition = RigidCondition && std::abs(Dot(Columns[First], Columns[Second]) - (First == Second ? 1.0 : 0.0)) <= 1.0e-9;
        OrthonormalFrame& Plane = Step.Plane;
        if (RigidCondition)
        {
            if (Dot(Cross(Map.ColumnX, Map.ColumnY), Map.ColumnZ) < 0.0) return "a sketch cannot be mirrored (🚧)";
            Plane.Origin = Turn(Plane.Origin) + Map.Shift;
            Plane.XAxis  = Normalize(Turn(Plane.XAxis));
            Plane.YAxis  = Normalize(Turn(Plane.YAxis));
            Plane.ZAxis  = Normalize(Turn(Plane.ZAxis));
            return {};
        }
        // ① the image plane and the in-plane map A = [[A11, A12], [0, A22]] in its frame
        const Vector3 ImageX = Turn(Plane.XAxis), ImageY = Turn(Plane.YAxis);
        const double  A11    = Length(ImageX);
        if (!(A11 > 1.0e-12)) return "a scale of 0 flattens the sketch";
        const Vector3 NewX = ImageX * (1.0 / A11);
        const double  A12  = Dot(ImageY, NewX);
        const Vector3 Rest = ImageY - NewX * A12;
        const double  A22  = Length(Rest);
        if (!(A22 > 1.0e-12 * std::max(1.0, A11))) return "a scale of 0 flattens the sketch";
        const Vector3 NewY = Rest * (1.0 / A22);
        const bool    UniformCondition = std::abs(A12) <= 1.0e-9 * A11 && std::abs(A22 - A11) <= 1.0e-9 * A11;
        SketchModel Model = SolvedSketch(Document, Step);                                    // As shown (solved positions)
        if (!UniformCondition)
            for (const SketchCurve& Curve : Model.Curves)
                if (Curve.Kind == SketchCurveKind::Circle || Curve.Kind == SketchCurveKind::Arc)
                    return "a stretch would turn " + SketchCurveName(Curve) + " into an ellipse (🚧): press S without an axis to scale evenly";
        const auto Apply = [&](Vector2 Point) { return Vector2{ A11 * Point.X + A12 * Point.Y, A22 * Point.Y }; };
        for (SketchPoint& Point : Model.Points) Point.Position = Apply(Point.Position);
        for (SketchCurve& Curve : Model.Curves) if (Curve.Kind == SketchCurveKind::Circle) Curve.Radius *= A11;
        std::vector<SketchConstraint> Kept;
        for (SketchConstraint Constraint : Model.Constraints)
        {
            using Kind = SketchConstraintKind;
            if (!UniformCondition && (Constraint.Kind == Kind::Perpendicular || Constraint.Kind == Kind::EqualLength || Constraint.Kind == Kind::Symmetric
                                      || Constraint.Kind == Kind::SplineCurvature || Constraint.Kind == Kind::SplineDirection
                                      || (Constraint.Kind == Kind::Vertical && std::abs(A12) > 1.0e-9 * A11)))
            {
                ++Dropped;
                continue;
            }
            if (Constraint.Kind == Kind::Fixed)
            {
                const Vector2 Held = Apply({ Constraint.Value, Constraint.SecondValue });
                Constraint.Value = Held.X; Constraint.SecondValue = Held.Y;
                Constraint.Parameter.clear();
            }
            const double Measured = MeasureSketchDimension(Model, Constraint);
            if (std::isfinite(Measured) && std::abs(Measured - Constraint.Value) > 1.0e-12 * std::max(1.0, std::abs(Measured)))
            {
                Constraint.Value = Measured;
                Constraint.Parameter.clear();                                                // The scaled value, not the parameter
            }
            Kept.push_back(std::move(Constraint));
        }
        Model.Constraints = std::move(Kept);
        // A literal elevation keeps its value: the origin moves so the lifted plane lands where Map takes it.
        const double  Lift    = Step.Elevation.Parameter.empty() ? Step.Elevation.Literal : 0.0;   // [len]
        const Vector3 NewZ    = Normalize(Cross(NewX, NewY));
        Step.Sketch  = std::move(Model);
        Plane.Origin = Turn(Plane.Origin + Plane.ZAxis * Lift) + Map.Shift - NewZ * Lift;
        Plane.XAxis  = NewX;
        Plane.YAxis  = NewY;
        Plane.ZAxis  = NewZ;
        return {};
    }
}

namespace
{

void CarryDimensionsWithAffine(SketcherDocument& Document, const std::vector<std::string>& Keys, const AffineMap& Map, bool SketchCondition)
{
    if (Document.Dimensions.empty() || Keys.empty()) return;
    const auto ApplyLinear = [&](Vector3 Vec) -> Vector3
    {
        return Map.ColumnX * Vec.X + Map.ColumnY * Vec.Y + Map.ColumnZ * Vec.Z;
    };
    const double Det   = std::abs(Dot(Cross(Map.ColumnX, Map.ColumnY), Map.ColumnZ));
    const double Scale = std::isfinite(Det) && Det > 1.0e-12 ? std::cbrt(Det) : 1.0;

    const auto MatchesTarget = [&](SketcherSelectionKind Kind, const std::string& Name) -> bool
    {
        if (Name.empty()) return false;
        const size_t At = Name.find('@');
        if (At != std::string::npos)
        {
            if (!SketchCondition) return false;
            const std::string StepLabel = Name.substr(0, At);
            return std::find(Keys.begin(), Keys.end(), StepLabel) != Keys.end();
        }
        if (SketchCondition) return false;
        if (Kind == SketcherSelectionKind::Body)
            return std::find(Keys.begin(), Keys.end(), Name) != Keys.end();
        const std::string Owner = SketcherBodyOfName(Document, Kind, Name);
        return !Owner.empty() && std::find(Keys.begin(), Keys.end(), Owner) != Keys.end();
    };

    for (SketcherDimension& Dim : Document.Dimensions)
    {
        if (!MatchesTarget(Dim.FirstKind, Dim.FirstName) && !MatchesTarget(Dim.SecondKind, Dim.SecondName)) continue;
        const Vector3 RotN = ApplyLinear(Dim.PlaneNormal);
        if (Length(RotN) > 1.0e-9) Dim.PlaneNormal = Normalize(RotN);
        const Vector3 RotD = ApplyLinear(Dim.Direction);
        if (Length(RotD) > 1.0e-9) Dim.Direction = Normalize(RotD);
        if (std::abs(Scale - 1.0) > 1.0e-6) Dim.Offset *= Scale;
    }
}

} // namespace

SketcherCommandOutcome TransformSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys, const AffineMap& Map, const std::string& Verb)
{
    const std::vector<std::string> Unique = UniqueKeys(Keys);
    return RunCommand(Document, Verb + " (" + ListCaptions(Document, Unique) + ")", false, [&]() -> std::string
    {
        if (Unique.empty()) return "pick the bodies to " + Verb + " (mode 4)";
        for (const std::string& Key : Unique) if (!FindRegeneratedBody(Document, Key)) return "no body named " + Key;
        CarryDimensionsWithAffine(Document, Unique, Map, false);
        const std::string Caption = NextCaption(Document, Verb);
        for (const std::string& Key : Unique)
        {
            FeatureStep Step;
            Step.Kind      = FeatureStepKind::Transform;
            Step.Label     = "Transform#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Target    = Key;
            Step.Placement = Map;
            Document.Captions[Step.Label] = Caption;
            Document.Tree.Steps.push_back(std::move(Step));
        }
        return {};
    });
}

SketcherCommandOutcome PlaceSketcherSketches(SketcherDocument& Document, const std::vector<std::string>& Labels, const AffineMap& Map, const std::string& Verb)
{
    const std::vector<std::string> Unique = UniqueKeys(Labels);
    const auto CarrySpaceCurve = [](FeatureStep& Step, const AffineMap& Transform) -> std::string
    {
        if (!Step.CurveOffsetGuide.empty()) return "Detach the live surface offset before transforming its result";
        const double Det = Dot(Cross(Transform.ColumnX, Transform.ColumnY), Transform.ColumnZ);
        if (!std::isfinite(Det) || std::abs(Det) <= 1.0e-12) return "a scale of 0 flattens the curve";
        const auto Apply = [&](Vector3 Point)
        {
            return Transform.ColumnX * Point.X + Transform.ColumnY * Point.Y + Transform.ColumnZ * Point.Z + Transform.Shift;
        };
        for (Vector3& Point : Step.Points)
        {
            Point = Apply(Point);
            if (!std::isfinite(Point.X + Point.Y + Point.Z)) return "transformed curve point is not finite";
        }
        for (FeaturePoleEdit& Edit : Step.PoleEdits)
        {
            Edit.Position = Apply(Edit.Position);
            if (!std::isfinite(Edit.Position.X + Edit.Position.Y + Edit.Position.Z)) return "transformed curve pole is not finite";
        }
        return {};
    };
    return RunCommand(Document, Verb + " (" + ListCaptions(Document, Unique) + ")", false, [&]() -> std::string
    {
        if (Unique.empty()) return "pick the sketches to " + Verb;
        int Dropped = 0;
        for (const std::string& Label : Unique)
        {
            const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Label; });
            if (Found == Document.Tree.Steps.end() || (Found->Kind != FeatureStepKind::Sketch && Found->Kind != FeatureStepKind::SpaceCurve))
                return "no sketch step labelled " + Label;
            const std::string Refusal = Found->Kind == FeatureStepKind::SpaceCurve ? CarrySpaceCurve(*Found, Map) : CarrySketchStep(Document, *Found, Map, Dropped);
            if (!Refusal.empty()) return Refusal;
        }
        CarryDimensionsWithAffine(Document, Unique, Map, true);
        return {};
    },0);
}

SketcherCommandOutcome DuplicateSketcherSketches(SketcherDocument& Document, const std::vector<std::string>& Labels, const AffineMap& Map)
{
    const std::vector<std::string> Unique = UniqueKeys(Labels);
    std::string Operands;
    for (const std::string& Label : Unique)
    {
        const auto Caption = Document.Captions.find(Label);
        Operands += (Operands.empty() ? "" : ", ") + (Caption != Document.Captions.end() ? Caption->second : Label);
    }
    return RunCommand(Document, "Duplicate (" + Operands + ")", false, [&]() -> std::string
    {
        if (Unique.empty()) return "pick the sketches to duplicate";
        std::vector<std::string> Copies;
        int Dropped = 0;
        for (const std::string& Label : Unique)
        {
            const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Label; });
            if (Found == Document.Tree.Steps.end() || (Found->Kind != FeatureStepKind::Sketch && Found->Kind != FeatureStepKind::SpaceCurve))
                return "no sketch step labelled " + Label;
            FeatureStep Copy = *Found;
            if (Found->Kind == FeatureStepKind::SpaceCurve)
            {
                const double Det = Dot(Cross(Map.ColumnX, Map.ColumnY), Map.ColumnZ);
                if (!std::isfinite(Det) || std::abs(Det) <= 1.0e-12) return "a scale of 0 flattens the curve";
                Copy.Label = "Curve#" + std::to_string(NextStepOrdinal(Document.Tree));
                const auto Apply = [&](Vector3 Point) { return Map.ColumnX * Point.X + Map.ColumnY * Point.Y + Map.ColumnZ * Point.Z + Map.Shift; };
                for (Vector3& Point : Copy.Points) Point = Apply(Point);
                for (FeaturePoleEdit& Edit : Copy.PoleEdits) Edit.Position = Apply(Edit.Position);
            }
            else
            {
                Copy.Label  = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
                Copy.Sketch = SolvedSketch(Document, *Found);                                // As shown (solved positions); the copy's label is new
                if (const std::string Refusal = CarrySketchStep(Document, Copy, Map, Dropped); !Refusal.empty()) return Refusal;
            }
            const auto  Caption = Document.Captions.find(Label);
            std::string Stem    = Caption != Document.Captions.end() ? Caption->second : (Found->Kind == FeatureStepKind::SpaceCurve ? std::string("Curve") : std::string("Sketch"));
            while (!Stem.empty() && std::isdigit(static_cast<unsigned char>(Stem.back()))) Stem.pop_back();
            if (Stem.empty()) Stem = Found->Kind == FeatureStepKind::SpaceCurve ? "Curve" : "Sketch";
            Document.Captions[Copy.Label] = NextCaption(Document, Stem);
            Copies.push_back(Copy.Label);
            Document.Tree.Steps.push_back(std::move(Copy));
        }
        Document.Selection        = {};
        Document.Selection.Curves = Copies;
        return {};
    });
}

SketcherJointOrigin ResolveSketcherJointOrigin(const SketcherDocument& Document, SketcherSelectionKind Kind, const std::string& Name)
{
    SketcherJointOrigin Origin;
    if (!Document.Partition) { Origin.Refusal = "there is no body"; return Origin; }
    if (Kind != SketcherSelectionKind::Edge && Kind != SketcherSelectionKind::Face) { Origin.Refusal = "a joint origin is an edge or a face"; return Origin; }
    for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
    {
        const std::map<uint32_t, std::string>& Names = Kind == SketcherSelectionKind::Edge ? Body.Naming.Edges : Body.Naming.Faces;
        for (const auto& [Slot, Candidate] : Names)
        {
            if (Candidate != Name) continue;
            const JointConnector Connector = ResolveJointConnector(*Document.Partition, Kind == SketcherSelectionKind::Edge ? ReferenceKind::Edge : ReferenceKind::Face, Slot);
            Origin.Body        = Body.Key;
            Origin.Frame       = Connector.Frame;
            Origin.Description = Connector.Description;
            Origin.Refusal     = Connector.Refusal;
            return Origin;
        }
    }
    Origin.Refusal = "no edge or face named " + Name;
    return Origin;
}

SketcherFaceFoot ProjectOntoSketcherFace(const SketcherDocument& Document, const std::string& Name, Vector3 Position)
{
    SketcherFaceFoot Foot;
    if (!Document.Partition) return Foot;
    for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
        for (const auto& [Slot, Candidate] : Body.Naming.Faces)
        {
            if (Candidate != Name) continue;
            const ModelPartition& Partition = *Document.Partition;
            const FaceToken       Face      = Partition.Faces.TokenAt(Slot);
            if (!Partition.Faces.Authenticate(Face)) return Foot;
            const FaceProfile&     Profile   = Partition.Faces.Resolve(Face);
            const SurfaceGeometry& Surface   = Partition.Surfaces.Resolve(Profile.Surface);
            const Vector2          Parameter = ProjectOntoSurface(Surface, Position, Vector2{});
            const Vector3          Point     = EvaluateSurface(Surface, Parameter).Position;
            const Vector3          Natural   = EvaluateSurfaceNormal(Surface, Parameter);
            if (!std::isfinite(Point.X + Point.Y + Point.Z) || !(Length(Natural) > 0.5)) return Foot;
            Foot.FoundCondition = true;
            Foot.Position       = Point;
            Foot.Normal         = Profile.Sense == Orientation::Forward ? Natural : Natural * -1.0;
            return Foot;
        }
    return Foot;
}

SketcherCommandOutcome JointSketcherBodies(SketcherDocument& Document, const SketcherJointRequest& Request)
{
    const SketcherJointOrigin Moving = ResolveSketcherJointOrigin(Document, Request.MovingKind, Request.MovingName);
    const SketcherJointOrigin Fixed  = ResolveSketcherJointOrigin(Document, Request.FixedKind, Request.FixedName);
    const std::string Label = std::string(JointKindLabel(Request.Kind)) + " joint (" + (Moving.Body.empty() ? std::string("?") : SketcherBodyCaption(Document, Moving.Body)) + " → "
                            + (Fixed.Body.empty() ? std::string("?") : SketcherBodyCaption(Document, Fixed.Body)) + ")";
    return RunCommand(Document, Label, true, [&]() -> std::string
    {
        if (!Moving.Refusal.empty()) return "moving origin: " + Moving.Refusal;
        if (!Fixed.Refusal.empty()) return "fixed origin: " + Fixed.Refusal;
        if (Moving.Body == Fixed.Body) return "both joint origins are on " + SketcherBodyCaption(Document, Moving.Body) + " (pick one on each body)";
        const std::string Caption   = NextCaption(Document, JointKindLabel(Request.Kind));
        const std::string StepLabel = "Joint#" + std::to_string(NextStepOrdinal(Document.Tree));
        const double      Degree    = std::numbers::pi / 180.0;
        const auto Bind = [&](const char* Field, double Value, double Low, double High, const char* Meaning, double Factor)
        {
            Document.Tree.Parameters.push_back({ Caption + "." + Field, Value, std::min(Low, Value), std::max(High, Value), Meaning });
            return ParameterValue(Caption + "." + Field, Factor);
        };
        const JointKind Kind   = Request.Kind;
        const bool      Slides = Kind == JointKind::Slider || Kind == JointKind::Cylindrical || Kind == JointKind::PinSlot || Kind == JointKind::Planar;
        const double    Span   = std::max({ 1.0, 4.0 * std::abs(Request.Slide), 4.0 * std::abs(Request.SecondSlide), 4.0 * std::abs(Request.Offset) });
        FeatureStep Step;
        Step.Kind   = FeatureStepKind::Joint;
        Step.Label  = StepLabel;
        Step.Target = Moving.Body;
        Step.Tool   = Fixed.Body;
        Step.References.push_back({ Request.MovingKind == SketcherSelectionKind::Edge ? ReferenceKind::Edge : ReferenceKind::Face, Request.MovingName, {} });
        Step.References.push_back({ Request.FixedKind == SketcherSelectionKind::Edge ? ReferenceKind::Edge : ReferenceKind::Face, Request.FixedName, {} });
        FeatureJoint& Joint = Step.Joint;
        Joint.Kind             = Kind;
        Joint.FlippedCondition = Request.Flipped;
        Joint.Angle            = Bind("Angle", Request.Angle, -180.0, 180.0, Kind == JointKind::Ball ? "yaw about the joint z [deg]" : "turn about the joint z [deg]", Degree);
        Joint.Offset           = Bind("Offset", Request.Offset, -Span, Span, "offset along the joint z", 1.0);
        if (Slides) Joint.Slide = Bind("Slide", Request.Slide, -Span, Span, Kind == JointKind::Slider || Kind == JointKind::Cylindrical ? "slide along the joint z" : "slide along the joint x", 1.0);
        if (Kind == JointKind::Planar) Joint.SecondSlide = Bind("SecondSlide", Request.SecondSlide, -Span, Span, "slide along the joint y", 1.0);
        if (Kind == JointKind::Ball)
        {
            Joint.Pitch = Bind("Pitch", Request.Pitch, -180.0, 180.0, "pitch about the joint y [deg]", Degree);
            Joint.Roll  = Bind("Roll", Request.Roll, -180.0, 180.0, "roll about the joint x [deg]", Degree);
        }
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Captions[StepLabel] = Caption;
        return {};
    });
}

SketcherCommandOutcome DuplicateSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys, const AffineMap& Map, const std::string& Source)
{
    const std::vector<std::string> Unique = UniqueKeys(Keys);
    return RunCommand(Document, (Source.empty() ? "Duplicate (" : "Paste (") + ListCaptions(Document, Unique) + ")", false, [&]() -> std::string
    {
        if (Unique.empty()) return Source.empty() ? "pick the bodies to duplicate (mode 4)" : "the clipboard is empty (Ctrl+C copies the picked bodies)";
        if (!Source.empty() && !FindStep(Document, Source)) return "the copied state is gone (step " + Source + " was deleted)";
        if (Source.empty()) for (const std::string& Key : Unique) if (!FindRegeneratedBody(Document, Key)) return "no body named " + Key;
        std::vector<std::string> Copies;
        for (const std::string& Key : Unique)
        {
            FeatureStep Step;
            Step.Kind      = FeatureStepKind::Duplicate;
            Step.Label     = "Duplicate#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Target    = Key;
            Step.Placement = Map;
            Step.Source    = Source;
            // "Block1" → "Block1Copy1"; a copy of a copy stays in the family ("Block1Copy1" → "Block1Copy2").
            std::string Stem = SketcherBodyCaption(Document, Key);
            const size_t Digits = Stem.find_last_not_of("0123456789");
            if (Digits != std::string::npos && Digits + 1 < Stem.size() && Digits >= 3 && Stem.compare(Digits - 3, 4, "Copy") == 0) Stem.erase(Digits - 3);
            Document.Captions[Step.Label] = NextCaption(Document, Stem + "Copy");
            Copies.push_back(Step.Label);
            Document.Tree.Steps.push_back(std::move(Step));
        }
        Document.Selection          = {};
        Document.Selection.Bodies   = Copies;
        Document.Selection.LastKind = SketcherSelectionKind::Body;
        return {};
    });
}

// 📝 A sketch cutter's extrude: the kernel moves the profile plane to Top along the normal and sweeps Bottom − Top, so a
//    Start above the plane is fine there (only the authored E refuses one: its start sinks into a tangent body).
SketcherCommandOutcome KnifeSketcherSketch(SketcherDocument& Document, const std::string& SketchLabel, double Top, double Bottom, FeatureCombination Combination,
                                           const std::string& Target)
{
    const std::string Caption = NextCaption(Document, "Knife");
    const auto        Source  = Document.Captions.find(SketchLabel);
    const std::string Label   = Caption + " · " + (Source != Document.Captions.end() ? Source->second : SketchLabel) + " · " + FeatureCombinationLabel(Combination) +
                                " · depth " + FormatAmount(-Bottom);
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == SketchLabel; });
        if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::Sketch) return "no sketch step labelled " + SketchLabel;
        if (!(Top >= 0.0) || !(Bottom < -1.0e-9) || !(Top - Bottom > 1.0e-9)) return "the knife depth must be positive";
        if (Combination == FeatureCombination::New) return "choose Difference, Union, Intersection or Separate";
        if (Combination != FeatureCombination::Separate)
        {
            if (!Document.Summary.PresentCondition) return "nothing to cut yet (no body)";
            if (Target.empty() || !FindRegeneratedBody(Document, Target)) return Target.empty() ? std::string("no body under the knife") : "no body named " + Target;
        }
        Found->FillCondition = true;                                                         // A cutter is solid
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Extrude;
        Step.Label       = "Extrude#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Profile     = SketchLabel;
        if (Top > 0.0) Step.Start = FeatureValue{ Top };
        Step.Extent      = FeatureValue{ Bottom };
        Step.Combination = Combination;
        Step.Target      = Combination == FeatureCombination::Separate ? std::string{} : Target;
        Document.Captions[Step.Label] = Caption;
        const std::string Made = Step.Label;
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Selection = {};
        if (Combination == FeatureCombination::Separate)
        {
            Document.Selection.Bodies   = { Made };
            Document.Selection.LastKind = SketcherSelectionKind::Body;
        }
        return {};
    });
}

SketcherCommandOutcome ImprintSketcherSketch(SketcherDocument& Document, const std::string& SketchLabel, const std::string& Target)
{
    const std::string Caption = NextCaption(Document, "Imprint");
    const auto        Source  = Document.Captions.find(SketchLabel);
    const std::string Key     = Target.empty() ? Document.Regeneration.Active : Target;
    const std::string Label   = Caption + " · " + (Source != Document.Captions.end() ? Source->second : SketchLabel) + " on " + SketcherBodyCaption(Document, Key);
    // The tool stands on the side away from the material (a closed outline on a face); open chains and undecided outlines
    // take the sketch normal (orient-to-face sketches: outward). Either side splits the same faces.
    const int Material = MeasureSketcherProfileMaterial(Document, SketchLabel, Key);
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == SketchLabel; });
        if (Found == Document.Tree.Steps.end() || Found->Kind != FeatureStepKind::Sketch) return "no sketch step labelled " + SketchLabel;
        if (!Document.Summary.PresentCondition) return "nothing to imprint on yet (no body)";
        if (Key.empty() || !FindRegeneratedBody(Document, Key)) return Key.empty() ? std::string("no body under the outline") : "no body named " + Key;
        FeatureStep Step;
        Step.Kind    = FeatureStepKind::Imprint;
        Step.Label   = "Imprint#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Profile = SketchLabel;
        Step.Extent  = FeatureValue{ Material > 0 ? -1.0 : 1.0 };
        Step.Target  = Key;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Selection = {};
        return {};
    });
}

SketcherCommandOutcome KnifeSketcherInsert(SketcherDocument& Document, const SketcherInsertPlacement& Placement)
{
    const std::string Caption = NextCaption(Document, "Knife");
    const std::string Label   = Caption + " · " + Placement.Name + " · " + FeatureCombinationLabel(Placement.Combination);
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (Placement.Exchange.empty()) return "the insert " + Placement.Name + " holds no solid";
        const AffineMap& Map = Placement.Map;
        const double Volume = Dot(Map.ColumnX, Cross(Map.ColumnY, Map.ColumnZ));
        if (!(Volume > 1.0e-15)) return "the insert placement is degenerate (zero size or mirrored)";
        if (Placement.Combination != FeatureCombination::Separate)
        {
            if (Placement.Combination != FeatureCombination::Union && Placement.Combination != FeatureCombination::Difference &&
                Placement.Combination != FeatureCombination::Intersection)
                return "choose Difference, Union, Intersection or Separate";
            if (Placement.Target.empty() || !FindRegeneratedBody(Document, Placement.Target))
                return Placement.Target.empty() ? std::string("place the insert on a body's face") : "no body named " + Placement.Target;
        }
        FeatureStep Import;
        Import.Kind         = FeatureStepKind::Import;
        Import.Label        = "Import#" + std::to_string(NextStepOrdinal(Document.Tree));
        Import.Exchange     = Placement.Exchange;
        Import.ExchangeUnit = SketcherModelUnitMillimetres;
        const std::string Insert = Import.Label;
        Document.Captions[Insert] = NextCaption(Document, Placement.Name.empty() ? std::string("Insert") : Placement.Name);
        Document.Tree.Steps.push_back(std::move(Import));
        FeatureStep Place;
        Place.Kind      = FeatureStepKind::Transform;
        Place.Label     = "Transform#" + std::to_string(NextStepOrdinal(Document.Tree));
        Place.Target    = Insert;
        Place.Placement = Map;
        Document.Captions[Place.Label] = NextCaption(Document, "Place");
        Document.Tree.Steps.push_back(std::move(Place));
        Document.Selection = {};
        if (Placement.Combination == FeatureCombination::Separate)
        {
            Document.Selection.Bodies   = { Insert };
            Document.Selection.LastKind = SketcherSelectionKind::Body;
            return {};
        }
        FeatureStep Combine;
        Combine.Kind        = FeatureStepKind::Combine;
        Combine.Label       = "Combine#" + std::to_string(NextStepOrdinal(Document.Tree));
        Combine.Combination = Placement.Combination;
        Combine.Target      = Placement.Target;
        Combine.Tool        = Insert;
        Document.Captions[Combine.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Combine));
        return {};
    });
}

SketcherCommandOutcome KnifeSketcherBodies(SketcherDocument& Document, const std::string& Target, const std::vector<std::string>& Tools, FeatureCombination Combination,
                                           bool KeepCondition)
{
    const std::string Cut = Target;                                                          // Target may live in the selection this command clears
    const std::string Caption = NextCaption(Document, "Knife");
    std::string Operands = SketcherBodyCaption(Document, Cut);
    const char* Sign = Combination == FeatureCombination::Difference ? " − " : Combination == FeatureCombination::Intersection ? " ∩ " : " ∪ ";
    for (const std::string& Tool : Tools) Operands += Sign + SketcherBodyCaption(Document, Tool);
    const std::string Label = Caption + " · " + FeatureCombinationLabel(Combination) + " (" + Operands + ")" + (KeepCondition ? " · cutters kept" : "");
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (Combination != FeatureCombination::Union && Combination != FeatureCombination::Difference && Combination != FeatureCombination::Intersection)
            return "choose Difference, Union or Intersection";
        if (Tools.empty()) return "select the target body, then the cutter bodies";
        if (!FindRegeneratedBody(Document, Cut)) return "no body named " + Cut;
        std::set<std::string> Seen{ Cut };
        for (const std::string& Tool : Tools)
        {
            if (!FindRegeneratedBody(Document, Tool)) return "no body named " + Tool;
            if (!Seen.insert(Tool).second) return SketcherBodyCaption(Document, Tool) + " is picked twice (the target cannot be a cutter)";
        }
        const AffineMap Identity;                                                            // In place
        for (const std::string& Tool : Tools)
        {
            std::string Cutter = Tool;
            if (KeepCondition)
            {
                FeatureStep Copy;
                Copy.Kind      = FeatureStepKind::Duplicate;
                Copy.Label     = "Duplicate#" + std::to_string(NextStepOrdinal(Document.Tree));
                Copy.Target    = Tool;
                Copy.Placement = Identity;
                Document.Captions[Copy.Label] = NextCaption(Document, SketcherBodyCaption(Document, Tool) + "Cutter");
                Cutter = Copy.Label;
                Document.Tree.Steps.push_back(std::move(Copy));
            }
            FeatureStep Step;
            Step.Kind        = FeatureStepKind::Combine;
            Step.Label       = "Combine#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Combination = Combination;
            Step.Target      = Cut;
            Step.Tool        = Cutter;
            Document.Captions[Step.Label] = Caption;
            Document.Tree.Steps.push_back(std::move(Step));
        }
        Document.Selection          = {};
        Document.Selection.Bodies   = { Cut };
        Document.Selection.LastKind = SketcherSelectionKind::Body;
        return {};
    });
}

SketcherCommandOutcome KnifeSketcherPlacedBody(SketcherDocument& Document, const std::string& Target, const std::string& Tool, const AffineMap& Map,
                                               FeatureCombination Combination, bool KeepCondition)
{
    const std::string Cut     = Target;                                                      // Both may live in the selection this command clears
    const std::string Moved   = Tool;
    const std::string Caption = NextCaption(Document, "Knife");
    const char*       Sign    = Combination == FeatureCombination::Difference ? " − " : Combination == FeatureCombination::Intersection ? " ∩ " : " ∪ ";
    const std::string Label   = Caption + " · " + FeatureCombinationLabel(Combination) + " (" + SketcherBodyCaption(Document, Cut) + Sign + SketcherBodyCaption(Document, Moved) +
                                " placed)" + (KeepCondition ? " · cutter kept" : "");
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (Combination != FeatureCombination::Union && Combination != FeatureCombination::Difference && Combination != FeatureCombination::Intersection)
            return "choose Difference, Union or Intersection";
        if (Cut.empty()) return "point at a face of the body to cut";
        if (!FindRegeneratedBody(Document, Cut)) return "no body named " + Cut;
        if (!FindRegeneratedBody(Document, Moved)) return "no body named " + Moved;
        if (Cut == Moved) return "the cutter cannot cut itself: point at another body";
        const double Volume = Dot(Map.ColumnX, Cross(Map.ColumnY, Map.ColumnZ));
        if (!(Volume > 1.0e-15)) return "the cutter placement is degenerate (zero size or mirrored)";
        FeatureStep Place;
        Place.Kind      = KeepCondition ? FeatureStepKind::Duplicate : FeatureStepKind::Transform;
        Place.Label     = (KeepCondition ? "Duplicate#" : "Transform#") + std::to_string(NextStepOrdinal(Document.Tree));
        Place.Target    = Moved;
        Place.Placement = Map;
        const std::string Cutter = KeepCondition ? Place.Label : Moved;
        Document.Captions[Place.Label] = NextCaption(Document, KeepCondition ? SketcherBodyCaption(Document, Moved) + "Cutter" : std::string("Place"));
        Document.Tree.Steps.push_back(std::move(Place));
        FeatureStep Combine;
        Combine.Kind        = FeatureStepKind::Combine;
        Combine.Label       = "Combine#" + std::to_string(NextStepOrdinal(Document.Tree));
        Combine.Combination = Combination;
        Combine.Target      = Cut;
        Combine.Tool        = Cutter;
        Document.Captions[Combine.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Combine));
        Document.Selection = {};
        return {};
    });
}

SketcherCommandOutcome KnifeSketcherPrimitive(SketcherDocument& Document, const SketcherConstructRequest& Request, const std::string& Target,
                                              FeatureCombination Combination, bool KeepCondition)
{
    const std::string Cut     = Target;
    const std::string Caption = NextCaption(Document, "Knife");
    const std::string Label   = Caption + " · " + SketcherConstructLabel(Request.Kind) + " · " + FeatureCombinationLabel(Combination) +
                                (Combination != FeatureCombination::Separate && !Cut.empty() ? " (" + SketcherBodyCaption(Document, Cut) + ")" : std::string()) +
                                (KeepCondition && Combination != FeatureCombination::Separate ? " · cutter kept" : "");
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (Combination != FeatureCombination::Union && Combination != FeatureCombination::Difference && Combination != FeatureCombination::Intersection &&
            Combination != FeatureCombination::Separate)
            return "choose Difference, Union, Intersection or Separate";
        if (Combination != FeatureCombination::Separate)
        {
            if (!Document.Summary.PresentCondition) return "nothing to cut yet (no body)";
            if (Cut.empty()) return "no body under the knife: draw the cutter on a face of the body to cut";
            if (!FindRegeneratedBody(Document, Cut)) return "no body named " + Cut;
        }
        // The same checks as a Construct tile (a cone's Height may be negative: it points down).
        const SketcherConstructKind Tile = Request.Kind;
        const bool BoxLike = Tile == SketcherConstructKind::Block || Tile == SketcherConstructKind::Wedge;
        const bool Round   = Tile == SketcherConstructKind::Cylinder || Tile == SketcherConstructKind::Sphere || Tile == SketcherConstructKind::Cone ||
                             Tile == SketcherConstructKind::Prism || Tile == SketcherConstructKind::Tube;
        const bool Tall    = Tile != SketcherConstructKind::Sphere && Tile != SketcherConstructKind::Torus;
        if ((BoxLike && (Request.Width <= 0.0 || Request.Depth <= 0.0)) || (Round && Request.Radius <= 0.0) ||
            (Tall && (Tile == SketcherConstructKind::Cone ? Request.Height == 0.0 : Request.Height <= 0.0)))
            return "every dimension must be positive";
        if (Tile == SketcherConstructKind::Torus && (Request.Minor <= 0.0 || Request.Major <= Request.Minor)) return "torus: Major must exceed Minor > 0";
        if (Tile == SketcherConstructKind::Tube && (Request.Inner <= 0.0 || Request.Inner >= Request.Radius)) return "tube: the bore must lie inside the outer radius";
        if (Tile == SketcherConstructKind::Prism && (Request.Sides < 3 || Request.Sides > 64)) return "prism: 3 … 64 sides";
        if (Request.PlaneCondition)
        {
            const OrthonormalFrame& Face = Request.Plane;
            const bool Finite = std::isfinite(Face.Origin.X) && std::isfinite(Face.Origin.Y) && std::isfinite(Face.Origin.Z);
            const bool Square = std::abs(Length(Face.XAxis) - 1.0) < 1.0e-9 && std::abs(Length(Face.YAxis) - 1.0) < 1.0e-9 && std::abs(Length(Face.ZAxis) - 1.0) < 1.0e-9
                             && std::abs(Dot(Face.XAxis, Face.YAxis)) < 1.0e-9 && Length(Cross(Face.XAxis, Face.YAxis) - Face.ZAxis) < 1.0e-9;
            if (!Finite || !Square) return "the face plane is not an orthonormal right-handed frame";
        }
        SketcherConstructRequest Cutter = Request;
        const bool Direct = Combination != FeatureCombination::Separate && !KeepCondition;
        Cutter.Target      = Direct ? Cut : std::string{};
        Cutter.Combination = Direct ? Combination : FeatureCombination::Separate;
        AuthorTile(Document, Cutter, Cutter.Combination);
        const std::string Made = Document.Tree.Steps.back().Label;                           // The sweep: the new body when Separate
        if (Direct) Document.Captions[Made] = Caption;                                       // The sweep is the cut itself
        Document.Selection = {};
        if (Combination == FeatureCombination::Separate)
        {
            Document.Selection.Bodies   = { Made };
            Document.Selection.LastKind = SketcherSelectionKind::Body;
            return {};
        }
        if (Direct) return {};
        FeatureStep Copy;
        Copy.Kind      = FeatureStepKind::Duplicate;
        Copy.Label     = "Duplicate#" + std::to_string(NextStepOrdinal(Document.Tree));
        Copy.Target    = Made;
        Copy.Placement = AffineMap{};
        const std::string Tool = Copy.Label;
        Document.Captions[Tool] = NextCaption(Document, Document.Captions[Made] + "Cutter");
        Document.Tree.Steps.push_back(std::move(Copy));
        FeatureStep Combine;
        Combine.Kind        = FeatureStepKind::Combine;
        Combine.Label       = "Combine#" + std::to_string(NextStepOrdinal(Document.Tree));
        Combine.Combination = Combination;
        Combine.Target      = Cut;
        Combine.Tool        = Tool;
        Document.Captions[Combine.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Combine));
        return {};
    });
}

SketcherCommandOutcome ChangeSketcherVisibility(SketcherDocument& Document, SketcherVisibilityVerb Verb)
{
    // The picked bodies: body picks plus the owners of picked faces and edges; the picked curves: sketches and 3D curves
    // (H hides them too — the Outliner eye's set).
    std::set<std::string> Picked(Document.Selection.Bodies.begin(), Document.Selection.Bodies.end());
    for (const std::string& Face : Document.Selection.Faces) if (const std::string Key = SketcherBodyOfName(Document, SketcherSelectionKind::Face, Face); !Key.empty()) Picked.insert(Key);
    for (const std::string& Edge : Document.Selection.Edges) if (const std::string Key = SketcherBodyOfName(Document, SketcherSelectionKind::Edge, Edge); !Key.empty()) Picked.insert(Key);
    const std::set<std::string> PickedCurves(Document.Selection.Curves.begin(), Document.Selection.Curves.end());
    static const char* const Labels[] = { "Hide selected", "Hide unselected", "Unhide all", "Invert hidden" };
    return RunAppearanceCommand(Document, Labels[static_cast<int>(Verb)], [&]() -> std::string
    {
        std::set<std::string>& Hidden = Document.Appearance.HiddenBodies;
        std::set<std::string>& Curves = Document.Appearance.HiddenCurves;
        std::set<std::string>  Present, Drawn;
        for (const RegeneratedBody& Body : Document.Regeneration.Bodies) Present.insert(Body.Key);
        // 📝 Curves H may show: sketches and 3D curves that are not deleted and not read by a later step (an extruded
        //    sketch stays with its body; its eye in the Outliner still works).
        for (const FeatureStep& Step : Document.Tree.Steps)
            if ((Step.Kind == FeatureStepKind::Sketch || Step.Kind == FeatureStepKind::SpaceCurve) && !Document.Appearance.RemovedCurves.contains(Step.Label)
                && (Step.Kind == FeatureStepKind::SpaceCurve || !SketcherSketchConsumedCondition(Document, Step.Label) || Curves.contains(Step.Label) || PickedCurves.contains(Step.Label)))
                Drawn.insert(Step.Label);
        std::erase_if(Hidden, [&](const std::string& Key) { return !Present.contains(Key); });       // Keys of removed bodies
        const std::set<std::string> Before = Hidden, CurvesBefore = Curves;
        switch (Verb)
        {
        case SketcherVisibilityVerb::HideSelected:
            if (Picked.empty() && PickedCurves.empty()) return "pick the bodies, sketches or curves to hide";
            Hidden.insert(Picked.begin(), Picked.end());
            Curves.insert(PickedCurves.begin(), PickedCurves.end());
            break;
        case SketcherVisibilityVerb::HideUnselected:
            if (Picked.empty() && PickedCurves.empty()) return "pick the bodies, sketches or curves to keep in view";
            for (const std::string& Key : Present) if (!Picked.contains(Key)) Hidden.insert(Key);
            for (const std::string& Label : Drawn) if (!PickedCurves.contains(Label)) Curves.insert(Label);
            break;
        case SketcherVisibilityVerb::UnhideAll:
            Hidden.clear();
            std::erase_if(Curves, [&](const std::string& Label) { return Drawn.contains(Label); });
            break;
        case SketcherVisibilityVerb::InvertHidden:
        {
            std::set<std::string> Swapped;
            for (const std::string& Key : Present) if (!Before.contains(Key)) Swapped.insert(Key);
            Hidden = std::move(Swapped);
            for (const std::string& Label : Drawn) { if (CurvesBefore.contains(Label)) Curves.erase(Label); else Curves.insert(Label); }
            break;
        }
        }
        if (Hidden == Before && Curves == CurvesBefore) return Verb == SketcherVisibilityVerb::UnhideAll ? "nothing is hidden" : "nothing changes";
        // Hidden bodies and curves leave the selection.
        std::erase_if(Document.Selection.Bodies, [&](const std::string& Key) { return Hidden.contains(Key); });
        std::erase_if(Document.Selection.Faces, [&](const std::string& Name) { return Hidden.contains(SketcherBodyOfName(Document, SketcherSelectionKind::Face, Name)); });
        std::erase_if(Document.Selection.Edges, [&](const std::string& Name) { return Hidden.contains(SketcherBodyOfName(Document, SketcherSelectionKind::Edge, Name)); });
        std::erase_if(Document.Selection.Vertices, [&](const std::string& Name) { return Hidden.contains(SketcherBodyOfName(Document, SketcherSelectionKind::Vertex, Name)); });
        std::erase_if(Document.Selection.Curves, [&](const std::string& Label) { return Curves.contains(Label); });
        return {};
    });
}

SketcherCommandOutcome ToggleSketcherBodyVisibility(SketcherDocument& Document, const std::string& Key)
{
    const bool HiddenCondition = Document.Appearance.HiddenBodies.contains(Key);
    return RunAppearanceCommand(Document, HiddenCondition ? "Show body" : "Hide body", [&]() -> std::string
    {
        const bool Present = std::any_of(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(),
                                         [&](const RegeneratedBody& Body) { return Body.Key == Key; });
        if (!Present) return "no body " + Key;
        std::set<std::string>& Hidden = Document.Appearance.HiddenBodies;
        if (HiddenCondition)
        {
            Hidden.erase(Key);
            return {};
        }
        Hidden.insert(Key);
        // A hidden body leaves the selection.
        std::erase(Document.Selection.Bodies, Key);
        std::erase_if(Document.Selection.Faces, [&](const std::string& Name) { return SketcherBodyOfName(Document, SketcherSelectionKind::Face, Name) == Key; });
        std::erase_if(Document.Selection.Edges, [&](const std::string& Name) { return SketcherBodyOfName(Document, SketcherSelectionKind::Edge, Name) == Key; });
        std::erase_if(Document.Selection.Vertices, [&](const std::string& Name) { return SketcherBodyOfName(Document, SketcherSelectionKind::Vertex, Name) == Key; });
        return {};
    });
}

bool SketcherCurveHiddenCondition(const SketcherDocument& Document, const std::string& Label) noexcept
{
    return Document.Appearance.HiddenCurves.contains(Label);
}

bool SketcherLockedCondition(const SketcherDocument& Document, const std::string& Key) noexcept
{
    return Document.Appearance.LockedFigures.contains(Key);
}

SketcherCommandOutcome ToggleSketcherCurveVisibility(SketcherDocument& Document, const std::string& Label)
{
    const bool HiddenCondition = Document.Appearance.HiddenCurves.contains(Label);
    return RunAppearanceCommand(Document, HiddenCondition ? "Show curve" : "Hide curve", [&]() -> std::string
    {
        const bool Present = std::any_of(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step)
                                         { return Step.Label == Label && (Step.Kind == FeatureStepKind::Sketch || Step.Kind == FeatureStepKind::SpaceCurve); });
        if (!Present) return "no sketch or curve " + Label;
        if (Document.Appearance.RemovedCurves.contains(Label)) return Label + " is deleted (a later step still reads it): undo brings it back";
        std::set<std::string>& Hidden = Document.Appearance.HiddenCurves;
        if (HiddenCondition)
        {
            Hidden.erase(Label);
            return {};
        }
        Hidden.insert(Label);
        std::erase(Document.Selection.Curves, Label);                                       // A hidden curve leaves the selection
        return {};
    });
}

SketcherCommandOutcome ToggleSketcherLock(SketcherDocument& Document, const std::string& Key)
{
    const bool LockedCondition = Document.Appearance.LockedFigures.contains(Key);
    return RunAppearanceCommand(Document, LockedCondition ? "Unlock" : "Lock", [&]() -> std::string
    {
        const bool Body  = std::any_of(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(), [&](const RegeneratedBody& Entry) { return Entry.Key == Key; });
        const bool Curve = std::any_of(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step)
                                       { return Step.Label == Key && (Step.Kind == FeatureStepKind::Sketch || Step.Kind == FeatureStepKind::SpaceCurve); });
        if (!Body && !Curve) return "no body, sketch or curve " + Key;
        std::set<std::string>& Locked = Document.Appearance.LockedFigures;
        if (LockedCondition)
        {
            Locked.erase(Key);
            return {};
        }
        Locked.insert(Key);
        // A locked figure leaves the selection (it cannot be edited until unlocked).
        std::erase(Document.Selection.Curves, Key);
        std::erase(Document.Selection.Bodies, Key);
        std::erase_if(Document.Selection.Faces, [&](const std::string& Name) { return SketcherBodyOfName(Document, SketcherSelectionKind::Face, Name) == Key; });
        std::erase_if(Document.Selection.Edges, [&](const std::string& Name) { return SketcherBodyOfName(Document, SketcherSelectionKind::Edge, Name) == Key; });
        std::erase_if(Document.Selection.Vertices, [&](const std::string& Name) { return SketcherBodyOfName(Document, SketcherSelectionKind::Vertex, Name) == Key; });
        return {};
    });
}

SketcherCommandOutcome JoinSketcherSketches(SketcherDocument& Document, const std::vector<std::string>& Sketches)
{
    std::vector<std::string> Captions;
    for (const std::string& Label : Sketches) Captions.push_back(Label);
    return RunCommand(Document, "Join (" + ListCaptions(Document, Captions) + ")", false, [&]() -> std::string
    {
        if (Sketches.size() < 2) return "pick two or more sketches to join";
        const FeatureStep* First = FindStep(Document, Sketches.front());
        if (!First || First->Kind != FeatureStepKind::Sketch) return "no sketch step labelled " + Sketches.front();
        const OrthonormalFrame Base   = ElevatedPlane(Document.Tree, *First);
        SketchModel            Joined = SolvedSketch(Document, *First);
        for (size_t Index = 1; Index < Sketches.size(); ++Index)
        {
            const FeatureStep* Other = FindStep(Document, Sketches[Index]);
            if (!Other || Other->Kind != FeatureStepKind::Sketch) return "no sketch step labelled " + Sketches[Index];
            if (Sketches[Index] == Sketches.front()) return SketcherBodyCaption(Document, Sketches[Index]) + " is picked twice";
            const OrthonormalFrame Plane = ElevatedPlane(Document.Tree, *Other);
            const Vector3          Apart = Plane.Origin - Base.Origin;
            if (std::fabs(Dot(Plane.ZAxis, Base.ZAxis)) < 1.0 - 1.0e-9 || std::fabs(Dot(Apart, Base.ZAxis)) > 1.0e-9 * std::max(1.0, Length(Apart)))
                return SketcherBodyCaption(Document, Other->Label) + " is not in the plane of " + SketcherBodyCaption(Document, First->Label);
            SketchPlacement Placement;
            Placement.Origin = { Dot(Apart, Base.XAxis), Dot(Apart, Base.YAxis) };
            Placement.XAxis  = { Dot(Plane.XAxis, Base.XAxis), Dot(Plane.XAxis, Base.YAxis) };
            Placement.YAxis  = { Dot(Plane.YAxis, Base.XAxis), Dot(Plane.YAxis, Base.YAxis) };
            const double Join = 1.0e-9 * std::max({ 1.0, MeasureSketchScale(Joined), MeasureSketchScale(SolvedSketch(Document, *Other)) });
            const SketchJoinOutcome Outcome = JoinSketchModels(Joined, SolvedSketch(Document, *Other), Placement, Join);
            if (!Outcome.Refusal.empty()) return Outcome.Refusal;
            Joined = Outcome.Sketch;
        }
        // A sketch a later step reads stays (hidden) so that step keeps its input; the others merge away.
        std::set<std::string>& Hidden = Document.Appearance.HiddenCurves;
        const auto Keep = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Sketches.front(); });
        if (SketcherSketchReferencedCondition(Document, Sketches.front()))
        {
            std::string Stem = SketcherBodyCaption(Document, Sketches.front());
            while (!Stem.empty() && std::isdigit(static_cast<unsigned char>(Stem.back()))) Stem.pop_back();
            FeatureStep Copy = *Keep;
            Copy.Label  = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
            Copy.Sketch = std::move(Joined);
            Document.Captions[Copy.Label] = NextCaption(Document, Stem.empty() ? std::string("Sketch") : Stem);
            Hidden.insert(Sketches.front());
            Document.Tree.Steps.push_back(std::move(Copy));
        }
        else Keep->Sketch = std::move(Joined);
        for (size_t Index = 1; Index < Sketches.size(); ++Index)
        {
            if (SketcherSketchReferencedCondition(Document, Sketches[Index]))
            {
                Hidden.insert(Sketches[Index]);
                continue;
            }
            std::erase_if(Document.Tree.Steps, [&](const FeatureStep& Step) { return Step.Label == Sketches[Index]; });
            Document.Captions.erase(Sketches[Index]);
        }
        for (const std::string& Label : Sketches)
            if (Hidden.contains(Label) || !FindStep(Document, Label)) std::erase(Document.Selection.Curves, Label);
        return {};
    }, 0);
}

SketcherCommandOutcome FlowSketcherSketches(SketcherDocument& Document, const SketcherFlowRequest& Request)
{
    const std::string Caption = NextCaption(Document, "Flow");
    std::vector<std::string> Shapes;
    for (const std::string& Label : Request.Shapes) Shapes.push_back(Label);
    const char* Fit = Request.Settings.StretchEnabled ? "stretched" : "true length";
    return RunCommand(Document, "Flow " + ListCaptions(Document, Shapes) + " along " + SketcherBodyCaption(Document, Request.Rail) + " into " + Caption + " (" + Fit + ")", false,
                      [&]() -> std::string
    {
        const FeatureStep* Rail = FindStep(Document, Request.Rail);
        if (!Rail || Rail->Kind != FeatureStepKind::Sketch) return "Flow: pick the rail sketch first, then the shapes";
        if (Request.Shapes.empty()) return "Flow: pick the rail curve, then one or more shapes to bend along it";
        const FeatureStep* First = FindStep(Document, Request.Shapes.front());
        if (!First || First->Kind != FeatureStepKind::Sketch) return "Flow: no sketch step labelled " + Request.Shapes.front();
        // ① the shapes as one model in the first shape's plane (coplanar, as Join).
        const OrthonormalFrame Base  = ElevatedPlane(Document.Tree, *First);
        SketchModel            Shape = SolvedSketch(Document, *First);
        for (const std::string& Label : Request.Shapes)
        {
            if (Label == Request.Rail) return "Flow: " + SketcherBodyCaption(Document, Label) + " is the rail and a shape";
            if (Label == Request.Shapes.front()) continue;
            const FeatureStep* Other = FindStep(Document, Label);
            if (!Other || Other->Kind != FeatureStepKind::Sketch) return "Flow: no sketch step labelled " + Label;
            const OrthonormalFrame Plane = ElevatedPlane(Document.Tree, *Other);
            const Vector3          Apart = Plane.Origin - Base.Origin;
            if (std::fabs(Dot(Plane.ZAxis, Base.ZAxis)) < 1.0 - 1.0e-9 || std::fabs(Dot(Apart, Base.ZAxis)) > 1.0e-9 * std::max(1.0, Length(Apart)))
                return "Flow: " + SketcherBodyCaption(Document, Label) + " is not in the plane of " + SketcherBodyCaption(Document, First->Label);
            SketchPlacement Placement;
            Placement.Origin = { Dot(Apart, Base.XAxis), Dot(Apart, Base.YAxis) };
            Placement.XAxis  = { Dot(Plane.XAxis, Base.XAxis), Dot(Plane.XAxis, Base.YAxis) };
            Placement.YAxis  = { Dot(Plane.YAxis, Base.XAxis), Dot(Plane.YAxis, Base.YAxis) };
            const double Join = 1.0e-9 * std::max({ 1.0, MeasureSketchScale(Shape), MeasureSketchScale(SolvedSketch(Document, *Other)) });
            const SketchJoinOutcome Joined = JoinSketchModels(Shape, SolvedSketch(Document, *Other), Placement, Join);
            if (!Joined.Refusal.empty()) return "Flow: " + Joined.Refusal;
            Shape = Joined.Sketch;
        }
        // ② bend, then a new sketch step on the rail's plane.
        const SketchFlowOutcome Flowed = FlowSketchModel(Shape, SolvedSketch(Document, *Rail), Request.Settings);
        if (!Flowed.Refusal.empty()) return "Flow: " + Flowed.Refusal;
        FeatureStep Step   = *Rail;
        Step.Label         = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Sketch        = Flowed.Sketch;
        Step.FillCondition = First->FillCondition && ExtractSketchProfile(Flowed.Sketch).Refusal.empty();
        Document.Captions[Step.Label] = Caption;
        Document.Selection.Curves     = { Step.Label };
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

SketcherCommandOutcome WrapSketcherSketches(SketcherDocument& Document, const SketcherWrapRequest& Request)
{
    const char* Verb = Request.Settings.Kind == SketchWrapKind::Project ? "Project " : "Wrap ";
    return RunCommand(Document, Verb + ListCaptions(Document, Request.Shapes) + " round " + SketcherBodyCaption(Document, Request.Rail), false, [&]() -> std::string
    {
        const FeatureStep* Rail = FindStep(Document, Request.Rail);
        if (!Rail || Rail->Kind != FeatureStepKind::Sketch) return "Wrap: pick the shapes and the curve to wrap round (a circle: a can)";
        if (Request.Shapes.empty()) return "Wrap: pick the shapes (text, outlines) and the curve to wrap them round";
        std::vector<SketchWrapShape> Shapes;
        for (const std::string& Label : Request.Shapes)
        {
            if (Label == Request.Rail) return "Wrap: " + SketcherBodyCaption(Document, Label) + " is the rail and a shape";
            const FeatureStep* Shape = FindStep(Document, Label);
            if (!Shape || Shape->Kind != FeatureStepKind::Sketch) return "Wrap: no sketch step labelled " + Label;
            Shapes.push_back({ SolvedSketch(Document, *Shape), ElevatedPlane(Document.Tree, *Shape) });
        }
        const SketchWrapOutcome Wrapped = WrapSketchModels(Shapes, SolvedSketch(Document, *Rail), ElevatedPlane(Document.Tree, *Rail), Request.Settings);
        if (!Wrapped.Refusal.empty()) return "Wrap: " + Wrapped.Refusal;
        // 💡 Fit each wrapped curve as tight as the fitter's span limit allows: 10⁻⁶ of the shape's width (on the rail to
        //    ~1 µm a metre), else 10⁻⁵, else 10⁻⁴ — the samples themselves lie exactly on the rail's surface.
        const double Width = std::max(Wrapped.Extent, 1.0e-3);                                 // [len] - Shape width
        std::vector<std::string> Made;
        for (size_t Index = 0; Index < Wrapped.Curves.size(); ++Index)
        {
            std::vector<Vector3> Closing = Wrapped.Curves[Index];
            if (Wrapped.Closed[Index] && Closing.size() > 2 && Length(Closing.back() - Closing.front()) > 0.0) Closing.push_back(Closing.front());
            double Tolerance = 1.0e-4 * Width;                                                   // [len] - Fit tolerance chosen
            for (const double Fraction : { 1.0e-6, 1.0e-5 })
            {
                CurveFitSettings Trial;
                Trial.Tolerance = Fraction * Width;
                Trial.SpanLimit = std::max(Trial.SpanLimit, static_cast<int>(Closing.size()) / 4);   // As the regeneration fits it
                if (FitSpaceCurve(Closing, Trial).Refusal.empty()) { Tolerance = Trial.Tolerance; break; }
            }
            FeatureStep Step;
            Step.Kind            = FeatureStepKind::SpaceCurve;
            Step.Label           = "Curve#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Points          = Wrapped.Curves[Index];
            Step.ClosedCondition = Wrapped.Closed[Index];
            Step.PlanarCondition = false;
            Step.Tolerance       = FeatureValue{ Tolerance };
            Document.Captions[Step.Label] = NextCaption(Document, "Wrap");
            Made.push_back(Step.Label);
            Document.Tree.Steps.push_back(std::move(Step));
        }
        Document.Selection        = {};
        Document.Selection.Curves = Made;
        return {};
    });
}

std::vector<std::string> SketcherJoinCandidates(const SketcherDocument& Document, const std::string& Sketch)
{
    std::vector<std::string> Group;
    const FeatureStep* First = FindStep(Document, Sketch);
    if (!First || First->Kind != FeatureStepKind::Sketch) return Group;
    const OrthonormalFrame   Base = ElevatedPlane(Document.Tree, *First);
    std::vector<SketchModel> Members{ SolvedSketch(Document, *First) };
    Group.push_back(Sketch);
    // Every other candidate, placed in the base plane's coordinates.
    std::vector<std::pair<std::string, SketchModel>> Pending;
    for (const FeatureStep& Step : Document.Tree.Steps)
    {
        if (Step.Kind != FeatureStepKind::Sketch || Step.Label == Sketch || SketcherSketchConsumedCondition(Document, Step.Label)) continue;
        const OrthonormalFrame Plane = ElevatedPlane(Document.Tree, Step);
        const Vector3          Apart = Plane.Origin - Base.Origin;
        if (std::fabs(Dot(Plane.ZAxis, Base.ZAxis)) < 1.0 - 1.0e-9 || std::fabs(Dot(Apart, Base.ZAxis)) > 1.0e-9 * std::max(1.0, Length(Apart))) continue;
        SketchPlacement Placement;
        Placement.Origin = { Dot(Apart, Base.XAxis), Dot(Apart, Base.YAxis) };
        Placement.XAxis  = { Dot(Plane.XAxis, Base.XAxis), Dot(Plane.XAxis, Base.YAxis) };
        Placement.YAxis  = { Dot(Plane.YAxis, Base.XAxis), Dot(Plane.YAxis, Base.YAxis) };
        Pending.emplace_back(Step.Label, PlaceSketchModel(SolvedSketch(Document, Step), Placement));
    }
    for (bool GrownCondition = true; GrownCondition;)
    {
        GrownCondition = false;
        for (auto Candidate = Pending.begin(); Candidate != Pending.end(); ++Candidate)
        {
            const double Join = 1.0e-9 * std::max(1.0, MeasureSketchScale(Candidate->second));
            const bool Touching = std::any_of(Members.begin(), Members.end(), [&](const SketchModel& Member) { return SketchContactCondition(Member, Candidate->second, std::max(Join, 1.0e-9 * MeasureSketchScale(Member))); });
            if (!Touching) continue;
            Group.push_back(Candidate->first);
            Members.push_back(Candidate->second);
            Pending.erase(Candidate);
            GrownCondition = true;
            break;
        }
    }
    // Tree order after the first (the join appends in that order).
    std::stable_sort(Group.begin() + 1, Group.end(), [&](const std::string& A, const std::string& B)
    {
        const auto Position = [&](const std::string& Label) { return std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Label; }); };
        return Position(A) < Position(B);
    });
    return Group;
}

SketchOffsetOutcome PreviewSketcherOffset(const SketcherDocument& Document, const std::string& Sketch, double Distance)
{
    const FeatureStep* Found = FindStep(Document, Sketch);
    if (!Found || Found->Kind != FeatureStepKind::Sketch)
    {
        SketchOffsetOutcome Outcome;
        Outcome.Refusal = "no sketch step labelled " + Sketch;
        return Outcome;
    }
    return OffsetSketchCurves(SolvedSketch(Document, *Found), Distance);
}

SketcherCommandOutcome OffsetSketcherSketch(SketcherDocument& Document, const std::string& Sketch, double Distance)
{
    return RunCommand(Document, "Offset " + SketcherBodyCaption(Document, Sketch) + " by " + FormatAmount(Distance), false, [&]() -> std::string
    {
        const SketchOffsetOutcome Outcome = PreviewSketcherOffset(Document, Sketch, Distance);
        if (!Outcome.Refusal.empty()) return Outcome.Refusal;
        const FeatureStep* Found = FindStep(Document, Sketch);
        FeatureStep Step;
        Step.Kind      = FeatureStepKind::Sketch;
        Step.Label     = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Plane     = Found->Plane;
        Step.Elevation = Found->Elevation;
        Step.Sketch    = Outcome.Sketch;
        Document.Captions[Step.Label] = NextCaption(Document, "Offset");
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

SketcherCommandOutcome PipeSketcherPath(SketcherDocument& Document, const std::string& Path, double Radius, FeatureCombination Combination, const std::string& Target)
{
    return RunCommand(Document, "Pipe r " + FormatAmount(Radius) + " along " + SketcherBodyCaption(Document, Path), false, [&]() -> std::string
    {
        const FeatureStep* Found = FindStep(Document, Path);
        if (!Found || (Found->Kind != FeatureStepKind::Sketch && Found->Kind != FeatureStepKind::SpaceCurve)) return "choose the path: focus a sketch step or select a curve";
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Pipe;
        Step.Label       = "Pipe#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Size        = FeatureValue{ Radius };
        Step.Combination = Combination;
        Step.Target      = Combination == FeatureCombination::Separate || Combination == FeatureCombination::New ? std::string{} : Target;
        if (Found->Kind == FeatureStepKind::Sketch) Step.Profile = Path;
        else Step.Sections = { Path };
        Document.Captions[Step.Label] = NextCaption(Document, "Pipe");
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Selection = {};
        return {};
    });
}

SketcherRevolveAxis ProposeSketcherRevolveAxis(const SketcherDocument& Document, const std::string& Sketch)
{
    SketcherRevolveAxis Axis;
    const FeatureStep* Found = FindStep(Document, Sketch);
    if (!Found || Found->Kind != FeatureStepKind::Sketch) return Axis;
    const SketchModel& Model = SolvedSketch(Document, *Found);
    for (const SketchCurve& Curve : Model.Curves)
    {
        if (Curve.Kind == SketchCurveKind::Line && Curve.Construction) { Axis.Line = Curve.Identifier; return Axis; }
    }
    // Side test on the drawn geometry (curve samples, so bulging arcs and splines count).
    double LowX = 1e300, HighX = -1e300, LowY = 1e300, HighY = -1e300, Scale = 0.0;
    for (const SketchCurve& Curve : Model.Curves)
    {
        if (Curve.Construction) continue;
        for (const Vector2& Sample : SampleSketchCurve(Model, Curve))
        {
            LowX = std::min(LowX, Sample.X); HighX = std::max(HighX, Sample.X);
            LowY = std::min(LowY, Sample.Y); HighY = std::max(HighY, Sample.Y);
            Scale = std::max({ Scale, std::fabs(Sample.X), std::fabs(Sample.Y) });
        }
    }
    const double Slack = 1.0e-9 * std::max(Scale, 1.0);
    const bool ClearOfY = LowX >= -Slack || HighX <= Slack, ClearOfX = LowY >= -Slack || HighY <= Slack;
    if (!ClearOfY && ClearOfX) Axis.Direction = { 1.0, 0.0 };
    return Axis;
}

std::string DescribeSketcherRevolveAxis(const SketcherDocument& Document, const std::string& Sketch, const SketcherRevolveAxis& Axis)
{
    if (Axis.Line >= 0)
    {
        const FeatureStep* Found = FindStep(Document, Sketch);
        if (Found)
        {
            const SketchModel& Model = SolvedSketch(Document, *Found);
            for (const SketchCurve& Curve : Model.Curves) if (Curve.Identifier == Axis.Line) return SketchCurveName(Curve);
        }
        return "line " + std::to_string(Axis.Line);
    }
    if (Axis.Origin.X == 0.0 && Axis.Origin.Y == 0.0)
    {
        if (Axis.Direction.X == 0.0 && Axis.Direction.Y != 0.0) return "sketch y-axis";
        if (Axis.Direction.Y == 0.0 && Axis.Direction.X != 0.0) return "sketch x-axis";
    }
    return "axis (" + FormatAmount(Axis.Origin.X) + ", " + FormatAmount(Axis.Origin.Y) + ") + t·(" + FormatAmount(Axis.Direction.X) + ", " + FormatAmount(Axis.Direction.Y) + ")";
}

namespace
{
    // Extrude's combination rules for the profile verbs: the first body is New, a second one needs a Boolean or New body.
    [[nodiscard]] std::string CombinationRefusal(const SketcherDocument& Document, FeatureCombination Combination, bool FirstBodyCondition, const std::string& Target)
    {
        if (FirstBodyCondition && (Combination == FeatureCombination::Difference || Combination == FeatureCombination::Intersection))
            return std::string("nothing to ") + (Combination == FeatureCombination::Difference ? "subtract from" : "intersect with") + " yet (no body)";
        if (!FirstBodyCondition && Combination == FeatureCombination::New) return "a second body needs Union, Difference, Intersection or New body";
        if (!Target.empty() && !FindRegeneratedBody(Document, Target)) return "no body named " + Target;
        return {};
    }
}

SketcherCommandOutcome RevolveSketcherSketch(SketcherDocument& Document, const std::string& Sketch, const SketcherRevolveAxis& Axis, double Angle, FeatureCombination Combination,
                                             const std::string& Target)
{
    const bool        FirstBodyCondition = !Document.Summary.PresentCondition && Combination != FeatureCombination::Separate;
    const std::string Caption = NextCaption(Document, "Revolve");
    const std::string Label   = Caption + " · " + SketcherBodyCaption(Document, Sketch) + " · " + FormatAmount(Angle * 180.0 / std::numbers::pi) + "° about " +
                                DescribeSketcherRevolveAxis(Document, Sketch, Axis) + " · " + (FirstBodyCondition ? "New" : FeatureCombinationLabel(Combination));
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        const FeatureStep* Found = FindStep(Document, Sketch);
        if (!Found || Found->Kind != FeatureStepKind::Sketch) return "choose the profile: focus a sketch step";
        if (!(Angle > 1.0e-9) || Angle > 2.0 * std::numbers::pi + 1.0e-12) return "the angle must lie in (0°, 360°]";
        if (const std::string Refusal = CombinationRefusal(Document, Combination, FirstBodyCondition, Target); !Refusal.empty()) return Refusal;
        FeatureStep Step;
        Step.Kind           = FeatureStepKind::Revolve;
        Step.Label          = "Revolve#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Profile        = Sketch;
        Step.AxisIdentifier = Axis.Line;
        if (Axis.Line < 0) { Step.AxisOrigin = Axis.Origin; Step.AxisDirection = Axis.Direction; }
        Step.Angle          = FeatureValue{ Angle };
        Step.Combination    = FirstBodyCondition ? FeatureCombination::New : Combination;
        Step.Target         = Target;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        return {};
    });
}

SketcherCommandOutcome EvolveSketcherProfile(SketcherDocument& Document, const std::string& Profile, const std::string& Path, FeatureCombination Combination, const std::string& Target)
{
    const bool        FirstBodyCondition = !Document.Summary.PresentCondition && Combination != FeatureCombination::Separate;
    const std::string Caption = NextCaption(Document, "Evolution");
    const std::string Label   = Caption + " · " + SketcherBodyCaption(Document, Profile) + " along " + (Path.empty() ? std::string("?") : SketcherBodyCaption(Document, Path)) + " · " +
                                (FirstBodyCondition ? "New" : FeatureCombinationLabel(Combination));
    return RunCommand(Document, Label, false, [&]() -> std::string
    {
        const FeatureStep* Found = FindStep(Document, Profile);
        if (!Found || Found->Kind != FeatureStepKind::Sketch) return "choose the profile: focus a sketch step";
        const FeatureStep* Spine = FindStep(Document, Path);
        if (!Spine || (Spine->Kind != FeatureStepKind::Sketch && Spine->Kind != FeatureStepKind::SpaceCurve)) return "choose the path: click a one-curve sketch or select a curve";
        if (Path == Profile) return "the path must be another sketch than the profile";
        if (const std::string Refusal = CombinationRefusal(Document, Combination, FirstBodyCondition, Target); !Refusal.empty()) return Refusal;
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Evolution;
        Step.Label       = "Evolution#" + std::to_string(NextStepOrdinal(Document.Tree));
        Step.Profile     = Profile;
        Step.Path        = Path;
        Step.Combination = FirstBodyCondition ? FeatureCombination::New : Combination;
        Step.Target      = Target;
        Document.Captions[Step.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Step));
        Document.Selection = {};
        return {};
    });
}

std::string ProposeSketcherEvolutionPath(const SketcherDocument& Document, const std::string& Profile)
{
    if (!Document.Selection.Curves.empty()) return Document.Selection.Curves.front();
    for (auto Step = Document.Tree.Steps.rbegin(); Step != Document.Tree.Steps.rend(); ++Step)
    {
        if (Step->Kind != FeatureStepKind::Sketch || Step->Label == Profile) continue;
        const SketchModel& Model = SolvedSketch(Document, *Step);
        int Drawn = 0;
        bool Open = true;
        for (const SketchCurve& Curve : Model.Curves)
        {
            if (Curve.Construction) continue;
            ++Drawn;
            Open = Open && Curve.Kind != SketchCurveKind::Circle && !(Curve.Kind == SketchCurveKind::Spline && Curve.Points.size() > 1 && Curve.Points.front() == Curve.Points.back());
        }
        if (Drawn == 1 && Open) return Step->Label;
    }
    return {};
}

std::string SketcherBodyOfName(const SketcherDocument& Document, SketcherSelectionKind Kind, const std::string& Name)
{
    // 💡 The two pieces of a cut carry the same names: the body the user picked on wins, then the active body.
    if (Kind != SketcherSelectionKind::Body)
    {
        for (const std::string& Preferred : { Document.Selection.PickedBody, Document.Regeneration.Active })
        {
            if (Preferred.empty()) continue;
            for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
            {
                if (Body.Key != Preferred) continue;
                const std::map<uint32_t, std::string>& Table = Kind == SketcherSelectionKind::Edge ? Body.Naming.Edges
                                                             : Kind == SketcherSelectionKind::Vertex ? Body.Naming.Vertices : Body.Naming.Faces;
                for (const auto& Entry : Table) if (Entry.second == Name) return Body.Key;
            }
        }
    }
    for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
    {
        if (Kind == SketcherSelectionKind::Body) { if (Body.Key == Name) return Body.Key; continue; }
        const std::map<uint32_t, std::string>& Table = Kind == SketcherSelectionKind::Edge ? Body.Naming.Edges
                                                     : Kind == SketcherSelectionKind::Vertex ? Body.Naming.Vertices : Body.Naming.Faces;
        for (const auto& Entry : Table) if (Entry.second == Name) return Body.Key;
    }
    return {};
}

std::string SketcherBodyCaption(const SketcherDocument& Document, const std::string& Key)
{
    const auto Found = Document.Captions.find(Key);
    return Found != Document.Captions.end() ? Found->second : Key;
}

void ClearSketcherSelection(SketcherDocument& Document)
{
    Document.Selection = {};
}

std::string DescribeSketcherStep(const SketcherDocument& Document, const FeatureStep& Step)
{
    const auto Found = Document.Captions.find(Step.Label);
    if (Found == Document.Captions.end()) return Step.Label;
    return Found->second + " · " + FeatureStepKindLabel(Step.Kind);
}

const char* SketcherConstructLabel(SketcherConstructKind Kind) noexcept
{
    switch (Kind)
    {
    case SketcherConstructKind::Block:    return "Block";
    case SketcherConstructKind::Cylinder: return "Cylinder";
    case SketcherConstructKind::Sphere:   return "Sphere";
    case SketcherConstructKind::Torus:    return "Torus";
    case SketcherConstructKind::Cone:     return "Cone";
    case SketcherConstructKind::Wedge:    return "Wedge";
    case SketcherConstructKind::Prism:    return "Prism";
    case SketcherConstructKind::Tube:     return "Tube";
    }
    return "Construct";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRAINTS (K)
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Label prefix per kind; the rectangle tool's own H1 V1 H2 V2 share the scheme.
    [[nodiscard]] const char* ConstraintPrefix(SketchConstraintKind Constraint) noexcept
    {
        switch (Constraint)
        {
        case Kind::Coincident:         return "C";
        case Kind::Fixed:              return "F";
        case Kind::Horizontal:         return "H";
        case Kind::Vertical:           return "V";
        case Kind::Distance:
        case Kind::Length:             return "D";
        case Kind::HorizontalDistance: return "DX";
        case Kind::VerticalDistance:   return "DY";
        case Kind::Parallel:           return "L";
        case Kind::Perpendicular:      return "P";
        case Kind::Angle:              return "A";
        case Kind::EqualLength:
        case Kind::EqualRadius:        return "Q";
        case Kind::Radius:             return "R";
        case Kind::PointOnLine:
        case Kind::PointOnCircle:      return "O";
        case Kind::Tangent:            return "T";
        case Kind::Midpoint:           return "M";
        case Kind::Symmetric:          return "Y";
        case Kind::Concentric:         return "CC";
        case Kind::ArcRule:            return "AR";
        case Kind::SplineTangent:      return "T";
        case Kind::SplineCurvature:    return "K";                                          // κ: "G1" would read as tangency
        case Kind::PointOnSpline:      return "O";
        case Kind::SplineDirection:    return "E";
        }
        return "X";
    }

    // A spline end direction is labelled by its sense (H / V), the rest by kind.
    [[nodiscard]] std::string LabelPrefix(const SketchConstraint& Constraint)
    {
        if (Constraint.Kind == Kind::SplineDirection && std::fabs(Constraint.Value) < 1.0e-12) return "H";
        if (Constraint.Kind == Kind::SplineDirection && std::fabs(Constraint.Value - std::numbers::pi / 2.0) < 1.0e-12) return "V";
        return ConstraintPrefix(Constraint.Kind);
    }

    [[nodiscard]] std::string NextLabel(const SketchModel& Sketch, const std::string& Prefix)
    {
        int Highest = 0;
        for (const SketchConstraint& Existing : Sketch.Constraints)
        {
            if (Existing.Label.size() <= Prefix.size() || Existing.Label.compare(0, Prefix.size(), Prefix) != 0) continue;
            const std::string Digits = Existing.Label.substr(Prefix.size());
            if (std::all_of(Digits.begin(), Digits.end(), [](unsigned char Character) { return std::isdigit(Character) != 0; })) Highest = std::max(Highest, std::stoi(Digits));
        }
        return Prefix + std::to_string(Highest + 1);
    }

    // Index of the sketch step labelled Label (Steps.size(): none).
    [[nodiscard]] size_t SketchStepIndex(const SketcherDocument& Document, const std::string& Label)
    {
        for (size_t Index = 0; Index < Document.Tree.Steps.size(); ++Index)
            if (Document.Tree.Steps[Index].Label == Label && Document.Tree.Steps[Index].Kind == FeatureStepKind::Sketch) return Index;
        return Document.Tree.Steps.size();
    }

    // The last replay's solve of sketch step Label when it comes before step Before (null Model otherwise).
    [[nodiscard]] LinkedSketchSource EarlierSketch(const SketcherDocument& Document, size_t Before, const std::string& Label)
    {
        const size_t Index = SketchStepIndex(Document, Label);
        if (Index >= Before) return {};
        const auto Solved = Document.Regeneration.Sketches.find(Label);
        if (Solved == Document.Regeneration.Sketches.end()) return {};
        return { &Solved->second, SketchStepFrame(Document.Tree, Document.Tree.Steps[Index]) };
    }

    // 📝 Picks into Model (the sketch of step Target): the edited sketch's own pass through, picks in earlier sketches are
    //    linked in. A point becomes a linked point (one already linked to it is reused); a curve becomes a construction
    //    curve on linked points, carrying the link (a circle's radius is then held too). Circles and arcs need parallel
    //    planes (anything else projects to an ellipse); an arc seen from behind runs the other way. Empty: all linked.
    [[nodiscard]] std::string LinkPicks(const SketcherDocument& Document, size_t Target, SketchModel& Model, const std::vector<SketcherSketchPick>& Picks,
                                        std::vector<SketchPick>& Local, int& Appended)
    {
        const std::string&     Own   = Document.Tree.Steps[Target].Label;
        const OrthonormalFrame Plane = SketchStepFrame(Document.Tree, Document.Tree.Steps[Target]);
        for (const SketcherSketchPick& Pick : Picks)
        {
            if (Pick.Sketch.empty() || Pick.Sketch == Own) { Local.push_back(Pick.Pick); continue; }
            const LinkedSketchSource Source = EarlierSketch(Document, Target, Pick.Sketch);
            if (!Source.Model)
            {
                const size_t Index = SketchStepIndex(Document, Pick.Sketch);
                if (Index == Document.Tree.Steps.size()) return "no sketch step " + Pick.Sketch;
                if (Index > Target) return Pick.Sketch + " comes after " + Own + " (link to earlier sketches only)";
                return Pick.Sketch + " has no solved geometry to link";
            }
            const SketchModel& From      = *Source.Model;
            const double       Alignment = Dot(Source.Plane.ZAxis, Plane.ZAxis);
            const auto Project = [&](int Index) -> Vector2
            {
                const Vector2 Position = From.Points[Index].Position;
                const Vector3 World    = Source.Plane.Origin + Source.Plane.XAxis * Position.X + Source.Plane.YAxis * Position.Y;
                return { Dot(World - Plane.Origin, Plane.XAxis), Dot(World - Plane.Origin, Plane.YAxis) };
            };
            const auto LinkPoint = [&](int Index) -> int
            {
                for (size_t Point = 0; Point < Model.Points.size(); ++Point)
                    if (Model.Points[Point].Link.Sketch == Pick.Sketch && Model.Points[Point].Link.Index == Index) return static_cast<int>(Point);
                const int Added = AppendSketchPoint(Model, Project(Index));
                Model.Points[Added].Link = { Pick.Sketch, Index };
                ++Appended;
                return Added;
            };
            const int Count = Pick.Pick.Kind == SketchPickKind::Point ? static_cast<int>(From.Points.size()) : static_cast<int>(From.Curves.size());
            if (Pick.Pick.Index < 0 || Pick.Pick.Index >= Count) return Pick.Sketch + " has no " + (Pick.Pick.Kind == SketchPickKind::Point ? "point " : "curve ") + std::to_string(Pick.Pick.Index);
            if (Pick.Pick.Kind == SketchPickKind::Point) { Local.push_back({ SketchPickKind::Point, LinkPoint(Pick.Pick.Index) }); continue; }
            int Reused = -1;
            for (size_t Index = 0; Index < Model.Curves.size(); ++Index)
                if (Model.Curves[Index].Link.Sketch == Pick.Sketch && Model.Curves[Index].Link.Index == Pick.Pick.Index) Reused = static_cast<int>(Index);
            if (Reused >= 0) { Local.push_back({ SketchPickKind::Curve, Reused }); continue; }
            const SketchCurve& Curve = From.Curves[Pick.Pick.Index];
            if ((Curve.Kind == SketchCurveKind::Circle || Curve.Kind == SketchCurveKind::Arc) && std::fabs(Alignment) < 1.0 - 1.0e-12)
                return SketchCurveName(Curve) + " of " + Pick.Sketch + " is not parallel to " + Own + " (it would project to an ellipse)";
            int Identifier = 1;
            for (const SketchCurve& Existing : Model.Curves) Identifier = std::max(Identifier, Existing.Identifier + 1);
            int Added = -1;
            switch (Curve.Kind)
            {
            case SketchCurveKind::Line:
            {
                const int Start = LinkPoint(Curve.Start);
                Added = AppendSketchLine(Model, Start, LinkPoint(Curve.End), Identifier, true);
                break;
            }
            case SketchCurveKind::Circle:
                Added = AppendSketchCircle(Model, LinkPoint(Curve.Centre), MeasureSketchRadius(From, Curve), Identifier, true);
                break;
            case SketchCurveKind::Arc:
            {
                const int Centre = LinkPoint(Curve.Centre);
                const int Start  = LinkPoint(Curve.Start);
                const int End    = LinkPoint(Curve.End);
                Added = Alignment > 0.0 ? AppendSketchArc(Model, Centre, Start, End, Identifier, true) : AppendSketchArc(Model, Centre, End, Start, Identifier, true);
                break;
            }
            case SketchCurveKind::Spline:
            {
                std::vector<int> Points;
                for (int Point : Curve.Points) Points.push_back(LinkPoint(Point));
                Added = AppendSketchSpline(Model, Points, Curve.Form, Identifier, true);
                Model.Curves[Added].Knots = Curve.Knots;
                break;
            }
            }
            Model.Curves[Added].Link = { Pick.Sketch, Pick.Pick.Index };
            ++Appended;
            Local.push_back({ SketchPickKind::Curve, Added });
        }
        return {};
    }

    [[nodiscard]] FeatureStep* FindSketchStep(SketcherDocument& Document, const std::string& Label, size_t& Index)
    {
        for (Index = 0; Index < Document.Tree.Steps.size(); ++Index)
            if (Document.Tree.Steps[Index].Label == Label) return Document.Tree.Steps[Index].Kind == FeatureStepKind::Sketch ? &Document.Tree.Steps[Index] : nullptr;
        return nullptr;
    }

    // The step's sketch with every parameter-bound dimension set to its parameter's value and its links re-projected from
    // the last replay, as regeneration solves it.
    [[nodiscard]] SketchModel BindSketch(const SketcherDocument& Document, const FeatureStep& Step)
    {
        SketchModel Bound = Step.Sketch;
        const size_t Target = SketchStepIndex(Document, Step.Label);
        if (Target < Document.Tree.Steps.size())
        {
            const std::vector<std::string> Notes = ProjectSketchLinks(Bound, SketchStepFrame(Document.Tree, Step), [&](const std::string& Label) { return EarlierSketch(Document, Target, Label); });
            static_cast<void>(Notes);                                                        // Dangling links stay held; the replay reports them
        }
        for (SketchConstraint& Constraint : Bound.Constraints)
        {
            if (Constraint.Parameter.empty()) continue;
            const double Value = EvaluateFeatureValue(Document.Tree, ParameterValue(Constraint.Parameter));
            if (!std::isnan(Value)) Constraint.Value = Value;
        }
        return Bound;
    }

    // Solved positions (points and circle radii) back into the authored sketch, with the reference dimensions' readings;
    // constraints stay as authored.
    void WriteBackPositions(FeatureStep& Step, const SketchModel& Solved)
    {
        for (size_t Point = 0; Point < Step.Sketch.Points.size() && Point < Solved.Points.size(); ++Point) Step.Sketch.Points[Point].Position = Solved.Points[Point].Position;
        for (size_t Curve = 0; Curve < Step.Sketch.Curves.size() && Curve < Solved.Curves.size(); ++Curve) Step.Sketch.Curves[Curve].Radius = Solved.Curves[Curve].Radius;
        for (size_t Constraint = 0; Constraint < Step.Sketch.Constraints.size() && Constraint < Solved.Constraints.size(); ++Constraint)
            if (Step.Sketch.Constraints[Constraint].DrivenCondition) Step.Sketch.Constraints[Constraint].Value = Solved.Constraints[Constraint].Value;
    }

    // Index of the G1 a G2 on the same spline joint supersedes (-1: none).
    [[nodiscard]] int SupersededTangent(const SketchModel& Sketch, const SketchConstraint& Curvature)
    {
        if (Curvature.Kind != Kind::SplineCurvature) return -1;
        for (size_t Index = 0; Index < Sketch.Constraints.size(); ++Index)
        {
            const SketchConstraint& Existing = Sketch.Constraints[Index];
            if (Existing.Kind != Kind::SplineTangent || Existing.Third != Curvature.Third) continue;
            const bool Same    = Existing.First == Curvature.First && Existing.Second == Curvature.Second;
            const bool Swapped = Existing.First == Curvature.Second && Existing.Second == Curvature.First;
            if (Same || Swapped) return static_cast<int>(Index);
        }
        return -1;
    }

    [[nodiscard]] std::string NameConstraint(const SketchModel& Sketch, int Index)
    {
        const SketchConstraint& Constraint = Sketch.Constraints[Index];
        const std::string Text = DescribeSketchConstraint(Sketch, Constraint);
        return Constraint.Label.empty() ? Text : Constraint.Label + " (" + Text + ")";
    }

    // 📝 Why a solve of Sketch is not acceptable, naming constraints; empty when it converged without conflicts. Watch:
    //    the index of the constraint just added (-1: none), refused also when merely redundant.
    [[nodiscard]] std::string SolveRefusal(const SketchModel& Sketch, const SketchSolveOutcome& Solve, int Watch)
    {
        if (Watch >= 0 && Watch < static_cast<int>(Solve.Constraints.size()))
        {
            const SketchConstraintReport& Report = Solve.Constraints[Watch];
            std::string Partners;
            for (int Partner : Report.Partners) if (Partner != Watch) Partners += (Partners.empty() ? "" : ", ") + NameConstraint(Sketch, Partner);
            const std::string Subject = DescribeSketchConstraint(Sketch, Sketch.Constraints[Watch]);
            if (Report.Status == SketchConstraintStatus::Unsupported) return Subject + ": its references do not fit the constraint";
            if (Report.Status == SketchConstraintStatus::Redundant) return Subject + " is already implied by " + (Partners.empty() ? std::string("the others") : Partners);
            if (Report.Status == SketchConstraintStatus::Conflicting) return Subject + " conflicts with " + (Partners.empty() ? std::string("the others") : Partners);
        }
        if (!Solve.ConflictGroups.empty())
        {
            std::string Group;
            for (int Member : Solve.ConflictGroups.front()) Group += (Group.empty() ? "" : ", ") + NameConstraint(Sketch, Member);
            return "conflicting constraints: " + Group;
        }
        if (!Solve.Converged) return "the sketch has no solution there (largest residual " + std::to_string(Solve.MaxResidual) + ")";
        return {};
    }

    [[nodiscard]] std::string SketchOwner(const SketcherDocument& Document, const std::string& Label)
    {
        const auto Caption = Document.Captions.find(Label);
        return Caption != Document.Captions.end() ? Caption->second : Label;
    }
}

std::string NextSketcherConstraintLabel(const SketchModel& Sketch, SketchConstraintKind Constraint)
{
    return NextLabel(Sketch, ConstraintPrefix(Constraint));
}

SketcherLinkedPicks LinkSketcherPicks(const SketcherDocument& Document, const std::string& Sketch, const std::vector<SketcherSketchPick>& Picks)
{
    SketcherLinkedPicks Linked;
    const size_t Target = SketchStepIndex(Document, Sketch);
    if (Target == Document.Tree.Steps.size()) { Linked.Refusal = "no sketch step " + Sketch; return Linked; }
    Linked.Sketch  = BindSketch(Document, Document.Tree.Steps[Target]);
    static_cast<void>(SolveSketch(Linked.Sketch));                                           // Proposals measure what is shown
    Linked.Refusal = LinkPicks(Document, Target, Linked.Sketch, Picks, Linked.Picks, Linked.Appended);
    return Linked;
}

std::vector<std::string> SketcherLinkSources(const SketcherDocument& Document, const std::string& Sketch)
{
    std::vector<std::string> Sources;
    const size_t Target = SketchStepIndex(Document, Sketch);
    for (size_t Index = 0; Index < Target && Index < Document.Tree.Steps.size(); ++Index)
        if (Document.Tree.Steps[Index].Kind == FeatureStepKind::Sketch) Sources.push_back(Document.Tree.Steps[Index].Label);
    return Sources;
}

bool SketcherLinkedCondition(const SketchModel& Sketch, SketchPick Pick) noexcept
{
    if (Pick.Kind == SketchPickKind::Point) return Pick.Index >= 0 && Pick.Index < static_cast<int>(Sketch.Points.size()) && !Sketch.Points[Pick.Index].Link.Sketch.empty();
    return Pick.Index >= 0 && Pick.Index < static_cast<int>(Sketch.Curves.size()) && !Sketch.Curves[Pick.Index].Link.Sketch.empty();
}

SketcherSketchDiagnosis DiagnoseSketcherSketch(const SketcherDocument& Document, const std::string& Sketch)
{
    SketcherSketchDiagnosis Diagnosis;
    for (const FeatureStep& Step : Document.Tree.Steps)
    {
        if (Step.Label != Sketch || Step.Kind != FeatureStepKind::Sketch) continue;
        Diagnosis.PresentCondition = true;
        Diagnosis.Sketch           = BindSketch(Document, Step);
        Diagnosis.Solve            = SolveSketch(Diagnosis.Sketch);
    }
    return Diagnosis;
}

SketcherCommandOutcome ConstrainSketcherSketch(SketcherDocument& Document, const std::string& Sketch, const SketchConstraint& Constraint, const std::vector<SketcherSketchPick>& Links)
{
    std::string Subject = SketchConstraintTitle(Constraint.Kind);
    const SketcherLinkedPicks Preview = LinkSketcherPicks(Document, Sketch, Links);
    const int Superseded = Preview.Refusal.empty() ? SupersededTangent(Preview.Sketch, Constraint) : -1;
    if (Preview.Refusal.empty()) Subject = DescribeSketchConstraint(Preview.Sketch, Constraint);
    if (Superseded >= 0) Subject += " (replaces " + NameConstraint(Preview.Sketch, Superseded) + ")";
    size_t Index = 0;
    FeatureStep* Found = FindSketchStep(Document, Sketch, Index);
    return RunCommand(Document, "Constrain " + SketchOwner(Document, Sketch) + ": " + Subject, false, [&]() -> std::string
    {
        if (!Found) return "no sketch step " + Sketch;
        if (!Preview.Refusal.empty()) return Preview.Refusal;
        std::vector<SketchPick> Local;
        int Appended = 0;
        if (std::string Refusal = LinkPicks(Document, Index, Found->Sketch, Links, Local, Appended); !Refusal.empty()) return Refusal;
        if (Superseded >= 0) Found->Sketch.Constraints.erase(Found->Sketch.Constraints.begin() + Superseded);
        SketchConstraint Added = Constraint;
        if (Added.Label.empty()) Added.Label = NextLabel(Found->Sketch, LabelPrefix(Added));
        Found->Sketch.Constraints.push_back(Added);
        SketchModel Solved = BindSketch(Document, *Found);
        const SketchSolveOutcome Solve = SolveSketch(Solved);
        if (std::string Refusal = SolveRefusal(Solved, Solve, static_cast<int>(Solved.Constraints.size()) - 1); !Refusal.empty()) return Refusal;
        WriteBackPositions(*Found, Solved);
        return {};
    }, Index);
}

SketcherCommandOutcome RemoveSketcherConstraint(SketcherDocument& Document, const std::string& Sketch, int Constraint)
{
    size_t Index = 0;
    FeatureStep* Found = FindSketchStep(Document, Sketch, Index);
    const std::string Named = Found && Constraint >= 0 && Constraint < static_cast<int>(Found->Sketch.Constraints.size()) ? NameConstraint(Found->Sketch, Constraint) : std::string("constraint");
    return RunCommand(Document, "Remove " + Named + " from " + SketchOwner(Document, Sketch), false, [&]() -> std::string
    {
        if (!Found) return "no sketch step " + Sketch;
        if (Constraint < 0 || Constraint >= static_cast<int>(Found->Sketch.Constraints.size())) return "the sketch has no constraint " + std::to_string(Constraint);
        Found->Sketch.Constraints.erase(Found->Sketch.Constraints.begin() + Constraint);
        return {};
    }, Index);
}

SketcherCommandOutcome ChangeSketcherDimension(SketcherDocument& Document, const std::string& Sketch, int Constraint, double Value)
{
    size_t Index = 0;
    FeatureStep* Found = FindSketchStep(Document, Sketch, Index);
    const bool  Valid = Found && Constraint >= 0 && Constraint < static_cast<int>(Found->Sketch.Constraints.size());
    const bool  Angular = Valid && Found->Sketch.Constraints[Constraint].Kind == Kind::Angle;
    char Shown[48];
    std::snprintf(Shown, sizeof(Shown), Angular ? "%.6g°" : "%.6g", Angular ? Value * 180.0 / std::numbers::pi : Value);
    const std::string Named = Valid ? (Found->Sketch.Constraints[Constraint].Label.empty() ? std::string(SketchConstraintTitle(Found->Sketch.Constraints[Constraint].Kind)) : Found->Sketch.Constraints[Constraint].Label) : std::string("dimension");
    return RunCommand(Document, Named + " = " + Shown + " in " + SketchOwner(Document, Sketch), false, [&]() -> std::string
    {
        if (!Found) return "no sketch step " + Sketch;
        if (!Valid) return "the sketch has no constraint " + std::to_string(Constraint);
        SketchConstraint& Dimension = Found->Sketch.Constraints[Constraint];
        if (!SketchDimensionCondition(Dimension.Kind)) return DescribeSketchConstraint(Found->Sketch, Dimension) + " has no value to change";
        if (Dimension.DrivenCondition) return Named + " is a reference dimension (E makes it driving)";
        if (!std::isfinite(Value)) return "the value is not a number";
        const bool Positive = Dimension.Kind == Kind::Distance || Dimension.Kind == Kind::Length || Dimension.Kind == Kind::Radius;
        if (Positive && !(Value > 0.0)) return std::string(SketchConstraintTitle(Dimension.Kind)) + " must be positive";
        if (!Dimension.Parameter.empty())
        {
            if (!AssignFeatureParameter(Document.Tree, Dimension.Parameter, Value)) return "unknown parameter " + Dimension.Parameter;
        }
        else Dimension.Value = Value;
        SketchModel Solved = BindSketch(Document, *Found);
        const SketchSolveOutcome Solve = SolveSketch(Solved);
        if (std::string Refusal = SolveRefusal(Solved, Solve, -1); !Refusal.empty()) return Refusal;
        WriteBackPositions(*Found, Solved);
        return {};
    }, Index);
}

SketcherCommandOutcome ToggleSketcherReference(SketcherDocument& Document, const std::string& Sketch, int Constraint)
{
    size_t Index = 0;
    FeatureStep* Found = FindSketchStep(Document, Sketch, Index);
    const bool Valid    = Found && Constraint >= 0 && Constraint < static_cast<int>(Found->Sketch.Constraints.size());
    const bool ToDriven = Valid && !Found->Sketch.Constraints[Constraint].DrivenCondition;
    const std::string Named = Valid ? NameConstraint(Found->Sketch, Constraint) : std::string("dimension");
    return RunCommand(Document, std::string(ToDriven ? "Reference " : "Drive ") + Named + " in " + SketchOwner(Document, Sketch), false, [&]() -> std::string
    {
        if (!Found) return "no sketch step " + Sketch;
        if (!Valid) return "the sketch has no constraint " + std::to_string(Constraint);
        SketchConstraint& Dimension = Found->Sketch.Constraints[Constraint];
        if (!SketchDimensionCondition(Dimension.Kind)) return Named + " has no value (only dimensions can be references)";
        if (ToDriven && !Dimension.Parameter.empty()) return Named + " is bound to " + Dimension.Parameter + " (it stays driving)";
        if (ToDriven)
        {
            Dimension.DrivenCondition = true;
            SketchModel Solved = BindSketch(Document, *Found);
            static_cast<void>(SolveSketch(Solved));
            WriteBackPositions(*Found, Solved);
            return {};
        }
        SketchModel Reading = BindSketch(Document, *Found);                                  // The current reading, so nothing moves
        static_cast<void>(SolveSketch(Reading));
        Dimension.DrivenCondition = false;
        Dimension.Value           = Reading.Constraints[Constraint].Value;
        SketchModel Solved = BindSketch(Document, *Found);
        const SketchSolveOutcome Solve = SolveSketch(Solved);
        if (std::string Refusal = SolveRefusal(Solved, Solve, Constraint); !Refusal.empty()) return Refusal + " (it stays a reference)";
        WriteBackPositions(*Found, Solved);
        return {};
    }, Index);
}

SketcherCommandOutcome UnlinkSketcherGeometry(SketcherDocument& Document, const std::string& Sketch, SketchPick Pick)
{
    size_t Index = 0;
    FeatureStep* Found = FindSketchStep(Document, Sketch, Index);
    const std::string Named = Found ? NameSketchPick(Found->Sketch, Pick) : std::string("geometry");
    return RunCommand(Document, "Unlink " + Named + " of " + SketchOwner(Document, Sketch), false, [&]() -> std::string
    {
        if (!Found) return "no sketch step " + Sketch;
        if (!SketcherLinkedCondition(Found->Sketch, Pick)) return Named + " is not linked";
        SketchModel Projected = BindSketch(Document, *Found);                                // Where it is shown now, so it stays there
        static_cast<void>(SolveSketch(Projected));
        WriteBackPositions(*Found, Projected);
        SketchModel& Model = Found->Sketch;
        const auto Free = [&](int Point) { if (Point >= 0 && Point < static_cast<int>(Model.Points.size())) Model.Points[Point].Link = {}; };
        if (Pick.Kind == SketchPickKind::Point)
        {
            Free(Pick.Index);
            for (SketchCurve& Curve : Model.Curves) if (Curve.Kind == SketchCurveKind::Circle && Curve.Centre == Pick.Index) Curve.Link = {};
            return {};
        }
        SketchCurve& Curve = Model.Curves[Pick.Index];
        Curve.Link = {};
        Free(Curve.Start);
        Free(Curve.End);
        Free(Curve.Centre);
        for (int Point : Curve.Points) Free(Point);
        return {};
    }, Index);
}

SketchDragOutcome PreviewSketcherDrag(const SketcherDocument& Document, const std::string& Sketch, int Point, Vector2 Target)
{
    for (const FeatureStep& Step : Document.Tree.Steps)
        if (Step.Label == Sketch && Step.Kind == FeatureStepKind::Sketch) return DragSketchPoint(BindSketch(Document, Step), Point, Target);
    SketchDragOutcome Missing;
    Missing.Refusal = "Drag: no sketch step " + Sketch;
    return Missing;
}

SketcherCommandOutcome DragSketcherSketchPoint(SketcherDocument& Document, const std::string& Sketch, int Point, Vector2 Target)
{
    size_t Index = 0;
    FeatureStep* Found = FindSketchStep(Document, Sketch, Index);
    const std::string Named = Found ? NameSketchPick(Found->Sketch, { SketchPickKind::Point, Point }) : std::string("point");
    return RunCommand(Document, "Drag " + Named + " of " + SketchOwner(Document, Sketch), false, [&]() -> std::string
    {
        if (!Found) return "no sketch step " + Sketch;
        const SketchDragOutcome Drag = PreviewSketcherDrag(Document, Sketch, Point, Target);
        if (!Drag.Refusal.empty()) return Drag.Refusal;
        if (!(Drag.Travel > 1.0e-12 * std::max(1.0, Drag.Solve.Scale))) return Named + " is fully constrained (it cannot move)";
        WriteBackPositions(*Found, Drag.Sketch);
        return {};
    }, Index);
}

SketcherCommandOutcome DragSketcherSketchPoints(SketcherDocument& Document, const std::string& Sketch, const std::vector<int>& Points, const std::vector<Vector2>& Targets)
{
    if (Points.size() == 1 && Targets.size() == 1) return DragSketcherSketchPoint(Document, Sketch, Points[0], Targets[0]);
    size_t Index = 0;
    FeatureStep* Found = FindSketchStep(Document, Sketch, Index);
    return RunCommand(Document, "Drag " + std::to_string(Points.size()) + " points of " + SketchOwner(Document, Sketch), false, [&]() -> std::string
    {
        if (!Found) return "no sketch step " + Sketch;
        if (Points.empty() || Points.size() != Targets.size()) return "Drag: points and targets do not pair up";
        SketchModel Working = {};
        for (const FeatureStep& Step : Document.Tree.Steps)
            if (Step.Label == Sketch && Step.Kind == FeatureStepKind::Sketch) Working = BindSketch(Document, Step);
        for (int Point : Points)
            if (Point < 0 || Point >= int(Working.Points.size())) return "Drag: no point " + std::to_string(Point) + " in " + Sketch;
        const std::vector<SketchPoint> Start = Working.Points;
        double Travel = 0.0;
        for (int Sweep = 0; Sweep < 6; ++Sweep)
        {
            double Miss = 0.0;
            for (size_t Pick = 0; Pick < Points.size(); ++Pick)
            {
                const SketchDragOutcome Drag = DragSketchPoint(Working, Points[Pick], Targets[Pick]);
                if (!Drag.Refusal.empty()) return Drag.Refusal;
                Working = Drag.Sketch;
            }
            for (size_t Pick = 0; Pick < Points.size(); ++Pick) Miss = std::max(Miss, Distance(Working.Points[Points[Pick]].Position, Targets[Pick]));
            if (Miss <= 1.0e-9) break;
        }
        for (size_t Point = 0; Point < Start.size(); ++Point) Travel = std::max(Travel, Distance(Start[Point].Position, Working.Points[Point].Position));
        if (!(Travel > 1.0e-12)) return "the points are fully constrained (they cannot move)";
        WriteBackPositions(*Found, Working);
        return {};
    }, Index);
}

const char* SketcherDrawingLabel(SketcherDrawingKind Kind) noexcept
{
    switch (Kind)
    {
    case SketcherDrawingKind::Line:      return "Line";
    case SketcherDrawingKind::Rectangle: return "Rectangle";
    case SketcherDrawingKind::Circle:    return "Circle";
    case SketcherDrawingKind::Arc:       return "Arc";
    case SketcherDrawingKind::Slot:      return "Slot";
    case SketcherDrawingKind::Curve:     return "Curve";
    case SketcherDrawingKind::Polygon:   return "Polygon";
    }
    return "Drawing";
}

const char* SketcherFaceOperationLabel(FaceOperationKind Operation) noexcept
{
    switch (Operation)
    {
    case FaceOperationKind::Offset:     return "Offset";
    case FaceOperationKind::Push:       return "Push";
    case FaceOperationKind::Tweak:      return "Tweak";
    case FaceOperationKind::Scale:      return "Scale";
    case FaceOperationKind::Rotate:     return "Rotate";
    case FaceOperationKind::Draft:      return "Draft";
    case FaceOperationKind::Extend:     return "Extend";
    case FaceOperationKind::Trim:       return "Trim";
    case FaceOperationKind::OffsetBody: return "OffsetBody";
    case FaceOperationKind::Hollow:     return "Hollow";
    case FaceOperationKind::Extrude:    return "ExtrudeFaces";
    case FaceOperationKind::Delete:     return "DeleteFaces";
    case FaceOperationKind::Heal:       return "Heal";
    case FaceOperationKind::Thicken:    return "Thicken";
    case FaceOperationKind::Inset:      return "Inset";
    case FaceOperationKind::Reshape:    return "Reshape";
    }
    return "Change";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DELETE (X / DELETE)
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The face names of an edge name Edge(A|B)[#k] (the sorted pair; one name for a seam, A|∅ for a laminar edge).
    [[nodiscard]] std::vector<std::string> EdgeFaceNames(const std::string& Edge)
    {
        std::vector<std::string> Faces;
        if (Edge.rfind("Edge(", 0) != 0) return Faces;
        int         Depth = 1;
        std::string Piece;
        for (size_t Index = 5; Index < Edge.size(); ++Index)
        {
            const char Letter = Edge[Index];
            if (Letter == '(') ++Depth;
            if (Letter == ')' && --Depth == 0)
            {
                if (!Piece.empty()) Faces.push_back(Piece);
                return Faces;
            }
            if (Letter == '|' && Depth == 1)
            {
                if (!Piece.empty()) Faces.push_back(Piece);
                Piece.clear();
                continue;
            }
            Piece += Letter;
        }
        return {};
    }

    // 📝 Blend#n.Strip(<edge>)[/k] → that blend step's label and the source edge name; false for any other face.
    [[nodiscard]] bool BlendStripSource(const SketcherDocument& Document, const std::string& Face, std::string& Blend, std::string& Source)
    {
        static constexpr std::string_view Marker = ".Strip(";
        const size_t At = Face.find(Marker);
        if (At == std::string::npos) return false;
        const FeatureStep* Step = FindStep(Document, Face.substr(0, At));
        if (!Step || Step->Kind != FeatureStepKind::Blend) return false;
        const size_t Open  = At + Marker.size();
        int          Depth = 1;
        for (size_t Index = Open; Index < Face.size(); ++Index)
        {
            if (Face[Index] == '(') ++Depth;
            else if (Face[Index] == ')' && --Depth == 0)
            {
                Blend  = Step->Label;
                Source = Face.substr(Open, Index - Open);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::string CaptionOrLabel(const SketcherDocument& Document, const std::string& Label)
    {
        const auto Found = Document.Captions.find(Label);
        return Found != Document.Captions.end() ? Found->second : Label;
    }

    // 📝 A step erased with its caption and the parameters named after it (Caption.Size, Caption.Amount …).
    void EraseStepWithParameters(SketcherDocument& Document, const std::string& Label)
    {
        const std::string Caption = CaptionOrLabel(Document, Label);
        std::erase_if(Document.Tree.Steps, [&](const FeatureStep& Step) { return Step.Label == Label; });
        std::erase_if(Document.Tree.Parameters, [&](const FeatureParameter& Parameter) { return Parameter.Name.rfind(Caption + ".", 0) == 0; });
        Document.Captions.erase(Label);
    }

    // 📝 Un-blend: every blend step lets go of its source edges (a variable blend's anchor paired with an edge goes with
    //    it); an emptied blend step goes with its parameters. Watched from the first edited step, so a later step that
    //    regenerated before and now refuses rolls the whole command back.
    [[nodiscard]] SketcherCommandOutcome UnblendSketcherEdges(SketcherDocument& Document, const std::map<std::string, std::set<std::string>>& Sources,
                                                             const std::string& Noun)
    {
        size_t      First = Document.Tree.Steps.size();
        size_t      Count = 0;
        std::string Captions;
        for (const auto& [Blend, Edges] : Sources)
        {
            for (size_t Index = 0; Index < Document.Tree.Steps.size(); ++Index) if (Document.Tree.Steps[Index].Label == Blend) First = std::min(First, Index);
            Captions += (Captions.empty() ? "" : ", ") + CaptionOrLabel(Document, Blend);
            Count    += Edges.size();
        }
        SketcherCommandOutcome Outcome = RunCommand(Document, "Delete " + Noun + ": un-blend " + std::to_string(Count) + " edge(s) of " + Captions, true, [&]() -> std::string
        {
            for (const auto& [Blend, Edges] : Sources)
            {
                const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Blend; });
                if (Found == Document.Tree.Steps.end()) return "no blend step labelled " + Blend;
                FeatureStep& Step   = *Found;
                const bool   Paired = Step.EndSize && Step.AnchorSizes.empty() && Step.SizeAnchors.size() == Step.References.size();
                size_t       Dropped = 0;
                // ① exact names, ② when none matched, names without their #k disambiguators (a re-numbered face pair).
                for (int Round = 0; Round < 2 && Dropped == 0; ++Round)
                {
                    for (size_t Index = Step.References.size(); Index-- > 0;)
                    {
                        const std::string& Name  = Step.References[Index].Name;
                        const bool         Match = Round == 0 ? Edges.contains(Name)
                                                              : std::any_of(Edges.begin(), Edges.end(), [&](const std::string& Edge) { return StripDisambiguators(Edge) == StripDisambiguators(Name); });
                        if (!Match) continue;
                        Step.References.erase(Step.References.begin() + static_cast<std::ptrdiff_t>(Index));
                        if (Paired) Step.SizeAnchors.erase(Step.SizeAnchors.begin() + static_cast<std::ptrdiff_t>(Index));
                        ++Dropped;
                    }
                }
                if (Dropped == 0) return CaptionOrLabel(Document, Blend) + " no longer lists the edge under that face (delete the step from History instead)";
                if (Step.References.empty()) EraseStepWithParameters(Document, Blend);
            }
            return {};
        }, First);
        if (Outcome.AcceptedCondition) Document.Selection = {};                             // A refusal keeps the picks (Restore leaves them)
        return Outcome;
    }

    // 📝 Dissolve by a restricted Heal step on Owner carrying the picked edges / vertices (the kernel refuses unless every
    //    one goes, and touches nothing else). Appended: a heal acts on the body as it stands.
    [[nodiscard]] SketcherCommandOutcome DissolveSketcherNames(SketcherDocument& Document, SketcherSelectionKind Kind, const std::vector<std::string>& Names)
    {
        const bool        EdgeCondition = Kind == SketcherSelectionKind::Edge;
        const std::string Noun          = EdgeCondition ? " edge(s)" : " vertex(es)";
        std::string       Owner, Problem;
        if (Names.empty()) Problem = EdgeCondition ? "pick edges first (mode 2)" : "pick vertices first (mode 1)";
        else if (!Document.Summary.PresentCondition) Problem = "there is no body";
        else Problem = CommonBody(Document, Kind, Names, Owner);
        SketcherCommandOutcome Outcome = RunCommand(Document, "Delete " + std::to_string(Names.size()) + Noun + " · dissolve", true, [&]() -> std::string
        {
            if (!Problem.empty()) return Problem;
            FeatureStep Step;
            Step.Kind      = FeatureStepKind::FaceOperation;
            Step.Label     = "Change#" + std::to_string(NextStepOrdinal(Document.Tree));
            Step.Operation = FaceOperationKind::Heal;
            Step.Target    = Owner;
            for (const std::string& Name : Names) Step.References.push_back({ EdgeCondition ? ReferenceKind::Edge : ReferenceKind::Vertex, Name, {} });
            Document.Captions[Step.Label] = NextCaption(Document, "Dissolve");
            Document.Tree.Steps.push_back(std::move(Step));
            return {};
        });
        if (Outcome.AcceptedCondition) Document.Selection = {};                             // A refusal keeps the picks
        else if (const size_t Reason = Outcome.Refusal.find("Heal: "); Reason != std::string::npos) Outcome.Refusal = Outcome.Refusal.substr(Reason + 6);
        return Outcome;
    }

    // 📝 A curve leaves: deleted when nothing reads it, else hidden and off the Outliner (RemovedCurves) for its readers.
    void RetireSketcherCurve(SketcherDocument& Document, const std::string& Label)
    {
        std::erase(Document.Selection.Curves, Label);
        if (SketcherSketchReferencedCondition(Document, Label))
        {
            Document.Appearance.HiddenCurves.insert(Label);
            Document.Appearance.RemovedCurves.insert(Label);
            Document.Appearance.LockedFigures.erase(Label);
            return;
        }
        EraseStepWithParameters(Document, Label);
        Document.Appearance.HiddenCurves.erase(Label);
        Document.Appearance.LockedFigures.erase(Label);
    }
} // namespace

SketcherCommandOutcome DeleteSketcherFaces(SketcherDocument& Document)
{
    const std::vector<std::string> Faces = Document.Selection.Faces;
    std::map<std::string, std::set<std::string>> Sources;
    size_t Strips = 0;
    for (const std::string& Face : Faces)
    {
        std::string Blend, Source;
        if (!BlendStripSource(Document, Face, Blend, Source)) continue;
        Sources[Blend].insert(Source);
        ++Strips;
    }
    if (!Faces.empty() && Strips == Faces.size()) return UnblendSketcherEdges(Document, Sources, std::to_string(Faces.size()) + " fillet face(s)");
    return ChangeSketcherSelection(Document, FaceOperationKind::Delete, 0.0);
}

SketcherCommandOutcome DeleteSketcherEdges(SketcherDocument& Document)
{
    const std::vector<std::string> Edges = Document.Selection.Edges;
    std::map<std::string, std::set<std::string>> Sources;
    size_t Blended = 0;
    for (const std::string& Edge : Edges)
    {
        bool Found = false;
        for (const std::string& Face : EdgeFaceNames(Edge))
        {
            std::string Blend, Source;
            if (!BlendStripSource(Document, Face, Blend, Source)) continue;
            Sources[Blend].insert(Source);
            Found = true;
        }
        Blended += Found ? 1 : 0;
    }
    if (!Edges.empty() && Blended == Edges.size()) return UnblendSketcherEdges(Document, Sources, std::to_string(Edges.size()) + " fillet edge(s)");
    if (Blended > 0)
    {
        SketcherCommandOutcome Outcome;
        Outcome.Label   = "Delete " + std::to_string(Edges.size()) + " edge(s)";
        Outcome.Refusal = "fillet edges and plain edges are picked together: delete them separately";
        return Outcome;
    }
    return DissolveSketcherNames(Document, SketcherSelectionKind::Edge, Edges);
}

SketcherCommandOutcome DeleteSketcherVertices(SketcherDocument& Document)
{
    return DissolveSketcherNames(Document, SketcherSelectionKind::Vertex, Document.Selection.Vertices);
}

SketcherCommandOutcome DeleteSketcherCurves(SketcherDocument& Document, const std::vector<std::string>& Labels)
{
    const std::vector<std::string> Selected = UniqueKeys(Labels); // May alias Document.Selection.Curves.
    std::string Captions;
    bool Kept = false;
    for (const std::string& Label : Selected)
    {
        const bool Read = SketcherSketchReferencedCondition(Document, Label);
        Captions += (Captions.empty() ? "" : ", ") + CaptionOrLabel(Document, Label) + (Read ? " sketch" : "");
        Kept = Kept || Read;
    }
    return RunCommand(Document, "Delete " + Captions + (Kept ? " (kept for the steps that read it)" : ""), false, [&]() -> std::string
    {
        if (Selected.empty()) return "pick sketches or curves first";
        for (const std::string& Label : Selected)
        {
            const FeatureStep* Step = FindStep(Document, Label);
            if (!Step || (Step->Kind != FeatureStepKind::Sketch && Step->Kind != FeatureStepKind::SpaceCurve)) return "no sketch or curve labelled " + Label;
            if (SketcherLockedCondition(Document, Label)) return CaptionOrLabel(Document, Label) + " is locked";
            if (Document.Appearance.RemovedCurves.contains(Label)) return CaptionOrLabel(Document, Label) + " is already deleted";
        }
        std::vector<std::string> Ordered;
        for (auto It = Document.Tree.Steps.rbegin(); It != Document.Tree.Steps.rend(); ++It)
            if (std::find(Selected.begin(), Selected.end(), It->Label) != Selected.end()) Ordered.push_back(It->Label);
        for (const std::string& Label : Ordered) RetireSketcherCurve(Document, Label);
        return {};
    }, 0);
}

SketcherCommandOutcome DeleteSketcherSketchPoints(SketcherDocument& Document, const std::string& Sketch, const std::vector<int>& Points)
{
    const std::string Owner = CaptionOrLabel(Document, Sketch);
    const std::string Label = "Delete " + std::to_string(Points.size()) + " point(s) of " + Owner;
    SketchModel       Edited;
    std::string       Problem;
    int               Erased = 0;
    if (const FeatureStep* Step = FindStep(Document, Sketch); !Step || Step->Kind != FeatureStepKind::Sketch) Problem = "no sketch labelled " + Sketch;
    else if (Points.empty()) Problem = "pick sketch points first (mode 1)";
    else
    {
        Edited = SolvedSketch(Document, *Step);
        for (int Point : Points) if (Point < 0 || Point >= static_cast<int>(Edited.Points.size())) Problem = "point " + std::to_string(Point) + " is not in " + Owner;
        // Disconnect only the selected spline controls; do not erase the whole incident spline.
        int Changed=0;
        if(Problem.empty()) for(auto& Curve:Edited.Curves) if(Curve.Kind==SketchCurveKind::Spline)
        {
            const auto Old=Curve.Points;
            std::erase_if(Curve.Points,[&](int I){return std::find(Points.begin(),Points.end(),I)!=Points.end();});
            if(Curve.Points!=Old)
            {
                if(Curve.Points.size()<2) {Problem="A spline needs at least two controls; no object was deleted";break;}
                Curve.Start=-1;Curve.End=-1;Curve.Centre=-1;Curve.Knots.clear();++Changed;
            }
        }
        if (Problem.empty()) Erased = EraseSketchPoints(Edited, Points)+Changed;
        if (Problem.empty() && Erased == 0) Problem = "no curve runs through the picked point(s)";
    }
    const bool Referenced = SketcherSketchReferencedCondition(Document, Sketch);
    // ① In place: the sketch itself changes (a step reading it follows when it still regenerates).
    SketcherCommandOutcome InPlace = RunCommand(Document, Label, false, [&]() -> std::string
    {
        if (!Problem.empty()) return Problem;
        if (Edited.Curves.empty())
        {
            RetireSketcherCurve(Document, Sketch);
            return {};
        }
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Sketch; });
        Found->Sketch = Edited;
        return {};
    }, 0);
    if (InPlace.AcceptedCondition || !Referenced || !Problem.empty()) return InPlace;

    // ② Detached: the original stays for the steps that read it (off the scene); the remaining curves become a free sketch.
    std::string Stem = Owner;
    while (!Stem.empty() && std::isdigit(static_cast<unsigned char>(Stem.back()))) Stem.pop_back();
    if (Stem.empty()) Stem = "Sketch";
    const std::string Copy = NextCaption(Document, Stem);
    return RunCommand(Document, Label + " into " + Copy + " (" + Owner + " kept for the steps that read it)", false, [&]() -> std::string
    {
        const auto Found = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(), [&](const FeatureStep& Step) { return Step.Label == Sketch; });
        if (Found == Document.Tree.Steps.end()) return "no sketch labelled " + Sketch;
        FeatureStep Detached = *Found;
        RetireSketcherCurve(Document, Sketch);
        Detached.Label  = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        Detached.Sketch = Edited;
        Document.Captions[Detached.Label] = Copy;
        Document.Tree.Steps.push_back(std::move(Detached));
        return {};
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                    PARAMETRIC VARIABLES, LIVE BOOLEANS & MODIFIER STACK
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] bool ValidVariableIdentifier(const std::string& Name) noexcept
    {
        if (Name.empty() || Name.find('.') != std::string::npos) return false;
        const unsigned char First = static_cast<unsigned char>(Name.front());
        if (!(std::isalpha(First) || First == '_')) return false;
        return std::all_of(Name.begin(), Name.end(), [](unsigned char Ch) { return std::isalnum(Ch) || Ch == '_'; });
    }

    [[nodiscard]] FeatureValue* ResolveMutableStepField(FeatureStep& Step, const std::string& Field)
    {
        if (Field == "Size")         return &Step.Size;
        if (Field == "EndSize")      { if (!Step.EndSize) Step.EndSize = Step.Size; return &*Step.EndSize; }
        if (Field == "Extent")       return &Step.Extent;
        if (Field == "Start")        return &Step.Start;
        if (Field == "Elevation")    return &Step.Elevation;
        if (Field == "Angle")        return &Step.Angle;
        if (Field == "Amount")       return &Step.Amount;
        if (Field == "Offset")       return &Step.Offset;
        if (Field == "Tolerance")    return &Step.Tolerance;
        if (Field == "Joint.Offset") return &Step.Joint.Offset;
        if (Field == "Joint.Angle")  return &Step.Joint.Angle;
        if (Field == "Joint.Slide")  return &Step.Joint.Slide;
        return nullptr;
    }

    template <typename Visitor>
    void VisitAllFeatureValues(FeatureTree& Tree, Visitor&& Visit)
    {
        for (FeatureStep& Step : Tree.Steps)
        {
            for (FeatureValue* Ptr : { &Step.Elevation, &Step.Extent, &Step.Start, &Step.Angle, &Step.Size, &Step.Amount, &Step.Tolerance, &Step.Offset,
                                       &Step.Joint.Angle, &Step.Joint.Slide, &Step.Joint.SecondSlide, &Step.Joint.Pitch, &Step.Joint.Roll, &Step.Joint.Offset })
                Visit(*Ptr);
            if (Step.EndSize) Visit(*Step.EndSize);
            for (FeatureValue& Anchor : Step.AnchorSizes) Visit(Anchor);
        }
        for (FeatureEdgeGroup& Group : Tree.EdgeGroups)
            for (EdgeWeightEntry& Entry : Group.Entries) Visit(Entry.Weight);
        for (FeatureModifier& Modifier : Tree.Modifiers)
        {
            for (FeatureValue* Ptr : { &Modifier.Offset, &Modifier.Size, &Modifier.Angle, &Modifier.Count, &Modifier.SecondaryCount,
                                       &Modifier.Distance, &Modifier.SecondaryDistance, &Modifier.Margin })
                Visit(*Ptr);
        }
    }

    [[nodiscard]] std::string ModifierRefusalOf(const SketcherDocument& Document, const std::string& Label)
    {
        for (const FeatureModifierReport& Report : Document.Regeneration.Modifiers)
            if (Report.Label == Label && Report.Status == FeatureStepStatus::Refused) return Report.Label + ": " + Report.Refusal;
        return {};
    }

    void PruneUnusedLiveCutters(SketcherDocument& Document)
    {
        std::erase_if(Document.Appearance.LiveCutters, [&](const std::string& Key)
        {
            return std::none_of(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(),
                                [&](const FeatureModifier& Mod) { return Mod.Kind == ModifierKind::Boolean && Mod.KeepToolCondition && Mod.Tool == Key; });
        });
    }
} // namespace

bool SketcherUserVariableCondition(const FeatureParameter& Parameter) noexcept
{
    return Parameter.Name.find('.') == std::string::npos;
}

SketcherCommandOutcome CreateSketcherVariable(SketcherDocument& Document, const std::string& Name, double Value,
                                              double Low, double High, const std::string& Description)
{
    return RunCommand(Document, "Variable " + Name + " = " + FormatAmount(Value), false, [&]() -> std::string
    {
        if (!ValidVariableIdentifier(Name)) return "variable name '" + Name + "' must be an identifier (letters, digits, '_', starting with a letter or '_')";
        if (!std::isfinite(Value) || !std::isfinite(Low) || !std::isfinite(High)) return "variable value and bounds must be finite";
        for (const FeatureParameter& Existing : Document.Tree.Parameters)
            if (Existing.Name == Name) return "a variable or parameter named '" + Name + "' already exists";
        const double SafeLow  = std::min({ Low, High, Value });
        const double SafeHigh = std::max({ Low, High, Value, SafeLow + 1.0e-6 });
        Document.Tree.Parameters.push_back({ Name, Value, SafeLow, SafeHigh, Description });
        return {};
    });
}

SketcherCommandOutcome DeleteSketcherVariable(SketcherDocument& Document, const std::string& Name)
{
    return RunCommand(Document, "Delete variable " + Name, false, [&]() -> std::string
    {
        const auto Found = std::find_if(Document.Tree.Parameters.begin(), Document.Tree.Parameters.end(),
                                        [&](const FeatureParameter& Param) { return Param.Name == Name; });
        if (Found == Document.Tree.Parameters.end()) return "no variable named '" + Name + "'";
        // Bake current evaluated values into any bound fields so deleting a variable preserves exact current geometry.
        VisitAllFeatureValues(Document.Tree, [&](FeatureValue& Val)
        {
            if (Val.Parameter == Name || Val.SecondParameter == Name)
            {
                const double Current = EvaluateFeatureValue(Document.Tree, Val);
                Val = FeatureValue{ std::isfinite(Current) ? Current : Val.Literal };
            }
        });
        for (FeatureStep& Step : Document.Tree.Steps)
            for (SketchConstraint& Constraint : Step.Sketch.Constraints)
                if (Constraint.Parameter == Name) { Constraint.Value = Found->Value; Constraint.Parameter.clear(); }
        Document.Tree.Parameters.erase(Found);
        return {};
    });
}

SketcherCommandOutcome RenameSketcherVariable(SketcherDocument& Document, const std::string& OldName, const std::string& NewName)
{
    return RunCommand(Document, "Rename variable " + OldName + " → " + NewName, false, [&]() -> std::string
    {
        if (!ValidVariableIdentifier(NewName)) return "variable name '" + NewName + "' must be a valid identifier";
        const auto Found = std::find_if(Document.Tree.Parameters.begin(), Document.Tree.Parameters.end(),
                                        [&](const FeatureParameter& Param) { return Param.Name == OldName; });
        if (Found == Document.Tree.Parameters.end()) return "no variable named '" + OldName + "'";
        if (OldName == NewName) return {};
        for (const FeatureParameter& Existing : Document.Tree.Parameters)
            if (Existing.Name == NewName) return "a variable named '" + NewName + "' already exists";
        Found->Name = NewName;
        VisitAllFeatureValues(Document.Tree, [&](FeatureValue& Val)
        {
            if (Val.Parameter == OldName) Val.Parameter = NewName;
            if (Val.SecondParameter == OldName) Val.SecondParameter = NewName;
        });
        for (FeatureStep& Step : Document.Tree.Steps)
            for (SketchConstraint& Constraint : Step.Sketch.Constraints)
                if (Constraint.Parameter == OldName) Constraint.Parameter = NewName;
        return {};
    });
}

SketcherCommandOutcome BindSketcherStepValue(SketcherDocument& Document, const std::string& StepLabel, const std::string& Field,
                                             const std::string& ParameterName, double Factor, double Offset)
{
    const std::string Caption = ParameterName.empty() ? ("Unbind " + StepLabel + "." + Field)
                                                      : ("Bind " + StepLabel + "." + Field + " ← " + ParameterName);
    return RunCommand(Document, Caption, false, [&]() -> std::string
    {
        const auto StepIt = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(),
                                         [&](const FeatureStep& Step) { return Step.Label == StepLabel; });
        if (StepIt == Document.Tree.Steps.end()) return "no step labelled '" + StepLabel + "'";
        FeatureValue* Target = ResolveMutableStepField(*StepIt, Field);
        if (!Target) return "step field '" + Field + "' cannot be bound";
        if (ParameterName.empty())
        {
            const double Current = EvaluateFeatureValue(Document.Tree, *Target);
            *Target = FeatureValue{ std::isfinite(Current) ? Current : Target->Literal };
            return {};
        }
        const auto ParamIt = std::find_if(Document.Tree.Parameters.begin(), Document.Tree.Parameters.end(),
                                          [&](const FeatureParameter& Param) { return Param.Name == ParameterName; });
        if (ParamIt == Document.Tree.Parameters.end()) return "no variable or parameter named '" + ParameterName + "'";
        Target->Literal         = ParamIt->Value * Factor + Offset;
        Target->Parameter       = ParameterName;
        Target->Factor          = Factor;
        Target->Offset          = Offset;
        Target->SecondParameter.clear();
        Target->SecondFactor    = 0.0;
        return {};
    }, 0);
}

SketcherCommandOutcome SaveStepValueAsVariable(SketcherDocument& Document, const std::string& StepLabel, const std::string& Field,
                                               const std::string& VariableName)
{
    return RunCommand(Document, "Save " + StepLabel + "." + Field + " as " + VariableName, false, [&]() -> std::string
    {
        if (!ValidVariableIdentifier(VariableName)) return "variable name '" + VariableName + "' must be a valid identifier";
        const auto StepIt = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(),
                                         [&](const FeatureStep& Step) { return Step.Label == StepLabel; });
        if (StepIt == Document.Tree.Steps.end()) return "no step labelled '" + StepLabel + "'";
        FeatureValue* Target = ResolveMutableStepField(*StepIt, Field);
        if (!Target) return "step field '" + Field + "' is unknown";
        const double Current = EvaluateFeatureValue(Document.Tree, *Target);
        if (!std::isfinite(Current)) return "current value of " + Field + " is not finite";
        const auto ParamIt = std::find_if(Document.Tree.Parameters.begin(), Document.Tree.Parameters.end(),
                                          [&](const FeatureParameter& Param) { return Param.Name == VariableName; });
        if (ParamIt == Document.Tree.Parameters.end())
        {
            const double Span = std::max(10.0, std::fabs(Current) * 4.0);
            Document.Tree.Parameters.push_back({ VariableName, Current, Current < 0.0 ? Current - Span : 0.0, Current + Span,
                                                 CaptionOrLabel(Document, StepLabel) + " " + Field });
        }
        else ParamIt->Value = Current;
        *Target = ParameterValue(VariableName);
        Target->Literal = Current;
        return {};
    }, 0);
}

SketcherCommandOutcome SetSketcherStepOffsetAxis(SketcherDocument& Document, const std::string& StepLabel, Vector3 Axis)
{
    return RunCommand(Document, StepLabel + " offset axis", false, [&]() -> std::string
    {
        if (Length(Axis) <= 1.0e-12) return "offset axis must be non-zero";
        const auto StepIt = std::find_if(Document.Tree.Steps.begin(), Document.Tree.Steps.end(),
                                         [&](const FeatureStep& Step) { return Step.Label == StepLabel; });
        if (StepIt == Document.Tree.Steps.end()) return "no step labelled '" + StepLabel + "'";
        StepIt->OffsetAxis = Normalize(Axis);
        return {};
    }, 0);
}

bool SketcherBodyReferencedByModifiersCondition(const SketcherDocument& Document, const std::string& BodyKey) noexcept
{
    if (BodyKey.empty()) return false;
    return std::any_of(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(), [&](const FeatureModifier& Modifier)
    {
        if (!Modifier.EnabledCondition) return false;
        const std::string Target = Modifier.Target.empty() ? Document.Regeneration.Active : Modifier.Target;
        return Target == BodyKey || Modifier.Tool == BodyKey;
    });
}

SketcherCommandOutcome AddSketcherLiveBoolean(SketcherDocument& Document, const std::string& Target, const std::string& Tool,
                                              FeatureCombination Combination, Vector3 OffsetAxis, double Offset)
{
    const std::string HostKey = Target.empty() ? Document.Regeneration.Active : Target;
    const std::string Label   = "Live " + std::string(FeatureCombinationLabel(Combination)) + " · " +
                                SketcherBodyCaption(Document, Tool) + " → " + SketcherBodyCaption(Document, HostKey);
    SketcherHistoryEntry Before = Snapshot(Document, Label);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Label;
    if (HostKey.empty() || !FindRegeneratedBody(Document, HostKey)) Outcome.Refusal = "target body not found";
    else if (Tool.empty() || !FindRegeneratedBody(Document, Tool))  Outcome.Refusal = "cutter body not found";
    else if (HostKey == Tool)                                       Outcome.Refusal = "a body cannot Live Boolean with itself";
    else if (Combination != FeatureCombination::Difference && Combination != FeatureCombination::Union && Combination != FeatureCombination::Intersection)
        Outcome.Refusal = "Live Boolean requires Difference, Union or Intersection";
    if (!Outcome.Refusal.empty())
    {
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }

    FeatureModifier Modifier;
    Modifier.Kind              = ModifierKind::Boolean;
    Modifier.Label             = "Boolean#" + std::to_string(NextStepOrdinal(Document.Tree));
    Modifier.Target            = HostKey;
    Modifier.Tool              = Tool;
    Modifier.Combination       = Combination;
    Modifier.Offset            = FeatureValue{ Offset };
    Modifier.OffsetAxis        = Length(OffsetAxis) > 1.0e-12 ? Normalize(OffsetAxis) : Vector3{ 0.0, 0.0, 1.0 };
    Modifier.KeepToolCondition = true;
    const std::string ModLabel = Modifier.Label;
    Document.Captions[ModLabel] = NextCaption(Document, "LiveBool");
    Document.Tree.Modifiers.push_back(std::move(Modifier));
    Document.Appearance.LiveCutters.insert(Tool);

    RegenerateSketcherDocumentDeferred(Document, true);
    Outcome.Refusal = ModifierRefusalOf(Document, ModLabel);
    if (!Outcome.Refusal.empty())
    {
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    SummarizeSketcherDocument(Document);
    Document.Selection         = {};
    Document.Selection.Bodies  = { Tool };
    Document.Selection.LastKind = SketcherSelectionKind::Body;
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome KnifeSketcherLiveInsert(SketcherDocument& Document, const SketcherInsertPlacement& Placement)
{
    const std::string Caption = NextCaption(Document, "LiveKnife");
    const std::string Label   = Caption + " · " + Placement.Name + " · Live " + FeatureCombinationLabel(Placement.Combination);
    SketcherHistoryEntry Before = Snapshot(Document, Label);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Label;
    if (Placement.Exchange.empty()) Outcome.Refusal = "the insert " + Placement.Name + " holds no solid";
    else if (!(Dot(Placement.Map.ColumnX, Cross(Placement.Map.ColumnY, Placement.Map.ColumnZ)) > 1.0e-15))
        Outcome.Refusal = "the insert placement is degenerate (zero size or mirrored)";
    else if (Placement.Target.empty() || !FindRegeneratedBody(Document, Placement.Target))
        Outcome.Refusal = Placement.Target.empty() ? "place the insert on a body's face" : ("no body named " + Placement.Target);
    else if (Placement.Combination != FeatureCombination::Difference && Placement.Combination != FeatureCombination::Union &&
             Placement.Combination != FeatureCombination::Intersection)
        Outcome.Refusal = "Live Knife requires Difference, Union or Intersection";
    if (!Outcome.Refusal.empty()) { Outcome.Milliseconds = ElapsedMs(Start); return Outcome; }

    FeatureStep Import;
    Import.Kind         = FeatureStepKind::Import;
    Import.Label        = "Import#" + std::to_string(NextStepOrdinal(Document.Tree));
    Import.Exchange     = Placement.Exchange;
    Import.ExchangeUnit = SketcherModelUnitMillimetres;
    const std::string CutterKey = Import.Label;
    Document.Captions[CutterKey] = NextCaption(Document, Placement.Name.empty() ? std::string("Cutter") : Placement.Name);
    Document.Tree.Steps.push_back(std::move(Import));

    FeatureStep Place;
    Place.Kind       = FeatureStepKind::Transform;
    Place.Label      = "Transform#" + std::to_string(NextStepOrdinal(Document.Tree));
    Place.Target     = CutterKey;
    Place.Placement  = Placement.Map;
    Place.OffsetAxis = Length(Placement.Map.ColumnZ) > 1.0e-12 ? Normalize(Placement.Map.ColumnZ) : Vector3{ 0.0, 0.0, 1.0 };
    Document.Captions[Place.Label] = Caption + ".Place";
    Document.Tree.Steps.push_back(std::move(Place));

    FeatureModifier Modifier;
    Modifier.Kind              = ModifierKind::Boolean;
    Modifier.Label             = "Boolean#" + std::to_string(NextStepOrdinal(Document.Tree));
    Modifier.Target            = Placement.Target;
    Modifier.Tool              = CutterKey;
    Modifier.Combination       = Placement.Combination;
    Modifier.OffsetAxis        = Place.OffsetAxis;
    Modifier.KeepToolCondition = true;
    const std::string ModLabel = Modifier.Label;
    Document.Captions[ModLabel] = Caption;
    Document.Tree.Modifiers.push_back(std::move(Modifier));
    Document.Appearance.LiveCutters.insert(CutterKey);

    RegenerateSketcherDocumentDeferred(Document, true);
    Outcome.Refusal = ModifierRefusalOf(Document, ModLabel);
    if (Outcome.Refusal.empty())
    {
        for (size_t Index = Before.Tree.Steps.size(); Index < Document.Regeneration.Steps.size(); ++Index)
            if (Document.Regeneration.Steps[Index].Status == FeatureStepStatus::Refused)
                Outcome.Refusal = Document.Regeneration.Steps[Index].Label + ": " + Document.Regeneration.Steps[Index].Refusal;
    }
    if (!Outcome.Refusal.empty())
    {
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    SummarizeSketcherDocument(Document);
    Document.Selection          = {};
    Document.Selection.Bodies   = { CutterKey };
    Document.Selection.LastKind = SketcherSelectionKind::Body;
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome ToggleSketcherLiveCutter(SketcherDocument& Document, const std::string& BodyKey)
{
    const bool WasCutter = Document.Appearance.LiveCutters.contains(BodyKey);
    const std::string Label = (WasCutter ? "Solid cutter " : "Semitransparent cutter ") + SketcherBodyCaption(Document, BodyKey);
    SketcherHistoryEntry Before = Snapshot(Document, Label);
    Before.AppearanceCondition  = true;
    if (!FindRegeneratedBody(Document, BodyKey))
    {
        SketcherCommandOutcome Outcome;
        Outcome.Label   = Label;
        Outcome.Refusal = "no body named " + BodyKey;
        return Outcome;
    }
    if (WasCutter) Document.Appearance.LiveCutters.erase(BodyKey);
    else           Document.Appearance.LiveCutters.insert(BodyKey);
    Document.SummaryCheckpoint.reset();
    SummarizeSketcherDocument(Document);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    SketcherCommandOutcome Outcome;
    Outcome.AcceptedCondition = true;
    Outcome.Label             = Label;
    return Outcome;
}

const char* SketcherCompoundCutterLabel(SketcherCompoundCutterKind Kind) noexcept
{
    switch (Kind)
    {
    case SketcherCompoundCutterKind::Countersink:   return "Countersink";
    case SketcherCompoundCutterKind::Counterbore:   return "Counterbore";
    case SketcherCompoundCutterKind::SteppedHole:   return "Stepped hole";
    case SketcherCompoundCutterKind::HexSocket:     return "Hex socket";
    case SketcherCompoundCutterKind::SteppedSlot:   return "Stepped slot";
    case SketcherCompoundCutterKind::SteppedPocket: return "Stepped pocket";
    }
    return "Compound cutter";
}

SketcherCommandOutcome KnifeSketcherCompoundCutter(SketcherDocument& Document, const SketcherCompoundCutterSpec& Spec)
{
    const char*       KindName  = SketcherCompoundCutterLabel(Spec.Kind);
    const bool        Live      = Spec.LiveCondition && Spec.Combination != FeatureCombination::Separate;
    const std::string Stem      = Live ? "LiveKnife" : "Knife";
    const std::string Caption   = NextCaption(Document, Stem);
    const std::string Label     = Caption + " · " + KindName + " · " + (Live ? "Live " : "") + FeatureCombinationLabel(Spec.Combination);
    SketcherHistoryEntry Before = Snapshot(Document, Label);
    const auto Start            = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Label;

    if (!(Length(Spec.Spot.ZAxis) > 1.0e-9)) Outcome.Refusal = "the cutter face frame has zero normal";
    else if (!(Spec.OuterRadius > 1.0e-4) || !(Spec.OuterDepth > 1.0e-4)) Outcome.Refusal = "Stage 1 radius and depth must be positive";
    else if (!(Spec.InnerRadius > 1.0e-4) || !(Spec.InnerDepth > 1.0e-4)) Outcome.Refusal = "Stage 2 radius and depth must be positive";
    else if (!(Spec.InnerRadius < Spec.OuterRadius - 1.0e-5)) Outcome.Refusal = "Stage 2 radius must sit inside Stage 1 radius";
    else if (Spec.Combination != FeatureCombination::Separate)
    {
        if (Spec.Combination != FeatureCombination::Difference && Spec.Combination != FeatureCombination::Union &&
            Spec.Combination != FeatureCombination::Intersection)
            Outcome.Refusal = "choose Difference, Union, Intersection or Separate";
        else if (Spec.Target.empty() || !FindRegeneratedBody(Document, Spec.Target))
            Outcome.Refusal = Spec.Target.empty() ? "place the compound cutter on a body face" : ("no body named " + Spec.Target);
    }
    if (!Outcome.Refusal.empty()) { Outcome.Milliseconds = ElapsedMs(Start); return Outcome; }

    const Vector3 Normal = Normalize(Spec.Spot.ZAxis);
    Vector3       BaseX  = Spec.Spot.XAxis - Normal * Dot(Spec.Spot.XAxis, Normal);
    if (!(Length(BaseX) > 1.0e-9))
        BaseX = std::abs(Normal.Z) < 0.9 ? Cross(Vector3{ 0.0, 0.0, 1.0 }, Normal) : Cross(Vector3{ 1.0, 0.0, 0.0 }, Normal);
    BaseX = Normalize(BaseX);
    const Vector3 BaseY  = Normalize(Cross(Normal, BaseX));
    const Vector3 Across = Normalize(BaseX * std::cos(Spec.Turn) + BaseY * std::sin(Spec.Turn));
    const bool    Boss   = Spec.Combination == FeatureCombination::Union;
    const Vector3 Dir    = Boss ? Normal : (Normal * -1.0);                                  // Axial plunge direction (Stage 1 -> Stage 2)
    const double  R1     = Spec.OuterRadius;
    const double  D1     = Spec.OuterDepth;
    const double  R2     = std::min(Spec.InnerRadius, R1 - 1.0e-4);
    const double  D2     = Spec.InnerDepth;
    const double  Lift   = Boss ? std::min(0.008, 0.25 * D1) : std::max(0.008, 0.15 * R1);

    std::string CutterStem;
    for (const char Ch : std::string(KindName)) if (std::isalnum(static_cast<unsigned char>(Ch))) CutterStem.push_back(Ch);
    const std::string CutterCaption = NextCaption(Document, CutterStem.empty() ? "Cutter" : CutterStem);
    std::string       CutterKey;

    const bool Revolved = Spec.Kind == SketcherCompoundCutterKind::Countersink ||
                          Spec.Kind == SketcherCompoundCutterKind::Counterbore ||
                          Spec.Kind == SketcherCompoundCutterKind::SteppedHole;
    if (Revolved)
    {
        FeatureStep Meridian;
        Meridian.Kind          = FeatureStepKind::Sketch;
        Meridian.Label         = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        Meridian.FillCondition = true;
        Meridian.Plane.Origin  = Spec.Spot.Origin;
        Meridian.Plane.XAxis   = Across;
        Meridian.Plane.YAxis   = Dir;
        Meridian.Plane.ZAxis   = Normalize(Cross(Meridian.Plane.XAxis, Meridian.Plane.YAxis));

        SketchModel& Sketch = Meridian.Sketch;
        const int P0 = AppendSketchPoint(Sketch, { 0.0, -Lift });
        const int P1 = AppendSketchPoint(Sketch, { R1,  -Lift });
        int P2 = -1, P3 = -1, P4 = -1, P5 = -1;
        if (Spec.Kind == SketcherCompoundCutterKind::Countersink)
        {
            P2 = AppendSketchPoint(Sketch, { R1, 0.0 });
            P3 = AppendSketchPoint(Sketch, { R2, D1 });
            P4 = AppendSketchPoint(Sketch, { R2, D1 + D2 });
            P5 = AppendSketchPoint(Sketch, { 0.0, D1 + D2 });
        }
        else if (Spec.Kind == SketcherCompoundCutterKind::Counterbore)
        {
            P2 = AppendSketchPoint(Sketch, { R1, D1 });
            P3 = AppendSketchPoint(Sketch, { R2, D1 });
            P4 = AppendSketchPoint(Sketch, { R2, D1 + D2 });
            P5 = AppendSketchPoint(Sketch, { 0.0, D1 + D2 });
        }
        else
        {
            const double Tip = std::min(0.45 * D2, 0.58 * R2);
            P2 = AppendSketchPoint(Sketch, { R1, D1 });
            P3 = AppendSketchPoint(Sketch, { R2, D1 });
            P4 = AppendSketchPoint(Sketch, { R2, Tip > 1.0e-4 ? (D1 + D2 - Tip) : (D1 + D2) });
            P5 = AppendSketchPoint(Sketch, { 0.0, D1 + D2 });
        }
        AppendSketchLine(Sketch, P5, P0, 1);                                                 // Curve 1 = revolution axis u = 0
        AppendSketchLine(Sketch, P0, P1, 2);
        AppendSketchLine(Sketch, P1, P2, 3);
        AppendSketchLine(Sketch, P2, P3, 4);
        AppendSketchLine(Sketch, P3, P4, 5);
        AppendSketchLine(Sketch, P4, P5, 6);

        const std::string MeridianLabel = Meridian.Label;
        Document.Captions[MeridianLabel] = CutterCaption + ".Meridian";
        Document.Appearance.RemovedCurves.insert(MeridianLabel);
        Document.Appearance.HiddenCurves.insert(MeridianLabel);
        Document.Tree.Steps.push_back(std::move(Meridian));

        FeatureStep Sweep;
        Sweep.Kind           = FeatureStepKind::Revolve;
        Sweep.Label          = "Revolve#" + std::to_string(NextStepOrdinal(Document.Tree));
        Sweep.Profile        = MeridianLabel;
        Sweep.AxisIdentifier = 1;
        Sweep.Angle          = LiteralValue(2.0 * Pi);
        Sweep.Combination    = !Document.Summary.PresentCondition && Spec.Combination == FeatureCombination::Separate ? FeatureCombination::New : FeatureCombination::Separate;
        CutterKey            = Sweep.Label;
        Document.Captions[CutterKey] = CutterCaption;
        Document.Tree.Steps.push_back(std::move(Sweep));
    }
    else
    {
        const Vector3 FrameY = Normalize(Cross(Dir, Across));                                // Right-handed: Across × FrameY = Dir
        const auto PopulateProfile = [&](SketchModel& Sketch, bool OuterStage)
        {
            const double Rad = OuterStage ? R1 : R2;
            if (OuterStage && Spec.Kind == SketcherCompoundCutterKind::HexSocket)
            {
                std::vector<int> Pts;
                for (int Corner = 0; Corner < 6; ++Corner)
                {
                    const double Angle = Corner * Pi / 3.0;
                    Pts.push_back(AppendSketchPoint(Sketch, { Rad * std::cos(Angle), Rad * std::sin(Angle) }));
                }
                for (int Corner = 0; Corner < 6; ++Corner)
                    AppendSketchLine(Sketch, Pts[Corner], Pts[(Corner + 1) % 6], Corner + 1);
            }
            else if (!OuterStage && Spec.Kind == SketcherCompoundCutterKind::HexSocket)
            {
                const int Centre = AppendSketchPoint(Sketch, { 0.0, 0.0 });
                AppendSketchCircle(Sketch, Centre, Rad, 1);
            }
            else if (Spec.Kind == SketcherCompoundCutterKind::SteppedSlot)
            {
                const double  HalfSpan = 1.15 * R1;                                          // Concentric cap centres across both stages
                const Vector2 First{ -HalfSpan, 0.0 }, Second{ +HalfSpan, 0.0 }, Left{ 0.0, 1.0 };
                const int FirstCentre  = AppendSketchPoint(Sketch, First);
                const int SecondCentre = AppendSketchPoint(Sketch, Second);
                const int TopFirst     = AppendSketchPoint(Sketch, First + Left * Rad);
                const int TopSecond    = AppendSketchPoint(Sketch, Second + Left * Rad);
                const int BotFirst     = AppendSketchPoint(Sketch, First - Left * Rad);
                const int BotSecond    = AppendSketchPoint(Sketch, Second - Left * Rad);
                AppendSketchLine(Sketch, BotFirst, BotSecond, 1);
                AppendSketchArc(Sketch, SecondCentre, BotSecond, TopSecond, 2);
                AppendSketchLine(Sketch, TopSecond, TopFirst, 3);
                AppendSketchArc(Sketch, FirstCentre, TopFirst, BotFirst, 4);
            }
            else
            {
                const int C0 = AppendSketchPoint(Sketch, { -Rad, -Rad });
                const int C1 = AppendSketchPoint(Sketch, { +Rad, -Rad });
                const int C2 = AppendSketchPoint(Sketch, { +Rad, +Rad });
                const int C3 = AppendSketchPoint(Sketch, { -Rad, +Rad });
                AppendSketchLine(Sketch, C0, C1, 1);
                AppendSketchLine(Sketch, C1, C2, 2);
                AppendSketchLine(Sketch, C2, C3, 3);
                AppendSketchLine(Sketch, C3, C0, 4);
            }
        };

        FeatureStep OuterSketch;
        OuterSketch.Kind          = FeatureStepKind::Sketch;
        OuterSketch.Label         = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        OuterSketch.FillCondition = true;
        OuterSketch.Plane         = { Spec.Spot.Origin - Dir * Lift, Across, FrameY, Dir };
        PopulateProfile(OuterSketch.Sketch, true);
        const std::string OuterSketchLabel = OuterSketch.Label;
        Document.Captions[OuterSketchLabel] = CutterCaption + ".Stage1Profile";
        Document.Appearance.RemovedCurves.insert(OuterSketchLabel);
        Document.Appearance.HiddenCurves.insert(OuterSketchLabel);
        Document.Tree.Steps.push_back(std::move(OuterSketch));

        Document.Tree.Parameters.push_back({ CutterCaption + ".OuterDepth", Lift + D1, 0.001, std::max(10.0, 4.0 * (Lift + D1)), "Stage 1 depth (+collar)" });
        FeatureStep OuterExtrude;
        OuterExtrude.Kind        = FeatureStepKind::Extrude;
        OuterExtrude.Label       = "Extrude#" + std::to_string(NextStepOrdinal(Document.Tree));
        OuterExtrude.Profile     = OuterSketchLabel;
        OuterExtrude.Extent      = ParameterValue(CutterCaption + ".OuterDepth");
        OuterExtrude.Combination = !Document.Summary.PresentCondition && Spec.Combination == FeatureCombination::Separate ? FeatureCombination::New : FeatureCombination::Separate;
        CutterKey                = OuterExtrude.Label;
        Document.Captions[CutterKey] = CutterCaption;
        Document.Tree.Steps.push_back(std::move(OuterExtrude));

        const double Overlap = std::min(0.002, 0.25 * D1);
        FeatureStep InnerSketch;
        InnerSketch.Kind          = FeatureStepKind::Sketch;
        InnerSketch.Label         = "Sketch#" + std::to_string(NextStepOrdinal(Document.Tree));
        InnerSketch.FillCondition = true;
        InnerSketch.Plane         = { Spec.Spot.Origin + Dir * (D1 - Overlap), Across, FrameY, Dir };
        PopulateProfile(InnerSketch.Sketch, false);
        const std::string InnerSketchLabel = InnerSketch.Label;
        Document.Captions[InnerSketchLabel] = CutterCaption + ".Stage2Profile";
        Document.Appearance.RemovedCurves.insert(InnerSketchLabel);
        Document.Appearance.HiddenCurves.insert(InnerSketchLabel);
        Document.Tree.Steps.push_back(std::move(InnerSketch));

        Document.Tree.Parameters.push_back({ CutterCaption + ".InnerDepth", D2 + Overlap, 0.001, std::max(10.0, 4.0 * (D2 + Overlap)), "Stage 2 depth" });
        FeatureStep InnerExtrude;
        InnerExtrude.Kind        = FeatureStepKind::Extrude;
        InnerExtrude.Label       = "Extrude#" + std::to_string(NextStepOrdinal(Document.Tree));
        InnerExtrude.Profile     = InnerSketchLabel;
        InnerExtrude.Extent      = ParameterValue(CutterCaption + ".InnerDepth");
        InnerExtrude.Combination = FeatureCombination::Union;
        InnerExtrude.Target      = CutterKey;
        Document.Captions[InnerExtrude.Label] = CutterCaption + ".Stage2";
        Document.Tree.Steps.push_back(std::move(InnerExtrude));
    }

    std::string ModLabel;
    if (Spec.Combination == FeatureCombination::Separate)
    {
        Document.Selection          = {};
        Document.Selection.Bodies   = { CutterKey };
        Document.Selection.LastKind = SketcherSelectionKind::Body;
    }
    else if (Live)
    {
        FeatureModifier Modifier;
        Modifier.Kind              = ModifierKind::Boolean;
        Modifier.Label             = "Boolean#" + std::to_string(NextStepOrdinal(Document.Tree));
        Modifier.Target            = Spec.Target;
        Modifier.Tool              = CutterKey;
        Modifier.Combination       = Spec.Combination;
        Modifier.OffsetAxis        = Normal;
        Modifier.KeepToolCondition = true;
        ModLabel                   = Modifier.Label;
        Document.Captions[ModLabel] = Caption;
        Document.Tree.Modifiers.push_back(std::move(Modifier));
        Document.Appearance.LiveCutters.insert(CutterKey);
        Document.Selection          = {};
        Document.Selection.Bodies   = { CutterKey };
        Document.Selection.LastKind = SketcherSelectionKind::Body;
    }
    else
    {
        FeatureStep Combine;
        Combine.Kind        = FeatureStepKind::Combine;
        Combine.Label       = "Combine#" + std::to_string(NextStepOrdinal(Document.Tree));
        Combine.Combination = Spec.Combination;
        Combine.Target      = Spec.Target;
        Combine.Tool        = CutterKey;
        Combine.OffsetAxis  = Normal;
        Document.Captions[Combine.Label] = Caption;
        Document.Tree.Steps.push_back(std::move(Combine));
        Document.Selection = {};
    }

    RegenerateSketcherDocumentDeferred(Document, true);
    if (!ModLabel.empty()) Outcome.Refusal = ModifierRefusalOf(Document, ModLabel);
    if (Outcome.Refusal.empty())
    {
        for (size_t Index = Before.Tree.Steps.size(); Index < Document.Regeneration.Steps.size(); ++Index)
            if (Document.Regeneration.Steps[Index].Status == FeatureStepStatus::Refused)
                Outcome.Refusal = Document.Regeneration.Steps[Index].Label + ": " + Document.Regeneration.Steps[Index].Refusal;
    }
    if (!Outcome.Refusal.empty())
    {
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    SummarizeSketcherDocument(Document);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome AssignSketcherEdgeWeights(SketcherDocument& Document, const std::string& GroupName,
                                                 const std::vector<std::string>& EdgeNames, FeatureValue Weight)
{
    const std::vector<std::string>& Picks = EdgeNames.empty() ? Document.Selection.Edges : EdgeNames;
    const std::string Label = "Paint " + std::to_string(Picks.size()) + " edge(s) → " + GroupName;
    SketcherHistoryEntry Before = Snapshot(Document, Label);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Label;
    if (GroupName.empty()) Outcome.Refusal = "name the edge group first";
    else if (Picks.empty()) Outcome.Refusal = "select one or more edges first";
    std::string Owner;
    if (Outcome.Refusal.empty()) Outcome.Refusal = CommonBody(Document, SketcherSelectionKind::Edge, Picks, Owner);
    const double EvaluatedWeight = EvaluateFeatureValue(Document.Tree, Weight);
    if (Outcome.Refusal.empty() && !std::isfinite(EvaluatedWeight))
        Outcome.Refusal = "edge weight variable '" + Weight.Parameter + "' is undefined";
    if (!Outcome.Refusal.empty()) { Outcome.Milliseconds = ElapsedMs(Start); return Outcome; }

    auto GroupIt = std::find_if(Document.Tree.EdgeGroups.begin(), Document.Tree.EdgeGroups.end(),
                                [&](const FeatureEdgeGroup& Group) { return Group.Name == GroupName; });
    if (GroupIt == Document.Tree.EdgeGroups.end())
    {
        Document.Tree.EdgeGroups.push_back({ GroupName, Owner, {} });
        GroupIt = std::prev(Document.Tree.EdgeGroups.end());
    }
    else if (!GroupIt->Body.empty() && GroupIt->Body != Owner)
    {
        Restore(Document, std::move(Before));
        Outcome.Refusal      = "edge group '" + GroupName + "' belongs to " + SketcherBodyCaption(Document, GroupIt->Body);
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }

    const bool EraseCondition = Weight.Parameter.empty() && Weight.SecondParameter.empty() && EvaluatedWeight <= 1.0e-9;
    for (const std::string& EdgeName : Picks)
    {
        auto Existing = std::find_if(GroupIt->Entries.begin(), GroupIt->Entries.end(),
                                     [&](const EdgeWeightEntry& Item) { return Item.Edge.Name == EdgeName; });
        if (EraseCondition)
        {
            if (Existing != GroupIt->Entries.end()) GroupIt->Entries.erase(Existing);
            continue;
        }
        if (Existing != GroupIt->Entries.end()) Existing->Weight = Weight;
        else GroupIt->Entries.push_back({ { ReferenceKind::Edge, EdgeName, {} }, Weight });
    }

    RegenerateSketcherDocumentDeferred(Document, true);
    for (const FeatureModifierReport& Report : Document.Regeneration.Modifiers)
        if (Report.Status == FeatureStepStatus::Refused) Outcome.Refusal = Report.Label + ": " + Report.Refusal;
    if (!Outcome.Refusal.empty())
    {
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    SummarizeSketcherDocument(Document);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome RemoveSketcherEdgeGroup(SketcherDocument& Document, const std::string& GroupName)
{
    return RunCommand(Document, "Delete edge group " + GroupName, false, [&]() -> std::string
    {
        const auto Found = std::find_if(Document.Tree.EdgeGroups.begin(), Document.Tree.EdgeGroups.end(),
                                        [&](const FeatureEdgeGroup& Group) { return Group.Name == GroupName; });
        if (Found == Document.Tree.EdgeGroups.end()) return "no edge group named '" + GroupName + "'";
        Document.Tree.EdgeGroups.erase(Found);
        return {};
    });
}

void SelectSketcherEdgeGroup(SketcherDocument& Document, const std::string& GroupName)
{
    const auto Found = std::find_if(Document.Tree.EdgeGroups.begin(), Document.Tree.EdgeGroups.end(),
                                    [&](const FeatureEdgeGroup& Group) { return Group.Name == GroupName; });
    if (Found == Document.Tree.EdgeGroups.end()) return;
    Document.Selection.Edges.clear();
    for (const EdgeWeightEntry& Entry : Found->Entries) Document.Selection.Edges.push_back(Entry.Edge.Name);
    Document.Selection.PickedBody = Found->Body;
    Document.Selection.LastKind   = SketcherSelectionKind::Edge;
    KeepResolvedNames(Document.Selection.Edges, Document.Regeneration.Bodies, true);
}

SketcherCommandOutcome AddSketcherModifier(SketcherDocument& Document, FeatureModifier Modifier)
{
    if (Modifier.Target.empty())
        Modifier.Target = !Document.Selection.Bodies.empty() ? Document.Selection.Bodies.front()
                        : (!Document.Selection.PickedBody.empty() ? Document.Selection.PickedBody : Document.Regeneration.Active);
    const std::string Stem    = ModifierKindLabel(Modifier.Kind);
    const std::string Caption = NextCaption(Document, Stem);
    if (Modifier.Label.empty()) Modifier.Label = Stem + "#" + std::to_string(NextStepOrdinal(Document.Tree));
    const std::string ModLabel = Modifier.Label;
    const std::string Label    = "Add modifier " + Caption;

    SketcherHistoryEntry Before = Snapshot(Document, Label);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Label;
    Document.Captions[ModLabel] = Caption;
    if (Modifier.Kind == ModifierKind::Boolean && Modifier.KeepToolCondition && !Modifier.Tool.empty())
        Document.Appearance.LiveCutters.insert(Modifier.Tool);
    Document.Tree.Modifiers.push_back(std::move(Modifier));

    RegenerateSketcherDocumentDeferred(Document, true);
    Outcome.Refusal = ModifierRefusalOf(Document, ModLabel);
    if (!Outcome.Refusal.empty())
    {
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    SummarizeSketcherDocument(Document);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome UpdateSketcherModifier(SketcherDocument& Document, const std::string& Label, const FeatureModifier& Updated)
{
    const std::string Caption = "Edit " + CaptionOrLabel(Document, Label);
    SketcherHistoryEntry Before = Snapshot(Document, Caption);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Caption;
    const auto Found = std::find_if(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(),
                                    [&](const FeatureModifier& Mod) { return Mod.Label == Label; });
    if (Found == Document.Tree.Modifiers.end())
    {
        Outcome.Refusal      = "no modifier labelled '" + Label + "'";
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    *Found       = Updated;
    Found->Label = Label;
    if (Found->Kind == ModifierKind::Boolean && Found->KeepToolCondition && !Found->Tool.empty())
        Document.Appearance.LiveCutters.insert(Found->Tool);
    PruneUnusedLiveCutters(Document);

    RegenerateSketcherDocumentDeferred(Document, true);
    Outcome.Refusal = ModifierRefusalOf(Document, Label);
    if (!Outcome.Refusal.empty())
    {
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    SummarizeSketcherDocument(Document);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome MoveSketcherModifier(SketcherDocument& Document, const std::string& Label, int Delta)
{
    const std::string Caption = std::string(Delta < 0 ? "Move up " : "Move down ") + CaptionOrLabel(Document, Label);
    SketcherHistoryEntry Before = Snapshot(Document, Caption);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Caption;
    const auto Found = std::find_if(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(),
                                    [&](const FeatureModifier& Mod) { return Mod.Label == Label; });
    if (Found == Document.Tree.Modifiers.end()) { Outcome.Refusal = "no modifier labelled '" + Label + "'"; return Outcome; }
    const int Index  = static_cast<int>(std::distance(Document.Tree.Modifiers.begin(), Found));
    const int Target = Index + (Delta < 0 ? -1 : 1);
    if (Target < 0 || Target >= static_cast<int>(Document.Tree.Modifiers.size()))
    {
        Outcome.Refusal = "modifier is already at the end of the stack";
        return Outcome;
    }
    std::swap(Document.Tree.Modifiers[static_cast<size_t>(Index)], Document.Tree.Modifiers[static_cast<size_t>(Target)]);
    RegenerateSketcherDocumentDeferred(Document, true);
    for (const FeatureModifierReport& Report : Document.Regeneration.Modifiers)
        if (Report.Status == FeatureStepStatus::Refused) Outcome.Refusal = Report.Label + ": " + Report.Refusal;
    if (!Outcome.Refusal.empty())
    {
        Restore(Document, std::move(Before));
        Outcome.Milliseconds = ElapsedMs(Start);
        return Outcome;
    }
    SummarizeSketcherDocument(Document);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome ToggleSketcherModifier(SketcherDocument& Document, const std::string& Label)
{
    const std::string Caption = "Toggle " + CaptionOrLabel(Document, Label);
    SketcherHistoryEntry Before = Snapshot(Document, Caption);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Caption;
    const auto Found = std::find_if(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(),
                                    [&](const FeatureModifier& Mod) { return Mod.Label == Label; });
    if (Found == Document.Tree.Modifiers.end()) { Outcome.Refusal = "no modifier labelled '" + Label + "'"; return Outcome; }
    Found->EnabledCondition = !Found->EnabledCondition;
    RegenerateSketcherDocument(Document, false);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome RemoveSketcherModifier(SketcherDocument& Document, const std::string& Label)
{
    const std::string Caption = "Remove " + CaptionOrLabel(Document, Label);
    SketcherHistoryEntry Before = Snapshot(Document, Caption);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Caption;
    const auto Found = std::find_if(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(),
                                    [&](const FeatureModifier& Mod) { return Mod.Label == Label; });
    if (Found == Document.Tree.Modifiers.end()) { Outcome.Refusal = "no modifier labelled '" + Label + "'"; return Outcome; }
    Document.Captions.erase(Label);
    Document.Tree.Modifiers.erase(Found);
    PruneUnusedLiveCutters(Document);
    RegenerateSketcherDocument(Document, false);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome ApplySketcherModifier(SketcherDocument& Document, const std::string& Label)
{
    const std::string Caption = "Apply " + CaptionOrLabel(Document, Label);
    SketcherHistoryEntry Before = Snapshot(Document, Caption);
    const auto Start = std::chrono::steady_clock::now();
    SketcherCommandOutcome Outcome;
    Outcome.Label = Caption;
    const auto Found = std::find_if(Document.Tree.Modifiers.begin(), Document.Tree.Modifiers.end(),
                                    [&](const FeatureModifier& Mod) { return Mod.Label == Label; });
    if (Found == Document.Tree.Modifiers.end()) { Outcome.Refusal = "no modifier labelled '" + Label + "'"; return Outcome; }
    const size_t ModIndex = static_cast<size_t>(std::distance(Document.Tree.Modifiers.begin(), Found));
    const FeatureModifier Copy = *Found;
    const std::string TargetKey = Copy.Target.empty() ? Document.Regeneration.Active : Copy.Target;
    for (size_t Earlier = 0; Earlier < ModIndex; ++Earlier)
    {
        const FeatureModifier& Prev = Document.Tree.Modifiers[Earlier];
        const std::string PrevTarget = Prev.Target.empty() ? Document.Regeneration.Active : Prev.Target;
        if (Prev.EnabledCondition && PrevTarget == TargetKey)
        {
            Outcome.Refusal = "apply earlier modifiers on " + SketcherBodyCaption(Document, TargetKey) + " first (top to bottom)";
            return Outcome;
        }
    }

    if (Copy.Kind == ModifierKind::Boolean)
    {
        FeatureStep Combine;
        Combine.Kind        = FeatureStepKind::Combine;
        Combine.Label       = "Combine#" + std::to_string(NextStepOrdinal(Document.Tree));
        Combine.Target      = TargetKey;
        Combine.Tool        = Copy.Tool;
        Combine.Combination = Copy.Combination;
        Combine.Offset      = Copy.Offset;
        Combine.OffsetAxis  = Copy.OffsetAxis;
        Document.Captions[Combine.Label] = CaptionOrLabel(Document, Label);
        Document.Captions.erase(Label);
        Document.Tree.Modifiers.erase(Document.Tree.Modifiers.begin() + static_cast<std::ptrdiff_t>(ModIndex));
        Document.Tree.Steps.push_back(std::move(Combine));
        PruneUnusedLiveCutters(Document);
    }
    else
    {
        // Evaluate tree with just this single modifier to capture its exact B-rep, then embed canonical single-solid STEP(s).
        SketcherDocument Scratch = CreateSketcherDocument(Document.Title);
        Scratch.Tree             = Document.Tree;
        Scratch.Tree.Modifiers   = { Copy };
        RegenerateSketcherDocumentDeferred(Scratch, true);
        const RegeneratedBody* Built = FindRegeneratedBody(Scratch, TargetKey);
        if (!Built || !Built->Body.PresentCondition())
        {
            Outcome.Refusal = "could not evaluate modifier for Apply";
            return Outcome;
        }
        StepRequest ExportReq;
        ExportReq.ModelUnitMillimetres = SketcherModelUnitMillimetres;
        ExportReq.Name                 = SketcherBodyCaption(Document, TargetKey);
        const StepExport Exported      = WriteStep(*Scratch.Partition, { Built->Body }, ExportReq);
        if (!Exported.Refusal.empty()) { Outcome.Refusal = Exported.Refusal; return Outcome; }

        ModelPartition VerifyPartition;
        StepImportRequest ImportReq;
        ImportReq.ModelUnitMillimetres = SketcherModelUnitMillimetres;
        const StepImport Split = ReadStep(VerifyPartition, Exported.Bytes, ImportReq);
        if (!Split.Refusal.empty() || Split.Bodies.empty()) { Outcome.Refusal = Split.Refusal.empty() ? "Apply produced no solids" : Split.Refusal; return Outcome; }

        const std::string BaseCaption = SketcherBodyCaption(Document, TargetKey);
        FeatureStep RemoveOld;
        RemoveOld.Kind   = FeatureStepKind::Remove;
        RemoveOld.Label  = "Remove#" + std::to_string(NextStepOrdinal(Document.Tree));
        RemoveOld.Target = TargetKey;
        Document.Tree.Steps.push_back(std::move(RemoveOld));

        std::string FirstImportedKey;
        for (size_t SolidIndex = 0; SolidIndex < Split.Bodies.size(); ++SolidIndex)
        {
            const StepExport Single = WriteStep(VerifyPartition, { Split.Bodies[SolidIndex] }, ExportReq);
            if (!Single.Refusal.empty()) { Restore(Document, std::move(Before)); Outcome.Refusal = Single.Refusal; return Outcome; }
            FeatureStep Imp;
            Imp.Kind         = FeatureStepKind::Import;
            Imp.Label        = "Import#" + std::to_string(NextStepOrdinal(Document.Tree));
            Imp.Exchange     = Single.Bytes;
            Imp.ExchangeUnit = SketcherModelUnitMillimetres;
            if (FirstImportedKey.empty()) FirstImportedKey = Imp.Label;
            Document.Captions[Imp.Label] = SolidIndex == 0 ? BaseCaption : (BaseCaption + "." + std::to_string(SolidIndex + 1));
            Document.Tree.Steps.push_back(std::move(Imp));
        }
        Document.Captions.erase(Label);
        Document.Tree.Modifiers.erase(Document.Tree.Modifiers.begin() + static_cast<std::ptrdiff_t>(ModIndex));
        for (FeatureModifier& Remaining : Document.Tree.Modifiers)
            if (Remaining.Target == TargetKey) Remaining.Target = FirstImportedKey;
    }

    RegenerateSketcherDocument(Document, true);
    Document.UndoEntries.push_back(std::move(Before));
    Document.RedoEntries.clear();
    TrimHistoryReplay(Document);
    Outcome.AcceptedCondition = true;
    Outcome.Milliseconds      = ElapsedMs(Start);
    return Outcome;
}

SketcherCommandOutcome RestoreSketcherVisibility(SketcherDocument& Document, const std::set<std::string>& HiddenBodies,
                                                 const std::set<std::string>& HiddenCurves, const std::string& Label)
{
    return RunAppearanceCommand(Document, Label, [&]() -> std::string
    {
        std::set<std::string> PresentBodies;
        for (const RegeneratedBody& Body : Document.Regeneration.Bodies) PresentBodies.insert(Body.Key);
        std::set<std::string> FilteredBodies;
        for (const std::string& Key : HiddenBodies) if (PresentBodies.contains(Key)) FilteredBodies.insert(Key);

        std::set<std::string> FilteredCurves = Document.Appearance.RemovedCurves;            // RemovedCurves ⊂ HiddenCurves invariant
        for (const FeatureStep& Step : Document.Tree.Steps)
            if ((Step.Kind == FeatureStepKind::Sketch || Step.Kind == FeatureStepKind::SpaceCurve) && HiddenCurves.contains(Step.Label))
                FilteredCurves.insert(Step.Label);

        if (Document.Appearance.HiddenBodies == FilteredBodies && Document.Appearance.HiddenCurves == FilteredCurves)
            return "visibility is already restored";
        Document.Appearance.HiddenBodies = std::move(FilteredBodies);
        Document.Appearance.HiddenCurves = std::move(FilteredCurves);
        return {};
    });
}

namespace
{

struct ResolvedDimensionOperand
{
    bool                        FoundCondition  = false;
    MeasureTarget               Target          = {};
    bool                        TargetCondition = false;
    Vector3                     Point           = {};
    Vector3                     SecondEndpoint  = {};
    bool                        SegmentCondition = false;
    double                      EdgeSpan        = 0.0;
    bool                        RadialCondition = false;
    Vector3                     Centre          = {};
    Vector3                     Rim             = {};
    double                      Radius          = 0.0;
    Vector3                     PlaneNormal     = { 0.0, 0.0, 1.0 };
    std::optional<Vector3>      LineDir         = {};
    std::optional<Vector3>      FaceNormal      = {};
};

[[nodiscard]] Vector3 SafeUnitNormal(Vector3 Candidate, Vector3 Fallback = { 0.0, 0.0, 1.0 })
{
    const double Norm = Length(Candidate);
    if (Norm > 1.0e-9) return Candidate * (1.0 / Norm);
    const double FallNorm = Length(Fallback);
    return FallNorm > 1.0e-9 ? Fallback * (1.0 / FallNorm) : Vector3{ 0.0, 0.0, 1.0 };
}

[[nodiscard]] Vector3 DefaultPlaneNormalForSpan(Vector3 Span)
{
    const Vector3 UnitSpan = SafeUnitNormal(Span, { 1.0, 0.0, 0.0 });
    // Prefer horizontal XY witness lines unless the span itself is nearly vertical along Z.
    if (std::abs(UnitSpan.Z) < 0.92) return { 0.0, 0.0, 1.0 };
    return std::abs(UnitSpan.Y) < 0.92 ? Vector3{ 0.0, -1.0, 0.0 } : Vector3{ 1.0, 0.0, 0.0 };
}

[[nodiscard]] bool ComputeBodyBounds(const SketcherDocument& Document, const std::string& BodyKey, Vector3& Low, Vector3& High)
{
    if (!Document.Partition) return false;
    const RegeneratedBody* Body = FindRegeneratedBody(Document, BodyKey);
    if (!Body || !Body->Body.PresentCondition()) return false;
    const ModelPartition& Partition = *Document.Partition;
    const BodyInventory   Inventory = Partition.CollectInventory(Body->Body);
    bool Any = false;
    const auto Include = [&](Vector3 Point)
    {
        if (!std::isfinite(Point.X) || !std::isfinite(Point.Y) || !std::isfinite(Point.Z)) return;
        if (!Any) { Low = High = Point; Any = true; return; }
        Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
        High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
    };
    for (const VertexToken Vertex : Inventory.Vertices) Include(Partition.Vertices.Resolve(Vertex).Position);
    for (const EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        if (!Profile.Curve.PresentCondition()) continue;
        const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
        for (int Step = 0; Step <= 24; ++Step)
        {
            const double Parameter = Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * (double(Step) / 24.0);
            Include(EvaluateCurve(Curve, Parameter).Position);
        }
    }
    return Any;
}

[[nodiscard]] bool ParseTrailingDimensionIndex(const std::string& Text, int& OutIndex)
{
    if (Text.empty()) return false;
    int  Sign = 1;
    size_t Pos = 0;
    if (Text[0] == '-') { Sign = -1; Pos = 1; }
    if (Pos >= Text.size()) return false;
    int Value = 0;
    for (; Pos < Text.size(); ++Pos)
    {
        if (!std::isdigit(static_cast<unsigned char>(Text[Pos]))) return false;
        Value = Value * 10 + (Text[Pos] - '0');
    }
    OutIndex = Sign * Value;
    return true;
}

[[nodiscard]] ResolvedDimensionOperand ResolveDimensionOperand(const SketcherDocument& Document, SketcherSelectionKind Kind, const std::string& Name)
{
    ResolvedDimensionOperand Out;
    if (Name.empty()) return Out;

    // 1. Associative Sketch / SpaceCurve entities ("Sketch#1@Point:0", "Sketch#1@Mid:3", "Sketch#1@Curve:3", "SpaceCurve#1@Pole:0", "SpaceCurve#1@Curve")
    const size_t At = Name.find('@');
    if (At != std::string::npos)
    {
        const std::string StepLabel = Name.substr(0, At);
        const std::string Suffix    = Name.substr(At + 1);
        for (const FeatureStep& Step : Document.Tree.Steps)
        {
            if (Step.Label != StepLabel) continue;
            if (Step.Kind == FeatureStepKind::Sketch)
            {
                const auto         SolvedIt = Document.Regeneration.Sketches.find(StepLabel);
                const SketchModel& Sketch   = SolvedIt != Document.Regeneration.Sketches.end() ? SolvedIt->second : Step.Sketch;
                OrthonormalFrame   Plane    = Step.Plane;
                const double       Lift     = EvaluateFeatureValue(Document.Tree, Step.Elevation);
                if (std::isfinite(Lift)) Plane.Origin = Plane.Origin + Plane.ZAxis * Lift;
                const auto ToWorld = [&](Vector2 Local) -> Vector3 { return Plane.Origin + Plane.XAxis * Local.X + Plane.YAxis * Local.Y; };

                int ParsedIdx = -1;
                if (Suffix.rfind("Point:", 0) == 0 && ParseTrailingDimensionIndex(Suffix.substr(6), ParsedIdx))
                {
                    if (ParsedIdx >= 0 && ParsedIdx < int(Sketch.Points.size()))
                    {
                        Out.FoundCondition = true;
                        Out.Point          = ToWorld(Sketch.Points[ParsedIdx].Position);
                        Out.PlaneNormal    = SafeUnitNormal(Plane.ZAxis);
                    }
                    return Out;
                }
                if (Suffix.rfind("Mid:", 0) == 0 && ParseTrailingDimensionIndex(Suffix.substr(4), ParsedIdx))
                {
                    for (const SketchCurve& Curve : Sketch.Curves)
                    {
                        if (Curve.Identifier != ParsedIdx) continue;
                        const std::vector<Vector2> Samples = SampleSketchCurve(Sketch, Curve);
                        if (!Samples.empty())
                        {
                            Out.FoundCondition = true;
                            Out.Point          = ToWorld(Samples[Samples.size() / 2]);
                            Out.PlaneNormal    = SafeUnitNormal(Plane.ZAxis);
                        }
                        return Out;
                    }
                    return Out;
                }
                if (Suffix.rfind("Curve:", 0) == 0 && ParseTrailingDimensionIndex(Suffix.substr(6), ParsedIdx))
                {
                    for (const SketchCurve& Curve : Sketch.Curves)
                    {
                        if (Curve.Identifier != ParsedIdx) continue;
                        Out.PlaneNormal = SafeUnitNormal(Plane.ZAxis);
                        if (Curve.Kind == SketchCurveKind::Line && Curve.Start >= 0 && Curve.Start < int(Sketch.Points.size())
                            && Curve.End >= 0 && Curve.End < int(Sketch.Points.size()))
                        {
                            Out.FoundCondition   = true;
                            Out.Point            = ToWorld(Sketch.Points[Curve.Start].Position);
                            Out.SecondEndpoint   = ToWorld(Sketch.Points[Curve.End].Position);
                            Out.EdgeSpan         = Distance(Out.Point, Out.SecondEndpoint);
                            Out.SegmentCondition = Out.EdgeSpan > 1.0e-9;
                            if (Out.SegmentCondition) Out.LineDir = SafeUnitNormal(Out.SecondEndpoint - Out.Point);
                        }
                        else if (Curve.Kind == SketchCurveKind::Circle && Curve.Centre >= 0 && Curve.Centre < int(Sketch.Points.size()))
                        {
                            Out.FoundCondition  = true;
                            Out.RadialCondition = true;
                            Out.Centre          = ToWorld(Sketch.Points[Curve.Centre].Position);
                            Out.Radius          = Curve.Radius;
                            Out.Point           = Out.Centre;
                            Out.Rim             = Out.Centre + SafeUnitNormal(Plane.XAxis, { 1.0, 0.0, 0.0 }) * Curve.Radius;
                        }
                        else if (Curve.Kind == SketchCurveKind::Arc && Curve.Centre >= 0 && Curve.Centre < int(Sketch.Points.size())
                                 && Curve.Start >= 0 && Curve.Start < int(Sketch.Points.size())
                                 && Curve.End >= 0 && Curve.End < int(Sketch.Points.size()))
                        {
                            Out.FoundCondition   = true;
                            Out.RadialCondition  = true;
                            Out.Centre           = ToWorld(Sketch.Points[Curve.Centre].Position);
                            Out.Point            = ToWorld(Sketch.Points[Curve.Start].Position);
                            Out.SecondEndpoint   = ToWorld(Sketch.Points[Curve.End].Position);
                            Out.Radius           = Distance(Out.Centre, Out.Point);
                            Out.SegmentCondition = Distance(Out.Point, Out.SecondEndpoint) > 1.0e-9;
                            const std::vector<Vector2> Samples = SampleSketchCurve(Sketch, Curve);
                            Out.Rim = !Samples.empty()
                                ? ToWorld(Samples[Samples.size() / 2])
                                : (Out.Centre + SafeUnitNormal(Out.Point - Out.Centre, Plane.XAxis) * Out.Radius);
                            Out.EdgeSpan = 0.0;
                            for (size_t Idx = 1; Idx < Samples.size(); ++Idx)
                                Out.EdgeSpan += Distance(ToWorld(Samples[Idx - 1]), ToWorld(Samples[Idx]));
                        }
                        else
                        {
                            const std::vector<Vector2> Samples = SampleSketchCurve(Sketch, Curve);
                            if (Samples.size() >= 2)
                            {
                                Out.FoundCondition   = true;
                                Out.Point            = ToWorld(Samples.front());
                                Out.SecondEndpoint   = ToWorld(Samples.back());
                                Out.SegmentCondition = Distance(Out.Point, Out.SecondEndpoint) > 1.0e-9;
                                Out.EdgeSpan         = 0.0;
                                for (size_t Idx = 1; Idx < Samples.size(); ++Idx)
                                    Out.EdgeSpan += Distance(ToWorld(Samples[Idx - 1]), ToWorld(Samples[Idx]));
                            }
                        }
                        return Out;
                    }
                    return Out;
                }
                return Out;
            }
            if (Step.Kind == FeatureStepKind::SpaceCurve)
            {
                const auto FitIt = Document.Regeneration.Curves.find(StepLabel);
                int ParsedIdx = -1;
                if (Suffix.rfind("Pole:", 0) == 0 && ParseTrailingDimensionIndex(Suffix.substr(5), ParsedIdx))
                {
                    if (FitIt != Document.Regeneration.Curves.end() && FitIt->second.Curve.Definition
                        && ParsedIdx >= 0 && ParsedIdx < int(FitIt->second.Curve.Definition->Poles.size()))
                    {
                        Out.FoundCondition = true;
                        Out.Point          = FitIt->second.Curve.Definition->Poles[ParsedIdx];
                    }
                    else if (ParsedIdx >= 0 && ParsedIdx < int(Step.Points.size()))
                    {
                        Out.FoundCondition = true;
                        Out.Point          = Step.Points[ParsedIdx];
                    }
                    return Out;
                }
                if (Suffix == "Curve" && FitIt != Document.Regeneration.Curves.end() && FitIt->second.Curve.Definition)
                {
                    const NurbsCurveDefinition& Def = *FitIt->second.Curve.Definition;
                    const NurbsDomain Domain = MeasureNurbsCurveDomain(Def);
                    Out.FoundCondition   = true;
                    Out.Point            = EvaluateNurbsCurve(Def, Domain.Low).Position;
                    Out.SecondEndpoint   = EvaluateNurbsCurve(Def, Domain.High).Position;
                    Out.SegmentCondition = Distance(Out.Point, Out.SecondEndpoint) > 1.0e-9;
                    Out.EdgeSpan         = 0.0;
                    Vector3 Prev         = Out.Point;
                    for (int SampleIdx = 1; SampleIdx <= 64; ++SampleIdx)
                    {
                        const double Tau = Domain.Low + (Domain.High - Domain.Low) * (double(SampleIdx) / 64.0);
                        const Vector3 Cur = EvaluateNurbsCurve(Def, Tau).Position;
                        Out.EdgeSpan += Distance(Prev, Cur);
                        Prev = Cur;
                    }
                    Out.PlaneNormal = DefaultPlaneNormalForSpan(Out.SecondEndpoint - Out.Point);
                    return Out;
                }
                return Out;
            }
        }
        return Out;
    }

    if (!Document.Partition) return Out;
    const ModelPartition& Partition = *Document.Partition;
    for (const RegeneratedBody& Body : Document.Regeneration.Bodies)
    {
        if (!Body.Body.PresentCondition()) continue;
        const BodyInventory Inventory = Partition.CollectInventory(Body.Body);
        if (Kind == SketcherSelectionKind::Vertex)
        {
            for (const VertexToken Vertex : Inventory.Vertices)
            {
                const auto Found = Body.Naming.Vertices.find(Vertex.Slot);
                if (Found == Body.Naming.Vertices.end() || Found->second != Name) continue;
                Out.FoundCondition  = true;
                Out.Target          = Vertex;
                Out.TargetCondition = true;
                Out.Point           = Partition.Vertices.Resolve(Vertex).Position;
                return Out;
            }
        }
        else if (Kind == SketcherSelectionKind::Edge)
        {
            for (const EdgeToken Edge : Inventory.Edges)
            {
                const auto Found = Body.Naming.Edges.find(Edge.Slot);
                if (Found == Body.Naming.Edges.end() || Found->second != Name) continue;
                const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
                Out.FoundCondition   = true;
                Out.Target           = Edge;
                Out.TargetCondition  = true;
                Out.Point            = Partition.Vertices.Resolve(Profile.Start).Position;
                Out.SecondEndpoint   = Partition.Vertices.Resolve(Profile.End).Position;
                Out.SegmentCondition = Distance(Out.Point, Out.SecondEndpoint) > 1.0e-9;
                Out.EdgeSpan         = MeasureEdgeLength(Partition, Edge);
                Out.PlaneNormal      = DefaultPlaneNormalForSpan(Out.SecondEndpoint - Out.Point);
                // Prefer an adjacent planar face normal so box/prism edge dimensions lie flat on the face plane.
                for (const FaceToken Face : Inventory.Faces)
                {
                    const FaceProfile& FProf = Partition.Faces.Resolve(Face);
                    if (!FProf.Surface.PresentCondition()) continue;
                    const auto* PlaneSurf = std::get_if<PlaneSurface>(&Partition.Surfaces.Resolve(FProf.Surface));
                    if (!PlaneSurf) continue;
                    bool SharesEdge = false;
                    for (const LoopToken Loop : FProf.Loops)
                        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                        {
                            if (Partition.Fins.Resolve(Fin).Edge == Edge) SharesEdge = true;
                        });
                    if (SharesEdge)
                    {
                        const double Sense = FProf.Sense == Orientation::Forward ? 1.0 : -1.0;
                        const Vector3 CandN = SafeUnitNormal(PlaneSurf->Frame.ZAxis * Sense);
                        if (Out.SegmentCondition && std::abs(Dot(CandN, SafeUnitNormal(Out.SecondEndpoint - Out.Point))) < 0.92)
                        {
                            Out.PlaneNormal = CandN;
                            break;
                        }
                    }
                }
                if (Profile.Curve.PresentCondition())
                {
                    const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
                    if (const auto* Line = std::get_if<StraightLine>(&Curve))
                    {
                        Out.LineDir = SafeUnitNormal(Line->Direction, Out.SecondEndpoint - Out.Point);
                    }
                    else if (const auto* Arc = std::get_if<CircularArc>(&Curve))
                    {
                        Out.RadialCondition = true;
                        Out.Centre          = Arc->Frame.Origin;
                        Out.Radius          = Arc->Radius;
                        Out.PlaneNormal     = SafeUnitNormal(Arc->Frame.ZAxis);
                        const double Mid    = 0.5 * (Profile.IntervalLow + Profile.IntervalHigh);
                        Out.Rim             = EvaluateCurve(Curve, Mid).Position;
                    }
                }
                return Out;
            }
        }
        else if (Kind == SketcherSelectionKind::Face)
        {
            for (const FaceToken Face : Inventory.Faces)
            {
                const auto Found = Body.Naming.Faces.find(Face.Slot);
                if (Found == Body.Naming.Faces.end() || Found->second != Name) continue;
                const FaceProfile& Profile = Partition.Faces.Resolve(Face);
                Out.FoundCondition  = true;
                Out.Target          = Face;
                Out.TargetCondition = true;
                Vector3 Sum{};
                int     Count = 0;
                for (const LoopToken Loop : Profile.Loops)
                    Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                    {
                        const EdgeProfile& EdgeProf = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
                        Sum = Sum + Partition.Vertices.Resolve(EdgeProf.Start).Position;
                        ++Count;
                    });
                if (Count > 0) Out.Point = Sum * (1.0 / double(Count));
                if (Profile.Surface.PresentCondition())
                {
                    const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
                    const double           Sense   = Profile.Sense == Orientation::Forward ? 1.0 : -1.0;
                    if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
                    {
                        Out.FaceNormal  = SafeUnitNormal(Plane->Frame.ZAxis * Sense);
                        Out.PlaneNormal = SafeUnitNormal(Plane->Frame.XAxis, { 0.0, 0.0, 1.0 });
                    }
                    else if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
                    {
                        Out.RadialCondition = true;
                        Out.Radius          = Cylinder->Radius;
                        Out.LineDir         = SafeUnitNormal(Cylinder->Frame.ZAxis);
                        Out.PlaneNormal     = *Out.LineDir;
                        const double Axial  = Count > 0 ? Dot(Out.Point - Cylinder->Frame.Origin, *Out.LineDir) : 0.0;
                        Out.Centre          = Cylinder->Frame.Origin + (*Out.LineDir) * Axial;
                        Out.Rim             = Out.Centre + SafeUnitNormal(Cylinder->Frame.XAxis, { 1.0, 0.0, 0.0 }) * Cylinder->Radius;
                    }
                    else if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
                    {
                        Out.RadialCondition = true;
                        Out.Centre          = Sphere->Frame.Origin;
                        Out.Radius          = Sphere->Radius;
                        Out.PlaneNormal     = { 0.0, 0.0, 1.0 };
                        Out.Rim             = Sphere->Frame.Origin + Vector3{ Sphere->Radius, 0.0, 0.0 };
                    }
                }
                return Out;
            }
        }
        else if (Kind == SketcherSelectionKind::Body && Body.Key == Name)
        {
            Vector3 Low{}, High{};
            if (ComputeBodyBounds(Document, Name, Low, High))
            {
                Out.FoundCondition = true;
                Out.Point          = (Low + High) * 0.5;
                if (!Inventory.Faces.empty())
                {
                    Out.Target          = Inventory.Faces.front();
                    Out.TargetCondition = true;
                }
            }
            return Out;
        }
    }
    return Out;
}

[[nodiscard]] std::string NextDimensionId(const SketcherDocument& Document)
{
    int Highest = 0;
    for (const SketcherDimension& Existing : Document.Dimensions)
    {
        if (Existing.Id.rfind("Dim", 0) == 0 && Existing.Id.size() > 3)
        {
            int Parsed = 0;
            bool Digits = true;
            for (size_t Index = 3; Index < Existing.Id.size(); ++Index)
            {
                if (!std::isdigit(static_cast<unsigned char>(Existing.Id[Index]))) { Digits = false; break; }
                Parsed = Parsed * 10 + (Existing.Id[Index] - '0');
            }
            if (Digits) Highest = std::max(Highest, Parsed);
        }
    }
    return "Dim" + std::to_string(Highest + 1);
}

void EvaluateLinearValue(SketcherDimension& Dim)
{
    const Vector3 Delta = Dim.SecondPoint - Dim.FirstPoint;
    if (Dim.AxisFilter == 0) Dim.Value = std::abs(Delta.X);
    else if (Dim.AxisFilter == 1) Dim.Value = std::abs(Delta.Y);
    else if (Dim.AxisFilter == 2) Dim.Value = std::abs(Delta.Z);
    else Dim.Value = Length(Delta);
}

void EvaluateSingleDimension(const SketcherDocument& Document, SketcherDimension& Dim)
{
    if (!Dim.FirstName.empty() && Dim.FirstKind == SketcherSelectionKind::Body && Dim.SecondName.empty() && Dim.AxisFilter >= 0 && Dim.AxisFilter < 3)
    {
        Vector3 Low{}, High{};
        if (ComputeBodyBounds(Document, Dim.FirstName, Low, High))
        {
            if (Dim.AxisFilter == 0)
            {
                Dim.FirstPoint  = { Low.X, Low.Y, Low.Z };
                Dim.SecondPoint = { High.X, Low.Y, Low.Z };
                Dim.PlaneNormal = { 0.0, 0.0, 1.0 };
            }
            else if (Dim.AxisFilter == 1)
            {
                Dim.FirstPoint  = { High.X, Low.Y, Low.Z };
                Dim.SecondPoint = { High.X, High.Y, Low.Z };
                Dim.PlaneNormal = { 0.0, 0.0, 1.0 };
            }
            else
            {
                Dim.FirstPoint  = { High.X, High.Y, Low.Z };
                Dim.SecondPoint = { High.X, High.Y, High.Z };
                Dim.PlaneNormal = { 0.0, -1.0, 0.0 };
            }
            EvaluateLinearValue(Dim);
        }
        return;
    }
    if (Dim.FirstName.empty())
    {
        if (Dim.Kind == SketcherDimensionKind::Linear) EvaluateLinearValue(Dim);
        else if (Dim.Kind == SketcherDimensionKind::Radius) Dim.Value = Distance(Dim.FirstPoint, Dim.SecondPoint);
        else if (Dim.Kind == SketcherDimensionKind::Diameter) Dim.Value = 2.0 * Distance(Dim.FirstPoint, Dim.SecondPoint);
        return;
    }
    const ResolvedDimensionOperand First = ResolveDimensionOperand(Document, Dim.FirstKind, Dim.FirstName);
    if (!First.FoundCondition) return;

    if (Dim.SecondName.empty())
    {
        if ((Dim.Kind == SketcherDimensionKind::Radius || Dim.Kind == SketcherDimensionKind::Diameter) && First.RadialCondition)
        {
            Dim.FirstPoint  = First.Centre;
            Dim.PlaneNormal = First.PlaneNormal;
            Vector3 Radial  = Dim.Direction - First.PlaneNormal * Dot(Dim.Direction, First.PlaneNormal);
            if (Length(Radial) > 1.0e-6)
            {
                Radial          = Normalize(Radial);
                Dim.Direction   = Radial;
                Dim.SecondPoint = First.Centre + Radial * First.Radius;
            }
            else
            {
                Dim.SecondPoint = First.Rim;
                Dim.Direction   = SafeUnitNormal(First.Rim - First.Centre, { 1.0, 0.0, 0.0 });
            }
            Dim.Value = Dim.Kind == SketcherDimensionKind::Diameter ? 2.0 * First.Radius : First.Radius;
        }
        else if (Dim.Kind == SketcherDimensionKind::Linear && First.SegmentCondition)
        {
            Dim.FirstPoint  = First.Point;
            Dim.SecondPoint = First.SecondEndpoint;
            if (Dim.FirstName.find('@') != std::string::npos) Dim.PlaneNormal = First.PlaneNormal;
            EvaluateLinearValue(Dim);
            if (Dim.AxisFilter < 0 && First.EdgeSpan > 0.0 && !First.LineDir.has_value()) Dim.Value = First.EdgeSpan;
        }
        return;
    }

    const ResolvedDimensionOperand Second = ResolveDimensionOperand(Document, Dim.SecondKind, Dim.SecondName);
    if (!Second.FoundCondition) return;

    if (Dim.Kind == SketcherDimensionKind::Angular)
    {
        constexpr double RadToDeg = 180.0 / std::numbers::pi;
        Vector3 RayA = First.LineDir.value_or(First.FaceNormal.value_or(Vector3{ 1.0, 0.0, 0.0 }));
        Vector3 RayB = Second.LineDir.value_or(Second.FaceNormal.value_or(Vector3{ 0.0, 1.0, 0.0 }));
        RayA = SafeUnitNormal(RayA, { 1.0, 0.0, 0.0 });
        RayB = SafeUnitNormal(RayB, { 0.0, 1.0, 0.0 });
        Vector3 Corner = (First.Point + Second.Point) * 0.5;
        if (First.LineDir && Second.LineDir)
        {
            const double AB    = Dot(RayA, RayB);
            const double Denom = 1.0 - AB * AB;
            if (Denom > 1.0e-8)
            {
                const Vector3 W0 = First.Point - Second.Point;
                const double  B  = Dot(RayA, W0);
                const double  C  = Dot(RayB, W0);
                const double  S  = (AB * C - B) / Denom;
                const double  T  = (C - AB * B) / Denom;
                Corner = ((First.Point + RayA * S) + (Second.Point + RayB * T)) * 0.5;
            }
            const Vector3 MidA = First.SegmentCondition ? (First.Point + First.SecondEndpoint) * 0.5 : (First.Point + RayA);
            const Vector3 MidB = Second.SegmentCondition ? (Second.Point + Second.SecondEndpoint) * 0.5 : (Second.Point + RayB);
            if (Dot(MidA - Corner, RayA) < -1.0e-6) RayA = RayA * -1.0;
            if (Dot(MidB - Corner, RayB) < -1.0e-6) RayB = RayB * -1.0;
        }
        const double Cosine = std::clamp(Dot(RayA, RayB), -1.0, 1.0);
        Dim.Value       = std::acos(Cosine) * RadToDeg;
        Dim.FirstPoint  = Corner;
        Dim.SecondPoint = RayA;
        Dim.Direction   = RayB;
        Dim.PlaneNormal = SafeUnitNormal(Cross(RayA, RayB), { 0.0, 0.0, 1.0 });
        return;
    }

    const bool BothExactPoints = (Dim.FirstKind == SketcherSelectionKind::Vertex || Dim.FirstName.find("@Point:") != std::string::npos || Dim.FirstName.find("@Mid:") != std::string::npos || Dim.FirstName.find("@Pole:") != std::string::npos)
                              && (Dim.SecondKind == SketcherSelectionKind::Vertex || Dim.SecondName.find("@Point:") != std::string::npos || Dim.SecondName.find("@Mid:") != std::string::npos || Dim.SecondName.find("@Pole:") != std::string::npos);

    if (!BothExactPoints && Document.Partition && First.TargetCondition && Second.TargetCondition)
    {
        const DistanceMeasure Pair = MeasureMinimumDistance(*Document.Partition, First.Target, Second.Target);
        if (Pair.Refusal.empty())
        {
            Dim.FirstPoint  = Pair.First;
            Dim.SecondPoint = Pair.Second;
            if (Dim.AxisFilter < 0) Dim.Value = Pair.Distance;
            else EvaluateLinearValue(Dim);
            return;
        }
    }
    Dim.FirstPoint  = First.Point;
    Dim.SecondPoint = Second.Point;
    const size_t AtA = Dim.FirstName.find('@');
    const size_t AtB = Dim.SecondName.find('@');
    if (AtA != std::string::npos && AtB != std::string::npos && Dim.FirstName.substr(0, AtA) == Dim.SecondName.substr(0, AtB)
        && Dim.FirstName.rfind("Sketch#", 0) == 0)
    {
        Dim.PlaneNormal = First.PlaneNormal;
    }
    EvaluateLinearValue(Dim);
}

} // namespace

std::vector<SketcherDimension> EvaluateSketcherDimensions(const SketcherDocument& Document)
{
    std::vector<SketcherDimension> Evaluated = Document.Dimensions;
    for (SketcherDimension& Dim : Evaluated) EvaluateSingleDimension(Document, Dim);
    return Evaluated;
}

void RefreshSketcherDimensions(SketcherDocument& Document)
{
    for (SketcherDimension& Dim : Document.Dimensions) EvaluateSingleDimension(Document, Dim);
}

const char* SketcherDimensionKindLabel(SketcherDimensionKind Kind) noexcept
{
    switch (Kind)
    {
    case SketcherDimensionKind::Linear:   return "Linear";
    case SketcherDimensionKind::Radius:   return "Radius";
    case SketcherDimensionKind::Diameter: return "Diameter";
    case SketcherDimensionKind::Angular:  return "Angle";
    }
    return "Linear";
}

std::string FormatSketcherDimension(const SketcherDimension& Dimension)
{
    char Buffer[96];
    const std::string Prefix = Dimension.Note.empty() ? std::string{} : (Dimension.Note + " ");
    switch (Dimension.Kind)
    {
    case SketcherDimensionKind::Angular:
        std::snprintf(Buffer, sizeof(Buffer), "%s%.2f\xC2\xB0", Prefix.c_str(), Dimension.Value);
        return Buffer;
    case SketcherDimensionKind::Radius:
        std::snprintf(Buffer, sizeof(Buffer), "%sR%.4g m", Prefix.c_str(), Dimension.Value);
        return Buffer;
    case SketcherDimensionKind::Diameter:
        std::snprintf(Buffer, sizeof(Buffer), "%s\xC3\x98%.4g m", Prefix.c_str(), Dimension.Value);
        return Buffer;
    case SketcherDimensionKind::Linear:
    {
        const char* AxisTag = Dimension.AxisFilter == 0 ? "dX " : Dimension.AxisFilter == 1 ? "dY " : Dimension.AxisFilter == 2 ? "dZ " : "";
        std::snprintf(Buffer, sizeof(Buffer), "%s%s%.4g m", Prefix.c_str(), Dimension.Note.empty() ? AxisTag : "", Dimension.Value);
        return Buffer;
    }
    }
    return {};
}

SketcherCommandOutcome AddSketcherDimensionFromSelection(SketcherDocument& Document, std::optional<SketcherDimensionKind> PreferredKind, double Offset)
{
    return RunAppearanceCommand(Document, "Add dimension", [&]() -> std::string
    {
        // Collect ordered selection items across vertices, edges, faces, curves, and bodies.
        struct PickEntry { SketcherSelectionKind Kind; std::string Name; };
        std::vector<PickEntry> Picks;
        for (const std::string& Name : Document.Selection.Vertices) Picks.push_back({ SketcherSelectionKind::Vertex, Name });
        for (const std::string& Name : Document.Selection.Faces)    Picks.push_back({ SketcherSelectionKind::Face, Name });
        for (const std::string& Name : Document.Selection.Edges)    Picks.push_back({ SketcherSelectionKind::Edge, Name });

        if (Picks.size() == 1)
        {
            const ResolvedDimensionOperand Op = ResolveDimensionOperand(Document, Picks[0].Kind, Picks[0].Name);
            if (!Op.FoundCondition) return "Dimension: selected entity could not be resolved";
            if (Op.RadialCondition && PreferredKind != SketcherDimensionKind::Linear)
            {
                SketcherDimension Dim;
                Dim.Id          = NextDimensionId(Document);
                Dim.Kind        = PreferredKind == SketcherDimensionKind::Radius ? SketcherDimensionKind::Radius : SketcherDimensionKind::Diameter;
                Dim.FirstKind   = Picks[0].Kind;
                Dim.FirstName   = Picks[0].Name;
                Dim.FirstPoint  = Op.Centre;
                Dim.SecondPoint = Op.Rim;
                Dim.PlaneNormal = Op.PlaneNormal;
                Dim.Offset      = Offset;
                Dim.Value       = Dim.Kind == SketcherDimensionKind::Diameter ? 2.0 * Op.Radius : Op.Radius;
                Document.Dimensions.push_back(std::move(Dim));
                return {};
            }
            if (Picks[0].Kind == SketcherSelectionKind::Edge && Op.SegmentCondition)
            {
                SketcherDimension Dim;
                Dim.Id          = NextDimensionId(Document);
                Dim.Kind        = SketcherDimensionKind::Linear;
                Dim.FirstKind   = SketcherSelectionKind::Edge;
                Dim.FirstName   = Picks[0].Name;
                Dim.FirstPoint  = Op.Point;
                Dim.SecondPoint = Op.SecondEndpoint;
                Dim.PlaneNormal = Op.PlaneNormal;
                Dim.Offset      = Offset;
                Dim.Value       = Op.LineDir.has_value() ? Distance(Op.Point, Op.SecondEndpoint) : Op.EdgeSpan;
                Document.Dimensions.push_back(std::move(Dim));
                return {};
            }
            // Single planar/non-radial face: fall through to dimension its owning body's W × D × H envelope if no second pick.
            const std::string OwnerBody = SketcherBodyOfName(Document, Picks[0].Kind, Picks[0].Name);
            if (!OwnerBody.empty() && PreferredKind != SketcherDimensionKind::Angular && PreferredKind != SketcherDimensionKind::Radius && PreferredKind != SketcherDimensionKind::Diameter)
            {
                Vector3 Low{}, High{};
                if (ComputeBodyBounds(Document, OwnerBody, Low, High))
                {
                    static const char* Notes[3] = { "W", "D", "H" };
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        SketcherDimension Dim;
                        Dim.Id         = NextDimensionId(Document);
                        Dim.Kind       = SketcherDimensionKind::Linear;
                        Dim.FirstKind  = SketcherSelectionKind::Body;
                        Dim.FirstName  = OwnerBody;
                        Dim.AxisFilter = Axis;
                        Dim.Offset     = Axis == 0 ? -std::abs(Offset) : std::abs(Offset);
                        Dim.Note       = Notes[Axis];
                        Document.Dimensions.push_back(std::move(Dim));
                    }
                    RefreshSketcherDimensions(Document);
                    return {};
                }
            }
            return "Dimension: select an edge, circular/cylindrical face, two entities, or a body";
        }

        if (Picks.size() >= 2)
        {
            const ResolvedDimensionOperand First  = ResolveDimensionOperand(Document, Picks[0].Kind, Picks[0].Name);
            const ResolvedDimensionOperand Second = ResolveDimensionOperand(Document, Picks[1].Kind, Picks[1].Name);
            if (!First.FoundCondition || !Second.FoundCondition) return "Dimension: selected entities could not be resolved";

            const bool BothDirectional = (First.LineDir.has_value() || First.FaceNormal.has_value()) && (Second.LineDir.has_value() || Second.FaceNormal.has_value());
            bool AutoAngular = false;
            if (BothDirectional && !PreferredKind.has_value())
            {
                const Vector3 DirA = SafeUnitNormal(First.LineDir.value_or(First.FaceNormal.value_or(Vector3{ 1.0, 0.0, 0.0 })));
                const Vector3 DirB = SafeUnitNormal(Second.LineDir.value_or(Second.FaceNormal.value_or(Vector3{ 0.0, 1.0, 0.0 })));
                const double  Cosine = std::abs(Dot(DirA, DirB));
                if (Cosine < 0.9995) AutoAngular = true;
            }
            if (PreferredKind == SketcherDimensionKind::Angular || AutoAngular)
            {
                if (!BothDirectional) return "Angle dimension needs two linear edges or planar/cylindrical faces";
                SketcherDimension Dim;
                Dim.Id         = NextDimensionId(Document);
                Dim.Kind       = SketcherDimensionKind::Angular;
                Dim.FirstKind  = Picks[0].Kind;
                Dim.FirstName  = Picks[0].Name;
                Dim.SecondKind = Picks[1].Kind;
                Dim.SecondName = Picks[1].Name;
                Dim.Offset     = Offset;
                Document.Dimensions.push_back(std::move(Dim));
                RefreshSketcherDimensions(Document);
                return {};
            }

            SketcherDimension Dim;
            Dim.Id          = NextDimensionId(Document);
            Dim.Kind        = SketcherDimensionKind::Linear;
            Dim.FirstKind   = Picks[0].Kind;
            Dim.FirstName   = Picks[0].Name;
            Dim.SecondKind  = Picks[1].Kind;
            Dim.SecondName  = Picks[1].Name;
            Dim.FirstPoint  = First.Point;
            Dim.SecondPoint = Second.Point;
            Dim.Offset      = Offset;
            Document.Dimensions.push_back(std::move(Dim));
            RefreshSketcherDimensions(Document);
            Dim.PlaneNormal = DefaultPlaneNormalForSpan(Document.Dimensions.back().SecondPoint - Document.Dimensions.back().FirstPoint);
            Document.Dimensions.back().PlaneNormal = Dim.PlaneNormal;
            if (Document.Dimensions.back().Value < 1.0e-7 && BothDirectional)
            {
                // Intersecting edges / adjacent planar faces with 0 separation: default to clean Angular dimension!
                Document.Dimensions.back().Kind = SketcherDimensionKind::Angular;
                RefreshSketcherDimensions(Document);
            }
            return {};
        }

        // Sketch curve selection: dimension circle diameter / line span on the sketch plane.
        if (!Document.Selection.Curves.empty())
        {
            const std::string& Label = Document.Selection.Curves.back();
            for (const FeatureStep& Step : Document.Tree.Steps)
            {
                if (Step.Label != Label || Step.Kind != FeatureStepKind::Sketch) continue;
                const auto SolvedIt = Document.Regeneration.Sketches.find(Label);
                const SketchModel& Sketch = SolvedIt != Document.Regeneration.Sketches.end() ? SolvedIt->second : Step.Sketch;
                OrthonormalFrame Plane = Step.Plane;
                Plane.Origin = Plane.Origin + Plane.ZAxis * EvaluateFeatureValue(Document.Tree, Step.Elevation);
                const auto ToWorld = [&](Vector2 Local) { return Plane.Origin + Plane.XAxis * Local.X + Plane.YAxis * Local.Y; };
                if (Sketch.Curves.size() == 1 && (Sketch.Curves[0].Kind == SketchCurveKind::Circle || Sketch.Curves[0].Kind == SketchCurveKind::Arc) && Sketch.Curves[0].Centre >= 0)
                {
                    const Vector2 Centre2 = Sketch.Points[Sketch.Curves[0].Centre].Position;
                    const double  Radius  = Sketch.Curves[0].Kind == SketchCurveKind::Circle ? Sketch.Curves[0].Radius
                                          : Distance(Centre2, Sketch.Points[Sketch.Curves[0].Start].Position);
                    SketcherDimension Dim;
                    Dim.Id          = NextDimensionId(Document);
                    Dim.Kind        = PreferredKind == SketcherDimensionKind::Radius ? SketcherDimensionKind::Radius
                                    : (Sketch.Curves[0].Kind == SketchCurveKind::Arc ? SketcherDimensionKind::Radius : SketcherDimensionKind::Diameter);
                    Dim.FirstKind   = SketcherSelectionKind::Edge;
                    Dim.FirstName   = Label + "@Curve:" + std::to_string(Sketch.Curves[0].Identifier);
                    Dim.FirstPoint  = ToWorld(Centre2);
                    Dim.SecondPoint = ToWorld({ Centre2.X + Radius, Centre2.Y });
                    Dim.Direction   = SafeUnitNormal(Plane.XAxis, { 1.0, 0.0, 0.0 });
                    Dim.PlaneNormal = Plane.ZAxis;
                    Dim.Offset      = Offset;
                    Dim.Value       = Dim.Kind == SketcherDimensionKind::Diameter ? 2.0 * Radius : Radius;
                    Document.Dimensions.push_back(std::move(Dim));
                    RefreshSketcherDimensions(Document);
                    return {};
                }
                if (Sketch.Points.size() >= 2)
                {
                    // Prefer associative line curves when available (e.g. rectangle / polygon edges).
                    int HorizCurveId = -1, VertCurveId = -1;
                    for (const SketchCurve& C : Sketch.Curves)
                    {
                        if (C.Kind != SketchCurveKind::Line || C.Start < 0 || C.End < 0) continue;
                        const Vector2 Delta = Sketch.Points[C.End].Position - Sketch.Points[C.Start].Position;
                        if (HorizCurveId < 0 && std::abs(Delta.X) > std::abs(Delta.Y) && std::abs(Delta.X) > 1.0e-6) HorizCurveId = C.Identifier;
                        else if (VertCurveId < 0 && std::abs(Delta.Y) >= std::abs(Delta.X) && std::abs(Delta.Y) > 1.0e-6) VertCurveId = C.Identifier;
                    }
                    Vector2 Low = Sketch.Points[0].Position, High = Low;
                    for (const SketchPoint& Pt : Sketch.Points)
                    {
                        Low  = { std::min(Low.X, Pt.Position.X), std::min(Low.Y, Pt.Position.Y) };
                        High = { std::max(High.X, Pt.Position.X), std::max(High.Y, Pt.Position.Y) };
                    }
                    if (HorizCurveId >= 0 || High.X - Low.X > 1.0e-6)
                    {
                        SketcherDimension DimX;
                        DimX.Id          = NextDimensionId(Document);
                        DimX.Kind        = SketcherDimensionKind::Linear;
                        if (HorizCurveId >= 0)
                        {
                            DimX.FirstKind = SketcherSelectionKind::Edge;
                            DimX.FirstName = Label + "@Curve:" + std::to_string(HorizCurveId);
                        }
                        DimX.FirstPoint  = ToWorld({ Low.X, Low.Y });
                        DimX.SecondPoint = ToWorld({ High.X, Low.Y });
                        DimX.PlaneNormal = Plane.ZAxis;
                        DimX.Offset      = -std::abs(Offset);
                        EvaluateLinearValue(DimX);
                        Document.Dimensions.push_back(std::move(DimX));
                    }
                    if (VertCurveId >= 0 || High.Y - Low.Y > 1.0e-6)
                    {
                        SketcherDimension DimY;
                        DimY.Id          = NextDimensionId(Document);
                        DimY.Kind        = SketcherDimensionKind::Linear;
                        if (VertCurveId >= 0)
                        {
                            DimY.FirstKind = SketcherSelectionKind::Edge;
                            DimY.FirstName = Label + "@Curve:" + std::to_string(VertCurveId);
                        }
                        DimY.FirstPoint  = ToWorld({ High.X, Low.Y });
                        DimY.SecondPoint = ToWorld({ High.X, High.Y });
                        DimY.PlaneNormal = Plane.ZAxis;
                        DimY.Offset      = std::abs(Offset);
                        EvaluateLinearValue(DimY);
                        Document.Dimensions.push_back(std::move(DimY));
                    }
                    RefreshSketcherDimensions(Document);
                    if (!Document.Dimensions.empty()) return {};
                }
            }
        }

        // Body selection: 2 bodies -> minimum clearance dimension; 1 body -> W × D × H overall bounding-box dimensions!
        if (Document.Selection.Bodies.size() >= 2 && Document.Partition)
        {
            const RegeneratedBody* BodyA = FindRegeneratedBody(Document, Document.Selection.Bodies[0]);
            const RegeneratedBody* BodyB = FindRegeneratedBody(Document, Document.Selection.Bodies[1]);
            if (BodyA && BodyB)
            {
                const BodyInventory InvA = Document.Partition->CollectInventory(BodyA->Body);
                const BodyInventory InvB = Document.Partition->CollectInventory(BodyB->Body);
                double  BestDist = std::numeric_limits<double>::infinity();
                Vector3 BestA{}, BestB{};
                std::string FaceNameA, FaceNameB;
                for (const FaceToken FA : InvA.Faces)
                    for (const FaceToken FB : InvB.Faces)
                    {
                        const DistanceMeasure M = MeasureMinimumDistance(*Document.Partition, FA, FB);
                        if (M.Refusal.empty() && M.Distance < BestDist)
                        {
                            BestDist = M.Distance;
                            BestA    = M.First;
                            BestB    = M.Second;
                            if (const auto ItA = BodyA->Naming.Faces.find(FA.Slot); ItA != BodyA->Naming.Faces.end()) FaceNameA = ItA->second;
                            if (const auto ItB = BodyB->Naming.Faces.find(FB.Slot); ItB != BodyB->Naming.Faces.end()) FaceNameB = ItB->second;
                        }
                    }
                if (std::isfinite(BestDist))
                {
                    SketcherDimension Dim;
                    Dim.Id          = NextDimensionId(Document);
                    Dim.Kind        = SketcherDimensionKind::Linear;
                    Dim.FirstKind   = SketcherSelectionKind::Face;
                    Dim.FirstName   = FaceNameA;
                    Dim.SecondKind  = SketcherSelectionKind::Face;
                    Dim.SecondName  = FaceNameB;
                    Dim.FirstPoint  = BestA;
                    Dim.SecondPoint = BestB;
                    Dim.PlaneNormal = DefaultPlaneNormalForSpan(BestB - BestA);
                    Dim.Offset      = Offset;
                    Dim.Value       = BestDist;
                    Document.Dimensions.push_back(std::move(Dim));
                    return {};
                }
            }
        }
        if (Document.Selection.Bodies.size() == 1)
        {
            const std::string& BodyKey = Document.Selection.Bodies.front();
            Vector3 Low{}, High{};
            if (!ComputeBodyBounds(Document, BodyKey, Low, High)) return "Dimension: selected body has no bounds";
            static const char* Notes[3] = { "W", "D", "H" };
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                SketcherDimension Dim;
                Dim.Id         = NextDimensionId(Document);
                Dim.Kind       = SketcherDimensionKind::Linear;
                Dim.FirstKind  = SketcherSelectionKind::Body;
                Dim.FirstName  = BodyKey;
                Dim.AxisFilter = Axis;
                Dim.Offset     = Axis == 0 ? -std::abs(Offset) : std::abs(Offset);
                Dim.Note       = Notes[Axis];
                Document.Dimensions.push_back(std::move(Dim));
            }
            RefreshSketcherDimensions(Document);
            return {};
        }

        return "Dimension: select an edge, circle/cylinder, two entities, or a body — or click 2 points in Dimension mode";
    });
}

SketcherCommandOutcome AddSketcherInteractiveDimension(SketcherDocument& Document, const SketcherDimension& Spec)
{
    return RunAppearanceCommand(Document, "Place dimension", [&]() -> std::string
    {
        SketcherDimension Dim = Spec;
        Dim.Id                = NextDimensionId(Document);
        Dim.AxisFilter        = std::clamp(Dim.AxisFilter, -1, 2);
        Dim.SlideAxis         = std::clamp(Dim.SlideAxis, -1, 2);
        Dim.PlaneNormal       = SafeUnitNormal(Dim.PlaneNormal, DefaultPlaneNormalForSpan(Dim.SecondPoint - Dim.FirstPoint));
        if (Length(Dim.Direction) > 1.0e-9) Dim.Direction = Normalize(Dim.Direction);
        else Dim.Direction = { 1.0, 0.0, 0.0 };
        EvaluateSingleDimension(Document, Dim);
        if (Dim.Kind == SketcherDimensionKind::Linear && Dim.Value < 1.0e-7)
            return "Dimension: start and end points coincide or have zero projected span";
        if ((Dim.Kind == SketcherDimensionKind::Radius || Dim.Kind == SketcherDimensionKind::Diameter) && Dim.Value < 1.0e-7)
            return "Dimension: circle / arc radius is zero";
        Document.Dimensions.push_back(std::move(Dim));
        RefreshSketcherDimensions(Document);
        return {};
    });
}

SketcherCommandOutcome AddSketcherPointDimension(SketcherDocument& Document, Vector3 First, Vector3 Second, Vector3 PlaneNormal,
                                                 double Offset, int AxisFilter, const std::string& FirstEntity,
                                                 SketcherSelectionKind FirstKind, const std::string& SecondEntity,
                                                 SketcherSelectionKind SecondKind)
{
    SketcherDimension Spec;
    Spec.Kind        = SketcherDimensionKind::Linear;
    Spec.FirstKind   = FirstKind;
    Spec.FirstName   = FirstEntity;
    Spec.SecondKind  = SecondKind;
    Spec.SecondName  = SecondEntity;
    Spec.FirstPoint  = First;
    Spec.SecondPoint = Second;
    Spec.PlaneNormal = PlaneNormal;
    Spec.Offset      = Offset;
    Spec.AxisFilter  = AxisFilter;
    return AddSketcherInteractiveDimension(Document, Spec);
}

SketcherCommandOutcome UpdateSketcherDimensionOffset(SketcherDocument& Document, const std::string& Id, double Offset)
{
    return RunAppearanceCommand(Document, "Move dimension", [&]() -> std::string
    {
        for (SketcherDimension& Dim : Document.Dimensions)
        {
            if (Dim.Id != Id) continue;
            if (!std::isfinite(Offset)) return "Dimension: offset must be finite";
            Dim.Offset = Offset;
            return {};
        }
        return "no dimension " + Id;
    });
}

SketcherCommandOutcome UpdateSketcherDimensionPlacement(SketcherDocument& Document, const std::string& Id, double Offset,
                                                        Vector3 Direction, std::optional<int> SlideAxis)
{
    return RunAppearanceCommand(Document, "Move dimension", [&]() -> std::string
    {
        for (SketcherDimension& Dim : Document.Dimensions)
        {
            if (Dim.Id != Id) continue;
            if (!std::isfinite(Offset)) return "Dimension: offset must be finite";
            Dim.Offset = Offset;
            if (SlideAxis.has_value()) Dim.SlideAxis = std::clamp(*SlideAxis, -1, 2);
            if (Length(Direction) > 1.0e-9)
            {
                Dim.Direction = Normalize(Direction);
                EvaluateSingleDimension(Document, Dim);
            }
            return {};
        }
        return "no dimension " + Id;
    });
}

SketcherCommandOutcome UpdateSketcherDimensionSlideAxis(SketcherDocument& Document, const std::string& Id, int SlideAxis)
{
    return RunAppearanceCommand(Document, "Dimension slide axis", [&]() -> std::string
    {
        for (SketcherDimension& Dim : Document.Dimensions)
        {
            if (Dim.Id != Id) continue;
            Dim.SlideAxis = std::clamp(SlideAxis, -1, 2);
            return {};
        }
        return "no dimension " + Id;
    });
}

SketcherCommandOutcome UpdateSketcherDimensionAxisFilter(SketcherDocument& Document, const std::string& Id, int AxisFilter)
{
    return RunAppearanceCommand(Document, "Dimension projection axis", [&]() -> std::string
    {
        for (SketcherDimension& Dim : Document.Dimensions)
        {
            if (Dim.Id != Id) continue;
            if (Dim.Kind != SketcherDimensionKind::Linear) return Id + " is not a linear dimension";
            const int Clamped = std::clamp(AxisFilter, -1, 2);
            const int Prev    = Dim.AxisFilter;
            Dim.AxisFilter    = Clamped;
            EvaluateSingleDimension(Document, Dim);
            if (Dim.Value < 1.0e-7)
            {
                Dim.AxisFilter = Prev;
                EvaluateSingleDimension(Document, Dim);
                return "Dimension: projected span along that axis is zero";
            }
            return {};
        }
        return "no dimension " + Id;
    });
}

SketcherCommandOutcome UpdateSketcherDimensionNote(SketcherDocument& Document, const std::string& Id, const std::string& Note)
{
    return RunAppearanceCommand(Document, "Edit dimension label", [&]() -> std::string
    {
        for (SketcherDimension& Dim : Document.Dimensions)
        {
            if (Dim.Id != Id) continue;
            Dim.Note = Note;
            return {};
        }
        return "no dimension " + Id;
    });
}

SketcherCommandOutcome ToggleSketcherDimensionVisibility(SketcherDocument& Document, const std::string& Id)
{
    return RunAppearanceCommand(Document, "Toggle dimension", [&]() -> std::string
    {
        for (SketcherDimension& Dim : Document.Dimensions)
        {
            if (Dim.Id != Id) continue;
            Dim.VisibleCondition = !Dim.VisibleCondition;
            return {};
        }
        return "no dimension " + Id;
    });
}

SketcherCommandOutcome ToggleSketcherDimensionRadialKind(SketcherDocument& Document, const std::string& Id)
{
    return RunAppearanceCommand(Document, "Switch R / \xC3\x98", [&]() -> std::string
    {
        for (SketcherDimension& Dim : Document.Dimensions)
        {
            if (Dim.Id != Id) continue;
            if (Dim.Kind == SketcherDimensionKind::Diameter)
            {
                Dim.Kind  = SketcherDimensionKind::Radius;
                Dim.Value *= 0.5;
                return {};
            }
            if (Dim.Kind == SketcherDimensionKind::Radius)
            {
                Dim.Kind  = SketcherDimensionKind::Diameter;
                Dim.Value *= 2.0;
                return {};
            }
            return Id + " is not a radial / diametral dimension";
        }
        return "no dimension " + Id;
    });
}

SketcherCommandOutcome RemoveSketcherDimension(SketcherDocument& Document, const std::string& Id)
{
    return RunAppearanceCommand(Document, "Delete dimension", [&]() -> std::string
    {
        const size_t Removed = std::erase_if(Document.Dimensions, [&](const SketcherDimension& Dim) { return Dim.Id == Id; });
        return Removed > 0 ? std::string{} : ("no dimension " + Id);
    });
}

SketcherCommandOutcome ClearSketcherDimensions(SketcherDocument& Document)
{
    return RunAppearanceCommand(Document, "Clear dimensions", [&]() -> std::string
    {
        if (Document.Dimensions.empty()) return "no dimensions to clear";
        Document.Dimensions.clear();
        return {};
    });
}

} // namespace Frontier::Workspaces::Sketcher
