/*====================================================================================================================================
                                                       STEPEXCHANGE.CPP
====================================================================================================================================*/
// 🧩 STEP AP214 writer: product structure, unit context, then one boundary representation per solid region — every instance
//    numbered in writing order, every real in the shortest form that reads back to the same double

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Exchange/StepExchange.h"
#include "Engine/GeometrySpace/Parametric/Exchange/ExchangePath.h"

#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CarrierApproximation.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <optional>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 💡 std::to_chars without a format gives the shortest digits that parse back to the same double; Part 21 then wants a
//    decimal point in every REAL ("1." not "1") and an upper-case exponent.
std::string Real(double Value)
{
    if (Value == 0.0) return "0.";
    char Text[64];
    const auto Result = std::to_chars(Text, Text + sizeof(Text), Value);
    std::string Out(Text, Result.ptr);
    const size_t Exponent = Out.find('e');
    if (Out.find('.') == std::string::npos) Out.insert(Exponent == std::string::npos ? Out.size() : Exponent, ".");
    for (char& Letter : Out)
        if (Letter == 'e') Letter = 'E';
    return Out;
}

// Part 21 string: apostrophes doubled, backslashes doubled, anything outside printable ASCII replaced.
std::string Quote(const std::string& Value)
{
    std::string Out = "'";
    for (const char Letter : Value)
    {
        if (Letter == '\'') Out += "''";
        else if (Letter == '\\') Out += "\\\\";
        else if (Letter >= 32 && Letter <= 126) Out += Letter;
        else Out += '_';
    }
    return Out + "'";
}

std::string Flag(bool Value) { return Value ? ".T." : ".F."; }

// Appends one item to a Part 21 list being built ("(" before the first, "," before the rest; the caller closes it).
void AppendItem(std::string& List, const std::string& Item)
{
    List += List.empty() ? '(' : ',';
    List += Item;
}

// 📝 Distinct knots and multiplicities of a clamped knot vector (STEP's B_SPLINE_…_WITH_KNOTS form).
struct KnotForm
{
    std::string Multiplicities = {};                                                         // [-] - "(4,1,4)"
    std::string Values         = {};                                                         // [-] - "(0.,0.5,1.)"
};

KnotForm DescribeKnots(const std::vector<double>& Knots)
{
    KnotForm Out;
    for (size_t Index = 0; Index < Knots.size();)
    {
        size_t Run = Index;
        while (Run < Knots.size() && Knots[Run] == Knots[Index]) ++Run;
        AppendItem(Out.Multiplicities, std::to_string(Run - Index));
        AppendItem(Out.Values, Real(Knots[Index]));
        Index = Run;
    }
    return { Out.Multiplicities + ")", Out.Values + ")" };
}

// 💡 Normalisation is not idempotent in floating point (N(N(v)) may differ from N(v) in the last bit), so a vector that is
//    already unit to rounding is written as it is: a direction read from a file then goes out bit-identical, and export →
//    import → export reaches a fixed point.
Vector3 KeepUnit(Vector3 Value)
{
    // An axis direction one ulp short of unit (−0.9999999999999999, 0, 0) is written as the exact axis: idempotent like
    // keeping, and what a reader of the file expects to see.
    const int Zeros = (Value.X == 0.0) + (Value.Y == 0.0) + (Value.Z == 0.0);
    if (Zeros == 2 && std::fabs(Dot(Value, Value) - 1.0) <= 4.0e-16)
        return { Value.X == 0.0 ? 0.0 : std::copysign(1.0, Value.X), Value.Y == 0.0 ? 0.0 : std::copysign(1.0, Value.Y), Value.Z == 0.0 ? 0.0 : std::copysign(1.0, Value.Z) };
    return std::fabs(Dot(Value, Value) - 1.0) <= 4.0e-16 ? Value : Normalize(Value);
}

bool RationalCondition(const std::vector<double>& Weights)
{
    return std::any_of(Weights.begin(), Weights.end(), [](double Weight) { return Weight != 1.0; });
}

// 💡 Sense of a STEP carrier at a point, so ADVANCED_FACE.same_sense can be decided by comparison with the face's outward
//    normal rather than by trusting that both conventions agree.
struct StepCarrier
{
    int                         Instance = 0;                                                // [-] - #n of the surface
    std::function<Vector3(Vector3, Vector2)> Normal = {};                                    // [-] - STEP normal at (position, parameter)
};

class StepWriter
{
public:
    explicit StepWriter(double Scale) : Scale(Scale) {}

    int Add(const std::string& Body)
    {
        Data += '#';
        Data += std::to_string(Next);
        Data += '=';
        Data += Body;
        Data += ";\n";
        return Next++;
    }

    static std::string Ref(int Instance)
    {
        std::string Out = "#";
        Out += std::to_string(Instance);
        return Out;
    }

    std::string Coordinates(Vector3 Value) const
    {
        std::string Out = "(";
        Out += Real(Value.X);
        Out += ',';
        Out += Real(Value.Y);
        Out += ',';
        Out += Real(Value.Z);
        Out += ')';
        return Out;
    }

    int Point(Vector3 Position) { return Add("CARTESIAN_POINT(''," + Coordinates(Position * Scale) + ")"); }
    int Direction(Vector3 Value) { return Add("DIRECTION(''," + Coordinates(KeepUnit(Value)) + ")"); }

    int Placement(Vector3 Origin, Vector3 Axis, Vector3 Reference)
    {
        const int Location = Point(Origin);
        const int AxisInstance = Direction(Axis);
        const int ReferenceInstance = Direction(Reference);
        return Add("AXIS2_PLACEMENT_3D(''," + Ref(Location) + "," + Ref(AxisInstance) + "," + Ref(ReferenceInstance) + ")");
    }

    int BsplineCurve(const NurbsCurveDefinition& Definition)
    {
        std::string Poles;
        for (const Vector3& Pole : Definition.Poles) AppendItem(Poles, Ref(Point(Pole)));
        Poles += ")";
        const KnotForm Knots = DescribeKnots(Definition.Knots);
        const std::string Degree = std::to_string(Definition.Degree);
        if (!RationalCondition(Definition.Weights))
            return Add("B_SPLINE_CURVE_WITH_KNOTS(''," + Degree + "," + Poles + ",.UNSPECIFIED.,.F.,.F.," + Knots.Multiplicities + "," + Knots.Values + ",.UNSPECIFIED.)");
        std::string Weights;
        for (const double Weight : Definition.Weights) AppendItem(Weights, Real(Weight));
        return Add("( BOUNDED_CURVE() B_SPLINE_CURVE(" + Degree + "," + Poles + ",.UNSPECIFIED.,.F.,.F.) B_SPLINE_CURVE_WITH_KNOTS(" + Knots.Multiplicities + "," + Knots.Values
                   + ",.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE(" + Weights + ")) REPRESENTATION_ITEM('') )");
    }

    int BsplineSurface(const NurbsSurfaceDefinition& Definition)
    {
        std::string Poles = "(", Weights = "(";
        for (int Row = 0; Row < Definition.CountU; ++Row)
        {
            Poles += Row ? ",(" : "(";
            Weights += Row ? ",(" : "(";
            for (int Column = 0; Column < Definition.CountV; ++Column)
            {
                const size_t At = static_cast<size_t>(Row) * Definition.CountV + Column;
                if (Column) { Poles += ','; Weights += ','; }
                Poles += Ref(Point(Definition.Poles[At]));
                Weights += Real(Definition.Weights[At]);
            }
            Poles += ")";
            Weights += ")";
        }
        Poles += ")";
        Weights += ")";
        const KnotForm KnotsU = DescribeKnots(Definition.KnotsU), KnotsV = DescribeKnots(Definition.KnotsV);
        const std::string Degrees = std::to_string(Definition.DegreeU) + "," + std::to_string(Definition.DegreeV);
        if (!RationalCondition(Definition.Weights))
            return Add("B_SPLINE_SURFACE_WITH_KNOTS(''," + Degrees + "," + Poles + ",.UNSPECIFIED.,.F.,.F.,.F.," + KnotsU.Multiplicities + "," + KnotsV.Multiplicities + ","
                       + KnotsU.Values + "," + KnotsV.Values + ",.UNSPECIFIED.)");
        return Add("( BOUNDED_SURFACE() B_SPLINE_SURFACE(" + Degrees + "," + Poles + ",.UNSPECIFIED.,.F.,.F.,.F.) B_SPLINE_SURFACE_WITH_KNOTS(" + KnotsU.Multiplicities + ","
                   + KnotsV.Multiplicities + "," + KnotsU.Values + "," + KnotsV.Values + ",.UNSPECIFIED.) GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_SURFACE(" + Weights
                   + ") REPRESENTATION_ITEM('') SURFACE() )");
    }

    std::string Data  = {};                                                                  // [-] - DATA section lines
    int         Next  = 1;                                                                   // [-] - Next instance number
    double      Scale = 1.0;                                                                 // [mm] - Per model unit
};

Vector3 SplineNormal(const NurbsSurfaceDefinition& Definition, Vector2 Parameter)
{
    const NurbsSurfaceDerivatives Derivatives = EvaluateNurbsSurfaceDerivatives(Definition, Parameter);
    return Cross(Derivatives.U, Derivatives.V);
}

// 📝 A piecewise-Bézier segment joined back into one B-spline: knots at the piece ends with multiplicity p (C0 joints,
//    exact), clamped at both ends. Used for periodic NURBS edges whose interval runs across the knot seam.
NurbsCurveDefinition JoinBezierPieces(const std::vector<NurbsBezierPiece>& Pieces)
{
    NurbsCurveDefinition Out;
    Out.Degree = static_cast<int>(Pieces.front().Poles.size()) - 1;
    for (int Repeat = 0; Repeat <= Out.Degree; ++Repeat) Out.Knots.push_back(Pieces.front().PieceLow);
    for (size_t Piece = 0; Piece < Pieces.size(); ++Piece)
    {
        for (size_t Index = Piece ? 1 : 0; Index < Pieces[Piece].Poles.size(); ++Index)
        {
            Out.Poles.push_back(Pieces[Piece].Poles[Index]);
            Out.Weights.push_back(Pieces[Piece].Weights[Index]);
        }
        const int Repeats = Piece + 1 < Pieces.size() ? Out.Degree : Out.Degree + 1;
        for (int Repeat = 0; Repeat < Repeats; ++Repeat) Out.Knots.push_back(Pieces[Piece].PieceHigh);
    }
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

StepExport WriteStep(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const StepRequest& Request)
{
    const auto Start = std::chrono::steady_clock::now();
    StepExport Out;
    const auto Finish = [&]() -> StepExport
    {
        Out.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        if (!Out.Refusal.empty()) Out.Bytes.clear();
        return Out;
    };
    if (Bodies.empty()) { Out.Refusal = "STEP: no body to export"; return Finish(); }
    if (!(Request.ModelUnitMillimetres > 0.0) || !std::isfinite(Request.ModelUnitMillimetres)) { Out.Refusal = "STEP: the model unit must be a positive length"; return Finish(); }
    if (!(Request.ApproximationTolerance > 0.0)) { Out.Refusal = "STEP: the approximation tolerance must be positive"; return Finish(); }
    for (const BodyToken Body : Bodies)
    {
        if (!Partition.Bodies.Authenticate(Body)) { Out.Refusal = "STEP: a body is not in the partition"; return Finish(); }
        if (Partition.Bodies.Resolve(Body).Category != BodyCategory::Solid) { Out.Refusal = "STEP: only solid bodies are exported (a sheet or wire body was given)"; return Finish(); }
    }

    StepWriter Writer(Request.ModelUnitMillimetres);
    ApproximationSettings Approximation;
    Approximation.Tolerance = Request.ApproximationTolerance;

    // ---- context and units first, so the representation can refer back to them
    const int Application = Writer.Add("APPLICATION_CONTEXT('automotive_design')");
    Writer.Add("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000," + StepWriter::Ref(Application) + ")");
    const int ProductContext = Writer.Add("PRODUCT_CONTEXT(''," + StepWriter::Ref(Application) + ",'mechanical')");
    const int Product = Writer.Add("PRODUCT(" + Quote(Request.Name) + "," + Quote(Request.Name) + ",''," + "(" + StepWriter::Ref(ProductContext) + "))");
    Writer.Add("PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(" + StepWriter::Ref(Product) + "))");
    const int Formation = Writer.Add("PRODUCT_DEFINITION_FORMATION('',''," + StepWriter::Ref(Product) + ")");
    const int DefinitionContext = Writer.Add("PRODUCT_DEFINITION_CONTEXT('part definition'," + StepWriter::Ref(Application) + ",'design')");
    const int Definition = Writer.Add("PRODUCT_DEFINITION('design',''," + StepWriter::Ref(Formation) + "," + StepWriter::Ref(DefinitionContext) + ")");
    const int Shape = Writer.Add("PRODUCT_DEFINITION_SHAPE(''," + StepWriter::Ref(Definition) + ")");
    const int LengthUnit = Writer.Add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )");
    const int AngleUnit = Writer.Add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )");
    const int SolidAngleUnit = Writer.Add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )");
    const int Uncertainty = Writer.Add("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(" + Real(Request.ApproximationTolerance * Request.ModelUnitMillimetres * 10.0) + "),"
                                       + StepWriter::Ref(LengthUnit) + ",'distance_accuracy_value','confusion accuracy')");
    const int Context = Writer.Add("( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((" + StepWriter::Ref(Uncertainty) + ")) GLOBAL_UNIT_ASSIGNED_CONTEXT(("
                                   + StepWriter::Ref(LengthUnit) + "," + StepWriter::Ref(AngleUnit) + "," + StepWriter::Ref(SolidAngleUnit)
                                   + ")) REPRESENTATION_CONTEXT('Context3D','3D Context with UNIT and UNCERTAINTY') )");
    std::vector<int> Items = { Writer.Placement({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }) };

    for (const BodyToken Body : Bodies)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        std::map<uint32_t, int>                    VertexInstances, EdgeInstances;
        std::map<uint32_t, StepCarrier>            SurfaceInstances;

        // ---- vertices and edges, shared by every face that uses them
        for (const VertexToken Vertex : Inventory.Vertices)
        {
            VertexInstances[Vertex.Slot] = Writer.Add("VERTEX_POINT(''," + StepWriter::Ref(Writer.Point(Partition.Vertices.Resolve(Vertex).Position)) + ")");
            ++Out.Vertices;
        }
        for (const EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            const double         Low     = Profile.IntervalLow, High = Profile.IntervalHigh;
            int  CurveInstance = 0;
            bool SameSense     = true;
            if (const auto* Line = std::get_if<StraightLine>(&Curve))
            {
                const int Direction = Writer.Direction(Line->Direction);
                const int Vector    = Writer.Add("VECTOR(''," + StepWriter::Ref(Direction) + "," + Real(Request.ModelUnitMillimetres) + ")");
                CurveInstance = Writer.Add("LINE(''," + StepWriter::Ref(Writer.Point(Line->Origin)) + "," + StepWriter::Ref(Vector) + ")");
                ++Out.ExactCurves;
            }
            else if (const auto* Circle = std::get_if<CircularArc>(&Curve))
            {
                const int Placement = Writer.Placement(Circle->Frame.Origin, Cross(Circle->Frame.XAxis, Circle->Frame.YAxis), Circle->Frame.XAxis);
                CurveInstance = Writer.Add("CIRCLE(''," + StepWriter::Ref(Placement) + "," + Real(Circle->Radius * Request.ModelUnitMillimetres) + ")");
                ++Out.ExactCurves;
            }
            else if (const auto* Ellipse = std::get_if<EllipticalArc>(&Curve))
            {
                const int Placement = Writer.Placement(Ellipse->Frame.Origin, Cross(Ellipse->Frame.XAxis, Ellipse->Frame.YAxis), Ellipse->Frame.XAxis);
                CurveInstance = Writer.Add("ELLIPSE(''," + StepWriter::Ref(Placement) + "," + Real(Ellipse->MajorRadius * Request.ModelUnitMillimetres) + ","
                                           + Real(Ellipse->MinorRadius * Request.ModelUnitMillimetres) + ")");
                ++Out.ExactCurves;
            }
            else
            {
                std::optional<NurbsCurveDefinition> Spline;
                const auto* Exact = std::get_if<NurbsCurve>(&Curve);
                if (Exact)
                {
                    const NurbsDomain Domain = MeasureNurbsCurveDomain(*Exact->Definition);
                    const double      Slack  = 1.0e-12 * std::max(1.0, Domain.High - Domain.Low);
                    if (Low >= Domain.Low - Slack && High <= Domain.High + Slack) Spline = *Exact->Definition;
                    else Spline = JoinBezierPieces(DecomposeNurbsCurve(*Exact->Definition, Low, High));
                    ++Out.ExactCurves;
                }
                else
                {
                    const CurveApproximation Fit = ApproximateCurveAsNurbs(Curve, Low, High, Approximation);
                    if (!Fit.Curve) { Out.Refusal = "STEP: an edge curve could not be approximated (" + Fit.Refusal + ")"; return Finish(); }
                    Spline = *Fit.Curve->Definition;
                    Out.Deviation = std::max(Out.Deviation, Fit.Deviation);
                    ++Out.ApproximatedCurves;
                    // The stand-in must run the same way as the edge (tangent at the start against the exact one).
                    const NurbsDomain Domain = MeasureNurbsCurveDomain(*Spline);
                    SameSense = Dot(EvaluateNurbsCurveDerivatives(*Spline, Domain.Low).First, EvaluateCurve(Curve, Low).Derivative) > 0.0;
                }
                CurveInstance = Writer.BsplineCurve(*Spline);
            }
            EdgeInstances[Edge.Slot] = Writer.Add("EDGE_CURVE(''," + StepWriter::Ref(VertexInstances[Profile.Start.Slot]) + "," + StepWriter::Ref(VertexInstances[Profile.End.Slot]) + ","
                                                  + StepWriter::Ref(CurveInstance) + "," + Flag(SameSense) + ")");
            ++Out.Edges;
        }

        // ---- outer versus cavity shells: signed divergence volume of a coarse tessellation (faces point out of the material)
        TessellationSettings Coarse;
        Coarse.SagRatio = 2.0e-2;
        const TessellationOutcome Tessellation = TessellateBody(Partition, Body, Coarse);
        if (!Tessellation.Refusal.empty()) { Out.Refusal = "STEP: shell classification failed (" + Tessellation.Refusal + ")"; return Finish(); }
        std::map<uint32_t, double> FaceVolumes;
        for (size_t Triangle = 0; Triangle < Tessellation.Complex.Triangles.size(); ++Triangle)
        {
            const std::array<int, 3>& Corners = Tessellation.Complex.Triangles[Triangle];
            const Vector3& First = Tessellation.Complex.Positions[Corners[0]];
            FaceVolumes[Tessellation.Complex.Faces[Tessellation.Complex.TriangleFaces[Triangle]].Face.Slot]
                += Dot(First, Cross(Tessellation.Complex.Positions[Corners[1]], Tessellation.Complex.Positions[Corners[2]])) / 6.0;
        }

        // ---- faces, shell by shell, region by region
        for (const RegionToken Region : Inventory.Regions)
        {
            const RegionProfile& RegionRecord = Partition.Regions.Resolve(Region);
            if (RegionRecord.Material != RegionMaterial::Solid || RegionRecord.Shells.empty()) continue;
            int              OuterShell = 0;
            std::vector<int> VoidShells;
            for (const SolidShellToken Shell : RegionRecord.Shells)
            {
                double Volume = 0.0;
                for (const FaceToken Face : Partition.Shells.Resolve(Shell).Faces) Volume += FaceVolumes[Face.Slot];
                const bool VoidCondition = Volume < 0.0;
                std::string FaceList;
                for (const FaceToken Face : Partition.Shells.Resolve(Shell).Faces)
                {
                    const FaceProfile&     FaceRecord = Partition.Faces.Resolve(Face);
                    const SurfaceGeometry& Surface    = Partition.Surfaces.Resolve(FaceRecord.Surface);
                    const FaceDomain       Domain     = DescribeFaceDomain(Partition, Face);
                    const std::optional<Vector2> Interior = LocateFaceInterior(Domain);
                    if (!Interior) { Out.Refusal = "STEP: a face has no interior point to orient it"; return Finish(); }

                    // Carrier: exact where STEP has the type, else a certified cubic stand-in over the face's chart window.
                    StepCarrier Carrier;
                    const auto Exact = SurfaceInstances.find(FaceRecord.Surface.Slot);
                    if (Exact != SurfaceInstances.end()) Carrier = Exact->second;
                    else if (const auto* Plane = std::get_if<PlaneSurface>(&Surface))
                    {
                        const Vector3 Axis = Plane->Frame.ZAxis;
                        Carrier = { Writer.Add("PLANE(''," + StepWriter::Ref(Writer.Placement(Plane->Frame.Origin, Axis, Plane->Frame.XAxis)) + ")"), [Axis](Vector3, Vector2) { return Axis; } };
                    }
                    else if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface))
                    {
                        const OrthonormalBasis Frame = Cylinder->Frame;
                        Carrier = { Writer.Add("CYLINDRICAL_SURFACE(''," + StepWriter::Ref(Writer.Placement(Frame.Origin, Frame.ZAxis, Frame.XAxis)) + "," + Real(Cylinder->Radius * Request.ModelUnitMillimetres) + ")"),
                                    [Frame](Vector3 Position, Vector2) { const Vector3 Offset = Position - Frame.Origin; return Offset - Frame.ZAxis * Dot(Offset, Frame.ZAxis); } };
                    }
                    else if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
                    {
                        if (Cone->RadialSlope == 0.0) { Out.Refusal = "STEP: a cone with zero slope"; return Finish(); }
                        const Vector3 Axis   = Cone->Frame.ZAxis * (Cone->RadialSlope > 0.0 ? 1.0 : -1.0);
                        const double  Angle  = std::atan(std::fabs(Cone->RadialSlope));
                        const double  Height = Cone->OriginRadius >= 0.0 ? 0.0 : -Cone->OriginRadius / Cone->RadialSlope;
                        const Vector3 Origin = Cone->Frame.Origin + Cone->Frame.ZAxis * Height;
                        const double  Radius = std::max(0.0, Cone->OriginRadius + Cone->RadialSlope * Height);
                        Carrier = { Writer.Add("CONICAL_SURFACE(''," + StepWriter::Ref(Writer.Placement(Origin, Axis, Cone->Frame.XAxis)) + "," + Real(Radius * Request.ModelUnitMillimetres) + "," + Real(Angle) + ")"),
                                    [Origin, Axis, Angle](Vector3 Position, Vector2)
                                    {
                                        const Vector3 Offset = Position - Origin;
                                        const Vector3 Radial = Normalize(Offset - Axis * Dot(Offset, Axis));
                                        return Radial * std::cos(Angle) - Axis * std::sin(Angle);
                                    } };
                    }
                    else if (const auto* Sphere = std::get_if<SphereSurface>(&Surface))
                    {
                        const Vector3 Centre = Sphere->Frame.Origin;
                        Carrier = { Writer.Add("SPHERICAL_SURFACE(''," + StepWriter::Ref(Writer.Placement(Centre, Sphere->Frame.ZAxis, Sphere->Frame.XAxis)) + "," + Real(Sphere->Radius * Request.ModelUnitMillimetres) + ")"),
                                    [Centre](Vector3 Position, Vector2) { return Position - Centre; } };
                    }
                    else if (const auto* Torus = std::get_if<TorusSurface>(&Surface))
                    {
                        const OrthonormalBasis Frame = Torus->Frame;
                        const double           Major = Torus->MajorRadius;
                        Carrier = { Writer.Add("TOROIDAL_SURFACE(''," + StepWriter::Ref(Writer.Placement(Frame.Origin, Frame.ZAxis, Frame.XAxis)) + "," + Real(Major * Request.ModelUnitMillimetres) + ","
                                               + Real(Torus->MinorRadius * Request.ModelUnitMillimetres) + ")"),
                                    [Frame, Major](Vector3 Position, Vector2)
                                    {
                                        const Vector3 Offset = Position - Frame.Origin;
                                        return Position - (Frame.Origin + Normalize(Offset - Frame.ZAxis * Dot(Offset, Frame.ZAxis)) * Major);
                                    } };
                    }
                    else
                    {
                        const auto* Spline = std::get_if<NurbsSurface>(&Surface);
                        bool InsideCondition = false;
                        if (Spline)
                        {
                            const NurbsDomain AlongU = MeasureNurbsSurfaceDomain(*Spline->Definition, true), AlongV = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
                            // DescribeFaceDomain widens a curved chart window by 10⁻³ of its span (+ 10⁻⁹) for the bulge between
                            // samples, so a face ending on its knot domain shows a window that far beyond it.
                            const double SlackU = 1.01e-3 * (Domain.Window.High.X - Domain.Window.Low.X) + 2.0e-9 * std::max(1.0, AlongU.High - AlongU.Low);
                            const double SlackV = 1.01e-3 * (Domain.Window.High.Y - Domain.Window.Low.Y) + 2.0e-9 * std::max(1.0, AlongV.High - AlongV.Low);
                            // A periodic chart window may sit whole periods away from the knot domain: the same patch.
                            const auto Shift = [](double Low, double Period, double DomainLow, double Slack)
                            {
                                return Period > 0.0 ? std::floor((Low - DomainLow + Slack) / Period) * Period : 0.0;
                            };
                            const double ShiftU = Shift(Domain.Window.Low.X, Spline->Definition->PeriodU, AlongU.Low, SlackU);
                            const double ShiftV = Shift(Domain.Window.Low.Y, Spline->Definition->PeriodV, AlongV.Low, SlackV);
                            InsideCondition = Domain.Window.Low.X - ShiftU >= AlongU.Low - SlackU && Domain.Window.High.X - ShiftU <= AlongU.High + SlackU
                                              && Domain.Window.Low.Y - ShiftV >= AlongV.Low - SlackV && Domain.Window.High.Y - ShiftV <= AlongV.High + SlackV;
                        }
                        if (Spline && InsideCondition)
                        {
                            const std::shared_ptr<const NurbsSurfaceDefinition> Shared = Spline->Definition;
                            Carrier = { Writer.BsplineSurface(*Shared), [Shared](Vector3, Vector2 Parameter) { return SplineNormal(*Shared, Parameter); } };
                            ++Out.ExactSurfaces;
                        }
                        else
                        {
                            const SurfaceApproximation Fit = ApproximateSurfaceAsNurbs(Surface, Domain.Window.Low, Domain.Window.High, Approximation);
                            if (!Fit.Surface) { Out.Refusal = "STEP: a face carrier could not be approximated (" + Fit.Refusal + ")"; return Finish(); }
                            const std::shared_ptr<const NurbsSurfaceDefinition> Shared = Fit.Surface->Definition;
                            Carrier = { Writer.BsplineSurface(*Shared), [Shared](Vector3, Vector2 Parameter) { return SplineNormal(*Shared, Parameter); } };
                            Out.Deviation = std::max(Out.Deviation, Fit.Deviation);
                            ++Out.ApproximatedSurfaces;
                        }
                    }
                    if (Exact == SurfaceInstances.end() && Surface.index() <= 4)
                    {
                        SurfaceInstances[FaceRecord.Surface.Slot] = Carrier;
                        ++Out.ExactSurfaces;
                    }

                    // Orientation: STEP normal against the face's outward normal at an interior point.
                    const Vector3 Position  = EvaluateSurface(Surface, *Interior).Position;
                    const Vector3 Outward   = EvaluateSurfaceNormal(Surface, *Interior) * (FaceRecord.Sense == Orientation::Forward ? 1.0 : -1.0);
                    bool          SameSense = Dot(Carrier.Normal(Position, *Interior), Outward) > 0.0;

                    // A cavity shell is written reversed and used through ORIENTED_CLOSED_SHELL(.F.) (ISO 10303-42 brep_with_voids).
                    if (VoidCondition) SameSense = !SameSense;
                    std::string Bounds;
                    for (const LoopToken Loop : FaceRecord.Loops)
                    {
                        std::string Uses;
                        bool        EmptyCondition = true;
                        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                        {
                            const FinProfile& FinRecord = Partition.Fins.Resolve(Fin);
                            AppendItem(Uses, StepWriter::Ref(Writer.Add("ORIENTED_EDGE('',*,*," + StepWriter::Ref(EdgeInstances[FinRecord.Edge.Slot]) + "," + Flag(FinRecord.Sense == Orientation::Forward) + ")")));
                            EmptyCondition = false;
                        });
                        if (EmptyCondition) { Out.Refusal = "STEP: a face loop without edges"; return Finish(); }
                        const int EdgeLoop = Writer.Add("EDGE_LOOP(''," + Uses + "))");
                        AppendItem(Bounds, StepWriter::Ref(Writer.Add("FACE_BOUND(''," + StepWriter::Ref(EdgeLoop) + "," + Flag(!VoidCondition) + ")")));
                    }
                    AppendItem(FaceList, StepWriter::Ref(Writer.Add("ADVANCED_FACE(''," + Bounds + ")," + StepWriter::Ref(Carrier.Instance) + "," + Flag(SameSense) + ")")));
                    ++Out.Faces;
                }
                const int Closed = Writer.Add("CLOSED_SHELL(''," + FaceList + "))");
                if (!VoidCondition)
                {
                    if (OuterShell) { Out.Refusal = "STEP: a solid region with two outer shells"; return Finish(); }
                    OuterShell = Closed;
                }
                else VoidShells.push_back(Writer.Add("ORIENTED_CLOSED_SHELL('',*," + StepWriter::Ref(Closed) + ",.F.)"));
            }
            if (!OuterShell) { Out.Refusal = "STEP: a solid region without an outer shell"; return Finish(); }
            if (VoidShells.empty()) Items.push_back(Writer.Add("MANIFOLD_SOLID_BREP(''," + StepWriter::Ref(OuterShell) + ")"));
            else
            {
                std::string Voids;
                for (const int Void : VoidShells) AppendItem(Voids, StepWriter::Ref(Void));
                Items.push_back(Writer.Add("BREP_WITH_VOIDS(''," + StepWriter::Ref(OuterShell) + "," + Voids + "))"));
                Out.Voids += static_cast<int>(VoidShells.size());
            }
            ++Out.Solids;
        }
    }

    std::string ItemList;
    for (const int Item : Items) AppendItem(ItemList, StepWriter::Ref(Item));
    const int Representation = Writer.Add("ADVANCED_BREP_SHAPE_REPRESENTATION(" + Quote(Request.Name) + "," + ItemList + ")," + StepWriter::Ref(Context) + ")");
    Writer.Add("SHAPE_DEFINITION_REPRESENTATION(" + StepWriter::Ref(Shape) + "," + StepWriter::Ref(Representation) + ")");

    const std::string Timestamp = Request.Timestamp.empty() ? "1970-01-01T00:00:00" : Request.Timestamp;
    Out.Bytes = "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('Frontier GeometryProcessing boundary representation'),'2;1');\nFILE_NAME(" + Quote(Request.Name + ".step") + "," + Quote(Timestamp)
                + ",(''),(''),'Frontier GeometryProcessing','Frontier GeometryProcessing','');\nFILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n" + Writer.Data
                + "ENDSEC;\nEND-ISO-10303-21;\n";
    Out.Instances = Writer.Next - 1;
    return Finish();
}

std::string ExportStepFile(const StepExport& Export, const std::string& Path)
{
    if (!Export.Refusal.empty()) return "STEP: nothing to write (" + Export.Refusal + ")";
    std::ofstream Stream(ResolveUtf8Path(Path), std::ios::binary);
    if (!Stream) return "STEP: cannot open " + Path;
    Stream.write(Export.Bytes.data(), static_cast<std::streamsize>(Export.Bytes.size()));
    return Stream ? std::string() : "STEP: write failed for " + Path;
}

} // namespace Frontier::GeometrySpace::Parametric
