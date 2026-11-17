#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSurfaceTracing.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces::Sketcher
{
using namespace GeometrySpace::Parametric;

SketcherSnap SnapSketcherTrace(SketcherSnapCatalogue& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraFrame& Frame, double X, double Y, bool CurveSnap, bool AutoCurves)
{
    SketcherSnapQuery Query;
    Query.Frame = Frame;
    Query.PixelX = X;
    Query.PixelY = Y;
    Query.CurveReach = 12.0;
    const bool Curves = CurveSnap || AutoCurves;
    Query.Filter = { false, false, false, false, false, false, false, Curves, !Curves };
    Query.FreeCurvesOnly = Curves;
    Query.OverlayCurvesVisible = Curves; // Authored curves are visibly drawn over the reference.
    // Ctrl prioritises an authored curve, then a CAD edge, then the surface.
    SketcherSnap Snap;
    if (Curves) {
        // Exact endpoints beat a nearby interior foot; consumed profiles are excluded.
        Query.Filter.EndpointEnabled = true; Query.Filter.CurveEnabled = false; Query.PointReach=8;
        Snap=FindSketcherSnap(Catalogue,Document,Scene,Query);
        Query.Filter.EndpointEnabled = false; Query.Filter.CurveEnabled = true;
    }
    if(Snap.Kind==SketcherSnapKind::None) Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    if (CurveSnap && Snap.Kind == SketcherSnapKind::None)
    {
        Query.FreeCurvesOnly = false;
        Query.BodyCurvesOnly = true;
        Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    }
    if (Curves && Snap.Kind == SketcherSnapKind::None)
    {
        Query.Filter.CurveEnabled = false;
        Query.Filter.FaceEnabled = true;
        Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    }
    if (Snap.Kind == SketcherSnapKind::Face)
    {
        double PX, PY;
        // Newton refinement at a silhouette can land on a different branch. Never
        // accept an off-ray foot as though it were the surface under the cursor.
        if (!ProjectSketcherPoint(Frame, Snap.Position, PX, PY) || std::hypot(PX - X, PY - Y) > 0.75) return {};
    }
    return Snap;
}

SketcherControlTrace MakeSketcherControlTrace(const std::vector<Vector3>& Controls,
    SketcherDrawingKind Kind, SketchSplineForm Form, bool Closed)
{
    SketcherControlTrace Result;
    if (Controls.size() < 2) { Result.Refusal = "Place at least two controls"; return Result; }
    for (size_t I = 0; I < Controls.size(); ++I)
    {
        const auto P = Controls[I];
        if (!std::isfinite(P.X) || !std::isfinite(P.Y) || !std::isfinite(P.Z))
        { Result.Refusal = "A control is not finite"; return Result; }
        if (I && Distance(P, Controls[I-1]) <= 1e-12)
        { Result.Refusal = "The same control twice in a row"; return Result; }
    }
    if (Closed && (Controls.size() < 4 || Distance(Controls.front(), Controls.back()) > 1e-12))
    { Result.Refusal = "Close by explicitly placing the last control on the first (at least three distinct controls)"; return Result; }
    if (Kind == SketcherDrawingKind::Arc)
    {
        if (Controls.size() != 3) { Result.Refusal = "Arc: place start, end, then a point on the arc"; return Result; }
        const auto X = Controls[1] - Controls[0], N = Cross(X, Controls[2] - Controls[0]);
        if (Length(N) < 1e-12) { Result.Refusal = "Arc points are collinear"; return Result; }
        SketcherDrawingRequest Request;
        Request.Kind = Kind; Request.PlaneCondition = true;
        Request.Plane.Origin = Controls[0]; Request.Plane.XAxis = Normalize(X);
        Request.Plane.ZAxis = Normalize(N); Request.Plane.YAxis = Cross(Request.Plane.ZAxis, Request.Plane.XAxis);
        for (auto P : Controls) { const auto D = P - Request.Plane.Origin; Request.Points.push_back({Dot(D,Request.Plane.XAxis),Dot(D,Request.Plane.YAxis)}); }
        SketchModel Model;
        Result.Refusal = ComposeSketcherSketch(Request, Model);
        if (!Result.Refusal.empty()) return Result;
        for (auto P : SampleSketchCurve(Model, Model.Curves.front()))
            Result.Samples.push_back(Request.Plane.Origin + Request.Plane.XAxis * P.X + Request.Plane.YAxis * P.Y);
        Result.Arc = Request;
        return Result;
    }
    if (Kind != SketcherDrawingKind::Line && Kind != SketcherDrawingKind::Curve)
    { Result.Refusal = "Use Line, Curve or Arc"; return Result; }
    if (Kind == SketcherDrawingKind::Curve && Form != SketchSplineForm::Nurbs && Form != SketchSplineForm::Bezier)
    { Result.Refusal = "Point-only tracing uses NURBS (3) or Bezier (2); interpolating forms need extra tangent poles"; return Result; }
    const int Degree = Kind == SketcherDrawingKind::Line ? 1 : Form == SketchSplineForm::Bezier ? int(Controls.size()) - 1 : std::min(3, int(Controls.size()) - 1);
    if (Degree > 15) { Result.Refusal = "Bezier supports at most 16 controls; use NURBS"; return Result; }
    NurbsCurveDefinition Definition;
    Definition.Degree = Degree;
    Definition.Poles = Controls; // Exact count, exact snapped XYZ values. No fitting or insertion.
    Definition.Weights.assign(Controls.size(), 1.0);
    Definition.Knots.assign(Degree + 1, 0.0);
    const int Spans = int(Controls.size()) - Degree;
    for (int I = 1; I < Spans; ++I) Definition.Knots.push_back(double(I) / Spans);
    Definition.Knots.insert(Definition.Knots.end(), Degree + 1, 1.0);
    Result.Curve.Definition = std::make_shared<NurbsCurveDefinition>(std::move(Definition));
    const int Samples = std::max(32, int(Controls.size()) * 16);
    for (int I = 0; I <= Samples; ++I)
        Result.Samples.push_back(EvaluateNurbsCurve(*Result.Curve.Definition, double(I) / Samples).Position);
    return Result;
}

SketcherSurfaceTrace ProjectSketcherTrace(SketcherSnapCatalogue& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraFrame& Frame, const std::vector<Vector3>& Controls,
    SketcherDrawingKind Kind, SketchSplineForm Form, bool Closed, bool Fit, double Tolerance)
{
    SketcherSurfaceTrace Result;
    if (Controls.size() < 2 || Controls.size() > 65 || !(Tolerance > 0.0) || !std::isfinite(Tolerance))
    {
        Result.Refusal = "Surface trace: use 2–65 distinct controls";
        return Result;
    }
    if (Closed && Controls.size() < 3)
    { Result.Refusal = "Surface trace: a closed spline needs at least three controls"; return Result; }
    // Camera changes must never silently move a clicked control from the back
    // of a sheet onto its newly visible front. All controls must still be on
    // the visible source (body-edge controls may lie on its exact silhouette).
    for (Vector3 Control : Controls)
    {
        double X, Y;
        if (!ProjectSketcherPoint(Frame, Control, X, Y))
        { Result.Refusal = "Surface trace: a control left the view; restore the view or split the section"; return Result; }
        SketcherSnap Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, false);
        if (Hit.Kind == SketcherSnapKind::None || Distance(Hit.Position, Control) > Tolerance)
            Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
        if (Hit.Kind == SketcherSnapKind::None || Distance(Hit.Position, Control) > Tolerance)
        { Result.Refusal = "Surface trace: a control is hidden / off the current surface; restore the view or split the section"; return Result; }
    }
    std::vector<Vector3> Seed;
    if (Kind == SketcherDrawingKind::Arc)
    {
        if (Controls.size() != 3) { Result.Refusal = "Surface arc: click start, end, then a point on the arc"; return Result; }
        const Vector3 Along = Controls[1] - Controls[0];
        const Vector3 Normal = Cross(Along, Controls[2] - Controls[0]);
        if (Length(Along) < 1.0e-10 || Length(Normal) < 1.0e-12)
        { Result.Refusal = "Surface arc: the three points are collinear"; return Result; }
        OrthonormalFrame Plane;
        Plane.Origin = Controls[0]; Plane.XAxis = Normalize(Along); Plane.ZAxis = Normalize(Normal);
        Plane.YAxis = Cross(Plane.ZAxis, Plane.XAxis);
        SketcherDrawingRequest Request;
        Request.Kind = Kind;
        Request.FillCondition = false;
        for (Vector3 Point : Controls)
        {
            const Vector3 Delta = Point - Plane.Origin;
            Request.Points.push_back({ Dot(Delta, Plane.XAxis), Dot(Delta, Plane.YAxis) });
        }
        SketchModel Sketch;
        Result.Refusal = ComposeSketcherSketch(Request, Sketch);
        if (!Result.Refusal.empty()) return Result;
        const ProfileSegment Arc = ResolveSketchCurveSegment(Sketch, 0, 1.0e-10, Result.Refusal);
        if (!Result.Refusal.empty()) return Result;
        const int Steps = Fit ? 128 : 48;
        for (int Index = 0; Index <= Steps; ++Index)
        {
            const Vector2 Point = EvaluateProfileSegment(Arc, double(Index) / double(Steps));
            Seed.push_back(Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y);
        }
    }
    else if (Kind == SketcherDrawingKind::Line)
    {
        for (size_t Span = 1; Span < Controls.size(); ++Span)
            for (int Step = Span == 1 ? 0 : 1; Step <= 32; ++Step)
                Seed.push_back(Controls[Span - 1] * (1.0 - double(Step) / 32.0) + Controls[Span] * (double(Step) / 32.0));
        if (Closed) { Result.Refusal = "Surface polyline: finish open sections separately"; return Result; }
    }
    else if (Kind == SketcherDrawingKind::Curve) Seed = SampleThroughCurve(Controls, Closed, Form, Fit ? 48 : 16);
    else { Result.Refusal = "Surface trace supports Curve, Arc and Line; turn Surface off for planar shapes"; return Result; }
    if (Seed.size() < 2) { Result.Refusal = "Surface trace: cannot form the seed curve"; return Result; }
    // Bounded work even with a long clicked path. Each finished fit also receives
    // an independent, denser surface check below; a sample-only fit is not enough.
    const size_t Count = std::min<size_t>(Fit ? 513 : 129, Seed.size());
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const Vector3 Point = Seed[Index * (Seed.size() - 1) / (Count - 1)];
        double X, Y;
        if (!ProjectSketcherPoint(Frame, Point, X, Y))
        { Result.Refusal = "Surface trace leaves the view; draw a shorter section"; return Result; }
        SketcherSnap Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, false);
        if (Hit.Kind == SketcherSnapKind::None && (Index == 0 || Index + 1 == Count))
        {
            // Exact boundary controls may project a fraction beyond the faceted
            // silhouette. Only an exact BODY edge at the same 3D point is valid.
            Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
            if (Hit.Kind != SketcherSnapKind::Curve || Distance(Hit.Position, Point) > Tolerance) Hit = {};
        }
        if (Hit.Kind == SketcherSnapKind::None)
        { Result.Refusal = "Surface trace crosses a gap / silhouette; add controls or split the curve"; return Result; }
        if (Result.Samples.empty() || Distance(Result.Samples.back(), Hit.Position) > 1.0e-10) Result.Samples.push_back(Hit.Position);
    }
    if (Result.Samples.size() < 2) { Result.Refusal = "Surface trace: coincident points"; return Result; }
    if (Closed) Result.Samples.back() = Result.Samples.front();
    if (!Fit) return Result;
    CurveFitSettings Settings;
    Settings.Tolerance = Tolerance * 0.25;
    Settings.Fairing = 1.0e-10;
    Settings.SpanLimit = 64;
    for (int Attempt = 0; Attempt < 3; ++Attempt)
    {
        Result.Fit = FitSpaceCurve(Result.Samples, Settings);
        if (!Result.Fit.Refusal.empty()) { Result.Refusal = Result.Fit.Refusal; return Result; }
        Result.WorstSurfaceDeviation = 0.0;
        bool Miss = false;
        for (int Step = 0; Step <= 256; ++Step)
        {
            const Vector3 Point = EvaluateNurbsCurve(*Result.Fit.Curve.Definition, double(Step) / 256.0).Position;
            double X, Y;
            if (!ProjectSketcherPoint(Frame, Point, X, Y)) { Miss = true; break; }
            SketcherSnap Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, false);
            if (Hit.Kind == SketcherSnapKind::None && (Step == 0 || Step == 256))
            {
                Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
                if (Hit.Kind != SketcherSnapKind::Curve || Distance(Hit.Position, Point) > Tolerance) Hit = {};
            }
            if (Hit.Kind == SketcherSnapKind::None) { Miss = true; break; }
            Result.WorstSurfaceDeviation = std::max(Result.WorstSurfaceDeviation, Distance(Point, Hit.Position));
        }
        if (!Miss && Result.WorstSurfaceDeviation <= Tolerance) return Result;
        Settings.Tolerance *= 0.25;
    }
    Result.Refusal = "Surface trace cannot meet the 0.2 mm sampled surface check; add controls or draw a shorter section";
    return Result;
}
}
