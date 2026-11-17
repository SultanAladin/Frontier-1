/*====================================================================================================================================
                                                       SOLIDARCMEASUREMENT.CPP
====================================================================================================================================*/
// 🧩 Inspect: selection → kernel measurement targets, Inspector rows, multi-body plane sections

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMeasurement.h"

#include "Engine/GeometrySpace/Parametric/Assembly/AssemblyAnalysis.h"
#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MinimumDistance.h"
#include "Engine/GeometrySpace/Parametric/Measurement/PlaneSection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <optional>
#include <variant>

namespace Frontier::Workspaces::Sketcher
{

namespace
{

using GeometrySpace::Parametric::Vector3;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A selected face or edge resolved to its partition token, its owning body and an optional direction for angles.
struct MeasureOperand
{
    const GeometrySpace::Parametric::RegeneratedBody* Owner     = nullptr;                                   // [-] - Owning body
    GeometrySpace::Parametric::MeasureTarget          Target    = {};                                        // [-] - Kernel token
    std::string                      Name      = {};                                        // [-] - Persistent name
    bool                             FaceCondition = false;                                 // [-] - Face (else edge)
    std::optional<Vector3>           Normal    = {};                                        // [-] - Planar face: outward unit normal
    std::optional<Vector3>           Line      = {};                                        // [-] - Straight edge / cylinder or cone axis
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORMATTING
//------------------------------------------------------------------------------------------------------------------------

std::string Figure(const char* Pattern, ...)
{
    char Text[256];
    va_list Arguments;
    va_start(Arguments, Pattern);
    std::vsnprintf(Text, sizeof(Text), Pattern, Arguments);
    va_end(Arguments);
    return Text;
}

std::string FigurePoint(Vector3 Point)
{
    return Figure("(%.4f, %.4f, %.4f)", Point.X, Point.Y, Point.Z);
}

void AppendRow(SketcherMeasurement& Measurement, const std::string& Caption, const std::string& Value, double Number = SketcherUnmeasured)
{
    Measurement.Rows.push_back({ Caption, Value, Number });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

bool HiddenCondition(const SketcherDocument& Document, const std::string& Key)
{
    return Document.Appearance.HiddenBodies.contains(Key);
}

bool SheetBodyCondition(const SketcherDocument& Document, const std::string& Key)
{
    for (const SketcherBodyFigures& Figures : Document.Summary.Bodies) if (Figures.Key == Key) return Figures.SheetCondition;
    return false;
}

const GeometrySpace::Parametric::RegeneratedBody* FindBody(const SketcherDocument& Document, const std::string& Key)
{
    for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies) if (Body.Key == Key) return &Body;
    return nullptr;
}

std::string BodyCaptionOf(const SketcherDocument& Document, const std::string& Key)
{
    for (const SketcherBodyFigures& Figures : Document.Summary.Bodies) if (Figures.Key == Key) return Figures.Caption.empty() ? Key : Figures.Caption;
    return Key;
}

// Carrier caption with its defining parameters ("Cylinder r 0.5000").
std::string DescribeSurface(const GeometrySpace::Parametric::SurfaceGeometry& Surface)
{
    if (const auto* Cylinder = std::get_if<GeometrySpace::Parametric::CylinderSurface>(&Surface)) return Figure("Cylinder r %.4f", Cylinder->Radius);
    if (const auto* Sphere = std::get_if<GeometrySpace::Parametric::SphereSurface>(&Surface)) return Figure("Sphere r %.4f", Sphere->Radius);
    if (const auto* Torus = std::get_if<GeometrySpace::Parametric::TorusSurface>(&Surface)) return Figure("Torus R %.4f r %.4f", Torus->MajorRadius, Torus->MinorRadius);
    if (const auto* Cone = std::get_if<GeometrySpace::Parametric::ConeSurface>(&Surface))
        return Figure("Cone half-angle %.3f°", std::atan(std::fabs(Cone->RadialSlope)) * 180.0 / 3.14159265358979323846);
    return GeometrySpace::Parametric::SurfaceCategoryLabel(Surface);
}

std::string DescribeCurve(const GeometrySpace::Parametric::CurveGeometry& Curve, double IntervalLow, double IntervalHigh)
{
    if (const auto* Arc = std::get_if<GeometrySpace::Parametric::CircularArc>(&Curve))
    {
        const double Sweep = std::fabs(IntervalHigh - IntervalLow) * 180.0 / 3.14159265358979323846;
        return Sweep > 359.999 ? Figure("Circle r %.4f", Arc->Radius) : Figure("Arc r %.4f · %.2f°", Arc->Radius, Sweep);
    }
    if (const auto* Ellipse = std::get_if<GeometrySpace::Parametric::EllipticalArc>(&Curve)) return Figure("Ellipse a %.4f b %.4f", Ellipse->MajorRadius, Ellipse->MinorRadius);
    return GeometrySpace::Parametric::CurveCategoryLabel(Curve);
}

// Persistent face / edge names → partition tokens of the shown bodies, in the selection's order (faces, then edges).
std::vector<MeasureOperand> ResolveOperands(const SketcherDocument& Document)
{
    std::vector<MeasureOperand> Operands;
    const GeometrySpace::Parametric::ModelPartition& Partition = *Document.Partition;
    for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies)
    {
        if (HiddenCondition(Document, Body.Key) || !Body.Body.PresentCondition()) continue;
        const GeometrySpace::Parametric::BodyInventory Inventory = Partition.CollectInventory(Body.Body);
        for (const GeometrySpace::Parametric::FaceToken Face : Inventory.Faces)
        {
            const auto Found = Body.Naming.Faces.find(Face.Slot);
            if (Found == Body.Naming.Faces.end()) continue;
            if (std::find(Document.Selection.Faces.begin(), Document.Selection.Faces.end(), Found->second) == Document.Selection.Faces.end()) continue;
            MeasureOperand Operand{ &Body, Face, Found->second, true };
            const GeometrySpace::Parametric::FaceProfile& Profile = Partition.Faces.Resolve(Face);
            if (Profile.Surface.PresentCondition())
            {
                const GeometrySpace::Parametric::SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
                const double Sense = Profile.Sense == GeometrySpace::Parametric::Orientation::Forward ? 1.0 : -1.0;
                if (const auto* Plane = std::get_if<GeometrySpace::Parametric::PlaneSurface>(&Surface)) Operand.Normal = Plane->Frame.ZAxis * Sense;
                if (const auto* Cylinder = std::get_if<GeometrySpace::Parametric::CylinderSurface>(&Surface)) Operand.Line = Cylinder->Frame.ZAxis;
                if (const auto* Cone = std::get_if<GeometrySpace::Parametric::ConeSurface>(&Surface)) Operand.Line = Cone->Frame.ZAxis;
            }
            Operands.push_back(Operand);
        }
        for (const GeometrySpace::Parametric::EdgeToken Edge : Inventory.Edges)
        {
            const auto Found = Body.Naming.Edges.find(Edge.Slot);
            if (Found == Body.Naming.Edges.end()) continue;
            if (std::find(Document.Selection.Edges.begin(), Document.Selection.Edges.end(), Found->second) == Document.Selection.Edges.end()) continue;
            MeasureOperand Operand{ &Body, Edge, Found->second, false };
            const GeometrySpace::Parametric::EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (Profile.Curve.PresentCondition())
                if (const auto* Line = std::get_if<GeometrySpace::Parametric::StraightLine>(&Partition.Curves.Resolve(Profile.Curve))) Operand.Line = Line->Direction;
            Operands.push_back(Operand);
        }
    }
    // Pick order within a kind (the selection vectors), faces before edges.
    const auto Rank = [&](const MeasureOperand& Operand)
    {
        const std::vector<std::string>& Names = Operand.FaceCondition ? Document.Selection.Faces : Document.Selection.Edges;
        return (Operand.FaceCondition ? 0 : 1 << 20) + int(std::find(Names.begin(), Names.end(), Operand.Name) - Names.begin());
    };
    std::sort(Operands.begin(), Operands.end(), [&](const MeasureOperand& First, const MeasureOperand& Second) { return Rank(First) < Rank(Second); });
    return Operands;
}

double FacePerimeter(const GeometrySpace::Parametric::ModelPartition& Partition, GeometrySpace::Parametric::FaceToken Face)
{
    double Perimeter = 0.0;
    for (const GeometrySpace::Parametric::LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
        Partition.EnumerateLoopFins(Loop, [&](GeometrySpace::Parametric::FinToken Fin) { Perimeter += GeometrySpace::Parametric::MeasureEdgeLength(Partition, Partition.Fins.Resolve(Fin).Edge); });
    return Perimeter;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SUBJECTS
//------------------------------------------------------------------------------------------------------------------------

void MeasureBodies(const SketcherDocument& Document, const std::vector<std::string>& Keys, SketcherMeasurement& Measurement)
{
    // 📝 Several bodies: totals, the volume-weighted centroid; inertia only for one body (unit density, about its centroid).
    double  Volume = 0.0, Area = 0.0;
    Vector3 Moment = {};
    std::optional<GeometrySpace::Parametric::BodyMassProperties> Single;
    int Sheets = 0;
    for (const std::string& Key : Keys)
    {
        const GeometrySpace::Parametric::RegeneratedBody* Body = FindBody(Document, Key);
        if (!Body) continue;
        if (SheetBodyCondition(Document, Key)) { ++Sheets; continue; }
        const GeometrySpace::Parametric::BodyMassProperties Mass = GeometrySpace::Parametric::MeasureBodyMass(*Document.Partition, Body->Body);
        if (!Mass.Refusal.empty()) { Measurement.Refusal = BodyCaptionOf(Document, Key) + ": " + Mass.Refusal; return; }
        Volume += Mass.Volume;
        Area   += Mass.SurfaceArea;
        Moment  = Moment + Mass.Centroid * Mass.Volume;
        if (Keys.size() == 1) Single = Mass;
    }
    if (!(Volume > 0.0))
    {
        Measurement.Refusal = Sheets ? "Measure: sheet bodies enclose no volume (select faces to measure their area)" : "Measure: no solid body";
        return;
    }
    Measurement.Subject  = Keys.size() == 1 ? "Body " + BodyCaptionOf(Document, Keys.front()) : Figure("%d bodies", int(Keys.size()) - Sheets);
    Measurement.Volume   = Volume;
    Measurement.Area     = Area;
    Measurement.Centroid = Moment * (1.0 / Volume);
    Measurement.CentroidCondition = true;
    AppendRow(Measurement, "Volume", Figure("%.6g m³", Volume), Volume);
    AppendRow(Measurement, "Surface area", Figure("%.6g m²", Area), Area);
    AppendRow(Measurement, "Centroid", FigurePoint(Measurement.Centroid));
    if (Single)
    {
        const std::array<double, 3> Principal = GeometrySpace::Parametric::ResolvePrincipalMoments(Single->Inertia);
        AppendRow(Measurement, "Principal I1 I2 I3", Figure("%.5g · %.5g · %.5g", Principal[0], Principal[1], Principal[2]), Principal[0]);
        AppendRow(Measurement, "Ixx Iyy Izz", Figure("%.5g · %.5g · %.5g", Single->Inertia[0], Single->Inertia[1], Single->Inertia[2]), Single->Inertia[0]);
    }
    if (Sheets) AppendRow(Measurement, "Skipped", Figure("%d sheet body(ies)", Sheets), Sheets);
}

// Two solid bodies: interference on a scratch copy of the partition (the overlap Boolean adds a body); the overlay gets
// the clearance's closest points or the edges of the shared volume, sampled 24 chords per edge.
void MeasureInterference(const SketcherDocument& Document, const std::string& FirstKey, const std::string& SecondKey, SketcherMeasurement& Measurement)
{
    const GeometrySpace::Parametric::RegeneratedBody* First  = FindBody(Document, FirstKey);
    const GeometrySpace::Parametric::RegeneratedBody* Second = FindBody(Document, SecondKey);
    if (!First || !Second || SheetBodyCondition(Document, FirstKey) || SheetBodyCondition(Document, SecondKey)) return;
    GeometrySpace::Parametric::ModelPartition Scratch = *Document.Partition;
    const GeometrySpace::Parametric::InterferenceMeasure Measure = GeometrySpace::Parametric::DetectBodyInterference(Scratch, First->Body, Second->Body);
    if (!Measure.Refusal.empty()) { AppendRow(Measurement, "Interference", Measure.Refusal); return; }
    Measurement.Interference = GeometrySpace::Parametric::InterferenceConditionLabel(Measure.Condition);
    Measurement.Subject      = BodyCaptionOf(Document, FirstKey) + " + " + BodyCaptionOf(Document, SecondKey);
    AppendRow(Measurement, "Interference", Measurement.Interference);
    switch (Measure.Condition)
    {
    case GeometrySpace::Parametric::InterferenceCondition::Clear:
    case GeometrySpace::Parametric::InterferenceCondition::Contact:
        Measurement.Distance          = Measure.Clearance;
        Measurement.DistanceCondition = Measure.Condition == GeometrySpace::Parametric::InterferenceCondition::Clear;
        Measurement.First             = Measure.First;
        Measurement.Second            = Measure.Second;
        AppendRow(Measurement, "Clearance", Figure("%.6g m", Measure.Clearance), Measure.Clearance);
        break;
    case GeometrySpace::Parametric::InterferenceCondition::Overlap:
    case GeometrySpace::Parametric::InterferenceCondition::Contained:
    {
        Measurement.OverlapVolume = Measure.Volume;
        AppendRow(Measurement, Measure.Condition == GeometrySpace::Parametric::InterferenceCondition::Overlap ? "Overlap volume" : "Contained volume", Figure("%.6g m³", Measure.Volume), Measure.Volume);
        for (GeometrySpace::Parametric::EdgeToken Edge : Scratch.CollectInventory(Measure.Body).Edges)
        {
            const GeometrySpace::Parametric::EdgeProfile&   Profile = Scratch.Edges.Resolve(Edge);
            const GeometrySpace::Parametric::CurveGeometry& Curve   = Scratch.Curves.Resolve(Profile.Curve);
            std::vector<Vector3> Polyline;
            for (int Index = 0; Index <= 24; ++Index)
                Polyline.push_back(GeometrySpace::Parametric::EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Index / 24.0).Position);
            Measurement.Overlap.push_back(std::move(Polyline));
        }
        break;
    }
    }
}

void MeasureFace(const SketcherDocument& Document, const MeasureOperand& Operand, SketcherMeasurement& Measurement)
{
    const GeometrySpace::Parametric::ModelPartition& Partition = *Document.Partition;
    const GeometrySpace::Parametric::FaceToken       Face      = std::get<GeometrySpace::Parametric::FaceToken>(Operand.Target);
    const GeometrySpace::Parametric::FaceAreaMeasure Area      = GeometrySpace::Parametric::MeasureFaceArea(Partition, Face);
    const double                    Perimeter = FacePerimeter(Partition, Face);
    const GeometrySpace::Parametric::FaceProfile&    Profile   = Partition.Faces.Resolve(Face);
    Measurement.Subject  = "Face " + Operand.Name;
    Measurement.Area     = Area.Area;
    Measurement.Length   = Perimeter;
    Measurement.Centroid = Area.Centroid;
    Measurement.CentroidCondition = true;
    AppendRow(Measurement, "Area", Figure("%.6g m²", Area.Area), Area.Area);
    AppendRow(Measurement, "Perimeter", Figure("%.6g m", Perimeter), Perimeter);
    if (Profile.Surface.PresentCondition()) AppendRow(Measurement, "Surface", DescribeSurface(Partition.Surfaces.Resolve(Profile.Surface)));
    if (Operand.Normal) AppendRow(Measurement, "Normal", FigurePoint(*Operand.Normal));
    AppendRow(Measurement, "Centroid", FigurePoint(Area.Centroid));
}

void MeasureEdge(const SketcherDocument& Document, const MeasureOperand& Operand, SketcherMeasurement& Measurement)
{
    const GeometrySpace::Parametric::ModelPartition& Partition = *Document.Partition;
    const GeometrySpace::Parametric::EdgeToken       Edge      = std::get<GeometrySpace::Parametric::EdgeToken>(Operand.Target);
    const GeometrySpace::Parametric::EdgeProfile&    Profile   = Partition.Edges.Resolve(Edge);
    const double                    Length    = GeometrySpace::Parametric::MeasureEdgeLength(Partition, Edge);
    Measurement.Subject = "Edge " + Operand.Name;
    Measurement.Length  = Length;
    AppendRow(Measurement, "Length", Figure("%.6g m", Length), Length);
    if (Profile.Curve.PresentCondition()) AppendRow(Measurement, "Curve", DescribeCurve(Partition.Curves.Resolve(Profile.Curve), Profile.IntervalLow, Profile.IntervalHigh));
    const Vector3 Start = Partition.Vertices.Resolve(Profile.Start).Position;
    const Vector3 End   = Partition.Vertices.Resolve(Profile.End).Position;
    AppendRow(Measurement, "Start", FigurePoint(Start));
    if (GeometrySpace::Parametric::Distance(Start, End) > 1.0e-9) AppendRow(Measurement, "End", FigurePoint(End));
}

// 📝 Angles: two planes → between the outward normals (90° for adjacent block faces, 180° for opposite ones); two lines →
//    the acute angle between them; a plane and a line → the acute angle between the line and the plane.
std::optional<double> AngleBetween(const MeasureOperand& First, const MeasureOperand& Second)
{
    constexpr double Degrees = 180.0 / 3.14159265358979323846;
    const auto Clamp = [](double Value) { return std::clamp(Value, -1.0, 1.0); };
    if (First.Normal && Second.Normal) return std::acos(Clamp(GeometrySpace::Parametric::Dot(*First.Normal, *Second.Normal))) * Degrees;
    if (First.Line && Second.Line) return std::acos(std::fabs(Clamp(GeometrySpace::Parametric::Dot(*First.Line, *Second.Line)))) * Degrees;
    if (First.Normal && Second.Line) return std::asin(std::fabs(Clamp(GeometrySpace::Parametric::Dot(*First.Normal, *Second.Line)))) * Degrees;
    if (First.Line && Second.Normal) return std::asin(std::fabs(Clamp(GeometrySpace::Parametric::Dot(*First.Line, *Second.Normal)))) * Degrees;
    return std::nullopt;
}

void MeasurePair(const SketcherDocument& Document, const MeasureOperand& First, const MeasureOperand& Second, SketcherMeasurement& Measurement)
{
    const GeometrySpace::Parametric::DistanceMeasure Measure = GeometrySpace::Parametric::MeasureMinimumDistance(*Document.Partition, First.Target, Second.Target);
    Measurement.Subject = "Distance " + First.Name + " → " + Second.Name;
    if (!Measure.Refusal.empty()) { Measurement.Refusal = Measure.Refusal; return; }
    Measurement.Distance          = Measure.Distance;
    Measurement.First             = Measure.First;
    Measurement.Second            = Measure.Second;
    Measurement.DistanceCondition = true;
    AppendRow(Measurement, "Minimum distance", Figure("%.6g m", Measure.Distance), Measure.Distance);
    const Vector3 Delta = Measure.Second - Measure.First;
    AppendRow(Measurement, "dx dy dz", Figure("%.4f · %.4f · %.4f", Delta.X, Delta.Y, Delta.Z));
    if (const std::optional<double> Angle = AngleBetween(First, Second))
    {
        Measurement.Angle = *Angle;
        AppendRow(Measurement, "Angle", Figure("%.4f°", *Angle), *Angle);
    }
    AppendRow(Measurement, "Point 1", FigurePoint(Measure.First));
    AppendRow(Measurement, "Point 2", FigurePoint(Measure.Second));
}

void MeasureTotals(const SketcherDocument& Document, const std::vector<MeasureOperand>& Operands, SketcherMeasurement& Measurement)
{
    double Area = 0.0, Length = 0.0;
    int    Faces = 0, Edges = 0;
    for (const MeasureOperand& Operand : Operands)
    {
        if (Operand.FaceCondition) { Area += GeometrySpace::Parametric::MeasureFaceArea(*Document.Partition, std::get<GeometrySpace::Parametric::FaceToken>(Operand.Target)).Area; ++Faces; }
        else { Length += GeometrySpace::Parametric::MeasureEdgeLength(*Document.Partition, std::get<GeometrySpace::Parametric::EdgeToken>(Operand.Target)); ++Edges; }
    }
    Measurement.Subject = Figure("%d face(s) + %d edge(s)", Faces, Edges);
    if (Faces) { Measurement.Area = Area; AppendRow(Measurement, "Total area", Figure("%.6g m²", Area), Area); }
    if (Edges) { Measurement.Length = Length; AppendRow(Measurement, "Total length", Figure("%.6g m", Length), Length); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTION
//------------------------------------------------------------------------------------------------------------------------

// Plane of a Face request: the named planar face of a shown body, moved Offset along the outward normal, kept side inward.
bool ResolveFacePlane(const SketcherDocument& Document, const SketcherSectionRequest& Request, GeometrySpace::Parametric::OrthonormalFrame& Plane)
{
    const GeometrySpace::Parametric::ModelPartition& Partition = *Document.Partition;
    for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies)
    {
        if (HiddenCondition(Document, Body.Key) || !Body.Body.PresentCondition()) continue;
        for (const GeometrySpace::Parametric::FaceToken Face : Partition.CollectInventory(Body.Body).Faces)
        {
            const auto Found = Body.Naming.Faces.find(Face.Slot);
            if (Found == Body.Naming.Faces.end() || Found->second != Request.Face) continue;
            const GeometrySpace::Parametric::FaceProfile& Profile = Partition.Faces.Resolve(Face);
            if (!Profile.Surface.PresentCondition()) return false;
            const auto* Carrier = std::get_if<GeometrySpace::Parametric::PlaneSurface>(&Partition.Surfaces.Resolve(Profile.Surface));
            if (!Carrier) return false;
            const double  Sense   = Profile.Sense == GeometrySpace::Parametric::Orientation::Forward ? 1.0 : -1.0;
            const Vector3 Outward = Carrier->Frame.ZAxis * Sense;
            Plane = GeometrySpace::Parametric::ConstructFrame(Carrier->Frame.Origin + Outward * Request.Offset, Outward * (Request.Flipped ? 1.0 : -1.0), Carrier->Frame.XAxis);
            return true;
        }
    }
    return false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string SketcherSelectionKey(const SketcherSelection& Selection)
{
    std::string Key;
    for (const std::string& Name : Selection.Faces) Key += "f:" + Name + ";";
    for (const std::string& Name : Selection.Edges) Key += "e:" + Name + ";";
    for (const std::string& Name : Selection.Bodies) Key += "b:" + Name + ";";
    return Key;
}

SketcherMeasurement MeasureSketcherSelection(const SketcherDocument& Document)
{
    const auto Start = std::chrono::steady_clock::now();
    SketcherMeasurement Measurement;
    Measurement.Revision     = Document.Revision;
    Measurement.SelectionKey = SketcherSelectionKey(Document.Selection);
    if (!Document.Partition || Document.Regeneration.Bodies.empty())
    {
        Measurement.Refusal = "Measure: the document has no body";
        return Measurement;
    }
    const std::vector<MeasureOperand> Operands = ResolveOperands(Document);
    if (Operands.size() == 1 && Operands.front().FaceCondition) MeasureFace(Document, Operands.front(), Measurement);
    else if (Operands.size() == 1) MeasureEdge(Document, Operands.front(), Measurement);
    else if (Operands.size() == 2) MeasurePair(Document, Operands[0], Operands[1], Measurement);
    else if (Operands.size() > 2) MeasureTotals(Document, Operands, Measurement);
    else if (!Document.Selection.Faces.empty() || !Document.Selection.Edges.empty()) Measurement.Refusal = "Measure: the selection names no shown face or edge";
    else
    {
        std::vector<std::string> Keys;
        for (const std::string& Key : Document.Selection.Bodies) if (FindBody(Document, Key) && !HiddenCondition(Document, Key)) Keys.push_back(Key);
        if (Keys.empty())
        {
            // Nothing selected: the active body, or the only shown one.
            if (FindBody(Document, Document.Regeneration.Active) && !HiddenCondition(Document, Document.Regeneration.Active)) Keys.push_back(Document.Regeneration.Active);
            else for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies) if (!HiddenCondition(Document, Body.Key)) { Keys.push_back(Body.Key); break; }
        }
        if (Keys.empty()) Measurement.Refusal = "Measure: every body is hidden";
        else MeasureBodies(Document, Keys, Measurement);
        if (Keys.size() == 2 && Measurement.Refusal.empty()) MeasureInterference(Document, Keys[0], Keys[1], Measurement);
    }
    Measurement.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    return Measurement;
}

std::string SketcherSectionKey(const SketcherSectionRequest& Request)
{
    static const char* Axes[] = { "x", "y", "z", "face" };
    return Figure("%s%s %.9g%s", Axes[int(Request.Axis) & 3], Request.Axis == SketcherSectionAxis::Face ? (" " + Request.Face).c_str() : "", Request.Offset,
                  Request.Flipped ? " flipped" : "");
}

SketcherSection ResolveSketcherSectionPlane(const SketcherDocument& Document, const SketcherSectionRequest& Request)
{
    SketcherSection Section;
    Section.Revision   = Document.Revision;
    Section.RequestKey = SketcherSectionKey(Request);
    const double Sign  = Request.Flipped ? -1.0 : 1.0;
    switch (Request.Axis)
    {
    case SketcherSectionAxis::X: Section.Plane = GeometrySpace::Parametric::ConstructFrame({ Request.Offset, 0.0, 0.0 }, Vector3{ -1.0, 0.0, 0.0 } * Sign, { 0.0, 1.0, 0.0 }); break;
    case SketcherSectionAxis::Y: Section.Plane = GeometrySpace::Parametric::ConstructFrame({ 0.0, Request.Offset, 0.0 }, Vector3{ 0.0, 1.0, 0.0 } * Sign, { 1.0, 0.0, 0.0 }); break;
    case SketcherSectionAxis::Z: Section.Plane = GeometrySpace::Parametric::ConstructFrame({ 0.0, 0.0, Request.Offset }, Vector3{ 0.0, 0.0, -1.0 } * Sign, { 1.0, 0.0, 0.0 }); break;
    case SketcherSectionAxis::Face:
        if (!Document.Partition || !ResolveFacePlane(Document, Request, Section.Plane))
        {
            Section.Refusal = Request.Face.empty() ? "Section: select a planar face first" : "Section: " + Request.Face + " is not a planar face of a shown body";
            return Section;
        }
        break;
    }
    Section.PlaneCondition = true;
    return Section;
}

SketcherSection SectionSketcherDocument(const SketcherDocument& Document, const SketcherSectionRequest& Request, double OutlineStep)
{
    const auto Start = std::chrono::steady_clock::now();
    SketcherSection Section = ResolveSketcherSectionPlane(Document, Request);
    if (!Section.PlaneCondition) return Section;
    if (!Document.Partition) { Section.Refusal = "Section: the document has no body"; return Section; }
    const GeometrySpace::Parametric::OrthonormalFrame& Plane = Section.Plane;
    struct Piece { double Area; double U; double V; std::array<double, 3> Moments; };
    std::vector<Piece> Pieces;
    Vector3 Moment = {};
    for (const GeometrySpace::Parametric::RegeneratedBody& Body : Document.Regeneration.Bodies)
    {
        if (HiddenCondition(Document, Body.Key) || !Body.Body.PresentCondition()) continue;
        if (SheetBodyCondition(Document, Body.Key)) { ++Section.Sheets; continue; }
        GeometrySpace::Parametric::PlaneSectionMeasure Measure = GeometrySpace::Parametric::SectionBody(*Document.Partition, Body.Body, Plane, OutlineStep);
        if (!Measure.Refusal.empty())
        {
            if (Measure.Refusal.find("misses") != std::string::npos) continue;                // The plane passes this body by
            Section.Refusal = BodyCaptionOf(Document, Body.Key) + ": " + Measure.Refusal;
            continue;
        }
        ++Section.Bodies;
        Section.Area      += Measure.Area;
        Section.Perimeter += Measure.Perimeter;
        Section.Regions   += Measure.Regions;
        Section.Loops     += Measure.Loops;
        Moment             = Moment + Measure.Centroid * Measure.Area;
        const Vector3 Local = Measure.Centroid - Plane.Origin;
        Pieces.push_back({ Measure.Area, GeometrySpace::Parametric::Dot(Local, Plane.XAxis), GeometrySpace::Parametric::Dot(Local, Plane.YAxis), Measure.SecondMoments });
        for (std::vector<Vector3>& Polyline : Measure.Outline) Section.Outline.push_back(std::move(Polyline));
    }
    if (Section.Area > 0.0)
    {
        // Parallel-axis transfer of each piece's moments to the common centroid.
        Section.Centroid = Moment * (1.0 / Section.Area);
        const Vector3 Local = Section.Centroid - Plane.Origin;
        const double  CentreU = GeometrySpace::Parametric::Dot(Local, Plane.XAxis), CentreV = GeometrySpace::Parametric::Dot(Local, Plane.YAxis);
        for (const Piece& Each : Pieces)
        {
            Section.SecondMoments[0] += Each.Moments[0] + Each.Area * (Each.V - CentreV) * (Each.V - CentreV);
            Section.SecondMoments[1] += Each.Moments[1] + Each.Area * (Each.U - CentreU) * (Each.U - CentreU);
            Section.SecondMoments[2] += Each.Moments[2] + Each.Area * (Each.U - CentreU) * (Each.V - CentreV);
        }
    }
    else if (Section.Refusal.empty()) Section.Refusal = Section.Sheets ? "Section: the plane misses every shown solid (sheets are not sectioned)" : "Section: the plane misses every shown body";
    Section.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    return Section;
}

} // namespace Frontier::Workspaces::Sketcher
