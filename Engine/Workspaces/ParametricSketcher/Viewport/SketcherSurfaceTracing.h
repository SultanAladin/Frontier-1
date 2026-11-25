#pragma once
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSnapping.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"

namespace Frontier::Workspaces::Sketcher
{
// Surface hits are exact carrier evaluations, seeded by the visible tessellation.
[[nodiscard]] SketcherSnap SnapSketcherTrace(SketcherSnapRepository& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraBasis& Frame, double X, double Y, bool CurveSnap, bool AutoCurves = false);

// Point-only authoring: exactly the supplied controls, never a surface fit.
struct SketcherControlTrace
{
    GeometrySpace::Parametric::NurbsCurve Curve;
    std::optional<SketcherDrawingRequest> Arc;
    std::vector<GeometrySpace::Parametric::Vector3> Samples; // Display tessellation only, never saved as controls
    std::string Refusal;
};
[[nodiscard]] SketcherControlTrace MakeSketcherControlTrace(const std::vector<GeometrySpace::Parametric::Vector3>& Controls,
    SketcherDrawingCategory Kind, GeometrySpace::Parametric::SketchSplineForm Form, bool Closed);

// Conforms a Line or Curve stroke onto Document.ReferenceSurfaces via BVH closest-point projection while keeping exact
// clicked endpoints (falls back to MakeSketcherControlTrace when no reference surface lies beneath the stroke).
[[nodiscard]] SketcherControlTrace ConstructReferenceConformingTrace(const SketcherDocument& Document,
    const std::vector<GeometrySpace::Parametric::Vector3>& Controls, SketcherDrawingCategory Kind,
    GeometrySpace::Parametric::SketchSplineForm Form, bool Closed);

struct SketcherSurfaceTrace
{
    std::vector<GeometrySpace::Parametric::Vector3> Samples;
    GeometrySpace::Parametric::CurveFitOutcome Fit;
    std::string Refusal;
    double WorstSurfaceDeviation = 0.0;
};
// Project the ENTIRE seed curve, not just its control points. Misses / gaps refuse.
// Arc is a three-point circular seed; a curved-surface projection becomes a NURBS.
// The fit is checked at 257 samples against the visible source, to Tolerance metres.
[[nodiscard]] SketcherSurfaceTrace ProjectSketcherTrace(SketcherSnapRepository& Catalogue, const SketcherDocument& Document,
    const SketcherScene& Scene, const OrbitCameraBasis& Frame, const std::vector<GeometrySpace::Parametric::Vector3>& Controls,
    SketcherDrawingCategory Kind, GeometrySpace::Parametric::SketchSplineForm Form, bool Closed, bool Fit,
    double Tolerance = 0.0002);
}
