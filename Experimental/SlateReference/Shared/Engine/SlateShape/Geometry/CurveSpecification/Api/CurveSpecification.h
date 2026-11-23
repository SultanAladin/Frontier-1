//============================================================================================================================================
//                                                        CURVESPECIFICATION.H
//============================================================================================================================================
// 🧩 Exact curve declarations for the CAD kernel — parameter intervals and the closed set of supported
//    curve subjects. These declarations do not own document rows, GPU spans, or editor state.
//
// 📝 The point and direction these curves are built from now live in `SpatialMeasure`, which this header
//    includes and re-exports so that naming a curve still brings its geometry vocabulary with it.

#pragma once

#include "SlateShape/Geometry/SpatialMeasure/Api/SpatialMeasure.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Slate
{

struct ParameterInterval
{
    double Minimum = 0.0;
    double Maximum = 1.0;
    bool Declared() const { return Maximum > Minimum; }
};

struct CurveName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class CurveSubject : std::uint32_t
{
    Line = 0u,
    CircularArc = 1u,
    Circle = 2u,
    EllipticalArc = 3u,
    Ellipse = 4u,
    Bezier = 5u,
    BasisSpline = 6u,
    RationalSpline = 7u,
    Hermite = 8u,
    SubjectCount = 9u
};

struct LineCurve
{
    SpatialPoint Origin = {};
    SpatialPoint Terminus = {};
};

struct CircularArcCurve
{
    SpatialPoint Centre = {};
    SpatialDirection Normal = {};
    SpatialDirection StartDirection = {};
    SpatialPoint ThroughPoint = {};
    bool ThroughDeclared = false;
    double Radius = 0.0;
    double SweepRadians = 0.0;
};

struct CircleCurve
{
    SpatialPoint Centre = {};
    SpatialDirection Normal = {};
    SpatialDirection StartDirection = {};
    double Radius = 0.0;
};

struct EllipticalArcCurve
{
    SpatialPoint Centre = {};
    SpatialDirection Normal = {};
    SpatialDirection MajorDirection = {};
    double MajorRadius = 0.0;
    double MinorRadius = 0.0;
    double StartRadians = 0.0;
    double SweepRadians = 0.0;
};

struct EllipseCurve
{
    SpatialPoint Centre = {};
    SpatialDirection Normal = {};
    SpatialDirection MajorDirection = {};
    double MajorRadius = 0.0;
    double MinorRadius = 0.0;
};

struct BezierCurve
{
    std::vector<SpatialPoint> ControlPoints = {};
};

struct BasisSplineCurve
{
    std::vector<SpatialPoint> ControlPoints = {};
    std::uint32_t Degree = 0u;
    bool Periodic = false;
};

struct RationalSplineCurve
{
    std::vector<SpatialPoint> ControlPoints = {};
    std::vector<double> Weights = {};
    std::uint32_t Degree = 0u;
    bool Periodic = false;
};

struct HermiteCurve
{
    std::vector<SpatialPoint> ControlPoints = {};
    std::vector<SpatialDirection> Tangents = {};
    SpatialPoint StartPoint = {};
    SpatialPoint EndPoint = {};
    SpatialDirection StartTangent = {};
    SpatialDirection EndTangent = {};
};

class CurveSpecification
{
public:
    static CurveSpecification DeclareLine(const SpatialPoint& Origin,
                                          const SpatialPoint& Terminus);
    static CurveSpecification DeclareCircularArc(const CircularArcCurve& Declared,
                                                 const ParameterInterval& Interval);
    static CurveSpecification DeclareCircle(const CircleCurve& Declared);
    static CurveSpecification DeclareThreePointArc(const SpatialPoint& StartPoint,
                                                   const SpatialPoint& ThroughPoint,
                                                   const SpatialPoint& EndPoint);
    static CurveSpecification DeclareEllipticalArc(const EllipticalArcCurve& Declared,
                                                   const ParameterInterval& Interval);
    static CurveSpecification DeclareEllipse(const EllipseCurve& Declared);
    static CurveSpecification DeclareOval(const EllipseCurve& Declared);
    static CurveSpecification DeclareBezier(const std::vector<SpatialPoint>& ControlPoints,
                                            const ParameterInterval& Interval);
    static CurveSpecification DeclareBasisSpline(const BasisSplineCurve& Declared,
                                                 const ParameterInterval& Interval);
    static CurveSpecification DeclareRationalSpline(const RationalSplineCurve& Declared,
                                                    const ParameterInterval& Interval);
    static CurveSpecification DeclareHermite(const HermiteCurve& Declared,
                                             const ParameterInterval& Interval);

    CurveSubject Subject() const { return HeldSubject; }
    const ParameterInterval& Interval() const { return HeldInterval; }
    const LineCurve& HeldLine() const { return Line; }
    LineCurve& HeldLine() { return Line; }
    const CircularArcCurve& HeldCircularArc() const { return CircularArc; }
    CircularArcCurve& HeldCircularArc() { return CircularArc; }
    const CircleCurve& HeldCircle() const { return Circle; }
    CircleCurve& HeldCircle() { return Circle; }
    const EllipticalArcCurve& HeldEllipticalArc() const { return EllipticalArc; }
    EllipticalArcCurve& HeldEllipticalArc() { return EllipticalArc; }
    const EllipseCurve& HeldEllipse() const { return Ellipse; }
    EllipseCurve& HeldEllipse() { return Ellipse; }
    const BezierCurve& HeldBezier() const { return Bezier; }
    BezierCurve& HeldBezier() { return Bezier; }
    const BasisSplineCurve& HeldBasisSpline() const { return BasisSpline; }
    BasisSplineCurve& HeldBasisSpline() { return BasisSpline; }
    const RationalSplineCurve& HeldRationalSpline() const { return RationalSpline; }
    RationalSplineCurve& HeldRationalSpline() { return RationalSpline; }
    const HermiteCurve& HeldHermite() const { return Hermite; }
    HermiteCurve& HeldHermite() { return Hermite; }
    bool Declared() const;

private:
    CurveSubject HeldSubject = CurveSubject::Line;
    ParameterInterval HeldInterval = {};
    LineCurve Line = {};
    CircularArcCurve CircularArc = {};
    CircleCurve Circle = {};
    EllipticalArcCurve EllipticalArc = {};
    EllipseCurve Ellipse = {};
    BezierCurve Bezier = {};
    BasisSplineCurve BasisSpline = {};
    RationalSplineCurve RationalSpline = {};
    HermiteCurve Hermite = {};
};

} // namespace Slate
