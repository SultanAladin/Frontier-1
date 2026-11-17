/*====================================================================================================================================
                                                        PLANESECTION.CPP
====================================================================================================================================*/
// 🧩 Exact plane section of a solid: the cut region's area, perimeter, centroid, second moments and outline

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Measurement/PlaneSection.h"
#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Feature/BodyCut.h"
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

HalfSpaceCut CutBodyAtPlane(ModelPartition& Scratch, BodyToken Body, const OrthonormalFrame& Plane)
{
    HalfSpaceCut Kept;
    bool SolidCondition = false;
    for (RegionToken Region : Scratch.Bodies.Resolve(Body).Regions) SolidCondition = SolidCondition || Scratch.Regions.Resolve(Region).Material == RegionMaterial::Solid;
    if (!SolidCondition) { Kept.Refusal = "Section: the body is a sheet"; return Kept; }

    // ---- ① the cut: a chain along Plane.YAxis on the plane through Plane.XAxis; its left side is +Plane.ZAxis
    const double   Reach   = MeasureCutReach(Scratch, Body, Plane);                          // [len] - Body radius about the origin
    Kept.Reach = Reach;
    const OrthonormalFrame Through = { Plane.Origin, Plane.YAxis, Plane.ZAxis, Plane.XAxis };
    ProfileSegment Chain;
    Chain.Kind  = ProfileSegmentKind::Line;
    Chain.Start = { -2.0 * Reach, 0.0 };
    Chain.End   = { 2.0 * Reach, 0.0 };
    Chain.Name  = "Section";
    const SketchProfile Profile = CloseCutChain({ Chain }, Reach);
    if (!Profile.Refusal.empty()) { Kept.Refusal = "Section: " + Profile.Refusal; return Kept; }
    const BodyCutOutcome Cut = CutBody(Scratch, Body, Profile, Through, Reach);
    // 📝 A cut that keeps nothing on the +ZAxis side is a plane beyond the body (or on a face whose normal points away).
    if (!Cut.Refusal.empty() && Cut.Refusal.find("misses") != std::string::npos) { Kept.Refusal = "Section: the plane misses the body"; return Kept; }
    if (!Cut.Refusal.empty()) { Kept.Refusal = "Section: " + Cut.Refusal.substr(Cut.Refusal.rfind("Cut: ", 0) == 0 ? 5 : 0); return Kept; }
    if (!Cut.Inside.Refusal.empty() || !Cut.Inside.Body.PresentCondition()) { Kept.Refusal = "Section: the plane misses the body"; return Kept; }
    Kept.Body = Cut.Inside.Body;

    // ---- ② cap faces: planar, normal ∥ Plane.ZAxis, on the plane
    const double Tolerance = 1.0e-9 * std::max(1.0, Reach);                                  // [len] - On-plane test
    for (FaceToken Face : Scratch.CollectInventory(Cut.Inside.Body).Faces)
    {
        const FaceProfile& Profile = Scratch.Faces.Resolve(Face);
        if (!Profile.Surface.PresentCondition()) continue;
        const auto* Flat = std::get_if<PlaneSurface>(&Scratch.Surfaces.Resolve(Profile.Surface));
        if (Flat == nullptr) continue;
        if (Length(Cross(Flat->Frame.ZAxis, Plane.ZAxis)) > 1.0e-9) continue;
        if (std::fabs(Dot(Flat->Frame.Origin - Plane.Origin, Plane.ZAxis)) > Tolerance) continue;
        Kept.Caps.push_back(Face);
    }
    if (Kept.Caps.empty()) Kept.Refusal = "Section: the plane misses the body";
    return Kept;
}

PlaneSectionMeasure SectionBody(const ModelPartition& Partition, BodyToken Body, const OrthonormalFrame& Plane, double OutlineStep)
{
    const auto          Start = std::chrono::steady_clock::now();
    PlaneSectionMeasure Measure;
    const auto Finish = [&]() -> PlaneSectionMeasure
    {
        Measure.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        return Measure;
    };
    ModelPartition     Scratch = Partition;
    const HalfSpaceCut Cut     = CutBodyAtPlane(Scratch, Body, Plane);
    if (!Cut.Refusal.empty()) { Measure.Refusal = Cut.Refusal; return Finish(); }
    const double                  Reach     = Cut.Reach;
    const double                  Tolerance = 1.0e-9 * std::max(1.0, Reach);                // [len] - On-plane test
    const std::vector<FaceToken>& Caps      = Cut.Caps;

    // ---- ③ area, centroid and second moments in plane coordinates
    const auto U = [&](Vector3 Position) { return Dot(Position - Plane.Origin, Plane.XAxis); };
    const auto V = [&](Vector3 Position) { return Dot(Position - Plane.Origin, Plane.YAxis); };
    double MomentU = 0.0, MomentV = 0.0;
    for (FaceToken Face : Caps)
    {
        Measure.Area += IntegrateFaceSurface(Scratch, Face, [](Vector3) { return 1.0; });
        MomentU      += IntegrateFaceSurface(Scratch, Face, U);
        MomentV      += IntegrateFaceSurface(Scratch, Face, V);
        Measure.Loops += static_cast<int>(Scratch.Faces.Resolve(Face).Loops.size());
    }
    if (!(Measure.Area > Tolerance * Tolerance)) { Measure.Refusal = "Section: the plane only touches the body"; return Finish(); }
    const double CentreU = MomentU / Measure.Area, CentreV = MomentV / Measure.Area;         // [len] - 𝑢̄, 𝑣̄
    Measure.Centroid = Plane.Origin + Plane.XAxis * CentreU + Plane.YAxis * CentreV;
    for (FaceToken Face : Caps)
    {
        Measure.SecondMoments[0] += IntegrateFaceSurface(Scratch, Face, [&](Vector3 Position) { const double Offset = V(Position) - CentreV; return Offset * Offset; });
        Measure.SecondMoments[1] += IntegrateFaceSurface(Scratch, Face, [&](Vector3 Position) { const double Offset = U(Position) - CentreU; return Offset * Offset; });
        Measure.SecondMoments[2] += IntegrateFaceSurface(Scratch, Face, [&](Vector3 Position) { return (U(Position) - CentreU) * (V(Position) - CentreV); });
    }
    Measure.Regions = static_cast<int>(Caps.size());

    // ---- ④ perimeter and outline (each boundary edge once)
    const double Step = OutlineStep > 0.0 ? OutlineStep : 0.01 * std::max(Reach, 1.0e-6);    // [len] - Outline chord bound
    std::set<uint32_t> Seen;
    for (FaceToken Face : Caps)
    {
        for (LoopToken Loop : Scratch.Faces.Resolve(Face).Loops)
        {
            Scratch.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const EdgeToken Edge = Scratch.Fins.Resolve(Fin).Edge;
                if (!Seen.insert(Edge.Slot).second) return;
                const double EdgeLength = MeasureEdgeLength(Scratch, Edge);
                Measure.Perimeter += EdgeLength;
                const EdgeProfile&   Profile = Scratch.Edges.Resolve(Edge);
                const CurveGeometry& Curve   = Scratch.Curves.Resolve(Profile.Curve);
                const int Pieces = std::holds_alternative<StraightLine>(Curve) ? 1 : std::clamp(static_cast<int>(std::ceil(EdgeLength / Step)), 8, 512);
                std::vector<Vector3> Polyline;
                for (int Index = 0; Index <= Pieces; ++Index) Polyline.push_back(EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Index / Pieces).Position);
                Measure.Outline.push_back(std::move(Polyline));
            });
        }
    }
    return Finish();
}

} // namespace Frontier::GeometrySpace::Parametric
