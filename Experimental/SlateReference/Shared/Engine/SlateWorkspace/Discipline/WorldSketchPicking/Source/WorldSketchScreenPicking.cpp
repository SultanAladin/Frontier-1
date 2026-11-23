//============================================================================================================================================
//                                              WORLDSKETCHSCREENPICKING.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorldSketchPicking/Api/WorldSketchScreenPicking.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slate
{

namespace
{

struct PickMetric
{
    double ScreenDistance = 1.0e300;
    double Depth = 1.0e300;
};

bool ResolveCameraRay(const ResolvedCamera& Camera,
                      const PlaneExtent& Extent,
                      float ScreenX,
                      float ScreenY,
                      SpatialPoint& RayOrigin,
                      SpatialDirection& RayDirection)
{
    const double CentreX = Extent.MinimumX + Extent.Width() * 0.5;
    const double CentreY = Extent.MinimumY + Extent.Height() * 0.5;
    const double NdcX = (static_cast<double>(ScreenX) - CentreX)
                      / std::max(static_cast<double>(Extent.Width()) * 0.5, 1.0);
    const double NdcY = (CentreY - static_cast<double>(ScreenY))
                      / std::max(static_cast<double>(Extent.Height()) * 0.5, 1.0);

    if (!Camera.Perspective)
    {
        const double Along = NdcX / std::max(Camera.OrthoScale, 0.001) * (Extent.Width() * 0.5);
        const double Upward = NdcY / std::max(Camera.OrthoScale, 0.001) * (Extent.Height() * 0.5);
        RayOrigin = Added(Camera.Frame.Eye,
                          Added(Scaled(Camera.Frame.Right, Along),
                                Scaled(Camera.Frame.Up, Upward)));
        RayDirection = Normalize(Camera.Frame.Forward);
        return true;
    }

    const double TanHalf = std::tan(Camera.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
    const double Aspect = Extent.Width() / std::max(Extent.Height(), 1.0f);
    RayOrigin = Camera.Frame.Eye;
    RayDirection = Normalize(Added(Added(Scaled(Camera.Frame.Right, NdcX * TanHalf * Aspect),
                                          Scaled(Camera.Frame.Up, NdcY * TanHalf)),
                                    Camera.Frame.Forward));
    return true;
}

double ResolveCameraDepth(const ResolvedCamera& Camera,
                          const SpatialPoint& Position)
{
    return Dot(Difference(Camera.Frame.Eye, Position), Camera.Frame.Forward);
}

bool CandidateVisible(const ResolvedCamera& Camera,
                      double Depth)
{
    return !Camera.Perspective || Depth > 0.01;
}

bool BetterMetric(const PickMetric& Candidate,
                  const PickMetric& Current)
{
    if (Candidate.ScreenDistance + 1.0e-9 < Current.ScreenDistance)
        return true;
    if (std::fabs(Candidate.ScreenDistance - Current.ScreenDistance) <= 1.0e-9)
        return Candidate.Depth < Current.Depth;
    return false;
}

double DistancePointSegmentSquared2(const double X,
                                    const double Y,
                                    const double X0,
                                    const double Y0,
                                    const double X1,
                                    const double Y1)
{
    const double DX = X1 - X0;
    const double DY = Y1 - Y0;
    const double LengthSquared = DX * DX + DY * DY;
    if (LengthSquared <= 1.0e-18)
    {
        const double RX = X - X0;
        const double RY = Y - Y0;
        return RX * RX + RY * RY;
    }

    const double Parameter = std::clamp(((X - X0) * DX + (Y - Y0) * DY) / LengthSquared, 0.0, 1.0);
    const double HitX = X0 + DX * Parameter;
    const double HitY = Y0 + DY * Parameter;
    const double RX = X - HitX;
    const double RY = Y - HitY;
    return RX * RX + RY * RY;
}

bool LoopContainsCurve(const DeclaredWorldLoop& Loop,
                       WorldCurveName Curve)
{
    for (const WorldCurveUse& Use : Loop.Traversal)
        if (Use.TraversedCurve.IssuedIndex == Curve.IssuedIndex)
            return true;
    return false;
}

const DeclaredWorldCurve* ResolveCurve(const WorldSketchStructure& Declared,
                                       WorldCurveName SourceCurve)
{
    return Declared.Resolve(SourceCurve);
}

bool ResolveCurvePolyline(const WorldSketchStructure& Declared,
                          WorldCurveName Curve,
                          std::vector<SpatialPoint>& Polyline)
{
    Polyline.clear();
    const DeclaredWorldCurve* Held = ResolveCurve(Declared, Curve);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    AppendCurvePolyline(Held->Geometry, Polyline, 48u);
    return Polyline.size() >= 2u;
}

bool ResolveLoopOwnerForCurve(const WorldSketchStructure& Declared,
                              WorldCurveName Curve,
                              WorldLoopName& Owner)
{
    Owner = {};
    for (std::uint32_t LoopIndex = 1u; LoopIndex <= Declared.LoopCount(); ++LoopIndex)
    {
        const DeclaredWorldLoop* Loop = Declared.Resolve(WorldLoopName{ LoopIndex });
        if (Loop != nullptr && LoopContainsCurve(*Loop, Curve))
        {
            Owner = { LoopIndex };
            return true;
        }
    }
    return false;
}

bool PickingPointInsideLoop(const WorldPlacementFrame& Frame,
                            const std::vector<SpatialPoint>& Loop,
                            const SpatialPoint& Probe)
{
    if (Loop.size() < 3u || !Frame.Declared())
        return false;

    std::vector<double> Alongs;
    std::vector<double> Acrosses;
    Alongs.reserve(Loop.size());
    Acrosses.reserve(Loop.size());
    for (const SpatialPoint& Point : Loop)
    {
        double Along = 0.0;
        double Across = 0.0;
        ResolveWorldPlacementCoordinates(Frame, Point, Along, Across);
        Alongs.push_back(Along);
        Acrosses.push_back(Across);
    }

    double ProbeAlong = 0.0;
    double ProbeAcross = 0.0;
    ResolveWorldPlacementCoordinates(Frame, Probe, ProbeAlong, ProbeAcross);

    bool Inside = false;
    for (std::size_t Index = 0u, Prior = Loop.size() - 1u; Index < Loop.size(); Prior = Index++)
    {
        const bool Crosses = ((Acrosses[Index] > ProbeAcross) != (Acrosses[Prior] > ProbeAcross))
                          && (ProbeAlong < (Alongs[Prior] - Alongs[Index])
                                          * (ProbeAcross - Acrosses[Index])
                                          / ((Acrosses[Prior] - Acrosses[Index]) + 1.0e-300)
                                          + Alongs[Index]);
        if (Crosses)
            Inside = !Inside;
    }
    return Inside;
}

bool ResolveNearestWorldSketchPoint(const WorldSketchStructure& Declared,
                                   const ResolvedCamera& Camera,
                                   const PlaneExtent& Extent,
                                   float ScreenX,
                                   float ScreenY,
                                   double MaximumDistancePixels,
                                   WorldPointPlacement& Resolved,
                                   PickMetric& Metric)
{
    Metric = {};
    std::vector<WorldPointPlacement> PointSet;
    bool Found = false;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        if (!ResolveWorldSketchPoints(Declared, { CurveIndex }, PointSet))
            continue;

        for (const WorldPointPlacement& Point : PointSet)
        {
            const double Depth = ResolveCameraDepth(Camera, Point.Position);
            if (!CandidateVisible(Camera, Depth))
                continue;

            float CandidateX = 0.0f;
            float CandidateY = 0.0f;
            if (!ProjectFromCamera(Camera, Extent, Point.Position, CandidateX, CandidateY))
                continue;

            const double DX = static_cast<double>(CandidateX) - static_cast<double>(ScreenX);
            const double DY = static_cast<double>(CandidateY) - static_cast<double>(ScreenY);
            const PickMetric Candidate = { std::sqrt(DX * DX + DY * DY), Depth };
            if (Candidate.ScreenDistance <= MaximumDistancePixels && (!Found || BetterMetric(Candidate, Metric)))
            {
                Metric = Candidate;
                Resolved = Point;
                Found = true;
            }
        }
    }

    return Found;
}

bool ResolveNearestWorldSketchControl(const WorldSketchStructure& Declared,
                                     const ResolvedCamera& Camera,
                                     const PlaneExtent& Extent,
                                     float ScreenX,
                                     float ScreenY,
                                     double MaximumDistancePixels,
                                     WorldControlPlacement& Resolved,
                                     PickMetric& Metric)
{
    Metric = {};
    std::vector<WorldControlPlacement> ControlSet;
    bool Found = false;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        if (!ResolveWorldSketchControls(Declared, { CurveIndex }, ControlSet))
            continue;

        for (const WorldControlPlacement& Control : ControlSet)
        {
            const double Depth = ResolveCameraDepth(Camera, Control.Position);
            if (!CandidateVisible(Camera, Depth))
                continue;

            float CandidateX = 0.0f;
            float CandidateY = 0.0f;
            if (!ProjectFromCamera(Camera, Extent, Control.Position, CandidateX, CandidateY))
                continue;

            const double DX = static_cast<double>(CandidateX) - static_cast<double>(ScreenX);
            const double DY = static_cast<double>(CandidateY) - static_cast<double>(ScreenY);
            const PickMetric Candidate = { std::sqrt(DX * DX + DY * DY), Depth };
            if (Candidate.ScreenDistance <= MaximumDistancePixels && (!Found || BetterMetric(Candidate, Metric)))
            {
                Metric = Candidate;
                Resolved = Control;
                Found = true;
            }
        }
    }

    return Found;
}

bool ResolveNearestWorldSketchCurve(const WorldSketchStructure& Declared,
                                   const ResolvedCamera& Camera,
                                   const PlaneExtent& Extent,
                                   float ScreenX,
                                   float ScreenY,
                                   double MaximumDistancePixels,
                                   WorldCurveName& Resolved,
                                   SpatialPoint& HitPosition,
                                   PickMetric& Metric)
{
    Metric = {};
    std::vector<SpatialPoint> Polyline;
    bool Found = false;

    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Declared.CurveCount(); ++CurveIndex)
    {
        if (!ResolveCurvePolyline(Declared, { CurveIndex }, Polyline))
            continue;

        for (std::size_t PointIndex = 0u; PointIndex + 1u < Polyline.size(); ++PointIndex)
        {
            float X0 = 0.0f;
            float Y0 = 0.0f;
            float X1 = 0.0f;
            float Y1 = 0.0f;
            if (!ProjectFromCamera(Camera, Extent, Polyline[PointIndex], X0, Y0) ||
                !ProjectFromCamera(Camera, Extent, Polyline[PointIndex + 1u], X1, Y1))
                continue;

            const double CandidateDistance = std::sqrt(DistancePointSegmentSquared2(
                ScreenX, ScreenY, X0, Y0, X1, Y1));
            if (CandidateDistance > MaximumDistancePixels)
                continue;

            const double DX = static_cast<double>(X1) - static_cast<double>(X0);
            const double DY = static_cast<double>(Y1) - static_cast<double>(Y0);
            const double LengthSquared = DX * DX + DY * DY;
            const double Parameter = LengthSquared > 1.0e-18
                                   ? std::clamp(((static_cast<double>(ScreenX) - X0) * DX
                                               + (static_cast<double>(ScreenY) - Y0) * DY) / LengthSquared,
                                                0.0, 1.0)
                                   : 0.0;
            const SpatialPoint CandidateHit = Added(Polyline[PointIndex],
                                                    Scaled(Difference(Polyline[PointIndex], Polyline[PointIndex + 1u]),
                                                           Parameter));
            const double Depth = ResolveCameraDepth(Camera, CandidateHit);
            if (!CandidateVisible(Camera, Depth))
                continue;

            const PickMetric Candidate = { CandidateDistance, Depth };
            if (!Found || BetterMetric(Candidate, Metric))
            {
                Metric = Candidate;
                Resolved = { CurveIndex };
                HitPosition = CandidateHit;
                Found = true;
            }
        }
    }

    return Found;
}

bool ResolveNearestWorldSketchLoop(const WorldSketchStructure& Declared,
                                  const ResolvedCamera& Camera,
                                  const PlaneExtent& Extent,
                                  float ScreenX,
                                  float ScreenY,
                                  WorldLoopName& Resolved,
                                  SpatialPoint& HitPosition,
                                  PickMetric& Metric)
{
    Metric = {};
    SpatialPoint RayOrigin = {};
    SpatialDirection RayDirection = {};
    if (!ResolveCameraRay(Camera, Extent, ScreenX, ScreenY, RayOrigin, RayDirection))
        return false;

    const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Declared);
    bool Found = false;

    for (const WorldLoopAnalysisRecord& Loop : Analysis.Loops)
    {
        if (!Loop.FillEligible || !Loop.SupportFrame.Declared() || Loop.Outline.size() < 3u)
            continue;

        SpatialPoint Hit = {};
        if (!ResolveWorldPlacementIntersection(Loop.SupportFrame, RayOrigin, RayDirection, Hit))
            continue;
        if (!PickingPointInsideLoop(Loop.SupportFrame, Loop.Outline, Hit))
            continue;

        const double Depth = ResolveCameraDepth(Camera, Hit);
        if (!CandidateVisible(Camera, Depth))
            continue;

        const PickMetric Candidate = { 0.0, Depth };
        if (!Found || BetterMetric(Candidate, Metric))
        {
            Metric = Candidate;
            Resolved = Loop.Loop;
            HitPosition = Hit;
            Found = true;
        }
    }

    return Found;
}

} // namespace

bool ResolveWorldSketchPick(const WorldSketchStructure& Declared,
                           const ResolvedCamera& Camera,
                           const PlaneExtent& Extent,
                           float ScreenX,
                           float ScreenY,
                           double MaximumDistancePixels,
                           WorldPick& Resolved)
{
    return ResolveWorldSketchPickForElement(Declared, Camera, Extent, ScreenX, ScreenY,
                                           MaximumDistancePixels, SelectionElement::Free, Resolved);
}

bool ResolveWorldSketchPickForElement(const WorldSketchStructure& Declared,
                                     const ResolvedCamera& Camera,
                                     const PlaneExtent& Extent,
                                     float ScreenX,
                                     float ScreenY,
                                     double MaximumDistancePixels,
                                     SelectionElement Element,
                                     WorldPick& Resolved)
{
    Resolved = {};

    if (Element == SelectionElement::Vertex || Element == SelectionElement::Free)
    {
        WorldPointPlacement Point = {};
        PickMetric Metric = {};
        if (ResolveNearestWorldSketchPoint(Declared, Camera, Extent, ScreenX, ScreenY,
                                          MaximumDistancePixels, Point, Metric))
        {
            Resolved.Subject = WorldPickSubject::Point;
            Resolved.Point = Point.Name;
            Resolved.Curve = Point.SourceCurve;
            Resolved.Position = Point.Position;
            return true;
        }

        WorldControlPlacement Control = {};
        if (ResolveNearestWorldSketchControl(Declared, Camera, Extent, ScreenX, ScreenY,
                                            MaximumDistancePixels, Control, Metric))
        {
            Resolved.Subject = WorldPickSubject::Control;
            Resolved.Control = Control.Name;
            Resolved.Curve = Control.SourceCurve;
            Resolved.Position = Control.Position;
            return true;
        }

        if (Element == SelectionElement::Vertex)
            return false;
    }

    if (Element == SelectionElement::Edge || Element == SelectionElement::Free)
    {
        PickMetric Metric = {};
        SpatialPoint Hit = {};
        WorldCurveName Curve = {};
        if (ResolveNearestWorldSketchCurve(Declared, Camera, Extent, ScreenX, ScreenY,
                                          MaximumDistancePixels, Curve, Hit, Metric))
        {
            Resolved.Subject = WorldPickSubject::Curve;
            Resolved.Curve = Curve;
            Resolved.Position = Hit;
            return true;
        }

        if (Element == SelectionElement::Edge)
            return false;
    }

    if (Element == SelectionElement::Face || Element == SelectionElement::Object || Element == SelectionElement::Free)
    {
        PickMetric Metric = {};
        SpatialPoint Hit = {};
        WorldLoopName Loop = {};
        if (ResolveNearestWorldSketchLoop(Declared, Camera, Extent, ScreenX, ScreenY, Loop, Hit, Metric))
        {
            Resolved.Subject = WorldPickSubject::Loop;
            Resolved.Loop = Loop;
            if (!ResolveWorldLoopPivot(Declared, Loop, Resolved.Position))
                Resolved.Position = Hit;
            return true;
        }

        if (Element == SelectionElement::Face)
            return false;

        if (Element == SelectionElement::Object)
        {
            SpatialPoint CurveHit = {};
            WorldCurveName Curve = {};
            if (ResolveNearestWorldSketchCurve(Declared, Camera, Extent, ScreenX, ScreenY,
                                              MaximumDistancePixels, Curve, CurveHit, Metric))
            {
                WorldLoopName Owner = {};
                if (ResolveLoopOwnerForCurve(Declared, Curve, Owner))
                {
                    Resolved.Subject = WorldPickSubject::Loop;
                    Resolved.Loop = Owner;
                    if (!ResolveWorldLoopPivot(Declared, Owner, Resolved.Position))
                        Resolved.Position = CurveHit;
                    return true;
                }

                Resolved.Subject = WorldPickSubject::Curve;
                Resolved.Curve = Curve;
                Resolved.Position = CurveHit;
                return true;
            }
        }
    }

    return false;
}

} // namespace Slate
