#pragma once
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
namespace Frontier::GeometrySpace::Parametric {
// Intrinsic distance on a connected triangle surface, not ambient XYZ distance.
// The target supplies the side, extent and component. No other sheet is searched.
struct SurfaceDistanceCurveOutcome {
    NurbsCurve Curve;
    std::string Refusal;
    double SourceGap = 0;
};
[[nodiscard]] SurfaceDistanceCurveOutcome TraceSurfaceDistanceCurve(const PolygonComplex& Mesh,
    const NurbsCurve& Guide, const NurbsCurve& Target, double Distance, double SourceTolerance);
// Refines the CAD tessellation until successive intrinsic contours agree within
// tolerance. Refuses on resource limits / nonconvergence; never substitutes XYZ offset.
[[nodiscard]] SurfaceDistanceCurveOutcome OffsetCurveOnBody(const ModelPartition& Partition, BodyToken Body,
    const NurbsCurve& Guide, const NurbsCurve& Target, double Distance, double Tolerance);
}
