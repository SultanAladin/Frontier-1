//============================================================================================================================================
//                                                         SKETCHSOLVE.CPP
//============================================================================================================================================

#include "SlateShape/Sketch/SketchSolve/Api/SketchSolve.h"

#include "SlateShape/Sketch/ConstraintSolver/Api/ConstraintSolver.h"
#include "SlateShape/Sketch/DimensionSolver/Api/DimensionSolver.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <cmath>

namespace Slate
{

namespace
{
    struct CurveSample
    {
        std::vector<SpatialPoint> Polyline = {};
    };

    void CaptureSketch(const SketchStructure& Declared,
                       std::vector<CurveSample>& Samples)
    {
        Samples.clear();
        Samples.resize(Declared.Curves().size());
        for (std::size_t Index = 0u; Index < Declared.Curves().size(); ++Index)
            AppendCurvePolyline(Declared.Curves()[Index].Geometry, Samples[Index].Polyline, 48u);
    }

    double DistanceSquared(const SpatialPoint& LeftPoint,
                           const SpatialPoint& RightPoint)
    {
        const double DeltaLeft = RightPoint.Left - LeftPoint.Left;
        const double DeltaUp = RightPoint.Up - LeftPoint.Up;
        const double DeltaForward = RightPoint.Forward - LeftPoint.Forward;
        return DeltaLeft * DeltaLeft + DeltaUp * DeltaUp + DeltaForward * DeltaForward;
    }

    double MaximumTravel(const std::vector<CurveSample>& Previous,
                         const std::vector<CurveSample>& Current)
    {
        double Maximum = 0.0;
        const std::size_t Count = Previous.size() < Current.size() ? Previous.size() : Current.size();
        for (std::size_t CurveIndex = 0u; CurveIndex < Count; ++CurveIndex)
        {
            const std::size_t PointCount = Previous[CurveIndex].Polyline.size() < Current[CurveIndex].Polyline.size()
                ? Previous[CurveIndex].Polyline.size()
                : Current[CurveIndex].Polyline.size();
            for (std::size_t PointIndex = 0u; PointIndex < PointCount; ++PointIndex)
            {
                const double Travel = std::sqrt(DistanceSquared(Previous[CurveIndex].Polyline[PointIndex],
                                                                Current[CurveIndex].Polyline[PointIndex]));
                if (Travel > Maximum)
                    Maximum = Travel;
            }
        }
        return Maximum;
    }
}

SketchSolveDisposition EvaluateSketchSolve(const SketchStructure& Declared)
{
    if (!Declared.Declared())
        return SketchSolveDisposition::InvalidSketch;
    if (Declared.Constraints().empty() && Declared.Dimensions().empty())
        return SketchSolveDisposition::NotRequested;
    if (EvaluateConstraints(Declared) == ConstraintDisposition::UnsupportedConstraint
     || EvaluateDimensions(Declared) == DimensionDisposition::UnsupportedDimension)
        return SketchSolveDisposition::UnsupportedSketch;
    if (EvaluateConstraints(Declared) == ConstraintDisposition::InvalidSketch
     || EvaluateDimensions(Declared) == DimensionDisposition::InvalidSketch)
        return SketchSolveDisposition::InvalidSketch;
    return SketchSolveDisposition::Produced;
}

Deliver<SketchSolveReport> ApplySketchSolve(SketchStructure& Declared,
                                            std::uint32_t IterationLimit,
                                            double TravelTolerance)
{
    const SketchSolveDisposition Standing = EvaluateSketchSolve(Declared);
    if (Standing == SketchSolveDisposition::NotRequested)
        return Deliver<SketchSolveReport>::Result({ Standing, 0u, 0.0 });
    if (Standing != SketchSolveDisposition::Produced)
        return Deliver<SketchSolveReport>::Refuse({ RefusalReason::ContentUnsupported, "the sketch solve is not supported" });

    if (IterationLimit == 0u)
        return Deliver<SketchSolveReport>::Result({ SketchSolveDisposition::LimitReached, 0u, 0.0 });

    std::vector<CurveSample> Previous;
    std::vector<CurveSample> Current;
    CaptureSketch(Declared, Previous);

    SketchSolveReport Report = {};
    Report.Standing = SketchSolveDisposition::LimitReached;

    for (std::uint32_t Iteration = 0u; Iteration < IterationLimit; ++Iteration)
    {
        const Deliver<bool> ConstraintsApplied = ApplyConstraints(Declared);
        if (!ConstraintsApplied)
            return Deliver<SketchSolveReport>::Refuse(ConstraintsApplied.Error);
        const Deliver<bool> DimensionsApplied = ApplyDimensions(Declared);
        if (!DimensionsApplied)
            return Deliver<SketchSolveReport>::Refuse(DimensionsApplied.Error);

        CaptureSketch(Declared, Current);
        Report.IterationCount = Iteration + 1u;
        Report.MaximumTravel = MaximumTravel(Previous, Current);
        if (Report.MaximumTravel <= TravelTolerance)
        {
            Report.Standing = SketchSolveDisposition::Produced;
            return Deliver<SketchSolveReport>::Result(Report);
        }
        Previous = Current;
    }

    return Deliver<SketchSolveReport>::Result(Report);
}

} // namespace Slate
