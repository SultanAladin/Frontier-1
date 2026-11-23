#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSurfaceTracing.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces::Sketcher
{
using namespace GeometrySpace::Parametric;

SketcherSnap SnapSketcherTrace(SketcherSnapRepository& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraBasis& Frame, double X, double Y, bool CurveSnap, bool AutoCurves)
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
    if(Snap.Kind==SketcherSnapCategory::None) Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    if (CurveSnap && Snap.Kind == SketcherSnapCategory::None)
    {
        Query.FreeCurvesOnly = false;
        Query.BodyCurvesOnly = true;
        Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    }
    if (Curves && Snap.Kind == SketcherSnapCategory::None)
    {
        Query.Filter.CurveEnabled = false;
        Query.Filter.FaceEnabled = true;
        Snap = FindSketcherSnap(Catalogue, Document, Scene, Query);
    }
    if (Snap.Kind == SketcherSnapCategory::Face)
    {
        double PX, PY;
        // Newton refinement at a silhouette can land on a different branch. Never
        // accept an off-ray foot as though it were the surface under the cursor.
        if (!ProjectSketcherPoint(Frame, Snap.Position, PX, PY) || std::hypot(PX - X, PY - Y) > 0.75) return {};
    }
    return Snap;
}

SketcherControlTrace MakeSketcherControlTrace(const std::vector<Vector3>& Controls,
    SketcherDrawingCategory Kind, SketchSplineForm Form, bool Closed)
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
    if (Kind == SketcherDrawingCategory::Arc)
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
    if (Kind != SketcherDrawingCategory::Line && Kind != SketcherDrawingCategory::Curve)
    { Result.Refusal = "Use Line, Curve or Arc"; return Result; }
    if (Kind == SketcherDrawingCategory::Curve && Form != SketchSplineForm::Nurbs && Form != SketchSplineForm::Bezier)
    {
        const std::vector<Vector3> Seed = SampleThroughCurve(Controls, Closed, Form, 32);
        if (Seed.size() < 2) { Result.Refusal = "Curve form takes no curve through these controls"; return Result; }
        double Chord = 0.0;
        for (size_t I = 1; I < Seed.size(); ++I) Chord += Distance(Seed[I - 1], Seed[I]);
        CurveFitSettings Settings;
        Settings.Tolerance = std::max(1.0e-5, Chord * 2.5e-4);
        Settings.Fairing   = 1.0e-10;
        Settings.SpanLimit = 48;
        const CurveFitOutcome Fitted = FitSpaceCurve(Seed, Settings);
        if (!Fitted.Refusal.empty() || !Fitted.Curve.Definition)
        {
            Result.Refusal = Fitted.Refusal.empty() ? "Curve fit failed" : Fitted.Refusal;
            return Result;
        }
        NurbsCurveDefinition Exact = *Fitted.Curve.Definition;
        Exact.Poles.front() = Controls.front();
        Exact.Poles.back()  = Closed ? Controls.front() : Controls.back();
        Result.Curve.Definition = std::make_shared<NurbsCurveDefinition>(std::move(Exact));
        const int Samples = std::max(32, int(Controls.size()) * 16);
        for (int I = 0; I <= Samples; ++I)
            Result.Samples.push_back(EvaluateNurbsCurve(*Result.Curve.Definition, double(I) / Samples).Position);
        return Result;
    }
    const int Degree = Kind == SketcherDrawingCategory::Line ? 1 : Form == SketchSplineForm::Bezier ? int(Controls.size()) - 1 : std::min(3, int(Controls.size()) - 1);
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

SketcherControlTrace ConstructReferenceConformingTrace(const SketcherDocument& Document,
    const std::vector<Vector3>& Controls, SketcherDrawingCategory Kind, SketchSplineForm Form, bool Closed)
{
    SketcherControlTrace Fallback = MakeSketcherControlTrace(Controls, Kind, Form, Closed);
    if (!Fallback.Refusal.empty() || Fallback.Arc || Document.ReferenceSurfaces.empty() || !Fallback.Curve.Definition)
    {
        return Fallback;
    }

    double TotalChord = 0.0;
    for (size_t I = 1; I < Controls.size(); ++I) TotalChord += Distance(Controls[I - 1], Controls[I]);
    if (!(TotalChord > 1.0e-9)) return Fallback;

    const double SearchRadius = std::max(0.05, TotalChord * 0.25);

    // Estimate a consistent outward panel normal from the clicked controls so Solidify shells always hit the OUTER skin
    Vector3 RefCentroid{ 0.0, 0.0, 0.0 };
    int RefCount = 0;
    for (const SketcherReferenceSurface& Surf : Document.ReferenceSurfaces)
    {
        if (!Surf.VisibleCondition || !Surf.Geometry || Surf.Geometry->Corners.empty()) continue;
        RefCentroid = RefCentroid + Vector3{ 0.5 * (Surf.MinBound[0] + Surf.MaxBound[0]),
                                             0.5 * (Surf.MinBound[1] + Surf.MaxBound[1]),
                                             0.5 * (Surf.MinBound[2] + Surf.MaxBound[2]) };
        ++RefCount;
    }
    if (RefCount > 0) RefCentroid = RefCentroid * (1.0 / double(RefCount));

    Vector3 PanelNormal{ 0.0, 0.0, 0.0 };
    for (const Vector3& Ctrl : Controls)
    {
        if (const auto Hit = ProjectOntoReferenceSurfaces(Document.ReferenceSurfaces, Ctrl, SearchRadius * 0.2))
        {
            Vector3 N = Hit->Normal;
            if (Length(PanelNormal) > 0.5)
            {
                if (Dot(N, PanelNormal) < 0.0) N = N * -1.0;
            }
            else if (Dot(N, Ctrl - RefCentroid) < 0.0)
            {
                N = N * -1.0;
            }
            PanelNormal = PanelNormal + N;
        }
    }
    const bool PanelNormalReady = Length(PanelNormal) > 1.0e-6;
    if (PanelNormalReady) PanelNormal = Normalize(PanelNormal);

    auto ProjectOuterSkin = [&](const Vector3& QueryPt) -> std::optional<Vector3>
    {
        if (PanelNormalReady)
        {
            const double RayLift = std::max(0.05, TotalChord * 0.15);
            const Vector3 RayStart = QueryPt + PanelNormal * RayLift;
            const Vector3 RayDir   = PanelNormal * -1.0;
            if (const auto RayHit = IntersectReferenceSurfaces(Document.ReferenceSurfaces, RayStart, RayDir, RayLift * 2.5))
            {
                if (Distance(RayHit->Position, QueryPt) <= SearchRadius) return RayHit->Position;
            }
        }
        if (const auto ClosestHit = ProjectOntoReferenceSurfaces(Document.ReferenceSurfaces, QueryPt, SearchRadius))
        {
            return ClosestHit->Position;
        }
        return std::nullopt;
    };

    constexpr int DenseCount = 64;
    std::vector<Vector3> Conformed;
    Conformed.reserve(DenseCount + 1);

    int HitSamples = 0;
    double MaxLift = 0.0;
    for (int Step = 0; Step <= DenseCount; ++Step)
    {
        const double Param = double(Step) / double(DenseCount);
        const Vector3 SeedPoint = EvaluateNurbsCurve(*Fallback.Curve.Definition, Param).Position;
        Vector3 Placed = SeedPoint;
        if (Step == 0)
        {
            Placed = Controls.front();
            ++HitSamples;
        }
        else if (Step == DenseCount)
        {
            Placed = Closed ? Controls.front() : Controls.back();
            ++HitSamples;
        }
        else if (const auto OuterPt = ProjectOuterSkin(SeedPoint))
        {
            MaxLift = std::max(MaxLift, Distance(SeedPoint, *OuterPt));
            Placed  = *OuterPt;
            ++HitSamples;
        }
        if (Conformed.empty() || Distance(Conformed.back(), Placed) > 1.0e-9 * std::max(1.0, TotalChord))
        {
            Conformed.push_back(Placed);
        }
    }
    if (HitSamples < DenseCount / 2 || Conformed.size() < 3) return Fallback;
    Conformed.front() = Controls.front();
    Conformed.back()  = Closed ? Controls.front() : Controls.back();
    if (MaxLift <= std::max(1.0e-5, TotalChord * 2.0e-4)) return Fallback;

    // 2-sweep gentle fairing + outer-skin re-projection keeps curves smooth and flush on the exact outer skin
    for (int Sweep = 0; Sweep < 2; ++Sweep)
    {
        std::vector<Vector3> Smoothed = Conformed;
        for (size_t I = 1; I + 1 < Conformed.size(); ++I)
        {
            const Vector3 Avg = (Conformed[I - 1] + Conformed[I] * 2.0 + Conformed[I + 1]) * 0.25;
            if (const auto OuterPt = ProjectOuterSkin(Avg)) Smoothed[I] = *OuterPt;
            else Smoothed[I] = Avg;
        }
        Conformed = std::move(Smoothed);
    }

    CurveFitSettings Settings;
    Settings.Tolerance = std::max(2.0e-4, TotalChord * 4.0e-4);
    Settings.Fairing   = 1.0e-7;
    Settings.SpanLimit = 18;
    CurveFitOutcome Fitted = FitSpaceCurve(Conformed, Settings);
    if (!Fitted.Refusal.empty() || !Fitted.Curve.Definition || Fitted.Curve.Definition->Poles.size() < 2)
    {
        Settings.Tolerance = std::max(1.0e-3, TotalChord * 2.0e-3);
        Settings.SpanLimit = 28;
        Fitted = FitSpaceCurve(Conformed, Settings);
    }
    if (!Fitted.Refusal.empty() || !Fitted.Curve.Definition || Fitted.Curve.Definition->Poles.size() < 2)
    {
        return Fallback;
    }

    NurbsCurveDefinition Exact = *Fitted.Curve.Definition;
    Exact.Poles.front() = Controls.front();
    Exact.Poles.back()  = Closed ? Controls.front() : Controls.back();

    SketcherControlTrace ConformedTrace;
    ConformedTrace.Curve.Definition = std::make_shared<NurbsCurveDefinition>(std::move(Exact));
    const int DisplaySamples = std::max(48, int(ConformedTrace.Curve.Definition->Poles.size()) * 12);
    ConformedTrace.Samples.reserve(static_cast<size_t>(DisplaySamples + 1));
    for (int I = 0; I <= DisplaySamples; ++I)
    {
        ConformedTrace.Samples.push_back(EvaluateNurbsCurve(*ConformedTrace.Curve.Definition, double(I) / DisplaySamples).Position);
    }
    return ConformedTrace;
}

SketcherSurfaceTrace ProjectSketcherTrace(SketcherSnapRepository& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraBasis& Frame, const std::vector<Vector3>& Controls,
    SketcherDrawingCategory Kind, SketchSplineForm Form, bool Closed, bool Fit, double Tolerance)
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
        if (Hit.Kind == SketcherSnapCategory::None || Distance(Hit.Position, Control) > Tolerance)
            Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
        if (Hit.Kind == SketcherSnapCategory::None || Distance(Hit.Position, Control) > Tolerance)
        { Result.Refusal = "Surface trace: a control is hidden / off the current surface; restore the view or split the section"; return Result; }
    }
    std::vector<Vector3> Seed;
    if (Kind == SketcherDrawingCategory::Arc)
    {
        if (Controls.size() != 3) { Result.Refusal = "Surface arc: click start, end, then a point on the arc"; return Result; }
        const Vector3 Along = Controls[1] - Controls[0];
        const Vector3 Normal = Cross(Along, Controls[2] - Controls[0]);
        if (Length(Along) < 1.0e-10 || Length(Normal) < 1.0e-12)
        { Result.Refusal = "Surface arc: the three points are collinear"; return Result; }
        OrthonormalBasis Plane;
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
    else if (Kind == SketcherDrawingCategory::Line)
    {
        for (size_t Span = 1; Span < Controls.size(); ++Span)
            for (int Step = Span == 1 ? 0 : 1; Step <= 32; ++Step)
                Seed.push_back(Controls[Span - 1] * (1.0 - double(Step) / 32.0) + Controls[Span] * (double(Step) / 32.0));
        if (Closed) { Result.Refusal = "Surface polyline: finish open sections separately"; return Result; }
    }
    else if (Kind == SketcherDrawingCategory::Curve) Seed = SampleThroughCurve(Controls, Closed, Form, Fit ? 48 : 16);
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
        if (Hit.Kind == SketcherSnapCategory::None && (Index == 0 || Index + 1 == Count))
        {
            // Exact boundary controls may project a fraction beyond the faceted
            // silhouette. Only an exact BODY edge at the same 3D point is valid.
            Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
            if (Hit.Kind != SketcherSnapCategory::Curve || Distance(Hit.Position, Point) > Tolerance) Hit = {};
        }
        if (Hit.Kind == SketcherSnapCategory::None)
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
            if (Hit.Kind == SketcherSnapCategory::None && (Step == 0 || Step == 256))
            {
                Hit = SnapSketcherTrace(Catalogue, Document, Scene, Frame, X, Y, true);
                if (Hit.Kind != SketcherSnapCategory::Curve || Distance(Hit.Position, Point) > Tolerance) Hit = {};
            }
            if (Hit.Kind == SketcherSnapCategory::None) { Miss = true; break; }
            Result.WorstSurfaceDeviation = std::max(Result.WorstSurfaceDeviation, Distance(Point, Hit.Position));
        }
        if (!Miss && Result.WorstSurfaceDeviation <= Tolerance) return Result;
        Settings.Tolerance *= 0.25;
    }
    Result.Refusal = "Surface trace cannot meet the 0.2 mm sampled surface check; add controls or draw a shorter section";
    return Result;
}
}
